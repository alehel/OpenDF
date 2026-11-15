#include "Image.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

#ifdef OPENDF_HAVE_JPEG
#include <jpeglib.h>
#include <csetjmp>
#endif

namespace df {

bool decodeTga(const std::vector<uint8_t>& d, RgbaImage& out) {
    // 18-byte header: id length, colour-map type, image type (2 = true
    // colour), colour-map spec (5), origin (4), width, height, bpp,
    // descriptor (bit 5 = top-down).
    if (d.size() < 18) return false;
    const int idLen = d[0], cmapType = d[1], type = d[2];
    const int w = d[12] | d[13] << 8, h = d[14] | d[15] << 8, bpp = d[16], desc = d[17];
    if (cmapType != 0 || type != 2 || (bpp != 24 && bpp != 32) || w <= 0 || h <= 0) return false;
    const size_t px = size_t(bpp / 8), start = 18 + size_t(idLen);
    if (d.size() < start + size_t(w) * size_t(h) * px) return false;
    out.width = w;
    out.height = h;
    out.rgba.resize(size_t(w) * size_t(h) * 4);
    const bool topDown = (desc & 0x20) != 0;
    for (int y = 0; y < h; ++y) {
        const int row = topDown ? y : h - 1 - y;
        for (int x = 0; x < w; ++x) {
            const uint8_t* s = &d[start + (size_t(y) * size_t(w) + size_t(x)) * px];
            uint8_t* t = &out.rgba[(size_t(row) * size_t(w) + size_t(x)) * 4];
            t[0] = s[2];
            t[1] = s[1];
            t[2] = s[0];
            t[3] = px == 4 ? s[3] : 255;
        }
    }
    return true;
}

#ifdef OPENDF_HAVE_JPEG
namespace {
struct JpegError {
    jpeg_error_mgr mgr;
    std::jmp_buf jump;
};
void jpegFail(j_common_ptr c) { std::longjmp(reinterpret_cast<JpegError*>(c->err)->jump, 1); }
}   // namespace
#endif

bool decodeJpeg(const std::vector<uint8_t>& d, RgbaImage& out) {
#ifdef OPENDF_HAVE_JPEG
    jpeg_decompress_struct c;
    JpegError err;
    c.err = jpeg_std_error(&err.mgr);
    err.mgr.error_exit = jpegFail;
    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&c);
        return false;
    }
    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, const_cast<unsigned char*>(d.data()), static_cast<unsigned long>(d.size()));
    if (jpeg_read_header(&c, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&c);
        return false;
    }
    c.out_color_space = JCS_RGB;
    jpeg_start_decompress(&c);
    out.width = int(c.output_width);
    out.height = int(c.output_height);
    out.rgba.assign(size_t(out.width) * size_t(out.height) * 4, 255);
    std::vector<uint8_t> row(size_t(out.width) * 3);
    while (c.output_scanline < c.output_height) {
        unsigned char* rows[1] = {row.data()};
        const int y = int(c.output_scanline);
        jpeg_read_scanlines(&c, rows, 1);
        for (int x = 0; x < out.width; ++x)
            for (int k = 0; k < 3; ++k) out.rgba[(size_t(y) * size_t(out.width) + size_t(x)) * 4 + size_t(k)] = row[size_t(x) * 3 + size_t(k)];
    }
    jpeg_finish_decompress(&c);
    jpeg_destroy_decompress(&c);
    return true;
#else
    (void)d;
    (void)out;
    return false;
#endif
}

#ifdef OPENDF_HAVE_JPEG
namespace {

// libjpeg's islow fixed-point constants (13 fractional bits), as Df2.exe's
// copy uses them.
constexpr int kF0_298 = 2446, kF0_390 = 3196, kF0_541 = 4433, kF0_765 = 6270, kF0_899 = 7373, kF1_175 = 9633,
              kF1_501 = 12299, kF1_847 = 15137, kF1_961 = 16069, kF2_053 = 16819, kF2_562 = 20995, kF3_072 = 25172;

// Clamp of a sample offset by +128, indexed by (value & 0x3ff) as the EXE's
// range table 0x50d6e4 (built at 0x481140): 0..127 -> 128..255, 128..511 ->
// 255, 512..895 -> 0, 896..1023 -> 0..127.
uint8_t idctLimit(int v) {
    v &= 0x3ff;
    if (v < 128) return uint8_t(v + 128);
    if (v < 512) return 255;
    if (v < 896) return 0;
    return uint8_t(v - 896);
}

// GOG Df2.exe 0x481220 (CD 0x476310): the integer IDCT of one 8x8 block,
// coefficients and quantisers in natural order. The column pass takes its
// row-6 term from index c+54 (+0x6c bytes, 0x4813ec / 0x4813f8) instead of
// c+48; the zero test and the row pass are libjpeg's.
void df2Idct(const JCOEF* coef, const UINT16* q, uint8_t out[64]) {
    int ws[64];
    for (int c = 0; c < 8; ++c) {
        auto dq = [&](int i) { return int(coef[i]) * int(q[i]); };
        if (!coef[c + 8] && !coef[c + 16] && !coef[c + 24] && !coef[c + 32] && !coef[c + 40] && !coef[c + 48] && !coef[c + 56]) {
            const int dc = dq(c) * 4;
            for (int r = 0; r < 8; ++r) ws[r * 8 + c] = dc;
            continue;
        }
        const int z2 = dq(c + 16), z3 = dq(c + 54);   // the slip: c + 54
        const int z1 = (z2 + z3) * kF0_541;
        const int t2 = z1 - z3 * kF1_847, t3 = z1 + z2 * kF0_765;
        const int t0 = (dq(c) + dq(c + 32)) * 8192, t1 = (dq(c) - dq(c + 32)) * 8192;
        const int e0 = t0 + t3, e3 = t0 - t3, e1 = t1 + t2, e2 = t1 - t2;
        const int o0 = dq(c + 56), o1 = dq(c + 40), o2 = dq(c + 24), o3 = dq(c + 8);
        const int z5 = (o0 + o1 + o2 + o3) * kF1_175;
        const int za = (o0 + o3) * -kF0_899, zb = (o1 + o2) * -kF2_562;
        const int zc = (o0 + o2) * -kF1_961 + z5, zd = (o1 + o3) * -kF0_390 + z5;
        const int p0 = o0 * kF0_298 + za + zc, p1 = o1 * kF2_053 + zb + zd;
        const int p2 = o2 * kF3_072 + zb + zc, p3 = o3 * kF1_501 + za + zd;
        ws[c] = (e0 + p3 + 0x400) >> 11;
        ws[56 + c] = (e0 - p3 + 0x400) >> 11;
        ws[8 + c] = (e1 + p2 + 0x400) >> 11;
        ws[48 + c] = (e1 - p2 + 0x400) >> 11;
        ws[16 + c] = (e2 + p1 + 0x400) >> 11;
        ws[40 + c] = (e2 - p1 + 0x400) >> 11;
        ws[24 + c] = (e3 + p0 + 0x400) >> 11;
        ws[32 + c] = (e3 - p0 + 0x400) >> 11;
    }
    for (int r = 0; r < 8; ++r) {
        const int* w = ws + r * 8;
        const int z1 = (w[2] + w[6]) * kF0_541;
        const int t2 = z1 - w[6] * kF1_847, t3 = z1 + w[2] * kF0_765;
        const int t0 = (w[0] + w[4]) * 8192, t1 = (w[0] - w[4]) * 8192;
        const int e0 = t0 + t3, e3 = t0 - t3, e1 = t1 + t2, e2 = t1 - t2;
        const int o0 = w[7], o1 = w[5], o2 = w[3], o3 = w[1];
        const int z5 = (o0 + o1 + o2 + o3) * kF1_175;
        const int za = (o0 + o3) * -kF0_899, zb = (o1 + o2) * -kF2_562;
        const int zc = (o0 + o2) * -kF1_961 + z5, zd = (o1 + o3) * -kF0_390 + z5;
        const int p0 = o0 * kF0_298 + za + zc, p1 = o1 * kF2_053 + zb + zd;
        const int p2 = o2 * kF3_072 + zb + zc, p3 = o3 * kF1_501 + za + zd;
        uint8_t* o = out + r * 8;
        o[0] = idctLimit((e0 + p3 + 0x20000) >> 18);
        o[7] = idctLimit((e0 - p3 + 0x20000) >> 18);
        o[1] = idctLimit((e1 + p2 + 0x20000) >> 18);
        o[6] = idctLimit((e1 - p2 + 0x20000) >> 18);
        o[2] = idctLimit((e2 + p1 + 0x20000) >> 18);
        o[5] = idctLimit((e2 - p1 + 0x20000) >> 18);
        o[3] = idctLimit((e3 + p0 + 0x20000) >> 18);
        o[4] = idctLimit((e3 - p0 + 0x20000) >> 18);
    }
}

}   // namespace
#endif

// GOG Df2.exe 0x482310 (CD 0x477400): baseline JFIF only, three components.
// The entropy decoding is standard, so libjpeg supplies the quantised
// coefficients; each block goes through 0x481220, the chroma is sampled
// nearest (x / (maxH / H), y / (maxV / V), component 1's ratios for both)
// and the colour is the 0x481080 tables: R = Y + Cr*1.402, G = Y +
// ((Cb*-0.34414 + 0.5 + Cr*-0.71414) >> 16), B = Y + Cb*1.772, clamped.
bool decodeJpegDf2(const std::vector<uint8_t>& d, RgbaImage& out) {
#ifdef OPENDF_HAVE_JPEG
    jpeg_decompress_struct c;
    JpegError err;
    c.err = jpeg_std_error(&err.mgr);
    err.mgr.error_exit = jpegFail;
    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&c);
        return false;
    }
    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, const_cast<unsigned char*>(d.data()), static_cast<unsigned long>(d.size()));
    if (jpeg_read_header(&c, TRUE) != JPEG_HEADER_OK || c.num_components != 3 || c.progressive_mode) {
        jpeg_destroy_decompress(&c);
        return decodeJpeg(d, out);
    }
    jvirt_barray_ptr* coefs = jpeg_read_coefficients(&c);
    const int W = int(c.image_width), H = int(c.image_height);
    // Each component's samples at its own resolution (whole blocks).
    std::vector<uint8_t> planes[3];
    int pw[3] = {}, ph[3] = {};
    for (int k = 0; k < 3; ++k) {
        jpeg_component_info& ci = c.comp_info[k];
        const int bw = int((ci.width_in_blocks + JDIMENSION(ci.h_samp_factor) - 1) / JDIMENSION(ci.h_samp_factor)) * ci.h_samp_factor;
        const int bh = int((ci.height_in_blocks + JDIMENSION(ci.v_samp_factor) - 1) / JDIMENSION(ci.v_samp_factor)) * ci.v_samp_factor;
        pw[k] = bw * 8;
        ph[k] = bh * 8;
        planes[k].assign(size_t(pw[k]) * size_t(ph[k]), 0);
        if (!ci.quant_table) {
            jpeg_destroy_decompress(&c);
            return false;
        }
        const UINT16* q = ci.quant_table->quantval;
        for (int by = 0; by < bh; ++by) {
            JBLOCKARRAY row = (*c.mem->access_virt_barray)(reinterpret_cast<j_common_ptr>(&c), coefs[k], JDIMENSION(by), 1, FALSE);
            for (int bx = 0; bx < bw; ++bx) {
                uint8_t px[64];
                df2Idct(row[0][bx], q, px);
                for (int r = 0; r < 8; ++r)
                    std::memcpy(&planes[k][size_t(by * 8 + r) * size_t(pw[k]) + size_t(bx) * 8], px + r * 8, 8);
            }
        }
    }
    int maxH = 1, maxV = 1;
    for (int k = 0; k < 3; ++k) {
        maxH = std::max(maxH, c.comp_info[k].h_samp_factor);
        maxV = std::max(maxV, c.comp_info[k].v_samp_factor);
    }
    if (c.comp_info[0].h_samp_factor != maxH || c.comp_info[0].v_samp_factor != maxV) {
        // The EXE reads luma as the full-resolution component; never so in the data.
        jpeg_destroy_decompress(&c);
        return decodeJpeg(d, out);
    }
    const int rx = maxH / c.comp_info[1].h_samp_factor, ry = maxV / c.comp_info[1].v_samp_factor;
    auto clamp = [](int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); };
    out.width = W;
    out.height = H;
    out.rgba.assign(size_t(W) * size_t(H) * 4, 255);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int Y = planes[0][size_t(y) * size_t(pw[0]) + size_t(x)];
            const int cx = x / rx, cy = y / ry;
            const int cb = int(planes[1][size_t(std::min(cy, ph[1] - 1)) * size_t(pw[1]) + size_t(std::min(cx, pw[1] - 1))]) - 128;
            const int cr = int(planes[2][size_t(std::min(cy, ph[2] - 1)) * size_t(pw[2]) + size_t(std::min(cx, pw[2] - 1))]) - 128;
            uint8_t* p = &out.rgba[(size_t(y) * size_t(W) + size_t(x)) * 4];
            p[0] = clamp(Y + ((cr * 91881 + 32768) >> 16));
            p[1] = clamp(Y + ((cb * -22554 + 32768 + cr * -46802) >> 16));
            p[2] = clamp(Y + ((cb * 116130 + 32768) >> 16));
        }
    jpeg_finish_decompress(&c);
    jpeg_destroy_decompress(&c);
    return true;
#else
    (void)d;
    (void)out;
    return false;
#endif
}

bool decodeRgbaImageDf2(const std::vector<uint8_t>& d, RgbaImage& out) {
    if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xD8) return decodeJpegDf2(d, out);
    return decodeTga(d, out);
}

bool jpegSupported() {
#ifdef OPENDF_HAVE_JPEG
    return true;
#else
    return false;
#endif
}

bool decodeRgbaImage(const std::vector<uint8_t>& d, RgbaImage& out) {
    if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xD8) return decodeJpeg(d, out);
    return decodeTga(d, out);
}

} // namespace df
