// Reference AsyncSink skeleton. Wraps any synchronous Sink behind a
// background thread and a bounded SPMC-ish queue. Drop-on-full policy.
//
// This is an EXAMPLE, not part of the library. Copy and adapt:
//   - Replace the queue with a lock-free MPSC if hot path contention
//     matters more than memory.
//   - Replace drop-on-full with block / overwrite-oldest as needed.
//   - Use a notify-once eventfd if you want to avoid the polling sleep.

#include "elog/elog.hpp"
#include "elog/sink_async.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <sys/uio.h>
#include <thread>
#include <utility>
#include <vector>

class AsyncSink : public elog::Sink {
public:
    AsyncSink(std::unique_ptr<elog::Sink> inner, std::size_t queue_cap = 4096)
        : inner_(std::move(inner)), cap_(queue_cap), worker_([this] { run(); }) {}

    ~AsyncSink() override {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        drain();
    }

    void write(elog::Level lvl, const iovec* iov, int n) override {
        std::size_t bytes = elog::iov_total_bytes(iov, n);
        std::string buf(bytes, '\0');
        elog::iov_serialize(iov, n, &buf[0]);

        {
            std::lock_guard<std::mutex> lk(mu_);
            if (q_.size() >= cap_) {
                ++dropped_;
                return;
            }
            q_.emplace_back(lvl, std::move(buf));
        }
        cv_.notify_one();
    }

    void flush() override {
        std::unique_lock<std::mutex> lk(mu_);
        cv_done_.wait(lk, [&] { return q_.empty(); });
        inner_->flush();
    }

    std::uint64_t dropped() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return dropped_;
    }

private:
    using Item = std::pair<elog::Level, std::string>;

    void run() {
        for (;;) {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return stop_ || !q_.empty(); });
            if (q_.empty() && stop_) break;
            Item it = std::move(q_.front());
            q_.pop_front();
            bool empty = q_.empty();
            lk.unlock();
            iovec iov;
            iov.iov_base = const_cast<char*>(it.second.data());
            iov.iov_len = it.second.size();
            inner_->write(it.first, &iov, 1);
            if (empty) cv_done_.notify_all();
        }
    }

    void drain() {
        for (auto& it : q_) {
            iovec iov;
            iov.iov_base = const_cast<char*>(it.second.data());
            iov.iov_len = it.second.size();
            inner_->write(it.first, &iov, 1);
        }
        q_.clear();
        inner_->flush();
    }

    std::unique_ptr<elog::Sink> inner_;
    std::size_t cap_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable cv_done_;
    std::deque<Item> q_;
    bool stop_ = false;
    std::uint64_t dropped_ = 0;
    std::thread worker_;
};

inline std::unique_ptr<elog::Sink> make_async_sink(std::unique_ptr<elog::Sink> inner,
                                                   std::size_t queue_cap = 4096) {
    return std::unique_ptr<elog::Sink>(new AsyncSink(std::move(inner), queue_cap));
}

int main() {
    auto& L = elog::default_logger();
    L.clear_sinks();
    L.add_sink(make_async_sink(elog::make_stderr_sink(true)));

    for (int i = 0; i < 5; ++i) {
        LOG_INFO_F("async msg {} of 5", i);
    }
    L.flush();
    return 0;
}
