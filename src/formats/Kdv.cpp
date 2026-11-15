#include "Kdv.h"

#include <algorithm>
#include <cstring>

namespace df {

namespace {

constexpr uint32_t tag(const char* s) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 | uint32_t(uint8_t(s[3])) << 24;
}
uint32_t u32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint16_t u16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }

// Bytes the codecs may read past a chunk's end (DF.EXE does not check; the
// zero padding keeps a damaged chunk inside the buffer). APPROXIMATION.
constexpr size_t kPad = 64;

} // namespace

bool KdvVideo::open(const std::filesystem::path& path) {
    *this = KdvVideo();
    file_.open(path, std::ios::binary);
    if (!file_) return setError("cannot open file");
    file_.seekg(0, std::ios::end);
    size_ = uint64_t(file_.tellg());   // 0x48bea8: the file's size bounds the chunk loop
    file_.seekg(0);
    // 0x48700a: the first chunk must be xVDK (0x48700f), then a pVDK right
    // after it is read too (0x48703e).
    for (int i = 0; i < 2; ++i) {
        if (consumed_ + 8 > size_) return i == 1 || setError("ANIMMGR: Invalid KDV File");
        uint8_t h[8];
        file_.seekg(std::streamoff(consumed_));
        file_.read(reinterpret_cast<char*>(h), 8);
        if (u32(h) != (i == 0 ? tag("xVDK") : tag("pVDK"))) {
            if (i == 0) return setError("ANIMMGR: Invalid KDV File");
            break;
        }
        std::vector<uint8_t> c;
        Step fail;
        if (!readChunk(c, fail)) return false;
        if (dispatch(c) == -2) return false;
    }
    return true;
}

bool KdvVideo::readChunk(std::vector<uint8_t>& c, Step& fail) {
    // 0x487827: at the file's end, done; a tag must end in 'K' (0x487849).
    fail = Step::End;
    if (consumed_ >= size_) return false;
    fail = Step::Error;
    uint8_t h[8] = {};
    file_.clear();
    file_.seekg(std::streamoff(consumed_));
    file_.read(reinterpret_cast<char*>(h), 8);
    if (file_.gcount() < 8 || h[3] != 'K') return setError("ANIMMGR: Invalid KDV File");
    const uint32_t size = u32(h + 4);
    // A chunk past the file's end: "[BRE] Invalid KDV File" (0x4877e1).
    // Below 8 bytes the chunk loop would not move on: an error too
    // (APPROXIMATION, DF.EXE has no such check).
    if (size < 8 || consumed_ + size > size_) return setError("ANIMMGR: [BRE] Invalid KDV File");
    c.assign(size + kPad, 0);
    std::memcpy(c.data(), h, 8);
    file_.read(reinterpret_cast<char*>(c.data() + 8), std::streamsize(size - 8));
    if (uint64_t(file_.gcount()) < size - 8) return setError("ANIMMGR: [BRE] Invalid KDV File");
    c.resize(size + kPad);
    consumed_ += size;   // 0x48785a
    return true;
}

KdvVideo::Step KdvVideo::next() {
    for (;;) {
        std::vector<uint8_t> c;
        Step fail;
        if (!readChunk(c, fail)) return fail;
        const int r = dispatch(c);
        // 0x487232: an image chunk (and a header with a new size, 0x48725b)
        // ends the step; the others go on to the next chunk.
        if (r == -2) return Step::Error;
        if (r <= 0) return Step::Frame;
    }
}

int KdvVideo::dispatch(const std::vector<uint8_t>& c) {
    const uint8_t* p = c.data();
    const uint32_t t = u32(p), size = u32(p + 4);
    const uint8_t* body = p + 8;
    const uint8_t* end = p + size;
    if (t == tag("xVDK")) {
        // 0x48743b: +0x10 frames, +0x14 period, +0x18 version 0x22 with
        // bit 31 the 24-bit flag (0x48748c), else "Mismatched Player and
        // KDV file" (0x48745a). The same size goes on; a new one is taken
        // and ends the step (0x4874aa).
        frames_ = int(u32(p + 0x10));
        period_ = int(u32(p + 0x14));
        const uint32_t ver = u32(p + 0x18);
        if ((ver & 0x7fffffff) != 0x22) { error_ = "ANIMMGR: Mismatched Player and KDV file"; return -2; }
        if (ver & 0x80000000u) trueColor_ = true;   // 0x486724 = 24; never set back to 8
        const int w = int(u32(p + 8)), h = int(u32(p + 0xc));
        const size_t bytes = size_t(std::max(w, 0)) * size_t(std::max(h, 0)) * (trueColor_ ? 3 : 1);
        if (w == width_ && h == height_ && bytes == frame_.size()) return 1;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { error_ = "ANIMMGR: Invalid KDV File"; return -2; }   // guard, APPROXIMATION
        width_ = w;
        height_ = h;
        // INFERRED: the screen DF.EXE decodes into starts cleared (black).
        frame_.assign(bytes, 0);
        return -1;
    }
    if (t == tag("pVDK")) {
        // 0x4874ff: 0xC0 dwords (768 bytes) to 0x486764; set at the next flip.
        std::memcpy(palette_.data(), body, 768);
        paletteChanged_ = true;
        return 1;
    }
    if (t == tag("aVDK") || t == tag("sVDK")) {
        // 0x48751c / 0x487594: the chunk body into the next of 16 buffers.
        ringAt_ = (ringAt_ + 1) & 15;
        ring_[size_t(ringAt_)].assign(body, end);
        ++pending_;
        stereo_ = t == tag("sVDK");
        return 1;
    }
    if (t == tag("i42K")) {
        if (!frame_.empty() && trueColor_) decode24(body, end);
        return 0;
    }
    if (t == tag("iVDK")) {
        if (!frame_.empty() && !trueColor_) decode8(body, end);
        return 0;
    }
    // 0x487691: "ANIMMGR: "xxxx" Invalid Digital Video Token" with the tag
    // byte-reversed into the quotes.
    error_ = "ANIMMGR: \"";
    for (int i = 3; i >= 0; --i) error_ += char(p[i]);
    error_ += "\" Invalid Digital Video Token";
    return -2;
}

// iVDK (0x487894): 4x4 blocks left to right, top to bottom; a type byte
// before every fourth block gives two bits per block, low bits first.
// Palette index 0 is never written (the previous frame shows through).
void KdvVideo::decode8(const uint8_t* s, const uint8_t* end) {
    const int w = width_, bw = width_ / 4, bh = height_ / 4;
    uint8_t* d = frame_.data();
    unsigned types = 0, count = 0;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            if (s >= end) return;
            if ((count++ & 3) == 0) types = *s++;
            uint8_t* o = d + size_t(by) * 4 * size_t(w) + size_t(bx) * 4;
            auto put = [&](int i, uint8_t v) { if (v) o[(i >> 2) * w + (i & 3)] = v; };
            int t = int(types & 3);
            types >>= 2;
            if (t == 3) t = mode8_ == 2 ? 4 : mode8_ == 0 ? 5 : 6;   // 0x487b19: the last mode again
            switch (t) {
            case 0:   // four colours, then 16 two-bit indices (0x4878cb)
                mode8_ = 0;
                std::memcpy(colors8_.data(), s, 4);
                s += 4;
                [[fallthrough]];
            case 5: {
                const uint32_t m = u32(s);
                for (int i = 0; i < 16; ++i) put(i, colors8_[(m >> (2 * i)) & 3]);
                s += 4;
                break;
            }
            case 1:
                if (s[0] == s[1]) {   // equal colours: 16 raw pixels (0x487b63)
                    for (int i = 0; i < 16; ++i) put(i, s[2 + i]);
                    s += 18;
                    break;
                }
                mode8_ = 1;   // two colours, then a 16-bit mask (0x4879fc)
                colors8_[0] = s[0];
                colors8_[1] = s[1];
                s += 2;
                [[fallthrough]];
            case 6: {
                const uint16_t m = u16(s);
                for (int i = 0; i < 16; ++i) put(i, colors8_[(m >> i) & 1]);
                s += 2;
                break;
            }
            case 2:   // one colour over the block (0x487b34)
                mode8_ = 2;
                colors8_[0] = *s++;
                [[fallthrough]];
            case 4:
                if (colors8_[0])
                    for (int i = 0; i < 16; ++i) o[(i >> 2) * w + (i & 3)] = colors8_[0];
                break;
            }
        }
}

// i42K (0x487e45; GOG 0x4913ec picks a 32-bit target, [0x491000] = 0x20,
// whenever the display has 24 bits or more, and 0x492725 runs that path):
// the same
// block scheme in B, G, R pixels. A colour is one byte with bit 7 set, an
// index into a 128-entry cache cleared per frame, or three bytes (the first
// doubled: 7 bits of blue, then green, red); literal colours of two-colour
// and solid blocks go into the cache at a rolling index. A pixel whose whole
// colour is 0 (blue, green and red) is not written.
void KdvVideo::decode24(const uint8_t* s, const uint8_t* end) {
    for (auto& e : cache_) e = {0, 0, 0};   // 0x487e61: 0x80 dwords cleared
    cacheAt_ = 0;
    const int w = width_, bw = width_ / 4, bh = height_ / 4;
    const size_t stride = size_t(w) * 3;
    uint8_t* d = frame_.data();
    auto color = [&](uint8_t* out, bool insert) {
        const uint8_t b = *s;
        if (b & 0x80) {
            const auto& c = cache_[b & 0x7f];
            out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
            ++s;
            return;
        }
        out[0] = uint8_t(b << 1);
        out[1] = s[1];
        out[2] = s[2];
        s += 3;
        if (insert) {
            cache_[size_t(cacheAt_)] = {out[0], out[1], out[2]};
            cacheAt_ = (cacheAt_ + 1) & 0x7f;
        }
    };
    unsigned types = 0, count = 0;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            if (s >= end) return;
            if ((count++ & 3) == 0) types = *s++;
            uint8_t* o = d + size_t(by) * 4 * stride + size_t(bx) * 12;
            auto put = [&](int i, const uint8_t* c) {
                if (!(c[0] | c[1] | c[2])) return;   // whole colour dword 0 (GOG 0x492725)
                uint8_t* q = o + size_t(i >> 2) * stride + size_t(i & 3) * 3;
                q[0] = c[0]; q[1] = c[1]; q[2] = c[2];
            };
            int t = int(types & 3);
            types >>= 2;
            if (t == 3) t = mode24_ == 2 ? 4 : mode24_ == 0 ? 5 : 6;   // 0x48846a
            switch (t) {
            case 0:   // four colours (not cached), 16 two-bit indices (0x487eb2)
                mode24_ = 0;
                for (int k = 0; k < 4; ++k) color(&colors24_[size_t(3 * k)], false);
                [[fallthrough]];
            case 5: {
                const uint32_t m = u32(s);
                for (int i = 0; i < 16; ++i) put(i, &colors24_[size_t(3 * ((m >> (2 * i)) & 3))]);
                s += 4;
                break;
            }
            case 1:
                if (!(s[0] | s[1] | s[2] | s[3] | s[4] | s[5])) {
                    // six zero bytes: 48 raw bytes, all written (0x488532)
                    for (int r = 0; r < 4; ++r) std::memcpy(o + size_t(r) * stride, s + 6 + 12 * r, 12);
                    s += 0x36;
                    break;
                }
                mode24_ = 1;   // two colours, a 16-bit mask (0x4881b3)
                color(&colors24_[0], true);
                color(&colors24_[3], true);
                [[fallthrough]];
            case 6: {
                const uint16_t m = u16(s);
                for (int i = 0; i < 16; ++i) put(i, &colors24_[size_t(3 * ((m >> i) & 1))]);
                s += 2;
                break;
            }
            case 2:   // one colour (0x488485), kept as c c at 0x487e34
                mode24_ = 2;
                color(&colors24_[0], true);
                std::memcpy(&colors24_[3], &colors24_[0], 3);
                [[fallthrough]];
            case 4:
                if (colors24_[0] | colors24_[1] | colors24_[2])   // 0x4884f2: black skips the block
                    for (int i = 0; i < 16; ++i) {
                        uint8_t* q = o + size_t(i >> 2) * stride + size_t(i & 3) * 3;
                        std::memcpy(q, &colors24_[0], 3);
                    }
                break;
            }
        }
}

void KdvVideo::toRgba(std::vector<uint8_t>& rgba) const {
    const size_t n = size_t(width_) * size_t(height_);
    rgba.resize(n * 4);
    for (size_t i = 0; i < n && i * (trueColor_ ? 3 : 1) < frame_.size(); ++i) {
        uint8_t* o = &rgba[i * 4];
        if (trueColor_) {
            const uint8_t* p = &frame_[i * 3];
            o[0] = p[2]; o[1] = p[1]; o[2] = p[0];
        } else {
            // 0x48b1a4: entries 1..254 from the KDV palette, 255 white;
            // entry 0 is never set by the player (INFERRED: black, the
            // cleared screen).
            const uint8_t k = frame_[i];
            if (k == 0) o[0] = o[1] = o[2] = 0;
            else if (k == 255) o[0] = o[1] = o[2] = 255;
            else { o[0] = palette_[k * 3u]; o[1] = palette_[k * 3u + 1]; o[2] = palette_[k * 3u + 2]; }
        }
        o[3] = 255;
    }
}

bool KdvVideo::takeAudio(int queued, Audio& out) {
    // 0x486d07: nothing pending, or 10 or more blocks queued (0x486d19):
    // nothing queued, and the pending chunks are dropped.
    if (pending_ == 0) return false;
    const int pending = pending_;
    pending_ = 0;
    if (queued >= 10) return false;
    if (stereo_) {
        // 0x486d2f: the oldest pending buffer, two SAMP blocks {"SAMP",
        // u32 length, u32 rate field, u8 shift, ...} each followed by its
        // samples; left and right interleaved.
        const std::vector<uint8_t>& b = ring_[size_t((ringAt_ - pending + 1) & 15)];
        if (b.size() < 0x10 || u32(b.data()) != tag("SAMP")) return false;   // DF.EXE queues stale bytes; nothing here
        const size_t len = u32(b.data() + 4);
        if (0x20 + 2 * len > b.size()) return false;
        out.lr.resize(2 * len);
        for (size_t i = 0; i < len; ++i) {
            out.lr[2 * i] = b[0x10 + i];
            out.lr[2 * i + 1] = b[0x20 + len + i];
        }
        out.shiftL = b[0xc];
        out.shiftR = b[0x10 + len + 0xc];
        return true;
    }
    // 0x486dc2: the newest buffer, one block played on both sides.
    const std::vector<uint8_t>& b = ring_[size_t(ringAt_)];
    if (b.size() < 0x10) return false;
    const size_t len = std::min<size_t>(u32(b.data() + 4), b.size() - 0x10);
    out.lr.resize(2 * len);
    for (size_t i = 0; i < len; ++i) out.lr[2 * i] = out.lr[2 * i + 1] = b[0x10 + i];
    out.shiftL = out.shiftR = b[0xc];
    return true;
}

} // namespace df
