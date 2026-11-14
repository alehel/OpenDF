#include "Audio.h"

#include "../core/Pff.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace df {

Audio::~Audio() { closeDevice(); }

bool Audio::openDevice() {
    if (device_) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec want{}, have{};
    want.freq = kRate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = &Audio::callback;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device_) return false;
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void Audio::closeDevice() {
    if (device_) SDL_CloseAudioDevice(device_);
    device_ = 0;
}

void Audio::callback(void* self, Uint8* stream, int len) {
    static_cast<Audio*>(self)->mix(reinterpret_cast<int16_t*>(stream), len / 4);
}

const SoundClip* Audio::clip(Vfs& vfs, const std::string& name) {
    std::string key = name;
    for (auto& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (key.find('.') == std::string::npos) key += ".WAV";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = clips_.find(key); it != clips_.end()) return it->second.get();
    }
    std::unique_ptr<SoundClip> out;
    std::vector<uint8_t> data;
    if (vfs.read(key, data) && !data.empty()) {
        SDL_AudioSpec spec{};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        SDL_RWops* rw = SDL_RWFromConstMem(data.data(), int(data.size()));
        bool df1 = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            df1 = df1Wavs_;
        }
        if (df1 && data.size() >= 0x2c && std::memcmp(data.data(), "RIFF", 4) == 0 && (data[0x22] | data[0x23] << 8) == 16) {
            // DF.EXE 0x48cfb0 converts a loose WAV in place, reading the
            // canonical 44-byte header: rate +0x18, bits +0x22, data size
            // +0x28, data at +0x2c. 16-bit data (0x48d03e): n = size >> 1;
            // peak = max |s| (0x48d05f, the word negated); scale = 0x7eff0000
            // / peak (unsigned; a silent clip would fault); then per sample
            // ((int8 low byte · scale) + 0x800000) >> 24 (logical, low 32
            // bits) ^ 0x80 (0x48d093: `movsx eax, byte ptr [esi]` reads the
            // low byte, not the high one). The mixer reads unsigned 8-bit
            // (0x498045). On the shipped data (INST177/INST178, peak 32768,
            // scale 0xfdfe) every byte comes out 0x80: silent. 8-bit data is
            // copied as it is, so it keeps SDL's path below.
            const auto u32 = [&](size_t o) { return uint32_t(data[o]) | uint32_t(data[o + 1]) << 8 | uint32_t(data[o + 2]) << 16 | uint32_t(data[o + 3]) << 24; };
            const uint32_t bytes = std::min<uint32_t>(u32(0x28), uint32_t(data.size() - 0x2c));
            const size_t n = bytes >> 1;
            uint32_t peak = 0;
            for (size_t i = 0; i < n; ++i) {
                const uint16_t w = uint16_t(data[0x2c + 2 * i] | data[0x2d + 2 * i] << 8);
                peak = std::max<uint32_t>(peak, (w & 0x8000) ? uint16_t(-w) : w);
            }
            out = std::make_unique<SoundClip>();
            out->rate = int(u32(0x18));
            out->samples.resize(n);
            const uint32_t scale = peak ? 0x7eff0000u / peak : 0;   // DF.EXE divides by zero here
            for (size_t i = 0; i < n; ++i) {
                const int32_t lo = int8_t(data[0x2c + 2 * i]);
                const uint8_t b = uint8_t(((uint32_t(lo * int64_t(scale)) + 0x800000u) >> 24) ^ 0x80u);
                out->samples[i] = int16_t((int(b) - 128) * 256);   // U8 -> S16, as SDL converts the 8-bit clips
            }
            SDL_RWclose(rw);
        } else if (SDL_LoadWAV_RW(rw, 1, &spec, &buf, &len)) {
            // To signed 16-bit mono at the clip's own rate.
            SDL_AudioCVT cvt;
            if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 1, spec.freq) >= 0) {
                std::vector<Uint8> work(size_t(len) * size_t(std::max(1, cvt.len_mult)));
                std::memcpy(work.data(), buf, len);
                cvt.buf = work.data();
                cvt.len = int(len);
                if (cvt.needed) SDL_ConvertAudio(&cvt);
                const int bytes = cvt.needed ? cvt.len_cvt : int(len);
                out = std::make_unique<SoundClip>();
                out->rate = spec.freq;
                out->samples.resize(size_t(bytes) / 2);
                std::memcpy(out->samples.data(), work.data(), out->samples.size() * 2);
            }
            SDL_FreeWAV(buf);
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = clips_[key];
    slot = std::move(out);
    return slot.get();
}

int Audio::play(const SoundClip* clip, float left, float right, float pitch, int volume, bool loop, int group) {
    if (!clip || clip->samples.empty() || pitch <= 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    Voice v;
    v.id = nextId_++;
    v.clip = clip;
    v.step = uint64_t(double(clip->rate) * pitch / kRate * 4294967296.0);
    v.left = left;
    v.right = right;
    v.loop = loop;
    v.volume = volume;
    v.group = group;
    const int limit = group ? kLoopVoices : kOneShotVoices;
    int busy = 0;
    for (const auto& o : voices_) busy += o.group == group;
    if (busy < limit) {
        voices_.push_back(v);
        return v.id;
    }
    if (group) return 0;
    for (auto& o : voices_)
        if (o.group == 0 && o.volume < volume) {
            o = v;
            return v.id;
        }
    return 0;
}

void Audio::setGains(int voice, float left, float right) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_)
        if (v.id == voice) { v.left = left; v.right = right; }
}

void Audio::setPitch(int voice, float pitch) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_)
        if (v.id == voice) v.step = uint64_t(double(v.clip->rate) * pitch / kRate * 4294967296.0);
}

void Audio::stop(int voice) {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == voice; }), voices_.end());
}

bool Audio::playing(int voice) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& v : voices_)
        if (v.id == voice) return true;
    return false;
}

void Audio::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.clear();
}

namespace {
// DF.EXE's per-voice multiply (0x4992dc, tables 0x4973c0 / 0x497280): the
// gain's shift class and a 64-entry multiplier table (M[29] = 28 is DF's).
int dfContrib(int g, int s) {
    static const int M[64] = {-27, -27, -27, -27, -27, -27, -25, -25, -24, -23, -21, -21, -20, -19, -18, -17,
                              -16, -15, -13, -13, -12, -11, -10, -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,
                              0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,
                              16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  28,  30,  31};
    const int a = g < 0 ? -g : g;
    const int cl = g == -128 ? 0 : a >= 64 ? 1 : a >= 32 ? 2 : a >= 16 ? 3 : a >= 8 ? 4 : a >= 4 ? 5 : 6;
    const int idx = (g * (1 << cl)) >> 3;   // arithmetic
    const int p = s * M[std::clamp(idx, -32, 31) + 32];
    return cl <= 3 ? p * (1 << (3 - cl)) : p >> (cl - 3);
}
} // namespace

void Audio::mix(int16_t* out, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dfMixer_) {
        iscratch_.assign(size_t(frames) * 2, 0);
        for (auto& v : voices_) {
            const auto& smp = v.clip->samples;
            const uint64_t end = uint64_t(smp.size()) << 32;
            // The float gains are pan · vol / (127 · 255): pan · vol back, then DF's integer.
            auto gi = [](float f) { const long pv = std::lround(double(f) * 127.0 * 255.0); return int(std::clamp<long>((pv * 255) >> 16, -128, 127)); };
            const int gL = gi(v.left), gR = gi(v.right);
            for (int i = 0; i < frames; ++i) {
                if (v.pos >= end) {
                    if (!v.loop) break;
                    v.pos %= end;
                }
                const int sv = smp[size_t(v.pos >> 32)] >> 8;   // the 8-bit sample, byte − 0x80
                iscratch_[size_t(i) * 2] += dfContrib(gL, sv);
                iscratch_[size_t(i) * 2 + 1] += dfContrib(gR, sv);
                v.pos += v.step;
            }
        }
        if (stream_) {
            scratch_.assign(size_t(frames) * 2, 0.f);
            stream_(scratch_.data(), frames);   // in DF's integer units
            for (size_t i = 0; i < scratch_.size(); ++i) iscratch_[i] += int32_t(std::lround(scratch_[i]));
        }
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                     [](const Voice& v) { return !v.loop && v.pos >= (uint64_t(v.clip->samples.size()) << 32); }),
                      voices_.end());
        for (int i = 0; i < frames; ++i) {
            int32_t L = (iprev_[0] >> 2) + iscratch_[size_t(i) * 2], R = (iprev_[1] >> 2) + iscratch_[size_t(i) * 2 + 1];
            if (hook_) {
                // Mode 3 (0x4a965b): the last 32 L+R sums, sum >> 8 added to both.
                const int32_t x = L + R;
                ringSum_ += x - ring_[size_t(ringAt_)];
                ring_[size_t(ringAt_)] = x;
                ringAt_ = (ringAt_ + 1) & 31;
                L += ringSum_ >> 8;
                R += ringSum_ >> 8;
            }
            L = std::clamp(L, -0x7fff, 0x7fff);
            R = std::clamp(R, -0x7fff, 0x7fff);
            iprev_[0] = L;
            iprev_[1] = R;
            out[size_t(i) * 2] = int16_t(L);
            out[size_t(i) * 2 + 1] = int16_t(R);
        }
        return;
    }
    scratch_.assign(size_t(frames) * 2, 0.f);
    for (auto& v : voices_) {
        const auto& s = v.clip->samples;
        const uint64_t end = uint64_t(s.size()) << 32;
        for (int i = 0; i < frames; ++i) {
            if (v.pos >= end) {
                if (!v.loop) break;
                v.pos %= end;
            }
            const float x = s[size_t(v.pos >> 32)] / 32768.f;
            scratch_[size_t(i) * 2] += x * v.left;
            scratch_[size_t(i) * 2 + 1] += x * v.right;
            v.pos += v.step;
        }
    }
    if (stream_) stream_(scratch_.data(), frames);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return !v.loop && v.pos >= (uint64_t(v.clip->samples.size()) << 32); }),
                  voices_.end());
    // DF.EXE's mixer (0x497f9c): a centred full-volume voice of a full-scale
    // 8-bit sample is 127 x 62 of 32767 (half what OpenDF's gains give), and
    // each output sample starts from the previous one >> 2 (0x497fda) before
    // the voices are added, then is clamped to 16 bits.
    for (size_t i = 0; i < scratch_.size(); ++i) {
        float& prev = feedback_[i & 1];
        const float y = std::clamp(scratch_[i] * 0.5f * master_ + prev * 0.25f, -1.f, 1.f);
        prev = y;
        out[i] = int16_t(y * 32767.f);
    }
}

} // namespace df
