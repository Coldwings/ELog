// Focused, repeated every-N timing to gauge measurement noise floor.

#include "elog/elog.hpp"
#include "elog/sink.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>

#ifdef ELOG_HAVE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#endif

namespace {
class NullSink : public elog::Sink {
public:
    void write(elog::Level, const iovec*, int) override {}
};

template <class F>
double timed(std::size_t iters, F&& body) {
    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) body();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
}
}

int main() {
    auto& L = elog::default_logger();
    L.clear_sinks();
    L.add_sink(std::unique_ptr<elog::Sink>(new NullSink()));
    L.set_level(elog::Level::INFO);

#ifdef ELOG_HAVE_SPDLOG
    auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("bench", sink);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
#endif

    constexpr std::size_t N = 100'000'000;
    std::printf("%-12s %-10s %-10s %-10s %-10s\n",
                "trial", "elog", "elog-via", "spdlog", "spdlog-naive");
    std::printf("---------------------------------------------------------\n");

    auto& cached = elog::default_logger();
    (void)cached;

    for (int trial = 1; trial <= 10; ++trial) {
        // (1) standard ELog macro (default_logger() resolved every call)
        double e = timed(N, [&]{ LOG_INFO_EVERY_N(100, "x={}", 1); });

        // (2) ELog macro using LOGGER_EVERY_N with a hoisted reference
        // — eliminates the per-call default_logger() magic-static check
        double e2 = timed(N, [&]{ LOGGER_EVERY_N(cached, ::elog::Level::INFO, 100, "x={}", 1); });

        // (3) hand-rolled atomic counter + spdlog::info
        double s = -1.0, sn = -1.0;
#ifdef ELOG_HAVE_SPDLOG
        s = timed(N, [&]{
            static std::atomic<std::uint64_t> ctr{0};
            if (ctr.fetch_add(1, std::memory_order_relaxed) % 100 == 0)
                spdlog::info("x={}", 1);
        });
        // (4) same hand-rolled atomic but with NO spdlog call at all —
        // pure gate cost
        sn = timed(N, [&]{
            static std::atomic<std::uint64_t> ctr{0};
            volatile bool fire = (ctr.fetch_add(1, std::memory_order_relaxed) % 100) == 0;
            (void)fire;
        });
#endif
        std::printf("trial %2d    %-10.3f %-10.3f %-10.3f %-10.3f\n",
                    trial, e, e2, s, sn);
    }
    return 0;
}
