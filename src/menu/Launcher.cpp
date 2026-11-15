#include "Launcher.h"

#include "../core/Pff.h"
#include "../formats/Image.h"
#include "../formats/Pcx.h"
#include "../render/Gl.h"
#include "../render/Hud.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;

namespace df {

// ---------------------------------------------------------------- games

GameKind identifyGame(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return GameKind::Unknown;
    Vfs vfs;
    std::string err;
    if (!vfs.mountGameDir(dir, &err)) return GameKind::Unknown;
    // The game's own archive names it (docs/OTHER-GAMES.md); Delta Force 2
    // and Delta Force by their menu script / first terrain.
    for (const auto& a : vfs.archives()) {
        std::string n = a->path().filename().string();
        for (auto& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (n == "DFLW.PFF") return GameKind::LandWarrior;
        if (n == "SMALL.PFF") return GameKind::TaskForceDagger;
    }
    if (vfs.exists("DF2.MNU")) return GameKind::DeltaForce2;
    if (vfs.exists("DFG1.TRN") && vfs.exists("DF.MNU")) return GameKind::DeltaForce;
    return GameKind::Unknown;
}

const char* gameTitle(GameKind kind) {
    switch (kind) {
    case GameKind::DeltaForce: return "Delta Force";
    case GameKind::DeltaForce2: return "Delta Force 2";
    case GameKind::LandWarrior: return "Delta Force: Land Warrior";
    case GameKind::TaskForceDagger: return "Delta Force: Task Force Dagger";
    default: return "Unknown game";
    }
}

bool gamePlayable(GameKind kind) { return kind == GameKind::DeltaForce || kind == GameKind::DeltaForce2; }

// ---------------------------------------------------------------- settings

fs::path LauncherSettings::defaultFile() {
    fs::path dir;
    if (char* p = SDL_GetPrefPath("", "OpenDF")) {
        dir = p;
        SDL_free(p);
    }
    return dir / "opendf.ini";
}

bool LauncherSettings::load(const fs::path& file) {
    std::ifstream in(file);
    if (!in) return false;
    games.clear();
    last.clear();
    std::string line, section;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { section = line; continue; }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (section == "[games]" && key == "game" && !value.empty()) games.emplace_back(fs::u8path(value));
        else if (section == "[launcher]" && key == "last") last = fs::u8path(value);
    }
    return true;
}

bool LauncherSettings::save(const fs::path& file) const {
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file);
    if (!out) return false;
    out << "; OpenDF settings (written by the launcher)\n[games]\n";
    for (const auto& g : games) out << "game=" << g.u8string() << "\n";
    out << "\n[launcher]\nlast=" << last.u8string() << "\n";
    return bool(out);
}

namespace {

// ---------------------------------------------------------------- fonts

// OpenDF's own 5x7 capitals, for when no Delta Force game with menu art has
// been added yet (rows top-down, bit 4 = left column).
struct BuiltinGlyph { char c; uint8_t rows[7]; };
const BuiltinGlyph kBuiltin[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}}, {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'J', {7, 2, 2, 2, 2, 18, 12}},      {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 17, 25, 21, 19, 17, 17}}, {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}}, {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},       {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},  {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 17, 10, 4, 4, 4}},    {'Z', {31, 1, 2, 4, 8, 16, 31}},     {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},      {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {31, 2, 4, 2, 1, 17, 14}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 30, 1, 1, 17, 14}},   {'6', {6, 8, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},       {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 2, 12}},
    {'.', {0, 0, 0, 0, 0, 12, 12}},      {',', {0, 0, 0, 0, 12, 4, 8}},       {':', {0, 12, 12, 0, 12, 12, 0}},
    {'/', {1, 1, 2, 4, 8, 16, 16}},      {'\\', {16, 16, 8, 4, 2, 1, 1}},     {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'_', {0, 0, 0, 0, 0, 0, 31}},       {'\'', {4, 4, 8, 0, 0, 0, 0}},       {'(', {2, 4, 8, 8, 8, 4, 2}},
    {')', {8, 4, 2, 2, 2, 4, 8}},        {'!', {4, 4, 4, 4, 4, 0, 4}},        {'?', {14, 17, 1, 2, 4, 0, 4}},
    {'~', {0, 0, 8, 21, 2, 0, 0}},       {'+', {0, 4, 4, 31, 4, 4, 0}},       {'=', {0, 0, 31, 0, 31, 0, 0}},
    {'>', {8, 4, 2, 1, 2, 4, 8}},        {'<', {2, 4, 8, 16, 8, 4, 2}},       {'"', {10, 10, 0, 0, 0, 0, 0}},
};

SheetFont builtinFont(Hud& hud) {
    SheetFont f;
    const int W = 16 * 6, H = 6 * 8;
    std::vector<uint8_t> rgba(size_t(W) * H * 4, 0);
    for (const auto& g : kBuiltin) {
        const int code = static_cast<unsigned char>(g.c), cx = (code % 16) * 6, cy = (code / 16 - 2) * 8;
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 5; ++x)
                if (g.rows[y] & (16 >> x)) {
                    const size_t i = (size_t(cy + y) * W + size_t(cx + x)) * 4;
                    rgba[i] = rgba[i + 1] = rgba[i + 2] = rgba[i + 3] = 255;
                }
        f.glyphs[code] = {cx, cy, 5, 7};
    }
    f.glyphs[' '] = {0, 0, 3, 7};
    f.tex = hud.makeImage(W, H, rgba);
    f.height = 7;
    f.ok = true;
    return f;
}

// A Delta Force menu font sheet (DF.EXE 0x442338, as Frontend::font): bands
// of 16 characters between full-width lines of palette index 1.
SheetFont df1Font(Hud& hud, Vfs& vfs, const std::string& name) {
    SheetFont f;
    std::vector<uint8_t> data;
    IndexedImage img;
    if (!vfs.read(name, data) || !decodePcx(data, img)) return f;
    const int W = img.width, H = img.height;
    auto px = [&](int x, int y) { return img.pixels[size_t(y) * size_t(W) + size_t(x)]; };
    int period = 0;
    for (int y = 0; y < H && !period; ++y) {
        bool full = true;
        for (int x = 0; x < W && full; ++x) full = px(x, y) == 1;
        if (full) period = y;
    }
    std::vector<uint8_t> rgba(size_t(W) * size_t(H) * 4, 0);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const uint8_t v = img.pixels[i];
        if (v >= 8 || v == 4) rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = rgba[i * 4 + 3] = 255;
    }
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
            f.glyphs[code++] = {start, y0, x - start, y1 - y0};
        }
    }
    f.tex = hud.makeImage(W, H, rgba);
    f.ok = f.height > 0;
    return f;
}

Color rgb(int r, int g, int b) { return {float(r) / 255.f, float(g) / 255.f, float(b) / 255.f, 1.f}; }

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// ---------------------------------------------------------------- the screen

struct Font {
    SheetFont sheet;
    float scale = 1;
    Color normal{}, lit{}, disabled{};
};

// Where things go on the 640x480 screen in each look.
struct Theme {
    enum Kind { Builtin, Df1, Df2 } kind = Builtin;
    HudImage background, button;   // the button: 4 stacked frames (normal, lit, selected, disabled)
    Font title, item, small, info;
    Color frame{};                 // outline colour (the add box, the info box)
    float titleX = 0, titleY = 0;
    int titleAlign = 0;
    float itemX = 172, itemY = 104, itemStep = 30, itemW = 296, itemH = 19;
    int visible = 7;
    float infoX0 = 120, infoY0 = 318, infoX1 = 520, infoY1 = 410;
    bool infoBox = false;
    float bottomY = 434;
    float addX = 70, removeX = 320, exitX = 590;   // centres (Builtin, Df1) or edges (Df2)
};

class Launcher {
public:
    explicit Launcher(const LauncherOptions& o) : o_(o) {}
    bool run(fs::path* chosen);

private:
    struct Entry { fs::path dir; GameKind kind = GameKind::Unknown; bool present = false; std::string label; };
    enum class Mode { List, Add };
    enum Hit { None = -100, AddBtn = -1, RemoveBtn = -2, ExitBtn = -3 };

    bool openWindow();
    void closeWindow();
    void loadEntries();
    void relabel();
    void pickTheme();
    void loadTheme(GameKind kind, const fs::path& dir);
    void save();
    bool tryAdd(std::string path);
    void removeSelected();
    void choose(int index);

    void draw();
    void drawList();
    void drawAdd();
    void drawBottom(const char* left, const char* middle, const char* right);
    int hitTest(float x, float y) const;
    float textWidth(const Font& f, const std::string& s) const;
    void text(const Font& f, float x, float y, const std::string& s, Color c, int align = 0);
    std::string fitTail(const Font& f, const std::string& s, float w) const;
    void blit(const HudImage& im, float x, float y, float sx, float sy, float sw, float sh);
    void outline(float x0, float y0, float x1, float y1, Color c);
    void fill(float x0, float y0, float x1, float y1, Color c);

    LauncherOptions o_;
    fs::path settingsFile_;
    LauncherSettings settings_;
    std::vector<Entry> entries_;
    Theme t_;
    Vfs vfs_;   // the game whose art the launcher wears
    std::unique_ptr<Hud> hud_;
    SDL_Window* win_ = nullptr;
    SDL_GLContext ctx_ = nullptr;
    float scale_ = 1, offX_ = 0, offY_ = 0;
    float mouseX_ = -1, mouseY_ = -1;
    int sel_ = 0, top_ = 0, hover_ = None, pendingRemove_ = -1;
    Mode mode_ = Mode::List;
    std::string input_, message_;
    bool messageBad_ = false;
    int chosen_ = -1;
    bool quit_ = false;
    uint32_t ticks_ = 0;
};

void Launcher::loadEntries() {
    entries_.clear();
    for (const auto& g : settings_.games) {
        Entry e;
        e.dir = g;
        std::error_code ec;
        e.present = fs::is_directory(g, ec);
        e.kind = e.present ? identifyGame(g) : GameKind::Unknown;
        entries_.push_back(e);
    }
    relabel();
}

void Launcher::relabel() {
    // Two copies of the same game: tell them apart by folder name.
    for (auto& e : entries_) {
        int same = 0;
        for (const auto& o : entries_) same += o.kind == e.kind;
        e.label = e.present ? gameTitle(e.kind) : e.dir.filename().string();
        if (e.present && same > 1) e.label += " (" + e.dir.filename().string() + ")";
    }
}

void Launcher::save() {
    settings_.games.clear();
    for (const auto& e : entries_) settings_.games.push_back(e.dir);
    if (!settings_.save(settingsFile_))
        std::fprintf(stderr, "Could not write %s\n", settingsFile_.string().c_str());
}

// The launcher wears the look of the game played last, else of Delta Force,
// else of Delta Force 2; with neither added, OpenDF's own plain screen.
void Launcher::pickTheme() {
    const Entry* pick = nullptr;
    for (const auto& e : entries_)
        if (e.present && gamePlayable(e.kind) && !settings_.last.empty() && e.dir == settings_.last) pick = &e;
    for (GameKind k : {GameKind::DeltaForce, GameKind::DeltaForce2})
        for (const auto& e : entries_)
            if (!pick && e.present && e.kind == k) pick = &e;
    loadTheme(pick ? pick->kind : GameKind::Unknown, pick ? pick->dir : fs::path());
}

void Launcher::loadTheme(GameKind kind, const fs::path& dir) {
    Hud& hud = *hud_;
    t_ = Theme{};
    vfs_ = Vfs{};
    std::string err;
    if (kind != GameKind::Unknown && !vfs_.mountGameDir(dir, &err)) kind = GameKind::Unknown;
    if (kind == GameKind::DeltaForce) {
        // DF.MNU: ImageTemplate (df_bkgd.pcx), ImageBtn296 (btn_296.pcx, 4
        // frames, drawn with the 0x407e50 blend), the main menu's fonts
        // (AVA14B title at 165,27; AVA12B buttons from 172,104 every 30;
        // AVA10B at the bottom, y 434) and its colours TrueGreen / LightRed
        // / DarkGrey.
        t_.kind = Theme::Df1;
        t_.background = hud.loadImage(vfs_, "DF_BKGD.PCX", false);
        t_.button = hud.loadImage(vfs_, "BTN_296.PCX", false);
        const Color green = rgb(0, 255, 0), red = rgb(255, 128, 64), grey = rgb(0, 100, 0);
        t_.title = {df1Font(hud, vfs_, "AVA_14B.PCX"), 1, green, red, grey};
        t_.item = {df1Font(hud, vfs_, "AVA_12B.PCX"), 1, green, red, grey};
        t_.small = {df1Font(hud, vfs_, "AVA_10B.PCX"), 1, green, red, grey};
        t_.info = {df1Font(hud, vfs_, "LCD_10.PCX"), 1, green, red, grey};
        t_.frame = grey;
        t_.titleX = 165, t_.titleY = 27, t_.titleAlign = 1;
        t_.itemX = 172, t_.itemY = 104, t_.itemStep = 30, t_.visible = 7;
        t_.itemW = t_.button.width ? float(t_.button.width) : 296.f;
        t_.itemH = t_.button.height ? float(t_.button.height) / 4.f : 19.f;
        t_.infoX0 = 90, t_.infoY0 = 318, t_.infoX1 = 550, t_.infoY1 = 410;
        t_.bottomY = 434, t_.addX = 70, t_.removeX = 320, t_.exitX = 590;
        if (t_.background.texture && t_.item.sheet.ok) return;
    } else if (kind == GameKind::DeltaForce2) {
        // DF2.MNU: ImageMenuFrame (df2_main.jpg), ImageBtn01 (df2_bt01.pcx,
        // 4 frames, index 0 keyed), the Main menu's STP26 title at 37,16,
        // FRK14N buttons from 172,80 every 30, the mm_Rect box at
        // 170,330-468,388 (0xCA4409) with LCD10N text and FRK12N at the
        // bottom (EXIT_GAME right-aligned at 585,432); AMBER_* colours.
        t_.kind = Theme::Df2;
        std::vector<uint8_t> data;
        RgbaImage im;
        if (vfs_.read("DF2_MAIN.JPG", data) && decodeRgbaImage(data, im)) t_.background = hud.makeImage(im.width, im.height, im.rgba);
        t_.button = hud.loadImage(vfs_, "DF2_BT01.PCX", false, true);
        const Color normal = rgb(240, 98, 28), lit = rgb(255, 128, 0), disabled = rgb(132, 64, 48);
        t_.title = {hud.loadSheetFont(vfs_, "STP_26.PCX"), 1, rgb(49, 24, 16), rgb(49, 24, 16), rgb(49, 24, 16)};
        t_.item = {hud.loadSheetFont(vfs_, "FRK_14.PCX"), 1, normal, lit, disabled};
        t_.small = {hud.loadSheetFont(vfs_, "FRK_12.PCX"), 1, normal, lit, disabled};
        t_.info = {hud.loadSheetFont(vfs_, "LCD_10.PCX"), 1, normal, lit, disabled};
        t_.frame = rgb(0xCA, 0x44, 0x09);
        t_.titleX = 37, t_.titleY = 16, t_.titleAlign = 0;
        t_.itemX = 172, t_.itemY = 80, t_.itemStep = 30, t_.visible = 8;
        t_.itemW = t_.button.width ? float(t_.button.width) : 296.f;
        t_.itemH = t_.button.height ? float(t_.button.height) / 4.f : 20.f;
        t_.infoX0 = 120, t_.infoY0 = 330, t_.infoX1 = 520, t_.infoY1 = 400, t_.infoBox = true;
        t_.bottomY = 432, t_.addX = 60, t_.removeX = 220, t_.exitX = 585;
        if (t_.background.texture && t_.item.sheet.ok) return;
    }
    // OpenDF's own: black, Delta Force's menu green, the built-in capitals.
    t_ = Theme{};
    vfs_ = Vfs{};
    const SheetFont b = builtinFont(hud);
    const Color green = rgb(0, 255, 0), red = rgb(255, 128, 64), grey = rgb(0, 100, 0);
    t_.title = {b, 3, green, red, grey};
    t_.item = {b, 2, green, red, grey};
    t_.small = {b, 2, green, red, grey};
    t_.info = {b, 1, green, red, grey};
    t_.frame = grey;
    t_.titleX = 320, t_.titleY = 30, t_.titleAlign = 1;
    t_.itemX = 172, t_.itemY = 104, t_.itemStep = 30, t_.itemW = 296, t_.itemH = 22, t_.visible = 7;
    t_.infoX0 = 90, t_.infoY0 = 318, t_.infoX1 = 550, t_.infoY1 = 410, t_.infoBox = true;
    t_.addX = 90, t_.removeX = 320, t_.exitX = 550;
}

bool Launcher::openWindow() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (o_.screenshot.empty() ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN);
    win_ = SDL_CreateWindow("OpenDF", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, o_.width, o_.height, flags);
    if (!win_) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    ctx_ = SDL_GL_CreateContext(win_);
    const char* missing = nullptr;
    if (!ctx_ || !loadGl(&missing)) { std::fprintf(stderr, "OpenGL 3.3: %s\n", ctx_ ? missing : SDL_GetError()); return false; }
    SDL_GL_SetSwapInterval(1);
    hud_ = std::make_unique<Hud>();
    std::string err;
    Vfs none;
    hud_->init(none, &err);   // the shaders; its HUD font is not used here
    SDL_ShowCursor(SDL_ENABLE);
    return true;
}

void Launcher::closeWindow() {
    hud_.reset();
    if (ctx_) SDL_GL_DeleteContext(ctx_);
    if (win_) SDL_DestroyWindow(win_);
    ctx_ = nullptr;
    win_ = nullptr;
}

bool Launcher::tryAdd(std::string path) {
    // Pasted paths may be quoted or shell-escaped; "~" is the home folder.
    auto trim = [](std::string& s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        size_t i = 0;
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        s.erase(0, i);
    };
    trim(path);
    if (path.size() >= 2 && (path.front() == '"' || path.front() == '\'') && path.back() == path.front()) path = path.substr(1, path.size() - 2);
    if (path.empty()) { message_ = "Type the folder of a Delta Force game first"; messageBad_ = true; return false; }
    if (path[0] == '~')
        if (const char* home = std::getenv("HOME")) path = std::string(home) + path.substr(1);
    std::error_code ec;
    fs::path dir = fs::u8path(path);
    if (!fs::is_directory(dir, ec) && path.find('\\') != std::string::npos) {
        std::string unescaped;
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '\\' && i + 1 < path.size()) ++i;
            unescaped += path[i];
        }
        if (fs::is_directory(fs::u8path(unescaped), ec)) dir = fs::u8path(unescaped);
    }
    if (!fs::is_directory(dir, ec)) { message_ = "No such folder"; messageBad_ = true; return false; }
    dir = fs::weakly_canonical(fs::absolute(dir, ec), ec);
    if (dir.filename().empty()) dir = dir.parent_path();
    // The setup folder itself (DFSETUP, DF2SETUP, ...): keep the game's root,
    // as --game takes it (Delta Force keeps its soldiers there).
    if (dir.has_parent_path()) {
        const std::string n = upper(dir.filename().string());
        if ((n == "DFSETUP" || n == "DF2SETUP" || n == "DFLSETUP" || n == "TFDSETUP") && identifyGame(dir.parent_path()) != GameKind::Unknown)
            dir = dir.parent_path();
    }
    // A GOG installer unpacked with innoextract puts the game in `app`
    // (Df.exe or Df2.exe next to the PFFs): take that folder.
    if (identifyGame(dir) == GameKind::Unknown)
        for (const auto& de : fs::directory_iterator(dir, ec))
            if (de.is_directory(ec) && upper(de.path().filename().string()) == "APP" && identifyGame(de.path()) != GameKind::Unknown) {
                dir = de.path();
                break;
            }
    for (size_t i = 0; i < entries_.size(); ++i)
        if (fs::equivalent(entries_[i].dir, dir, ec)) {
            sel_ = int(i);
            message_ = std::string(entries_[i].label) + " is already in the list";
            messageBad_ = false;
            return true;
        }
    const GameKind kind = identifyGame(dir);
    if (kind == GameKind::Unknown) {
        message_ = "No Delta Force game found in that folder";
        messageBad_ = true;
        return false;
    }
    Entry e;
    e.dir = dir;
    e.kind = kind;
    e.present = true;
    entries_.push_back(e);
    relabel();
    sel_ = int(entries_.size()) - 1;
    save();
    message_ = std::string("Added ") + gameTitle(kind);
    messageBad_ = false;
    // The first Delta Force game added gives the launcher its look.
    if (t_.kind == Theme::Builtin && gamePlayable(kind)) pickTheme();
    return true;
}

void Launcher::removeSelected() {
    if (sel_ < 0 || sel_ >= int(entries_.size())) return;
    if (pendingRemove_ != sel_) {
        pendingRemove_ = sel_;
        message_ = "Remove again to take it off the list (its files are kept)";
        messageBad_ = true;
        return;
    }
    message_ = "Removed " + entries_[size_t(sel_)].label;
    messageBad_ = false;
    entries_.erase(entries_.begin() + sel_);
    pendingRemove_ = -1;
    relabel();
    sel_ = std::min(sel_, int(entries_.size()) - 1);
    save();
}

void Launcher::choose(int index) {
    if (index < 0 || index >= int(entries_.size())) return;
    const Entry& e = entries_[size_t(index)];
    sel_ = index;
    if (!e.present) { message_ = "Folder not found"; messageBad_ = true; return; }
    if (!gamePlayable(e.kind)) { message_ = std::string(gameTitle(e.kind)) + " is not playable in OpenDF yet"; messageBad_ = true; return; }
    chosen_ = index;
}

// ---------------------------------------------------------------- drawing

float Launcher::textWidth(const Font& f, const std::string& s) const { return Hud::sheetWidth(f.sheet, upper(s), f.scale); }

void Launcher::text(const Font& f, float x, float y, const std::string& s, Color c, int align) {
    const std::string u = upper(s);
    const float w = Hud::sheetWidth(f.sheet, u, f.scale);
    if (align == 1) x -= std::floor(w / 2);
    else if (align == 2) x -= w;
    hud_->sheetText(f.sheet, offX_ + x * scale_, offY_ + y * scale_, u, c, f.scale * scale_);
}

// The end of `s` that fits in `w` (paths: the folder names matter most).
std::string Launcher::fitTail(const Font& f, const std::string& s, float w) const {
    if (textWidth(f, s) <= w) return s;
    std::string t = s;
    while (!t.empty() && textWidth(f, "..." + t) > w) t.erase(0, 1);
    return "..." + t;
}

void Launcher::blit(const HudImage& im, float x, float y, float sx, float sy, float sw, float sh) {
    if (!im.texture) return;
    hud_->imageUV(im.texture, offX_ + x * scale_, offY_ + y * scale_, sw * scale_, sh * scale_, sx / float(im.width), sy / float(im.height),
                  (sx + sw) / float(im.width), (sy + sh) / float(im.height));
}

void Launcher::outline(float x0, float y0, float x1, float y1, Color c) {
    const float t = 1.f / scale_ * std::max(1.f, std::floor(scale_));
    fill(x0, y0, x1, y0 + t, c);
    fill(x0, y1 - t, x1, y1, c);
    fill(x0, y0, x0 + t, y1, c);
    fill(x1 - t, y0, x1, y1, c);
}

void Launcher::fill(float x0, float y0, float x1, float y1, Color c) {
    hud_->rect(offX_ + x0 * scale_, offY_ + y0 * scale_, (x1 - x0) * scale_, (y1 - y0) * scale_, c);
}

void Launcher::drawBottom(const char* left, const char* middle, const char* right) {
    const Font& f = t_.small;
    const float y = t_.kind == Theme::Builtin ? t_.bottomY - 4 : t_.bottomY;
    auto col = [&](Hit h, bool enabled) { return !enabled ? f.disabled : hover_ == h ? f.lit : f.normal; };
    const bool df2 = t_.kind == Theme::Df2;
    if (left) text(f, t_.addX, y, left, col(AddBtn, true), df2 ? 0 : 1);
    if (middle) text(f, t_.removeX, y, middle, col(RemoveBtn, sel_ >= 0 && sel_ < int(entries_.size())), df2 ? 0 : 1);
    if (right) text(f, df2 ? t_.exitX : std::min(t_.exitX, 598 - textWidth(f, right) / 2), y, right, col(ExitBtn, true), df2 ? 2 : 1);
}

int Launcher::hitTest(float x, float y) const {
    if (mode_ == Mode::List)
        for (int i = top_; i < int(entries_.size()) && i < top_ + t_.visible; ++i) {
            const float by = t_.itemY + float(i - top_) * t_.itemStep;
            if (x >= t_.itemX && x < t_.itemX + t_.itemW && y >= by && y < by + t_.itemH) return i;
        }
    const Font& f = t_.small;
    const float h = float(f.sheet.height) * f.scale + 4, by = t_.bottomY - (t_.kind == Theme::Builtin ? 6 : 2);
    if (y < by || y > by + h) return None;
    const bool df2 = t_.kind == Theme::Df2;
    const char* labels[3] = {mode_ == Mode::Add ? "Add" : "Add game", mode_ == Mode::Add ? nullptr : "Remove", mode_ == Mode::Add ? "Cancel" : "Exit"};
    const float xs[3] = {t_.addX, t_.removeX, df2 ? t_.exitX : std::min(t_.exitX, 598 - textWidth(f, labels[2]) / 2)};
    const int align[3] = {df2 ? 0 : 1, df2 ? 0 : 1, df2 ? 2 : 1};
    const Hit hits[3] = {AddBtn, RemoveBtn, ExitBtn};
    for (int k = 0; k < 3; ++k) {
        if (!labels[k]) continue;
        const float w = textWidth(f, labels[k]);
        const float x0 = align[k] == 1 ? xs[k] - w / 2 : align[k] == 2 ? xs[k] - w : xs[k];
        if (x >= x0 - 4 && x < x0 + w + 4) return hits[k];
    }
    return None;
}

void Launcher::drawList() {
    text(t_.title, t_.titleX, t_.titleY, "Select game", t_.title.normal, t_.titleAlign);
    if (entries_.empty()) {
        const float cx = (t_.infoX0 + t_.infoX1) / 2;
        text(t_.item, cx, t_.itemY + 30, "No games added yet", t_.item.normal, 1);
        text(t_.info, cx, t_.itemY + 70, "Add game: the folder you copied a Delta Force CD to", t_.info.normal, 1);
        text(t_.info, cx, t_.itemY + 70 + float(t_.info.sheet.height) * t_.info.scale + 6, "(opendf import puts them in games/)",
             t_.info.normal, 1);
    }
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + t_.visible) top_ = sel_ - t_.visible + 1;
    top_ = std::max(0, top_);
    for (int i = top_; i < int(entries_.size()) && i < top_ + t_.visible; ++i) {
        const Entry& e = entries_[size_t(i)];
        const bool enabled = e.present && gamePlayable(e.kind), lit = i == sel_;
        const float y = t_.itemY + float(i - top_) * t_.itemStep;
        // Frames: 0 normal, 1 lit, 3 disabled (the menus' drawstates 1, 2,
        // 4); a highlighted game that cannot start is lit with grey text.
        const int frame = lit ? 1 : !enabled ? 3 : 0;
        if (t_.button.texture) {
            const float fh = float(t_.button.height) / 4.f;
            if (t_.kind == Theme::Df1) hud_->setTranslucent(true);
            blit(t_.button, t_.itemX, y, 0, fh * float(frame), float(t_.button.width), fh);
            if (t_.kind == Theme::Df1) hud_->setTranslucent(false);
        } else {
            fill(t_.itemX, y, t_.itemX + t_.itemW, y + t_.itemH, {0, lit ? 0.35f : 0.2f, 0, 0.6f});
            outline(t_.itemX, y, t_.itemX + t_.itemW, y + t_.itemH, lit ? t_.item.lit : t_.frame);
        }
        const Color c = lit ? (enabled ? t_.item.lit : t_.item.disabled) : enabled ? t_.item.normal : t_.item.disabled;
        const float th = float(t_.item.sheet.height) * t_.item.scale;
        text(t_.item, t_.itemX + t_.itemW / 2, y + std::floor((t_.itemH - th) / 2), fitTail(t_.item, e.label, t_.itemW - 12), c, 1);
        if (lit && !enabled && t_.kind == Theme::Builtin)
            outline(t_.itemX, y, t_.itemX + t_.itemW, y + t_.itemH, t_.item.lit);
    }
    if (top_ > 0) text(t_.small, t_.itemX + t_.itemW + 10, t_.itemY, "-", t_.small.normal);
    if (top_ + t_.visible < int(entries_.size()))
        text(t_.small, t_.itemX + t_.itemW + 10, t_.itemY + float(t_.visible - 1) * t_.itemStep, "+", t_.small.normal);

    // The highlighted game: name, folder, state.
    if (t_.infoBox && !entries_.empty()) outline(t_.infoX0, t_.infoY0, t_.infoX1, t_.infoY1, t_.frame);
    const float x = t_.infoX0 + 6, w = t_.infoX1 - t_.infoX0 - 12;
    float y = t_.infoY0 + 6;
    const float lh = float(t_.info.sheet.height) * t_.info.scale + 4;
    if (sel_ >= 0 && sel_ < int(entries_.size())) {
        const Entry& e = entries_[size_t(sel_)];
        text(t_.info, x, y, e.present ? gameTitle(e.kind) : "Missing game folder", t_.info.normal);
        y += lh;
        text(t_.info, x, y, fitTail(t_.info, e.dir.string(), w), t_.info.normal);
        y += lh;
        if (!e.present) text(t_.info, x, y, "Folder not found: reconnect its drive or remove it", t_.info.lit);
        else if (!gamePlayable(e.kind)) text(t_.info, x, y, "Not playable in OpenDF yet", t_.info.lit);
        else text(t_.info, x, y, "Ready: Enter or click to start", t_.info.normal);
        y += lh;
    }
    if (!message_.empty()) text(t_.info, x, y, fitTail(t_.info, message_, w), messageBad_ ? t_.info.lit : t_.info.normal);
    drawBottom("Add game", "Remove", "Exit");
}

void Launcher::drawAdd() {
    text(t_.title, t_.titleX, t_.titleY, "Add game", t_.title.normal, t_.titleAlign);
    const float x0 = 60, x1 = 580;
    const float lh = float(t_.info.sheet.height) * t_.info.scale + 6;
    float y = t_.itemY + 10;
    const char* lines[] = {"Type or paste the folder of a Delta Force game", "(where its CD was copied, e.g. games/DeltaForce)",
                           "and press Enter, or drop the folder onto this window."};
    for (const char* l : lines) {
        text(t_.info, (x0 + x1) / 2, y, l, t_.info.normal, 1);
        y += lh;
    }
    y += 16;
    const float boxH = float(t_.info.sheet.height) * t_.info.scale + 10;
    fill(x0, y, x1, y + boxH, {0, 0, 0, 0.45f});
    outline(x0, y, x1, y + boxH, t_.kind == Theme::Df2 ? t_.frame : t_.info.normal);
    const std::string shown = fitTail(t_.info, input_ + "_", x1 - x0 - 12);
    std::string s = shown;
    if (ticks_ & 0x20) s.back() = ' ';
    text(t_.info, x0 + 6, y + 5, s, t_.info.lit);
    y += boxH + 12;
    if (!message_.empty()) text(t_.info, (x0 + x1) / 2, y, message_, messageBad_ ? t_.info.lit : t_.info.normal, 1);
    drawBottom("Add", nullptr, "Cancel");
}

void Launcher::draw() {
    if (t_.background.texture) blit(t_.background, 0, 0, 0, 0, float(t_.background.width), float(t_.background.height));
    else {
        fill(0, 0, 640, 480, {0.02f, 0.06f, 0.02f, 1});
        outline(8, 8, 632, 472, t_.frame);
        outline(8, 420, 632, 472, t_.frame);
    }
    if (mode_ == Mode::List) drawList();
    else drawAdd();
}

// ---------------------------------------------------------------- the loop

bool Launcher::run(fs::path* chosen) {
    settingsFile_ = o_.settingsFile.empty() ? LauncherSettings::defaultFile() : o_.settingsFile;
    const bool existed = settings_.load(settingsFile_);
    if (!existed || settings_.games.empty()) {
        // First start: offer the games already in a games/ folder (where
        // `opendf import` puts them) next to OpenDF or the current folder.
        std::vector<fs::path> where = {fs::current_path() / "games"};
        if (char* base = SDL_GetBasePath()) {
            const fs::path b = base;
            SDL_free(base);
            where.push_back(b / "games");
            where.push_back(b.parent_path().parent_path() / "games");
        }
        std::error_code ec;
        for (const auto& w : where) {
            if (!fs::is_directory(w, ec)) continue;
            std::vector<fs::path> dirs;
            for (const auto& de : fs::directory_iterator(w, ec))
                if (de.is_directory(ec)) dirs.push_back(fs::weakly_canonical(de.path(), ec));
            // In the order the games came out.
            std::vector<std::pair<GameKind, fs::path>> found;
            for (const auto& d : dirs) {
                bool dup = false;
                for (const auto& g : settings_.games) dup = dup || g == d;
                const GameKind k = dup ? GameKind::Unknown : identifyGame(d);
                if (k != GameKind::Unknown) found.emplace_back(k, d);
            }
            std::sort(found.begin(), found.end());
            for (const auto& f : found) settings_.games.push_back(f.second);
        }
        if (!settings_.games.empty()) settings_.save(settingsFile_);
    }
    loadEntries();
    if (!openWindow()) { closeWindow(); return false; }
    pickTheme();
    // Start on the game played last.
    for (size_t i = 0; i < entries_.size(); ++i)
        if (!settings_.last.empty() && entries_[i].dir == settings_.last) sel_ = int(i);

    const std::string& start = o_.startScreen;
    if (start == "add") { mode_ = Mode::Add; SDL_StartTextInput(); }
    else if (start.rfind("select:", 0) == 0) sel_ = std::atoi(start.c_str() + 7);
    else if (start.rfind("launch:", 0) == 0) choose(std::atoi(start.c_str() + 7));
    else if (start.rfind("add:", 0) == 0) { mode_ = Mode::Add; input_ = start.substr(4); if (tryAdd(input_)) mode_ = Mode::List; }
    else if (start.rfind("remove:", 0) == 0) { sel_ = std::atoi(start.c_str() + 7); removeSelected(); removeSelected(); }
    if (mode_ == Mode::List) SDL_StopTextInput();

    int frame = 0;
    while (!quit_ && chosen_ < 0) {
        int ww, wh, dw, dh;
        SDL_GetWindowSize(win_, &ww, &wh);
        SDL_GL_GetDrawableSize(win_, &dw, &dh);
        // The 640x480 screen scaled to the window, aspect kept (as the front ends).
        scale_ = std::min(float(dw) / 640.f, float(dh) / 480.f);
        offX_ = (float(dw) - 640.f * scale_) / 2;
        offY_ = (float(dh) - 480.f * scale_) / 2;
        auto toScreen = [&](int x, int y, float& sx, float& sy) {
            sx = (float(x) * float(dw) / float(std::max(1, ww)) - offX_) / scale_;
            sy = (float(y) * float(dh) / float(std::max(1, wh)) - offY_) / scale_;
        };
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit_ = true;
            else if (ev.type == SDL_DROPFILE) {
                std::string p = ev.drop.file;
                SDL_free(ev.drop.file);
                if (tryAdd(p)) { mode_ = Mode::List; SDL_StopTextInput(); }
            } else if (ev.type == SDL_MOUSEMOTION) {
                toScreen(ev.motion.x, ev.motion.y, mouseX_, mouseY_);
                hover_ = hitTest(mouseX_, mouseY_);
                if (hover_ >= 0) sel_ = hover_;
            } else if (ev.type == SDL_MOUSEWHEEL && mode_ == Mode::List && !entries_.empty()) {
                sel_ = std::clamp(sel_ - ev.wheel.y, 0, int(entries_.size()) - 1);
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                toScreen(ev.button.x, ev.button.y, mouseX_, mouseY_);
                const int h = hitTest(mouseX_, mouseY_);
                if (h != RemoveBtn) pendingRemove_ = -1;
                if (mode_ == Mode::List) {
                    if (h >= 0) choose(h);
                    else if (h == AddBtn) { mode_ = Mode::Add; message_.clear(); SDL_StartTextInput(); }
                    else if (h == RemoveBtn) removeSelected();
                    else if (h == ExitBtn) quit_ = true;
                } else {
                    if (h == AddBtn && tryAdd(input_)) { mode_ = Mode::List; input_.clear(); SDL_StopTextInput(); }
                    else if (h == ExitBtn) { mode_ = Mode::List; message_.clear(); SDL_StopTextInput(); }
                }
            } else if (ev.type == SDL_TEXTINPUT && mode_ == Mode::Add) {
                input_ += ev.text.text;
            } else if (ev.type == SDL_KEYDOWN) {
                const SDL_Keycode k = ev.key.keysym.sym;
                const bool cmd = (ev.key.keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
                if (mode_ == Mode::Add) {
                    if (k == SDLK_ESCAPE) { mode_ = Mode::List; message_.clear(); SDL_StopTextInput(); }
                    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                        if (tryAdd(input_)) { mode_ = Mode::List; input_.clear(); SDL_StopTextInput(); }
                    } else if (k == SDLK_BACKSPACE && !input_.empty()) {
                        // Whole UTF-8 characters.
                        do input_.pop_back();
                        while (!input_.empty() && (static_cast<unsigned char>(input_.back()) & 0xC0) == 0x80);
                    } else if (cmd && k == SDLK_v) {
                        if (char* clip = SDL_GetClipboardText()) {
                            std::string c = clip;
                            SDL_free(clip);
                            c.erase(std::remove_if(c.begin(), c.end(), [](char ch) { return ch == '\n' || ch == '\r'; }), c.end());
                            input_ += c;
                        }
                    } else if (cmd && k == SDLK_u) input_.clear();
                    continue;
                }
                if (k != SDLK_DELETE && k != SDLK_BACKSPACE) pendingRemove_ = -1;
                if (k == SDLK_ESCAPE) quit_ = true;
                else if ((k == SDLK_UP || k == SDLK_KP_8) && sel_ > 0) { --sel_; message_.clear(); }
                else if ((k == SDLK_DOWN || k == SDLK_KP_2) && sel_ + 1 < int(entries_.size())) { ++sel_; message_.clear(); }
                else if (k == SDLK_HOME) sel_ = 0;
                else if (k == SDLK_END) sel_ = int(entries_.size()) - 1;
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) choose(sel_);
                else if (k == SDLK_a || k == SDLK_INSERT) { mode_ = Mode::Add; message_.clear(); SDL_StartTextInput(); }
                else if (k == SDLK_DELETE || k == SDLK_BACKSPACE) removeSelected();
            }
        }
        if (entries_.empty()) sel_ = 0;
        else sel_ = std::clamp(sel_, 0, int(entries_.size()) - 1);
        ++ticks_;

        glViewport(0, 0, dw, dh);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        hud_->begin(dw, dh);
        draw();
        hud_->end();
        if (!o_.screenshot.empty() && ++frame >= o_.frames) {
            std::vector<uint8_t> px(size_t(dw) * size_t(dh) * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            if (FILE* f = std::fopen(o_.screenshot.c_str(), "wb")) {
                std::fprintf(f, "P6 %d %d 255\n", dw, dh);
                for (int y = dh - 1; y >= 0; --y) std::fwrite(&px[size_t(y) * size_t(dw) * 3], 1, size_t(dw) * 3, f);
                std::fclose(f);
                std::printf("wrote %s\n", o_.screenshot.c_str());
            }
            break;
        }
        SDL_GL_SwapWindow(win_);
    }
    SDL_StopTextInput();
    closeWindow();
    if (chosen_ < 0) return false;
    settings_.last = entries_[size_t(chosen_)].dir;
    save();
    *chosen = entries_[size_t(chosen_)].dir;
    return true;
}

} // namespace

bool runLauncher(const LauncherOptions& options, fs::path* chosen) {
    Launcher l(options);
    return l.run(chosen);
}

} // namespace df
