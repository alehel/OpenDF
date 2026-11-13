// GPU upload and drawing of .3DI models, with a by-name cache.
#pragma once

#include "../core/Pff.h"
#include "../formats/Model3di.h"
#include "Gl.h"
#include "Math.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace df {

struct GpuModel {
    struct Batch { GLint first; GLsizei count; int texture; float color[3]; int blend; };
    struct Lod { GLuint vao = 0, vbo = 0; std::vector<Batch> batches; float min[3], max[3]; int triangles = 0; };
    std::string name;
    std::vector<GLuint> textures;
    std::vector<Lod> lods;
    float radius = 1.f;  // of LOD 0, around the origin
};

struct SceneLighting {
    Vec3 eye, fog, sun{0.4f, 0.8f, 0.45f};
    float fogStart = 1e6f, fogEnd = 2e6f;
};

class ModelRenderer {
public:
    ~ModelRenderer();
    bool init(std::string* error);

    // Loads (once) NAME.3DI from the VFS. Returns nullptr if missing/unparseable.
    const GpuModel* get(Vfs& vfs, const std::string& name, std::string* error = nullptr);

    void begin(const Mat4& viewProj, const SceneLighting& light);
    // Opaque and alpha-tested parts; call drawTranslucent afterwards for blended ones.
    void draw(const GpuModel& m, int lod, const Mat4& model);
    void drawTranslucent(const GpuModel& m, int lod, const Mat4& model);
    void end();

    // Picks a LOD from the camera distance and on-screen size.
    static int chooseLod(const GpuModel& m, float distance);

private:
    void drawBatches(const GpuModel& m, int lod, const Mat4& model, bool translucent);
    GLuint prog_ = 0;
    GLint locModel_ = -1, locColor_ = -1, locTextured_ = -1, locAlpha_ = -1, locMode_ = -1;
    std::map<std::string, std::unique_ptr<GpuModel>> cache_;
    std::map<std::string, std::string> failed_;
};

} // namespace df
