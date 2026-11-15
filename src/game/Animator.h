// A soldier's .CHR animation as DF.EXE plays it (0x467c40 start,
// 0x468180 enter frame, 0x468290 tick, 0x468520 transitions), 30 ticks a
// second (the body update's odd ticks, 0x436c9d).
//
// Entering a frame sets each part's angle step to (frame angle - current) /
// frame ticks, the short way round; each tick adds the steps, so a frame's
// pose is reached when its ticks are over. A new animation therefore blends
// from wherever the body is. When the last frame ends, its first
// unconditional transition picks the next animation and frame (loops jump
// back to frame 1; deaths to their own last frame, holding it); a target
// without frames freezes the body.
#pragma once

#include "../formats/Chr.h"

#include <array>
#include <vector>

namespace df {

class Animator {
public:
    // Starts animation `anim` at frame 0, blending from the current pose.
    void start(const Chr& chr, int anim);
    // One animation tick; returns the event byte of a frame that just
    // ended (0 if none): 0x40-0x43 footsteps, 0x12-0x16 voices. Delta Force
    // 2 (KSA bodies, GOG Df2.exe 0x423380): the frame just entered.
    int tick(const Chr& chr);

    int animation() const { return anim_; }
    int frame() const { return frame_; }
    bool frozen() const { return frozen_; }
    // Current angles per part in turns (raw: a0, a1, a2 as stored).
    const std::vector<std::array<float, 3>>& angles() const { return cur_; }
    // Root motion of the current frame per tick (0x468180), model axes
    // (x forward, y left, z up), metres: (previous frame's anchor contact -
    // this frame's) + the frame's root offset, over its ticks. "override"
    // moves move the body by it (0x43666b).
    const std::array<float, 3>& rootStep() const { return rootStep_; }
    // The frame being blended to (for contact points / root motion).
    const ChrFrame* currentFrame(const Chr& chr) const;

private:
    void enter(const Chr& chr, int anim, int frame);

    int anim_ = -1, frame_ = 0, left_ = 0;
    bool frozen_ = true;
    std::vector<std::array<float, 3>> cur_, step_;
    std::array<float, 3> rootStep_{0, 0, 0};
    const ChrFrame* prev_ = nullptr;
};

} // namespace df
