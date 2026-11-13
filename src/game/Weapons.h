// Player weapons: magazines, fire modes, reloads, scopes and simulated
// bullets (gravity drop and travel time) hitting terrain and objects.
//
// Weapon values come from DF.EXE's own weapon tables, read from the user's
// copy at start-up (see docs/FORMATS.md); the built-in copies of those values
// are used if DF.EXE is missing or not the expected version. Spread, damage
// are still OpenDF approximations.
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
    float recoilDeg;            // unused: the original kicks only the gun model (see kick())
    bool oneHanded = false;     // held in one hand: the soldier uses the *_1HW moves
    // Gun table (DF.EXE 0x4E9060) projectile fields:
    int power = 1;              // +0x10: damage 1 << power, halving each `falloff` ticks of flight
    int falloff = 30;           // +0x14, ticks
    int lifetime = 70;          // +0x18, ticks of flight
    int accuracy = 1;           // +0x1C: spread ±0.0824°·(A−1) per axis, none at 1
    int gunFlags = 0;           // +0x30: 0x10 hand grenade (thrown, explodes on impact), 0x01 M203, 0x40 LAW
    bool thrown() const { return gunFlags & 0x10; }
};

// Built-in copy of the original's values (fallback).
const std::vector<WeaponDef>& builtinWeaponDefs();
// Index of a weapon by short name (".45", "M4", "M40", "MP5", "SAW",
// "Grenade"); -1 for "Ammo" or anything unknown.
int weaponIndex(const std::string& name);

struct Projectile {
    Vec3 pos, vel;              // metres, metres per second
    int age = 0, lifetime = 0;  // ticks
    int power = 1, falloff = 30;
    int flags = 0;              // gun table +0x30
    float tickClock = 0;
    // DF.EXE 0x44f3e4: flags & 0x51 explode on impact.
    bool explosive() const { return flags & 0x51; }
};

// A bullet hit, for spawning the impact effect (DF.EXE 0x44f71f: 0.25 m
// back along the path).
struct Impact {
    Vec3 pos;
    int object = -1;   // -1 = terrain, -2 = water
    int part = -1;     // people: body part
    bool explosive = false;   // grenade / M203 round: explodes here
    bool armed = true;        // explosive: armed (old enough or impact-armed)
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
    void selectGrenade() { if (grenadeSlot_ >= 0) select(grenadeSlot_); }
    void selectSecondary(int mode) { if (secondarySlot_ >= 0) select(secondarySlot_, mode); }
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
    bool firing() const { return cooldown_ > 0; }   // refire time running ([0x93334c] > 0)
    // Gun model kick (DF.EXE 0x449cbc, fire class 1): metres back, left, up
    // in the weapon's own axes; firing never moves the view.
    const float* kick() const { return kick_; }
    // Random aim offset taken when the scope comes up (0x44b870), radians.
    float scopeYaw() const { return scopeYaw_; }
    float scopePitch() const { return scopePitch_; }
    void cancelZoom() { zoomLevel_ = 0; }
    float flash() const { return flashTimer_; }
    // Hits since the last call.
    const std::vector<Projectile>& projectiles() const { return bullets_; }
    std::vector<Impact> takeImpacts() { std::vector<Impact> out; out.swap(impacts_); return out; }

private:
    void fire(Player& player, World& world);

    std::vector<WeaponDef> defs_;
    bool fromExe_ = false;
    std::vector<Slot> slots_;
    std::vector<std::vector<std::string>> names_;   // per weapon, per mode
    int current_ = 0;
    int zoomLevel_ = 0;
    float cooldown_ = 0, reloadTimer_ = 0, flashTimer_ = 0;
    float kick_[3] = {0, 0, 0};
    float scopeYaw_ = 0, scopePitch_ = 0;
    int burstLeft_ = 0;
    std::vector<Projectile> bullets_;
    std::vector<Impact> impacts_;
    int grenadeSlot_ = -1, secondarySlot_ = -1;
    std::mt19937 rng_{1998};
};

} // namespace df
