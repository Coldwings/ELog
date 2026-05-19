#include "elog/buffered_file_sink.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>

namespace elog {

namespace {

void writev_all(int fd, const iovec* iov, int n) noexcept {
    iovec local[16];
    iovec* p = local;
    std::vector<iovec> heap;
    if (n > 16) {
        heap.assign(iov, iov + n);
        p = heap.data();
    } else {
        for (int i = 0; i < n; ++i) p[i] = iov[i];
    }
    int idx = 0;
    int rem = n;
    while (rem > 0) {
        ssize_t w = ::writev(fd, p + idx, rem);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        std::size_t consumed = static_cast<std::size_t>(w);
        while (rem > 0 && consumed >= p[idx].iov_len) {
            consumed -= p[idx].iov_len;
            ++idx;
            --rem;
        }
        if (rem > 0 && consumed > 0) {
            p[idx].iov_base = static_cast<char*>(p[idx].iov_base) + consumed;
            p[idx].iov_len -= consumed;
        }
    }
}

class BufferedFileSink final : public Sink {
public:
    BufferedFileSink(int fd, std::size_t cap, Level threshold)
        : fd_(fd), cap_(cap), buf_(new char[cap]), threshold_(threshold) {}

    ~BufferedFileSink() override {
        flush();
        if (fd_ >= 0) ::close(fd_);
    }

    void write(Level lvl, const iovec* iov, int n) override {
        std::size_t total = 0;
        for (int i = 0; i < n; ++i) total += iov[i].iov_len;

        std::lock_guard<std::mutex> lk(mu_);

        if (total > cap_) {
            // entry larger than entire buffer: drain buffer, then writev
            // the entry directly without staging.
            flush_locked();
            writev_all(fd_, iov, n);
            return;
        }

        if (pos_ + total > cap_) {
            flush_locked();
        }

        char* dst = buf_.get() + pos_;
        for (int i = 0; i < n; ++i) {
            std::memcpy(dst, iov[i].iov_base, iov[i].iov_len);
            dst += iov[i].iov_len;
        }
        pos_ += total;

        if (lvl >= threshold_) {
            flush_locked();
        }
    }

    void flush() override {
        std::lock_guard<std::mutex> lk(mu_);
        flush_locked();
    }

private:
    void flush_locked() noexcept {
        if (pos_ == 0) return;
        iovec iov{buf_.get(), pos_};
        writev_all(fd_, &iov, 1);
        pos_ = 0;
    }

    int fd_;
    std::size_t cap_;
    std::unique_ptr<char[]> buf_;
    std::size_t pos_ = 0;
    Level threshold_;
    std::mutex mu_;
};

}  // namespace

std::unique_ptr<Sink> make_buffered_file_sink(const char* path,
                                              std::size_t buffer_bytes,
                                              Level flush_threshold) {
    int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) return nullptr;
    return std::unique_ptr<Sink>(
        new BufferedFileSink(fd, buffer_bytes, flush_threshold));
}

}  // namespace elog
