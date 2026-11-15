#include "Script.h"

#include <algorithm>
#include <cstdio>

namespace df {

namespace {
uint32_t g_savedFlags = 0, g_flags = 0;
// DF.EXE 0x4e5854: starts at 1 and flips on every mission load; condition
// class 4 reads it, so missions can alternate between two variants.
int g_loadToggle = 1;
// The eighth of the events the next visit takes (GOG Df.exe [0x4f2be4],
// GOG Df2.exe [0x4e029c]): 0 at start-up, advanced only by the visit
// (0x42eaf0 / 0x41ae50) and never reset, so it carries over between missions.
int g_eighth = 0;
constexpr int kVisit = 128;   // ticks between visits of one event
constexpr uint32_t kItemArea = 6013;   // "Area Trigger" marker (ITEMS.DEF 106013)
} // namespace

uint32_t& campaignFlags() { return g_flags; }
uint32_t& savedCampaignFlags() { return g_savedFlags; }

void MissionScript::load(const Mission& m) {
    mission_ = &m;
    g_loadToggle ^= 1;
    g_flags = g_savedFlags;
    ticks_ = 0;
    goals_ = fails_ = 0;
    unhandled_ = 0;
    events_.clear();
    for (const auto& e : m.events) {
        Event ev;
        ev.src = &e;
        ev.flags = uint32_t(e.raw[0]);
        ev.interval = int(uint32_t(e.raw[3]) >> 16);
        ev.delay = int(uint32_t(e.raw[4]) >> 16);
        events_.push_back(ev);
    }
    // Areas for "team inside area" (0x45c9a0): the bounding box of every Area
    // Trigger marker in group 2 whose tag (record byte +0x4A) is the area
    // number; unusable (always false) unless it has width and height
    // (GOG Df.exe 0x42eea0 builds it, 0x460330 needs x1 != x2 and y1 != y2).
    conditionAreas_.assign(m.conditions.size(), {});
    for (size_t i = 0; i < m.conditions.size(); ++i) {
        const auto& c = m.conditions[i].raw;
        if (c[1] != 1 || c[2] != 10) continue;
        AreaBox box;
        if (m.version >= 0x0F) {
            // Delta Force 2 (GOG Df2.exe 0x41b200 at load, 0x4152a0 the test):
            // the first and the last marker of the tag; with fewer than two
            // the condition is zeroed (always false). The box is their min/max
            // x and y, false unless both axes differ; flag 8 when either
            // marker's record z is non-zero adds the height range.
            for (int k = 0; k < m.groupSize[2]; ++k) {
                const MissionObject* o = m.inGroup(2, k);
                if (o->type != kItemArea || int(int8_t((uint32_t(o->raw[18]) >> 16) & 0xFF)) != c[4]) continue;
                (box.markerA < 0 ? box.markerA : box.markerB) = m.groupStart[2] + k;
            }
            if (box.markerA >= 0 && box.markerB >= 0) {
                const MissionObject &a = m.objects[size_t(box.markerA)], &b = m.objects[size_t(box.markerB)];
                box.minX = std::min<double>(a.x, b.x);
                box.maxX = std::max<double>(a.x, b.x);
                box.minY = std::min<double>(a.y, b.y);
                box.maxY = std::max<double>(a.y, b.y);
                box.zRange = a.z != 0 || b.z != 0;
                box.valid = box.minX != box.maxX && box.minY != box.maxY;
            }
            conditionAreas_[i] = box;
            continue;
        }
        bool any = false;
        for (int k = 0; k < m.groupSize[2]; ++k) {
            const MissionObject* o = m.inGroup(2, k);
            if (o->type != kItemArea || ((uint32_t(o->raw[18]) >> 16) & 0xFF) != uint32_t(c[4])) continue;
            if (!any) box = {o->x, o->y, o->x, o->y, false};
            box.minX = std::min<double>(box.minX, o->x);
            box.maxX = std::max<double>(box.maxX, o->x);
            box.minY = std::min<double>(box.minY, o->y);
            box.maxY = std::max<double>(box.maxY, o->y);
            any = true;
        }
        box.valid = any && box.minX != box.maxX && box.minY != box.maxY;
        conditionAreas_[i] = box;
    }
}

bool MissionScript::condition(const MissionRecord& rec, size_t self, ScriptHost& host) const {
    const int32_t* c = rec.raw;
    const int cls = c[1], sub = c[2], p1 = c[3], p2 = c[4];
    auto total = [&](int t) { return t == 0 ? 0 : host.teamTotal(t); };
    auto alive = [&](int t) { return t == 0 ? 0 : host.teamAlive(t); };
    switch (cls) {
    case 1:
        switch (sub) {
        case 4: return alive(p1) <= 0;                       // team all dead
        case 5: return alive(p1) > 0;                        // team has someone alive
        case 6: return total(p1) - alive(p1) >= p2;          // team lost at least p2
        case 9: return total(p1) == alive(p1);               // team lost nobody
        case 10: return conditionAreas_[self].valid && host.teamInArea(p1, conditionAreas_[self]);
        case 12: return alive(p1) >= p2;                     // at least p2 alive
        case 1: return host.teamSpotted(p1, p2);              // team spotted team (0xa957e0)
        case 2: return host.teamEngaging(p1, p2);             // team engaging (0xa8d5e0; DF2 team p2, GOG Df2.exe 0x41b180)
        case 3: return host.teamAlerted(p1);                  // team alerted (team record +4 == 2)
        case 7: return host.reachedWaypoint(p1, p2, c[5]);    // team reached point p3 of path p2
        case 11: return host.teamCarrying(p1, p2);            // team carries an item of team p2
        case 13: return host.teamHit(p1, p2);                 // DF2: team p1 has hit team p2 (GOG Df2.exe 0x41b150)
        // 0 and 8 are always false.
        default: return false;
        }
    case 2: return p1 >= 0 && p1 < int(events_.size()) && events_[size_t(p1)].fired;
    case 3: return p1 >= 0 && p1 < 32 && (g_flags >> p1 & 1);
    case 4: return g_loadToggle != 0;
    // Single player with Squad Members on (GOG Df.exe 0x42e620: [0x67f520]
    // == 0 and attributes 0x200 clear; GOG DF2.exe 0x41af30: df2.cfg
    // attributes 0x20 clear).
    case 5: return host.squadMembers();
    default: return false;
    }
}

bool MissionScript::conditions(const Event& e, ScriptHost& host) const {
    // 0x45c48c: left to right, operator from the previous condition's flags
    // (bit 1 OR, bit 2 XOR, else AND); bit 0 negates.
    const int n = e.src->conditionCount, first = e.src->firstCondition;
    if (n == 0) return true;
    bool r = false;
    uint32_t op = 0;
    for (int i = 0; i < n; ++i) {
        const size_t k = size_t(first + i);
        if (k >= mission_->conditions.size()) break;
        const auto& c = mission_->conditions[k];
        bool v = condition(c, k, host);
        if (c.raw[0] & 1) v = !v;
        if (i == 0) r = v;
        else if (op & 2) r = r || v;
        else if (op & 4) r = r != v;
        else r = r && v;
        op = uint32_t(c.raw[0]);
    }
    return r;
}

void MissionScript::actions(const Event& e, ScriptHost& host) {
    for (int i = 0; i < e.src->actionCount; ++i) {
        const size_t k = size_t(e.src->firstAction + i);
        if (k >= mission_->actions.size()) break;
        const int32_t* a = mission_->actions[k].raw;
        const int type = a[1], p1 = a[3];
        switch (type) {
        case 0: break;
        case 2: host.killTeam(p1); break;
        case 4: host.removeTeam(p1); break;
        case 5: if (p1 >= 0 && p1 < 32) g_flags |= 1u << p1; break;
        case 6: host.showText(p1); break;
        case 7: host.radio(p1); break;
        // Only latches the result (GOG Df.exe 0x42e810 -> 0x444ca0(1)); the
        // flags are saved when the mission is left (see savedCampaignFlags).
        case 8: host.endMission(1); break;
        case 9: host.endMission(2); break;
        case 10: host.endMission(0); break;
        case 14: if (p1 >= 0 && p1 < 32) goals_ |= 1u << p1; break;
        case 15: if (p1 >= 0 && p1 < 32) fails_ |= 1u << p1; break;
        case 1: host.followPath(p1, a[4]); break;
        case 3: host.aiProperty(p1, a[2], a[4]); break;
        case 11: host.teamSpeed(p1, a[4]); break;
        case 16: host.setTeamSide(p1, a[4]); break;    // Delta Force 2 (0x41953a)
        case 17: host.moveTeam(p1, a[4]); break;       // Delta Force 2 (0x41954b)
        case 18: host.teleportTeam(p1, a[4]); break;   // Delta Force 2 (0x41955c)
        // 12 / 13 (AI properties of a side in a box) are not used by any mission.
        default: ++unhandled_; break;
        }
    }
}

// One visit (GOG Df.exe 0x42e9e0, GOG Df2.exe 0x41ad40 the same): with no
// timer running the event may fire; otherwise its timers count down and it
// cannot fire again in that visit, only on its next one, 128 ticks later.
void MissionScript::visit(size_t i, ScriptHost& host) {
    Event& e = events_[i];
    if (e.delayLeft == 0 && e.repeatLeft == 0) {
        if (e.fired || !conditions(e, host)) return;
        e.fired = true;
        if (e.delay) e.delayLeft = e.delay;
        else actions(e, host);
        if (e.flags & 1) {
            if (e.interval) e.repeatLeft = e.interval;
            else e.fired = false;
        }
        return;
    }
    if (e.delayLeft != 0) {
        e.delayLeft -= kVisit;
        if (e.delayLeft < 1) {
            e.delayLeft = 0;
            // The conditions again when the delay ends: false disarms it.
            if (!conditions(e, host)) { e.fired = false; e.repeatLeft = 0; return; }
            actions(e, host);
        }
    }
    if (e.repeatLeft != 0) {
        e.repeatLeft -= kVisit;
        if (e.repeatLeft < 1) { e.repeatLeft = 0; e.fired = false; }
    }
}

void MissionScript::visitFlagged(uint32_t bit, ScriptHost& host) {
    if (!mission_) return;
    for (size_t i = 0; i < events_.size(); ++i)
        if (events_[i].flags & bit) visit(i, host);
}

void MissionScript::tick(ScriptHost& host) {
    if (!mission_) return;
    // Every 16th tick, the first on the 16th (GOG Df.exe 0x443c50: the
    // counter [0x6e4734], 0 from the mission start 0x440130, is incremented
    // and tested > 15; GOG Df2.exe [0x67ffe4] the same). 0x42eaf0 (GOG
    // Df2.exe 0x41ae50) visits one eighth of the events, those with flag 2
    // (mission start) or 4 (mission end) left out.
    if (++ticks_ % 16) return;
    const int k = g_eighth;
    g_eighth = (g_eighth + 1) & 7;
    for (size_t i = size_t(k); i < events_.size(); i += 8)
        if (!(events_[i].flags & 6)) visit(i, host);
}

} // namespace df
