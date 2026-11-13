// Renders a Delta Force Voxel Space terrain on the GPU.
//
// The original engine ray-cast a 1024x1024 height/colour map column by column.
// Here the same maps drive a chunked, wrap-around heightmap mesh whose heights
// are fetched in the vertex shader; the per-cell detail tiles (SNIP_*.PCX)
// that gave the original its close-up texture are applied in the fragment shader.
#pragma once

#include "../core/Pff.h"
#include "../formats/Pcx.h"
#include "../formats/Trn.h"
#include "Gl.h"
#include "Math.h"

#include <string>
#include <vector>

namespace df {

struct WorldScale {
    // One map pixel is one metre: mission coordinates (also metres, matching
    // the 1/256 m model units) land 1:1 on terrain pixels; see docs/FORMATS.md.
    float cellSize = 1.0f;
    // Metres per elevation step: DF.EXE converts elevation bytes with
    // `<< 14` into 16.16 metres, i.e. 0.25 m per step.
    float heightScale = 0.25f;
};

class Terrain {
public:
    ~Terrain();
    bool load(Vfs& vfs, const std::string& trnName, std::string* error);
    void draw(const Mat4& view, const Mat4& proj, Vec3 eye, float viewDistance);

    // World-space ground height at (x, z), bilinear, wrapping.
    float heightAt(float x, float z) const;
    float mapExtent() const { return size_ * scale.cellSize; }

    const TerrainDesc& desc() const { return desc_; }
    Vec3 fogColor() const;
    Vec3 sunDirection() const;
    GLuint colorTexture() const { return texColor_; }
    int mapSize() const { return size_; }
    WorldScale scale;
    // F / 320: the original's ring distances scale with the focal length of
    // its display mode (F = width / 2 at 90°).
    float ringScale = 1.f;

private:
    struct Mesh { GLuint vao = 0, vbo = 0, ibo = 0; GLsizei count = 0; };
    void buildMeshes();
    void release();
    Mesh makeGrid(int resolution);

    TerrainDesc desc_;
    int size_ = 0;
    std::vector<uint8_t> heights_;

    GLuint texColor_ = 0, texHeight_ = 0, texDetailIdx_ = 0;
    GLuint texDetailColor_ = 0, texDetailShade_ = 0;
    GLuint texSkyPal_ = 0, texClouds_ = 0, texWater_ = 0;
    GLuint progTerrain_ = 0, progSky_ = 0, progWater_ = 0;
    GLuint skyVao_ = 0, waterVao_ = 0, waterVbo_ = 0;
    Mesh lods_[4];
    Rgb horizon_{170, 200, 230};
    Rgb skyColors_[17];
    bool hasWater_ = false;
};

} // namespace df
