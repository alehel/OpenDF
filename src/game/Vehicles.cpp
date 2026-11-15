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

// Helicopter types as the EXEs test them (item def +0x20, the graphic
// number). DF.EXE: the Gazelle, and everything else as the Black Hawk. GOG
// Df2.exe: the Gazelle (0x7e2) and Black Hawk (0x7e5) door guns have limits
// and only the Black Hawk turns its door to the target; the Havoc (0x7d5),
// Hind (0x7ec) and Ka-29 (0x7ed) are neither (0x42b922, 0x42bb5e, 0x42bca2).
enum class HeloType { Gazelle, Hawk, Other };
HeloType heloType(const World& world, const WorldObject& o) {
    const int g = o.item ? o.item->id - 100000 : 0;
    if (g == kGazelle) return HeloType::Gazelle;
    return (!world.df2Rules || g == 0x7e5) ? HeloType::Hawk : HeloType::Other;
}

// The rest height of the origin above the ground (DF.EXE: Gazelle 0x3f000,
// others 0x37000; GOG Df2.exe 0x42ca03 / 0x42c6ad by graphic number).
float heloRest(const World& world, const WorldObject& o) {
    const int g = o.item ? o.item->id - 100000 : 0;
    int r = g == kGazelle ? 0x3f000 : 0x37000;
    if (world.df2Rules) r = g == 0x7e5 ? 0x37000 : g == 0x7d5 ? 0x38000 : g == 0x7ec ? 0x41000 : g == 0x7ed ? 0x34000 : 0x3f000;
    return float(r) / 65536.f;
}

} // namespace

void VehicleSystem::init(World& world, const Mission& mission) {
    mission_ = &mission;
    vehicles_.clear();
    for (size_t i = 0; i < world.objects.size(); ++i) {
        WorldObject& o = world.objects[i];
        if (!o.item || o.missionIndex < 0) continue;
        const std::string& f = o.item->aiFunction;
        const std::string& mv = o.item->moveFunction;
        const bool df2Mover = mv == "herc" || mv == "boat" || mv == "loco" || mv == "carg";
        if (f != "veh0" && f != "helo" && !df2Mover) continue;
        const MissionObject& mo = mission.objects[size_t(o.missionIndex)];
        const auto* rec = reinterpret_cast<const uint8_t*>(mo.raw);
        Vehicle v;
        v.object = int(i);
        v.helo = f == "helo";
        if (v.helo) std::fprintf(stderr, "DBGH helo side %d flags %x\n", o.side, unsigned(mo.raw[3]));
        v.kind = v.helo ? Kind::Helo : mv == "herc" ? Kind::Herc : mv == "boat" ? Kind::Boat : mv == "loco" ? Kind::Loco
               : mv == "carg" ? Kind::Car : Kind::Veh0;
        const uint32_t fl = uint32_t(mo.raw[3]);
        v.flags = (fl & 1 ? 1u : 0u) | (fl & 2 ? 2u : 0u);
        v.path = rec[0x49];
        v.pathMode = v.path ? 1 : 0;
        v.waypoint = rec[0x4A];
        v.targetHeading = o.heading;
        v.lastHp = o.hp;
        // GOG Df.exe 0x449710 (0x449b24..0x449b36): home = the load position.
        // Delta Force 2 (its helicopter home is not traced in Df2.exe; its
        // vehicle path step 0x42db20 writes none): the first one-way path end.
        v.home = o.pos;
        v.hasHome = !world.df2Rules;
        // The AI block soldiers use too (0x440109): perception +0x24 (%),
        // field of view +0x38 (degrees; half angle in 1/256 turns), alert +0x42.
        v.perception = (mo.raw[0x24 / 4] > 0 ? float(mo.raw[0x24 / 4]) : 100.f) / 100.f;
        {
            const uint16_t fov = uint16_t(rec[0x38] | rec[0x39] << 8);
            v.fovHalf = float((int((fov ? float(fov) : 180.f) * 65536.f / 360.f) >> 9) | 1) / 256.f * 2 * kPi;
        }
        v.alert = std::clamp(int(rec[0x42]), 0, 2);
        // DF2's BRDM (item graphic number 0x7e7, GOG Df2.exe 0x423a25): the
        // AI block (0x403790) also gives ai+0x34 0x200 from record bit 0x800
        // and ai+0x80 = 65536 x 100 / accuracy (record +0x20, 0 -> 0x10000).
        v.brdm = world.df2Rules && v.kind == Kind::Veh0 && o.item->id == 102023;
        v.everyone = fl & 0x800;
        {
            const int acc = mo.raw[0x20 / 4];
            const int a80 = acc ? int(65536.0 * (1.0 / (double(acc) * 0.01))) : 0x10000;
            v.aimScale = float(a80 >> 1) / 65536.f;
        }
        // A helicopter starts on the ground unless placed in the air.
        v.floorHeight = world.terrain->heightAt(o.pos.x, o.pos.z);
        v.probe = v.floorHeight;   // INFERRED: +0xdc (DF1) / +0xF4 (DF2) before the first probe
        v.think = 1 + int(i % 8);
        vehicles_.push_back(v);
    }
    // Train coupling (DF2.EXE 0x45bd1f): every Train-flagged vehicle (attrib
    // 0x800) points at the first other loco (type 2025) in the list.
    for (size_t a = 0; a < vehicles_.size(); ++a) {
        const WorldObject& o = world.objects[size_t(vehicles_[a].object)];
        if (!o.item || !(o.item->attrib & 0x800)) continue;
        for (size_t b = 0; b < vehicles_.size(); ++b) {
            const WorldObject& l = world.objects[size_t(vehicles_[b].object)];
            if (a != b && l.item && l.item->id == 102025) { vehicles_[a].loco = int(b); break; }
        }
    }
}

// Delta Force 2's trains: loco 0x42c4a0 / ai 0x401340, cars carg 0x42c440.
void VehicleSystem::driveTrain(World& world, AiSystem& ai, Vehicle& v, float teamSpeed) {
    WorldObject& o = world.objects[size_t(v.object)];
    if (v.kind == Kind::Car) {
        // A car copies its loco's step along y and rocks: roll = 4·cos(y << 11)
        // (0x1000000 = 1.4°, one period per 32 m of y). A destroyed car rolls on.
        v.vyTick = v.loco >= 0 ? vehicles_[size_t(v.loco)].vyTick : 0.f;
        o.pos.z += v.vyTick;
        // carg zeroes +0x3C, +0x40, +0x44, +0x9C, then +0x40 = the loco's.
        o.tickVel = {0, 0, v.vyTick};
        o.tickTurn = 0;
        o.roll = 4.f * float(0x400000) / 4294967296.f * 2 * kPi * std::cos(-o.pos.z / 32.f * 2 * kPi);
        return;
    }
    if (--v.think <= 0) {
        // The path only fires its waypoint events; the target speed is the
        // team's speed with or without one; the horn (803) when it starts.
        if (!v.dead) {
            pathStep(world, ai, v);
            if (v.targetSpeed == 0 && teamSpeed != 0 && playSound) playSound(803, o.pos);
            v.targetSpeed = teamSpeed;
        }
        v.think = 0x40;
    }
    // Heading snaps to +y or -y (north / south).
    const float theta = wrapAngle(kPi / 2 - o.heading);   // DF's angle from east, anticlockwise
    o.heading = (theta >= 0 && theta < kPi) ? 0.f : kPi;
    if (v.speed != 0) v.vyTick = (o.heading == 0 ? -1.f : 1.f) * v.speed / kTicks;   // north = -z
    else if (v.loco >= 0 && vehicles_[size_t(v.loco)].vyTick != 0) v.vyTick = vehicles_[size_t(v.loco)].vyTick;
    else if (v.loco < 0) v.vyTick = 0;
    if (v.speed != 0 || v.loco >= 0) o.pos.z += v.vyTick;
    o.tickVel = {0, 0, v.vyTick};   // loco: +0x40 only (+0x3C and +0x9C stay 0)
    const float accel = (o.item ? o.item->acceleration : 1.f) / kTicks * (v.dead ? 2.f : 1.f);
    if (std::abs(v.targetSpeed - v.speed) <= accel) v.speed = v.targetSpeed;
    else v.speed += v.targetSpeed > v.speed ? accel : -accel;
}

// Path step (0x45aa68 / 0x47c5d4): head for the point; arrival when within
// its radius or 2 |speed| (per tick) of it; a one-way path ends with speed 0.
bool VehicleSystem::pathStep(World& world, AiSystem& ai, Vehicle& v) {
    WorldObject& o = world.objects[size_t(v.object)];
    if (!v.pathMode || !mission_ || v.path < 0 || v.path >= int(mission_->paths.size())) return false;
    const MissionPath& path = mission_->paths[size_t(v.path)];
    // Path 0 or an empty path (GOG Df.exe 0x45c6a0 returns 0): path mode ends
    // and the vehicle stops (0x450ae0 / 0x453f30 clear ai+0xa4).
    if (v.path == 0 || path.points.empty()) { v.pathMode = 0; return false; }
    v.waypoint = std::clamp(v.waypoint, 0, int(path.points.size()) - 1);
    const MissionObject* wp = mission_->inGroup(2, path.points[size_t(v.waypoint)]);
    if (!wp) return false;
    const float ext = world.extent();
    Vec3 at = world.nearest({std::fmod(std::fmod(wp->x, ext) + ext, ext), 0, std::fmod(std::fmod(-wp->y, ext) + ext, ext)}, o.pos);
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
                if (!world.df2Rules) v.home = o.pos;   // GOG Df.exe 0x450c26..0x450c2c: every one-way path end
                else if (!v.hasHome) v.home = o.pos;
                v.hasHome = true;
                return false;
            }
        }
        // The next point is looked up again (GOG Df.exe 0x450ae0, 0x453f30;
        // Df2.exe 0x42db20, 0x42f030 call 0x45c6a0 / 0x4033d0 once more) and
        // the heading is set toward it, not the point just reached.
        if (const MissionObject* nx = mission_->inGroup(2, path.points[size_t(v.waypoint)]))
            at = world.nearest({std::fmod(std::fmod(nx->x, ext) + ext, ext), 0, std::fmod(std::fmod(-nx->y, ext) + ext, ext)}, o.pos);
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
        if (world.df2Rules && v.kind == Kind::Veh0) {
            // DF2's veh0 think (GOG Df2.exe 0x4237c0): the next one in 0x20
            // ticks on a path, 0x40 off it (0x4239f9 / 0x423a11), 0x10 once
            // destroyed; the BRDM's turret thinks here (0x423a1e) and the
            // tracking timer ai+0x50 grows by the interval (0x423a98).
            if (v.dead) v.think = 0x10;
            else {
                v.think = onPath ? 0x20 : 0x40;
                if (v.brdm) brdmThink(world, ai, v);
                v.losTimer += v.think;
            }
        } else {
            v.think = std::abs(v.speed) > 0 ? 0x40 : 0x10;
        }
    }
    if (world.df2Rules && v.kind == Kind::Veh0 && v.dead) {
        // DF2's veh0 move (GOG Df2.exe 0x423ac0) runs only while flags & 4
        // is clear: a destroyed one stops where it blew up (no steer, step,
        // ramp or ground follow); the think (0x423865) has zeroed the turn
        // +0x9C and the velocity block +0x3C..+0x50.
        v.speed = 0;
        o.tickVel = {0, 0, 0};
        o.tickTurn = 0;
        return;
    }
    // veh0 move (0x480689): steer = heading error / 8; turn (π/4)/L·v·sin
    // steer; forward v·cos steer; a lateral term (L/2)·SIN[turn per tick
    // in 1/2048 turns] along DF's (sin yaw, cos yaw); all with the new yaw
    // and the speed before this tick's ramp. GOG Df.exe indexes its sine
    // table unsigned (0x42a078: shr 0x15), so clockwise turns get the term
    // too, with its sign; the CD (0x4806e4) read before its table for
    // negative indices, taken as 0 (still so for Delta Force 2 here).
    float steer = wrapAngle(v.targetHeading - o.heading) / 8.f;
    if (world.df2Rules && v.kind == Kind::Veh0) {
        // DF2.EXE's veh0 (0x41efa1): the steer eases toward (target − heading) / 2
        // by at most 0x200000 (0.18°) a tick; herc and boat keep DF1's / 8.
        const float want = wrapAngle(v.targetHeading - o.heading) / 2.f, step = float(0x200000) / 4294967296.f * 2 * kPi;
        steer = o.steer + std::clamp(want - o.steer, -step, step);
    }
    o.steer = steer;   // drawn by the `tire` routine (0x4808fc)
    const float axle = o.item && o.item->axleDist > 0 ? o.item->axleDist : 2.3f;
    const float dyaw = (kPi / 4.f) / axle * v.speed * std::sin(steer) / kTicks;
    o.heading += dyaw;
    const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)}, right{std::cos(o.heading), 0, std::sin(o.heading)};
    const Vec3 was = o.pos;
    o.pos = o.pos + fwd * (v.speed * std::cos(steer) / kTicks);
    {
        const int idx = int(std::floor(-dyaw * 2048.f / (2 * kPi)));   // DF yaw grows anticlockwise
        if (idx >= 0 || !world.df2Rules) {
            const float lat = std::sin(float(idx) * 2 * kPi / 2048.f) * axle * 0.5f / 64.f;
            o.pos.x += lat * std::cos(o.heading);   // DF (sin θ, cos θ) with θ = π/2 − heading
            o.pos.z -= lat * std::sin(o.heading);
        }
    }
    // The step is the velocity +0x3C / +0x40 and the turn +0x90 (DF1
    // 0x480732..0x480797) / +0x9C (DF2 veh0 0x41f09d..0x41f0fe, herc
    // 0x42c240, boat 0x42be10), written only while the speed is not 0: a
    // stopped vehicle keeps its last values (DF1 0x480520 skips the move).
    if (v.speed != 0) {
        o.tickVel = {o.pos.x - was.x, 0, o.pos.z - was.z};
        o.tickTurn = dyaw;
    }
    const float accel = (o.item ? o.item->acceleration : 1.f) / kTicks * (v.dead ? 2.f : 1.f);
    if (std::abs(v.targetSpeed - v.speed) <= accel) v.speed = v.targetSpeed;
    else v.speed += v.targetSpeed > v.speed ? accel : -accel;
    const float ext = world.extent();
    o.pos.x -= std::floor(o.pos.x / ext) * ext;
    o.pos.z -= std::floor(o.pos.z / ext) * ext;
    // The Hercules never follows the ground: level at its start height (0x42c240).
    if (v.kind == Kind::Herc) return;
    const Vec3 f = o.pos + fwd * 2.75f, b = o.pos - fwd * 2.75f, l = o.pos - right * 0.6875f, r = o.pos + right * 0.6875f;
    float hf = world.terrain->heightAt(f.x, f.z), hb = world.terrain->heightAt(b.x, b.z);
    float hl = world.terrain->heightAt(l.x, l.z), hr = world.terrain->heightAt(r.x, r.z);
    if (v.kind == Kind::Boat) {
        // 0x42be10: each sample at least the water level (afloat); destroyed,
        // at least the boat's own height, sinking 0x800 a tick while any
        // sample is at or under it, until it rests on the bottom.
        const float floor = v.dead ? o.pos.y - o.lift : world.terrain->waterLevel();
        const bool sinking = v.dead && std::min({hf, hb, hl, hr}) <= floor;
        hf = std::max(hf, floor); hb = std::max(hb, floor); hl = std::max(hl, floor); hr = std::max(hr, floor);
        if (sinking) { hf -= 0x800 / 65536.f; hb -= 0x800 / 65536.f; }
    }
    o.base = (hf + hb) * 0.5f;
    o.pos.y = o.base + o.lift;
    // (front - back) << 11 and (left - right) << 13 as binary angles.
    o.pitch = (hf - hb) / 32.f * 2 * kPi;
    o.roll = (hl - hr) / 8.f * 2 * kPi;
}

// helo (0x47c92c think every 8 ticks, 0x47d05c move): rotor spin-up
// (+0x800000 per think to 0x1b000000 when busy, else wind-down; lift from
// 0x14000000); modes: on a
// path 18 m above the floor (band 4 m), hover 8 m (1 m), land; turning at
// turn_rate when more than 6 m up; speed ramps at `acceleration`; vertical
// speed capped at 2 m/s; bank from the turn (within 22.5 degrees), nose
// down by speed.
void VehicleSystem::fly(World& world, AiSystem& ai, Vehicle& v, float teamSpeed) {
    WorldObject& o = world.objects[size_t(v.object)];
    const float turn = 2 * kPi;   // binary angle -> radians
    const float ground = world.terrain->heightAt(o.pos.x, o.pos.z);
    const float rest = heloRest(world, o);
    // Rotor angles advance by the speeds on every move call (0x47d064).
    o.rotorAngle[0] += uint32_t(v.rotor);
    o.rotorAngle[1] += uint32_t(v.rotor);
    o.rotorSpeed = v.rotor;
    if (--v.think <= 0) {
        v.think = 8;
        // Destroyed (+0x6C & 4): both rotor speeds 0 at once (0x47cac8).
        if (v.dead) v.rotor = 0;
        bool hadTarget = v.gunTarget != -1;   // GOG Df2.exe 0x42c21b: the dead think reads it before clearing it
        int attackShift = -1;                 // the attack approach's altitude call (0x42b7f0), if any
        float attackH = 8, attackBand = 1;
        if (!v.dead) heloTargeting(world, ai, v);
        if (!v.dead) hadTarget = v.gunTarget != -1;   // a target taken this think counts (0x42c50d)
        else v.gunTarget = -1;   // the dead think clears the target (the gun fires until then)
        // Tail rotor (0x47cb4b): above 2 hits a smoke puff at hardpoint 2
        // every think, and one more hit with chance 1/8 up to 17 (GOG 0x45109c).
        if (!v.dead && o.tailHits > 2) {
            if (smoke && o.model && o.model->points.size() > 2) {
                const auto& p = o.model->points[2];
                const Mat4 mm = o.matrix(o.pos);
                smoke({mm.at(0, 0) * p[0] + mm.at(0, 1) * p[1] + mm.at(0, 2) * p[2] + mm.at(0, 3),
                       mm.at(1, 0) * p[0] + mm.at(1, 1) * p[1] + mm.at(1, 2) * p[2] + mm.at(1, 3),
                       mm.at(2, 0) * p[0] + mm.at(2, 1) * p[1] + mm.at(2, 2) * p[2] + mm.at(2, 3)});
            }
            if (o.tailHits <= 16 && (rnd() & 7) == 0) ++o.tailHits;
        }
        // Delta Force's altitude controller (GOG Df.exe 0x450100) runs in
        // the think: which branch calls it with which height, band and
        // hysteresis shift (ctl 1), the landing branch (ctl 2), or neither
        // (ctl 0: the accel +0xb0 stays).
        const int nTail = o.tailHits > 2 ? std::min(o.tailHits, 5) : 1;   // 0x45100a
        int ctl = 2, ctlShift = nTail;
        float ctlH = 8.f, ctlBand = 1.f;
        auto use = [&](float H, float band, int shift) { ctl = 1, ctlH = H, ctlBand = band, ctlShift = shift; };
        const bool onPath = !v.dead && v.gunTarget == -1 && pathStep(world, ai, v);
        if (onPath) { v.mode = 2; v.targetSpeed = teamSpeed; }
        else if (v.gunTarget != -1 && !v.dead) {
            // Attack approach (0x47bf0c) toward the target's position when
            // acquired, only with lift (GOG Df.exe 0x451432 calls 0x4503f0
            // from rotor 0x14000000; below it heading, speed and mode stay
            // as they were while the rotor spins up): > 80 m 15 m/s at
            // 18 m; 30-80 m 5 m/s at 8 m; 8-30 m hover at 8 m; closer 5 m/s at 8 m. The Black Hawk turns its door
            // to the target: DF +0x10000000 / +0x30000000 (anticlockwise, the
            // nose left, the target on the right-hand door side) in the middle
            // bands (GOG Df.exe 0x4503f0), so -22.5 / -67.5 degrees in
            // OpenDF's clockwise headings.
            const Vec3 d = world.nearest(v.lastKnown, o.pos) - o.pos;
            // GOG Df.exe 0x4503f0 -> 0x477670(|dx|, |dy|, 0), Df2.exe 0x42b8c7
            // -> 0x41e880 the same: max + 11/32 min.
            const float ax = std::abs(d.x), az = std::abs(d.z);
            const float dist = std::max(ax, az) + std::min(ax, az) * 11.f / 32.f;
            // GOG Df2.exe 0x42b922 / 0x42b969: the Black Hawk (0x7e5) only.
            const bool hawk = heloType(world, o) == HeloType::Hawk;
            float h = std::atan2(d.x, -d.z);
            // DF2 (GOG Df2.exe 0x42c5b1): 0x42b7f0 also only with lift; its
            // altitude calls (0x42b8ed, 0x42b917, 0x42b95e, 0x42b99e): 18 m
            // band 4 m shift 4 beyond 80 m, else 8 m band 1 m, shift 5, 7
            // in the hover band (8..30 m), 5 closer. Delta Force's are the
            // use() calls (0x4503f0 -> 0x450100, the same bands and shifts).
            if (world.df2Rules && v.rotor >= float(0x14000000))
                attackShift = dist > 80 ? 4 : dist > 30 ? 5 : dist > 8 ? 7 : 5, attackH = dist > 80 ? 18.f : 8.f, attackBand = dist > 80 ? 4.f : 1.f;
            if (v.rotor < float(0x14000000)) { h = v.targetHeading; ctl = 0; }
            else if (dist > 80) { v.targetSpeed = 15; v.mode = 2; use(18.f, 4.f, 4); }
            else if (dist > 30) { v.targetSpeed = 5; v.mode = 1; if (hawk) h -= kPi / 8; use(8.f, 1.f, 5); }
            else if (dist > 8) { v.targetSpeed = 0; v.mode = 1; if (hawk) h -= 3 * kPi / 8; use(8.f, 1.f, 7); }
            else { v.targetSpeed = 5; v.mode = 1; use(8.f, 1.f, 5); }
            v.targetHeading = h;
        } else if (!v.dead && (v.flags & 2) && v.hasHome && homeDist(world, v) > 4.f) {
            // Back home (GOG Df.exe 0x4509f0): the load position or the latest
            // one-way path end; within 4 m (0x40000) it returns 0 and the
            // idle branch below decides.
            const Vec3 d = world.nearest(v.home, o.pos) - o.pos;
            const float dist = homeDist(world, v);
            v.targetHeading = std::atan2(d.x, -d.z);
            // 0x4509f0 calls 0x450100 itself: shifts 2 / 1 / 0.
            if (dist > 80) v.targetSpeed = 15, v.mode = 2, use(18.f, 4.f, 2);
            else if (dist > 30) v.targetSpeed = 5, v.mode = 2, use(18.f, 4.f, 1);
            else v.targetSpeed = 2.5f, v.mode = 1, use(8.f, 1.f, 0);
        } else if (!world.df2Rules) {
            // Idle (GOG Df.exe 0x4512cd..0x451330): hover (mode 1) when
            // alerted (ai+0x9c 2) or the signed speed +0x7c is 0x400 or more;
            // else mode 1 only from 9 m up (+0xb4 >= 0x90000, the height the
            // last controller or landing think stored), 0 below.
            v.targetSpeed = 0;
            if (v.alert == 2 || v.speed >= 0x400 / 65536.f * kTicks) v.mode = 1;
            else v.mode = v.heightB4 >= float(0x90000) / 65536.f ? 1 : 0;
        } else {
            v.targetSpeed = 0;
            v.mode = (v.alert == 2 || std::abs(v.speed) >= 0x400 / 65536.f * kTicks) ? 1 : 0;
            if (v.mode == 1 && v.stopped && v.alert != 2) v.mode = 0;
        }
        // Path and idle (0x451317..0x45138f): mode 0 or no lift -> the
        // landing branch; else 0x450100 at 18 m / 4 m (path) or 8 m / 1 m.
        if (!v.dead && ctl == 2 && v.gunTarget == -1 && v.mode != 0 && v.rotor >= float(0x14000000))
            use(v.mode == 2 ? 18.f : 8.f, v.mode == 2 ? 4.f : 1.f, nTail);
        if (world.df2Rules) df2Altitude(world, v, hadTarget, attackShift, attackH, attackBand);
        float fl = ground;
        if (world.df2Rules) {
            // The ground around and ahead (1, 2, 4 s at this speed), for the
            // rotor's idle test below; df2Altitude keeps the floor +0xCC.
            const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)};
            for (const Vec3 p : {o.pos + Vec3{4, 0, 0}, o.pos - Vec3{4, 0, 0}, o.pos + Vec3{0, 0, 4}, o.pos - Vec3{0, 0, 4},
                                 o.pos + fwd * v.speed, o.pos + fwd * (2 * v.speed), o.pos + fwd * (4 * v.speed)})
                fl = std::max(fl, world.terrain->heightAt(p.x, p.z));
        } else {
            if (ctl == 2) {
                // The landing branch (0x451198..0x4512a6): the probe +0xdc
                // refreshed (0x486fd0 from 3 m below, 3 m step: terrain or an
                // object face, not its own); with lift and more than 9 m up
                // (0x90000) it hovers instead.
                v.probe = world.faceGround(o.pos - Vec3{0, 3.f, 0}, 3.f, nullptr, v.object);
                const float unit = 1.f / 65536.f;
                const bool lift = v.rotor >= float(0x14000000);
                v.floorHeight = v.probe;
                v.heightB4 = o.pos.y - v.probe;   // +0xb4 (0x45119e.., before the landing clamp)
                if (lift && o.pos.y - v.probe > float(0x90000) * unit) use(8.f, 1.f, nTail);
                else if (v.probe + rest < o.pos.y) v.accel = !lift ? -0x134 * unit : v.vz < -0x3ff * unit ? 0.f : -0x9a * unit;
                else { v.accel = 0; v.vz = 0; o.pos.y = v.probe + rest; }
            }
            if (ctl == 1) altitude(world, v, ctlH, ctlBand, ctlShift);
            fl = v.floorHeight;
        }
        if (!v.dead) {
            // Rotor (GOG Df.exe 0x450e40): +0x800000 per think up to
            // 0x1b000000 (0x451035) while the mode is not 0 or the think is
            // busy: a target, alerted (ai+0x9c 2), idle (below 0x400 speed)
            // more than 8 m up (+0xb4 > 0x80000), or still coming down with
            // lift in the landing branch; otherwise it winds down, 0x400000
            // per think above 0x700000, then 0x20000 to 0 (0x4514d7), and the
            // rotor loop stops at 0. Parked helicopters stay silent and still.
            // DF2 keeps the old always-on spin-up (its own think is not
            // reproduced here yet).
            const bool liftNow = v.rotor >= float(0x14000000);
            // The idle test reads the stored +0xb4 (0x45131f), signed speed.
            const bool idle = v.speed < 0x400 / 65536.f * kTicks;
            const bool busy = world.df2Rules || v.mode != 0 || v.gunTarget != -1 || v.alert == 2 ||
                              (idle && v.heightB4 > float(0x80000) / 65536.f) || (liftNow && o.pos.y > ground + rest);
            if (busy) v.rotor = std::min(v.rotor + float(0x800000), float(0x1b000000));
            else if (v.rotor > float(0x700000)) v.rotor -= float(0x400000);
            else v.rotor = std::max(v.rotor - float(0x20000), 0.f);
        }
        if (!world.df2Rules) v.probeTimer += 8;   // ai+0x2c += the think interval (0x451115); DF2: in df2Altitude
        if (!world.df2Rules) v.losTimer += 8;     // ai+0x40 (0x45111b), the door gun's sight re-check
    }
    const bool lift = v.rotor >= float(0x14000000);
    const float unit = 1.f / 65536.f;   // m per tick
    if (!world.df2Rules) {
        // The move (GOG Df.exe 0x451590): z += vz, then vz += accel (no cap);
        // while coming down, never below max(terrain, probe +0xdc) + rest
        // (vz is left as it is), destroyed when that happens faster than
        // 0x1000 a tick (0x45170e).
        o.pos.y += v.vz;
        v.vz += v.accel;
        if (v.vz < 0) {
            const float floor = std::max(world.terrain->heightAt(o.pos.x, o.pos.z), v.probe) + rest;
            if (o.pos.y < floor) {
                o.pos.y = floor;
                // 0x45170e..0x45174e: under -0x1000 a tick the impact calls the
                // three blasts 0x450d50 and sets flags 0x10c whether or not the
                // helicopter was already destroyed (flag 4 is not tested), so a
                // shot-down one goes off again when it hits the ground (once).
                if (v.vz < -0x1000 * unit && !v.impactBlast) {
                    v.impactBlast = true;
                    if (!v.dead && o.hp > 0) world.damage(v.object, o.hp + 100);   // the destroy blasts follow
                    else if (detonate) {
                        const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)};
                        detonate(o.pos + Vec3{0, 1, 0}, 40, o.heading);
                        detonate(o.pos + fwd * 4.f, 10, o.heading);
                        detonate(o.pos - fwd * 4.f, 10, o.heading);
                    }
                }
            }
        }
    }
    const float height = o.pos.y - v.floorHeight;
    // Turning (0x47ba4c): turn_rate degrees/s, only above 6 m.
    v.yawRate = 0;
    if (height > 6.f && lift) {
        const float maxTurn = (o.item ? o.item->turnRate : 30.f) * kPi / 180.f / kTicks;
        const float err = wrapAngle(v.targetHeading - o.heading);
        v.yawRate = std::clamp(err, -maxTurn, maxTurn);
        o.heading += v.yawRate;
        // +0x90 (DF1 0x47ba4c) / +0x9C (DF2 0x426580): 0 first, ± the rate
        // when turning by it; reaching the target heading leaves 0. Below
        // 6 m the routine is not called and the last value stays.
        o.tickTurn = std::abs(err) < maxTurn ? 0.f : v.yawRate;
    }
    // Tail-rotor spin (0x47d0bc): min(hits << 22, 0x1400000) per tick,
    // anticlockwise, beyond what the heading controller can hold.
    if (!v.dead && o.tailHits > 2)
        o.heading -= float(std::min(uint32_t(o.tailHits) << 22, 0x1400000u)) / 4294967296.f * turn;
    // Speed (0x47bacc). Delta Force (GOG Df.exe 0x44ffc0, from 0x451697
    // toward 0 without lift and 0x451790 / 0x451848 toward +0x88 with it):
    // by twice the rate once destroyed (flags & 4, 0x44ffe7), lift or not.
    const float accel = (o.item ? o.item->acceleration : 4.f) / kTicks * (!world.df2Rules && v.dead ? 2.f : 1.f);
    const float want = !world.df2Rules && !lift ? 0.f : v.targetSpeed;
    if (std::abs(want - v.speed) <= accel) v.speed = want;
    else v.speed += want > v.speed ? accel : -accel;
    if (world.df2Rules && !lift && height < 1.f) v.speed = 0;
    // The vertical: Delta Force's think chose the accel (altitude(), the
    // move above); Delta Force 2's the vzAccel (df2Altitude), applied after
    // the step below (GOG Df2.exe 0x42c960).
    const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)};
    o.pos = o.pos + fwd * (v.speed / kTicks);
    // Velocity +0x3C / +0x40 = speed (cos, sin) >> 6, 0 at speed 0 (DF1
    // 0x47bb1c, DF2 0x426660).
    o.tickVel = fwd * (v.speed / kTicks);
    if (world.df2Rules) {
        // GOG Df2.exe 0x42c99c..0x42c9b4: z += vz, then vz += +0xC8; no cap.
        o.pos.y += v.vz;
        v.vz += v.vzAccel;
        // 0x42c9d9..0x42ca7e: while falling, it lands on max(ground, probe)
        // + rest (vz is left as it is: the next think zeroes it); landing
        // with vz under -0x1000 destroys it.
        const float land = std::max(ground, v.probe) + rest;
        if (v.vz < 0 && o.pos.y < land) {
            o.pos.y = land;
            if (v.vz < -0x1000 * unit && !v.dead && o.hp > 0) world.damage(v.object, o.hp + 100);
        }
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

// Delta Force's altitude controller (GOG Df.exe 0x450100), from the think:
// floor = max(probe +0xdc under the hub, refreshed when ai+0x2c passes
// 0x3e; terrain 4 m to each side), then the terrain 1, 2 and 4 s ahead at
// this speed, n = 1 + those that raise it. Hysteresis 0x100 << shift: the
// height H lower while coming down, the band wider otherwise. Below:
// vz > 0x7ff ? -0x100 : n * 0x100; above: vz < -0x7ff ? +0x100 : -n * 0x100;
// inside -0xcd (rising), +0x33 (falling), -0x4d (still). Tail hits 7..11
// count as below, 12 or more give -0x134. The result is the accel +0xb0.
void VehicleSystem::altitude(World& world, Vehicle& v, float H, float band, int shift) {
    WorldObject& o = world.objects[size_t(v.object)];
    const float unit = 1.f / 65536.f;
    if (v.probeTimer > 0x3e) {
        v.probeTimer = 0;
        v.probe = world.faceGround(o.pos - Vec3{0, 3.f, 0}, 3.f, nullptr, v.object);
    }
    float fl = v.probe;
    for (const Vec3 p : {o.pos + Vec3{4, 0, 0}, o.pos - Vec3{4, 0, 0}, o.pos + Vec3{0, 0, 4}, o.pos - Vec3{0, 0, 4}})
        fl = std::max(fl, world.terrain->heightAt(p.x, p.z));
    const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)};
    int n = 1;
    for (const float k : {1.f, 2.f, 4.f}) {
        const Vec3 p = o.pos + fwd * (k * v.speed);
        const float h = world.terrain->heightAt(p.x, p.z);
        if (h > fl) { fl = h; ++n; }
    }
    v.floorHeight = fl;
    v.heightB4 = o.pos.y - fl;   // +0xb4 = z - floor (0x450100)
    float lo = H + fl;
    const float hy = float(0x100 << shift) * unit;
    if (v.vz < 0) lo -= hy;
    else band += hy;
    int err = o.pos.y < lo ? 1 : o.pos.y > lo + band ? -1 : 0;
    if (o.tailHits > 6 && o.tailHits < 12) err = 1;
    if (o.tailHits > 11) { v.accel = -0x134 * unit; return; }
    if (err < 0) v.accel = v.vz < -0x7ff * unit ? 0x100 * unit : -float(n) * 0x100 * unit;
    else if (err == 0) v.accel = v.vz > 0 ? -0xcd * unit : v.vz < 0 ? 0x33 * unit : -0x4d * unit;
    else v.accel = v.vz > 0x7ff * unit ? -0x100 * unit : float(n) * 0x100 * unit;
}

// 2D distance to home (0x4509f0: 0x477670 of |dx|, |dy|).
float VehicleSystem::homeDist(const World& world, const Vehicle& v) const {
    const WorldObject& o = world.objects[size_t(v.object)];
    const Vec3 d = world.nearest(v.home, o.pos) - o.pos;
    if (world.df2Rules) return std::sqrt(d.x * d.x + d.z * d.z);
    // GOG Df.exe 0x4509f0 -> 0x477670(|dx|, |dy|, 0): max + 11/32 min.
    const float ax = std::abs(d.x), az = std::abs(d.z);
    return std::max(ax, az) + std::min(ax, az) * 11.f / 32.f;
}

// GOG Df2.exe 0x445f50 from 3 m under the origin: the terrain, or the
// highest volume top of the helicopter's neighbours (a vehicle's list, 20 m)
// up to the origin.
float VehicleSystem::heloProbe(const World& world, const Vehicle& v) const {
    const WorldObject& o = world.objects[size_t(v.object)];
    return world.df2Surface(o.pos - Vec3{0, 3.f, 0}, 3.f, world.df2Neighbours(o.pos, 20.f, v.object));
}

// Delta Force 2's helicopter altitude, chosen at think time (GOG Df2.exe
// helo think 0x42c5dc..0x42c7ae) and applied by the move every tick.
// Attack approach (0x42b7f0, with lift): its own call; with a target and no
// lift nothing changes. Hover / path with lift: 0x42b520(8 m, band 1 m) /
// (18 m, 4 m), shift 1, or min(tail hits, 5) above 2 hits. Otherwise
// (landing, no lift, destroyed): the probe +0xF4 (0x445f50, at least the
// water while intact); no lift: fall at -0x134 until at probe + rest; with
// lift more than 9 m up: the hover call; above probe + rest: -0x9a while vz
// > -0x400, else 0; at or under it: set down, vz and the acceleration 0.
void VehicleSystem::df2Altitude(World& world, Vehicle& v, bool hadTarget, int attackShift, float attackH, float attackBand) {
    WorldObject& o = world.objects[size_t(v.object)];
    const float unit = 1.f / 65536.f;
    const float water = world.terrain->waterLevel();
    // 0x42b520(H, band, shift).
    auto controller = [&](float H, float band, int shift) {
        // The probe is refreshed when ai+0x3C (the think intervals) passes 0x3E.
        if (v.probeTimer > 0x3E) { v.probeTimer = 0; v.probe = heloProbe(world, v); }
        float fl = v.probe;
        for (const Vec3 p : {o.pos + Vec3{4, 0, 0}, o.pos - Vec3{4, 0, 0}, o.pos + Vec3{0, 0, 4}, o.pos - Vec3{0, 0, 4}})
            fl = std::max(fl, world.terrain->heightAt(p.x, p.z));
        fl = std::max(fl, water);
        // Look-ahead at 1, 2 and 4 s of speed: k = 1, 2 when the first
        // sample raises the floor, +1 for each later one that does.
        const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)};
        int k = 1;
        for (int i = 0; i < 3; ++i) {
            const Vec3 p = o.pos + fwd * (v.speed * float(1 << i));
            const float h = world.terrain->heightAt(p.x, p.z);
            if (h > fl) { fl = h; k = i == 0 ? 2 : k + 1; }
        }
        v.floorHeight = fl;   // +0xCC = z - floor
        float low = fl + H;
        if (v.vz < 0) low -= float(0x100 << shift) * unit;
        else band += float(0x100 << shift) * unit;
        float err = o.pos.y < low ? low - o.pos.y : o.pos.y > low + band ? low + band - o.pos.y : 0.f;
        if (o.tailHits > 11) { v.vzAccel = -0x134 * unit; return; }   // 0x42b748
        if (o.tailHits > 6) err = 1;                                    // 7..11 hits climb
        if (err < 0) v.vzAccel = v.vz < -0x7ff * unit ? 0x100 * unit : -0x100 * float(k) * unit;
        else if (err > 0) v.vzAccel = v.vz > 0x7ff * unit ? -0x100 * unit : 0x100 * float(k) * unit;
        else v.vzAccel = ((v.vz > 0 ? -0x80 : v.vz < 0 ? 0x80 : 0) - 0x4D) * unit;
    };
    v.probeTimer += 8;   // 0x42c93b: ai+0x3C += the think interval (counted from here; read by 0x42b520)
    const bool lift = v.rotor >= float(0x14000000);
    const int shift = o.tailHits > 2 ? std::min(o.tailHits, 5) : 1;
    if (hadTarget) {
        if (attackShift >= 0) controller(attackH, attackBand, attackShift);
        return;
    }
    if (!v.dead && lift && v.mode != 0) {
        controller(v.mode == 1 ? 8.f : 18.f, v.mode == 1 ? 1.f : 4.f, shift);
        return;
    }
    v.probe = heloProbe(world, v);
    if (!v.dead) v.probe = std::max(v.probe, water);
    v.floorHeight = v.probe;
    const float floor = v.probe + heloRest(world, o);
    if (lift && o.pos.y - v.probe > 9.f) controller(8.f, 1.f, shift);
    else if (o.pos.y > floor) v.vzAccel = !lift ? -0x134 * unit : v.vz > -0x400 * unit ? -0x9a * unit : 0.f;
    else { o.pos.y = floor; v.vzAccel = 0; v.vz = 0; }
}
Vec3 VehicleSystem::targetPosition(const World& world, int t) const {
    return t == -3 ? player_.origin : world.objects[size_t(t)].pos;
}

bool VehicleSystem::targetAlive(const World& world, int t) const {
    return t == -3 ? player_.alive : (t >= 0 && world.objects[size_t(t)].alive());
}

// The gun point (0x47c148): Obj * T(hp4) * Rz(gun yaw) * Ry(gun pitch) *
// T(-hp4) * hp8, in the model's own axes (OpenDF: file +Z -> +Y).
Vec3 VehicleSystem::gunPoint(const World& world, const Vehicle& v) const {
    const WorldObject& o = world.objects[size_t(v.object)];
    const Mat4 body = o.matrix(o.pos);
    if (!o.model || o.model->points.size() <= 8) return o.pos - Vec3{0, 1.f, 0};
    const auto& p4 = o.model->points[4];
    const auto& p8 = o.model->points[8];
    Mat4 gp;
    const float c = std::cos(v.gunPitch), sn = std::sin(v.gunPitch);
    gp.at(0, 0) = c; gp.at(0, 1) = -sn; gp.at(1, 0) = sn; gp.at(1, 1) = c;
    const Mat4 m = body * translate({p4[0], p4[1], p4[2]}) * rotateY(v.gunYaw) * gp * translate({-p4[0], -p4[1], -p4[2]});
    return {m.at(0, 0) * p8[0] + m.at(0, 1) * p8[1] + m.at(0, 2) * p8[2] + m.at(0, 3),
            m.at(1, 0) * p8[0] + m.at(1, 1) * p8[1] + m.at(1, 2) * p8[2] + m.at(1, 3),
            m.at(2, 0) * p8[0] + m.at(2, 1) * p8[1] + m.at(2, 2) * p8[2] + m.at(2, 3)};
}

// Door-gun targeting, every think (0x47ccf2, 0x47c734, 0x47bf0c, 0x47c220):
// with record flag 1 none. A target seen dead is dropped during a pause;
// with flag 2 one more than 100 m (2D) from where the helicopter took it is
// given up for 60 thinks. Without a target the soldiers' scan (0x46a804,
// GOG Df.exe 0x45e280, from 1 m above the hub) is drawn against rand
// (0x450c40; DF2 0x42bff0 the same): score x perception x2 (hard, side 2),
// / 4 when relaxed; taking a target marks the team spotted (0x42edd0; DF2
// 0x41ac00). With a target the
// sight line is re-checked about every 192 ticks; only while it holds is the
// last known position refreshed (and a dead target noticed). The gun's aim
// is that position from the gun point, relative to the body, clamped to the
// gun's limits.
void VehicleSystem::heloTargeting(World& world, AiSystem& ai, Vehicle& v) {
    WorldObject& o = world.objects[size_t(v.object)];
    if (v.flags & 1) return;
    auto dist2D = [&](Vec3 a, Vec3 b) {
        const Vec3 d = world.nearest(b, a) - a;
        float x = std::abs(d.x), z = std::abs(d.z);
        if (x < z) std::swap(x, z);
        return x + z * 11.f / 32.f;   // 0x435bf8
    };
    if (v.gunTarget != -1) {
        if (v.deadSeen && v.pause > 0) v.gunTarget = -1;
        else if ((v.flags & 2) && dist2D(v.acquiredAt, v.lastKnown) > 100.f) { v.gunTarget = -1; v.giveUp = 60; }
    }
    if (v.gunTarget == -1) {
        if (v.giveUp > 0) { --v.giveUp; return; }
        // The scan 0x45e280 first (GOG Df2.exe 0x42bff0 -> 0x417830 the same):
        // while not alerted, a dead person of its own side within 50 m (2D)
        // and in sight (1..2) sets ai+0x9c = 2 and the team's alert word
        // (0x44a3e0 / Df2.exe 0x4045c0).
        if (v.alert != 2)
            for (size_t qi = 0; qi < world.objects.size(); ++qi) {
                const WorldObject& q = world.objects[qi];
                if (!q.person || !q.dead || q.side != o.side) continue;
                const int los = ai.sightLine(world, o.pos, q.pos, v.object, int(qi));
                if (los < 1 || los > 2 || dist2D(o.pos, q.pos) >= 50.f) continue;
                v.alert = 2;
                ai.alertTeamOf(o.team);
            }
        int id = -1;
        Vec3 at{0, 0, 0};
        const Vec3 eye = o.pos + Vec3{0, 1.f, 0};
        float score = ai.bestTarget(world, v.object, eye, o.heading, o.side, v.fovHalf, player_, id, at);
        if (id == -1) return;
        // Perception once, x2 on hard for side 2, / 4 while ai+0x9c is 0
        // (0x45e280); 0x450c40 takes the target when rand < score.
        score *= v.perception * ((ai.hard && o.side == 2) ? 2.f : 1.f);
        if (v.alert == 0) score *= 0.25f;
        if (float(rnd()) >= score * 65536.f) return;
        if ((v.flags & 2) && dist2D(o.pos, at) > 100.f) return;
        // 0x450ca8: the "team spotted team" bit (0x42edd0).
        ai.markSpotted(o.team, id == -3 ? player_.team : world.objects[size_t(id)].team);
        v.gunTarget = id;
        v.lastKnown = at;
        // Only "dead seen" (0x40) is cleared (GOG Df.exe 0x450cb3, Df2.exe
        // 0x42bff0); "in sight" (0x80) keeps its value, so a target taken
        // while it is clear is aimed at the spot first seen until a re-check.
        v.deadSeen = false;
        v.pause = int(rnd() & 0x3F) + 0x20;
        const int r = int((rnd() & 0xFFFF) << 8) - 0x800000;
        v.bias = float(r >= 0 ? r + 0x1000000 : r - 0x1000000);
        v.acquiredAt = o.pos;
        v.giveUp = 0;
        return;
    }
    // Tracking (0x47bf0c), with lift. Delta Force's timer ai+0x40 runs
    // every think, lift or not (GOG Df.exe 0x45111b, in fly()).
    if (v.rotor >= float(0x14000000)) {
        if (world.df2Rules) v.losTimer += 8;
        if (v.losTimer > 0xBA) {
            v.losTimer = 0;
            v.seen = ai.sightLine(world, o.pos + Vec3{0, 1.f, 0}, targetPosition(world, v.gunTarget), v.object, v.gunTarget) >= 1;
            // GOG Df.exe 0x45051a: each sight re-check that holds marks the
            // team spotted (Delta Force only: not traced in Df2.exe).
            if (v.seen && !world.df2Rules) ai.markSpotted(o.team, v.gunTarget == -3 ? player_.team : world.objects[size_t(v.gunTarget)].team);
        }
        if (v.seen) {
            if (!targetAlive(world, v.gunTarget)) v.deadSeen = true;
            v.lastKnown = targetPosition(world, v.gunTarget);
        }
    }
    // Aim (0x47c220).
    const float turn = 2 * kPi / 4294967296.f;
    const HeloType type = heloType(world, o);
    const bool hawk = type == HeloType::Hawk;
    const float deg = kPi / 180.f;
    const Vec3 d = world.nearest(v.lastKnown, o.pos) - gunPoint(world, v);
    const float yaw = -wrapAngle(std::atan2(d.x, -d.z) - o.heading);
    // Delta Force (GOG Df.exe 0x450710): the flat range is the integer
    // square root of the whole-metre differences (0x477620), in metres.
    const float flatRange = world.df2Rules ? std::sqrt(d.x * d.x + d.z * d.z)
                                           : std::floor(std::sqrt(std::floor(d.x) * std::floor(d.x) + std::floor(-d.z) * std::floor(-d.z)));
    const float pitch = std::atan2(d.y, flatRange) - o.pitch;
    // GOG Df2.exe 0x42bb5e / 0x42bba8: a helicopter that is neither the
    // Gazelle nor the Black Hawk stores the aim unclamped.
    if (type == HeloType::Other) { v.gunTgtYaw = yaw; v.gunTgtPitch = pitch; return; }
    v.gunTgtYaw = std::clamp(yaw, hawk ? -135 * deg : -45 * deg, hawk ? -45 * deg : 45 * deg);
    v.gunTgtPitch = std::clamp(pitch, hawk ? -float(0x0C000000) * turn : -45 * deg, 0.f);
}

// The door gun every tick (0x47d0e8, 0x47d24b): the gun turns 0x800000 per
// tick toward its aim when more than 0x1000000 off; with a target (no rotor
// or death check) and the yaw within 0x0C000000 of the aim: a pause counts
// down, then a burst counter, a round whenever it is a multiple of 8; when
// both are out a new pause 64..126, a burst 16..46 and the bias x 3/4. A
// round (0x47c340): from the gun point at the last known position, refused
// outside the gun's limits; +-0x200000 error on yaw and pitch, the bias on
// yaw, then the gun's own spread (gun 14 accuracy 4: (0x8000 - rand) x 90
// each, 0x46af90).
void VehicleSystem::doorGun(World& world, AiSystem& ai, Vehicle& v, Arsenal& shots, SoundSystem& sound) {
    WorldObject& o = world.objects[size_t(v.object)];
    const float turn = 2 * kPi / 4294967296.f;
    const HeloType type = heloType(world, o);
    const bool hawk = type == HeloType::Hawk;
    const float deg = kPi / 180.f;
    // 0x47ca6c (GOG Df2.exe 0x42c379, 0x7e5 only): the Black Hawk's gun
    // starts pointing out of its door.
    if (!v.gunInit) {
        v.gunInit = true;
        if (hawk) v.gunYaw = v.gunTgtYaw = -90 * deg;
    }
    const float step = float(0x800000) * turn, deadBand = float(0x1000000) * turn;
    if (std::abs(v.gunTgtYaw - v.gunYaw) > deadBand) v.gunYaw += v.gunTgtYaw > v.gunYaw ? step : -step;
    if (std::abs(v.gunTgtPitch - v.gunPitch) > deadBand) v.gunPitch += v.gunTgtPitch > v.gunPitch ? step : -step;
    o.gunYaw = v.gunYaw;
    o.gunPitch = v.gunPitch;
    if (v.gunTarget == -1) return;
    if (std::abs(v.gunTgtYaw - v.gunYaw) >= float(0x0C000000) * turn) return;
    if (v.pause > 0) { --v.pause; return; }
    if (v.burst <= 0) {
        v.pause = int(rnd() & 0x3F) + 0x40;
        v.burst = int(rnd() & 0x1F) + 0x10;
        v.bias -= float(int(v.bias) >> 2);
        return;
    }
    if (--v.burst & 7) return;
    const Vec3 muzzle = gunPoint(world, v);
    const Vec3 d = world.nearest(v.lastKnown, muzzle) - muzzle;
    {
        const float ry = -wrapAngle(std::atan2(d.x, -d.z) - o.heading);
        const float rp = std::atan2(d.y, shots.df2() ? std::sqrt(d.x * d.x + d.z * d.z)
                                                     : std::floor(std::sqrt(std::floor(d.x) * std::floor(d.x) + std::floor(-d.z) * std::floor(-d.z)))) - o.pitch;
        // GOG Df2.exe 0x42bccf: no limits for the other types.
        if (type != HeloType::Other && (ry < (hawk ? -135 * deg : -45 * deg) || ry > (hawk ? -45 * deg : 45 * deg) ||
            rp < (hawk ? -float(0x0C000000) * turn : -45 * deg) || rp > 0))
            return;
    }
    auto err = [&]() { return float(int((rnd() & 0xFFFF) << 6) - 0x200000) * turn; };
    // DF2 vehicle guns fire mode 53 (.50, ammo 34; 0x41fac0); DF1 mode 12.
    const int gunMode = shots.df2() ? 53 : 12;
    const ModeDef& md0 = shots.modeAt(gunMode);
    const GunDef& g0 = shots.gunAt(md0.gun);
    auto spread = [&]() { return float((0x8000 - int(rnd())) * (g0.accuracy - 1) * 30) * turn; };
    // OpenDF heading is clockwise: the DF yaw bias and spread turn the other way.
    const float yaw = std::atan2(d.x, -d.z) - err() - v.bias * turn - spread();
    // Delta Force (GOG Df.exe 0x450830): the flat range as 0x450710's.
    const float flatRange = shots.df2() ? std::sqrt(d.x * d.x + d.z * d.z)
                                        : std::floor(std::sqrt(std::floor(d.x) * std::floor(d.x) + std::floor(-d.z) * std::floor(-d.z)));
    const float pitch = std::atan2(d.y, flatRange) + err() + spread();
    const ModeDef& md = shots.modeAt(gunMode);
    const GunDef& g = shots.gunAt(md.gun);
    Projectile p;
    p.pos = muzzle;
    p.vel = Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)} * (float(g.velocity) * 60.f / 62.f);
    p.lifetime = g.lifetime;
    p.power = g.power;
    p.falloff = std::max(1, g.falloff);
    p.flags = g.flags;
    p.from = o.pos;
    p.shooter = v.object;
    if (shots.df2()) p.gun = md.gun;
    shots.addProjectile(p);
    // The round goes through the soldiers' spawn as the BRDM's (GOG Df.exe
    // 0x450830 -> 0x45ea00 -> 0x46fb40 -> 0x462f20; Df2.exe 0x416a30 ->
    // 0x407000 -> 0x451ae0): the sound and a noise of half its range
    // (0x4630fd, DF1 only when the range is not 0), the gunner marked firing.
    if (g.sounds[0] > 0) {
        sound.play(g.sounds[0], muzzle);
        const float range = sound.range(g.sounds[0]);
        if (shots.df2() || range > 0) ai.noise(muzzle, range * 0.5f, o.side, 2);
        std::fprintf(stderr, "DBGDG helo %d fires, noise %.1f\n", v.object, range * 0.5f);
    }
    if (!(g.flags & 0x80)) ai.markFired(v.object);
}

// The BRDM's gun point (GOG Df2.exe 0x4243a0): Obj * T(hp0) * Rz(turret
// yaw) * Ry(turret pitch) * T(-hp0) * hp5, in the model's own axes.
Vec3 VehicleSystem::brdmGunPoint(const World& world, const Vehicle& v) const {
    const WorldObject& o = world.objects[size_t(v.object)];
    if (!o.model || o.model->points.size() <= 5) return o.pos + Vec3{0, 2.f, 0};
    const auto& p0 = o.model->points[0];
    const auto& p5 = o.model->points[5];
    Mat4 gp;
    const float c = std::cos(v.gunPitch), sn = std::sin(v.gunPitch);
    gp.at(0, 0) = c; gp.at(0, 1) = -sn; gp.at(1, 0) = sn; gp.at(1, 1) = c;
    const Mat4 m = o.matrix(o.pos) * translate({p0[0], p0[1], p0[2]}) * rotateY(v.gunYaw) * gp * translate({-p0[0], -p0[1], -p0[2]});
    return {m.at(0, 0) * p5[0] + m.at(0, 1) * p5[1] + m.at(0, 2) * p5[2] + m.at(0, 3),
            m.at(1, 0) * p5[0] + m.at(1, 1) * p5[1] + m.at(1, 2) * p5[2] + m.at(1, 3),
            m.at(2, 0) * p5[0] + m.at(2, 1) * p5[1] + m.at(2, 2) * p5[2] + m.at(2, 3)};
}

// The BRDM's turret, every veh0 think (GOG Df2.exe 0x423a1e..0x423a64),
// unless ai flag 1. A target seen dead (ai+0x34 0x40) is dropped during a
// pause; without one the soldiers' scan (0x417830, as AiSystem::scan: score
// x perception, x2 on hard for side 2, / 4 when relaxed) is drawn against
// rand (0x424680), which also sets the last known point, a pause of
// 32..95 and a yaw bias of +-(0x1000000 + ((rand & 0xffff) - 0x8000) << 8)
// x (ai+0x80 >> 1) >> 16. Tracking (0x424780): once ai+0x50 passes 0xC0 a
// sight line from 1 m up sets or clears ai+0x34 0x80; while it is set the
// last known point follows the target (and 0x40 notes it dead). Aim
// (0x424470): that point from the gun point, relative to the hull, yaw
// within +-0x60000000 and pitch within -0x4000000..0x20000000.
void VehicleSystem::brdmThink(World& world, AiSystem& ai, Vehicle& v) {
    WorldObject& o = world.objects[size_t(v.object)];
    if (v.flags & 1) return;
    const float turn = 2 * kPi / 4294967296.f;
    auto teamOf = [&](int t) { return t == -3 ? player_.team : world.objects[size_t(t)].team; };
    if (v.gunTarget != -1) {
        if (v.deadSeen && v.pause > 0) v.gunTarget = -1;
    } else if (o.side != 0 || v.everyone) {
        int id = -1;
        Vec3 at{0, 0, 0};
        const Vec3 eye = o.pos + Vec3{0, 1.f, 0};
        float score = ai.bestTarget(world, v.object, eye, o.heading, o.side, v.fovHalf, player_, id, at, v.everyone);
        // 0x403790: at night a perception above 0x4ccc loses 0x2666.
        const float perception = (ai.night && v.perception > 0.3f) ? v.perception - 0.15f : v.perception;
        score *= perception * ((ai.hard && o.side == 2) ? 2.f : 1.f);
        if (v.alert == 0) score *= 0.25f;
        if (id != -1 && float(rnd()) < score * 65536.f) {
            v.gunTarget = id;
            v.lastKnown = at;
            ai.markSpotted(o.team, teamOf(id));
            v.deadSeen = false;   // 0x80 (in sight) is left as it was
            v.pause = int(rnd() & 0x3F) + 0x20;
            const int r = (int(rnd() & 0xFFFF) - 0x8000) * 0x100;
            const int64_t b = int64_t(r < 0 ? r - 0x1000000 : r + 0x1000000) * int64_t(v.aimScale * 65536.f);
            v.bias = float(int32_t(b >> 16));
        }
    }
    if (v.gunTarget == -1) return;
    // Tracking (0x424780).
    if (v.losTimer > 0xC0) {
        v.losTimer = 0;
        v.seen = ai.sightLine(world, o.pos + Vec3{0, 1.f, 0}, targetPosition(world, v.gunTarget), v.object, v.gunTarget) >= 1;
        if (v.seen) ai.markSpotted(o.team, teamOf(v.gunTarget));
    }
    if (v.seen) {
        if (!targetAlive(world, v.gunTarget)) v.deadSeen = true;
        v.lastKnown = targetPosition(world, v.gunTarget);
    }
    // Aim (0x424470), the horizontal distance in whole metres.
    const Vec3 d = world.nearest(v.lastKnown, o.pos) - brdmGunPoint(world, v);
    const float flat = std::floor(std::sqrt(std::floor(d.x) * std::floor(d.x) + std::floor(d.z) * std::floor(d.z)));
    const float yaw = -wrapAngle(std::atan2(d.x, -d.z) - o.heading);
    const float pitch = std::atan2(d.y, flat) - o.pitch;
    v.gunTgtYaw = std::clamp(yaw, -float(0x60000000) * turn, float(0x60000000) * turn);
    v.gunTgtPitch = std::clamp(pitch, -float(0x4000000) * turn, float(0x20000000) * turn);
}

// The BRDM's turret every tick (end of the veh0 mover, GOG Df2.exe
// 0x423ac0): yaw and pitch turn 0x200000 toward their targets when more
// than 0x400000 off; the post-shot timer counts down; with a target and the
// yaw within 0x10000000 of its aim: the pause counts down, then the burst,
// a round whenever the decremented burst is a multiple of 16; when both are
// out a new pause 64..127, a burst 16..47 and the bias x 3/4. A round
// (0x424550): from the gun point at the last known point, refused unless
// within +-0x60000000 yaw and -0x4000000..0x20000000 pitch of the hull;
// +-0x200000 error on each and the bias on yaw; mode 0x35 (0x416a30: the
// gun's spread (accuracy - 1) x (0x8000 - rand) x 30 per axis, then the
// round with its sound and noise, 0x451ae0); the timer 10.
void VehicleSystem::brdmTurret(World& world, AiSystem& ai, Vehicle& v, Arsenal& shots, SoundSystem& sound) {
    WorldObject& o = world.objects[size_t(v.object)];
    const float turn = 2 * kPi / 4294967296.f;
    const float step = float(0x200000) * turn, deadBand = float(0x400000) * turn;
    if (std::abs(v.gunTgtYaw - v.gunYaw) > deadBand) v.gunYaw += v.gunTgtYaw > v.gunYaw ? step : -step;
    if (std::abs(v.gunTgtPitch - v.gunPitch) > deadBand) v.gunPitch += v.gunTgtPitch > v.gunPitch ? step : -step;
    o.gunYaw = v.gunYaw;
    o.gunPitch = v.gunPitch;
    if (o.shotFlash != 0) --o.shotFlash;   // obj+0xC8, the post-shot timer
    if (v.gunTarget == -1 || std::abs(v.gunTgtYaw - v.gunYaw) >= float(0x10000000) * turn) return;
    if (v.pause > 0) { --v.pause; return; }
    if (v.burst <= 0) {
        v.pause = int(rnd() & 0x3F) + 0x40;
        v.burst = int(rnd() & 0x1F) + 0x10;
        v.bias -= float(int(v.bias) >> 2);
        return;
    }
    if (--v.burst & 0xF) return;
    const Vec3 muzzle = brdmGunPoint(world, v);
    const Vec3 d = world.nearest(v.lastKnown, muzzle) - muzzle;
    const float flat = std::floor(std::sqrt(std::floor(d.x) * std::floor(d.x) + std::floor(d.z) * std::floor(d.z)));
    float yaw = std::atan2(d.x, -d.z), pitch = std::atan2(d.y, flat);
    {
        const float ry = -wrapAngle(yaw - o.heading), rp = pitch - o.pitch;
        if (std::abs(ry) > float(0x60000000) * turn || rp < -float(0x4000000) * turn || rp > float(0x20000000) * turn) return;
    }
    auto err = [&]() { return float((int(rnd() & 0xFFFF) - 0x8000) * 0x40) * turn; };
    // OpenDF heading is clockwise: DF yaw offsets turn the other way.
    yaw -= err();
    pitch += err();
    yaw -= v.bias * turn;
    const ModeDef& md = shots.modeAt(53);
    const GunDef& g = shots.gunAt(md.gun);
    if (g.accuracy != 1) {
        yaw -= float((g.accuracy - 1) * (0x8000 - int(rnd())) * 0x1e) * turn;
        pitch += float((g.accuracy - 1) * (0x8000 - int(rnd())) * 0x1e) * turn;
    }
    Projectile p;
    p.pos = muzzle;
    p.vel = Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)} * (float(g.velocity) * 60.f / 62.f);
    p.lifetime = g.lifetime;
    p.power = g.power;
    p.falloff = std::max(1, g.falloff);
    p.flags = g.flags;
    p.from = o.pos;
    p.shooter = v.object;
    p.gun = md.gun;
    shots.addProjectile(p);
    // 0x451ae0: the gun's sound at the muzzle and a noise of half its range.
    // Delta Force's rounds all go through GOG Df.exe 0x462f20: the noise only
    // when the sound's range is not 0 (0x4630d6), the shooter marked unless
    // the gun has flag 0x80 (0x463119), with or without a sound. DF2 (GOG
    // Df2.exe 0x451ae0): the noise for any round with a sound; the gunner
    // counts as firing unless the ammo is suppressed (CD flag 0x80; +0x64
    // 0x4000 per round, sound or not, below).
    if (shots.df2()) {
        if (g.sounds[0] > 0) {
            sound.play(g.sounds[0], muzzle);
            ai.noise(muzzle, sound.range(g.sounds[0]) * 0.5f, o.side, 2);
        }
    } else {
        if (g.sounds[0] > 0) {
            sound.play(g.sounds[0], muzzle);
            if (const float range = sound.range(g.sounds[0]); range > 0) ai.noise(muzzle, range * 0.5f, o.side, 2);
        }
        if (!(g.flags & 0x80)) ai.markFired(v.object);
    }
    // DF2: the gunner counts as firing unless the ammo is suppressed (CD
    // flag 0x80; 0x451ae0 sets +0x64 0x4000 per round, sound or not).
    if (shots.df2() && !(g.flags & 0x80)) ai.markFired(v.object);
    o.shotFlash = 10;
}

void VehicleSystem::tick(World& world, AiSystem& ai, Arsenal& shots, SoundSystem& sound, const AiSystem::PlayerView& player) {
    player_ = player;
    for (auto& v : vehicles_) {
        WorldObject& o = world.objects[size_t(v.object)];
        if (o.removed) continue;
        // A hit does not alert a vehicle: GOG Df.exe 0x464c50 (CD 0x44f184)
        // writes ai+0x9c only for organics (item kind 3).
        v.lastHp = o.hp;
        if (!v.dead && (o.wrecked || (o.hp <= 0 && o.item && o.item->hp > 0))) {
            // Destroyed (0x480357 / 0x47c83c): the blast (shrapnel 60, or for a
            // helicopter 40 plus two of 10 to the sides), speed to zero.
            v.dead = true;
            v.targetSpeed = 0;
            if (v.kind == Kind::Car) {
                // gnrl: no blast.
            } else if (v.kind == Kind::Boat && bigBlast) {
                // DF2.EXE 0x401f60 (GOG 0x4021de..0x4022a8): five big blasts 1 m
                // up at the boat and 5 m to each side, the boat's yaw with each.
                const Vec3 c = o.pos + Vec3{0, 1, 0};
                for (const Vec3 d : {Vec3{0, 0, 0}, Vec3{5, 0, 0}, Vec3{-5, 0, 0}, Vec3{0, 0, -5}, Vec3{0, 0, 5}}) bigBlast(c + d, o.heading, o.killer);   // owner: +0xF8
                o.tickVel = {0, 0, 0};   // then +0x3C..+0x50 are zeroed (6 dwords)
            } else if (detonate) {
                const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)};
                if (v.helo) {
                    detonate(o.pos + Vec3{0, 1, 0}, 40, o.heading);
                    detonate(o.pos + fwd * 4.f, 10, o.heading);
                    detonate(o.pos - fwd * 4.f, 10, o.heading);
                } else {
                    detonate(o.pos + Vec3{0, 1, 0}, 60, o.heading);
                    if (wreckSmoke) wreckSmoke(v.object);
                }
            }
        }
        const float teamSpeed = ai.teamSpeedMs(o.team);
        static int dbg = 0;
        if (std::getenv("OPENDF_DEBUGVEH") && (++dbg % (120 * int(vehicles_.size()))) < int(vehicles_.size()))
            std::fprintf(stderr, "veh %d %s team %d path %d wp %d speed %.1f/%.1f pos (%.1f %.1f %.1f) hdg %.0f rotor %.0f mode %d gun %d %.0f/%.0f\n",
                         v.object, v.helo ? "helo" : "veh0", o.team, v.path, v.waypoint, v.speed, teamSpeed, o.pos.x, o.pos.y, o.pos.z,
                         o.heading * 57.3f, v.rotor / 1e6f, v.mode, v.gunTarget, v.gunYaw * 57.3f, v.gunTgtYaw * 57.3f);
        if (v.helo) {
            fly(world, ai, v, teamSpeed);
            doorGun(world, ai, v, shots, sound);
        } else if (v.kind == Kind::Loco || v.kind == Kind::Car) {
            driveTrain(world, ai, v, teamSpeed);
        } else {
            drive(world, ai, v, teamSpeed);
            if (v.brdm && !v.dead) brdmTurret(world, ai, v, shots, sound);
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
            // GOG Df2.exe 0x42c2da..0x42c34b: by graphic number, 502 for the
            // gazelle 2018, Havoc 2005, Hind 2028 and Ka-29 2029, 503 for the
            // Black Hawk 2021, and no loop for any other helicopter.
            int loop = (o.item && o.item->id == 100000 + kGazelle) ? 502 : 503;
            if (world.df2Rules) {
                const int k = o.item ? o.item->id - 100000 : 0;
                loop = (k == 2018 || k == 2005 || k == 2028 || k == 2029) ? 502 : k == 2021 ? 503 : 0;
            }
            if (v.rotor > 0 && loop) {
                const float pitch = std::clamp((v.rotor / 8192.f + 0x5000) / 65536.f, 0x6000 / 65536.f, 0x12000 / 65536.f);
                sound.setLoop(100000 + v.object, loop, o.pos, pitch);
            }
        } else if (v.kind == Kind::Car) {
        } else if (v.kind == Kind::Loco) {
            // The Train loop (808) while moving (0x401340).
            if (std::abs(v.speed) > 0 && !v.dead) sound.setLoop(100000 + v.object, 808, o.pos, 1.f);
        } else if (std::abs(v.speed) > 0 && !v.dead) {
            // Engine loop 506, pitch 1 + v / team speed (0x48026d).
            const float ts = ai.teamSpeedMs(o.team);
            sound.setLoop(100000 + v.object, 506, o.pos, ts > 0 ? 1.f + v.speed / ts : 1.f);
        }
    }
}

void VehicleSystem::property(const World& world, int team, int subtype, int value) {
    // GOG Df.exe 0x45ff20 (Df2.exe 0x4185b0) on a vehicle's AI block. The
    // helicopter reads flags bit 1 (no targeting) and 2 (100 m leash, home),
    // the alert byte, field of view and perception; the BRDM (DF2) also the
    // accuracy (ai+0x80). The rest (3 / 4 set flags 0x10 / 0x20) is kept for
    // completeness. Team 0 is
    // skipped (0x4601a0).
    if (team == 0) return;
    for (auto& v : vehicles_) {
        if (world.objects[size_t(v.object)].team != team) continue;
        switch (subtype) {
        case 2: if (value) v.flags |= 2; else v.flags &= ~2u; break;
        case 3: if (value) v.flags |= 0x10; else v.flags &= ~0x10u; break;
        case 4: if (value) v.flags |= 0x20; else v.flags &= ~0x20u; break;
        case 5: v.alert = 2; break;
        case 6: v.alert = 0; break;
        case 7: {
            // (value << 16) / 360, then the scan's half angle (>> 9 | 1) in 1/256 turns.
            const int fov = (value << 16) / 0x168;
            v.fovHalf = float((fov >> 9) | 1) / 256.f * 2 * kPi;
            break;
        }
        case 8:
            if (value != 0) {
                v.accuracy = value;
                v.aimScale = float(int(65536.0 * (1.0 / (double(value) * 0.01))) >> 1) / 65536.f;
            }
            break;
        case 9: v.perception = float((value << 16) / 100) / 65536.f; break;
        case 14: v.silent = value != 0; break;
        case 15: if (value) v.flags |= 1; else v.flags &= ~1u; break;
        default: break;
        }
    }
}

void VehicleSystem::followPath(World& world, int team, int path) {
    // 0x46bbe8 (GOG Df.exe 0x45f670) also sends vehicles of the team down the
    // path, from the nearest point; path 0 included (it halts them, pathStep).
    if (team < 3 || !mission_ || path < 0 || path >= int(mission_->paths.size())) return;
    const MissionPath& mp = mission_->paths[size_t(path)];
    for (auto& v : vehicles_) {
        WorldObject& o = world.objects[size_t(v.object)];
        if (o.team != team || v.dead) continue;
        v.pathMode = 1;
        v.path = path;
        v.stopped = false;
        // No think timer change: the vehicle reacts at its next think.
        v.waypoint = nearestPathPoint(world, *mission_, mp, o.pos);
    }
}

} // namespace df
