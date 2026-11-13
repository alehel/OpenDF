# Audit: everything OpenDF draws or decides

Working list for the audit of 2026-11-13. Every item gets a source (DF.EXE,
game data, manual, footage) or is listed in VERIFICATION.md as an
approximation. Status: **ok** = checked against the original, **fixed** =
was wrong and corrected, **open** = no source found yet (listed as an
approximation), **todo** = not checked yet.

## Top bar

| # | Item | Status | Notes |
|---|---|---|---|
| T1 | Bars, borders, dividers | fixed | DF.EXE 0x404650/0x485300: bars H/8 = 60 each (was 59/60), borders at y 0/59/420/479 and both side edges, dividers x 350 / 431, all one pixel in HUD green |
| T2 | HUD colours | fixed | were eyeballed; now DF.EXE's RGB table (0x4027b1): text/outline (1,129,1), bright (1,230,1), dark (9,51,9), blue (17,60,253), red, white |
| T3 | Stance icon | fixed | POS800 1:1 (footage is 800x600), centred x 50, top 3.75 (0x484c74) |
| T4 | Weapon name | fixed | (75, 10); red while reloading or out of ammunition (0x44b0a0) |
| T5 | Magazine / rounds boxes, magazine icon | fixed | boxes 75-135 / 140-200; the icon is font character 1 (was a guessed line); boxes have no fill, 2-pixel dark scanlines, HUD-green outline (0x401de4) |
| T6 | "Round chambered" bar | fixed | x 205-210, solid when chambered, empty box otherwise (0x483558); chambered = rounds > 0 and not reloading (inferred) |
| T7 | Weapon silhouette | fixed | WEAP800 1:1, right edge x 325, top 16.25 (0x484ae8) |
| T8 | Compass | fixed | (382,30) r 20; letters are font characters 2-5; inner circle r 5 + needle 10 in team colour; dot r 2 bright green (0x483d9c) |
| T9 | Grid / Carrying / WP boxes | fixed | positions from 0x4836b8 / 0x483718 / 0x483d48; "None" without a route |
| T10 | Font and size | fixed | F_SML800 at 0.8 of a 640 unit: the original at 800x600, which is what the footage shows (measured text widths ÷ font widths = 0.797) |

## Bottom bar

| # | Item | Status | Notes |
|---|---|---|---|
| B1 | Messages | fixed | x 6.25, y 430/445/460, three lines, white, 1240 ticks each (0x483390, 0x42f400); were grey, 12 s, 14-unit spacing |
| B2 | Soldier name / mission name | fixed | (437.5, 426.25 / 441.9); "OPERATIVE" was invented: the name comes from the soldier profile, `--name` until the soldier screens exist |

## 3D view

| # | Item | Status | Notes |
|---|---|---|---|
| V1 | Crosshair | fixed | bright green one-pixel arms 4..10 / 5..11 from the centre (0x401f94) |
| V2 | Bullet impacts (puffs) | open | OpenDF's puffs are invented; the original uses effect models (EXP_B.3DI via ITEMS.DEF effects) |
| V3 | Gun bob, reload lowering, recoil push, muzzle flash time | todo | |
| V4 | View distance, fog, sky, terrain tint | open | view distance 1200 m and fog 35-98 % are guesses; TRN `filter`/`gamma`/`saturation`/`horizon` are read but their use (÷128 tint) is a guess; the original renders terrain as voxels. Needs its own study (part of "terrain look") |
| V2b | Weapon bob, recoil kick, reload lowering, flash time | open | all guesses; DF.EXE has `movegun` and weapon table +0xA4 still undecoded |
| V5 | Scope inset | fixed | centre (480,240), radius 100 (was 120), ticks r/4..r/2, range "%3im" / "1km+" at (500,312.5); shown only while not moving (0x450600) |

## Command map

| # | Item | Status | Notes |
|---|---|---|---|
| M1 | Map grid and labels | fixed | bright green 1 px; labels white with black outline (0x43ddf4) |
| M2 | Route and markers | fixed | route bright green; markers from record +0x4C (type: 1 circle, 3 diamond, ...) in side colour, radius 6.25 (0x43e434) |
| M3 | Player marker (blue, steady) | fixed | footage |
| M4 | Current waypoint blink | fixed | DF.EXE 0x43d975 |
| M5 | Soldier marks | **open** | code: GPS option default 1 draws soldiers whose record +0x4C is set (all 62 in Insurrection); footage shows only dead ones (X) and one live one, so something changes the flag at run time; being traced |
| M6 | Compass rose | fixed | (560,315), arms 40, star with inner points 5; N white, E S W green, outlined (0x43dec0); was invented |
| M7 | Map scale and zoom | fixed | default scale 1 = 0.625 px/m at 640 (was 0.75), zoom ×0.9 / ×1.1 within 0.25..3.6 (0x42ddbf) |

## Panels

| # | Item | Status | Notes |
|---|---|---|---|
| P1 | Goals screen | fixed | no panel (was invented); right-aligned at x 625 from y 72.5, 15.6 apart, shadowed; headings white, goals white / green when met, lose lines white / red when hit (0x485004) |
| P2 | Mission end panel | fixed | measured from footage: box x 80-560, y 72-153, scene dimmed to ~43 %, bright green outline and text |

## Controls

| # | Item | Status | Notes |
|---|---|---|---|
| K1 | Every default key binding | fixed | DF.EXE default table at 0x4df4f8 (read at run time, built-in copy as fallback). Was wrong: Y next waypoint (original W), map zoom keys reversed, Tab/R/X/Z/WASD invented. Now the original table + mouse look (user's choice); N = debug flight only |
| K2 | Weapon keys | fixed | slots per DF.EXE 0x44b598: 2 sidearm, 3-5 primary modes, 6-8 secondary modes (1 knife, 9/0 grenade/laser not implemented). Was: invented 2/3/4/6/7/8 with all five guns carried |
| K3 | Keyboard turn/look rates (numpad) | open | 150°/s and 90°/s, not found in DF.EXE yet |

## Gameplay

| # | Item | Status | Notes |
|---|---|---|---|
| G1 | Weapon switch delay | fixed | was a guessed 0.4 s; DF.EXE 0x44aafe clears cooldown and reload, no delay; an empty weapon starts reloading |
| G2 | Reload rules | fixed | DF.EXE 0x44a004 / 0x449f58: reload discards the rounds left; the last round fired starts the reload at once; empty trigger reloads |
| G7 | Loadout | open | sidearm + one primary + one secondary as in the original; which ones comes from Change Gear (not built), `--loadout` until then (default M4, M40: a placeholder) |
| G3 | Bullet flight (gravity, 3 s life) | open | not traced in DF.EXE yet |
| G4 | People die from one hit | open | ITEMS.DEF gives people hp 2-4; damage per gun (gun table +0x14?) not traced yet |
| G5 | Zoom levels per weapon | open | scope power from player table +0xA0 (inferred); number of zoom steps not traced |
| G6 | Message lifetime | fixed | see B1 |

## Found during the audit

| # | Item | Status | Notes |
|---|---|---|---|
| X1 | Palm crowns drawn magenta inside the scope inset | open | a colour-key/transparency bug at the LOD used for magnified views |
