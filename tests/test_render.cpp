#include <gtest/gtest.h>

#include "elog/render.hpp"
#include "elog/scratch.hpp"

#include <cstring>
#include <string>

using elog::Iov;
using elog::elog_render;
using elog::string_ref;

namespace {

std::string iov_to_string(const Iov& v) {
    return std::string(static_cast<const char*>(v.base), v.len);
}

}  // namespace

TEST(Render, Bool) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    auto a = elog_render(sc, pos, true);
    auto b = elog_render(sc, pos, false);
    EXPECT_EQ(iov_to_string(a), "true");
    EXPECT_EQ(iov_to_string(b), "false");
    EXPECT_EQ(pos, 0u);  // bool returns static strings, no scratch use
}

TEST(Render, Char) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    auto v = elog_render(sc, pos, 'x');
    EXPECT_EQ(iov_to_string(v), "x");
    EXPECT_EQ(pos, 1u);
}

TEST(Render, Integers) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;

    auto a = elog_render(sc, pos, 0);
    EXPECT_EQ(iov_to_string(a), "0");

    auto b = elog_render(sc, pos, -1234);
    EXPECT_EQ(iov_to_string(b), "-1234");

    auto c = elog_render(sc, pos, static_cast<unsigned long long>(18446744073709551615ull));
    EXPECT_EQ(iov_to_string(c), "18446744073709551615");

    auto d = elog_render(sc, pos, static_cast<long long>(-9223372036854775807LL - 1));
    EXPECT_EQ(iov_to_string(d), "-9223372036854775808");

    auto e = elog_render(sc, pos, static_cast<short>(-32768));
    EXPECT_EQ(iov_to_string(e), "-32768");

    auto f = elog_render(sc, pos, 42);
    EXPECT_EQ(iov_to_string(f), "42");

    EXPECT_GE(reinterpret_cast<const char*>(b.base), sc);
    EXPECT_LT(reinterpret_cast<const char*>(b.base), sc + 4096);
}

TEST(Render, Floats) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    auto a = elog_render(sc, pos, 3.14);
    EXPECT_EQ(iov_to_string(a), "3.14");

    pos = 0;
    auto b = elog_render(sc, pos, 1.5f);
    EXPECT_EQ(iov_to_string(b), "1.5");
}

TEST(Render, CString_ZeroCopy) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    const char* msg = "hello world";
    auto v = elog_render(sc, pos, msg);
    EXPECT_EQ(v.base, static_cast<const void*>(msg));
    EXPECT_EQ(v.len, std::strlen(msg));
    EXPECT_EQ(pos, 0u);
}

TEST(Render, CharArray_ZeroCopy) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    const char buf[] = "literal";
    auto v = elog_render(sc, pos, buf);
    EXPECT_EQ(v.base, static_cast<const void*>(&buf[0]));
    EXPECT_EQ(v.len, 7u);  // excludes terminator
    EXPECT_EQ(pos, 0u);
}

TEST(Render, StringRef_ZeroCopy) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    const char* src = "abcdef";
    string_ref s(src, 6);
    auto v = elog_render(sc, pos, s);
    EXPECT_EQ(v.base, static_cast<const void*>(src));
    EXPECT_EQ(v.len, 6u);
    EXPECT_EQ(pos, 0u);
}

TEST(Render, StdString_ZeroCopy) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    std::string s = "stdstring";
    auto v = elog_render(sc, pos, s);
    EXPECT_EQ(v.base, static_cast<const void*>(s.data()));
    EXPECT_EQ(v.len, s.size());
    EXPECT_EQ(pos, 0u);
}

TEST(Render, Pointer) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    int x = 0;
    auto v = elog_render(sc, pos, static_cast<const void*>(&x));
    auto str = iov_to_string(v);
    EXPECT_EQ(str.substr(0, 2), "0x");
    EXPECT_GT(str.size(), 2u);

    pos = 0;
    auto z = elog_render(sc, pos, static_cast<const void*>(nullptr));
    EXPECT_EQ(iov_to_string(z), "0x0");
}

TEST(Render, NullPtr) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    auto v = elog_render(sc, pos, nullptr);
    EXPECT_EQ(iov_to_string(v), "nullptr");
}

TEST(Render, ThreadId) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    std::thread::id id = std::this_thread::get_id();
    auto v = elog_render(sc, pos, id);
    EXPECT_GT(v.len, 0u);
    EXPECT_GE(reinterpret_cast<const char*>(v.base), sc);
}

TEST(Render, TimePoint) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    auto tp = std::chrono::system_clock::now();
    auto v = elog_render(sc, pos, tp);
    auto s = iov_to_string(v);
    EXPECT_EQ(s.size(), 23u);  // YYYY-MM-DD HH:MM:SS.mmm
    EXPECT_EQ(s[4], '-');
    EXPECT_EQ(s[7], '-');
    EXPECT_EQ(s[10], ' ');
    EXPECT_EQ(s[13], ':');
    EXPECT_EQ(s[16], ':');
    EXPECT_EQ(s[19], '.');
}

TEST(Render, FormatHex) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    auto a = elog_render(sc, pos, elog::hex(255));
    EXPECT_EQ(iov_to_string(a), "ff");
    pos = 0;
    auto b = elog_render(sc, pos, elog::hex(0));
    EXPECT_EQ(iov_to_string(b), "0");
    pos = 0;
    auto c = elog_render(sc, pos, elog::hex(0xdeadbeef));
    EXPECT_EQ(iov_to_string(c), "deadbeef");
}

TEST(Render, FormatBin) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    auto a = elog_render(sc, pos, elog::bin(5));
    EXPECT_EQ(iov_to_string(a), "101");
    pos = 0;
    auto b = elog_render(sc, pos, elog::bin(0));
    EXPECT_EQ(iov_to_string(b), "0");
}

TEST(Render, FormatFixed) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    auto a = elog_render(sc, pos, elog::fixed(1.5, 3));
    EXPECT_EQ(iov_to_string(a), "1.500");
    pos = 0;
    auto b = elog_render(sc, pos, elog::fixed(0.0, 0));
    EXPECT_EQ(iov_to_string(b), "0");
}

TEST(Render, FormatQuoted) {
    char* sc = elog::tls_scratch();
    std::size_t pos = 0;
    auto a = elog_render(sc, pos, elog::quoted("hi"));
    EXPECT_EQ(iov_to_string(a), "\"hi\"");
}
