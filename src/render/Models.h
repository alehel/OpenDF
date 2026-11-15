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
        int slot;   // the front material's signed +0x1F
    };
    struct Lod {
        GLuint vao = 0, vbo = 0; std::vector<Batch> batches; float min[3], max[3]; int triangles = 0;
        int frames = 1, ticksPerFrame = 1;
        bool animated = false;                        // see ModelLod
        std::vector<std::array<float, 3>> points;     // this LOD's hardpoints (OpenDF axes)
        std::vector<int> materialTextures;
        std::vector<ModelMaterial> materials;         // see ModelLod
        bool df1 = false;                             // a Delta Force (not v8) model
        std::vector<int> meshParent;                  // see ModelLod
        std::vector<std::array<float, 3>> meshPivot;
        std::vector<float> meshRadius;
        std::vector<std::array<float, 3>> meshCentre;   // see ModelLod (file axes)
    };
    std::string name;
    std::string kind;                  // "efct" = billboard effect
    std::string lodCodes[4];           // per-LOD draw routine ("helo", "gnrc", ...)
    float lodThreshold[4] = {0, 0, 0, 0};
    float headerRadius = 1;            // 3DI header +0x60 (LOD selection)
    float flatRadius = 0, height = 0;  // 3DI header +0x64, LOD 0 header +0x24 (blasts)
    float box[4] = {0, 0, 0, 0};       // LOD 0 header +0x28..+0x34: xmin xmax ymin ymax, file axes
    // Material +0x12 of the model's largest LOD-0 material: OpenDF hit-tests
    // objects with boxes, not polygons, so this stands in for the hit face's
    // material when choosing the impact effect.
    uint16_t impact = 0;
    std::vector<GLuint> textures;
    std::vector<GLuint> indexTextures; // raw 8-bit texels (R8, nearest)
    std::vector<bool> textureNoGrade;  // see Model3diTexture::noGrade
    std::vector<Lod> lods;
    std::vector<CollisionBox> collision;   // LOD 0 collision volumes (may be empty)
    std::vector<std::array<float, 6>> meshBoxes;   // v5 LOD 0 per mesh box (min xyz, max xyz; GOG Df.exe 0x4889c0)
    std::vector<MeshVolumes> meshVolumes;  // v8 LOD 0 plane volumes per mesh (DF2.EXE 0x440f80)
    bool hasVolumes = false;               // any v8 LOD 0 volume (0x441094: LOD +0xB8 != 0)
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
    // Delta Force: the TRN grade GOG Df.exe 0x4867a0 bakes into the model
    // palettes (unless flagged) and material colours before shading: the
    // saturation, then this table (0: none).
    GLuint gradeLut = 0;
    float gradeSaturation = 1;
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
    // `weapon`: a first-person weapon (DF.EXE 0x44abd0, DF2.EXE 0x44a223):
    // mesh 0 always, the muzzle-flash meshes 1 and 2 only when bit 0 / bit 1
    // of `flash` is set, no others; `frame` (animated materials) applies to
    // the flash meshes. Otherwise `frame` is the whole model's frame.
    void draw(const GpuModel& m, int lod, const Mat4& model, bool weapon = false, int flash = 0, int frame = 0);
    // A posed person: each mesh drawn with model x meshes[mesh]; `frame`
    // as for draw (a Delta Force person: its +0x98 variant, GOG Df.exe
    // 0x481344..0x481356).
    void drawPosed(const GpuModel& m, int lod, const Mat4& model, const std::vector<Mat4>& meshes, int frame = 0);
    void drawTranslucent(const GpuModel& m, int lod, const Mat4& model, bool weapon = false, int flash = 0, int frame = 0);
    // Delta Force's deferred faces (GOG Df.exe 0x46b1d0 queues blended faces,
    // flags & 0x700; 0x485610 draws them after the water, 0x46bf60 /
    // 0x46bf10): while on, the translucent halves of draw calls are recorded
    // (with the tint) instead of drawn; drawDeferred() draws and forgets them
    // (between begin and end).
    void setDeferTranslucent(bool on) { defer_ = on; }
    void drawDeferred();
    void end();
    // Multiplies the colour and alpha of what is drawn next; `replace`: the
    // colour instead replaces the material colour of texel-as-alpha parts
    // (DF2's vertex colour 0x445e50). begin() resets it to (1, 1, 1, 1).
    // raw: a replacing colour that skips the scene grade (DF2's bld2 fireball
    // colours, set by 0x42abc0 without 0x411f70).
    void setTint(float r, float g, float b, float a, bool replace = false, bool raw = false);
    // The mission's colour variant (cleared models reload with it).
    void setVariant(int variant);

    // DF.EXE 0x4414b8 / 0x46fd94: projected radius s = F·R / depth (persons
    // use R = 1); the first LOD whose threshold s exceeds; -1 = not drawn
    // (s <= 0.5 px or no such LOD).
    static int chooseLod(const GpuModel& m, float depth, float focal, bool person);

private:
    void drawBatches(const GpuModel& m, int lod, const Mat4& model, bool translucent, bool weapon, int flash, int frame,
                     const std::vector<Mat4>* meshes = nullptr);
    void release();
    GLuint prog_ = 0, identityLut_ = 0;
    int variant_ = 0;
    GLint locModel_ = -1, locColor_ = -1, locTextured_ = -1, locMode_ = -1, locSpec_ = -1, locSpecPow_ = -1,
          locBright_ = -1, locKey_ = -1, locGradeTexel_ = -1, locRoot_ = -1, locPose_ = -1, locPoseCount_ = -1;
    bool grade_ = false;   // begin(): the scene has a TRN grade
    bool defer_ = false;
    float tint_[4] = {1, 1, 1, 1};
    bool tintReplace_ = false, tintRaw_ = false;
    struct Deferred {
        const GpuModel* m; int lod; Mat4 model; bool weapon; int flash, frame;
        bool posed; std::vector<Mat4> meshes; float tint[4]; bool replace, raw;
    };
    std::vector<Deferred> deferred_;
    void drawTranslucentNow(const GpuModel& m, int lod, const Mat4& model, bool weapon, int flash, int frame,
                            const std::vector<Mat4>* meshes);
    static constexpr int kMaxPoses = 32;   // uPose[] in the vertex shader
    std::map<std::string, std::unique_ptr<GpuModel>> cache_;
    std::map<std::string, std::string> failed_;
};

} // namespace df
