// Playing a mission in first person.
#pragma once

#include "../core/Pff.h"

#include <filesystem>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace df {

struct GameOptions {
    std::string mission = "C02M01";
    std::filesystem::path gameDir;       // where DF.EXE is looked for
    int width = 1280, height = 720;
    // Testing aids: render after `frames` frames to a .ppm and exit.
    std::string screenshot;
    int frames = 1;
    bool hasCamera = false;
    float camera[5] = {0, 0, 0, 0, 0};   // x, y (north), height, yaw, pitch
    int weapon = -1;                     // slot index
    int stance = -1;                     // 0 stand, 1 crouch, 2 prone
    int zoom = 0;                        // zoom presses
    bool holdFire = false;
    bool walkForward = false;
    bool showMap = false;
    bool viewModelOnly = false;          // testing: draw only the weapon on magenta
    bool noHud = false;                  // testing (screenshots): no HUD, the 3D view fills the window
    bool killEnemies = false;            // testing: every side-2 soldier dies at the start
    // Scripted runs: a timed input script (see docs/TESTING.md), shown in a
    // visible window in real time (`watch`) and/or recorded to a video file.
    std::string inputScript;
    // Until the Change Gear menu exists: the primary weapon ("M4", "M40",
    // "MP5", "SAW") and the secondary kit ("Ammo" or ".45"); a new soldier in
    // the original has M4/M203 + Ammo (DF.EXE 0x40ee14).
    // Change Gear choices (loadoutRow names): primary, kit, sidearm.
    std::string primary = "M4", secondary = "Ammo", sidearm = ".45";
    // Delta Force 2's Change Gear (soldier record +0x2b8..+0x2c8): weapon
    // types; -1 = take the names above.
    int df2Sidearm = -1, df2Primary = -1, df2Secondary = -1, df2Gear = 0, df2Grenade = 26;
    // Until the soldier screens exist: the name shown in the bottom bar (the
    // original shows the soldier's name, e.g. "ROK").
    std::string soldierName;
    bool mixerHook = true;               // DF1: the mixer's mode-3 hook (on from start-up until a mission has ended)
    bool female = false;                 // the soldier's face is a female one (profile +0x640 >= 8)
    // The player's person variant (organic +0x98): GOG Df.exe 0x407a90 reads
    // [0x67f430] from the face table 0x4d0490 (stride 0x84) by profile
    // +0x640, 0x4829f0 stores it; the person draw (0x481330) puts it in the
    // animated-material slot 1.
    int faceVariant = 0;
    // GPS icons option ("gpsicon"): 0 no soldiers on the command map,
    // 1 (the original's default) only flagged ones, 2 all.
    int gpsIcons = 1;
    // df.cfg "attributes" (Difficulty settings): 0x80 enemy AI hard, 0x100
    // player health easy, 0x200 no squad members, 0x800 power ups on.
    uint32_t attributes = 0;
    // Delta Force 2: df2.cfg "attributes" ([0x653000]; default 2): 0x10 enemy
    // AI hard, 0x20 squad members off, 0x80 tracers off, 0x100 friendly-fire
    // warning off (Options3).
    uint32_t df2Attributes = 2;
    // Delta Force 2: df2.cfg "sp_wind" (GOG Df2.exe [0x67cf4c], 0x43c280):
    // single player's wind, 0 the mission's, 1 none, 2 random.
    int df2Wind = 0;
    // df.cfg CONTROLS / MUSIC: crosshairs (0x40200d), flipmouse and
    // mousescale (0x42eba1: turn per count = mousescale << 14 binary angle),
    // dialog_volume (0x472f44).
    bool crosshairs = true, flipMouse = false;
    int mouseScale = 128, dialogVolume = 200;
    // df.cfg showgun: the view model is drawn only when set (GOG Df.exe
    // 0x407a90 -> [0x67f514], read by 0x483f30).
    bool showGun = true;
    // Delta Force 2: the scope sensitivity [0x67ccfc] (soldier +0x5d8 +
    // 0x80, GOG Df2.exe 0x43c352), the scoped mouse turn rate (0x41eb50).
    int df2ScopeSens = 0x80;
    // df.cfg DISPLAY gamma (default 128): the display table's gamma, the
    // last colour stage of every Delta Force frame (GOG Df.exe 0x402940).
    int displayGamma = 128;
    // df.cfg DISPLAY window_size: the letterbox, 1 or 2 eighths of the
    // height per band (GOG Df.exe 0x407a90 -> [0x67f4f8], 0x404ba0 clamps).
    int letterbox = 1;
    // The soldier's key bindings (profile +0x6a0), applied over the game
    // table at mission start (0x41cca0): DF.EXE name, VK | mouse mask << 16.
    std::vector<std::pair<std::string, uint32_t>> bindings;
    bool watch = false;
    std::string record;
};

// What the front end needs back from a mission (debriefing, profile).
struct MissionOutcome {
    bool played = false;     // the mission ran
    bool won = false;        // side 1 won
    int kills = 0;           // people the player killed
    bool quit = false;       // the window was closed: leave the program (exit code 2)
    // The in-mission option toggles as the mission ended, copied back to the
    // menu's settings (GOG Df.exe 0x401e70 -> 0x407980).
    bool crosshairs = true, showGun = true;
    // Delta Force: earlier attempts of this run won and then replayed with
    // Space (GOG Df.exe 0x404e30 -> 0x401e70 -> 0x407de0 records each one).
    bool earlierWin = false;
    int earlierKills = 0;
    int letterbox = 0;       // the letterbox at the end (0x407980 -> df.cfg window_size); 0 = not played
    // Runs finished earlier with Space ("replay mission"), in order: GOG
    // Df2.exe 0x466ad0 records each one (0x464140 -> 0x43c6f0) before the
    // replay starts.
    struct Run { bool won = false; int kills = 0; };
    std::vector<Run> replayed;
};

// `campaignFlags`: in, the soldier's campaign flags; out, updated only when
// the mission was won (DF.EXE 0x401b47).
int runGame(Vfs& vfs, const GameOptions& options, MissionOutcome* outcome = nullptr, uint32_t* campaignFlags = nullptr);

} // namespace df
