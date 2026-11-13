// Radio dialog index (DFDLGnn.DBF): which clips make up each dialog
// ("dlg101" -> dlg101_00000, dlg101_00001, ...) and their subtitles. The
// audio itself is in DFDLGnn.PWF; subtitle text in DFDLGnn.BIN.
//
// Layout: 32-byte header ("DLG0"), then per dialog: name[24], u32 part count
// at +0x18, parts from +0x34; each part 0x44 bytes: clip name[24], then a
// 16-byte text field: "_" + entry number in DFDLGnn.BIN, or "##" for no
// subtitle.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace df {

struct DialogPart {
    std::string clip;
    int subtitle = -1;   // DFDLGnn.BIN entry, -1 = none
};

class DialogIndex {
public:
    bool parse(const std::vector<uint8_t>& file);
    const std::vector<DialogPart>* find(const std::string& name) const;

private:
    std::unordered_map<std::string, std::vector<DialogPart>> dialogs_;
};

} // namespace df
