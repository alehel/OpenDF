#include "Pwf.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace df {

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

struct Reader {
    const std::vector<uint8_t>& d;
    bool ok(size_t o, size_t n) const { return o + n <= d.size(); }
    uint32_t u32(size_t o) const { uint32_t v = 0; if (ok(o, 4)) std::memcpy(&v, &d[o], 4); return v; }
    int32_t i32(size_t o) const { return int32_t(u32(o)); }
    uint16_t u16(size_t o) const { uint16_t v = 0; if (ok(o, 2)) std::memcpy(&v, &d[o], 2); return v; }
    std::string str(size_t o, size_t n) const {
        std::string s;
        for (size_t i = 0; i < n && ok(o + i, 1) && d[o + i]; ++i) s += char(d[o + i]);
        return s;
    }
};

} // namespace

bool Pwf::load(std::vector<uint8_t> file, std::string* error) {
    data_ = std::move(file);
    samples_.clear();
    byName_.clear();
    defs_.clear();
    clips_.clear();
    Reader r{data_};
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    // Header: u32 header size, "PWF2", u32 sample count, ..., +0x10 the
    // definitions block offset (0 in banks without definitions).
    if (!r.ok(0, 0x1C) || (std::memcmp(&data_[4], "PWF2", 4) != 0 && std::memcmp(&data_[4], "PWF1", 4) != 0))
        return fail("not a PWF file");
    const uint32_t hs = r.u32(0), n = r.u32(8);
    // Sample entries, 52 bytes: name[24], u32 offset, u32 size, ...
    for (uint32_t i = 0; i < n; ++i) {
        const size_t o = hs + size_t(i) * 52;
        if (!r.ok(o, 52)) return fail("truncated sample table");
        Entry e{r.str(o, 24), r.u32(o + 24), r.u32(o + 28)};
        byName_[upper(e.name)] = int(samples_.size());
        samples_.push_back(e);
    }
    // Definitions: {u32 0x14, u32 count, u32 offset}; records of 0x50 bytes:
    // name at +4, pitch base +0x1C and random range +0x20 (16.16), layer
    // count +0x24, 8 layer offsets +0x28, maximum distance +0x48 (m),
    // flags +0x4C. Layer (0x30): u16 variants, u16 round robin, i32 range
    // (m), u32 flags, u32, 8 variant offsets at +0x10. Variant (28): u32
    // sample, pitch base and range (16.16), gain, cap.
    const uint32_t dfo = std::memcmp(&data_[4], "PWF2", 4) == 0 ? r.u32(0x10) : 0;
    if (dfo && r.ok(dfo, 12)) {
        const uint32_t nd = r.u32(dfo + 4), defOff = r.u32(dfo + 8);
        for (uint32_t i = 0; i < nd; ++i) {
            const size_t o = defOff + size_t(i) * 0x50;
            if (!r.ok(o, 0x50)) break;
            SoundDef d;
            d.name = r.str(o + 4, 24);
            d.pitchBase = r.i32(o + 0x1C) / 65536.f;
            d.pitchRand = r.i32(o + 0x20) / 65536.f;
            const uint32_t nl = std::min<uint32_t>(r.u32(o + 0x24), 8);
            d.maxDistance = float(r.i32(o + 0x48));
            d.flags = r.u32(o + 0x4C);
            for (uint32_t li = 0; li < nl; ++li) {
                const size_t lo = r.u32(o + 0x28 + li * 4);
                if (!r.ok(lo, 0x30)) continue;
                SoundLayer l;
                const uint32_t nv = std::min<uint32_t>(r.u16(lo), 8);
                l.next = r.u16(lo + 2);
                l.range = float(r.i32(lo + 4));
                l.flags = r.u32(lo + 8);
                for (uint32_t vi = 0; vi < nv; ++vi) {
                    const size_t vo = r.u32(lo + 0x10 + vi * 4);
                    if (!r.ok(vo, 28)) continue;
                    SoundVariant v;
                    v.sample = int(r.u32(vo));
                    v.pitchBase = r.i32(vo + 4) / 65536.f;
                    v.pitchRand = r.i32(vo + 8) / 65536.f;
                    v.gain = r.i32(vo + 12);
                    v.cap = r.i32(vo + 16);
                    if (v.sample >= 0 && v.sample < int(samples_.size())) l.variants.push_back(v);
                }
                d.layers.push_back(std::move(l));
            }
            defs_[upper(d.name)] = std::move(d);
        }
    }
    return true;
}

int Pwf::sampleIndex(const std::string& name) const {
    auto it = byName_.find(upper(name));
    return it == byName_.end() ? -1 : it->second;
}

SoundDef* Pwf::def(const std::string& name) {
    auto it = defs_.find(upper(name));
    return it == defs_.end() ? nullptr : &it->second;
}

const SoundClip* Pwf::clip(int sample) {
    if (sample < 0 || sample >= int(samples_.size())) return nullptr;
    if (auto it = clips_.find(sample); it != clips_.end()) return &it->second;
    const Entry& e = samples_[size_t(sample)];
    Reader r{data_};
    SoundClip c;
    const size_t o = e.offset;
    if (r.ok(o, 16) && std::memcmp(&data_[o], "SAMP", 4) == 0) {
        // "SAMP" (DF.EXE's in-memory form, 0x48cfb0): u32 byte count at +4,
        // rate byte at +0xB = rate·16/22050 (0x10 22050 Hz, 0x08 11025 Hz),
        // unsigned 8-bit mono PCM from +0x10.
        const uint32_t len = r.u32(o + 4);
        c.rate = int(data_[o + 0xB]) * 22050 / 16;
        c.rateField = r.u32(o + 8);   // the mixer multiplies the whole field (0x4a9a12)
        for (uint32_t i = 0; i < len && r.ok(o + 16 + i, 1); ++i) c.samples.push_back(int16_t((int(data_[o + 16 + i]) - 128) << 8));
    } else if (r.ok(o, 0x2C) && std::memcmp(&data_[o], "RIFF", 4) == 0) {
        // A WAV, read on first use into a buffer of the entry's size
        // (0x491d3e..0x491d6b) and converted in place as a loose one is
        // (0x48cfb0; DF2.EXE 0x4711a0): see dfConvertWav.
        dfConvertWav(&data_[o], std::min<size_t>(e.size, data_.size() - o), full16, c);
    }
    return &(clips_[sample] = std::move(c));
}

} // namespace df
