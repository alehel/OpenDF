// The soldier profile's 44 key bindings (profile +0x6a0, 12 bytes {vk,
// mouse, 0}): defaults 0x4dd190, row labels 0x4dd3a0, game-table names
// (0x41cca0; row 8 feeds attack_1 and attack_2). Shared by the keyboard page
// and the in-game help (0x4816a0). The GOG Df.exe has 46 rows (labels
// 0x4e8004, game names 0x41ce20; row 28 is "AudioMacros" there): rows 44
// and 45 below exist only in it, and its defaults come from DFDK.KEY
// (0x407040), which gives them as here.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace df {

struct KeyRow { const char* label; const char* game; uint32_t vk; uint32_t mouse; };
constexpr int kKeyRowsCd = 44, kKeyRowsGog = 46;
inline const KeyRow kKeyRows[kKeyRowsGog] = {
    {"KBDMOVELEFT", "slide_left", 0x25, 0}, {"KBDMOVERIGHT", "slide_right", 0x27, 0}, {"KBDMOVEUP", "move_forward", 0x26, 0},
    {"KBDMOVEDOWN", "move_back", 0x28, 0}, {"KBDGOPRONE", "Prone", 0x22, 3}, {"KBDCROUCH", "Crouch", 0x23, 0},
    {"KBDSTAND", "Stand", 0x2E, 0}, {"KBDJUMP", "move_jump", 0x20, 0}, {"KBDFIRE", "attack_1", 0x11, 1},
    {"KBDSCOPE", "scope", 0x53, 2}, {"KBDWALK", "NoRun", 0x10, 0}, {"KBDCYCLEWEAPON", "cycle", 0x08, 0},
    {"KBD20", "Binoculars", 0x42, 0}, {"KBD21", "weapon1", 0x31, 0}, {"KBD22", "weapon2", 0x32, 0},
    {"KBD23", "weapon3", 0x33, 0}, {"KBD24", "weapon4", 0x34, 0}, {"KBD25", "weapon5", 0x35, 0},
    {"KBD26", "weapon6", 0x36, 0}, {"KBD27", "weapon7", 0x37, 0}, {"KBD28", "weapon8", 0x38, 0},
    {"KBD29", "weapon9", 0x39, 0}, {"KBDLASER", "weapon0", 0x30, 0}, {"KBD210", "zoomin", 0xBB, 0},
    {"KBD211", "zoomout", 0xBD, 0}, {"KBDTALK", "talk", 0x54, 0}, {"KBDTEAMTALK", "stalk", 0x59, 0},
    {"KBDWAYPOINTS", "NextWaypoint", 0x57, 0}, {"KBDAUDIOCOMMAND", "audiocommand", 0x41, 0},
    {"KBDVIDEORESOLUTION", "resolution", 0x56, 0}, {"KBDDROPITEMS", "Drop", 0x44, 0}, {"KBDCHANGEMAGAZINE", "magazine", 0x4D, 0},
    {"KBDLOOKUP", "look_up", 0x68, 0}, {"KBDLOOKDOWN", "look_down", 0x62, 0}, {"KBDLOOKRIGHT", "turn_right", 0x66, 0},
    {"KBDLOOKLEFT", "turn_left", 0x64, 0}, {"KBDCYCLEFOB", "tab", 0x09, 0}, {"KBDORDERS", "briefing", 0x4F, 0},
    {"KBDRECENTMSGS", "OldMessages", 0x52, 0}, {"KBDTEAMTAGS", "ShowFriendly", 0x46, 0}, {"KBDGOALS", "MissionGoals", 0x47, 0},
    {"KBDCOMMANDMAP", "BigMap", 0x43, 0}, {"KBDTURBO", "Turbo", 0x48, 0}, {"KBDEFLAGS", "NextFlag", 0x51, 0},
    {"KBDPREVWP", "PrevWaypoint", 0x45, 0}, {"KBDSERVER", "server", 0x24, 0},
};


// 0x421ff0: a Windows virtual-key code's name; `text` looks a DFMENU.BIN
// GroupMainMenu key up.
inline std::string vkName(uint32_t vk, const std::function<std::string(const char*)>& text) {
    if (vk == 0) return "";
    if ((vk >= 0x30 && vk <= 0x39) || (vk >= 0x41 && vk <= 0x5A)) return std::string(1, char(vk));
    if (vk >= 0x60 && vk <= 0x69) return text("KBDKEYPAD") + " " + char('0' + (vk - 0x60));
    static const std::pair<uint32_t, const char*> named[] = {
        {0x25, "KBDLEFT"}, {0x27, "KBDRIGHT"}, {0x26, "KBDUP"}, {0x28, "KBDDOWN"}, {0x11, "KBDCTRL"}, {0x10, "KBDSHIFT"},
        {0x0D, "KBDENTER"}, {0x2E, "KBDDELETE"}, {0x2D, "KBDINSERT"}, {0x24, "KBDHOME"}, {0x23, "KBDEND"},
        {0x6B, "KBDKEYPADPLUS"}, {0x6C, "KBDSEPARATOR"}, {0x6D, "KBDKEYPADMINUS"}, {0x20, "KBDSPACEBAR"}, {0x08, "KBDBACK"},
        {0x09, "KBDTAB"}, {0x21, "KBDPAGEUP"}, {0x22, "KBDPAGEDOWN"}, {0xBC, "KBDCOMMA"}, {0xBE, "KBDPERIOD"},
        {0xBA, "KBDSEMICOLON"}, {0xDB, "KBDLEFTBRACKET"}, {0xDD, "KBDRIGHTBRACKET"}};
    for (const auto& [v, k] : named)
        if (v == vk) return text(k);
    switch (vk) {
    case 0x6A: return "*";
    case 0xBB: return "+";
    case 0xBD: return "-";
    case 0x6E: return ".";
    case 0x6F: return "/";
    default: return "Unknown " + std::to_string(vk);
    }
}

} // namespace df
