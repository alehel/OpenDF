// Sound playback: WAV clips from the game data mixed into a stereo stream.
//
// The mixer runs either on an SDL audio device (live play) or is pulled in
// fixed steps by the game (--record, so the soundtrack of a recording is the
// same every time). What plays, how loud and where is decided by the game
// (DF.EXE's rules live in game/Sound.*); this is only the mixer.
#pragma once

#include <SDL.h>

#include <cstdint>
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

    // Starts a voice; returns its handle (> 0), or 0 if it could not get a
    // voice. `priority`: a new voice may take the place of the lowest-
    // priority (then oldest) voice when all are busy, if it is not lower.
    int play(const SoundClip* clip, float left, float right, bool loop = false, int priority = 0);
    void setGains(int voice, float left, float right);
    void stop(int voice);
    bool playing(int voice) const;
    void stopAll();
    // Maximum simultaneous voices.
    void setVoices(int n) { maxVoices_ = n; }
    void setMasterVolume(float v) { master_ = v; }

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
        int priority = 0;
        uint64_t started = 0;
    };
    static void callback(void* self, Uint8* stream, int len);

    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    std::map<std::string, std::unique_ptr<SoundClip>> clips_;
    SDL_AudioDeviceID device_ = 0;
    int nextId_ = 1;
    int maxVoices_ = 16;
    float master_ = 1;
    uint64_t counter_ = 0;
    std::vector<float> scratch_;
};

} // namespace df
