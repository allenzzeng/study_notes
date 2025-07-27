# 设置构建本项目所需的最低 CMake 版本为 3.15
# 低于此版本的 CMake 会报错，确保使用现代 CMake 特性
cmake_minimum_required(VERSION 3.15)

# 定义项目名称为 study_notes_02
# 这会初始化项目变量并默认启用 C 和 C++ 语言支持
project(study_notes_02)

# 设置 C++ 语言标准为 C++20
# 这会向编译器添加对应的标准标志（如 -std=c++20）
set(CMAKE_CXX_STANDARD 20)

# 要求必须支持指定的 C++ 标准（C++20）
# 如果编译器不支持 C++20 将报错而不是降级
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 定义一个名为 study02 的可执行文件目标
# 该目标由 main.cpp 源文件构建生成
# 在 Windows 下会生成 study02.exe，Linux/macOS 生成 study02
add_executable(study02 main.cpp)

# 为 study02 目标添加头文件包含路径
# PRIVATE 表示该路径仅用于构建 study02 目标本身
# ${CMAKE_CURRENT_SOURCE_DIR} 是当前 CMakeLists.txt 所在目录
# 这样编译器会在该目录下查找 #include 的头文件
target_include_directories(study02 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
