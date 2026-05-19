#pragma once

#include "iov.hpp"
#include "level.hpp"

#include <chrono>
#include <cstddef>
#include <thread>

namespace elog {

struct LogCtx {
    Level level;
    const char* file;
    int line;
    std::chrono::system_clock::time_point tp;
    std::thread::id tid;
};

using PrologueFn = void(*)(char* scratch, std::size_t& pos, Iov& out, const LogCtx& ctx);

// Renders "YYYY-MM-DD HH:MM:SS.uuuuuu LEVEL [tid] file:line | ".
// Time source: std::chrono::system_clock::now() (CLOCK_REALTIME on Linux).
// This is wall-clock time and is therefore subject to NTP step adjustments,
// manual `date -s` changes, and leap seconds. NTP slew (continuous adjust)
// is bounded to ~500 ppm and is normally invisible. If you need strictly
// monotonic timestamps, install a custom prologue using CLOCK_MONOTONIC.
void default_prologue(char* scratch, std::size_t& pos, Iov& out, const LogCtx& ctx);

}  // namespace elog
