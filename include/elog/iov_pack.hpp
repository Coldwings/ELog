#pragma once

#include "iov.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace elog {

namespace detail {
struct IovScratchCtx {
    Iov* base;
    std::size_t cap;
    std::size_t pos;
};

// Per-thread pointer to the iov scratch supplied by the currently-running
// emit_f. Null when no LOG call is in flight.
inline IovScratchCtx*& tls_iov_ctx() noexcept {
    thread_local IovScratchCtx* p = nullptr;
    return p;
}
}  // namespace detail

// Allocate `n` Iov slots out of the per-LOG-call scratch supplied by the
// currently-running emit_f. The returned pointer is stable for the rest of
// the LOG call (until emit_f returns). Must only be called from inside an
// elog_render that is being invoked by emit_f. Returns nullptr if no LOG is
// in flight or if the scratch is exhausted (capacity is per-call and bounded
// by the worst-case number of iov entries in one LOG call).
inline Iov* iov_scratch_alloc(std::size_t n) noexcept {
    auto* ctx = ::elog::detail::tls_iov_ctx();
    if (ctx == nullptr) return nullptr;
    if (ctx->pos + n > ctx->cap) return nullptr;
    Iov* p = ctx->base + ctx->pos;
    ctx->pos += n;
    return p;
}

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
