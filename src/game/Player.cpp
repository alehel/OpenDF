#include "Player.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace df {

namespace {

struct StanceSpec { float eye, height; };

// Eye height and body height in metres: approximations.
StanceSpec spec(Stance s) {
    switch (s) {
    case Stance::Crouch: return {1.0f, 1.15f};
    case Stance::Prone: return {0.35f, 0.5f};
    default: return {1.65f, 1.8f};
    }
}

// The original's eight move directions: 0 forward, then counter-clockwise
// (1 left-forward, 2 left, 3 left-back, 4 back, 5 right-back, 6 right,
// 7 right-forward), as the PLAYANIM.ANM comments for the running moves say.
int moveDirection(float forward, float strafe) {
    const float a = std::atan2(-strafe, forward);   // left positive
    return (int(std::lround(a / (3.14159265f / 4))) + 8) % 8;
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
