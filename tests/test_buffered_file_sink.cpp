#include "elog/buffered_file_sink.hpp"
#include "elog/elog.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

namespace {

std::string read_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

off_t file_size(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return -1;
    return st.st_size;
}

std::string tmp_path(const char* tag) {
    return std::string("/tmp/elog_buftest_") + tag + "_" +
           std::to_string(::getpid()) + ".log";
}

iovec make_iov(const char* s) {
    return iovec{const_cast<char*>(s), std::strlen(s)};
}

}  // namespace

TEST(BufferedFileSink, BatchesInfoUntilExplicitFlush) {
    auto path = tmp_path("batch_info");
    ::unlink(path.c_str());
    {
        auto sink = elog::make_buffered_file_sink(path.c_str(),
            /*buffer=*/4096, /*threshold=*/elog::Level::WARN);
        ASSERT_NE(sink, nullptr);

        for (int i = 0; i < 5; ++i) {
            iovec iov = make_iov("hello\n");
            sink->write(elog::Level::INFO, &iov, 1);
        }
        // No flush yet — buffer holds it; file is empty on disk.
        EXPECT_EQ(file_size(path), 0);

        sink->flush();
        EXPECT_EQ(file_size(path), 30);
        EXPECT_EQ(read_all(path), "hello\nhello\nhello\nhello\nhello\n");
    }
    ::unlink(path.c_str());
}

TEST(BufferedFileSink, WarnFlushesImmediately) {
    auto path = tmp_path("warn_flush");
    ::unlink(path.c_str());
    {
        auto sink = elog::make_buffered_file_sink(path.c_str(),
            /*buffer=*/4096, /*threshold=*/elog::Level::WARN);

        iovec a = make_iov("info1\n");
        sink->write(elog::Level::INFO, &a, 1);
        EXPECT_EQ(file_size(path), 0);

        iovec b = make_iov("danger\n");
        sink->write(elog::Level::WARN, &b, 1);
        // WARN drained both buffered INFO and the WARN itself.
        EXPECT_EQ(file_size(path), 6 + 7);
        EXPECT_EQ(read_all(path), "info1\ndanger\n");
    }
    ::unlink(path.c_str());
}

TEST(BufferedFileSink, ErrorAndFatalAlsoFlush) {
    auto path = tmp_path("error_flush");
    ::unlink(path.c_str());
    {
        auto sink = elog::make_buffered_file_sink(path.c_str(), 4096,
            elog::Level::WARN);

        iovec i = make_iov("info\n");
        iovec e = make_iov("err\n");
        iovec f = make_iov("fatal\n");

        sink->write(elog::Level::INFO,  &i, 1);
        sink->write(elog::Level::ERROR, &e, 1);
        EXPECT_EQ(read_all(path), "info\nerr\n");

        sink->write(elog::Level::INFO,  &i, 1);
        sink->write(elog::Level::FATAL, &f, 1);
        EXPECT_EQ(read_all(path), "info\nerr\ninfo\nfatal\n");
    }
    ::unlink(path.c_str());
}

TEST(BufferedFileSink, BufferFullForcesFlush) {
    auto path = tmp_path("full");
    ::unlink(path.c_str());
    {
        auto sink = elog::make_buffered_file_sink(path.c_str(),
            /*buffer=*/16, /*threshold=*/elog::Level::WARN);

        iovec a = make_iov("aaaaa\n");  // 6 bytes
        sink->write(elog::Level::INFO, &a, 1);
        sink->write(elog::Level::INFO, &a, 1);
        EXPECT_EQ(file_size(path), 0);

        sink->write(elog::Level::INFO, &a, 1);  // would overflow 12+6=18>16
        // First two were drained, third stays in buffer.
        EXPECT_EQ(file_size(path), 12);

        sink->flush();
        EXPECT_EQ(file_size(path), 18);
    }
    ::unlink(path.c_str());
}

TEST(BufferedFileSink, EntryLargerThanBufferGoesDirect) {
    auto path = tmp_path("oversize");
    ::unlink(path.c_str());
    {
        auto sink = elog::make_buffered_file_sink(path.c_str(),
            /*buffer=*/8, /*threshold=*/elog::Level::WARN);

        // Pre-load some buffered data.
        iovec small = make_iov("hi\n");
        sink->write(elog::Level::INFO, &small, 1);
        EXPECT_EQ(file_size(path), 0);

        std::string big(64, 'x');
        big.push_back('\n');
        iovec b = make_iov(big.c_str());
        sink->write(elog::Level::INFO, &b, 1);
        // Big entry forced a flush of the small one, then went direct.
        EXPECT_EQ(file_size(path), static_cast<off_t>(3 + big.size()));
    }
    ::unlink(path.c_str());
}

TEST(BufferedFileSink, DestructorFlushes) {
    auto path = tmp_path("dtor");
    ::unlink(path.c_str());
    {
        auto sink = elog::make_buffered_file_sink(path.c_str(), 4096,
            elog::Level::WARN);
        iovec a = make_iov("survived\n");
        sink->write(elog::Level::INFO, &a, 1);
        EXPECT_EQ(file_size(path), 0);
    }  // dtor runs flush
    EXPECT_EQ(read_all(path), "survived\n");
    ::unlink(path.c_str());
}

TEST(BufferedFileSink, IntegratesWithLogger) {
    auto path = tmp_path("logger");
    ::unlink(path.c_str());
    {
        elog::Logger L("buftest");
        L.add_sink(elog::make_buffered_file_sink(path.c_str(), 4096,
            elog::Level::WARN));
        L.set_level(elog::Level::DEBUG);

        for (int i = 0; i < 100; ++i) {
            LOGGER_F(L, elog::Level::INFO, "info {}", i);
        }
        // Buffered, may or may not have started writing yet.

        LOGGER_F(L, elog::Level::ERROR, "boom {}", 42);
        // ERROR triggered drain; everything so far must be on disk.

        L.flush();
        std::string out = read_all(path);
        EXPECT_NE(out.find("info 0"), std::string::npos);
        EXPECT_NE(out.find("info 99"), std::string::npos);
        EXPECT_NE(out.find("boom 42"), std::string::npos);
    }
    ::unlink(path.c_str());
}
