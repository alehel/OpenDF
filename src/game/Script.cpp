#include "Script.h"

#include <algorithm>
#include <cstdio>

namespace df {

namespace {
uint32_t g_savedFlags = 0, g_flags = 0;
// DF.EXE 0x4e5854: starts at 1 and flips on every mission load; condition
// class 4 reads it, so missions can alternate between two variants.
int g_loadToggle = 1;
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
    visitCount_ = 0;
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
    // number; unusable (always false) unless it has width.
    conditionAreas_.assign(m.conditions.size(), {});
    for (size_t i = 0; i < m.conditions.size(); ++i) {
        const auto& c = m.conditions[i].raw;
        if (c[1] != 1 || c[2] != 10) continue;
        AreaBox box;
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
        box.valid = any && box.minX != box.maxX;
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
        case 2: return host.teamEngaging(p1);                 // team engaging (0xa8d5e0)
        case 3: return host.teamAlerted(p1);                  // team alerted (team record +4 == 2)
        case 7: return host.reachedWaypoint(p1, p2, c[5]);    // team reached point p3 of path p2
        case 11: return host.teamCarrying(p1, p2);            // team carries an item of team p2
        // 0 and 8 are always false.
        default: return false;
        }
    case 2: return p1 >= 0 && p1 < int(events_.size()) && events_[size_t(p1)].fired;
    case 3: return p1 >= 0 && p1 < 32 && (g_flags >> p1 & 1);
    case 4: return g_loadToggle != 0;
    case 5: return true;   // single player with squad members (the default option)
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
        case 8:
            g_savedFlags = g_flags;   // the campaign keeps its flags only on a win
            host.endMission(1);
            break;
        case 9: host.endMission(2); break;
        case 10: host.endMission(0); break;
        case 14: if (p1 >= 0 && p1 < 32) goals_ |= 1u << p1; break;
        case 15: if (p1 >= 0 && p1 < 32) fails_ |= 1u << p1; break;
        case 1: host.followPath(p1, a[4]); break;
        case 3: host.aiProperty(p1, a[2], a[4]); break;
        case 11: host.teamSpeed(p1, a[4]); break;
        // 12 / 13 (AI properties of a side in a box) are not used by any mission.
        default: ++unhandled_; break;
        }
    }
}

void MissionScript::visit(size_t i, ScriptHost& host) {
    Event& e = events_[i];
    if (e.delayLeft > 0) {
        e.delayLeft -= kVisit;
        if (e.delayLeft <= 0) {
            e.delayLeft = 0;
            if (conditions(e, host)) actions(e, host);
            else { e.fired = false; e.repeatLeft = 0; }
        }
    }
    if (e.repeatLeft > 0) {
        e.repeatLeft -= kVisit;
        if (e.repeatLeft <= 0) { e.repeatLeft = 0; e.fired = false; }
    }
    if (e.delayLeft == 0 && e.repeatLeft == 0 && !e.fired && conditions(e, host)) {
        e.fired = true;
        if (e.delay) e.delayLeft = e.delay;
        else actions(e, host);
        if (e.flags & 1) {
            if (e.interval) e.repeatLeft = e.interval;
            else e.fired = false;
        }
    }
}

void MissionScript::tick(ScriptHost& host) {
    if (!mission_) return;
    if (ticks_ == 0)   // mission-start events (flag bit 2; none in the shipped missions)
        for (auto& e : events_)
            if ((e.flags & 4) && conditions(e, host)) { e.fired = true; actions(e, host); }
    if (ticks_++ % 16) return;
    const int k = visitCount_++ % 8;
    for (size_t i = size_t(k); i < events_.size(); i += 8)
        if (!(events_[i].flags & (4 | 16))) visit(i, host);
}

} // namespace df
