#include "TracerLines.h"

namespace df {

namespace {

const char* kTracerVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() { vColor = aColor; gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

const char* kTracerFS = R"(#version 330 core
in vec4 vColor;
out vec4 oColor;
void main() { oColor = vColor; }
)";

} // namespace

TracerLines::~TracerLines() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (prog_) glDeleteProgram(prog_);
}

void TracerLines::flush(const Mat4& viewProj) {
    if (verts_.empty()) return;
    if (!prog_) {
        prog_ = compileProgram(kTracerVS, kTracerFS);
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
        glBindVertexArray(0);
    }
    if (!prog_) return;
    glUseProgram(prog_);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "uViewProj"), 1, GL_FALSE, viewProj.m);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, std::ptrdiff_t(verts_.size() * sizeof(float)), verts_.data(), GL_STREAM_DRAW);
    // Blended over the scene, depth-tested, no depth write (as the other
    // translucent lines).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_LINES, 0, GLsizei(verts_.size() / 7));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace df
