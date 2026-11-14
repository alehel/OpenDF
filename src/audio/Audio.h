// Sound playback: WAV clips from the game data mixed into a stereo stream.
//
// The mixer runs either on an SDL audio device (live play) or is pulled in
// fixed steps by the game (--record, so the soundtrack of a recording is the
// same every time). What plays, how loud and where is decided by the game
// (DF.EXE's rules live in game/Sound.*); this is only the mixer.
#pragma once

#include <SDL.h>
#include <array>
#include <cstdio>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace df {

class Vfs;

struct SoundClip {
    std::vector<int16_t> samples;   // mono
    int rate = 22050;
    // The games' in-memory rate field (SAMP +8, DF.EXE 0x48cfb0 / DF2.EXE
    // 0x4711a0): (rate·16 / 22050) << 24, an integer divide. The integer
    // mixer steps by it (0x4a9a10 / 0x4a0092); 0 = derive it from `rate`.
    uint32_t rateField = 0;
};

// A RIFF WAV converted as the games convert a loaded file in place (DF.EXE
// 0x48cfb0, DF2.EXE 0x4711a0): the canonical 44-byte header is assumed
// (rate +0x18, bits +0x22, data size +0x28, data from +0x2c, whatever the
// chunks really are); 8-bit data is copied, 16-bit scaled to 8 bits by
// 0x7eff0000 / peak from the sample's low byte (DF1, `full16` false) or the
// whole word (DF2). `size` is how many bytes follow `d` in memory. Returns
// false if it is not a RIFF file of at least 0x2c bytes.
bool dfConvertWav(const uint8_t* d, size_t size, bool full16, SoundClip& out);

class Audio {
public:
    static constexpr int kRate = 22050;   // output rate (the game's WAVs are 22050 Hz)

    ~Audio();
    // Live output on the default device. Returns false if there is none.
    bool openDevice();
    void closeDevice();
    bool live() const { return device_ != 0; }

    // Decodes and caches a WAV from the game data; null if missing.
    const SoundClip* clip(Vfs& vfs, const std::string& name);

    // Starts a voice; returns its handle (> 0), or 0 if there was no voice
    // for it. As DF.EXE's mixer (0x4a99b0): 16 voices, 10-15 kept for
    // looping sounds (group 1); a one-shot (group 0) takes a free voice, or
    // else the first one-shot voice quieter than itself (`volume` 0..255),
    // or is dropped. `pitch` scales the clip's own rate.
    int play(const SoundClip* clip, float left, float right, float pitch = 1, int volume = 255, bool loop = false,
             int group = 0);
    void setGains(int voice, float left, float right);
    void setPitch(int voice, float pitch);
    void stop(int voice);
    bool playing(int voice) const;
    void stopAll();
    void setMasterVolume(float v) { master_ = v; }
    // DF.EXE's integer mixer (mixer.md; 0x4992dc, 0x497fda): each voice's
    // gain is DF's integer (pan · vol · 255) >> 16, rebuilt from the float
    // gains the game passes (pan · vol / (127 · 255)), multiplied through the
    // mixer's rounding table; the stream is in DF's units ((pcm − 128) ·
    // ((V >> 1) >> shift)); y = clamp(y[n−1] >> 2 + Σ, ±0x7fff). `hook`: the
    // startup L+R echo of mode 3 (0x4a965b), on until the first mission ends.
    void setDfMixer(bool on, bool hook) {
        std::lock_guard<std::mutex> lock(mutex_);
        dfMixer_ = on;
        if (hook != hook_) { ring_.fill(0); ringSum_ = 0; ringAt_ = 0; }
        hook_ = hook;
        iprev_[0] = iprev_[1] = 0;
    }
    // Which game's sound code runs: 0 none (SDL's WAV decoder, float
    // steps), 1 Delta Force (DF.EXE), 2 Delta Force 2 (DF2.EXE). It picks
    // the loose-WAV conversion of clips decoded from now on (dfConvertWav)
    // and, with the integer mixer on, DF2's mixer differences: a buffer
    // mixed in four parts with each voice's pitch sliding a quarter of the
    // way per part (0x48e05a, 0x48f818, 0x48e531), gains flipped in sign
    // when that is nearer the old ones (0x49fcba), updates to a voice not
    // yet started ignored (0x49fbf4).
    void setDfGame(int game) {
        std::lock_guard<std::mutex> lock(mutex_);
        dfGame_ = game;
        fifo_.clear();
        fifoAt_ = 0;
    }
    // Testing: OPENDF_AUDIODUMP=FILE makes openDevice() open no device and
    // instead the caller's advance(seconds) mixes that much (at kRate, the
    // fractions carried) and appends it to FILE as raw s16le stereo.
    // Returns true while dumping.
    bool advance(double seconds);
    // A stereo stream added to the mix (the front-end music); it adds
    // `frames` interleaved float frames at kRate into its buffer.
    void setStream(std::function<void(float*, int)> stream) {
        std::lock_guard<std::mutex> lock(mutex_);
        stream_ = std::move(stream);
    }

    // Mixes `frames` stereo frames (interleaved s16) at kRate.
    void mix(int16_t* out, int frames);

private:
    struct Voice {
        int id = 0;
        const SoundClip* clip = nullptr;
        uint64_t pos = 0;     // 32.32 fixed point sample position
        uint64_t step = 0;
        float left = 1, right = 1;
        bool loop = false;
        int volume = 0;
        int group = 0;
        // The integer mixer's state (DF.EXE voice +0x24/+0x28/+8, DF2.EXE
        // +0x24/+0x28, +8 current step, +0x34 target, +0x38 per-part delta,
        // +0x3e updated): gains as the games' integers, steps in 1/1024
        // samples.
        int gL = 0, gR = 0;
        int32_t stepCur = 0, stepTarget = 0, stepDelta = 0;
        bool updated = false;
        bool started = false;   // DF2: mixed at least once (else still pending)
    };
    void dfMixBlock(int frames);   // DF2.EXE 0x48e028 over one call's frames
    void dfVoices(int32_t* acc, int from, int frames);
    void dfOutput(const int32_t* acc, int16_t* out, int frames);
    static void callback(void* self, Uint8* stream, int len);

    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    std::map<std::string, std::unique_ptr<SoundClip>> clips_;
    SDL_AudioDeviceID device_ = 0;
    int nextId_ = 1;
    static constexpr int kOneShotVoices = 10, kLoopVoices = 6;
    float master_ = 1;
    std::vector<float> scratch_;
    float feedback_[2] = {0, 0};   // the last output sample per channel (0x497fda)
    bool dfMixer_ = false, hook_ = false;
    int dfGame_ = 0;
    std::vector<int16_t> fifo_;   // DF2: mixed blocks not yet handed out
    size_t fifoAt_ = 0;
    FILE* dump_ = nullptr;
    double dumpCarry_ = 0;
    int32_t iprev_[2] = {0, 0};
    std::array<int32_t, 32> ring_{};
    int32_t ringSum_ = 0;
    int ringAt_ = 0;
    std::vector<int32_t> iscratch_;
    std::function<void(float*, int)> stream_;
};

} // namespace df
