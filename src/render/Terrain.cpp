#include "Terrain.h"

#include "../formats/Pcx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace df {

namespace {

constexpr int kChunkCells = 64;
constexpr float kSkirtDepth = 8.f;

// The original's colour pipeline (DF.EXE, see docs/FORMATS.md "Terrain
// rendering"): palette colours are mixed in a "linear" space (decode ^1.25,
// encode ^0.8), then saturation (Y = (R+2G+B)/4) and the per-channel
// gamma/filter tables (uploaded as a 256-entry lookup texture) are applied.
const char* kPost = R"(
uniform sampler2D uLut;
uniform float uSat;
vec3 dec(vec3 c) { return pow(max(c, 0.0), vec3(1.25)); }
vec3 enc(vec3 c) { return pow(max(c, 0.0), vec3(0.8)); }
vec3 post(vec3 c) {
    float y = (c.r + 2.0 * c.g + c.b) * 0.25;
    c = clamp(y + (c - y) * uSat, 0.0, 1.0);
    float k = 255.0 / 256.0, o = 0.5 / 256.0;
    return vec3(texture(uLut, vec2(c.r * k + o, 0.5)).r, texture(uLut, vec2(c.g * k + o, 0.5)).g,
                texture(uLut, vec2(c.b * k + o, 0.5)).b);
}
)";

const char* kTerrainVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;          // chunk-local cell x, cell y, skirt flag
uniform sampler2D uHeight;
uniform vec2 uOrigin;
uniform float uCell, uHScale, uMapSize, uSkirt, uChunkScale;
uniform mat4 uViewProj;
out vec2 vCell;
out vec3 vWorld;
void main() {
    vec2 c = uOrigin + aPos.xy * uChunkScale;
    ivec2 ic = ivec2(mod(c, uMapSize));
    float h = texelFetch(uHeight, ic, 0).r * 255.0 * uHScale - aPos.z * uSkirt;
    vWorld = vec3(c.x * uCell, h, c.y * uCell);
    vCell = c;
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
)";

const char* kTerrainFSBody = R"(
in vec2 vCell;
in vec3 vWorld;
uniform sampler2D uColor, uDetailIdx;
uniform sampler2DArray uDetailColor, uDetailShade;
uniform float uMapSize, uTiles, uRingScale;
uniform vec3 uEye, uFog, uCloudCol;
out vec4 oColor;
void main() {
    vec2 uv = (vCell + 0.5) / uMapSize;
    float dist = length(vWorld.xz - uEye.xz);
    float near = 100.0 * uRingScale;
    if (dist < near) {
        // Near rings (0x438fac): bilinear colour map; detail shade darkens
        // toward black below 128 and brightens toward sky colour 16 above;
        // a detail colour is averaged 50/50. No sun lighting in the original.
        vec3 col = post(texture(uColor, uv, -16.0).rgb);
        ivec2 cell = ivec2(mod(floor(vCell), uMapSize));
        float tile = min(texelFetch(uDetailIdx, cell, 0).r * 255.0, uTiles - 1.0);
        vec3 duv = vec3(fract(vCell), tile);
        float sh = texture(uDetailShade, duv).r * 255.0;
        if (sh < 128.0) col *= 1.0 - 2.0 * (128.0 - sh) / 256.0;
        else col += (post(uCloudCol) - col) * (2.0 * (sh - 128.0) / 256.0);
        vec4 dc = texture(uDetailColor, duv);
        if (dc.a > 0.5) col = 0.5 * col + 0.5 * post(dc.rgb);
        oColor = vec4(col, 1.0);
        return;
    }
    // Far rings (0x43b3f0): ring k (100·2^k .. 100·2^(k+1) m) samples colour
    // mip k with fog level k: f = max(0, 20k - 15)/256 toward sky colour 0.
    float k = clamp(floor(log2(dist / near)), 0.0, 6.0);
    vec3 base = textureLod(uColor, uv, k).rgb;
    float f = max(0.0, 20.0 * k - 15.0) / 256.0;
    oColor = vec4(post(enc(mix(dec(base), dec(uFog), f))), 1.0);
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

const char* kSkyFSBody = R"(
in vec2 vNdc;
uniform mat4 uInvRot;
uniform vec2 uTan;
uniform vec3 uEye;
uniform float uCloudH, uHorizon, uPitchTan, uTime;
uniform vec3 uPal[17];
uniform sampler2D uClouds;
out vec4 oColor;
void main() {
    vec3 v = vec3(vNdc.x * uTan.x, vNdc.y * uTan.y, -1.0);
    vec3 dir = normalize((uInvRot * vec4(v, 0.0)).xyz);
    // Gradient (0x43b930): index L = 2·horizon per 640-scale row above the
    // horizon row (centre + tan(pitch)·F + 5); colours 0..15 interpolated
    // in linear space, 16 sub-steps each.
    float rows = 320.0 * (v.y / -v.z + uPitchTan) + 5.0;
    float L = clamp(rows * 2.0 * uHorizon, 0.0, 255.0);
    int kk = int(L) >> 4;
    float j = mod(floor(L), 16.0) / 16.0;
    vec3 sky = kk == 0 ? dec(uPal[0]) : mix(dec(uPal[kk - 1]), dec(uPal[min(kk, 15)]), j);
    // Clouds (0x43b5f4): the sky map projected on a plane sky_height m up,
    // 8 m per texel, drifting with time; pixels over 128 blend toward sky
    // colour 16 in 64 steps.
    if (dir.y > 0.001 && uEye.y < uCloudH) {
        float t = (uCloudH - uEye.y) / dir.y;
        vec2 p = (uEye.xz + dir.xz * t) / 8.0 + vec2(uTime / 64.0);
        float px = texture(uClouds, p / 512.0).r * 255.0;
        float c = px < 128.0 ? 0.0 : min(62.0, floor((px - 128.0) / 2.0)) / 64.0;
        sky = mix(sky, dec(uPal[16]), c);
    }
    oColor = vec4(post(enc(sky)), 1.0);
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
    vec2 p = vWorld.xz / 64.0;
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

    // Sky palette: entries 0..15 the gradient from the horizon up (entry 0 is
    // also the fog colour), entry 16 the cloud/highlight colour (DF.EXE 0x433880).
    IndexedImage skyPal;
    for (int i = 0; i < 17; ++i) skyColors_[i] = {170, 200, 230};
    if (loadImage(vfs, desc_.skyPalette, skyPal) && skyPal.hasPalette)
        for (int i = 0; i < 17; ++i) skyColors_[i] = skyPal.palette[i];
    horizon_ = skyColors_[0];

    // Gamma/filter tables (DF.EXE 0x4356c8): curve C[i] = round(sqrt(255 i) - i);
    // g = gamma - 128; v = i + C·2g/256 (or minus for g < 0); out = min(255,
    // 2·(v·filter/256)) per channel.
    std::vector<uint8_t> lut(256 * 3);
    const int g = desc_.gamma - 128;
    for (int i = 0; i < 256; ++i) {
        const int c = int(std::lround(std::sqrt(255.0 * i) - i));
        int v = g >= 0 ? i + ((c * 2 * g) >> 8) : std::max(0, i - ((c * std::min(255, -2 * g)) >> 8));
        for (int ch = 0; ch < 3; ++ch) lut[size_t(i) * 3 + ch] = uint8_t(std::min(255, 2 * ((v * desc_.filter[ch]) >> 8)));
    }
    texSkyPal_ = makeTex2D(256, 1, GL_RGB8, GL_RGB, lut.data(), false, false);
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

    const std::string terrainFS = std::string("#version 330 core\n") + kPost + kTerrainFSBody;
    const std::string skyFS = std::string("#version 330 core\n") + kPost + kSkyFSBody;
    progTerrain_ = compileProgram(kTerrainVS, terrainFS.c_str());
    progSky_ = compileProgram(kSkyVS, skyFS.c_str());
    progWater_ = compileProgram(kWaterVS, kWaterFS);
    if (!progTerrain_ || !progSky_ || !progWater_) return fail("shader compilation failed (see stderr)");

    glUseProgram(progTerrain_);
    setInt(progTerrain_, "uHeight", 0);
    setInt(progTerrain_, "uColor", 1);
    setInt(progTerrain_, "uDetailIdx", 2);
    setInt(progTerrain_, "uDetailColor", 3);
    setInt(progTerrain_, "uDetailShade", 4);
    setInt(progTerrain_, "uLut", 5);
    setF(progTerrain_, "uTiles", float(tiles));
    setF(progTerrain_, "uSat", desc_.saturation / 128.f);
    glUseProgram(progSky_);
    setInt(progSky_, "uLut", 0);
    setInt(progSky_, "uClouds", 1);
    setF(progSky_, "uSat", desc_.saturation / 128.f);
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

Vec3 Terrain::sunDirection() const {
    // The original has no sun lighting (sun_slope is never read); models
    // still use this direction until their lighting is traced.
    return normalize(Vec3{0.3f, 0.9f, 0.4f});
}

void Terrain::draw(const Mat4& view, const Mat4& proj, Vec3 eye, float viewDistance) {
    const Mat4 viewProj = proj * view;
    const Vec3 fog = fogColor();
    const float fogStart = viewDistance * 0.35f, fogEnd = viewDistance * 0.98f;
    const float cell = scale.cellSize;
    auto rgb = [](Rgb c) { return Vec3{c.r / 255.f, c.g / 255.f, c.b / 255.f}; };

    // Sky (behind everything).
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(progSky_);
    setM4(progSky_, "uInvRot", inverseRotationOnly(view));
    glUniform2f(glGetUniformLocation(progSky_, "uTan"), 1.f / proj.m[0], 1.f / proj.m[5]);
    setV3(progSky_, "uEye", eye);
    setF(progSky_, "uCloudH", desc_.skyHeight);   // metres (DF.EXE 0x43b5f4)
    setF(progSky_, "uHorizon", desc_.horizon);
    {
        const Vec3 fwd{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)};
        const float pitch = std::asin(std::clamp(fwd.y, -1.f, 1.f));
        setF(progSky_, "uPitchTan", std::tan(pitch));
    }
    setF(progSky_, "uTime", float(SDL_GetTicks()) * 60.f / 1000.f);
    for (int i = 0; i < 17; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "uPal[%d]", i);
        setV3(progSky_, name, rgb(skyColors_[i]));
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texSkyPal_);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, texClouds_);
    glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    // Terrain around the camera; the map repeats endlessly. DF.EXE draws
    // terrain out to 40·F m (12.8 km at 640 wide, ring table 0x438404).
    glUseProgram(progTerrain_);
    setM4(progTerrain_, "uViewProj", viewProj);
    setF(progTerrain_, "uCell", cell);
    setF(progTerrain_, "uHScale", scale.heightScale);
    setF(progTerrain_, "uMapSize", float(size_));
    setF(progTerrain_, "uSkirt", kSkirtDepth);
    setF(progTerrain_, "uRingScale", ringScale);
    setV3(progTerrain_, "uEye", eye);
    setV3(progTerrain_, "uFog", fog);
    setV3(progTerrain_, "uCloudCol", rgb(skyColors_[16]));
    GLuint units[6] = {texHeight_, texColor_, texDetailIdx_, texDetailColor_, texDetailShade_, texSkyPal_};
    for (int i = 0; i < 6; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(i == 3 || i == 4 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, units[i]);
    }

    const float terrainDistance = 40.f * 320.f * ringScale;
    const Vec3 fwd{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)};
    const GLint locOrigin = glGetUniformLocation(progTerrain_, "uOrigin");
    const GLint locScale = glGetUniformLocation(progTerrain_, "uChunkScale");
    const float chunkWorld = kChunkCells * cell, blockCells = kChunkCells * 8.f, blockWorld = blockCells * cell;
    const int bx = int(std::floor(eye.x / blockWorld)), bz = int(std::floor(eye.z / blockWorld));
    auto visible = [&](float cx, float cz, float size) {
        const float radius = size * 0.75f + 255.f * scale.heightScale;
        const Vec3 d{cx - eye.x, 0, cz - eye.z};
        const float horiz = std::sqrt(d.x * d.x + d.z * d.z);
        if (horiz > terrainDistance + radius) return -1.f;
        if (d.x * fwd.x + d.z * fwd.z < -radius) return -1.f;
        return horiz;
    };
    // Near: 64-cell chunks within two 512-cell blocks of the camera's block.
    glUniform1f(locScale, 1.f);
    for (int zb = bz - 2; zb <= bz + 2; ++zb)
        for (int xb = bx - 2; xb <= bx + 2; ++xb)
            for (int cz = zb * 8; cz < zb * 8 + 8; ++cz)
                for (int cx = xb * 8; cx < xb * 8 + 8; ++cx) {
                    const float horiz = visible((cx + 0.5f) * chunkWorld, (cz + 0.5f) * chunkWorld, chunkWorld);
                    if (horiz < 0) continue;
                    const int lod = horiz < 160 ? 0 : horiz < 400 ? 1 : horiz < 800 ? 2 : 3;
                    glUniform2f(locOrigin, float(cx * kChunkCells), float(cz * kChunkCells));
                    glBindVertexArray(lods_[lod].vao);
                    glDrawElements(GL_TRIANGLES, lods_[lod].count, GL_UNSIGNED_INT, nullptr);
                }
    // Far: whole 512-cell blocks as one coarse chunk each.
    glUniform1f(locScale, 8.f);
    const int reach = int(std::ceil(terrainDistance / blockWorld)) + 1;
    for (int zb = bz - reach; zb <= bz + reach; ++zb)
        for (int xb = bx - reach; xb <= bx + reach; ++xb) {
            if (std::abs(zb - bz) <= 2 && std::abs(xb - bx) <= 2) continue;
            const float horiz = visible((xb + 0.5f) * blockWorld, (zb + 0.5f) * blockWorld, blockWorld);
            if (horiz < 0) continue;
            // Sample spacing like the original's far rings: 16 m to 3.2 km,
            // 32 m to 6.4 km, 64 m beyond (scaled).
            const int lod = horiz < 3200 * ringScale ? 1 : horiz < 6400 * ringScale ? 2 : 3;
            glUniform2f(locOrigin, float(xb) * blockCells, float(zb) * blockCells);
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
