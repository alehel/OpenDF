// Copies a game's files off its CD (a mounted disc or folder, a disc image, or
// a raw disc device) into a games/ folder.
//
// NovaLogic's CDs list some files at about ten times their real size as copy
// protection (Delta Force 2: TERRAINS.PFF is listed at 3.2 GB and is 319 MB),
// so a plain copy either fails at the end of the disc or drags in gigabytes of
// whatever follows. The importer cuts every file back to its real size:
//   - PFF archives end where their own entry table and last entry end,
//   - Bink videos (.BIK) carry their size in their header,
//   - in a disc image, nothing can run past the start of the next file.
#pragma once

#include <filesystem>
#include <string>

namespace df {

// Which supported game a CD (folder or image root) holds: "DeltaForce", "DF2"
// or "" if unknown. Used to pick the default destination folder name.
std::string detectGame(const std::filesystem::path& source);

// Copies `source` into `dest`. `source` is either a directory (mounted disc,
// drive letter, or an existing copy) or an image: .iso, raw .bin/.img (2352-byte
// sectors), a .cue (its data track's .bin is read), or a raw disc device.
// Prints progress to stdout; returns the number of files that could not be
// copied (0 = success), or -1 if the source could not be read at all.
int importGame(const std::filesystem::path& source, const std::filesystem::path& dest, std::string* error);

} // namespace df
