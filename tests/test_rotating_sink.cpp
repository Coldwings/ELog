#include <gtest/gtest.h>

#include "elog/level.hpp"
#include "elog/rotating_sink.hpp"
#include "elog/sink.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

// --- temp dir helpers -------------------------------------------------------

std::string make_tmp_dir() {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "/tmp/elog_test_M5_%d_%ld",
                  static_cast<int>(::getpid()),
                  static_cast<long>(::time(nullptr)));
    std::string dir = buf;
    // Best-effort: tolerate either fresh or pre-existing.
    ::mkdir(dir.c_str(), 0755);
    return dir;
}

void rm_dir_recursive(const std::string& dir) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;
    while (auto* ent = ::readdir(d)) {
        if (!std::strcmp(ent->d_name, ".") || !std::strcmp(ent->d_name, ".."))
            continue;
        std::string p = dir + "/" + ent->d_name;
        ::unlink(p.c_str());
    }
    ::closedir(d);
    ::rmdir(dir.c_str());
}

bool path_exists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

off_t file_size(const std::string& p) {
    struct stat st;
    if (::stat(p.c_str(), &st) != 0) return -1;
    return st.st_size;
}

std::string read_all(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream os;
    os << f.rdbuf();
    return os.str();
}

// Convenience: write a single string as one writev-style msg through the sink.
void put(elog::Sink& s, const std::string& msg) {
    iovec iov;
    iov.iov_base = const_cast<char*>(msg.data());
    iov.iov_len = msg.size();
    s.write(elog::Level::INFO, &iov, 1);
}

class RotatingSinkTest : public ::testing::Test {
protected:
    void SetUp() override { dir_ = make_tmp_dir(); }
    void TearDown() override { rm_dir_recursive(dir_); }
    std::string base() const { return dir_ + "/app.log"; }
    std::string indexed(std::size_t i) const {
        return dir_ + "/app." + std::to_string(i) + ".log";
    }
    std::string dir_;
};

}  // namespace

// 1) Single-thread: 100 messages of ~10 bytes, max_bytes=100, max_files=3.
//    After ~30 messages we should have rotated; verify all three files exist.
TEST_F(RotatingSinkTest, SingleThreadRotatesAndKeepsChain) {
    auto sink = elog::make_rotating_file_sink(base().c_str(),
                                              /*max_bytes=*/100,
                                              /*max_files=*/3);
    ASSERT_NE(sink, nullptr);

    // 100 messages of exactly 10 bytes each = 1000 bytes total.
    // With max_bytes=100 we expect ~10 rotations; chain caps at 3.
    for (int i = 0; i < 100; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "msg=%04d\n", i);  // 9 chars + '\n' = 10
        put(*sink, buf);
    }
    sink->flush();

    EXPECT_TRUE(path_exists(base())) << "active file missing";
    EXPECT_TRUE(path_exists(indexed(1))) << "rotated .1 missing";
    EXPECT_TRUE(path_exists(indexed(2))) << "rotated .2 missing";

    EXPECT_GE(file_size(base()), 0);
    EXPECT_LE(file_size(base()), 200);  // up to one rotation overshoot
    EXPECT_LE(file_size(indexed(1)), 200);
    EXPECT_LE(file_size(indexed(2)), 200);
}

// 2) Chain caps at max_files: rotate >5 times with max_files=3; assert
//    no app.3.log / app.4.log exist.
TEST_F(RotatingSinkTest, ChainCapsAtMaxFiles) {
    auto sink = elog::make_rotating_file_sink(base().c_str(),
                                              /*max_bytes=*/40,
                                              /*max_files=*/3);
    ASSERT_NE(sink, nullptr);

    // 40 bytes per message ensures one rotation per write; do many writes.
    std::string msg(40, 'x');
    msg.back() = '\n';
    for (int i = 0; i < 30; ++i) {
        put(*sink, msg);
    }
    sink->flush();

    EXPECT_TRUE(path_exists(base()));
    EXPECT_TRUE(path_exists(indexed(1)));
    EXPECT_TRUE(path_exists(indexed(2)));
    EXPECT_FALSE(path_exists(indexed(3))) << "chain leaked past max_files";
    EXPECT_FALSE(path_exists(indexed(4)));
    EXPECT_FALSE(path_exists(indexed(5)));
}

// 3) Multi-threaded: 4 threads x 1000 messages, max_bytes set so we get
//    many rotations; verify all messages are present somewhere in the
//    surviving rotation chain (or were evicted by older rotations).
//    Because old rotations may be evicted, we verify a strict subset
//    invariant: any line present is well-formed, and across all kept
//    files the count is >= total - (evicted lines). To make the test
//    deterministic, we use max_files large enough that nothing is
//    evicted.
TEST_F(RotatingSinkTest, MultiThreadedNoLossWhenChainLargeEnough) {
    constexpr int kThreads = 4;
    constexpr int kPerThread = 1000;
    constexpr std::size_t kMaxBytes = 4096;          // ~ rotate every ~250 lines
    constexpr std::size_t kMaxFiles = 64;            // big enough to keep all lines

    auto sink = elog::make_rotating_file_sink(base().c_str(),
                                              kMaxBytes, kMaxFiles);
    ASSERT_NE(sink, nullptr);

    std::atomic<bool> go{false};
    std::vector<std::thread> ts;
    ts.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t]() {
            while (!go.load(std::memory_order_acquire)) { /* spin */ }
            for (int i = 0; i < kPerThread; ++i) {
                char buf[32];
                int n = std::snprintf(buf, sizeof(buf), "t=%d i=%04d\n", t, i);
                iovec iov;
                iov.iov_base = buf;
                iov.iov_len = static_cast<std::size_t>(n);
                sink->write(elog::Level::INFO, &iov, 1);
            }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& th : ts) th.join();
    sink->flush();

    // Collect all lines from app.log + every app.N.log that exists.
    std::vector<std::string> all;
    auto consume = [&](const std::string& p) {
        if (!path_exists(p)) return;
        std::string body = read_all(p);
        std::size_t s = 0;
        while (s < body.size()) {
            std::size_t eol = body.find('\n', s);
            if (eol == std::string::npos) {
                all.push_back(body.substr(s));
                break;
            }
            all.push_back(body.substr(s, eol - s));
            s = eol + 1;
        }
    };
    consume(base());
    for (std::size_t i = 1; i < kMaxFiles; ++i) consume(indexed(i));

    EXPECT_EQ(all.size(),
              static_cast<std::size_t>(kThreads * kPerThread))
        << "lost or duplicated lines";

    std::set<std::string> uniq(all.begin(), all.end());
    EXPECT_EQ(uniq.size(), all.size())
        << "duplicate lines (rotation reopened wrong file?)";

    // Every (t,i) pair must appear exactly once.
    std::set<std::string> expected;
    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kPerThread; ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "t=%d i=%04d", t, i);
            expected.insert(buf);
        }
    }
    EXPECT_EQ(uniq, expected) << "missing or unexpected (thread,iter) pairs";
}

// 4) Persistent state across rotations: after rotation, app.log size
//    drops back to roughly the size of the message that triggered the
//    next epoch. We force exactly one rotation and assert the new
//    app.log is small.
TEST_F(RotatingSinkTest, ActiveFileResetsToZeroAfterRotation) {
    auto sink = elog::make_rotating_file_sink(base().c_str(),
                                              /*max_bytes=*/50,
                                              /*max_files=*/3);
    ASSERT_NE(sink, nullptr);

    // Six 10-byte messages = 60 bytes -> one rotation after the 5th.
    for (int i = 0; i < 5; ++i) {
        put(*sink, "0123456789");
    }
    sink->flush();

    // After 5 writes (50 bytes) the cumulative reaches max_bytes and we
    // rotate. So app.log should now be empty, and app.1.log should hold
    // the prior 50 bytes.
    EXPECT_TRUE(path_exists(indexed(1)));
    EXPECT_EQ(file_size(indexed(1)), 50);
    EXPECT_EQ(file_size(base()), 0);

    // One more write goes into the fresh app.log.
    put(*sink, "0123456789");
    sink->flush();
    EXPECT_EQ(file_size(base()), 10);
    EXPECT_EQ(file_size(indexed(1)), 50);
}

// Bonus: max_files == 1 truncates instead of renaming.
TEST_F(RotatingSinkTest, MaxFilesOneTruncates) {
    auto sink = elog::make_rotating_file_sink(base().c_str(),
                                              /*max_bytes=*/30,
                                              /*max_files=*/1);
    ASSERT_NE(sink, nullptr);

    for (int i = 0; i < 10; ++i) {
        put(*sink, "0123456789");  // 10 bytes each
    }
    sink->flush();

    EXPECT_TRUE(path_exists(base()));
    EXPECT_FALSE(path_exists(indexed(1)));
    EXPECT_FALSE(path_exists(indexed(2)));
    EXPECT_LE(file_size(base()), 30);
}
