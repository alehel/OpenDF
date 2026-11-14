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
    // The nearest cell's raw height, no interpolation (DF.EXE's byte map
    // 0x68c024, e.g. for sound occlusion).
    float cellHeight(float x, float z) const;
    float mapExtent() const { return size_ * scale.cellSize; }
    // DF.EXE [0x68bfe8]: the water level in metres, 0 when the map has none
    // (0x430d28); every water test is skipped when it is 0.
    float waterLevel() const { return desc_.waterHeight * scale.heightScale; }

    const TerrainDesc& desc() const { return desc_; }
    Vec3 fogColor() const;
    // The TRN's colour finish, for models: gamma/filter table, saturation,
    // and sky palette entry 16 (shade table highlight).
    GLuint lutTexture() const { return texSkyPal_; }
    // Surface at (x, z): 1 dirt, 2 grass, 3 snow, 4 cement, 5 sand, 6 packed
    // dirt, 0 none; each colour-map palette colour is matched to the nearest
    // TRN `type` colour (DF.EXE 0x4305b0; used for impact effects).
    int surfaceAt(float x, float z) const;
    // Water, after everything else is drawn (DF.EXE 0x43bfd8); `ticks` drives the waves.
    void drawWater(const Mat4& view, const Mat4& proj, Vec3 eye, float ticks);
    float saturationFactor() const { return desc_.saturation / 128.f; }
    Vec3 highlight() const { return {skyColors_[16].r / 255.f, skyColors_[16].g / 255.f, skyColors_[16].b / 255.f}; }
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
    GLuint texSkyPal_ = 0, texClouds_ = 0, texWater_ = 0, texWaterGrad_ = 0, texScene_ = 0;
    int sceneW_ = 0, sceneH_ = 0;
    GLuint progTerrain_ = 0, progSky_ = 0, progWater_ = 0;
    GLuint skyVao_ = 0, waterVao_ = 0, waterVbo_ = 0;
    Mesh lods_[5];
    Rgb horizon_{170, 200, 230};
    Rgb skyColors_[17];
    std::vector<uint8_t> surface_;   // per colour-map pixel
    bool hasWater_ = false;
};

} // namespace df
