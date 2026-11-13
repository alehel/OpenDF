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
        else if (key == "interrupt_sound_id") cur.interruptSound = std::atoi(val.c_str());
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
    for (int i = 0; i < 8; ++i) m.routeNameIds[i] = int(u32(d, 0xAC + 4 * i));
    m.colorVariant = int(u32(d, 0x74) & 3);

    // Layout from DF.EXE's loader (0x440490): four groups of 96-byte object
    // records (counts at 0x98..0xA4), 128 paths of 0x88 bytes, 64 records of
    // 32 bytes, 32 names of 20 bytes, then the event system.
    auto fail = [&](const char* what) { if (error) *error = what; return false; };
    size_t o = 0x10C;
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
