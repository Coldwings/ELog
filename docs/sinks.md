# Sinks

A `Sink` is what receives an emitted log line. ELog ships four built-ins
plus an opt-in buffered variant. Custom sinks are easy.

## Built-in factories

```cpp
#include "elog/sink.hpp"
#include "elog/rotating_sink.hpp"
#include "elog/buffered_file_sink.hpp"

auto& L = elog::default_logger();

// Process descriptors. Color flag wraps lines with ANSI escapes when
// the sink chooses to (off by default for stdout, on for stderr).
L.add_sink(elog::make_stderr_sink(/*colored=*/true));
L.add_sink(elog::make_stdout_sink(/*colored=*/false));

// Plain file. One writev() syscall per emit, O_APPEND.
L.add_sink(elog::make_file_sink("app.log"));

// Size-rotating file. Renames foo.log -> foo.1.log -> foo.2.log ...
// up to max_files when total bytes pass max_bytes.
L.add_sink(elog::make_rotating_file_sink("app.log",
    /*max_bytes=*/64 << 20, /*max_files=*/5));

// Opt-in buffered file (see "Durability tradeoff" below).
L.add_sink(elog::make_buffered_file_sink("hot.log",
    /*buffer_bytes=*/64 * 1024,
    /*flush_threshold=*/elog::Level::WARN));
```

A logger can hold any number of sinks. Each `LOG_*` is dispatched to all of
them in order.

## Durability: no user-space buffering by default

The default file sink (`make_file_sink`) does **one `writev()` syscall per
emit**. When `LOG_*` returns, the bytes have already crossed the kernel
boundary. The page cache only loses data on kernel panic or host power
loss — process SIGSEGV / SIGABRT / `_exit` cannot drop log lines that
already returned.

This is a deliberate tradeoff against userland buffering (the default for
`std::FILE*`, `spdlog::basic_file_sink`, glog's INFO buffering). User-side
buffering reduces syscall count but loses the in-flight buffer when a
process crashes — and **the few seconds before the crash are usually the
only logs that matter for diagnosis**.

If you need batched throughput for INFO/DEBUG paths and explicitly accept
that loss, the buffered sink:

```cpp
elog::make_buffered_file_sink(path, buffer_bytes, flush_threshold);
```

mirrors the glog/spdlog `flush_on(WARN)` policy:

- TRACE/DEBUG/INFO are appended to a 64 KiB (default) user-space buffer
  and only flushed when the buffer fills, on explicit `Logger::flush()`,
  or in the sink's destructor.
- WARN/ERROR/FATAL are appended **and** trigger an immediate `writev` of
  the entire buffer. Critical messages always survive crashes.

The threshold is configurable, e.g. `Level::ERROR` to also batch WARN.

## Per-fd atomicity

Linux serializes per-`fd` `writev` at the kernel inode/file level. ELog's
synchronous sinks rely on this and take **no userspace mutex** on the
emit path. Multi-threaded log lines never interleave at the byte level
within a single sink. This holds for stderr, stdout, regular files, and
the rotating sink (which only takes a mutex during the actual rotation
boundary).

The buffered sink does take a per-sink mutex around the user-space buffer.

## The `Sink` interface

```cpp
class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(Level lvl, const iovec* iov, int n) = 0;
    virtual void flush() {}
};
```

### Lifetime contract for `iov`

The iov entries handed to `write()` may point into the caller thread's
TLS scratch buffer. **You must consume them synchronously and must not
retain pointers past return.** This is the rule that makes ELog's
zero-copy emit path possible — the static format pieces live in `.rodata`
forever, but the rendered numeric pieces and the per-call prologue live
in TLS scratch only for the duration of the `write()` call.

If your sink wants to defer or queue work (an async wrapper, a network
sink that batches), you must serialize the iov payload into your own
storage **before** returning from `write()`. ELog ships two small helpers
in `<elog/sink_async.hpp>`:

```cpp
std::size_t elog::iov_total_bytes(const iovec*, int n) noexcept;
std::size_t elog::iov_serialize (const iovec*, int n, char* out) noexcept;
```

so you can flatten in one shot.

## Writing a custom synchronous sink

```cpp
class JournalSink : public elog::Sink {
public:
    explicit JournalSink(int priority) : prio_(priority) {}

    void write(elog::Level lvl, const iovec* iov, int n) override {
        // Map ELog level to syslog priority if you want.
        (void)lvl;
        // sd_journal_sendv accepts iovec; map our entries 1:1.
        ::sd_journal_sendv(iov, n);   // hypothetical
    }

private:
    int prio_;
};

L.add_sink(std::unique_ptr<elog::Sink>(new JournalSink(LOG_INFO)));
```

That's the whole API. Anything ABI-compatible with `iovec` (writev,
sendmsg, sd_journal_sendv, ...) is one line.

## Writing an async sink

ELog deliberately doesn't ship one — the right queue policy
(drop / block / overwrite-oldest), drain semantics, and shutdown ordering
vary per application.  `examples/async_sink_skeleton.cpp` is a 90-line
working SPSC implementation you can copy:

```cpp
class AsyncSink : public elog::Sink {
public:
    AsyncSink(std::unique_ptr<elog::Sink> inner, std::size_t cap)
        : inner_(std::move(inner)), cap_(cap), worker_([this] { run(); }) {}

    ~AsyncSink() override {
        { std::lock_guard<std::mutex> lk(mu_); stop_ = true; }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        drain();
    }

    void write(elog::Level lvl, const iovec* iov, int n) override {
        std::size_t total = elog::iov_total_bytes(iov, n);
        std::string buf(total, '\0');
        elog::iov_serialize(iov, n, &buf[0]);     // copy out before queueing

        {
            std::lock_guard<std::mutex> lk(mu_);
            if (q_.size() >= cap_) { ++dropped_; return; }
            q_.emplace_back(lvl, std::move(buf));
        }
        cv_.notify_one();
    }
    // ... worker thread replays via inner_->write(level, &iov, 1) ...
};
```

Wrap any synchronous sink:

```cpp
L.add_sink(make_async_sink(elog::make_file_sink("burst.log"),
                           /*queue_cap=*/4096));
```

The example covers drop-on-full policy. For block-on-full or
overwrite-oldest, swap the queue.

## Multi-sink behavior

```cpp
L.add_sink(elog::make_stderr_sink(true));
L.add_sink(elog::make_file_sink("app.log"));
```

Each emit goes to every sink in the order they were added. There's no
mutex coordinating them — Linux's per-fd atomicity covers each sink's
output independently. Ordering across two different fds isn't guaranteed
to be globally sequenced (one CPU core can race ahead of another) but
**within** a single fd lines never tear.
