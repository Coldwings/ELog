// Compare integer-to-chars rendering strategies on representative values.
// All routines write into a caller-owned buffer; std::to_string returns a
// std::string for fairness with the typical user code.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

// (1) Branchless single-digit naive loop.
std::size_t naive_u64(std::uint64_t v, char* out) noexcept {
    char tmp[24];
    int n = 0;
    if (v == 0) { out[0] = '0'; return 1; }
    while (v) {
        tmp[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    for (int i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    return static_cast<std::size_t>(n);
}

// (2) ELog's two-digit lookup version.
constexpr const char* kTwoDigits =
    "00010203040506070809" "10111213141516171819"
    "20212223242526272829" "30313233343536373839"
    "40414243444546474849" "50515253545556575859"
    "60616263646566676869" "70717273747576777879"
    "80818283848586878889" "90919293949596979899";

std::size_t two_digit_u64(std::uint64_t v, char* out) noexcept {
    char buf[24];
    char* end = buf + sizeof(buf);
    char* p = end;
    while (v >= 100) {
        std::uint64_t r = v % 100;
        v /= 100;
        p -= 2;
        std::memcpy(p, kTwoDigits + r * 2, 2);
    }
    if (v >= 10) {
        p -= 2;
        std::memcpy(p, kTwoDigits + v * 2, 2);
    } else {
        *--p = static_cast<char>('0' + v);
    }
    std::size_t n = static_cast<std::size_t>(end - p);
    std::memcpy(out, p, n);
    return n;
}

// (3) std::to_string (returns std::string; SSO covers all u64).
std::size_t std_tostring(std::uint64_t v, char* out) noexcept {
    std::string s = std::to_string(v);
    std::memcpy(out, s.data(), s.size());
    return s.size();
}

// (4) snprintf("%llu", v).
std::size_t snprintf_u64(std::uint64_t v, char* out) noexcept {
    int n = std::snprintf(out, 24, "%llu", static_cast<unsigned long long>(v));
    return n < 0 ? 0 : static_cast<std::size_t>(n);
}

template <class F>
double bench(const char* name, const std::vector<std::uint64_t>& xs, F fn) {
    char out[24];
    std::size_t total = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int rep = 0; rep < 50; ++rep) {
        for (auto v : xs) total += fn(v, out);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count()
              / (50.0 * static_cast<double>(xs.size()));
    std::printf("  %-22s %7.2f ns/op   (sink %zu)\n", name, ns, total);
    return ns;
}

void run_set(const char* label, const std::vector<std::uint64_t>& xs) {
    std::printf("\n[%s] %zu values\n", label, xs.size());
    bench("naive single-digit",  xs, naive_u64);
    bench("two-digit lookup",    xs, two_digit_u64);
    bench("std::to_string",      xs, std_tostring);
    bench("snprintf %llu",       xs, snprintf_u64);
}

}  // namespace

int main() {
    std::mt19937_64 rng(0xC0FFEE);

    auto gen = [&](std::uint64_t lo, std::uint64_t hi, std::size_t n) {
        std::vector<std::uint64_t> v;
        v.reserve(n);
        std::uniform_int_distribution<std::uint64_t> d(lo, hi);
        for (std::size_t i = 0; i < n; ++i) v.push_back(d(rng));
        return v;
    };

    run_set("1-digit  (0..9)",          gen(0, 9, 10000));
    run_set("3-digit  (100..999)",      gen(100, 999, 10000));
    run_set("6-digit  (1e5..1e6)",      gen(100000, 999999, 10000));
    run_set("10-digit (1e9..1e10)",     gen(1000000000ULL, 9999999999ULL, 10000));
    run_set("19-digit (near u64 max)",  gen(1000000000000000000ULL, 9999999999999999999ULL, 10000));

    // Mixed realistic distribution: log-uniform over 0..1e12.
    std::vector<std::uint64_t> mix;
    mix.reserve(10000);
    std::uniform_real_distribution<double> e(0.0, 12.0);
    for (int i = 0; i < 10000; ++i) {
        double mag = e(rng);
        std::uint64_t v = static_cast<std::uint64_t>(std::pow(10.0, mag));
        mix.push_back(v);
    }
    run_set("log-uniform 0..1e12",      mix);

    return 0;
}
