#include "Weapons.h"

#include <algorithm>
#include <cmath>

namespace df {

namespace {
constexpr float kPi = 3.14159265f;
}

const std::vector<WeaponDef>& weaponDefs() {
    using F = FireMode;
    // Names from the game's DFGAME.BIN string table; icons are WEAP640.PCX
    // frames. Gun offsets and scope magnifications come from DF.EXE's player
    // weapon table (168-byte records at 0x4E97D0; +0x78 offset, +0xA0 scope
    // power, which is 0 for weapons without a scope). Fire rates, velocities,
    // spread, damage and reload times are still real-world approximations.
    static const std::vector<WeaponDef> defs = {
        {{{"SpecOps .45 caliber pistol", F::Single}},
         "PM19", 10, {102, -34, -43}, 7, 4, 300, 255, 1.4f, {}, 1, 1.6f, 1.6f},
        {{{"M4 Assault Rifle, Single Shot", F::Single}, {"M4 Assault Rifle, Burst", F::Burst}},
         "PM4", 4, {72, -44, -75}, 30, 8, 800, 880, 0.5f, {4.f}, 1, 2.2f, 0.6f},
        {{{"M40 Sniper Rifle", F::Single}},
         "PM40", 9, {60, -35, -45}, 5, 8, 45, 790, 0.08f, {8.f}, 2, 3.0f, 2.5f},
        {{{"H&K MP5 SD, Single Shot", F::Single}, {"H&K MP5 SD, Burst", F::Burst}, {"H&K MP5 SD, Full Auto", F::Auto}},
         "PMP5", 11, {84, -35, -72}, 30, 8, 800, 285, 0.7f, {}, 1, 2.0f, 0.35f},
        {{{"SAW Auto. Weap., Full Auto", F::Auto}},
         "PM249", 8, {85, -43, -73}, 200, 3, 750, 915, 1.0f, {}, 1, 5.0f, 0.5f},
    };
    return defs;
}

void Arsenal::init() {
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
                if (triggerPressed) {
                    if (s.mags > 0) { messages.push_back("Reloading."); reload(); }
                    else messages.push_back("Out of ammunition.");
                }
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
                if (hit.object >= 0) {
                    std::string msg = world.damage(hit.object, b.damage);
                    if (!msg.empty()) messages.push_back(msg);
                }
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
