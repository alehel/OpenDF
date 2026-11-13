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
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUv;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = mat3(uModel) * aNormal;
    vUv = aUv;
    gl_Position = uViewProj * w;
}
)";

const char* kModelFS = R"(#version 330 core
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUv;
uniform sampler2D uTex;
uniform bool uTextured;
uniform vec3 uColor, uEye, uFog, uSun;
uniform float uAlpha, uFogStart, uFogEnd;
uniform int uMode;   // 0 opaque, 1 translucent, 2 multiply
out vec4 oColor;
void main() {
    vec4 c = uTextured ? texture(uTex, vUv) : vec4(uColor, 1.0);
    if (c.a < 0.5) discard;
    float fogF = smoothstep(uFogStart, uFogEnd, length(vWorld - uEye));
    if (uMode == 2) {               // darkens what is behind; fades out with distance
        oColor = vec4(mix(c.rgb, vec3(1.0), fogF), 1.0);
        return;
    }
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    float light = 0.55 + 0.55 * max(dot(n, uSun), 0.0);
    vec3 col = c.rgb * light;
    oColor = vec4(mix(col, uFog, fogF), uAlpha);
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

void setV3(GLuint p, const char* n, Vec3 v) { glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z); }

} // namespace

ModelRenderer::~ModelRenderer() {
    for (auto& [name, m] : cache_) {
        for (GLuint t : m->textures) glDeleteTextures(1, &t);
        for (auto& l : m->lods) {
            glDeleteBuffers(1, &l.vbo);
            glDeleteVertexArrays(1, &l.vao);
        }
    }
    if (prog_) glDeleteProgram(prog_);
}

bool ModelRenderer::init(std::string* error) {
    prog_ = compileProgram(kModelVS, kModelFS);
    if (!prog_) { if (error) *error = "model shader failed"; return false; }
    locModel_ = glGetUniformLocation(prog_, "uModel");
    locColor_ = glGetUniformLocation(prog_, "uColor");
    locTextured_ = glGetUniformLocation(prog_, "uTextured");
    locAlpha_ = glGetUniformLocation(prog_, "uAlpha");
    locMode_ = glGetUniformLocation(prog_, "uMode");
    glUseProgram(prog_);
    glUniform1i(glGetUniformLocation(prog_, "uTex"), 0);
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
    else if (!loadModel3di(data, src, &err)) {}
    else if (src.lods.empty()) err = "no geometry";
    if (!err.empty()) {
        failed_[name] = err;
        if (error) *error = err;
        return nullptr;
    }

    auto g = std::make_unique<GpuModel>();
    g->name = name.substr(0, name.size() - 4);
    for (auto& t : src.textures) g->textures.push_back(uploadTexture(t));
    for (auto& l : src.lods) {
        GpuModel::Lod gl;
        std::vector<ModelVertex> all;
        for (auto& b : l.batches) {
            GpuModel::Batch gb{GLint(all.size()), GLsizei(b.vertices.size()), b.texture,
                               {b.color[0], b.color[1], b.color[2]}, b.blend};
            all.insert(all.end(), b.vertices.begin(), b.vertices.end());
            gl.batches.push_back(gb);
        }
        std::copy(l.min, l.min + 3, gl.min);
        std::copy(l.max, l.max + 3, gl.max);
        gl.triangles = l.triangleCount;
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
    const GpuModel* out = g.get();
    cache_[name] = std::move(g);
    return out;
}

int ModelRenderer::chooseLod(const GpuModel& m, float distance) {
    // Switch roughly when the object shrinks below ~25 / 10 radii of distance.
    const float r = m.radius;
    int lod = distance < r * 25 ? 0 : distance < r * 60 ? 1 : 2;
    return std::min(lod, int(m.lods.size()) - 1);
}

void ModelRenderer::begin(const Mat4& viewProj, const SceneLighting& light) {
    glUseProgram(prog_);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "uViewProj"), 1, GL_FALSE, viewProj.m);
    setV3(prog_, "uEye", light.eye);
    setV3(prog_, "uFog", light.fog);
    setV3(prog_, "uSun", normalize(light.sun));
    glUniform1f(glGetUniformLocation(prog_, "uFogStart"), light.fogStart);
    glUniform1f(glGetUniformLocation(prog_, "uFogEnd"), light.fogEnd);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_CULL_FACE);
}

void ModelRenderer::drawBatches(const GpuModel& m, int lod, const Mat4& model, bool translucent) {
    if (m.lods.empty()) return;
    const auto& l = m.lods[std::clamp(lod, 0, int(m.lods.size()) - 1)];
    glUniformMatrix4fv(locModel_, 1, GL_FALSE, model.m);
    glBindVertexArray(l.vao);
    for (const auto& b : l.batches) {
        if ((b.blend != 0) != translucent) continue;
        glUniform1i(locMode_, b.blend);
        glUniform1f(locAlpha_, b.blend == 1 ? 0.5f : 1.0f);
        if (b.blend == 2) glBlendFunc(GL_DST_COLOR, GL_ZERO);
        else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const bool textured = b.texture >= 0 && b.texture < int(m.textures.size());
        glUniform1i(locTextured_, textured);
        if (textured) glBindTexture(GL_TEXTURE_2D, m.textures[b.texture]);
        else glUniform3f(locColor_, b.color[0], b.color[1], b.color[2]);
        glDrawArrays(GL_TRIANGLES, b.first, b.count);
    }
}

void ModelRenderer::draw(const GpuModel& m, int lod, const Mat4& model) { drawBatches(m, lod, model, false); }

void ModelRenderer::drawTranslucent(const GpuModel& m, int lod, const Mat4& model) {
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    // Ground shadows sit exactly on the terrain; pull them slightly forward.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.f, -4.f);
    drawBatches(m, lod, model, true);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void ModelRenderer::end() {
    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace df
