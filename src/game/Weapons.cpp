#include "Weapons.h"

#include <algorithm>
#include <cmath>

namespace df {

namespace {
constexpr float kPi = 3.14159265f;
}

const std::vector<WeaponDef>& weaponDefs() {
    static const std::vector<WeaponDef> defs = {
        //  name          view     mag spare  rpm  auto   m/s   spread  zoom        scope dmg reload recoil
        {".45 PISTOL",   "PM19",    7,  4,  300, false, 255,  1.4f, {1.5f, 1.5f}, false, 1, 1.6f, 1.6f},
        {"M4 CARBINE",   "PM4",    30,  8,  800, true,  880,  0.5f, {2.0f, 2.0f}, false, 1, 2.2f, 0.6f},
        {"M40 SNIPER",   "PM40",    5,  8,   45, false, 790,  0.08f,{4.0f, 10.f}, true,  2, 3.0f, 2.5f},
        {"MP5SD",        "PMP5",   30,  8,  800, true,  285,  0.7f, {1.5f, 1.5f}, false, 1, 2.0f, 0.35f},
        {"M249 SAW",     "PM249", 200,  3,  750, true,  915,  1.0f, {2.0f, 2.0f}, false, 1, 5.0f, 0.5f},
    };
    return defs;
}

void Arsenal::init() {
    slots_.clear();
    for (int i = 0; i < int(weaponDefs().size()); ++i) {
        const auto& d = weaponDefs()[i];
        slots_.push_back({i, d.magazine, d.spareMags, d.canAuto});
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
    }
    if (mode >= 0 && def().canAuto) slots_[current_].autoMode = mode == 1;
}

void Arsenal::cycle() { select((current_ + 1) % int(slots_.size())); }

void Arsenal::reload() {
    Slot& s = slots_[current_];
    if (reloadTimer_ > 0 || s.mags <= 0 || s.rounds == def().magazine) return;
    reloadTimer_ = def().reloadSeconds;
    zoomLevel_ = 0;
}

void Arsenal::toggleZoom() {
    if (reloadTimer_ > 0) return;
    const WeaponDef& d = def();
    int levels = d.zoom[1] != d.zoom[0] ? 2 : 1;
    zoomLevel_ = (zoomLevel_ + 1) % (levels + 1);
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
    if (zoomLevel_) spread *= d.scope ? 0.25f : 0.6f;
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
        const bool wantsFire = s.autoMode ? triggerDown : triggerPressed;
        if (wantsFire && cooldown_ <= 0) {
            if (s.rounds > 0) fire(player, world);
            else if (triggerPressed) {
                if (s.mags > 0) { messages.push_back("RELOADING"); reload(); }
                else messages.push_back("OUT OF AMMO");
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
