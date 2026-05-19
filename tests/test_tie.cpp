#include "elog/elog.hpp"
#include "elog/sink.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <sys/uio.h>
#include <vector>

namespace {

class CountingSink : public elog::Sink {
public:
    std::atomic<int> count{0};
    std::mutex mu;
    std::vector<std::string> lines;

    void write(elog::Level, const iovec* iov, int n) override {
        std::string buf;
        for (int i = 0; i < n; ++i) {
            buf.append(static_cast<const char*>(iov[i].iov_base), iov[i].iov_len);
        }
        std::lock_guard<std::mutex> lk(mu);
        ++count;
        lines.push_back(std::move(buf));
    }
};

struct SinkProbe {
    CountingSink* raw;
    std::unique_ptr<elog::Sink> owned;
};

SinkProbe make_probe() {
    auto s = std::unique_ptr<CountingSink>(new CountingSink());
    SinkProbe p;
    p.raw = s.get();
    p.owned = std::move(s);
    return p;
}

void emit_one(elog::Logger& L) {
    const char body[] = "hello\n";
    iovec iov;
    iov.iov_base = const_cast<char*>(body);
    iov.iov_len = sizeof(body) - 1;
    L.emit(elog::Level::INFO, &iov, 1);
}

}  // namespace

TEST(Tie, OneLevelFanOut) {
    elog::Logger A("A_tie1"), B("B_tie1");
    auto pa = make_probe(); auto pb = make_probe();
    A.add_sink(std::move(pa.owned));
    B.add_sink(std::move(pb.owned));
    A.tie(B);
    emit_one(A);
    EXPECT_EQ(pa.raw->count.load(), 1);
    EXPECT_EQ(pb.raw->count.load(), 1);
}

TEST(Tie, MultiLevelFanOut) {
    elog::Logger A("A_tie2"), B("B_tie2"), C("C_tie2");
    auto pa = make_probe(); auto pb = make_probe(); auto pc = make_probe();
    A.add_sink(std::move(pa.owned));
    B.add_sink(std::move(pb.owned));
    C.add_sink(std::move(pc.owned));
    A.tie(B);
    B.tie(C);
    emit_one(A);
    EXPECT_EQ(pa.raw->count.load(), 1);
    EXPECT_EQ(pb.raw->count.load(), 1);
    EXPECT_EQ(pc.raw->count.load(), 1);
}

TEST(Tie, CycleNoInfiniteRecursion) {
    elog::Logger A("A_tie3"), B("B_tie3");
    auto pa = make_probe(); auto pb = make_probe();
    A.add_sink(std::move(pa.owned));
    B.add_sink(std::move(pb.owned));
    A.tie(B);
    B.tie(A);
    emit_one(A);
    EXPECT_EQ(pa.raw->count.load(), 1);
    EXPECT_EQ(pb.raw->count.load(), 1);
}

TEST(Tie, SelfTieIsNoOp) {
    elog::Logger A("A_tie4");
    auto pa = make_probe();
    A.add_sink(std::move(pa.owned));
    A.tie(A);
    EXPECT_EQ(A.tied_count(), 0u);
    emit_one(A);
    EXPECT_EQ(pa.raw->count.load(), 1);
}

TEST(Tie, DuplicateTieDeduped) {
    elog::Logger A("A_tie5"), B("B_tie5");
    auto pa = make_probe(); auto pb = make_probe();
    A.add_sink(std::move(pa.owned));
    B.add_sink(std::move(pb.owned));
    A.tie(B);
    A.tie(B);
    EXPECT_EQ(A.tied_count(), 1u);
    emit_one(A);
    EXPECT_EQ(pb.raw->count.load(), 1);
}

TEST(Tie, UntieRemovesForwarding) {
    elog::Logger A("A_tie6"), B("B_tie6");
    auto pa = make_probe(); auto pb = make_probe();
    A.add_sink(std::move(pa.owned));
    B.add_sink(std::move(pb.owned));
    A.tie(B);
    A.untie(B);
    EXPECT_EQ(A.tied_count(), 0u);
    emit_one(A);
    EXPECT_EQ(pa.raw->count.load(), 1);
    EXPECT_EQ(pb.raw->count.load(), 0);
}

TEST(Tie, FanOutIgnoresTiedLevel) {
    elog::Logger A("A_tie7"), B("B_tie7");
    auto pa = make_probe(); auto pb = make_probe();
    A.add_sink(std::move(pa.owned));
    B.add_sink(std::move(pb.owned));
    A.set_level(elog::Level::TRACE);
    B.set_level(elog::Level::FATAL);
    A.tie(B);
    iovec iov;
    const char body[] = "x\n";
    iov.iov_base = const_cast<char*>(body);
    iov.iov_len = sizeof(body) - 1;
    A.emit(elog::Level::DEBUG, &iov, 1);
    EXPECT_EQ(pa.raw->count.load(), 1);
    EXPECT_EQ(pb.raw->count.load(), 1);
}
