#include "Mission.h"

#include <algorithm>
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
        if (line.compare(0, 7, "attrib:") == 0) {
            std::istringstream as(line.substr(7));
            std::string a;
            while (as >> a) {
                for (auto& ch : a) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                static const std::pair<const char*, uint32_t> bits[] = {{"good", 1}, {"evil", 2}, {"aidata", 4}, {"fo", 8},
                                                                        {"movecb", 0x10}, {"powerup", 0x20}, {"flesh", 0x40},
                                                                        {"noak", 0x80},
                                                                        // Delta Force 2 (DF2.EXE 0x41e020, strings 0x4ab5b8)
                                                                        {"notool", 0x100}, {"snap", 0x200}, {"pfoil", 0x400},
                                                                        {"train", 0x800}, {"s&d", 0x1000}, {"noctool", 0x2000}};
                for (const auto& b : bits)
                    if (a == b.first) cur.attrib |= b.second;
            }
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
        else if (key == "ai_function") cur.aiFunction = val;
        else if (key == "move_function") cur.moveFunction = val;
        else if (key == "interrupt_sound_id") cur.interruptSound = std::atoi(val.c_str());
        else if (key == "phrase_set") cur.phraseSet = std::atoi(val.c_str());
        else if (key == "acceleration") cur.acceleration = float(std::atof(val.c_str()));
        else if (key == "turn_rate") cur.turnRate = float(std::atof(val.c_str()));
        else if (key == "axle_dist") cur.axleDist = float(std::atof(val.c_str()));
        else if (key == "hp") {
            std::istringstream hs(val);
            hs >> cur.hp >> cur.damageClass >> cur.hp3;   // DF2: up to three (+0x94/+0x98/+0x9c)
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
    m.version = d[3];
    // Delta Force 2 and later (version 0x0F, DF2.EXE 0x403bd0): a 0x208-byte
    // header with 32-byte title/author, terrain at 0x44 and dialogue at 0x54,
    // flags at 0x78 (night 0x100000), weather at 0xAC instead of the route
    // names (those moved to the waypoint records' +0x58); the rest as DF1.
    const bool v15 = m.version >= 0x0F;
    if (v15) {
        if (d.size() < 0x208) {
            if (error) *error = "truncated BMS header";
            return false;
        }
        m.title = cstr(d, 0x04, 0x20);
        m.author = cstr(d, 0x24, 0x20);
        m.terrain = cstr(d, 0x44, 0x10);
        m.dialogue = cstr(d, 0x54, 0x10);
        m.lightFlags = (u32(d, 0x78) & 0x100000) ? 0x10 : 0;
        // Wind for the whole mission (DF2.EXE 0x45d3f8 -> 0x46bbf0): speed
        // kph at +0xE8, the direction it blows from in degrees at +0xEC.
        // Flag 0x78 bit 0: the mission's own water height (word +0x88, TRN
        // water_height units; DF2.EXE 0x45d6ae).
        if (u32(d, 0x78) & 1) m.waterOverride = int(u32(d, 0x88) & 0xFFFF);
        // The sky block (a SUNSETS.DEF entry): clouds 0xB8, gradient 0xC8,
        // height 0xD8, horizon 16.16 0xDC, gamma/sat/r/g/b bytes 0xE0..0xE4.
        m.sky.clouds = cstr(d, 0xB8, 0x10);
        m.sky.gradient = cstr(d, 0xC8, 0x10);
        m.sky.on = !m.sky.clouds.empty() && !m.sky.gradient.empty();
        m.sky.height = float(int32_t(u32(d, 0xD8)));
        m.sky.horizon = float(int32_t(u32(d, 0xDC))) / 65536.f;
        m.sky.gamma = d[0xE0];
        m.sky.saturation = d[0xE1];
        for (int k = 0; k < 3; ++k) m.sky.filter[k] = d[size_t(0xE2 + k)];
        // Goals (DF2.EXE 0x442520): win ids +0xB0..+0xB3, lose ids +0xB4..+0xB7
        // (DF2WPN.BIN STRWINCOND / STRLOSECOND%03i), 0 ends a list.
        for (int k = 0; k < 4; ++k) {
            m.winIds[k] = d[size_t(0xB0 + k)];
            m.loseIds[k] = d[size_t(0xB4 + k)];
        }
        m.windKph = int(u32(d, 0xE8));
        m.windDeg = int(u32(d, 0xEC));
    } else {
        m.title = cstr(d, 0x04, 0x28);
        m.author = cstr(d, 0x2C, 0x28);
        m.terrain = cstr(d, 0x54, 0x10);
        m.dialogue = cstr(d, 0x64, 0x10);
        for (int i = 0; i < 8; ++i) m.routeNameIds[i] = int(u32(d, 0xAC + 4 * i));
        m.lightFlags = d.size() > 0x7A ? d[0x7A] : 0;
    }
    m.colorVariant = int(u32(d, 0x74) & 3);
    m.variantWord = int(u32(d, 0x74));
    m.fireMissions = int(u32(d, 0x84));

    // Layout from DF.EXE's loader (0x440490): four groups of 96-byte object
    // records (counts at 0x98..0xA4), 128 paths of 0x88 bytes, 64 records of
    // 32 bytes, 32 names of 20 bytes, then the event system.
    auto fail = [&](const char* what) { if (error) *error = what; return false; };
    size_t o = v15 ? 0x208 : 0x10C;
    for (int g = 0; g < 4; ++g) {
        const uint32_t n = u32(d, 0x98 + 4 * g);
        if (n > 10000 || o + size_t(n) * 0x60 > d.size()) return fail("BMS object table truncated");
        m.groupStart[g] = int(m.objects.size());
        m.groupSize[g] = int(n);
        for (uint32_t i = 0; i < n; ++i, o += 0x60) {
            MissionObject ob;
            ob.type = u32(d, o);
            ob.id = u32(d, o + 0x08);
            ob.flags = u32(d, o + 0x0C);
            ob.x = fixed16(d, o + 0x10);
            ob.y = fixed16(d, o + 0x14);
            ob.z = fixed16(d, o + 0x18);
            ob.heading = float(u32(d, o + 0x34) % 360);
            ob.group = g;
            ob.indexInGroup = int(i);
            for (int k = 0; k < 24; ++k) ob.raw[k] = int32_t(u32(d, o + 4 * k));
            m.objects.push_back(ob);
        }
    }
    if (o + 128 * 0x88 + 64 * 32 + 32 * 20 + 12 > d.size()) return fail("BMS path tables truncated");
    for (int i = 0; i < 128; ++i, o += 0x88) {
        MissionPath p;
        p.flags = u32(d, o);
        const uint32_t n = std::min<uint32_t>(u32(d, o + 4), 32);
        for (uint32_t k = 0; k < n; ++k) p.points.push_back(int(u32(d, o + 8 + 4 * k)));
        m.paths.push_back(std::move(p));
    }
    o += 64 * 32 + 32 * 20;
    const uint32_t nE = u32(d, o), nC = u32(d, o + 4), nA = u32(d, o + 8);
    o += 12;
    if (nE > 100000 || nC > 100000 || nA > 100000 || o + nE * 24 + (nC + nA) * size_t(32) > d.size())
        return fail("BMS event tables truncated");
    for (uint32_t i = 0; i < nE; ++i, o += 24) {
        MissionEvent e;
        for (int k = 0; k < 6; ++k) e.raw[k] = int32_t(u32(d, o + 4 * k));
        e.firstCondition = e.raw[1];
        e.firstAction = e.raw[2];
        e.conditionCount = (e.raw[5] >> 8) & 0xFF;
        e.actionCount = (e.raw[5] >> 16) & 0xFF;
        m.events.push_back(e);
    }
    for (auto* list : {&m.conditions, &m.actions}) {
        const uint32_t n = list == &m.conditions ? nC : nA;
        for (uint32_t i = 0; i < n; ++i, o += 32) {
            MissionRecord r;
            for (int k = 0; k < 8; ++k) r.raw[k] = int32_t(u32(d, o + 4 * k));
            list->push_back(r);
        }
    }
    if (m.terrain.empty()) {
        if (error) *error = "mission names no terrain";
        return false;
    }
    return true;
}

} // namespace df
