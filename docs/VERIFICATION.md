# What is verified against the original

OpenDF aims to do things the way the original does. This list tracks every
value or behaviour and where it comes from. **Original**: read from the game
(DF.EXE, its data files, the manual). **Footage**: measured from a recording
of the original. **Inferred**: deduced from data, plausible but unconfirmed.
**Approximation**: OpenDF's own choice, to be replaced.

Values that live in DF.EXE are read from the user's own copy at start-up
(`DFSETUP/DF.EXE`); OpenDF keeps built-in copies of them as a fallback for
when DF.EXE is missing or a different version.

## Weapons

| Item | Status | Source |
|---|---|---|
| Names and fire modes | Original | `DFGAME.BIN` strings, read at run time |
| First-person model, offset and camera | Original | DF.EXE player weapon table `+0x64`, `+0x78`; matches footage |
| Magazine size | Original | gun table `+0x08` (M4 30, M40 5, MP5 SD 30, SAW 200, .45 7) |
| Starting magazines | Original + footage | gun table `+0x04` = n; player gets 2n−1 spares (M4 23, as in the footage). The doubling is gated by a soldier-profile flag (`+0x688`) whose meaning is unknown |
| Muzzle velocity | Original (units inferred: m/s) | gun table `+0x0C` (M4 1000, M40 950, MP5 SD 295, SAW 1000, .45 295) |
| Scope power | Inferred | player table `+0xA0` (M4 4, M40 8, 0 = no scope) |
| Fire rate | Original + footage | player table `+0x9C`: ticks between rounds at 60 ticks/s (M4 single 10, burst 6, M40 70, MP5 SD 10/6/6, SAW 6, .45 10); bursts of 3 about 0.1 s apart in footage |
| Reload time | Original + footage | fixed 0x78 = 120 ticks (2 s) in DF.EXE; matches footage |
| Weapon text red while reloading | Footage | |
| Spread, damage, recoil | Approximation | candidate fields: player table `+0xA4`; gun table `+0x10..+0x1C` — not yet traced in code |
| Sounds | Original (not played yet) | gun table `+0x24..+0x30` sound ids |
| Loadout | Approximation | the original starts with the knife in hand and the weapons chosen on Change Gear; OpenDF gives all five |

## Player and camera

| Item | Status | Source |
|---|---|---|
| Field of view (90° horizontal on the 640×362 view) | Original-equivalent | reproduces footage exactly with the engine's gun offsets |
| Movement speeds | Original + footage | the soldier's `.ANM` move table (`PLAYANIM.ANM`, via `anim_def` in ITEMS.DEF), read at run time: velocity in m/s per move, eight directions, one- or two-handed weapon. Running forward with a rifle 5.0 m/s (footage: ~5 m/s), pistol 6.0; walking (Shift) 1.5/1.0; crawling 1.0 forward and back, 0.5 sideways |
| Crouched movement | Inferred | the original has no crouched moves (DF.EXE's move names: standing, walking, running, crawling, swimming only), so OpenDF stands the soldier up to move. To check against footage |
| Walking moves 5 and 7 | Unclear | `PLAYANIM.ANM`'s comments for walking disagree with those for running about which of 5/7 is right-forward; OpenDF uses the running order |
| Moving while shooting (`*_S_*` moves) | Not yet used | different speeds while firing |
| Eye heights, jump, step height, slope limit, radius | Approximation | |
| Weapon bob, recoil kick, reload lowering | Approximation | |

## World

| Item | Status | Source |
|---|---|---|
| 1 m per terrain pixel, map wraps every 1024 | Original | DF.EXE terrain sampling (0x432F94): cell = (x & 0x3FF) + ((y & 0x3FF0000) >> 6) on 16.16 coordinates; also building pads, bridges, footage waypoint distances |
| Mission y = north | Original data | building pads, bridges |
| No north/south mirroring | Footage | direction from start to camp matches the original's command map |
| Height per elevation step: 0.25 m | Original | DF.EXE: `elevation_byte << 14` in 16.16 metres (0x432F98); TRN `water_height` uses the same units (`<< 14`) |
| TRN `sky_height`, `sun_slope` | Units unknown | stored unscaled by the TRN parser (0x42FD39..) |
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
| Grid square names and map grid | Original + footage | DF.EXE 0x43DCE0: column = ⌊(x + 1000) / 100⌋ as A..Z, AA.., row = ⌊(1000 − y) / 100⌋ + 1, in mission coordinates (not the 1024 m terrain tile). Start of Insurrection is I12, as in footage; the command map's grid lines up with the terrain as in footage |
| Command map zoom | Footage | 0.75 px per metre at 640×480 |
| Waypoint box and route | Missing | the player's route is among the BMS waypoint markers (item 6005), linked by the mission data; to come with mission scripts. `.JIM` files are not used by DF.EXE (no reference to them; named per terrain, with route names that differ from the game's) |

## Not yet implemented, so not checkable

Death animations (`.ANM`/`.CHR`), bullet impact effects (`EXP_B.3DI` via
`ITEMS.DEF` effect items), knife/grenades/M203/satchels, player health, AI,
mission triggers and goals, menus.
