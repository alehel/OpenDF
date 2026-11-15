#include "Config.h"

#include "../core/Pff.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace df {

DfConfig::DfConfig() {
    // Write order 0x40dc88; defaults 0x40dab8 (renderer 1 and max_res 4 are
    // the values for an OS version above 5, [0x4e552c]; internet_address
    // "0.0.0.0", the rest of the text keys empty). The GOG Df.exe has the
    // same keys and defaults (0x407120, 0x407360) except df1Gog()'s.
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
    find("internet_address")->str = "0.0.0.0";
}

DfConfig DfConfig::df1Gog() {
    // GOG Df.exe 0x407120: attributes 2 ([0x4cee8c]); the writer 0x407360
    // adds four keys after whichbrowser (all read back by 0x406640):
    // country (0x4c8b78, the only line with tabs before its '='),
    // red_password, blue_password (empty) and mp_verbose 1 ([0x4cf2e0]).
    DfConfig c;
    c.set("attributes", 2);
    c.entries_.push_back({"SYSTEM", "country", true, 0, {}, "country\t\t\t"});
    c.entries_.push_back({"SYSTEM", "red_password", true, 0, {}, {}});
    c.entries_.push_back({"SYSTEM", "blue_password", true, 0, {}, {}});
    c.entries_.push_back({"SYSTEM", "mp_verbose", false, 1, {}, {}});
    return c;
}

DfConfig DfConfig::df2() {
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
    for (const D& d : kDf2) c.entries_.push_back({d.section, d.key, d.str != nullptr, d.value, d.str ? d.str : "", {}});
    return c;
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
        // GOG Df.exe 0x4568e0: a line (up to 700 bytes, cut at the newline;
        // text mode drops a CR) is split by 0x4565f0 on ' ', ',' and '\t',
        // a double quote grouping up to the next one (the quotes removed,
        // so "" makes no token), at most 30 tokens. No token, or a first
        // token starting with '/', skips the line; 0x406640 then compares
        // token 0 with the keys (case-insensitive, 0x49be20) and takes token
        // 2 as the value, whatever token 1 is. Delta Force 2 reads df2.cfg
        // the same way (GOG Df2.exe 0x46adb0 / 0x46ae90).
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> tok;
        bool quoted = false, start = true;
        for (char c : line) {
            if (!quoted && (c == ' ' || c == ',' || c == '\t')) { start = true; continue; }
            if (c == '"') {
                if (quoted) { quoted = false; start = true; }
                else { quoted = true; start = true; }
                continue;
            }
            if (start) {
                if (tok.size() >= 30) break;
                tok.emplace_back();
            }
            start = false;
            tok.back() += c;
        }
        if (tok.empty() || tok[0][0] == '/') continue;
        // INFERRED: with fewer than three tokens (key = "") DF.EXE reads a
        // stale token-2 pointer into the line, an empty string in practice;
        // OpenDF ignores the line.
        if (tok.size() < 3) continue;
        Entry* e = find(tok[0]);
        if (!e) continue;   // unknown keys are ignored, as 0x406640 does
        if (e->text) e->str = tok[2];
        else e->value = std::atoi(tok[2].c_str());   // atoi (0x49afa0)
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
        if (!e.label.empty()) std::snprintf(line, sizeof line, "%s = \"%s\"\n", e.label.c_str(), e.str.c_str());
        else if (e.text) std::snprintf(line, sizeof line, "%-16s = \"%s\"\n", e.key.c_str(), e.str.c_str());
        else std::snprintf(line, sizeof line, "%-16s = %i\n", e.key.c_str(), e.value);
        f << line;
    }
    f << "\n";
    return bool(f);
}

} // namespace df
