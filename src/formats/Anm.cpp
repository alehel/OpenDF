#include "Anm.h"

#include "../core/Pff.h"

#include <sstream>

namespace df {

bool AnimDefs::parse(const std::string& text) {
    moves_.clear();
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (auto c = line.find("//"); c != std::string::npos) line.resize(c);
        std::istringstream ls(line);
        std::string name, flag;
        AnimMove m;
        if (!(ls >> name >> m.animation >> m.velocity)) continue;
        m.override = (ls >> flag) && toUpper(flag) == "OVERRIDE";
        moves_[toUpper(name)] = m;
    }
    return !moves_.empty();
}

const AnimMove* AnimDefs::find(const std::string& move) const {
    auto it = moves_.find(toUpper(move));
    return it == moves_.end() ? nullptr : &it->second;
}

} // namespace df
