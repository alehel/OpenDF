// 8-bit ZSoft PCX images (all textures, terrain maps and palettes in Delta Force).
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
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

// The PrintScreen command (GOG Df.exe 0x473eb0, CD 0x426afc): the first
// free sshot000..998.pcx (GOG) / screen00..98.pcx (CD) in `dir` (nothing
// when all exist; empty path), written as a 3-plane 24-bit PCX from
// `rgb` (width * height * 3 bytes, R G B, top row first).
std::filesystem::path freeScreenshotPath(const std::filesystem::path& dir, bool gog);
bool writePcx24(const std::filesystem::path& file, int width, int height, const uint8_t* rgb);

} // namespace df
