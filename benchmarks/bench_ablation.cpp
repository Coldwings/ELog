// Ablation benchmark to find where ELog's per-emit time goes.
// Each scenario adds one piece of work to the prior one, so the
// delta tells you that piece's cost.

#include "elog/elog.hpp"
#include "elog/sink.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <sys/uio.h>
#include <thread>

namespace {

class NullSink : public elog::Sink {
public:
    void write(elog::Level, const iovec* iov, int n) override {
        for (int i = 0; i < n; ++i) sum_ += iov[i].iov_len;
    }
    std::size_t sum_ = 0;
};

double bench(const char* name, std::size_t iters, std::function<void()> body) {
    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) body();
    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    std::printf("%-44s %10.1f ns/op\n", name, ns);
    return ns;
}

void noop_prologue(char*, std::size_t&, elog::Iov& out, const elog::LogCtx&) {
    static const char k[] = "X";
    out = elog::Iov{k, 1};
}

}  // namespace

int main() {
    auto& L = elog::default_logger();
    L.clear_sinks();
    L.add_sink(std::unique_ptr<elog::Sink>(new NullSink()));
    L.set_level(elog::Level::INFO);

    std::printf("--- baseline: full prologue, 2 int args ---\n");
    bench("LOG_INFO_F (2 ints)",            5'000'000, [&]{
        LOG_INFO_F("a={} b={}", 42, 7);
    });

    std::printf("--- ablation: noop prologue (no time/tid/file) ---\n");
    L.set_prologue(&noop_prologue);
    bench("LOG_INFO_F (2 ints, noop prologue)", 5'000'000, [&]{
        LOG_INFO_F("a={} b={}", 42, 7);
    });

    std::printf("--- single int ---\n");
    bench("LOG_INFO_F (1 int, noop prologue)", 5'000'000, [&]{
        LOG_INFO_F("a={}", 42);
    });

    std::printf("--- zero args ---\n");
    bench("LOG_INFO_F (0 args, noop prologue)", 5'000'000, [&]{
        LOG_INFO_F("plain");
    });

    std::printf("--- direct emit (skip macros entirely) ---\n");
    bench("Logger::emit (1 iov)", 5'000'000, [&]{
        const char body[] = "x\n";
        iovec iov{const_cast<char*>(body), sizeof(body) - 1};
        L.emit(elog::Level::INFO, &iov, 1);
    });

    std::printf("--- raw clock cost ---\n");
    bench("now() syscall only",                  5'000'000, [&]{
        auto v = std::chrono::system_clock::now();
        (void)v;
    });

    bench("get_id() only",                       5'000'000, [&]{
        auto v = std::this_thread::get_id();
        (void)v;
    });

    return 0;
}
