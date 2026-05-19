#include "elog/rotating_sink.hpp"
#include "elog/sink.hpp"
#include "elog/level.hpp"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

namespace elog {

namespace {

// Copied (not refactored) from src/sink.cpp on purpose; we'll DRY in M8.
// Loops on EINTR, advances the iov vector after partial writes.
void writev_all(int fd, const iovec* iov, int n) noexcept {
    iovec local[64];
    iovec* p;
    iovec* heap = nullptr;
    if (n <= static_cast<int>(sizeof(local) / sizeof(local[0]))) {
        for (int i = 0; i < n; ++i) local[i] = iov[i];
        p = local;
    } else {
        heap = new (std::nothrow) iovec[n];
        if (!heap) return;
        for (int i = 0; i < n; ++i) heap[i] = iov[i];
        p = heap;
    }

    int idx = 0;
    int rem = n;
    while (rem > 0) {
        ssize_t w = ::writev(fd, p + idx, rem);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        std::size_t adv = static_cast<std::size_t>(w);
        while (rem > 0 && adv >= p[idx].iov_len) {
            adv -= p[idx].iov_len;
            ++idx;
            --rem;
        }
        if (rem > 0 && adv > 0) {
            p[idx].iov_base = static_cast<char*>(p[idx].iov_base) + adv;
            p[idx].iov_len -= adv;
        }
    }

    delete[] heap;
}

// Build "<base>.<n>.log" from a base path that may or may not end in .log.
// If `path` ends in ".log", we splice the index before that suffix:
//   "app.log"           -> "app.<n>.log"
//   "logs/app.log"      -> "logs/app.<n>.log"
//   "app"               -> "app.<n>"     (no .log suffix)
//   "app.txt"           -> "app.txt.<n>" (suffix is not .log)
std::string indexed_path(const std::string& path, std::size_t n) {
    static const char kSuffix[] = ".log";
    constexpr std::size_t kSuffixLen = sizeof(kSuffix) - 1;

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%zu", n);

    if (path.size() >= kSuffixLen &&
        path.compare(path.size() - kSuffixLen, kSuffixLen, kSuffix) == 0) {
        std::string out;
        out.reserve(path.size() + 1 + std::strlen(buf));
        out.append(path, 0, path.size() - kSuffixLen);
        out.push_back('.');
        out.append(buf);
        out.append(kSuffix);
        return out;
    }
    std::string out = path;
    out.push_back('.');
    out.append(buf);
    return out;
}

off_t file_size_or_zero(const char* path) noexcept {
    struct stat st;
    if (::stat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    return st.st_size;
}

class RotatingFileSink : public Sink {
public:
    RotatingFileSink(const char* path, std::size_t max_bytes,
                     std::size_t max_files)
        : path_(path ? path : ""),
          max_bytes_(max_bytes ? max_bytes : 1),
          max_files_(max_files ? max_files : 1),
          fd_(-1),
          bytes_written_(0) {
        // O_APPEND so any concurrent or accidental external writes also
        // land at end-of-file (Linux guarantees writev-up-to-PIPE_BUF
        // atomicity vs. concurrent writes too). We still track the size
        // ourselves to decide when to rotate.
        fd_ = ::open(path_.c_str(),
                     O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd_ >= 0) {
            bytes_written_.store(
                static_cast<std::size_t>(file_size_or_zero(path_.c_str())),
                std::memory_order_relaxed);
        }
    }

    ~RotatingFileSink() override {
        if (fd_ >= 0) ::close(fd_);
    }

    void write(Level /*lvl*/, const iovec* iov, int n) override {
        if (n <= 0 || path_.empty()) return;

        std::size_t total = 0;
        for (int i = 0; i < n; ++i) total += iov[i].iov_len;

        // MVP: serialize the whole write under the rotation mutex. Linux
        // already serializes the underlying writev per-fd, so this lock
        // is only needed to make the rotation rename-and-reopen race-free.
        // TODO(M8): drop the lock from the common path using an atomic
        // fd_+gen and only acquire on rotation (see header comment).
        std::lock_guard<std::mutex> lk(mu_);
        if (fd_ < 0) return;

        std::size_t now = bytes_written_.load(std::memory_order_relaxed) + total;
        bytes_written_.store(now, std::memory_order_relaxed);

        writev_all(fd_, iov, n);

        if (now >= max_bytes_) {
            rotate_locked();
        }
    }

    void flush() override {
        std::lock_guard<std::mutex> lk(mu_);
        if (fd_ >= 0) ::fsync(fd_);
    }

private:
    void rotate_locked() noexcept {
        // Close current fd before renaming; on POSIX renaming an open
        // file is fine but closing first matches "fresh open" semantics
        // and avoids holding a file description on the now-rotated name.
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }

        if (max_files_ >= 2) {
            // Remove the eldest, if it exists.
            std::string eldest = indexed_path(path_, max_files_ - 1);
            ::unlink(eldest.c_str());

            // Shift app.{i}.log -> app.{i+1}.log for i = max_files-2 .. 1
            for (std::size_t i = max_files_ - 1; i >= 2; --i) {
                std::string from = indexed_path(path_, i - 1);
                std::string to   = indexed_path(path_, i);
                ::rename(from.c_str(), to.c_str());
            }

            // app.log -> app.1.log
            std::string first = indexed_path(path_, 1);
            ::rename(path_.c_str(), first.c_str());
        } else {
            // max_files == 1: just truncate on next open via O_TRUNC.
            ::unlink(path_.c_str());
        }

        // Open a fresh app.log. With max_files==1 the file is gone above
        // so plain O_CREAT is enough; with max_files>=2 we just renamed
        // it away. O_TRUNC guards against the tiny race where another
        // process recreated it.
        fd_ = ::open(path_.c_str(),
                     O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        bytes_written_.store(0, std::memory_order_relaxed);
    }

    std::string path_;
    std::size_t max_bytes_;
    std::size_t max_files_;
    int fd_;
    std::atomic<std::size_t> bytes_written_;
    std::mutex mu_;
};

}  // namespace

std::unique_ptr<Sink> make_rotating_file_sink(const char* path,
                                              std::size_t max_bytes,
                                              std::size_t max_files) {
    if (!path || !*path) return nullptr;
    if (max_bytes == 0) max_bytes = 1;
    if (max_files == 0) max_files = 1;
    std::unique_ptr<RotatingFileSink> s(
        new RotatingFileSink(path, max_bytes, max_files));
    return std::unique_ptr<Sink>(s.release());
}

}  // namespace elog
