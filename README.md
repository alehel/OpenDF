# OpenDF

A cross-platform (Windows / macOS / Linux) reimplementation of the engine of
NovaLogic's **Delta Force** (1998). It contains no game data. It reads the files
from your own copy of the CD.

![Jungle terrain DFG1 in the OpenDF viewer](docs/screenshot-jungle.png)

## Status

This project is in its early stages and **not yet playable as a game**. What works today:

| Area | State |
|---|---|
| PFF3 archives (`DF.PFF`, `DFBASE.PFF`) | ✅ read |
| PFF2 archives (`MISSIONS.PFF`, `AUDIO.PFF`, `VIEWS.PFF`), encrypted | ✅ decrypted on the fly |
| Extract / list tool | ✅ |
| PCX images and palettes | ✅ |
| `.TRN` terrain descriptors | ✅ |
| Terrain rendering: colour and elevation maps, detail tiles, sky, clouds, water, haze, endless wrap-around | ✅ (scale approximated) |
| Free-fly and walk camera with ground collision | ✅ |
| 3DO / 3DI models, `.AI` object descriptors, `.JIM` waypoints, `.BMS` missions | ⏳ next |
| Weapons, AI soldiers, mission scripting, HUD, menus | ⏳ |
| Audio (WAV/MIDI in `AUDIO.PFF`, `DFMUSIC.SBF`), `.KDV` video | ⏳ |
| NovaWorld multiplayer | ❌ out of scope for now |

See [docs/ROADMAP.md](docs/ROADMAP.md) for the plan and
[docs/FORMATS.md](docs/FORMATS.md) for what is known about the file formats.

## Building

Requirements: a C++17 compiler, CMake ≥ 3.16, an OpenGL 3.3 capable GPU.
SDL2 is used if installed; otherwise CMake downloads and builds it.

**macOS**
```bash
brew install cmake sdl2
```
```bash
cmake -S . -B build && cmake --build build -j
```

**Linux (Debian/Ubuntu)**
```bash
sudo apt install build-essential cmake libsdl2-dev
```
```bash
cmake -S . -B build && cmake --build build -j
```

**Windows (Visual Studio 2019+)**
```bash
cmake -S . -B build
```
```bash
cmake --build build --config Release
```
The executable is `build\Release\opendf.exe`.

## Running

Point `--data` at your CD copy (the folder containing `DFSETUP`, or `DFSETUP` itself):

```bash
./build/opendf --data "/path/to/Delta Force (USA)"
```

If the `OpenDF` folder sits inside the CD folder, as it does here, `--data ..` works.

| Command | Purpose |
|---|---|
| `opendf [--data DIR] [--terrain DFG1] [--size 1600x900]` | terrain viewer |
| `opendf --data DIR list [.EXT]` | list every file in the archives |
| `opendf --data DIR extract OUTDIR [.EXT]` | extract files (encrypted ones decoded) |

Viewer controls: mouse to look, **WASD** to move, **Shift** to go faster,
**Space / C** for up / down, **F** to switch between walking and flying,
**Tab / Shift+Tab** for the next / previous terrain, **[ ]** for height scale,
**- =** for view distance, **Esc** to release the mouse and then quit.

## Legal

OpenDF is an independent engine. You need an original copy of Delta Force;
none of the game's assets are included in or distributed with this project.
