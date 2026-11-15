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

// 0x41e880 of |dx|, |dy|, |dz| in 16.16: max + 11/32 mid + 1/4 min, with
// the EXE's 32-bit wrap (done unsigned) and arithmetic shifts.
int32_t approx16(Vec3 v) {
    auto fx = [](float m) { return int32_t(std::lround(double(m) * 65536.0)); };
    int32_t a = std::abs(fx(v.x)), b = std::abs(fx(v.z)), c = std::abs(fx(v.y));
    if (a < b) std::swap(a, b);
    if (a < c) std::swap(a, c);
    if (b < c) std::swap(b, c);
    const int32_t mid = int32_t(uint32_t(b) * 11u) >> 5;
    return int32_t(uint32_t(a) + uint32_t(mid) + uint32_t(c >> 2));
}

} // namespace

void AiSystem::init(World& world, const Mission& mission) {
    mission_ = &mission;
    people_.clear();
    noise_.clear();
    teamAlert_.fill(0);
    spotted_.fill(0);
    engaging_.fill(0);   // GOG Df2.exe 0x41b620 clears 0xab3a00 at load
    reached_.clear();
    teamSpeed_.fill(0);
    ticks_ = 0;
    playerFired_ = false;
    int stagger = 0;
    for (size_t i = 0; i < world.objects.size(); ++i) {
        WorldObject& o = world.objects[i];
        if (!o.person || !o.item || o.item->aiFunction != "org1" || o.missionIndex < 0 || o.removed) continue;
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
        // An alert byte of 2 from the record does not alert the team: the
        // team word is written only by GOG Df.exe 0x44a3e0 (Df2.exe 0x4045c0),
        // never at load (0x449710 / 0x403790).
        o.weaponMode = p.weapon;
        people_.push_back(p);
        world.requestMove(o, idleMove(world, p));
    }
    if (std::getenv("OPENDF_DEBUGAI")) {
        std::fprintf(stderr, "ai: %zu soldiers\n", people_.size());
        for (const auto& p : people_) {
            const WorldObject& o = world.objects[size_t(p.object)];
            std::fprintf(stderr, "  soldier %d side %d weapon %d at %.1f east %.1f north\n", p.object, o.side, p.weapon, o.pos.x, -o.pos.z);
        }
    }
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


void AiSystem::markFired(int object) {
    // obj+0x6d |= 0x40 (GOG 0x4631ef): x6 visibility until 0x445c80 clears it.
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
                           int& bestId, Vec3& bestPos, bool everyone, float* nearPlayer, Vec3 bodyAt, bool coverMe,
                           int* deadFriendAlert) const {
    float best = 0;
    bestId = -1;
    struct { float heading; int side; } self{heading, side};
    // DF2's dead-friend check, inside the scan (GOG Df2.exe 0x417830): for
    // every dead person of the scanner's own side (not with ai flag 0x200,
    // whose same-side people are targets) while the alert read at the scan's
    // start is not 2: the sight line between the body origins 1..2 and
    // max + 11/32 min of |dx|, |dy| (0x41e880(|dx|, |dy|, 0), no wrap)
    // under 50 m alert the scanner (the caller adds the team alert and bark
    // 1, which the EXE queues once per body). No break: every body is
    // checked against the start-of-scan alert.
    if (df2 && deadFriendAlert && !everyone && *deadFriendAlert != 2 && selfId >= 0) {
        const WorldObject& me = world.objects[size_t(selfId)];
        auto fx = [](float m) { return int32_t(std::lround(double(m) * 65536.0)); };
        for (size_t qi = 0; qi < world.objects.size(); ++qi) {
            const WorldObject& q = world.objects[qi];
            if (int(qi) == selfId || !q.person || !q.dead || q.removed || q.side != side) continue;
            int32_t a = std::abs(fx(q.pos.x - me.pos.x)), b = std::abs(fx(q.pos.z - me.pos.z));
            if (a < b) std::swap(a, b);
            if (a + int32_t(uint32_t(b) * 11u >> 5) >= 0x320000) continue;
            if (const int r = sightLine(world, me.pos, q.pos, selfId, int(qi)); r < 1 || r > 2) continue;
            *deadFriendAlert = 2;
        }
    }
    // DF2's person scan (0x417830; Vehicles' scans pass no dead-friend alert).
    const bool org = df2 && deadFriendAlert;
    auto consider = [&](int id, Vec3 at, int side, int move, bool fired, bool targetsPlayer) {
        if (std::getenv("OPENDF_DEBUGAI2")) std::fprintf(stderr, "scan %d side %d cand %d side %d\n", selfId, self.side, id, side);
        // DF2 ai flag 0x200: everyone is a target, own side and side 0 too.
        if (!everyone && (side == 0 || side == self.side)) return;
        const Vec3 tp = world.nearest(at, eye);
        float diff = headingTo(eye, tp) - self.heading;
        diff -= std::floor(diff / (2 * kPi) + 0.5f) * 2 * kPi;
        if (std::abs(diff) > half) return;
        // GOG Df2.exe 0x417830: for the player-controlled candidate (flags
        // & 1) in the field of view, before the 1000 m and sight tests, the
        // least of max + 11/32 mid + 1/4 min (0x41e880) of |dx|, |dy|, |dz|
        // between the body origins, in whole metres.
        if (nearPlayer && id == -3) {
            auto fx = [](float m) { return int32_t(std::lround(double(m) * 65536.0)); };
            int32_t a = std::abs(fx(tp.x - bodyAt.x)), b = std::abs(fx(tp.z - bodyAt.z)), c = std::abs(fx(tp.y - bodyAt.y));
            if (a < b) std::swap(a, b);
            if (a < c) std::swap(a, c);
            if (b < c) std::swap(b, c);
            // 32-bit wrap as the EXE's imul / add (past ~2979 m on the middle
            // axis the sum goes negative there too), done unsigned to avoid UB;
            // then arithmetic shifts (sar) as 0x41e880 and 0x417830's >> 0x10.
            const int32_t mid = int32_t(uint32_t(b) * 11u) >> 5;
            const int32_t sum = int32_t(uint32_t(a) + uint32_t(mid) + uint32_t(c >> 2));
            const float m = float(sum >> 16);
            if (m < *nearPlayer) *nearPlayer = m;
        }
        // DF2's person scan (GOG Df2.exe 0x417830): the distance is the
        // whole metres of 0x41e880(|dx|, |dy|, |dz|) between the body
        // origins (the +1 m eye only for the sight line), 100 / d as 16.16
        // (100 at 0), under 1000.
        float d, inv;
        if (org) {
            const int32_t m = approx16(tp - bodyAt) >> 16;
            if (m >= 1000) return;
            d = float(m);
            inv = float(m == 0 ? 0x640000 : 0x640000 / m) / 65536.f;
        } else {
            d = len3(tp - eye);
            if (d >= 1000.f || d < 0.01f) return;
            inv = 100.f / d;
        }
        if (std::getenv("OPENDF_DEBUGAI2")) std::fprintf(stderr, "  in fov, d %.1f\n", d);
        // Visible: 1 or 2, i.e. at most one see-through face (0x46a804).
        if (const int r = sightLine(world, eye, tp, selfId, id); r < 1 || r > 2) return;
        if (std::getenv("OPENDF_DEBUGAI2")) std::fprintf(stderr, "  visible\n");
        float vis = moveVisibility(move);
        // DF2's ghillie suit (0x415a69): the player in it (kit 32), crouched
        // or prone (visibility <= 0.5) and not firing lately: unseen beyond
        // 50 m, half as visible within.
        // (0x417830 tests the candidate's ai+0xb8, set for the player only
        // outside multiplayer; the cut-off is whole metres d < 0x33.)
        if (df2 && id == -3 && playerGear == 32 && vis <= 0.5f && !fired) {
            if (org ? d >= 51.f : d > 50.f) return;
            vis *= 0.5f;
        }
        float score = (half - std::abs(diff)) / half * inv * vis * (fired ? 6.f : 1.f);
        // DF2 "cover me" (GOG Df2.exe 0x417830, after the x6 term): with the
        // scanner's order (+0xbb) 2, a candidate whose AI target is the
        // player scores twice.
        if (coverMe && targetsPlayer) score *= 2.f;
        if (score > best) best = score, bestId = id, bestPos = tp;
    };
    if (player.alive) consider(-3, player.origin, player.side, player.moveNumber, playerFired_, false);
    for (size_t i = 0; i < world.objects.size(); ++i) {
        const WorldObject& o = world.objects[i];
        if (!o.person || !o.alive() || int(i) == selfId) continue;
        const Person* q = nullptr;
        for (const auto& x : people_)
            if (x.object == int(i)) q = &x;
        consider(int(i), o.pos, o.side, o.moveNumber, q && q->fired, q && q->target == -3);
    }
    return best;
}

// The searchlight's scan (DF2.EXE 0x425c70 over the 256 soldier slots):
// skip side 0, the light's own side (+0x68, mission byte 0x43; GOG Df2.exe
// 0x42aa10 compares the slot's +0x68 with the light's) and the dead; the target's yaw from the
// light's current heading and its pitch from the light's pitch must both be
// within 0x10/256 turn (22.5 degrees); distance under 1000 m; the sight line
// 0x406180 gives 1..2. score = (100 / d) · (16 - |dyaw|) / 16 (dyaw in 1/256
// turns) · move visibility (move table +0x10, 0x425df3..0x425e05) · 6 if
// it fired (+0x64 & 0x4000, set at 0x44a9dc; 0x425e09..0x425e1f) · 2
// unless +0x64 & 1, the player-controlled slot (0x447833; 0x425e5e).
// Returns the best score, 0 if none.
float AiSystem::lightScan(const World& world, int self, Vec3 at, uint32_t yaw, int32_t pitch, int side, const PlayerView& player,
                          int& bestId, Vec3& bestPos) const {
    float best = 0;
    bestId = -1;
    auto consider = [&](int id, Vec3 pos, int tside, int move, bool fired, bool isPlayer) {
        if (tside == 0 || tside == side) return;
        const Vec3 tp = world.nearest(pos, at);
        const Vec3 d = tp - at;
        const float horiz = std::sqrt(d.x * d.x + d.z * d.z);
        const uint32_t ty = uint32_t(int64_t(std::llround(double(std::atan2(-d.z, d.x)) / (2 * kPi) * 4294967296.0)));
        const int32_t tpi = int32_t(int64_t(std::llround(double(std::atan2(d.y, horiz)) / (2 * kPi) * 4294967296.0)));
        const int dyaw = std::abs(int32_t(ty - yaw)) >> 24, dpitch = std::abs(int32_t(uint32_t(tpi) - uint32_t(pitch))) >> 24;
        if (dyaw > 0x10 || dpitch > 0x10) return;
        // Whole metres of 0x41e880(|dx|, |dy|, |dz|) from the light's
        // origin, 100 / d as 16.16 (100 at 0), under 1000 (GOG 0x42aa10).
        const int32_t dist = approx16(d) >> 16;
        if (dist >= 1000) return;
        if (const int r = world.sight(at, tp, self, id); r < 1 || r > 2) return;
        float score = float(dist == 0 ? 0x640000 : 0x640000 / dist) / 65536.f * float(16 - dyaw) / 16.f * moveVisibility(move) * (fired ? 6.f : 1.f) * (isPlayer ? 1.f : 2.f);
        if (score > best) best = score, bestId = id, bestPos = tp;
    };
    if (player.alive) consider(-3, player.origin, player.side, player.moveNumber, playerFired_, true);
    for (size_t i = 0; i < world.objects.size(); ++i) {
        const WorldObject& o = world.objects[i];
        if (!o.person || !o.alive() || int(i) == self) continue;
        const Person* q = nullptr;
        for (const auto& x : people_)
            if (x.object == int(i)) q = &x;
        consider(int(i), o.pos, o.side, o.moveNumber, q && q->fired, false);
    }
    return best;
}

// GOG Df.exe 0x44f540 (CD 0x47b074): over the 256 people slots, skipping
// the empty, those of the light's side (+0x70) or side 0 and the dead. In
// DF's axes (x east, y north, z up, 16.16): the yaw atan(dx, dy) less the
// light's yaw and the pitch atan(isqrt(whole-metre dx^2 + dy^2), dz) less
// its pitch, each folded to 0..128 in 1/256 turns, at most 0x10; the length
// max + 11/32 mid + 1/4 min (0x477670) in whole metres under 1000; the
// sight line from the light's origin (0x42e2b0) 1..2. Score (16.16): 100 /
// metres (100 at 0) x (16 - dyaw) / 16 x move visibility x 6 if it fired
// (+0x6d & 0x40) x 2 unless the player's slot (+0x6c & 1), the best kept
// when strictly higher. The invisibility cheat (0x67dbc9 & 8) is not
// modelled. Returns the score as a fraction (0x10000 = 1).
float AiSystem::lightScanDf1(const World& world, int self, Vec3 at, uint32_t yaw, int32_t pitch, int side, const PlayerView& player,
                             int& bestId) const {
    auto fx = [](float m) { return int32_t(std::lround(double(m) * 65536.0)); };
    auto ang = [](double y, double x) { return uint32_t(int64_t(std::llround(std::atan2(y, x) / (2 * kPi) * 4294967296.0))); };
    auto fold = [](uint32_t a) { uint32_t v = a >> 24; return v > 0x7f ? 0x100 - v : v; };
    int64_t best = 0;
    bestId = -1;
    auto consider = [&](int id, Vec3 pos, int tside, int move, bool fired, bool isPlayer) {
        if (tside == 0 || tside == side) return;
        const Vec3 tp = world.nearest(pos, at);
        const int32_t dx = fx(tp.x - at.x), dy = fx(at.z - tp.z), dz = fx(tp.y - at.y);
        const uint32_t dyaw = fold(ang(dy, dx) - yaw);
        if (dyaw > 0x10) return;
        const int64_t hx = dx >> 16, hy = dy >> 16;
        const int64_t horiz = std::llround(std::sqrt(double(hx * hx + hy * hy)));
        if (fold(ang(dz, double(horiz << 16)) - uint32_t(pitch)) > 0x10) return;
        int32_t a = std::abs(dx), b = std::abs(dy), c = std::abs(dz);
        if (a < b) std::swap(a, b);
        if (a < c) std::swap(a, c);
        if (b < c) std::swap(b, c);
        const int32_t d = (a + (b * 11 >> 5) + (c >> 2)) >> 16;
        const int64_t inv = d == 0 ? 0x640000 : 0x640000 / d;
        if (d >= 1000) return;
        if (const int r = world.sight(at, tp, self, id); r < 1 || r > 2) return;
        int64_t s = inv * int64_t((int32_t(16 - dyaw) * 0x10000) >> 4) >> 16;
        s = s * int64_t(std::lround(double(moveVisibility(move)) * 65536.0)) >> 16;
        s = s * (fired ? 0x60000 : 0x10000) >> 16;
        if (!isPlayer) s *= 2;
        if (s > best) best = s, bestId = id;
    };
    if (player.alive) consider(-3, player.origin, player.side, player.moveNumber, playerFired_, true);
    for (size_t i = 0; i < world.objects.size(); ++i) {
        const WorldObject& o = world.objects[i];
        if (!o.person || !o.alive() || int(i) == self) continue;
        const Person* q = nullptr;
        for (const auto& x : people_)
            if (x.object == int(i)) q = &x;
        consider(int(i), o.pos, o.side, o.moveNumber, q && q->fired, false);
    }
    return float(best) / 65536.f;
}

float AiSystem::scanScore(const World& world, const Person& p, const PlayerView& player, int& bestId, Vec3& bestPos, float* nearPlayer,
                          bool* deadFriend) const {
    const WorldObject& self = world.objects[size_t(p.object)];
    const Vec3 eye = self.pos + Vec3{0, 1.f, 0};
    const float half = float((int(p.fov * 65536.f / 360.f) >> 9) | 1) / 256.f * 2 * kPi;   // 1/256 turns
    bestId = -1;
    bestPos = Vec3{0, 0, 0};
    // DF2 (GOG Df2.exe 0x417830): nothing with ai flag 1, nor for a side-0
    // soldier without flag 0x200.
    if (df2 && ((p.flags & 1) || (self.side == 0 && !(p.flags & 0x200)))) return 0;
    int alert = p.alert;
    const float best = bestTarget(world, p.object, eye, self.heading, self.side, half, player, bestId, bestPos, df2 && (p.flags & 0x200),
                                  nearPlayer, self.pos, df2 && p.order == 2, df2 ? &alert : nullptr);
    if (deadFriend) *deadFriend = alert != p.alert;
    float score = best * p.perception * ((hard && self.side == 2) ? 2.f : 1.f);
    // Only when relaxed (0x46a896); DF2 reads the alert after the loop, so
    // a body found in this scan already lifts the quarter (GOG Df2.exe 0x417830).
    if (alert == 0) score *= 0.25f;
    return score;
}

// DF2: the scan found a body of the soldier's own side (GOG Df2.exe
// 0x417830): alert, the team alerted, bark 1.
void AiSystem::deadFriendAlert(World& world, Person& p, SoundSystem& sound) {
    p.alert = 2;
    alertTeam(world.objects[size_t(p.object)].team);
    bark(world, p, sound, 1);
}

// Delta Force 2's team share (GOG Df2.exe 0x414350), from org1's radio
// (0x401a69 with a target, 0x401d6b alert without one). Only with alert 2
// and a target (a dead one kept with 0x40 too). Mates: AI people of the same
// side, alive, not deaf (0x1000), |dx|, |dy| <= 50 m and dx^2 + dy^2 <= 2500
// (16.16, no wrap). On the same target: without 0x40 a sender who saw it
// more recently gives his unseen time and last known position (3 when he
// was 0x1f0 ticks fresher); with 0x40 the mate learns it is dead (|= 0x40,
// 2). A mate without a target takes it with alert 2 and the team alerted:
// without 0x40 if the sender's unseen time is under the mate's give-up (1),
// with 0x40 always, the 0x40 too (2). The mate's state is not touched. The
// result is the last mate's.
int AiSystem::teamShare(World& world, Person& p) {
    if (p.alert != 2 || p.target == -1) return 0;
    const WorldObject& o = world.objects[size_t(p.object)];
    auto fx = [](float m) { return int64_t(std::llround(double(m) * 65536.0)); };
    int result = 0;
    for (auto& q : people_) {
        if (&q == &p || (q.flags & 0x1000)) continue;
        const WorldObject& qo = world.objects[size_t(q.object)];
        if (!qo.alive() || qo.side != o.side) continue;
        const int64_t dx = std::abs(fx(qo.pos.x - o.pos.x)), dz = std::abs(fx(qo.pos.z - o.pos.z));
        if (dx > 0x320000 || dz > 0x320000 || (dx * dx >> 16) + (dz * dz >> 16) > 0x9c40000) continue;
        if (q.target == p.target) {
            if (!(p.flags & 0x40)) {
                if (p.unseen < q.unseen) {
                    if (q.unseen - p.unseen > 0x1f0) result = 3;
                    q.unseen = p.unseen;
                    q.lastKnown = p.lastKnown;
                }
            } else if (!(q.flags & 0x40)) {
                result = 2;
                q.flags |= 0x40;
            }
        } else if (q.target == -1) {
            if (!(p.flags & 0x40)) {
                if (p.unseen >= q.giveUpTicks) continue;
                result = 1;
            } else {
                result = 2;
                q.flags |= 0x40;
            }
            q.alert = 2;
            alertTeam(qo.team);
            q.target = p.target;
            q.lastKnown = p.lastKnown;
        }
    }
    return result;
}

bool AiSystem::scan(World& world, Person& p, const PlayerView& player, SoundSystem& sound) {
    const WorldObject& self = world.objects[size_t(p.object)];
    const Vec3 eye = self.pos + Vec3{0, 1.f, 0};
    int bestId = -1;
    Vec3 bestPos{0, 0, 0};
    float nearPlayer = 1024.f;   // GOG Df2.exe 0x417830: local_10 = 0x400
    bool deadFriend = false;
    const float score = scanScore(world, p, player, bestId, bestPos, &nearPlayer, &deadFriend);
    if (deadFriend) deadFriendAlert(world, p, sound);
    // DF2 +0xAC: stored whenever the scan returns a candidate, before the
    // random draw (GOG Df2.exe 0x401de5 alert / 0x401efe relaxed); read for
    // the scan interval at 0x401d92 / 0x401ea5.
    if (df2 && bestId != -1) p.nearestPlayer = nearPlayer;
    if (bestId == -1 || float(rnd()) >= score * 65536.f) return false;
    if (std::getenv("OPENDF_DEBUGAI"))
        std::fprintf(stderr, "ai %d (team %d) spots %d at %.0f m, score %.3f\n", p.object, self.team, bestId, len3(bestPos - eye), score);
    p.target = bestId;
    p.flags &= ~0x40u;
    // DF2's spot (GOG Df2.exe 0x401e05 alert / 0x401f43 relaxed) stores only
    // the target and clears 0x40: the last known position, the unseen and
    // seen times and 0x80 are left to the has-target sight check.
    if (df2) return true;
    p.lastKnown = bestPos;
    p.unseen = 0;
    p.seen = 0;
    p.flags |= 0x80;
    return true;
}

// Path walking (0x459854): head straight for the point; on arrival mark it
// reached and take the next; a one-way path stops at its end facing the
// last point's heading.
void AiSystem::patrol(World& world, Person& p) {
    WorldObject& o = world.objects[size_t(p.object)];
    if (!mission_ || p.path < 0 || p.path >= int(mission_->paths.size())) { p.pathMode = 0; return; }
    const MissionPath& path = mission_->paths[size_t(p.path)];
    // Path 0 (every shipped mission leaves it empty; "follow path 0" halts a
    // team) or an empty path: no point, so path mode ends (GOG Df.exe 0x45c6a0
    // returns 0, 0x45b400 clears ai+0xa4 and thinks again in 16).
    if (p.path == 0 || path.points.empty()) { p.pathMode = 0; p.think = 16; return; }
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
        // GOG Df.exe 0x45b400: the next point is looked up again (0x45c6a0)
        // and the heading set toward it before the 8-tick think. (Delta Force
        // 2's person walker is not located in Df2.exe yet: left as it was.)
        if (const MissionObject* nx = df2 ? nullptr : mission_->inGroup(2, path.points[size_t(p.waypoint)])) {
            const Vec3 nat{std::fmod(std::fmod(nx->x, ext) + ext, ext), 0, std::fmod(std::fmod(-nx->y, ext) + ext, ext)};
            o.heading = headingTo(o.pos, world.nearest(nat, o.pos));
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
    // Muzzle (DF.EXE 0x46ad30, DF2.EXE 0x414f20, the same routine): the held
    // weapon's hardpoint (mode +0x90 / +0x30) at the posed right hand. Both
    // fire nothing without a mode, a held-weapon model or a skeleton
    // (0x46ad94 / 0x414f6b..0x414f7b).
    Vec3 muzzle;
    if (!this->muzzle || !this->muzzle(p.object, o.weaponMode, muzzle)) return;
    if (std::getenv("OPENDF_DEBUGMUZZLE"))
        std::fprintf(stderr, "muzzle %d mode %d off (%.2f %.2f %.2f) heading %.0f\n", p.object, o.weaponMode, muzzle.x - o.pos.x, muzzle.y - o.pos.y,
                     muzzle.z - o.pos.z, o.heading * 57.3f);
    // Aim (DF.EXE 0x46b838, DF2.EXE 0x415de0, the same formula). Two draws
    // r = 2 rnd - 0x10000 come first in both branches (0x415edb / 0x415ee7).
    auto r = [&]() { return float(int(rnd()) - 0x8000) / 32768.f; };
    const float r1 = r(), r2 = r();
    const float turnUnit = 2 * kPi / 4294967296.f;
    float yaw, pitch;
    if (followUp) {
        // Burst follow-ups (counter +0x6c / +0x7c set): the body's heading and
        // pitch plus r1 >> 3 and r2 >> 5 (+-0x2000 / +-0x800 of 2^32), for this
        // round only (0x415f37..0x415f65).
        yaw = o.heading + r1 * 0x2000 * turnUnit;
        pitch = o.aimPitch + r2 * 0x800 * turnUnit;
    } else {
        // At the target's own origin while it is in sight (flag 0x80:
        // 0x46b963 / 0x415e11), else at the last known position with errMul 5;
        // from the body origin, the horizontal distance in whole metres
        // (isqrt((dx >> 16)^2 + (dy >> 16)^2) << 16, 0x46b8a4 / 0x415eb4).
        // Error x errMul: 16 - seconds of sight, down to the target move's aim
        // factor.
        const Vec3 at = (p.flags & 0x80) ? targetPos(world, p, player) : p.lastKnown;
        const Vec3 aim = world.nearest(at, o.pos) - o.pos;
        yaw = std::atan2(aim.x, -aim.z);
        const float flat = std::floor(std::sqrt(std::floor(aim.x) * std::floor(aim.x) + std::floor(aim.z) * std::floor(aim.z)));
        pitch = std::atan2(aim.y, flat);
        int move = 2;
        if (p.target == -3) move = player.moveNumber;
        else if (p.target >= 0) move = world.objects[size_t(p.target)].moveNumber;
        const float f = moveAimFactor(move);
        const float seen = std::min(float(p.seen), (16.f - f) * 62.f);
        const float errMul = (p.flags & 0x80) ? std::max(f, 16.f - seen / 62.f) : 5.f;
        const float unit = float(int(65536.f * 100.f / p.accuracy) * 30 >> 16);
        const float spread = unit * errMul * 65536.f * turnUnit / ((hard && o.side == 2) ? 4.f : 1.f);
        yaw += r1 * spread;
        pitch += r2 * spread / 4.f;
        // The aimed heading and pitch (error included) stay on the soldier
        // (+0x30 / +0x34; DF2 0x415f85).
        o.heading = yaw;
        o.aimPitch = pitch;
    }
    // The gun's own spread, per round (0x46af90 / 0x414ad0).
    {
        const GunDef& gs = shots.gunAt(md.gun);
        if (gs.accuracy > 1) {
            const float dev = 30.f * 0x8000 * turnUnit * float(gs.accuracy - 1);
            yaw += r() * dev;
            pitch += r() * dev;
        }
    }
    const Vec3 dir{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
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
    if (df2) {
        // DF2 (GOG Df2.exe 0x451ae0): the sound and its noise box (0x414660)
        // for any round with a sound; the shooter counts as firing for every
        // round not from suppressed ammo (CD flag 0x80), sound or not (below).
        if (id > 0 && !silentRound) {
            sound.play(id, muzzle);
            noise(muzzle, sound.range(id) * 0.5f, o.side, 2);
        }
    } else {
        // GOG Df.exe's round 0x462f20 with the gun 0x45ea00 picks (a burst's
        // follow-ups the next gun, silent): its sound, the noise only when
        // that sound's range is not 0 (0x4630d6), and the shooter marked as
        // having fired (+0x6d |= 0x40) unless the gun has flag 0x80 (the
        // silenced guns 4, 5, 6, 17; 0x463119).
        int roundGun = md.gun;
        for (const auto& bg : burstGuns)
            if (bg[0] == p.weapon) roundGun = bg[1] + (followUp ? 1 : 0);
        if (id > 0 && !silentRound) {
            sound.play(id, muzzle);
            if (const float range = sound.range(id); range > 0) noise(muzzle, range * 0.5f, o.side, 2);
        }
        if (!(shots.gunAt(roundGun).flags & 0x80)) markFired(p.object);
    }
    // DF2 (GOG Df2.exe 0x451ae0): the shooter counts as firing for every
    // round not from suppressed ammo (CD flag 0x80), sound or not.
    if (shots.df2() && !(g.flags & 0x80)) markFired(p.object);
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

    // DF2 (GOG Df2.exe 0x4018df..0x4018ec): a target kept with 0x40 (seen
    // dead) goes through the no-target logic too.
    if (p.target == -1 || (df2 && (p.flags & 0x40))) {
        if (p.alert != 2) {
            // Hearing (0x46c2c4; DF2 0x413620): inside any noise box -> alert,
            // face it (DF2: not with ai flag 0x1000, deaf; no bark).
            for (const auto& n : noise_) {
                if (df2 && (p.flags & 0x1000)) break;
                const Vec3 c = world.nearest(n.at, o.pos);
                if (std::abs(c.x - o.pos.x) <= n.r && std::abs(c.z - o.pos.z) <= n.r && std::abs(c.y - o.pos.y) <= n.r) {
                    if (std::getenv("OPENDF_DEBUGAI")) std::fprintf(stderr, "ai %d hears a shot\n", p.object);
                    p.alert = 2;
                    alertTeam(o.team);
                    o.heading = headingTo(o.pos, c);
                    if (df2) {
                        // GOG Df2.exe 0x401e76..0x401e95: once the noise
                        // (0x415480) has made him alert, bark 2 unless
                        // silent (+0x34 & 0x100) and think again in 16; no
                        // scan or path this think, the move unchanged.
                        if (!p.silent) bark(world, p, sound, 2);
                        p.think = 16;
                        return;
                    }
                    bark(world, p, sound, 2);
                    break;
                }
            }
            // A dead friend within 50 m in sight alerts (bark 1, ATTACK).
            // DF2 checks bodies inside the scan instead (bestTarget).
            if (p.alert != 2 && !df2)
                for (size_t qi = 0; qi < world.objects.size(); ++qi)
                    if (const auto& q = world.objects[qi]; q.person && q.dead && q.side == o.side && octagonal(q.pos, o.pos) < 50.f &&
                        sightLine(world, o.pos, q.pos, p.object, int(qi)) >= 1 && sightLine(world, o.pos, q.pos, p.object, int(qi)) <= 2) {
                        p.alert = 2;
                        alertTeam(o.team);
                        bark(world, p, sound, 1);
                        break;
                    }
            // Scan every 0x174 ticks (relaxed) or 0xf8 (cautious).
            // DF2: by +0xAC, the player's distance at the last scan that
            // returned a candidate (GOG Df2.exe 0x401ea5): under 32 m 62
            // ticks, under 100 m 124, else 372.
            const int interval = df2 ? (p.nearestPlayer < 32.f ? 62 : p.nearestPlayer < 100.f ? 124 : 372) : (p.alert == 0 ? 0x174 : 0xf8);
            if (p.scanTimer >= interval) {
                p.scanTimer = 0;
                if (scan(world, p, player, sound)) {
                    bark(world, p, sound, 3);
                    // GOG Df.exe 0x45bd90 / 0x45bda6: 0x42edd0 and 0x42ee00
                    // (own team, target team) set the spotted and engaged bits.
                    const int tt = (p.target == -3 ? player.team : world.objects[size_t(p.target)].team) & 63;
                    spotted_[size_t(o.team & 63)] |= 1ull << tt;
                    engaging_[size_t(o.team & 63)] |= 1ull << tt;
                    p.alert = 2;
                    alertTeam(o.team);
                    p.state = 2;
                    p.stateTimer = 0;
                    // DF2 thinks again in 0x10 (GOG Df2.exe 0x401f5d).
                    p.think = df2 ? 16 : 8;
                    return;
                }
            }
        } else {
            // DF2's team share, alert without a live target (GOG Df2.exe
            // 0x401d4f..0x401d8c): passes a dead target's news (0x40) on.
            if (df2 && p.radioTimer > 0x1f0 && !p.silent) {
                p.radioTimer = (-int(rnd())) & 0x3f;
                const int r = teamShare(world, p);
                if (r == 1 || r == 3) bark(world, p, sound, 4);
                else if (r == 2) bark(world, p, sound, 5);
            }
            if (p.scanTimer >= (df2 ? (p.nearestPlayer < 32.f ? 62 : p.nearestPlayer < 100.f ? 124 : 248) : 0xf8)) {
                p.scanTimer = 0;
                if (scan(world, p, player, sound)) {
                    // DF2 marks the spot at once (GOG Df2.exe 0x401e00:
                    // 0x41ac00(own team, target team)).
                    if (df2) markSpotted(o.team & 63, (p.target == -3 ? player.team : world.objects[size_t(p.target)].team) & 63);
                    p.state = 2;
                    p.stateTimer = 0;
                    p.think = 8;
                    return;
                }
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
    // 0x45c918 (GOG Df.exe 0x45b643: 0x42ee00(own team, target team)).
    engaging_[size_t(o.team & 63)] |= 1ull << ((p.target == -3 ? player.team : world.objects[size_t(p.target)].team) & 63);
    if (!df2 && !targetAlive(world, p, player)) {
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
        // DF.EXE 0x459a9f..0x459aad, DF2.EXE 0x401732..0x40175c: from 1 m
        // above the origin to the target's origin; any result >= 1 is seen.
        if (sightLine(world, eye, tp, p.object, p.target) >= 1) {
            p.flags |= 0x80;
            p.lastKnown = tp;
            p.unseen = 0;
            spotted_[size_t(o.team & 63)] |= 1ull << ((p.target == -3 ? player.team : world.objects[size_t(p.target)].team) & 63);
        } else {
            p.flags &= ~0x80u;
        }
        // Seen or not, the scan runs again with no random draw (GOG Df.exe
        // 0x45b6b3 -> 0x45e280, switch 0x45b6bf..0x45b704; DF2 GOG Df2.exe
        // 0x40197f -> 0x417830, 0x401987..0x4019cd): a different best target
        // scoring over 0x4000 (0.25) is taken, seen, marked spotted, think
        // 0x10, and its position is the last known (0x45b72b).
        int id = -1;
        Vec3 at{0, 0, 0};
        bool deadFriend = false;
        const float score = scanScore(world, p, player, id, at, nullptr, &deadFriend);
        if (deadFriend) deadFriendAlert(world, p, sound);
        if (id != -1 && id != p.target && score * 65536.f > float(0x4000)) {
            p.target = id;
            p.flags |= 0x80;
            spotted_[size_t(o.team & 63)] |= 1ull << ((id == -3 ? player.team : world.objects[size_t(id)].team) & 63);
            p.think = 16;
            p.lastKnown = at;
            p.unseen = 0;
            if (df2 && !targetAlive(world, p, player)) p.flags |= 0x40;   // GOG Df2.exe: already dead
            if (std::getenv("OPENDF_DEBUGAI")) std::fprintf(stderr, "ai %d switches to %d, score %.3f\n", p.object, id, score);
        }
    }
    if (df2 && (p.flags & 0x80)) {
        // GOG Df2.exe 0x4019d5..0x401a08, while seen: a dead target gets
        // 0x40 (bark 5 if this soldier killed it) and is kept, so the next
        // think runs the no-target logic (0x4018ea) and the team share
        // passes the news on; the last known position is the target's.
        if (!targetAlive(world, p, player)) {
            p.flags |= 0x40;
            if (p.target >= 0 && world.objects[size_t(p.target)].killer == p.object) bark(world, p, sound, 5);
        }
        p.lastKnown = world.nearest(targetPos(world, p, player), o.pos);
    }
    if (p.flags & 0x80) p.seen += elapsed;
    else { p.unseen += elapsed; p.seen = 0; }
    if (p.unseen > p.giveUpTicks) {
        p.target = -1;
        p.state = 0;
        return;
    }
    if (df2) {
        // GOG Df2.exe 0x401a46..0x401acc: the team share (0x414350) every
        // 0x1f0 ticks unless silent, the timer back to (-rand) & 0x3f;
        // bark 4 for 1 / 3, 5 for 2.
        if (p.radioTimer > 0x1f0 && !p.silent) {
            p.radioTimer = (-int(rnd())) & 0x3f;
            const int r = teamShare(world, p);
            if (r == 1 || r == 3) bark(world, p, sound, 4);
            else if (r == 2) bark(world, p, sound, 5);
        }
    } else if (p.radioTimer > 0x1f0) {
        // Radio (0x46b600) every 0x1f0 ticks: same-side people within 50 m
        // without a target take this one (bark 4, WARN).
        p.radioTimer = 0;
        bool told = false;
        for (auto& q : people_) {
            if (&q == &p || q.target != -1) continue;
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
        float chosen = h;
        if (!df2) {
            // Terrain probe (GOG Df.exe 0x45edd0, CD 0x46b34c), on the
            // heading to the target. The flank (+0x7c, 1/256 turns) side is
            // ai flag 4 (DF minus, OpenDF's clockwise plus) or 8; with
            // neither, rand < 0x8000 picks 4, else 8, and it stays. From
            // h +- flank, flank/4 + 8 headings 0x4000000 apart further to that
            // side; the first whose ground 8 m and 16 m ahead passes against
            // the person's height wins: water at 8 m <= 0.75 m deep, rise to
            // 8 m <= 0.5 m, drop <= 1 m, rise to 16 m <= 3 m, drop <= 4 m.
            // Else from DF h + 0x10000000 (OpenDF h - 22.5 degrees), 128
            // steps the same way testing only 8 m (water, rise <= 2 m, drop
            // <= 4 m); else h.
            if (!(p.flags & 0xC)) p.flags |= rnd() < 0x8000 ? 4u : 8u;
            const int flank256 = int(p.flank * 256.f / 360.f);   // (deg << 8) / 360 (0x4497c0)
            const float unit = 2 * kPi / 4294967296.f;
            const float sgn = (p.flags & 4) ? 1.f : -1.f;
            const float step = sgn * float(0x4000000) * unit;
            const float w = world.terrain->waterLevel();
            auto ground = [&](float hh, float m) {
                const Vec3 a = o.pos + Vec3{std::sin(hh), 0, -std::cos(hh)} * m;
                return world.terrain->heightAt(a.x, a.z);
            };
            bool found = false;
            float t = h + sgn * float(int64_t(flank256) << 24) * unit;
            for (int k = 0; k < flank256 / 4 + 8; ++k, t += step) {
                const float g8 = ground(t, 8.f), g16 = ground(t, 16.f), z = o.pos.y;
                if (w != 0 && w - g8 > 0.75f) continue;
                if (g8 - z > 0.5f || g16 - z > 3.f || z - g8 > 1.f || z - g16 > 4.f) continue;
                chosen = t;
                found = true;
                break;
            }
            if (!found) {
                t = h - float(0x10000000) * unit;
                for (int k = 0; k < 128; ++k, t += step) {
                    const float g8 = ground(t, 8.f), z = o.pos.y;
                    if (w != 0 && w - g8 > 0.75f) continue;
                    if (g8 - z > 2.f || z - g8 > 4.f) continue;
                    chosen = t;
                    break;
                }
            }
        } else {
            const float flank = p.flank * kPi / 180.f;
            if (p.flags & 4) h += flank;
            else if (p.flags & 8) h -= flank;
            // Terrain probe: avoid deep water and big steps.
            auto ok = [&](float hh) {
                const Vec3 f{std::sin(hh), 0, -std::cos(hh)};
                const float g0 = world.terrain->heightAt(o.pos.x, o.pos.z);
                const Vec3 a = o.pos + f * 2.f, b = o.pos + f * 4.f;
                const float ga = world.terrain->heightAt(a.x, a.z), gb = world.terrain->heightAt(b.x, b.z);
                const float w = world.terrain->waterLevel();
                // DF2 rejects water only with ai flag 0x800, deeper than 0.25 m (0x4151a0).
                if ((p.flags & 0x800) && w != 0 && w - ga > 0.25f) return false;
                if (ga - g0 > 0.5f || g0 - ga > 1.f) return false;
                if (gb - g0 > 3.f || g0 - gb > 4.f) return false;
                return true;
            };
            chosen = h;
            if (!ok(h)) {
                const int tries = int(p.flank * 256.f / 360.f) / 4 + 8;
                const float step = float(0x4000000) / 4294967296.f * 2 * kPi;
                const float sgn = (rnd() & 0x8000) ? 1.f : -1.f;
                for (int k = 1; k <= tries; ++k) {
                    if (ok(h + sgn * step * float(k))) { chosen = h + sgn * step * float(k); break; }
                    if (ok(h - sgn * step * float(k))) { chosen = h - sgn * step * float(k); break; }
                }
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
        if (df2) {
            // GOG Df2.exe 0x417650(self, dist, 0x10000), called at 0x401cd7:
            // every organic of the shooter's side (the player too), alive
            // and not removed, not the shooter; along the ray from 0.75 m
            // above the origin by the current yaw +0x30 and pitch +0x34, a
            // projection t in (0xff / 65536 m, dist] and a miss of at most
            // 1 m by 0x41e880 (max + 11/32 mid + 1/4 min of |dx|, |dy|, |dz|).
            const Vec3 dir{std::cos(o.aimPitch) * std::sin(o.heading), std::sin(o.aimPitch), -std::cos(o.aimPitch) * std::cos(o.heading)};
            auto inLine = [&](Vec3 at) {
                const Vec3 c = world.nearest(at, from);
                const float t = dot(c - from, dir);
                if (t <= 0xff / 65536.f || t > dist) return false;
                const Vec3 m = c - (from + dir * t);
                float a = std::abs(m.x), b = std::abs(m.y), e = std::abs(m.z);
                if (a < b) std::swap(a, b);
                if (a < e) std::swap(a, e);
                if (b < e) std::swap(b, e);
                return a + b * 11.f / 32.f + e * 0.25f <= 1.f;
            };
            if (player.alive && player.side == o.side && inLine(player.origin)) blocked = true;
            for (const auto& q : world.objects) {
                if (blocked) break;
                if (&q == &o || !q.person || !q.alive() || q.removed || q.side != o.side) continue;
                if (inLine(q.pos)) blocked = true;
            }
        } else
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
    // DF2 counts each up and clears once it passes 0x1b2 / 0x1f0 (GOG
    // Df2.exe 0x45fc50: [0x67fff8] -> 0x4156a0, [0x67fff4] -> 0x45adc0),
    // every 0x1b3 / 0x1f1 ticks, the counters kept across missions.
    bool clearNoise, clearFired;
    if (df2) {
        if ((clearNoise = ++noiseTick_ > 0x1b2)) noiseTick_ = 0;
        if ((clearFired = ++firedTick_ > 0x1f0)) firedTick_ = 0;
    } else {
        clearNoise = ticks_ % 0x1b2 == 0;
        clearFired = ticks_ % 0x1f0 == 0;
    }
    if (clearNoise) noise_.clear();
    if (clearFired) {
        playerFired_ = false;
        for (auto& p : people_) p.fired = false;
    }
    for (auto& p : people_) {
        WorldObject& o = world.objects[size_t(p.object)];
        if (!o.alive()) continue;
        if (o.hp < p.lastHp) hurt(world, p.object);
        p.lastHp = o.hp;
        if (p.testWalk && !(o.moveNumber >= 16 && o.moveNumber <= 19)) {
            const bool two = modeInfo(p.weapon).flags & 4;
            world.requestMove(o, p.testWalkSlow ? (two ? 28 : 20) : (two ? 68 : 60));
        }
        // Delta Force 2 ladders (0x440444: the chooser 0x43f690 on odd
        // ticks, for the AI as for the player): while the person stood in a
        // type-4 volume last tick (+0x64 & 0x400000, which the push-out sets
        // only with AI +0x34 & 0x400), a walk or run move (control word
        // +0x74: direction bits 0-2, speed bits 3-4 = 1 walk / 2 run) becomes
        // a ladder move by direction and z moves at once: walk 0x43f951
        // (table 0x43fb40), run 0x43fa42 (0x43fb60): forward +0x800 / +0x1000
        // 16 Ladder_Up, left / right 18 / 19, the front diagonals half (the
        // GOG build signs the climb by the aim pitch, below). Out of the
        // volume the chooser gives the walk / run move again. AiSystem walks
        // and runs forward only, so an AI walking into a ladder climbs up
        // while aiming up and down otherwise (INFERRED from the moves
        // AiSystem gives; DF2's AI sets the control word, not the move).
        if (world.df2Rules && (ticks_ & 1) && !o.chute && !o.swimming) {
            const bool onLadderMove = o.moveNumber >= 16 && o.moveNumber <= 19;
            const int want = onLadderMove ? p.gait : o.moveNumber;
            int dir = -1, speed = 0;
            for (const auto& [lo, sp] : {std::pair{20, 1}, {28, 1}, {40, 1}, {48, 1}, {60, 2}, {68, 2}, {80, 2}, {88, 2}})
                if (want >= lo && want < lo + 8) { dir = want - lo; speed = sp; }
            // GOG Df2.exe's chooser 0x445980 (0x445d1b walk, 0x445df7 run):
            // the climb's sign is the aim pitch +0x34 (in (0, 0x7fffffff) up,
            // level or down down), Ladder_Down is never chosen; back and its
            // diagonals give move 0 Null walking and the run move running
            // (AiSystem gives forward moves only, so those are left out).
            if (o.ladder && dir >= 0 && (dir <= 2 || dir >= 6)) {
                static const int climb[8] = {2, 1, 0, 0, 0, 0, 0, 1};
                static const int ladderMove[8] = {16, 18, 18, 0, 0, 0, 19, 19};
                const int s = o.aimPitch > 0 ? 1 : -1;
                o.base += float(s * climb[dir] * (speed == 2 ? 0x800 : 0x400)) / 65536.f;
                p.gait = want;
                world.requestMove(o, ladderMove[dir]);
                if (o.moveNumber != ladderMove[dir]) o.moveNumber = ladderMove[dir];   // not in this .ANM: still the ladder move
            } else if (onLadderMove) {
                world.requestMove(o, p.gait);
                p.gait = -1;
            }
        }
        // GOG Df2.exe 0x445980 (the chooser of 0x4467d0's odd ticks): a
        // person manning an emplaced gun (+0x8c) plays 107 Crouched_EWeap
        // (speed 0; not in the enemies' .ANMs: no motion either).
        if (world.df2Rules && (ticks_ & 1) && !o.chute && o.mount >= 0) {
            world.requestMove(o, 107);
            if (o.moveNumber != 107) o.moveNumber = 107;
        }
        // Movement (0x436158): the move's .ANM speed along the heading (not
        // while on a DF2 parachute).
        if (o.moves && o.moveNumber >= 0 && !o.chute) {
            const char* name = moveName(o.moveNumber);
            const AnimMove* mv = name ? o.moves->find(name) : nullptr;
            if (mv && mv->velocity > 0) {
                // On land velocity / 60 along the heading; swimming the
                // move's whole per-frame distance and a vertical part
                // (World::personStep: DF.EXE 0x43621a, DF2.EXE 0x4404dc).
                Vec3 step = world.personStep(o, mv->velocity);
                // DF2 ladder moves go along heading + their direction (move
                // table +0: Ladder_Down 4, Left 1, Right 7; 0x4ab758).
                if (const int md = o.moveNumber == 17 ? 4 : o.moveNumber == 18 ? 1 : o.moveNumber == 19 ? 7 : 0; md) {
                    const float a = -float(md) * kPi / 4.f, c = std::cos(a), sn = std::sin(a);
                    step = {c * step.x - sn * step.z, step.y, sn * step.x + c * step.z};
                }
                Vec3 next = o.pos + step;
                // Kept out of collision volumes like the player (0x437a28:
                // the same +-0.75 m mover box), on odd ticks only (0x436cbe);
                // DF2 pushes people out of the volumes in World::personTick.
                Vec3 feet{next.x, o.base, next.z};
                if (ticks_ & 1) feet = world.resolve(feet, o.hip);
                o.pos.x = feet.x;
                o.pos.z = feet.z;
                o.base = feet.y;   // the push can move the origin up or down (0x4889c0)
                const float ext = world.extent();
                o.pos.x -= std::floor(o.pos.x / ext) * ext;
                o.pos.z -= std::floor(o.pos.z / ext) * ext;
                // Height: World::personTick (gravity and the ground pass).
            }
        }
        if (!p.testWalk && --p.think <= 0) {
            think(world, p, shots, sound, player);
            p.think = std::max(1, p.think);
            p.delay = p.think;
        }
    }
}

bool AiSystem::spotted(int team, int targetTeam) const {
    return team >= 0 && team < 64 && targetTeam >= 0 && targetTeam < 64 && (spotted_[size_t(team)] >> targetTeam & 1);
}

bool AiSystem::engaging(int team, int targetTeam) const {
    return team >= 0 && team < 64 && targetTeam >= 0 && targetTeam < 64 && (engaging_[size_t(team)] >> targetTeam & 1);
}

bool AiSystem::alerted(int team) const { return team >= 0 && team < 64 && teamAlert_[size_t(team)] == 2; }

bool AiSystem::reached(int team, int path, int point) const {
    auto it = reached_.find(team * 128 + path);
    return it != reached_.end() && point >= 0 && point < 32 && (it->second >> point & 1);
}

void AiSystem::followPath(World& world, int team, int path) {
    // 0x46bbe8 (GOG Df.exe 0x45f670): teams below 3 are ignored; each member
    // takes the path from its nearest point (0x45c5e0, 0 for an empty path).
    // Path 0 is taken too: the next path step ends path mode (see patrol).
    if (team < 3 || !mission_ || path < 0 || path >= int(mission_->paths.size())) return;
    const MissionPath& mp = mission_->paths[size_t(path)];
    for (auto& p : people_) {
        WorldObject& o = world.objects[size_t(p.object)];
        if (o.team != team || !o.alive()) continue;
        p.pathMode = 1;
        p.path = path;
        p.waypoint = 0;
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
        // 2/3/4/15 set a flag for a non-zero value and clear it for 0 in both
        // games (GOG Df.exe 0x45ff20, CD 0x46c444); 14 the same for silent.
        case 2: if (!value) p.flags &= ~2u; else p.flags |= 2; break;
        case 3: if (!value) p.flags &= ~0x10u; else p.flags |= 0x10; break;
        case 4: if (!value) p.flags &= ~0x20u; else p.flags |= 0x20; break;
        // 5/6 set the live alert byte (GOG Df.exe 0x45ff20 ai+0x9c, Df2.exe
        // 0x4185b0 ai+0xb4) to 2 / 0; the team's alert word is not touched
        // here (DF2 sets it after the loop, below).
        case 5: p.alert = 2; break;
        case 6: p.alert = 0; break;
        case 7: p.fov = float(value); break;
        case 8: if (value > 0) p.accuracy = float(value); break;
        case 9: p.perception = float(value) / 100.f; break;
        case 10: p.advanceTicks = value * 62; break;
        case 11: p.holdTicks = value * 62; break;
        case 12: p.giveUpTicks = value * 62; break;
        case 13: p.flank = float(value); break;
        case 14: p.silent = value != 0; break;
        case 15: if (!value) p.flags &= ~1u; else p.flags |= 1; break;
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
    // Delta Force 2's action (GOG Df2.exe 0x4184c0): after the members,
    // sub 5 sets the team's alert word (0x4045c0) and 6 clears it (0x4045a0),
    // with or without members, the word the "team alerted" condition reads
    // (0x41af30); team 0 is skipped. Delta Force's 0x4601a0 does neither:
    // there only a member that really reacts (0x44a3e0 from 0x45e280 /
    // 0x45fd90 / 0x464c50) alerts the team.
    if (df2 && team > 0 && team < 64 && (subtype == 5 || subtype == 6))
        teamAlert_[size_t(team)] = subtype == 5 ? 2 : 0;
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
