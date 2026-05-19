#pragma once

#include "level.hpp"

#include <memory>
#include <sys/uio.h>

namespace elog {

// Sink contract:
//   - write() may be called concurrently from multiple threads.
//   - The iov entries passed to write() may point into the calling
//     thread's TLS scratch buffer. The implementation MUST consume
//     them synchronously and MUST NOT retain pointers past return.
//   - An async sink wrapper must serialize/copy the bytes it cares
//     about before queueing.
class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(Level lvl, const iovec* iov, int n) = 0;
    virtual void flush() {}
};

std::unique_ptr<Sink> make_stderr_sink(bool colored = true);
std::unique_ptr<Sink> make_stdout_sink(bool colored = false);
std::unique_ptr<Sink> make_file_sink(const char* path);

}  // namespace elog
