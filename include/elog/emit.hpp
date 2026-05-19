#pragma once

#include "format.hpp"
#include "iov.hpp"
#include "iov_pack.hpp"
#include "level.hpp"
#include "logger.hpp"
#include "prologue.hpp"
#include "render.hpp"
#include "render_extra.hpp"
#include "scratch.hpp"

#include <chrono>
#include <cstddef>
#include <sys/uio.h>
#include <thread>
#include <type_traits>
#include <utility>

namespace elog {
namespace detail {

template <class T> struct is_pack : std::false_type {};
template <> struct is_pack<iov_pack> : std::true_type {};

template <class... Args> struct any_pack : std::false_type {};
template <class T, class... Rest>
struct any_pack<T, Rest...> : std::conditional<
    is_pack<typename std::decay<T>::type>::value,
    std::true_type,
    any_pack<Rest...>>::type {};

// ---------- Fast path: no iov_pack args ----------

template <std::size_t N, std::size_t L, class... Args>
inline typename std::enable_if<!any_pack<Args...>::value, void>::type
emit_f(Logger& logger, Level lvl, const char* file, int line,
       const FmtSpec<N, L>& spec, Args&&... args) {
    using ::elog::elog_render;

    char* scratch = ::elog::tls_scratch();
    std::size_t pos = 0;

    LogCtx ctx{lvl, file, line,
               std::chrono::system_clock::now(),
               std::this_thread::get_id()};
    Iov pro{nullptr, 0};
    logger.prologue_fn()(scratch, pos, pro, ctx);

    constexpr std::size_t NA = sizeof...(Args) == 0 ? 1 : sizeof...(Args);
    Iov arg_iovs[NA];
    arg_iovs[0] = Iov{nullptr, 0};
    int idx = 0;
    using expand = int[];
    (void)expand{0,
        (arg_iovs[idx] = elog_render(scratch, pos, std::forward<Args>(args)),
         ++idx, 0)...};
    (void)idx;

    constexpr std::size_t IC = N + 2;
    iovec iov[IC];
    int ic = 0;
    iov[ic].iov_base = const_cast<void*>(pro.base);
    iov[ic].iov_len = pro.len;
    ++ic;
    for (std::size_t i = 0; i < N; ++i) {
        const Piece& p = spec.pieces[i];
        if (p.lit_offset != kHoleSentinel) {
            iov[ic].iov_base = const_cast<void*>(
                static_cast<const void*>(spec.lit_data + p.lit_offset));
            iov[ic].iov_len = p.len;
        } else {
            const Iov& a = arg_iovs[p.arg_idx];
            iov[ic].iov_base = const_cast<void*>(a.base);
            iov[ic].iov_len = a.len;
        }
        ++ic;
    }
    static const char nl = '\n';
    iov[ic].iov_base = const_cast<void*>(static_cast<const void*>(&nl));
    iov[ic].iov_len = 1;
    ++ic;

    logger.emit(lvl, iov, ic);
}

// ---------- Pack path: at least one arg is iov_pack ----------

struct ArgSlot {
    const Iov* base;          // non-null ⇒ pack of `count` entries
    std::size_t count;
    Iov single;               // used iff base == nullptr
};

template <class T>
inline typename std::enable_if<!is_pack<typename std::decay<T>::type>::value,
                               ArgSlot>::type
make_arg_slot(char* scratch, std::size_t& pos, T&& v) {
    using ::elog::elog_render;
    return ArgSlot{nullptr, 0, elog_render(scratch, pos, std::forward<T>(v))};
}

inline ArgSlot make_arg_slot(char* /*scratch*/, std::size_t& /*pos*/,
                             const iov_pack& p) {
    return ArgSlot{p.base, p.count, Iov{nullptr, 0}};
}

template <std::size_t N, std::size_t L, class... Args>
inline typename std::enable_if<any_pack<Args...>::value, void>::type
emit_f(Logger& logger, Level lvl, const char* file, int line,
       const FmtSpec<N, L>& spec, Args&&... args) {
    char* scratch = ::elog::tls_scratch();
    std::size_t pos = 0;

    LogCtx ctx{lvl, file, line,
               std::chrono::system_clock::now(),
               std::this_thread::get_id()};
    Iov pro{nullptr, 0};
    logger.prologue_fn()(scratch, pos, pro, ctx);

    constexpr std::size_t NA = sizeof...(Args) == 0 ? 1 : sizeof...(Args);
    ArgSlot arg_slots[NA];
    arg_slots[0] = ArgSlot{nullptr, 0, Iov{nullptr, 0}};
    int idx = 0;
    using expand = int[];
    (void)expand{0,
        (arg_slots[idx] = make_arg_slot(scratch, pos, std::forward<Args>(args)),
         ++idx, 0)...};
    (void)idx;

    // Worst case: each hole is an iov_pack carrying up to kPackMax entries.
    // 32 covers typical structured-log fan-outs; larger users should split
    // the LOG call.
    constexpr std::size_t kPackMax = 32;
    constexpr std::size_t IC = N * kPackMax + 2;
    iovec iov[IC];
    int ic = 0;
    iov[ic].iov_base = const_cast<void*>(pro.base);
    iov[ic].iov_len = pro.len;
    ++ic;
    for (std::size_t i = 0; i < N; ++i) {
        const Piece& p = spec.pieces[i];
        if (p.lit_offset != kHoleSentinel) {
            iov[ic].iov_base = const_cast<void*>(
                static_cast<const void*>(spec.lit_data + p.lit_offset));
            iov[ic].iov_len = p.len;
            ++ic;
        } else {
            const ArgSlot& a = arg_slots[p.arg_idx];
            if (a.base != nullptr) {
                std::size_t n = a.count > kPackMax ? kPackMax : a.count;
                for (std::size_t k = 0; k < n; ++k) {
                    iov[ic].iov_base = const_cast<void*>(a.base[k].base);
                    iov[ic].iov_len = a.base[k].len;
                    ++ic;
                }
            } else {
                iov[ic].iov_base = const_cast<void*>(a.single.base);
                iov[ic].iov_len = a.single.len;
                ++ic;
            }
        }
    }
    static const char nl = '\n';
    iov[ic].iov_base = const_cast<void*>(static_cast<const void*>(&nl));
    iov[ic].iov_len = 1;
    ++ic;

    logger.emit(lvl, iov, ic);
}

}  // namespace detail
}  // namespace elog
