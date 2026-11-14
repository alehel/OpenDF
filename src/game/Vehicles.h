// Vehicles and helicopters as DF.EXE moves them: veh0 (think 0x480240,
// move 0x480514: a bicycle model on the ground, at the team's speed) and
// helo (think 0x47c92c, move 0x47d05c: altitude controller, turn rate, bank,
// door gun). DF.EXE moves them by speed >> 6 per tick (1/64 s), which is
// followed here per 60 Hz tick.
#pragma once

#include "../formats/Mission.h"
#include "Ai.h"
#include "Weapons.h"
#include "World.h"

#include <functional>
#include <random>
#include <vector>

namespace df {

class SoundSystem;
class AiSystem;

class VehicleSystem {
public:
    void init(World& world, const Mission& mission);
    // One 60 Hz tick. `player` is the player as a target for door guns.
    void tick(World& world, AiSystem& ai, Arsenal& shots, SoundSystem& sound, const AiSystem::PlayerView& player);
    // A smoke puff (0x480aac) at a point: a damaged tail rotor.
    std::function<void(Vec3 at)> smoke;
    // A vehicle was destroyed: blast with shrapnel (0x46cb64 with n pieces)
    // at a point and heading (set by the game).
    std::function<void(Vec3 at, int shrapnel, float heading)> detonate;
    // Engine / rotor loops, every frame.
    void sounds(const World& world, const AiSystem& ai, SoundSystem& sound) const;
    // Moving vehicles and their speeds (m/s), e.g. for claymore sensors.
    std::vector<std::pair<int, float>> moving() const {
        std::vector<std::pair<int, float>> out;
        for (const auto& v : vehicles_) if (!v.dead && !v.helo) out.push_back({v.object, std::abs(v.speed)});
        return out;
    }
    // Script "follow path" for vehicles (0x46bbe8).
    void followPath(World& world, int team, int path);

private:
    struct Vehicle {
        int object = -1;
        bool helo = false;
        int pathMode = 0, path = 0, waypoint = 0;
        float speed = 0;          // m/s along the heading
        float targetSpeed = 0;
        float targetHeading = 0;
        bool stopped = false;     // a one-way path has ended
        int think = 1;
        bool dead = false;
        uint32_t flags = 0;       // ai+0x24 (2: return home after the path)
        // Helicopters.
        float vz = 0;             // m per tick
        float rotor = 0;          // rotor speed, binary angle per tick
        int mode = 1;             // 2 path, 1 hover, 0 land
        bool alerted = false;
        Vec3 home{0, 0, 0};
        bool hasHome = false;
        float bank = 0, yawRate = 0;
        float floorHeight = 0;
        int gunTarget = -1;       // ai+0x44: world object, -3 the player
        Vec3 lastKnown{0, 0, 0};  // ai+0x48: aimed and fired at
        Vec3 acquiredAt{0, 0, 0}; // obj+0xBC: own position when the target was taken
        bool seen = false, deadSeen = false;   // ai+0x24 0x80 / 0x40
        int losTimer = 0, giveUp = 0;          // ai+0x40, ai+0x38 (thinks)
        float perception = 1, fovHalf = 0;     // ai+0x74, ai+0x98 (radians)
        int alert = 0;                         // ai+0x9C
        // Door gun relative to the body in DF.EXE's sense (radians; yaw
        // anticlockwise from the nose, pitch up +): current +0xC4/+0xC8 and
        // target +0xCC/+0xD0.
        float gunYaw = 0, gunPitch = 0, bias = 0;
        float gunTgtYaw = 0, gunTgtPitch = 0;
        bool gunInit = false;
        int burst = 0, pause = 0;  // ai+0x6C, ai+0x90
        int lastHp = 0;
    };
    void drive(World& world, AiSystem& ai, Vehicle& v, float teamSpeed);
    void fly(World& world, AiSystem& ai, Vehicle& v, float teamSpeed);
    void doorGun(World& world, Vehicle& v, Arsenal& shots, SoundSystem& sound);
    void heloTargeting(World& world, AiSystem& ai, Vehicle& v);
    Vec3 gunPoint(const World& world, const Vehicle& v) const;
    Vec3 targetPosition(const World& world, int t) const;
    bool targetAlive(const World& world, int t) const;
    AiSystem::PlayerView player_;
    bool pathStep(World& world, AiSystem& ai, Vehicle& v);
    uint32_t rnd() { return uint32_t(rng_() & 0x7fff) * 2; }

    const Mission* mission_ = nullptr;
    std::vector<Vehicle> vehicles_;
    std::mt19937 rng_{1998};
};

} // namespace df
