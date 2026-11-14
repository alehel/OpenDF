// Player weapons as DF.EXE has them: ten slots holding one weapon mode each
// (keys 0-9), modes from the player weapon table, ammunition kept per gun,
// and simulated projectiles (bullets, grenades, M203 and LAW rounds).
//
// All values come from DF.EXE's tables, read from the user's copy at
// start-up (see docs/FORMATS.md); built-in copies of the same tables are used
// if DF.EXE is missing or not the expected version.
#pragma once

#include <cmath>

#include "../core/ExeImage.h"
#include "../formats/Mission.h"
#include "Player.h"
#include "World.h"

#include <array>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace df {

// The original's logic runs at 60 ticks per second.
constexpr float kTick = 1.f / 60.f;

// Player weapon table record (0x4E97D0, 168 bytes, 39 modes).
struct ModeDef {
    int type = 0;               // +0x6C fire type: 1 gun, 2 launcher, 6 knife, 7 claymore, 8 detonator, 9 satchel, 10 laser, 11 grenade
    int gun = 0;                // +0x74 gun table index (ammunition is kept per gun)
    int item = 0;               // +0x64 first-person item (ITEMS.DEF id - 100000)
    int offset[3] = {};         // +0x78 first-person model offset: forward, left, up in 1/256 m
    int muzzleVertex = 0;       // +0x90 vertex of the first-person model the round leaves from
    int cooldown = 0;           // +0x9C ticks between rounds
    int scope = 0;              // +0xA0 zoom inset magnification
    int flags = 0;              // +0xA4: 0x1 zoom allowed, 0x4 rifle (two-handed), 0x20 full auto
    std::string name;           // DFGAME.BIN string of the mode number
    std::string viewModel;      // the first-person item's graphic
    int icon = -1;              // WEAP%i.PCX frame (0x484ae8), -1 none
    bool burst = false;         // 0x46aefc: modes 6, 8, 21, 35, 36 fire bursts of three
    bool oneHanded() const { return !(flags & 4); }
    bool automatic() const { return flags & 0x20; }
    bool zoomable() const { return (flags & 1) && scope > 1; }
    // Fire types OpenDF handles: 1 guns, 2 launchers, 6 knife, 7 claymore,
    // 8 radio detonator, 9 satchel, 10 laser designator, 11 grenade.
    bool works() const { return type == 1 || type == 2 || (type >= 6 && type <= 11); }
};

// Gun table record (0x4E9060, 52 bytes).
struct GunDef {
    int n = 0;                  // +0x04 magazines (n - 1 spare; 2n - 1 with the Ammo kit); rounds for launchers
    int magazine = 0;           // +0x08 rounds per magazine
    int velocity = 0;           // +0x0C m/s (moved at velocity / 62 per tick)
    int power = 0;              // +0x10 damage 1 << power, halving every `falloff` ticks
    int falloff = 1;            // +0x14
    int lifetime = 0;           // +0x18 ticks of flight
    int accuracy = 1;           // +0x1C spread ±(A−1)·30·0x8000 binary angle per axis
    int flags = 0;              // +0x30: 0x01 M203, 0x10 hand grenade, 0x40 LAW
    // +0x20 shot, +0x24 chamber / load, +0x28 reload, +0x2C dry fire (sound ids, 0 none)
    int sounds[4] = {0, 0, 0, 0};
};

// Things the weapons ask the game to do: place a charge (satchel 4020,
// claymore 4014 motion / 4040 radio; 1 m ahead, the player's heading), set
// off the player's satchels and radio claymores, ask for a fire mission
// (the laser designator: pressed, then the request 180 ticks later).
struct WeaponEvent {
    enum Kind { Place, Detonate, LaserPressed, LaserRequest } kind;
    int item = 0;
    Vec3 pos{0, 0, 0};
    float heading = 0;
};

// A sound the weapons asked for (DF.EXE 0x473220): id at a position.
struct SoundEvent { int id; Vec3 pos; bool shot = false; };

// A shot in flight.
struct Projectile {
    Vec3 pos, vel;              // metres, metres per second
    int age = 0, lifetime = 0;  // ticks
    int power = 1, falloff = 30;
    int flags = 0;              // gun +0x30
    int id = 0;                 // unique, for sounds that follow it
    Vec3 from{0, 0, 0};         // where it was fired from (the attacker, for death moves)
    int shooter = -3;           // world object that fired it (-3 the player)
    float tickClock = 0;
    // The round record's yaw/pitch (+0x30/+0x34, 0x44d450): the launch
    // direction, never changed in flight (0x44f3e0), so the grenade, M203 and
    // LAW models are drawn that way the whole time.
    float yaw = 0, pitch = 0;
    void orient() {
        yaw = std::atan2(vel.x, -vel.z);
        pitch = std::atan2(vel.y, std::sqrt(vel.x * vel.x + vel.z * vel.z));
    }
    // DF.EXE 0x44f3e4: flags & 0x51 explode on impact.
    bool explosive() const { return flags & 0x51; }
};

// A hit, for spawning the impact effect (DF.EXE 0x44f71f: 0.25 m back along
// the path).
struct Impact {
    Vec3 pos;
    int object = -1;          // -1 = terrain, -2 = water
    int part = -1;            // people: body part
    bool explosive = false;   // grenade / M203 / LAW round
    bool armed = true;        // explosive: armed (M203: more than 8 ticks of flight)
    bool law = false;         // LAW (gun flag 0x40): heavy blast (type 3) out of the water
    int flags = 0;            // the projectile's gun flags (0x20 shrapnel)
    uint32_t material = 0;    // objects: the hit face's material flags (0x44f90a picks effect and sound)
};

// The soldier's equipment (Change Gear): loadout rows of DF.EXE's item table
// (0x4E9610). Sidearm: 6 .45, 5 HS .22. Primary: 9 M4/M203, 23 M40,
// 12 Barrett, 15 MP5 SD, 11 SAW. Kit: 1 Ammo, 20 LAW, 21 Satchel,
// 24 Claymore. A new soldier has .45, M4/M203 and Ammo (0x40ee14).
struct Loadout { int sidearm = 6, primary = 9, kit = 1; };
// Row for a short name (".45", "HS22", "M4", "M40", "Barrett", "MP5", "SAW",
// "Ammo", "LAW", "Satchel", "Claymore"); -1 if unknown.
int loadoutRow(const std::string& name);

// The original's weapon tables for any soldier (DF.EXE's values; the player's
// Arsenal reads the same from the user's DF.EXE): mode -> gun, cooldown,
// +0xA4 flags, third-person item (+0x68, ITEMS.DEF id - 100000); gun ->
// record. The AI's weapons (BMS byte +0x46, "AI properties" 1) index
// 0x4eb168.
const ModeDef& modeInfo(int mode);
const GunDef& gunInfo(int gun);
int modeThirdPersonItem(int mode);
int aiWeaponMode(int index);

class Arsenal {
public:
    static constexpr int kSlots = 10;

    // `gameText`: DFGAME.BIN strings (mode names); `exe`: the original
    // DF.EXE (may be null: built-in tables); `items`: ITEMS.DEF.
    void init(const std::vector<std::string>& gameText, const ExeImage* exe, const ItemDefs* items, Loadout loadout);
    bool fromExe() const { return fromExe_; }

    // Keys 0-9 (0x44aa74): slot 0 laser designator, 1 knife, 2 sidearm,
    // 3-5 the primary's modes, 6-8 the kit's, 9 frag grenade. A slot without
    // a working mode does nothing.
    void select(int slot);
    // Ammunition crate (0x447b90 -> 0x44b634): every carried gun full again,
    // as at the mission start.
    void refill();
    void cycle();                 // 0x44b81c: next slot with a mode, wrapping
    void reload();                // 0x44a004
    void toggleZoom();
    void cancelZoom() { zoomLevel_ = 0; }

    // Runs the weapon on 60 Hz ticks. Messages go to the HUD.
    void update(float dt, bool triggerDown, bool triggerPressed, Player& player, World& world,
                std::vector<std::string>& messages);

    int slot() const { return current_; }
    // The mode in a slot (null if empty), e.g. for the help screen.
    const ModeDef* slotMode(int i) const { return i >= 0 && i < kSlots && slots_[size_t(i)] ? &modes_[size_t(slots_[size_t(i)])] : nullptr; }
    const ModeDef& mode() const { return modes_[size_t(slots_[size_t(current_)])]; }
    const GunDef& gun() const { return guns_[size_t(mode().gun)]; }
    const std::string& modeName() const { return mode().name; }
    // Ammunition of the current mode's gun (0x933354 / 0x9333d4 / 0x933454).
    bool chambered() const { return chambered_[size_t(mode().gun)] != 0; }
    int rounds() const { return mag_[size_t(mode().gun)]; }
    int spares() const { return spares_[size_t(mode().gun)]; }

    float zoom() const { return zoomLevel_ ? float(mode().scope) : 1.f; }
    bool zoomed() const { return zoomLevel_ > 0; }
    bool reloading() const { return reloadTicks_ > 0; }
    bool firing() const { return cooldown_ > 0; }   // refire time running ([0x93334c] > 0)
    // Gun model kick: metres back, left, up in the weapon's own axes.
    const float* kick() const { return kick_; }
    // Binoculars up ([0x93367c]): no firing, no scope.
    void setBinoculars(bool on) { binoculars_ = on; if (on) zoomLevel_ = 0; }
    float flash() const { return flashTimer_; }
    const std::vector<Projectile>& projectiles() const { return bullets_; }
    // Rounds not fired by the player's weapon (shrapnel); they hit and do
    // damage like any other.
    void addProjectile(Projectile p) { p.id = ++projectileIds_; p.orient(); bullets_.push_back(p); }
    // The first-person model's points (LOD 0 vertices in file order) by
    // graphic name, for the muzzle (player table +0x90); null if unknown.
    std::function<const std::vector<std::array<float, 3>>*(const std::string&)> modelPoints;
    std::vector<Impact> takeImpacts() { std::vector<Impact> out; out.swap(impacts_); return out; }
    // Knife strikes since the last call: the point 0.5 m along the view
    // from the body origin (0x42ca8c), for blast kind 2.
    // The listener, for bullets whizzing by (0x44f330).
    void setListener(Vec3 p) { listener_ = p; }
    // Rounds that hit the player: damage, body part, where they came from.
    struct PlayerHit { int damage; int part; Vec3 from; };
    std::vector<PlayerHit> takePlayerHits() { std::vector<PlayerHit> out; out.swap(playerHits_); return out; }
    std::vector<WeaponEvent> takeEvents() { std::vector<WeaponEvent> out; out.swap(events_); return out; }
    std::vector<SoundEvent> takeSounds() { std::vector<SoundEvent> out; out.swap(sounds_); return out; }
    std::vector<Vec3> takeStabs() { std::vector<Vec3> out; out.swap(stabs_); return out; }

private:
    void tick(bool triggerDown, Player& player, std::vector<std::string>& messages);
    void fire(Player& player);
    void startReload();
    void moveProjectiles(float dt, World& world);
    int& chamberedRef() { return chambered_[size_t(mode().gun)]; }
    int& magRef() { return mag_[size_t(mode().gun)]; }
    int& sparesRef() { return spares_[size_t(mode().gun)]; }

    std::vector<ModeDef> modes_;
    std::vector<GunDef> guns_;
    bool fromExe_ = false;
    std::array<int, kSlots> slots_{};   // mode per slot, 0 = empty
    int current_ = 3;
    int kit_ = 1;
    std::vector<int> chambered_, mag_, spares_;   // per gun
    int cooldown_ = 0, reloadTicks_ = 0, burstLeft_ = 0;
    bool pressed_ = false;
    float clock_ = 0;
    int zoomLevel_ = 0;
    float flashTimer_ = 0;
    float kick_[3] = {0, 0, 0};
    bool binoculars_ = false;
    std::vector<Projectile> bullets_;
    std::vector<Impact> impacts_;
    std::vector<Vec3> stabs_;
    std::vector<SoundEvent> sounds_;
    std::vector<WeaponEvent> events_;
    std::vector<PlayerHit> playerHits_;
    bool followUp_ = false;
    int projectileIds_ = 0;
    Vec3 listener_{0, 0, 0};    // firing the second or third round of a burst
    Vec3 soundPos_{0, 0, 0};   // the player's position for weapon handling sounds
    void sound(int field, int gun = -1);
    std::mt19937 rng_{1998};
};

} // namespace df
