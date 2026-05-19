#include "elog/scratch.hpp"

namespace elog {

char* tls_scratch() noexcept {
    static thread_local char buf[kScratchSize];
    return buf;
}

}  // namespace elog
