#include "Terrain.h"

#include "../formats/Pcx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace df {

namespace {

constexpr int kChunkCells = 64;
constexpr float kSkirtDepth = 40.f;

const char* kTerrainVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;          // chunk-local cell x, cell y, skirt flag
uniform sampler2D uHeight;
uniform vec2 uOrigin;
uniform float uCell, uHScale, uMapSize, uSkirt;
uniform mat4 uViewProj;
out vec2 vCell;
out vec3 vWorld;
void main() {
    vec2 c = uOrigin + aPos.xy;
    ivec2 ic = ivec2(mod(c, uMapSize));
    float h = texelFetch(uHeight, ic, 0).r * 255.0 * uHScale - aPos.z * uSkirt;
    vWorld = vec3(c.x * uCell, h, c.y * uCell);
    vCell = c;
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
)";

const char* kTerrainFS = R"(#version 330 core
in vec2 vCell;
in vec3 vWorld;
uniform sampler2D uColor, uHeight, uDetailIdx;
uniform sampler2DArray uDetailColor, uDetailShade;
uniform float uMapSize, uCell, uHScale, uTiles;
uniform vec3 uEye, uFog, uTint, uSun;
uniform float uFogStart, uFogEnd, uSat, uGamma;
out vec4 oColor;
void main() {
    vec2 uv = (vCell + 0.5) / uMapSize;
    vec3 col = texture(uColor, uv).rgb;

    float t = 1.0 / uMapSize;
    float k = 255.0 * uHScale;
    float hl = texture(uHeight, uv - vec2(t, 0.0)).r * k;
    float hr = texture(uHeight, uv + vec2(t, 0.0)).r * k;
    float hd = texture(uHeight, uv - vec2(0.0, t)).r * k;
    float hu = texture(uHeight, uv + vec2(0.0, t)).r * k;
    vec3 n = normalize(vec3(hl - hr, 2.0 * uCell, hd - hu));

    float dist = length(vWorld - uEye);
    float fade = 1.0 - smoothstep(120.0, 700.0, dist);
    if (fade > 0.0) {
        ivec2 cell = ivec2(mod(floor(vCell), uMapSize));
        float tile = min(texelFetch(uDetailIdx, cell, 0).r * 255.0, uTiles - 1.0);
        vec3 duv = vec3(fract(vCell), tile);
        vec4 dc = texture(uDetailColor, duv);
        float shade = texture(uDetailShade, duv).r * 255.0 / 120.0;
        vec3 detailed = mix(col, dc.rgb, dc.a * 0.5) * mix(1.0, clamp(shade, 0.45, 1.3), 0.75);
        col = mix(col, detailed, fade);
    }

    col *= (0.82 + 0.3 * max(dot(n, uSun), 0.0)) * uTint;
    float l = dot(col, vec3(0.299, 0.587, 0.114));
    col = pow(max(mix(vec3(l), col, uSat), 0.0), vec3(uGamma));
    oColor = vec4(mix(col, uFog, smoothstep(uFogStart, uFogEnd, dist)), 1.0);
}
)";

const char* kSkyVS = R"(#version 330 core
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 0.9999, 1.0);
}
)";

const char* kSkyFS = R"(#version 330 core
in vec2 vNdc;
uniform mat4 uInvRot;
uniform vec2 uTan;
uniform vec3 uEye, uFog;
uniform float uCloudH, uCloudScale;
uniform sampler2D uSkyPal, uClouds;
out vec4 oColor;
void main() {
    vec3 dir = normalize((uInvRot * vec4(vNdc.x * uTan.x, vNdc.y * uTan.y, -1.0, 0.0)).xyz);
    float e = clamp(dir.y, 0.0, 1.0);
    vec3 sky = texture(uSkyPal, vec2(pow(e, 0.55), 0.5)).rgb;
    if (dir.y > 0.0 && uEye.y < uCloudH) {
        float t = (uCloudH - uEye.y) / dir.y;
        vec2 p = (uEye.xz + dir.xz * t) / uCloudScale;
        float c = clamp((texture(uClouds, p).r * 255.0 - 127.0) / 100.0, 0.0, 1.0);
        c *= smoothstep(0.0, 0.2, dir.y);
        sky = mix(sky, vec3(0.93, 0.94, 0.96), c * 0.85);
    }
    sky = mix(uFog, sky, smoothstep(-0.02, 0.08, dir.y));
    oColor = vec4(sky, 1.0);
}
)";

const char* kWaterVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform vec3 uEye;
uniform float uLevel, uExtent;
uniform mat4 uViewProj;
out vec3 vWorld;
void main() {
    vWorld = vec3(uEye.x + aPos.x * uExtent, uLevel, uEye.z + aPos.y * uExtent);
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
)";

const char* kWaterFS = R"(#version 330 core
in vec3 vWorld;
uniform sampler2D uWater;
uniform vec3 uEye, uFog;
uniform float uAlpha, uFogStart, uFogEnd, uTime;
out vec4 oColor;
void main() {
    vec2 p = vWorld.xz / 512.0;
    vec3 c = 0.5 * (texture(uWater, p + vec2(uTime * 0.010, 0.0)).rgb +
                    texture(uWater, p * 1.3 + vec2(0.0, uTime * 0.013)).rgb);
    float dist = length(vWorld - uEye);
    oColor = vec4(mix(c, uFog, smoothstep(uFogStart, uFogEnd, dist)), uAlpha);
}
)";

std::string withExt(std::string name, const char* ext) {
    if (name.find('.') == std::string::npos) name += ext;
    return name;
}

bool loadImage(Vfs& vfs, const std::string& name, IndexedImage& img) {
    if (name.empty()) return false;
    std::vector<uint8_t> data;
    return vfs.read(withExt(name, ".PCX"), data) && decodePcx(data, img);
}

GLuint makeTex2D(int w, int h, GLint internal, GLenum format, const void* data, bool linear, bool mips) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mips ? GL_LINEAR_MIPMAP_LINEAR : (linear ? GL_LINEAR : GL_NEAREST));
    if (mips) glGenerateMipmap(GL_TEXTURE_2D);
    return t;
}

GLuint makeTexArray(int w, int h, int layers, GLint internal, GLenum format, const void* data) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D_ARRAY, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internal, w, h, layers, 0, format, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    return t;
}

std::vector<uint8_t> toRgb(const IndexedImage& img, const std::array<Rgb, 256>& pal) {
    std::vector<uint8_t> rgb(img.pixels.size() * 3);
    for (size_t i = 0; i < img.pixels.size(); ++i) {
        const Rgb& c = pal[img.pixels[i]];
        rgb[i * 3] = c.r; rgb[i * 3 + 1] = c.g; rgb[i * 3 + 2] = c.b;
    }
    return rgb;
}

void setInt(GLuint p, const char* n, int v) { glUniform1i(glGetUniformLocation(p, n), v); }
void setF(GLuint p, const char* n, float v) { glUniform1f(glGetUniformLocation(p, n), v); }
void setV3(GLuint p, const char* n, Vec3 v) { glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z); }
void setM4(GLuint p, const char* n, const Mat4& m) { glUniformMatrix4fv(glGetUniformLocation(p, n), 1, GL_FALSE, m.m); }

} // namespace

Terrain::~Terrain() { release(); }

void Terrain::release() {
    GLuint texs[] = {texColor_, texHeight_, texDetailIdx_, texDetailColor_, texDetailShade_, texSkyPal_, texClouds_, texWater_};
    for (GLuint t : texs) if (t) glDeleteTextures(1, &t);
    texColor_ = texHeight_ = texDetailIdx_ = texDetailColor_ = texDetailShade_ = texSkyPal_ = texClouds_ = texWater_ = 0;
    for (auto& m : lods_) {
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.ibo) glDeleteBuffers(1, &m.ibo);
        m = {};
    }
    if (skyVao_) glDeleteVertexArrays(1, &skyVao_);
    if (waterVao_) glDeleteVertexArrays(1, &waterVao_);
    if (waterVbo_) glDeleteBuffers(1, &waterVbo_);
    skyVao_ = waterVao_ = waterVbo_ = 0;
    for (GLuint p : {progTerrain_, progSky_, progWater_}) if (p) glDeleteProgram(p);
    progTerrain_ = progSky_ = progWater_ = 0;
}

bool Terrain::load(Vfs& vfs, const std::string& trnName, std::string* error) {
    release();
    auto fail = [&](const std::string& msg) { if (error) *error = msg; return false; };

    std::vector<uint8_t> raw;
    if (!vfs.read(withExt(trnName, ".TRN"), raw)) return fail("terrain " + trnName + " not found");
    desc_ = TerrainDesc{};
    if (!parseTrn(std::string(raw.begin(), raw.end()), desc_)) return fail("cannot parse " + trnName);

    IndexedImage color, elev, detail;
    if (!loadImage(vfs, desc_.colorMap, color)) return fail("missing colour map " + desc_.colorMap);
    if (!loadImage(vfs, desc_.elevMap, elev)) return fail("missing elevation map " + desc_.elevMap);
    if (color.width != color.height || elev.width != color.width || elev.height != color.height)
        return fail("colour/elevation maps must be equal-sized squares");
    size_ = color.width;
    heights_ = elev.pixels;

    texColor_ = makeTex2D(size_, size_, GL_RGB8, GL_RGB, toRgb(color, color.palette).data(), true, true);
    texHeight_ = makeTex2D(size_, size_, GL_R8, GL_RED, heights_.data(), true, false);

    if (!loadImage(vfs, desc_.detailMap, detail) || detail.width != size_ || detail.height != size_) {
        detail.width = detail.height = size_;
        detail.pixels.assign(size_t(size_) * size_, 0);
    }
    texDetailIdx_ = makeTex2D(size_, size_, GL_R8, GL_RED, detail.pixels.data(), false, false);

    // Detail tiles: a WxH strip of square WxW tiles stacked vertically.
    IndexedImage dC, dS, dD;
    int tile = 16, tiles = 1;
    std::vector<uint8_t> dcRgba, dsRg;
    if (loadImage(vfs, desc_.detailColor, dC) && loadImage(vfs, desc_.detailShade, dS) &&
        dC.width == dS.width && dC.height == dS.height && dC.height % dC.width == 0) {
        tile = dC.width;
        tiles = dC.height / tile;
        bool haveElev = loadImage(vfs, desc_.detailElev, dD) && dD.pixels.size() == dC.pixels.size();
        dcRgba.resize(dC.pixels.size() * 4);
        dsRg.resize(dC.pixels.size() * 2);
        for (size_t i = 0; i < dC.pixels.size(); ++i) {
            const Rgb& c = dC.palette[dC.pixels[i]];
            dcRgba[i * 4] = c.r; dcRgba[i * 4 + 1] = c.g; dcRgba[i * 4 + 2] = c.b;
            dcRgba[i * 4 + 3] = dC.pixels[i] ? 255 : 0;
            dsRg[i * 2] = dS.pixels[i] ? dS.pixels[i] : 120;
            dsRg[i * 2 + 1] = haveElev ? dD.pixels[i] : 128;
        }
    } else {
        dcRgba.assign(size_t(tile) * tile * 4, 0);
        dsRg.assign(size_t(tile) * tile * 2, 120);
    }
    texDetailColor_ = makeTexArray(tile, tile, tiles, GL_RGBA8, GL_RGBA, dcRgba.data());
    texDetailShade_ = makeTexArray(tile, tile, tiles, GL_RG8, GL_RG, dsRg.data());

    // Sky gradient: palette entries 0..15 of the sky_palette PCX run from the
    // horizon (0) to the zenith (15); the remainder is a stock VGA palette.
    IndexedImage skyPal;
    std::vector<uint8_t> grad;
    if (loadImage(vfs, desc_.skyPalette, skyPal) && skyPal.hasPalette) {
        for (int i = 0; i < 16; ++i) {
            grad.push_back(skyPal.palette[i].r);
            grad.push_back(skyPal.palette[i].g);
            grad.push_back(skyPal.palette[i].b);
        }
    }
    if (grad.size() < 6) grad = {170, 200, 230, 70, 110, 190};
    horizon_ = {grad[0], grad[1], grad[2]};
    texSkyPal_ = makeTex2D(int(grad.size() / 3), 1, GL_RGB8, GL_RGB, grad.data(), true, false);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

    IndexedImage clouds;
    if (!loadImage(vfs, desc_.skyMap, clouds)) {
        clouds.width = clouds.height = 1;
        clouds.pixels = {127};
    }
    texClouds_ = makeTex2D(clouds.width, clouds.height, GL_R8, GL_RED, clouds.pixels.data(), true, true);

    IndexedImage water, waterPal;
    hasWater_ = false;
    if (loadImage(vfs, desc_.waterMap, water)) {
        const auto& pal = (loadImage(vfs, desc_.waterPalette, waterPal) && waterPal.hasPalette) ? waterPal.palette : water.palette;
        texWater_ = makeTex2D(water.width, water.height, GL_RGB8, GL_RGB, toRgb(water, pal).data(), true, true);
        hasWater_ = true;
    }

    progTerrain_ = compileProgram(kTerrainVS, kTerrainFS);
    progSky_ = compileProgram(kSkyVS, kSkyFS);
    progWater_ = compileProgram(kWaterVS, kWaterFS);
    if (!progTerrain_ || !progSky_ || !progWater_) return fail("shader compilation failed (see stderr)");

    glUseProgram(progTerrain_);
    setInt(progTerrain_, "uHeight", 0);
    setInt(progTerrain_, "uColor", 1);
    setInt(progTerrain_, "uDetailIdx", 2);
    setInt(progTerrain_, "uDetailColor", 3);
    setInt(progTerrain_, "uDetailShade", 4);
    setF(progTerrain_, "uTiles", float(tiles));
    glUseProgram(progSky_);
    setInt(progSky_, "uSkyPal", 0);
    setInt(progSky_, "uClouds", 1);
    glUseProgram(progWater_);
    setInt(progWater_, "uWater", 0);
    glUseProgram(0);

    buildMeshes();
    return true;
}

Terrain::Mesh Terrain::makeGrid(int res) {
    std::vector<float> v;
    std::vector<uint32_t> idx;
    const float step = float(kChunkCells) / res;
    const int n = res + 1;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) { v.push_back(x * step); v.push_back(y * step); v.push_back(0); }
    for (int y = 0; y < res; ++y)
        for (int x = 0; x < res; ++x) {
            uint32_t a = y * n + x, b = a + 1, c = a + n, d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    // Skirts hang below each edge to hide cracks between neighbouring LODs.
    auto edge = [&](int x0, int y0, int dx, int dy) {
        for (int i = 0; i < res; ++i) {
            int ax = x0 + dx * i, ay = y0 + dy * i, bx = ax + dx, by = ay + dy;
            uint32_t base = uint32_t(v.size() / 3);
            for (auto [px, py] : {std::pair{ax, ay}, std::pair{bx, by}}) {
                v.push_back(px * step); v.push_back(py * step); v.push_back(0);
                v.push_back(px * step); v.push_back(py * step); v.push_back(1);
            }
            idx.insert(idx.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
        }
    };
    edge(0, 0, 1, 0);
    edge(0, res, 1, 0);
    edge(0, 0, 0, 1);
    edge(res, 0, 0, 1);

    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, std::ptrdiff_t(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &m.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, std::ptrdiff_t(idx.size() * sizeof(uint32_t)), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
    m.count = GLsizei(idx.size());
    return m;
}

void Terrain::buildMeshes() {
    const int res[4] = {64, 32, 16, 8};
    for (int i = 0; i < 4; ++i) lods_[i] = makeGrid(res[i]);

    glGenVertexArrays(1, &skyVao_);
    const float quad[] = {-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
    glGenVertexArrays(1, &waterVao_);
    glBindVertexArray(waterVao_);
    glGenBuffers(1, &waterVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, waterVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

float Terrain::heightAt(float x, float z) const {
    if (size_ == 0) return 0;
    float cx = x / scale.cellSize, cz = z / scale.cellSize;
    float fx = std::floor(cx), fz = std::floor(cz);
    float tx = cx - fx, tz = cz - fz;
    auto h = [&](long ix, long iz) {
        ix = ((ix % size_) + size_) % size_;
        iz = ((iz % size_) + size_) % size_;
        return float(heights_[size_t(iz) * size_ + size_t(ix)]);
    };
    long ix = long(fx), iz = long(fz);
    float a = h(ix, iz) + (h(ix + 1, iz) - h(ix, iz)) * tx;
    float b = h(ix, iz + 1) + (h(ix + 1, iz + 1) - h(ix, iz + 1)) * tx;
    return (a + (b - a) * tz) * scale.heightScale;
}

Vec3 Terrain::fogColor() const { return {horizon_.r / 255.f, horizon_.g / 255.f, horizon_.b / 255.f}; }

void Terrain::draw(const Mat4& view, const Mat4& proj, Vec3 eye, float viewDistance) {
    const Mat4 viewProj = proj * view;
    const Vec3 fog = fogColor();
    const float fogStart = viewDistance * 0.35f, fogEnd = viewDistance * 0.98f;
    const float cell = scale.cellSize;

    // Sky (behind everything).
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(progSky_);
    setM4(progSky_, "uInvRot", inverseRotationOnly(view));
    {
        // Recover tan(fov/2) and aspect from the projection matrix.
        glUniform2f(glGetUniformLocation(progSky_, "uTan"), 1.f / proj.m[0], 1.f / proj.m[5]);
    }
    setV3(progSky_, "uEye", eye);
    setV3(progSky_, "uFog", fog);
    setF(progSky_, "uCloudH", std::max(desc_.skyHeight * scale.heightScale, 600.f));
    setF(progSky_, "uCloudScale", 8192.f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texSkyPal_);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, texClouds_);
    glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    // Terrain chunks around the camera; the map repeats endlessly in x and z.
    glUseProgram(progTerrain_);
    setM4(progTerrain_, "uViewProj", viewProj);
    setF(progTerrain_, "uCell", cell);
    setF(progTerrain_, "uHScale", scale.heightScale);
    setF(progTerrain_, "uMapSize", float(size_));
    setF(progTerrain_, "uSkirt", kSkirtDepth);
    setV3(progTerrain_, "uEye", eye);
    setV3(progTerrain_, "uFog", fog);
    setV3(progTerrain_, "uTint", {desc_.filter[0] / 128.f, desc_.filter[1] / 128.f, desc_.filter[2] / 128.f});
    const float sun = float(std::clamp(desc_.sunSlope, 5, 90)) * 3.14159265f / 180.f;
    setV3(progTerrain_, "uSun", normalize(Vec3{std::cos(sun) * 0.6f, std::sin(sun), std::cos(sun) * 0.8f}));
    setF(progTerrain_, "uFogStart", fogStart);
    setF(progTerrain_, "uFogEnd", fogEnd);
    setF(progTerrain_, "uSat", desc_.saturation / 128.f);
    setF(progTerrain_, "uGamma", 128.f / std::max(desc_.gamma, 1));
    GLuint units[5] = {texHeight_, texColor_, texDetailIdx_, texDetailColor_, texDetailShade_};
    for (int i = 0; i < 5; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(i >= 3 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, units[i]);
    }

    const Vec3 fwd{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)};
    const float chunkWorld = kChunkCells * cell;
    const float chunkRadius = chunkWorld * 0.75f + 255.f * scale.heightScale;
    const GLint locOrigin = glGetUniformLocation(progTerrain_, "uOrigin");
    const int cx0 = int(std::floor((eye.x - viewDistance) / chunkWorld));
    const int cx1 = int(std::floor((eye.x + viewDistance) / chunkWorld));
    const int cz0 = int(std::floor((eye.z - viewDistance) / chunkWorld));
    const int cz1 = int(std::floor((eye.z + viewDistance) / chunkWorld));
    for (int cz = cz0; cz <= cz1; ++cz)
        for (int cx = cx0; cx <= cx1; ++cx) {
            Vec3 center{(cx + 0.5f) * chunkWorld, eye.y, (cz + 0.5f) * chunkWorld};
            Vec3 d = center - eye;
            float horiz = std::sqrt(d.x * d.x + d.z * d.z);
            if (horiz > viewDistance + chunkRadius) continue;
            if (dot(d, fwd) < -chunkRadius) continue;
            int lod = horiz < 700 ? 0 : horiz < 1500 ? 1 : horiz < 3000 ? 2 : 3;
            glUniform2f(locOrigin, float(cx * kChunkCells), float(cz * kChunkCells));
            glBindVertexArray(lods_[lod].vao);
            glDrawElements(GL_TRIANGLES, lods_[lod].count, GL_UNSIGNED_INT, nullptr);
        }

    // Translucent water plane.
    const float level = desc_.waterHeight * scale.heightScale;
    if (hasWater_ && eye.y > level) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(progWater_);
        setM4(progWater_, "uViewProj", viewProj);
        setV3(progWater_, "uEye", eye);
        setV3(progWater_, "uFog", fog);
        setF(progWater_, "uLevel", level);
        setF(progWater_, "uExtent", viewDistance);
        setF(progWater_, "uAlpha", desc_.waterOpacity > 0 ? desc_.waterOpacity : 1.f);
        setF(progWater_, "uFogStart", fogStart);
        setF(progWater_, "uFogEnd", fogEnd);
        setF(progWater_, "uTime", SDL_GetTicks() / 1000.f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texWater_);
        glBindVertexArray(waterVao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

} // namespace df
