#include "Audio.h"

#include "../core/Pff.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace df {

Audio::~Audio() {
    closeDevice();
    if (dump_) std::fclose(dump_);
}

bool Audio::openDevice() {
    if (device_ || dump_) return true;
    if (const char* f = std::getenv("OPENDF_AUDIODUMP"); f && *f) {
        dump_ = std::fopen(f, "wb");
        if (dump_) return true;
    }
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

bool Audio::advance(double seconds) {
    if (!dump_) return false;
    dumpCarry_ += seconds * kRate;
    const int n = int(dumpCarry_);
    dumpCarry_ -= n;
    if (n > 0) {
        std::vector<int16_t> buf(size_t(n) * 2);
        mix(buf.data(), n);   // mix() appends to the dump
    }
    return true;
}

bool dfConvertWav(const uint8_t* d, size_t size, bool full16, SoundClip& out) {
    if (size < 0x2c || std::memcmp(d, "RIFF", 4) != 0) return false;
    const auto u16 = [&](size_t o) { return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8; };
    const auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
    // Rate field (0x48cfe2 / 0x4711ea): ((rate << 4) / 22050) << 24, an
    // integer divide (DF2 by the reciprocal 0x2f8df18f, rounding toward 0
    // the same way): 5512 Hz -> 3 -> 4134 Hz, 5564 Hz -> 4 -> 5512 Hz.
    const int32_t q = int32_t(u32(0x18) << 4) / 22050;
    out.rateField = uint32_t(q) << 24;
    out.rate = q * 22050 / 16;
    // The data size at +0x28 and the data at +0x2c (0x48cfcb / 0x4711dc),
    // whatever chunks the file really has. With an 18-byte fmt chunk and a
    // fact chunk (WATEREXP.WAV, which nothing plays) +0x28 is "ct" and half
    // the fact size (291939 bytes) and +0x2c the fact payload, so the clip
    // starts with 14 header bytes and runs on past the end of the file. What
    // lies there in the game's heap cannot be known: APPROXIMATION, the clip
    // stops at the end of the file.
    const size_t len = u32(0x28), avail = size - 0x2c;
    const uint8_t* src = d + 0x2c;
    out.samples.clear();
    if (u16(0x22) == 16) {
        // 16-bit (0x48d03e / 0x471219): n = size >> 1; peak = max |s| (the
        // word negated, unsigned compare); scale = 0x7eff0000 / peak
        // (unsigned; a silent clip divides by zero in the games); then per
        // sample ((v · scale + 0x800000) >> 24) ^ 0x80, v the low byte
        // sign-extended in DF.EXE (0x48d093 `movsx eax, byte ptr [esi]`, a
        // bug: INST177/INST178 come out all 0x80, silent), the whole word in
        // DF2.EXE (0x47125d `movsx eax, word ptr [esi]`).
        const size_t n = std::min(len >> 1, avail >> 1);
        uint32_t peak = 0;
        for (size_t i = 0; i < n; ++i) {
            const uint16_t w = uint16_t(u16(0x2c + 2 * i));
            peak = std::max<uint32_t>(peak, (w & 0x8000) ? uint16_t(-w) : w);
        }
        const uint32_t scale = peak ? 0x7eff0000u / peak : 0;
        out.samples.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const int32_t v = full16 ? int32_t(int16_t(uint16_t(u16(0x2c + 2 * i)))) : int32_t(int8_t(src[2 * i]));
            const uint8_t b = uint8_t(((uint32_t(int64_t(v) * int64_t(scale)) + 0x800000u) >> 24) ^ 0x80u);
            out.samples[i] = int16_t((int(b) - 128) * 256);   // the mixer's byte - 0x80, kept as s16
        }
    } else {
        // Any other width: the bytes are copied as they are, unsigned 8-bit
        // to the mixer (0x48d024 / 0x471298).
        const size_t n = std::min(len, avail);
        out.samples.resize(n);
        for (size_t i = 0; i < n; ++i) out.samples[i] = int16_t((int(src[i]) - 128) * 256);
    }
    return true;
}

const SoundClip* Audio::clip(Vfs& vfs, const std::string& name) {
    std::string key = name;
    for (auto& c : key) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (key.find('.') == std::string::npos) key += ".WAV";
    int game = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = clips_.find(key); it != clips_.end()) return it->second.get();
        game = dfGame_;
    }
    std::unique_ptr<SoundClip> out;
    std::vector<uint8_t> data;
    if (vfs.read(key, data) && !data.empty()) {
        // The games convert every loaded WAV assuming the canonical header
        // (dfConvertWav). DF2.EXE first decodes MS ADPCM (format 2,
        // 0x476020) to 8-bit; OpenDF lets SDL decode those (APPROXIMATION;
        // no shipped loose WAV uses it).
        const bool adpcm = data.size() >= 0x16 && (data[0x14] | data[0x15] << 8) == 2;
        if (game && !(game == 2 && adpcm)) {
            out = std::make_unique<SoundClip>();
            if (!dfConvertWav(data.data(), data.size(), game == 2, *out)) out.reset();
        } else {
            SDL_AudioSpec spec{};
            Uint8* buf = nullptr;
            Uint32 len = 0;
            SDL_RWops* rw = SDL_RWFromConstMem(data.data(), int(data.size()));
            if (SDL_LoadWAV_RW(rw, 1, &spec, &buf, &len)) {
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
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = clips_[key];
    slot = std::move(out);
    return slot.get();
}

namespace {
// The voice step of both mixers (DF.EXE 0x4a9a10, DF2.EXE 0x4a0092 and the
// updates 0x4a9769 / 0x49fcaa): pitch 16.16 (0 = 1.0) times the clip's rate
// field, the high dword, >> 2: 1/1024 samples per output sample (the
// position's fraction is 10 bits, 0x498042 `sar eax, 0xa`). 22050 Hz at
// pitch 1 is 1024.
int32_t dfStep(const SoundClip* c, float pitch) {
    const uint32_t rf = c->rateField ? c->rateField : uint32_t(int64_t(c->rate) * 16 / 22050) << 24;
    int32_t p = int32_t(std::lround(double(pitch) * 65536.0));
    if (p == 0) p = 0x10000;
    return int32_t((int64_t(p) * int64_t(int32_t(rf))) >> 32) >> 2;
}

// The games' integer gain for a float gain pan · vol / (127 · 255): pan ·
// vol back, then (pan · vol · master 255) >> 16 (0x4a99b0 / 0x49fc69).
int dfGain(float f) {
    const long pv = std::lround(double(f) * 127.0 * 255.0);
    return int(std::clamp<long>((pv * 255) >> 16, -128, 127));
}
} // namespace

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
    v.gL = dfGain(left);
    v.gR = dfGain(right);
    v.stepCur = v.stepTarget = dfStep(clip, pitch);   // 0x4a009f / 0x4a00a2: +8 = +0x34, +0x38 = 0
    if (dfGame_ == 1) {
        // GOG Df.exe 16 fixed voices (active 0x4a16d2, pending 0x4a1a12,
        // 0x34 each). A one-shot (0x4b4f5e) takes the lowest slot that is
        // not reserved (+0x31), not playing and not pending; else the
        // lowest neither reserved nor pending whose start volume (+0x1c) is
        // below its own (strictly); else none. Slots 10-15 are reserved for
        // the loops during a mission only (0x43e4a0 / 0x43e6d0). OpenDF's
        // loops take the lowest free one of 10-15.
        Voice* at[kVoices] = {};
        for (auto& o : voices_)
            if (o.slot >= 0 && o.slot < kVoices) at[o.slot] = &o;
        const int from = group ? kOneShotVoices : 0;
        const int to = (!group && loopsReserved_) ? kOneShotVoices : kVoices;
        for (int i = from; i < to; ++i)
            if (!at[i]) {
                v.slot = i;
                voices_.push_back(v);
                return v.id;
            }
        if (group) return 0;
        for (int i = from; i < to; ++i)
            if (at[i]->started && at[i]->volume < volume) {
                v.slot = i;
                *at[i] = v;
                return v.id;
            }
        return 0;
    }
    if (dfGame_ == 2) {
        // GOG Df2.exe 0x4aafc0: 16 slots in index order. A voice not yet
        // mixed is the slot's pending record (0x496f72), promoted at the
        // next mix (0x49a7fb); replacing the playing voice here is the same,
        // as nothing is mixed in between.
        auto in = [&](int s) -> Voice* {
            for (auto& o : voices_)
                if (o.slot == s) return &o;
            return nullptr;
        };
        if (group) {
            for (int s = 10; s < 16; ++s)
                if (!in(s)) {
                    if (loop) {
                        // A loop (0x4ab183) follows on as the slot's template
                        // with flag +0x30; its first copy into the voice
                        // (0x4996d9..0x4996fb) steps the seed (rol 11 + seed
                        // + 1) and starts it seed & 0x3fc00 (0..255 samples)
                        // past sample 1, where every later pass restarts
                        // (dfVoices).
                        loopSeed_ = (loopSeed_ << 11 | loopSeed_ >> 21) + loopSeed_ + 1;
                        v.pos = (uint64_t(1) << 32) + (uint64_t(loopSeed_ & 0x3fc00) << 22);
                    }
                    v.slot = s;
                    voices_.push_back(v);
                    return v.id;
                }
            return 0;
        }
        for (int s = 0; s < 16; ++s)
            if (!reserved_[size_t(s)] && !in(s)) { v.slot = s; voices_.push_back(v); return v.id; }
        for (int s = 0; s < 16; ++s) {
            if (reserved_[size_t(s)]) continue;
            Voice* o = in(s);
            if (o && o->started && o->volume < volume) { v.slot = s; *o = v; return v.id; }
        }
        return 0;
    }
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
        if (v.id == voice) {
            // A pending voice is left as it is (GOG Df.exe 0x4b4cc4: an id
            // found only in the pending table returns at once; Df2.exe 0x49fbf4).
            if (dfMixer_ && !v.started) continue;
            v.left = left;
            v.right = right;
            int nL = dfGain(left), nR = dfGain(right);
            if (dfGame_ == 2) {
                // 0x49fcba..0x49fd48: for each channel whose old and new
                // gains differ in sign, |old - new| is summed, for the new
                // pair and for it negated; the negated pair is taken when it
                // is the nearer (strictly), a whole-voice phase flip.
                auto cost = [&](int a, int b) {
                    int c = 0;
                    if ((v.gL ^ a) < 0) c += std::abs(v.gL - a);
                    if ((v.gR ^ b) < 0) c += std::abs(v.gR - b);
                    return c;
                };
                if (cost(nL, nR) > cost(-nL, -nR)) nL = -nL, nR = -nR;
                v.updated = true;   // +0x3e (0x49fbdd)
            }
            v.gL = nL;
            v.gR = nR;
        }
}

void Audio::setPitch(int voice, float pitch) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_)
        if (v.id == voice) {
            if (dfMixer_ && !v.started) continue;   // GOG Df.exe 0x4b4cc4, Df2.exe 0x49fbf4
            v.step = uint64_t(double(v.clip->rate) * pitch / kRate * 4294967296.0);
            v.stepTarget = dfStep(v.clip, pitch);
            if (dfGame_ == 2) v.updated = true;   // DF2: the target +0x34 only (0x49fcb7); the mixer slides to it
            else v.stepCur = v.stepTarget;        // DF1: the step +8 at once (0x4a9776)
        }
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
    reserved_.fill(false);
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

void Audio::dfVoices(int32_t* acc, int from, int frames) {
    for (auto& v : voices_) {
        const auto& smp = v.clip->samples;
        const uint64_t end = uint64_t(smp.size()) << 32;
        const uint64_t step = uint64_t(uint32_t(std::max<int32_t>(v.stepCur, 0))) << 22;   // 1/1024 -> 32.32
        for (int i = from; i < from + frames; ++i) {
            if (v.pos >= end) {
                if (!v.loop) break;
                // DF2: the template's position, sample 1, the fraction dropped (0x4996b5).
                if (dfGame_ == 2) v.pos = uint64_t(1) << 32;
                else v.pos %= end;
                if (v.pos >= end) break;   // a one-sample loop
            }
            const int sv = smp[size_t(v.pos >> 32)] >> 8;   // the 8-bit sample, byte − 0x80
            acc[size_t(i) * 2] += dfContrib(v.gL, sv);
            acc[size_t(i) * 2 + 1] += dfContrib(v.gR, sv);
            v.pos += step;
        }
    }
}

void Audio::dfOutput(const int32_t* acc, int16_t* out, int frames) {
    for (int i = 0; i < frames; ++i) {
        int32_t L = (iprev_[0] >> 2) + acc[size_t(i) * 2], R = (iprev_[1] >> 2) + acc[size_t(i) * 2 + 1];
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
}

void Audio::dfMixBlock(int n) {
    // DF2.EXE 0x48e028, one call: first 0x48f7fb latches the voices - a
    // new one starts at its step with no slide (0x4a00a5); an updated one
    // (+0x3e) gets the delta (target − current) >> 2 (0x48f818, arithmetic);
    // the others 0 (0x48f8aa). Then the call's frames are mixed in parts of
    // (n + 4) >> 2 (0x48e05a), the last one what is left (0x48e07c, none
    // if nothing is), and after each part every voice's step grows by its
    // delta (0x48e531..0x48e67c), so it reaches the target (within the
    // rounding) after four.
    for (auto& v : voices_) {
        if (!v.started) {
            v.started = true;
            v.stepCur = v.stepTarget;
            v.stepDelta = 0;
            v.updated = false;
        } else if (v.updated) {
            v.stepDelta = (v.stepTarget - v.stepCur) >> 2;
            v.updated = false;
        } else {
            v.stepDelta = 0;
        }
    }
    iscratch_.assign(size_t(n) * 2, 0);
    const int part = (n + 4) >> 2;
    int left = n, at = 0;
    for (int k = 0; k < 4; ++k) {
        int len = part;
        left -= part;
        if (left < 0) {
            len = part + left;
            if (len <= 0) break;
        }
        dfVoices(iscratch_.data(), at, len);
        at += len;
        for (auto& v : voices_) v.stepCur += v.stepDelta;
    }
    if (stream_) {
        scratch_.assign(size_t(n) * 2, 0.f);
        stream_(scratch_.data(), n);   // in the games' integer units
        for (size_t i = 0; i < scratch_.size(); ++i) iscratch_[i] += int32_t(std::lround(scratch_[i]));
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return !v.loop && v.pos >= (uint64_t(v.clip->samples.size()) << 32); }),
                  voices_.end());
    const size_t base = fifo_.size();
    fifo_.resize(base + size_t(n) * 2);
    dfOutput(iscratch_.data(), fifo_.data() + base, n);
}

void Audio::mix(int16_t* out, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dfMixer_ && dfGame_ == 2) {
        // DF2.EXE mixes on a 100 ms multimedia timer (0x47102b:
        // timeSetEvent(100, ...) -> 0x471070) whatever the play cursor has
        // passed, nominally 0x89d frames (0x470e4b); OpenDF mixes calls of
        // exactly 0x89d frames (INFERRED: the timer's nominal period) and
        // hands them out as the device or the recorder asks.
        constexpr int kCall = 0x89d;
        size_t need = size_t(frames) * 2;
        int16_t* o = out;
        while (need) {
            if (fifoAt_ >= fifo_.size()) {
                fifo_.clear();
                fifoAt_ = 0;
                dfMixBlock(kCall);
            }
            const size_t n = std::min(need, fifo_.size() - fifoAt_);
            std::memcpy(o, fifo_.data() + fifoAt_, n * sizeof(int16_t));
            o += n;
            fifoAt_ += n;
            need -= n;
        }
    } else if (dfMixer_) {
        // DF.EXE (0x497f9c): one call, the steps and gains as they are
        // (0x4992dc patches them at the start of each call, no slide).
        iscratch_.assign(size_t(frames) * 2, 0);
        for (auto& v : voices_) v.started = true;
        dfVoices(iscratch_.data(), 0, frames);
        if (stream_) {
            scratch_.assign(size_t(frames) * 2, 0.f);
            stream_(scratch_.data(), frames);   // in DF's integer units
            for (size_t i = 0; i < scratch_.size(); ++i) iscratch_[i] += int32_t(std::lround(scratch_[i]));
        }
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                     [](const Voice& v) { return !v.loop && v.pos >= (uint64_t(v.clip->samples.size()) << 32); }),
                      voices_.end());
        dfOutput(iscratch_.data(), out, frames);
    } else {
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
    if (direct_) direct_(out, frames);
    if (dump_) std::fwrite(out, sizeof(int16_t), size_t(frames) * 2, dump_);
}

} // namespace df
