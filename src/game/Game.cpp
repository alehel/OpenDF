#include "Game.h"

#include "../render/Gl.h"
#include "../render/Hud.h"
#include "../render/Models.h"
#include "../render/Terrain.h"
#include "../formats/Rtxt.h"
#include "../formats/Chr.h"
#include "../formats/Dlg.h"
#include "../formats/HudDef.h"
#include "../audio/Audio.h"
#include "Controls.h"
#include "Player.h"
#include "Script.h"
#include "Sound.h"
#include "KeyMap.h"
#include "Ai.h"
#include "Vehicles.h"
#include "Moves.h"
#include "Weapons.h"
#include "Weather.h"
#include "World.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <random>
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
// Message text: each message carries its own colour (0x42f400's second
// argument, stored at ring entry +0x78 and drawn by 0x483390 and 0x484d94).
// The single-player callers: -1 (white: the 16-bit glyph store 0x442546
// writes the low word 0xffff; the 8-bit one byte 0xff) for triggered text
// (0x404875), dialog subtitles (0x472f9a), Friendly Tags On/Off (0x42e0b0 /
// 0x42e5eb), gamma (0x42e2db) and "Pansy." (0x42dfbd); white 0x66f930 for
// "New Resolution" (0x4031e5); bright green 0x66f94c for "Ammo reloaded",
// the ammo-box pickup (0x447b90 -> packet 0x1d -> 0x42b57f, with the refill
// 0x44b634); red 0x66f944 for the Fire Missions REQUESTED / REQUESTOK /
// REQUESTDENIED lines (0x44a738, 0x42b6f9, 0x42b72f). The rest are
// multiplayer: chat in blue 0x66f93c (team, 0x42d841) or green 0x66f938
// (all, 0x42d8f5), net text by its colour byte 0 white, 1 blue, 2 green,
// 3 yellow 0x66f940 (0x42b070..0x42b0fc), the flag and session lines white.
const Color kHudMessage = kHudWhite;

// The original lays its HUD out in a virtual 1024x768 space (DF.EXE
// 0x424788); the positions below are those values converted to 640x480
// units (x 0.625). The bars are H/8 = 60 units each (0x404650). The screen is
// scaled by height and, on wide screens, the right-hand group stays anchored
// to the right edge instead of being stretched; things in the 3D view stay
// centred.
// Delta Force 2's HUD colours: text and lines 0xff8000, red 0xff0000.
constexpr Color kDf2Amber{1.f, 128.f / 255.f, 0.f, 1.f}, kDf2Red{1.f, 0.f, 0.f, 1.f};
constexpr float kTopBar = 60, kBottomBar = 60, kTopDivider = 350, kBottomDivider = 431;

// The original's camera (DF.EXE 0x440ff0, FOV 0x5a set at 0x404489): 90°
// horizontal across the 3D view, focal length = half the view width, the
// same for both axes, so 58.72° vertical on the 640x360 view. Wider screens
// keep this vertical angle and see more at the sides (OpenDF's widescreen
// choice, which keeps the weapon where it is).
const float kOriginalFovY = 2.f * std::atan(1.f / (640.f / 360.f));

// The held weapon's fixed turn (0x44d3c0 -> 0x46df64): T * Rz(-30) *
// Ry(-65) * Rx(-30) in DF axes, here in OpenDF's (x, z, -y).
static const Mat4& heldWeaponOffset() {
    static const Mat4 offset = [] {
        const ChrMatrix c = Chr::partRotation({30.f / 360.f, -65.f / 360.f, 30.f / 360.f});
        Mat4 r;
        const int map[3] = {0, 2, 1};
        const float sign[3] = {1, 1, -1};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.at(i, j) = sign[i] * sign[j] * c.m[map[i]][map[j]];
        return r;
    }();
    return offset;
}

struct Message { std::string text; float age; Color colour; };


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
        // DF.EXE's virtual 1024x768 HUD coordinates (centred horizontally).
        float vx(float x) const { return cx(x * 0.625f - 320.f); }
        float vy(float y) const { return this->y(y * 0.625f); }
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
    void drawMapMarker(const Screen& s, float x, float y, int type, Color c, float heading, bool dead, float radius = -1);
    void hudBox(const Screen& s, float x0, float y0, float x1, float y1, bool right);
    void outlinedText(float x, float y, const std::string& t, Color c, float scale, Color outline);
    float rangeToAim() const;
    // The scope inset shows while the scope is on; moving turns it off (0x4373ab).
    bool scopeShown() const { return arsenal_.scopeShown(); }
    Vec3 aimDirection() const;

    Vfs& vfs_;
    GameOptions o_;
    SDL_Window* win_ = nullptr;
    ItemDefs items_;
    std::vector<std::string> gameText_;   // DFGAME.BIN
    ExeImage exe_;                        // the original DF.EXE, for its tables
    ExeImage df2Exe_;                     // Delta Force 2: DF2.EXE, for DF2's own tables
    Controls controls_;                   // the original's key table (from DF.EXE)
    AnimDefs playerMoves_;                // the player soldier's .ANM move table
    float eyeHeightTable_[3][2] = {{1.7558f, 1.7677f}, {1.3768f, 1.3768f}, {0.5031f, 0.5031f}};
    float floatOrigin_ = 0.1489f, swimOrigin_ = 0.3184f;   // PLAYER01.CHR Floating / Swimming
    std::string missionName_;             // from the campaign text (DFCAMPxx.BIN)
    TextTable campText_;                  // DFCAMPxx.BIN
    TextTable wpnText_;                   // Delta Force 2: DF2WPN.BIN (waypoint names)
    TextTable menuText_;                  // DFMENU.BIN
    TextTable gameTable_;                 // DFGAME.BIN by section and key
    // Placed charges: satchels (schg), claymores (clay radio, cly2 motion).
    struct Charge { int object; int item; int fuse; };
    std::vector<Charge> charges_;
    // Fire missions (0x4482e0 / 0x448368): strikes left (BMS header +0x84),
    // up to 16 on the way, the lasered point and its 64-tick window.
    int fireMissions_ = 0;
    struct Strike { int timer; Vec3 target; };
    std::vector<Strike> strikes_;
    Vec3 laserPoint_{0, 0, 0};
    int laserWindow_ = 0;
    float laserRange_ = -1;
    void weaponEvent(const WeaponEvent& e);
    void tickCharges();
    void blowCharge(Charge& c);
    void shrapnel(Vec3 at, float yaw, float pitch, int speedFactor);
    // Shrapnel's random numbers: DF.EXE rand()*2 (0x435db4; its seed call
    // 0x435dbc is a bare ret); DF2.EXE 0x41ac40 (x = rol(x,11) + x, x =
    // rol(x,4) ^ 1, 16 bits), reseeded to 0x5FC by every burst (0x41ac30).
    uint32_t shrapRand() {
        if (!world_.df2Rules) return uint32_t(std::uniform_int_distribution<int>(0, 0x7fff)(effectRng_) * 2);
        auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
        df2Rng_ = rol(df2Rng_, 11) + df2Rng_;
        df2Rng_ = rol(df2Rng_, 4) ^ 1;
        return df2Rng_ & 0xFFFF;
    }
    void shrapSeed() { df2Rng_ = 0x5FC; }
    // Delta Force 2's radar and command-map shapes (0x421510): objects whose
    // obj+0x6E (signed byte record +0x4C) is non-zero, soldiers aside, at
    // their live positions (removed ones skipped); records with no world
    // object (markers) at their BMS positions.
    struct Df2Shape { const MissionObject* mission; const WorldObject* world; int shape, side; };
    template <class F> void forDf2Shapes(F&& f) const {
        std::vector<const WorldObject*> live(mission_.objects.size(), nullptr);
        for (const auto& o : world_.objects)
            if (o.missionIndex >= 0 && size_t(o.missionIndex) < live.size()) live[size_t(o.missionIndex)] = &o;
        for (size_t i = 0; i < mission_.objects.size(); ++i) {
            const MissionObject& mo = mission_.objects[i];
            if (mo.group == 3) continue;
            const WorldObject* w = live[i];
            if (w && w->removed) continue;
            const int shape = w ? w->radarShape : int(int8_t(mo.raw[19] & 0xFF));
            if (!shape) continue;
            f(Df2Shape{&mo, w, shape, int(int8_t((uint32_t(mo.raw[16]) >> 24) & 0xFF))});
        }
    }
    uint32_t df2Rng_ = 0x5FC;
    // Delta Force 2's searchlights (`slgt`, glight 104016), Land Mines
    // (`mine` 104054) and wall triggers (`vtrg` 101062): none is placed in a
    // shipped mission (OPENDF_TESTDF2ITEMS places one of each to test them).
    struct Searchlight {
        int object = -1;
        bool init = false, dead = false;
        uint32_t homeYaw = 0, homePitch = 0, targetYaw = 0, targetPitch = 0;   // +0xA8 / +0xAC / +0x98 / +0xB8
        int32_t sweep = 0, moving = 0;   // +0xB0, +0x9C
        int speed = 0;                   // +0xB4
        int target = -1, counter = 0, wait = 0;   // AI +0x54, +0x48, the think timer
        // AI +0x84: record +0x24 · 65536 / 100 (1.0 if 0), less 0.15 at night
        // when above 0.3 (0x40386c..0x4038c6; that init runs for any object
        // whose item has +0x24 & 4, 0x403740, the light included).
        float perception = 1;
        int alert = 0;                   // AI byte +0xb4, from record byte +0x42 (0x403811)
    };
    std::vector<Searchlight> lights_;
    void tickDf2Items();
    void df2Blast(Vec3 at, int fragments, float heading);
    void fragmentBurst(Vec3 at, int fragments, float heading);
    bool df2Touch(const WorldObject& o, Vec3 origin, Vec3 feet) const;
    void mineContact(int index);
    bool df2ItemDestroyed(int index);
    // DF2.EXE 0x415490: the big blast (kind 5, 12 m) and 200 fragments, as a
    // satchel (vehicle deaths, ibom).
    void bigBlast(Vec3 at);
    // DF2's bld2 collapse (0x4028d0): crush, then 64 steps one per 8 ticks.
    struct Collapse { int object, step, wait, rubble, dust, smoke; float box[4]; };   // box: the 2 m-inset footprint, file axes
    std::vector<Collapse> collapses_;
    void startCollapse(int index);
    void tickCollapses();
    int carried_ = -1;                    // the object the player carries (+0x90)
    bool leaveMission_ = false, quit_ = false;
public:
    MissionOutcome outcome() const {
        MissionOutcome o;
        o.played = true;
        o.won = missionResult_ == kPlayerSide;
        for (const auto& w : world_.objects) o.kills += (w.person && w.dead && w.killer == -3) ? 1 : 0;
        o.quit = quit_;
        return o;
    }
private:
    bool briefingShown_ = false, helpShown_ = false;   // [0x66f96c], [0x66f968]
    std::string briefing_;                // built at mission load (0x4813c4)
    int briefStart_ = 1, briefLast_ = 1, briefPage_ = 0;   // [0xc73704], [0xc73700], [0x4f30dc]
    void touchPickups();
    void dropCarried();
    bool teamCarrying(int team, int itemTeam) const override {
        return team == kPlayerTeam && carried_ >= 0 && world_.objects[size_t(carried_)].team == itemTeam;
    }
    void drawBriefing(const Screen& s);
    void drawHelp(const Screen& s);
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
    // Ambience (ai_function timers, 0x45a5a8 / 0x45a830 / sndN): objects
    // that repeat their interrupt_sound_id.
    // section: the BMS group (0x436c74 visits 1 every 32 ticks, 2 every 64);
    // slot: the record's index in its group.
    struct Ambient { int object; int wait; int section; int slot; };
    // The C runtime's rand (Watcom 0x49153c, never seeded: 1), which DF.EXE
    // draws ambience waits from (shared there with ~150 other callers).
    uint32_t crtSeed_ = 1;
    int crtRand() { crtSeed_ = crtSeed_ * 0x41C64E6Du + 0x3039u; return int((crtSeed_ >> 16) & 0x7FFF); }
    std::vector<Ambient> ambients_;
    struct ActiveDialog { const std::vector<DialogPart>* parts; int next, wait; };
    std::vector<ActiveDialog> activeDialogs_;
    void tickDialogs();
    float scriptClock_ = 0;
    uint32_t ticks_ = 0;                  // 60 Hz simulation ticks (DF.EXE 0xc70acc)
    // Effect objects (DF.EXE 0x46a214 / 0x436eec / 0x480c70): an item's model,
    // billboarded toward the camera, animated frame by frame, removed after
    // frames x ticks-per-frame.
    struct Effect {
        const GpuModel* model; Vec3 pos; float scale; int age, life, ticksPerFrame, frames;
        // Smoke puffs ("smok", 0x480bfc): drift and rise speeds in m/tick,
        // roll in turns.
        bool smoke = false; float drift = 0, rise = 0, roll = 0;
        // DF2's landed canopy (`para` 0x42c570): heading, tipping 0..90°.
        bool canopy = false; float heading = 0, pitch = 0;
        // DF2's dust clouds (move `smk2` 0x42aaa0, item 107051 SMKT_H): 16.16
        // velocity per tick (DF2 axes x east, y north, z up), size +0x84,
        // growth shift +0xb4, cap +0xb8, alpha +0xac and its step +0xb0, roll
        // +0x38 and spin +0xa8, colour +0x80.
        bool smk2 = false;
        // DF2's bld2 fireballs: a dust cloud with move 0x42abc0 instead
        // (0x402f75), drawn with polygon flag 0x800 (0x41f7ed).
        bool fireball = false;
        int32_t vel[3] = {0, 0, 0}, size = 0, shift = 0, maxSize = 0, alpha = 0, dAlpha = 0, rot = 0, spin = 0;
        uint8_t tint[3] = {255, 255, 255};
    };
    // DF2.EXE 0x414010 (kind 0, t 0) / 0x414220 (dust, moving outward along
    // (dx, dz) at 1/16 m a tick) and 0x413e80 (smoke, shade t).
    Effect* spawnSmk2(Vec3 at, bool dust, float dx, float dz, int t);   // nullptr when none was made
    std::vector<Effect> effects_;
    // Smoke generators (ITEMS.DEF "Smoke Generator", sgen 0x480b88): a puff
    // every 16..23 ticks, 100 puffs.
    struct SmokeGen { Vec3 pos; int wait = 0, puffs = 0; };
    // Sound: live on the audio device, or mixed in fixed steps into the
    // recording (--record).
    Audio audio_;
    SoundSystem sound_;
    AiSystem ai_;
    VehicleSystem vehicles_;
    // ScriptHost: the AI's conditions and actions.
    bool teamSpotted(int team, int targetTeam) const override { return ai_.spotted(team, targetTeam); }
    bool teamEngaging(int team) const override { return ai_.engaging(team); }
    bool teamAlerted(int team) const override { return ai_.alerted(team); }
    bool reachedWaypoint(int team, int path, int point) const override { return ai_.reached(team, path, point); }
    void followPath(int team, int path) override { ai_.followPath(world_, team, path); vehicles_.followPath(world_, team, path); }
    void aiProperty(int team, int subtype, int value) override { ai_.property(world_, team, subtype, value); }
    void teamSpeed(int team, int kmh) override { ai_.teamSpeed(team, kmh); }
    void setTeamSide(int team, int side) override {
        if (team == 0) return;   // 0x4133c5: team 0 is left alone
        for (auto& o : world_.objects)
            if (o.team == team) o.side = side;
    }
    void moveTeam(int team, int newTeam) override {
        if (team == 0) return;
        for (auto& o : world_.objects)
            if (o.team == team) o.team = newTeam;
    }
    void teleportTeam(int team, int value) override {
        // DF2.EXE 0x416960: the target's position and angles to every person
        // of the team. INFERRED: a target 100 m or more above the ground puts
        // them on a chute (DF2.EXE 0x4169e2 copies the target's own flag
        // 0x20, which nothing sets, so its paradrops would kill their teams;
        // INFERRED from the missions: a target 100 m or more up is a chute
        // drop, as the soldiers' start rule 0x45bb10).
        for (const auto& mo : mission_.objects) {
            if (mo.type != 6088 || (mo.raw[18] & 0xFF) != team) continue;
            if (reinterpret_cast<const uint8_t*>(mo.raw)[0x4A] != value) continue;
            const float ext = terrain_.mapExtent();
            const Vec3 at{std::fmod(std::fmod(mo.x, ext) + ext, ext), 0, std::fmod(std::fmod(-mo.y, ext) + ext, ext)};
            for (auto& o : world_.objects) {
                if (!o.person || o.team != team || !o.alive()) continue;
                o.pos.x = at.x;
                o.pos.z = at.z;
                o.heading = mo.heading * kPi / 180.f;
                o.base = terrain_.heightAt(at.x, at.z) + mo.z;
                if (mo.z >= 100.f) o.chute = true;   // ORed in (0x4169e8), never cleared; vz is kept
                o.pos.y = o.base + o.hip;
            }
            break;
        }
    }
    void playSound(int id, Vec3 at) { sound_.play(id, at); }
    FILE* audioOut_ = nullptr;
    double audioClock_ = 0;
    std::vector<SmokeGen> smokeGens_;
    int playerHp_ = 4;                    // character word +0x74
    // The player's item (0x469ae0): male/female x normal/snow x day/night.
    int playerItem_ = kItemPlayerSoldier;
    int playerModelItem_ = 0;             // DF2: the gear's model item (0 = the player item's)
    // Dead: the body slides with the death move's root motion from where it
    // fell (0x436158), at the yaw of death; the orbit camera's pivot and base
    // yaw follow it smoothed (0x481184: 1/16 and 1/32 per tick).
    Vec3 deadShift_{0, 0, 0}, camPivot_{0, 0, 0}, camLag_{0, 0, 0};
    float deadYaw_ = 0, camYaw_ = 0;      // OpenDF heading / DF yaw (anticlockwise), radians
    Vec3 deadBodyOrigin() const;
    int damageFlash_ = 0;                 // [0x933554]: red flash strength 0..255
    // Delta Force 2: air used under water (player slot +0xcc, DF2.EXE 0x4587f0).
    int air_ = 0;
    void tickAir();
    void hurtPlayer(int amount, int deathType = 1, Vec3 from = {0, 0, 0}, int part = 0);
    // Animation events (0x4360b0): footsteps 0x40-0x43 (0x46a680), voices
    // 0x12-0x16 (0x473f70, slots 7-11) of a body at `origin`.
    void animEvent(int event, Vec3 origin, int phraseSet, int team = 0);
    std::string playerMove_;
    Chr playerChr_;
    Animator playerBody_;
    // The camera: the player's eye, or once dead the orbit around the body
    // (0x404104 / 0x481224).
    Vec3 viewEye() const;
    static std::vector<Mat4> personPose(const GpuModel& m, int lod, const Chr& chr, const std::vector<std::array<float, 3>>& turns,
                                        float aimPitch = 0);
    Vec3 viewDir() const;
    bool playerDead() const { return playerHp_ <= 0; }
    void explode(Vec3 at, int surface, bool heavy = false, float radius = 10.f);
    void detonate(Vec3 at, int shrapnel, float heading);   // surface: 0 terrain, 1 object, 2 water
    void spawnPuff(Vec3 at);
    void tickEffects();
    // Delta Force 2's rain and snow (BMS flag 0x78 bit 3, 0x46ba90).
    Weather weather_;
    void drawWeather(const Mat4& view, const Mat4& proj, Vec3 eye);
    std::mt19937 effectRng_{1998};
    void spawnImpact(const Impact& hit);
    void spawnEffect(int slot, Vec3 pos);
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
    // Delta Force 2's HUD (docs/OTHER-GAMES.md): two bitmap strips
    // (DHT%iG / DHB%iG), amber icons drawn additively and positions per
    // display mode from HUD.DEF (DF2.EXE 0x443bd0, 0x444150).
    struct Df2Hud {
        bool on = false;
        HudDef def;
        HudImage top, bottom, weap, pos, itm, rad;
    } df2Hud_;
    void drawDf2Strips(const Screen& s);
    // DF2.EXE 0x45c160: a panel darkened 50 % with a one-pixel orange outline,
    // in the virtual 1024x768 space.
    void df2Panel(const Screen& s, float vx0, float vy0, float vx1, float vy1);
    // A DF2GAME.BIN string (section, key), or `fallback`.
    std::string df2Text(const char* section, const char* key, const char* fallback) const {
        const std::string* t = gameTable_.find(section, key);
        return t && !t->empty() ? *t : std::string(fallback);
    }
    bool nvg_ = false;   // DF2 night vision ([0x765740], forced on for night missions)
    // Delta Force 2's command map (DF2.EXE 0x40b370): a 640x480 mouse UI.
    // Its one selection ([0xbcb424], set by select 0x40bb10 from a Map pick,
    // a Players row or a Team row): 0 none, 1 the player, 2 a world object
    // (soldiers), 3 a mission record with a radar shape (markers, buildings,
    // vehicles), 4 a player waypoint (pool slot).
    struct Df2Sel {
        int kind = 0, index = -1;
        bool operator==(const Df2Sel& o) const { return kind == o.kind && (kind == 0 || index == o.index); }
        bool operator!=(const Df2Sel& o) const { return !(*this == o); }
    };
    // Player waypoints (DF2.EXE 0x445e70): item 6089 objects in a 128-slot
    // pool (0xd34dc0), cleared at mission load (0x403520). The owner
    // (+0xf0) is always the player here: received waypoints are multiplayer.
    struct Df2Waypoint { bool used = false; double x = 0, y = 0; float z = 0; std::string name; };
    std::vector<Df2Waypoint> df2Wps_ = std::vector<Df2Waypoint>(128);
    // [0x765754], the HUD waypoint, when it is not the mission route's
    // current point (kind 0: route_[routeIndex_]); "Set as my Waypoint".
    Df2Sel df2HudWp_;
    bool df2Elevation_ = false;   // [0x765600]: the radars' elevation picture (D, F9 / F10)
    bool df2KeyFlag_ = false;     // [0x7655fc]: set by F9 / F10, read only by Escape (0x41b59d)
    Df2Loadout df2Loadout_;       // the player's weapon types (controller +0xb6..+0xba)
    struct Df2Map {
        bool loaded = false;
        HudImage tile, tile2, edge, corner, end1, midl, end2, iconWay, iconDepth, iconCentre, iconContour, cursor, cross, hand, wcur;
        HudImage bt09, bt10, weap;   // arrows (DF2_BT09 / BT10), WEAP640 for the Weapons tab
        HudImage pic;                // the elevation / contour picture
        SheetFont big;
        int tab = 0;
        bool entered = false;        // the tab's enter hook has run
        bool toggles[6] = {true, true, true, true, true, true};   // grid, names, people, waypoints, vehicles, buildings
        float panX = 0, panZ = 0;     // metres from the player
        int mouseX = 0, mouseY = 0;   // in the 640x480 screen
        int lastX = 0, lastY = 0;     // the mouse the frame before (waypoint drag)
        bool panning = false, leftHeld = false;
        Df2Sel sel;                   // [0xbcb424]
        Df2Sel drag;                  // 0xbd055c: the waypoint being dragged
        bool wTool = false;           // 0xbd058c (BSS: starts 0, never reset)
        bool wButton = false;         // the W button's state (0 whenever the Map tab is entered)
        bool dButton = false;         // the D button's state (= [0x765600] when the Map tab is entered)
        bool contours = false;        // 0xbd0588 (BSS: starts 0, never reset)
        int scrollPlayers = 0, scrollTeam = 0;   // [0xbd0594], [0xbd0598]
        int barWidget = 0, barAnchorY = 0, barCount = 0;
        std::map<int, std::string> planText;
        std::string teamName;                  // the panel's team-name buffer 0x4ca878   // teammates' controller +0x14 (0x403380), by object   // 0x40c040's widget, [0xbcc488], [0xbcc490]
        // A list's scroll-bar record (0x40c3f0) and its thumb (0x40c710) in
        // 640 px: track x0..x1, ty0 (+0x14 = y0 + 16 - 1), ty1 (+0x1c = y1 - 16).
        struct Bar { int x0, x1, ty0, ty1, top, bottom; };
        static Bar bar(float vx, float vy0, float vy1, int pos, int page) {
            auto V = [](float v) { return int(std::floor(v * 0.625f)); };
            Bar b{};
            b.x0 = V(vx);
            b.x1 = b.x0 + 16;
            b.ty0 = V(vy0) + 16 - 1;
            b.ty1 = V(vy1) - 16;
            const int L = b.ty1 - (b.ty0 + 3);
            b.top = pos * L / 64 + b.ty0 + 3;
            b.bottom = std::min((pos + page) * L / 64 + b.ty0 + 3, b.ty1);
            return b;
        }
        int current[5] = {}, request[5] = {};    // Weapons tab: 0xbec6c0, 0xbe72a0
        int wbtnY[5] = {252, 348, 444, 540, 636};  // its < > buttons' y (virtual)
        // Blips drawn this frame (0x4226c0 records them; 0x422640 picks).
        struct Pick { float x, y; Df2Sel sel; };
        std::vector<Pick> picks;
        // The text prompt (0x41c650 / 0x4422b0): "Type in a name", 16 chars.
        bool prompt = false;
        std::string promptText;
        // Testing: OPENDF_CMAPTEST, "frame op args; ..." (see drawDf2CommandMap).
        struct TestCmd { int frame; char op; int x, y; std::string text; };
        std::vector<TestCmd> test;
        int frame = 0;
        bool testInit = false;
    } df2Map_;
    void drawDf2CommandMap(const Screen& s);
    void df2MapClick(const Screen& s, int x, int y, bool down, int button);
    void df2MapEnter(int tab);
    void df2MapFrame();
    void df2MapKey(const SDL_KeyboardEvent& k);
    void df2Select(Df2Sel s) { df2Map_.sel = s; }
    bool df2SelValid(Df2Sel s) const;
    bool df2SelPos(Df2Sel s, double& x, double& y, float* z = nullptr) const;
    std::string df2SelName(Df2Sel s) const;
    int df2SelItem(Df2Sel s) const;
    std::string df2MissionName(size_t index) const;
    bool df2SelOnPlayerTeam(Df2Sel s) const;
    std::vector<std::pair<std::string, int>> df2ActionButtons() const;
    void df2Action(int code);
    void df2SetWaypoint(Df2Sel s);
    void df2NextWaypoint();
    struct Df2WpView { bool ok = false; double x = 0, y = 0; float elev = 0; std::string name; int listIndex = -1; };
    Df2WpView df2CurrentWaypoint() const;
    bool df2HudWpIs(Df2Sel s) const;
    void df2Picture(HudImage& img, int w, int h, double px, double py, double step, bool elevation, bool contours);
    int df2TypeCategory(int type) const;
    std::string df2TypeName(int type) const;
    void df2RequestWeapons();
    // The 640x480 screen inside the window (aspect kept, centred).
    float df2MapK(const Screen& s) const { return std::min(s.w / 640.f, s.h / 480.f); }
    void drawDf2Bottom(const Screen& s);
    void drawDf2Top(const Screen& s);
    // A HUD.DEF point in this window: x left- or right-anchored about the
    // strip's seam, y from the top (top strip) or the bottom (bottom strip).
    float df2X(const Screen& s, int x, bool bottom) const;
    float df2Y(const Screen& s, int y, bool bottom) const;
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
    // Binoculars ([0x93367c], 0x44b870): a 22-degree view with a fixed
    // random aim offset chosen as they come up (yaw, pitch radians).
    bool binoculars_ = false;
    float binoYaw_ = 0, binoPitch_ = 0;
    // DF.EXE 0x44b870 / DF2.EXE 0x4481e0 (the same code): sin / cos of a
    // random angle (0x435db4 / 0x41ac40, << 16) through the tables
    // (0x435c40 / 0x41ab10), times 8: up to 2^25 binary angle (2.8125
    // degrees), the first added to the yaw, the second to the pitch.
    void pickBinocularOffset() {
        const float r = std::uniform_real_distribution<float>(0.f, 2 * kPi)(effectRng_);
        binoYaw_ = std::sin(r) * 2.8125f * kPi / 180.f;
        binoPitch_ = std::cos(r) * 2.8125f * kPi / 180.f;
    }
    bool paused_ = false;                 // [0xc70ac4] (0x42df53)
    bool oldMessages_ = false;            // [0x66f974]
    bool friendlyTags_ = false;           // [0x66f978]
    int letterbox_ = 1;                   // lbsize (0x404650): 1 or 2 eighths of the height per band
    float viewFovY_ = 0;                  // vertical field of view of this frame's 3D view
    std::deque<std::pair<std::string, Color>> history_;   // the 16 latest messages and their colours (ring 0x68a280, +0x78)
    void drawOldMessages(const Screen& s);
    // Delta Force 2's top strip (DF2.EXE 0x442170): two histories of 40
    // {text, colour}, newest first; sys at sys_message, chat at chat_message.
    struct Df2Line { std::string text; Color colour; };
    std::deque<Df2Line> df2Sys_, df2Chat_;
    // df1Colour: DF.EXE's colour argument to 0x42f400 (see kHudMessage).
    void pushMessage(const std::string& text, Color df2Colour = {1, 1, 1, 1}, bool df2Chat = false, Color df1Colour = kHudMessage) {
        if (df2Hud_.on) {
            auto& h = df2Chat ? df2Chat_ : df2Sys_;
            h.push_front({text.substr(0, 119), df2Colour});
            while (h.size() > 40) h.pop_back();
        }
        messages_.push_back({text, 0, df1Colour});
        history_.push_back({text, df1Colour});
        while (history_.size() > 16) history_.pop_front();
    }
    void drawFriendlyTags(const Screen& s, const Mat4& viewProj);
    void drawPaused(const Screen& s);
    void drawBinoculars(const Screen& s);
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
    {
        // 0x469ae0: header +0x74 == 2 the snow soldiers, +0x7A & 0x10 night.
        const bool snow = mission_.variantWord == 2, night = (mission_.lightFlags & 0x10) != 0;
        static const int kItems[2][2][2] = {{{5003, 5004}, {5044, 5045}}, {{5010, 5011}, {5046, 5047}}};
        playerItem_ = 100000 + kItems[snow][night][o_.female ? 1 : 0];
        playerModelItem_ = 0;
        if (mission_.version >= 0x0F) {
            // Delta Force 2 (0x4128d0): by id, the same snow/night tests.
            static const int kDf2[2][2][2] = {{{5300, 5302}, {5306, 5307}}, {{5304, 5305}, {5308, 5309}}};
            playerItem_ = 100000 + kDf2[snow][night][o_.female ? 1 : 0];
            // Gear only changes the drawn model (0x415410): ghillie (32)
            // 105310/105311, LAR V (33) 105312/105313; it beats snow/night.
            if (o_.df2Gear == 32) playerModelItem_ = o_.female ? 105311 : 105310;
            else if (o_.df2Gear == 33) playerModelItem_ = o_.female ? 105313 : 105312;
            if (!items_.byId(playerModelItem_)) playerModelItem_ = 0;
        }
        if (!items_.byId(playerItem_)) playerItem_ = kItemPlayerSoldier;
    }
    std::string trn = mission_.terrain;
    if (auto dot = trn.rfind('.'); dot != std::string::npos) trn.resize(dot);
    terrain_.waterOverride = mission_.waterOverride;
    terrain_.skyOverride = mission_.sky;
    terrain_.fogLevelOverride = mission_.fogLevel;
    terrain_.fogColorOverride = mission_.fogColor;
    if (!terrain_.load(vfs_, trn, &err)) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), err.c_str());
        return false;
    }
    current_ = index;
    // Testing: OPENDF_HEIGHTSCALE overrides metres per elevation step.
    if (const char* hs = std::getenv("OPENDF_HEIGHTSCALE")) terrain_.scale.heightScale = float(std::atof(hs));
    world_.terrain = &terrain_;
    world_.df2Rules = mission_.version >= 0x0F;
    world_.fogLevel = terrain_.fogLevel();
    models_.setVariant(mission_.colorVariant);
    world_.build(vfs_, models_, items_, mission_);
    ai_.df2 = mission_.version >= 0x0F;
    ai_.night = (mission_.lightFlags & 0x10) != 0;
    ai_.playerGear = o_.df2Gear;
    // Squad members off, before the AI lists: DF.EXE 0x402cc5 (attributes
    // 0x200) -> 0x402a38, DF2.EXE 0x45bf44 (df2.cfg attributes 0x20, the
    // Options3 SQUAD MEMBERS row) -> 0x45c460: every person of the four
    // "Squad Member" items (item +0x20 = 0x138a, 0x13ae, 0x13b8, 0x13b9:
    // 105002, 105038, 105048, 105049) is deleted (0x469fd8 / 0x4160d0 zero the
    // object and its AI block). The same ids in both games.
    const bool df2Mission = mission_.version >= 0x0F;
    if (df2Mission ? (o_.df2Attributes & 0x20) : (o_.attributes & 0x200))
        for (auto& o : world_.objects) {
            const int id = o.item ? o.item->id - 100000 : 0;
            if (id == 5002 || id == 5038 || id == 5048 || id == 5049) { o.removed = true; o.boxes.clear(); }
        }
    ai_.init(world_, mission_);
    // Enemy AI hard: DF.EXE attributes 0x80 ([0x66fa88]), DF2.EXE df2.cfg
    // attributes 0x10 ([0x653000], read by the aim 0x415f16).
    ai_.hard = df2Mission ? (o_.df2Attributes & 0x10) != 0 : (o_.attributes & 0x80) != 0;
    vehicles_.init(world_, mission_);
    lights_.clear();
    if (world_.df2Rules)
        for (size_t i = 0; i < world_.objects.size(); ++i) {
            const WorldObject& o = world_.objects[i];
            if (!o.item || o.item->aiFunction != "slgt") continue;
            Searchlight l;
            l.object = int(i);
            if (o.missionIndex >= 0) {
                const int32_t v = mission_.objects[size_t(o.missionIndex)].raw[0x24 / 4];
                l.perception = (v > 0 ? float(v) : 100.f) / 100.f;
                l.alert = reinterpret_cast<const uint8_t*>(mission_.objects[size_t(o.missionIndex)].raw)[0x42];
            }
            if ((mission_.lightFlags & 0x10) && l.perception > 0.3f) l.perception -= 0.15f;
            lights_.push_back(l);
        }
    vehicles_.detonate = [this](Vec3 at, int n, float h) { detonate(at, n, h); };
    vehicles_.smoke = [this](Vec3 at) { spawnPuff(at); };
    vehicles_.bigBlast = [this](Vec3 at) { bigBlast(at); };
    vehicles_.playSound = [this](int id, Vec3 at) { playSound(id, at); };
    // AI rounds leave from the weapon (0x46ad9c; DF2.EXE 0x414f20 the same):
    // the body (no pitch) x the posed right hand (mesh 7) x the held-weapon
    // turn x the mode's hardpoint (+0x90; DF2 +0x30) of its third-person
    // item's model (DF2: the mode's +0x08 item).
    ai_.muzzle = [this](int object, int mode, Vec3& at) {
        const WorldObject& o = world_.objects[size_t(object)];
        if (!o.chr || !o.model || o.anim.angles().empty()) return false;
        const int item = arsenal_.heldItemAt(mode);
        const ItemDef* wi = item > 0 ? items_.byId(100000 + item) : nullptr;
        const GpuModel* wm = wi && !wi->graphic.empty() ? models_.get(vfs_, wi->graphic) : nullptr;
        const int hp = arsenal_.modeAt(mode).muzzleVertex;
        if (!wm || hp < 0 || hp >= int(wm->points.size()) || o.model->lods.empty() || o.model->lods[0].meshPivot.size() <= 7) return false;
        const auto pose = personPose(*o.model, 0, *o.chr, o.anim.angles(), o.aimPitch);
        if (pose.size() <= 7) return false;
        const auto& pv = o.model->lods[0].meshPivot[7];
        const Mat4 m = o.matrix(o.pos) * pose[7] * translate({pv[0], pv[1], pv[2]}) * heldWeaponOffset();
        const auto& q = wm->points[size_t(hp)];
        at = {m.at(0, 0) * q[0] + m.at(0, 1) * q[1] + m.at(0, 2) * q[2] + m.at(0, 3),
              m.at(1, 0) * q[0] + m.at(1, 1) * q[1] + m.at(1, 2) * q[2] + m.at(1, 3),
              m.at(2, 0) * q[0] + m.at(2, 1) * q[1] + m.at(2, 2) * q[2] + m.at(2, 3)};
        return true;
    };

    player_ = Player{};
    player_.moves = &playerMoves_;
    std::memcpy(player_.eyeHeights, eyeHeightTable_, sizeof eyeHeightTable_);
    player_.floatOrigin = floatOrigin_;
    player_.swimOrigin = swimOrigin_;
    player_.feet = {512, 0, 512};
    float startLift = 0;
    for (const auto& obj : mission_.objects) {
        const ItemDef* item = items_.byMissionType(obj.type);
        if (item && item->id == kItemPlayerStart) {
            const float ext = terrain_.mapExtent();
            player_.feet = {obj.x, 0, -obj.y};
            player_.wrap(ext);
            player_.yaw = obj.heading * kPi / 180.f;
            startLift = obj.z;
            break;
        }
    }
    player_.feet.y = world_.groundHeight(player_.feet, 0.45f);
    // Delta Force 2: the player starts at ground + the marker's z (0x457fc0,
    // 0x458133; no clamp) and drops from there. A start 100 m or more up is
    // a parachute drop 1 m higher, as for soldiers (0x45bb10). INFERRED for
    // the player: in DF2.EXE (1999-10-19) only the soldiers' start pass sets
    // the chute flag and markers never carry it (df2_parachute.md), so the
    // player would free-fall and die at the ten missions that start 100-160 m
    // up (D03M04, D03M11, D03M15, D05M03, D05M12, D06M03, D06M05, D06M06,
    // D06M14, D06M16); the start heights sit on the 100 m test, the D06M06
    // briefing says the player will paradrop, and the Land Warrior mission
    // editor manual gives the rule: a "Start, Player" Z of 100.00..300.00
    // parachutes the player in (also AI units).
    player_.chute = mission_.version >= 0x0F && startLift >= 100.f;
    if (player_.chute) player_.feet.y += startLift + 1.f;
    else if (mission_.version >= 0x0F && startLift > 0) player_.feet.y = terrain_.heightAt(player_.feet.x, player_.feet.z) + startLift;
    {
        Loadout lo;
        if (const int r = loadoutRow(o_.sidearm); r >= 0) lo.sidearm = r;
        if (const int r = loadoutRow(o_.primary); r >= 0) lo.primary = r;
        if (const int r = loadoutRow(o_.secondary); r >= 0) lo.kit = r;
        bool df2Weapons = false;
        if (mission_.version >= 0x0F) {
            // Delta Force 2: its own tables from DF2.EXE, its loadout by weapon
            // type (WeaponNames2); --loadout PRIMARY,SECONDARY,SIDEARM.
            Df2Loadout d2;
            static const std::pair<const char*, int> kTypes[] = {
                {"M4", 9}, {"MK", 10}, {"MASTERKEY", 10}, {"SAW", 11}, {"M249", 11}, {"BARRETT", 12}, {"MP5", 15},
                {"M40", 23}, {"UAR", 29}, {"AMMO", 1}, {"LAW", 20}, {"SATCHEL", 21}, {"CLAYMORE", 24},
                {"VIDEO", 36}, {".45", 6}, {".45S", 5}, {"HS22", 5}, {"P11", 28}};
            auto typeOf = [&](const std::string& n, int def) {
                const std::string u = toUpper(n);
                for (const auto& t : kTypes)
                    if (u == t.first) return t.second;
                return def;
            };
            d2.primary = o_.df2Primary >= 0 ? o_.df2Primary : typeOf(o_.primary, 9);
            d2.secondary = o_.df2Secondary >= 0 ? o_.df2Secondary : typeOf(o_.secondary, 1);
            d2.sidearm = o_.df2Sidearm >= 0 ? o_.df2Sidearm : typeOf(o_.sidearm, 6);
            d2.gear = o_.df2Gear;
            d2.grenade = o_.df2Grenade;
            std::vector<std::string> hudGuns;
            for (int m = 0; m < 57; ++m) {
                char key[16];
                std::snprintf(key, sizeof key, "HG%03d", m);
                const std::string* t = gameTable_.find("HudGuns", key);
                hudGuns.push_back(t ? *t : std::string());
            }
            df2Weapons = arsenal_.initDf2(hudGuns, df2Exe_, &items_, d2);
            df2Loadout_ = d2;
            if (const ItemDef* soldier = items_.byId(playerItem_)) arsenal_.playerArmourClass = soldier->damageClass;
            if (!df2Weapons) std::fprintf(stderr, "DF2.EXE weapon tables not usable; using Delta Force's\n");
            arsenal_.setWind(float(mission_.windKph), float(mission_.windDeg));
        }
        weather_.start(mission_.version >= 0x0F ? mission_.weather : 0);
        if (!df2Weapons) arsenal_.init(gameText_, &exe_, &items_, lo);
        arsenal_.modelPoints = [this](const std::string& name) -> const std::vector<std::array<float, 3>>* {
            const GpuModel* m = name.empty() ? nullptr : models_.get(vfs_, name);
            return m ? &m->points : nullptr;
        };
    }
    // Hit points: ITEMS.DEF "player01" `hp` (4) (DF.EXE 0x469c76).
    {
        const ItemDef* soldier = items_.byId(mission_.version >= 0x0F ? playerItem_ : kItemPlayerSoldier);
        playerHp_ = soldier && soldier->hp > 0 ? soldier->hp : 4;
        // DF2.EXE 0x440651: the chute glides with the item's PFoil (0x400);
        // every player item (player01..) has it in DF2's ITEMS.DEF.
        player_.pfoil = !soldier || (soldier->attrib & 0x400);
        // Player Health: Easy (0x469c02); DF2 has no such option (health is
        // the ITEMS.DEF hp as it is, DF2.EXE 0x412a0a).
        if ((o_.attributes & 0x100) && mission_.version < 0x0F) playerHp_ *= 4;
        playerBody_ = Animator{};
        playerMove_.clear();
        damageFlash_ = 0;
        air_ = 0;
        // DF2.EXE 0x44781e: night missions (header flag 0x100000) start with
        // the night vision on.
        nvg_ = mission_.version >= 0x0F && (mission_.lightFlags & 0x10);
    }
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
    // Powerups (attrib 0x20: ammo and medical crates) are removed at the start
    // in single player (0x402aa4, unless option bit 8).
    if (!(o_.attributes & 0x800))
        for (auto& o : world_.objects)
            if (o.item && (o.item->attrib & 0x20)) o.removed = true;
    carried_ = -1;
    briefingShown_ = helpShown_ = false;
    charges_.clear();
    strikes_.clear();
    collapses_.clear();
    fireMissions_ = mission_.fireMissions;
    laserWindow_ = 0;
    {
        // Briefing (0x4813c4): campaign section "Briefing<n>", key "Mission<n>a";
        // (expanded below)
        int cn = 0, mn = 0;
        std::sscanf(name.c_str(), "C%dM%d", &cn, &mn);
        char sec[32], key[32];
        std::snprintf(sec, sizeof sec, "Briefing%d", mn);
        std::snprintf(key, sizeof key, "Mission%da", mn);
        const std::string* t = campText_.find(sec, key);
        briefing_ = t ? *t : "UNABLE TO FIND MISSION BRIEFING";
        // {ADD}<key> appends another key; {CV:n}<key> / {CV:!n}<key> append it
        // only if campaign flag n is set / clear (0x412620 / 0x4125c8).
        for (int guard = 0; guard < 64; ++guard) {
            size_t a = briefing_.find("{ADD}"), c = briefing_.find("{CV:");
            if (a == std::string::npos && c == std::string::npos) break;
            bool include = true;
            size_t tagEnd;
            if (c != std::string::npos && (a == std::string::npos || c < a)) {
                a = c;
                tagEnd = briefing_.find('}', c);
                if (tagEnd == std::string::npos) break;
                std::string cond = briefing_.substr(c + 4, tagEnd - c - 4);
                const bool neg = !cond.empty() && cond[0] == '!';
                const int flag = std::atoi(cond.c_str() + (neg ? 1 : 0));
                const bool set = flag >= 0 && flag < 32 && (campaignFlags() >> flag & 1);
                include = neg ? !set : set;
                ++tagEnd;
            } else {
                tagEnd = a + 5;
            }
            size_t e = tagEnd;
            while (e < briefing_.size() && !std::isspace(static_cast<unsigned char>(briefing_[e]))) ++e;
            const std::string* more = include ? campText_.find(sec, briefing_.substr(tagEnd, e - tagEnd)) : nullptr;
            briefing_.replace(a, e - a, more ? *more : "");
        }
        briefStart_ = briefLast_ = 1;
        briefPage_ = 0;
    }
    {
        // Radio dialogs: the mission header names the set ("dfdlg02").
        std::vector<uint8_t> raw;
        const std::string set = toUpper(mission_.dialogue);
        dialogs_ = DialogIndex{};
        dialogText_.clear();
        activeDialogs_.clear();
        sound_.stopAll();
        ambients_.clear();
        for (size_t i = 0; i < world_.objects.size(); ++i) {
            const auto& o = world_.objects[i];
            if (!o.item || o.item->interruptSound <= 0) continue;
            const std::string& f = o.item->aiFunction;
            // Delta Force 2 adds snd3 (random ambient markers, 0x402730) and
            // bld2 (collapsing buildings, their sound as dec0's, 0x4028d0).
            if ((f == "dec0" || f == "tree" || f == "palm" || f == "snd0" || f == "snd1" || f == "snd2" || f == "snd3" || f == "bld2") &&
                o.missionIndex >= 0) {
                // First wait (0x440071): a counter mod 60, the counter going
                // up 11 per object loaded, in file order.
                const MissionObject& mo = mission_.objects[size_t(o.missionIndex)];
                ambients_.push_back({int(i), (11 * o.missionIndex) % 60, mo.group, mo.indexInGroup});
            }
        }
        if (!set.empty()) {
            if (vfs_.read(set + ".DBF", raw)) dialogs_.parse(raw);
            if (vfs_.read(set + ".BIN", raw)) parseRtxt(raw, dialogText_);
            sound_.loadDialogBank(vfs_, set + ".PWF");
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
    // DF2: the waypoint pool is cleared at mission load (0x403520); the
    // HUD waypoint and the elevation view at mission start (0x447810/0x447816).
    for (auto& w : df2Wps_) w = Df2Waypoint{};
    df2HudWp_ = Df2Sel{};
    df2Elevation_ = df2KeyFlag_ = false;
    df2Map_.sel = df2Map_.drag = Df2Sel{};
    df2Map_.prompt = false;
    df2Map_.entered = false;
    if (exe_.loaded() && !arsenal_.fromExe())
        std::fprintf(stderr, "DF.EXE is not the expected version; using built-in weapon values\n");
    else if (arsenal_.fromExe()) std::printf(arsenal_.df2() ? "weapon tables read from DF2.EXE\n" : "weapon tables read from DF.EXE\n");
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
        // DF2 (0x403ad6 / 0x403b87): a zero radius is 0.5 m for item 6005,
        // 1 m for 6006 (object +0x88, 16.16).
        if (mission_.version >= 0x0F && o->raw[7] == 0) rp.radius = o->type == 6006 ? 1.f : o->type == 6005 ? 0.5f : 0.f;
        char key[24];
        const std::string* n = nullptr;
        if (mission_.version >= 0x0F) {
            // Delta Force 2: each waypoint names itself, record +0x58 ->
            // STRWPNAME%03i in DF2WPN.BIN "WPNames" (DF2.EXE 0x403afe).
            std::snprintf(key, sizeof key, "STRWPNAME%03d", o->raw[0x58 / 4]);
            n = wpnText_.find("WPNames", key);
        } else {
            std::snprintf(key, sizeof key, "WPN%03d", mission_.routeNameIds[i]);
            n = campText_.find("Waypoint Names", key);
        }
        if (n && !n->empty()) rp.name = *n;
        else rp.name = std::to_string(i);   // DF.EXE falls back to "%i"
        route_.push_back(std::move(rp));
    }
}

void Game::updateRoute() {
    // DF.EXE 0x449a39: arrived when within the radius on both axes and
    // max + min/2 (an octagonal distance) is under it; wraps to the start.
    if (route_.empty()) return;
    if (df2HudWp_.kind != 0) {
        // DF2 0x446230: while the route has entries the arrival test runs on
        // the HUD waypoint [0x765754] itself, whatever it is, with its radius
        // +0x88: item 6005 record +0x1C or 0.5 m, 6006 +0x1C or 1 m
        // (0x403ad6 / 0x403b87); a W-tool waypoint is made from a zeroed
        // template that never sets it (0x445e70) and a soldier's creator
        // leaves it 0 (INFERRED for soldiers: the other writers of +0x88 were
        // not enumerated), and with radius 0 nothing ever arrives (unsigned
        // compare 0x44628d). On arrival 0x446700(0): with more than one entry
        // the waypoint is looked up in the route; not found counts as index
        // 0, so the next is entry 1 (removed entries skipped, wrapping).
        if (df2HudWp_.kind == 4) return;
        double r = 0;
        if (df2HudWp_.kind == 3 && df2SelValid(df2HudWp_)) {
            const MissionObject& o = mission_.objects[size_t(df2HudWp_.index)];
            if (o.type == 6005) r = o.raw[7] ? double(o.raw[7]) : 0.5;
            if (o.type == 6006) r = o.raw[7] ? double(o.raw[7]) : 1.0;
        }
        const Df2WpView w = df2CurrentWaypoint();
        if (r <= 0 || !w.ok) return;
        const double ax = std::abs(player_.missionX() - w.x), ay = std::abs(player_.missionY() - w.y);
        if (ax > r && ay > r) return;   // 0x446273: both must exceed (the rest is the octagon test)
        if (std::max(ax, ay) + std::min(ax, ay) / 2 >= r) return;
        if (route_.size() <= 1) return;
        df2HudWp_ = Df2Sel{};
        routeIndex_ = 1;
        return;
    }
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
    if (df2Hud_.on) {
        // DF2.EXE 0x443bd0: strips copied as they are; the icons through
        // the amber ramp (their own palettes are dropped).
        std::snprintf(name, sizeof name, "DHT%dG.PCX", modeW);
        df2Hud_.top = hud_.loadImage(vfs_, name, false);
        std::snprintf(name, sizeof name, "DHB%dG.PCX", modeW);
        df2Hud_.bottom = hud_.loadImage(vfs_, name, false);
        std::snprintf(name, sizeof name, "WEAP%d.PCX", modeW);
        df2Hud_.weap = hud_.loadAmberImage(vfs_, name);
        std::snprintf(name, sizeof name, "POS%d.PCX", modeW);
        df2Hud_.pos = hud_.loadAmberImage(vfs_, name);
        std::snprintf(name, sizeof name, "ITM%d.PCX", modeW);
        df2Hud_.itm = hud_.loadAmberImage(vfs_, name);
        std::snprintf(name, sizeof name, "RAD%d.PCX", modeW);
        df2Hud_.rad = hud_.loadAmberImage(vfs_, name);
    }
    std::snprintf(name, sizeof name, "POS%d.PCX", modeW);
    stanceIcons_ = hud_.loadImage(vfs_, name);
    std::snprintf(name, sizeof name, "WEAP%d.PCX", modeW);
    weaponIcons_ = hud_.loadImage(vfs_, name);
    std::snprintf(name, sizeof name, "F_SML%d.PCX", std::min(modeW, 800));
    std::string err;
    if (!hud_.loadFont(vfs_, name, &err)) std::fprintf(stderr, "%s\n", err.c_str());
}

// Effect slots (DF.EXE table 0xab3400) -> ITEMS.DEF items.
void Game::spawnEffect(int slot, Vec3 pos) {
    static const int kItems[28] = {0, 107016, 107015, 107013, 107014, 107001, 107002, 107003, 107004,
                                   107005, 107006, 107007, 107008, 107009, 107010, 107011, 107012,
                                   // Delta Force 2 (table 0xbb8b20, built at 0x414500): 17 rain poink,
                                   // 18 snow, 19 gex5, 20 boomd, 21 parachut, 22 pararnd, 23-27.
                                   107017, 107018, 107030, 107023, 107037, 107038, 107044, 105311, 105310, 105313, 105312};
    const int count = mission_.version >= 0x0F ? 27 : 16;
    if (slot < 1 || slot > count) return;
    const ItemDef* item = items_.byId(kItems[slot]);
    if (!item || item->graphic.empty()) return;
    const GpuModel* m = models_.get(vfs_, item->graphic);
    if (!m || m->lods.empty()) return;
    const auto& l = m->lods[0];
    // Scale rand·2 + 0x8000 in 16.16: 0.5..1.5.
    std::uniform_real_distribution<float> u(0.5f, 1.5f);
    effects_.push_back({m, pos, u(effectRng_), 0, l.frames * l.ticksPerFrame, l.ticksPerFrame, l.frames});
    if (std::getenv("OPENDF_DEBUGHITS"))
        std::fprintf(stderr, "effect slot %d %s: %d frames x %d ticks at (%.2f %.2f %.2f) radius %.2f\n", slot, item->graphic.c_str(),
                     l.frames, l.ticksPerFrame, pos.x, pos.y, pos.z, m->radius);
    if (effects_.size() > 0x400) effects_.erase(effects_.begin());   // 0x400 slots
}

// Explosion (DF.EXE 0x44f776 terrain, 0x44f90a / 0x44fb78 object or person,
// 0x44fc20 water; barrels 0x46cb64): effect 3 "ground exp" (4 "water exp" in
// water), blast type 1 with radius 10 m, and on terrain a smoke generator.
// Sound 0x32 (0x35 in the water). The LAW out of the water is blast
// type 3 (`heavy`: objects lose 128 - 16 d).
void Game::explode(Vec3 at, int surface, bool heavy, float radius) {
    spawnEffect(surface == 2 ? 4 : 3, at);
    // Delta Force 2 sends these through its speed-of-sound queue (0x45E910).
    sound_.playDelayed(surface == 2 ? SoundSystem::ExploWater : SoundSystem::ExploM240HE, at);
    for (auto& m : world_.blast(at, radius, heavy)) pushMessage(m);
    // The player is a character like the others (0x785b60): body origin
    // within radius + 1 m, terrain line of sight, 4 / 2 / 1 hit points.
    if (!playerDead()) {
        const Vec3 p = world_.nearest(player_.origin(), at);
        const Vec3 v = p - at;
        const float d = std::sqrt(dot(v, v));
        RayHit h;
        const Vec3 target = at + Vec3{0, 0.5f, 0};
        const bool blocked = world_.raycast(p, target, h, false) && world_.raycast(p + Vec3{0, 0.5f, 0}, target, h, false);
        // Blast deaths (0x44fda8): type 2 under 4 m; 4-8 m type 3 one time in
        // four, else 2; farther type 2.
        const int type = d < 4 ? 2 : d < 8 ? (std::uniform_int_distribution<int>(0, 3)(effectRng_) == 0 ? 3 : 2) : 2;
        if (mission_.version >= 0x0F) {
            // DF2.EXE 0x44d4a0: base 8 (32 for the 12 m charge), full under
            // 4 m, half under 8 m, a quarter beyond; blocked sight cancels
            // type 1 and doubles the distance for the others.
            const int base = radius > 11.f ? 32 : 8;
            const float dd = blocked ? 2 * d : d;
            if (d <= radius + 1.f && (!blocked || heavy))
                hurtPlayer(dd < 4 ? base : dd < 8 ? base / 2 : base / 4, type, at);
        } else if (d <= radius + 1.f && !blocked) hurtPlayer(d < 4 ? 4 : d < 8 ? 2 : 1, type, at);
    }
    if (surface == 0) smokeGens_.push_back({at, 0, 0});
}

// A person's pose (0x44ca34): per 3DI mesh (= .CHR part), the part's
// rotation from the animation about the mesh pivot; each pivot carried by
// its parent mesh's matrix. Model axes: OpenDF's (x forward, y up, z right);
// the .CHR rotations are in (x forward, y left, z up).
std::vector<Mat4> Game::personPose(const GpuModel& m, int lod, const Chr& chr, const std::vector<std::array<float, 3>>& turns,
                                   float aimPitch) {
    // The aim pitch (obj+0x34, within +-90 degrees) goes 1/4 into the torso
    // and 3/4 into the head (0x44ca34).
    const float aim = std::clamp(aimPitch, -kPi / 2, kPi / 2) / (2 * kPi);
    const auto& l = m.lods[size_t(std::clamp(lod, 0, int(m.lods.size()) - 1))];
    const size_t n = l.meshPivot.size();
    std::vector<Mat4> out(n);
    std::vector<Vec3> origin(n);
    std::vector<Mat4> rot(n);
    for (size_t k = 0; k < n; ++k) {
        Mat4 r;
        if (k < turns.size()) {
            std::array<float, 3> t = turns[k];
            if (k == 1) t[1] += aim * 0.25f;
            if (k == 2) t[1] += aim * 0.75f;
            const ChrMatrix c = chr.rotationFor(t);
            // C R C^-1 with C: (x, y, z) -> (x, z, -y).
            const int map[3] = {0, 2, 1};
            const float sign[3] = {1, 1, -1};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) r.at(i, j) = sign[i] * sign[j] * c.m[map[i]][map[j]];
        }
        rot[k] = r;
        const auto& pv = l.meshPivot[k];
        const Vec3 pivot{pv[0], pv[1], pv[2]};
        const int parent = k < l.meshParent.size() ? l.meshParent[k] : -1;
        if (k == 0 || parent < 0 || size_t(parent) >= k) origin[k] = pivot;
        else {
            const auto& pp = l.meshPivot[size_t(parent)];
            const Vec3 rel = pivot - Vec3{pp[0], pp[1], pp[2]};
            Mat4& pr = rot[size_t(parent)];
            origin[k] = origin[size_t(parent)] + Vec3{pr.at(0, 0) * rel.x + pr.at(0, 1) * rel.y + pr.at(0, 2) * rel.z,
                                                     pr.at(1, 0) * rel.x + pr.at(1, 1) * rel.y + pr.at(1, 2) * rel.z,
                                                     pr.at(2, 0) * rel.x + pr.at(2, 1) * rel.y + pr.at(2, 2) * rel.z};
        }
        out[k] = translate(origin[k]) * r * translate(pivot * -1.f);
    }
    return out;
}

void Game::animEvent(int event, Vec3 origin, int phraseSet, int team) {
    event &= 0xFF;
    // Delta Force 2 (DF2.EXE 0x43f480): 0x10 DEATH_WATER (400), 0x11 UW_IDLE
    // (804), 0x17 UW_SWIM (805).
    if (mission_.version >= 0x0F && (event == 0x10 || event == 0x11 || event == 0x17)) {
        playSound(event == 0x10 ? 400 : event == 0x11 ? 804 : 805, origin);
        return;
    }
    if (event >= 0x12 && event <= 0x16) {
        sound_.voice(phraseSet, 7 + event - 0x12, origin);
        return;
    }
    if (event < 0x40 || event > 0x43) return;
    // 0x46a680: the sound set by the event's low bit; in water (level above
    // origin - 0.8 m) a splash (effect 2) and 0x88 / 0x89 on the surface;
    // else by the ground's surface type.
    const bool set1 = !(event & 1);
    const float water = terrain_.waterLevel();
    if (water != 0 && origin.y - 0.8f < water) {
        const Vec3 at{origin.x, water, origin.z};
        spawnEffect(2, at);
        playSound(set1 ? SoundSystem::FsWaterR : SoundSystem::FsWaterL, at);
        // DF2.EXE 0x413975/0x413985: a +-10 m noise with the walker's team, flags 0.
        if (mission_.version >= 0x0F) ai_.noise(origin, 10.f, team, 0);
        return;
    }
    const int surface = terrain_.surfaceAt(origin.x, origin.z);
    if (mission_.version >= 0x0F) {
        // DF2.EXE 0x4139ae..0x413b35: the detail tile's CAL class through the
        // jump tables 0x413b38 / 0x413b50: 1 dirt 0x82/0x83, 2 grass
        // 0x8b/0x8c, 3 snow 0x86/0x87, 4 rock and 6 0x82/0x83, each with a
        // +-5 m noise; 5 sand 0x82/0x83 with +-2 m (0x413a3e); class 0 or 7+
        // (no .CAL, unmatched names): no sound and no noise. The noise goes
        // to 0x412840 with the walker's team and flags 0.
        if (surface < 1 || surface > 6) return;
        static const int a[7] = {0, 0x82, 0x8B, 0x86, 0x82, 0x82, 0x82}, b[7] = {0, 0x83, 0x8C, 0x87, 0x83, 0x83, 0x83};
        playSound((set1 ? a : b)[surface], origin);
        ai_.noise(origin, surface == 5 ? 2.f : 5.f, team, 0);
        return;
    }
    static const int ids1[7] = {0x82, 0x82, 0x8B, 0x86, 0x82, 0x82, 0x82}, ids0[7] = {0x83, 0x83, 0x8C, 0x87, 0x83, 0x83, 0x83};
    playSound((set1 ? ids1 : ids0)[std::clamp(surface, 0, 6)], origin);
}

// Pickups by touch (0x437a28 -> 0x437f60): objects with the movecb
// attribute within 0.75 m + their flat radius on both horizontal axes (R =
// model entry +0x64, copied from the LOD 0 header +0x20 by 0x4379f4: the
// 3DI header's flat radius), then DF.EXE asks the model's collision
// volumes. Delta Force 2 (0x4409d4 -> 0x440f80 -> 0x43fc80): see df2Touch.
// By item: mission goal items are carried (sound 0x166; one at a time); the
// medical crate restores full health (0x165); ammunition crates refill
// everything ("Ammo reloaded", 0x164); a land mine explodes. Picked crates
// are removed.
void Game::touchPickups() {
    if (playerDead()) return;
    const Vec3 feet = player_.feet;
    for (size_t i = 0; i < world_.objects.size(); ++i) {
        WorldObject& o = world_.objects[i];
        if (o.removed || !o.item || !(o.item->attrib & 0x10) || !o.model) continue;
        const Vec3 at = world_.nearest(o.pos, feet);
        if (std::getenv("OPENDF_DEBUGPICK") && (ticks_ % 30) == 0)
            std::fprintf(stderr, "pickup %s at (%.1f %.1f %.1f) feet (%.1f %.1f %.1f) r %.2f\n", o.item->name.c_str(), at.x, at.y, at.z,
                         feet.x, feet.y, feet.z, o.model->flatRadius);
        if (world_.df2Rules) {
            if (!df2Touch(o, player_.origin(), feet)) continue;
        } else {
            // 0x437a28: the mover box (+-0.75 m across, -0.75..+1.0 m up around
            // the body origin) must overlap one of the item's collision volumes
            // (in its own frame); an item without volumes cannot be touched.
            const Vec3 org = world_.nearest(player_.origin(), at);
            const float reach = 0.75f + o.model->flatRadius;
            if (std::abs(at.x - org.x) > reach || std::abs(at.z - org.z) > reach) continue;
            if (o.model->collision.empty()) continue;
            // Into the object's frame (as World.cpp's Frame: yaw only).
            const float th = kPi * 0.5f - o.heading, c = std::cos(th), sn = std::sin(th);
            const Vec3 dd = org - at;
            const Vec3 l{c * dd.x - sn * dd.z, dd.y, sn * dd.x + c * dd.z};
            bool touch = false;
            for (const auto& b : o.model->collision) {
                if (b.type != 1 && b.type != 3) continue;
                if (!(l.x + 0.75f >= b.min[0] && l.x - 0.75f <= b.max[0] && l.z + 0.75f >= b.min[2] && l.z - 0.75f <= b.max[2] &&
                      l.y + 1.0f >= b.min[1] && l.y - 0.75f <= b.max[1]))
                    continue;
                if (b.type == 3 && std::hypot(l.x - b.cx, l.z - b.cz) > b.r + 0.75f) continue;
                touch = true;
            }
            if (!touch) continue;
        }
        const int id = o.item->id - 100000;
        switch (id) {
        case 4008: case 4018: case 4021: case 4034: case 4038: case 4077:
            if (carried_ >= 0) break;
            carried_ = int(i);
            o.removed = true;   // carried: not drawn or hit (+0x6c 0x2)
            o.carried = !world_.df2Rules;   // DF1: still blocks sight (0x45b6e0)
            if (o.carried) o.pos = player_.origin();
            o.boxes.clear();
            sound_.play2D(0x166);
            break;
        case 4045:
            playerHp_ = std::max(playerHp_, (items_.byId(kItemPlayerSoldier) ? items_.byId(kItemPlayerSoldier)->hp : 4) *
                                                (o_.attributes & 0x100 ? 4 : 1));
            sound_.play2D(0x165);
            o.removed = true;
            o.boxes.clear();
            break;
        case 4010: case 4055:
            arsenal_.refill();
            pushMessage("Ammo reloaded", {1, 1, 1, 1}, false, kHudBright);   // 0x42b57f: colour 0x66f94c
            sound_.play2D(0x164);
            o.removed = true;
            o.boxes.clear();
            break;
        case 4054:
            if (world_.df2Rules) { mineContact(int(i)); break; }   // DF2 0x43ff93
            if (o.hp > 0) {
                o.hp = 0;
                o.removed = true;
                o.boxes.clear();
                detonate(o.pos + Vec3{0, 1, 0}, 60, o.heading);
            }
            break;
        default: break;
        }
    }
}

// One shrapnel round (0x44d730): speed ((rand*2) | 0x100) * factor >> 6 in
// 16.16 m per tick; flags 0x20, 400 ticks, power 1, falloff 64.
void Game::shrapnel(Vec3 at, float yaw, float pitch, int speedFactor) {
    const float speed = float(int((shrapRand() | 0x100) * uint32_t(speedFactor)) >> 6) / 65536.f * 60.f;
    Projectile p;
    p.pos = at;
    p.from = at;
    p.shooter = -1;
    p.vel = Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)} * speed;
    p.lifetime = 0x190;
    p.power = 1;
    p.falloff = 0x40;
    p.flags = 0x20;
    if (world_.df2Rules) p.gun = 31;   // DF2 0x44ac70: ammo 31 (100 grains; water drag)
    arsenal_.addProjectile(p);
}

void Game::weaponEvent(const WeaponEvent& e) {
    switch (e.kind) {
    case WeaponEvent::Place: {
        // Host create (0x447fd8): on the ground; sounds 0x79 satchel, 0x78 claymore.
        const ItemDef* item = items_.byId(100000 + e.item);
        const GpuModel* m = item && !item->graphic.empty() ? models_.get(vfs_, item->graphic) : nullptr;
        if (!item || !m) break;
        Vec3 at = e.pos;
        const float ext = world_.extent();
        at.x -= std::floor(at.x / ext) * ext;
        at.z -= std::floor(at.z / ext) * ext;
        if (world_.df2Rules) {
            // DF2.EXE 0x4520d6 (the host's create): the new object's own
            // neighbour list (0x43f2b0: an "object" item, 10 m; other types
            // none) and z = 0x43fb80(obj, pos, 0x4000): the terrain or a
            // volume top within 0.25 m above. INFERRED: the sent z is the
            // player's feet height.
            const float margin = item->type == "vehicle" ? 20.f : item->type == "object" || item->type == "person" ? 10.f : 0.f;
            at.y = world_.df2Surface(at, 0x4000 / 65536.f, world_.df2Neighbours(at, margin));
        } else {
            at.y = world_.groundHeight({at.x, player_.feet.y + 0.25f, at.z}, 0.25f);
        }
        const int obj = world_.spawn(item, m, at, e.heading);
        charges_.push_back({obj, e.item, -1});
        playSound(e.item == 4020 ? 0x79 : 0x78, at);
        break;
    }
    case WeaponEvent::Detonate:
        // 0x447db4: every satchel and radio claymore of the player, at once.
        for (auto& c : charges_)
            if ((c.item == 4020 || c.item == 4040) && !world_.objects[size_t(c.object)].removed) world_.objects[size_t(c.object)].hp = 0;
        break;
    case WeaponEvent::LaserPressed:
        sound_.play2D(0x1AE);
        if (const std::string* t = gameTable_.find("Fire Missions", "REQUESTED")) pushMessage(*t, kDf2Red, true, kHudRed);   // DF1 0x44a738: 0x66f944
        break;
    case WeaponEvent::LaserRequest:
        // 0x4482e0: a strike on the way if any are left and one of 16 slots
        // is free: 1280 ticks, at the requester's position now.
        if (fireMissions_ > 0 && strikes_.size() < 16) {
            --fireMissions_;
            strikes_.push_back({0x500, player_.feet});
            sound_.play2D(0x1AF);
            if (const std::string* t = gameTable_.find("Fire Missions", "REQUESTOK")) pushMessage(*t, {1, 1, 1, 1}, false, kHudRed);   // DF1 0x42b6f9
        } else {
            sound_.play2D(0x1B0);
            if (const std::string* t = gameTable_.find("Fire Missions", "REQUESTDENIED")) pushMessage(*t, kDf2Red, true, kHudRed);   // DF1 0x42b72f
        }
        break;
    }
}

// A placed charge goes off (0x447d28 -> 0x46cdfc satchel: blast type 5,
// radius 12, 200 shrapnel all round and upward; 0x447cac -> 0x46cc90
// claymore: blast type 1 and a fan of 160 pieces).
// Delta Force 2 (0x4572d0 -> 0x415490): 0.2 m higher, seeded.
void Game::bigBlast(Vec3 at) {
    if (world_.df2Rules) { at.y += 0x3333 / 65536.f; shrapSeed(); }
    explode(at, 0, true, 12.f);
    for (int k = 0; k < 200; ++k) {
        const float yaw = float(shrapRand() << 16) / 4294967296.f * 2 * kPi;
        const float pitch = float(shrapRand() << 14) / 4294967296.f * 2 * kPi;
        shrapnel(at, yaw, pitch, 0x1E);
    }
}

void Game::startCollapse(int index) {
    WorldObject& o = world_.objects[size_t(index)];
    // Crush box (0x454c10): everyone (and every vehicle) whose origin is
    // within +-R across and +-H up and down of the building loses 100 hp.
    // R, H = model entry +0x64 / +0x68. +0x64 is the 3DI header's +0x64, the
    // flat radius (the header is read verbatim into the entry, 0x43e591);
    // +0x68 is then overwritten with the LOD 0 header +0x24, the height
    // (0x43e6aa..0x43e6ad): GpuModel::height.
    const float R = o.model ? o.model->flatRadius : 5.f, Hh = o.model ? o.model->height : 5.f;
    for (size_t j = 0; j < world_.objects.size(); ++j) {
        WorldObject& t = world_.objects[j];
        if (int(j) == index || !t.alive() || !(t.person || (t.item && t.item->type == "vehicle"))) continue;
        const Vec3 p = world_.nearest(t.pos, o.pos);
        if (std::abs(p.x - o.pos.x) <= R && std::abs(p.z - o.pos.z) <= R && std::abs(p.y - o.pos.y) <= Hh)
            if (auto m = world_.damage(int(j), 100, 0x7fff, world_.deathMove(int(j), 2, 0, o.pos)); !m.empty()) pushMessage(m);
    }
    {
        const Vec3 p = world_.nearest(player_.origin(), o.pos);
        if (std::abs(p.x - o.pos.x) <= R && std::abs(p.z - o.pos.z) <= R && std::abs(p.y - o.pos.y) <= Hh) hurtPlayer(100, 2, o.pos);
    }
    // First think (0x402979..0x402a39): the building model's LOD 0 header
    // box [+0x28 xmin, +0x2c xmax, +0x30 ymin, +0x34 ymax] (16.16 m, model
    // axes): w = xmax - xmin, d = ymax - ymin; area ((w·d) >> 16) >> 16 m²,
    // perimeter (2 (w + d)) >> 16 m; rubble steps clamp(area/64 + 4, 8, 48),
    // dust 2·perimeter (twice that for the oil rigs 3072/3073/3102), smoke
    // area / 2. Every step uses the box inset by 2 m (0x402a39..0x402a66).
    const float* b = o.model ? o.model->box : nullptr;
    const int64_t x0 = b ? int64_t(std::lround(b[0] * 65536.f)) : 0, x1 = b ? int64_t(std::lround(b[1] * 65536.f)) : 0;
    const int64_t y0 = b ? int64_t(std::lround(b[2] * 65536.f)) : 0, y1 = b ? int64_t(std::lround(b[3] * 65536.f)) : 0;
    const int64_t w = x1 - x0, d = y1 - y0;
    const int area = int(((w * d) >> 16) >> 16), perim = int((2 * (w + d)) >> 16);
    const int rubble = std::clamp((area >> 6) + 4, 8, 48);
    int dust = 2 * perim;
    if (o.item && (o.item->id == 103072 || o.item->id == 103073 || o.item->id == 103102)) dust *= 2;
    Collapse c{index, 0, 0, rubble, dust, area >> 1, {}};
    c.box[0] = float(x0) / 65536.f + 2.f;
    c.box[1] = float(x1) / 65536.f - 2.f;
    c.box[2] = float(y0) / 65536.f + 2.f;
    c.box[3] = float(y1) / 65536.f - 2.f;
    collapses_.push_back(c);
    // Step 0 (0x412840(x, y, z, 0x1f40000, 0, 0)): a +-500 m box on all
    // three axes with flags 0 and team 0. Relaxed soldiers inside it turn
    // alert and face it (0x413620); the alert-state turn (0x415570) only
    // follows gunshot boxes (flags 2), so not this one.
    ai_.noise(o.pos, 500.f, 0, 0);
}

Game::Effect* Game::spawnSmk2(Vec3 at, bool dust, float dx, float dz, int t) {
    // Nothing below the water (0x41401c / 0x413e80).
    if (const float water = terrain_.waterLevel(); at.y <= water) return nullptr;
    const ItemDef* item = items_.byId(107051);   // "dust cloud", graphic smkt_h, move smk2
    const GpuModel* m = item && !item->graphic.empty() ? models_.get(vfs_, item->graphic) : nullptr;
    if (!m || m->lods.empty()) return nullptr;
    const auto& l = m->lods[0];
    Effect e{m, at, 1.f, 0, 0x7fffffff, l.ticksPerFrame, l.frames};
    e.smk2 = true;
    // The generator is DF2's 0x41ac40, shared with the shrapnel (shrapRand).
    if (dust) {
        // 0x414010 kind 0: a random heading at 0x300..0x4ff a tick (replaced
        // below by 0x414220), roll rand << 16, size (rand + 0x10000) >> 3,
        // spin +-0x2000000, alpha 400 at -1.5 a tick, shift 5, cap 3.0.
        (void)shrapRand();
        (void)shrapRand();
        e.rot = int32_t(shrapRand() << 16);
        e.size = int32_t((shrapRand() + 0x10000) >> 3);
        e.spin = (shrapRand() & 0x10) ? 0x2000000 : int32_t(0xfe000000);
        e.alpha = 0x1900000;
        e.dAlpha = -0x18000;
        e.shift = 5;
        e.maxSize = 0x30000;
        // 0x414220: the horizontal velocity 1/16 m a tick along (dx, dy).
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len > 0) {
            e.vel[0] = int32_t(dx / len * 4096.f);
            e.vel[1] = int32_t(-dz / len * 4096.f);
        }
    } else {
        // 0x413e80: size (rand + 0x20000) >> 4, roll rand << 16, v = (1, 0,
        // 0) raw, alpha 400 at -1.125 a tick, shift 6, cap 2.0.
        e.size = int32_t((shrapRand() + 0x20000) >> 4);
        e.rot = int32_t(shrapRand() << 16);
        e.vel[0] = 1;
        e.alpha = 0x1900000;
        e.dAlpha = -0x12000;
        e.shift = 6;
        e.maxSize = 0x20000;
    }
    // Colour: lerp((0x73, 0x58, 0x39) -> 0, t / 256) | 1 (0x413e40), plus
    // (rand · 17) & 0x0f0f0f (0x4141dd); then 0x411f70: the scene colour
    // grade 0x411ec0 (y = (R + 2G + B) >> 2, saturation [0xbba8ac] about y,
    // then the per-channel tables built by 0x411d50; the grade the terrain
    // and the model colours get, 0x43f27d). OpenDF applies that grade when
    // drawing (the model shader's post() on the replacing colour).
    const int tt = std::clamp(t, 0, 255);
    uint32_t c = (uint32_t(0x73 - 0x73 * tt / 256) << 16) | (uint32_t(0x58 - 0x58 * tt / 256) << 8) | uint32_t(0x39 - 0x39 * tt / 256) | 1u;
    const uint32_t r = shrapRand();
    c += ((r << 4) + r) & 0x0f0f0f;
    e.tint[0] = uint8_t(c >> 16);
    e.tint[1] = uint8_t(c >> 8);
    e.tint[2] = uint8_t(c);
    if (std::getenv("OPENDF_DEBUGHITS")) std::fprintf(stderr, "smk2 %s size %.2f at (%.1f %.1f %.1f) frames %d tint %d %d %d\n", dust ? "dust" : "smoke", e.size / 65536.f, at.x, at.y, at.z, e.frames, e.tint[0], e.tint[1], e.tint[2]);
    effects_.push_back(e);
    if (effects_.size() > 0x400) effects_.erase(effects_.begin());
    return &effects_.back();
}

// One think per 8 ticks (buildings are visited 1/8 per tick, 0x440180).
void Game::tickCollapses() {
    for (auto& c : collapses_) {
        if (c.step >= 64 || --c.wait > 0) continue;
        c.wait = 8;
        WorldObject& o = world_.objects[size_t(c.object)];
        // The building's matrix (0x42bb00 -> 0x42b3f0): points in its frame,
        // local z 0, so the edges turn with its heading (INFERRED: yaw only).
        const Mat4 frame = o.matrix(o.pos);
        auto world = [&](float x, float y) {
            const Vec3 l{x, 0, -y};   // file x forward, y left -> OpenDF model axes
            return Vec3{frame.at(0, 0) * l.x + frame.at(0, 2) * l.z + frame.at(0, 3), frame.at(1, 3),
                        frame.at(2, 0) * l.x + frame.at(2, 2) * l.z + frame.at(2, 3)};
        };
        // rand · (hi - lo) + lo, rand 16 bits (16.16 fraction).
        auto rnd = [&](float lo, float hi) {
            return lo + float((int64_t(shrapRand()) * int64_t((hi - lo) * 65536.f)) >> 16) / 65536.f;
        };
        const float* b = c.box;
        if (c.step < c.rubble)
            for (int k = 0; k < 2; ++k) {
                // Two rubble bursts on the inset edges: odd steps the x edges at
                // a random y (0x402ab6), even steps the y edges at a random x;
                // effect 0x13 and sound 0x32 through the delay queue (0x45e910).
                const Vec3 at = (c.step & 1) ? world(b[k], rnd(b[2], b[3])) : world(rnd(b[0], b[1]), b[2 + k]);
                spawnEffect(0x13, at);
                sound_.playDelayed(0x32, at);
            }
        if (c.step > 3) {
            // Dust (0x402c49..0x402d76): up to 12 clouds from the budget, at a
            // random point moved onto side i & 3 (x min, x max, y min, y max),
            // drifting outward along (point - origin) (0x414220).
            const int n = std::min(12, c.dust);
            c.dust -= n;
            for (int i = 0; i < n; ++i) {
                float x = rnd(b[0], b[1]), y = rnd(b[2], b[3]);
                switch (i & 3) {
                case 0: x = b[0]; break;
                case 1: x = b[1]; break;
                case 2: y = b[2]; break;
                default: y = b[3]; break;
                }
                const Vec3 at = world(x, y);
                spawnSmk2(at, true, at.x - o.pos.x, at.z - o.pos.z, 0);
            }
            // Smoke only on a step that spawned no dust (0x402d7e): up to 6
            // puffs, a random angle, radius (rand·R >> 16) / 2, height up to
            // H / 16, shade i + 4·step (0x402d84..0x402e6d -> 0x413e80).
            if (n == 0) {
                const float R = o.model ? o.model->flatRadius : 5.f, Hh = o.model ? o.model->height : 5.f;
                const int k = std::min(6, c.smoke);
                c.smoke -= k;
                for (int i = 0; i < k; ++i) {
                    const float a = float(shrapRand() << 16) / 4294967296.f * 2 * kPi;
                    const float rr = rnd(0, R) * 0.5f, h = rnd(0, Hh / 16.f);
                    spawnSmk2(o.pos + Vec3{std::cos(a) * rr, h, -std::sin(a) * rr}, false, 0, 0, i + 4 * c.step);
                }
            }
        }
        if (c.step > 2 && c.step < 12 && o.model && !o.model->lods.empty()) {
            // Fireballs, steps 3..11 (0x402e73..0x402fba): one per mesh 1 ..
            // count - 2 of the LOD 0 shape, at the centre of the mesh's box
            // (+0x40..+0x54), through the building's matrix (0x42b3f0), as a
            // dust cloud (0x414220) moving outward along (point - origin);
            // then v.x and v.y += (rand & 0x1ff) - 0x200, v.z += rand & 0xff,
            // move 0x42abc0, growth shift 7, cap 0xc000, age += 3 · step.
            const auto& centres = o.model->lods[0].meshCentre;
            for (int i = 1; i < int(centres.size()) - 1; ++i) {
                const auto& f = centres[size_t(i)];
                const Vec3 l{f[0], f[2], -f[1]};   // file x forward, y left, z up -> OpenDF model axes
                const Vec3 at{frame.at(0, 0) * l.x + frame.at(0, 1) * l.y + frame.at(0, 2) * l.z + frame.at(0, 3),
                              frame.at(1, 0) * l.x + frame.at(1, 1) * l.y + frame.at(1, 2) * l.z + frame.at(1, 3),
                              frame.at(2, 0) * l.x + frame.at(2, 1) * l.y + frame.at(2, 2) * l.z + frame.at(2, 3)};
                Effect* e = spawnSmk2(at, true, at.x - o.pos.x, at.z - o.pos.z, 0);
                if (!e) continue;
                e->vel[0] += int32_t(shrapRand() & 0x1ff) - 0x200;
                e->vel[1] += int32_t(shrapRand() & 0x1ff) - 0x200;
                e->vel[2] += int32_t(shrapRand() & 0xff);
                e->fireball = true;
                e->shift = 7;
                e->maxSize = 0xc000;
                e->age += 3 * c.step;
                if (std::getenv("OPENDF_DEBUGHITS")) std::fprintf(stderr, "fireball step %d mesh %d at (%.1f %.1f %.1f)\n", c.step, i, at.x, at.y, at.z);
            }
        }
        if (c.step == 8) world_.applyHusk(c.object);   // flags |= 8: the husk
        ++c.step;
    }
    collapses_.erase(std::remove_if(collapses_.begin(), collapses_.end(), [](const Collapse& c) { return c.step >= 64; }), collapses_.end());
}

void Game::blowCharge(Charge& c) {
    WorldObject& o = world_.objects[size_t(c.object)];
    o.removed = true;
    o.boxes.clear();
    const Vec3 at = o.pos + Vec3{0, 0.2f, 0};
    if (c.item == 4020) {
        if (world_.df2Rules) shrapSeed();
        explode(at, 0, true, 12.f);
        for (int k = 0; k < 200; ++k) {
            const float yaw = o.heading + float(shrapRand() << 16) / 4294967296.f * 2 * kPi;
            const float pitch = float(shrapRand() << 14) / 4294967296.f * 2 * kPi;
            shrapnel(at, yaw, pitch, 0x1E);
        }
    } else {
        explode(at, 0);
        // Yaw heading - 30 degrees + k x 1.8 (DF.EXE's loop adds k x 0.3 to
        // its 1.5-degree steps, so the fan is lopsided), pitch -2, 3, 8, 13.
        for (int k = 0; k < 40; ++k)
            for (int p = 0; p < 4; ++p)
                shrapnel(at, o.heading + (-30.f + 1.8f * float(k)) * kPi / 180.f, (-2.f + 5.f * float(p)) * kPi / 180.f, 0x3C);
    }
}

// Per tick: charges hit or set off blow up; motion claymores (cly2, every
// 0x20 ticks, 0x46cf48) sense anyone within 15 m ahead (+-33.75 degrees),
// in sight and moving at least 1.1 m/s (vehicles 0.2), and go off 32 ticks
// later. Fire missions count down; while the laser is on a point the strike
// is pulled toward it 1 m a tick; at 0 a satchel-sized shell lands.
void Game::tickCharges() {
    for (auto& c : charges_) {
        WorldObject& o = world_.objects[size_t(c.object)];
        if (o.removed) continue;
        if (o.hp <= 0 || o.wrecked) { blowCharge(c); continue; }
        if (c.fuse > 0) { if (--c.fuse == 0) blowCharge(c); continue; }
        if (c.item != 4014 || (ticks_ & 0x1f)) continue;
        const Vec3 sensor = o.pos + Vec3{0, 0.3f, 0};
        auto sees = [&](Vec3 at, float speed, float need) {
            const Vec3 v = world_.nearest(at, sensor) - sensor;
            float a = std::abs(v.x), b = std::abs(v.y), cc = std::abs(v.z);
            if (a < b) std::swap(a, b);
            if (a < cc) std::swap(a, cc);
            if (b < cc) std::swap(b, cc);
            if (a + b * 11.f / 32.f + cc * 0.25f > 15.f || speed < need) return false;
            float rel = std::atan2(v.x, -v.z) - o.heading;
            rel -= std::floor(rel / (2 * kPi) + 0.5f) * 2 * kPi;
            if (std::abs(rel) > 0x18 / 256.f * 2 * kPi) return false;
            RayHit h;
            return !world_.raycast(sensor, sensor + v, h, true, c.object, false);
        };
        bool hit = !playerDead() && sees(player_.origin(), player_.speed(), 1.1f);
        for (const auto& q : world_.objects) {
            if (hit) break;
            if (!q.person || !q.alive() || !q.moves) continue;
            const char* n = moveName(q.moveNumber);
            const AnimMove* mv = n ? q.moves->find(n) : nullptr;
            hit = sees(q.pos, mv ? mv->velocity : 0.f, 1.1f);
        }
        // Vehicles moving at 0.2 m/s or more (target z + 1 m).
        for (const auto& [obj, speed] : vehicles_.moving()) {
            if (hit) break;
            hit = sees(world_.objects[size_t(obj)].pos + Vec3{0, 1, 0}, speed, 0.2f);
        }
        if (hit) c.fuse = 32;
    }
    if (laserWindow_ > 0) --laserWindow_;
    for (auto& st : strikes_) {
        if (laserWindow_ > 0) {
            const Vec3 d = world_.nearest(laserPoint_, st.target) - st.target;
            const float dx = std::abs(d.x), dz = std::abs(d.z), len = std::max(dx, dz) + std::min(dx, dz) * 0.5f;
            if (len > 1.f) st.target = st.target + Vec3{d.x, 0, d.z} * (1.f / len);
        }
        if (--st.timer == 0) {
            // DF2 0x458a56: ground + 0x1999; below the water, water + 0x4000;
            // then the big blast (+0.2 m, 0x4572d0).
            Vec3 at{st.target.x, terrain_.heightAt(st.target.x, st.target.z) + 0x1999 / 65536.f, st.target.z};
            if (const float w = terrain_.waterLevel(); w != 0 && at.y < w) at.y = w + 0x4000 / 65536.f;
            bigBlast(at);
        }
    }
    strikes_.erase(std::remove_if(strikes_.begin(), strikes_.end(), [](const Strike& s) { return s.timer <= 0; }), strikes_.end());
}

// Drop (0x46c9c0): 2 m ahead on the ground, or where the body lies.
void Game::dropCarried() {
    if (carried_ < 0) return;
    WorldObject& o = world_.objects[size_t(carried_)];
    Vec3 at = player_.feet;
    if (!playerDead()) at = at + Vec3{std::sin(player_.yaw), 0, -std::cos(player_.yaw)} * 2.f;
    const float ext = world_.extent();
    at.x -= std::floor(at.x / ext) * ext;
    at.z -= std::floor(at.z / ext) * ext;
    o.pos = {at.x, terrain_.heightAt(at.x, at.z) + o.lift, at.z};
    o.base = o.pos.y - o.lift;
    o.removed = false;
    o.carried = false;
    o.boxes = o.model ? o.model->collision : std::vector<CollisionBox>{};
    carried_ = -1;
}

// Delta Force 2's air (DF2.EXE 0x4587f0, a player tick, only on maps with
// water): 0 when dead (or wearing the LAR V rebreather, not selectable in
// OpenDF yet); +1 a tick while the origin is at least 0.5 m below the water;
// at the surface a count over 60 resets with AIR_SUCK (801), a smaller one
// stays. Gags (802) at 2304, 2496 and 2688; over 2880 the player drowns at
// once (no killer, the water death move, no red flash).
void Game::tickAir() {
    const float water = terrain_.waterLevel();
    if (water == 0) return;
    if (playerDead()) { air_ = 0; return; }
    const Vec3 at = player_.origin();
    if (at.y <= water - 0.5f) {
        ++air_;
        if (air_ == 2304 || air_ == 2496 || air_ == 2688) playSound(802, at);
        if (air_ > 2880) {
            const int flash = damageFlash_;
            hurtPlayer(playerHp_, 5, at);   // kind 5: drowning (0x458899)
            damageFlash_ = flash;
        }
    } else if (air_ > 60) {
        air_ = 0;
        playSound(801, at);
    }
}

// Damage to the player (0x44fe9e blast, 0x44f2b2 bullets): hit points, and
// the red flash +0x78 up to 0xFF (0x44b6d0).
void Game::hurtPlayer(int amount, int deathType, Vec3 from, int part) {
    if (playerDead()) return;
    playerHp_ -= amount;
    damageFlash_ = std::min(0xFF, damageFlash_ + 0x78);
    if (playerDead()) dropCarried();   // 0x46c9c0 at death: where the body is
    if (!playerDead() || playerChr_.partCount() == 0) return;
    // 0x45a234: the death move (0x46c920), as for any character.
    const float water = terrain_.waterLevel();
    const Vec3 v = world_.nearest(from, player_.feet) - player_.feet;
    float rel = std::atan2(v.x, -v.z) - player_.yaw;
    rel -= std::floor(rel / (2 * kPi) + 0.5f) * 2 * kPi;
    const int dir = std::abs(rel) <= kPi / 4 ? 0 : std::abs(rel) >= 3 * kPi / 4 ? 2 : rel < 0 ? 1 : 3;
    const int move = World::deathMoveNumber(world_.df2Rules, water != 0 && water >= player_.origin().y, deathType, part, dir);
    const char* name = moveName(move);
    const AnimMove* mv = name ? playerMoves_.find(name) : nullptr;
    if (!mv) mv = playerMoves_.find("Death_B_P01DF");
    // From whatever move the body was in (no reset to standing).
    if (mv) playerBody_.start(playerChr_, mv->animation);
    deadShift_ = {0, 0, 0};
    deadYaw_ = player_.yaw;
}

Vec3 Game::deadBodyOrigin() const {
    const ChrFrame* f = playerBody_.currentFrame(playerChr_);
    return player_.feet + deadShift_ + Vec3{0, f ? Chr::originHeight(*f) : 0.f, 0};
}

// Dead (0x404104 -> 0x481224): third-person orbit around the body, yaw
// = tick << 21 (one turn per 2048 ticks), pitch 0x4000000 (5.625° down),
// 3 m away from the body position raised 0.5 m (0x480d40), at least 1 m
// above the ground.
// (0x480d75): the eye is the pivot plus (-3, 0, +0.5) turned by yaw =
// smoothed body yaw + tick << 21 and pitch 0x4000000 (3.03 m behind, 0.20 m
// up), at least 1 m above the ground (less while the pivot still lags by
// under 0.5 m); it looks at the pivot raised 0.5 m (0x4810ec).
Vec3 Game::viewDir() const {
    if (!playerDead()) return player_.forward();
    const float s = std::abs(camLag_.x) + std::abs(camLag_.z);
    const Vec3 target = camPivot_ + Vec3{0, 0.5f + (s < 0.5f ? (s - 0.5f) * 0.5f : 0.f), 0};
    return normalize(target - viewEye());
}

Vec3 Game::viewEye() const {
    if (!playerDead()) return player_.eye();
    const float th = camYaw_ + float(uint32_t(ticks_) << 21) / 4294967296.f * 2 * kPi;
    const float p = float(0x4000000) / 4294967296.f * 2 * kPi;
    const float back = -3.f * std::cos(p) - 0.5f * std::sin(p), up = -3.f * std::sin(p) + 0.5f * std::cos(p);
    Vec3 e = camPivot_ + Vec3{std::cos(th) * back, up, -std::sin(th) * back};
    const float s = std::abs(camLag_.x) + std::abs(camLag_.z);
    float floor = terrain_.heightAt(e.x, e.z) + 1.f;
    if (s < 0.5f) floor += s - 0.5f;
    e.y = std::max(e.y, floor);
    return e;
}

// New puff (0x480aac): item 107016, no speed yet, random roll, scale
// (rand*2 + 0x20000) >> 4 in 16.16 (0.125..0.19).
void Game::spawnPuff(Vec3 at) {
    const ItemDef* item = items_.byId(107016);
    const GpuModel* m = item && !item->graphic.empty() ? models_.get(vfs_, item->graphic) : nullptr;
    if (!m || m->lods.empty()) return;
    const auto& l = m->lods[0];
    std::uniform_int_distribution<int> r(0, 0x7fff);
    Effect e{m, at, float((r(effectRng_) * 2 + 0x20000) >> 4) / 65536.f, 0, l.frames * l.ticksPerFrame, l.ticksPerFrame,
             l.frames};
    e.smoke = true;
    e.roll = float(r(effectRng_)) / 65536.f;   // rand << 16: 0..180 degrees (0x480aac)
    effects_.push_back(e);
    if (effects_.size() > 0x400) effects_.erase(effects_.begin());
}

// Per 60 Hz tick: effects age; smoke puffs move (0x480bfc): x += v, north
// += v/4, up += w; scale grows 1/128 up to 2.0; roll one step; then w +=
// 0x20 and v += 8 (16.16 per tick). Generators emit their puffs; barrels
// count down to their explosion.
void Game::tickEffects() {
    for (auto& e : effects_) {
        ++e.age;
        if (e.canopy) {
            // 0x42c570: pitch t·300 << 14 up to 90°, sinking 0x400 a tick, then
            // 0x0c00 a tick at 90°; gone under -16 m.
            if (e.age * 300 <= 0x10000) { e.pitch = float(e.age * 300) / 65536.f * kPi / 2; e.pos.y -= 0x400 / 65536.f; }
            else { e.pitch = kPi / 2; e.pos.y -= 0xc00 / 65536.f; }
            if (e.pos.y < -16.f) e.age = e.life;
            continue;
        }
        if (e.fireball) {
            // 0x42abc0 each tick: pos += v; on ticks with bit 2 of [0x6536b4]:
            // v.x and v.y lose >> 8, v.z += 0x10 while under 0x200, size +=
            // size >> shift up to the cap. Roll and spin as smk2. No alpha
            // step: alpha +0x7c = 0xff. On those ticks t = 6 · age picks the
            // colour, lerp(a, b, t & 0xff) per channel (0x413e40): t < 0x100
            // (ff ff c0) -> (ff c0 40), < 0x200 (ff c0 40) -> (c0 40 00),
            // < 0x300 (c0 40 00) -> (40 00 00); gone at 0x300 (0x4160d0).
            e.pos.x += e.vel[0] / 65536.f;
            e.pos.z -= e.vel[1] / 65536.f;
            e.pos.y += e.vel[2] / 65536.f;
            if (ticks_ & 4) {
                e.vel[0] -= e.vel[0] >> 8;
                e.vel[1] -= e.vel[1] >> 8;
                if (e.vel[2] < 0x200) e.vel[2] += 0x10;
                e.size = std::min(e.maxSize, int32_t(uint32_t(e.size) + (uint32_t(e.size) >> e.shift)));
            }
            e.rot = int32_t(uint32_t(e.rot) + uint32_t(e.spin));
            if (e.spin > 0x400000) e.spin = std::max(0x400000, e.spin - 0x100000);
            else if (e.spin < -0x400000) e.spin = std::min(-0x400000, e.spin + 0x100000);
            else e.spin = e.spin < 0 ? -0x400000 : 0x400000;
            if (ticks_ & 4) {
                static const uint8_t kStops[4][3] = {{0xff, 0xff, 0xc0}, {0xff, 0xc0, 0x40}, {0xc0, 0x40, 0x00}, {0x40, 0x00, 0x00}};
                const int t = 6 * e.age;
                if (t >= 0x300) { e.age = e.life; continue; }
                const int k = t >> 8, u = t & 0xff;
                for (int ch = 0; ch < 3; ++ch)
                    e.tint[ch] = uint8_t(kStops[k][ch] + (((int(kStops[k + 1][ch]) - int(kStops[k][ch])) * u) >> 8));
                e.alpha = 0xff0000;   // +0x7c = 0xff
                e.dAlpha = 0;
            }
            continue;
        }
        if (e.smk2) {
            // smk2 (0x42aaa0) each tick: pos += v; on ticks with bit 2 of the
            // frame counter [0x6536b4] (4 of every 8): vz += 0x10, vx and vy
            // lose >> 6, size += size >> shift up to the cap; alpha += step,
            // gone at <= 0; roll += spin, the spin easing 0x100000 a tick
            // toward +-0x400000 and held there (0 becomes +0x400000).
            e.pos.x += e.vel[0] / 65536.f;
            e.pos.z -= e.vel[1] / 65536.f;
            e.pos.y += e.vel[2] / 65536.f;
            if (ticks_ & 4) {
                e.vel[2] += 0x10;
                e.vel[0] -= e.vel[0] >> 6;
                e.vel[1] -= e.vel[1] >> 6;
                e.size = std::min(e.maxSize, e.size + (e.size >> e.shift));
            }
            e.alpha += e.dAlpha;
            if (e.alpha <= 0) e.age = e.life;
            e.rot = int32_t(uint32_t(e.rot) + uint32_t(e.spin));
            if (e.spin > 0x400000) e.spin = std::max(0x400000, e.spin - 0x100000);
            else if (e.spin < -0x400000) e.spin = std::min(-0x400000, e.spin + 0x100000);
            else e.spin = e.spin < 0 ? -0x400000 : 0x400000;
            continue;
        }
        if (!e.smoke) continue;
        e.pos.x += e.drift;
        e.pos.z -= e.drift * 0.25f;
        e.pos.y += e.rise;
        e.scale = std::min(2.f, e.scale + e.scale / 128.f);
        e.roll += 1.f / 256.f;
        e.rise += 0x20 / 65536.f;
        e.drift += 8 / 65536.f;
    }
    effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const Effect& e) { return e.age >= e.life; }),
                   effects_.end());
    std::uniform_int_distribution<int> r(0, 0x7fff);
    for (auto& g : smokeGens_) {
        if (--g.wait > 0) continue;
        spawnPuff(g.pos);
        ++g.puffs;
        g.wait = ((r(effectRng_) * 2) >> 13) + 16;
    }
    smokeGens_.erase(std::remove_if(smokeGens_.begin(), smokeGens_.end(), [](const SmokeGen& g) { return g.puffs >= 100; }),
                     smokeGens_.end());
    for (auto& o : world_.objects)
        if (o.detonate > 0 && --o.detonate == 0) {
            o.detonate = -1;
            detonate(o.pos, 10, o.heading);
        }
}

// DF2.EXE 0x4524d0 -> 0x412b00, the common blast: effect 3 (0x413850),
// blast type 1 of 10 m (0x44d460), sound 0x32 (0x45e910), a +-200 m noise
// with flags 0 (0x412b5c), then `fragments` seeded rounds; no smoke
// generator of its own.
void Game::df2Blast(Vec3 at, int fragments, float heading) {
    explode(at, 1);   // surface 1: effect 3, no smoke generator
    ai_.noise(at, 200.f, 0, 0);
    shrapSeed();
    fragmentBurst(at, fragments, heading);
}

// DF2.EXE 0x440f80 (mode 2, from the person mover 0x4409a2..0x4409ef): two
// points of radius 0.5 m, the origin (+0x24..+0x2c) and origin + ((frame
// word +0x44 + 0x80) << 8), the pose box's foot plus 0.5 m, each within
// +-(R + 0.5 m) of the object on all three axes (R = model entry +0x60, the
// 3DI header radius: the header is read verbatim into the entry, 0x43e591).
// Then 0x440f80 itself (0x4410c0..0x441379; the callback [entry+0x4c] only
// poses the meshes): per mesh with volumes (mesh +0x1c) and not pose slot
// +0x3c & 1, the point in the posed mesh frame must lie in the mesh box
// +0x40..+0x54 widened by r, then inside a volume's convex hull (every
// plane, vol +0x48 count / +0x4c, i16 normal x4 and i16 d << 8: n·p + d - r
// < 0); type 4 and 6 volumes only set out-flags 1 / 4 (0x4412fb..0x441318).
// The same test as the person mover's push-out (World::df2Overlap, READ),
// true when a volume other than type 4 / 6 is entered.
bool Game::df2Touch(const WorldObject& o, Vec3 origin, Vec3 feet) const {
    const Vec3 pts[2] = {world_.nearest(origin, feet), feet + Vec3{0, 0.5f, 0}};
    const float halves[2] = {0.5f, 0.5f};
    Vec3 push;
    uint32_t flags = 0;
    return world_.df2Overlap(o, pts, halves, 2, push, flags);
}

// A Land Mine stepped on (DF2.EXE 0x43fc80 type 0xFD6, case 0x43ff93): if
// its hp is above 0, the common blast at the mine with no fragments (owned
// by the mover), a Smoke Generator 104046 0.5 m above it (0x4531e0 ->
// 0x4142e0), flags |= 4, hp 0, radar shape 0. The mine stays drawn (the
// draw list 0x4490d0 skips only +0x64 & 2 and no model; the draw 0x4459f0
// takes the husk only with flag 8); later contacts and rounds do nothing.
void Game::mineContact(int index) {
    WorldObject& o = world_.objects[size_t(index)];
    if (o.hp <= 0 || o.wrecked || o.removed) return;
    o.hp = 0;
    o.wrecked = true;
    o.radarShape = 0;
    df2Blast(o.pos, 0, o.heading);
    smokeGens_.push_back({o.pos + Vec3{0, 0.5f, 0}, 0, 0});
}

// Delta Force 2's destruction of the section-8 items (the ai functions
// below run on hp <= 0); true if handled.
bool Game::df2ItemDestroyed(int index) {
    WorldObject& o = world_.objects[size_t(index)];
    const std::string f = o.item ? o.item->aiFunction : std::string();
    if (f == "slgt") {
        // 0x427f30: once, flags |= 0x10C (destroyed, the husk gligtx, dead for
        // the AI), the target yaw = the heading, the turn stopped, and the
        // common blast 1 m above it (owner the last attacker) with no
        // fragments. The husk stays.
        for (auto& l : lights_)
            if (l.object == index) l.dead = true;
        df2Blast(o.pos + Vec3{0, 1.f, 0}, 0, o.heading);
        return true;
    }
    if (f == "mine") {
        // 0x4295f0: hit by a round (event 1) or a blast (2) to hp <= 0: flags
        // |= 4 and the common blast at the mine (no lift), no fragments; no
        // smoke, no husk (still drawn: 0x4490d0, 0x4459f0), the radar shape kept.
        df2Blast(o.pos, 0, o.heading);
        return true;
    }
    if (f == "vtrg") {
        // 0x42a980: on hp <= 0, once: the nearest group-1 "vwal crack"
        // (item 3062) by 0x41aad0 (max + 11/32 mid + 1/4 min); within 10 m
        // (0xA0000) its hp = 0 and flags |= 0x10C, so it turns into its husk
        // vwalx without its own break step (gnrc 0x4297c0 sees flag 8 and
        // idles: no effect, no "destroyed" message), then the common blast
        // with 10 fragments at the wall's origin (owner 0). The trigger
        // itself gets 0x10C and, having no husk +0x14, is still drawn with its
        // own model (0x4459f0: the husk only when flag 8 and +0x14 is set).
        int best = -1;
        float bestD = 0;
        for (size_t j = 0; j < world_.objects.size(); ++j) {
            const WorldObject& w = world_.objects[j];
            if (w.missionGroup != 1 || !w.item || w.item->id != 103062 || w.removed || !w.model) continue;
            const Vec3 v = world_.nearest(w.pos, o.pos) - o.pos;
            float a = std::abs(v.x), b = std::abs(v.y), c = std::abs(v.z);
            if (a < b) std::swap(a, b);
            if (a < c) std::swap(a, c);
            if (b < c) std::swap(b, c);
            const float d = a + b * 11.f / 32.f + c * 0.25f;
            if (best < 0 || d < bestD) best = int(j), bestD = d;
        }
        if (best >= 0 && bestD < 10.f) {
            WorldObject& w = world_.objects[size_t(best)];
            w.hp = 0;
            if (w.husk) { w.model = w.husk; w.husk = nullptr; }
            w.wrecked = true;
            world_.refreshBoxes(w);
            df2Blast(w.pos, 10, w.heading);
        }
        return true;
    }
    return false;
}

// Per tick: the searchlights' turn (move `slgt` 0x4281e0) and think (ai
// 0x427f30), and people walking onto Land Mines (0x440400 -> 0x43fc80).
void Game::tickDf2Items() {
    auto toYaw = [](float heading) { return uint32_t(int64_t(std::llround(double(kPi * 0.5f - heading) / (2 * kPi) * 4294967296.0))); };
    auto fromYaw = [](uint32_t y) { return kPi * 0.5f - float(y) / 4294967296.f * 2 * kPi; };
    auto toAng = [](float rad) { return int32_t(int64_t(std::llround(double(rad) / (2 * kPi) * 4294967296.0))); };
    for (auto& l : lights_) {
        WorldObject& o = world_.objects[size_t(l.object)];
        if (l.dead || o.wrecked || o.removed) continue;
        uint32_t yaw = toYaw(o.heading);
        int32_t pitch = toAng(o.pitch);
        const int R = std::max(1, int(o.item->turnRate));   // item +0xE4 (turn_rate 20)
        auto turn = [&]() {
            // 0x427e40: the yaw speed (1/65536 turn a tick) rises by R >> 3
            // while far (|d| >> 16 > 8R) and slower than R, else falls by R >> 3
            // while above it; it snaps when closer than the speed. The pitch
            // moves R a tick and snaps within R.
            const int32_t d = int32_t(l.targetYaw - yaw);
            const int ad = int(std::abs(int64_t(d)) >> 16);
            if (ad > 8 * R && l.speed < R) l.speed += R >> 3;
            else if (l.speed > (R >> 3)) l.speed -= R >> 3;
            if (ad < l.speed) { yaw = l.targetYaw; l.moving = 0; }
            else { l.moving = d > 0 ? (l.speed << 16) : -(l.speed << 16); yaw += uint32_t(l.moving); }
            const int32_t dp = int32_t(l.targetPitch - uint32_t(pitch));
            if (std::abs(int64_t(dp)) >> 16 <= R) pitch = int32_t(l.targetPitch);
            else pitch += dp > 0 ? (R << 16) : -(R << 16);
        };
        if (l.init) {
            turn();
            // No target and the turn done: the sweep swings to the other side
            // of the placed heading (+-45 degrees).
            if (l.target == -1 && l.moving == 0) {
                l.sweep = -l.sweep;
                l.targetYaw = l.homeYaw + uint32_t(l.sweep);
                l.targetPitch = l.homePitch;
            }
        }
        // The think (every visit when its timer runs out; group-1 objects are
        // visited 1 tick in 8, INFERRED as for the collapses).
        if (--l.wait <= 0) {
            l.wait = 0x10;
            if (!l.init) {
                // First think: home yaw = heading | 1, home pitch, sweep
                // 0x20000000 (45 degrees), aiming at home + 45.
                l.init = true;
                l.homeYaw = yaw | 1;
                l.homePitch = uint32_t(pitch);
                l.sweep = 0x20000000;
                l.targetYaw = l.homeYaw + uint32_t(l.sweep);
                l.targetPitch = l.homePitch;
            } else {
                const bool targetDead = l.target == -3 ? playerDead() : l.target >= 0 && !world_.objects[size_t(l.target)].alive();
                if (--l.counter > 0 && l.target != -1 && !targetDead) {
                    // Tracking: aim at the target with a +-0x200000 jitter,
                    // the yaw within +-45 degrees of home, the pitch within
                    // +-11.25 degrees; think 0x20.
                    const Vec3 tp = world_.nearest(l.target == -3 ? player_.origin() : world_.objects[size_t(l.target)].pos, o.pos);
                    const Vec3 d = tp - o.pos;
                    const float horiz = std::sqrt(d.x * d.x + d.z * d.z);
                    const int32_t jy = int32_t(int32_t(shrapRand()) - 0x8000) << 6;
                    const int32_t want = int32_t(uint32_t(int64_t(std::llround(double(std::atan2(-d.z, d.x)) / (2 * kPi) * 4294967296.0))) - l.homeYaw) + jy;
                    l.targetYaw = l.homeYaw + uint32_t(std::clamp(want, -0x20000000, 0x20000000));
                    const int32_t jp = int32_t(int32_t(shrapRand()) - 0x8000) << 6;
                    const int32_t wantP = int32_t(uint32_t(toAng(std::atan2(d.y, horiz))) - l.homePitch) + jp;
                    l.targetPitch = l.homePitch + uint32_t(std::clamp(wantP, -0x8000000, 0x8000000));
                    l.wait = 0x20;
                } else {
                    // Scan (0x425c70); seen when score · perception > a 16-bit
                    // draw: the "spotted" mark 0x418960(target team, own team)
                    // (argument order as read), the target kept for 8 thinks.
                    AiSystem::PlayerView pv;
                    pv.alive = !playerDead();
                    pv.origin = player_.origin();
                    const int mn = moveNumber(player_.move());
                    pv.moveNumber = mn >= 0 ? mn : 2;
                    pv.side = kPlayerSide;
                    pv.team = kPlayerTeam;
                    int id = -1;
                    Vec3 at{0, 0, 0};
                    const float score = ai_.lightScan(world_, l.object, o.pos, yaw, pitch, o.team, pv, id, at);
                    l.target = -1;
                    // score · perception, a quarter when AI byte +0xb4 is 0
                    // (0x425e8f..0x425eb8).
                    const float seen = score * l.perception * (l.alert == 0 ? 0.25f : 1.f);
                    if (id != -1 && seen * 65536.f > float(shrapRand())) {
                        l.target = id;
                        l.counter = 8;
                        ai_.markSpotted(id == -3 ? kPlayerTeam : world_.objects[size_t(id)].team, o.team);
                    }
                    l.wait = 0x10;
                }
            }
        }
        o.heading = fromYaw(yaw);
        o.pitch = float(pitch) / 4294967296.f * 2 * kPi;
    }
    // People onto Land Mines (any person, any team; vehicles do not run the
    // person mover). The player's own contacts go through touchPickups.
    for (size_t i = 0; i < world_.objects.size(); ++i) {
        WorldObject& m = world_.objects[i];
        if (!m.item || m.item->id != 104054 || m.hp <= 0 || m.wrecked || m.removed) continue;
        for (const auto& q : world_.objects)
            if (q.person && q.alive() && df2Touch(m, q.pos, Vec3{q.pos.x, q.base, q.pos.z})) {   // base = origin + (frame +0x44 << 8)
                mineContact(int(i));
                break;
            }
    }
}

// 0x46cb64: a blast (effect 3, blast type 1, sound 0x32) and smoke
// generator, then n shrapnel rounds (0x44d730) in two groups: yaw heading +
// a random turn, pitch rand<<15 (0..180 degrees) for the first half and
// rand<<16 (a full turn) for the second; speed ((rand*2) | 0x100) * 20 >> 6
// (16.16 m per tick, up to 18.75 m/s); flags 0x20, 400 ticks, power 1,
// falloff 64. Barrels n = 10, vehicles 60, helicopters 40 + 10 + 10.
void Game::detonate(Vec3 at, int shrapnel, float heading) {
    explode(at, 0);
    if (world_.df2Rules) {
        shrapSeed();   // DF2 0x412b00
        ai_.noise(at, 200.f, 0, 0);   // 0x412b5c: +-200 m, flags 0
    }
    fragmentBurst(at, shrapnel, heading);
}

void Game::fragmentBurst(Vec3 at, int shrapnel, float heading) {
    for (int group = 0; group < 2; ++group)
        for (int k = 0; k < shrapnel / 2; ++k) {
            const uint32_t yawBits = uint32_t(int64_t(std::lround(heading / (2 * kPi) * 4294967296.0))) + (shrapRand() << 16);
            const uint32_t pitchBits = shrapRand() << (group ? 16 : 15);
            const float yaw = float(yawBits) / 4294967296.f * 2 * kPi;
            const float pitch = float(pitchBits) / 4294967296.f * 2 * kPi;
            this->shrapnel(at, yaw, pitch, 20);
        }
}

void Game::spawnImpact(const Impact& hit) {
    if (hit.explosive && hit.armed) {
        const int surface = hit.object == -2 ? 2 : hit.object == -1 ? 0 : 1;
        explode(hit.pos, surface, hit.law && surface != 2);
        return;
    }
    // An M203 round before it is armed (0x44f3e0): like a bullet on the
    // ground or water; effect 5 on an object or person.
    if (hit.explosive && (hit.object >= 0 || hit.object == -3)) {
        spawnEffect(5, hit.pos);
        return;
    }
    // Terrain (0x46a054): surface 1..6 -> dirt, grass, snow, stone, sand,
    // heavy dirt. People (0x44f9e3): flesh. Objects: by the hit material's
    // +0x12 bits (metal, wood, stone, foliage; armour, cloth), else dirt.
    // Impact sounds (0x46a054 terrain by surface; objects by the hit
    // material; people flesh; water 0x3b).
    {
        int id = 0;
        const bool shrapnel = hit.flags & 0x20;
        if (hit.object == -2) id = SoundSystem::BulletWater;
        else if (hit.object == -3) id = SoundSystem::BulletFlesh;   // the player
        else if (hit.object < 0) {
            static const int bySurface[7] = {0, SoundSystem::BulletDirt, SoundSystem::BulletGrass, SoundSystem::BulletSand,
                                             SoundSystem::BulletStone, SoundSystem::BulletSand, SoundSystem::BulletDirt};
            id = bySurface[std::clamp(terrain_.surfaceAt(hit.pos.x, hit.pos.z), 0, 6)];
        } else {
            const auto& o = world_.objects[size_t(hit.object)];
            if (o.person) id = SoundSystem::BulletFlesh;
            else if (o.item && (o.item->attrib & 0x40)) id = SoundSystem::BulletFlesh;   // `flesh` items
            else {
                // 0x44f90a: the hit face's material, first match: 0x10000
                // metal, 0x20000 wood, 0x40000 stone, 0x80000 bush,
                // 0x04000000 armour, 0x08000000 canvas (shrapnel variants
                // for stone and armour), none: no sound.
                const uint32_t f = hit.material;
                id = (f & 0x10000) ? SoundSystem::BulletMetal : (f & 0x20000) ? SoundSystem::BulletWood
                   : (f & 0x40000) ? (shrapnel ? SoundSystem::ShrapnelStone : SoundSystem::BulletStone)
                   : (f & 0x80000) ? SoundSystem::BulletBush
                   : (f & 0x04000000) ? (shrapnel ? SoundSystem::ShrapnelArmor : SoundSystem::BulletArmor)
                   : (f & 0x08000000) ? SoundSystem::BulletCanvas : 0;
            }
        }
        if (id) playSound(id, hit.pos);
    }
    int slot = 0;
    if (hit.object == -2) slot = 2;   // water: "water splash" (0x44fc20)
    else if (hit.object == -3) slot = 16;   // the player: flesh
    else if (hit.object < 0) {
        static const int bySurface[7] = {0, 5, 7, 9, 13, 8, 6};
        slot = bySurface[std::clamp(terrain_.surfaceAt(hit.pos.x, hit.pos.z), 0, 6)];
    } else {
        const auto& o = world_.objects[size_t(hit.object)];
        if (o.person || (o.item && (o.item->attrib & 0x40))) slot = 16;
        else {
            const uint32_t f = hit.material;
            slot = (f & 0x10000) ? 11 : (f & 0x20000) ? 10 : (f & 0x40000) ? 13 : (f & 0x80000) ? 15 : (f & 0x04000000) ? 12
                 : (f & 0x08000000) ? 14 : 5;
        }
    }
    spawnEffect(slot, hit.pos);
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
    if (const std::string* t = campText_.find("Triggered Text", key)) pushMessage(*t);
}

void Game::radio(int id) {
    // 0x473ee0: the dialog joins the queue (16 at most; more are dropped).
    char name[16];
    std::snprintf(name, sizeof name, "dlg%03d", id);
    if (const auto* parts = dialogs_.find(name); parts && !parts->empty() && activeDialogs_.size() < 16)
        activeDialogs_.push_back({parts, 0, 0});
}

// Per 60 Hz tick: each queued dialog plays its lines in order (all queued
// dialogs advance at once); a line shows its subtitle as it starts
// (0x472f7c -> 0x42f400) and the next follows the line's length plus its
// gap byte x 62 / 10 ticks.
void Game::tickDialogs() {
    for (auto& d : activeDialogs_) {
        if (d.wait > 0 && --d.wait > 0) continue;
        if (d.next >= int(d.parts->size())) { d.next = -1; continue; }
        const DialogPart& p = (*d.parts)[size_t(d.next++)];
        sound_.playDialogLine(p.clip);
        if (p.subtitle >= 0 && p.subtitle < int(dialogText_.size()) && !dialogText_[size_t(p.subtitle)].empty())
            pushMessage(dialogText_[size_t(p.subtitle)]);
        d.wait = sound_.dialogLineTicks(p.clip) + p.gap * 62 / 10 + 1;
    }
    activeDialogs_.erase(std::remove_if(activeDialogs_.begin(), activeDialogs_.end(), [](const ActiveDialog& d) { return d.next < 0; }),
                         activeDialogs_.end());
}

void Game::endMission(int winnerSide) {
    if (missionResult_ < 0) missionResult_ = winnerSide;
}

void Game::handleKey(const SDL_KeyboardEvent& k) {
    const bool shift = (k.keysym.mod & KMOD_SHIFT) != 0;
    if (missionResult_ >= 0) {
        // "Hit Enter to end mission." / "Hit Space Bar to replay mission".
        if (k.keysym.sym == SDLK_RETURN || k.keysym.sym == SDLK_KP_ENTER) {
            leaveMission_ = true;   // exit code 1: to the Post Menu / debriefing
            return;
        }
        if (k.keysym.sym == SDLK_SPACE) { loadMission(current_); return; }
    }
    // DF2's command-map name prompt takes the keyboard while it is open.
    if (df2Hud_.on && showMap_ && df2Map_.prompt) { df2MapKey(k); return; }
    // Debug keys (not in the original): N free flight (Ctrl+N in DF2, where N
    // is the night vision).
    if (k.keysym.sym == SDLK_n && (!df2Hud_.on || (k.keysym.mod & KMOD_CTRL))) { player_.fly = !player_.fly; return; }
    (void)shift;
    // Briefing scrolling (0x42e938 -> 0x481360): a page at a time.
    if (briefingShown_ && (k.keysym.sym == SDLK_PAGEDOWN || k.keysym.sym == SDLK_PAGEUP)) {
        if (k.keysym.sym == SDLK_PAGEDOWN) {
            if (briefStart_ != briefLast_) {
                briefPage_ = std::max(briefPage_, briefLast_ - briefStart_);
                briefStart_ = briefLast_;
            }
        } else {
            briefStart_ = std::max(1, briefStart_ - briefPage_);
        }
        return;
    }
    const Action act = controls_.actionFor(k.keysym.scancode);
    // While paused, the game's commands (flag 0x10000) are ignored (0x42d9e3).
    if (paused_ && act != Action::Pause && act != Action::Escape) return;
    switch (act) {
    case Action::Escape:
        // Esc closes the first overlay open (0x42e964): pause, old messages,
        // map / goals; else (OpenDF) frees the mouse or quits.
        if (paused_) { paused_ = false; break; }
        // DF2 0x41b59d: with [0x7655fc] set (F9 / F10), Escape only clears it.
        if (df2Hud_.on && df2KeyFlag_) { df2KeyFlag_ = false; break; }
        if (oldMessages_) { oldMessages_ = false; break; }
        if (briefingShown_) { briefingShown_ = false; break; }
        if (helpShown_) { helpShown_ = false; break; }
        if (showMap_ || showGoals_) { showMap_ = showGoals_ = false; break; }
        if (captured_) { captured_ = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
        else leaveMission_ = true;   // (OpenDF) leave the mission
        break;
    case Action::Stand: player_.stance = Stance::Stand; break;
    case Action::Crouch: player_.stance = Stance::Crouch; break;
    case Action::Prone: player_.stance = Stance::Prone; break;
    // Weapon keys select the original's slots (DF.EXE 0x42de85 -> 0x44aa74):
    // 0 laser designator, 1 knife (neither done yet), 2 sidearm, 3-5 the
    // primary's modes, 6-8 the kit's, 9 frag grenade.
    case Action::Weapon0: arsenal_.select(0); break;
    case Action::Weapon1: arsenal_.select(1); break;
    case Action::Weapon2: arsenal_.select(2); break;
    case Action::Weapon3: arsenal_.select(3); break;
    case Action::Weapon4: arsenal_.select(4); break;
    case Action::Weapon5: arsenal_.select(5); break;
    case Action::Weapon6: arsenal_.select(6); break;
    case Action::Weapon7: arsenal_.select(7); break;
    case Action::Weapon8: arsenal_.select(8); break;
    case Action::Weapon9: arsenal_.select(9); break;
    case Action::Cycle: arsenal_.cycle(); break;
    case Action::Magazine: arsenal_.reload(); break;
    case Action::Scope: arsenal_.toggleZoom(); break;
    case Action::NextWaypoint:
        if (df2Hud_.on) df2NextWaypoint();
        else if (!route_.empty()) routeIndex_ = (routeIndex_ + 1) % int(route_.size());
        break;
    // DF2 0x41bdf4 (mapterrain, F9): elevation on -> off and the flag set,
    // else the flag toggled. 0x41be2e (mapelevation, F10): elevation on ->
    // the flag toggled (elevation stays on), else the flag set and elevation
    // on. The map's D button keeps its picture until the tab is entered again.
    case Action::MapTerrain:
        if (df2Elevation_) { df2Elevation_ = false; df2KeyFlag_ = true; }
        else df2KeyFlag_ = !df2KeyFlag_;
        break;
    case Action::MapElevation:
        if (df2Elevation_) df2KeyFlag_ = !df2KeyFlag_;
        else { df2KeyFlag_ = true; df2Elevation_ = true; }
        break;
    case Action::Pause: paused_ = !paused_; break;
    case Action::Drop: if (!playerDead()) dropCarried(); break;
    case Action::Briefing: briefingShown_ = !briefingShown_; break;
    case Action::Help: helpShown_ = !helpShown_; break;
    case Action::OldMessages: oldMessages_ = !oldMessages_; break;
    case Action::ShowFriendly:
        friendlyTags_ = !friendlyTags_;
        pushMessage(friendlyTags_ ? "Friendly Tags: On" : "Friendly Tags: Off");
        break;
    case Action::LetterBox: letterbox_ = letterbox_ == 1 ? 2 : 1; break;
    case Action::ScopeInc: arsenal_.zeroStep(1); break;    // DF2 0x445ff0 (X)
    case Action::ScopeDec: arsenal_.zeroStep(-1); break;   // (Z)
    case Action::NightVision: if (df2Hud_.on) nvg_ = !nvg_; break;   // DF2 action 51, [0x765740]
    case Action::Binoculars:
        // DF2.EXE 0x41b6e3: the binoculars are slot 10 (mode 47); their view
        // follows the raised move, set each tick (Game::run, 0x45c7e2).
        if (arsenal_.df2()) { arsenal_.toggleBinoculars(); break; }
        binoculars_ = !binoculars_ && !playerDead();
        if (binoculars_) pickBinocularOffset();
        arsenal_.setBinoculars(binoculars_);
        break;
    case Action::BigMap:
        showMap_ = !showMap_;
        showGoals_ = false;
        // DF2's command map takes the mouse (its own cursor is drawn).
        if (df2Hud_.on && captured_) {
            SDL_SetRelativeMouseMode(showMap_ ? SDL_FALSE : SDL_TRUE);
            SDL_ShowCursor(SDL_DISABLE);
        }
        break;
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
    const Vec3 eye = viewEye();
    // Terrain rings scale with the original's focal length: F = 320 per 640
    // pixels of its display mode, or the scope view's own focal length.
    terrain_.ringScale = zoomView ? scopeFocal_ / 320.f : float(hudMode_ ? hudMode_ : 640) / 640.f;
    terrain_.df2FovDeg = binoculars_ && !playerDead() ? 22.f : 90.f;   // view+0x4c (0x40423d binoculars)
    terrain_.draw(view, proj, eye, viewDistance_);
    // Delta Force 2: the water before the objects while the camera is above
    // it (DF2.EXE 0x4489a9), after them below it (0x4489c6).
    const bool waterFirst = terrain_.df2Water() && eye.y > terrain_.waterLevel();
    if (waterFirst) terrain_.drawWater(view, proj, eye, float(ticks_));
    // Objects (DF.EXE 0x44badc / 0x4414b8 / 0x46fd94): never fogged; culled
    // beyond 1020 m (either horizontal axis, or view depth + radius); LOD by
    // projected radius F·R/depth against the model's thresholds.
    const float focal = 320.f * terrain_.ringScale;
    SceneLighting light;
    light.lut = terrain_.lutTexture();
    light.saturation = terrain_.saturationFactor();
    light.highlight = terrain_.highlight();
    const Vec3 fwd = viewDir();
    light.viewer = fwd * -1.f;
    models_.begin(proj * view, light);
    struct Visible { const WorldObject* o; Vec3 at; int lod; };
    std::vector<Visible> visible;
    for (const auto& o : world_.objects) {
        if (o.removed) continue;
        Vec3 at = world_.nearest(o.pos, eye);
        Vec3 d = at - eye;
        if (std::abs(d.x) > 1020.f || std::abs(d.z) > 1020.f) continue;
        const float depth = dot(d, fwd);
        if (depth < -o.model->radius || depth + o.model->headerRadius > 1020.f) continue;
        const int lod = ModelRenderer::chooseLod(*o.model, std::max(depth, 0.01f), focal, o.person);
        if (lod < 0) continue;
        visible.push_back({&o, at, lod});
    }
    for (auto& v : visible) {
        if (v.lod < 4 && v.o->model->lodCodes[v.lod] == "helo" && v.lod < int(v.o->model->lods.size()) && v.o->model->points.size() >= 3) {
            // 0x478114: mesh 0 and 6 static; mesh 1 (blades) or 2 (disc) at
            // rotor speed >= 0x0C000000, about file +Z through hardpoint 0;
            // mesh 3 or 4 about file +Y through hardpoint 2; mesh 5 (the
            // gun) about hardpoint 4 by its yaw (Z) then pitch (Y).
            // Hardpoints of LOD 0 serve every LOD (they match in BLACKHWK
            // and GAZELLE).
            const GpuModel& m = *v.o->model;
            const size_t meshes = m.lods[size_t(v.lod)].meshParent.size();
            const bool fast = v.o->rotorSpeed >= float(0x0C000000);
            const Mat4 hide = [] { Mat4 z; for (int i = 0; i < 16; ++i) z.m[i] = 0; return z; }();
            auto about = [&](int hp, const Mat4& r) {
                const auto& p = m.points[size_t(hp)];
                return translate({p[0], p[1], p[2]}) * r * translate({-p[0], -p[1], -p[2]});
            };
            // File +Z (up) is OpenDF +Y; file +Y (left) is OpenDF -Z.
            const float main = float(v.o->rotorAngle[0]) / 4294967296.f * 2 * kPi;
            const float tail = float(v.o->rotorAngle[1]) / 4294967296.f * 2 * kPi;
            Mat4 rz;
            { const float c = std::cos(tail), s = std::sin(tail); rz.at(0, 0) = c; rz.at(0, 1) = -s; rz.at(1, 0) = s; rz.at(1, 1) = c; }
            std::vector<Mat4> pose(meshes, Mat4{});
            if (meshes > 1) pose[1] = fast ? hide : about(0, rotateY(main));
            if (meshes > 2) pose[2] = fast ? about(0, rotateY(main)) : hide;
            if (meshes > 3) pose[3] = fast ? hide : about(2, rz);
            if (meshes > 4) pose[4] = fast ? about(2, rz) : hide;
            if (meshes > 5 && m.points.size() > 4) {
                Mat4 gp;
                const float c = std::cos(v.o->gunPitch), sn = std::sin(v.o->gunPitch);
                gp.at(0, 0) = c; gp.at(0, 1) = -sn; gp.at(1, 0) = sn; gp.at(1, 1) = c;
                pose[5] = about(4, rotateY(v.o->gunYaw) * gp);
            }
            models_.drawPosed(m, v.lod, v.o->matrix(v.at), pose);
            continue;
        }
        if (v.lod < 4 && v.o->model->lodCodes[v.lod] == "tire" && v.lod < int(v.o->model->lods.size()) && v.o->model->points.size() >= 3) {
            // 0x4808fc: mesh 0 as is, mesh 1 (shadow) not drawn, front wheels
            // mesh 2 about hardpoint 2 and mesh 3 about hardpoint 1, turned
            // about the vertical by the steering angle; no rolling. (The
            // hardpoints are used as DF.EXE uses them, including the data's
            // odd pivots.)
            const GpuModel& m = *v.o->model;
            const size_t meshes = m.lods[size_t(v.lod)].meshParent.size();
            Mat4 hide;
            for (float& x : hide.m) x = 0;
            auto about = [&](int hp, const Mat4& r) {
                const auto& p = m.points[size_t(hp)];
                return translate({p[0], p[1], p[2]}) * r * translate({-p[0], -p[1], -p[2]});
            };
            // DF yaw is anticlockwise, OpenDF heading clockwise.
            const Mat4 steer = rotateY(-v.o->steer);
            std::vector<Mat4> pose(meshes, Mat4{});
            if (meshes > 1) pose[1] = hide;
            if (meshes > 2) pose[2] = about(2, steer);
            if (meshes > 3) pose[3] = about(1, steer);
            models_.drawPosed(m, v.lod, v.o->matrix(v.at), pose);
            continue;
        }
        if (v.o->chr && !v.o->anim.angles().empty()) {
            const auto pose = personPose(*v.o->model, v.lod, *v.o->chr, v.o->anim.angles(), v.o->dead ? 0.f : v.o->aimPitch);
            const Mat4 body = v.o->matrix(v.at);
            models_.drawPosed(*v.o->model, v.lod, body, pose);
            // DF2: the parachute over a falling person (0x4153b0): `parachut`
            // for PFoil items (attrib 0x400), else `pararnd`, same transform and LOD.
            if (v.o->chute)
                if (const ItemDef* ci = items_.byId(v.o->item && (v.o->item->attrib & 0x400) ? 107037 : 107038); ci && !ci->graphic.empty())
                    if (const GpuModel* cm = models_.get(vfs_, ci->graphic)) models_.draw(*cm, std::min(v.lod, int(cm->lods.size()) - 1), body);
            // The held weapon (0x44cc34): the mode's third-person item (+0x68),
            // its LOD's first mesh, at the right hand (mesh 7) turned by the
            // fixed offset 0x44d3c0 (-30, -65, -30 degrees as yaw, pitch, roll).
            if (const int item = arsenal_.heldItemAt(v.o->weaponMode); item > 0 && pose.size() > 7 && !v.o->dead)
                if (const ItemDef* wi = items_.byId(100000 + item); wi && !wi->graphic.empty())
                    if (const GpuModel* wm = models_.get(vfs_, wi->graphic); wm && !wm->lods.empty()) {
                        const Mat4& offset = heldWeaponOffset();
                        const auto& l = v.o->model->lods[size_t(std::clamp(v.lod, 0, int(v.o->model->lods.size()) - 1))];
                        const auto& pv = l.meshPivot[7];
                        const Mat4 hand = pose[7] * translate({pv[0], pv[1], pv[2]});
                        models_.draw(*wm, std::min(v.lod, int(wm->lods.size()) - 1), body * hand * offset);
                    }
            continue;
        }
        models_.draw(*v.o->model, v.lod, v.o->matrix(v.at));
    }
    for (auto& v : visible)
        if (!(v.o->chr && !v.o->anim.angles().empty())) models_.drawTranslucent(*v.o->model, v.lod, v.o->matrix(v.at));
    // The dead player's body, seen from the orbit camera: the soldier's
    // model posed by the death animation DF.EXE chose (0x46c920).
    if (playerDead() && !playerBody_.angles().empty())
        if (const ItemDef* it = items_.byId(playerModelItem_ ? playerModelItem_ : playerItem_); it && !it->graphic.empty())
            if (const GpuModel* bm = models_.get(vfs_, it->graphic); bm && !bm->lods.empty()) {
                WorldObject body;
                body.model = bm;
                body.pos = deadBodyOrigin();
                body.heading = deadYaw_;
                models_.drawPosed(*bm, 0, body.matrix(world_.nearest(body.pos, eye)), personPose(*bm, 0, playerChr_, playerBody_.angles()));
            }
    // Effects: billboards facing the camera (yaw only), scaled, at their frame.
    const float camYaw = std::atan2(fwd.x, -fwd.z);
    for (const auto& e : effects_) {
        const Vec3 at = world_.nearest(e.pos, eye);
        const Vec3 d = at - eye;
        if (std::abs(d.x) > 1020.f || std::abs(d.z) > 1020.f || dot(d, fwd) < 0.5f) continue;
        if (e.canopy) {
            WorldObject body;
            body.heading = e.heading;
            body.pitch = e.pitch;
            models_.draw(*e.model, 0, body.matrix(at));
            continue;
        }
        if (e.smk2) {
            // The efct draw 0x41f740: facing the camera (yaw camera + 0x7fffffff,
            // pitch 0x80000000 - camera pitch), rolled by +0x38 (the third
            // angle, about model x, applied first: 0x42bc00 -> 0x42ade0(.., 0,
            // ..)), uniform scale
            // +0x84 (0x42bc00), frame (age / ticks per frame) % frames
            // (0x41f7a2..0x41f7ca), in the colour +0x80 (0x445e50, the vertex
            // colour, replacing the material colour +0x14 when its RGB is
            // non-zero, 0x4603bf..0x4603d6) with alpha +0x7c: opaque while the alpha is over 255, else
            // max(alpha >> 16, 1) / 255, so it fades out.
            const int frame = (e.age / std::max(1, e.ticksPerFrame)) % std::max(1, e.frames);
            Mat4 m = translate(at) * rotateY(kPi * 0.5f - (camYaw + kPi));
            Mat4 tilt;
            const float cp = std::asin(std::clamp(fwd.y, -1.f, 1.f)), c = std::cos(-cp), sn = std::sin(-cp);
            tilt.at(0, 0) = c; tilt.at(0, 1) = -sn; tilt.at(1, 0) = sn; tilt.at(1, 1) = c;
            m = m * tilt * rotateX(float(uint32_t(e.rot)) / 4294967296.f * 2 * kPi);
            Mat4 sc;
            sc.at(0, 0) = sc.at(1, 1) = sc.at(2, 2) = e.size / 65536.f;
            m = m * sc;
            const float a = e.alpha > 0xff0000 ? 1.f : float(std::max(e.alpha >> 16, 1)) / 255.f;
            models_.setTint(e.tint[0] / 255.f, e.tint[1] / 255.f, e.tint[2] / 255.f, a, true, e.fireball);
            glEnable(GL_BLEND);
            // The fireballs set 0x445e60(1), polygon flag 0x800 (0x4457ed):
            // the software spans for it (0x45eac0 -> 0x484ffd) add texel ·
            // colour to the screen with saturation (paddusb, 0x4850a1):
            // additive. (The Direct3D back end [0xa7b6b4] is outside DF2.EXE.)
            // Their colours skip the scene grade (0x42abc0 has no 0x411f70).
            glBlendFunc(GL_SRC_ALPHA, e.fireball ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            models_.draw(*e.model, 0, m, false, false, frame);
            models_.drawTranslucent(*e.model, 0, m, false, false, frame);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            models_.setTint(1, 1, 1, 1);
            continue;
        }
        const int frame = std::min(e.age / std::max(1, e.ticksPerFrame), e.frames - 1);
        // Model +X faces the heading; the quad's normal is +X, turned to the camera.
        Mat4 m = translate(at) * rotateY(kPi * 0.5f - (camYaw + kPi));
        // Puffs (efct draw 0x480c70 with +0x3C set) also take the camera's
        // pitch, so +X lies along the view axis and the roll (1/256 turn a
        // tick) turns them in the screen plane.
        if (e.smoke) {
            Mat4 tilt;
            const float cp = std::asin(std::clamp(fwd.y, -1.f, 1.f)), c = std::cos(-cp), sn = std::sin(-cp);
            tilt.at(0, 0) = c; tilt.at(0, 1) = -sn; tilt.at(1, 0) = sn; tilt.at(1, 1) = c;
            m = m * tilt * rotateX(e.roll * 2 * kPi);
        }
        Mat4 sc;
        sc.at(0, 0) = sc.at(1, 1) = sc.at(2, 2) = e.scale;
        m = m * sc;
        models_.draw(*e.model, 0, m, false, false, frame);
        models_.drawTranslucent(*e.model, 0, m, false, false, frame);
    }
    // Rounds drawn in flight (0x44d96d): gun flag 0x10 the "grenade" item
    // 4033, 0x01 the M203's 4001 (m406), 0x40 the LAW's 4042 (law_rckt);
    // at the launch yaw and pitch for the whole flight (gnrc draw:
    // T * Rz(yaw) * Ry(pitch), the record's angles never change).
    for (const auto& p : arsenal_.projectiles()) {
        const int id = (p.flags & 0x10) ? 104033 : (p.flags & 0x40) ? 104042 : (p.flags & 0x01) ? 104001 : 0;
        const ItemDef* gi = id ? items_.byId(id) : nullptr;
        const GpuModel* gm = gi && !gi->graphic.empty() ? models_.get(vfs_, gi->graphic) : nullptr;
        if (!gm) continue;
        const Vec3 at = world_.nearest(p.pos, eye);
        Mat4 pitch;
        const float c = std::cos(p.pitch), sn = std::sin(p.pitch);
        pitch.at(0, 0) = c; pitch.at(0, 1) = -sn; pitch.at(1, 0) = sn; pitch.at(1, 1) = c;
        models_.draw(*gm, 0, translate(at) * rotateY(kPi * 0.5f - p.yaw) * pitch);
    }
    models_.end();
    // Water last: it tints what is under it (DF.EXE 0x43bfd8).
    if (!waterFirst) terrain_.drawWater(view, proj, eye, float(ticks_));
    // Delta Force 2's fog pass (0x4485b0, called at 0x44892a after the
    // objects), then the rain and snow, unfogged (0x448930 -> 0x46b780).
    terrain_.drawFog(proj);
    drawWeather(view, proj, eye);
}

// DF2.EXE 0x46b780, drawn only by the software frame (0x448930, after the
// fog pass, so unfogged); the D3D frame 0x448980 has no call to it, so in
// D3D mode DF2 drew no rain or snow (the simulation and the splashes still
// ran). OpenDF draws what the software renderer showed.
void Game::drawWeather(const Mat4& view, const Mat4& proj, Vec3 eye) {
    if (!weather_.type()) return;
    if (weather_.type() == 1) {
        weather_.drawRain(proj * view, eye, terrain_.mapExtent(), int32_t(arsenal_.windX()), int32_t(-arsenal_.windZ()));
        return;
    }
    // Snow: item 0x1b6a = 107018 "snow" (SNOW.3DI), facing the camera (yaw
    // view+0x10 + 0x7fffffff, pitch 0x80000000 - view+0x14), at scale 2.0
    // (0x42bc00 with 0x20000), untinted; not drawn without a model. The
    // per-flake visibility test 0x404a20 (view, pos, 0x200): a sphere of
    // r = 1/128 m against the view frustum (planes 0x404784..0x40485e: near
    // at 1/16 m, the four side planes of the view's field of view) and depth
    // + r <= 0xff00000 (4080 m); 1 = skip, 2 (partly in) is still drawn.
    const ItemDef* item = items_.byId(107018);
    const GpuModel* m = item && !item->graphic.empty() ? models_.get(vfs_, item->graphic) : nullptr;
    if (!m || m->lods.empty()) return;
    const Vec3 fwd = viewDir();
    const float camYaw = std::atan2(fwd.x, -fwd.z);
    Mat4 face = rotateY(kPi * 0.5f - (camYaw + kPi));
    {
        Mat4 tilt;
        const float cp = std::asin(std::clamp(fwd.y, -1.f, 1.f)), c = std::cos(-cp), sn = std::sin(-cp);
        tilt.at(0, 0) = c; tilt.at(0, 1) = -sn; tilt.at(1, 0) = sn; tilt.at(1, 1) = c;
        face = face * tilt;
    }
    Mat4 sc;
    sc.at(0, 0) = sc.at(1, 1) = sc.at(2, 2) = 2.f;
    face = face * sc;
    SceneLighting light;
    light.lut = terrain_.lutTexture();
    light.saturation = terrain_.saturationFactor();
    light.highlight = terrain_.highlight();
    light.viewer = fwd * -1.f;
    models_.begin(proj * view, light);
    weather_.forEachFlake([&](Vec3 p) {
        const Vec3 at = world_.nearest(p, eye);
        {
            constexpr float r = 1.f / 128.f;
            const float vx = view.at(0, 0) * at.x + view.at(0, 1) * at.y + view.at(0, 2) * at.z + view.at(0, 3);
            const float vy = view.at(1, 0) * at.x + view.at(1, 1) * at.y + view.at(1, 2) * at.z + view.at(1, 3);
            const float depth = -(view.at(2, 0) * at.x + view.at(2, 1) * at.y + view.at(2, 2) * at.z + view.at(2, 3));
            if (depth - 1.f / 16.f < -r || depth + r > float(0xff00000) / 65536.f) return;   // near plane, 0x404a96
            // Side planes through the eye at the half field of view (tan = 1 / proj focal).
            const float th = std::atan(1.f / proj.at(0, 0)), tv = std::atan(1.f / proj.at(1, 1));
            if (depth * std::sin(th) - std::abs(vx) * std::cos(th) < -r) return;
            if (depth * std::sin(tv) - std::abs(vy) * std::cos(tv) < -r) return;
        }
        const Mat4 mm = translate(at) * face;
        models_.draw(*m, 0, mm);
        models_.drawTranslucent(*m, 0, mm);
    });
    models_.end();
}

void Game::drawViewModel(const Screen& s) {
    // The original hides the weapon while the zoom inset is up.
    // Nor is it drawn once the player is dead (0x4042df).
    if (!gunVisible_ || scopeShown() || showMap_ || playerDead() || binoculars_) return;   // 0x4042ff
    const GpuModel* m = models_.get(vfs_, arsenal_.mode().viewModel);
    if (!m) return;
    glClear(GL_DEPTH_BUFFER_BIT);

    // As in the original engine: the P* model is drawn unrotated (aligned with
    // the view) at the weapon's offset from DF.EXE's player weapon table, with
    // the same camera as the world. Offset axes are the model's own: forward,
    // left, up in 1/256 m.
    const int* g = arsenal_.mode().offset;
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
    Mat4 proj = perspective(viewFovY_ > 0 ? viewFovY_ : kOriginalFovY, s.w / s.playH, 0.02f, 10.f);
    // The world light (DF.EXE 0x44bea4) rotated into view space, as the gun
    // is drawn there; the viewer looks along -Z.
    SceneLighting light;
    {
        const Mat4 rot = lookDir({0, 0, 0}, aimDirection(), {0, 1, 0});
        const Vec3 L = light.light;
        light.light = {rot.at(0, 0) * L.x + rot.at(0, 1) * L.y + rot.at(0, 2) * L.z,
                       rot.at(1, 0) * L.x + rot.at(1, 1) * L.y + rot.at(1, 2) * L.z,
                       rot.at(2, 0) * L.x + rot.at(2, 1) * L.y + rot.at(2, 2) * L.z};
        light.viewer = {0, 0, 1};
        light.lut = terrain_.lutTexture();
        light.saturation = terrain_.saturationFactor();
        light.highlight = terrain_.highlight();
    }
    models_.begin(proj, light);
    models_.draw(*m, 0, model, true, arsenal_.flash() > 0);
    models_.drawTranslucent(*m, 0, model, true, arsenal_.flash() > 0);
    models_.end();
}

// The zoom view is a magnified circle on the right of the 3D view (the main
// view stays as it is), like the original.
void Game::drawZoomInset(const Screen& s, const Mat4& view, float fovY) {
    if (!scopeShown() || showMap_) return;
    // Delta Force 2 (0x44deb0): centre (0.75 W, H / 2) of the screen, r = W·5/32.
    const float cx = s.cx(160), cy = df2Hud_.on ? s.h * 0.5f : s.playY + s.playH * 0.5f, r = s.y(100);

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
    // DF2.EXE 0x44e0a0: the scope camera's pitch lowered by the zeroing
    // elevation (the shot keeps the player's pitch, so the bore rises).
    drawWorld(df2Hud_.on ? rotateX(arsenal_.zeroElevation()) * view : view, perspective(insetFov, 1.f, 0.1f, viewDistance_ * 1.1f), true);
    glDisable(GL_STENCIL_TEST);
    glViewport(0, 0, int(s.w), int(s.h));
    // DF2.EXE 0x44e2a0: on night missions the scope image is green only, ×4.
    if (df2Hud_.on && (mission_.lightFlags & 0x10)) {
        hud_.begin(int(s.w), int(s.h));
        hud_.nightVision(cx, cy, r, 1.f);
        hud_.end();
    }
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

    // Weapon name (0x483444) at (75, 10); red while reloading or with no
    // ammunition left at all (0x44b0a0 / 0x44b164).
    const ModeDef& md = arsenal_.mode();
    // Red (0x44b074): types 7 / 9 also while the cooldown runs; 8 never; 10
    // with no fire missions left.
    const bool outOfAmmo = md.type == 8 ? false
                         : md.type == 10 ? fireMissions_ <= 0
                         : (arsenal_.spares() <= 0 && arsenal_.rounds() <= 0 && !arsenal_.chambered()) ||
                               ((md.type == 7 || md.type == 9) && arsenal_.firing());
    hud_.text(s.lx(75), s.y(10), arsenal_.modeName(), arsenal_.reloading() || outOfAmmo ? kHudRed : kHudGreen, ts);

    // Ammunition (0x483447, by fire type, jump table 0x483418), y 28.75-47.5:
    // guns: spare magazines with the magazine glyph (font character 1) at
    // x 75-135, rounds in the magazine at 140-200, the "round chambered"
    // bar at 205-210 (solid when chambered, else an outline); launchers and
    // grenades: the rounds left at 75-135 and the bar at 140-145.
    auto chamberBar = [&](float x) {
        if (arsenal_.chambered()) hud_.rect(s.lx(x), s.y(28.75f), s.lx(x + 5) - s.lx(x), s.y(47.5f) - s.y(28.75f), kHudGreen);
        else hudBox(s, x, 28.75f, x + 5, 47.5f, false);
    };
    auto countBox = [&](float x, int value, bool glyph) {
        hudBox(s, x, 28.75f, x + 60, 47.5f, false);
        if (glyph) hud_.text(s.lx(x + 3.1f), s.y(31.9f), "\x01", kHudGreen, ts);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d", value);
        hud_.text(s.lx(x + 30) - hud_.textWidth(buf, ts) / 2, s.y(31.9f), buf, kHudGreen, ts);
    };
    if (md.type == 1) {
        countBox(75, arsenal_.spares(), true);
        countBox(140, arsenal_.rounds(), false);
        chamberBar(205);
    } else if (md.type == 2 || md.type == 7 || md.type == 9 || md.type == 11) {
        countBox(75, arsenal_.spares(), false);
        chamberBar(140);
    } else if (md.type == 6) {
        chamberBar(75);   // knife: the ready bar only (0x48353d)
    } else if (md.type == 10) {
        countBox(75, fireMissions_, false);   // fire missions left (0x483523, [0x933690])
    }

    // Weapon silhouette (0x484ae8): WEAP800.PCX, 13 frames, chosen per mode,
    // drawn 1:1 with its right edge at x 325 and top at y 16.25.
    if (weaponIcons_.texture && md.icon >= 0) {
        const int frame = md.icon;
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
    {
        // 0x48375b: the carried item's name by id.
        std::string what = "Nothing";
        if (carried_ >= 0 && world_.objects[size_t(carried_)].item) {
            switch (world_.objects[size_t(carried_)].item->id - 100000) {
            case 4008: what = "Metal Case"; break;
            case 4018: what = "Case"; break;
            case 4021: what = "Black Box"; break;
            case 4034: what = "Laptop"; break;
            case 4035: what = "Red Flag"; break;
            case 4036: what = "Blue Flag"; break;
            case 4038: what = "Code Book"; break;
            case 4077: what = "Bomb!"; break;
            default: what = "Unknown"; break;
            }
        }
        hud_.text(s.rx(490.6f), s.y(13.1f), "Carrying: " + what, kHudGreen, ts);
    }
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
        hud_.text(s.lx(6.25f), s.by(430 + 15 * float(i)), messages_[i].text, messages_[i].colour, ts);   // entry +0x78 (0x4833e2)

    // Soldier name and mission name (0x484e54) at x 437.5, y 426.25 / 441.9.
    hud_.text(s.rx(437.5f), s.by(426.25f), o_.soldierName, kHudGreen, ts);
    hud_.text(s.rx(437.5f), s.by(441.9f), missionName_, kHudGreen, ts);
}

void Game::drawMapMarker(const Screen& s, float x, float y, int type, Color c, float heading, bool dead, float radius) {
    // DF.EXE 0x43e434: radius 5 virtual units, doubled on the command map.
    const float r = radius > 0 ? radius : s.y(6.25f), t = s.px();
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
    // the main cross, a small X, and a four-pointed star (0x42529c lines in
    // [0x66f94c]); N in [0x66f930] (white), E S W in [0x66f94c], the lines'
    // own colour (0x43e0c7 / 0x43e0f2 / 0x43e118 / 0x43e13e, through 0x43ddf4
    // with flag 2), black-outlined.
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

    const float cx = s.w * 0.5f, cy = s.playY + s.playH * 0.5f, t = s.px();
    if (showMap_ && df2Hud_.on) {
        // DF2.EXE 0x45c610: only the command map while it is open.
        drawDf2CommandMap(s);
        hud_.end();
        return;
    }
    if (showMap_) drawCommandMap(s);
    else if (crosshair_ && df2Hud_.on) {
        // DF2.EXE (0x45a9e0): four orange lines, gap W*320/65536 and arm
        // W*640/65536 mode pixels from the centre of the 3D rows.
        const float a = float(s.modeW * 320 / 65536) * s.art(), b = float(s.modeW * 640 / 65536) * s.art();
        hud_.rect(cx - a - b, cy, b, t, kDf2Amber);
        hud_.rect(cx + a, cy, b, t, kDf2Amber);
        hud_.rect(cx, cy - a - b, t, b, kDf2Amber);
        hud_.rect(cx, cy + a, t, b, kDf2Amber);
    } else if (crosshair_) {
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
    if (scopeShown() && !showMap_ && df2Hud_.on) {
        // DF2.EXE 0x44e2a0: a black ring; posts solid black from the edge to
        // r/2, the inner ±r/2 segments half blended with 0x000080; the range
        // "%4im" at (780, 500) in orange ("10km+", "1m-"); the zeroing "%4im"
        // (setting × 100 m, keys X/Z 0x445ff0) at (700, 500) in
        // yellow, not for the LAW; the laser designator's square of r/8.
        const float zx = s.cx(160), zy = s.h * 0.5f, r = s.y(100);
        const Color black{0.004f, 0.004f, 0.004f, 1}, navy{0, 0, 0.5f, 0.5f};
        hud_.ring(zx, zy, r, t, black);
        const bool laser = arsenal_.mode().type == 10;
        if (!laser) {
            hud_.rect(zx - r, zy, r / 2, t, black);
            hud_.rect(zx + r / 2, zy, r / 2, t, black);
            hud_.rect(zx, zy - r, t, r / 2, black);
            hud_.rect(zx, zy + r / 2, t, r / 2, black);
            hud_.rect(zx - r / 2, zy, r, t, navy);
            hud_.rect(zx, zy - r / 2, t, r, navy);
        } else {
            const float d = r / 8;
            hud_.rect(zx - d, zy - d, 2 * d, t, kDf2Amber);
            hud_.rect(zx - d, zy + d, 2 * d, t, kDf2Amber);
            hud_.rect(zx - d, zy - d, t, 2 * d, kDf2Amber);
            hud_.rect(zx + d, zy - d, t, 2 * d, kDf2Amber);
        }
        const float range = rangeToAim();
        char buf[32];
        if (laser) {
            if (range < 0 || range > 999) std::snprintf(buf, sizeof buf, "Range: 1km+");
            else std::snprintf(buf, sizeof buf, "Range: %3im", int(range));
            hud_.text(s.vx(770) - hud_.textWidth(buf, s.text()) / 2, s.vy(450), buf, kDf2Amber, s.text());
            const bool target = range >= 0 && range <= 999;
            const char* status = target ? "Laser Active" : "No Target";
            if (target || (ticks_ & 0x10))
                hud_.text(s.vx(770) - hud_.textWidth(status, s.text()) / 2, s.vy(300), status, kDf2Amber, s.text());
        } else {
            if (range < 0 || range > 9999) std::snprintf(buf, sizeof buf, "10km+");
            else if (range < 1) std::snprintf(buf, sizeof buf, "1m-");
            else std::snprintf(buf, sizeof buf, "%4dm", int(range));
            hud_.text(s.vx(780), s.vy(500), buf, kDf2Amber, s.text());
            if (arsenal_.modeIndex() != 0x1c) {
                std::snprintf(buf, sizeof buf, "%4dm", arsenal_.zeroSetting() * 100);
                hud_.text(s.vx(700), s.vy(500), buf, rgb(0xff, 0xff, 0), s.text());
            }
        }
    } else if (scopeShown() && !showMap_) {
        const float zx = s.cx(160), zy = cy, r = s.y(100);
        hud_.ring(zx, zy, r, t, kHudBright);
        const float in = r * 0.25f, out = r * 0.5f;
        if (arsenal_.mode().type != 10) {   // the laser view has no ticks (0x450cf7 is the other branch)
            hud_.rect(zx - out, zy, out - in, t, kHudBright);
            hud_.rect(zx + in, zy, out - in, t, kHudBright);
            hud_.rect(zx, zy - out, t, out - in, kHudBright);
            hud_.rect(zx, zy + in, t, out - in, kHudBright);
        }
        const float range = rangeToAim();
        char buf[32];
        if (arsenal_.mode().type == 10) {
            // Laser designator (0x4508be..0x450b4c): an axis-aligned diamond of
            // r/8; "Range: %3im" / "Range: 1km+" centred at (770, 450) and
            // "Laser Active" or a blinking "No Target" (tick bit 0x10) centred
            // at (770, 300) in the 1024x768 layout.
            const float d = r / 8.f;
            hud_.line(zx - d, zy, zx, zy - d, t, kHudBright);
            hud_.line(zx, zy - d, zx + d, zy, t, kHudBright);
            hud_.line(zx + d, zy, zx, zy + d, t, kHudBright);
            hud_.line(zx, zy + d, zx - d, zy, t, kHudBright);
            if (range < 0 || range > 999) std::snprintf(buf, sizeof buf, "Range: 1km+");
            else std::snprintf(buf, sizeof buf, "Range: %3im", int(range));
            hud_.text(s.vx(770) - hud_.textWidth(buf, s.text()) / 2, s.vy(450), buf, kHudBright, s.text());
            const bool target = range >= 0 && range <= 999;
            const char* status = target ? "Laser Active" : "No Target";
            if (target || (ticks_ & 0x10))
                hud_.text(s.vx(770) - hud_.textWidth(status, s.text()) / 2, s.vy(300), status, kHudBright, s.text());
        } else {
            if (range < 0 || range > 999) std::snprintf(buf, sizeof buf, "1km+");
            else if (range < 1) std::snprintf(buf, sizeof buf, "1m-");
            else std::snprintf(buf, sizeof buf, "%3dm", int(range));
            hud_.text(s.cx(180), s.y(312.5f), buf, kHudBright, s.text());
        }
    }

    if (showGoals_) drawGoals(s);
    if (missionResult_ >= 0) drawMissionEnd(s);
    // 0x482f02: dead, until the mission is over: "You Are Dead" centred at
    // x 512, top + 0x14 (as the end panel), in the message colour
    // ([0x66f948]: colour table entry [0x66fa48], white by default).
    if (playerDead() && missionResult_ < 0 && df2Hud_.on) {
        // DF2.EXE: STRMISC11 centred at (512, top+20) in the HUD colour
        // ([0x653618], white by default).
        const std::string dead = df2Text("Misc", "STRMISC11", "You are dead.");
        hud_.text(s.vx(512) - hud_.textWidth(dead, s.text()) / 2, s.playY + s.vy(20), dead, kHudWhite, s.text());
    } else if (playerDead() && missionResult_ < 0) {
        const char* dead = "You Are Dead";
        hud_.text(s.cx(0) - hud_.textWidth(dead, s.text()) / 2, s.y(72.5f), dead, kHudMessage, s.text());
    }

    if (friendlyTags_) drawFriendlyTags(s, viewProj);
    if (binoculars_ && !playerDead()) drawBinoculars(s);
    // Delta Force 2's breath bar (0x441540) while the air count is not 0, in
    // the virtual 1024x768 space: half-width w = 100 - air*100/2880 about
    // x 512, 10 high, 20 below the 3D view's top; cyan, red over 2240;
    // "Breath" centred 20 lower in the bar's colour.
    if (df2Hud_.on && air_ > 0) {
        const int w = 100 - air_ * 100 / 2880;
        const float top = s.playY + s.vy(20);
        const Color c = air_ > 2240 ? kDf2Red : rgb(0, 0xff, 0xff);   // the label too
        if (w > 0) hud_.rect(s.vx(512.f - float(w)), top, s.vx(512.f + float(w)) - s.vx(512.f - float(w)), s.vy(10), c);
        hud_.text(s.vx(512) - hud_.textWidth("Breath", s.text()) / 2, top + s.vy(20), "Breath", c, s.text());
    }
    if (oldMessages_) drawOldMessages(s);
    if (briefingShown_) drawBriefing(s);
    if (helpShown_) drawHelp(s);
    if (paused_) drawPaused(s);

    if (df2Hud_.on) {
        drawDf2Strips(s);
        drawDf2Top(s);
        drawDf2Bottom(s);
    } else {
        drawTopBar(s);
        drawBottomBar(s);
    }
    // Damage flash (drawn after the HUD, 0x4043a8 / 0x44c1c0): the 3D view
    // multiplied by (255, 255 - v, 255 - v) / 256, a red tint of strength v.
    if (damageFlash_ > 0) {
        const float c = float(255 - damageFlash_) / 256.f;
        hud_.multiply(0, s.playY, s.w, s.playH, {1, c, c, 1});
    }
    hud_.end();
}

// Delta Force 2's HUD. DF2.EXE draws each display mode's art 1:1 at that
// mode's pixel positions from HUD.DEF; here the mode's pixels are scaled to
// the window height (s.art()). Wider windows (a modern amenity): the strips
// are split at a seam, the left part stays at the left, the right part at
// the right, and the seam column is stretched across the gap; HUD.DEF
// points left of the seam are left-anchored, the others right-anchored.
namespace {
// Seam columns in 640 pixels, inside a panel so the stretch continues its
// frame: the bottom strip's extras panel, the top strip's chat panel.
constexpr float kDf2SeamBottom = 320.f, kDf2SeamTop = 190.f;
}   // namespace

float Game::df2X(const Screen& s, int x, bool bottom) const {
    const float n = float(s.modeW), seam = (bottom ? kDf2SeamBottom : kDf2SeamTop) * n / 640.f;
    return float(x) < seam ? float(x) * s.art() : s.w - (n - float(x)) * s.art();
}

float Game::df2Y(const Screen& s, int y, bool bottom) const {
    // Bottom-strip points are in screen rows of the mode (0x442020 copies
    // DHB to rows screenH - h ..).
    return bottom ? s.h - (float(s.modeW) * 0.75f - float(y)) * s.art() : float(y) * s.art();
}

void Game::df2Panel(const Screen& s, float vx0, float vy0, float vx1, float vy1) {
    const float x0 = s.vx(vx0), x1 = s.vx(vx1), y0 = s.vy(vy0), y1 = s.vy(vy1), t = s.px();
    hud_.multiply(x0, y0, x1 - x0, y1 - y0, {0.5f, 0.5f, 0.5f, 1});
    hud_.rect(x0, y0, x1 - x0, t, kDf2Amber);
    hud_.rect(x0, y1 - t, x1 - x0, t, kDf2Amber);
    hud_.rect(x0, y0, t, y1 - y0, kDf2Amber);
    hud_.rect(x1 - t, y0, t, y1 - y0, kDf2Amber);
}

void Game::drawDf2Strips(const Screen& s) {
    // DHT copied to the top rows (0x442170), DHB to the bottom rows
    // (0x442020), opaque.
    hud_.setBlend(4);
    auto strip = [&](const HudImage& img, float y, float seam640) {
        if (!img.texture) return;
        const float k = s.art(), w = float(img.width) * k, h = float(img.height) * k;
        const float seam = seam640 * float(img.width) / 640.f, su = seam / float(img.width);
        const float left = seam * k, right = w - left;
        if (s.w <= w + 0.5f) {
            hud_.image(img.texture, (s.w - w) * 0.5f, y, w, h);
            return;
        }
        hud_.imageUV(img.texture, 0, y, left, h, 0, 0, su, 1);
        hud_.imageUV(img.texture, s.w - right, y, right, h, su, 0, 1, 1);
        const float du = 1.f / float(img.width);
        hud_.imageUV(img.texture, left, y, s.w - right - left, h, su, 0, su + du, 1);
    };
    strip(df2Hud_.top, 0, kDf2SeamTop);
    strip(df2Hud_.bottom, s.h - float(df2Hud_.bottom.height) * s.art(), kDf2SeamBottom);
    hud_.setBlend(0);
}

void Game::drawDf2Bottom(const Screen& s) {
    // DF2.EXE 0x442020, in this order: radar, weapon block, waypoint block,
    // extras, stance icon, carried item. Text in the HUD font, 0xff8000.
    const int n = s.modeW;
    const float k = s.art(), ts = s.text();
    const HudDef& def = df2Hud_.def;
    auto X = [&](const char* key, size_t i = 0) { return df2X(s, def.value(n, key, i), true); };
    auto Y = [&](const char* key, size_t i = 1) { return df2Y(s, def.value(n, key, i), true); };
    auto centred = [&](const std::string& t, float x, float y, Color c) { hud_.text(x - hud_.textWidth(t, ts) / 2, y, t, c, ts); };
    auto frameOf = [&](const HudImage& img, int frames, int f, float x, float y) {
        const float fh = float(img.height / frames), H = float(img.height);
        hud_.imageUV(img.texture, x, y, float(img.width) * k, fh * k, 0, float(f) * fh / H, 1, float(f + 1) * fh / H);
    };

    // Radar (0x421510, 0x4226c0): the terrain colour map north-up in the
    // `map` rectangle at (scale << 10) / W m a pixel (1.6 m at 640), then
    // rad%i.pcx added over it, then the blips (size 5W/1024 pixels).
    const float mx0 = X("map", 0), my0 = Y("map", 1), mx1 = X("map", 2), my1 = Y("map", 3);
    const float mcx = (mx0 + mx1) * 0.5f, mcy = (my0 + my1) * 0.5f;
    const float mPerPx = 1024.f / float(n) * mapScale_ / k;   // metres per window pixel
    const Vec3 c = player_.feet;
    if (mx1 > mx0 && my1 > my0 && df2Elevation_) {
        // [0x765600] (the map's D, F9 / F10) is global: the HUD radar shows
        // the elevation picture too, never contours (no flag 0x2000).
        static HudImage radarPic;
        const int w = std::max(1, int((mx1 - mx0) / k)), h = std::max(1, int((my1 - my0) / k));
        df2Picture(radarPic, w, h, player_.missionX(), player_.missionY(), 1024.0 / double(n) * mapScale_, true, false);
        hud_.image(radarPic.texture, mx0, my0, mx1 - mx0, my1 - my0);
    } else if (mx1 > mx0 && my1 > my0) {
        const float ext = terrain_.mapExtent(), hw = (mx1 - mx0) * 0.5f * mPerPx, hh = (my1 - my0) * 0.5f * mPerPx;
        hud_.imageUV(terrain_.radarTexture() ? terrain_.radarTexture() : terrain_.colorTexture(), mx0, my0, mx1 - mx0, my1 - my0, (c.x - hw) / ext, (c.z - hh) / ext, (c.x + hw) / ext,
                     (c.z + hh) / ext);
    }
    if (df2Hud_.rad.texture) {
        hud_.setBlend(5);
        const float w = float(df2Hud_.rad.width) * k, h = float(df2Hud_.rad.height) * k;
        const float cx = (df2X(s, def.value(n, "map", 0), true) + df2X(s, def.value(n, "map", 2), true)) * 0.5f;
        hud_.image(df2Hud_.rad.texture, std::floor(cx - w / 2), std::floor(mcy - h / 2), w, h);
        hud_.setBlend(0);
    }
    {
        hud_.clip(mx0, my0, mx1 - mx0, my1 - my0);
        const float blip = float(std::max(1, 5 * n / 1024)) * k;
        auto teamColour = [](int side) {
            return side == 1 ? rgb(0x10, 0x40, 0xff) : side == 2 ? rgb(0xff, 0, 0) : side == 3 ? rgb(0xff, 0xff, 0) : side == 4 ? rgb(0xff, 0xff, 0xff) : rgb(0, 0xff, 0);
        };
        const double px = player_.missionX(), py = player_.missionY();
        auto toRadar = [&](double x, double y) {
            return std::pair<float, float>{mcx + float(x - px) / mPerPx, mcy - float(y - py) / mPerPx};
        };
        // Route lines between the waypoints (0x44f550), orange.
        for (size_t i = 0; i + 1 < route_.size(); ++i) {
            auto [ax, ay] = toRadar(route_[i].x, route_[i].y);
            auto [bx, by] = toRadar(route_[i + 1].x, route_[i + 1].y);
            hud_.line(ax, ay, bx, by, s.px(), kDf2Amber);
        }
        // Soldiers by the GPS option (0 none, 1 marked, 2 all), shape 5.
        if (o_.gpsIcons > 0)
            for (const auto& o : world_.objects) {
                if (o.missionGroup != 3 || !o.person || o.removed) continue;
                if (o_.gpsIcons == 1 && !o.mapMarker) continue;
                const Vec3 at = world_.nearest(o.pos, c);
                drawMapMarker(s, mcx + (at.x - c.x) / mPerPx, mcy + (at.z - c.z) / mPerPx, 5, teamColour(o.side), o.heading, o.dead,
                              blip);
            }
        // Objects with a radar shape (obj+0x6E, 0x421d2f): at their live
        // positions; the current waypoint blinks.
        const MissionObject* current = route_.empty() || df2HudWp_.kind ? nullptr : route_[size_t(routeIndex_)].object;
        forDf2Shapes([&](const Df2Shape& d) {
            if (d.mission == current && !(ticks_ & 16)) return;
            float bx, by;
            if (d.world) {
                const Vec3 at = world_.nearest(d.world->pos, c);
                bx = mcx + (at.x - c.x) / mPerPx;
                by = mcy + (at.z - c.z) / mPerPx;
            } else std::tie(bx, by) = toRadar(d.mission->x, d.mission->y);
            drawMapMarker(s, bx, by, d.shape, teamColour(d.side), 0, false, blip);
        });
        // Player waypoints (pool loop 0x421e48): diamonds in team 0's green;
        // the HUD waypoint blinks (0x421e60).
        for (size_t i = 0; i < df2Wps_.size(); ++i) {
            if (!df2Wps_[i].used || (df2HudWpIs(Df2Sel{4, int(i)}) && !(ticks_ & 16))) continue;
            auto [bx, by] = toRadar(df2Wps_[i].x, df2Wps_[i].y);
            drawMapMarker(s, bx, by, 3, teamColour(0), 0, false, blip);
        }
        // The player: shape 5 in its team colour (green).
        drawMapMarker(s, mcx, mcy, 5, teamColour(0), player_.yaw, false, blip);
        hud_.unclip();
    }
    // The next waypoint (0x443850): a filled dot of radius 3 on the ring of
    // map_radius in its compass bearing.
    if (const Df2WpView p = df2CurrentWaypoint(); p.ok && !route_.empty()) {
        const double ex = p.x - player_.missionX(), ny = p.y - player_.missionY(), len = std::sqrt(ex * ex + ny * ny);
        if (len > 0) {
            const float r = float(def.value(n, "map_radius", 0)) * k;
            hud_.ring(mcx + float(ex / len) * r, mcy - float(ny / len) * r, 1.5f * k, 3.f * k, kDf2Amber);
        }
    }

    // Weapon block (0x443110): icon, name centred at weapon_name (red when
    // not ready), the clips text centred at weapon_clips, the rounds bar
    // (solid, proportional) and the chamber bar, by the weapon's kind.
    hud_.setBlend(5);
    static const signed char kWeapFrame[53] = {
        -1, 12, 12, -1, 7, 10, 4, 4, -1, -1, 4, 13, 8, 8, 8, 3, -1, -1, -1, -1, 11, 11, 11, -1, -1, -1, -1,
        1, 5, 2, 2, -1, 9, 0, 0, -1, -1, 6, 6, 15, 16, 16, -1, 19, 21, 20, -1, 14, 17, 0, 0, 13, 13};
    const int mode = arsenal_.modeIndex();
    if (df2Hud_.weap.texture && mode >= 0 && mode < 53 && kWeapFrame[mode] >= 0)
        frameOf(df2Hud_.weap, 22, kWeapFrame[mode], X("gun_icon"), Y("gun_icon"));
    hud_.setBlend(0);
    const ModeDef& md = arsenal_.mode();
    // DF2.EXE 0x447400 / 0x443110: the weapon record's own kind (+0x0C).
    const int kind = md.type;
    const int clips = arsenal_.spares(), rounds = arsenal_.rounds(), cap = arsenal_.gun().magazine;
    const bool chamber = arsenal_.chambered();
    bool showRounds = false, showChamber = false, clipText = false, ok = true;
    switch (kind) {
    case 1: showRounds = showChamber = clipText = true; ok = !arsenal_.reloading() && (clips || rounds || chamber); break;
    case 2: showChamber = clipText = true; ok = !arsenal_.firing() && (clips || chamber); break;
    case 3: case 4: case 5: showRounds = showChamber = clipText = true; break;
    case 6: showChamber = true; ok = !arsenal_.firing() && (clips || chamber); break;
    case 7: case 9: showChamber = clipText = true; ok = !arsenal_.firing() && (clips || chamber); break;
    case 10: clipText = true; ok = fireMissions_ > 0; break;
    case 11: showChamber = clipText = true; ok = !arsenal_.firing() && (clips || chamber); break;   // 0x447584[10] = 0x447521
    // Binoculars: red unless they are up (a *_BIN move: 104, 114, 5).
    case 12: ok = binoculars_; break;
    default: if (kind > 12) showRounds = showChamber = clipText = true; break;
    }
    std::string name = arsenal_.modeName();
    char key[16];
    std::snprintf(key, sizeof key, "HG%03d", mode);
    if (const std::string* t = gameTable_.find("HudGuns", key)) name = *t;
    const Color nameColour = ok ? kDf2Amber : kDf2Red;
    centred(name, X("weapon_name"), Y("weapon_name"), nameColour);
    char buf[32];
    if (clipText) std::snprintf(buf, sizeof buf, "\x01 %d", kind == 10 ? fireMissions_ : clips);
    else std::snprintf(buf, sizeof buf, "NA");
    centred(buf, X("weapon_clips"), Y("weapon_clips"), kDf2Amber);
    if (showRounds) {
        const int x0 = def.value(n, "weapon_rounds", 0), x1 = def.value(n, "weapon_rounds", 2);
        const int64_t frac = cap ? (int64_t(rounds) << 16) / cap : 0x10000;
        const int w = int((int64_t(x1 - x0) * frac) >> 16);
        if (w > 0) {
            const float a = X("weapon_rounds", 0), top = Y("weapon_rounds", 1), bottom = Y("weapon_rounds", 3);
            hud_.rect(a, top, float(w) * k, bottom - top, kDf2Amber);
        }
    }
    if (showChamber && chamber) {
        const float a = X("weapon_chamber", 0), b = X("weapon_chamber", 2), top = Y("weapon_chamber", 1), bottom = Y("weapon_chamber", 3);
        hud_.rect(a, top, b - a, bottom - top, nameColour);
    }

    // Waypoint block (0x442c30): left-aligned lines; heading and grid always NA.
    {
        std::string lines[5] = {"NO WAYPOINT", "  HEADING: NA", "  GRID: NA", "  RANGE: NA", "  ELEVATION: NA"};
        if (const Df2WpView p = df2CurrentWaypoint(); p.ok) {
            char line[160];
            // "WP%i: %s" with its place in the mission list, "WP: %s" if not in it.
            if (p.listIndex >= 0) std::snprintf(line, sizeof line, "WP%d: %s", p.listIndex + 1, p.name.c_str());
            else std::snprintf(line, sizeof line, "WP: %s", p.name.c_str());
            lines[0] = line;
            const long dx = long(std::floor(p.x - player_.missionX())), dy = long(std::floor(p.y - player_.missionY()));
            const long d = long(std::sqrt(double(dx * dx + dy * dy)));
            if (d < 1000) std::snprintf(line, sizeof line, "  RANGE: %ldm", d);
            else std::snprintf(line, sizeof line, "  RANGE: %ldkm+", d / 1000);
            lines[3] = line;
            // The waypoint's own height (w+0x2c).
            std::snprintf(line, sizeof line, "  ELEVATION: %dm", int(std::floor(p.elev)));
            lines[4] = line;
        }
        const char* keys[5] = {"waypoint_name", "waypoint_heading", "waypoint_grid", "waypoint_dist", "waypoint_elev"};
        for (int i = 0; i < 5; ++i) hud_.text(X(keys[i]), Y(keys[i]), lines[i], kDf2Amber, ts);
    }

    // Extras (0x441b20): the player's name; the wind, "Wind: %s, %ikph" with
    // the direction ((deg + 22) % 360) / 45 as N..NW, or "Wind: None";
    // extra3/4 are multiplayer only, extra5 is empty.
    hud_.text(X("extra1"), Y("extra1"), o_.soldierName, kDf2Amber, ts);
    {
        static const char* kDir[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        char wind[48];
        if (mission_.windKph == 0) std::snprintf(wind, sizeof wind, "Wind: None");
        else std::snprintf(wind, sizeof wind, "Wind: %s, %ikph", kDir[((mission_.windDeg + 22) % 360 + 360) % 360 / 45], mission_.windKph);
        hud_.text(X("extra2"), Y("extra2"), wind, kDf2Amber, ts);
    }

    hud_.setBlend(5);
    // Stance (0x442bc0): POS%i 3 frames prone, crouched, standing.
    if (df2Hud_.pos.texture)
        frameOf(df2Hud_.pos, 3, player_.stance == Stance::Prone ? 0 : player_.stance == Stance::Crouch ? 1 : 2, X("pos_icon"), Y("pos_icon"));
    // Carried item (0x442360): ITM%i 7 frames by item id.
    if (df2Hud_.itm.texture && carried_ >= 0 && world_.objects[size_t(carried_)].item) {
        int f = -1;
        switch (world_.objects[size_t(carried_)].item->id - 100000) {
        case 4038: f = 0; break;
        case 4008: case 4018: f = 2; break;
        case 4077: f = 3; break;
        case 4021: f = 4; break;
        case 4034: f = 5; break;
        case 4035: case 4036: case 4128: f = 6; break;
        default: break;
        }
        if (f >= 0) frameOf(df2Hud_.itm, 7, f, X("carrying_icon"), Y("carrying_icon"));
    }
    hud_.setBlend(0);
}

void Game::drawDf2Top(const Screen& s) {
    // DF2.EXE 0x442170: three lines of each history at (x0 + 2, y0 + k·12W/640),
    // the oldest of the three on top, each in its own colour; no timeout.
    const int n = s.modeW;
    const float ts = s.text(), sp = float(12 * n / 640) * s.art();
    auto history = [&](const std::deque<Df2Line>& h, const char* key) {
        const float x = df2X(s, df2Hud_.def.value(n, key, 0) + 2, false), y = df2Y(s, df2Hud_.def.value(n, key, 1), false);
        for (int row = 0; row < 3; ++row) {
            const size_t e = size_t(2 - row);
            if (e < h.size() && !h[e].text.empty()) hud_.text(x, y + float(row) * sp, h[e].text, h[e].colour, ts);
        }
    };
    history(df2Chat_, "chat_message");
    history(df2Sys_, "sys_message");
}

// Delta Force 2's command map (DF2.EXE 0x40b370, 0x40d630, 0x410f10): drawn
// instead of the 3D view at 640x480 (here the 640x480 screen scaled into the
// window, aspect kept); layout in the 1024x768 virtual space x 0.625 (whole
// pixels), art 1:1 at 640. Multiplayer-only parts (net messages, the
// Send to... list, Make Permanent, voice buttons, the start countdown) are
// left out: in single player they never show.
namespace {
// DF2.EXE 0x4aecdc (copied to 0xa39114): the elevation ramp T; the elevation
// palette 0x653200 (0x422f30) is RGB (0, T[i], 0).
const uint8_t kDf2ElevRamp[256] = {
    0,   3,   5,   7,   9,   11,  13,  14,  16,  18,  19,  21,  22,  24,  25,  26,  28,  29,  31,  32,  33,  35,  36,  37,  39,  40,
    41,  42,  44,  45,  46,  47,  48,  50,  51,  52,  53,  54,  56,  57,  58,  59,  60,  61,  63,  64,  65,  66,  67,  68,  69,  70,
    71,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,
    99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 123,
    124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 140, 141, 142, 143, 144, 145, 146, 147, 148,
    149, 150, 151, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 161, 162, 163, 164, 165, 166, 167, 168, 169, 169, 170, 171,
    172, 173, 174, 175, 176, 177, 177, 178, 179, 180, 181, 182, 183, 183, 184, 185, 186, 187, 188, 189, 190, 190, 191, 192, 193, 194,
    195, 196, 196, 197, 198, 199, 200, 201, 202, 202, 203, 204, 205, 206, 207, 207, 208, 209, 210, 211, 212, 212, 213, 214, 215, 216,
    217, 217, 218, 219, 220, 221, 222, 222, 223, 224, 225, 226, 227, 227, 228, 229, 230, 231, 232, 232, 233, 234, 235, 236, 236, 237,
    238, 239, 240, 240, 241, 242, 243, 244, 245, 245, 246, 247, 248, 249, 249, 250, 251, 252, 253, 253, 254, 255};
// 0x4434e0 (byte table 0x443614, jump table 0x4435b4): a weapon TYPE's
// WEAP frame, -1 none (type 9 M4/M203 has no picture: READ).
const signed char kDf2TypeFrame[39] = {-1, 18, 12, 12, -1, 7,  10, -1, 4,  -1, 13, 8,  3, -1, -1, 11, -1, -1, -1, 1,
                                       5,  2,  -1, 9,  6,  -1, 0,  0,  15, 16, -1, 19, 21, 20, -1, 14, 17, 0,  0};
// 0x4a4c88: the categories (type +0x94) each Weapons slot accepts, as bits.
const int kDf2SlotMask[5] = {8, 0x10, 0x30, 4, 0x40};
// Grid name of a point (0x422d60): columns (x + 1000) / 100 as the map's
// letters, rows (1000 - y) / 100 + 1.
std::string df2GridName(double x, double y) {
    const int i = (int(std::floor(x)) + 1000) / 100, row = (1000 - int(std::floor(y))) / 100 + 1;
    std::string col;
    if (i >= 0 && i < 26) col = std::string(1, char('A' + i));
    else if (i >= 26) col = std::string(1, char('@' + i / 26)) + char('A' + i % 26);
    return col + std::to_string(row);
}
} // namespace

bool Game::df2SelValid(Df2Sel s) const {
    switch (s.kind) {
    case 1: return true;
    case 2: return s.index >= 0 && size_t(s.index) < world_.objects.size() && !world_.objects[size_t(s.index)].removed;
    case 3:
        if (s.index < 0 || size_t(s.index) >= mission_.objects.size()) return false;
        for (const auto& o : world_.objects)
            if (o.missionIndex == s.index) return !o.removed;
        return true;
    case 4: return s.index >= 0 && size_t(s.index) < df2Wps_.size() && df2Wps_[size_t(s.index)].used;
    default: return false;
    }
}

bool Game::df2SelPos(Df2Sel s, double& x, double& y, float* z) const {
    if (!df2SelValid(s)) return false;
    if (s.kind == 1) {
        x = player_.missionX();
        y = player_.missionY();
        if (z) *z = player_.feet.y;
    } else if (s.kind == 2) {
        // The copy nearest the player on the repeating map (as drawn).
        const Vec3 at = world_.nearest(world_.objects[size_t(s.index)].pos, player_.feet);
        x = at.x;
        y = -at.z;
        if (z) *z = at.y;
    } else if (s.kind == 3) {
        const MissionObject& mo = mission_.objects[size_t(s.index)];
        for (const auto& o : world_.objects)
            if (o.missionIndex == s.index) {
                const Vec3 at = world_.nearest(o.pos, player_.feet);
                x = at.x;
                y = -at.z;
                if (z) *z = at.y;
                return true;
            }
        x = mo.x;
        y = mo.y;
        if (z) *z = terrain_.heightAt(float(mo.x), float(-mo.y)) + mo.z;   // ground plus its lift
    } else {
        const Df2Waypoint& w = df2Wps_[size_t(s.index)];
        x = w.x;
        y = w.y;
        if (z) *z = w.z;
    }
    return true;
}

std::string Game::df2MissionName(size_t index) const {
    const MissionObject& mo = mission_.objects[index];
    for (const auto& rp : route_)
        if (rp.object == &mo) return rp.name;
    const ItemDef* it = items_.byMissionType(mo.type);
    if (it && it->type == "marker") {
        // Waypoint markers name themselves: record +0x58 -> STRWPNAME%03i (0x403afe).
        char key[24];
        std::snprintf(key, sizeof key, "STRWPNAME%03d", mo.raw[0x58 / 4]);
        if (const std::string* n = wpnText_.find("WPNames", key); n && !n->empty()) return *n;
    }
    return it ? it->name : std::string();
}

std::string Game::df2SelName(Df2Sel s) const {
    std::string n;
    if (!df2SelValid(s)) return n;
    if (s.kind == 1) n = o_.soldierName;
    else if (s.kind == 2) n = world_.objects[size_t(s.index)].item ? world_.objects[size_t(s.index)].item->name : std::string();
    else if (s.kind == 3) n = df2MissionName(size_t(s.index));
    else n = df2Wps_[size_t(s.index)].name;
    return n.empty() ? df2Text("Cmap", "CMAP_ITEM_UNKNOWN", "Unknown") : n;
}

int Game::df2SelItem(Df2Sel s) const {
    if (!df2SelValid(s)) return 0;
    if (s.kind == 1) return playerItem_ - 100000;
    if (s.kind == 2) return world_.objects[size_t(s.index)].item ? world_.objects[size_t(s.index)].item->id - 100000 : 0;
    if (s.kind == 3) return int(mission_.objects[size_t(s.index)].type);
    return 6089;
}

// The player and the squad: the objects of the player array 0xc399c0 on the
// player's team (+0x68, BMS byte +0x43 = OpenDF's side). The player array is
// mission group 3: the mission loader puts group 0 at 0xc4a5c0 (0x403d89),
// 1 at 0xcd05c0 (0x403df2), 2 at 0xd135c0 (0x403e5b) and 3 at 0xc399c0
// (0x403ec4, count [0xd42b04]) through 0x403590 (READ).
bool Game::df2SelOnPlayerTeam(Df2Sel s) const {
    if (s.kind == 1) return true;
    if (s.kind != 2 || !df2SelValid(s)) return false;
    const WorldObject& o = world_.objects[size_t(s.index)];
    return o.person && o.side == kPlayerSide && o.missionGroup == 3;
}

// The action buttons for the selection (0x40dd50), the first at (688, 696),
// each next 32 higher. Codes: 1 Bozo Filter, 2 Send Message to (both no-ops,
// 0x4015e0), 3 Set as my Waypoint (0x411630), 4 Delete (0x411680), 5 Rename
// (0x411700). Set Voice Channel / Send to... / Make Permanent need voice or
// network players (multiplayer only).
std::vector<std::pair<std::string, int>> Game::df2ActionButtons() const {
    std::vector<std::pair<std::string, int>> b;
    const Df2Sel s = df2Map_.sel;
    if (!df2SelValid(s)) return b;
    const std::string setWay = df2Text("Cmap", "CMAP_BTN_SETWAY", "Set as my Waypoint");
    if (s.kind == 1) {   // +0x64 bit 0: a player
        b.push_back({df2Text("Cmap", "CMAP_BTN_BOZO", "Bozo Filter"), 1});
        b.push_back({df2Text("Cmap", "CMAP_BTN_SENDMESSAGE", "Send Message to"), 2});
    }
    if (df2SelOnPlayerTeam(s)) b.push_back({setWay, 3});
    const int item = df2SelItem(s);
    if (s.kind == 3) {
        const ItemDef* it = items_.byMissionType(uint32_t(item));
        if ((it && it->type == "marker") || item == 6005 || item == 4035 || item == 4036) b.push_back({setWay, 3});
    }
    if (s.kind == 4) {
        b.push_back({df2Text("Cmap", "CMAP_BTN_DELETE", "Delete"), 4});
        b.push_back({df2Text("Cmap", "CMAP_BTN_RENAME", "Rename"), 5});
        b.push_back({setWay, 3});
    }
    return b;
}

void Game::df2Action(int code) {
    Df2Map& m = df2Map_;
    switch (code) {
    case 3: df2SetWaypoint(m.sel); break;
    case 4:   // 0x411680: zero the object, select(0); a HUD waypoint pointing at it shows NO WAYPOINT
        if (m.sel.kind == 4 && df2SelValid(m.sel)) {
            df2Wps_[size_t(m.sel.index)] = Df2Waypoint{};
            df2Select(Df2Sel{});
        }
        break;
    case 5:   // 0x411700: the name prompt again
        m.prompt = true;
        m.promptText.clear();
        SDL_StartTextInput();
        break;
    default: break;   // Bozo Filter, Send Message to: no-ops (0x4015e0)
    }
}

// 0x447b70: [0x765754] = obj (players of another team refused: multiplayer).
// A mission waypoint that is on the route becomes the route's current point.
void Game::df2SetWaypoint(Df2Sel s) {
    if (s.kind == 3)
        for (size_t i = 0; i < route_.size(); ++i)
            if (route_[i].object == &mission_.objects[size_t(s.index)]) {
                routeIndex_ = int(i);
                df2HudWp_ = Df2Sel{};
                return;
            }
    if (s.kind == 0) {
        // [0x765754] = 0: NO WAYPOINT (an empty selection, which has no button).
        df2HudWp_ = Df2Sel{4, -1};
        return;
    }
    df2HudWp_ = s;
}

// NextWaypoint (action 32, 0x446700(+1)): only the mission list; the current
// waypoint not in it -> from index 0, so the next is entry 1.
void Game::df2NextWaypoint() {
    if (route_.empty()) return;
    if (df2HudWp_.kind != 0) {
        df2HudWp_ = Df2Sel{};
        routeIndex_ = 1 % int(route_.size());
        return;
    }
    routeIndex_ = (routeIndex_ + 1) % int(route_.size());
}

bool Game::df2HudWpIs(Df2Sel s) const {
    if (df2HudWp_.kind == 0) return s.kind == 3 && !route_.empty() && route_[size_t(routeIndex_)].object == &mission_.objects[size_t(s.index)];
    return df2HudWp_ == s;
}

Game::Df2WpView Game::df2CurrentWaypoint() const {
    Df2WpView v;
    if (df2HudWp_.kind == 0) {
        if (route_.empty()) return v;
        const RoutePoint& p = route_[size_t(routeIndex_)];
        v.ok = true;
        v.x = p.x;
        v.y = p.y;
        // The waypoint's own height (w+0x2c): ground plus its lift.
        v.elev = terrain_.heightAt(float(p.x), float(-p.y)) + (p.object ? p.object->z : 0.f);
        v.name = p.name;
        v.listIndex = routeIndex_;
        return v;
    }
    // A slot deleted or an object gone (class 0): NO WAYPOINT.
    if (!df2SelPos(df2HudWp_, v.x, v.y, &v.elev)) return v;
    v.ok = true;
    v.name = df2SelName(df2HudWp_);
    return v;
}

// The radar picture of 0x421510 for the elevation view and contours, w x h
// pixels at `step` metres a pixel centred on (px, py) (mission metres):
// column x from px - halfW * step, row y from py + halfH * step down. H is
// the level-0 height in 8.8 elevation steps; elevation: E[H >> 8] (no water
// blending); contours (flag 0x2000): where H >> 12 differs from the previous
// pixel of the row (0 before the first) or the pixel one row south, colour
// [0x6535fc] = E[255] = 0x00ff00. Without elevation the pixels that are not
// contour are left transparent (drawn over the colour picture).
void Game::df2Picture(HudImage& img, int w, int h, double px, double py, double step, bool elevation, bool contours) {
    std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4, 0);
    const float toSteps = 256.f / terrain_.scale.heightScale;
    auto H = [&](double x, double y) { return std::max(0, int(terrain_.cellHeight(float(x), float(-y)) * toSteps)); };
    const int hw = w / 2, hh = h / 2;
    for (int r = 0; r < h; ++r) {
        const double y = py + double(hh) * step - double(r) * step;
        int prev = 0;
        for (int c = 0; c < w; ++c) {
            const double x = px - double(hw) * step + double(c) * step;
            const int v = H(x, y);
            uint8_t* o = &rgba[(size_t(r) * size_t(w) + size_t(c)) * 4];
            if (contours && ((v >> 12) != (prev >> 12) || (v >> 12) != (H(x, y - step) >> 12))) {
                o[1] = 255;
                o[3] = 255;
            } else if (elevation) {
                o[1] = kDf2ElevRamp[std::min(255, v >> 8)];
                o[3] = 255;
            }
            prev = v;
        }
    }
    hud_.updateImage(img, w, h, rgba);
}

int Game::df2TypeCategory(int type) const {
    // The weapon-type table [0x4f05a4] = 0x4C5150, 216 bytes each, +0x94 menu category.
    int32_t v = 0;
    if (type <= 0 || type >= 39 || !df2Exe_.i32(0x4C5150 + uint32_t(type) * 216 + 0x94, v)) return 0;
    return v;
}

std::string Game::df2TypeName(int type) const {
    char key[24];
    std::snprintf(key, sizeof key, "STRWEAP2_%02d", type);
    return df2Text("WeaponNames2", key, "?");
}

// Request Weapons (0x411800 -> message 0x55): the server side (0x45a6e0)
// accepts only when the player's object has +0x64 & 0x1000000, or in the
// multiplayer start window [0x655cd0] (0 in single player). The flag is
// cleared every tick and set by the volume push-out (0x440acf) while the
// player overlaps a type-6 collision volume: an armory area (AMMODUMP,
// KHUFU and TAVERN are the shipped models that have one). INFERRED: single
// player delivers its own message 0x55. Refused, nothing happens and no
// message is shown (0x45a6fb jumps to the end). OPENDF_REARM accepts
// anywhere (testing).
void Game::df2RequestWeapons() {
    Df2Map& m = df2Map_;
    const bool accepted = player_.armory() || std::getenv("OPENDF_REARM");
    if (std::getenv("OPENDF_DEBUGPLAYER")) std::fprintf(stderr, "Request Weapons: %s\n", accepted ? "accepted" : "refused (not in an armory volume)");
    if (!accepted) return;
    // 0x458190 applies the loadout: each requested type only if its menu
    // category fits the slot (2 sidearm, 3 primary, 4 secondary 1, 4/5
    // secondary 2, 6 grenade; else 0), then every slot's magazines from the
    // type table (+0x84/+0x88/+0x8c); 0x456f10 refills the ammunition pools
    // from them (clips x rounds, +1 with a round chambered), as at the
    // mission start: Arsenal::initDf2 does both. The client writes
    // +0xb6..+0xba, re-equips (0x447e20), shows STRCLI10 "Weapons Updated",
    // plays sound 0x164 and re-runs the tab's enter (0x40a5d9).
    df2Loadout_.primary = m.request[0];
    df2Loadout_.secondary = m.request[1];
    df2Loadout_.gear = m.request[2];
    df2Loadout_.sidearm = m.request[3];
    df2Loadout_.grenade = m.request[4];
    std::vector<std::string> hudGuns;
    for (int i = 0; i < 57; ++i) {
        char key[16];
        std::snprintf(key, sizeof key, "HG%03d", i);
        const std::string* t = gameTable_.find("HudGuns", key);
        hudGuns.push_back(t ? *t : std::string());
    }
    if (arsenal_.initDf2(hudGuns, df2Exe_, &items_, df2Loadout_)) arsenal_.setWind(float(mission_.windKph), float(mission_.windDeg));
    pushMessage(df2Text("Client", "STRCLI10", "Weapons Updated"));
    sound_.play2D(0x164);
    const int cur[5] = {df2Loadout_.primary, df2Loadout_.secondary, df2Loadout_.gear, df2Loadout_.sidearm, df2Loadout_.grenade};
    for (int k = 0; k < 5; ++k) m.current[k] = m.request[k] = cur[k];
}

// A tab's enter hook (switch 0x40cd60: the old tab's buttons go, the new
// tab's enter hook runs; select(selected) again unless the tab has flag 4).
void Game::df2MapEnter(int tab) {
    Df2Map& m = df2Map_;
    m.tab = tab;
    m.entered = true;
    m.drag = Df2Sel{};
    if (tab == 0) {
        // 0x40d2c0: pan := 0; the icon buttons are made again: W with state 0
        // (even if the tool is still on, READ quirk), D with [0x765600].
        m.panX = m.panZ = 0;
        m.wButton = false;
        m.dButton = df2Elevation_;
    } else if (tab == 3) {
        // 0x40fb50: the current loadout to both lists (Primary, Secondary 1,
        // Secondary 2, Sidearm, Grenade = controller +0xb6..+0xba).
        const int cur[5] = {df2Loadout_.primary, df2Loadout_.secondary, df2Loadout_.gear, df2Loadout_.sidearm, df2Loadout_.grenade};
        for (int k = 0; k < 5; ++k) m.current[k] = m.request[k] = cur[k];
        // The < > buttons are made once, at the running y of the first draw.
        int y = 252;
        for (int k = 0; k < 5; ++k) {
            m.wbtnY[k] = y;
            if (m.request[k]) y += 96;
        }
    }
}

// The name prompt's keys (chat input 0x41d320): Enter calls the callback
// 0x411890 (a non-empty name goes to the selected item-6089 object, +0x54);
// Escape closes it: 0x41d320 sends characters 0..0x1b through the byte
// table 0x41d6ac and the jump table 0x41d698 (READ): 8 Backspace drops the
// last character, 13 Enter stores the line (5-line history 0xa78b00) and
// calls the callback with it, 27 Escape (0x41d669) calls the callback with
// NULL (0x411890 then does nothing) and resets the prompt (0x41cfd0); code 0
// goes to the special keys and every other control character is appended
// like a letter (OpenDF's text input only delivers printable ones).
void Game::df2MapKey(const SDL_KeyboardEvent& k) {
    Df2Map& m = df2Map_;
    switch (k.keysym.sym) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (!m.promptText.empty() && m.sel.kind == 4 && df2SelValid(m.sel)) df2Wps_[size_t(m.sel.index)].name = m.promptText;
        [[fallthrough]];
    case SDLK_ESCAPE:
        m.prompt = false;
        m.promptText.clear();
        SDL_StopTextInput();
        break;
    case SDLK_BACKSPACE:
        if (!m.promptText.empty()) m.promptText.pop_back();
        break;
    default: break;
    }
}

// Once a frame: the test script, + / - while held (0x4111b0 / 0x4111e0 act
// every frame, ignoring the repeat counter), the waypoint drag.
void Game::df2MapFrame() {
    Df2Map& m = df2Map_;
    auto V = [](float v) { return std::floor(v * 0.625f); };
    for (const auto& c : m.test) {
        if (c.frame != m.frame) continue;
        if (c.op == 'm' || c.op == 'd' || c.op == 'u') {
            m.mouseX = c.x;
            m.mouseY = c.y;
            if (c.op != 'm') df2MapClick(Screen{}, c.x, c.y, c.op == 'd', 1);
        } else if (c.op == 'k') {
            SDL_KeyboardEvent ev{};
            ev.keysym.scancode = SDL_GetScancodeFromName(c.text.c_str());
            ev.keysym.sym = SDL_GetKeyFromScancode(ev.keysym.scancode);
            handleKey(ev);
        } else if (c.op == 't' && m.prompt) {
            m.promptText += c.text;
            if (m.promptText.size() > 16) m.promptText.resize(16);
        }
    }
    ++m.frame;
    // The scroll bars of the Players (page 16) and Team (page 14) lists: the
    // widget pass 0x40c040 each frame finds the widget under the mouse; a new
    // one takes the mouse as the drag anchor ([0xbcc488]) and resets the hold
    // counter; the same one with the button down calls its callback 0x40c8c0
    // with the counter, then counts up (released: back to 0). 0x40c8c0: the
    // arrows and page zones act on counts 0 and >= 10 (0x40c8ee), the thumb on
    // every one. Up arrow: pos - 1 while > min; down: pos + 1 while pos + page
    // < max; page-up zone: pos - page, at least 0; page-down: pos + page, at
    // most max - page, at least 0; thumb: pos + (max - min)·dy / L for the
    // mouse's dy from the anchor (clamped the same way), and the anchor moves
    // with the thumb's top (0x40ca13..0x40ca2e).
    if (m.tab == 1 || m.tab == 2) {
        const float vy1 = m.tab == 1 ? 672 : 616;
        const int page = m.tab == 1 ? 16 : 14;
        int& pos = m.tab == 1 ? m.scrollPlayers : m.scrollTeam;
        const Df2Map::Bar r = Df2Map::bar(608, 216, vy1, pos, page);
        const int mx = m.mouseX, my = m.mouseY;
        int w = 0;   // 1 up, 2 page up, 3 thumb, 4 page down, 5 down (the last match of 0x40c040 wins)
        auto inside = [&](int x0, int y0, int x1, int y1) { return mx >= x0 && mx <= x1 && my >= y0 && my <= y1; };
        // The zone widgets (0x40c710): x0 + 1 .. x1 - 2; page-up ty0 + 3 .. top,
        // thumb top .. bottom, page-down bottom .. ty1; arrows 16 x 16.
        if (inside(r.x0, int(V(216)), r.x0 + 16, int(V(216)) + 16)) w = 1;
        if (inside(r.x0 + 1, r.ty0 + 3, r.x1 - 2, r.top)) w = 2;
        if (inside(r.x0 + 1, r.top, r.x1 - 2, r.bottom)) w = 3;
        if (inside(r.x0 + 1, r.bottom, r.x1 - 2, r.ty1)) w = 4;
        if (inside(r.x0, int(V(vy1)) - 16, r.x0 + 16, int(V(vy1)))) w = 5;
        if (w != m.barWidget) {
            m.barWidget = w;
            m.barAnchorY = my;
            m.barCount = 0;
        } else if (w && m.leftHeld) {
            const int c = m.barCount++;
            const int mn = 0, mxv = 64, L = r.ty1 - (r.ty0 + 3);
            if (w == 3) {
                const int dy = my - m.barAnchorY;
                if (dy != 0 && L > 0) {
                    pos += (mxv - mn) * dy / L;
                    if (dy > 0 && pos + page > mxv) pos = mxv - page;
                    if (pos < 0) pos = 0;
                    const int oldTop = r.top;
                    m.barAnchorY += Df2Map::bar(608, 216, vy1, pos, page).top - oldTop;
                }
            } else if (c <= 0 || c >= 10) {
                if (w == 1 && pos > mn) --pos;
                if (w == 5 && pos + page < mxv) ++pos;
                if (w == 2) pos = std::max(0, pos - page);
                if (w == 4) {
                    pos += page;
                    if (pos + page > mxv) pos = mxv - page;
                    if (pos < 0) pos = 0;
                }
            }
        } else if (!m.leftHeld) {
            m.barCount = 0;
        }
    } else {
        m.barWidget = 0;
    }
    if (m.tab == 0 && m.leftHeld) {
        auto in = [&](float vx, float vy, float w) { return m.mouseX >= V(vx) && m.mouseX < V(vx) + w && m.mouseY >= V(vy) && m.mouseY < V(vy) + 16; };
        if (in(608, 668, V(32))) mapScale_ = std::max(0.25f, mapScale_ * 0.9f);   // + (0x4000 min)
        if (in(608, 700, V(32))) mapScale_ = std::min(3.6f, mapScale_ * 1.1f);    // - (0x39999 max)
        // Held (0x40d558..0x40d5cd): the drag target moves by the world delta
        // of the mouse (+0x24 x, +0x28 y; the height +0x2c stays).
        if (m.drag.kind && df2SelValid(m.drag) && (m.mouseX != m.lastX || m.mouseY != m.lastY)) {
            const double step = 1024.0 / 640.0 * mapScale_;
            const double dx = double(m.mouseX - m.lastX) * step, dy = -double(m.mouseY - m.lastY) * step;
            if (m.drag.kind == 4) {
                df2Wps_[size_t(m.drag.index)].x += dx;
                df2Wps_[size_t(m.drag.index)].y += dy;
            } else if (m.drag.kind == 3) {
                MissionObject& mo = mission_.objects[size_t(m.drag.index)];
                mo.x += dx;
                mo.y += dy;
                for (auto& rp : route_)
                    if (rp.object == &mo) { rp.x += dx; rp.y += dy; }
                for (auto& o : world_.objects)
                    if (o.missionIndex == m.drag.index) { o.pos.x += float(dx); o.pos.z -= float(dy); }
            }
        }
    }
    if (!m.leftHeld) m.drag = Df2Sel{};
    m.lastX = m.mouseX;
    m.lastY = m.mouseY;
}

void Game::drawDf2CommandMap(const Screen& s) {
    Df2Map& m = df2Map_;
    // Testing: OPENDF_CMAPTAB opens a tab (2 Team selects the first squad
    // member); OPENDF_CMAPTEST runs "FRAME OP ARGS" commands separated by
    // ';' (frames of the map, mouse in its 640x480 pixels): m X Y move,
    // d X Y left press, u X Y release, k NAME a key (SDL name), t TEXT typed
    // into the name prompt.
    if (!m.testInit) {
        m.testInit = true;
        if (const char* t = std::getenv("OPENDF_CMAPTAB")) {
            df2MapEnter(std::atoi(t));
            for (size_t i = 0; i < world_.objects.size() && m.tab == 2; ++i)
                if (world_.objects[i].person && world_.objects[i].alive() && world_.objects[i].side == 1) { m.sel = Df2Sel{2, int(i)}; break; }
        }
        if (const char* t = std::getenv("OPENDF_CMAPTEST")) {
            std::stringstream all(t);
            std::string one;
            while (std::getline(all, one, ';')) {
                std::istringstream is(one);
                Df2Map::TestCmd c{};
                if (!(is >> c.frame >> c.op)) continue;
                if (c.op == 'k' || c.op == 't') { std::getline(is >> std::ws, c.text); }
                else is >> c.x >> c.y;
                m.test.push_back(c);
            }
        }
    }
    if (!m.entered) df2MapEnter(m.tab);
    df2MapFrame();
    if (!m.loaded) {
        m.loaded = true;
        auto pcx = [&](const char* n, bool key) { return hud_.loadImage(vfs_, n, false, key); };
        m.tile = pcx("CTILE.PCX", false);
        m.tile2 = pcx("CTILE2.PCX", false);
        m.edge = pcx("DF2_EDGE.PCX", true);
        m.corner = pcx("DF2_CRNR.PCX", true);
        m.end1 = pcx("DF2_END1.PCX", true);
        m.midl = pcx("DF2_MIDL.PCX", true);
        m.end2 = pcx("DF2_END2.PCX", true);
        m.iconWay = pcx("DF2_WAY.PCX", true);
        m.iconDepth = pcx("DF2_DPTH.PCX", true);
        m.iconCentre = pcx("DF2_CENT.PCX", true);
        m.iconContour = pcx("DF2_CNTR.PCX", true);
        m.bt09 = pcx("DF2_BT09.PCX", true);
        m.bt10 = pcx("DF2_BT10.PCX", true);
        // The Weapons tab's pictures: 0x443320, the HUD's WEAP sheet through
        // the amber palette 0x9e0128; the map runs at 640, so WEAP640.
        m.weap = hud_.loadAmberImage(vfs_, "WEAP640.PCX");
        // Cursors: the pixels are an alpha map (0x40c250), blended in green,
        // white with the W tool.
        auto cursor = [&](const char* n) {
            std::vector<uint8_t> d;
            IndexedImage img;
            if (!vfs_.read(n, d) || !decodePcx(d, img)) return HudImage{};
            std::vector<uint8_t> rgba(img.pixels.size() * 4, 255);
            for (size_t i = 0; i < img.pixels.size(); ++i) rgba[i * 4 + 3] = img.pixels[i];
            return hud_.makeImage(img.width, img.height, rgba);
        };
        m.cursor = cursor("DF2CURSA.PCX");
        m.cross = cursor("DF2CROSA.PCX");
        m.hand = cursor("DF2HANDA.PCX");
        m.wcur = cursor("DF2WCURA.PCX");
        // f_LG%i with i = min(width, 800) (0x45c110), the width 640 while
        // the map is open (0x45da2a); F_LG640 is also the only f_LG in
        // DF2.PFF (an F_LG800 load would fail and leave the font empty,
        // 0x4197e0 frees it first via 0x4195c0).
        m.big = hud_.loadSheetFont(vfs_, "F_LG640.PCX");
    }
    const float k = df2MapK(s), ox = (s.w - 640.f * k) * 0.5f, oy = (s.h - 480.f * k) * 0.5f;
    // f_sml (F_SML%i, the width capped at 800) is drawn 1:1 in the mode's
    // pixels while the layout scales with the mode (0x44f050: 1024-virtual
    // -> surface); the map always runs at 640x480 (Game::run, 0x45da2a), so
    // hudMode_ is 640 here and kt = k: F_SML640 1:1 in this 640 frame.
    const float kt = k * 640.f / float(std::max(640, hudMode_));
    auto V = [](float v) { return std::floor(v * 0.625f); };   // virtual -> 640
    auto X = [&](float x) { return ox + x * k; };
    auto Y = [&](float y) { return oy + y * k; };
    const Color cHead = rgb(0xc0, 0x60, 0), cSel = rgb(0xff, 0x90, 0x20), cBlack = rgb(0, 0, 0);
    // A rectangle (640 px) tiled from screen (0, 0) with a 128x128 tile.
    auto tiled = [&](const HudImage& t, float x0, float y0, float x1, float y1) {
        if (!t.texture) return;
        for (float ty = std::floor(y0 / 128.f) * 128.f; ty < y1; ty += 128.f)
            for (float tx = std::floor(x0 / 128.f) * 128.f; tx < x1; tx += 128.f) {
                const float a0 = std::max(x0, tx), b0 = std::max(y0, ty), a1 = std::min(x1, tx + 128.f), b1 = std::min(y1, ty + 128.f);
                if (a1 <= a0 || b1 <= b0) continue;
                hud_.imageUV(t.texture, X(a0), Y(b0), (a1 - a0) * k, (b1 - b0) * k, (a0 - tx) / 128.f, (b0 - ty) / 128.f, (a1 - tx) / 128.f,
                             (b1 - ty) / 128.f);
            }
    };
    // A frame of a sheet of 4 stacked frames.
    auto frame = [&](const HudImage& im, int f, float x, float y) {
        if (!im.texture) return;
        const float fh = float(im.height) / 4.f;
        hud_.imageUV(im.texture, X(x), Y(y), float(im.width) * k, fh * k, 0, float(f) * fh / float(im.height), 1, float(f + 1) * fh / float(im.height));
    };
    // A panel: CTILE fill, EDGE frames 0 top / 2 bottom / 3 left / 1 right in
    // 8 px steps, CRNR 0-3 at the corners, 3 px outside the fill.
    auto panel = [&](float vx0, float vy0, float vx1, float vy1) {
        const float x0 = V(vx0), y0 = V(vy0), x1 = V(vx1), y1 = V(vy1);
        tiled(m.tile, x0, y0, x1, y1);
        const float L = x0 - 3, T = y0 - 3, R = x1 + 3, B = y1 + 3;
        for (float x = L + 8; x < R - 8; x += 8) { frame(m.edge, 0, x, T); frame(m.edge, 2, x, B - 8); }
        for (float y = T + 8; y < B - 8; y += 8) { frame(m.edge, 3, L, y); frame(m.edge, 1, R - 8, y); }
        frame(m.corner, 0, L, T);
        frame(m.corner, 1, R - 8, T);
        frame(m.corner, 2, L, B - 8);
        frame(m.corner, 3, R - 8, B - 8);
    };
    // The background over the whole window (the 640x480 tiles continued past
    // its sides: an amenity), then the panels.
    {
        const HudImage& t = m.tile2;
        if (t.texture)
            for (float ty = -std::ceil(oy / (128.f * k)) * 128.f; Y(ty) < s.h; ty += 128.f)
                for (float tx = -std::ceil(ox / (128.f * k)) * 128.f; X(tx) < s.w; tx += 128.f)
                    hud_.image(t.texture, X(tx), Y(ty), 128.f * k, 128.f * k);
    }
    panel(32, 124, 992, 168);
    panel(32, 184, 656, 736);
    panel(672, 184, 992, 736);
    panel(12, 12, 588, 100);
    panel(600, 12, 1012, 100);
    // Last three chat / system lines (x 44 / 632, y 24, 44, 64), own colours.
    auto three = [&](const std::deque<Df2Line>& h, float vx) {
        for (int row = 0; row < 3; ++row) {
            const size_t e = size_t(2 - row);
            if (e < h.size()) hud_.text(X(V(vx)), Y(V(24 + 20 * float(row))), h[e].text, h[e].colour, kt);
        }
    };
    three(df2Chat_, 44);
    three(df2Sys_, 632);
    // Tabs (Cmap CMAP_MENU000..004), f_LG, spread over x 25..615 at y 80;
    // all five are shown in single player (no tab has flag 1 or 2).
    std::string tabs[5];
    float total = 0;
    for (int i = 0; i < 5; ++i) {
        char key[24];
        std::snprintf(key, sizeof key, "CMAP_MENU%03d", i);
        static const char* kDef[5] = {"Map", "Players", "Team", "Weapons", "Mission"};
        tabs[i] = df2Text("Cmap", key, kDef[i]);
        total += Hud::sheetWidth(m.big, tabs[i]);
    }
    const float gap = (590.f - total) / 5.f;
    float tx = 25.f + gap / 2;
    for (int i = 0; i < 5; ++i) {
        hud_.sheetText(m.big, X(tx), Y(80), tabs[i], i == m.tab ? cSel : rgb(0x60, 0x30, 0), k);
        tx += Hud::sheetWidth(m.big, tabs[i]) + gap;
    }
    // A text button: END1 + MIDL ... + END2, frame = state, text centred.
    auto textButton = [&](float vx, float vy, float vw, const std::string& label, bool on, Color offC, Color onC) {
        const float x0 = V(vx), y0 = V(vy), x1 = x0 + V(vw);
        const int f = on ? 1 : 0;
        frame(m.end1, f, x0, y0);
        for (float x = x0 + 8; x < x1 - 8; x += 8) frame(m.midl, f, x, y0);
        frame(m.end2, f, x1 - 8, y0);
        const float w = hud_.textWidth(label, kt);
        hud_.text(X((x0 + x1) / 2) - w / 2, Y(y0 + 2), label, on ? onC : offC, kt);
    };
    // An icon button (flags 0x80 | icon, 16x16 px, no frame): frame state +
    // 2 while the mouse is over it.
    auto icon = [&](const HudImage& im, float x, float y, bool on) {
        const bool hover = m.mouseX >= x && m.mouseX <= x + 16 && m.mouseY >= y && m.mouseY <= y + 16;
        frame(im, (on ? 1 : 0) + (hover ? 2 : 0), x, y);
    };
    // f_sml text with a black shadow at +1 (rows of the lists).
    auto shadowed = [&](float x, float y, const std::string& t, Color c) {
        hud_.text(X(x + 1), Y(y + 1), t, cBlack, kt);
        hud_.text(X(x), Y(y), t, c, kt);
    };
    // A list's scroll bar (creator 0x40c660 -> 0x40c3f0, drawn by 0x40c7d0).
    // The up arrow (DF2_BT09) at (x, y0), the down arrow (BT10) at (x, y1 -
    // its height); the track record (+0x10..+0x1c) is (x, y0 + 16 - 1) -
    // (x + 16, y1 - 16) (the arrows are 16 wide, 64 / 4 = 16 high: 0x40ce50
    // stores h >> 2). 0x40c7d0: the outline 0x450560 (x0, ty0 + 2)-(x1 - 1,
    // ty1 + 1) and the thumb 0x4505e0 (x0 + 1, top)-(x1 - 2, bottom) in
    // 0xc06000 filled 0x683000, L = ty1 - (ty0 + 3), top = (pos - min)·L /
    // (max - min) + ty0 + 3, bottom = (pos - min + page)·L / (max - min) +
    // ty0 + 3 clamped to ty1 (integer division). Both lists: min 0, max 64.
    auto scrollBar = [&](float vx, float vy0, float vy1, int pos, int page) {
        icon(m.bt09, V(vx), V(vy0), false);
        icon(m.bt10, V(vx), V(vy1) - 16, false);
        const Df2Map::Bar r = Df2Map::bar(vx, vy0, vy1, pos, page);
        const Color c0 = rgb(0xc0, 0x60, 0), c1 = rgb(0x68, 0x30, 0);
        auto outline = [&](int x0, int y0, int x1, int y1, Color c) {   // 0x450560: four lines, ends inclusive
            hud_.rect(X(float(x0)), Y(float(y0)), float(x1 - x0 + 1) * k, k, c);
            hud_.rect(X(float(x0)), Y(float(y1)), float(x1 - x0 + 1) * k, k, c);
            hud_.rect(X(float(x0)), Y(float(y0)), k, float(y1 - y0 + 1) * k, c);
            hud_.rect(X(float(x1)), Y(float(y0)), k, float(y1 - y0 + 1) * k, c);
        };
        outline(r.x0, r.ty0 + 2, r.x1 - 1, r.ty1 + 1, c0);
        // 0x4505e0: fill rows top + 1 .. bottom - 1 in the fill colour, then the outline.
        if (r.bottom - r.top > 1) hud_.rect(X(float(r.x0 + 1)), Y(float(r.top + 1)), float(r.x1 - 2 - (r.x0 + 1) + 1) * k, float(r.bottom - r.top - 1) * k, c1);
        outline(r.x0 + 1, r.top, r.x1 - 2, r.bottom, c0);
    };
    // The selected object's panel (0x40e190 at (684, 200)): f_sml 0xc06000,
    // headings at x 704, values at 724.
    auto infoPanel = [&]() {
        const Df2Sel sel = m.sel;
        double px = 0, py = 0;
        if (!df2SelPos(sel, px, py)) return;
        auto head = [&](float vy, const std::string& t) { hud_.text(X(V(704)), Y(V(vy)), t, cHead, kt); };
        auto value = [&](float vy, const std::string& t) { hud_.text(X(V(724)), Y(V(vy)), t, cHead, kt); };
        const int item = df2SelItem(sel);
        const ItemDef* it = sel.kind == 3 ? items_.byMissionType(uint32_t(item)) : nullptr;
        // 0x40e190: an object of the player array (mission group 3, any team)
        // gets the person panel 0x40e250; the rest go by class.
        const bool person = sel.kind == 1 || (sel.kind == 2 && world_.objects[size_t(sel.index)].person && world_.objects[size_t(sel.index)].missionGroup == 3);
        if (person) {
            // 0x40e250: Selected, name, team name by +0x68 (the BMS side, the
            // player's included: 0 STRCLI19, 1 STRCLI05, 2 STRCLI06, 3
            // STRCLI17, 4 STRCLI18; any other value leaves the buffer 0x4ca878
            // as the last one was), then for the player's team: grid, plan,
            // weapons.
            head(200, df2Text("Cmap", "CMAP_HEADING_SELECTED", "Selected"));
            value(224, df2SelName(sel));
            static const char* kTeamKey[5] = {"STRCLI19", "STRCLI05", "STRCLI06", "STRCLI17", "STRCLI18"};
            static const char* kTeamDef[5] = {"Green Team", "Blue Team", "Red Team", "Yellow Team", "Violet Team"};
            const int team = sel.kind == 1 ? kPlayerSide : world_.objects[size_t(sel.index)].side;
            if (team >= 0 && team <= 4) m.teamName = df2Text("Client", kTeamKey[team], kTeamDef[team]);
            value(248, m.teamName);
            if (!df2SelOnPlayerTeam(sel)) return;
            value(272, df2GridName(px, py));
            head(320, df2Text("Cmap", "CMAP_HEADING_PLAN", "Plan"));
            // The plan: controller +0x14, empty -> CMAP_PLANNONE. The player's
            // is a multiplayer plan (empty here); a teammate's is the
            // TeamOrders L_* text 0x403380 copies there when its order changes.
            std::string plan;
            if (sel.kind == 2)
                if (auto pt = m.planText.find(sel.index); pt != m.planText.end()) plan = pt->second;
            value(344, plan.empty() ? df2Text("Cmap", "CMAP_PLANNONE", "None") : plan);
            head(392, df2Text("Cmap", "CMAP_HEADING_WEAPONS", "Weapons"));
            // Each non-zero controller byte +0xb6..+0xba. A teammate's are
            // never written (see the Team tab), so only the heading shows.
            float y = 416;
            if (sel.kind == 1) {
                const int types[5] = {df2Loadout_.primary, df2Loadout_.secondary, df2Loadout_.gear, df2Loadout_.sidearm, df2Loadout_.grenade};
                for (int t : types)
                    if (t) { value(y, df2TypeName(t)); y += 24; }
            }
            return;
        }
        const char* kindKey = nullptr;
        const char* kindDef = nullptr;
        if (sel.kind == 4 || item == 6005 || (it && it->type == "marker")) { kindKey = "CMAP_ITEM_WAYPOINT"; kindDef = "Waypoint"; }   // 0x40e800
        else if (it && it->type == "building") { kindKey = "CMAP_ITEM_BUILDING"; kindDef = "Building"; }                   // 0x40e9d0
        else if (it && it->type == "vehicle") { kindKey = "CMAP_ITEM_VEHICLE"; kindDef = "Vehicle"; }                      // 0x40ea90
        if (!kindKey) return;   // enemy soldiers, items: nothing
        head(200, df2Text("Cmap", kindKey, kindDef));
        value(224, df2SelName(sel));
        value(248, df2GridName(px, py));
        // Item 6089: the owner only when it is not the player (multiplayer).
    };
    auto actionButtons = [&]() {
        const auto b = df2ActionButtons();
        for (size_t i = 0; i < b.size(); ++i) textButton(688, 696 - 32 * float(i), 280, b[i].first, false, cHead, cHead);
    };
    m.picks.clear();
    if (m.tab == 0) {
        // Map tab: the radar renderer in (48, 200)-(640, 656), north-up,
        // centred on the player + pan, the radar's scale shared (1.6 m a
        // pixel at 640 by default).
        const float x0 = X(V(48)), y0 = Y(V(200)), x1 = X(V(640)), y1 = Y(V(656));
        const float mPerPx = 1024.f / 640.f * mapScale_ / k;
        const Vec3 c = player_.feet + Vec3{m.panX, 0, m.panZ};
        const float mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
        const double px = player_.missionX() + m.panX, py = player_.missionY() - m.panZ;
        hud_.clip(x0, y0, x1 - x0, y1 - y0);
        if (!df2Elevation_)
            if (const GLuint tex = terrain_.radarTexture() ? terrain_.radarTexture() : terrain_.colorTexture()) {
                const float ext = terrain_.mapExtent(), hw = (x1 - x0) * 0.5f * mPerPx, hh = (y1 - y0) * 0.5f * mPerPx;
                hud_.imageUV(tex, x0, y0, x1 - x0, y1 - y0, (c.x - hw) / ext, (c.z - hh) / ext, (c.x + hw) / ext, (c.z + hh) / ext);
            }
        // The elevation picture ([0x765600]) and / or the contours (0xbd0588,
        // flag 0x2000), made per 640 pixel as 0x421510 does.
        if (df2Elevation_ || m.contours) {
            const int w = int(V(640) - V(48)), h = int(V(656) - V(200));
            df2Picture(m.pic, w, h, px, py, 1024.0 / 640.0 * mapScale_, df2Elevation_, m.contours);
            hud_.image(m.pic.texture, x0, y0, x1 - x0, y1 - y0);
        }
        auto toMap = [&](double x, double y) { return std::pair<float, float>{mx + float(x - px) / mPerPx, my - float(y - py) / mPerPx}; };
        // Grid (0x40e...): 100 m lines in orange, labels white at the cell middle.
        if (m.toggles[0]) {
            const double halfW = (x1 - x0) * 0.5 * mPerPx, halfH = (y1 - y0) * 0.5 * mPerPx;
            for (double gx = std::ceil((px - halfW) / 100) * 100; gx <= px + halfW; gx += 100) {
                const float sx = toMap(gx, py).first;
                hud_.rect(sx, y0, k, y1 - y0, kDf2Amber);
                const int i = int(std::floor(gx / 100)) + 10;
                std::string lab;
                if (i >= 0 && i < 26) lab = std::string(1, char('A' + i));
                else if (i >= 26) lab = std::string(1, char('@' + i / 26)) + char('A' + i % 26);
                hud_.text(sx + 50 / mPerPx - hud_.textWidth(lab, kt) / 2, y0 + k, lab, kHudWhite, kt);
            }
            for (double gy = std::floor((py + halfH) / 100) * 100; gy >= py - halfH; gy -= 100) {
                const float sy = toMap(px, gy).second;
                hud_.rect(x0, sy, x1 - x0, k, kDf2Amber);
                const int row = ((10 - int(std::floor((gy - 100) / 100))) % 100 + 100) % 100;
                hud_.text(x0 + 2 * k, sy + 50 / mPerPx - 4 * k, std::to_string(row), kHudWhite, kt);
            }
        }
        const float blip = 6.f * k;
        auto teamColour = [](int side) {
            return side == 1 ? rgb(0x10, 0x40, 0xff) : side == 2 ? rgb(0xff, 0, 0) : side == 3 ? rgb(0xff, 0xff, 0) : side == 4 ? rgb(0xff, 0xff, 0xff) : rgb(0, 0xff, 0);
        };
        // Blips at double size, a black pass at +-1 pixel first (flag 0x1000);
        // each is recorded for picking (0x4226c0, within the clip +- 8 px).
        // The selected one blinks (flag 0x4000, 0x4229e5 / 0x422a1f): while
        // (frame + 8) & 0x20, its outline is its colour halved and the colour
        // itself | 0x808080.
        auto blipAt = [&](float x, float y, int type, Color col, float heading, bool dead, Df2Sel id) {
            if (x > x0 - 8 * k && x < x1 + 8 * k && y > y0 - 8 * k && y < y1 + 8 * k) m.picks.push_back({(x - ox) / k, (y - oy) / k, id});
            Color outline = rgb(1, 1, 1);
            if (id.kind && id == m.sel && ((ticks_ + 8) & 0x20)) {
                auto ch = [](float v) { return int(std::lround(v * 255.f)); };
                outline = rgb((ch(col.r) >> 1) & 0x7f, (ch(col.g) >> 1) & 0x7f, (ch(col.b) >> 1) & 0x7f);
                col = rgb(ch(col.r) | 0x80, ch(col.g) | 0x80, ch(col.b) | 0x80);
            }
            for (auto [dx, dy] : {std::pair{-k, 0.f}, {k, 0.f}, {0.f, -k}, {0.f, k}})
                drawMapMarker(s, x + dx, y + dy, type, outline, heading, dead, blip);
            drawMapMarker(s, x, y, type, col, heading, dead, blip);
        };
        // The mission route (0x4224c0, not in multiplayer modes), 0xff8000,
        // first four black (0x010101) copies at x +- 1 / y +- 1 (flag 0x1000).
        if (m.toggles[3])
            for (size_t i = 0; i + 1 < route_.size(); ++i) {
                auto [ax, ay] = toMap(route_[i].x, route_[i].y);
                auto [bx, by] = toMap(route_[i + 1].x, route_[i + 1].y);
                for (auto [dx, dy] : {std::pair{-k, 0.f}, {k, 0.f}, {0.f, -k}, {0.f, k}})
                    hud_.line(ax + dx, ay + dy, bx + dx, by + dy, k, rgb(1, 1, 1));
                hud_.line(ax, ay, bx, by, k, kDf2Amber);
            }
        if (m.toggles[2] && o_.gpsIcons > 0)
            for (size_t i = 0; i < world_.objects.size(); ++i) {
                const WorldObject& o = world_.objects[i];
                if (o.missionGroup != 3 || !o.person || o.removed) continue;
                if (o_.gpsIcons == 1 && !o.mapMarker) continue;
                const Vec3 at = world_.nearest(o.pos, player_.feet);
                blipAt(mx + (at.x - c.x) / mPerPx, my + (at.z - c.z) / mPerPx, 5, teamColour(o.side), o.heading, o.dead, Df2Sel{2, int(i)});
            }
        forDf2Shapes([&](const Df2Shape& d) {
            const ItemDef* it = items_.byMissionType(d.mission->type);
            const std::string kind = it ? it->type : std::string();
            if ((kind == "building" && !m.toggles[5]) || (kind == "vehicle" && !m.toggles[4]) || (kind == "marker" && !m.toggles[3])) return;
            const Df2Sel id{3, int(d.mission - mission_.objects.data())};
            // The HUD waypoint blinks (0x421dc0): drawn while frame & 0x10.
            if (df2HudWpIs(id) && !(ticks_ & 16)) return;
            float bx, by;
            if (d.world) {
                const Vec3 at = world_.nearest(d.world->pos, player_.feet);
                bx = mx + (at.x - c.x) / mPerPx;
                by = my + (at.z - c.z) / mPerPx;
            } else std::tie(bx, by) = toMap(d.mission->x, d.mission->y);
            blipAt(bx, by, d.shape, teamColour(d.side), 0, false, id);
        });
        if (m.toggles[1] && m.toggles[3])
            for (const auto& rp : route_) {
                auto [bx, by] = toMap(rp.x, rp.y);
                hud_.text(bx + blip, by - 4 * k, rp.name, kDf2Amber, kt);
            }
        // Player waypoints (pool loop 0x421e48, Waypoints filter): shape 3
        // (diamond) in team 0's green 0x00ff00; names (+0x54) in the blip
        // colour at x + size with Names; the HUD waypoint blinks (0x421e60).
        if (m.toggles[3])
            for (size_t i = 0; i < df2Wps_.size(); ++i) {
                const Df2Waypoint& w = df2Wps_[i];
                const Df2Sel id{4, int(i)};
                if (!w.used || (df2HudWpIs(id) && !(ticks_ & 16))) continue;
                auto [bx, by] = toMap(w.x, w.y);
                blipAt(bx, by, 3, teamColour(0), 0, false, id);
                if (m.toggles[1]) hud_.text(bx + blip, by - 4 * k, w.name.empty() ? df2Text("Cmap", "CMAP_ITEM_UNKNOWN", "Unknown") : w.name, teamColour(0), kt);
            }
        blipAt(mx - m.panX / mPerPx, my - m.panZ / mPerPx, 5, teamColour(0), player_.yaw, false, Df2Sel{1, 0});
        hud_.unclip();
        // Buttons (width 152 virtual): Gridlines, Names, People, Waypoints,
        // Vehicles, Buildings; the icon buttons and + / -.
        static const char* kLabels[6] = {"Gridlines", "Names", "People", "Waypoints", "Vehicles", "Buildings"};
        static const char* kKeys[6] = {"CMAP_BTN_GRIDLINES", "CMAP_BTN_NAMES", "CMAP_BTN_PEOPLE", "CMAP_BTN_WAYPOINTS", "CMAP_BTN_VEHICLES",
                                       "CMAP_BTN_BUILDINGS"};
        static const float kPos[6][2] = {{48, 668}, {48, 700}, {208, 668}, {208, 700}, {368, 668}, {368, 700}};
        for (int i = 0; i < 6; ++i)
            textButton(kPos[i][0], kPos[i][1], 152, df2Text("Cmap", kKeys[i], kLabels[i]), m.toggles[i], rgb(0x68, 0x30, 0), cSel);
        icon(m.iconWay, V(528), V(668), m.wButton);
        icon(m.iconDepth, V(528), V(700), m.dButton);
        icon(m.iconCentre, V(568), V(668), false);
        icon(m.iconContour, V(568), V(700), m.contours);
        textButton(608, 668, 32, "+", true, cHead, cHead);
        textButton(608, 700, 32, "-", true, cHead, cHead);
        infoPanel();
        actionButtons();
    } else if (m.tab == 1) {
        // Players tab (0x40eb50): the player array's players (+0x64 & 1):
        // in single player the player alone, no buttons on its own row.
        static const char* kHead[5][3] = {{"CMAP_HEADING_NAME", "Name", "80"}, {"CMAP_HEADING_MUTED", "Muted", "272"},
                                          {"CMAP_HEADING_TALKTO", "Talk to", "360"}, {"CMAP_HEADING_LISTENING", "Listening", "448"},
                                          {"CMAP_HEADING_WAYPOINTS", "Waypoints", "536"}};
        for (int i = 0; i < 5; ++i) {
            const std::string t = df2Text("Cmap", kHead[i][0], kHead[i][1]);
            const float x = V(float(std::atoi(kHead[i][2])));
            hud_.text(X(i == 0 ? x : x) - (i == 0 ? 0 : hud_.textWidth(t, kt) / 2), Y(V(196)), t, cHead, kt);
        }
        scrollBar(608, 216, 672, m.scrollPlayers, 16);
        const int rows = 1 - m.scrollPlayers;   // the player only
        for (int r = 0; r < std::min(rows, 16); ++r) {
            const float x0 = V(80), y0 = V(224 + 28 * float(r));
            const std::string name = df2SelName(Df2Sel{1, 0});
            const bool selected = m.sel.kind == 1;
            const std::string num = std::to_string(m.scrollPlayers + r + 1);
            const float nw = hud_.textWidth(num, kt) / k;
            hud_.text(X(x0 - 11 - nw), Y(y0 + 1), num, cBlack, kt);   // right-aligned
            hud_.text(X(x0 - 12 - nw), Y(y0), num, selected ? cSel : cHead, kt);
            if (selected) {
                hud_.text(X(x0), Y(y0), "[", kDf2Red, kt);
                hud_.text(X(V(82) + hud_.textWidth(name, kt) / k), Y(y0), "]", kDf2Red, kt);
            }
            shadowed(x0, y0, name, selected ? cSel : cHead);
        }
        infoPanel();
        actionButtons();
    } else if (m.tab == 2) {
        // Team tab (0x40f190): the rows of the list builder 0x40ca80 (player
        // array, same team, self included) from (80, 224), 28 apart, skipping
        // the scroll position ([0xbd0598]). Heading "%s/%s/%s" (0x4a52a8) of
        // Name / Weapon / Plan centred at (344, 196). Each row (0x40f2c8):
        // the number (scroll + r + 1) right-aligned at x0 - 12 with a black
        // shadow at +1, the name shadowed at x0, [ ] in red round the selected
        // one, then the weapon at x0 + W·208/1024 (the controller's +0xb6
        // type's name) and the plan at x0 + W·384/1024: a player (+0x64 & 1)
        // its plan text (controller +0x14), the others TeamOrders S_HPHF /
        // S_HPCM / S_HP by the order byte +0xbb, else S_NONE; empty ->
        // CMAP_PLANNONE. Selected 0xff9020, else 0xc06000. The selected one's
        // buttons: the player "Set my plan to..." (plans: multiplayer, no
        // effect in single player), a teammate "Set teammate to..." with the
        // four orders (0x4113b0).
        {
            char hdr[96];
            std::snprintf(hdr, sizeof hdr, "%s/%s/%s", df2Text("Cmap", "CMAP_HEADING_NAME", "Name").c_str(),
                          df2Text("Cmap", "CMAP_HEADING_WEAPON", "Weapon").c_str(), df2Text("Cmap", "CMAP_HEADING_PLAN", "Plan").c_str());
            hud_.text(X(V(344)) - hud_.textWidth(hdr, kt) / 2, Y(V(196)), hdr, cHead, kt);
        }
        scrollBar(608, 216, 616, m.scrollTeam, 14);
        int r = 0, n = 0;
        auto line = [&](Df2Sel id, const std::string& name, const std::string& weapon, const std::string& plan) {
            if (n++ < m.scrollTeam || r >= 14) return;
            const float x0 = V(80), y0 = V(224 + 28 * float(r));
            const bool selected = id == m.sel;
            const Color c = selected ? cSel : cHead;
            const std::string num = std::to_string(m.scrollTeam + r + 1);
            const float nw = hud_.textWidth(num, kt) / k;
            hud_.text(X(x0 - 11 - nw), Y(y0 + 1), num, cBlack, kt);
            hud_.text(X(x0 - 12 - nw), Y(y0), num, c, kt);
            shadowed(x0, y0, name, c);
            if (selected) {
                hud_.text(X(x0), Y(y0), "[", kDf2Red, kt);
                hud_.text(X(V(82) + hud_.textWidth(name, kt) / k), Y(y0), "]", kDf2Red, kt);
            }
            shadowed(x0 + std::floor(640.f * 208.f / 1024.f), y0, weapon, c);
            shadowed(x0 + std::floor(640.f * 384.f / 1024.f), y0, plan, c);
            ++r;
        };
        static const char* kOrderKey[4] = {"CMAP_BTN_NOORDER", "CMAP_BTN_HPHF", "CMAP_BTN_HPCM", "CMAP_BTN_HP"};
        static const char* kOrderDef[4] = {"No orders", "Hold Position, Hold Fire", "Hold Position, Cover Me", "Hold Position"};
        static const char* kShortKey[4] = {"S_NONE", "S_HPHF", "S_HPCM", "S_HP"};
        static const char* kShortDef[4] = {"None", "HPHF", "HPCM", "HP"};
        const std::string planNone = df2Text("Cmap", "CMAP_PLANNONE", "None");
        // The player's row first (INFERRED: the player is the first object of
        // its team in mission group 3 in the shipped missions).
        line(Df2Sel{1, 0}, df2SelName(Df2Sel{1, 0}), df2TypeName(df2Loadout_.primary), planNone);
        for (size_t i = 0; i < world_.objects.size(); ++i) {
            // Teammates: no code writes a mission soldier's controller bytes
            // +0xb6..+0xba (writers: the network 0x408eca / 0x40a071 /
            // 0x40a60b and the local player's join copy 0x454273), so the
            // weapon column shows type 0, "None".
            if (!df2SelOnPlayerTeam(Df2Sel{2, int(i)}) || !world_.objects[i].alive()) continue;
            const int ord = std::clamp(ai_.order(int(i)), 0, 3);
            line(Df2Sel{2, int(i)}, df2SelName(Df2Sel{2, int(i)}), df2TypeName(0), df2Text("TeamOrders", kShortKey[ord], kShortDef[ord]));
        }
        if (m.sel.kind == 1) {
            hud_.text(X(V(80)), Y(V(640)), df2Text("Cmap", "CMAP_SETPLAN", "Set my plan to..."), cHead, kt);
            static const char* kPlans[2][4] = {{"CMAP_PLAN001", "CMAP_PLAN002", "CMAP_PLAN003", "CMAP_PLAN004"},
                                               {"CMAP_PLAN011", "CMAP_PLAN012", "CMAP_PLAN013", "CMAP_PLAN014"}};
            for (int r = 0; r < 2; ++r) {
                hud_.text(X(V(56)), Y(V(664 + 32 * r)), "-", cHead, kt);
                for (int c = 0; c < 4; ++c)
                    textButton(100 + 136 * c, 664 + 32 * r, 124, df2Text("Cmap", kPlans[r][c], kPlans[r][c]), false, cHead, cSel);
            }
        } else if (m.sel.kind == 2 && df2SelOnPlayerTeam(m.sel)) {
            hud_.text(X(V(80)), Y(V(640)), df2Text("Cmap", "CMAP_SETTEAMMATE", "Set teammate to..."), cHead, kt);
            static const float kOrderPos[4][2] = {{56, 664}, {344, 664}, {56, 696}, {344, 696}};
            const int cur = ai_.order(m.sel.index);
            for (int i = 0; i < 4; ++i)
                textButton(kOrderPos[i][0], kOrderPos[i][1], 280, df2Text("Cmap", kOrderKey[i], kOrderDef[i]), i == cur, cHead, cSel);
        }
        infoPanel();
        actionButtons();
    } else if (m.tab == 3) {
        // Weapons tab (0x40fbe0), f_sml in colour -1 (0x40fc67): the HUD
        // font draw (0x4198e0) stores or blends the colour dword as given,
        // with no special case, so 0xffffffff is white: the current loadout left at y 212 + 96k, the requested
        // one right with < > (DF2_BT09 / BT10) at the y they were made at,
        // its pictures and names at a y that advances only after a filled
        // slot; "Request Weapons" at (336, 696). No selected-object panel.
        static const char* kSlotKey[5] = {"CMAP_WEAPON_PRIMARY", "CMAP_WEAPON_SECONDARY1", "CMAP_WEAPON_SECONDARY2", "CMAP_WEAPON_SIDEARM",
                                          "CMAP_WEAPON_GRENADE"};
        static const char* kSlotDef[5] = {"Primary", "Secondary 1", "Secondary 2", "Sidearm", "Grenade"};
        const Color white = rgb(0xff, 0xff, 0xff);
        auto picture = [&](int type, float x, float y) {
            if (!m.weap.texture || type < 0 || type >= 39 || kDf2TypeFrame[type] < 0) return;
            const float fh = float(m.weap.height / 22), H = float(m.weap.height), f = float(kDf2TypeFrame[type]);
            hud_.setBlend(5);
            hud_.imageUV(m.weap.texture, X(x), Y(y), float(m.weap.width) * k, fh * k, 0, f * fh / H, 1, (f + 1) * fh / H);
            hud_.setBlend(0);
        };
        int ry = 252;
        for (int slot = 0; slot < 5; ++slot) {
            const std::string label = df2Text("Cmap", kSlotKey[slot], kSlotDef[slot]);
            const float y = 212 + 96 * float(slot);
            if (m.current[slot]) {
                hud_.text(X(V(64)), Y(V(y)), label + ": " + df2TypeName(m.current[slot]), white, kt);
                picture(m.current[slot], V(64) + 32, V(y + 20));
            } else hud_.text(X(V(64)), Y(V(y)), label + ":", white, kt);
            icon(m.bt09, V(400), V(float(m.wbtnY[slot])), false);
            icon(m.bt10, V(428), V(float(m.wbtnY[slot])), false);
            if (m.request[slot]) {
                picture(m.request[slot], V(400) + 32, V(float(ry - 20)));
                hud_.text(X(V(464)), Y(V(float(ry - 40))), df2TypeName(m.request[slot]), white, kt);
                ry += 96;
            }
        }
        textButton(336, 696, 300, df2Text("Cmap", "CMAP_BTN_REQUESTWEAP", "Request Weapons"), false, cHead, cHead);
    } else if (m.tab == 4) {
        // Mission tab: the DF2BRIEF MBBRIEF text in (64, 216)-(608, 704),
        // colour 0xc06000; the box title CMAP_SETTINGS (0x410e60), its
        // settings n/a in single player.
        std::vector<uint8_t> raw;
        static TextTable brief;
        static bool loaded = false;
        if (!loaded) { loaded = true; if (vfs_.read("DF2BRIEF.BIN", raw)) brief.parse(raw); }
        const std::string cur = current_ >= 0 && current_ < int(missions_.size()) ? toUpper(missions_[size_t(current_)]) : std::string();
        const std::string key = cur.substr(0, std::min<size_t>(6, cur.size()));
        if (const std::string* t = brief.find("MBBRIEF", key)) {
            float y = Y(V(216));
            std::string line, word;
            const float maxW = (V(608) - V(64)) * k;
            auto emit = [&]() { hud_.text(X(V(64)), y, line, cHead, kt); y += 18.f * 0.625f * k; line.clear(); };
            for (char ch : *t + " ") {
                if (ch == '\r') continue;
                if (ch == '\n') { if (!word.empty()) { line += (line.empty() ? "" : " ") + word; word.clear(); } emit(); continue; }
                if (ch == ' ') {
                    const std::string cand = line.empty() ? word : line + " " + word;
                    if (hud_.textWidth(cand, kt) > maxW && !line.empty()) { emit(); line = word; }
                    else line = cand;
                    word.clear();
                    continue;
                }
                word += ch;
            }
            if (!line.empty()) emit();
        }
        hud_.text(X(V(720)), Y(V(232)), df2Text("Cmap", "CMAP_SETTINGS", "Game Settings"), kDf2Amber, kt);
        hud_.text(X(V(720)), Y(V(264)), "n/a", kDf2Amber, kt);
    }
    // The text prompt (0x4422b0 while 0x41ca70()): the darkened box with an
    // orange outline (0x45c160) at (50, y)-(974, y + 80), y = [0x65366c] - 160
    // with [0x65366c] = (H - b) · 768 / H (0x45cb40, integer), b the bottom
    // HUD strip's height (0x443e21: [0x835d10], set by the strip load
    // 0x443d8f): 464 at 640x480 (DHB640G 90 rows), 463 at 1024x768; the
    // prompt at (60, y + 16) and the typed text at (60, y + 46), f_sml
    // 0xff8000, "_" while frame & 0x30. The box 0x45c160(x 50, y, 1024 - 50,
    // y + 80): its x ends rounded down to 4 surface pixels (& 0xfffc), each
    // pixel halved ((c & 0xfe) >> 1, MMX 0x45c247) on the software path (the
    // other path, [0xa38de8] = 2, clears it to black), then the outline in
    // [0x65361c] = 0xff8000 through x1 - 1, y1 - 1.
    if (m.prompt) {
        int py = 464;
        if (df2Hud_.bottom.texture && hudMode_ > 0) {
            const int H = hudMode_ * 3 / 4;
            py = (H - df2Hud_.bottom.height) * 768 / H - 160;
        }
        const float bx0 = X(float(int(V(50)) & ~3)), by0 = Y(V(float(py))), bx1 = X(float(int(V(974)) & ~3)), by1 = Y(V(float(py + 80)));
        hud_.multiply(bx0, by0, bx1 - bx0, by1 - by0, {0.5f, 0.5f, 0.5f, 1});
        hud_.rect(bx0, by0, bx1 - bx0, k, kDf2Amber);
        hud_.rect(bx0, by1 - k, bx1 - bx0, k, kDf2Amber);
        hud_.rect(bx0, by0, k, by1 - by0, kDf2Amber);
        hud_.rect(bx1 - k, by0, k, by1 - by0, kDf2Amber);
        hud_.text(X(V(60)), Y(V(float(py + 16))), "Type in a name", kDf2Amber, kt);   // literal 0x4a4f50
        hud_.text(X(V(60)), Y(V(float(py + 46))), m.promptText + ((ticks_ & 0x30) ? "_" : ""), kDf2Amber, kt);
    }
    // The cursor (0x40c250): the W cursor in white anywhere on the Map tab
    // while the tool is on (0x40d60a), the hand while panning, the cross
    // over the map, else the arrow; all others green.
    const bool overMap = m.tab == 0 && m.mouseX >= V(48) && m.mouseX < V(640) && m.mouseY >= V(200) && m.mouseY < V(656);
    const bool wcur = m.tab == 0 && m.wTool;
    const HudImage& cur = wcur ? m.wcur : m.panning && overMap ? m.hand : overMap ? m.cross : m.cursor;
    if (cur.texture) {
        const float hx = wcur ? 6.f : m.panning && overMap ? 10.f : overMap ? 12.f : 0.f;
        const float hy = wcur ? 6.f : m.panning && overMap ? 10.f : overMap ? 11.f : 0.f;
        hud_.image(cur.texture, X(float(m.mouseX) - hx), Y(float(m.mouseY) - hy), float(cur.width) * k, float(cur.height) * k,
                   wcur ? Color{1, 1, 1, 1} : Color{0, 1, 0, 1});
    }
}

void Game::df2MapClick(const Screen& s, int x, int y, bool down, int button) {
    Df2Map& m = df2Map_;
    auto V = [](float v) { return std::floor(v * 0.625f); };
    if (button == 3) { m.panning = down; return; }   // right drag pans
    if (button == 1) m.leftHeld = down;
    if (!down || button != 1) return;
    auto in = [&](float vx, float vy, float w) { return x >= V(vx) && x < V(vx) + w && y >= V(vy) && y < V(vy) + 16; };
    // Tabs (the text rectangles, y 80).
    if (y >= 80 && y < 80 + std::max(1, m.big.height)) {
        float total = 0;
        std::string tabs[5];
        for (int i = 0; i < 5; ++i) {
            char key[24];
            std::snprintf(key, sizeof key, "CMAP_MENU%03d", i);
            static const char* kDef[5] = {"Map", "Players", "Team", "Weapons", "Mission"};
            tabs[i] = df2Text("Cmap", key, kDef[i]);
            total += Hud::sheetWidth(m.big, tabs[i]);
        }
        const float gap = (590.f - total) / 5.f;
        float tx = 25.f + gap / 2;
        for (int i = 0; i < 5; ++i) {
            const float w = Hud::sheetWidth(m.big, tabs[i]);
            if (x >= tx && x <= tx + w) { df2MapEnter(i); return; }
            tx += w + gap;
        }
    }
    // The action buttons of the selection (Map, Players, Team; group 2).
    if (m.tab <= 2) {
        const auto b = df2ActionButtons();
        for (size_t i = 0; i < b.size(); ++i)
            if (in(688, 696 - 32 * float(i), V(280))) { df2Action(b[i].second); return; }
    }
    if (m.tab == 1) {
        // Row click (0x40b80a): row 0 is the player while not scrolled away.
        const float w = hud_.textWidth(df2SelName(Df2Sel{1, 0}), 640.f / float(std::max(640, hudMode_)));
        if (m.scrollPlayers == 0 && x >= V(80) && x <= V(82) + w && y >= V(224) && y < V(252)) df2Select(Df2Sel{1, 0});
        return;
    }
    if (m.tab == 2) {
        // Team tab: pick a row (the player first), or give an order.
        if (y >= V(224) && y < V(616) && x >= V(80) && x < V(600)) {
            const int r = int((float(y) - V(224)) / V(28) + 0.0001f) + m.scrollTeam;
            if (r == 0) { df2Select(Df2Sel{1, 0}); return; }
            int row = 0;
            for (size_t i = 0; i < world_.objects.size(); ++i) {
                if (!df2SelOnPlayerTeam(Df2Sel{2, int(i)}) || !world_.objects[i].alive()) continue;
                if (++row == r) { df2Select(Df2Sel{2, int(i)}); return; }
            }
            return;
        }
        if (m.sel.kind == 2 && df2SelOnPlayerTeam(m.sel)) {
            static const float kOrderPos[4][2] = {{56, 664}, {344, 664}, {56, 696}, {344, 696}};
            for (int i = 0; i < 4; ++i)
                if (in(kOrderPos[i][0], kOrderPos[i][1], V(280)) && ai_.setOrder(m.sel.index, i)) {
                    // STRMISC34..37 in green, a system line (0x403380).
                    char key[16];
                    std::snprintf(key, sizeof key, "STRMISC%02d", 34 + i);
                    pushMessage(df2Text("Misc", key, "Acknowledged."), rgb(0, 0xff, 0));
                    // ... and TeamOrders L_NONE / L_HPHF / L_HPCM / L_HP to the plan (+0x14).
                    static const char* kLong[4] = {"L_NONE", "L_HPHF", "L_HPCM", "L_HP"};
                    m.planText[m.sel.index] = df2Text("TeamOrders", kLong[i], kLong[i]);
                }
        }
        return;
    }
    if (m.tab == 3) {
        // < (0x411740) / > (0x4117a0): step the requested type through the
        // 39 types, wrapping, to one whose category the slot accepts.
        for (int slot = 0; slot < 5; ++slot) {
            const int dir = in(400, float(m.wbtnY[slot]), 16) ? -1 : in(428, float(m.wbtnY[slot]), 16) ? 1 : 0;
            if (!dir) continue;
            int t = m.request[slot];
            for (int n = 0; n < 39; ++n) {
                t = (t + dir + 39) % 39;
                if (kDf2SlotMask[slot] & (1 << df2TypeCategory(t))) { m.request[slot] = t; break; }
            }
            return;
        }
        if (in(336, 696, V(300))) df2RequestWeapons();
        return;
    }
    if (m.tab != 0) return;
    static const float kPos[6][2] = {{48, 668}, {48, 700}, {208, 668}, {208, 700}, {368, 668}, {368, 700}};
    for (int i = 0; i < 6; ++i)
        if (in(kPos[i][0], kPos[i][1], V(152))) { m.toggles[i] = !m.toggles[i]; return; }
    if (in(528, 668, 16)) { m.wButton = !m.wButton; m.wTool = m.wButton; return; }        // W (0x411250)
    if (in(528, 700, 16)) { m.dButton = !m.dButton; df2Elevation_ = m.dButton; return; }   // D (0x411280)
    if (in(568, 668, 16)) { m.panX = m.panZ = 0; return; }                                 // >< re-centres
    if (in(568, 700, 16)) { m.contours = !m.contours; return; }                            // {{{ (0x411220)
    // The map (0x40d2d0): strictly inside (48, 200)-(640, 656).
    if (x > V(48) && x < V(640) && y > V(200) && y < V(656)) {
        const double step = 1024.0 / 640.0 * mapScale_;
        const double cx = (V(48) + V(640)) / 2.0, cy = (V(200) + V(656)) / 2.0;
        if (m.wTool) {
            // 0x422ea0 (the radar transform inverted), then 0x445e70(x, y,
            // "WP", player): the first free pool slot (none of 128 free:
            // nothing, select(0)); then the name prompt, and W goes off.
            const double wx = player_.missionX() + m.panX + (double(x) - cx) * step, wy = player_.missionY() - m.panZ + (cy - double(y)) * step;
            Df2Sel made;
            for (size_t i = 0; i < df2Wps_.size(); ++i)
                if (!df2Wps_[i].used) {
                    df2Wps_[i] = Df2Waypoint{true, wx, wy, terrain_.heightAt(float(wx), float(-wy)), "WP"};
                    made = Df2Sel{4, int(i)};
                    break;
                }
            df2Select(made);
            m.prompt = true;
            m.promptText.clear();
            SDL_StartTextInput();
            m.wTool = m.wButton = false;
            return;
        }
        // Pick (0x422640): the nearest blip drawn last frame under 1000
        // square pixels; empty map keeps the selection. Waypoints (class
        // type 7, items 6005 and 6089) become the drag target.
        float best = 1000.f;
        Df2Sel found;
        for (const auto& p : m.picks) {
            const float d = (p.x - float(x)) * (p.x - float(x)) + (p.y - float(y)) * (p.y - float(y));
            if (d < best) { best = d; found = p.sel; }
        }
        m.drag = Df2Sel{};
        if (found.kind) {
            df2Select(found);
            const int item = df2SelItem(found);
            const ItemDef* it = found.kind == 3 ? items_.byMissionType(uint32_t(item)) : nullptr;
            if (found.kind == 4 || (found.kind == 3 && ((it && it->type == "marker") || item == 6005))) m.drag = found;
        }
        return;
    }
    (void)s;
}

void Game::drawBriefing(const Screen& s) {
    // 0x482cb8: darkened box (200, 200)-(824, 568); "Mission Briefing"
    // (DFMENU.BIN) centred at (512, 225) in bright green; the text wrapped
    // by 0x482970 into x0 + 5, y0 + 35, width - 10, height - 40: words split
    // at spaces, "{...}" tags removed, "~" a line break, a word that would
    // pass the right edge starts a line; lines before the start line are
    // skipped; stop when the next line would pass the bottom.
    const float x0 = s.vx(200), x1 = s.vx(824), y0 = s.vy(200), y1 = s.vy(568);
    hud_.rect(x0, y0, x1 - x0, y1 - y0, {0, 0, 0, 1 - 10.f / 31.f});
    const std::string* title = menuText_.find("GroupMainMenu", "HELPMISSIONBRIEFING");
    const std::string t = title ? *title : "Mission Briefing";
    hud_.text(s.vx(512) - hud_.textWidth(t, s.text()) / 2, s.vy(225), t, kHudBright, s.text());
    const float bx = x0 + s.vx(5) - s.vx(0), by = y0 + s.vy(35), bw = (x1 - x0) - (s.vx(10) - s.vx(0)), bh = (y1 - y0) - s.vy(40);
    const float lh = hud_.lineHeight(s.text());
    float x = bx, y = by;
    int line = 1;
    std::istringstream words(briefing_);
    std::string w;
    while (words >> w) {
        if (w[0] == '{') {
            const size_t e = w.find('}');
            w = e == std::string::npos ? std::string() : w.substr(e + 1);
            if (w.empty()) continue;
        }
        if (w[0] == '~') {
            ++line;
            if (line > briefStart_) { y += line - 1 > briefStart_ - 1 ? lh : 0; }
            x = bx;
            if (y + lh > by + bh) break;
            continue;
        }
        if (w.size() > 63) w.resize(63);
        w += ' ';
        const float ww = hud_.textWidth(w, s.text());
        if (x + ww > bx + bw && x > bx) {
            ++line;
            if (line > briefStart_) y += lh;
            x = bx;
            if (y + lh > by + bh) break;
        }
        if (line >= briefStart_) hud_.text(x, y, w, kHudBright, s.text());
        x += ww;
    }
    briefLast_ = line;
}

void Game::drawHelp(const Screen& s) {
    // 0x48232c: darkened box (7, 98)-(1023, 666); labels (DFMENU.BIN HELP*)
    // and keys in two columns, rows 25 apart from y 115, bright green: left
    // labels at x 40 / keys at 200, right at 540 / 770 (0x4822f4 draws at
    // x + 20, y + 100). Key texts from the soldier's bindings (0x4816a0).
    const float x0 = s.vx(7), x1 = s.vx(1023), y0 = s.vy(98), y1 = s.vy(666);
    hud_.rect(x0, y0, x1 - x0, y1 - y0, {0, 0, 0, 1 - 10.f / 31.f});
    auto label = [&](const char* key) {
        const std::string* t = menuText_.find("GroupMainMenu", key);
        return t ? *t : std::string(key);
    };
    // 0x48151c: a binding's text: the key's name (0x421ff0), then
    // ", Mouse n" for a mouse button (even with no key). The soldier's
    // bindings, else DF.EXE's defaults.
    auto bindingText = [&](const char* game) {
        uint32_t vk = 0, mouse = 0;
        bool found = false;
        for (const auto& [n, key] : o_.bindings)
            if (n == game) { vk = key & 0xFFFF; const uint32_t m = key >> 16; mouse = m == 1 ? 1 : m == 2 ? 2 : m == 0x10 ? 3 : 0; found = true; break; }
        if (!found)
            for (const auto& r : kKeyRows)
                if (std::string(r.game) == game) { vk = r.vk; mouse = r.mouse; break; }
        std::string t = vkName(vk, [&](const char* k) { return label(k); });
        if (mouse) t += ", " + label(mouse == 1 ? "KBDMOUSELEFT" : mouse == 2 ? "KBDMOUSERIGHT" : "KBDMOUSEMIDDLE");
        return t;
    };
    auto keyOf = [&](Action a) {
        static const std::pair<Action, const char*> names[] = {
            {Action::MoveForward, "move_forward"}, {Action::MoveBack, "move_back"}, {Action::SlideLeft, "slide_left"},
            {Action::SlideRight, "slide_right"}, {Action::Crouch, "Crouch"}, {Action::Stand, "Stand"}, {Action::Prone, "Prone"},
            {Action::Jump, "move_jump"}, {Action::NoRun, "NoRun"}, {Action::Binoculars, "Binoculars"}, {Action::Briefing, "briefing"},
            {Action::OldMessages, "OldMessages"}, {Action::ShowFriendly, "ShowFriendly"}, {Action::MissionGoals, "MissionGoals"},
            {Action::Fire, "attack_1"}, {Action::Scope, "scope"}, {Action::MapZoomIn, "zoomin"}, {Action::MapZoomOut, "zoomout"},
            {Action::NextWaypoint, "NextWaypoint"}, {Action::BigMap, "BigMap"}, {Action::Weapon0, "weapon0"}, {Action::Weapon1, "weapon1"},
            {Action::Weapon2, "weapon2"}, {Action::Weapon3, "weapon3"}, {Action::Weapon4, "weapon4"}, {Action::Weapon5, "weapon5"},
            {Action::Weapon6, "weapon6"}, {Action::Weapon7, "weapon7"}, {Action::Weapon8, "weapon8"}, {Action::Weapon9, "weapon9"}};
        for (const auto& [act, n] : names)
            if (act == a) return bindingText(n);
        return controls_.describe(a);
    };
    auto put = [&](float lx, float kx, float y, const std::string& l, const std::string& k) {
        hud_.text(s.vx(lx + 20), s.vy(y + 100), l, kHudBright, s.text());
        hud_.text(s.vx(kx + 20), s.vy(y + 100), k, kHudBright, s.text());
    };
    struct Row { const char* label; std::string key; };
    const std::vector<Row> left = {
        {"HELPLOOKAROUND", label("HELPMOUSE")},
        {"HELPMOVE", keyOf(Action::SlideLeft) + ", " + keyOf(Action::SlideRight) + ", " + keyOf(Action::MoveForward) + ", " +
                         keyOf(Action::MoveBack)},
        {"HELPCROUCH", keyOf(Action::Crouch)}, {"HELPSTAND", keyOf(Action::Stand)}, {"HELPGOPRONE", keyOf(Action::Prone)},
        {"HELPJUMP", keyOf(Action::Jump)}, {"HELPWALK", keyOf(Action::NoRun)}, {nullptr, ""},
        {"HELPEXITGAME", label("HELPESC")}, {"HELPBINOCULARS", keyOf(Action::Binoculars)}, {"HELPTALK", bindingText("talk") + " (" + bindingText("stalk") + ")"},
        {"HELPRESOLUTION", bindingText("resolution")}, {"HELPFOCAMERA", label("HELPF9")}, {"HELPCYCLEFO", bindingText("tab")}, {"HELPFIRSTPERSON", label("HELPF2")},
        {"HELPTHIRDPERSON", label("HELPF5F6F7")}, {"HELPBRIEFINGS", keyOf(Action::Briefing)},
        {"HELPRECENT", keyOf(Action::OldMessages)}, {"HELPFRIENDLYTAGS", keyOf(Action::ShowFriendly)},
        {"HELPGOALS", keyOf(Action::MissionGoals)}, {"HELPTURBO", bindingText("Turbo")}, {"HELPNEXTFLAG", bindingText("NextFlag")},
    };
    float y = 15;
    for (const auto& r : left) {
        if (r.label) put(20, 180, y, label(r.label), r.key);
        y += 25;
    }
    // Right: the carried weapons (slots 1-9, then 0) with their keys, then
    // fire and scope; 50 lower the maps.
    y = 15;
    static const Action slotKeys[10] = {Action::Weapon0, Action::Weapon1, Action::Weapon2, Action::Weapon3, Action::Weapon4,
                                        Action::Weapon5, Action::Weapon6, Action::Weapon7, Action::Weapon8, Action::Weapon9};
    for (int k = 1; k <= 10; ++k) {
        const int slot = k % 10;
        const ModeDef* md = arsenal_.slotMode(slot);
        if (!md || md->name.empty()) continue;
        put(520, 750, y, md->name, keyOf(slotKeys[slot]));
        y += 25;
    }
    put(520, 750, y, label("HELPFIRE"), keyOf(Action::Fire));
    y += 25;
    put(520, 750, y, label("HELPSCOPE"), keyOf(Action::Scope));
    y += 75;
    put(520, 750, y, label("HELPTERRAINMAP"), label("HELPF10"));
    y += 25;
    put(520, 750, y, label("HELPCONTOURMAP"), label("HELPF11"));
    y += 25;
    put(520, 750, y, label("HELPZOOM"), keyOf(Action::MapZoomIn) + ", " + keyOf(Action::MapZoomOut));
    y += 25;
    put(520, 750, y, label("HELPWAYPOINT"), keyOf(Action::NextWaypoint));
    y += 25;
    put(520, 750, y, label("HELPBIGMAP"), keyOf(Action::BigMap));
}

void Game::drawPaused(const Screen& s) {
    // 0x48327c: a darkened box (shade level 10) from (310, 330) to (714, 438)
    // and "Game Paused" centred at (512, 380) in green (0x66f938).
    if (df2Hud_.on) {
        // DF2.EXE 0x442b60: the panel box and STROVER7 centred at (512, 380), green.
        df2Panel(s, 310, 330, 714, 438);
        const std::string t = df2Text("Overlays", "STROVER7", "Game Paused");
        hud_.text(s.vx(512) - hud_.textWidth(t, s.text()) / 2, s.vy(380), t, kHudPureGreen, s.text());
        return;
    }
    const float x0 = s.vx(310), x1 = s.vx(714), y0 = s.vy(330), y1 = s.vy(438);
    hud_.rect(x0, y0, x1 - x0, y1 - y0, {0, 0, 0, 1 - 10.f / 31.f});
    const char* t = "Game Paused";
    hud_.text(s.vx(512) - hud_.textWidth(t, s.text()) / 2, s.vy(380), t, kHudPureGreen, s.text());
}

void Game::drawOldMessages(const Screen& s) {
    // 0x484d94: a darkened box (100, 100)-(924, 668), "Messages" centred at
    // (512, 120) in bright green, then the 16 latest lines from x 130, y 160,
    // 30 apart: entry 15 - k on row k (entry 0 the newest, pushed by 0x42f400
    // into a 40-deep ring), so the oldest of the 16 is at the top and the
    // newest always at y 610 (0x484e01..0x484e35), each in its message
    // colour (the ring's +0x78, 0x484e0d).
    if (df2Hud_.on) {
        // DF2.EXE 0x442850: the panel box, STROVER43 centred at (512, 120) in
        // orange, the system history's entries 15..0 (0x41cf50 pushes the
        // newest into entry 0) at x 130, y 160 + 30k, each in its own colour:
        // bottom-anchored, the newest at y 610.
        df2Panel(s, 100, 100, 924, 668);
        const std::string t = df2Text("Overlays", "STROVER43", "Messages");
        hud_.text(s.vx(512) - hud_.textWidth(t, s.text()) / 2, s.vy(120), t, kDf2Amber, s.text());
        for (int k = 0; k < 16; ++k) {
            const size_t e = size_t(15 - k);
            if (e < df2Sys_.size()) hud_.text(s.vx(130), s.vy(160 + 30 * float(k)), df2Sys_[e].text, df2Sys_[e].colour, s.text());
        }
        return;
    }
    const float x0 = s.vx(100), x1 = s.vx(924), y0 = s.vy(100), y1 = s.vy(668);
    hud_.rect(x0, y0, x1 - x0, y1 - y0, {0, 0, 0, 1 - 10.f / 31.f});
    const char* t = "Messages";
    hud_.text(s.vx(512) - hud_.textWidth(t, s.text()) / 2, s.vy(120), t, kHudBright, s.text());
    const size_t first = history_.size() < 16 ? 0 : history_.size() - 16;
    for (size_t i = first, row = 16 - (history_.size() - first); i < history_.size(); ++i, ++row)
        hud_.text(s.vx(130), s.vy(160 + 30 * float(row)), history_[i].first, history_[i].second, s.text());   // entry +0x78 (0x484e0d)
}

void Game::drawFriendlyTags(const Screen& s, const Mat4& viewProj) {
    // 0x44c72c / 0x44c574: soldiers on the player's side (no alive check)
    // named in the campaign's "People Names" as NAMEID<record +4> (no name,
    // no tag); at the origin raised by 1 m on screen, then 25 up; centred,
    // clipped to x 60..964 and the view (top + 5 .. bottom - 30); colour by
    // side: 1 blue, 2 red, else green.
    const Vec3 eye = viewEye();
    for (const auto& o : world_.objects) {
        if (!o.person || o.side != kPlayerSide || o.missionIndex < 0) continue;
        const int nameId = int(mission_.objects[size_t(o.missionIndex)].raw[1]);
        if (nameId == 0) continue;
        char key[24];
        std::snprintf(key, sizeof key, "NAMEID%03d", nameId);
        const std::string* name = campText_.find("People Names", key);
        if (!name || name->empty()) continue;
        const Vec3 at = world_.nearest(o.pos, eye);
        if (std::getenv("OPENDF_DEBUGTAG")) std::fprintf(stderr, "tag %s at (%.1f %.1f %.1f) eye (%.1f %.1f %.1f)\n", name->c_str(), at.x, at.y, at.z, eye.x, eye.y, eye.z);
        auto project = [&](Vec3 p, float& px, float& py) {
            const float cx = viewProj.at(0, 0) * p.x + viewProj.at(0, 1) * p.y + viewProj.at(0, 2) * p.z + viewProj.at(0, 3);
            const float cy = viewProj.at(1, 0) * p.x + viewProj.at(1, 1) * p.y + viewProj.at(1, 2) * p.z + viewProj.at(1, 3);
            const float cw = viewProj.at(3, 0) * p.x + viewProj.at(3, 1) * p.y + viewProj.at(3, 2) * p.z + viewProj.at(3, 3);
            if (cw <= 0.1f) return false;
            px = (cx / cw * 0.5f + 0.5f) * s.w;
            py = s.playY + (0.5f - cy / cw * 0.5f) * s.playH;
            return true;
        };
        float px, py, ux, uy;
        if (!project(at, px, py) || !project(at + Vec3{0, 1.f, 0}, ux, uy)) continue;
        if (py - uy <= 0.5f) continue;   // 1 m must be more than half a pixel
        py = uy - s.vy(25);
        if (px < s.vx(60) || px > s.vx(964) || py < s.playY + s.vy(5) || py > s.playY + s.playH - s.vy(30)) continue;
        const Color c = o.side == 1 ? kHudBlue : o.side == 2 ? kHudRed : kHudPureGreen;
        hud_.text(px - hud_.textWidth(*name, s.text()) / 2, py, *name, c, s.text());
    }
}

void Game::drawBinoculars(const Screen& s) {
    // 0x401f94: "Distance: %3im" at (768, 369) (the depth at the centre of
    // the view, "1km+" from 1000 m) and "Heading: %3i" at (256, 369), both
    // centred, bright green. The heading is DF.EXE's (yaw - 90 degrees +
    // offset) in its angles (0 east, anticlockwise: obj+0x30, loader
    // 0x440109), i.e. 360 - the compass bearing of the view.
    Player cam = player_;
    cam.yaw += binoYaw_;
    cam.pitch += binoPitch_;
    const Vec3 eye = player_.eye(), dir = cam.forward();
    RayHit hit;
    float dist = 1e9f;
    if (world_.raycast(eye, eye + dir * 1100.f, hit)) dist = hit.t * 1100.f * dot(dir, dir);
    char buf[32];
    if (dist >= 1000.f) std::snprintf(buf, sizeof buf, "Distance: 1km+");
    else std::snprintf(buf, sizeof buf, "Distance: %3im", int(dist));
    // DF2.EXE 0x45a9e0: the same texts in orange, 15 above the 3D view's centre.
    const float ty = df2Hud_.on ? s.playY + s.playH * 0.5f - s.vy(15) : s.vy(369);
    const Color tc = df2Hud_.on ? kDf2Amber : kHudBright;
    hud_.text(s.vx(768) - hud_.textWidth(buf, s.text()) / 2, ty, buf, tc, s.text());
    float deg = 360.f - cam.yaw * 180.f / kPi;
    deg -= 360.f * std::floor(deg / 360.f);
    std::snprintf(buf, sizeof buf, "Heading: %3i", int(deg));
    hud_.text(s.vx(256) - hud_.textWidth(buf, s.text()) / 2, ty, buf, tc, s.text());
}

void Game::drawMissionEnd(const Screen& s) {
    // DF.EXE 0x483094: success if the winner is the player's side. A darkened
    // panel (0x401b6c) from (128, 116) to (896, 246) virtual, i.e. x 80-560,
    // y 72.5-153.75, shade level 10 of the 32-step shade table (0x435070:
    // level r keeps r/31 of the brightness, so 10/31), with a one-pixel
    // outline in bright green (0x66f94c); three lines centred at x 512 in
    // green (0x66f938) with tops at 136, 176, 206 virtual.
    const bool won = missionResult_ == kPlayerSide;
    if (df2Hud_.on) {
        // DF2.EXE 0x441660: panel (128, top+20)-(896, top+150), green lines
        // centred at x 512 with tops top+40, +80 and +110 (SP).
        const float top = s.playY / s.vy(1);
        df2Panel(s, 128, top + 20, 896, top + 150);
        const std::string lines[3] = {won ? df2Text("Overlays", "STROVER1", "Congratulations!  Mission Completed Successfully.")
                                          : df2Text("Overlays", "STROVER2", "Game Over.  Mission Incomplete."),
                                      df2Text("Overlays", "STROVER3", "Hit Enter to end mission."),
                                      df2Text("Overlays", "STROVER4", "Hit Space Bar to replay mission")};
        const float tops[3] = {40, 80, 110};
        for (int i = 0; i < 3; ++i)
            hud_.text(s.vx(512) - hud_.textWidth(lines[i], s.text()) / 2, s.vy(top + tops[i]), lines[i], kHudPureGreen, s.text());
        return;
    }
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
    if (df2Hud_.on) {
        // DF2.EXE 0x442520: right-aligned at x 1000 from top+20, 25 apart,
        // each line twice (0x402000 shadow at +1,+1 pixel); "Win Conditions:"
        // then the header's win ids as DF2WPN STRWINCOND%03i (0x804000 once
        // met), 10 lower "Lose Condiitions:" (sic) and STRLOSECOND%03i (red
        // once hit); others white.
        const float ts = s.text(), sh = s.px(), top = s.playY / s.vy(1);
        float y = top + 20;
        auto line = [&](const std::string& text, Color c) {
            const float x = s.vx(1000) - hud_.textWidth(text, ts);
            hud_.text(x + sh, s.vy(y) + sh, text, rgb(0x40, 0x20, 0), ts);
            hud_.text(x, s.vy(y), text, c, ts);
            y += 25;
        };
        line(df2Text("Overlays", "STROVER51", "Win Conditions:"), kHudWhite);
        for (int i = 0; i < 4 && mission_.winIds[i]; ++i) {
            char key[24];
            std::snprintf(key, sizeof key, "STRWINCOND%03d", mission_.winIds[i]);
            const std::string* t = wpnText_.find("WinConditions", key);
            line(t ? *t : key, script_.goalDone(i + 1) ? rgb(0x80, 0x40, 0) : kHudWhite);
        }
        y += 10;
        line(df2Text("Overlays", "STROVER52", "Lose Condiitions:"), kHudWhite);
        for (int i = 0; i < 4 && mission_.loseIds[i]; ++i) {
            char key[24];
            std::snprintf(key, sizeof key, "STRLOSECOND%03d", mission_.loseIds[i]);
            const std::string* t = wpnText_.find("LoseConditions", key);
            line(t ? *t : key, script_.failDone(i + 1) ? kHudRed : kHudWhite);
        }
        return;
    }
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
    // Delta Force 2 names its game text DF2GAME.BIN.
    const char* gameBin = vfs_.exists("DFGAME.BIN") ? "DFGAME.BIN" : "DF2GAME.BIN";
    if (vfs_.read(gameBin, raw)) parseRtxt(raw, gameText_);
    if (vfs_.read("DFMENU.BIN", raw)) menuText_.parse(raw);
    if (vfs_.read("DF2WPN.BIN", raw)) wpnText_.parse(raw);
    if (vfs_.read(gameBin, raw)) gameTable_.parse(raw);
    {
        // The player soldier (ITEMS.DEF "player01") names its move table.
        const ItemDef* soldier = items_.byId(kItemPlayerSoldier);
        const std::string anm = toUpper(soldier && !soldier->animDef.empty() ? soldier->animDef : "playanim") + ".ANM";
        if (!vfs_.read(anm, raw) || !playerMoves_.parse(std::string(raw.begin(), raw.end())))
            std::fprintf(stderr, "cannot read %s; using built-in movement speeds\n", anm.c_str());
        // Eye heights: the pose's origin height (.CHR contact points) plus the
        // eye offset, 0.7 m, or 0.4 m prone (DF.EXE 0x449913).
        Chr& chr = playerChr_;
        const std::string chrName = toUpper(soldier && !soldier->chrFile.empty() ? soldier->chrFile : "player01") + ".CHR";
        const std::string ksaName = chrName.substr(0, chrName.size() - 4) + ".KSA";
        if ((vfs_.read(chrName, raw) && chr.parse(raw)) || (vfs_.read(ksaName, raw) && chr.parseKsa(raw))) {
            const char* stances[3] = {"Standing", "Crouched", "Prone"};
            for (int st = 0; st < 3; ++st)
                for (int h = 0; h < 2; ++h) {
                    const AnimMove* mv = playerMoves_.find(std::string(stances[st]) + (h ? "_1HW" : "_2HW"));
                    const ChrAnimation* a = mv ? chr.animation(mv->animation) : nullptr;
                    if (a) eyeHeightTable_[st][h] = Chr::originHeight(a->frames[0]) + (st == 2 ? 0.4f : 0.7f);
                }
            for (int k = 0; k < 2; ++k) {
                const AnimMove* mv = playerMoves_.find(k ? "Swimming" : "Floating");
                const ChrAnimation* a = mv ? chr.animation(mv->animation) : nullptr;
                if (a) (k ? swimOrigin_ : floatOrigin_) = Chr::originHeight(a->frames[0]);
            }
        }
    }
    {
        const auto exePath = findGameExe(o_.gameDir);
        std::string err;
        if (exePath.empty()) {
            if (!vfs_.exists("DF2.PWF")) std::fprintf(stderr, "DF.EXE not found; using built-in weapon values\n");
        } else if (!exe_.load(exePath, &err)) std::fprintf(stderr, "%s; using built-in weapon values\n", err.c_str());
        if (const auto df2 = findDf2Exe(o_.gameDir); !df2.empty() && !df2Exe_.load(df2, &err))
            std::fprintf(stderr, "%s; using built-in DF2 values\n", err.c_str());
        if (!controls_.init(&exe_) && !vfs_.exists("DF2.PWF")) std::fprintf(stderr, "using built-in key bindings\n");
        if (vfs_.exists("DF2.PWF") && !controls_.initDf2(df2Exe_)) std::fprintf(stderr, "using DF1's key bindings for DF2\n");
        for (const auto& [name, key] : o_.bindings) controls_.set(name, key);
        std::string sndErr;
        if (!sound_.init(vfs_, &exe_, &audio_, &sndErr, &df2Exe_)) std::fprintf(stderr, "no sounds: %s\n", sndErr.c_str());
        sound_.setDialogVolume(o_.dialogVolume);
        crosshair_ = o_.crosshairs;
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

    // Delta Force 2's HUD: its strips and HUD.DEF (Land Warrior and Task
    // Force Dagger have a HUD.DEF too, with other art).
    if (std::vector<uint8_t> def; vfs_.exists("DHB640G.PCX") && vfs_.read("HUD.DEF", def))
        df2Hud_.on = df2Hud_.def.parse(def);

    if (rc == 0 && std::getenv("OPENDF_LOWEST")) {
        float best = 1e9, bx = 0, bz = 0;
        const float ext = terrain_.mapExtent();
        for (float x = 0; x < ext; x += 16) for (float z = 0; z < ext; z += 16) { float h = terrain_.heightAt(x, z); if (h < best) { best = h; bx = x; bz = z; } }
        std::fprintf(stderr, "LOWEST %.2f at mission (%.1f, %.1f) water %.2f\n", best, bx, -bz, terrain_.waterLevel());
    }
    if (rc == 0) {
        if (o_.hasCamera) {
            player_ = Player{};
            player_.moves = &playerMoves_;
    std::memcpy(player_.eyeHeights, eyeHeightTable_, sizeof eyeHeightTable_);
    player_.floatOrigin = floatOrigin_;
    player_.swimOrigin = swimOrigin_;
            player_.feet = {o_.camera[0], 0, -o_.camera[1]};
            player_.wrap(terrain_.mapExtent());
            player_.feet.y = world_.groundHeight(player_.feet, 0.45f) + o_.camera[2];
            player_.yaw = o_.camera[3] * kPi / 180.f;
            player_.pitch = o_.camera[4] * kPi / 180.f;
            // OPENDF_NOFLY: a high --camera start falls under the physics (tests).
            player_.fly = o_.camera[2] > 2 && !std::getenv("OPENDF_NOFLY");
        }
        if (o_.weapon >= 0) arsenal_.select(o_.weapon);
        if (o_.stance >= 0) player_.stance = Stance(std::clamp(o_.stance, 0, 2));
        for (int i = 0; i < o_.zoom; ++i) arsenal_.toggleZoom();
        showMap_ = o_.showMap;
        if (o_.killEnemies)
            for (size_t i = 0; i < world_.objects.size(); ++i)
                if (world_.objects[i].side == 2 && world_.objects[i].missionGroup == 3) world_.damage(int(i), 1000);

        captured_ = !scripted;
        // The integer mixer of both games (DF.EXE 0x497f9c, DF2.EXE 0x48e028:
        // the same tables, gains, y[n-1] >> 2 and clamp). Its mode-3 hook:
        // DF1 on until the first mission ends; DF2 already reset by the menu
        // teardown into the Game Loop (0x43b68d -> 0x42f420 -> 0x43cb50 ->
        // 0x4712e0), so never on in a DF2 mission.
        audio_.setDfMixer(true, mission_.version < 0x0F && o_.mixerHook);
        audio_.setDfGame(mission_.version < 0x0F ? 1 : 2);   // the WAV conversion and mixer of DF.EXE / DF2.EXE
        if (!o_.record.empty()) {
            audioOut_ = std::fopen((o_.record + ".pcm").c_str(), "wb");
        } else if (!hidden && !audio_.openDevice()) {
            std::fprintf(stderr, "no audio device: %s\n", SDL_GetError());
        }
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
            if (leaveMission_) break;   // Enter on the end panel / Esc: back to the front end
            Uint64 now = SDL_GetPerformanceCounter();
            float dt = fixedStep ? 1.f / 60.f : std::min(float(now - last) / SDL_GetPerformanceFrequency(), 0.1f);
            const float realDt = dt;   // the input script keeps time while paused
            if (paused_) dt = 0;       // Pause stops the game (0x42df53)
            last = now;

            SDL_Event e;
            bool zoomClick = false;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { running = false; quit_ = true; }
                if (leaveMission_) running = false;
                else if (df2Hud_.on && showMap_ && (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)) {
                    // DF2's command map is a mouse UI: the pointer in its 640x480 screen.
                    int dw2, dh2, ww2, wh2;
                    SDL_GL_GetDrawableSize(win_, &dw2, &dh2);
                    SDL_GetWindowSize(win_, &ww2, &wh2);
                    Screen ms;
                    ms.w = float(dw2);
                    ms.h = float(dh2);
                    const float k = df2MapK(ms), ox = (ms.w - 640.f * k) * 0.5f, oy = (ms.h - 480.f * k) * 0.5f;
                    const int mx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x, my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
                    const float px = float(mx) * float(dw2) / float(std::max(1, ww2)), py = float(my) * float(dh2) / float(std::max(1, wh2));
                    const int nx = int((px - ox) / k), ny = int((py - oy) / k);
                    if (e.type == SDL_MOUSEMOTION && df2Map_.panning) {
                        const float mPerPx = 1024.f / 640.f * mapScale_;
                        df2Map_.panX = std::clamp(df2Map_.panX - float(nx - df2Map_.mouseX) * mPerPx, -1024.f, 1024.f);
                        df2Map_.panZ = std::clamp(df2Map_.panZ - float(ny - df2Map_.mouseY) * mPerPx, -1024.f, 1024.f);
                    }
                    df2Map_.mouseX = nx;
                    df2Map_.mouseY = ny;
                    if (e.type != SDL_MOUSEMOTION)
                        df2MapClick(ms, nx, ny, e.type == SDL_MOUSEBUTTONDOWN, e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 3 : 2);
                }
                else if (e.type == SDL_MOUSEBUTTONDOWN && !captured_) { captured_ = true; SDL_SetRelativeMouseMode(SDL_TRUE); }
                else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) zoomClick = true;   // scope (original: S or right mouse)
                else if (e.type == SDL_MOUSEMOTION && captured_ && !showMap_) {
                    // DF.EXE 0x42eba0: mousescale << 14 binary angle per count
                    // (0x200000, 0.1758°, at the default 128), a quarter while
                    // zoomed; flipmouse flips the vertical axis (0x42ebd6).
                    // DF2.EXE 0x41adec..0x41ae13: an eighth while the scope
                    // shows (0x447b40, not moving: +0x74 & 0x18 clear).
                    const float zoomScale = arsenal_.df2() ? (arsenal_.scopeShown() ? 0.125f : 1.f) : (arsenal_.zoomed() ? 0.25f : 1.f);
                    const float sens = 0.17578125f * kPi / 180.f * float(o_.mouseScale) / 128.f * zoomScale;
                    const float dy = o_.flipMouse ? -float(e.motion.yrel) : float(e.motion.yrel);
                    player_.yaw += e.motion.xrel * sens;
                    player_.pitch = std::clamp(player_.pitch - dy * sens, -67.5f * kPi / 180.f, 90.f * kPi / 180.f);
                } else if (e.type == SDL_TEXTINPUT && df2Hud_.on && showMap_ && df2Map_.prompt) {
                    // The command map's name prompt (chat input 0x41d320, max 16).
                    for (const char* c = e.text.text; *c; ++c)
                        if (uint8_t(*c) >= 32 && uint8_t(*c) < 128 && df2Map_.promptText.size() < 16) df2Map_.promptText += *c;
                } else if (e.type == SDL_KEYDOWN && (!e.key.repeat || (df2Map_.prompt && showMap_))) handleKey(e.key);
            }
            if (zoomClick) arsenal_.toggleZoom();

            if (!inputCmds_.empty()) runInputScript(realDt);
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
            // Dead (0x45a234): no more control. The death sequence itself
            // (animation, mission end 0x444d24 / 0x46c9c0) is not done yet.
            if (showMap_ || playerDead()) in = PlayerInput{};
            // Keyboard turning and looking (DF.EXE 0x436fec): 0x1fffffe binary
            // angle per 30 Hz controller tick (84.375°/s), half that with NoRun
            // held; the same for pitch, which stays within -67.5°..+90° (0x437435).
            if (!showMap_) {
                const float rate = (in.walk ? 42.1875f : 84.375f) * kPi / 180.f * dt;
                player_.yaw += rate * (float(down(Action::TurnRight)) - float(down(Action::TurnLeft)));
                player_.pitch += rate * (float(down(Action::LookUp)) - float(down(Action::LookDown)));
            }
            player_.pitch = std::clamp(player_.pitch, -67.5f * kPi / 180.f, 90.f * kPi / 180.f);
            // Any move or jump key puts the binoculars down (0x4373ab: the
            // key bits [0x93351c] & 0x41e clear [0x93367c]); the scope flag
            // stays, DF.EXE only hides the scope while the keys are down
            // (0x44b44d in 0x44b420, Arsenal::zoomed).
            const bool moveKeys = in.forward != 0 || in.strafe != 0 || in.jump;
            arsenal_.setMoveKeys(moveKeys);
            if (!arsenal_.df2() && moveKeys && binoculars_) { binoculars_ = false; arsenal_.setBinoculars(false); }   // 0x4373ab
            if (arsenal_.df2()) {
                // DF2.EXE: holding the binoculars (mode 47, +0x3C 0x200) the
                // move chooser picks the raised moves 0x72 / 0x68 / 5 only
                // at speed bits ((+0x74 >> 3) & 3) zero (0x43f74f, 0x43f7a3,
                // 0x43f886, each then testing the mode's 0x200); moving keeps
                // the slot but lowers them. The frame (0x45c7e2) gives the
                // view FOV 22 and the offset only in those moves and not in
                // an outside view ([0xbba840]); the offset is picked anew on
                // the first frame of each raise ([0x4d2e78], 0x45c809 /
                // 0x45c869 -> 0x4481e0). INFERRED: speed bits non-zero
                // exactly while a move key is down.
                const bool raised = arsenal_.binocularsHeld() && !moveKeys && !playerDead();
                if (raised && !binoculars_) pickBinocularOffset();
                binoculars_ = raised;
            }
            const bool trigger = !showMap_ && !playerDead() && (down(Action::Fire) || o_.holdFire);

            player_.oneHanded = arsenal_.mode().oneHanded();
            in.shooting = arsenal_.firing();
            player_.update(dt, in, world_);
            if (std::getenv("OPENDF_DEBUGPLAYER") && (ticks_ % 30) == 0) {
                const Vec3 f = player_.feet, e = player_.eye();
                std::fprintf(stderr, "t %.1f hp %d mission (%.1f, %.1f) feet %.2f ground %.2f surface %.2f eye %.2f water %.2f %s speed %.1f%s%s%s\n", ticks_ / 60.f,
                             playerHp_, player_.missionX(), player_.missionY(), f.y, terrain_.heightAt(f.x, f.z),
                             world_.groundHeight(f, 0.5f), e.y, terrain_.waterLevel(),
                             player_.swimming() ? "SWIM" : player_.inWater() ? "FLOAT" : "land", player_.speed(),
                             player_.chute ? " CHUTE" : "", player_.armory() ? " ARMORY" : "", player_.onLadder() ? " LADDER" : "");
                if (player_.platform() >= 0 && size_t(player_.platform()) < world_.objects.size()) {
                    const WorldObject& po = world_.objects[size_t(player_.platform())];
                    std::fprintf(stderr, "  on %s (object %d) at (%.2f, %.2f), yaw %.1f\n", po.item ? po.item->name.c_str() : "?", player_.platform(),
                                 po.pos.x, po.pos.z, player_.yaw * 57.2958f);
                }
            }
            updateRoute();
            // Mission events run on the original's 60 Hz ticks.
            for (const auto& hit : arsenal_.takeImpacts()) spawnImpact(hit);
            // Rockets carry ITEMS.DEF `ambient rocket1` as a loop (0x44d6f9).
            {
                static const int rocket = sound_.idOf("ROCKET1");
                for (const auto& p : arsenal_.projectiles())
                    if (p.flags & 0x40) sound_.setLoop(p.id, rocket, p.pos);
                vehicles_.sounds(world_, ai_, sound_);
                sound_.sweepLoops();
                sound_.updateLoops();
            }
            for (const auto& e : arsenal_.takeSounds()) {
                if (e.id < 0) sound_.play2D(-e.id);
                else playSound(e.id, e.pos);
                // The player's gunshots are heard (0x46c228: range / 2).
                if (e.shot) ai_.shotFired(e.pos, sound_.range(e.id) * 0.5f, kPlayerSide, -3);
            }
            for (const auto& h : arsenal_.takePlayerHits()) hurtPlayer(h.damage, 1, h.from, h.part);
            for (const auto& e : arsenal_.takeEvents()) weaponEvent(e);
            // The designator's point (0x4508be: the depth at the centre of the
            // zoomed view), sent with the player's state: a 64-tick window.
            if (arsenal_.mode().type == 10 && arsenal_.zoomed()) {
                const float range = rangeToAim();
                if (range >= 0 && range <= 999) {
                    laserPoint_ = player_.eye() + aimDirection() * range;
                    laserWindow_ = 64;
                }
            }
            for (int i : world_.takeDestroyed()) {
                if (mission_.version >= 0x0F) {
                    // Delta Force 2's new ai_functions on destruction.
                    const WorldObject& d = world_.objects[size_t(i)];
                    const std::string f = d.item ? d.item->aiFunction : std::string();
                    if (f.rfind("gnrl", 0) == 0) continue;   // 0x4012b0: silent, no barrel, no sound
                    if (f.rfind("smok", 0) == 0) { spawnSmk2(d.pos, false, 0, 0, 0xc0); continue; }   // 0x402550: one dark cloud (0x413e80, t 0xc0)
                    if (f.rfind("ibom", 0) == 0) { bigBlast(d.pos); continue; }    // 0x401240 -> 0x4572d0
                    if (f.rfind("bld2", 0) == 0) { startCollapse(i); continue; }   // 0x4028d0
                    if (df2ItemDestroyed(i)) continue;                              // slgt, mine, vtrg
                }
                // 0x47ecf0: destruction sounds by item (def +0x20 = id - 100000).
                static const std::pair<int, int> kDestroyed[] = {
                    {1001, 0x135}, {1002, 0x136}, {1039, 0x137}, {1040, 0x138}, {1041, 0x13F}, {1042, 0x145}, {1044, 0x146},
                    {1046, 0x145}, {1048, 0x138}, {1049, 0x145}, {3008, 0x13C}, {4015, 0x13D}, {4019, 0x13E}};
                const auto& o = world_.objects[size_t(i)];
                for (const auto& d : kDestroyed)
                    if (o.item && o.item->id - 100000 == d.first) playSound(d.second, o.pos);
            }
            for (auto& m : world_.landingMessages_) pushMessage(m);
            world_.landingMessages_.clear();
            // Falling into the water (DF.EXE 0x4365a2, DF2.EXE 0x440bb6):
            // effect 4 "water exp" and sound 0x8A (FS_WATER_SLOSH) at the surface.
            for (const Vec3& at : world_.takeSplashes()) {
                spawnEffect(4, at);
                playSound(0x8A, at);
            }
            for (int i : world_.takeLandings()) {
                // 0x413d70: the canopy left where the person landed, 1800 ticks.
                const WorldObject& o = world_.objects[size_t(i)];
                if (const ItemDef* ci = items_.byId(o.item && (o.item->attrib & 0x400) ? 107037 : 107038); ci && !ci->graphic.empty())
                    if (const GpuModel* cm = models_.get(vfs_, ci->graphic)) {
                        Effect e{cm, Vec3{o.pos.x, o.base, o.pos.z}, 1.f, 0, 1800, 1, 1};
                        e.canopy = true;
                        e.heading = o.heading;
                        effects_.push_back(e);
                    }
            }
            for (const auto& at : arsenal_.takeStabs())
                for (auto& m : world_.stab(at, player_.origin())) pushMessage(m);
            for (const auto& sp : player_.takeSpawns()) {
                if (sp.slot == -1) {
                    // The player's canopy (PFoil: `parachut`).
                    if (const ItemDef* ci = items_.byId(107037); ci && !ci->graphic.empty())
                        if (const GpuModel* cm = models_.get(vfs_, ci->graphic)) {
                            Effect e{cm, sp.pos, 1.f, 0, 1800, 1, 1};
                            e.canopy = true;
                            e.heading = player_.yaw;
                            effects_.push_back(e);
                        }
                    continue;
                }
                spawnEffect(sp.slot, sp.pos);
            }
            for (scriptClock_ += dt; scriptClock_ >= 1.f / 60.f; scriptClock_ -= 1.f / 60.f) {
                ++ticks_;
                tickEffects();
                tickDialogs();
                touchPickups();
                // DF1: the carrier's move (0x4365c6 -> 0x436a0c) copies its
                // origin +0x24..+0x2c into the carried item every tick.
                if (carried_ >= 0 && world_.objects[size_t(carried_)].carried) world_.objects[size_t(carried_)].pos = player_.origin();
                if (mission_.version >= 0x0F) { tickAir(); tickCollapses(); tickDf2Items(); }
                // DF2.EXE 0x45b45d -> 0x46baa0: the rain and snow, once a tick.
                weather_.tick(int32_t(arsenal_.windX()), int32_t(-arsenal_.windZ()), int32_t(std::lround(terrain_.waterLevel() * 65536.f)),
                              [this](Vec3 at) { spawnEffect(17, at); });
                // DF2 fall damage (0x43f600, single player): n = trunc(vz /
                // -10360.5) with vz in 16.16 m a tick, (1 << n) - 1 for n >= 1
                // (the player's flag 1 skips the -1 others get).
                if (const float vy = player_.takeLanding(); mission_.version >= 0x0F && vy < 0) {
                    const int n = int(-vy / 60.f * 65536.f / 10360.5f);
                    if (n >= 1) hurtPlayer((1 << std::min(n, 20)) - 1, 4, player_.feet);   // kind 4 (0x43f645)
                }
                // Testing: OPENDF_TEAMSPEED="TEAM:KMH[,TEAM:KMH...]" sets team
                // speeds at tick 1, as the script's set-speed action does
                // (0x46cafc), e.g. to start a mission's train at once.
                if (static bool once = false; !once && std::getenv("OPENDF_TEAMSPEED")) {
                    once = true;
                    for (const char* p = std::getenv("OPENDF_TEAMSPEED"); p && *p;) {
                        int team = 0, kmh = 0, n = 0;
                        if (std::sscanf(p, "%d:%d%n", &team, &kmh, &n) != 2) break;
                        ai_.teamSpeed(team, kmh);
                        p += n;
                        if (*p == ',') ++p;
                    }
                }
                // Testing: OPENDF_TESTLADDER=1 (DF2) at tick 30 takes the first
                // soldier allowed on ladders (BMS +0xC & 0x1000), finds the
                // nearest object with a type-4 volume (sampled every 0.25 m;
                // =ITEMNAME only objects of that item), sets him on the ground
                // 1 m in front of the middle of the ladder, facing it, and
                // makes him run on (AiSystem::testWalk; OPENDF_TESTLADDERWALK=1
                // walks).
                if (static bool once = false; !once && std::getenv("OPENDF_TESTLADDER") && world_.df2Rules && ticks_ > 30) {
                    once = true;
                    const std::string want = std::getenv("OPENDF_TESTLADDER");
                    int who = -1;
                    for (size_t j = 0; j < world_.objects.size() && who < 0; ++j)
                        if (world_.objects[j].person && world_.objects[j].alive() && world_.objects[j].climbs) who = int(j);
                    float bestD = 1e9f;
                    int on = -1;
                    std::vector<Vec3> pts;   // object frame: (right, up, forward)
                    for (size_t j = 0; who >= 0 && j < world_.objects.size(); ++j) {
                        const WorldObject& n = world_.objects[j];
                        if (n.person || n.removed || !n.model || !n.model->hasVolumes) continue;
                        if (want != "1" && (!n.item || n.item->name != want)) continue;
                        const Vec3 c = world_.nearest(n.pos, world_.objects[size_t(who)].pos);
                        const Vec3 dd = c - world_.objects[size_t(who)].pos;
                        const float dist = std::sqrt(dd.x * dd.x + dd.z * dd.z);
                        if (dist >= bestD) continue;
                        const Vec3 fwd{std::sin(n.heading), 0, -std::cos(n.heading)}, right{-fwd.z, 0, fwd.x};
                        const float R = n.model->flatRadius, H = n.model->headerRadius, half = 0.05f;
                        std::vector<Vec3> found;
                        for (float x = -R; x <= R; x += 0.25f)
                            for (float z = -R; z <= R; z += 0.25f)
                                for (float y = -H; y <= H; y += 0.25f) {
                                    const Vec3 q = c + right * x + fwd * z + Vec3{0, y, 0};
                                    Vec3 push;
                                    uint32_t flags = 0;
                                    world_.df2Overlap(n, &q, &half, 1, push, flags);
                                    if (flags & 1) found.push_back({x, y, z});
                                }
                        if (!found.empty()) { bestD = dist; on = int(j); pts = found; }
                    }
                    if (who >= 0 && on >= 0) {
                        WorldObject& s = world_.objects[size_t(who)];
                        const WorldObject& n = world_.objects[size_t(on)];
                        // One ladder: the points within 1.5 m (across) of the lowest.
                        Vec3 first = pts[0];
                        for (const Vec3& q : pts) if (q.y < first.y) first = q;
                        std::vector<Vec3> one;
                        for (const Vec3& q : pts) if (std::abs(q.x - first.x) <= 1.5f && std::abs(q.z - first.z) <= 1.5f) one.push_back(q);
                        pts = one;
                        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
                        for (const Vec3& q : pts) {
                            const float v[3] = {q.x, q.y, q.z};
                            for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], v[k]), hi[k] = std::max(hi[k], v[k]);
                        }
                        // The ladder's thin horizontal axis faces outward, away from the centre.
                        const float mx = (lo[0] + hi[0]) * 0.5f, mz = (lo[2] + hi[2]) * 0.5f;
                        const bool alongX = hi[0] - lo[0] < hi[2] - lo[2];
                        const float ox = alongX ? (mx < 0 ? -1.f : 1.f) : 0.f, oz = alongX ? 0.f : (mz < 0 ? -1.f : 1.f);
                        const Vec3 fwd{std::sin(n.heading), 0, -std::cos(n.heading)}, right{-fwd.z, 0, fwd.x};
                        const Vec3 c = world_.nearest(n.pos, s.pos);
                        const Vec3 mid = c + right * mx + fwd * mz;
                        Vec3 at = c + right * (mx + ox * ((hi[0] - lo[0]) * 0.5f + 1.f)) + fwd * (mz + oz * ((hi[2] - lo[2]) * 0.5f + 1.f));
                        at.y = terrain_.heightAt(at.x, at.z);
                        std::fprintf(stderr, "ladder test: person %d to (%.2f %.2f %.2f) at %s (object %d), ladder %d points, middle (%.2f %.2f) heights %.2f..%.2f\n",
                                     who, at.x, at.y, at.z, n.item ? n.item->name.c_str() : "?", on, int(pts.size()), mid.x, mid.z, c.y + lo[1], c.y + hi[1]);
                        s.pos = {at.x, at.y + s.hip, at.z};
                        s.base = at.y;
                        s.heading = std::atan2(mid.x - at.x, -(mid.z - at.z));
                        s.vz = 0;
                        s.airborne = false;
                        s.platform = -1;
                        s.neighbours = world_.df2Neighbours(s.pos, 10.f, who);
                        ai_.testWalk(who, std::getenv("OPENDF_TESTLADDERWALK") != nullptr);
                    }
                }
                if (static bool once = false; !once && std::getenv("OPENDF_TESTBLD2") && ticks_ > 30) {
                    once = true;
                    for (size_t j = 0; j < world_.objects.size(); ++j)
                        if (const char* want = std::getenv("OPENDF_TESTBLD2");
                            world_.objects[j].item && world_.objects[j].item->aiFunction.rfind("bld2", 0) == 0 &&
                            (std::strcmp(want, "1") == 0 || world_.objects[j].item->name == want)) {   // "1" or an item name
                            const auto& o = world_.objects[j];
                            std::fprintf(stderr, "bld2 %s at mission (%.1f, %.1f)\n", o.item->name.c_str(), o.missionX, o.missionY);
                            if (auto m = world_.damage(int(j), 100000, 0x7fff); !m.empty()) pushMessage(m);
                            break;
                        }
                }
                if (std::getenv("OPENDF_TESTDF2ITEMS") && world_.df2Rules) {
                    // Testing (no shipped mission places these items): at tick 30
                    // a searchlight 15 m ahead facing the player (team 99), a
                    // Land Mine 4 m ahead, and a wall trigger with a "vwal
                    // crack" 6 m to the right; tick 240 the mine is put under
                    // the player's feet, tick 360 the trigger is shot to 0 hp.
                    // Their graphics (glight, gligtx, landmine, trigger, vwcrak,
                    // vwalx) are not on the disc either, so stand-ins are drawn:
                    // CRATE1 for the light and the wall (CRATE9 its husk),
                    // AMMOBOX for the mine and the trigger.
                    auto place = [&](int id, Vec3 at, float heading) {
                        const ItemDef* it = items_.byId(id);
                        if (!it) return -1;
                        const GpuModel* m = !it->graphic.empty() ? models_.get(vfs_, it->graphic) : nullptr;
                        const bool big = id == 104016 || id == 103062;
                        if (!m) m = models_.get(vfs_, big ? "CRATE1" : "AMMOBOX");
                        if (!m) return -1;
                        at.y = terrain_.heightAt(at.x, at.z);
                        const int k = world_.spawn(it, m, at, heading);
                        if (!it->husk.empty()) {
                            const GpuModel* h = models_.get(vfs_, it->husk);
                            world_.objects[size_t(k)].husk = h ? h : models_.get(vfs_, "CRATE9");
                        }
                        return k;
                    };
                    static int mine = -1, trig = -1, crack = -1, light = -1;
                    const Vec3 fw{std::sin(player_.yaw), 0, -std::cos(player_.yaw)}, rt{std::cos(player_.yaw), 0, std::sin(player_.yaw)};
                    if (ticks_ == 30) {
                        if (const int k = place(104016, player_.feet + fw * 15.f, player_.yaw + kPi); k >= 0) {
                            world_.objects[size_t(k)].team = 99;
                            light = k;
                            Searchlight l;
                            l.object = k;
                            lights_.push_back(l);
                        }
                        mine = place(104054, player_.feet + fw * 4.f, 0);
                        trig = place(101062, player_.feet + rt * 6.f, 0);
                        if ((crack = place(103062, player_.feet + rt * 8.f + fw * 2.f, player_.yaw)) >= 0) world_.objects[size_t(crack)].missionGroup = 1;
                    }
                    if (ticks_ == 240 && mine >= 0) {
                        WorldObject& m = world_.objects[size_t(mine)];
                        m.pos = {player_.feet.x, terrain_.heightAt(player_.feet.x, player_.feet.z), player_.feet.z};
                        m.base = m.pos.y;
                    }
                    if (ticks_ == 360 && trig >= 0)
                        if (auto m = world_.damage(trig, 1000, 0x7fff); !m.empty()) pushMessage(m);
                    if (ticks_ == 420 && light >= 0)
                        if (auto m = world_.damage(light, 1000, 0x7fff); !m.empty()) pushMessage(m);
                    if ((ticks_ == 300 || ticks_ == 400 || ticks_ == 480) && crack >= 0)
                        std::fprintf(stderr, "test: crack model %s wrecked %d; mine hp %d wrecked %d; light model %s wrecked %d\n",
                                     world_.objects[size_t(crack)].model->name.c_str(), int(world_.objects[size_t(crack)].wrecked),
                                     world_.objects[size_t(mine)].hp, int(world_.objects[size_t(mine)].wrecked),
                                     world_.objects[size_t(light)].model->name.c_str(), int(world_.objects[size_t(light)].wrecked));
                    if ((ticks_ % 32) == 0)
                        for (const auto& l : lights_)
                            std::fprintf(stderr, "light %d: heading %.1f deg pitch %.1f target %d counter %d\n", l.object,
                                         world_.objects[size_t(l.object)].heading * 180.f / kPi, world_.objects[size_t(l.object)].pitch * 180.f / kPi,
                                         l.target, l.counter);
                }
                sound_.tickDelayed();
                arsenal_.underwaterRule = mission_.version >= 0x0F;
                arsenal_.waterLevel = terrain_.waterLevel();
                tickCharges();
                // The player as a target: animated hit spheres (0x44dca4).
                world_.playerBody.present = !playerDead();
                world_.playerBody.origin = player_.origin();
                world_.playerBody.heading = player_.yaw;
                if (!playerBody_.angles().empty()) {
                    if (!playerChr_.ksa()) world_.playerBody.spheres = playerChr_.spheres(playerBody_.angles());
                    else if (const ItemDef* it = items_.byId(playerModelItem_ ? playerModelItem_ : playerItem_); it && !it->graphic.empty())
                        if (const GpuModel* bm = models_.get(vfs_, it->graphic); bm && !bm->lods.empty())
                            world_.playerBody.spheres = World::ksaSpheres(*bm, playerChr_, playerBody_.angles(), 0.f);
                }
                {
                    AiSystem::PlayerView pv;
                    pv.alive = !playerDead();
                    pv.origin = player_.origin();
                    const int mn = moveNumber(player_.move());
                    pv.moveNumber = mn >= 0 ? mn : 2;
                    pv.side = kPlayerSide;
                    pv.team = kPlayerTeam;
                    ai_.tick(world_, arsenal_, sound_, pv);
                    vehicles_.tick(world_, ai_, arsenal_, sound_, pv);
                }
                // The camera's pivot and base yaw follow the body (0x481184).
                {
                    const Vec3 o = playerDead() ? deadBodyOrigin() : player_.origin();
                    camLag_ = world_.nearest(o, camPivot_) - camPivot_;
                    if (std::abs(camLag_.x) > 50.f || std::abs(camLag_.z) > 50.f) camPivot_ = o, camLag_ = {0, 0, 0};
                    else camPivot_ = camPivot_ + camLag_ * (1.f / 16.f);
                    float dy = (kPi * 0.5f - (playerDead() ? deadYaw_ : player_.yaw)) - camYaw_;
                    dy -= std::floor(dy / (2 * kPi) + 0.5f) * 2 * kPi;
                    camYaw_ += dy / 32.f;
                }
                // plyr (0x45a27c): every 64 ticks the route's points count as
                // reached for the player's side.
                if ((ticks_ & 63) == 0 && !playerDead())
                    ai_.playerRoute({float(player_.missionX()), 0, float(player_.missionY())}, kPlayerSide);
                // The player's body animates like any soldier's (30 Hz): the
                // current move's animation, whose events make footsteps.
                if (ticks_ & 1) {
                    if (!playerDead() && player_.move() != playerMove_) {
                        playerMove_ = player_.move();
                        if (const AnimMove* mv = playerMoves_.find(playerMove_)) playerBody_.start(playerChr_, mv->animation);
                    }
                    const int ev = playerBody_.tick(playerChr_);
                    if (playerDead()) {
                        const auto& r = playerBody_.rootStep();
                        const Vec3 fw{std::sin(deadYaw_), 0, -std::cos(deadYaw_)}, lf{-std::cos(deadYaw_), 0, -std::sin(deadYaw_)};
                        deadShift_ = deadShift_ + fw * r[0] + lf * r[1];
                    }
                    const ItemDef* soldier = items_.byId(kItemPlayerSoldier);
                    if (ev) animEvent(ev, player_.origin(), soldier ? soldier->phraseSet : 0, kPlayerTeam);
                }
                for (const auto& e : world_.takeAnimEvents()) {
                    const auto& o = world_.objects[size_t(e.object)];
                    animEvent(e.event, o.pos, o.item ? o.item->phraseSet : 0, o.team);
                }
                // Ambience (0x45a5a8 dec0, 0x45a830 tree, 0x47f64c palm, 0x45a734..
                // snd0..2): the sound (4 m up for dec0/tree/palm; trees and palms
                // only while standing), then the next wait from rand x 2 (r):
                // dec0/tree/palm ((r >> 14) + 5) << 6, snd0 ((r*6 >> 16) + 20) << 6,
                // snd1 ((r*4 >> 16) + 8) << 6, snd2 ((r*2 >> 16) + 3) << 6. Section 0
                // objects are visited every tick (think at wait <= 0, then
                // wait - 1); section 1 when (tick & 31) == slot & 31 and section
                // 2 when (tick & 63) == slot & 63, a visit taking 32 / 64 off a
                // positive wait or else thinking (0x436c74).
                for (auto& a : ambients_) {
                    bool think = false;
                    if (a.section == 1 || a.section == 2) {
                        const uint32_t mask = a.section == 1 ? 31 : 63;
                        if ((ticks_ & mask) != (uint32_t(a.slot) & mask)) continue;
                        if (a.wait > 0) a.wait -= int(mask + 1);
                        else think = true;
                    } else {
                        think = a.wait <= 0;
                        --a.wait;
                    }
                    if (!think) continue;
                    const auto& o = world_.objects[size_t(a.object)];
                    const std::string& f = o.item->aiFunction;
                    Vec3 at = o.pos;
                    const bool sound = f.rfind("snd", 0) == 0;   // snd0..3: at the marker itself
                    if (!sound) at.y += 4.f;
                    if (f == "tree" && !o.alive()) { a.wait = 0x400; continue; }
                    if (o.alive() || sound) playSound(o.item->interruptSound, at);
                    const int r = crtRand() * 2;
                    a.wait = f == "snd0" ? (((r * 6) >> 16) + 20) << 6 : f == "snd1" ? (((r * 4) >> 16) + 8) << 6
                           : f == "snd2" ? (((r * 2) >> 16) + 3) << 6 : ((r >> 14) + 5) << 6;
                    // snd3 (0x402730): acceleration·64 + rand·axle_dist·64 ticks
                    // ((rand · (axle << 16)) >> 26 + (acc << 10) >> 4).
                    if (f == "snd3")
                        a.wait = int((int64_t(r & 0xFFFF) * int64_t(o.item->axleDist * 65536.f)) >> 26) + int(o.item->acceleration * 64.f);
                    if (a.section != 1 && a.section != 2) --a.wait;   // the visit's decrement follows the think
                }
                // 0x449830: the damage flash fades 2 per tick.
                damageFlash_ = damageFlash_ > 1 ? damageFlash_ - 2 : 0;
                // 0x446148, every 64 ticks: a dead player ends the mission
                // (0x44663c(2)); no side wins.
                if (playerDead() && missionResult_ < 0 && (ticks_ & 63) == 0) endMission(0);
                if (missionResult_ < 0) script_.tick(*this);
            }
            std::vector<std::string> msgs;
            // The listener (0x4735b8): the player, or the external camera once dead.
            {
                const Vec3 at = playerDead() ? viewEye() : player_.origin();
                const Vec3 f = viewDir();
                sound_.setListener(at, std::atan2(f.x, -f.z), &terrain_);
                arsenal_.setListener(at);
            }
            arsenal_.update(dt, trigger, trigger && !prevTrigger, player_, world_, msgs);
            prevTrigger = trigger;
            for (auto& m : msgs) pushMessage(m);
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
            // The largest of the original's modes (640x480, 800x600,
            // 1024x768) that fits the window height.
            s.modeW = s.h >= 768 ? 1024 : s.h >= 600 ? 800 : 640;
            // DF2.EXE's command map runs in 640x480: opening it sets
            // resolution index 4 (0x45da2a -> 0x45cbc0, the old index kept in
            // [0x4d2dd8]), closing restores it (0x45d9ea); the mode set
            // reloads the fonts for the new width (0x45cbfa -> 0x45c0a0:
            // f_sml%i and f_LG%i, i = min(width, 800)) and the HUD strips
            // (0x45cc06 -> 0x443bd0). So the map's text is F_SML640 /
            // F_LG640 at any display mode.
            useHudMode(df2Hud_.on && showMap_ ? 640 : s.modeW);
            // The 3D view: rows letterbox/8 .. 1 - letterbox/8 of the height
            // (0x404650); Delta Force 2: the rows between its strips, DHT_h ..
            // H - DHB_h (DF2.EXE 0x45cb40).
            s.playY = float(letterbox_) * s.h / 8.f;
            s.playH = std::max(1.f, s.h - 2 * s.playY);
            if (df2Hud_.on && df2Hud_.top.texture && df2Hud_.bottom.texture) {
                s.playY = float(df2Hud_.top.height) * s.art();
                s.playH = std::max(1.f, s.h - s.playY - float(df2Hud_.bottom.height) * s.art());
            }
            if (o_.noHud) { s.playY = 0; s.playH = s.h; }   // testing: the 3D view only, whole window

            glViewport(0, 0, dw, dh);
            glClearColor(0, 0, 0, 1);
            glClearStencil(0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            // 3D view between the two HUD bars.
            glViewport(0, int(s.h - s.playY - s.playH), dw, int(s.playH));
            // Focal length W/2 per 640 of width (FOV 90 across, 0x404489), or
            // the binoculars' FOV 22 (0x40423d); the letterbox crops the height.
            const float tanHalfX = binoculars_ && !playerDead() ? std::tan(11.f * kPi / 180.f) : 1.f;
            const float fovY = 2.f * std::atan(tanHalfX * std::tan(kOriginalFovY / 2) * s.playH / (s.h * 0.75f));
            viewFovY_ = 2.f * std::atan(std::tan(kOriginalFovY / 2) * s.playH / (s.h * 0.75f));
            Mat4 proj = perspective(fovY, s.w / s.playH, 0.1f, viewDistance_ * 1.1f);
            // With the binoculars up the camera carries their random offset (0x404246).
            Player cam = player_;
            if (binoculars_) { cam.yaw += binoYaw_; cam.pitch += binoPitch_; }
            if (binoculars_) scopeFocal_ = 320.f / tanHalfX;
            Mat4 view = playerDead() ? lookDir(viewEye(), viewDir(), {0, 1, 0}) : lookDir(player_.eye(), cam.forward(), {0, 1, 0});
            if (o_.viewModelOnly) {
                glClearColor(1, 0, 1, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                drawViewModel(s);
                glViewport(0, 0, dw, dh);
            } else {
                // DF2 draws only the command map while it is open (0x45c610).
                if (!(df2Hud_.on && showMap_)) drawWorld(view, proj, binoculars_ && !playerDead());
                // The weather spawns around the last rendered frame's camera (0x46b780's end).
                weather_.setCamera(viewEye());
                if (df2Hud_.on && showMap_) {
                    glViewport(0, 0, dw, dh);
                } else if (df2Hud_.on) {
                    // DF2.EXE 0x45c610: the scope inset, the night vision
                    // (0x449450), then the weapon pass (0x448980).
                    drawZoomInset(s, view, fovY);
                    glViewport(0, 0, dw, dh);
                    if (nvg_ && !showMap_) {
                        hud_.begin(dw, dh);
                        hud_.nightVision(s.w * 0.5f, s.playY + s.playH * 0.5f, float(s.modeW) * 5.f / 32.f * s.art());
                        hud_.end();
                    }
                    glViewport(0, int(s.h - s.playY - s.playH), dw, int(s.playH));
                    drawViewModel(s);
                } else {
                    drawViewModel(s);
                    drawZoomInset(s, view, fovY);
                }
                glViewport(0, 0, dw, dh);
                if (!o_.noHud) drawHud(s, proj * view);
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
                if (audioOut_) {
                    // The soundtrack of this 1/60 s step.
                    audioClock_ += Audio::kRate / 60.0;
                    const int n = int(audioClock_);
                    audioClock_ -= n;
                    std::vector<int16_t> pcm(size_t(n) * 2);
                    audio_.mix(pcm.data(), n);
                    std::fwrite(pcm.data(), 2, pcm.size(), audioOut_);
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
            if (!running && video) {
                pclose(video);
                video = nullptr;
                if (audioOut_) {
                    // Put the soundtrack into the recording.
                    std::fclose(audioOut_);
                    audioOut_ = nullptr;
                    const std::string pcm = o_.record + ".pcm", tmp = o_.record + ".tmp.mp4";
                    char cmd[2048];
                    std::snprintf(cmd, sizeof cmd,
                                  "ffmpeg -loglevel error -y -i \"%s\" -f s16le -ar %d -ac 2 -i \"%s\" -c:v copy -c:a aac -b:a 128k "
                                  "-shortest \"%s\" && mv \"%s\" \"%s\"",
                                  o_.record.c_str(), Audio::kRate, pcm.c_str(), tmp.c_str(), tmp.c_str(), o_.record.c_str());
                    if (std::system(cmd) != 0) std::fprintf(stderr, "could not add the soundtrack to %s\n", o_.record.c_str());
                    std::remove(pcm.c_str());
                }
                std::printf("recorded %s\n", o_.record.c_str());
            }

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

int runGame(Vfs& vfs, const GameOptions& options, MissionOutcome* outcome, uint32_t* flags) {
    if (flags) savedCampaignFlags() = *flags;
    Game game(vfs, options);
    const int rc = game.run();
    if (outcome) *outcome = game.outcome();
    if (flags) *flags = savedCampaignFlags();
    return rc;
}

} // namespace df
