// Player weapons: magazines, fire modes, reloads, scopes and simulated
// bullets (gravity drop and travel time) hitting terrain and objects.
//
// Weapon values come from DF.EXE's own weapon tables, read from the user's
// copy at start-up (see docs/FORMATS.md); the built-in copies of those values
// are used if DF.EXE is missing or not the expected version. Spread, damage
// and recoil are still OpenDF approximations.
#pragma once

#include "../core/ExeImage.h"
#include "../formats/Mission.h"
#include "Player.h"
#include "World.h"

#include <random>
#include <string>
#include <vector>

namespace df {

enum class FireMode { Single, Burst, Auto };

// The original's logic runs at 60 ticks per second.
constexpr float kTick = 1.f / 60.f;

struct WeaponMode {
    const char* name;           // fallback if DFGAME.BIN is unavailable
    FireMode fire;
    int table;                  // DF.EXE player weapon table index (= DFGAME.BIN string)
    int fireTicks;              // ticks between rounds (table +0x9C)
};

struct WeaponDef {
    std::vector<WeaponMode> modes;
    std::string viewModel;      // first-person .3DI (ITEMS.DEF item at table +0x64)
    int icon;                   // frame in WEAP640.PCX
    // First-person model offset from the eye, from DF.EXE's player weapon
    // table (record +0x78): forward, left, up in 1/256 m.
    int gunOffset[3];
    int magazine;               // rounds per magazine (gun table +0x08)
    int magazines;              // n, gun table +0x04: spares n-1, or 2n-1 for the primary with the "Ammo" kit
    float muzzleVelocity;       // m/s
    float spreadDeg;            // standing, still, unzoomed
    std::vector<float> zoom;    // magnifications of the zoom inset (none = no scope)
    int damage;
    float reloadSeconds;
    float recoilDeg;
    bool oneHanded = false;     // held in one hand: the soldier uses the *_1HW moves
};

// Built-in copy of the original's values (fallback).
const std::vector<WeaponDef>& builtinWeaponDefs();
// Index of a weapon by short name (".45", "M4", "M40", "MP5", "SAW"); -1 for
// "Ammo" or anything unknown.
int weaponIndex(const std::string& name);

struct Projectile {
    Vec3 pos, vel;
    float life;
    int damage;
};

struct Impact {
    Vec3 pos;
    float age;
    bool object;
};

class Arsenal {
public:
    struct Slot { int def; int rounds; int mags; int mode; };

    // `gameText`: DFGAME.BIN strings, for the weapon names (may be empty).
    // `exe`: the original DF.EXE, for the weapon tables (may be null).
    // The original carries a sidearm and the primary and secondary kits
    // chosen on Change Gear (indices into the weapon list; see weaponIndex).
    // `secondary` < 0 is the "Ammo" kit (a new soldier's default): no second
    // gun, and the primary gets 2n-1 spare magazines instead of n-1
    // (DF.EXE 0x44b598, profile +0x688 == 1).
    void init(const std::vector<std::string>& gameText = {}, const ExeImage* exe = nullptr,
              const ItemDefs* items = nullptr, int primary = 1, int secondary = -1);
    // True if the weapon values were read from DF.EXE.
    bool fromExe() const { return fromExe_; }
    const std::string& modeName() const;
    // Selects a weapon slot (0 sidearm, 1 primary, 2 secondary); `mode` < 0
    // keeps that weapon's current fire mode. A mode the weapon does not have
    // does nothing, like an empty slot in the original.
    void select(int slot, int mode = -1);
    void cycle();
    void cycleMode();
    void reload();
    void toggleZoom();

    // Fires as needed and advances bullets. Returns messages for the HUD.
    void update(float dt, bool triggerDown, bool triggerPressed, Player& player, World& world,
                std::vector<std::string>& messages);

    const Slot& current() const { return slots_[current_]; }
    const WeaponDef& def() const { return defs_[slots_[current_].def]; }
    const WeaponMode& mode() const { return def().modes[slots_[current_].mode]; }
    int slotIndex() const { return current_; }
    // Magnification of the zoom inset (1 = not zoomed).
    float zoom() const { return zoomLevel_ ? def().zoom[zoomLevel_ - 1] : 1.f; }
    bool zoomed() const { return zoomLevel_ > 0; }
    bool reloading() const { return reloadTimer_ > 0; }
    float recoil() const { return recoil_; }
    float recoilPitch() const;   // radians the view is currently kicked up
    float flash() const { return flashTimer_; }
    const std::vector<Impact>& impacts() const { return impacts_; }

private:
    void fire(Player& player, World& world);

    std::vector<WeaponDef> defs_;
    bool fromExe_ = false;
    std::vector<Slot> slots_;
    std::vector<std::vector<std::string>> names_;   // per weapon, per mode
    int current_ = 0;
    int zoomLevel_ = 0;
    float cooldown_ = 0, reloadTimer_ = 0, recoil_ = 0, flashTimer_ = 0;
    int burstLeft_ = 0;
    std::vector<Projectile> bullets_;
    std::vector<Impact> impacts_;
    std::mt19937 rng_{1998};
};

} // namespace df
