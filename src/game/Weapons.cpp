#include "Weapons.h"

#include <algorithm>
#include <cmath>

namespace df {

namespace {
constexpr float kPi = 3.14159265f;
}

const std::vector<WeaponDef>& weaponDefs() {
    using F = FireMode;
    // Names: DFGAME.BIN (index = DF.EXE weapon table index). Icons: WEAP640.PCX.
    // From DF.EXE's weapon tables (see docs/FORMATS.md):
    //  - gun offset (player table +0x78) and scope power (+0xA0);
    //  - magazine size (gun table +0x08), magazines (+0x04; the player gets
    //    2n-1 spares, which is what footage of the original shows, e.g. 23
    //    for the M4) and muzzle velocity in m/s (+0x0C).
    // Still approximations: fire rate, spread, damage, reload time, recoil.
    static const std::vector<WeaponDef> defs = {
        {{{"SpecOps .45 caliber pistol", F::Single, 5}},
         "PM19", 10, {102, -34, -43}, 7, 2 * 3 - 1, 300, 295, 1.4f, {}, 1, 1.6f, 1.6f},
        {{{"M4 Assault Rifle, Single Shot", F::Single, 7}, {"M4 Assault Rifle, Burst", F::Burst, 6}},
         "PM4", 4, {72, -44, -75}, 30, 2 * 12 - 1, 800, 1000, 0.5f, {4.f}, 1, 2.2f, 0.6f},
        {{{"M40 Sniper Rifle", F::Single, 32}},
         "PM40", 9, {60, -35, -45}, 5, 2 * 12 - 1, 45, 950, 0.08f, {8.f}, 2, 3.0f, 2.5f},
        {{{"H&K MP5 SD, Single Shot", F::Single, 22}, {"H&K MP5 SD, Burst", F::Burst, 21},
          {"H&K MP5 SD, Full Auto", F::Auto, 20}},
         "PMP5", 11, {84, -35, -72}, 30, 2 * 12 - 1, 800, 295, 0.7f, {}, 1, 2.0f, 0.35f},
        {{{"SAW Auto. Weap., Full Auto", F::Auto, 12}},
         "PM249", 8, {85, -43, -73}, 200, 2 * 3 - 1, 750, 1000, 1.0f, {}, 1, 5.0f, 0.5f},
    };
    return defs;
}

const std::string& Arsenal::modeName() const { return names_[current_][slots_[current_].mode]; }

void Arsenal::init(const std::vector<std::string>& gameText) {
    names_.clear();
    for (const auto& d : weaponDefs()) {
        std::vector<std::string> n;
        for (const auto& m : d.modes)
            n.push_back(m.textIndex < int(gameText.size()) && !gameText[m.textIndex].empty() ? gameText[m.textIndex] : m.name);
        names_.push_back(std::move(n));
    }
    slots_.clear();
    for (int i = 0; i < int(weaponDefs().size()); ++i) {
        const auto& d = weaponDefs()[i];
        slots_.push_back({i, d.magazine, d.spareMags, 0});
    }
    current_ = 1;   // start with the carbine
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
    cooldown_ = 60.f / d.roundsPerMinute;
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
                if (burstLeft_ > 0 && --burstLeft_ == 0) cooldown_ = std::max(cooldown_, 0.25f);
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
