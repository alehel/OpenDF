#include "FrontendDf2.h"

#include "../audio/Audio.h"
#include "../audio/Music.h"
#include "../core/ExeImage.h"
#include "../formats/Bink.h"
#include "../formats/Image.h"
#include "../formats/Mission.h"
#include "../formats/Pcx.h"
#include "../formats/Rtxt.h"
#include "../game/Config.h"
#include "../game/Controls.h"
#include "../render/Gl.h"
#include "../render/Hud.h"
#include "../render/Window.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>

namespace df {

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

Color argbColor(uint32_t c) {
    return {float((c >> 16) & 0xFF) / 255.f, float((c >> 8) & 0xFF) / 255.f, float(c & 0xFF) / 255.f, 1.f};
}

// ---------------------------------------------------------------- DF2.MNU

struct D2Font { std::string file; uint32_t fg[3] = {0xFFF0621C, 0xFFFF8000, 0xFF844030}; };
struct D2Image { std::string file; int attrib = 0, frames = 1; };
struct D2Button {
    std::string name, menu, text, callback, font, image;
    std::string mnuText;   // the text at load time, which the layout 0x42d8d0 measures
    int type = 0, attrib = 0, x = 0, y = 0, p1 = 0, p2 = 0, p3 = 0, drawstate = 1, hot1 = 0, hot2 = 0, zorder = 0;
    int user1 = 0, user2 = 0, w = 0, h = 0;
    bool selected = false;
    int flash = 0;
};
struct D2Menu { std::string name, background; std::vector<int> buttons; };

// DF2.EXE 0x42e460: records KEY=value, fields split by strtok(", ") (empty
// fields collapse), attribute letters -> 1 << (letter - 'A').
struct D2Mnu {
    std::map<std::string, std::string> sounds;   // name -> file
    std::map<std::string, uint32_t> colors;
    std::map<std::string, D2Font> fonts;
    std::map<std::string, D2Image> images;
    std::map<std::string, D2Menu> menus;
    std::vector<D2Button> buttons;

    static int attrib(const std::string& s) {
        int a = 0;
        for (char c : s)
            if (c >= 'A' && c <= 'Z') a |= 1 << (c - 'A');
            else if (c >= 'a' && c <= 'z') a |= 1 << (c - 'a');
        return a;
    }

    bool parse(const std::string& text, const TextTable& menuText) {
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (auto c = line.find(';'); c != std::string::npos) line.resize(c);
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = upper(line.substr(0, eq));
            std::vector<std::string> f;
            std::string cur;
            for (char ch : line.substr(eq + 1)) {
                if (ch == ',' || ch == ' ' || ch == '\t' || ch == '\r') {
                    if (!cur.empty()) f.push_back(cur);
                    cur.clear();
                } else {
                    cur += ch;
                }
            }
            if (!cur.empty()) f.push_back(cur);
            auto at = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
            auto num = [&](size_t i) { return std::atoi(at(i).c_str()); };
            if (key == "SOUND" && f.size() >= 2) sounds[upper(f[0])] = f[1];
            else if (key == "ARGB" && f.size() >= 5)
                colors[upper(f[0])] = uint32_t(num(1)) << 24 | uint32_t(num(2)) << 16 | uint32_t(num(3)) << 8 | uint32_t(num(4));
            else if (key == "FONT" && f.size() >= 8) {
                D2Font fo;
                fo.file = f[1];
                const size_t idx[3] = {2, 4, 6};
                for (int k = 0; k < 3; ++k)
                    if (auto it = colors.find(upper(f[idx[k]])); it != colors.end()) fo.fg[k] = it->second;
                fonts[upper(f[0])] = fo;
            } else if (key == "IMAGE" && f.size() >= 2) {
                images[upper(f[0])] = {f[1], attrib(at(2)), std::max(1, num(3))};
            } else if (key == "MENU" && f.size() >= 3) {
                menus[upper(f[0])] = {f[0], upper(f[2]), {}};
            } else if (key == "BUTTON" && f.size() >= 20) {
                D2Button b;
                b.name = f[0];
                b.menu = upper(f[1]);
                b.type = num(3);
                b.attrib = attrib(f[4]);
                if (upper(f[5]) != "NULL") {
                    const std::string* t = menuText.find("MENU", upper(f[5]));
                    b.text = t ? *t : "??TTID??";
                }
                b.mnuText = b.text;
                b.callback = upper(f[6]) == "NULL" ? "" : f[6];
                b.x = num(7);
                b.y = num(8);
                b.font = upper(f[9]) == "NULL" ? "" : upper(f[9]);
                b.image = upper(f[10]) == "NULL" ? "" : upper(f[10]);
                b.p1 = num(11);
                b.p2 = num(12);
                b.p3 = num(13);
                b.drawstate = num(14);
                b.hot1 = num(15);
                b.hot2 = num(16);
                b.zorder = num(17);
                b.user1 = num(18);
                b.user2 = num(19);
                buttons.push_back(b);
            }
        }
        // GOG Df2.exe 0x433210 (CD 0x42e460): 0x432ba0 first qsorts all the
        // buttons by name (_strcmpi), then 0x4330a0 (CD 0x42e2f0) appends
        // them to their menus' lists in that order and 0x433040 (CD
        // 0x42e290) sorts each list by descending zorder with its unstable
        // i<j swap loop. The order of equal-z buttons decides which one is
        // drawn on top (Campaign's region buttons over C_WORLD). The buttons
        // vector itself keeps file order; only the lists follow the name order.
        auto lowerByte = [](unsigned char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : int(c); };
        std::vector<int> byName(buttons.size());
        for (size_t i = 0; i < buttons.size(); ++i) byName[i] = int(i);
        std::stable_sort(byName.begin(), byName.end(), [&](int a, int b) {
            const std::string &x = buttons[size_t(a)].name, &y = buttons[size_t(b)].name;
            return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end(),
                                                [&](char p, char q) { return lowerByte(p) < lowerByte(q); });
        });
        for (int i : byName)
            if (auto it = menus.find(buttons[size_t(i)].menu); it != menus.end()) it->second.buttons.push_back(i);
        for (auto& [name, m] : menus) {
            auto& L = m.buttons;
            for (size_t i = 0; i + 1 < L.size(); ++i)
                for (size_t j = i; j < L.size(); ++j)
                    if (buttons[size_t(L[i])].zorder < buttons[size_t(L[j])].zorder) std::swap(L[i], L[j]);
        }
        return !menus.empty() && !buttons.empty();
    }
    D2Button* button(const std::string& name) {
        for (auto& b : buttons)
            if (b.name == name) return &b;
        return nullptr;
    }
};

// Options2 "Keyboard" (0x43a5a0): the rows' label keys, the defaults {VK,
// mouse button} and the game commands they set at mission start (row 8 also
// sets attack_2). GOG Df2.exe has 59 rows (0x2c4 / 12 in 0x43e780): labels
// 0x4c8144, defaults 0x4c7190 (copied by 0x4368f0), commands 0x441140; the CD
// build's 48 (0x4b3af0, 0x4b2cb8, 0x43afb0) are rows 0..47, with rows 45/46
// relabelled INCREASE/DECREASE_SCOPE_ZERO. Rows 48..58 are GOG's additions.
constexpr size_t kDf2KeyRows = 59;
struct Df2KeyRow { const char* label; const char* game; uint32_t vk, mouse; };
const Df2KeyRow kDf2Keys[kDf2KeyRows] = {
    {"STRAFE_LEFT", "slide_left", 0x25, 0}, {"STRAFE_RIGHT", "slide_right", 0x27, 0}, {"MOVE_FORWARD", "move_forward", 0x26, 0},
    {"MOVE_BACK", "move_back", 0x28, 0}, {"GO_PRONE", "Prone", 0x22, 3}, {"CROUCH", "Crouch", 0x23, 0},
    {"STAND", "Stand", 0x2E, 0}, {"JUMP", "move_jump", 0x20, 0}, {"FIRE_WEAPON", "attack_1", 0x11, 1},
    {"SCOPE", "scope", 'S', 2}, {"WALK", "NoRun", 0x10, 0}, {"CYCLE_WEAPONS", "cycle", 0x08, 0},
    {"BINOCULARS", "Binoculars", 'B', 0}, {"KNIFE", "weapon1", '1', 0}, {"SIDEARM", "weapon2", '2', 0},
    {"PRIMARY_WPN_FN_1", "weapon3", '3', 0}, {"PRIMARY_WPN_FN_2", "weapon4", '4', 0}, {"PRIMARY_WPN_FN_3", "weapon5", '5', 0},
    {"SECONDARY_WEAPON", "weapon6", '6', 0}, {"AUXILIARY_GEAR", "weapon7", '7', 0}, {"DETONATOR", "weapon8", '8', 0},
    {"GRENADE", "weapon9", '9', 0}, {"LASER_DESIGNATOR", "weapon0", '0', 0}, {"ZOOM_IN", "zoomin", 0xBB, 0},
    {"ZOOM_OUT", "zoomout", 0xBD, 0}, {"TALK", "talk", 'T', 0}, {"TEAM_TALK", "stalk", 'Y', 0},
    {"NEXT_WAYPOINT", "NextWaypoint", 'W', 0}, {"AUDIO_COMMAND", "AudioMacros", 'A', 0}, {"VIDEO_RESOLUTION", "resolution", 'V', 0},
    {"DROP_ITEMS", "Drop", 'D', 0}, {"CHANGE_MAGAZINE", "magazine", 'M', 0}, {"LOOK_UP", "look_up", 'P', 0},
    {"LOOK_DOWN", "look_down", 0xBE, 0}, {"TURN_RIGHT", "turn_right", 0xBA, 0}, {"TURN_LEFT", "turn_left", 'L', 0},
    {"CYCLE_FORWARD_OBSERVER", "tab", 0x09, 0}, {"ORDERS", "briefing", 'O', 0}, {"RECENT_MESSAGES", "OldMessages", 'R', 0},
    {"TEAM_TAGS", "ShowFriendly", 'F', 0}, {"GOALS", "MissionGoals", 'G', 0}, {"COMMANDERS_SCREEN", "BigMap", 'C', 0},
    {"TURBO", "Turbo", 'H', 0}, {"NEXT_FLAG_(MP)", "NextFlag", 'Q', 0}, {"TOGGLE_HOST_SCREEN", "PrevWaypoint", 0x24, 0},
    {"INCREASE_SCOPE_ZERO", "scopeinc", 'X', 0}, {"DECREASE_SCOPE_ZERO", "scopedec", 'Z', 0}, {"TOGGLE_NIGHT_VISION", "NVG", 'N', 0},
    {"PRINT_SCREEN", "PrintScreen", 'I', 0}, {"SELECT_LAST_TALKER", "VonSLT", 0x6F, 0}, {"DISPLAY_LAST_TALKER", "VonDLT", 0x6A, 0},
    {"VON_DISPLAY", "VonDisplay", 0x6D, 0}, {"UNMUTE_SELECTED_PLAYER", "VonUSP", 0x6B, 0}, {"DECREASE_RECORD_VOLUME", "VonRVU", 0x61, 0},
    {"INCREASE_RECORD_VOLUME", "VonRVD", 0x63, 0}, {"MUTE_CURRENT_TALKER", "VonMCT", 0x60, 0}, {"BREAK_TALKER", "VonBT", 0x6E, 0},
    {"SELECT_TARGETED", "VonSBX", 0xDC, 0}, {"SCOPE_MAGNIFICATION", "scopemag", 'E', 0}};

// GOG Df2.exe 0x43e6c0 (replacing the CD's allow-list 0x439310): a row may
// take any key but Esc, Scroll Lock, Pause, the Windows keys, VK 0x2A, '`'
// (0xC0) and F1..F24; those are "reserved". (0xFF: OpenDF's mark for a key
// with no Windows VK, also reserved.)
bool df2KeyAllowed(uint32_t vk) {
    switch (vk) {
    case 0xFF:
    case 0x1B: case 0x91: case 0x13: case 0x5B: case 0x5C: case 0x2A: case 0xC0: return false;
    default: return vk < 0x70 || vk > 0x87;
    }
}

// ---------------------------------------------------------------- soldiers

// df2plyrs.sav (GOG Df2.exe 0x436890 / 0x4369f0; CD 0x431ba0 / 0x431c90):
// 'FPBC', version 14 (the CD wrote and read 10), 5 records of 0x6E8, the
// same layout in both builds (GOG fills more of it: defaultSoldier).
struct Soldier {
    std::array<uint8_t, 0x6E8> d{};
    int32_t get(size_t o) const { int32_t v; std::memcpy(&v, &d[o], 4); return v; }
    void set(size_t o, int32_t v) { std::memcpy(&d[o], &v, 4); }
    std::string name() const { return std::string(reinterpret_cast<const char*>(&d[4]), strnlen(reinterpret_cast<const char*>(&d[4]), 16)); }
    void setName(const std::string& n) {
        std::memset(&d[4], 0, 16);
        std::memcpy(&d[4], n.data(), std::min<size_t>(15, n.size()));
    }
    bool empty() const { return name().empty(); }
};

// GOG Df2.exe 0x425170: the default key table 0x4c7190 (0x2d0 bytes, 60
// rows of {VK, mouse button, 0}), read over from df2.dfk when the game
// folder has one (fread of one 0x2d0 item: a short file replaces what it
// holds); without it, the table built into Df2.exe (kDf2Keys).
using Df2KeyTable = std::array<uint8_t, 0x2d0>;
Df2KeyTable loadDf2KeyTable(const std::filesystem::path& dir) {
    Df2KeyTable t{};
    for (size_t i = 0; i < kDf2KeyRows; ++i) {
        const uint32_t vk = kDf2Keys[i].vk, mouse = kDf2Keys[i].mouse;
        std::memcpy(&t[12 * i], &vk, 4);
        std::memcpy(&t[12 * i + 4], &mouse, 4);
    }
    std::ifstream k(dir / "Df2.dfk", std::ios::binary);
    Df2KeyTable file{};
    if (k) {
        k.read(reinterpret_cast<char*>(file.data()), std::streamsize(file.size()));
        std::memcpy(t.data(), file.data(), size_t(k.gcount()));
    }
    return t;
}

// Campaign missions (table 0x4afba0): BMS, campaign 1/2, region 0..4.
struct CampMission { const char* bms; int campaign, region; };
const CampMission kMissions[28] = {
    {"D03M01", 1, 0}, {"D03M02", 1, 0}, {"D03M03", 1, 1}, {"D03M04", 1, 1}, {"D03M05", 1, 1}, {"D03M06", 1, 1},
    {"D03M07", 1, 1}, {"D03M08", 1, 1}, {"D03M09", 1, 2}, {"D03M10", 1, 2}, {"D03M11", 1, 2}, {"D03M12", 1, 2},
    {"D03M13", 1, 2}, {"D03M15", 1, 2}, {"D05M01", 2, 3}, {"D05M02", 2, 3}, {"D05M03", 2, 3}, {"D05M04", 2, 3},
    {"D05M05", 2, 3}, {"D05M06", 2, 4}, {"D05M07", 2, 4}, {"D05M08", 2, 4}, {"D05M09", 2, 4}, {"D05M10", 2, 4},
    {"D05M11", 2, 4}, {"D05M12", 2, 4}, {"D05M13", 2, 4}, {"D05M15", 2, 4}};
// Regions (table 0x4afb00): region picture, button pictures.
const char* const kRegionImage[5] = {"IMAGEANTARCTICA", "IMAGEUGANDA", "IMAGEGEORGIA", "IMAGECHAD", "IMAGEKAZAKHSTAN"};
const char* const kRegionButton[5] = {"IMAGEBTNANTARCTICA", "IMAGEBTNUGANDA", "IMAGEBTNGEORGIA", "IMAGEBTNCHAD", "IMAGEBTNKAZAKHSTAN"};

// Change Gear (0x4300a0): weapon type -> picture (0x4b02f8), "Magazine"
// rounds (0x4b0394), "Range" metres (0x4b0430), category (type +0x94).
struct GearInfo { int type; const char* image; int rounds, range, category; };
const GearInfo kGear[] = {
    {1, "IMAGEAMMO", -1, -1, 4},        {5, "IMAGE45SILNCE", 10, 50, 2},  {6, "IMAGE45PISTOL", 10, 50, 2},
    {9, "IMAGEM4GRENAD", 30, 500, 3},   {10, "IMAGEM4SHOTGN", 30, 500, 3}, {11, "IMAGESAW", 200, 800, 3},
    {12, "IMAGEBARRET", 8, 1500, 3},    {15, "IMAGEHKMP5", 30, 100, 3},   {20, "IMAGELAW80", 1, 400, 4},
    {21, "IMAGESATCHEL", -1, -1, 4},    {23, "IMAGEM40RIFLE", 5, 800, 3}, {24, "IMAGECLAYMORE", -1, -1, 4},
    {28, "IMAGEH20PISTL", 5, 15, 2},    {29, "IMAGEH20RIFLE", 26, 100, 3}, {31, "IMAGEKEVLAR", -1, -1, 5},
    {32, "IMAGEGHILLIE", -1, -1, 5},    {33, "IMAGEREBREATH", -1, -1, 5}, {36, "IMAGECAMERA", -1, -1, 4}};
const GearInfo* gearInfo(int type) {
    for (const auto& g : kGear)
        if (g.type == type) return &g;
    return nullptr;
}

// A front-end font glyph (DF2.EXE 0x468d30, 28-byte records at font +0): the
// tight ink box (x, y, w, h) in the sheet and yoff = top ink row - band top - 3.
struct Glyph { int x = 0, y = 0, w = 0, h = 0, yoff = 0; };
// height: the height of 'A' (+0x734, used for line spacing and the hot-key
// text's vertical offset); heightX: the height of 'X' (+0x9b8).
struct D2FontTex { HudImage tex; std::array<Glyph, 256> glyphs{}; int height = 0, heightX = 0; bool ok = false; };

enum class Screen { Soldier, Main, Campaign, Quick, Briefing, Gear, Options1, Options2, Options3, Options4, Credits, Mission, Quit };

// The movie's soundtrack as binkplay plays it: its own sound output, not
// DF2's mixer (Audio::setDirectStream). Decoded samples queue here as
// stereo at the mixer's 22050 Hz (other rates resampled linearly, mono
// doubled: INFERRED, the intros are 22050 Hz stereo) and are added to the
// output as the device asks.
class BinkSound {
public:
    void push(const std::vector<int16_t>& pcm, int rate, int channels) {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t n = pcm.size() / size_t(channels);
        const double step = double(rate) / Audio::kRate;
        for (; pos_ < double(n); pos_ += step) {
            const size_t i = size_t(pos_);
            for (int c = 0; c < 2; ++c) {
                const int ch = std::min(c, channels - 1);
                const double a = pcm[i * size_t(channels) + size_t(ch)];
                const double b = i + 1 < n ? pcm[(i + 1) * size_t(channels) + size_t(ch)] : a;
                queue_.push_back(int16_t(std::lround(a + (b - a) * (pos_ - double(i)))));
            }
        }
        pos_ -= double(n);
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        pos_ = 0;
    }
    void mix(int16_t* out, int frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        const size_t n = std::min(queue_.size(), size_t(frames) * 2);
        for (size_t i = 0; i < n; ++i) out[i] = int16_t(std::clamp(int(out[i]) + int(queue_[i]), -32768, 32767));
        queue_.erase(queue_.begin(), queue_.begin() + std::ptrdiff_t(n));
    }

private:
    std::mutex mutex_;
    std::deque<int16_t> queue_;
    double pos_ = 0;
};

} // namespace

class FrontendDf2 {
public:
    FrontendDf2(Vfs& vfs, const FrontendOptions& o) : vfs_(vfs), o_(o) {}
    int run();

private:
    // Window, drawing.
    bool openWindow();
    void closeWindow();
    Hud& hud() { return *hud_; }
    HudImage file(const std::string& name);
    HudImage image(const std::string& name) {
        auto it = mnu_.images.find(upper(name));
        return it == mnu_.images.end() ? HudImage{} : file(it->second.file);
    }
    int imageFrames(const std::string& name) {
        auto it = mnu_.images.find(upper(name));
        return it == mnu_.images.end() ? 1 : it->second.frames;
    }
    D2FontTex& font(const std::string& name);
    float textWidth(const std::string& fontName, const std::string& s);
    float textHeight(const std::string& fontName, const std::string& s);
    void text(const std::string& fontName, float x, float y, const std::string& s, uint32_t color, int align = 0);
    int wrapped(const std::string& fontName, float x0, float y0, float x1, float y1, const std::string& s, uint32_t color,
                int skipLines = 0);
    int breakLines(const std::string& fontName, int width, const std::string& s, std::vector<std::string>* lines = nullptr);
    void boxText(const std::string& fontName, int l, int r, int top, int bottom, const std::string& s, uint32_t color, int justify);
    void blit(const HudImage& im, float x, float y, float sx, float sy, float sw, float sh);
    void rectOutline(float x0, float y0, float x1, float y1, uint32_t color);
    void fill(float x0, float y0, float x1, float y1, uint32_t color);
    void sound(const std::string& name, int channel = 0);

    // Menus.
    void enter(Screen s);
    void playIntroMovie();   // 0x435830: DF2INTRO.BIK through binkplay.exe
    void playBinkVideo(const std::filesystem::path& path);   // binkplay's part
    bool introShot_ = false;   // testing: --screenshot was taken during the intro
    bool cdExe_ = false;       // the CD's DF2.EXE: DF2.MNU hot keys are DirectInput scancodes
    D2Menu* menu() { auto it = mnu_.menus.find(menuName_); return it == mnu_.menus.end() ? nullptr : &it->second; }
    D2Button* btn(const std::string& name) { return mnu_.button(name); }
    void layout(D2Button& b);
    D2Button* hit(int x, int y);
    void drawMenu();
    // GOG Df2.exe 0x434000 (0x434151 / 0x43415d): ImageCurTarg is the menu
    // cursor with its hot spot at (12, 12) (0x433780); drawn every frame
    // after the page (0x443d90 -> 0x4436a0).
    HudImage cursor_;
    bool cursorLoaded_ = false;
    void drawCursor();
    // The pages' own drawing after 0x443d90 (Choose Soldier 0x436720, Main
    // 0x43a780): their connector lines, over the cursor.
    void drawConnectors();
    void hvLine(int x0, int y0, int x1, int y1, uint32_t color);
    std::string mmHover_;   // [0x4c5998]: the Main button whose help is shown (0x43a700)
    void drawButton(D2Button& b);
    void drawDefault(D2Button& b);
    // 0x43d4c0 keeps the justification in two locals of its loop ([esp+0x14]
    // for push buttons, [esp+0x18] for the rest) that only F/H/E/D buttons set.
    // On entry they hold whatever the stack had; INFERRED: 0 (neither 1, 2 nor
    // 4), which centres hot-key text and left-aligns the rest - the Main menu's
    // eight buttons (no F/H/E/D, drawn before EXIT GAME) come out centred.
    int staleJustify0_ = 0, staleJustify1_ = 0;
    void ownerDraw(D2Button& b);
    void event(D2Button& b, int ev);
    void key(const SDL_KeyboardEvent& k);

    // Data.
    void loadSoldiers();
    void defaultSoldier(Soldier& s);
    Df2KeyTable keyTable_{};   // GOG Df2.exe 0x4c7190, loadDf2KeyTable
    void saveSoldiers();
    Soldier& soldier() { return soldiers_[size_t(current_)]; }
    uint32_t header(const std::string& bms, size_t offset);
    bool missionAvailable(int i);
    bool missionOpen(int i);
    void refreshSoldier();
    void refreshCampaign();
    void refreshQuick();
    void refreshGear();
    void refreshOptions();
    void loadBriefing();
    void gearStep(size_t slot, int category, int dir, int altCategory = -1);
    void startMission();
    // Options2 "Keyboard" (0x439ea0..0x43a930).
    void refreshKeys();
    std::string keyName(size_t row);
    void remapMessage(const char* key) { helpText_ = menuText(key, key); helpShown_ = 0; }
    void captureTick();
    std::array<uint8_t, kDf2KeyRows * 12> keySnapshot_{};   // 0x4cf370, taken on the first visit per Settings session
    bool haveKeySnapshot_ = false;             // [0x4cf36c]
    bool keyDelete_ = false, keyCapture_ = false;
    int keyRow_ = 0, keyMouse_ = 0;
    std::deque<uint32_t> keyQueue_;            // key-downs while capturing (VKs)
    std::map<int, int> channels_;              // menu sound channel -> playing voice
    // Credits (0x430e00 / 0x430ea0 / 0x4316e0): CREDITS.SCR commands run
    // until a PAUSE; 250 object slots indexed by the command's ID.
    struct CreditCmd { int type = -1, id = 0, flags = 0, life = 0, t = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0; std::string font, color, str; };
    struct CreditObj { int type = 0, flags = 0, life = -1, x1 = 0, y1 = 0, x2 = 0, y2 = 0, t = 0; std::string font, str; uint32_t color = 0xFFFFFFFF; };
    std::vector<CreditCmd> creditCmds_;
    std::array<CreditObj, 250> creditObjs_{};
    size_t creditPc_ = 0;
    int creditPause_ = 0;
    bool creditDone_ = false;
    TextTable creditText_;
    bool creditTextLoaded_ = false;
    void loadCredits();
    void creditTick();
    void drawCredits();
    std::string menuText(const char* key, const char* fallback) {
        const std::string* t = menuText_.find("MENU", key);
        return t ? *t : fallback;
    }

    Vfs& vfs_;
    FrontendOptions o_;
    SDL_Window* win_ = nullptr;
    SDL_GLContext ctx_ = nullptr;
    std::unique_ptr<Hud> hud_;
    Audio audio_;
    Music music_;
    BinkSound binkSound_;
    int frame_ = 0;   // testing: frames shown for --frames, the intro's included
    bool musicLoaded_ = false;
    DfConfig cfg_;
    std::filesystem::path saveDir_;
    D2Mnu mnu_;
    TextTable menuText_, briefText_, gameText_;
    std::map<std::string, HudImage> images_;
    std::map<std::string, D2FontTex> fonts_;
    float scale_ = 1, offX_ = 0, offY_ = 0;
    int mouseX_ = 0, mouseY_ = 0;
    std::string hover_, pressed_, dragging_;
    // 0x439c00: the music volume [0x4cdfbc]: rising from 0 starts the music
    // (0x43cbc0), 0 stops it (0x43cc10), otherwise the new volume (0x46c170).
    void setMusicVolume(int v) {
        const int old = cfg_.get("music_volume");
        if (v == old) return;
        cfg_.set("music_volume", v);
        if (old == 0) { music_.setVolume(std::clamp(v, 0, 255)); music_.start(); }
        else if (v == 0) music_.stop();
        else music_.setVolume(std::clamp(v, 0, 255));
    }
    // Event 0xA: a slider's new value is written where it belongs.
    void sliderChanged(D2Button& b) {
        if (b.name == "but_opt1_mssl") soldier().set(0x5b0, b.p2);
        else if (b.name == "but_opt1_sshsl") soldier().set(0x5d8, b.p2);   // opt_Accept 0x43ebf7
        else if (b.name == "but_opt1_sfsl") soldier().set(0x5b8, b.p2);
        else if (b.name == "but_opt1_dvsl") soldier().set(0x5c0, b.p2);
        else if (b.name == "but_opt1_mvsl") setMusicVolume(b.p2);
        else if (b.name == "but_qm_vsl") { quickTop_ = b.p2; refreshQuick(); }
        else if (b.name == "but_opt2_vsl") { b.p2 = std::min(b.p2, b.p1 - 1); refreshKeys(); }   // opt2_ScrollTo 0x43a6b0
        // mb_MapVSlide / mb_MapHSlide (GOG Df2.exe 0x43d970 / 0x43d950): the
        // map's scroll target follows the slider.
        else if (b.name == "but_mb_mpvsl") mapTargetY_ = b.p2;
        else if (b.name == "but_mb_mphsl") mapTargetX_ = b.p2;
    }
    // A held slider follows the mouse (GOG Df2.exe 0x433d10 / 0x433b60, CD
    // 0x42f150, event 1): value = (mouse - pos - knob/2) / length · range,
    // clamped.
    void dragTo(D2Button& b) {
        const HudImage im = image(b.image);
        const float len = float(std::max(1, b.p3));
        const float m = b.type == 2 ? float(mouseX_ - b.x - im.width / 2) : float(mouseY_ - b.y - im.height / 2);
        b.p2 = std::clamp(int(m / len * float(b.p1)), 0, b.p1);
        sliderChanged(b);
    }
    // A press on a slider (GOG Df2.exe 0x433d10 type 2 / 0x433b60 type 3,
    // event 2): at or before its start the value goes to 0; otherwise a value
    // under the mouse (as dragTo) within half the knob's size of the current
    // one takes hold of the knob, anything further steps the value by user1
    // (+0x7c, 5 when 0) towards it. Then clamped and event 0xA.
    void sliderPress(D2Button& b) {
        b.drawstate = 3;
        const HudImage im = image(b.image);
        const bool across = b.type == 2;
        const int m = across ? mouseX_ : mouseY_, pos = across ? b.x : b.y;
        const int half = (across ? im.width : im.height) >> 1;
        const int step = b.user1 ? b.user1 : 5;
        if (m <= pos) {
            b.p2 = 0;
        } else {
            const int v = int(double(m - half - pos) / double(std::max(1, b.p3)) * double(b.p1));
            if (v < 10000) {
                if (std::abs(v - b.p2) <= half) dragging_ = b.name;
                else b.p2 += v > b.p2 ? step : -step;
            }
        }
        b.p2 = std::clamp(b.p2, 0, std::max(0, b.p1));
        sliderChanged(b);
    }
    // GOG Df2.exe 0x43cdf0: the map's scroll target centred on picture point
    // (x, y) of the view but_mb_mpimg (scroll = point - ((view - 1) >> 1)),
    // clamped to [0, the slider's range]; without ease the sliders' values
    // (the scroll itself) jump there too.
    void mapCentre(int x, int y, bool ease) {
        D2Button* v = btn("but_mb_mpimg");
        D2Button* hs = btn("but_mb_mphsl");
        D2Button* vs = btn("but_mb_mpvsl");
        if (!v || !hs || !vs) return;
        mapTargetX_ = std::min(std::max(0, x - ((v->user1 - v->x - 1) >> 1)), hs->p1);
        mapTargetY_ = std::min(std::max(0, y - ((v->user2 - v->y - 1) >> 1)), vs->p1);
        if (!ease) { hs->p2 = mapTargetX_; vs->p2 = mapTargetY_; }
    }
    // The Mission Briefing page's tick (GOG Df2.exe 0x43ca20): each map
    // slider's value moves towards the scroll target by at most 10.
    void mapTick() {
        auto ease = [](D2Button* s, int target) {
            if (!s) return;
            if (target < s->p2) s->p2 -= std::min(s->p2 - target, 10);
            else if (target > s->p2) s->p2 += std::min(target - s->p2, 10);
        };
        ease(btn("but_mb_mphsl"), mapTargetX_);
        ease(btn("but_mb_mpvsl"), mapTargetY_);
    }
    int sliderValue(const char* name) { const D2Button* s = btn(name); return s ? s->p2 : 0; }

    Screen screen_ = Screen::Soldier, next_ = Screen::Soldier, returnTo_ = Screen::Main;
    std::string menuName_;
    int stageTicks_ = 0;   // 12-tick enter stage: input ignored (0x42e610)
    bool leaving_ = false;
    int leaveTicks_ = 0;   // Main Menu / Mission Briefing: 12 more ticks before leaving
    bool busy() const { return stageTicks_ > 0 || leaveTicks_ > 0; }
    uint64_t ticks_ = 0;
    std::array<Soldier, 5> soldiers_{};
    int current_ = 0;
    std::string editing_;   // button being edited (soldier name)
    std::string editText_, editBefore_;

    int campaign_ = 1, missionSel_ = -1;   // missionSel_: [0x4c3f70]
    // GOG Df2.exe 0x4c3f74..0x4c3f84 (static data -1, -1, -1, -1, 0): the
    // region shown per campaign at 0x4c3f74 + c·4 and the region that
    // blinks at 0x4c3f7c + c·4, c = 1 / 2; the blink timer 0x4c3f84 is the
    // same word as campaign 2's blink region (so campaign 2's blink only
    // ever toggles region 0, a campaign 1 button).
    std::array<int, 5> campMem_{{-1, -1, -1, -1, 0}};
    int& campRegion(int c) { return campMem_[size_t(c)]; }
    int& campBlink(int c) { return campMem_[size_t(c) + 2]; }
    int& campTimer() { return campMem_[4]; }
    struct RegionStats { bool avail = false; int total = 0, complete = 0; };
    RegionStats regionStats(int r);
    int nextRegion(int c);
    void blinkNext(int c);
    void campaignInit();
    void campaignTick();
    bool fromCampaign_ = true;
    std::vector<std::string> quick_;   // quick missions (D06*)
    int quickTop_ = 0, quickSel_ = -1;
    std::string bms_;                 // the chosen mission
    std::string helpText_;
    size_t helpShown_ = 0;
    HudImage briefMap_;
    // The map's scroll is the value (+0x6c) of but_mb_mphsl / but_mb_mpvsl
    // (GOG Df2.exe 0x4f39b8 / 0x4f37c0); this is where it is heading.
    int mapTargetX_ = 0, mapTargetY_ = 0;   // 0x4f3d6c / 0x4f3d70
    int mapCentreX_ = 512, mapCentreY_ = 512;   // the mission's centre point, 0x4f3d64 / 0x4f3d68
    // GOG Df2.exe 0x4045e0: the briefing map's markers, up to 50, in the
    // picture's 1024² coordinates (0x4f3a40, count 0x4f3d60).
    struct MapMarker { int marker, x, y, team; };
    std::vector<MapMarker> mapMarkers_;
    int optPage_ = 1;
    // Voice briefing (DF2.EXE 0x4384f0 start, 0x4378af per tick, 0x4385c0
    // end, 0x438640 the button, 0x437a70 leaving the page).
    bool briefVoice_ = false;        // [0x4cef94] playing
    bool briefStarted_ = false;      // [0x4cef98]: played since the mission was chosen
    int briefHandle_ = 0;            // [0x4cefb0]
    int briefCount_ = 0, briefAt_ = 0;   // [0x4cef90] left, [0x4cef8c] next slot
    std::array<std::string, 19> briefFiles_;   // 0x4cedf0: slot NN - 1
    int briefRamp_ = 0, briefRampMode_ = 0;    // [0x4cefa0] ticks, [0x4cef9c] 1 up / 2 down
    int briefMusic_ = 0, briefMusicTarget_ = 0, briefMusicStep_ = 0;   // [0x4cefa8], [0x4cefac], [0x4cefa4]
    void stopVoice() {
        // 0x4385c0: the voice stopped (0x471480), the counts cleared, the
        // button back to PLAY BRIEFING. The music stays where the ramp left it.
        if (briefHandle_) audio_.stop(briefHandle_);
        briefHandle_ = 0;
        briefVoice_ = false;
        briefCount_ = briefAt_ = 0;
        if (D2Button* b = btn("but_mb_rbbtn")) b->text = menuText("PLAY_BRIEFING", "PLAY BRIEFING");
    }
    void startVoice() {
        // 0x4384f0, once per chosen mission (also on entering the page,
        // 0x43778f): 0x437aa0 files <mission>NN.WAV (the first six letters,
        // NN 01..19) by NN into 19 slots, counting them; with any, the music
        // ramps over 60 ticks from the soldier's music volume (+0x5bc)
        // toward 70 (0x438552), by trunc(min(70 − v, 0) / 60) a tick, and the
        // voices follow.
        if (briefStarted_) return;
        briefStarted_ = true;
        briefFiles_.fill(std::string());
        briefCount_ = briefAt_ = 0;
        const std::string mis = upper(bms_.substr(0, std::min<size_t>(6, bms_.size())));
        for (const std::string& f : vfs_.list(".WAV")) {
            if (f.size() < 8 || upper(f.substr(0, 6)) != mis) continue;
            const int nn = (f[6] - '0') * 10 + (f[7] - '0');   // 0x437afd: c6·10 + c7 − 0x210
            if (nn <= 0 || nn >= 20) continue;
            briefFiles_[size_t(nn - 1)] = f;
            ++briefCount_;
        }
        if (briefCount_ <= 0) return;
        briefHandle_ = 0;
        briefRamp_ = 60;
        briefRampMode_ = 2;
        briefVoice_ = true;
        briefMusic_ = soldier().get(0x5bc);
        briefMusicTarget_ = 70;
        briefMusicStep_ = std::min(70 - briefMusic_, 0) / 60;
        if (D2Button* b = btn("but_mb_rbbtn")) b->text = menuText("STOP_BRIEFING", "STOP BRIEFING");
    }
    void voiceTick() {
        // 0x43783a: while the ramp runs, the music moves (0x46c170 clamps
        // it to 0..255) and nothing else happens.
        if (briefRamp_ > 0) {
            --briefRamp_;
            if ((briefRampMode_ == 1 && briefMusic_ < briefMusicTarget_) || (briefRampMode_ == 2 && briefMusic_ > briefMusicTarget_)) {
                briefMusic_ += briefMusicStep_;
                music_.setVolume(std::clamp(briefMusic_, 0, 255));
            }
            return;
        }
        if (!briefVoice_) return;
        if (briefCount_ > 0) {
            // 0x4378c9: the next slot once the last voice has ended (0x471420);
            // an empty slot plays nothing but is used up.
            if (briefHandle_ && audio_.playing(briefHandle_)) return;
            const std::string f = briefFiles_[size_t(std::min(briefAt_, 18))];
            --briefCount_;
            ++briefAt_;
            if (const SoundClip* c = f.empty() ? nullptr : audio_.clip(vfs_, f)) {
                // 0x437933: 0x4712f0(clip, direction 0, volume 255, pitch
                // 1.0): pan[0] (63 left, 64 right) · 255 · master 255 >> 16,
                // gains 62 / 63 as the loudest menu sound; the dialogue
                // volume (+0x5c0) is not used.
                briefHandle_ = audio_.play(c, 63.f / 127.f, 64.f / 127.f);
            }
        } else if (!briefHandle_ || !audio_.playing(briefHandle_)) {
            stopVoice();
        }
    }
    void leaveBriefing() {
        // 0x437a70: the voice ends and the music goes back to the soldier's
        // music volume (+0x5bc).
        stopVoice();
        music_.setVolume(std::clamp(soldier().get(0x5bc), 0, 255));
        briefRamp_ = 0;
    }
    std::mt19937 rng_{1};
};

// ---------------------------------------------------------------- window

bool FrontendDf2::openWindow() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const Uint32 flags = windowFlags(SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (o_.screenshot.empty() ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN));
    win_ = SDL_CreateWindow("OpenDF - Delta Force 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, o_.width, o_.height, flags);
    if (!win_) return false;
    ctx_ = SDL_GL_CreateContext(win_);
    const char* missing = nullptr;
    if (!ctx_ || !loadGl(&missing)) return false;
    hud_ = std::make_unique<Hud>();
    SDL_GL_SetSwapInterval(1);
    std::string err;
    if (!hud().init(vfs_, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return false; }
    images_.clear();
    fonts_.clear();
    briefMap_ = {};
    cursor_ = {};
    cursorLoaded_ = false;
    SDL_ShowCursor(SDL_DISABLE);   // the menus draw their own cursor (drawCursor)
    return true;
}

void FrontendDf2::closeWindow() {
    hud_.reset();
    images_.clear();
    fonts_.clear();
    briefMap_ = {};
    cursor_ = {};
    cursorLoaded_ = false;
    SDL_ShowCursor(SDL_ENABLE);
    if (ctx_) SDL_GL_DeleteContext(ctx_);
    if (win_) SDL_DestroyWindow(win_);
    ctx_ = nullptr;
    win_ = nullptr;
}

HudImage FrontendDf2::file(const std::string& name) {
    const std::string key = upper(name);
    if (auto it = images_.find(key); it != images_.end()) return it->second;
    HudImage im;
    std::vector<uint8_t> data;
    if (key.size() > 4 && key.compare(key.size() - 4, 4, ".PCX") == 0) {
        // PCX art: palette index 0 is the colour key (0x462d20: the 8-bit blit
        // skips index 0, 0x462dd3, for every frame; frame height = h / frames).
        im = hud().loadImage(vfs_, key, false, true);
    } else if (vfs_.read(key, data)) {
        RgbaImage rgba;
        if (decodeRgbaImageDf2(data, rgba)) im = hud().makeImage(rgba.width, rgba.height, rgba.rgba);
    }
    images_[key] = im;
    return im;
}

// DF2.EXE 0x468d30 (the front-end font loader, called from the FONT record
// parser 0x42ce31): bands top-down, a band runs from the row after the last
// line of palette index 1 (column 0) to the next such line; glyphs are searched
// from the first column holding an index-2 baseline mark. Ink is index 3 or
// > 7; each run of ink columns is the next glyph (numbered across all bands,
// not 16 per band), its box the tight ink rectangle. Indices <= 7 are then
// cleared (index 3 sets the width of the space but is not drawn).
D2FontTex& FrontendDf2::font(const std::string& name) {
    const std::string key = upper(name);
    if (auto it = fonts_.find(key); it != fonts_.end()) return it->second;
    D2FontTex& f = fonts_[key];
    auto fi = mnu_.fonts.find(key);
    std::vector<uint8_t> data;
    IndexedImage img;
    if (fi == mnu_.fonts.end() || !vfs_.read(upper(fi->second.file), data) || !decodePcx(data, img)) return f;
    const int W = img.width, H = img.height;
    auto px = [&](int x, int y) { return img.pixels[size_t(y) * size_t(W) + size_t(x)]; };
    auto ink = [&](int x, int y) { const uint8_t v = px(x, y); return v == 3 || v > 7; };
    // 0x469240 / 0x4616d0 blend each pixel into the screen by the red byte of its
    // palette entry: dst += (colour - dst) * (red >> 1) >> 7.
    std::vector<uint8_t> rgba(size_t(W) * size_t(H) * 4, 0);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const uint8_t v = img.pixels[i];
        if (v <= 7) continue;
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = uint8_t(std::min(255, (img.palette[v].r >> 1) * 2));
    }
    int code = 0;
    for (int r = 0; r < H;) {
        int n = 0;
        while (r + n < H && px(0, r + n) != 1) ++n;
        if (r + n >= H) break;
        int col = 0;
        auto hasMark = [&](int c) { for (int k = 0; k < n; ++k) if (px(c, r + k) == 2) return true; return false; };
        while (col < W && !hasMark(col)) ++col;
        if (col >= W) break;
        while (col < W) {
            int y0 = H, y1 = -1;
            auto scan = [&](int c) {
                bool any = false;
                for (int k = 0; k < n; ++k)
                    if (ink(c, r + k)) { any = true; y0 = std::min(y0, r + k); y1 = std::max(y1, r + k); }
                return any;
            };
            if (!scan(col)) { ++col; continue; }
            const int x0 = col;
            int x1 = col++;
            while (col < W) {
                const bool any = scan(col++);
                if (!any) break;
                x1 = col - 1;
            }
            if (code < 256) f.glyphs[size_t(code)] = {x0, y0, x1 - x0 + 1, y1 - y0 + 1, y0 - r - 3};
            ++code;
        }
        r += n + 1;
    }
    f.height = f.glyphs['A'].h;
    f.heightX = f.glyphs['X'].h;
    f.tex = hud().makeImage(W, H, rgba);
    f.ok = code > 0;
    return f;
}

// 0x4694a0: width = sum of (glyph w + 2), height = the tallest glyph.
float FrontendDf2::textWidth(const std::string& fontName, const std::string& s) {
    D2FontTex& f = font(fontName);
    float w = 0;
    for (char c : s) w += float(f.glyphs[static_cast<unsigned char>(c)].w + 2);
    return w;
}

float FrontendDf2::textHeight(const std::string& fontName, const std::string& s) {
    D2FontTex& f = font(fontName);
    int h = 0;
    for (char c : s) h = std::max(h, f.glyphs[static_cast<unsigned char>(c)].h);
    return float(h);
}

// 0x469240: each glyph's ink box at (x, y + yoff), advancing w + 2; align 0
// left, 1 right, 2 centred (0x469420: x - w, x - w/2).
void FrontendDf2::text(const std::string& fontName, float x, float y, const std::string& s, uint32_t color, int align) {
    D2FontTex& f = font(fontName);
    if (!f.ok) return;
    const float w = textWidth(fontName, s);
    if (align == 1) x -= w;
    else if (align == 2) x -= std::floor(w / 2);
    const Color c = argbColor(color);
    for (char ch : s) {
        const Glyph& g = f.glyphs[static_cast<unsigned char>(ch)];
        if (g.w > 0 && g.h > 0)
            hud().imageUV(f.tex.texture, offX_ + x * scale_, offY_ + (y + float(g.yoff)) * scale_, float(g.w) * scale_, float(g.h) * scale_,
                          float(g.x) / float(f.tex.width), float(g.y) / float(f.tex.height), float(g.x + g.w) / float(f.tex.width),
                          float(g.y + g.h) / float(f.tex.height), c);
        x += float(g.w + 2);
    }
}

// GOG Df2.exe 0x41bf70 (the counter) and 0x41c0e0's loop: the text broken
// into lines of at most `width` pixels. Characters go into a line buffer
// (0x09 skipped; 0x0a, 0x0d and 0x0d 0x0a end the line); a space marks the
// last break point; once the buffer is wider than `width` the line is cut at
// that space and what followed it is read again, or, with no space, after the
// overflowing character. After every break the spaces that follow are
// skipped (leading spaces of the text itself are kept); runs of spaces stay.
// Returns the number of ended lines: a last line without a newline is not
// counted (but is in `lines`, when not empty).
int FrontendDf2::breakLines(const std::string& fontName, int width, const std::string& s, std::vector<std::string>* lines) {
    int count = 0;
    std::string buf;
    size_t lastSpace = std::string::npos;
    size_t i = 0;
    auto lineBreak = [&]() {
        if (lines) lines->push_back(buf);
        buf.clear();
        lastSpace = std::string::npos;
        ++count;
        while (i < s.size() && s[i] == ' ') ++i;
    };
    while (i < s.size() && s[i] != 0) {
        const char c = s[i];
        if (c == '\t') { ++i; continue; }
        if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
            ++i;
            lineBreak();
            continue;
        }
        buf += c;
        ++i;
        if (c == ' ') lastSpace = buf.size() - 1;
        if (int(textWidth(fontName, buf)) <= width) continue;
        if (lastSpace != std::string::npos) {
            i -= buf.size() - lastSpace - 1;   // read again what followed the space
            buf.resize(lastSpace);
        }
        lineBreak();
    }
    if (lines && !buf.empty()) lines->push_back(buf);
    return count;
}

// GOG Df2.exe 0x41c0e0 (CD 0x419d50): the text (its first 3999 characters)
// broken as breakLines at width x1 - x0; lines before `skipLines` are passed
// over, the others drawn from y0 down, (height of 'A') + 4 apart (the CD
// build: + 3), while y + (height of 'A') + 3 <= y1.
int FrontendDf2::wrapped(const std::string& fontName, float x0, float y0, float x1, float y1, const std::string& s, uint32_t color,
                         int skipLines) {
    D2FontTex& f = font(fontName);
    if (!f.ok) return 0;
    std::vector<std::string> lines;
    const int count = breakLines(fontName, int(x1 - x0), s.substr(0, 3999), &lines);
    const float hA = float(f.height);
    float y = y0;
    for (size_t i = size_t(std::max(0, skipLines)); i < lines.size() && y + hA + 3 <= y1; ++i, y += hA + (cdExe_ ? 3 : 4)) text(fontName, x0, y, lines[i], color);
    return count;
}

void FrontendDf2::blit(const HudImage& im, float x, float y, float sx, float sy, float sw, float sh) {
    if (!im.texture) return;
    hud().imageUV(im.texture, offX_ + x * scale_, offY_ + y * scale_, sw * scale_, sh * scale_, sx / float(im.width),
                  sy / float(im.height), (sx + sw) / float(im.width), (sy + sh) / float(im.height));
}

void FrontendDf2::rectOutline(float x0, float y0, float x1, float y1, uint32_t color) {
    const Color c = argbColor(color);
    const float t = std::max(1.f, scale_);
    auto R = [&](float x, float y, float w, float h) { hud().rect(offX_ + x * scale_, offY_ + y * scale_, w * scale_, h * scale_, c); };
    R(x0, y0, x1 - x0, t / scale_);
    R(x0, y1, x1 - x0 + 1, t / scale_);
    R(x0, y0, t / scale_, y1 - y0);
    R(x1, y0, t / scale_, y1 - y0);
}

void FrontendDf2::fill(float x0, float y0, float x1, float y1, uint32_t color) {
    hud().rect(offX_ + x0 * scale_, offY_ + y0 * scale_, (x1 - x0) * scale_, (y1 - y0) * scale_, argbColor(color));
}

void FrontendDf2::sound(const std::string& name, int channel) {
    // MNU [SOUNDS] (0x42f500): the file at its volume (x SFX volume of the soldier).
    auto it = mnu_.sounds.find(upper(name));
    const std::string f = it == mnu_.sounds.end() ? name : it->second;
    int vol = 80;
    if (upper(name) == "BTN_SLCT") vol = 150;
    vol = vol * std::clamp(soldier().get(0x5b8), 0, 255) / 255;
    if (const SoundClip* c = audio_.clip(vfs_, upper(f))) {
        // 0x43cb00 -> 0x4712f0(sample, direction 0, vol 0..255, pitch 1.0):
        // the mixer's gains (pan[0] = 63 left, 64 right) x vol x master 255
        // >> 16 (0x49fc69), passed as pan · vol / (127 · 255).
        const float gl = 63.f * float(vol) / (127.f * 255.f), gr = 64.f * float(vol) / (127.f * 255.f);
        // 0x42f500 plays on a channel: a new sound there cuts the old one.
        if (channel) {
            if (auto ch = channels_.find(channel); ch != channels_.end()) audio_.stop(ch->second);
            channels_[channel] = audio_.play(c, gl, gr);
        } else {
            audio_.play(c, gl, gr);
        }
    }
}

// ---------------------------------------------------------------- soldiers

// GOG Df2.exe 0x4368f0 (CD 0x431c00 filled 48 rows and none of the
// GOG fields): zeroed; the 59 key rows (0xb1 dwords) from the default key
// table; mouse 128 (+0x5b0), +0x5c8 1, +0x5cc 0x7f, SFX 255, music 255,
// dialogue 200; gear sidearm 5, primary 10, secondary 21, auxiliary 1,
// grenade 26; MACRO_0..MACRO_9 (MENU text, "??key??" when missing as
// 0x4338d0 does) into the 40-byte macro slots at +0xe8, 39 characters and
// a NUL. Version 14 files hold these, so what OpenDF writes is what GOG's
// 0x4369f0 reads without a patch.
void FrontendDf2::defaultSoldier(Soldier& s) {
    s.d.fill(0);
    std::memcpy(&s.d[0x2e0], keyTable_.data(), 0xb1 * 4);
    s.set(0x5b0, 0x80); s.set(0x5c8, 1); s.set(0x5cc, 0x7f); s.set(0x5b8, 0xff); s.set(0x5bc, 0xff); s.set(0x5c0, 200);
    s.set(0x2bc, 10); s.set(0x2b8, 5); s.set(0x2c0, 0x15); s.set(0x2c4, 1); s.set(0x2c8, 0x1a);
    for (int i = 0; i < 10; ++i) {
        const std::string key = "MACRO_" + std::to_string(i);
        const std::string t = menuText(key.c_str(), ("??" + key + "??").c_str());
        char* slot = reinterpret_cast<char*>(&s.d[0xe8 + 0x28 * size_t(i)]);
        std::strncpy(slot, t.c_str(), 0x27);
        slot[0x27] = 0;
    }
}

void FrontendDf2::loadSoldiers() {
    keyTable_ = loadDf2KeyTable(saveDir_);
    for (auto& s : soldiers_) defaultSoldier(s);
    std::ifstream f(saveDir_ / "df2plyrs.sav", std::ios::binary);
    char head[8] = {};
    // GOG Df2.exe 0x4369f0: 'FPBC' with version 10..14 reads the five
    // records, then patches by the file's version, one patch per version (not
    // cumulative); version 14 needs none. The CD's 0x431c90 took 10 only.
    if (f.read(head, 8) && std::memcmp(head, "FPBC", 4) == 0) {
        int32_t ver;
        std::memcpy(&ver, head + 4, 4);
        if (ver >= 10 && ver <= 14) {
            for (auto& s : soldiers_) f.read(reinterpret_cast<char*>(s.d.data()), std::streamsize(s.d.size()));
            if (ver == 10) {
                for (auto& s : soldiers_) { s.set(0x5c8, 1); s.set(0x5cc, 0x7f); }
            } else if (ver == 11) {
                // Key rows 48..57 (30 dwords) from the default key table
                // (0x4c7190 + 0x240, loadDf2KeyTable).
                for (auto& s : soldiers_) std::memcpy(&s.d[0x520], &keyTable_[0x240], 30 * 4);
            } else if (ver == 12) {
                // MACRO_0..MACRO_9 (MENU text, "??key??" when missing as
                // 0x4338d0 does) into the 40-byte macro slots at +0xe8.
                for (auto& s : soldiers_)
                    for (int i = 0; i < 10; ++i) {
                        const std::string key = "MACRO_" + std::to_string(i);
                        const std::string t = menuText(key.c_str(), ("??" + key + "??").c_str());
                        std::strncpy(reinterpret_cast<char*>(&s.d[0xe8 + 0x28 * size_t(i)]), t.c_str(), 0x28);
                    }
            } else if (ver == 13) {
                for (auto& s : soldiers_) s.set(0x598, 0x45); // key row 58: 'E'
            }
        }
    }
    // OpenDF's first DF2 builds wrote soldiers without the default bindings;
    // a named soldier with all rows empty gets the default rows (an OpenDF
    // repair). Rows 48..58 alone empty are left so: GOG's version 10 patch
    // does not fill them either (a CD file keeps them unbound).
    for (auto& s : soldiers_) {
        bool none = true;
        for (size_t i = 0; i < kDf2KeyRows && none; ++i) none = s.get(0x2e0 + 12 * i) == 0 && s.get(0x2e4 + 12 * i) == 0;
        if (none && !s.empty()) std::memcpy(&s.d[0x2e0], keyTable_.data(), 0xb1 * 4);
    }
    bool any = false;
    for (auto& s : soldiers_) any = any || !s.empty();
    // All empty: slot 0 is defaulted again and becomes CS_DEFAULTNAME "Blackhawk".
    if (!any) {
        defaultSoldier(soldiers_[0]);
        soldiers_[0].setName(menuText("CS_DEFAULTNAME", "Blackhawk"));
    }
}

// GOG Df2.exe 0x436890: 'FPBC', version 14, the five records.
void FrontendDf2::saveSoldiers() {
    std::ofstream f(saveDir_ / "df2plyrs.sav", std::ios::binary);
    const char head[8] = {'F', 'P', 'B', 'C', 14, 0, 0, 0};
    f.write(head, 8);
    for (auto& s : soldiers_) f.write(reinterpret_cast<const char*>(s.d.data()), std::streamsize(s.d.size()));
}

uint32_t FrontendDf2::header(const std::string& bms, size_t offset) {
    std::vector<uint8_t> d;
    if (!vfs_.read(bms + ".BMS", d) || d.size() < offset + 4) return 0;
    uint32_t v;
    std::memcpy(&v, &d[offset], 4);
    return v;
}

bool FrontendDf2::missionAvailable(int i) {
    // 0x42f97a: header +0x7C 0, or the soldier's unlock mask for the campaign shares a bit.
    const uint32_t need = header(kMissions[i].bms, 0x7C);
    return need == 0 || (uint32_t(soldier().get(0xa8 + 4 * size_t(kMissions[i].campaign))) & need);
}

bool FrontendDf2::missionOpen(int i) {
    // GOG Df2.exe 0x434670 (CD 0x42f8a0): completed missions count as available.
    return soldier().get(0x30 + 4 * size_t(i)) != 0 || missionAvailable(i);
}

// ---------------------------------------------------------------- screens

void FrontendDf2::enter(Screen s) {
    screen_ = next_ = s;
    stageTicks_ = 12;
    hover_.clear();
    pressed_.clear();
    helpText_.clear();
    helpShown_ = 0;
    static const char* kMenus[] = {"CHOOSESOLDIER", "MAIN", "CAMPAIGN", "QUICKMISSION", "MISSIONBRIEFING", "CHANGEGEAR", "OPTIONS1",
                                   "OPTIONS2", "OPTIONS3", "OPTIONS4", "CREDITS", "", ""};
    menuName_ = kMenus[int(s)];
    if (D2Menu* m = menu())
        for (int i : m->buttons) {
            D2Button& b = mnu_.buttons[size_t(i)];
            b.selected = false;
            b.flash = 0;
            if (b.drawstate == 2 || b.drawstate == 3) b.drawstate = 1;
            layout(b);
        }
    sound("mnu_tran", 1);
    // Music Var1 per state (0x43cc20).
    const int var1 = s == Screen::Campaign ? 1 : s == Screen::Options1 || s == Screen::Options2 ? 3 : s == Screen::Soldier ? 4 : s == Screen::Credits ? 5
                   : s == Screen::Briefing ? 6 : s == Screen::Gear ? 7 : 0;
    music_.setVar(1, var1);
    switch (s) {
    case Screen::Soldier: refreshSoldier(); break;
    case Screen::Main:
        mmHover_.clear();   // 0x43a5ae
        soldier().set(0x28, 1);   // 0x43a5c9: the Campaign page opens on campaign 1
        // NovaWorld, Multiplayer and Update are not in OpenDF.
        for (const char* n : {"but_mm_qmb", "but_mm_nwb", "but_mm_upd"})
            if (D2Button* b = btn(n)) b->drawstate = 4;
        break;
    case Screen::Campaign: campaignInit(); break;
    case Screen::Quick: refreshQuick(); break;
    case Screen::Briefing: loadBriefing(); break;
    case Screen::Gear: refreshGear(); break;
    case Screen::Options1:
    case Screen::Options3: refreshOptions(); break;
    case Screen::Credits: loadCredits(); break;
    case Screen::Options2:
        // 0x439ea0: snapshot the bindings on the first visit, scroll to the
        // top (range forced to 50, GOG Df2.exe 0x43f44f; CD 39), clear the box, REMAP_CLICK.
        if (!haveKeySnapshot_) {
            std::memcpy(keySnapshot_.data(), &soldier().d[0x2e0], keySnapshot_.size());
            haveKeySnapshot_ = true;
        }
        if (D2Button* b = btn("but_opt2_vsl")) { b->p1 = 50; b->p2 = 0; }
        remapMessage("REMAP_CLICK");
        refreshKeys();
        break;
    default: break;
    }
}

// Credits init 0x430e00 / parser 0x451290: [MISSIONSCRIPT] SCRIPT= lines,
// fields by strtok(", "), command names case-sensitive, {END} stops.
void FrontendDf2::loadCredits() {
    creditCmds_.clear();
    creditObjs_ = {};
    creditPc_ = 0;
    creditPause_ = 0;
    creditDone_ = false;
    std::vector<uint8_t> raw;
    if (!creditTextLoaded_ && vfs_.read("CREDITS.BIN", raw)) creditTextLoaded_ = creditText_.parse(raw);
    if (!vfs_.read("CREDITS.SCR", raw)) return;
    std::istringstream in(std::string(raw.begin(), raw.end()));
    std::string line;
    bool inScript = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '[') { inScript = upper(line).rfind("[MISSIONSCRIPT]", 0) == 0; continue; }
        if (!inScript || line.rfind("SCRIPT=", 0) != 0) continue;
        std::vector<std::string> f;
        std::string cur;
        for (char c : line.substr(7)) {
            if (c == ',' || c == ' ' || c == '\t') { if (!cur.empty()) f.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) f.push_back(cur);
        if (f.empty()) continue;
        auto num = [&](size_t i) { return i < f.size() ? std::atoi(f[i].c_str()) : 0; };
        auto str = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
        CreditCmd c;
        const std::string& n = f[0];
        if (n == "{END}") break;
        if (n == "{PRINTTEXT}") {
            c = {0, num(1), num(2), num(3), 0, num(6), num(7), 0, 0, str(4), str(5), str(8)};
            const std::string* t = creditText_.find("Credits", c.str);
            if (!t) t = creditText_.find("CREDITS", c.str);
            if (t) c.str = *t;
        } else if (n == "{PAUSE}") c = {2, 0, 0, 0, num(1)};
        else if (n == "{DISPLAYANIM}") c = {3, num(1), num(2), num(3), num(6), num(4), num(5), 0, 0, "", "", str(8)};
        else if (n == "{DISPLAYPIC}") c = {4, num(1), num(2), num(3), 0, num(4), num(5), 0, 0, "", "", str(6)};
        else if (n == "{DRAWLINE}") c = {6, num(1), num(2), num(3), 0, num(5), num(6), num(7), num(8), "", str(4)};
        else if (n == "{PLAYSOUND}") c = {8, 0, 0, 0, 0, num(2), 0, 0, 0, "", "", str(1)};
        else if (n == "{PRINTTEXTTTY}") c.type = 1;
        else if (n == "{REMOVEOBJECT}") c.type = 5;
        else if (n == "{REMOVELINE}") c.type = 7;
        else continue;   // unknown: skipped
        creditCmds_.push_back(c);
    }
}

// Credits tick 0x430ea0.
void FrontendDf2::creditTick() {
    if (!creditDone_) {
        if (creditPause_ > 0) --creditPause_;
        else
            while (true) {
                if (creditPc_ >= creditCmds_.size()) {
                    // The end of the list: straight to the Main Menu.
                    creditDone_ = true;
                    next_ = Screen::Main;
                    break;
                }
                const CreditCmd& c = creditCmds_[creditPc_++];
                if (c.type == 2) { creditPause_ = c.t; break; }
                if (c.type == 8) {
                    // PLAYSOUND on channel 2 (volume scaling untraced; unused by DF2's credits).
                    sound(c.str, 2);
                    continue;
                }
                if (c.type != 0 && c.type != 3 && c.type != 4 && c.type != 6) continue;   // TTY text, removals: ignored
                if (c.id < 0 || c.id >= int(creditObjs_.size())) continue;
                CreditObj& o = creditObjs_[size_t(c.id)];
                o = {};
                o.type = c.type == 0 ? 1 : c.type == 4 ? 2 : c.type == 3 ? 3 : 4;
                o.flags = c.flags;
                o.life = c.life;
                o.x1 = c.x1; o.y1 = c.y1; o.x2 = c.x2; o.y2 = c.y2;
                o.t = c.t;
                o.font = c.font;
                o.str = c.str;
                if (auto it = mnu_.colors.find(upper(c.color)); it != mnu_.colors.end()) o.color = it->second;
            }
    }
    for (CreditObj& o : creditObjs_) {
        if (!o.type) continue;
        if (o.life > 0) --o.life;
        else if (o.life == 0) o.type = 0;
    }
}

// cr_ScriptRender 0x4316e0: slots in index order.
void FrontendDf2::drawCredits() {
    for (const CreditObj& o : creditObjs_) {
        if (o.type == 4) {
            // 0x461920: a one-pixel line, end points included.
            const Color c = argbColor(o.color);
            if (o.y1 == o.y2) fill(float(std::min(o.x1, o.x2)), float(o.y1), float(std::max(o.x1, o.x2) + 1), float(o.y1 + 1), o.color);
            else if (o.x1 == o.x2) fill(float(o.x1), float(std::min(o.y1, o.y2)), float(o.x1 + 1), float(std::max(o.y1, o.y2) + 1), o.color);
            else hud().line(offX_ + (float(o.x1) + 0.5f) * scale_, offY_ + (float(o.y1) + 0.5f) * scale_, offX_ + (float(o.x2) + 0.5f) * scale_,
                            offY_ + (float(o.y2) + 0.5f) * scale_, std::max(1.f, scale_), c);
        } else if (o.type == 2) {
            // Flags 0 solid, 1 colour-keyed (pixel 0), 2 a 50 % blend, else nothing.
            // (MNU PCX images are loaded colour-keyed; flags 0 draws them so too.)
            const HudImage im = image(o.str);
            if (!im.texture || o.flags < 0 || o.flags > 2) continue;
            hud().imageUV(im.texture, offX_ + float(o.x1) * scale_, offY_ + float(o.y1) * scale_, float(im.width) * scale_,
                          float(im.height) * scale_, 0, 0, 1, 1, Color{1, 1, 1, o.flags == 2 ? 0.5f : 1.f});
        } else if (o.type == 3) {
            // 0x462d20: frame (life / t) % frames.
            const HudImage im = image(o.str);
            const int frames = std::max(1, imageFrames(o.str));
            if (!im.texture) continue;
            const int f = (o.life / std::max(1, o.t)) % frames;
            const float fh = float(im.height / frames);
            blit(im, float(o.x1), float(o.y1), 0, fh * float(std::max(0, f)), float(im.width), fh);
        } else if (o.type == 1) {
            const int align = (o.flags & 1) ? 0 : (o.flags & 2) ? 1 : 2;
            if (o.flags & 8) text(o.font, float(o.x1 + 1), float(o.y1 + 1), o.str, 0xFF010101, align);
            text(o.font, float(o.x1), float(o.y1), o.str, o.color, align);
        }
    }
}

// GOG Df2.exe 0x43f9b0 (CD 0x439fe0): a binding's text. The key first by
// 0x43f520's names (MENU text keys), then the punctuation keys, then "%c"
// when the VK is a printable character (ctype & 0x157), else "KEY %d".
std::string FrontendDf2::keyName(size_t row) {
    const uint32_t vk = uint32_t(soldier().get(0x2e0 + 12 * row)), m = uint32_t(soldier().get(0x2e4 + 12 * row));
    std::string s;
    static const std::pair<uint32_t, const char*> named[] = {
        {0x03, "CANCEL"}, {0x08, "BACKSPACE"}, {0x09, "TAB"}, {0x0C, "CLEAR"}, {0x0D, "ENTER"}, {0x10, "SHIFT"},
        {0x11, "CTRL"}, {0x12, "ALT"}, {0x14, "CAPS_LOCK"}, {0x15, "HANGUL"}, {0x17, "JUNJA"}, {0x18, "FINAL"},
        {0x19, "KANJI"}, {0x1C, "CONVERT"}, {0x1D, "NONCONVERT"}, {0x1E, "ACCEPT"}, {0x1F, "MODECHANGE"},
        {0x20, "SPACE_BAR"}, {0x21, "PAGE_UP"}, {0x22, "PAGE_DOWN"}, {0x23, "END"}, {0x24, "HOME"}, {0x25, "LEFT"},
        {0x26, "UP"}, {0x27, "RIGHT"}, {0x28, "DOWN"}, {0x29, "SELECT"}, {0x2B, "EXECUTE"}, {0x2C, "SNAPSHOT"},
        {0x2D, "INSERT"}, {0x2E, "DELETE"}, {0x2F, "HELP"}, {0x5D, "APPS"}, {0x60, "KEYPAD_0"}, {0x61, "KEYPAD_1"},
        {0x62, "KEYPAD_2"}, {0x63, "KEYPAD_3"}, {0x64, "KEYPAD_4"}, {0x65, "KEYPAD_5"}, {0x66, "KEYPAD_6"},
        {0x67, "KEYPAD_7"}, {0x68, "KEYPAD_8"}, {0x69, "KEYPAD_9"}, {0x6A, "KEYPAD_*"}, {0x6B, "KEYPAD_+"},
        {0x6C, "SEPARATOR"}, {0x6D, "KEYPAD_-"}, {0x6E, "KEYPAD_."}, {0x6F, "KEYPAD_/"}, {0x90, "NUM_LOCK"},
        {0xA4, "LEFT_MENU"}, {0xA5, "RIGHT_MENU"}, {0xE5, "PROCESS"}, {0xF6, "ATTN"}, {0xF7, "CRSEL"},
        {0xF8, "EXSEL"}, {0xF9, "EREOF"}, {0xFA, "PLAY"}, {0xFB, "ZOOM"}, {0xFC, "NONAME"}, {0xFD, "PA1"},
        {0xFE, "OEM_CLEAR"}};
    static const std::pair<uint32_t, const char*> punct[] = {
        {0xBA, ";"}, {0xBB, "="}, {0xBC, ","}, {0xBD, "-"}, {0xBE, "."}, {0xBF, "/"},
        {0xDB, "["}, {0xDC, "\\"}, {0xDD, "]"}, {0xDE, "'"}};
    bool found = vk == 0;
    for (const auto& [v, k] : named)
        if (!found && v == vk) { s = upper(menuText(k, k)); found = true; }
    for (const auto& [v, k] : punct)
        if (!found && v == vk) { s = k; found = true; }
    if (!found) {
        // MSVC's C-locale ctype & 0x157 (alpha, digit, punct, blank): 0x20..0x7e and tab.
        if ((vk >= 0x20 && vk < 0x7F) || vk == 0x09) s = std::string(1, char(vk));
        else s = upper(menuText("KEY", "KEY")) + " " + std::to_string(vk);
    }
    if (m) {
        if (vk) s += " " + upper(menuText("OR", "OR")) + " ";
        s += upper(menuText("MOUSE", "MOUSE")) + " " + std::to_string(m);
    }
    return s;
}

// 0x43a5a0: ten rows from the slider's value.
void FrontendDf2::refreshKeys() {
    const D2Button* vsl = btn("but_opt2_vsl");
    const int top = vsl ? vsl->p2 : 0;
    for (int i = 0; i < 10; ++i) {
        const size_t row = size_t(std::clamp(top + i, 0, int(kDf2KeyRows) - 1));
        std::string label = menuText(kDf2Keys[row].label, kDf2Keys[row].label);
        if (!menuText_.find("MENU", kDf2Keys[row].label)) std::replace(label.begin(), label.end(), '_', ' ');
        char n[24];
        std::snprintf(n, sizeof n, "but_opt2_keyl%d", i);
        if (D2Button* b = btn(n)) b->text = label;
        std::snprintf(n, sizeof n, "but_opt2_keyb%d", i);
        if (D2Button* b = btn(n)) b->text = keyName(row);
    }
}

// Options tick 0x439510: a caught mouse button first, else one key-down.
void FrontendDf2::captureTick() {
    if (!keyCapture_) return;
    const size_t row = size_t(keyRow_);
    auto at = [&](size_t r, size_t o) { return soldier().get(0x2e0 + 12 * r + o); };
    if (keyMouse_) {
        for (size_t r = 0; r < kDf2KeyRows; ++r)
            if (r != row && at(r, 4) == keyMouse_) soldier().set(0x2e4 + 12 * r, 0);
        soldier().set(0x2e4 + 12 * row, keyMouse_);
        remapMessage("REMAP_OK");
        refreshKeys();
    } else if (!keyQueue_.empty()) {
        const uint32_t vk = keyQueue_.front();
        keyQueue_.pop_front();
        for (size_t r = 0; r < kDf2KeyRows; ++r)
            if (r != row && uint32_t(at(r, 0)) == vk) soldier().set(0x2e0 + 12 * r, 0);
        if (df2KeyAllowed(vk)) {
            soldier().set(0x2e0 + 12 * row, int32_t(vk));
            remapMessage("REMAP_OK");
            refreshKeys();
        } else {
            remapMessage("REMAP_RESERVED");
        }
    } else {
        return;
    }
    keyMouse_ = 0;
    keyCapture_ = false;
}

// Button size (GOG Df2.exe 0x432690, CD 0x42d8c0), +0x74 x +0x78: with an
// image a push button is image width x height / 4 (always 4 stacked frames,
// whatever the IMAGE record's frame count), a slider the whole image and any
// other type 0 x 0 (so pictures are never under the mouse); without one the
// text's box or (p1, p2). Then a slider's track p3 is its length.
void FrontendDf2::layout(D2Button& b) {
    const HudImage im = b.image.empty() ? HudImage{} : image(b.image);
    if (im.texture) {
        b.w = b.h = 0;
        if (b.type == 1) { b.w = im.width; b.h = im.height / 4; }
        if (b.type == 2 || b.type == 3) { b.w = im.width; b.h = im.height; }
    } else if (!b.mnuText.empty()) {
        // 0x42dc7c: (user1 - x, user2 - y); if either is not positive both come
        // from the text's measure 0x4694a0 (the text as stored, '&' removed).
        b.w = b.user1 - b.x;
        b.h = b.user2 - b.y;
        if (b.w <= 0 || b.h <= 0) {
            std::string t = b.mnuText;
            if (size_t a = t.find('&'); a != std::string::npos && a + 1 < t.size()) t.erase(a, 1);
            const std::string f = b.font.empty() ? "FRK12N" : b.font;
            b.w = int(textWidth(f, t));
            b.h = int(textHeight(f, t));
        }
    } else {
        b.w = b.p1;
        b.h = b.p2;
    }
    if (b.type == 2) b.w = b.p3;
    if (b.type == 3) b.h = b.p3;
}

// GOG Df2.exe 0x4337a0 (CD 0x42e9f0): the first button of the page, from the
// highest z-order, whose box holds the mouse. Only J (0x200) is ignored: any
// type, drawstate, C or callback-less button is hit (and so covers what lies
// below it); what happens then depends on the type (0x433ec0). Without an
// image and G a box is 1.6 times as tall, and lies left of x when
// right-justified (0x10) but not left-justified (8).
D2Button* FrontendDf2::hit(int x, int y) {
    D2Menu* m = menu();
    if (!m) return nullptr;
    for (int i : m->buttons) {
        D2Button& b = mnu_.buttons[size_t(i)];
        if (b.attrib & 0x200) continue;
        int x0 = b.x, h = b.h;
        if (b.image.empty() && !(b.attrib & 0x40)) {
            h = int(float(b.h) * 1.6f);
            if (!(b.attrib & 8) && (b.attrib & 0x10)) x0 -= b.w;
        }
        if (x0 <= x && b.y <= y && x < x0 + b.w && y < b.y + h) return &b;
    }
    return nullptr;
}

// 0x43d4c0: the default drawing (skipped for C or state 0), then a G
// button's owner draw (event 7) on top of it.
void FrontendDf2::drawButton(D2Button& b) {
    if (b.drawstate == 0) return;
    if (!(b.attrib & 4)) drawDefault(b);
    if (b.attrib & 0x40) ownerDraw(b);
}

void FrontendDf2::drawDefault(D2Button& b) {
    const HudImage im = b.image.empty() ? HudImage{} : image(b.image);
    const int frames = b.type == 1 ? 4 : std::max(1, imageFrames(b.image));
    if (im.texture) {
        const float fh = float(im.height / frames);
        if (b.type == 1) {
            int f = std::clamp(b.drawstate - 1, 0, frames - 1);
            if (b.selected || b.flash > 0) f = std::min(2, frames - 1);
            blit(im, float(b.x), float(b.y), 0, fh * float(f), float(im.width), fh);
        } else if (b.type == 4) {
            blit(im, float(b.x), float(b.y), 0, fh * float(std::clamp(b.p2, 0, frames - 1)), float(im.width), fh);
        } else if (b.type == 2 || b.type == 3) {
            // 0x43d8a0 / 0x43d919: a disabled slider is not drawn.
            if (b.drawstate == 4) return;
            const float pos = b.p1 > 0 ? float(b.p2) * float(b.p3) / float(b.p1) : 0;
            blit(im, float(b.x) + (b.type == 2 ? pos : 0), float(b.y) + (b.type == 3 ? pos : 0), 0, 0, float(im.width), fh);
        }
        // 0x43d98e: other types draw no image, only their text.
    }
    if (b.type >= 2 && b.type <= 4) return;   // sliders and animations have no text
    if (b.text.empty() || b.font.empty()) return;
    auto fi = mnu_.fonts.find(b.font);
    const D2Font df = fi == mnu_.fonts.end() ? D2Font{} : fi->second;
    D2FontTex& ft = font(b.font);
    // The set-text routine 0x42d7b0 (all 99 callers) removes the first '&' that
    // has a character after it, keeps that character's index (+0x90) and the
    // character upper-cased (+0x8c) as the button's key. Done here at draw time.
    std::string t = b.text;
    int hotIdx = 0;
    bool hot = false;
    if (size_t a = t.find('&'); a != std::string::npos && a + 1 < t.size()) {
        t.erase(a, 1);
        hotIdx = int(a);
        hot = true;
    }
    const int x = b.x, y = b.y;
    if (b.type != 1) {
        // 0x43d98e (types other than 1-4): lit in state 2, disabled in 4; the box
        // (x, y)-(x + w, y + height of 'A'); F/H centre in [x, x + w], E right in
        // [x - w + 2, x - 2], D left in [x + 2, x + w - 2] (an image's box
        // [x + 2, x + w - 2] wins); no hot-key underline. The justification is a
        // local of the draw loop that only F/H/E/D set (staleJustify0_).
        const uint32_t c = b.drawstate == 2 ? df.fg[1] : b.drawstate == 4 ? df.fg[2] : df.fg[0];
        int l = x, r = x;
        if (b.attrib & 0xA0) { staleJustify0_ = 4; l = x; r = x + b.w; }
        if (b.attrib & 0x10) { staleJustify0_ = 2; l = x - b.w + 2; r = x - 2; }
        if (b.attrib & 0x08) { staleJustify0_ = 1; l = x + 2; r = x + b.w - 2; }
        if (im.texture) { l = x + 2; r = x + b.w - 2; }
        boxText(b.font, l, r, y, y + ft.height, t, c, staleJustify0_);
        return;
    }
    // 0x43d5d2 (push buttons): lit in state 2 or while selected / flashing,
    // disabled in state 4.
    const bool lit = b.selected || b.flash > 0;
    const uint32_t c = b.drawstate == 4 ? df.fg[2] : (lit || b.drawstate == 2) ? df.fg[1] : df.fg[0];
    // 0x43d653: the box starts as (x, y)-(x, y + h): F/H/E/D only set the
    // justification (4 / 2 / 1); their box sizes are all overwritten below by
    // the image's [x + 2 (+ user1 for D), x + w - 2]. Without an image the box
    // has no width, so a centred image-less button is centred on x itself and a
    // right-justified one ends at x. A button with none of F/H/E/D keeps the
    // previous push button's value (a stale local, staleJustify1_).
    int l = x, r = x, top = y;
    const int bottom = y + b.h;
    if (b.attrib & 0xA0) staleJustify1_ = 4;
    if (b.attrib & 0x10) staleJustify1_ = 2;
    if (b.attrib & 0x08) staleJustify1_ = 1;
    if (im.texture) {
        l = x + 2 + ((b.attrib & 0x08) ? b.user1 : 0);
        r = x + b.w - 2;
        // 0x43d71c: without a hot key the box top is raised by 0.4 x the height
        // of 'X' (float 0x4a1418, truncated by _ftol 0x479f64).
        if (!hot) top = y - int(float(ft.heightX) * 0.4f);
    }
    if (!hot) {
        boxText(b.font, l, r, top, bottom, t, c, staleJustify1_);
        return;
    }
    // Hot-key text: the text, then '_' one pixel lower under the key character
    // (0x419fd0 left at l, 0x41a1f0 right-aligned to l, 0x41a0d0 centred on
    // l + (r - l) / 2 at y + (bottom - y - height of 'A') / 4; the first two
    // draw at y itself).
    const float W = textWidth(b.font, t), pre = textWidth(b.font, t.substr(0, size_t(hotIdx)));
    float tx = float(l), ty = float(y);
    if (staleJustify1_ == 2) tx = float(l) - W;
    else if (staleJustify1_ != 1) {
        tx = float(l + ((r - l) >> 1)) - float(int(W) >> 1);
        ty = float(y + ((bottom - ft.height - y) >> 2));
    }
    text(b.font, tx, ty, t, c);
    text(b.font, tx + pre, ty + 1, "_", c);
}

// 0x469380: the text in a box, vertically centred on its tallest glyph
// ((bottom - top - h) >> 1); justify 2 right, 4 centred, anything else left.
void FrontendDf2::boxText(const std::string& fontName, int l, int r, int top, int bottom, const std::string& s, uint32_t color, int justify) {
    const int W = int(textWidth(fontName, s)), H = int(textHeight(fontName, s));
    const int ty = top + ((bottom - top - H) >> 1);
    const int tx = justify == 2 ? r - W : justify == 4 ? l + ((r - l - W) >> 1) : l;
    text(fontName, float(tx), float(ty), s, color);
}

// GOG Df2.exe 0x4436a0: the cursor image at (mouse - hot spot), the area
// under it saved first (0x46b4f0) and put back before the next frame
// (0x443d90); 0x46d130 skips pixels whose RGB is 0 and draws the others
// opaque. Clipped to the 640x480 screen. Hit tests use the mouse itself.
void FrontendDf2::drawCursor() {
    if (!cursorLoaded_) {
        cursorLoaded_ = true;
        auto it = mnu_.images.find("IMAGECURTARG");
        std::vector<uint8_t> data;
        RgbaImage rgba;
        if (it != mnu_.images.end() && vfs_.read(upper(it->second.file), data) && decodeRgbaImage(data, rgba)) {
            for (size_t i = 0; i + 3 < rgba.rgba.size(); i += 4)
                rgba.rgba[i + 3] = (rgba.rgba[i] | rgba.rgba[i + 1] | rgba.rgba[i + 2]) ? 255 : 0;
            cursor_ = hud().makeImage(rgba.width, rgba.height, rgba.rgba);
        }
    }
    if (!cursor_.texture) return;
    hud().clip(offX_, offY_, 640.f * scale_, 480.f * scale_);
    blit(cursor_, float(mouseX_ - 12), float(mouseY_ - 12), 0, 0, float(cursor_.width), float(cursor_.height));
    hud().unclip();
}

// 0x46b790 for the axis-parallel lines the pages draw: one pixel wide, both
// end points included.
void FrontendDf2::hvLine(int x0, int y0, int x1, int y1, uint32_t color) {
    fill(float(std::min(x0, x1)), float(std::min(y0, y1)), float(std::max(x0, x1) + 1), float(std::max(y0, y1) + 1), color);
}

void FrontendDf2::drawConnectors() {
    if (screen_ == Screen::Soldier) {
        // GOG Df2.exe 0x436720 (from 0x4366da): 0xCA4409 when the current
        // soldier has a name (record +4), else 0x844030. A trunk at x 357,
        // y 80..326; the current slot but_cs_<n> joined at its middle row to
        // x 41, down to the middle of but_cs_oirct and across to the slot's
        // x, and from its right edge to the trunk; the trunk's ends joined to
        // the middles of but_cs_fcrct (to its top) and but_cs_aprct (to its
        // bottom) at x (x + user1) / 2.
        const uint32_t c = soldier().empty() ? 0x844030 : 0xCA4409;
        char n[16];
        std::snprintf(n, sizeof n, "but_cs_%d", current_);
        const D2Button* b = btn(n);
        const D2Button* oi = btn("but_cs_oirct");
        const D2Button* fc = btn("but_cs_fcrct");
        const D2Button* ap = btn("but_cs_aprct");
        if (!b || !oi || !fc || !ap) return;
        hvLine(357, 80, 357, 326, c);
        const int cy = (b->h >> 1) + b->y, oy = int(unsigned(oi->user2 - oi->y) >> 1) + oi->y;
        hvLine(41, cy, b->x, cy, c);
        hvLine(41, cy, 41, oy, c);
        hvLine(41, oy, b->x, oy, c);
        hvLine(b->x + b->w, cy, 357, cy, c);
        const int fx = int(unsigned(fc->user1 - fc->x) >> 1) + fc->x;
        hvLine(357, 80, fx, 80, c);
        hvLine(fx, 80, fx, fc->y, c);
        const int ax = int(unsigned(ap->user1 - ap->x) >> 1) + ap->x;
        hvLine(357, 326, ax, 326, c);
        hvLine(ax, 326, ax, ap->user2, c);
    } else if (screen_ == Screen::Main) {
        // GOG Df2.exe 0x43a780 (from 0x43a6da): while a button's help is
        // shown, from its middle row at x 155 down to the middle of
        // but_mm_hlrct, both ends running right to 2 left of the button.
        const D2Button* b = btn(mmHover_);
        const D2Button* hl = btn("but_mm_hlrct");
        if (!b || !hl) return;
        const int cy = (b->h >> 1) + b->y, x2 = b->x - 2, hy = int(unsigned(hl->user2 - hl->y) >> 1) + hl->y;
        hvLine(155, cy, x2, cy, 0xCA4409);
        hvLine(155, cy, 155, hy, 0xCA4409);
        hvLine(155, hy, x2, hy, 0xCA4409);
    }
}

void FrontendDf2::drawMenu() {
    D2Menu* m = menu();
    if (!m) return;
    const HudImage bg = image(m->background);
    if (bg.texture) blit(bg, 0, 0, 0, 0, float(bg.width), float(bg.height));
    staleJustify0_ = staleJustify1_ = 0;
    for (auto it = m->buttons.rbegin(); it != m->buttons.rend(); ++it) drawButton(mnu_.buttons[size_t(*it)]);
}

// ---------------------------------------------------------------- owner draws

void FrontendDf2::ownerDraw(D2Button& b) {
    const std::string& cb = b.callback;
    if (cb == "mm_Rect") {
        // 0x435fd0: a one-pixel rectangle (x, y)-(user1, user2), 0xCA4409 (0x844030 disabled).
        rectOutline(float(b.x), float(b.y), float(b.user1), float(b.user2), b.drawstate == 4 ? 0x844030 : 0xCA4409);
        // The Options2 message box (0x439f60): LCD10N at +5, 130x145, in the box font's colour.
        if (b.name == "but_opt2_ttybox" && !helpText_.empty())
            wrapped("LCD10N", float(b.x + 5), float(b.y + 5), float(b.x + 5 + 130), float(b.y + 5 + 145), helpText_.substr(0, helpShown_),
                    mnu_.fonts[b.font].fg[0]);
        if (b.name == "but_mm_hlrct" && !helpText_.empty())
            wrapped("LCD10N", float(b.x + 5), float(b.y + 5), float(b.user1 - 5), float(b.user2 - 5), helpText_.substr(0, helpShown_),
                    mnu_.fonts["LCD10N"].fg[0]);
    } else if (cb == "cr_ScriptRender") {
        drawCredits();
    } else if (cb == "opt4_Explain") {
        // 0x43ad30: the button's text wrapped in (x, y)-(user1, user2), coloured by state.
        auto fi = mnu_.fonts.find(b.font);
        const D2Font df = fi == mnu_.fonts.end() ? D2Font{} : fi->second;
        wrapped(b.font, float(b.x), float(b.y), float(b.user1), float(b.user2), b.text,
                b.drawstate == 4 ? df.fg[2] : b.drawstate == 2 ? df.fg[1] : df.fg[0]);
    } else if (cb == "ca_DrawRegion") {
        const int region = campRegion(campaign_);
        if (region >= 0 && region < 5) {
            const HudImage im = image(kRegionImage[region]);
            blit(im, float(b.x), float(b.y), 0, 0, float(im.width), float(im.height));
            // Titles of the available missions under their dots (LCD10N; LCD10S selected).
            for (int i = 0; i < 28; ++i) {
                if (kMissions[i].region != region || kMissions[i].campaign != campaign_ || !missionOpen(i)) continue;
                char name[16];
                std::snprintf(name, sizeof name, "but_ca_%c%c%c%c", kMissions[i].bms[1], kMissions[i].bms[2], kMissions[i].bms[4], kMissions[i].bms[5]);
                const D2Button* dot = btn(name);
                const std::string* t = briefText_.find("TITLE", kMissions[i].bms);
                if (!dot || !t) continue;
                const std::string title = t->substr(0, t->find('\r'));
                const float w = textWidth("LCD10N", title);
                const float x = std::clamp(float(dot->x + dot->w / 2) - w / 2, float(b.x + 2), float(b.user1) - w - 2);
                text(i == missionSel_ ? "LCD10S" : "LCD10N", x, float(dot->y + dot->h + 1), title,
                     mnu_.fonts[i == missionSel_ ? "LCD10S" : "LCD10N"].fg[0]);
            }
        }
    } else if (cb == "ca_CampaignBrief") {
        std::string key = campaign_ == 1 ? "C03" : "C05";
        if (missionSel_ >= 0) key = kMissions[missionSel_].bms;
        if (const std::string* t = briefText_.find("CABRIEF", key))
            wrapped("LCD10S", float(b.x), float(b.y), float(b.user1), float(b.user2), *t, mnu_.fonts["LCD10S"].fg[0]);
    } else if (cb == "qm_Brief") {
        if (quickSel_ >= 0 && quickSel_ < int(quick_.size()))
            if (const std::string* t = briefText_.find("QMBRIEF", quick_[size_t(quickSel_)]))
                wrapped("LCD10S", float(b.x), float(b.y), float(b.user1), float(b.user2), *t, mnu_.fonts["LCD10S"].fg[0]);
    } else if (cb == "qm_SelMis") {
        const int i = quickTop_ + b.user1;
        if (i < int(quick_.size())) {
            const HudImage im = image(b.image);
            const float fh = float(im.height / 4);
            blit(im, float(b.x), float(b.y), 0, fh * float(i == quickSel_ ? 2 : b.drawstate == 2 ? 1 : 0), float(im.width), fh);
            const std::string* t = briefText_.find("TITLE", quick_[size_t(i)]);
            std::string title = t ? *t : quick_[size_t(i)];
            const size_t br = title.find('\r');
            const std::string l1 = title.substr(0, br), l2 = br == std::string::npos ? "" : title.substr(title.find('\n', br) + 1);
            const uint32_t c = mnu_.fonts["FRK12N"].fg[i == quickSel_ ? 1 : 0];
            text("FRK12N", float(b.x + 6), float(b.y + 4), l1, c);
            text("FRK11N", float(b.x + 6), float(b.y + 20), l2, mnu_.fonts["FRK11N"].fg[0]);
        }
    } else if (cb == "mb_TextBrief") {
        // GOG Df2.exe 0x43d120, event 7, not in drawstate 0: the border
        // (0x43afc0), then the button's text (the briefing, set by 0x43d000)
        // in its font, coloured by state, from (x + 5, y + 5), width
        // user1 - x - 10, down to user2, from line but_mb_brvsl's value.
        rectOutline(float(b.x), float(b.y), float(b.user1), float(b.user2), b.drawstate == 4 ? 0x844030 : 0xCA4409);
        auto fi = mnu_.fonts.find(b.font);
        const D2Font df = fi == mnu_.fonts.end() ? D2Font{} : fi->second;
        wrapped(b.font, float(b.x + 5), float(b.y + 5), float(b.user1 - 5), float(b.user2), b.text,
                b.drawstate == 4 ? df.fg[2] : b.drawstate == 2 ? df.fg[1] : df.fg[0], sliderValue("but_mb_brvsl"));
    } else if (cb == "mb_Map") {
        // GOG Df2.exe 0x43d250 (CD 0x437fd0), event 7, only with the picture
        // (+0x64): the mm_Rect border (0x43afc0), then the tiled terrain
        // picture 1:1 (0x46c6d0) at (x+1, y+1), (user1-x-1) x (user2-y-1),
        // from the sliders' values.
        if (!briefMap_.texture) return;
        rectOutline(float(b.x), float(b.y), float(b.user1), float(b.user2), b.drawstate == 4 ? 0x844030 : 0xCA4409);
        const int sx = sliderValue("but_mb_mphsl"), sy = sliderValue("but_mb_mpvsl"), w = b.user1 - b.x, h = b.user2 - b.y;
        blit(briefMap_, float(b.x + 1), float(b.y + 1), float(sx), float(sy),
             float(std::min(w - 1, briefMap_.width)), float(std::min(h - 1, briefMap_.height)));
        // Then three passes over the markers of 0x4045e0. A point's screen
        // position is view origin + point - scroll (not +1).
        auto visible = [&](int x0, int y0, int x1, int y1, int x, int y) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; };   // 0x43d220
        auto route = [](int marker) { return marker == 1 || marker == 2 || marker == 3; };
        // 0x46c8e0: frame f of an icon strip of 8 frames stacked vertically, index 0 keyed.
        auto icon = [&](const char* file, int frame, int x, int y) {
            const HudImage im = this->file(file);
            if (!im.texture || frame < 0 || frame >= 8) return;
            const float fh = float(im.height) / 8.f;
            blit(im, float(x), float(y), 0, fh * float(frame), float(im.width), fh);
        };
        // (1) The route: markers 1-3 joined in order by lines of 0xFFFFE000,
        // clipped to the view (0x443ea0).
        {
            hud().clip(offX_ + float(b.x) * scale_, offY_ + float(b.y) * scale_, float(w) * scale_, float(h) * scale_);
            bool first = true;
            float px = 0, py = 0;
            const Color c = argbColor(0xFFFFE000);
            for (const MapMarker& mk : mapMarkers_) {
                if (!route(mk.marker)) continue;
                const float x = float(mk.x - sx + b.x), y = float(mk.y - sy + b.y);
                if (!first)
                    hud().line(offX_ + (px + 0.5f) * scale_, offY_ + (py + 0.5f) * scale_, offX_ + (x + 0.5f) * scale_, offY_ + (y + 0.5f) * scale_,
                               std::max(1.f, scale_), c);
                first = false;
                px = x;
                py = y;
            }
            hud().unclip();
        }
        // (2) Every other marker: frame marker-1 of its team's icon (0 green,
        // 1 blue, 2 red, 3 yellow) at (x-4, y-4) when inside the view.
        for (const MapMarker& mk : mapMarkers_) {
            if (route(mk.marker) || !visible(sx, sy, sx + w - 8, sy + h - 8, mk.x - 4, mk.y - 4)) continue;
            static const char* const kTeamIcon[4] = {"grn_icon.pcx", "blu_icon.pcx", "red_icon.pcx", "yel_icon.pcx"};
            if (mk.team < 0 || mk.team > 3) continue;
            icon(kTeamIcon[mk.team], mk.marker - 1, b.x - sx + mk.x - 4, b.y - sy + mk.y - 4);
        }
        // (3) Markers 1-3: the running number ("%d", LCD10S, left-aligned) at
        // (x+4, y-5), then frame marker-1 of the yellow icon at (x-4, y-4).
        int n = 0;
        for (const MapMarker& mk : mapMarkers_) {
            if (!route(mk.marker)) continue;
            ++n;
            if (visible(sx, sy, sx + w - 16, sy + h - 4, mk.x - 4, mk.y - 4))
                text("LCD10S", float(b.x - sx + mk.x + 4), float(b.y - sy + mk.y - 5), std::to_string(n), mnu_.fonts["LCD10S"].fg[0]);
            if (visible(sx, sy, sx + w - 4, sy + h - 4, mk.x - 4, mk.y - 4))
                icon("yel_icon.pcx", mk.marker - 1, b.x - sx + mk.x - 4, b.y - sy + mk.y - 4);
        }
    }
}

// ---------------------------------------------------------------- refreshes

void FrontendDf2::refreshSoldier() {
    for (int i = 0; i < 5; ++i) {
        char n[16];
        std::snprintf(n, sizeof n, "but_cs_%d", i);
        if (D2Button* b = btn(n)) {
            b->text = soldiers_[size_t(i)].empty() ? menuText("NEW_OPERATIVE", "NEW OPERATIVE") : soldiers_[size_t(i)].name();
            b->selected = i == current_;
        }
    }
    const Soldier& s = soldier();
    auto set = [&](const char* name, const std::string& t) { if (D2Button* b = btn(name)) b->text = t; };
    set("but_cs_ontxt", menuText("OPERATIVE_NAME", "Operative Name:") + " " + s.name());
    set("but_cs_cktxt", menuText("CAMPAIGN_KILLS", "Campaign Kills:") + " " + std::to_string(s.get(0x29c)));
    set("but_cs_cmtxt", menuText("COMPLETED_MISSIONS", "Completed Missions:") + " " + std::to_string(s.get(0x2a4)));
    set("but_cs_mktxt", menuText("MULTIPLAYER_KILLS", "Multiplayer Kills:") + " " + std::to_string(s.get(0x280)));
    set("but_cs_mdtxt", menuText("MULTIPLAYER_DEATHS", "Multiplayer Deaths:") + " " + std::to_string(s.get(0x288)));
    set("but_cs_mwtxt", menuText("TEAM_GAMES_WON", "Team Games Won:") + " " + std::to_string(s.get(0x294)));
    char img[24];
    std::snprintf(img, sizeof img, "IMAGEFACE%02d", std::clamp(s.get(0x278), 0, 14) + 1);
    if (D2Button* b = btn("but_cs_fcimg")) b->image = img;
    std::snprintf(img, sizeof img, "IMAGEFLAG%02d", std::clamp(s.get(0x27c), 0, 28) + 1);
    if (D2Button* b = btn("but_cs_apimg")) b->image = img;
}

// GOG Df2.exe 0x434670: a region is available when one of its missions is
// completed or unlocked; total and completed missions.
FrontendDf2::RegionStats FrontendDf2::regionStats(int r) {
    RegionStats st;
    for (int i = 0; i < 28; ++i) {
        if (kMissions[i].region != r) continue;
        ++st.total;
        if (soldier().get(0x30 + 4 * size_t(i))) ++st.complete;
        if (missionOpen(i)) st.avail = true;
    }
    return st;
}

// 0x434a90 / 0x434a20's search: the first available region of campaign c
// with missions left, else -1.
int FrontendDf2::nextRegion(int c) {
    for (int r = 0; r < 5; ++r) {
        const RegionStats st = regionStats(r);
        if (st.avail && (r < 3 ? 1 : 2) == c && st.total > st.complete) return r;
    }
    return -1;
}

// GOG Df2.exe 0x434a20: the timer and the blink region cleared, then the
// next region with missions left blinks, the timer at 30 (written in that
// order, so for campaign 2 the shared word ends up as that region).
void FrontendDf2::blinkNext(int c) {
    campTimer() = 0;
    campBlink(c) = -1;
    if (const int r = nextRegion(c); r >= 0) { campTimer() = 30; campBlink(c) = r; }
}

// GOG Df2.exe 0x434440, the page's init: the campaign clamped to 1..2
// (0x434670); with no region shown yet the first one with missions left
// (0x434a90, none: still none); a shown region that is complete makes the
// next such region blink (0x434a20: [0x4c3f84] = 30); a completed selected
// mission is deselected (0x4344cb); then the buttons (0x434800).
void FrontendDf2::campaignInit() {
    if (soldier().get(0x28) < 1 || soldier().get(0x28) > 2) soldier().set(0x28, 1);
    const int c = soldier().get(0x28);
    if (campRegion(c) == -1) {
        briefStarted_ = false;   // [0x4f39d0]
        if (const int r = nextRegion(c); r >= 0) campRegion(c) = r;
    } else if (campRegion(c) >= 0 && campRegion(c) < 5) {
        const RegionStats st = regionStats(campRegion(c));
        if (st.total == st.complete) blinkNext(c);
    }
    if (missionSel_ >= 0 && missionSel_ < 28 && soldier().get(0x30 + 4 * size_t(missionSel_))) missionSel_ = -1;
    refreshCampaign();
}

// The page's tick (GOG Df2.exe 0x43457b): the timer counts down; at 0 the
// blink region's button flips its selected flag and the timer restarts at 30.
void FrontendDf2::campaignTick() {
    if (campTimer() > 0) { --campTimer(); return; }
    const int r = campBlink(soldier().get(0x28) == 2 ? 2 : 1);
    if (r < 0 || r >= 5) return;
    static const char* const kRegionBtn[5] = {"but_ca_anbtn", "but_ca_ugbtn", "but_ca_gabtn", "but_ca_chbtn", "but_ca_kabtn"};
    if (D2Button* b = btn(kRegionBtn[r])) b->selected = !b->selected;
    campTimer() = 30;
}

void FrontendDf2::refreshCampaign() {
    campaign_ = std::clamp(soldier().get(0x28), 1, 2);
    for (int c = 1; c <= 2; ++c) {
        if (D2Button* b = btn(c == 1 ? "but_ca_c1btn" : "but_ca_c2btn")) {
            const std::string* t = briefText_.find("TITLE", c == 1 ? "C03" : "C05");
            b->text = t ? *t : (c == 1 ? "C03" : "C05");
            b->selected = c == campaign_;
        }
    }
    const char* regionBtn[5] = {"but_ca_anbtn", "but_ca_ugbtn", "but_ca_gabtn", "but_ca_chbtn", "but_ca_kabtn"};
    const int region = campRegion(campaign_);
    // GOG Df2.exe 0x434670: a region's button picture is its "B" one when all
    // its missions are completed.
    for (int r = 0; r < 5; ++r) {
        const RegionStats st = regionStats(r);
        if (D2Button* b = btn(regionBtn[r])) b->image = std::string(kRegionButton[r]) + (st.total == st.complete ? "B" : "");
    }
    // GOG Df2.exe 0x434800 (CD 0x42fa30): every region and mission button
    // starts at drawstate 0, unselected. A region of this campaign is selected
    // when it is the one shown (no region shown: no picture, no dots) and
    // drawn only when available; a mission dot is drawn only when available
    // and in this campaign's shown region. Drawstate 0 is neither drawn nor
    // clickable.
    for (int r = 0; r < 5; ++r) {
        const bool mine = (r < 3 ? 1 : 2) == campaign_;
        if (D2Button* b = btn(regionBtn[r])) {
            b->drawstate = mine && regionStats(r).avail ? 1 : 0;
            b->selected = mine && r == region;
            layout(*b);
        }
    }
    for (int i = 0; i < 28; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "but_ca_%c%c%c%c", kMissions[i].bms[1], kMissions[i].bms[2], kMissions[i].bms[4], kMissions[i].bms[5]);
        if (D2Button* b = btn(name)) {
            const bool shown = missionOpen(i) && kMissions[i].campaign == campaign_ && kMissions[i].region == region;
            b->drawstate = shown ? 1 : 0;
            b->image = soldier().get(0x30 + 4 * size_t(i)) ? "IMAGEBTN37" : "IMAGEBTN36";
            b->selected = shown && i == missionSel_;
            layout(*b);
        }
    }
    if (D2Button* b = btn("but_ca_mbtxt")) b->drawstate = missionSel_ >= 0 ? 1 : 4;
}

void FrontendDf2::refreshQuick() {
    // The D06*.BMS missions in the archives (0x43c190).
    quick_.clear();
    for (const auto& n : vfs_.list(".BMS"))
        if (n.size() >= 3 && upper(n).compare(0, 3, "D06") == 0) quick_.push_back(upper(n.substr(0, n.size() - 4)));
    std::sort(quick_.begin(), quick_.end());
    if (D2Button* b = btn("but_qm_mbtxt")) b->drawstate = quickSel_ >= 0 ? 1 : 4;
    if (D2Button* b = btn("but_qm_map")) {
        b->image = "IMAGEMAP1";
        if (quickSel_ >= 0 && quickSel_ < int(quick_.size()) && vfs_.exists(quick_[size_t(quickSel_)] + ".TGA")) {
            mnu_.images["QM_PICTURE"] = {quick_[size_t(quickSel_)] + ".TGA", 0, 1};
            b->image = "QM_PICTURE";
        }
        layout(*b);
    }
    if (D2Button* b = btn("but_qm_vsl")) { b->p1 = std::max(1, int(quick_.size()) - 8); b->p2 = quickTop_; }
}

void FrontendDf2::loadBriefing() {
    // GOG Df2.exe 0x43d000: but_mb_mbrct's text is the mission's MBBRIEF
    // (keyed by its first six letters); but_mb_brvsl's range is the number
    // of lines it breaks into (0x41bf70 at the text's width user1 - x - 10)
    // less the 25 the box shows, at least 0, and its value 0.
    if (D2Button* t = btn("but_mb_mbrct")) {
        const std::string* brief = briefText_.find("MBBRIEF", bms_.substr(0, std::min<size_t>(6, bms_.size())));
        t->text = brief ? *brief : std::string();
        if (D2Button* sl = btn("but_mb_brvsl")) {
            sl->p1 = std::max(0, breakLines(t->font, t->user1 - t->x - 10, t->text) - 25);
            sl->p2 = 0;
        }
    }
    briefMap_ = {};
    mapMarkers_.clear();
    mapCentreX_ = mapCentreY_ = 0x200;
    std::vector<uint8_t> d;
    Mission m;
    if (vfs_.read(bms_ + ".BMS", d)) {
        std::string err;
        if (parseMission(d, m, &err)) {
            // GOG Df2.exe 0x43ce90: the picture is the header's terrain name
            // (+0x44, read only from a version-15 header, 0x4044a0) with its
            // extension replaced by .JPG (e.g. DFS1.TRN -> DFS1.JPG, 256²),
            // repeated 4 x 4 (0x46c760(img, 4, 3) copies each row 4 times
            // across and the whole picture 4 times down): a tiling, 4 m a
            // pixel like the markers.
            std::string jpg = m.version == 15 ? m.terrain : std::string();
            if (auto dot = jpg.rfind('.'); dot != std::string::npos) jpg.resize(dot);
            std::vector<uint8_t> raw;
            RgbaImage im;
            if (!jpg.empty() && vfs_.read(upper(jpg) + ".JPG", raw) && decodeRgbaImageDf2(raw, im) && im.width > 0 && im.height > 0) {
                const size_t row = size_t(im.width) * 4;
                std::vector<uint8_t> tiled(row * 4 * size_t(im.height) * 4);
                for (int ty = 0; ty < 4; ++ty)
                    for (int y = 0; y < im.height; ++y)
                        for (int tx = 0; tx < 4; ++tx)
                            std::memcpy(&tiled[(size_t(ty * im.height + y) * 4 + size_t(tx)) * row], &im.rgba[size_t(y) * row], row);
                briefMap_ = hud().makeImage(im.width * 4, im.height * 4, tiled);
            }
            // GOG Df2.exe 0x4045e0 (CD 0x404390), BMS version >= 15 only: the
            // objects of the four groups in file order whose marker byte
            // (+0x4C) is set and whose flags byte (+0x0C) & 0x50 is clear, up
            // to 50, as {marker, x, y, team byte +0x43}. Points go to the 1024²
            // picture: px = 0x200 + (x16.16 >> 18), py = 0x200 - (y >> 18).
            auto byteAt = [](const MissionObject& o, int off) {
                return int8_t(uint8_t(uint32_t(o.raw[off / 4]) >> (8 * (off % 4))));
            };
            int32_t cx = 0, cy = 0;
            if (m.version >= 15) {
                for (const auto& o : m.objects) {
                    const int marker = byteAt(o, 0x4C);
                    const bool hidden = (uint8_t(byteAt(o, 0x0C)) & 0x50) != 0;
                    if (marker != 0 && !hidden && mapMarkers_.size() < 50)
                        mapMarkers_.push_back({marker, 0x200 + (o.raw[4] >> 18), 0x200 - (o.raw[5] >> 18), byteAt(o, 0x43)});
                    // Centre: the last group-2 object of type 0x1796 = 6038,
                    // "Briefing Map Center" (0x404796), unless it is a marker
                    // with flags & 0x50; without one, (0, 0) -> (512, 512).
                    if (o.group == 2 && o.type == 0x1796 && (marker == 0 || !hidden)) { cx = o.raw[4]; cy = o.raw[5]; }
                }
            }
            mapCentreX_ = 0x200 + (cx >> 18);
            mapCentreY_ = 0x200 - (cy >> 18);
        }
    }
    // 0x43ce90: with the picture the sliders' range is picture - view
    // (1024 - 279 / 1024 - 323) and the scroll starts on the centre
    // (0x43cdf0, not eased); without it the map shows nothing and both
    // sliders' range and value are 0.
    D2Button* v = btn("but_mb_mpimg");
    D2Button* hs = btn("but_mb_mphsl");
    D2Button* vs = btn("but_mb_mpvsl");
    if (v && hs && vs) {
        if (briefMap_.texture) {
            hs->p1 = v->x - v->user1 + briefMap_.width;
            vs->p1 = v->y - v->user2 + briefMap_.height;
            mapCentre(mapCentreX_, mapCentreY_, false);
        } else {
            hs->p1 = hs->p2 = vs->p1 = vs->p2 = 0;
        }
    }
    // Page init (0x4376f0): the ramp cleared, then 0x43778f -> 0x4384f0.
    briefRamp_ = briefRampMode_ = 0;
    startVoice();
}

void FrontendDf2::refreshGear() {
    struct Panel { const char* pic; const char* txt; const char* rnd; const char* rng; size_t slot; };
    const Panel panels[4] = {{"but_cg_pwpic", "but_cg_pwtxt", "but_cg_pwrnd", "but_cg_pwrng", 0x2bc},
                             {"but_cg_swpic", "but_cg_swtxt", "but_cg_swrnd", "but_cg_swrng", 0x2c0},
                             {"but_cg_sapic", "but_cg_satxt", "but_cg_sarnd", "but_cg_sarng", 0x2b8},
                             {"but_cg_agpic", "but_cg_agtxt", "but_cg_agrnd", "but_cg_agrng", 0x2c4}};
    for (const Panel& p : panels) {
        const int type = soldier().get(p.slot);
        const GearInfo* g = gearInfo(type);
        if (D2Button* b = btn(p.pic)) { b->image = g ? g->image : ""; layout(*b); }
        char key[24];
        std::snprintf(key, sizeof key, "STRWEAP2L_%02d", type);
        const std::string* name = gameText_.find("WeaponNames2L", key);
        if (D2Button* b = btn(p.txt)) b->text = name ? *name : "";
        if (D2Button* b = btn(p.rnd)) {
            b->text.clear();
            if (g && g->rounds > 0)
                b->text = menuText("MAGAZINE", "Magazine:") + " " + std::to_string(g->rounds) + " " +
                          (g->rounds == 1 ? menuText("ROUND", "Round") : menuText("ROUNDS", "Rounds"));
        }
        if (D2Button* b = btn(p.rng)) {
            b->text.clear();
            if (g && g->range > 0) b->text = menuText("RANGE", "Range:") + " " + std::to_string(g->range) + " " + menuText("METERS", "Meters");
        }
    }
    if (D2Button* b = btn("but_cg_gntxt"))
        b->text = soldier().get(0x2c8) == 37 ? menuText("DELAYED_FRAGMENTATION", "Delayed Fragmentation") : menuText("FRAGMENTATION", "Fragmentation");
}

// 0x4300a0 arrows: cycle 0..38 with wrap, skipping other categories.
void FrontendDf2::gearStep(size_t slot, int category, int dir, int altCategory) {
    int t = soldier().get(slot);
    for (int k = 0; k < 39; ++k) {
        t = (t + dir + 39) % 39;
        const GearInfo* g = gearInfo(t);
        if (g && (g->category == category || g->category == altCategory)) break;
    }
    soldier().set(slot, t);
    refreshGear();
}

void FrontendDf2::refreshOptions() {
    auto set = [&](const char* name, const std::string& t) { if (D2Button* b = btn(name)) b->text = t; };
    if (screen_ == Screen::Options1) {
        if (D2Button* b = btn("but_opt1_mssl")) { b->p1 = 512; b->p2 = soldier().get(0x5b0); b->p3 = 260; }
        // GOG Df2.exe 0x43e683 / 0x43ec70: SCOPE SENSITIVITY 0..0x180, the
        // soldier's +0x5d8.
        if (D2Button* b = btn("but_opt1_sshsl")) { b->p1 = 0x180; b->p2 = soldier().get(0x5d8); }
        set("but_opt1_fmtxt", soldier().get(0x5b4) ? menuText("YES", "YES") : menuText("NO", "NO"));
        set("but_opt1_dvtxt", menuText("FULL_SCREEN_(HARDWARE)", "OpenGL"));
        char res[32];
        int dw = 0, dh = 0;
        if (win_) SDL_GL_GetDrawableSize(win_, &dw, &dh);
        std::snprintf(res, sizeof res, "%dx%d", dw, dh);
        set("but_opt1_rstxt", res);
        set("but_opt1_tutxt", cfg_.get("turbo") ? menuText("YES", "YES") : menuText("NO", "NO"));
        if (D2Button* b = btn("but_opt1_sfsl")) { b->p1 = 256; b->p2 = soldier().get(0x5b8); b->p3 = 260; }
        if (D2Button* b = btn("but_opt1_mvsl")) { b->p1 = 256; b->p2 = cfg_.get("music_volume"); b->p3 = 260; }
        if (D2Button* b = btn("but_opt1_dvsl")) { b->p1 = 256; b->p2 = soldier().get(0x5c0); b->p3 = 260; }
    } else {
        const int at = cfg_.get("attributes");
        set("but_opt3_aitxt", (at & 0x10) ? menuText("HARD", "HARD") : menuText("NORMAL", "NORMAL"));
        set("but_opt3_smtxt", (at & 0x20) ? menuText("OFF", "OFF") : menuText("ON", "ON"));
        set("but_opt3_trtxt", (at & 0x80) ? menuText("OFF", "OFF") : menuText("ON", "ON"));
        set("but_opt3_fftxt", (at & 0x100) ? menuText("OFF", "OFF") : menuText("ON", "ON"));
        const int gps = std::clamp(cfg_.get("gpsicon"), 0, 2);
        set("but_opt3_gptxt", gps == 0 ? menuText("SHOW_FRIENDLY", "Friendly") : gps == 2 ? menuText("SHOW_EVERYTHING", "Everything")
                                                                               : menuText("DEFAULT_COLD", "Default"));
        const int w = std::clamp(cfg_.get("sp_wind"), 0, 2);
        set("but_opt3_wntxt", w == 1 ? menuText("NONE", "None") : w == 2 ? menuText("RANDOM", "Random") : menuText("DEFAULT_COLD", "Default"));
    }
}

void FrontendDf2::startMission() { next_ = Screen::Mission; }

// ---------------------------------------------------------------- events

void FrontendDf2::event(D2Button& b, int ev) {
    const std::string& cb = b.callback;
    if (ev == 1 && screen_ == Screen::Main) {
        // Hover: the help text MM_xxHELP teletyped in the help box.
        static const std::pair<const char*, const char*> kHelp[] = {
            {"mm_QuickMission", "MM_QMHELP"}, {"mm_Campaign", "MM_CAHELP"}, {"mm_Novaworld", "MM_NWHELP"}, {"mm_Multiplayer", "MM_MPHELP"},
            {"mm_Options", "MM_OPHELP"}, {"mm_NewSoldier", "MM_CSHELP"}, {"mm_Credits", "MM_CRHELP"}, {"mm_Update", "MM_UPHELP"}};
        // 0x43ab4a: 0x43a700 keeps the button ([0x4c5998]) for the connector
        // (0x43a780) and teletypes its help.
        for (const auto& [n, k] : kHelp)
            if (b.callback == n) { helpText_ = menuText(k, ""); helpShown_ = 0; mmHover_ = b.name; }
    }
    if (ev == 2 && screen_ == Screen::Main) {
        // 0x43ab5c: leaving the button, 0x43a700(0, 0) drops the connector
        // and empties the help box.
        static const char* const kHelpButtons[] = {"mm_QuickMission", "mm_Campaign", "mm_Novaworld", "mm_Multiplayer",
                                                   "mm_Options", "mm_NewSoldier", "mm_Credits", "mm_Update"};
        for (const char* n : kHelpButtons)
            if (b.callback == n) { helpText_.clear(); helpShown_ = 0; mmHover_.clear(); }
    }
    // mb_Map (GOG Df2.exe 0x43d250), event 3: a press on the map heads the
    // scroll for the picture point under the mouse (0x43cdf0, eased).
    if (ev == 3 && cb == "mb_Map")
        mapCentre(sliderValue("but_mb_mphsl") - b.x + mouseX_, sliderValue("but_mb_mpvsl") - b.y + mouseY_, true);
    if (ev != 4) return;
    auto exitGame = [&]() { next_ = Screen::Quit; };
    if (cb == "mm_Quit" || cb == "cs_ExitGame" || cb == "ca_ExitGame" || cb == "qm_ExitGame" || cb == "mb_ExitGame" ||
        cb == "cg_ExitGame" || cb == "opt_ExitGame" || cb == "cr_ExitGame") exitGame();
    else if (cb == "mm_QuickMission") next_ = Screen::Quick;
    else if (cb == "mm_Campaign") next_ = Screen::Campaign;
    else if (cb == "mm_Options") next_ = Screen::Options1;
    else if (cb == "mm_NewSoldier") next_ = Screen::Soldier;
    else if (cb == "mm_Credits") next_ = Screen::Credits;
    // Choose Soldier.
    else if (cb == "cs_GetName") {
        current_ = b.name.back() - '0';
        if (soldier().empty()) {
            editing_ = b.name;
            editBefore_ = b.text;
            editText_.clear();
            SDL_StartTextInput();
        }
        refreshSoldier();
    } else if (cb == "cs_EditName") {
        char n[16];
        std::snprintf(n, sizeof n, "but_cs_%d", current_);
        editing_ = n;
        editText_ = soldier().name();
        editBefore_ = editText_;
        SDL_StartTextInput();
    } else if (cb == "cs_DeleteSoldier") {
        defaultSoldier(soldier());
        refreshSoldier();
    } else if (cb == "cs_PrevFace" || cb == "cs_NextFace") {
        soldier().set(0x278, (soldier().get(0x278) + (cb == "cs_NextFace" ? 1 : 14)) % 15);
        refreshSoldier();
    } else if (cb == "cs_PrevFlag" || cb == "cs_NextFlag") {
        soldier().set(0x27c, (soldier().get(0x27c) + (cb == "cs_NextFlag" ? 1 : 28)) % 29);
        refreshSoldier();
    } else if (cb == "cs_MainMenu") {
        if (!soldier().empty()) { saveSoldiers(); next_ = Screen::Main; }
    }
    // Campaign.
    else if (cb == "ca_ChooseCampaign") {
        // GOG Df2.exe 0x434d10: another campaign only; the selection cleared,
        // its region (none yet: the first with missions left, 0x434a90; a
        // complete one: the next blinks, 0x434a20).
        if (b.user2 != soldier().get(0x28)) {
            missionSel_ = -1;
            soldier().set(0x28, b.user2);
            const int c = b.user2;
            if (c >= 1 && c <= 2) {
                if (campRegion(c) == -1) {
                    if (const int r = nextRegion(c); r >= 0) campRegion(c) = r;
                } else if (campRegion(c) >= 0 && campRegion(c) < 5) {
                    const RegionStats st = regionStats(campRegion(c));
                    if (st.total == st.complete) blinkNext(c);
                }
            }
            refreshCampaign();
        }
    } else if (cb == "ca_ChooseRegion") {
        // GOG Df2.exe 0x434c80: another region only; the selection cleared;
        // choosing the blinking region stops the blink, a complete one makes
        // the next blink (0x434a20).
        const int c = soldier().get(0x28) == 2 ? 2 : 1;
        if (campRegion(c) != b.user2) {
            missionSel_ = -1;
            campRegion(c) = b.user2;
            if (campRegion(c) == campBlink(c)) {
                campBlink(c) = -1;
                campTimer() = 0;
            } else if (campRegion(c) >= 0 && campRegion(c) < 5) {
                const RegionStats st = regionStats(campRegion(c));
                if (st.total == st.complete) blinkNext(c);
            }
            refreshCampaign();
        }
    } else if (cb == "ca_ChooseMission") {
        // GOG Df2.exe 0x434c50: another mission only ([0x4f39d0] = 0: its
        // voices play again).
        if (b.user2 != missionSel_) {
            missionSel_ = b.user2;
            briefStarted_ = false;
            refreshCampaign();
        }
    } else if (cb == "ca_MissionBriefing" && missionSel_ >= 0) {
        bms_ = kMissions[missionSel_].bms;
        fromCampaign_ = true;
        returnTo_ = Screen::Campaign;
        next_ = Screen::Briefing;
    } else if (cb == "ca_Back" || cb == "qm_Back" || cb == "opt_Accept" || cb == "cr_Back") {
        if (cb == "opt_Accept") { saveSoldiers(); cfg_.save(saveDir_ / "df2.cfg"); haveKeySnapshot_ = false; }
        next_ = Screen::Main;
    }
    // Quick missions.
    else if (cb == "qm_SelMis") {
        const int i = quickTop_ + b.user1;
        if (i < int(quick_.size())) quickSel_ = i, briefStarted_ = false;   // 0x43c4f6
        refreshQuick();
    } else if (cb == "qm_ScrollUp") { quickTop_ = std::max(0, quickTop_ - 1); refreshQuick(); }
    else if (cb == "qm_ScrollDown") { quickTop_ = std::clamp(quickTop_ + 1, 0, std::max(0, int(quick_.size()) - 8)); refreshQuick(); }
    else if (cb == "qm_MissionBriefing" && quickSel_ >= 0 && quickSel_ < int(quick_.size())) {
        bms_ = quick_[size_t(quickSel_)];
        fromCampaign_ = false;
        returnTo_ = Screen::Quick;
        next_ = Screen::Briefing;
    }
    // Briefing.
    else if (cb == "mb_StartMission" || cb == "cg_StartMission") { stopVoice(); startMission(); }
    else if (cb == "mb_ChangeGear") {
        stopVoice();
        // Entering from the briefing: the mission's defaults (header +0x94..+0x97,
        // non-zero ones: sidearm, primary, secondary, auxiliary).
        const uint32_t g = header(bms_, 0x94);
        const size_t slots[4] = {0x2b8, 0x2bc, 0x2c0, 0x2c4};
        for (int k = 0; k < 4; ++k)
            if (const int v = int((g >> (8 * k)) & 0xFF)) soldier().set(slots[k], v);
        next_ = Screen::Gear;
    } else if (cb == "mb_Back") { stopVoice(); next_ = returnTo_; }
    else if (cb == "mb_Replay") {
        // PLAY_BRIEFING / STOP_BRIEFING (0x438640): playing, it stops;
        // otherwise it starts again from the first voice.
        if (briefVoice_) stopVoice();
        else {
            briefStarted_ = false;
            stopVoice();
            startVoice();
        }
    }
    // mb_BriefUp / mb_BriefDown (GOG Df2.exe 0x43d1c0 / 0x43d1f0): the
    // briefing slider's value -1 (not below 0) / +1 (not above its range).
    else if (cb == "mb_BriefUp") { if (D2Button* sl = btn("but_mb_brvsl")) sl->p2 = std::max(0, sl->p2 - 1); }
    else if (cb == "mb_BriefDown") { if (D2Button* sl = btn("but_mb_brvsl")) sl->p2 = std::min(sl->p2 + 1, sl->p1); }
    // mb_MapUp / Down / Left / Right (GOG Df2.exe 0x43d650 / 0x43d610 /
    // 0x43d690 / 0x43d6d0): the slider's value -+10 within its range, the
    // target with it; mb_MapCenter (0x43d930) heads back for the mission's
    // centre (eased).
    else if (cb == "mb_MapUp" || cb == "mb_MapDown" || cb == "mb_MapLeft" || cb == "mb_MapRight") {
        const bool vert = cb == "mb_MapUp" || cb == "mb_MapDown";
        if (D2Button* s = btn(vert ? "but_mb_mpvsl" : "but_mb_mphsl")) {
            s->p2 = std::clamp(s->p2 + (cb == "mb_MapDown" || cb == "mb_MapRight" ? 10 : -10), 0, std::max(0, s->p1));
            (vert ? mapTargetY_ : mapTargetX_) = s->p2;
        }
    } else if (cb == "mb_MapCenter") mapCentre(mapCentreX_, mapCentreY_, true);
    // Change Gear.
    else if (cb == "cg_PWLeft" || cb == "cg_PWRight") gearStep(0x2bc, 3, cb == "cg_PWRight" ? 1 : -1);
    else if (cb == "cg_SWLeft" || cb == "cg_SWRight") gearStep(0x2c0, 4, cb == "cg_SWRight" ? 1 : -1);
    else if (cb == "cg_SALeft" || cb == "cg_SARight") gearStep(0x2b8, 2, cb == "cg_SARight" ? 1 : -1);
    else if (cb == "cg_AGLeft" || cb == "cg_AGRight") gearStep(0x2c4, 4, cb == "cg_AGRight" ? 1 : -1, 5);
    else if (cb == "cg_GNLeft" || cb == "cg_GNRight") { soldier().set(0x2c8, soldier().get(0x2c8) == 26 ? 37 : 26); refreshGear(); }
    else if (cb == "cg_DefaultGear") {
        const uint32_t g = header(bms_, 0x94);
        soldier().set(0x2b8, 5); soldier().set(0x2bc, 10); soldier().set(0x2c0, 21); soldier().set(0x2c4, 1); soldier().set(0x2c8, 26);
        const size_t slots[4] = {0x2b8, 0x2bc, 0x2c0, 0x2c4};
        for (int k = 0; k < 4; ++k)
            if (const int v = int((g >> (8 * k)) & 0xFF)) soldier().set(slots[k], v);
        refreshGear();
    } else if (cb == "cg_Back") next_ = Screen::Briefing;
    // Options.
    else if (cb == "opt_NextPage" || cb == "opt_PrevPage") {
        // 0x4396a0 / 0x4396e0: four pages (0x4b3ae0), wrapping.
        static const Screen kPages[4] = {Screen::Options1, Screen::Options2, Screen::Options3, Screen::Options4};
        int p = 0;
        while (p < 3 && kPages[p] != screen_) ++p;
        next_ = kPages[(p + (cb == "opt_NextPage" ? 1 : 3)) % 4];
    }
    // Options2.
    else if (cb == "opt2_ScrollUp" || cb == "opt2_ScrollDn") {
        if (D2Button* v = btn("but_opt2_vsl")) {
            if (cb == "opt2_ScrollUp" && v->p2 > 0) --v->p2;
            else if (cb == "opt2_ScrollDn" && v->p2 < v->p1 - 1) ++v->p2;
        }
        refreshKeys();
    } else if (cb == "opt2_KeyMap") {
        const int row = (btn("but_opt2_vsl") ? btn("but_opt2_vsl")->p2 : 0) + (b.name.back() - '0');
        if (keyDelete_) {
            for (size_t o = 0; o < 12; o += 4) soldier().set(0x2e0 + 12 * size_t(row) + o, 0);
            keyDelete_ = false;
            remapMessage("REMAP_DELETED");
            refreshKeys();
        } else {
            keyCapture_ = true;
            keyRow_ = row;
            keyQueue_.clear();
            remapMessage("REMAP_PRESS");
        }
    } else if (cb == "opt2_DeleteKey") { remapMessage("REMAP_SELECT"); keyDelete_ = true; }
    else if (cb == "opt2_Defaults") {
        for (size_t i = 0; i < kDf2KeyRows; ++i) {
            soldier().set(0x2e0 + 12 * i, int32_t(kDf2Keys[i].vk));
            soldier().set(0x2e4 + 12 * i, int32_t(kDf2Keys[i].mouse));
            soldier().set(0x2e8 + 12 * i, 0);
        }
        remapMessage("REMAP_DEFAULT");
        refreshKeys();
    } else if (cb == "opt2_Revert") {
        std::memcpy(&soldier().d[0x2e0], keySnapshot_.data(), keySnapshot_.size());
        remapMessage("REMAP_REVERT");
        refreshKeys();
    }
    else if (cb == "opt1_FMChange") { soldier().set(0x5b4, !soldier().get(0x5b4)); refreshOptions(); }
    else if (cb == "opt1_TUChange") { cfg_.set("turbo", !cfg_.get("turbo")); refreshOptions(); }
    else if (cb == "opt1_MSLeft" || cb == "opt1_MSRight" || cb == "opt1_FXLeft" || cb == "opt1_FXRight" || cb == "opt1_MVLeft" ||
             cb == "opt1_MVRight" || cb == "opt1_DVLeft" || cb == "opt1_DVRight" || cb == "opt1_SSLeft" || cb == "opt1_SSRight") {
        // 0x4399c0.. / 0x439d90.. / 0x439de0.. / 0x439e50..; GOG Df2.exe
        // opt1_SSLeft / opt1_SSRight 0x43ef70 / 0x43ef90 (but_opt1_sshsl):
        // the slider's value +-1 within 0..range, then the same write as
        // dragging it.
        const std::string k = cb.substr(5, 2);
        const char* name = k == "MS" ? "but_opt1_mssl" : k == "FX" ? "but_opt1_sfsl" : k == "MV" ? "but_opt1_mvsl"
                         : k == "SS" ? "but_opt1_sshsl" : "but_opt1_dvsl";
        if (D2Button* sl = btn(name)) {
            const bool right = cb.find("Right") != std::string::npos;
            sl->p2 = std::clamp(sl->p2 + (right ? 1 : -1), 0, sl->p1);
            sliderChanged(*sl);
        }
        refreshOptions();
    }
    else if (cb.rfind("opt3_", 0) == 0) {
        int at = cfg_.get("attributes");
        const std::string k = cb.substr(5, 2);
        if (k == "AI") at ^= 0x10;
        else if (k == "SM") at ^= 0x20;
        else if (k == "TR") at ^= 0x80;
        else if (k == "FF") at ^= 0x100;
        else if (k == "GP") cfg_.set("gpsicon", (std::clamp(cfg_.get("gpsicon"), 0, 2) + (cb.find("Right") != std::string::npos ? 1 : 2)) % 3);
        else if (k == "WN") cfg_.set("sp_wind", (std::clamp(cfg_.get("sp_wind"), 0, 2) + (cb.find("Right") != std::string::npos ? 1 : 2)) % 3);
        cfg_.set("attributes", at);
        refreshOptions();
    }
}

void FrontendDf2::key(const SDL_KeyboardEvent& k) {
    const SDL_Keycode sym = k.keysym.sym;
    if (!editing_.empty()) {
        // 0x42eb80: Backspace removes, Enter commits (cb 8), Esc restores (cb 9).
        if (sym == SDLK_BACKSPACE || sym == SDLK_DELETE) { if (!editText_.empty()) editText_.pop_back(); }
        else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
            if (!editText_.empty()) soldier().setName(editText_);
            editing_.clear();
            SDL_StopTextInput();
            refreshSoldier();
        } else if (sym == SDLK_ESCAPE) {
            editing_.clear();
            SDL_StopTextInput();
            refreshSoldier();
        }
        return;
    }
    if (busy()) return;
    // Options2 capture: the Options tick takes the key-down before the menu sees it.
    if (keyCapture_ && (screen_ == Screen::Options1 || screen_ == Screen::Options2 || screen_ == Screen::Options3 || screen_ == Screen::Options4)) {
        if (!k.repeat) {
            const uint32_t vk = Controls::toVirtualKey(k.keysym.scancode);
            keyQueue_.push_back(vk ? vk : 0xFF);   // a key with no VK here: reserved
        }
        return;
    }
    // GOG Df2.exe 0x4333c0 (0x4335e0; CD 0x42e832): the first button
    // (z-order) whose hotkey1/2 (+0x84 / +0x88) is the key (0x432c50), else
    // the first whose '&' key is the upper-cased character (0x432c00); a
    // hidden or disabled match, or one without a callback, does nothing. Then
    // pushed for 3 frames (+0xa0), btn_slct on channel 3, events 3 and 4. GOG
    // matches the Windows virtual key of WM_KEYDOWN (0x473690): its DF2.MNU
    // has 13 (Enter) on the accept / start buttons and 27 (Escape) on BACK.
    // The CD build's input gave DirectInput scancodes (CD 0x42dea0) and its
    // DF2.MNU has 1 (Escape) on BACK only, so with the CD's DF2.EXE that one
    // is mapped. The character is the key's unshifted symbol (INFERRED: the
    // EXE's key event carries the translated character; letters and digits
    // are the same either way).
    if (D2Menu* m = menu()) {
        D2Button* hit = nullptr;
        int code = int(Controls::toVirtualKey(k.keysym.scancode));
        if (cdExe_) code = k.keysym.scancode == SDL_SCANCODE_ESCAPE ? 1 : 0;   // DIK_ESCAPE
        if (code != 0)
            for (int i : m->buttons) {
                D2Button& b = mnu_.buttons[size_t(i)];
                if (b.hot1 == code || b.hot2 == code) { hit = &b; break; }
            }
        if (!hit && sym > 0 && sym < 128) {
            const int ch = std::toupper(int(sym));
            for (int i : m->buttons) {
                D2Button& b = mnu_.buttons[size_t(i)];
                const size_t a = b.text.find('&');
                if (a != std::string::npos && a + 1 < b.text.size() && std::toupper(static_cast<unsigned char>(b.text[a + 1])) == ch) { hit = &b; break; }
            }
        }
        if (hit && hit->drawstate != 0 && hit->drawstate != 4 && !hit->callback.empty()) {
            sound("btn_slct", 3);
            hit->flash = 3;
            event(*hit, 3);
            event(*hit, 4);
        }
    }
}

// ---------------------------------------------------------------- loop

void FrontendDf2::playIntroMovie() {
    // 0x435830: binkplay.exe from DF2.EXE's folder (FindFirstFile on the
    // bare name: the current folder), else <CD>\DF2SETUP\binkplay.exe;
    // DF2INTRO.BIK the same way. Either missing: no intro. The command
    // line is "binkplay.exe DF2INTRO.BIK" plus " /I2/C" when df2.cfg's
    // video_type is 0, else " /r/D208/U1/C" (0x435993). DF2.EXE starts it
    // (CreateProcess, 0x4359f3), hides its own window first if video_type
    // is 0 (ShowWindow 0, 0x435a15), then polls the process every 500 ms
    // (GetExitCodeProcess, Sleep 0x1f4) pumping its messages (0x46de90);
    // closing DF2 (quit flag 0xdd00ac) stops the wait. When binkplay exits
    // the window is restored (ShowWindow 9) if it was hidden. Skipping is
    // binkplay's own (not traced: RAD's program, packed).
    std::error_code ec;
    auto find = [&](const char* name) {
        std::vector<std::filesystem::path> dirs = {saveDir_};
        for (auto& e : std::filesystem::directory_iterator(o_.gameDir, ec))
            if (upper(e.path().filename().string()) == "DF2SETUP") dirs.push_back(e.path());
        for (const auto& d : dirs)
            for (auto& e : std::filesystem::directory_iterator(d, ec))
                if (upper(e.path().filename().string()) == name) return e.path();
        return std::filesystem::path();
    };
    const std::filesystem::path player = find("BINKPLAY.EXE"), movie = find("DF2INTRO.BIK");
    if (player.empty() || movie.empty()) return;
    if (std::getenv("OPENDF_DEBUGVIDEO"))
        std::fprintf(stderr, "intro: %s %s %s\n", player.string().c_str(), movie.string().c_str(),
                     cfg_.get("video_type") == 0 ? "/I2/C" : "/r/D208/U1/C");
    playBinkVideo(movie);
    // INFERRED: keys and clicks during the movie went to binkplay's
    // window, not to DF2's.
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_KEYDOWN, SDL_MOUSEBUTTONUP);
}

void FrontendDf2::playBinkVideo(const std::filesystem::path& path) {
    // binkplay.exe is RAD's own program (packed) and was not traced; what
    // it does here is OpenDF's: the movie decoded by formats/Bink.h, each
    // frame shown when due by the file's rate / scale, centred with its
    // aspect kept, its sound straight to the output. Skipping: Esc or any
    // other key, or a mouse click, ends it (INFERRED: binkplay's own
    // handling). Closing the window ends it and DF2.
    BinkVideo v;
    if (!v.open(path)) {
        std::fprintf(stderr, "%s: %s\n", path.string().c_str(), v.error().c_str());
        return;
    }
    const bool debug = std::getenv("OPENDF_DEBUGVIDEO");
    if (debug)
        std::fprintf(stderr, "intro: BIK%c %dx%d, %d frames at %d/%d fps, audio %d Hz x %d\n", v.revision(), v.width(), v.height(),
                     v.frameCount(), v.rate(), v.scale(), v.audioRate(), v.audioChannels());
    HudImage tex;
    std::vector<uint8_t> rgba;
    std::vector<int16_t> pcm;
    binkSound_.clear();
    audio_.setDirectStream([this](int16_t* out, int frames) { binkSound_.mix(out, frames); });
    // Testing (--screenshot): a clock that moves only while the player
    // waits, so frame N is always the same picture (and OPENDF_AUDIODUMP
    // the same sound); OPENDF_INTROKEY=N presses a key after N shown
    // frames, OPENDF_INTROCLICK=N clicks.
    const bool fixed = !o_.screenshot.empty();
    const int testKey = fixed && std::getenv("OPENDF_INTROKEY") ? std::atoi(std::getenv("OPENDF_INTROKEY")) : -1;
    const int testClick = fixed && std::getenv("OPENDF_INTROCLICK") ? std::atoi(std::getenv("OPENDF_INTROCLICK")) : -1;
    const Uint64 freq = SDL_GetPerformanceFrequency(), c0 = SDL_GetPerformanceCounter();
    double virt = 0, heard = 0;
    auto now = [&]() { return fixed ? virt : double(SDL_GetPerformanceCounter() - c0) / double(freq); };
    auto hear = [&]() {   // OPENDF_AUDIODUMP: the sound up to the clock
        const double t = now();
        if (t > heard) { audio_.advance(t - heard); heard = t; }
    };
    const double period = double(v.scale()) / double(v.rate());
    bool quit = false, skip = false;
    int shown = 0, late = 0;
    for (int n = 0;; ++n) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (fullscreenEvent(win_, e)) continue;
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN) skip = true;
        }
        if (shown == testKey || shown == testClick) skip = true;
        if (quit || skip) break;
        const BinkVideo::Step st = v.next();
        if (st != BinkVideo::Step::Frame) {
            if (st == BinkVideo::Step::Error) std::fprintf(stderr, "%s: frame %d: %s\n", path.string().c_str(), n, v.error().c_str());
            break;
        }
        // The sound decoded with the frame is queued at once: the file
        // sends it ahead of the pictures, so the queue holds what is due
        // later and the two stay together from the first frame on.
        pcm.clear();
        v.takeAudio(pcm);
        if (v.hasAudio() && !pcm.empty()) binkSound_.push(pcm, v.audioRate(), v.audioChannels());
        // Frame n is due n periods after the first; a late one is still
        // shown (the decoder runs far faster than the movie's rate).
        const double due = n * period;
        if (fixed) virt = std::max(virt, due);
        while (due - now() > 0.002) SDL_Delay(1);
        if (now() - due > period) ++late;
        hear();
        v.toRgba(rgba);
        hud().updateImage(tex, v.width(), v.height(), rgba);
        int dw, dh;
        SDL_GL_GetDrawableSize(win_, &dw, &dh);
        const float sc = std::min(float(dw) / float(v.width()), float(dh) / float(v.height()));
        const float w = float(v.width()) * sc, h = float(v.height()) * sc;
        glViewport(0, 0, dw, dh);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        hud().begin(dw, dh);
        hud().image(tex.texture, (float(dw) - w) * 0.5f, (float(dh) - h) * 0.5f, w, h);
        hud().end();
        ++shown;
        if (fixed && ++frame_ >= o_.frames) {
            std::vector<uint8_t> px(size_t(dw) * size_t(dh) * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            if (FILE* f = std::fopen(o_.screenshot.c_str(), "wb")) {
                std::fprintf(f, "P6 %d %d 255\n", dw, dh);
                for (int y = dh - 1; y >= 0; --y) std::fwrite(&px[size_t(y) * size_t(dw) * 3], 1, size_t(dw) * 3, f);
                std::fclose(f);
            }
            introShot_ = true;
            break;
        }
        SDL_GL_SwapWindow(win_);
    }
    // The movie's end (or a skip) stops its sound with it.
    binkSound_.clear();
    audio_.setDirectStream(nullptr);
    if (tex.texture) glDeleteTextures(1, &tex.texture);
    if (debug) std::fprintf(stderr, "intro: %d frames shown (%d late)%s\n", shown, late, skip ? ", skipped" : quit ? ", quit" : "");
    if (quit) {
        SDL_Event q{};
        q.type = SDL_QUIT;
        SDL_PushEvent(&q);
    }
}

int FrontendDf2::run() {
    std::vector<uint8_t> raw;
    if (vfs_.read("DF2.BIN", raw)) menuText_.parse(raw);
    if (vfs_.read("DF2BRIEF.BIN", raw)) briefText_.parse(raw);
    if (vfs_.read("DF2GAME.BIN", raw)) gameText_.parse(raw);
    if (!vfs_.read("DF2.MNU", raw) || !mnu_.parse(std::string(raw.begin(), raw.end()), menuText_)) {
        std::fprintf(stderr, "cannot read DF2.MNU\n");
        return 1;
    }
    const auto exe = findDf2Exe(o_.gameDir);
    saveDir_ = exe.empty() ? o_.gameDir : exe.parent_path();
    if (ExeImage im; !exe.empty() && im.load(exe, nullptr)) cdExe_ = im.build() == ExeBuild::Df2Cd;
    loadSoldiers();
    cfg_ = DfConfig::df2();   // DF2.EXE's own keys (0x436ab0 / 0x436cd0)
    cfg_.load(saveDir_ / "df2.cfg");
    audio_.openDevice();
    {
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(saveDir_, ec))
            if (upper(e.path().filename().string()) == "DF2MUSIC.SBF") musicLoaded_ = music_.load(vfs_, e.path(), "DF2MUSIC.BIN");
    }
    if (!musicLoaded_) std::fprintf(stderr, "no menu music (DF2MUSIC.SBF / DF2MUSIC.BIN)\n");
    // 0x43cbc0: the music plays only while df2.cfg's music_volume is above
    // 0, at that volume (0x46c170).
    music_.setVolume(std::clamp(cfg_.get("music_volume"), 0, 255));
    // DF2.EXE's mixer is DF.EXE's integer one (0x48e028; tables 0x48d240,
    // 0x48d100, 0x4a09d0 and pan 0x4a06ce byte-identical to 0x4973c0,
    // 0x497280, 0x4aa320, 0x4aa019; music (V >> 1) >> shift, 0x4a0339), with
    // the mode-3 hook set at start-up (0x420919: 0x4712b0(3, 1)) and reset
    // when the menus close for the Game Loop (0x42f420 -> 0x4712e0).
    music_.setIntegerUnits(true);
    music_.setDf2(true);   // DF2.EXE's VM (switch 0x35) and engine (fades, 0x46bfd0)
    audio_.setDfMixer(true, true);
    audio_.setDfGame(2);   // DF2.EXE's WAV conversion (0x4711a0) and mixer (0x48e028)
    audio_.setStream([this](float* out, int frames) { music_.mix(out, frames); });
    if (!openWindow()) return 1;
    const std::string& s0 = o_.startScreen;
    // The first state is MainMenu (0x41ff91); its init on the first entry
    // (0x435500, flag 0x4b14b8) sets up the menu system (0x42f230), plays
    // the intro unless df2.cfg's no_anim is set (0x43552a -> 0x435830),
    // sets the menu video mode (0x436c40), starts the music (0x43cbc0)
    // and goes on to ChooseSoldier. `play intro` is the same start, for tests.
    if ((s0.empty() || s0 == "intro") && cfg_.get("no_anim") == 0) playIntroMovie();
    if (introShot_) {   // testing: the screenshot was a frame of the intro
        closeWindow();
        return 0;
    }
    if (cfg_.get("music_volume") > 0) music_.start();
    music_.setVar(0, 0);
    Screen first = s0 == "main" ? Screen::Main : s0 == "campaign" ? Screen::Campaign : s0 == "quick" ? Screen::Quick
                 : s0 == "briefing" ? Screen::Briefing : s0 == "loadout" ? Screen::Gear : s0 == "settings" ? Screen::Options1
                 : s0 == "keyboard" ? Screen::Options2 : s0 == "difficulty" ? Screen::Options3 : s0 == "voice" ? Screen::Options4
                 : s0 == "credits" ? Screen::Credits : Screen::Soldier;
    if (first == Screen::Briefing || first == Screen::Gear) bms_ = "D03M01";
    enter(first);
    bool windowQuit = false;
    Uint64 last = SDL_GetPerformanceCounter();
    double clock = 0;
    while (screen_ != Screen::Quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (fullscreenEvent(win_, e)) continue;
            if (e.type == SDL_QUIT) { next_ = Screen::Quit; leaveTicks_ = 0; windowQuit = true; break; }
            if (e.type == SDL_TEXTINPUT && !editing_.empty()) {
                for (const char* p = e.text.text; *p; ++p)
                    if (editText_.size() < 15 && static_cast<unsigned char>(*p) >= 32 && static_cast<unsigned char>(*p) < 128) editText_ += *p;
            }
            if (e.type == SDL_KEYDOWN) key(e.key);
            if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                const int mx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x, my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
                int ww, wh, dw, dh;
                SDL_GetWindowSize(win_, &ww, &wh);
                SDL_GL_GetDrawableSize(win_, &dw, &dh);
                const float px = float(mx) * float(dw) / float(std::max(1, ww)), py = float(my) * float(dh) / float(std::max(1, wh));
                // The window may have changed size since the last frame (fullscreen).
                scale_ = std::min(float(dw) / 640.f, float(dh) / 480.f);
                offX_ = (float(dw) - 640.f * scale_) / 2;
                offY_ = (float(dh) - 480.f * scale_) / 2;
                mouseX_ = int((px - offX_) / scale_);
                mouseY_ = int((py - offY_) / scale_);
                if (busy() || !editing_.empty()) continue;
                // 0x43a740: while capturing, a button-down is the binding (left 1,
                // right 2, middle 3) and goes no further.
                if (screen_ == Screen::Options2 && keyCapture_ && e.type == SDL_MOUSEBUTTONDOWN) {
                    const int m = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2
                                : e.button.button == SDL_BUTTON_MIDDLE ? 3 : 0;
                    if (m) { keyMouse_ = m; pressed_.clear(); continue; }
                }
                // GOG Df2.exe 0x433f20: while a slider is held (0x4f2034) a
                // move goes to it alone; letting go ends the hold (state 1,
                // event 0xA) and is then handled as any button-up.
                const bool leftDown = e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT;
                const bool leftUp = e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT;
                if (D2Button* d = btn(dragging_)) {
                    if (e.type == SDL_MOUSEMOTION) { dragTo(*d); continue; }
                    if (leftUp) {
                        dragging_.clear();
                        if (d->drawstate == 3) { d->drawstate = 1; sliderChanged(*d); }
                    }
                }
                // 0x433ec0: the hit button's type picks its handler: 1 push
                // (0x433a30), 2 / 3 slider (0x433d10 / 0x433b60), any other
                // type nothing. Each ignores drawstate 0 and 4.
                D2Button* b = hit(mouseX_, mouseY_);
                const std::string name = b ? b->name : std::string();
                auto live = [](const D2Button* x) { return x && x->drawstate != 0 && x->drawstate != 4; };
                if (name != hover_) {
                    if (D2Button* old = btn(hover_)) {
                        if (old->type == 1 && live(old)) {
                            if (old->drawstate == 2 || old->drawstate == 3) old->drawstate = 1;
                            event(*old, 2);
                        } else if ((old->type == 2 || old->type == 3) && old->drawstate == 3) {
                            old->drawstate = 1;
                        }
                    }
                    hover_ = name;
                    // 0x433a30 event 1: btn_roll unless M (0x1000), whether
                    // or not there is a callback.
                    if (b && b->type == 1 && live(b)) {
                        b->drawstate = 2;
                        if (!(b->attrib & 0x1000)) sound("btn_roll", 4);
                        event(*b, 1);
                    }
                }
                if (b && b->type == 1 && live(b)) {
                    if (leftDown) {
                        b->drawstate = 3;
                        if (!(b->attrib & 0x1000)) sound("btn_slct", 3);
                        pressed_ = name;
                        event(*b, 3);
                    } else if (leftUp && pressed_ == name) {
                        pressed_.clear();
                        b->drawstate = 2;
                        event(*b, 4);
                    }
                } else if (b && (b->type == 2 || b->type == 3) && live(b)) {
                    if (leftDown) sliderPress(*b);
                    else if (leftUp && b->drawstate == 3) { b->drawstate = 1; sliderChanged(*b); }
                }
            }
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        clock += double(now - last) / double(SDL_GetPerformanceFrequency());
        last = now;
        if (!o_.screenshot.empty()) clock = 0.016 * 4;
        for (; clock >= 0.016; clock -= 0.016) {
            ++ticks_;
            // Testing: OPENDF_FE2EVENTS="TICK OP ARGS; ..." pushes real SDL
            // events (OP m = move to x y, c = left click at x y, r = right
            // click, k = key by SDL name) in 640x480 screen coordinates.
            if (const char* ev = std::getenv("OPENDF_FE2EVENTS")) {
                std::istringstream all(ev);
                std::string item;
                while (std::getline(all, item, ';')) {
                    std::istringstream in(item);
                    uint64_t at = 0;
                    std::string op;
                    if (!(in >> at >> op) || at != ticks_) continue;
                    auto toWin = [&](float x, float y, int& wx, int& wy) {
                        int ww, wh, dw, dh;
                        SDL_GetWindowSize(win_, &ww, &wh);
                        SDL_GL_GetDrawableSize(win_, &dw, &dh);
                        wx = int((offX_ + x * scale_) * float(ww) / float(std::max(1, dw)));
                        wy = int((offY_ + y * scale_) * float(wh) / float(std::max(1, dh)));
                    };
                    if (op == "m" || op == "c" || op == "r") {
                        float x = 0, y = 0;
                        in >> x >> y;
                        int wx, wy;
                        toWin(x, y, wx, wy);
                        SDL_Event m{};
                        m.type = SDL_MOUSEMOTION;
                        m.motion.x = wx;
                        m.motion.y = wy;
                        SDL_PushEvent(&m);
                        if (op != "m")
                            for (Uint32 t : {Uint32(SDL_MOUSEBUTTONDOWN), Uint32(SDL_MOUSEBUTTONUP)}) {
                                SDL_Event b{};
                                b.type = t;
                                b.button.button = op == "c" ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
                                b.button.x = wx;
                                b.button.y = wy;
                                SDL_PushEvent(&b);
                            }
                    } else if (op == "k") {
                        std::string name;
                        std::getline(in >> std::ws, name);
                        SDL_Event k{};
                        k.type = SDL_KEYDOWN;
                        k.key.keysym.scancode = SDL_GetScancodeFromName(name.c_str());
                        k.key.keysym.sym = SDL_GetKeyFromScancode(k.key.keysym.scancode);
                        SDL_PushEvent(&k);
                    }
                }
            }
            if (stageTicks_ > 0) --stageTicks_;
            if (leaveTicks_ > 0) --leaveTicks_;
            // Teletype boxes (0x43d040 / 0x43d110): one character a tick; while
            // unfinished, every frame plays a random TTY_CLK1..9 on channel 2 (0x43d000).
            if (helpShown_ < helpText_.size()) {
                ++helpShown_;
                sound("TTY_CLK" + std::to_string(std::uniform_int_distribution<int>(1, 9)(rng_)), 2);
            }
            if (screen_ == Screen::Options1 || screen_ == Screen::Options2 || screen_ == Screen::Options3 || screen_ == Screen::Options4)
                captureTick();
            if (screen_ == Screen::Credits) creditTick();
            if (D2Menu* m = menu())
                for (int i : m->buttons) {
                    D2Button& b = mnu_.buttons[size_t(i)];
                    if (b.flash > 0) --b.flash;
                }
            if (screen_ == Screen::Campaign) campaignTick();   // GOG 0x43457b
            if (screen_ == Screen::Briefing) { mapTick(); voiceTick(); }   // GOG 0x43ca20 (CD 0x437820), the page's tick
            audio_.advance(0.016);   // OPENDF_AUDIODUMP (tests)
            music_.update();
        }
        if (!editing_.empty())
            if (D2Button* b = btn(editing_)) b->text = editText_ + ((ticks_ / 15) & 1 ? "_" : "");
        // 0x42c800 starts stage 2: only the Main Menu (0x4356a0) and the Mission
        // Briefing (0x437811) wait it out, 12 ticks with input ignored; nothing
        // is drawn differently (the fade hooks are empty in the retail game).
        if (next_ != screen_ && !windowQuit && (screen_ == Screen::Main || screen_ == Screen::Briefing)) {
            if (!leaving_) { leaving_ = true; leaveTicks_ = 13; }
            if (leaveTicks_ > 0) goto draw;
        }
        leaving_ = false;
        if (next_ != screen_) {
            if (screen_ == Screen::Briefing) leaveBriefing();   // 0x437a70
            if (next_ == Screen::Mission) {
                // Pre Menu -> Game Loop -> Post Menu (0x43ad90).
                music_.stop();
                saveSoldiers();
                closeWindow();
                GameOptions g = o_.game;
                g.mission = bms_;
                g.soldierName = soldier().name();
                g.df2Sidearm = soldier().get(0x2b8);
                g.df2Primary = soldier().get(0x2bc);
                g.df2Secondary = soldier().get(0x2c0);
                g.df2Gear = soldier().get(0x2c4);
                g.df2Grenade = soldier().get(0x2c8);
                // GOG Df2.exe 0x43c280: faces 10-14 ([0x67cc14] = +0x278 > 9)
                // are female (player slot +0xc4 via 0x4604e0, read by the spawn 0x45c150).
                g.female = soldier().get(0x278) > 9;
                g.df2Attributes = uint32_t(cfg_.get("attributes"));
                g.df2Wind = std::clamp(cfg_.get("sp_wind"), 0, 2);
                g.gpsIcons = std::clamp(cfg_.get("gpsicon"), 0, 2);
                g.crosshairs = cfg_.get("crosshairs") != 0;
                g.flipMouse = soldier().get(0x5b4) != 0;
                // GOG Df2.exe 0x43c346 / 0x43c352: [0x67ccf8] = +0x5b0, [0x67ccfc]
                // = +0x5d8 + 0x80, both clamped by the mouse code 0x41eb50
                // (<= 0 -> 0x80 / <= 0x80 -> 0x80, > 0x1ff -> 0x1ff) and
                // written back after the mission (0x43c1f0 / 0x43c205: +0x5d8
                // = [0x67ccfc] - 0x80).
                const int mouseSens = soldier().get(0x5b0) <= 0 ? 0x80 : std::min(soldier().get(0x5b0), 0x1ff);
                const int scopeSens = std::clamp(soldier().get(0x5d8) + 0x80, 0x80, 0x1ff);
                g.mouseScale = mouseSens;
                g.df2ScopeSens = scopeSens;
                g.dialogVolume = std::clamp(soldier().get(0x5c0), 0, 255);
                // 0x43afb0: each row's command takes the VK (0 clears it) and the
                // mouse mask (1, 2, 0x10 for button 3); FIRE_WEAPON sets attack_2 too.
                g.bindings.clear();
                for (size_t i = 0; i < kDf2KeyRows; ++i) {
                    const uint32_t vk = uint32_t(soldier().get(0x2e0 + 12 * i)) & 0xFFFF, m = uint32_t(soldier().get(0x2e4 + 12 * i));
                    const uint32_t mask = m == 1 ? 1 : m == 2 ? 2 : m == 3 ? 0x10 : 0;
                    g.bindings.push_back({kDf2Keys[i].game, vk | mask << 16});
                    if (i == 8) g.bindings.push_back({"attack_2", vk | mask << 16});
                }
                // The campaign switches of the soldier's campaign (+0x28), for
                // campaign and quick missions alike (GOG Df2.exe 0x43c280:
                // [0x67cf44] = +0xa8 + 4 * [+0x28]).
                const size_t flagsAt = 0xa8 + 4 * size_t(std::clamp(soldier().get(0x28), 0, 7));
                uint32_t flags = uint32_t(soldier().get(flagsAt));
                MissionOutcome out;
                audio_.setDfMixer(true, false);   // 0x43b68d: the hook is off from here on
                runGame(vfs_, g, &out, &flags);
                soldier().set(0x5b0, mouseSens);
                soldier().set(0x5d8, scopeSens - 0x80);
                // GOG Df2.exe 0x43c6f0 (CD 0x437550, from 0x464140), for every
                // finished run (those replayed with Space first, 0x466ad0 ->
                // 0x464140, already recorded when the window is then closed):
                // a single-player win, campaign or quick, adds the kills
                // (+0x29c) and one completed mission (+0x2a4, every win; GOG
                // only); a
                // campaign mission ([0x4c3f88], index [0x4c3f70] >= 0) is also
                // marked done. Nothing on a loss. The flags word goes back
                // every time (it only changes on a win, 0x464140). +0x2b0 /
                // +0x2ac (from the mission's per-player stats) are not tracked.
                auto record = [&](bool won, int kills) {
                    if (!won) return;
                    if (fromCampaign_ && missionSel_ >= 0) soldier().set(0x30 + 4 * size_t(missionSel_), 1);
                    soldier().set(0x29c, soldier().get(0x29c) + kills);
                    if (!cdExe_) soldier().set(0x2a4, soldier().get(0x2a4) + 1);   // CD 0x437550 has no +0x2a4
                };
                for (const MissionOutcome::Run& r : out.replayed) record(r.won, r.kills);
                if (out.quit) {
                    if (!out.replayed.empty()) soldier().set(flagsAt, int32_t(flags));
                    break;
                }
                record(out.won, out.kills);
                soldier().set(flagsAt, int32_t(flags));
                saveSoldiers();
                if (!openWindow()) return 1;
                if (cfg_.get("music_volume") > 0) music_.start();
                music_.setVar(0, 1);
                music_.setVar(2, out.won ? 2 : 1);
                enter(fromCampaign_ ? Screen::Campaign : Screen::Quick);
            } else if (next_ == Screen::Quit) {
                break;
            } else {
                enter(next_);
            }
        }
    draw:
        int dw, dh;
        SDL_GL_GetDrawableSize(win_, &dw, &dh);
        // The 640x480 screens scaled to the window, aspect kept (an amenity).
        scale_ = std::min(float(dw) / 640.f, float(dh) / 480.f);
        offX_ = (float(dw) - 640.f * scale_) / 2;
        offY_ = (float(dh) - 480.f * scale_) / 2;
        glViewport(0, 0, dw, dh);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        hud().begin(dw, dh);
        drawMenu();
        drawCursor();
        drawConnectors();
        hud().end();
        if (!o_.screenshot.empty() && ++frame_ >= o_.frames) {
            std::vector<uint8_t> px(size_t(dw) * size_t(dh) * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            if (FILE* f = std::fopen(o_.screenshot.c_str(), "wb")) {
                std::fprintf(f, "P6 %d %d 255\n", dw, dh);
                for (int y = dh - 1; y >= 0; --y) std::fwrite(&px[size_t(y) * size_t(dw) * 3], 1, size_t(dw) * 3, f);
                std::fclose(f);
            }
            break;
        }
        SDL_GL_SwapWindow(win_);
    }
    saveSoldiers();
    cfg_.save(saveDir_ / "df2.cfg");
    music_.stop();
    closeWindow();
    return 0;
}

int runFrontendDf2(Vfs& vfs, const FrontendOptions& options) {
    FrontendDf2 f(vfs, options);
    return f.run();
}

} // namespace df
