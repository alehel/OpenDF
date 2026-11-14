#include "World.h"
#include "Moves.h"
#include <map>
#include "../formats/Anm.h"
#include "../formats/Chr.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace df {

namespace {

constexpr float kPi = 3.14159265f;

// Object frame: world = at + R * local, R = rotateY(pi/2 - heading).
struct Frame {
    Vec3 at;
    float c, s;
    Frame(Vec3 a, float heading) : at(a) {
        float th = kPi * 0.5f - heading;
        c = std::cos(th);
        s = std::sin(th);
    }
    Vec3 toLocal(Vec3 w) const {
        Vec3 d = w - at;
        return {c * d.x - s * d.z, d.y, s * d.x + c * d.z};
    }
};

bool insideXZ(const CollisionBox& b, Vec3 p) {
    return p.x >= b.min[0] && p.x <= b.max[0] && p.z >= b.min[2] && p.z <= b.max[2];
}

// Slab test of segment p0→p1 against box b; returns entry fraction.
bool segmentBox(Vec3 p0, Vec3 p1, const CollisionBox& b, float& tHit) {
    float t0 = 0, t1 = 1;
    const float o[3] = {p0.x, p0.y, p0.z};
    const float d[3] = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-9f) {
            if (o[a] < b.min[a] || o[a] > b.max[a]) return false;
            continue;
        }
        float inv = 1.f / d[a];
        float ta = (b.min[a] - o[a]) * inv, tb = (b.max[a] - o[a]) * inv;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    tHit = t0;
    return true;
}

} // namespace

Mat4 WorldObject::matrix(Vec3 at) const {
    Mat4 m = translate(at) * rotateY(kPi * 0.5f - heading);
    // Vehicles: pitch about the sideways axis (model +Z), roll about the
    // length (model +X).
    if (pitch != 0) {
        Mat4 r;
        const float c = std::cos(pitch), s = std::sin(pitch);
        r.at(0, 0) = c; r.at(0, 1) = -s;
        r.at(1, 0) = s; r.at(1, 1) = c;
        m = m * r;
    }
    if (roll != 0) m = m * rotateX(roll);
    if (fall > 0) {
        // Topple backwards about the model's sideways axis, lifted a little
        // so the body lies on the ground rather than in it.
        float a = fall * kPi * 0.5f;
        Mat4 r;
        r.at(0, 0) = std::cos(a); r.at(0, 1) = -std::sin(a);
        r.at(1, 0) = std::sin(a); r.at(1, 1) = std::cos(a);
        m = m * translate({0, (0.2f - hip) * fall, 0}) * r;
    }
    return m;
}

void World::refreshBoxes(WorldObject& o) const {
    o.boxes.clear();
    if (o.removed || o.dead || !o.model) return;
    if (std::getenv("OPENDF_DEBUGBOX") && o.item)
        for (const auto& b : o.model->collision)
            std::fprintf(stderr, "boxes %s: type %d r %.2f c (%.2f %.2f) x %.2f..%.2f z %.2f..%.2f\n", o.item->name.c_str(), b.type, b.r, b.cx, b.cz,
                         b.min[0], b.max[0], b.min[2], b.max[2]);
    if (!o.model->collision.empty()) {
        o.boxes = o.model->collision;
        return;
    }
    const std::string& type = o.item ? o.item->type : std::string();
    const auto& l = o.model->lods[0];
    if (o.person) {
        // Narrow box around the body: arms of the static pose stick out.
        o.boxes.push_back({{-0.25f, l.min[1], -0.25f}, {0.25f, l.max[1], 0.25f}});
    } else if (type == "vehicle" || type == "building" || type == "object" || o.hp > 0) {
        // No collision volumes in the file: use the model's bounds (also for
        // destructible decorations such as crates, so they can be shot).
        o.boxes.push_back({{l.min[0], l.min[1], l.min[2]}, {l.max[0], l.max[1], l.max[2]}});
    }
    // Decorations without volumes (bushes, ferns, grass) are walk-through.
}

void World::build(Vfs& vfs, ModelRenderer& models, const ItemDefs& items, const Mission& mission) {
    objects.clear();
    // Skeletons and move tables of people (ITEMS.DEF chr_file, anim_def).
    auto load = [&](const ItemDef& item, WorldObject& o) {
        if (item.chrFile.empty() || item.animDef.empty()) return;
        const std::string c = toUpper(item.chrFile) + ".CHR", a = toUpper(item.animDef) + ".ANM";
        if (!chrs_.count(c)) {
            std::vector<uint8_t> raw;
            if (!vfs.read(c, raw) || !chrs_[c].parse(raw)) {
                // Delta Force 2 and later: the .KSA animation file.
                chrs_[c] = Chr{};
                if (!vfs.read(toUpper(item.chrFile) + ".KSA", raw) || !chrs_[c].parseKsa(raw)) chrs_[c] = Chr{};
            }
        }
        if (!anms_.count(a)) {
            std::vector<uint8_t> raw;
            if (vfs.read(a, raw)) anms_[a].parse(std::string(raw.begin(), raw.end()));
        }
        if (chrs_[c].partCount() == 0 || anms_[a].empty()) return;
        o.chr = &chrs_[c];
        o.moves = &anms_[a];
    };
    const float ext = extent();
    for (const auto& mo : mission.objects) {
        const ItemDef* item = items.byMissionType(mo.type);
        if (!item || item->graphic.empty() || item->type == "effect") continue;
        WorldObject o;
        o.item = item;
        o.model = models.get(vfs, item->graphic);
        if (!o.model) continue;
        if (!item->husk.empty()) o.husk = models.get(vfs, item->husk);
        o.pos = {std::fmod(std::fmod(mo.x, ext) + ext, ext), 0, std::fmod(std::fmod(-mo.y, ext) + ext, ext)};
        o.pos.y = terrain->heightAt(o.pos.x, o.pos.z) + mo.z;
        o.heading = mo.heading * kPi / 180.f;
        o.lift = mo.z;
        o.person = item->type == "person";
        o.base = o.pos.y;
        // Delta Force 2 (0x45bb10): a person 100 m or more up starts on a
        // parachute, 1 m higher.
        if (df2Rules && o.person && mo.z >= 100.f) {
            o.chute = true;
            o.base += 1.f;
            o.pos.y += 1.f;
        }
        if (o.person) {
            // Character models have their origin at the hips; stand them on their feet.
            o.hip = -o.model->lods[0].min[1];
            o.pos.y += o.hip;
            load(*item, o);
        }
        o.hp = item->hp;
        o.missionGroup = mo.group;
        o.team = mo.raw[18] & 0xFF;
        o.side = (uint32_t(mo.raw[16]) >> 24) & 0xFF;
        o.mapMarker = (mo.raw[19] & 0xFFFF) != 0;

        o.missionX = mo.x;
        o.missionY = mo.y;
        o.missionIndex = int(&mo - mission.objects.data());
        refreshBoxes(o);
        objects.push_back(std::move(o));
    }
    // People stand (Standing_2HW, held on its frame 1).
    for (auto& o : objects)
        if (o.chr && o.moves) {
            startMove(o, 2);
            poseUpdate(o);
        }
}

int World::spawn(const ItemDef* item, const GpuModel* model, Vec3 pos, float heading) {
    WorldObject o;
    o.item = item;
    o.model = model;
    o.pos = pos;
    o.base = pos.y;
    o.heading = heading;
    o.hp = item ? item->hp : 0;
    refreshBoxes(o);
    objects.push_back(std::move(o));
    return int(objects.size()) - 1;
}

bool World::startMove(WorldObject& o, int number) {
    if (!o.chr || !o.moves) return false;
    const char* name = moveName(number);
    const AnimMove* mv = name ? o.moves->find(name) : nullptr;
    if (!mv || !o.chr->animation(mv->animation)) return false;
    o.anim.start(*o.chr, mv->animation);
    o.moveNumber = number;
    return true;
}

void World::requestMove(WorldObject& o, int number) {
    if (o.moveNumber != number) startMove(o, number);
}

std::vector<std::array<float, 4>> World::ksaSpheres(const GpuModel& m, const Chr& chr,
                                                   const std::vector<std::array<float, 3>>& turns, float aimPitch) {
    std::vector<std::array<float, 4>> out;
    if (m.lods.empty()) return out;
    const auto& l = m.lods[0];
    const float aim = std::clamp(aimPitch, -kPi / 2, kPi / 2) / (2 * kPi);
    const size_t n = std::min(l.meshPivot.size(), turns.size());
    std::vector<ChrMatrix> rot(n);
    std::vector<std::array<float, 3>> origin(n);   // file axes (x forward, y left, z up)
    auto fileAxes = [](const std::array<float, 3>& p) { return std::array<float, 3>{p[0], -p[2], p[1]}; };
    for (size_t k = 0; k < n; ++k) {
        std::array<float, 3> t = turns[k];
        if (k == 1) t[1] += aim * 0.25f;
        if (k == 2) t[1] += aim * 0.75f;
        rot[k] = chr.rotationFor(t);
        const auto pivot = fileAxes(l.meshPivot[k]);
        const int parent = k < l.meshParent.size() ? l.meshParent[k] : -1;
        if (k == 0 || parent < 0 || size_t(parent) >= k) origin[k] = pivot;
        else {
            // The pivot offset from the parent's, carried by the parent's rotation.
            const auto pp = fileAxes(l.meshPivot[size_t(parent)]);
            const float rel[3] = {pivot[0] - pp[0], pivot[1] - pp[1], pivot[2] - pp[2]};
            const ChrMatrix& r = rot[size_t(parent)];
            for (int i = 0; i < 3; ++i)
                origin[k][size_t(i)] = origin[size_t(parent)][size_t(i)] + r.m[i][0] * rel[0] + r.m[i][1] * rel[1] + r.m[i][2] * rel[2];
        }
        const float radius = k < l.meshRadius.size() ? l.meshRadius[k] : 0.f;
        out.push_back({origin[k][0], origin[k][1], origin[k][2], radius});
    }
    return out;
}

void World::poseUpdate(WorldObject& o) {
    // Hit spheres from the current pose (0x4701f4); the origin sits the
    // pose's contact-point height above the ground (0x4364c8).
    if (!o.chr) return;
    if (o.chr->ksa()) o.spheres = o.model ? ksaSpheres(*o.model, *o.chr, o.anim.angles(), o.aimPitch) : decltype(o.spheres){};
    else o.spheres = o.chr->spheres(o.anim.angles());
    if (const ChrFrame* f = o.anim.currentFrame(*o.chr)) {
        o.hip = Chr::originHeight(*f);
        o.pos.y = o.base + o.hip;
    }
}

int World::deathMove(int index, int type, int part, Vec3 from) const {
    const WorldObject& o = objects[size_t(index)];
    const float water = terrain->waterLevel();
    if (water != 0 && water >= o.pos.y) return 0xFE;
    const Vec3 v = nearest(from, o.pos) - o.pos;
    float rel = std::atan2(v.x, -v.z) - o.heading;           // clockwise from the victim's facing
    rel -= std::floor(rel / (2 * kPi) + 0.5f) * 2 * kPi;     // -pi..pi
    const int dir = std::abs(rel) <= kPi / 4 ? 0 : std::abs(rel) >= 3 * kPi / 4 ? 2 : rel < 0 ? 1 : 3;
    if (type == 1) return 0xBE + std::clamp(part, 0, 14) * 4 + dir;
    if (type == 2) return 0xFA + dir;
    return 0xFF;
}

Vec3 World::nearest(Vec3 p, Vec3 ref) const {
    const float ext = extent();
    auto wrap = [&](float v, float r) {
        float d = std::fmod(v - r, ext);
        if (d > ext * 0.5f) d -= ext;
        if (d < -ext * 0.5f) d += ext;
        return r + d;
    };
    return {wrap(p.x, ref.x), p.y, wrap(p.z, ref.z)};
}

float World::groundHeight(Vec3 feet, float step) const {
    float h = terrain->heightAt(feet.x, feet.z);
    for (const auto& o : objects) {
        if (o.boxes.empty()) continue;
        Vec3 at = nearest(o.pos, feet);
        float dx = at.x - feet.x, dz = at.z - feet.z;
        if (dx * dx + dz * dz > (o.model->radius + 1) * (o.model->radius + 1)) continue;
        Frame f(at, o.heading);
        Vec3 local = f.toLocal(feet);
        for (const auto& b : o.boxes) {
            if (!insideXZ(b, local)) continue;
            float top = at.y + b.max[1];
            if (top <= feet.y + step && top > h) h = top;
        }
    }
    return h;
}

Vec3 World::resolve(Vec3 feet, float radius, float height, float step) const {
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& o : objects) {
            if (o.boxes.empty()) continue;
            Vec3 at = nearest(o.pos, feet);
            float dx = at.x - feet.x, dz = at.z - feet.z;
            float reach = o.model->radius + radius + 1;
            if (dx * dx + dz * dz > reach * reach) continue;
            Frame f(at, o.heading);
            for (const auto& b : o.boxes) {
                float bottom = at.y + b.min[1], top = at.y + b.max[1];
                if (feet.y + step >= top || feet.y + height <= bottom) continue;  // can step onto it / passes under
                if (b.type != 1 && b.type != 3) continue;   // other volume types are ignored (0x437a28)
                // 0x437a28: the mover is a square of +-radius in the
                // object's frame; an overlap is pushed out along the axis
                // needing the smaller push (x on a tie), toward the side
                // away from the box centre.
                Vec3 l = f.toLocal(feet);
                if (l.x + radius < b.min[0] || l.x - radius > b.max[0] || l.z + radius < b.min[2] || l.z - radius > b.max[2]) continue;
                const float cx = (b.min[0] + b.max[0]) * 0.5f, cz = (b.min[2] + b.max[2]) * 0.5f;
                const float qx = l.x < cx ? b.min[0] - (l.x + radius) : b.max[0] - (l.x - radius);
                const float qz = l.z < cz ? b.min[2] - (l.z + radius) : b.max[2] - (l.z - radius);
                float px = 0, pz = 0;
                if (b.type == 3) {
                    // Cylinder: within r + radius of the centre, set out to
                    // (r + radius) x 1.01 along the line from the centre.
                    const float dx = l.x - b.cx, dz = l.z - b.cz, dist = std::sqrt(dx * dx + dz * dz);
                    const float k = (b.r + radius) * 1.01f;
                    if (dist > b.r + radius) continue;
                    const float a = std::atan2(dz, dx);
                    px = b.cx + k * std::cos(a) - l.x;
                    pz = b.cz + k * std::sin(a) - l.z;
                } else if (std::abs(qz) >= std::abs(qx)) px = qx; else pz = qz;
                // Back to world: inverse of toLocal's rotation.
                feet.x += f.c * px + f.s * pz;
                feet.z += -f.s * px + f.c * pz;
            }
        }
    }
    return feet;
}

// A round against one object's LOD 0 faces (0x44e918, per object): the
// pose routine of its draw code gives each mesh's matrix and skip flag
// (gnrc and others: the object matrix, all hittable; helo 0x478070: rotors
// unrotated, meshes 3 and 5 skipped while the main rotor is slow, 2 and 4
// when fast; tire 0x4807e0: mesh 1 skipped, front wheels 2/3 steered;
// efct/shrk: never hit). Per face: the file's face box, materials with
// 0x01000000 let rounds through, the plane crossing from the front (both
// ways with material flag 0x2), t = |ds| / (|ds| + |de|), kept if <= the
// best so far, then the point in the triangle on the face's projection.
// Shared by rounds and sight lines: the object's LOD 0 meshes posed by its
// draw routine, the segment taken into each mesh (file axes), each face's
// box, plane crossing (front to back; both ways with material 0x2), t and
// DF.EXE's crossing-number point-in-triangle test. `visit(face, mesh, t)`
// returns false to stop. `skipMask`: the pose slot flags that skip a mesh.
template <class Visit>
static void testFaces(const World& world, const WorldObject& o, Vec3 a, Vec3 b, std::array<uint8_t, 64>& hitSkip, uint32_t skipMaterial,
                      float maxT, Visit visit) {
    const GpuModel& m = *o.model;
    const std::string& code = m.lodCodes[0];
    if (code == "efct" || code == "shrk" || m.hitMeshes.empty()) return;
    const size_t meshes = m.hitMeshes.size();
    const Vec3 at = world.nearest(o.pos, a);
    const Mat4 body = o.matrix(at);
    std::vector<Mat4> pose(meshes, Mat4{});
    auto about = [&](int hp, const Mat4& r) {
        const auto& p = m.points[size_t(hp)];
        return translate({p[0], p[1], p[2]}) * r * translate({-p[0], -p[1], -p[2]});
    };
    if (code == "helo") {
        for (size_t i = 0; i < meshes && i < hitSkip.size(); ++i) hitSkip[i] = 0;
        const bool fast = o.rotorSpeed >= float(0x0C000000);
        for (int i : fast ? std::array<int, 2>{2, 4} : std::array<int, 2>{3, 5})
            if (i < int(hitSkip.size())) hitSkip[size_t(i)] = 1;
    } else if (code == "tire") {
        hitSkip[1] = 1;   // the shadow; slots 0, 2, 3 keep their flags (0x4807e0)
        if (m.points.size() >= 3) {
            const Mat4 steer = rotateY(-o.steer);
            if (meshes > 2) pose[2] = about(2, steer);
            if (meshes > 3) pose[3] = about(1, steer);
        }
    } else {
        for (size_t i = 0; i < meshes && i < hitSkip.size(); ++i) hitSkip[i] = 0;
    }
    for (size_t mi = 0; mi < meshes; ++mi) {
        if (mi < hitSkip.size() && hitSkip[mi]) continue;
        const Mat4 w = body * pose[mi];
        auto toFile = [&](Vec3 p) {
            const Vec3 q{p.x - w.at(0, 3), p.y - w.at(1, 3), p.z - w.at(2, 3)};
            const Vec3 l{w.at(0, 0) * q.x + w.at(1, 0) * q.y + w.at(2, 0) * q.z, w.at(0, 1) * q.x + w.at(1, 1) * q.y + w.at(2, 1) * q.z,
                         w.at(0, 2) * q.x + w.at(1, 2) * q.y + w.at(2, 2) * q.z};
            return std::array<float, 3>{l.x, -l.z, l.y};
        };
        const auto ls = toFile(a), le = toFile(b);
        float lo[3], hi[3];
        for (int c = 0; c < 3; ++c) lo[c] = std::min(ls[c], le[c]), hi[c] = std::max(ls[c], le[c]);
        for (const HitFace& f : m.hitMeshes[mi]) {
            if (lo[0] > f.box[1] || hi[0] < f.box[0] || lo[1] > f.box[3] || hi[1] < f.box[2] || lo[2] > f.box[5] || hi[2] < f.box[4]) continue;
            if (f.material & skipMaterial) continue;
            const float ds = f.n[0] * ls[0] + f.n[1] * ls[1] + f.n[2] * ls[2] + f.d;
            const float de = f.n[0] * le[0] + f.n[1] * le[1] + f.n[2] * le[2] + f.d;
            if (f.material & 0x2) { if ((ds > 0 && de > 0) || (ds <= 0 && de <= 0)) continue; }
            else if (!(ds > 0 && de <= 0)) continue;
            const float sum = std::abs(ds) + std::abs(de);
            if (sum <= 0) continue;
            const float t = std::abs(ds) / sum;
            if (t > maxT) continue;
            const float h[3] = {ls[0] + (le[0] - ls[0]) * t, ls[1] + (le[1] - ls[1]) * t, ls[2] + (le[2] - ls[2]) * t};
            const int ua = f.axis == 4 ? 1 : 0, va = f.axis == 1 ? 1 : 2;
            const float pu = h[ua], pv = h[va];
            int c = 0;
            bool aboveA = f.v[0][va] >= pv;
            for (int k = 0; k < 3 && c < 2; ++k) {
                const float* A = f.v[k];
                const float* B = f.v[(k + 1) % 3];
                const bool aboveB = B[va] >= pv;
                if (aboveA != aboveB) {
                    const bool ltA = pu > A[ua], ltB = pu > B[ua];
                    if (!ltA && !ltB) ++c;
                    else if (ltA != ltB) {
                        const float x = A[ua] + (B[ua] - A[ua]) / (B[va] - A[va]) * (pv - A[va]);
                        if (x >= pu) ++c;
                    }
                }
                aboveA = aboveB;
            }
            if (c != 1) continue;
            if (!visit(f, int(mi), t)) return;
        }
    }
}

bool World::roundHit(const WorldObject& o, Vec3 a, Vec3 b, float& best, bool haveObject, int& mesh, uint32_t& material) const {
    bool found = false;
    testFaces(*this, o, a, b, hitSkip_, 0x01000000, 2.f, [&](const HitFace& f, int mi, float t) {
        // Ties: a later face wins among objects, not over the terrain.
        if (haveObject || found ? t > best : t >= best) return true;
        best = t;
        mesh = mi;
        material = f.material;
        found = true;
        return true;
    });
    return found;
}

// Sight lines (0x45be04): 800 m at most; the terrain; then objects' faces
// as rounds see them (0x45b6e0) but every object (no hidden skip), the
// intact model, faces with material 0x800 skipped, and a face with a
// 0x868C material bit (translucent, keyed, ...) counted as see-through
// while any other blocks; people never block; +1 when either end is at or
// below a water level. Result: <= 0 blocked, else 1 + see-through count.
int World::sight(Vec3 a, Vec3 b, int looker, int target) const {
    b = nearest(b, a);
    const Vec3 d = b - a;
    const float len = std::sqrt(dot(d, d));
    if (len >= 800.f) return 0;
    if (len < 1e-4f) return 1;
    RayHit h;
    if (raycast(a, b, h, false)) return 0;
    const Vec3 mid = a + d * 0.5f;
    int extra = 0;
    for (size_t i = 0; i < objects.size(); ++i) {
        const WorldObject& o = objects[i];
        if (int(i) == looker || int(i) == target || o.person || !o.model || o.removed) continue;
        const Vec3 at = nearest(o.pos, mid);
        const float R = o.model->headerRadius;
        if (at.x < std::min(a.x, b.x) - R || at.x > std::max(a.x, b.x) + R || at.y < std::min(a.y, b.y) - R ||
            at.y > std::max(a.y, b.y) + R || at.z < std::min(a.z, b.z) - R || at.z > std::max(a.z, b.z) + R)
            continue;
        const Vec3 dir = d * (1.f / len);
        const Vec3 c = a + dir * dot(at - a, dir);
        const Vec3 off = c - at;
        float x = std::abs(off.x), y = std::abs(off.y), z = std::abs(off.z);
        if (x < y) std::swap(x, y);
        if (x < z) std::swap(x, z);
        if (y < z) std::swap(y, z);
        if (x + y * 11.f / 32.f + z * 0.25f > R) continue;
        bool blocked = false;
        testFaces(*this, o, a, b, hitSkip_, 0x800, 2.f, [&](const HitFace& f, int, float) {
            if (f.material & 0x868C) { ++extra; return true; }
            blocked = true;
            return false;
        });
        if (blocked) return 0;
    }
    if (const float w = terrain->waterLevel(); w != 0 && (a.y <= w || b.y <= w)) ++extra;
    return 1 + extra;
}

bool World::raycast(Vec3 a, Vec3 b, RayHit& hit, bool objects, int ignore, bool people, bool rounds) const {
    hit = RayHit{};
    bool found = false;
    Vec3 d = b - a;
    const float len = std::sqrt(dot(d, d));
    if (len < 1e-5f) return false;

    // Terrain: march, then refine by bisection.
    const int steps = std::max(2, int(len / 0.5f));
    float prev = 0;
    for (int i = 1; i <= steps; ++i) {
        float t = float(i) / steps;
        Vec3 p = a + d * t;
        if (p.y < terrain->heightAt(p.x, p.z)) {
            float lo = prev, hi = t;
            for (int k = 0; k < 8; ++k) {
                float mid = (lo + hi) * 0.5f;
                Vec3 q = a + d * mid;
                if (q.y < terrain->heightAt(q.x, q.z)) hi = mid; else lo = mid;
            }
            hit.t = hi;
            hit.object = -1;
            found = true;
            break;
        }
        prev = t;
    }

    Vec3 mid = a + d * 0.5f;
    // People (and the player): only if the path passes within 1.5 m of the
    // body origin, then each part's sphere (0x44dca4); nearest wins.
    auto approxLen = [](Vec3 v) {
        float x = std::abs(v.x), y = std::abs(v.y), z = std::abs(v.z);
        if (x < y) std::swap(x, y);
        if (x < z) std::swap(x, z);
        if (y < z) std::swap(y, z);
        return x + y * 11.f / 32.f + z * 0.25f;
    };
    if (objects && people && ignore != -3 && playerBody.present) {
        const Vec3 at = nearest(playerBody.origin, mid);
        auto closest = [&](Vec3 c, float& t) {
            t = std::clamp(dot(c - a, d) / (len * len), 0.f, 1.f);
            return approxLen(a + d * t - c);
        };
        float tc;
        if (closest(at, tc) <= 1.5f) {
            const float h = playerBody.heading;
            const Vec3 fwd{std::sin(h), 0, -std::cos(h)}, left{-std::cos(h), 0, -std::sin(h)};
            for (size_t k = 0; k < playerBody.spheres.size(); ++k) {
                const auto& sp = playerBody.spheres[k];
                const Vec3 c = at + fwd * sp[0] + left * sp[1] + Vec3{0, sp[2], 0};
                float t;
                if (closest(c, t) <= sp[3] && t < hit.t) {
                    hit.t = t;
                    hit.object = -3;
                    hit.part = int(k);
                    found = true;
                }
            }
        }
    }
    for (size_t i = 0; objects && i < this->objects.size(); ++i) {
        if (int(i) == ignore) continue;
        const auto& o = this->objects[i];
        if (!people && o.person) continue;
        // Living people (DF.EXE 0x44dca4): only if the path passes within 1.5 m
        // of the body origin, then each part's sphere; the nearest along the
        // path wins.
        if (o.person && !o.dead && !o.spheres.empty()) {
            const Vec3 at = nearest(o.pos, mid);
            // Distance as DF.EXE measures it (0x435bf8): largest component +
            // 11/32 of the middle one + 1/4 of the smallest.
            auto approxLen = [](Vec3 v) {
                float x = std::abs(v.x), y = std::abs(v.y), z = std::abs(v.z);
                if (x < y) std::swap(x, y);
                if (x < z) std::swap(x, z);
                if (y < z) std::swap(y, z);
                return x + y * 11.f / 32.f + z * 0.25f;
            };
            auto closest = [&](Vec3 c, float& t) {
                const Vec3 ac = c - a;
                t = std::clamp(dot(ac, d) / (len * len), 0.f, 1.f);
                return approxLen(a + d * t - c);
            };
            float tc;
            if (closest(at, tc) > 1.5f) continue;
            const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)}, left{-std::cos(o.heading), 0, -std::sin(o.heading)};
            for (size_t k = 0; k < o.spheres.size(); ++k) {
                const auto& sp = o.spheres[k];
                const Vec3 c = at + fwd * sp[0] + left * sp[1] + Vec3{0, sp[2], 0};
                float t;
                if (closest(c, t) <= sp[3] && t < hit.t) {
                    hit.t = t;
                    hit.object = int(i);
                    hit.part = int(k);
                    found = true;
                }
            }
            continue;
        }
        if (rounds && !o.person) {
            if (o.removed || !o.model) continue;
            // Broad phase (0x44e918): the header radius R; the segment box
            // grown by R must hold the object, and the line (not clamped to
            // the segment) must pass within R by DF.EXE's approximate length.
            const Vec3 at = nearest(o.pos, mid);
            const float R = o.model->headerRadius;
            if (at.x < std::min(a.x, b.x) - R || at.x > std::max(a.x, b.x) + R || at.y < std::min(a.y, b.y) - R ||
                at.y > std::max(a.y, b.y) + R || at.z < std::min(a.z, b.z) - R || at.z > std::max(a.z, b.z) + R)
                continue;
            const Vec3 dir = d * (1.f / len);
            const Vec3 c = a + dir * dot(at - a, dir);
            const Vec3 off = c - at;
            float x = std::abs(off.x), y = std::abs(off.y), z = std::abs(off.z);
            if (x < y) std::swap(x, y);
            if (x < z) std::swap(x, z);
            if (y < z) std::swap(y, z);
            if (x + y * 11.f / 32.f + z * 0.25f > R) continue;
            float best = hit.t;
            int mesh = -1;
            uint32_t material = 0;
            if (roundHit(o, a, b, best, found && hit.object >= 0 && !this->objects[size_t(hit.object)].person, mesh, material)) {
                hit.t = best;
                hit.object = int(i);
                hit.part = -1;
                hit.mesh = mesh;
                hit.material = material;
                found = true;
            }
            continue;
        }
        if (o.boxes.empty()) continue;
        Vec3 at = nearest(o.pos, mid);
        Vec3 off = at - mid;
        float reach = o.model->radius + len * 0.5f;
        if (off.x * off.x + off.z * off.z > reach * reach) continue;
        Frame f(at, o.heading);
        Vec3 la = f.toLocal(a), lb = f.toLocal(b);
        for (const auto& bx : o.boxes) {
            float t;
            if (segmentBox(la, lb, bx, t) && t < hit.t) {
                hit.t = t;
                hit.object = int(i);
                found = true;
            }
        }
    }
    if (found) hit.point = a + d * hit.t;
    return found;
}

std::string World::damage(int index, int amount, int power, int deathMove, int killer) {
    if (index < 0 || index >= int(objects.size())) return {};
    WorldObject& o = objects[index];
    if (o.removed || o.dead) return {};
    if (o.item && power < o.item->damageClass) return {};
    if (o.person) {
        // People have ITEMS.DEF's hit points (2-4) and die at 0. Head hits
        // (x4) need the body-part spheres of the .CHR skeleton: not yet.
        o.hp -= amount;
        if (o.hp > 0) return {};
        o.dead = true;
        o.killer = killer;
        // The death animation (0x46c920 / 0x469764); without one, topple.
        if (!(deathMove >= 0 && startMove(o, deathMove)) && !startMove(o, 0xBE)) o.fall = 0.001f;
        refreshBoxes(o);
        return o.item->name + " down";
    }
    if (o.hp <= 0 || !o.item) return {};   // indestructible
    o.hp -= amount;
    if (o.hp > 0) return {};
    if (df2Rules && o.item->aiFunction == "bld2") {
        // DF2.EXE 0x4028d0: the collapse runs over 64 thinks; the husk at step 8.
        o.hp = 0;
        o.collapse = 0;
        destroyed_.push_back(index);
        return o.item->name + " destroyed";
    }
    applyHusk(index);
    return o.item->name + " destroyed";
}

void World::applyHusk(int index) {
    WorldObject& o = objects[size_t(index)];
    if (o.husk) {
        o.model = o.husk;
        o.husk = nullptr;
        o.wrecked = true;
    } else {
        o.removed = true;
    }
    o.hp = 0;
    if (o.collapse < 0) destroyed_.push_back(index);
    // An explosive barrel goes off 10 ticks after it is destroyed (0x45a3c0).
    if (o.item->aiFunction == "brrl" && o.detonate < 0) o.detonate = 10;
    refreshBoxes(o);
}

// Blasts (0x45019a..0x450492), distances by DF.EXE's approximate length
// (0x435bf8: largest + 11/32 middle + 1/4 smallest), by list:
// people: to the origin within R + 1 m (per axis and in length); objects and
// vehicles (BMS group 0: types object, vehicle): to the origin, reach
// widened to flat radius + R; buildings and decorations (group 1): the
// excess outside a square box about the origin, +-flat radius across and
// +-height up and down (not turned with the object), within R. Objects
// (0x44ff27): 128 - 16 d (heavy) or 32 - 4 d; when neither sight line from
// the origin or 0.5 m above it reaches 0.5 m above the blast: nothing for
// type 1, half otherwise.
std::vector<std::string> World::blast(Vec3 at, float radius, bool heavy) {
    std::vector<std::string> out;
    auto len3 = [](float a, float b, float c) {
        a = std::abs(a), b = std::abs(b), c = std::abs(c);
        if (a < b) std::swap(a, b);
        if (a < c) std::swap(a, c);
        if (b < c) std::swap(b, c);
        return a + b * 11.f / 32.f + c * 0.25f;
    };
    const Vec3 target = at + Vec3{0, 0.5f, 0};
    for (size_t i = 0; i < objects.size(); ++i) {
        WorldObject& o = objects[i];
        if (!o.alive()) continue;
        const Vec3 p = nearest(o.pos, at);
        if (o.person) {
            const float r = radius + 1.f;
            if (std::abs(p.x - at.x) > r || std::abs(p.y - at.y) > r || std::abs(p.z - at.z) > r) continue;
            float d = len3(p.x - at.x, p.y - at.y, p.z - at.z);
            if (d > r) continue;
            // Line of sight (0x45be04): from the body to the blast point
            // raised 0.5 m, then from 0.5 m higher; both blocked: no damage
            // for type 1, double distance otherwise.
            RayHit h;
            if (raycast(p, target, h, false) && raycast(p + Vec3{0, 0.5f, 0}, target, h, false)) {
                if (!heavy) continue;
                d *= 2;
            }
            // DF2.EXE 0x44d4a0: base 8 (type 5, the 12 m charge: 32), full under
            // 4 m, half under 8 m, a quarter beyond.
            const int base = radius > 11.f ? 32 : 8;
            const int amount = df2Rules ? (d < 4 ? base : d < 8 ? base / 2 : base / 4) : d < 4 ? 4 : d < 8 ? 2 : 1;
            // Death move type 2, or 3 one time in four at 4-8 m (0x44fda8).
            const int type = (d >= 4 && d < 8 && (std::rand() & 3) == 0) ? 3 : 2;
            std::string m = damage(int(i), amount, 99, deathMove(int(i), type, 0, at));
            if (!m.empty()) out.push_back(m);
            continue;
        }
        if (o.hp <= 0 || !o.model) continue;
        const std::string& type = o.item ? o.item->type : std::string();
        const float h = o.model->flatRadius, v = o.model->height;
        float d;
        if (type == "object" || type == "vehicle") {
            const float r = h + radius;
            if (std::abs(p.x - at.x) > r || std::abs(p.y - at.y) > r || std::abs(p.z - at.z) > r) continue;
            d = len3(p.x - at.x, p.y - at.y, p.z - at.z);
            if (d > r) continue;
        } else {
            const float ex = std::max(0.f, std::abs(at.x - p.x) - h), ez = std::max(0.f, std::abs(at.z - p.z) - h);
            const float ey = std::max(0.f, std::abs(at.y - p.y) - v);
            if (ex > radius || ey > radius || ez > radius) continue;
            d = len3(ex, ey, ez);
            if (d > radius) continue;
        }
        int amount = heavy ? int(128 - 16 * d) : int(32 - 4 * d);
        if (df2Rules) {
            // DF2.EXE 0x44d4a0: type 5 (12 m) 512 - 16 d, type 3 128 - 16 d,
            // type 1 32 - 4 d; blast class 3 / 2 / 0, +1 inside the object's
            // box, must reach the item's third hp value (def+0x9c).
            const bool five = radius > 11.f;
            amount = five ? int(512 - 16 * d) : heavy ? int(128 - 16 * d) : int(32 - 4 * d);
            int cls = five ? 3 : heavy ? 2 : 0;
            if (d <= 0.f) ++cls;
            if (o.item && cls < o.item->hp3) continue;
        }
        if (sight(p, target, int(i), -1) <= 0 && sight(p + Vec3{0, 0.5f, 0}, target, int(i), -1) <= 0) {
            if (!heavy) continue;
            amount >>= 1;
        }
        if (amount <= 0) continue;
        std::string m = damage(int(i), amount);
        if (!m.empty()) out.push_back(m);
    }
    return out;
}

void World::update(float dt) {
    // DF2 parachutes (0x440b2b): -0x2222 a tick (0.133 m) while above the
    // ground and the water; landing ends it and leaves the canopy.
    for (chuteClock_ += dt; chuteClock_ >= 1.f / 60.f; chuteClock_ -= 1.f / 60.f)
        for (size_t i = 0; i < objects.size(); ++i) {
            WorldObject& o = objects[i];
            if (!o.chute) continue;
            o.base -= 0x2222 / 65536.f;
            const float ground = terrain->heightAt(o.pos.x, o.pos.z), water = terrain->waterLevel();
            if (o.base <= ground || (water != 0 && o.base < water)) {
                o.base = std::max(o.base, ground);
                o.chute = false;
                landings_.push_back(int(i));
            }
            o.pos.y = o.base + o.hip;
        }
    for (auto& o : objects)
        if (o.dead && o.fall > 0 && o.fall < 1) o.fall = std::min(1.f, o.fall + dt * 2.5f);
    // People's animations: 30 ticks a second (the odd 60 Hz ticks).
    for (animClock_ += dt; animClock_ >= 1.f / 60.f; animClock_ -= 1.f / 60.f) {
        if (!(++animTicks_ & 1)) continue;
        for (size_t i = 0; i < objects.size(); ++i) {
            WorldObject& o = objects[i];
            if (!o.chr || o.removed) continue;
            if (const int ev = o.anim.tick(*o.chr)) animEvents_.push_back({int(i), ev});
            // Override moves (deaths) carry the body along (0x43666b).
            if (o.dead) {
                const auto& r = o.anim.rootStep();
                const Vec3 fwd{std::sin(o.heading), 0, -std::cos(o.heading)}, left{-std::cos(o.heading), 0, -std::sin(o.heading)};
                o.pos = o.pos + fwd * r[0] + left * r[1];
                o.base = terrain->heightAt(o.pos.x, o.pos.z) + o.lift;
            }
            poseUpdate(o);
        }
    }
}

std::vector<std::string> World::stab(Vec3 at, Vec3 attacker) {
    std::vector<std::string> out;
    for (size_t i = 0; i < objects.size(); ++i) {
        WorldObject& o = objects[i];
        if (!o.alive()) continue;
        const Vec3 v = nearest(o.pos, at) - at;
        const float d = std::sqrt(dot(v, v));
        if (o.person ? d > 2.f : (!o.item || o.item->id != 104048 || d > 1.f)) continue;
        // Death move 0x46c920(obj, 2, dir, 1): type 1, part 2 -> 0xC6 + dir.
        std::string m = damage(int(i), 8, 99, o.person ? deathMove(int(i), 1, 2, attacker) : -1);
        if (std::getenv("OPENDF_DEBUGHITS")) std::fprintf(stderr, "stab hits %s at %.2f m\n", o.item ? o.item->name.c_str() : "?", d);
        if (!m.empty()) out.push_back(m);
    }
    return out;
}

} // namespace df
