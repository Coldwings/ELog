# ELog

A high-performance C++14 logging library for POSIX, designed around four
ideas:

1. **`writev`-driven zero-copy.** Static format pieces, string-like args, ANSI
   escapes, and the trailing newline are passed straight to the kernel as
   `iovec` entries. Only numeric / custom args get rendered into a per-thread
   scratch buffer.
2. **Compile-time format parsing.** The format string is parsed once per
   call-site into a `static constexpr` piece table — no runtime scanning of
   the literal, no allocations on the emit path.
3. **Lazy evaluation.** When a level or rate-limit gate is closed, the
   argument expressions are not evaluated. Disabled call sites compile down
   to `load + cmp + jne`.
4. **Lock-free emit.** Linux serializes per-fd `writev` at the inode level,
   so the default synchronous sinks need no userspace mutex on the hot
   path. The tie path takes a mutex only when at least one logger is
   actually tied (atomic counter fast path). Mutexes only appear in:
   registry mutation, file rotation boundary, the optional buffered file
   sink, and tie fan-out when ties exist.

C++14 standard only — no `if constexpr`, no fold expressions, no
`std::string_view`, no UDL extensions.

## Quick start

```cpp
#include "elog/elog.hpp"

int main() {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::INFO);

    LOG_INFO_F("user {} logged in from {}", "alice", "10.0.0.1");
    LOG_WARN_F("retry {}/{} after {}ms", 3, 5, 250);
    LOG_DEBUG_F("never evaluated when level < DEBUG: {}", expensive_call());
}
```

Output:

```
2026-05-19 13:30:15.421033 INFO [12345] foo.cpp:7 | user alice logged in from 10.0.0.1
2026-05-19 13:30:15.421054 WARN [12345] foo.cpp:8 | retry 3/5 after 250ms
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

CMake options:

| option | default | what it does |
|---|---|---|
| `ELOG_BUILD_TESTS` | ON | build gtest suite (96 cases) |
| `ELOG_BUILD_EXAMPLES` | ON | build the 5 programs in `examples/` |
| `ELOG_BUILD_BENCHMARKS` | ON | build the in-tree benchmarks |
| `ELOG_BUILD_COMPARE` | OFF | also build `bench_compare` (needs `third_party/spdlog` and `third_party/PhotonLibOS`) |
| `ELOG_LTO` | OFF | enable link-time optimization |

Targets exposed: `elog::elog` (static lib). Requires CMake ≥ 3.14, GCC 7+ /
Clang 7+, Linux/POSIX.

## Levels and macros

Levels: `TRACE < DEBUG < INFO < WARN < ERROR < FATAL < OFF`.

| Family | Macro |
|---|---|
| basic | `LOG_INFO_F(fmt, ...)` |
| explicit logger | `LOGGER_F(my_logger, elog::Level::INFO, fmt, ...)` |
| rate by count | `LOG_INFO_EVERY_N(n, fmt, ...)` |
| rate by time | `LOG_INFO_EVERY_N_SEC(s, ...)`, `LOG_INFO_EVERY_N_MS(ms, ...)` |
| first N | `LOG_INFO_FIRST_N(n, ...)`, `LOG_INFO_ONCE(...)` |
| sampled | `LOG_INFO_SAMPLED(prob_percent, ...)` |
| token bucket | `LOG_INFO_BURST(burst, refill_per_sec, ...)` |
| dedup | `LOG_INFO_DEDUP(...)` (consecutive same call-site collapsed into a summary) |
| conditional | `LOG_INFO_IF(cond, ...)` |

Each variant has the six level forms (`TRACE/DEBUG/INFO/WARN/ERROR/FATAL`)
and a `LOGGER_*` form taking an explicit logger.

## Format placeholders

Single token: `{}`. Adjacent literal `{{` and `}}` produce literal `{`/`}`.
No `:spec` syntax — formatting is expressed by wrapper types so it is
extensible:

```cpp
LOG_INFO_F("{} dec, {} hex, {} bin, {} oct",         42, elog::hex(42), elog::bin(42), elog::oct(42));
LOG_INFO_F("{} fixed-3, {} padded",                  elog::fixed(3.14159, 3), elog::pad_left(7, 4, '0'));
LOG_INFO_F("{} quoted, {} escaped",                  elog::quoted("hi"), elog::escaped("a\nb"));
LOG_INFO_F("hex dump = {}",                          elog::hexdump(buf, n));
LOG_INFO_F("vector = {}",                            std::vector<int>{1,2,3});  // [1, 2, 3]
LOG_INFO_F("map = {}",                               std::map<std::string,int>{{"a",1},{"b",2}});
LOG_INFO_F("joined = {}",                            elog::join(items, ", "));
```

## Composite zero-copy via `iov_pack`

When you have N segments (some borrowed from existing buffers, some owned)
that you want to log as one logical value with zero-copy preserved
end-to-end, build a list of `Iov` and wrap it with `elog::iov_pack`. ELog
splices the entries directly into the per-call iovec — no memcpy, no
intermediate buffer.

```cpp
std::vector<elog::Iov> pieces;
pieces.push_back({"key=", 4});
pieces.push_back({k.data(), k.size()});       // borrowed (zero-copy)
pieces.push_back({" val=", 5});
pieces.push_back({v.data(), v.size()});       // borrowed (zero-copy)
LOG_INFO_F("entry: {}", elog::iov_pack(pieces));
```

The pack itself is just a `{base, count}` view; it does not own the iov
entries. They must outlive the LOG call (same lifetime rule as string args
borrowed via `Iov`). The dispatch is fully compile-time: a LOG call with no
`iov_pack` argument compiles to the original fast path with zero overhead;
LOG calls with a pack get a slightly different code path that handles
variable-length iov expansion.

`iov_pack` constructs from `std::vector<Iov>`, `std::array<Iov, N>`, or a C
array of `Iov`. Up to 32 entries per pack per LOG call.

## Custom types

Provide a free function `elog_render` in your type's namespace:

```cpp
namespace geom {
struct Point { int x, y; };

inline elog::Iov elog_render(char* scratch, std::size_t& pos,
                             const Point& p) noexcept {
    using elog::elog_render;
    std::size_t start = pos;
    if (pos < 4096) scratch[pos++] = '(';
    elog::Iov a = elog_render(scratch, pos, p.x);
    /* … append ", " … */
    elog::Iov b = elog_render(scratch, pos, p.y);
    if (pos < 4096) scratch[pos++] = ')';
    return elog::Iov{scratch + start, pos - start};
}
}
```

ADL picks it up at the call site. See `examples/custom_type.cpp`.

## Sinks

```cpp
auto& L = elog::default_logger();
L.add_sink(elog::make_stderr_sink(/*colored=*/true));
L.add_sink(elog::make_stdout_sink(/*colored=*/false));
L.add_sink(elog::make_file_sink("app.log"));
L.add_sink(elog::make_rotating_file_sink("app.log", /*max_bytes=*/64<<20, /*max_files=*/5));

// Opt-in buffered file sink. INFO/DEBUG batched, WARN+ flushes synchronously.
L.add_sink(elog::make_buffered_file_sink("hot.log",
    /*buffer_bytes=*/64*1024,
    /*flush_threshold=*/elog::Level::WARN));
```

### No user-space buffering by default. By design.

The default file sink (`make_file_sink`) issues one `writev()` syscall per
emit. Once `LOG_*` returns, the bytes are in the kernel — surviving any
user-space crash. The page cache itself only loses data if the kernel panics
or the host loses power.

Userland buffering (the default for `std::FILE*` / spdlog's
`basic_file_sink`) trades crash-time log integrity for syscall amortization.
When a process SIGSEGVs the in-flight buffer is gone — and **the few seconds
before the crash are often the only logs that matter**.

`make_buffered_file_sink` exists as an explicit opt-in for high-rate
INFO/DEBUG paths where individual lines are losable. It mirrors glog's and
spdlog's `flush_on(WARN)` policy: WARN/ERROR/FATAL still flush synchronously
through the buffer so anything diagnostically important still survives a
crash. The destructor flushes too, so an orderly shutdown drops nothing.

### `Sink` contract

```cpp
class Sink {
public:
    virtual void write(Level, const iovec* iov, int n) = 0;
    virtual void flush() {}
};
```

The iov entries handed to `write()` may point into the calling thread's
TLS scratch buffer. **Implementations must consume them synchronously and
must not retain pointers past return.** An async wrapper has to flatten
the payload into its own storage before queueing.

`include/elog/sink_async.hpp` ships two helpers for that:

```cpp
std::size_t elog::iov_total_bytes(const iovec*, int n) noexcept;
std::size_t elog::iov_serialize(const iovec*, int n, char* out) noexcept;
```

A working AsyncSink wrapper is in `examples/async_sink_skeleton.cpp` —
copy and adapt to your queue / drop policy.

## Multi-logger and tie

```cpp
auto& app   = elog::get_logger("app");
auto& audit = elog::get_logger("audit");
app.add_sink(elog::make_stderr_sink(true));
audit.add_sink(elog::make_rotating_file_sink("audit.log", 64<<20, 8));

app.tie(audit);  // every emit on `app` is also forwarded to `audit`
```

Tie is fan-out, not aliasing: forwarded emits ignore the tied logger's
level (think syslog forwarding). Cycles are broken by a per-thread
visited set, so `A.tie(B); B.tie(A)` is safe.

## Custom prologue

```cpp
elog::default_logger().set_prologue([](char* scratch, std::size_t& pos,
                                       elog::Iov& out, const elog::LogCtx& ctx) {
    std::size_t start = pos;
    /* write whatever you want into scratch + pos; advance pos */
    out = elog::Iov{scratch + start, pos - start};
});
```

The default writes `YYYY-MM-DD HH:MM:SS.uuuuuu LEVEL [tid] file:line | `
(microsecond precision) and caches the second-resolution portion in TLS so
only the microsecond suffix is re-rendered each call. The Linux thread id
(via `gettid`) is also TLS-cached.

Time source is `std::chrono::system_clock::now()` (`CLOCK_REALTIME`). It is
**wall-clock**, not monotonic — subject to NTP step adjustments, manual
`date -s`, and leap seconds. Slew (≤ ±500 ppm) is generally invisible; step
events are rare in steady state. If you need strictly monotonic timestamps
in log lines, install a custom prologue using `CLOCK_MONOTONIC`.

## Aggregation policy primer

`LOG_*_DEDUP` collapses consecutive same-call-site emits into a single
line followed by a synthesized `[suppressed N occurrences of file:line]`
summary. The summary is flushed when:

- a different call-site emits (cross-site flush),
- the user calls `elog::flush_dedup()`,
- `atexit` runs at program shutdown.

`LOG_*_EVERY_N_SEC` uses a `compare_exchange` loop on a per-call-site
`std::atomic<int64_t> next_emit_ns`, so racing threads emit at most one
per window without a mutex.

## Performance

The hot path for an enabled call boils down to:

1. atomic level load + branch
2. (optional) atomic gate tick (every-N / sample / etc.)
3. per-arg render into TLS scratch (or borrow source pointer for strings)
4. one `writev(fd, iov, n)`

No userspace mutex on stderr / stdout / file sinks. The tie path uses a
`tie_count_` atomic fast path: when no other loggers are tied (the common
case), `emit()` skips the visited-set bookkeeping entirely. Rotation takes
a sink-local mutex only when a write would push the file past `max_bytes`.
The buffered file sink takes its own mutex for the user-space buffer.

Disabled call sites (`L.set_level(OFF)` or guarded by `LOG_*_IF(false, ...)`)
do not evaluate their argument expressions and compile down to gate-load
plus branch.

### Snapshot vs spdlog 1.14 and PhotonLibOS ALog

Median of repeated runs on aarch64 Linux (Release, default build), each
library configured to emit equivalent content. Three I/O modes — pick the
row that matches your actual setup. All numbers ns/op.

```
== sink: NullSink (no I/O — pure dispatch + format) ==
scenario        elog            spdlog          alog
disabled        1.0             2.7             0.8 ★
int only        45              43 ★            78
double only     73 ★            101             192
int+double      60 ★            81              139
two strings     39 ★            85              80
5 mixed args    65 ★            136             159
every-N(100)    2.5             —               2.5 ★

== sink: tmpfs file, every emit flushed (durable) ==
scenario        elog            spdlog          alog
disabled        1.1             3.3             0.9 ★
int only        231 ★          346             305
double only     275 ★          394             406
int+double      273 ★          416             360
two strings     235 ★          376             264
5 mixed args    289 ★          431             344
every-N(100)    4.2 ★          —               5.6

== sink: tmpfs, INFO/DEBUG buffered, WARN+ flushed (throughput) ==
scenario        elog            spdlog
disabled        1.0 ★          2.8
int only        78 ★           140
double only     105 ★          199
int+double      99 ★           175
two strings     75 ★           182
5 mixed args    112 ★          233
every-N(100)    2.8 ★          —
```

Notes on fairness:

- **`spdlog` in the durable row** is forced into `spdlog::flush_on(info)`
  to match ELog/ALog's per-emit writev contract. Without this, spdlog's
  default `basic_file_sink` batches ~40 lines per fwrite via stdio's 4 KB
  buffer — fast, but loses recent lines on a process crash.
- **`every-N(100)`** is shown for ELog and ALog (both have native rate-limit
  macros). spdlog has no native equivalent — comparing against a hand-rolled
  `static atomic + modulo` wrapper isn't the same thing.
- **The buffered row omits ALog**: ALog has no buffered file sink — the
  comparison is meant to show the cost of explicit user-space buffering at
  parity, between libraries that offer it.

Run yourself with `./build/benchmarks/bench_compare` after configuring with
`-DELOG_BUILD_COMPARE=ON` (FetchContent will pull spdlog v1.14.1 and
PhotonLibOS HEAD into the build directory). The runtime output is a colored
side-by-side ratio table.

### Other benchmark binaries

- `bench_basic` — quick wall-clock ns/op measurement of a few core scenarios
- `bench_perf` — `__attribute__((noinline))` scenarios designed to be driven
  by `perf stat`. See `benchmarks/run_perf.sh`.
- `bench_ablation` — adds one piece of work at a time so the per-stage cost
  can be read off the deltas
- `bench_everyn` — focused noise-floor study of the every-N gate

## Layout

```
include/elog/             # public headers
  elog.hpp                # umbrella
  iov.hpp                 # struct Iov { base, len }
  iov_pack.hpp            # composite zero-copy wrapper for N iov segments
  level.hpp
  string_ref.hpp          # minimal {ptr, len}
  scratch.hpp             # thread_local 4 KiB
  render.hpp              # built-in elog_render overloads + format wrappers
  render_extra.hpp        # containers + padding + hexdump + escaped + nullable + join
  grisu2.hpp              # double → shortest decimal
  sink.hpp                # Sink + factories
  sink_async.hpp          # iov_total_bytes / iov_serialize helpers (for user-built async wrappers)
  buffered_file_sink.hpp  # opt-in user-space buffered file sink, WARN+ flushes synchronously
  rotating_sink.hpp
  prologue.hpp
  logger.hpp              # Logger + tie
  registry.hpp            # named loggers
  format.hpp              # FmtSpec + count_pieces + make_spec
  emit.hpp                # detail::emit_f (the iovec assembly point)
  aggregate.hpp           # per-call-site state structs
  macros.hpp              # LOG_*, LOG_*_F, LOG_*_EVERY_N, ...
src/                      # implementation .cpp
tests/                    # gtest, 96 cases
examples/                 # basic, multi_logger, aggregation, custom_type, async_sink_skeleton
benchmarks/               # bench_basic, bench_perf, bench_ablation, bench_everyn, bench_compare
```

`bench_compare` fetches spdlog and PhotonLibOS via CMake `FetchContent` at
configure time — they are not vendored in the source tree.

## What ELog does not provide

- A built-in async sink. `examples/async_sink_skeleton.cpp` is a 90-line
  reference SPMC-ish wrapper you can copy and adapt.
- `printf`-style `%s/%d` placeholders. Use `{}` plus typed wrappers
  (`hex(x)`, `fixed(d, prec)`, `pad_left(v, width)`, …) for formatting
  control.
- Windows / non-POSIX support. Hard dependency on `writev`, `gettid`,
  `clock_gettime`.
- Structured logging / JSON output. Write a custom sink — the
  `iov_serialize` helpers in `sink_async.hpp` make line capture trivial.
