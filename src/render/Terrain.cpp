#include "Terrain.h"
#include "../formats/Image.h"

#include "../formats/Pcx.h"

#include <algorithm>
#include <array>
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
// Delta Force: the TRN grade is baked into the colour-map and detail
// palettes at load; post() is the df.cfg table alone (uSat 1). Delta Force
// 2: post() is the TRN saturation and table.
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
layout(location = 1) in vec2 aOrigin;       // per instance: the chunk's first cell
uniform sampler2D uHeight, uDetailIdx;
uniform sampler2DArray uDetailShade;        // r: shade, g: detail elevation
uniform float uCell, uHScale, uMapSize, uSkirt, uChunkScale, uTiles, uNear, uBumps;
uniform float uFarBase, uKMax;
uniform bool uHeightMips;
uniform bool uDf2;
uniform float uBand0;   // Delta Force 2: the innermost near band's end, F / p1 / 64 m
uniform vec3 uEye;
uniform mat4 uViewProj;
out vec2 vCell;
out vec3 vWorld;
float elev(vec2 c) {
    vec2 m = mod(c, uMapSize);
    return texelFetch(uHeight, ivec2(m), 0).r * uHScale;   // elevation steps
}
void main() {
    vec2 c = aOrigin + aPos.xy * uChunkScale;
    // Bilinear between the corner elevations (fine meshes sit between cells).
    vec2 f = fract(c), b = floor(c);
    float h = mix(mix(elev(b), elev(b + vec2(1, 0)), f.x), mix(elev(b + vec2(0, 1)), elev(b + vec2(1, 1)), f.x), f.y);
    // Delta Force far rings (GOG Df.exe 0x48c440): ring k's grid is 2^k
    // cells and its heights are pyramid level k (the highest of each 2^k
    // block), k as the colour mip below.
    float dist0 = length(c * uCell - uEye.xz);
    if (uHeightMips && dist0 >= uNear) {
        int k = int(clamp(floor(log2(dist0 / uFarBase)), 0.0, uKMax));
        float s = exp2(float(k));
        vec2 cs = floor(c / s);
        c = cs * s;
        ivec2 sz = textureSize(uHeight, k);
        h = texelFetch(uHeight, ivec2(mod(cs, vec2(sz))), k).r * uHScale;
    }
    // Near rings: detail elevation e >= 128 raises the ground by (e - 128)/128 m
    // (DF.EXE 0x438fac). Delta Force 2 (GOG Df2.exe 0x471c00): every
    // sample by e/256 m (2e in its 8.8 heights, the stored map's 2 x tile
    // maximum bias taken off again), the texel nearest the sample in the
    // max pyramid level of its band (bands end at 2^j x F / p1 / 64 m,
    // level j; 0x4707b0, 0x472440).
    float dist = length(c * uCell - uEye.xz);
    if (uBumps > 0.5 && dist < uNear) {
        float tile = min(texelFetch(uDetailIdx, ivec2(mod(b, uMapSize)), 0).r * 255.0, uTiles - 1.0);
        if (uDf2) {
            int L = int(clamp(ceil(log2(max(dist, 1e-3) / uBand0)), 0.0, 5.0));
            ivec2 ts = textureSize(uDetailShade, L).xy;
            ivec2 t = clamp(ivec2(f * vec2(ts)), ivec2(0), ts - 1);
            h += texelFetch(uDetailShade, ivec3(t, int(tile)), L).g * 255.0 / 256.0;
        } else {
            float e = texture(uDetailShade, vec3(f, tile)).g * 255.0;
            if (e >= 128.0) h += (e - 128.0) / 128.0;
        }
    }
    h -= aPos.z * uSkirt;
    vWorld = vec3(c.x * uCell, h, c.y * uCell);
    vCell = c;
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
)";

const char* kTerrainFSBody = R"(
in vec2 vCell;
in vec3 vWorld;
uniform sampler2D uColor, uDetailIdx, uColorFar;
uniform sampler2DArray uDetailColor, uDetailShade;
uniform bool uAlphaDetail;
uniform float uMapSize, uTiles, uNear, uFarBase, uKMax, uEnd;
uniform vec3 uEye, uFog, uCloudCol;
uniform bool uDf2;
out vec4 oColor;
void main() {
    vec2 uv = (vCell + 0.5) / uMapSize;
    float dist = length(vWorld.xz - uEye.xz);
    if (dist < uNear) {
        // Near rings (0x438fac): bilinear colour map; detail shade darkens
        // toward black below 128 and brightens toward sky colour 16 above;
        // a detail colour is averaged 50/50. No sun lighting in the original.
        vec3 col = post(texture(uColor, uv, -16.0).rgb);
        ivec2 cell = ivec2(mod(floor(vCell), uMapSize));
        float tile = min(texelFetch(uDetailIdx, cell, 0).r * 255.0, uTiles - 1.0);
        vec3 duv = vec3(fract(vCell), tile);
        if (uAlphaDetail) {   // Delta Force 2
            vec4 dc = texture(uDetailColor, duv);
            col = mix(col, post(dc.rgb), dc.a);
            oColor = vec4(col, 1.0);
            return;
        }
        // Delta Force (GOG Df.exe 0x489ff0): the tile texel's raw shade s
        // and colour d; s == 0: d's colour replaces the ground (none: as
        // is); otherwise shade, then d averaged 50/50.
        ivec3 dsz = textureSize(uDetailShade, 0);
        ivec3 dt = ivec3(min(ivec2(fract(vCell) * vec2(dsz.xy)), dsz.xy - 1), int(tile));
        float sh = texelFetch(uDetailShade, dt, 0).r * 255.0;
        vec4 dc = texelFetch(uDetailColor, dt, 0);
        if (sh < 0.5) {
            if (dc.a > 0.5) col = post(dc.rgb);
        } else {
            if (sh < 128.0) col *= 1.0 - 2.0 * (128.0 - sh) / 256.0;
            // toward raw sky entry 16 ([0xbd67cc], built from the sky file's
            // bytes by 0x47b590): no saturation or display table on it.
            else col += (uCloudCol - col) * (2.0 * (sh - 128.0) / 256.0);
            if (dc.a > 0.5) col = 0.5 * col + 0.5 * post(dc.rgb);
        }
        oColor = vec4(col, 1.0);
        return;
    }
    // Far rings (0x43b3f0): ring k (uFarBase·2^k .. uFarBase·2^(k+1) m, k
    // up to uKMax) samples colour mip k with fog level k: f = max(0, 20k -
    // 15)/256 toward sky colour 0.
    // Delta Force 2 has no ring fog (its bands 0x4678ed..0x467954 are only
    // cut at the fog distance; distance fog is the fog_level pass alone).
    float k = clamp(floor(log2(dist / uFarBase)), 0.0, uKMax);
    // Delta Force 2 (GOG Df2.exe 0x470900): every far mip carries its cells'
    // detail-tile tint (uColorFar, built at load).
    vec3 base = uAlphaDetail ? textureLod(uColorFar, uv, k).rgb : textureLod(uColor, uv, k).rgb;
    float f = uDf2 ? 0.0 : max(0.0, 20.0 * k - 15.0) / 256.0;
#ifdef RING_END
    if (dist > uEnd) discard;   // Delta Force: no ring past the last (far k 5)
#endif
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
uniform float uCloudH, uHorizon, uPitchTan, uTime, uSkyF, uSkyStepF, uCloudF;
uniform bool uDf2, uNoSky;
uniform vec3 uPal[17];
uniform sampler2D uClouds, uGrad;
uniform vec4 uViewport;
uniform int uHorizonRow, uSkyStep, uKMax;
out vec4 oColor;
// Clouds (0x43b5f4): the sky map projected on a plane sky_height m up,
// 8 m per texel, drifting with time; the texel (0..255), -1 for none.
float cloudTexel(vec3 dir) {
    if (dir.y <= 0.001 || uEye.y >= uCloudH) return -1.0;
    float t = (uCloudH - uEye.y) / dir.y;
    vec2 p = (uEye.xz + dir.xz * t) / 8.0 + vec2(uTime / 64.0);
    return texture(uClouds, p / 512.0).r * 255.0;
}
// Delta Force (GOG Df.exe 0x48c650): the level by the row's texel step,
// depth / F / 8 m, halved while over one texel (at most 8); the map holds
// the thresholded densities 0..62 (0x47b590) and their mips (0x47d920).
float cloudDensity(vec3 dir, float viewLen) {
    if (dir.y <= 0.001 || uEye.y >= uCloudH) return -1.0;
    float t = (uCloudH - uEye.y) / dir.y;
    vec2 p = (uEye.xz + dir.xz * t) / 8.0 + vec2(uTime / 64.0);
    float st = t / viewLen / uCloudF / 8.0;
    int lod = 0;
    while (st > 1.0 && lod < 8) { st *= 0.5; ++lod; }
    return floor(textureLod(uClouds, p / 512.0, float(lod)).r * 255.0 + 0.001);
}
void main() {
    vec3 v = vec3(vNdc.x * uTan.x, vNdc.y * uTan.y, -1.0);
    vec3 dir = normalize((uInvRot * vec4(v, 0.0)).xyz);
    if (uDf2) {
        // Delta Force 2 (0x467700): screen row k above the horizon row takes
        // gradient entry min(255, (k·s) >> 16), 256 steps not interpolated
        // and constant across the row; rows at or below the horizon, and
        // every row when the pitch is under -45 degrees (DF2 draws no sky
        // then), take entry 0 (OpenDF's fill).
        int row = int(floor(uViewport.y + uViewport.w - gl_FragCoord.y));
        int k = uHorizonRow - row;
        int idx = (uNoSky || k < 0) ? 0 : (k > uKMax ? 255 : min(255, (k * uSkyStep) >> 16));
        vec3 sky = texelFetch(uGrad, ivec2(idx, 0), 0).rgb;
        // Clouds (0x4679b0, span 0x485c80): lerp(gradient, cloud colour,
        // density / 256) per row LUT, in plain byte space; no grading (2.7
        // of the DF2 terrain notes).
        float px = cloudTexel(dir);
        if (px >= 0.0) sky = mix(sky, uPal[16], px / 256.0);
        oColor = vec4(sky, 1.0);
        return;
    }
    // Gradient (0x43b930, GOG Df.exe 0x48c990): index L = s per 640-scale
    // row above the horizon row (centre + tan(pitch)·F + 5), s from the
    // view's FOV and width (2·horizon in the 90-degree main view); colours
    // 0..15 interpolated in linear space, 16 sub-steps each.
    float rows = uSkyF * (v.y / -v.z + uPitchTan) + 5.0;
    float L = clamp(rows * uSkyStepF, 0.0, 255.0);
    int kk = int(L) >> 4;
    float j = mod(floor(L), 16.0) / 16.0;
    vec3 sky = kk == 0 ? dec(uPal[0]) : mix(dec(uPal[kk - 1]), dec(uPal[min(kk, 15)]), j);
    // Pixels over 128 blend toward sky colour 16 in 64 steps (thresholded
    // at load: 0..62).
    float px = cloudDensity(dir, length(v));
    if (px >= 0.0) sky = mix(sky, dec(uPal[16]), px / 64.0);
    oColor = vec4(post(enc(sky)), 1.0);
}
)";

// Delta Force 2's fog pass (DF2.EXE 0x4485b0, software frame; the D3D copy
// 0x45b620 uses the same blend): f = min(255, z >> (fog_level + 8)) with z
// the pixel's depth in 16.16 m (view depth: READ for the D3D per-vertex fog,
// the projected vertex's z >> (fog_level + 8) clamped to 0..255 at
// 0x460449..0x46045d; the pass itself 0x448640..0x4486a8). The software
// z-buffer (view +0xc64 rows) holds that same vertex depth (vertex +0x8,
// 16.16 m; the point plot 0x45ee4f tests it) stepped linearly across the
// screen: the spans start at span +0x14 and add +0x28 a pixel (0x484077),
// written where nearer (unsigned). OpenDF reads the perspective-correct view
// depth instead (APPROXIMATION: equal at the vertices and along the water's
// rows, a little nearer inside large near triangles), then out = c + (C -
// c)·f/256 per channel in plain byte space; drawn as fog_color with alpha
// f/256 over the frame. Empty depth (the sky, z 0x7fffffff) gives f = 255.
const char* kFogFS = R"(#version 330 core
uniform sampler2D uDepth;
uniform vec4 uViewport;
uniform vec3 uFogColor;
uniform float uM10, uM14, uFarM10, uFarM14;
uniform int uLevel, uSplit;
out vec4 oColor;
void main() {
    float d = texelFetch(uDepth, ivec2(gl_FragCoord.xy - uViewport.xy), 0).r;
    float f = 255.0;
    if (d < 1.0) {
        // NDC z from the window depth: one range 0..1, or (two passes) the
        // near pass's 0..0.5 and the far pass's 0.5..1.
        float ndc = uSplit == 0 ? 2.0 * d - 1.0 : d < 0.5 ? 4.0 * d - 1.0 : 4.0 * d - 3.0;
        bool far = uSplit != 0 && d >= 0.5;
        float depth = (far ? uFarM14 : uM14) / (ndc + (far ? uFarM10 : uM10));   // view depth, metres
        f = min(255.0, floor(depth * 65536.0 / exp2(float(uLevel + 8))));
    }
    oColor = vec4(uFogColor, f / 256.0);
}
)";

const char* kWaterVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform vec3 uEye;
uniform float uLevel, uExtent;
uniform mat4 uViewProj;
uniform bool uBelow;
uniform float uQuadDepth;   // the full-screen quad's NDC z
out vec3 vWorld;
void main() {
    vWorld = vec3(uEye.x + aPos.x * uExtent, uLevel, uEye.z + aPos.y * uExtent);
    // Under water: the whole view (a full-screen quad).
    gl_Position = uBelow ? vec4(aPos, uQuadDepth, 1.0) : uViewProj * vec4(vWorld, 1.0);
}
)";

// DF.EXE 0x43bfd8 / 0x43bd7a: a water pixel is W[r][c] = T[(1-op)·c + op·4r]:
// r = water texel >> 2 (0..63), c = the luminance (R+2G+B)/4 of what is
// already drawn there, T the 256-step water palette gradient (finished with
// the TRN colour pipeline). 16 texels per metre; waves warp the texture
// coordinates by up to 4 texels (sine table, period 16 m, phase tick << 23).
// No fog; drawn only where it is in front.
const char* kWaterFS = R"(#version 330 core
in vec3 vWorld;
uniform sampler2D uWaterIdx, uGradient, uScene;
uniform vec4 uViewport;     // x, y, w, h of the 3D view
uniform float uOpacity, uPhase, uMaxDepth;   // uMaxDepth: the surface's view depth limit
uniform float uWaterF;   // Delta Force: the view's focal length (original pixels), for the mip level
uniform vec3 uEye, uFwd;
uniform bool uBelow, uDf2, uTint;
uniform vec3 uWaterColor;
out vec4 oColor;
const float kTwoPi = 6.28318531;
void main() {
    if (uDf2) {
        // Delta Force 2 (DF2.EXE 0x466b60, span 0x485e17): the ripple map's
        // own (graded) colours, 16 texels a metre, the same wobble as DF1,
        // averaged 50/50 with what is drawn; under water a 50 % tint with
        // palette entry 255 (0x448bb0).
        if (uTint) { oColor = vec4(uWaterColor, 0.5); return; }
        vec2 t = vWorld.xz * 16.0;
        float iu = mod(t.x * 4.0 + uPhase, 1024.0), iv = mod(t.y * 4.0 + uPhase, 1024.0);
        vec2 w = t + 4.0 * vec2(cos(kTwoPi * iv / 1024.0), sin(kTwoPi * iu / 1024.0));
        oColor = vec4(texture(uWaterIdx, w / 256.0).rgb, 0.5);
        return;
    }
    vec3 under = texture(uScene, (gl_FragCoord.xy - uViewport.xy) / uViewport.zw).rgb * 255.0;
    float lum = floor((under.r + 2.0 * under.g + under.b) / 4.0);
    float r = 0.0;
    if (uMaxDepth > 0.0 && dot(vWorld - uEye, uFwd) > uMaxDepth) discard;
    if (!uBelow) {
        vec2 t = vWorld.xz * 16.0;                                // texels
        float iu = mod(t.x * 4.0 + uPhase, 1024.0), iv = mod(t.y * 4.0 + uPhase, 1024.0);
        vec2 w = t + 4.0 * vec2(cos(kTwoPi * iv / 1024.0), sin(kTwoPi * iu / 1024.0));
        // GOG Df.exe 0x48d040 / 0x48d570: the level per row by the texel
        // step depth / F · 16, halved while over one texel, at most 7; the
        // levels hold the map >> 2 (0..63) and its mips (0x47d920).
        float st = max(dot(vWorld - uEye, uFwd), 0.0) * 16.0 / uWaterF;
        int lod = 0;
        while (st > 1.0 && lod < 7) { st *= 0.5; ++lod; }
        r = floor(textureLod(uWaterIdx, w / 256.0, float(lod)).r * 255.0 + 0.001);
    }
    float idx = floor((1.0 - uOpacity) * lum + uOpacity * 4.0 * r);
    oColor = vec4(texture(uGradient, vec2((idx + 0.5) / 256.0, 0.5)).rgb, 1.0);
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

// An 8-bit map with explicit mips as GOG Df.exe 0x47d920 builds them: each
// texel (a + b + c + d + 2) >> 2, less 1 when not 0; `levels` below level 0.
GLuint makeIndexMips(int w, int h, std::vector<uint8_t> level, int levels, bool linear) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    int n = 0;
    for (;; ++n) {
        glTexImage2D(GL_TEXTURE_2D, n, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, level.data());
        if (n == levels || w < 2 || h < 2) break;
        const int nw = w / 2, nh = h / 2;
        std::vector<uint8_t> next(size_t(nw) * size_t(nh));
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                const size_t a = size_t(2 * y) * size_t(w) + size_t(2 * x), b = a + size_t(w);
                int v = (level[a] + level[a + 1] + level[b] + level[b + 1] + 2) >> 2;
                if (v != 0) --v;
                next[size_t(y) * size_t(nw) + size_t(x)] = uint8_t(v);
            }
        level.swap(next);
        w = nw;
        h = nh;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, n);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST);
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

// Gamma/filter tables (DF.EXE 0x4356c8 / GOG Df.exe 0x480a70): curve C[i] =
// round(sqrt(255 i) - i); g = gamma - 128; v = i + C·2g/256 (or minus for
// g < 0); out = min(255, 2·(v·filter/256)) per channel; 256 RGB entries.
std::vector<uint8_t> gammaTable(int gamma, const int filter[3]) {
    std::vector<uint8_t> lut(256 * 3);
    const int g = gamma - 128;
    for (int i = 0; i < 256; ++i) {
        const int c = int(std::lround(std::sqrt(255.0 * i) - i));
        int v = g >= 0 ? i + ((c * 2 * g) >> 8) : std::max(0, i - ((c * std::min(255, -2 * g)) >> 8));
        for (int ch = 0; ch < 3; ++ch) lut[size_t(i) * 3 + size_t(ch)] = uint8_t(std::min(255, 2 * ((v * filter[ch]) >> 8)));
    }
    return lut;
}

void setInt(GLuint p, const char* n, int v) { glUniform1i(glGetUniformLocation(p, n), v); }
void setF(GLuint p, const char* n, float v) { glUniform1f(glGetUniformLocation(p, n), v); }
void setV3(GLuint p, const char* n, Vec3 v) { glUniform3f(glGetUniformLocation(p, n), v.x, v.y, v.z); }
void setM4(GLuint p, const char* n, const Mat4& m) { glUniformMatrix4fv(glGetUniformLocation(p, n), 1, GL_FALSE, m.m); }

} // namespace

Terrain::~Terrain() { release(); }

void Terrain::release() {
    GLuint texs[] = {texDisplayLut_, texColor_, texColorFar_, texElevation_, texHeight_, texDetailIdx_, texDetailColor_, texDetailShade_, texSkyPal_, texClouds_, texWater_,
                     texWaterGrad_, texScene_, texRadar_, texSkyGrad_, texDepth_, texMap_};
    for (GLuint t : texs) if (t) glDeleteTextures(1, &t);
    texRadar_ = texDisplayLut_ = texElevation_ = texMap_ = 0;
    texColor_ = texColorFar_ = texHeight_ = texDetailIdx_ = texDetailColor_ = texDetailShade_ = texSkyPal_ = texClouds_ = texWater_ = 0;
    texWaterGrad_ = texScene_ = texSkyGrad_ = texDepth_ = 0;
    sceneW_ = sceneH_ = depthW_ = depthH_ = 0;
    for (auto& m : lods_) {
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.ibo) glDeleteBuffers(1, &m.ibo);
        m = {};
    }
    if (skyVao_) glDeleteVertexArrays(1, &skyVao_);
    if (waterVao_) glDeleteVertexArrays(1, &waterVao_);
    if (waterVbo_) glDeleteBuffers(1, &waterVbo_);
    if (instVbo_) glDeleteBuffers(1, &instVbo_);
    skyVao_ = waterVao_ = waterVbo_ = instVbo_ = 0;
    for (GLuint p : {progTerrain_, progSky_, progWater_, progFog_}) if (p) glDeleteProgram(p);
    progTerrain_ = progSky_ = progWater_ = progFog_ = 0;
}

bool Terrain::load(Vfs& vfs, const std::string& trnName, std::string* error) {
    release();
    alphaDetail_ = false;
    auto fail = [&](const std::string& msg) { if (error) *error = msg; return false; };

    std::vector<uint8_t> raw;
    if (!vfs.read(withExt(trnName, ".TRN"), raw)) return fail("terrain " + trnName + " not found");
    desc_ = TerrainDesc{};
    if (!parseTrn(std::string(raw.begin(), raw.end()), desc_)) return fail("cannot parse " + trnName);
    if (skyOverride.on) {
        desc_.skyMap = skyOverride.clouds;
        desc_.skyPalette = skyOverride.gradient;
        desc_.skyHeight = skyOverride.height;
        desc_.horizon = skyOverride.horizon;
        desc_.gamma = skyOverride.gamma;
        desc_.saturation = skyOverride.saturation;
        for (int k = 0; k < 3; ++k) desc_.filter[k] = skyOverride.filter[k];
    }
    // Delta Force 2: the mission's fog after the terrain's (0x45d6df).
    if (fogLevelOverride >= 0) desc_.fogLevel = fogLevelOverride;
    if (fogColorOverride >= 0) {
        desc_.fogColor[0] = int((fogColorOverride >> 16) & 0xFF);
        desc_.fogColor[1] = int((fogColorOverride >> 8) & 0xFF);
        desc_.fogColor[2] = int(fogColorOverride & 0xFF);
    }

    // The TRN's gamma/filter table (DF.EXE 0x4356c8 / GOG Df.exe 0x480a70).
    const std::vector<uint8_t> lut = gammaTable(desc_.gamma, desc_.filter);
    // Delta Force: the display table, df.cfg gamma with filters 128.
    const int unity[3] = {128, 128, 128};
    const std::vector<uint8_t> display = gammaTable(displayGamma, unity);
    // Delta Force: the TRN grade of one palette colour (GOG Df.exe 0x480bf0:
    // Y = (R + 2G + B) >> 2, Y + ((c - Y)·sat >> 7) clamped, then the
    // table). GOG 0x47be90 applies it once to the colour-map and detail
    // palettes; the sky, clouds, fog, highlight and water are mixed from
    // the raw colours and pass only through the df.cfg table (0x47d080).
    auto trnGrade = [&](const Rgb& c) {
        const int y = (c.r + 2 * c.g + c.b) >> 2;
        const int in[3] = {c.r, c.g, c.b};
        Rgb out;
        uint8_t* o = &out.r;
        for (int ch = 0; ch < 3; ++ch)
            o[ch] = lut[size_t(std::clamp(y + (((in[ch] - y) * desc_.saturation) >> 7), 0, 255)) * 3 + size_t(ch)];
        return out;
    };
    auto gradedPalette = [&](const std::array<Rgb, 256>& pal) {
        std::array<Rgb, 256> out = pal;
        for (auto& c : out) c = trnGrade(c);
        return out;
    };

    IndexedImage color, elev, detail;
    // Delta Force 2 and later: a true-colour JPEG/TGA colour map (no palette).
    RgbaImage colorRgba;
    bool trueColor = false;
    if (!loadImage(vfs, desc_.colorMap, color)) {
        std::vector<uint8_t> d;
        for (const char* ext : {".JPG", ".TGA"})
            if (vfs.read(withExt(desc_.colorMap, ext), d) && decodeRgbaImageDf2(d, colorRgba)) { trueColor = true; break; }
        if (!trueColor)
            return fail("missing colour map " + desc_.colorMap + (jpegSupported() ? "" : " (OpenDF was built without libjpeg)"));
        color.width = colorRgba.width;
        color.height = colorRgba.height;
        color.pixels.assign(size_t(color.width) * size_t(color.height), 0);
    }
    if (!loadImage(vfs, desc_.elevMap, elev)) return fail("missing elevation map " + desc_.elevMap);
    if (color.width != color.height || elev.width != color.width || elev.height != color.height)
        return fail("colour/elevation maps must be equal-sized squares");
    size_ = color.width;
    gen2_ = trueColor;
    if (gen2_ && waterOverride >= 0) desc_.waterHeight = float(waterOverride);
    heights_.assign(elev.pixels.begin(), elev.pixels.end());
    detailMap_.clear();
    detailElev_.clear();
    sightMax_.clear();
    if (gen2_) {
        // Delta Force 2 (DF2.EXE 0x464c30): heights smoothed into 8.8 words,
        // H = 256 · Σ w·h / 29 with the wrapping kernel [2 3 2; 3 9 3; 2 3 2],
        // compared as H << 7 against 16.16 metres: 0.5 m an elevation step.
        static const int kW[3][3] = {{2, 3, 2}, {3, 9, 3}, {2, 3, 2}};
        std::vector<float> smooth(heights_.size());
        for (int z = 0; z < size_; ++z)
            for (int x = 0; x < size_; ++x) {
                int sum = 0;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx)
                        sum += kW[dz + 1][dx + 1] *
                               elev.pixels[size_t((z + dz + size_) % size_) * size_t(size_) + size_t((x + dx + size_) % size_)];
                smooth[size_t(z) * size_t(size_) + size_t(x)] = float(256 * sum / 29) / 256.f;
            }
        heights_.swap(smooth);
        scale.heightScale = 0.5f;
    } else {
        scale.heightScale = 0.25f;
    }

    if (trueColor) {
        std::vector<uint8_t> rgb(size_t(size_) * size_t(size_) * 3);
        for (size_t i = 0; i < size_t(size_) * size_t(size_); ++i)
            for (int k = 0; k < 3; ++k) rgb[i * 3 + size_t(k)] = colorRgba.rgba[i * 4 + size_t(k)];
        // DF2.EXE 0x4637c0 (when water_height is not 0): ground under the
        // level darkened, depth = waterH - H in 8.8, d = depth·6/256,
        // RGB × (d > 230 ? 25 : 255 - d) / 256.
        if (desc_.waterHeight != 0) {
            const int waterH = int(desc_.waterHeight) * 256;
            for (size_t i = 0; i < size_t(size_) * size_t(size_); ++i) {
                const int depth = waterH - int(heights_[i] * 256.f);
                if (depth <= 0) continue;
                const int d = depth * 6 / 256, f = d > 230 ? 25 : 255 - d;
                for (int k = 0; k < 3; ++k) rgb[i * 3 + size_t(k)] = uint8_t(rgb[i * 3 + size_t(k)] * f / 256);
            }
        }
        texColor_ = makeTex2D(size_, size_, GL_RGB8, GL_RGB, rgb.data(), true, true);
        colorRgb_ = rgb;
    } else {
        texColor_ = makeTex2D(size_, size_, GL_RGB8, GL_RGB, toRgb(color, gradedPalette(color.palette)).data(), true, true);
        // Delta Force's elevation picture (F11 mapelevation, [0xc23a30]): the
        // 32-bit ramp GOG Df.exe 0x48df90 builds at 0x67d740, green =
        // TRN grade of (0, T[h], 0), red and blue 0, with T the EXE's table at
        // 0x4cebe0 (copied to 0x50342c by 0x401c60), which is exactly
        // round(255 (h / 255)^0.8); 0x48e070 reads it by each cell's
        // elevation byte.
        uint8_t ramp[256];
        for (int i = 0; i < 256; ++i) ramp[i] = uint8_t(std::lround(255.0 * std::pow(i / 255.0, 0.8)));
        std::vector<uint8_t> elevRgb(size_t(size_) * size_t(size_) * 3, 0);
        for (size_t i = 0; i < elev.pixels.size(); ++i) elevRgb[i * 3 + 1] = trnGrade(Rgb{0, ramp[elev.pixels[i]], 0}).g;
        texElevation_ = makeTex2D(size_, size_, GL_RGB8, GL_RGB, elevRgb.data(), false, false);
    }
    {
        // GOG Df.exe 0x47d080 ends with 0x47b4a0(0xbd5480): the colour-map
        // palette as graded by 0x47be90 -> 0x480d00 (TRN saturation and
        // gamma/filter table) against the TRN type colours, the first of the
        // nearest (plain squared RGB) wins; no types: 1.
        const std::array<Rgb, 256> pal = trueColor ? color.palette : gradedPalette(color.palette);
        uint8_t byIndex[256];
        for (int i = 0; i < 256; ++i) {
            int best = 1;
            long bestD = -1;
            for (const auto& t : desc_.types) {
                const long dr = pal[size_t(i)].r - t.r, dg = pal[size_t(i)].g - t.g, db = pal[size_t(i)].b - t.b;
                const long d = dr * dr + dg * dg + db * db;
                if (bestD < 0 || d < bestD) {
                    bestD = d;
                    const std::string& m = t.material;
                    best = m == "dirt" ? 1 : m == "grass" ? 2 : m == "snow" ? 3 : m == "cement" ? 4 : m == "sand" ? 5
                         : m == "packed_dirt" ? 6 : 0;
                }
            }
            byIndex[i] = uint8_t(desc_.types.empty() ? 1 : best);
        }
        surface_.resize(color.pixels.size());
        for (size_t i = 0; i < color.pixels.size(); ++i) surface_[i] = byIndex[color.pixels[i]];
        // Delta Force 2 ignores the TRN `type` lines (the parser skips them,
        // 0x465e5b): its only surface source is the .CAL class below; no
        // .CAL (or unmatched names) means class 0 everywhere.
        if (trueColor) std::fill(surface_.begin(), surface_.end(), uint8_t(0));
    }
    glGenTextures(1, &texHeight_);
    glBindTexture(GL_TEXTURE_2D, texHeight_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, size_, size_, 0, GL_RED, GL_FLOAT, heights_.data());
    {
        // The height pyramid (GOG Df.exe 0x47dd10 at 0x47dd9b..0x47ddc4:
        // 0x47d620 makes level k from k - 1, each texel the highest of its
        // 2x2), read by the far rings (0x48c440: ring k's heights from
        // level k). Down to 1x1 so the texture is complete.
        std::vector<float> prev = heights_, next;
        int w = size_, level = 0;
        while (w > 1) {
            const int n = w / 2;
            next.assign(size_t(n) * size_t(n), 0.f);
            for (int z = 0; z < n; ++z)
                for (int x = 0; x < n; ++x) {
                    const size_t a = size_t(2 * z) * size_t(w) + size_t(2 * x), b = a + size_t(w);
                    next[size_t(z) * size_t(n) + size_t(x)] = std::max({prev[a], prev[a + 1], prev[b], prev[b + 1]});
                }
            glTexImage2D(GL_TEXTURE_2D, ++level, GL_R32F, n, n, 0, GL_RED, GL_FLOAT, next.data());
            prev.swap(next);
            w = n;
        }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);

    if (!loadImage(vfs, desc_.detailMap, detail) || detail.width != size_ || detail.height != size_) {
        detail.width = detail.height = size_;
        detail.pixels.assign(size_t(size_) * size_, 0);
    }
    texDetailIdx_ = makeTex2D(size_, size_, GL_R8, GL_RED, detail.pixels.data(), false, false);
    // Delta Force 2: surfaces from the char_data .CAL, one `name,value` line
    // per detail tile (DF2.EXE 0x466290): DT dirt 1, GS grass 2, SW snow 3,
    // RK/CT rock/concrete 4, SD sand 5, RD road 8, MD mud 9, IE ice 10,
    // QS 11 (GOG Df2.exe 0x470190 the same), other names 0. Classes 8..11
    // sound only in the GOG build (Game::animEvent, Game::spawnImpact).
    if (trueColor && !desc_.charData.empty()) {
        std::vector<uint8_t> raw;
        if (vfs.read(withExt(desc_.charData, ".CAL"), raw)) {
            std::array<uint8_t, 256> cls{};
            size_t i = 0;
            std::string text(raw.begin(), raw.end()), line;
            for (size_t p = 0; p <= text.size() && i < 256; ++p) {
                if (p < text.size() && text[p] != '\n' && text[p] != '\r') { line += text[p]; continue; }
                if (line.empty()) continue;
                std::string name = line.substr(0, line.find(','));
                for (auto& ch : name) ch = char(std::toupper(static_cast<unsigned char>(ch)));
                const std::string pre = name.substr(0, 2);
                const bool digit = name.size() == 3 && name[2] >= '1' && name[2] <= '3';
                cls[i++] = !digit ? 0 : pre == "DT" ? 1 : pre == "GS" ? 2 : pre == "SW" ? 3 : (pre == "RK" || pre == "CT") ? 4 : pre == "SD" ? 5
                       : pre == "RD" ? 8 : pre == "MD" ? 9 : pre == "IE" ? 10 : pre == "QS" ? 11 : 0;
                line.clear();
            }
            // DF2.EXE's renumbering, copied with its bug: the detail map is
            // rewritten to ranks among the used indices (remap[old] = rank,
            // 0x4643a0..0x464425), then the CAL table is permuted the same
            // way again, table[i] = cal[remap[i]] (0x466668..0x4666b4), and
            // looked up through the rewritten map: class(o) =
            // cal[remap[remap[o]]]. Right only where the used indices are
            // exactly 0..N-1; on the DF* terrains it differs on 24-98 % of
            // the cells, which is what the player hears. remap of an unused
            // index is 0 at first start (READ: 0x50edf0 lies past .data's raw
            // data, zero-filled at load; its only writer 0x4643ea never clears
            // it, so a later terrain would see stale entries).
            std::array<bool, 256> used{};
            for (uint8_t p : detail.pixels) used[p] = true;
            std::array<uint8_t, 256> remap{};
            int n = 0;
            for (int o = 0; o < 256; ++o)
                if (used[size_t(o)]) remap[size_t(o)] = uint8_t(n++);
            std::array<uint8_t, 256> eff{};
            for (int o = 0; o < 256; ++o) eff[size_t(o)] = cls[remap[remap[size_t(o)]]];
            for (size_t c = 0; c < surface_.size() && c < detail.pixels.size(); ++c) surface_[c] = eff[detail.pixels[c]];
        }
    }

    // Detail tiles: a WxH strip of square WxW tiles stacked vertically.
    IndexedImage dC, dS, dD;
    int tile = 16, tiles = 1;
    std::vector<uint8_t> dcRgba, dsRg;
    if (loadImage(vfs, desc_.detailColor, dC) && loadImage(vfs, desc_.detailShade, dS) &&
        dC.width == dS.width && dC.height == dS.height && dC.height % dC.width == 0) {
        tile = dC.width;
        tiles = dC.height / tile;
        bool haveElev = loadImage(vfs, desc_.detailElev, dD) && dD.pixels.size() == dC.pixels.size();
        if (!trueColor && haveElev && tile == 16) {
            // Delta Force's sight-line detail (GOG Df.exe 0x47b590 loads the
            // 16x16 tiles' elevations into byte 2 of 0xbd531c, 256 tiles,
            // the rest zeroed by 0x47a7a0).
            detailMap_ = detail.pixels;
            detailElev_.assign(256 * 256, 0);
            std::copy_n(dD.pixels.begin(), std::min(dD.pixels.size(), detailElev_.size()), detailElev_.begin());
        }
        dcRgba.resize(dC.pixels.size() * 4);
        dsRg.resize(dC.pixels.size() * 2);
        const std::array<Rgb, 256> dPal = trueColor ? dC.palette : gradedPalette(dC.palette);
        for (size_t i = 0; i < dC.pixels.size(); ++i) {
            const Rgb& c = dPal[dC.pixels[i]];
            dcRgba[i * 4] = c.r; dcRgba[i * 4 + 1] = c.g; dcRgba[i * 4 + 2] = c.b;
            dcRgba[i * 4 + 3] = dC.pixels[i] ? 255 : 0;
            dsRg[i * 2] = dS.pixels[i];   // raw: 0 means the detail colour replaces (0x489ff0)
            dsRg[i * 2 + 1] = haveElev ? dD.pixels[i] : (trueColor ? 0 : 128);   // DF2: no relief without the map
        }
    } else {
        // Delta Force 2 (DF2.EXE terrain loader): a 64-wide TGA strip of
        // 64x64 tiles whose alpha is the blend weight over the colour map
        // (out = colour (1 - a) + detail a); no shade strip; detail_elev as
        // in DF1. One tile covers one map cell.
        RgbaImage t;
        std::vector<uint8_t> raw;
        if (vfs.read(withExt(desc_.detailColor, ".TGA"), raw) && decodeRgbaImage(raw, t) && t.width > 0 && t.height % t.width == 0) {
            tile = t.width;
            tiles = t.height / tile;
            dcRgba = t.rgba;
            const bool haveElev = loadImage(vfs, desc_.detailElev, dD) && dD.pixels.size() == size_t(t.width) * size_t(t.height);
            if (haveElev) {
                // GOG Df2.exe 0x46f680 copies each used tile's elevation into
                // the DMap chain and 0x46d970 reduces it by 2x2 maxima to one
                // byte at level 0: the tile's highest elevation. 0x46e0f0
                // adds twice that to the cell's word height.
                const size_t per = size_t(t.width) * size_t(t.width);
                std::vector<uint8_t> top(size_t(tiles), 0);
                for (size_t k = 0; k < size_t(tiles); ++k)
                    top[k] = *std::max_element(dD.pixels.begin() + long(k * per), dD.pixels.begin() + long((k + 1) * per));
                detailTop_.assign(detail.pixels.size(), 0);
                for (size_t c = 0; c < detail.pixels.size(); ++c)
                    if (detail.pixels[c] < tiles) detailTop_[c] = top[detail.pixels[c]];
            }
            dsRg.resize(size_t(t.width) * size_t(t.height) * 2);
            for (size_t i = 0; i < dsRg.size() / 2; ++i) {
                dsRg[i * 2] = 128;   // neutral shade
                dsRg[i * 2 + 1] = haveElev ? dD.pixels[i] : 0;   // no relief without the map
            }
            alphaDetail_ = true;
        } else {
            dcRgba.assign(size_t(tile) * tile * 4, 0);
            dsRg.assign(size_t(tile) * tile * 2, 120);
        }
    }
    // GOG Df2.exe's far terrain (0x470900): colour-only rings are tinted by
    // the cell's detail tile, out = sat(c * S[t] / 256 + C[t]) per channel,
    // at mip 0 when drawn (0x472380 tables -> 0x490a79) and baked into mips
    // 1..9 (0x46e0f0, then the box mips 0x46dc00). The loader (0x46f680)
    // premultiplies the tile's RGB by its alpha (pmulhw of the 12-bit
    // expansions x * 257 >> 4); 0x46d970 reduces each 64x64 tile by 2x2
    // averages to one texel: C[t] its premultiplied RGB (sums >> 2), S[t]
    // 255 minus its alpha ((sum + 2) >> 2).
    if (alphaDetail_ && !colorRgb_.empty() && tile > 0 && (tile & (tile - 1)) == 0) {
        auto x12 = [](int v) { return (v * 257) >> 4; };
        std::vector<std::array<int, 3>> meanC(static_cast<size_t>(tiles));
        std::vector<int> meanS(static_cast<size_t>(tiles));
        for (int t = 0; t < tiles; ++t) {
            const size_t at = size_t(t) * size_t(tile) * size_t(tile);
            std::vector<std::array<int, 3>> c(size_t(tile) * size_t(tile));
            std::vector<int> a(size_t(tile) * size_t(tile));
            for (size_t i = 0; i < c.size(); ++i) {
                const uint8_t* p = &dcRgba[(at + i) * 4];
                a[i] = p[3];
                for (int k = 0; k < 3; ++k) c[i][size_t(k)] = (x12(p[k]) * x12(p[3])) >> 16;
            }
            for (int w = tile; w > 1; w /= 2) {
                const int h = w / 2;
                std::vector<std::array<int, 3>> c2(size_t(h) * size_t(h));
                std::vector<int> a2(size_t(h) * size_t(h));
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < h; ++x) {
                        const size_t i0 = size_t(2 * y) * size_t(w) + size_t(2 * x), i1 = i0 + 1, i2 = i0 + size_t(w), i3 = i2 + 1;
                        const size_t o = size_t(y) * size_t(h) + size_t(x);
                        for (int k = 0; k < 3; ++k)
                            c2[o][size_t(k)] = std::min(255, (c[i0][size_t(k)] + c[i1][size_t(k)] + c[i2][size_t(k)] + c[i3][size_t(k)]) >> 2);
                        a2[o] = (a[i0] + a[i1] + a[i2] + a[i3] + 2) >> 2;
                    }
                c.swap(c2);
                a.swap(a2);
            }
            meanC[size_t(t)] = c[0];
            meanS[size_t(t)] = 255 - a[0];
        }
        std::vector<uint8_t> far(colorRgb_.size());
        for (size_t i = 0; i < size_t(size_) * size_t(size_); ++i) {
            const int t = std::min(int(i < detail.pixels.size() ? detail.pixels[i] : 0), tiles - 1);
            const int s = x12(meanS[size_t(t)]);
            for (int k = 0; k < 3; ++k) {
                const int v = ((x12(colorRgb_[i * 3 + size_t(k)]) * s) >> 16) + meanC[size_t(t)][size_t(k)];
                far[i * 3 + size_t(k)] = uint8_t(std::clamp(v, 0, 255));
            }
        }
        texColorFar_ = makeTex2D(size_, size_, GL_RGB8, GL_RGB, far.data(), true, true);
    }
    texDetailColor_ = makeTexArray(tile, tile, tiles, GL_RGBA8, GL_RGBA, dcRgba.data());
    texDetailShade_ = makeTexArray(tile, tile, tiles, GL_RG8, GL_RG, dsRg.data());
    if (!trueColor) {
        // Delta Force's max map 0xbd52d4 (0x47d9e0): each tile's highest
        // detail elevation (the 1x1 level of 0x47d0a0 / 0x47d2f0, a max per
        // 2x2), then per cell the highest of its four corners, each its
        // height plus ceil((max - 128) / 16) steps by its own tile, at most
        // 255. Without detail elevations the corners are their heights.
        std::array<int, 256> tileMax{};
        for (size_t t = 0; t < 256 && !detailElev_.empty(); ++t)
            for (size_t k = 0; k < 256; ++k) tileMax[t] = std::max<int>(tileMax[t], detailElev_[t * 256 + k]);
        auto corner = [&](int x, int z) {
            const size_t c = size_t((z + size_) % size_) * size_t(size_) + size_t((x + size_) % size_);
            const int e = detailMap_.empty() ? 0 : tileMax[detailMap_[c]];
            return int(heights_[c]) + (((e < 0x80 ? 0 : e - 0x80) + 0xf) >> 4);
        };
        sightMax_.resize(size_t(size_) * size_t(size_));
        for (int z = 0; z < size_; ++z)
            for (int x = 0; x < size_; ++x)
                sightMax_[size_t(z) * size_t(size_) + size_t(x)] =
                    uint8_t(std::min(0xff, std::max({corner(x, z), corner(x + 1, z), corner(x, z + 1), corner(x + 1, z + 1)})));
    }
    if (alphaDetail_) {
        // Delta Force 2 (GOG Df2.exe 0x46d970): the detail elevation's
        // pyramid takes the maximum of each 2x2 block (the near bands sample
        // the level of their own spacing, 0x471c00); the shade stays neutral.
        std::vector<uint8_t> lv = dsRg;
        for (int w = tile, level = 1; w > 1; w /= 2, ++level) {
            const int h = w / 2;
            std::vector<uint8_t> next(size_t(h) * h * tiles * 2);
            for (int t = 0; t < tiles; ++t)
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < h; ++x) {
                        auto at = [&](int xx, int yy) { return lv[((size_t(t) * w + yy) * w + xx) * 2 + 1]; };
                        uint8_t* o = &next[((size_t(t) * h + y) * h + x) * 2];
                        o[0] = 128;
                        o[1] = std::max(std::max(at(2 * x, 2 * y), at(2 * x + 1, 2 * y)), std::max(at(2 * x, 2 * y + 1), at(2 * x + 1, 2 * y + 1)));
                    }
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, 0, h, h, tiles, GL_RG, GL_UNSIGNED_BYTE, next.data());
            lv.swap(next);
        }
    }

    // Sky palette: entries 0..15 the gradient from the horizon up (entry 0 is
    // also the fog colour), entry 16 the cloud/highlight colour (DF.EXE 0x433880).
    IndexedImage skyPal;
    for (int i = 0; i < 17; ++i) skyColors_[i] = {170, 200, 230};
    if (loadImage(vfs, desc_.skyPalette, skyPal) && skyPal.hasPalette)
        for (int i = 0; i < 17; ++i) skyColors_[i] = skyPal.palette[i];
    else {
        // Delta Force 2: a 16x257 TGA whose column 0 is a 256-step gradient
        // (row 256 the horizon = entry 0, row 1 the zenith = entry 255) and
        // row 0 the cloud colour; all 256 entries are drawn (0x467700).
        RgbaImage t;
        std::vector<uint8_t> raw;
        if (vfs.read(withExt(desc_.skyPalette, ".TGA"), raw) && decodeRgbaImage(raw, t) && t.height >= 257) {
            auto px = [&](int row) {
                const uint8_t* c = &t.rgba[size_t(row) * size_t(t.width) * 4];
                return Rgb{c[0], c[1], c[2]};
            };
            for (int i = 0; i < 16; ++i) skyColors_[i] = px(256 - i * 16);
            skyColors_[16] = px(0);
            std::vector<uint8_t> grad(256 * 3);
            for (int i = 0; i < 256; ++i) {
                const Rgb c = px(256 - i);
                grad[size_t(i) * 3] = c.r;
                grad[size_t(i) * 3 + 1] = c.g;
                grad[size_t(i) * 3 + 2] = c.b;
            }
            texSkyGrad_ = makeTex2D(256, 1, GL_RGB8, GL_RGB, grad.data(), false, false);
        }
    }
    if (gen2_ && !texSkyGrad_) {
        // No gradient file: entry 0 everywhere.
        const uint8_t c[3] = {skyColors_[0].r, skyColors_[0].g, skyColors_[0].b};
        std::vector<uint8_t> grad(256 * 3);
        for (int i = 0; i < 256 * 3; ++i) grad[size_t(i)] = c[i % 3];
        texSkyGrad_ = makeTex2D(256, 1, GL_RGB8, GL_RGB, grad.data(), false, false);
    }
    horizon_ = skyColors_[0];

    texSkyPal_ = makeTex2D(256, 1, GL_RGB8, GL_RGB, lut.data(), false, false);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    if (!gen2_) {
        texDisplayLut_ = makeTex2D(256, 1, GL_RGB8, GL_RGB, display.data(), false, false);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    }

    IndexedImage clouds;
    if (!loadImage(vfs, desc_.skyMap, clouds)) {
        clouds.width = clouds.height = 1;
        clouds.pixels = {127};
    }
    if (gen2_) {
        texClouds_ = makeTex2D(clouds.width, clouds.height, GL_R8, GL_RED, clouds.pixels.data(), true, true);
    } else {
        // GOG Df.exe 0x47b590: v < 0x80 -> 0, else (v - 0x80) >> 1 with 0x3f
        // made 0x3e; levels 1..8 by 0x47d920 (0x47dd10, [0xbd5198] & 1).
        std::vector<uint8_t> sky(clouds.pixels.size());
        for (size_t i = 0; i < sky.size(); ++i) {
            const int v = clouds.pixels[i];
            int c = v < 0x80 ? 0 : ((v + 0x80) & 0xff) >> 1;
            if (c == 0x3f) c = 0x3e;
            sky[i] = uint8_t(c);
        }
        texClouds_ = makeIndexMips(clouds.width, clouds.height, sky, 8, true);
    }

    // Water (DF.EXE 0x431b3e, 0x431d04): none if water_height is 0 or the
    // palette is missing; the gradient T from water palette entries 0..15.
    IndexedImage water, waterPal;
    std::vector<uint8_t> df1WaterGrad;   // Delta Force: the water ramp, for the map picture
    hasWater_ = false;
    if (gen2_ && desc_.waterHeight != 0) {
        // Delta Force 2 (DF2.EXE 0x465390..0x4667d0): water_map is a 256-wide
        // PCX coloured by its own palette, graded like the terrain; no
        // water_palette. If it does not load the map has no water at all
        // (level 0; RIPPLE3, named by DMDG3 and DMS2, is not on the disc).
        if (loadImage(vfs, desc_.waterMap, water) && water.width == 256 && water.hasPalette) {
            auto graded = [&](const Rgb& c) {
                const float y = (c.r + 2.f * c.g + c.b) / 4.f;
                const float in[3] = {float(c.r), float(c.g), float(c.b)};
                Rgb out;
                uint8_t* o = &out.r;
                for (int ch = 0; ch < 3; ++ch) {
                    const float v = std::clamp(y + (in[ch] - y) * desc_.saturation / 128.f, 0.f, 255.f);
                    o[ch] = lut[size_t(int(v)) * 3 + size_t(ch)];
                }
                return out;
            };
            std::vector<uint8_t> rgb(water.pixels.size() * 3);
            for (size_t i = 0; i < water.pixels.size(); ++i) {
                const Rgb c = graded(water.palette[water.pixels[i]]);
                rgb[i * 3] = c.r;
                rgb[i * 3 + 1] = c.g;
                rgb[i * 3 + 2] = c.b;
            }
            texWater_ = makeTex2D(water.width, water.height, GL_RGB8, GL_RGB, rgb.data(), false, true);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            waterColor_ = graded(water.palette[255]);
            hasWater_ = true;
        } else {
            desc_.waterHeight = 0;
        }
    } else if (desc_.waterHeight != 0 && loadImage(vfs, desc_.waterMap, water) && loadImage(vfs, desc_.waterPalette, waterPal) &&
        waterPal.hasPalette) {
        // GOG Df.exe 0x47b590: the water map >> 2 (0..63); levels 1..7 by
        // 0x47d920 (0x47dd10, [0xbd5198] & 2).
        std::vector<uint8_t> w(water.pixels.size());
        for (size_t i = 0; i < w.size(); ++i) w[i] = uint8_t(water.pixels[i] >> 2);
        texWater_ = makeIndexMips(water.width, water.height, w, 7, false);
        std::vector<uint8_t> grad(256 * 3);
        // The ramp is mixed from the raw water palette and passes only
        // through the df.cfg table (GOG Df.exe 0x47d080 -> 0x480ca0).
        auto post = [&](float c[3], uint8_t* out) {
            for (int ch = 0; ch < 3; ++ch) out[ch] = display[size_t(std::clamp(int(c[ch]), 0, 255)) * 3 + size_t(ch)];
        };
        for (int k = 0; k < 15; ++k)
            for (int j = 0; j < 16; ++j) {
                const Rgb& a = waterPal.palette[size_t(k)];
                const Rgb& b = waterPal.palette[size_t(k + 1)];
                float c[3] = {a.r + (b.r - a.r) * j / 16.f, a.g + (b.g - a.g) * j / 16.f, a.b + (b.b - a.b) * j / 16.f};
                post(c, &grad[size_t(16 * (k + 1) + j) * 3]);
            }
        for (int i = 0; i < 16; ++i) std::copy(&grad[16 * 3], &grad[16 * 3] + 3, &grad[size_t(i) * 3]);
        texWaterGrad_ = makeTex2D(256, 1, GL_RGB8, GL_RGB, grad.data(), false, false);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        hasWater_ = true;
        df1WaterGrad.swap(grad);
    }

    if (!gen2_) {
        // Delta Force's command map and GPS inset picture (GOG Df.exe
        // 0x48e070): the colour map point-sampled (each pixel the nearest
        // texel, no filtering). With water ([0xbd5248] != 0) a cell whose
        // elevation byte is at or below min(255, level >> 14) is drawn
        // through the water clut [0xbf67f8] (0x47d080) by its colour's
        // luminance (VoxPal_32 byte 3, (R + 2G + B) >> 2 after the df.cfg
        // table): row 0 of the clut, the water ramp at
        // (0x10000 - opacity) * lum >> 16.
        std::vector<uint8_t> rgb = toRgb(color, gradedPalette(color.palette));
        if (!df1WaterGrad.empty() && desc_.waterHeight != 0) {
            const int level = std::min(255, int(desc_.waterHeight));
            const int keep = 0x10000 - int(desc_.waterOpacity * 65536.f);
            for (size_t i = 0; i < color.pixels.size(); ++i) {
                if (elev.pixels[i] > level) continue;
                const uint8_t* c = &rgb[i * 3];
                const int lum = (display[size_t(c[0]) * 3] + 2 * display[size_t(c[1]) * 3 + 1] + display[size_t(c[2]) * 3 + 2]) >> 2;
                const size_t k = size_t(std::clamp((keep * lum) >> 16, 0, 255)) * 3;
                std::copy(&df1WaterGrad[k], &df1WaterGrad[k] + 3, &rgb[i * 3]);
            }
        }
        texMap_ = makeTex2D(size_, size_, GL_RGB8, GL_RGB, rgb.data(), false, false);
    }

    if (gen2_ && !colorRgb_.empty()) {
        // The radar's picture: VColor level 0 (graded), water cells averaged
        // with the plain water colour ((a>>1 & 0x7f7f7f7f) + (b>>1 & ..)).
        std::vector<uint8_t> radar(colorRgb_.size());
        for (size_t i = 0; i < colorRgb_.size() / 3; ++i) {
            const uint8_t* c = &colorRgb_[i * 3];
            const float y = (c[0] + 2.f * c[1] + c[2]) / 4.f;
            const uint8_t* w = &waterColor_.r;
            const bool wet = hasWater_ && heights_[i] <= desc_.waterHeight;
            for (int ch = 0; ch < 3; ++ch) {
                const float v = std::clamp(y + (c[ch] - y) * desc_.saturation / 128.f, 0.f, 255.f);
                uint8_t g = lut[size_t(int(v)) * 3 + size_t(ch)];
                if (wet) g = uint8_t((g >> 1) + (w[ch] >> 1));
                radar[i * 3 + size_t(ch)] = g;
            }
        }
        texRadar_ = makeTex2D(size_, size_, GL_RGB8, GL_RGB, radar.data(), false, false);
        colorRgb_.clear();
        colorRgb_.shrink_to_fit();
    }
    // Delta Force only: the terrain stops after the last ring (a discard
    // compiled in only there, so Delta Force 2's shader stays as it was).
    const std::string terrainFS = std::string("#version 330 core\n") + (gen2_ ? "" : "#define RING_END\n") + kPost + kTerrainFSBody;
    const std::string skyFS = std::string("#version 330 core\n") + kPost + kSkyFSBody;
    progTerrain_ = compileProgram(kTerrainVS, terrainFS.c_str());
    progSky_ = compileProgram(kSkyVS, skyFS.c_str());
    progWater_ = compileProgram(kWaterVS, kWaterFS);
    progFog_ = compileProgram(kSkyVS, kFogFS);
    if (!progTerrain_ || !progSky_ || !progWater_ || !progFog_) return fail("shader compilation failed (see stderr)");

    glUseProgram(progTerrain_);
    setInt(progTerrain_, "uHeight", 0);
    setInt(progTerrain_, "uColor", 1);
    setInt(progTerrain_, "uDetailIdx", 2);
    setInt(progTerrain_, "uDetailColor", 3);
    setInt(progTerrain_, "uDetailShade", 4);
    setInt(progTerrain_, "uAlphaDetail", alphaDetail_ ? 1 : 0);
    setInt(progTerrain_, "uDf2", gen2_ ? 1 : 0);
    setInt(progTerrain_, "uLut", 5);
    setInt(progTerrain_, "uColorFar", 6);
    setF(progTerrain_, "uTiles", float(tiles));
    setF(progTerrain_, "uSat", saturationFactor());
    glUseProgram(progSky_);
    setInt(progSky_, "uLut", 0);
    setInt(progSky_, "uClouds", 1);
    setInt(progSky_, "uGrad", 2);
    setF(progSky_, "uSat", saturationFactor());
    glUseProgram(progWater_);
    setInt(progWater_, "uWaterIdx", 0);
    setInt(progWater_, "uGradient", 1);
    setInt(progWater_, "uScene", 2);
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
    const int res[5] = {64, 32, 16, 8, 256};   // 4: 1/4 m, for the detail bumps around the camera
    for (int i = 0; i < 5; ++i) lods_[i] = makeGrid(res[i]);
    // (OpenDF) One instance buffer of chunk origins for every LOD: the
    // chunks of a LOD go in one instanced draw (draw calls are what cost).
    glGenBuffers(1, &instVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    for (auto& m : lods_) {
        glBindVertexArray(m.vao);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glVertexAttribDivisor(1, 1);
    }
    glBindVertexArray(0);

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

float Terrain::sightHeight(float x, float z) const {
    float h = heightAt(x, z);
    if (detailMap_.empty()) return h;
    const float cx = x / scale.cellSize, cz = z / scale.cellSize;
    const float fx = std::floor(cx), fz = std::floor(cz);
    const long ix = ((long(fx) % size_) + size_) % size_, iz = ((long(fz) % size_) + size_) % size_;
    const int sx = std::min(15, int((cx - fx) * 16.f)), sz = std::min(15, int((cz - fz) * 16.f));
    const int e = detailElev_[size_t(detailMap_[size_t(iz) * size_t(size_) + size_t(ix)]) * 256 + size_t(sz * 16 + sx)];
    if (e >= 0x80) h += float(e - 0x80) / 64.f;
    return h;
}

float Terrain::sightMax(float x, float z) const {
    if (sightMax_.empty()) return 0;
    long ix = long(std::floor(x / scale.cellSize)), iz = long(std::floor(z / scale.cellSize));
    ix = ((ix % size_) + size_) % size_;
    iz = ((iz % size_) + size_) % size_;
    return float(sightMax_[size_t(iz) * size_t(size_) + size_t(ix)]) * scale.heightScale;
}

void Terrain::df2Gradient(float x, float z, float& gx, float& gz) const {
    gx = gz = 0;
    if (size_ == 0 || !gen2_) return;
    // GOG's cell is (x >> 16, y >> 16) with y = -z; heightAt's corners put
    // GOG grid row n at OpenDF row -n, so the cell's row is ceil(z).
    const long ix = long(std::floor(x / scale.cellSize)), iz = long(std::ceil(z / scale.cellSize));
    auto H = [&](long cx, long cz) {
        cx = ((cx % size_) + size_) % size_;
        cz = ((cz % size_) + size_) % size_;
        const size_t i = size_t(cz) * size_t(size_) + size_t(cx);
        return long(std::lround(heights_[i] * 256.f)) + (i < detailTop_.size() ? 2L * detailTop_[i] : 0L);
    };
    // (H[+2] - H[-2]) * 0x20 in 16.16; heights are 8.8 words of 0.5 m steps.
    gx = float((H(ix + 2, iz) - H(ix - 2, iz)) * 0x20) / 65536.f;
    gz = float((H(ix, iz + 2) - H(ix, iz - 2)) * 0x20) / 65536.f;
}

float Terrain::cornerHeight(float x, float z) const {
    if (size_ == 0) return 0;
    // GOG row n is OpenDF row -n (see df2Gradient), so the cell's row is ceil(z).
    long ix = long(std::floor(x / scale.cellSize)), iz = long(std::ceil(z / scale.cellSize));
    ix = ((ix % size_) + size_) % size_;
    iz = ((iz % size_) + size_) % size_;
    return float(heights_[size_t(iz) * size_ + size_t(ix)]) * scale.heightScale;
}

void Terrain::df1Gradient(float x, float z, float& gx, float& gz) const {
    gx = gz = 0;
    if (size_ == 0) return;
    const long ix = long(std::floor(x / scale.cellSize)), iz = long(std::ceil(z / scale.cellSize));
    auto H = [&](long cx, long cz) {
        cx = ((cx % size_) + size_) % size_;
        cz = ((cz % size_) + size_) % size_;
        return heights_[size_t(cz) * size_t(size_) + size_t(cx)];
    };
    gx = (H(ix + 2, iz) - H(ix - 2, iz)) * 0x1000 / 65536.f;
    gz = (H(ix, iz + 2) - H(ix, iz - 2)) * 0x1000 / 65536.f;
}

float Terrain::cellHeight(float x, float z) const {
    if (size_ == 0) return 0;
    long ix = long(std::floor(x / scale.cellSize)), iz = long(std::floor(z / scale.cellSize));
    ix = ((ix % size_) + size_) % size_;
    iz = ((iz % size_) + size_) % size_;
    return float(heights_[size_t(iz) * size_ + size_t(ix)]) * scale.heightScale;
}

void Terrain::drawWaterTint(Vec3 eye) {
    // Delta Force 2 (GOG Df2.exe 0x44f8d0 -> 0x44fcb0(view, [0x5386e8],
    // 0x80); D3D 0x44fa80 -> 0x44fda0): the camera at or below the level,
    // the whole 3D view (the weapon, the fog and the weather included) is
    // blended 50 % toward the water colour, as the frame's last step.
    if (!gen2_ || !hasWater_ || eye.y > desc_.waterHeight * scale.heightScale) return;
    glUseProgram(progWater_);
    glUniform1i(glGetUniformLocation(progWater_, "uDf2"), 1);
    glUniform1i(glGetUniformLocation(progWater_, "uTint"), 1);
    glUniform1i(glGetUniformLocation(progWater_, "uBelow"), 1);
    setV3(progWater_, "uWaterColor", {waterColor_.r / 255.f, waterColor_.g / 255.f, waterColor_.b / 255.f});
    glBindVertexArray(waterVao_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void Terrain::drawWater(const Mat4& view, const Mat4& proj, Vec3 eye, float ticks) {
    if (!hasWater_) return;
    const float level = desc_.waterHeight * scale.heightScale;
    if (gen2_) {
        // Delta Force 2: one plane at the level out to min(2^fog_level m,
        // 2047 m) (0x4670b7..0x4670d4; 2047 m with fog off), depth-tested
        // and writing depth; seen from below as well (the under-water tint
        // is drawWaterTint, at the end of the frame). The
        // software water (0x466b60) writes its depth: each span (0x485e17)
        // has one z, the row's view depth in 16.16 m (0x466de0, up to
        // 0x7ff0000), stored where nearer, so the fog pass 0x4485b0 fogs it
        // like the ground (READ). The fog quad 0x467568..0x46760e belongs to
        // the hardware water (0x4670a0.., drawn through the hw3d table
        // [0xa7b6b0] of HW_D3D.DLL) and has no software counterpart.
        glUseProgram(progWater_);
        glUniform1i(glGetUniformLocation(progWater_, "uDf2"), 1);
        glUniform1i(glGetUniformLocation(progWater_, "uTint"), 0);
        glUniform1i(glGetUniformLocation(progWater_, "uBelow"), 0);
        setF(progWater_, "uPhase", std::fmod(ticks * 2.f, 1024.f));
        setV3(progWater_, "uWaterColor", {waterColor_.r / 255.f, waterColor_.g / 255.f, waterColor_.b / 255.f});
        setV3(progWater_, "uEye", eye);
        setM4(progWater_, "uViewProj", proj * view);
        setF(progWater_, "uLevel", level);
        setF(progWater_, "uExtent", fogLevel() ? std::min(fogDistance(), 2047.f) : 2047.f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texWater_);
        glBindVertexArray(waterVao_);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
        return;
    }
    // Under water nothing here: drawUnderwater() after the objects
    // (GOG Df.exe 0x48d570, called near the end of 0x485610).
    if (eye.y <= level) return;
    df1WaterSetup(eye, ticks);
    // The surface out to 1024 m of view depth (GOG Df.exe 0x48d040: a span
    // stops past 0x3feffff), only where it is in front (depth test); the quad
    // reaches the diagonal corners.
    glUniform1i(glGetUniformLocation(progWater_, "uBelow"), 0);
    setM4(progWater_, "uViewProj", proj * view);
    setF(progWater_, "uLevel", level);
    setF(progWater_, "uExtent", 1449.f);
    setF(progWater_, "uMaxDepth", float(0x3feffff) / 65536.f);
    setV3(progWater_, "uFwd", Vec3{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)});
    glDrawArrays(GL_TRIANGLES, 0, 6);
    setF(progWater_, "uMaxDepth", 0.f);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void Terrain::df1WaterSetup(Vec3 eye, float phaseTicks) {
    // What is already drawn: copied so the water can read its luminance.
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    if (!texScene_ || sceneW_ != vp[2] || sceneH_ != vp[3]) {
        if (texScene_) glDeleteTextures(1, &texScene_);
        texScene_ = makeTex2D(vp[2], vp[3], GL_RGB8, GL_RGB, nullptr, false, false);
        sceneW_ = vp[2];
        sceneH_ = vp[3];
    }
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, texScene_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
    glUseProgram(progWater_);
    glUniform1i(glGetUniformLocation(progWater_, "uDf2"), 0);
    glUniform4f(glGetUniformLocation(progWater_, "uViewport"), float(vp[0]), float(vp[1]), float(vp[2]), float(vp[3]));
    setF(progWater_, "uOpacity", desc_.waterOpacity);
    setF(progWater_, "uWaterF", 320.f * ringScale);
    // Phase: tick << 23 in table units of 2^22 -> 2 entries per tick.
    setF(progWater_, "uPhase", std::fmod(phaseTicks * 2.f, 1024.f));
    setV3(progWater_, "uEye", eye);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texWater_);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, texWaterGrad_);
    glBindVertexArray(waterVao_);
    glDepthMask(GL_FALSE);
}

bool Terrain::df1UnderWater(Vec3 eye) const { return !gen2_ && hasWater_ && eye.y <= waterLevel(); }

void Terrain::drawUnderwater(const Mat4& view, const Mat4& projNear, const Mat4& projFar, Vec3 eye, float phaseTicks) {
    // GOG Df.exe 0x48d570 (view z <= the water level): every pixel through
    // the water table with r = 0 (0x43c500), except where the surface is in
    // front: there the ripple texel as above water, mapped from below with
    // the phase of the 20 Hz timer ([0x491014] << 0x17), out to 1023 m of
    // view depth (0x3feffff). Both read the colours as drawn before it.
    if (!df1UnderWater(eye)) return;
    df1WaterSetup(eye, phaseTicks);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glUniform1i(glGetUniformLocation(progWater_, "uBelow"), 1);
    setM4(progWater_, "uViewProj", Mat4{});
    setF(progWater_, "uLevel", 0.f);
    setF(progWater_, "uExtent", 1.f);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glUniform1i(glGetUniformLocation(progWater_, "uBelow"), 0);
    setV3(progWater_, "uEye", eye);
    setF(progWater_, "uLevel", waterLevel());
    setF(progWater_, "uExtent", 1449.f);
    setF(progWater_, "uMaxDepth", float(0x3feffff) / 65536.f);
    setV3(progWater_, "uFwd", Vec3{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)});
    // The caller's two depth ranges (far 0.5..1, near 0..0.5).
    for (int range = 0; range < 2; ++range) {
        glDepthRange(range ? 0.0 : 0.5, range ? 0.5 : 1.0);
        setM4(progWater_, "uViewProj", (range ? projNear : projFar) * view);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glDepthRange(0.0, 1.0);
    setF(progWater_, "uMaxDepth", 0.f);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void Terrain::tintUnderwaterOver(Vec3 eye) {
    // (OpenDF) What was drawn after drawUnderwater() with depth cleared first
    // (the first-person weapon): GOG Df.exe draws the gun before 0x48d570,
    // so it is tinted with r = 0 too. Only where depth was written since.
    if (!df1UnderWater(eye)) return;
    df1WaterSetup(eye, 0.f);
    glUniform1i(glGetUniformLocation(progWater_, "uBelow"), 1);
    glUniform1f(glGetUniformLocation(progWater_, "uQuadDepth"), 0.99999f);
    setM4(progWater_, "uViewProj", Mat4{});
    setF(progWater_, "uLevel", 0.f);
    setF(progWater_, "uExtent", 1.f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDepthFunc(GL_LESS);
    glUniform1f(glGetUniformLocation(progWater_, "uQuadDepth"), 0.f);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

int Terrain::surfaceAt(float x, float z) const {
    if (size_ == 0 || surface_.empty()) return 0;
    const long ix = ((long(std::floor(x / scale.cellSize)) % size_) + size_) % size_;
    const long iz = ((long(std::floor(z / scale.cellSize)) % size_) + size_) % size_;
    return surface_[size_t(iz) * size_t(size_) + size_t(ix)];
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
    const float cell = scale.cellSize;
    auto rgb = [](Rgb c) { return Vec3{c.r / 255.f, c.g / 255.f, c.b / 255.f}; };

    // Sky (behind everything). Delta Force 2 with fog on draws no gradient
    // and no clouds (0x46766e..0x4676a9): it only marks the rows as far
    // away, and the fog pass turns them into the fog colour.
    if (!fogLevel() && pass != Pass::Near) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(progSky_);
    setM4(progSky_, "uInvRot", inverseRotationOnly(view));
    glUniform2f(glGetUniformLocation(progSky_, "uTan"), 1.f / proj.m[0], 1.f / proj.m[5]);
    setV3(progSky_, "uEye", eye);
    setF(progSky_, "uCloudH", desc_.skyHeight);   // metres (DF.EXE 0x43b5f4)
    setF(progSky_, "uHorizon", desc_.horizon);
    glUniform1i(glGetUniformLocation(progSky_, "uDf2"), gen2_ ? 1 : 0);
    {
        const Vec3 fwd{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)};
        const float pitch = std::asin(std::clamp(fwd.y, -1.f, 1.f));
        setF(progSky_, "uPitchTan", std::tan(pitch));
        if (gen2_) {
            // DF2.EXE 0x467700: W = the view's width in the original's
            // pixels at this focal length and FOV; focal = trunc(W/2 /
            // tan(fov/2) + 0.5) (0x404742); the horizon row = centre +
            // ((tan(pitch)·65536 · focal) >> 16) + 5, or centre + 0x800 at 89
            // degrees and more, none under -45 (0x46772e..0x46779c); step s =
            // ((((fov << 16) / W) << 16) / 0x3333 · 0x20000 >> 16) · horizon
            // >> 16 per row (0x4677bd..0x467816).
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            const double fpx = double(proj.m[5]) * vp[3] / 2.0;
            const double halfFov = df2FovDeg * 3.14159265358979 / 360.0;
            const int64_t W = std::max<int64_t>(1, std::llround(2.0 * fpx * std::tan(halfFov)));
            const int64_t focal = int64_t(double(W) / 2.0 / std::tan(halfFov) + 0.5);
            const double deg = pitch * 180.0 / 3.14159265358979;
            int64_t horizonRow = vp[3] / 2;
            if (deg >= 89.0) horizonRow += 0x800;
            else horizonRow += ((int64_t(std::tan(double(pitch)) * 65536.0) * focal) >> 16) + 5;
            int64_t st = (int64_t(df2FovDeg) << 16) / W;
            st = (st << 16) / 0x3333;
            st = (st * 0x20000) >> 16;
            st = (st * int64_t(desc_.horizon * 65536.f)) >> 16;
            st = std::max<int64_t>(1, st);
            glUniform1i(glGetUniformLocation(progSky_, "uNoSky"), deg < -45.0 ? 1 : 0);
            glUniform1i(glGetUniformLocation(progSky_, "uHorizonRow"), int(std::clamp<int64_t>(horizonRow, -0x10000, 0x10000)));
            glUniform1i(glGetUniformLocation(progSky_, "uSkyStep"), int(std::min<int64_t>(st, 0x7fffffff)));
            glUniform1i(glGetUniformLocation(progSky_, "uKMax"), int((int64_t(256) << 16) / st + 1));
            glUniform4f(glGetUniformLocation(progSky_, "uViewport"), float(vp[0]), float(vp[1]), float(vp[2]), float(vp[3]));
        }
    }
    if (!gen2_) {
        // GOG Df.exe 0x48c990: s = ((((fov << 16) / W) << 16) / 0x2400 ·
        // 0x20000 >> 16) · horizon >> 16 per row (2·horizon at 90 / 640).
        int64_t st = (int64_t(df1SkyFov) << 16) / std::max(1, df1SkyW);
        st = (st << 16) / 0x2400;
        st = (st * 0x20000) >> 16;
        st = (st * int64_t(desc_.horizon * 65536.f)) >> 16;
        setF(progSky_, "uSkyStepF", float(st) / 65536.f);
        setF(progSky_, "uSkyF", df1SkyF);
        setF(progSky_, "uCloudF", 320.f * ringScale);   // view +0x54, the mode's pixels
    }
    // Delta Force: 0x485610 -> 0x489980 with the step count [0x67dbf4].
    setF(progSky_, "uTime", !gen2_ && skyTicks >= 0.f ? skyTicks : float(SDL_GetTicks()) * 60.f / 1000.f);
    for (int i = 0; i < 17; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "uPal[%d]", i);
        setV3(progSky_, name, rgb(skyColors_[i]));
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, lutTexture());
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, texClouds_);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, texSkyGrad_);
    glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    }

    // Terrain around the camera; the map repeats endlessly. Delta Force
    // (ring table GOG Df.exe 0x489440, radii in 1/16 m, F the focal length):
    // near rings 0..4 end at 8·F/p1 (F / (2·p1) m, 72.7 m at F 320 and p1
    // 2.2); far ring k ends at 16·2^k·F/p2 (2^k·F/p2 m), and 0x48dc70 draws
    // rings 0..10 only (5 near, far k 0..5), so nothing past 32·F/p2 m
    // (4654 m). Delta Force 2 (GOG Df2.exe 0x4722f0 -> 0x472440): near
    // bands k = 6..1 end at F / (2^k·p1) (the last at F / (2·p1), 80 m at
    // F 320 and p1 2), far bands k = 0..8 end at 2^k·F/p2 and sample colour
    // mip k, so the terrain reaches 256·F/p2 m (40960 m), cut at the fog
    // distance.
    const float focalF = 320.f * ringScale;
    const float nearEnd = focalF / (2.f * ringP1);
    const float farBase = focalF / (2.f * ringP2);
    const float ringsEnd = gen2_ ? 256.f * focalF / ringP2 : 32.f * focalF / ringP2;
    glUseProgram(progTerrain_);
    setM4(progTerrain_, "uViewProj", viewProj);
    setF(progTerrain_, "uCell", cell);
    setF(progTerrain_, "uHScale", scale.heightScale);
    setF(progTerrain_, "uMapSize", float(size_));
    setF(progTerrain_, "uSkirt", kSkirtDepth);
    setF(progTerrain_, "uNear", nearEnd);
    setF(progTerrain_, "uFarBase", farBase);
    setF(progTerrain_, "uKMax", gen2_ ? 8.f : 5.f);
    glUniform1i(glGetUniformLocation(progTerrain_, "uHeightMips"), gen2_ ? 0 : 1);
    setF(progTerrain_, "uBand0", focalF / (64.f * ringP1));
    setF(progTerrain_, "uEnd", gen2_ ? 1e9f : ringsEnd);
    setV3(progTerrain_, "uEye", eye);
    setV3(progTerrain_, "uFog", fog);
    setV3(progTerrain_, "uCloudCol", rgb(skyColors_[16]));
    GLuint units[7] = {texHeight_, texColor_, texDetailIdx_, texDetailColor_, texDetailShade_, lutTexture(),
                       texColorFar_ ? texColorFar_ : texColor_};
    for (int i = 0; i < 7; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(i == 3 || i == 4 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, units[i]);
    }

    // Delta Force 2 with fog: bands starting past 2^fog_level m are not
    // drawn (0x4678ed..0x467905, 0x46793a..0x467956).
    const float terrainDistance = fogLevel() ? std::min(ringsEnd, fogDistance()) : ringsEnd;
    const Vec3 fwd{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)};
    const GLint locScale = glGetUniformLocation(progTerrain_, "uChunkScale");
    const GLint locBumps = glGetUniformLocation(progTerrain_, "uBumps");
    const float chunkWorld = kChunkCells * cell, blockCells = kChunkCells * 8.f, blockWorld = blockCells * cell;
    const int bx = int(std::floor(eye.x / blockWorld)), bz = int(std::floor(eye.z / blockWorld));
    // (OpenDF) The view's four side planes (rows 3 +- 0, 3 +- 1 of proj ·
    // view): chunks wholly outside are not drawn. Only a speed-up: what is
    // drawn on screen is unchanged. It matters in the scope view, whose
    // rings reach out with its focal length while its view is narrow.
    float planes[4][4];
    for (int p = 0; p < 4; ++p) {
        const int row = p / 2;
        const float sgn = (p & 1) ? -1.f : 1.f;
        float len = 0;
        for (int c = 0; c < 4; ++c) {
            planes[p][c] = viewProj.at(3, c) + sgn * viewProj.at(row, c);
            if (c < 3) len += planes[p][c] * planes[p][c];
        }
        len = std::sqrt(len);
        if (len > 0) for (float& v : planes[p]) v /= len;
    }
    const float midY = 128.f * scale.heightScale;
    auto visible = [&](float cx, float cz, float size) {
        const float radius = size * 0.75f + 256.f * scale.heightScale;
        const Vec3 d{cx - eye.x, 0, cz - eye.z};
        const float horiz = std::sqrt(d.x * d.x + d.z * d.z);
        if (horiz > terrainDistance + radius) return -1.f;
        if (d.x * fwd.x + d.z * fwd.z < -radius) return -1.f;
        // Two depth passes: a chunk wholly on the other side of the split
        // (its view depth within +-radius of the centre's) is clipped anyway.
        if (pass != Pass::All) {
            const float depth = d.x * fwd.x + (midY - eye.y) * fwd.y + d.z * fwd.z;
            if (pass == Pass::Far ? depth + radius < passSplit : depth - radius > passSplit * 1.01f) return -1.f;
        }
        for (const auto& pl : planes)
            if (pl[0] * cx + pl[1] * midY + pl[2] * cz + pl[3] < -radius) return -1.f;
        return horiz;
    };
    // Chunk origins per batch: near LODs 0..4 (scale 1, bumps), then far
    // LODs 1..3 (scale 8, no bumps), each drawn as one instanced call.
    std::vector<float> batch[8];
    // Near: 64-cell chunks within two 512-cell blocks of the camera's block.
    for (int zb = bz - 2; zb <= bz + 2; ++zb)
        for (int xb = bx - 2; xb <= bx + 2; ++xb)
            for (int cz = zb * 8; cz < zb * 8 + 8; ++cz)
                for (int cx = xb * 8; cx < xb * 8 + 8; ++cx) {
                    const float horiz = visible((cx + 0.5f) * chunkWorld, (cz + 0.5f) * chunkWorld, chunkWorld);
                    if (horiz < 0) continue;
                    const bool fine = horiz < nearEnd + chunkWorld;
                    const int lod = fine ? 4 : horiz < 160 ? 0 : horiz < 400 ? 1 : horiz < 800 ? 2 : 3;
                    batch[lod].insert(batch[lod].end(), {float(cx * kChunkCells), float(cz * kChunkCells)});
                }
    // Far: whole 512-cell blocks as one coarse chunk each.
    const int reach = int(std::ceil(terrainDistance / blockWorld)) + 1;
    for (int zb = bz - reach; zb <= bz + reach; ++zb)
        for (int xb = bx - reach; xb <= bx + reach; ++xb) {
            if (std::abs(zb - bz) <= 2 && std::abs(xb - bx) <= 2) continue;
            const float horiz = visible((xb + 0.5f) * blockWorld, (zb + 0.5f) * blockWorld, blockWorld);
            if (horiz < 0) continue;
            // Sample spacing like the original's far rings: 16 m to 3.2 km,
            // 32 m to 6.4 km, 64 m beyond (scaled).
            const int lod = horiz < 3200 * ringScale ? 1 : horiz < 6400 * ringScale ? 2 : 3;
            batch[4 + lod].insert(batch[4 + lod].end(), {float(xb) * blockCells, float(zb) * blockCells});
        }

    // One upload into a fresh buffer store (no wait for the GPU still
    // reading the last view's origins).
    std::vector<float>& all = instData_;
    all.clear();
    for (const auto& bt : batch) all.insert(all.end(), bt.begin(), bt.end());
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    glBufferData(GL_ARRAY_BUFFER, std::ptrdiff_t(all.size() * sizeof(float)), all.data(), GL_STREAM_DRAW);
    size_t offset = 0;
    for (int i = 0; i < 8; ++i) {
        if (batch[i].empty()) continue;
        const bool far = i >= 5;
        const Mesh& m = lods_[far ? i - 4 : i];
        glUniform1f(locScale, far ? 8.f : 1.f);
        glUniform1f(locBumps, far ? 0.f : 1.f);
        glBindVertexArray(m.vao);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<const void*>(offset * sizeof(float)));
        glDrawElementsInstanced(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr, GLsizei(batch[i].size() / 2));
        offset += batch[i].size();
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void Terrain::drawFog(const Mat4& proj, const Mat4* farProj) {
    if (!fogLevel() || !progFog_) return;
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    if (!texDepth_ || depthW_ != vp[2] || depthH_ != vp[3]) {
        if (texDepth_) glDeleteTextures(1, &texDepth_);
        glGenTextures(1, &texDepth_);
        glBindTexture(GL_TEXTURE_2D, texDepth_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, vp[2], vp[3], 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        depthW_ = vp[2];
        depthH_ = vp[3];
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texDepth_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
    glUseProgram(progFog_);
    setInt(progFog_, "uDepth", 0);
    glUniform4f(glGetUniformLocation(progFog_, "uViewport"), float(vp[0]), float(vp[1]), float(vp[2]), float(vp[3]));
    setV3(progFog_, "uFogColor", {desc_.fogColor[0] / 255.f, desc_.fogColor[1] / 255.f, desc_.fogColor[2] / 255.f});
    setF(progFog_, "uM10", proj.m[10]);
    setF(progFog_, "uM14", proj.m[14]);
    setF(progFog_, "uFarM10", farProj ? farProj->m[10] : proj.m[10]);
    setF(progFog_, "uFarM14", farProj ? farProj->m[14] : proj.m[14]);
    glUniform1i(glGetUniformLocation(progFog_, "uSplit"), farProj ? 1 : 0);
    setInt(progFog_, "uLevel", fogLevel());
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(0);
}

} // namespace df
