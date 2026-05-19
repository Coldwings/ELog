#pragma once

#include "iov.hpp"
#include "render.hpp"
#include "string_ref.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace elog {

template <class T> struct pad_left_t;
template <class T> struct pad_right_t;
template <class T> struct nullable;
template <class It> struct join_t;

template <class T>
Iov elog_render(char* scratch, std::size_t& pos, const pad_left_t<T>& w) noexcept;
template <class T>
Iov elog_render(char* scratch, std::size_t& pos, const pad_right_t<T>& w) noexcept;
template <class T>
Iov elog_render(char* scratch, std::size_t& pos, const nullable<T>& n) noexcept;
template <class It>
Iov elog_render(char* scratch, std::size_t& pos, const join_t<It>& j) noexcept;

template <class T, class Alloc>
Iov elog_render(char* scratch, std::size_t& pos, const std::vector<T, Alloc>& v) noexcept;
template <class T, std::size_t N>
Iov elog_render(char* scratch, std::size_t& pos, const std::array<T, N>& v) noexcept;
template <class A, class B>
Iov elog_render(char* scratch, std::size_t& pos, const std::pair<A, B>& p) noexcept;
template <class... Ts>
Iov elog_render(char* scratch, std::size_t& pos, const std::tuple<Ts...>& t) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, const std::tuple<>&) noexcept;
template <class K, class V, class C, class A>
Iov elog_render(char* scratch, std::size_t& pos, const std::map<K, V, C, A>& m) noexcept;
template <class K, class V, class H, class E, class A>
Iov elog_render(char* scratch, std::size_t& pos, const std::unordered_map<K, V, H, E, A>& m) noexcept;
template <class T, class C, class A>
Iov elog_render(char* scratch, std::size_t& pos, const std::set<T, C, A>& s) noexcept;
template <class T, class H, class E, class A>
Iov elog_render(char* scratch, std::size_t& pos, const std::unordered_set<T, H, E, A>& s) noexcept;

struct oct {
    std::uint64_t v;
    explicit oct(std::uint64_t x) noexcept : v(x) {}
};

struct hexdump {
    const void* p;
    std::size_t n;
    hexdump(const void* ptr, std::size_t len) noexcept : p(ptr), n(len) {}
};

struct escaped {
    string_ref s;
    explicit escaped(string_ref r) noexcept : s(r) {}
    explicit escaped(const char* p) noexcept : s(p) {}
    explicit escaped(const std::string& str) noexcept : s(str.data(), str.size()) {}
};

template <class T>
struct pad_left_t {
    const T& v;
    int width;
    char fill;
};

template <class T>
struct pad_right_t {
    const T& v;
    int width;
    char fill;
};

template <class T>
inline pad_left_t<T> pad_left(const T& v, int width, char fill = ' ') noexcept {
    return pad_left_t<T>{v, width, fill};
}

template <class T>
inline pad_right_t<T> pad_right(const T& v, int width, char fill = ' ') noexcept {
    return pad_right_t<T>{v, width, fill};
}

inline fixed precision(double v, int prec) noexcept { return fixed(v, prec); }

template <class T>
struct nullable {
    const T* p;
    explicit nullable(const T* ptr) noexcept : p(ptr) {}
};

template <class T>
inline nullable<T> nullable_of(const T* p) noexcept {
    return nullable<T>{p};
}

template <class It>
struct join_t {
    It first;
    It last;
    const char* sep;
    std::size_t sep_len;
};

template <class Range>
inline auto join(const Range& r, const char* sep) noexcept
    -> join_t<decltype(std::begin(r))> {
    using std::begin;
    using std::end;
    const char* s = sep ? sep : "";
    std::size_t n = 0;
    while (s[n]) ++n;
    return join_t<decltype(begin(r))>{begin(r), end(r), s, n};
}

Iov elog_render(char* scratch, std::size_t& pos, oct v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, hexdump v) noexcept;
Iov elog_render(char* scratch, std::size_t& pos, escaped v) noexcept;

namespace detail {

inline std::size_t copy_bytes(char* dst, const void* src, std::size_t n) noexcept {
    auto* s = static_cast<const unsigned char*>(src);
    for (std::size_t i = 0; i < n; ++i) dst[i] = static_cast<char>(s[i]);
    return n;
}

inline std::size_t append_str(char* scratch, std::size_t& pos, const char* s, std::size_t n) noexcept {
    std::size_t start = pos;
    constexpr std::size_t kCap = 4096;
    if (pos > kCap) return start;
    std::size_t avail = kCap - pos;
    if (n > avail) n = avail;
    for (std::size_t i = 0; i < n; ++i) scratch[pos + i] = s[i];
    pos += n;
    return start;
}

inline void append_iov(char* scratch, std::size_t& pos, Iov v) noexcept {
    append_str(scratch, pos, static_cast<const char*>(v.base), v.len);
}

template <class T>
Iov render_padded(char* scratch, std::size_t& pos, const T& v, int width, char fill, bool left_align) noexcept;

}  // namespace detail

template <class T>
Iov elog_render(char* scratch, std::size_t& pos, const pad_left_t<T>& w) noexcept {
    return detail::render_padded(scratch, pos, w.v, w.width, w.fill, /*left_align=*/false);
}

template <class T>
Iov elog_render(char* scratch, std::size_t& pos, const pad_right_t<T>& w) noexcept {
    return detail::render_padded(scratch, pos, w.v, w.width, w.fill, /*left_align=*/true);
}

template <class T>
Iov elog_render(char* scratch, std::size_t& pos, const nullable<T>& n) noexcept {
    using elog::elog_render;
    if (n.p == nullptr) {
        std::size_t start = detail::append_str(scratch, pos, "<null>", 6);
        return Iov{scratch + start, pos - start};
    }
    return elog_render(scratch, pos, *n.p);
}

namespace detail {

template <class T>
inline Iov render_padded(char* scratch, std::size_t& pos, const T& v, int width, char fill, bool left_align) noexcept {
    using elog::elog_render;
    std::size_t start = pos;
    Iov inner = elog_render(scratch, pos, v);
    std::size_t inner_len = inner.len;
    if (width <= 0 || static_cast<std::size_t>(width) <= inner_len) {
        if (inner.base != static_cast<const void*>(scratch + start)) {
            std::size_t cs = pos;
            append_str(scratch, pos, static_cast<const char*>(inner.base), inner.len);
            return Iov{scratch + cs, pos - cs};
        }
        return inner;
    }
    std::size_t pad_count = static_cast<std::size_t>(width) - inner_len;

    if (inner.base == static_cast<const void*>(scratch + start)) {
        if (left_align) {
            for (std::size_t i = 0; i < pad_count; ++i) {
                if (pos >= 4096) break;
                scratch[pos++] = fill;
            }
            return Iov{scratch + start, pos - start};
        } else {
            std::size_t end = pos;
            std::size_t shift = pad_count;
            std::size_t bytes_to_move = end - start;
            if (end + shift > 4096) {
                std::size_t avail = (end + shift > 4096) ? (4096 - end) : shift;
                shift = avail;
            }
            for (std::size_t i = bytes_to_move; i > 0; --i) {
                scratch[start + shift + (i - 1)] = scratch[start + (i - 1)];
            }
            for (std::size_t i = 0; i < shift; ++i) scratch[start + i] = fill;
            pos = end + shift;
            return Iov{scratch + start, pos - start};
        }
    } else {
        const char* src = static_cast<const char*>(inner.base);
        std::size_t out_start = pos;
        if (left_align) {
            append_str(scratch, pos, src, inner_len);
            for (std::size_t i = 0; i < pad_count; ++i) {
                if (pos >= 4096) break;
                scratch[pos++] = fill;
            }
        } else {
            for (std::size_t i = 0; i < pad_count; ++i) {
                if (pos >= 4096) break;
                scratch[pos++] = fill;
            }
            append_str(scratch, pos, src, inner_len);
        }
        return Iov{scratch + out_start, pos - out_start};
    }
}

}  // namespace detail

template <class It>
Iov elog_render(char* scratch, std::size_t& pos, const join_t<It>& j) noexcept {
    using elog::elog_render;
    std::size_t start = pos;
    bool first = true;
    for (auto it = j.first; it != j.last; ++it) {
        if (!first) {
            detail::append_str(scratch, pos, j.sep, j.sep_len);
        }
        first = false;
        Iov e = elog_render(scratch, pos, *it);
        if (e.base != static_cast<const void*>(scratch + (pos - e.len))) {
            detail::append_iov(scratch, pos, e);
        }
    }
    return Iov{scratch + start, pos - start};
}

template <class T, class Alloc>
Iov elog_render(char* scratch, std::size_t& pos, const std::vector<T, Alloc>& v) noexcept {
    using elog::elog_render;
    std::size_t start = pos;
    detail::append_str(scratch, pos, "[", 1);
    bool first = true;
    for (const auto& e : v) {
        if (!first) detail::append_str(scratch, pos, ", ", 2);
        first = false;
        Iov ie = elog_render(scratch, pos, e);
        if (ie.base != static_cast<const void*>(scratch + (pos - ie.len))) {
            detail::append_iov(scratch, pos, ie);
        }
    }
    detail::append_str(scratch, pos, "]", 1);
    return Iov{scratch + start, pos - start};
}

template <class T, std::size_t N>
Iov elog_render(char* scratch, std::size_t& pos, const std::array<T, N>& v) noexcept {
    using elog::elog_render;
    std::size_t start = pos;
    detail::append_str(scratch, pos, "[", 1);
    bool first = true;
    for (const auto& e : v) {
        if (!first) detail::append_str(scratch, pos, ", ", 2);
        first = false;
        Iov ie = elog_render(scratch, pos, e);
        if (ie.base != static_cast<const void*>(scratch + (pos - ie.len))) {
            detail::append_iov(scratch, pos, ie);
        }
    }
    detail::append_str(scratch, pos, "]", 1);
    return Iov{scratch + start, pos - start};
}

template <class A, class B>
Iov elog_render(char* scratch, std::size_t& pos, const std::pair<A, B>& p) noexcept {
    using elog::elog_render;
    std::size_t start = pos;
    detail::append_str(scratch, pos, "(", 1);
    Iov a = elog_render(scratch, pos, p.first);
    if (a.base != static_cast<const void*>(scratch + (pos - a.len))) {
        detail::append_iov(scratch, pos, a);
    }
    detail::append_str(scratch, pos, ", ", 2);
    Iov b = elog_render(scratch, pos, p.second);
    if (b.base != static_cast<const void*>(scratch + (pos - b.len))) {
        detail::append_iov(scratch, pos, b);
    }
    detail::append_str(scratch, pos, ")", 1);
    return Iov{scratch + start, pos - start};
}

namespace detail {

template <std::size_t I, class Tuple>
struct tuple_render_helper {
    static void apply(char* scratch, std::size_t& pos, const Tuple& t) noexcept {
        using elog::elog_render;
        tuple_render_helper<I - 1, Tuple>::apply(scratch, pos, t);
        append_str(scratch, pos, ", ", 2);
        Iov e = elog_render(scratch, pos, std::get<I>(t));
        if (e.base != static_cast<const void*>(scratch + (pos - e.len))) {
            append_iov(scratch, pos, e);
        }
    }
};

template <class Tuple>
struct tuple_render_helper<0, Tuple> {
    static void apply(char* scratch, std::size_t& pos, const Tuple& t) noexcept {
        using elog::elog_render;
        Iov e = elog_render(scratch, pos, std::get<0>(t));
        if (e.base != static_cast<const void*>(scratch + (pos - e.len))) {
            append_iov(scratch, pos, e);
        }
    }
};

}  // namespace detail

template <class... Ts>
Iov elog_render(char* scratch, std::size_t& pos, const std::tuple<Ts...>& t) noexcept {
    std::size_t start = pos;
    detail::append_str(scratch, pos, "(", 1);
    constexpr std::size_t N = sizeof...(Ts);
    if (N > 0) {
        detail::tuple_render_helper<(N == 0 ? 0 : N - 1), std::tuple<Ts...>>::apply(scratch, pos, t);
    }
    detail::append_str(scratch, pos, ")", 1);
    return Iov{scratch + start, pos - start};
}

inline Iov elog_render(char* scratch, std::size_t& pos, const std::tuple<>&) noexcept {
    std::size_t start = pos;
    detail::append_str(scratch, pos, "()", 2);
    return Iov{scratch + start, pos - start};
}

namespace detail {

template <class Map>
inline Iov render_map(char* scratch, std::size_t& pos, const Map& m) noexcept {
    using elog::elog_render;
    std::size_t start = pos;
    append_str(scratch, pos, "{", 1);
    bool first = true;
    for (const auto& kv : m) {
        if (!first) append_str(scratch, pos, ", ", 2);
        first = false;
        Iov k = elog_render(scratch, pos, kv.first);
        if (k.base != static_cast<const void*>(scratch + (pos - k.len))) append_iov(scratch, pos, k);
        append_str(scratch, pos, ": ", 2);
        Iov v = elog_render(scratch, pos, kv.second);
        if (v.base != static_cast<const void*>(scratch + (pos - v.len))) append_iov(scratch, pos, v);
    }
    append_str(scratch, pos, "}", 1);
    return Iov{scratch + start, pos - start};
}

template <class Set>
inline Iov render_set(char* scratch, std::size_t& pos, const Set& s) noexcept {
    using elog::elog_render;
    std::size_t start = pos;
    append_str(scratch, pos, "{", 1);
    bool first = true;
    for (const auto& e : s) {
        if (!first) append_str(scratch, pos, ", ", 2);
        first = false;
        Iov ie = elog_render(scratch, pos, e);
        if (ie.base != static_cast<const void*>(scratch + (pos - ie.len))) append_iov(scratch, pos, ie);
    }
    append_str(scratch, pos, "}", 1);
    return Iov{scratch + start, pos - start};
}

}  // namespace detail

template <class K, class V, class C, class A>
Iov elog_render(char* scratch, std::size_t& pos, const std::map<K, V, C, A>& m) noexcept {
    return detail::render_map(scratch, pos, m);
}

template <class K, class V, class H, class E, class A>
Iov elog_render(char* scratch, std::size_t& pos, const std::unordered_map<K, V, H, E, A>& m) noexcept {
    return detail::render_map(scratch, pos, m);
}

template <class T, class C, class A>
Iov elog_render(char* scratch, std::size_t& pos, const std::set<T, C, A>& s) noexcept {
    return detail::render_set(scratch, pos, s);
}

template <class T, class H, class E, class A>
Iov elog_render(char* scratch, std::size_t& pos, const std::unordered_set<T, H, E, A>& s) noexcept {
    return detail::render_set(scratch, pos, s);
}

}  // namespace elog
