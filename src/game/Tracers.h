// Delta Force's round tracers (GOG Df.exe 0x463350 push, 0x4634d0 draw;
// ring 0x9e3098): every tick a round leaves a line from where it was to
// where it went (or the hit point), faded out over 30 ticks.
#pragma once

#include "../render/Math.h"

#include <array>
#include <cstdint>
#include <functional>

namespace df {

class Tracers {
public:
    Tracers() = default;
    Tracers(const Tracers&) = delete;
    Tracers& operator=(const Tracers&) = delete;
    ~Tracers();
    // 0x463350: the next of 2048 slots (index pre-incremented & 0x7ff),
    // life 0x1e; the colour is the round's +0x88.
    void push(Vec3 a, Vec3 b, uint32_t colour);
    // Mission start (0x4633f0 -> 0x4633c0): every line gone.
    void clear();
    // 0x4634d0, at the end of the 3D view (0x485731): every live line loses
    // the ticks run since the last call ([0x6e4724] - [0xa158a0]; none
    // while paused, [0x6e4718]), is dropped once life >> 2 < 1, and is
    // drawn through 0x462df0(0xa00) -> 0x462520: a depth-tested 1-pixel
    // line, no depth write, each channel dst + (colour - dst)·a/256 with
    // a = (life >> 2) << 3. A line with an end beyond 1000 m of view depth
    // (0x3e80001) is tested at 1000 m at both ends (-0x6000000 = 1000 << 22).
    // `ticks` is the game's tick counter; `nearest` maps a point to its
    // copy nearest the camera on the wrapping map.
    void age(uint32_t ticks, bool paused);
    void draw(const Mat4& view, const Mat4& proj, const std::function<Vec3(Vec3)>& nearest);

private:
    struct Line { Vec3 a{0, 0, 0}, b{0, 0, 0}; int life = 0; uint32_t colour = 0; };
    std::array<Line, 0x800> ring_{};
    unsigned next_ = 0;                 // [0x4f9148]
    uint32_t lastTicks_ = 0;            // [0xa158a0]
    bool started_ = false;
    unsigned prog_ = 0, vao_ = 0, vbo_ = 0;
};

} // namespace df
