#include "elog/elog.hpp"
#include "elog/sink.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

class NullSink : public elog::Sink {
public:
    void write(elog::Level, const iovec* iov, int n) override {
        for (int i = 0; i < n; ++i) bytes_ += iov[i].iov_len;
        ++count_;
    }
    std::uint64_t count() const noexcept { return count_; }
    std::uint64_t bytes() const noexcept { return bytes_; }
private:
    std::uint64_t count_ = 0;
    std::uint64_t bytes_ = 0;
};

double bench_one(const char* name, std::size_t iters, std::function<void()> body) {
    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) body();
    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    std::printf("%-40s %10zu iters %10.1f ns/op\n", name, iters, ns);
    return ns;
}

}  // namespace

int main() {
    auto& L = elog::default_logger();
    L.clear_sinks();
    auto null_owned = std::unique_ptr<NullSink>(new NullSink());
    NullSink* null_raw = null_owned.get();
    L.add_sink(std::move(null_owned));

    L.set_level(elog::Level::OFF);
    bench_one("disabled (level OFF)", 10'000'000, [&] {
        LOG_INFO_F("hello {} world {}", 42, 3.14);
    });

    L.set_level(elog::Level::INFO);
    bench_one("enabled int + double", 1'000'000, [&] {
        LOG_INFO_F("hello {} world {}", 42, 3.14);
    });

    bench_one("enabled string", 1'000'000, [&] {
        LOG_INFO_F("user {} from {}", "alice", "10.0.0.1");
    });

    bench_one("enabled 5 mixed args", 500'000, [&] {
        LOG_INFO_F("a={} b={} c={} d={} e={}",
                   1, "two", 3.0, true, elog::hex(0xCAFE));
    });

    bench_one("EVERY_N(100)", 1'000'000, [&] {
        LOG_INFO_EVERY_N(100, "every 100th {}", 1);
    });

    int n_threads = 4;
    std::size_t per_thread = 250'000;
    std::vector<std::thread> ts;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n_threads; ++i) {
        ts.emplace_back([&, i] {
            for (std::size_t k = 0; k < per_thread; ++k) {
                LOG_INFO_F("t{} k={}", i, k);
            }
        });
    }
    for (auto& t : ts) t.join();
    auto t1 = std::chrono::steady_clock::now();
    double total = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double ns_op = total / (n_threads * per_thread);
    std::printf("%-40s %10zu total  %10.1f ns/op (4 threads)\n",
                "concurrent emit", n_threads * per_thread, ns_op);

    std::printf("\nsink saw %llu lines, %llu bytes\n",
                static_cast<unsigned long long>(null_raw->count()),
                static_cast<unsigned long long>(null_raw->bytes()));
    return 0;
}
