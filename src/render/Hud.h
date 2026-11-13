// 2D overlay: text in the game's own font (F_SML640.PCX), filled shapes,
// textured images and the sniper-scope mask. Coordinates are in pixels
// with (0,0) at the top left.
#pragma once

#include "../core/Pff.h"
#include "Gl.h"
#include "Math.h"

#include <string>
#include <vector>

namespace df {

struct Color { float r, g, b, a; };

class Hud {
public:
    ~Hud();
    bool init(Vfs& vfs, std::string* error);

    void begin(int width, int height);
    void end();

    // Text at integer `scale`; returns the advance in pixels.
    float text(float x, float y, const std::string& s, Color c, float scale = 1);
    float textWidth(const std::string& s, float scale = 1) const;
    float lineHeight(float scale = 1) const { return 13 * scale; }

    void rect(float x, float y, float w, float h, Color c);
    void line(float x0, float y0, float x1, float y1, float thickness, Color c);
    void puff(float cx, float cy, float radius, Color c);   // soft-edged disc
    void image(GLuint texture, float x, float y, float w, float h, Color tint = {1, 1, 1, 1});
    // Everything outside a circle of `radius` around (cx, cy) goes black.
    void scopeMask(float cx, float cy, float radius);

    int width() const { return width_; }
    int height() const { return height_; }

private:
    struct Vertex { float x, y, u, v, r, g, b, a; };
    void flush();
    void setMode(int mode, GLuint texture);
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c);

    GLuint prog_ = 0, vao_ = 0, vbo_ = 0, font_ = 0;
    GLint locScreen_ = -1, locMode_ = -1, locCircle_ = -1;
    int width_ = 0, height_ = 0;
    int mode_ = -1;
    GLuint texture_ = 0;
    std::vector<Vertex> verts_;
    float glyphX_[256] = {}, glyphW_[256] = {};   // source column and width per character
};

} // namespace df
