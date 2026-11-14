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
    struct Batch {
        GLint first; GLsizei count; int texture; float color[3]; int blend;
        uint32_t flags; int material, mesh; float specular; int specularPower; float fullbright; bool doubleSided;
    };
    struct Lod {
        GLuint vao = 0, vbo = 0; std::vector<Batch> batches; float min[3], max[3]; int triangles = 0;
        int frames = 1, ticksPerFrame = 1;
        std::vector<int> materialTextures;
        std::vector<int> meshParent;                  // see ModelLod
        std::vector<std::array<float, 3>> meshPivot;
        std::vector<float> meshRadius;
    };
    std::string name;
    std::string kind;                  // "efct" = billboard effect
    std::string lodCodes[4];           // per-LOD draw routine ("helo", "gnrc", ...)
    float lodThreshold[4] = {0, 0, 0, 0};
    float headerRadius = 1;            // 3DI header +0x60 (LOD selection)
    float flatRadius = 0, height = 0;  // 3DI header +0x64, LOD 0 header +0x24 (blasts)
    // Material +0x12 of the model's largest LOD-0 material: OpenDF hit-tests
    // objects with boxes, not polygons, so this stands in for the hit face's
    // material when choosing the impact effect.
    uint16_t impact = 0;
    std::vector<GLuint> textures;
    std::vector<GLuint> indexTextures; // raw 8-bit texels (R8, nearest)
    std::vector<Lod> lods;
    std::vector<CollisionBox> collision;   // LOD 0 collision volumes (may be empty)
    std::vector<std::vector<HitFace>> hitMeshes;   // LOD 0 faces per mesh, for rounds (0x44e918)
    std::vector<std::array<float, 3>> points;   // LOD 0 hardpoints (muzzle points)
    float radius = 1.f;  // of LOD 0, around the origin
};

// DF.EXE lighting (0x44bea4 / 0x46f488): one directional light, ambient
// 0.55, diffuse 0.5, back light 25 %, Blinn specular per material; shade
// levels 1..60 of the palette shade table; no fog on objects.
struct SceneLighting {
    Vec3 light{-0.70710678f, 0.70710678f, 0};   // toward the light, in the space models are drawn in
    Vec3 viewer{0, 0, 1};                        // toward the viewer (DF uses the view axis)
    GLuint lut = 0;                              // terrain gamma/filter table (256x1 RGB)
    float saturation = 1;
    Vec3 highlight{1, 1, 1};                     // sky palette entry 16 (shade rows 32..63)
};

class ModelRenderer {
public:
    ~ModelRenderer();
    bool init(std::string* error);

    // Loads (once) NAME.3DI from the VFS. Returns nullptr if missing/unparseable.
    const GpuModel* get(Vfs& vfs, const std::string& name, std::string* error = nullptr);

    void begin(const Mat4& viewProj, const SceneLighting& light);
    // Opaque and colour-keyed parts; call drawTranslucent afterwards for blended ones.
    // `weapon`: a first-person weapon, whose fullbright muzzle-flash parts are
    // drawn only while `flash` is set. `frame`: animated materials' frame.
    void draw(const GpuModel& m, int lod, const Mat4& model, bool weapon = false, bool flash = false, int frame = 0);
    // A posed person: each mesh drawn with model x meshes[mesh].
    void drawPosed(const GpuModel& m, int lod, const Mat4& model, const std::vector<Mat4>& meshes);
    void drawTranslucent(const GpuModel& m, int lod, const Mat4& model, bool weapon = false, bool flash = false, int frame = 0);
    void end();
    // The mission's colour variant (cleared models reload with it).
    void setVariant(int variant);

    // DF.EXE 0x4414b8 / 0x46fd94: projected radius s = F·R / depth (persons
    // use R = 1); the first LOD whose threshold s exceeds; -1 = not drawn
    // (s <= 0.5 px or no such LOD).
    static int chooseLod(const GpuModel& m, float depth, float focal, bool person);

private:
    void drawBatches(const GpuModel& m, int lod, const Mat4& model, bool translucent, bool weapon, bool flash, int frame,
                     const std::vector<Mat4>* meshes = nullptr);
    void release();
    GLuint prog_ = 0, identityLut_ = 0;
    int variant_ = 0;
    GLint locModel_ = -1, locColor_ = -1, locTextured_ = -1, locMode_ = -1, locSpec_ = -1, locSpecPow_ = -1,
          locBright_ = -1, locKey_ = -1;
    std::map<std::string, std::unique_ptr<GpuModel>> cache_;
    std::map<std::string, std::string> failed_;
};

} // namespace df
