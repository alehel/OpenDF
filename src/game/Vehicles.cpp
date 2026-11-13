#include "Vehicles.h"

#include "Ai.h"
#include "Sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace df {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTicks = 64.f;   // DF.EXE's per-tick steps are speed >> 6
constexpr int kWaypointB = 6006;
constexpr int kGazelle = 2018;   // graphic number 0x7e2: loop 502, else 503

float wrapAngle(float a) { return a - std::floor(a / (2 * kPi) + 0.5f) * 2 * kPi; }

} // namespace

void VehicleSystem::init(World& world, const Mission& mission) {
    mission_ = &mission;
    vehicles_.clear();
    for (size_t i = 0; i < world.objects.size(); ++i) {
        WorldObject& o = world.objects[i];
        if (!o.item || o.missionIndex < 0) continue;
        const std::string& f = o.item->aiFunction;
        if (f != "veh0" && f != "helo") continue;
        const MissionObject& mo = mission.objects[size_t(o.missionIndex)];
        const auto* rec = reinterpret_cast<const uint8_t*>(mo.raw);
        Vehicle v;
        v.object = int(i);
        v.helo = f == "helo";
        const uint32_t fl = uint32_t(mo.raw[3]);
        v.flags = (fl & 1 ? 1u : 0u) | (fl & 2 ? 2u : 0u);
        v.path = rec[0x49];
        v.pathMode = v.path ? 1 : 0;
        v.waypoint = rec[0x4A];
        v.targetHeading = o.heading;
        v.lastHp = o.hp;
        v.alerted = rec[0x42] == 2;
        // A helicopter starts on the ground unless placed in the air.
        v.floorHeight = world.terrain->heightAt(o.pos.x, o.pos.z);
        v.think = 1 + int(i % 8);
        vehicles_.push_back(v);
    }
}

// Path step (0x45aa68 / 0x47c5d4): head for the point; arrival when within
// its radius or 2 |speed| (per tick) of it; a one-way path ends with speed 0.
bool VehicleSystem::pathStep(World& world, AiSystem& ai, Vehicle& v) {
    WorldObject& o = world.objects[size_t(v.object)];
    if (!v.pathMode || !mission_ || v.path <= 0 || v.path >= int(mission_->paths.size())) return false;
    const MissionPath& path = mission_->paths[size_t(v.path)];
    if (path.points.empty()) return false;
    v.waypoint = std::clamp(v.waypoint, 0, int(path.points.size()) - 1);
    const MissionObject* wp = mission_->inGroup(2, path.points[size_t(v.waypoint)]);
    if (!wp) return false;
    const float ext = world.extent();
    const Vec3 at = world.nearest({std::fmod(std::fmod(wp->x, ext) + ext, ext), 0, std::fmod(std::fmod(-wp->y, ext) + ext, ext)}, o.pos);
    float radius = float(wp->raw[7]);
    if (radius <= 0) radius = wp->type == kWaypointB ? 1.f : 0.5f;
    const float dx = std::abs(at.x - o.pos.x), dz = std::abs(at.z - o.pos.z);
    const float d = std::max(dx, dz) + std::min(dx, dz) * 0.5f;
    if (d <= radius || 2.f * std::abs(v.speed) / kTicks * kTicks > d) {
        ai.markReached(o.team, v.path, v.waypoint);   // 0x45c940
        if (++v.waypoint >= int(path.points.size())) {
            v.waypoint = 0;
            if (path.flags & 1) {
                v.pathMode = 0;
                v.path = 0;
                v.stopped = true;
                if (!v.hasHome) { v.home = o.pos; v.hasHome = true; }   // 0x47c717
                return false;
            }
        }
    }
    v.targetHeading = std::atan2(at.x - o.pos.x, -(at.z - o.pos.z));
    return true;
}

// veh0 (0x480514): speed ramps toward the team speed by `acceleration`
// per tick (doubled once destroyed); steering angle = heading error / 8;
// yaw rate = (pi/4) / axle x v x sin(steer); forward v x cos(steer); the
// body sits on the ground between points 2.75 m ahead and behind, pitched
// and rolled to it (+-0.6875 m to the sides). No collisions.
void VehicleSystem::drive(World& world, AiSystem& ai, Vehicle& v, float teamSpeed) {
    WorldObject& o = world.objects[size_t(v.object)];
    if (--v.think <= 0) {
        const bool onPath = pathStep(world, ai, v);
        v.targetSpeed = (onPath && !v.dead) ? teamSpeed : 0.f;
        v.think = std::abs(v.speed) > 0 ? 0x40 : 0x10;
    }
    const float accel = (o.item ? o.item->acceleration : 1.f) / kTicks * (v.dead ? 2.f : 1.f);
    if (std::abs(v.targetSpeed - v.speed) <= accel) v.speed = v.targetSpeed;
    else v.speed += v.targetSpeed > v.speed ? accel : -accel;
    const float steer = wrapAngle(v.targetHeading - o.heading) / 8.f;
    const float axle = o.item && o.item->axleDist > 0 ? o.item->axleDist : 2.3f;
    o.heading += (kPi / 4.f) / axle * v.speed * std::sin(steer) / kTicks;
    const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)}, right{std::cos(o.heading), 0, std::sin(o.heading)};
    o.pos = o.pos + fwd * (v.speed * std::cos(steer) / kTicks);
    const float ext = world.extent();
    o.pos.x -= std::floor(o.pos.x / ext) * ext;
    o.pos.z -= std::floor(o.pos.z / ext) * ext;
    const Vec3 f = o.pos + fwd * 2.75f, b = o.pos - fwd * 2.75f, l = o.pos - right * 0.6875f, r = o.pos + right * 0.6875f;
    const float hf = world.terrain->heightAt(f.x, f.z), hb = world.terrain->heightAt(b.x, b.z);
    const float hl = world.terrain->heightAt(l.x, l.z), hr = world.terrain->heightAt(r.x, r.z);
    o.base = (hf + hb) * 0.5f;
    o.pos.y = o.base + o.lift;
    // (front - back) << 11 and (left - right) << 13 as binary angles.
    o.pitch = (hf - hb) / 32.f * 2 * kPi;
    o.roll = (hl - hr) / 8.f * 2 * kPi;
}

// helo (0x47c92c think every 8 ticks, 0x47d05c move): rotor spin-up
// (+0x800000 per think to 0x1b000000; lift from 0x14000000); modes: on a
// path 18 m above the floor (band 4 m), hover 8 m (1 m), land; turning at
// turn_rate when more than 6 m up; speed ramps at `acceleration`; vertical
// speed capped at 2 m/s; bank from the turn (within 22.5 degrees), nose
// down by speed.
void VehicleSystem::fly(World& world, AiSystem& ai, Vehicle& v, float teamSpeed) {
    WorldObject& o = world.objects[size_t(v.object)];
    const float turn = 2 * kPi;   // binary angle -> radians
    const float ground = world.terrain->heightAt(o.pos.x, o.pos.z);
    const float rest = (o.item && o.item->id == 100000 + kGazelle) ? 0x3f000 / 65536.f : 0x37000 / 65536.f;
    if (--v.think <= 0) {
        v.think = 8;
        if (!v.dead) v.rotor = std::min(v.rotor + float(0x800000), float(0x1b000000));
        else v.rotor = std::max(0.f, v.rotor - float(v.rotor > 0x700000 ? 0x400000 : 0x20000));
        const bool onPath = !v.dead && pathStep(world, ai, v);
        if (onPath) { v.mode = 2; v.targetSpeed = teamSpeed; }
        else if (v.gunTarget != -1 && !v.dead) {
            // Attack approach (0x47bf0c) toward the target's position when
            // acquired: > 80 m 15 m/s at 18 m; 30-80 m 5 m/s at 8 m; 8-30 m
            // hover at 8 m; closer 5 m/s at 8 m. The Black Hawk turns its door
            // to the target: +22.5 / +67.5 degrees in the middle bands.
            const Vec3 d = world.nearest(v.gunAcquired, o.pos) - o.pos;
            const float dist = std::sqrt(d.x * d.x + d.z * d.z);
            const bool hawk = !(o.item && o.item->id == 100000 + kGazelle);
            float h = std::atan2(d.x, -d.z);
            if (dist > 80) { v.targetSpeed = 15; v.mode = 2; }
            else if (dist > 30) { v.targetSpeed = 5; v.mode = 1; if (hawk) h += kPi / 8; }
            else if (dist > 8) { v.targetSpeed = 0; v.mode = 1; if (hawk) h += 3 * kPi / 8; }
            else { v.targetSpeed = 5; v.mode = 1; }
            v.targetHeading = h;
        } else if (!v.dead && (v.flags & 2) && v.hasHome) {
            // Back to the point where the path ended (0x47c4f0).
            const Vec3 d = world.nearest(v.home, o.pos) - o.pos;
            const float dist = std::sqrt(d.x * d.x + d.z * d.z);
            v.targetHeading = std::atan2(d.x, -d.z);
            if (dist > 80) v.targetSpeed = 15, v.mode = 2;
            else if (dist > 30) v.targetSpeed = 5, v.mode = 2;
            else if (dist > 4) v.targetSpeed = 2.5f, v.mode = 1;
            else v.targetSpeed = 0, v.mode = 0;
        } else {
            v.targetSpeed = 0;
            v.mode = (v.alerted || std::abs(v.speed) >= 0x400 / 65536.f * kTicks) ? 1 : 0;
            if (v.mode == 1 && v.stopped && !v.alerted) v.mode = 0;
        }
        // The floor: the ground around and ahead (1, 2, 4 s at this speed).
        const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)};
        float fl = ground;
        for (const Vec3 p : {o.pos + Vec3{4, 0, 0}, o.pos - Vec3{4, 0, 0}, o.pos + Vec3{0, 0, 4}, o.pos - Vec3{0, 0, 4},
                             o.pos + fwd * v.speed, o.pos + fwd * (2 * v.speed), o.pos + fwd * (4 * v.speed)})
            fl = std::max(fl, world.terrain->heightAt(p.x, p.z));
        v.floorHeight = fl;
    }
    const bool lift = v.rotor >= float(0x14000000);
    const float height = o.pos.y - v.floorHeight;
    // Turning (0x47ba4c): turn_rate degrees/s, only above 6 m.
    v.yawRate = 0;
    if (height > 6.f && lift) {
        const float maxTurn = (o.item ? o.item->turnRate : 30.f) * kPi / 180.f / kTicks;
        const float err = wrapAngle(v.targetHeading - o.heading);
        v.yawRate = std::clamp(err, -maxTurn, maxTurn);
        o.heading += v.yawRate;
    }
    // Speed (0x47bacc).
    const float accel = (o.item ? o.item->acceleration : 4.f) / kTicks;
    if (std::abs(v.targetSpeed - v.speed) <= accel) v.speed = v.targetSpeed;
    else v.speed += v.targetSpeed > v.speed ? accel : -accel;
    if (!lift && height < 1.f) v.speed = 0;
    // Altitude (0x47bc1c): climb below floor + H, descend above floor + H +
    // band, damped inside; land: down at up to 1 m/s; no lift: fall.
    const float unit = 1.f / 65536.f;   // m per tick
    if (!lift || v.dead) {
        v.vz -= 0x134 * unit;
    } else if (v.mode == 0) {
        v.vz = std::max(v.vz - 0x9a * unit, -0x400 * unit);
    } else {
        const float H = v.mode == 2 ? 18.f : 8.f, band = v.mode == 2 ? 4.f : 1.f;
        if (height < H) v.vz += 0x100 * unit;
        else if (height > H + band) v.vz -= 0x100 * unit;
        else v.vz += v.vz < 0 ? 0x33 * unit : -0xcd * unit;
        v.vz = std::clamp(v.vz, -0x800 * unit, 0x800 * unit);
    }
    const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)};
    o.pos = o.pos + fwd * (v.speed / kTicks);
    o.pos.y += v.vz;
    if (o.pos.y < ground + rest) {
        // On the ground; hitting it fast while falling destroys it (0x47d1da).
        if (v.vz < -0x1000 * unit && !v.dead && o.hp > 0) world.damage(v.object, o.hp + 100);
        o.pos.y = ground + rest;
        v.vz = 0;
    }
    const float ext = world.extent();
    o.pos.x -= std::floor(o.pos.x / ext) * ext;
    o.pos.z -= std::floor(o.pos.z / ext) * ext;
    o.base = o.pos.y - o.lift;
    // Bank toward the turn (within 22.5 degrees, 0x400000 per tick), level
    // below 6 m; nose down by speed (-v << 8).
    const float bankTarget = height > 6.f ? std::clamp(-v.yawRate * 8.f * v.speed, -kPi / 8, kPi / 8) : 0.f;
    const float step = float(0x400000) / 4294967296.f * turn;
    v.bank += std::clamp(bankTarget - v.bank, -step, step);
    o.roll = v.bank;
    o.pitch = -v.speed * 256.f * 65536.f / 4294967296.f * turn;
}

// The door gun (0x47c148 / 0x47c734): the SAW (mode 12) at targets within
// reach, in bursts of a round every 8 ticks for 16..47 ticks, pauses of
// 64..127, after a wait of 32..95 ticks; per-shot error +-0x200000 and a
// yaw bias shrinking by 1/4 after each burst. APPROXIMATIONS: targets are
// the nearest visible enemy under 100 m (DF.EXE scores them as soldiers do
// and draws against distance); the muzzle is 1 m below the helicopter; the
// gun turns to the aim at once (DF.EXE slews 0x800000 per tick).
void VehicleSystem::doorGun(World& world, Vehicle& v, Arsenal& shots, SoundSystem& sound, const Target& player) {
    WorldObject& o = world.objects[size_t(v.object)];
    if (v.dead || (v.flags & 1) || v.rotor < float(0x14000000)) return;
    if (v.cooldown > 0) { --v.cooldown; return; }
    const Vec3 muzzle = o.pos - Vec3{0, 1.f, 0};
    auto visible = [&](Vec3 at) {
        RayHit h;
        return !world.raycast(muzzle, world.nearest(at, muzzle), h, true, v.object, false);
    };
    auto targetPos = [&](int t) { return t == -3 ? player.at : world.objects[size_t(t)].pos; };
    auto targetAlive = [&](int t) { return t == -3 ? player.alive : (t >= 0 && world.objects[size_t(t)].alive()); };
    if (v.gunTarget != -1) {
        if (!targetAlive(v.gunTarget)) { v.gunTarget = -1; return; }
        const Vec3 tp = world.nearest(targetPos(v.gunTarget), o.pos);
        const Vec3 moved = tp - world.nearest(v.gunAcquired, tp);
        if (moved.x * moved.x + moved.z * moved.z > 100.f * 100.f) { v.gunTarget = -1; v.cooldown = 60 * 8; return; }
    } else if ((v.think & 7) == 0) {
        float best = 100.f;
        auto consider = [&](int id, Vec3 at, int side) {
            if (side == 0 || side == o.side) return;
            const Vec3 d = world.nearest(at, o.pos) - o.pos;
            const float dist = std::sqrt(dot(d, d));
            if (dist < best && visible(at)) best = dist, v.gunTarget = id, v.gunAcquired = at;
        };
        if (player.alive) consider(-3, player.at, player.side);
        for (size_t i = 0; i < world.objects.size(); ++i)
            if (world.objects[i].person && world.objects[i].alive()) consider(int(i), world.objects[i].pos, world.objects[i].side);
        if (v.gunTarget != -1) {
            v.wait = 32 + int(rnd() >> 10) % 64;
            v.bias = (rnd() & 0x8000) ? float(rnd() & 0x7fffff) : -float(0x1000000 + (rnd() & 0x7fffff));
        }
        return;
    }
    if (v.gunTarget == -1) return;
    if (v.wait > 0) { --v.wait; return; }
    if (v.burst <= 0) {
        if (v.pause > 0) { --v.pause; if (v.pause == 0) v.burst = 16 + int(rnd() >> 10) % 32; return; }
        v.pause = 64 + int(rnd() >> 10) % 64;
        v.bias *= 0.75f;
        return;
    }
    --v.burst;
    if (++v.fireTimer % 8) return;
    const Vec3 d = world.nearest(targetPos(v.gunTarget), muzzle) - muzzle;
    const float turn = 2 * kPi / 4294967296.f;
    auto err = [&]() { return (float(int(rnd()) - 0x8000) / 32768.f) * float(0x200000) * turn; };
    float yaw = std::atan2(d.x, -d.z) + v.bias * turn + err();
    float pitch = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) + err();
    // Gun limits relative to the body (0x47c220 / 0x47c340); it fires only
    // within 16.9 degrees of the aim: gazelle yaw +-45, pitch 0..-45; Black
    // Hawk yaw -135..-45 (its door), pitch 0..-16.9.
    {
        const bool hawk = !(o.item && o.item->id == 100000 + kGazelle);
        float rel = wrapAngle(yaw - o.heading);
        const float deg = kPi / 180.f;
        const float lo = hawk ? -135 * deg : -45 * deg, hi = hawk ? -45 * deg : 45 * deg;
        const float plo = hawk ? -16.9f * deg : -45 * deg;
        const float cr = std::clamp(rel, lo, hi), cp = std::clamp(pitch, plo, 0.f);
        if (std::abs(cr - rel) > 16.9f * deg || std::abs(cp - pitch) > 16.9f * deg) return;
        yaw = o.heading + cr;
        pitch = cp;
    }
    const ModeDef& md = modeInfo(12);
    const GunDef& g = gunInfo(md.gun);
    Projectile p;
    p.pos = muzzle;
    p.vel = Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)} * (float(g.velocity) * 60.f / 62.f);
    p.lifetime = g.lifetime;
    p.power = g.power;
    p.falloff = std::max(1, g.falloff);
    p.flags = g.flags;
    p.from = o.pos;
    p.shooter = v.object;
    shots.addProjectile(p);
    if (g.sounds[0] > 0) sound.play(g.sounds[0], muzzle);
}

void VehicleSystem::tick(World& world, AiSystem& ai, Arsenal& shots, SoundSystem& sound, const Target& player) {
    for (auto& v : vehicles_) {
        WorldObject& o = world.objects[size_t(v.object)];
        if (o.removed) continue;
        // Shot: helicopters become alerted (0x44f221).
        if (o.hp < v.lastHp) v.alerted = true;
        v.lastHp = o.hp;
        if (!v.dead && (o.wrecked || (o.hp <= 0 && o.item && o.item->hp > 0))) {
            // Destroyed (0x480357 / 0x47c83c): the blast (shrapnel 60, or for a
            // helicopter 40 plus two of 10 to the sides), speed to zero.
            v.dead = true;
            v.targetSpeed = 0;
            if (detonate) {
                const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)};
                if (v.helo) {
                    detonate(o.pos + Vec3{0, 1, 0}, 40, o.heading);
                    detonate(o.pos + fwd * 4.f, 10, o.heading);
                    detonate(o.pos - fwd * 4.f, 10, o.heading);
                } else {
                    detonate(o.pos + Vec3{0, 1, 0}, 60, o.heading);
                }
            }
        }
        const float teamSpeed = ai.teamSpeedMs(o.team);
        static int dbg = 0;
        if (std::getenv("OPENDF_DEBUGVEH") && (++dbg % (120 * int(vehicles_.size()))) < int(vehicles_.size()))
            std::fprintf(stderr, "veh %d %s team %d path %d wp %d speed %.1f/%.1f pos (%.1f %.1f %.1f) hdg %.0f rotor %.0f mode %d\n",
                         v.object, v.helo ? "helo" : "veh0", o.team, v.path, v.waypoint, v.speed, teamSpeed, o.pos.x, o.pos.y, o.pos.z,
                         o.heading * 57.3f, v.rotor / 1e6f, v.mode);
        if (v.helo) {
            fly(world, ai, v, teamSpeed);
            doorGun(world, v, shots, sound, player);
        } else {
            drive(world, ai, v, teamSpeed);
        }
    }
}

void VehicleSystem::sounds(const World& world, const AiSystem& ai, SoundSystem& sound) const {
    for (const auto& v : vehicles_) {
        const WorldObject& o = world.objects[size_t(v.object)];
        if (o.removed) continue;
        if (v.helo) {
            // Rotor loop 502 (gazelle) or 503, pitch (rotor >> 13) + 0x5000
            // within 0x6000..0x12000 (0x47ca0f).
            if (v.rotor > 0) {
                const float pitch = std::clamp((v.rotor / 8192.f + 0x5000) / 65536.f, 0x6000 / 65536.f, 0x12000 / 65536.f);
                sound.setLoop(100000 + v.object, (o.item && o.item->id == 100000 + kGazelle) ? 502 : 503, o.pos, pitch);
            }
        } else if (std::abs(v.speed) > 0 && !v.dead) {
            // Engine loop 506, pitch 1 + v / team speed (0x48026d).
            const float ts = ai.teamSpeedMs(o.team);
            sound.setLoop(100000 + v.object, 506, o.pos, ts > 0 ? 1.f + v.speed / ts : 1.f);
        }
    }
}

void VehicleSystem::followPath(World& world, int team, int path) {
    // 0x46bbe8 also sends vehicles of the team down the path, from the nearest point.
    if (team < 3 || !mission_ || path <= 0 || path >= int(mission_->paths.size())) return;
    const MissionPath& mp = mission_->paths[size_t(path)];
    const float ext = world.extent();
    for (auto& v : vehicles_) {
        WorldObject& o = world.objects[size_t(v.object)];
        if (o.team != team || v.dead) continue;
        v.pathMode = 1;
        v.path = path;
        v.stopped = false;
        float best = 1e30f;
        for (size_t k = 0; k < mp.points.size(); ++k) {
            const MissionObject* wp = mission_->inGroup(2, mp.points[k]);
            if (!wp) continue;
            const Vec3 d = world.nearest({std::fmod(std::fmod(wp->x, ext) + ext, ext), 0, std::fmod(std::fmod(-wp->y, ext) + ext, ext)}, o.pos) - o.pos;
            if (d.x * d.x + d.z * d.z < best) best = d.x * d.x + d.z * d.z, v.waypoint = int(k);
        }
        v.think = 1;
    }
}

} // namespace df
