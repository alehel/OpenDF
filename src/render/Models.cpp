#include "Models.h"

#include <algorithm>
#include <cmath>

namespace df {

namespace {

const char* kModelVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uViewProj, uModel;
uniform vec3 uLight, uViewer;
uniform float uSpec, uBright;
uniform int uSpecPow;
out vec2 vUv;
out vec2 vLit;   // shade for the front face, the back face
float shade(vec3 n) {
    vec3 h = normalize(uLight + uViewer);
    float d = dot(n, uLight);
    if (d < 0.0) d = -d / 4.0;                 // back light wrapped at 25 %
    float lit = uBright >= 0.0 ? uBright : 0.55 + 0.5 * d;
    if (uSpec != 0.0) {
        float s = dot(n, h);
        if (s > 0.0) {
            for (int i = 0; i < uSpecPow && i < 8; ++i) s *= s;
            lit += s * uSpec;
        }
    }
    return clamp(lit * 32.0, 1.0, 60.0);       // shade level
}
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vec3 n = normalize(mat3(uModel) * aNormal);
    vLit = vec2(shade(n), shade(-n));
    vUv = aUv;
    gl_Position = uViewProj * w;
}
)";

const char* kModelFS = R"(#version 330 core
in vec2 vUv;
in vec2 vLit;
uniform sampler2D uTex, uIndex, uLut;
uniform bool uTextured, uKey;
uniform vec3 uColor, uHighlight;
uniform float uSat;
uniform int uMode;   // 0 opaque/keyed, 1 50 % average, 2 texel-as-alpha
out vec4 oColor;
vec3 dec(vec3 c) { return pow(max(c, 0.0), vec3(1.25)); }
vec3 enc(vec3 c) { return pow(max(c, 0.0), vec3(0.8)); }
vec3 post(vec3 c) {
    float y = (c.r + 2.0 * c.g + c.b) * 0.25;
    c = clamp(y + (c - y) * uSat, 0.0, 1.0);
    float k = 255.0 / 256.0, o = 0.5 / 256.0;
    return vec3(texture(uLut, vec2(c.r * k + o, 0.5)).r, texture(uLut, vec2(c.g * k + o, 0.5)).g,
                texture(uLut, vec2(c.b * k + o, 0.5)).b);
}
void main() {
    float idx = uTextured ? texture(uIndex, vUv).r * 255.0 : 1.0;
    if (uMode == 2) {               // 0x8000: dst += (C - dst) * texel / 256
        oColor = vec4(post(uColor), idx / 256.0);
        return;
    }
    vec4 c = uTextured ? texture(uTex, vUv) : vec4(uColor, 1.0);
    if (uTextured && (uMode == 1 || uKey) && idx < 0.5) discard;   // texel 0 skipped
    if (c.a < 0.5) discard;
    if (uMode == 1) {               // 0x0080: unlit (shade row 32), averaged 50/50
        oColor = vec4(post(c.rgb), 0.5);
        return;
    }
    // Shade table (0x435070 / 0x434d84): rows 0..31 black -> colour, rows
    // 32..63 colour -> sky colour 16; mixed in the ^1.25 space.
    float level = floor(gl_FrontFacing ? vLit.x : vLit.y);
    vec3 lin = dec(c.rgb);
    lin = level <= 31.0 ? lin * (level / 31.0) : lin + (dec(uHighlight) - lin) * ((level - 32.0) / 31.0);
    oColor = vec4(post(enc(lin)), 1.0);
}
)";

GLuint uploadTexture(const Model3diTexture& t) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Transparent texels take a neighbour's colour so filtering doesn't
    // produce dark fringes around foliage and fences.
    std::vector<uint8_t> px = t.rgba;
    if (t.indexZeroTransparent) {
        const int w = t.width, h = t.height;
        for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    uint8_t* p = &px[(size_t(y) * w + x) * 4];
                    if (p[3]) continue;
                    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                    for (int k = 0; k < 4; ++k) {
                        int nx = (x + dx[k] + w) % w, ny = (y + dy[k] + h) % h;
                        const uint8_t* q = &t.rgba[(size_t(ny) * w + nx) * 4];
                        if (q[3]) { p[0] = q[0]; p[1] = q[1]; p[2] = q[2]; break; }
                    }
                }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    return id;
}

GLuint uploadIndices(const Model3diTexture& t) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, t.width, t.height, 0, GL_RED, GL_UNSIGNED_BYTE, t.indices.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    return id;
}

void setV3(GLuint p, const char* n, Vec3 v) { glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z); }

} // namespace

void ModelRenderer::release() {
    for (auto& [name, m] : cache_) {
        for (GLuint t : m->textures) glDeleteTextures(1, &t);
        for (GLuint t : m->indexTextures) glDeleteTextures(1, &t);
        for (auto& l : m->lods) {
            glDeleteBuffers(1, &l.vbo);
            glDeleteVertexArrays(1, &l.vao);
        }
    }
    cache_.clear();
    failed_.clear();
}

ModelRenderer::~ModelRenderer() {
    release();
    if (identityLut_) glDeleteTextures(1, &identityLut_);
    if (prog_) glDeleteProgram(prog_);
}

void ModelRenderer::setVariant(int variant) {
    if (variant == variant_) return;
    variant_ = variant;
    release();   // models reload with the new colours (pointers held elsewhere must be refreshed)
}

bool ModelRenderer::init(std::string* error) {
    prog_ = compileProgram(kModelVS, kModelFS);
    if (!prog_) { if (error) *error = "model shader failed"; return false; }
    locModel_ = glGetUniformLocation(prog_, "uModel");
    locColor_ = glGetUniformLocation(prog_, "uColor");
    locTextured_ = glGetUniformLocation(prog_, "uTextured");
    locMode_ = glGetUniformLocation(prog_, "uMode");
    locSpec_ = glGetUniformLocation(prog_, "uSpec");
    locSpecPow_ = glGetUniformLocation(prog_, "uSpecPow");
    locBright_ = glGetUniformLocation(prog_, "uBright");
    locKey_ = glGetUniformLocation(prog_, "uKey");
    glUseProgram(prog_);
    glUniform1i(glGetUniformLocation(prog_, "uTex"), 0);
    glUniform1i(glGetUniformLocation(prog_, "uIndex"), 1);
    glUniform1i(glGetUniformLocation(prog_, "uLut"), 2);
    {
        std::vector<uint8_t> lut(256 * 3);
        for (int i = 0; i < 256; ++i) lut[size_t(i) * 3] = lut[size_t(i) * 3 + 1] = lut[size_t(i) * 3 + 2] = uint8_t(i);
        glGenTextures(1, &identityLut_);
        glBindTexture(GL_TEXTURE_2D, identityLut_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, lut.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    }
    glUseProgram(0);
    return true;
}

const GpuModel* ModelRenderer::get(Vfs& vfs, const std::string& rawName, std::string* error) {
    std::string name = toUpper(rawName);
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".3DI") != 0) name += ".3DI";
    if (auto it = cache_.find(name); it != cache_.end()) return it->second.get();
    if (auto it = failed_.find(name); it != failed_.end()) { if (error) *error = it->second; return nullptr; }

    std::vector<uint8_t> data;
    Model3di src;
    std::string err;
    if (!vfs.read(name, data)) err = "not found";
    else if (!loadModel3di(data, src, &err, variant_)) {}
    else if (src.lods.empty()) err = "no geometry";
    if (!err.empty()) {
        failed_[name] = err;
        if (error) *error = err;
        return nullptr;
    }

    auto g = std::make_unique<GpuModel>();
    g->name = name.substr(0, name.size() - 4);
    g->kind = src.kind;
    for (int k = 0; k < 4; ++k) g->lodCodes[k] = src.lodCodes[k];
    if (!src.lods.empty()) g->hitMeshes = src.lods[0].hitMeshes;
    std::copy(src.lodThreshold, src.lodThreshold + 4, g->lodThreshold);
    g->headerRadius = src.radius > 0 ? src.radius : 1.f;
    for (auto& t : src.textures) {
        g->textures.push_back(uploadTexture(t));
        g->indexTextures.push_back(uploadIndices(t));
    }
    for (auto& l : src.lods) {
        GpuModel::Lod gl;
        std::vector<ModelVertex> all;
        for (auto& b : l.batches) {
            GpuModel::Batch gb{GLint(all.size()), GLsizei(b.vertices.size()), b.texture, {b.color[0], b.color[1], b.color[2]},
                               b.blend, b.flags, b.material, b.mesh, b.specular, b.specularPower, b.fullbright, b.doubleSided};
            all.insert(all.end(), b.vertices.begin(), b.vertices.end());
            gl.batches.push_back(gb);
        }
        std::copy(l.min, l.min + 3, gl.min);
        std::copy(l.max, l.max + 3, gl.max);
        gl.triangles = l.triangleCount;
        gl.frames = l.frames;
        gl.materialTextures = l.materialTextures;
        gl.meshParent = l.meshParent;
        gl.meshPivot = l.meshPivot;
        gl.ticksPerFrame = l.ticksPerFrame;
        glGenVertexArrays(1, &gl.vao);
        glBindVertexArray(gl.vao);
        glGenBuffers(1, &gl.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, gl.vbo);
        glBufferData(GL_ARRAY_BUFFER, std::ptrdiff_t(all.size() * sizeof(ModelVertex)), all.data(), GL_STATIC_DRAW);
        const GLsizei stride = sizeof(ModelVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ModelVertex, px)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ModelVertex, nx)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ModelVertex, u)));
        glBindVertexArray(0);
        g->lods.push_back(std::move(gl));
    }
    const auto& l0 = g->lods[0];
    float r2 = 0;
    for (int a = 0; a < 3; ++a) {
        float e = std::max(std::fabs(l0.min[a]), std::fabs(l0.max[a]));
        r2 += e * e;
    }
    g->radius = std::max(std::sqrt(r2), 0.1f);
    g->collision = src.lods[0].collision;
    g->points = src.lods[0].points;
    {
        size_t most = 0;
        for (const auto& b : src.lods[0].batches)
            if (b.vertices.size() > most) { most = b.vertices.size(); g->impact = b.impact; }
    }
    const GpuModel* out = g.get();
    cache_[name] = std::move(g);
    return out;
}

int ModelRenderer::chooseLod(const GpuModel& m, float depth, float focal, bool person) {
    const float r = person ? 1.f : m.headerRadius;
    const float s = depth < r ? 1024.f : focal * r / std::max(depth, 1e-3f);
    if (s <= 0.5f) return -1;
    for (int k = 0; k < 4; ++k)
        if (s > m.lodThreshold[k]) return k < int(m.lods.size()) ? k : -1;
    return -1;
}

void ModelRenderer::begin(const Mat4& viewProj, const SceneLighting& light) {
    glUseProgram(prog_);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "uViewProj"), 1, GL_FALSE, viewProj.m);
    setV3(prog_, "uLight", normalize(light.light));
    setV3(prog_, "uViewer", normalize(light.viewer));
    setV3(prog_, "uHighlight", light.highlight);
    glUniform1f(glGetUniformLocation(prog_, "uSat"), light.saturation);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, light.lut ? light.lut : identityLut_);
    glActiveTexture(GL_TEXTURE0);
}

void ModelRenderer::drawBatches(const GpuModel& m, int lod, const Mat4& model, bool translucent, bool weapon, bool flash,
                                int frame, const std::vector<Mat4>* meshes) {
    if (m.lods.empty() || lod < 0) return;
    const auto& l = m.lods[std::clamp(lod, 0, int(m.lods.size()) - 1)];
    glUniformMatrix4fv(locModel_, 1, GL_FALSE, model.m);
    glBindVertexArray(l.vao);
    for (const auto& b : l.batches) {
        if ((b.blend != 0) != translucent) continue;
        if (meshes) {
            const Mat4 mm = b.mesh >= 0 && b.mesh < int(meshes->size()) ? model * (*meshes)[size_t(b.mesh)] : model;
            glUniformMatrix4fv(locModel_, 1, GL_FALSE, mm.m);
        }
        // First-person weapons: the fullbright muzzle-flash parts only while firing.
        if (weapon && b.fullbright >= 0 && !flash) continue;
        if (b.doubleSided) glDisable(GL_CULL_FACE);
        else glEnable(GL_CULL_FACE);
        if (b.flags & 0x0040) {   // decal: drawn at the nearest vertex's depth (0x46f9bc)
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.f, -2.f);
        }
        glUniform1i(locMode_, b.blend);
        glUniform1i(locKey_, (b.flags & 0x0200) != 0);
        glUniform1f(locSpec_, b.specular);
        glUniform1i(locSpecPow_, b.specularPower);
        glUniform1f(locBright_, b.fullbright);
        // Animated materials (flag 0x4000): material base + frame is used
        // instead (DF.EXE 0x46f72f); here, its texture.
        int texture = b.texture;
        if ((b.flags & 0x4000) && frame > 0 && b.material + frame < int(l.materialTextures.size()))
            texture = l.materialTextures[size_t(b.material + frame)];
        const bool textured = texture >= 0 && texture < int(m.textures.size());
        glUniform1i(locTextured_, textured);
        glUniform3f(locColor_, b.color[0], b.color[1], b.color[2]);
        if (textured) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m.indexTextures[size_t(texture)]);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m.textures[size_t(texture)]);
        }
        glDrawArrays(GL_TRIANGLES, b.first, b.count);
        if (b.flags & 0x0040) glDisable(GL_POLYGON_OFFSET_FILL);
    }
    glDisable(GL_CULL_FACE);
}

void ModelRenderer::draw(const GpuModel& m, int lod, const Mat4& model, bool weapon, bool flash, int frame) {
    drawBatches(m, lod, model, false, weapon, flash, frame);
}

void ModelRenderer::drawPosed(const GpuModel& m, int lod, const Mat4& model, const std::vector<Mat4>& meshes) {
    drawBatches(m, lod, model, false, false, false, 0, &meshes);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    drawBatches(m, lod, model, true, false, false, 0, &meshes);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void ModelRenderer::drawTranslucent(const GpuModel& m, int lod, const Mat4& model, bool weapon, bool flash, int frame) {
    // Translucent faces test depth but do not write it, in submission order.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.f, -4.f);
    drawBatches(m, lod, model, true, weapon, flash, frame);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void ModelRenderer::end() {
    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace df
