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
    std::vector<uint8_t> rgba;       // width*height*4, decoded
};

// One render-ready vertex, already converted to OpenDF world space:
// metres, +Y up (file: 1/256 m units, +Z up, +X forward, +Y left).
struct ModelVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

struct ModelBatch {
    int texture = -1;                // index into Model3di::textures, -1 = flat colour
    float color[3] = {0.7f, 0.7f, 0.7f};
    // 0 = opaque (alpha-tested), 1 = 50% translucent, 2 = multiply/darken
    // (ground shadows, rotor blur discs).
    int blend = 0;
    std::vector<ModelVertex> vertices;   // triangle list
};

struct ModelLod {
    std::vector<ModelBatch> batches;
    float min[3] = {0, 0, 0}, max[3] = {0, 0, 0};
    int triangleCount = 0;
};

struct Model3di {
    std::string name;
    int version = 0;
    std::vector<Model3diTexture> textures;
    std::vector<ModelLod> lods;
};

bool loadModel3di(const std::vector<uint8_t>& file, Model3di& out, std::string* error);

} // namespace df
