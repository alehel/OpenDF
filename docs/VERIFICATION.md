# What is verified against the original

OpenDF aims to do things the way the original does. This list tracks every
value or behaviour and where it comes from. **Original**: read from the game
(DF.EXE, its data files, the manual). **Footage**: measured from a recording
of the original. **Inferred**: deduced from data, plausible but unconfirmed.
**Approximation**: OpenDF's own choice, to be replaced.

## Weapons

| Item | Status | Source |
|---|---|---|
| Names and fire modes | Original | `DFGAME.BIN` strings, read at run time |
| First-person model, offset and camera | Original | DF.EXE player weapon table `+0x64`, `+0x78`; matches footage |
| Magazine size | Original | gun table `+0x08` (M4 30, M40 5, MP5 SD 30, SAW 200, .45 7) |
| Starting magazines | Original + footage | gun table `+0x04` = n; player gets 2n−1 spares (M4 23, as in the footage). The doubling is gated by a soldier-profile flag (`+0x688`) whose meaning is unknown |
| Muzzle velocity | Original (units inferred: m/s) | gun table `+0x0C` (M4 1000, M40 950, MP5 SD 295, SAW 1000, .45 295) |
| Scope power | Inferred | player table `+0xA0` (M4 4, M40 8, 0 = no scope) |
| Fire rate, spread, damage, reload time, recoil, burst length | Approximation | candidate fields: player table `+0x9C`, `+0xA4`; gun table `+0x10..+0x1C` — not yet traced in code |
| Sounds | Original (not played yet) | gun table `+0x24..+0x30` sound ids |
| Loadout | Approximation | the original starts with the knife in hand and the weapons chosen on Change Gear; OpenDF gives all five |

## Player and camera

| Item | Status | Source |
|---|---|---|
| Field of view (90° horizontal on the 640×362 view) | Original-equivalent | reproduces footage exactly with the engine's gun offsets |
| Running speed ≈ 5 m/s | Footage | waypoint distance falling ~5 m per second while running |
| Walk speed, crouch/prone speeds, eye heights, jump, step height, slope limit, radius | Approximation | |
| Weapon bob, recoil kick, reload lowering | Approximation | |

## World

| Item | Status | Source |
|---|---|---|
| 1 m per terrain pixel, mission y = north | Original data | building pads, bridges, barbed wire; waypoint distances in footage agree |
| No north/south mirroring | Footage | direction from start to camp matches the original's command map |
| Height per elevation step (0.25 m) | Approximation | |
| Terrain lighting, detail textures, sky, clouds, haze, water | Approximation | |
| Model material flags | Inferred | |
| LOD distances, model lighting | Approximation | |

## HUD and text

| Item | Status | Source |
|---|---|---|
| Layout, art, font | Original + footage | `POS640`, `WEAP640`, `F_SML640`; positions measured from footage |
| "Carrying: Nothing", "Ammo reloaded", mission-end texts | Original | DF.EXE strings |
| Mission names ("Insurrection") | Original | `DFCAMPcc.BIN` entry m−1, read at run time |
| Radio messages ("Charlie 2 is down.", opening orders) | Original text known, not shown yet | `DFDLGcc.BIN`; triggered by mission scripts (not decoded) |
| Grid square names | **Wrong** | the original's grid is offset from the terrain origin (start of Insurrection is I12 in the original, N2 in OpenDF); linked to the `.JIM` waypoint coordinates |
| Waypoint box | Missing | needs `.JIM` |

## Not yet implemented, so not checkable

Death animations (`.ANM`/`.CHR`), bullet impact effects (`EXP_B.3DI` via
`ITEMS.DEF` effect items), knife/grenades/M203/satchels, player health, AI,
mission triggers and goals, menus.
