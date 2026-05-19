#pragma once

#include <cstddef>

namespace elog {
namespace detail {

// Writes shortest round-trip decimal of v into out (no nul). Supports
// finite normal/subnormal, zero, +/-inf, nan. Worst-case 25 chars.
// Returns number of chars written.
std::size_t format_double(char* out, double v) noexcept;

}  // namespace detail
}  // namespace elog
