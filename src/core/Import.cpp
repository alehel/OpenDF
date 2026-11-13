#include "Import.h"

#include "Pff.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <vector>

namespace fs = std::filesystem;

namespace df {

namespace {

constexpr uint64_t kSector = 2048;  // ISO9660 logical sector (user data)

uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string mb(uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f MB", double(bytes) / (1024.0 * 1024.0));
    return buf;
}

// One file on the source, with the size its directory claims.
struct SrcFile {
    std::string rel;      // path relative to the CD root, '/'-separated
    uint64_t listed = 0;  // size from the directory (may be a lie)
    uint64_t cap = 0;     // hard upper bound on the real size (image: up to the next file)
    uint64_t lba = 0;     // image only: first sector
};

// Reads bytes [offset, offset+n) of a source file. Returns the number read
// (short at end of data or on a read error).
using ReadFn = std::function<size_t(uint64_t offset, uint8_t* out, size_t n)>;

// The size a file really has, judged from its own contents. Only shrinks.
// (On Delta Force 2 every fake size is the real one times ten plus a digit.)
uint64_t realSize(const SrcFile& f, const ReadFn& read) {
    uint64_t size = std::min(f.listed, f.cap);
    const std::string name = toUpper(f.rel);
    auto ends = [&](const char* ext) {
        const size_t n = std::strlen(ext);
        return name.size() >= n && name.compare(name.size() - n, n, ext) == 0;
    };
    if (ends(".PFF")) {
        uint8_t h[20];
        if (read(0, h, 20) != 20 || std::memcmp(h + 4, "PFF", 3) != 0) return size;
        const uint32_t count = u32(h + 8), recordSize = u32(h + 12), table = u32(h + 16);
        if (recordSize < 16 || count > 1000000) return size;
        std::vector<uint8_t> t(size_t(count) * recordSize);
        if (read(table, t.data(), t.size()) != t.size()) return size;
        uint64_t end = uint64_t(table) + t.size();
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* r = t.data() + size_t(i) * recordSize;
            const uint64_t e = uint64_t(u32(r + 4)) + u32(r + 8);
            if (e <= size) end = std::max(end, e);  // ignore entries pointing past the data
        }
        // The archive closes with a footer after its table: 12 bytes ending in
        // "KING" in PFF3, 4 bytes in PFF2.
        if (h[7] == '3') {
            uint8_t foot[12];
            if (read(end, foot, 12) == 12 && std::memcmp(foot + 8, "KING", 4) == 0) end += 12;
        } else {
            end += 4;
        }
        end = std::min(size, end);
        // Only trust the index over the directory when the directory is plainly
        // lying (the fake sizes are ~10x); an honest copy is left byte-for-byte.
        const bool lying = f.listed > f.cap || f.listed > 2 * end;
        return lying ? end : size;
    }
    if (ends(".BIK")) {
        uint8_t h[8];
        if (read(0, h, 8) == 8 && std::memcmp(h, "BIK", 3) == 0) return std::min(size, uint64_t(u32(h + 4)) + 8);
    }
    return size;
}

// Copies `size` bytes of a source file to `dst`. False on a short read.
bool copyOut(const ReadFn& read, uint64_t size, const fs::path& dst, std::string* why) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) { *why = "cannot write " + dst.string(); return false; }
    std::vector<uint8_t> buf(1 << 20);
    for (uint64_t at = 0; at < size;) {
        const size_t want = size_t(std::min<uint64_t>(buf.size(), size - at));
        const size_t got = read(at, buf.data(), want);
        out.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(got));
        at += got;
        if (got != want) {
            out.close();
            fs::remove(dst, ec);
            *why = "read error at byte " + std::to_string(at);
            return false;
        }
    }
    if (!out) { *why = "write failed (disk full?)"; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Disc images

// A CD image: 2048-byte "cooked" sectors (.iso), or raw 2352-byte sectors
// (.bin/.img, Mode 1 or Mode 2 Form 1) of which 2048 bytes are user data.
class Image {
public:
    bool open(const fs::path& path, std::string* error) {
        file_.open(path, std::ios::binary);
        if (!file_) { *error = "cannot open " + path.string(); return false; }
        file_.seekg(0, std::ios::end);
        const auto end = file_.tellg();
        bytes_ = end > 0 ? uint64_t(end) : 0;  // 0 for devices that can't seek to the end
        // Find the ISO9660 primary volume descriptor ("\1CD001" at sector 16).
        static const struct { uint32_t stride, offset; } kLayouts[] = {
            {2048, 0}, {2352, 16}, {2352, 24}, {2336, 8}};
        for (auto l : kLayouts) {
            stride_ = l.stride;
            offset_ = l.offset;
            uint8_t pvd[8];
            if (readSectorBytes(16, 0, pvd, 8) == 8 && pvd[0] == 1 && std::memcmp(pvd + 1, "CD001", 5) == 0) {
                uint8_t space[4];
                readSectorBytes(16, 80, space, 4);
                sectors_ = u32(space);
                if (bytes_) sectors_ = std::min<uint64_t>(sectors_ ? sectors_ : UINT64_MAX, bytes_ / stride_);
                if (!sectors_) sectors_ = UINT64_MAX / kSector;  // unknown: read until the device stops
                return true;
            }
        }
        *error = path.string() + " is not an ISO9660 CD image (or uses an unsupported format)";
        return false;
    }

    uint64_t sectors() const { return sectors_; }
    const char* layout() const {
        return stride_ == 2048 ? "2048-byte sectors" : offset_ == 16 ? "raw Mode 1 sectors" : "raw Mode 2 sectors";
    }

    // Reads user data starting `offset` bytes into sector `lba`.
    size_t read(uint64_t lba, uint64_t offset, uint8_t* out, size_t n) {
        lba += offset / kSector;
        offset %= kSector;
        if (lba >= sectors_) return 0;
        n = size_t(std::min<uint64_t>(n, (sectors_ - lba) * kSector - offset));
        if (stride_ == kSector) return readSectorBytes(lba, offset, out, n);  // contiguous: one read
        size_t done = 0;
        while (done < n) {
            const size_t chunk = size_t(std::min<uint64_t>(kSector - offset, n - done));
            const size_t got = readSectorBytes(lba, offset, out + done, chunk);
            done += got;
            if (got != chunk) break;
            ++lba;
            offset = 0;
        }
        return done;
    }

private:
    // Raw read at user-data `offset` of sector `lba` (crosses sectors only when cooked).
    size_t readSectorBytes(uint64_t lba, uint64_t offset, uint8_t* out, size_t n) {
        file_.clear();
        file_.seekg(std::streamoff(lba * stride_ + offset_ + offset));
        file_.read(reinterpret_cast<char*>(out), std::streamsize(n));
        return size_t(file_.gcount());
    }

    std::ifstream file_;
    uint64_t bytes_ = 0, sectors_ = 0;
    uint32_t stride_ = 2048, offset_ = 0;
};

// Walks the ISO9660 directory tree.
bool listImage(Image& img, std::vector<SrcFile>& files, std::string* error) {
    uint8_t pvd[2048];
    if (img.read(16, 0, pvd, sizeof pvd) != sizeof pvd) { *error = "cannot read the volume descriptor"; return false; }
    struct Dir { uint64_t lba, size; std::string path; int depth; };
    std::vector<Dir> todo = {{u32(pvd + 156 + 2), u32(pvd + 156 + 10), "", 0}};
    while (!todo.empty()) {
        Dir d = todo.back();
        todo.pop_back();
        if (d.depth > 16 || d.size > (64u << 20)) continue;  // guard against loops / garbage
        std::vector<uint8_t> data(d.size);
        data.resize(img.read(d.lba, 0, data.data(), data.size()));
        for (size_t i = 0; i < data.size();) {
            const uint8_t len = data[i];
            if (len == 0) { i = (i / kSector + 1) * kSector; continue; }  // rest of this sector is padding
            if (len < 34 || i + len > data.size()) break;
            const uint8_t* r = data.data() + i;
            i += len;
            const uint8_t nameLen = r[32];
            if (33 + nameLen > len) continue;
            std::string name(reinterpret_cast<const char*>(r + 33), nameLen);
            if (name.size() == 1 && (name[0] == 0 || name[0] == 1)) continue;  // "." and ".."
            if (auto semi = name.find(';'); semi != std::string::npos) name.resize(semi);
            if (!name.empty() && name.back() == '.') name.pop_back();
            if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
                name == "." || name == "..")
                continue;
            const std::string path = d.path.empty() ? name : d.path + "/" + name;
            if (r[25] & 2) todo.push_back({u32(r + 2), u32(r + 10), path, d.depth + 1});
            else files.push_back({path, u32(r + 10), 0, u32(r + 2)});
        }
    }
    // A file can't extend into the next file (or past the end of the disc).
    std::vector<uint64_t> starts;
    for (auto& f : files) starts.push_back(f.lba);
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    for (auto& f : files) {
        auto next = std::upper_bound(starts.begin(), starts.end(), f.lba);
        const uint64_t limit = next != starts.end() ? *next : img.sectors();
        f.cap = limit > f.lba ? (limit - f.lba) * kSector : 0;
        if (f.listed == 0) f.cap = 0;
    }
    std::sort(files.begin(), files.end(), [](const SrcFile& a, const SrcFile& b) { return a.rel < b.rel; });
    return true;
}

// A .cue sheet names the .bin holding the data track.
fs::path cueTarget(const fs::path& cue, std::string* error) {
    std::ifstream in(cue);
    std::string line;
    while (std::getline(in, line)) {
        const auto key = line.find_first_not_of(" \t");
        if (key == std::string::npos || lower(line.substr(key, 5)) != "file ") continue;
        std::string rest = line.substr(key + 5);
        std::string name;
        if (auto q = rest.find('"'); q != std::string::npos) name = rest.substr(q + 1, rest.find('"', q + 1) - q - 1);
        else name = rest.substr(0, rest.find_last_of(' '));
        return cue.parent_path() / name;
    }
    *error = "no FILE line in " + cue.string();
    return {};
}

// ---------------------------------------------------------------------------
// Folders (a mounted disc, a drive letter, or a copy)

void listFolder(const fs::path& root, std::vector<SrcFile>& files) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (!name.empty() && name[0] == '.') {  // .DS_Store, .Trashes, .fseventsd, ...
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        SrcFile f;
        f.rel = fs::relative(it->path(), root, ec).generic_string();
        f.listed = it->file_size(ec);
        f.cap = f.listed;
        files.push_back(f);
    }
    std::sort(files.begin(), files.end(), [](const SrcFile& a, const SrcFile& b) { return a.rel < b.rel; });
}

ReadFn folderReader(const fs::path& path) {
    auto in = std::make_shared<std::ifstream>(path, std::ios::binary);
    return [in](uint64_t offset, uint8_t* out, size_t n) -> size_t {
        if (!in->is_open()) return 0;
        in->clear();
        in->seekg(std::streamoff(offset));
        in->read(reinterpret_cast<char*>(out), std::streamsize(n));
        return size_t(in->gcount());
    };
}

// ---------------------------------------------------------------------------

struct Source {
    std::unique_ptr<Image> image;  // null for a folder
    fs::path folder;
    std::vector<SrcFile> files;

    ReadFn reader(const SrcFile& f) {
        if (!image) return folderReader(folder / fs::path(f.rel));
        Image* img = image.get();
        const uint64_t lba = f.lba;
        return [img, lba](uint64_t offset, uint8_t* out, size_t n) { return img->read(lba, offset, out, n); };
    }
};

bool openSource(const fs::path& path, Source& src, std::string* error) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        src.folder = path;
        listFolder(path, src.files);
        if (src.files.empty()) { *error = "no files in " + path.string(); return false; }
        return true;
    }
    if (!fs::exists(path, ec)) { *error = path.string() + " does not exist"; return false; }
    fs::path imagePath = path;
    if (lower(path.extension().string()) == ".cue") {
        imagePath = cueTarget(path, error);
        if (imagePath.empty()) return false;
    }
    src.image = std::make_unique<Image>();
    if (!src.image->open(imagePath, error)) return false;
    if (!listImage(*src.image, src.files, error)) return false;
    if (src.files.empty()) { *error = "the image has no files"; return false; }
    return true;
}

std::string detect(const std::vector<SrcFile>& files) {
    bool df1 = false, df2 = false;
    for (auto& f : files) {
        const std::string n = toUpper(f.rel);
        df1 |= n == "DFSETUP/DF.EXE" || n == "DFSETUP/DF.PFF";
        df2 |= n == "DF2SETUP/DF2.EXE" || n == "DF2SETUP/DF2.PFF";
    }
    return df2 ? "DF2" : df1 ? "DeltaForce" : "";
}

} // namespace

std::string detectGame(const fs::path& source) {
    Source src;
    std::string err;
    return openSource(source, src, &err) ? detect(src.files) : "";
}

int importGame(const fs::path& source, const fs::path& dest, std::string* error) {
    Source src;
    if (!openSource(source, src, error)) return -1;

    uint64_t listedTotal = 0;
    for (auto& f : src.files) listedTotal += f.listed;
    std::string game = detect(src.files);
    if (game.empty()) game = "game files";
    std::printf("Importing %s\n  from %s (%s, %zu files, %s listed)\n  to   %s\n", game.c_str(),
                source.string().c_str(), src.image ? src.image->layout() : "folder", src.files.size(),
                mb(listedTotal).c_str(), dest.string().c_str());

    int failed = 0, trimmed = 0;
    uint64_t total = 0;
    for (auto& f : src.files) {
        const ReadFn read = src.reader(f);
        const uint64_t size = realSize(f, read);
        if (size < f.listed) {
            ++trimmed;
            std::printf("  %-28s %10s  (disc lists %s)\n", f.rel.c_str(), mb(size).c_str(), mb(f.listed).c_str());
        } else if (size >= (8u << 20)) {
            std::printf("  %-28s %10s\n", f.rel.c_str(), mb(size).c_str());
        }
        std::fflush(stdout);
        std::string why;
        if (!copyOut(read, size, dest / fs::path(f.rel), &why)) {
            ++failed;
            std::printf("  FAILED %s: %s\n", f.rel.c_str(), why.c_str());
            continue;
        }
        total += size;
    }
    std::printf("Copied %zu files, %s", src.files.size() - size_t(failed), mb(total).c_str());
    if (trimmed) std::printf("; %d cut back to their real size", trimmed);
    std::printf(".\n");
    if (failed)
        std::printf("%d file(s) could not be read. The disc may be scratched; if it is a mounted disc image,\n"
                    "import the image file itself instead (macOS sometimes mounts only part of an image).\n",
                    failed);
    return failed;
}

} // namespace df
