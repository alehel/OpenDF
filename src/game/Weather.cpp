#include "Weather.h"

#include <cmath>
#include <vector>

namespace df {

namespace {

const char* kRainVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

// The rain material (0x4e6ed0): colour +0x14 = 0x806a7e9a, RGB (106,126,154);
// the software line (0x45fcf0 -> 0x460098) blends dst·(255 - 100) + colour·100
// with level +0x41 = 0x64.
const char* kRainFS = R"(#version 330 core
out vec4 oColor;
void main() { oColor = vec4(106.0 / 255.0, 126.0 / 255.0, 154.0 / 255.0, 100.0 / 255.0); }
)";

constexpr float kTwoPi = 6.28318530718f;

} // namespace

Weather::~Weather() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (prog_) glDeleteProgram(prog_);
}

uint32_t Weather::rand16() {
    auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    rng_ = rol(rng_, 11) + rng_;
    rng_ = rol(rng_, 4) ^ 1;
    return rng_ & 0xFFFF;
}

void Weather::start(int type) {
    type_ = type == 1 || type == 2 ? type : 0;
    for (auto& p : parts_) p = Particle{};
}

void Weather::setCamera(Vec3 eye) {
    cam_[0] = int32_t(std::lround(eye.x * 65536.f));
    cam_[1] = int32_t(std::lround(-eye.z * 65536.f));
    cam_[2] = int32_t(std::lround(eye.y * 65536.f));
}

void Weather::tick(int32_t windEast, int32_t windNorth, int32_t water16, const std::function<void(Vec3)>& splash) {
    if (!type_) return;
    // 0x46bac4..0x46baea: rain falls 0x5000 a tick with up to 5 new drops a
    // tick, snow 0x800 with 1.
    const int32_t dz = type_ == 1 ? -0x5000 : -0x800;
    const int maxSpawn = type_ == 1 ? 5 : 1;
    const int32_t floor = water16 + 0x400;
    int spawned = 0;
    for (auto& p : parts_) {
        if (p.active && p.z <= floor) {
            // Down to the water level (+1/64 m; 0 when the map has none):
            // effect 17 there, the slot freed for the next tick (0x46bb0b..0x46bb29).
            splash(Vec3{p.x / 65536.f, floor / 65536.f, -p.y / 65536.f});
            p.active = false;
            continue;
        }
        if (p.active && p.z > -0x10000) {
            // The full wind velocity per tick (0x46bb3b..0x46bb52).
            p.x += windEast;
            p.y += windNorth;
            p.z += dz;
            continue;
        }
        if (spawned >= maxSpawn) continue;
        // 0x46bb5c..0x46bbca: angle rand << 16, radius rand << 6 (0..64 m,
        // uniform in radius), through the 2048-step table 0x41ac00 without
        // interpolation (x cos, y sin, >> 22); 16 m above the camera.
        const uint32_t ang = rand16() << 16;
        const int64_t R = int64_t(rand16()) << 6;
        const float a = float(ang >> 21) * kTwoPi / 2048.f;
        const int64_t c = int64_t(std::lround(std::cos(a) * 4194304.0)), s = int64_t(std::lround(std::sin(a) * 4194304.0));
        p.x = cam_[0] + int32_t((R * c) >> 22);
        p.y = cam_[1] + int32_t((R * s) >> 22);
        p.z = cam_[2] + 0x100000;
        p.active = true;
        ++spawned;
    }
}

void Weather::drawRain(const Mat4& viewProj, Vec3 eye, float extent, int32_t windEast, int32_t windNorth) {
    if (type_ != 1) return;
    if (!prog_) {
        prog_ = compileProgram(kRainVS, kRainFS);
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindVertexArray(0);
    }
    if (!prog_) return;
    // A streak from P to Q = P - half a tick of wind, 0.5 m up (0x46b7c3..):
    // slanted with the wind.
    std::vector<float> v;
    v.reserve(parts_.size() * 6);
    auto wrap = [&](float a, float ref) { return extent > 0 ? a - std::floor((a - ref) / extent + 0.5f) * extent : a; };
    for (const auto& p : parts_) {
        if (!p.active) continue;
        const float x = wrap(p.x / 65536.f, eye.x), z = wrap(-p.y / 65536.f, eye.z), y = p.z / 65536.f;
        v.insert(v.end(), {x, y, z, x - windEast / 131072.f, y + 0.5f, z + windNorth / 131072.f});
    }
    if (v.empty()) return;
    glUseProgram(prog_);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "uViewProj"), 1, GL_FALSE, viewProj.m);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, std::ptrdiff_t(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);   // INFERRED: z-tested against the frame's depth, not written (the software line rasterizer 0x46014f tests z unsigned against view+0xc64; its write-back was not traced)
    glDrawArrays(GL_LINES, 0, GLsizei(v.size() / 3));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace df
