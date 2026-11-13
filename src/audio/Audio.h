// Sound playback: WAV clips from the game data mixed into a stereo stream.
//
// The mixer runs either on an SDL audio device (live play) or is pulled in
// fixed steps by the game (--record, so the soundtrack of a recording is the
// same every time). What plays, how loud and where is decided by the game
// (DF.EXE's rules live in game/Sound.*); this is only the mixer.
#pragma once

#include <SDL.h>

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
    std::function<void(float*, int)> stream_;
};

} // namespace df
