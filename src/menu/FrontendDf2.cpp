#include "FrontendDf2.h"

#include "../audio/Audio.h"
#include "../audio/Music.h"
#include "../core/ExeImage.h"
#include "../formats/Image.h"
#include "../formats/Mission.h"
#include "../formats/Pcx.h"
#include "../formats/Rtxt.h"
#include "../game/Config.h"
#include "../render/Gl.h"
#include "../render/Hud.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
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
        // 0x42e2f0 / 0x42e290: per-menu lists by descending zorder (stable).
        for (size_t i = 0; i < buttons.size(); ++i)
            if (auto it = menus.find(buttons[i].menu); it != menus.end()) it->second.buttons.push_back(int(i));
        for (auto& [name, m] : menus)
            std::stable_sort(m.buttons.begin(), m.buttons.end(), [&](int a, int b) { return buttons[size_t(a)].zorder > buttons[size_t(b)].zorder; });
        return !menus.empty() && !buttons.empty();
    }
    D2Button* button(const std::string& name) {
        for (auto& b : buttons)
            if (b.name == name) return &b;
        return nullptr;
    }
};

// ---------------------------------------------------------------- soldiers

// df2plyrs.sav (0x431ba0 / 0x431c90): 'FPBC', version 10, 5 records of 0x6E8.
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
    // Defaults (0x431c00): campaign 1, gear sidearm 5, primary 10, secondary
    // 21, auxiliary 1, grenade 26, mouse 128, SFX 255, music 255, dialogue 200.
    void defaults() {
        d.fill(0);
        set(0x28, 1);
        set(0x2b8, 5); set(0x2bc, 10); set(0x2c0, 21); set(0x2c4, 1); set(0x2c8, 26);
        set(0x5b0, 128); set(0x5b8, 255); set(0x5bc, 255); set(0x5c0, 200);
    }
};

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

struct Glyph { int x = 0, y = 0, w = 0, h = 0; };
struct D2FontTex { HudImage tex; std::array<Glyph, 256> glyphs{}; int height = 0; bool ok = false; };

enum class Screen { Soldier, Main, Campaign, Quick, Briefing, Gear, Options1, Options3, Credits, Mission, Quit };

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
    void text(const std::string& fontName, float x, float y, const std::string& s, uint32_t color, int align = 0);
    int wrapped(const std::string& fontName, float x0, float y0, float x1, float y1, const std::string& s, uint32_t color,
                int skipLines = 0);
    void blit(const HudImage& im, float x, float y, float sx, float sy, float sw, float sh);
    void rectOutline(float x0, float y0, float x1, float y1, uint32_t color);
    void fill(float x0, float y0, float x1, float y1, uint32_t color);
    void sound(const std::string& name);

    // Menus.
    void enter(Screen s);
    D2Menu* menu() { auto it = mnu_.menus.find(menuName_); return it == mnu_.menus.end() ? nullptr : &it->second; }
    D2Button* btn(const std::string& name) { return mnu_.button(name); }
    void layout(D2Button& b);
    D2Button* hit(int x, int y);
    void drawMenu();
    void drawButton(D2Button& b);
    void ownerDraw(D2Button& b);
    void event(D2Button& b, int ev);
    void key(const SDL_KeyboardEvent& k);

    // Data.
    void loadSoldiers();
    void saveSoldiers();
    Soldier& soldier() { return soldiers_[size_t(current_)]; }
    uint32_t header(const std::string& bms, size_t offset);
    bool missionAvailable(int i);
    void refreshSoldier();
    void refreshCampaign();
    void refreshQuick();
    void refreshGear();
    void refreshOptions();
    void loadBriefing();
    void gearStep(size_t slot, int category, int dir, int altCategory = -1);
    void startMission();
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
    void dragTo(D2Button& b) {
        // 0x42f150: value = (mouse - pos - knob/2) / length · range, clamped.
        const HudImage im = image(b.image);
        const float len = float(std::max(1, b.p3));
        const float m = b.type == 2 ? float(mouseX_ - b.x - im.width / 2) : float(mouseY_ - b.y - im.height / 2);
        b.p2 = std::clamp(int(m / len * float(b.p1)), 0, b.p1);
        // Event 0xA: the settings sliders write their values.
        if (b.name == "but_opt1_mssl") soldier().set(0x5b0, b.p2);
        else if (b.name == "but_opt1_sfsl") soldier().set(0x5b8, b.p2);
        else if (b.name == "but_opt1_dvsl") soldier().set(0x5c0, b.p2);
        else if (b.name == "but_opt1_mvsl") { cfg_.set("music_volume", b.p2); music_.setVolume(std::clamp(b.p2, 0, 255)); }
        else if (b.name == "but_qm_vsl") { quickTop_ = b.p2; refreshQuick(); }
        else if (b.name == "but_mb_brvsl") briefLine_ = b.p2;
        else if (b.name == "but_mb_mpvsl") mapY_ = float(b.p2) / float(std::max(1, b.p1)) * (1024.f - 323.f);
        else if (b.name == "but_mb_mphsl") mapX_ = float(b.p2) / float(std::max(1, b.p1)) * (1024.f - 279.f);
    }

    Screen screen_ = Screen::Soldier, next_ = Screen::Soldier, returnTo_ = Screen::Main;
    std::string menuName_;
    int stageTicks_ = 0;   // 12-tick enter stage: input ignored (0x42e610)
    uint64_t ticks_ = 0;
    std::array<Soldier, 5> soldiers_{};
    int current_ = 0;
    std::string editing_;   // button being edited (soldier name)
    std::string editText_, editBefore_;

    int campaign_ = 1, region_ = -1, missionSel_ = -1;
    bool fromCampaign_ = true;
    std::vector<std::string> quick_;   // quick missions (D06*)
    int quickTop_ = 0, quickSel_ = -1;
    std::string bms_;                 // the chosen mission
    std::string helpText_;
    size_t helpShown_ = 0;
    int briefLine_ = 0;
    HudImage briefMap_;
    float mapX_ = 0, mapY_ = 0;
    std::vector<std::pair<float, float>> waypoints_;
    int optPage_ = 1;
    // Voice briefing (0x437aa0): <mission>NN.WAV, NN 01..19, in order.
    int briefVoice_ = 0, briefVoiceTicks_ = 0, briefHandle_ = 0;
    void stopVoice() {
        if (briefHandle_) audio_.stop(briefHandle_);
        briefHandle_ = 0;
        briefVoice_ = 0;
        briefVoiceTicks_ = 0;
        if (D2Button* b = btn("but_mb_rbbtn")) b->text = menuText("PLAY_BRIEFING", "PLAY BRIEFING");
    }
    void nextVoice() {
        while (briefVoice_ >= 1 && briefVoice_ <= 19) {
            char n[32];
            std::snprintf(n, sizeof n, "%s%02d.WAV", bms_.substr(0, std::min<size_t>(6, bms_.size())).c_str(), briefVoice_++);
            if (const SoundClip* c = audio_.clip(vfs_, n)) {
                const float g = float(std::clamp(soldier().get(0x5c0), 0, 255)) / 255.f * 0.5f;
                briefHandle_ = audio_.play(c, g, g);
                briefVoiceTicks_ = c->rate > 0 ? int(c->samples.size() * 1000 / size_t(c->rate) / 16) + 1 : 1;
                return;
            }
        }
        stopVoice();
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
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (o_.screenshot.empty() ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN);
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
    SDL_ShowCursor(SDL_ENABLE);
    return true;
}

void FrontendDf2::closeWindow() {
    hud_.reset();
    images_.clear();
    fonts_.clear();
    briefMap_ = {};
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
        // PCX art: palette index 0 is the colour key (INFERRED for the 4-frame strips).
        im = hud().loadImage(vfs_, key, false, true);
    } else if (vfs_.read(key, data)) {
        RgbaImage rgba;
        if (decodeRgbaImage(data, rgba)) im = hud().makeImage(rgba.width, rgba.height, rgba.rgba);
    }
    images_[key] = im;
    return im;
}

// DF2.EXE 0x4196f0 / 0x4195f0: bands top-down, a band ends at the first row
// whose column-0 pixel is 1 and the next starts two rows below; ink is
// pixel 3 (spacer), 4 or >= 8; each run of ink columns is the next glyph.
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
    std::vector<uint8_t> rgba(size_t(W) * size_t(H) * 4, 0);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const uint8_t v = img.pixels[i];
        if (v >= 8 || v == 4) rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4 + 3] = 255;
    }
    int y0 = 0;
    for (int band = 0; band < 16 && y0 < H; ++band) {
        int y1 = y0;
        while (y1 < H && px(0, y1) != 1) ++y1;
        if (y1 <= y0) break;
        if (!f.height) f.height = y1 - y0;
        int code = band * 16;
        for (int x = 0; x < W && code < band * 16 + 16;) {
            auto ink = [&](int cx) {
                for (int y = y0; y < y1; ++y) {
                    const uint8_t v = px(cx, y);
                    if (v >= 8 || v == 3 || v == 4) return true;
                }
                return false;
            };
            if (!ink(x)) { ++x; continue; }
            const int start = x;
            while (x < W && ink(x)) ++x;
            f.glyphs[size_t(code++)] = {start, y0, x - start, y1 - y0};
        }
        y0 = y1 + 2;
    }
    f.tex = hud().makeImage(W, H, rgba);
    f.ok = f.height > 0;
    return f;
}

float FrontendDf2::textWidth(const std::string& fontName, const std::string& s) {
    D2FontTex& f = font(fontName);
    float w = 0;
    for (char c : s)
        if (c != '&') w += float(f.glyphs[static_cast<unsigned char>(c)].w + 1);
    return w;
}

// Opaque glyphs in `color` (0x4198e0); align 0 left, 1 right, 2 centred;
// y is the top of the glyph cell. '&' marks a hot key (not drawn).
void FrontendDf2::text(const std::string& fontName, float x, float y, const std::string& s, uint32_t color, int align) {
    D2FontTex& f = font(fontName);
    if (!f.ok) return;
    const float w = textWidth(fontName, s);
    if (align == 1) x -= w;
    else if (align == 2) x -= std::floor(w / 2);
    const Color c = argbColor(color);
    for (char ch : s) {
        if (ch == '&') continue;
        const Glyph& g = f.glyphs[static_cast<unsigned char>(ch)];
        if (g.w > 0 && ch != ' ')
            hud().imageUV(f.tex.texture, offX_ + x * scale_, offY_ + y * scale_, float(g.w) * scale_, float(g.h) * scale_,
                          float(g.x) / float(f.tex.width), float(g.y) / float(f.tex.height), float(g.x + g.w) / float(f.tex.width),
                          float(g.y + g.h) / float(f.tex.height), c);
        x += float(g.w + 1);
    }
}

// Word-wrapped text in a box; "\r\n" / "~" break lines. Returns the line count.
int FrontendDf2::wrapped(const std::string& fontName, float x0, float y0, float x1, float y1, const std::string& s, uint32_t color,
                         int skipLines) {
    D2FontTex& f = font(fontName);
    if (!f.ok) return 0;
    std::vector<std::string> lines;
    std::string line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        const std::string cand = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(fontName, cand) > x1 - x0) { lines.push_back(line); line = word; }
        else line = cand;
        word.clear();
    };
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\r') continue;
        if (c == '\n' || c == '~') { flushWord(); lines.push_back(line); line.clear(); continue; }
        if (c == ' ') { flushWord(); continue; }
        word += c;
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    const float lh = float(f.height + 1);
    float y = y0;
    for (size_t i = size_t(std::max(0, skipLines)); i < lines.size() && y + lh <= y1 + 1; ++i, y += lh) text(fontName, x0, y, lines[i], color);
    return int(lines.size());
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

void FrontendDf2::sound(const std::string& name) {
    // MNU [SOUNDS] (0x42f500): the file at its volume (x SFX volume of the soldier).
    auto it = mnu_.sounds.find(upper(name));
    const std::string f = it == mnu_.sounds.end() ? name : it->second;
    int vol = 80;
    if (upper(name) == "BTN_SLCT") vol = 150;
    vol = vol * std::clamp(soldier().get(0x5b8), 0, 255) / 255;
    if (const SoundClip* c = audio_.clip(vfs_, upper(f))) {
        const float g = float(vol) / 255.f * 0.5f;
        audio_.play(c, g, g);
    }
}

// ---------------------------------------------------------------- soldiers

void FrontendDf2::loadSoldiers() {
    for (auto& s : soldiers_) s.defaults();
    std::ifstream f(saveDir_ / "df2plyrs.sav", std::ios::binary);
    char head[8] = {};
    if (f.read(head, 8) && std::memcmp(head, "FPBC", 4) == 0 && head[4] == 10)
        for (auto& s : soldiers_) f.read(reinterpret_cast<char*>(s.d.data()), std::streamsize(s.d.size()));
    bool any = false;
    for (auto& s : soldiers_) any = any || !s.empty();
    // All empty: slot 0 becomes CS_DEFAULTNAME "Blackhawk".
    if (!any) soldiers_[0].setName(menuText("CS_DEFAULTNAME", "Blackhawk"));
}

void FrontendDf2::saveSoldiers() {
    std::ofstream f(saveDir_ / "df2plyrs.sav", std::ios::binary);
    const char head[8] = {'F', 'P', 'B', 'C', 10, 0, 0, 0};
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

// ---------------------------------------------------------------- screens

void FrontendDf2::enter(Screen s) {
    screen_ = next_ = s;
    stageTicks_ = 12;
    hover_.clear();
    pressed_.clear();
    helpText_.clear();
    helpShown_ = 0;
    static const char* kMenus[] = {"CHOOSESOLDIER", "MAIN", "CAMPAIGN", "QUICKMISSION", "MISSIONBRIEFING", "CHANGEGEAR", "OPTIONS1",
                                   "OPTIONS3", "CREDITS", "", ""};
    menuName_ = kMenus[int(s)];
    if (D2Menu* m = menu())
        for (int i : m->buttons) {
            D2Button& b = mnu_.buttons[size_t(i)];
            b.selected = false;
            b.flash = 0;
            if (b.drawstate == 2 || b.drawstate == 3) b.drawstate = 1;
            layout(b);
        }
    sound("mnu_tran");
    // Music Var1 per state (0x43cc20).
    const int var1 = s == Screen::Campaign ? 1 : s == Screen::Options1 ? 3 : s == Screen::Soldier ? 4 : s == Screen::Credits ? 5
                   : s == Screen::Briefing ? 6 : s == Screen::Gear ? 7 : 0;
    music_.setVar(1, var1);
    switch (s) {
    case Screen::Soldier: refreshSoldier(); break;
    case Screen::Main:
        // NovaWorld, Multiplayer and Update are not in OpenDF.
        for (const char* n : {"but_mm_qmb", "but_mm_nwb", "but_mm_upd"})
            if (D2Button* b = btn(n)) b->drawstate = 4;
        break;
    case Screen::Campaign: refreshCampaign(); break;
    case Screen::Quick: refreshQuick(); break;
    case Screen::Briefing: loadBriefing(); break;
    case Screen::Gear: refreshGear(); break;
    case Screen::Options1:
    case Screen::Options3: refreshOptions(); break;
    default: break;
    }
}

// Button size (0x42d8d0): image type 1 -> image width x height / frames;
// sliders: the image, the track p3; text-only: to (user1, user2) or the text.
void FrontendDf2::layout(D2Button& b) {
    const HudImage im = b.image.empty() ? HudImage{} : image(b.image);
    if (im.texture) {
        // 0x42d8d0: a push button's image is always 4 stacked frames
        // (whatever the IMAGE record's frame count); animations use the record's.
        b.w = im.width;
        b.h = im.height / (b.type == 1 ? 4 : std::max(1, imageFrames(b.image)));
        if (b.type == 2) b.w = b.p3;
        if (b.type == 3) b.h = b.p3;
    } else if (!b.text.empty() || b.user1 || b.user2) {
        b.w = b.user1 > b.x ? b.user1 - b.x : int(textWidth(b.font.empty() ? "FRK12N" : b.font, b.text));
        b.h = b.user2 > b.y ? b.user2 - b.y : font(b.font.empty() ? "FRK12N" : b.font).height;
    } else {
        b.w = b.p1;
        b.h = b.p2;
    }
}

D2Button* FrontendDf2::hit(int x, int y) {
    D2Menu* m = menu();
    if (!m) return nullptr;
    // 0x42e9f0: from the highest z-order; J (0x200) ignores the mouse;
    // text-only buttons are 1.6 times as tall; right-justified ones lie left of x.
    for (int i : m->buttons) {
        D2Button& b = mnu_.buttons[size_t(i)];
        if (b.drawstate == 0 || (b.attrib & 0x200) || (b.attrib & 4) || b.callback.empty()) continue;
        if (b.type == 0) continue;
        float x0 = float(b.x), w = float(b.w), h = float(b.h);
        if (b.image.empty() && !(b.attrib & 0x40)) {
            h *= 1.6f;
            if (b.attrib & 0x10) x0 -= w;
        }
        if (x >= x0 && x < x0 + w && y >= b.y && y < b.y + h) return &b;
    }
    return nullptr;
}

void FrontendDf2::drawButton(D2Button& b) {
    if (b.drawstate == 0) return;
    if (b.attrib & 0x40) ownerDraw(b);   // G: owner draw (event 7)
    if (b.attrib & 4) return;            // C: no default draw
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
            const float pos = b.p1 > 0 ? float(b.p2) * float(b.p3) / float(b.p1) : 0;
            blit(im, float(b.x) + (b.type == 2 ? pos : 0), float(b.y) + (b.type == 3 ? pos : 0), 0, 0, float(im.width), fh);
        } else {
            blit(im, float(b.x), float(b.y), 0, 0, float(im.width), fh);
        }
    }
    if (b.text.empty() || b.font.empty()) return;
    auto fi = mnu_.fonts.find(b.font);
    const D2Font df = fi == mnu_.fonts.end() ? D2Font{} : fi->second;
    const uint32_t c = b.drawstate == 4 ? df.fg[2] : (b.drawstate == 2 || b.selected) ? df.fg[1] : df.fg[0];
    const float fh = float(font(b.font).height);
    float y = float(b.y);
    if (im.texture) y += std::floor((float(b.h) - fh) / 2);
    // F / H centre; an image button without D/E/F centres too (INFERRED:
    // the text-in-image placement 0x419fd0 is not traced).
    if ((b.attrib & (0x20 | 0x80)) || (im.texture && !(b.attrib & (0x8 | 0x10)))) text(b.font, float(b.x) + float(b.w) / 2, y, b.text, c, 2);
    else if (b.attrib & 0x10) text(b.font, im.texture ? float(b.x + b.w) : float(b.x), y, b.text, c, 1);
    else text(b.font, float(b.x + 2 + (im.texture ? b.user1 : 0)), y, b.text, c, 0);
}

void FrontendDf2::drawMenu() {
    D2Menu* m = menu();
    if (!m) return;
    const HudImage bg = image(m->background);
    if (bg.texture) blit(bg, 0, 0, 0, 0, float(bg.width), float(bg.height));
    for (auto it = m->buttons.rbegin(); it != m->buttons.rend(); ++it) drawButton(mnu_.buttons[size_t(*it)]);
}

// ---------------------------------------------------------------- owner draws

void FrontendDf2::ownerDraw(D2Button& b) {
    const std::string& cb = b.callback;
    if (cb == "mm_Rect") {
        // 0x435fd0: a one-pixel rectangle (x, y)-(user1, user2), 0xCA4409 (0x844030 disabled).
        rectOutline(float(b.x), float(b.y), float(b.user1), float(b.user2), b.drawstate == 4 ? 0x844030 : 0xCA4409);
        if (b.name == "but_mm_hlrct" && !helpText_.empty())
            wrapped("LCD10N", float(b.x + 5), float(b.y + 5), float(b.user1 - 5), float(b.user2 - 5), helpText_.substr(0, helpShown_),
                    mnu_.fonts["LCD10N"].fg[0]);
    } else if (cb == "ca_DrawRegion") {
        if (region_ >= 0) {
            const HudImage im = image(kRegionImage[region_]);
            blit(im, float(b.x), float(b.y), 0, 0, float(im.width), float(im.height));
            // Titles of the available missions under their dots (LCD10N; LCD10S selected).
            for (int i = 0; i < 28; ++i) {
                if (kMissions[i].region != region_ || !missionAvailable(i)) continue;
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
        const std::string key = bms_.substr(0, std::min<size_t>(6, bms_.size()));
        if (const std::string* t = briefText_.find("MBBRIEF", key))
            wrapped("LCD10S", float(b.x), float(b.y), float(b.user1), float(b.user2), *t, mnu_.fonts["LCD10S"].fg[0], briefLine_);
    } else if (cb == "mb_Map") {
        // The terrain's JPG scaled x4 to 1024² in the view, scrolled; numbered waypoints.
        const float vw = float(b.user1 - b.x), vh = float(b.user2 - b.y);
        if (briefMap_.texture) {
            const float s = 1024.f / float(briefMap_.width);
            blit(briefMap_, float(b.x), float(b.y), mapX_ / s, mapY_ / s, vw / s, vh / s);
        }
        int n = 1;
        for (auto [wx, wy] : waypoints_) {
            const float x = float(b.x) + wx - mapX_, y = float(b.y) + wy - mapY_;
            if (x >= b.x && x < b.user1 - 8 && y >= b.y && y < b.user2 - 10)
                text("LCD10S", x, y, std::to_string(n), mnu_.fonts["LCD10S"].fg[0], 2);
            ++n;
        }
        rectOutline(float(b.x), float(b.y), float(b.user1), float(b.user2), 0xCA4409);
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
    int firstOpen = -1;
    for (int r = 0; r < 5; ++r) {
        bool all = true, any = false;
        for (int i = 0; i < 28; ++i)
            if (kMissions[i].region == r) {
                any = true;
                if (!soldier().get(0x30 + 4 * size_t(i))) all = false;
            }
        const bool mine = (r < 3 ? 1 : 2) == campaign_;
        if (D2Button* b = btn(regionBtn[r])) {
            b->drawstate = mine ? 1 : 4;
            b->image = std::string(kRegionButton[r]) + (all && any ? "B" : "");
            b->selected = r == region_;
            layout(*b);
        }
        if (mine && !all && firstOpen < 0) firstOpen = r;
    }
    if (region_ < 0 || (region_ < 3 ? 1 : 2) != campaign_) region_ = firstOpen >= 0 ? firstOpen : (campaign_ == 1 ? 0 : 3);
    for (int i = 0; i < 28; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "but_ca_%c%c%c%c", kMissions[i].bms[1], kMissions[i].bms[2], kMissions[i].bms[4], kMissions[i].bms[5]);
        if (D2Button* b = btn(name)) {
            const bool shown = kMissions[i].region == region_;
            b->drawstate = !shown ? 0 : missionAvailable(i) ? 1 : 4;
            b->image = soldier().get(0x30 + 4 * size_t(i)) ? "IMAGEBTN37" : "IMAGEBTN36";
            b->selected = i == missionSel_;
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
    briefLine_ = 0;
    briefMap_ = {};
    waypoints_.clear();
    std::vector<uint8_t> d;
    Mission m;
    if (vfs_.read(bms_ + ".BMS", d)) {
        std::string err;
        if (parseMission(d, m, &err)) {
            std::string trn = m.terrain;
            if (auto dot = trn.rfind('.'); dot != std::string::npos) trn.resize(dot);
            std::vector<uint8_t> td, raw;
            TerrainDesc desc;
            if (vfs_.read(trn + ".TRN", td) && parseTrn(std::string(td.begin(), td.end()), desc) && vfs_.read(upper(desc.colorMap) + ".JPG", raw)) {
                RgbaImage im;
                if (decodeRgbaImage(raw, im)) briefMap_ = hud().makeImage(im.width, im.height, im.rgba);
            }
            // The player's route (path flag 2): map metres are 1:1 with the
            // 1024² picture (the 256² JPG x4).
            for (const auto& p : m.paths)
                if (p.flags & 2) {
                    for (int idx : p.points)
                        if (const MissionObject* o = m.inGroup(2, idx); o && waypoints_.size() < 50) waypoints_.push_back({o->x, -o->y});
                    break;
                }
            // Centred on the player's start (the first group-3 object of side 1, INFERRED).
            float px = 512, py = 512;
            for (const auto& o : m.objects)
                if (o.group == 3) { px = o.x; py = -o.y; break; }
            for (auto& w : waypoints_) { w.first = std::fmod(w.first + 1024.f, 1024.f); w.second = std::fmod(w.second + 1024.f, 1024.f); }
            px = std::fmod(px + 1024.f, 1024.f);
            py = std::fmod(py + 1024.f, 1024.f);
            mapX_ = std::clamp(px - 140.f, 0.f, 1024.f - 279.f);
            mapY_ = std::clamp(py - 160.f, 0.f, 1024.f - 323.f);
        }
    }
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
        for (const auto& [n, k] : kHelp)
            if (b.callback == n) { helpText_ = menuText(k, ""); helpShown_ = 0; }
    }
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
        soldier().defaults();
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
        soldier().set(0x28, b.user2);
        region_ = -1;
        missionSel_ = -1;
        refreshCampaign();
    } else if (cb == "ca_ChooseRegion") {
        region_ = b.user2;
        missionSel_ = -1;
        refreshCampaign();
    } else if (cb == "ca_ChooseMission") {
        missionSel_ = b.user2;
        refreshCampaign();
    } else if (cb == "ca_MissionBriefing" && missionSel_ >= 0) {
        bms_ = kMissions[missionSel_].bms;
        fromCampaign_ = true;
        returnTo_ = Screen::Campaign;
        next_ = Screen::Briefing;
    } else if (cb == "ca_Back" || cb == "qm_Back" || cb == "opt_Accept" || cb == "cr_Back") {
        if (cb == "opt_Accept") { saveSoldiers(); cfg_.save(saveDir_ / "df2.cfg"); }
        next_ = Screen::Main;
    }
    // Quick missions.
    else if (cb == "qm_SelMis") {
        const int i = quickTop_ + b.user1;
        if (i < int(quick_.size())) quickSel_ = i;
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
        // PLAY_BRIEFING / STOP_BRIEFING.
        if (briefVoice_) stopVoice();
        else {
            briefVoice_ = 1;
            b.text = menuText("STOP_BRIEFING", "STOP BRIEFING");
            nextVoice();
        }
    }
    else if (cb == "mb_BriefUp") briefLine_ = std::max(0, briefLine_ - 1);
    else if (cb == "mb_BriefDown") ++briefLine_;
    else if (cb == "mb_MapUp") mapY_ = std::max(0.f, mapY_ - 16);
    else if (cb == "mb_MapDown") mapY_ = std::min(1024.f - 323.f, mapY_ + 16);
    else if (cb == "mb_MapLeft") mapX_ = std::max(0.f, mapX_ - 16);
    else if (cb == "mb_MapRight") mapX_ = std::min(1024.f - 279.f, mapX_ + 16);
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
    else if (cb == "opt_NextPage" || cb == "opt_PrevPage") next_ = screen_ == Screen::Options1 ? Screen::Options3 : Screen::Options1;
    else if (cb == "opt1_FMChange") { soldier().set(0x5b4, !soldier().get(0x5b4)); refreshOptions(); }
    else if (cb == "opt1_TUChange") { cfg_.set("turbo", !cfg_.get("turbo")); refreshOptions(); }
    else if (cb == "opt1_MSLeft" || cb == "opt1_MSRight") { soldier().set(0x5b0, std::clamp(soldier().get(0x5b0) + (cb == "opt1_MSRight" ? 5 : -5), 0, 512)); refreshOptions(); }
    else if (cb == "opt1_FXLeft" || cb == "opt1_FXRight") { soldier().set(0x5b8, std::clamp(soldier().get(0x5b8) + (cb == "opt1_FXRight" ? 5 : -5), 0, 256)); refreshOptions(); }
    else if (cb == "opt1_MVLeft" || cb == "opt1_MVRight") {
        cfg_.set("music_volume", std::clamp(cfg_.get("music_volume") + (cb == "opt1_MVRight" ? 5 : -5), 0, 256));
        music_.setVolume(std::clamp(cfg_.get("music_volume"), 0, 255));
        refreshOptions();
    } else if (cb == "opt1_DVLeft" || cb == "opt1_DVRight") { soldier().set(0x5c0, std::clamp(soldier().get(0x5c0) + (cb == "opt1_DVRight" ? 5 : -5), 0, 256)); refreshOptions(); }
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
    if (stageTicks_ > 0) return;
    // BACK buttons carry hot key 1 (Escape).
    if (sym == SDLK_ESCAPE)
        if (D2Menu* m = menu())
            for (int i : m->buttons) {
                D2Button& b = mnu_.buttons[size_t(i)];
                if (b.hot1 == 1 && b.drawstate != 0 && b.drawstate != 4) { sound("btn_slct"); b.flash = 3; event(b, 3); event(b, 4); return; }
            }
}

// ---------------------------------------------------------------- loop

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
    loadSoldiers();
    cfg_.load(saveDir_ / "df2.cfg");
    audio_.openDevice();
    {
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(saveDir_, ec))
            if (upper(e.path().filename().string()) == "DF2MUSIC.SBF") musicLoaded_ = music_.load(vfs_, e.path(), "DF2MUSIC.BIN");
    }
    if (!musicLoaded_) std::fprintf(stderr, "no menu music (DF2MUSIC.SBF / DF2MUSIC.BIN)\n");
    music_.setVolume(255);
    audio_.setStream([this](float* out, int frames) { music_.mix(out, frames); });
    if (!openWindow()) return 1;
    music_.start();
    music_.setVar(0, 0);
    const std::string& s0 = o_.startScreen;
    Screen first = s0 == "main" ? Screen::Main : s0 == "campaign" ? Screen::Campaign : s0 == "quick" ? Screen::Quick
                 : s0 == "briefing" ? Screen::Briefing : s0 == "loadout" ? Screen::Gear : s0 == "settings" ? Screen::Options1
                 : s0 == "difficulty" ? Screen::Options3 : s0 == "credits" ? Screen::Credits : Screen::Soldier;
    if (first == Screen::Briefing || first == Screen::Gear) bms_ = "D03M01";
    enter(first);
    int frame = 0;
    Uint64 last = SDL_GetPerformanceCounter();
    double clock = 0;
    while (screen_ != Screen::Quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { next_ = Screen::Quit; break; }
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
                mouseX_ = int((px - offX_) / scale_);
                mouseY_ = int((py - offY_) / scale_);
                if (stageTicks_ > 0 || !editing_.empty()) continue;
                if (D2Button* d = btn(dragging_)) {
                    if (e.type == SDL_MOUSEMOTION) dragTo(*d);
                    else if (e.type == SDL_MOUSEBUTTONUP) dragging_.clear();
                    continue;
                }
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                    if (D2Menu* m = menu())
                        for (int i : m->buttons) {
                            D2Button& s = mnu_.buttons[size_t(i)];
                            if ((s.type != 2 && s.type != 3) || s.drawstate == 0 || s.drawstate == 4) continue;
                            const HudImage knob = image(s.image);
                            const float w = s.type == 2 ? float(s.p3 + knob.width) : float(knob.width);
                            const float h = s.type == 3 ? float(s.p3 + knob.height) : float(knob.height);
                            if (mouseX_ >= s.x && mouseX_ < s.x + w && mouseY_ >= s.y && mouseY_ < s.y + h) {
                                dragging_ = s.name;
                                dragTo(s);
                                break;
                            }
                        }
                if (!dragging_.empty()) continue;
                D2Button* b = hit(mouseX_, mouseY_);
                const std::string name = b ? b->name : std::string();
                if (name != hover_) {
                    if (D2Button* old = btn(hover_)) {
                        if (old->drawstate == 2 || old->drawstate == 3) old->drawstate = 1;
                        event(*old, 2);
                    }
                    hover_ = name;
                    if (b && b->drawstate != 4) {
                        b->drawstate = 2;
                        if (!(b->attrib & 0x400)) sound("btn_roll");
                        event(*b, 1);
                    }
                }
                if (b && b->drawstate != 4) {
                    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                        b->drawstate = 3;
                        if (!(b->attrib & 0x400)) sound("btn_slct");
                        pressed_ = name;
                        event(*b, 3);
                    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && pressed_ == name) {
                        pressed_.clear();
                        b->drawstate = 2;
                        event(*b, 4);
                    }
                }
            }
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        clock += double(now - last) / double(SDL_GetPerformanceFrequency());
        last = now;
        if (!o_.screenshot.empty()) clock = 0.016 * 4;
        for (; clock >= 0.016; clock -= 0.016) {
            ++ticks_;
            if (stageTicks_ > 0) --stageTicks_;
            if (helpShown_ < helpText_.size()) ++helpShown_;   // one character a tick (0x43d1a0)
            if (D2Menu* m = menu())
                for (int i : m->buttons) {
                    D2Button& b = mnu_.buttons[size_t(i)];
                    if (b.flash > 0) --b.flash;
                }
            // The next incomplete region blinks while none is chosen (0x42fc50).
            if (briefVoice_ && --briefVoiceTicks_ <= 0) nextVoice();
            music_.update();
        }
        if (!editing_.empty())
            if (D2Button* b = btn(editing_)) b->text = editText_ + ((ticks_ / 15) & 1 ? "_" : "");
        if (next_ != screen_) {
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
                g.gpsIcons = std::clamp(cfg_.get("gpsicon"), 0, 2);
                g.crosshairs = cfg_.get("crosshairs") != 0;
                g.flipMouse = soldier().get(0x5b4) != 0;
                g.mouseScale = std::clamp(soldier().get(0x5b0), 0, 255);
                g.dialogVolume = std::clamp(soldier().get(0x5c0), 0, 255);
                const int camp = fromCampaign_ && missionSel_ >= 0 ? kMissions[missionSel_].campaign : 0;
                uint32_t flags = camp ? uint32_t(soldier().get(0xa8 + 4 * size_t(camp))) : 0;
                MissionOutcome out;
                runGame(vfs_, g, &out, &flags);
                if (out.quit) break;
                // 0x437550: a campaign win marks the mission done; kills add
                // up; the unlock mask comes back from the game.
                if (camp) {
                    soldier().set(0xa8 + 4 * size_t(camp), int32_t(flags));
                    if (out.won && missionSel_ >= 0) {
                        if (!soldier().get(0x30 + 4 * size_t(missionSel_))) soldier().set(0x2a4, soldier().get(0x2a4) + 1);
                        soldier().set(0x30 + 4 * size_t(missionSel_), 1);
                    }
                    soldier().set(0x29c, soldier().get(0x29c) + out.kills);
                }
                saveSoldiers();
                if (!openWindow()) return 1;
                music_.start();
                music_.setVar(0, 1);
                music_.setVar(2, out.won ? 2 : 1);
                enter(fromCampaign_ ? Screen::Campaign : Screen::Quick);
            } else if (next_ == Screen::Quit) {
                break;
            } else {
                enter(next_);
            }
        }
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
        hud().end();
        if (!o_.screenshot.empty() && ++frame >= o_.frames) {
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
