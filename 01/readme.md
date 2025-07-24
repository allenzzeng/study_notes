# ThreadPool 示例项目

这是一个简单的 C++ 线程池实现示例，展示了如何使用现代 C++ 特性创建和管理线程池。

## 项目结构

```
01/
├── ThreadPool.h    # 线程池实现头文件
└── main.cpp       # 示例主程序
```

## 快速开始

### 1. 下载项目

使用 [DownGit](https://downgit.github.io/) 下载项目：
1. 访问 https://downgit.github.io/
2. 粘贴项目URL（如：` https://github.com/allenzzeng/study-notes/edit/main/01 `）
3. 点击下载按钮获取项目文件

### 2. 编译程序

在项目目录下打开命令行，执行以下命令：

```bash
g++ -std=c++20 -pthread main.cpp -o threadpool_demo.exe
```

### 3. 运行程序

```bash
.\threadpool_demo.exe
```

## 功能说明

- 线程池自动管理一组工作线程
- 支持提交任意可调用对象及其参数
- 使用 `std::future` 获取异步任务结果
- 支持 C++20 特性（如 `std::views::iota`）

## 预期输出

程序会并行执行8个任务，输出类似：

```
hello 0
hello 1
hello 2
world 0
world 1
world 2
Task 0 result: 0
Task 1 result: 1
Task 2 result: 4
...
```

## 系统要求

- 支持 C++20 的编译器（GCC 11+ 或 Clang 12+）
- Windows/Linux/macOS 系统
