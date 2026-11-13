// The front end: DF.EXE's menu screens (docs/AUDIT.md M rows) driven by
// DF.MNU, the soldier profiles (playerNN.ply) and the single-player flow:
// splash -> Choose Soldier -> Main Menu -> Campaign -> Mission Briefing (->
// Change Gear) -> the mission -> Debriefing -> Campaign.
#pragma once

#include "../core/Pff.h"
#include "../game/Game.h"

#include <filesystem>

namespace df {

struct FrontendOptions {
    std::filesystem::path gameDir;   // where the profiles live (as DF.EXE: next to the game)
    int width = 1280, height = 960;
    GameOptions game;                // passed on to missions
    // Testing: open this screen ("soldier", "main", "campaign", "briefing",
    // "loadout", "difficulty", "debrief") and save a screenshot after `frames`.
    std::string startScreen, screenshot;
    int frames = 1;
};

int runFrontend(Vfs& vfs, const FrontendOptions& options);

} // namespace df
