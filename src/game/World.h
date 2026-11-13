// The live mission world: placed objects with their collision boxes and
// hit points, on top of a wrapping terrain.
#pragma once

#include "../core/Pff.h"
#include "../formats/Mission.h"
#include "../render/Models.h"
#include "../render/Terrain.h"

#include <array>
#include <string>
#include <vector>

namespace df {

struct WorldObject {
    const ItemDef* item = nullptr;
    const GpuModel* model = nullptr;   // current graphic (the husk once destroyed)
    const GpuModel* husk = nullptr;
    Vec3 pos;                          // map position (x/z wrapped into the map), y = ground + lift
    float heading = 0;                 // radians clockwise from north
    float lift = 0;                    // height above ground from the mission
    int hp = 0;                        // 0 = indestructible
    bool person = false;
    bool dead = false;                 // person killed
    bool removed = false;              // destroyed without a husk
    float fall = 0;                    // 0..1 while a killed person falls
    float hip = 0;                     // people: model origin is at the hips, this far above the feet
    bool wrecked = false;              // destroyed, husk shown
    int detonate = -1;                 // explosive barrel: ticks until it explodes (DF.EXE 0x45a3c0)
    // From the mission record: its group in the file (0 moving objects,
    // 1 scenery, 2 markers, 3 people), team (byte +0x48), side (+0x43) and
    // position in mission coordinates.
    int missionGroup = -1, team = 0, side = 0;
    bool mapMarker = false;            // record +0x4C nonzero: shown on the command map
    double missionX = 0, missionY = 0;
    bool alive() const { return !dead && !removed && !wrecked; }
    // People: the .CHR hit spheres of their current pose (standing until
    // there is AI), centre in model axes (x forward, y left, z up) relative
    // to the body origin, and radius; metres.
    std::vector<std::array<float, 4>> spheres;
    std::vector<CollisionBox> boxes;   // model-space solid boxes

    Mat4 matrix(Vec3 at) const;
};

struct RayHit {
    float t = 1;          // fraction along the segment
    int object = -1;      // -1 = terrain
    int part = -1;        // people: body part hit (2 = head), from the .CHR hit spheres
    Vec3 point;
};

class World {
public:
    Terrain* terrain = nullptr;
    std::vector<WorldObject> objects;

    void build(Vfs& vfs, ModelRenderer& models, const ItemDefs& items, const Mission& mission);
    float extent() const { return terrain->mapExtent(); }

    // Copy of p (x/z) nearest to `ref` on the endlessly repeating map.
    Vec3 nearest(Vec3 p, Vec3 ref) const;

    // Highest surface under (x, z) that a body whose feet are at `feetY` can
    // stand on: the terrain, or the top of a box at most `step` above the feet.
    float groundHeight(Vec3 feet, float step) const;

    // Pushes a vertical cylinder (feet at `feet`) out of solid boxes.
    Vec3 resolve(Vec3 feet, float radius, float height, float step) const;

    // First thing hit on the segment a→b (terrain or object box);
    // `objects` false tests the terrain only.
    bool raycast(Vec3 a, Vec3 b, RayHit& hit, bool objects = true) const;

    // Explosion damage (DF.EXE blast queue 0x44fd4c / 0x450134, handler
    // 0x44fda8) at `at` with radius `radius` m; `heavy` = blast types 3/5
    // (LAW, satchel). Returns messages for what was killed or destroyed.
    // People take 4 / 2 / 1 hit points within 4 / 8 / radius + 1 m, if the
    // terrain does not block the way; objects max(0, 32 - 4 d), or
    // 128 - 16 d for heavy blasts; armour is not checked.
    std::vector<std::string> blast(Vec3 at, float radius, bool heavy);
    // Knife (blast kind 2, 0x44ffd0, radius 1 m): every living person whose
    // body origin is within 2 m (radius + 1 m) of `at` loses 8 hit points,
    // whatever the armour; no line of sight; among objects only the piranha
    // (item 4048) is hurt.
    std::vector<std::string> stab(Vec3 at);

    // Applies damage from a bullet. Returns a short message when something
    // is destroyed or killed, empty otherwise.
    // DF.EXE 0x44f184: no effect unless `power` reaches the target's armour
    // class (ITEMS.DEF `hp A B`, B); people die when their hit points reach 0.
    std::string damage(int index, int amount, int power = 99);

    void update(float dt);
    // Objects destroyed (not people) since the last call, for their sounds.
    std::vector<int> takeDestroyed() { std::vector<int> out; out.swap(destroyed_); return out; }

private:
    void refreshBoxes(WorldObject& o) const;
    std::vector<int> destroyed_;
};

} // namespace df
