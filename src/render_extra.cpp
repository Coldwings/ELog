#include "elog/render_extra.hpp"

#include <cstdio>

namespace elog {

namespace {

constexpr std::size_t kScratchCap = 4096;

inline std::size_t bound_avail(std::size_t pos) noexcept {
    return pos >= kScratchCap ? 0u : (kScratchCap - pos);
}

}  // namespace

Iov elog_render(char* scratch, std::size_t& pos, oct v) noexcept {
    std::size_t start = pos;
    char tmp[24];
    int n = std::snprintf(tmp, sizeof(tmp), "%llo",
                          static_cast<unsigned long long>(v.v));
    if (n < 0) n = 0;
    std::size_t want = static_cast<std::size_t>(n);
    std::size_t avail = bound_avail(pos);
    if (want > avail) want = avail;
    for (std::size_t i = 0; i < want; ++i) scratch[pos + i] = tmp[i];
    pos += want;
    return Iov{scratch + start, pos - start};
}

Iov elog_render(char* scratch, std::size_t& pos, hexdump v) noexcept {
    static const char hexd[] = "0123456789abcdef";
    std::size_t start = pos;
    auto* p = static_cast<const unsigned char*>(v.p);
    constexpr std::size_t kCap = 64;
    std::size_t n = v.n;
    bool truncated = false;
    if (n > kCap) { n = kCap; truncated = true; }
    for (std::size_t i = 0; i < n; ++i) {
        if (bound_avail(pos) < 3) break;
        if (i > 0) scratch[pos++] = ' ';
        scratch[pos++] = hexd[(p[i] >> 4) & 0xF];
        scratch[pos++] = hexd[p[i] & 0xF];
    }
    if (truncated) {
        const char* sfx = " ...";
        for (std::size_t i = 0; i < 4 && bound_avail(pos) > 0; ++i) {
            scratch[pos++] = sfx[i];
        }
    }
    return Iov{scratch + start, pos - start};
}

Iov elog_render(char* scratch, std::size_t& pos, escaped v) noexcept {
    static const char hexd[] = "0123456789abcdef";
    std::size_t start = pos;
    const char* s = v.s.data();
    std::size_t n = v.s.size();
    for (std::size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t avail = bound_avail(pos);
        if (avail == 0) break;
        switch (c) {
            case '\\': if (avail >= 2) { scratch[pos++] = '\\'; scratch[pos++] = '\\'; } break;
            case '"':  if (avail >= 2) { scratch[pos++] = '\\'; scratch[pos++] = '"'; } break;
            case '\n': if (avail >= 2) { scratch[pos++] = '\\'; scratch[pos++] = 'n'; } break;
            case '\r': if (avail >= 2) { scratch[pos++] = '\\'; scratch[pos++] = 'r'; } break;
            case '\t': if (avail >= 2) { scratch[pos++] = '\\'; scratch[pos++] = 't'; } break;
            case '\0': if (avail >= 2) { scratch[pos++] = '\\'; scratch[pos++] = '0'; } break;
            default:
                if (c >= 0x20 && c < 0x7F) {
                    scratch[pos++] = static_cast<char>(c);
                } else if (avail >= 4) {
                    scratch[pos++] = '\\';
                    scratch[pos++] = 'x';
                    scratch[pos++] = hexd[(c >> 4) & 0xF];
                    scratch[pos++] = hexd[c & 0xF];
                }
                break;
        }
    }
    return Iov{scratch + start, pos - start};
}

}  // namespace elog
