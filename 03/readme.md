# 03 模块：TinyHttpd 简易 HTTP 服务器实现

## 模块简介
本模块实现了一个轻量级的 HTTP 服务器（TinyHttpd），支持基本的 HTTP 请求处理和路由管理。服务器采用多线程模型处理客户端连接，能够解析 HTTP 请求并返回相应的响应，适合作为网络编程和 HTTP 协议的学习案例。


## 核心功能

### 1. 服务器基础架构
- **类设计**：核心类 `TinyHttpd` 封装了服务器的启动、停止、路由管理和连接处理逻辑
- **跨平台支持**：通过条件编译适配 Windows（使用 `WSAStartup`）和类 Unix 系统的套接字接口
- **生命周期管理**：构造函数初始化路由表，析构函数自动停止服务器并释放资源


### 2. 网络通信
- **套接字操作**：实现了 socket 创建、绑定（bind）、监听（listen）和接受连接（accept）的完整流程
- **地址复用**：通过 `SO_REUSEADDR` 选项允许端口快速重用，避免服务器重启时的地址占用问题
- **多线程处理**：对每个客户端连接创建独立线程处理，避免单线程阻塞（使用 `std::thread` 并 detach）


### 3. 路由与请求处理
- **路由管理**：通过 `add_route` 方法注册 HTTP 方法（如 GET）和路径（如 `/`）对应的处理函数
- **请求解析**：`HttpRequest` 类负责解析客户端发送的 HTTP 请求（包含方法、路径等信息）
- **响应生成**：`HttpResponse` 提供文本响应（`make_text`）和 404 响应（`make_404`）的生成方法，并通过 `send` 发送给客户端


## 核心代码解析

### 服务器启动流程（`start` 方法）
1. **初始化网络库**：Windows 平台下调用 `WSAStartup` 初始化 Winsock
2. **创建套接字**：使用 `socket(AF_INET, SOCK_STREAM, 0)` 创建 TCP 套接字
3. **设置选项**：通过 `setsockopt` 启用地址复用
4. **绑定地址**：将套接字绑定到指定端口（默认 8080）
5. **监听连接**：开始监听客户端连接请求（`listen`）
6. **接受连接**：循环调用 `accept` 接受客户端连接，并创建线程处理（`handle_connection`）


### 连接处理（`handle_connection` 方法）
1. **解析请求**：通过 `HttpRequest::parse` 解析客户端发送的 HTTP 请求
2. **路由匹配**：根据请求的方法和路径在路由表中查找对应的处理函数
3. **生成响应**：调用匹配的处理函数生成响应，或返回 404 响应
4. **发送响应**：通过 `HttpResponse::send` 将响应发送给客户端


## 编译与运行

### 编译要求
- 支持 C++11 及以上标准的编译器（需支持线程库）
- Windows 平台需链接 `ws2_32.lib`（ Winsock 库）


### 运行步骤
1. 编译 `tinyhttpd.cpp` 及相关依赖文件（包含 `HttpRequest`、`HttpResponse` 实现）
2. 启动服务器：
   ```bash
   ./tinyhttpd
   ```
   服务器默认在 8080 端口启动，控制台将输出 `Server started on port 8080`

3. 测试服务器：
   - 使用浏览器访问 `http://127.0.0.1:8080`，将看到 `Welcome to TinyHttpd!`
   - 访问不存在的路径（如 `http://127.0.0.1:8080/abc`），将收到 404 响应


## 扩展与定制
- **添加路由**：通过 `add_route` 方法注册自定义路由，例如：
  ```cpp
  TinyHttpd server;
  server.add_route("GET", "/about", [](const HttpRequest&) {
      return HttpResponse::make_text("This is a tiny HTTP server.");
  });
  ```
- **修改端口**：启动服务器时指定端口：
  ```cpp
  server.start(8081);  // 在 8081 端口启动
  ```


## 技术要点
- **套接字编程**：掌握 TCP 服务器的完整工作流程（socket -> bind -> listen -> accept -> recv/send）
- **多线程模型**：通过线程池或独立线程处理并发连接（当前实现为每个连接创建线程，可优化为线程池）
- **HTTP 协议**：了解 HTTP 请求/响应的基本格式，掌握请求解析和响应生成的逻辑
- **资源管理**：使用 RAII 思想管理套接字和线程资源，避免内存泄漏和资源泄露


## 注意事项
- 当前实现为简化版本，未处理大量并发连接的性能优化（可引入线程池或 I/O 多路复用改进）
- 未完全实现 HTTP 协议规范（如头部解析、Chunked 传输等），适合学习基础原理
- Windows 平台需注意套接字关闭和 `WSACleanup` 的调用，避免资源泄露

本模块可结合 RFC 2616（HTTP 1.1 规范）深入学习 HTTP 协议细节，或通过引入 `epoll`/`select` 等机制提升服务器的并发处理能力。
