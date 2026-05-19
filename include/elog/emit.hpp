#pragma once

#include "format.hpp"
#include "iov.hpp"
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
#include <utility>

namespace elog {
namespace detail {

template <std::size_t N, std::size_t L, class... Args>
inline void emit_f(Logger& logger, Level lvl, const char* file, int line,
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

}  // namespace detail
}  // namespace elog
