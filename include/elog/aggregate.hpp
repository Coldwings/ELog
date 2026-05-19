#pragma once

#include "level.hpp"
#include "logger.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace elog {
namespace detail {

// ---------------- EveryNState ----------------
// Emits on call counts 1, 1+n, 1+2n, ...
// Implementation: post-increment counter, return true when (old % n) == 0.
struct EveryNState {
    std::atomic<std::uint64_t> count{0};

    bool tick(std::uint64_t n) noexcept {
        if (n == 0) return true;
        std::uint64_t c = count.fetch_add(1, std::memory_order_relaxed);
        return (c % n) == 0;
    }
};

// ---------------- EveryTimeState ----------------
// At most one emit per period. Stores next-allowed steady_clock time-point in
// nanoseconds. Initial value 0 means first call always passes.
struct EveryTimeState {
    std::atomic<std::int64_t> next_ns{0};

    bool tick(std::int64_t period_ns) noexcept;
};

// ---------------- FirstNState ----------------
// Emits only the first n calls.
struct FirstNState {
    std::atomic<std::uint64_t> count{0};

    bool tick(std::uint64_t n) noexcept {
        std::uint64_t c = count.fetch_add(1, std::memory_order_relaxed);
        return c < n;
    }
};

// ---------------- SampledState ----------------
// Thread-local xorshift64 PRNG. State is shared per-thread, not per-call-site,
// so SampledState has no instance data and tick is static.
struct SampledState {
    static bool tick(std::uint32_t prob_percent) noexcept;
};

// ---------------- BurstState ----------------
// Token bucket with capacity=burst, refill rate refill_per_sec.
// Mutex-protected; per-call-site contention is normally zero.
struct BurstState {
    std::mutex mu;
    double tokens{0.0};
    std::int64_t last_ns{0};
    bool initialized{false};

    bool tick(std::uint32_t burst, double refill_per_sec) noexcept;
};

// ---------------- DedupState ----------------
// Per-call-site state for consecutive-call-site dedup.
//
// Behavior:
//  - On call: if g_last_dedup == this, ++suppressed and return false (suppress).
//  - Otherwise: exchange g_last_dedup to this. If old != null, flush old,
//    then return true (current emits normally).
//  - On Logger flush / atexit: flush all DedupStates with non-zero suppressed.
//
// flush() emits a synthesized line via the captured logger:
//   "[suppressed N occurrences of <file>:<line>]"
// suppressed_count is reset to zero atomically.
struct DedupState {
    std::atomic<std::uint64_t> suppressed{0};
    std::atomic<bool> initialized{false};
    Logger* logger{nullptr};
    Level level{Level::INFO};
    const char* file{nullptr};
    int line{0};

    DedupState() noexcept;

    // Returns true if caller should emit; false if suppressed.
    bool tick(Logger& logger, Level lvl, const char* file_, int line_) noexcept;

    // Emit the suppressed-summary line if suppressed > 0; reset count.
    void flush_summary() noexcept;
};

// Global "last dedup site" pointer. Reset to nullptr on flush_all_dedup.
extern std::atomic<DedupState*> g_last_dedup;

// Register the DedupState in the global registry so atexit and explicit
// flush calls can find it.
void register_dedup(DedupState* s);

// Flush all DedupStates' summaries. Called automatically at program exit
// (registered via atexit when first DedupState is constructed). Tests and
// users can also invoke directly.
void flush_all_dedup() noexcept;

}  // namespace detail

// Public-facing helper for tests / users who want to force a dedup flush.
inline void flush_dedup() noexcept { detail::flush_all_dedup(); }

}  // namespace elog
