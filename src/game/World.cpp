#include "World.h"
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
    // Skeletons and move tables for people's hit spheres.
    std::map<std::string, Chr> chrs;
    std::map<std::string, AnimDefs> anms;
    auto spheresFor = [&](const ItemDef& item) -> std::vector<std::array<float, 4>> {
        if (item.chrFile.empty() || item.animDef.empty()) return {};
        const std::string c = toUpper(item.chrFile) + ".CHR", a = toUpper(item.animDef) + ".ANM";
        if (!chrs.count(c)) {
            std::vector<uint8_t> raw;
            if (!vfs.read(c, raw) || !chrs[c].parse(raw)) chrs[c] = Chr{};
        }
        if (!anms.count(a)) {
            std::vector<uint8_t> raw;
            if (vfs.read(a, raw)) anms[a].parse(std::string(raw.begin(), raw.end()));
        }
        const AnimMove* mv = anms[a].find("Standing_2HW");
        const ChrAnimation* an = mv ? chrs[c].animation(mv->animation) : nullptr;
        return an ? chrs[c].spheres(an->frames[0]) : std::vector<std::array<float, 4>>{};
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
        if (o.person) {
            // Character models have their origin at the hips; stand them on their feet.
            o.hip = -o.model->lods[0].min[1];
            o.pos.y += o.hip;
        }
        o.hp = item->hp;
        o.missionGroup = mo.group;
        o.team = mo.raw[18] & 0xFF;
        o.side = (uint32_t(mo.raw[16]) >> 24) & 0xFF;
        o.mapMarker = (mo.raw[19] & 0xFFFF) != 0;
        if (o.person) o.spheres = spheresFor(*item);
        o.missionX = mo.x;
        o.missionY = mo.y;
        refreshBoxes(o);
        objects.push_back(std::move(o));
    }
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
                Vec3 l = f.toLocal(feet);
                float cx = std::clamp(l.x, b.min[0], b.max[0]);
                float cz = std::clamp(l.z, b.min[2], b.max[2]);
                float ox = l.x - cx, oz = l.z - cz;
                float d2 = ox * ox + oz * oz;
                if (d2 >= radius * radius) continue;
                float px, pz;
                if (d2 > 1e-8f) {
                    float d = std::sqrt(d2);
                    px = ox / d * (radius - d);
                    pz = oz / d * (radius - d);
                } else {
                    // Centre inside the box: leave by the nearest face.
                    float e[4] = {l.x - b.min[0], b.max[0] - l.x, l.z - b.min[2], b.max[2] - l.z};
                    int k = int(std::min_element(e, e + 4) - e);
                    px = k == 0 ? -(e[0] + radius) : k == 1 ? (e[1] + radius) : 0;
                    pz = k == 2 ? -(e[2] + radius) : k == 3 ? (e[3] + radius) : 0;
                }
                // Back to world: inverse of toLocal's rotation.
                feet.x += f.c * px + f.s * pz;
                feet.z += -f.s * px + f.c * pz;
            }
        }
    }
    return feet;
}

bool World::raycast(Vec3 a, Vec3 b, RayHit& hit, bool objects) const {
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
    for (size_t i = 0; objects && i < this->objects.size(); ++i) {
        const auto& o = this->objects[i];
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

std::string World::damage(int index, int amount, int power) {
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
        o.fall = 0.001f;
        refreshBoxes(o);
        return o.item->name + " down";
    }
    if (o.hp <= 0 || !o.item) return {};   // indestructible
    o.hp -= amount;
    if (o.hp > 0) return {};
    if (o.husk) {
        o.model = o.husk;
        o.husk = nullptr;
        o.wrecked = true;
    } else {
        o.removed = true;
    }
    o.hp = 0;
    // An explosive barrel goes off 10 ticks after it is destroyed (0x45a3c0).
    if (o.item->aiFunction == "brrl" && o.detonate < 0) o.detonate = 10;
    refreshBoxes(o);
    return o.item->name + " destroyed";
}

std::vector<std::string> World::blast(Vec3 at, float radius, bool heavy) {
    std::vector<std::string> out;
    for (size_t i = 0; i < objects.size(); ++i) {
        WorldObject& o = objects[i];
        if (!o.alive()) continue;
        const Vec3 p = nearest(o.pos, at);
        if (o.person) {
            // Characters (0x785b60): within radius + 1 m of the body origin.
            const Vec3 v = p - at;
            const float dist = std::sqrt(dot(v, v));
            if (dist > radius + 1.f) continue;
            // Line of sight (0x45be04): from the body to the blast point
            // raised 0.5 m, then from 0.5 m higher; both blocked: no damage
            // for type 1, double distance otherwise.
            RayHit h;
            const Vec3 target = at + Vec3{0, 0.5f, 0};
            float d = dist;
            if (raycast(p, target, h, false) && raycast(p + Vec3{0, 0.5f, 0}, target, h, false)) {
                if (!heavy) continue;
                d *= 2;
            }
            const int amount = d < 4 ? 4 : d < 8 ? 2 : 1;
            std::string m = damage(int(i), amount);
            if (!m.empty()) out.push_back(m);
            continue;
        }
        if (o.hp <= 0 || !o.model) continue;
        // Objects (0x820f60): distance outside the object's half extents
        // (horizontal, vertical). APPROXIMATION: OpenDF uses the model's
        // LOD-0 bounds for DF.EXE's +0x64 / +0x68 extents.
        const auto& l = o.model->lods[0];
        const float hx = std::max({std::abs(l.min[0]), std::abs(l.max[0]), std::abs(l.min[2]), std::abs(l.max[2])});
        const float hz = (l.max[1] - l.min[1]) * 0.5f;
        const Vec3 c = p + Vec3{0, (l.max[1] + l.min[1]) * 0.5f, 0};
        const float ex = std::max(0.f, std::sqrt((c.x - at.x) * (c.x - at.x) + (c.z - at.z) * (c.z - at.z)) - hx);
        const float ey = std::max(0.f, std::abs(c.y - at.y) - hz);
        const float d = std::sqrt(ex * ex + ey * ey);
        if (d > radius) continue;
        const int amount = heavy ? std::max(0, int(128 - 16 * d)) : std::max(0, int(32 - 4 * d));
        if (amount <= 0) continue;
        std::string m = damage(int(i), amount);
        if (!m.empty()) out.push_back(m);
    }
    return out;
}

void World::update(float dt) {
    for (auto& o : objects)
        if (o.dead && o.fall < 1) o.fall = std::min(1.f, o.fall + dt * 2.5f);
}

} // namespace df
