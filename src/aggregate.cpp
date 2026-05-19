#include "elog/aggregate.hpp"

#include "elog/emit.hpp"
#include "elog/format.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <random>
#include <vector>

namespace elog {
namespace detail {

// ---------------- EveryTimeState ----------------
bool EveryTimeState::tick(std::int64_t period_ns) noexcept {
    using clock = std::chrono::steady_clock;
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   clock::now().time_since_epoch())
                   .count();

    std::int64_t expected = next_ns.load(std::memory_order_acquire);
    for (;;) {
        if (now < expected) return false;
        std::int64_t desired = now + period_ns;
        if (next_ns.compare_exchange_weak(
                expected, desired,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return true;
        }
        // expected was updated by CAS; re-check loop condition.
    }
}

// ---------------- SampledState ----------------
namespace {

struct Xorshift64 {
    std::uint64_t s;

    explicit Xorshift64(std::uint64_t seed) noexcept
        : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    std::uint64_t next() noexcept {
        std::uint64_t x = s;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        s = x;
        return x;
    }
};

Xorshift64& tls_rng() noexcept {
    thread_local Xorshift64 rng([] {
        std::random_device rd;
        std::uint64_t seed = (static_cast<std::uint64_t>(rd()) << 32) ^
                             static_cast<std::uint64_t>(rd());
        return seed;
    }());
    return rng;
}

}  // namespace

bool SampledState::tick(std::uint32_t prob_percent) noexcept {
    if (prob_percent == 0) return false;
    if (prob_percent >= 100) return true;
    std::uint64_t r = tls_rng().next();
    return (r % 100u) < prob_percent;
}

// ---------------- BurstState ----------------
bool BurstState::tick(std::uint32_t burst, double refill_per_sec) noexcept {
    using clock = std::chrono::steady_clock;
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   clock::now().time_since_epoch())
                   .count();
    std::lock_guard<std::mutex> lock(mu);
    if (!initialized) {
        tokens = static_cast<double>(burst);
        last_ns = now;
        initialized = true;
    } else {
        double elapsed = static_cast<double>(now - last_ns) / 1e9;
        if (elapsed > 0.0) {
            tokens += elapsed * refill_per_sec;
            if (tokens > static_cast<double>(burst)) {
                tokens = static_cast<double>(burst);
            }
            last_ns = now;
        }
    }
    if (tokens >= 1.0) {
        tokens -= 1.0;
        return true;
    }
    return false;
}

// ---------------- DedupState / registry ----------------

std::atomic<DedupState*> g_last_dedup{nullptr};

namespace {

struct DedupRegistry {
    std::mutex mu;
    std::vector<DedupState*> states;

    void add(DedupState* s) {
        std::lock_guard<std::mutex> lock(mu);
        states.push_back(s);
    }

    void flush_all() {
        std::lock_guard<std::mutex> lock(mu);
        for (DedupState* s : states) {
            s->flush_summary();
        }
        // Clear last-pointer so subsequent calls re-emit instead of suppressing.
        g_last_dedup.store(nullptr, std::memory_order_release);
    }
};

DedupRegistry& dedup_registry() noexcept {
    static DedupRegistry r;
    return r;
}

std::once_flag g_atexit_once;

void ensure_atexit_installed() {
    std::call_once(g_atexit_once, [] {
        std::atexit([] {
            dedup_registry().flush_all();
        });
    });
}

}  // namespace

void register_dedup(DedupState* s) {
    dedup_registry().add(s);
    ensure_atexit_installed();
}

void flush_all_dedup() noexcept {
    dedup_registry().flush_all();
}

DedupState::DedupState() noexcept {
    register_dedup(this);
}

bool DedupState::tick(Logger& l, Level lvl, const char* f, int ln) noexcept {
    // Initialize identity once. Multiple threads racing here all write the
    // same values (same call-site), so a relaxed CAS-on-bool gate is enough
    // to ensure registry consistency.
    if (!initialized.load(std::memory_order_acquire)) {
        bool expected = false;
        if (initialized.compare_exchange_strong(
                expected, true,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            logger = &l;
            level = lvl;
            file = f;
            line = ln;
            // publish: set initialized=true happened above; readers gated on
            // initialized.load(acquire) see these stores via the acq_rel cas.
        }
        // Even if we lost the race, the winner has populated fields; spin not
        // required because subsequent `==this` CAS against g_last_dedup is
        // independent.
    }

    DedupState* cur = g_last_dedup.load(std::memory_order_acquire);
    if (cur == this) {
        suppressed.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    DedupState* prev = g_last_dedup.exchange(this, std::memory_order_acq_rel);
    if (prev != nullptr && prev != this) {
        prev->flush_summary();
    }
    return true;
}

void DedupState::flush_summary() noexcept {
    std::uint64_t n = suppressed.exchange(0, std::memory_order_acq_rel);
    if (n == 0) return;
    if (logger == nullptr || file == nullptr) return;
    if (!logger->enabled(level)) return;

    static constexpr const char kFmt[] =
        "[suppressed {} occurrences of {}:{}]";
    static constexpr auto spec = ::elog::make_spec<
        ::elog::count_pieces(kFmt),
        ::elog::count_lit_chars(kFmt)>(kFmt);
    ::elog::detail::emit_f(*logger, level, file, line, spec,
                           static_cast<unsigned long long>(n), file, line);
}

}  // namespace detail
}  // namespace elog
