// The OpenDF game launcher: an original OpenDF screen (no NovaLogic game had
// one) shown when OpenDF starts without --game. It lists the game folders
// the user has added (kept in OpenDF's own settings file, never inside the
// game folders) and starts the chosen game's own front end. It is drawn with
// the art and fonts of a Delta Force game the user has added: Delta Force's
// menu template and AVA fonts, or Delta Force 2's menu frame and FRK fonts.
// docs/LAUNCHER.md.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace df {

enum class GameKind { Unknown, DeltaForce, DeltaForce2, LandWarrior, TaskForceDagger };

// Which Delta Force game a folder holds (the CD root, a copy of it, or its
// setup folder), from the archives Vfs::mountGameDir finds there.
GameKind identifyGame(const std::filesystem::path& dir);
const char* gameTitle(GameKind kind);
// Whether OpenDF can play it yet (has its front end).
bool gamePlayable(GameKind kind);

// opendf.ini in the platform's user settings folder (SDL_GetPrefPath):
//   [games]
//   game=/path/to/DeltaForce
//   [launcher]
//   last=/path/to/DeltaForce
struct LauncherSettings {
    std::vector<std::filesystem::path> games;
    std::filesystem::path last;   // the game launched last time (picks the launcher's look)

    static std::filesystem::path defaultFile();
    // False if the file does not exist (first start).
    bool load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
};

struct LauncherOptions {
    int width = 1280, height = 960;
    std::filesystem::path settingsFile;   // empty: LauncherSettings::defaultFile()
    // Testing: "add" opens the add-a-game screen, "select:N" highlights game
    // N (0-based), "launch:N" picks game N at once, "add:PATH" adds a
    // folder as the add screen would, "remove:N" takes game N off; with
    // `screenshot` set the launcher saves a screenshot after `frames` frames
    // and returns.
    std::string startScreen, screenshot;
    int frames = 1;
};

// Runs the launcher until a game is chosen (returns true, `chosen` = its
// folder) or the user quits (false).
bool runLauncher(const LauncherOptions& options, std::filesystem::path* chosen);

} // namespace df
