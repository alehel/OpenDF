// First-person soldier movement: stances, running (the default in Delta
// Force; Shift walks), jumping, slopes and collision with the world.
//
// Movement speeds come from the soldier's .ANM move table (PLAYANIM.ANM for
// the player, chosen by `anim_def` in ITEMS.DEF), as in the original: each
// move (Running0_2HW, Crawling_CR4, ...) has a velocity in m/s, per stance,
// one of eight directions, and one- or two-handed weapon.
#pragma once

#include "../formats/Anm.h"
#include "World.h"

namespace df {

enum class Stance { Stand, Crouch, Prone };

struct PlayerInput {
    float forward = 0, strafe = 0;   // -1..1
    bool walk = false, jump = false;
    bool up = false, down = false;   // fly mode only
};

class Player {
public:
    Vec3 feet;
    float yaw = 0, pitch = 0;        // yaw: compass radians, pitch: up positive
    Stance stance = Stance::Stand;
    bool fly = false;                // debug free flight (N)
    const AnimDefs* moves = nullptr; // the soldier's .ANM table
    bool oneHanded = false;          // pistol: the *_1HW moves

    void update(float dt, const PlayerInput& in, const World& world);
    // `feet` is kept inside the repeating terrain tile (0..ext); missions use
    // one continuous coordinate space, so count the wraps to recover it.
    void wrap(float ext);
    // Position in mission coordinates: metres, x east, y north.
    double missionX() const { return feet.x + double(wrapX_) * wrapExtent_; }
    double missionY() const { return -(feet.z + double(wrapZ_) * wrapExtent_); }
    Vec3 eye() const { return {feet.x, feet.y + eyeHeight_, feet.z}; }
    Vec3 forward() const;
    bool moving() const { return moving_; }
    float speed() const { return speed_; }

private:
    float eyeHeight_ = 1.45f;
    bool sliding_ = false, jumpHeld_ = false;
    float vy_ = 0;
    bool onGround_ = true, moving_ = false;
    float speed_ = 0;
    int wrapX_ = 0, wrapZ_ = 0;
    float wrapExtent_ = 1024;
};

const char* stanceName(Stance s);

} // namespace df
