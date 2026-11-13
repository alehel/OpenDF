// Playing a mission in first person.
#pragma once

#include "../core/Pff.h"

#include <string>

namespace df {

struct GameOptions {
    std::string mission = "C02M01";
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
};

int runGame(Vfs& vfs, const GameOptions& options);

} // namespace df
