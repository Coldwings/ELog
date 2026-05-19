#pragma once

#include "iov.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace elog {

// iov_pack lets a single LOG_*_F argument expand into multiple iovec entries
// without the renderer having to memcpy borrowed bytes into scratch. emit_f
// recognizes this type at compile time and splices its entries directly into
// the per-call iov array.
//
// Use it when you have N pieces (some borrowed, some owned) that you want to
// log as one logical value with zero-copy preserved across all of them.
//
//   std::vector<elog::Iov> pieces;
//   pieces.push_back({"key=", 4});
//   pieces.push_back({k.data(), k.size()});       // borrowed
//   pieces.push_back({" val=", 5});
//   pieces.push_back({v.data(), v.size()});       // borrowed
//   LOG_INFO_F("entry: {}", elog::iov_pack(pieces));
//
// The pack itself is a {base, count} view; it does NOT own the iovs. They
// must outlive the LOG call (which is the same lifetime requirement that
// already applies to string args borrowed via Iov).
struct iov_pack {
    const Iov* base;
    std::size_t count;

    constexpr iov_pack(const Iov* b, std::size_t n) noexcept
        : base(b), count(n) {}

    iov_pack(const std::vector<Iov>& v) noexcept
        : base(v.data()), count(v.size()) {}

    template <std::size_t N>
    constexpr iov_pack(const std::array<Iov, N>& a) noexcept
        : base(a.data()), count(N) {}

    template <std::size_t N>
    constexpr iov_pack(const Iov (&a)[N]) noexcept
        : base(a), count(N) {}
};

}  // namespace elog
