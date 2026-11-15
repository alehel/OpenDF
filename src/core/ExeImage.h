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

// The builds of the game executables OpenDF knows (docs/FORMATS.md "Game
// executables"): the US retail CDs and the GOG release (the reference).
enum class ExeBuild { Unknown, Df1Cd, Df1Gog, Df2Cd, Df2Gog };

// Where DF.EXE keeps the static tables OpenDF reads.
struct Df1ExeTables {
    uint32_t modes;       // 39 player weapon modes of 168 bytes
    uint32_t guns;        // 28 guns of 52 bytes
    uint32_t rows;        // 28 loadout rows (modes at +4, +8, +0xC)
    uint32_t rowRecord;   // 16 bytes (CD), 20 (GOG)
    uint32_t keys;        // key bindings: name at +0, key at +32, 44 bytes each
    uint32_t sounds;      // 0x200 sound records of 40 bytes
    uint32_t phrases;     // 16 voice sets of 12 sound ids
};

// Where DF2.EXE keeps them.
struct Df2ExeTables {
    uint32_t modes;       // 57 weapon modes of 92 bytes
    uint32_t types;       // 39 weapon types of 216 bytes
    uint32_t ammo;        // ammunition types
    uint32_t ammoRecord;  // 96 bytes (CD), 100 (GOG)
    int ammoCount;        // 38 (CD), 39 (GOG)
    uint32_t ammoShift;   // GOG inserts a word at +0x0C: CD offsets >= 0x0C move by 4
    int ammoFlagShift;    // GOG keeps the CD's flag bits 16 higher (CD 0x1 = GOG 0x10000)
    uint32_t curve;       // 44 drag curve points of 12 bytes
    uint32_t keys;        // key bindings from record 1: name at +4, key at +0x24
    uint32_t keyRecord;   // 44 bytes (CD), 0x70 (GOG)
    uint32_t sounds;      // 469 sound records of 40 bytes (DF1's layout)
    uint32_t phrases;     // 22 voice sets of 12 sound ids
};

class ExeImage {
public:
    bool load(const std::filesystem::path& path, std::string* error);
    bool loaded() const { return !data_.empty(); }
    // Which build was loaded (by file size and CRC-32); Unknown for any
    // other file, whose tables are then not read.
    ExeBuild build() const { return build_; }
    const char* buildName() const;
    // The table addresses of a known DF.EXE / DF2.EXE build, else nullptr.
    const Df1ExeTables* df1() const;
    const Df2ExeTables* df2() const;

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
    ExeBuild build_ = ExeBuild::Unknown;
};

// Finds DF.EXE next to the game's PFF archives (case-insensitive, so GOG's
// Df.exe too).
std::filesystem::path findGameExe(const std::filesystem::path& gameDir);
// Delta Force 2's DF2.EXE next to its archives (DF2SETUP, case-insensitive);
// read only for DF2's own tables, never by the DF.EXE readers.
std::filesystem::path findDf2Exe(const std::filesystem::path& gameDir);

} // namespace df
