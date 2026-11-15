// Delta Force 2's animals (ITEMS.DEF ai_function / move_function, GOG
// Df2.exe ai table 0x4af100, move table 0x4c37xx): vultures `vult` (think
// 0x42dfe0, move 0x42e1c0), penguins `peng` (think 0x42f030, move 0x42f430)
// and fish / piranhas `prnh` (think 0x42db20, move 0x42de00). Each think
// sets its own interval (+0xFC); the movers run every tick.
#pragma once

#include "../formats/Mission.h"
#include "Ai.h"
#include "World.h"

#include <functional>
#include <random>
#include <vector>

namespace df {

class WildlifeSystem {
public:
    void init(World& world, const Mission& mission, bool night);
    // One 60 Hz tick. `playerSwimming`: the player's +0x64 & 0x20000.
    void tick(World& world, AiSystem& ai, const AiSystem::PlayerView& player, bool playerSwimming);
    // A positioned one-shot sound (0x467da0).
    std::function<void(int id, Vec3 at)> playSound;
    // An effect slot (0x4156b0), as 0x45af50 spawns it with its sound.
    std::function<void(int slot, Vec3 at)> spawnEffect;
    // The piranha's bite (blast kind 4, 0x455180) reached the player: 1 hp.
    std::function<void(Vec3 from)> bitePlayer;

private:
    enum class Kind { Vulture, Penguin, Fish };
    struct Animal {
        int object = -1;
        Kind kind = Kind::Vulture;
        int think = 1;
        bool dead = false;          // +0x64 & 4 (set with 0x10C by the think)
        uint32_t yaw = 0;           // +0x30, DF binary angle (anticlockwise from east)
        uint32_t wantYaw = 0;       // +0xA0
        int32_t pitch = 0, roll = 0;   // +0x34, +0x38
        int32_t speed = 0, want = 0;   // +0x90, +0x9C (16.16 m/s)
        int32_t vx = 0, vy = 0;        // +0x3C, +0x40 (16.16 m a tick, DF x east / y north)
        int32_t vz = 0, az = 0;        // vult +0xC0, peng +0xBC, prnh +0xB0 / +0xB4 (16.16 m a tick)
        int32_t phase = 0;             // peng +0xB4 (the waddle)
        int state = 0;                 // peng +0xC0 (0 land .. 4); prnh +0xC0 (jaw / tail flick)
        float homeX = 0, homeZ = 0;    // vult ai+0x64 / +0x68
        int target = -1;               // ai+0x54: world object, -3 the player
        Vec3 lastKnown{0, 0, 0};       // ai+0x58..+0x60
        int pathMode = 0, path = 0, waypoint = 0;   // ai+0xC0, +0xC8, +0xCC
        int losTimer = 0, pause = 0;   // ai+0x50, ai+0xA0
        bool seen = false;             // ai+0x34 & 0x80
        uint32_t aiFlags = 0;          // ai+0x34: 1 no acquisition, 4 the flank side
        float perception = 1;          // ai+0x84
        int alert = 0;                 // ai+0xB4
        int fov = 0x40;                // ai+0xB0 >> 9: half angle in 1/256 turns
    };
    void vultureThink(World& world, AiSystem& ai, Animal& a, const AiSystem::PlayerView& player);
    void vultureMove(World& world, Animal& a);
    void penguinThink(World& world, AiSystem& ai, Animal& a);
    void penguinMove(World& world, Animal& a);
    void fishThink(World& world, AiSystem& ai, Animal& a, const AiSystem::PlayerView& player, bool playerSwimming);
    void fishAttack(World& world, AiSystem& ai, Animal& a, const AiSystem::PlayerView& player);
    void fishMove(World& world, Animal& a);
    bool steer(World& world, const Animal& a, uint32_t& yaw, int32_t& dist, int32_t& radius) const;
    bool nextPoint(World& world, AiSystem& ai, Animal& a);
    void depth(World& world, Animal& a, int32_t z, int32_t band, bool full);
    void ramp(World& world, Animal& a, int32_t to);
    void turn(World& world, Animal& a, int factor = 1);
    void step(World& world, Animal& a);
    void place(World& world, Animal& a, float z);
    uint32_t rnd() { return uint32_t(rng_()) & 0xFFFF; }   // 0x41ea20: 0..0xFFFF

    const Mission* mission_ = nullptr;
    std::vector<Animal> animals_;
    std::mt19937 rng_{1998};
};

} // namespace df
