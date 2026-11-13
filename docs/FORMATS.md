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
PFF3 files end with a short trailer ending in `KING`, which is ignored.

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
    hardpoint 12 bytes i32 x, y, z (/256)
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
| `0x10000`–`0x8000000` | unknown; common on buildings (probably damage/destruction states) |

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
| `.SBF` | `SBF0` | music bank with named tracks (`Intro`, …) |
| `.MNU`, `.BIN`, `.RC`, `.H`, `.I` | | menu system resources |
| `.ANM`, `.CHR` | | soldier animations / character data |
