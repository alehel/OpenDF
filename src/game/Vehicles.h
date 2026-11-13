// Vehicles and helicopters as DF.EXE moves them: veh0 (think 0x480240,
// move 0x480514: a bicycle model on the ground, at the team's speed) and
// helo (think 0x47c92c, move 0x47d05c: altitude controller, turn rate, bank,
// door gun). DF.EXE moves them by speed >> 6 per tick (1/64 s), which is
// followed here per 60 Hz tick.
#pragma once

#include "../formats/Mission.h"
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
    struct Target { bool alive; Vec3 at; int side; };
    // One 60 Hz tick. `player` is the player as a target for door guns.
    void tick(World& world, AiSystem& ai, Arsenal& shots, SoundSystem& sound, const Target& player);
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
        int gunTarget = -1;       // world object, -3 the player
        Vec3 gunAcquired{0, 0, 0};
        float gunYaw = 0, gunPitch = 0, bias = 0;
        int burst = 0, pause = 0, wait = 0, fireTimer = 0, cooldown = 0;
        int lastHp = 0;
    };
    void drive(World& world, AiSystem& ai, Vehicle& v, float teamSpeed);
    void fly(World& world, AiSystem& ai, Vehicle& v, float teamSpeed);
    void doorGun(World& world, Vehicle& v, Arsenal& shots, SoundSystem& sound, const Target& player);
    bool pathStep(World& world, AiSystem& ai, Vehicle& v);
    uint32_t rnd() { return uint32_t(rng_() & 0x7fff) * 2; }

    const Mission* mission_ = nullptr;
    std::vector<Vehicle> vehicles_;
    std::mt19937 rng_{1998};
};

} // namespace df
