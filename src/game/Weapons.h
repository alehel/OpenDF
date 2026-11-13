// Player weapons: magazines, fire modes, reloads, scopes and simulated
// bullets (gravity drop and travel time) hitting terrain and objects.
//
// The weapon numbers below are real-world approximations, not values from
// DF.EXE (not yet located there).
#pragma once

#include "Player.h"
#include "World.h"

#include <random>
#include <string>
#include <vector>

namespace df {

enum class FireMode { Single, Burst, Auto };

struct WeaponMode {
    const char* name;           // as shown on the HUD (DFGAME.BIN wording)
    FireMode fire;
};

struct WeaponDef {
    std::vector<WeaponMode> modes;
    const char* viewModel;      // first-person .3DI
    int icon;                   // frame in WEAP640.PCX
    // First-person model offset from the eye, from DF.EXE's player weapon
    // table (record +0x78): forward, left, up in 1/256 m.
    int gunOffset[3];
    int magazine, spareMags;
    float roundsPerMinute;
    float muzzleVelocity;       // m/s
    float spreadDeg;            // standing, still, unzoomed
    std::vector<float> zoom;    // magnifications of the zoom inset (none = no scope)
    int damage;
    float reloadSeconds;
    float recoilDeg;
};

const std::vector<WeaponDef>& weaponDefs();

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

    void init();
    // Selects a weapon slot; `mode` < 0 keeps that weapon's current fire mode.
    void select(int slot, int mode = -1);
    void cycle();
    void cycleMode();
    void reload();
    void toggleZoom();

    // Fires as needed and advances bullets. Returns messages for the HUD.
    void update(float dt, bool triggerDown, bool triggerPressed, Player& player, World& world,
                std::vector<std::string>& messages);

    const Slot& current() const { return slots_[current_]; }
    const WeaponDef& def() const { return weaponDefs()[slots_[current_].def]; }
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

    std::vector<Slot> slots_;
    int current_ = 0;
    int zoomLevel_ = 0;
    float cooldown_ = 0, reloadTimer_ = 0, recoil_ = 0, flashTimer_ = 0;
    int burstLeft_ = 0;
    std::vector<Projectile> bullets_;
    std::vector<Impact> impacts_;
    std::mt19937 rng_{1998};
};

} // namespace df
