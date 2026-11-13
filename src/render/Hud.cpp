#include "Hud.h"

#include "../formats/Pcx.h"

#include <algorithm>
#include <cmath>

#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif

namespace df {

namespace {

const char* kHudVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreen;
out vec2 vUv;
out vec4 vColor;
out vec2 vPos;
void main() {
    vUv = aUv;
    vColor = aColor;
    vPos = aPos;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

const char* kHudFS = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
in vec2 vPos;
uniform sampler2D uTex;
uniform int uMode;      // 0 solid, 1 textured, 2 scope mask, 3 soft puff, 4 ring, 5 disc
uniform vec3 uCircle;   // centre x, y, radius in pixels
uniform float uThickness;
out vec4 oColor;
void main() {
    if (uMode == 1) { oColor = texture(uTex, vUv) * vColor; return; }
    if (uMode == 4) {
        float d = abs(length(vPos - uCircle.xy) - uCircle.z);
        oColor = vec4(vColor.rgb, vColor.a * (1.0 - smoothstep(uThickness * 0.5 - 0.5, uThickness * 0.5 + 0.5, d)));
        return;
    }
    if (uMode == 5) {
        if (length(vPos - uCircle.xy) > uCircle.z) discard;
        oColor = vec4(0.0);
        return;
    }
    if (uMode == 3) {
        float d = length(vUv * 2.0 - 1.0);
        oColor = vec4(vColor.rgb, vColor.a * smoothstep(1.0, 0.2, d));
        return;
    }
    if (uMode == 2) {
        float d = length(vPos - uCircle.xy) - uCircle.z;
        oColor = vec4(0.0, 0.0, 0.0, smoothstep(-1.5, 1.5, d));
        return;
    }
    oColor = vColor;
}
)";

// F_SML640.PCX: 16 characters per 16-pixel line, packed proportionally.
// Ink is palette index 9; index 3 (magenta) marks the extent of glyphs with
// little or no ink (space, '!', '.', ...). Runs of ink-or-marker columns in a
// line are the 16 glyphs, in character order.
constexpr int kCellH = 16, kFirstX = 9;

} // namespace

Hud::~Hud() {
    if (font_) glDeleteTextures(1, &font_);
    for (GLuint t : images_) glDeleteTextures(1, &t);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_) glDeleteProgram(prog_);
}

bool Hud::init(Vfs& vfs, std::string* error) {
    prog_ = compileProgram(kHudVS, kHudFS);
    if (!prog_) { if (error) *error = "HUD shader failed"; return false; }
    locScreen_ = glGetUniformLocation(prog_, "uScreen");
    locMode_ = glGetUniformLocation(prog_, "uMode");
    locCircle_ = glGetUniformLocation(prog_, "uCircle");
    locThickness_ = glGetUniformLocation(prog_, "uThickness");
    glUseProgram(prog_);
    glUniform1i(glGetUniformLocation(prog_, "uTex"), 0);
    glUseProgram(0);

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, r)));
    glBindVertexArray(0);

    std::vector<uint8_t> data;
    IndexedImage img;
    if (!vfs.read("F_SML640.PCX", data) || !decodePcx(data, img) || img.width < 256 || img.height < 256) {
        if (error) *error = "cannot load HUD font F_SML640.PCX";
        return false;
    }
    std::vector<uint8_t> rgba(size_t(img.width) * img.height * 4, 255);
    for (size_t i = 0; i < img.pixels.size(); ++i) rgba[i * 4 + 3] = img.pixels[i] == 9 ? 255 : 0;
    for (int line = 0; line < 16; ++line) {
        std::vector<std::pair<int, int>> runs;
        int start = -1;
        for (int x = kFirstX; x <= img.width; ++x) {
            bool used = false;
            for (int y = line * kCellH; x < img.width && y < line * kCellH + kCellH - 1; ++y) {
                uint8_t p = img.pixels[size_t(y) * img.width + x];
                if (p == 9 || p == 3) { used = true; break; }
            }
            if (used && start < 0) start = x;
            if (!used && start >= 0) { runs.emplace_back(start, x - start); start = -1; }
        }
        for (int i = 0; i < 16; ++i) {
            int c = line * 16 + i;
            if (runs.size() == 16) { glyphX_[c] = float(runs[i].first); glyphW_[c] = float(runs[i].second); }
            else { glyphX_[c] = float(12 + i * 11); glyphW_[c] = 10; }   // unused symbol lines
        }
    }
    glGenTextures(1, &font_);
    glBindTexture(GL_TEXTURE_2D, font_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    // Linear filtering: HUD text is scaled by non-integer factors.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

HudImage Hud::loadImage(Vfs& vfs, const std::string& name) {
    HudImage out;
    std::vector<uint8_t> data;
    IndexedImage img;
    if (!vfs.read(name, data) || !decodePcx(data, img)) return out;
    std::vector<uint8_t> rgba(img.pixels.size() * 4);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const Rgb& c = img.palette[img.pixels[i]];
        rgba[i * 4] = c.r; rgba[i * 4 + 1] = c.g; rgba[i * 4 + 2] = c.b;
        rgba[i * 4 + 3] = (c.r | c.g | c.b) ? 255 : 0;
    }
    glGenTextures(1, &out.texture);
    glBindTexture(GL_TEXTURE_2D, out.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    out.width = img.width;
    out.height = img.height;
    images_.push_back(out.texture);
    texture_ = 0;   // force a rebind on the next draw
    return out;
}

void Hud::begin(int width, int height) {
    width_ = width;
    height_ = height;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_);
    glUniform2f(locScreen_, float(width), float(height));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glActiveTexture(GL_TEXTURE0);
    mode_ = -1;
    texture_ = 0;
}

void Hud::end() {
    flush();
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Hud::flush() {
    if (verts_.empty()) return;
    glBufferData(GL_ARRAY_BUFFER, std::ptrdiff_t(verts_.size() * sizeof(Vertex)), verts_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts_.size()));
    verts_.clear();
}

void Hud::setMode(int mode, GLuint texture) {
    if (mode == mode_ && texture == texture_) return;
    flush();
    mode_ = mode;
    texture_ = texture;
    glUniform1i(locMode_, mode);
    if (texture) glBindTexture(GL_TEXTURE_2D, texture);
}

void Hud::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c) {
    Vertex a{x0, y0, u0, v0, c.r, c.g, c.b, c.a}, b{x1, y0, u1, v0, c.r, c.g, c.b, c.a};
    Vertex d{x0, y1, u0, v1, c.r, c.g, c.b, c.a}, e{x1, y1, u1, v1, c.r, c.g, c.b, c.a};
    verts_.insert(verts_.end(), {a, b, d, d, b, e});
}

float Hud::textWidth(const std::string& s, float scale) const {
    float w = 0;
    for (unsigned char ch : s) w += (glyphW_[ch] + 1) * scale;
    return w;
}

float Hud::text(float x, float y, const std::string& s, Color c, float scale) {
    setMode(1, font_);
    const float start = x;
    for (int pass = 0; pass < 2; ++pass) {
        // Pass 0 draws a 1-pixel dark shadow so text reads on any background.
        float px = start + (pass == 0 ? scale : 0), py = y + (pass == 0 ? scale : 0);
        Color col = pass == 0 ? Color{0, 0, 0, c.a * 0.8f} : c;
        for (unsigned char ch : s) {
            const float gw = glyphW_[ch];
            const float u0 = glyphX_[ch] / 256.f, v0 = float((ch / 16) * kCellH) / 256.f;
            const float u1 = (glyphX_[ch] + gw) / 256.f, v1 = v0 + (kCellH - 1) / 256.f;
            if (ch != ' ') quad(px, py, px + gw * scale, py + (kCellH - 1) * scale, u0, v0, u1, v1, col);
            px += (gw + 1) * scale;
        }
        x = px;
    }
    return x - start;
}

void Hud::rect(float x, float y, float w, float h, Color c) {
    setMode(0, 0);
    quad(x, y, x + w, y + h, 0, 0, 0, 0, c);
}

void Hud::line(float x0, float y0, float x1, float y1, float t, Color c) {
    setMode(0, 0);
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    Vertex a{x0 + nx, y0 + ny, 0, 0, c.r, c.g, c.b, c.a}, b{x1 + nx, y1 + ny, 0, 0, c.r, c.g, c.b, c.a};
    Vertex d{x0 - nx, y0 - ny, 0, 0, c.r, c.g, c.b, c.a}, e{x1 - nx, y1 - ny, 0, 0, c.r, c.g, c.b, c.a};
    verts_.insert(verts_.end(), {a, b, d, d, b, e});
}

void Hud::puff(float cx, float cy, float r, Color c) {
    setMode(3, 0);
    quad(cx - r, cy - r, cx + r, cy + r, 0, 0, 1, 1, c);
}

void Hud::image(GLuint texture, float x, float y, float w, float h, Color tint) {
    setMode(1, texture);
    quad(x, y, x + w, y + h, 0, 0, 1, 1, tint);
}

void Hud::imageUV(GLuint texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color tint) {
    setMode(1, texture);
    quad(x, y, x + w, y + h, u0, v0, u1, v1, tint);
}

void Hud::ring(float cx, float cy, float radius, float thickness, Color c) {
    setMode(4, 0);
    glUniform3f(locCircle_, cx, cy, radius);
    glUniform1f(locThickness_, thickness);
    const float e = radius + thickness;
    quad(cx - e, cy - e, cx + e, cy + e, 0, 0, 0, 0, c);
    flush();
}

void Hud::disc(float cx, float cy, float radius) {
    setMode(5, 0);
    glUniform3f(locCircle_, cx, cy, radius);
    quad(cx - radius, cy - radius, cx + radius, cy + radius, 0, 0, 0, 0, {0, 0, 0, 0});
    flush();
}

void Hud::scopeMask(float cx, float cy, float radius) {
    setMode(2, 0);
    glUniform3f(locCircle_, cx, cy, radius);
    quad(0, 0, float(width_), float(height_), 0, 0, 0, 0, {0, 0, 0, 1});
    flush();
}

} // namespace df
