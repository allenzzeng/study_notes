#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <concepts>

// 线程池类
class ThreadPool {
public:
    // 构造函数，默认使用硬件支持的线程数
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency())
        : stop(false) {  // 初始化停止标志为false
        // 创建指定数量的工作线程
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {  // 每个线程执行以下lambda函数
                for (;;) {  // 无限循环，直到线程池停止
                    std::function<void()> task;  // 用于存储待执行的任务

                    {  // 加锁区域开始
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        // 等待条件变量：当线程池停止或任务队列不为空时继续
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                            });

                        // 如果线程池已停止且任务队列为空，则线程退出
                        if (this->stop && this->tasks.empty())
                            return;

                        // 从任务队列中取出一个任务
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }  // 加锁区域结束，自动释放锁

                    task();  // 执行任务
                }
                });
        }
    }

    // 向线程池添加任务（模板函数）
    template<typename F, typename... Args>
        requires std::invocable<F, Args...>  // C++20概念：确保F可以使用Args...调用
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {  // 返回任务结果的future

        using return_type = std::invoke_result_t<F, Args...>;  // 获取调用结果的类型

        // 将任务包装成packaged_task，以便获取future
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        // 获取任务的future对象
        std::future<return_type> res = task->get_future();
        {  // 加锁区域开始
            std::unique_lock<std::mutex> lock(queue_mutex);

            // 如果线程池已停止，则不允许添加新任务
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            // 将任务添加到队列（使用lambda包装packaged_task）
            tasks.emplace([task]() { (*task)(); });
        }  // 加锁区域结束，自动释放锁

        condition.notify_one();  // 通知一个等待的线程有新任务
        return res;  // 返回future对象
    }

    // 析构函数
    ~ThreadPool() {
        {  // 加锁区域开始
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;  // 设置停止标志
        }  // 加锁区域结束，自动释放锁

        condition.notify_all();  // 通知所有线程
        for (std::thread& worker : workers)
            worker.join();  // 等待所有线程结束
    }

private:
    std::vector<std::thread> workers;  // 工作线程集合
    std::queue<std::function<void()>> tasks;  // 任务队列

    std::mutex queue_mutex;  // 任务队列的互斥锁
    std::condition_variable condition;  // 条件变量，用于线程间通信
    bool stop;  // 线程池停止标志
};
