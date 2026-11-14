#include "Pff.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace fs = std::filesystem;

namespace df {

std::string toUpper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

static uint32_t readU32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

void pffDecrypt(uint8_t* data, size_t size) {
    // Recovered from PACK.EXE: key starts at 0x01E177CE and is rotated left by
    // 7 bits before each byte; the low byte is XOR'd into the data. Because
    // rol-7 on 32 bits has period 32, the keystream repeats every 32 bytes.
    static uint8_t stream[32];
    static bool ready = false;
    if (!ready) {
        uint32_t k = 0x01E177CEu;
        for (auto& b : stream) {
            k = (k << 7) | (k >> 25);
            b = static_cast<uint8_t>(k);
        }
        ready = true;
    }
    for (size_t i = 0; i < size; ++i) data[i] ^= stream[i & 31];
}

bool PffArchive::open(const fs::path& path, std::string* error) {
    path_ = path;
    file_.open(path, std::ios::binary);
    if (!file_) {
        if (error) *error = "cannot open " + path.string();
        return false;
    }
    uint8_t hdr[20];
    if (!file_.read(reinterpret_cast<char*>(hdr), sizeof hdr)) {
        if (error) *error = "truncated header in " + path.string();
        return false;
    }
    const uint32_t headerSize = readU32(hdr);
    const uint32_t count = readU32(hdr + 8);
    const uint32_t recordSize = readU32(hdr + 12);
    const uint32_t tableOffset = readU32(hdr + 16);
    if (std::memcmp(hdr + 4, "PFF", 3) != 0 || headerSize < 20 || recordSize < 32) {
        if (error) *error = path.string() + " is not a PFF archive";
        return false;
    }
    const char version = static_cast<char>(hdr[7]);
    encrypted_ = (version == '2');

    std::vector<uint8_t> table(size_t(count) * recordSize);
    file_.seekg(tableOffset);
    if (!file_.read(reinterpret_cast<char*>(table.data()), table.size())) {
        if (error) *error = "truncated entry table in " + path.string();
        return false;
    }
    file_.seekg(0, std::ios::end);
    const uint64_t fileSize = static_cast<uint64_t>(file_.tellg());

    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* r = table.data() + size_t(i) * recordSize;
        // PFF3: first dword is a "deleted" flag. PFF2: a per-entry checksum.
        if (!encrypted_ && readU32(r) != 0) continue;
        PffEntry e;
        e.offset = readU32(r + 4);
        e.size = readU32(r + 8);
        e.timestamp = readU32(r + 12);
        char name[17] = {};
        std::memcpy(name, r + 16, 16);
        e.name = toUpper(name);
        if (e.name.empty() || uint64_t(e.offset) + e.size > fileSize) continue;
        entries_.push_back(std::move(e));
    }
    return true;
}

bool PffArchive::read(const PffEntry& e, std::vector<uint8_t>& out) {
    out.resize(e.size);
    file_.clear();
    file_.seekg(e.offset);
    if (!file_.read(reinterpret_cast<char*>(out.data()), e.size)) return false;
    if (encrypted_) pffDecrypt(out.data(), out.size());
    return true;
}

bool Vfs::mount(const fs::path& archive, std::string* error) {
    auto a = std::make_unique<PffArchive>();
    if (!a->open(archive, error)) return false;
    for (size_t i = 0; i < a->entries().size(); ++i) index_[a->entries()[i].name] = {a.get(), i};
    archives_.push_back(std::move(a));
    return true;
}

bool Vfs::mountGameDir(const fs::path& gameDir, std::string* error) {
    // Install order: base data first, then the CD packs that patch into it.
    // Delta Force; then the later games' archives (docs/OTHER-GAMES.md):
    // terrain packs, Delta Force 2's DF2.PFF, Land Warrior's DFLW.PFF and
    // Task Force Dagger's SMALL.PFF.
    static const char* kArchives[] = {"DFBASE.PFF", "DF.PFF", "MISSIONS.PFF", "AUDIO.PFF", "VIEWS.PFF",
                                      "TERRAINS.PFF", "DF2.PFF", "DFLW.PFF", "SMALL.PFF"};

    // The game's setup folder: DFSETUP, DF2SETUP, DFLSETUP or TFDSETUP (any case).
    std::vector<fs::path> roots = {gameDir};
    {
        std::error_code ec;
        if (fs::is_directory(gameDir, ec))
            for (auto& de : fs::directory_iterator(gameDir, ec)) {
                const std::string n = toUpper(de.path().filename().string());
                if (de.is_directory(ec) && (n == "DFSETUP" || n == "DF2SETUP" || n == "DFLSETUP" || n == "TFDSETUP")) roots.push_back(de.path());
            }
    }
    // Case-insensitive lookup so a lower-cased copy of the CD works on Linux.
    auto findIn = [](const fs::path& dir, const std::string& want) -> fs::path {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return {};
        for (auto& de : fs::directory_iterator(dir, ec))
            if (toUpper(de.path().filename().string()) == want) return de.path();
        return {};
    };

    int mounted = 0;
    for (const char* name : kArchives) {
        for (auto& root : roots) {
            fs::path p = findIn(root, name);
            if (p.empty()) continue;
            std::string err;
            if (mount(p, &err)) ++mounted;
            else if (error) *error += err + "\n";
            break;
        }
    }
    if (mounted == 0 && error) *error += "no PFF archives found under " + gameDir.string();
    return exists("DF.PFF") || mounted > 0;
}

bool Vfs::exists(const std::string& name) const { return index_.count(toUpper(name)) != 0; }

bool Vfs::read(const std::string& name, std::vector<uint8_t>& out) {
    auto it = index_.find(toUpper(name));
    if (it == index_.end()) return false;
    return it->second.archive->read(it->second.archive->entries()[it->second.index], out);
}

std::vector<std::string> Vfs::list(const std::string& extension) const {
    std::vector<std::string> names;
    const std::string ext = toUpper(extension);
    for (auto& [name, ref] : index_) {
        if (ext.empty() || (name.size() >= ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0))
            names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace df
