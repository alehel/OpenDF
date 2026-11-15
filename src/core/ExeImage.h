// Read-only access to the original DF.EXE's static data (Win32 PE image),
// so engine values that only exist inside the executable — weapon tables and
// the like — come from the user's own copy rather than being copied into
// OpenDF.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace df {

class ExeImage {
public:
    bool load(const std::filesystem::path& path, std::string* error);
    bool loaded() const { return !data_.empty(); }

    // Reads a 32-bit value at a virtual address; false if it is not mapped.
    bool u32(uint32_t va, uint32_t& out) const;
    bool i32(uint32_t va, int32_t& out) const;
    // A zero-terminated string of at most `max` bytes at a virtual address.
    std::string str(uint32_t va, size_t max) const;

private:
    struct Section { uint32_t va, size, fileOffset, fileSize; };
    std::vector<uint8_t> data_;
    std::vector<Section> sections_;
    uint32_t imageBase_ = 0;
};

// Finds DF.EXE next to the game's PFF archives (case-insensitive).
std::filesystem::path findGameExe(const std::filesystem::path& gameDir);
// Delta Force 2's DF2.EXE next to its archives (DF2SETUP, case-insensitive);
// read only for DF2's own tables, never by the DF.EXE readers.
std::filesystem::path findDf2Exe(const std::filesystem::path& gameDir);

// Which Delta Force 2 executable a loaded image is: the CD's DF2.EXE or the
// GOG release's Df2.exe 1.06.15, which keeps its tables at other addresses
// (and its ammo records are 100 bytes, not 96). Told apart by the M4's ammo
// record (7: a 30-round magazine at 915 m/s), CD 0x4C4310 + 7·96 (+0x08,
// +0x0C) or GOG Df2.exe 0x4D95B8 + 7·100 (+0x08, +0x10).
enum class Df2Build { Unknown, Cd, Gog };
Df2Build df2Build(const ExeImage& exe);

} // namespace df
