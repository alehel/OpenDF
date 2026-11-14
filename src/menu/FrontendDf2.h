// Delta Force 2's front end (DF2.EXE's menu states 0x45ec40; docs/OTHER-GAMES.md):
// DF2.MNU (sections [SOUNDS] [ARGBS] [FONTS] [IMAGES] [MENUS] [BUTTONS],
// each button naming its menu), 640x480 screens with JPG/TGA/PCX art, the
// soldier records of df2plyrs.sav and the single-player flow: Choose
// Soldier -> Main Menu -> Campaign / Quick Missions -> Mission Briefing (->
// Change Gear) -> the mission -> back to Campaign / Quick Missions (DF2 has
// no debriefing screen).
#pragma once

#include "Frontend.h"

namespace df {

int runFrontendDf2(Vfs& vfs, const FrontendOptions& options);

} // namespace df
