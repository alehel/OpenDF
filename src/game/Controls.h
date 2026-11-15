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
    // GOG Df2.exe: scope magnification (scopemag, E, command 0xc2).
    ScopeMag,
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
    // Delta Force 2's one-shot dispatch, in its table's order (records with
    // flag 4, the held commands, are polled instead). A key press (GOG
    // Df2.exe 0x421be0): the first record with that key whose flags pass
    // the modifier test (0x40 needs Shift, 0x100 Ctrl; otherwise with
    // Shift or Ctrl down, unless the key is one of them, it needs 0x200);
    // Action::Count when none does or it is not an OpenDF action. Then,
    // as before, a held record bound to the key (OpenDF takes some of
    // them on the press). df2Table() is false when GOG's table was not read.
    bool df2Table() const { return !df2Recs_.empty(); }
    Action df2KeyAction(SDL_Scancode key, bool shift, bool ctrl) const;
    // A mouse button (0x420e10: 1 left, 2 right, 0x10 middle): the first
    // one-shot record whose mouse mask is exactly that.
    Action df2MouseAction(int mask) const;
    // Windows virtual-key codes <-> SDL scancodes (the keys DF.EXE knows).
    static SDL_Scancode fromVirtualKey(uint32_t vk);
    static uint32_t toVirtualKey(SDL_Scancode sc);

private:
    struct Binding { SDL_Scancode key = SDL_SCANCODE_UNKNOWN; int mouse = 0; };
    Binding bind_[int(Action::Count)];
    struct Df2Rec { std::string name; Action action; Binding bind; uint16_t flags; };
    std::vector<Df2Rec> df2Recs_;   // GOG Df2.exe 0x4b1710 order, flags +0x2a
    bool fromExe_ = false;
};

} // namespace df
