#pragma once

#include <cstddef>

namespace elog {

struct Iov {
    const void* base;
    std::size_t len;
};

}  // namespace elog
