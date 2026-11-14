// NovaLogic PWF2 sound banks: SNIPER.PWF (all game sounds and their
// definitions) and DFDLGnn.PWF (dialog speech). Layout from DF.EXE's loader
// (0x49218c) and player (0x492210); see docs/FORMATS.md.
#pragma once

#include "Audio.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace df {

// One way a layer can sound: a sample with its pitch and loudness.
struct SoundVariant {
    int sample = -1;
    float pitchBase = 1, pitchRand = 0;   // pitch = base + rand(range)
    int gain = 255, cap = 255;            // volume = min(gain * curve >> 8, cap)
};

// Layers of a sound play together (e.g. the near crack and the far boom of
// an explosion); each picks one of its variants.
struct SoundLayer {
    float range = 0;                      // metres to silence (0: full volume)
    uint32_t flags = 0;                   // 0x1 positional pan, 0x10 variants in turn (else random)
    std::vector<SoundVariant> variants;
    int next = 0;                         // round-robin position
    int shuffleStart = 0;                 // DF2 flag 0x80: the random start (+0xC) ...
    bool shuffling = false;               // ... and runtime bit 0x100
};

struct SoundDef {
    std::string name;
    float pitchBase = 1, pitchRand = 0;
    float maxDistance = 0;                // metres: not played beyond
    uint32_t flags = 0;
    std::vector<SoundLayer> layers;
};

class Pwf {
public:
    bool load(std::vector<uint8_t> file, std::string* error = nullptr);
    // Delta Force 2's converter (DF2.EXE 0x4711A0) reads the whole 16-bit
    // sample; DF1's (0x48cfb0) its low byte.
    bool full16 = false;
    int sampleCount() const { return int(samples_.size()); }
    int sampleIndex(const std::string& name) const;
    const std::string& sampleName(int i) const { return samples_[size_t(i)].name; }
    // Decoded sample (cached).
    const SoundClip* clip(int sample);
    // Sound definition by name (case-insensitive); null if none.
    SoundDef* def(const std::string& name);

private:
    struct Entry { std::string name; uint32_t offset = 0, size = 0; };
    std::vector<uint8_t> data_;
    std::vector<Entry> samples_;
    std::map<std::string, int> byName_;
    std::map<std::string, SoundDef> defs_;
    std::map<int, SoundClip> clips_;
};

} // namespace df
