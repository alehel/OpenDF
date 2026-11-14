#include "Image.h"

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
