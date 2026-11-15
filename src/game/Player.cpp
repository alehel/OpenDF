#include "Player.h"

#include "Moves.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace df {

namespace {

// The original's eight move directions: 0 forward, then counter-clockwise
// (1 left-forward, 2 left, 3 left-back, 4 back, 5 right-back, 6 right,
// 7 right-forward), as the PLAYANIM.ANM comments for the running moves say.
int moveDirection(float forward, float strafe) {
    const float a = std::atan2(-strafe, forward);   // left positive
    return (int(std::lround(a / (3.14159265f / 4))) + 8) % 8;
}

// DF.EXE values (player controller 0x436fd0, physics 0x436158):
constexpr float kStep = 0.5f;             // highest floor stepped onto (0x435dc0, 0x8000)
constexpr float kGravity = 0xb2 * 3600.f / 65536.f;   // 0xb2 per tick², 9.78 m/s²
constexpr float kJumpSpeed = 0x1111 * 60.f / 65536.f; // 4.0 m/s
constexpr float kJumpLift = 0x1000 / 65536.f;          // 1/16 m at once
constexpr float kGlue = 0x800 / 65536.f;               // ground within 1/32 m holds the feet
constexpr float kAirborne = 0x8000 / 65536.f;          // more than 0.5 m above ground: airborne
constexpr float kSlideStart = 0x14ccc / 65536.f;       // gradient 1.3 on either axis starts a slide
constexpr float kSlideStop = 0xb333 / 65536.f;         // until both are under 0.7
constexpr float kSlideRate = 0x1999 / 65536.f * 60.f;  // xy moves -gradient x 0.1 m per tick
// Water (0x436158, 0x43621a, 0x449918):
constexpr float kWaterDrag = 0xcccc / 65536.f;         // vertical speed x0.8 per tick, no gravity
constexpr float kSwimDepth = 26214.4f / 65536.f;       // no rising once origin + 0.4 m is above the surface
constexpr float kWaterEye = 0x4ccc / 65536.f;          // eye 0.3 m above the origin

} // namespace

const char* stanceName(Stance s) {
    switch (s) {
    case Stance::Crouch: return "CROUCH";
    case Stance::Prone: return "PRONE";
    default: return "STAND";
    }
}

Vec3 Player::forward() const {
    return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
}

void Player::update(float dt, const PlayerInput& in, World& world) {
    Vec3 flatFwd{std::sin(yaw), 0, -std::cos(yaw)};
    Vec3 right{-flatFwd.z, 0, flatFwd.x};
    Vec3 wish = flatFwd * in.forward + right * in.strafe;
    float len = std::sqrt(dot(wish, wish));
    if (len > 1) wish = wish * (1.f / len);
    moving_ = len > 0.01f;

    // The eye eases toward its target by a factor 0.9 per 60 Hz tick (DF.EXE
    // 0x449913; the pose change itself follows the animation in the original).
    // Moving while crouched plays the standing gait (the crouch stays set).
    const int pose = stance == Stance::Prone ? 2 : (stance == Stance::Crouch && !moving_) ? 1 : 0;
    const float target = eyeHeights[pose][oneHanded ? 1 : 0];
    eyeHeight_ = target + (eyeHeight_ - target) * std::pow(0.9f, dt * 60.f);

    const float water = world.terrain->waterLevel();
    mounted_ = false;
    if (world.df2Rules && !fly) {
        // Delta Force 2: its own person move, on the 60 Hz ticks.
        for (df2Clock_ += dt; df2Clock_ >= 1.f / 60.f; df2Clock_ -= 1.f / 60.f) tickDf2(in, world);
        wrap(world.extent());
        // GOG Df2.exe: the gun's pose follows the player's view, and the
        // view comes from the gun (0x44dd50 -> 0x416e90).
        // 0x447120 takes the angle input and clamps it in the same pass, so
        // the view never shows an angle past the limits: the mouse turns the
        // player every frame, the odd ticks only 30 times a second.
        if (world.playerMount >= 0) clampToMount(world);
        world.playerYaw = yaw;
        world.playerPitch = pitch;
        mounted_ = world.playerMount >= 0;
        if (mounted_) mountEye_ = world.mountEye(world.playerMount, feet);
        return;
    }
    // DF.EXE 0x436158 runs for the player too: riding the platform +0x100
    // every 60 Hz tick (0x43634d), in every state. OpenDF moves the DF1
    // player by the frame time, so the carry runs on its own tick clock.
    if (!fly) {
        for (carryClock_ += dt; carryClock_ >= 1.f / 60.f; carryClock_ -= 1.f / 60.f) world.carry(platform_, feet, yaw);
    } else {
        platform_ = -1;
    }
    if (inWater_ && !fly) {
        updateInWater(dt, in, world, water);
        return;
    }

    if (fly) {
        Vec3 f = forward();
        Vec3 move = f * in.forward + right * in.strafe;
        if (in.up) move.y += 1;
        if (in.down) move.y -= 1;
        float m = std::sqrt(dot(move, move));
        if (m > 0) feet = feet + move * ((in.walk ? 120.f : 25.f) * dt / m);
        feet.y = std::max(feet.y, world.terrain->heightAt(feet.x, feet.z));
        speed_ = 0;
        return;
    }

    // Stance and jump commands (GOG Df.exe 0x488200, CD 0x436fd0), only in
    // the on-ground branch (not airborne +0x6d & 0x20, water +0x6e & 2 or
    // sliding +0x6e & 4); latched until then. In order: jump, Prone, Crouch,
    // Stand. Prone and Crouch toggle: pressed again they stand up. Standing
    // up on a platform (+0x100) DF raises the origin by 0.5 m from prone or
    // 0.25 m from a crouch (also crouching from prone, a GOG change), which
    // keeps the new pose's feet within the 0.5 m step of the platform's
    // top; OpenDF tracks the feet, which stay on the platform.
    // move_jump is a press command in GOG (key table 0x4fb2fc flag 1, CD 5)
    // re-armed by [0xc23a10] while the jump key is up (0x4795a0), so holding
    // it jumps once; prone, it stands the soldier up instead. Either way the
    // press is used up: the 0x400 bit is a one-poll event (0x477d40 sets it
    // from the key press, 0x479750 clears [0xc23a00] via cmd 0xa3 each
    // poll), so a press made in the air, sliding, or the press that stands
    // the soldier up from prone is lost, even if the key is held on. Only a
    // jump uses up the re-arm [0xc23a10] (0x488200); standing up does not.
    if (!in.jump) jumpArmed_ = true;
    bool jumpNow = false;
    if (onGround_ && !sliding_) {
        if (in.jumpPress) {
            if (stance == Stance::Prone) stance = Stance::Stand;
            else if (jumpArmed_) { jumpNow = true; jumpArmed_ = false; }
        }
        if (reqProne_) stance = stance == Stance::Prone ? Stance::Stand : Stance::Prone;
        if (reqCrouch_) stance = stance == Stance::Crouch ? Stance::Stand : Stance::Crouch;
        if (reqStand_) stance = Stance::Stand;
        reqProne_ = reqCrouch_ = reqStand_ = false;
    }

    // Move selection (GOG Df.exe 0x488200, CD 0x436fd0), by move number
    // (the AI's perception and aim factors read it, 0x46a804 / 0x46b838).
    // f is the weapon mode's raw flags (+0xA4); the _S_ moves while the
    // refire time runs ([0xc23830] > 0). In order:
    //  airborne (+0x6d & 0x20): prone 0x70 SLW (f & 8) / 0x6f 2HW (& 4) /
    //    0x74 KNIFE (& 0x40) / 0x6e 1HW; else Jumping 0x82 / 0x83 (f & 1),
    //    0x84 / 0x85 shooting;
    //  sliding (+0x6e & 4): 0x82 Jumping_1HW;
    //  moving: prone 0x78 + dir Crawling; the uphill flag 0x80000 (set by
    //    the previous tick's physics) the walking moves, else walk (NoRun) or
    //    run: 0x14 / 0x1c (f & 0xc) + dir, shooting 0x28 / 0x30, running
    //    0x3c / 0x44, 0x50 / 0x58; also when crouched (the crouch flag
    //    stays set, the soldier is crouched again when he stops);
    //  still: standing 3 SLW (& 8), 4 SCW (the scope shown, 0x484740), 2
    //    2HW (& 4), 7 KNIFE (& 0x40), 1 1HW (shooting 0xc / 0xd / 0xb / 0xe /
    //    0xa); crouched 0x66 / 0x65 / 0x6a / 100; prone 0x70 / 0x6f / 0x6e.
    // A move the .ANM does not list has speed 0 in DF.EXE's table (e.g.
    // running while shooting: PLAYANIM.ANM has no Running*_S_* entries).
    {
        const int f = df1ModeFlags;
        const bool sh = in.shooting;
        const int dir = moveDirection(in.forward, in.strafe);
        int n;
        if (!onGround_) {
            if (stance == Stance::Prone) n = (f & 8) ? 0x70 : (f & 4) ? 0x6f : (f & 0x40) ? 0x74 : 0x6e;
            else n = (sh ? 0x84 : 0x82) + ((f & 1) ? 1 : 0);
        } else if (sliding_) {
            n = 0x82;
        } else if (moving_) {
            if (stance == Stance::Prone) n = 0x78 + dir;
            else {
                const bool walk = uphill_ || in.walk;
                const bool two = (f & 0xc) != 0;
                n = (walk ? (sh ? 0x28 : 0x14) : (sh ? 0x50 : 0x3c)) + (two ? 8 : 0) + dir;
            }
        } else if (stance == Stance::Prone) {
            n = (f & 8) ? 0x70 : (f & 4) ? 0x6f : 0x6e;
        } else if (stance == Stance::Crouch) {
            n = (f & 8) ? 0x66 : (f & 4) ? 0x65 : (f & 0x40) ? 0x6a : 100;
        } else {
            if (sh) n = (f & 8) ? 0xc : scoped ? 0xd : (f & 4) ? 0xb : (f & 0x40) ? 0xe : 0xa;
            else n = (f & 8) ? 3 : scoped ? 4 : (f & 4) ? 2 : (f & 0x40) ? 7 : 1;
        }
        const char* name = moveName(n);
        move_ = name ? name : "Null";
    }
    // The move's speed and direction (move table +0: Walking / Running /
    // Crawling N go N·45°, the others 0).
    const bool dirMove = moving_ && onGround_ && !sliding_;
    speed_ = 0;
    if (moves && !moves->empty()) {
        const AnimMove* m = moves->find(move_);
        speed_ = m ? m->velocity : 0.f;
    } else if (dirMove) {
        speed_ = stance == Stance::Prone ? 0.5f : (in.walk || uphill_) ? 1.0f : 5.0f;
    }

    // Terrain gradient (GOG Df.exe 0x47e5d0 from 0x487370; CD 0x4332ac) for
    // sliding and uphill walking: only on the ground (+0x6d & 1), not
    // airborne or in water (0x22000), and not standing on an object (+0x100,
    // 0x4874d6), where the slide and uphill flags are kept and the move is
    // used. A slide starts at 1.3 on either axis (the move still applies that
    // tick) and stops under 0.7 on both (the move applies); while it lasts
    // xy moves -gradient x 0x1999 a tick. Not sliding, uphill steeper than
    // 0.7 along the move's direction sets 0x80000, which the next move
    // selection turns into a walking move (0x4874f6 clears it first).
    bool slideMove = false;
    float gx = 0, gz = 0;
    const float moveAngle = yaw - float(dirMove ? moveDirection(in.forward, in.strafe) : 0) * 3.14159265f / 4.f;
    if (onGround_ && platform_ < 0) {
        const float h0x = world.terrain->heightAt(feet.x - 0.5f, feet.z), h1x = world.terrain->heightAt(feet.x + 0.5f, feet.z);
        const float h0z = world.terrain->heightAt(feet.x, feet.z - 0.5f), h1z = world.terrain->heightAt(feet.x, feet.z + 0.5f);
        gx = h1x - h0x;
        gz = h1z - h0z;
        uphill_ = false;
        if (sliding_) {
            if (std::abs(gx) < kSlideStop && std::abs(gz) < kSlideStop) sliding_ = false;
            else slideMove = true;
        } else if (std::abs(gx) > kSlideStart || std::abs(gz) > kSlideStart) {
            sliding_ = true;
        } else if (std::abs(gx) > kSlideStop || std::abs(gz) > kSlideStop) {
            if (gx * std::sin(moveAngle) - gz * std::cos(moveAngle) > kSlideStop) uphill_ = true;
        }
    }
    // Direction: heading + dir·45°, at the move's speed (0x48751c); in the
    // air the take-off motion is kept (0x4874e1); sliding replaces movement.
    Vec3 delta;
    if (slideMove) delta = Vec3{-gx, 0, -gz} * (kSlideRate * dt);
    else if (!onGround_) delta = airDelta_ * dt;
    else {
        airDelta_ = Vec3{std::sin(moveAngle), 0, -std::cos(moveAngle)} * speed_;
        delta = airDelta_ * dt;
    }

    feet = feet + delta;
    feet = world.resolve(feet, landOrigin());

    // Jump (CD 0x4375de / 0x437730, GOG 0x488200): 4 m/s up ([0xc2382c]
    // 0x1111) and 1/16 m at once; taken above.
    if (jumpNow) { vy_ = kJumpSpeed; feet.y += kJumpLift; onGround_ = false; }

    // Ground (0x43655b..0x436a07): below ground snaps up; within 1/32 m holds;
    // higher up gravity applies, and over 0.5 m (or moving up) is airborne
    // until landing. No falling damage.
    const float ground = world.groundHeight(feet, kStep, &platform_);   // 0x435dc0 also sets +0x100
    float d = feet.y - ground;
    if (onGround_) {
        if (d < 0) { feet.y = ground; vy_ = 0; }
        else if (d <= kGlue && vy_ <= 0) { feet.y = ground; vy_ = 0; }
        else {
            vy_ -= kGravity * dt;
            feet.y += vy_ * dt;
            if (feet.y - ground > kAirborne || vy_ > 0) onGround_ = false;
            if (feet.y < ground) { feet.y = ground; vy_ = 0; }
        }
    } else {
        vy_ -= kGravity * dt;
        feet.y += vy_ * dt;
        d = feet.y - world.groundHeight(feet, kStep, &platform_);
        if (d < 0) { feet.y -= d; landingSpeed_ = vy_; vy_ = 0; onGround_ = true; }
    }

    // Into the water (0x436577 airborne, 0x43697d walking): once the level is
    // above the body origin. Falling in splashes: effect 4 "water exp" on the
    // surface (0x4365a2).
    if (water != 0 && water > feet.y + landOrigin()) {
        if (!onGround_) spawns_.push_back({4, {feet.x, water, feet.z}});
        inWater_ = true;
        swimMove_ = false;
        originY_ = feet.y + landOrigin();
        eyeOffset_ = eyeHeight_ - landOrigin();
        onGround_ = true;
        sliding_ = false;
    }

    wrap(world.extent());
}

float Player::landOrigin() const {
    // The eye table is the pose's origin height plus DF.EXE's eye offset.
    const int pose = stance == Stance::Prone ? 2 : (stance == Stance::Crouch && !moving_) ? 1 : 0;
    return eyeHeights[pose][oneHanded ? 1 : 0] - (pose == 2 ? 0.4f : 0.7f);
}

// DF.EXE in water: the move is Swimming while the forward key is held,
// otherwise Floating (0x43752e, before any stance). No gravity: vertical
// speed decays x0.8 per tick (0x436912). Swimming moves along the heading
// and view pitch: on land a tick moves half the move's per-frame distance
// (`sar 1` at 0x436653), in water the whole of it horizontally (0x436251,
// no halving) and half vertically (0x436289), i.e. 2·v·cos(pitch) and
// v·sin(pitch) for a move of v m/s on land (DF2.EXE 0x4404dc is the same);
// it does not rise once the
// origin is within 0.4 m of the surface (0x43621a / 0x43628f). The bottom
// holds the body up (lowest contact point of the pose); the water is left
// when the origin is above the level (0x4368ee).
void Player::updateInWater(float dt, const PlayerInput& in, const World& world, float water) {
    swimMove_ = in.forward > 0;
    moving_ = swimMove_;
    move_ = swimMove_ ? "Swimming" : "Floating";
    const float poseOrigin = swimMove_ ? swimOrigin : floatOrigin;
    eyeOffset_ = kWaterEye + (eyeOffset_ - kWaterEye) * std::pow(0.9f, dt * 60.f);
    vy_ *= std::pow(kWaterDrag, dt * 60.f);
    originY_ += vy_ * dt;

    speed_ = 0;
    Vec3 horiz{0, 0, 0};
    float vert = 0;
    if (swimMove_) {
        const AnimMove* m = moves ? moves->find("Swimming") : nullptr;
        speed_ = m ? m->velocity : 2.0f;
        horiz = Vec3{std::sin(yaw), 0, -std::cos(yaw)} * (2.f * speed_ * std::cos(pitch));
        vert = speed_ * std::sin(pitch);
        if (vert > 0 && originY_ + kSwimDepth > water) vert = 0;
    }
    Vec3 f{feet.x + horiz.x * dt, originY_ - poseOrigin, feet.z + horiz.z * dt};
    f = world.resolve(f, poseOrigin);
    feet.x = f.x;
    feet.z = f.z;
    originY_ = f.y + poseOrigin;   // 0x4889c0 also pushes the origin up or down
    originY_ += vert * dt;
    const float ground = world.groundHeight({feet.x, originY_ - poseOrigin, feet.z}, kStep, &platform_);
    if (originY_ - poseOrigin < ground) {
        originY_ = ground + poseOrigin;
        if (vy_ < 0) vy_ = 0;
    }
    feet.y = originY_ - poseOrigin;

    if (originY_ > water) {
        // Out: the standing pose's origin is higher, so the feet go to the
        // ground and the eye eases from where it was.
        const float eyeY = originY_ + eyeOffset_;
        inWater_ = false;
        swimMove_ = false;
        feet.y = std::max(originY_ - landOrigin(), world.groundHeight(feet, kStep));
        eyeHeight_ = eyeY - feet.y;
        onGround_ = true;
        vy_ = 0;
    }
    wrap(world.extent());
}

void Player::enterWater(float z) {
    inWater_ = true;
    swimMove_ = false;
    originY_ = z;
    eyeOffset_ = eyeHeight_ - landOrigin();
    onGround_ = true;
    sliding_ = false;
}

void Player::leaveWater(float z) {
    // The standing pose's origin is higher, so the feet go down by the
    // difference and the eye eases from where it was.
    const float eyeY = z + eyeOffset_;
    inWater_ = false;
    swimMove_ = false;
    feet.y = z - landOrigin();
    eyeHeight_ = eyeY - feet.y;
}

// GOG Df2.exe 0x447120's limits on a gun: the heading within 0x20000000
// (45 degrees) of the gun's, the pitch within 0x10000000 (22.5 degrees).
void Player::clampToMount(const World& world) {
    const WorldObject& g = world.objects[size_t(world.playerMount)];
    float dy = std::remainder(yaw - g.heading, 2 * 3.14159265f);
    dy = std::clamp(dy, -3.14159265f / 4, 3.14159265f / 4);
    yaw = g.heading + dy;
    pitch = std::clamp(pitch, -3.14159265f / 8, 3.14159265f / 8);
}

// One tick of DF2.EXE's person move for the player (READ unless marked):
// the jump key 0x440cd0 and the move chooser 0x43f690 (GOG 0x445980) on odd ticks (the
// motion they give is stored and repeated on even ticks, 0x440858), then
// 0x440400: x, y += the step; z += vz + dz; the volume push-out (0x440984);
// the ground (terrain, or d = 0 standing on an object) and the state table
// (parachute, airborne, swimming, sliding, normal) with gravity 0xA7.
void Player::tickDf2(const PlayerInput& in, World& world) {
    const float water = world.terrain->waterLevel();
    const bool odd = (++df2Ticks_ & 1) != 0;
    const Vec3 flatFwd{std::sin(yaw), 0, -std::cos(yaw)};
    float z = inWater_ ? originY_ : feet.y + landOrigin();   // obj +0x2C, the origin
    constexpr float G = 0xA7 / 65536.f;
    if (inWater_) eyeOffset_ = kWaterEye + (eyeOffset_ - kWaterEye) * 0.9f;   // 0x4461e7

    if (odd && world.playerMount >= 0) {
        // GOG Df2.exe 0x447120 (before the person move): manning a gun, the
        // heading is kept within 0x20000000 (45 degrees) of the gun's and
        // the pitch within 0x10000000 (22.5 degrees), and the origin is put
        // back at the gun (0x418300).
        clampToMount(world);
        const Vec3 m = world.mountOrigin(world.playerMount, yaw, feet);
        feet.x = m.x;
        feet.z = m.z;
        z = m.y;
    }
    if (odd) {
        // Jump (GOG Df2.exe 0x447120, 0x44713b..0x447181): not while
        // airborne or swimming (0x22000) or on a chute (0x20); prone it
        // asks for the Prone toggle ([0x79a67c] = 1), which stands up just
        // below; else, not while sliding (0x40000), z += 0x1000 and vz =
        // 0x1555. The crouch ([0x79a2c4]) is not tested: a crouched soldier
        // jumps and stays crouched. move_jump is a held command (table
        // 0x4a5ca8 flags 0x405, as move_forward; the control bit +0x74 &
        // 0x10000 is rebuilt each pass, 0x41b18f): no latch.
        const bool press = in.jump;
        if (press && onGround_ && !inWater_ && !chute) {
            if (stance == Stance::Prone) reqProne_ = true;
            else if (!sliding_) { z += 0x1000 / 65536.f; vz_ = 0x1555 / 65536.f; }
        }
        // The stance commands (0x41f150: Crouch [0x79a678], Prone
        // [0x79a67c], Stand [0x79a680], latched by requestStance) taken on
        // this odd tick, in 0x447120's order, wherever the player is.
        // Prone and Crouch toggle; standing on an object (+0x100) the origin
        // is lifted 0x8000 (0.5 m) out of prone and 0x4000 (0.25 m) out of a
        // crouch or from prone to a crouch.
        // The origin stays where it was (the new pose's feet then fall or
        // are pushed up to the ground). GOG's eye is the origin plus an eased
        // offset ([0x79a688], 0x44dd50); OpenDF's is the feet plus an eased
        // height, so that height takes the change of the origin's height
        // above the feet, keeping the eye where it was.
        const float lo0 = landOrigin();
        const bool onObj = platform_ >= 0;
        if (reqProne_) {
            if (stance != Stance::Prone) stance = Stance::Prone;
            else { if (onObj) z += 0x8000 / 65536.f; stance = Stance::Stand; }
        }
        if (reqCrouch_) {
            if (stance != Stance::Crouch) { if (stance == Stance::Prone && onObj) z += 0x4000 / 65536.f; stance = Stance::Crouch; }
            else { if (onObj) z += 0x4000 / 65536.f; stance = Stance::Stand; }
        }
        if (reqStand_) {
            if (stance == Stance::Prone) { if (onObj) z += 0x8000 / 65536.f; }
            else if (stance == Stance::Crouch && onObj) z += 0x4000 / 65536.f;
            stance = Stance::Stand;
        }
        reqProne_ = reqCrouch_ = reqStand_ = false;
        if (!inWater_) eyeHeight_ += landOrigin() - lo0;

        // The move chooser (0x43f690) and its motion (0x4404c8..0x4406ca).
        Vec3 step{0, 0, 0};
        float dz = 0;
        speed_ = 0;
        const bool below = water != 0 && z < water;   // 0x43f6fb
        const int dir = moving_ ? moveDirection(in.forward, in.strafe) : 0;
        if (chute) {
            // 150 Parachute (0x43f704); its motion is replaced below.
            move_ = "Parachute";
            poseOrigin_ = landOrigin();
        } else if (world.playerMount >= 0) {
            // GOG Df2.exe 0x445980: on a gun (+0x8c), 107 Crouched_EWeap
            // (PLAYANIM.ANM: speed 0), before the water and the stances.
            move_ = "Crouched_EWeap";
            poseOrigin_ = landOrigin();
        } else if (below) {
            // 141 Swimming with a speed and direction 0, else 140 Floating.
            swimMove_ = moving_ && dir == 0;
            moving_ = swimMove_;
            move_ = swimMove_ ? "Swimming" : "Floating";
            poseOrigin_ = swimMove_ ? swimOrigin : floatOrigin;
        } else {
            // Land and ladder moves (GOG Df2.exe 0x445980; CD 0x43f690). The
            // hand class comes from the mode's raw flags +0x3C: & 0x18 the
            // _2HW moves, & 4 the _1HW ones, else the plain WalkingN /
            // RunningN (knife, grenades, explosives, laser, binoculars).
            const int f = df2ModeFlags;
            const char* hand = (f & 0x18) ? "_2HW" : (f & 4) ? "_1HW" : "";
            const bool crouch = stance == Stance::Crouch;
            if (stance == Stance::Prone) {
                // Prone (+0x74 & 0x20) is tested before the ladder: crawling,
                // or Prone_BIN / _SLW / _2HW (& 8) / _1HW.
                move_ = moving_ ? "Crawling_CR" + std::to_string(dir)
                                : (f & 0x200) ? "Prone_BIN" : (f & 0x10) ? "Prone_SLW" : (f & 8) ? "Prone_2HW" : "Prone_1HW";
            } else if (!moving_) {
                // Still: on a ladder and not shooting Ladder_Stand (0xf);
                // crouched BIN / SLW / 2HW / 1HW / KNIFE; standing the same
                // with SCW while the scope is in (+0x74 & 0x80, 0x44eba0),
                // the _S set while shooting (+0x64 & 0x8000000).
                if (ladder_ && !in.shooting) move_ = "Ladder_Stand";
                else if (crouch)
                    move_ = (f & 0x200) ? "Crouched_BIN" : (f & 0x10) ? "Crouched_SLW" : (f & 8) ? "Crouched_2HW"
                          : (f & 4) ? "Crouched_1HW" : "Crouched_KNIFE";
                else if (f & 0x200) move_ = "Standing_BIN";
                else
                    move_ = std::string(in.shooting ? "Standing_S" : "Standing") +
                            ((f & 0x10) ? "_SLW" : scoped ? "_SCW" : (f & 8) ? "_2HW" : (f & 4) ? "_1HW" : "_KNIFE");
            } else if (ladder_ && (dir <= 2 || dir >= 6)) {
                // On a ladder (+0x64 & 0x400000; standing walk 0x445d1b, run
                // 0x445df7, crouched walk 0x445b4e, run 0x445beb): up or down
                // by the view pitch (+0x34 in (0, 0x7fffffff) up, level or
                // down down): forward 0x800 (run 0x1000) Ladder_Up, the front
                // diagonals half with Ladder_Left / Ladder_Right, left and
                // right those moves without a climb. Ladder_Down is never
                // chosen.
                const float s = pitch > 0 ? 1.f : -1.f;
                static const int climb[8] = {2, 1, 0, 0, 0, 0, 0, 1};
                static const char* name[8] = {"Ladder_Up", "Ladder_Left", "Ladder_Left", "", "", "", "Ladder_Right", "Ladder_Right"};
                z += s * float(climb[dir]) * (in.walk ? 0x400 : 0x800) / 65536.f;
                move_ = name[dir];
            } else if (ladder_ && in.walk && !crouch) {
                // Back and its diagonals on a ladder, standing and walking:
                // move 0 Null (0x445eb7), no motion. Running, or crouched,
                // they are the land moves: the player steps off.
                move_ = "Null";
            } else {
                // Walking / running (crouched the same moves), _S_ while
                // shooting for the hand classes only.
                move_ = std::string(in.walk ? "Walking" : "Running") + std::to_string(dir) + (in.shooting && *hand ? "_S" : "") + hand;
            }
            poseOrigin_ = landOrigin();
        }
        // The move's velocity v (ANM m/s / 30 a frame) along heading + its
        // direction (move table +0, 0x4ab758: Running/Walking N -> N, the
        // ladder moves 0/4/1/7, Swimming 0).
        const AnimMove* m = moves && !moves->empty() ? moves->find(move_) : nullptr;
        const float vel = m ? m->velocity : 0.f;
        speed_ = vel;
        int moveDir = 0;
        if (move_.rfind("Ladder_", 0) == 0) moveDir = move_ == "Ladder_Down" ? 4 : move_ == "Ladder_Left" ? 1 : move_ == "Ladder_Right" ? 7 : 0;
        else if (!below && moving_) moveDir = dir;
        const float a = yaw - float(moveDir) * 3.14159265f / 4.f;
        const Vec3 along{std::sin(a), 0, -std::cos(a)};
        const float v = vel / 30.f;
        if (inWater_) {
            // 0x4404dc: v cos(pitch) horizontally, v sin(pitch) / 2 up, not
            // upward once the origin is within 0.4 m of the surface.
            step = along * (v * std::cos(pitch));
            dz = v * std::sin(pitch) * 0.5f;
            if (dz > 0 && z + kSwimDepth > water) dz = 0;
        } else {
            step = along * (v * 0.5f);   // 0x440596: half a frame a tick
        }
        // GOG Df2.exe 0x4467d0 (0x446a9f..0x446bef): the step is stored in
        // the per-person table 0xa17300 (repeated on even ticks, 0x446c31)
        // except airborne, where the stored take-off step is used.
        if (chute) {
            // 0x440651: PFoil glides 0x4000 a tick along the heading (+ the
            // Parachute move's direction 0); without PFoil no motion.
            step = pfoil ? flatFwd * (0x4000 / 65536.f) : Vec3{0, 0, 0};
            dz = 0;
            step_ = step;
        } else if (!onGround_) {
            step = step_;   // airborne: the stored take-off step (0x44082a)
        } else {
            step_ = step;
            if (!inWater_ && !ladder_ && platform_ < 0) {
                // Slopes, on the terrain only (not 0x422000, +0x100 == 0):
                // the gradient 0x46eaa0; a slide (0x40000) starts at 1.3 on
                // either axis, stops under 0.7 on both, and while it lasts
                // this odd tick moves -gradient x 0x1999 instead of the move
                // (only the locals; the stored step stays the move's, so
                // even ticks move by it). Uphill steeper than 0.7 sets
                // 0x80000, which nothing in GOG reads: the move is kept.
                float gx = 0, gz = 0;
                world.terrain->df2Gradient(feet.x, feet.z, gx, gz);
                if (sliding_) {
                    if (std::abs(gx) < kSlideStop && std::abs(gz) < kSlideStop) sliding_ = false;
                    else step = Vec3{-gx, 0, -gz} * (0x1999 / 65536.f);
                } else if (std::abs(gx) > kSlideStart || std::abs(gz) > kSlideStart) {
                    sliding_ = true;
                }
            }
        }
        stepDz_ = dz;
        tickStep_ = step;
    }

    // Riding the platform +0xF8 (0x440876..0x440944), then the movement
    // (0x440947..0x44096f); +0x64 &= ~0x01400000.
    world.carry(platform_, feet, yaw);
    const Vec3 move = odd ? tickStep_ : step_;   // 0x446c52: the locals this tick
    feet.x += move.x;
    feet.z += move.z;
    z += vz_ + stepDz_;
    ladder_ = armory_ = false;

    // Push-out (0x440984): the neighbour list is rebuilt every 0x41 ticks
    // (0x440180), the player's margin is a person's 10 m.
    if (listTicks_ >= 0x40) { listTicks_ = 0; neighbours_ = world.df2Neighbours({feet.x, z, feet.z}, 10.f); }
    else ++listTicks_;
    bool onObject = false;
    if (!neighbours_.empty()) {
        Vec3 origin{feet.x, z, feet.z};
        const World::Df2Contact c = world.df2PushOut(origin, -poseOrigin_, neighbours_, true);   // the player: +0x64 & 1
        feet.x = origin.x;
        feet.z = origin.z;
        z = origin.y;
        onObject = c.onObject;
        if (c.onObject) platform_ = c.platform;   // 0x440a24
        // GOG Df2.exe 0x446e0a: a ladder clears airborne (0x2000) and sets
        // 0x400000; not prone (+0x74 & 0x20, 0x446e15) vel_z is zeroed too
        // (0x446e1c), so only the ladder moves change the height.
        if (c.ladder) {
            onGround_ = true;
            ladder_ = true;
            if (stance != Stance::Prone) vz_ = 0;
        }
        armory_ = c.armory;
        // GOG Df2.exe 0x4467d0: type-7 volumes (flags 8) -> 0x4145a0 with
        // the control direction (+0x74 & 7, 0x41efd0: the keys held, 0
        // with none) and the person's dead flag (+0x64 & 4: the move runs
        // for the dead player too, and 0x4145a0 then clears both links); a
        // new link puts the origin at the gun (0x418300).
        const int dir = moving_ ? moveDirection(in.forward, in.strafe) : 0;
        for (int g : c.mounts)
            if (world.df2Mount(-3, g, dir, in.dead)) {
                const Vec3 m = world.mountOrigin(g, yaw, feet);
                feet.x = m.x;
                feet.z = m.z;
                z = m.y;
            }
    }
    // 0x4467d0's end: a destroyed gun (+0x64 & 4) lets go.
    if (world.playerMount >= 0 && !world.objects[size_t(world.playerMount)].alive()) world.clearMount(-3);

    // Ground and the state table (0x440af6..0x440c9e).
    const float feetY = z - poseOrigin_;
    const float d = onObject ? 0.f : feetY - world.terrain->heightAt(feet.x, feet.z);
    if (!onObject && d <= 0) platform_ = -1;   // 0x440b1f
    const bool below = water != 0 && z < water;
    if (chute) {
        if (d > 0 && !below) vz_ = -0x2222 / 65536.f;
        else { chute = false; spawns_.push_back({-1, {feet.x, feetY, feet.z}}); }   // the canopy (0x413d70)
    } else if (!onGround_) {
        if (d <= 0) {
            landingSpeed_ = vz_ * 60.f;   // 0x43f600 (fall damage, by the game)
            z -= d;
            vz_ = 0;
            onGround_ = true;
        } else if (below) {
            // 0x440b9b: effect 4 at the surface; sound 0x8A is not in DF2's
            // sound data (silent).
            spawns_.push_back({4, {feet.x, water, feet.z}});
            enterWater(z);
        } else {
            vz_ -= G;
        }
    } else if (inWater_) {
        if (z > water) leaveWater(z);
        else if (d < 0) { z -= d; vz_ = 0; }
        else vz_ = vz_ * float(0xCCCC) / 65536.f;
    } else if (sliding_) {
        if (d < 0) z -= d;
        vz_ -= G;
    } else if (d <= 0) {
        vz_ = 0;
        z -= d;
    } else if (below) {
        enterWater(z);
        vz_ -= G;
    } else if (d > kGlue && !(ladder_ && stance != Stance::Prone)) {
        // GOG Df2.exe 0x4470a5: no gravity on a ladder unless prone.
        vz_ -= G;
        if (d > kAirborne || vz_ > 0) onGround_ = false;
    }
    if (inWater_) {
        originY_ = z;
        feet.y = z - poseOrigin_;
    } else {
        feet.y = z - landOrigin();
    }
}

void Player::wrap(float ext) {
    wrapExtent_ = ext;
    const float wx = std::floor(feet.x / ext), wz = std::floor(feet.z / ext);
    wrapX_ += int(wx);
    wrapZ_ += int(wz);
    feet.x -= wx * ext;
    feet.z -= wz * ext;
}

} // namespace df
