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
    DF2's JPEGs (terrain colour maps, front-end pictures, the briefing
    map) decode as Df2.exe's own decoder 0x482310 does: libjpeg supplies
    the coefficients, then 0x481220's IDCT, whose column pass takes the
    row-6 term from coefficient c+54 instead of c+48 (8x8 block artefacts
    in about 16% of DF2_MAIN.JPG's blocks, all of DFD1_C.JPG's), nearest
    chroma and the 0x481080 colour tables. Bit-exact with libjpeg's islow
    without fancy upsampling once the slip is undone. The launcher keeps
    plain libjpeg.
- `opendf --game games/DF2 mission D03M01` (or any `DxxMyy`) loads and plays
  with DF1's rules and built-in tables, now with:
  - **soldier animations** from `PLAYER01.KSA` (DF2.EXE 0x41e5e0): 255
    animations of 88-byte frames (15 part angles, pose extents, feet height,
    root motion, event), next-animation/frame continuation, no interpolation;
    posed about the model's mesh pivots with DF2's rotation Rz(a0)Ry(a1)Rx(a2)
    (0x449df0);
  - **sounds** from `DF2.PWF` (DF1's PWF2 format) with DF2's table (0x4B85A8,
    1024 id slots), whole 16-bit WAV samples (DF2 fixed DF1's low-byte bug);
    a name the bank defines twice plays its first record (GOG Df2.exe
    0x47b300; DF2.PWF has 8, e.g. GS_M16_SHOT, EXPLO_WATER, SATCHEL_SET);
    the "Sound: loop" marker items (types 6045..6087) carry their region's
    LOOP.DEF loop from mission load (0x465330 -> 0x4682b0: antloop, zaireloop,
    nigerloop, iranloop, kazloop, nightloop, waterloop 1/2), mixed with the
    other loops (6 loudest every 8 ticks);
  - **terrain detail**: a 64-wide TGA strip of 64×64 tiles blended by their
    alpha, one tile a map cell; the **sky gradient**: all 256 entries of
    column 0 of the TGA sky palette, not interpolated, screen row k above the
    horizon row taking entry (k·s) >> 16 with s = 10·fov·horizon/width per
    row (0x467700); **surfaces** only from the `char_data` .CAL (DT/GS/SW/
    RK/CT/SD → dirt/grass/snow/rock/sand, RD/MD/IE/QS → classes 8-11, GOG
    0x470190; the TRN `type` lines are ignored,
    0x465e5b), read through DF2.EXE's renumbering bug, class(o) =
    cal[remap[remap[o]]] (0x4643a0, 0x466668), copied as the player hears it;
    no .CAL or unmatched names: class 0, silent footsteps (0x4139ae; ±5 m AI
    noise per step, ±2 m on sand, ±10 m in water); RD/MD/IE/QS are classes
    8..11, which only the GOG build sounds (footsteps 0x415780: road/QS/mud
    0x82/0x83, ice 0x86/0x87; terrain hits 0x415a60: road metal 0x37, mud and
    QS dirt, ice sand, with AI noise 10/5/5/2 m), the CD build is silent there;
    terrain hits make an AI noise by class (5/2/2/10/5/10 m for 1..6; GOG 0x415a60 all of 1..11: sound 3C/40/3A/3D/3A/3C/3C/37/3C/3A/3C, effect slot 5/7/9/13/8/6/5/11/5/9/5, noise 5/2/2/10/5/10/10/10/5/5/2 m);
  - **rotor loops** (GOG 0x42c2da): 502 for the gazelle, Havoc, Hind and
    Ka-29, 503 for the Black Hawk only; **whiz-by** 0x41 (0x4547a2): never for
    hand or delay grenades, the step's start or end within 40 m on each axis;
  - **soldier hit spheres** (0x44b2f0): KSA has no skeleton, so one sphere
    per mesh, centred on the posed mesh pivot, radius mesh +0x64; the 1.5 m
    body pre-test and nearest-along-the-ray rule are DF1's;
  - **DF2's own HUD** (DF2.EXE 0x442940; full notes in the tracing specs):
    the 3D view is only the rows between two opaque bitmap strips
    (`DHT%iG`/`DHB%iG`, 0x45cb40; drawn before every overlay box, GOG 0x448fd0); every position per display mode from
    `HUD.DEF` (0x444150); text in the HUD font in 0xff8000, all DF2 text without a shadow (GOG 0x41bb90); the HUD font f_sml%i (i = min(width, 800)) is a band sheet (GOG 0x465a20 -> 0x41ba90 flag 2, 0x41b9a0 / 0x41b8a0: F_SML640 277x240, 15-row bands), undefined characters drawn as glyph 0; the weapon,
    stance, carried-item and radar-frame images through a fixed amber ramp
    (index i → (i·255>>8, i>>1, 0)) added onto the strip (0x450c10, saturating);
    weapon icon by mode from a 22-frame sheet (0x443370); weapon name from
    `HudGuns` HG%03i centred, red when not ready, clips "\x01 n" / `NA`,
    a proportional solid rounds bar and the chamber bar (0x443110);
    waypoint lines `WP%i: name`, `HEADING: NA`, `GRID: NA`, `RANGE`,
    `ELEVATION` (0x442c30) with names per waypoint record +0x58 from
    `DF2WPN.BIN` "WPNames"; the player's name (0x441b20); the radar: the
    colour map north-up at 1.6 m a pixel at 640 (scale as the command map),
    route lines, objects by their marker shape, GPS soldiers, the player
    (colours as the command map's, GOG 0x4273b0: the player 0xff8000, its
    side's people green, other sides' people red, other objects by side 0
    green, 1 0x1040ff, 2 red, 3 yellow, 4 0xff027f), the
    next-waypoint dot on the `map_radius` ring (0x421510, 0x4226c0,
    0x443850); the top strip: the last three of two 40-line histories
    (system and chat), each line in its own colour, no timeout (0x442170);
    the crosshair: four orange arms, gap W·320/65536, arm W·640/65536
    (0x45a9e0); target identification (GOG 0x4642b0, scope 0x455be0,
    pick 0x4189b0): unless attributes & 0x100 (the "FF" option off), the
    first person (organics table) of the player's side or side 0 along the aim within the
    centre's range + 2 m and 2 m of the ray gets, if on the player's side,
    four red diagonal ticks at the crosshair corners (scope: ±r/2 to ±r/4),
    and, if it has a name (object +0x54 from 0x403790: DF2WPN.BIN `PeopleNames` STRNAME%03i of
    record +4, item 6005 `WPNames` STRWPNAME%03i of +0x58), its name in red W·1920/65536 above the top arm (scope: at
    the inset centre), with the crosshairs on or off; friendly tags (F,
    GOG 0x41f150 / 0x44fe70 / 0x44fee0): 0 off, 1 names, 2 glyph 4, with
    STRMISC05 / 04 / 33, on (1) at every fresh mission start but kept on a
    replay (0x466ba0), none with attributes & 0x400; named people on the
    player's side in view, 1 m projecting over half a pixel, 25 above the
    origin raised by 1 m, centred, no clip, always green 0x00ff00 (the CD
    build coloured by side). Wider windows (amenity): each strip is split inside one
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
    (movecb) do not push. Type 4 volumes are ladders: contact clears airborne
    and, unless prone (+0x74 & 0x20), zeroes vel_z (GOG Df2.exe 0x446e0a..
    0x446e1c), and there is no gravity on one unless prone (0x4470a5: a
    prone person falls through), so a jump or a fall stops at the ladder;
    an AI's +0x74 is never written outside the local player (0x41efd0,
    0x447120) and the network receive paths (0x409fae, 0x45d3e7), so the
    prone test is always false for an AI. The
    chooser climbs (GOG Df2.exe 0x445980; CD 0x43f951 / 0x43fa42, which
    climbed down on back): forward 0x800 (run 0x1000) a chooser tick (odd
    ticks) with Ladder_Up, up when the pitch +0x34 is above level, down
    when level or below; the front diagonals half with Ladder_Left /
    Right, sideways those moves with no climb; Ladder_Down is never
    chosen. Back and its diagonals: standing and walking move 0 Null (no
    motion), running or crouched the land moves (stepping off). Standing
    or crouched still and not shooting: Ladder_Stand. Prone never climbs;
    the player always may, AI with ai flag 0x400 (BMS record +0xC bit
    0x1000; 73 soldiers in D03M03/06/08, D05M06/09, D06M05/11/19). The
    chooser 0x43f690 is the person move's (0x44044f) for everyone, so an AI
    walking or running into a ladder climbs it (AiSystem: the walk / run
    move becomes the ladder move while +0x64 & 0x400000, z at once; back
    to it out of the volume). AI only goes forward, so it climbs up only
    while its aim pitch (+0x34) is above level; at level it goes down.
    Only PLAYANIM.ANM has the Ladder_* moves (velocity 0.25 / 0.5); enemy
    .ANMs climb in place with no horizontal step (INFERRED: DF2's table
    entry for a missing move). A side push sinks 0.02 m a tick, so a
    walker (0x800 per odd tick) pressed against a wall cannot climb, a
    runner (0x1000) can (READ: 0x440a8b). `OPENDF_TESTLADDER=1|ITEM` sends
    the first such soldier into the nearest ladder (testing; D06M11's
    powcamp guard tower: under the CD rules climbed 27.5 to 32.9 m and
    stepped onto it; under GOG's the guard aims level and stays at the foot). Type 6 volumes set +0x64 & 0x1000000: the
    armory (item 103099 `armory`, model AMMODUMP, in the multiplayer maps;
    KHUFU and TAVERN have one too). Emplaced guns (GOG Df2.exe 1.06.15
    only; the CD build pushes type 7 like any solid volume): ITEMS.DEF
    attrib `EWeap` (0x8000; `4Team` is 0x4000, 0x422cca / 0x422cfa) marks
    50Cal_S/K/K0/K2/P/B and glaunch; their seat is a type-7 volume, which
    sets flags 8 and does not push (0x447450). The person move calls
    0x4145a0(person, gun) for it (0x4467d0): with the gun EWeap and the
    person on nothing or this gun, a control direction 3..5 (keys held
    back, 0x41efd0) or either dead/destroyed clears both links (person
    +0x8c, gun +0xF8); else, if nobody's +0x8c is this gun, it links them
    and puts the person's origin at the gun plus hardpoint 0 turned by his
    heading (0x418300). Linked, the chooser plays 107 Crouched_EWeap
    (0x445980; speed 0 in PLAYANIM.ANM, absent from the enemies'), the
    player's heading stays within 45 degrees of the gun's and the pitch
    within 22.5, re-placed every chooser tick (0x447120), the view comes
    from hardpoint 2 through the gun's posed mesh 1 with the player's
    angles (0x44dd50 -> 0x416e90), and the gun's "ewpn" pose (0x450c60)
    turns meshes 1 and 2 about hardpoint 1 by the gunner's heading less
    the gun's and his pitch. A destroyed gun lets go (0x4467d0's end).
    The gun's own weapon: slots 11 / 12 hold modes 0x37 (.50 cal, ammo
    36, 200 rounds, cooldown 6, full auto) and 0x38 (ammo 37, 40 rounds,
    cooldown 16) (0x44eeb0); after each weapon tick (0x44c840's end) a
    manned gun selects 12 for item 4080 (glaunch), else 11, and none
    held returns to slot 3; 0x44f160 refuses 11 / 12 without a gun
    (a weapon key switches away for one tick), 0x44d3c0 does not cycle
    off them, and the rounds leave the mounted eye (0x44dd50 ->
    0x416e90). Not done: the HUD's name and icon for modes 55 / 56
    and the multiplayer mount 0x4183d0. Only TKH_G21 places
    guns (two 50Cal_K); walking into one there seats the player.
    READ quirks kept: the sum of a mesh is
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
    0x4B85A8, GOG 0x4CD7C8, 469 records, ids below 0x400; voice sets 0x4B70FC,
    GOG 0x4CBCC0, 22 sets: the
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
    plus drag bands and bullet mass), the drag curve (0x4C86B8); the GOG
    Df2.exe 1.06.15 (init 0x4758f0) keeps them elsewhere (ammo 0x4D95B8, 39
    rows of 100 bytes with a new dword at +0x0C and the flags << 16; types
    0x4DA4F8, modes 0x4DC5E0, curve 0x4DDA60) with its own values (e.g. ammo
    4-6 accuracy 3 and 180 grains, M203 12 rounds, grenade and M203 flight
    1000 ticks), told apart by the M4's ammo row; slots per
    0x447e20 (laser, knife, sidearm, primary ×3, secondary, gear, detonator,
    grenade), Extra Ammo doubling the primary's clips; bursts 6/8/51/21/35/36;
    GOG's Df2.exe 1.06.15 (0x4758f0, told apart by the M4's ammo record) has
    the tables at 0x4DC5E0 / 0x4DA4F8 / 0x4D95B8 / 0x4DDA60, 39 ammo records
    of 100 bytes (+0x0C the clips Extra Ammo adds, later fields 4 on, flags
    << 16), changed values (M203 12 rounds, mounted guns 55/56 full auto) and
    its own refill (0x44dfc0: Extra Ammo as secondary or gear adds +0x0C
    clips to slot 3's weapon, slot 4's too for primaries 9/10);
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
    that must reach the item's third hp value; the blast's line of fire for
    people, the player and objects is GOG 0x406610 (terrain, decorations and
    items in the way, no fog cutoff); the blast carries its owner (the
    round's shooter, the charge's placer, the strike's requester): objects of
    the owner's side are spared and victims and fragments credit the owner
    (0x454b60, 0x452120). Round explosions (GOG 0x453860): gex5 (0x13) on the
    ground with a smoke generator (none for impact rounds whose ammo has
    0x20000000), boomd (0x14) on objects, people and for the delay grenade's
    fuse, effect 4 / sound 0x35 in the water, each with a ±200 m AI noise;
    water splashes ±10 m, a dud M203 on a person effect 0x10, sound 0x39,
    ±5 m. Glass (an intact object's 0x10000000 face): rounds and explosives
    (LAW, impact and delay grenades) go through it without exploding, the
    mesh breaking once (obj +0x78) with GLASS_SMASH 800 and a ±20 m (±40 m
    for explosives) noise, no impact effect; the panes stay drawn (+0x78 has
    no draw reader). Not yet: the glass shards (0x414a90: min(100, area·20)
    of 107047..107049, ai `gdeb`) and the blast's flag-2 mesh pass. The bld2
    crush (0x45cd80) takes 100 hp from people, every item-list object and
    every non-building decoration in its box. Rounds penetrate objects (0x44bdd0: the face's
    material cost × (hp2 + 1) against |v|²·M, speed × sqrt((E−T)/E), up to 20
    hits a tick; a face with no impact material, such as the wire fences, or
    glass on a husk costs nothing and the round goes on at full speed, GOG
    0x453860; an AI noise per material: flesh, wood and none 5 m, metal, stone and armour 15 m, bush and canvas 3 m, intact glass 20 m)
    and people (GOG 0x453860 case 3: flesh 100·(hp2 + 1) after the damage, a
    5 m noise, the person skipped for the rest of the tick); the passes of a
    tick follow the segment traced at its start, each resuming 0x2000 past
    the hit. An item with hp2 < 0 (dish, radar, the 'Target:' vehicles)
    stops every round dead, whatever its face (0x453510), and sets off a
    delay grenade there. Tracers (0x452290 / 0x452610): every pass of a
    round leaves a segment in a ring of 2048 that fades over 0x1e ticks
    (alpha ((life >> 2) << 3) / 255), coloured at fire time by the
    shooter's side (0x451ae0: 1 light blue, 2 light red, 3 yellow, 4 pink,
    else white; M203 and LAW white, shrapnel 0x1010101); with Options3
    TRACERS on (attributes 0x80 clear) all but rounds with flags 0x1c900000
    (hand and delay grenades, shotgun pellets among them), off only the LAW
    and shrapnel; under the water every round, in white. Shrapnel (0x412b00 / 0x4136c0 / 0x415490): DF1's counts
    and speeds, but fragments are ammo 31 and DF2's random generator 0x41ac40
    is reseeded to 0x5FC by every burst (DF1's seed call is empty), so each
    burst has the same pattern about its heading; the big blast (GOG
    0x45f910 -> 0x4174d0: satchel, fire mission, boat, ibom) at the given
    point (the +0x3333 goes only into the network packet), gex5 with the
    generator's first two numbers as its yaw and scale, kind 5, a ±200 m
    noise, no smoke, 200 fragments fanned from the object's yaw (the strike
    0); the fire-mission shell kept above the water. Rounds drawn in flight:
    M203 first, then the hand and delay grenades' 4033, then the LAW's
    rocket (0x451ae0); the delay grenade's fuse is its lifetime less
    (yaw >> 16) & 0x7f. Death moves (0x415130): DF1's
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
    binoculars in hand (their HUD name red) but lowers the view. The
    chooser's hand class (GOG 0x445980) is the mode's +0x3C: & 0x18 the
    *_2HW moves, & 4 the *_1HW ones, anything else (knife, grenades,
    satchel, claymores, detonator, laser, binoculars) the plain WalkingN /
    RunningN (Running0 6.0 m/s against Running0_2HW's 5.0), _S only for
    the two hand classes; still: BIN 0x200, SLW 0x10, SCW with the scope
    in (standing), 2HW 8, 1HW 4, else KNIFE (prone: 1HW). The scope
    inset is likewise hidden while moving (0x45c6ac: +0x74 & 0x18), while
    the shot's origin and the designator take 0x447b40 alone; the mouse
    turns at an eighth while the inset shows in the CD build (0x41ae13);
    GOG Df2.exe 0x41eb50 replaces the eighth by a rate that grows with the
    frame's counts u over the mode's width / height s, 0x40000 + 4096 ·
    scope · (2u/s)² binary angle per count, scope = the soldier's SCOPE
    SENSITIVITY (+0x5d8, Settings 0..0x180) + 0x80 clamped to 0x80..0x1ff;
    the mouse sensitivity (+0x5b0, 0..0x200) is used as 0x80 when <= 0 and
    0x1ff above. Both are written back after the mission. DF1's [0x93353c]
    is the same (AUDIT G5), hidden while moving or with the binoculars up
    ([0x93367c], a flag that moving clears, 0x4373ab). Kevlar is checked on every person
    (0x44cd35, AI block +0xB8 = 31), but only player slots ever set that
    byte (0x45429d and the network copies); placed soldiers have none.
    GOG Df2.exe's video camera (secondary or gear type 36, mode 48, kind
    13, ammo 38: one camera): firing sets the inset flag [0x79a6b4]
    (0x44d8a0) and, chambered and out of cooldown, places item 4138 1 m
    ahead like a satchel (0x44d460 -> 0x406d00; the host removes the
    player's older camera first, 0x459b80 -> 0x418190, and 0x414850 takes
    the new one as [0x79a6b8] with its yaw and pitch), with the satchel's
    kick and the load sound; its cooldown ends like a satchel's (0x44cc20).
    0x44c840 lets the camera go once destroyed or removed and keeps its
    pitch within 45 degrees. While the flag is on and the scope is not
    shown, 0x465f90 -> 0x4568a0 draws the camera's view (0.5 m above it,
    90 degrees across) at x 0x2c0..0x3c0, y view top + 0x40..+0x100 of the
    1024x768 frame, or clears the flag when there is no camera. VCam (F12,
    0x41f150 case 0x28) toggles the flag; holding mode 48 with the view on
    the mouse turns the camera instead of the player (0x41eb50 -> commands
    0x35..0x38, cases 0x29..0x2c). Mode flag 0x100 (modes 31, 48) hides the
    first-person model once no clip or chambered one is left (0x44ea10).
    The Forward Observer (GOG 0x41f150 case 0x14, fo F11: night vision
    off, [0x79a6a8] toggled; N turns it off, case 0x27): with the camera's
    inset off and the scope not shown, 0x456670 draws its target's view
    ([0x79a6ac], the player at mission start, 0x44e7f0) in the same
    rectangle (x rounded down to 4) from the target's origin raised by the
    player's eye offset [0x79a688], its yaw and pitch, 90 degrees across,
    the target itself not drawn; 0x4559d0(1) outlines it in 0xff8000 and
    writes the target's name at (x0 + 10, y1 + 10); a target no longer in
    use turns it off, none while the player is dead or the target on
    another side. Tab (case 0x15, 0x44e590) picks the next person in use on
    the player's side with item attrib 8 (FO), wrapping; none: no target.
    OpenDF counts the player's own table entry as before the first person.
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
    0x44e0a0), the laser square; the scope view's FOV 28.125 / mag degrees
    in 16.16 (GOG 0x4557e0: 0x46500000 / (mag × 640)), mag for slots 3-5
    the kept magnification [0x79a698] (E / Shift+E step it 2..scope with
    wraparound while scoped, 0x44c7e0; cleared at mission start, 0x44e7f0,
    then the slot-3 scope, 0x44eeb0; clamped by 0x44e6a0; shown as "%2ix" at (700, 480) in
    yellow), half of it with the
    fraction kept, focal trunc(100 / tan(half) + 0.5) (camera 0x4048b0), not
    DF1's 28 / mag whole degrees;
    night vision (key N, on at the start of night missions: 0x44781e) as an
    ellipse 4r × 2r, green only ×4, before the gun is drawn (0x449450), not
    while the scope shows (GOG 0x465fde); the scope image green ×4 while the
    night vision is on (GOG 0x455be0, [0x79a7fc]). DF2 has no "Player Health: Easy".
  - **front end** (`opendf --game games/DF2 play`; src/menu/FrontendDf2.cpp;
    DF2.EXE states 0x45ec40): DF2.MNU parsed as 0x42e460 does ([SOUNDS]
    [ARGBS] [FONTS] [IMAGES] [MENUS] [BUTTONS], strtok fields, attribute
    letters, buttons qsorted by name with _strcmpi then each menu's list
    exchange-sorted by descending z-order, GOG 0x432ba0 / 0x433040, so the
    Campaigns region buttons draw over the world map; button sizes), DF2.BIN `MENU` texts, 640×480
    screens scaled to the window (amenity), 4-frame button strips, the menu
    fonts (bands ending at a column-0 marker row), mm_Rect outlines 0xCA4409;
    Choose Soldier (df2plyrs.sav 'FPBC' v14 as GOG 0x436890 writes, v10..v14
    read with 0x4369f0's per-version patch, 5 × 0x6E8: name, face, arm patch,
    stats; "Blackhawk" when empty; name editing), Main Menu (help texts
    teletyped; NovaWorld, Multiplayer and Update disabled), Campaigns (C03/C05,
    regions, the 28 missions with the header +0x7C unlock rule, done marks,
    titles under the dots, CABRIEF; GOG 0x434800: only this campaign's
    regions with a done or unlocked mission and only the done or unlocked
    dots of the current region are shown, the rest hidden and unclickable;
    the shown region kept per campaign, first the first available one with
    missions left, a complete one making the next blink every 30 ticks,
    GOG 0x434440 / 0x434a90 / 0x434a20 / 0x43457b, with campaign 2's blink
    word shared with the timer as in the EXE; the Main Menu resets the
    campaign to 1, 0x43a5c9), Quick Missions (D06*, TITLE, QMBRIEF,
    pictures), Mission Briefing (MBBRIEF at (x + 5, y + 5) in its border,
    scrolled by but_mb_brvsl over max(0, lines - 25), GOG 0x43d000 /
    0x43d120; the map, GOG 0x43ce90: the
    version-15 header's terrain name with .JPG for .TRN (DFS1.JPG, 256²)
    tiled 4 × 4 into 1024² (0x46c760), drawn 1:1 at (x+1, y+1) inside the
    mm_Rect border, nothing at all without the picture; the scroll is the
    map sliders' value, range picture − view, eased by ≤ 10 a tick toward a
    target (page tick 0x43ca20) that the sliders, the arrows (±10 at once,
    0x43d610..0x43d6d0), MAP CENTER (0x43d930) and a press on the map
    (0x43d250 event 3: centres on the point under the mouse) set; the BMS
    markers of GOG 0x4045e0, object byte +0x4C set and flags & 0x50 clear, up
    to 50, at 4 m a pixel: route markers 1-3 joined by 0xFFFFE000 lines with a yellow icon
    and their number at (x+4, y-5), the others as 8-frame team icons
    grn/blu/red/yel_icon.pcx, GOG 0x43d250; scroll centred by (view-1)>>1,
    0x43cdf0), Change Gear (categories 3/4/2/4+5, grenades 26/37, the
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
    slider dragging. The mouse (GOG 0x433f20): the hit test 0x4337a0 takes
    the first button from the top z-order whose box (0x432690: image push
    button w × h/4, slider image with the track p3 as length, any other
    image type 0 × 0; text box or (p1, p2) without one, 1.6 times as tall
    and left of x when right-justified unless G) holds the mouse, skipping
    only J (0x200), so hidden, C and callback-less buttons are hit too and
    cover what is below; then by type (0x433ec0) push buttons (btn_roll on
    entering, btn_slct on pressing, unless M) or sliders (a press near the
    knob holds it, elsewhere steps the value by user1 or 5 toward the
    mouse), other types nothing, drawstate 0 and 4 ignored. Settings has four
    pages that wrap (0x4b3ae0): page 2 "Keyboard" (0x439ea0): 59 rows in the
    GOG build (labels 0x4c8144, defaults 0x4c7190 copied into new soldiers by
    0x4368f0; the CD's 48 plus PRINT_SCREEN, the voice-over-net keys,
    SELECT_TARGETED and SCOPE_MAGNIFICATION, rows 45/46 named
    INCREASE/DECREASE_SCOPE_ZERO; soldier +0x2e0, 12 bytes {VK, mouse 0..3,
    0}), ten shown, scroll arrows and a slider forced to range 50; clicking a key box waits for one key or
    mouse button (left 1, right 2, middle 3, swallowed while waiting), which
    is taken from every other row first; Esc, Scroll Lock, Pause, the
    Windows keys, VK 0x2A, '`' and F1..F24 give REMAP_RESERVED (GOG
    0x43e6c0; the CD's allow-list 0x439310 is gone); DELETE KEY then a row
    clears it, DEFAULT copies the defaults, REVERT the snapshot taken on the
    first visit (cleared by ACCEPT); key names as 0x439fe0 ("CTRL OR MOUSE
    1", "KEYPAD 5"); messages REMAP_* teletyped in the box at (455,205)
    130×145; ACCEPT saves df2plyrs.sav; at mission start every row sets its
    command's key (0 clears it) and mouse mask (1, 2, 0x10), FIRE_WEAPON both
    attack_1 and attack_2 (GOG 0x441140; PrintScreen and the Von*
    commands have no game-side action in OpenDF yet). Page 4 "Voice-over-net": drawn from the
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
    3 px outside, the last three chat/system lines, the six tabs of GOG's
    table 0x4af9e0 (Map, VON, Players, Team, Weapons, Mission: Cmap
    CMAP_MENU000, the literal "VON", CMAP_MENU001-004; F_LG640, 0xff9020 /
    0x603000, laid out from x 40 with the rest up to 984 shared six ways,
    0x40bda0, clickable on their text, 0x40d860; the CD's table has five);
    Map tab: the
    radar picture at the radar's scale in (48,200)-(640,656), 100 m grid in
    orange with white labels (columns x/100+10 as letters, negative ones
    wrapped by 702, rows 10 − y/100),
    route lines, double-size blips with a black outline in GOG's colours
    (0x4273b0: the player 0xff8000, its side's people green, other sides'
    people red; other objects by side), with Names every named blip's +0x54
    name (PeopleNames / WPNames, the soldier name, W waypoint names) at
    (x + size, y) in each outline pass's colour (0x427904), the
    toggle buttons (Gridlines, Names, People, Waypoints, Vehicles, Buildings:
    END1/MIDL/END2, text 0xff9020 on / 0x683000 off; every text button
    draws frame 1 under the mouse and 2 while held there, GOG 0x40c8a0 /
    0x40cae0), the icon buttons, + / −
    zoom, >< re-centre, right-drag pan, the DF2CURSA / DF2CROSA cursors in
    green; Mission tab (0x412bb0): MBBRIEF through the text box 0x40c310
    in (64,216)-(608,704), 0xc06000, wrapped at spaces / '~' / CR / LF,
    lines the 'A' glyph height + 2 apart, empty segments not drawn,
    clipped at the bottom, scrolled by its bar (608, 0..count+1 page 18),
    then "Game Settings\rn/a" unwrapped in (704,216)-(960,704) with its own
    bar at 944 (READ quirk: a line cut by width at a CR/LF/'~' loses the
    first letter of the next word, "ituation."); VON (0x40f5b0) and
    Players (0x40fb70): the players (the player alone in single player),
    16 numbered rows from y 240, the VON or Receive/Waypoints/Mute/Text
    chat headings, no per-row buttons on the player's own row; Team tab
    (0x410060): the side-1
    soldiers (player first) as Name/Weapon/Plan rows from y 240 (14
    rows, headings Name at 80, Weapon centred at 288, Plan at 464, y 204),
    each row hit only on its number-less name rect (0x40d790), picked there or by
    clicking them on the map (0x422640); a teammate's four orders (Cmap
    CMAP_BTN_NOORDER/HPHF/HPCM/HP) set the AI byte +0xBB and answer with
    STRMISC34..37 in green (0x4113b0 -> 0x403380); the player's plans are
    shown (multiplayer only). All six tabs show in single player. One
    selection ([0xbcb424]) is shared by Map picks (the nearest blip drawn
    last frame within 1000 px², 0x422640), Players rows and Team rows; the
    selected blip blinks (outline its colour halved, colour | 0x808080 while
    (frame + 8) & 0x20). The selected-object panel at (684,200) (0x40e190:
    Selected/name/team/grid/Plan/Weapons for the player and squad;
    Waypoint/Building/Vehicle with name and grid 0x427a60: 16.16 fixed
    point, columns wrapped by 70200 m, rows by 10000 m) and its action
    buttons from (688,696) upward (0x40dd50: Bozo Filter and Send Message to
    as no-ops, Set as my Waypoint, Delete, Rename) on the Map, Players and
    VON, Players and Team tabs. The W tool (0x411250, one-shot; the W button made with state 0
    on every Map visit; the white DF2WCURA cursor anywhere on the Map tab):
    a click on the map makes a player waypoint "WP" (item 6089, 128-slot pool
    0xd34dc0 cleared at mission load; full: nothing made) at the clicked
    ground point, selects it and opens the "Type in a name" prompt (16
    chars, the darkened box 0x4422b0); held left drags waypoints, mission
    ones too (x/y only; not on the press frame, GOG 0x40dd70). Pool waypoints show in the 3D view (GOG 0x450070: a green line one metre tall straight up the screen, the name centred 15 pixels above), on the map and the HUD radar as
    green diamonds, the HUD waypoint blinking. "Set as my Waypoint"
    (0x447b70) makes any object the HUD waypoint ("WP: name" off the mission
    list; W returns to the list at entry 1, 0x446700; a deleted one shows NO
    WAYPOINT). Elevation (D button, F9 / F10 with the [0x7655fc] flag that
    Escape clears first): the green ramp 0x4aecdc, also on the HUD radar, no
    water blend; contours ({{{, map only): 0x00ff00 where H >> 12 changes
    from the previous pixel of the row or the pixel south. The mission
    route keeps up to 16 points (GOG 0x459740; DF1 8). The Weapons tab (0x40fbe0):
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
  - **item behaviours** (DF2.EXE ai table 0x4a4030): the "Sound: ..." markers
    snd0..snd3 (GOG Df2.exe 0x4027f0, 0x402850, 0x4028b0, 0x402910; CD
    0x402730 for snd3): graphic-less records in the markers table, visited
    every 64 ticks (0x446550), first wait counter % 60 (0x403790, +11 a
    record, never reset), the sound at the marker, then 20..25 / 8..11 /
    3..4 × 64 ticks, snd3 int(acceleration)·64 + r·axle_dist·64 ticks, r
    from the shared generator 0x41ea20;
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
    spots within 22.5° and 1000 m anyone of another non-zero side (GOG
    0x42aa10 compares sides, not teams), tracks for 8 thinks, the husk and a
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
    steer by at most 0.18° a tick (0x41efa1) and thinks every 0x20 ticks on
    a path, 0x40 off it (GOG 0x4237c0); a destroyed one stops dead where it
    blew up (the move 0x423ac0 runs only while +0x64 & 4 is clear, the
    think 0x423865 zeroes +0x9C and +0x3C..+0x50). The BRDM (item 102023, graphic
    number 0x7e7) has a turret gun (GOG 0x423a1e; none with AI flag 1):
    each think it takes a target through the soldiers' scan (0x417830) drawn
    against rand, with a pause of 32..95 and a yaw bias scaled by
    100 / accuracy (0x424680), re-checks the sight line every 0xC0 ticks
    (0x424780) and aims the turret at the last known point (yaw ±135°,
    pitch −5.6..45° from the hull, 0x424470); every tick the turret turns
    0x200000 toward it and, within 0x10000000, fires .50 (mode 53) bursts
    of 16..47 ticks, one round per 16, pauses 64..127, the bias ×3/4 each
    time (0x423ac0, 0x424550); `brdm` draws the turret, gun, steered front
    wheels and the muzzle flash for one frame after a round (0x4295f0);
    helicopters by graphic number (item def +0x20): rest height of the
    origin above the ground Black Hawk 0x7e5 0x37000, Havoc 0x7d5 0x38000,
    Hind 0x7ec 0x41000, Ka-29 0x7ed 0x34000, others (Gazelle) 0x3f000
    (0x42ca03); only the Black Hawk turns its door to the target (+22.5° /
    +67.5°, 0x42b922 / 0x42b969) and starts its gun at -90° (0x42c379); door
    gun limits Gazelle ±45° yaw, -45..0° pitch, Black Hawk -135..-45° yaw,
    -16.9..0° pitch, any other type unclamped with no fire refusal (0x42bb5e,
    0x42bccf); rotor loop 503 for the Black Hawk, 502 for the Gazelle, Havoc,
    Hind and Ka-29 (0x42c8d4);
    the altitude is chosen at think time (0x42b520) and applied every tick
    (0x42c960: z += vz, vz += it, no cap; falling, it lands on max(ground,
    probe) + rest): floor = max(the object probe 0x445f50 from 3 m under
    the origin, refreshed every ~8 thinks, the terrain 4 m to each side, the
    water level), raised by samples 1, 2 and 4 s of speed ahead, the
    climb/descent ×k (1, 2 after the first raising sample, +1 per further
    one); the band moves by 0x100 << shift (down when sinking, up
    otherwise); too low +0x100·k (−0x100 above 0x7ff), too high −0x100·k
    (+0x100 under −0x7ff), in the band ∓0x80 − 0x4D; 7..11 tail hits climb,
    12+ −0x134. Hover 8 m / band 1 m, path 18 m / 4 m (shift 1, or the tail
    hits up to 5); the attack approach (only with lift) 18 m / 4 m shift 4
    beyond 80 m, else 8 m / 1 m shift 5, 7 in the 8..30 m hover band, by
    0x41e880's distance; landing, unlifted and destroyed ones fall to /
    settle on the probe floor (at least the water while intact);
  - **animals** (src/game/Wildlife.cpp; ai table 0x4af100, move table
    0x4c37xx): vultures `vult` (think 0x42dfe0 every 0x10 ticks, move
    0x42e1c0): 3 m/s at 72 m (40 m with a target), at least 20 m above
    the ground, climbing / sinking speed >> 7 a tick; home again (+ a random
    0..43.6°) beyond 200 m; ENV_VULTURE_CALL with chance 1/64; the corpse
    scan 0x42aeb0 (dead people of another side, not side 0, within 90° and
    999 m, in sight; 100/d × angle, ×2 but the player; × perception, ÷4
    relaxed) against rand takes a target (spotted 0x41ac00), which it
    circles (0x42df50: 3 m/s beyond 16 m, else 2 m/s with the yaw
    +0x18000000); destroyed: ENV_VULTURE_DEATH once, it falls under 0x134 a
    tick with its momentum and thuds (0x48) and bounces on the ground.
    Penguins `peng` (think 0x42f030 every 8, move 0x42f430): walk their path
    (0x4033d0) at 0.5 m/s, which the mover holds whatever the think wants
    (so one without a path walks straight on), hop into the water when the
    ground 2 s ahead is under it, dive (splash effect 4 + PENGUIN_INTO_WATER,
    0x45af50), swim at 2.25 m/s with a random ±22.5° heading now and then,
    climb out (PENGUIN_OUTOF_WTR) and level off; a waddle roll; destroyed:
    PENGUIN_DEATH (0x42a9f0) and tipping over. Fish `prnh` (think 0x42db20
    every 8, move 0x42de00): their path at 1 m/s with a tail-flick burst
    (+0xC0 = 0x100, the speed + (+0xC0 << 12)) 1/32 of thinks, 0.5 m under
    the surface (0x42d870); unless AI flag 1 (D05M06's eleven `fishy2` all
    have it, so they only hang under the surface) the scan 0x42ac80 (people
    of any side but 0 up to 1 m above the water, within the field of view
    and 99 m, sight from 1 m up; ×6 swimming, ÷256 otherwise) takes a
    swimmer, kept while he swims; the attack 0x42d920 lunges by the jaw
    within 4 m, steers ±0x11000000 off him and within 0.75 m, after its
    pause, bites with chance 1/8 (blast kind 4, 0x455180: 1 hp from every
    person within 2 m, the player too) with effect 0x10 and
    ENV_PIRANHA_BITE every time, backing off at -4 m/s; destroyed: belly
    up, rising to just under the surface (no husk: it stays drawn).
    INFERRED: the first think staggered as the markers' (+11 a record % 60);
    the penguin's flipper / foot phases and the vulture's wing beat (+0xB0)
    are not drawn;
  - **parachutes** (0x45bb10, 0x440b2b): people placed 100 m or more up,
    and the player and teleported teams at a start or teleport marker more
    than 0x62FFFF (99 m) above the ground: the marker pass GOG Df2.exe
    0x45c4b0 (CD 0x4544a0) flags those markers 0x20, which the start
    (0x460810) and the teleport (0x418be0) copy, descend 0x2222 a tick under
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
    flags}; GOG Df2.exe 0x4b1738, 112-byte entries, name +0x4C, key +0x6C), with scopeinc X, scopedec Z, NVG N and scopemag E added to OpenDF's actions.
- **DF2 status (2026-11-14):** playable through its own front end (`opendf
  --game games/DF2 play`); all 47 single-player and quick missions load and
  run. Still open, by area:
  - *the player's parachute* (settled 2026-11-15): earlier traces missed
    the marker pass GOG Df2.exe 0x45c4b0 / CD 0x4544a0 (its flag write goes
    through a pointer 0x28 into the record, so it reads as +0x3C); it sets
    flag 0x20 on start and teleport markers more than 99 m up, and the
    start and teleport copy it. Below that the player starts at the
    marker's height and drops, as DF2.EXE does;
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
  - *settled since* (mission, GOG Df2.exe 1.06.15): the single-player record
    filter (0x403790: flags 0x10 / 0x80 dropped, 0x20 / 0x40 kept, '4Team'
    items and side-3/4 records dropped with mp_numteams 2; the slot stays
    zeroed), so the player starts on the first loaded 6001 (D05M13 at
    (-149, -962); 0x460810 takes 106094 markers first, none in the data);
    the same filter in Df.exe (0x449710) puts DF1 C02M01 at (-191, -113);
    record yaw = int16 +0x34 and pitch = int16 +0x36 (both games, e.g. the
    tilted pungi stakes); kill team (0x416380) destroys every object of the
    team through its ai function, `null` / `dec0` excepted, and on team 1
    the player too (an organic of team 1, 0x4146f0, whose `plyr` 0x401fc0
    dies at hp <= 0 on any event: death move kind 1, no red flash); the
    campaign switches are kept when the mission is left with side 1 the
    winner and at the Space replay (0x464140 via 0x464190 / 0x466ad0), not
    at action 8 (0x41b700 case 8 only latches the winner), so the unlock
    switch a win event sets after its action 8 is kept (DF1 still copies
    them at action 8); events (0x41ad40, as Df.exe's 0x42e9e0): with a
    timer running an event only counts down and cannot fire in that
    visit; the scheduler 0x41ae50 (every 16th tick from the 16th) skips
    flag 2 and 4 events; flag-2 events are visited once at load
    (0x41aeb0) and flag-4 events when the mission is left, before the
    switches are saved (0x464140 -> 0x41aef0; single player sets the gate
    [0x67cd34]); condition 1/2 tests bit p2 of team p1 (0x41b180); one-point paths are marked stop-at-end (flags |= 1)
    at load (0x403e20, loop 0x4041b0: 356 of the 778 lack it in the
    files); condition 1/13
    "team hit team" (table 0xabbe00, set on a direct hit 0x453510 ->
    0x41ac30); area condition 1/10 (0x41b200, 0x4152a0): the first and last
    marker of the tag, both axes must differ, live positions of groups 0
    and 3 with no alive test, a height range between the markers' absolute
    heights when either record z is non-zero; pungi sticks (0x446050 case
    0x42a): hp -= 2 a tick in contact when airborne or running uncrouched,
    the player and AI alike (INFERRED for the AI: running = a Running move);
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
    the text when user1/user2 give no positive size), wrap GOG 0x41c0e0 /
    counter 0x41bf70 (a buffer cut at its last space or after the
    overflowing character, spaces after a break skipped, no '~'; lines 'A'
    height + 4 apart, the CD's 0x419d50 + 3); the button text 0x43d4c0: '&' removed by the
    set-text routine 0x42d7b0 with the key letter underlined by '_' one
    pixel lower (0x419fd0 left, 0x41a1f0 right-aligned to the box's left,
    0x41a0d0 centred at y + (h − 'A' height)/4), the plain box 0x469380
    (vertically centred on the tallest glyph, image buttons' top raised by
    0.4 × the 'X' height), the image box [x + 2 (+ user1 for D), x + w − 2],
    an image-less push button centred on x, the two stale justify locals,
    other types' box (x, y)−(x + w, y + 'A' height) without underline,
    owner draw after the default draw; character hot keys (0x42de50) and
    hotkey1/2 (GOG 0x432c50: the Windows virtual key, 13 Enter / 27 Escape
    in GOG's DF2.MNU; CD 0x42dea0: DirectInput scancodes, 1 Escape); the
    menu cursor CUR_TARG.TGA with hot spot (12, 12) (GOG 0x434000,
    0x4436a0); Choose Soldier's and Main's connector lines (0x436720,
    0x43a780; Main's help and line cleared when the hover ends, 0x43a700);
    the command map's scroll bars (creator 0x40c3f0
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
    headings no longer overlap; f_sml goes through the band scanner
    0x41ba90 -> 0x41b9a0 / 0x41b8a0 like f_LG and is drawn opaque by
    0x41bb90, advance width + 1, a code without a glyph as glyph 0, so
    F_SML640 (277x240, 14-row bands) loads instead of DF1's fixed
    16-pixel cells keeping F_SML800); the rain's lines test but do
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
    +0xAC, the player's distance (1024 m unless in the field of view,
    0x417830) stored whenever the scan returns a candidate, before the
    draw (0x401de5 / 0x401efe): 62 / 124 / 372, or 248 when alert; the ghillie suit hides a crouched or prone
    player who has not fired beyond 50 m and halves visibility within
    (0x415a69); BMS flag bits 0x800/0x1000/0x2000/0x4000 → attack everyone
    / (movement) / avoid water over 0.25 m / deaf; flag 1 blocks
    acquisition; script properties 16-19 set or clear them and 2/3/4/14/15
    can clear; night lowers perception 0.15; alert soldiers no longer turn
    while idle; DF1's water probe only with the
    avoid-water bit; any order holds position; no shot while a friend (the
    player included) is within 1 m (0x41e880) of the ray from 0.75 m up
    along the aim, between 1/256 m and the target distance (0x417650,
    0x401cd7). The radio is a team share
    (GOG Df2.exe 0x414350, from 0x401a69 with a target and 0x401d6b alert
    without one; every 0x1f0 ticks unless silent, timer (−rand) & 0x3f):
    mates of the side, not deaf, |dx|, |dy| ≤ 50 m and dx²+dy² ≤ 2500, on
    the same target get a fresher unseen time and last known point (bark 4
    when 0x1f0 ticks fresher) or learn it died (0x40, bark 5); mates with
    none take it (bark 4; with 0x40 too, bark 5); their state is not
    touched. A target seen dead keeps its slot with 0x40 (bark 5 if this
    soldier killed it, 0x4019d5) and the soldier runs the no-target logic
    (0x4018ea); the last known point follows the target while it is seen
    (0x4019f9). A relaxed soldier who hears a noise (0x415480) barks 2
    unless silent and thinks again in 16 (0x401e76..0x401e95). The dead-
    friend check is inside the scan (0x417830): own-side bodies, not with
    flag 0x200, while the alert at the scan's start is not 2, sight 1..2
    between the body origins and max + 11/32 min < 50 m: alert, team alert,
    bark 1; the relaxed quarter then no longer applies. A soldier ordered
    to cover the player (order 2) scores candidates targeting the player
    twice. Helicopters (0x42bff0) draw against the scan's score with
    perception once and mark their team as having spotted the target's;
    searchlights mark (light team, target team) (0x42ce2f). Enemies fire through DF2's
    tables (the BMS weapon byte's table is DF1's, file 0xc88c8; 0x414ad0),
    vehicle guns mode 53. Their shooting is DF1's (READ): the muzzle
    0x414f20 (= 0x46ad30: the mode's +0x08 item's vertex +0x30 at the posed
    hand; no shot without it), the aim 0x415de0 (= 0x46b838: from the body
    origin at the target's origin while it is seen, else the last known
    point with errMul 5, whole-metre distance, the error stored on the
    body; burst rounds along the body's angles ± r>>3 / r>>5), the keep-
    target sight line from 1 m up to the target's origin (0x401732), after
    which (seen or not) the full scan runs and a different best candidate
    scoring over 0x4000, without a random draw, becomes the target (GOG
    Df2.exe 0x40197f..0x4019fc, scan 0x417830; the scan gives nothing for
    a side-0 soldier without flag 0x200).
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
