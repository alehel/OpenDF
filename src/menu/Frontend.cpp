#include "Frontend.h"

#include "../audio/Audio.h"
#include "../audio/Music.h"
#include "../core/ExeImage.h"
#include "../game/Config.h"
#include "../formats/Pcx.h"
#include "../formats/Rtxt.h"
#include "../render/Gl.h"
#include "../render/Hud.h"
#include "Mnu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>

namespace df {

namespace {

constexpr int kProfileSize = 0xA90;

Color argb(uint32_t c) {
    return {float((c >> 16) & 255) / 255.f, float((c >> 8) & 255) / 255.f, float(c & 255) / 255.f, float(c >> 24) / 255.f};
}

// A soldier profile (playerNN.ply, 0xA90 bytes, DF.EXE 0x40f40c / 0x40f630):
// "PLY" + version 0x1A, name +0x04, flags +0x24 (bit 0 in use), campaign
// +0x28, per campaign c mission-done flags +0x30 + c*0x44 + 4i and campaign
// flags +0x70 + c*0x44, selected mission +0x470 + 4c, face +0x640, campaign
// kills +0x664, sidearm/primary/secondary item +0x680/+0x684/+0x688, list
// indices +0x694/+0x698/+0x69C.
struct Profile {
    std::array<uint8_t, kProfileSize> d{};
    int32_t get(int off) const { int32_t v; std::memcpy(&v, &d[size_t(off)], 4); return v; }
    void set(int off, int32_t v) { std::memcpy(&d[size_t(off)], &v, 4); }
    bool valid() const { return d[0] == 'P' && d[1] == 'L' && d[2] == 'Y' && (get(0x24) & 1); }
    std::string name() const { return std::string(reinterpret_cast<const char*>(&d[4]), strnlen(reinterpret_cast<const char*>(&d[4]), 15)); }
    std::string str(int off, int max) const { return std::string(reinterpret_cast<const char*>(&d[size_t(off)]), strnlen(reinterpret_cast<const char*>(&d[size_t(off)]), size_t(max))); }
    void setStr(int off, int max, const std::string& v) {
        std::memset(&d[size_t(off)], 0, size_t(max));
        std::memcpy(&d[size_t(off)], v.data(), std::min(v.size(), size_t(max - 1)));
    }
    void setName(const std::string& n) {
        std::memset(&d[4], 0, 16);
        std::string u = upperCase(n).substr(0, 15);
        std::memcpy(&d[4], u.data(), u.size());
    }
    void defaults() {
        // 0x40ee14: .45, M4/M203, Double ammo; +0x68C = 8.
        d.fill(0);
        d[0] = 'P'; d[1] = 'L'; d[2] = 'Y'; d[3] = 0x1A;
        set(0x28, 1);
        set(0x680, 6);
        set(0x684, 9);
        set(0x688, 1);
        set(0x68C, 8);
    }
};

struct Glyph { int x = 0, y = 0, w = 0, h = 0; };

// A menu font sheet (0x442338): bands of 16 characters between full-width
// lines of palette index 1; a column with ink (index >= 8, 4, or 3 = width
// marker) belongs to a glyph; each run of ink columns is the next character.
struct MenuFont {
    HudImage tex;
    std::array<Glyph, 256> glyphs{};
    int height = 0;
    bool ok = false;
};

// Mission counts per campaign (0x4c5664) and campaign-map dots (0x4c5450).
const int kMissionCount[6] = {4, 6, 6, 8, 10, 10};
const int kDots[6][10][2] = {
    {},
    {{103, 30}, {119, 50}, {154, 146}, {176, 176}, {125, 204}, {70, 112}},
    {{108, 47}, {95, 119}, {166, 96}, {142, 188}, {91, 221}, {135, 151}},
    {{24, 129}, {44, 173}, {85, 206}, {80, 139}, {116, 117}, {104, 161}, {139, 154}, {214, 150}},
    {{30, 97}, {54, 55}, {86, 71}, {89, 116}, {146, 98}, {120, 147}, {147, 124}, {171, 161}, {231, 150}, {166, 197}},
    {{213, 19}, {212, 45}, {137, 36}, {108, 61}, {80, 98}, {75, 125}, {33, 146}, {42, 171}, {66, 195}, {167, 61}},
};

// Change Gear lists (0x4dbc78 / 0x4dc04c / 0x4dc35c): item id, picture, text key.
struct GearEntry { int item; const char* image; const char* text; const char* name; };
const GearEntry kPrimary[] = {{9, "ImageGunM4M203", "LOM4M203TXT", "M4/M203"},  {23, "ImageGunM40A1", "LOM4M40A1TXT", "M40A1"},
                              {12, "ImageGunBarret", "LOBLTXT", "Barrett"},     {15, "ImageGunHKMP5", "LOHKMP5TXT", "HK MP5"},
                              {11, "ImageGunM249", "LOM249TXT", "M249 SAW"}};
const GearEntry kSecondary[] = {{1, "ImageGunAmmoBox", "LODAMMOTXT", "Double Ammo"}, {21, "ImageGunSatchel", "LOSATCHELTXT", "2 Satchels"},
                                {24, "ImageGunClaymore", "LOCLAYMORETXT", "2 Claymores"}, {20, "ImageGunLaw", "LOLAWTXT", "2 LAWs"}};
const GearEntry kSidearm[] = {{6, "ImageGunSpec45", "LOSPEC45TXT", "Spec Ops .45"}, {5, "ImageGunHS22", "LOHS22STXT", "HS .22"}};

// Loadout row ids -> the game's loadout names.
std::string primaryName(int item) {
    switch (item) { case 23: return "M40"; case 12: return "Barrett"; case 15: return "MP5"; case 11: return "SAW"; default: return "M4"; }
}
std::string kitName(int item) {
    switch (item) { case 21: return "Satchel"; case 24: return "Claymore"; case 20: return "LAW"; default: return "Ammo"; }
}
std::string sidearmName(int item) { return item == 5 ? "HS22" : ".45"; }

} // namespace

class Frontend {
public:
    Frontend(Vfs& vfs, const FrontendOptions& o) : vfs_(vfs), o_(o) {}
    int run();

private:
    enum Screen { Start, Soldier, Main, Options1, Options3, Macros, Campaign, Briefing, Loadout, Mission, Debrief, End, Quit };

    bool openWindow();
    void closeWindow();
    bool loadData();
    void loadProfiles();
    void saveProfile(int i);
    Profile& prof() { return profiles_[size_t(cur_)]; }

    HudImage file(const std::string& pcx, bool transparent = false);
    HudImage image(const std::string& name, bool transparent = false);
    MenuFont& font(const std::string& name);
    float textWidth(MenuFont& f, const std::string& s);
    // Text with {C:Name} colour codes; align 0 left, 1 centre, 2 right.
    void drawText(const std::string& fontName, float x, float y, const std::string& s, uint32_t color, int align = 0);
    // Word wrap (~0x409260): words at spaces, "~" a line break, "{...}"
    // link tags dropped; returns the number of lines.
    int drawWrapped(const std::string& fontName, float x, float y, float w, float h, const std::string& s, uint32_t color,
                    int startLine = 0);
    void blit(const HudImage& im, float x, float y, float sx, float sy, float sw, float sh, bool translucent);

    void enter(Screen s);
    void setMenu(const std::string& name);
    void drawMenu();
    void drawButton(MnuButton& b);
    MnuButton* hit(int x, int y);
    void event(MnuButton& b, int ev);
    void standard(MnuButton& b, int ev, const std::function<void()>& action);
    void ownerDraw(MnuButton& b);
    void key(const SDL_KeyboardEvent& k);
    void textInput(const char* t);
    void startEdit(MnuButton& b, int max);
    void sound(const char* wav, int volume = 255);

    // Screens.
    void refreshSoldier();
    void refreshCampaign();
    void refreshOptions();
    bool missionAvailable(int camp, int mis);
    uint32_t missionHeader(int camp, int mis, int offset);
    void acceptMission();
    void startMission();

    Vfs& vfs_;
    FrontendOptions o_;
    SDL_Window* win_ = nullptr;
    SDL_GLContext ctx_ = nullptr;
    std::unique_ptr<Hud> hudPtr_;
    Hud& hud() { return *hudPtr_; }
    Audio audio_;
    Music music_;
    DfConfig cfg_;              // df.cfg (written at exit, 0x40dc88 from 0x4016c7)
    std::string dragging_;      // the slider being dragged (0x4c1b64)
    void refreshSettings();
    void refreshMacros();
    void profileDefaults(Profile& p);
    void syncSliders();
    bool musicLoaded_ = false;
    void musicVar(int n, int v) { music_.setVar(n, v); }
    float scale_ = 1, offX_ = 0, offY_ = 0;
    int width_ = 0, height_ = 0;

    Mnu mnu_;
    TextTable menuText_, campsText_, campText_;
    std::string mnuSource_;
    std::map<std::string, HudImage> images_;
    std::map<std::string, MenuFont> fonts_;
    std::array<Profile, 9> profiles_;
    int cur_ = 0;

    Screen screen_ = Start, next_ = Start;
    std::string menu_;
    std::string hover_, pressed_;
    int mouseX_ = 0, mouseY_ = 0;
    std::string editing_, editBackup_;
    int editMax_ = 16, editMode_ = 0;   // 1 delete-recreate, 2 rename
    bool deleting_ = false;
    Screen prevScreen_ = Start;
    int ticks_ = 0, screenTicks_ = 0;
    // Campaign / briefing / loadout state.
    int camp_ = 1, mis_ = 0, lastMapClick_ = -100;
    std::string briefing_, caption_;
    int briefLine_ = 0;
    int loTab_ = 0, loSel_[3] = {0, 0, 0};
    MissionOutcome outcome_;
    int quitCode_ = 0;
    uint32_t options_ = 0;   // df.cfg attributes (Enemy AI 0x80, Player Health 0x100, ...)
    int gpsIcons_ = 1;
};

bool Frontend::openWindow() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    win_ = SDL_CreateWindow("OpenDF", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, o_.width, o_.height,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!win_) return false;
    ctx_ = SDL_GL_CreateContext(win_);
    const char* missing = nullptr;
    if (!ctx_ || !loadGl(&missing)) return false;
    hudPtr_ = std::make_unique<Hud>();
    SDL_GL_SetSwapInterval(1);
    std::string err;
    if (!hud().init(vfs_, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return false; }
    images_.clear();
    fonts_.clear();
    SDL_ShowCursor(SDL_DISABLE);
    return true;
}

void Frontend::closeWindow() {
    hudPtr_.reset();
    images_.clear();
    fonts_.clear();
    if (ctx_) SDL_GL_DeleteContext(ctx_);
    if (win_) SDL_DestroyWindow(win_);
    ctx_ = nullptr;
    win_ = nullptr;
    SDL_ShowCursor(SDL_ENABLE);
}

bool Frontend::loadData() {
    std::vector<uint8_t> raw;
    if (vfs_.read("DFMENU.BIN", raw)) menuText_.parse(raw);
    if (vfs_.read("DFCAMPS.BIN", raw)) campsText_.parse(raw);
    if (!vfs_.read("DF.MNU", raw)) return false;
    mnuSource_.assign(raw.begin(), raw.end());
    return mnu_.parse(mnuSource_, menuText_);
}

void Frontend::loadProfiles() {
    // 0x40f40c: nine files; an absent or unused one is zeroed with defaults;
    // with no soldier at all slot 0 becomes "BLACKHAWK" (NSDELETEME).
    bool any = false;
    for (int i = 0; i < 9; ++i) {
        Profile& p = profiles_[size_t(i)];
        char name[32];
        std::snprintf(name, sizeof name, "player%02d.ply", i + 1);
        std::ifstream f(o_.gameDir / name, std::ios::binary);
        std::array<uint8_t, kProfileSize> buf{};
        if (f && f.read(reinterpret_cast<char*>(buf.data()), kProfileSize) && buf[0] == 'P' && buf[1] == 'L' && buf[2] == 'Y' &&
            buf[3] >= 0x1A) {
            p.d = buf;
        } else {
            profileDefaults(p);
        }
        if (!p.valid()) { profileDefaults(p); p.setName("New Operative"); }
        // Profiles written by OpenDF before it kept the message macros have
        // all ten empty; they get DF.EXE's defaults (DF.EXE never writes that).
        bool noMacros = true;
        for (int m = 0; m < 10; ++m) noMacros = noMacros && p.str(0x4b0 + 40 * m, 40).empty();
        if (noMacros) {
            Profile fresh;
            profileDefaults(fresh);
            std::memcpy(&p.d[0x4b0], &fresh.d[0x4b0], 400);
        }
        any = any || p.valid();
        std::memset(&p.d[0x14], 0, 16);
        std::memcpy(&p.d[0x14], name, std::strlen(name));
    }
    if (!any) {
        const std::string* n = menuText_.find("GroupMainMenu", "NSDELETEME");
        profiles_[0].setName(n ? *n : "BLACKHAWK");
        profiles_[0].set(0x24, 1);
    }
    cur_ = 0;
    for (int i = 0; i < 9; ++i)
        if (profiles_[size_t(i)].valid()) { cur_ = i; break; }
}

void Frontend::saveProfile(int i) {
    Profile& p = profiles_[size_t(i)];
    if (!p.valid()) return;
    char name[32];
    std::snprintf(name, sizeof name, "player%02d.ply", i + 1);
    std::ofstream f(o_.gameDir / name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(p.d.data()), kProfileSize);
}

HudImage Frontend::file(const std::string& pcx, bool transparent) {
    const std::string key = upperCase(pcx) + (transparent ? "#t" : "");
    if (auto it = images_.find(key); it != images_.end()) return it->second;
    HudImage im = hud().loadImage(vfs_, upperCase(pcx), transparent);
    images_[key] = im;
    return im;
}

HudImage Frontend::image(const std::string& name, bool transparent) {
    auto it = mnu_.images.find(upperCase(name));
    return it == mnu_.images.end() ? HudImage{} : file(it->second, transparent);
}

MenuFont& Frontend::font(const std::string& name) {
    const std::string key = upperCase(name);
    if (auto it = fonts_.find(key); it != fonts_.end()) return it->second;
    MenuFont& f = fonts_[key];
    auto fi = mnu_.fonts.find(key);
    std::vector<uint8_t> data;
    IndexedImage img;
    if (fi == mnu_.fonts.end() || !vfs_.read(upperCase(fi->second.file), data) || !decodePcx(data, img)) return f;
    const int W = img.width, H = img.height;
    auto px = [&](int x, int y) { return img.pixels[size_t(y) * size_t(W) + size_t(x)]; };
    std::vector<int> lines;
    for (int y = 0; y < H; ++y) {
        bool full = true;
        for (int x = 0; x < W && full; ++x) full = px(x, y) == 1;
        if (full) lines.push_back(y);
    }
    std::vector<uint8_t> rgba(size_t(W) * size_t(H) * 4, 0);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const uint8_t v = img.pixels[i];
        if (v >= 8 || v == 4) rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4 + 3] = 255;
    }
    // Bands repeat every `period` rows (the first separator line's y): band
    // k is above the separator at period * (k + 1); band 0 starts at the top.
    const int period = lines.empty() ? 0 : lines[0];
    for (int band = 0; period > 0 && band < 16 && period * (band + 1) <= H; ++band) {
        const int y0 = band == 0 ? 0 : period * band + 1, y1 = period * (band + 1);
        if (y1 <= y0) continue;
        if (!f.height) f.height = period - 1;
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
    }
    f.tex = hud().makeImage(W, H, rgba);
    f.ok = f.height > 0;
    return f;
}

float Frontend::textWidth(MenuFont& f, const std::string& s) {
    float w = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (static_cast<unsigned char>(s[i]) == 0xA0) { w += float(f.glyphs[' '].w + 1); continue; }
        if (s[i] == '{') { const size_t e = s.find('}', i); if (e != std::string::npos) { i = e; continue; } }
        w += float(f.glyphs[static_cast<unsigned char>(s[i])].w + 1);
    }
    return w;
}

void Frontend::blit(const HudImage& im, float x, float y, float sx, float sy, float sw, float sh, bool translucent) {
    if (!im.texture) return;
    if (translucent) hud().setTranslucent(true);
    hud().imageUV(im.texture, offX_ + x * scale_, offY_ + y * scale_, sw * scale_, sh * scale_, sx / float(im.width), sy / float(im.height),
                 (sx + sw) / float(im.width), (sy + sh) / float(im.height));
    if (translucent) hud().setTranslucent(false);
}

void Frontend::drawText(const std::string& fontName, float x, float y, const std::string& text, uint32_t color, int align) {
    // Link words are joined by non-breaking spaces (0xA0): drawn as spaces.
    std::string s = text;
    for (auto& ch : s)
        if (static_cast<unsigned char>(ch) == 0xA0) ch = ' ';
    MenuFont& f = font(fontName);
    if (!f.ok) return;
    const float w = textWidth(f, s);
    if (align == 1) x -= w / 2;
    else if (align == 2) x -= w;
    Color c = argb(color);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '{') {
            const size_t e = s.find('}', i);
            if (e != std::string::npos) {
                const std::string tag = upperCase(s.substr(i + 1, e - i - 1));
                if (tag.rfind("C:", 0) == 0)
                    if (auto it = mnu_.colors.find(tag.substr(2)); it != mnu_.colors.end()) c = argb(it->second);
                i = e;
                continue;
            }
        }
        const Glyph& g = f.glyphs[static_cast<unsigned char>(s[i])];
        if (g.w > 0 && s[i] != ' ')
            hud().imageUV(f.tex.texture, offX_ + x * scale_, offY_ + y * scale_, float(g.w) * scale_, float(g.h) * scale_,
                         float(g.x) / float(f.tex.width), float(g.y) / float(f.tex.height), float(g.x + g.w) / float(f.tex.width),
                         float(g.y + g.h) / float(f.tex.height), c);
        x += float(g.w + 1);
    }
}

int Frontend::drawWrapped(const std::string& fontName, float x, float y, float w, float h, const std::string& s, uint32_t color,
                          int startLine) {
    MenuFont& f = font(fontName);
    if (!f.ok) return 0;
    const float lh = float(f.height);
    float cx = x, cy = y;
    int line = 0;
    std::istringstream words(s);
    std::string word;
    auto newline = [&]() { ++line; cx = x; if (line > startLine) cy += lh; };
    while (words >> word) {
        if (word[0] == '{' && upperCase(word).rfind("{C:", 0) != 0) {
            const size_t e = word.find('}');
            word = e == std::string::npos ? std::string() : word.substr(e + 1);
            if (word.empty()) continue;
        }
        if (word == "~") { newline(); continue; }
        const float ww = textWidth(f, word + " ");
        if (cx + ww > x + w && cx > x) newline();
        if (cy + lh > y + h) break;
        if (line >= startLine) drawText(fontName, cx, cy, word, color);
        cx += ww;
    }
    return line + 1;
}

void Frontend::sound(const char* wav, int volume) {
    // Menu sounds play at fx_volume (0x404d04, 0x407124).
    volume = volume * std::clamp(cfg_.get("fx_volume"), 0, 255) / 255;
    if (const SoundClip* c = audio_.clip(vfs_, wav)) {
        const float g = float(volume) / 255.f * 0.5f;
        audio_.play(c, g, g);
    }
}

void Frontend::setMenu(const std::string& name) {
    menu_ = upperCase(name);
    hover_.clear();
    pressed_.clear();
    editing_.clear();
    // Fresh drawstates from the file.
    Mnu fresh;
    fresh.parse(mnuSource_, menuText_);
    if (const MnuMenu* m = fresh.menu(menu_))
        for (const auto& bn : m->buttons)
            if (MnuButton* src = fresh.button(bn); src && mnu_.button(bn)) *mnu_.button(bn) = *src;
}

void Frontend::enter(Screen s) {
    if (screen_ == Macros && s != Macros) saveProfile(cur_);   // Macros exit hook 0x420ed4
    prevScreen_ = screen_;
    screen_ = s;
    screenTicks_ = 0;
    switch (s) {
    case Start: menu_.clear(); break;
    case Soldier:
        // After the logo (0x419b51): music_start, Var00=0 (Begin plays Intro).
        if (screen_ == Soldier && prevScreen_ == Start) { music_.start(); musicVar(0, 0); }
        musicVar(1, 4);   // 0x40ea5f
        setMenu("NewSoldierMenu");
        deleting_ = false;
        editMode_ = 0;
        refreshSoldier();
        break;
    case Main:
        musicVar(1, 0);   // 0x40cc54
        setMenu("menu1");
        saveProfile(cur_);   // saved whenever the main menu opens (0x40f718)
        if (MnuButton* b = mnu_.button("but_mainmp")) b->drawstate = 4;   // multiplayer: not in OpenDF
        if (MnuButton* b = mnu_.button("but_mainup")) b->drawstate = 4;   // online update: not in OpenDF
        break;
    case Options1: {
        // 0x40aed0: the sliders take their df.cfg values, then the value boxes.
        musicVar(1, 3);   // 0x40af83
        setMenu("OptionsMenu");
        for (auto [n, k] : {std::pair{"but_op_mvsl", "cd_volume"}, {"but_op_sfsl", "fx_volume"}, {"but_op_dvsl", "dialog_volume"}, {"but_op_mssl", "mousescale"}})
            if (MnuButton* x = mnu_.button(n)) x->sliderPos = cfg_.get(k);
        refreshSettings();
        break;
    }
    case Options3: musicVar(1, 11); setMenu("Options3Menu"); refreshOptions(); break;   // 0x41c47c
    case Macros:
        setMenu("MsgMacMenu");
        refreshMacros();
        musicVar(1, 12);   // 0x420e2c
        break;
    case Campaign:
        musicVar(1, 1);   // 0x4114b8
        setMenu("CampMenu");
        camp_ = std::clamp(int(prof().get(0x28)), 1, 5);
        prof().set(0x28, camp_);
        refreshCampaign();
        break;
    case Briefing: {
        musicVar(1, 6);   // 0x41243a
        setMenu("MissionMenu");
        // 0x412620: section Briefing<m>, key Mission<m>a; {ADD}, {CV:n}, {CV:!n}.
        char sec[32], key[32];
        std::snprintf(sec, sizeof sec, "Briefing%d", mis_ + 1);
        std::snprintf(key, sizeof key, "Mission%da", mis_ + 1);
        const std::string* t = campText_.find(sec, key);
        briefing_ = t ? *t : "UNABLE TO FIND MISSION BRIEFING";
        const uint32_t flags = uint32_t(prof().get(0x70 + camp_ * 0x44));
        for (int guard = 0; guard < 64; ++guard) {
            size_t a = briefing_.find("{ADD}"), c = briefing_.find("{CV:");
            if (a == std::string::npos && c == std::string::npos) break;
            bool include = true;
            size_t tagEnd;
            if (c != std::string::npos && (a == std::string::npos || c < a)) {
                a = c;
                tagEnd = briefing_.find('}', c);
                if (tagEnd == std::string::npos) break;
                const std::string cond = briefing_.substr(c + 4, tagEnd - c - 4);
                const bool neg = !cond.empty() && cond[0] == '!';
                const int n = std::atoi(cond.c_str() + (neg ? 1 : 0));
                const bool set = n >= 0 && n < 32 && (flags >> n & 1);
                include = neg ? !set : set;
                ++tagEnd;
            } else {
                tagEnd = a + 5;
            }
            size_t e = tagEnd;
            while (e < briefing_.size() && briefing_[e] != ' ') ++e;
            const std::string* more = include ? campText_.find(sec, briefing_.substr(tagEnd, e - tagEnd)) : nullptr;
            briefing_.replace(a, e - a, more ? *more : "");
        }
        briefLine_ = 0;
        caption_.clear();
        break;
    }
    case Loadout:
        musicVar(1, 7);   // 0x41fd58
        setMenu("LoadoutMenu");
        loSel_[0] = std::clamp(int(prof().get(0x698)), 0, 4);
        loSel_[1] = std::clamp(int(prof().get(0x69C)), 0, 3);
        loSel_[2] = std::clamp(int(prof().get(0x694)), 0, 1);
        loTab_ = 0;
        if (MnuButton* b = mnu_.button("but_lo_w1")) b->drawstate = 3;
        break;
    case Debrief:
        setMenu("DebfMenu");
        saveProfile(cur_);
        break;
    case End:
        music_.stop();    // 0x41cadf
        setMenu("EndMenu");
        for (int i = 0; i < 9; ++i) saveProfile(i);
        break;
    default: break;
    }
}

MnuButton* Frontend::hit(int x, int y) {
    const MnuMenu* m = mnu_.menu(menu_);
    if (!m) return nullptr;
    for (const auto& bn : m->buttons) {
        MnuButton* b = mnu_.button(bn);
        if (!b || b->type == 0 || b->drawstate == 0 || (b->attrib & 0x200)) continue;
        int w = b->width, h = b->height;
        if (!b->image.empty()) {
            const HudImage im = image(b->image);
            if (b->type == 1) { w = im.width; h = im.height / 4; }
            else if (b->type == 6) { w = im.width; h = im.height; }
            else if (b->type == 2) h = im.height;   // 0x405774: H slider = knob height
        } else if (b->type == 1 && !b->text.empty() && !w) {
            w = int(textWidth(font(b->font), b->text));
            h = font(b->font).height;
        }
        int bx = b->x;
        if (b->image.empty() && !(b->attrib & 0x28)) bx = (b->attrib & 0x10) ? b->x - w : b->x - w / 2;
        if (x >= bx && x < bx + w && y >= b->y && y < b->y + h) return b;
    }
    return nullptr;
}

void Frontend::drawButton(MnuButton& b) {
    if (b.drawstate == 0) return;
    if (b.attrib & 0x20) ownerDraw(b);
    if (!b.image.empty() && b.type == 1) {
        const HudImage im = image(b.image);
        if (b.attrib & 0x800) blit(im, float(b.x), float(b.y), 0, 0, float(im.width), float(im.height), !(b.attrib & 0x1000));
        else {
            const float ch = float(im.height) / 4.f;
            const int cell = std::clamp(b.drawstate - 1, 0, 3);
            blit(im, float(b.x), float(b.y), 0, ch * float(cell), float(im.width), ch, true);
        }
    } else if (!b.image.empty() && b.type == 2 && !(b.attrib & 2) && b.drawstate != 4) {
        // Knob at x + pos * width / size (0x40886c).
        const HudImage im = image(b.image);
        const float kx = float(b.x) + (b.sliderSize > 0 ? float(b.sliderPos) * float(b.width) / float(b.sliderSize) : 0.f);
        blit(im, kx, float(b.y), 0, 0, float(im.width), float(im.height), false);
    } else if (!b.image.empty() && b.type == 6 && !(b.attrib & 0x20)) {
        const HudImage im = image(b.image);
        blit(im, float(b.x), float(b.y), 0, 0, float(im.width), float(im.height), false);
    }
    if ((b.attrib & 0x40) && !b.text.empty()) {
        const auto fit = mnu_.fonts.find(upperCase(b.font));
        const MnuFont mf = fit == mnu_.fonts.end() ? MnuFont{} : fit->second;
        uint32_t c = mf.fg[0];
        if (b.drawstate == 4) c = mf.fg[2];
        else if (b.drawstate == 2 || hover_ == b.name) c = mf.fg[1];
        float ax = float(b.x), ay = float(b.y);
        int align = (b.attrib & 0x08) ? 0 : (b.attrib & 0x10) ? 2 : 1;
        if (b.attrib & 0x80) {
            int w = b.width, h = b.height;
            if (!b.image.empty()) { const HudImage im = image(b.image); w = im.width; h = im.height / 4; }
            ax = (b.attrib & 0x08) ? float(b.x + 6) : (b.attrib & 0x10) ? float(b.x + w - 6) : float(b.x) + float(w) / 2;
            ay += float(h - font(b.font).height) / 2;
        }
        std::string t = b.text;
        if (editing_ == b.name && (ticks_ & 0x20)) t += '_';
        drawText(b.font, ax, ay, t, c, align);
        if (b.hot && b.hotPos >= 0 && editing_ != b.name) {
            MenuFont& f = font(b.font);
            float x0 = ax - (align == 1 ? textWidth(f, b.text) / 2 : align == 2 ? textWidth(f, b.text) : 0);
            drawText(b.font, x0 + textWidth(f, b.text.substr(0, size_t(b.hotPos))), ay + 1, "_", c);
        }
    }
}

void Frontend::drawMenu() {
    if (screen_ == Start) {
        // The splash (DF_LEAD.PCX), then on to the soldier screen.
        blit(file("DF_LEAD.PCX"), 0, 0, 0, 0, 640, 480, false);
        return;
    }
    const MnuMenu* m = mnu_.menu(menu_);
    if (!m) return;
    if (!m->background.empty()) blit(image(m->background), 0, 0, 0, 0, 640, 480, false);
    for (const auto& bn : m->buttons)
        if (MnuButton* b = mnu_.button(bn)) drawButton(*b);
    // The cursor (0x408df8): ImageCursor1 centred on the mouse.
    const HudImage cur = image("ImageCursor1", true);
    blit(cur, float(mouseX_ - cur.width / 2), float(mouseY_ - cur.height / 2), 0, 0, float(cur.width), float(cur.height), false);
}

void Frontend::standard(MnuButton& b, int ev, const std::function<void()>& action) {
    // The common pattern: press -> menubtn1.wav and pushed; release ->
    // normal and the action; hover lit / unlit.
    if (ev == 1) { sound("MENUBTN1.WAV"); b.drawstate = 3; }
    else if (ev == 2) { b.drawstate = 1; action(); }
    else if (ev == 6) b.drawstate = 2;
    else if (ev == 7) b.drawstate = 1;
}

bool Frontend::missionAvailable(int camp, int mis) {
    // 0x4109e0: BMS header +0x7C == 0 or all its bits in the campaign flags.
    const uint32_t req = missionHeader(camp, mis, 0x7C);
    const uint32_t flags = uint32_t(prof().get(0x70 + camp * 0x44));
    return req == 0 || (flags & req) == req;
}

uint32_t Frontend::missionHeader(int camp, int mis, int offset) {
    char name[32];
    std::snprintf(name, sizeof name, "C%02dM%02d.BMS", camp + 1, mis + 1);
    std::vector<uint8_t> d;
    if (!vfs_.read(name, d) || size_t(offset + 4) > d.size()) return 0;
    uint32_t v;
    std::memcpy(&v, &d[size_t(offset)], 4);
    return v;
}

void Frontend::refreshSoldier() {
    // Row labels: the soldier's name or "New Operative"; the current row pushed.
    for (int i = 0; i < 9; ++i) {
        char n[32];
        std::snprintf(n, sizeof n, "but_ns_%dtxt", i + 1);
        if (MnuButton* b = mnu_.button(n)) b->text = profiles_[size_t(i)].name();
        for (const char* suffix : {"", "l"}) {
            std::snprintf(n, sizeof n, "but_ns_%d%s", i + 1, suffix);
            if (MnuButton* b = mnu_.button(n)) b->drawstate = i == cur_ ? 3 : 1;
        }
    }
    const bool valid = prof().valid();
    if (MnuButton* b = mnu_.button("but_ns_actxt")) b->drawstate = valid ? 1 : 4;
    for (const char* n : {"but_ns_plt", "but_ns_prt", "but_ns_ed", "but_ns_de"})
        if (MnuButton* b = mnu_.button(n)) b->drawstate = valid && !deleting_ ? 1 : 0;
    for (const char* n : {"but_ns_dey", "but_ns_den"})
        if (MnuButton* b = mnu_.button(n)) b->drawstate = deleting_ ? 1 : 0;
}

void Frontend::refreshOptions() {
    // 0x41c150: the value boxes of the Difficulty page.
    auto t = [&](const char* k) { const std::string* x = menuText_.find("GroupMainMenu", k); return x ? *x : std::string(k); };
    auto put = [&](const char* b, const std::string& v) {
        if (MnuButton* x = mnu_.button(b)) { x->text = v; x->attrib |= 0xC0; }
    };
    put("but_opt3_eaiw", t(options_ & 0x80 ? "OPT3HARD" : "OPT3NORMAL"));
    put("but_opt3_phw", t(options_ & 0x100 ? "OPT3EASY" : "OPT3NORMAL"));
    put("but_opt3_smw", t(options_ & 0x200 ? "OPT3OFF" : "OPT3ON"));
    put("but_opt3_trw", t(options_ & 0x1000 ? "OPT3OFF" : "OPT3ON"));
    put("but_opt3_bxw", t(options_ & 0x2000 ? "OPT3OFF" : "OPT3ON"));
    put("but_opt3_pww", t(options_ & 0x800 ? "OPT3ON" : "OPT3OFF"));
    put("but_opt3_gpsw", t(gpsIcons_ == 0 ? "OPT3GPSSHOWFRIENDLY" : gpsIcons_ == 1 ? "OPT3GPSDEFAULT" : "OPT3GPSSHOWALL"));
}

void Frontend::profileDefaults(Profile& p) {
    // 0x40ee14 loadout etc., then the message macros DEFAULTMACRO1..10 from
    // DFMENU.BIN "Macros" into +0x4b0, 40 bytes each (0x40f6ac).
    p.defaults();
    for (int i = 0; i < 10; ++i) {
        char k[32];
        std::snprintf(k, sizeof k, "DEFAULTMACRO%d", i + 1);
        if (const std::string* t = menuText_.find("Macros", k)) p.setStr(0x4b0 + 40 * i, 40, *t);
    }
}

void Frontend::refreshMacros() {
    // 0x420d80: the ten boxes show profile +0x4b0 + 40 i.
    for (int i = 0; i < 10; ++i) {
        char n[32];
        std::snprintf(n, sizeof n, "but_mm_mm%d", i + 1);
        if (MnuButton* b = mnu_.button(n)) { b->text = prof().str(0x4b0 + 40 * i, 40); b->attrib |= 0x40; }
    }
}

void Frontend::refreshSettings() {
    // 0x40b1cc: the value boxes of the General Settings page.
    auto t = [&](const char* k) { const std::string* x = menuText_.find("GroupMainMenu", k); return x ? *x : std::string(k); };
    auto put = [&](const char* b, const std::string& v) {
        if (MnuButton* x = mnu_.button(b)) { x->text = v; x->attrib |= 0xC0; }
    };
    put("but_op_fmw", t(cfg_.get("flipmouse") ? "OMYES" : "OMNO"));
    put("but_op_chw", t(cfg_.get("crosshairs") ? "OMYES" : "OMNO"));
    const int r = cfg_.get("renderer");
    if (r >= 0 && r <= 2) put("but_op_cow", t(r == 0 ? "OM256" : r == 1 ? "OM16MILLION" : "OM65THOUSAND"));
    static const char* kRes[] = {"OM320BY240", "OM400BY300", "OM512BY382", "OM640BY480", "OM800BY600", "OM1024BY768"};
    const int g = cfg_.get("game_res");
    if (g >= 0 && g <= 5) put("but_op_rew", t(kRes[g]));
    put("but_op_wiw", t(cfg_.get("windowed") ? "OMYES" : "OMNO"));
    put("but_op_tuw", t(cfg_.get("turbo") ? "OMON" : "OMOFF"));
    put("but_op_mew", t(cfg_.get("cd_play") ? "OMYES" : "OMNO"));
}

void Frontend::syncSliders() {
    // The sliders' owner draw (event 3, every frame) copies their positions
    // into df.cfg (0x40b6c4, 0x40ba20, 0x40bb6a).
    for (auto [n, k] : {std::pair{"but_op_mvsl", "cd_volume"}, {"but_op_sfsl", "fx_volume"}, {"but_op_dvsl", "dialog_volume"}, {"but_op_mssl", "mousescale"}})
        if (MnuButton* x = mnu_.button(n)) {
            x->sliderPos = std::clamp(x->sliderPos, 0, 255);
            cfg_.set(k, x->sliderPos);
        }
    music_.setVolume(cfg_.get("cd_volume"));
}

void Frontend::refreshCampaign() {
    std::vector<uint8_t> raw;
    char file[32];
    std::snprintf(file, sizeof file, "DFCAMP%02d.BIN", camp_ + 1);
    campText_ = TextTable{};
    if (vfs_.read(file, raw)) campText_.parse(raw);
    mis_ = std::clamp(int(prof().get(0x470 + 4 * camp_)), 0, kMissionCount[camp_] - 1);
    for (int i = 1; i <= 5; ++i) {
        char n[32], k[32];
        std::snprintf(n, sizeof n, "but_ca_%d", i + 1);
        std::snprintf(k, sizeof k, "STR_C%02iNAME", i + 1);
        if (MnuButton* b = mnu_.button(n)) {
            if (const std::string* t = menuText_.find("Campaign Names", k)) { b->text = *t; b->attrib |= 0xC0; }
            b->drawstate = i == camp_ ? 3 : 1;
        }
    }
}

void Frontend::acceptMission() {
    // 0x411914: the mission's default gear from BMS header +0x78..+0x7A (a
    // clear bit picks the item; later bits win).
    const uint32_t g = missionHeader(camp_, mis_, 0x78);
    const int prim[5] = {0, 4, 1, 2, 3}, sec[4] = {0, 1, 2, 3}, side[2] = {0, 1};
    for (int b = 0; b < 5; ++b) if (!(g >> b & 1)) prof().set(0x698, prim[b]);
    for (int b = 0; b < 4; ++b) if (!(g >> (8 + b) & 1)) prof().set(0x69C, sec[b]);
    for (int b = 0; b < 2; ++b) if (!(g >> (16 + b) & 1)) prof().set(0x694, side[b]);
    prof().set(0x684, kPrimary[std::clamp(int(prof().get(0x698)), 0, 4)].item);
    prof().set(0x688, kSecondary[std::clamp(int(prof().get(0x69C)), 0, 3)].item);
    prof().set(0x680, kSidearm[std::clamp(int(prof().get(0x694)), 0, 1)].item);
    next_ = Briefing;
}

void Frontend::startMission() { next_ = Mission; }

void Frontend::startEdit(MnuButton& b, int max) {
    editing_ = b.name;
    editBackup_ = b.text;
    b.text.clear();
    editMax_ = max;
    SDL_StartTextInput();
}

void Frontend::event(MnuButton& b, int ev) {
    const std::string cb = b.callback;
    auto to = [&](Screen s) { return [this, s] { next_ = s; }; };
    // Main menu.
    if (cb == "MNU_MAIN_NS") return standard(b, ev, to(Soldier));
    if (cb == "MNU_MAIN_SP") return standard(b, ev, to(Campaign));
    if (cb == "MNU_MAIN_OPTION") return standard(b, ev, to(Options1));
    if (cb == "MNU_MAIN_EXIT" || cb == "MNU_NS_EXIT" || cb == "MNU_CAMP_EXIT" || cb == "MNU_MIS_EXIT" || cb == "MNU_HOST_EXIT" ||
        cb == "MNU_MM_EXIT" || cb == "MNU_OPT1_EXIT" || cb == "MNU_OPT3_EXIT")
        return standard(b, ev, to(End));
    if (cb == "MNU_MAIN_CREDITS" || cb == "MNU_MAIN_MP" || cb == "MNU_MAIN_UPDATE") return standard(b, ev, [] {});
    if (cb == "MNU_END_ACCEPT") return standard(b, ev, to(Quit));
    // Choose Soldier.
    if (cb == "MNU_NS_NAMEBUTTON1") {
        const int row = std::atoi(b.name.c_str() + 7) - 1;   // BUT_NS_<n>[L]
        if (row < 0 || row > 8 || !editing_.empty()) return;
        if (ev == 6 && row != cur_) b.drawstate = 2;
        else if (ev == 7 && row != cur_) b.drawstate = 1;
        else if (ev == 2) {
            cur_ = row;
            deleting_ = false;
            refreshSoldier();
            if (!prof().valid()) {
                char n[32];
                std::snprintf(n, sizeof n, "but_ns_%dtxt", row + 1);
                editMode_ = 0;
                if (MnuButton* t = mnu_.button(n)) startEdit(*t, 16);
            }
        }
        return;
    }
    if (cb == "MNU_NS_ACCEPT")
        return standard(b, ev, [this] {
            if (!prof().valid()) return;
            for (int i = 0; i < 9; ++i) saveProfile(i);
            next_ = Main;
        });
    if (cb == "MNU_NS_EDIT")
        return standard(b, ev, [this] {
            char n[32];
            std::snprintf(n, sizeof n, "but_ns_%dtxt", cur_ + 1);
            editMode_ = 2;
            if (MnuButton* t = mnu_.button(n)) startEdit(*t, 16);
        });
    if (cb == "MNU_NS_DELETE") return standard(b, ev, [this] { deleting_ = true; refreshSoldier(); });
    if (cb == "MNU_NS_DELETENO") return standard(b, ev, [this] { deleting_ = false; refreshSoldier(); });
    if (cb == "MNU_NS_DELETEYES")
        return standard(b, ev, [this] {
            deleting_ = false;
            refreshSoldier();
            char n[32];
            std::snprintf(n, sizeof n, "but_ns_%dtxt", cur_ + 1);
            editMode_ = 1;
            if (MnuButton* t = mnu_.button(n)) startEdit(*t, 16);
        });
    if (cb == "MNU_NS_PICLEFT" || cb == "MNU_NS_PICRIGHT")
        return standard(b, ev, [this, cb] {
            const int f = int(prof().get(0x640)) + (cb == "MNU_NS_PICLEFT" ? -1 : 1);
            prof().set(0x640, (f + 11) % 11);
        });
    // Campaign.
    if (cb == "MNU_CAMP_SELECT") {
        if (ev == 1) {
            sound("MENUBTN1.WAV");
            camp_ = b.userid;
            prof().set(0x28, camp_);
            refreshCampaign();
        }
        return;
    }
    if (cb == "MNU_CAMP_ACCEPT") return standard(b, ev, [this] { acceptMission(); });
    if (cb == "MNU_CAMP_BACK" || cb == "MNU_OPT1_ACCEPT" || cb == "MNU_OPT3_ACCEPT") return standard(b, ev, to(Main));
    if (cb == "MNU_CAMP_MAP" && ev == 1) {
        const int mx = mouseX_ - b.x, my = mouseY_ - b.y;
        for (int i = 0; i < kMissionCount[camp_]; ++i) {
            if (!missionAvailable(camp_, i)) continue;
            if (std::abs(mx - kDots[camp_][i][0]) > 10 || std::abs(my - kDots[camp_][i][1]) > 10) continue;
            if (i == mis_ && ticks_ - lastMapClick_ <= 20) { acceptMission(); return; }   // double click
            lastMapClick_ = ticks_;
            mis_ = i;
            prof().set(0x470 + 4 * camp_, i);
            sound("BEEP2.WAV");
            return;
        }
        return;
    }
    // Briefing.
    if (cb == "MNU_MIS_ACCEPT") return standard(b, ev, [this] { startMission(); });
    if (cb == "MNU_MIS_BACK") return standard(b, ev, to(Campaign));
    if (cb == "MNU_MIS_LOADOUT") return standard(b, ev, to(Loadout));
    if (cb == "MNU_MIS_SCROLLBAR") {
        if (ev == 1 || ev == 2) {
            const std::string n = upperCase(b.name);
            if (n == "BUT_MI_UP") briefLine_ = std::max(0, briefLine_ - 1);
            else if (n == "BUT_MI_DN") ++briefLine_;
        }
        return;
    }
    // Change Gear.
    if (cb == "MNU_LO_WEAPON") {
        if (ev == 1) {
            for (const char* n : {"but_lo_w1", "but_lo_w2", "but_lo_w3"})
                if (MnuButton* t = mnu_.button(n)) t->drawstate = 1;
            b.drawstate = 3;
            loTab_ = std::clamp(b.userid, 0, 2);
            sound("MENUBTN1.WAV");
        }
        return;
    }
    if (cb == "MNU_LO_LEFT" || cb == "MNU_LO_RIGHT")
        return standard(b, ev, [this, cb] {
            const int n = loTab_ == 0 ? 5 : loTab_ == 1 ? 4 : 2;
            loSel_[loTab_] = (loSel_[loTab_] + (cb == "MNU_LO_LEFT" ? n - 1 : 1)) % n;
        });
    if (cb == "MNU_LO_BUTTON") {
        // Esc (the MENU's back button, not on screen; 0x420070): like ACCEPT
        // but without the press sound.
        if (ev == 1) { b.drawstate = 3; return; }
        if (ev != 2) return standard(b, ev, [] {});
        b.drawstate = 1;
        prof().set(0x698, loSel_[0]);
        prof().set(0x69C, loSel_[1]);
        prof().set(0x694, loSel_[2]);
        prof().set(0x684, kPrimary[loSel_[0]].item);
        prof().set(0x688, kSecondary[loSel_[1]].item);
        prof().set(0x680, kSidearm[loSel_[2]].item);
        next_ = Briefing;
        return;
    }
    if (cb == "MNU_LO_ACCEPT" || cb == "MNU_LO_BACK")
        return standard(b, ev, [this] {
            prof().set(0x698, loSel_[0]);
            prof().set(0x69C, loSel_[1]);
            prof().set(0x694, loSel_[2]);
            prof().set(0x684, kPrimary[loSel_[0]].item);
            prof().set(0x688, kSecondary[loSel_[1]].item);
            prof().set(0x680, kSidearm[loSel_[2]].item);
            next_ = Briefing;
        });
    // Debriefing.
    if (cb == "MNU_DEBF_ACCEPT") return standard(b, ev, to(Campaign));
    // General Settings (0x40b518..0x40bb80).
    if (cb == "MNU_OPT1_FLIPMOUSE" || cb == "MNU_OPT1_CROSSHAIRS" || cb == "MNU_OPT1_WINDOWED" || cb == "MNU_OPT1_TURBO" ||
        cb == "MNU_OPT1_MUSICENABLED" || cb == "MNU_OPT1_RESOLUTION" || cb == "MNU_OPT1_RENDERER") {
        // Toggles and lists change on press (drawstate 3), then the boxes refresh.
        if (ev == 1) {
            b.drawstate = 3;
            const bool left = upperCase(b.name).back() == 'L';
            auto flip = [&](const char* k) { cfg_.set(k, cfg_.get(k) ^ 1); };
            if (cb == "MNU_OPT1_FLIPMOUSE") flip("flipmouse");
            else if (cb == "MNU_OPT1_CROSSHAIRS") flip("crosshairs");
            else if (cb == "MNU_OPT1_WINDOWED") flip("windowed");
            else if (cb == "MNU_OPT1_TURBO") flip("turbo");
            else if (cb == "MNU_OPT1_MUSICENABLED") flip("cd_play");
            else if (cb == "MNU_OPT1_RESOLUTION") {
                // 0x40b733: +-1, below 0 -> max_res, above max_res -> 0.
                int r = cfg_.get("game_res") + (left ? -1 : 1);
                if (r < 0) r = cfg_.get("max_res");
                if (r > cfg_.get("max_res")) r = 0;
                cfg_.set("game_res", r);
            } else {
                // 0x40b7b4: steps through the renderers DF.EXE found
                // (0x4f5140); OpenDF offers all three.
                cfg_.set("renderer", (cfg_.get("renderer") + (left ? 2 : 1)) % 3);
            }
            refreshSettings();
        } else if (ev == 2 || ev == 7) b.drawstate = 1;
        else if (ev == 6) b.drawstate = 2;
        return;
    }
    if (cb == "MNU_OPT1_MOUSESENSITIVITY" || cb == "MNU_OPT1_SFXVOLUME" || cb == "MNU_OPT1_MUSICVOLUME" || cb == "MNU_OPT1_DIALOGVOLUME") {
        // Arrows: click9 at fx/2, pushed, value -+10 clamped to 0..255, the
        // slider follows (0x40b5a8, 0x40ba68).
        if (ev == 1) {
            sound("CLICK9.WAV", 128);
            b.drawstate = 3;
            const char* key = cb == "MNU_OPT1_MOUSESENSITIVITY" ? "mousescale" : cb == "MNU_OPT1_SFXVOLUME" ? "fx_volume"
                            : cb == "MNU_OPT1_MUSICVOLUME" ? "cd_volume" : "dialog_volume";
            const char* slider = cb == "MNU_OPT1_MOUSESENSITIVITY" ? "but_op_mssl" : cb == "MNU_OPT1_SFXVOLUME" ? "but_op_sfsl"
                               : cb == "MNU_OPT1_MUSICVOLUME" ? "but_op_mvsl" : "but_op_dvsl";
            const bool left = upperCase(b.name).back() == 'L';
            const int v = std::clamp(cfg_.get(key) + (left ? -10 : 10), 0, 255);
            cfg_.set(key, v);
            if (MnuButton* x = mnu_.button(slider)) x->sliderPos = v;
            if (cb == "MNU_OPT1_MUSICVOLUME") music_.setVolume(v);
        } else if (ev == 2 || ev == 7) b.drawstate = 1;
        else if (ev == 6) b.drawstate = 2;
        return;
    }
    if (cb == "MNU_OPT1_MSGMAC" || cb == "MNU_OPT3_MSGMAC") return standard(b, ev, to(Macros));
    if (cb == "MNU_OPT1_KBDREMAP") return standard(b, ev, [] {});   // the keyboard page is not in OpenDF yet
    // Message Macros (0x420ee4..0x421234).
    if (cb == "MNU_MM_ACCEPT") return standard(b, ev, to(Main));
    if (cb == "MNU_MM_OPTIONS") return standard(b, ev, to(Options1));
    if (cb == "MNU_MM_DIFFICULTY") return standard(b, ev, to(Options3));
    if (cb == "MNU_MM_MMBOX") {
        // 0x420fa0: press pushes; release edits but_mm_mm<n> (40 characters);
        // Enter stores the text in the profile, Esc restores; both reset
        // the dot and the box.
        char box[32], dot[32];
        std::snprintf(box, sizeof box, "but_mm_mm%d", b.userid + 1);
        std::snprintf(dot, sizeof dot, "but_mm_mm%dd", b.userid + 1);
        MnuButton* bx = mnu_.button(box);
        MnuButton* dt = mnu_.button(dot);
        if (ev == 1) b.drawstate = 3;
        else if (ev == 2) {
            b.drawstate = 1;
            if (bx && upperCase(editing_) != upperCase(bx->name)) startEdit(*bx, 40);
        } else if (ev == 6) { if (b.drawstate == 1) b.drawstate = 2; }
        else if (ev == 7) { if (b.drawstate == 2) b.drawstate = 1; }
        else if (ev == 4 || ev == 5) {
            if (ev == 4 && bx) prof().setStr(0x4b0 + 40 * std::clamp(b.userid, 0, 9), 40, bx->text);
            if (dt) dt->drawstate = 1;
            if (bx) bx->drawstate = 1;
            refreshMacros();
        }
        return;
    }
    // Settings tabs: General <-> Difficulty (the keyboard and macro pages are not in OpenDF yet).
    if (cb == "MNU_OPT1_DIFFICULTY") return standard(b, ev, to(Options3));
    if (cb == "MNU_OPT3_OPTIONS") return standard(b, ev, to(Options1));
    // Difficulty rows (0x41c150): each arrow toggles.
    auto toggle = [&](uint32_t bit) { return [this, bit] { options_ ^= bit; refreshOptions(); }; };
    if (cb == "MNU_OPT3_ENEMYAI") return standard(b, ev, toggle(0x80));
    if (cb == "MNU_OPT3_PLAYERHEALTH") return standard(b, ev, toggle(0x100));
    if (cb == "MNU_OPT3_SQUADMEMBERS") return standard(b, ev, toggle(0x200));
    if (cb == "MNU_OPT3_TRACERS") return standard(b, ev, toggle(0x1000));
    if (cb == "MNU_OPT3_BREAKX") return standard(b, ev, toggle(0x2000));
    if (cb == "MNU_OPT3_POWERUPS") return standard(b, ev, toggle(0x800));
    if (cb == "MNU_OPT3_GPSICON") return standard(b, ev, [this, &b] { gpsIcons_ = (gpsIcons_ + (b.userid ? 1 : 2)) % 3; refreshOptions(); });
    if (!cb.empty()) standard(b, ev, [] {});
}

void Frontend::ownerDraw(MnuButton& b) {
    const std::string cb = upperCase(b.userdraw.empty() ? b.callback : b.userdraw);
    const uint32_t green = 0xFF00FF00;
    if (cb == "MNU_NS_RIGHTBOX") {
        blit(image(b.image), float(b.x), float(b.y), 0, 0, 156, 175, false);
        if (!prof().valid() || !editing_.empty()) return;
        // The face (0x410498): face01..08, female01..03, 128x128 at +15, +14.
        const int f = std::clamp(int(prof().get(0x640)), 0, 10);
        char name[32];
        if (f < 8) std::snprintf(name, sizeof name, "FACE%02d.PCX", f + 1);
        else std::snprintf(name, sizeof name, "FEMALE%02d.PCX", f - 7);
        blit(file(name), float(b.x + 15), float(b.y + 14), 0, 0, 128, 128, false);
        return;
    }
    if (cb == "MNU_NS_STATUSBOX") {
        // 0x40ee6c: name ~ campaign kills ~ completed missions (left).
        blit(image(b.image), float(b.x), float(b.y), 0, 0, 520, 61, false);
        auto t = [&](const char* k) { const std::string* s = menuText_.find("GroupMainMenu", k); return s ? *s : std::string(k); };
        std::string left;
        if (deleting_) left = t("NSDELETEDIALOG");
        else if (!editing_.empty()) left = t("NSCREATEHELP");
        else if (prof().valid()) {
            int done = 0;
            for (int c = 1; c <= 5; ++c)
                for (int i = 0; i < 16; ++i) done += prof().get(0x30 + c * 0x44 + 4 * i) != 0;
            char buf[256];
            std::snprintf(buf, sizeof buf, "%s: %s ~ %s: %d ~ %s: %d", t("NSOPERATIVENAME").c_str(), prof().name().c_str(),
                          t("NSSPKILLS").c_str(), int(prof().get(0x664)), t("NSSPSPK").c_str(), done);
            left = buf;
        }
        drawWrapped("LCD10", float(b.x + 8), float(b.y + 6), 250, 50, left, green);
        return;
    }
    if (cb == "MNU_CAMP_MAP") {
        // The campaign map with a dot per available mission (0x410be8).
        blit(image(b.image), float(b.x), float(b.y), 0, 0, 256, 256, false);
        char n[32];
        std::snprintf(n, sizeof n, "ImageCampMap%d", camp_);
        blit(image(n), float(b.x), float(b.y), 0, 0, 256, 256, false);
        for (int i = 0; i < kMissionCount[camp_]; ++i) {
            if (!missionAvailable(camp_, i)) continue;
            const int dx = kDots[camp_][i][0], dy = kDots[camp_][i][1];
            const bool done = prof().get(0x30 + camp_ * 0x44 + 4 * i) != 0;
            const bool hov = std::abs(mouseX_ - b.x - dx) <= 10 && std::abs(mouseY_ - b.y - dy) <= 10;
            const char* dot = done ? "ImageCampMapFinsDot" : i == mis_ ? "ImageCampMapSeltDot" : hov ? "ImageCampMapPassDot" : "ImageCampMapRegDot";
            blit(image(dot), float(b.x + dx - 5), float(b.y + dy - 5), 0, 0, 11, 11, false);
            char key[32];
            std::snprintf(key, sizeof key, "STR_VC%02dM%02d", camp_ + 1, i + 1);
            const std::string* nm = campText_.find("Mission Names", key);
            const uint32_t c = i == mis_ ? 0xFFAAFFAA : hov ? 0xFFEB8200 : green;
            if (nm) drawText("LCD7", float(b.x + dx), float(b.y + dy - 15), *nm, c, 1);
        }
        return;
    }
    if (cb == "MNU_CAMP_TEXTBOX") {
        // Overview (0x410df4): DFCAMPS.BIN "Mission Statements", C<c>M<m>.
        blit(image(b.image), float(b.x), float(b.y), 0, 0, 256, 84, false);
        char key[32];
        std::snprintf(key, sizeof key, "C%dM%d", camp_ + 1, mis_ + 1);
        if (const std::string* t = campsText_.find("Mission Statements", key))
            drawWrapped("LCD10", float(b.x + 6), float(b.y + 6), 244, 74, *t, green);
        return;
    }
    if (cb == "MNU_MIS_PL1") {
        // The mission map C<c>M<m>SM.PCX (APPROXIMATION: shown whole, no
        // scrolling or route marks yet).
        char n[32];
        std::snprintf(n, sizeof n, "C%dM%dSM.PCX", camp_ + 1, mis_ + 1);
        blit(file(n), float(b.x), float(b.y), 0, 0, 256, 256, false);
        return;
    }
    if (cb == "MNU_MIS_PL2") {
        blit(image(b.image), float(b.x), float(b.y), 0, 0, 228, 286, false);
        const int lines = drawWrapped("LCD10", float(b.x + 8), float(b.y + 8), 210, 272, briefing_, green, briefLine_);
        briefLine_ = std::min(briefLine_, std::max(0, lines - 1));
        return;
    }
    if (cb == "MNU_MIS_PL3") {
        blit(image(b.image), float(b.x), float(b.y), 0, 0, 256, 56, false);
        drawWrapped("LCD10", float(b.x + 6), float(b.y + 6), 244, 44, caption_, green);
        return;
    }
    if (cb == "MNU_LO_GUNPIC") {
        const GearEntry& e = loTab_ == 0 ? kPrimary[loSel_[0]] : loTab_ == 1 ? kSecondary[loSel_[1]] : kSidearm[loSel_[2]];
        const HudImage im = image(e.image);
        blit(im, float(b.x), float(b.y), 0, 0, float(im.width), float(im.height), false);
        return;
    }
    if (cb == "MNU_LO_GUNINFO") {
        blit(image(b.image), float(b.x), float(b.y), 0, 0, 252, 140, false);
        const GearEntry& e = loTab_ == 0 ? kPrimary[loSel_[0]] : loTab_ == 1 ? kSecondary[loSel_[1]] : kSidearm[loSel_[2]];
        if (const std::string* t = menuText_.find("GroupMainMenu", e.text)) drawWrapped("LCD10", 104, 268, 238, 123, *t, green);
        return;
    }
    if (cb == "MNU_LO_CURRENTLOADOUT") {
        // APPROXIMATION: the layout of the current-gear box.
        blit(image(b.image), float(b.x), float(b.y), 0, 0, 180, 109, false);
        const std::string* h = menuText_.find("GroupMainMenu", "LOCURRENTLOADOUT");
        drawText("LCD10", float(b.x + 8), float(b.y + 8), h ? *h : "Current Gear", green);
        drawText("LCD10", float(b.x + 8), float(b.y + 30), kPrimary[loSel_[0]].name, green);
        drawText("LCD10", float(b.x + 8), float(b.y + 50), kSecondary[loSel_[1]].name, green);
        drawText("LCD10", float(b.x + 8), float(b.y + 70), kSidearm[loSel_[2]].name, green);
        return;
    }
}

void Frontend::textInput(const char* t) {
    MnuButton* b = mnu_.button(editing_);
    if (!b) return;
    for (const char* p = t; *p; ++p)
        if (int(b->text.size()) + 1 < editMax_ && *p >= 32 && *p < 127) b->text += *p;
}

void Frontend::key(const SDL_KeyboardEvent& k) {
    const SDL_Keycode sym = k.keysym.sym;
    if (!editing_.empty()) {
        MnuButton* b = mnu_.button(editing_);
        if (!b) { editing_.clear(); return; }
        if (sym == SDLK_BACKSPACE && !b->text.empty()) b->text.pop_back();
        else if (b->callback != "MNU_NS_TEXT" && (sym == SDLK_RETURN || sym == SDLK_KP_ENTER || sym == SDLK_ESCAPE)) {
            // Framework (0x406bf8): Enter -> cb(4), Esc -> text restored, cb(5).
            const bool commit = sym != SDLK_ESCAPE;
            if (!commit) b->text = editBackup_;
            editing_.clear();
            SDL_StopTextInput();
            event(*b, commit ? 4 : 5);
        } else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
            // mnu_ns_text (0x410074): empty keeps the old name; else the
            // upper-cased name, slot in use, saved at once.
            if (!b->text.empty()) {
                if (editMode_ == 1) profileDefaults(prof());
                prof().setName(b->text);
                prof().set(0x24, prof().get(0x24) | 1);
                saveProfile(cur_);
            } else if (editMode_ == 1) {
                profileDefaults(prof());
                prof().setName("New Operative");
            }
            b->text = prof().name();
            editing_.clear();
            SDL_StopTextInput();
            refreshSoldier();
        } else if (sym == SDLK_ESCAPE) {
            b->text = editBackup_;
            editing_.clear();
            SDL_StopTextInput();
        }
        return;
    }
    if (screen_ == Start) { sound("BUMP1.WAV"); next_ = Soldier; return; }
    const MnuMenu* m = mnu_.menu(menu_);
    if (!m) return;
    auto fire = [&](const std::string& name) {
        MnuButton* b = mnu_.button(name);
        if (!b || b->drawstate == 0 || b->drawstate == 4 || b->callback.empty()) return;
        event(*b, 1);
        event(*b, 2);
    };
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) return fire(m->accept);
    if (sym == SDLK_ESCAPE) return fire(m->back);
    if (sym == SDLK_UP) return fire(m->up);
    if (sym == SDLK_DOWN) return fire(m->down);
    if (sym == SDLK_LEFT) return fire(m->left);
    if (sym == SDLK_RIGHT) return fire(m->right);
    if (sym >= 32 && sym < 127) {
        const char c = char(std::toupper(int(sym)));
        for (const auto& bn : m->buttons)
            if (MnuButton* b = mnu_.button(bn); b && b->hot == c) return fire(bn);
    }
}

int Frontend::run() {
    if (!loadData()) { std::fprintf(stderr, "cannot read DF.MNU\n"); return 1; }
    loadProfiles();
    // df.cfg: read at start (0x454e10), written at exit (0x40dc88).
    cfg_.load(o_.gameDir / "df.cfg");
    options_ = uint32_t(cfg_.get("attributes"));
    gpsIcons_ = std::clamp(cfg_.get("gpsicon"), 0, 2);
    music_.setVolume(std::clamp(cfg_.get("cd_volume"), 0, 255));
    audio_.openDevice();
    // DF.EXE opens DFMUSIC.SBF from its own folder (0x4b81b0).
    if (const auto exe = findGameExe(o_.gameDir); !exe.empty()) {
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(exe.parent_path(), ec))
            if (toUpper(e.path().filename().string()) == "DFMUSIC.SBF") musicLoaded_ = music_.load(vfs_, e.path());
    }
    if (!musicLoaded_) std::fprintf(stderr, "no menu music (DFMUSIC.SBF / DFMUSIC.BIN)\n");
    audio_.setStream([this](float* out, int frames) { music_.mix(out, frames); });
    if (!openWindow()) return 1;
    enter(Start);
    next_ = Start;
    {
        const std::string s = o_.startScreen;
        Screen first = s == "soldier" ? Soldier : s == "main" ? Main : s == "campaign" ? Campaign : s == "briefing" ? Briefing
                     : s == "loadout" ? Loadout : s == "difficulty" ? Options3 : s == "settings" ? Options1 : s == "macros" ? Macros : s == "debrief" ? Debrief : Start;
        if (first == Briefing || first == Loadout || first == Debrief) { enter(Campaign); }
        if (first != Start) { enter(first); next_ = first; music_.start(); musicVar(0, 0); musicVar(1, first == Soldier ? 4 : first == Main ? 0 : first == Campaign ? 1 : first == Briefing ? 6 : first == Loadout ? 7 : first == Options3 ? 11 : first == Options1 ? 3 : 0); }
    }
    int frame = 0;
    Uint64 last = SDL_GetPerformanceCounter();
    double clock = 0;
    while (screen_ != Quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { next_ = Quit; break; }
            if (e.type == SDL_TEXTINPUT && !editing_.empty()) textInput(e.text.text);
            if (e.type == SDL_KEYDOWN) key(e.key);
            if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                int mx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x, my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
                int ww, wh, dw, dh;
                SDL_GetWindowSize(win_, &ww, &wh);
                SDL_GL_GetDrawableSize(win_, &dw, &dh);
                const float px = float(mx) * float(dw) / float(std::max(1, ww)), py = float(my) * float(dh) / float(std::max(1, wh));
                mouseX_ = int((px - offX_) / scale_);
                mouseY_ = int((py - offY_) / scale_);
                if (screen_ == Start) {
                    if (e.type == SDL_MOUSEBUTTONDOWN) { sound("BUMP1.WAV"); next_ = Soldier; }
                    continue;
                }
                // Slider drag (0x40728c): pos = (mx - x - knobW/2) / width * size,
                // clamped; the mouse stays captured until the button is up.
                auto dragTo = [&](MnuButton& sl) {
                    const HudImage im = image(sl.image);
                    const float f = sl.width > 0 ? float(mouseX_ - sl.x - im.width / 2) / float(sl.width) : 0.f;
                    sl.sliderPos = std::clamp(int(f * float(sl.sliderSize)), 0, sl.sliderSize);
                    event(sl, 1);
                };
                if (MnuButton* sl = mnu_.button(dragging_)) {
                    if (e.type == SDL_MOUSEMOTION) dragTo(*sl);
                    else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) { event(*sl, 2); dragging_.clear(); }
                    continue;
                }
                MnuButton* b = hit(mouseX_, mouseY_);
                if (b && b->type == 2 && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && !(b->attrib & 2) && b->drawstate != 4) {
                    sound("CLICK9.WAV");
                    dragging_ = b->name;
                    dragTo(*b);
                    continue;
                }
                const std::string name = b ? b->name : std::string();
                if (name != hover_) {
                    // Hover out (7), and a release if it was pressed (0x407050).
                    if (MnuButton* old = mnu_.button(hover_)) {
                        event(*old, 7);
                        if (pressed_ == hover_) { pressed_.clear(); event(*old, 2); }
                    }
                    hover_ = name;
                    if (b && b->drawstate != 4 && !(b->attrib & 2)) {
                        if (!(b->attrib & 0x400)) sound("GRASS5.WAV", 128);
                        event(*b, 6);
                    }
                }
                if (b && b->drawstate != 4 && !(b->attrib & 2)) {
                    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                        if (!(b->attrib & 0x400)) sound("CLICK9.WAV");
                        pressed_ = name;
                        event(*b, 1);
                        if (MnuButton* l = mnu_.button(b->link)) event(*l, 1);
                    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && pressed_ == name) {
                        pressed_.clear();
                        event(*b, 2);
                        if (MnuButton* l = mnu_.button(b->link)) event(*l, 2);
                        if (hit(mouseX_, mouseY_) == b) event(*b, 6);
                    }
                }
            }
        }
        // Fixed 16 ms ticks (0x46d600).
        const Uint64 now = SDL_GetPerformanceCounter();
        clock += double(now - last) / double(SDL_GetPerformanceFrequency());
        last = now;
        for (; clock >= 0.016; clock -= 0.016) {
            ++ticks_;
            ++screenTicks_;
            if (screen_ == Start && screenTicks_ >= 600) next_ = Soldier;
        }
        if (screen_ == Options1) syncSliders();
        music_.update();   // once per menu frame (0x406bf8 -> 0x4b8490)
        if (next_ != screen_) {
            if (next_ == Mission) {
                // Pre Menu -> Game Loop: the mission with the soldier's gear and
                // campaign flags; then Post Menu -> Debriefing.
                music_.stop();   // 0x406ab9: silent in the mission
                closeWindow();
                GameOptions g = o_.game;
                char m[32];
                std::snprintf(m, sizeof m, "C%02dM%02d", camp_ + 1, mis_ + 1);
                g.mission = m;
                g.primary = primaryName(prof().get(0x684));
                g.secondary = kitName(prof().get(0x688));
                g.sidearm = sidearmName(prof().get(0x680));
                g.soldierName = prof().name();
                g.gpsIcons = gpsIcons_;
                g.attributes = options_;
                g.crosshairs = cfg_.get("crosshairs") != 0;
                g.flipMouse = cfg_.get("flipmouse") != 0;
                g.mouseScale = std::clamp(cfg_.get("mousescale"), 0, 255);
                g.dialogVolume = std::clamp(cfg_.get("dialog_volume"), 0, 255);
                uint32_t flags = uint32_t(prof().get(0x70 + camp_ * 0x44));
                outcome_ = MissionOutcome{};
                runGame(vfs_, g, &outcome_, &flags);
                if (outcome_.quit) break;
                // 0x40e5d4: a win marks the mission done, kills add up; the
                // flags are kept only on a win.
                prof().set(0x70 + camp_ * 0x44, int32_t(flags));
                if (outcome_.won) prof().set(0x30 + camp_ * 0x44 + 4 * mis_, 1);
                prof().set(0x664, prof().get(0x664) + outcome_.kills);
                if (!openWindow()) return 1;
                // Post Menu (0x41e480): music_start, then Var00=1 (single
                // player); Debf init: Var02 = 0 won / 1 lost (0x423f18).
                music_.start();
                musicVar(0, 1);
                musicVar(2, outcome_.won ? 0 : 1);
                enter(Debrief);
                next_ = Debrief;
            } else if (next_ == Quit) {
                break;
            } else {
                enter(next_);
            }
        }
        // Draw: DF.EXE's 640x480 screen, scaled to the window height, centred.
        int dw, dh;
        SDL_GL_GetDrawableSize(win_, &dw, &dh);
        width_ = dw;
        height_ = dh;
        scale_ = std::min(float(dw) / 640.f, float(dh) / 480.f);
        offX_ = (float(dw) - 640.f * scale_) * 0.5f;
        offY_ = (float(dh) - 480.f * scale_) * 0.5f;
        glViewport(0, 0, dw, dh);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        hud().begin(dw, dh);
        drawMenu();
        if (screen_ == Options1 || screen_ == Options3 || screen_ == Macros) {
            // The settings pages' separator under the tab strip: a green line
            // (37,108)-(600,108) after the buttons (0x40b00c, 0x41c4fc, 0x420eac).
            hud().line(offX_ + 37 * scale_, offY_ + 108.5f * scale_, offX_ + 600 * scale_, offY_ + 108.5f * scale_, scale_, {0, 1, 0, 1});
        }
        if (screen_ == Main) {
            // The hover help (0x40c788 / 0x418568): the hovered button's
            // MM*TEXT in ImageMainPlt at (172, 324), 296x61, 8 px inset, LCD10
            // green (APPROXIMATION: shown at once, not teletyped; no
            // connector lines).
            static const std::pair<const char*, const char*> help[] = {
                {"BUT_MAINNS", "MMNEWSOLDIERTEXT"}, {"BUT_MAINSP", "MMSINGLEPLAYERTEXT"}, {"BUT_MAINMP", "MMMULTIPLAYERTEXT"},
                {"BUT_MAINOP", "MMGAMEOPTIONSTEXT"}, {"BUT_MAINCR", "MMGAMECREDITSTEXT"}, {"BUT_MAINUP", "MMUPDATETEXT"},
                {"BUT_MAINEX", "MMEXITTEXT"}};
            for (const auto& [bn, key] : help)
                if (hover_ == bn) {
                    blit(image("ImageMainPlt"), 172, 324, 0, 0, 296, 61, false);
                    if (const std::string* t = menuText_.find("GroupMainMenu", key))
                        drawWrapped("LCD10", 180, 332, 280, 45, *t, 0xFF00FF00);
                }
        }
        if (screen_ == Debrief) {
            // 0x423b78: "Mission Statistics ~ Number of Kills : n ~ ~ Win<m>",
            // or "Mission Statistics ~ ~ Lose<m>"; teletype box (86, 104)
            // 468x272, AVA10B (shown at once: APPROXIMATION).
            auto t = [&](const char* k) { const std::string* s = menuText_.find("GroupMainMenu", k); return s ? *s : std::string(k); };
            char sec[32], key[32];
            std::snprintf(sec, sizeof sec, "Briefing%d", mis_ + 1);
            std::snprintf(key, sizeof key, outcome_.won ? "Win%d" : "Lose%d", mis_ + 1);
            const std::string* body = campText_.find(sec, key);
            std::string text = t("DEBFMISSIONSTATS");
            if (outcome_.won) text += " ~ " + t("DEBFKILLS") + " : " + std::to_string(outcome_.kills) + " ~ ~ ";
            else text += " ~ ~ ";
            text += body ? *body : "Unable to find mission debriefing text";
            drawWrapped("AVA10B", 86, 104, 468, 272, text, 0xFF00FF00);
        }
        hud().end();
        if (!o_.screenshot.empty() && ++frame >= o_.frames) {
            std::vector<uint8_t> px(size_t(dw) * size_t(dh) * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            std::ofstream f(o_.screenshot, std::ios::binary);
            f << "P6\n" << dw << " " << dh << "\n255\n";
            for (int y = dh - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(&px[size_t(y) * size_t(dw) * 3]), dw * 3);
            break;
        }
        SDL_GL_SwapWindow(win_);
    }
    for (int i = 0; i < 9; ++i) saveProfile(i);
    cfg_.set("attributes", int(options_));
    cfg_.set("gpsicon", gpsIcons_);
    if (o_.screenshot.empty()) cfg_.save(o_.gameDir / "df.cfg");
    music_.stop();
    closeWindow();
    return quitCode_;
}

int runFrontend(Vfs& vfs, const FrontendOptions& options) {
    Frontend f(vfs, options);
    return f.run();
}

} // namespace df
