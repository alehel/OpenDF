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
    playerMount = -1;
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
        if (mo.omitted) continue;
        const ItemDef* item = items.byMissionType(mo.type);
        if (!item || item->graphic.empty() || item->type == "effect") continue;
        WorldObject o;
        o.item = item;
        o.model = models.get(vfs, item->graphic);
        if (!o.model) continue;
        o.intact = o.model;
        if (!item->husk.empty()) o.husk = models.get(vfs, item->husk);
        o.pos = {std::fmod(std::fmod(mo.x, ext) + ext, ext), 0, std::fmod(std::fmod(-mo.y, ext) + ext, ext)};
        o.pos.y = terrain->heightAt(o.pos.x, o.pos.z) + mo.z;
        o.heading = mo.heading * kPi / 180.f;
        o.lift = mo.z;
        o.person = item->type == "person";
        // Record +0x36 -> object +0x34 (GOG Df2.exe 0x403790, GOG Df.exe
        // 0x449710): the body pitch; for people that word is the aim pitch.
        (o.person ? o.aimPitch : o.pitch) = mo.pitch * kPi / 180.f;
        o.base = o.pos.y;
        // Delta Force 2 (0x45bb10): a person 100 m or more up starts on a
        // parachute.
        if (df2Rules && o.person && mo.z >= 100.f) o.chute = true;
        if (o.person) {
            // DF.EXE 0x40385c / DF2.EXE 0x45bb53: a person's origin (the
            // pelvis) starts at terrain + z + 1 m; the feet follow from the
            // pose (set below) and gravity does the rest.
            o.pos.y += 1.f;
            o.hip = -o.model->lods[0].min[1];
            load(*item, o);
        }
        o.hp = item->hp;
        o.missionGroup = mo.group;
        // DF2 (0x40374a): BMS record +0xC bit 0x1000 -> AI +0x34 & 0x400, the
        // ladder test of the person move (0x440ab6).
        if (df2Rules && o.person) o.climbs = (uint32_t(mo.raw[3]) & 0x1000) != 0;
        o.team = mo.raw[18] & 0xFF;
        o.side = (uint32_t(mo.raw[16]) >> 24) & 0xFF;
        o.radarShape = int(int8_t(mo.raw[19] & 0xFF));
        o.mapMarker = df2Rules ? o.radarShape != 0 : (mo.raw[19] & 0xFF) != 0;   // DF.EXE +0x76: record byte +0x4C

        o.missionX = mo.x;
        o.missionY = mo.y;
        o.missionIndex = int(&mo - mission.objects.data());
        refreshBoxes(o);
        if (std::getenv("OPENDF_DEBUGOBJ"))
            std::fprintf(stderr, "object %zu %s (%s) group %d at mission (%.1f, %.1f) z %.2f ground %.2f height %.2f volumes %d\n", objects.size(),
                         item->name.c_str(), item->graphic.c_str(), mo.group, mo.x, mo.y, mo.z, o.pos.y - mo.z, o.model->height,
                         int(o.model->meshVolumes.size()));
        objects.push_back(std::move(o));
    }
    // People stand (Standing_2HW, held on its frame 1); the feet below the
    // origin by the pose.
    for (auto& o : objects)
        if (o.person) {
            const float origin = o.pos.y;
            if (o.chr && o.moves) {
                startMove(o, 2);
                o.hip = 0;
                if (o.anim.currentFrame(*o.chr)) o.hip = o.anim.originHeight(*o.chr);
            }
            o.base = origin - o.hip;
            poseUpdate(o);
            o.pos.y = o.base + o.hip;
        }
}

int World::spawn(const ItemDef* item, const GpuModel* model, Vec3 pos, float heading) {
    WorldObject o;
    o.item = item;
    o.model = model;
    o.intact = model;
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
    if (!df2Rules && ((number >= 250 && number <= 253) || number == 255)) smokeOwners_.push_back(int(&o - objects.data()));
    return true;
}

void World::requestMove(WorldObject& o, int number) {
    // DF2.EXE's move chooser 0x43f690 (READ 0x43f6fb..0x43f745): with the
    // origin below the water level the move is 141 Swimming when the mover
    // goes forward at some speed, else 140 Floating, whatever was asked
    // (after the parachute's 150). INFERRED for the AI: a move with a
    // velocity is a forward move (AiSystem only walks and runs forward).
    if (df2Rules && o.moves && !o.chute) {
        const float water = terrain->waterLevel();
        if (water != 0 && o.base + o.hip < water) {
            const char* name = moveName(number);
            const AnimMove* mv = name ? o.moves->find(name) : nullptr;
            number = mv && mv->velocity > 0 ? 141 : 140;
        }
    }
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
    if (o.anim.currentFrame(*o.chr)) {
        o.hip = o.anim.originHeight(*o.chr);
        o.pos.y = o.base + o.hip;
    }
}

int World::deathMoveNumber(bool df2, bool underWater, int kind, int part, int dir) {
    if (underWater) return 0xFE;   // Death_Water
    // Neither game clamps the part; DF2's numbers (170 + 4 part + dir, 230 +
    // dir, 235, 210, 234) name the same moves as DF1's below.
    if (kind == 1) return 0xBE + std::max(0, part) * 4 + dir;
    if (kind == 2) return 0xFA + dir;
    if (kind == 3) return 0xFF;
    if (df2 && kind == 4) return 0xBE + 10 * 4;   // Death_B_P11DF (0x415130 case 4: 0xD2)
    if (df2 && kind == 5) return 0xFE;            // Death_Water (case 5: 0xEA)
    if (df2) return dir;                          // other kinds return dir
    return 0xFF;   // DF.EXE returns an undefined value for other kinds (never called with them)
}

int World::deathMove(int index, int type, int part, Vec3 from) const {
    const WorldObject& o = objects[size_t(index)];
    const float water = terrain->waterLevel();
    const Vec3 v = nearest(from, o.pos) - o.pos;
    float rel = std::atan2(v.x, -v.z) - o.heading;           // clockwise from the victim's facing
    rel -= std::floor(rel / (2 * kPi) + 0.5f) * 2 * kPi;     // -pi..pi
    const int dir = std::abs(rel) <= kPi / 4 ? 0 : std::abs(rel) >= 3 * kPi / 4 ? 2 : rel < 0 ? 1 : 3;
    return deathMoveNumber(df2Rules, water != 0 && water >= o.pos.y, type, part, dir);
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

float World::groundHeight(Vec3 feet, float step, int* owner) const {
    if (!df2Rules) return faceGround(feet, step, owner);   // DF.EXE 0x435dc0 (people and the player)
    if (owner) *owner = -1;
    // Delta Force 2 (0x43fb80): the terrain or a volume top the point is in,
    // among the objects a person's list would hold (10 m margin, 0x43f2b0).
    return df2Surface(feet, step, df2Neighbours(feet, 10.f));
}

Vec3 World::resolve(Vec3 feet, float originAbove) const {
    if (df2Rules) return feet;   // DF2.EXE has no mover box: the volume push-out (0x440f80) in personTick
    // GOG Df.exe 0x4889c0 (CD 0x437a28), at most two passes over the mover's
    // list while something pushed (0x487370). The mover is a box around its
    // body origin (obj +0x2C): x, y +-0.75 m, height origin - 0.75 ..
    // origin + 1.0 m, in each mesh's frame.
    constexpr float kHalf = 0xc000 / 65536.f, kUp = 0x10000 / 65536.f;
    Vec3 org{feet.x, feet.y + originAbove, feet.z};
    for (int pass = 0; pass < 2; ++pass) {
        bool any = false;
        for (const auto& o : objects) {
            // The mover's list (0x436a34, rebuilt every tick by 0x436bec
            // into +0x104 / +0x108) takes its candidates from the object
            // arrays 0x820f60 and 0x796f60 (not the mover itself), never
            // from the people array 0x785b60: soldiers and the player do
            // not collide with people, their own body box included.
            if (o.person || o.removed || o.dead || !o.model || o.boxes.empty()) continue;
            // Only models with collision volumes (LOD +0xb0); the bounds
            // OpenDF makes up for the others (refreshBoxes) are for rounds.
            const GpuModel& m = *o.model;
            if (m.collision.empty()) continue;
            // The object's square: +-(model +0x64 + 0.75) around it.
            const Vec3 at = nearest(o.pos, org);
            const float reach = m.flatRadius + kHalf;
            if (std::abs(at.x - org.x) > reach || std::abs(at.z - org.z) > reach) continue;
            // A touch item (attrib movecb 0x10) never pushes (0x488c5b); its
            // touch is Game::touchPickups.
            const bool touch = o.item && (o.item->attrib & 0x10);
            const Mat4 w = o.matrix(at);
            auto toLocal = [&](Vec3 p) {
                const Vec3 q{p.x - w.at(0, 3), p.y - w.at(1, 3), p.z - w.at(2, 3)};
                return Vec3{w.at(0, 0) * q.x + w.at(1, 0) * q.y + w.at(2, 0) * q.z, w.at(0, 1) * q.x + w.at(1, 1) * q.y + w.at(2, 1) * q.z,
                            w.at(0, 2) * q.x + w.at(1, 2) * q.y + w.at(2, 2) * q.z};
            };
            for (size_t mi = 0; mi < m.meshBoxes.size(); ++mi) {
                // Each mesh with volumes (+0x1C): its box (+0x40..+0x54) first.
                const auto& mb = m.meshBoxes[mi];
                Vec3 l = toLocal(org);
                if (!(l.x - kHalf <= mb[3] && mb[0] <= l.x + kHalf && l.z - kHalf <= mb[5] && mb[2] <= l.z + kHalf &&
                      l.y - kHalf <= mb[4] && mb[1] <= l.y + kUp))
                    continue;
                bool pushed = false;
                for (const auto& b : m.collision) {
                    if (b.mesh != int(mi) || (b.type != 1 && b.type != 3)) continue;
                    if (!(b.min[0] <= l.x + kHalf && l.x - kHalf <= b.max[0] && b.min[2] <= l.z + kHalf && l.z - kHalf <= b.max[2] &&
                          b.min[1] <= l.y + kUp && l.y - kHalf <= b.max[1]))
                        continue;
                    if (b.type == 1) {
                        // Away from the volume's centre (+0x08..+0x10) along
                        // the axis needing the smallest push: x unless y or
                        // z needs less, then y unless z needs less (file
                        // axes; OpenDF z is file -y, y the height).
                        const float qx = l.x < b.cx ? b.min[0] - (l.x + kHalf) : b.max[0] - (l.x - kHalf);
                        const float qz = l.z > b.cz ? b.max[2] - (l.z - kHalf) : b.min[2] - (l.z + kHalf);
                        const float qy = l.y < b.cy ? b.min[1] - (l.y + kUp) : b.max[1] - (l.y - kHalf);
                        const float ax = std::abs(qx), az = std::abs(qz), ay = std::abs(qy);
                        if (ax <= az && ax <= ay) l.x += qx;
                        else if (az <= ax && az <= ay) l.z += qz;
                        else l.y += qy;
                        pushed = true;
                    } else {
                        // Upright cylinder: within r + 0.75 of the centre (on
                        // x, y), set out to (r + 0.75) x 1.01 (0x1028f).
                        const float dx = l.x - b.cx, dz = l.z - b.cz;
                        if (std::sqrt(dx * dx + dz * dz) > b.r + kHalf) continue;
                        const float k = (b.r + kHalf) * (0x1028f / 65536.f), a = std::atan2(dz, dx);
                        l.x = b.cx + k * std::cos(a);
                        l.z = b.cz + k * std::sin(a);
                        pushed = true;
                    }
                }
                if (!pushed) continue;
                // Written back (0x45cda0) unless the item is a touch item;
                // either way the pass counts as pushing.
                if (!touch) org = Vec3{w.at(0, 0) * l.x + w.at(0, 1) * l.y + w.at(0, 2) * l.z + w.at(0, 3),
                                       w.at(1, 0) * l.x + w.at(1, 1) * l.y + w.at(1, 2) * l.z + w.at(1, 3),
                                       w.at(2, 0) * l.x + w.at(2, 1) * l.y + w.at(2, 2) * l.z + w.at(2, 3)};
                any = true;
            }
        }
        if (!any) break;
    }
    return {org.x, org.y - originAbove, org.z};
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
// The object's LOD 0 meshes posed by its draw routine: fn(mesh, world
// matrix) for every mesh whose pose slot is not skipped.
template <class Fn>
static void forPosedMeshes(const World& world, const WorldObject& o, const GpuModel& m, Vec3 ref, std::array<uint8_t, 64>& hitSkip, Fn fn) {
    const std::string& code = m.lodCodes[0];
    if (code == "efct" || code == "shrk" || m.hitMeshes.empty()) return;
    const size_t meshes = m.hitMeshes.size();
    const Vec3 at = world.nearest(o.pos, ref);
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
    } else if (code == "palm" && !world.df2Rules) {
        // GOG Df.exe 0x44d2a0: every slot is the object matrix with its skip
        // flag clear, then the state's unused meshes are flagged.
        std::vector<bool> shown;
        palmPose(o.palmState, meshes, m.points, pose, shown);
        for (size_t i = 0; i < meshes && i < hitSkip.size(); ++i) hitSkip[i] = shown[i] ? 0 : 1;
    } else if (code == "ewpn") {
        for (size_t i = 0; i < meshes && i < hitSkip.size(); ++i) hitSkip[i] = 0;
        pose = world.ewpnPose(o, meshes);
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
        if (!fn(int(mi), body * pose[mi])) return;
    }
}

void palmPose(int state, size_t meshes, const std::vector<std::array<float, 3>>& points, std::vector<Mat4>& pose,
              std::vector<bool>& shown) {
    std::vector<int> drawn;
    int hp = -1;
    switch (state) {
    case 0: drawn = {0, 1, 2}; break;
    case 1: drawn = {0, 3}; break;
    case 2: drawn = {0, 1, 5}; break;
    case 0x10: drawn = {1, 2, 4}; hp = 0; break;
    case 0x11: drawn = {2}; hp = 1; break;
    case 0x20: drawn = {1, 4}; hp = 0; break;
    default: break;
    }
    Mat4 hide;
    for (float& x : hide.m) x = 0;
    Mat4 at;
    if (hp >= 0 && size_t(hp) < points.size()) {
        const auto& p = points[size_t(hp)];
        at = translate({-p[0], -p[1], -p[2]});
    }
    pose.assign(meshes, hide);
    shown.assign(meshes, false);
    for (int i : drawn)
        if (size_t(i) < meshes) pose[size_t(i)] = at, shown[size_t(i)] = true;
}

// File axes (x, y, z up) of a world point in a posed mesh.
static std::array<float, 3> toFileAxes(const Mat4& w, Vec3 p) {
    const Vec3 q{p.x - w.at(0, 3), p.y - w.at(1, 3), p.z - w.at(2, 3)};
    const Vec3 l{w.at(0, 0) * q.x + w.at(1, 0) * q.y + w.at(2, 0) * q.z, w.at(0, 1) * q.x + w.at(1, 1) * q.y + w.at(2, 1) * q.z,
                 w.at(0, 2) * q.x + w.at(1, 2) * q.y + w.at(2, 2) * q.z};
    return {l.x, -l.z, l.y};
}

// DF.EXE's crossing-number point-in-triangle test on projection (ua, va).
static bool inTriangle(const HitFace& f, float pu, float pv, int ua, int va) {
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
    return c == 1;
}

template <class Visit>
static void testFaces(const World& world, const WorldObject& o, const GpuModel& m, Vec3 a, Vec3 b, std::array<uint8_t, 64>& hitSkip,
                      uint32_t skipMaterial, float maxT, bool bothSides, Visit visit) {
    forPosedMeshes(world, o, m, a, hitSkip, [&](int mi, const Mat4& w) {
        auto toFile = [&](Vec3 p) {
            const Vec3 q{p.x - w.at(0, 3), p.y - w.at(1, 3), p.z - w.at(2, 3)};
            const Vec3 l{w.at(0, 0) * q.x + w.at(1, 0) * q.y + w.at(2, 0) * q.z, w.at(0, 1) * q.x + w.at(1, 1) * q.y + w.at(2, 1) * q.z,
                         w.at(0, 2) * q.x + w.at(1, 2) * q.y + w.at(2, 2) * q.z};
            return std::array<float, 3>{l.x, -l.z, l.y};
        };
        const auto ls = toFile(a), le = toFile(b);
        float lo[3], hi[3];
        for (int c = 0; c < 3; ++c) lo[c] = std::min(ls[c], le[c]), hi[c] = std::max(ls[c], le[c]);
        for (const HitFace& f : m.hitMeshes[size_t(mi)]) {
            if (lo[0] > f.box[1] || hi[0] < f.box[0] || lo[1] > f.box[3] || hi[1] < f.box[2] || lo[2] > f.box[5] || hi[2] < f.box[4]) continue;
            if (f.material & skipMaterial) continue;
            const float ds = f.n[0] * ls[0] + f.n[1] * ls[1] + f.n[2] * ls[2] + f.d;
            const float de = f.n[0] * le[0] + f.n[1] * le[1] + f.n[2] * le[2] + f.d;
            // bothSides: any crossing of the plane, whatever the material
            // (GOG Df.exe rounds, 0x464a44..0x464b71); else from the front
            // only, both ways with material 0x2 (CD 0x44e918, sight 0x42db80).
            if (bothSides || (f.material & 0x2)) { if ((ds > 0) == (de > 0)) continue; }
            else if (!(ds > 0 && de <= 0)) continue;
            const float sum = std::abs(ds) + std::abs(de);
            if (sum <= 0) continue;
            const float t = std::abs(ds) / sum;
            if (t > maxT) continue;
            const float h[3] = {ls[0] + (le[0] - ls[0]) * t, ls[1] + (le[1] - ls[1]) * t, ls[2] + (le[2] - ls[2]) * t};
            const int ua = f.axis == 4 ? 1 : 0, va = f.axis == 1 ? 1 : 2;
            if (!inTriangle(f, h[ua], h[va], ua, va)) continue;
            if (!visit(f, mi, t)) return false;
        }
        return true;
    });
}

float World::faceGround(Vec3 feet, float step, int* owner, int skip) const {
    float best = terrain->heightAt(feet.x, feet.z);
    if (owner) *owner = -1;   // mover +0x100 = 0 (0x435dd9)
    for (const auto& o : objects) {
        if (o.person || o.removed || !o.model || o.model->hitMeshes.empty()) continue;
        if (skip >= 0 && &o == &objects[size_t(skip)]) continue;
        const Vec3 at = nearest(o.pos, feet);
        const float R = o.model->flatRadius;   // +0x64
        if (std::abs(at.x - feet.x) > R || std::abs(at.z - feet.z) > R) continue;
        forPosedMeshes(*this, o, *o.model, feet, hitSkip_, [&](int mi, const Mat4& w) {
            const auto L = toFileAxes(w, feet);
            for (const HitFace& f : o.model->hitMeshes[size_t(mi)]) {
                if (L[0] < f.box[0] || L[0] > f.box[1] || L[1] < f.box[2] || L[1] > f.box[3]) continue;
                if (f.material & 0x01000000) continue;
                if (std::abs(f.n[2]) < 0x40 / 16384.f) continue;
                if (!inTriangle(f, L[0], L[1], 0, 1)) continue;
                const float zf = -(f.n[0] * L[0] + f.n[1] * L[1] + f.d) / f.n[2];
                // Back to the world: file (x, y, z) is model (x, z, -y).
                const float wy = w.at(1, 0) * L[0] + w.at(1, 1) * zf + w.at(1, 2) * -L[1] + w.at(1, 3);
                if (wy >= best && wy <= feet.y + step) {
                    best = wy;
                    if (owner) *owner = int(&o - objects.data());   // +0x100 = n (an equal later face too)
                }
            }
            return true;
        });
    }
    return best;
}

// DF.EXE 0x43634d..0x43643d / DF2.EXE 0x440876..0x440944 (READ): with a
// platform p, x += p+0x3C, y += p+0x40; then if p's turn (DF1 +0x90, DF2
// +0x9C) is not 0: (dx, dy) = position - p's position, (s, c) = sincos(turn)
// (0x435c40 / 0x41ab10: sine, then cosine), x = p.x + c dx - s dy, y = p.y +
// s dx + c dy (products >> 22), heading += turn. No z: the platform's height
// reaches the rider through the ground pass. In OpenDF axes (z = -y, the
// heading clockwise) the same turn is a clockwise rotation by tickTurn.
void World::carry(int platform, Vec3& pos, float& heading) const {
    if (platform < 0 || size_t(platform) >= objects.size()) return;
    const WorldObject& p = objects[size_t(platform)];
    pos.x += p.tickVel.x;
    pos.z += p.tickVel.z;
    if (p.tickTurn != 0) {
        const Vec3 c = nearest(p.pos, pos);
        const float dx = pos.x - c.x, dz = pos.z - c.z;
        const float sn = std::sin(p.tickTurn), cs = std::cos(p.tickTurn);
        pos.x = c.x + cs * dx - sn * dz;
        pos.z = c.z + sn * dx + cs * dz;
        heading += p.tickTurn;
    }
    const float ext = extent();
    pos.x -= std::floor(pos.x / ext) * ext;
    pos.z -= std::floor(pos.z / ext) * ext;
}

// DF2.EXE 0x440f80(points, halves, count, object, &push, &flags), READ:
//   flags = 0, push = 0; model = the husk when destroyed, else the model;
//   some point within +-(model +0x60 + half) of the object on all three
//   axes, else 0; no LOD 0 volumes (+0xB8) -> 0; the pose routine
//   (model +0x4C) gives the mesh slots. Per mesh with volumes and slot +0x3C
//   bit 0 clear, per point: into the mesh (0x42baa0 inverse, 0x42b550); the
//   cube must overlap the mesh box (+0x40..+0x54); then sum := 0 and per
//   volume, per plane s = n.L + d - half, outside at the first s >= 0;
//   inside: type 4 -> flags |= 1, type 6 -> flags |= 4 (GOG Df2.exe
//   0x447450 also type 7 -> flags |= 8, where the CD build pushes), others: hit and
//   sum += s_max x n of the least-negative plane. After the points, push +=
//   the slot's rotation (0x42b680) of sum.
// READ quirk kept: `sum` is cleared only when a point passes a mesh box, so
// a later mesh that no point reaches adds the previous mesh's sum again
// (in its own rotation), and only the last point that reaches a mesh counts.
// Its first value is uninitialised stack in DF2.EXE: taken as 0 here.
bool World::df2Overlap(const WorldObject& n, const Vec3* pts, const float* halves, int count, Vec3& push, uint32_t& flags) const {
    flags = 0;
    push = {0, 0, 0};
    if (!n.model || count <= 0) return false;
    const GpuModel& m = *n.model;
    const Vec3 at = nearest(n.pos, pts[0]);
    const float R = m.headerRadius;   // model +0x60
    bool near = false;
    for (int k = 0; k < count; ++k) {
        const Vec3 q = nearest(pts[k], at);
        if (std::abs(q.x - at.x) <= R + halves[k] && std::abs(q.z - at.z) <= R + halves[k] && std::abs(q.y - at.y) <= R + halves[k]) near = true;
    }
    if (!near || !m.hasVolumes) return false;
    bool hit = false;
    float sum[3] = {0, 0, 0};
    forPosedMeshes(*this, n, *n.model, pts[0], hitSkip_, [&](int mi, const Mat4& w) {
        if (size_t(mi) >= m.meshVolumes.size() || m.meshVolumes[size_t(mi)].volumes.empty()) return true;
        const MeshVolumes& mv = m.meshVolumes[size_t(mi)];
        for (int k = 0; k < count; ++k) {
            const auto L = toFileAxes(w, nearest(pts[k], at));
            const float h = halves[k];
            if (L[0] - h > mv.box[1] || L[0] + h < mv.box[0] || L[1] - h > mv.box[3] || L[1] + h < mv.box[2] ||
                L[2] - h > mv.box[5] || L[2] + h < mv.box[4])
                continue;
            sum[0] = sum[1] = sum[2] = 0;
            for (const PlaneVolume& v : mv.volumes) {
                float best = -16384.f;   // 0xC0000000
                const VolumePlane* bp = nullptr;
                bool inside = true;
                for (const VolumePlane& pl : v.planes) {
                    const float sd = pl.n[0] * L[0] + pl.n[1] * L[1] + pl.n[2] * L[2] + pl.d - h;
                    if (sd >= 0) { inside = false; break; }
                    if (sd > best) { best = sd; bp = &pl; }
                }
                if (!inside) continue;
                if (v.type == 4) flags |= 1;
                else if (v.type == 6) flags |= 4;
                else if (v.type == 7) flags |= 8;   // GOG Df2.exe 0x447450: an emplaced gun's seat, not solid
                else {
                    hit = true;
                    if (bp)
                        for (int c = 0; c < 3; ++c) sum[c] += best * bp->n[c];
                }
            }
        }
        // File (x, y, z up) -> model (x, z, -y) -> world (rotation only).
        const float mx = sum[0], my = sum[2], mz = -sum[1];
        push.x += w.at(0, 0) * mx + w.at(0, 1) * my + w.at(0, 2) * mz;
        push.y += w.at(1, 0) * mx + w.at(1, 1) * my + w.at(1, 2) * mz;
        push.z += w.at(2, 0) * mx + w.at(2, 1) * my + w.at(2, 2) * mz;
        return true;
    });
    return hit;
}

std::vector<int> World::df2Neighbours(Vec3 at, float margin, int self) const {
    std::vector<int> out;
    if (margin <= 0) return out;
    for (size_t i = 0; i < objects.size(); ++i) {
        const WorldObject& o = objects[i];
        // Groups 0 (0xC4A5C0) and 1 (0xCD05C0); +0x64 & 2 (removed) skipped.
        if (int(i) == self || o.person || o.removed || !o.model || o.missionGroup == 2 || o.missionGroup == 3) continue;
        const Vec3 p = nearest(o.pos, at);
        const float r = o.model->flatRadius + margin;   // model +0x64
        if (std::abs(p.x - at.x) > r || std::abs(p.z - at.z) > r) continue;
        if (std::abs(p.y - at.y) > o.model->headerRadius + margin) continue;   // model +0x60
        if (out.size() >= 2000) break;   // 0x7d0 entries in all (0x43f370)
        out.push_back(int(i));
    }
    return out;
}

// DF2.EXE 0x440984..0x440af1 (READ). P0 = the origin, P1 = (x, y, z +
// (frame +0x44 << 8) + 0x8000): 0.5 m above the feet; both half 0x8000.
// Per neighbour, 0x440f80; on a hit: a touch item (def +0x24 & 0x10) runs
// its contact (0x43fc80, done by the game) and does not push; otherwise
// push.z < 0 -> standing on it (+0xF8 = it); x and y move by the push less
// 0x1999 (0.1 m) toward zero, not at all within 0.1 m; z: a push under
// 0x51E (0.02 m) gets 0x51E added, z -= push.z (so 0.02 m of overlap stays,
// and a side push lowers z by 0.02 m); with a ladder volume touched (flags
// 1) the whole push instead (GOG 0x446ebc). The points are not moved between
// neighbours. Flags 1 (type 4) with the player or AI +0x34 & 0x400: ladder
// 0x400000 and not airborne; flags 4 (type 6): +0x64 |= 0x1000000.
World::Df2Contact World::df2PushOut(Vec3& origin, float feetOffset, const std::vector<int>& neighbours, bool mayClimb) const {
    Df2Contact c;
    const Vec3 pts[2] = {origin, origin + Vec3{0, feetOffset + 0.5f, 0}};
    const float halves[2] = {0.5f, 0.5f};
    constexpr float kSide = 0x1999 / 65536.f, kOverlap = 0x51E / 65536.f;
    for (int i : neighbours) {
        if (i < 0 || size_t(i) >= objects.size()) continue;
        const WorldObject& n = objects[size_t(i)];
        Vec3 push;
        uint32_t flags = 0;
        if (df2Overlap(n, pts, halves, 2, push, flags)) {
            if (n.item && (n.item->attrib & 0x10)) {
                c.touched.push_back(i);
            } else {
                if (push.y < 0) { c.onObject = true; c.platform = i; }
                if (flags & 1) {
                    // GOG Df2.exe 0x446ebc: an object whose ladder volume
                    // (type 4) the points touch pushes in full
                    // (0x446f33..0x446f4b), whoever climbs.
                    origin.x -= push.x;
                    origin.z -= push.z;
                    origin.y -= push.y;
                } else {
                    if (std::abs(push.x) > kSide) origin.x -= push.x > 0 ? push.x - kSide : push.x + kSide;
                    if (std::abs(push.z) > kSide) origin.z -= push.z > 0 ? push.z - kSide : push.z + kSide;
                    float pz = push.y;
                    if (pz < kOverlap) pz += kOverlap;
                    origin.y -= pz;
                }
            }
        }
        if ((flags & 1) && mayClimb) c.ladder = true;
        if (flags & 4) c.armory = true;
        if (flags & 8) c.mounts.push_back(i);   // GOG Df2.exe 0x4467d0: 0x4145a0(person, it)
    }
    return c;
}

static Vec3 applyPoint(const Mat4& w, Vec3 p) {
    return {w.at(0, 0) * p.x + w.at(0, 1) * p.y + w.at(0, 2) * p.z + w.at(0, 3),
            w.at(1, 0) * p.x + w.at(1, 1) * p.y + w.at(1, 2) * p.z + w.at(1, 3),
            w.at(2, 0) * p.x + w.at(2, 1) * p.y + w.at(2, 2) * p.z + w.at(2, 3)};
}

// GOG Df2.exe 0x4145a0 (READ), see World.h. The "anyone mans it" test
// scans every person's +0x8c (0xc7216c, stride 0x114), the player's too.
bool World::df2Mount(int person, int gun, int moveDir, bool dead) {
    if (gun < 0 || size_t(gun) >= objects.size()) return false;
    WorldObject& g = objects[size_t(gun)];
    const bool player = person == -3;
    if (!player && (person < 0 || size_t(person) >= objects.size())) return false;
    int& link = player ? playerMount : objects[size_t(person)].mount;
    if (!g.item || (link != -1 && link != gun) || !(g.item->attrib & 0x8000)) return false;
    if ((moveDir >= 3 && moveDir <= 5) || dead || !g.alive()) {
        link = -1;
        g.gunner = -1;
        return false;
    }
    if (link != -1) return false;   // already on this gun
    if (playerMount == gun) return false;
    for (const WorldObject& o : objects)
        if (o.person && o.mount == gun) return false;
    link = gun;
    g.gunner = person;    return true;
}

// GOG Df2.exe 0x4467d0's end: a destroyed gun (+0x64 & 4) lets go.
void World::clearMount(int person) {
    int& link = person == -3 ? playerMount : objects[size_t(person)].mount;
    if (link >= 0 && size_t(link) < objects.size()) objects[size_t(link)].gunner = -1;
    link = -1;
}

// GOG Df2.exe 0x418300 (READ): matrix 0x4308d0 from the gun's position and
// (heading = the person's +0x30, pitch 0, roll 0), applied (0x4301c0) to the
// gun model's hardpoint 0 (LOD 0 +0xA4, ints 0..2).
Vec3 World::mountOrigin(int gun, float personHeading, Vec3 ref) const {
    const WorldObject& g = objects[size_t(gun)];
    const Vec3 at = nearest(g.pos, ref);
    if (!g.model || g.model->points.empty()) return at;
    const auto& p = g.model->points[0];
    return applyPoint(translate(at) * rotateY(kPi * 0.5f - personHeading), {p[0], p[1], p[2]});
}

// GOG Df2.exe 0x450c60 (disassembled; not in the Ghidra export): slot 0 =
// the object's matrix (0x4308d0); if +0xF8 (the gunner) is set and his
// +0x8c is this gun, the matrix 0x430ad0 of (-hardpoint 1, yaw = gunner
// +0x30 - gun +0x30, pitch = gunner +0x34, roll 0), a turn about hardpoint
// 1, is put after slot 0 for slots 1 and 2 (0x42fcf0); else (and +0xF8
// cleared when the link is stale) every slot is slot 0.
std::vector<Mat4> World::ewpnPose(const WorldObject& g, size_t meshes) const {
    std::vector<Mat4> pose(meshes, Mat4{});
    if (!g.model || g.model->points.size() < 2 || g.gunner == -1) return pose;
    const int self = int(&g - objects.data());
    float yaw = 0, pitch = 0;
    if (g.gunner == -3) {
        if (playerMount != self) return pose;
        yaw = playerYaw;
        pitch = playerPitch;
    } else {
        if (g.gunner < 0 || size_t(g.gunner) >= objects.size() || objects[size_t(g.gunner)].mount != self) return pose;
        yaw = objects[size_t(g.gunner)].heading;
        pitch = objects[size_t(g.gunner)].aimPitch;
    }
    // DF yaw is anticlockwise, OpenDF headings clockwise; pitch up positive
    // about the model's sideways axis (as the vehicles' body pitch).
    Mat4 gp;
    const float c = std::cos(pitch), s = std::sin(pitch);
    gp.at(0, 0) = c; gp.at(0, 1) = -s;
    gp.at(1, 0) = s; gp.at(1, 1) = c;
    const auto& p = g.model->points[1];
    const Mat4 turn = translate({p[0], p[1], p[2]}) * rotateY(-(yaw - g.heading)) * gp * translate({-p[0], -p[1], -p[2]});
    for (size_t i = 1; i < meshes && i < 3; ++i) pose[i] = turn;
    return pose;
}

// GOG Df2.exe 0x416e90 (READ): the gun's pose routine (+0x4C), its slot 1
// (+0x40) applied to hardpoint 2 (+0xA4 +0x18..+0x20); the view (0x416a30)
// takes the person's own heading and pitch.
Vec3 World::mountEye(int gun, Vec3 ref) const {
    const WorldObject& g = objects[size_t(gun)];
    const Vec3 at = nearest(g.pos, ref);
    if (!g.model || g.model->points.size() < 3) return at;
    const auto& p = g.model->points[2];
    Mat4 slot1 = g.matrix(at);
    if (g.model->lodCodes[0] == "ewpn") {
        const auto pose = ewpnPose(g, std::max<size_t>(g.model->hitMeshes.size(), 2));
        slot1 = slot1 * pose[1];
    }
    return applyPoint(slot1, {p[0], p[1], p[2]});
}

// DF2.EXE 0x43fb80(p, mover, step), READ: best = terrain(p); +0xF8 = 0; per
// neighbour, 0x440f80 with one point of half 0x100; on a hit c = p.z -
// push.z is kept if best <= c <= p.z + step.
float World::df2Surface(Vec3 p, float step, const std::vector<int>& neighbours) const {
    float best = terrain->heightAt(p.x, p.z);
    const float half = 0x100 / 65536.f;
    for (int i : neighbours) {
        if (i < 0 || size_t(i) >= objects.size()) continue;
        Vec3 push;
        uint32_t flags = 0;
        if (!df2Overlap(objects[size_t(i)], &p, &half, 1, push, flags)) continue;
        const float c = p.y - push.y;
        if (c <= p.y + step && c >= best) best = c;
    }
    return best;
}

Vec3 World::personStep(WorldObject& o, float velocity) const {
    const Vec3 f{std::sin(o.heading), 0, -std::cos(o.heading)};
    o.swimDz = 0;
    if (!o.swimming) return f * (velocity / 60.f);
    // In the water (DF.EXE 0x43621a, DF2.EXE 0x4404dc): the move's per-frame
    // distance v = velocity / 30 along the heading and pitch (+0x34), not
    // halved horizontally, halved vertically; no rising once the origin is
    // within 0.4 m (26214.4 / 65536) of the surface.
    const float v = velocity / 30.f, p = o.aimPitch;
    float dz = v * std::sin(p) * 0.5f;
    const float water = terrain->waterLevel();
    if (dz > 0 && o.base + o.hip + 26214.4f / 65536.f > water) dz = 0;
    o.swimDz = dz;
    return f * (v * std::cos(p));
}

bool World::roundHit(const WorldObject& o, Vec3 a, Vec3 b, float& best, bool haveObject, int& mesh, uint32_t& material, Vec3* normal) const {
    bool found = false;
    testFaces(*this, o, *o.model, a, b, hitSkip_, 0x01000000, 2.f, !df2Rules, [&](const HitFace& f, int mi, float t) {
        // Ties: a later face wins among objects, not over the terrain.
        if (haveObject || found ? t > best : t >= best) return true;
        best = t;
        mesh = mi;
        material = f.material;
        if (normal) *normal = {f.n[0], f.n[2], -f.n[1]};   // file axes (x, y, z up) -> OpenDF model axes
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
int World::sight(Vec3 a, Vec3 b, int looker, int target, bool fog) const {
    b = nearest(b, a);
    const Vec3 d = b - a;
    const float len = std::sqrt(dot(d, d));
    if (len >= 800.f) return 0;
    // Delta Force 2 (0x406180): with fog on, not past the fog distance.
    if (df2Rules && fog && fogLevel > 0 && len > float(1 << fogLevel)) return 0;
    if (len < 1e-4f) return 1;
    if (!df2Rules) {
        // GOG Df.exe's terrain march 0x47e4b0 (CD 0x433074), in 1/16 m
        // steps i from the start, clear once i passes the length: a coarse
        // test every 1 m against the cell's max map (0x47d9e0), a point at
        // or below it (inclusive) backing up 1 m and testing the next 19
        // points strictly against the ground with its detail elevation
        // (0x47e000; so a hit at the start tests points behind the eye),
        // then the coarse test again from the last of them. Ground that
        // rises between coarse points above the max map's cells is missed.
        const int n = int(len * 16.f);
        const Vec3 step = d * (1.f / 16.f / len);
        int i = 0;
        while (i <= n) {
            const Vec3 p = a + step * float(i);
            if (p.y > terrain->sightMax(p.x, p.z)) { i += 16; continue; }
            i -= 16;
            for (int k = 0; k < 19; ++k) {
                if (++i > n) break;
                const Vec3 q = a + step * float(i);
                if (q.y < terrain->sightHeight(q.x, q.z)) return 0;
            }
        }
    } else {
        // GOG Df2.exe 0x4063d0 -> 0x46e740, in its 16.16 axes (x east, y
        // north = -z, z up): the length isqrt(sum of d^2 >> 22) << 11, the
        // unit direction d * (2^32 / length) >> 16, and n = length >> 12
        // steps i of 1/16 m from the start. Every 1 m a coarse test against
        // the word grid's vertex at (x >> 16, y >> 16); a point at or below
        // it (inclusive) backs up 1 m and tests the next 19 points (dir >> 4
        // each) strictly against the bilinear height 0x46e870, then the
        // coarse test again. Clear once i passes n; blocked returns -i <<
        // 12, which is positive for a hit behind the start (i < 0).
        auto fx = [](float m) { return int32_t(std::lround(double(m) * 65536.0)); };
        const int32_t dx = fx(d.x), dy = fx(-d.z), dz = fx(d.y);
        auto sq = [](int32_t v) { return uint32_t(uint64_t(int64_t(v) * v) >> 22); };
        const uint32_t sum = sq(dx) + sq(dy) + sq(dz);
        uint32_t root = uint32_t(std::sqrt(double(sum)));
        while (uint64_t(root) * root > sum) --root;
        while (uint64_t(root + 1) * (root + 1) <= sum) ++root;
        const int32_t length = int32_t(root << 11);
        if (length == 0) return 1;
        const int32_t inv = int32_t(0x100000000LL / length);
        const int32_t ux = int32_t((int64_t(dx) * inv) >> 16), uy = int32_t((int64_t(dy) * inv) >> 16), uz = int32_t((int64_t(dz) * inv) >> 16);
        const int n = length >> 12;
        int32_t x = fx(a.x), y = fx(-a.z), z = fx(a.y);
        int i = 0;
        for (;;) {
            if (i > n) break;
            if (z > terrain->df2SightVertex(x, y)) {
                x += ux, y += uy, z += uz;
                i += 16;
                continue;
            }
            i -= 16;
            x -= ux, y -= uy, z -= uz;
            bool clear = false;
            for (int k = 0; k < 19; ++k) {
                x += ux >> 4, y += uy >> 4, z += uz >> 4;
                if (++i > n) { clear = true; break; }
                if (z < terrain->df2SightGround(x, y)) return -i * 0x1000;
            }
            if (clear) break;
        }
    }
    const Vec3 mid = a + d * 0.5f;
    int extra = 0;
    for (size_t i = 0; i < objects.size(); ++i) {
        const WorldObject& o = objects[i];
        // DF1 skips only empty records, the looker and the target (GOG
        // 0x42db80; CD 0x45b737, 0x45b74a, 0x45b792): a carried item (hidden,
        // +0x6c & 2) and one destroyed without a husk (its record kept) still
        // count, with the intact model +0x10 (radius +0x60, its pose and
        // faces) whether a husk or nothing is drawn.
        if (int(i) == looker || int(i) == target || o.person || !o.model) continue;
        if (o.removed && !(!df2Rules && (o.carried || o.blown))) continue;
        const GpuModel& m = !df2Rules && o.intact ? *o.intact : *o.model;
        const Vec3 at = nearest(o.pos, mid);
        const float R = m.headerRadius;
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
        testFaces(*this, o, m, a, b, hitSkip_, 0x800, 2.f, false, [&](const HitFace& f, int, float) {
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

    // Delta Force's rounds (GOG Df.exe 0x47e390, from the round mover
    // 0x464eb0): 1 m steps from the start while above the raw corner byte of
    // the cell (x0, y0); at or below it, 1 m back and up to 19 steps of
    // 1/16 m against the bilinear ground (below it: a hit at that many
    // sixteenths, which can lie behind the start), then coarse again; no hit
    // once the count passes the length in sixteenths.
    const int steps = rounds && !df2Rules ? 0 : std::max(2, int(len / 0.5f));
    if (rounds && !df2Rules) {
        const Vec3 dir = d * (1.f / len);
        const int limit = int(len * 16.f);
        int n = 0;
        bool hitGround = false;
        for (;;) {
            if (n > limit) break;
            const Vec3 p = a + dir * (float(n) / 16.f);
            if (p.y > terrain->cornerHeight(p.x, p.z)) { n += 16; continue; }
            n -= 16;
            for (int fine = 0; fine < 19; ++fine) {
                if (++n > limit) break;
                const Vec3 q = a + dir * (float(n) / 16.f);
                if (q.y < terrain->heightAt(q.x, q.z)) { hitGround = true; break; }
            }
            if (hitGround || n > limit) break;
        }
        if (hitGround) {
            hit.t = float(n) / 16.f / len;
            hit.object = -1;
            found = true;
        }
    }
    // Terrain: march, then refine by bisection.
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
    // Delta Force's rounds (GOG Df.exe 0x463790, the player among the
    // people): the body origin inside the segment's box grown by 2 m, its
    // projection on the line not behind the start (-1 < dot(origin - a,
    // dir)), and that line point (not clamped to the end) within 1.5 m
    // (0x477670) of the origin; only then the posed spheres.
    const bool df1Round = rounds && !df2Rules;
    auto broadPhase = [&](Vec3 at, float& tc, auto&& closest) {
        if (!df1Round) return closest(at, tc) <= 1.5f;
        if (at.x < std::min(a.x, b.x) - 2.f || at.x > std::max(a.x, b.x) + 2.f || at.y < std::min(a.y, b.y) - 2.f ||
            at.y > std::max(a.y, b.y) + 2.f || at.z < std::min(a.z, b.z) - 2.f || at.z > std::max(a.z, b.z) + 2.f)
            return false;
        tc = dot(at - a, d) / (len * len);
        if (tc < 0) return false;
        return approxLen(a + d * tc - at) <= 1.5f;
    };
    if (objects && people && ignore != -3 && playerBody.present) {
        const Vec3 at = nearest(playerBody.origin, mid);
        auto closest = [&](Vec3 c, float& t) {
            t = std::clamp(dot(c - a, d) / (len * len), 0.f, 1.f);
            return approxLen(a + d * t - c);
        };
        float tc;
        if (broadPhase(at, tc, closest)) {
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
    // (OpenDF) The gun the shooter mans is skipped with the shooter: until
    // the gun's own weapon (GOG Df2.exe 0x44c840 / 0x44f160) is ported, a
    // gunner fires his personal weapon from the eye on the gun (hardpoint 2
    // of its posed mesh 1), and its rounds and sight lines would otherwise
    // hit the gun's own meshes.
    const int ignoreMount = ignore == -3 ? playerMount
                            : ignore >= 0 && size_t(ignore) < this->objects.size() ? this->objects[size_t(ignore)].mount
                                                                                    : -1;
    for (size_t i = 0; objects && i < this->objects.size(); ++i) {
        if (int(i) == ignore || (ignoreMount >= 0 && int(i) == ignoreMount)) continue;
        const auto& o = this->objects[i];
        if (!people && o.person) continue;
        // Living people (DF.EXE 0x44dca4): only if the path passes within 1.5 m
        // of the body origin, then each part's sphere; the nearest along the
        // path wins. Delta Force's rounds (GOG Df.exe 0x463790) do not skip
        // the dead (no flags & 4 test): a body's posed spheres stop them
        // (flesh effect and sound, no damage, an armed explosive goes off).
        if (o.person && (!o.dead || (rounds && !df2Rules && !o.removed)) && !o.spheres.empty()) {
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
            if (!broadPhase(at, tc, closest)) continue;
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
            Vec3 n{0, 1, 0};
            if (roundHit(o, a, b, best, found && hit.object >= 0 && !this->objects[size_t(hit.object)].person, mesh, material, &n)) {
                // The face normal into world axes (the object's rotation).
                const Mat4 m = o.matrix(o.pos);
                hit.normal = normalize(Vec3{m.at(0, 0) * n.x + m.at(0, 1) * n.y + m.at(0, 2) * n.z, m.at(1, 0) * n.x + m.at(1, 1) * n.y + m.at(1, 2) * n.z,
                                            m.at(2, 0) * n.x + m.at(2, 1) * n.y + m.at(2, 2) * n.z});
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
    // Delta Force 2: an object's +0xF8 holds who damaged it last (rounds
    // 0x453510, blasts 0x454b60), which the boat's death blasts pass on as
    // their owner (GOG Df2.exe 0x402206..0x4022a3).
    if (df2Rules && !o.person && killer != -1) o.killer = killer;
    if (o.person) {
        // People have ITEMS.DEF's hit points (2-4) and die at 0 (head hits'
        // multipliers are applied by the callers from the .CHR / .KSA spheres).
        o.hp -= amount;
        if (o.hp > 0) return {};
        o.dead = true;
        o.killer = killer;
        // The death animation (0x46c920 / 0x469764); without one, topple.
        if (!(deathMove >= 0 && startMove(o, deathMove)) && !startMove(o, 0xBE)) o.fall = 0.001f;
        refreshBoxes(o);
        return o.item->displayName + " down";
    }
    if (!df2Rules && o.item && o.item->aiFunction == "gnrc") {
        // GOG Df.exe 0x4533f0 (CD 0x47ee6c), the think and the damage
        // handler: at hp <= 0 it sets flags 4 and thinks again in 0x20 ticks;
        // the next run with flag 4 (that think, or at once a further round or
        // blast) blows it up (0x453270) and only then sets flag 8, the husk.
        // Game::gnrcExplode does that part.
        if (o.gnrcTimer > 0) { o.gnrcTimer = 0; return {}; }
        if (o.hp <= 0 || o.gnrcTimer == 0) return {};
        o.hp -= amount;
        if (o.hp > 0) return {};
        o.gnrcTimer = 0x20;
        return o.item->displayName + " destroyed";
    }
    if (!df2Rules && o.item && o.item->aiFunction == "brrl") {
        // GOG Df.exe 0x45bf80, the barrel's think and damage handler (the
        // damage 0x464c50 and the blasts call it without a flags & 4 test):
        // with flag 4 set it goes off at once (0x446fb0, 10 fragments), the
        // smoke generator (0x447610) and the record cleared (0x4471d0);
        // otherwise at hp <= 0 flags |= 4 and the think in 10 ticks. Until
        // then the barrel stays drawn and is still hit by rounds and blasts.
        if (o.detonate > 0) { o.detonate = 1; return {}; }   // goes off at the next tick
        if (o.hp <= 0) return {};
        o.hp -= amount;
        if (o.hp > 0) return {};
        o.hp = 0;
        o.detonate = 10;
        destroyed_.push_back(index);
        return o.item->displayName + " destroyed";
    }
    if (o.hp <= 0 || !o.item) return {};   // indestructible
    o.hp -= amount;
    if (o.hp > 0) return {};
    if (df2Rules && o.item->aiFunction == "bld2") {
        // DF2.EXE 0x4028d0: the collapse runs over 64 thinks; the husk at step 8.
        o.hp = 0;
        o.collapse = 0;
        destroyed_.push_back(index);
        return o.item->displayName + " destroyed";
    }
    if (!o.husk && (o.item->aiFunction == "mine" || (df2Rules && (o.item->aiFunction == "vtrg" || o.item->aiFunction == "prnh")))) {
        // DF2.EXE 0x4295f0 / 0x42a980: flagged destroyed (4, or 0x10C) but
        // not removed; with no husk the model stays drawn (the draw list
        // 0x4490d0 skips only +0x64 & 2, the draw 0x4459f0 takes the husk only
        // with flag 8 and a husk). Delta Force's mine (GOG Df.exe 0x453220)
        // also only sets flag 4 (and blows up, see the destroyed loop). A
        // dead fish (prnh 0x42db20: 0x10C) floats on, belly up.
        o.hp = 0;
        o.wrecked = true;
        destroyed_.push_back(index);
        return o.item->displayName + " destroyed";
    }
    applyHusk(index);
    return o.item->displayName + " destroyed";
}

void World::applyHusk(int index) {
    WorldObject& o = objects[size_t(index)];
    if (o.husk) {
        o.model = o.husk;
        o.husk = nullptr;
        o.wrecked = true;
    } else {
        o.removed = true;
        o.blown = true;
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
std::vector<std::string> World::blast(Vec3 at, float radius, bool heavy, int owner, int ownerSide) {
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
        if (!o.alive() && o.gnrcTimer <= 0 && o.detonate <= 0) continue;   // a waiting gnrc object (0x4533f0) or barrel (0x45bf80) still takes it
        const Vec3 p = nearest(o.pos, at);
        if (o.person) {
            const float r = radius + 1.f;
            if (std::abs(p.x - at.x) > r || std::abs(p.y - at.y) > r || std::abs(p.z - at.z) > r) continue;
            float d = len3(p.x - at.x, p.y - at.y, p.z - at.z);
            if (d > r) continue;
            // Line of sight: from the body to the blast point raised 0.5 m,
            // then from 0.5 m higher; both blocked: no damage for type 1,
            // double distance otherwise. Delta Force (GOG Df.exe 0x465830 ->
            // 0x42e2b0, CD 0x45be04) uses the sight line, so terrain,
            // buildings, decorations and items shield. Delta Force 2 (GOG
            // Df2.exe 0x454bde / 0x454c01 -> 0x406610): the line of fire, the
            // terrain and the decorations and items in the way (walls,
            // buildings, vehicles), no fog cutoff.
            const bool blocked = df2Rules ? sight(p, target, int(i), -1, false) <= 0 && sight(p + Vec3{0, 0.5f, 0}, target, int(i), -1, false) <= 0
                                          : sight(p, target, int(i), -1) <= 0 && sight(p + Vec3{0, 0.5f, 0}, target, int(i), -1) <= 0;
            if (blocked) {
                if (!heavy) continue;
                d *= 2;
            }
            // DF2.EXE 0x44d4a0: base 8 (type 5, the 12 m charge: 32), full under
            // 4 m, half under 8 m, a quarter beyond.
            const int base = radius > 11.f ? 32 : 8;
            const int amount = df2Rules ? (d < 4 ? base : d < 8 ? base / 2 : base / 4) : d < 4 ? 4 : d < 8 ? 2 : 1;
            // Death move type 2, or 3 one time in four at 4-8 m (0x44fda8;
            // GOG Df.exe 0x465977: 0x477860 below 0x4000).
            const int type = (d >= 4 && d < 8 && (rand16 ? rand16() < 0x4000 : (std::rand() & 3) == 0)) ? 3 : 2;
            // DF2 0x454cd7: the owner goes into the victim's +0xF8.
            std::string m = damage(int(i), amount, 99, deathMove(int(i), type, 0, at), df2Rules ? owner : -1);
            if (!m.empty()) out.push_back(m);
            continue;
        }
        // A destroyed Delta Force gnrc object waiting to go off (flag 4, no
        // husk yet) is still in the blast and fires at once (0x4533f0).
        if ((o.hp <= 0 && o.gnrcTimer <= 0 && o.detonate <= 0) || !o.model) continue;
        // DF2 0x454f52..0x454f56: an object of the owner's side is spared.
        // (The CD's 0x44d4a0 has no such test.)
        if (df2Rules && df2Gog && owner != -1 && o.side == ownerSide) continue;
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
        if (sight(p, target, int(i), -1, !df2Rules) <= 0 && sight(p + Vec3{0, 0.5f, 0}, target, int(i), -1, !df2Rules) <= 0) {
            if (!heavy) continue;
            amount >>= 1;
        }
        // Delta Force (GOG Df.exe 0x465830, 0x4659de): the amount is clamped
        // to 0 and the object still gets the event, so a destroyed gnrc
        // object waiting to go off (0x4533f0) fires at once.
        if (df2Rules && amount <= 0) continue;
        amount = std::max(amount, 0);
        std::string m = damage(int(i), amount, 99, -1, df2Rules ? owner : -1);
        if (!m.empty()) out.push_back(m);
    }
    return out;
}

void World::update(float dt) {
    for (personClock_ += dt; personClock_ >= 1.f / 60.f; personClock_ -= 1.f / 60.f) {
        ++personTicks_;
        // DF2 0x440180: the neighbour lists are rebuilt (0x440ef0) when the
        // counter has reached 0x40, else it counts up; built at mission start
        // too (0x45d8f7, the counter's first value here).
        if (df2Rules) {
            if (listTicks_ >= 0x40) {
                listTicks_ = 0;
                for (size_t i = 0; i < objects.size(); ++i)
                    if (objects[i].person) objects[i].neighbours = df2Neighbours(objects[i].pos, 10.f, int(i));
            } else {
                ++listTicks_;
            }
        }
        // Every person with an animation, alive or dead (DF.EXE 0x436cd5,
        // DF2.EXE 0x440225: +0x18 != 0).
        for (auto& o : objects)
            if (o.person && !o.removed) personTick(o);
        // Testing: OPENDF_DEBUGRIDE=1 lists, every 120 ticks, the people
        // standing on an object (+0x100 / +0xF8) and where they are on it;
        // =N (N > 1) follows world object N every 10 ticks.
        const char* ride = std::getenv("OPENDF_DEBUGRIDE");
        const int follow = ride ? std::atoi(ride) : 0;
        if (ride && personTicks_ % (follow > 1 ? 10 : 120) == 0)
            for (size_t i = 0; i < objects.size(); ++i) {
                const WorldObject& o = objects[i];
                if (!o.person || o.removed || (follow > 1 && int(i) != follow)) continue;
                const bool on = o.platform >= 0 && size_t(o.platform) < objects.size();
                const WorldObject* p = on ? &objects[size_t(o.platform)] : nullptr;
                const Vec3 rel = p ? nearest(o.pos, p->pos) - p->pos : Vec3{0, 0, 0};
                if (p || o.airborne || follow > 1)
                    std::fprintf(stderr, "ride t %u person %zu move %d at (%.2f %.2f %.2f)%s%s on %d %s rel (%.2f %.2f %.2f) vel %.3f turn %.4f hdg %.1f%s\n", personTicks_, i, o.moveNumber,
                                 o.pos.x, o.base, o.pos.z, o.airborne ? " AIR" : "", o.alive() ? "" : " dead", o.platform,
                                 p && p->item ? p->item->name.c_str() : "-", rel.x, o.base - (p ? p->pos.y : 0.f), rel.z,
                                 p ? std::sqrt(dot(p->tickVel, p->tickVel)) * 60.f : 0.f, p ? p->tickTurn : 0.f, p ? p->heading * 57.2958f : 0.f, o.ladder ? " LADDER" : "");
            }
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
            }
            poseUpdate(o);
        }
    }
}

void World::personTick(WorldObject& o) {
    const bool wasSwimming = o.swimming;
    if (df2Rules) personTickDf2(o);
    else personTickDf1(o);
    if (o.swimming != wasSwimming && std::getenv("OPENDF_DEBUGSWIM"))
        std::fprintf(stderr, "person %d %s the water at mission (%.1f, %.1f), origin %.2f water %.2f move %d alive %d\n", int(&o - objects.data()),
                     o.swimming ? "enters" : "leaves", o.pos.x, -o.pos.z, o.base + o.hip, terrain->waterLevel(), o.moveNumber, int(o.alive()));
}

void World::personTickDf1(WorldObject& o) {
    // Riding the platform +0x100 every tick (0x43634d), before the move.
    carry(o.platform, o.pos, o.heading);
    // z += vz + the swim move's dz every tick (DF.EXE 0x436440..0x436459).
    o.base += o.vz + o.swimDz;
    o.swimDz = 0;
    const float water = terrain->waterLevel();
    // DF.EXE: AI ground pass on ticks with (tick & 3) == 3 (0x4364bf),
    // object faces within 0.5 m above the feet; gravity 0xB20 a pass.
    if ((personTicks_ & 3) != 3) { o.pos.y = o.base + o.hip; return; }
    const float g = faceGround({o.pos.x, o.base, o.pos.z}, 0.5f, &o.platform);
    const float G = 0xB20 / 65536.f;
    const float d = o.base - g;
    const float z = o.base + o.hip;               // the origin (obj +0x2C)
    const bool under = water != 0 && water > z;   // `cmp [0x68bfe8], z; jle`
    if (o.airborne) {
        // 0x436563: landing; or into the water: +0x6C &= ~0x22000, +0x6E |= 2,
        // effect 4 and sound 0x8A at (x, y, water) (0x436586..0x4365be).
        if (d < 0) { o.vz = 0; o.airborne = false; o.base -= d; }
        else if (under) { o.airborne = false; o.swimming = true; splashes_.push_back({o.pos.x, water, o.pos.z}); }
        else o.vz -= G;
    } else if (o.swimming) {
        // 0x4368e6: out when the origin is above the level; the bottom holds
        // the body up; else vz x 0x68DB for AI (0xCCCC is the player's).
        if (z > water) o.swimming = false;
        else if (d < 0) { o.vz = 0; o.base -= d; }
        else o.vz = o.vz * float(0x68DB) / 65536.f;
    } else if (d < 0) {
        o.vz = 0;
        o.base -= d;
    } else if (under) {
        // 0x43697d: walking in: +0x6E |= 2 (no splash), and gravity once.
        o.swimming = true;
        o.vz -= G;
    } else if (d > 0x800 / 65536.f) {
        o.vz -= G;
        if (d > 0.5f || o.vz > 0) o.airborne = true;
    }
    o.pos.y = o.base + o.hip;
}

// DF2.EXE person move 0x440400 (READ), every tick for everyone, alive or
// dead; the horizontal part comes from AiSystem (personStep) or the death
// root motion, and on a parachute from the drift below.
void World::personTickDf2(WorldObject& o) {
    const float water = terrain->waterLevel();
    // On a parachute (0x440651): with the item attribute PFoil (0x400)
    // 0x4000 a tick (0.25 m) along the heading + the Parachute move's
    // direction (0, table 0x4ab758), from 0x41ab10's (cos, sin); without it
    // no horizontal motion. Walking is skipped (0x4406a8 -> 0x440876).
    if (o.chute && o.item && (o.item->attrib & 0x400)) {
        const Vec3 f{std::sin(o.heading), 0, -std::cos(o.heading)};
        o.pos = o.pos + f * (0x4000 / 65536.f);
        const float ext = extent();
        o.pos.x -= std::floor(o.pos.x / ext) * ext;
        o.pos.z -= std::floor(o.pos.z / ext) * ext;
    }
    // Riding the platform +0xF8 (0x440876), then z += vz + dz
    // (0x440962..0x44096f); +0x64 &= ~0x01400000.
    carry(o.platform, o.pos, o.heading);
    o.base += o.vz + o.swimDz;
    o.swimDz = 0;
    o.ladder = o.armory = false;
    // Push-out against the neighbour list (0x440984): points at the origin
    // and 0.5 m above the feet (feet = origin + frame +0x44 << 8 = -hip).
    bool onObject = false;
    if (!o.neighbours.empty()) {
        Vec3 origin{o.pos.x, o.base + o.hip, o.pos.z};
        const Df2Contact c = df2PushOut(origin, -o.hip, o.neighbours, o.climbs);
        o.pos.x = origin.x;
        o.pos.z = origin.z;
        o.base = origin.y - o.hip;
        if (c.onObject) { onObject = true; o.platform = c.platform; }
        // GOG Df2.exe 0x446e0a: airborne cleared, 0x400000 set; not prone
        // (+0x74 & 0x20, 0x446e15) vel_z = 0 (0x446e1c). +0x74 is the human
        // control word: written only for the local player (0x41efd0,
        // 0x447120) and by the multiplayer receive paths (0x409fae,
        // 0x45d3e7), never for an AI, so for the AI the prone test is
        // always false and the ladder always zeroes vel_z.
        if (c.ladder) {
            o.airborne = false;
            o.ladder = true;
            o.vz = 0;
        }
        if (c.armory) o.armory = true;
        // GOG Df2.exe 0x4467d0: flags 8 -> 0x4145a0(person, gun); a new
        // link places the origin at the gun (0x418300). The control
        // direction (+0x74 & 7) is the walk / run move's.
        const int self = int(&o - objects.data());
        int dir = 0;
        for (const auto& [lo, n] : {std::pair{20, 8}, {28, 8}, {40, 8}, {48, 8}, {60, 8}, {68, 8}, {80, 8}, {88, 8}})
            if (o.moveNumber >= lo && o.moveNumber < lo + n) dir = o.moveNumber - lo;
        for (int g : c.mounts)
            if (df2Mount(self, g, dir, o.dead)) {
                const Vec3 m = mountOrigin(g, o.heading, o.pos);
                o.pos.x = m.x;
                o.pos.z = m.z;
                o.base = m.y - o.hip;
            }
    }
    // 0x4467d0's end: the gun destroyed (+0x64 & 4) clears both links.
    if (o.mount >= 0 && !objects[size_t(o.mount)].alive()) clearMount(int(&o - objects.data()));
    // Ground (0x440af6): d = 0 on an object, else feet - terrain (d <= 0
    // clears +0xF8).
    float d = 0;
    if (!onObject) {
        d = o.base - terrain->heightAt(o.pos.x, o.pos.z);
        if (d <= 0) o.platform = -1;
    }
    const float z = o.base + o.hip;
    const bool below = water != 0 && z < water;   // [0x50bd84] = 0: no water
    constexpr float G = 0xA7 / 65536.f;
    if (o.chute) {
        // 0x440b2e: vz = -0x2222 while above the ground (or an object) and
        // the water; else the chute ends and the canopy is left (0x413d70).
        if (d > 0 && !below) o.vz = -0x2222 / 65536.f;
        else { o.chute = false; landings_.push_back(int(&o - objects.data())); }
    } else if (o.airborne) {
        if (d <= 0) {
            // Landing (0x43f600, single player): n = trunc(vz / -10360.5) - 1
            // for everyone but the player, (1 << n) - 1 damage, kind 4.
            if (o.alive()) {
                const int n = int(-o.vz * 65536.f / 10360.5f) - 1;
                if (n >= 1) {
                    const int idx = int(&o - objects.data());
                    if (auto m = damage(idx, (1 << std::min(n, 20)) - 1, 0x7fff, deathMoveNumber(true, false, 4, 0, 0)); !m.empty())
                        landingMessages_.push_back(m);
                }
            }
            o.airborne = false;
            o.base -= d;
            o.vz = 0;
        } else if (below) {
            // 0x440b9b: into the water, effect 4 and sound 0x8A at the surface.
            o.airborne = false;
            o.swimming = true;
            splashes_.push_back({o.pos.x, water, o.pos.z});
        } else {
            o.vz -= G;
        }
    } else if (o.swimming) {
        // 0x440bdb: out above the level; the bottom holds; else vz x 0xCCCC.
        if (z > water) o.swimming = false;
        else if (d < 0) { o.base -= d; o.vz = 0; }
        else o.vz = o.vz * float(0xCCCC) / 65536.f;
    } else if (d <= 0) {
        o.vz = 0;
        o.base -= d;
    } else if (below) {
        o.swimming = true;   // 0x440c5c: walking in, no splash, gravity once
        o.vz -= G;
    } else if (d > 0x800 / 65536.f && !o.ladder) {
        // 0x4470a5: no gravity on a ladder unless prone (+0x74 & 0x20,
        // 0x4470ac; never set for an AI, see 0x446e15 above).
        o.vz -= G;
        if (d > 0x8000 / 65536.f || o.vz > 0) o.airborne = true;
    }
    o.pos.y = o.base + o.hip;
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
