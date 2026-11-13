// The live mission world: placed objects with their collision boxes and
// hit points, on top of a wrapping terrain.
#pragma once

#include "../core/Pff.h"
#include "../formats/Mission.h"
#include "../render/Models.h"
#include "../render/Terrain.h"

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
    std::vector<CollisionBox> boxes;   // model-space solid boxes

    Mat4 matrix(Vec3 at) const;
};

struct RayHit {
    float t = 1;          // fraction along the segment
    int object = -1;      // -1 = terrain
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

    // First thing hit on the segment a→b (terrain or object box).
    bool raycast(Vec3 a, Vec3 b, RayHit& hit) const;

    // Applies damage from a bullet. Returns a short message when something
    // is destroyed or killed, empty otherwise.
    std::string damage(int index, int amount);

    void update(float dt);

private:
    void refreshBoxes(WorldObject& o) const;
};

} // namespace df
