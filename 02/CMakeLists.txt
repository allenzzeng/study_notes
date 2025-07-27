cmake_minimum_required(VERSION 3.15)
project(study_notes_02)

# 指定 C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行文件
add_executable(study02 main.cpp)

# 头文件包含路径
target_include_directories(study02 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
