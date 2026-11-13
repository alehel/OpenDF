#include "Player.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace df {

namespace {

// Eye height above the feet. DF.EXE (0x449913) puts the eye 0.7 m above the
// body origin (0.4 m prone, 0.3 m in water) and eases toward it by 0.9 per
// tick. Standing, the origin is 0.75 m above the feet (the collision box,
// 0x437a28, runs from -0.75 to +1.0 m), so the eye is 1.45 m up. Crouched and
// prone the origin follows the animation frames (.CHR), not decoded yet, so
// those two heights are still approximations.
float eyeTarget(Stance s) {
    switch (s) {
    case Stance::Crouch: return 1.0f;    // approximation
    case Stance::Prone: return 0.35f;    // approximation
    default: return 0.75f + 0.7f;
    }
}

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
    // The eye eases toward its target by a factor 0.9 per 60 Hz tick.
    eyeHeight_ = eyeTarget(stance) + (eyeHeight_ - eyeTarget(stance)) * std::pow(0.9f, dt * 60.f);

    Vec3 flatFwd{std::sin(yaw), 0, -std::cos(yaw)};
    Vec3 right{-flatFwd.z, 0, flatFwd.x};
    Vec3 wish = flatFwd * in.forward + right * in.strafe;
    float len = std::sqrt(dot(wish, wish));
    if (len > 1) wish = wish * (1.f / len);
    moving_ = len > 0.01f;

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

    speed_ = 0;
    if (moving_) {
        // The original has no crouched moves: a crouching soldier stands up
        // to move (inferred from the move tables, which have none).
        if (stance == Stance::Crouch) stance = Stance::Stand;
        const int dir = moveDirection(in.forward, in.strafe);
        std::string move;
        float fallback;
        if (stance == Stance::Prone) {
            move = "Crawling_CR" + std::to_string(dir);
            fallback = 0.5f;
        } else {
            move = std::string(in.walk ? "Walking" : "Running") + std::to_string(dir) + (oneHanded ? "_1HW" : "_2HW");
            fallback = in.walk ? 1.0f : 5.0f;
        }
        const AnimMove* m = moves ? moves->find(move) : nullptr;
        speed_ = m ? m->velocity : fallback;
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
    Vec3 delta = wish * (speed_ * dt);
    if (sliding_) delta = delta + Vec3{-gx, 0, -gz} * (kSlideRate * dt);

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
