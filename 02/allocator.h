#pragma once
#include <memory>
#include <cstdlib>
#include <new>



namespace mystl {

    // �򵥵��ڴ������ʵ��
    template <typename T>
    class allocator {
    public:
        // ���Ͷ���
        using value_type = T;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;
        using size_type = size_t;
        using difference_type = ptrdiff_t;

        // C++20�����ķ���������
        using is_always_equal = std::true_type;  // ��ʾ���и����͵ķ��������ǵȼ۵�

        // Ĭ�Ϲ��캯��
        allocator() noexcept = default;

        // �������캯��
        template <typename U>
        allocator(const allocator<U>&) noexcept {}

        // �����ڴ�
        [[nodiscard]] pointer allocate(size_type n) {
            if (n > max_size()) {
                throw std::bad_alloc();
            }

            // ʹ��malloc�����ڴ�
            if (auto p = static_cast<pointer>(std::malloc(n * sizeof(T)))) {
                return p;
            }
            throw std::bad_alloc();
        }

        // �ͷ��ڴ�
        void deallocate(pointer p, size_type) noexcept {
            std::free(p);
        }

        // �������
        template <typename U, typename... Args>
        void construct(U* p, Args&&... args) {
            ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
        }

        // ���ٶ���
        template <typename U>
        void destroy(U* p) {
            p->~U();
        }

        // ����ܷ���Ĵ�С
        [[nodiscard]] size_type max_size() const noexcept {
            return size_type(-1) / sizeof(T);
        }
    };

    // �Ƚ������������Ƿ����
    template <typename T1, typename T2>
    bool operator==(const allocator<T1>&, const allocator<T2>&) noexcept {
        return true;
    }

} // namespace mystl