#include "elog/render.hpp"
#include "elog/grisu2.hpp"
#include "elog/scratch.hpp"

#include <cstdio>
#include <ctime>
#include <functional>

#include <sys/syscall.h>
#include <unistd.h>

namespace elog {

namespace {

constexpr const char* kTwoDigits =
    "00010203040506070809"
    "10111213141516171819"
    "20212223242526272829"
    "30313233343536373839"
    "40414243444546474849"
    "50515253545556575859"
    "60616263646566676869"
    "70717273747576777879"
    "80818283848586878889"
    "90919293949596979899";

inline std::size_t u64_to_chars(std::uint64_t v, char* out) noexcept {
    char buf[24];
    char* end = buf + sizeof(buf);
    char* p = end;
    while (v >= 100) {
        std::uint64_t r = v % 100;
        v /= 100;
        p -= 2;
        std::memcpy(p, kTwoDigits + r * 2, 2);
    }
    if (v >= 10) {
        p -= 2;
        std::memcpy(p, kTwoDigits + v * 2, 2);
    } else {
        *--p = static_cast<char>('0' + v);
    }
    std::size_t n = static_cast<std::size_t>(end - p);
    std::memcpy(out, p, n);
    return n;
}

inline std::size_t i64_to_chars(std::int64_t v, char* out) noexcept {
    if (v < 0) {
        out[0] = '-';
        std::uint64_t u = static_cast<std::uint64_t>(-(v + 1)) + 1u;
        return 1 + u64_to_chars(u, out + 1);
    }
    return u64_to_chars(static_cast<std::uint64_t>(v), out);
}

inline Iov render_unsigned(char* scratch, std::size_t& pos, std::uint64_t v) noexcept {
    std::size_t old = pos;
    std::size_t n = u64_to_chars(v, scratch + pos);
    pos += n;
    return Iov{scratch + old, n};
}

inline Iov render_signed(char* scratch, std::size_t& pos, std::int64_t v) noexcept {
    std::size_t old = pos;
    std::size_t n = i64_to_chars(v, scratch + pos);
    pos += n;
    return Iov{scratch + old, n};
}

inline Iov render_double(char* scratch, std::size_t& pos, double v) noexcept {
    std::size_t old = pos;
    if (kScratchSize - pos < 32) return Iov{scratch + old, 0};
    std::size_t n = elog::detail::format_double(scratch + pos, v);
    pos += n;
    return Iov{scratch + old, n};
}

inline Iov render_long_double(char* scratch, std::size_t& pos, long double v) noexcept {
    std::size_t old = pos;
    int n = std::snprintf(scratch + pos, kScratchSize - pos, "%Lg", v);
    if (n < 0) n = 0;
    if (static_cast<std::size_t>(n) > kScratchSize - pos) n = static_cast<int>(kScratchSize - pos);
    pos += static_cast<std::size_t>(n);
    return Iov{scratch + old, static_cast<std::size_t>(n)};
}

}  // namespace

Iov elog_render(char* scratch, std::size_t& pos, bool v) noexcept {
    static const char kTrue[] = "true";
    static const char kFalse[] = "false";
    (void)scratch; (void)pos;
    if (v) return Iov{kTrue, 4};
    return Iov{kFalse, 5};
}

Iov elog_render(char* scratch, std::size_t& pos, char v) noexcept {
    std::size_t old = pos;
    scratch[pos++] = v;
    return Iov{scratch + old, 1};
}

Iov elog_render(char* scratch, std::size_t& pos, signed char v) noexcept {
    return render_signed(scratch, pos, static_cast<std::int64_t>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, unsigned char v) noexcept {
    return render_unsigned(scratch, pos, static_cast<std::uint64_t>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, short v) noexcept {
    return render_signed(scratch, pos, static_cast<std::int64_t>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, unsigned short v) noexcept {
    return render_unsigned(scratch, pos, static_cast<std::uint64_t>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, int v) noexcept {
    return render_signed(scratch, pos, static_cast<std::int64_t>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, unsigned int v) noexcept {
    return render_unsigned(scratch, pos, static_cast<std::uint64_t>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, long v) noexcept {
    return render_signed(scratch, pos, static_cast<std::int64_t>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, unsigned long v) noexcept {
    return render_unsigned(scratch, pos, static_cast<std::uint64_t>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, long long v) noexcept {
    return render_signed(scratch, pos, static_cast<std::int64_t>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, unsigned long long v) noexcept {
    return render_unsigned(scratch, pos, static_cast<std::uint64_t>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, float v) noexcept {
    return render_double(scratch, pos, static_cast<double>(v));
}

Iov elog_render(char* scratch, std::size_t& pos, double v) noexcept {
    return render_double(scratch, pos, v);
}

Iov elog_render(char* scratch, std::size_t& pos, long double v) noexcept {
    return render_long_double(scratch, pos, v);
}

Iov elog_render(char* /*scratch*/, std::size_t& /*pos*/, const char* v) noexcept {
    if (v == nullptr) {
        static const char kNull[] = "(null)";
        return Iov{kNull, 6};
    }
    return Iov{v, std::strlen(v)};
}

Iov elog_render(char* /*scratch*/, std::size_t& /*pos*/, string_ref v) noexcept {
    return Iov{v.data(), v.size()};
}

Iov elog_render(char* /*scratch*/, std::size_t& /*pos*/, const std::string& v) noexcept {
    return Iov{v.data(), v.size()};
}

Iov elog_render(char* scratch, std::size_t& pos, const void* v) noexcept {
    std::size_t old = pos;
    scratch[pos++] = '0';
    scratch[pos++] = 'x';
    std::uintptr_t u = reinterpret_cast<std::uintptr_t>(v);
    if (u == 0) {
        scratch[pos++] = '0';
        return Iov{scratch + old, pos - old};
    }
    char tmp[24];
    int n = 0;
    while (u != 0) {
        unsigned d = static_cast<unsigned>(u & 0xF);
        tmp[n++] = static_cast<char>(d < 10 ? '0' + d : 'a' + (d - 10));
        u >>= 4;
    }
    while (n-- > 0) scratch[pos++] = tmp[n];
    return Iov{scratch + old, pos - old};
}

Iov elog_render(char* /*scratch*/, std::size_t& /*pos*/, std::nullptr_t) noexcept {
    static const char kNull[] = "nullptr";
    return Iov{kNull, 7};
}

Iov elog_render(char* scratch, std::size_t& pos, std::thread::id v) noexcept {
    thread_local long cached_tid = 0;
    thread_local std::thread::id cached_id{};
    if (v == std::this_thread::get_id()) {
        if (cached_tid == 0) {
            cached_tid = static_cast<long>(::syscall(SYS_gettid));
            cached_id = v;
        }
        return render_unsigned(scratch, pos, static_cast<std::uint64_t>(cached_tid));
    }
    std::size_t h = std::hash<std::thread::id>{}(v);
    return render_unsigned(scratch, pos, static_cast<std::uint64_t>(h));
}

Iov elog_render(char* scratch, std::size_t& pos,
                std::chrono::system_clock::time_point v) noexcept {
    std::size_t old = pos;
    auto t = std::chrono::system_clock::to_time_t(v);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  v.time_since_epoch()).count();
    long ms = static_cast<long>((us / 1000) % 1000);
    std::tm tm{};
    ::localtime_r(&t, &tm);
    int n = std::snprintf(scratch + pos, kScratchSize - pos,
                          "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
                          tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                          tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    if (n < 0) n = 0;
    pos += static_cast<std::size_t>(n);
    return Iov{scratch + old, static_cast<std::size_t>(n)};
}

Iov elog_render(char* scratch, std::size_t& pos, hex v) noexcept {
    std::size_t old = pos;
    std::uint64_t u = v.v;
    if (u == 0) {
        scratch[pos++] = '0';
        return Iov{scratch + old, 1};
    }
    char tmp[16];
    int n = 0;
    while (u != 0) {
        unsigned d = static_cast<unsigned>(u & 0xF);
        tmp[n++] = static_cast<char>(d < 10 ? '0' + d : 'a' + (d - 10));
        u >>= 4;
    }
    while (n-- > 0) scratch[pos++] = tmp[n];
    return Iov{scratch + old, pos - old};
}

Iov elog_render(char* scratch, std::size_t& pos, bin v) noexcept {
    std::size_t old = pos;
    std::uint64_t u = v.v;
    if (u == 0) {
        scratch[pos++] = '0';
        return Iov{scratch + old, 1};
    }
    char tmp[64];
    int n = 0;
    while (u != 0) {
        tmp[n++] = static_cast<char>('0' + (u & 1u));
        u >>= 1;
    }
    while (n-- > 0) scratch[pos++] = tmp[n];
    return Iov{scratch + old, pos - old};
}

Iov elog_render(char* scratch, std::size_t& pos, fixed v) noexcept {
    std::size_t old = pos;
    int prec = v.prec < 0 ? 0 : v.prec;
    int n = std::snprintf(scratch + pos, kScratchSize - pos, "%.*f", prec, v.v);
    if (n < 0) n = 0;
    if (static_cast<std::size_t>(n) > kScratchSize - pos) n = static_cast<int>(kScratchSize - pos);
    pos += static_cast<std::size_t>(n);
    return Iov{scratch + old, static_cast<std::size_t>(n)};
}

Iov elog_render(char* scratch, std::size_t& pos, quoted v) noexcept {
    std::size_t old = pos;
    if (pos + v.s.size() + 2 > kScratchSize) {
        scratch[pos++] = '"';
        scratch[pos++] = '"';
        return Iov{scratch + old, 2};
    }
    scratch[pos++] = '"';
    std::memcpy(scratch + pos, v.s.data(), v.s.size());
    pos += v.s.size();
    scratch[pos++] = '"';
    return Iov{scratch + old, pos - old};
}

}  // namespace elog
