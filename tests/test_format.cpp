#include <gtest/gtest.h>

#include "elog/format.hpp"

using elog::count_pieces;
using elog::count_lit_chars;
using elog::make_spec;
using elog::kHoleSentinel;

static_assert(count_pieces("hello {} world {}") == 4, "merge lit/hole/lit/hole");
static_assert(count_pieces("") == 0, "empty");
static_assert(count_pieces("{}") == 1, "single hole");
static_assert(count_pieces("{{x}}") == 1, "escapes merge into single literal");
static_assert(count_pieces("a {} b {} c") == 5, "five pieces");
static_assert(count_pieces("{}{}") == 2, "adjacent holes");
static_assert(count_pieces("plain text") == 1, "single literal");

static_assert(count_lit_chars("") == 0, "");
static_assert(count_lit_chars("{}") == 0, "");
static_assert(count_lit_chars("{{x}}") == 3, "");      // {x}
static_assert(count_lit_chars("hello {}") == 6, "");
static_assert(count_lit_chars("plain text") == 10, "");

TEST(Format, EmptyString) {
    constexpr auto spec = make_spec<count_pieces(""), count_lit_chars("")>("");
    EXPECT_EQ(spec.hole_count, 0u);
}

TEST(Format, SingleHole) {
    constexpr auto spec = make_spec<count_pieces("{}"), count_lit_chars("{}")>("{}");
    EXPECT_EQ(spec.hole_count, 1u);
    EXPECT_EQ(spec.pieces[0].lit_offset, kHoleSentinel);
    EXPECT_EQ(spec.pieces[0].arg_idx, 0u);
}

TEST(Format, HelloHole) {
    constexpr const char fmt[] = "hello {}";
    constexpr auto spec = make_spec<count_pieces(fmt), count_lit_chars(fmt)>(fmt);
    EXPECT_EQ(spec.hole_count, 1u);
    EXPECT_NE(spec.pieces[0].lit_offset, kHoleSentinel);
    EXPECT_EQ(spec.pieces[0].len, 6u);
    EXPECT_EQ(std::string(spec.lit_data + spec.pieces[0].lit_offset,
                          spec.pieces[0].len),
              "hello ");
    EXPECT_EQ(spec.pieces[1].lit_offset, kHoleSentinel);
    EXPECT_EQ(spec.pieces[1].arg_idx, 0u);
}

TEST(Format, MultipleHoles) {
    constexpr const char fmt[] = "x={}, y={}";
    constexpr auto spec = make_spec<count_pieces(fmt), count_lit_chars(fmt)>(fmt);
    EXPECT_EQ(spec.hole_count, 2u);
    EXPECT_NE(spec.pieces[0].lit_offset, kHoleSentinel);
    EXPECT_EQ(std::string(spec.lit_data + spec.pieces[0].lit_offset,
                          spec.pieces[0].len), "x=");
    EXPECT_EQ(spec.pieces[1].lit_offset, kHoleSentinel);
    EXPECT_EQ(spec.pieces[1].arg_idx, 0u);
    EXPECT_EQ(std::string(spec.lit_data + spec.pieces[2].lit_offset,
                          spec.pieces[2].len), ", y=");
    EXPECT_EQ(spec.pieces[3].lit_offset, kHoleSentinel);
    EXPECT_EQ(spec.pieces[3].arg_idx, 1u);
}

TEST(Format, EscapesMerge) {
    constexpr const char fmt[] = "{{x}}";
    constexpr auto spec = make_spec<count_pieces(fmt), count_lit_chars(fmt)>(fmt);
    EXPECT_EQ(spec.hole_count, 0u);
    EXPECT_NE(spec.pieces[0].lit_offset, kHoleSentinel);
    EXPECT_EQ(spec.pieces[0].len, 3u);
    EXPECT_EQ(std::string(spec.lit_data + spec.pieces[0].lit_offset,
                          spec.pieces[0].len), "{x}");
}
