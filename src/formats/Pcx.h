// 8-bit ZSoft PCX images (all textures, terrain maps and palettes in Delta Force).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace df {

struct Rgb { uint8_t r, g, b; };

struct IndexedImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;       // width*height palette indices, row-major
    std::array<Rgb, 256> palette{};    // trailing VGA palette (zeros if absent)
    bool hasPalette = false;
};

bool decodePcx(const std::vector<uint8_t>& file, IndexedImage& out);

} // namespace df
