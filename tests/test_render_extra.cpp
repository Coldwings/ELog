#include "elog/render_extra.hpp"
#include "elog/elog.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace test_render_extra_ns {

inline std::string render_to_string(elog::Iov iv) {
    return std::string(static_cast<const char*>(iv.base), iv.len);
}

template <class T>
inline std::string call_render(const T& v) {
    char scratch[4096];
    std::size_t pos = 0;
    using elog::elog_render;
    elog::Iov iv = elog_render(scratch, pos, v);
    return render_to_string(iv);
}

}  // namespace test_render_extra_ns

using test_render_extra_ns::call_render;
using test_render_extra_ns::render_to_string;

TEST(RenderExtra, Octal) {
    EXPECT_EQ(call_render(elog::oct(8)), "10");
    EXPECT_EQ(call_render(elog::oct(0)), "0");
    EXPECT_EQ(call_render(elog::oct(64)), "100");
}

TEST(RenderExtra, HexdumpShort) {
    unsigned char data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    EXPECT_EQ(call_render(elog::hexdump(data, 4)), "de ad be ef");
}

TEST(RenderExtra, HexdumpTruncatedAt64) {
    unsigned char data[100];
    for (int i = 0; i < 100; ++i) data[i] = static_cast<unsigned char>(i);
    std::string out = call_render(elog::hexdump(data, 100));
    EXPECT_NE(out.find("..."), std::string::npos);
}

TEST(RenderExtra, EscapedBasic) {
    EXPECT_EQ(call_render(elog::escaped("hello\nworld")), "hello\\nworld");
    EXPECT_EQ(call_render(elog::escaped("tab\there")), "tab\\there");
    EXPECT_EQ(call_render(elog::escaped("a\"b")), "a\\\"b");
}

TEST(RenderExtra, EscapedHexNonPrintable) {
    char data[] = {0x01, 0x1F, 'A', 0};
    EXPECT_EQ(call_render(elog::escaped(elog::string_ref(data, 3))), "\\x01\\x1fA");
}

TEST(RenderExtra, PadLeftNumber) {
    EXPECT_EQ(call_render(elog::pad_left(42, 6)), "    42");
    EXPECT_EQ(call_render(elog::pad_left(42, 6, '0')), "000042");
}

TEST(RenderExtra, PadLeftString) {
    std::string s = "hi";
    EXPECT_EQ(call_render(elog::pad_left(s, 5)), "   hi");
}

TEST(RenderExtra, PadRightNumber) {
    EXPECT_EQ(call_render(elog::pad_right(42, 6)), "42    ");
    EXPECT_EQ(call_render(elog::pad_right(42, 6, '_')), "42____");
}

TEST(RenderExtra, PadRightString) {
    std::string s = "hi";
    EXPECT_EQ(call_render(elog::pad_right(s, 5)), "hi   ");
}

TEST(RenderExtra, PadShorterThanWidthNoOp) {
    EXPECT_EQ(call_render(elog::pad_left(123456, 3)), "123456");
}

TEST(RenderExtra, PrecisionAlias) {
    EXPECT_EQ(call_render(elog::precision(3.14159, 2)), "3.14");
}

TEST(RenderExtra, NullableNull) {
    int* p = nullptr;
    EXPECT_EQ(call_render(elog::nullable<int>{p}), "<null>");
}

TEST(RenderExtra, NullableValue) {
    int x = 42;
    EXPECT_EQ(call_render(elog::nullable<int>{&x}), "42");
}

TEST(RenderExtra, JoinIntVector) {
    std::vector<int> v = {1, 2, 3};
    EXPECT_EQ(call_render(elog::join(v, ", ")), "1, 2, 3");
}

TEST(RenderExtra, JoinStringVector) {
    std::vector<std::string> v = {"a", "b", "c"};
    EXPECT_EQ(call_render(elog::join(v, "-")), "a-b-c");
}

TEST(RenderExtra, JoinEmpty) {
    std::vector<int> v;
    EXPECT_EQ(call_render(elog::join(v, ",")), "");
}

TEST(RenderExtra, VectorInts) {
    std::vector<int> v = {1, 2, 3};
    EXPECT_EQ(call_render(v), "[1, 2, 3]");
}

TEST(RenderExtra, VectorEmpty) {
    std::vector<int> v;
    EXPECT_EQ(call_render(v), "[]");
}

TEST(RenderExtra, VectorStrings) {
    std::vector<std::string> v = {"a", "b"};
    EXPECT_EQ(call_render(v), "[a, b]");
}

TEST(RenderExtra, ArrayInts) {
    std::array<int, 3> a = {{10, 20, 30}};
    EXPECT_EQ(call_render(a), "[10, 20, 30]");
}

TEST(RenderExtra, PairIntString) {
    std::pair<int, std::string> p{1, "x"};
    EXPECT_EQ(call_render(p), "(1, x)");
}

TEST(RenderExtra, TupleEmpty) {
    std::tuple<> t;
    EXPECT_EQ(call_render(t), "()");
}

TEST(RenderExtra, TupleSingle) {
    std::tuple<int> t{7};
    EXPECT_EQ(call_render(t), "(7)");
}

TEST(RenderExtra, TupleMulti) {
    std::tuple<int, std::string, double> t{1, "two", 3.0};
    EXPECT_EQ(call_render(t), "(1, two, 3)");
}

TEST(RenderExtra, MapOrdered) {
    std::map<std::string, int> m;
    m["a"] = 1;
    m["b"] = 2;
    EXPECT_EQ(call_render(m), "{a: 1, b: 2}");
}

TEST(RenderExtra, UnorderedMap) {
    std::unordered_map<std::string, int> m;
    m["only"] = 99;
    std::string out = call_render(m);
    EXPECT_EQ(out, "{only: 99}");
}

TEST(RenderExtra, SetOrdered) {
    std::set<int> s = {3, 1, 2};
    EXPECT_EQ(call_render(s), "{1, 2, 3}");
}

TEST(RenderExtra, UnorderedSetSingle) {
    std::unordered_set<int> s = {42};
    EXPECT_EQ(call_render(s), "{42}");
}

TEST(RenderExtra, NestedVectorPair) {
    std::vector<std::pair<int, std::string>> v = {{1, "a"}, {2, "b"}};
    EXPECT_EQ(call_render(v), "[(1, a), (2, b)]");
}

namespace user_ns {
struct Point {
    int x, y;
};

inline elog::Iov elog_render(char* scratch, std::size_t& pos, const Point& p) noexcept {
    std::size_t start = pos;
    using elog::elog_render;
    char ch = '(';
    if (pos < 4096) scratch[pos++] = ch;
    elog::Iov xi = elog_render(scratch, pos, p.x);
    if (xi.base != static_cast<const void*>(scratch + (pos - xi.len))) {
        for (std::size_t i = 0; i < xi.len && pos < 4096; ++i) {
            scratch[pos++] = static_cast<const char*>(xi.base)[i];
        }
    }
    if (pos + 1 < 4096) { scratch[pos++] = ','; scratch[pos++] = ' '; }
    elog::Iov yi = elog_render(scratch, pos, p.y);
    if (yi.base != static_cast<const void*>(scratch + (pos - yi.len))) {
        for (std::size_t i = 0; i < yi.len && pos < 4096; ++i) {
            scratch[pos++] = static_cast<const char*>(yi.base)[i];
        }
    }
    if (pos < 4096) scratch[pos++] = ')';
    return elog::Iov{scratch + start, pos - start};
}
}  // namespace user_ns

TEST(RenderExtra, UserAdlExtension) {
    user_ns::Point p{3, 7};
    char scratch[4096];
    std::size_t pos = 0;
    using user_ns::elog_render;
    elog::Iov iv = elog_render(scratch, pos, p);
    EXPECT_EQ(render_to_string(iv), "(3, 7)");
}
