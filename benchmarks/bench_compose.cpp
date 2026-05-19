// Probe the cost of single-Iov-return composites: logging a
// std::vector<std::string> currently memcpys every string into scratch
// because the outer renderer must return a single contiguous Iov. A
// multi-Iov return type would let strings stay zero-copy.

#include "elog/elog.hpp"
#include "elog/render_extra.hpp"
#include "elog/sink.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <sys/uio.h>
#include <vector>

namespace {

class NullSink : public elog::Sink {
public:
    void write(elog::Level, const iovec* iov, int n) override {
        for (int i = 0; i < n; ++i) bytes_ += iov[i].iov_len;
        ++count_;
    }
    std::uint64_t count_ = 0;
    std::uint64_t bytes_ = 0;
};

double bench(const char* name, std::size_t iters, std::function<void()> body) {
    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) body();
    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    std::printf("  %-40s %7.1f ns/op\n", name, ns);
    return ns;
}

}  // namespace

int main() {
    auto& L = elog::default_logger();
    L.clear_sinks();
    auto sink = std::unique_ptr<NullSink>(new NullSink());
    NullSink* raw = sink.get();
    L.add_sink(std::move(sink));
    L.set_level(elog::Level::INFO);

    std::vector<std::string> v3;
    v3.emplace_back("alice");
    v3.emplace_back("bob");
    v3.emplace_back("carol");

    std::vector<std::string> v3_long;
    v3_long.emplace_back(std::string(60, 'x'));
    v3_long.emplace_back(std::string(60, 'y'));
    v3_long.emplace_back(std::string(60, 'z'));

    std::vector<int> ints3 = {1, 2, 3};

    // Pre-built iov_pack views of the same content — zero-copy.
    std::vector<elog::Iov> v3_iov;
    v3_iov.push_back({"[", 1});
    v3_iov.push_back({v3[0].data(), v3[0].size()});
    v3_iov.push_back({", ", 2});
    v3_iov.push_back({v3[1].data(), v3[1].size()});
    v3_iov.push_back({", ", 2});
    v3_iov.push_back({v3[2].data(), v3[2].size()});
    v3_iov.push_back({"]", 1});

    std::vector<elog::Iov> v3_long_iov;
    v3_long_iov.push_back({"[", 1});
    v3_long_iov.push_back({v3_long[0].data(), v3_long[0].size()});
    v3_long_iov.push_back({", ", 2});
    v3_long_iov.push_back({v3_long[1].data(), v3_long[1].size()});
    v3_long_iov.push_back({", ", 2});
    v3_long_iov.push_back({v3_long[2].data(), v3_long[2].size()});
    v3_long_iov.push_back({"]", 1});

    constexpr std::size_t N = 1'000'000;
    std::printf("(emit count is checked; bytes shows total iov size)\n\n");

    // 1. Single string baseline (zero-copy reference)
    bench("LOG_INFO_F(\"u={}\", string-short)", N, [&]{
        LOG_INFO_F("u={}", v3[0]);
    });

    // 2. Vector<int> — no string involved
    bench("LOG_INFO_F(\"v={}\", vector<int>{3})", N, [&]{
        LOG_INFO_F("v={}", ints3);
    });

    // 3. Vector<short string> — currently memcpys 5+3+5 = 13 bytes
    bench("LOG_INFO_F(\"v={}\", vector<string>{3 short})", N, [&]{
        LOG_INFO_F("v={}", v3);
    });

    // 4. Vector<long string> — currently memcpys 60*3 = 180 bytes
    bench("LOG_INFO_F(\"v={}\", vector<string>{3 x 60ch})", N, [&]{
        LOG_INFO_F("v={}", v3_long);
    });

    // 5. Same content via iov_pack — should preserve zero-copy on strings
    bench("LOG_INFO_F(\"v={}\", iov_pack(3 short))", N, [&]{
        LOG_INFO_F("v={}", elog::iov_pack(v3_iov));
    });

    bench("LOG_INFO_F(\"v={}\", iov_pack(3 x 60ch))", N, [&]{
        LOG_INFO_F("v={}", elog::iov_pack(v3_long_iov));
    });

    std::printf("\nsink saw %llu lines, %llu bytes\n",
                static_cast<unsigned long long>(raw->count_),
                static_cast<unsigned long long>(raw->bytes_));
    return 0;
}
