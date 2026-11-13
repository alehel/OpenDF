#include "Player.h"

#include <algorithm>
#include <cmath>

namespace df {

namespace {

struct StanceSpec { float eye, height, run, walk; };

// Speeds in m/s. Approximations: a jog when running, a careful walk with Shift.
StanceSpec spec(Stance s) {
    switch (s) {
    case Stance::Crouch: return {1.0f, 1.15f, 2.0f, 1.0f};
    case Stance::Prone: return {0.35f, 0.5f, 0.7f, 0.4f};
    default: return {1.65f, 1.8f, 5.0f, 2.0f};
    }
}

constexpr float kRadius = 0.3f;
constexpr float kStep = 0.45f;       // highest ledge walked onto without jumping
constexpr float kMaxSlope = 1.4f;    // rise per metre (~55°) beyond which ground blocks

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
    const StanceSpec st = spec(stance);
    eyeHeight_ += (st.eye - eyeHeight_) * std::min(1.f, dt * 8.f);

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

    speed_ = in.walk ? st.walk : st.run;
    Vec3 delta = wish * (speed_ * dt);

    // Refuse to walk up walls of terrain that are too steep.
    const float groundHere = world.groundHeight(feet, kStep);
    auto tryMove = [&](Vec3 d) {
        Vec3 next = feet + d;
        float g = world.groundHeight(next, kStep);
        float horiz = std::sqrt(d.x * d.x + d.z * d.z);
        if (onGround_ && horiz > 0 && g - groundHere > std::max(kStep, horiz * kMaxSlope)) return false;
        feet = next;
        return true;
    };
    if (!tryMove(delta)) {
        if (!tryMove({delta.x, 0, 0})) tryMove({0, 0, delta.z});
    }
    feet = world.resolve(feet, kRadius, st.height, kStep);

    if (in.jump && onGround_ && stance == Stance::Stand) {
        vy_ = 4.2f;
        onGround_ = false;
    }
    vy_ -= 9.81f * dt;
    feet.y += vy_ * dt;
    const float ground = world.groundHeight(feet, kStep);
    if (feet.y <= ground) {
        feet.y = ground;
        vy_ = 0;
        onGround_ = true;
    } else if (onGround_ && feet.y - ground < kStep && vy_ <= 0) {
        feet.y = ground;   // follow the ground down small steps and slopes
        vy_ = 0;
    } else {
        onGround_ = false;
    }

    const float ext = world.extent();
    feet.x = std::fmod(std::fmod(feet.x, ext) + ext, ext);
    feet.z = std::fmod(std::fmod(feet.z, ext) + ext, ext);
}

} // namespace df
