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
    };
    Entry* find(const std::string& key);
    const Entry* find(const std::string& key) const;
    std::vector<Entry> entries_;
};

} // namespace df
