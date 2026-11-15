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
| `DFSETUP/VIEWS.PFF` | 52 MB | **PFF2, encrypted**: ten `H_*.KDV` award videos, never played by DF.EXE (see "KDV video") |
| `DFSETUP/DFMUSIC.SBF` | 27 MB | `SBF0` music bank |
| `DFSETUP/INTRO.KDV` | 160 MB | intro video (KDV; 30 MB of video, then filler) |
| `DFSETUP/PACK.EXE` | | archive tool; holds the PFF2 cipher (`/ENCODE`) |
| `DFSETUP/CMPRSSH0.DLL` | | static-Huffman `compress_buffer`/`expand_buffer`; **not** used for PFF2 |

At install time the encrypted PFF2 packs are decoded by `PACK.EXE` and merged
into the installed `dfbase.pff`. OpenDF instead mounts them all directly, in
this order: `DFBASE, DF, MISSIONS, AUDIO, VIEWS`. DF.EXE looks a name up in
`dfbase.pff` first and in `df.pff` only when it is missing there, taking the
first directory entry of that name (GOG 0x48fc10, 0x48fd00, 0x49e590), so
with `DFBASE.PFF` mounted the first archive and the first entry win (the one
name both stock archives hold with different data, `_BACKUP_.PCX`, is never
read). The later games' archives keep later ones overriding earlier ones.

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

DF.EXE also writes PCX: the PrintScreen command (Shift+P in a mission,
table flag 0x40, command 2; Shift+P in the menus, GOG 0x40d9e0) saves the
screen to the first free `sshot000.pcx`..`sshot998.pcx` (GOG 0x473eb0;
the CD's 0x426afc `screen00.pcx`..`screen98.pcx`) in the game folder,
nothing when all exist. Header 0x0A, 5, 1, 8, window 0, 0, w − 1, h − 1,
75 dpi, bytes per line the width rounded up to even, palette info 1; an
8-bit screen as one plane plus the palette, a hi-colour one as three
planes (R, G, B rows) and no palette. Runs are at most 63 and stop at the
row's end; a lone byte with its top two bits set goes out as 0xC1, byte.
OpenDF writes the drawn window that way, 24-bit.

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
    vertex   8 bytes   i16 x, y, z; u16 mesh (v5 and v8, in a mesh whose flag byte
                       has bit 0: the part whose matrix places it, GOG Df.exe
                       0x469fc0, DF2.EXE 0x44b140)
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

Delta Force 2's v8 models (texture fields 12 bytes later) keep the sequence
as a signed 16-bit word at @0x20, and Df2.exe compares it with the
material's sequence byte (@0x34 + camouflage variant, or @0x37 for an alpha
material) sign-extended (GOG 0x444d3d / 0x444e9a), with no fallback by
name: a byte 0x80..0xFF never matches and the material is hidden (0x0800).
That hides `DF2PAR02`'s vest pieces (sequences 128..137) and one LOD of
`DF2FNT01`.

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

The GOG release (Df.exe 1.00.03.03p) stores ITEMS.DEF, LOOP.DEF and every
`.ANM` scrambled, with no header: the bytes reversed, then each XORed with the
low byte of s = rol(rol(s, 11) + s, 4) ^ 1 (stepped before each byte) from
s = 0x04960552 (descrambler 0x456eb0, called by the text parser 0x456720).
Descrambled they match the CD files byte for byte. DF2 uses the same stream
behind a `SCR\x01` header with seed 0x01234567. OpenDF undoes both in
`Vfs::read`.

Text: `begin "name"` … `end` blocks with `id`, `type` (decoration, building,
vehicle, person, object, marker, effect), `graphic` (`.3DI`), `husk`
(destroyed graphic), `hp A B`, `ai_function`, `move_function`, `chr_file` and
`anim_def` (people), and vehicle handling values.

After loading it DF.EXE renames every item that has a DFGAME.BIN "Item
Names" entry `STR_ITM%04i` (id − 100000) to that text (GOG 0x401490 ->
0x482960, CD 0x4496c0): 110 of the 306 items, e.g. hummer → "humvee",
sandbnk3 → "bunker, sand". Nothing looks items up by name afterwards;
OpenDF keeps the ITEMS.DEF name for its lookups and shows the new one
(`ItemDef::displayName`).

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

## Game executables (CD and GOG builds)

OpenDF reads static tables (weapons, key bindings, sound names, voice sets)
out of the user's own game executable (`src/core/ExeImage.*`). The executable
is found next to the PFF archives, any case: the CD's `DFSETUP/DF.EXE` /
`DF2SETUP/DF2.EXE`, or GOG's `Df.exe` / `Df2.exe` in the game folder (`app/`
when the GOG installer was unpacked with innoextract). The build is
recognised by file size and CRC-32; each build has its own set of table
addresses (`Df1ExeTables` / `Df2ExeTables`). Any other file (another patch
level, a regional or modified executable) is reported as an unknown build
and tried against each table set in turn (DF.EXE CD, GOG, then DF2.EXE CD,
GOG) by its key table (`move_forward` on the Up arrow); the first that fits
is used ("unknown build ...; its tables matched the DF.EXE (CD) set"), and
each table's own sanity check (weapons, keys, sounds) still accepts or
rejects it. When none fits, OpenDF uses its built-in values, as when the
executable is missing. The GOG release
is the reference: where its tables differ from the CD's, its values are used.

| Build | Size | CRC-32 |
|---|---|---|
| CD `DF.EXE` (v1.00.000) | 1,043,468 | `63B1DB57` |
| GOG `Df.exe` | 1,102,860 | `7CED8382` |
| CD `DF2.EXE` | 835,584 | `A0947236` |
| GOG `Df2.exe` | 921,600 | `4CED7D2C` |

Delta Force (the tables were matched through the code that uses them, CD /
GOG function):

| Table | CD | GOG | Found through | Differences in GOG's |
|---|---|---|---|---|
| player weapon modes (39 × 168) | `0x4E97D0` | `0x4F9934` | `0x470580` / `0x46bfb0` | none |
| guns (28 × 52) | `0x4E9060` | `0x4F9154` | `0x470580` / `0x46bfb0` | gun 2 magazine 1 (was 0); guns 4-6 (M89 rounds) power 1, falloff 5 (were 0, 10) |
| loadout rows (28) | `0x4E9610`, 16 bytes | `0x4F9704`, **20 bytes** | `0x470580` / `0x46bfb0`, rows read at r·0x10 / r·0x14 + 4 (GOG `0x484970`) | a fifth word (1-5, not read by OpenDF); the modes are the same |
| key bindings (44 bytes) | `0x4DF4F8` | `0x4FB300` | `0x41cbc0` / `0x41cd30` | 119 bindings (CD 121) |
| sounds (0x200 × 40) | `0x4EB194` | `0x4F2EA4` | `0x472e70` / `0x43e340` | ids 451-470 added (`AMAC1`..`AMAC10`, `DMAC1`..`DMAC10`) |
| voice sets (16 × 12) | `0x4F019C` | `0x4F7EAC` | `0x473f70` / `0x43f5a0` | none |

Delta Force 2:

| Table | CD | GOG | Found through | Differences in GOG's |
|---|---|---|---|---|
| weapon modes (57 × 92) | `0x4C7238` | `0x4DC5E0` | `0x46b230` / `0x4758f0` | mode 48 is a weapon (type 13, ammunition 38, item 4138); modes 55/56 changed; flag 0x100 on modes 31, 48 |
| weapon types (39 × 216) | `0x4C5150` | `0x4DA4F8` | `0x46b230` / `0x4758f0` | none past the names |
| ammunition | `0x4C4310`, 38 × 96 | `0x4D95B8`, **39 × 100** | `0x46b230` / `0x4758f0` | see below |
| drag curve (44 × 12) | `0x4C86B8` | `0x4DDA60` | `0x46b230` / `0x4758f0` | none |
| key bindings (0x200 records) | `0x4A5CA8`, 44 bytes | `0x4B1710`, **0x70 bytes** | `0x41c700` / `0x41e370` | 155 bindings (CD 120): name at +4, key at +0x24 in both; GOG adds a description at +0x30 |
| sounds (469 used of 1023 × 40) | `0x4B85A8` | `0x4CD7C8` | `0x45da50` / `0x467880` | none |
| voice sets (22 × 12) | `0x4B70FC` | `0x4CBCC0` | `0x448240` / `0x44f340` | none |

GOG `Df2.exe`'s ammunition records have a word inserted at `+0x0C` (values
1-10, not identified; OpenDF does not read it), so every CD field from
`+0x0C` on is 4 bytes later: velocity `+0x10`, power `+0x14`, falloff
`+0x18`, lifetime `+0x1C`, accuracy `+0x20`, sounds `+0x24..+0x30`, flags
`+0x34`, drag bands `+0x38..+0x5C`, grains `+0x60`. The flags hold the CD's
bits 16 higher (the bullet set-up, CD `0x44a6a0` / GOG `0x451ae0`, tests
`0x1, 0x10, 0x1000, 0x40, 0x800` / `0x10000, 0x100000, 0x10000000, 0x400000,
0x8000000`); OpenDF shifts them down and keeps the CD's bit values. Values
that changed: ammunition 4-6 accuracy 3 (was 5) and grains 180 (124); 12, 13
lifetime 1000 (600); 13 spare magazines 12 (18); 14 2 (3); 29 4 (3); 36 is
now 200-round magazines (n 2, accuracy 8); 37 40-round magazines (n 2,
accuracy 7, flags 0x2003); 38 is new.

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
  0x00 u32 type            ITEMS.DEF id − 100000; a type with no item is the first
                           ITEMS.DEF entry, 'null' (DF.EXE GOG 0x4828c0): C04M05's
                           two type-3034 objects ("inhut2" has the mistyped id 3034)
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
| `.KDV` | `xVDK` chunks | video; decoded, see "KDV video" below |
| `.BIK` | `BIK` + revision | RAD's Bink video (DF2, Land Warrior); decoded, see "Bink video" below |
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
Opcode 0x01 pushes its byte zero-extended (DF.EXE 0x4ab681, DF2.EXE 0x48b2c1).

Delta Force 2: DF2MUSIC.SBF (next to DF2.EXE) and DF2MUSIC.BIN (track "DF2Menu", 13 states:
0 Begin picks by Var00/Var02, 1 the intro (int201..204, toms1xx), 2..4 after a mission, 5..11 one
per page group, 12 the page switch). DF2.EXE's VM (0x48b1f0, table 0x48ac50) adds a switch,
opcode 0x35 (0x48b610; a nop in DF.EXE): operands `n, unused, entrySize, totalLength`, then n
entries; a value below n runs its entry's first instruction and, when that ends at the entry's end,
goes on after the switch (totalLength from the opcode); otherwise the switch is skipped. State 12
switches on Var01 (0→5, 1→6, 2/3→7, 4→5, 5→8, 6→9, 7→10, 8/9→7, 10→11), the page states pick
random stems with switches. `0x33 8` setVolume and `0x33 9` fadeTo (0x48b74a / 0x48b75c → 0x46c240
/ 0x46c280) set the fade and its target (0..255, 16.16); when a stem starts the step is
(target − fade) / (size / blockSize) (0x46bf86), and each queued block sets the stream volume to
master · fade >> 8 before stepping (0x46c050; the volume applies to the whole ring, 0x4a0372).
DF.EXE does the same (GOG Df.exe 0x4c4fa0 / 0x4c4fe0, 0x4c4ca0, 0x4c4d20 -> 0x494810 / 0x4b52dd):
with the fade full the stream volume is (255 · cd_volume) >> 8; its sub-ops 2..7 push their own
number and pop nothing (0x4b705d). The
menus write Var01 every frame from the current state (0x43cc20 from the main loop 0x45ecd8):
Campaign 1, MultiPlayer / JoinMultiPlayer / NovaWorld 2 (10 with 0x4cdf4c), Options1/2 3,
ChooseSoldier 4, Credits 5, MissionBriefing 6, ChangeGear 7, HostMultiPlayer 8, HostOptions1 9,
others 0. Without the switch every page state was skipped and the script fell back to Begin,
which with Var00 = 0 restarted the intro piece.

## KDV video (INTRO.KDV, VIEWS.PFF H_*.KDV)

NovaLogic's own video format, played by DF.EXE's hand-written "ANIMMGR"
module (0x486b0c open, 0x486c00 step, 0x487232 chunk loop, 0x487428 chunk
dispatch, 0x487894 / 0x487e45 codecs, 0x486c48 timing, 0x486d07 sound).
OpenDF: `src/formats/Kdv.{h,cpp}`, `opendf kdv FILE [OUTDIR [EVERY]]`.

A file is a flat run of chunks `{char tag[4], u32 size}` where `size`
includes the 8-byte header. Every tag ends in `K` (checked, 0x487849);
the loop ends when the bytes consumed reach the file size (0x487827).

| Tag | Contents |
|---|---|
| `xVDK` (0x1C bytes) | header: +0x08 width, +0x0C height, +0x10 frame count (of this clip), +0x14 frame period in DF.EXE's 120 Hz ticks, +0x18 version `0x22`, bit 31 set for 24-bit video. Another version: "ANIMMGR: Mismatched Player and KDV file" (0x48745a). A later `xVDK` of the same size just carries on (INTRO.KDV is two clips, 451 + 466 frames); a new size ends the step like a frame |
| `pVDK` (8 + 768) | palette, 256 × RGB, 8 bits a component (0x4874f8). Applied at the next flip: entries 1..254 from it, 255 white, 0 never set (0x48b1a4) |
| `aVDK` | mono sound: one `SAMP` block |
| `sVDK` | stereo sound: two `SAMP` blocks, left then right (the chunk may have 2 pad bytes after them) |
| `iVDK` | an 8-bit frame (0x487894) |
| `i42K` | a 24-bit frame (0x487e45) |

Any other tag: `ANIMMGR: "xxxx" Invalid Digital Video Token` with the tag
byte-reversed in the quotes (0x487691).

`SAMP` block: `"SAMP"`, u32 sample count, u32 rate field `0x10000000`
(((rate·16) / 22050) << 24, i.e. 22050 Hz, as in loaded WAVs), u8
attenuation shift (+0x0C, 0..8; 8 = silence), u8 0, u16 1, then the
unsigned 8-bit samples. Each frame's sound is exactly one frame long
(INTRO: 1470 samples at 8 ticks = 1/15 s; H_*: 1837 at 10 ticks = 1/12 s).

### Frame codec

Both codecs decode in place onto the previous frame (DF.EXE decodes into
its back buffer, 0x48aaac), in 4×4 blocks left to right, top to bottom.
Before every fourth block comes a type byte, two bits per block, low bits
first (the counter runs on across rows):

| Type | 8-bit (iVDK) | 24-bit (i42K) |
|---|---|---|
| 0 | 4 colours (4 bytes), then 16 two-bit indices (u32, low bits first, row-major) | 4 colours (not cached), then the u32 of indices |
| 1 | 2 colours, then a u16 mask (1 bit per pixel). If the two colour bytes are equal instead: 16 raw pixels follow the pair (18 bytes) | if the next 6 bytes are all 0: 48 raw bytes follow them (54 bytes), written as they are. Else 2 colours (cached), then a u16 mask |
| 2 | one colour byte for the whole block | one colour (cached) for the whole block |
| 3 | the last block's type 0/1/2 again with its colours: a new mask (0, 1) or nothing (2) | the same |

8-bit: index 0 is never written, so the old pixel shows through. 24-bit:
pixels are 3 bytes B, G, R (the DirectDraw 24-bit layout; the result
matches the footage, e.g. the red stripes of H_CAM's ribbon). A colour is
either one byte with bit 7 set, an index (bits 0..6) into a 128-entry
cache cleared at the start of every frame (0x487e61), or 3 bytes: blue =
first byte << 1 (7 bits), green, red. The literal colours of type 1 and 2
blocks go into the cache at a rolling index (0, 1, ... 127, 0); type 0's
do not. A pixel (types 0 and 1) or a type 2 block whose colour is 0, 0, 0
is not written (so real black is coded as red 1): DF.EXE decodes into a
32-bit target on any display of 24 bits or more (GOG 0x4913ec, 0x492725)
and tests the whole colour dword. DF.EXE has a second path for 16-bit displays (0x48859d,
the same stream converted to 16 bits); its 8-bit and 24-bit paths are the
ones that match the files.

### Player (DF.EXE)

- Opening (0x486b27): videos up to 320 wide get a 320×240 screen, wider
  ones 640×480; 8 bits for 8-bit video, else 32 (0x48b39c). The frame sits
  at the left, centred vertically, with its own width as the row stride.
- A step (0x486c00) reads chunks until a frame is decoded, then times it
  (0x486c48): the frame is due one period after the last. If it is late it
  is not shown, up to five in a row; the sixth is shown and the schedule
  restarts at the current time. If it is early the player waits until 3/4
  of a period before it is due, then flips (palette first if a new one
  arrived).
- Sound (0x486d07, after every frame, shown or not): the sound chunks read
  since the last frame went into a 16-buffer ring; only one is queued:
  stereo the oldest, mono the newest (so at the start of INTRO.KDV, where
  four sVDK chunks precede the first frame, three are dropped; all four are
  silent). It is dropped too if the mixer's stream queue holds 10 or more
  blocks (0x4a9c14). Queued as an interleaved block at 22050 Hz with gain
  (V>>1)>>shift per side (0x4a9c23), V = 255 (the stream volume before the
  menu music sets it, 0x48c9d7).
- The loop (0x48ff60(name, skippable)): the message pump, then if
  skippable one entry from the key queue (any WM_KEYDOWN / WM_SYSKEYDOWN,
  repeats included, 0x485a3a; the queue is emptied when the video starts,
  0x489e2c) ends it; no frames while the window lacks focus (0x490015);
  closing the program ends it. Mouse clicks do not skip. On close the
  stream queue is emptied (0x497eaf): the sound stops at once.

### Where the files are played

- **INTRO.KDV** (24-bit, 640×480, 917 frames at 15 fps, 61 s): the Start
  Screen's init (0x418b6c) plays it first (0x4019d4), skippable, then shows
  DF_LEAD. DF.EXE looks for `INTRO.KDV` in its own folder, else
  `<CD>\DFSETUP\INTRO.KDV`. Only the first 30 MB are video: after the
  second clip (file offset 0x1CD14F0) the file is filler (`\r\n\r\n` then
  bytes that are not chunks), so playback ends there with "Invalid KDV
  File", shown nowhere. A left click during the intro is seen by the Start
  Screen's mouse hook, registered before the intro (0x418b91), so DF_LEAD
  then fades at once; a second key press left in the queue does the same
  (0x418beb).
- **VIEWS.PFF `H_*.KDV`** (10 files, 640×480, 12 fps, 144..177 frames):
  award / medal presentations ("Awards", "Cambodia Joint Operations Seal",
  an Air Medal on a desk, ...); H_AIR, H_AMC, H_DSC, H_MSM, H_SLS (177
  frames) are 8-bit with three palettes (black, the picture, black),
  H_CAM, H_IND, H_KYR, H_MON, H_SAK (144) 24-bit. **DF.EXE never plays
  them**: ANIMMGR has one caller
  (0x401a27, INTRO.KDV), no `H_` or `.KDV` name other than INTRO.KDV is in
  the executable, and DF.EXE opens no VIEWS.PFF (the installer merges it
  into dfbase.pff); "medal" and "award" appear nowhere in it.

## Bink video (DF2INTRO.BIK, DFLWINT.BIK)

RAD Game Tools' format, not NovaLogic's: Delta Force 2 and Land Warrior
never decode it, they run RAD's `binkplay.exe` on the file (DF2.EXE
0x435830, docs/AUDIT.md F15). So this section describes the format, not an
EXE trace; OpenDF's decoder is its own (`src/formats/Bink.{h,cpp}`,
`opendf bink FILE [OUTDIR [EVERY|N,N,...]]`). Its constant tables (scan
order, run patterns, the 16 Huffman trees, both quantiser sets, the
block-type run lengths, the audio band edges and run lengths) were read
out of RAD's own player: Task Force Dagger's `TFDSETUP/BINKPLAY.EXE` is
UPX 1.20 (NRV2D) packed and unpacks to an image holding them (offsets in
Bink.cpp); DF2's and Land Warrior's binkplay.exe are Shrinker-packed and
were not unpacked. Revisions `c`..`k` of Bink 1 are decoded; `b` (other
bundles and DCT, used by very early files) is not. Checked against an
independent decoder (FFmpeg) on DF2INTRO.BIK (`f`), DFLWINT.BIK, TFD's
INT.BIK and DFBHDT1.BIK (`i`): every frame of all four is identical byte
for byte (Y, U, V), the RDFT soundtracks within 1 LSB and the DCT ones
(TFD) within 1 LSB.

| File | Header | Video | Audio |
|---|---|---|---|
| DF2 `DF2SETUP/DF2INTRO.BIK` | `BIKf` | 640×480, 1361 frames, 15 fps (90.7 s), one keyframe (frame 0) | 22050 Hz stereo RDFT (flags 0xE000) |
| Land Warrior `DFLSETUP/DFLWINT.BIK` | `BIKi` | 640×480, 1069 frames, 15 fps (71.3 s) | 22050 Hz stereo RDFT |
| TFD `TFDSETUP/INT.BIK` | `BIKi` | 640×480, 1476 frames, 2997/100 fps | 44100 Hz stereo DCT (0x7000) |

### Container

All values little-endian.

| Offset | Contents |
|---|---|
| 0x00 | `BIK` + revision letter |
| 0x04 | u32 file size − 8 |
| 0x08 | u32 frame count |
| 0x0C | u32 largest frame (bytes) |
| 0x10 | u32 frame count again |
| 0x14 / 0x18 | u32 width / height |
| 0x1C / 0x20 | u32 rate / scale: frames per second = rate / scale |
| 0x24 | u32 video flags: bit 20 an alpha plane, bit 17 grey (no chroma; INFERRED handling) |
| 0x28 | u32 audio track count T |
| 0x2C | T × u32 largest decoded audio packet (bytes) |
| … | T × (u16 sample rate, u16 flags: bit 13 stereo, bit 14 16-bit, bit 12 DCT transform, else RDFT) |
| … | T × u32 track id |
| … | (frames + 1) × u32 frame offsets; bit 0 marks a keyframe, the frame's size is the next offset minus this one (bits 0 cleared) |

A frame is, per audio track, a u32 size and that many bytes of audio
packet, then the video packet to the frame's end. An audio packet starts
with a u32: the bytes it decodes to (16-bit samples, all channels).

### Video

The bit reader takes bits least significant first (from 32-bit
little-endian words). From revision `i` the alpha and luma planes are each
preceded by a u32 (their size; skipped). Then the planes: alpha (if
flagged), Y, and the two chroma planes at half size: U then V before `h`,
V then U from `h` on. Each plane ends on a 32-bit boundary. Revision `k`
starts each plane with a bit: set, the whole plane is one 8-bit value.

A plane is 8×8 blocks (luma: (w+7)/8 × (h+7)/8; chroma: (w+15)/16 ×
(h+15)/16 of 8×8). Its header gives the Huffman trees of nine **bundles**,
streams of values the blocks consume in order:

| # | Bundle | Values |
|---|---|---|
| 0 | block types | 0..9; Huffman values 12..15 repeat the previous type 4, 8, 12, 32 times; or one 4-bit type for the whole run |
| 1 | sub-block types | the same, for the 8×8 inside a scaled block |
| 2 | colours | bytes: high nibble from one of 16 extra trees chosen by the previous high nibble, low nibble from the bundle's tree; before `i` the byte is sign-magnitude around 128; or one colour for the run |
| 3 | pattern | bytes from two Huffman nibbles (low first): 8 pixels of a pattern row |
| 4, 5 | x / y offsets | −15..15 (Huffman or a 4-bit value for all, sign bit if non-zero) |
| 6, 7 | intra / inter DC | 11-bit (intra) or 10-bit + sign (inter) start, then per group of 8 a 4-bit width and signed deltas of that width |
| 8 | runs | 0..15 (run length − 1) |

A tree is 4 bits choosing one of 16 fixed code trees (tree 0: plain 4-bit
values), then the order of its 16 symbols: explicitly (bit 1: 3-bit count −
1, that many 4-bit symbols, the rest in increasing order) or by 1..4 merge
passes over 0..15 (bit 0: 2-bit depth, then one bit per output symbol
choosing the left or right run). At the start of each row of blocks each
bundle whose earlier values are all used up reads a new run: a count (the
width grows with the plane width; 0 ends the bundle for the plane) and its
values.

Block types (each 8×8 unless noted; "previous" is the last frame's plane):

| Type | Block |
|---|---|
| 0 skip | copy from the previous frame |
| 1 scaled | a sub-block (type from bundle 1: run, intra, fill, pattern or raw) decoded at 8×8 and drawn at 16×16; its other three 8×8s carry no data (their type entries are skipped) |
| 2 motion | copy from the previous frame at (x, y) offset |
| 3 run | a 4-bit index into 16 pixel orders, then runs (bundle 8 + 1) each with a bit: one colour for the run, or a colour per pixel |
| 4 residue | motion copy, then a 7-bit count and coefficients by bit planes, added modulo 256 |
| 5 intra | DCT: DC from bundle 6, coefficients, 4-bit quantiser, IDCT |
| 6 fill | one colour |
| 7 inter | motion copy, then a DCT (DC from bundle 7) added modulo 256 |
| 8 pattern | two colours and 8 pattern rows |
| 9 raw | 64 colours |

DCT coefficients are coded by bit planes from a 4-bit top plane down over
a work list (the groups 4..23, 24..43, 44..63 and the single coefficients
1, 2, 3); a set bit per live entry per plane says something in it is that
large, groups split into groups of 4, coefficients become set (magnitude
bits + sign) or wait for a later plane. Dequantising multiplies by the
quantiser table entry (16 tables each for intra and inter, AAN prescale
included) and shifts right 11; the IDCT is the AAN factorisation in
integers (2896, 3784, 2217, −5352 over 2^11), columns then rows rounded by
1/256. Inter and residue blocks are added without clamping (modulo 256):
the encoder relies on it, e.g. a black pixel brightened by a negative
residual. Output is YUV 4:2:0; OpenDF converts with BT.601 studio-range
coefficients (INFERRED: binkplay's converter was not traced).

### Audio

Per track: frame length 2^9 (rate < 22050), 2^10 (< 44100) or 2^11
samples; RDFT tracks code stereo as one interleaved signal (rate × 2,
length × 2). The overlap is length/16. Band edges: Zwicker's critical bands
(100, 200, … 15500 Hz) scaled to the length, bands up to half the
(interleaved) rate. Quantiser i (0..95) = e^(0.15289·i) × root, root =
2 / (√length · 32768) for RDFT, √length / 32768 for DCT.

A packet is blocks, each 32-bit aligned (DCT blocks start with 2 unused
bits), per channel: two floats (5-bit exponent, 23-bit mantissa, sign) for
coefficients 0 and 1, an 8-bit quantiser per band, then the coefficients
from 2 in groups: a bit (0: 8 coefficients; 1: a 4-bit index into 2, 3,
4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64 times 8), a 4-bit width
(0: all zero), and per coefficient that many bits (+ sign if non-zero)
times its band's quantiser. The transform: RDFT, an inverse real DFT with
coefficients 0 and 1 the DC and Nyquist terms (halved) and then (cos, sin)
pairs; DCT, a DCT-III scaled by 2/length. Each block's first length/16
samples fade linearly from the previous block's last length/16 (which
are held back), so a block yields length − length/16 samples, as signed
16-bit.

### Player (OpenDF)

binkplay.exe is RAD's packed program and was not traced. OpenDF's
`FrontendDf2::playBinkVideo` decodes the movie where DF2.EXE starts
binkplay, shows each frame when due by rate / scale, centred with its
aspect kept, and sends the sound straight to the output (not through
DF2's mixer, which binkplay's sound never passed through). Any key or a
mouse click skips it (INFERRED); closing the window ends it. 640×480 is
decoded and converted at about 700 frames a second on an Apple-silicon
Mac.
