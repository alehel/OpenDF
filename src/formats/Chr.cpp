#include "Chr.h"

#include <cmath>
#include <cstring>

namespace df {

namespace {
constexpr float kScale = float(0x1d2b0) / 65536.f / 65536.f;   // file units -> metres (0x469270)
constexpr float kTwoPi = 6.28318531f;

int32_t rd(const std::vector<uint8_t>& d, size_t o) {
    if (o + 4 > d.size()) return 0;
    return int32_t(uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24);
}

ChrMatrix multiply(const ChrMatrix& a, const ChrMatrix& b) {   // apply b, then a
    ChrMatrix r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + (j == 3 ? a.m[i][3] : 0.f);
    return r;
}

std::array<float, 3> transformPoint(const ChrMatrix& m, std::array<float, 3> v) {
    std::array<float, 3> r{};
    for (int i = 0; i < 3; ++i) r[size_t(i)] = m.m[i][0] * v[0] + m.m[i][1] * v[1] + m.m[i][2] * v[2] + m.m[i][3];
    return r;
}

ChrMatrix rotation(int axis, float turns) {   // 0x46d738
    const float a = turns * kTwoPi, c = std::cos(a), s = std::sin(a);
    ChrMatrix r{};
    if (axis == 0) r = {{{1, 0, 0, 0}, {0, c, -s, 0}, {0, s, c, 0}}};
    else if (axis == 1) r = {{{c, 0, -s, 0}, {0, 1, 0, 0}, {s, 0, c, 0}}};
    else r = {{{c, -s, 0, 0}, {s, c, 0, 0}, {0, 0, 1, 0}}};
    return r;
}

std::array<float, 3> remap(const std::array<float, 3>& v) { return {-v[2], -v[0], v[1]}; }   // 0x470375
} // namespace

bool Chr::parse(const std::vector<uint8_t>& d) {
    parts_.clear();
    order_.clear();
    anims_.clear();
    if (d.size() < 0x60 || (std::memcmp(d.data(), "CHR1", 4) != 0 && std::memcmp(d.data(), "CHR0", 4) != 0)) return false;
    const size_t frameHeader = d[3] == '1' ? 0x34 : 0x24;
    const int partOff = rd(d, 4), partCount = rd(d, 8), idxOff = rd(d, 0xC), idxCount = rd(d, 0x10);
    const int animTab = rd(d, 0x14);
    if (partCount <= 0 || partCount > 64 || idxCount <= 0 || idxCount > 64) return false;
    for (int p = 0; p < partCount; ++p) {
        const size_t b = size_t(partOff) + 0x5c * size_t(p);
        ChrPart part;
        for (int k = 0; k < 3; ++k) {
            part.joint[size_t(k)] = rd(d, b + 0x1c + 4 * k) * kScale;
            part.centre[size_t(k)] = rd(d, b + 0x28 + 4 * k) * kScale;
        }
        part.radius = rd(d, b + 0x34) * kScale;
        part.parent = rd(d, b + 0x38);
        parts_.push_back(part);
    }
    for (int k = 0; k < idxCount; ++k) order_.push_back(rd(d, size_t(idxOff) + 4 * size_t(k)));
    anims_.resize(255);
    for (int a = 0; a < 255; ++a) {
        const int ao = rd(d, size_t(animTab) + 4 * size_t(a));
        if (ao <= 0 || size_t(ao) + 0x14 > d.size()) continue;
        const int nparts = rd(d, size_t(ao) + 4), npts = rd(d, size_t(ao) + 8);
        if (nparts <= 0 || nparts > 64 || npts < 0 || npts > 16) continue;
        size_t fo = size_t(ao) + 0x14;
        for (int guard = 0; guard < 4096 && fo + frameHeader + 4 * size_t(nparts) <= d.size(); ++guard) {
            ChrFrame f;
            const int next = rd(d, fo);
            f.flags = uint32_t(rd(d, fo + 4));
            f.ticks = std::max(1, int(d[fo + 8] | d[fo + 9] << 8));
            const int pts = rd(d, fo + 0x20);
            for (int k = 0; k < npts; ++k)
                f.contacts.push_back({rd(d, size_t(pts) + 12 * size_t(k)) * kScale, rd(d, size_t(pts) + 12 * size_t(k) + 4) * kScale,
                                      rd(d, size_t(pts) + 12 * size_t(k) + 8) * kScale});
            for (int p = 0; p < nparts; ++p) {
                const size_t o = fo + frameHeader + 4 * size_t(p);
                f.angles.push_back({d[o], d[o + 1], d[o + 2]});
            }
            anims_[size_t(a)].frames.push_back(std::move(f));
            if (next <= 0) break;
            fo += size_t(next);
        }
    }
    return true;
}

const ChrAnimation* Chr::animation(int n) const {
    if (n < 0 || n >= int(anims_.size()) || anims_[size_t(n)].frames.empty()) return nullptr;
    return &anims_[size_t(n)];
}

std::vector<ChrMatrix> Chr::pose(const ChrFrame& f) const {
    std::vector<ChrMatrix> mats;
    for (size_t i = 0; i < order_.size() && i < f.angles.size(); ++i) {
        const int pi = order_[i];
        if (pi < 0 || pi >= int(parts_.size())) break;
        const ChrPart& p = parts_[size_t(pi)];
        std::array<float, 3> t{};
        if (p.parent >= 0 && p.parent < int(mats.size())) t = transformPoint(mats[size_t(p.parent)], remap(p.joint));
        // Angle bytes, 256 per turn: yaw = -a0, pitch = a1, roll = -a2;
        // M = T · Rz(yaw) · R1(pitch) · Rx(roll) (0x46df64).
        const float yaw = -f.angles[i][0] / 256.f, pitch = f.angles[i][1] / 256.f, roll = -f.angles[i][2] / 256.f;
        ChrMatrix m = multiply(rotation(2, yaw), multiply(rotation(1, pitch), rotation(0, roll)));
        m.m[0][3] = t[0];
        m.m[1][3] = t[1];
        m.m[2][3] = t[2];
        mats.push_back(m);
    }
    return mats;
}

float Chr::originHeight(const ChrFrame& f) {
    float lowest = 0;
    bool any = false;
    for (size_t k = 0; k < f.contacts.size(); ++k) {
        if (f.flags >> (16 + k) & 1) continue;
        if (!any || f.contacts[k][1] < lowest) lowest = f.contacts[k][1];
        any = true;
    }
    return any ? -lowest : 0.f;
}

std::vector<std::array<float, 4>> Chr::spheres(const ChrFrame& f) const {
    const auto mats = pose(f);
    std::vector<std::array<float, 4>> out;
    for (size_t i = 0; i < mats.size(); ++i) {
        const ChrPart& p = parts_[size_t(order_[i])];
        const auto c = transformPoint(mats[i], p.centre);
        out.push_back({c[0], c[1], c[2], p.radius});
    }
    return out;
}

} // namespace df
