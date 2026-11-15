// True-colour images used by the later Delta Force games (docs/OTHER-GAMES.md):
// TGA (uncompressed, 24/32-bit) and JPEG (through libjpeg when OpenDF was
// built with it).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace df {

struct RgbaImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;   // width*height*4, rows top-down
};

bool decodeTga(const std::vector<uint8_t>& file, RgbaImage& out);
bool decodeJpeg(const std::vector<uint8_t>& file, RgbaImage& out);
// By the file's own signature (JPEG FF D8, else TGA).
bool decodeRgbaImage(const std::vector<uint8_t>& file, RgbaImage& out);
// Delta Force 2's own JPEG decoder (GOG Df2.exe 0x482310): its IDCT with the
// row-6 slip and nearest chroma. For the game's images (VBLIT_LOADJPG, the
// terrain colour maps); other formats and files as above.
bool decodeJpegDf2(const std::vector<uint8_t>& file, RgbaImage& out);
bool decodeRgbaImageDf2(const std::vector<uint8_t>& file, RgbaImage& out);
bool jpegSupported();

} // namespace df
