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
| Layout, art, font, colours | Original | DF.EXE HUD code (virtual 1024x768 layout, RGB colour table), shown as the original at 800x600 (`POS800`, `WEAP800`, `F_SML800`, as in the footage); item by item in docs/AUDIT.md |
| "Carrying: Nothing", "Ammo reloaded", mission-end texts | Original | DF.EXE strings |
| Mission names ("Insurrection") | Original | `DFCAMPcc.BIN` entry m−1, read at run time |
| Radio messages ("Charlie 2 is down.", opening orders) | Original | mission events fire dialogs `dlg%03i`; clips and subtitles from `DFDLGnn.DBF` + `DFDLGnn.BIN`. Insurrection's opening "Proceed southwest and neutralize all hostiles." appears as in footage. Shown as text at once; the original plays the clips (audio) in sequence, subtitles with their clip |
| Triggered text | Original | campaign "Triggered Text" `ID%03i` (no shipped campaign uses it) |
| Mission end panel | Original | texts and success rule from DF.EXE (0x483094); events decide the winner |
| Goals screen (G) | Original text, own layout | "Win Conditions:" `M%02iSUBGOAL%02i`, "Lose Conditions:" `M%02iSUBFAIL%02i` from the campaign "Goals" section, met goals highlighted; the original's screen layout not yet compared |
| Grid square names and map grid | Original + footage | DF.EXE 0x43DCE0: column = ⌊(x + 1000) / 100⌋ as A..Z, AA.., row = ⌊(1000 − y) / 100⌋ + 1, in mission coordinates (not the 1024 m terrain tile). Start of Insurrection is I12, as in footage; the command map's grid lines up with the terrain as in footage |
| Command map zoom | Footage | 0.75 px per metre at 640×480 |
| Waypoint route, names, box, compass dot, map route | Original + footage | route = first BMS path with flag 2 (DF.EXE 0x447856); names = campaign text "Waypoint Names"/`WPN%03i` with the numbers at BMS header +0xAC; arrival radius = waypoint record +0x1C; advance rule 0x449a39; box format and distance 0x4838ac. Insurrection gives IP, CP ALPHA, CP BRAVO, OBJ BREEZE, CP CHARLIE, EP; route shape matches the footage's command map. `.JIM` files are not used by DF.EXE |
| Distance at the start of Insurrection | **Open** | OpenDF shows "WP1: IP (29m)", footage "22m". DF.EXE picks the first player-start marker in single player (0x442c47), which is what OpenDF does; another start marker (id 381) would give 21 m |
| Map route markers | Original + footage | circles at first/last point, diamonds between; the current waypoint's marker blinks, shown while tick bit 4 is set (16 ticks on, 16 off; DF.EXE 0x43d975, counter 0xc70acc incremented per tick); footage ≈ 0.55 s cycle |
| Map player marker | Footage | blue circle with heading line, not blinking |
| "Next waypoint" key | Approximation | the original's default key is not identified yet; OpenDF uses Y |

## Mission events

| Item | Status | Source |
|---|---|---|
| Event engine: schedule (every 128 ticks, one eighth per 16 ticks), condition chaining, NOT, delays with re-check, repeats | Original | DF.EXE 0x445914, 0x45c534, 0x45c48c |
| Conditions: team all dead / alive / losses / at least n alive / none lost, team inside area, event fired, campaign flags, load toggle, squad option | Original | 0x45c17c; areas from Area Trigger markers by tag (0x45c9a0) |
| Conditions needing AI: team spotted / engaging / alerted / reached waypoint / carrying | **Not yet** | always false until AI exists |
| Actions: kill team, remove team, campaign flag, text, radio, end mission (win/lose/none), goals met, lose conditions | Original | 0x45c368 |
| Actions needing AI: follow path, AI properties, team speed | **Not yet** | ignored until AI exists |
| Team membership and counts | Original | team = record byte +0x48, side = +0x43; counts over players, people and moving objects (0x4409f4) |
| Player death | **Not yet** | the player cannot be hurt yet, so "Bravo 1 is down" and failures from it cannot happen |

## Not yet implemented, so not checkable

Death animations (`.ANM`/`.CHR`), bullet impact effects (`EXP_B.3DI` via
`ITEMS.DEF` effect items), knife/grenades/M203/satchels, player health, AI,
mission triggers and goals, menus.
