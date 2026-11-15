// The live mission world: placed objects with their collision boxes and
// hit points, on top of a wrapping terrain.
#pragma once

#include "../core/Pff.h"
#include "../formats/Mission.h"
#include "../render/Models.h"
#include "../render/Terrain.h"
#include "../formats/Anm.h"
#include "../formats/Chr.h"
#include "Animator.h"

#include <array>
#include <map>
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
    // Delta Force: a carried goal item (+0x6c |= 0x102, 0x46c980). It is
    // `removed` (not drawn or hit) but stays in the object list, at the
    // carrier's origin (0x4365c6 -> 0x436a0c), and sight (0x45b6e0) does not
    // skip hidden objects, so it still blocks sight lines.
    bool carried = false;
    float fall = 0;                    // 0..1 while a killed person falls
    float hip = 0;                     // people: model origin is at the hips, this far above the feet
    bool wrecked = false;              // destroyed, husk shown
    int detonate = -1;                 // explosive barrel: ticks until it explodes (DF.EXE 0x45a3c0)
    // Delta Force `gnrc` objects (GOG Df.exe 0x4533f0): ticks until a
    // destroyed one goes off (flags 4 set, model still intact); 0 = at the
    // next tick (hit again while waiting), -1 = idle or done.
    int gnrcTimer = -1;
    // From the mission record: its group in the file (0 moving objects,
    // 1 scenery, 2 markers, 3 people), team (byte +0x48), side (+0x43) and
    // position in mission coordinates.
    int missionGroup = -1, team = 0, side = 0;
    bool mapMarker = false;            // record +0x4C nonzero: shown on the command map
    int radarShape = 0;                // Delta Force 2: obj+0x6E = signed byte record +0x4C (0x403590)
    double missionX = 0, missionY = 0;
    // A gnrc object waiting to go off has flag 4 already: counted as gone
    // (0x44a200), though still in blasts and hit by rounds.
    bool alive() const { return !dead && !removed && !wrecked && gnrcTimer < 0; }
    // People: the .CHR hit spheres of their current pose (standing until
    // there is AI), centre in model axes (x forward, y left, z up) relative
    // to the body origin, and radius; metres.
    std::vector<std::array<float, 4>> spheres;
    std::vector<CollisionBox> boxes;   // model-space solid boxes
    // People: skeleton, move table and the animation playing (0x44ca34
    // poses the item's 3DI meshes, one per .CHR part).
    const Chr* chr = nullptr;
    const AnimDefs* moves = nullptr;
    Animator anim;
    float base = 0;                    // ground + lift: the origin sits its pose's height above this
    int missionIndex = -1;             // index in Mission::objects
    int moveNumber = -1;               // DF move number playing (obj+0x80)
    int killer = -1;                   // who killed it (obj+0xf8; -3 the player)
    int weaponMode = 0;                // people: the weapon held (obj+0x94, player-table mode)
    float aimPitch = 0;                // people: aim pitch (obj+0x34), radians, up positive
    float pitch = 0, roll = 0;         // vehicles: body pitch (nose up +) and roll (right side down +), radians
    // Helicopters: rotor angles (binary angle, main and tail, += speed every
    // tick, 0x47d064) and the rotor speed (blades/disc choice, 0x478114).
    uint32_t rotorAngle[2] = {0, 0};
    float rotorSpeed = 0;
    float steer = 0;
    float gunYaw = 0, gunPitch = 0;    // helo door gun (DF sense), drawn on mesh 5
    int collapse = -1;
    float vz = 0;                      // people: vertical speed, metres a tick (+0x44)
    bool airborne = false;             // people: falling (DF1 +0x6D & 0x20, DF2 +0x64 & 0x2000)
    bool chute = false;                // DF2: on a parachute (person flag +0x64 & 0x20)
    bool swimming = false;             // people: in the water (DF1 +0x6E & 2, DF2 +0x64 & 0x20000)
    float swimDz = 0;                  // people: the swim move's vertical step this tick (0x436289 / 0x440567)
    // Delta Force 2 person flags set by the volume push-out (0x440f80) each
    // tick: +0x64 & 0x400000 (a type-4 ladder volume, for movers that may
    // climb) and & 0x1000000 (a type-6 volume: an armory area).
    bool ladder = false, armory = false;
    bool climbs = false;               // DF2: AI block +0x34 & 0x400 (BMS record +0xC & 0x1000): may use ladders
    // People: the object stood on (-1 none): DF1 +0x100, the owner of the
    // face 0x435dc0 found (0 when the terrain is highest); DF2 +0xF8, the
    // object whose volume pushed up (0x440a24), cleared on the terrain (0x440b1f).
    int platform = -1;
    // Movers: this tick's velocity (DF +0x3C / +0x40, m a tick; OpenDF x / z)
    // and turn (DF1 +0x90, DF2 +0x9C; radians a tick, OpenDF heading sense),
    // as the move routine left them: what carries people standing on it
    // (DF1 0x43634d, DF2 0x440876). 0 for objects that never move.
    Vec3 tickVel{0, 0, 0};
    float tickTurn = 0;
    std::vector<int> neighbours;       // DF2 +0xFC/+0x100: objects near enough to touch (0x43f2b0)
    int tailHits = 0;                  // helo +0xD4: tail-rotor hits (meshes 3/4), grows when above 2                   // veh0: front-wheel steering (+0x94), OpenDF heading sense
    bool animated() const { return chr && moves && !anim.frozen(); }

    Mat4 matrix(Vec3 at) const;
};

struct RayHit {
    float t = 1;          // fraction along the segment
    int object = -1;      // -1 = terrain
    int part = -1;        // people: body part hit (2 = head), from the .CHR hit spheres
    int mesh = -1;        // objects hit by a round: the mesh
    uint32_t material = 0;   // objects hit by a round: the face's material flags (0x10000.. impact class)
    Vec3 normal{0, 1, 0};    // objects hit by a round: the face's normal, world axes
    Vec3 point;
};

class World {
public:
    // Delta Force 2 (.KSA, no skeleton; DF2.EXE 0x44b2f0): one sphere per
    // mesh, centred on the posed mesh pivot (0x449df0 pose, aim pitch 1/4
    // torso, 3/4 head), radius mesh +0x64. Same axes as WorldObject::spheres.
    static std::vector<std::array<float, 4>> ksaSpheres(const GpuModel& m, const Chr& chr,
                                                        const std::vector<std::array<float, 3>>& turns, float aimPitch);
    Terrain* terrain = nullptr;
    std::vector<WorldObject> objects;

    void build(Vfs& vfs, ModelRenderer& models, const ItemDefs& items, const Mission& mission);
    float extent() const { return terrain->mapExtent(); }

    // Copy of p (x/z) nearest to `ref` on the endlessly repeating map.
    Vec3 nearest(Vec3 p, Vec3 ref) const;

    // Highest surface under (x, z) that a body whose feet are at `feetY` can
    // stand on: the terrain, or the top of a box at most `step` above the feet.
    float groundHeight(Vec3 feet, float step, int* owner = nullptr) const;
    // DF.EXE 0x435dc0: the terrain, or the highest object face (LOD 0, posed,
    // not near-vertical, not 0x01000000) under (x, z) no higher than feet +
    // step. People are never surfaces.
    // `owner` (if given) gets the object of the face (DF1 +0x100), -1 the terrain.
    float faceGround(Vec3 feet, float step, int* owner = nullptr) const;
    // Riding a platform (DF.EXE 0x43634d..0x43643d, DF2.EXE 0x440876..0x440944,
    // READ): x, y += the platform's +0x3C / +0x40; if its turn is not 0, the
    // position is turned by it about the platform's origin (sin/cos tables
    // 0x435c40 / 0x41ab10, 1.0 = 0x400000) and the heading +0x30 += turn.
    void carry(int platform, Vec3& pos, float& heading) const;
    // Every person every tick (DF.EXE 0x436158, DF2.EXE 0x440400): fall by
    // gravity, land on the surface below.
    void personTick(WorldObject& o);

    // Pushes a vertical cylinder (feet at `feet`) out of solid boxes (DF.EXE
    // 0x437a28). Under Delta Force 2's rules it does nothing: DF2 has no
    // mover box, people are pushed out of the volumes in personTick.
    Vec3 resolve(Vec3 feet, float radius, float height, float step) const;

    // Delta Force 2 collision volumes (DF2.EXE 0x440f80): `count` cubes of
    // half-size halves[k] centred on pts[k] against object `n`'s LOD 0 plane
    // volumes. True if a point is inside a volume of type other than 4 / 6;
    // `push` (world axes) is then the summed least-negative plane distance
    // times its normal; flags 1 a type-4 volume, 4 a type-6 volume.
    bool df2Overlap(const WorldObject& n, const Vec3* pts, const float* halves, int count, Vec3& push, uint32_t& flags) const;
    // DF2.EXE 0x43f2b0: the objects (groups 0 and 1, not removed) within the
    // mover's margin (item type 1 vehicle 20 m, 3 person / 6 object 10 m,
    // others none) of `at`: |dx|, |dy| <= model +0x64 + margin, |dz| <=
    // model +0x60 + margin.
    std::vector<int> df2Neighbours(Vec3 at, float margin, int self = -1) const;
    // The person push-out of the DF2 person move (0x440984..0x440af1): the
    // 0.5 m cubes at the origin and 0.5 m above the feet (feet = origin +
    // feetOffset), against each neighbour; moves `origin`.
    struct Df2Contact { bool onObject = false; int platform = -1; bool ladder = false, armory = false; std::vector<int> touched; };
    Df2Contact df2PushOut(Vec3& origin, float feetOffset, const std::vector<int>& neighbours, bool mayClimb) const;
    // DF2.EXE 0x43fb80: the terrain, or the highest volume top a 1/256 m
    // point at `p` overlaps, no higher than p + step (items placed by
    // 0x4520d6 with step 0x4000).
    float df2Surface(Vec3 p, float step, const std::vector<int>& neighbours) const;
    // DF2: the horizontal step of a person's move this tick and its swim
    // dz (land: velocity / 60 along the heading, 0x440596; swimming:
    // velocity / 30 x cos(pitch) and dz = velocity / 60 x sin(pitch), not
    // upward within 0.4 m of the surface, 0x4404dc; DF.EXE 0x43621a the same).
    Vec3 personStep(WorldObject& o, float velocity) const;

    // First thing hit on the segment a→b (terrain or object box, or the
    // player's hit spheres: object -3); `objects` false tests the terrain
    // only; `ignore` skips one object (the shooter; -3 the player).
    // `people` false: people and the player do not block (lines of sight).
    // `rounds`: objects are tested as DF.EXE tests a round (0x44e918):
    // their LOD 0 faces in the pose of their draw routine, not the boxes.
    bool raycast(Vec3 a, Vec3 b, RayHit& hit, bool objects = true, int ignore = -1, bool people = true, bool rounds = false) const;
    // DF.EXE's sight line 0x45be04: <= 0 blocked, else 1 + see-through faces.
    int sight(Vec3 a, Vec3 b, int looker, int target) const;

    // The player as a target for others' rounds: body origin, heading and
    // the .CHR hit spheres of the current pose (set by the game each tick).
    struct PlayerBody { bool present = false; Vec3 origin{0, 0, 0}; float heading = 0; std::vector<std::array<float, 4>> spheres; };
    PlayerBody playerBody;

    // Explosion damage (DF.EXE blast queue 0x44fd4c / 0x450134, handler
    // 0x44fda8) at `at` with radius `radius` m; `heavy` = blast types 3/5
    // (LAW, satchel). Returns messages for what was killed or destroyed.
    // People take 4 / 2 / 1 hit points within 4 / 8 / radius + 1 m, if the
    // terrain does not block the way; objects max(0, 32 - 4 d), or
    // 128 - 16 d for heavy blasts; armour is not checked.
    std::vector<std::string> blast(Vec3 at, float radius, bool heavy);
    // Delta Force 2's rules (DF2.EXE): explosion damage (0x44d4a0), bullet
    // damage to objects (0x44bff0). Set by the game for DF2 missions.
    bool df2Rules = false;
    // Delta Force 2: the terrain's fog_level (0 off); nothing is seen past
    // 2^fog_level m (0x40621d..0x406240).
    int fogLevel = 0;
    // Swaps a destroyed object to its husk (or removes it), as damage() does
    // at once in DF1; DF2's collapsing buildings do it at step 8.
    void applyHusk(int index);
    // DF2: people who landed from a parachute this tick (for the canopy).
    std::vector<int> takeLandings() { std::vector<int> out; out.swap(landings_); return out; }
    // People who fell into the water this tick (effect 4 and sound 0x8A at
    // the surface: DF.EXE 0x4365a2, DF2.EXE 0x440bb6).
    std::vector<Vec3> takeSplashes() { std::vector<Vec3> out; out.swap(splashes_); return out; }
    // Knife (blast kind 2, 0x44ffd0, radius 1 m): every living person whose
    // body origin is within 2 m (radius + 1 m) of `at` loses 8 hit points,
    // whatever the armour; no line of sight; among objects only the piranha
    // (item 4048) is hurt.
    std::vector<std::string> stab(Vec3 at, Vec3 attacker);

    // Applies damage from a bullet. Returns a short message when something
    // is destroyed or killed, empty otherwise.
    // DF.EXE 0x44f184: no effect unless `power` reaches the target's armour
    // class (ITEMS.DEF `hp A B`, B); people die when their hit points reach 0.
    std::string damage(int index, int amount, int power = 99, int deathMove = -1, int killer = -1);
    // DF.EXE's death move (0x46c920): 0xFE in water; type 1 (bullets, knife)
    // 0xBE + part x 4 + direction; type 2 (blasts) 0xFA + direction; type
    // 3 0xFF. Direction: where the attacker is, 0 front, 1 left, 2 back,
    // 3 right of the victim.
    int deathMove(int index, int type, int part, Vec3 from) const;
    // The death move number (DF1 numbering, played by name) for kind 1
    // round/knife (part, dir), 2 blast, 3 fire, and Delta Force 2's 4 (fall /
    // contact) and 5 (drowning); DF.EXE 0x46c920, DF2.EXE 0x415130.
    static int deathMoveNumber(bool df2, bool underWater, int kind, int part, int dir);
    // Starts a person's move by DF.EXE number; false if their .ANM lacks it.
    bool startMove(WorldObject& o, int number);
    // Keeps a person in a move (obj+0x7c vs +0x80): starts it if it changed.
    void requestMove(WorldObject& o, int number);
    // Events of people's animations since the last call (footsteps).
    struct AnimEvent { int object; int event; };
    std::vector<AnimEvent> takeAnimEvents() { std::vector<AnimEvent> out; out.swap(animEvents_); return out; }

    void update(float dt);
    // Objects destroyed (not people) since the last call, for their sounds.
    std::vector<int> takeDestroyed() { std::vector<int> out; out.swap(destroyed_); return out; }

    // A new object at run time (placed charges): its model, hit points and boxes.
    int spawn(const ItemDef* item, const GpuModel* model, Vec3 pos, float heading);
    void refreshBoxes(WorldObject& o) const;

private:
    std::vector<int> landings_;
    std::vector<Vec3> splashes_;
    uint32_t listTicks_ = 0x40;        // DF2 [0x9e2e60]: neighbour lists rebuilt when it reaches 0x40
    void personTickDf1(WorldObject& o);
    void personTickDf2(WorldObject& o);
    uint32_t personTicks_ = 0;
    float personClock_ = 0;
public:
    std::vector<std::string> landingMessages_;   // "... destroyed" texts from landing damage, for the game to show
private:
    std::vector<int> destroyed_;
    std::vector<AnimEvent> animEvents_;
    std::map<std::string, Chr> chrs_;
    std::map<std::string, AnimDefs> anms_;
    float animClock_ = 0;
    int animTicks_ = 0;
    void poseUpdate(WorldObject& o);
    // The pose routines' slot flags (0x964020 + 0x40 i, +0x3C): one static
    // buffer in DF.EXE, so routines that set only some slots (tire) keep
    // the others from the previous object posed.
    mutable std::array<uint8_t, 64> hitSkip_{};
    bool roundHit(const WorldObject& o, Vec3 a, Vec3 b, float& best, bool haveObject, int& mesh, uint32_t& material, Vec3* normal = nullptr) const;
};

} // namespace df
