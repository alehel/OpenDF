#include "ExeImage.h"

#include "Pff.h"

#include <fstream>

namespace fs = std::filesystem;

namespace df {

bool ExeImage::load(const fs::path& path, std::string* error) {
    auto fail = [&](const char* m) { data_.clear(); if (error) *error = m; return false; };
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
    return sections_.empty() ? fail("the game executable has no sections") : true;
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

Df2Build df2Build(const ExeImage& exe) {
    if (!exe.loaded()) return Df2Build::Unknown;
    int32_t mag = 0, speed = 0;
    if (exe.i32(0x4D95B8 + 7 * 100 + 0x08, mag) && exe.i32(0x4D95B8 + 7 * 100 + 0x10, speed) && mag == 30 && speed == 915)
        return Df2Build::Gog;
    if (exe.i32(0x4C4310 + 7 * 96 + 0x08, mag) && exe.i32(0x4C4310 + 7 * 96 + 0x0C, speed) && mag == 30 && speed == 915)
        return Df2Build::Cd;
    return Df2Build::Unknown;
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
