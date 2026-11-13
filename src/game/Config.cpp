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
    for (const D& d : kDefaults) entries_.push_back({d.section, d.key, d.text, d.value, {}});
}

DfConfig::Entry* DfConfig::find(const std::string& key) {
    const std::string k = toUpper(key);
    for (Entry& e : entries_)
        if (toUpper(e.key) == k) return &e;
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
        Entry* e = find(key);
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
