#include "Music.h"

#include "../core/Pff.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace df {

uint32_t Music::u32(size_t at) const {
    uint32_t v = 0;
    if (at + 4 <= script_.size()) std::memcpy(&v, &script_[at], 4);
    return v;
}

bool Music::load(Vfs& vfs, const std::filesystem::path& sbfPath, const char* script) {
    std::ifstream f(sbfPath, std::ios::binary);
    if (!f) return false;
    sbf_.assign(std::istreambuf_iterator<char>(f), {});
    if (sbf_.size() < 0x18 || std::memcmp(sbf_.data(), "SBF0", 4) != 0) return false;
    auto s32 = [&](size_t at) { uint32_t v = 0; if (at + 4 <= sbf_.size()) std::memcpy(&v, &sbf_[at], 4); return v; };
    // Header: table offset +0x10, count +0x14; entries of 32 bytes
    // {name[16], offset, size, block size, 0}.
    const uint32_t table = s32(0x10), count = s32(0x14);
    stems_.clear();
    for (uint32_t i = 0; i < count && table + 32 * (i + 1) <= sbf_.size(); ++i)
        stems_.push_back({std::string(reinterpret_cast<const char*>(&sbf_[table + 32 * i]), strnlen(reinterpret_cast<const char*>(&sbf_[table + 32 * i]), 16)),
                          s32(table + 32 * i + 16), s32(table + 32 * i + 20), std::max<uint32_t>(16, s32(table + 32 * i + 24))});
    debug_ = std::getenv("OPENDF_DEBUGMUSIC") != nullptr;
    if (!vfs.read(script, script_) || script_.size() < 0x30 || std::memcmp(script_.data(), "SCR0", 4) != 0) return false;
    // SCR0: +0xC offset of the track table; track header "MU00": +0x24
    // state table, +0x28 state count, +0x2C default state (track-relative).
    track_ = u32(u32(0xC));
    states_.clear();
    const uint32_t st = u32(track_ + 0x24), n = u32(track_ + 0x28);
    for (uint32_t i = 0; i < n && i < 32; ++i) states_.push_back(u32(track_ + st + 4 * i));
    return !stems_.empty() && !states_.empty();
}

void Music::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_ || states_.empty()) return;
    globals_.fill(0);
    locals_.fill(0);
    stack_.clear();
    returns_.clear();
    pc_ = track_ + states_[std::min<size_t>(u32(track_ + 0x2C), states_.size() - 1)];
    stem_ = -1;
    read_ = remaining_ = 0;
    queue_.clear();
    played_ = 0;
    running_ = true;
}

void Music::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    stem_ = -1;
    remaining_ = 0;
    queue_.clear();
}

void Music::setVar(int n, int value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (n >= 0 && n < 16) globals_[size_t(n)] = value;
}

void Music::play(int stem) {
    // 0x4b82d0: an index past the end plays stem 0.
    stem_ = stem >= 0 && stem < int(stems_.size()) ? stem : 0;
    read_ = 0;
    remaining_ = stems_[size_t(stem_)].size;
    if (debug_)
        std::fprintf(stderr, "music: PLAY %d %s (Var01=%d i=%d)\n", stem_, stems_[size_t(stem_)].name.c_str(), globals_[1], globals_[16]);
}

// One tick (0x4ab610): runs from the saved PC until an opcode yields (PLAY,
// goto state, yield, return with an empty return stack); after 32
// instructions also when the data stack is empty.
void Music::tick() {
    const size_t code = track_ + u32(track_ + 0x20);
    auto byte = [&]() { return pc_ < script_.size() ? script_[pc_++] : uint8_t(0x3a); };
    auto imm32 = [&]() { const uint32_t v = u32(pc_); pc_ += 4; return v; };
    auto truth = [](bool b) { return b ? -1 : 0; };
    for (int count = 0; count < 4096; ++count) {
        if (count >= 32 && stack_.empty()) return;
        const uint8_t op = byte();
        switch (op) {
        case 0x01: stack_.push_back(int8_t(byte())); break;
        case 0x02: stack_.push_back(int32_t(imm32())); break;
        case 0x03: stack_.push_back(global(byte())); break;
        case 0x04: stack_.push_back(locals_[size_t(std::min<int>(byte() / 4, 15))]); break;
        case 0x05: byte(); stack_.push_back(0); break;
        case 0x06: byte(); byte(); stack_.push_back(0); break;
        case 0x07: byte(); stack_.push_back(0); break;   // no constants in DFMenu
        case 0x08: global(byte()) = pop(); break;
        case 0x09: locals_[size_t(std::min<int>(byte() / 4, 15))] = pop(); break;
        case 0x0a: case 0x0b: byte(); byte(); pop(); break;
        case 0x0c: stack_.push_back(0); break;
        case 0x0f: stack_.clear(); break;
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:
        case 0x1c: case 0x1d: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: {
            const int32_t b = pop(), a = pop();
            int32_t r = 0;
            switch (op) {
            case 0x10: r = a + b; break;
            case 0x11: r = a - b; break;
            case 0x12: r = a * b; break;
            case 0x13: r = b ? a / b : 0; break;
            case 0x14: r = b ? a % b : 0; break;
            case 0x15: case 0x17: r = a & b; break;
            case 0x16: case 0x18: r = a | b; break;
            case 0x19: r = a ^ b; break;
            case 0x1c: r = int32_t(uint32_t(a) << (b & 31)); break;
            case 0x1d: r = a >> (b & 31); break;
            case 0x20: r = truth(a == b); break;
            case 0x21: r = truth(a != b); break;
            case 0x22: r = truth(a >= b); break;
            case 0x23: r = truth(a <= b); break;
            case 0x24: r = truth(a > b); break;
            case 0x25: r = truth(a < b); break;
            }
            stack_.push_back(r);
            break;
        }
        case 0x1a: stack_.push_back(-pop()); break;
        case 0x1b: stack_.push_back(~pop()); break;
        case 0x1e: stack_.push_back(truth(pop() == 0)); break;
        case 0x28: ++global(byte()); break;
        case 0x29: --global(byte()); break;
        case 0x2a: ++locals_[size_t(std::min<int>(byte() / 4, 15))]; break;
        case 0x2b: --locals_[size_t(std::min<int>(byte() / 4, 15))]; break;
        case 0x30: pc_ = code + imm32(); break;
        case 0x31: { const size_t t = code + imm32(); if (pop() == 0) pc_ = t; break; }
        case 0x32: { const size_t t = code + imm32(); if (pop() != 0) pc_ = t; break; }
        case 0x33: {
            const uint8_t b = byte();
            if (b == 1) {
                // random(lo, hi) inclusive (0x4aba4b): x = rol(rol(x, 11) + x, 2).
                const int32_t hi = pop(), lo = pop();
                auto rol = [](uint32_t v, int s) { return (v << s) | (v >> (32 - s)); };
                seed_ = rol(rol(seed_, 11) + seed_, 2);
                const int32_t span = hi - lo + 1;
                stack_.push_back(span > 0 ? lo + int32_t((seed_ & 0xFFFF) % uint32_t(span)) : lo);
            } else if (b == 0) {
                stack_.push_back(pop());
            } else {
                pop();
                stack_.push_back(0);   // setVolume / fadeTo: not used by DFMenu
            }
            break;
        }
        case 0x34: { const uint32_t t = imm32(); returns_.push_back(int32_t(pc_)); pc_ = track_ + t; break; }
        case 0x38: { const int n = byte(); for (int k = n - 1; k >= 0; --k) locals_[size_t(std::min(15, 15 - k))] = pop(); break; }
        case 0x39:
            if (returns_.empty()) return;
            pc_ = size_t(returns_.back());
            returns_.pop_back();
            break;
        case 0x3a: return;
        case 0x3b: { const uint8_t s = byte(); if (s < states_.size()) pc_ = track_ + states_[s]; return; }
        case 0x3e: play(byte()); return;
        case 0x3f: pc_ = track_ + states_[std::min<size_t>(u32(track_ + 0x2C), states_.size() - 1)]; return;
        default: break;   // nops
        }
    }
}

void Music::update() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) return;
    if (remaining_ > 0) {
        if (queue_.size() <= 12) {
            const Stem& st = stems_[size_t(stem_)];
            const size_t base = size_t(st.offset) + read_;
            if (base + 8 <= sbf_.size()) {
                // Block: u32 bytes, u8 shiftL, u8 shiftR, u16, then 8-bit stereo.
                uint32_t nbytes;
                std::memcpy(&nbytes, &sbf_[base], 4);
                nbytes = std::min<uint32_t>({nbytes, st.block - 8, uint32_t(sbf_.size() - base - 8)});
                queue_.push_back({base + 8, nbytes / 2, sbf_[base + 4], sbf_[base + 5]});
            }
            remaining_ = remaining_ > st.block ? remaining_ - st.block : 0;
            read_ += st.block;
        }
    } else {
        tick();
    }
}

void Music::mix(float* out, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) return;
    // Gain per block (0x4a9c84): (V>>1)>>shift on the signed sample, V the
    // stream volume (master; fades unused by DFMenu).
    const float vol = float(std::clamp(master_, 0, 255)) / 255.f / 128.f;
    int done = 0;
    while (done < frames && !queue_.empty()) {
        const Block& b = queue_.front();
        const float gl = vol / float(1 << std::min(b.shiftL, 15)), gr = vol / float(1 << std::min(b.shiftR, 15));
        while (done < frames && played_ < b.frames) {
            const size_t at = b.at + size_t(played_) * 2;
            out[size_t(done) * 2] += float(int(sbf_[at]) - 128) * gl;
            out[size_t(done) * 2 + 1] += float(int(sbf_[at + 1]) - 128) * gr;
            ++played_;
            ++done;
        }
        if (played_ >= b.frames) {
            queue_.erase(queue_.begin());
            played_ = 0;
        }
    }
}

} // namespace df
