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

struct HudImage {
    GLuint texture = 0;
    int width = 0, height = 0;
};

class Hud {
public:
    ~Hud();
    bool init(Vfs& vfs, std::string* error);
    // Switches the HUD font (F_SML640.PCX, F_SML800.PCX, ...).
    bool loadFont(Vfs& vfs, const std::string& name, std::string* error);
    // Loads a game PCX as a texture; pure black becomes transparent.
    // `index0Transparent`: palette index 0 is the colour key (menu icon sheets).
    HudImage loadImage(Vfs& vfs, const std::string& name, bool blackTransparent = true, bool index0Transparent = false);
    // Delta Force 2's HUD icons (weap/pos/itm/rad%i.pcx): the palette index
    // through DF2's amber ramp, for drawing with blend 5 (additive).
    HudImage loadAmberImage(Vfs& vfs, const std::string& name);
    // A texture from RGBA pixels (nearest filtering).
    HudImage makeImage(int width, int height, const std::vector<uint8_t>& rgba);
    // DF.EXE's menu button blend (0x407e50): dst = src / 2 + dst * 3 / 4.
    void setTranslucent(bool on);
    // Menu blends: 0 normal alpha, 1 src/2 + 3/4 dst (buttons), 2 src/2 +
    // dst/2 (0x407cd8), 3 DF.EXE's line pixel (0x409964): src/2 plus half the
    // background's blue only (its red and green are dropped); 4 opaque copy; 5 saturating add.
    void setBlend(int mode);

    void begin(int width, int height);
    void end();

    // Text at integer `scale`; returns the advance in pixels.
    float text(float x, float y, const std::string& s, Color c, float scale = 1);
    float textWidth(const std::string& s, float scale = 1) const;
    float lineHeight(float scale = 1) const { return 13 * scale; }

    void rect(float x, float y, float w, float h, Color c);
    // Multiplies what is already drawn by `c` (the damage flash).
    void multiply(float x, float y, float w, float h, Color c);
    // Adds what is drawn to itself (dst × 2, saturating).
    void brighten(float x, float y, float w, float h);
    // Delta Force 2's night vision over an ellipse of half-height r and
    // half-width 2r (one pixel rows): green only, ×4 saturating.
    void nightVision(float cx, float cy, float r, float widthScale = 2.f);
    void line(float x0, float y0, float x1, float y1, float thickness, Color c);
    void puff(float cx, float cy, float radius, Color c);   // soft-edged disc
    void image(GLuint texture, float x, float y, float w, float h, Color tint = {1, 1, 1, 1});
    void imageUV(GLuint texture, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 Color tint = {1, 1, 1, 1});
    void ring(float cx, float cy, float radius, float thickness, Color c);
    // Writes only where inside the circle (used to fill the stencil buffer).
    void disc(float cx, float cy, float radius);
    // Everything outside a circle of `radius` around (cx, cy) goes black.
    void scopeMask(float cx, float cy, float radius);

    // Restricts drawing to a rectangle (pixels, top-left origin) until unclip().
    void clip(float x, float y, float w, float h);
    void unclip();

    int width() const { return width_; }
    int height() const { return height_; }

private:
    struct Vertex { float x, y, u, v, r, g, b, a; };
    void flush();
    void setMode(int mode, GLuint texture);
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c);

    GLuint prog_ = 0, vao_ = 0, vbo_ = 0, font_ = 0;
    GLint locScreen_ = -1, locMode_ = -1, locCircle_ = -1, locThickness_ = -1;
    int width_ = 0, height_ = 0;
    int mode_ = -1;
    GLuint texture_ = 0;
    std::vector<Vertex> verts_;
    std::vector<GLuint> images_;
    float glyphX_[256] = {}, glyphW_[256] = {};   // source column and width per character
};

} // namespace df
