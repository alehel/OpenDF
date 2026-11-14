#include "Frontend.h"

#include "../audio/Audio.h"
#include "../audio/Music.h"
#include "../core/ExeImage.h"
#include "../game/Config.h"
#include "../game/KeyMap.h"
#include "../game/Controls.h"
#include "../formats/Kdv.h"
#include "../formats/Pcx.h"
#include "../formats/Rtxt.h"
#include "../render/Gl.h"
#include "../render/Hud.h"
#include "../render/Window.h"
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
#include <mutex>
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

// The KDV player's sound (DF.EXE 0x486d07 -> 0x4a9c23): blocks of
// unsigned 8-bit stereo at 22050 Hz in the mixer's 16-slot stream queue
// (0x496da2), played one after another; gain (V >> 1) >> shift per side
// (0x4a9c84) with V the stream volume, 255 until the menu music sets it
// (0x48c9d7, 0x48cb88). Emptied when the player closes (0x4870ab ->
// 0x497eaf).
class KdvStream {
public:
    void push(KdvVideo::Audio a) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(a));
    }
    // Blocks queued, the one playing included ((head - tail) & 15, 0x4a9c14).
    int size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return int(queue_.size());
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        played_ = 0;
    }
    void mix(float* out, int frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        constexpr int V = 255;
        int done = 0;
        while (done < frames && !queue_.empty()) {
            const KdvVideo::Audio& b = queue_.front();
            const float gl = float((V >> 1) >> std::min(b.shiftL, 31)), gr = float((V >> 1) >> std::min(b.shiftR, 31));
            const size_t n = b.lr.size() / 2;
            for (; done < frames && played_ < n; ++played_, ++done) {
                out[size_t(done) * 2] += float(int(b.lr[played_ * 2]) - 128) * gl;
                out[size_t(done) * 2 + 1] += float(int(b.lr[played_ * 2 + 1]) - 128) * gr;
            }
            if (played_ >= n) {
                queue_.erase(queue_.begin());
                played_ = 0;
            }
        }
    }

private:
    std::mutex mutex_;
    std::vector<KdvVideo::Audio> queue_;
    size_t played_ = 0;
};

} // namespace

class Frontend {
public:
    Frontend(Vfs& vfs, const FrontendOptions& o) : vfs_(vfs), o_(o) {}
    int run();

private:
    enum Screen { Start, Soldier, Main, Options1, Options3, Macros, Keyboard, Credits, Campaign, Briefing, Loadout, Mission, Debrief, End, Quit };

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
    void blitHalf(const HudImage& im, float x, float y, float w, float h);   // 50 % blend (0x407cd8)
    // The teletype engine (0x418568 start, 0x418408 tick, 0x4184d4 draw):
    // `rate` characters per 16 ms tick; while typing every draw plays a
    // random Ttclick1..9 at fx*192/255; the text is word-wrapped in its rect
    // in the font's own colour. ensureTty restarts it when the text changes.
    struct Tty { bool active = false; std::string text; size_t count = 0; float x = 0, y = 0, w = 0, h = 0; std::string font; int rate = 1; };
    Tty ttyHelp_, ttyNsLeft_, ttyNsRight_, ttyNsHelp_, ttyCamp_, ttyDebf_, ttyLo_;
    int helpRow_ = -1;
    void ensureTty(Tty& t, const std::string& text, float x, float y, float w, float h, const char* font, int rate);
    void drawTty(Tty& t);
    void tickTtys();
    // Screen wipe (0x418f64(1), draw 0x41933c, tick 0x419414): the old frame,
    // the new screen's first frame revealed 128 px every 4 ticks with the
    // swipe bar blended at the edge and df_bkg2 keyed on top; Whoosh2 at fx.
    GLuint lastTex_ = 0, startTex_ = 0, endTex_ = 0;
    int texW_ = 0, texH_ = 0;
    std::map<GLuint, std::pair<int, int>> texSize_;
    bool haveLast_ = false, wipeActive_ = false, wipeCaptured_ = false;
    int wipeStep_ = 0, wipeCount_ = 0;
    int leadFade_ = -1;           // DF_LEAD -> DF_BKGD frames left (0x418be0), -1 = not started
    int endTicks_ = -1;           // End screen: exit after 10 ticks (0x41cb40)
    bool transitionDone() const { return !wipeActive_; }
    void captureTo(GLuint& tex);
    void drawCaptured(GLuint tex, float x, float y, float w, float h);
    void drawWipe();

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
    KdvStream kdvSound_;
    // The intro (DF.EXE 0x4019d4 from the Start Screen's init 0x418b6c):
    // INTRO.KDV through the ANIMMGR player. Returns false when the window
    // was closed or the test screenshot was taken during it; `frame`
    // counts presented frames for --screenshot/--frames.
    bool playIntro(int& frame, bool& shotTaken);
    bool introLeadSkip_ = false;   // a left click or an unread key during the intro (0x418b50, 0x489dc4)
    void writeScreenshot(int dw, int dh);
    DfConfig cfg_;              // df.cfg (written at exit, 0x40dc88 from 0x4016c7)
    std::string dragging_;      // the slider being dragged (0x4c1b64)
    void refreshSettings();
    void refreshMacros();
    // Keyboard Remap (0x422c10 init, 0x422d3c update, 0x423048 draw).
    std::array<std::array<uint32_t, 2>, 44> kbWork_{}, kbRevert_{};
    int kbScroll_ = 0, kbSel_ = -1, kbMode_ = 0, kbCapture_ = 0, kbTimer_ = -1, kbLastSlider_ = -1;
    std::string kbMsg_;
    size_t kbShown_ = 0;
    void refreshKeys();
    void kbMessage(const char* key, int ticks);
    void kbCaptured(int type, uint32_t code);
    std::string keyName(uint32_t vk);
    // Credits (0x419c70 init, 0x419d94 update, 0x41a3a8 draw): credits.scr
    // commands run against credits.bin; text objects in 100 slots by ID.
    struct CreditCmd { int type = 0; int id = 0, flags = 0, life = 0, x = 0, y = 0, arg = 0; std::string font, color, text; };
    struct CreditText { bool active = false; int life = 0, x = 0, y = 0, flags = 0; std::string font, text; uint32_t color = 0; };
    std::vector<CreditCmd> credits_;
    size_t creditPc_ = 0;
    int creditPause_ = 0;
    bool creditsDone_ = false;
    std::array<CreditText, 100> creditSlots_{};
    TextTable creditText_;
    void loadCredits();
    void tickCredits();
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
    // Briefing map (docs/AUDIT.md F9): items from the BMS, view origin in
    // the 1024x1024 map space (1 px = 4 m), link table, caption teletype.
    struct MapItem { int type, x, y, side; };
    struct BriefLink { std::string key; int word = 0; int r[4] = {0, 0, 0, 0}; };
    std::vector<MapItem> mapItems_;
    int mapCur_[2] = {0, 0}, mapTgt_[2] = {0, 0}, mapCenter_[2] = {512, 512}, marker_[2] = {0, 0};
    bool markerOn_ = false;
    std::vector<BriefLink> links_;
    size_t captionShown_ = 0;
    void loadBriefingMap();
    void centreMap(int x, int y);
    void clickLink();
    void drawBriefingText(const MnuButton& b);
    void drawBriefingMap(const MnuButton& b);
    void menuLine(float x0, float y0, float x1, float y1, uint32_t color, const float* clip = nullptr);
    int briefLine_ = 0, briefTotal_ = 0, briefVisible_ = 0;
    int loTab_ = 0, loSel_[3] = {0, 0, 0};
    MissionOutcome outcome_;
    bool missionPlayed_ = false;   // a mission has ended this run (the mixer hook is off since)
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
                            windowFlags(SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE));
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
    // Captured frames die with the context: no wipe from a mission's frame.
    if (ctx_) for (GLuint* t : {&lastTex_, &startTex_, &endTex_}) if (*t) glDeleteTextures(1, t);
    lastTex_ = startTex_ = endTex_ = 0;
    texSize_.clear();
    haveLast_ = wipeActive_ = false;
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
        // Likewise key bindings (all 44 empty: never written by DF.EXE).
        bool noKeys = true;
        for (int m = 0; m < 44; ++m) noKeys = noKeys && p.get(0x6a0 + 12 * m) == 0 && p.get(0x6a4 + 12 * m) == 0;
        if (noKeys) {
            Profile fresh;
            profileDefaults(fresh);
            std::memcpy(&p.d[0x6a0], &fresh.d[0x6a0], 44 * 12);
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
    // The menu fonts are loaded with flags 3 (0x40624c): bit 2 cuts the
    // glyphs (0x4422f0), zeroing the width markers 3 and 2 (0x442231,
    // 0x442249); bit 1 sets the font's blend flag (0x44263c), so a glyph
    // pixel is not drawn solid but blended (0x442aa6): a = palette red of
    // its index >> 1, dst += (colour - dst) * a >> 7 per channel. The sheets
    // are anti-aliased grey ramps (index 9 white .. 22 dark grey), so the
    // alpha is a / 128; index 0 is skipped.
    std::vector<uint8_t> rgba(size_t(W) * size_t(H) * 4, 0);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const uint8_t v = img.pixels[i];
        if (v == 0 || v == 2 || v == 3) continue;
        const int a = img.palette[v].r >> 1;
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = uint8_t((a * 255 + 64) / 128);
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

void Frontend::captureTo(GLuint& tex) {
    // The HUD caches the bound texture: restore the binding afterwards.
    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    int dw, dh;
    SDL_GL_GetDrawableSize(win_, &dw, &dh);
    auto& size = texSize_[tex];
    if (!tex || size.first != dw || size.second != dh) {
        if (!tex) glGenTextures(1, &tex);
        texSize_[tex] = {dw, dh};
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dw, dh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, dw, dh);
    glBindTexture(GL_TEXTURE_2D, GLuint(prev));
}

void Frontend::drawCaptured(GLuint tex, float x, float y, float w, float h) {
    // A rect of a captured frame (bottom-up rows) at the same menu position.
    int dw, dh;
    SDL_GL_GetDrawableSize(win_, &dw, &dh);
    const float px = offX_ + x * scale_, py = offY_ + y * scale_, pw = w * scale_, ph = h * scale_;
    hud().setBlend(4);
    hud().imageUV(tex, px, py, pw, ph, px / float(dw), 1.f - py / float(dh), (px + pw) / float(dw), 1.f - (py + ph) / float(dh));
    hud().setBlend(0);
}

void Frontend::drawWipe() {
    if (!wipeCaptured_) {
        // Frame 1: the new screen drew itself; keep it as EndBuf, show the
        // old frame and play Whoosh2 (frame 2 in DF.EXE).
        hud().end();
        captureTo(endTex_);
        hud().begin(texW_, texH_);
        wipeCaptured_ = true;
        sound("WHOOSH2.WAV");
    }
    drawCaptured(startTex_, 0, 0, 640, 480);
    if (wipeStep_ > 0) {
        drawCaptured(endTex_, 0, 0, float(128 * wipeStep_), 480);
        // The bar: 50 %, black skipped (0x4083f8).
        const HudImage bar = image("ImageLightBarRt");
        if (bar.texture)
            hud().imageUV(bar.texture, offX_ + float(128 * (wipeStep_ - 1) + 44) * scale_, offY_, 84 * scale_, 480 * scale_, 0, 0, 84.f / float(bar.width),
                          480.f / float(bar.height), {1, 1, 1, 0.5f});
    }
    // df_bkg2 keyed (palette index 0) over everything.
    const std::string key = "DF_BKG2.PCX#k";
    if (!images_.count(key)) images_[key] = hud().loadImage(vfs_, "DF_BKG2.PCX", false, true);
    blit(images_[key], 0, 0, 0, 0, 640, 480, false);
}

void Frontend::blitHalf(const HudImage& im, float x, float y, float w, float h) {
    if (!im.texture) return;
    hud().setBlend(2);
    hud().imageUV(im.texture, offX_ + x * scale_, offY_ + y * scale_, w * scale_, h * scale_, 0, 0, w / float(im.width), h / float(im.height));
    hud().setBlend(0);
}

void Frontend::ensureTty(Tty& t, const std::string& text, float x, float y, float w, float h, const char* font, int rate) {
    if (t.active && t.text == text) return;
    t = {true, text.substr(0, 0x3FE), 0, x, y, w, h, font, rate};
}

void Frontend::tickTtys() {
    for (Tty* t : {&ttyHelp_, &ttyNsLeft_, &ttyNsRight_, &ttyNsHelp_, &ttyCamp_, &ttyDebf_, &ttyLo_})
        if (t->active) t->count = std::min(t->text.size(), t->count + size_t(t->rate));
}

void Frontend::drawTty(Tty& t) {
    if (!t.active) return;
    if (t.count < t.text.size()) {
        char n[16];
        std::snprintf(n, sizeof n, "TTCLICK%d.WAV", 1 + std::rand() % 9);
        sound(n, 192);
    }
    drawWrapped(t.font, t.x, t.y, t.w, t.h, t.text.substr(0, t.count), 0xFF00FF00);
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
        // Direction 0's pan (kPan[0]: 63 left, 64 right) at that volume.
        audio_.play(c, 63.f * float(volume) / (127.f * 255.f), 64.f * float(volume) / (127.f * 255.f));
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
    if (s != End && s != Start && s != Quit && haveLast_) {
        std::swap(startTex_, lastTex_);   // 0x419104: the last frame of the old screen
        wipeActive_ = true;
        wipeCaptured_ = false;
        wipeStep_ = wipeCount_ = 0;
    }
    if (s == End) endTicks_ = 10;
    for (Tty* t : {&ttyHelp_, &ttyNsLeft_, &ttyNsRight_, &ttyNsHelp_, &ttyCamp_, &ttyDebf_, &ttyLo_}) *t = Tty{};
    helpRow_ = -1;
    if (screen_ == Macros && s != Macros) saveProfile(cur_);   // Macros exit hook 0x420ed4
    if (screen_ == Keyboard && s != Keyboard) {
        // 0x423118: any exit stores the working copy and saves the profile.
        for (int i = 0; i < 44; ++i) { prof().set(0x6a0 + 12 * i, int32_t(kbWork_[size_t(i)][0])); prof().set(0x6a4 + 12 * i, int32_t(kbWork_[size_t(i)][1])); }
        kbCapture_ = 0;
        kbSel_ = -1;
        saveProfile(cur_);
    }
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
    case Credits:
        setMenu("CreditsMenu");
        loadCredits();
        musicVar(1, 5);   // 0x419d5c
        break;
    case Keyboard: {
        // 0x422c10: working copy and revert snapshot from the profile; mode
        // ASSIGN KEY (DELETE KEY enabled); "Please select a key ..." (no music var).
        setMenu("KBDMenu");
        for (int i = 0; i < 44; ++i) kbWork_[size_t(i)] = {uint32_t(prof().get(0x6a0 + 12 * i)), uint32_t(prof().get(0x6a4 + 12 * i))};
        kbRevert_ = kbWork_;
        kbSel_ = -1;
        kbCapture_ = 0;
        kbMode_ = 0;
        kbLastSlider_ = -1;
        if (MnuButton* x = mnu_.button("but_kbd_ak")) x->drawstate = 3;
        if (MnuButton* x = mnu_.button("but_kbd_am")) x->drawstate = 1;
        if (MnuButton* x = mnu_.button("but_kbd_dk")) x->drawstate = 1;
        kbMessage("KBDCLICKKEY", -1);
        refreshKeys();
        break;
    }
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
        captionShown_ = 0;
        {
            // 0x41e670: every {...} group is cut out; it links the word that
            // follows (index = spaces copied so far).
            links_.clear();
            std::string out;
            int spaces = 0;
            for (size_t i = 0; i < briefing_.size(); ++i) {
                if (briefing_[i] == '{') {
                    const size_t e = briefing_.find('}', i);
                    if (e != std::string::npos) {
                        if (links_.size() < 128) links_.push_back({briefing_.substr(i + 1, std::min<size_t>(e - i - 1, 63)), spaces, {}});
                        i = e;
                        continue;
                    }
                }
                if (briefing_[i] == ' ') ++spaces;
                out += briefing_[i];
            }
            briefing_ = out;
        }
        loadBriefingMap();
        {
            // 0x41edbc: line breaks counted as the text is laid out in
            // (348..558); quirk: `~` counts a break but does not reset x.
            // Visible = trunc(272 / font height) (0x4122a0); the slider
            // range is total - visible + 1, or 0 if it all fits.
            MenuFont& f = font("AVA10B");
            briefTotal_ = 0;
            briefVisible_ = f.height > 0 ? 272 / f.height : 1;
            std::vector<std::string> tokens;
            std::string t;
            for (char c : briefing_) {
                if (c == ' ') { if (!t.empty()) tokens.push_back(t); t.clear(); }
                else t += c;
            }
            if (!t.empty()) tokens.push_back(t);
            float x = 348;
            for (size_t i = 0; i < tokens.size(); ++i) {
                if (i > 0 && tokens[i] == "~") { ++briefTotal_; continue; }
                const float w = textWidth(f, tokens[i] + " ");
                if (i > 0 && x + w > 558) { ++briefTotal_; x = 348; }
                x += w;
            }
            if (MnuButton* sl = mnu_.button("but_mi_sl")) {
                sl->sliderSize = briefTotal_ >= briefVisible_ ? briefTotal_ - briefVisible_ + 1 : 0;
                sl->sliderPos = 0;
            }
        }
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
            else if (b->type == 3) w = im.width;    // V slider = knob width
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
    } else if (!b.image.empty() && (b.type == 2 || b.type == 3) && !(b.attrib & 2) && b.drawstate != 4) {
        // Knob at x + pos * width / size, or y + pos * height / size (0x40886c).
        const HudImage im = image(b.image);
        const float f = b.sliderSize > 0 ? float(b.sliderPos) / float(b.sliderSize) : 0.f;
        if (b.type == 2) blit(im, float(b.x) + f * float(b.width), float(b.y), 0, 0, float(im.width), float(im.height), false);
        else blit(im, float(b.x), float(b.y) + f * float(b.height), 0, 0, float(im.width), float(im.height), false);
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
        // The splash (DF_LEAD.PCX); once ended, DF_BKGD 50 %-blended over it
        // on 10 draw frames (0x418be0), then Choose Soldier.
        blit(file("DF_LEAD.PCX"), 0, 0, 0, 0, 640, 480, false);
        if (leadFade_ >= 0) {
            const int k = 10 - leadFade_ + 1;
            const HudImage bk = file("DF_BKGD.PCX", false);
            if (bk.texture)
                hud().imageUV(bk.texture, offX_, offY_, 640 * scale_, 480 * scale_, 0, 0, 1, 1, {1, 1, 1, 1.f - std::pow(0.5f, float(k))});
            if (--leadFade_ <= 0) { leadFade_ = -1; next_ = Soldier; screenTicks_ = -100000; }
        }
        return;
    }
    const MnuMenu* m = mnu_.menu(menu_);
    if (!m) return;
    if (!m->background.empty()) blit(image(m->background), 0, 0, 0, 0, 640, 480, false);
    for (const auto& bn : m->buttons)
        if (MnuButton* b = mnu_.button(bn)) drawButton(*b);
    // The cursor (0x408df8): ImageCursor1 centred on the mouse; pixels of
    // palette index 0 are skipped (0x408f14: cmpb $0, (%edx)), so the blue
    // (0,0,255) of CURSOR1.PCX's index 0 never shows.
    const auto ci = mnu_.images.find("IMAGECURSOR1");
    const std::string cpcx = ci == mnu_.images.end() ? std::string() : upperCase(ci->second);
    const std::string ckey = cpcx + "#k";
    if (!cpcx.empty() && !images_.count(ckey)) images_[ckey] = hud().loadImage(vfs_, cpcx, false, true);
    const HudImage cur = cpcx.empty() ? HudImage{} : images_[ckey];
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
    for (int i = 0; i < 44; ++i) {   // key bindings from 0x4dd190
        p.set(0x6a0 + 12 * i, int32_t(kKeyRows[i].vk));
        p.set(0x6a4 + 12 * i, int32_t(kKeyRows[i].mouse));
    }
    for (int i = 0; i < 10; ++i) {
        char k[32];
        std::snprintf(k, sizeof k, "DEFAULTMACRO%d", i + 1);
        if (const std::string* t = menuText_.find("Macros", k)) p.setStr(0x4b0 + 40 * i, 40, *t);
    }
}

void Frontend::loadBriefingMap() {
    // 0x440bdc: records of all four groups with map type +0x4C, not hidden
    // (+0x0C bits 0x10/0x40), at most 200; map point (x>>18)+512, 512-(y>>18).
    // The view starts on the first group-3 "Briefing Map Center" (6038).
    mapItems_.clear();
    markerOn_ = false;
    mapCenter_[0] = mapCenter_[1] = 512;
    std::vector<uint8_t> d;
    char n[32];
    std::snprintf(n, sizeof n, "C%02dM%02d.BMS", camp_ + 1, mis_ + 1);
    if (vfs_.read(n, d) && d.size() >= 0x10C && std::memcmp(d.data(), "BMS", 3) == 0 && d[3] >= 0x0D) {
        auto i32 = [&](size_t o) { int32_t v = 0; if (o + 4 <= d.size()) std::memcpy(&v, &d[o], 4); return v; };
        size_t o = 0x10C;
        bool centre = false;
        for (int g = 0; g < 4; ++g) {
            const int count = i32(0x98 + 4 * size_t(g));
            for (int k = 0; k < count && o + 0x60 <= d.size(); ++k, o += 0x60) {
                const int mx = (i32(o + 0x10) >> 18) + 512, my = 512 - (i32(o + 0x14) >> 18);
                if (g == 3 && !centre && i32(o) == 6038) { centre = true; mapCenter_[0] = mx; mapCenter_[1] = my; }
                if (d[o + 0x4C] && !(d[o + 0x0C] & 0x50) && mapItems_.size() < 200)
                    mapItems_.push_back({d[o + 0x4C], mx, my, d[o + 0x43]});
            }
        }
    }
    centreMap(mapCenter_[0], mapCenter_[1]);
}

void Frontend::centreMap(int x, int y) {
    // 0x411df0: origin = point - 128 clamped to 0..768, set at once (view,
    // target and the two sliders).
    mapCur_[0] = mapTgt_[0] = std::clamp(x - 128, 0, 768);
    mapCur_[1] = mapTgt_[1] = std::clamp(y - 128, 0, 768);
    if (MnuButton* h = mnu_.button("but_mi_hscrsl")) h->sliderPos = mapTgt_[0];
    if (MnuButton* v = mnu_.button("but_mi_vscrsl")) v->sliderPos = mapTgt_[1];
}

void Frontend::menuLine(float x0, float y0, float x1, float y1, uint32_t color, const float* clip) {
    // A 1-px line in 640x480 space; clipped to [clip] (Liang-Barsky) if given.
    if (clip) {
        float t0 = 0, t1 = 1;
        const float dx = x1 - x0, dy = y1 - y0;
        const float p[4] = {-dx, dx, -dy, dy}, q[4] = {x0 - clip[0], clip[2] - x0, y0 - clip[1], clip[3] - y0};
        for (int i = 0; i < 4; ++i) {
            if (p[i] == 0) { if (q[i] < 0) return; continue; }
            const float t = q[i] / p[i];
            if (p[i] < 0) t0 = std::max(t0, t); else t1 = std::min(t1, t);
        }
        if (t0 > t1) return;
        const float ax = x0 + t0 * dx, ay = y0 + t0 * dy, bx = x0 + t1 * dx, by = y0 + t1 * dy;
        x0 = ax; y0 = ay; x1 = bx; y1 = by;
    }
    // DF.EXE's pixel op (0x409964): colour/2 plus half the background's
    // blue; the alpha byte is ignored.
    Color c = argb(color);
    c.a = 1;
    hud().setBlend(3);
    hud().line(offX_ + (x0 + 0.5f) * scale_, offY_ + (y0 + 0.5f) * scale_, offX_ + (x1 + 0.5f) * scale_, offY_ + (y1 + 0.5f) * scale_,
               scale_, c);
    hud().setBlend(0);
}

void Frontend::drawBriefingMap(const MnuButton& b) {
    // 0x412c10 ev3. Easing per drawn frame: min(gap >> 1, 32) (stops 1 px short).
    for (int a = 0; a < 2; ++a) {
        int& c = mapCur_[a];
        const int t = mapTgt_[a];
        if (c > t) c -= std::min((c - t) >> 1, 32);
        if (c < t) c += std::min((t - c) >> 1, 32);
    }
    const int ox = mapCur_[0], oy = mapCur_[1];
    const float bx = float(b.x), by = float(b.y);
    // The picture wraps (0x408560): window pixel (d, r) shows picture pixel
    // ((d + ox + 1) & 255, (r + oy) & 255).
    char n[32];
    std::snprintf(n, sizeof n, "C%dM%dSM.PCX", camp_ + 1, mis_ + 1);
    const HudImage pic = file(n);
    const int u0 = (ox + 1) & 255, v0 = oy & 255;
    for (int qy = 0; qy < 2; ++qy)
        for (int qx = 0; qx < 2; ++qx) {
            const int su = qx ? 0 : u0, sv = qy ? 0 : v0;
            const int w = qx ? u0 : 256 - u0, h = qy ? v0 : 256 - v0;
            if (w <= 0 || h <= 0) continue;
            blit(pic, bx + float(qx ? 256 - u0 : 0), by + float(qy ? 256 - v0 : 0), float(su), float(sv), float(w), float(h), false);
        }
    // Outline (0x409f88), green.
    const uint32_t green = 0xFF00FF00, route = 0xFFFFE000;
    menuLine(bx, by, bx + 255, by, green);
    menuLine(bx + 255, by, bx + 255, by + 255, green);
    menuLine(bx + 255, by + 255, bx, by + 255, green);
    menuLine(bx, by + 255, bx, by, green);
    const float clip[4] = {bx, by, bx + 255, by + 255};
    // Route: items of type 1 or 3 joined in order, clipped to the window.
    bool have = false;
    float px = 0, py = 0;
    for (const MapItem& it : mapItems_) {
        if (it.type != 1 && it.type != 3) continue;
        const float sx = bx + float(it.x - ox), sy = by + float(it.y - oy);
        if (have) menuLine(px, py, sx, sy, route, clip);
        px = sx; py = sy; have = true;
    }
    auto inside = [](int x, int y, int x0, int y0, int x1, int y1) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; };
    auto icon = [&](const char* sheet, int frame, float x, float y) {
        // Colour-keyed on palette index 0 (0x407b28).
        const std::string pcx = mnu_.images.count(upperCase(sheet)) ? upperCase(mnu_.images[upperCase(sheet)]) : std::string();
        const std::string key = pcx + "#k";
        if (!images_.count(key)) images_[key] = hud().loadImage(vfs_, pcx, false, true);
        const HudImage im = images_[key];
        blit(im, x, y, 0, float(frame * 6), 8, 6, false);
    };
    // Side icons (8x6 frames type-1 of Green/Blue/RedIcons), culled only.
    for (const MapItem& it : mapItems_) {
        if (it.type == 1 || it.type == 3 || it.side < 0 || it.side > 2) continue;
        if (!inside(it.x - 4, it.y - 4, ox + 4, oy + 4, ox + 247, oy + 247)) continue;
        static const char* kSheet[3] = {"ImageGreenIcons", "ImageBlueIcons", "ImageRedIcons"};
        icon(kSheet[it.side], it.type - 1, bx + float(it.x - ox - 4), by + float(it.y - oy - 4));
    }
    // Waypoints: numbers in LCD10 0xFFAAFFAA at (+4, -6), YellowIcons frame type-1.
    int number = 0;
    for (const MapItem& it : mapItems_) {
        if (it.type != 1 && it.type != 3) continue;
        ++number;
        const float sx = bx + float(it.x - ox), sy = by + float(it.y - oy);
        if (inside(it.x - 4, it.y - 4, ox - 4, oy + 4, ox + 235, oy + 247))
            drawText("LCD10", sx + 4, sy - 6, std::to_string(number), 0xFFAAFFAA);
        if (inside(it.x - 4, it.y - 4, ox + 4, oy + 4, ox + 247, oy + 247)) icon("ImageYellowIcons", it.type - 1, sx - 4, sy - 4);
    }
    // The link marker: a crosshair across the window.
    if (markerOn_ && inside(marker_[0], marker_[1], ox, oy, ox + 254, oy + 254)) {
        const float mx = bx + float(marker_[0] - ox), my = by + float(marker_[1] - oy);
        menuLine(mx, by, mx, by + 255, route);
        menuLine(bx, my, bx + 255, my, route);
    }
}

void Frontend::drawBriefingText(const MnuButton& b) {
    // 0x41e9f4: AVA10B in (x+8, y+8)-(x+218, y+280); tokens split on spaces
    // (strtok), each drawn with a trailing space; "~" breaks the line; links
    // pale (0xFFDDFFDD), red under the mouse; plain text TrueGreen.
    MenuFont& f = font("AVA10B");
    if (!f.ok) return;
    const float left = float(b.x + 8), top = float(b.y + 8), right = float(b.x + 218), bottom = float(b.y + 280);
    const float fh = float(f.height);
    std::vector<std::string> tokens;
    {
        std::string t;
        for (char c : briefing_) {
            if (c == ' ') { if (!t.empty()) tokens.push_back(t); t.clear(); }
            else t += c;
        }
        if (!t.empty()) tokens.push_back(t);
    }
    float x = left, y = top;
    int lineNo = 0;
    size_t link = 0;
    auto put = [&](size_t index, bool visible) {
        const std::string tok = tokens[index] + " ";
        const float w = textWidth(f, tok);
        if (link < links_.size() && links_[link].word == int(index)) {
            BriefLink& l = links_[link++];
            if (visible) {
                l.r[0] = int(x); l.r[1] = int(y); l.r[2] = int(x + w); l.r[3] = int(y + fh - 1);
                const bool hover = mouseX_ >= l.r[0] && mouseX_ <= l.r[2] && mouseY_ >= l.r[1] && mouseY_ <= l.r[3];
                drawText("AVA10B", x, y, tok, hover ? 0xFFFF0000 : 0xFFDDFFDD);
            } else {
                l.r[0] = l.r[1] = l.r[2] = l.r[3] = 0;
            }
        } else if (visible) {
            drawText("AVA10B", x, y, tok, 0xFF00FF00);
        }
        x += w;
    };
    auto newline = [&]() {
        x = left;
        if (lineNo >= briefLine_) y += fh;
        ++lineNo;
    };
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i == 0) { put(i, true); continue; }   // the first token is drawn untested (as DF.EXE)
        if (tokens[i] == "~") { newline(); }
        else {
            if (x + textWidth(f, tokens[i] + " ") > right) newline();
            if (y + fh > bottom) break;
            put(i, lineNo >= briefLine_);
            continue;
        }
        if (y + fh > bottom) break;
    }
}

void Frontend::clickLink() {
    // 0x41ed5c / 0x4132a5: the first link under the mouse; its text from
    // Briefing<m> is the caption and its entry's map point (u16 x, u16 y)
    // gets the marker and the view; beep1 at fx*64/255; the caption is
    // teletyped.
    const BriefLink* hit = nullptr;
    for (const BriefLink& l : links_)
        if (mouseX_ >= l.r[0] && mouseX_ <= l.r[2] && mouseY_ >= l.r[1] && mouseY_ <= l.r[3] && l.r[2] > 0) { hit = &l; break; }
    if (!hit) return;
    char sec[32];
    std::snprintf(sec, sizeof sec, "Briefing%d", mis_ + 1);
    if (const std::string* t = campText_.find(sec, hit->key)) {
        caption_ = *t;
        const uint32_t pos = campText_.position(sec, hit->key);
        markerOn_ = true;
        marker_[0] = int(pos & 0xFFFF);
        marker_[1] = int(pos >> 16);
        centreMap(marker_[0], marker_[1]);
    } else {
        caption_ = "Unable to find link '" + hit->key + "'";
    }
    sound("BEEP1.WAV", 64);
    if (caption_.size() > 1022) caption_.resize(1022);
    captionShown_ = 0;
}

void Frontend::loadCredits() {
    // Every visit starts over (the init clears the slots and the finished
    // flag). credits.scr: [MISSIONSCRIPT] SCRIPT={COMMAND},args (0x421538).
    credits_.clear();
    creditSlots_ = {};
    creditPc_ = 0;
    creditPause_ = 0;
    creditsDone_ = false;
    std::vector<uint8_t> raw;
    creditText_ = TextTable{};
    if (vfs_.read("CREDITS.BIN", raw)) creditText_.parse(raw);
    if (!vfs_.read("CREDITS.SCR", raw)) return;
    std::istringstream in(std::string(raw.begin(), raw.end()));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == ';' || eq == std::string::npos || upperCase(line.substr(0, eq)) != "SCRIPT") continue;
        std::vector<std::string> f;
        std::stringstream ss(line.substr(eq + 1));
        for (std::string t; std::getline(ss, t, ',');) {
            t.erase(0, t.find_first_not_of(" \t"));
            t.erase(t.find_last_not_of(" \t") + 1);
            f.push_back(t);
        }
        if (f.empty()) continue;
        const std::string cmd = upperCase(f[0]);
        auto num = [&](size_t i) { return i < f.size() ? std::atoi(f[i].c_str()) : 0; };
        auto str = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
        CreditCmd c;
        if (cmd == "{PRINTTEXT}" || cmd == "{PRINTTEXTTTY}") {
            c = {cmd == "{PRINTTEXT}" ? 0 : 1, num(1), num(2), num(3), num(6), num(7), 0, str(4), str(5), str(8)};
        } else if (cmd == "{PAUSE}") { c.type = 2; c.arg = num(1); }
        else if (cmd == "{PLAYSOUND}") { c.type = 8; c.text = str(1); c.arg = num(2); }
        else if (cmd == "{REMOVEOBJECT}") { c.type = 5; c.id = num(1); }
        else if (cmd == "{END}") break;
        else continue;   // lines, rectangles, pictures, anims: not used by CREDITS.SCR
        credits_.push_back(c);
    }
}

void Frontend::tickCredits() {
    // 0x419d94: a pause counts down; else commands run up to and including
    // the next PAUSE; at the end of the list -> Main Menu (not looped). Then
    // every text loses one tick of life and goes at 0 (-1 = for ever).
    if (!creditsDone_) {
        if (creditPause_ > 0) --creditPause_;
        else for (;;) {
            if (creditPc_ >= credits_.size()) { creditsDone_ = true; next_ = Main; break; }
            const CreditCmd& c = credits_[creditPc_++];
            if (c.type == 0 && c.id >= 0 && c.id < 100) {
                // 0x419edd: the key's text from credits.bin "Credits".
                CreditText& t = creditSlots_[size_t(c.id)];
                const std::string* tx = creditText_.find("Credits", c.text);
                t = {true, c.life, c.x, c.y, c.flags, c.font, tx ? *tx : c.text, 0xFF00FF00};
                const auto it = mnu_.colors.find(upperCase(c.color));
                if (it != mnu_.colors.end()) t.color = it->second | 0xFF000000;
            } else if (c.type == 5 && c.id >= 0 && c.id < 100) {
                creditSlots_[size_t(c.id)].active = false;
            } else if (c.type == 8) {
                // 0x41a220: volume = arg * fx_volume / 255, at most 255.
                const int vol = std::min(255, int(std::lround(c.arg * std::clamp(cfg_.get("fx_volume"), 0, 255) / 255.0)));
                if (const SoundClip* clip = audio_.clip(vfs_, upperCase(c.text))) {
                    audio_.play(clip, 63.f * float(vol) / (127.f * 255.f), 64.f * float(vol) / (127.f * 255.f));
                }
            } else if (c.type == 2) {
                creditPause_ = c.arg;
                break;
            }
        }
    }
    for (CreditText& t : creditSlots_)
        if (t.active && --t.life == 0) t.active = false;
}

std::string Frontend::keyName(uint32_t vk) {
    return vkName(vk, [&](const char* k) { const std::string* x = menuText_.find("GroupMainMenu", k); return x ? *x : std::string(k); });
}

void Frontend::refreshKeys() {
    // 0x4227bc: rows scroll .. scroll+10: label, then "key or Mouse n".
    auto t = [&](const char* k) { const std::string* x = menuText_.find("GroupMainMenu", k); return x ? *x : std::string(k); };
    for (int r = 0; r < 11; ++r) {
        const int i = std::clamp(kbScroll_ + r, 0, 43);
        char n[32];
        std::snprintf(n, sizeof n, "but_kbd_%d", r + 1);
        if (MnuButton* b = mnu_.button(n)) { b->text = t(kKeyRows[i].label); b->attrib |= 0x40; }
        std::snprintf(n, sizeof n, "but_kbd_%dw", r + 1);
        std::string v = keyName(kbWork_[size_t(i)][0]);
        const uint32_t m = kbWork_[size_t(i)][1];
        if (m >= 1 && m <= 3) {
            if (!v.empty()) v += " " + t("KBDOR") + " ";
            v += t(m == 1 ? "KBDMOUSELEFT" : m == 2 ? "KBDMOUSERIGHT" : "KBDMOUSEMIDDLE");
        }
        if (MnuButton* b = mnu_.button(n)) { b->text = v; b->attrib |= 0x40; }
    }
}

void Frontend::kbMessage(const char* key, int ticks) {
    // 0x422bb8: teletyped into (467,282) 116x98; ticks -1 = stays.
    const std::string* x = menuText_.find("GroupMainMenu", key);
    kbMsg_ = x ? *x : key;
    kbShown_ = 0;
    kbTimer_ = ticks;
}

void Frontend::kbCaptured(int type, uint32_t code) {
    // 0x422d3c: a key (type 1) or mouse button (type 2) while waiting.
    static const auto allowed = [](uint32_t vk) {   // 0x421ec8
        return (vk >= 0x30 && vk <= 0x39) || (vk >= 0x41 && vk <= 0x5A) || (vk >= 0x25 && vk <= 0x28) || (vk >= 0x60 && vk <= 0x6F) ||
               vk == 0x11 || vk == 0x10 || vk == 0x0D || vk == 0x2D || vk == 0x2E || vk == 0x24 || vk == 0x23 || vk == 0x20 ||
               vk == 0x08 || vk == 0x09 || vk == 0x21 || vk == 0x22 || vk == 0xBB || vk == 0xBD || vk == 0xBC || vk == 0xBE ||
               vk == 0xBA || vk == 0xDB || vk == 0xDD;
    };
    const int i = std::clamp(kbScroll_ + kbSel_, 0, 43);
    if (kbCapture_ == 1 && type == 1) {
        if (allowed(code)) {
            for (int j = 0; j < 44; ++j)   // unbound elsewhere (0x422764)
                if (j != i && kbWork_[size_t(j)][0] == code) kbWork_[size_t(j)][0] = 0;
            kbWork_[size_t(i)][0] = code;
            kbMessage("KBDACCEPTEDKEY", 120);
        } else {
            kbMessage("KBDRESERVEDKEY", 180);
        }
    } else if (kbCapture_ == 2 && type == 2) {
        for (int j = 0; j < 44; ++j)
            if (j != i && kbWork_[size_t(j)][1] == code) kbWork_[size_t(j)][1] = 0;
        kbWork_[size_t(i)][1] = code;
        kbMessage("KBDACCEPTEDKEY", 120);
    }   // the other kind cancels silently
    char n[32];
    std::snprintf(n, sizeof n, "but_kbd_%dw", kbSel_ + 1);
    if (MnuButton* b = mnu_.button(n)) b->drawstate = 1;
    std::snprintf(n, sizeof n, "but_kbd_%dd", kbSel_ + 1);
    if (MnuButton* b = mnu_.button(n)) b->drawstate = 1;
    kbCapture_ = 0;
    kbSel_ = -1;
    refreshKeys();
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
    if (cb == "MNU_MAIN_CREDITS") return standard(b, ev, to(Credits));
    if (cb == "MNU_CREDITS_ACCEPT") return standard(b, ev, to(Main));   // 0x41a434
    if (cb == "MNU_MAIN_MP" || cb == "MNU_MAIN_UPDATE") return standard(b, ev, [] {});
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
    if (cb == "MNU_MIS_PL1") {
        // Click on the map (0x412f90): click9 at fx*64/255, centre on it.
        if (ev == 1) {
            sound("CLICK9.WAV", 64);
            centreMap(mapTgt_[0] + mouseX_ - 50, mapTgt_[1] + mouseY_ - 66);
        }
        return;
    }
    if (cb == "MNU_MIS_PL2") { if (ev == 2) clickLink(); return; }
    if (cb == "MNU_MIS_SCROLLMAP") {
        // 0x4138c4: arrows step the target 32 px (clamped, sliders follow);
        // the centre button goes back to the start point.
        if (b.type == 2 || b.type == 3) return;   // the sliders act in their owner draw
        if (ev == 1) {
            b.drawstate = 3;
            sound("CLICK9.WAV", 128);
            switch (b.userid) {
            case 0: mapTgt_[1] -= 32; break;
            case 1: mapTgt_[1] += 32; break;
            case 2: mapTgt_[0] -= 32; break;
            case 3: mapTgt_[0] += 32; break;
            case 6: centreMap(mapCenter_[0], mapCenter_[1]); return;
            default: break;
            }
            mapTgt_[0] = std::clamp(mapTgt_[0], 0, 768);
            mapTgt_[1] = std::clamp(mapTgt_[1], 0, 768);
            if (MnuButton* h = mnu_.button("but_mi_hscrsl")) h->sliderPos = mapTgt_[0];
            if (MnuButton* v = mnu_.button("but_mi_vscrsl")) v->sliderPos = mapTgt_[1];
        } else if (ev == 2 || ev == 7) b.drawstate = 1;
        else if (ev == 6) b.drawstate = 2;
        return;
    }
    if (cb == "MNU_MIS_SCROLLBAR") {
        // 0x4129e0: press pushes with click9 at fx/2; release steps the first
        // line within 0..total-visible+1 (0 when it all fits); the slider
        // follows. The slider's owner draw (event 3) sets the line.
        if (b.type == 3) return;
        if (ev == 1) { b.drawstate = 3; sound("CLICK9.WAV", 128); }
        else if (ev == 2) {
            b.drawstate = 1;
            if (briefTotal_ < briefVisible_) briefLine_ = 0;
            else if (upperCase(b.name) == "BUT_MI_UP") briefLine_ = std::max(0, briefLine_ - 1);
            else if (upperCase(b.name) == "BUT_MI_DN") briefLine_ = std::min(briefLine_ + 1, briefTotal_ - briefVisible_ + 1);
            if (MnuButton* sl = mnu_.button("but_mi_sl")) sl->sliderPos = briefLine_;
        } else if (ev == 6) b.drawstate = 2;
        else if (ev == 7) b.drawstate = 1;
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
            {
                // 0x40b7b4 over the availability table 0x4f5140, set once to
                // {1, 1, 0} (0x40161a): 256 <-> 16 Million, never 3Dfx.
                static const int kAvail[3] = {1, 1, 0};
                const int d = left ? -1 : 1;
                int n = cfg_.get("renderer") + d;
                for (int guard = 0; guard < 4; ++guard) {
                    if (n > 2) n = 0;
                    if (n < 0) n = 2;
                    if (kAvail[n]) break;
                    n += d;
                }
                cfg_.set("renderer", n);
            }
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
    if (cb == "MNU_OPT1_KBDREMAP") return standard(b, ev, to(Keyboard));
    // Keyboard Remap.
    if (cb == "MNU_KBD_ASSIGNKEY" || cb == "MNU_KBD_ASSIGNMOUSE" || cb == "MNU_KBD_DELETEKEY") {
        // Radio buttons (0x4237b0, 0x423700, 0x42385c).
        if (ev == 1) {
            for (const char* n : {"but_kbd_ak", "but_kbd_am", "but_kbd_dk"})
                if (MnuButton* x = mnu_.button(n)) x->drawstate = 1;
            b.drawstate = 3;
            kbMode_ = cb == "MNU_KBD_ASSIGNKEY" ? 0 : cb == "MNU_KBD_ASSIGNMOUSE" ? 1 : 2;
            kbMessage(kbMode_ == 0 ? "KBDKEYMOD" : kbMode_ == 1 ? "KBDMOUSEMOD" : "KBDDELETEMOD", -1);
        } else if (ev == 6) { if (b.drawstate == 1) b.drawstate = 2; }
        else if (ev == 7) { if (b.drawstate == 2) b.drawstate = 1; }
        return;
    }
    if (cb == "MNU_KBD_GETKEY") {
        // 0x4231b8: row u = userid; press pushes box and dot; release waits
        // for a key / mouse button or deletes.
        char w[32], d[32];
        std::snprintf(w, sizeof w, "but_kbd_%dw", b.userid + 1);
        std::snprintf(d, sizeof d, "but_kbd_%dd", b.userid + 1);
        MnuButton* bw = mnu_.button(w);
        MnuButton* bd = mnu_.button(d);
        auto both = [&](int st) { if (bw) bw->drawstate = st; if (bd) bd->drawstate = st; };
        if (ev == 1) both(3);
        else if (ev == 6) { if (b.drawstate == 1) both(2); }
        else if (ev == 7) { if (b.drawstate == 2) both(1); }
        else if (ev == 2) {
            if (kbMode_ == 0) { kbCapture_ = 1; kbMessage("KBDASSIGNKEYTXT", -1); }
            else if (kbMode_ == 1) { kbCapture_ = 2; kbMessage("KBDASSIGNMOUSETXT", -1); }
            else {
                const int i = std::clamp(kbScroll_ + b.userid, 0, 43);
                kbWork_[size_t(i)] = {0, 0};
                refreshKeys();
                b.drawstate = 1;
                kbMessage("KBDDELETEKEYTXT", 120);
            }
            if (kbSel_ >= 0 && kbSel_ != b.userid) {
                char pw[32], pd[32];
                std::snprintf(pw, sizeof pw, "but_kbd_%dw", kbSel_ + 1);
                std::snprintf(pd, sizeof pd, "but_kbd_%dd", kbSel_ + 1);
                if (MnuButton* x = mnu_.button(pw)) x->drawstate = 1;
                if (MnuButton* x = mnu_.button(pd)) x->drawstate = 1;
            }
            kbSel_ = b.userid;
            if (kbMode_ == 2) both(1);
        }
        return;
    }
    if (cb == "MNU_KBD_SCROLLBAR") {
        // 0x4235c0: arrows step on release (click9 at fx/2 on press); the
        // slider sets the scroll in its owner draw.
        if (b.type == 3) return;
        if (ev == 1) { b.drawstate = 3; sound("CLICK9.WAV", 128); }
        else if (ev == 2) {
            b.drawstate = 1;
            kbScroll_ = std::clamp(kbScroll_ + (upperCase(b.name) == "BUT_KBD_UP" ? -1 : 1), 0, 33);
            if (MnuButton* sl = mnu_.button("but_kbd_sl")) sl->sliderPos = kbScroll_;
            refreshKeys();
        } else if (ev == 6) b.drawstate = 2;
        else if (ev == 7) b.drawstate = 1;
        return;
    }
    if (cb == "MNU_KBD_DEFAULT" || cb == "MNU_KBD_REVERT")
        return standard(b, ev, [this, cb] {
            // 0x423490 / 0x423528.
            if (cb == "MNU_KBD_DEFAULT") for (int i = 0; i < 44; ++i) kbWork_[size_t(i)] = {kKeyRows[i].vk, kKeyRows[i].mouse};
            else kbWork_ = kbRevert_;
            refreshKeys();
            kbMessage(cb == "MNU_KBD_DEFAULT" ? "KBDDEFAULTSETTINGSTXT" : "KBDREVERTSETTINGSTXT", 180);
        });
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
        // 0x410498: only for a soldier in use and not renaming/creating:
        // plate 50 %, face keyed, frame, and the connector tree.
        if (!prof().valid() || !editing_.empty()) return;
        blitHalf(image(b.image), float(b.x), float(b.y), 156, 175);
        // The face (0x410498): face01..08, female01..03, 128x128 at +15, +14.
        const int f = std::clamp(int(prof().get(0x640)), 0, 10);
        char name[32];
        if (f < 8) std::snprintf(name, sizeof name, "FACE%02d.PCX", f + 1);
        else std::snprintf(name, sizeof name, "FEMALE%02d.PCX", f - 7);
        blit(file(name), float(b.x + 15), float(b.y + 14), 0, 0, 128, 128, false);
        const uint32_t g = 0x0000FF00;
        menuLine(439, 96, 566, 96, g); menuLine(439, 223, 566, 223, g); menuLine(439, 96, 439, 223, g); menuLine(566, 96, 566, 223, g);
        const float yr = float(91 + 26 * cur_);
        const MnuButton* ed = mnu_.button("but_ns_ed");
        const MnuButton* de = mnu_.button("but_ns_de");
        const float yE = ed ? float(ed->y + 9) : 273.f, yD = de ? float(de->y + 9) : 299.f;
        menuLine(380, yr, 397, yr, g);
        menuLine(397, yr, 397, yD, g);
        menuLine(397, 169, 423, 169, g);
        menuLine(397, yE, 423, yE, g);
        menuLine(397, yD, 423, yD, g);
        menuLine(397, yr, 397, 169, g);
        return;
    }
    if (cb == "MNU_NS_STATUSBOX") {
        // 0x410338: delete -> green outline (60,332)-(380,394), no plate;
        // create/rename -> ImageOperPlt3 50 %; else a soldier -> ImageOperPlt
        // 50 %. Texts teletyped 2 characters per tick (0x40ee6c): name, kills,
        // missions (left), multiplayer kills/deaths/wins (right); help texts
        // at (68,340) 310x52.
        auto t = [&](const char* k) { const std::string* s = menuText_.find("GroupMainMenu", k); return s ? *s : std::string(k); };
        const uint32_t g = 0x0000FF00;
        if (deleting_) {
            menuLine(60, 332, 380, 332, g); menuLine(60, 394, 380, 394, g); menuLine(60, 332, 60, 394, g); menuLine(380, 332, 380, 394, g);
        } else if (!editing_.empty()) {
            blitHalf(image("ImageOperPlt3"), 60, 332, 320, 61);
            if (editMode_ == 1) {
                const float yb = float(82 + 26 * cur_ + 9);
                menuLine(380, yb, 397, yb, g); menuLine(397, yb, 397, 341, g); menuLine(397, 341, 380, 341, g);
            }
        } else if (prof().valid()) {
            blitHalf(image(b.image), float(b.x), float(b.y), 520, 61);
            // The connector from the selected row's dot (0x40fbc0).
            const float ym = float(82 + 26 * cur_ + 9);
            menuLine(60, ym, 48, ym, g); menuLine(48, ym, 48, 361, g); menuLine(48, 361, 60, 361, g);
        }
        if (!transitionDone()) return;
        if (deleting_) {
            ensureTty(ttyNsHelp_, t("NSDELETEDIALOG"), 68, 340, 310, 52, "LCD10", 2);
            drawTty(ttyNsHelp_);
        } else if (!editing_.empty()) {
            ensureTty(ttyNsHelp_, t(editMode_ == 1 ? "NSCREATEHELP" : "NSDELETEHELP"), 68, 340, 310, 52, "LCD10", 2);
            drawTty(ttyNsHelp_);
        } else if (prof().valid()) {
            int done = 0;
            for (int c = 1; c <= 5; ++c)
                for (int i = 0; i < 16; ++i) done += prof().get(0x30 + c * 0x44 + 4 * i) != 0;
            char buf[256];
            std::snprintf(buf, sizeof buf, "%s: %s ~ %s: %d ~ %s: %d", t("NSOPERATIVENAME").c_str(), prof().name().c_str(),
                          t("NSSPKILLS").c_str(), int(prof().get(0x664)), t("NSSPSPK").c_str(), done);
            ensureTty(ttyNsLeft_, buf, 68, 343, 258, 60, "LCD10", 2);
            std::snprintf(buf, sizeof buf, "%s: %d ~ %s: %d ~ %s: %d", t("NSMULTIPLAYERKILLS").c_str(), int(prof().get(0x648)),
                          t("NSMPDEATHS").c_str(), int(prof().get(0x650)), t("NSMPSPK").c_str(), int(prof().get(0x65c)));
            ensureTty(ttyNsRight_, buf, 328, 343, 260, 60, "LCD10", 2);
            drawTty(ttyNsLeft_);
            drawTty(ttyNsRight_);
        }
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
        // Teletyped at (69,292) 240x122 (0x410f65); connector from the
        // selected campaign button (61,by+9)-(51,by+9)-(51,314)-(61,314) (0x410b60).
        if (transitionDone()) {
            const std::string* t = campsText_.find("Mission Statements", key);
            ensureTty(ttyCamp_, t ? *t : "Unable to find mission overview text!", 69, 292, 240, 122, "LCD10", 1);
            drawTty(ttyCamp_);
        }
        char cb2[32];
        std::snprintf(cb2, sizeof cb2, "but_ca_%d", camp_ + 1);
        if (const MnuButton* cbtn = mnu_.button(cb2)) {
            const float yb = float(cbtn->y + 9);
            menuLine(61, yb, 51, yb, 0x0000FF00);
            menuLine(51, yb, 51, 314, 0x0000FF00);
            menuLine(51, 314, 61, 314, 0x0000FF00);
        }
        return;
    }
    if (cb == "MNU_MIS_PL1") { drawBriefingMap(b); return; }
    if (cb == "MNU_MIS_SCROLLBAR" && b.type == 3) { briefLine_ = std::max(0, b.sliderPos); return; }   // 0x412b10
    if (cb == "MNU_KBD_SCROLLBAR" && b.type == 3) {
        kbScroll_ = std::clamp(b.sliderPos, 0, 33);
        if (kbScroll_ != kbLastSlider_) { kbLastSlider_ = kbScroll_; refreshKeys(); }
        return;
    }
    if (cb == "MNU_MIS_SCROLLMAP") {
        // The map sliders' owner draw (0x413a56): target = slider position.
        if (b.type == 2) mapTgt_[0] = std::clamp(b.sliderPos, 0, 768);
        else if (b.type == 3) mapTgt_[1] = std::clamp(b.sliderPos, 0, 768);
        return;
    }
    if (cb == "MNU_MIS_PL2") {
        blit(image(b.image), float(b.x), float(b.y), 0, 0, 228, 286, false);
        drawBriefingText(b);
        return;
    }
    if (cb == "MNU_MIS_PL3") {
        // Caption teletype (0x4184d4): Ttclick1..9 at fx*192/255 on every
        // draw until all is shown; word-wrapped LCD10 at +8 in w-16 x h-16.
        blit(image(b.image), float(b.x), float(b.y), 0, 0, 256, 56, false);
        if (captionShown_ < caption_.size()) {
            char t[16];
            std::snprintf(t, sizeof t, "TTCLICK%d.WAV", 1 + std::rand() % 9);
            sound(t, 192);
        }
        drawWrapped("LCD10", float(b.x + 8), float(b.y + 8), 240, 40, caption_.substr(0, captionShown_), green);
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
        // Teletyped on item change (0x41fae5), 1 character per tick.
        if (const std::string* t = menuText_.find("GroupMainMenu", e.text); t && transitionDone()) {
            ensureTty(ttyLo_, *t, 104, 268, 238, 123, "LCD10", 1);
            drawTty(ttyLo_);
        }
        return;
    }
    if (cb == "MNU_LO_CURRENTLOADOUT") {
        // 0x4204f4: the button's image, then at absolute positions the
        // heading in AVA12WG WhiteGreen (160,255,160), the three choices'
        // name keys (the text key without "TXT": LOM4M203 ...) in the
        // button's font LCD10, 0x00CC00, and the fixed laser and grenade
        // lines in 0x00AA00.
        const HudImage im = image(b.image);
        blit(im, float(b.x), float(b.y), 0, 0, float(im.width), float(im.height), false);
        auto txt = [&](const std::string& k, const char* fallback) {
            const std::string* t = menuText_.find("GroupMainMenu", k);
            return t ? *t : std::string(fallback);
        };
        auto nameKey = [](const GearEntry& e) { std::string k = e.text; if (k.size() > 3) k.resize(k.size() - 3); return k; };
        const std::string font = b.font.empty() ? "LCD10" : b.font;
        drawText("AVA12WG", 374, 298, txt("LOCURRENTLOADOUT", "Current Gear"), 0xFFA0FFA0);
        drawText(font, 374, 323, txt(nameKey(kPrimary[loSel_[0]]), kPrimary[loSel_[0]].name), 0xFF00CC00);
        drawText(font, 374, 335, txt(nameKey(kSecondary[loSel_[1]]), kSecondary[loSel_[1]].name), 0xFF00CC00);
        drawText(font, 374, 347, txt(nameKey(kSidearm[loSel_[2]]), kSidearm[loSel_[2]].name), 0xFF00CC00);
        drawText(font, 374, 359, txt("LOLASER", "Laser Designator"), 0xFF00AA00);
        drawText(font, 374, 371, txt("LOFGREN", "Fragmentation Grenade"), 0xFF00AA00);
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
    if (screen_ == Start) { if (leadFade_ < 0) { sound("BUMP1.WAV"); leadFade_ = 10; } return; }
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

void Frontend::writeScreenshot(int dw, int dh) {
    std::vector<uint8_t> px(size_t(dw) * size_t(dh) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream f(o_.screenshot, std::ios::binary);
    f << "P6\n" << dw << " " << dh << "\n255\n";
    for (int y = dh - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(&px[size_t(y) * size_t(dw) * 3]), dw * 3);
}

bool Frontend::playIntro(int& frame, bool& shotTaken) {
    // 0x4019d4: INTRO.KDV from DF.EXE's folder as a plain file (0x48b780(1),
    // 0x48bbd0), else <CD>\DFSETUP\INTRO.KDV (0x4891a0, "%s\%s\%s"); played
    // skippable (0x48ff60(name, 1)). No file: no intro.
    std::filesystem::path path;
    std::error_code ec;
    auto look = [&](const std::filesystem::path& dir) {
        if (!path.empty() || dir.empty()) return;
        for (auto& e : std::filesystem::directory_iterator(dir, ec))
            if (toUpper(e.path().filename().string()) == "INTRO.KDV") path = e.path();
    };
    if (const auto exe = findGameExe(o_.gameDir); !exe.empty()) look(exe.parent_path());
    for (auto& e : std::filesystem::directory_iterator(o_.gameDir, ec))
        if (toUpper(e.path().filename().string()) == "DFSETUP") look(e.path());
    KdvVideo v;
    if (path.empty() || !v.open(path)) {
        if (!path.empty()) std::fprintf(stderr, "%s: %s\n", path.string().c_str(), v.error().c_str());
        return true;
    }
    // 0x486b27: a 320x240 screen for videos up to 320 wide, else 640x480;
    // the frame at the left edge, centred vertically (0x486b7d), its own
    // width the row stride.
    const int sw = v.width() > 320 ? 640 : 320, sh = v.width() > 320 ? 480 : 240;
    const int top = (sh - v.height()) / 2;
    HudImage tex;
    std::vector<uint8_t> rgba;
    kdvSound_.clear();
    // Testing (--screenshot): a clock that moves only while the player
    // waits, so frame N is always the same picture (and OPENDF_AUDIODUMP
    // the same sound).
    const bool fixed = !o_.screenshot.empty();
    const Uint64 freq = SDL_GetPerformanceFrequency(), c0 = SDL_GetPerformanceCounter();
    int64_t virt = 0, heard = 0;
    // DF.EXE's clock (0x485f98, 0x486014): 120 ticks a second.
    auto now = [&]() { return fixed ? virt : int64_t(double(SDL_GetPerformanceCounter() - c0) * 120.0 / double(freq)); };
    auto hear = [&]() {   // OPENDF_AUDIODUMP: the sound up to the clock
        const int64_t t = now();
        if (t > heard) { audio_.advance(double(t - heard) / 120.0); heard = t; }
    };
    int keys = 0;   // 0x489e2c: the key queue starts empty
    // Testing: OPENDF_INTROKEY=N presses a key after N shown frames (with
    // --screenshot), OPENDF_INTROCLICK=N clicks the left button.
    const int testKey = fixed && std::getenv("OPENDF_INTROKEY") ? std::atoi(std::getenv("OPENDF_INTROKEY")) : -1;
    const int testClick = fixed && std::getenv("OPENDF_INTROCLICK") ? std::atoi(std::getenv("OPENDF_INTROCLICK")) : -1;
    int shown = 0;
    bool quit = false, active = true;
    const int period = std::max(1, v.framePeriod());
    int64_t target = now();   // 0x487069
    int late = 0;             // 0x486a7c
    for (;;) {
        // 0x486138: the message pump. Keys (WM_KEYDOWN / WM_SYSKEYDOWN,
        // repeats too) go into the key queue (0x485a3a); a left click goes
        // to the Start Screen's mouse hook, registered before the intro
        // (0x418b91 -> 0x418b50), which marks DF_LEAD as done (0x4d9120).
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (fullscreenEvent(win_, e)) continue;
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_KEYDOWN) ++keys;
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) introLeadSkip_ = true;
            else if (e.type == SDL_WINDOWEVENT && !fixed) {
                // WM_SETFOCUS / WM_KILLFOCUS (0x48580b, 0x485c64): no frames
                // while the window is not active (0x490015).
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) active = true;
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) active = false;
            }
        }
        if (shown == testKey) ++keys;
        if (shown == testClick) introLeadSkip_ = true;
        if (quit) break;                   // 0x490024: the quit flag
        if (keys > 0) { --keys; break; }   // 0x48ffcc: one key from the queue skips
        hear();
        if (!active) { SDL_Delay(10); continue; }
        // 0x486c15: the end, or an invalid chunk (0x49000b; INTRO.KDV ends
        // that way: its two clips are followed by filler that is not a
        // chunk). DF.EXE shows no message.
        if (v.next() != KdvVideo::Step::Frame) break;
        // 0x486c48: each frame is due `period` ticks after the last. Late:
        // not shown (its sound still queued), up to five in a row; the sixth
        // is shown and the schedule restarts from now. Early: wait until 3/4
        // of a period before it is due, then show it.
        target += period;
        bool show = true;
        if (target - now() <= 0) {
            if (++late < 6) show = false;
            else target = now();
        } else {
            const int64_t at = target - ((int64_t(period) * 3) >> 2);
            if (fixed) virt = std::max(virt, at + 1);
            while (at - now() >= 0) SDL_Delay(1);
            hear();
        }
        if (show) {
            late = 0;
            ++shown;
            v.takePaletteChanged();   // 0x486cab: a new palette with this flip
            v.toRgba(rgba);
            hud().updateImage(tex, v.width(), v.height(), rgba);
            int dw, dh;
            SDL_GL_GetDrawableSize(win_, &dw, &dh);
            const float sc = std::min(float(dw) / float(sw), float(dh) / float(sh));
            const float ox = (float(dw) - float(sw) * sc) * 0.5f, oy = (float(dh) - float(sh) * sc) * 0.5f;
            glViewport(0, 0, dw, dh);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            hud().begin(dw, dh);
            hud().image(tex.texture, ox, oy + float(top) * sc, float(v.width()) * sc, float(v.height()) * sc);
            hud().end();
            if (fixed && ++frame >= o_.frames) {
                writeScreenshot(dw, dh);
                shotTaken = true;
                break;
            }
            SDL_GL_SwapWindow(win_);
        }
        // 0x486d07: this frame's sound into the stream queue.
        KdvVideo::Audio a;
        if (v.takeAudio(kdvSound_.size(), a)) kdvSound_.push(std::move(a));
    }
    // 0x48ffe4 -> 0x48707f -> 0x497eaf: closing the player empties the
    // stream queue. Keys still queued reach DF_LEAD's update (0x418beb).
    kdvSound_.clear();
    if (tex.texture) glDeleteTextures(1, &tex.texture);
    if (keys > 0) introLeadSkip_ = true;
    if (quit) {
        SDL_Event q{};
        q.type = SDL_QUIT;
        SDL_PushEvent(&q);
    }
    return !quit && !shotTaken;
}

int Frontend::run() {
    if (!loadData()) { std::fprintf(stderr, "cannot read DF.MNU\n"); return 1; }
    loadProfiles();
    // df.cfg: read at start (0x454e10), written at exit (0x40dc88).
    cfg_.load(o_.gameDir / "df.cfg");
    options_ = uint32_t(cfg_.get("attributes"));
    gpsIcons_ = std::clamp(cfg_.get("gpsicon"), 0, 2);
    music_.setVolume(std::clamp(cfg_.get("cd_volume"), 0, 255));
    // DF.EXE's integer mixer, with the mode-3 hook set at start-up
    // (0x4014a5) and reset when a mission ends (0x40491b / 0x473162).
    music_.setIntegerUnits(true);
    audio_.setDfMixer(true, !missionPlayed_);
    audio_.setDfGame(1);   // DF.EXE's WAV conversion (0x48cfb0) and mixer
    audio_.openDevice();
    // DF.EXE opens DFMUSIC.SBF from its own folder (0x4b81b0).
    if (const auto exe = findGameExe(o_.gameDir); !exe.empty()) {
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(exe.parent_path(), ec))
            if (toUpper(e.path().filename().string()) == "DFMUSIC.SBF") musicLoaded_ = music_.load(vfs_, e.path());
    }
    if (!musicLoaded_) std::fprintf(stderr, "no menu music (DFMUSIC.SBF / DFMUSIC.BIN)\n");
    audio_.setStream([this](float* out, int frames) {
        kdvSound_.mix(out, frames);
        music_.mix(out, frames);
    });
    if (!openWindow()) return 1;
    int frame = 0;
    // The Start Screen's init (0x418b6c) plays the intro first (0x418b99),
    // then shows DF_LEAD. `play intro` is the same start, for tests.
    if (o_.startScreen.empty() || o_.startScreen == "intro") {
        bool shot = false;
        playIntro(frame, shot);
        if (shot) {
            music_.stop();
            closeWindow();
            return 0;
        }
    }
    enter(Start);
    next_ = Start;
    if (introLeadSkip_ && leadFade_ < 0) { sound("BUMP1.WAV"); leadFade_ = 10; }
    {
        const std::string s = o_.startScreen;
        Screen first = s == "soldier" ? Soldier : s == "main" ? Main : s == "campaign" ? Campaign : s == "briefing" ? Briefing
                     : s == "loadout" ? Loadout : s == "difficulty" ? Options3 : s == "settings" ? Options1 : s == "macros" ? Macros : s == "keyboard" ? Keyboard : s == "credits" ? Credits : s == "debrief" ? Debrief : Start;
        if (first == Briefing || first == Loadout || first == Debrief) { enter(Campaign); }
        if (first != Start) { enter(first); next_ = first; music_.start(); musicVar(0, 0); musicVar(1, first == Soldier ? 4 : first == Main ? 0 : first == Campaign ? 1 : first == Briefing ? 6 : first == Loadout ? 7 : first == Options3 ? 11 : first == Options1 ? 3 : 0); }
    }
    Uint64 last = SDL_GetPerformanceCounter();
    double clock = 0;
    while (screen_ != Quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (fullscreenEvent(win_, e)) continue;
            if (e.type == SDL_QUIT) { next_ = Quit; break; }
            if (e.type == SDL_TEXTINPUT && !editing_.empty()) textInput(e.text.text);
            if (screen_ == Keyboard && kbCapture_ != 0) {
                // Waiting for a key or mouse button: normal menu input is skipped.
                if (e.type == SDL_KEYDOWN && !e.key.repeat) kbCaptured(1, Controls::toVirtualKey(e.key.keysym.scancode));
                else if (e.type == SDL_MOUSEBUTTONDOWN)
                    kbCaptured(2, e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : 3);
                continue;
            }
            if (e.type == SDL_KEYDOWN) key(e.key);
            if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                int mx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x, my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
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
                if (screen_ == Start) {
                    if (e.type == SDL_MOUSEBUTTONDOWN && leadFade_ < 0) { sound("BUMP1.WAV"); leadFade_ = 10; }
                    continue;
                }
                // Slider drag (0x40728c): pos = (mx - x - knobW/2) / width * size,
                // clamped; the mouse stays captured until the button is up.
                auto dragTo = [&](MnuButton& sl) {
                    // V (0x407388): pos = (my - y - knobH/6) / height * size.
                    const HudImage im = image(sl.image);
                    const float f = sl.type == 3 ? (sl.height > 0 ? float(mouseY_ - sl.y - im.height / 6) / float(sl.height) : 0.f)
                                                 : (sl.width > 0 ? float(mouseX_ - sl.x - im.width / 2) / float(sl.width) : 0.f);
                    sl.sliderPos = std::clamp(int(f * float(sl.sliderSize)), 0, sl.sliderSize);
                    event(sl, 1);
                };
                if (MnuButton* sl = mnu_.button(dragging_)) {
                    if (e.type == SDL_MOUSEMOTION) dragTo(*sl);
                    else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) { event(*sl, 2); dragging_.clear(); }
                    continue;
                }
                MnuButton* b = hit(mouseX_, mouseY_);
                if (b && (b->type == 2 || b->type == 3) && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && !(b->attrib & 2) && b->drawstate != 4) {
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
            audio_.advance(0.016);   // OPENDF_AUDIODUMP (tests)
            ++screenTicks_;
            static const int leadTicks = std::getenv("OPENDF_LEADTICKS") ? std::atoi(std::getenv("OPENDF_LEADTICKS")) : 600;   // testing
            if (screen_ == Start && screenTicks_ >= leadTicks && leadFade_ < 0) { sound("BUMP1.WAV"); leadFade_ = 10; }
            if (wipeActive_ && wipeCaptured_ && ++wipeCount_ >= 4) {
                wipeCount_ = 0;
                if (++wipeStep_ > 5) wipeActive_ = false;
            }
            if (screen_ == End && endTicks_ >= 0 && --endTicks_ < 0) next_ = Quit;
            if (screen_ == Credits && next_ == Credits) tickCredits();
            if (screen_ == Briefing && captionShown_ < caption_.size()) ++captionShown_;
            if (transitionDone()) tickTtys();
            if (screen_ == Keyboard) {
                if (transitionDone()) kbShown_ = std::min(kbMsg_.size(), kbShown_ + 2);   // 2 per tick (0x422dbd + 0x422e38)
                if (kbTimer_ > 0 && --kbTimer_ == 0) kbMessage(kbMode_ == 0 ? "KBDKEYMOD" : kbMode_ == 1 ? "KBDMOUSEMOD" : "KBDDELETEMOD", -1);
            }   // 1 character per tick (0x418408)
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
                g.female = prof().get(0x640) >= 8;
                g.gpsIcons = gpsIcons_;
                g.attributes = options_;
                g.crosshairs = cfg_.get("crosshairs") != 0;
                g.flipMouse = cfg_.get("flipmouse") != 0;
                g.mouseScale = std::clamp(cfg_.get("mousescale"), 0, 255);
                g.dialogVolume = std::clamp(cfg_.get("dialog_volume"), 0, 255);
                // 0x41cca0: the profile's 44 bindings over the game table;
                // mouse 1/2/3 -> mask 1/2/0x10; Fire also sets attack_2.
                g.bindings.clear();
                for (int i = 0; i < 44; ++i) {
                    const uint32_t vk = uint32_t(prof().get(0x6a0 + 12 * i)) & 0xFFFF, m = uint32_t(prof().get(0x6a4 + 12 * i));
                    const uint32_t mask = m == 1 ? 1 : m == 2 ? 2 : m == 3 ? 0x10 : 0;
                    g.bindings.push_back({kKeyRows[i].game, vk | mask << 16});
                    if (i == 8) g.bindings.push_back({"attack_2", vk | mask << 16});
                }
                uint32_t flags = uint32_t(prof().get(0x70 + camp_ * 0x44));
                outcome_ = MissionOutcome{};
                g.mixerHook = !missionPlayed_;
                runGame(vfs_, g, &outcome_, &flags);
                missionPlayed_ = true;
                audio_.setDfMixer(true, false);
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
        texW_ = dw;
        texH_ = dh;
        if (screen_ == End && haveLast_) {
            // 0x41cb40 only presents: the last frame stays (dfsplash.pcx is never drawn).
            drawCaptured(lastTex_, 0, 0, 640, 480);
            hud().end();
            SDL_GL_SwapWindow(win_);
            continue;
        }
        drawMenu();
        if (screen_ == Credits)
            // 0x41a3a8: after the buttons, each live text at its point, flags
            // 4 centred (1 left, 2 right).
            for (const CreditText& t : creditSlots_)
                if (t.active) drawText(t.font, float(t.x), float(t.y), t.text, t.color, (t.flags & 4) ? 1 : (t.flags & 2) ? 2 : 0);
        if (screen_ == Keyboard) {
            // 0x423048: frame (459,274)-(590,387) and the teletyped message.
            const uint32_t g = 0xFF00FF00;
            menuLine(459, 274, 590, 274, g);
            menuLine(590, 274, 590, 387, g);
            menuLine(590, 387, 459, 387, g);
            menuLine(459, 387, 459, 274, g);
            drawWrapped("LCD10", 467, 282, 116, 98, kbMsg_.substr(0, kbShown_), g);
        }
        if (screen_ == Options1 || screen_ == Options3 || screen_ == Macros || screen_ == Keyboard) {
            // The settings pages' separator under the tab strip: a green line
            // (37,108)-(600,108) after the buttons (0x40b00c, 0x41c4fc, 0x420eac).
            menuLine(37, 108, 600, 108, 0x0000FF00);
        }
        if (screen_ == Main) {
            // The hover help (0x40c788, 0x40ce70): the hovered button's
            // MM*TEXT teletyped at (180,332) 288x53 in LCD10; hover-out clears
            // it; connector from the button row (yr = 30 row + 113); then
            // ImageMainPlt at (172,324) blended src/2 + 3/4 dst over the text.
            static const std::pair<const char*, const char*> help[] = {
                {"BUT_MAINNS", "MMNEWSOLDIERTEXT"}, {"BUT_MAINSP", "MMSINGLEPLAYERTEXT"}, {"BUT_MAINMP", "MMMULTIPLAYERTEXT"},
                {"BUT_MAINOP", "MMGAMEOPTIONSTEXT"}, {"BUT_MAINCR", "MMGAMECREDITSTEXT"}, {"BUT_MAINUP", "MMUPDATETEXT"},
                {"BUT_MAINEX", "MMEXITTEXT"}};
            int row = -1;
            const char* key = nullptr;
            for (const auto& [bn, k] : help)
                if (hover_ == bn)
                    if (const MnuButton* hb = mnu_.button(bn); hb && (hb->attrib & 0x20) && hb->drawstate != 4) { row = hb->userid; key = k; }
            if (row != helpRow_) { ttyHelp_ = Tty{}; helpRow_ = row; }
            if (key && transitionDone()) {
                const std::string* t = menuText_.find("GroupMainMenu", key);
                ensureTty(ttyHelp_, t ? *t : key, 180, 332, 288, 53, "LCD10", 1);
                drawTty(ttyHelp_);
                const float yr = float(30 * row + 113);
                menuLine(172, yr, 162, yr, 0x0000FF00);
                menuLine(162, yr, 162, 356, 0x0000FF00);
                menuLine(162, 356, 171, 356, 0x0000FF00);
                blit(image("ImageMainPlt"), 172, 324, 0, 0, 296, 61, true);
            }
        }
        if (screen_ == Debrief) {
            // 0x423b78: "Mission Statistics ~ Number of Kills : n ~ ~ Win<m>",
            // or "Mission Statistics ~ ~ Lose<m>"; teletype box (86, 104)
            // 468x272, AVA10B.
            auto t = [&](const char* k) { const std::string* s = menuText_.find("GroupMainMenu", k); return s ? *s : std::string(k); };
            char sec[32], key[32];
            std::snprintf(sec, sizeof sec, "Briefing%d", mis_ + 1);
            std::snprintf(key, sizeof key, outcome_.won ? "Win%d" : "Lose%d", mis_ + 1);
            const std::string* body = campText_.find(sec, key);
            std::string text = t("DEBFMISSIONSTATS");
            if (outcome_.won) text += " ~ " + t("DEBFKILLS") + " : " + std::to_string(outcome_.kills) + " ~ ~ ";
            else text += " ~ ~ ";
            text += body ? *body : "Unable to find mission debriefing text";
            if (transitionDone()) {   // teletyped once from init, 1 char/tick (0x4240c3)
                ensureTty(ttyDebf_, text, 86, 104, 468, 272, "AVA10B", 1);
                drawTty(ttyDebf_);
            }
        }
        if (wipeActive_) drawWipe();
        hud().end();
        captureTo(lastTex_);
        haveLast_ = true;
        if (!o_.screenshot.empty() && ++frame >= o_.frames) {
            writeScreenshot(dw, dh);
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
