# ELog

[![ci](https://github.com/Coldwings/ELog/actions/workflows/ci.yml/badge.svg)](https://github.com/Coldwings/ELog/actions/workflows/ci.yml)
[![bench](https://github.com/Coldwings/ELog/actions/workflows/bench.yml/badge.svg)](https://github.com/Coldwings/ELog/actions/workflows/bench.yml)

A high-performance C++14 logging library for POSIX, designed around four
ideas:

1. **`writev`-driven zero-copy.** Static format pieces, string-like args,
   ANSI escapes, and the trailing newline are passed straight to the
   kernel as `iovec` entries. Only numeric / custom args get rendered
   into a per-thread scratch buffer.
2. **Compile-time format parsing.** The format string is parsed once per
   call-site into a `static constexpr` piece table — no runtime scanning
   of the literal, no allocations on the emit path.
3. **Lazy evaluation.** When a level or rate-limit gate is closed, the
   argument expressions are not evaluated. Disabled call sites compile
   down to `load + cmp + jne`.
4. **Lock-free emit.** Linux serializes per-fd `writev` at the inode
   level, so the default synchronous sinks need no userspace mutex on
   the hot path.

C++14 standard only — no `if constexpr`, no fold expressions, no
`std::string_view`, no UDL extensions.

## 30-second example

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

```
2026-05-19 13:30:15.421033 INFO [12345] foo.cpp:5 | user alice logged in from 10.0.0.1
2026-05-19 13:30:15.421054 WARN [12345] foo.cpp:6 | retry 3/5 after 250ms
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build       # 100 cases, all pass
```

Full options and integration: [docs/getting-started.md](docs/getting-started.md).

## Performance at a glance

```
sink: per-emit writev, durable     elog        spdlog      alog
int only                           225 ★      290         248
double only                        257 ★      347         370
two strings                        208 ★      340         250
5 mixed args                       302 ★      365         325
```

Numbers ns/op, aarch64 Linux Release. spdlog forced into `flush_on(info)`
to match the durability contract. Full benchmark with three I/O modes,
ablation breakdown, and methodology in
[docs/performance.md](docs/performance.md).

**Live numbers from CI** — the
[bench workflow](https://github.com/Coldwings/ELog/actions/workflows/bench.yml)
runs on every push to main. Each successful run's *Summary* tab renders
the bench_basic / bench_compose / bench_compare tables inline; the
*Artifacts* section has the raw `.txt` outputs (5 repetitions of
bench_compare) for download. Numbers there are on x86-64 Ubuntu so they
differ from the aarch64 snapshot above, but the relative shape is the
same.

## Documentation

| | |
|---|---|
| [getting-started.md](docs/getting-started.md) | install, levels, basic LOG_*_F, format placeholders, built-in renderable types |
| [custom-types.md](docs/custom-types.md)       | extending ELog with your own types: `elog_render` ADL pattern and `iov_pack` for composite zero-copy |
| [sinks.md](docs/sinks.md)                     | built-in sinks (stderr/stdout/file/rotating/buffered), the durability tradeoff, writing custom and async sinks |
| [aggregation.md](docs/aggregation.md)         | `EVERY_N` / `EVERY_N_SEC` / `FIRST_N` / `ONCE` / `SAMPLED` / `BURST` / `DEDUP` / `IF` macro families |
| [multi-logger.md](docs/multi-logger.md)       | named registry, tie / fan-out, custom prologue, time semantics |
| [performance.md](docs/performance.md)         | design rationale, benchmark snapshot, ablation methodology, what to do if a scenario looks slow |

## Examples

`examples/` ships five working programs you can run directly:

- `basic.cpp` — minimal usage tour
- `multi_logger.cpp` — named loggers, tie, rotating file
- `aggregation.cpp` — every-N, dedup, first-N
- `custom_type.cpp` — both extension patterns side-by-side
- `async_sink_skeleton.cpp` — 90-line reference for a queued async sink

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
tests/                    # gtest, 100 cases
examples/                 # 5 demo programs
benchmarks/               # bench_basic, bench_perf, bench_ablation, bench_everyn, bench_compare, bench_compose, bench_itoa
docs/                     # markdown documentation
```

`bench_compare` fetches spdlog and PhotonLibOS via CMake `FetchContent` at
configure time — they are not vendored in the source tree.

## What ELog does not provide

- A built-in async sink. `examples/async_sink_skeleton.cpp` is a
  reference SPSC implementation you can copy.
- `printf`-style `%s/%d` placeholders. Use `{}` plus typed wrappers
  (`hex(x)`, `fixed(d, prec)`, `pad_left(v, width)`, …) for formatting
  control.
- Windows / non-POSIX support. Hard dependency on `writev`, `gettid`,
  `clock_gettime`.
- Structured logging / JSON output. Write a custom sink — the
  `iov_serialize` helpers in `sink_async.hpp` make line capture trivial.
