#pragma once

#include "sink.hpp"

#include <cstddef>
#include <memory>

namespace elog {

// Size-based rotating file sink.
//
// Wraps a base file at `path` (e.g. "app.log").
//
// Rotation policy:
//   When a write would push the cumulative on-disk file size beyond
//   `max_bytes`, rotate before opening a fresh `path`:
//
//     - Remove `app.{max_files-1}.log` if it exists.
//     - Rename `app.{i}.log`     -> `app.{i+1}.log`
//       for i = max_files-2 .. 1
//     - Rename `app.log`         -> `app.1.log`
//     - Open a fresh empty `app.log`.
//
//   max_files == 1 disables the rename chain (the file is just truncated
//   on rotation). max_files >= 2 follows the chain above.
//
// Thread safety:
//   The whole write() is serialized under an internal std::mutex. This is
//   the MVP correctness path; per-write contention is the main downside.
//   Linux already serializes per-fd writev so we get atomicity for free,
//   but we still need synchronization to swap the fd safely on rotation.
//
//   TODO(M8): perf goal — make the common-case (no rotation) write path
//   lock-free using std::atomic<int> fd_ + std::atomic<size_t>
//   bytes_written_ + a generation counter, taking the mutex only to
//   actually rotate.
class Sink;

std::unique_ptr<Sink> make_rotating_file_sink(const char* path,
                                              std::size_t max_bytes,
                                              std::size_t max_files);

}  // namespace elog
