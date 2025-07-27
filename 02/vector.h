#pragma once
#include "allocator.h"
#include <initializer_list>
#include <algorithm>
#include <stdexcept>


namespace mystl {

    template <typename T, typename Alloc = allocator<T>>
    class vector {
    public:
        // 类型定义
        using value_type = T;
        using allocator_type = Alloc;
        using size_type = typename std::allocator_traits<Alloc>::size_type;
        using difference_type = typename std::allocator_traits<Alloc>::difference_type;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = typename std::allocator_traits<Alloc>::pointer;
        using const_pointer = typename std::allocator_traits<Alloc>::const_pointer;
        using iterator = pointer;  // 简化实现，指针作为迭代器
        using const_iterator = const_pointer;

    private:
        pointer start_ = nullptr;      // 指向第一个元素
        pointer finish_ = nullptr;     // 指向最后一个元素的下一个位置
        pointer end_of_storage_ = nullptr; // 指向分配内存的末尾
        [[no_unique_address]] Alloc allocator_; // 分配器对象

    public:
        // 默认构造函数
        vector() noexcept(noexcept(Alloc())) : allocator_() {}

        // 指定分配器的构造函数
        explicit vector(const Alloc& alloc) noexcept : allocator_(alloc) {}

        // 指定大小和初始值的构造函数
        explicit vector(size_type n, const T& value = T(), const Alloc& alloc = Alloc())
            : allocator_(alloc) {
            start_ = allocator_.allocate(n);
            finish_ = start_ + n;
            end_of_storage_ = finish_;
            std::uninitialized_fill(start_, finish_, value);
        }

        // 范围构造函数
        template <typename InputIt>
        vector(InputIt first, InputIt last, const Alloc& alloc = Alloc())
            : allocator_(alloc) {
            // 计算距离
            size_type n = std::distance(first, last);
            start_ = allocator_.allocate(n);
            finish_ = start_ + n;
            end_of_storage_ = finish_;
            std::uninitialized_copy(first, last, start_);
        }

        // 初始化列表构造函数
        vector(std::initializer_list<T> init, const Alloc& alloc = Alloc())
            : vector(init.begin(), init.end(), alloc) {}

        // 析构函数
        ~vector() {
            clear();
            allocator_.deallocate(start_, capacity());
        }

        // 拷贝构造函数
        vector(const vector& other)
            : allocator_(std::allocator_traits<Alloc>::select_on_container_copy_construction(
                other.allocator_)) {
            size_type n = other.size();
            start_ = allocator_.allocate(n);
            finish_ = start_ + n;
            end_of_storage_ = finish_;
            std::uninitialized_copy(other.begin(), other.end(), start_);
        }

        // 移动构造函数
        vector(vector&& other) noexcept
            : start_(other.start_),
            finish_(other.finish_),
            end_of_storage_(other.end_of_storage_),
            allocator_(std::move(other.allocator_)) {
            other.start_ = other.finish_ = other.end_of_storage_ = nullptr;
        }

        // 拷贝赋值运算符
        vector& operator=(const vector& other) {
            if (this != &other) {
                // 分配新内存
                size_type n = other.size();
                pointer new_start = allocator_.allocate(n);
                pointer new_finish = std::uninitialized_copy(other.begin(), other.end(), new_start);

                // 释放旧内存
                clear();
                allocator_.deallocate(start_, capacity());

                // 更新指针
                start_ = new_start;
                finish_ = new_finish;
                end_of_storage_ = new_finish;
            }
            return *this;
        }

        // 移动赋值运算符
        vector& operator=(vector&& other) noexcept {
            if (this != &other) {
                // 释放当前资源
                clear();
                allocator_.deallocate(start_, capacity());

                // 接管other的资源
                start_ = other.start_;
                finish_ = other.finish_;
                end_of_storage_ = other.end_of_storage_;
                allocator_ = std::move(other.allocator_);

                // 将other置于有效但未定义的状态
                other.start_ = other.finish_ = other.end_of_storage_ = nullptr;
            }
            return *this;
        }

        // 元素访问
        reference operator[](size_type pos) {
            return start_[pos];
        }

        const_reference operator[](size_type pos) const {
            return start_[pos];
        }

        reference at(size_type pos) {
            if (pos >= size()) {
                throw std::out_of_range("vector::at");
            }
            return start_[pos];
        }

        const_reference at(size_type pos) const {
            if (pos >= size()) {
                throw std::out_of_range("vector::at");
            }
            return start_[pos];
        }

        reference front() {
            return *start_;
        }

        const_reference front() const {
            return *start_;
        }

        reference back() {
            return *(finish_ - 1);
        }

        const_reference back() const {
            return *(finish_ - 1);
        }

        pointer data() noexcept {
            return start_;
        }

        const_pointer data() const noexcept {
            return start_;
        }

        // 迭代器
        iterator begin() noexcept {
            return start_;
        }

        const_iterator begin() const noexcept {
            return start_;
        }

        iterator end() noexcept {
            return finish_;
        }

        const_iterator end() const noexcept {
            return finish_;
        }

        // 容量
        bool empty() const noexcept {
            return start_ == finish_;
        }

        size_type size() const noexcept {
            return finish_ - start_;
        }

        size_type capacity() const noexcept {
            return end_of_storage_ - start_;
        }

        // 修改器
        void clear() {
            if (start_) {
                for (pointer p = start_; p != finish_; ++p) {
                    allocator_.destroy(p);
                }
                finish_ = start_;
            }
        }

        void push_back(const T& value) {
            if (finish_ == end_of_storage_) {
                reallocate(size() ? size() * 2 : 1);
            }
            allocator_.construct(finish_, value);
            ++finish_;
        }

        void push_back(T&& value) {
            if (finish_ == end_of_storage_) {
                reallocate(size() ? size() * 2 : 1);
            }
            allocator_.construct(finish_, std::move(value));
            ++finish_;
        }

        template <typename... Args>
        reference emplace_back(Args&&... args) {
            if (finish_ == end_of_storage_) {
                reallocate(size() ? size() * 2 : 1);
            }
            allocator_.construct(finish_, std::forward<Args>(args)...);
            ++finish_;
            return back();
        }

        void pop_back() {
            --finish_;
            allocator_.destroy(finish_);
        }

    private:
        // 重新分配内存
        void reallocate(size_type new_capacity) {
            // 分配新内存
            pointer new_start = allocator_.allocate(new_capacity);
            pointer new_finish = new_start;

            try {
                // 移动或复制现有元素
                new_finish = std::uninitialized_move(start_, finish_, new_start);
            }
            catch (...) {
                // 发生异常时释放新分配的内存
                allocator_.deallocate(new_start, new_capacity);
                throw;
            }

            // 销毁并释放旧内存
            clear();
            allocator_.deallocate(start_, capacity());

            // 更新指针
            start_ = new_start;
            finish_ = new_finish;
            end_of_storage_ = start_ + new_capacity;
        }
    };

} // namespace mystl