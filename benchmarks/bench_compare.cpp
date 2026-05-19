// Apples-to-apples timing of ELog vs spdlog vs PhotonLibOS ALog.
// Each library is configured with a discard sink and INFO level so the
// measured cost is the in-process work (formatting + dispatch + sink call),
// not actual I/O.
//
// Build with -DELOG_BUILD_COMPARE=ON. Each library is fetched via CMake
// FetchContent (or referenced from third_party/) and compiled in.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

#include "elog/elog.hpp"

#ifdef ELOG_HAVE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/async.h>
#endif

#include "elog/sink.hpp"
#include "elog/buffered_file_sink.hpp"

#ifdef ELOG_HAVE_ALOG
#include <alog.h>
#endif

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

struct Result {
    const char* lib;
    const char* scenario;
    std::size_t iters;
    double ns_per_op;
};

double time_ns(std::size_t iters, std::function<void()> body) {
    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iters; ++i) body();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
}

enum class Mode { Null, Tmpfs, TmpfsBuffered };
const char* tmpfs_dir() { return "/dev/shm"; }

std::string sink_path(const char* lib) {
    return std::string(tmpfs_dir()) + "/elog_bench_" + lib + ".log";
}

void setup_elog(Mode mode) {
    auto& L = elog::default_logger();
    L.clear_sinks();
    if (mode == Mode::Null) {
        L.add_sink(std::unique_ptr<elog::Sink>(new NullSink()));
    } else if (mode == Mode::TmpfsBuffered) {
        ::unlink(sink_path("elog").c_str());
        L.add_sink(elog::make_buffered_file_sink(sink_path("elog").c_str(),
            /*buffer=*/64 * 1024, /*threshold=*/elog::Level::WARN));
    } else {
        ::unlink(sink_path("elog").c_str());
        L.add_sink(elog::make_file_sink(sink_path("elog").c_str()));
    }
    L.set_level(elog::Level::INFO);
}

void bench_elog(std::vector<Result>& out, Mode mode) {
    setup_elog(mode);
    out.push_back({"elog", "disabled",        100'000'000,
        (elog::default_logger().set_level(elog::Level::OFF), time_ns(100'000'000, [&]{
            LOG_INFO_F("hello {} world {}", 42, 3.14);
        }))});
    elog::default_logger().set_level(elog::Level::INFO);
    out.push_back({"elog", "int only",         1'000'000,
        time_ns(1'000'000, [&]{ LOG_INFO_F("a={} b={}", 42, 7); })});
    out.push_back({"elog", "double only",      1'000'000,
        time_ns(1'000'000, [&]{ LOG_INFO_F("a={} b={}", 1.5, 3.14); })});
    out.push_back({"elog", "int+double",       1'000'000,
        time_ns(1'000'000, [&]{ LOG_INFO_F("a={} b={}", 42, 3.14); })});
    out.push_back({"elog", "two strings",      1'000'000,
        time_ns(1'000'000, [&]{ LOG_INFO_F("u={} ip={}", "alice", "10.0.0.1"); })});
    out.push_back({"elog", "5 mixed args",       500'000,
        time_ns(500'000, [&]{
            LOG_INFO_F("{} {} {} {} {}", 1, "two", 3.0, true, elog::hex(0xCAFE));
        })});
    out.push_back({"elog", "every-N(100)",     1'000'000,
        time_ns(1'000'000, [&]{ LOG_INFO_EVERY_N(100, "x={}", 1); })});
}

#ifdef ELOG_HAVE_SPDLOG
void bench_spdlog(std::vector<Result>& out, Mode mode) {
    std::shared_ptr<spdlog::sinks::sink> sink;
    if (mode == Mode::Null) {
        sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    } else {
        ::unlink(sink_path("spdlog").c_str());
        sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            sink_path("spdlog"), /*truncate=*/true);
    }
    auto logger = std::make_shared<spdlog::logger>("bench", sink);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%f %^%l%$ [%t] %s:%# | %v");
    spdlog::set_level(spdlog::level::info);
    if (mode == Mode::Tmpfs) {
        // Match ELog's "every emit hits the fd" semantics. Without this,
        // spdlog batches ~40 lines per fwrite via stdio's 4KB buffer.
        spdlog::flush_on(spdlog::level::info);
    } else if (mode == Mode::TmpfsBuffered) {
        // Match ELog's buffered sink (auto-flush at WARN). Spdlog's
        // basic_file_sink already buffers via stdio; we just align the
        // critical-level flush threshold.
        spdlog::flush_on(spdlog::level::warn);
    } else {
        spdlog::flush_on(spdlog::level::off);
    }

    out.push_back({"spdlog", "disabled",        100'000'000,
        (spdlog::set_level(spdlog::level::off),
         time_ns(100'000'000, [&]{ spdlog::info("hello {} world {}", 42, 3.14); }))});
    spdlog::set_level(spdlog::level::info);
    out.push_back({"spdlog", "int only",         1'000'000,
        time_ns(1'000'000, [&]{ spdlog::info("a={} b={}", 42, 7); })});
    out.push_back({"spdlog", "double only",      1'000'000,
        time_ns(1'000'000, [&]{ spdlog::info("a={} b={}", 1.5, 3.14); })});
    out.push_back({"spdlog", "int+double",       1'000'000,
        time_ns(1'000'000, [&]{ spdlog::info("a={} b={}", 42, 3.14); })});
    out.push_back({"spdlog", "two strings",      1'000'000,
        time_ns(1'000'000, [&]{ spdlog::info("u={} ip={}", "alice", "10.0.0.1"); })});
    out.push_back({"spdlog", "5 mixed args",       500'000,
        time_ns(500'000, [&]{
            spdlog::info("{} {} {} {} {:#x}", 1, "two", 3.0, true, 0xCAFE);
        })});
    // every-N is intentionally not benchmarked for spdlog: spdlog has no
    // native rate-limit macro, comparing against a hand-rolled atomic+modulo
    // wrapper isn't apples-to-apples.
}
#endif

#ifdef ELOG_HAVE_ALOG
void bench_alog(std::vector<Result>& out, Mode mode) {
    if (mode == Mode::Null) {
        default_logger.log_output = log_output_null;
    } else {
        ::unlink(sink_path("alog").c_str());
        default_logger.log_output = new_log_output_file(sink_path("alog").c_str());
    }
    default_logger.log_level = ALOG_INFO;

    out.push_back({"alog", "disabled",        100'000'000,
        (default_logger.log_level = ALOG_FATAL,
         time_ns(100'000'000, [&]{ LOG_INFO("hello ` world `", 42, 3.14); }))});
    default_logger.log_level = ALOG_INFO;
    out.push_back({"alog", "int only",         1'000'000,
        time_ns(1'000'000, [&]{ LOG_INFO("a=` b=`", 42, 7); })});
    out.push_back({"alog", "double only",      1'000'000,
        time_ns(1'000'000, [&]{ LOG_INFO("a=` b=`", 1.5, 3.14); })});
    out.push_back({"alog", "int+double",       1'000'000,
        time_ns(1'000'000, [&]{ LOG_INFO("a=` b=`", 42, 3.14); })});
    out.push_back({"alog", "two strings",      1'000'000,
        time_ns(1'000'000, [&]{ LOG_INFO("u=` ip=`", "alice", "10.0.0.1"); })});
    out.push_back({"alog", "5 mixed args",       500'000,
        time_ns(500'000, [&]{
            LOG_INFO("` ` ` ` `", 1, "two", 3.0, true, HEX(0xCAFE));
        })});
    out.push_back({"alog", "every-N(100)",     1'000'000,
        time_ns(1'000'000, [&]{ LOG_EVERY_N(100, LOG_INFO("x=`", 1)); })});
}
#endif

// ANSI styling. Disabled when stdout is not a TTY or NO_COLOR is set.
struct Style {
    const char* bold;
    const char* dim;
    const char* green;
    const char* yellow;
    const char* gray;
    const char* reset;
};

Style make_style() {
    bool tty = ::isatty(1);
    const char* nc = std::getenv("NO_COLOR");
    if (!tty || (nc && *nc)) return Style{"", "", "", "", "", ""};
    return Style{
        "\033[1m", "\033[2m", "\033[32m", "\033[33m", "\033[90m", "\033[0m"
    };
}

std::string fmt_ns(double v) {
    char buf[32];
    if (v < 10.0)        std::snprintf(buf, sizeof(buf), "%.2f",  v);
    else if (v < 100.0)  std::snprintf(buf, sizeof(buf), "%.1f",  v);
    else                 std::snprintf(buf, sizeof(buf), "%.0f",  v);
    return buf;
}

std::string fmt_ratio(double v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2fx", v);
    return buf;
}

void pad_to(std::string& s, std::size_t w) {
    if (s.size() < w) s.append(w - s.size(), ' ');
}

void print_table(const std::vector<Result>& results) {
    Style sty = make_style();

    static const std::vector<std::string> kScenarios = {
        "disabled", "int only", "double only", "int+double",
        "two strings", "5 mixed args", "every-N(100)"
    };
    static const std::vector<std::string> kLibs = {
        "elog", "spdlog", "alog"
    };

    // Pivot: lookup[scenario][lib] = ns/op.
    std::map<std::string, std::map<std::string, double>> lookup;
    std::set<std::string> have_libs;
    for (const auto& r : results) {
        lookup[r.scenario][r.lib] = r.ns_per_op;
        have_libs.insert(r.lib);
    }

    std::vector<std::string> libs;
    for (const auto& l : kLibs) if (have_libs.count(l)) libs.push_back(l);

    constexpr std::size_t kScenW = 16;
    constexpr std::size_t kLibW  = 16;  // "ns/op (ratio)"

    std::printf("\n%s%-*s%s", sty.bold, (int)kScenW, "scenario", sty.reset);
    for (const auto& l : libs) {
        std::string head = l;
        pad_to(head, kLibW);
        std::printf("%s%s%s", sty.bold, head.c_str(), sty.reset);
    }
    std::printf("%s%s%s\n", sty.bold, "best", sty.reset);

    std::string sep_scen(kScenW, '-');
    std::printf("%s%s%s", sty.gray, sep_scen.c_str(), sty.reset);
    for (std::size_t i = 0; i < libs.size(); ++i) {
        std::string sep(kLibW, '-');
        std::printf("%s%s%s", sty.gray, sep.c_str(), sty.reset);
    }
    std::printf("%s%s%s\n", sty.gray, "----", sty.reset);

    for (const auto& scen : kScenarios) {
        auto sit = lookup.find(scen);
        if (sit == lookup.end()) continue;

        double best = 1e300;
        std::string winner;
        for (const auto& kv : sit->second) {
            if (kv.second < best) { best = kv.second; winner = kv.first; }
        }

        std::string head = scen;
        pad_to(head, kScenW);
        std::printf("%s", head.c_str());

        for (const auto& l : libs) {
            auto vit = sit->second.find(l);
            std::string cell;
            if (vit == sit->second.end()) {
                cell = "—";
            } else {
                std::string ns = fmt_ns(vit->second);
                std::string ratio = (l == winner)
                    ? std::string("")
                    : "(" + fmt_ratio(vit->second / best) + ")";
                if (l == winner) {
                    cell = ns + " ★";
                } else {
                    cell = ns + " " + ratio;
                }
            }
            const char* color = "";
            if (vit != sit->second.end()) {
                if (l == winner) color = sty.green;
                else if (vit->second > best * 1.5) color = sty.yellow;
            }
            pad_to(cell, kLibW);
            std::printf("%s%s%s", color, cell.c_str(), sty.reset);
        }
        std::printf("%s%s%s\n", sty.dim, winner.c_str(), sty.reset);
    }
    std::printf("\n%slegend: ★ = fastest in row; (Nx) = ratio vs row best%s\n",
                sty.dim, sty.reset);
}

}  // namespace

int main(int argc, char** argv) {
    std::string only;
    bool run_null = true;
    bool run_tmpfs = true;
    bool run_tmpfs_buf = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "null")           { run_null = true;  run_tmpfs = false; run_tmpfs_buf = false; }
        else if (a == "tmpfs")     { run_null = false; run_tmpfs = true;  run_tmpfs_buf = false; }
        else if (a == "buffered")  { run_null = false; run_tmpfs = false; run_tmpfs_buf = true; }
        else if (a == "all")       { run_null = true;  run_tmpfs = true;  run_tmpfs_buf = true; }
        else                       { only = a; }
    }

    auto run = [&](Mode mode) {
        std::vector<Result> results;
        if (only.empty() || only == "elog")   bench_elog(results, mode);
#ifdef ELOG_HAVE_SPDLOG
        if (only.empty() || only == "spdlog") bench_spdlog(results, mode);
#endif
#ifdef ELOG_HAVE_ALOG
        // ALog has no buffered file sink; skip it in TmpfsBuffered to avoid
        // an apples-to-pears comparison.
        if (mode != Mode::TmpfsBuffered &&
            (only.empty() || only == "alog")) {
            bench_alog(results, mode);
        }
#endif
        return results;
    };

    Style sty = make_style();
    if (run_null) {
        std::printf("\n%s== sink: NullSink (consume iov, no I/O) ==%s\n",
                    sty.bold, sty.reset);
        print_table(run(Mode::Null));
    }
    if (run_tmpfs) {
        std::printf("\n%s== sink: tmpfs file, every emit flushed ==%s\n",
                    sty.bold, sty.reset);
        std::printf("%selog/alog: unbuffered writev per emit (their default)%s\n",
                    sty.dim, sty.reset);
        std::printf("%sspdlog:    forced flush_on(info) to neutralize stdio's 4 KB batch%s\n",
                    sty.dim, sty.reset);
        std::printf("%sso each LOG_* survives a process crash; bytes are in kernel before return%s\n",
                    sty.dim, sty.reset);
        print_table(run(Mode::Tmpfs));
    }
    if (run_tmpfs_buf) {
        std::printf("\n%s== sink: tmpfs file, INFO/DEBUG buffered, WARN+ flushed ==%s\n",
                    sty.bold, sty.reset);
        std::printf("%selog:   make_buffered_file_sink(64KB, threshold=WARN)%s\n",
                    sty.dim, sty.reset);
        std::printf("%sspdlog: basic_file_sink + flush_on(warn)%s\n",
                    sty.dim, sty.reset);
        std::printf("%salog:   omitted — no buffered file sink%s\n",
                    sty.dim, sty.reset);
        std::printf("%scaveat: INFO/DEBUG bytes batched in user-space — lost on SIGSEGV%s\n",
                    sty.yellow, sty.reset);
        print_table(run(Mode::TmpfsBuffered));
    }
    if (run_tmpfs || run_tmpfs_buf) {
        ::unlink(sink_path("elog").c_str());
        ::unlink(sink_path("spdlog").c_str());
        ::unlink(sink_path("alog").c_str());
    }
    return 0;
}
