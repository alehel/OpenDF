// KDV video, as DF.EXE's "ANIMMGR" player reads it (0x486b0c open,
// 0x486c00 step, 0x487232 chunk loop, 0x487428 chunk dispatch, 0x487894
// 8-bit codec, 0x487e45 24-bit codec, 0x486d07 audio). A KDV file is a
// sequence of chunks {char tag[4], u32 size (header included)}:
//   xVDK  header: +8 width, +0xC height, +0x10 frame count, +0x14 frame
//         period in 120 Hz ticks, +0x18 version 0x22 (bit 31: 24-bit video)
//   pVDK  palette: 256 RGB triplets, 8 bits each
//   sVDK  stereo audio: two SAMP blocks (left, right); aVDK mono: one
//   iVDK  8-bit frame, i42K 24-bit frame: 4x4 blocks, delta-coded onto
//         the previous frame (see docs/FORMATS.md "KDV video")
// Decoding needs nothing but the file; timing, display and the sound queue
// are the player's (src/menu/Frontend.cpp playIntro).
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace df {

class KdvVideo {
public:
    // Opens the file and reads the leading xVDK header (and a pVDK right
    // after it), as 0x486e4a / 0x48700a. Returns false with `error` set.
    bool open(const std::filesystem::path& path);
    const std::string& error() const { return error_; }

    int width() const { return width_; }
    int height() const { return height_; }
    bool trueColor() const { return trueColor_; }   // header +0x18 bit 31 (24-bit, i42K)
    int framePeriod() const { return period_; }     // header +0x14, 120 Hz ticks per frame
    int frameCount() const { return frames_; }      // header +0x10 of the current clip

    enum class Step { Frame, End, Error };
    // 0x487232: reads chunks until an image chunk is decoded into the
    // frame buffer (Frame), the file is used up (End, 0x487827) or a chunk
    // is invalid (Error: a tag not ending in 'K', an unknown token, a
    // header of another version, a chunk past the end of the file).
    Step next();

    // The frame buffer the codec writes into (DF.EXE decodes straight into
    // its back buffer): width·height palette indices (8-bit) or
    // width·height·3 bytes B, G, R (24-bit). It keeps what earlier frames
    // drew; pixels a frame skips stay.
    const std::vector<uint8_t>& pixels() const { return frame_; }
    // pVDK (0x4874f8): 256 RGB triplets; DF.EXE sets entries 1..254 from it
    // and 255 to white (0x48b1a4). True once after a new palette.
    const std::array<uint8_t, 768>& palette() const { return palette_; }
    bool takePaletteChanged() { const bool c = paletteChanged_; paletteChanged_ = false; return c; }
    // The frame as RGBA (top row first) with DF.EXE's palette rules.
    void toRgba(std::vector<uint8_t>& rgba) const;

    // The audio DF.EXE queues for this frame (0x486d07): of the audio
    // chunks read since the last call only one: for stereo (sVDK) the
    // oldest, for mono (aVDK) the newest; the rest are dropped. `queued` is
    // the number of blocks in the mixer's stream queue: at 10 or more the
    // chunk is dropped too. Unsigned 8-bit samples, interleaved L R, at
    // 22050 Hz; shiftL/shiftR the SAMP blocks' +0xC attenuation shifts.
    struct Audio { std::vector<uint8_t> lr; int shiftL = 0, shiftR = 0; };
    bool takeAudio(int queued, Audio& out);

private:
    bool readChunk(std::vector<uint8_t>& chunk, Step& fail);
    int dispatch(const std::vector<uint8_t>& c);   // 0x487428: 1 continue, 0 image, -1 new size, -2 error
    void decode8(const uint8_t* s, const uint8_t* end);    // iVDK, 0x487894
    void decode24(const uint8_t* s, const uint8_t* end);   // i42K, 0x487e45 (the >= 24-bit display path)
    bool setError(const char* msg) { error_ = msg; return false; }

    std::ifstream file_;
    uint64_t size_ = 0, consumed_ = 0;
    std::string error_;
    int width_ = 0, height_ = 0, period_ = 0, frames_ = 0;
    bool trueColor_ = false;
    std::vector<uint8_t> frame_;
    std::array<uint8_t, 768> palette_{};
    bool paletteChanged_ = false;
    // Audio ring (0x486aa0, 16 buffers; 0x486ae0 index from 7, 0x486ae4
    // pending count, 0x486ae5 last chunk stereo).
    std::array<std::vector<uint8_t>, 16> ring_;
    int ringAt_ = 7, pending_ = 0;
    bool stereo_ = false;
    // 8-bit codec state kept between blocks (0x486ae7 last mode, ecx the
    // last block's colours).
    int mode8_ = 0;
    std::array<uint8_t, 4> colors8_{};
    // 24-bit codec state (0x487e44 last mode, 0x487e34..0x487e3f the block
    // colours, 0x487c30 the 128-entry colour cache, 0x487e30 its index).
    int mode24_ = 0;
    std::array<uint8_t, 12> colors24_{};
    std::array<std::array<uint8_t, 3>, 128> cache_{};
    int cacheAt_ = 0;
};

} // namespace df
