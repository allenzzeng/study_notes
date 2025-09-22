# 02 模块：自定义容器与算法实现（基于 CMake 构建）

## 模块简介
本模块实现了 C++ 标准库中部分容器（`vector`、`list`）和基础算法（`sort`、`find` 等）的简化版本，旨在深入理解 STL 容器的底层实现原理（如内存管理、迭代器设计、动态扩容等）。代码风格参考了标准库的设计思想，同时通过简洁的实现展示核心逻辑，适合作为 C++ 容器与算法的学习案例。


## 核心组件

### 1. 容器实现
- **`vector.h`**：动态数组容器，支持随机访问，底层使用连续内存存储，当空间不足时自动扩容（默认翻倍策略），实现了 `push_back`、`emplace_back`、`pop_back` 等核心操作。
- **`list.h`**：双向链表容器，通过节点指针维护元素顺序，支持在头部/尾部高效插入删除，实现了 `push_back`、`push_front`、`insert`、`erase` 等操作。
- **`allocator.h`**：内存分配器，封装了底层内存的分配（`allocate`）、释放（`deallocate`）、对象构造（`construct`）和析构（`destroy`），为容器提供内存管理支持。

### 2. 算法实现
- **`algorithm.h`**：包含基础算法函数，如 `find`（查找元素）、`sort`（排序容器元素）等，遵循迭代器接口设计，可适配自定义容器。

### 3. 测试程序
- **`main.cpp`**：验证自定义容器和算法的功能，包括 `vector` 和 `list` 的基本操作（初始化、添加元素、遍历等），以及 `sort`、`find` 算法的使用示例。


## 编译与运行（基于 CMake）

### 环境要求
- 支持 C++20 标准的编译器（如 Visual Studio 2022、GCC 10+、Clang 12+）
- CMake 3.15 及以上版本


### 编译步骤（Windows 平台，Visual Studio 2022）
1. 进入模块目录：
   ```bash
   cd path\to\study_notes\02
   ```

2. 创建并进入构建目录：
   ```bash
   mkdir build
   cd build
   ```

3. 生成 Visual Studio 项目文件：
   ```bash
   cmake .. -G "Visual Studio 17 2022" -A x64
   ```
   - `-G "Visual Studio 17 2022"`：指定生成 Visual Studio 2022 解决方案
   - `-A x64`：指定构建 64 位目标程序

4. 编译项目：
   - 打开生成的 `study_notes_02.sln` 解决方案
   - 在 Visual Studio 中选择“生成”->“生成解决方案”

5. 运行测试程序：
   - 编译生成的可执行文件位于 `build\Debug\study02.exe` 或 `build\Release\study02.exe`
   - 直接运行可执行文件，观察输出结果


### 编译说明（CMakeLists.txt）
项目通过 CMake 管理构建流程，核心配置包括：
- 指定 C++ 标准为 C++20，确保支持现代 C++ 特性（如右值引用、范围 for 循环等）
- 定义可执行目标 `study02`，关联源文件 `main.cpp` 及头文件目录
- 自动处理头文件依赖，确保编译器能正确找到 `allocator.h`、`vector.h` 等自定义头文件


## 测试输出示例
运行测试程序后，将输出以下内容，验证容器和算法的功能：
```
=== Testing mystl::vector ===
Vector elements: 1 2 3 4 5 6 7 
Size: 7, Capacity: 8

=== Testing mystl::list ===
List elements: Hello World from MySTL 
Size: 4

=== Testing mystl::algorithm ===
Before sort: 5 3 1 4 2 
After sort: 1 2 3 4 5 
Found 3 at position 2
```


## 学习要点
- **内存管理**：理解 `vector` 的动态扩容机制（拷贝/移动元素、释放旧内存）和 `list` 的节点内存管理差异。
- **迭代器设计**：自定义容器如何通过迭代器适配标准算法，`vector` 随机访问迭代器与 `list` 双向迭代器的区别。
- **模板编程**：通过类模板实现通用容器，支持任意数据类型，掌握模板参数、类型别名的使用。
- **CMake 构建**：学习跨平台构建工具的基本配置，如何指定 C++ 标准、关联源文件和头文件目录。


## 参考资料
本模块实现思路参考了 C++ 标准库（STL）的设计理念，可结合《C++ Primer》《STL 源码剖析》等资料深入学习容器底层原理。同时，CMake 配置部分参考了现代 CMake 最佳实践，确保构建流程的简洁性和可移植性。
