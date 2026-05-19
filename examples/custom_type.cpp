#include "elog/elog.hpp"
#include "elog/render_extra.hpp"

#include <string>
#include <vector>

namespace geom {
struct Vec3 {
    double x, y, z;
};

inline elog::Iov elog_render(char* scratch, std::size_t& pos, const Vec3& v) noexcept {
    using elog::elog_render;
    std::size_t start = pos;
    if (pos < 4096) scratch[pos++] = '<';
    elog::Iov a = elog_render(scratch, pos, elog::fixed(v.x, 2));
    (void)a;
    if (pos + 1 < 4096) { scratch[pos++] = ','; scratch[pos++] = ' '; }
    elog::Iov b = elog_render(scratch, pos, elog::fixed(v.y, 2));
    (void)b;
    if (pos + 1 < 4096) { scratch[pos++] = ','; scratch[pos++] = ' '; }
    elog::Iov c = elog_render(scratch, pos, elog::fixed(v.z, 2));
    (void)c;
    if (pos < 4096) scratch[pos++] = '>';
    return elog::Iov{scratch + start, pos - start};
}
}  // namespace geom

int main() {
    auto& L = elog::default_logger();
    L.set_level(elog::Level::DEBUG);

    geom::Vec3 v{1.5, 2.5, 3.5};
    LOG_INFO_F("position = {}", v);

    std::vector<int> ids = {3, 1, 4, 1, 5, 9};
    LOG_INFO_F("ids = {}", ids);

    return 0;
}
