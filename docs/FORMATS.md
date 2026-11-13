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

**Unknowns / approximations in OpenDF:** world scale. OpenDF currently uses
8 m per map cell and 1.5 m per elevation step (adjustable with `[ ]`). The
real values should come from `DF.EXE` or from object placement in missions.
The exact meaning of `gamma`, `horizon` and `sun_slope` is also guessed.

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
    volume  72 bytes   collision box/plane (v5)
    material 76 bytes  name[12]; u32 flags @0x10; u8 flat RGB @0x28; u8 texture sequence @0x34
```

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

Text: `begin "name"` … `end` blocks with `id`, `type` (decoration, vehicle,
…), `graphic` (`.3DI`), `husk` (destroyed graphic), `hp`, `ai_function`.

## BMS mission (first look)

`C%02iM%02i.BMS` in `DFBASE.PFF`: `"BMS\r"`, title, author, terrain (`C2M1.TRN`),
dialogue set (`dfdlg02`), section counts at 0x98, then 96-byte object
records from 0x10C: u32 type (e.g. 2009 ↔ `ITEMS.DEF` id 102009?), u32 0,
u32 id, u32 0, i32 x, y, z (16.16; z is height above ground), … Positions
span about 1–2 km per mission. **The terrain cell size will be pinned down
by matching these to terrain features**; until then 8 m per cell is a guess.

## Other formats (identified, not yet decoded)

| Ext | Magic | Notes |
|---|---|---|
| `.JIM` | binary | mission waypoints; contains names like `1-START POINT`, `2-CP ALPHA` |
| `.PAL` | binary | mission map palettes |
| `.KDV` | `xVDK`/`sVDK`/`pVDK` chunks | video: 640×480, includes `SAMP` audio chunks |
| `.SBF` | `SBF0` | music bank with named tracks (`Intro`, …) |
| `.MNU`, `.BIN`, `.RC`, `.H`, `.I` | | menu system resources |
| `.ANM`, `.CHR` | | soldier animations / character data |
