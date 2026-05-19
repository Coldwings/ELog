# Rate-limited and aggregated logging

ELog ships eight families of aggregation macros. Each family covers all
six levels and a `LOGGER_*` form taking an explicit logger.

## Quick taxonomy

| Family | Macro | Effect |
|---|---|---|
| Count gate | `LOG_INFO_EVERY_N(n, fmt, ...)` | Emit on call counts 1, n+1, 2n+1, ... |
| Time gate (sec) | `LOG_INFO_EVERY_N_SEC(s, fmt, ...)` | At most one emit per s seconds |
| Time gate (ms) | `LOG_INFO_EVERY_N_MS(ms, fmt, ...)` | Same but ms granularity |
| First N | `LOG_INFO_FIRST_N(n, fmt, ...)` | Emit only the first n calls |
| Once | `LOG_INFO_ONCE(fmt, ...)` | Alias for `FIRST_N(1)` |
| Sampling | `LOG_INFO_SAMPLED(percent, fmt, ...)` | Random sample (xorshift64 PRNG) |
| Token bucket | `LOG_INFO_BURST(burst, refill_per_sec, fmt, ...)` | Allow bursts up to capacity |
| Dedup | `LOG_INFO_DEDUP(fmt, ...)` | Consecutive same call-site collapse into a counted summary |
| Conditional | `LOG_INFO_IF(cond, fmt, ...)` | Plain conditional |

All families lazy-evaluate: when the gate suppresses, the format
arguments are **not** evaluated and the call site costs ~2.5 ns or less.

```cpp
LOG_INFO_EVERY_N(100, "i = {}", i);                  // 1st, 101st, 201st, ...
LOG_INFO_EVERY_N_SEC(5, "{} active conns", count);   // at most every 5 s
LOG_INFO_EVERY_N_MS(250, "queue depth = {}", depth); // at most every 250 ms

LOG_WARN_FIRST_N(3, "deprecated config field; ignoring");
LOG_INFO_ONCE("startup complete");

LOG_INFO_SAMPLED(1, "rare trace: {}", req_id);       // 1% of calls
LOG_INFO_BURST(/*burst=*/10, /*refill_per_sec=*/2.0,
               "user {} probed", user);              // 10/s steady, brief 10-burst

LOG_ERROR_DEDUP("config check failed");              // keeps the spam in check

LOG_ERROR_IF(rc < 0, "syscall returned {}", rc);
```

`LOGGER_*` variants take an explicit logger:

```cpp
LOGGER_EVERY_N_SEC(audit_log, elog::Level::INFO, 60, "audit summary: {}", state);
```

## Semantics of "level vs gate"

ELog checks **level first, then gate**. If the level is not enabled, the
gate's counter is not advanced — the call is fully short-circuited.
This matches the principle that a disabled level has no observable side
effects.

(spdlog has no native rate limiter; the typical user-written wrapper does
gate-then-level — slightly cheaper but counts increment even when the
level is off. ALog's `LOG_EVERY_N(N, LOG_INFO(...))` does gate-first
because its inner LogBuilder defers level check to its destructor. Both
are reasonable; ELog's choice is "level governs everything below it".)

## DEDUP — consecutive call-site suppression

`LOG_*_DEDUP` is the most useful and most subtle gate. It collapses
**consecutive emits from the same call site** into a single line followed
by a synthesized summary on flush:

```cpp
for (int i = 0; i < 1000; ++i) {
    LOG_WARN_DEDUP("config check failed");
}
LOGGER_F(L, elog::Level::INFO, "moving on");
elog::flush_dedup();
```

Output:

```
... WARN ... | config check failed
... INFO ... | moving on
... INFO ... | [suppressed 999 occurrences of foo.cpp:NN]
```

A summary is emitted when:

- A different call site emits (cross-site flush — the previous site's
  pending count is summarized first).
- The user calls `elog::flush_dedup()` explicitly.
- The program exits via `atexit`.

The summary line does **not** re-render the original arguments — it
references file/line/count only. This means the dedup state can be tiny
(no need to keep the latest args alive).

## Performance

Numbers from `bench_compare null` mode on aarch64. All gates suppress 99%
of the time (counter % 100 != 0 case for `EVERY_N(100)`), so the path
measured is the gate-rejected path:

| Family | ns/op |
|---|---:|
| EVERY_N(100) | 2.5 |
| EVERY_N_SEC(1) | ~3 |
| FIRST_N(N) (after threshold) | ~1.5 |
| ONCE (after threshold) | ~1.5 |
| SAMPLED(1) | ~3 |
| BURST | ~5 (mutex per call site) |
| DEDUP | ~2.5 |
| IF(false) | ~1 |

These are well below "one disabled INFO call" cost (1 ns) plus a tiny
gate. The point: **don't worry about scattering rate-limited LOGs in hot
paths**. The cost is in the noise.

## Internal state

Each call site gets its own state object via a function-local `static`.
Construction is one-time and constexpr where possible. The state lives
for the lifetime of the program.

- `EveryNState`, `FirstNState`: `std::atomic<uint64_t>` counter
- `EveryTimeState`: `std::atomic<int64_t>` next-allowed nanosecond,
  CAS-loop ensures only one thread per window passes
- `SampledState`: thread-local xorshift64 PRNG, no shared state
- `BurstState`: per-site `std::mutex` + token bucket (per-site contention
  is normally zero so the mutex is not measurable in practice)
- `DedupState`: per-site `std::atomic<uint64_t>` suppressed counter, plus
  a global `std::atomic<DedupState*>` "last fired" pointer for cross-site
  flush
