#pragma once
#include "allocator.h"
#include <initializer_list>
#include <algorithm>
#include <stdexcept>


namespace mystl {

    template <typename T, typename Alloc = allocator<T>>
    class vector {
    public:
        // ���Ͷ���
        using value_type = T;
        using allocator_type = Alloc;
        using size_type = typename std::allocator_traits<Alloc>::size_type;
        using difference_type = typename std::allocator_traits<Alloc>::difference_type;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = typename std::allocator_traits<Alloc>::pointer;
        using const_pointer = typename std::allocator_traits<Alloc>::const_pointer;
        using iterator = pointer;  // ��ʵ�֣�ָ����Ϊ������
        using const_iterator = const_pointer;

    private:
        pointer start_ = nullptr;      // ָ���һ��Ԫ��
        pointer finish_ = nullptr;     // ָ�����һ��Ԫ�ص���һ��λ��
        pointer end_of_storage_ = nullptr; // ָ������ڴ��ĩβ
        [[no_unique_address]] Alloc allocator_; // ����������

    public:
        // Ĭ�Ϲ��캯��
        vector() noexcept(noexcept(Alloc())) : allocator_() {}

        // ָ���������Ĺ��캯��
        explicit vector(const Alloc& alloc) noexcept : allocator_(alloc) {}

        // ָ����С�ͳ�ʼֵ�Ĺ��캯��
        explicit vector(size_type n, const T& value = T(), const Alloc& alloc = Alloc())
            : allocator_(alloc) {
            start_ = allocator_.allocate(n);
            finish_ = start_ + n;
            end_of_storage_ = finish_;
            std::uninitialized_fill(start_, finish_, value);
        }

        // ��Χ���캯��
        template <typename InputIt>
        vector(InputIt first, InputIt last, const Alloc& alloc = Alloc())
            : allocator_(alloc) {
            // �������
            size_type n = std::distance(first, last);
            start_ = allocator_.allocate(n);
            finish_ = start_ + n;
            end_of_storage_ = finish_;
            std::uninitialized_copy(first, last, start_);
        }

        // ��ʼ���б��캯��
        vector(std::initializer_list<T> init, const Alloc& alloc = Alloc())
            : vector(init.begin(), init.end(), alloc) {}

        // ��������
        ~vector() {
            clear();
            allocator_.deallocate(start_, capacity());
        }

        // �������캯��
        vector(const vector& other)
            : allocator_(std::allocator_traits<Alloc>::select_on_container_copy_construction(
                other.allocator_)) {
            size_type n = other.size();
            start_ = allocator_.allocate(n);
            finish_ = start_ + n;
            end_of_storage_ = finish_;
            std::uninitialized_copy(other.begin(), other.end(), start_);
        }

        // �ƶ����캯��
        vector(vector&& other) noexcept
            : start_(other.start_),
            finish_(other.finish_),
            end_of_storage_(other.end_of_storage_),
            allocator_(std::move(other.allocator_)) {
            other.start_ = other.finish_ = other.end_of_storage_ = nullptr;
        }

        // ������ֵ�����
        vector& operator=(const vector& other) {
            if (this != &other) {
                // �������ڴ�
                size_type n = other.size();
                pointer new_start = allocator_.allocate(n);
                pointer new_finish = std::uninitialized_copy(other.begin(), other.end(), new_start);

                // �ͷž��ڴ�
                clear();
                allocator_.deallocate(start_, capacity());

                // ����ָ��
                start_ = new_start;
                finish_ = new_finish;
                end_of_storage_ = new_finish;
            }
            return *this;
        }

        // �ƶ���ֵ�����
        vector& operator=(vector&& other) noexcept {
            if (this != &other) {
                // �ͷŵ�ǰ��Դ
                clear();
                allocator_.deallocate(start_, capacity());

                // �ӹ�other����Դ
                start_ = other.start_;
                finish_ = other.finish_;
                end_of_storage_ = other.end_of_storage_;
                allocator_ = std::move(other.allocator_);

                // ��other������Ч��δ�����״̬
                other.start_ = other.finish_ = other.end_of_storage_ = nullptr;
            }
            return *this;
        }

        // Ԫ�ط���
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

        // ������
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

        // ����
        bool empty() const noexcept {
            return start_ == finish_;
        }

        size_type size() const noexcept {
            return finish_ - start_;
        }

        size_type capacity() const noexcept {
            return end_of_storage_ - start_;
        }

        // �޸���
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
        // ���·����ڴ�
        void reallocate(size_type new_capacity) {
            // �������ڴ�
            pointer new_start = allocator_.allocate(new_capacity);
            pointer new_finish = new_start;

            try {
                // �ƶ���������Ԫ��
                new_finish = std::uninitialized_move(start_, finish_, new_start);
            }
            catch (...) {
                // �����쳣ʱ�ͷ��·�����ڴ�
                allocator_.deallocate(new_start, new_capacity);
                throw;
            }

            // ���ٲ��ͷž��ڴ�
            clear();
            allocator_.deallocate(start_, capacity());

            // ����ָ��
            start_ = new_start;
            finish_ = new_finish;
            end_of_storage_ = start_ + new_capacity;
        }
    };

} // namespace mystl