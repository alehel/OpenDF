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
    // Delta Force 2's terrain gradient for sliding (GOG Df2.exe 0x46eaa0):
    // at the cell holding (x, z), no interpolation, the height change over
    // +-2 cells / 4 of the word grid [0x535ae8], which holds the smoothed
    // heights raised by twice the cell's detail tile's top elevation
    // (0x46e0f0; the ground 0x46e950 takes it off again). In metres a metre,
    // OpenDF axes (gz = dh/dz). 0 on Delta Force terrains.
    void df2Gradient(float x, float z, float& gx, float& gz) const;
    float mapExtent() const { return size_ * scale.cellSize; }
    // DF.EXE [0x68bfe8]: the water level in metres, 0 when the map has none
    // (0x430d28); every water test is skipped when it is 0.
    float waterLevel() const { return desc_.waterHeight * scale.heightScale; }

    const TerrainDesc& desc() const { return desc_; }
    Vec3 fogColor() const;
    // The final colour stage, for models: the display gamma/filter table and
    // saturation, and sky palette entry 16 (shade table highlight). Delta
    // Force: the df.cfg gamma table with filters 128 and no saturation step
    // (GOG Df.exe 0x402940 / 0x4027d0 -> 0x480bb0(cfg gamma, 0x80 x4));
    // Delta Force 2: the TRN's table and saturation.
    GLuint lutTexture() const { return gen2_ ? texSkyPal_ : texDisplayLut_; }
    // Delta Force: the TRN grade (saturation, then the TRN gamma/filter
    // table) that GOG Df.exe 0x4867a0 bakes into the model palettes and
    // material colours at load (0 / 1: none, as for Delta Force 2).
    GLuint gradeLutTexture() const { return gen2_ ? 0 : texSkyPal_; }
    float gradeSaturation() const { return gen2_ ? 1.f : desc_.saturation / 128.f; }
    // Surface at (x, z): 1 dirt, 2 grass, 3 snow, 4 cement, 5 sand, 6 packed
    // dirt, 0 none; each colour-map palette colour is matched to the nearest
    // TRN `type` colour (DF.EXE 0x4305b0; used for impact effects). Delta
    // Force 2: the .CAL class of the cell's detail tile (1 dirt, 2 grass,
    // 3 snow, 4 rock/concrete, 5 sand; 0 none), DF2.EXE 0x4139ae / 0x413b93.
    int surfaceAt(float x, float z) const;
    // Water, after everything else is drawn (DF.EXE 0x43bfd8); `ticks` drives the waves.
    void drawWater(const Mat4& view, const Mat4& proj, Vec3 eye, float ticks);
    float saturationFactor() const { return gen2_ ? desc_.saturation / 128.f : 1.f; }
    Vec3 highlight() const { return {skyColors_[16].r / 255.f, skyColors_[16].g / 255.f, skyColors_[16].b / 255.f}; }
    Vec3 sunDirection() const;
    GLuint colorTexture() const { return texColor_; }
    // Delta Force 2's radar picture (DF2.EXE 0x421510): the level-0 colour
    // map, nearest texel, cells at or under the water level averaged 50/50
    // with the water colour; 0 when not a DF2 terrain.
    GLuint radarTexture() const { return texRadar_; }
    int mapSize() const { return size_; }
    WorldScale scale;
    // F / 320: the original's ring distances scale with the focal length of
    // its display mode (F = width / 2 at 90°).
    float ringScale = 1.f;
    // Delta Force: the ring table's divisors (GOG Df.exe 0x489440 p1, p2):
    // 2.2 / 2.2 for the main view, 3.2 / 3.2 with the binoculars (0x404640),
    // the scope 2.5 (magnification under 5) or 5.0 / 2.5 (0x466090). Near rings end at F / (2·p1) m, far
    // ring k (0..5, 0x48dc70 draws rings 0..10) ends at 2^k·F / p2 m.
    float ringP1 = 2.2f, ringP2 = 2.2f;
    // Delta Force: df.cfg "gamma" (DISPLAY, default 128), the display table's
    // gamma. Set before load().
    int displayGamma = 128;
    // Delta Force 2: the mission's water height override (BMS flag 0x78 bit 0,
    // word +0x88, in TRN water_height units); -1 none. Set before load().
    int waterOverride = -1;
    SkyOverride skyOverride;   // Delta Force 2: the mission's sky block; set before load()
    // Delta Force 2: the mission's fog_level / fog_color (BMS flag 0x78 bits
    // 1 / 2, applied after the terrain loads, 0x45d6df); -1 none. Set before load().
    int fogLevelOverride = -1;
    long long fogColorOverride = -1;
    // Delta Force 2 fog (DF2.EXE 0x464500 getter): the distance D = 2^fog_level
    // m, 0 when fog is off (always 0 on Delta Force terrains).
    int fogLevel() const { return gen2_ && desc_.fogLevel > 0 && desc_.fogLevel < 16 ? desc_.fogLevel : 0; }
    float fogDistance() const { return fogLevel() ? float(1 << fogLevel()) : 0.f; }
    // The per-pixel fog pass of DF2.EXE's software frame (0x4485b0): every
    // pixel of the current viewport blended toward fog_color by its depth.
    // Call after the terrain, water, objects and effects, before rain/snow.
    // With `farProj` the frame was drawn in two depth passes (Pass): depth
    // below 0.5 is read with `proj` (range 0..0.5), the rest with farProj.
    void drawFog(const Mat4& proj, const Mat4* farProj = nullptr);
    // (OpenDF) Which of the caller's two depth passes draw() and drawWater()
    // are in: the far pass (view depth >= passSplit, depth range 0.5..1)
    // draws the sky and skips the chunks wholly nearer than the split; the
    // near pass skips the sky and the chunks wholly beyond it, and draws the
    // full-view under-water tints. All: one pass.
    enum class Pass { All, Far, Near };
    Pass pass = Pass::All;
    float passSplit = 0;
    // Delta Force 2's horizontal field of view in degrees (view+0x4c: 90, the
    // binoculars 22), for the sky gradient's step per row (0x4677bd).
    float df2FovDeg = 90.f;
    // Delta Force 2 water (DF2.EXE 0x4670a0/0x466b60): drawn before the
    // objects while the camera is above it, after them below it.
    bool df2Water() const { return gen2_ && hasWater_; }

private:
    struct Mesh { GLuint vao = 0, vbo = 0, ibo = 0; GLsizei count = 0; };
    void buildMeshes();
    void release();
    Mesh makeGrid(int resolution);

    TerrainDesc desc_;
    int size_ = 0;
    std::vector<float> heights_;   // elevation steps (DF2: smoothed, fractional)
    std::vector<uint8_t> detailTop_;   // DF2: per cell, its detail tile's top elevation (0x46d970 level 0)
    bool gen2_ = false;            // Delta Force 2 terrain (true-colour map)
    Rgb waterColor_{0, 0, 0};      // DF2: graded water palette entry 255 (under-water tint, radar)
    bool alphaDetail_ = false;   // Delta Force 2 detail tiles: blended by their alpha

    GLuint texColor_ = 0, texHeight_ = 0, texDetailIdx_ = 0, texRadar_ = 0;
    std::vector<uint8_t> colorRgb_;   // DF2: the colour map as drawn (for the radar)
    GLuint texDetailColor_ = 0, texDetailShade_ = 0;
    GLuint texDisplayLut_ = 0;   // Delta Force: the df.cfg gamma table (filters 128)
    GLuint texSkyPal_ = 0, texClouds_ = 0, texWater_ = 0, texWaterGrad_ = 0, texScene_ = 0;
    int sceneW_ = 0, sceneH_ = 0;
    GLuint progTerrain_ = 0, progSky_ = 0, progWater_ = 0, progFog_ = 0;
    GLuint texSkyGrad_ = 0, texDepth_ = 0;   // DF2: the 256-step sky gradient; the depth copy for the fog pass
    int depthW_ = 0, depthH_ = 0;
    GLuint skyVao_ = 0, waterVao_ = 0, waterVbo_ = 0, instVbo_ = 0;
    std::vector<float> instData_;   // the chunk origins of the last draw
    Mesh lods_[5];
    Rgb horizon_{170, 200, 230};
    Rgb skyColors_[17];
    std::vector<uint8_t> surface_;   // per colour-map pixel
    bool hasWater_ = false;
};

} // namespace df
