#include "elog/registry.hpp"
#include "elog/logger.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

TEST(Registry, GetLoggerCreatesAndCaches) {
    auto& a = elog::get_logger("regtest_a");
    auto& b = elog::get_logger("regtest_a");
    EXPECT_EQ(&a, &b);
    EXPECT_EQ(a.name(), "regtest_a");
    elog::remove_logger("regtest_a");
}

TEST(Registry, FindLoggerNullForUnknown) {
    EXPECT_EQ(elog::find_logger("regtest_unknown"), nullptr);
}

TEST(Registry, FindLoggerAfterCreate) {
    auto& a = elog::get_logger("regtest_findme");
    EXPECT_EQ(elog::find_logger("regtest_findme"), &a);
    elog::remove_logger("regtest_findme");
}

TEST(Registry, RemoveLogger) {
    elog::get_logger("regtest_removable");
    EXPECT_TRUE(elog::remove_logger("regtest_removable"));
    EXPECT_EQ(elog::find_logger("regtest_removable"), nullptr);
    EXPECT_FALSE(elog::remove_logger("regtest_removable"));
}

TEST(Registry, LoggerNamesContainsCreated) {
    elog::get_logger("regtest_listed");
    auto names = elog::logger_names();
    EXPECT_NE(std::find(names.begin(), names.end(), "regtest_listed"), names.end());
    elog::remove_logger("regtest_listed");
}

TEST(Registry, DefaultLoggerAliased) {
    auto& d = elog::default_logger();
    auto* found = elog::find_logger("default");
    EXPECT_EQ(found, &d);
}

TEST(Registry, IndependentLevels) {
    auto& a = elog::get_logger("regtest_lvl_a");
    auto& b = elog::get_logger("regtest_lvl_b");
    a.set_level(elog::Level::TRACE);
    b.set_level(elog::Level::ERROR);
    EXPECT_TRUE(a.enabled(elog::Level::DEBUG));
    EXPECT_FALSE(b.enabled(elog::Level::DEBUG));
    elog::remove_logger("regtest_lvl_a");
    elog::remove_logger("regtest_lvl_b");
}
