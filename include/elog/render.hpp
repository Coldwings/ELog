#pragma once

#include "iov.hpp"
#include "string_ref.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

namespace elog {

// elog_render is the customization point. It MUST be a free function
// found by ADL (or via using-declaration in emit_f). For string-like
// arguments, the returned Iov MUST point at the source bytes (zero
// copy, scratch untouched). For numeric/custom arguments, render into
// scratch[pos..], advance pos, and return {scratch + old_pos, written}.

Iov elog_render(char* scratch, std::size_t& pos, bool v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, char v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, signed char v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, unsigned char v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, short v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, unsigned short v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, int v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, unsigned int v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, long v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, unsigned long v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, long long v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, unsigned long long v) noexcept;

Iov elog_render(char* scratch, std::size_t& pos, float v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, double v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, long double v) noexcept;

Iov elog_render(char* scratch, std::size_t& pos, const char* v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, string_ref v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, const std::string& v) noexcept;

Iov elog_render(char* scratch, std::size_t& pos, const void* v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, std::nullptr_t) noexcept;

Iov elog_render(char* scratch, std::size_t& pos, std::thread::id v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos,
                std::chrono::system_clock::time_point v) noexcept;

template <std::size_t N>
inline Iov elog_render(char* /*scratch*/, std::size_t& /*pos*/, const char (&v)[N]) noexcept {
    std::size_t n = N;
    if (n > 0 && v[n - 1] == '\0') --n;
    return Iov{static_cast<const void*>(&v[0]), n};
}

struct hex {
    std::uint64_t v;
    explicit hex(std::uint64_t x) noexcept : v(x) {}
};

struct bin {
    std::uint64_t v;
    explicit bin(std::uint64_t x) noexcept : v(x) {}
};

struct fixed {
    double v;
    int prec;
    fixed(double x, int p) noexcept : v(x), prec(p) {}
};

struct quoted {
    string_ref s;
    explicit quoted(string_ref r) noexcept : s(r) {}
    explicit quoted(const char* p) noexcept : s(p) {}
    explicit quoted(const std::string& str) noexcept : s(str.data(), str.size()) {}
};

Iov elog_render(char* scratch, std::size_t& pos, hex v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, bin v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, fixed v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, quoted v) noexcept;

}  // namespace elog
