#include "Tracers.h"

#include "../render/Gl.h"

#include <vector>

namespace df {

namespace {

// aDepth: 1 to test this end at 1000 m of view depth (uClampZ: that depth's
// NDC z under this projection).
const char* kTracerVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aClamp;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uClampZ;
out vec4 vColor;
void main() {
    vec4 clip = uProj * (uView * vec4(aPos, 1.0));
    if (aClamp > 0.5) clip.z = uClampZ * clip.w;
    gl_Position = clip;
    vColor = aColor;
}
)";

const char* kTracerFS = R"(#version 330 core
in vec4 vColor;
out vec4 oColor;
void main() { oColor = vColor; }
)";

} // namespace

Tracers::~Tracers() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (prog_) glDeleteProgram(prog_);
}

void Tracers::push(Vec3 a, Vec3 b, uint32_t colour) {
    next_ = (next_ + 1) & 0x7ff;
    ring_[next_] = Line{a, b, 0x1e, colour};
}

void Tracers::clear() {
    for (auto& l : ring_) l = Line{};   // 0x4633c0 (the index is kept)
    started_ = false;
}

void Tracers::age(uint32_t ticks, bool paused) {
    int elapsed = started_ ? int(ticks - lastTicks_) : 0;
    lastTicks_ = ticks;
    started_ = true;
    if (elapsed < 0 || paused) elapsed = 0;
    for (auto& l : ring_) {
        if (l.life <= 0) continue;
        l.life -= elapsed;
        if ((l.life >> 2) < 1) l.life = 0;
    }
}

void Tracers::draw(const Mat4& view, const Mat4& proj, const std::function<Vec3(Vec3)>& nearest) {
    std::vector<float> v;
    for (const auto& l : ring_) {
        if (l.life <= 0) continue;
        const Vec3 a = nearest(l.a), b = a + (l.b - l.a);
        auto depth = [&](Vec3 p) { return -(view.m[2] * p.x + view.m[6] * p.y + view.m[10] * p.z + view.m[14]); };
        const float clampDepth = depth(a) > 1000.f || depth(b) > 1000.f ? 1.f : 0.f;
        const float r = float((l.colour >> 16) & 0xff) / 255.f, g = float((l.colour >> 8) & 0xff) / 255.f,
                    bl = float(l.colour & 0xff) / 255.f, al = float((l.life >> 2) << 3) / 256.f;
        v.insert(v.end(), {a.x, a.y, a.z, r, g, bl, al, clampDepth, b.x, b.y, b.z, r, g, bl, al, clampDepth});
    }
    if (v.empty()) return;
    if (!prog_) {
        prog_ = compileProgram(kTracerVS, kTracerFS);
        if (!prog_) return;
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        const GLsizei stride = 8 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(7 * sizeof(float)));
        glBindVertexArray(0);
    }
    // NDC z of view depth 1000 m: (P10·(-1000) + P14) / 1000.
    const float clampZ = (proj.m[10] * -1000.f + proj.m[14]) / 1000.f;
    glUseProgram(prog_);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "uView"), 1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "uProj"), 1, GL_FALSE, proj.m);
    glUniform1f(glGetUniformLocation(prog_, "uClampZ"), clampZ);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, std::ptrdiff_t(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_LINES, 0, GLsizei(v.size() / 8));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace df
