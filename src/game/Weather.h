// Delta Force 2's rain and snow (DF2.EXE 0x46ba90 type, 0x46baa0 per-tick
// simulation, 0x46b780 drawing): 1024 particles spawned around the camera,
// falling and drifting with the mission's wind.
#pragma once

#include "../render/Gl.h"
#include "../render/Math.h"

#include <array>
#include <cstdint>
#include <functional>

namespace df {

class Weather {
public:
    ~Weather();
    // 0x46ba90: 0 none, 1 rain, 2 snow (READ: type 1 splashes with effect 17,
    // item 107017 "rain poink"; type 2 draws item 107018 "snow", 0x46b94d);
    // clears every particle.
    void start(int type);
    int type() const { return type_; }
    // One sim tick (0x46baa0). Wind and water level in 16.16 m (DF2 axes: x
    // east, y north); `splash` gets effect 17's position, OpenDF axes.
    void tick(int32_t windEast, int32_t windNorth, int32_t water16, const std::function<void(Vec3)>& splash);
    // The camera of the last rendered frame (saved at the end of 0x46b780),
    // OpenDF axes.
    void setCamera(Vec3 eye);
    // Rain streaks (0x46b780 rain branch) after the fog pass, depth-tested,
    // not writing depth. `eye` maps each particle to its nearest copy on the
    // wrapping map (`extent` metres).
    void drawRain(const Mat4& viewProj, Vec3 eye, float extent, int32_t windEast, int32_t windNorth);
    // Snow: each live flake's position (OpenDF axes), for the caller's billboard.
    template <class F> void forEachFlake(F&& f) const {
        for (const auto& p : parts_)
            if (p.active) f(Vec3{p.x / 65536.f, p.z / 65536.f, -p.y / 65536.f});
    }

private:
    struct Particle { int32_t x = 0, y = 0, z = 0; bool active = false; };
    std::array<Particle, 1024> parts_{};   // 0x4e9240, 20 bytes each
    int type_ = 0;                         // 0x4ee270
    int32_t cam_[3] = {0, 0, 0};           // 0x4ee264..0x4ee26c, GOG 0x512e84..0x512e8c: the player origin (x east, y north, z up)
    // DF2.EXE's generator 0x41ac40 (x = rol(x,11) + x, x = rol(x,4) ^ 1, 16
    // bits). DF2 shares one state with its other callers (shrapnel reseeds
    // it to 0x5FC per burst); OpenDF keeps a separate state for the weather.
    uint32_t rng_ = 0x5FC;
    uint32_t rand16();
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
};

} // namespace df
