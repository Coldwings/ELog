#pragma once

#include <cstddef>
#include <cstdint>

namespace elog {

// kHoleSentinel marks a Piece as a hole. We use a u16 offset rather
// than a const char* lit because forming pointers into a constexpr
// FmtSpec object's own buffer is not possible in C++14 constant
// evaluation. The lit bytes live in FmtSpec::lit_data; pieces refer
// to them by offset.
constexpr std::uint16_t kHoleSentinel = static_cast<std::uint16_t>(0xFFFFu);

struct Piece {
    std::uint16_t lit_offset;
    std::uint16_t len;
    std::uint16_t arg_idx;
};

constexpr bool piece_is_hole(const Piece& p) noexcept {
    return p.lit_offset == kHoleSentinel;
}

constexpr std::size_t count_pieces(const char* s) noexcept {
    std::size_t pieces = 0;
    bool in_lit = false;
    while (*s != '\0') {
        char c1 = s[0];
        char c2 = c1 ? s[1] : '\0';
        if (c1 == '{' && c2 == '{') {
            if (!in_lit) { ++pieces; in_lit = true; }
            s += 2;
        } else if (c1 == '}' && c2 == '}') {
            if (!in_lit) { ++pieces; in_lit = true; }
            s += 2;
        } else if (c1 == '{' && c2 == '}') {
            ++pieces;
            in_lit = false;
            s += 2;
        } else {
            if (!in_lit) { ++pieces; in_lit = true; }
            ++s;
        }
    }
    return pieces;
}

constexpr std::size_t count_lit_chars(const char* s) noexcept {
    std::size_t n = 0;
    while (*s != '\0') {
        char c1 = s[0];
        char c2 = c1 ? s[1] : '\0';
        if (c1 == '{' && c2 == '{') { ++n; s += 2; }
        else if (c1 == '}' && c2 == '}') { ++n; s += 2; }
        else if (c1 == '{' && c2 == '}') { s += 2; }
        else { ++n; ++s; }
    }
    return n;
}

template <std::size_t N, std::size_t L>
struct FmtSpec {
    Piece pieces[N == 0 ? 1 : N];
    char  lit_data[L == 0 ? 1 : L];
    std::size_t hole_count;
};

template <std::size_t N, std::size_t L>
constexpr FmtSpec<N, L> make_spec(const char* s) noexcept {
    FmtSpec<N, L> spec{};
    spec.hole_count = 0;
    std::size_t pi = 0;
    std::size_t li = 0;
    bool in_lit = false;
    std::uint16_t cur_off = 0;
    std::uint16_t cur_len = 0;
    std::uint16_t arg_idx = 0;

    while (*s != '\0') {
        char c1 = s[0];
        char c2 = c1 ? s[1] : '\0';
        if (c1 == '{' && c2 == '{') {
            if (!in_lit) {
                in_lit = true;
                cur_off = static_cast<std::uint16_t>(li);
                cur_len = 0;
            }
            spec.lit_data[li++] = '{';
            ++cur_len;
            s += 2;
        } else if (c1 == '}' && c2 == '}') {
            if (!in_lit) {
                in_lit = true;
                cur_off = static_cast<std::uint16_t>(li);
                cur_len = 0;
            }
            spec.lit_data[li++] = '}';
            ++cur_len;
            s += 2;
        } else if (c1 == '{' && c2 == '}') {
            if (in_lit) {
                spec.pieces[pi].lit_offset = cur_off;
                spec.pieces[pi].len = cur_len;
                spec.pieces[pi].arg_idx = 0;
                ++pi;
                in_lit = false;
            }
            spec.pieces[pi].lit_offset = kHoleSentinel;
            spec.pieces[pi].len = 0;
            spec.pieces[pi].arg_idx = arg_idx++;
            ++pi;
            s += 2;
        } else {
            if (!in_lit) {
                in_lit = true;
                cur_off = static_cast<std::uint16_t>(li);
                cur_len = 0;
            }
            spec.lit_data[li++] = c1;
            ++cur_len;
            ++s;
        }
    }
    if (in_lit) {
        spec.pieces[pi].lit_offset = cur_off;
        spec.pieces[pi].len = cur_len;
        spec.pieces[pi].arg_idx = 0;
        ++pi;
    }
    spec.hole_count = arg_idx;
    return spec;
}

}  // namespace elog
