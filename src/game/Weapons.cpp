#include "Weapons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../core/Pff.h"

namespace df {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kModeCount = 39, kGunCount = 28, kRowCount = 28;
// DF.EXE tables (CD 0x4E97D0 modes, 0x4E9060 guns, 0x4E9610 rows; the
// build's addresses come from ExeImage::df1()); see docs/FORMATS.md.
constexpr uint32_t kModeRecord = 168, kGunRecord = 52;
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
    case 6: case 8: case 51: return 8;   // 51: DF2's MasterKey burst
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
    if (const Df1ExeTables* t = exe ? exe->df1() : nullptr) {
        const uint32_t kModeTable = t->modes, kGunTable = t->guns, kRowTable = t->rows, kRowRecord = t->rowRecord;
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

bool Arsenal::initDf2(const std::vector<std::string>& hudGuns, const ExeImage& exe, const ItemDefs* items, Df2Loadout lo) {
    const Df2ExeTables* t = exe.df2();
    if (!t) return false;
    // CD 0x4C7238 modes, 0x4C5150 types, 0x4C4310 ammo, 0x4C86B8 curve; the
    // build's from ExeImage::df2(). GOG Df2.exe's ammunition records are 100
    // bytes with a word inserted at +0x0C (`at`) and the flags 16 bits up.
    const uint32_t kModes = t->modes, kTypes = t->types, kAmmo = t->ammo, kCurve = t->curve;
    constexpr int kModeN = 57, kTypeN = 39, kCurveN = 44;
    const int kAmmoN = t->ammoCount;
    auto at = [&](uint32_t cdOffset) { return cdOffset >= 0x0C ? cdOffset + t->ammoShift : cdOffset; };
    bool ok = true;
    auto rd = [&](uint32_t va) { int32_t v = 0; ok = ok && exe.i32(va, v); return v; };
    std::vector<ModeDef> modes(kModeN);
    std::vector<GunDef> guns(kAmmoN);
    for (int m = 0; m < kModeN; ++m) {
        const uint32_t b = kModes + uint32_t(m) * 92;
        ModeDef& d = modes[size_t(m)];
        d.type = rd(b + 0x0C);   // kind: DF1's fire types (dispatch 0x446c34)
        d.gun = rd(b + 0x14);    // ammo type: ammunition is kept per ammo type
        d.item = rd(b + 0x04);   // first-person model item
        d.heldItem = rd(b + 0x08);   // third-person item (0x414f40 via [0x4ee328 + 0x8c·m])
        for (int k = 0; k < 3; ++k) d.offset[k] = rd(b + 0x18 + 4 * uint32_t(k));
        d.muzzleVertex = rd(b + 0x30);
        d.cooldown = rd(b + 0x34);
        d.scope = rd(b + 0x38);
        const int f = rd(b + 0x3C);
        // DF2 flags: 0x1 scope, 0x40 full auto, 0x4 pistols (one-handed);
        // DF1's bits here: 0x1 zoom, 0x20 full auto, 0x4 two-handed.
        d.flags = (f & 1) | ((f & 0x40) ? 0x20 : 0) | ((f & 4) ? 0 : 4);
        if (d.gun < 0 || d.gun >= kAmmoN) ok = false;
        if (m < int(hudGuns.size())) d.name = hudGuns[size_t(m)];
        {
            // The weapon type's scope elevations (+0x98 + 4r, r 0..15).
            const int type = rd(b + 0x10);
            if (type > 0 && type < kTypeN)
                for (int r = 0; r < 16; ++r) d.elevation.push_back(rd(kTypes + uint32_t(type) * 216 + 0x98 + 4 * uint32_t(r)));
        }
        d.icon = -1;
        // Bursts (0x414ad0): 6, 8 and 51 (shot 8 then 9), 21, 35, 36.
        d.burst = m == 6 || m == 8 || m == 51 || m == 21 || m == 35 || m == 36;
        d.binoculars = (f & 0x200) != 0;   // tested by the move chooser 0x43f690 (0x43f76a / 0x43f7b3)
        if (items && d.item > 0)
            if (const ItemDef* it = items->byId(100000 + d.item); it && !it->graphic.empty()) {
                std::string g = it->graphic;
                if (auto dot = g.rfind('.'); dot != std::string::npos) g.resize(dot);
                d.viewModel = toUpper(g);
            }
    }
    for (int a = 0; a < kAmmoN; ++a) {
        const uint32_t b = kAmmo + uint32_t(a) * t->ammoRecord;
        GunDef& g = guns[size_t(a)];
        g.n = rd(b + 0x04);
        g.magazine = rd(b + 0x08);
        if (t->ammoShift) g.extraClips = rd(b + 0x0C);   // GOG Df2.exe 0x44dfc0 (Extra Ammo)
        g.velocity = rd(b + at(0x0C));
        g.power = rd(b + at(0x10));
        g.falloff = std::max(1, rd(b + at(0x14)));
        g.lifetime = rd(b + at(0x18));
        g.accuracy = rd(b + at(0x1C));
        for (int k = 0; k < 4; ++k) g.sounds[k] = rd(b + at(0x20 + 4 * uint32_t(k)));
        // In the CD's bits (GOG Df2.exe 0x451ae0 tests them 16 bits higher).
        g.flags = int(uint32_t(rd(b + at(0x30))) >> t->ammoFlagShift);
        // 0x46b230: thresholds / 62 (C truncation), coefficients 2^32 / c.
        for (int k = 0; k < 5; ++k) {
            g.dragThr[k] = int64_t(rd(b + at(0x34 + 8 * uint32_t(k)))) / 62;
            const int64_t c = rd(b + at(0x38 + 8 * uint32_t(k)));
            g.dragCoef[k] = c ? (int64_t(1) << 32) / c : 0;
        }
        // +0x58 (GOG +0x5C) is both the fifth band's coefficient and the one under water.
        g.waterCoef = g.dragCoef[4];
        const int64_t grains = int64_t(rd(b + at(0x5C))) << 16;
        const int64_t kg = (grains << 16) / 0x3c480000;
        g.mass = (kg << 16) / 0xa7;
    }
    std::vector<DragPoint> curve(kCurveN);
    for (int i = 0; i < kCurveN; ++i) {
        const uint32_t b = kCurve + uint32_t(i) * 12;
        curve[size_t(i)] = {(int64_t(rd(b)) << 16) / 62, int64_t(rd(b + 4)) / 62, 0};
    }
    for (int i = 0; i + 1 < kCurveN; ++i)
        if (curve[size_t(i + 1)].x != curve[size_t(i)].x)
            curve[size_t(i)].slope = ((curve[size_t(i + 1)].y - curve[size_t(i)].y) << 16) / (curve[size_t(i + 1)].x - curve[size_t(i)].x);
    // Sanity: the M4's ammo (7) is 30 rounds at 915 m/s.
    if (!ok || guns[7].magazine != 30 || guns[7].velocity != 915) return false;

    // Slots (0x447e20): 0 laser designator (type 19), 1 knife (type 2),
    // 2 sidearm, 3-5 the primary's modes, 6 secondary, 7 body gear,
    // 8 the radio detonator (type 21 mode 1), 9 the grenade.
    auto typeMode = [&](int t, int k) { return t >= 0 && t < kTypeN ? rd(kTypes + uint32_t(t) * 216 + 0x84 + 4 * uint32_t(k)) : 0; };
    modes_ = std::move(modes);
    guns_ = std::move(guns);
    dragCurve_ = std::move(curve);
    slots_ = {typeMode(19, 0), typeMode(2, 0), typeMode(lo.sidearm, 0), typeMode(lo.primary, 0), typeMode(lo.primary, 1),
              typeMode(lo.primary, 2), typeMode(lo.secondary, 0), typeMode(lo.gear, 0), typeMode(21, 1), typeMode(lo.grenade, 0)};
    for (int& s : slots_)
        if (s < 0 || s >= int(modes_.size()) || !modes_[size_t(s)].works()) s = 0;
    // Slot 10: T[35] mode 0 (47, the binoculars, kind 12: fires nothing).
    if (const int b = typeMode(35, 0); b > 0 && b < int(modes_.size())) slots_[kBinocularSlot] = b;
    prevSlot_ = 3;
    gear_ = lo.gear;
    df2_ = true;
    df2Gog_ = t->ammoShift != 0;   // the GOG build (its 100-byte ammo rows)
    df2Lo_ = lo;
    fromExe_ = true;
    // Extra Ammo (secondary type 1, [0x765628]) doubles the primary's clips
    // (CD 0x4472d0); GOG's refill (0x44dfc0) is in refill().
    kit_ = lo.secondary == 1 ? 1 : 0;
    refill();
    current_ = 3;
    if (!slots_[3]) cycle();
    zoomLevel_ = 0;
    cooldown_ = reloadTicks_ = burstLeft_ = 0;
    pressed_ = false;
    clock_ = flashTimer_ = 0;
    kick_[0] = kick_[1] = kick_[2] = 0;
    bullets_.clear();
    impacts_.clear();
    return true;
}

void Arsenal::setWind(float kph, float degrees) {
    // 0x46bbf0: v = trunc(trunc(trunc(kph·65536000/60)/60)/65) per tick,
    // blowing from `degrees`: x (east) = -v sin, north = -v cos.
    const int64_t v = int64_t(int64_t(int64_t(int64_t(kph) * 65536000 / 60) / 60) / 65);
    const double a = double(degrees) * 3.14159265358979 / 180.0;
    windX_ = int64_t(-double(v) * std::sin(a));
    windZ_ = int64_t(double(v) * std::cos(a));   // OpenDF z = -north
}

int Arsenal::df2Damage(const Projectile& b) const {
    if (b.gun < 0 || b.gun >= int(guns_.size())) return 1;
    const int64_t vx = int64_t(b.vel.x * kTick * 65536.f), vy = int64_t(b.vel.y * kTick * 65536.f),
                  vz = int64_t(b.vel.z * kTick * 65536.f);
    const int64_t v2 = (vx * vx + vy * vy + vz * vz) >> 16;
    const int64_t e = (v2 * guns_[size_t(b.gun)].mass) >> 16;
    return e <= 0x190000 ? 1 : int(e / 0x190000);
}

void Arsenal::df2Drag(Projectile& b, float water) const {
    if (b.gun < 0 || b.gun >= int(guns_.size()) || dragCurve_.empty()) return;
    const GunDef& g = guns_[size_t(b.gun)];
    int64_t v[3] = {int64_t(b.vel.x * kTick * 65536.f), int64_t(b.vel.y * kTick * 65536.f), int64_t(b.vel.z * kTick * 65536.f)};
    const int64_t s = int64_t(std::sqrt(double(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])));
    const bool under = water != 0 && b.pos.y < water;
    int64_t c = 0;
    if (under) c = g.waterCoef;
    else if (s < g.dragThr[0]) c = g.dragCoef[0];
    else if (s < g.dragThr[1]) c = g.dragCoef[1];
    else if (s < g.dragThr[2]) c = g.dragCoef[2];
    else if (s < g.dragThr[3]) c = g.dragCoef[3];
    const DragPoint& e = dragCurve_[size_t(std::min<int64_t>(s >> 15, int64_t(dragCurve_.size()) - 1))];
    const int64_t f = e.y + (((s - e.x) * e.slope) >> 16);
    int64_t k = (f * c) >> 16;
    if (under) k = (k * (0x10000 + (int64_t((water - b.pos.y) * 65536.f) << 16) / 0xa0000)) >> 16;
    k = std::clamp<int64_t>(k, 0, 0x8000);
    const int64_t wind[3] = {windX_, 0, windZ_};
    for (int i = 0; i < 3; ++i) v[i] -= ((v[i] - wind[i]) * k) >> 16;
    b.vel = Vec3{float(v[0]), float(v[1]), float(v[2])} * (1.f / (65536.f * kTick));
}

void Arsenal::refill() {
    // Ammunition per gun (0x44b634): n - 1 spare, a full magazine, a round
    // chambered; the Ammo kit (row 1) gives the primary's guns 2n - 1.
    chambered_.assign(guns_.size(), 0);
    mag_.assign(guns_.size(), 0);
    spares_.assign(guns_.size(), 0);
    if (df2_ && df2Gog_) {
        refillDf2Gog();
        return;
    }
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

void Arsenal::refillDf2Gog() {
    // GOG Df2.exe 0x44dfc0: per weapon's ammo n - 1 spare clips, a full
    // magazine, a round chambered, for slots 2-7 and 9 and the two mounted
    // guns' modes 55 and 56 (slots 0, 1 and 8 use ammo 0, which OpenDF
    // fills as the CD does). Slot 7 holding slot 6's mode (the same item as
    // secondary and gear) adds n clips to it. Extra Ammo as the secondary
    // (+0xb7) and again as the gear (+0xb8) each add the ammo's +0x0C clips
    // to slot 3's weapon, and to slot 4's when the primary is type 9 or 10.
    auto fill = [&](int mode) {
        if (mode <= 0 || mode >= int(modes_.size())) return;
        const int g = modes_[size_t(mode)].gun;
        if (g < 0 || g >= int(guns_.size())) return;
        spares_[size_t(g)] = guns_[size_t(g)].n - 1;
        mag_[size_t(g)] = guns_[size_t(g)].magazine;
        chambered_[size_t(g)] = 1;
    };
    auto gunOf = [&](int slot) {
        const int m = slots_[size_t(slot)];
        return m > 0 && m < int(modes_.size()) ? modes_[size_t(m)].gun : -1;
    };
    for (int i : {0, 1, 8}) fill(slots_[size_t(i)]);
    for (int i : {2, 3, 4, 5, 6}) fill(slots_[size_t(i)]);
    if (slots_[7] && slots_[7] == slots_[6]) {
        if (const int g = gunOf(7); g >= 0) spares_[size_t(g)] += guns_[size_t(g)].n;
    } else {
        fill(slots_[7]);
    }
    fill(slots_[9]);
    fill(55);
    fill(56);
    for (int kit : {df2Lo_.secondary, df2Lo_.gear}) {
        if (kit != 1) continue;
        if (const int g = gunOf(3); g >= 0) spares_[size_t(g)] += guns_[size_t(g)].extraClips;
        if (df2Lo_.primary == 9 || df2Lo_.primary == 10)
            if (const int g = gunOf(4); g >= 0) spares_[size_t(g)] += guns_[size_t(g)].extraClips;
    }
    reloadTicks_ = 0;
}

void Arsenal::select(int slot) {
    if (slot < 0 || slot >= int(slots_.size())) return;   // 10: DF2's binoculars (0x41b6e3)
    // DF.EXE 0x44aa74 / DF2.EXE 0x4480b0 (the key handlers 0x42de85 /
    // 0x41bced call it for any slot 0-9): no test for an empty slot or for
    // the slot already held, so the same key again cancels a running
    // cooldown or reload too, and an empty slot holds no weapon (mode 0).
    current_ = slot;
    // The scope flag (DF.EXE [0x93353c], DF2.EXE [0x7655f4]) is only
    // toggled (0x44b498 / 0x447b30, their only writers) and cleared with the
    // mission's weapon block (0x449753 memset 0x933340 / 0x4477bf
    // 0x7653a0): select and reload leave it; it counts for flag-0x1 modes
    // only (zoomed(), 0x44b420 / 0x447b40).
    reloadTicks_ = 0;   // 0x44ab1d / 0x448129
    cooldown_ = 0;      // 0x44ab14 / 0x44811f
    burstLeft_ = 0;     // the burst counter (player AI block +0x6c / +0x7c, 0x44abc2 / 0x4481b8)
    // By type (tables 0x44aa48 / 0x4481c4 + 0x4481d4, the same in both
    // games): a gun with an empty magazine starts the reload (0x44a004 /
    // 0x448020; spares and a full magazine are checked there); its chamber
    // is not looked at, so a gun put away mid-cooldown comes back
    // unchambered. Launchers, claymores, satchels and grenades (2, 7, 9, 11)
    // with an empty chamber and spares: the cooldown with the load sound
    // +0x24 (0x44a290 / 0x44a4c0; DF2 0x447f40(0)). Others: nothing.
    const int type = mode().type;
    if (type == 1) { if (magRef() == 0) startReload(); }
    else if ((type == 2 || type == 7 || type == 9 || type == 11) && !chambered() && sparesRef() > 0) {
        cooldown_ = mode().cooldown;
        sound(1);
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
    // DF2.EXE 0x448020 is the same (clips, rounds = 0, 0x78 ticks, +0x28).
    // Neither touches the scope flag or the burst counter (0x44a004..0x44a0a0
    // writes only [0x933350] and the gun's rounds; the reload key 0x42e7e2
    // -> 0x44a88c jumps straight here for type 1, table 0x44a860).
    if (reloadTicks_ > 0 || sparesRef() <= 0 || magRef() == gun().magazine) return;
    sound(2);
    magRef() = 0;
    reloadTicks_ = kReloadTicks;
}

void Arsenal::toggleZoom() {
    // DF.EXE 0x42e45c -> 0x44b498 / DF2.EXE 0x41b35a -> 0x447b30: the flag
    // is toggled whatever is held, reloading or not; zoomed() decides when
    // it shows.
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
    } else if (md.type == 2) {
        // 0x44a184: 0x4000 back, added to the kick still settling; the
        // +-0x400 circle and 0x4000 down (DF2.EXE the same).
        const float r = turn(rng_);
        kick_[0] -= 0x4000 / 65536.f;
        kick_[1] = 0x400 / 65536.f * std::sin(r);
        kick_[2] = (0x400 * std::cos(r) - 0x4000) / 65536.f;
    } else {
        // 0x44a36c: 0x1000 back (subtracted, 0x44a49b), 0x400 sideways and 0x2000 down.
        const float r = turn(rng_);
        kick_[0] -= 0x1000 / 65536.f;
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
    if (zoomed() && md.type == 1) {
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
    // anything else plays its gun +0x2C sound (the same field 0x447070 plays
    // as the dry click) at the shooter and fires nothing; the caller still
    // uses the round, kicks and starts the cooldown (0x44718a..0x44725c).
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
    p.gun = md.gun;
    p.orient();
    if (df2_ && (g.flags & 0x400)) {
        // DF2's shotgun (shot 0x13, 0x44aa10): 12 pellets, heading and pitch
        // each uniform within ±0x4000000 (±5.625°) of the aim. (DF2 seeds its
        // generator with the heading; OpenDF's own generator here.)
        std::uniform_real_distribution<float> u(-1.f, 1.f);
        const float half = 5.625f * kPi / 180.f;
        for (int i = 0; i < 12; ++i) {
            Projectile q = p;
            const float y = player.yaw + u(rng_) * half, pt = player.pitch + u(rng_) * half;
            q.vel = Vec3{std::cos(pt) * std::sin(y), std::sin(pt), -std::cos(pt) * std::cos(y)} * (float(g.velocity) * 60.f / 62.f);
            q.id = ++projectileIds_;
            q.orient();
            bullets_.push_back(q);
        }
    } else {
        bullets_.push_back(p);
    }
    // The Barrett (mode 15) pushes the shooter back 0x800 (1/32 m) along
    // -heading (0x447070).
    if (df2_ && slots_[size_t(current_)] == 15) {
        player.feet.x -= std::sin(player.yaw) * (0x800 / 65536.f);
        player.feet.z += std::cos(player.yaw) * (0x800 / 65536.f);
    }
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
    // Then the flash meshes (DF.EXE 0x44d635, DF2.EXE 0x44a9be): ammo +0x30
    // bit 0x2 mesh 1, bit 0x4 mesh 2 (the M203's grenades: 0x5).
    if (const int fm = ((g.flags & 0x2) ? 1 : 0) | ((g.flags & 0x4) ? 2 : 0)) {
        flashMeshes_ = fm;
        flashTimer_ = kTick;   // shows for about one frame
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

// One 60 Hz tick of the weapon: DF.EXE 0x449830 / DF2.EXE 0x446050, the
// same order in both: the cooldown and its end (0x44996d / 0x44611d), then
// the reload and its end (0x44997f / 0x44613b), then, while the burst counter
// is set and no cooldown runs, the fire command (0x449991 -> 0x44a920 /
// 0x446155 -> 0x447270); the trigger press is the same command.
void Arsenal::tick(bool triggerDown, Player& player, std::vector<std::string>& messages) {
    (void)messages;   // neither reload end puts up a message ("Ammo reloaded" is the crate, 0x42b56e)
    const ModeDef& md = mode();
    bool shot = false;
    if (cooldown_ > 0 && --cooldown_ == 0) {
        if (md.type == 1) {
            // DF.EXE 0x449f58 / DF2.EXE 0x446410, silent in both: a round from
            // the magazine into the chamber; the magazine now (or already)
            // empty starts the reload (0x44a004 / 0x448020); otherwise full
            // auto (DF1 flag 0x20, DF2 0x40) fires again while attack_1 (0x8c)
            // is held (DF1 not with the binoculars up, [0x93367c] 0x449ff1).
            if (magRef() > 0) {
                chamberedRef() = 1;
                --magRef();
                if (magRef() == 0) startReload();
                else if (md.automatic() && triggerDown && !binoculars_) { fire(player); shot = true; }
            } else {
                startReload();
            }
        } else if (md.type == 10) {
            chamberedRef() = 1;
            events_.push_back({WeaponEvent::LaserRequest});   // 0x44a764 / DF2 0x446300
        } else if (md.type == 8) {
            chamberedRef() = 1;
        } else if (md.type == 6) {
            chamberedRef() = 1;   // 0x44a6c4 / DF2 0x4462d0: the knife is ready again
        } else if (sparesRef() > 0 && !chambered()) {
            // 0x44a318 / 0x44a548 (DF2 0x4463c0): the next grenade, launcher
            // round or charge from the spares (DF2's "clips").
            chamberedRef() = 1;
            --sparesRef();
        }
    }
    if (reloadTicks_ > 0 && --reloadTicks_ == 0) {
        // DF.EXE 0x44a0a4 / DF2.EXE 0x447950 (guns; reloads start only for
        // them): one spare magazine used, the magazine full; an empty chamber
        // -> 0x449ed0(0) / 0x447f40(0): unless a cooldown is running, the
        // cooldown starts with the chamber sound +0x24, and the round goes in
        // when it ends. No message, no other sound.
        if (sparesRef() > 0) {
            --sparesRef();
            magRef() = gun().magazine;
            if (!chambered() && cooldown_ <= 0) { cooldown_ = mode().cooldown; sound(1); }
        }
    }
    // The fire command (DF.EXE 0x44a920, DF2.EXE 0x447270): no reload test
    // in either game (a chambered round fires during a reload); DF.EXE also
    // checks the binoculars (0x44a947; DF2's are a mode of kind 12, which fires
    // nothing). A press during the cooldown is lost.
    auto command = [&](bool followUp) {
        if (shot || cooldown_ > 0 || binoculars_) return;
        if (md.type == 8 || md.type == 10) {
            fire(player);   // the detonator and the designator need no round
        } else if (md.type == 1 || md.type == 2 || md.type == 11) {
            if (chambered()) {
                if (followUp) { --burstLeft_; followUp_ = true; fire(player); followUp_ = false; }
                else { fire(player); if (md.burst) burstLeft_ = 2; }   // 0x46af90 / 0x414ad0: three rounds in all
            } else {
                // Dry fire: the click +0x2C at the player and the burst
                // counter cleared (guns 0x449dd8 / 0x4470ae, launchers
                // 0x44a221 / 0x446f4d, grenades 0x44a3e3 / 0x446c9d); a gun
                // also starts a silent cooldown (0x449ed0(1) / 0x447f40(1))
                // whose end chambers a round or starts the reload.
                burstLeft_ = 0;
                sound(3);
                if (md.type == 1) cooldown_ = md.cooldown;
            }
        } else if (md.type == 6) {
            // Knife: DF1's strike has no chamber test, only the cooldown
            // (GOG Df.exe 0x483b10 case 6; CD 0x44a7c8), so a switch during
            // the cooldown (select clears it) cannot leave it dead. DF2 keeps
            // the chamber test (0x44691b) until DF2.EXE's knife is checked.
            if (!df2_ || chambered()) fire(player);
        } else if (md.type == 7 || md.type == 9) {
            // Claymore, satchel: nothing without a chambered one, no click
            // (0x44aecb; DF2 0x446a5a).
            if (chambered()) fire(player);
        }
        shot = true;
    };
    if (burstLeft_ > 0 && cooldown_ <= 0) command(true);
    if (pressed_) {
        pressed_ = false;
        command(burstLeft_ > 0);
    }
    // OPENDF_DEBUGWEAPON: the weapon state whenever a sound is queued or a
    // counter starts or ends.
    static const bool debug = std::getenv("OPENDF_DEBUGWEAPON") != nullptr;
    if (debug) {
        static int last[8] = {-1};
        const int now[8] = {current_, modeIndex(), magRef(), sparesRef(), chambered() ? 1 : 0, cooldown_ > 0, reloadTicks_ > 0, burstLeft_};
        static size_t lastSounds = 0;
        if (std::memcmp(now, last, sizeof now) != 0 || sounds_.size() != lastSounds) {
            std::fprintf(stderr, "weapon t%llu slot %d mode %d mag %d spares %d chambered %d cooldown %d reload %d burst %d sounds",
                         (unsigned long long)debugTick_, now[0], now[1], now[2], now[3], now[4], cooldown_, reloadTicks_, now[7]);
            for (size_t i = lastSounds < sounds_.size() ? lastSounds : 0; i < sounds_.size(); ++i) std::fprintf(stderr, " %d", sounds_[i].id);
            std::fprintf(stderr, "\n");
            std::memcpy(last, now, sizeof now);
        }
        lastSounds = sounds_.size();
    }
    ++debugTick_;
}

void Arsenal::moveProjectiles(float dt, World& world) {
    // Projectiles (0x44f3e4), per 60 Hz tick: trace the segment pos..pos+v,
    // the nearest hit stops it (no penetration); otherwise move, gravity
    // 0xb2 per tick² (not the LAW), age one tick; gone after its lifetime in
    // ticks or below -1 m.
    // The death move's direction: DF.EXE from the shooter; DF2.EXE 0x44c063
    // from the round's reversed velocity.
    auto hitFrom = [&](const Projectile& p, const RayHit& h) { return df2_ ? h.point - p.vel : p.from; };
    for (auto& b : bullets_) {
        int passes = 0;   // DF2: up to 20 hits in a tick (penetration)
        for (b.tickClock += dt; b.tickClock >= kTick && b.age < b.lifetime; b.tickClock -= kTick) {
            const Vec3 next = b.pos + b.vel * kTick;
            RayHit hit;
            bool hitSomething = world.raycast(b.pos, next, hit, true, b.shooter, true, true);
            // DF2 0x44c391: a round under the water slower than 0x4000 a tick
            // (grenades excepted) is spent after this tick's trace.
            bool spent = false;
            if (const float w = world.terrain->waterLevel(); df2_ && w != 0 && b.pos.y < w && !(b.flags & 0x1000))
                spent = std::sqrt(dot(b.vel, b.vel)) * kTick < 0x4000 / 65536.f;
            Vec3 stopAt{};
            bool stopped = false;   // DF2: the tick ends at the water
            // Water (0x44f0e4, tested after terrain and objects; the nearest
            // wins): a start under or within 0x200 of the surface hits at
            // t = 0, otherwise where the segment crosses the level. The round
            // ends there (0x44fc20): nothing travels under water.
            if (const float w = world.terrain->waterLevel(); w != 0 && df2_) {
                // Delta Force 2 (0x44bd40, hit type 4 of 0x44c2f0, the nearest
                // wins): crossing down at (z0 - w)/|uz|; rising, at the negative
                // distance -(w - z0)/uz, which always wins; under water, no
                // test. Handler 0x44cee5: the point 0x4000 back along the path;
                // explosives (0x851) blast (type 1, effect 4) or, an M203 within
                // 8 ticks, splash; other rounds splash (effect 2, sound 0x3B)
                // and the tick ends 0x6000 further on (0.125 m past the surface),
                // velocity kept (a delay grenade's zeroed). So rounds go on
                // under the water and never come back out.
                const Vec3 seg = next - b.pos;
                const float len = std::sqrt(dot(seg, seg));
                const bool down = b.pos.y > w && next.y < w, up = b.pos.y < w && next.y > w;
                if (len > 0 && (down || up)) {
                    const Vec3 u = seg * (1.f / len);
                    const float d = down ? (b.pos.y - w) / std::abs(u.y) : -(w - b.pos.y) / u.y;
                    if (up || !hitSomething || d < hit.t * len) {
                        const Vec3 at = b.pos + u * (d - 0x4000 / 65536.f);
                        if (b.flags & 0x851) {
                            Impact im{at, -2, -1};
                            im.explosive = true;
                            im.armed = !(b.age <= 8 && !(b.flags & 0x50));
                            im.flags = b.flags;
                            impacts_.push_back(im);
                            b.age = b.lifetime;
                            break;
                        }
                        Impact im{at, -2, -1};
                        im.flags = b.flags;
                        impacts_.push_back(im);
                        stopAt = at + u * (0x6000 / 65536.f);
                        stopped = true;
                        hitSomething = false;
                        if (b.flags & 0x1000) b.vel = {0, 0, 0};
                    }
                }
            } else if (const float w = world.terrain->waterLevel(); w != 0) {
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
            // DF2's delay grenade (flag 0x1000, 0x44ae50): it never goes off on
            // impact; on an object's face it slides off, v -= n·(v·n)·1.07 then
            // × 0.85; on anything else it stops there.
            if (hitSomething && df2_ && (b.flags & 0x1000) && hit.object != -2) {
                const Vec3 dir = normalize(b.vel);
                b.pos = hit.point - dir * (0x4000 / 65536.f);
                if (hit.object >= 0 && !world.objects[size_t(hit.object)].person) {
                    const float d = dot(b.vel, hit.normal);
                    b.vel = (b.vel - hit.normal * (d * float(0x111EB) / 65536.f)) * (float(0xD999) / 65536.f);
                } else {
                    b.vel = {0, 0, 0};
                }
                ++b.age;
                if (b.lifetime - b.age <= 10) {
                    Impact im{b.pos, -1, -1};
                    im.explosive = true;
                    im.flags = b.flags;
                    impacts_.push_back(im);
                    b.age = b.lifetime;
                }
                continue;
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
                // Delta Force 2 (0x44bff0): people take the round's energy,
                // max(1, E / 25), x8 on the head (part 2).
                if (df2_ && (hit.object == -3 || (hit.object >= 0 && world.objects[size_t(hit.object)].person))) {
                    int dmg = df2Damage(b);
                    // Kevlar (0x44cd1f..0x44cd54 -> 0x44bdd0): on any person
                    // whose AI block +0xB8 is 31 (Armor), parts 0 and 1, the
                    // round first spends 75·(n + 1) of its energy (n: the
                    // person's second hp value, item +0x98); nothing left, no
                    // damage. Only player slots ever set +0xB8 (the loadout
                    // copy 0x45429d, network players 0x408ee2 / 0x40a089 /
                    // 0x40a623); a placed soldier's block is zeroed by
                    // 0x404080 and the BMS loader 0x403590 never writes it, so
                    // in single player only the player can wear Kevlar.
                    if (hit.object == -3 && gear_ == 31 && hit.part >= 0 && hit.part <= 1) {
                        const int64_t vx = int64_t(b.vel.x * kTick * 65536.f), vy = int64_t(b.vel.y * kTick * 65536.f),
                                      vz = int64_t(b.vel.z * kTick * 65536.f);
                        const int64_t e = (((vx * vx + vy * vy + vz * vz) >> 16) * guns_[size_t(std::max(0, b.gun))].mass) >> 16;
                        const int64_t left = e - int64_t(0x4b0000) * (playerArmourClass + 1);
                        dmg = left <= 0 ? 0 : left <= 0x190000 ? 1 : int(left / 0x190000);
                    }
                    dmg *= hit.part == 2 ? 8 : 1;
                    if (hit.object == -3) { if (dmg > 0) playerHits_.push_back({dmg, hit.part, b.from}); }
                    else
                        world.damage(hit.object, dmg, 0x7fff, world.deathMove(hit.object, 1, std::max(0, hit.part), hitFrom(b, hit)), b.shooter);
                } else if (df2_ && hit.object >= 0) {
                    // Objects (0x44bff0): an item whose second hp value is below
                    // 0 stops the round unharmed; buildings take nothing;
                    // vehicles only rockets (flag 0x40: 64) and shells and
                    // impact grenades (0x11: 16); the rest max(1, E/25).
                    const WorldObject& target = world.objects[size_t(hit.object)];
                    const ItemDef* it = target.item;
                    int dmg = 0;
                    if (it && it->damageClass >= 0 && it->type != "building") {
                        if (it->type == "vehicle") dmg = (b.flags & 0x40) ? 64 : (b.flags & 0x11) ? 16 : 0;
                        else dmg = df2Damage(b);
                    }
                    if (dmg > 0) world.damage(hit.object, dmg, 0x7fff, world.deathMove(hit.object, 1, std::max(0, hit.part), hitFrom(b, hit)), b.shooter);
                    // Penetration (0x44c2f0 -> 0x44bdd0; GOG Df2.exe 0x453860
                    // case 1/2 -> 0x4532f0): a regular round (not flags 0x1851:
                    // GOG 0x8400000 / 0x110000 / 0x10000000) on an object with
                    // an item; the face's material costs 100 (Flesh item,
                    // checked first), metal 200, wood 90, stone 500, bush 10,
                    // armour 2000, canvas 5, 0x10000000 10 while the object is
                    // intact. T = cost · (hp2 + 1); with E = |v|²·M above T the
                    // round goes on, its speed × sqrt((E−T)/E); otherwise it
                    // stops. A face with none of these (wire fences: WIREFENC /
                    // WIREGATE 'Afencing', FENCEHI 'Abrbwr', 0x8003), or 0x10000000
                    // on a husk, costs nothing (GOG 0x4542ee: effect 5 only) and
                    // the round goes on at full speed. Every face makes a 5 m
                    // noise (0x414660 0x50000; 20 m, 0x140000, for an intact
                    // 0x10000000 face). The round goes on from 0.25 m before
                    // the hit + 0.375 m; up to 20 hits a tick.
                    if (it && (b.flags & 0x1851) == 0) {
                        int cost = 0;
                        const uint32_t m = hit.material;
                        bool glass = false;
                        if (it->attrib & 0x40) cost = 100;
                        else if (m & 0x10000) cost = 200;
                        else if (m & 0x20000) cost = 90;
                        else if (m & 0x40000) cost = 500;
                        else if (m & 0x80000) cost = 10;
                        else if (m & 0x04000000) cost = 2000;
                        else if (m & 0x08000000) cost = 5;
                        else if ((m & 0x10000000) && !target.wrecked) cost = 10, glass = true;
                        impacts_.back().noise = glass ? 20.f : 5.f;
                        bool goesOn = cost == 0;
                        if (cost > 0) {
                            const int64_t vx = int64_t(b.vel.x * kTick * 65536.f), vy = int64_t(b.vel.y * kTick * 65536.f),
                                          vz = int64_t(b.vel.z * kTick * 65536.f);
                            const int64_t e = (((vx * vx + vy * vy + vz * vz) >> 16) * guns_[size_t(std::max(0, b.gun))].mass) >> 16;
                            const int64_t t = int64_t(cost) * 65536 * (it->damageClass + 1);
                            if (e > t && e > 0) {
                                b.vel = b.vel * float(std::sqrt(double(e - t) / double(e)));
                                goesOn = true;
                            }
                        }
                        if (goesOn && passes < 20) {
                            b.pos = hit.point + normalize(b.vel) * (0x2000 / 65536.f);
                            ++passes;
                            b.tickClock += kTick;   // the same tick goes on from here
                            continue;
                        }
                    }
                } else
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
                                 world.deathMove(hit.object, 1, std::max(0, hit.part), hitFrom(b, hit)), b.shooter);
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
            b.pos = stopped ? stopAt : next;
            if (df2_) {
                // DF2.EXE 0x44c2f0: gravity 0xa7 a tick² (not the LAW; 0x14
                // for a delay grenade under the water), then drag toward the
                // wind (the water's coefficient below the surface).
                const float w = world.terrain->waterLevel();
                const bool under = w != 0 && b.pos.y <= w;
                if (!(b.flags & 0x40)) b.vel.y -= ((b.flags & 0x1000) && under ? 0x14 : 0xa7) * 60.f / 65536.f * 60.f * kTick;
                df2Drag(b, w);
                if (spent) b.age = b.lifetime;
            } else if (!(b.flags & 0x40)) b.vel.y -= 0xb2 * 3600.f / 65536.f * kTick;
            ++b.age;
            // The delay grenade explodes (type 1) once its lifetime is 10 or less.
            if (df2_ && (b.flags & 0x1000) && b.lifetime - b.age <= 10 && b.age < b.lifetime) {
                Impact im{b.pos, -1, -1};
                im.explosive = true;
                im.flags = b.flags;
                impacts_.push_back(im);
                b.age = b.lifetime;
            }
            if (b.pos.y < -1.f) b.age = b.lifetime;
        }
    }
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Projectile& p) { return p.age >= p.lifetime; }),
                   bullets_.end());
}

} // namespace df
