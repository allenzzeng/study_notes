#pragma once
#include "allocator.h"
#include <initializer_list>
#include <utility>



namespace mystl {

    // 链表节点
    template <typename T>
    struct list_node {
        list_node* prev;
        list_node* next;
        T value;

        // 完美转发构造函数
        template <typename... Args>
        list_node(Args&&... args)
            : prev(nullptr), next(nullptr), value(std::forward<Args>(args)...) {}
    };

    // 链表迭代器
    template <typename T>
    class list_iterator {
    public:
        using value_type = T;
        using pointer = T*;
        using reference = T&;
        using difference_type = ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;

        using node_type = list_node<T>;
        using node_pointer = list_node<T>*;

    private:
        node_pointer node_;

    public:
        list_iterator() noexcept : node_(nullptr) {}
        explicit list_iterator(node_pointer node) noexcept : node_(node) {}

        reference operator*() const noexcept {
            return node_->value;
        }

        pointer operator->() const noexcept {
            return &node_->value;
        }

        list_iterator& operator++() noexcept {
            node_ = node_->next;
            return *this;
        }

        list_iterator operator++(int) noexcept {
            list_iterator tmp = *this;
            node_ = node_->next;
            return tmp;
        }

        list_iterator& operator--() noexcept {
            node_ = node_->prev;
            return *this;
        }

        list_iterator operator--(int) noexcept {
            list_iterator tmp = *this;
            node_ = node_->prev;
            return tmp;
        }

        bool operator==(const list_iterator& other) const noexcept {
            return node_ == other.node_;
        }

        bool operator!=(const list_iterator& other) const noexcept {
            return node_ != other.node_;
        }

        node_pointer node() const noexcept {
            return node_;
        }
    };

    // 链表类
    template <typename T, typename Alloc = allocator<list_node<T>>>
    class list {
    private:
        using node_type = list_node<T>;
        using node_pointer = node_type*;
        using allocator_type = Alloc;
        using node_allocator = typename std::allocator_traits<Alloc>::template rebind_alloc<node_type>;

        node_pointer head_;       // 头哨兵节点
        node_pointer tail_;       // 尾哨兵节点
        size_t size_;             // 元素数量
        [[no_unique_address]] node_allocator allocator_;  // 分配器

    public:
        using value_type = T;
        using reference = T&;
        using const_reference = const T&;
        using pointer = T*;
        using const_pointer = const T*;
        using iterator = list_iterator<T>;
        using const_iterator = const list_iterator<T>;
        using size_type = size_t;
        using difference_type = ptrdiff_t;

        // 构造函数
        list() : size_(0) {
            init();
        }

        explicit list(const Alloc& alloc) : allocator_(alloc), size_(0) {
            init();
        }

        list(size_type count, const T& value, const Alloc& alloc = Alloc())
            : allocator_(alloc), size_(0) {
            init();
            try {
                while (count--) {
                    emplace_back(value);
                }
            }
            catch (...) {
                clear();
                throw;
            }
        }

        explicit list(size_type count, const Alloc& alloc = Alloc())
            : allocator_(alloc), size_(0) {
            init();
            try {
                while (count--) {
                    emplace_back();
                }
            }
            catch (...) {
                clear();
                throw;
            }
        }

        template <typename InputIt>
        list(InputIt first, InputIt last, const Alloc& alloc = Alloc())
            : allocator_(alloc), size_(0) {
            init();
            try {
                for (; first != last; ++first) {
                    emplace_back(*first);
                }
            }
            catch (...) {
                clear();
                throw;
            }
        }

        list(const list& other)
            : allocator_(
                std::allocator_traits<node_allocator>::select_on_container_copy_construction(
                    other.allocator_)),
            size_(0) {
            init();
            try {
                for (const auto& item : other) {
                    emplace_back(item);
                }
            }
            catch (...) {
                clear();
                throw;
            }
        }

        list(list&& other) noexcept
            : head_(other.head_),
            tail_(other.tail_),
            size_(other.size_),
            allocator_(std::move(other.allocator_)) {
            other.head_ = other.tail_ = nullptr;
            other.size_ = 0;
        }

        list(std::initializer_list<T> init, const Alloc& alloc = Alloc())
            : list(init.begin(), init.end(), alloc) {}

        // 析构函数
        ~list() {
            clear();
            if (head_) {
                allocator_.deallocate(head_, 1);
            }
            if (tail_) {
                allocator_.deallocate(tail_, 1);
            }
        }

        // 赋值运算符
        list& operator=(const list& other) {
            if (this != &other) {
                list tmp(other);
                swap(tmp);
            }
            return *this;
        }

        list& operator=(list&& other) noexcept {
            if (this != &other) {
                clear();
                swap(other);
            }
            return *this;
        }

        list& operator=(std::initializer_list<T> ilist) {
            list tmp(ilist);
            swap(tmp);
            return *this;
        }

        // 元素访问
        reference front() {
            return head_->next->value;
        }

        const_reference front() const {
            return head_->next->value;
        }

        reference back() {
            return tail_->prev->value;
        }

        const_reference back() const {
            return tail_->prev->value;
        }

        // 迭代器
        iterator begin() noexcept {
            return iterator(head_->next);
        }

        const_iterator begin() const noexcept {
            return const_iterator(head_->next);
        }

        iterator end() noexcept {
            return iterator(tail_);
        }

        const_iterator end() const noexcept {
            return const_iterator(tail_);
        }

        // 容量
        bool empty() const noexcept {
            return size_ == 0;
        }

        size_type size() const noexcept {
            return size_;
        }

        // 修改器
        void clear() noexcept {
            while (!empty()) {
                pop_back();
            }
        }

        iterator insert(const_iterator pos, const T& value) {
            return emplace(pos, value);
        }

        iterator insert(const_iterator pos, T&& value) {
            return emplace(pos, std::move(value));
        }

        template <typename... Args>
        iterator emplace(const_iterator pos, Args&&... args) {
            node_pointer new_node = create_node(std::forward<Args>(args)...);
            node_pointer pos_node = pos.node();

            // 链接新节点
            new_node->prev = pos_node->prev;
            new_node->next = pos_node;
            pos_node->prev->next = new_node;
            pos_node->prev = new_node;

            ++size_;
            return iterator(new_node);
        }

        iterator erase(const_iterator pos) {
            node_pointer pos_node = pos.node();
            node_pointer next_node = pos_node->next;

            // 从链表中移除节点
            pos_node->prev->next = pos_node->next;
            pos_node->next->prev = pos_node->prev;

            // 销毁节点
            allocator_.destroy(pos_node);
            allocator_.deallocate(pos_node, 1);

            --size_;
            return iterator(next_node);
        }

        void push_back(const T& value) {
            emplace_back(value);
        }

        void push_back(T&& value) {
            emplace_back(std::move(value));
        }

        template <typename... Args>
        reference emplace_back(Args&&... args) {
            return *emplace(end(), std::forward<Args>(args)...);
        }

        void pop_back() {
            erase(--end());
        }

        void push_front(const T& value) {
            emplace_front(value);
        }

        void push_front(T&& value) {
            emplace_front(std::move(value));
        }

        template <typename... Args>
        reference emplace_front(Args&&... args) {
            return *emplace(begin(), std::forward<Args>(args)...);
        }

        void pop_front() {
            erase(begin());
        }

        void swap(list& other) noexcept {
            std::swap(head_, other.head_);
            std::swap(tail_, other.tail_);
            std::swap(size_, other.size_);
            std::swap(allocator_, other.allocator_);
        }

    private:
        // 初始化哨兵节点
        void init() {
            head_ = allocator_.allocate(1);
            tail_ = allocator_.allocate(1);
            head_->next = tail_;
            head_->prev = nullptr;
            tail_->prev = head_;
            tail_->next = nullptr;
        }

        // 创建新节点
        template <typename... Args>
        node_pointer create_node(Args&&... args) {
            node_pointer new_node = allocator_.allocate(1);
            try {
                allocator_.construct(new_node, std::forward<Args>(args)...);
            }
            catch (...) {
                allocator_.deallocate(new_node, 1);
                throw;
            }
            return new_node;
        }
    };

} // namespace mystl
