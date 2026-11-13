#include "Weapons.h"

#include <algorithm>
#include <cmath>

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
    //  - per gun: magazine size (+0x08), magazines n (+0x04; the player gets
    //    2n-1 spares, as footage of the original shows: 23 for the M4) and
    //    muzzle velocity in m/s (+0x0C).
    // Icons: WEAP640.PCX frames. Spread, damage and recoil are approximations.
    constexpr float reload = kReloadTicks * kTick;
    static const std::vector<WeaponDef> defs = {
        {{{"SpecOps .45 caliber pistol", F::Single, 5, 10}},
         "PM19", 10, {102, -34, -43}, 7, 2 * 3 - 1, 295, 1.4f, {}, 1, reload, 1.6f, true},
        {{{"M4 Assault Rifle, Single Shot", F::Single, 7, 10}, {"M4 Assault Rifle, Burst", F::Burst, 6, 6}},
         "PM4", 4, {72, -44, -75}, 30, 2 * 12 - 1, 1000, 0.5f, {4.f}, 1, reload, 0.6f},
        {{{"M40 Sniper Rifle", F::Single, 32, 70}},
         "PM40", 9, {60, -35, -45}, 5, 2 * 12 - 1, 950, 0.08f, {8.f}, 2, reload, 2.5f},
        {{{"H&K MP5 SD, Single Shot", F::Single, 22, 10}, {"H&K MP5 SD, Burst", F::Burst, 21, 6},
          {"H&K MP5 SD, Full Auto", F::Auto, 20, 6}},
         "PMP5", 11, {84, -35, -72}, 30, 2 * 12 - 1, 295, 0.7f, {}, 1, reload, 0.35f},
        {{{"SAW Auto. Weap., Full Auto", F::Auto, 12, 6}},
         "PM249", 8, {85, -43, -73}, 200, 2 * 3 - 1, 1000, 1.0f, {}, 1, reload, 0.5f},
    };
    return defs;
}

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
        d.spareMags = std::max(0, 2 * n - 1);
        d.muzzleVelocity = float(velocity);
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

const std::string& Arsenal::modeName() const { return names_[current_][slots_[current_].mode]; }

void Arsenal::init(const std::vector<std::string>& gameText, const ExeImage* exe, const ItemDefs* items) {
    defs_ = builtinWeaponDefs();
    fromExe_ = exe && exe->loaded() && readExeTables(*exe, items, defs_);
    names_.clear();
    for (const auto& d : defs_) {
        std::vector<std::string> n;
        for (const auto& m : d.modes)
            n.push_back(m.table < int(gameText.size()) && !gameText[m.table].empty() ? gameText[m.table] : m.name);
        names_.push_back(std::move(n));
    }
    slots_.clear();
    for (int i = 0; i < int(defs_.size()); ++i) slots_.push_back({i, defs_[i].magazine, defs_[i].spareMags, 0});
    current_ = 1;   // start with the carbine
    zoomLevel_ = 0;
    cooldown_ = reloadTimer_ = recoil_ = flashTimer_ = 0;
    burstLeft_ = 0;
    bullets_.clear();
    impacts_.clear();
}

void Arsenal::select(int slot, int mode) {
    if (slot < 0 || slot >= int(slots_.size())) return;
    if (slot != current_) {
        current_ = slot;
        zoomLevel_ = 0;
        reloadTimer_ = 0;
        cooldown_ = 0.4f;   // bring the weapon up
        burstLeft_ = 0;
    }
    if (mode >= 0) slots_[current_].mode = std::min(mode, int(def().modes.size()) - 1);
}

void Arsenal::cycle() { select((current_ + 1) % int(slots_.size())); }

void Arsenal::cycleMode() {
    Slot& s = slots_[current_];
    s.mode = (s.mode + 1) % int(def().modes.size());
}

void Arsenal::reload() {
    Slot& s = slots_[current_];
    if (reloadTimer_ > 0 || s.mags <= 0 || s.rounds == def().magazine) return;
    reloadTimer_ = def().reloadSeconds;
    zoomLevel_ = 0;
}

void Arsenal::toggleZoom() {
    if (reloadTimer_ > 0) return;
    if (def().zoom.empty()) return;   // no scope on this weapon
    zoomLevel_ = (zoomLevel_ + 1) % (int(def().zoom.size()) + 1);
}

void Arsenal::fire(Player& player, World& world) {
    Slot& s = slots_[current_];
    const WeaponDef& d = def();
    --s.rounds;
    cooldown_ = mode().fireTicks * kTick;
    flashTimer_ = 0.05f;
    recoil_ += d.recoilDeg;

    // Accuracy: steadier crouched/prone and when still; scopes help a lot.
    float spread = d.spreadDeg;
    if (player.stance == Stance::Crouch) spread *= 0.6f;
    if (player.stance == Stance::Prone) spread *= 0.35f;
    if (player.moving()) spread *= 2.0f;
    if (zoomLevel_) spread *= 0.5f;
    std::normal_distribution<float> n(0.f, spread * kPi / 180.f * 0.5f);
    float yaw = player.yaw + n(rng_), pitch = player.pitch + recoilPitch() + n(rng_);
    Vec3 dir{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
    Vec3 origin = player.eye() + dir * 0.3f;
    bullets_.push_back({origin, dir * d.muzzleVelocity, 3.f, d.damage});
}

float Arsenal::recoilPitch() const {
    // The muzzle climbs while firing and settles back on its own.
    return std::min(recoil_, 6.f) * 0.5f * kPi / 180.f;
}

void Arsenal::update(float dt, bool triggerDown, bool triggerPressed, Player& player, World& world,
                     std::vector<std::string>& messages) {
    Slot& s = slots_[current_];
    const WeaponDef& d = def();
    cooldown_ = std::max(0.f, cooldown_ - dt);
    flashTimer_ = std::max(0.f, flashTimer_ - dt);
    recoil_ = std::max(0.f, recoil_ - dt * (6.f + recoil_ * 2.f));

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
        const FireMode fm = mode().fire;
        if (fm == FireMode::Burst && triggerPressed && burstLeft_ == 0 && cooldown_ <= 0) burstLeft_ = 3;
        const bool wantsFire = fm == FireMode::Auto ? triggerDown : fm == FireMode::Burst ? burstLeft_ > 0 : triggerPressed;
        if (wantsFire && cooldown_ <= 0) {
            if (s.rounds > 0) {
                fire(player, world);
                if (burstLeft_ > 0) --burstLeft_;   // a burst: three rounds, fireTicks apart
            } else {
                burstLeft_ = 0;
                if (triggerPressed && s.mags > 0) reload();
            }
        }
    }

    // Bullets: integrate with gravity; test each step's segment for hits.
    for (auto& b : bullets_) {
        float remaining = dt;
        while (remaining > 0 && b.life > 0) {
            float h = std::min(remaining, 1.f / 120.f);
            Vec3 next = b.pos + b.vel * h + Vec3{0, -4.905f * h * h, 0};
            RayHit hit;
            if (world.raycast(b.pos, next, hit)) {
                impacts_.push_back({hit.point, 0, hit.object >= 0});
                if (hit.object >= 0) world.damage(hit.object, b.damage);
                b.life = 0;
                break;
            }
            b.vel.y -= 9.81f * h;
            b.pos = next;
            b.life -= h;
            remaining -= h;
        }
    }
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Projectile& p) { return p.life <= 0; }),
                   bullets_.end());
    for (auto& i : impacts_) i.age += dt;
    impacts_.erase(std::remove_if(impacts_.begin(), impacts_.end(), [](const Impact& i) { return i.age > 1.5f; }),
                   impacts_.end());
}

} // namespace df
