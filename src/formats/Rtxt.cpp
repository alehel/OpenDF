#include "Rtxt.h"

#include <cstring>

namespace df {

bool parseRtxt(const std::vector<uint8_t>& d, std::vector<std::string>& out) {
    out.clear();
    if (d.size() < 16 || std::memcmp(d.data(), "RTXT", 4) != 0) return false;
    auto u32 = [&](size_t o) { return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24; };
    const uint32_t count = u32(12);
    const size_t base = 16 + size_t(count) * 16;
    if (count > 100000 || base > d.size()) return false;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        size_t o = base + u32(16 + size_t(i) * 16);
        std::string s;
        while (o < d.size() && d[o]) s.push_back(char(d[o++]));
        out.push_back(std::move(s));
    }
    return true;
}

} // namespace df
