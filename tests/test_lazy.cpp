#include <gtest/gtest.h>

#include "elog/elog.hpp"

namespace {

int g_counter_calls = 0;

int counter() {
    return ++g_counter_calls;
}

}  // namespace

TEST(Lazy, NoEvalWhenLevelOff) {
    auto& L = elog::default_logger();
    auto saved = L.level();
    L.set_level(elog::Level::OFF);
    g_counter_calls = 0;
    for (int i = 0; i < 100; ++i) {
        LOG_INFO_F("{}", counter());
    }
    EXPECT_EQ(g_counter_calls, 0);
    L.set_level(saved);
}

TEST(Lazy, NoEvalWhenLevelHigher) {
    auto& L = elog::default_logger();
    auto saved = L.level();
    L.set_level(elog::Level::ERROR);
    g_counter_calls = 0;
    for (int i = 0; i < 50; ++i) {
        LOG_INFO_F("{}", counter());
    }
    EXPECT_EQ(g_counter_calls, 0);
    L.set_level(saved);
}
