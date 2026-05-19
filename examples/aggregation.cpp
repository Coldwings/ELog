#include "elog/elog.hpp"

#include <thread>
#include <chrono>

int main() {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::DEBUG);

    for (int i = 0; i < 1000; ++i) {
        LOG_INFO_EVERY_N(100, "every-100th: i = {}", i);
    }

    for (int i = 0; i < 100; ++i) {
        LOG_INFO_FIRST_N(3, "first 3 only: i = {}", i);
    }

    for (int i = 0; i < 10; ++i) {
        LOG_INFO_ONCE("this prints once even in a loop ({})", i);
    }

    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(250)) {
        LOG_INFO_EVERY_N_MS(50, "throttled to 50ms cadence");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    for (int i = 0; i < 100; ++i) {
        LOG_INFO_DEDUP("repeated message");
    }
    L.flush();
    elog::flush_dedup();

    return 0;
}
