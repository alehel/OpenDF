// Radio dialog index (DFDLGnn.DBF): which clips make up each dialog
// ("dlg101" -> dlg101_00000, dlg101_00001, ...) and their subtitles. The
// audio itself is in DFDLGnn.PWF; subtitle text in DFDLGnn.BIN.
//
// Layout (GOG Df.exe 0x455c90, 0x456110): 28-byte header ("DLG0"), then per
// dialog: u32 0x34, name[24] at +4, u32 part count at +0x1c, parts from
// +0x34; each part 0x44 bytes: u32, clip name[24] at +4, then a 16-byte text
// field at +0x1c: "_" + entry number in DFDLGnn.BIN, or "##" for no
// subtitle; a byte at +0x34, the gap byte at +0x35, ints at +0x38..+0x40.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace df {

struct DialogPart {
    std::string clip;
    int subtitle = -1;   // DFDLGnn.BIN entry, -1 = none
    int gap = 0;         // part +0x35: pause after the line, in tenths of 62 ticks (0x472f44)
};

class DialogIndex {
public:
    bool parse(const std::vector<uint8_t>& file);
    const std::vector<DialogPart>* find(const std::string& name) const;

private:
    std::unordered_map<std::string, std::vector<DialogPart>> dialogs_;
};

} // namespace df
