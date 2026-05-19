#include "elog/elog.hpp"
#include "elog/render_extra.hpp"

#include <string>
#include <vector>

// ---- Pattern 1: numeric custom type, single-Iov return ----
// All members must be rendered to text anyway, so the natural pattern is
// the standard ADL elog_render returning a single contiguous Iov in scratch.
namespace geom {
struct Vec3 {
    double x, y, z;
};

inline elog::Iov elog_render(char* scratch, std::size_t& pos, const Vec3& v) noexcept {
    using elog::elog_render;
    std::size_t start = pos;
    if (pos < 4096) scratch[pos++] = '<';
    (void)elog_render(scratch, pos, elog::fixed(v.x, 2));
    if (pos + 1 < 4096) { scratch[pos++] = ','; scratch[pos++] = ' '; }
    (void)elog_render(scratch, pos, elog::fixed(v.y, 2));
    if (pos + 1 < 4096) { scratch[pos++] = ','; scratch[pos++] = ' '; }
    (void)elog_render(scratch, pos, elog::fixed(v.z, 2));
    if (pos < 4096) scratch[pos++] = '>';
    return elog::Iov{scratch + start, pos - start};
}
}  // namespace geom

// ---- Pattern 2: string-bearing custom type, zero-copy via iov_pack return ----
// elog_render returns iov_pack instead of Iov; ELog detects this at compile
// time (SFINAE) and routes the type through the multi-iov path. Each string
// member flows through writev as a borrowed iovec entry — no memcpy, no
// intermediate buffer.
namespace audit {
struct UserRecord {
    std::string name;
    std::string ip;
};

inline elog::iov_pack elog_render(char* /*scratch*/, std::size_t& /*pos*/,
                                  const UserRecord& r) noexcept {
    // Allocate from emit_f's per-call iov scratch. The returned pointer is
    // stable for the entire LOG call, and each renderer invocation gets its
    // own range — so logging two UserRecords in the same LOG is safe.
    elog::Iov* buf = elog::iov_scratch_alloc(4);
    buf[0] = {"name=", 5};
    buf[1] = {r.name.data(), r.name.size()};       // borrowed
    buf[2] = {" ip=", 4};
    buf[3] = {r.ip.data(), r.ip.size()};           // borrowed
    return elog::iov_pack(buf, 4);
}
}  // namespace audit

int main() {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::DEBUG);

    // Numeric custom type — renders into scratch.
    geom::Vec3 v{1.5, 2.5, 3.5};
    LOG_INFO_F("position = {}", v);

    // Container — single-Iov path is fine for primitives.
    std::vector<int> ids = {3, 1, 4, 1, 5, 9};
    LOG_INFO_F("ids = {}", ids);

    // String-bearing custom type — pass it directly. elog_render returns
    // iov_pack; the strings stay zero-copy all the way to writev.
    audit::UserRecord user{"alice", "10.0.0.1"};
    LOG_INFO_F("user: {}", user);

    // Two instances of the same type in one LOG also work — each render
    // gets its own range out of emit_f's scratch.
    audit::UserRecord admin{"admin", "10.0.0.2"};
    LOG_INFO_F("from {} to {}", user, admin);

    return 0;
}
