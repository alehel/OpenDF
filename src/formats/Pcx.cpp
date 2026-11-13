#include "Pcx.h"

namespace df {

bool decodePcx(const std::vector<uint8_t>& f, IndexedImage& out) {
    if (f.size() < 128 || f[0] != 0x0A || f[3] != 8 || f[65] != 1) return false;
    auto u16 = [&](size_t o) { return int(f[o] | (f[o + 1] << 8)); };
    const int w = u16(8) - u16(4) + 1;
    const int h = u16(10) - u16(6) + 1;
    const int bytesPerLine = u16(66);
    if (w <= 0 || h <= 0 || bytesPerLine < w) return false;

    out.width = w;
    out.height = h;
    out.pixels.assign(size_t(w) * h, 0);

    // RLE runs may cross scanline boundaries, so decode the whole stream first.
    std::vector<uint8_t> line(size_t(bytesPerLine) * h);
    size_t src = 128, dst = 0;
    const size_t end = f.size();
    while (dst < line.size() && src < end) {
        uint8_t c = f[src++];
        if ((c & 0xC0) == 0xC0) {
            int run = c & 0x3F;
            if (src >= end) break;
            uint8_t v = f[src++];
            while (run-- && dst < line.size()) line[dst++] = v;
        } else {
            line[dst++] = c;
        }
    }
    for (int y = 0; y < h; ++y)
        std::copy_n(line.begin() + size_t(y) * bytesPerLine, w, out.pixels.begin() + size_t(y) * w);

    out.hasPalette = f.size() >= 769 && f[f.size() - 769] == 0x0C;
    if (out.hasPalette) {
        const uint8_t* p = f.data() + f.size() - 768;
        for (int i = 0; i < 256; ++i) out.palette[i] = {p[i * 3], p[i * 3 + 1], p[i * 3 + 2]};
    }
    return true;
}

} // namespace df
