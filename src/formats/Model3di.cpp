#include "Model3di.h"

#include <cstdio>
#include <cstdlib>

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

// v8: Delta Force 2 / Land Warrior (DF2.EXE 0x43e570, docs/OTHER-GAMES.md):
// 52-byte texture headers (with 16-bit index+alpha pixels), a 192-byte LOD
// header with collision planes (8 bytes) before 80-byte volumes, 120-byte
// materials.
struct Layout { size_t lodHeader, mesh; bool volumes; size_t texHeader = 36, volume = 72, material = 76; bool v8 = false; };

bool layoutFor(int version, Layout& l) {
    switch (version) {
    case 2: l = {176, 88, false}; return true;
    case 3: case 4: l = {176, 104, false}; return true;
    case 5: case 6: l = {184, 112, true}; return true;
    case 8: l = {192, 112, true, 52, 80, 120, true}; return true;
    default: return false;
    }
}

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool loadModel3di(const std::vector<uint8_t>& file, Model3di& m, std::string* error, int variant) {
    variant = std::clamp(variant, 0, file.size() > 3 && file[3] >= 8 ? 2 : 3);   // v8: three camouflage variants
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
    for (int k = 0; k < 4; ++k) m.lodThreshold[k] = r.i32(0x18 + 4 * k) / 65536.f;
    m.radius = r.i32(0x60) / 65536.f;
    m.flatRadius = r.i32(0x64) / 65536.f;
    m.kind = r.str(0x28, 4);
    for (int k = 0; k < 4; ++k) {
        std::string c(reinterpret_cast<const char*>(&r.d[0x28 + 4 * size_t(k)]), 4);
        m.lodCodes[k] = std::string(c.rbegin(), c.rend());
    }

    // Textures: 36-byte header, pixels (frame 0 is used), 256 x RGBA palette.
    size_t p = 0x80;
    for (uint32_t i = 0; i < texCount; ++i) {
        if (!r.ok(p, layout.texHeader)) return fail("truncated texture header");
        Model3diTexture t;
        const size_t f = layout.v8 ? 0x0C : 0;   // v8 fields move by 12 (a second name at +0x0C)
        t.name = r.str(p, 12);
        const uint32_t size = r.u32(p + 0x10 + f);
        // v8: the header is copied whole into the texture table (GOG Df2.exe
        // 0x445120, 0x34 bytes) and +0x20 is read as a signed word
        // (0x444d5f / 0x444eb1).
        t.sequence = layout.v8 ? int(r.i16(p + 0x14 + f)) : int(r.u8(p + 0x14 + f));
        const uint8_t tflags = r.u8(p + 0x16 + f);
        t.indexZeroTransparent = !layout.v8 && (tflags & 0x20) != 0;
        t.noGrade = !layout.v8 && (tflags & 0x10) != 0;
        t.width = r.u16(p + 0x18 + f);
        t.height = r.u16(p + 0x1A + f);
        const size_t pixels = p + layout.texHeader;
        const size_t palette = pixels + size;
        const size_t texels = size_t(t.width) * t.height;
        if (texels == 0 || size < texels || !r.ok(pixels, size) || !r.ok(palette, 1024)) return fail("bad texture");
        // v8: loaded only for its camouflage variants (bits 0-2) or all (0x08)
        // (0x43ee76); skipped textures keep no sequence.
        if (layout.v8 && !(tflags & (1 << variant)) && !(tflags & 0x08)) {
            p = palette + 1024;
            continue;
        }
        // v8 alpha textures: 2 bytes a texel (palette index, alpha), named at +0x0C.
        const bool alpha16 = layout.v8 && size == 2 * texels;
        if (alpha16) t.name = r.str(p + 0x0C, 16);
        t.alpha16 = alpha16;
        t.rgba.resize(texels * 4);
        t.indices.resize(texels);
        for (size_t k = 0; k < texels; ++k) {
            const uint8_t idx = alpha16 ? file[pixels + 2 * k] : file[pixels + k];
            t.indices[k] = idx;
            const uint8_t* c = &file[palette + size_t(idx) * 4];
            t.rgba[k * 4] = c[0];
            t.rgba[k * 4 + 1] = c[1];
            t.rgba[k * 4 + 2] = c[2];
            t.rgba[k * 4 + 3] = alpha16 ? file[pixels + 2 * k + 1] : (t.indexZeroTransparent && idx == 0) ? 0 : 255;
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

    std::map<int, int> bySequence;
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
        const uint32_t nvol = layout.volumes ? r.u32(p + (layout.v8 ? 0xB8 : 0xB0)) : 0;
        const uint32_t nplanes = layout.v8 ? r.u32(p + 0xB0) : 0;   // v8: 8-byte collision planes
        if (nv > 65536 || nn > 65536 || nf > 65536 || nm > 4096 || nmat > 4096) return fail("implausible LOD counts");

        size_t q = p + layout.lodHeader;
        const size_t vOff = q; q += size_t(nv) * 8;
        const size_t nOff = q; q += size_t(nn) * 8;
        const size_t fOff = q; q += size_t(nf) * 72;
        const size_t mOff = q; q += size_t(nm) * layout.mesh;
        const size_t hOff = q; q += size_t(nh) * 12;   // hardpoints
        q += size_t(nplanes) * 8;
        const size_t volOff = q; q += size_t(nvol) * layout.volume;
        const size_t matOff = q; q += size_t(nmat) * layout.material;
        if (!r.ok(p + layout.lodHeader, q - (p + layout.lodHeader))) return fail("truncated LOD data");

        // slot: signed byte +0x1F (the animated material's frame slot, or
        // 0x7F: a back-side material follows, DF2.EXE 0x44bc00); noSpan:
        // hidden for a reason other than flag 0x0800.
        struct Mat { int tex; float rgb[3]; bool hidden; int blend; uint32_t flags; float spec; int specPow; float bright; uint16_t impact;
                     int slot = 0; bool noSpan = false; };
        std::vector<Mat> mats(nmat);
        for (uint32_t i = 0; i < nmat; ++i) {
            const size_t o = matOff + size_t(i) * layout.material;
            uint32_t flags = r.u16(o + 0x10);
            const size_t rgbAt = o + 0x28 + 3 * size_t(variant);
            Mat mt{-1, {r.u8(rgbAt) / 255.f, r.u8(rgbAt + 1) / 255.f, r.u8(rgbAt + 2) / 255.f}, false, 0, flags,
                   r.i32(o + 0x38) / 65536.f, int(r.u8(o + 0x40)), (flags & 0x0020) ? r.i32(o + 0x3c) / 65536.f : -1.f,
                   r.u16(o + 0x12)};
            // Textured only if a texture has the variant's sequence id (0x43f217).
            if ((flags & 0x0001) && layout.v8) {
                // GOG Df2.exe 0x444d3d: the byte is sign-extended and compared
                // with the textures' signed words in file order (0x444d5f), with
                // no name fallback; 0x80..0xFF never match and no match sets
                // 0x0800 (0x444d74), e.g. df2par02's vest pieces.
                const int ti = int(int8_t(r.u8(o + 0x34 + size_t(variant))));
                if (auto it = bySequence.find(ti); it != bySequence.end()) mt.tex = it->second;
            } else if (flags & 0x0001) {
                const uint8_t ti = r.u8(o + 0x34 + size_t(variant));
                if (auto it = bySequence.find(ti); ti != 0xFF && it != bySequence.end()) mt.tex = it->second;
                else if (auto jt = byName.find(upper(r.str(o, 12))); jt != byName.end()) mt.tex = jt->second;
            } else if (layout.v8 && (flags & 0x8000)) {
                // v8: an alpha material without 0x0001 takes its texture by
                // the signed sequence at +0x37 (GOG Df2.exe 0x444e9a vs the
                // signed word at 0x444eb1; CD 0x43ebca), e.g. the SMKT_H
                // dust-cloud frames and SNOW.3DI.
                const int ti = int(int8_t(r.u8(o + 0x37)));
                if (auto it = bySequence.find(ti); it != bySequence.end()) mt.tex = it->second;
            }
            // 0x0800 hidden; 0x0008 is not drawn in the hi-colour renderer;
            // 0x0080 and 0x8000 need a texture (no span for the untextured ones).
            const bool textured = mt.tex >= 0;
            mt.noSpan = (flags & 0x0008) != 0 || ((flags & (0x0080 | 0x8000)) && !textured) ||
                        (layout.v8 && (flags & 0x0001) && !textured);   // v8: a missing texture hides it
            mt.hidden = mt.noSpan || (flags & 0x0800) != 0;
            mt.slot = int(int8_t(r.u8(o + 0x1F)));
            // v8: 0x8000 marks the alpha textures (16-bit index + alpha):
            // lit as usual and blended by the texel's alpha (mode 3).
            mt.blend = (flags & 0x8000) ? (layout.v8 ? 3 : 2) : (flags & 0x0080) ? 1 : 0;
            // v8 0x8000 materials on an 8-bit texture (SMKT_H, SNOW, the smoke
            // and spark effects: a grey ramp palette i -> (i, i, i)): INFERRED
            // drawn as v5's 0x8000, the texel as the blend weight toward the
            // material (or vertex) colour, since the texture holds no colour.
            if (layout.v8 && (flags & 0x8000) && mt.tex >= 0 && !m.textures[size_t(mt.tex)].alpha16) mt.blend = 2;
            mats[i] = mt;
        }
        std::vector<int> matTextures;
        for (const auto& mt : mats) matTextures.push_back(mt.tex);

        auto vertex = [&](uint32_t i) {
            const size_t o = vOff + size_t(i) * 8;
            return std::array<float, 3>{r.i16(o) / kUnitsPerMetre, r.i16(o + 4) / kUnitsPerMetre, -r.i16(o + 2) / kUnitsPerMetre};
        };
        auto normal = [&](uint32_t i) {
            const size_t o = nOff + size_t(i) * 8;
            return std::array<float, 3>{r.i16(o) / 16384.f, r.i16(o + 4) / 16384.f, -r.i16(o + 2) / 16384.f};
        };

        ModelLod lod;
        lod.materialTextures = matTextures;
        for (const auto& mt : mats) {
            ModelMaterial mm;
            mm.texture = mt.tex;
            std::copy(mt.rgb, mt.rgb + 3, mm.color);
            mm.blend = mt.blend;
            mm.flags = mt.flags;
            mm.specular = mt.spec;
            mm.specularPower = mt.specPow;
            mm.fullbright = mt.bright;
            mm.hidden = mt.hidden;
            lod.materials.push_back(mm);
        }
        // Hardpoints: i32 x, y, z in 16.16 metres, file axes as the vertices.
        for (uint32_t i = 0; i < nh; ++i) {
            const size_t o = hOff + size_t(i) * 12;
            lod.points.push_back({r.i32(o) / 65536.f, r.i32(o + 8) / 65536.f, -r.i32(o + 4) / 65536.f});
        }
        lod.frames = std::max(1, r.i32(p + 0x40) >> 16);
        if (li == 0) m.height = r.i32(p + 0x24) / 65536.f;
        if (li == 0)
            for (int k = 0; k < 4; ++k) m.box[k] = r.i32(p + 0x28 + 4 * size_t(k)) / 65536.f;
        lod.ticksPerFrame = std::max(1, r.i32(p + 0x58) >> 16);
        lod.animated = r.u16(p + 0x5a) != 0;
        // Collision volume: 72 bytes; the axis-aligned box is six 16.16-metre
        // values at +0x30 (xmin, xmax, ymin, ymax, zmin, zmax) in file axes.
        for (uint32_t i = 0; i < nvol; ++i) {
            const size_t o = volOff + size_t(i) * layout.volume + 0x30;
            float xmin = r.i32(o) / 65536.f, xmax = r.i32(o + 4) / 65536.f;
            float ymin = r.i32(o + 8) / 65536.f, ymax = r.i32(o + 12) / 65536.f;
            float zmin = r.i32(o + 16) / 65536.f, zmax = r.i32(o + 20) / 65536.f;
            if (xmax < xmin || ymax < ymin || zmax < zmin) continue;
            CollisionBox cb{{xmin, zmin, -ymax}, {xmax, zmax, -ymin}};
            const size_t vo = volOff + size_t(i) * layout.volume;
            cb.type = int(r.u32(vo));
            cb.cx = r.i32(vo + 0x08) / 65536.f;
            cb.cz = -r.i32(vo + 0x0C) / 65536.f;
            cb.r = r.i32(vo + 0x18) / 65536.f;
            cb.cy = r.i32(vo + 0x10) / 65536.f;
            // v5: the owning mesh, by the running total of the meshes' +0x1C.
            if (!layout.v8) {
                uint32_t first = 0;
                for (uint32_t mi = 0; mi < nm; ++mi) {
                    const uint32_t count = r.u32(mOff + size_t(mi) * layout.mesh + 0x1C);
                    if (i >= first && i - first < count) { cb.mesh = int(mi); break; }
                    first += count;
                }
            }
            lod.collision.push_back(cb);
        }
        if (layout.volumes && !layout.v8)
            for (uint32_t mi = 0; mi < nm; ++mi) {
                const size_t mo = mOff + size_t(mi) * layout.mesh + 0x40;
                auto v = [&](int k) { return r.i32(mo + 4 * size_t(k)) / 65536.f; };
                lod.meshBoxes.push_back({v(0), v(4), -v(3), v(1), v(5), -v(2)});
            }
        // v8 (DF2.EXE loader 0x43e937..0x43e9a4): each mesh owns the next
        // +0x1C volumes, each volume the next +0x48 planes. Running totals
        // past the LOD's counts (10 shipped LODs do not add up) are cut off
        // here; DF2.EXE would read past the arrays (not reproduced).
        if (layout.v8 && li == 0) {
            uint32_t vol = 0, plane = 0;
            for (uint32_t mi = 0; mi < nm; ++mi) {
                const size_t mo = mOff + size_t(mi) * layout.mesh;
                MeshVolumes mv;
                for (int c = 0; c < 6; ++c) mv.box[c] = r.i32(mo + 0x40 + 4 * size_t(c)) / 65536.f;
                const uint32_t count = r.u32(mo + 0x1C);
                for (uint32_t k = 0; k < count && vol < nvol; ++k, ++vol) {
                    const size_t vo = volOff + size_t(vol) * layout.volume;
                    PlaneVolume pv;
                    pv.type = int(r.u32(vo));
                    const uint32_t np = r.u32(vo + 0x48);
                    for (uint32_t j = 0; j < np && plane < nplanes; ++j, ++plane) {
                        const size_t po = hOff + size_t(nh) * 12 + size_t(plane) * 8;
                        pv.planes.push_back({{r.i16(po) / 16384.f, r.i16(po + 2) / 16384.f, r.i16(po + 4) / 16384.f}, r.i16(po + 6) / 256.f});
                    }
                    mv.volumes.push_back(std::move(pv));
                }
                lod.meshVolumes.push_back(std::move(mv));
            }
        }
        if (std::getenv("OPENDF_DEBUGMESH") && li == 0 && m.name == std::getenv("OPENDF_DEBUGMESH"))
            for (uint32_t mi = 0, vb = 0; mi < nm; ++mi) {
                const size_t mo = mOff + size_t(mi) * layout.mesh;
                const uint32_t mv = r.u32(mo + 0x04);
                float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
                for (uint32_t k = 0; k < mv; ++k) {
                    const auto v = vertex(vb + k);
                    for (int c = 0; c < 3; ++c) lo[c] = std::min(lo[c], v[c]), hi[c] = std::max(hi[c], v[c]);
                }
                std::fprintf(stderr, "mesh %u verts %u +0x24 %d pivot (%.3f %.3f %.3f) bbox (%.2f..%.2f, %.2f..%.2f, %.2f..%.2f) words:", mi, mv,
                             r.i32(mo + 0x24), r.i32(mo + 0x34) / 65536.f, r.i32(mo + 0x38) / 65536.f, r.i32(mo + 0x3c) / 65536.f,
                             lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
                for (size_t w = 0; w < layout.mesh; w += 4) std::fprintf(stderr, " %x", r.u32(mo + w));
                std::fprintf(stderr, "\n");
                vb += mv;
            }
        for (uint32_t mi = 0; mi < nm; ++mi) {
            const size_t mo = mOff + size_t(mi) * layout.mesh;
            lod.meshParent.push_back(layout.mesh >= 0x40 ? r.i32(mo + 0x24) : -1);
            lod.meshPivot.push_back(layout.mesh >= 0x40 ? std::array<float, 3>{r.i32(mo + 0x34) / 65536.f, r.i32(mo + 0x3c) / 65536.f,
                                                                               -r.i32(mo + 0x38) / 65536.f}
                                                        : std::array<float, 3>{0, 0, 0});
            lod.meshRadius.push_back(layout.mesh >= 0x68 ? r.i32(mo + 0x64) / 65536.f : 0.f);
            if (layout.mesh >= 0x58) {
                auto mid = [&](size_t a) { return float((r.i32(mo + a) + r.i32(mo + a + 4)) >> 1) / 65536.f; };
                lod.meshCentre.push_back({mid(0x40), mid(0x48), mid(0x50)});
            } else lod.meshCentre.push_back({0, 0, 0});
        }
        if (li == 0) lod.hitMeshes.resize(nm);
        std::map<int, size_t> batchIndex;   // material and mesh -> batch
        bool first = true;
        uint32_t face = 0, vBase = 0, nBase = 0;
        for (uint32_t mi = 0; mi < nm && face < nf; ++mi) {
            const size_t mo = mOff + size_t(mi) * layout.mesh;
            const uint32_t meshVerts = r.u32(mo + 0x04), meshFaces = r.u32(mo + 0x0C), meshNormals = r.u32(mo + 0x14);
            for (uint32_t k = 0; k < meshFaces && face < nf; ++k, ++face) {
                const size_t fo = fOff + size_t(face) * 72;
                const uint32_t matIdx = r.u32(fo + 0x44);
                if (li == 0 && matIdx < nmat) {
                    // Rounds test every face, drawn or not (0x44ec29).
                    HitFace h{};
                    bool ok = true;
                    for (int c = 0; c < 3; ++c) {
                        const uint32_t vx = vBase + uint32_t(r.i16(fo + 0x1C + c * 2));
                        if (vx >= nv) { ok = false; break; }
                        const size_t o = vOff + size_t(vx) * 8;
                        h.v[c][0] = r.i16(o) / 256.f; h.v[c][1] = r.i16(o + 2) / 256.f; h.v[c][2] = r.i16(o + 4) / 256.f;
                    }
                    const uint32_t ni2 = nBase + uint32_t(r.u16(fo + 0x02));
                    if (ok && ni2 < nn) {
                        const size_t o = nOff + size_t(ni2) * 8;
                        h.n[0] = r.i16(o) / 16384.f; h.n[1] = r.i16(o + 2) / 16384.f; h.n[2] = r.i16(o + 4) / 16384.f;
                        h.axis = r.u16(o + 6);
                        h.d = r.i32(fo + 0x28) / 65536.f;
                        for (int c = 0; c < 6; ++c) h.box[c] = r.i32(fo + 0x2C + 4 * size_t(c)) / 65536.f;
                        h.material = uint32_t(mats[matIdx].flags) | uint32_t(mats[matIdx].impact) << 16;
                        lod.hitMeshes[mi].push_back(h);
                    }
                }
                // Delta Force (GOG Df.exe 0x46b1d0): an animated slot-1
                // material is swapped for its frame's record before the
                // 0x0800 test, so its faces are kept (the draw tests the
                // frame's record).
                const bool df1Anim = !layout.v8 && matIdx < nmat && (mats[matIdx].flags & 0x4000) && mats[matIdx].slot == 1;
                if (matIdx >= nmat || (mats[matIdx].hidden && !(df1Anim && !mats[matIdx].noSpan))) continue;
                uint32_t vi[3], ni[3];
                bool valid = true;
                for (int c = 0; c < 3; ++c) {
                    vi[c] = vBase + uint32_t(r.i16(fo + 0x1C + c * 2));
                    ni[c] = nBase + uint32_t(r.i16(fo + 0x22 + c * 2));
                    if (vi[c] >= nv) valid = false;
                }
                if (!valid) continue;

                const Mat& mt = mats[matIdx];
                // DF2.EXE / GOG Df2.exe 0x44bc00: a back-facing face of a
                // two-sided material (flag 0x0002) that also has flag 0x4000
                // and slot +0x1F == 0x7F is drawn with the next material
                // (+0x78: its texture, flags, blend and brightness), turned
                // and lit from its back; its front keeps the material.
                // Only flag 0x0800 is tested on the front material. Delta
                // Force the same (GOG Df.exe 0x46b1d0: +0x10 & 2, +0x11 &
                // 0x40, +0x1F == 0x7F -> the next 0x4c record), e.g. the
                // BGUY01 hat, BAGTENT and BRDM panels.
                const bool backMaterial = (mt.flags & 0x4002) == 0x4002 && mt.slot == 0x7F && matIdx + 1 < nmat;
                auto batchFor = [&](int key, const Mat& bm, uint32_t index, bool doubleSided) -> size_t {
                    auto it = batchIndex.find(key);
                    if (it == batchIndex.end()) {
                        ModelBatch b;
                        b.texture = bm.tex;
                        std::copy(bm.rgb, bm.rgb + 3, b.color);
                        b.blend = bm.blend;
                        b.flags = bm.flags;
                        b.material = int(index);
                        b.mesh = int(mi);
                        b.specular = bm.spec;
                        b.specularPower = bm.specPow;
                        b.fullbright = bm.bright;
                        b.doubleSided = doubleSided;
                        b.impact = bm.impact;
                        b.slot = mt.slot;
                        it = batchIndex.emplace(key, lod.batches.size()).first;
                        lod.batches.push_back(std::move(b));
                    }
                    return it->second;
                };
                const size_t frontAt = batchFor(int(matIdx) * 1024 + int(mi), mt, matIdx, !backMaterial && (mt.flags & 0x0002) != 0);
                // The back side: its own single-sided batch, winding reversed.
                const size_t backAt = backMaterial && !mats[matIdx + 1].noSpan
                                          ? batchFor((1 << 24) + int(matIdx) * 1024 + int(mi), mats[matIdx + 1], matIdx + 1, false)
                                          : SIZE_MAX;
                ModelBatch& b = lod.batches[frontAt];
                ModelBatch* back = backAt != SIZE_MAX ? &lod.batches[backAt] : nullptr;

                std::array<float, 3> pos[3] = {vertex(vi[0]), vertex(vi[1]), vertex(vi[2])};
                float e1[3], e2[3], fn[3];
                for (int c = 0; c < 3; ++c) { e1[c] = pos[1][c] - pos[0][c]; e2[c] = pos[2][c] - pos[0][c]; }
                fn[0] = e1[1] * e2[2] - e1[2] * e2[1];
                fn[1] = e1[2] * e2[0] - e1[0] * e2[2];
                fn[2] = e1[0] * e2[1] - e1[1] * e2[0];
                float len = std::sqrt(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
                if (len > 0) for (float& c : fn) c /= len;

                // Face byte +0 bit 0x01: per-vertex normals (Gouraud); else the
                // face normal (word +0x02) for all three (flat) (0x46fa5a).
                const bool gouraud = (r.u8(fo) & 1) != 0;
                const uint32_t faceNormal = nBase + uint32_t(r.u16(fo + 0x02));
                // GOG Df.exe 0x46b1d0 -> 0x469fc0 (DF1) and DF2.EXE / GOG
                // Df2.exe 0x44bc00 -> 0x44b140: in a mesh whose flag byte +0
                // has bit 0 set, each vertex is placed by the part matrix its
                // own word +6 names (skinned joints); else 0x469dc0 / 0x44af90
                // places the whole mesh by the mesh's matrix. Both layouts.
                const bool skinned = (r.u8(mo) & 1) != 0;
                ModelVertex tri[3];
                for (int c = 0; c < 3; ++c) {
                    ModelVertex v{};
                    v.px = pos[c][0]; v.py = pos[c][1]; v.pz = pos[c][2];
                    const uint32_t nIdx = gouraud ? ni[c] : faceNormal;
                    std::array<float, 3> n = nIdx < nn ? normal(nIdx) : std::array<float, 3>{fn[0], fn[1], fn[2]};
                    v.nx = n[0]; v.ny = n[1]; v.nz = n[2];
                    v.u = r.i32(fo + 0x04 + c * 4) / 65536.f;
                    v.v = r.i32(fo + 0x10 + c * 4) / 65536.f;
                    v.bone = -1.f;
                    if (skinned) {
                        const uint16_t bone = r.u16(vOff + size_t(vi[c]) * 8 + 6);
                        if (bone < nm) v.bone = float(bone);
                    }
                    tri[c] = v;
                    b.vertices.push_back(v);
                    for (int a = 0; a < 3; ++a) {
                        float val = (&v.px)[a];
                        if (first) { lod.min[a] = lod.max[a] = val; }
                        lod.min[a] = std::min(lod.min[a], val);
                        lod.max[a] = std::max(lod.max[a], val);
                    }
                    first = false;
                }
                if (back)
                    for (int c : {0, 2, 1}) {
                        ModelVertex v = tri[c];
                        v.nx = -v.nx; v.ny = -v.ny; v.nz = -v.nz;
                        back->vertices.push_back(v);
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
