#include "elog/prologue.hpp"
#include "elog/scratch.hpp"

#include <cstring>
#include <ctime>
#include <functional>

#include <sys/syscall.h>
#include <unistd.h>

namespace elog {

namespace {

inline std::size_t write_two(char* p, unsigned v) noexcept {
    p[0] = static_cast<char>('0' + (v / 10));
    p[1] = static_cast<char>('0' + (v % 10));
    return 2;
}

inline std::size_t write_six(char* p, unsigned v) noexcept {
    p[0] = static_cast<char>('0' + (v / 100000));
    p[1] = static_cast<char>('0' + ((v / 10000) % 10));
    p[2] = static_cast<char>('0' + ((v / 1000) % 10));
    p[3] = static_cast<char>('0' + ((v / 100) % 10));
    p[4] = static_cast<char>('0' + ((v / 10) % 10));
    p[5] = static_cast<char>('0' + (v % 10));
    return 6;
}

inline std::size_t write_uint(char* p, std::uint64_t v) noexcept {
    char tmp[24];
    int n = 0;
    if (v == 0) { p[0] = '0'; return 1; }
    while (v != 0) {
        tmp[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    std::size_t out = static_cast<std::size_t>(n);
    while (n-- > 0) *p++ = tmp[n];
    return out;
}

inline std::size_t write_int(char* p, int v) noexcept {
    if (v < 0) {
        *p++ = '-';
        return 1 + write_uint(p, static_cast<std::uint64_t>(-(static_cast<long long>(v))));
    }
    return write_uint(p, static_cast<std::uint64_t>(v));
}

inline const char* basename_or_self(const char* path) noexcept {
    const char* last = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/') last = p + 1;
    }
    return last;
}

}  // namespace

void default_prologue(char* scratch, std::size_t& pos, Iov& out, const LogCtx& ctx) {
    static thread_local std::time_t cached_sec = -1;
    static thread_local char cached_str[20];  // "YYYY-MM-DD HH:MM:SS"
    static thread_local std::size_t cached_tid_len = 0;
    static thread_local char cached_tid[24];
    static thread_local std::thread::id cached_tid_id{};
    static thread_local bool tid_cached = false;

    auto t = std::chrono::system_clock::to_time_t(ctx.tp);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  ctx.tp.time_since_epoch()).count();
    long us_part = static_cast<long>(us % 1000000);
    if (us_part < 0) us_part = 0;

    if (t != cached_sec) {
        std::tm tm{};
        ::localtime_r(&t, &tm);
        unsigned y = static_cast<unsigned>(tm.tm_year + 1900);
        cached_str[0] = static_cast<char>('0' + (y / 1000) % 10);
        cached_str[1] = static_cast<char>('0' + (y / 100) % 10);
        cached_str[2] = static_cast<char>('0' + (y / 10) % 10);
        cached_str[3] = static_cast<char>('0' + y % 10);
        cached_str[4] = '-';
        write_two(cached_str + 5, static_cast<unsigned>(tm.tm_mon + 1));
        cached_str[7] = '-';
        write_two(cached_str + 8, static_cast<unsigned>(tm.tm_mday));
        cached_str[10] = ' ';
        write_two(cached_str + 11, static_cast<unsigned>(tm.tm_hour));
        cached_str[13] = ':';
        write_two(cached_str + 14, static_cast<unsigned>(tm.tm_min));
        cached_str[16] = ':';
        write_two(cached_str + 17, static_cast<unsigned>(tm.tm_sec));
        cached_str[19] = '\0';
        cached_sec = t;
    }

    if (!tid_cached || cached_tid_id != ctx.tid) {
        std::uint64_t id_value;
        if (ctx.tid == std::this_thread::get_id()) {
            id_value = static_cast<std::uint64_t>(::syscall(SYS_gettid));
        } else {
            id_value = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(ctx.tid));
        }
        cached_tid_len = write_uint(cached_tid, id_value);
        cached_tid_id = ctx.tid;
        tid_cached = true;
    }

    std::size_t old = pos;
    char* base = scratch + pos;

    std::memcpy(base, cached_str, 19);
    base += 19;
    *base++ = '.';
    base += write_six(base, static_cast<unsigned>(us_part));
    *base++ = ' ';

    const char* lname = level_name(ctx.level);
    std::size_t ln = std::strlen(lname);
    std::memcpy(base, lname, ln);
    base += ln;
    *base++ = ' ';
    *base++ = '[';
    std::memcpy(base, cached_tid, cached_tid_len);
    base += cached_tid_len;
    *base++ = ']';
    *base++ = ' ';

    const char* fn = basename_or_self(ctx.file ? ctx.file : "");
    std::size_t fnlen = std::strlen(fn);
    std::memcpy(base, fn, fnlen);
    base += fnlen;
    *base++ = ':';
    base += write_int(base, ctx.line);
    *base++ = ' ';
    *base++ = '|';
    *base++ = ' ';

    std::size_t written = static_cast<std::size_t>(base - (scratch + old));
    pos = old + written;
    out = Iov{scratch + old, written};
}

}  // namespace elog
