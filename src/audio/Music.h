// Front-end music as DF.EXE plays it: DFMUSIC.SBF ("SBF0": 82 stems of
// unsigned 8-bit stereo at 22050 Hz in 0x1008-byte blocks with per-block
// attenuation shifts) sequenced by DFMUSIC.BIN ("SCR0", the compiled
// "DFMenu" script) on DF.EXE's small stack VM (0x4ab5ce). The menus talk to
// the script through Var00..Var15 (0x4b8570). See docs/FORMATS.md.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace df {

class Vfs;

class Music {
public:
    // `sbf`: the stems file on disk; the script from the game data.
    // `script`: DFMUSIC.BIN (Delta Force 2: DF2MUSIC.BIN, the same SCR0 scheme).
    bool load(Vfs& vfs, const std::filesystem::path& sbf, const char* script = "DFMUSIC.BIN");
    void start();                  // 0x404cc4 (all variables 0, state Begin)
    void stop();                   // 0x404c90
    void setVar(int n, int value); // 0x4b8570
    void setVolume(int v) { master_ = v; }   // cd_volume, 0..255
    // DF.EXE's integer units for Audio's DF mixer: (pcm − 128) · ((V >> 1) >> shift).
    void setIntegerUnits(bool on) { integer_ = on; }
    // Delta Force 2's VM: the switch opcode 0x35 (0x48b610). Both games:
    // the script's setVolume / fadeTo (0x33 8 / 9; GOG Df.exe 0x4c4fa0 /
    // 0x4c4fe0, DF2.EXE 0x48b74a / 0x48b75c), whose fade moves over one
    // stem (0x4c4ca0, 0x46bf86); the stream volume, set as each block is
    // queued (GOG Df.exe 0x4c4d20 -> 0x494810, DF2.EXE 0x46c050 ->
    // 0x470cf0, applied to the whole ring by 0x4b52dd / 0x4a0372), is
    // master · fade >> 8.
    void setDf2(bool on) { df2_ = on; }
    // Once per menu frame (0x4b8490 from the menu loop 0x406bf8): queues
    // at most one block while fewer than 13 are queued, or, when the stem
    // is fully read, runs one script tick to pick the next.
    void update();
    // Adds `frames` stereo frames at 22050 Hz into `out` (called by the mixer).
    void mix(float* out, int frames);

private:
    struct Stem { std::string name; uint32_t offset = 0, size = 0, block = 0x1008; };
    void tick();                   // one script tick: runs until a yield
    bool step();                   // one instruction; true when it yields (carry set)
    void play(int stem);
    uint32_t u32(size_t at) const;
    int32_t pop() { if (stack_.empty()) return 0; const int32_t v = stack_.back(); stack_.pop_back(); return v; }
    int32_t& global(int byteOffset) { return globals_[size_t(std::clamp(byteOffset / 4, 0, 17))]; }

    std::vector<uint8_t> sbf_, script_;
    std::vector<Stem> stems_;
    size_t track_ = 0;             // track header offset in the script
    std::vector<uint32_t> states_; // track-relative entry points
    std::array<int32_t, 18> globals_{};   // Var00..Var15, i, lastTime
    std::array<int32_t, 16> locals_{};
    std::vector<int32_t> stack_, returns_;
    size_t pc_ = 0;
    uint32_t seed_ = 0x0D1CFACE;
    bool running_ = false;
    int stem_ = -1;
    uint32_t read_ = 0, remaining_ = 0;   // read position / bytes left in the stem
    struct Block { size_t at; uint32_t frames; int shiftL, shiftR; };
    std::vector<Block> queue_;     // the ring (16 slots, at most 13 queued)
    uint32_t played_ = 0;          // frames played of queue_.front()
    int master_ = 255;
    bool df2_ = false;
    // DF2.EXE's fade (16.16): current 0xd86b5c, target 0xd86b60, step per
    // block 0xd86b64 (set when a stem starts, 0x46bf86); the stream volume.
    int32_t fade_ = 0xff0000, fadeTarget_ = 0xff0000, fadeStep_ = 0;
    int streamVol_ = 255;
    bool integer_ = false;
    bool debug_ = false;          // OPENDF_DEBUGMUSIC: print each PLAY
    std::mutex mutex_;
};

} // namespace df
