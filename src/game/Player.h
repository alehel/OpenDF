// First-person soldier movement: stances, running (the default in Delta
// Force; Shift walks), jumping, slopes and collision with the world.
#pragma once

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

    void update(float dt, const PlayerInput& in, const World& world);
    Vec3 eye() const { return {feet.x, feet.y + eyeHeight_, feet.z}; }
    Vec3 forward() const;
    bool moving() const { return moving_; }
    float speed() const { return speed_; }

private:
    float eyeHeight_ = 1.65f;
    float vy_ = 0;
    bool onGround_ = true, moving_ = false;
    float speed_ = 0;
};

const char* stanceName(Stance s);

} // namespace df
