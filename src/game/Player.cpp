#include "Player.h"

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
constexpr float kRadius = 0.75f;          // collision box half-width (0x437a28)
constexpr float kHeight = 1.75f;          // box from -0.75 to +1.0 m around the origin
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

void Player::update(float dt, const PlayerInput& in, const World& world) {
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

    // Move selection (DF.EXE 0x436fd0): prone crawls; otherwise walk (NoRun
    // or steep uphill) or run in the standard gait, also when crouched (the
    // crouch flag stays set, the soldier is crouched again when he stops);
    // the *_S_* moves while the weapon's refire time runs. A move the .ANM
    // does not list has speed 0 in DF.EXE's table (e.g. running while
    // shooting: PLAYANIM.ANM has no Running*_S_* entries).
    speed_ = 0;
    if (moving_) {
        const int dir = moveDirection(in.forward, in.strafe);
        std::string move;
        float fallback;
        if (stance == Stance::Prone) {
            move = "Crawling_CR" + std::to_string(dir);
            fallback = 0.5f;
        } else {
            move = std::string(in.walk ? "Walking" : "Running") + std::to_string(dir) + (in.shooting ? "_S" : "") +
                   (oneHanded ? "_1HW" : "_2HW");
            fallback = in.walk ? 1.0f : 5.0f;
        }
        if (moves && !moves->empty()) {
            const AnimMove* m = moves->find(move);
            speed_ = m ? m->velocity : 0.f;
        } else {
            speed_ = fallback;
        }
        move_ = move;
    } else {
        // Standing still: the stance's pose (_S_ while the refire time runs).
        const char* base = stance == Stance::Prone ? "Prone" : stance == Stance::Crouch ? "Crouched" : "Standing";
        move_ = std::string(base) + (in.shooting && stance == Stance::Stand ? "_S" : "") + (oneHanded ? "_1HW" : "_2HW");
    }

    // Terrain gradient (DF.EXE 0x4332ac) for sliding and uphill walking.
    const float h0x = world.terrain->heightAt(feet.x - 0.5f, feet.z), h1x = world.terrain->heightAt(feet.x + 0.5f, feet.z);
    const float h0z = world.terrain->heightAt(feet.x, feet.z - 0.5f), h1z = world.terrain->heightAt(feet.x, feet.z + 0.5f);
    const float gx = h1x - h0x, gz = h1z - h0z;
    if (onGround_) {
        if (std::abs(gx) > kSlideStart || std::abs(gz) > kSlideStart) sliding_ = true;
        else if (std::abs(gx) < kSlideStop && std::abs(gz) < kSlideStop) sliding_ = false;
    }
    // Uphill steeper than 0.7 along the heading forces the walking moves (0x436705).
    if (moving_ && stance != Stance::Prone && gx * flatFwd.x + gz * flatFwd.z > kSlideStop && moves) {
        const std::string walk = "Walking" + std::to_string(moveDirection(in.forward, in.strafe)) + (oneHanded ? "_1HW" : "_2HW");
        if (const AnimMove* w = moves->find(walk)) speed_ = std::min(speed_, w->velocity);
    }
    // Direction: heading + dir·45°, at the move's speed (0x43661c); in the air
    // the take-off motion is kept (0x43678c); sliding replaces movement.
    Vec3 delta;
    if (sliding_ && onGround_) delta = Vec3{-gx, 0, -gz} * (kSlideRate * dt);
    else if (!onGround_) delta = airDelta_ * dt;
    else {
        Vec3 dirv{0, 0, 0};
        if (moving_) {
            const float a = yaw - float(moveDirection(in.forward, in.strafe)) * 3.14159265f / 4.f;
            dirv = Vec3{std::sin(a), 0, -std::cos(a)};
        }
        airDelta_ = dirv * speed_;
        delta = airDelta_ * dt;
    }

    feet = feet + delta;
    feet = world.resolve(feet, kRadius, kHeight, kStep);

    // Jump (0x4375de): prone, the key stands the soldier up; otherwise 4 m/s up
    // and 1/16 m at once (not while sliding).
    if (in.jump && !jumpHeld_) {
        if (stance == Stance::Prone) stance = Stance::Stand;
        else if (onGround_ && !sliding_) { vy_ = kJumpSpeed; feet.y += kJumpLift; onGround_ = false; }
    }
    jumpHeld_ = in.jump;

    // Ground (0x43655b..0x436a07): below ground snaps up; within 1/32 m holds;
    // higher up gravity applies, and over 0.5 m (or moving up) is airborne
    // until landing. No falling damage.
    const float ground = world.groundHeight(feet, kStep);
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
        d = feet.y - world.groundHeight(feet, kStep);
        if (d < 0) { feet.y -= d; vy_ = 0; onGround_ = true; }
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
// and view pitch, vertically at half rate, and does not rise once the
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
        horiz = Vec3{std::sin(yaw), 0, -std::cos(yaw)} * (speed_ * std::cos(pitch));
        vert = speed_ * std::sin(pitch) * 0.5f;
        if (vert > 0 && originY_ + kSwimDepth > water) vert = 0;
    }
    Vec3 f{feet.x + horiz.x * dt, originY_ - poseOrigin, feet.z + horiz.z * dt};
    f = world.resolve(f, kRadius, kHeight, kStep);
    feet.x = f.x;
    feet.z = f.z;
    originY_ += vert * dt;
    const float ground = world.groundHeight({feet.x, originY_ - poseOrigin, feet.z}, kStep);
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

void Player::wrap(float ext) {
    wrapExtent_ = ext;
    const float wx = std::floor(feet.x / ext), wz = std::floor(feet.z / ext);
    wrapX_ += int(wx);
    wrapZ_ += int(wz);
    feet.x -= wx * ext;
    feet.z -= wz * ext;
}

} // namespace df
