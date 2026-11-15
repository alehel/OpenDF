#include "ExeImage.h"

#include "Pff.h"

#include <cstdio>
#include <fstream>

namespace fs = std::filesystem;

namespace df {

namespace {

uint32_t crc32(const std::vector<uint8_t>& data) {
    uint32_t table[256];
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// The known builds, by file size and CRC-32 (docs/FORMATS.md "Game
// executables"). Each table was found through the code that uses it in both
// builds (CD address / GOG address).
struct KnownBuild { ExeBuild build; size_t size; uint32_t crc; const char* name; };
constexpr KnownBuild kBuilds[] = {
    {ExeBuild::Df1Cd, 1043468, 0x63B1DB57u, "DF.EXE (CD)"},
    {ExeBuild::Df1Gog, 1102860, 0x7CED8382u, "Df.exe (GOG)"},
    {ExeBuild::Df2Cd, 835584, 0xA0947236u, "DF2.EXE (CD)"},
    {ExeBuild::Df2Gog, 921600, 0x4CED7D2Cu, "Df2.exe (GOG)"},
};

// DF.EXE 0x470580 / GOG Df.exe 0x46bfb0 store the gun, row and mode table
// pointers; rows are 16 bytes in the CD build and 20 in GOG's (GOG Df.exe
// 0x484970 reads row r's modes at r·0x14 + 4). Keys: 0x41cbc0 / 0x41cd30.
// Sounds: 0x472e70 / 0x43e340 (sorted from record 1, 0x4eb1bc / 0x4f2ecc).
// Voice sets: 0x473f70 / 0x43f5a0.
constexpr Df1ExeTables kDf1Cd{0x4E97D0, 0x4E9060, 0x4E9610, 16, 0x4DF4F8, 0x4EB194, 0x4F019C};
constexpr Df1ExeTables kDf1Gog{0x4F9934, 0x4F9154, 0x4F9704, 20, 0x4FB300, 0x4F2EA4, 0x4F7EAC};

// DF2.EXE 0x46b230 / GOG Df2.exe 0x4758f0 store the ammunition, type, mode
// and drag curve pointers and counts: GOG has 39 ammunition types of 100
// bytes (a word inserted at +0x0C: velocity at +0x10, flags +0x34, drag
// from +0x38, grains +0x60) and keeps the flags 16 bits higher (the bullet
// set-up DF2.EXE 0x44a6a0 tests +0x30 for 0x1, 0x10, 0x1000, 0x40, 0x800;
// GOG Df2.exe 0x451ae0 tests +0x34 for 0x10000, 0x100000, ...). Keys:
// 0x200 records from 0x4a5ca8 (44 bytes, DF2.EXE 0x41c700) / 0x4b1710
// (0x70 bytes, GOG Df2.exe 0x41e370); OpenDF reads from record 1. Sounds:
// 0x45da50 / 0x467880. Voice sets: 0x448240 / 0x44f340.
constexpr Df2ExeTables kDf2Cd{0x4C7238, 0x4C5150, 0x4C4310, 96, 38, 0, 0, 0x4C86B8, 0x4A5CD4, 44, 0x4B85A8, 0x4B70FC};
constexpr Df2ExeTables kDf2Gog{0x4DC5E0, 0x4DA4F8, 0x4D95B8, 100, 39, 4, 16, 0x4DDA60, 0x4B1780, 0x70, 0x4CD7C8, 0x4CBCC0};

} // namespace

const char* ExeImage::buildName() const {
    for (const auto& b : kBuilds)
        if (b.build == build_) return b.name;
    return "unknown build";
}

const Df1ExeTables* ExeImage::df1() const {
    if (!loaded()) return nullptr;
    return build_ == ExeBuild::Df1Cd ? &kDf1Cd : build_ == ExeBuild::Df1Gog ? &kDf1Gog : nullptr;
}

const Df2ExeTables* ExeImage::df2() const {
    if (!loaded()) return nullptr;
    return build_ == ExeBuild::Df2Cd ? &kDf2Cd : build_ == ExeBuild::Df2Gog ? &kDf2Gog : nullptr;
}

bool ExeImage::load(const fs::path& path, std::string* error) {
    auto fail = [&](const std::string& m) {
        data_.clear();
        build_ = ExeBuild::Unknown;
        if (error) *error = m;
        return false;
    };
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail("cannot open the game executable");
    data_.assign(std::istreambuf_iterator<char>(f), {});
    auto rd = [&](size_t o) -> uint32_t {
        return o + 4 <= data_.size() ? uint32_t(data_[o]) | uint32_t(data_[o + 1]) << 8 | uint32_t(data_[o + 2]) << 16 |
                                           uint32_t(data_[o + 3]) << 24
                                     : 0;
    };
    auto rd16 = [&](size_t o) -> uint32_t { return o + 2 <= data_.size() ? uint32_t(data_[o] | data_[o + 1] << 8) : 0; };
    if (data_.size() < 0x40 || data_[0] != 'M' || data_[1] != 'Z') return fail("the game executable is not a PE file");
    const uint32_t pe = rd(0x3C);
    if (pe + 24 > data_.size() || rd(pe) != 0x00004550) return fail("the game executable has no PE header");
    const uint32_t sections = rd16(pe + 6), optSize = rd16(pe + 20);
    imageBase_ = rd(pe + 24 + 28);
    // Watcom-linked: VirtualSize may be 0, so use the larger of the two sizes.
    sections_.clear();
    for (uint32_t i = 0; i < sections; ++i) {
        const size_t s = pe + 24 + optSize + size_t(i) * 40;
        if (s + 40 > data_.size()) break;
        Section sec{rd(s + 12), rd(s + 8), rd(s + 20), rd(s + 16)};
        if (sec.size < sec.fileSize) sec.size = sec.fileSize;
        sections_.push_back(sec);
    }
    if (sections_.empty()) return fail("the game executable has no sections");
    // Only a known build's tables are read: another build keeps them at other
    // addresses, so it is treated like a missing executable.
    const uint32_t crc = crc32(data_);
    build_ = ExeBuild::Unknown;
    for (const auto& b : kBuilds)
        if (b.size == data_.size() && b.crc == crc) build_ = b.build;
    if (build_ == ExeBuild::Unknown) {
        char m[200];
        std::snprintf(m, sizeof m, "%s is an unknown build (%zu bytes, CRC-32 %08X; OpenDF reads the CD and GOG releases)",
                      path.filename().string().c_str(), data_.size(), unsigned(crc));
        return fail(m);
    }
    return true;
}

bool ExeImage::u32(uint32_t va, uint32_t& out) const {
    const uint32_t rva = va - imageBase_;
    for (const auto& s : sections_) {
        if (rva < s.va || rva + 4 > s.va + s.fileSize) continue;
        const size_t o = s.fileOffset + (rva - s.va);
        if (o + 4 > data_.size()) return false;
        out = uint32_t(data_[o]) | uint32_t(data_[o + 1]) << 8 | uint32_t(data_[o + 2]) << 16 | uint32_t(data_[o + 3]) << 24;
        return true;
    }
    return false;
}

std::string ExeImage::str(uint32_t va, size_t max) const {
    std::string out;
    const uint32_t rva = va - imageBase_;
    for (const auto& sec : sections_) {
        if (rva < sec.va || rva >= sec.va + sec.fileSize) continue;
        for (size_t o = sec.fileOffset + (rva - sec.va); o < data_.size() && out.size() < max && data_[o]; ++o) out += char(data_[o]);
        break;
    }
    return out;
}

bool ExeImage::i32(uint32_t va, int32_t& out) const {
    uint32_t v;
    if (!u32(va, v)) return false;
    out = int32_t(v);
    return true;
}

fs::path findDf2Exe(const fs::path& gameDir) {
    for (const fs::path& dir : {gameDir, gameDir / "DF2SETUP", gameDir / "df2setup"}) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (toUpper(e.path().filename().string()) == "DF2.EXE") return e.path();
    }
    return {};
}

fs::path findGameExe(const fs::path& gameDir) {
    // Delta Force's DF.EXE (the later games' executables are not read: their
    // tables are at other addresses, docs/OTHER-GAMES.md).
    for (const fs::path& dir : {gameDir, gameDir / "DFSETUP", gameDir / "dfsetup"}) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (toUpper(e.path().filename().string()) == "DF.EXE") return e.path();
    }
    return {};
}

} // namespace df
