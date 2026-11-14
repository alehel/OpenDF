#include "HudDef.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace df {

bool HudDef::parse(const std::vector<uint8_t>& file) {
    sections_.clear();
    std::istringstream in(std::string(file.begin(), file.end()));
    std::string line;
    int mode = -1;
    while (std::getline(in, line)) {
        // Tokens split at blanks and commas.
        for (char& c : line)
            if (c == ',' || c == '\t' || c == '\r') c = ' ';
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key)) continue;
        for (char& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (key == "begin") {
            ls >> mode;
            sections_[mode];
            continue;
        }
        if (key == "end") {
            mode = -1;
            continue;
        }
        if (mode < 0) continue;
        std::vector<int> values;
        std::string word;
        while (ls >> word) values.push_back(std::atoi(word.c_str()));
        sections_[mode][key] = std::move(values);
    }
    return !sections_.empty();
}

std::vector<int> HudDef::modes() const {
    std::vector<int> out;
    for (const auto& [m, s] : sections_) out.push_back(m);
    return out;
}

const std::vector<int>& HudDef::get(int mode, const std::string& key) const {
    static const std::vector<int> kEmpty;
    auto s = sections_.find(mode);
    if (s == sections_.end()) return kEmpty;
    auto k = s->second.find(key);
    return k == s->second.end() ? kEmpty : k->second;
}

} // namespace df
