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

} // namespace df
