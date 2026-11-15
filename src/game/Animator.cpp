#include "Animator.h"

#include <cmath>

namespace df {

void Animator::start(const Chr& chr, int anim) {
    enter(chr, anim, 0);
}

void Animator::enter(const Chr& chr, int anim, int frame) {
    const ChrAnimation* a = chr.animation(anim);
    if (!a || frame < 0 || frame >= int(a->frames.size())) {
        frozen_ = true;
        rootStep_ = {0, 0, 0};
        return;
    }
    anim_ = anim;
    frame_ = frame;
    frozen_ = false;
    const ChrFrame& f = a->frames[size_t(frame)];
    const size_t n = f.angles.size();
    if (cur_.size() != n) {
        // A new body starts in this frame's pose.
        cur_.assign(n, {0, 0, 0});
        for (size_t p = 0; p < n; ++p)
            for (int k = 0; k < 3; ++k) cur_[p][size_t(k)] = f.angles[p][size_t(k)] / 256.f;
    }
    step_.assign(n, {0, 0, 0});
    left_ = std::max(1, f.ticks);
    // Root motion (CHR axes x lateral, y up, z back -> model x = -z,
    // y = -x, z = y).
    std::array<float, 3> d = f.root;
    if (prev_ && f.anchor < int(f.contacts.size()) && f.anchor < int(prev_->contacts.size()))
        for (int k = 0; k < 3; ++k) d[size_t(k)] += prev_->contacts[size_t(f.anchor)][size_t(k)] - f.contacts[size_t(f.anchor)][size_t(k)];
    rootStep_ = {-d[2] / float(left_), -d[0] / float(left_), d[1] / float(left_)};
    // Holding the last frame: Delta Force re-enters it (GOG Df.exe 0x43aa30
    // -> 0x43a150 -> 0x43a690), so its root offset keeps applying (the
    // anchor contacts cancel); OpenDF keeps a KSA body still.
    if (prev_ == &f && chr.ksa()) rootStep_ = {0, 0, 0};
    prev_ = &f;
    for (size_t p = 0; p < n; ++p)
        for (int k = 0; k < 3; ++k) {
            float d = f.angles[p][size_t(k)] / 256.f - cur_[p][size_t(k)];
            d -= std::round(d);   // the short way round
            step_[p][size_t(k)] = d / float(left_);
        }
}

int Animator::tick(const Chr& chr) {
    if (frozen_) return 0;
    for (size_t p = 0; p < cur_.size(); ++p)
        for (int k = 0; k < 3; ++k) cur_[p][size_t(k)] += step_[p][size_t(k)];
    if (--left_ > 0) return 0;
    const ChrAnimation* a = chr.animation(anim_);
    if (!a) { frozen_ = true; return 0; }
    const ChrFrame& f = a->frames[size_t(frame_)];
    const int event = f.event;
    if (frame_ + 1 < int(a->frames.size())) {
        enter(chr, anim_, frame_ + 1);
    } else {
        // The last frame's transitions (0x468520): the first one without
        // conditions; none at all -> animation 0.
        const ChrTransition* t = nullptr;
        for (const auto& tr : f.transitions)
            if (!tr.cond[0] && !tr.cond[1]) { t = &tr; break; }
        if (!t && f.transitions.empty()) enter(chr, 0, 0);
        else if (t) enter(chr, t->anim, std::max(0, t->frame));
        else frozen_ = true, rootStep_ = {0, 0, 0};
    }
    return event;
}

const ChrFrame* Animator::currentFrame(const Chr& chr) const {
    const ChrAnimation* a = chr.animation(anim_);
    return a && frame_ < int(a->frames.size()) ? &a->frames[size_t(frame_)] : nullptr;
}

} // namespace df
