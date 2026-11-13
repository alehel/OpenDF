#include "Weapons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../core/Pff.h"

namespace df {

namespace {
constexpr float kPi = 3.14159265f;
}

// DF.EXE (US release) tables; see docs/FORMATS.md.
constexpr uint32_t kPlayerTable = 0x4E97D0, kPlayerRecord = 168;   // per fire mode
constexpr uint32_t kGunTable = 0x4E9060, kGunRecord = 52;           // per gun
// Reload time: a fixed 0x78 ticks in DF.EXE.
constexpr int kReloadTicks = 120;

const std::vector<WeaponDef>& builtinWeaponDefs() {
    using F = FireMode;
    // Copies of DF.EXE's values, used when DF.EXE cannot be read:
    //  - per mode: table index, fire cooldown in ticks (+0x9C);
    //  - first-person model (+0x64), gun offset (+0x78), scope power (+0xA0);
    //  - per gun: magazine size (+0x08), magazines n (+0x04; spares n-1,
    //    2n-1 for the primary with the "Ammo" kit) and muzzle velocity in
    //    m/s (+0x0C).
    // Icons: WEAP640.PCX frames. Spread, damage and recoil are approximations.
    constexpr float reload = kReloadTicks * kTick;
    static const std::vector<WeaponDef> defs = {
        {{{"SpecOps .45 caliber pistol", F::Single, 5, 10}},
         "PM19", 10, {102, -34, -43}, 7, 3, 295, 1.4f, {}, 1, reload, 1.6f, true},
        {{{"M4 Assault Rifle, Single Shot", F::Single, 7, 10}, {"M4 Assault Rifle, Burst", F::Burst, 6, 6}},
         "PM4", 4, {72, -44, -75}, 30, 12, 1000, 0.5f, {4.f}, 1, reload, 0.6f},
        {{{"M40 Sniper Rifle", F::Single, 32, 70}},
         "PM40", 9, {60, -35, -45}, 5, 12, 950, 0.08f, {8.f}, 2, reload, 2.5f},
        {{{"H&K MP5 SD, Single Shot", F::Single, 22, 10}, {"H&K MP5 SD, Burst", F::Burst, 21, 6},
          {"H&K MP5 SD, Full Auto", F::Auto, 20, 6}},
         "PMP5", 11, {84, -35, -72}, 30, 12, 295, 0.7f, {}, 1, reload, 0.35f},
        {{{"SAW Auto. Weap., Full Auto", F::Auto, 12, 6}},
         "PM249", 8, {85, -43, -73}, 200, 3, 1000, 1.0f, {}, 1, reload, 0.5f},
    };
    return defs;
}

namespace {

// Built-in copies of the gun-table projectile fields, per weapon above.
void builtinProjectiles(std::vector<WeaponDef>& defs) {
    const int v[5][4] = {{1, 10, 200, 8}, {1, 30, 70, 1}, {2, 40, 70, 1}, {0, 10, 100, 5}, {1, 30, 70, 4}};
    for (size_t i = 0; i < defs.size() && i < 5; ++i) {
        defs[i].power = v[i][0];
        defs[i].falloff = v[i][1];
        defs[i].lifetime = v[i][2];
        defs[i].accuracy = v[i][3];
    }
}

} // namespace

namespace {

// Overrides `defs` with the values in the user's DF.EXE. Returns false (and
// leaves `defs` alone) if the tables are not where the US release has them.
bool readExeTables(const ExeImage& exe, const ItemDefs* items, std::vector<WeaponDef>& defs) {
    auto mode = [&](int m, uint32_t field, int32_t& v) { return exe.i32(kPlayerTable + m * kPlayerRecord + field, v); };
    auto gun = [&](int g, uint32_t field, int32_t& v) { return exe.i32(kGunTable + g * kGunRecord + field, v); };
    // Sanity check: M4 single shot uses item 4057 and a 30-round magazine.
    int32_t item = 0, gunIndex = 0, size = 0;
    if (!mode(7, 0x64, item) || !mode(7, 0x74, gunIndex) || item != 4057 || gunIndex != 7) return false;
    if (!gun(7, 0x08, size) || size != 30) return false;

    std::vector<WeaponDef> out = defs;
    for (auto& d : out) {
        const int first = d.modes[0].table;
        int32_t g = 0, n = 0, velocity = 0, scope = 0, fp = 0;
        if (!mode(first, 0x74, g) || g < 0 || g >= 28) return false;
        for (int i = 0; i < 3; ++i)
            if (!mode(first, 0x78 + 4 * i, d.gunOffset[i])) return false;
        for (auto& m : d.modes)
            if (!mode(m.table, 0x9C, m.fireTicks) || m.fireTicks <= 0) return false;
        if (!mode(first, 0xA0, scope)) return false;
        d.zoom = scope > 1 ? std::vector<float>{float(scope)} : std::vector<float>{};
        if (!gun(g, 0x04, n) || !gun(g, 0x08, d.magazine) || !gun(g, 0x0C, velocity)) return false;
        d.magazines = std::max(1, int(n));
        d.muzzleVelocity = float(velocity);
        if (!gun(g, 0x10, d.power) || !gun(g, 0x14, d.falloff) || !gun(g, 0x18, d.lifetime) || !gun(g, 0x1C, d.accuracy))
            return false;
        d.falloff = std::max(1, d.falloff);
        if (items && mode(first, 0x64, fp))
            if (const ItemDef* it = items->byId(fp); it && !it->graphic.empty()) {
                std::string name = it->graphic;
                if (auto dot = name.rfind('.'); dot != std::string::npos) name.resize(dot);
                d.viewModel = toUpper(name);
            }
    }
    defs = std::move(out);
    return true;
}

} // namespace

int weaponIndex(const std::string& name) {
    const char* names[] = {".45", "M4", "M40", "MP5", "SAW"};
    for (int i = 0; i < 5; ++i)
        if (name == names[i]) return i;
    return -1;
}

const std::string& Arsenal::modeName() const { return names_[slots_[current_].def][slots_[current_].mode]; }

void Arsenal::init(const std::vector<std::string>& gameText, const ExeImage* exe, const ItemDefs* items, int primary,
                   int secondary) {
    defs_ = builtinWeaponDefs();
    builtinProjectiles(defs_);
    fromExe_ = exe && exe->loaded() && readExeTables(*exe, items, defs_);
    names_.clear();
    for (const auto& d : defs_) {
        std::vector<std::string> n;
        for (const auto& m : d.modes)
            n.push_back(m.table < int(gameText.size()) && !gameText[m.table].empty() ? gameText[m.table] : m.name);
        names_.push_back(std::move(n));
    }
    slots_.clear();
    const int n = int(defs_.size());
    const bool ammoKit = secondary < 0;
    auto add = [&](int d, bool doubled) {
        if (d < 0 || d >= n) return;
        const int m = defs_[size_t(d)].magazines;
        slots_.push_back({d, defs_[size_t(d)].magazine, doubled ? 2 * m - 1 : m - 1, 0});
    };
    add(0, false);                              // sidearm: the .45 (the HS .22 is not implemented)
    add(primary >= 0 ? primary : 1, ammoKit);   // primary
    add(secondary, false);                      // secondary gun, if not the Ammo kit
    current_ = 1;   // the primary weapon (the original starts with the knife, not implemented yet)
    zoomLevel_ = 0;
    cooldown_ = reloadTimer_ = flashTimer_ = 0;
    kick_[0] = kick_[1] = kick_[2] = 0;
    burstLeft_ = 0;
    bullets_.clear();
    impacts_.clear();
}

void Arsenal::select(int slot, int mode) {
    if (slot < 0 || slot >= int(slots_.size())) return;
    if (mode >= int(defs_[size_t(slots_[size_t(slot)].def)].modes.size())) return;
    // DF.EXE 0x44aa74: switching clears the fire cooldown and any reload in
    // progress (no bring-up delay); an empty weapon starts reloading.
    if (slot != current_) {
        current_ = slot;
        zoomLevel_ = 0;
        reloadTimer_ = 0;
        cooldown_ = 0;
        burstLeft_ = 0;
        if (slots_[current_].rounds == 0) reload();
    }
    if (mode >= 0) slots_[current_].mode = std::min(mode, int(def().modes.size()) - 1);
}

void Arsenal::cycle() { select((current_ + 1) % int(slots_.size())); }

void Arsenal::cycleMode() {
    Slot& s = slots_[current_];
    s.mode = (s.mode + 1) % int(def().modes.size());
}

void Arsenal::reload() {
    // DF.EXE 0x44a004: the rounds left in the magazine are discarded; the
    // new one is in after 0x78 ticks.
    Slot& s = slots_[current_];
    if (reloadTimer_ > 0 || s.mags <= 0 || s.rounds == def().magazine) return;
    s.rounds = 0;
    reloadTimer_ = def().reloadSeconds;
    zoomLevel_ = 0;
}

void Arsenal::toggleZoom() {
    if (reloadTimer_ > 0) return;
    if (def().zoom.empty()) return;   // no scope on this weapon
    zoomLevel_ = (zoomLevel_ + 1) % (int(def().zoom.size()) + 1);
    if (zoomLevel_) {
        // 0x44b870: a fixed random aim offset for this zoom, sin/cos of a
        // random angle times 8 table units (up to 2.8125°).
        std::uniform_real_distribution<float> a(0.f, 2 * kPi);
        const float r = a(rng_), m = 2.8125f * kPi / 180.f;
        scopeYaw_ = std::sin(r) * m;
        scopePitch_ = std::cos(r) * m;
    }
}

void Arsenal::fire(Player& player, World& world) {
    (void)world;
    Slot& s = slots_[current_];
    const WeaponDef& d = def();
    --s.rounds;
    cooldown_ = mode().fireTicks * kTick;
    flashTimer_ = kTick;   // the flash mesh shows for about one frame (DF.EXE 0x4045f9)
    // DF.EXE 0x449cbc (fire class 1): the gun model jumps 0x800 back and a
    // random 0x400 sideways/up (16.16 m); it settles at 0x80 per tick (0x4498a9).
    {
        std::uniform_real_distribution<float> a(0.f, 2 * kPi);
        const float r = a(rng_);
        kick_[0] = -0x800 / 65536.f;
        kick_[1] = 0x400 / 65536.f * std::sin(r);
        kick_[2] = 0x400 / 65536.f * std::cos(r);
    }

    // Spread (0x46af90): yaw and pitch each off by up to ±0.0824°·(A−1),
    // uniform; nothing else (stance, movement, scope) changes it.
    float yaw = player.yaw, pitch = player.pitch;
    if (d.accuracy > 1) {
        const float maxDev = float(0xF0000) / 4294967296.f * 2 * kPi * float(d.accuracy - 1);
        std::uniform_real_distribution<float> u(-1.f, 1.f);
        yaw += u(rng_) * maxDev;
        pitch += u(rng_) * maxDev;
    }
    Vec3 dir{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};

    // Origin (0x449abc / 0x44b420): scoped, from the eye along the view
    // (with the scope's offset); otherwise from the gun model's position.
    Vec3 origin;
    if (zoomLevel_) {
        const float sy = yaw + scopeYaw_, sp = pitch + scopePitch_;
        dir = {std::cos(sp) * std::sin(sy), std::sin(sp), -std::cos(sp) * std::cos(sy)};
        origin = player.eye() + Vec3{0, -0x200 / 65536.f, 0};
    } else {
        const Vec3 fwd = player.forward();
        const Vec3 right = normalize(Vec3{-std::cos(player.yaw), 0, -std::sin(player.yaw)}) * -1.f;
        const Vec3 up = normalize(Vec3{fwd.y * right.z - fwd.z * right.y, fwd.z * right.x - fwd.x * right.z,
                                       fwd.x * right.y - fwd.y * right.x}) * -1.f;
        const float f = (d.gunOffset[0] / 256.f) + kick_[0], l = (d.gunOffset[1] / 256.f) + kick_[1],
                    u = (d.gunOffset[2] / 256.f) + kick_[2];
        origin = player.eye() + fwd * f - right * l + up * u;
    }
    // Speed: velocity · 65536 / 62 per tick (the divisor is 62).
    Projectile p;
    p.pos = origin;
    p.vel = dir * (d.muzzleVelocity * 60.f / 62.f);
    p.lifetime = d.lifetime;
    p.power = d.power;
    p.falloff = d.falloff;
    bullets_.push_back(p);
}

void Arsenal::update(float dt, bool triggerDown, bool triggerPressed, Player& player, World& world,
                     std::vector<std::string>& messages) {
    Slot& s = slots_[current_];
    const WeaponDef& d = def();
    cooldown_ = std::max(0.f, cooldown_ - dt);
    flashTimer_ = std::max(0.f, flashTimer_ - dt);
    for (float& k : kick_) {
        const float step = 0x80 / 65536.f * 60.f * dt;
        k = k > 0 ? std::max(0.f, k - step) : std::min(0.f, k + step);
    }

    if (reloadTimer_ > 0) {
        reloadTimer_ -= dt;
        if (reloadTimer_ <= 0) {
            reloadTimer_ = 0;
            --s.mags;
            s.rounds = d.magazine;
            messages.push_back("Ammo reloaded");   // DF.EXE's own message
        }
    } else {
        // Burst fires three rounds per press; full auto fires while held.
        // Full auto (table +0xA4 bit 0x20) refires while held, but not while zoomed (0x449fc3).
        const FireMode fm = mode().fire == FireMode::Auto && zoomLevel_ ? FireMode::Single : mode().fire;
        if (fm == FireMode::Burst && triggerPressed && burstLeft_ == 0 && cooldown_ <= 0) burstLeft_ = 3;
        const bool wantsFire = fm == FireMode::Auto ? triggerDown : fm == FireMode::Burst ? burstLeft_ > 0 : triggerPressed;
        if (wantsFire && cooldown_ <= 0) {
            if (s.rounds > 0) {
                fire(player, world);
                if (burstLeft_ > 0) --burstLeft_;   // a burst: three rounds, fireTicks apart
                // DF.EXE 0x449f58: the last round starts the reload at once.
                if (s.rounds == 0) { burstLeft_ = 0; reload(); }
            } else {
                burstLeft_ = 0;
                if (triggerPressed && s.mags > 0) reload();
            }
        }
    }

    // Bullets (0x44f3e4), per 60 Hz tick: trace the segment pos..pos+v, the
    // nearest hit stops the bullet (no penetration); otherwise move, gravity
    // 0xb2 per tick², age one tick; gone after `lifetime` ticks.
    for (auto& b : bullets_) {
        for (b.tickClock += dt; b.tickClock >= kTick && b.age < b.lifetime; b.tickClock -= kTick) {
            const Vec3 next = b.pos + b.vel * kTick;
            RayHit hit;
            if (world.raycast(b.pos, next, hit)) {
                {
                    const Vec3 dir = normalize(b.vel);
                    impacts_.push_back({hit.point - dir * 0.25f, hit.object, hit.part});
                }
                // Damage (0x44f184): 1 << (power >> (age / falloff)).
                // ×4 on the head (part 2) (0x44f184).
                if (std::getenv("OPENDF_DEBUGHITS"))
                {
                    std::fprintf(stderr, "hit obj %d part %d at (%.2f %.2f %.2f) age %d\n", hit.object, hit.part, hit.point.x,
                                 hit.point.y, hit.point.z, b.age);
                    if (hit.object >= 0) {
                        const auto& o = world.objects[size_t(hit.object)];
                        std::fprintf(stderr, "  origin (%.2f %.2f %.2f) heading %.1f deg hip %.2f\n", o.pos.x, o.pos.y, o.pos.z,
                                     o.heading * 57.2958f, o.hip);
                        for (size_t k = 0; k < o.spheres.size(); ++k)
                            std::fprintf(stderr, "  part %zu c (%.3f %.3f %.3f) r %.3f\n", k, o.spheres[k][0], o.spheres[k][1],
                                         o.spheres[k][2], o.spheres[k][3]);
                    }
                }
                if (hit.object >= 0)
                    world.damage(hit.object, (1 << (b.power >> (b.age / b.falloff))) * (hit.part == 2 ? 4 : 1), b.power);
                b.age = b.lifetime;
                break;
            }
            b.pos = next;
            b.vel.y -= 0xb2 * 3600.f / 65536.f * kTick;
            ++b.age;
        }
    }
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Projectile& p) { return p.age >= p.lifetime; }),
                   bullets_.end());
}

} // namespace df
