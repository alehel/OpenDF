#include "Pcx.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

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

std::filesystem::path freeScreenshotPath(const std::filesystem::path& dir, bool gog) {
    // 0x473eb0: "sshot%0.3i.pcx" (0x4cceb0) probed from 0 while one exists
    // (0x498cb0), up to 998; CD 0x426afc: "screen%0.2i.pcx" up to 98.
    const int limit = gog ? 999 : 99;
    for (int i = 0; i < limit; ++i) {
        char name[32];
        if (gog) std::snprintf(name, sizeof name, "sshot%03d.pcx", i);
        else std::snprintf(name, sizeof name, "screen%02d.pcx", i);
        std::error_code ec;
        if (!std::filesystem::exists(dir / name, ec)) return dir / name;
    }
    return {};
}

bool writePcx24(const std::filesystem::path& file, int w, int h, const uint8_t* rgb) {
    std::ofstream f(file, std::ios::binary);
    if (!f || w <= 0 || h <= 0 || w > 0xFFFF || h > 0xFFFF) return false;
    // Header (0x473eb0): 0x0A, version 5, RLE 1, 8 bits; window 0, 0,
    // w - 1, h - 1; 75 x 75 dpi; a zero 48-byte palette, reserved 0;
    // 3 planes (1 for an 8-bit screen); bytes per line the width rounded
    // up to even; palette info 1; 58 zero bytes.
    const int bpl = (w + 1) & ~1;
    uint8_t hdr[128] = {0x0A, 5, 1, 8};
    auto put16 = [&](int at, int v) { hdr[at] = uint8_t(v); hdr[at + 1] = uint8_t(v >> 8); };
    put16(8, w - 1);
    put16(10, h - 1);
    put16(12, 75);
    put16(14, 75);
    hdr[65] = 3;
    put16(66, bpl);
    put16(68, 1);
    f.write(reinterpret_cast<const char*>(hdr), sizeof hdr);
    // Each row: the red, green and blue planes `width` bytes apart (as
    // 0x473eb0 lays them out), encoded over 3 x bytes-per-line bytes
    // (INFERRED for an odd width: the EXE encodes 3 bytes past its planes;
    // OpenDF pads with 0). Runs of up to 63 as 0xC0 | n then the byte; a
    // single byte as itself, or 0xC1 first when its top two bits are set;
    // runs do not cross rows. No palette after a 24-bit image.
    const int len = 3 * bpl;
    std::vector<uint8_t> plane(static_cast<size_t>(len)), out;
    out.reserve(size_t(len) * 2);
    for (int y = 0; y < h; ++y) {
        std::fill(plane.begin(), plane.end(), uint8_t(0));
        const uint8_t* row = rgb + size_t(y) * size_t(w) * 3;
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) plane[size_t(c * w + x)] = row[size_t(x) * 3 + size_t(c)];
        out.clear();
        for (int at = 0; at < len;) {
            const uint8_t b = plane[size_t(at)];
            int n = 1;
            while (n < 63 && at + n < len && plane[size_t(at + n)] == b) ++n;
            if (n > 1) out.push_back(uint8_t(0xC0 | n));
            else if ((b & 0xC0) == 0xC0) out.push_back(0xC1);
            out.push_back(b);
            at += n;
        }
        f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    }
    return bool(f);
}

} // namespace df
