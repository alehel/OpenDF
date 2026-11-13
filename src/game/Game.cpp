#include "Game.h"

#include "../render/Gl.h"
#include "../render/Hud.h"
#include "../render/Models.h"
#include "../render/Terrain.h"
#include "../formats/Rtxt.h"
#include "../formats/Chr.h"
#include "../formats/Dlg.h"
#include "Controls.h"
#include "Player.h"
#include "Script.h"
#include "Weapons.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <sstream>

namespace df {

namespace {

constexpr float kPi = 3.14159265f;

// HUD colours: DF.EXE builds them at start-up from these RGB values
// (0x4027b1; nearest palette entries in 8-bit modes).
constexpr Color rgb(int r, int g, int b) { return {r / 255.f, g / 255.f, b / 255.f, 1}; }
const Color kHudWhite = rgb(255, 255, 255);   // 0x66f930
const Color kHudBlack = rgb(0, 0, 0);         // 0x66f934
const Color kHudPureGreen = rgb(0, 255, 0);   // 0x66f938
const Color kHudBlue = rgb(17, 60, 253);      // 0x66f93c
const Color kHudRed = rgb(255, 0, 0);         // 0x66f944
const Color kHudBright = rgb(1, 230, 1);      // 0x66f94c: crosshair, scope, map grid, route
const Color kHudGreen = rgb(1, 129, 1);       // 0x66f950: HUD text and outlines
const Color kHudDark = rgb(9, 51, 9);         // 0x66f954: box scanlines, text shadow
// Message text: most callers pass white.
const Color kHudMessage = kHudWhite;

// The original lays its HUD out in a virtual 1024x768 space (DF.EXE
// 0x424788); the positions below are those values converted to 640x480
// units (x 0.625). The bars are H/8 = 60 units each (0x404650). The screen is
// scaled by height and, on wide screens, the right-hand group stays anchored
// to the right edge instead of being stretched; things in the 3D view stay
// centred.
constexpr float kTopBar = 60, kBottomBar = 60, kTopDivider = 350, kBottomDivider = 431;

// The original's camera (DF.EXE 0x440ff0, FOV 0x5a set at 0x404489): 90°
// horizontal across the 3D view, focal length = half the view width, the
// same for both axes, so 58.72° vertical on the 640x360 view. Wider screens
// keep this vertical angle and see more at the sides (OpenDF's widescreen
// choice, which keeps the weapon where it is).
const float kOriginalFovY = 2.f * std::atan(1.f / (640.f / 360.f));

struct Message { std::string text; float age; };

bool project(const Mat4& vp, Vec3 p, float w, float h, float& sx, float& sy, float& depth) {
    float x = vp.at(0, 0) * p.x + vp.at(0, 1) * p.y + vp.at(0, 2) * p.z + vp.at(0, 3);
    float y = vp.at(1, 0) * p.x + vp.at(1, 1) * p.y + vp.at(1, 2) * p.z + vp.at(1, 3);
    float cw = vp.at(3, 0) * p.x + vp.at(3, 1) * p.y + vp.at(3, 2) * p.z + vp.at(3, 3);
    if (cw <= 0.05f) return false;
    sx = (x / cw * 0.5f + 0.5f) * w;
    sy = (1 - (y / cw * 0.5f + 0.5f)) * h;
    depth = cw;
    return true;
}

void writePpm(const std::string& path, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream f(path, std::ios::binary);
    f << "P6 " << w << " " << h << " 255\n";
    for (int y = h - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(&px[size_t(y) * w * 3]), w * 3);
}

// The original's grid (DF.EXE 0x43DCE0): 100 m squares in mission
// coordinates, origin at x = -1000, y = +1000; letters west to east (A..Z,
// then AA, AB, ...), numbers north to south from 1. E.g. "I12".
constexpr double kGridSquare = 100, kGridOrigin = 1000;
std::string gridColumn(double x) {
    int m = int(std::floor(x + kGridOrigin));
    if (m < 0) m += 70200;   // as DF.EXE does
    int col = m / 100;
    if (col < 0) col += 702;
    char buf[3] = {};
    if (col < 26) buf[0] = char('A' + col);
    else { buf[0] = char('@' + col / 26); buf[1] = char('A' + col % 26); }
    return buf;
}
int gridRow(double y) {
    double d = kGridOrigin - y;
    if (d < 0) d += 10000;   // as DF.EXE does
    return int(d / kGridSquare) + 1;
}
std::string gridName(double x, double y) { return gridColumn(x) + std::to_string(gridRow(y)); }

// "C03M03 - Blow Up C3" -> "Blow Up C3"
std::string missionName(const std::string& title) {
    auto dash = title.find(" - ");
    return dash == std::string::npos ? title : title.substr(dash + 3);
}

class Game : public ScriptHost {
public:
    Game(Vfs& vfs, const GameOptions& o) : vfs_(vfs), o_(o) {}
    int run();

    // ScriptHost: what the mission's events can see and do.
    int teamTotal(int team) const override { return teamCount(team, false); }
    int teamAlive(int team) const override { return teamCount(team, true); }
    bool teamInArea(int team, const AreaBox& a) const override;
    void killTeam(int team) override;
    void removeTeam(int team) override;
    void showText(int id) override;
    void radio(int id) override;
    void endMission(int winnerSide) override;

private:
    struct Screen {
        float w, h, u;          // pixels; u = pixels per original 640x480 unit
        float top, bottom;      // bar heights in pixels
        float playY, playH;     // 3D view area (top-left origin)
        float lx(float x) const { return x * u; }                 // left-anchored
        float rx(float x) const { return w - (640.f - x) * u; }   // right-anchored
        float cx(float dx) const { return w * 0.5f + dx * u; }     // relative to the centre
        float y(float v) const { return v * u; }
        float by(float v) const { return h - (480.f - v) * u; }   // bottom-bar y
        // The original's display mode that fits this window (modeW x
        // modeW*3/4): DF.EXE draws that mode's font and bitmaps 1:1, so in
        // 640x480 units they are 640/modeW in size.
        int modeW = 640;
        float text() const { return u * 640.f / float(modeW); }
        float art() const { return u * 640.f / float(modeW); }
        float px() const { return std::max(1.f, u); }   // one original pixel (lines)
    };

    bool loadMission(int index);
    void handleKey(const SDL_KeyboardEvent& k);
    void drawWorld(const Mat4& view, const Mat4& proj, bool zoomView);
    void drawViewModel(const Screen& s);
    void drawZoomInset(const Screen& s, const Mat4& view, float fovY);
    void drawHud(const Screen& s, const Mat4& viewProj);
    void drawTopBar(const Screen& s);
    void drawBottomBar(const Screen& s);
    void drawCommandMap(const Screen& s);
    void drawMapMarker(const Screen& s, float x, float y, int type, Color c, float heading, bool dead);
    void hudBox(const Screen& s, float x0, float y0, float x1, float y1, bool right);
    void outlinedText(float x, float y, const std::string& t, Color c, float scale, Color outline);
    float rangeToAim() const;
    // The scope inset shows while the scope is on; moving turns it off (0x4373ab).
    bool scopeShown() const { return arsenal_.zoomed(); }
    Vec3 aimDirection() const;

    Vfs& vfs_;
    GameOptions o_;
    SDL_Window* win_ = nullptr;
    ItemDefs items_;
    std::vector<std::string> gameText_;   // DFGAME.BIN
    ExeImage exe_;                        // the original DF.EXE, for its tables
    Controls controls_;                   // the original's key table (from DF.EXE)
    AnimDefs playerMoves_;                // the player soldier's .ANM move table
    float eyeHeightTable_[3][2] = {{1.7558f, 1.7677f}, {1.3768f, 1.3768f}, {0.5031f, 0.5031f}};
    std::string missionName_;             // from the campaign text (DFCAMPxx.BIN)
    TextTable campText_;                  // DFCAMPxx.BIN
    // The player's route (DF.EXE 0x447856 builds it, 0x449a39 advances it,
    // 0x4838ac shows it): points of the first path flagged 2.
    struct RoutePoint { double x, y; float radius; std::string name; const MissionObject* object; };
    std::vector<RoutePoint> route_;
    int routeIndex_ = 0;
    void buildRoute();
    void updateRoute();
    int teamCount(int team, bool aliveOnly) const;
    void drawMissionEnd(const Screen& s);
    void drawGoals(const Screen& s);

    // Scripted test runs (--input / --watch / --record).
    struct InputCmd { float t; std::string op, arg; float value; };
    std::vector<InputCmd> inputCmds_;
    size_t inputNext_ = 0;
    float inputClock_ = 0, turnRate_ = 0, lookRate_ = 0, faceTarget_ = -1;
    std::vector<SDL_Scancode> inputHeld_;
    bool inputDone_ = false;
    bool loadInputScript(const std::string& path);
    void runInputScript(float dt);
    bool held(SDL_Scancode sc) const;

    // Mission scripting.
    MissionScript script_;
    DialogIndex dialogs_;                 // DFDLGnn.DBF
    std::vector<std::string> dialogText_; // DFDLGnn.BIN
    float scriptClock_ = 0;
    uint32_t ticks_ = 0;                  // 60 Hz simulation ticks (DF.EXE 0xc70acc)
    int missionResult_ = -1;              // winning side once the mission has ended
    int missionNumber_ = 0;               // m of CccMmm
    static constexpr int kPlayerTeam = 1, kPlayerSide = 1;
    std::vector<std::string> missions_;
    int current_ = 0;
    Mission mission_;
    // GL-owning members live behind pointers so they can be released before
    // the context is destroyed.
    std::unique_ptr<Terrain> terrainPtr_ = std::make_unique<Terrain>();
    std::unique_ptr<ModelRenderer> modelsPtr_ = std::make_unique<ModelRenderer>();
    std::unique_ptr<Hud> hudPtr_ = std::make_unique<Hud>();
    Terrain& terrain_ = *terrainPtr_;
    ModelRenderer& models_ = *modelsPtr_;
    Hud& hud_ = *hudPtr_;
    HudImage stanceIcons_, weaponIcons_;
    int hudMode_ = 0;   // display mode whose HUD assets are loaded
    void useHudMode(int modeW);
    World world_;
    Player player_;
    Arsenal arsenal_;
    std::deque<Message> messages_;
    float missionTime_ = 0;
    // Objects: DF.EXE's depth buffer saturates at 1024 m (inferred cull distance).
    float viewDistance_ = 1024;
    float scopeFocal_ = 320;
    float mapScale_ = 1.f;   // command map scale (DF.EXE 0x933540, default 1.0)
    bool showMap_ = false, showGoals_ = false, crosshair_ = true, gunVisible_ = true;
    bool captured_ = true;
};

bool Game::loadMission(int index) {
    std::vector<uint8_t> raw;
    std::string err;
    const std::string& name = missions_[index];
    if (!vfs_.read(name + ".BMS", raw) || !parseMission(raw, mission_, &err)) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), err.empty() ? "unreadable" : err.c_str());
        return false;
    }
    std::string trn = mission_.terrain;
    if (auto dot = trn.rfind('.'); dot != std::string::npos) trn.resize(dot);
    if (!terrain_.load(vfs_, trn, &err)) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), err.c_str());
        return false;
    }
    current_ = index;
    // Testing: OPENDF_HEIGHTSCALE overrides metres per elevation step.
    if (const char* hs = std::getenv("OPENDF_HEIGHTSCALE")) terrain_.scale.heightScale = float(std::atof(hs));
    world_.terrain = &terrain_;
    world_.build(vfs_, models_, items_, mission_);

    player_ = Player{};
    player_.moves = &playerMoves_;
    std::memcpy(player_.eyeHeights, eyeHeightTable_, sizeof eyeHeightTable_);
    player_.feet = {512, 0, 512};
    for (const auto& obj : mission_.objects) {
        const ItemDef* item = items_.byMissionType(obj.type);
        if (item && item->id == kItemPlayerStart) {
            const float ext = terrain_.mapExtent();
            player_.feet = {obj.x, 0, -obj.y};
            player_.wrap(ext);
            player_.yaw = obj.heading * kPi / 180.f;
            break;
        }
    }
    player_.feet.y = world_.groundHeight(player_.feet, 0.45f);
    arsenal_.init(gameText_, &exe_, &items_, weaponIndex(o_.primary), weaponIndex(o_.secondary));
    // The mission's display name ("Insurrection") is in the campaign text file:
    // DFCAMPcc.BIN, entry m-1 for CccMmm.
    missionName_ = missionName(mission_.title);
    {
        int c = 0, m = 0;
        std::vector<uint8_t> camp;
        char file[32];
        campText_ = TextTable{};
        if (std::sscanf(name.c_str(), "C%dM%d", &c, &m) == 2 && m > 0) {
            std::snprintf(file, sizeof file, "DFCAMP%02d.BIN", c);
            if (vfs_.read(file, camp) && campText_.parse(camp) && m - 1 < int(campText_.text().size()) &&
                !campText_.text()[m - 1].empty())
                missionName_ = campText_.text()[m - 1];
        }
    }
    buildRoute();
    {
        // Radio dialogs: the mission header names the set ("dfdlg02").
        std::vector<uint8_t> raw;
        const std::string set = toUpper(mission_.dialogue);
        dialogs_ = DialogIndex{};
        dialogText_.clear();
        if (!set.empty()) {
            if (vfs_.read(set + ".DBF", raw)) dialogs_.parse(raw);
            if (vfs_.read(set + ".BIN", raw)) parseRtxt(raw, dialogText_);
        }
        int c = 0;
        missionNumber_ = 0;
        std::sscanf(name.c_str(), "C%dM%d", &c, &missionNumber_);
    }
    script_.load(mission_);
    scriptClock_ = 0;
    missionResult_ = -1;
    missionTime_ = 0;
    messages_.clear();
    if (exe_.loaded() && !arsenal_.fromExe())
        std::fprintf(stderr, "DF.EXE is not the expected version; using built-in weapon values\n");
    else if (arsenal_.fromExe()) std::printf("weapon tables read from DF.EXE\n");
    std::printf("mission %s: \"%s\" on %s, %zu objects\n", name.c_str(), mission_.title.c_str(), trn.c_str(),
                world_.objects.size());
    return true;
}

void Game::buildRoute() {
    route_.clear();
    routeIndex_ = 0;
    const MissionPath* path = nullptr;
    for (const auto& p : mission_.paths)
        if (p.flags & 2) { path = &p; break; }
    if (!path) return;
    for (size_t i = 0; i < path->points.size() && i < 8; ++i) {
        const MissionObject* o = mission_.inGroup(2, path->points[i]);
        if (!o) continue;
        RoutePoint rp{o->x, o->y, float(o->raw[7]), {}, o};   // record +0x1C: arrival radius, metres
        char key[16];
        std::snprintf(key, sizeof key, "WPN%03d", mission_.routeNameIds[i]);
        if (const std::string* n = campText_.find("Waypoint Names", key); n && !n->empty()) rp.name = *n;
        else rp.name = std::to_string(i);   // DF.EXE falls back to "%i"
        route_.push_back(std::move(rp));
    }
}

void Game::updateRoute() {
    // DF.EXE 0x449a39: arrived when within the radius on both axes and
    // max + min/2 (an octagonal distance) is under it; wraps to the start.
    if (route_.empty()) return;
    const RoutePoint& p = route_[size_t(routeIndex_)];
    const double ax = std::abs(player_.missionX() - p.x), ay = std::abs(player_.missionY() - p.y);
    if (ax > p.radius || ay > p.radius) return;
    if (std::max(ax, ay) + std::min(ax, ay) / 2 >= p.radius) return;
    routeIndex_ = (routeIndex_ + 1) % int(route_.size());
}

bool Game::loadInputScript(const std::string& path) {
    // One command per line: "<seconds> <op> [arg] [value]", # comments.
    //   press KEY      a key press (SDL key name: W, C, Tab, Left Shift, ...)
    //   hold KEY       keep a key down; "hold Fire" holds the trigger
    //   release KEY    let it go
    //   turn DEG/S     keep turning (negative = left); "turn 0" stops
    //   look DEG/S     keep pitching (positive = up)
    //   face DEG       turn smoothly to a compass heading (0 = north, 90 = east)
    //   end            finish the run
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream ls(line);
        InputCmd c{0, {}, {}, 0};
        if (!(ls >> c.t >> c.op)) continue;
        std::string rest, word;
        while (ls >> word) rest += (rest.empty() ? "" : " ") + word;
        if (c.op == "turn" || c.op == "look" || c.op == "face") c.value = float(std::atof(rest.c_str()));
        else c.arg = rest;
        inputCmds_.push_back(c);
    }
    std::stable_sort(inputCmds_.begin(), inputCmds_.end(), [](const InputCmd& a, const InputCmd& b) { return a.t < b.t; });
    return true;
}

bool Game::held(SDL_Scancode sc) const {
    return std::find(inputHeld_.begin(), inputHeld_.end(), sc) != inputHeld_.end();
}

void Game::runInputScript(float dt) {
    inputClock_ += dt;
    while (inputNext_ < inputCmds_.size() && inputCmds_[inputNext_].t <= inputClock_) {
        const InputCmd& c = inputCmds_[inputNext_++];
        const SDL_Scancode sc = c.arg == "Fire" ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromName(c.arg.c_str());
        if (c.op == "press" && sc != SDL_SCANCODE_UNKNOWN) {
            SDL_KeyboardEvent k{};
            k.type = SDL_KEYDOWN;
            k.keysym.scancode = sc;
            k.keysym.sym = SDL_GetKeyFromScancode(sc);
            if (held(SDL_SCANCODE_LSHIFT)) k.keysym.mod = KMOD_LSHIFT;
            handleKey(k);
        } else if (c.op == "hold") {
            inputHeld_.push_back(c.arg == "Fire" ? SDL_SCANCODE_LCTRL : sc);
        } else if (c.op == "release") {
            const SDL_Scancode r = c.arg == "Fire" ? SDL_SCANCODE_LCTRL : sc;
            inputHeld_.erase(std::remove(inputHeld_.begin(), inputHeld_.end(), r), inputHeld_.end());
        } else if (c.op == "turn") turnRate_ = c.value;
        else if (c.op == "look") lookRate_ = c.value;
        else if (c.op == "face") { faceTarget_ = c.value; turnRate_ = 0; }
        else if (c.op == "end") inputDone_ = true;
        else std::fprintf(stderr, "input script: unknown command \"%s %s\"\n", c.op.c_str(), c.arg.c_str());
    }
    if (inputNext_ >= inputCmds_.size() && !inputCmds_.empty()) inputDone_ = true;
    if (faceTarget_ >= 0) {
        // Shortest way round, at most 120 degrees a second.
        float d = std::remainder(faceTarget_ * kPi / 180.f - player_.yaw, 2 * kPi);
        const float step = 120.f * kPi / 180.f * dt;
        if (std::abs(d) <= step) { player_.yaw += d; faceTarget_ = -1; }
        else player_.yaw += d > 0 ? step : -step;
    }
    player_.yaw += turnRate_ * kPi / 180.f * dt;
    player_.pitch = std::clamp(player_.pitch + lookRate_ * kPi / 180.f * dt, -1.5f, 1.5f);
}

// DF.EXE picks its HUD art by screen width: POS%i.PCX / WEAP%i.PCX (0x4846f8)
// and the font F_SML%i.PCX with the width capped at 800 (0x40298c).
void Game::useHudMode(int modeW) {
    if (modeW == hudMode_) return;
    hudMode_ = modeW;
    char name[32];
    std::snprintf(name, sizeof name, "POS%d.PCX", modeW);
    stanceIcons_ = hud_.loadImage(vfs_, name);
    std::snprintf(name, sizeof name, "WEAP%d.PCX", modeW);
    weaponIcons_ = hud_.loadImage(vfs_, name);
    std::snprintf(name, sizeof name, "F_SML%d.PCX", std::min(modeW, 800));
    std::string err;
    if (!hud_.loadFont(vfs_, name, &err)) std::fprintf(stderr, "%s\n", err.c_str());
}

int Game::teamCount(int team, bool aliveOnly) const {
    // DF.EXE 0x440904/0x4409f4 counts the players and every object of a team
    // in file groups 0 (moving objects), 1 (scenery and props) and 3 (people);
    // markers (group 2) are not counted.
    if (team == 0) return 0;
    int n = team == kPlayerTeam ? 1 : 0;   // the player (cannot be killed yet)
    for (const auto& o : world_.objects)
        if (o.team == team && o.missionGroup != 2 && (!aliveOnly || o.alive())) ++n;
    return n;
}

bool Game::teamInArea(int team, const AreaBox& a) const {
    auto inside = [&](double x, double y) { return x >= a.minX && x <= a.maxX && y >= a.minY && y <= a.maxY; };
    if (team == kPlayerTeam && inside(player_.missionX(), player_.missionY())) return true;
    // Other teams stand where the mission placed them until OpenDF has AI.
    for (const auto& o : world_.objects)
        if (o.team == team && o.alive() && o.missionGroup != 2 && inside(o.missionX, o.missionY))
            return true;
    return false;
}

void Game::killTeam(int team) {
    for (size_t i = 0; i < world_.objects.size(); ++i) {
        auto& o = world_.objects[i];
        if (o.team == team && o.missionGroup == 3 && o.alive()) world_.damage(int(i), 1000);
    }
}

void Game::removeTeam(int team) {
    for (auto& o : world_.objects)
        if (o.team == team && o.missionGroup != 2) o.removed = true;
}

void Game::showText(int id) {
    char key[16];
    std::snprintf(key, sizeof key, "ID%03d", id);
    if (const std::string* t = campText_.find("Triggered Text", key)) messages_.push_back({*t, 0});
}

void Game::radio(int id) {
    // Subtitles of the dialog's clips; the audio comes with sound support.
    char name[16];
    std::snprintf(name, sizeof name, "dlg%03d", id);
    if (const auto* parts = dialogs_.find(name))
        for (const auto& p : *parts)
            if (p.subtitle >= 0 && p.subtitle < int(dialogText_.size()) && !dialogText_[size_t(p.subtitle)].empty())
                messages_.push_back({dialogText_[size_t(p.subtitle)], 0});
}

void Game::endMission(int winnerSide) {
    if (missionResult_ < 0) missionResult_ = winnerSide;
}

void Game::handleKey(const SDL_KeyboardEvent& k) {
    const bool shift = (k.keysym.mod & KMOD_SHIFT) != 0;
    if (missionResult_ >= 0) {
        // "Hit Enter to end mission." / "Hit Space Bar to replay mission".
        if (k.keysym.sym == SDLK_RETURN || k.keysym.sym == SDLK_KP_ENTER) {
            SDL_Event q;
            q.type = SDL_QUIT;
            SDL_PushEvent(&q);
            return;
        }
        if (k.keysym.sym == SDLK_SPACE) { loadMission(current_); return; }
    }
    // Debug keys (not in the original): N free flight.
    if (k.keysym.sym == SDLK_n) { player_.fly = !player_.fly; return; }
    (void)shift;
    switch (controls_.actionFor(k.keysym.scancode)) {
    case Action::Escape:
        if (showMap_ || showGoals_) { showMap_ = showGoals_ = false; break; }
        if (captured_) { captured_ = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
        else { SDL_Event q; q.type = SDL_QUIT; SDL_PushEvent(&q); }
        break;
    case Action::Stand: player_.stance = Stance::Stand; break;
    case Action::Crouch: player_.stance = Stance::Crouch; break;
    case Action::Prone: player_.stance = Stance::Prone; break;
    // Weapon keys select the original's slots (DF.EXE 0x44b598/0x44aa74):
    // 1 knife, 2 sidearm, 3-5 the primary's fire modes, 6-8 the secondary's,
    // 9 and 0 grenade / laser designator (not implemented yet).
    case Action::Weapon2: arsenal_.select(0); break;
    case Action::Weapon3: arsenal_.select(1, 0); break;
    case Action::Weapon4: arsenal_.select(1, 1); break;
    case Action::Weapon5: arsenal_.select(1, 2); break;
    case Action::Weapon6: arsenal_.select(2, 0); break;
    case Action::Weapon7: arsenal_.select(2, 1); break;
    case Action::Weapon8: arsenal_.select(2, 2); break;
    case Action::Cycle: arsenal_.cycle(); break;
    case Action::Magazine: arsenal_.reload(); break;
    case Action::Scope: arsenal_.toggleZoom(); break;
    case Action::NextWaypoint:
        if (!route_.empty()) routeIndex_ = (routeIndex_ + 1) % int(route_.size());
        break;
    case Action::BigMap: showMap_ = !showMap_; showGoals_ = false; break;
    case Action::MissionGoals: showGoals_ = !showGoals_; showMap_ = false; break;
    // DF.EXE 0x42ddbf: the map scale (metres per pixel) x0.9 / x1.1, 0.25..3.6.
    case Action::MapZoomIn: mapScale_ = std::max(0.25f, mapScale_ * 0.9f); break;
    case Action::MapZoomOut: mapScale_ = std::min(3.6f, mapScale_ * 1.1f); break;
    case Action::Crosshairs: crosshair_ = !crosshair_; break;
    case Action::ShowGun: gunVisible_ = !gunVisible_; break;
    case Action::CenterView: player_.pitch = 0; break;
    default: break;
    }
}

Vec3 Game::aimDirection() const {
    // Shots go where the soldier faces; firing never moves the view.
    return player_.forward();
}

float Game::rangeToAim() const {
    Vec3 eye = player_.eye();
    RayHit hit;
    const float maxRange = 2000.f;
    if (!world_.raycast(eye, eye + aimDirection() * maxRange, hit)) return -1;
    return hit.t * maxRange;
}

void Game::drawWorld(const Mat4& view, const Mat4& proj, bool zoomView) {
    const Vec3 eye = player_.eye();
    // Terrain rings scale with the original's focal length: F = 320 per 640
    // pixels of its display mode, or the scope view's own focal length.
    terrain_.ringScale = zoomView ? scopeFocal_ / 320.f : float(hudMode_ ? hudMode_ : 640) / 640.f;
    terrain_.draw(view, proj, eye, viewDistance_);
    // Model fog: the terrain's fog starts at the 200 m ring (scaled); models
    // are drawn to 1024 m, where the terrain has 18 % fog. Model fog itself is
    // not traced: approximation.
    const float rs = terrain_.ringScale;
    SceneLighting light{eye, terrain_.fogColor(), terrain_.sunDirection(), 200.f * rs, 5700.f * rs};
    models_.begin(proj * view, light);
    const Vec3 fwd = aimDirection();
    const float zoom = zoomView ? arsenal_.zoom() : 1.f;
    struct Visible { const WorldObject* o; Vec3 at; int lod; };
    std::vector<Visible> visible;
    for (const auto& o : world_.objects) {
        if (o.removed) continue;
        Vec3 at = world_.nearest(o.pos, eye);
        Vec3 d = at - eye;
        float dist = std::sqrt(dot(d, d));
        if (dist > viewDistance_ + o.model->radius || dot(d, fwd) < -o.model->radius) continue;
        visible.push_back({&o, at, ModelRenderer::chooseLod(*o.model, dist / zoom)});
    }
    for (auto& v : visible) models_.draw(*v.o->model, v.lod, v.o->matrix(v.at));
    for (auto& v : visible) models_.drawTranslucent(*v.o->model, v.lod, v.o->matrix(v.at));
    models_.end();
}

void Game::drawViewModel(const Screen& s) {
    // The original hides the weapon while the zoom inset is up.
    if (!gunVisible_ || scopeShown() || showMap_) return;
    const GpuModel* m = models_.get(vfs_, arsenal_.def().viewModel);
    if (!m) return;
    glClear(GL_DEPTH_BUFFER_BIT);

    // As in the original engine: the P* model is drawn unrotated (aligned with
    // the view) at the weapon's offset from DF.EXE's player weapon table, with
    // the same camera as the world. Offset axes are the model's own: forward,
    // left, up in 1/256 m.
    const int* g = arsenal_.def().gunOffset;
    Vec3 offset{-g[1] / 256.f, g[2] / 256.f, -g[0] / 256.f};
    // DF.EXE 0x449abc: the gun is fixed to the eye (no bob, sway or lowering);
    // only the firing kick moves it (forward, left, up in its own axes).
    const float* k = arsenal_.kick();
    offset = offset + Vec3{-k[1], k[2], -k[0]};
    // Model axes (+X barrel, +Y up, +Z right) to view axes (-Z ahead, +Y up, +X right).
    Mat4 base;
    base.at(0, 0) = 0; base.at(2, 0) = -1;
    base.at(0, 2) = 1; base.at(2, 2) = 0;
    Mat4 model = translate(offset) * base;

    // Same camera as the world: on wider screens the gun stays where it is
    // relative to the centre of the view (in screen heights); the extra width
    // only shows more of the world, so ultrawide screens look natural.
    Mat4 proj = perspective(kOriginalFovY, s.w / s.playH, 0.02f, 10.f);
    SceneLighting light{{0, 0, 0}, {0, 0, 0}, normalize(Vec3{0.3f, 0.9f, 0.4f}), 1e6f, 2e6f};
    models_.begin(proj, light);
    models_.draw(*m, 0, model);
    models_.drawTranslucent(*m, 0, model, arsenal_.flash() > 0);
    models_.end();
}

// The zoom view is a magnified circle on the right of the 3D view (the main
// view stays as it is), like the original.
void Game::drawZoomInset(const Screen& s, const Mat4& view, float fovY) {
    if (!scopeShown() || showMap_) return;
    const float cx = s.cx(160), cy = s.playY + s.playH * 0.5f, r = s.y(100);

    glEnable(GL_STENCIL_TEST);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glViewport(0, 0, int(s.w), int(s.h));
    hud_.begin(int(s.w), int(s.h));
    hud_.disc(cx, cy, r);
    hud_.end();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    // DF.EXE 0x450600 / 0x440ff0: the scope view's FOV parameter is 28 /
    // magnification (integer), its half-angle (fov + 1) >> 1 whole degrees,
    // and the focal length round(r / tan(half) + 0.5) in pixels of the
    // 640-wide original (r = 100), across the inset's width.
    (void)fovY;
    const float size = 2 * r;
    const int fovParam = 28 / std::max(1, int(arsenal_.zoom()));
    const float half = float((fovParam + 1) >> 1) * kPi / 180.f;
    const float focal = std::floor(100.f / std::tan(half) + 0.5f);
    scopeFocal_ = focal * 640.f / 200.f;   // per 640 pixels of view width
    const float insetFov = 2 * std::atan(100.f / focal);
    glViewport(int(cx - r), int(s.h - cy - r), int(size), int(size));
    glClear(GL_DEPTH_BUFFER_BIT);
    drawWorld(view, perspective(insetFov, 1.f, 0.1f, viewDistance_ * 1.1f), true);
    glDisable(GL_STENCIL_TEST);
    glViewport(0, 0, int(s.w), int(s.h));
}

void Game::hudBox(const Screen& s, float x0, float y0, float x1, float y1, bool right) {
    // DF.EXE 0x401de4: no fill; dark green scanlines every 2 pixels from
    // y1 + 2, then a one-pixel outline in the HUD green.
    const float X0 = right ? s.rx(x0) : s.lx(x0), X1 = right ? s.rx(x1) : s.lx(x1);
    const float Y0 = s.y(y0), Y1 = s.y(y1), t = s.px();
    for (float yy = Y0 + 2 * t; yy < Y1; yy += 2 * t) hud_.rect(X0, yy, X1 - X0, t, kHudDark);
    hud_.rect(X0, Y0, X1 - X0, t, kHudGreen);
    hud_.rect(X0, Y1 - t, X1 - X0, t, kHudGreen);
    hud_.rect(X0, Y0, t, Y1 - Y0, kHudGreen);
    hud_.rect(X1 - t, Y0, t, Y1 - Y0, kHudGreen);
}

void Game::outlinedText(float x, float y, const std::string& t, Color c, float scale, Color outline) {
    // DF.EXE 0x43ddf4: the text four times in black, one pixel around, then in colour.
    const float o = std::max(1.f, scale);
    for (auto [dx, dy] : {std::pair{-o, 0.f}, {o, 0.f}, {0.f, -o}, {0.f, o}}) hud_.text(x + dx, y + dy, t, outline, scale);
    hud_.text(x, y, t, c, scale);
}

void Game::drawTopBar(const Screen& s) {
    // DF.EXE 0x482dd0 and the functions it calls; 640x480 units.
    const float t = s.px(), ts = s.text();
    hud_.rect(0, 0, s.w, s.top, kHudBlack);
    hud_.rect(0, 0, s.w, t, kHudGreen);                       // y 0
    hud_.rect(0, s.top - t, s.w, t, kHudGreen);               // y 59
    hud_.rect(0, 0, t, s.top, kHudGreen);                     // x 0
    hud_.rect(s.w - t, 0, t, s.top, kHudGreen);               // x W-1
    hud_.rect(s.rx(kTopDivider), 0, t, s.top, kHudGreen);     // divider x 350

    // Stance (0x484c74): POS800.PCX frames prone, crouched, standing; centred
    // on x 50, top y 3.75, drawn 1:1.
    if (stanceIcons_.texture) {
        const int frame = player_.stance == Stance::Prone ? 0 : player_.stance == Stance::Crouch ? 1 : 2;
        const float fh = stanceIcons_.height / 3.f, w = stanceIcons_.width * s.art(), h = fh * s.art();
        hud_.imageUV(stanceIcons_.texture, s.lx(50) - w / 2, s.y(3.75f), w, h, 0, frame / 3.f, 1, (frame + 1) / 3.f);
    }

    // Weapon name (0x483444) at (75, 10); red while reloading or out of
    // ammunition (0x44b0a0).
    const auto& sl = arsenal_.current();
    const bool outOfAmmo = sl.mags <= 0 && sl.rounds <= 0;
    hud_.text(s.lx(75), s.y(10), arsenal_.modeName(), arsenal_.reloading() || outOfAmmo ? kHudRed : kHudGreen, ts);

    // Magazines (x 75-135) with the magazine glyph (font character 1) and
    // rounds (x 140-200), y 28.75-47.5; the narrow "round chambered" bar at
    // x 205-210 is solid when a round is chambered, otherwise an empty box.
    hudBox(s, 75, 28.75f, 135, 47.5f, false);
    hudBox(s, 140, 28.75f, 200, 47.5f, false);
    hud_.text(s.lx(78.1f), s.y(31.9f), "\x01", kHudGreen, ts);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d", sl.mags);
    hud_.text(s.lx(105) - hud_.textWidth(buf, ts) / 2, s.y(31.9f), buf, kHudGreen, ts);
    std::snprintf(buf, sizeof buf, "%d", sl.rounds);
    hud_.text(s.lx(170) - hud_.textWidth(buf, ts) / 2, s.y(31.9f), buf, kHudGreen, ts);
    if (sl.rounds > 0 && !arsenal_.reloading()) hud_.rect(s.lx(205), s.y(28.75f), s.lx(210) - s.lx(205), s.y(47.5f) - s.y(28.75f), kHudGreen);
    else hudBox(s, 205, 28.75f, 210, 47.5f, false);

    // Weapon silhouette (0x484ae8): WEAP800.PCX, 13 frames, drawn 1:1 with
    // its right edge at x 325 and top at y 16.25.
    if (weaponIcons_.texture) {
        const int frame = arsenal_.def().icon;
        const float fh = weaponIcons_.height / 13.f, w = weaponIcons_.width * s.art(), h = fh * s.art();
        hud_.imageUV(weaponIcons_.texture, s.lx(325) - w, s.y(16.25f), w, h, 0, frame / 13.f, 1, (frame + 1) / 13.f);
    }

    // Compass (0x483d9c): north-up ring (382, 30) radius 20 in HUD green,
    // letters from font characters 2-5; an inner circle (radius 5) and a
    // heading needle (length 10) in the team colour (blue for side 1); the
    // next waypoint as a bright green dot (radius 2) on the ring.
    {
        const float cx = s.rx(382.5f), cy = s.y(30), r = s.y(20);
        hud_.ring(cx, cy, r, t, kHudGreen);
        auto glyph = [&](const char* g, float x, float y) {
            hud_.text(s.rx(x) - hud_.textWidth(g, ts) / 2, s.y(y), g, kHudGreen, ts);
        };
        glyph("\x02", 382.5f, 0);    // N
        glyph("\x03", 382.5f, 47.5f);// S
        glyph("\x04", 407.5f, 25);   // E
        glyph("\x05", 357.5f, 25);   // W
        hud_.ring(cx, cy, s.y(5), t, kHudBlue);
        const float dx = std::sin(player_.yaw), dy = -std::cos(player_.yaw);
        hud_.line(cx, cy, cx + dx * s.y(10), cy + dy * s.y(10), t, kHudBlue);
        if (!route_.empty()) {
            const RoutePoint& p = route_[size_t(routeIndex_)];
            const double ex = p.x - player_.missionX(), ny = p.y - player_.missionY();
            const double len = std::sqrt(ex * ex + ny * ny);
            if (len > 0.5) hud_.ring(cx + float(ex / len) * r, cy - float(ny / len) * r, t, 2 * t, kHudBright);
        }
    }

    // Grid square (0x4836b8), carried item (0x483718), waypoint (0x483d48).
    hudBox(s, 418.75f, 10, 481.25f, 28.75f, true);
    hudBox(s, 487.5f, 10, 631.25f, 28.75f, true);
    hudBox(s, 418.75f, 35, 631.25f, 53.75f, true);
    std::string grid = gridName(player_.missionX(), player_.missionY());
    hud_.text(s.rx(450) - hud_.textWidth(grid, ts) / 2, s.y(13.1f), grid, kHudGreen, ts);
    hud_.text(s.rx(490.6f), s.y(13.1f), "Carrying: Nothing", kHudGreen, ts);
    // "WP%i: %s (%s)", distance "%im" or "%ikm+" (0x4838ac): whole metres of
    // x and y difference, integer square root; "None" without a route.
    std::string wp = "None";
    if (!route_.empty()) {
        const RoutePoint& p = route_[size_t(routeIndex_)];
        const long dx = long(std::floor(p.x - player_.missionX())), dy = long(std::floor(p.y - player_.missionY()));
        const long d = long(std::sqrt(double(dx * dx + dy * dy)));
        char dist[32], line[128];
        if (d < 1000) std::snprintf(dist, sizeof dist, "%ldm", d);
        else std::snprintf(dist, sizeof dist, "%ldkm+", d / 1000);
        std::snprintf(line, sizeof line, "WP%d: %s (%s)", routeIndex_ + 1, p.name.c_str(), dist);
        wp = line;
    }
    hud_.text(s.rx(425), s.y(38.1f), wp, kHudGreen, ts);
}

void Game::drawBottomBar(const Screen& s) {
    const float t = s.px(), ts = s.text();
    const float y0 = s.h - s.bottom;
    hud_.rect(0, y0, s.w, s.bottom, kHudBlack);
    hud_.rect(0, y0, s.w, t, kHudGreen);                      // y 420
    hud_.rect(0, s.h - t, s.w, t, kHudGreen);                 // y 479
    hud_.rect(0, y0, t, s.bottom, kHudGreen);
    hud_.rect(s.w - t, y0, t, s.bottom, kHudGreen);
    hud_.rect(s.rx(kBottomDivider), y0, t, s.bottom, kHudGreen);

    // Messages (0x483390): up to three lines at x 6.25, y 430 / 445 / 460,
    // oldest on top; each stays 1240 ticks (0x42f400).
    for (size_t i = 0; i < messages_.size(); ++i)
        hud_.text(s.lx(6.25f), s.by(430 + 15 * float(i)), messages_[i].text, kHudMessage, ts);

    // Soldier name and mission name (0x484e54) at x 437.5, y 426.25 / 441.9.
    hud_.text(s.rx(437.5f), s.by(426.25f), o_.soldierName, kHudGreen, ts);
    hud_.text(s.rx(437.5f), s.by(441.9f), missionName_, kHudGreen, ts);
}

void Game::drawMapMarker(const Screen& s, float x, float y, int type, Color c, float heading, bool dead) {
    // DF.EXE 0x43e434: radius 5 virtual units, doubled on the command map.
    const float r = s.y(6.25f), t = s.px();
    switch (type) {
    case 1: hud_.ring(x, y, r, t, c); break;                                  // hollow circle
    case 2: hud_.ring(x, y, r / 2, r, c); hud_.ring(x, y, r, t, kHudBlack); break;   // filled circle, black outline
    case 3:                                                                   // diamond
        hud_.line(x - r, y, x, y - r, t, c); hud_.line(x, y - r, x + r, y, t, c);
        hud_.line(x + r, y, x, y + r, t, c); hud_.line(x, y + r, x - r, y, t, c);
        break;
    case 4: hud_.line(x - r, y, x + r, y, t, c); hud_.line(x, y - r, x, y + r, t, c); break;   // plus
    case 5:                                                                   // soldier
        if (dead) { hud_.line(x - r, y - r, x + r, y + r, t, c); hud_.line(x - r, y + r, x + r, y - r, t, c); break; }
        hud_.ring(x, y, r, t, c);
        hud_.line(x, y, x + std::sin(heading) * 2 * r, y - std::cos(heading) * 2 * r, t, c);
        break;
    case 6:                                                                   // square
        hud_.line(x - r, y - r, x + r, y - r, t, c); hud_.line(x + r, y - r, x + r, y + r, t, c);
        hud_.line(x + r, y + r, x - r, y + r, t, c); hud_.line(x - r, y + r, x - r, y - r, t, c);
        break;
    case 7:                                                                   // circle with X
        hud_.ring(x, y, r, t, c);
        hud_.line(x - r * 0.7f, y - r * 0.7f, x + r * 0.7f, y + r * 0.7f, t, c);
        hud_.line(x - r * 0.7f, y + r * 0.7f, x + r * 0.7f, y - r * 0.7f, t, c);
        break;
    case 8:                                                                   // triangle pointing east
        hud_.line(x - r, y - r, x + r, y, t, c); hud_.line(x + r, y, x - r, y + r, t, c);
        hud_.line(x - r, y + r, x - r, y - r, t, c);
        break;
    default: break;
    }
}

void Game::drawCommandMap(const Screen& s) {
    // DF.EXE 0x43cfc8: the terrain colour map over the whole 3D view, centred
    // on the player, north up. Scale [0x933540] (default 1) is world metres
    // per pixel x 1024 / W, i.e. 0.625 / scale pixels per metre at 640.
    const float x0 = 0, y0 = s.playY, w = s.w, h = s.playH, t = s.px();
    const float pxPerM = s.u * 0.625f / mapScale_;
    const float ext = terrain_.mapExtent();
    const float halfW = w / pxPerM * 0.5f, halfH = h / pxPerM * 0.5f;
    const Vec3 c = player_.feet;
    const float u0 = (c.x - halfW) / ext, u1 = (c.x + halfW) / ext;
    const float v0 = (c.z - halfH) / ext, v1 = (c.z + halfH) / ext;
    hud_.imageUV(terrain_.colorTexture(), x0, y0, w, h, u0, v0, u1, v1);

    const double mx = player_.missionX(), my = player_.missionY();
    auto toScreen = [&](double x, double y) {
        return std::pair<float, float>{x0 + w / 2 + float(x - mx) * pxPerM, y0 + h / 2 + float(my - y) * pxPerM};
    };

    // Grid (0x43e16c / 0x43e258): 100 m, one pixel, bright green; labels
    // white with a black outline: columns centred at the top, rows at the left.
    const float sq = float(kGridSquare) * pxPerM, ts = s.text();
    for (double gx = std::ceil((mx - halfW) / kGridSquare) * kGridSquare; gx <= mx + halfW; gx += kGridSquare) {
        const float sx = toScreen(gx, my).first;
        hud_.rect(sx, y0, t, h, kHudBright);
        const std::string label = gridColumn(gx + kGridSquare / 2);
        outlinedText(sx + sq * 0.5f - hud_.textWidth(label, ts) / 2, y0 + t, label, kHudWhite, ts, kHudBlack);
    }
    for (double gy = std::floor((my + halfH) / kGridSquare) * kGridSquare; gy >= my - halfH; gy -= kGridSquare) {
        const float sy = toScreen(mx, gy).second;
        hud_.rect(x0, sy, w, t, kHudBright);
        outlinedText(x0 + 2 * t, sy + sq * 0.5f - s.y(4), std::to_string(gridRow(gy - kGridSquare / 2)), kHudWhite, ts,
                     kHudBlack);
    }

    // Route lines between the player's waypoints (0x43e340), bright green.
    for (size_t i = 0; i + 1 < route_.size(); ++i) {
        auto [ax, ay] = toScreen(route_[i].x, route_[i].y);
        auto [bx, by] = toScreen(route_[i + 1].x, route_[i + 1].y);
        hud_.line(ax, ay, bx, by, t, kHudBright);
    }

    // Map objects: anything in file groups 0-2 whose record +0x4C is nonzero
    // is drawn with that marker type in its side's colour (0 green, 1 blue,
    // 2 red). The current waypoint blinks: drawn while tick bit 4 is set
    // (0x43d975; 16 ticks on, 16 off).
    auto sideColour = [](int side) { return side == 1 ? kHudBlue : side == 2 ? kHudRed : kHudPureGreen; };
    const MissionObject* current = nullptr;
    if (!route_.empty()) current = route_[size_t(routeIndex_)].object;
    for (const auto& o : mission_.objects) {
        const int type = o.raw[19] & 0xFFFF;
        if (!type || o.group == 3) continue;
        if (&o == current && !(ticks_ & 16)) continue;
        auto [sx, sy] = toScreen(o.x, o.y);
        if (sx < x0 - 20 || sx > x0 + w + 20 || sy < y0 - 20 || sy > y0 + h + 20) continue;
        drawMapMarker(s, sx, sy, type, sideColour((uint32_t(o.raw[16]) >> 24) & 0xFF), 0, false);
    }
    // Soldiers by the GPS option (0x43d445): 0 none, 1 (default) those whose
    // record +0x4C is set, 2 all; drawn as soldier markers, an X once dead.
    if (o_.gpsIcons > 0)
        for (const auto& o : world_.objects) {
            if (o.missionGroup != 3 || !o.person || o.removed) continue;
            if (o_.gpsIcons == 1 && !o.mapMarker) continue;
            Vec3 at = world_.nearest(o.pos, c);
            const float sx = x0 + w / 2 + (at.x - c.x) * pxPerM, sy = y0 + h / 2 + (at.z - c.z) * pxPerM;
            drawMapMarker(s, sx, sy, 5, sideColour(o.side), o.heading, o.dead);
        }
    // The player: a soldier marker in the team colour (side 1, blue).
    drawMapMarker(s, x0 + w / 2, y0 + h / 2, 5, kHudBlue, player_.yaw, false);

    // Compass rose (0x43dec0): centre (560, 315), arms 40, inner points 5;
    // the main cross, a small X, and a four-pointed star; N in white, E S W
    // in green (bright, as the rest of the rose: assumed), black-outlined.
    {
        const float rx = s.rx(560), ry = s.y(315), a = s.y(40), i = s.y(5);
        hud_.line(rx - a, ry, rx + a, ry, t, kHudBright);
        hud_.line(rx, ry - a, rx, ry + a, t, kHudBright);
        hud_.line(rx - i, ry - i, rx + i, ry + i, t, kHudBright);
        hud_.line(rx - i, ry + i, rx + i, ry - i, t, kHudBright);
        const float tips[4][2] = {{0, -a}, {a, 0}, {0, a}, {-a, 0}};
        for (auto& tp : tips) {
            const float tx = rx + tp[0], ty = ry + tp[1];
            // the two inner points next to this tip
            const float ax = tp[0] != 0 ? (tp[0] > 0 ? i : -i) : -i, ay = tp[1] != 0 ? (tp[1] > 0 ? i : -i) : -i;
            const float bx = tp[0] != 0 ? ax : i, by = tp[1] != 0 ? ay : i;
            hud_.line(tx, ty, rx + ax, ry + ay, t, kHudBright);
            hud_.line(tx, ty, rx + bx, ry + by, t, kHudBright);
        }
        auto label = [&](const char* l, float x, float y, Color col) {
            outlinedText(s.rx(x) - hud_.textWidth(l, ts) / 2, s.y(y), l, col, ts, kHudBlack);
        };
        label("N", 561, 266, kHudWhite);
        label("E", 606, 311, kHudBright);
        label("S", 561, 356, kHudBright);
        label("W", 516, 311, kHudBright);
    }
}

void Game::drawHud(const Screen& s, const Mat4& viewProj) {
    hud_.begin(int(s.w), int(s.h));

    // Bullet impacts as small puffs: dust on the ground, dark on objects.
    for (const auto& i : arsenal_.impacts()) {
        float sx, sy, depth;
        if (!project(viewProj, i.pos, s.w, s.playH, sx, sy, depth)) continue;
        sy += s.playY;
        float r = std::clamp(60.f / depth, 1.5f, 18.f) * s.u * (1 + i.age * 1.5f);
        float a = std::max(0.f, 1 - i.age / 1.5f) * 0.8f;
        Color c = i.object ? Color{0.15f, 0.15f, 0.15f, a} : Color{0.72f, 0.65f, 0.52f, a};
        hud_.puff(sx, sy, r, c);
    }

    const float cx = s.w * 0.5f, cy = s.playY + s.playH * 0.5f, t = s.px();
    if (showMap_) drawCommandMap(s);
    else if (crosshair_) {
        // DF.EXE 0x401f94: four one-pixel arms in bright green, 4..10 to the
        // left/top and 5..11 to the right/bottom of the centre.
        hud_.rect(cx - s.y(10), cy, s.y(6), t, kHudBright);
        hud_.rect(cx + s.y(5), cy, s.y(6), t, kHudBright);
        hud_.rect(cx, cy - s.y(10), t, s.y(6), kHudBright);
        hud_.rect(cx, cy + s.y(5), t, s.y(6), kHudBright);
    }

    // Scope inset (0x450600): centre (480, 240), radius 100, a one-pixel
    // bright green border, ticks from r/4 to r/2 in the four directions, the
    // range "%3im" at (500, 312.5).
    if (scopeShown() && !showMap_) {
        const float zx = s.cx(160), zy = cy, r = s.y(100);
        hud_.ring(zx, zy, r, t, kHudBright);
        const float in = r * 0.25f, out = r * 0.5f;
        hud_.rect(zx - out, zy, out - in, t, kHudBright);
        hud_.rect(zx + in, zy, out - in, t, kHudBright);
        hud_.rect(zx, zy - out, t, out - in, kHudBright);
        hud_.rect(zx, zy + in, t, out - in, kHudBright);
        const float range = rangeToAim();
        char buf[32];
        if (range < 0 || range > 999) std::snprintf(buf, sizeof buf, "1km+");
        else if (range < 1) std::snprintf(buf, sizeof buf, "1m-");
        else std::snprintf(buf, sizeof buf, "%3dm", int(range));
        hud_.text(s.cx(180), s.y(312.5f), buf, kHudBright, s.text());
    }

    if (showGoals_) drawGoals(s);
    if (missionResult_ >= 0) drawMissionEnd(s);

    drawTopBar(s);
    drawBottomBar(s);
    hud_.end();
}

void Game::drawMissionEnd(const Screen& s) {
    // DF.EXE 0x483094: success if the winner is the player's side. A darkened
    // panel (0x401b6c) from (128, 116) to (896, 246) virtual, i.e. x 80-560,
    // y 72.5-153.75, shade level 10 of the 32-step shade table (0x435070:
    // level r keeps r/31 of the brightness, so 10/31), with a one-pixel
    // outline in bright green (0x66f94c); three lines centred at x 512 in
    // green (0x66f938) with tops at 136, 176, 206 virtual.
    const bool won = missionResult_ == kPlayerSide;
    const float x0 = s.cx(-240), x1 = s.cx(240), y0 = s.y(72.5f), y1 = s.y(153.75f), t = s.px();
    hud_.rect(x0, y0, x1 - x0, y1 - y0, {0, 0, 0, 1 - 10.f / 31.f});
    hud_.rect(x0, y0, x1 - x0, t, kHudBright);
    hud_.rect(x0, y1 - t, x1 - x0, t, kHudBright);
    hud_.rect(x0, y0, t, y1 - y0, kHudBright);
    hud_.rect(x1 - t, y0, t, y1 - y0, kHudBright);
    const char* lines[] = {won ? "Congratulations!  Mission Completed Successfully." : "Game Over.  Mission Incomplete.",
                           "Hit Enter to end mission.", "Hit Space Bar to replay mission"};
    const float tops[] = {85, 110, 128.75f};
    for (int i = 0; i < 3; ++i)
        hud_.text(s.cx(0) - hud_.textWidth(lines[i], s.text()) / 2, s.y(tops[i]), lines[i], kHudPureGreen, s.text());
}

void Game::drawGoals(const Screen& s) {
    // DF.EXE 0x485004: no panel; right-aligned at x 625 from y 72.5, lines
    // 25 virtual units (15.6) apart, each with a (1,1) dark green shadow.
    // "Win Conditions:" then up to four M%02iSUBGOAL%02i lines (green once
    // met, white until then); 10 units further "Lose Conditions:" and up to
    // four M%02iSUBFAIL%02i lines (red once hit, white until then).
    const float ts = s.text(), sh = s.px();
    float y = 72.5f;
    auto line = [&](const std::string& text, Color c) {
        const float x = s.rx(625) - hud_.textWidth(text, ts);
        hud_.text(x + sh, s.y(y) + sh, text, kHudDark, ts);
        hud_.text(x, s.y(y), text, c, ts);
        y += 15.625f;
    };
    for (int part = 0; part < 2; ++part) {
        std::vector<std::pair<std::string, bool>> items;
        for (int n = 1; n <= 4; ++n) {
            char key[24];
            std::snprintf(key, sizeof key, part == 0 ? "M%02dSUBGOAL%02d" : "M%02dSUBFAIL%02d", missionNumber_, n);
            if (const std::string* t = campText_.find("Goals", key))
                items.push_back({*t, part == 0 ? script_.goalDone(n) : script_.failDone(n)});
        }
        if (items.empty()) continue;
        if (part == 1) y += 6.25f;
        line(part == 0 ? "Win Conditions:" : "Lose Conditions:", kHudWhite);
        for (auto& [text, met] : items) line(text, met ? (part == 0 ? kHudGreen : kHudRed) : kHudWhite);
    }
}

int Game::run() {
    std::vector<uint8_t> raw;
    if (!vfs_.read("ITEMS.DEF", raw) || !items_.parse(std::string(raw.begin(), raw.end()))) {
        std::fprintf(stderr, "cannot read ITEMS.DEF\n");
        return 1;
    }
    for (auto& n : vfs_.list(".BMS")) missions_.push_back(n.substr(0, n.size() - 4));
    if (vfs_.read("DFGAME.BIN", raw)) parseRtxt(raw, gameText_);
    {
        // The player soldier (ITEMS.DEF "player01") names its move table.
        const ItemDef* soldier = items_.byId(kItemPlayerSoldier);
        const std::string anm = toUpper(soldier && !soldier->animDef.empty() ? soldier->animDef : "playanim") + ".ANM";
        if (!vfs_.read(anm, raw) || !playerMoves_.parse(std::string(raw.begin(), raw.end())))
            std::fprintf(stderr, "cannot read %s; using built-in movement speeds\n", anm.c_str());
        // Eye heights: the pose's origin height (.CHR contact points) plus the
        // eye offset, 0.7 m, or 0.4 m prone (DF.EXE 0x449913).
        Chr chr;
        const std::string chrName = toUpper(soldier && !soldier->chrFile.empty() ? soldier->chrFile : "player01") + ".CHR";
        if (vfs_.read(chrName, raw) && chr.parse(raw)) {
            const char* stances[3] = {"Standing", "Crouched", "Prone"};
            for (int st = 0; st < 3; ++st)
                for (int h = 0; h < 2; ++h) {
                    const AnimMove* mv = playerMoves_.find(std::string(stances[st]) + (h ? "_1HW" : "_2HW"));
                    const ChrAnimation* a = mv ? chr.animation(mv->animation) : nullptr;
                    if (a) eyeHeightTable_[st][h] = Chr::originHeight(a->frames[0]) + (st == 2 ? 0.4f : 0.7f);
                }
        }
    }
    {
        const auto exePath = findGameExe(o_.gameDir);
        std::string err;
        if (exePath.empty()) std::fprintf(stderr, "DF.EXE not found; using built-in weapon values\n");
        else if (!exe_.load(exePath, &err)) std::fprintf(stderr, "%s; using built-in weapon values\n", err.c_str());
        if (!controls_.init(&exe_)) std::fprintf(stderr, "using built-in key bindings\n");
    }
    if (missions_.empty()) { std::fprintf(stderr, "no missions found\n"); return 1; }
    auto it = std::find(missions_.begin(), missions_.end(), toUpper(o_.mission));
    if (it == missions_.end()) { std::fprintf(stderr, "unknown mission %s\n", o_.mission.c_str()); return 1; }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);   // zoom inset mask
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const bool test = !o_.screenshot.empty();
    const bool scripted = test || !o_.inputScript.empty() || !o_.record.empty();
    // Fixed 60 Hz steps make a run repeatable; watching paces it in real time.
    const bool fixedStep = !o_.record.empty() || (test && !o_.watch);
    const bool hidden = scripted && !o_.watch;
    if (!o_.inputScript.empty() && !loadInputScript(o_.inputScript)) {
        std::fprintf(stderr, "cannot read input script %s\n", o_.inputScript.c_str());
        return 1;
    }
    win_ = SDL_CreateWindow("OpenDF", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, o_.width, o_.height,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE));
    if (!win_) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win_);
    if (!ctx) { std::fprintf(stderr, "OpenGL 3.3 context: %s\n", SDL_GetError()); SDL_DestroyWindow(win_); return 1; }
    SDL_GL_SetSwapInterval(1);
    int rc = 0;
    const char* missing = nullptr;
    std::string err;
    if (!loadGl(&missing)) { std::fprintf(stderr, "missing OpenGL function %s\n", missing); rc = 1; }
    else if (!models_.init(&err) || !hud_.init(vfs_, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); rc = 1; }
    else if (!loadMission(int(it - missions_.begin()))) rc = 1;

    if (rc == 0) {
        if (o_.hasCamera) {
            player_ = Player{};
            player_.moves = &playerMoves_;
    std::memcpy(player_.eyeHeights, eyeHeightTable_, sizeof eyeHeightTable_);
            player_.feet = {o_.camera[0], 0, -o_.camera[1]};
            player_.wrap(terrain_.mapExtent());
            player_.feet.y = world_.groundHeight(player_.feet, 0.45f) + o_.camera[2];
            player_.yaw = o_.camera[3] * kPi / 180.f;
            player_.pitch = o_.camera[4] * kPi / 180.f;
            player_.fly = o_.camera[2] > 2;
        }
        if (o_.weapon >= 0) arsenal_.select(o_.weapon);
        if (o_.stance >= 0) player_.stance = Stance(std::clamp(o_.stance, 0, 2));
        for (int i = 0; i < o_.zoom; ++i) arsenal_.toggleZoom();
        showMap_ = o_.showMap;
        if (o_.killEnemies)
            for (size_t i = 0; i < world_.objects.size(); ++i)
                if (world_.objects[i].side == 2 && world_.objects[i].missionGroup == 3) world_.damage(int(i), 1000);

        captured_ = !scripted;
        FILE* video = nullptr;
        int videoW = 0, videoH = 0;
        std::vector<uint8_t> videoFrame;
        float endHold = -1;   // after a watched script ends: keep showing for a moment
        SDL_SetRelativeMouseMode(captured_ ? SDL_TRUE : SDL_FALSE);
        Uint64 last = SDL_GetPerformanceCounter();
        bool running = true, prevTrigger = false;
        int frame = 0;
        float fpsTimer = 0;
        int fpsFrames = 0;
        while (running) {
            Uint64 now = SDL_GetPerformanceCounter();
            float dt = fixedStep ? 1.f / 60.f : std::min(float(now - last) / SDL_GetPerformanceFrequency(), 0.1f);
            last = now;

            SDL_Event e;
            bool zoomClick = false;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                else if (e.type == SDL_MOUSEBUTTONDOWN && !captured_) { captured_ = true; SDL_SetRelativeMouseMode(SDL_TRUE); }
                else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) zoomClick = true;   // scope (original: S or right mouse)
                else if (e.type == SDL_MOUSEMOTION && captured_ && !showMap_) {
                    // DF.EXE 0x42eba0: 0x200000 binary angle per count at the
                    // nominal mouse scale (0.1758°), a quarter while zoomed.
                    const float sens = 0.17578125f * kPi / 180.f * (arsenal_.zoomed() ? 0.25f : 1.f);
                    player_.yaw += e.motion.xrel * sens;
                    player_.pitch = std::clamp(player_.pitch - e.motion.yrel * sens, -67.5f * kPi / 180.f, 90.f * kPi / 180.f);
                } else if (e.type == SDL_KEYDOWN && !e.key.repeat) handleKey(e.key);
            }
            if (zoomClick) arsenal_.toggleZoom();

            if (!inputCmds_.empty()) runInputScript(dt);
            const Uint8* kb = SDL_GetKeyboardState(nullptr);
            Uint8 keys[SDL_NUM_SCANCODES];
            std::memcpy(keys, kb, sizeof keys);
            for (SDL_Scancode sc : inputHeld_) keys[sc] = 1;
            const Uint32 mouse = captured_ ? SDL_GetMouseState(nullptr, nullptr) : 0;
            auto down = [&](Action a) { return controls_.down(a, keys, mouse); };
            PlayerInput in;
            in.forward = float(down(Action::MoveForward)) - float(down(Action::MoveBack));
            in.strafe = float(down(Action::SlideRight)) - float(down(Action::SlideLeft));
            in.walk = down(Action::NoRun);
            in.jump = down(Action::Jump);
            in.up = down(Action::Jump);
            in.down = down(Action::Crouch) && player_.fly;
            if (o_.walkForward) in.forward = 1;
            if (showMap_) in = PlayerInput{};
            // Keyboard turning and looking (DF.EXE 0x436fec): 0x1fffffe binary
            // angle per 30 Hz controller tick (84.375°/s), half that with NoRun
            // held; the same for pitch, which stays within -67.5°..+90° (0x437435).
            if (!showMap_) {
                const float rate = (in.walk ? 42.1875f : 84.375f) * kPi / 180.f * dt;
                player_.yaw += rate * (float(down(Action::TurnRight)) - float(down(Action::TurnLeft)));
                player_.pitch += rate * (float(down(Action::LookUp)) - float(down(Action::LookDown)));
            }
            player_.pitch = std::clamp(player_.pitch, -67.5f * kPi / 180.f, 90.f * kPi / 180.f);
            // Any move or jump key turns the scope off (0x4373ab).
            if (in.forward != 0 || in.strafe != 0 || in.jump) arsenal_.cancelZoom();
            const bool trigger = !showMap_ && (down(Action::Fire) || o_.holdFire);

            player_.oneHanded = arsenal_.def().oneHanded;
            in.shooting = arsenal_.firing();
            player_.update(dt, in, world_);
            updateRoute();
            // Mission events run on the original's 60 Hz ticks.
            for (scriptClock_ += dt; scriptClock_ >= 1.f / 60.f; scriptClock_ -= 1.f / 60.f) {
                ++ticks_;
                if (missionResult_ < 0) script_.tick(*this);
            }
            std::vector<std::string> msgs;
            arsenal_.update(dt, trigger, trigger && !prevTrigger, player_, world_, msgs);
            prevTrigger = trigger;
            for (auto& m : msgs) messages_.push_back({m, 0});
            for (auto& m : messages_) m.age += dt;
            // At most three lines; each leaves 1240 ticks after it arrived (DF.EXE 0x42f400).
            while (messages_.size() > 3 || (!messages_.empty() && messages_.front().age > 1240.f / 60.f)) messages_.pop_front();
            world_.update(dt);
            missionTime_ += dt;

            int dw, dh;
            SDL_GL_GetDrawableSize(win_, &dw, &dh);
            Screen s;
            s.w = float(dw);
            s.h = float(dh);
            s.u = s.h / 480.f;
            s.top = kTopBar * s.u;
            s.bottom = kBottomBar * s.u;
            s.playY = s.top;
            s.playH = std::max(1.f, s.h - s.top - s.bottom);
            // The largest of the original's modes (640x480, 800x600,
            // 1024x768) that fits the window height.
            s.modeW = s.h >= 768 ? 1024 : s.h >= 600 ? 800 : 640;
            useHudMode(s.modeW);

            glViewport(0, 0, dw, dh);
            glClearColor(0, 0, 0, 1);
            glClearStencil(0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            // 3D view between the two HUD bars.
            glViewport(0, int(s.bottom), dw, int(s.playH));
            const float fovY = kOriginalFovY;
            Mat4 proj = perspective(fovY, s.w / s.playH, 0.1f, viewDistance_ * 1.1f);
            // While zoomed the camera carries the scope's random offset (0x404246).
            Player cam = player_;
            if (arsenal_.zoomed()) { cam.yaw += arsenal_.scopeYaw(); cam.pitch += arsenal_.scopePitch(); }
            Mat4 view = lookDir(player_.eye(), cam.forward(), {0, 1, 0});
            if (o_.viewModelOnly) {
                glClearColor(1, 0, 1, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                drawViewModel(s);
                glViewport(0, 0, dw, dh);
            } else {
                drawWorld(view, proj, false);
                drawViewModel(s);
                drawZoomInset(s, view, fovY);
                glViewport(0, 0, dw, dh);
                drawHud(s, proj * view);
            }

            if (!o_.record.empty()) {
                // Pipe the frame to ffmpeg (H.264, 30 fps, 1280 wide).
                if (!video) {
                    videoW = dw & ~1;
                    videoH = dh & ~1;
                    char cmd[1024];
                    std::snprintf(cmd, sizeof cmd,
                                  "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r 60 -i - "
                                  "-vf vflip,fps=30,scale=1280:-2 -c:v libx264 -pix_fmt yuv420p -crf 20 \"%s\"",
                                  videoW, videoH, o_.record.c_str());
                    video = popen(cmd, "w");
                    if (!video) std::fprintf(stderr, "cannot start ffmpeg for --record\n");
                    videoFrame.resize(size_t(videoW) * videoH * 3);
                }
                if (video) {
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glReadPixels(0, 0, videoW, videoH, GL_RGB, GL_UNSIGNED_BYTE, videoFrame.data());
                    fwrite(videoFrame.data(), 1, videoFrame.size(), video);
                }
            }
            if (inputDone_ && endHold < 0) endHold = 1.5f;
            if (endHold >= 0 && (endHold -= dt) < 0) running = false;
            if (test && ++frame >= o_.frames) {
                writePpm(o_.screenshot, dw, dh);
                std::printf("wrote %s (%dx%d)\n", o_.screenshot.c_str(), dw, dh);
                for (auto& m : messages_) std::printf("  message: %s\n", m.text.c_str());
                if (missionResult_ >= 0) std::printf("  mission ended, winner side %d\n", missionResult_);
                std::printf("  player at %.2f, %.2f (grid %s), %.2f m above ground\n", player_.missionX(), player_.missionY(),
                            gridName(player_.missionX(), player_.missionY()).c_str(),
                            player_.feet.y - terrain_.heightAt(player_.feet.x, player_.feet.z));
                running = false;
            }
            SDL_GL_SwapWindow(win_);
            if (!running && video) { pclose(video); video = nullptr; std::printf("recorded %s\n", o_.record.c_str()); }

            ++fpsFrames;
            fpsTimer += dt;
            if (fpsTimer >= 0.5f) {
                char title[200];
                std::snprintf(title, sizeof title, "OpenDF - %s - %.0f fps", mission_.title.c_str(), fpsFrames / fpsTimer);
                SDL_SetWindowTitle(win_, title);
                fpsFrames = 0;
                fpsTimer = 0;
            }
        }
    }
    // GL objects must go before the context.
    world_.objects.clear();
    terrainPtr_.reset();
    modelsPtr_.reset();
    hudPtr_.reset();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win_);
    return rc;
}

} // namespace

int runGame(Vfs& vfs, const GameOptions& options) {
    Game game(vfs, options);
    return game.run();
}

} // namespace df
