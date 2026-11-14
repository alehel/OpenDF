// Bink 1 video, RAD Game Tools' format (DF2INTRO.BIK, DFLWINT.BIK). This is
// not an EXE trace: Delta Force 2 and Land Warrior never decode Bink
// themselves, they run RAD's binkplay.exe on the file (DF2.EXE 0x435830).
// The decoder follows the format (docs/FORMATS.md "Bink video"); its
// constant tables (scan order, run patterns, Huffman trees, quantisers) are
// RAD's, read out of binkplay.exe's data. A file is
//   header  "BIK" + revision letter, u32 file size - 8, u32 frames, u32
//           largest frame, u32 frames, u32 width, u32 height, u32 rate,
//           u32 scale (frames per second = rate / scale), u32 video flags
//           (bit 20 alpha plane, bit 17 grey), u32 audio tracks; then per
//           track u32 largest decoded packet, per track u16 sample rate
//           + u16 flags (bit 13 stereo, bit 12 DCT transform instead of
//           RDFT), per track u32 track id
//   index   frames + 1 u32 file offsets (bit 0: keyframe)
//   frame   per audio track u32 size + packet, then the video packet
// Decoding needs nothing but the file; timing, display and the sound queue
// are the player's (src/menu/FrontendDf2.cpp playBinkVideo).
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace df {

class BinkVideo {
public:
    // Reads the header and the frame index. Revisions 'c'..'k' of Bink 1
    // (the 'b' revision's different bundles are not implemented).
    bool open(const std::filesystem::path& path);
    const std::string& error() const { return error_; }

    char revision() const { return revision_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int frameCount() const { return int(index_.size()) - 1; }
    int rate() const { return rate_; }     // frames per second = rate / scale
    int scale() const { return scale_; }
    bool hasAlpha() const { return alpha_; }
    // The first audio track (the only one the intros have).
    bool hasAudio() const { return audioRate_ > 0; }
    int audioRate() const { return audioRate_; }
    int audioChannels() const { return audioChannels_; }
    bool audioDct() const { return audioDct_; }

    enum class Step { Frame, End, Error };
    // Reads the next frame: decodes its first audio track's packet (kept
    // for takeAudio) and its video packet into the planes.
    Step next();
    int frame() const { return frame_; }       // the frame next() decoded
    bool keyframe() const { return key_; }

    // The decoded planes: 0 Y, 1 U (Cb), 2 V (Cr) at half size, 3 alpha.
    const uint8_t* plane(int p) const { return cur_[size_t(p)].data(); }
    int stride(int p) const { return stride_[size_t(p)]; }
    // The frame as RGBA (top row first), BT.601 studio range (INFERRED:
    // the converter binkplay uses was not traced).
    void toRgba(std::vector<uint8_t>& rgba) const;

    // Appends the audio decoded since the last call: signed 16-bit,
    // interleaved, audioChannels() channels at audioRate().
    void takeAudio(std::vector<int16_t>& out) {
        out.insert(out.end(), pcm_.begin(), pcm_.end());
        pcm_.clear();
    }
    // Decoded sample count the packets reported (their first u32, in bytes)
    // and the count decoded, for checking.
    uint64_t audioReported() const { return reported_; }
    uint64_t audioDecoded() const { return decoded_; }

    struct Bits;   // the LSB-first bit reader (Bink.cpp)

private:
    // A Huffman tree as a frame codes it: one of the 16 fixed trees and
    // the order of its 16 symbols.
    struct Tree { int table = 0; std::array<uint8_t, 16> syms{}; };
    // A bundle: values a row of blocks reads in order, refilled per row
    // while the plane's data lasts.
    struct Bundle {
        std::vector<int32_t> data;
        size_t dec = 0, ptr = 0;
        bool done = false;
        int len = 0;   // bits of the per-row value count
        Tree tree;
    };
    bool decodeVideo(const uint8_t* d, size_t size);
    bool decodePlane(Bits& b, int p, bool chroma);
    bool readBundleRow(Bits& b, int i);
    int value(int i);
    void decodeAudio(const uint8_t* d, size_t size);
    bool audioBlock(Bits& b);
    bool setError(const std::string& msg) { error_ = msg; return false; }

    std::ifstream file_;
    std::string error_;
    char revision_ = 0;
    int width_ = 0, height_ = 0, rate_ = 0, scale_ = 1;
    bool alpha_ = false, gray_ = false;
    int tracks_ = 0;
    std::vector<uint32_t> index_;
    int frame_ = -1;
    bool key_ = false;
    std::vector<uint8_t> packet_;

    // Video: two frame buffers per plane (the one being decoded and the
    // previous one the skip and motion blocks copy from).
    std::array<std::vector<uint8_t>, 4> cur_, prev_;
    std::array<int, 4> stride_{}, rows_{};
    bool haveRef_ = false;
    std::array<Bundle, 9> bundles_;
    std::array<Tree, 16> colHigh_;
    int colLast_ = 0;

    // Audio.
    int audioRate_ = 0, audioChannels_ = 0;
    bool audioDct_ = false;
    int chans_ = 1;               // channels the transform runs on (1 for RDFT: interleaved)
    int frameLen_ = 0, overlap_ = 0, bands_ = 0;
    std::array<int, 26> band_{};
    std::array<float, 96> quant_{};
    float root_ = 0;
    bool first_ = true;
    std::array<std::vector<float>, 2> coeffs_, prevOut_;
    std::vector<float> fft_;      // scratch, complex interleaved
    std::vector<int16_t> pcm_;
    uint64_t reported_ = 0, decoded_ = 0;
};

} // namespace df
