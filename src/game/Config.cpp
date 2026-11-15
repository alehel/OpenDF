#include "Config.h"

#include "../core/Pff.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace df {

DfConfig::DfConfig() {
    // Write order 0x40dc88; defaults 0x40dab8 (renderer 1 and max_res 4 are
    // the values for an OS version above 5, [0x4e552c]).
    struct D { const char* section; const char* key; int value; bool text; };
    static const D kDefaults[] = {
        {"MUSIC", "cd_play", 0, false}, {"MUSIC", "menu_track", 2, false}, {"MUSIC", "game_track", 2, false},
        {"MUSIC", "cd_volume", 255, false}, {"MUSIC", "fx_volume", 255, false}, {"MUSIC", "dialog_volume", 200, false},
        {"DISPLAY", "game_res", 3, false}, {"DISPLAY", "window_size", 1, false}, {"DISPLAY", "windowed", 0, false},
        {"DISPLAY", "renderer", 1, false}, {"DISPLAY", "max_res", 4, false}, {"DISPLAY", "gamma", 128, false},
        {"DISPLAY", "turbo", 0, false},
        {"CONTROLS", "flipmouse", 0, false}, {"CONTROLS", "crosshairs", 1, false}, {"CONTROLS", "mousescale", 128, false},
        {"CONTROLS", "showgun", 1, false}, {"CONTROLS", "picinpic", 0, false},
        {"MULTIPLAYER", "login_password", 0, true}, {"MULTIPLAYER", "game_password", 0, true},
        {"MULTIPLAYER", "squad_password", 0, true}, {"MULTIPLAYER", "game_name", 0, true},
        {"MULTIPLAYER", "phone_number", 0, true}, {"MULTIPLAYER", "internet_address", 0, true},
        {"MULTIPLAYER", "mp_service", 4, false}, {"MULTIPLAYER", "mp_gametype", 0, false},
        {"MULTIPLAYER", "maxplayers", 16, false}, {"MULTIPLAYER", "dedicated", 0, false},
        {"MULTIPLAYER", "com_port", 0, false}, {"MULTIPLAYER", "baud_rate", 0, false},
        {"MULTIPLAYER", "max_lives", 20, false}, {"MULTIPLAYER", "max_kills", 20, false},
        {"MULTIPLAYER", "team_req", 1, false}, {"MULTIPLAYER", "attributes", 0, false},
        {"MULTIPLAYER", "replay", 0, false}, {"MULTIPLAYER", "time_limit", 30, false},
        {"MULTIPLAYER", "koth_limit", 30, false}, {"MULTIPLAYER", "ammo_respawn", 2, false},
        {"MULTIPLAYER", "timeout", 0, false}, {"MULTIPLAYER", "gpsicon", 1, false},
        {"MULTIPLAYER", "showtags", 0, false}, {"MULTIPLAYER", "contest", 0, false},
        {"MAP", "map_infrared", 1, false}, {"MAP", "map_iff", 1, false}, {"MAP", "map_AWAC", 1, false},
        {"SYSTEM", "preempt_pff", 0, false}, {"SYSTEM", "whichbrowser", 0, false},
    };
    for (const D& d : kDefaults) entries_.push_back({d.section, d.key, d.text, d.value, {}, {}});
}

DfConfig DfConfig::df2(bool gog) {
    DfConfig c;
    c.entries_.clear();
    struct D { const char* section; const char* key; int value; const char* str; };
    // Write order 0x436cd0; defaults 0x436ab0 (df2_menus.md §4).
    static const D kDf2[] = {
        {"DISPLAY", "video_type", 1, nullptr}, {"DISPLAY", "game_res", 4, nullptr}, {"DISPLAY", "no_anim", 0, nullptr},
        {"DISPLAY", "window_size", 1, nullptr}, {"DISPLAY", "music_volume", 255, nullptr}, {"DISPLAY", "gamma", 128, nullptr},
        {"DISPLAY", "turbo", 0, nullptr},
        {"CONTROLS", "crosshairs", 1, nullptr}, {"CONTROLS", "showgun", 1, nullptr}, {"CONTROLS", "picinpic", 0, nullptr},
        {"MULTIPLAYER", "login_password", 0, ""}, {"MULTIPLAYER", "game_password", 0, ""},
        {"MULTIPLAYER", "squad_password", 0, ""}, {"MULTIPLAYER", "game_name", 0, ""},
        {"MULTIPLAYER", "phone_number", 0, ""}, {"MULTIPLAYER", "internet_address", 0, "0.0.0.0"},
        {"MULTIPLAYER", "mp_service", 4, nullptr}, {"MULTIPLAYER", "mp_gametype", 0, nullptr},
        {"MULTIPLAYER", "maxplayers", 16, nullptr}, {"MULTIPLAYER", "dedicated", 0, nullptr},
        {"MULTIPLAYER", "com_port", 0, nullptr}, {"MULTIPLAYER", "baud_rate", 0, nullptr},
        {"MULTIPLAYER", "max_team_lives", 100, nullptr}, {"MULTIPLAYER", "max_kills", 20, nullptr},
        {"MULTIPLAYER", "team_req", 1, nullptr}, {"MULTIPLAYER", "attributes", 2, nullptr},
        {"MULTIPLAYER", "startdelay", 0, nullptr}, {"MULTIPLAYER", "destroybuild", 0, nullptr},
        {"MULTIPLAYER", "deathmes", 1, nullptr}, {"MULTIPLAYER", "replay", 0, nullptr},
        {"MULTIPLAYER", "time_limit", 30, nullptr}, {"MULTIPLAYER", "koth_limit", 5, nullptr},
        {"MULTIPLAYER", "timeout", 0, nullptr}, {"MULTIPLAYER", "mp_numteams", 2, nullptr},
        {"MULTIPLAYER", "gpsicon", 1, nullptr}, {"MULTIPLAYER", "sp_wind", 0, nullptr},
        {"MULTIPLAYER", "showtags", 0, nullptr}, {"MULTIPLAYER", "contest", 0, nullptr},
        {"MAP", "map_infrared", 1, nullptr}, {"MAP", "map_iff", 1, nullptr}, {"MAP", "map_AWAC", 1, nullptr},
        {"SYSTEM", "preempt_pff", 0, nullptr}, {"SYSTEM", "whichbrowser", 0, nullptr}, {"SYSTEM", "country", 0, ""},
        {"SYSTEM", "red_password", 0, ""}, {"SYSTEM", "blue_password", 0, ""}, {"SYSTEM", "yellow_password", 0, ""},
        {"SYSTEM", "violet_password", 0, ""}, {"SYSTEM", "mp_verbose", 1, nullptr},
    };
    for (const D& d : kDf2) {
        // GOG Df2.exe's writer 0x43bd10 (CD 0x436cd0) adds mp_gameport
        // [0x4f29e8], mp_vonport [0x4f29ec] and mp_usealtnwport [0x4f29f0]
        // (all 0, 0x43bae0) before login_password, and "flagreturntime"
        // [0x4f2b18] (default 0xd2) after contest. Its parser 0x43b110 reads
        // that value back only as "mp_flagreturntime" (0x4c7634).
        if (gog && std::string(d.key) == "login_password")
            for (const char* k : {"mp_gameport", "mp_vonport", "mp_usealtnwport"}) c.entries_.push_back({"MULTIPLAYER", k, false, 0, {}, {}});
        c.entries_.push_back({d.section, d.key, d.str != nullptr, d.value, d.str ? d.str : "", {}});
        if (gog && std::string(d.key) == "contest")
            c.entries_.push_back({"MULTIPLAYER", "flagreturntime", false, 0xd2, {}, "mp_flagreturntime"});
    }
    return c;
}

DfConfig::Entry* DfConfig::find(const std::string& key) {
    const std::string k = toUpper(key);
    for (Entry& e : entries_)
        if (toUpper(e.key) == k) return &e;
    return nullptr;
}

DfConfig::Entry* DfConfig::findLoaded(const std::string& key) {
    const std::string k = toUpper(key);
    for (Entry& e : entries_)
        if (toUpper(e.loadKey.empty() ? e.key : e.loadKey) == k) return &e;
    return nullptr;
}

const DfConfig::Entry* DfConfig::find(const std::string& key) const {
    return const_cast<DfConfig*>(this)->find(key);
}

int DfConfig::get(const std::string& key) const {
    const Entry* e = find(key);
    return e ? e->value : 0;
}

void DfConfig::set(const std::string& key, int value) {
    if (Entry* e = find(key)) e->value = value;
}

bool DfConfig::load(const std::filesystem::path& file) {
    std::ifstream f(file);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        // Tokens: key, '=', value (strings in quotes); '/' starts a comment line.
        size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos || line[a] == '/') continue;
        const size_t eq = line.find('=', a);
        if (eq == std::string::npos) continue;
        std::string key = line.substr(a, eq - a);
        key.erase(key.find_last_not_of(" \t") + 1);
        std::string value = line.substr(eq + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t\r") + 1);
        Entry* e = findLoaded(key);
        if (!e) continue;   // unknown keys are ignored, as 0x40d1e4 does
        if (e->text) {
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
            e->str = value;
        } else {
            e->value = std::atoi(value.c_str());   // atoi (0x490eaa)
        }
    }
    return true;
}

bool DfConfig::save(const std::filesystem::path& file) const {
    std::ofstream f(file);
    if (!f) return false;
    std::string section;
    for (const Entry& e : entries_) {
        if (e.section != section) {
            if (!section.empty()) f << "\n";
            f << "// " << e.section << "\n";
            section = e.section;
        }
        char line[160];
        if (e.text) std::snprintf(line, sizeof line, "%-16s = \"%s\"\n", e.key.c_str(), e.str.c_str());
        else std::snprintf(line, sizeof line, "%-16s = %i\n", e.key.c_str(), e.value);
        f << line;
    }
    f << "\n";
    return bool(f);
}

} // namespace df
