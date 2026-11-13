#include "Rtxt.h"

#include <cstring>

namespace df {

namespace {
uint32_t rd(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size() ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24 : 0;
}
std::string cstr(const std::vector<uint8_t>& d, size_t o) {
    std::string s;
    while (o < d.size() && d[o]) s.push_back(char(d[o++]));
    return s;
}
} // namespace

bool parseRtxt(const std::vector<uint8_t>& d, std::vector<std::string>& out) {
    out.clear();
    if (d.size() < 16 || std::memcmp(d.data(), "RTXT", 4) != 0) return false;
    const uint32_t count = rd(d, 12);
    const size_t base = 16 + size_t(count) * 16;
    if (count > 100000 || base > d.size()) return false;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) out.push_back(cstr(d, base + rd(d, 16 + size_t(i) * 16)));
    return true;
}

bool TextTable::parse(const std::vector<uint8_t>& d) {
    keys_.clear();
    sections_.clear();
    positions_.clear();
    if (!parseRtxt(d, text_)) return false;
    for (size_t i = 0; i < text_.size(); ++i) positions_.push_back(rd(d, 16 + i * 16 + 4));
    const size_t kb = rd(d, 4);
    const uint32_t ns = rd(d, kb);
    if (kb + 4 + size_t(ns) * 8 > d.size() || ns > 1000) return true;   // no key block: text only
    size_t nameAt = kb + 4 + size_t(ns) * 8;
    int first = 0;
    for (uint32_t s = 0; s < ns; ++s) {
        Section sec;
        sec.name = cstr(d, nameAt);
        nameAt += sec.name.size() + 1;
        size_t keyAt = kb + 4 + rd(d, kb + 4 + 8 * s);
        sec.count = int(rd(d, kb + 8 + 8 * s));
        sec.first = first;
        first += sec.count;
        for (int k = 0; k < sec.count; ++k) {
            keys_.push_back(cstr(d, keyAt));
            keyAt += keys_.back().size() + 1;
        }
        sections_.push_back(std::move(sec));
    }
    if (keys_.size() != text_.size()) keys_.clear(), sections_.clear();
    return true;
}

const std::string* TextTable::find(const std::string& section, const std::string& key) const {
    for (const auto& s : sections_) {
        if (s.name != section) continue;
        for (int i = s.first; i < s.first + s.count; ++i)
            if (keys_[size_t(i)] == key) return &text_[size_t(i)];
    }
    return nullptr;
}

uint32_t TextTable::position(const std::string& section, const std::string& key) const {
    const std::string* t = find(section, key);
    if (!t) return 0;
    const size_t i = size_t(t - text_.data());
    return i < positions_.size() ? positions_[i] : 0;
}

} // namespace df
