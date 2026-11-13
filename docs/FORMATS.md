# Delta Force file formats

Notes from reverse engineering the US retail CD (v1.00.000, Oct 1998).
All integers are little-endian.

## Disc layout

| File | Size | Contents |
|---|---|---|
| `DFSETUP/DF.EXE` | 1 MB | game; opens only `df.pff` and `dfbase.pff` |
| `DFSETUP/DFBASE.PFF` | 92 MB | PFF3: UI PCX, palettes, menus (`DF.MNU`, `*.BIN`), mission briefings (`*.BMS`), animations (`*.ANM`), character set (`PLAYER01.CHR`), defs |
| `DFSETUP/DF.PFF` | 174 MB | PFF3: every object/soldier/weapon model (`*.3DI`, 362), terrains (`*.TRN` + `*_C/_D/_M.PCX`), sky/water textures |
| `DFSETUP/MISSIONS.PFF` | 19 MB | **PFF2, encrypted**: object models (`*.3DO`, 1729), object descriptors (`*.AI`, 431), waypoints (`*.JIM`, 85) |
| `DFSETUP/AUDIO.PFF` | 39 MB | **PFF2, encrypted**: speech/SFX `*.WAV` (987), `*.MID` (100) |
| `DFSETUP/VIEWS.PFF` | 52 MB | **PFF2, encrypted**: `H_*.KDV` campaign briefing videos |
| `DFSETUP/DFMUSIC.SBF` | 27 MB | `SBF0` music bank |
| `DFSETUP/INTRO.KDV` | 160 MB | intro video (`xVDK` container) |
| `DFSETUP/PACK.EXE` | | archive tool; holds the PFF2 cipher (`/ENCODE`) |
| `DFSETUP/CMPRSSH0.DLL` | | static-Huffman `compress_buffer`/`expand_buffer`; **not** used for PFF2 |

At install time the encrypted PFF2 packs are decoded by `PACK.EXE` and merged
into the installed `dfbase.pff`. OpenDF instead mounts them all directly, in
this order, with later archives overriding earlier ones:
`DFBASE, DF, MISSIONS, AUDIO, VIEWS`.

## PFF archive

```
header (20 bytes)
  u32 headerSize      = 20
  char magic[4]       = "PFF2" | "PFF3"
  u32 fileCount
  u32 recordSize      = 32
  u32 tableOffset     (entry table usually at end of file)
entry (recordSize bytes)
  u32 flag            PFF3: 0 = live, nonzero = deleted. PFF2: per-entry checksum
  u32 offset
  u32 size
  u32 timestamp       (unix time)
  char name[16]       NUL-terminated, 8.3, upper case
```
PFF3 files end with a 12-byte trailer after the entry table whose last four
bytes are `KING`; PFF2 files end with a 4-byte trailer. Both are ignored when
reading, but `opendf import` keeps them so an imported archive is byte-identical
to an honest copy.

### Fake sizes on the Delta Force 2 CD

As copy protection, the Delta Force 2 CD's directory lists `DF2.PFF`,
`TERRAINS.PFF`, `AUDIO.PFF`, `MISSIONS.PFF` and `DF2INTRO.BIK` at about ten
times their real size: each listed size is the real size followed by one extra
digit (`TERRAINS.PFF`: 3,190,097,151 listed, 319,009,715 real), about 5.4 GB on a
700 MB disc. A plain copy fails or reads into the files that follow. `opendf
import` (src/core/Import.cpp) takes the real size from the archive's own entry
table plus trailer, and a Bink video's from its header.

### PFF2 encryption

Every PFF2 entry is XOR'd with a keystream that restarts at the beginning of
each file (recovered from `PACK.EXE` at `0x401000`):

```c
uint32_t k = 0x01E177CE;
for (i = 0; i < size; i++) {
    k = (k << 7) | (k >> 25);   // rol 7
    data[i] ^= (uint8_t)k;
}
```
Because rol-7 has period 32 on a 32-bit word, the keystream is a fixed
32-byte pattern beginning `00 78 2E 7C 70 07 C2 77 …`.

## PCX

Standard ZSoft PCX v5, 8 bpp, one plane, RLE, with a 256-colour VGA palette
after a `0x0C` marker at the end.

## TRN terrain descriptor

Text, `key value` per line, `//` comments. Example (`DFG1.TRN`):

```
color_map    dfg1_c     1024x1024 colour map (shading baked in), own palette
elev_map     dfg1_d     1024x1024 8-bit heights
detail_map   dfg1_m     1024x1024 detail-tile index per cell (values 64..255)
detail_color snip_c     16x4096 = 256 stacked 16x16 tiles; index 0 = transparent
detail_elev  snip_d     same layout; micro-elevation around 128
detail_shade snip_s     same layout; brightness, ~120 = neutral
sky_map      clouds06   512x512 cloud layer, values 127..255
sky_palette  skygrad4   palette entries 0..15 = horizon -> zenith gradient
sky_height   1236
water_map    aquajonw   256x256 water texture
water_palette awuajonp  (typo in the original; missing file, game falls back)
water_height 0
water_opacity 0.6
filter       120,130,128   RGB tint, 128 = neutral
gamma        140
saturation   115
sun_slope    90
type R G B material      maps colour-map colours to footstep/terrain materials
```

Terrains are 1024×1024 and **wrap around** in both directions.

**Scale:** 1 m per map pixel and about 0.25 m per elevation step; see
*World scale and orientation* below. The exact meaning of `gamma`, `horizon`
and `sun_slope` is still guessed.

## 3DI model (objects, vehicles, soldiers, weapons)

Every in-game graphic is a `.3DI` in `DF.PFF`. `ITEMS.DEF` maps each item to
one with its `graphic` key (`graphic humvee` → `HUMVEE.3DI`). Versions on the
disc: v5 (354 files), v3 (5), v2 (1). Layout cross-checked with the
community converter [Nova3di](https://github.com/Novahq-net/Nova3di) and
verified byte-for-byte against all files: each LOD's computed size matches its
stored size, and the last LOD ends exactly at end of file.

```
header (0x80 bytes)
  char  magic[3] = "3DI";  u8 version
  char  name[12]
  u32 @0x14  LOD count
  u32 @0x7C  texture count
texture × count (starts at 0x80)
  char name[12]; 4 unused
  u32  @0x10 pixel bytes (= w*h on this disc; one frame each)
  u8   @0x14 sequence      materials reference textures by this, not by index
  u8   @0x16 flags         0x20 = palette index 0 is transparent
  u16  @0x18 width; u16 @0x1A height
  u8   pixels[size]        8-bit indices, rows top-down
  u8   palette[256][4]     R, G, B, unused
LOD × count, each: header (v5: 184 bytes, v2-v4: 176) then data
  u32 @0x14 data size     next LOD = header + headerSize + dataSize
  u32 @0x80 vertices, @0x88 normals, @0x90 faces, @0x98 meshes,
      @0xA0 hardpoints, @0xA8 materials, @0xB0 collision volumes (v5 only)
  data, in order:
    vertex   8 bytes   i16 x, y, z; u16 mesh
    normal   8 bytes   i16 x, y, z (/16384); i16 flags
    face    72 bytes   u16 flags; i32 u[3] @0x04, v[3] @0x10 (/65536, may exceed 1 = tiling);
                       i16 vertex[3] @0x1C, normal[3] @0x22 (relative to the mesh);
                       u32 material @0x44
    mesh   112 bytes (v5; v3-4 104, v2 88)  u32 vertices @0x04, faces @0x0C, normals @0x14
                       faces are consumed mesh by mesh in order; add the running
                       vertex/normal totals of earlier meshes to the indices
    hardpoint 12 bytes i32 x, y, z (16.16 m; first-person muzzles: DF.EXE 0x44b4a0)
    volume  72 bytes   collision volume (v5), all values 16.16 metres in file axes:
                       u32 type (1 box, 3 cylinder); 0; centre x, y, z @0x08;
                       radii @0x14; half-extents x, y, z @0x20; 0;
                       box xmin, xmax, ymin, ymax, zmin, zmax @0x30
                       (e.g. the rope bridge has 24: posts and deck planks)
    material 76 bytes  name[12]; u32 flags @0x10; u8 flat RGB @0x28; u8 texture sequence @0x34
```

**Character models** (soldiers, hostages) have their origin at the hips (for
`PLAYER01`, z runs from −1.01 m to +0.82 m), so they have to be raised by
their leg length. Everything else sits on its origin.

**First-person weapons** are the `P*` models (`PM4`, `PM40`, `PMP5`, `PM249`,
`PM19`): one LOD, the gun with the hand holding it.

**Units and axes:** vertices are 1/256 metre (soldier 470 units, about 1.84 m;
Humvee 1236, about 4.8 m), **+Z up, +X forward, +Y left**. OpenDF maps
(x, y, z) to world (x, z, −y).

**Material flags seen** (meaning as far as known):

| Bit | Use |
|---|---|
| `0x0001` | textured |
| `0x0002` | cut-out (foliage, fences) |
| `0x0004` | ground shadow / crate details |
| `0x0008` | effect trails, hidden |
| `0x0020` | muzzle flash (animated `AMGF01_A`, `_A02`, …), shown only when firing |
| `0x0040` | slightly darkened |
| `0x0080` | with `0x0001`: 50% translucent |
| `0x0800` | invisible (collision-only geometry) |
| `0x8000` | multiply blend: shadows under vehicles, rotor-blur discs, window panes |
| `0x10000`–`0x8000000` | impact surface for rounds (DF.EXE 0x44f90a, first match): 0x10000 metal, 0x20000 wood, 0x40000 stone, 0x80000 bush, 0x04000000 armour, 0x08000000 canvas; 0x01000000 = rounds pass through the face (0x44ec29) |

## 3DO + AI (legacy object library)

`MISSIONS.PFF` holds 1,729 `.3DO` models with `_H/_M/_L` LODs and 431 `.AI`
text descriptors (model per camo: `>g` green, `>d` desert, `>s` snow; plot
and move routines; collision flags). About 70% of the textures they reference
are **not on the disc**: tanks (M1A2, T-80, T-90, BMP), Comanche cockpit
parts and so on, apparently NovaLogic's shared library from other titles.
Delta Force itself draws `.3DI` via `ITEMS.DEF`, so OpenDF ignores these for now.

```
3DO header (92 bytes)
  "3DO1", 01 01 00 00, name[8], u32 radius (16.16), i32 bbox[6] (16.16)
  u32 textures, vertices, faces, normals, hardpoints, ?
  u32 offsets[6] to: texture table (28-byte entries: name[12] + 16 bytes),
      vertices (44 bytes: i32 x,y,z 16.16; 5 × 0; i32 u,v 16.16; 0),
      faces (32 bytes: 0, texture byte-offset, 3 vertex byte-offsets, 3 normal byte-offsets),
      normals (16 bytes), extra (12-byte points)
```

## ITEMS.DEF

Text: `begin "name"` … `end` blocks with `id`, `type` (decoration, building,
vehicle, person, object, marker, effect), `graphic` (`.3DI`), `husk`
(destroyed graphic), `hp A B`, `ai_function`, `move_function`, `chr_file` and
`anim_def` (people), and vehicle handling values.

`hp A B`: A is hit points (every person has 1). B looks like a damage class:
0 for soft things such as crates and barrels, 1 for vehicles, 2 for trees.
OpenDF lets bullets damage class 0 only; the other classes presumably need
explosives. This reading is inferred from which items use which value.

## DF.EXE player weapon table

`DF.EXE` holds the player weapons as static data: 39 records of 168 bytes at
virtual address `0x4E97D0` (pointer stored in `0xB7A954` at start-up; the
debug command `movegun` edits `+0x78..+0x80` and prints "Move Gun: %i, %i, %i").
Record order matches the weapon name list in `DFGAME.BIN` (`STR_PWNAME%02i`):
0 No Weapon, 1 Knife, 2 Knife Thrown, 3 Garrote, 4 HS .22, 5 SpecOps .45,
6/7 M4 burst/single, 8/9 M16, 10 M203, 11 Shotgun, 12–14 SAW, 15 Barret,
16 M89, 17–19 MP5, 20–22 MP5 SD, 23/24 M11, 25 Stinger, 26 RPG, 27 Laser
Designator, 28 LAW, 29 Satchel, 30 Radio Detonator, 31 Camera, 32 M40,
33 Frag, 34 Smoke, 35/36 AK-47, 37/38 Claymore.

| Offset | Meaning |
|---|---|
| `+0x00` | weapon number |
| `+0x04` | name, filled in at run time |
| `+0x64` | first-person model item id − 100000 (4057 = `PM4`) |
| `+0x68` | third-person model item id − 100000 |
| `+0x78`, `+0x7C`, `+0x80` | **first-person model offset from the eye**: forward, left, up in 1/256 m (M4: 72, −44, −75) |
| `+0x9C`, `+0xA4` | not yet identified |
| `+0xA0` | scope magnification (M4 4, M40 8, 0 for weapons without a scope; inferred from the values) |

The engine draws the `P*` model **unrotated** at that offset with the world
camera: 90° horizontal field of view across the 640×362 3D view (about 58.9°
vertical). Rendering it that way reproduces footage of the original exactly
(`docs/compare-weapon-original.png`, original left).

## F_SML640.PCX (HUD font)

256×256, one 16-pixel line per 16 characters (line = code / 16, so line 2 is
space to `/`), glyphs packed proportionally from x = 9. Ink is palette index
9; index 3 (magenta) marks the extent of glyphs with little or no ink (space,
`!`, `'`, `.`); index 1 (green) is the line separator. Runs of ink or marker
columns give exactly 16 glyphs per text line. `F_SML320/400/512/800` are the
same font for other screen resolutions.

## BMS mission

`C%02iM%02i.BMS` in `DFBASE.PFF` (62 missions: campaigns 2–6 plus the `C08`
multiplayer maps).

```
0x00  "BMS\r"
0x04  char title[40]       "C02M01 - Clear Camp"
0x2C  char author[40]
0x54  char terrain[16]     "C2M1.TRN" (each mission has its own TRN, usually
                           pointing at a shared DFxx map)
0x64  char dialogue[16]    "dfdlg02"
0x98  u32 counts[...]      the first four add up to the object count
0x10C object records, 96 bytes each, until a zero type
  0x00 u32 type            ITEMS.DEF id − 100000 (one item, 3034, is stored as-is)
  0x08 u32 id              unique within the mission
  0x0C u32 flags           team/state bits (0, 0x10, 0x100, 0x102, 0x112 …; not decoded)
  0x10 i32 x               16.16, metres east
  0x14 i32 y               16.16, metres north
  0x18 i32 z               16.16, metres above the ground
  0x1C … 0x30              mostly constant (4; 100, 100, 100; 0x10; 0x400): AI parameters?
  0x34 u32 heading         degrees clockwise from north; the model's +X points this way
  0x38 … 0x4C              AI/behaviour data (not decoded)
after the records: triggers/objectives (not decoded)
```

Invisible items (`type marker` in `ITEMS.DEF`) include `start, player`
(106001), waypoints, area triggers, multiplayer starts and ambient sound
emitters. OpenDF starts the camera at the first `start, player`.

## World scale and orientation (calibrated)

- **1 mission unit = 1 terrain pixel = 1 metre.** Mission (x, y) maps to
  terrain column x, row −y, wrapping every 1024. Evidence:
  - Over 795 buildings in all missions, the heightmap under each building is
    12× flatter than the map average with this mapping. The designers
    flattened building pads. Every other combination of scale (⅛–2),
    origin and axis flips tested scores no better than chance.
  - The `C08` multiplayer maps place objects across ±1024 units, one whole terrain.
  - Barbed-wire rolls (3.08 m long models) are laid in lines 2.84 units apart.
  - Rope bridges (44 m models) start on a canyon rim and span the gorge
    in their heading's direction.
- **Height:** 0.25 m per elevation step (estimate). The heightmaps change by a
  median of 1 step per pixel (90th percentile 3, 99th 7). At 0.25 m/step that
  is about 14° typical ground, with only canyon walls steeper than 45°. Larger
  values make ordinary terrain unwalkable. The exact engine constant has not
  been found yet.
- Terrains are therefore about 1 km square and tile endlessly. Some missions
  have objects several km out, which land on wrapped copies of the map.

## Other formats (identified, not yet decoded)

| Ext | Magic | Notes |
|---|---|---|
| `.JIM` | binary | briefing-map annotations: 9-byte points (u8 kind, u16 x, u16 y, u32 n) then 16-byte names like `2-CP ALPHA`; for the M7 briefing screen |
| `.PAL` | binary | mission map palettes |
| `.KDV` | `xVDK`/`sVDK`/`pVDK` chunks | video: 640×480, includes `SAMP` audio chunks |
| `.MNU`, `.BIN`, `.RC`, `.H`, `.I` | | menu system resources |
| `.ANM`, `.CHR` | | soldier animations / character data |

## .ANM move tables

Plain text, one move per line: `name animation-number velocity [override]`,
`//` comments. The name is one of DF.EXE's fixed move names
(`Standing_2HW`, `Walking0_1HW` .. `Walking7_1HW`, `Running0_2HW`,
`Crawling_CR0` .. `Crawling_CR7`, `Swimming`, `Death_B_P03DF`, ...). The
animation number indexes the soldier's `.CHR` file; the velocity is in m/s
(the file's own header says so). Directions 0..7: forward, left-forward,
left, left-back, back, right-back, right, right-forward. `1HW`/`2HW` = one-
or two-handed weapon; `_S_` = while shooting. Each soldier item in ITEMS.DEF
names its table with `anim_def` and its animation file with `chr_file`.

## Mission coordinates and the grid

BMS positions are metres (16.16) in one continuous space, x east and y
north; missions span several kilometres while the 1024 m terrain repeats
underneath. The HUD grid (DF.EXE 0x43DCE0) is 100 m squares with its corner
at (−1000, +1000): letter(s) from ⌊(x + 1000) / 100⌋ (A..Z, then AA, AB, ...;
negative values get +70200 m first), number ⌊(1000 − y) / 100⌋ + 1
(negative values get +10000 m first).

## BMS layout (DF.EXE loader 0x440490)

| Part | Size |
|---|---|
| Header | 0x10C: "BMS\r", title @0x04, author @0x2C, terrain @0x54, dialogue @0x64, object counts of four groups @0x98..0xA4, route name numbers @0xAC (8 × u32) |
| Objects | four groups of 96-byte records |
| Paths | 128 × 0x88: u32 flags (bit 1: player route), u32 count, 32 × u32 indices into group 2 |
| Teams | 64 × 32 bytes |
| Names | 32 × 20 bytes (empty in the shipped missions) |
| Events | u32 counts (events, conditions, actions); events 24 bytes, conditions 32, actions 32 |

Object record fields found so far: +0x00 type, +0x08 id, +0x10/14/18 position
(16.16 m), +0x1C waypoint arrival radius (m), +0x34 heading, +0x43 side,
+0x48 team, +0x49 path, +0x4A start waypoint / area tag.

## BMS events

Event (24 bytes): +0 flags (bit 0 repeat, bit 2 at mission start only, bit 4
at mission end only), +4 first condition, +8 first action, dword 3 = repeat
interval << 16, dword 4 = delay << 16 (ticks), +0x15 condition count, +0x16
action count. Condition (32 bytes): flags (bit 0 NOT, bit 1 OR / bit 2 XOR
with the next), class, subtype, p1, p2, p3. Action (32 bytes): unused, type,
subtype, p1, p2. Meanings: docs/VERIFICATION.md and src/game/Script.cpp.

## DFDLGnn.DBF

"DLG0" header (32 bytes), then per dialog: name[24], part count at +0x18,
parts from +0x34, each 0x44 bytes: clip name[24], text field[16] ("_" +
DFDLGnn.BIN entry, or "##" for no subtitle). Audio in DFDLGnn.PWF.

## RTXT key block

Header field 1 is the offset of a key block: u32 section count, per section
(u32 offset of its first key name from block start + 4, u32 entry count),
the section names, then one key name per entry. DF.EXE finds text by
section and key, e.g. "Waypoint Names" / "WPN008" = "IP".

## .JIM

`count:u8`, then 9-byte waypoints (`x:u16 y:u16 index:u32 0x7F`), further
records, then 21-byte names ("Player", "1-Start Point", "2-CP ALPHA", ...).
Named per terrain (C2M1.JIM), including terrains with no mission; DF.EXE
does not reference them, so they look like a leftover of the mission tools.
Coordinate units not decoded.

## .CHR (soldier skeleton and animations)

PLAYER01.CHR, shared by all soldiers (ITEMS.DEF `chr_file`). Loader 0x467810,
load-time scale ×0x1d2b0/65536 (0x469270), posing 0x4701f4 / 0x46df64.

- Header: "CHR1" (frame header 0x34) or "CHR0" (0x24); +4 part array, +8
  part count (15), +0xC part order list, +0x10 its count, +0x14 animation
  table (255 entries), +0x18 animation count.
- Part (0x5c bytes): vertex/face/normal arrays and counts (its own skinned
  mesh), +0x1c joint offset from the parent, +0x28 hit-sphere centre,
  +0x34 radius, +0x38 parent (−1 root), +0x3c bounding box. Parts: 0 pelvis
  (origin), 1 torso, 2 head, 3/4 upper arms, 5/6 forearms, 7/8 hands,
  9/10 thighs, 11/12 shins, 13/14 feet.
- Axes: x lateral, y up, z backward; model = (−z, −x, y).
- Animation (number = the ANM's animation number): frame count, part count,
  contact-point count (4: hands, feet), …; frames chained by a next offset;
  frame: flags (bit 16+k: contact k not touching), u16 ticks, root-motion
  offset, anchor contact index, contact-point array, then per part three
  angle bytes (256 per turn: yaw = −a0, pitch = a1, roll = −a2) + pad.
- Posing: positions chain through parents (joint offsets transformed by the
  parent matrix), rotations are absolute per part; M = T·Rz(yaw)·R(pitch)·Rx(roll).
- Ground contact: the origin sits above the lowest touching contact point.
- Hit spheres: part matrix × raw centre (no axis remap, as DF.EXE does).

## PWF sound banks (SNIPER.PWF, DFDLGnn.PWF)

Header: u32 header size (0x1C), "PWF2" (or "PWF1"), u32 sample count, …,
+0x10 offset of the definitions block (PWF2 only). Sample entries (52 bytes
from the header size): name[24], u32 offset, u32 size. A sample is either a
RIFF WAV (PWF1) or DF.EXE's converted form "SAMP" (0x48cfb0): u32 byte count
at +4, rate byte at +0xB = rate·16/22050 (0x10 = 22050 Hz, 0x08 = 11025 Hz),
sample shift +0xC (0), unsigned 8-bit mono PCM from +0x10 (the block keeps the
original WAV's size).

Definitions block: {u32 0x14, u32 count, u32 offset}; definition 0x50 bytes:
name at +4, pitch base +0x1C and random range +0x20 (16.16), layer count +0x24,
8 layer offsets +0x28, maximum distance +0x48 (m), flags +0x4C. Layer 0x30:
u16 variants, u16 round-robin index, i32 range (m), u32 flags (0x1 positional
pan, 0x10 variants in turn, 0x2/0x4 against the play mask 6), u32, 8 variant
offsets at +0x10. Variant 28 bytes: u32 sample, pitch base and random range
(16.16), gain, cap.

LOOP.DEF: `"name" "wav" volume clip maxdist pitcha pitchb` (loose WAV files).

## DFMUSIC.SBF and DFMUSIC.BIN (menu music)

DFMUSIC.SBF ("SBF0", read from disk next to DF.EXE):

```
0x00 "SBF0"  0x04 u32 0x100  0x08 u32 0  0x0C u32 0  0x10 u32 tableOffset (0x18)  0x14 u32 count (82)
table: count x 32 bytes { char name[16]; u32 offset; u32 size; u32 blockSize (0x1008); u32 0 }
block (0x1008 bytes): u32 nbytes (4096, less in the last block); u8 shiftL; u8 shiftR; u16 unused;
                      u8 pcm[4096] unsigned 8-bit stereo L,R at 22050 Hz
```

The gain of a block is (V>>1)>>shift per channel, V the stream volume 0..255.

DFMUSIC.BIN ("SCR0", in DFBASE.PFF) is a compiled music script:

```
0x00 "SCR0"  0x04 u32 0x100  0x08 u32 trackCount (1)  0x0C u32 offset of the track-offset table (0x20)
track header "MU00" (file 0x24), offsets relative to it:
  +0x08 name "DFMenu"  +0x20 codeBase (0x80)  +0x24 stateTable (0x68)  +0x28 stateCount (6)
  +0x2C defaultState (0)
states (track-relative): Begin, BasicLoop, Lost, Won, Cancelled, FromMP
```

Byte-coded stack VM (DF.EXE 0x4ab5ce, table 0x4ab070); jumps are relative to the code base, calls
and states to the track. Globals Var00..Var15 at byte offsets 0x00..0x3C, `i` 0x40, `lastTime`
0x44; the game writes Var0n with 0x4b8570. The full opcode table is in src/audio/Music.cpp.
