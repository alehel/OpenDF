// Delta Force 2's bullet tracers on screen (GOG Df2.exe 0x452610, called by
// the frame 0x44f8d0 / 0x44fa80 after the scene): each live record of the
// round tracer ring as a line (0x46a0d0 mode 0x210) in its colour, alpha
// ((life >> 2) << 3) / 255. The records themselves are the Arsenal's.
#pragma once

#include "../render/Gl.h"
#include "../render/Math.h"
#include "Weapons.h"

#include <vector>

namespace df {

class TracerLines {
public:
    ~TracerLines();
    // `wrap` maps a point to its copy nearest the camera on the wrapping map.
    template <class Wrap> void draw(const Mat4& viewProj, const std::vector<Arsenal::Tracer>& tracers, Wrap&& wrap) {
        verts_.clear();
        for (const auto& t : tracers) {
            if ((t.life >> 2) < 1) continue;
            const Vec3 a = wrap(t.a), b = a + (t.b - t.a);
            const float r = float((t.colour >> 16) & 0xff) / 255.f, g = float((t.colour >> 8) & 0xff) / 255.f,
                        bl = float(t.colour & 0xff) / 255.f, al = float((t.life >> 2) << 3) / 255.f;
            verts_.insert(verts_.end(), {a.x, a.y, a.z, r, g, bl, al, b.x, b.y, b.z, r, g, bl, al});
        }
        flush(viewProj);
    }

private:
    void flush(const Mat4& viewProj);
    std::vector<float> verts_;
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
};

} // namespace df
