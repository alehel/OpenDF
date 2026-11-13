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
    Count
};

class Controls {
public:
    // Reads the defaults from DF.EXE if possible; false = built-in copy used.
    bool init(const ExeImage* exe);
    bool fromExe() const { return fromExe_; }

    // The action bound to a key press, or Action::Count.
    Action actionFor(SDL_Scancode key) const;
    // Whether an action's key or mouse button is held.
    bool down(Action a, const Uint8* keys, Uint32 mouseButtons) const;
    std::string describe(Action a) const;

private:
    struct Binding { SDL_Scancode key = SDL_SCANCODE_UNKNOWN; int mouse = 0; };
    Binding bind_[int(Action::Count)];
    bool fromExe_ = false;
};

} // namespace df
