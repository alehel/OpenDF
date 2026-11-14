# The other NovaLogic games in `games/`

A first survey (2026-11-14) of what supporting the later Delta Force games
would involve, and a separate note on Comanche. Everything here was read from
the files on the discs; nothing is implemented yet. Plan: Delta Force 2, Land
Warrior and Task Force Dagger in OpenDF; Comanche as a separate application
(research only here).

## Shared NovaLogic files

- `MISSIONS.PFF` (19 371 222 bytes) and `AUDIO.PFF` are **byte-identical** on
  the Delta Force, Delta Force 2 and Comanche Gold discs (md5
  e897f1bb...). `MISSIONS.PFF` is the legacy model library (1729 `.3DO`, 431
  `.AI`, 85 `.JIM`); `AUDIO.PFF` holds 987 WAVs and 100 MIDIs. Land Warrior
  ships the same `MISSIONS.PFF`. They are NovaLogic's common library, not
  per-game data.
- All games use the PFF container: `PFF3` (plain, first entry dword = deleted
  flag) or `PFF2` (encrypted, first dword a checksum). OpenDF already reads
  both.

## Delta Force 2 (1999) — `games/DF2/DF2SETUP`

| Archive | Contents |
|---|---|
| `DF2.PFF` (PFF3, 32-byte records) | 419 WAV, 203 PCX, 89 TGA, 85 BMS, 24 JPG, 14 BIN, 12 ANM, 5 PWF, 4 DBF, 3 DEF, 2 KSA, MNU, SCR |
| `TERRAINS.PFF` (PFF3) | 621 3DI, 67 PCX, 50 TGA, 19 CAL, 18 TRN, 18 JPG, DEF |
| `MISSIONS.PFF`, `AUDIO.PFF` | the shared library (above) |
| `DF2MUSIC.SBF` | music bank (DF1's `DFMUSIC.SBF` format family) |
| `DF2.EXE` (835 584 bytes), `DF2MED.EXE` | the game and the mission editor |

First differences from DF1:
- 3DI header version **8** (DF1: 5); BMS version **0x0F** (DF1: 0x0D); TRN is
  the same text format (`terrain_name ...`).
- The `.DEF` files (`ITEMS.DEF`, `HUD.DEF`, `LOOP.DEF`) are no longer plain
  text: they start with `SCR` followed by scrambled bytes. A decoder must be
  found in `DF2.EXE`.
- New file types: `TGA` textures, `CAL` (terrain, 19 files), `KSA`
  (`PLAYER01.KSA`, probably the character animation replacing `.CHR`).
- The game data directory is `DF2SETUP/`; OpenDF only looks in `DFSETUP/`
  today (`Vfs::mountGameDir`, `findGameExe`).

## Land Warrior (2000) — `games/DFLW/DFLSETUP`

| Archive | Contents |
|---|---|
| `DFLW.PFF` (PFF3, **36-byte records**: one extra dword, apparently a hash) | 500 WAV, 440 PCX, 186 TGA, 71 BMS, 53 SAF, 28 JPG, 16 BIN, 12 ANM, 7 PWF, 7 3DI, 5 DBF, 4 DEF (incl. `CHARATTR.DEF`), 2 MNU, KSA, ACA, SCC, ICS |
| `TERRAINS.PFF` (PFF3, 36-byte records) | 4053 PCX, 966 3DI, 295 TGA, 35 TRN, 25 ENV |
| `DFLWMUS.SBF`, `DFLW.EXE` (1.9 MB), `DFLWLC.EXE`, `DFLWMEDC.EXE` | music, game, launcher, editor |

- 3DI version 8, BMS 0x0F. New: `SAF` (53 files), `ENV`, `ACA`, `SCC`, `ICS`.
- OpenDF's PFF reader already accepts the 36-byte records (it reads the first
  32 bytes of each and skips the rest).

## Task Force Dagger (2002) — `games/DFTFD/TFDSETUP`

| Archive | Contents |
|---|---|
| `SMALL.PFF` (PFF3, 36-byte records) | 253 TGA, 189 PCX, 93 WAV, 69 BMS, 53 SAF, 12 BIN, 12 ANM, 4 DEF, 3 PWF, … |
| `TERRAINS.PFF` (PFF3, 36-byte records) | 4273 PCX, 1487 TGA, 1343 3DI, 35 TRN, 25 ENV |
| `MUS.SBF`, `DFTFD.EXE` (3.3 MB), `DFTFDLC.EXE`, `DFTFDMED.EXE` | music, game (by Zombie Studios; leftover Land Warrior strings in the executable, e.g. "Delta Force Land Warrior" in an error message and `dflwmusic`, point to Land Warrior's code base), launcher, editor |

- 3DI version **10**; BMS 0x0F; the disc has SafeDisc files (`SECDRV.SYS`,
  `DRVMGT.DLL`, `00000000.256`): the executable is likely encrypted, which
  matters for reading its tables (DF1's approach reads values out of `DF.EXE`).

## Status in OpenDF

- `opendf --game games/DF2|DFLW|DFTFD list|extract` works: the setup folders
  (`DF2SETUP`, `DFLSETUP`, `TFDSETUP`) and the later archives are mounted.
- Delta Force 2 formats now read:
  - **3DI version 8** (all 618 v8 models; the 3 version-7 files are refused by
    DF2.EXE too): 52-byte texture headers with a second name, 16-bit
    index+alpha textures (material flag 0x8000: lit like any surface and
    blended by the texel alpha; DF1's 0x8000/0x0080 blends do not apply.
    How DF2's renderer DLL draws them is INFERRED, not traced), the texture
    variant mask (3 camouflage variants), 192-byte LOD headers, collision
    planes, 80-byte volumes, 120-byte materials (DF2.EXE 0x43e570).
  - **BMS version 0x0F**: a 0x208-byte header (title/author 32 bytes, terrain
    0x44, dialogue 0x54, flags 0x78 with night 0x100000, weather 0xAC); the
    rest is DF1's layout (DF2.EXE 0x403bd0).
  - **SCR text scrambling** (ITEMS.DEF, LOOP.DEF, .ANM): reversed payload XORed
    with `s = rol(rol(s,11)+s,4) ^ 1` from 0x01234567 (DF2.EXE 0x4614f0),
    undone in `Vfs::read`.
  - Terrain colour maps as JPEG (libjpeg, optional at build time) or TGA.
- `opendf --game games/DF2 mission D03M01` (or any `DxxMyy`) loads and plays
  with DF1's rules and built-in tables, now with:
  - **soldier animations** from `PLAYER01.KSA` (DF2.EXE 0x41e5e0): 255
    animations of 88-byte frames (15 part angles, pose extents, feet height,
    root motion, event), next-animation/frame continuation, no interpolation;
    posed about the model's mesh pivots with DF2's rotation Rz(a0)Ry(a1)Rx(a2)
    (0x449df0);
  - **sounds** from `DF2.PWF` (DF1's PWF2 format) with DF2's table (0x4B85A8,
    1024 id slots), whole 16-bit WAV samples (DF2 fixed DF1's low-byte bug);
  - **terrain detail**: a 64-wide TGA strip of 64×64 tiles blended by their
    alpha, one tile a map cell; the **sky gradient** from column 0 of the TGA
    sky palette (sampled to 16 steps: APPROXIMATION); **surfaces** from the
    `char_data` .CAL (DT/GS/SW/RK/CT/SD → dirt/grass/snow/rock/sand; the
    tile's own line, not DF2.EXE's permuted table: APPROXIMATION);
  - **soldier hit spheres** (0x44b2f0): KSA has no skeleton, so one sphere
    per mesh, centred on the posed mesh pivot, radius mesh +0x64; the 1.5 m
    body pre-test and nearest-along-the-ray rule are DF1's;
  - **DF2's own HUD** (DF2.EXE 0x442940; full notes in the tracing specs):
    the 3D view is only the rows between two opaque bitmap strips
    (`DHT%iG`/`DHB%iG`, 0x45cb40); every position per display mode from
    `HUD.DEF` (0x444150); text in the HUD font in 0xff8000; the weapon,
    stance, carried-item and radar-frame images through a fixed amber ramp
    (index i → (i·255>>8, i>>1, 0)) added onto the strip (0x450c10, saturating);
    weapon icon by mode from a 22-frame sheet (0x443370); weapon name from
    `HudGuns` HG%03i centred, red when not ready, clips "\x01 n" / `NA`,
    a proportional solid rounds bar and the chamber bar (0x443110);
    waypoint lines `WP%i: name`, `HEADING: NA`, `GRID: NA`, `RANGE`,
    `ELEVATION` (0x442c30) with names per waypoint record +0x58 from
    `DF2WPN.BIN` "WPNames"; the player's name (0x441b20); the radar: the
    colour map north-up at 1.6 m a pixel at 640 (scale as the command map),
    route lines, objects by their marker shape, GPS soldiers, the player, the
    next-waypoint dot on the `map_radius` ring (0x421510, 0x4226c0,
    0x443850); the top strip: the last three of two 40-line histories
    (system and chat), each line in its own colour, no timeout (0x442170);
    the crosshair: four orange arms, gap W·320/65536, arm W·640/65536
    (0x45a9e0). Wider windows (amenity): each strip is split inside one
    panel, the halves stay at the window's sides and that panel stretches;
    HUD.DEF points follow their half. INFERRED: the weapon's ammo kind uses
    DF1's weapon type numbers; radar objects use record +0x4C as their
    shape. Not yet: the breath bar, the radar's water blend and
    nearest-texel sampling;
  - **wind**: fixed for the mission from the BMS header (+0xE8 kph, +0xEC
    degrees it blows from; DF2.EXE 0x45d3f8), shown as `Wind: SW, 3kph` /
    `Wind: None`. The `sp_wind` option (default/none/random) only applies in
    multiplayer. Not yet: its only physical effect, DF2's projectile drag
    toward the wind velocity (0x44c2f0, per-ammo drag bands; DF1 has no
    drag), and rain/snow drifting with it.
- Not yet: the new layer flag
  0x80 (random-then-sequential variants), the speed-of-sound delay for distant
  sounds (d·62/350 ticks from 30 m), the cloud layer (512-wide density PCX),
  fog keys, the new ITEMS.DEF attribute bits, DF2's menus, and everything DF1
  reads from DF.EXE (DF2.EXE has other addresses).

## Suggested order

1. Mount the later layouts: `DF2SETUP/`, `DFLSETUP/`, `TFDSETUP/`, their
   archive names (`DF2.PFF`, `TERRAINS.PFF`, `DFLW.PFF`, `SMALL.PFF`), and the
   game executable per game.
2. Delta Force 2 first (closest to DF1): the `SCR` DEF decoder, 3DI v8, BMS
   0x0F differences, TGA textures, `KSA` animation, `CAL` terrain data; then a
   mission loading in the viewer, then playing.
3. Land Warrior: on top of DF2, the new `SAF`/`ENV` formats and its executable.
4. Pause: Black Hawk Down (2003) is not in `games/` yet. The games most
   likely build on each other, so it comes before Task Force Dagger.
5. Black Hawk Down, then Task Force Dagger: 3DI v10, and check whether
   `DFTFD.EXE` can be read at all (SafeDisc).

## Comanche (separate application) — research note

- **Comanche 3** (`games/Comanche3`): data in `NOVA/`: a `RESOURCE.RES`
  archive with the magic `RESOURCE2` (not PFF) split over `RESOURCE._01.._03`,
  18 `.KDV` videos/maps (`C3.NAM`, `C3.SAV`); DOS-era installer
  (`INSTALL.EXE`, VESA).
- **Comanche Gold** (`games/ComancheGold/C3G`): PFF2 archives: `CGOLD.PFF`
  (994 WAV, 548 PCX, 204 3DO, 101 MID, 52 AI, 16 MPL, 9 MIS/POS/INF/FNT, KDA,
  SKY, MNU), `RESOURCE.PFF` (4083 PCX, the 3DO/AI/JIM library, 94 MIS/POS/INF,
  84 ORD, REC), `MAPS.PFF` and `KDV.PFF` (KDV), plus the shared `MISSIONS.PFF`
  and `AUDIO.PFF`; a mission `EDITOR.EXE` with `EDITOR.PDF`.
- Comanche's terrain is NovaLogic's voxel engine (Voxel Space; maps in `.KDV`
  / map files), not Delta Force's polygon terrain, and its models are the
  older `.3DO` format. A separate application can reuse OpenDF's PFF reader,
  PCX/WAV/MID loaders and some of the 3DO knowledge (`docs/FORMATS.md`, "3DO +
  AI"), but needs its own renderer, flight model and mission format work.
