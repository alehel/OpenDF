// .3DI models: every object, vehicle, soldier and weapon in Delta Force.
// A file holds its own textures (8-bit + RGBA palette each) followed by
// several levels of detail (LOD 0 = most detailed).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace df {

struct Model3diTexture {
    std::string name;
    int width = 0, height = 0;
    uint8_t sequence = 0;            // what materials refer to
    bool indexZeroTransparent = false;
    bool alpha16 = false;            // v8: 16-bit texels (index, alpha)
    // Header +0x16 flag 0x10 (v5): the palette is left out of the TRN grade
    // (GOG Df.exe 0x4867a0 skips textures flagged 0x10 or 0x80).
    bool noGrade = false;
    std::vector<uint8_t> rgba;       // width*height*4, decoded
    std::vector<uint8_t> indices;    // the raw 8-bit texels (used as alpha by 0x8000 materials)
};

// One render-ready vertex, already converted to OpenDF world space:
// metres, +Y up (file: 1/256 m units, +Z up, +X forward, +Y left).
struct ModelVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// Material flags (DF.EXE renderer, 0x46f9e3): 0x0001 textured, 0x0002
// double-sided, 0x0008 background re-shade (not drawn in hi-colour), 0x0020
// fullbright, 0x0040 decal depth, 0x0080 50 % translucent, 0x0200 colour key,
// 0x0800 hidden, 0x4000 animated (material + frame), 0x8000 texel-as-alpha
// blend toward the material colour.
struct ModelBatch {
    int texture = -1;                // index into Model3di::textures, -1 = flat colour
    float color[3] = {0.7f, 0.7f, 0.7f};   // the mission's colour variant
    // 0 = opaque (colour key = texel 0 skipped), 1 = 50 % average (0x0080),
    // 2 = texel-as-alpha toward `color` (0x8000).
    int blend = 0;
    uint32_t flags = 0;
    int material = 0;                // index in the LOD's material list
    int mesh = 0;                    // mesh index (weapon models: 1-2 are the muzzle flash)
    float specular = 0;              // +0x38, 16.16
    int specularPower = 0;           // +0x40: s is squared this many times
    float fullbright = -1;           // +0x3c when flag 0x0020, else -1
    uint16_t impact = 0;             // +0x12: surface for impact effects (metal, wood, stone, ...)
    bool doubleSided = false;
    std::vector<ModelVertex> vertices;   // triangle list
};

// Axis-aligned box in model space (already in OpenDF axes, metres).
// A face as DF.EXE's round test reads it (0x44e918), file axes, metres:
// vertices, plane (normal word +0x02, D at +0x28: n.v + D, front side > 0),
// the file's face box (+0x2C, xmin xmax ymin ymax zmin zmax), the material
// flags dword (+0x10: 0x2 two-sided, 0x01000000 rounds pass, 0x10000..
// 0x08000000 impact class) and the projection axis (normal word +0x06: 1 Z,
// 2 Y, 4 X dominant).
struct HitFace {
    float v[3][3];
    float n[3];
    float d;
    float box[6];
    uint32_t material;
    uint16_t axis;
};

struct CollisionBox {
    float min[3], max[3];
    // Volume type (+0x00): 1 box, 3 upright cylinder (centre +0x08..+0x10,
    // radius +0x18); others are ignored by DF.EXE's mover test (0x437a28).
    int type = 1;
    float cx = 0, cz = 0, r = 0;   // cylinder centre (OpenDF x, z) and radius
};

// Delta Force 2 (v8) collision volume as DF2.EXE's push-out 0x440f80 reads
// it, file axes (x forward, y left, z up), metres: the type (+0x00: 1 box or
// hull, 3 cylinder, 4 ladder, 6 armory area) and its planes (+0x48 count,
// +0x4C pointer by running total, 8 bytes each: i16 nx, ny, nz / 16384, i16
// d / 256 m); a point is inside when n.p + d < 0 for every plane.
struct VolumePlane { float n[3]; float d; };
struct PlaneVolume { int type = 1; std::vector<VolumePlane> planes; };
// Per mesh: its box (+0x40..+0x54: xmin xmax ymin ymax zmin zmax, 16.16 m)
// and the volumes it owns (+0x1C count, +0x20 pointer by running total).
struct MeshVolumes { float box[6] = {0, 0, 0, 0, 0, 0}; std::vector<PlaneVolume> volumes; };

struct ModelLod {
    std::vector<ModelBatch> batches;
    int frames = 1, ticksPerFrame = 1;      // LOD header +0x40 / +0x58 (effects)
    // LOD header +0x5a (the high word of +0x58) is not 0: the gnrc draw
    // (GOG Df.exe 0x481060) animates the LOD's 0x4000 materials.
    bool animated = false;
    std::vector<int> materialTextures;      // per material: texture index (-1 none), for animated materials
    std::vector<CollisionBox> collision;   // from the file's collision volumes (v5)
    std::vector<MeshVolumes> meshVolumes;  // v8 LOD 0: per mesh, the plane volumes (DF2.EXE 0x440f80)
    std::vector<std::vector<HitFace>> hitMeshes;   // LOD 0 only: every face per mesh (hidden ones too)
    // Hardpoints (LOD header +0xA0, 12 bytes: i32 x, y, z in 16.16 m),
    // OpenDF axes, metres: DF.EXE's model +0xa4 point list (the
    // first-person muzzle is point +0x90 of the player table, 0x44b4a0).
    std::vector<std::array<float, 3>> points;
    // Per mesh: parent mesh (+0x24) and pivot (+0x34, 16.16, absolute,
    // OpenDF axes). People have one mesh per .CHR part, posed about these.
    std::vector<int> meshParent;
    std::vector<std::array<float, 3>> meshPivot;
    // Per mesh +0x64 (16.16 m, v8 meshes of 0x70 bytes): Delta Force 2's
    // hit-sphere radius about the posed pivot (DF2.EXE 0x44b2f0); 0 if absent.
    std::vector<float> meshRadius;
    // Per mesh +0x40..+0x54 (v8 0x70-byte meshes): the box xmin xmax ymin
    // ymax zmin zmax, 16.16 m, file axes, model frame; DF2's bld2 fireballs
    // start at its centre ((min + max) >> 1, 0x402eaa..0x402ef4).
    std::vector<std::array<float, 3>> meshCentre;
    float min[3] = {0, 0, 0}, max[3] = {0, 0, 0};
    int triangleCount = 0;
};

struct Model3di {
    std::string name;
    int version = 0;
    std::string kind;                // header +0x28: "efct" billboard effect, "ef3d", ...
    // Header +0x28..+0x34: the draw routine per LOD slot as a FourCC stored
    // byte-reversed ("oleh" = "helo"), looked up in DF.EXE's table 0x4E53DC.
    std::string lodCodes[4];
    float lodThreshold[4] = {0, 0, 0, 0};   // header +0x18..+0x24: projected-radius pixels
    float radius = 1;                // header +0x60, metres
    float flatRadius = 0;            // header +0x64: LOD 0's largest horizontal vertex radius (blasts, collision reach)
    float height = 0;                // LOD 0 header +0x24: zmax - zmin (model +0x68 at load, 0x43f383)
    // LOD 0 header +0x28..+0x34: xmin, xmax, ymin, ymax in metres, file axes
    // (x forward, y left); DF2's bld2 footprint (0x402979).
    float box[4] = {0, 0, 0, 0};
    std::vector<Model3diTexture> textures;
    std::vector<ModelLod> lods;
};

// `variant` picks which of a material's four colours/textures to use (the
// mission header +0x74, DF.EXE 0x43f964).
bool loadModel3di(const std::vector<uint8_t>& file, Model3di& out, std::string* error, int variant = 0);

} // namespace df
