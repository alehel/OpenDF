#include "Bink.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace df {

namespace {

uint32_t u32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint16_t u16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }

// RAD's tables, as binkplay.exe holds them (the one Task Force Dagger
// ships, DFTFD/TFDSETUP/BINKPLAY.EXE, UPX 1.20 / NRV2D-packed; +offsets
// into the unpacked image, i.e. address 0x401000 + offset). DF2's and
// Land Warrior's binkplay.exe are Shrinker-packed and were not unpacked;
// INFERRED that they hold the same tables (the files decode with these).
// Every revision 'c'..'k' uses the same ones.
//   kScan        +0x4da00  coefficient order of the DCT and residue blocks
//   kPatterns    +0x4da48  the 16 pixel orders of the run blocks
//   kTreeLens    +0x4de50  the 16 Huffman trees: code lengths
//   kTreeCodes   +0x4df58  ... and codes, read LSB first
//   kIntraQuant  +0x49980  the 16 quantisers of intra DCT blocks (x 2^-11
//                          after the multiply; IDCT prescale included), in
//                          natural (not scan) order
//   kInterQuant  +0x4b9c0  the same for inter DCT blocks
const uint8_t kScan[64] = {
     0,  1,  8,  9,  2,  3, 10, 11,  4,  5, 12, 13,  6,  7, 14, 15, 20, 21, 28, 29, 22, 23, 30, 31, 16, 17, 24, 25, 32, 33, 40, 41, 34, 35, 42, 43, 48, 49, 56, 57, 50, 51, 58, 59, 18, 19, 26, 27, 36, 37, 44, 45, 38, 39, 46, 47, 52, 53, 60, 61, 54, 55, 62, 63,
};
const uint8_t kPatterns[16][64] = {
    { 0,  8, 16, 24, 32, 40, 48, 56, 57, 49, 41, 33, 25, 17,  9,  1,  2, 10, 18, 26, 34, 42, 50, 58, 59, 51, 43, 35, 27, 19, 11,  3,  4, 12, 20, 28, 36, 44, 52, 60, 61, 53, 45, 37, 29, 21, 13,  5,  6, 14, 22, 30, 38, 46, 54, 62, 63, 55, 47, 39, 31, 23, 15,  7},
    {59, 58, 57, 56, 48, 49, 50, 51, 43, 42, 41, 40, 32, 33, 34, 35, 27, 26, 25, 24, 16, 17, 18, 19, 11, 10,  9,  8,  0,  1,  2,  3,  4,  5,  6,  7, 15, 14, 13, 12, 20, 21, 22, 23, 31, 30, 29, 28, 36, 37, 38, 39, 47, 46, 45, 44, 52, 53, 54, 55, 63, 62, 61, 60},
    {25, 17, 18, 26, 27, 19, 11,  3,  2, 10,  9,  1,  0,  8, 16, 24, 32, 40, 48, 56, 57, 49, 41, 42, 50, 58, 59, 51, 43, 35, 34, 33, 29, 21, 22, 30, 31, 23, 15,  7,  6, 14, 13,  5,  4, 12, 20, 28, 36, 44, 52, 60, 61, 53, 45, 46, 54, 62, 63, 55, 47, 39, 38, 37},
    { 3, 11,  2, 10,  1,  9,  0,  8, 16, 24, 17, 25, 18, 26, 19, 27, 35, 43, 34, 42, 33, 41, 32, 40, 48, 56, 49, 57, 50, 58, 51, 59, 60, 52, 61, 53, 62, 54, 63, 55, 47, 39, 46, 38, 45, 37, 44, 36, 28, 20, 29, 21, 30, 22, 31, 23, 15,  7, 14,  6, 13,  5, 12,  4},
    {24, 25, 16, 17,  8,  9,  0,  1,  2,  3, 10, 11, 18, 19, 26, 27, 28, 29, 20, 21, 12, 13,  4,  5,  6,  7, 14, 15, 22, 23, 30, 31, 39, 38, 47, 46, 55, 54, 63, 62, 61, 60, 53, 52, 45, 44, 37, 36, 35, 34, 43, 42, 51, 50, 59, 58, 57, 56, 49, 48, 41, 40, 33, 32},
    { 0,  1,  2,  3,  8,  9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27, 32, 33, 34, 35, 40, 41, 42, 43, 48, 49, 50, 51, 56, 57, 58, 59,  4,  5,  6,  7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31, 36, 37, 38, 39, 44, 45, 46, 47, 52, 53, 54, 55, 60, 61, 62, 63},
    { 6,  7, 15, 14, 13,  5, 12,  4,  3, 11,  2, 10,  9,  1,  0,  8, 16, 24, 17, 25, 18, 26, 19, 27, 20, 28, 21, 29, 22, 30, 23, 31, 39, 47, 38, 46, 37, 45, 36, 44, 35, 43, 34, 42, 33, 41, 32, 40, 49, 48, 56, 57, 58, 50, 59, 51, 60, 52, 61, 53, 54, 55, 63, 62},
    { 0,  1,  2,  3,  4,  5,  6,  7, 15, 14, 13, 12, 11, 10,  9,  8, 16, 17, 18, 19, 20, 21, 22, 23, 31, 30, 29, 28, 27, 26, 25, 24, 32, 33, 34, 35, 36, 37, 38, 39, 47, 46, 45, 44, 43, 42, 41, 40, 48, 49, 50, 51, 52, 53, 54, 55, 63, 62, 61, 60, 59, 58, 57, 56},
    { 0,  8,  9,  1,  2,  3, 11, 10, 18, 19, 27, 26, 25, 17, 16, 24, 32, 40, 41, 33, 34, 35, 43, 42, 50, 49, 48, 56, 57, 58, 59, 51, 52, 60, 61, 62, 63, 55, 54, 53, 45, 44, 36, 37, 38, 46, 47, 39, 31, 23, 22, 30, 29, 28, 20, 21, 13, 12,  4,  5,  6, 14, 15,  7},
    {24, 25, 16, 17,  8,  9,  0,  1,  2,  3, 10, 11, 18, 19, 26, 27, 28, 29, 20, 21, 12, 13,  4,  5,  6,  7, 14, 15, 22, 23, 30, 31, 38, 39, 46, 47, 54, 55, 62, 63, 60, 61, 52, 53, 44, 45, 36, 37, 34, 35, 42, 43, 50, 51, 58, 59, 56, 57, 48, 49, 40, 41, 32, 33},
    { 0,  8,  1,  9,  2, 10,  3, 11, 19, 27, 18, 26, 17, 25, 16, 24, 32, 40, 33, 41, 34, 42, 35, 43, 51, 59, 50, 58, 49, 57, 48, 56, 60, 52, 61, 53, 62, 54, 63, 55, 47, 39, 46, 38, 45, 37, 44, 36, 31, 23, 30, 22, 29, 21, 28, 20, 12,  4, 13,  5, 14,  6, 15,  7},
    { 0,  8, 16, 24, 25, 26, 27, 19, 11,  3,  2,  1,  9, 17, 18, 10,  4, 12, 20, 28, 29, 30, 31, 23, 15,  7,  6,  5, 13, 21, 22, 14, 36, 44, 52, 60, 61, 62, 63, 55, 47, 39, 38, 37, 45, 53, 54, 46, 32, 40, 48, 56, 57, 58, 59, 51, 43, 35, 34, 33, 41, 49, 50, 42},
    { 0,  8,  9,  1,  2,  3, 11, 10, 19, 27, 26, 18, 17, 16, 24, 25, 33, 32, 40, 41, 42, 34, 35, 43, 51, 59, 58, 50, 49, 57, 56, 48, 52, 60, 61, 53, 54, 62, 63, 55, 47, 39, 38, 46, 45, 44, 36, 37, 29, 28, 20, 21, 22, 30, 31, 23, 14, 15,  7,  6,  5, 13, 12,  4},
    {24, 16,  8,  0,  1,  2,  3, 11, 19, 27, 26, 25, 17, 10,  9, 18, 28, 20, 12,  4,  5,  6,  7, 15, 23, 31, 30, 29, 21, 14, 13, 22, 60, 52, 44, 36, 37, 38, 39, 47, 55, 63, 62, 61, 53, 46, 45, 54, 56, 48, 40, 32, 33, 34, 35, 43, 51, 59, 58, 57, 49, 42, 41, 50},
    { 0,  8,  9,  1,  2, 10, 18, 17, 16, 24, 25, 26, 27, 19, 11,  3,  7,  6, 14, 15, 23, 22, 21, 13,  5,  4, 12, 20, 28, 29, 30, 31, 63, 62, 54, 55, 47, 46, 45, 53, 61, 60, 52, 44, 36, 37, 38, 39, 56, 48, 49, 57, 58, 50, 42, 41, 40, 32, 33, 34, 35, 43, 51, 59},
    { 0,  1,  8,  9, 16, 17, 24, 25, 32, 33, 40, 41, 48, 49, 56, 57, 58, 59, 50, 51, 42, 43, 34, 35, 26, 27, 18, 19, 10, 11,  2,  3,  4,  5, 12, 13, 20, 21, 28, 29, 36, 37, 44, 45, 52, 53, 60, 61, 62, 63, 54, 55, 46, 47, 38, 39, 30, 31, 22, 23, 14, 15,  6,  7},
};
const uint8_t kTreeLens[16][16] = {
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4},
    {1, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5},
    {2, 2, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5},
    {2, 3, 3, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5},
    {3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5},
    {3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5},
    {2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5},
    {1, 3, 3, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {1, 2, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {1, 3, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6},
    {2, 2, 3, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6},
    {1, 4, 4, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6},
    {2, 2, 2, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {1, 3, 3, 3, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7},
    {1, 3, 3, 3, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7},
    {2, 2, 3, 3, 3, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7},
};
const uint8_t kTreeCodes[16][16] = {
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F},
    {0x00, 0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F, 0x13, 0x15, 0x17, 0x19, 0x1B, 0x1D, 0x1F},
    {0x00, 0x02, 0x01, 0x09, 0x05, 0x15, 0x0D, 0x1D, 0x03, 0x13, 0x0B, 0x1B, 0x07, 0x17, 0x0F, 0x1F},
    {0x00, 0x02, 0x06, 0x01, 0x09, 0x05, 0x0D, 0x1D, 0x03, 0x13, 0x0B, 0x1B, 0x07, 0x17, 0x0F, 0x1F},
    {0x00, 0x04, 0x02, 0x06, 0x01, 0x09, 0x05, 0x0D, 0x03, 0x13, 0x0B, 0x1B, 0x07, 0x17, 0x0F, 0x1F},
    {0x00, 0x04, 0x02, 0x0A, 0x06, 0x0E, 0x01, 0x09, 0x05, 0x0D, 0x03, 0x0B, 0x07, 0x17, 0x0F, 0x1F},
    {0x00, 0x02, 0x0A, 0x06, 0x0E, 0x01, 0x09, 0x05, 0x0D, 0x03, 0x0B, 0x1B, 0x07, 0x17, 0x0F, 0x1F},
    {0x00, 0x01, 0x05, 0x03, 0x13, 0x0B, 0x1B, 0x3B, 0x07, 0x27, 0x17, 0x37, 0x0F, 0x2F, 0x1F, 0x3F},
    {0x00, 0x01, 0x03, 0x13, 0x0B, 0x2B, 0x1B, 0x3B, 0x07, 0x27, 0x17, 0x37, 0x0F, 0x2F, 0x1F, 0x3F},
    {0x00, 0x01, 0x05, 0x0D, 0x03, 0x13, 0x0B, 0x1B, 0x07, 0x27, 0x17, 0x37, 0x0F, 0x2F, 0x1F, 0x3F},
    {0x00, 0x02, 0x01, 0x05, 0x0D, 0x03, 0x13, 0x0B, 0x1B, 0x07, 0x17, 0x37, 0x0F, 0x2F, 0x1F, 0x3F},
    {0x00, 0x01, 0x09, 0x05, 0x0D, 0x03, 0x13, 0x0B, 0x1B, 0x07, 0x17, 0x37, 0x0F, 0x2F, 0x1F, 0x3F},
    {0x00, 0x02, 0x01, 0x03, 0x13, 0x0B, 0x1B, 0x3B, 0x07, 0x27, 0x17, 0x37, 0x0F, 0x2F, 0x1F, 0x3F},
    {0x00, 0x01, 0x05, 0x03, 0x07, 0x27, 0x17, 0x37, 0x0F, 0x4F, 0x2F, 0x6F, 0x1F, 0x5F, 0x3F, 0x7F},
    {0x00, 0x01, 0x05, 0x03, 0x07, 0x17, 0x37, 0x77, 0x0F, 0x4F, 0x2F, 0x6F, 0x1F, 0x5F, 0x3F, 0x7F},
    {0x00, 0x02, 0x01, 0x05, 0x03, 0x07, 0x27, 0x17, 0x37, 0x0F, 0x2F, 0x6F, 0x1F, 0x5F, 0x3F, 0x7F},
};
const uint32_t kIntraQuant[16][64] = {
    {
        0x010000, 0x016315, 0x014E7B, 0x016577, 0x010000, 0x00EEDA, 0x00BE80, 0x00611E,
        0x01E83D, 0x02A535, 0x02F1E6, 0x02724C, 0x024102, 0x017F9B, 0x01083C, 0x00A552,
        0x02346F, 0x030EE5, 0x02C628, 0x027F20, 0x021F88, 0x01DC53, 0x014819, 0x00A743,
        0x01FBFA, 0x02C096, 0x0297B5, 0x023F32, 0x027FAD, 0x01F697, 0x015A31, 0x009688,
        0x01D000, 0x028396, 0x02346F, 0x01FBFA, 0x025000, 0x01AB6B, 0x012669, 0x008D43,
        0x019247, 0x01F9AA, 0x01DC53, 0x0231B8, 0x01D122, 0x0159B3, 0x00EE1F, 0x0075ED,
        0x012F12, 0x01E06C, 0x01C48C, 0x019748, 0x01490C, 0x010288, 0x00E0F1, 0x0072AD,
        0x00CB10, 0x0119A8, 0x014E86, 0x0122AF, 0x00F735, 0x00EF51, 0x00A4D8, 0x006517,
    },
    {
        0x015555, 0x01D971, 0x01BDF9, 0x01DC9F, 0x015555, 0x013E78, 0x00FE00, 0x00817D,
        0x028AFC, 0x0386F1, 0x03ED33, 0x034311, 0x030158, 0x01FF7A, 0x01604F, 0x00DC6D,
        0x02F095, 0x0413DC, 0x03B2E0, 0x03542A, 0x02D4B5, 0x027B19, 0x01B577, 0x00DF04,
        0x02A54E, 0x03AB73, 0x0374F1, 0x02FEEE, 0x0354E7, 0x029E1F, 0x01CD96, 0x00C8B6,
        0x026AAB, 0x035A1E, 0x02F095, 0x02A54E, 0x031555, 0x0239E4, 0x01888C, 0x00BC59,
        0x02185E, 0x02A238, 0x027B19, 0x02ECF5, 0x026C2D, 0x01CCEE, 0x013D7E, 0x009D3C,
        0x019418, 0x028090, 0x025B66, 0x021F0B, 0x01B6BB, 0x0158B5, 0x012BEC, 0x0098E6,
        0x010EC0, 0x01778A, 0x01BE09, 0x018394, 0x01499C, 0x013F17, 0x00DBCB, 0x0086C9,
    },
    {
        0x01AAAB, 0x024FCE, 0x022D78, 0x0253C7, 0x01AAAB, 0x018E16, 0x013D80, 0x00A1DC,
        0x032DBB, 0x0468AD, 0x04E87F, 0x0413D5, 0x03C1AE, 0x027F58, 0x01B863, 0x011388,
        0x03ACBA, 0x0518D3, 0x049F98, 0x042935, 0x0389E2, 0x0319DF, 0x0222D4, 0x0116C5,
        0x034EA1, 0x04964F, 0x04522D, 0x03BEA9, 0x042A21, 0x0345A7, 0x0240FC, 0x00FAE3,
        0x030555, 0x0430A5, 0x03ACBA, 0x034EA1, 0x03DAAB, 0x02C85D, 0x01EAAF, 0x00EB6F,
        0x029E76, 0x034AC5, 0x0319DF, 0x03A833, 0x030738, 0x02402A, 0x018CDE, 0x00C48A,
        0x01F91E, 0x0320B4, 0x02F23F, 0x02A6CE, 0x022469, 0x01AEE2, 0x0176E7, 0x00BF20,
        0x015270, 0x01D56D, 0x022D8B, 0x01E479, 0x019C02, 0x018EDD, 0x0112BE, 0x00A87B,
    },
    {
        0x020000, 0x02C62A, 0x029CF6, 0x02CAEF, 0x020000, 0x01DDB4, 0x017D01, 0x00C23C,
        0x03D07A, 0x054A69, 0x05E3CC, 0x04E499, 0x048204, 0x02FF36, 0x021077, 0x014AA3,
        0x0468DF, 0x061DCA, 0x058C50, 0x04FE3F, 0x043F0F, 0x03B8A6, 0x029032, 0x014E86,
        0x03F7F5, 0x05812C, 0x052F69, 0x047E65, 0x04FF5A, 0x03ED2E, 0x02B461, 0x012D11,
        0x03A000, 0x05072C, 0x0468DF, 0x03F7F5, 0x04A000, 0x0356D6, 0x024CD2, 0x011A85,
        0x03248D, 0x03F353, 0x03B8A6, 0x046370, 0x03A243, 0x02B365, 0x01DC3E, 0x00EBD9,
        0x025E24, 0x03C0D8, 0x038919, 0x032E91, 0x029218, 0x020510, 0x01C1E2, 0x00E559,
        0x019620, 0x02334F, 0x029D0D, 0x02455E, 0x01EE69, 0x01DEA2, 0x0149B0, 0x00CA2D,
    },
    {
        0x02AAAB, 0x03B2E3, 0x037BF2, 0x03B93E, 0x02AAAB, 0x027CF0, 0x01FC01, 0x0102FA,
        0x0515F8, 0x070DE2, 0x07DA65, 0x068621, 0x0602B1, 0x03FEF3, 0x02C09F, 0x01B8DA,
        0x05E129, 0x0827B8, 0x0765C0, 0x06A855, 0x05A96A, 0x04F632, 0x036AED, 0x01BE09,
        0x054A9C, 0x0756E5, 0x06E9E2, 0x05FDDB, 0x06A9CE, 0x053C3E, 0x039B2D, 0x01916B,
        0x04D555, 0x06B43B, 0x05E129, 0x054A9C, 0x062AAB, 0x0473C8, 0x031118, 0x0178B2,
        0x0430BC, 0x05446F, 0x04F632, 0x05D9EB, 0x04D85A, 0x0399DC, 0x027AFD, 0x013A77,
        0x032830, 0x050121, 0x04B6CC, 0x043E16, 0x036D76, 0x02B16A, 0x0257D8, 0x0131CC,
        0x021D80, 0x02EF14, 0x037C11, 0x030728, 0x029337, 0x027E2E, 0x01B796, 0x010D91,
    },
    {
        0x038000, 0x04DACA, 0x0492AE, 0x04E322, 0x038000, 0x0343FB, 0x029AC1, 0x0153E8,
        0x06ACD5, 0x094238, 0x0A4EA5, 0x08900C, 0x07E388, 0x053E9F, 0x039CD0, 0x02429E,
        0x07B786, 0x0AB421, 0x09B58C, 0x08BCEF, 0x076E5B, 0x068322, 0x047C57, 0x02496B,
        0x06F1ED, 0x09A20D, 0x0912F8, 0x07DD30, 0x08BEDE, 0x06DF11, 0x04BBAB, 0x020EDD,
        0x065800, 0x08CC8E, 0x07B786, 0x06F1ED, 0x081800, 0x05D7F7, 0x040670, 0x01EE69,
        0x057FF7, 0x06E9D2, 0x068322, 0x07AE04, 0x065BF6, 0x04B9F1, 0x03416C, 0x019CBC,
        0x0424BF, 0x06917B, 0x062FEB, 0x05917D, 0x047FAA, 0x0388DC, 0x03134C, 0x01915C,
        0x02C6B8, 0x03D9CB, 0x0492D7, 0x03F964, 0x036138, 0x03459C, 0x0240F5, 0x0161CF,
    },
    {
        0x040000, 0x058C54, 0x0539EC, 0x0595DD, 0x040000, 0x03BB68, 0x02FA01, 0x018477,
        0x07A0F4, 0x0A94D3, 0x0BC798, 0x09C932, 0x090409, 0x05FE6D, 0x0420EE, 0x029547,
        0x08D1BE, 0x0C3B94, 0x0B18A0, 0x09FC7F, 0x087E1F, 0x07714C, 0x052064, 0x029D0D,
        0x07EFEA, 0x0B0258, 0x0A5ED3, 0x08FCC9, 0x09FEB5, 0x07DA5D, 0x0568C3, 0x025A21,
        0x074000, 0x0A0E59, 0x08D1BE, 0x07EFEA, 0x094000, 0x06ADAC, 0x0499A5, 0x02350B,
        0x06491A, 0x07E6A7, 0x07714C, 0x08C6E0, 0x074487, 0x0566CA, 0x03B87B, 0x01D7B3,
        0x04BC48, 0x0781B1, 0x071232, 0x065D22, 0x052430, 0x040A20, 0x0383C5, 0x01CAB3,
        0x032C3F, 0x04669F, 0x053A1A, 0x048ABC, 0x03DCD3, 0x03BD45, 0x029361, 0x01945A,
    },
    {
        0x050000, 0x06EF69, 0x068867, 0x06FB55, 0x050000, 0x04AA42, 0x03B881, 0x01E595,
        0x098931, 0x0D3A07, 0x0EB97E, 0x0C3B7E, 0x0B450B, 0x077E08, 0x05292A, 0x033A99,
        0x0B062D, 0x0F4A78, 0x0DDEC8, 0x0C7B9F, 0x0A9DA7, 0x094D9F, 0x06687D, 0x034450,
        0x09EBE4, 0x0DC2EE, 0x0CF687, 0x0B3BFB, 0x0C7E62, 0x09D0F4, 0x06C2F4, 0x02F0AA,
        0x091000, 0x0C91EF, 0x0B062D, 0x09EBE4, 0x0B9000, 0x085917, 0x05C00E, 0x02C24D,
        0x07DB61, 0x09E050, 0x094D9F, 0x0AF898, 0x0915A8, 0x06C07D, 0x04A69A, 0x024D9F,
        0x05EB59, 0x09621D, 0x08D6BE, 0x07F46A, 0x066D3C, 0x050CA7, 0x0464B6, 0x023D5F,
        0x03F74F, 0x058046, 0x0688A0, 0x05AD6B, 0x04D407, 0x04AC96, 0x033839, 0x01F971,
    },
    {
        0x060000, 0x08527E, 0x07D6E1, 0x0860CC, 0x060000, 0x05991C, 0x047702, 0x0246B3,
        0x0B716E, 0x0FDF3C, 0x11AB63, 0x0EADCB, 0x0D860D, 0x08FDA3, 0x063165, 0x03DFEA,
        0x0D3A9C, 0x12595D, 0x10A4F0, 0x0EFABE, 0x0CBD2E, 0x0B29F1, 0x07B096, 0x03EB93,
        0x0BE7DF, 0x108384, 0x0F8E3C, 0x0D7B2E, 0x0EFE0F, 0x0BC78B, 0x081D24, 0x038732,
        0x0AE000, 0x0F1585, 0x0D3A9C, 0x0BE7DF, 0x0DE000, 0x0A0482, 0x06E677, 0x034F90,
        0x096DA8, 0x0BD9FA, 0x0B29F1, 0x0D2A50, 0x0AE6CA, 0x081A2F, 0x0594B9, 0x02C38C,
        0x071A6B, 0x0B4289, 0x0A9B4A, 0x098BB2, 0x07B649, 0x060F2F, 0x0545A7, 0x02B00C,
        0x04C25F, 0x0699EE, 0x07D727, 0x06D01A, 0x05CB3C, 0x059BE7, 0x03DD11, 0x025E87,
    },
    {
        0x080000, 0x0B18A8, 0x0A73D7, 0x0B2BBB, 0x080000, 0x0776CF, 0x05F402, 0x0308EF,
        0x0F41E8, 0x1529A5, 0x178F2F, 0x139264, 0x120812, 0x0BFCD9, 0x0841DC, 0x052A8E,
        0x11A37B, 0x187727, 0x163140, 0x13F8FE, 0x10FC3E, 0x0EE297, 0x0A40C8, 0x053A1A,
        0x0FDFD4, 0x1604B0, 0x14BDA5, 0x11F992, 0x13FD69, 0x0FB4B9, 0x0AD186, 0x04B442,
        0x0E8000, 0x141CB1, 0x11A37B, 0x0FDFD4, 0x128000, 0x0D5B58, 0x093349, 0x046A15,
        0x0C9235, 0x0FCD4D, 0x0EE297, 0x118DC0, 0x0E890D, 0x0ACD94, 0x0770F7, 0x03AF65,
        0x09788F, 0x0F0362, 0x0E2463, 0x0CBA43, 0x0A4861, 0x08143F, 0x070789, 0x039565,
        0x06587F, 0x08CD3D, 0x0A7434, 0x091577, 0x07B9A6, 0x077A89, 0x0526C2, 0x0328B4,
    },
    {
        0x0C0000, 0x10A4FD, 0x0FADC3, 0x10C198, 0x0C0000, 0x0B3237, 0x08EE03, 0x048D66,
        0x16E2DB, 0x1FBE78, 0x2356C7, 0x1D5B96, 0x1B0C1A, 0x11FB46, 0x0C62CA, 0x07BFD5,
        0x1A7539, 0x24B2BB, 0x2149E1, 0x1DF57D, 0x197A5D, 0x1653E3, 0x0F612C, 0x07D727,
        0x17CFBD, 0x210709, 0x1F1C78, 0x1AF65B, 0x1DFC1E, 0x178F16, 0x103A49, 0x070E64,
        0x15C000, 0x1E2B0A, 0x1A7539, 0x17CFBD, 0x1BC000, 0x140904, 0x0DCCEE, 0x069F20,
        0x12DB4F, 0x17B3F4, 0x1653E3, 0x1A54A0, 0x15CD94, 0x10345E, 0x0B2972, 0x058718,
        0x0E34D7, 0x168513, 0x153695, 0x131765, 0x0F6C91, 0x0C1E5E, 0x0A8B4E, 0x056018,
        0x0984BE, 0x0D33DC, 0x0FAE4E, 0x0DA033, 0x0B9678, 0x0B37CE, 0x07BA22, 0x04BD0E,
    },
    {
        0x110000, 0x179466, 0x16362A, 0x17BCED, 0x110000, 0x0FDC79, 0x0CA685, 0x0672FB,
        0x206C0C, 0x2CF87F, 0x321044, 0x299714, 0x265125, 0x19794E, 0x118BF4, 0x0AFA6D,
        0x257B66, 0x33FD33, 0x2F28A9, 0x2A711B, 0x241804, 0x1FA181, 0x15C9A9, 0x0B1B77,
        0x21BBA2, 0x2EC9F7, 0x2C12FF, 0x263256, 0x2A7A80, 0x21600A, 0x16FD3C, 0x09FF0D,
        0x1ED000, 0x2ABCF9, 0x257B66, 0x21BBA2, 0x275000, 0x1C621B, 0x138CFB, 0x09616E,
        0x1AB6B0, 0x219444, 0x1FA181, 0x254D38, 0x1EE33C, 0x16F4DB, 0x0FD00C, 0x07D4B7,
        0x142030, 0x1FE730, 0x1E0D52, 0x1B0BCF, 0x15D9CE, 0x112B06, 0x0EF004, 0x079D77,
        0x0D7C0E, 0x12B423, 0x1636EE, 0x134D9E, 0x106A80, 0x0FE464, 0x0AF25B, 0x06B67F,
    },
    {
        0x160000, 0x1E83CF, 0x1CBE90, 0x1EB842, 0x160000, 0x1486BA, 0x105F06, 0x085891,
        0x29F53D, 0x3A3286, 0x40C9C2, 0x35D293, 0x319630, 0x20F756, 0x16B51E, 0x0E3506,
        0x308193, 0x4347AC, 0x3D0771, 0x36ECBA, 0x2EB5AA, 0x28EF20, 0x1C3225, 0x0E5FC7,
        0x2BA786, 0x3C8CE5, 0x390986, 0x316E52, 0x36F8E1, 0x2B30FE, 0x1DC030, 0x0CEFB7,
        0x27E000, 0x374EE7, 0x308193, 0x2BA786, 0x32E000, 0x24BB33, 0x194D09, 0x0C23BB,
        0x229212, 0x2B7494, 0x28EF20, 0x3045D0, 0x27F8E4, 0x1DB557, 0x1476A6, 0x0A2256,
        0x1A0B89, 0x29494D, 0x26E410, 0x230039, 0x1C470A, 0x1637AD, 0x1354B9, 0x09DAD6,
        0x11735D, 0x183469, 0x1CBF8F, 0x18FB09, 0x153E87, 0x1490FA, 0x0E2A94, 0x08AFF0,
    },
    {
        0x1C0000, 0x26D64D, 0x249572, 0x27190E, 0x1C0000, 0x1A1FD6, 0x14D607, 0x0A9F44,
        0x3566AA, 0x4A11C2, 0x527525, 0x44805E, 0x3F1C3E, 0x29F4F9, 0x1CE683, 0x1214F0,
        0x3DBC30, 0x55A109, 0x4DAC61, 0x45E778, 0x3B72D9, 0x341911, 0x23E2BB, 0x124B5B,
        0x378F64, 0x4D1069, 0x4897C2, 0x3EE97F, 0x45F6F0, 0x36F889, 0x25DD54, 0x1076E9,
        0x32C000, 0x46646C, 0x3DBC30, 0x378F64, 0x40C000, 0x2EBFB5, 0x203380, 0x0F734B,
        0x2BFFB9, 0x374E8E, 0x341911, 0x3D7020, 0x32DFAE, 0x25CF86, 0x1A0B5F, 0x0CE5E2,
        0x2125F5, 0x348BD6, 0x317F5B, 0x2C8BEB, 0x23FD53, 0x1C46DC, 0x189A60, 0x0C8AE2,
        0x1635BC, 0x1ECE57, 0x2496B6, 0x1FCB22, 0x1B09C4, 0x1A2CE1, 0x1207A5, 0x0B0E77,
    },
    {
        0x220000, 0x2F28CC, 0x2C6C53, 0x2F79DA, 0x220000, 0x1FB8F1, 0x194D09, 0x0CE5F7,
        0x40D818, 0x59F0FE, 0x642089, 0x532E29, 0x4CA24B, 0x32F29C, 0x2317E8, 0x15F4DB,
        0x4AF6CC, 0x67FA67, 0x5E5152, 0x54E237, 0x483007, 0x3F4303, 0x2B9351, 0x1636EE,
        0x437743, 0x5D93EE, 0x5825FE, 0x4C64AD, 0x54F4FF, 0x42C014, 0x2DFA79, 0x13FE1A,
        0x3DA000, 0x5579F1, 0x4AF6CC, 0x437743, 0x4EA000, 0x38C437, 0x2719F7, 0x12C2DB,
        0x356D61, 0x432888, 0x3F4303, 0x4A9A70, 0x3DC678, 0x2DE9B5, 0x1FA018, 0x0FA96E,
        0x284060, 0x3FCE60, 0x3C1AA5, 0x36179D, 0x2BB39B, 0x22560C, 0x1DE007, 0x0F3AEE,
        0x1AF81B, 0x256845, 0x2C6DDD, 0x269B3C, 0x20D500, 0x1FC8C8, 0x15E4B7, 0x0D6CFE,
    },
    {
        0x2C0000, 0x3D079E, 0x397D20, 0x3D7083, 0x2C0000, 0x290D75, 0x20BE0C, 0x10B121,
        0x53EA79, 0x74650C, 0x819383, 0x6BA525, 0x632C61, 0x41EEAC, 0x2D6A3B, 0x1C6A0C,
        0x610326, 0x868F57, 0x7A0EE2, 0x6DD974, 0x5D6B54, 0x51DE40, 0x38644B, 0x1CBF8F,
        0x574F0B, 0x7919CA, 0x72130C, 0x62DCA3, 0x6DF1C2, 0x5661FB, 0x3B8060, 0x19DF6D,
        0x4FC000, 0x6E9DCE, 0x610326, 0x574F0B, 0x65C000, 0x497665, 0x329A12, 0x184776,
        0x452423, 0x56E928, 0x51DE40, 0x608BA0, 0x4FF1C9, 0x3B6AAE, 0x28ED4D, 0x1444AC,
        0x341713, 0x52929A, 0x4DC821, 0x460071, 0x388E14, 0x2C6F5A, 0x26A973, 0x13B5AD,
        0x22E6BA, 0x3068D2, 0x397F1E, 0x31F611, 0x2A7D0F, 0x2921F4, 0x1C5528, 0x115FDF,
    },
};
const uint32_t kInterQuant[16][64] = {
    {
        0x010000, 0x017946, 0x016363, 0x0152A7, 0x012000, 0x00E248, 0x00A486, 0x0053E0,
        0x01A5A9, 0x0248DC, 0x0243EC, 0x0209EA, 0x01BBDA, 0x015CBC, 0x00F036, 0x008095,
        0x01B701, 0x0260EB, 0x023D97, 0x020437, 0x01B701, 0x016959, 0x00F8E7, 0x007EE4,
        0x019DE9, 0x023E1B, 0x021CCC, 0x01E6B4, 0x01B0B9, 0x0153FD, 0x00EA30, 0x007763,
        0x017000, 0x01FE6E, 0x01E0D1, 0x01B0B9, 0x018000, 0x012DB5, 0x00CFD2, 0x006E5C,
        0x012DB5, 0x01A27B, 0x018A33, 0x01718D, 0x0146D9, 0x0100CE, 0x00B0E4, 0x005A2D,
        0x00D87A, 0x014449, 0x013178, 0x0112EA, 0x00E9CC, 0x00B7B1, 0x008337, 0x0042E5,
        0x007B9A, 0x00AB71, 0x00AD08, 0x009BB9, 0x00846F, 0x006B85, 0x004A10, 0x002831,
    },
    {
        0x015555, 0x01F708, 0x01D9D9, 0x01C389, 0x018000, 0x012DB5, 0x00DB5D, 0x006FD5,
        0x023237, 0x030BD0, 0x03053B, 0x02B7E3, 0x024FCE, 0x01D0FA, 0x014048, 0x00AB71,
        0x024957, 0x032BE4, 0x02FCC9, 0x02B04A, 0x024957, 0x01E1CC, 0x014BDE, 0x00A92F,
        0x0227E1, 0x02FD7A, 0x02D110, 0x0288F1, 0x0240F7, 0x01C551, 0x013840, 0x009F2F,
        0x01EAAB, 0x02A893, 0x028116, 0x0240F7, 0x020000, 0x019247, 0x011518, 0x009325,
        0x019247, 0x022DF9, 0x020D99, 0x01ECBC, 0x01B3CC, 0x015668, 0x00EBDA, 0x00783D,
        0x0120A3, 0x01B061, 0x01974B, 0x016E8E, 0x0137BB, 0x00F4ED, 0x00AEF4, 0x005931,
        0x00A4CE, 0x00E497, 0x00E6B5, 0x00CFA2, 0x00B093, 0x008F5C, 0x0062BF, 0x003597,
    },
    {
        0x01AAAB, 0x0274CB, 0x02504F, 0x02346C, 0x01E000, 0x017922, 0x011235, 0x008BCA,
        0x02BEC4, 0x03CEC4, 0x03C689, 0x0365DC, 0x02E3C1, 0x024539, 0x01905A, 0x00D64D,
        0x02DBAD, 0x03F6DD, 0x03BBFB, 0x035C5C, 0x02DBAD, 0x025A40, 0x019ED6, 0x00D37B,
        0x02B1D9, 0x03BCD8, 0x038554, 0x032B2D, 0x02D134, 0x0236A5, 0x018650, 0x00C6FB,
        0x026555, 0x0352B8, 0x03215C, 0x02D134, 0x028000, 0x01F6D8, 0x015A5E, 0x00B7EF,
        0x01F6D8, 0x02B977, 0x029100, 0x0267EB, 0x0220C0, 0x01AC02, 0x0126D1, 0x00964C,
        0x0168CC, 0x021C7A, 0x01FD1E, 0x01CA31, 0x0185A9, 0x013228, 0x00DAB2, 0x006F7D,
        0x00CE01, 0x011DBD, 0x012062, 0x01038A, 0x00DCB8, 0x00B333, 0x007B6F, 0x0042FC,
    },
    {
        0x020000, 0x02F28D, 0x02C6C5, 0x02A54E, 0x024000, 0x01C48F, 0x01490C, 0x00A7BF,
        0x034B52, 0x0491B8, 0x0487D8, 0x0413D5, 0x0377B5, 0x02B977, 0x01E06C, 0x01012A,
        0x036E03, 0x04C1D6, 0x047B2D, 0x04086E, 0x036E03, 0x02D2B3, 0x01F1CE, 0x00FDC7,
        0x033BD1, 0x047C37, 0x043998, 0x03CD69, 0x036172, 0x02A7FA, 0x01D460, 0x00EEC7,
        0x02E000, 0x03FCDD, 0x03C1A1, 0x036172, 0x030000, 0x025B6A, 0x019FA3, 0x00DCB8,
        0x025B6A, 0x0344F5, 0x031466, 0x02E31B, 0x028DB3, 0x02019B, 0x0161C7, 0x00B45B,
        0x01B0F5, 0x028892, 0x0262F1, 0x0225D5, 0x01D398, 0x016F63, 0x01066F, 0x0085C9,
        0x00F735, 0x0156E2, 0x015A10, 0x013772, 0x0108DD, 0x00D70A, 0x00941F, 0x005062,
    },
    {
        0x02AAAB, 0x03EE11, 0x03B3B2, 0x038713, 0x030000, 0x025B6A, 0x01B6BB, 0x00DFAA,
        0x04646D, 0x0617A0, 0x060A75, 0x056FC6, 0x049F9B, 0x03A1F4, 0x028090, 0x0156E2,
        0x0492AE, 0x0657C8, 0x05F991, 0x056093, 0x0492AE, 0x03C399, 0x0297BD, 0x01525F,
        0x044FC1, 0x05FAF4, 0x05A220, 0x0511E1, 0x0481ED, 0x038AA2, 0x027080, 0x013E5E,
        0x03D555, 0x055126, 0x05022D, 0x0481ED, 0x040000, 0x03248D, 0x022A2F, 0x01264B,
        0x03248D, 0x045BF2, 0x041B33, 0x03D979, 0x036799, 0x02ACCF, 0x01D7B5, 0x00F079,
        0x024147, 0x0360C3, 0x032E96, 0x02DD1C, 0x026F75, 0x01E9D9, 0x015DE9, 0x00B262,
        0x01499C, 0x01C92E, 0x01CD6A, 0x019F43, 0x016127, 0x011EB8, 0x00C57F, 0x006B2D,
    },
    {
        0x038000, 0x052876, 0x04DBD9, 0x04A148, 0x03F000, 0x0317FB, 0x023FD5, 0x01258F,
        0x05C3CF, 0x07FF02, 0x07EDBA, 0x0722B4, 0x06117C, 0x04C491, 0x0348BD, 0x01C209,
        0x060085, 0x085336, 0x07D78F, 0x070EC1, 0x060085, 0x04F0B9, 0x036728, 0x01BC1C,
        0x05A8AE, 0x07D960, 0x0764CA, 0x06A777, 0x05EA87, 0x04A5F5, 0x0333A8, 0x01A1DB,
        0x050800, 0x06FA82, 0x0692DA, 0x05EA87, 0x054000, 0x041FF9, 0x02D75E, 0x018242,
        0x041FF9, 0x05B8AE, 0x0563B2, 0x050D6E, 0x0477F9, 0x0382D0, 0x026B1D, 0x013B9F,
        0x02F5AD, 0x046F00, 0x042D25, 0x03C235, 0x03324A, 0x0282ED, 0x01CB42, 0x00EA21,
        0x01B09C, 0x02580C, 0x025D9B, 0x022108, 0x01CF83, 0x017851, 0x010336, 0x008CAC,
    },
    {
        0x040000, 0x05E519, 0x058D8A, 0x054A9C, 0x048000, 0x03891F, 0x029218, 0x014F7E,
        0x0696A4, 0x092370, 0x090FB0, 0x0827AA, 0x06EF69, 0x0572EE, 0x03C0D8, 0x020254,
        0x06DC05, 0x0983AC, 0x08F65A, 0x0810DD, 0x06DC05, 0x05A565, 0x03E39B, 0x01FB8E,
        0x0677A2, 0x08F86E, 0x087330, 0x079AD1, 0x06C2E4, 0x054FF3, 0x03A8C0, 0x01DD8D,
        0x05C000, 0x07F9B9, 0x078343, 0x06C2E4, 0x060000, 0x04B6D4, 0x033F47, 0x01B970,
        0x04B6D4, 0x0689EB, 0x0628CC, 0x05C635, 0x051B65, 0x040337, 0x02C38F, 0x0168B6,
        0x0361EA, 0x051124, 0x04C5E1, 0x044BAA, 0x03A730, 0x02DEC6, 0x020CDD, 0x010B93,
        0x01EE69, 0x02ADC5, 0x02B41F, 0x026EE5, 0x0211BA, 0x01AE14, 0x01283E, 0x00A0C4,
    },
    {
        0x050000, 0x075E60, 0x06F0ED, 0x069D43, 0x05A000, 0x046B67, 0x03369E, 0x01A35E,
        0x083C4D, 0x0B6C4C, 0x0B539C, 0x0A3194, 0x08AB44, 0x06CFAA, 0x04B10F, 0x0282E8,
        0x089307, 0x0BE497, 0x0B33F1, 0x0A1514, 0x089307, 0x070EBF, 0x04DC82, 0x027A72,
        0x08158B, 0x0B3689, 0x0A8FFC, 0x098186, 0x08739C, 0x06A3F0, 0x0492F0, 0x0254F0,
        0x073000, 0x09F827, 0x096413, 0x08739C, 0x078000, 0x05E489, 0x040F19, 0x0227CC,
        0x05E489, 0x082C66, 0x07B2FF, 0x0737C2, 0x06623F, 0x050405, 0x037473, 0x01C2E3,
        0x043A64, 0x06556D, 0x05F75A, 0x055E94, 0x0490FC, 0x039677, 0x029015, 0x014E78,
        0x026A04, 0x035936, 0x036127, 0x030A9E, 0x029629, 0x021999, 0x01724E, 0x00C8F5,
    },
    {
        0x060000, 0x08D7A6, 0x085450, 0x07EFEA, 0x06C000, 0x054DAE, 0x03DB24, 0x01F73E,
        0x09E1F6, 0x0DB528, 0x0D9788, 0x0C3B7E, 0x0A671E, 0x082C66, 0x05A145, 0x03037D,
        0x0A4A08, 0x0E4582, 0x0D7187, 0x0C194B, 0x0A4A08, 0x087818, 0x05D569, 0x02F955,
        0x09B373, 0x0D74A5, 0x0CACC8, 0x0B683A, 0x0A2455, 0x07F7ED, 0x057D20, 0x02CC54,
        0x08A000, 0x0BF696, 0x0B44E4, 0x0A2455, 0x090000, 0x07123E, 0x04DEEA, 0x029629,
        0x07123E, 0x09CEE0, 0x093D32, 0x08A950, 0x07A918, 0x0604D2, 0x042556, 0x021D11,
        0x0512DF, 0x0799B6, 0x0728D2, 0x06717F, 0x057AC8, 0x044E28, 0x03134C, 0x01915C,
        0x02E59E, 0x0404A7, 0x040E2F, 0x03A657, 0x031A97, 0x02851E, 0x01BC5D, 0x00F126,
    },
    {
        0x080000, 0x0BCA33, 0x0B1B15, 0x0A9538, 0x090000, 0x07123E, 0x052430, 0x029EFD,
        0x0D2D48, 0x1246E0, 0x121F5F, 0x104F53, 0x0DDED2, 0x0AE5DD, 0x0781B1, 0x0404A7,
        0x0DB80B, 0x130757, 0x11ECB4, 0x1021B9, 0x0DB80B, 0x0B4ACB, 0x07C736, 0x03F71D,
        0x0CEF44, 0x11F0DC, 0x10E661, 0x0F35A3, 0x0D85C7, 0x0A9FE7, 0x075180, 0x03BB1A,
        0x0B8000, 0x0FF372, 0x0F0686, 0x0D85C7, 0x0C0000, 0x096DA8, 0x067E8E, 0x0372E1,
        0x096DA8, 0x0D13D6, 0x0C5198, 0x0B8C6A, 0x0A36CB, 0x08066E, 0x05871E, 0x02D16B,
        0x06C3D4, 0x0A2248, 0x098BC3, 0x089754, 0x074E60, 0x05BD8B, 0x0419BB, 0x021726,
        0x03DCD3, 0x055B8A, 0x05683E, 0x04DDC9, 0x042374, 0x035C28, 0x02507C, 0x014188,
    },
    {
        0x0C0000, 0x11AF4C, 0x10A89F, 0x0FDFD4, 0x0D8000, 0x0A9B5D, 0x07B649, 0x03EE7B,
        0x13C3EC, 0x1B6A50, 0x1B2F0F, 0x1876FD, 0x14CE3C, 0x1058CB, 0x0B4289, 0x0606FB,
        0x149410, 0x1C8B03, 0x1AE30E, 0x183296, 0x149410, 0x10F030, 0x0BAAD2, 0x05F2AB,
        0x1366E6, 0x1AE949, 0x195991, 0x16D074, 0x1448AB, 0x0FEFDA, 0x0AFA40, 0x0598A7,
        0x114000, 0x17ED2B, 0x1689C8, 0x1448AB, 0x120000, 0x0E247C, 0x09BDD5, 0x052C51,
        0x0E247C, 0x139DC1, 0x127A63, 0x11529F, 0x0F5230, 0x0C09A5, 0x084AAC, 0x043A21,
        0x0A25BE, 0x0F336D, 0x0E51A4, 0x0CE2FE, 0x0AF590, 0x089C51, 0x062698, 0x0322B9,
        0x05CB3C, 0x08094E, 0x081C5D, 0x074CAE, 0x06352E, 0x050A3B, 0x0378BA, 0x01E24D,
    },
    {
        0x110000, 0x190DAC, 0x17998C, 0x167D16, 0x132000, 0x0F06C3, 0x0AECE7, 0x0591D9,
        0x1C0039, 0x26D69C, 0x2682AB, 0x22A891, 0x1D797F, 0x172876, 0x0FF398, 0x0889E3,
        0x1D2717, 0x286F9A, 0x2616FF, 0x2247AA, 0x1D2717, 0x17FEEF, 0x108754, 0x086D1D,
        0x1B7C71, 0x261FD3, 0x23E98D, 0x2051FA, 0x1CBC47, 0x1693CA, 0x0F8D30, 0x07ED98,
        0x187000, 0x21E552, 0x1FEDDC, 0x1CBC47, 0x198000, 0x140904, 0x0DCCEE, 0x07541E,
        0x140904, 0x1BCA27, 0x1A2D62, 0x188A62, 0x15B46F, 0x110DAA, 0x0BBF1F, 0x05FD04,
        0x0E6022, 0x1588DA, 0x1448FE, 0x124192, 0x0F868B, 0x0C32C8, 0x08B6AD, 0x047130,
        0x083540, 0x0B6284, 0x0B7D84, 0x0A574B, 0x08CB57, 0x0723D4, 0x04EB08, 0x02AB42,
    },
    {
        0x160000, 0x206C0C, 0x1E8A79, 0x1D1A59, 0x18C000, 0x13722A, 0x0E2385, 0x073537,
        0x243C86, 0x3242E8, 0x31D646, 0x2CDA25, 0x2624C3, 0x1DF820, 0x14A4A7, 0x0B0CCC,
        0x25BA1D, 0x345430, 0x314AEF, 0x2C5CBE, 0x25BA1D, 0x1F0DAE, 0x1563D6, 0x0AE78E,
        0x2391FB, 0x31565C, 0x2E798A, 0x29D380, 0x252FE4, 0x1D37BB, 0x142021, 0x0A4288,
        0x1FA000, 0x2BDD7A, 0x2951EF, 0x252FE4, 0x210000, 0x19ED8D, 0x11DC06, 0x097BEA,
        0x19ED8D, 0x23F68C, 0x21E061, 0x1FC224, 0x1C16AE, 0x1611AE, 0x0F3391, 0x07BFE7,
        0x129A87, 0x1BDE47, 0x1A4058, 0x17A026, 0x141787, 0x0FC93E, 0x0B46C1, 0x05BFA8,
        0x0A9F44, 0x0EBBBA, 0x0EDEAB, 0x0D61E9, 0x0B617F, 0x093D6D, 0x065D55, 0x037437,
    },
    {
        0x1C0000, 0x2943B2, 0x26DEC9, 0x250A43, 0x1F8000, 0x18BFD8, 0x11FEA9, 0x092C75,
        0x2E1E7C, 0x3FF810, 0x3F6DCE, 0x3915A3, 0x308BE1, 0x262485, 0x1A45EB, 0x0E1049,
        0x300425, 0x4299B2, 0x3EBC76, 0x387608, 0x300425, 0x2785C6, 0x1B393F, 0x0DE0E4,
        0x2D456E, 0x3ECB00, 0x3B2652, 0x353BBA, 0x2F5439, 0x252FA8, 0x199D41, 0x0D0EDC,
        0x284000, 0x37D40F, 0x3496D3, 0x2F5439, 0x2A0000, 0x20FFCB, 0x16BAF1, 0x0C1213,
        0x20FFCB, 0x2DC56D, 0x2B1D93, 0x286B74, 0x23BFC6, 0x1C1681, 0x1358E8, 0x09DCF8,
        0x17AD66, 0x2377FE, 0x21692A, 0x1E11A5, 0x19924F, 0x141767, 0x0E5A0D, 0x075104,
        0x0D84E2, 0x12C062, 0x12ECDA, 0x110840, 0x0E7C16, 0x0BC28A, 0x0819B2, 0x04655D,
    },
    {
        0x220000, 0x321B58, 0x2F3318, 0x2CFA2D, 0x264000, 0x1E0D86, 0x15D9CE, 0x0B23B2,
        0x380072, 0x4DAD38, 0x4D0556, 0x455122, 0x3AF2FE, 0x2E50EB, 0x1FE730, 0x1113C7,
        0x3A4E2D, 0x50DF33, 0x4C2DFD, 0x448F54, 0x3A4E2D, 0x2FFDDF, 0x210EA8, 0x10DA39,
        0x36F8E1, 0x4C3FA5, 0x47D31B, 0x40A3F5, 0x39788E, 0x2D2795, 0x1F1A61, 0x0FDB2F,
        0x30E000, 0x43CAA5, 0x3FDBB7, 0x39788E, 0x330000, 0x281209, 0x1B99DB, 0x0EA83B,
        0x281209, 0x37944D, 0x345AC4, 0x3114C3, 0x2B68DF, 0x221B53, 0x177E3E, 0x0BFA09,
        0x1CC044, 0x2B11B4, 0x2891FC, 0x248324, 0x1F0D17, 0x18658F, 0x116D5A, 0x08E260,
        0x106A80, 0x16C509, 0x16FB08, 0x14AE97, 0x1196AE, 0x0E47A8, 0x09D60F, 0x055684,
    },
    {
        0x2C0000, 0x40D818, 0x3D14F2, 0x3A34B2, 0x318000, 0x26E454, 0x1C470A, 0x0E6A6E,
        0x48790C, 0x6485D0, 0x63AC8D, 0x59B44A, 0x4C4986, 0x3BF03F, 0x29494D, 0x161998,
        0x4B743A, 0x68A861, 0x6295DE, 0x58B97B, 0x4B743A, 0x3E1B5C, 0x2AC7AC, 0x15CF1D,
        0x4723F6, 0x62ACB8, 0x5CF313, 0x53A701, 0x4A5FC7, 0x3A6F75, 0x284041, 0x148510,
        0x3F4000, 0x57BAF3, 0x52A3DE, 0x4A5FC7, 0x420000, 0x33DB1A, 0x23B80D, 0x12F7D4,
        0x33DB1A, 0x47ED19, 0x43C0C2, 0x3F8448, 0x382D5C, 0x2C235D, 0x1E6723, 0x0F7FCF,
        0x25350D, 0x37BC8E, 0x3480B0, 0x2F404C, 0x282F0E, 0x1F927D, 0x168D83, 0x0B7F50,
        0x153E87, 0x1D7775, 0x1DBD56, 0x1AC3D2, 0x16C2FF, 0x127AD9, 0x0CBAAA, 0x06E86E,
    },
};

// Bundle numbers, in the order a plane's header and each row of blocks
// read them.
enum { kBlockTypes, kSubBlockTypes, kColors, kPattern, kXOff, kYOff, kIntraDc, kInterDc, kRun, kBundles };

// Block types (8x8, except the scaled one: 16x16 from an 8x8 sub-block).
enum { kSkip, kScaled, kMotion, kRunBlock, kResidue, kIntra, kFill, kInter, kPatternBlock, kRaw };

// Block-type runs: Huffman values 12..15 repeat the last type this often.
constexpr int kTypeRuns[4] = {4, 8, 12, 32};

// Bink audio: the run lengths (x 8 coefficients) of a coefficient group
// whose length bit is set, and the band edges in Hz (Zwicker's critical
// bands, as binkplay.exe's +0x65c60 and +0x65c74 hold them).
constexpr int kAudioRuns[16] = {2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};
constexpr int kCriticalFreqs[24] = {100,  200,  300,  400,  510,  630,  770,  920,  1080, 1270, 1480,  1720,
                                    2000, 2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500};

// Per tree: the symbol index for the next 7 bits (LSB first) and the code
// length, built from kTreeCodes / kTreeLens.
struct HuffTables {
    std::array<std::array<uint8_t, 128>, 16> sym{}, len{};
    HuffTables() {
        for (int t = 0; t < 16; ++t)
            for (int s = 0; s < 16; ++s) {
                const int l = kTreeLens[t][s], c = kTreeCodes[t][s];
                for (int hi = 0; hi < (128 >> l); ++hi) {
                    sym[size_t(t)][size_t(c | hi << l)] = uint8_t(s);
                    len[size_t(t)][size_t(c | hi << l)] = uint8_t(l);
                }
            }
    }
};
const HuffTables& huff() {
    static const HuffTables h;
    return h;
}

// Bink's 8x8 IDCT: the AAN factorisation in integers (constants x 2^11:
// 2896 = sqrt(2), 3784 = 2 cos(pi/8), 2217 = 2 sqrt(2) cos(3pi/8),
// -5352 = -2 sqrt(2) cos(pi/8)), columns first without rounding, then
// rows rounded by 1/256.
// The coefficients come prescaled by the quantiser tables.
template <int Step, typename In, typename Out, typename Store>
inline void idct8(const In* s, Out* d, int dstep, Store store) {
    const int a0 = s[0] + s[4 * Step], a1 = s[0] - s[4 * Step];
    const int a2 = s[2 * Step] + s[6 * Step], a3 = (2896 * (s[2 * Step] - s[6 * Step])) >> 11;
    const int a4 = s[5 * Step] + s[3 * Step], a5 = s[5 * Step] - s[3 * Step];
    const int a6 = s[1 * Step] + s[7 * Step], a7 = s[1 * Step] - s[7 * Step];
    const int b0 = a4 + a6;
    const int b1 = (3784 * (a5 + a7)) >> 11;
    const int b2 = ((-5352 * a5) >> 11) - b0 + b1;
    const int b3 = ((2896 * (a6 - a4)) >> 11) - b2;
    const int b4 = ((2217 * a7) >> 11) + b3 - b1;
    store(d[0], a0 + a2 + b0);
    store(d[1 * dstep], a1 + a3 - a2 + b2);
    store(d[2 * dstep], a1 - a3 + a2 + b3);
    store(d[3 * dstep], a0 - a2 - b4);
    store(d[4 * dstep], a0 - a2 + b4);
    store(d[5 * dstep], a1 - a3 + a2 - b3);
    store(d[6 * dstep], a1 + a3 - a2 - b2);
    store(d[7 * dstep], a0 + a2 - b0);
}

uint8_t clip8(int v) { return uint8_t(std::clamp(v, 0, 255)); }

// add = false: the block replaces the pixels (clamped to 0..255); true: it
// is added to them modulo 256, unclamped: the encoder codes inter blocks
// that way (a dark pixel can be made bright by a negative residual).
void idct(int32_t blk[64], uint8_t* dst, int stride, bool add) {
    int t[64];
    for (int i = 0; i < 8; ++i) {
        const int32_t* s = blk + i;
        if ((s[8] | s[16] | s[24] | s[32] | s[40] | s[48] | s[56]) == 0) {
            for (int k = 0; k < 8; ++k) t[i + 8 * k] = s[0];   // DC only: a flat column
        } else {
            idct8<8>(s, t + i, 8, [](int& o, int v) { o = v; });
        }
    }
    for (int r = 0; r < 8; ++r) {
        int o[8];
        idct8<1>(t + 8 * r, o, 1, [](int& x, int v) { x = (v + 0x7f) >> 8; });
        uint8_t* p = dst + r * stride;
        for (int k = 0; k < 8; ++k) p[k] = add ? uint8_t(p[k] + o[k]) : clip8(o[k]);
    }
}

// In-place radix-2 complex FFT of n (a power of 2) interleaved re/im
// floats, sign +1: sum x_k e^(+2 pi i k n / N) (an inverse transform,
// unscaled).
void fftInverse(float* z, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(z[2 * i], z[2 * j]);
            std::swap(z[2 * i + 1], z[2 * j + 1]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double a = 2 * M_PI / len;
        for (int k = 0; k < len / 2; ++k) {
            const float wr = float(std::cos(a * k)), wi = float(std::sin(a * k));
            for (int i = k; i < n; i += len) {
                float* u = z + 2 * i;
                float* v = z + 2 * (i + len / 2);
                const float tr = v[0] * wr - v[1] * wi, ti = v[0] * wi + v[1] * wr;
                v[0] = u[0] - tr;
                v[1] = u[1] - ti;
                u[0] += tr;
                u[1] += ti;
            }
        }
    }
}

} // namespace

// Bits are read from the packet's bytes least significant first (the same
// as from 32-bit little-endian words). Reads past the end give zeros and
// set `over`.
struct BinkVideo::Bits {
    const uint8_t* d;
    size_t n, pos = 0;   // n: bits in the packet
    bool over = false;
    Bits(const uint8_t* data, size_t bytes) : d(data), n(bytes * 8) {}   // data padded by 8 bytes
    size_t left() const { return pos < n ? n - pos : 0; }
    uint32_t get(int k) {
        if (k <= 0) return 0;
        if (pos + size_t(k) > n) {
            over = true;
            pos = n;
            return 0;
        }
        uint64_t w;
        std::memcpy(&w, d + (pos >> 3), 8);
        const uint32_t v = uint32_t((w >> (pos & 7)) & ((uint64_t(1) << k) - 1));
        pos += size_t(k);
        return v;
    }
    int bit() { return int(get(1)); }
    uint32_t peek7() const {
        if (pos >= n) return 0;
        uint64_t w;
        std::memcpy(&w, d + (pos >> 3), 8);
        return uint32_t(w >> (pos & 7)) & 127;
    }
    void skip(size_t k) { pos = std::min(n, pos + k); }
    void align32() { pos = std::min(n, (pos + 31) & ~size_t(31)); }
};

namespace {

using Bits = BinkVideo::Bits;

int huffSym(Bits& b, int table) {
    const uint32_t p = b.peek7();
    b.skip(huff().len[size_t(table)][p]);
    return huff().sym[size_t(table)][p];
}

// A signed coefficient of `bits` + 1 magnitude bits whose top bit is
// implied, then the sign.
int coef(Bits& b, int bits) {
    if (bits == 0) return b.bit() ? -1 : 1;
    const int t = int(b.get(bits)) | 1 << bits;
    return b.bit() ? -t : t;
}

// The DCT coefficients of a block (after the DC, which the DC bundles
// give): bit planes from the top one down. A work list starts with the
// groups 4..23, 24..43, 44..63 (mode 0) and the single coefficients 1, 2,
// 3 (mode 3). Per bit plane each live entry has a bit "something in here
// is this large": a mode-0 group then codes its first 4 coefficients and
// splits the rest into 4 groups of 4 (mode 1 -> mode 2 entries); a mode-2
// group codes its 4; a coefficient a group codes is either set now (bit
// 0: magnitude, sign) or put on the list as a mode-3 single (bit 1) for a
// later plane. Then the 4-bit quantiser index.
int readDct(Bits& b, int32_t blk[64], int idx[64], int& count) {
    int cl[128], ml[128];
    int start = 64, end = 64;
    auto add = [&](int c, int m) { cl[end] = c; ml[end++] = m; };
    add(4, 0);
    add(24, 0);
    add(44, 0);
    add(1, 3);
    add(2, 3);
    add(3, 3);
    count = 0;
    for (int bits = int(b.get(4)) - 1; bits >= 0; --bits) {
        int pos = start;
        while (pos < end) {
            if (!(cl[pos] | ml[pos]) || !b.bit()) { ++pos; continue; }
            int c = cl[pos];
            switch (ml[pos]) {
            case 0:
            case 2:
                if (ml[pos] == 0) {
                    cl[pos] = c + 4;
                    ml[pos] = 1;
                } else {
                    cl[pos] = 0;
                    ml[pos++] = 0;
                }
                for (int i = 0; i < 4; ++i, ++c) {
                    if (b.bit()) {
                        cl[--start] = c;
                        ml[start] = 3;
                    } else {
                        blk[kScan[c]] = coef(b, bits);
                        idx[count++] = c;
                    }
                }
                break;
            case 1:
                ml[pos] = 2;
                for (int i = 0; i < 3; ++i) add(c += 4, 2);
                break;
            default:   // 3
                blk[kScan[c]] = coef(b, bits);
                idx[count++] = c;
                cl[pos] = 0;
                ml[pos++] = 0;
                break;
            }
        }
    }
    return int(b.get(4));
}

// A residue block (added to a motion-compensated copy): the same work
// list over all 64 coefficients (groups 4.., 24.., 44.. and 0..3 as a
// mode-2 group), coded from the bit 1 << (3 bits) down; each coefficient
// already set first gets a refinement bit per plane (+- the plane's bit
// away from zero). `masks` (7 bits) caps the number of coded bits.
void readResidue(Bits& b, int32_t blk[64], int masks) {
    int cl[128], ml[128], nz[64];
    int start = 64, end = 64, nzc = 0;
    auto add = [&](int c, int m) { cl[end] = c; ml[end++] = m; };
    add(4, 0);
    add(24, 0);
    add(44, 0);
    add(0, 2);
    for (int mask = 1 << b.get(3); mask; mask >>= 1) {
        for (int i = 0; i < nzc; ++i) {
            if (!b.bit()) continue;
            blk[nz[i]] += blk[nz[i]] < 0 ? -mask : mask;
            if (--masks < 0) return;
        }
        int pos = start;
        while (pos < end) {
            if (!(cl[pos] | ml[pos]) || !b.bit()) { ++pos; continue; }
            int c = cl[pos];
            switch (ml[pos]) {
            case 0:
            case 2:
                if (ml[pos] == 0) {
                    cl[pos] = c + 4;
                    ml[pos] = 1;
                } else {
                    cl[pos] = 0;
                    ml[pos++] = 0;
                }
                for (int i = 0; i < 4; ++i, ++c) {
                    if (b.bit()) {
                        cl[--start] = c;
                        ml[start] = 3;
                    } else {
                        nz[nzc++] = kScan[c];
                        blk[kScan[c]] = b.bit() ? -mask : mask;
                        if (--masks < 0) return;
                    }
                }
                break;
            case 1:
                ml[pos] = 2;
                for (int i = 0; i < 3; ++i) add(c += 4, 2);
                break;
            default:
                nz[nzc++] = kScan[c];
                blk[kScan[c]] = b.bit() ? -mask : mask;
                cl[pos] = 0;
                ml[pos++] = 0;
                if (--masks < 0) return;
                break;
            }
        }
    }
}

void dequant(int32_t blk[64], const uint32_t q[64], const int idx[64], int count) {
    blk[0] = int32_t(uint32_t(blk[0]) * q[0]) >> 11;
    for (int i = 0; i < count; ++i) {
        const int k = kScan[idx[i]];
        blk[k] = int32_t(uint32_t(blk[k]) * q[k]) >> 11;
    }
}

// The 16 symbols' order for a tree: tree 0 is the identity; otherwise
// either the first 1..8 symbols explicitly (4 bits each, the rest in
// increasing order), or merge sorts of 1..4 passes from the identity, a
// bit per output choosing the left or the right run.
void readTree(Bits& b, int& table, std::array<uint8_t, 16>& syms) {
    table = int(b.get(4));
    for (int i = 0; i < 16; ++i) syms[size_t(i)] = uint8_t(i);
    if (!table) return;
    if (b.bit()) {
        bool used[16] = {};
        int len = int(b.get(3));
        for (int i = 0; i <= len; ++i) {
            syms[size_t(i)] = uint8_t(b.get(4));
            used[syms[size_t(i)]] = true;
        }
        for (int i = 0; i < 16 && len < 15; ++i)
            if (!used[i]) syms[size_t(++len)] = uint8_t(i);
    } else {
        const int depth = int(b.get(2));
        uint8_t in[16], out[16];
        for (int i = 0; i < 16; ++i) in[i] = uint8_t(i);
        for (int pass = 0; pass <= depth; ++pass) {
            const int size = 1 << pass;
            for (int t = 0; t < 16; t += size * 2) {
                const uint8_t *l = in + t, *r = in + t + size;
                int nl = size, nr = size;
                uint8_t* o = out + t;
                while (nl && nr) {
                    if (!b.bit()) { *o++ = *l++; --nl; }
                    else { *o++ = *r++; --nr; }
                }
                while (nl--) *o++ = *l++;
                while (nr--) *o++ = *r++;
            }
            std::memcpy(in, out, 16);
        }
        std::memcpy(syms.data(), in, 16);
    }
}

} // namespace

bool BinkVideo::open(const std::filesystem::path& path) {
    *this = BinkVideo();
    file_.open(path, std::ios::binary);
    if (!file_) return setError("cannot open file");
    uint8_t h[44];
    file_.read(reinterpret_cast<char*>(h), sizeof h);
    if (file_.gcount() < 44 || std::memcmp(h, "BIK", 3) != 0) return setError("not a Bink file");
    revision_ = char(h[3]);
    if (revision_ < 'c' || revision_ > 'k') return setError(std::string("Bink revision '") + revision_ + "' not supported");
    const uint32_t frames = u32(h + 8);
    width_ = int(u32(h + 20));
    height_ = int(u32(h + 24));
    rate_ = int(u32(h + 28));
    scale_ = int(u32(h + 32));
    const uint32_t vflags = u32(h + 36);
    alpha_ = vflags & 0x100000;
    gray_ = vflags & 0x20000;
    tracks_ = int(u32(h + 40));
    if (frames == 0 || frames > 1000000 || width_ <= 0 || height_ <= 0 || width_ > 4096 || height_ > 4096 || rate_ <= 0 ||
        scale_ <= 0 || tracks_ < 0 || tracks_ > 256)
        return setError("bad Bink header");
    std::vector<uint8_t> t(size_t(tracks_) * 12);
    file_.read(reinterpret_cast<char*>(t.data()), std::streamsize(t.size()));
    if (size_t(file_.gcount()) < t.size()) return setError("truncated Bink header");
    if (tracks_ > 0) {
        // Track 0: u32 largest packet at +0, u16 rate and u16 flags after
        // all tracks' sizes.
        const uint8_t* a = t.data() + size_t(tracks_) * 4;
        audioRate_ = u16(a);
        const uint16_t af = u16(a + 2);
        audioChannels_ = af & 0x2000 ? 2 : 1;
        audioDct_ = af & 0x1000;
        // The transform: RDFT files code stereo as one interleaved signal
        // at twice the rate; DCT files each channel on its own.
        int bits = audioRate_ < 22050 ? 9 : audioRate_ < 44100 ? 10 : 11;
        int rate = audioRate_;
        chans_ = audioChannels_;
        if (!audioDct_) {
            rate *= audioChannels_;
            if (audioChannels_ == 2) ++bits;
            chans_ = 1;
        }
        frameLen_ = 1 << bits;
        overlap_ = frameLen_ / 16;
        const int half = (rate + 1) / 2;
        root_ = audioDct_ ? float(frameLen_ / (std::sqrt(double(frameLen_)) * 32768.0))
                          : float(2.0 / (std::sqrt(double(frameLen_)) * 32768.0));
        // Quantisers: e^(0.1529 i), i.e. 10^(0.0664 i) (about 0.66 dB a step).
        for (int i = 0; i < 96; ++i) quant_[size_t(i)] = float(std::exp(i * 0.15289164787221953823)) * root_;
        for (bands_ = 1; bands_ < 25; ++bands_)
            if (half <= kCriticalFreqs[std::min(bands_ - 1, 23)]) break;
        band_[0] = 2;
        for (int i = 1; i < bands_; ++i) band_[size_t(i)] = (kCriticalFreqs[i - 1] * frameLen_ / half) & ~1;
        band_[size_t(bands_)] = frameLen_;
        for (int c = 0; c < 2; ++c) {
            coeffs_[size_t(c)].assign(size_t(frameLen_), 0.f);
            prevOut_[size_t(c)].assign(size_t(overlap_), 0.f);
        }
        if (audioRate_ <= 0) audioRate_ = 0;
    }
    index_.resize(size_t(frames) + 1);
    file_.read(reinterpret_cast<char*>(index_.data()), std::streamsize(index_.size() * 4));
    if (size_t(file_.gcount()) < index_.size() * 4) return setError("truncated frame index");
    // The last entry is the end of the last frame (in DF2INTRO.BIK and
    // DFLWINT.BIK the file size, header +4 + 8).
    for (size_t i = 0; i + 1 < index_.size(); ++i)
        if ((index_[i + 1] & ~1u) < (index_[i] & ~1u)) return setError("bad frame index");
    // Planes: luma in 8x8 blocks, chroma (and the half size) in 16x16;
    // buffers rounded to 16 pixels so a scaled block never leaves them.
    for (int p = 0; p < 4; ++p) {
        const bool chroma = p == 1 || p == 2;
        const int bw = chroma ? (width_ + 15) >> 4 : (width_ + 7) >> 3;
        const int bh = chroma ? (height_ + 15) >> 4 : (height_ + 7) >> 3;
        stride_[size_t(p)] = ((bw + 1) & ~1) * 8;
        rows_[size_t(p)] = ((bh + 1) & ~1) * 8;
        const uint8_t fill = (p == 1 || p == 2) ? 128 : p == 3 ? 255 : 0;
        cur_[size_t(p)].assign(size_t(stride_[size_t(p)]) * size_t(rows_[size_t(p)]), fill);
        prev_[size_t(p)] = cur_[size_t(p)];
    }
    return true;
}

BinkVideo::Step BinkVideo::next() {
    if (frame_ + 1 >= frameCount()) return Step::End;
    ++frame_;
    const uint32_t at = index_[size_t(frame_)] & ~1u, end = index_[size_t(frame_) + 1] & ~1u;
    key_ = index_[size_t(frame_)] & 1;
    const size_t size = end - at;
    packet_.assign(size + 16, 0);
    file_.clear();
    file_.seekg(std::streamoff(at));
    file_.read(reinterpret_cast<char*>(packet_.data()), std::streamsize(size));
    if (size_t(file_.gcount()) < size) { setError("truncated frame"); return Step::Error; }
    size_t pos = 0;
    for (int t = 0; t < tracks_; ++t) {
        if (pos + 4 > size) { setError("bad audio packet"); return Step::Error; }
        const uint32_t n = u32(&packet_[pos]);
        pos += 4;
        if (n > size - pos) { setError("bad audio packet"); return Step::Error; }
        if (t == 0 && n >= 4 && frameLen_ > 0) {
            std::vector<uint8_t> a(packet_.begin() + std::ptrdiff_t(pos), packet_.begin() + std::ptrdiff_t(pos + n));
            a.resize(n + 16, 0);
            decodeAudio(a.data(), n);
        }
        pos += n;
    }
    std::swap(cur_, prev_);
    if (!decodeVideo(packet_.data() + pos, size - pos)) return Step::Error;
    haveRef_ = true;
    return Step::Frame;
}

// ---------------------------------------------------------------- video

bool BinkVideo::decodeVideo(const uint8_t* d, size_t size) {
    // A copy keeps the 8 bytes of padding the reader needs.
    std::vector<uint8_t> buf(d, d + size);
    buf.resize(size + 16, 0);
    Bits b(buf.data(), size);
    // From 'i' each plane but the chroma ones is preceded by its size (u32,
    // skipped); the alpha plane first, then Y and the two chroma planes,
    // in the order U, V before 'h' and V, U from it (DF2INTRO.BIK, 'f', and
    // DFLWINT.BIK, 'i', decode right that way; the switch at 'h' itself is
    // INFERRED, no 'g' or 'h' file was at hand).
    if (alpha_) {
        if (revision_ >= 'i') b.skip(32);
        if (!decodePlane(b, 3, false)) return false;
    }
    if (revision_ >= 'i') b.skip(32);
    for (int i = 0; i < 3; ++i) {
        const int p = i == 0 || revision_ < 'h' ? i : 3 - i;
        if (gray_ && i > 0) {
            // INFERRED: grey files code no chroma; mid grey.
            std::fill(cur_[size_t(p)].begin(), cur_[size_t(p)].end(), uint8_t(128));
            continue;
        }
        if (b.left() == 0) {
            // The data ended: the plane stays as it was (INFERRED).
            cur_[size_t(p)] = prev_[size_t(p)];
            continue;
        }
        if (!decodePlane(b, p, i > 0)) return false;
    }
    return true;
}

bool BinkVideo::readBundleRow(Bits& b, int i) {
    Bundle& u = bundles_[size_t(i)];
    // A bundle reads a new run of values only once the last is used up;
    // a count of 0 ends it for the plane.
    if (u.done || u.dec > u.ptr) return true;
    const int n = int(b.get(u.len));
    if (n == 0) {
        u.done = true;
        return true;
    }
    if (u.dec + size_t(n) > u.data.size()) return setError("Bink bundle overflow");
    int32_t* o = u.data.data() + u.dec;
    auto sym = [&](const Tree& t) { return int(t.syms[size_t(huffSym(b, t.table))]); };
    auto signedSym = [&]() {
        const int v = sym(u.tree);
        return v && b.bit() ? -v : v;
    };
    switch (i) {
    case kBlockTypes:
    case kSubBlockTypes:
        // One type for all (bit 1, 4 bits) or Huffman types where 12..15
        // repeat the last one 4, 8, 12 or 32 times.
        if (b.bit()) {
            std::fill(o, o + n, int32_t(b.get(4)));
        } else {
            int last = 0;
            for (int k = 0; k < n;) {
                const int v = sym(u.tree);
                if (v < 12) {
                    o[k++] = last = v;
                } else {
                    const int run = kTypeRuns[v - 12];
                    if (k + run > n) return setError("Bink block type run overflow");
                    std::fill(o + k, o + k + run, int32_t(last));
                    k += run;
                }
            }
        }
        break;
    case kColors: {
        // Bytes: the high nibble from one of 16 trees picked by the last
        // high nibble, the low nibble from the bundle's tree. Before 'i'
        // the byte is sign and magnitude around 128.
        auto color = [&]() {
            colLast_ = sym(colHigh_[size_t(colLast_)]);
            int v = colLast_ << 4 | sym(u.tree);
            if (revision_ < 'i') {
                const int m = v & 0x7f;
                v = (v & 0x80 ? -m : m) + 128;
            }
            return v & 0xff;
        };
        if (b.bit()) std::fill(o, o + n, int32_t(color()));
        else for (int k = 0; k < n; ++k) o[k] = color();
        break;
    }
    case kPattern:
        // A row of 8 pattern bits: two Huffman nibbles, low first.
        for (int k = 0; k < n; ++k) {
            const int lo = sym(u.tree);
            o[k] = lo | sym(u.tree) << 4;
        }
        break;
    case kXOff:
    case kYOff:
        // Motion offsets -15..15: 4 bits + sign for all, or Huffman + sign.
        if (b.bit()) {
            int v = int(b.get(4));
            if (v && b.bit()) v = -v;
            std::fill(o, o + n, int32_t(v));
        } else {
            for (int k = 0; k < n; ++k) o[k] = signedSym();
        }
        break;
    case kIntraDc:
    case kInterDc: {
        // DCs: the first in 11 bits (inter: 10 + sign), then per group of 8
        // a 4-bit width and as many differences (+ sign if non-zero).
        const bool sgn = i == kInterDc;
        int v = int(b.get(sgn ? 10 : 11));
        if (v && sgn && b.bit()) v = -v;
        o[0] = v;
        for (int k = 1; k < n; k += 8) {
            const int cnt = std::min(n - k, 8), w = int(b.get(4));
            for (int j = 0; j < cnt; ++j) {
                if (w) {
                    int dv = int(b.get(w));
                    if (dv && b.bit()) dv = -dv;
                    v += dv;
                    if (v < -32768 || v > 32767) return setError("Bink DC overflow");
                }
                o[k + j] = v;
            }
        }
        break;
    }
    case kRun:
        if (b.bit()) std::fill(o, o + n, int32_t(b.get(4)));
        else for (int k = 0; k < n; ++k) o[k] = sym(u.tree);
        break;
    }
    u.dec += size_t(n);
    return true;
}

int BinkVideo::value(int i) {
    Bundle& u = bundles_[size_t(i)];
    // Damaged data can read past what was decoded: 0 (INFERRED).
    return u.ptr < u.data.size() ? u.data[u.ptr++] : 0;
}

bool BinkVideo::decodePlane(Bits& b, int p, bool chroma) {
    const int bw = chroma ? (width_ + 15) >> 4 : (width_ + 7) >> 3;
    const int bh = chroma ? (height_ + 15) >> 4 : (height_ + 7) >> 3;
    const int stride = stride_[size_t(p)];
    uint8_t* const cur = cur_[size_t(p)].data();
    const uint8_t* const ref = (haveRef_ ? prev_ : cur_)[size_t(p)].data();
    // 'k': a bit for a plane of one value (8 bits).
    if (revision_ == 'k' && b.bit()) {
        std::fill(cur_[size_t(p)].begin(), cur_[size_t(p)].end(), uint8_t(b.get(8)));
        b.align32();
        return true;
    }
    // The bit widths of the per-row value counts, from the plane width (at
    // least 8): enough for a row of blocks (and its pixels for colours,
    // pattern rows and runs).
    const int w = (std::max(width_ >> (chroma ? 1 : 0), 8) + 7) & ~7;
    auto bitsFor = [](int v) { int n = 0; while (v >> n) ++n; return n; };   // floor(log2 v) + 1
    bundles_[kBlockTypes].len = bitsFor((w >> 3) + 511);
    bundles_[kSubBlockTypes].len = bitsFor((w >> 4) + 511);
    bundles_[kColors].len = bitsFor(bw * 64 + 511);
    bundles_[kIntraDc].len = bundles_[kInterDc].len = bundles_[kXOff].len = bundles_[kYOff].len = bitsFor((w >> 3) + 511);
    bundles_[kPattern].len = bitsFor((bw << 3) + 511);
    bundles_[kRun].len = bitsFor(bw * 48 + 511);
    // The plane header: the bundles' trees (the colours' 16 high-nibble
    // trees first; the DC bundles have none).
    const size_t cap = size_t(bw) * size_t(bh) * 64 + 64;
    for (int i = 0; i < kBundles; ++i) {
        Bundle& u = bundles_[size_t(i)];
        if (i == kColors) {
            for (auto& t : colHigh_) readTree(b, t.table, t.syms);
            colLast_ = 0;
        }
        if (i != kIntraDc && i != kInterDc) readTree(b, u.tree.table, u.tree.syms);
        if (u.data.size() < cap) u.data.assign(cap, 0);
        u.dec = u.ptr = 0;
        u.done = false;
    }
    if (b.over) return setError("Bink plane header truncated");
    // A motion block's source must start inside the previous frame's
    // blocks (as a linear offset, so it may run into the next row).
    const std::ptrdiff_t refEnd = (bw - 1) * 8 + std::ptrdiff_t(stride) * (bh - 1) * 8;
    int32_t blk[64];
    int idx[64], count;
    uint8_t ub[64];
    auto motion = [&](const uint8_t* at, const uint8_t*& src) {
        const int x = value(kXOff), y = value(kYOff);
        const std::ptrdiff_t o = (at - ref) + x + std::ptrdiff_t(y) * stride;
        src = ref + std::clamp<std::ptrdiff_t>(o, 0, refEnd);
        return o >= 0 && o <= refEnd;
    };
    auto copy8 = [&](uint8_t* dst, const uint8_t* src) {
        for (int r = 0; r < 8; ++r) std::memcpy(dst + r * stride, src + r * stride, 8);
    };
    // Run blocks: a 4-bit pattern (pixel order), then runs of a value
    // (bit 1: one colour for the run) or of colours, up to 64 pixels.
    auto runs = [&](auto put) {
        const uint8_t* scan = kPatterns[b.get(4)];
        int i = 0;
        do {
            const int run = value(kRun) + 1;
            i += run;
            if (i > 64) return false;
            if (b.bit()) {
                const int v = value(kColors);
                for (int j = 0; j < run; ++j) put(*scan++, v);
            } else {
                for (int j = 0; j < run; ++j) put(*scan++, value(kColors));
            }
        } while (i < 63);
        if (i == 63) put(*scan++, value(kColors));
        return true;
    };
    for (int by = 0; by < bh; ++by) {
        for (int i = 0; i < kBundles; ++i)
            if (!readBundleRow(b, i)) return false;
        uint8_t* dst = cur + size_t(by) * 8 * size_t(stride);
        const uint8_t* prev = ref + size_t(by) * 8 * size_t(stride);
        for (int bx = 0; bx < bw; ++bx, dst += 8, prev += 8) {
            int type = value(kBlockTypes);
            // A 16x16 block's other three 8x8s: their types are coded once
            // per row (odd rows and odd columns skip two).
            if (((by & 1) || (bx & 1)) && type == kScaled) {
                ++bx;
                dst += 8;
                prev += 8;
                continue;
            }
            const uint8_t* src;
            switch (type) {
            case kSkip:
                copy8(dst, prev);
                break;
            case kScaled: {
                // An 8x8 sub-block drawn at twice the size.
                const int sub = value(kSubBlockTypes);
                switch (sub) {
                case kRunBlock:
                    if (!runs([&](int at, int v) { ub[at] = uint8_t(v); })) return setError("Bink run overflow");
                    break;
                case kIntra:
                    std::memset(blk, 0, sizeof blk);
                    blk[0] = value(kIntraDc);
                    dequant(blk, kIntraQuant[readDct(b, blk, idx, count) & 15], idx, count);
                    idct(blk, ub, 8, false);
                    break;
                case kFill: {
                    const uint8_t v = uint8_t(value(kColors));
                    for (int r = 0; r < 16; ++r) std::memset(dst + r * stride, v, 16);
                    break;
                }
                case kPatternBlock: {
                    const int c0 = value(kColors), c1 = value(kColors);
                    for (int r = 0; r < 8; ++r) {
                        int v = value(kPattern);
                        for (int k = 0; k < 8; ++k, v >>= 1) ub[r * 8 + k] = uint8_t(v & 1 ? c1 : c0);
                    }
                    break;
                }
                case kRaw:
                    for (int k = 0; k < 64; ++k) ub[k] = uint8_t(value(kColors));
                    break;
                default:
                    return setError("Bink: bad scaled sub-block type " + std::to_string(sub));
                }
                if (sub != kFill)
                    for (int r = 0; r < 16; ++r)
                        for (int k = 0; k < 16; ++k) dst[r * stride + k] = ub[(r >> 1) * 8 + (k >> 1)];
                ++bx;
                dst += 8;
                prev += 8;
                break;
            }
            case kMotion:
                if (!motion(prev, src)) return setError("Bink motion vector out of the frame");
                copy8(dst, src);
                break;
            case kRunBlock:
                if (!runs([&](int at, int v) { dst[(at >> 3) * stride + (at & 7)] = uint8_t(v); }))
                    return setError("Bink run overflow");
                break;
            case kResidue:
                if (!motion(prev, src)) return setError("Bink motion vector out of the frame");
                copy8(dst, src);
                std::memset(blk, 0, sizeof blk);
                readResidue(b, blk, int(b.get(7)));
                // Added modulo 256, as the inter DCT blocks.
                for (int r = 0; r < 8; ++r)
                    for (int k = 0; k < 8; ++k) dst[r * stride + k] = uint8_t(dst[r * stride + k] + blk[r * 8 + k]);
                break;
            case kIntra:
                std::memset(blk, 0, sizeof blk);
                blk[0] = value(kIntraDc);
                dequant(blk, kIntraQuant[readDct(b, blk, idx, count) & 15], idx, count);
                idct(blk, dst, stride, false);
                break;
            case kFill: {
                const uint8_t v = uint8_t(value(kColors));
                for (int r = 0; r < 8; ++r) std::memset(dst + r * stride, v, 8);
                break;
            }
            case kInter:
                if (!motion(prev, src)) return setError("Bink motion vector out of the frame");
                copy8(dst, src);
                std::memset(blk, 0, sizeof blk);
                blk[0] = value(kInterDc);
                dequant(blk, kInterQuant[readDct(b, blk, idx, count) & 15], idx, count);
                idct(blk, dst, stride, true);
                break;
            case kPatternBlock: {
                const int c0 = value(kColors), c1 = value(kColors);
                for (int r = 0; r < 8; ++r) {
                    int v = value(kPattern);
                    for (int k = 0; k < 8; ++k, v >>= 1) dst[r * stride + k] = uint8_t(v & 1 ? c1 : c0);
                }
                break;
            }
            case kRaw:
                for (int r = 0; r < 8; ++r)
                    for (int k = 0; k < 8; ++k) dst[r * stride + k] = uint8_t(value(kColors));
                break;
            default:
                return setError("Bink: bad block type " + std::to_string(type));
            }
        }
    }
    // The next plane starts on a 32-bit boundary.
    b.align32();
    return true;
}

void BinkVideo::toRgba(std::vector<uint8_t>& rgba) const {
    rgba.resize(size_t(width_) * size_t(height_) * 4);
    const uint8_t *Y = cur_[0].data(), *U = cur_[1].data(), *V = cur_[2].data(), *A = cur_[3].data();
    for (int y = 0; y < height_; ++y) {
        uint8_t* o = &rgba[size_t(y) * size_t(width_) * 4];
        const uint8_t* yr = Y + size_t(y) * size_t(stride_[0]);
        const uint8_t* ur = U + size_t(y >> 1) * size_t(stride_[1]);
        const uint8_t* vr = V + size_t(y >> 1) * size_t(stride_[2]);
        const uint8_t* ar = A + size_t(y) * size_t(stride_[3]);
        for (int x = 0; x < width_; ++x, o += 4) {
            // x 2^16: 1.164 (Y - 16), 1.596 V, 0.392 U, 0.813 V, 2.017 U.
            const int c = (yr[x] - 16) * 76309, u = ur[x >> 1] - 128, v = vr[x >> 1] - 128;
            o[0] = clip8((c + 104597 * v + 32768) >> 16);
            o[1] = clip8((c - 25675 * u - 53279 * v + 32768) >> 16);
            o[2] = clip8((c + 132201 * u + 32768) >> 16);
            o[3] = alpha_ ? ar[x] : 255;
        }
    }
}

// ---------------------------------------------------------------- audio

void BinkVideo::decodeAudio(const uint8_t* d, size_t size) {
    Bits b(d, size);
    // The packet's first u32: the bytes it decodes to (16-bit samples).
    reported_ += b.get(32) / 2;
    // Blocks to the end of the packet, each 32-bit aligned (a block needs
    // at least its two floats and band quantisers).
    const size_t minBits = (audioDct_ ? 2 : 0) + size_t(chans_) * (58 + size_t(bands_) * 8);
    while (b.left() >= minBits)
        if (!audioBlock(b)) break;
}

bool BinkVideo::audioBlock(Bits& b) {
    auto getFloat = [&]() {
        // 5-bit exponent, 23-bit mantissa, sign: m x 2^(e - 23).
        const int e = int(b.get(5));
        const float f = std::ldexp(float(b.get(23)), e - 23);
        return b.bit() ? -f : f;
    };
    if (audioDct_) b.skip(2);   // DCT blocks start with 2 unused bits
    const int n = frameLen_;
    for (int ch = 0; ch < chans_; ++ch) {
        float* c = coeffs_[size_t(ch)].data();
        c[0] = getFloat() * root_;
        c[1] = getFloat() * root_;
        float q[26];
        for (int i = 0; i < bands_; ++i) q[i] = quant_[size_t(std::min(int(b.get(8)), 95))];
        // Coefficients from 2 on in groups: a group of 8 (bit 0) or of
        // kAudioRuns[4 bits] x 8 (bit 1), then a 4-bit width: 0 zeros the
        // group, else each coefficient is `width` bits (+ sign if not 0)
        // times its band's quantiser.
        int k = 0;
        float qq = q[0];
        for (int i = 2; i < n;) {
            int j = b.bit() ? i + kAudioRuns[b.get(4)] * 8 : i + 8;
            j = std::min(j, n);
            const int width = int(b.get(4));
            if (width == 0) {
                std::fill(c + i, c + j, 0.f);
                i = j;
                while (band_[size_t(k)] < i) qq = q[k++];
            } else {
                for (; i < j; ++i) {
                    if (band_[size_t(k)] == i) qq = q[k++];
                    const int v = int(b.get(width));
                    c[i] = v == 0 ? 0.f : (b.bit() ? -qq : qq) * float(v);
                }
            }
        }
        if (b.over) return false;
        // The transform into n samples.
        if (audioDct_) {
            // DCT-III, scaled by 2/n: x_t = (2/n) (c_0 + sum_k c_k cos(pi k
            // (t + 1/2) / n)), as a 2n-point inverse FFT of c_k e^(i pi k /
            // 2n). (The scale was settled by comparing the output with an
            // independent decoder's, to 1 LSB; RAD's code was not traced.)
            fft_.assign(size_t(n) * 4, 0.f);
            for (int i = 0; i < n; ++i) {
                const double a = M_PI * i / (2.0 * n);
                fft_[size_t(i) * 2] = float(c[i] * std::cos(a));
                fft_[size_t(i) * 2 + 1] = float(c[i] * std::sin(a));
            }
            fftInverse(fft_.data(), 2 * n);
            const float scale = 2.f / float(n);
            for (int i = 0; i < n; ++i) c[i] = fft_[size_t(i) * 2] * scale;
        } else {
            // Inverse real DFT: c_0 the DC term, c_1 the Nyquist term, then
            // (cos, sin) pairs: x_t = c_0/2 + c_1/2 (-1)^t + sum_k (c_2k
            // cos(2 pi k t / n) + c_2k+1 sin(2 pi k t / n)), k = 1..n/2-1,
            // i.e. Re(sum Z_k e^(2 pi i k t / n)) with Z_k = c_2k - i c_2k+1.
            fft_.assign(size_t(n) * 2, 0.f);
            fft_[0] = c[0] * 0.5f;
            fft_[size_t(n)] = c[1] * 0.5f;
            for (int i = 1; i < n / 2; ++i) {
                fft_[size_t(i) * 2] = c[2 * i];
                fft_[size_t(i) * 2 + 1] = -c[2 * i + 1];
            }
            fftInverse(fft_.data(), n);
            for (int i = 0; i < n; ++i) c[i] = fft_[size_t(i) * 2];
        }
    }
    // Overlap: the first n/16 samples fade in over the last n/16 of the
    // block before (the weights stepping by the channel count, interleaved
    // positions); those last ones are kept for the next block, the rest out.
    const int count = overlap_ * chans_;
    for (int ch = 0; ch < chans_; ++ch) {
        float* c = coeffs_[size_t(ch)].data();
        float* p = prevOut_[size_t(ch)].data();
        if (!first_)
            for (int i = 0, j = ch; i < overlap_; ++i, j += chans_)
                c[i] = (p[i] * float(count - j) + c[i] * float(j)) / float(count);
        std::memcpy(p, c + n - overlap_, size_t(overlap_) * sizeof(float));
    }
    first_ = false;
    const int outN = n - overlap_;
    const size_t base = pcm_.size();
    pcm_.resize(base + size_t(outN) * size_t(chans_));
    for (int i = 0; i < outN; ++i)
        for (int ch = 0; ch < chans_; ++ch)
            pcm_[base + size_t(i) * size_t(chans_) + size_t(ch)] =
                int16_t(std::clamp(std::lround(coeffs_[size_t(ch)][size_t(i)] * 32768.f), -32768L, 32767L));
    decoded_ += uint64_t(outN) * uint64_t(chans_);
    b.align32();
    return true;
}

} // namespace df
