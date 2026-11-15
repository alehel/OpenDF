// Soldier AI as DF.EXE runs it: the `org1` think function (0x4599ec) of
// every AI person, with its per-person block (obj+0xc, filled from the BMS
// record at 0x440109..0x440478), perception (0x46a804 target score, 0x45be04
// line of sight, 0x46c228 noise), radio (0x46b600), path walking (0x459854),
// advance / engage combat states, aimed fire (0x46b838 -> 0x46af90) and the
// mission script's AI conditions and actions (0x45c0e4, 0x46bbe8,
// 0x46c444). See docs/AUDIT.md (AI rows) for sources and approximations.
#pragma once

#include "../formats/Mission.h"
#include "Player.h"
#include "Weapons.h"
#include "World.h"

#include <array>
#include <functional>
#include <cstdint>
#include <map>
#include <random>
#include <vector>

namespace df {

class SoundSystem;

// Delta Force 2's start point on a path for follow-path (GOG Df2.exe
// 0x4032f0), shared by soldiers and vehicles.
int df2NearestPoint(const World& world, const Mission& m, const MissionPath& mp, const WorldObject& o);

class AiSystem {
public:
    // firstThink: per mission record, its first think wait (Delta Force's
    // load counter, see Game::loadMission); empty: count over the persons.
    void init(World& world, const Mission& mission, const std::vector<int>& firstThink = {});
    // "Enemy AI: Hard" (0x66fa88 bit 0x80): side 2 sees twice as well and
    // aims four times tighter.
    bool hard = false;
    // Delta Force 2's org1 (0x4015f0, DF1's recompiled; docs/OTHER-GAMES.md):
    // set by the game for DF2 missions, with the night flag and the
    // player's body gear (32 = ghillie suit).
    bool df2 = false, night = false;
    int playerGear = 0;
    // The muzzle of a soldier's weapon (0x46ad9c): set by the game, which
    // poses the body; when unset, 0.5 m ahead and 0.4 m above the origin.
    std::function<bool(int object, int mode, Vec3& at)> muzzle;

    // The player as others see it.
    struct PlayerView {
        bool alive = false;
        Vec3 origin{0, 0, 0};
        int moveNumber = 2;     // DF move number (visibility / aim factors)
        int side = 1, team = 1;
    };
    // The target scan 0x46a804 (soldiers and helicopters): the best enemy by
    // field of view (half angle `half`, radians), 100 / distance, line of
    // sight, the target's move visibility and x6 if it fired lately; before
    // perception and the relaxed quarter. Returns the score, 0 if none.
    // `nearPlayer` (DF2, GOG Df2.exe 0x417830 local_10 -> *param_3): the
    // whole-metre distance from `bodyAt` to the player when he passes the
    // field-of-view test, else left as it was (the caller's 1024).
    // `coverMe` (DF2, the scanner's order 2): an AI candidate targeting the
    // player scores twice. `deadFriendAlert` (DF2): the scanner's alert,
    // set to 2 when a body of its own side is found (see Ai.cpp).
    float bestTarget(const World& world, int self, Vec3 eye, float heading, int side, float half, const PlayerView& player,
                     int& id, Vec3& at, bool everyone = false, float* nearPlayer = nullptr, Vec3 bodyAt = {}, bool coverMe = false,
                     int* deadFriendAlert = nullptr) const;
    // 0x45be04: <= 0 blocked, else 1 + see-through faces (World::sight).
    int sightLine(const World& world, Vec3 a, Vec3 b, int self, int target) const { return world.sight(a, b, self, target); }
    // One 60 Hz tick: people move (their move's speed along their heading)
    // and think when their timer runs out.
    void tick(World& world, Arsenal& shots, SoundSystem& sound, const PlayerView& player);
    // Testing (OPENDF_TESTLADDER): the soldier on world object `object`
    // stops thinking and keeps running (walking with `walk`) along his heading.
    void testWalk(int object, bool walk = false) { if (Person* p = byObject(object)) { p->testWalk = true; p->testWalkSlow = walk; } }

    // A shot (0x44d71f / 0x46c228) is noise() of half its sound's range with
    // flags 2 plus markFired(): the shooter counts as firing (x6 to be seen)
    // until the flags are cleared (every 0x1f1 ticks); -3 the player.
    void markFired(int object);
    // Whether `object` (-3 the player) fired lately (+0x6c & 0x4000).
    bool hasFired(int object) const {
        if (object == -3) return playerFired_;
        for (const auto& p : people_)
            if (p.object == object) return p.fired;
        return false;
    }
    // Any other noise (DF2.EXE 0x412840 with flags 0: footsteps, a building
    // collapsing): a box of +-r on all three axes; heard by relaxed soldiers
    // (0x413620, any team, any flags), not turned toward by alert ones
    // (0x415570 wants flags & 2). `team` is the noise's team (+4), which no
    // reader checks. Does not mark anyone as firing.
    void noise(Vec3 at, float r, int team, int flags) {
        if (noise_.size() < 200) noise_.push_back({at, r, team, flags});
    }
    // Delta Force 2's searchlight scan (0x425c70); yaw and pitch are DF2
    // binary angles (yaw anticlockwise from east). See Ai.cpp.
    float lightScan(const World& world, int self, Vec3 at, uint32_t yaw, int32_t pitch, int side, const PlayerView& player,
                    int& bestId, Vec3& bestPos) const;
    // Delta Force's searchlight scan (GOG Df.exe 0x44f540, CD 0x47b074):
    // the same over people of another side (side 0 never), DF's own binary
    // angles, whole-metre distances; the best score before perception.
    float lightScanDf1(const World& world, int self, Vec3 at, uint32_t yaw, int32_t pitch, int side, const PlayerView& player,
                       int& bestId) const;
    // Delta Force's bird-tree score (GOG Df.exe 0x44fba0), 16.16.
    int32_t birdScoreDf1(const World& world, Vec3 at, int side, const PlayerView& player) const;
    // 0x418960(a, b): sets bit b of row a of the "spotted" table that the
    // script condition 1 (team a spotted team b) reads.
    void markSpotted(int a, int b) {
        if (a >= 0 && a < 64 && b >= 0 && b < 64) spotted_[size_t(a)] |= 1ull << b;
    }
    // GOG Df.exe 0x44a3e0: the team's alert word = 2 (vehicles seeing a dead friend).
    void alertTeamOf(int team) { alertTeam(team); }
    // Called when someone is hurt (0x44f218): an AI victim becomes alert.
    void hurt(World& world, int object);
    // Delta Force, a round hit a living AI person (GOG Df.exe 0x464c50),
    // damage or not: ai +0x9c = 2 and the team's alert word 2 (0x44a3e0).
    void roundHit(int object, int team) {
        if (Person* p = byObject(object)) p->alert = 2;
        alertTeam(team);
    }

    // Script conditions (0x45c0e4) and actions.
    bool spotted(int team, int targetTeam) const;
    bool engaging(int team, int targetTeam) const;
    bool alerted(int team) const;
    bool reached(int team, int path, int point) const;
    void followPath(World& world, int team, int path);       // action 1 (0x46bbe8)
    void property(World& world, int team, int subtype, int value);   // action 3 (0x46c444)
    void teamSpeed(int team, int kmh);                        // action 11 (0x46cafc)
    // The player's route (plyr 0x45a27c, every 64 ticks): points within
    // their radius count as reached for team = the player's side.
    void playerRoute(Vec3 origin, int side);
    // For vehicles: the team's speed (m/s) and marking path points reached.
    float teamSpeedMs(int team) const { return team >= 0 && team < 64 ? teamSpeed_[size_t(team)] * 1000.f / 3600.f : 0.f; }
    void markReached(int team, int path, int point) { reached_[team * 128 + path] |= 1u << (point & 31); }
    // DF2 command-map orders (0x403380): AI byte +0xBB = 0 none, 1 hold
    // position and fire, 2 hold position and cover me, 3 hold position.
    // False if the object is no AI soldier or already has that order.
    bool setOrder(int object, int order) {
        for (auto& p : people_)
            if (p.object == object) { if (p.order == order) return false; p.order = order; return true; }
        return false;
    }
    int order(int object) const {
        for (const auto& p : people_) if (p.object == object) return p.order;
        return 0;
    }

private:
    struct Person {
        int object = -1;
        uint32_t flags = 0;           // ai+0x24: 2 hold, 4/8 flank side, 0x10 stand, 0x20 prone, 0x40 target dead, 0x80 visible
        bool silent = false;          // ai+0x25 bit 0
        int state = 0, stateTimer = 0;   // +0x28 / +0x2c: 1 advance, 2 engage
        int unseen = 0, seen = 0;     // +0x30 / +0x34
        int scanTimer = 0, radioTimer = 0, losTimer = 0;   // +0x38 / +0x3c / +0x40
        int target = -1;              // +0x44: world object, -3 the player, -1 none
        Vec3 lastKnown{0, 0, 0};      // +0x48
        float homeHeading = 0;
        int weapon = 0;               // +0x64: player-table mode
        int burstLeft = 0;            // +0x6c
        float accuracy = 100;         // rec +0x20 (%)
        float perception = 1;         // +0x74
        float flank = 45;             // +0x7c degrees
        float closeRange = 16;        // +0x80
        float engageRadius = 1024;    // +0x84
        int advanceTicks = 310, holdTicks = 310, giveUpTicks = 18600;   // +0x88 / +0x8c / +0x94
        float fov = 180;              // +0x98 degrees
        int alert = 0;                // +0x9c
        int pathMode = 0, path = 0, waypoint = 0;   // +0xa4 / +0xac / +0xb0
        int think = 1;                // obj+0xfc: ticks to the next think
        int delay = 1;                // the delay that was set (added to the timers, 0x459800)
        bool fired = false;           // obj+0x6c bit 0x4000
        int lastHp = 0;
        // Delta Force 2 (block moved +0x10; new fields):
        int unseenLimit = 60 * 62;    // +0xA8: BMS +0x54 x 62 (default 60 s)
        float nearestPlayer = 0;      // +0xAC: the scan's player distance (1024 unless in the field of view), whole metres
        int order = 0;                // +0xBB: 1 hold fire, 2 cover me, 3 hold position (command map)
        int gait = -1;                // DF2: the walk / run move a ladder move replaced (the control word +0x74)
        bool testWalk = false, testWalkSlow = false;
    };
    struct Noise { Vec3 at; float r; int side; int flags; };

    void think(World& world, Person& p, Arsenal& shots, SoundSystem& sound, const PlayerView& player);
    void patrol(World& world, Person& p);
    // The scan's score (0x46a804; GOG Df.exe 0x45e280): the best target x
    // perception, x2 when hard, a quarter when relaxed; no random draw.
    // `deadFriend` is set when the scan's dead-friend check alerted the
    // scanner (the caller applies it: alert, team alert, bark 1).
    float scanScore(const World& world, const Person& p, const PlayerView& player, int& bestId, Vec3& bestPos, float* nearPlayer = nullptr,
                    bool* deadFriend = nullptr) const;
    bool scan(World& world, Person& p, const PlayerView& player, SoundSystem& sound);
    // The scan's dead-friend check (GOG Df.exe 0x45e280, Df2.exe 0x417830):
    // `alert` set to 2 for a body of the scanner's side (see Ai.cpp).
    void deadFriendScan(const World& world, int selfId, int side, int& alert) const;
    // Delta Force's best target in 16.16 (GOG Df.exe 0x45e280), before perception.
    int64_t bestTargetDf1(const World& world, const Person& p, const PlayerView& player, int& bestId, Vec3& bestPos) const;
    void deadFriendAlert(World& world, Person& p, SoundSystem& sound);
    // Delta Force 2's team share (GOG Df2.exe 0x414350): 0, or 1 / 2 / 3.
    int teamShare(World& world, Person& p);
    Vec3 targetPos(const World& world, const Person& p, const PlayerView& player) const;
    bool targetAlive(const World& world, const Person& p, const PlayerView& player) const;
    void fire(World& world, Person& p, Arsenal& shots, SoundSystem& sound, const PlayerView& player, bool followUp);
    void alertTeam(int team) { if (team >= 0 && team < 64) teamAlert_[size_t(team)] = 2; }
    void bark(World& world, const Person& p, SoundSystem& sound, int slot);
    int idleMove(const World& world, const Person& p) const;
    int walkMove(const World& world, const Person& p) const;
    float headingTo(Vec3 from, Vec3 to) const;
    Person* byObject(int object);
    // GOG Df.exe 0x477860 (called by the soldier code, e.g. 0x45c3a0,
    // 0x45dab0; Df2.exe 0x41ac40 is the same): x = rol(x,11) + x, x =
    // rol(x,4) ^ 1, 16 uniform bits (the CD build's 0x435db4 gave
    // (rand & 0x7fff) * 2). The EXEs share one state; here the AI has its own.
    uint32_t rnd() {
        rng_ = ((rng_ << 11) | (rng_ >> 21)) + rng_;
        rng_ = ((rng_ << 4) | (rng_ >> 28)) ^ 1;
        return rng_ & 0xFFFF;
    }

    const Mission* mission_ = nullptr;
    std::vector<Person> people_;
    std::vector<Noise> noise_;
    std::array<int, 64> teamAlert_{};
    std::array<uint64_t, 64> spotted_{};          // team -> target-team bits
    std::array<uint64_t, 64> engaging_{};         // team -> engaged-team bits (GOG Df.exe 0x6d7e50)
    std::map<int, uint32_t> reached_;             // team * 128 + path -> point bits
    std::array<int, 64> teamSpeed_{};
    int ticks_ = 0;
    // GOG Df.exe [0x6e4748] / [0x6e4744], GOG Df2.exe [0x67fff8] / [0x67fff4]:
    // process-wide, never reset (see tick).
    static inline int noiseClock_ = 0, firedClock_ = 0;
    bool playerFired_ = false;
    uint32_t rng_ = 0x5FC;
};

// The point of `path` a member taking it starts from (GOG Df.exe 0x45c5e0,
// Df2.exe 0x4032f0): per axis the whole-metre difference (16.16 >> 16)
// between the object and the marker (ground + record height, 0x404e30),
// squared and summed in 3-D, the last of equals winning; 0 for no points.
int nearestPathPoint(const World& world, const Mission& mission, const MissionPath& path, Vec3 pos);

} // namespace df
