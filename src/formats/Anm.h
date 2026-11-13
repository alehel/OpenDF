// .ANM animation definition tables (PLAYANIM.ANM, ENEMY00.ANM, ...): one
// line per move, "name  animation-number  velocity [override]", with //
// comments. The velocity, in m/s, is how fast a soldier moves while playing
// that move; ITEMS.DEF picks a table per soldier with `anim_def`.
#pragma once

#include <string>
#include <unordered_map>

namespace df {

struct AnimMove {
    int animation = 0;     // animation number in the soldier's .CHR file
    float velocity = 0;    // m/s
    bool override = false;
};

class AnimDefs {
public:
    bool parse(const std::string& text);
    // Null if the table has no such move.
    const AnimMove* find(const std::string& move) const;
    bool empty() const { return moves_.empty(); }

private:
    std::unordered_map<std::string, AnimMove> moves_;   // upper-case names
};

} // namespace df
