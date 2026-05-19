#include "elog/elog.hpp"

#include <string>

int main() {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::DEBUG);

    LOG_INFO_F("user {} logged in from {}", std::string("alice"), "10.0.0.1");
    LOG_DEBUG_F("token = {}", elog::quoted("abc123"));
    LOG_WARN_F("retry {}/{} after {}ms", 3, 5, 250);

    int x = 0xCAFE;
    LOG_INFO_F("x = {} (hex={}, bin={})", x, elog::hex(x), elog::bin(x));
    LOG_INFO_F("pi = {}", elog::fixed(3.14159, 2));

    return 0;
}
