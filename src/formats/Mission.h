// ITEMS.DEF (what every placeable thing is) and .BMS missions (where things go).
#pragma once

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
};

class ItemDefs {
public:
    bool parse(const std::string& text);
    // BMS records store the item id minus 100000. ITEMS.DEF has one item
    // whose id was typed without the leading 10 (3034), so both are tried.
    const ItemDef* byMissionType(uint32_t type) const;
    const ItemDef* byId(int id) const;
    size_t size() const { return items_.size(); }

private:
    std::map<int, ItemDef> items_;
};

constexpr int kItemPlayerStart = 106001;

struct MissionObject {
    uint32_t type = 0;         // ITEMS.DEF id - 100000
    uint32_t id = 0;           // unique per mission
    uint32_t flags = 0;        // @0x0C, e.g. team/state bits (not yet decoded)
    float x = 0, y = 0;        // map metres; x east, y north (terrain row = -y)
    float z = 0;               // height above the ground, metres
    float heading = 0;         // degrees clockwise from north; model +X faces it
};

struct Mission {
    std::string title, author, terrain, dialogue;
    std::vector<MissionObject> objects;
};

bool parseMission(const std::vector<uint8_t>& file, Mission& out, std::string* error);

} // namespace df
