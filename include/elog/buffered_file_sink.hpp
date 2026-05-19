#pragma once

#include "level.hpp"
#include "sink.hpp"

#include <cstddef>
#include <memory>

namespace elog {

// Userland-buffered file sink. Trades crash-time integrity of recent INFO/
// DEBUG/TRACE entries for fewer syscalls under high log volume.
//
// Critical entries (>= flush_threshold, default WARN) are flushed
// synchronously after being appended, matching glog and spdlog policy.
// So:
//   - INFO/DEBUG/TRACE: batched into the buffer, written when full or on
//     explicit flush(). MAY be lost if the process crashes.
//   - WARN/ERROR/FATAL: appended then immediately writev'd. Survive crashes
//     for the same reason make_file_sink() entries do.
//
// flush() is also called automatically from the sink destructor so an
// orderly shutdown doesn't drop buffered lines.
//
// NOT a replacement for make_file_sink() — the default file sink remains
// unbuffered for the reason that crashes happen and the most recent
// few seconds of logs are usually the most valuable diagnostic data.
std::unique_ptr<Sink> make_buffered_file_sink(
    const char* path,
    std::size_t buffer_bytes = 64 * 1024,
    Level flush_threshold = Level::WARN);

}  // namespace elog
