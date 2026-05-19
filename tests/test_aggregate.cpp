#include <gtest/gtest.h>

#include "elog/elog.hpp"
#include "elog/aggregate.hpp"
#include "elog/sink.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <sys/uio.h>
#include <thread>
#include <vector>

namespace {

// CapturingSink records each emit's payload (joined iovecs) so tests can
// count and inspect lines.
class CapturingSink : public elog::Sink {
public:
    void write(elog::Level, const iovec* iov, int n) override {
        std::string line;
        for (int i = 0; i < n; ++i) {
            line.append(static_cast<const char*>(iov[i].iov_base), iov[i].iov_len);
        }
        std::lock_guard<std::mutex> lk(mu_);
        lines_.push_back(std::move(line));
    }
    void flush() override {}
    std::size_t count() {
        std::lock_guard<std::mutex> lk(mu_);
        return lines_.size();
    }
    std::vector<std::string> snapshot() {
        std::lock_guard<std::mutex> lk(mu_);
        return lines_;
    }
    void clear() {
        std::lock_guard<std::mutex> lk(mu_);
        lines_.clear();
    }

private:
    std::mutex mu_;
    std::vector<std::string> lines_;
};

struct CaptureFixture {
    elog::Logger L;
    CapturingSink* sink_ptr;

    CaptureFixture() : L("agg-test") {
        L.set_level(elog::Level::TRACE);
        auto s = std::unique_ptr<CapturingSink>(new CapturingSink());
        sink_ptr = s.get();
        L.add_sink(std::move(s));
    }
};

}  // namespace

TEST(Aggregate, EveryN) {
    CaptureFixture f;
    for (int i = 0; i < 100; ++i) {
        LOGGER_EVERY_N(f.L, ::elog::Level::INFO, 10, "i={}", i);
    }
    EXPECT_EQ(f.sink_ptr->count(), 10u);
}

TEST(Aggregate, EveryNSec) {
    CaptureFixture f;
    auto start = std::chrono::steady_clock::now();
    int iters = 0;
    while (std::chrono::steady_clock::now() - start <
           std::chrono::milliseconds(100)) {
        LOGGER_EVERY_N_SEC(f.L, ::elog::Level::INFO, 1, "tight={}", iters);
        ++iters;
    }
    EXPECT_GE(iters, 1);
    EXPECT_EQ(f.sink_ptr->count(), 1u);
}

TEST(Aggregate, EveryNMs) {
    CaptureFixture f;
    // 50ms period, run for ~150ms in tight loop -> expect 2-4 emits.
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start <
           std::chrono::milliseconds(150)) {
        LOGGER_EVERY_N_MS(f.L, ::elog::Level::INFO, 50, "ms");
    }
    auto c = f.sink_ptr->count();
    EXPECT_GE(c, 2u);
    EXPECT_LE(c, 5u);
}

TEST(Aggregate, FirstN) {
    CaptureFixture f;
    for (int i = 0; i < 100; ++i) {
        LOGGER_FIRST_N(f.L, ::elog::Level::INFO, 5, "first={}", i);
    }
    EXPECT_EQ(f.sink_ptr->count(), 5u);
}

TEST(Aggregate, Once) {
    CaptureFixture f;
    for (int i = 0; i < 100; ++i) {
        LOGGER_ONCE(f.L, ::elog::Level::INFO, "once={}", i);
    }
    EXPECT_EQ(f.sink_ptr->count(), 1u);
}

TEST(Aggregate, Sampled) {
    CaptureFixture f;
    constexpr int kIters = 10000;
    for (int i = 0; i < kIters; ++i) {
        LOGGER_SAMPLED(f.L, ::elog::Level::INFO, 10, "s={}", i);
    }
    auto c = f.sink_ptr->count();
    // Expect ~1000, allow +-20%.
    EXPECT_GE(c, 800u);
    EXPECT_LE(c, 1200u);
}

TEST(Aggregate, SampledZeroAndHundred) {
    CaptureFixture f;
    for (int i = 0; i < 100; ++i) {
        LOGGER_SAMPLED(f.L, ::elog::Level::INFO, 0, "z");
    }
    EXPECT_EQ(f.sink_ptr->count(), 0u);

    for (int i = 0; i < 100; ++i) {
        LOGGER_SAMPLED(f.L, ::elog::Level::INFO, 100, "h");
    }
    EXPECT_EQ(f.sink_ptr->count(), 100u);
}

TEST(Aggregate, Burst) {
    CaptureFixture f;
    // burst=3, refill=1/sec. Five rapid-fire -> 3 emits.
    for (int i = 0; i < 5; ++i) {
        LOGGER_BURST(f.L, ::elog::Level::INFO, 3, 1.0, "b={}", i);
    }
    EXPECT_EQ(f.sink_ptr->count(), 3u);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    LOGGER_BURST(f.L, ::elog::Level::INFO, 3, 1.0, "after-refill");
    EXPECT_EQ(f.sink_ptr->count(), 4u);
}

TEST(Aggregate, DedupBasic) {
    CaptureFixture f;
    // Reset any leftover global dedup state from prior tests.
    elog::flush_dedup();
    f.sink_ptr->clear();

    for (int i = 0; i < 100; ++i) {
        LOGGER_DEDUP(f.L, ::elog::Level::INFO, "same site");
    }
    // First call passes; the rest are suppressed and pending until flush.
    EXPECT_EQ(f.sink_ptr->count(), 1u);

    elog::flush_dedup();

    auto lines = f.sink_ptr->snapshot();
    ASSERT_EQ(lines.size(), 2u);
    // Second line should be the synthesized suppressed-summary.
    EXPECT_NE(lines[1].find("[suppressed 99 occurrences"), std::string::npos)
        << "got: " << lines[1];
}

TEST(Aggregate, DedupCrossSite) {
    CaptureFixture f;
    elog::flush_dedup();
    f.sink_ptr->clear();

    for (int i = 0; i < 10; ++i) {
        LOGGER_DEDUP(f.L, ::elog::Level::INFO, "site-A");
    }
    // Hitting B should flush A's 9 suppressed before emitting B.
    LOGGER_DEDUP(f.L, ::elog::Level::INFO, "site-B");

    auto lines = f.sink_ptr->snapshot();
    // Expected order: A(emit), [suppressed 9... A], B(emit)
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_NE(lines[0].find("site-A"), std::string::npos);
    EXPECT_NE(lines[1].find("[suppressed 9 occurrences"), std::string::npos)
        << "got: " << lines[1];
    EXPECT_NE(lines[2].find("site-B"), std::string::npos);

    // Cleanup so later tests don't see B as "the last dedup site".
    elog::flush_dedup();
}

TEST(Aggregate, IfTrueEmits) {
    CaptureFixture f;
    LOGGER_IF(f.L, ::elog::Level::INFO, true, "yes");
    EXPECT_EQ(f.sink_ptr->count(), 1u);
}

TEST(Aggregate, IfFalseSuppressesAndDoesNotEvaluateArgs) {
    CaptureFixture f;
    int side_effects = 0;
    auto bump = [&]() -> int { ++side_effects; return 42; };
    for (int i = 0; i < 50; ++i) {
        LOGGER_IF(f.L, ::elog::Level::INFO, false, "arg={}", bump());
    }
    EXPECT_EQ(f.sink_ptr->count(), 0u);
    EXPECT_EQ(side_effects, 0);
}

TEST(Aggregate, LazyArgsWhenLevelOff) {
    CaptureFixture f;
    f.L.set_level(elog::Level::OFF);
    int side_effects = 0;
    auto bump = [&]() -> int { ++side_effects; return 1; };

    for (int i = 0; i < 20; ++i) {
        LOGGER_EVERY_N(f.L, ::elog::Level::INFO, 1, "x={}", bump());
        LOGGER_EVERY_N_SEC(f.L, ::elog::Level::INFO, 1, "x={}", bump());
        LOGGER_EVERY_N_MS(f.L, ::elog::Level::INFO, 1, "x={}", bump());
        LOGGER_FIRST_N(f.L, ::elog::Level::INFO, 100, "x={}", bump());
        LOGGER_ONCE(f.L, ::elog::Level::INFO, "x={}", bump());
        LOGGER_SAMPLED(f.L, ::elog::Level::INFO, 100, "x={}", bump());
        LOGGER_BURST(f.L, ::elog::Level::INFO, 10, 1.0, "x={}", bump());
        LOGGER_DEDUP(f.L, ::elog::Level::INFO, "x={}", bump());
        LOGGER_IF(f.L, ::elog::Level::INFO, true, "x={}", bump());
    }
    EXPECT_EQ(side_effects, 0);
    EXPECT_EQ(f.sink_ptr->count(), 0u);
}

TEST(Aggregate, LazyArgsWhenGateRejects) {
    CaptureFixture f;
    int side_effects = 0;
    auto bump = [&]() -> int { ++side_effects; return 1; };

    // Use FIRST_N with n=0 -> always rejects, args must not be evaluated.
    for (int i = 0; i < 50; ++i) {
        LOGGER_FIRST_N(f.L, ::elog::Level::INFO, 0, "x={}", bump());
    }
    EXPECT_EQ(side_effects, 0);
    EXPECT_EQ(f.sink_ptr->count(), 0u);

    // EVERY_N with N=10: 100 calls -> 10 emits, 90 args skipped.
    side_effects = 0;
    f.sink_ptr->clear();
    for (int i = 0; i < 100; ++i) {
        LOGGER_EVERY_N(f.L, ::elog::Level::INFO, 10, "v={}", bump());
    }
    EXPECT_EQ(side_effects, 10);
    EXPECT_EQ(f.sink_ptr->count(), 10u);
}
