#pragma once

#include "sink.hpp"

#include <cstddef>
#include <sys/uio.h>

// Helpers for users who want to wrap a synchronous Sink behind their
// own async queue (SPSC ring, lockfree mpmc, etc.). ELog ships these
// utilities but does NOT provide a built-in async implementation —
// queue policies (drop / block / overwrite), worker thread management,
// and shutdown ordering vary too widely. See examples/async_sink_skeleton.cpp
// for a working starting point.
//
// Lifetime contract recap:
//   - Sink::write() receives iov entries that may live only until the
//     call returns (most point into TLS scratch belonging to the calling
//     thread; static literals point into .rodata and live forever, but
//     the callee can't tell which is which without inspecting addresses).
//   - An async wrapper MUST flatten the iov payload into its own owned
//     buffer before returning from write().
//   - Worker thread later replays via the inner sink's write() with a
//     single-iov view of the owned buffer.

namespace elog {

inline std::size_t iov_total_bytes(const iovec* iov, int n) noexcept {
    std::size_t total = 0;
    for (int i = 0; i < n; ++i) total += iov[i].iov_len;
    return total;
}

// Flatten the iov payload into a contiguous buffer of size at least
// iov_total_bytes(iov, n). Returns the number of bytes written.
inline std::size_t iov_serialize(const iovec* iov, int n, char* out) noexcept {
    std::size_t pos = 0;
    for (int i = 0; i < n; ++i) {
        const auto* src = static_cast<const unsigned char*>(iov[i].iov_base);
        for (std::size_t k = 0; k < iov[i].iov_len; ++k) {
            out[pos++] = static_cast<char>(src[k]);
        }
    }
    return pos;
}

}  // namespace elog
