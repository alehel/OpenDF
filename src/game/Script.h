// The mission event system, as DF.EXE runs it (0x45c368..0x45cd00): events
// with a run of conditions and a run of actions, stored in the .BMS.
//
// Timing (0x445914/0x45c644): every 16 ticks one eighth of the events is
// visited (index mod 8), so each event is looked at every 128 ticks and its
// timers count down 128 at a time. Conditions combine strictly left to right
// with the previous condition's operator (AND, OR, XOR) and may be negated.
// A fired event runs its actions at once or after its delay (the conditions
// are checked again when the delay ends); repeating events re-arm.
//
// Behaviour that needs AI (teams spotting, engaging or reaching waypoints,
// paths, AI properties) is evaluated as "not yet" / ignored until OpenDF has
// AI; see docs/VERIFICATION.md.
#pragma once

#include "../formats/Mission.h"

#include <cstdint>
#include <string>
#include <vector>

namespace df {

struct AreaBox { double minX = 0, minY = 0, maxX = 0, maxY = 0; bool valid = false; };

// What the script asks of the game.
class ScriptHost {
public:
    virtual ~ScriptHost() = default;
    virtual int teamTotal(int team) const = 0;
    virtual int teamAlive(int team) const = 0;
    virtual bool teamInArea(int team, const AreaBox& area) const = 0;
    virtual void killTeam(int team) = 0;
    virtual void removeTeam(int team) = 0;
    virtual void showText(int id) = 0;     // campaign "Triggered Text", key ID%03i
    virtual void radio(int id) = 0;        // dialog dlg%03i
    virtual void endMission(int winnerSide) = 0;   // 0 = nobody
};

class MissionScript {
public:
    void load(const Mission& mission);
    // Call once per 60 Hz tick.
    void tick(ScriptHost& host);

    bool goalDone(int n) const { return n >= 0 && n < 32 && (goals_ >> n & 1); }
    bool failDone(int n) const { return n >= 0 && n < 32 && (fails_ >> n & 1); }
    int unhandledActions() const { return unhandled_; }

private:
    struct Event {
        const MissionEvent* src = nullptr;
        uint32_t flags = 0;
        int delay = 0, interval = 0;       // ticks
        int delayLeft = 0, repeatLeft = 0;
        bool fired = false;
    };

    bool condition(const MissionRecord& c, size_t self, ScriptHost& host) const;
    bool conditions(const Event& e, ScriptHost& host) const;
    void actions(const Event& e, ScriptHost& host);
    void visit(size_t i, ScriptHost& host);

    const Mission* mission_ = nullptr;
    std::vector<Event> events_;
    std::vector<AreaBox> conditionAreas_;   // per condition, for class 1.10
    uint64_t ticks_ = 0;
    int visitCount_ = 0;
    uint32_t goals_ = 0, fails_ = 0;
    int unhandled_ = 0;
};

// Campaign flags (DF.EXE 0x66f9d8): restored at mission start, saved back
// when side 1 wins. Kept for the session.
uint32_t& campaignFlags();

} // namespace df
