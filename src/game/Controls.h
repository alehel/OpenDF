// Key bindings: the original's default table, read from the user's DF.EXE
// (0x4df4f8: 44-byte entries, name[32], Windows virtual-key code with mouse
// buttons in the high word (1 left, 2 right), flags, action id), with a
// built-in copy as fallback. The mouse (look, left = fire, right = scope) is
// added on top as a modern amenity.
#pragma once

#include "../core/ExeImage.h"

#include <SDL.h>

#include <string>
#include <vector>

namespace df {

enum class Action {
    MoveForward, MoveBack, SlideLeft, SlideRight, TurnLeft, TurnRight, LookUp, LookDown, CenterView, Jump,
    NoRun, Crouch, Prone, Stand, Fire, Scope, Magazine, Cycle,
    Weapon0, Weapon1, Weapon2, Weapon3, Weapon4, Weapon5, Weapon6, Weapon7, Weapon8, Weapon9,
    NextWaypoint, BigMap, MissionGoals, MapZoomIn, MapZoomOut, Crosshairs, ShowGun, Escape,
    Binoculars, Pause, OldMessages, ShowFriendly, LetterBox, Drop, Briefing, Help,
    // Delta Force 2: scope zeroing (X / Z) and night vision (N).
    ScopeInc, ScopeDec, NightVision,
    // Delta Force 2: the map's elevation view (F9 mapterrain / F10
    // mapelevation, actions 362 / 363).
    MapTerrain, MapElevation,
    // GOG Df2.exe: the video camera's inset (VCam, F12, id 0x34).
    VCam,
    // Delta Force: the squad-mate inset on/off (pinp, F8, command 0x1e)
    // and its next squad mate (fo, F9, command 0x1f; GOG Df.exe 0x477d40).
    SquadInset, SquadCycle,
    Count
};

class Controls {
public:
    // Reads the defaults from DF.EXE if possible; false = built-in copy used.
    bool init(const ExeImage* exe);
    // Delta Force 2's defaults from DF2.EXE (0x4a5cd4: 44-byte records {id,
    // name[32], key, flags}); false = left as they were.
    bool initDf2(const ExeImage& exe);
    bool fromExe() const { return fromExe_; }

    // The action bound to a key press, or Action::Count.
    Action actionFor(SDL_Scancode key) const;
    // Whether an action's key or mouse button is held.
    bool down(Action a, const Uint8* keys, Uint32 mouseButtons) const;
    std::string describe(Action a) const;
    // Overwrites one game-table entry by DF.EXE name (key = VK | mouse mask
    // << 16; 0 unbinds), as 0x41cbfc does with the profile's bindings.
    void set(const std::string& name, uint32_t key);
    // Windows virtual-key codes <-> SDL scancodes (the keys DF.EXE knows).
    static SDL_Scancode fromVirtualKey(uint32_t vk);
    static uint32_t toVirtualKey(SDL_Scancode sc);

private:
    struct Binding { SDL_Scancode key = SDL_SCANCODE_UNKNOWN; int mouse = 0; };
    Binding bind_[int(Action::Count)];
    bool fromExe_ = false;
};

} // namespace df
