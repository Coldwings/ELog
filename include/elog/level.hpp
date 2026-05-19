#pragma once

#include <cstdint>

namespace elog {

enum class Level : std::uint8_t {
    TRACE = 0,
    DEBUG = 1,
    INFO  = 2,
    WARN  = 3,
    ERROR = 4,
    FATAL = 5,
    OFF   = 6,
};

const char* level_name(Level lvl) noexcept;
const char* level_color(Level lvl) noexcept;
const char* level_color_reset() noexcept;

}  // namespace elog
