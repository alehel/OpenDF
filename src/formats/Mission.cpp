#include "Mission.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace df {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string cstr(const std::vector<uint8_t>& d, size_t off, size_t n) {
    std::string s(reinterpret_cast<const char*>(&d[off]), n);
    s.resize(std::strlen(s.c_str()));
    return s;
}

uint32_t u32(const std::vector<uint8_t>& d, size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}

float fixed16(const std::vector<uint8_t>& d, size_t o) { return int32_t(u32(d, o)) / 65536.f; }

} // namespace

bool ItemDefs::parse(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    ItemDef cur;
    bool open = false;
    while (std::getline(in, line)) {
        if (auto c = line.find("//"); c != std::string::npos) line.resize(c);
        line = trim(line);
        if (line.empty() || line[0] == '/') continue;
        if (line.compare(0, 5, "begin") == 0) {
            cur = ItemDef{};
            auto q1 = line.find('"'), q2 = line.rfind('"');
            if (q1 != std::string::npos && q2 > q1) cur.name = line.substr(q1 + 1, q2 - q1 - 1);
            open = true;
            continue;
        }
        if (!open) continue;
        if (line == "end") {
            if (cur.id) items_[cur.id] = cur;
            open = false;
            continue;
        }
        size_t sp = line.find_first_of(" \t");
        std::string key = line.substr(0, sp);
        std::string val = sp == std::string::npos ? "" : trim(line.substr(sp));
        if (key == "id") cur.id = std::atoi(val.c_str());
        else if (key == "type") cur.type = val;
        else if (key == "graphic") cur.graphic = val;
        else if (key == "husk") cur.husk = val;
        else if (key == "anim_def") cur.animDef = val;
        else if (key == "chr_file") cur.chrFile = val;
        else if (key == "hp") {
            std::istringstream hs(val);
            hs >> cur.hp >> cur.damageClass;
        }
    }
    return !items_.empty();
}

const ItemDef* ItemDefs::byId(int id) const {
    auto it = items_.find(id);
    return it == items_.end() ? nullptr : &it->second;
}

const ItemDef* ItemDefs::byMissionType(uint32_t type) const {
    if (const ItemDef* d = byId(100000 + int(type))) return d;
    return byId(int(type));
}

bool parseMission(const std::vector<uint8_t>& d, Mission& m, std::string* error) {
    if (d.size() < 0x10C || std::memcmp(d.data(), "BMS", 3) != 0) {
        if (error) *error = "not a BMS mission";
        return false;
    }
    m = Mission{};
    m.title = cstr(d, 0x04, 0x28);
    m.author = cstr(d, 0x2C, 0x28);
    m.terrain = cstr(d, 0x54, 0x10);
    m.dialogue = cstr(d, 0x64, 0x10);

    // 96-byte object records from 0x10C until the first empty/invalid type.
    for (size_t r = 0x10C; r + 0x60 <= d.size(); r += 0x60) {
        const uint32_t type = u32(d, r);
        if (type == 0 || type > 100000) break;
        MissionObject o;
        o.type = type;
        o.id = u32(d, r + 0x08);
        o.flags = u32(d, r + 0x0C);
        o.x = fixed16(d, r + 0x10);
        o.y = fixed16(d, r + 0x14);
        o.z = fixed16(d, r + 0x18);
        o.heading = float(u32(d, r + 0x34) % 360);
        m.objects.push_back(o);
    }
    if (m.terrain.empty()) {
        if (error) *error = "mission names no terrain";
        return false;
    }
    return true;
}

} // namespace df
