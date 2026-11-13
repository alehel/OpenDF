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
| T3 | Stance icon | fixed | POS%i of the display mode, 1:1, centred x 50, top 3.75 (0x484c74, 0x4846f8) |
| T4 | Weapon name | fixed | (75, 10); red while reloading or out of ammunition (0x44b0a0) |
| T5 | Magazine / rounds boxes, magazine icon | fixed | boxes 75-135 / 140-200; the icon is font character 1 (was a guessed line); boxes have no fill, 2-pixel dark scanlines, HUD-green outline (0x401de4) |
| T6 | "Round chambered" bar | fixed | x 205-210, solid when chambered, empty box otherwise (0x483558); chambered = rounds > 0 and not reloading (inferred) |
| T7 | Weapon silhouette | fixed | WEAP%i of the display mode, 1:1, right edge x 325, top 16.25 (0x484ae8) |
| T8 | Compass | fixed | (382,30) r 20; letters are font characters 2-5; inner circle r 5 + needle 10 in team colour; dot r 2 bright green (0x483d9c) |
| T9 | Grid / Carrying / WP boxes | fixed | positions from 0x4836b8 / 0x483718 / 0x483d48; "None" without a route |
| T10 | Font and size | fixed | F_SML%i with the width capped at 800 (0x40298c), 1:1 in the original display mode that fits the window (640x480 / 800x600 / 1024x768); the footage was recorded at 800x600 |

## Bottom bar

| # | Item | Status | Notes |
|---|---|---|---|
| B1 | Messages | fixed | x 6.25, y 430/445/460, three lines, white, 1240 ticks each (0x483390, 0x42f400); were grey, 12 s, 14-unit spacing |
| B2 | Soldier name / mission name | fixed | (437.5, 426.25 / 441.9); "OPERATIVE" was invented: the name comes from the soldier profile, `--name` until the soldier screens exist |

## 3D view

| # | Item | Status | Notes |
|---|---|---|---|
| V1 | Crosshair | fixed | bright green one-pixel arms 4..10 / 5..11 from the centre (0x401f94) |
| V2 | Bullet impacts (puffs) | fixed | invented puffs replaced by DF.EXE effect objects: see X6 |
| V3 | Gun bob, reload lowering, recoil push, muzzle flash time | fixed | see V2b |
| V6 | Water | fixed | DF.EXE 0x431b3e/0x431d04/0x43bfd8/0x43bd7a/0x43c500: none when TRN water_height is 0; pixel = T[(1−op)·lum + op·4r] with lum = (R+2G+B)/4 of what is already drawn, r = water texel >> 2, T = 256-step gradient from water palette entries 0..15 (through the TRN colour pipeline); 16 texels/m, waves warp ±4 texels (period 16 m, phase 2 table entries per tick); no fog; drawn last, out to 1023 m; under water the whole view is tinted through r = 0. Verified on C02M03 |
| V4 | Terrain colour, fog, sky, view distance | fixed | no sun lighting (sun_slope unused); near rings (< 100 m·F/320): bilinear colour map, detail shade toward black / sky colour 16, detail colour averaged 50/50 (0x438fac); far rings: colour mip k with stepped fog max(0, 20k−15)/256 toward sky colour 0 (0x431924), mixed with ^1.25/^0.8; saturation then gamma/filter tables (0x4358f4, 0x4356c8); sky gradient 2·horizon per row and clouds at sky_height m drifting with time (0x43b930, 0x43b5f4); terrain to 40·F m (12.8 km at 640 wide). detail-elevation bumps (e ≥ 128 raises the near ground by (e−128)/128 m, up to 0.19 m with SNIP_D) on a ¼ m mesh around the camera; water: see V6; objects: see X8 |
| V2b | Gun model motion | fixed | no bob, sway or reload lowering in DF.EXE (0x449abc); firing kicks the gun model 1/32 m back and ±1/64 m randomly, settling 0x80/tick (0x449cbc); the view never moves; flash ≈ one frame. Table +0xA4 = flags (scope, poses, range readout, full auto, …) |
| V5 | Scope inset | fixed | centre (480,240), radius 100 (was 120), ticks r/4..r/2, range "%3im" / "1km+" at (500,312.5); shown only while not moving (0x450600) |

## Command map

| # | Item | Status | Notes |
|---|---|---|---|
| M1 | Map grid and labels | fixed | bright green 1 px; labels white with black outline (0x43ddf4) |
| M2 | Route and markers | fixed | route bright green; markers from record +0x4C (type: 1 circle, 3 diamond, ...) in side colour, radius 6.25 (0x43e434) |
| M3 | Player marker | fixed | soldier marker in side colour, blue (0x43e434) |
| M4 | Current waypoint blink | fixed | DF.EXE 0x43d975 |
| M5 | Soldier marks | fixed | GPS option (default 1, "gpsicon") draws soldiers whose record +0x4C is set as soldier markers in side colour (red enemies, blue friendlies), an X once dead, culled to the map window (0x43d445, 0x43e434). The flag never changes at run time; footage at 173 s shows live and dead enemies this way. OpenDF shows more at once only because soldiers do not move or die yet (no AI) |
| M6 | Compass rose | fixed | (560,315), arms 40, star with inner points 5; N white, E S W green, outlined (0x43dec0); was invented |
| M7 | Map scale and zoom | fixed | default scale 1 = 0.625 px/m at 640 (was 0.75), zoom ×0.9 / ×1.1 within 0.25..3.6 (0x42ddbf) |

## Panels

| # | Item | Status | Notes |
|---|---|---|---|
| P1 | Goals screen | fixed | no panel (was invented); right-aligned at x 625 from y 72.5, 15.6 apart, shadowed; headings white, goals white / green when met, lose lines white / red when hit (0x485004) |
| P2 | Mission end panel | fixed | 0x483094: darkened box (128,116)-(896,246) virtual, shade 10 = 10/31 brightness (0x401b6c, 0x435070), bright green outline; text pure green (0,255,0) centred at x 512, tops 136/176/206 virtual. The earlier footage measurement (43 %, bright text) was off |

## Controls

| # | Item | Status | Notes |
|---|---|---|---|
| K1 | Every default key binding | fixed | DF.EXE default table at 0x4df4f8 (read at run time, built-in copy as fallback). Was wrong: Y next waypoint (original W), map zoom keys reversed, Tab/R/X/Z/WASD invented. Now the original table + mouse look (user's choice); N = debug flight only |
| K2 | Weapon keys | fixed | slots per DF.EXE 0x44b598: 2 sidearm, 3-5 primary modes, 6-8 secondary modes (1 knife, 9/0 grenade/laser not implemented). Was: invented 2/3/4/6/7/8 with all five guns carried |
| K3 | Keyboard turn/look, mouse | fixed | 0x1fffffe per 30 Hz controller tick = 84.375°/s, 42.1875°/s with NoRun (corrected: the controller runs every second tick, 0x436c74), same for pitch; pitch −67.5°..+90° (0x436fec, 0x437435); mouse 0.1758° per count, a quarter zoomed (0x42eba0) |

## Gameplay

| # | Item | Status | Notes |
|---|---|---|---|
| G1 | Weapon switch delay | fixed | was a guessed 0.4 s; DF.EXE 0x44aafe clears cooldown and reload, no delay; an empty weapon starts reloading |
| G2 | Reload rules | fixed | DF.EXE 0x44a004 / 0x449f58: reload discards the rounds left; the last round fired starts the reload at once; empty trigger reloads |
| G7 | Loadout | fixed | default from DF.EXE 0x40ee14: .45, M4/M203, Ammo; ten slots from the loadout rows (0x44b598): keys 3-5 were single/burst in OpenDF, DF.EXE has burst, M203, single (MP5 SD auto/burst/single, SAW auto/semi-auto); start on slot 3 (burst); Ammo doubles the spares of slots 3-5, the M203 included (35 rounds); modes, guns and rows all read from DF.EXE; `--loadout PRIMARY,KIT,SIDEARM` until Change Gear exists |
| G8 | Fire modes and ammunition | fixed | see VERIFICATION Weapons: per-gun chambered/magazine/spares; bursts from 0x46af90 (were a footage-based guess); full auto 0x20; M203 and LAW (fire type 2); HUD readout by fire type (0x483447) |
| G3 | Bullet flight | fixed | projectile per 60 Hz tick: speed velocity·60/62 m/s, gravity 9.78 m/s², no drag, lifetime gun +0x18 ticks, no penetration (0x44d450, 0x44f3e4); origin at the gun muzzle, or the eye along the view when scoped |
| G4 | Damage | fixed | 1 << (P >> age/falloff), no effect if P < armour class B (0x44f184); people lose ITEMS.DEF hp and die at 0; head (part 2) ×4 using the .CHR hit spheres of the standing pose: the path must pass within 1.5 m of the body origin, then the nearest sphere along it wins, distance by DF.EXE's approximation (0x44dca4, 0x435bf8). The large upper-arm and hand spheres often take shots aimed at the head from behind or the far side, as in the original |
| G5 | Scope | fixed | magnification table +0xA0, FOV 28/mag whole degrees (0x450600); one zoom step |
| G6 | Message lifetime | fixed | see B1 |

## Found during the audit

| # | Item | Status | Notes |
|---|---|---|---|
| X1 | Fern (FERN2) drawn magenta inside the scope inset | fixed | transparent texels (palette entry 0, magenta) bled through texture filtering when magnified; they now take their opaque neighbours' colour. The original samples textures unfiltered |
| X2 | Spread | fixed | uniform ±0.0824°·(A−1) on yaw and pitch, A = gun +0x1C; stance, movement and scope do not matter (0x46af90). Were invented modifiers |
| X3 | Player physics | fixed | jump 4 m/s (+1/16 m), prone jump stands up; gravity 9.78; ground glue 1/32 m, airborne past 0.5 m; step 0.5 m; slides on gradients > 1.3 until < 0.7; uphill > 0.7 forces walking; collision box ±0.75 m (0x436158, 0x436fd0, 0x437a28) |
| X4 | Eye height | fixed | origin height of the stance pose (.CHR contact points: standing 1.056 m, crouched 0.677, prone 0.103 for 2HW) + 0.7 m (0.4 prone) (0x449913, 0x4364c8): 1.76 / 1.38 / 0.50 m; eases 0.9 per tick |
| X5 | Scope behaviour | fixed | moving turns it off; random aim offset up to 2.8° per zoom (0x44b870); full auto does not refire while zoomed |
| X6 | Impact effects | fixed | effect objects (0x46a214, 0x436eec, 0x480c70): the slot's ITEMS.DEF item model 0.25 m back along the path, billboarded (yaw), scale 0.5..1.5, animated materials frame = age / ticks-per-frame, removed after frames × ticks-per-frame; terrain surface from the TRN `type` lines, people → flesh, objects → the model's dominant material +0x12 (approximation: OpenDF hits boxes, not polygons). Smoke puffs, splashes and explosions: see X10–X12 |
| X7 | Move selection | fixed | 0x436fd0 at 30 Hz: crawl when prone; walk (NoRun or steep uphill) / run otherwise, also when crouched (crouch stays set, back to crouched when stopping); *_S_* moves while the refire time runs, speed 0 for moves the .ANM lacks (no Running*_S_* in PLAYANIM.ANM: running stops while firing); speed v m/s along heading + dir·45°, instantly; take-off motion kept in the air; sliding replaces movement. Swimming: see X9. Override moves (ANM 4th token "override", record +0xC): the animation's root displacement ×1.8235 drives the body (0x43666b); only Null, prone transitions and deaths use it — open until people are animated |
| X8 | Model rendering | fixed | lighting (0x44bea4, 0x46f488): light toward −X/up at 45°, ambient 0.55 + diffuse 0.5, back light 25 %, Blinn specular per material (+0x38, power +0x40), fullbright (0x0020, +0x3c); shade levels 1..60 through the shade table (black → colour → sky colour 16) in the ^1.25 space and the TRN colour finish; flat or Gouraud per face flag; no fog on objects; LOD by projected radius F·R/depth vs header thresholds (persons R = 1), not drawn under 0.5 px; cull at 1020 m; materials: double-sided (else back faces culled), 0x0080 50 % average, 0x8000 texel-as-alpha toward the material colour, colour key, 0x0008 not drawn, decal depth, animated materials, four colour variants chosen by the mission header +0x74 (0x46f9e3, 0x43f964) |
| X9 | Swimming | fixed | water flag when the level is above the body origin (0x436577 airborne, with effect 4 "water exp" on the surface; 0x43697d walking); move Swimming while the forward key is held, else Floating (0x43752e, before any stance); no gravity, vertical speed ×0.8 per tick (0x436912); swim along heading and view pitch, vertical at half rate, no rising once origin + 0.4 m is above the surface (0x43621a/0x43628f); the bottom holds the pose's lowest contact point (Floating 0.149 m, Swimming 0.318 m from PLAYER01.CHR); out when the origin is above the level (0x4368ee); eye eases to 0.3 m above the origin (0x449918). Not found in DF.EXE: weapon, jump or drowning rules for water (OpenDF: weapons work, jump ignored). Footstep splashes (0x46a680, effect 2 when origin − 0.8 m is below the level): open, needs the .CHR animation events. Stance icon while swimming: unchanged (POS art has no swim pose) |
| X10 | Bullets and water | fixed | water plane tested after terrain/objects, nearest wins (0x44f0e4): a start under or within 0x200 of the surface hits at once; the round ends there (0x44fc20) with effect 2 "water splash", explosives with effect 4 "water exp" |
| X11 | Frag grenade | fixed | always carried in slot 9 (key 9, 0x44b598: mode 33, gun 12: 1 + 5 spare); thrown along the view (no lob), 20 m/s ·60/62, spread ±(A−1)·30·0x8000 (0x44a36c), gravity 0xb2/tick², kick 0x1000 back, 0x2000 down; next grenade in hand after the 90-tick cooldown (0x44a318); explodes on first impact (flag 0x10 arms at once; others after 8 ticks), gone after 1000 ticks or below −1 m; in-flight model ITEMS.DEF "grenade" (orientation: APPROXIMATION, turned to the travel direction) |
| X12 | Explosions | fixed | effect 3 "ground exp" (4 "water exp" in water) and blast type 1, radius 10 m (0x44fd4c/0x44fda8): people (and the player, 4 hp from ITEMS.DEF) within radius + 1 m lose 4/2/1 hp under 4/8 m, unless the terrain blocks both lines of sight (0x45be04); objects lose max(0, 32 − 4d) (APPROXIMATION: LOD-0 bounds for DF.EXE's +0x64/+0x68 extents); no armour check, no camera shake, no crater (0x435dbc is a stub). Terrain hits leave a smoke generator (sgen 0x480b88: a puff every 16..23 ticks, 100 puffs); puffs (0x480aac/0x480bfc) start at scale 0.125..0.19, grow 1/128 per tick to 2.0, rise +0x20 and drift +8 (16.16) per tick², roll one step per tick (roll axis: APPROXIMATION, the billboard normal). Explosive barrels go off 10 ticks after destruction (0x45a3c0); their shrapnel: open. Sounds 0x32/0x35/0x3b: open (no sound). Player death sequence (0x45a234 → 0x444d24/0x46c9c0): open; a dead player only loses control |
