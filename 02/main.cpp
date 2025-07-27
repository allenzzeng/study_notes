#pragma once

#include "allocator.h"
#include "vector.h"
#include "list.h"
#include "algorithm.h"
#include <iostream>


int main() {
    // ≤‚ ‘vector
    std::cout << "=== Testing mystl::vector ===\n";
    mystl::vector<int> vec = { 1, 2, 3, 4, 5 };
    vec.push_back(6);
    vec.emplace_back(7);

    std::cout << "Vector elements: ";
    for (const auto& num : vec) {
        std::cout << num << " ";
    }
    std::cout << "\nSize: " << vec.size() << ", Capacity: " << vec.capacity() << "\n\n";

    // ≤‚ ‘list
    std::cout << "=== Testing mystl::list ===\n";
    mystl::list<std::string> lst = { "Hello", "World" };
    lst.emplace_back("from");
    lst.emplace_back("MySTL");

    std::cout << "List elements: ";
    for (const auto& str : lst) {
        std::cout << str << " ";
    }
    std::cout << "\nSize: " << lst.size() << "\n\n";

    // ≤‚ ‘À„∑®
    std::cout << "=== Testing mystl::algorithm ===\n";
    mystl::vector<int> numbers = { 5, 3, 1, 4, 2 };

    std::cout << "Before sort: ";
    for (const auto& num : numbers) {
        std::cout << num << " ";
    }
    std::cout << "\n";

    mystl::sort(numbers.begin(), numbers.end());

    std::cout << "After sort: ";
    for (const auto& num : numbers) {
        std::cout << num << " ";
    }
    std::cout << "\n";

    // ≤‚ ‘find
    auto it = mystl::find(numbers.begin(), numbers.end(), 3);
    if (it != numbers.end()) {
        std::cout << "Found 3 at position " << (it - numbers.begin()) << "\n";
    }
    else {
        std::cout << "3 not found\n";
    }

    return 0;
}