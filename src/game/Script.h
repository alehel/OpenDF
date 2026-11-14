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
// The AI conditions and actions go to the host's AI (game/Ai.*).
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
    // AI (0x45c0e4 conditions, 0x46bbe8 / 0x46c444 / 0x46cafc actions).
    virtual bool teamSpotted(int team, int targetTeam) const { (void)team; (void)targetTeam; return false; }
    virtual bool teamEngaging(int team) const { (void)team; return false; }
    virtual bool teamAlerted(int team) const { (void)team; return false; }
    virtual bool reachedWaypoint(int team, int path, int point) const { (void)team; (void)path; (void)point; return false; }
    virtual void followPath(int team, int path) { (void)team; (void)path; }
    virtual void aiProperty(int team, int subtype, int value) { (void)team; (void)subtype; (void)value; }
    virtual void teamSpeed(int team, int kmh) { (void)team; (void)kmh; }
    // 0x46ca54: someone of team p1 carries an item whose mission team is p2.
    virtual bool teamCarrying(int team, int itemTeam) const { (void)team; (void)itemTeam; return false; }
    // Delta Force 2 action 18 (0x416960): every person of `team` to the
    // teleport target (6088) of that team whose record +0x4A is `value`.
    virtual void teleportTeam(int team, int value) { (void)team; (void)value; }
    // Delta Force 2 action 16 (0x4133c0): every object of team p1 to side p2
    // (+0x68); action 17 (0x413340): every object of team p1 to team p2 (+0x6a).
    virtual void setTeamSide(int team, int side) { (void)team; (void)side; }
    virtual void moveTeam(int team, int newTeam) { (void)team; (void)newTeam; }
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
// The flags a mission starts with (the soldier's); updated when side 1 wins.
uint32_t& savedCampaignFlags();

} // namespace df
