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
    shape. The radar picture is the graded level-0 colour map, nearest
    texel, cells at or under the water level averaged 50/50 with the water
    colour (0x421510);
  - **sky**: the mission's sky block (BMS 0xB8..0xE4, a SUNSETS.DEF entry)
    replaces the TRN's clouds, gradient, sky height, horizon, gamma,
    saturation and filter when both names are set (0x4654b6); clouds blend
    toward the cloud colour (gradient row 0) by density/256 (0x4679b0); the
    sky is not colour-graded. APPROXIMATION still: the 256-step gradient is
    sampled to 16 steps with DF1's row rate (DF2's rate uses camera field
    +0x4c, not traced);
  - **air** (0x4587f0): +1 a tick while the player's origin is 0.5 m or more
    under the water; surfacing with more than 60 resets it (AIR_SUCK 801);
    gags (802) at 2304/2496/2688; over 2880 (48 s) the player drowns at once;
    the breath bar (0x441540) while the count is not 0: half-width
    100 − c·100/2880 about x 512 (1024×768 space), cyan, red over 2240, with
    "Breath" below in the same colour. The LAR V rebreather (body slot 33)
    stops it; OpenDF has no DF2 loadout yet, so nobody wears one;
  - **wind**: fixed for the mission from the BMS header (+0xE8 kph, +0xEC
    degrees it blows from; DF2.EXE 0x45d3f8), shown as `Wind: SW, 3kph` /
    `Wind: None`. The `sp_wind` option (default/none/random) only applies in
    multiplayer. Not yet: its only physical effect, DF2's projectile drag
    toward the wind velocity (0x44c2f0, per-ammo drag bands; DF1 has no
    drag), and rain/snow drifting with it.
  - **heights** (DF2.EXE 0x464c30, 0x464798): the elevation PCX smoothed
    into 8.8 words, H = 256·Σw·h/29 with the wrapping kernel [2 3 2; 3 9 3;
    2 3 2], compared as H << 7: **0.5 m a step** (DF1 0.25 m); ground under
    the water level darkened at load by ×(255 − 6·depth steps)/256, at least
    25/256 (0x4637c0); the mission's water height override (flag 0x78 bit 0,
    +0x88, 0x45d6ae);
  - **water** (0x4670a0 hardware, 0x466b60 software as the reference): one
    plane at the level out to 2047 m, the 256-wide `water_map` PCX in its own
    palette graded like the terrain, 16 texels a metre, DF1's ±0.25 m wobble
    (16 m, 512 ticks), averaged 50/50 with the scene and writing depth;
    drawn before the objects with the camera above it, after them below it,
    then a 50 % tint with palette entry 255 over the view. No water when the
    PCX fails to load (RIPPLE3 is not on the disc: DMDG3 and DMS2 are dry).
    `water_palette` and `water_opacity` are ignored, as in DF2.EXE.
  - **swimming**: DF2.EXE's water physics (0x440b2b, 0x4404dc, 0x43f690)
    are DF1's (no gravity, ×0.8 vertical damping, hanging at depth or on the
    bottom, swimming along the view, the 0.4 m rise cap, eye 0.3 m), so the
    shared player code serves both. Comparing them showed a DF1 error, fixed
    for both: in water a tick moves the whole per-frame step, on land half,
    so swimming is 2v·cos(pitch) and v·sin(pitch) (AUDIT X9). DF2 only:
    shots from a muzzle under the water fire just the knife and the
    underwater pistol/rifle, others play their +0x2C sound (0x414ad0); the
    animation events 0x10/0x11/0x17 play DEATH_WATER/UW_IDLE/UW_SWIM
    (0x43f480).
  - **sound**: DF2's tables read from the user's DF2.EXE (sound names
    0x4B85A8, 469 records, ids below 0x400; voice sets 0x4B70FC, 22 sets: the
    new 16-21 are ids 700-772), built-in copies otherwise; no terrain
    occlusion (0x45E0A0 returns 1.0); the layer flag 0x80 shuffle (a random
    start, then in turn back round to it, 0x470560); the speed-of-sound queue
    (0x45E910: from 30 m a sound waits d·62/350 ticks, 128 slots) for voices
    and explosions (INFERRED: the other 14 callers not mapped).
  - **weapons** read from the user's DF2.EXE (0x46b230 and its in-place
    conversions): 57 modes (0x4C7238, 92 bytes: kind = DF1's fire type, ammo,
    first-person item, offsets, muzzle vertex, cooldown, zoom, flags 0x1
    scope / 0x40 full auto / 0x4 one-handed), 39 weapon types (0x4C5150,
    loadout categories, modes), 38 ammo records (0x4C4310: DF1's gun layout
    plus drag bands and bullet mass), the drag curve (0x4C86B8); slots per
    0x447e20 (laser, knife, sidearm, primary ×3, secondary, gear, detonator,
    grenade), Extra Ammo doubling the primary's clips; bursts 6/8/51/21/35/36;
    projectiles (0x44c2f0): gravity 0xa7 a tick², drag toward the mission's
    wind by the ammo's speed band and the curve (×(1 + depth/10) under water,
    at most 0.5 a tick), rounds under the water spent below 15.5 m/s; people
    take max(1, |v|²·M / 25) (×8 head, 0x44bf40/0x44bff0); the MasterKey's
    12 pellets within ±5.625°; the Barrett's 1/32 m push. INFERRED: bullets
    carry on under the water and splash where they cross it. Not yet: Kevlar
    (75·(n+1) energy), people's health scale, objects' damage, explosions,
    scope zeroing (X/Z), DF2's loadout screen. Since then: Kevlar for the
    player (Armor: a round first spends 75·(n+1) energy on parts 0/1,
    0x44bdd0); people's health is the ITEMS.DEF hp unscaled (0x412a0a);
    objects (0x44bff0): items with a negative second hp value stop rounds
    unharmed, buildings take nothing, vehicles only rockets (64) and
    shells/impact grenades (16), others max(1, E/25); explosions (0x44d4a0):
    people base 8 (32 for the 12 m charge) full/half/quarter by 4 m/8 m,
    objects 512/128 − 16d or 32 − 4d with a blast class (3/2/0, +1 inside)
    that must reach the item's third hp value. Rounds penetrate objects (0x44bdd0: the face's
    material cost × (hp2 + 1) against |v|²·M, speed × sqrt((E−T)/E), up to 20
    hits a tick). Not yet: shrapnel counts, the delay grenade's bounce,
    the M203 dud within 8 ticks (DF1's arming rule is used), fall damage.
  - **in-mission screens** (DF2.EXE 0x442940 and friends): panels darkened
    50 % with an orange outline (0x45c160); the mission-end panel (0x441660,
    STROVER1-4, green, at top+20..+150), "You are dead." (STRMISC11, top+20),
    pause (STROVER7), the message log (the system history 15..0, own colours),
    the goals (right-aligned at 1000, STRWINCOND/STRLOSECOND%03i from the
    header's ids +0xB0/+0xB4, met 0x804000 / hit red, "Lose Condiitions:"
    as spelt), binocular texts in orange at the view's centre − 15; the
    scope (0x44e2a0): centred at H/2 of the screen, black ring and posts with
    half-blended navy inner segments, "%4im" range in orange, the zeroing
    readout in yellow (setting × 100 m; X/Z step it 0..10, 0..15 for the
    Barrett, and lower the scope view by the type's elevation, 0x445ff0 /
    0x44e0a0), the laser square;
    night vision (key N, on at the start of night missions: 0x44781e) as an
    ellipse 4r × 2r, green only ×4, before the gun is drawn (0x449450); the
    scope image green ×4 at night. DF2 has no "Player Health: Easy".
  - **front end** (`opendf --game games/DF2 play`; src/menu/FrontendDf2.cpp;
    DF2.EXE states 0x45ec40): DF2.MNU parsed as 0x42e460 does ([SOUNDS]
    [ARGBS] [FONTS] [IMAGES] [MENUS] [BUTTONS], strtok fields, attribute
    letters, z-order lists, button sizes), DF2.BIN `MENU` texts, 640×480
    screens scaled to the window (amenity), 4-frame button strips, the menu
    fonts (bands ending at a column-0 marker row), mm_Rect outlines 0xCA4409;
    Choose Soldier (df2plyrs.sav 'FPBC' v10, 5 × 0x6E8: name, face, arm patch,
    stats; "Blackhawk" when empty; name editing), Main Menu (help texts
    teletyped; NovaWorld, Multiplayer and Update disabled), Campaigns (C03/C05,
    regions, the 28 missions with the header +0x7C unlock rule, done marks,
    titles under the dots, CABRIEF), Quick Missions (D06*, TITLE, QMBRIEF,
    pictures), Mission Briefing (MBBRIEF, the terrain JPG ×4 with numbered
    waypoints), Change Gear (categories 3/4/2/4+5, grenades 26/37, the
    mission's header defaults +0x94..+0x97), Settings pages 1 and 3 (df2.cfg,
    sp_wind, gpsicon, attributes), DF2MUSIC.SBF with DF2MUSIC.BIN; after a
    mission back to Campaigns / Quick Missions (no debriefing in DF2), the
    win marking the mission done and the unlock mask kept. Not yet: the fade
    effect, the keyboard page, credits, the voice briefing, slider dragging,
    the map scroll sliders, multiplayer.
  - **command map** (key C; 0x40b370): instead of the 3D view, a 640×480
    mouse screen (scaled into the window; the background tiles continue past
    its sides): CTILE2 background, CTILE panels with DF2_EDGE/DF2_CRNR frames
    3 px outside, the last three chat/system lines, the tabs (Cmap
    CMAP_MENU000-004, F_LG640, 0xff9020 / 0x603000, clickable); Map tab: the
    radar picture at the radar's scale in (48,200)-(640,656), 100 m grid in
    orange with white labels (columns x/100+10 as letters, rows 10 − y/100),
    route lines, double-size blips with a black outline, waypoint names, the
    toggle buttons (Gridlines, Names, People, Waypoints, Vehicles, Buildings:
    END1/MIDL/END2, 0xff9020 on / 0x683000 off), the icon buttons, + / −
    zoom, >< re-centre, right-drag pan, the DF2CURSA / DF2CROSA cursors in
    green; Mission tab: MBBRIEF in 0xc06000. Not yet: the W waypoint tool,
    the elevation and contour views, selecting objects, the Players, Team
    (squad orders) and Weapons tabs.
  - **item behaviours** (DF2.EXE ai table 0x4a4030): snd3 ambient markers
    (acceleration·64 + rand·axle_dist·64 ticks, at the marker, 0x402730);
    bld2 buildings (their sound as dec0's; destroyed: everyone and every
    vehicle within the model's ±flat radius / ±height loses 100 hp, then 64
    steps one per 8 ticks: two rubble bursts a step (effect 0x13 = item 7030,
    sound 0x32 delayed) for clamp(area/64 + 4, 8, 48) steps, dust and smoke
    from step 4, the husk at step 8, a ±500 m noise for the AI; 0x4028d0);
    gnrl breaks silently (0x4012b0); smok one puff (0x402550); ibom the kind-5
    blast with 200 fragments (0x401240 → 0x4572d0); DF2's effect table (0x414500:
    DF1's 16 plus 17 rain, 18 snow, 19 gex5, 20 boomd, 21/22 parachutes,
    23-27). APPROXIMATION: the footprint is the flat-radius square, and the
    smk2 dust cloud is drawn as OpenDF's smoke puff. `OPENDF_TESTBLD2=1`
    destroys a mission's first bld2 at tick 30 (testing).
  - **vehicles** (by ITEMS.DEF move_function): herc flies level at its
    start height and keeps moving when shot down (0x42c240); boat floats on
    four samples clamped to the water, dies in five big blasts and sinks
    0x800 a tick (0x401f60, 0x42be10); loco runs along the map's y axis at its
    team's speed (the path only fires its events), horn 803 on starting,
    loop 808 (0x42c4a0, 0x401340); carg cars copy the first loco's step and
    rock ±1.4° every 32 m (0x42c440, coupling 0x45bd1f); DF2's veh0 eases its
    steer by at most 0.18° a tick (0x41efa1);
  - **parachutes** (0x45bb10, 0x440b2b): people placed 100 m or more up
    (and, INFERRED, the player's start marker) descend 0x2222 a tick under
    `parachut` (PFoil items) or `pararnd`; landing leaves the canopy, which
    tips over and sinks for 1800 ticks (`para` 0x42c570). Not yet: script
    action 18 (teleport) and whether teleported paratroopers open a chute.
  - **keys**: DF2's defaults from DF2.EXE (0x4a5cd4, {id, name[32], key,
    flags}), with scopeinc X, scopedec Z and NVG N added to OpenDF's actions.
- **DF2 status (2026-11-14):** playable through its own front end (`opendf
  --game games/DF2 play`); all 47 single-player and quick missions load and
  run. Still open, by area:
  - *unverified guesses* (INFERRED, listed above): ammo-display kinds by
    DF1's type numbers, radar shapes from record +0x4C, rounds under water,
    the player's parachute start, teleported paratroopers' chutes, the
    undud M203 rule (DF1's), bld2's square footprint;
  - *approximations*: the sky gradient's row rate (16 steps), the smk2 dust
    cloud drawn as a smoke puff, CAL classes per tile line;
  - *not yet*: fall damage; shrapnel counts per explosion; the delay
    grenade's bounce; fog_level / fog_color (unused on the disc); the
    command map's W/D/contour tools and Players/Team/Weapons tabs (squad
    orders); the front end's keyboard page, credits, voice briefing, fades
    and slider dragging; multiplayer (out of scope); AI behaviour beyond
    DF1's (DF2's AI routines were not traced: soldiers think as in DF1, but
    fire through DF2's tables: the BMS weapon byte's table is the same at
    DF2.EXE file offset 0xc88c8, and every shooter uses DF2's modes, ammo,
    drag and energy damage, 0x414ad0; vehicle guns mode 53, 0x41fac0). The
    player is DF2's `player01` (105300, hp 8) or, by name, its snow, night,
    female, LAR V and ghillie variants (INFERRED choice).

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
