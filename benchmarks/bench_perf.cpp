// Tight scenarios for `perf stat -- ./bench_perf <scenario> <iters>`.
// Each scenario is a clean loop with no branching except the LOG itself,
// so cycles/IPC/branch-miss readings reflect the logging cost directly.
//
// Usage:
//   ./bench_perf disabled         100000000
//   ./bench_perf enabled_int       10000000
//   ./bench_perf enabled_string    10000000
//   ./bench_perf enabled_mixed5     5000000
//   ./bench_perf every_n_100       50000000
//   ./bench_perf if_false         100000000
//   ./bench_perf rotating          1000000
//   ./bench_perf devnull_writev    1000000

#include "elog/elog.hpp"
#include "elog/rotating_sink.hpp"
#include "elog/sink.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/uio.h>
#include <unistd.h>

namespace {

class NullSink : public elog::Sink {
public:
    void write(elog::Level, const iovec* iov, int n) override {
        for (int i = 0; i < n; ++i) sum_ += iov[i].iov_len;
    }
    std::size_t sum() const noexcept { return sum_; }
private:
    std::size_t sum_ = 0;
};

class DevNullWritev : public elog::Sink {
public:
    DevNullWritev() : fd_(::open("/dev/null", O_WRONLY)) {}
    ~DevNullWritev() override { if (fd_ >= 0) ::close(fd_); }
    void write(elog::Level, const iovec* iov, int n) override {
        ssize_t w = ::writev(fd_, iov, n);
        (void)w;
    }
private:
    int fd_;
};

__attribute__((noinline)) void run_disabled(std::size_t iters) {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::OFF);
    int x = 42;
    double y = 3.14;
    for (std::size_t i = 0; i < iters; ++i) {
        LOG_INFO_F("hello {} world {}", x, y);
    }
}

__attribute__((noinline)) void run_enabled_int(std::size_t iters) {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::INFO);
    for (std::size_t i = 0; i < iters; ++i) {
        LOG_INFO_F("counter {} value {}", i, i * 31);
    }
}

__attribute__((noinline)) void run_enabled_string(std::size_t iters) {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::INFO);
    const char* user = "alice";
    const char* ip = "10.0.0.1";
    for (std::size_t i = 0; i < iters; ++i) {
        LOG_INFO_F("user {} from {}", user, ip);
    }
}

__attribute__((noinline)) void run_enabled_mixed5(std::size_t iters) {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::INFO);
    for (std::size_t i = 0; i < iters; ++i) {
        LOG_INFO_F("a={} b={} c={} d={} e={}",
                   1, "two", 3.0, true, elog::hex(0xCAFE));
    }
}

__attribute__((noinline)) void run_every_n_100(std::size_t iters) {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::INFO);
    for (std::size_t i = 0; i < iters; ++i) {
        LOG_INFO_EVERY_N(100, "every 100th {}", i);
    }
}

__attribute__((noinline)) void run_if_false(std::size_t iters) {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::INFO);
    for (std::size_t i = 0; i < iters; ++i) {
        LOG_INFO_IF(false, "should never emit {}", i);
    }
}

__attribute__((noinline)) void run_rotating(std::size_t iters) {
    auto& L = elog::default_logger();
    L.clear_sinks();
    L.add_sink(elog::make_rotating_file_sink("/tmp/elog_perf.log",
                                             1 << 20, 4));
    L.set_level(elog::Level::INFO);
    for (std::size_t i = 0; i < iters; ++i) {
        LOG_INFO_F("rot {}", i);
    }
    L.flush();
    ::unlink("/tmp/elog_perf.log");
    ::unlink("/tmp/elog_perf.1.log");
    ::unlink("/tmp/elog_perf.2.log");
    ::unlink("/tmp/elog_perf.3.log");
}

__attribute__((noinline)) void run_devnull_writev(std::size_t iters) {
    auto& L = elog::default_logger();
    L.clear_sinks();
    L.add_sink(std::unique_ptr<elog::Sink>(new DevNullWritev()));
    L.set_level(elog::Level::INFO);
    for (std::size_t i = 0; i < iters; ++i) {
        LOG_INFO_F("devnull {}", i);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: %s <scenario> <iters>\n"
            "scenarios: disabled enabled_int enabled_string enabled_mixed5\n"
            "           every_n_100 if_false rotating devnull_writev\n",
            argv[0]);
        return 2;
    }

    auto& L = elog::default_logger();
    L.clear_sinks();
    L.add_sink(std::unique_ptr<NullSink>(new NullSink()));

    std::string s = argv[1];
    std::size_t n = std::strtoull(argv[2], nullptr, 10);

    if (s == "disabled")        run_disabled(n);
    else if (s == "enabled_int")     run_enabled_int(n);
    else if (s == "enabled_string")  run_enabled_string(n);
    else if (s == "enabled_mixed5")  run_enabled_mixed5(n);
    else if (s == "every_n_100")     run_every_n_100(n);
    else if (s == "if_false")        run_if_false(n);
    else if (s == "rotating")        run_rotating(n);
    else if (s == "devnull_writev")  run_devnull_writev(n);
    else {
        std::fprintf(stderr, "unknown scenario: %s\n", s.c_str());
        return 2;
    }
    return 0;
}
