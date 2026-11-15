// df.cfg: DF.EXE's settings file. Defaults as 0x40dab8, read line by line
// as "key = value" (lines starting with '/' skipped; 0x454e10, 0x40d1e4),
// written at program exit in DF.EXE's own layout (0x40dc88).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace df {

class DfConfig {
public:
    DfConfig();
    // Delta Force 2's df2.cfg: DF2.EXE's keys, defaults (0x436ab0) and
    // write order (0x436cd0).
    // gog: GOG Df2.exe's extra keys (0x43bae0 / 0x43bd10).
    static DfConfig df2(bool gog);
    bool load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
    int get(const std::string& key) const;
    void set(const std::string& key, int value);

private:
    struct Entry {
        std::string section, key;   // section: the "// MUSIC" heading it is written under
        bool text = false;
        int value = 0;
        std::string str;
        std::string loadKey;        // the name load() reads it by, when not key
    };
    Entry* find(const std::string& key);
    Entry* findLoaded(const std::string& key);
    const Entry* find(const std::string& key) const;
    std::vector<Entry> entries_;
};

} // namespace df
