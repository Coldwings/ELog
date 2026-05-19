#include "elog/sink.hpp"
#include "elog/level.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/uio.h>
#include <unistd.h>

namespace elog {

namespace {

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

class FdSink : public Sink {
    int fd_;
    bool owns_;
    bool colored_;

public:
    FdSink(int fd, bool owns, bool colored) noexcept
        : fd_(fd), owns_(owns), colored_(colored) {}
    ~FdSink() override {
        if (owns_ && fd_ >= 0) ::close(fd_);
    }

    void write(Level lvl, const iovec* iov, int n) override {
        if (fd_ < 0 || n <= 0) return;
        if (!colored_) {
            writev_all(fd_, iov, n);
            return;
        }
        const char* color = level_color(lvl);
        const char* reset = level_color_reset();
        iovec ext[80];
        iovec* p;
        iovec* heap = nullptr;
        int total = n + 2;
        if (total <= static_cast<int>(sizeof(ext) / sizeof(ext[0]))) {
            p = ext;
        } else {
            heap = new (std::nothrow) iovec[total];
            if (!heap) {
                writev_all(fd_, iov, n);
                return;
            }
            p = heap;
        }
        // Layout: [color][iov[0..n-2]][reset][iov[n-1]] so that the
        // reset sits before the trailing newline.
        p[0].iov_base = const_cast<char*>(color);
        p[0].iov_len = std::strlen(color);
        for (int i = 0; i < n - 1; ++i) p[i + 1] = iov[i];
        p[n].iov_base = const_cast<char*>(reset);
        p[n].iov_len = std::strlen(reset);
        p[n + 1] = iov[n - 1];
        writev_all(fd_, p, total);
        delete[] heap;
    }

    void flush() override {
        if (fd_ >= 0) ::fsync(fd_);
    }
};

}  // namespace

std::unique_ptr<Sink> make_stderr_sink(bool colored) {
    return std::unique_ptr<Sink>(new FdSink(2, false, colored));
}

std::unique_ptr<Sink> make_stdout_sink(bool colored) {
    return std::unique_ptr<Sink>(new FdSink(1, false, colored));
}

std::unique_ptr<Sink> make_file_sink(const char* path) {
    int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return nullptr;
    return std::unique_ptr<Sink>(new FdSink(fd, true, false));
}

}  // namespace elog
