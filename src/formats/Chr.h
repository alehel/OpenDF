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

struct ChrFrame {
    uint32_t flags = 0;              // bit (16+k): contact point k not touching
    int ticks = 1;
    std::vector<std::array<float, 3>> contacts;   // CHR axes, metres
    std::vector<std::array<uint8_t, 3>> angles;   // per part: yaw, pitch, roll bytes (256 per turn)
};

struct ChrAnimation { std::vector<ChrFrame> frames; };

// A 3x4 matrix: rows of (rotation | translation), model axes, metres.
struct ChrMatrix { float m[3][4]; };

class Chr {
public:
    bool parse(const std::vector<uint8_t>& file);
    const std::vector<ChrPart>& parts() const { return parts_; }
    const ChrAnimation* animation(int n) const;

    // DF.EXE 0x4701f4: per-part matrices for a frame, relative to the body
    // origin (the pelvis joint). Positions chain through the parents, the
    // rotations do not (each part's angles are absolute).
    std::vector<ChrMatrix> pose(const ChrFrame& f) const;
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
};

} // namespace df
