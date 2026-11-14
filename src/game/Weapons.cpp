#include "Weapons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../core/Pff.h"

namespace df {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kModeCount = 39, kGunCount = 28, kRowCount = 28;
// DF.EXE (US release) tables; see docs/FORMATS.md.
constexpr uint32_t kModeTable = 0x4E97D0, kModeRecord = 168;
constexpr uint32_t kGunTable = 0x4E9060, kGunRecord = 52;
constexpr uint32_t kRowTable = 0x4E9610, kRowRecord = 16;
constexpr int kReloadTicks = 0x78;   // 0x44a004
constexpr int kFragRow = 26;         // slot 9: always the frag grenade (0x44b598)

// Built-in copies of DF.EXE's tables (used when DF.EXE cannot be read).
// type, gun, item, offset x3, muzzle vertex, cooldown, scope, flags  (player table 0x4E97D0, +0x6C +0x74 +0x64 +0x78.. +0x90 +0x9C +0xA0 +0xA4)
const int kBuiltinModes[kModeCount][10] = {
    {0, 0, 0, 0, 0, 0, 0, 30, 0, 0},
    {6, 0, 4005, 41, -65, -148, 0, 70, 0, 66},
    {0, 0, 0, 118, -78, -86, 0, 30, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 30, 0, 0},
    {1, 17, 4032, 141, -57, -54, 0, 10, 0, 2},
    {1, 11, 4007, 102, -34, -43, 0, 10, 0, 0},
    {1, 7, 4057, 72, -44, -75, 0, 6, 4, 5},
    {1, 7, 4057, 72, -44, -75, 0, 10, 4, 5},
    {1, 7, 4003, 52, -45, -75, 0, 6, 0, 4},
    {1, 7, 4003, 52, -45, -75, 0, 6, 0, 4},
    {2, 13, 4057, 72, -44, -75, 1, 150, 4, 21},
    {1, 19, 4003, 52, -45, -75, 0, 30, 0, 4},
    {1, 14, 4024, 85, -43, -73, 0, 6, 0, 36},
    {1, 14, 4024, 85, -43, -73, 0, 6, 0, 4},
    {1, 14, 4024, 85, -43, -73, 0, 5, 0, 36},
    {1, 15, 4028, 45, -20, -42, 0, 55, 8, 5},
    {1, 16, 4003, 87, -67, -77, 0, 60, 8, 7},
    {1, 4, 4003, 94, -33, -54, 0, 6, 0, 0},
    {1, 4, 4003, 94, -33, -54, 0, 6, 0, 0},
    {1, 4, 4003, 94, -33, -54, 0, 10, 0, 0},
    {1, 4, 4030, 84, -35, -72, 0, 6, 0, 38},
    {1, 4, 4030, 84, -35, -72, 0, 6, 0, 6},
    {1, 4, 4030, 84, -35, -72, 0, 10, 0, 6},
    {1, 3, 4003, 87, -67, -77, 0, 4, 0, 0},
    {1, 3, 4003, 87, -67, -77, 0, 35, 0, 0},
    {0, 20, 4003, 87, -67, -77, 0, 5, 2, 13},
    {0, 18, 4003, 87, -67, -77, 0, 5, 0, 0},
    {10, 0, 4073, 66, -45, -33, 0, 180, 2, 1},
    {2, 27, 0, 30, -67, 0, 0, 130, 2, 13},
    {9, 2, 4020, 87, -67, -77, 0, 90, 0, 128},
    {8, 0, 4071, 73, -88, -140, 0, 5, 0, 0},
    {0, 0, 4003, 87, -67, -77, 0, 5, 0, 0},
    {1, 22, 4026, 60, -35, -45, 0, 70, 8, 5},
    {11, 12, 4037, 36, -57, -107, 0, 90, 0, 130},
    {0, 1, 4003, 87, -67, -77, 0, 30, 0, 130},
    {1, 23, 4003, 52, -45, -75, 0, 5, 4, 4},
    {1, 25, 4003, 52, -45, -75, 0, 5, 4, 4},
    {7, 10, 4039, 66, -68, -94, 0, 90, 4, 128},
    {7, 10, 4039, 66, -68, -94, 0, 90, 4, 128},
};
// n, magazine, velocity, power, falloff, lifetime, accuracy, flags  (gun table 0x4E9060, +0x04..+0x1C, +0x30)
const int kBuiltinGuns[kGunCount][8] = {
    {0, 0, 0, 0, 0, 0, 1, 0},
    {5, 0, 20, 0, 0, 100, 1, 0},
    {2, 0, 0, 0, 0, 0, 1, 0},
    {12, 30, 295, 0, 10, 100, 5, 2},
    {12, 30, 295, 0, 10, 100, 5, 130},
    {12, 30, 295, 0, 10, 100, 5, 130},
    {12, 30, 295, 0, 10, 100, 5, 130},
    {12, 30, 1000, 1, 30, 70, 1, 2},
    {12, 30, 1000, 1, 30, 70, 1, 2},
    {12, 30, 1000, 1, 30, 70, 3, 2},
    {2, 1, 0, 0, 0, 0, 1, 0},
    {3, 7, 295, 1, 10, 200, 8, 2},
    {6, 1, 20, 0, 0, 1000, 6, 16},
    {18, 1, 72, 0, 0, 1000, 1, 5},
    {3, 200, 1000, 1, 30, 70, 4, 2},
    {4, 10, 1200, 4, 50, 60, 1, 2},
    {4, 5, 0, 0, 0, 0, 1, 0},
    {3, 9, 250, 0, 10, 200, 4, 130},
    {5, 0, 0, 0, 0, 0, 1, 0},
    {3, 10, 0, 0, 0, 0, 1, 0},
    {1, 0, 0, 0, 0, 0, 1, 0},
    {3, 0, 0, 0, 0, 0, 1, 0},
    {12, 5, 950, 2, 40, 70, 1, 2},
    {12, 30, 1000, 1, 30, 70, 1, 258},
    {12, 30, 1000, 1, 30, 70, 3, 258},
    {12, 30, 1000, 1, 30, 70, 1, 258},
    {12, 30, 1000, 1, 30, 70, 3, 258},
    {2, 1, 72, 3, 1000, 500, 1, 66},
};
// Loadout rows (0x4E9610): modes per item row
const int kBuiltinRows[kRowCount][3] = {
    {0, 0, 0},
    {0, 0, 0},
    {1, 0, 0},
    {2, 0, 0},
    {3, 0, 0},
    {4, 0, 0},
    {5, 0, 0},
    {8, 9, 0},
    {6, 7, 0},
    {6, 10, 7},
    {8, 11, 9},
    {12, 13, 0},
    {15, 0, 0},
    {16, 0, 0},
    {17, 18, 19},
    {20, 21, 22},
    {23, 24, 0},
    {25, 0, 0},
    {26, 0, 0},
    {27, 0, 0},
    {28, 0, 0},
    {29, 30, 0},
    {31, 0, 0},
    {32, 0, 0},
    {37, 38, 30},
    {0, 0, 0},
    {33, 0, 0},
    {34, 0, 0},
};


// Gun sound ids +0x20..+0x2C (built-in copy).
const int kBuiltinGunSounds[kGunCount][4] = {
    {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {98, 97, 96, 95}, {94, 0, 0, 0}, {0, 0, 0, 0},
    {82, 84, 83, 85}, {81, 84, 83, 85}, {0, 0, 0, 0}, {0, 0, 0, 0}, {86, 89, 88, 87}, {0, 0, 0, 0}, {80, 113, 0, 114},
    {99, 102, 101, 100}, {90, 93, 92, 91}, {0, 0, 0, 0}, {109, 112, 111, 110}, {0, 0, 0, 0}, {0, 0, 0, 0},
    {0, 0, 0, 0}, {0, 0, 0, 0}, {103, 106, 105, 104}, {107, 0, 0, 0}, {0, 0, 0, 0}, {108, 0, 0, 0}, {0, 0, 0, 0},
    {115, 117, 0, 116},
};

// The guns a burst mode fires (0x46aefc): the first round from the burst
// gun (its sound is the whole burst), the other two from the next gun
// (silent). Ammunition comes from the mode's own gun.
int burstGun(int mode) {
    switch (mode) {
    case 6: case 8: return 8;
    case 21: return 5;
    case 35: return 23;
    case 36: return 25;
    default: return -1;
    }
}

// WEAP%i.PCX frame per mode (0x484ae8, jump table 0x484a4c); -1 none.
int iconFor(int mode) {
    switch (mode) {
    case 1: case 2: return 12;
    case 4: return 7;
    case 5: return 10;
    case 6: case 7: case 10: return 4;
    case 12: case 13: case 14: return 8;
    case 15: return 3;
    case 20: case 21: case 22: return 11;
    case 27: return 1;
    case 28: return 5;
    case 29: case 30: return 2;
    case 32: return 9;
    case 33: case 34: return 0;
    case 37: case 38: return 6;
    default: return -1;
    }
}

bool isBurst(int mode) { return mode == 6 || mode == 8 || mode == 21 || mode == 35 || mode == 36; }

} // namespace

const ModeDef& modeInfo(int mode) {
    static std::vector<ModeDef> modes = [] {
        std::vector<ModeDef> out(kModeCount);
        for (int m = 0; m < kModeCount; ++m) {
            const int* v = kBuiltinModes[m];
            ModeDef& d = out[size_t(m)];
            d.type = v[0]; d.gun = v[1]; d.item = v[2];
            d.offset[0] = v[3]; d.offset[1] = v[4]; d.offset[2] = v[5];
            d.muzzleVertex = v[6]; d.cooldown = v[7]; d.scope = v[8]; d.flags = v[9];
            d.burst = isBurst(m);
        }
        return out;
    }();
    return modes[size_t(std::clamp(mode, 0, kModeCount - 1))];
}

const GunDef& gunInfo(int gun) {
    static std::vector<GunDef> guns = [] {
        std::vector<GunDef> out(kGunCount);
        for (int g = 0; g < kGunCount; ++g) {
            const int* v = kBuiltinGuns[g];
            out[size_t(g)] = {v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]};
            out[size_t(g)].falloff = std::max(1, out[size_t(g)].falloff);
            for (int k = 0; k < 4; ++k) out[size_t(g)].sounds[k] = kBuiltinGunSounds[g][k];
        }
        return out;
    }();
    return guns[size_t(std::clamp(gun, 0, kGunCount - 1))];
}

int modeThirdPersonItem(int mode) {
    static const int items[kModeCount] = {0,    4004, 0,    0,    4031, 4006, 4056, 4056, 4002, 4002, 4056, 4002, 4023,
                                          4023, 4023, 4027, 4002, 4002, 4002, 4002, 4029, 4029, 4029, 4002, 4002, 4002,
                                          4002, 4074, 4041, 0,    4072, 4002, 4025, 4033, 4002, 4022, 4022, 0,    0};
    return mode >= 0 && mode < kModeCount ? items[mode] : 0;
}

int aiWeaponMode(int index) {
    static const int table[12] = {0, 8, 35, 36, 32, 23, 23, 20, 26, 5, 4, 0};   // 0x4eb168
    return index >= 0 && index < 12 ? table[index] : 0;
}

int loadoutRow(const std::string& name) {
    static const std::pair<const char*, int> rows[] = {{".45", 6}, {"HS22", 5}, {"M4", 9}, {"M40", 23}, {"Barrett", 12},
                                                       {"MP5", 15}, {"SAW", 11}, {"Ammo", 1}, {"LAW", 20},
                                                       {"Satchel", 21}, {"Claymore", 24}};
    for (const auto& r : rows)
        if (name == r.first) return r.second;
    return -1;
}

void Arsenal::init(const std::vector<std::string>& gameText, const ExeImage* exe, const ItemDefs* items, Loadout loadout) {
    modes_.assign(kModeCount, ModeDef{});
    guns_.assign(kGunCount, GunDef{});
    int rows[kRowCount][3];
    for (int m = 0; m < kModeCount; ++m) {
        const int* v = kBuiltinModes[m];
        ModeDef& d = modes_[size_t(m)];
        d.type = v[0]; d.gun = v[1]; d.item = v[2];
        d.offset[0] = v[3]; d.offset[1] = v[4]; d.offset[2] = v[5];
        d.muzzleVertex = v[6]; d.cooldown = v[7]; d.scope = v[8]; d.flags = v[9];
    }
    for (int g = 0; g < kGunCount; ++g) {
        const int* v = kBuiltinGuns[g];
        guns_[size_t(g)] = {v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]};
        for (int k = 0; k < 4; ++k) guns_[size_t(g)].sounds[k] = kBuiltinGunSounds[g][k];
    }
    for (int r = 0; r < kRowCount; ++r)
        for (int k = 0; k < 3; ++k) rows[r][k] = kBuiltinRows[r][k];

    // The user's DF.EXE: sanity-check (M4 single shot: item 4057, gun 7 with
    // a 30-round magazine; row 9 = modes 6, 10, 7), then read everything.
    fromExe_ = false;
    if (exe && exe->loaded()) {
        int32_t item = 0, gun = 0, size = 0, r9 = 0;
        if (exe->i32(kModeTable + 7 * kModeRecord + 0x64, item) && exe->i32(kModeTable + 7 * kModeRecord + 0x74, gun) &&
            exe->i32(kGunTable + 7 * kGunRecord + 0x08, size) && exe->i32(kRowTable + 9 * kRowRecord + 4, r9) &&
            item == 4057 && gun == 7 && size == 30 && r9 == 6) {
            bool ok = true;
            auto rd = [&](uint32_t va) { int32_t v = 0; ok = ok && exe->i32(va, v); return int(v); };
            for (int m = 0; m < kModeCount; ++m) {
                const uint32_t b = kModeTable + uint32_t(m) * kModeRecord;
                ModeDef& d = modes_[size_t(m)];
                d.type = rd(b + 0x6C); d.gun = rd(b + 0x74); d.item = rd(b + 0x64);
                for (int i = 0; i < 3; ++i) d.offset[i] = rd(b + 0x78 + 4 * uint32_t(i));
                d.muzzleVertex = rd(b + 0x90); d.cooldown = rd(b + 0x9C); d.scope = rd(b + 0xA0); d.flags = rd(b + 0xA4);
                if (d.gun < 0 || d.gun >= kGunCount) ok = false;
            }
            for (int g = 0; g < kGunCount; ++g) {
                const uint32_t b = kGunTable + uint32_t(g) * kGunRecord;
                guns_[size_t(g)] = {rd(b + 4), rd(b + 8), rd(b + 0xC), rd(b + 0x10), rd(b + 0x14), rd(b + 0x18), rd(b + 0x1C),
                                    rd(b + 0x30)};
                for (int k = 0; k < 4; ++k) guns_[size_t(g)].sounds[k] = rd(b + 0x20 + 4 * uint32_t(k));
            }
            for (int r = 0; r < kRowCount; ++r)
                for (int k = 0; k < 3; ++k) {
                    rows[r][k] = rd(kRowTable + uint32_t(r) * kRowRecord + 4 + 4 * uint32_t(k));
                    if (rows[r][k] < 0 || rows[r][k] >= kModeCount) ok = false;
                }
            fromExe_ = ok;
            if (!ok) return init(gameText, nullptr, items, loadout);
        }
    }
    for (int m = 0; m < kModeCount; ++m) {
        ModeDef& d = modes_[size_t(m)];
        if (m < int(gameText.size())) d.name = gameText[size_t(m)];
        d.icon = iconFor(m);
        d.burst = isBurst(m);
        if (items && d.item > 0)
            if (const ItemDef* it = items->byId(100000 + d.item); it && !it->graphic.empty()) {
                std::string g = it->graphic;
                if (auto dot = g.rfind('.'); dot != std::string::npos) g.resize(dot);
                d.viewModel = toUpper(g);
            }
    }
    for (auto& g : guns_) g.falloff = std::max(1, g.falloff);

    // Slots (0x44b598): 0 laser designator (mode 27), 1 knife (mode 1),
    // 2 the sidearm row's first mode, 3-5 the primary row, 6-8 the kit row,
    // 9 the frag grenade. Modes OpenDF cannot use yet stay empty.
    auto row = [&](int r, int k) { return r >= 0 && r < kRowCount ? rows[r][k] : 0; };
    slots_ = {27, 1, row(loadout.sidearm, 0), row(loadout.primary, 0), row(loadout.primary, 1), row(loadout.primary, 2),
              row(loadout.kit, 0), row(loadout.kit, 1), row(loadout.kit, 2), row(kFragRow, 0)};
    for (int& s : slots_)
        if (s < 0 || s >= kModeCount || !modes_[size_t(s)].works()) s = 0;

    kit_ = loadout.kit;
    refill();
    // Mission start: slot 3, the primary's first mode (0x449791).
    current_ = 3;
    if (!slots_[3]) cycle();
    zoomLevel_ = 0;
    cooldown_ = reloadTicks_ = burstLeft_ = 0;
    pressed_ = false;
    clock_ = flashTimer_ = 0;
    kick_[0] = kick_[1] = kick_[2] = 0;
    bullets_.clear();
    impacts_.clear();
}

void Arsenal::refill() {
    // Ammunition per gun (0x44b634): n - 1 spare, a full magazine, a round
    // chambered; the Ammo kit (row 1) gives the primary's guns 2n - 1.
    chambered_.assign(kGunCount, 0);
    mag_.assign(kGunCount, 0);
    spares_.assign(kGunCount, 0);
    for (int i = 0; i < kSlots; ++i) {
        if (!slots_[size_t(i)]) continue;
        const int g = modes_[size_t(slots_[size_t(i)])].gun;
        const GunDef& gd = guns_[size_t(g)];
        const bool doubled = kit_ == 1 && i >= 3 && i <= 5;
        spares_[size_t(g)] = doubled ? 2 * gd.n - 1 : gd.n - 1;
        mag_[size_t(g)] = gd.magazine;
        chambered_[size_t(g)] = 1;
    }
    reloadTicks_ = 0;
}

void Arsenal::select(int slot) {
    if (slot < 0 || slot >= kSlots || !slots_[size_t(slot)] || slot == current_) return;
    // 0x44aa74: the cooldown, any reload and a burst are cancelled.
    current_ = slot;
    zoomLevel_ = 0;
    reloadTicks_ = 0;
    cooldown_ = 0;
    burstLeft_ = 0;
    if (mode().type == 1) {
        // An empty magazine starts reloading. (APPROXIMATION: a round is
        // chambered from a non-empty magazine, as 0x449f58 would.)
        if (!chambered() && magRef() > 0) { chamberedRef() = 1; --magRef(); }
        if (!chambered() && magRef() == 0) startReload();
    } else if (!chambered()) {
        // Launchers and grenades load the next round after the cooldown (0x44a290 / 0x44a4c0).
        cooldown_ = mode().cooldown;
    }
}

void Arsenal::cycle() {
    for (int k = 1; k <= kSlots; ++k) {
        const int s = (current_ + k) % kSlots;
        if (slots_[size_t(s)]) { select(s); return; }
    }
}

void Arsenal::reload() {
    if (mode().type != 1) return;   // only guns have magazines (0x44a860)
    startReload();
}

void Arsenal::sound(int field, int gunIndex) {
    const GunDef& g = guns_[size_t(gunIndex >= 0 ? gunIndex : mode().gun)];
    if (g.sounds[field] > 0) sounds_.push_back({g.sounds[field], soundPos_});
}

void Arsenal::startReload() {
    // 0x44a004: needs a spare magazine and a magazine that is not full; the
    // rounds left in it are discarded; the new one is in after 0x78 ticks.
    // The reload sound (+0x28) plays as it begins (0x44a084).
    if (reloadTicks_ > 0 || sparesRef() <= 0 || magRef() == gun().magazine) return;
    sound(2);
    magRef() = 0;
    reloadTicks_ = kReloadTicks;
    zoomLevel_ = 0;
    burstLeft_ = 0;
}

void Arsenal::toggleZoom() {
    // 0x44b420: only modes with flag 0x1 (and a magnification), not with
    // the binoculars up ([0x93367c]).
    if (reloadTicks_ > 0 || !mode().zoomable() || binoculars_) return;
    zoomLevel_ = zoomLevel_ ? 0 : 1;
}

void Arsenal::fire(Player& player) {
    const ModeDef& md = mode();
    const GunDef& g = gun();
    chamberedRef() = 0;
    cooldown_ = md.cooldown;
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    std::uniform_real_distribution<float> turn(0.f, 2 * kPi);
    if (md.type == 7 || md.type == 9) {
        // Claymore (0x44ad50) / satchel (0x44ae90): placed 1 m ahead at the
        // player's heading (ground snapped by the game); the same kick as a
        // grenade; the next one after the 90-tick cooldown (0x44a548).
        const float r = turn(rng_);
        kick_[0] -= 0x1000 / 65536.f;
        kick_[1] = 0x400 / 65536.f * std::sin(r);
        kick_[2] = (0x400 * std::cos(r) - 0x2000) / 65536.f;
        const int item = md.type == 9 ? 4020 : slots_[size_t(current_)] == 37 ? 4014 : 4040;
        events_.push_back({WeaponEvent::Place, item,
                           player.feet + Vec3{std::sin(player.yaw), 0, -std::cos(player.yaw)} * 1.f, player.yaw});
        return;
    }
    if (md.type == 8) {
        // Radio detonator (0x42cef8): all the player's satchels and radio
        // claymores; no ammunition.
        chamberedRef() = 1;
        events_.push_back({WeaponEvent::Detonate});
        return;
    }
    if (md.type == 10) {
        // Laser designator (0x44a6f8): "Fire Mission request, over." with
        // 0x1AE now; the request itself when the cooldown ends (0x44a764).
        chamberedRef() = 1;
        events_.push_back({WeaponEvent::LaserPressed});
        return;
    }
    if (md.type == 6) {
        // Knife (0x44a598): the model pulls back 0x2000 and down 0x2000 with
        // a random 0x400 wobble (0x44a5f1); the strike point is 0x8000 along
        // the view from the body origin (0x42ca8c). No ammunition; ready
        // again when the cooldown ends (0x44a6c4).
        const float r = turn(rng_);
        kick_[0] -= 0x2000 / 65536.f;
        kick_[1] = 0x400 / 65536.f * std::sin(r);
        kick_[2] = (0x400 * std::cos(r) - 0x2000) / 65536.f;
        stabs_.push_back(player.origin() + player.forward() * (0x8000 / 65536.f));
        return;
    }
    if (md.type == 1) {
        // 0x449cbc: the gun model jumps 0x800 back and a random 0x400
        // sideways/up (16.16 m); it settles at 0x80 per tick (0x4498a9).
        const float r = turn(rng_);
        kick_[0] = -0x800 / 65536.f;
        kick_[1] = 0x400 / 65536.f * std::sin(r);
        kick_[2] = 0x400 / 65536.f * std::cos(r);
        flashTimer_ = kTick;   // the flash mesh shows for about one frame (0x4045f9)
    } else if (md.type == 2) {
        // 0x44a120: 0x4000 back and a random drop of up to 0x4000
        // (APPROXIMATION: the random term's range is inferred).
        kick_[0] = -0x4000 / 65536.f;
        kick_[1] = 0;
        kick_[2] = -0x4000 / 65536.f * unit(rng_);
    } else {
        // 0x44a36c: 0x1000 back, 0x400 sideways and 0x2000 down.
        const float r = turn(rng_);
        kick_[0] = -0x1000 / 65536.f;
        kick_[1] = 0x400 / 65536.f * std::sin(r);
        kick_[2] = (0x400 * std::cos(r) - 0x2000) / 65536.f;
    }

    // Spread (0x46af90): yaw and pitch each off by (A−1)·30·(0x8000 − rand)
    // binary angle, i.e. uniform ±(A−1)·0.0824°.
    float yaw = player.yaw, pitch = player.pitch;
    if (g.accuracy > 1) {
        const float maxDev = 30.f * 0x8000 / 4294967296.f * 2 * kPi * float(g.accuracy - 1);
        std::uniform_real_distribution<float> u(-1.f, 1.f);
        yaw += u(rng_) * maxDev;
        pitch += u(rng_) * maxDev;
    }
    Vec3 dir{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};

    // Origin (0x449abc / 0x44b420 / 0x44b4a0): zoomed guns fire from the
    // eye along the view (with the zoom's aim offset); otherwise from the
    // first-person model's muzzle point (vertex +0x90 of the model, placed
    // at the table offset plus the kick, aligned with the view). Launchers
    // always fire from the model (0x44a120).
    Vec3 origin;
    if (zoomLevel_ && md.type == 1) {
        origin = player.eye() + Vec3{0, -0x200 / 65536.f, 0};
    } else {
        const Vec3 fwd = player.forward();
        const Vec3 right{std::cos(player.yaw), 0, std::sin(player.yaw)};
        const Vec3 up = normalize(Vec3{right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z,
                                       right.x * fwd.y - right.y * fwd.x});
        float f = md.offset[0] / 256.f + kick_[0], l = md.offset[1] / 256.f + kick_[1], u = md.offset[2] / 256.f + kick_[2];
        // Model axes: x along the barrel, y up, z right.
        if (const auto* pts = modelPoints ? modelPoints(md.viewModel) : nullptr;
            pts && md.muzzleVertex >= 0 && md.muzzleVertex < int(pts->size())) {
            const auto& q = (*pts)[size_t(md.muzzleVertex)];
            f += q[0];
            l -= q[2];
            u += q[1];
            if (std::getenv("OPENDF_DEBUGHITS"))
                std::fprintf(stderr, "muzzle %s vertex %d: forward %.3f left %.3f up %.3f\n", md.viewModel.c_str(), md.muzzleVertex,
                             f, l, u);
        }
        origin = player.eye() + fwd * f - right * l + up * u;
    }
    // Delta Force 2 (DF2.EXE 0x414ad0): a muzzle under the water fires only
    // the knife (1) and the underwater pistol (0x27) and rifle (0x28/0x29);
    // anything else just plays its gun +0x2C sound (INFERRED: the dry-fire
    // click; the round is still used, also INFERRED).
    if (underwaterRule && waterLevel != 0 && origin.y < waterLevel) {
        const int m = slots_[size_t(current_)];
        if (m != 1 && m != 0x27 && m != 0x28 && m != 0x29) {
            if (g.sounds[3] > 0) sounds_.push_back({g.sounds[3], player.origin(), false});
            return;
        }
    }
    // Speed: velocity · 65536 / 62 per tick (0x44d450).
    Projectile p;
    p.pos = origin;
    p.vel = dir * (float(g.velocity) * 60.f / 62.f);
    p.lifetime = g.lifetime;
    p.power = g.power;
    p.falloff = g.falloff;
    p.flags = g.flags;
    p.id = ++projectileIds_;
    p.from = player.origin();
    p.orient();
    bullets_.push_back(p);
    // The shot (gun +0x20, 0x44d5fe) at the muzzle. A burst sounds once:
    // the first round carries the burst gun's sound, the others none.
    if (const int bg = burstGun(slots_[size_t(current_)]); bg >= 0) {
        if (!followUp_) {
            const int id = guns_[size_t(bg)].sounds[0];
            if (id > 0) sounds_.push_back({id, origin, true});
        }
    } else if (g.sounds[0] > 0) {
        sounds_.push_back({g.sounds[0], origin, true});
    }
    // A launcher loads its next round at once, with the load sound (0x44a290).
    if (md.type == 2) sound(1);
}

void Arsenal::update(float dt, bool triggerDown, bool triggerPressed, Player& player, World& world,
                     std::vector<std::string>& messages) {
    if (triggerPressed) pressed_ = true;
    soundPos_ = player.origin();
    flashTimer_ = std::max(0.f, flashTimer_ - dt);
    for (float& k : kick_) {
        const float step = 0x80 / 65536.f * 60.f * dt;
        k = k > 0 ? std::max(0.f, k - step) : std::min(0.f, k + step);
    }
    for (clock_ += dt; clock_ >= kTick; clock_ -= kTick) tick(triggerDown, player, messages);
    moveProjectiles(dt, world);
}

// One 60 Hz tick of the weapon (0x449830 and the fire command 0x44a920).
void Arsenal::tick(bool triggerDown, Player& player, std::vector<std::string>& messages) {
    const ModeDef& md = mode();
    if (reloadTicks_ > 0 && --reloadTicks_ == 0) {
        // 0x44a0a4: one spare magazine used, the magazine full.
        --sparesRef();
        magRef() = gun().magazine;
        messages.push_back("Ammo reloaded");   // DF.EXE's own message
        sounds_.push_back({-0x164, {0, 0, 0}});   // PU_AMMO, 2D (0x42b587); negative id = 2D
        // APPROXIMATION (inferred): an empty chamber is loaded from it.
        if (!chambered() && magRef() > 0) { chamberedRef() = 1; --magRef(); }
    }
    bool shot = false;
    if (cooldown_ > 0 && --cooldown_ == 0) {
        if (md.type == 1) {
            // 0x449f58: the next round from the magazine; an empty magazine
            // starts the reload.
            if (magRef() > 0) { chamberedRef() = 1; --magRef(); sound(1); }   // chamber sound +0x24 (0x449f48)
            if (magRef() == 0) startReload();
            // A burst goes on by itself (0x44988e); full auto refires while
            // the trigger is held, but not with the binoculars up ([0x93367c], 0x449ff1).
            if (chambered() && reloadTicks_ == 0) {
                if (burstLeft_ > 0) { --burstLeft_; followUp_ = true; fire(player); followUp_ = false; shot = true; }
                else if (md.automatic() && triggerDown && !binoculars_) { fire(player); shot = true; }
            }
        } else if (md.type == 10) {
            chamberedRef() = 1;
            events_.push_back({WeaponEvent::LaserRequest});   // 0x44a764
        } else if (md.type == 8) {
            chamberedRef() = 1;
        } else if (md.type == 6) {
            chamberedRef() = 1;   // 0x44a6c4: the knife is ready again
        } else if (sparesRef() > 0 && !chambered()) {
            // 0x44a318: the next grenade / launcher round.
            chamberedRef() = 1;
            --sparesRef();
        }
    }
    // A press fires a chambered round once the cooldown is over; a press
    // during the cooldown is lost (0x44a920).
    if (pressed_) {
        pressed_ = false;
        // The binoculars block the trigger (0x44a947).
        if (!shot && cooldown_ <= 0 && reloadTicks_ == 0 && !binoculars_) {
            if (md.type == 8 || md.type == 10) {
                fire(player);   // the detonator and the designator need no round
            } else if (!chambered() && md.type != 1 && md.type != 6) {
                sound(3);                       // launcher / grenade dry fire (0x44a221, 0x44a3e3)
            } else if (chambered()) {
                fire(player);
                if (md.burst) burstLeft_ = 2;   // 0x46af90: three rounds in all
            } else if (md.type == 1) {
                burstLeft_ = 0;                 // dry fire (0x449e1d), sound +0x2C (0x449e03)
                sound(3);
                startReload();
            }
        }
    }
}

void Arsenal::moveProjectiles(float dt, World& world) {
    // Projectiles (0x44f3e4), per 60 Hz tick: trace the segment pos..pos+v,
    // the nearest hit stops it (no penetration); otherwise move, gravity
    // 0xb2 per tick² (not the LAW), age one tick; gone after its lifetime in
    // ticks or below -1 m.
    for (auto& b : bullets_) {
        for (b.tickClock += dt; b.tickClock >= kTick && b.age < b.lifetime; b.tickClock -= kTick) {
            const Vec3 next = b.pos + b.vel * kTick;
            RayHit hit;
            bool hitSomething = world.raycast(b.pos, next, hit, true, b.shooter, true, true);
            // Water (0x44f0e4, tested after terrain and objects; the nearest
            // wins): a start under or within 0x200 of the surface hits at
            // t = 0, otherwise where the segment crosses the level. The round
            // ends there (0x44fc20): nothing travels under water.
            if (const float w = world.terrain->waterLevel(); w != 0) {
                float t = 2;
                if (b.pos.y <= w + 0x200 / 65536.f) t = 0;
                else if (next.y < w) t = (b.pos.y - w) / (b.pos.y - next.y);
                if (t <= 1 && (!hitSomething || t < hit.t)) {
                    hit = RayHit{};
                    hit.t = t;
                    hit.object = -2;
                    hit.point = b.pos + (next - b.pos) * t;
                    hitSomething = true;
                }
            }
            if (hitSomething) {
                const Vec3 dir = normalize(b.vel);
                Impact im{hit.point - dir * 0.25f, hit.object, hit.part};
                // Explosive rounds: the M203's arms after 8 ticks of flight
                // (flags 0x11); grenades and the LAW at once.
                im.explosive = b.explosive();
                im.armed = !(b.flags & 0x01) || b.age > 8;
                im.law = b.flags & 0x40;
                im.flags = b.flags;
                im.material = hit.material;
                impacts_.push_back(im);
                if (std::getenv("OPENDF_DEBUGHITS")) {
                    std::fprintf(stderr, "hit obj %d part %d mesh %d material %08x at (%.2f %.2f %.2f) age %d\n", hit.object, hit.part,
                                 hit.mesh, hit.material, hit.point.x, hit.point.y, hit.point.z, b.age);
                    if (hit.object >= 0) {
                        const auto& o = world.objects[size_t(hit.object)];
                        std::fprintf(stderr, "  origin (%.2f %.2f %.2f) heading %.1f deg hip %.2f\n", o.pos.x, o.pos.y, o.pos.z,
                                     o.heading * 57.2958f, o.hip);
                        for (size_t k = 0; k < o.spheres.size(); ++k)
                            std::fprintf(stderr, "  part %zu c (%.3f %.3f %.3f) r %.3f\n", k, o.spheres[k][0], o.spheres[k][1],
                                         o.spheres[k][2], o.spheres[k][3]);
                    }
                }
                // Direct hit (0x44f184): 1 << (power >> (age / falloff)), x4
                // on the head (part 2); explosives too, before their blast.
                if (hit.object == -3) {
                    // The player (0x44f184 / 0x44f2b2): armour class 0.
                    playerHits_.push_back({(1 << (b.power >> (b.age / b.falloff))) * (hit.part == 2 ? 4 : 1), hit.part, b.from});
                } else if (hit.object >= 0 && (b.flags & 0x100) && world.objects[size_t(hit.object)].item &&
                           (world.objects[size_t(hit.object)].item->attrib & 0x80)) {
                    // 0x44f184: rounds with flag 0x100 do nothing to `noak` items.
                } else if (hit.object >= 0) {
                    WorldObject& target = world.objects[size_t(hit.object)];
                    const int before = target.hp;
                    world.damage(hit.object, (1 << (b.power >> (b.age / b.falloff))) * (hit.part == 2 ? 4 : 1), b.power,
                                 world.deathMove(hit.object, 1, std::max(0, hit.part), b.from), b.shooter);
                    // Helicopters (0x47cc36): a hit on the tail rotor (mesh 3/4) or
                    // mesh 6 gives 1 hp back. (DF.EXE also counts tail hits toward a
                    // tail-rotor failure, 0x47d0bc, not in OpenDF.)
                    if (target.model && target.model->lodCodes[0] == "helo" && (hit.mesh == 3 || hit.mesh == 4 || hit.mesh == 6) &&
                        target.hp < before && !target.dead) {
                        ++target.hp;
                        if (hit.mesh != 6) ++target.tailHits;   // +0xD4 (mesh 6: +0xD8, never read)
                    }
                }
                b.age = b.lifetime;
                break;
            }
            // Whiz-by (0x44f330): the point of this step nearest the listener,
            // if it lies inside the step and the round is within 40 m of the
            // listener on both horizontal axes.
            {
                const Vec3 d = next - b.pos;
                const float dd = dot(d, d);
                const float t = dd > 0 ? dot(listener_ - b.pos, d) / dd : -1;
                if (t > 0 && t < 1 && std::abs(b.pos.x - listener_.x) < 40.f && std::abs(b.pos.z - listener_.z) < 40.f)
                    sounds_.push_back({0x41, b.pos + d * t});
            }
            b.pos = next;
            if (!(b.flags & 0x40)) b.vel.y -= 0xb2 * 3600.f / 65536.f * kTick;
            ++b.age;
            if (b.pos.y < -1.f) b.age = b.lifetime;
        }
    }
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Projectile& p) { return p.age >= p.lifetime; }),
                   bullets_.end());
}

} // namespace df
