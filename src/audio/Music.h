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
    bool load(Vfs& vfs, const std::filesystem::path& sbf);
    void start();                  // 0x404cc4 (all variables 0, state Begin)
    void stop();                   // 0x404c90
    void setVar(int n, int value); // 0x4b8570
    void setVolume(int v) { master_ = v; }   // cd_volume, 0..255
    // Once per menu frame (0x4b8490 from the menu loop 0x406bf8): queues
    // at most one block while fewer than 13 are queued, or, when the stem
    // is fully read, runs one script tick to pick the next.
    void update();
    // Adds `frames` stereo frames at 22050 Hz into `out` (called by the mixer).
    void mix(float* out, int frames);

private:
    struct Stem { std::string name; uint32_t offset = 0, size = 0, block = 0x1008; };
    void tick();                   // one script tick: runs until a yield
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
    bool debug_ = false;          // OPENDF_DEBUGMUSIC: print each PLAY
    std::mutex mutex_;
};

} // namespace df
