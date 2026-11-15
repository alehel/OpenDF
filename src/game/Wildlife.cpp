#include "Wildlife.h"

#include "Moves.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace df {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kWaypointB = 6006;

int32_t fix(float m) { return int32_t(std::lround(double(m) * 65536.0)); }
float unfix(int64_t v) { return float(double(v) / 65536.0); }
float yawToHeading(uint32_t yaw) { return kPi * 0.5f - float(double(yaw) / 4294967296.0 * 2 * kPi); }
uint32_t headingToYaw(float h) { return uint32_t(int64_t(std::llround(double(kPi * 0.5f - h) / (2 * kPi) * 4294967296.0))); }
float yawRad(uint32_t yaw) { return float(double(yaw) / 4294967296.0 * 2 * kPi); }
// 0x41e930: the DF binary angle of (east, north).
uint32_t angleOf(float east, float north) {
    return uint32_t(int64_t(std::llround(double(std::atan2(north, east)) / (2 * kPi) * 4294967296.0)));
}
// 0x41e880: max + 11/32 mid + 1/4 min, 16.16.
int32_t approx(int32_t a, int32_t b, int32_t c) {
    a = std::abs(a), b = std::abs(b), c = std::abs(c);
    if (a < b) std::swap(a, b);
    if (a < c) std::swap(a, c);
    if (b < c) std::swap(b, c);
    // 32-bit wrap as the EXE's imul / add (as Ai.cpp), done unsigned: no UB.
    const int32_t mid = int32_t(uint32_t(b) * 11u) >> 5;
    return int32_t(uint32_t(a) + uint32_t(mid) + uint32_t(c >> 2));
}
int32_t mul16(int64_t a, int64_t b) { return int32_t((a * b) >> 16); }

} // namespace

void WildlifeSystem::init(World& world, const Mission& mission, bool night) {
    mission_ = &mission;
    animals_.clear();
    if (!world.df2Rules) return;
    for (size_t i = 0; i < world.objects.size(); ++i) {
        WorldObject& o = world.objects[i];
        if (!o.item || o.missionIndex < 0 || o.removed) continue;
        const std::string& f = o.item->aiFunction;
        if (f != "vult" && f != "peng" && f != "prnh") continue;
        const MissionObject& mo = mission.objects[size_t(o.missionIndex)];
        const auto* rec = reinterpret_cast<const uint8_t*>(mo.raw);
        Animal a;
        a.object = int(i);
        a.kind = f == "vult" ? Kind::Vulture : f == "peng" ? Kind::Penguin : Kind::Fish;
        a.yaw = a.wantYaw = headingToYaw(o.heading);
        a.homeX = o.pos.x;
        a.homeZ = o.pos.z;
        // The AI block (0x403790) as the soldiers' and vehicles': path
        // +0x49 / point +0x4A, perception +0x24 (at night above 0x4ccc
        // less 0x2666), field of view +0x38, alertness +0x42, flags +0xC
        // (1 no acquisition, 8 -> ai flag 4).
        a.path = rec[0x49];
        a.pathMode = a.path ? 1 : 0;
        a.waypoint = rec[0x4A];
        a.perception = (mo.raw[0x24 / 4] > 0 ? float(mo.raw[0x24 / 4]) : 100.f) / 100.f;
        if (night && a.perception > 0.3f) a.perception -= 0.15f;
        a.alert = std::clamp(int(rec[0x42]), 0, 2);
        {
            const uint16_t fov = uint16_t(rec[0x38] | rec[0x39] << 8);
            a.fov = (int((fov ? float(fov) : 180.f) * 65536.f / 360.f) >> 9) | 1;
        }
        const uint32_t fl = uint32_t(mo.raw[3]);
        a.aiFlags = (fl & 1 ? 1u : 0u) | (fl & 8 ? 4u : 0u);
        // INFERRED: the first think staggered as the markers' (counter % 60, +11 a record).
        a.think = 1 + (mo.indexInGroup * 11) % 60;
        animals_.push_back(a);
    }
}

// The mover's position step (0x42b400): +0x3C / +0x40 = speed (cos, sin)
// >> 6 (0 at speed 0), added to x / y.
void WildlifeSystem::step(World& world, Animal& a) {
    WorldObject& o = world.objects[size_t(a.object)];
    if (a.speed == 0) a.vx = a.vy = 0;
    else {
        a.vx = int32_t((double(a.speed) * std::cos(yawRad(a.yaw))) / 64.0);
        a.vy = int32_t((double(a.speed) * std::sin(yawRad(a.yaw))) / 64.0);
    }
    o.pos.x += unfix(a.vx);
    o.pos.z -= unfix(a.vy);
    const float ext = world.extent();
    o.pos.x -= std::floor(o.pos.x / ext) * ext;
    o.pos.z -= std::floor(o.pos.z / ext) * ext;
    o.tickVel = {unfix(a.vx), 0, -unfix(a.vy)};
}

// 0x42b3a0: the speed toward `to` by the item's acceleration (def +0xE0 =
// acceleration << 10), doubled once destroyed; it snaps when closer.
void WildlifeSystem::ramp(World& world, Animal& a, int32_t to) {
    const WorldObject& o = world.objects[size_t(a.object)];
    int32_t acc = int32_t(o.item ? o.item->acceleration : 0.f) << 10;
    if (a.dead) acc <<= 1;
    if (a.speed == to) return;
    if (std::abs(to - a.speed) < acc) a.speed = to;
    else a.speed += to > a.speed ? acc : -acc;
}

// 0x42b320: the yaw toward +0xA0 by def +0xE4 (turn_rate << 16 / 360 >> 6)
// << 16 a tick (x4 for a piranha with a target, 0x42df13); it snaps when
// closer.
void WildlifeSystem::turn(World& world, Animal& a, int factor) {
    WorldObject& o = world.objects[size_t(a.object)];
    const int32_t rate = ((int32_t(o.item ? o.item->turnRate : 0.f) << 16) / 360 >> 6) * factor;
    o.tickTurn = 0;
    if (a.yaw == a.wantYaw) return;
    const int32_t d = int32_t(a.wantYaw - a.yaw);
    if (std::abs(d >> 16) < rate) a.yaw = a.wantYaw;
    else {
        const uint32_t s = uint32_t(rate) << 16;
        a.yaw += (d >> 16) > 0 ? s : uint32_t(0) - s;
        o.tickTurn = -float(int32_t((d >> 16) > 0 ? s : uint32_t(0) - s)) / 4294967296.f * 2 * kPi;
    }
}

void WildlifeSystem::place(World& world, Animal& a, float z) {
    WorldObject& o = world.objects[size_t(a.object)];
    o.pos.y = z;
    o.base = z - o.lift;
}

// The path steering (0x4033d0, path mode 1): the heading to the current
// point, the distance (max + min / 2) and the point's radius.
bool WildlifeSystem::steer(World& world, const Animal& a, uint32_t& yaw, int32_t& dist, int32_t& radius) const {
    const WorldObject& o = world.objects[size_t(a.object)];
    if (a.pathMode != 1 || !mission_ || a.path <= 0 || a.path >= int(mission_->paths.size())) return false;
    const MissionPath& path = mission_->paths[size_t(a.path)];
    if (path.points.empty() || a.waypoint < 0 || a.waypoint >= int(path.points.size())) return false;
    const MissionObject* wp = mission_->inGroup(2, path.points[size_t(a.waypoint)]);
    if (!wp) return false;
    const float ext = world.extent();
    const Vec3 at = world.nearest({std::fmod(std::fmod(wp->x, ext) + ext, ext), 0, std::fmod(std::fmod(-wp->y, ext) + ext, ext)}, o.pos);
    const int32_t dx = std::abs(fix(at.x - o.pos.x)), dy = std::abs(fix(at.z - o.pos.z));
    dist = dy < dx ? (dy >> 1) + dx : (dx >> 1) + dy;
    yaw = angleOf(at.x - o.pos.x, -(at.z - o.pos.z));
    float r = float(wp->raw[7]);   // as VehicleSystem::pathStep reads the marker radius
    if (r <= 0) r = wp->type == kWaypointB ? 1.f : 0.5f;
    radius = fix(r);
    return true;
}

// A point reached (0x42f0f0.. / 0x42dc80..): marked for the team
// (0x41ac90), the next one; at the end of a one-way path the path mode
// and path are cleared (false).
bool WildlifeSystem::nextPoint(World& world, AiSystem& ai, Animal& a) {
    const WorldObject& o = world.objects[size_t(a.object)];
    ai.markReached(o.team, a.path, a.waypoint);
    const MissionPath& path = mission_->paths[size_t(a.path)];
    if (++a.waypoint >= int(path.points.size())) a.waypoint = 0;
    if (a.waypoint == 0 && (path.flags & 1)) {
        a.pathMode = 0;
        a.path = 0;
        return false;
    }
    return true;
}

// 0x42d870(z, band, full): the vertical acceleration +0xB4 toward the band
// [z, z + band]: below +0x40 (while vz < 0x100), above -0x40 (while vz >
// -0x100), inside damped by 0x20; >> 3 unless `full`.
void WildlifeSystem::depth(World& world, Animal& a, int32_t z, int32_t band, bool full) {
    const WorldObject& o = world.objects[size_t(a.object)];
    const int32_t at = fix(o.pos.y);
    const int32_t err = at < z ? z - at : at > z + band ? z - at + band : 0;
    a.az = 0;
    if (err < 0) { if (a.vz > -0x100) a.az = -0x40; }
    else if (err == 0) { if (a.vz > 0) a.az = -0x20; else if (a.vz < 0) a.az = 0x20; }
    else if (a.vz < 0x100) a.az = 0x40;
    if (!full) a.az >>= 3;
}

void WildlifeSystem::tick(World& world, AiSystem& ai, const AiSystem::PlayerView& player, bool playerSwimming) {
    for (auto& a : animals_) {
        WorldObject& o = world.objects[size_t(a.object)];
        if (o.removed) continue;
        if (--a.think <= 0) {
            if (a.kind == Kind::Vulture) vultureThink(world, ai, a, player);
            else if (a.kind == Kind::Penguin) penguinThink(world, ai, a);
            else fishThink(world, ai, a, player, playerSwimming);
        }
        if (a.kind == Kind::Vulture) vultureMove(world, a);
        else if (a.kind == Kind::Penguin) penguinMove(world, a);
        else fishMove(world, a);
        static const bool debug = std::getenv("OPENDF_DEBUGWILD") != nullptr;
        if (debug && a.think == 1)
            std::fprintf(stderr, "wild %d %s pos (%.1f %.1f %.1f) speed %.2f/%.2f yaw %.0f state %d target %d path %d/%d dead %d\n", a.object,
                         o.item->aiFunction.c_str(), o.pos.x, o.pos.y, o.pos.z, unfix(a.speed), unfix(a.want), yawRad(a.yaw) * 57.3f,
                         a.state, a.target, a.pathMode ? a.path : 0, a.waypoint, int(a.dead));
        o.heading = yawToHeading(a.yaw);
        o.pitch = float(double(a.pitch) / 4294967296.0 * 2 * kPi);
        o.roll = -float(double(a.roll) / 4294967296.0 * 2 * kPi);   // OpenDF's roll is the other way (as the helicopter bank)
    }
}

// ---- Vultures ----------------------------------------------------------

// vult think (GOG Df2.exe 0x42dfe0), every 0x10 ticks. Destroyed: 0x10C,
// the speed wanted 0, +0xC0 0 and ENV_VULTURE_DEATH (0x46) once; then the
// think idles (0x400). Without a target: 3 m/s, home (ai+0x64 / +0x68)
// again with a random offset (rand & 0x1F00) << 16 when more than 200 m
// (2D, 0x41e880) away, ENV_VULTURE_CALL (0x45) with chance 1/64, and the
// corpse scan (0x42aeb0) drawn against rand: a target is marked spotted
// (0x41ac00). With one (0x42df50): at it at 3 m/s beyond 16 m, else 2 m/s
// with the wanted yaw +0x18000000, so it circles. The velocity +0x3C /
// +0x40 is set here, from the speed and yaw.
void WildlifeSystem::vultureThink(World& world, AiSystem& ai, Animal& a, const AiSystem::PlayerView& player) {
    WorldObject& o = world.objects[size_t(a.object)];
    a.think = 0x10;
    if (a.dead) { a.think = 0x400; return; }
    if (o.wrecked || (o.item->hp > 0 && o.hp <= 0)) {
        a.dead = true;
        a.want = 0;
        a.vz = 0;
        if (playSound) playSound(0x46, o.pos);
        return;
    }
    auto posOf = [&](int t) { return t == -3 ? player.origin : world.objects[size_t(t)].pos; };
    if (a.target == -1) {
        a.want = 0x30000;
        const Vec3 home = world.nearest({a.homeX, 0, a.homeZ}, o.pos);
        if (approx(fix(home.x - o.pos.x), fix(home.z - o.pos.z), 0) > 0xC80000)
            a.wantYaw = angleOf(home.x - o.pos.x, -(home.z - o.pos.z)) + ((rnd() & 0x1F00) << 16);
        if ((rnd() & 0x3F) == 0 && playSound) playSound(0x45, o.pos);
        // 0x42aeb0: dead people (+0x64 & 4) of another side, not side 0,
        // within 90 degrees of the yaw and 999 m (whole metres), in sight
        // (1 or 2): 100 / d x (64 - |angle| in 1/256 turns) / 64, x2 for
        // anyone but the player; the best x perception, / 4 when relaxed.
        int best = -1;
        int32_t bestScore = 0;
        auto consider = [&](int id, Vec3 at, int side) {
            if (side == o.side || side == 0) return;
            const Vec3 tp = world.nearest(at, o.pos);
            uint32_t u = (angleOf(tp.x - o.pos.x, -(tp.z - o.pos.z)) - a.yaw) >> 24;
            if (u > 0x7F) u = 0x100 - u;
            if (u >= 0x41) return;
            const int32_t d = approx(fix(tp.x - o.pos.x), fix(tp.z - o.pos.z), fix(tp.y - o.pos.y)) >> 16;
            int32_t s = 0x640000;
            if (d != 0) {
                if (d > 999) return;
                s = 0x640000 / d;
            }
            const int r = ai.sightLine(world, o.pos, tp, a.object, id);
            if (r < 1 || r > 2) return;
            int32_t score = mul16(s, (0x400000 - int32_t(u) * 0x10000) >> 6);
            if (id != -3) score <<= 1;
            if (bestScore < score) { best = id; bestScore = score; }
        };
        for (size_t i = 0; i < world.objects.size(); ++i) {
            const WorldObject& q = world.objects[i];
            if (q.person && q.dead && !q.removed) consider(int(i), q.pos, q.side);
        }
        if (!player.alive) consider(-3, player.origin, player.side);
        int32_t score = mul16(bestScore, fix(a.perception));
        if (a.alert == 0) score >>= 2;
        if (best != -1 && int32_t(rnd()) < score) {
            ai.markSpotted(o.team, best == -3 ? player.team : world.objects[size_t(best)].team);
            a.target = best;
            a.lastKnown = posOf(best);
        }
    } else {
        a.lastKnown = posOf(a.target);
        const Vec3 tp = world.nearest(a.lastKnown, o.pos);
        a.wantYaw = angleOf(tp.x - o.pos.x, -(tp.z - o.pos.z));
        if (approx(fix(tp.x - o.pos.x), fix(tp.z - o.pos.z), 0) > 0x100000) a.want = 0x30000;
        else {
            a.want = 0x20000;
            a.wantYaw += 0x18000000;
        }
    }
    a.vx = int32_t((double(a.speed) * std::cos(yawRad(a.yaw))) / 64.0);
    a.vy = int32_t((double(a.speed) * std::sin(yawRad(a.yaw))) / 64.0);
    a.think = 0x10;   // 0x42e1a4: also after the target branch's 8
}

// vult move (0x42e1c0). Alive: the think's velocity; up or down by speed
// >> 7 a tick toward 72 m (40 m with a target), at least 20 m above the
// ground, within 1 m; turn (0x42b320) and speed (0x42b3a0). Destroyed:
// down with +0xC0 -= 0x134 a tick while above the ground + 1/16 m,
// carried by the velocity; on reaching it ENV_SEAGULL_THUD (0x48) when
// faster than 0x1000 a tick, a bounce of -vz / 8 and the velocity / 4.
void WildlifeSystem::vultureMove(World& world, Animal& a) {
    WorldObject& o = world.objects[size_t(a.object)];
    if (a.dead) {
        const int32_t g = fix(world.terrain->heightAt(o.pos.x, o.pos.z)) + 0x1000;
        int32_t z = fix(o.pos.y);
        if (z >= g) { z += a.vz; a.vz -= 0x134; }
        if (z <= g) {
            if (a.vz < -0x1000 && playSound) playSound(0x48, o.pos);
            z = g;
            a.vz = -a.vz >> 3;
            a.vx >>= 2;
            a.vy >>= 2;
        } else {
            o.pos.x += unfix(a.vx);
            o.pos.z -= unfix(a.vy);
        }
        place(world, a, unfix(z));
        o.tickVel = {0, 0, 0};
        return;
    }
    o.pos.x += unfix(a.vx);
    o.pos.z -= unfix(a.vy);
    const float ext = world.extent();
    o.pos.x -= std::floor(o.pos.x / ext) * ext;
    o.pos.z -= std::floor(o.pos.z / ext) * ext;
    o.tickVel = {unfix(a.vx), 0, -unfix(a.vy)};
    int32_t want = a.target != -1 ? 0x280000 : 0x480000;
    want = std::max(want, fix(world.terrain->heightAt(o.pos.x, o.pos.z)) + 0x140000);
    int32_t z = fix(o.pos.y);
    if (z < want) z += a.speed >> 7;
    else if (z > want + 0x10000) z -= a.speed >> 7;
    place(world, a, unfix(z));
    turn(world, a);
    ramp(world, a, a.want);
}

// ---- Penguins ----------------------------------------------------------

// peng think (GOG Df2.exe 0x42f030), every 8 ticks. Destroyed: PENGUIN_DEATH
// (0x1E1, 0x42a9f0), the speed wanted 0, 0x10C. It follows its path
// (0x4033d0): a point counts within its radius or 2 |speed|; swimming and
// more than 6 m from it, the heading is redrawn with chance 1/8 (rand &
// 0x1C == 0x1C) to the point +-22.5 degrees. The ground 2 s ahead (1.5 s
// swimming) drives the water states: 0 -> 1 when it is under the water
// (+0xBC = 0x1000, a hop); 1 -> 2 when the ground here is more than 0.25 m
// under the water and the origin 1/16 m under it (0x45af50: effect 4 and
// PENGUIN_INTO_WATER, pitch 0xC8000000); 2 -> 3 when the ground ahead is
// above the water (PENGUIN_OUTOF_WTR, +0xBC = 0x1C00); 3 -> 4 on landing
// (pitch 0xC4000000); 4 -> 0 once the pitch is back to 0.
void WildlifeSystem::penguinThink(World& world, AiSystem& ai, Animal& a) {
    WorldObject& o = world.objects[size_t(a.object)];
    a.think = 8;
    if (a.dead) { a.think = 0x400; return; }
    if (o.wrecked || (o.item->hp > 0 && o.hp <= 0)) {
        if (playSound) playSound(0x1E1, o.pos);
        a.want = 0;
        a.dead = true;
        return;
    }
    if (a.pathMode == 0) a.want = 0;
    else {
        uint32_t hdg = 0;
        int32_t dist = 0, radius = 0;
        if (!steer(world, a, hdg, dist, radius)) a.pathMode = 0;
        if (a.pathMode == 0) a.want = 0;
        else {
            bool go = true;
            if (!(radius < dist && std::abs(a.speed) * 2 <= dist)) {
                if (nextPoint(world, ai, a)) steer(world, a, hdg, dist, radius);
                else go = false;
            }
            if (go) {
                if (a.state == 2 && dist > 0x60000) {
                    if ((rnd() & 0x1C) == 0x1C) {
                        hdg += (uint32_t(rnd() & 0x7FFF) + 0x3C000u) * 0x4000u;
                        if (hdg != a.yaw) a.wantYaw = hdg;
                    }
                } else if (hdg != a.yaw) a.wantYaw = hdg;
                a.want = 0x8000;
            }
        }
    }
    // The ground ahead: speed x2 (x1.5 swimming) along the yaw.
    const float k = a.state == 2 ? 1.5f : 2.f, s = unfix(a.speed) * k;
    const float ahead = world.terrain->heightAt(o.pos.x + s * std::cos(yawRad(a.yaw)), o.pos.z - s * std::sin(yawRad(a.yaw)));
    const int32_t ga = fix(ahead), water = fix(world.terrain->waterLevel()), z = fix(o.pos.y);
    const int32_t here = fix(world.terrain->heightAt(o.pos.x, o.pos.z));
    auto splash = [&](int sound) {
        if (spawnEffect) spawnEffect(4, o.pos);
        if (playSound) playSound(sound, o.pos);
    };
    switch (a.state) {
    case 0:
        if (ga < water) { a.state = 1; a.vz = 0x1000; }
        break;
    case 1:
        if (here < water - 0x4000 && z <= water - 0x1000) { splash(0x4A); a.state = 2; a.pitch = int32_t(0xC8000000u); }
        break;
    case 2:
        if (ga > water) { splash(0x4B); a.state = 3; a.vz = 0x1C00; }
        break;
    case 3:
        if (a.vz <= 0 && z <= here + 0x1000) { a.state = 4; a.pitch = int32_t(0xC4000000u); }
        break;
    case 4:
        if (a.pitch >= 0) a.state = 0;
        if (ga < water) { a.state = 1; a.vz = 0x1000; }
        break;
    default: break;
    }
    a.losTimer += a.think;
}

// peng move (0x42f430): 0 on land 0.5 m/s (the speed ramps there whatever
// the think wants), turning, 1/16 m above the ground, level; 1 diving
// 2.25 m/s, falling by +0xBC (-0x134 a tick, stopped 1/16 m under the
// water), nose down 0x2000000 a tick; 2 swimming toward 2.25 m/s, turning,
// at its depth, pitch 0xC8000000; 3 out 3 m/s, +0xBC (-0x9a a tick) onto
// the ground, pitch 0xE0000000; 4 landing toward 0.5 m/s, 1/8 m above the
// ground, the nose up 0xC000000 a tick to level. The waddle roll follows
// the phase +0xB4. Destroyed: it tips over, the roll +0x3000000 a tick to
// 0x40000000.
void WildlifeSystem::penguinMove(World& world, Animal& a) {
    WorldObject& o = world.objects[size_t(a.object)];
    if (a.dead) {
        a.phase = 0;
        if (uint32_t(a.roll) < 0x40000000u) a.roll += 0x3000000;
        o.tickVel = {0, 0, 0};
        return;
    }
    switch (a.state) {
    case 0:
        ramp(world, a, 0x8000);
        step(world, a);
        turn(world, a);
        place(world, a, unfix(fix(world.terrain->heightAt(o.pos.x, o.pos.z)) + 0x1000));
        a.phase = int32_t(uint32_t(a.phase) + (uint32_t(a.speed + 0x4000) << 4));   // wraps as the EXE's add
        a.pitch = 0;
        break;
    case 1: {
        a.speed = 0x24000;
        step(world, a);
        const int32_t z = fix(o.pos.y) + a.vz;
        a.vz -= 0x134;
        place(world, a, unfix(z));
        if (z <= fix(world.terrain->waterLevel()) - 0x1000) a.vz = 0;
        a.pitch = int32_t(uint32_t(a.pitch) + 0xFE000000u);
        break;
    }
    case 2:
        ramp(world, a, 0x24000);
        step(world, a);
        turn(world, a);
        a.phase = int32_t(uint32_t(a.phase) + uint32_t(8 * a.speed + 0x40000));
        a.pitch = int32_t(0xC8000000u);
        break;
    case 3: {
        a.speed = 0x30000;
        step(world, a);
        int32_t z = fix(o.pos.y) + a.vz;
        a.vz -= 0x9A;
        const int32_t g = fix(world.terrain->heightAt(o.pos.x, o.pos.z));
        if (z <= g) {
            if (a.vz < 0) a.vz = 0;
            z = g;
        }
        place(world, a, unfix(z));
        a.pitch = int32_t(0xE0000000u);
        break;
    }
    case 4:
        ramp(world, a, 0x8000);
        step(world, a);
        place(world, a, unfix(fix(world.terrain->heightAt(o.pos.x, o.pos.z)) + 0x2000));
        if (a.speed <= 0x8000 && a.pitch < 0) a.pitch = int32_t(uint32_t(a.pitch) + 0xC000000u);
        if (a.pitch > 0) a.pitch = 0;
        break;
    default: break;
    }
    const int32_t e = (a.phase - 0x400000) & 0x1FF0000;
    a.roll = e > 0x1000000 ? -(0x1800000 - e) * 8 : -(e - 0x800000) * 8;
}

// ---- Fish / piranhas ---------------------------------------------------

// prnh think (GOG Df2.exe 0x42db20), every 8 ticks. Destroyed: 0x10C and
// the speed wanted 0, then it rises to just under the surface
// (0x42d870(water - 1/256 m, 1/256, damped)) every 0x10 ticks. A target is
// kept only while it swims (+0x64 & 0x20000). Without one, unless AI flag 1,
// the scan (0x42ac80) drawn against rand takes one (spotted 0x41ac00, a
// pause ai+0xA0 of 32..95, its position); otherwise the path at 1 m/s with
// a tail flick (+0xC0 = 0x100) with chance 1/32, 0.5 m under the surface
// (0x42d870(water - 0.5, 0.25, 1)). With a target: the attack (0x42d920).
void WildlifeSystem::fishThink(World& world, AiSystem& ai, Animal& a, const AiSystem::PlayerView& player, bool playerSwimming) {
    WorldObject& o = world.objects[size_t(a.object)];
    const int32_t water = fix(world.terrain->waterLevel());
    int cand = a.target;   // iVar3: ai+0x54 at the start, then the scan's result
    a.think = 8;
    if (a.dead) {
        depth(world, a, water - 0x100, 0x100, false);
        a.think = 0x10;
        return;
    }
    if (o.wrecked || (o.item->hp > 0 && o.hp <= 0)) {
        a.want = 0;
        a.dead = true;
        return;
    }
    auto swims = [&](int t) { return t == -3 ? playerSwimming : world.objects[size_t(t)].swimming; };
    bool attack = false;
    if (a.target != -1) {
        if (swims(a.target)) attack = true;
        else a.target = -1;
    }
    if (!attack) {
        bool took = false;
        if (!(a.aiFlags & 1)) {
            // 0x42ac80: people of any side but 0 (the player too) whose
            // origin is at most 1 m above the water, in the field of view
            // (ai+0xB0), within 99 m (whole metres), in sight from 1 m up (1..3):
            // 100 / d x (fov - |angle|) / fov x the move's visibility x 6
            // when swimming (else / 256); the best x perception, / 4 relaxed.
            int best = -1;
            int32_t bestScore = 0;
            const Vec3 eye = o.pos + Vec3{0, 1.f, 0};
            auto consider = [&](int id, Vec3 at, int side, int move, bool swimming) {
                if (side == 0 || fix(at.y) > water + 0x10000) return;
                const Vec3 tp = world.nearest(at, o.pos);
                uint32_t u = (angleOf(tp.x - o.pos.x, -(tp.z - o.pos.z)) - a.yaw) >> 24;
                if (u > 0x7F) u = 0x100 - u;
                if (int(u) > a.fov) return;
                const int32_t d = approx(fix(tp.x - o.pos.x), fix(tp.z - o.pos.z), fix(tp.y - o.pos.y)) >> 16;
                int32_t s = 0x640000;
                if (d != 0) {
                    if (d > 99) return;
                    s = 0x640000 / d;
                }
                const int r = ai.sightLine(world, eye, tp, a.object, id);
                if (r < 1 || r > 3) return;
                const int32_t f = int32_t((uint32_t(a.fov - int(u)) << 16) / uint32_t(a.fov));
                int32_t score = mul16(s, f);
                score = mul16(score, fix(moveVisibility(move)));
                score = mul16(score, swimming ? 0x60000 : 0x100);
                if (bestScore < score) { best = id; bestScore = score; }
            };
            for (size_t i = 0; i < world.objects.size(); ++i) {
                const WorldObject& q = world.objects[i];
                if (q.person && !q.removed) consider(int(i), q.pos, q.side, q.moveNumber, q.swimming);
            }
            consider(-3, player.origin, player.side, player.moveNumber, playerSwimming);
            cand = best;
            int32_t score = mul16(bestScore, fix(a.perception));
            if (a.alert == 0) score >>= 2;
            if (best != -1 && int32_t(rnd()) < score) {
                ai.markSpotted(o.team, best == -3 ? player.team : world.objects[size_t(best)].team);
                a.target = best;
                a.pause = int(rnd() & 0x3F) + 0x20;
                a.lastKnown = best == -3 ? player.origin : world.objects[size_t(best)].pos;
                took = true;
            }
        }
        if (!took) {
            bool done = false;
            if (a.pathMode != 0) {
                uint32_t hdg = 0;
                int32_t dist = 0, radius = 0;
                if (!steer(world, a, hdg, dist, radius)) a.pathMode = 0;
                if (a.pathMode != 0) {
                    done = true;
                    bool go = true;
                    if (dist <= radius || dist < std::abs(a.speed) * 2) {
                        if (nextPoint(world, ai, a)) steer(world, a, hdg, dist, radius);
                        else go = false;
                    }
                    if (go) {
                        if (hdg != a.yaw) a.wantYaw = hdg;
                        a.want = 0x10000;
                        if ((rnd() & 0x1F) == 0) a.state = 0x100;
                    }
                }
            }
            if (!done) a.want = 0;
        }
    }
    if (cand == -1) {
        depth(world, a, water - 0x8000, 0x4000, true);
        a.losTimer += a.think;
        return;
    }
    fishAttack(world, ai, a, player);
    a.losTimer += a.think;
    if (a.pause > 0) a.pause -= a.think;
}

// The attack (0x42d920): the sight line from 1 m up re-checked once ai+0x50
// passes 0xBA (spotted while it holds); while seen the last known point
// follows the target. The jaw +0xC0 opens by 0x20 up to 0x70 within 4 m and
// 90 degrees; the wanted yaw is the target's +-0x11000000 (by AI flag 4).
// Within 0.75 m, once the pause is out: a bite with chance 1/8 (blast kind 4
// at the midpoint, 0x454b20: 1 hp from every person within 2 m; the pause
// 0x100), and every time effect 0x10 with ENV_PIRANHA_BITE (0x49, 0x45af50)
// there; the speed -4 m/s. The depth follows the target, no higher than
// 0.5 m under the surface.
void WildlifeSystem::fishAttack(World& world, AiSystem& ai, Animal& a, const AiSystem::PlayerView& player) {
    WorldObject& o = world.objects[size_t(a.object)];
    if (a.target == -1) return;
    const Vec3 tp = world.nearest(a.target == -3 ? player.origin : world.objects[size_t(a.target)].pos, o.pos);
    bool update = a.seen;
    if (a.losTimer >= 0xBB) {
        a.losTimer = 0;
        const int r = ai.sightLine(world, o.pos + Vec3{0, 1.f, 0}, tp, a.object, a.target);
        a.seen = r >= 1;
        update = a.seen;
        if (a.seen) ai.markSpotted(o.team, a.target == -3 ? player.team : world.objects[size_t(a.target)].team);
    }
    if (update) a.lastKnown = tp;
    const Vec3 lk = world.nearest(a.lastKnown, o.pos);
    const int32_t d = approx(fix(lk.x - o.pos.x), fix(lk.z - o.pos.z), fix(lk.y - o.pos.y));
    uint32_t h = angleOf(lk.x - o.pos.x, -(lk.z - o.pos.z));
    if (d < 0x40000 && std::abs(int64_t(int32_t(h - a.yaw))) < 0x40000000 && a.state < 0x70) a.state += 0x20;
    h += (a.aiFlags & 4) ? uint32_t(0) - 0x11000000u : 0x11000000u;
    a.wantYaw = h;
    if (d < 0xC000) {
        if (a.pause < 1) {
            const Vec3 mid = (tp + o.pos) * 0.5f;
            if ((rnd() & 7) == 0) {
                // 0x455200 kind 4 (radius 1 m): people within 2 m (0x41e880)
                // of the point, alive, not the fish: hp - 1, the fish the
                // killer, the hit move 0x417170(.., 2, dir, 1) (0xEA in the water).
                for (size_t j = 0; j < world.objects.size(); ++j) {
                    WorldObject& q = world.objects[j];
                    if (!q.person || !q.alive()) continue;
                    const Vec3 qp = world.nearest(q.pos, mid);
                    if (approx(fix(qp.x - mid.x), fix(qp.z - mid.z), fix(qp.y - mid.y)) > 0x20000) continue;
                    world.damage(int(j), 1, 0x7fff, world.deathMove(int(j), 1, 2, mid), a.object);
                    ai.hurt(world, int(j));
                }
                const Vec3 pp = world.nearest(player.origin, mid);
                if (player.alive && bitePlayer && approx(fix(pp.x - mid.x), fix(pp.z - mid.z), fix(pp.y - mid.y)) <= 0x20000) bitePlayer(mid);
                a.pause = 0x100;
            }
            if (spawnEffect) spawnEffect(0x10, mid);
            if (playSound) playSound(0x49, mid);
        }
        a.speed = int32_t(0xFFFC0000u);
    }
    const int32_t water = fix(world.terrain->waterLevel());
    depth(world, a, std::min(fix(tp.y), water - 0x8000), 0x4000, true);
}

// prnh move (0x42de00): z += +0xB0, +0xB0 += +0xB4, the pitch -vz << 18.
// Destroyed: belly up, the roll +0x1000000 a tick to 0x40000000, no
// travel. Alive: the jaw / tail +0xC0 closes (by 2 above 0xE0, else by 8
// down to 0x60 while the target is ahead, to 0 otherwise); the speed toward
// +0x9C + (+0xC0 << 12) (a flick is a burst), the step, the turn (x4 with
// a target).
void WildlifeSystem::fishMove(World& world, Animal& a) {
    WorldObject& o = world.objects[size_t(a.object)];
    place(world, a, unfix(int64_t(fix(o.pos.y)) + a.vz));
    a.vz += a.az;
    a.pitch = int32_t(uint32_t(-a.vz) << 18);
    if (a.dead) {
        if (uint32_t(a.roll) < 0x40000000u) a.roll += 0x1000000;
        o.tickVel = {0, 0, 0};
        return;
    }
    const bool ahead = a.target != -1 && std::abs(int64_t(int32_t(a.wantYaw - a.yaw))) < 0x40000000;
    if (a.state > 0xE0) a.state -= 2;
    else if (a.state > (ahead ? 0x60 : 0)) a.state -= 8;
    ramp(world, a, a.want + (a.state << 12));
    step(world, a);
    turn(world, a, a.target != -1 ? 4 : 1);
}

} // namespace df
