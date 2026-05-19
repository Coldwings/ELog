#pragma once

#include <cstddef>
#include <cstring>

namespace elog {

class string_ref {
    const char* data_;
    std::size_t size_;

public:
    constexpr string_ref() noexcept : data_(nullptr), size_(0) {}
    constexpr string_ref(const char* d, std::size_t n) noexcept : data_(d), size_(n) {}
    string_ref(const char* s) noexcept : data_(s), size_(s ? std::strlen(s) : 0) {}

    constexpr const char* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }
};

}  // namespace elog
