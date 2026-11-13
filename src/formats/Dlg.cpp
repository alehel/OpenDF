#include "Dlg.h"

#include <cstdlib>
#include <cstring>

namespace df {

namespace {
std::string field(const std::vector<uint8_t>& d, size_t o, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && o + i < d.size() && d[o + i]; ++i) s.push_back(char(d[o + i]));
    return s;
}
} // namespace

bool DialogIndex::parse(const std::vector<uint8_t>& d) {
    dialogs_.clear();
    if (d.size() < 0x20 || std::memcmp(d.data(), "DLG0", 4) != 0) return false;
    size_t o = 0x20;
    while (o + 0x34 <= d.size()) {
        const std::string name = field(d, o, 24);
        const uint32_t n = uint32_t(d[o + 0x18]) | uint32_t(d[o + 0x19]) << 8 | uint32_t(d[o + 0x1A]) << 16 |
                           uint32_t(d[o + 0x1B]) << 24;
        if (name.empty() || n > 64) break;
        size_t p = o + 0x34;
        std::vector<DialogPart> parts;
        for (uint32_t k = 0; k < n && p + 0x44 <= d.size(); ++k, p += 0x44) {
            DialogPart part;
            part.clip = field(d, p, 24);
            const std::string text = field(d, p + 24, 16);
            if (!text.empty() && text[0] == '_') part.subtitle = std::atoi(text.c_str() + 1);
            if (p + 0x35 < d.size()) part.gap = d[p + 0x35];
            parts.push_back(std::move(part));
        }
        dialogs_[name] = std::move(parts);
        o = p;
    }
    return !dialogs_.empty();
}

const std::vector<DialogPart>* DialogIndex::find(const std::string& name) const {
    auto it = dialogs_.find(name);
    return it == dialogs_.end() ? nullptr : &it->second;
}

} // namespace df
