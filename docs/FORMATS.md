# Delta Force file formats

Notes from reverse engineering the US retail CD (v1.00.000, Oct 1998).
All integers are little-endian.

## Disc layout

| File | Size | Contents |
|---|---|---|
| `DFSETUP/DF.EXE` | 1 MB | game; opens only `df.pff` and `dfbase.pff` |
| `DFSETUP/DFBASE.PFF` | 92 MB | PFF3: UI PCX, palettes, menus (`DF.MNU`, `*.BIN`), mission briefings (`*.BMS`), animations (`*.ANM`), character set (`PLAYER01.CHR`), defs |
| `DFSETUP/DF.PFF` | 174 MB | PFF3: soldier/weapon models (`*.3DI`, 362), terrains (`*.TRN` + `*_C/_D/_M.PCX`), sky/water textures |
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

## Other formats (identified, not yet decoded)

| Ext | Magic | Notes |
|---|---|---|
| `.3DO` | `3DO1` | static object models with `_H/_M/_L` LODs, referenced by `.AI` |
| `.3DI` | `3DI\x05` + name | character/weapon models with textures (likely embedded) |
| `.AI` | text | object descriptor: model LODs per camo (`>g` green, `>d` desert, `>s` snow), plot/move routines, collision flags |
| `.JIM` | binary | mission waypoints; contains names like `1-START POINT`, `2-CP ALPHA` |
| `.BMS` | binary | mission files (`CxxMyy`) |
| `.PAL` | binary | mission map palettes |
| `.KDV` | `xVDK`/`sVDK`/`pVDK` chunks | video: 640×480, includes `SAMP` audio chunks |
| `.SBF` | `SBF0` | music bank with named tracks (`Intro`, …) |
| `.MNU`, `.BIN`, `.RC`, `.H`, `.I` | | menu system resources |
| `.ANM`, `.CHR` | | soldier animations / character data |
