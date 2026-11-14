// ITEMS.DEF (what every placeable thing is) and .BMS missions (where things go).
#pragma once

#include "Trn.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace df {

struct ItemDef {
    int id = 0;
    std::string name;
    std::string type;      // decoration, building, vehicle, person, object, marker, effect
    std::string graphic;   // .3DI name, empty for markers
    std::string husk;      // destroyed graphic
    std::string animDef;   // soldiers: .ANM move table (`anim_def`)
    std::string chrFile;   // soldiers: .CHR animation file (`chr_file`)
    // `hp A B`: A = hit points (0 = indestructible). B looks like a damage
    // class: 0 soft (bullets work), 1 armoured vehicles, 2 trees; inferred
    // from which items use which value.
    int hp = 0, damageClass = 0;
    int hp3 = 0;              // Delta Force 2: a third `hp` value (item +0x9c), 0-4 on buildings; meaning not traced
    std::string aiFunction;   // `ai_function` (brrl: explosive barrel, sgen: smoke generator, ...)
    std::string moveFunction; // `move_function` (veh0, helo, tire; DF2: herc, boat, loco, carg, para, smk2, shck)
    int interruptSound = 0;
    int phraseSet = 0;        // `phrase_set`: voice set (0x4F019C)
    // Vehicles (parser 0x448cec): `acceleration` (m/s^2 at 64 ticks/s,
    // item +0xdc), `turn_rate` (degrees/s, +0xe0), `axle_dist` (m, +0xe4).
    float acceleration = 0, turnRate = 0, axleDist = 0;
    // `attrib:` flags (0x449262, def +0x24): good 1, evil 2, AIData 4, FO 8,
    // movecb 0x10 (touch callback: pickups), powerup 0x20, flesh 0x40, NoAK 0x80;
    // Delta Force 2 adds NoTool 0x100, Snap 0x200, PFoil 0x400, Train 0x800,
    // S&D 0x1000, NoCTool 0x2000.
    uint32_t attrib = 0;   // `interrupt_sound_id`: sound id the object repeats (ambience)
};

class ItemDefs {
public:
    bool parse(const std::string& text);
    // BMS records store the item id minus 100000. ITEMS.DEF has one item
    // whose id was typed without the leading 10 (3034), so both are tried.
    const ItemDef* byMissionType(uint32_t type) const;
    const ItemDef* byId(int id) const;
    const ItemDef* byName(const std::string& name) const;
    size_t size() const { return items_.size(); }

private:
    std::map<int, ItemDef> items_;
};

constexpr int kItemPlayerStart = 106001;
constexpr int kItemPlayerSoldier = 105003;   // "player01": the player's soldier

struct MissionObject {
    uint32_t type = 0;         // ITEMS.DEF id - 100000
    uint32_t id = 0;           // unique per mission
    uint32_t flags = 0;        // @0x0C, e.g. team/state bits (not yet decoded)
    float x = 0, y = 0;        // map metres; x east, y north (terrain row = -y)
    float z = 0;               // height above the ground, metres
    float heading = 0;         // degrees clockwise from north; model +X faces it
    int group = 0;             // which of the file's four object groups (0..3)
    int indexInGroup = 0;      // paths and events refer to objects by this
    int32_t raw[24] = {};      // the whole 96-byte record, for fields not yet decoded
};

// A path: an ordered list of objects of group 2 (waypoint markers). The
// player's route is one of them. DF.EXE keeps 128 (0x748360, 0x88 bytes).
struct MissionPath {
    uint32_t flags = 0;
    std::vector<int> points;   // indices into group 2
};

// The mission's event system (DF.EXE 0x45C7BC): each event has a run of
// conditions and a run of actions. Records are kept raw until decoded.
struct MissionEvent {
    int32_t raw[6] = {};
    int firstCondition = 0, conditionCount = 0;
    int firstAction = 0, actionCount = 0;
};
struct MissionRecord { int32_t raw[8] = {}; };   // a condition or an action

struct Mission {
    int version = 0x0D;                     // header byte 3: 0x0D Delta Force, 0x0F Delta Force 2 and later
    std::string title, author, terrain, dialogue;
    std::vector<MissionObject> objects;     // all groups, in file order
    int groupStart[4] = {}, groupSize[4] = {};
    std::vector<MissionPath> paths;         // always 128
    std::vector<MissionEvent> events;
    std::vector<MissionRecord> conditions, actions;
    // The player's route names: header +0xAC, one number per point of the
    // route path; text "WPN%03i" in the campaign's "Waypoint Names" section.
    int routeNameIds[8] = {};
    // Header +0x74: which of each model material's four colour/texture
    // variants this mission uses (DF.EXE 0x43f964).
    int colorVariant = 0;
    // Header +0x84: fire missions (artillery strikes) available (0x44319d).
    int fireMissions = 0;
    // Header +0x74 as a whole (2 = the snow soldiers) and +0x7A (0x10 =
    // night: night soldiers, tracers) (0x469ae0, 0x44d5a0).
    int variantWord = 0, lightFlags = 0;
    int windKph = 0, windDeg = 0;
    int waterOverride = -1;
    // Delta Force 2, applied after the terrain loads (0x45d6df): flag 0x78
    // bit 1 -> fog_level = +0x8C (0x45d6e7), bit 2 -> fog_color = +0x90,
    // R << 16 | G << 8 | B (0x45d6f7); -1 not set.
    int fogLevel = -1;
    int64_t fogColor = -1;
    // Flag 0x78 bit 3 -> the weather type +0xAC (0x45d707 -> 0x46ba90):
    // 0 none, 1 rain, 2 snow (READ: type 1 splashes with effect 17 = item
    // 107017 "rain poink", type 2 draws item 0x1b6a = 107018 "snow", 0x46b94d).
    int weather = 0;
    SkyOverride sky;                         // Delta Force 2: header 0xB8..0xE4
    int winIds[4] = {0, 0, 0, 0}, loseIds[4] = {0, 0, 0, 0};   // Delta Force 2: header 0xB0 / 0xB4                  // Delta Force 2: header flag 0x78 bit 0 -> +0x88; -1 none            // Delta Force 2: header +0xE8 / +0xEC
    // The object at `index` of `group`, or null.
    const MissionObject* inGroup(int group, int index) const {
        if (group < 0 || group > 3 || index < 0 || index >= groupSize[group]) return nullptr;
        return &objects[size_t(groupStart[group] + index)];
    }
};

bool parseMission(const std::vector<uint8_t>& file, Mission& out, std::string* error);

} // namespace df
