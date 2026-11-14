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

#include <string>
#include <vector>

namespace df {

enum class Stance { Stand, Crouch, Prone };

struct PlayerInput {
    float forward = 0, strafe = 0;   // -1..1
    bool walk = false, jump = false;
    bool shooting = false;           // the weapon's refire time is running (DF.EXE [0x93334c] > 0)
    bool up = false, down = false;   // fly mode only
};

class Player {
public:
    Vec3 feet;
    float yaw = 0, pitch = 0;        // yaw: compass radians, pitch: up positive
    Stance stance = Stance::Stand;
    bool fly = false;                // debug free flight (N)
    bool chute = false;              // Delta Force 2: on a parachute (0.133 m a tick down)
    const AnimDefs* moves = nullptr; // the soldier's .ANM table
    bool oneHanded = false;          // pistol: the *_1HW moves
    // Eye height above the feet per stance (stand, crouch, prone) and hand
    // (two-, one-handed): the pose's origin height (.CHR contact points) plus
    // DF.EXE's eye offset (0x449913). Filled from the game data; defaults
    // are the PLAYER01.CHR values.
    float eyeHeights[3][2] = {{1.7558f, 1.7677f}, {1.3768f, 1.3768f}, {0.5031f, 0.5031f}};
    // Origin (pelvis) height above the lowest contact point of the Floating
    // and Swimming poses (PLAYER01.CHR animations 134 / 133).
    float floatOrigin = 0.1489f, swimOrigin = 0.3184f;

    void update(float dt, const PlayerInput& in, const World& world);
    float landOrigin() const;
    // Effect slots spawned since the last call (water entry splash), with
    // their position.
    struct Spawn { int slot; Vec3 pos; };
    std::vector<Spawn> takeSpawns() { std::vector<Spawn> out; out.swap(spawns_); return out; }
    bool inWater() const { return inWater_; }
    bool swimming() const { return inWater_ && swimMove_; }
    // `feet` is kept inside the repeating terrain tile (0..ext); missions use
    // one continuous coordinate space, so count the wraps to recover it.
    void wrap(float ext);
    // Position in mission coordinates: metres, x east, y north.
    double missionX() const { return feet.x + double(wrapX_) * wrapExtent_; }
    double missionY() const { return -(feet.z + double(wrapZ_) * wrapExtent_); }
    Vec3 eye() const { return inWater_ ? Vec3{feet.x, originY_ + eyeOffset_, feet.z} : Vec3{feet.x, feet.y + eyeHeight_, feet.z}; }
    Vec3 forward() const;
    // Body origin (the pelvis; DF.EXE's object z): what blasts measure to.
    Vec3 origin() const { return inWater_ ? Vec3{feet.x, originY_, feet.z} : Vec3{feet.x, feet.y + landOrigin(), feet.z}; }
    bool moving() const { return moving_; }
    // The move being played (an .ANM name), for the body's animation.
    const std::string& move() const { return move_; }
    float speed() const { return speed_; }
    // The vertical speed (m/s, down negative) of the last landing; 0 after read.
    float takeLanding() { const float v = landingSpeed_; landingSpeed_ = 0; return v; }

private:
    void updateInWater(float dt, const PlayerInput& in, const World& world, float water);

    std::string move_ = "Standing_2HW";
    float eyeHeight_ = 1.7558f;
    // In water (DF.EXE obj+0x6e bit 2) the body origin is tracked directly
    // and the eye sits eyeOffset_ above it.
    bool inWater_ = false, swimMove_ = false;
    float originY_ = 0, eyeOffset_ = 0;
    std::vector<Spawn> spawns_;
    bool sliding_ = false, jumpHeld_ = false;
    Vec3 airDelta_{0, 0, 0};         // horizontal speed kept from take-off (m/s)
    float vy_ = 0;
    float landingSpeed_ = 0;
    bool onGround_ = true, moving_ = false;
    float speed_ = 0;
    int wrapX_ = 0, wrapZ_ = 0;
    float wrapExtent_ = 1024;
};

const char* stanceName(Stance s);

} // namespace df
