#include <iostream>
#include <vector>
#include <chrono>
#include <ranges>  // C++20范围库
#include "ThreadPool.h"

int main()
{
    // 创建线程池，默认使用硬件支持的线程数
    ThreadPool pool;
    // 用于存储任务结果的future集合
    std::vector<std::future<int>> results;

    // 使用C++20的std::views::iota生成0~7的范围
    for (int i : std::views::iota(0, 8)) {
        // 向线程池添加任务
        results.emplace_back(
            pool.enqueue([i] {
                // 任务内容开始
                std::cout << "hello " << i << std::endl;  // 打印开始信息
                std::this_thread::sleep_for(std::chrono::seconds(1));  // 模拟耗时操作
                std::cout << "world " << i << std::endl;  // 打印结束信息
                return i * i;  // 返回计算结果
                })
        );
    }

    // 遍历结果（使用C++20的带初始化语句的范围for循环）
    for (size_t index = 0; auto && result : results) {
        // 获取并打印每个任务的结果
        std::cout << "Task " << index++ << " result: " << result.get() << '\n';
    }

    std::cout << std::endl;
    return 0;
}
