// PFF archive reader (NovaLogic "PFF2" / "PFF3" resource packs) and a simple
// virtual file system that layers several archives on top of each other.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace df {

struct PffEntry {
    std::string name;      // upper-case, e.g. "DFG1_C.PCX"
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t timestamp = 0;
    bool encrypted = false;   // Delta Force 2 rules: entry byte 2 bit 0
};

// PFF2 archives (MISSIONS/AUDIO/VIEWS.PFF on the CD) store every entry XOR'd
// with a rotating key. PACK.EXE decodes them at install time; we do it on read.
void pffDecrypt(uint8_t* data, size_t size);

// Delta Force 2's own reader (GOG Df2.exe 0x47d720, CD 0x472880) decrypts
// single entries instead (0x47d9f0, key 0x312a4ce).
void pffDecryptDf2(uint8_t* data, size_t size);

class PffArchive {
public:
    // df2Rules: read the archive as Df2.exe does (its DF2.PFF / TERRAINS.PFF),
    // otherwise as PACK.EXE and DF.EXE do.
    bool open(const std::filesystem::path& path, std::string* error, bool df2Rules = false);

    const std::vector<PffEntry>& entries() const { return entries_; }
    const std::filesystem::path& path() const { return path_; }
    bool encrypted() const { return encrypted_; }

    bool read(const PffEntry& entry, std::vector<uint8_t>& out);

private:
    std::filesystem::path path_;
    std::ifstream file_;
    std::vector<PffEntry> entries_;
    bool encrypted_ = false;
};

class Vfs {
public:
    // Mounts every known archive found in `gameDir` (or its DFSETUP sub-folder).
    // Later mounts override earlier ones on name collisions, except for
    // Delta Force (DFBASE.PFF found), where the first archive and entry win.
    bool mountGameDir(const std::filesystem::path& gameDir, std::string* error);
    bool mount(const std::filesystem::path& archive, std::string* error, bool df2Rules = false);

    bool exists(const std::string& name) const;
    bool read(const std::string& name, std::vector<uint8_t>& out);
    // A file on disk read under `name` before the archives (Delta Force 2's
    // own missions saved by Df2med.exe in the game folder, listed by the
    // Quick Missions page); list() names it too.
    void addLoose(const std::string& name, const std::filesystem::path& file);

    // All file names with the given extension (e.g. ".TRN"), sorted.
    std::vector<std::string> list(const std::string& extension = "") const;
    const std::vector<std::unique_ptr<PffArchive>>& archives() const { return archives_; }

private:
    struct Ref { PffArchive* archive; size_t index; };
    std::vector<std::unique_ptr<PffArchive>> archives_;
    std::unordered_map<std::string, Ref> index_;
    bool firstWins_ = false;   // DF.EXE's lookup order (GOG 0x48fd00)
    std::unordered_map<std::string, std::filesystem::path> loose_;
};

std::string toUpper(std::string s);

} // namespace df
