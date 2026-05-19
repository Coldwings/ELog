#include "elog/level.hpp"

namespace elog {

const char* level_name(Level lvl) noexcept {
    switch (lvl) {
        case Level::TRACE: return "TRACE";
        case Level::DEBUG: return "DEBUG";
        case Level::INFO:  return "INFO";
        case Level::WARN:  return "WARN";
        case Level::ERROR: return "ERROR";
        case Level::FATAL: return "FATAL";
        case Level::OFF:   return "OFF";
    }
    return "?";
}

const char* level_color(Level lvl) noexcept {
    switch (lvl) {
        case Level::TRACE: return "\x1b[37m";       // white
        case Level::DEBUG: return "\x1b[36m";       // cyan
        case Level::INFO:  return "\x1b[32m";       // green
        case Level::WARN:  return "\x1b[33m";       // yellow
        case Level::ERROR: return "\x1b[31m";       // red
        case Level::FATAL: return "\x1b[1;31m";     // bold red
        case Level::OFF:   return "";
    }
    return "";
}

const char* level_color_reset() noexcept {
    return "\x1b[0m";
}

}  // namespace elog
