// .CHR soldier skeleton and animation (PLAYER01.CHR, shared by all
// soldiers; ITEMS.DEF `chr_file`). Layout and posing from DF.EXE (loader
// 0x467810, scale 0x469270, posing 0x4701f4 / 0x46df64); see docs/FORMATS.md.
//
// CHR axes: x lateral, y up, z backward. Model axes (as the 3DI): x forward,
// y left, z up; model = (-z, -x, y). Lengths are scaled by 0x1d2b0/65536 at
// load (16.16 metres afterwards).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace df {

struct ChrPart {
    int parent = -1;
    std::array<float, 3> joint{};    // offset from the parent joint, CHR axes, metres
    std::array<float, 3> centre{};   // hit-sphere centre, part-local (used raw, as DF.EXE does)
    float radius = 0;                // metres
};

struct ChrTransition {
    uint8_t cond[2] = {0, 0};        // bytes 2, 3: conditions (0 = always)
    int anim = 0, frame = 0;         // byte 4 target animation, byte 5 target frame + 1
};

struct ChrFrame {
    uint32_t flags = 0;              // bit (16+k): contact point k not touching
    int ticks = 1;
    uint8_t event = 0, event2 = 0;   // +0xA, +0xB: 0x40-0x43 footsteps, 0x12-0x16 voices (0x4360b0)
    std::array<float, 3> root{};     // +0xC root offset (CHR axes, metres)
    int anchor = 0;                  // +0x18 anchor contact point
    std::vector<ChrTransition> transitions;   // checked when the last frame ends (0x468520)
    std::vector<std::array<float, 3>> contacts;   // CHR axes, metres
    std::vector<std::array<uint8_t, 3>> angles;   // per part: yaw, pitch, roll bytes (256 per turn)
    float ground = 0;                // KSA: the origin's height above the feet (no contacts)
    bool hasGround = false;
};

struct ChrAnimation { std::vector<ChrFrame> frames; };

// A 3x4 matrix: rows of (rotation | translation), model axes, metres.
struct ChrMatrix { float m[3][4]; };

class Chr {
public:
    bool parse(const std::vector<uint8_t>& file);
    // Delta Force 2's .KSA (DF2.EXE 0x41e5e0): 255 animations of 88-byte
    // frames, 15 parts posed about the model's own mesh pivots (no skeleton).
    bool parseKsa(const std::vector<uint8_t>& file);
    bool ksa() const { return ksa_; }
    // The part rotation for this file's convention: CHR Rz(-a0) Ry(a1)
    // Rx(-a2); KSA Rz(a0) Ry(a1) Rx(a2) (DF2.EXE 0x449df0).
    ChrMatrix rotationFor(const std::array<float, 3>& turns) const;
    const std::vector<ChrPart>& parts() const { return parts_; }
    const ChrAnimation* animation(int n) const;

    // DF.EXE 0x4701f4: per-part matrices for a frame, relative to the body
    // origin (the pelvis joint). Positions chain through the parents, the
    // rotations do not (each part's angles are absolute).
    std::vector<ChrMatrix> pose(const ChrFrame& f) const;
    // Rotation per part from angles in turns (yaw, pitch, roll as stored:
    // yaw = -a0, pitch = a1, roll = -a2 applied here), model axes; no
    // translation (the 3DI mesh pivots place the parts, 0x44ca34).
    static ChrMatrix partRotation(const std::array<float, 3>& turns);
    // Pose and hit spheres from interpolated angles (turns, raw a0..a2).
    std::vector<ChrMatrix> pose(const std::vector<std::array<float, 3>>& turns) const;
    std::vector<std::array<float, 4>> spheres(const std::vector<std::array<float, 3>>& turns) const;
    int partCount() const { return int(parts_.size()); }
    // Height of the origin above the lowest touching contact point
    // (0x4364c8: ground contact uses the animation's contact points).
    static float originHeight(const ChrFrame& f);
    // Hit-sphere centres in model axes relative to the origin (0x44dca4: the
    // part matrix applied to the raw centre, without an axis remap).
    std::vector<std::array<float, 4>> spheres(const ChrFrame& f) const;

private:
    std::vector<ChrPart> parts_;
    std::vector<int> order_;
    std::vector<ChrAnimation> anims_;   // by animation number (empty if absent)
    bool ksa_ = false;
};

} // namespace df
