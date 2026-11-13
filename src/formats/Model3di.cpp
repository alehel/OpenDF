#include "Model3di.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace df {

namespace {

constexpr float kUnitsPerMetre = 256.f;

struct Reader {
    const std::vector<uint8_t>& d;
    bool ok(size_t off, size_t n) const { return off <= d.size() && n <= d.size() - off; }
    uint8_t u8(size_t o) const { return d[o]; }
    uint16_t u16(size_t o) const { return uint16_t(d[o] | (d[o + 1] << 8)); }
    int16_t i16(size_t o) const { return int16_t(u16(o)); }
    uint32_t u32(size_t o) const { return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24; }
    int32_t i32(size_t o) const { return int32_t(u32(o)); }
    std::string str(size_t o, size_t n) const {
        std::string s(reinterpret_cast<const char*>(&d[o]), n);
        s.resize(std::strlen(s.c_str()));
        return s;
    }
};

struct Layout { size_t lodHeader, mesh; bool volumes; };

bool layoutFor(int version, Layout& l) {
    switch (version) {
    case 2: l = {176, 88, false}; return true;
    case 3: case 4: l = {176, 104, false}; return true;
    case 5: case 6: l = {184, 112, true}; return true;
    default: return false;
    }
}

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool loadModel3di(const std::vector<uint8_t>& file, Model3di& m, std::string* error) {
    auto fail = [&](const char* msg) { if (error) *error = msg; return false; };
    Reader r{file};
    if (!r.ok(0, 0x80) || std::memcmp(file.data(), "3DI", 3) != 0) return fail("not a 3DI file");
    m = Model3di{};
    m.version = file[3];
    Layout layout;
    if (!layoutFor(m.version, layout)) return fail("unsupported 3DI version");
    m.name = r.str(4, 12);
    const uint32_t lodCount = r.u32(0x14);
    const uint32_t texCount = r.u32(0x7C);
    if (lodCount == 0 || lodCount > 16 || texCount > 1024) return fail("implausible 3DI header");

    // Textures: 36-byte header, pixels (frame 0 is used), 256 x RGBA palette.
    size_t p = 0x80;
    for (uint32_t i = 0; i < texCount; ++i) {
        if (!r.ok(p, 36)) return fail("truncated texture header");
        Model3diTexture t;
        t.name = r.str(p, 12);
        const uint32_t size = r.u32(p + 0x10);
        t.sequence = r.u8(p + 0x14);
        t.indexZeroTransparent = (r.u8(p + 0x16) & 0x20) != 0;
        t.width = r.u16(p + 0x18);
        t.height = r.u16(p + 0x1A);
        const size_t pixels = p + 36;
        const size_t palette = pixels + size;
        const size_t texels = size_t(t.width) * t.height;
        if (texels == 0 || size < texels || !r.ok(pixels, size) || !r.ok(palette, 1024)) return fail("bad texture");
        t.rgba.resize(texels * 4);
        for (size_t k = 0; k < texels; ++k) {
            const uint8_t idx = file[pixels + k];
            const uint8_t* c = &file[palette + size_t(idx) * 4];
            t.rgba[k * 4] = c[0];
            t.rgba[k * 4 + 1] = c[1];
            t.rgba[k * 4 + 2] = c[2];
            t.rgba[k * 4 + 3] = (t.indexZeroTransparent && idx == 0) ? 0 : 255;
        }
        // Transparent texels keep palette entry 0's colour (often magenta).
        // The original samples textures without filtering; OpenDF filters, so
        // give transparent texels their opaque neighbours' colour (a few
        // passes of dilation) to keep that colour from bleeding into edges.
        if (t.indexZeroTransparent) {
            const int w = t.width, h = t.height;
            std::vector<uint8_t> done(texels);
            for (size_t k = 0; k < texels; ++k) done[k] = t.rgba[k * 4 + 3] != 0;
            for (int pass = 0; pass < 8; ++pass) {
                std::vector<uint8_t> next = done;
                bool changed = false;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const size_t k = size_t(y) * w + x;
                        if (done[k]) continue;
                        int sum[3] = {}, n = 0;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int xx = x + dx, yy = y + dy;
                                if ((dx || dy) && xx >= 0 && yy >= 0 && xx < w && yy < h && done[size_t(yy) * w + xx]) {
                                    const size_t j = (size_t(yy) * w + xx) * 4;
                                    for (int c = 0; c < 3; ++c) sum[c] += t.rgba[j + c];
                                    ++n;
                                }
                            }
                        if (!n) continue;
                        for (int c = 0; c < 3; ++c) t.rgba[k * 4 + c] = uint8_t(sum[c] / n);
                        next[k] = 1;
                        changed = true;
                    }
                done.swap(next);
                if (!changed) break;
            }
        }
        m.textures.push_back(std::move(t));
        p = palette + 1024;
    }

    std::map<uint8_t, int> bySequence;
    std::map<std::string, int> byName;
    for (int i = int(m.textures.size()) - 1; i >= 0; --i) {
        bySequence[m.textures[i].sequence] = i;      // first one wins
        const std::string n = upper(m.textures[i].name);
        byName[n] = i;
        if (n.size() > 1) byName.emplace(n.substr(1), i);
    }

    for (uint32_t li = 0; li < lodCount; ++li) {
        if (!r.ok(p, layout.lodHeader)) return fail("truncated LOD header");
        const uint32_t dataSize = r.u32(p + 0x14);
        const uint32_t nv = r.u32(p + 0x80), nn = r.u32(p + 0x88), nf = r.u32(p + 0x90);
        const uint32_t nm = r.u32(p + 0x98), nh = r.u32(p + 0xA0), nmat = r.u32(p + 0xA8);
        const uint32_t nvol = layout.volumes ? r.u32(p + 0xB0) : 0;
        if (nv > 65536 || nn > 65536 || nf > 65536 || nm > 4096 || nmat > 4096) return fail("implausible LOD counts");

        size_t q = p + layout.lodHeader;
        const size_t vOff = q; q += size_t(nv) * 8;
        const size_t nOff = q; q += size_t(nn) * 8;
        const size_t fOff = q; q += size_t(nf) * 72;
        const size_t mOff = q; q += size_t(nm) * layout.mesh;
        q += size_t(nh) * 12;                  // hardpoints (unused for now)
        const size_t volOff = q; q += size_t(nvol) * 72;
        const size_t matOff = q; q += size_t(nmat) * 76;
        if (!r.ok(p + layout.lodHeader, q - (p + layout.lodHeader))) return fail("truncated LOD data");

        struct Mat { int tex; float rgb[3]; bool hidden; int blend; };
        std::vector<Mat> mats(nmat);
        for (uint32_t i = 0; i < nmat; ++i) {
            const size_t o = matOff + size_t(i) * 76;
            const uint32_t flags = r.u32(o + 0x10);
            Mat mt{-1, {r.u8(o + 0x28) / 255.f, r.u8(o + 0x29) / 255.f, r.u8(o + 0x2A) / 255.f},
                   // 0x0800 hidden, 0x0008 effect trails
                   (flags & (0x0800 | 0x0008)) != 0,
                   // 0x0020 muzzle flash, 0x8000 multiply, 0x0081 translucent
                   (flags & 0x0020) ? 3 : (flags & 0x8000) ? 2 : (flags & 0x0081) == 0x0081 ? 1 : 0};
            if (flags & 0x0001) {
                const uint8_t ti = r.u8(o + 0x34);
                if (auto it = bySequence.find(ti); ti != 0xFF && it != bySequence.end()) mt.tex = it->second;
                else if (auto jt = byName.find(upper(r.str(o, 12))); jt != byName.end()) mt.tex = jt->second;
            }
            mats[i] = mt;
        }

        auto vertex = [&](uint32_t i) {
            const size_t o = vOff + size_t(i) * 8;
            return std::array<float, 3>{r.i16(o) / kUnitsPerMetre, r.i16(o + 4) / kUnitsPerMetre, -r.i16(o + 2) / kUnitsPerMetre};
        };
        auto normal = [&](uint32_t i) {
            const size_t o = nOff + size_t(i) * 8;
            return std::array<float, 3>{r.i16(o) / 16384.f, r.i16(o + 4) / 16384.f, -r.i16(o + 2) / 16384.f};
        };

        ModelLod lod;
        // Collision volume: 72 bytes; the axis-aligned box is six 16.16-metre
        // values at +0x30 (xmin, xmax, ymin, ymax, zmin, zmax) in file axes.
        for (uint32_t i = 0; i < nvol; ++i) {
            const size_t o = volOff + size_t(i) * 72 + 0x30;
            float xmin = r.i32(o) / 65536.f, xmax = r.i32(o + 4) / 65536.f;
            float ymin = r.i32(o + 8) / 65536.f, ymax = r.i32(o + 12) / 65536.f;
            float zmin = r.i32(o + 16) / 65536.f, zmax = r.i32(o + 20) / 65536.f;
            if (xmax < xmin || ymax < ymin || zmax < zmin) continue;
            lod.collision.push_back({{xmin, zmin, -ymax}, {xmax, zmax, -ymin}});
        }
        std::map<int, size_t> batchIndex;   // material -> batch
        bool first = true;
        uint32_t face = 0, vBase = 0, nBase = 0;
        for (uint32_t mi = 0; mi < nm && face < nf; ++mi) {
            const size_t mo = mOff + size_t(mi) * layout.mesh;
            const uint32_t meshVerts = r.u32(mo + 0x04), meshFaces = r.u32(mo + 0x0C), meshNormals = r.u32(mo + 0x14);
            for (uint32_t k = 0; k < meshFaces && face < nf; ++k, ++face) {
                const size_t fo = fOff + size_t(face) * 72;
                const uint32_t matIdx = r.u32(fo + 0x44);
                if (matIdx >= nmat || mats[matIdx].hidden) continue;
                uint32_t vi[3], ni[3];
                bool valid = true;
                for (int c = 0; c < 3; ++c) {
                    vi[c] = vBase + uint32_t(r.i16(fo + 0x1C + c * 2));
                    ni[c] = nBase + uint32_t(r.i16(fo + 0x22 + c * 2));
                    if (vi[c] >= nv) valid = false;
                }
                if (!valid) continue;

                const Mat& mt = mats[matIdx];
                const int key = int(matIdx);
                auto it = batchIndex.find(key);
                if (it == batchIndex.end()) {
                    ModelBatch b;
                    b.texture = mt.tex;
                    std::copy(mt.rgb, mt.rgb + 3, b.color);
                    b.blend = mt.blend;
                    it = batchIndex.emplace(key, lod.batches.size()).first;
                    lod.batches.push_back(std::move(b));
                }
                ModelBatch& b = lod.batches[it->second];

                std::array<float, 3> pos[3] = {vertex(vi[0]), vertex(vi[1]), vertex(vi[2])};
                float e1[3], e2[3], fn[3];
                for (int c = 0; c < 3; ++c) { e1[c] = pos[1][c] - pos[0][c]; e2[c] = pos[2][c] - pos[0][c]; }
                fn[0] = e1[1] * e2[2] - e1[2] * e2[1];
                fn[1] = e1[2] * e2[0] - e1[0] * e2[2];
                fn[2] = e1[0] * e2[1] - e1[1] * e2[0];
                float len = std::sqrt(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
                if (len > 0) for (float& c : fn) c /= len;

                for (int c = 0; c < 3; ++c) {
                    ModelVertex v{};
                    v.px = pos[c][0]; v.py = pos[c][1]; v.pz = pos[c][2];
                    std::array<float, 3> n = ni[c] < nn ? normal(ni[c]) : std::array<float, 3>{fn[0], fn[1], fn[2]};
                    v.nx = n[0]; v.ny = n[1]; v.nz = n[2];
                    v.u = r.i32(fo + 0x04 + c * 4) / 65536.f;
                    v.v = r.i32(fo + 0x10 + c * 4) / 65536.f;
                    b.vertices.push_back(v);
                    for (int a = 0; a < 3; ++a) {
                        float val = (&v.px)[a];
                        if (first) { lod.min[a] = lod.max[a] = val; }
                        lod.min[a] = std::min(lod.min[a], val);
                        lod.max[a] = std::max(lod.max[a], val);
                    }
                    first = false;
                }
                ++lod.triangleCount;
            }
            vBase += meshVerts;
            nBase += meshNormals;
        }
        m.lods.push_back(std::move(lod));
        p += layout.lodHeader + dataSize;
    }
    return true;
}

} // namespace df
