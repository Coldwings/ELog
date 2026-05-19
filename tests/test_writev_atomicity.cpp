#include <gtest/gtest.h>

#include "elog/elog.hpp"

#include <atomic>
#include <fstream>
#include <regex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    return s;
}

}  // namespace

TEST(WritevAtomicity, ThreadsToFile) {
    char tmpl[] = "/tmp/elog_atomic_XXXXXX";
    int fd = ::mkstemp(tmpl);
    ASSERT_GE(fd, 0);
    ::close(fd);
    std::string path = tmpl;

    elog::Logger L("atomic");
    L.set_level(elog::Level::INFO);
    L.add_sink(elog::make_file_sink(path.c_str()));

    constexpr int kThreads = 8;
    constexpr int kIters = 1000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    std::atomic<bool> go{false};
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&L, &go, t]() {
            while (!go.load(std::memory_order_acquire)) { /* spin */ }
            for (int i = 0; i < kIters; ++i) {
                LOGGER_F(L, ::elog::Level::INFO, "thread {} iter {}", t, i);
            }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& th : threads) th.join();
    L.flush();

    std::string body = read_file(path);
    ::unlink(path.c_str());

    ASSERT_FALSE(body.empty());
    EXPECT_EQ(body.back(), '\n');

    int line_count = 0;
    std::vector<std::vector<int>> per_thread(kThreads);
    std::regex line_re(
        R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6} INFO \[\d+\] [^ ]+:\d+ \| thread (\d+) iter (\d+))");

    std::size_t start = 0;
    while (start < body.size()) {
        std::size_t eol = body.find('\n', start);
        ASSERT_NE(eol, std::string::npos) << "trailing data without newline at " << start;
        std::string line = body.substr(start, eol - start);
        std::smatch m;
        ASSERT_TRUE(std::regex_match(line, m, line_re))
            << "malformed line: [" << line << "]";
        int tid = std::stoi(m[1].str());
        int it = std::stoi(m[2].str());
        ASSERT_GE(tid, 0);
        ASSERT_LT(tid, kThreads);
        per_thread[tid].push_back(it);
        ++line_count;
        start = eol + 1;
    }

    EXPECT_EQ(line_count, kThreads * kIters);
    for (int t = 0; t < kThreads; ++t) {
        ASSERT_EQ(per_thread[t].size(), static_cast<std::size_t>(kIters))
            << "thread " << t << " missing lines";
        for (int i = 0; i < kIters; ++i) {
            EXPECT_EQ(per_thread[t][i], i) << "thread " << t << " out of order at " << i;
        }
    }
}
