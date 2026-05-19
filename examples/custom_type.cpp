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

// ---- Pattern 2: string-bearing custom type, zero-copy via iov_pack ----
// When a record contains std::string / string_ref members, building a one-off
// iov_pack lets each string flow through writev as a borrowed iovec entry —
// no memcpy into scratch.
namespace audit {
struct UserRecord {
    std::string name;
    std::string ip;

    // Build an iov_pack pointing at our string members. The returned pack
    // borrows from `pieces` and from `*this`; both must outlive the LOG call.
    // The simplest pattern is to construct on the stack right at the call site.
    void as_iov(std::vector<elog::Iov>& pieces) const {
        pieces.push_back({"name=", 5});
        pieces.push_back({name.data(), name.size()});       // borrowed
        pieces.push_back({" ip=", 4});
        pieces.push_back({ip.data(), ip.size()});           // borrowed
    }
};
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

    // String-bearing custom type via iov_pack — zero-copy on name and ip.
    audit::UserRecord user{"alice", "10.0.0.1"};
    std::vector<elog::Iov> pieces;
    pieces.reserve(8);
    user.as_iov(pieces);
    LOG_INFO_F("user: {}", elog::iov_pack(pieces));

    return 0;
}
