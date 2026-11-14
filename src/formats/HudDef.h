// Delta Force 2's HUD.DEF (DF2.EXE parser 0x444150): per display mode
// ("begin 640" ... "end") the HUD element positions in that mode's screen
// pixels, e.g. "gun_icon 38,396" or "map 536,395 630,475".
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace df {

class HudDef {
public:
    bool parse(const std::vector<uint8_t>& file);
    bool has(int mode) const { return sections_.count(mode) != 0; }
    std::vector<int> modes() const;
    // The numbers of `key` in `mode`'s section (empty if absent).
    const std::vector<int>& get(int mode, const std::string& key) const;
    int value(int mode, const std::string& key, size_t i, int fallback = 0) const {
        const auto& v = get(mode, key);
        return i < v.size() ? v[i] : fallback;
    }

private:
    std::map<int, std::map<std::string, std::vector<int>>> sections_;
};

} // namespace df
