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
    bool dead = false;               // the player is dead (+0x64 & 4): DF2's 0x4145a0 lets go of a gun
};

class Player {
public:
    Vec3 feet;
    float yaw = 0, pitch = 0;        // yaw: compass radians, pitch: up positive
    Stance stance = Stance::Stand;
    bool fly = false;                // debug free flight (N)
    bool chute = false;              // Delta Force 2: on a parachute (0.133 m a tick down)
    bool pfoil = true;               // the player item's attribute PFoil (0x400): the chute glides (DF2.EXE 0x440651)
    const AnimDefs* moves = nullptr; // the soldier's .ANM table
    bool oneHanded = false;          // pistol: the *_1HW moves
    // Delta Force 2: the current mode's raw flags +0x3C (0x18 _2HW moves,
    // 4 _1HW, neither the plain WalkingN / RunningN; 0x10 SLW, 8 2HW,
    // 0x200 BIN poses) and the scope in (+0x74 & 0x80): GOG Df2.exe 0x445980.
    int df2ModeFlags = 0;
    bool scoped = false;
    // Eye height above the feet per stance (stand, crouch, prone) and hand
    // (two-, one-handed): the pose's origin height (.CHR contact points) plus
    // DF.EXE's eye offset (0x449913). Filled from the game data; defaults
    // are the PLAYER01.CHR values.
    float eyeHeights[3][2] = {{1.7558f, 1.7677f}, {1.3768f, 1.3768f}, {0.5031f, 0.5031f}};
    // Origin (pelvis) height above the lowest contact point of the Floating
    // and Swimming poses (PLAYER01.CHR animations 134 / 133).
    float floatOrigin = 0.1489f, swimOrigin = 0.3184f;

    // `world` is changed only by GOG Df2.exe's emplaced-gun links (0x4145a0).
    void update(float dt, const PlayerInput& in, World& world);
    // GOG Df2.exe: manning an emplaced gun (+0x8c): the eye is the gun's
    // (0x416e90 instead of the normal view, 0x44dd50).
    // Delta Force: the Crouch / Prone / Stand commands are latched (GOG
    // Df.exe 0x477d40: 0xa1 [0xc23a04], 0xa2 [0xc23a08], 0xa4 [0xc23a0c])
    // and taken in update's on-ground branch (0x488200).
    void requestStance(Stance s) { (s == Stance::Crouch ? reqCrouch_ : s == Stance::Prone ? reqProne_ : reqStand_) = true; }
    bool mounted() const { return mounted_; }
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
    Vec3 eye() const { return mounted_ ? mountEye_ : inWater_ ? Vec3{feet.x, originY_ + eyeOffset_, feet.z} : Vec3{feet.x, feet.y + eyeHeight_, feet.z}; }
    Vec3 forward() const;
    // Body origin (the pelvis; DF.EXE's object z): what blasts measure to.
    Vec3 origin() const { return inWater_ ? Vec3{feet.x, originY_, feet.z} : Vec3{feet.x, feet.y + landOrigin(), feet.z}; }
    bool moving() const { return moving_; }
    // Off the ground (DF2 +0x64 & 0x2000).
    bool airborne() const { return !onGround_; }
    // The move being played (an .ANM name), for the body's animation.
    const std::string& move() const { return move_; }
    float speed() const { return speed_; }
    // Delta Force 2: standing in a type-6 collision volume this tick (person
    // flag +0x64 & 0x1000000, 0x440acf): Request Weapons is accepted there.
    bool armory() const { return armory_; }
    // Delta Force 2: on a ladder (a type-4 volume, +0x64 & 0x400000).
    bool onLadder() const { return ladder_; }
    // The object stood on (DF1 +0x100, DF2 +0xF8; -1 none): it carries the
    // player along (DF.EXE 0x43634d, DF2.EXE 0x440876).
    int platform() const { return platform_; }
    // The vertical speed (m/s, down negative) of the last landing; 0 after read.
    float takeLanding() { const float v = landingSpeed_; landingSpeed_ = 0; return v; }

private:
    void updateInWater(float dt, const PlayerInput& in, const World& world, float water);
    // Delta Force 2's person move for the player, one 60 Hz tick (DF2.EXE
    // 0x440cd0 jump key, 0x43f690 move chooser, 0x440400 physics).
    void tickDf2(const PlayerInput& in, World& world);
    void clampToMount(const World& world);
    bool mounted_ = false;
    Vec3 mountEye_{0, 0, 0};
    void enterWater(float z);
    void leaveWater(float z);
    float df2Clock_ = 0;
    uint32_t df2Ticks_ = 0;
    float vz_ = 0;                   // DF2: vertical speed, metres a tick (+0x44)
    Vec3 step_{0, 0, 0};             // DF2: the stored horizontal step of the move (0x9e2260)
    float stepDz_ = 0;               // and its vertical part (swimming)
    float poseOrigin_ = 0;           // DF2: the chosen move's origin height above the feet
    bool ladder_ = false, armory_ = false;
    std::vector<int> neighbours_;    // DF2 +0xFC: the objects that can push (0x43f2b0)
    int platform_ = -1;              // DF1 +0x100 / DF2 +0xF8
    float carryClock_ = 0;           // DF1: the 60 Hz ticks of the platform carry
    uint32_t listTicks_ = 0x40;      // DF2 [0x9e2e60]: rebuilt when it reaches 0x40

    std::string move_ = "Standing_2HW";
    float eyeHeight_ = 1.7558f;
    // In water (DF.EXE obj+0x6e bit 2) the body origin is tracked directly
    // and the eye sits eyeOffset_ above it.
    bool inWater_ = false, swimMove_ = false;
    float originY_ = 0, eyeOffset_ = 0;
    std::vector<Spawn> spawns_;
    bool sliding_ = false;
    bool reqCrouch_ = false, reqProne_ = false, reqStand_ = false;
    bool jumpArmed_ = true;          // DF1 GOG [0xc23a10]: set while the jump key is up (0x4795a0)
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
