#pragma once
#include <memory>
#include <cstdlib>
#include <new>



namespace mystl {

    // 简单的内存分配器实现
    template <typename T>
    class allocator {
    public:
        // 类型定义
        using value_type = T;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;
        using size_type = size_t;
        using difference_type = ptrdiff_t;

        // C++20新增的分配器特性
        using is_always_equal = std::true_type;  // 表示所有该类型的分配器都是等价的

        // 默认构造函数
        allocator() noexcept = default;

        // 拷贝构造函数
        template <typename U>
        allocator(const allocator<U>&) noexcept {}

        // 分配内存
        [[nodiscard]] pointer allocate(size_type n) {
            if (n > max_size()) {
                throw std::bad_alloc();
            }

            // 使用malloc分配内存
            if (auto p = static_cast<pointer>(std::malloc(n * sizeof(T)))) {
                return p;
            }
            throw std::bad_alloc();
        }

        // 释放内存
        void deallocate(pointer p, size_type) noexcept {
            std::free(p);
        }

        // 构造对象
        template <typename U, typename... Args>
        void construct(U* p, Args&&... args) {
            ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
        }

        // 销毁对象
        template <typename U>
        void destroy(U* p) {
            p->~U();
        }

        // 最大能分配的大小
        [[nodiscard]] size_type max_size() const noexcept {
            return size_type(-1) / sizeof(T);
        }
    };

    // 比较两个分配器是否相等
    template <typename T1, typename T2>
    bool operator==(const allocator<T1>&, const allocator<T2>&) noexcept {
        return true;
    }

} // namespace mystl
