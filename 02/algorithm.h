#pragma once

namespace mystl {

    // 比较两个范围是否相等
    template <typename InputIt1, typename InputIt2>
    bool equal(InputIt1 first1, InputIt1 last1, InputIt2 first2) {
        for (; first1 != last1; ++first1, ++first2) {
            if (!(*first1 == *first2)) {
                return false;
            }
        }
        return true;
    }

    // 填充范围
    template <typename ForwardIt, typename T>
    void fill(ForwardIt first, ForwardIt last, const T& value) {
        for (; first != last; ++first) {
            *first = value;
        }
    }

    // 交换两个元素
    template <typename T>
    void swap(T& a, T& b) noexcept {
        T temp = std::move(a);
        a = std::move(b);
        b = std::move(temp);
    }

    // 查找元素
    template <typename InputIt, typename T>
    InputIt find(InputIt first, InputIt last, const T& value) {
        for (; first != last; ++first) {
            if (*first == value) {
                return first;
            }
        }
        return last;
    }

    // 反转范围
    template <typename BidirIt>
    void reverse(BidirIt first, BidirIt last) {
        while ((first != last) && (first != --last)) {
            mystl::swap(*first++, *last);
        }
    }

    // 排序 (简化版快速排序)
    template <typename RandomIt>
    void sort(RandomIt first, RandomIt last) {
        if (first == last) return;

        auto pivot = *first;
        RandomIt middle1 = std::partition(
            first, last, [pivot](const auto& em) { return em < pivot; });
        RandomIt middle2 = std::partition(
            middle1, last, [pivot](const auto& em) { return !(pivot < em); });

        sort(first, middle1);
        sort(middle2, last);
    }

} // namespace mystl