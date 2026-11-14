// Sound playback: WAV clips from the game data mixed into a stereo stream.
//
// The mixer runs either on an SDL audio device (live play) or is pulled in
// fixed steps by the game (--record, so the soundtrack of a recording is the
// same every time). What plays, how loud and where is decided by the game
// (DF.EXE's rules live in game/Sound.*); this is only the mixer.
#pragma once

#include <SDL.h>
#include <array>

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
};

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
    };
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
    int32_t iprev_[2] = {0, 0};
    std::array<int32_t, 32> ring_{};
    int32_t ringSum_ = 0;
    int ringAt_ = 0;
    std::vector<int32_t> iscratch_;
    std::function<void(float*, int)> stream_;
};

} // namespace df
