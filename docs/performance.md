# Performance

## Design choices that drive ns/op

1. **`writev`-driven zero-copy.** Static format pieces (`.rodata`),
   string-like args (borrowed from `std::string::data()`), ANSI escapes,
   and the trailing newline are passed straight to the kernel as iovec
   entries. Only numeric / custom args land in a per-thread scratch
   buffer.

2. **Compile-time format parsing.** Each call site has a `static
   constexpr FmtSpec` produced by parsing the literal at compile time.
   No runtime scanning, no allocations on the emit path.

3. **Lazy evaluation.** A LOG below the active threshold compiles to one
   atomic load + branch and **does not evaluate its argument
   expressions**. Same for rate-limited LOGs whose gate is closed.

4. **Lock-free hot path.** Linux serializes per-fd `writev` at the inode
   level; the synchronous sinks need no userspace mutex. The tie path
   uses a `tie_count_` atomic fast-path to skip visited-set bookkeeping
   when nothing is tied. Mutexes appear only at: registry mutation, file
   rotation boundary, the buffered file sink, and the tie fan-out when
   ties exist.

## Snapshot vs spdlog 1.14 and PhotonLibOS ALog

Median of repeated runs on aarch64 Linux (Release, default build), each
library configured to emit equivalent content. Three I/O modes — pick
the row that matches your actual setup. All numbers ns/op.

```
== sink: NullSink (no I/O — pure dispatch + format) ==
scenario        elog            spdlog          alog
disabled        1.0             2.7             0.8 ★
int only        38 ★            44              78
double only     73 ★            101             192
int+double      58 ★            81              139
two strings     39 ★            85              80
5 mixed args    65 ★            136             159
every-N(100)    2.5             —               2.5 ★

== sink: tmpfs file, every emit flushed (durable) ==
scenario        elog            spdlog          alog
int only        225 ★          290             248
double only     257 ★          347             370
int+double      241 ★          324             313
two strings     208 ★          340             250
5 mixed args    302 ★          365             325
every-N(100)    4.2 ★          —               5.6

== sink: tmpfs, INFO/DEBUG buffered, WARN+ flushed (throughput) ==
scenario        elog            spdlog
int only        78 ★           137
double only     100 ★          197
int+double      99 ★           175
two strings     75 ★           182
5 mixed args    112 ★          233
```

Caveats on fairness:

- **`spdlog` in the durable row** is forced into `flush_on(info)` to
  match ELog's per-emit `writev` durability contract. Without this
  override, spdlog's default `basic_file_sink` batches ~40 lines per
  fwrite via stdio's 4 KiB buffer — fast but loses recent lines on a
  process crash.
- **`every-N(100)`** is ELog vs ALog only. spdlog has no native
  rate-limit; comparing against a hand-rolled `static atomic + modulo`
  wrapper isn't the same thing.
- **The buffered row omits ALog** — ALog has no buffered file sink.

## Benchmark binaries

| binary | what |
|---|---|
| `bench_basic` | quick wall-clock ns/op on a few core scenarios |
| `bench_perf` | per-scenario `__attribute__((noinline))` so `perf stat` can attach. Driven by `benchmarks/run_perf.sh`. |
| `bench_ablation` | adds one piece of work at a time so the per-stage cost can be read off the deltas |
| `bench_everyn` | focused noise-floor study of the every-N gate |
| `bench_compare` | cross-library matrix vs spdlog and ALog, three I/O modes |
| `bench_compose` | composite-type render path (vector<string> vs `iov_pack`) |
| `bench_itoa` | itoa strategy comparison |

Build the comparison benchmark with `-DELOG_BUILD_COMPARE=ON` (FetchContent
fetches spdlog v1.14.1 and PhotonLibOS HEAD).

## Ablation: where the time goes for an INFO log

`bench_ablation` peels work off the emit path one layer at a time:

```
LOG_INFO_F (2 ints, full prologue)               44.0 ns/op
LOG_INFO_F (2 ints, noop prologue)               20.2 ns/op   ← prologue: ~24 ns
LOG_INFO_F (1 int, noop prologue)                17.5 ns/op   ← second int: ~3 ns
LOG_INFO_F (0 args, noop prologue)               15.7 ns/op   ← first int + composition: ~5 ns
Logger::emit (1 iov, no macro, no prologue)       3.1 ns/op   ← emit dispatch
now() syscall only                               12.1 ns/op   ← clock_gettime via vDSO
get_id() only                                     0.8 ns/op
```

So a typical `LOG_INFO_F("a={} b={}", x, y)` decomposes roughly as:

| stage | ns |
|---|---:|
| `system_clock::now()` (vDSO) | ~12 |
| prologue: time/level/tid/file:line into scratch | ~12 |
| emit + sink dispatch | ~3 |
| iov assembly + writev kernel-side | ~5 |
| 2 × int render | ~6 |
| **Total** | **~38 ns** |

## Disabled-call-site cost

```
LOG_INFO_F (level OFF) 1.0 ns/op
```

That's `default_logger()` (inline magic-static guard byte test) + atomic
level load + cmp + branch. About 3 instructions of overhead per disabled
LOG, with the branch reliably predicted. At 10⁶ disabled INFO calls per
second, that's 1 ms of CPU — negligible. **Sprinkling debug LOGs liberally
in hot paths is a non-issue when the level is OFF.**

## What to do if your scenario looks slow

- **Heavy doubles?** ELog uses Grisu2 (`src/grisu2.cpp`) which is already
  ~30% faster than `snprintf("%g")` and ~50% faster than libc's
  `to_string`. If you're seeing slow logs that are mostly doubles, the
  remaining cost is ineliminable in software (~30 ns each).
- **Composite types with strings?** Use `iov_pack` (see [custom-types.md](custom-types.md))
  to keep strings zero-copy. ~50 ns saved per emit on string-heavy
  composites.
- **File output is the bottleneck?** That's the syscall (~180 ns to
  tmpfs, more for real disk). The buffered file sink amortizes it for
  INFO/DEBUG paths if you can accept the durability tradeoff (see
  [sinks.md](sinks.md)).
- **Profile it.** `bench_perf` + `benchmarks/run_perf.sh` will give you
  perf-stat output (cycles, IPC, branch misses) on real hardware.
