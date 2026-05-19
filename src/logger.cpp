#include "elog/logger.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace elog {

namespace {

thread_local std::unordered_set<const Logger*> tl_emit_visited;

struct VisitGuard {
    const Logger* p;
    bool inserted;
    explicit VisitGuard(const Logger* x) : p(x) {
        inserted = tl_emit_visited.insert(p).second;
    }
    ~VisitGuard() {
        if (inserted) tl_emit_visited.erase(p);
    }
    VisitGuard(const VisitGuard&) = delete;
    VisitGuard& operator=(const VisitGuard&) = delete;
};

}  // namespace

Logger::Logger(std::string name) : name_(std::move(name)) {}

void Logger::set_level(Level lvl) noexcept {
    level_.store(lvl, std::memory_order_relaxed);
}

void Logger::add_sink(std::unique_ptr<Sink> s) {
    if (s) sinks_.push_back(std::move(s));
}

void Logger::clear_sinks() {
    sinks_.clear();
}

void Logger::set_prologue(PrologueFn fn) {
    prologue_ = (fn != nullptr) ? fn : &default_prologue;
}

void Logger::tie(Logger& other) {
    if (&other == this) return;
    std::lock_guard<std::mutex> lk(tie_mu_);
    auto it = std::find(ties_.begin(), ties_.end(), &other);
    if (it == ties_.end()) {
        ties_.push_back(&other);
        tie_count_.store(static_cast<unsigned>(ties_.size()),
                         std::memory_order_release);
    }
}

void Logger::untie(Logger& other) {
    std::lock_guard<std::mutex> lk(tie_mu_);
    auto it = std::find(ties_.begin(), ties_.end(), &other);
    if (it != ties_.end()) {
        ties_.erase(it);
        tie_count_.store(static_cast<unsigned>(ties_.size()),
                         std::memory_order_release);
    }
}

std::size_t Logger::tied_count() const noexcept {
    std::lock_guard<std::mutex> lk(tie_mu_);
    return ties_.size();
}

void Logger::emit(Level lvl, const iovec* iov, int n) {
    if (tie_count_.load(std::memory_order_acquire) == 0) {
        for (auto& s : sinks_) {
            s->write(lvl, iov, n);
        }
        return;
    }

    VisitGuard g(this);
    if (!g.inserted) return;

    for (auto& s : sinks_) {
        s->write(lvl, iov, n);
    }

    std::vector<Logger*> snapshot;
    {
        std::lock_guard<std::mutex> lk(tie_mu_);
        snapshot = ties_;
    }
    for (Logger* t : snapshot) {
        if (t) t->emit(lvl, iov, n);
    }
}

void Logger::flush() {
    for (auto& s : sinks_) {
        s->flush();
    }
    std::vector<Logger*> snapshot;
    {
        std::lock_guard<std::mutex> lk(tie_mu_);
        snapshot = ties_;
    }
    VisitGuard g(this);
    if (!g.inserted) return;
    for (Logger* t : snapshot) {
        if (t) t->flush();
    }
}

}  // namespace elog
