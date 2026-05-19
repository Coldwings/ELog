#pragma once

#include <cstddef>

namespace elog {

constexpr std::size_t kScratchSize = 4096;

char* tls_scratch() noexcept;

}  // namespace elog
