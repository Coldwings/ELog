#include <gtest/gtest.h>

#include "elog/elog.hpp"

#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    return s;
}

}  // namespace

TEST(Logger, FileSinkBasic) {
    char tmpl[] = "/tmp/elog_test_XXXXXX";
    int fd = ::mkstemp(tmpl);
    ASSERT_GE(fd, 0);
    ::close(fd);
    std::string path = tmpl;

    elog::Logger L("test");
    L.set_level(elog::Level::INFO);
    L.add_sink(elog::make_file_sink(path.c_str()));

    LOGGER_F(L, ::elog::Level::INFO, "hello {}", std::string("world"));
    L.flush();

    std::string out = read_file(path);
    ::unlink(path.c_str());

    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out.back(), '\n');
    EXPECT_NE(out.find("INFO"), std::string::npos);
    EXPECT_NE(out.find("hello world"), std::string::npos);
    EXPECT_NE(out.find(" | "), std::string::npos);
}

TEST(Logger, StderrPipe) {
    int saved = ::dup(2);
    ASSERT_GE(saved, 0);

    int pipefd[2];
    ASSERT_EQ(::pipe(pipefd), 0);

    int flags = ::fcntl(pipefd[0], F_GETFL, 0);
    ::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    ASSERT_GE(::dup2(pipefd[1], 2), 0);
    ::close(pipefd[1]);

    auto& L = elog::default_logger();
    auto savedlvl = L.level();
    L.set_level(elog::Level::INFO);

    LOG_INFO_F("hello {}", std::string("world"));
    L.flush();

    ::fsync(2);
    ::dup2(saved, 2);
    ::close(saved);

    char buf[4096];
    ssize_t n = ::read(pipefd[0], buf, sizeof(buf));
    ::close(pipefd[0]);

    L.set_level(savedlvl);

    ASSERT_GT(n, 0);
    std::string out(buf, static_cast<std::size_t>(n));

    EXPECT_EQ(out.back(), '\n');
    EXPECT_NE(out.find("hello world"), std::string::npos);
    EXPECT_NE(out.find(" | "), std::string::npos);
}
