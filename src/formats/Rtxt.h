// RTXT string tables (DFGAME.BIN, DFCAMP0x.BIN, DFDLG0x.BIN, ...): the
// game's own text — weapon names, mission names, briefings, captions.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace df {

// Layout: "RTXT", u32 key-block offset, u32 key-block size, u32 count; then
// count 16-byte entries (u32 text offset, u32 briefing-map position or 0,
// u32 section index, u32 0); then the NUL-terminated strings. The key block:
// u32 section count, per section (u32 offset of its first key name, u32
// entry count) with offsets from the block start + 4, then the section names,
// then one key name per entry. DF.EXE looks text up by section and key, e.g.
// ("Waypoint Names", "WPN008") -> "IP".
bool parseRtxt(const std::vector<uint8_t>& file, std::vector<std::string>& out);

class TextTable {
public:
    bool parse(const std::vector<uint8_t>& file);
    const std::vector<std::string>& text() const { return text_; }
    // Null if the section or key does not exist.
    const std::string* find(const std::string& section, const std::string& key) const;
    bool hasSection(const std::string& section) const;
    // An entry's second dword: a briefing-map point (u16 x, u16 y), or 0.
    uint32_t position(const std::string& section, const std::string& key) const;

private:
    struct Section { std::string name; int first = 0, count = 0; };
    std::vector<std::string> text_, keys_;
    std::vector<uint32_t> positions_;
    std::vector<Section> sections_;
};

} // namespace df
