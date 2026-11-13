// Playing a mission in first person.
#pragma once

#include "../core/Pff.h"

#include <filesystem>
#include <string>

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
    bool killEnemies = false;            // testing: every side-2 soldier dies at the start
    // Scripted runs: a timed input script (see docs/TESTING.md), shown in a
    // visible window in real time (`watch`) and/or recorded to a video file.
    std::string inputScript;
    // Until the Change Gear menu exists: the primary weapon ("M4", "M40",
    // "MP5", "SAW") and the secondary kit ("Ammo" or ".45"); a new soldier in
    // the original has M4/M203 + Ammo (DF.EXE 0x40ee14).
    std::string primary = "M4", secondary = "Ammo";
    // Until the soldier screens exist: the name shown in the bottom bar (the
    // original shows the soldier's name, e.g. "ROK").
    std::string soldierName;
    // GPS icons option ("gpsicon"): 0 no soldiers on the command map,
    // 1 (the original's default) only flagged ones, 2 all.
    int gpsIcons = 1;
    bool watch = false;
    std::string record;
};

int runGame(Vfs& vfs, const GameOptions& options);

} // namespace df
