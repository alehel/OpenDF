// RTXT string tables (DFGAME.BIN, DFCAMP0x.BIN, DFDLG0x.BIN, ...): the
// game's own text — weapon names, mission names, briefings, captions.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace df {

// Layout: "RTXT", u32 text size, u32 (unknown), u32 count; then count
// 16-byte entries (u32 offset into the text block, 12 unused bytes); then the
// NUL-terminated strings.
bool parseRtxt(const std::vector<uint8_t>& file, std::vector<std::string>& out);

} // namespace df
