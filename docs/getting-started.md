# Getting started

## Install

ELog has no runtime dependencies beyond libc + pthread on POSIX. Build it
with CMake into a static library:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build       # 100 cases, all pass
```

Then in your project:

```cmake
add_subdirectory(path/to/ELog)
target_link_libraries(my_app PRIVATE elog)
```

CMake options:

| option | default | what it does |
|---|---|---|
| `ELOG_BUILD_TESTS` | ON | build the gtest suite |
| `ELOG_BUILD_EXAMPLES` | ON | build the 5 demo programs in `examples/` |
| `ELOG_BUILD_BENCHMARKS` | ON | build the in-tree benchmark binaries |
| `ELOG_BUILD_COMPARE` | OFF | also build `bench_compare`; pulls spdlog and PhotonLibOS via FetchContent |
| `ELOG_LTO` | OFF | enable link-time optimization |

Requires CMake ≥ 3.14, GCC 7+ / Clang 7+, Linux/POSIX.

## First emit

```cpp
#include "elog/elog.hpp"

int main() {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::INFO);

    LOG_INFO_F("user {} logged in from {}", "alice", "10.0.0.1");
    LOG_WARN_F("retry {}/{} after {}ms", 3, 5, 250);
}
```

Output:

```
2026-05-19 13:30:15.421033 INFO [12345] foo.cpp:5 | user alice logged in from 10.0.0.1
2026-05-19 13:30:15.421054 WARN [12345] foo.cpp:6 | retry 3/5 after 250ms
```

The default logger writes to `stderr` with ANSI colors when stderr is a
terminal, no colors otherwise.

## Levels

```
TRACE < DEBUG < INFO < WARN < ERROR < FATAL < OFF
```

Six per-level macros, plus the umbrella `LOGGER_F` that takes an explicit
logger and level:

```cpp
LOG_TRACE_F("...");      LOG_DEBUG_F("...");      LOG_INFO_F("...");
LOG_WARN_F("...");       LOG_ERROR_F("...");      LOG_FATAL_F("...");

LOGGER_F(my_logger, elog::Level::INFO, "explicit logger + level");
```

A LOG below the active threshold compiles to a single atomic load + branch
and **does not evaluate its argument expressions**:

```cpp
L.set_level(elog::Level::INFO);
LOG_DEBUG_F("expensive: {}", expensive_call());   // expensive_call NOT called
```

Set level dynamically at runtime:

```cpp
L.set_level(elog::Level::DEBUG);  // atomic; safe under concurrent log calls
```

## Format placeholders

Single token: `{}`. Adjacent literal `{{` and `}}` produce literal `{`/`}`.
There is no `:spec` syntax — formatting is expressed by typed wrapper
values, which keeps the parser trivial and the extension story uniform.

```cpp
LOG_INFO_F("{} dec, {} hex, {} bin, {} oct",
           42, elog::hex(42), elog::bin(42), elog::oct(42));

LOG_INFO_F("{} fixed-3, {} padded",
           elog::fixed(3.14159, 3), elog::pad_left(7, 4, '0'));

LOG_INFO_F("{} quoted, {} escaped",
           elog::quoted("hi"), elog::escaped("a\nb"));

LOG_INFO_F("hex dump = {}", elog::hexdump(buf, n));
```

## Built-in renderable types

Out of the box ELog renders:

- `bool`, `char`, all signed/unsigned integer widths
- `float`, `double`, `long double`
- `const char*`, `char[N]`, `std::string`, `elog::string_ref`
- `void*`, `std::nullptr_t`
- `std::thread::id`, `std::chrono::system_clock::time_point`
- Format wrappers: `hex`, `bin`, `oct`, `fixed(v, prec)`, `quoted`,
  `pad_left(v, width, fill='\\ ')`, `pad_right(v, width, fill)`, `precision`,
  `hexdump(p, n)`, `escaped`, `nullable<T*>`, `join(range, sep)`
- Containers (`render_extra.hpp`): `vector`, `array`, `pair`, `tuple`,
  `map`, `unordered_map`, `set`, `unordered_set`

To log a type ELog doesn't know about, see [custom-types.md](custom-types.md).

## File output

```cpp
#include "elog/sink.hpp"

auto& L = elog::default_logger();
L.add_sink(elog::make_file_sink("app.log"));
```

For more sinks (rotating, buffered, custom) see [sinks.md](sinks.md).

## Multiple loggers

```cpp
#include "elog/registry.hpp"

auto& app   = elog::get_logger("app");
auto& audit = elog::get_logger("audit");
LOGGER_F(audit, elog::Level::INFO, "user {} did {}", uid, action);
```

For named registry, fan-out via `tie`, and custom prologue see
[multi-logger.md](multi-logger.md).

## Rate-limited and conditional logging

```cpp
LOG_INFO_EVERY_N_SEC(5, "heartbeat: {} active connections", count);
LOG_WARN_FIRST_N(3, "config not yet loaded");
LOG_INFO_DEDUP("repeated check failed");
LOG_ERROR_IF(rc < 0, "syscall returned {}", rc);
```

For the full taxonomy of aggregation macros see
[aggregation.md](aggregation.md).
