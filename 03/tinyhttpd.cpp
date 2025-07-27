#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cerrno>
#endif

using namespace std;

struct PairHash {
    template <class T1, class T2>
    size_t operator()(const pair<T1, T2>& p) const {
        auto hash1 = hash<T1>{}(p.first);
        auto hash2 = hash<T2>{}(p.second);
        return hash1 ^ (hash2 << 1);
    }
};

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCKET_VALUE = INVALID_SOCKET;
constexpr int SOCKET_ERROR_VALUE = SOCKET_ERROR;
#else
using socket_t = int;
constexpr socket_t INVALID_SOCKET_VALUE = -1;
constexpr int SOCKET_ERROR_VALUE = -1;
#endif

#ifdef _WIN32
#define CLOSE_SOCKET closesocket
#else
#define CLOSE_SOCKET close
#endif

class HttpRequest {
public:
    string method;
    string path;
    string version;
    unordered_map<string, string> headers;
    string body;

    bool parse(socket_t client_socket) {
        constexpr size_t BUFFER_SIZE = 8192;
        string request_data;
        char buffer[BUFFER_SIZE];

        while (true) {
            int bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if (bytes_read <= 0) break;
            request_data.append(buffer, bytes_read);
            if (request_data.find("\r\n\r\n") != string::npos) break;
        }

        if (request_data.empty()) return false;

        size_t line_end = request_data.find("\r\n");
        if (line_end == string::npos) return false;

        parse_request_line(string_view(request_data).substr(0, line_end));

        size_t headers_start = line_end + 2;
        size_t headers_end = request_data.find("\r\n\r\n", headers_start);
        if (headers_end == string::npos) return false;

        parse_headers(string_view(request_data).substr(headers_start, headers_end - headers_start));

        size_t body_start = headers_end + 4;
        if (headers.count("Content-Length")) {
            size_t content_length = stoul(headers["Content-Length"]);
            if (request_data.size() < body_start + content_length) {
                // 继续读取请求体
                while (request_data.size() < body_start + content_length) {
                    int bytes_read = recv(client_socket, buffer, BUFFER_SIZE, 0);
                    if (bytes_read <= 0) return false;
                    request_data.append(buffer, bytes_read);
                }
            }
            body = request_data.substr(body_start, content_length);
        }

        return true;
    }

private:
    void parse_request_line(string_view line) {
        size_t method_end = line.find(' ');
        if (method_end == string_view::npos) return;
        method = string(line.substr(0, method_end));

        size_t path_end = line.find(' ', method_end + 1);
        if (path_end == string_view::npos) return;
        path = string(line.substr(method_end + 1, path_end - method_end - 1));

        version = string(line.substr(path_end + 1));
    }

    void parse_headers(string_view headers_data) {
        size_t start = 0;
        while (start < headers_data.length()) {
            size_t end = headers_data.find("\r\n", start);
            if (end == string_view::npos) break;

            string_view line = headers_data.substr(start, end - start);
            size_t colon = line.find(':');
            if (colon != string_view::npos) {
                string key = string(line.substr(0, colon));
                string value = string(line.substr(colon + 1));
                size_t first = value.find_first_not_of(" \t");
                size_t last = value.find_last_not_of(" \t");
                if (first != string::npos && last != string::npos)
                    value = value.substr(first, last - first + 1);
                headers[move(key)] = move(value);
            }

            start = end + 2;
        }
    }
};

class HttpResponse {
public:
    int status_code = 200;
    string status_text = "OK";
    unordered_map<string, string> headers;
    string body;

    string build() const {
        stringstream response;
        response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
        for (const auto& [k, v] : headers) {
            response << k << ": " << v << "\r\n";
        }
        response << "\r\n" << body;
        return response.str();
    }

    void send(socket_t client_socket) const {
        string data = build();
        size_t total_sent = 0;
        while (total_sent < data.size()) {
            int sent = ::send(client_socket, data.c_str() + total_sent,
                              static_cast<int>(data.size() - total_sent), 0);
            if (sent <= 0) break;
            total_sent += sent;
        }
    }

    static HttpResponse make_404() {
        HttpResponse res;
        res.status_code = 404;
        res.status_text = "Not Found";
        res.body = "<h1>404 Not Found</h1>";
        res.headers["Content-Type"] = "text/html";
        res.headers["Content-Length"] = to_string(res.body.size());
        return res;
    }

    static HttpResponse make_text(string content) {
        HttpResponse res;
        res.body = move(content);
        res.headers["Content-Type"] = "text/plain";
        res.headers["Content-Length"] = to_string(res.body.size());
        return res;
    }

    static HttpResponse get_current_time() {
        auto now = chrono::system_clock::now();
        time_t t = chrono::system_clock::to_time_t(now);
        stringstream ss;
        ss << put_time(localtime(&t), "%Y-%m-%d %H:%M:%S");
        return make_text(ss.str());
    }
};

class TinyHttpd {
public:
    using Handler = function<HttpResponse(const HttpRequest&)>;

    TinyHttpd() : stop_flag(false), server_socket(INVALID_SOCKET_VALUE) {
        add_route("GET", "/", [](const HttpRequest&) {
            return HttpResponse::make_text("Welcome to TinyHttpd!");
        });
    }

    ~TinyHttpd() { stop(); }

    void add_route(string method, string path, Handler handler) {
        lock_guard<mutex> lock(routes_mutex);
        routes[{move(method), move(path)}] = move(handler);
    }

    void start(int port = 8080, int backlog = 10) {
#ifdef _WIN32
        WSADATA wsaData;
        int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (wsa_result != 0) {
            cerr << "WSAStartup failed: " << wsa_result << "\n";
            return;
        }
#endif
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == INVALID_SOCKET_VALUE) {
            perror("Socket creation failed");
            return;
        }

        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (::bind(server_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_VALUE) {
            perror("Bind failed");
            CLOSE_SOCKET(server_socket);
#ifdef _WIN32
            WSACleanup();
#endif
            return;
        }

        if (listen(server_socket, backlog) == SOCKET_ERROR_VALUE) {
            perror("Listen failed");
            CLOSE_SOCKET(server_socket);
#ifdef _WIN32
            WSACleanup();
#endif
            return;
        }

        cout << "Server started on port " << port << endl;

        while (!stop_flag) {
            sockaddr_in client_addr{};
            socklen_t len = sizeof(client_addr);
            socket_t client_socket = accept(server_socket, reinterpret_cast<sockaddr*>(&client_addr), &len);
            if (client_socket == INVALID_SOCKET_VALUE) {
                if (!stop_flag) perror("Accept failed");
                continue;
            }

            thread([this, client_socket]() {
                handle_connection(client_socket);
                CLOSE_SOCKET(client_socket);
            }).detach();
        }

        CLOSE_SOCKET(server_socket);
#ifdef _WIN32
        WSACleanup();
#endif
    }

    void stop() {
        stop_flag = true;
        if (server_socket != INVALID_SOCKET_VALUE) {
#ifdef _WIN32
            shutdown(server_socket, SD_BOTH);
#else
            shutdown(server_socket, SHUT_RDWR);
#endif
            CLOSE_SOCKET(server_socket);
            server_socket = INVALID_SOCKET_VALUE;
        }
    }

private:
    socket_t server_socket;
    atomic<bool> stop_flag;
    mutex routes_mutex;
    unordered_map<pair<string, string>, Handler, PairHash> routes;

    void handle_connection(socket_t client_socket) {
        HttpRequest req;
        if (!req.parse(client_socket)) {
            HttpResponse::make_404().send(client_socket);
            return;
        }

        cout << req.method << " " << req.path << endl;

        Handler handler;
        {
            lock_guard<mutex> lock(routes_mutex);
            auto it = routes.find({req.method, req.path});
            if (it != routes.end()) handler = it->second;
        }

        if (handler)
            handler(req).send(client_socket);
        else
            HttpResponse::make_404().send(client_socket);
    }
};

int main() {
    TinyHttpd server;

    server.add_route("GET", "/hello", [](const HttpRequest&) {
        return HttpResponse::make_text("Hello, World!");
    });

    server.add_route("GET", "/time", [](const HttpRequest&) {
        return HttpResponse::get_current_time();
    });

    server.add_route("POST", "/echo", [](const HttpRequest& req) {
        return HttpResponse::make_text(req.body);
    });

    thread server_thread([&]() {
        server.start(8080);
    });

    cout << "Press Enter to stop the server...\n";
    cin.get();
    server.stop();
    server_thread.join();
    return 0;
}
