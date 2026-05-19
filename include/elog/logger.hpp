#pragma once

#include "level.hpp"
#include "prologue.hpp"
#include "sink.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <sys/uio.h>
#include <vector>

namespace elog {

class Logger {
    std::atomic<Level> level_{Level::INFO};
    std::vector<std::unique_ptr<Sink>> sinks_;
    PrologueFn prologue_ = &default_prologue;
    std::string name_;

    mutable std::mutex tie_mu_;
    std::vector<Logger*> ties_;
    std::atomic<unsigned> tie_count_{0};

public:
    explicit Logger(std::string name = "default");

    bool enabled(Level lvl) const noexcept {
        return lvl >= level_.load(std::memory_order_relaxed);
    }

    void set_level(Level lvl) noexcept;
    Level level() const noexcept { return level_.load(std::memory_order_relaxed); }

    void add_sink(std::unique_ptr<Sink> s);
    void clear_sinks();

    void set_prologue(PrologueFn fn);
    PrologueFn prologue_fn() const noexcept { return prologue_; }

    // Tie semantics: emits forward unconditionally to tied loggers (no
    // re-check of their level). Cycles are detected per-thread so
    // A.tie(B); B.tie(A) produces one emit per logger, not infinite recursion.
    void tie(Logger& other);
    void untie(Logger& other);
    std::size_t tied_count() const noexcept;

    void emit(Level lvl, const iovec* iov, int n);
    void flush();

    const std::string& name() const noexcept { return name_; }
};

namespace detail {
Logger* default_logger_init();
}

inline Logger& default_logger() noexcept {
    static Logger* const instance = ::elog::detail::default_logger_init();
    return *instance;
}

}  // namespace elog
