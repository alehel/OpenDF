// DF.MNU, the front end's layout file, as DF.EXE reads it (0x406788): the
// whole file upper-cased; sections [RGBS], [RGBAS], [FONTS], [IMAGES],
// [BUTTONS], [MENUS], [MENUBUTTONS] (the others are never read); records
// KEY=value up to ';'; fields split at commas, spaces and line ends with
// strtok's rules (empty fields collapse). Button text comes from DFMENU.BIN,
// section "GroupMainMenu", key = the upper-cased text id; "{hot}X" marks the
// hot key. See docs/FORMATS.md.
#pragma once

#include "../formats/Rtxt.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace df {

struct MnuFont {
    std::string file;
    uint32_t fg[3] = {0xFF00FF00, 0xFF00FF00, 0xFF00FF00};   // normal, lit, disabled (0xAARRGGBB)
};

struct MnuButton {
    std::string name;
    int type = 0;            // 0 label, 1 button, 2 H slider, 3 V slider, 4/5 list (inert), 6 picture
    int attrib = 0;          // see docs/FORMATS.md (0x8 left, 0x10 right, 0x20 owner draw, 0x40 text, 0x80 text in rect, ...)
    std::string textId, text;
    std::string callback, link, font, image, userdraw;
    int x = 0, y = 0, sliderSize = 0, sliderPos = 0, width = 0, height = 0, state = 0, drawstate = 1, userid = 0;
    char hot = 0;            // upper-cased hot key, 0 none
    int hotPos = -1;
};

struct MnuMenu {
    std::string name, background, accept, back, up, down, left, right;
    std::vector<std::string> buttons;   // draw order and hit priority
};

class Mnu {
public:
    bool parse(const std::string& text, const TextTable& menuText);
    std::map<std::string, uint32_t> colors;          // 0xAARRGGBB
    std::map<std::string, MnuFont> fonts;
    std::map<std::string, std::string> images;       // name -> PCX file
    std::map<std::string, MnuButton> buttons;
    std::map<std::string, MnuMenu> menus;
    MnuButton* button(const std::string& name);
    const MnuMenu* menu(const std::string& name) const;
};

std::string upperCase(std::string s);

} // namespace df
