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

- The launcher (`opendf` without `--game`, [LAUNCHER.md](LAUNCHER.md)) lists
  every added game: Delta Force and Delta Force 2 start their front ends; Land
  Warrior and Task Force Dagger are recognised (`DFLW.PFF`, `SMALL.PFF`) and
  shown greyed out until they are playable (`gamePlayable` in
  `src/menu/Launcher.cpp`).
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
    undone in `Vfs::read`. The GOG Delta Force (Df.exe 1.00.03.03p) scrambles
    the same 13 files in DFBASE.PFF with no header: the whole file reversed,
    then the same stream from 0x4960552 (GOG Df.exe 0x401050, 0x456720,
    0x456eb0); `Vfs::read` undoes it when DFBASE.PFF carries DFDK.KEY
    (only the GOG release does), and the results match the CD files byte
    for byte.
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
    alpha, one tile a map cell; the **sky gradient**: all 256 entries of
    column 0 of the TGA sky palette, not interpolated, screen row k above the
    horizon row taking entry (k·s) >> 16 with s = 10·fov·horizon/width per
    row (0x467700); **surfaces** only from the `char_data` .CAL (DT/GS/SW/
    RK/CT/SD → dirt/grass/snow/rock/sand; the TRN `type` lines are ignored,
    0x465e5b), read through DF2.EXE's renumbering bug, class(o) =
    cal[remap[remap[o]]] (0x4643a0, 0x466668), copied as the player hears it;
    no .CAL or unmatched names: class 0, silent footsteps (0x4139ae; ±5 m AI
    noise per step, ±2 m on sand, ±10 m in water);
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
    sky is not colour-graded; the gradient rows as described above (view
    +0x4c is the FOV in degrees, 22 with the binoculars);
  - **fog** (used on the disc: D03M07 and D06M17 fog_level 5, DM_S21 and
    KH_S22 7, TDM_D31 7 in 0x646464): BMS flag 0x78 bit 1 → fog_level +0x8C,
    bit 2 → fog_color +0x90 (0x45d6df; TRN keys `fog_level` / `fog_color`,
    default 192,192,192). D = 2^fog_level m: no sky or clouds, the ground cut
    at D, every pixel blended c + (C − c)·min(255, depth·256/D)/256 after the
    objects (the software frame's pass 0x4485b0; INFERRED: depth = view
    depth), the water plane out to min(D, 2047 m), soldiers see no farther
    than D (0x406180). DF2 has no ring fog on its far terrain;
  - **rain and snow** (BMS flag 0x78 bit 3 → +0xAC: 1 rain in CTF_D11,
    D03M11, D05M11, TDM_DG31; 2 snow in D06M01, D06M09): 1024 particles
    born up to 64 m around the last frame's camera, 16 m up (rain 5 a tick
    falling 0x5000, snow 1 falling 0x800), drifting with the full wind
    velocity, effect 17 where they reach the water level (0x46baa0); rain
    a 1-pixel streak 0.5 m up and half a tick of wind back in (106,126,154)
    at 100/255, snow SNOW.3DI at scale 2 facing the camera, both after the
    fog (0x46b780). DF2's D3D frame never called the drawing, so with a
    3D card the original showed none; OpenDF draws what its software
    renderer showed;
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
    multiplayer. Its physical effect, DF2's projectile drag toward the wind
    velocity (0x44c2f0, per-ammo drag bands; DF1 has no drag), is in
    (`Arsenal::df2Drag`).
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
    (0x43f480). The AI swims too (both games, every person runs the same
    move): falling in sets the swim flag with effect 4 and sound 0x8A at the
    surface, walking in sets it silently with one gravity step; in water
    vz ×0x68DB per DF1 AI ground pass (every 4th tick, 0x43693b; the
    player's ×0xCCCC), ×0xCCCC every tick in DF2 (0x440c0a); out when the
    origin is above the level; the bottom holds. The swim motion is by the
    flag (0x43620b / 0x4404df): the move's whole per-frame step along the
    heading and pitch. DF2's chooser (0x43f690) makes any mover under the
    level play 141 Swimming (moving forward) or 140 Floating; DF1's AI keeps
    its own move (only the player's controller picks Swimming, 0x43752e).
    The AI's steering keeps out of deep water (DF1 0x46b34c: 0.75 m at 8 m ahead; DF2
    0x4151a0: 0.25 m with ai flag 0x800 only).
  - **collision volumes** (0x440f80, 0x440984): v8 volumes are convex plane
    sets (LOD +0xB0 planes, volume +0x48 count). People and the player are
    two 0.5 m cubes (at the origin and 0.5 m above the feet) against every
    object of their neighbour list (0x43f2b0: groups 0/1 within the
    flat radius + 10 m, the header radius + 10 m up; rebuilt every 65
    ticks); a cube inside a volume (every plane's n·p + d − 0.5 < 0) is
    pushed out along the least-negative plane: sideways less 0.1 m
    (nothing within 0.1 m), vertically less 0.02 m (a side push sinks the
    mover 0.02 m, which the terrain then lifts back); a downward push stands
    him on the object (ground distance 0, +0xF8 = it). Touch items
    (movecb) do not push. Type 4 volumes are ladders: no gravity, and the
    chooser climbs: forward +0x800 (run +0x1000) a chooser tick (odd ticks)
    with Ladder_Up, back down, the diagonals half, sideways Ladder_Left /
    Right (0x43f951 walk, 0x43fa42 run; jump tables 0x43fb40 / 0x43fb60);
    the player always may, AI with ai flag 0x400 (BMS record +0xC bit
    0x1000; 73 soldiers in D03M03/06/08, D05M06/09, D06M05/11/19). The
    chooser 0x43f690 is the person move's (0x44044f) for everyone, so an AI
    walking or running into a ladder climbs it (AiSystem: the walk / run
    move becomes the ladder move while +0x64 & 0x400000, z at once; back
    to it out of the volume). AI only goes forward, so none climbs down.
    Only PLAYANIM.ANM has the Ladder_* moves (velocity 0.25 / 0.5); enemy
    .ANMs climb in place with no horizontal step (INFERRED: DF2's table
    entry for a missing move). A side push sinks 0.02 m a tick, so a
    walker (0x800 per odd tick) pressed against a wall cannot climb, a
    runner (0x1000) can (READ: 0x440a8b). `OPENDF_TESTLADDER=1|ITEM` sends
    the first such soldier into the nearest ladder (testing; D06M11's
    powcamp guard tower: climbed 27.5 to 32.9 m and stepped onto it). Type 6 volumes set +0x64 & 0x1000000: the
    armory (item 103099 `armory`, model AMMODUMP, in the multiplayer maps;
    KHUFU and TAVERN have one too). READ quirks kept: the sum of a mesh is
    cleared only when a point reaches the mesh box, so a mesh no point
    reaches adds the previous mesh's push again. DF2 has no DF1 mover box
    (World::resolve does nothing under DF2 rules) and no face ground: the
    surface is found only by overlap, so a falling person lands when the
    feet cube enters the roof volume. Placed charges go on a volume top
    within 0.25 m (0x4520d6 → 0x43fb80, a 1/256 m point). Riding: before
    moving, a person on a platform (+0xF8, the object whose volume pushed
    him up; DF1 +0x100, the owner of the face 0x435dc0 found) gets its
    per-tick velocity +0x3C/+0x40 and is turned about its origin by its
    turn (DF2 +0x9C, DF1 +0x90), heading too (0x440876..0x440944, DF1
    0x43634d..0x43643d; READ). Movers write them: veh0/herc/boat while the
    speed is not 0 (stale when stopped), helo every tick (turn only above
    6 m), loco +0x40 only, carg the loco's +0x40 and 0 turn, the boat's
    death zeroes them. DF2 moves the people before the objects (0x440238,
    0x4402bd), so the rider follows one tick behind. +0xF8 is cleared only
    on the terrain, so a jump from a train keeps its speed. D06M05 (a
    train with soldiers on its cars, the team dropped from 100 m) and D03M10
    put soldiers on the cars; `OPENDF_TEAMSPEED=10:40` starts D06M05's
    train at once; `OPENDF_DEBUGRIDE=1` lists the riders.
  - **sound**: DF2's tables read from the user's DF2.EXE (sound names
    0x4B85A8, 469 records, ids below 0x400; voice sets 0x4B70FC, 22 sets: the
    new 16-21 are ids 700-772), built-in copies otherwise; no terrain
    occlusion (0x45E0A0 returns 1.0); the layer flag 0x80 shuffle (a random
    start, then in turn back round to it, 0x470560); the speed-of-sound queue
    (0x45E910: from 30 m a sound waits d·62/350 ticks, 128 slots) for voices
    and explosions (INFERRED: the other 14 callers not mapped). The mixer is
    DF1's integer one (0x48e028; the rounding tables 0x48d240 / 0x48d100 /
    0x4a09d0 and pan 0x4a06ce are byte-identical to DF.EXE's, gains
    pan·vol·master(255) >> 16 at 0x49fc69, music (V >> 1) >> shift at
    0x4a0339, y[n−1] >> 2, clamp ±0x7fff, the mode-3 hook 0x49fb83 set at
    start-up (0x420919: (3, 1))). Unlike DF1 the hook is reset when the menus
    close for the Game Loop (0x43b68d → 0x42f420 → 0x43cb50 → 0x4712e0), so
    it is on in the front end until the first mission starts and never in a
    mission. DF2 mixes each 100 ms timer call (0x47102b) in four parts of
    (n + 4) >> 2 frames (0x48e05a) and a voice whose pitch was changed
    moves its step a quarter of the way (target +0x34 − step +8) >> 2 after
    each part (0x48f818, 0x48e531), where DF1 sets it at once (0x4a971a);
    an update gives the voice the sign-flipped gain pair when that is nearer
    the old one (0x49fcba) and is ignored for a voice not yet started
    (0x49fbf4). OpenDF mixes DF2 in calls of 0x89d frames (INFERRED: the
    timer's nominal length, 0x470e4b). Both games step voices in 1/1024
    samples by pitch × the clip's rate field (0x4a0092) and convert WAVs
    with the canonical 44-byte header (0x4711a0: rate field (rate·16) /
    22050 << 24, size +0x28, data +0x2c; 16-bit by the whole word).
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
    12 pellets within ±5.625°; the Barrett's 1/32 m push. Water (0x44bd40,
    hit type 4, handled at 0x44cee5): a round crossing the surface splashes
    (effect 2, sound 0x3B) and its tick ends 0.125 m below it, velocity kept,
    so rounds go on under the water (and a rising one is pushed back down);
    explosives blast there (type 1, effect 4) or, an M203 within 8 ticks,
    splash. The death move's direction comes from the round's reversed
    velocity (0x44c063). Not yet: Kevlar
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
    hits a tick). Shrapnel (0x412b00 / 0x4136c0 / 0x415490): DF1's counts
    and speeds, but fragments are ammo 31 and DF2's random generator 0x41ac40
    is reseeded to 0x5FC by every burst (DF1's seed call is empty), so each
    burst has the same pattern about its heading; big blasts 0.2 m up, the
    fire-mission shell kept above the water. Death moves (0x415130): DF1's
    names, plus kind 4 (falls: Death_B_P11DF) and 5 (drowning: Death_Water),
    no part clamp; soldiers landing hard take (1 << n) − 1 (0x43f600).
    Weapon handling is DF1's, routine for routine (READ): select 0x4480b0
    (= 0x44aa74: timers and burst counter cleared, a gun with an empty
    magazine reloads, launchers/charges/grenades with an empty chamber get
    the cooldown with the +0x24 sound; any slot 0-9 is taken, also an empty
    one or the one held, 0x41bced), the tick 0x446050 (cooldown end, then
    reload end, then the burst counter's fire command), the silent cooldown-
    end chambering 0x446410 (full auto on flag 0x40), reload end 0x447950
    (an empty chamber gets the cooldown with +0x24 via 0x447f40(0); no
    message — "Ammo reloaded" is the ammo crate in both games), dry fire
    0x4470ae (click +0x2C, silent cooldown, burst counter 0; launchers and
    grenades click only, charges do nothing) and the fire command 0x447270
    with no reload gate. DF2's scope flag [0x7655f4] is only toggled
    (0x447b30, from command 0x41b35a; cleared with the weapon block at the
    mission start, 0x4477bf) and counts for flag-0x1 modes (0x447b40), so it
    survives weapon changes, reloads, moving and the binoculars. DF2's
    binoculars are a weapon slot: B (0x41b6e3) selects slot 10 (T[35] mode
    47, kind 12, flags 0x200, no scope bit) and saves the slot held in
    [0x76553c], B again selects that slot. So with the scope flag on, the
    scope is hidden while the binoculars are held and comes back with the
    gun. The binocular view (FOV 22, the random offset 0x4481e0 picked anew
    on each raise, the Heading/Distance texts) shows only in the raised
    moves 0x68/0x72/5, which the move chooser 0x43f690 picks for a 0x200
    mode at speed bits ((+0x74 >> 3) & 3) zero: walking keeps the
    binoculars in hand (their HUD name red) but lowers the view. The scope
    inset is likewise hidden while moving (0x45c6ac: +0x74 & 0x18), while
    the shot's origin and the designator take 0x447b40 alone; the mouse
    turns at an eighth while the inset shows (0x41ae13). DF1's [0x93353c]
    is the same (AUDIT G5), hidden while moving or with the binoculars up
    ([0x93367c], a flag that moving clears, 0x4373ab). Kevlar is checked on every person
    (0x44cd35, AI block +0xB8 = 31), but only player slots ever set that
    byte (0x45429d and the network copies); placed soldiers have none.
  - **in-mission screens** (DF2.EXE 0x442940 and friends): panels darkened
    50 % with an orange outline (0x45c160); the mission-end panel (0x441660,
    STROVER1-4, green, at top+20..+150), "You are dead." (STRMISC11, top+20),
    pause (STROVER7), the message log (the system history 15..0, entry 0 the
    newest, bottom-anchored, own colours),
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
    win marking the mission done and the unlock mask kept; the voice briefing
    (0x4384f0: on entering the briefing once per chosen mission, and from
    PLAY BRIEFING; `<mission>NN.WAV`, NN 01..19, filed by NN and played in
    slot order, each when the last has ended; the music first ramps over 60
    ticks from the soldier's music volume +0x5bc toward 70 and stays there
    until the page is left, 0x437a70; each voice at direction 0, volume 255,
    pitch 1.0 (0x437933), gains 62 / 63, the dialogue volume unused) and
    slider dragging. Settings has four
    pages that wrap (0x4b3ae0): page 2 "Keyboard" (0x439ea0): 48 rows (labels
    0x4b3af0, defaults 0x4b2cb8 copied into new soldiers by 0x431c00,
    soldier +0x2e0, 12 bytes {VK, mouse 0..3, 0}), ten shown, scroll arrows
    and a slider forced to range 39; clicking a key box waits for one key or
    mouse button (left 1, right 2, middle 3, swallowed while waiting), which
    is taken from every other row first; keys outside 0x439310's list
    (Esc, F-keys, Alt, '/', …) give REMAP_RESERVED; DELETE KEY then a row
    clears it, DEFAULT copies the defaults, REVERT the snapshot taken on the
    first visit (cleared by ACCEPT); key names as 0x439fe0 ("CTRL OR MOUSE
    1", "KEYPAD 5"); messages REMAP_* teletyped in the box at (455,205)
    130×145; ACCEPT saves df2plyrs.sav; at mission start every row sets its
    command's key (0 clears it) and mouse mask (1, 2, 0x10), FIRE_WEAPON both
    attack_1 and attack_2 (0x43afb0). Page 4 "Voice-over-net": drawn from the
    MNU with the VON_BLURB box (opt4_Explain 0x43ad30); its other callbacks
    are empty in DF2.EXE (0x4015e0), so nothing on it does anything. Credits
    (0x430e00): CREDITS.SCR's `[MISSIONSCRIPT]` (strtok ", ", `{PRINTTEXT}`
    texts from CREDITS.BIN `Credits`, `{PAUSE}` t+1 ticks, `{DISPLAYPIC}`
    flags 0/1/2, `{DISPLAYANIM}`, `{DRAWLINE}`, `{PLAYSOUND}` on channel 2;
    TTY text and removals ignored) into 250 slots by ID, lives counting down
    (−1 forever), drawn in slot order; at the end back to the Main Menu.
    Teletype boxes play a random TTY_CLK1..9 on channel 2 each tick while
    typing (0x43d110 calls 0x43d000 while unfinished; DF2.EXE does it per
    drawn frame); menu sounds use their channels (mnu_tran 1, btn_slct 3,
    btn_roll 4: a new sound cuts the old). Screen changes: no fade is drawn
    (the hooks 0x468c00/40/70 call an empty table); input is ignored for 12
    ticks after entering a screen; only the Main Menu and the Mission
    Briefing wait 12 more ticks after a click before switching (0x4356a0,
    0x437811), the others switch at once. A soldier saved by earlier OpenDF
    builds with no bindings at all gets the defaults when loaded (an OpenDF
    repair). Not yet: multiplayer (out of scope).
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
    green; Mission tab: MBBRIEF in 0xc06000; Team tab (0x40f190): the side-1
    soldiers (player first) as Name/Plan/Weapon rows, picked there or by
    clicking them on the map (0x422640); a teammate's four orders (Cmap
    CMAP_BTN_NOORDER/HPHF/HPCM/HP) set the AI byte +0xBB and answer with
    STRMISC34..37 in green (0x4113b0 -> 0x403380); the player's plans are
    shown (multiplayer only). All five tabs show in single player. One
    selection ([0xbcb424]) is shared by Map picks (the nearest blip drawn
    last frame within 1000 px², 0x422640), Players rows and Team rows; the
    selected blip blinks (outline its colour halved, colour | 0x808080 while
    (frame + 8) & 0x20). The selected-object panel at (684,200) (0x40e190:
    Selected/name/team/grid/Plan/Weapons for the player and squad;
    Waypoint/Building/Vehicle with name and grid 0x422d60) and its action
    buttons from (688,696) upward (0x40dd50: Bozo Filter and Send Message to
    as no-ops, Set as my Waypoint, Delete, Rename) on the Map, Players and
    Team tabs. The W tool (0x411250, one-shot; the W button made with state 0
    on every Map visit; the white DF2WCURA cursor anywhere on the Map tab):
    a click on the map makes a player waypoint "WP" (item 6089, 128-slot pool
    0xd34dc0 cleared at mission load; full: nothing made) at the clicked
    ground point, selects it and opens the "Type in a name" prompt (16
    chars, the darkened box 0x4422b0); held left drags waypoints, mission
    ones too (x/y only). Pool waypoints show on the map and the HUD radar as
    green diamonds, the HUD waypoint blinking. "Set as my Waypoint"
    (0x447b70) makes any object the HUD waypoint ("WP: name" off the mission
    list; W returns to the list at entry 1, 0x446700; a deleted one shows NO
    WAYPOINT). Elevation (D button, F9 / F10 with the [0x7655fc] flag that
    Escape clears first): the green ramp 0x4aecdc, also on the HUD radar, no
    water blend; contours ({{{, map only): 0x00ff00 where H >> 12 changes
    from the previous pixel of the row or the pixel south. The Players tab
    (the player alone in single player: number, name, red brackets when
    selected; headings; scroll bar arrows). The Weapons tab (0x40fbe0):
    current and requested loadout with WEAP640 pictures by type (0x4434e0),
    < > cycling through types the slot's category mask allows (0x4a4c88),
    the button rows' quirk (empty slots share a row); Request Weapons is
    accepted only while the player overlaps a type-6 volume (+0x64 &
    0x1000000, an armory: 0x45a6e0), else nothing happens; accepted, the
    loadout is applied with each slot's full magazines and the ammunition
    pools refilled as at the start (0x458190, 0x456f10). INFERRED: single
    player delivers its own request message. OPENDF_REARM accepts anywhere
    (testing). + / − zoom every frame while held, route lines with the black
    ±1 px outline, the Mission tab's CMAP_SETTINGS title. Not done: the
    scroll-bar thumb (not traced), the Players/Team rows staying clickable
    on the Map tab (READ quirk), and the multiplayer parts (send-to lists,
    Make Permanent, voice buttons, received waypoints, the start countdown).
    Testing: OPENDF_CMAPTAB opens a tab, OPENDF_CMAPTEST scripts map clicks,
    keys and typing.
  - **item behaviours** (DF2.EXE ai table 0x4a4030): snd3 ambient markers
    (acceleration·64 + rand·axle_dist·64 ticks, at the marker, 0x402730);
    bld2 buildings (their sound as dec0's; destroyed: everyone and every
    vehicle within the model's ±flat radius / ±height loses 100 hp, then 64
    steps one per 8 ticks: two rubble bursts a step (effect 0x13 = item 7030,
    sound 0x32 delayed) for clamp(area/64 + 4, 8, 48) steps on the edges
    of the model's LOD-0 header box inset 2 m and turned with the building,
    from step 4 up to 12 smk2 dust clouds a step (2·perimeter in all)
    drifting outward, then smoke clouds (area/2) on steps without dust, the
    husk at step 8, a ±500 m flags-0 noise that alerts relaxed soldiers;
    0x4028d0); smk2 clouds are SMKT_H billboards growing, rolling and fading
    (0x42aaa0, efct draw 0x41f740), their colour INFERRED to replace the
    material's; v8 alpha materials without flag 1 take the texture at +0x37
    (0x43ebca) and 8-bit grey textures on them are INFERRED drawn as
    texel-as-alpha;
    gnrl breaks silently (0x4012b0); smok one puff (0x402550); ibom the kind-5
    blast with 200 fragments (0x401240 → 0x4572d0); DF2's effect table (0x414500:
    DF1's 16 plus 17 rain, 18 snow, 19 gex5, 20 boomd, 21/22 parachutes,
    23-27). `OPENDF_TESTBLD2=1` destroys a mission's first bld2 at tick 30
    (testing). The searchlight `slgt` (DF1 has it too, GOG Df.exe 0x4519a0; sweeps ±45° about its placed heading,
    spots within 22.5° and 1000 m, tracks for 8 thinks, the husk and a
    blast 1 m up when shot; 0x427f30, 0x4281e0, 0x425c70), Land Mine (`mine`
    0x4295f0 when shot or blasted; any person stepping on it, 0x43fc80: a
    blast and a smoke generator 0.5 m up) and wall trigger (`vtrg`
    0x42a980: the nearest "vwal crack" within 10 m turns into its husk, a
    blast with 10 fragments) are in; no shipped mission places them and their
    models are not on the disc: `OPENDF_TESTDF2ITEMS=1` places one of each
    with stand-in models (testing); a gone-off mine and trigger stay drawn
    (the draw list 0x4490d0 skips only +0x64 & 2, the draw 0x4459f0 takes
    the husk only with flag 8 and a husk). Pickups: the person mover's two
    0.5 m cubes against the volume planes (0x440f80, World::df2Overlap);
    the bld2 crush box is ±flat radius across, ±height up (entry +0x68 =
    LOD 0 header +0x24, stored at 0x43e6aa);
  - **vehicles** (by ITEMS.DEF move_function): herc flies level at its
    start height and keeps moving when shot down (0x42c240); boat floats on
    four samples clamped to the water, dies in five big blasts and sinks
    0x800 a tick (0x401f60, 0x42be10); loco runs along the map's y axis at its
    team's speed (the path only fires its events), horn 803 on starting,
    loop 808 (0x42c4a0, 0x401340); carg cars copy the first loco's step and
    rock ±1.4° every 32 m (0x42c440, coupling 0x45bd1f); DF2's veh0 eases its
    steer by at most 0.18° a tick (0x41efa1);
  - **parachutes** (0x45bb10, 0x440b2b): people placed 100 m or more up
    (and, INFERRED from the missions, the player's start marker and
    teleport targets that high: the trace found no code that sets the chute
    flag on markers, but starts like D06M06's 160 m only make sense with
    one) descend 0x2222 a tick under
    `parachut` (PFoil items) or `pararnd`; landing (on the ground, an
    object's volume or the water) leaves the canopy, which tips over and
    sinks for 1800 ticks (`para` 0x42c570). There is no wind drift: with
    PFoil (every player item and the Squad Members) the chute glides 0x4000
    a tick (0.25 m, 15 m/s) along the heading, so the player steers by
    turning; without PFoil it comes straight down (0x440651; the wind
    vector 0x4ee250 only moves rain and snow). The canopy is drawn, not an
    object: rounds cannot hit it, and nothing but the landing clears the
    flag, so "shoot the chute to fall faster" is not in this EXE (the
    readers of +0x64 & 0x20 at 0x44c490 / 0x44cb46 / 0x44d0b4 are the
    projectile table's flags). A chutist killed in the air keeps falling
    at the chute's rate under the canopy (dead people run the person move). Script action 18
    (teleport, 0x41955c) moves the team (`teleportTeam`).
  - **start-up and intro movie** (docs/AUDIT.md F15): DF2.EXE's first
    state is MainMenu (0x41ff91); its first init (0x435500) loads the menu
    system (0x42f230), then, unless df2.cfg `no_anim` is set (0x43552a),
    plays DF2INTRO.BIK by running `binkplay.exe DF2INTRO.BIK /I2/C`
    (`video_type` 0) or `... /r/D208/U1/C` (any other `video_type`),
    binkplay.exe and the movie each from DF2.EXE's folder or else
    `<CD>\DF2SETUP` (no intro if either is missing), and waits for the
    process (0x435830: its window hidden for `video_type` 0, polled every
    500 ms, the quit flag ends the wait); then the menu video mode
    (0x436c40), the music (0x43cbc0) and Choose Soldier. No logo or splash
    image; skipping is binkplay's own. The IntroSplash state (a demo-build
    menu) is unreachable in retail. OpenDF runs the same sequence and,
    where DF2.EXE starts binkplay, plays the movie with its own Bink 1
    decoder (`FrontendDf2::playBinkVideo`, src/formats/Bink.{h,cpp},
    docs/FORMATS.md "Bink video"; skipping by any key or click is
    INFERRED, binkplay was not traced). The decoder also handles Land
    Warrior's DFLWINT.BIK (revision `i`).
    `OPENDF_DEBUGVIDEO=1` prints the command DF2.EXE would run. DF2.EXE
    still carries DF1's ANIMMGR KDV player (its strings at file 0x87d9f),
    but DF2 ships no KDV file.
  - **keys**: DF2's defaults from DF2.EXE (0x4a5cd4, {id, name[32], key,
    flags}), with scopeinc X, scopedec Z and NVG N added to OpenDF's actions.
- **DF2 status (2026-11-14):** playable through its own front end (`opendf
  --game games/DF2 play`); all 47 single-player and quick missions load and
  run. Still open, by area:
  - *unverified guesses* (INFERRED, listed above): the player's parachute
    start and teleported paratroopers' chutes. A full sweep of DF2.EXE
    (1999-10-19) finds the chute flag set only by the soldiers' start pass
    (0x45bb10); the player and teleports copy a marker flag that is never
    set, so that EXE would drop the player to its death at the ten missions
    starting 100-160 m up and kill its scripted paradrop teams. OpenDF applies
    the soldiers' 100 m rule to them too: the start heights sit on that test,
    the D06M06 briefing says the player paradrops, reviews and walkthroughs
    describe DF2 missions opening with an automatic parachute drop, and the
    Land Warrior mission editor manual (same engine family) says a "Start,
    Player" marker with Z between 100.00 and 300.00 makes the player
    parachute in, and that this also works for AI units. (DF2's own editor
    manual says nothing about it; a patched DF2.EXE, the last official patch
    being 1.06.15, may set the flag where this one does not.) Below 100 m the player
    starts at the marker's height and drops, as DF2.EXE does;
  - *settled since*: the ammo display's kinds (DF2's own record +0x0C, with
    grenades red while firing or empty and binoculars red unless up), radar
    shapes (signed byte +0x4C, live positions, 0x403590 / 0x421510), rounds
    under water, the player item by id (0x4128d0: 105300..105309; ghillie /
    LAR V only change the drawn model, 0x415410);
  - *settled since*: rain vs snow (type 1 splashes item 107017 "rain
    poink", type 2 draws 107018 "snow", 0x46b94d); the flake test 0x404a20
    (a 1/128 m sphere against the view frustum, near 1/16 m, far 4080 m);
    the smk2 colour (0x411f70 is the scene grade 0x411ec0), roll (+0x38
    about model x, 0x42bc00) and tint (replaces the material colour,
    0x4603bf); the fireballs' flag 0x800 is additive in the software spans
    (0x45eac0 → 0x484ffd, paddusb); the searchlight factors (+0x64 & 1 the
    player slot, & 0x4000 fired, move table +0x10) and its perception (record
    +0x24, 0x40386c) and the ÷4 when AI byte +0xb4 is 0 (0x425eb8, now in);
    DF2 pickups: R = header +0x60, two 0.5 m points at the origin and the
    pose's foot + 0.5 m (0x4409a2), a sphere against each volume's planes
    (0x440f80); the CAL remap table starts zeroed (0x50edf0 past .data);
    the front end's PCX key is index 0 for every 8-bit image (0x462dd3);
    the briefing map centre is the "Briefing Map Center" marker 6038 and
    points are at 4 m a pixel about 512 (0x404546, 0x4045dd); the command
    map prompt y = (H − bottom strip)·768/H − 160 (0x45cb40, 0x443e21);
    the scroll bar's thumb and zones (0x40c7d0, 0x40c8c0); the image-button
    text placement (0x43d4c0); the software z-buffer's depth (0x484077);
  - *also INFERRED*: the front end's
    stale justify locals start each frame at 0 (stack garbage in the EXE;
    0 centres the Main menu's hot-key labels); the character of a key
    hot key is the unshifted SDL symbol; message 0x55 reaching the local
    server in single player (queued 0x406ac0, packed 0x411ba0, the flush
    0x411c70 not followed); the player listed first on the Team tab; a
    soldier's waypoint radius (+0x88) taken as 0;
  - *approximations*: the fog pass reads the perspective-correct depth where
    the software z-buffer steps the vertex depth linearly across each span
    (0x484077); (DF2 pickups and Land Mines now use the v8 volume planes,
    0x440f80; the crush box height is the LOD 0 header +0x24, 0x43e6aa);
  - *done since* (front end and command map from DF2.EXE): the front-end
    font loader 0x468d30 (bands below each index-1 line, glyphs from the
    index-2 baseline mark, tight ink boxes with y offset ymin − band − 3,
    numbered across the sheet, advance w + 2 (0x469240), alpha = palette
    red / 256 (0x4616d0)); text measure 0x4694a0, layout 0x42dc7c (both from
    the text when user1/user2 give no positive size), wrap 0x419d50 (lines
    'A' height + 3 apart); the button text 0x43d4c0: '&' removed by the
    set-text routine 0x42d7b0 with the key letter underlined by '_' one
    pixel lower (0x419fd0 left, 0x41a1f0 right-aligned to the box's left,
    0x41a0d0 centred at y + (h − 'A' height)/4), the plain box 0x469380
    (vertically centred on the tallest glyph, image buttons' top raised by
    0.4 × the 'X' height), the image box [x + 2 (+ user1 for D), x + w − 2],
    an image-less push button centred on x, the two stale justify locals,
    other types' box (x, y)−(x + w, y + 'A' height) without underline,
    owner draw after the default draw; character hot keys (0x42de50) and
    hotkey1/2 (0x42dea0); the command map's scroll bars (creator 0x40c3f0
    arrow placement, 0x40c7d0 outline and thumb, 0x40c710 zones, 0x40c8c0
    arrows, pages and thumb drag on the widget pass 0x40c040's hold
    counter); the prompt's Escape (table 0x41d6ac / 0x41d698 → 0x41d669);
    the player array is mission group 3 (0x403ec4); Team tab heading
    Name/Weapon/Plan, its columns at W·208/1024 and W·384/1024, teammates'
    weapon "None" (their controller bytes +0xb6.. are never written) and
    TeamOrders S_* / L_* plans (0x40f53e, 0x403380); team names by side,
    the player's included (0x40e330); Weapons tab colour −1 = white
    (0x4198e0); the prompt box rounded to 4 pixels and halved (0x45c160);
    arrival at an off-route HUD waypoint (0x446230: its own radius +0x88,
    then entry 1 via 0x446700); the command map runs at 640x480 (0x45da2a
    sets resolution index 4, closing restores it, 0x45d9ea), and the mode
    set reloads f_sml%i / f_LG%i at i = min(width, 800) (0x45c0a0), so the
    map's text is F_SML640 / F_LG640 at any display mode (the Players
    headings no longer overlap); the rain's lines test but do
    not write depth (0x46014f); the software water writes its rows' depth
    (0x485e17) and is fogged by the fog pass, its fog quad 0x467568 being
    the hardware path's;
  - *done since*: the player's fall damage (0x43f600), the M203 arming
    (the same 8 ticks as DF1);
  - *done since*: the delay grenade's slide and fuse, the command map's
    Team tab with squad orders, its W waypoint tool, elevation / contour
    views, Players and Weapons tabs and selection panel, the front end's keyboard page, credits,
    voice briefing, slider dragging and screen timing;
  - *done since*: bld2's per-mesh fireballs (0x402e73..0x402fba, steps
    3..11: one dust cloud per mesh 1..count−2 at the centre of the mesh box
    +0x40..+0x54, move 0x42abc0: drag >> 8, rising to 0x200, growth >> 7 up
    to 0.75, colour from (ff ff c0) through (ff c0 40), (c0 40 00) to
    (40 00 00) over t = 6·age, gone at 0x300; drawn additive and ungraded);
    `OPENDF_TESTBLD2=<item name>` picks the bld2 to destroy;
  - *not yet*: multiplayer (out of scope).
  - **soldier AI** (DF2's org1 0x4015f0 is DF1's 0x4599ec recompiled; the
    differences, READ): fire also needs under 128 m at night, no parachute,
    no hold-fire order, unseen within BMS +0x54×62 (default 60 s) and more
    than 0x40 ticks of continuous sight (0x401a8f); the scan interval by
    the player's distance at the last successful scan (62 / 124 / 372, or
    248 when alert, 0x4015f0); the ghillie suit hides a crouched or prone
    player who has not fired beyond 50 m and halves visibility within
    (0x415a69); BMS flag bits 0x800/0x1000/0x2000/0x4000 → attack everyone
    / (movement) / avoid water over 0.25 m / deaf; flag 1 blocks
    acquisition; script properties 16-19 set or clear them and 2/3/4/14/15
    can clear; night lowers perception 0.15; alert soldiers no longer turn
    while idle; silent soldiers never radio; DF1's water probe only with the
    avoid-water bit; any order holds position. Enemies fire through DF2's
    tables (the BMS weapon byte's table is DF1's, file 0xc88c8; 0x414ad0),
    vehicle guns mode 53. Their shooting is DF1's (READ): the muzzle
    0x414f20 (= 0x46ad30: the mode's +0x08 item's vertex +0x30 at the posed
    hand; no shot without it), the aim 0x415de0 (= 0x46b838: from the body
    origin at the target's origin while it is seen, else the last known
    point with errMul 5, whole-metre distance, the error stored on the
    body; burst rounds along the body's angles ± r>>3 / r>>5), the keep-
    target sight line from 1 m up to the target's origin (0x401732).
    Squad Members Off is df2.cfg attributes 0x20 (0x45bf44 → 0x45c460): the
    same four Squad Member items as DF1 are deleted (the ghillie one, 105271,
    stays); enemy AI hard is attributes 0x10 (0x415f16). The player is DF2's `player01` (105300, hp 8) or
    its snow, night and female variants by id (0x4128d0), the ghillie and
    LAR V models drawn in their place (0x415410);
  - **delay grenade** (0x44ae50): no impact fuse; it slides off object
    faces (v −= n·(v·n)·1.07, × 0.85), stops on other surfaces, and goes
    off (type 1) with 10 ticks of its 444 − rand(127) left.

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
