#include "Ai.h"

#include "Moves.h"
#include "Sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace df {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kWaypointA = 6005, kWaypointB = 6006;   // ITEMS.DEF 106005 / 106006

// DF.EXE's 2D distance for AI (0x459854): max(|dx|, |dy|) + min / 2.
float octagonal(Vec3 a, Vec3 b) {
    const float dx = std::abs(a.x - b.x), dz = std::abs(a.z - b.z);
    return std::max(dx, dz) + std::min(dx, dz) * 0.5f;
}

float len3(Vec3 v) { return std::sqrt(dot(v, v)); }

} // namespace

void AiSystem::init(World& world, const Mission& mission) {
    mission_ = &mission;
    people_.clear();
    noise_.clear();
    teamAlert_.fill(0);
    spotted_.fill(0);
    engaging_.fill(false);
    reached_.clear();
    teamSpeed_.fill(0);
    ticks_ = 0;
    playerFired_ = false;
    int stagger = 0;
    for (size_t i = 0; i < world.objects.size(); ++i) {
        WorldObject& o = world.objects[i];
        if (!o.person || !o.item || o.item->aiFunction != "org1" || o.missionIndex < 0) continue;
        const MissionObject& mo = mission.objects[size_t(o.missionIndex)];
        const auto* rec = reinterpret_cast<const uint8_t*>(mo.raw);
        auto u16 = [&](int off) { return int(rec[off] | rec[off + 1] << 8); };
        auto i32 = [&](int off) { return mo.raw[off / 4]; };
        Person p;
        p.object = int(i);
        // ai+0x24 from record +0xC (0x440109): bit0 -> 1, bit1 -> 2, bit2 -> 8,
        // bit3 -> 4, bit8 -> 0x10, bit9 -> 0x20; bit10 -> silent.
        const uint32_t f = uint32_t(i32(0xC));
        p.flags = (f & 1 ? 1u : 0u) | (f & 2 ? 2u : 0u) | (f & 4 ? 8u : 0u) | (f & 8 ? 4u : 0u) | (f & 0x100 ? 0x10u : 0u) |
                  (f & 0x200 ? 0x20u : 0u);
        p.silent = f & 0x400;
        if (df2) {
            // DF2.EXE 0x40374a: record bits 0x800/0x1000/0x2000/0x4000 -> ai
            // 0x200 attack everyone, 0x400 (movement, unclear), 0x800 avoid
            // water, 0x1000 deaf; +0x54 x 62 the unseen limit (default 60 s).
            p.flags |= (f & 0x800 ? 0x200u : 0u) | (f & 0x1000 ? 0x400u : 0u) | (f & 0x2000 ? 0x800u : 0u) | (f & 0x4000 ? 0x1000u : 0u);
            p.unseenLimit = (i32(0x54) > 0 ? i32(0x54) : 60) * 62;
        }
        p.accuracy = i32(0x20) > 0 ? float(i32(0x20)) : 100.f;
        p.perception = (i32(0x24) > 0 ? float(i32(0x24)) : 100.f) / 100.f;
        if (df2 && night && p.perception > 0.3f) p.perception -= 0.15f;   // DF2: night lowers it
        p.closeRange = i32(0x2C) > 0 ? float(i32(0x2C)) : 16.f;
        p.engageRadius = i32(0x30) > 0 ? float(i32(0x30)) : 1024.f;
        p.fov = u16(0x38) ? float(u16(0x38)) : 180.f;
        p.advanceTicks = (u16(0x3A) ? u16(0x3A) : 5) * 62;
        p.holdTicks = (u16(0x3C) ? u16(0x3C) : 5) * 62;
        p.giveUpTicks = (u16(0x40) ? u16(0x40) : 300) * 62;
        p.alert = std::clamp(int(rec[0x42]), 0, 2);
        p.weapon = aiWeaponMode(rec[0x46]);
        p.path = rec[0x49];
        p.pathMode = p.path ? 1 : 0;
        p.waypoint = rec[0x4A];
        p.flank = rec[0x4B] ? float(rec[0x4B]) : 45.f;
        p.homeHeading = o.heading;
        // Staggered first thinks: counter % 60, the counter +11 per person.
        p.think = 1 + stagger % 60;
        stagger += 11;
        p.lastHp = o.hp;
        if (p.alert == 2 && o.team >= 0 && o.team < 64) teamAlert_[size_t(o.team)] = 2;
        o.weaponMode = p.weapon;
        people_.push_back(p);
        world.requestMove(o, idleMove(world, p));
    }
    if (std::getenv("OPENDF_DEBUGAI")) std::fprintf(stderr, "ai: %zu soldiers\n", people_.size());
}

AiSystem::Person* AiSystem::byObject(int object) {
    for (auto& p : people_)
        if (p.object == object) return &p;
    return nullptr;
}

float AiSystem::headingTo(Vec3 from, Vec3 to) const {
    return std::atan2(to.x - from.x, -(to.z - from.z));   // clockwise from north
}

// Idle / stopped move (0x45ac38): relaxed or cautious stand; alert: prone
// (0x20), standing ready (0x10) or crouched. Two-handed when the weapon's
// mode flags have 0x4.
int AiSystem::idleMove(const World& world, const Person& p) const {
    (void)world;
    const bool two = modeInfo(p.weapon).flags & 4;
    if (p.alert != 2) return two ? 2 : 1;
    if (p.flags & 0x20) return two ? 111 : 110;
    if (p.flags & 0x10) return two ? 11 : 10;
    return two ? 101 : 100;
}

// Moving (0x45abd0): running when alert, else walking.
int AiSystem::walkMove(const World& world, const Person& p) const {
    (void)world;
    const bool two = modeInfo(p.weapon).flags & 4;
    if (p.alert == 2) return two ? 68 : 60;
    return two ? 28 : 20;
}

Vec3 AiSystem::targetPos(const World& world, const Person& p, const PlayerView& player) const {
    if (p.target == -3) return player.origin;
    if (p.target >= 0) return world.objects[size_t(p.target)].pos;
    return p.lastKnown;
}

bool AiSystem::targetAlive(const World& world, const Person& p, const PlayerView& player) const {
    if (p.target == -3) return player.alive;
    return p.target >= 0 && world.objects[size_t(p.target)].alive();
}


void AiSystem::shotFired(Vec3 at, float range, int side, int object) {
    // 0x46c228: a box of +-r at the shot (up to 200), flags 2 = a gunshot.
    if (noise_.size() < 200) noise_.push_back({at, range, side, 2});
    if (object == -3) playerFired_ = true;
    else if (Person* p = byObject(object)) p->fired = true;
}

void AiSystem::hurt(World& world, int object) {
    // 0x44f218: the victim becomes alert, its team too; it thinks at once.
    Person* p = byObject(object);
    if (!p) return;
    p->alert = 2;
    alertTeam(world.objects[size_t(object)].team);
    p->think = 1;
}

void AiSystem::bark(World& world, const Person& p, SoundSystem& sound, int slot) {
    // 0x473f70; silent soldiers (ai+0x25) skip barks 3, 4 and 5.
    if (p.silent && slot >= 3 && slot <= 5) return;
    const WorldObject& o = world.objects[size_t(p.object)];
    if (o.item) sound.voice(o.item->phraseSet, slot, o.pos);
}

// Target score (0x46a804): field of view, 100 m / distance, line of sight,
// the target's move visibility, x6 if it fired lately; the best score x
// perception (>> 2 when relaxed) against a random draw.
float AiSystem::bestTarget(const World& world, int selfId, Vec3 eye, float heading, int side, float half, const PlayerView& player,
                           int& bestId, Vec3& bestPos, bool everyone) const {
    float best = 0;
    bestId = -1;
    struct { float heading; int side; } self{heading, side};
    auto consider = [&](int id, Vec3 at, int side, int move, bool fired) {
        if (std::getenv("OPENDF_DEBUGAI2")) std::fprintf(stderr, "scan %d side %d cand %d side %d\n", selfId, self.side, id, side);
        // DF2 ai flag 0x200: everyone is a target, own side and side 0 too.
        if (!everyone && (side == 0 || side == self.side)) return;
        const Vec3 tp = world.nearest(at, eye);
        float diff = headingTo(eye, tp) - self.heading;
        diff -= std::floor(diff / (2 * kPi) + 0.5f) * 2 * kPi;
        if (std::abs(diff) > half) return;
        const float d = len3(tp - eye);
        if (d >= 1000.f || d < 0.01f) return;
        if (std::getenv("OPENDF_DEBUGAI2")) std::fprintf(stderr, "  in fov, d %.1f\n", d);
        // Visible: 1 or 2, i.e. at most one see-through face (0x46a804).
        if (const int r = sightLine(world, eye, tp, selfId, id); r < 1 || r > 2) return;
        if (std::getenv("OPENDF_DEBUGAI2")) std::fprintf(stderr, "  visible\n");
        float vis = moveVisibility(move);
        // DF2's ghillie suit (0x415a69): the player in it (kit 32), crouched
        // or prone (visibility <= 0.5) and not firing lately: unseen beyond
        // 50 m, half as visible within.
        if (df2 && id == -3 && playerGear == 32 && vis <= 0.5f && !fired) {
            if (d > 50.f) return;
            vis *= 0.5f;
        }
        float score = (half - std::abs(diff)) / half * (100.f / d) * vis * (fired ? 6.f : 1.f);
        if (score > best) best = score, bestId = id, bestPos = tp;
    };
    if (player.alive) consider(-3, player.origin, player.side, player.moveNumber, playerFired_);
    for (size_t i = 0; i < world.objects.size(); ++i) {
        const WorldObject& o = world.objects[i];
        if (!o.person || !o.alive() || int(i) == selfId) continue;
        const Person* q = nullptr;
        for (const auto& x : people_)
            if (x.object == int(i)) q = &x;
        consider(int(i), o.pos, o.side, o.moveNumber, q && q->fired);
    }
    return best;
}

bool AiSystem::scan(World& world, Person& p, const PlayerView& player) {
    const WorldObject& self = world.objects[size_t(p.object)];
    const Vec3 eye = self.pos + Vec3{0, 1.f, 0};
    const float half = float((int(p.fov * 65536.f / 360.f) >> 9) | 1) / 256.f * 2 * kPi;   // 1/256 turns
    int bestId = -1;
    Vec3 bestPos{0, 0, 0};
    // DF2: ai flag 1 blocks acquisition entirely.
    if (df2 && (p.flags & 1)) return false;
    const float best = bestTarget(world, p.object, eye, self.heading, self.side, half, player, bestId, bestPos, df2 && (p.flags & 0x200));
    float score = best * p.perception * ((hard && self.side == 2) ? 2.f : 1.f);
    if (p.alert == 0) score *= 0.25f;   // only when relaxed (0x46a896)
    if (bestId == -1 || float(rnd()) >= score * 65536.f) return false;
    if (std::getenv("OPENDF_DEBUGAI"))
        std::fprintf(stderr, "ai %d (team %d) spots %d at %.0f m, score %.3f\n", p.object, self.team, bestId, len3(bestPos - eye), score);
    p.target = bestId;
    p.lastKnown = bestPos;
    // DF2 +0xAC: the player's distance at this scan (the scan interval).
    if (df2 && bestId == -3) p.nearestPlayer = len3(bestPos - eye);
    p.unseen = 0;
    p.seen = 0;
    p.flags |= 0x80;
    p.flags &= ~0x40u;
    return true;
}

// Path walking (0x459854): head straight for the point; on arrival mark it
// reached and take the next; a one-way path stops at its end facing the
// last point's heading.
void AiSystem::patrol(World& world, Person& p) {
    WorldObject& o = world.objects[size_t(p.object)];
    if (!mission_ || p.path <= 0 || p.path >= int(mission_->paths.size())) { p.pathMode = 0; return; }
    const MissionPath& path = mission_->paths[size_t(p.path)];
    if (path.points.empty()) { p.pathMode = 0; return; }
    p.waypoint = std::clamp(p.waypoint, 0, int(path.points.size()) - 1);
    const MissionObject* wp = mission_->inGroup(2, path.points[size_t(p.waypoint)]);
    if (!wp) { p.pathMode = 0; return; }
    const float ext = world.extent();
    Vec3 at{std::fmod(std::fmod(wp->x, ext) + ext, ext), 0, std::fmod(std::fmod(-wp->y, ext) + ext, ext)};
    at = world.nearest(at, o.pos);
    float radius = float(wp->raw[7]);
    if (radius <= 0) radius = wp->type == kWaypointB ? 1.f : 0.5f;
    const float d = octagonal(o.pos, at);
    if (d <= radius) {
        reached_[o.team * 128 + p.path] |= 1u << (p.waypoint & 31);   // 0x45c940
        ++p.waypoint;
        if (p.waypoint >= int(path.points.size())) {
            p.waypoint = 0;
            if (path.flags & 1) {
                world.requestMove(o, idleMove(world, p));
                p.pathMode = 0;
                p.path = 0;
                o.heading = wp->heading * kPi / 180.f;
                p.think = 16;
                return;
            }
        }
        world.requestMove(o, walkMove(world, p));
        p.think = 8;
        return;
    }
    o.heading = headingTo(o.pos, at);   // no turn rate: the heading is set (0x459854)
    world.requestMove(o, walkMove(world, p));
    p.think = d > 8.f ? 64 : std::max(1, int(d * 8.f));
}

void AiSystem::fire(World& world, Person& p, Arsenal& shots, SoundSystem& sound, const PlayerView& player, bool followUp) {
    WorldObject& o = world.objects[size_t(p.object)];
    const ModeDef& md = shots.modeAt(p.weapon);
    if (md.type != 1) return;
    // Muzzle (0x46ad30: the held weapon at the right hand). APPROXIMATION:
    // 0.5 m ahead of the body origin, 0.4 m up.
    const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)};
    Vec3 muzzle = o.pos + fwd * 0.5f + Vec3{0, 0.4f, 0};
    if (this->muzzle) this->muzzle(p.object, o.weaponMode, muzzle);
    if (std::getenv("OPENDF_DEBUGMUZZLE"))
        std::fprintf(stderr, "muzzle %d mode %d off (%.2f %.2f %.2f) heading %.0f\n", p.object, o.weaponMode, muzzle.x - o.pos.x, muzzle.y - o.pos.y,
                     muzzle.z - o.pos.z, o.heading * 57.3f);
    Vec3 dir;
    if (followUp) {
        dir = p.burstDir;   // the burst keeps the first round's aim
    } else {
        // 0x46b838: aim at the last known position; error x errMul: 5 unseen,
        // else 16 - seconds of sight, down to the target move's aim factor.
        const Vec3 aim = world.nearest(p.lastKnown, muzzle) - muzzle;
        float yaw = std::atan2(aim.x, -aim.z);
        float pitch = std::atan2(aim.y, std::sqrt(aim.x * aim.x + aim.z * aim.z));
        int move = 2;
        if (p.target == -3) move = player.moveNumber;
        else if (p.target >= 0) move = world.objects[size_t(p.target)].moveNumber;
        const float f = moveAimFactor(move);
        const float seen = std::min(float(p.seen), (16.f - f) * 62.f);
        const float errMul = (p.flags & 0x80) ? std::max(f, 16.f - seen / 62.f) : 5.f;
        const float unit = float(int(65536.f * 100.f / p.accuracy) * 30 >> 16);
        const float spread = unit * errMul * 65536.f / 4294967296.f * 2 * kPi / ((hard && o.side == 2) ? 4.f : 1.f);
        auto r = [&]() { return float(int(rnd()) - 0x8000) / 32768.f; };
        yaw += r() * spread;
        pitch += r() * spread / 4.f;
        // The gun's own spread (0x46af90).
        const GunDef& g = shots.gunAt(md.gun);
        if (g.accuracy > 1) {
            const float dev = 30.f * 0x8000 / 4294967296.f * 2 * kPi * float(g.accuracy - 1);
            yaw += r() * dev;
            pitch += r() * dev;
        }
        dir = {std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
        p.burstDir = dir;
    }
    const GunDef& g = shots.gunAt(md.gun);
    Projectile pr;
    pr.pos = muzzle;
    pr.vel = dir * (float(g.velocity) * 60.f / 62.f);
    pr.lifetime = g.lifetime;
    pr.power = g.power;
    pr.falloff = std::max(1, g.falloff);
    pr.flags = g.flags;
    pr.from = o.pos;
    pr.shooter = p.object;
    if (shots.df2()) pr.gun = md.gun;   // DF2: drag and energy damage by the ammo record
    shots.addProjectile(pr);
    if (std::getenv("OPENDF_DEBUGAI"))
        std::fprintf(stderr, "ai %d fires mode %d at target %d (%s)\n", p.object, p.weapon, p.target, followUp ? "burst" : "aimed");
    // The shot sound (0x44d5fe) and its noise (radius = the sound's range / 2).
    static const int burstGuns[][2] = {{6, 8}, {8, 8}, {21, 5}, {35, 23}, {36, 25}};
    int soundGun = md.gun;
    bool silentRound = false;
    for (const auto& bg : burstGuns)
        if (bg[0] == p.weapon) { soundGun = bg[1]; silentRound = followUp; }
    const int id = shots.gunAt(soundGun).sounds[0];
    if (id > 0 && !silentRound) {
        sound.play(id, muzzle);
        shotFired(muzzle, sound.range(id) * 0.5f, o.side, p.object);
    }
    if (!followUp && md.burst) p.burstLeft = 2;
    p.think = std::max(1, md.cooldown);
}

void AiSystem::think(World& world, Person& p, Arsenal& shots, SoundSystem& sound, const PlayerView& player) {
    WorldObject& o = world.objects[size_t(p.object)];
    const int elapsed = p.delay;   // 0x459800 adds the delay to the timers
    p.stateTimer += elapsed;
    p.scanTimer += elapsed;
    p.radioTimer += elapsed;
    p.losTimer += elapsed;
    p.think = 64;
    if (p.burstLeft > 0) {
        --p.burstLeft;
        fire(world, p, shots, sound, player, true);
        return;
    }
    const Vec3 eye = o.pos + Vec3{0, 1.f, 0};

    if (p.target == -1) {
        if (p.alert != 2) {
            // Hearing (0x46c2c4): inside any noise box -> alert, face it
            // (DF2: not with ai flag 0x1000, deaf).
            for (const auto& n : noise_) {
                if (df2 && (p.flags & 0x1000)) break;
                const Vec3 c = world.nearest(n.at, o.pos);
                if (std::abs(c.x - o.pos.x) <= n.r && std::abs(c.z - o.pos.z) <= n.r && std::abs(c.y - o.pos.y) <= n.r) {
                    if (std::getenv("OPENDF_DEBUGAI")) std::fprintf(stderr, "ai %d hears a shot\n", p.object);
                    p.alert = 2;
                    alertTeam(o.team);
                    o.heading = headingTo(o.pos, c);
                    bark(world, p, sound, 2);
                    break;
                }
            }
            // A dead friend within 50 m in sight alerts (bark 1, ATTACK).
            if (p.alert != 2)
                for (size_t qi = 0; qi < world.objects.size(); ++qi)
                    if (const auto& q = world.objects[qi]; q.person && q.dead && q.side == o.side && octagonal(q.pos, o.pos) < 50.f &&
                        sightLine(world, o.pos, q.pos, p.object, int(qi)) >= 1 && sightLine(world, o.pos, q.pos, p.object, int(qi)) <= 2) {
                        p.alert = 2;
                        alertTeam(o.team);
                        bark(world, p, sound, 1);
                        break;
                    }
            // Scan every 0x174 ticks (relaxed) or 0xf8 (cautious).
            // DF2: by +0xAC, the player's distance at the last successful scan:
            // under 32 m 62 ticks, under 100 m 124, else 372.
            const int interval = df2 ? (p.nearestPlayer < 32.f ? 62 : p.nearestPlayer < 100.f ? 124 : 372) : (p.alert == 0 ? 0x174 : 0xf8);
            if (p.scanTimer >= interval) {
                p.scanTimer = 0;
                if (scan(world, p, player)) {
                    bark(world, p, sound, 3);
                    spotted_[size_t(o.team & 63)] |= 1ull << ((p.target == -3 ? player.team : world.objects[size_t(p.target)].team) & 63);
                    engaging_[size_t(o.team & 63)] = true;
                    p.alert = 2;
                    alertTeam(o.team);
                    p.state = 2;
                    p.stateTimer = 0;
                    p.think = 8;
                    return;
                }
            }
        } else if (p.scanTimer >= (df2 ? (p.nearestPlayer < 32.f ? 62 : p.nearestPlayer < 100.f ? 124 : 248) : 0xf8)) {
            p.scanTimer = 0;
            if (scan(world, p, player)) {
                p.state = 2;
                p.stateTimer = 0;
                p.think = 8;
                return;
            }
        }
        if (p.pathMode) {
            patrol(world, p);
            return;
        }
        if (p.alert == 2) {
            // No target: turn toward an enemy shot (0x46c35c), else keep
            // turning 0x800000 (0.7 degrees) per think.
            bool heard = false;
            for (const auto& n : noise_)
                if (n.side != o.side && (n.flags & 2)) {
                    const Vec3 c = world.nearest(n.at, o.pos);
                    if (std::abs(c.x - o.pos.x) <= n.r && std::abs(c.z - o.pos.z) <= n.r) {
                        o.heading = headingTo(o.pos, c);
                        heard = true;
                        break;
                    }
                }
            if (!heard && !df2) o.heading += float(0x800000) / 4294967296.f * 2 * kPi;   // DF2 no longer turns
        }
        world.requestMove(o, idleMove(world, p));
        return;
    }

    // Has a target.
    engaging_[size_t(o.team & 63)] = true;   // 0x45c918
    if (!targetAlive(world, p, player)) {
        // 0x40: the target is dead; GOTONE if this soldier killed it.
        p.flags |= 0x40;
        if (p.target >= 0 && world.objects[size_t(p.target)].killer == p.object) bark(world, p, sound, 5);
        p.target = -1;
        p.state = 0;
        o.aimPitch = 0;
        world.requestMove(o, idleMove(world, p));
        p.think = 16;
        return;
    }
    const Vec3 tp = world.nearest(targetPos(world, p, player), o.pos);
    if (p.losTimer > 0xba) {
        p.losTimer = 0;
        if (sightLine(world, eye, tp + Vec3{0, 0.5f, 0}, p.object, p.target) >= 1) {   // 0x459aad: any visible result
            p.flags |= 0x80;
            p.lastKnown = tp;
            p.unseen = 0;
            spotted_[size_t(o.team & 63)] |= 1ull << ((p.target == -3 ? player.team : world.objects[size_t(p.target)].team) & 63);
        } else {
            p.flags &= ~0x80u;
        }
    }
    if (p.flags & 0x80) p.seen += elapsed;
    else { p.unseen += elapsed; p.seen = 0; }
    if (p.unseen > p.giveUpTicks) {
        p.target = -1;
        p.state = 0;
        return;
    }
    // Radio (0x46b600) every 0x1f0 ticks: same-side people within 50 m
    // without a target take this one (bark 4, WARN).
    if (p.radioTimer > 0x1f0 && !(df2 && p.silent)) {   // DF2: silent soldiers never radio
        p.radioTimer = 0;
        bool told = false;
        for (auto& q : people_) {
            if (&q == &p || q.target != -1 || (df2 && (q.flags & 0x1000))) continue;   // DF2: deaf ones hear no radio
            const WorldObject& qo = world.objects[size_t(q.object)];
            if (!qo.alive() || qo.side != o.side || octagonal(qo.pos, o.pos) > 50.f || p.unseen >= q.giveUpTicks) continue;
            q.target = p.target;
            q.lastKnown = p.lastKnown;
            q.alert = 2;
            q.state = 1;
            alertTeam(qo.team);
            told = true;
        }
        if (told) bark(world, p, sound, 4);
    }
    const float dist = octagonal(o.pos, p.lastKnown);
    if (df2 && p.order != 0) p.state = 2;   // DF2: any order holds position (engage)
    if (p.pathMode && dist > p.engageRadius && !(df2 && p.order != 0)) {
        patrol(world, p);
        return;
    }
    if (p.state != 2) {
        // Advance (state 1): toward the last known position, flanked by
        // ai+0x7c to the side of flags 4 / 8, at the walk / run move.
        p.state = 1;
        float h = headingTo(o.pos, world.nearest(p.lastKnown, o.pos));
        const float flank = p.flank * kPi / 180.f;
        if (p.flags & 4) h += flank;
        else if (p.flags & 8) h -= flank;
        // Terrain probe (0x46b34c): avoid deep water and big steps.
        auto ok = [&](float hh) {
            const Vec3 f{std::sin(hh), 0, -std::cos(hh)};
            const float g0 = world.terrain->heightAt(o.pos.x, o.pos.z);
            const Vec3 a = o.pos + f * 2.f, b = o.pos + f * 4.f;
            const float ga = world.terrain->heightAt(a.x, a.z), gb = world.terrain->heightAt(b.x, b.z);
            const float w = world.terrain->waterLevel();
            // DF1 rejects water deeper than 0.75 m; DF2 only with ai flag 0x800,
            // deeper than 0.25 m (0x4151a0).
            if (!df2 && w != 0 && w - ga > 0.75f) return false;
            if (df2 && (p.flags & 0x800) && w != 0 && w - ga > 0.25f) return false;
            if (ga - g0 > 0.5f || g0 - ga > 1.f) return false;
            if (gb - g0 > 3.f || g0 - gb > 4.f) return false;
            return true;
        };
        float chosen = h;
        if (!ok(h)) {
            const int tries = int(p.flank * 256.f / 360.f) / 4 + 8;
            const float step = float(0x4000000) / 4294967296.f * 2 * kPi;
            const float sgn = (rnd() & 0x8000) ? 1.f : -1.f;
            for (int k = 1; k <= tries; ++k) {
                if (ok(h + sgn * step * float(k))) { chosen = h + sgn * step * float(k); break; }
                if (ok(h - sgn * step * float(k))) { chosen = h - sgn * step * float(k); break; }
            }
        }
        o.heading = chosen;
        world.requestMove(o, walkMove(world, p));
        p.think = 64;
        bool friendClose = false;
        for (const auto& q : world.objects)
            if (&q != &o && q.person && q.alive() && std::abs(q.pos.x - o.pos.x) < 2.f && std::abs(q.pos.z - o.pos.z) < 2.f)
                friendClose = true;
        if ((dist <= p.closeRange && (p.flags & 0x80)) ||
            (!(p.flags & 2) && p.stateTimer > p.advanceTicks && rnd() < 0x8000 && !friendClose) || (p.flags & 2)) {
            p.state = 2;
            p.stateTimer = 0;
        }
        return;
    }
    // Engage (state 2): the stance, facing the last known position; fire
    // under 350 m half the time if no friend is in the line (0x46c040).
    world.requestMove(o, idleMove(world, p));
    o.heading = headingTo(o.pos, world.nearest(p.lastKnown, o.pos));
    {
        const Vec3 d = world.nearest(p.lastKnown, o.pos) - (o.pos + Vec3{0, 0.5f, 0});
        o.aimPitch = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z));
    }
    p.think = dist < 30.f ? 16 : dist < 70.f ? 32 : 64;
    // DF2 (0x401a8f): also under 128 m at night, not on a parachute, not
    // ordered to hold fire, unseen within +0xA8, and seen more than 0x40.
    const bool df2Ok = !df2 || ((!night || dist < 128.f) && !o.chute && p.order != 1 && p.unseen <= p.unseenLimit && p.seen > 0x40);
    if (dist < 350.f && rnd() < 0x8000 && df2Ok) {
        const Vec3 from = o.pos + Vec3{0, 0.75f, 0}, to = world.nearest(p.lastKnown, from);
        const Vec3 d = to - from;
        const float dd = std::max(dot(d, d), 1e-4f);
        bool blocked = false;
        for (const auto& q : world.objects) {
            if (&q == &o || !q.person || !q.alive() || q.side != o.side) continue;
            const Vec3 c = world.nearest(q.pos, from);
            const float t = std::clamp(dot(c - from, d) / dd, 0.f, 1.f);
            if (len3(from + d * t - c) < 1.f) { blocked = true; break; }
        }
        if (!blocked) {
            const int keep = p.think;
            fire(world, p, shots, sound, player, false);
            if (p.burstLeft == 0) p.think = keep;
        }
    }
    if (!(p.flags & 2) && p.stateTimer > p.holdTicks && rnd() < 0x8000 && (dist > p.closeRange || !(p.flags & 0x80))) {
        p.state = 1;
        p.stateTimer = 0;
    }
}

void AiSystem::tick(World& world, Arsenal& shots, SoundSystem& sound, const PlayerView& player) {
    ++ticks_;
    // Noise boxes are cleared every 0x1b2 ticks (0x4459e8), the "fired"
    // flags every 0x1f0 (0x4473bc).
    if (ticks_ % 0x1b2 == 0) noise_.clear();
    if (ticks_ % 0x1f0 == 0) {
        playerFired_ = false;
        for (auto& p : people_) p.fired = false;
    }
    for (auto& p : people_) {
        WorldObject& o = world.objects[size_t(p.object)];
        if (!o.alive()) continue;
        if (o.hp < p.lastHp) hurt(world, p.object);
        p.lastHp = o.hp;
        // Movement (0x436158): the move's .ANM speed along the heading (not
        // while on a DF2 parachute).
        if (o.moves && o.moveNumber >= 0 && !o.chute) {
            const char* name = moveName(o.moveNumber);
            const AnimMove* mv = name ? o.moves->find(name) : nullptr;
            if (mv && mv->velocity > 0) {
                const Vec3 f{std::sin(o.heading), 0, -std::cos(o.heading)};
                Vec3 next = o.pos + f * (mv->velocity / 60.f);
                // Kept out of collision volumes like the player (0x437a28:
                // the same +-0.75 m mover box), on odd ticks only (0x436cbe).
                Vec3 feet{next.x, o.base, next.z};
                if (ticks_ & 1) feet = world.resolve(feet, 0.75f, 1.75f, 0.5f);
                o.pos.x = feet.x;
                o.pos.z = feet.z;
                const float ext = world.extent();
                o.pos.x -= std::floor(o.pos.x / ext) * ext;
                o.pos.z -= std::floor(o.pos.z / ext) * ext;
                // Height: World::personTick (gravity and the ground pass).
            }
        }
        if (--p.think <= 0) {
            think(world, p, shots, sound, player);
            p.think = std::max(1, p.think);
            p.delay = p.think;
        }
    }
}

bool AiSystem::spotted(int team, int targetTeam) const {
    return team >= 0 && team < 64 && targetTeam >= 0 && targetTeam < 64 && (spotted_[size_t(team)] >> targetTeam & 1);
}

bool AiSystem::engaging(int team) const { return team >= 0 && team < 64 && engaging_[size_t(team)]; }

bool AiSystem::alerted(int team) const { return team >= 0 && team < 64 && teamAlert_[size_t(team)] == 2; }

bool AiSystem::reached(int team, int path, int point) const {
    auto it = reached_.find(team * 128 + path);
    return it != reached_.end() && point >= 0 && point < 32 && (it->second >> point & 1);
}

void AiSystem::followPath(World& world, int team, int path) {
    // 0x46bbe8: teams below 3 are ignored; each member takes the path from
    // its nearest point.
    if (team < 3 || !mission_ || path <= 0 || path >= int(mission_->paths.size())) return;
    const MissionPath& mp = mission_->paths[size_t(path)];
    for (auto& p : people_) {
        WorldObject& o = world.objects[size_t(p.object)];
        if (o.team != team || !o.alive()) continue;
        p.pathMode = 1;
        p.path = path;
        float best = 1e30f;
        const float ext = world.extent();
        for (size_t k = 0; k < mp.points.size(); ++k) {
            const MissionObject* wp = mission_->inGroup(2, mp.points[k]);
            if (!wp) continue;
            Vec3 at{std::fmod(std::fmod(wp->x, ext) + ext, ext), o.pos.y, std::fmod(std::fmod(-wp->y, ext) + ext, ext)};
            const Vec3 d = world.nearest(at, o.pos) - o.pos;
            if (dot(d, d) < best) best = dot(d, d), p.waypoint = int(k);
        }
        p.think = 1;
    }
}

void AiSystem::property(World& world, int team, int subtype, int value) {
    for (auto& p : people_) {
        if (world.objects[size_t(p.object)].team != team) continue;
        switch (subtype) {
        case 1: p.weapon = aiWeaponMode(value); world.objects[size_t(p.object)].weaponMode = p.weapon; break;
        case 2: if (df2 && !value) p.flags &= ~2u; else p.flags |= 2; break;   // DF2 also clears
        case 3: if (df2 && !value) p.flags &= ~0x10u; else p.flags |= 0x10; break;   // DF2 also clears
        case 4: if (df2 && !value) p.flags &= ~0x20u; else p.flags |= 0x20; break;   // DF2 also clears
        case 5: p.alert = 2; alertTeam(team); break;
        case 6: p.alert = 0; break;
        case 7: p.fov = float(value); break;
        case 8: if (value > 0) p.accuracy = float(value); break;
        case 9: p.perception = float(value) / 100.f; break;
        case 10: p.advanceTicks = value * 62; break;
        case 11: p.holdTicks = value * 62; break;
        case 12: p.giveUpTicks = value * 62; break;
        case 13: p.flank = float(value); break;
        case 14: p.silent = df2 ? value != 0 : true; break;
        case 15: if (df2 && !value) p.flags &= ~1u; else p.flags |= 1; break;
        // DF2 (0x413...): 16-19 set or clear ai 0x200 / 0x400 / 0x800 / 0x1000.
        case 16: case 17: case 18: case 19:
            if (df2) {
                const uint32_t bit = 0x200u << (subtype - 16);
                if (value) p.flags |= bit; else p.flags &= ~bit;
            }
            break;
        default: break;
        }
    }
}

void AiSystem::teamSpeed(int team, int kmh) {
    // Stored for vehicles and helicopters (0x46cafc); people ignore it.
    if (team >= 0 && team < 64) teamSpeed_[size_t(team)] = kmh;
}

void AiSystem::playerRoute(Vec3 origin, int side) {
    if (!mission_) return;
    for (size_t pi = 0; pi < mission_->paths.size(); ++pi) {
        const MissionPath& mp = mission_->paths[pi];
        if (!(mp.flags & 2)) continue;   // the player's route
        for (size_t k = 0; k < mp.points.size() && k < 32; ++k) {
            const MissionObject* wp = mission_->inGroup(2, mp.points[k]);
            if (!wp) continue;
            float radius = float(wp->raw[7]);
            if (radius <= 0) radius = wp->type == kWaypointB ? 1.f : 0.5f;
            const float dx = std::abs(float(wp->x) - origin.x), dy = std::abs(float(wp->y) - origin.z);
            if (std::max(dx, dy) + std::min(dx, dy) * 0.5f <= radius) reached_[side * 128 + int(pi)] |= 1u << k;
        }
    }
}

} // namespace df
