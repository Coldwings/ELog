# Multiple loggers, tie, prologue

## Default logger

Most code only needs `elog::default_logger()`. It is constructed lazily on
first access, registered as `"default"`, and outlives the program (never
destroyed — avoids static-destruction-order issues with shutdown-time
LOGs).

```cpp
auto& L = elog::default_logger();
L.set_level(elog::Level::DEBUG);
L.add_sink(elog::make_file_sink("app.log"));
```

The default logger comes preconfigured with one stderr sink and `INFO`
level.

## Named registry

```cpp
#include "elog/registry.hpp"

auto& app   = elog::get_logger("app");      // creates if absent
auto& audit = elog::get_logger("audit");

audit.set_level(elog::Level::INFO);
audit.add_sink(elog::make_rotating_file_sink("audit.log", 64<<20, 8));

LOGGER_F(audit, elog::Level::INFO, "user {} did {}", uid, action);
```

```cpp
elog::Logger* find_logger(const std::string& name);   // get or null
bool          remove_logger(const std::string& name); // delete from registry
std::vector<std::string> logger_names();              // snapshot of all
```

`get_logger("default")` returns the same instance as
`elog::default_logger()`.

The registry mutex is taken only on lookup/create/remove. The hot
emit path on a found logger doesn't touch it.

## Tie: fan-out to other loggers

```cpp
auto& app   = elog::get_logger("app");
auto& audit = elog::get_logger("audit");

app.add_sink(elog::make_stderr_sink(true));
audit.add_sink(elog::make_rotating_file_sink("audit.log", 64<<20, 8));

app.tie(audit);   // every emit on `app` is forwarded to `audit`
```

Tie is **fan-out**, not aliasing. Forwarded emits go through the tied
logger's sinks but **bypass its level check** (think of it as syslog-style
forwarding). If you want level-gated fan-out, gate at the source.

```cpp
app.tie(audit);
audit.tie(metrics);    // chain — emits from app reach all three

audit.untie(metrics);  // detach
```

### Cycle safety

```cpp
A.tie(B);
B.tie(A);   // legal, no infinite loop
```

A per-thread visited-set breaks cycles: each emit on a logger marks it,
forwards, then unmarks on return. Repeated visits in the same call chain
are skipped.

### Cost when no ties

`Logger::emit` checks an atomic `tie_count_` first. When no other loggers
are tied (the common case), it skips the visited-set bookkeeping entirely
and falls through to the sinks. This is the fast path that lets ELog
match spdlog/ALog on the per-emit microbenchmark.

## Custom prologue

The prologue is the per-line header rendered before the format pieces.
Default is:

```
YYYY-MM-DD HH:MM:SS.uuuuuu LEVEL [tid] file:line | 
```

Time is from `std::chrono::system_clock::now()` (Linux `CLOCK_REALTIME`),
microsecond precision. The second-resolution prefix is TLS-cached and
only the microsecond suffix is re-rendered each call. The Linux thread
id (via `gettid`) is also TLS-cached.

To customize, install your own function:

```cpp
elog::default_logger().set_prologue([](char* scratch, std::size_t& pos,
                                       elog::Iov& out,
                                       const elog::LogCtx& ctx) {
    std::size_t start = pos;

    // Example: fixed prefix, level, no time, no file/line.
    static const char prefix[] = "[my-app] ";
    std::memcpy(scratch + pos, prefix, sizeof(prefix) - 1);
    pos += sizeof(prefix) - 1;

    const char* lname = elog::level_name(ctx.level);
    std::size_t ln = std::strlen(lname);
    std::memcpy(scratch + pos, lname, ln);
    pos += ln;
    scratch[pos++] = ' ';

    out = elog::Iov{scratch + start, pos - start};
});
```

The `LogCtx` you receive:

```cpp
struct LogCtx {
    Level level;
    const char* file;        // __FILE__ at the LOG_*_F call site
    int line;                // __LINE__
    std::chrono::system_clock::time_point tp;   // taken in emit_f
    std::thread::id tid;
};
```

### Time semantics

`system_clock::now()` is **wall-clock**, not monotonic — subject to NTP
step adjustments, manual `date -s`, and leap seconds. Slew is bounded to
about ±500 ppm by the kernel and is normally invisible; step events are
rare in steady state.

If you need strictly monotonic timestamps in log lines, install a custom
prologue using `CLOCK_MONOTONIC` (or a hybrid: cache `(mono_t0, wall_t0)`
at startup, then compute `wall_at_log = wall_t0 + (mono_now - mono_t0)`
for monotonicity with wall-clock readability).

### Per-logger prologue

`set_prologue` is per-logger, not global. Different loggers can use
different prologues — useful for, e.g., an audit logger that wants
machine-parseable JSON-prefixed lines while the app logger uses the
default human-readable format.

## Multi-logger patterns

```cpp
// pattern 1: tag tree
auto& parser = elog::get_logger("parser");
auto& net    = elog::get_logger("net");
auto& root   = elog::default_logger();
parser.tie(root);  // module loggers fan out to root
net.tie(root);

// pattern 2: separate destinations
auto& diag = elog::get_logger("diag");   // file sink with verbose level
auto& term = elog::get_logger("term");   // stderr sink with quiet level
diag.set_level(elog::Level::DEBUG);
diag.add_sink(elog::make_rotating_file_sink("diag.log", 256<<20, 4));
term.set_level(elog::Level::WARN);
term.add_sink(elog::make_stderr_sink(true));
diag.tie(term);   // diag is the source of truth; term shows the user the bad bits

// pattern 3: the audit channel
auto& audit = elog::get_logger("audit");
audit.set_level(elog::Level::INFO);
audit.set_prologue(my_machine_parseable_prologue);
audit.add_sink(elog::make_rotating_file_sink("audit.log", 64<<20, 16));
// don't tie — audit is independent of normal logging
```
