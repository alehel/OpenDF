# What is verified against the original

OpenDF aims to do things the way the original does. **DF.EXE is the
authority**: values come from its code and data (and the game files it
reads). A recording of the original is only a cross-check and a hint where
to look; it is never the source of a value DF.EXE can provide. This list
tracks every value or behaviour and where it comes from. **Original**: read
from DF.EXE or the game files. **Footage-only**: no code source found yet,
taken from the recording (to be replaced). **Inferred**: deduced from data,
plausible but unconfirmed. **Approximation**: OpenDF's own choice, to be
replaced. Item-by-item HUD/controls audit: docs/AUDIT.md.

Values that live in DF.EXE are read from the user's own copy at start-up
(`DFSETUP/DF.EXE`); OpenDF keeps built-in copies of them as a fallback for
when DF.EXE is missing or a different version.

## Weapons

| Item | Status | Source |
|---|---|---|
| Names and fire modes | Original | `DFGAME.BIN` strings, read at run time |
| First-person model, offset and camera | Original | DF.EXE player weapon table `+0x64`, `+0x78`; matches footage |
| Magazine size | Original | gun table `+0x08` (M4 30, M40 5, MP5 SD 30, SAW 200, .45 7) |
| Spare magazines | Original | gun table `+0x04` = n; spares n−1, or 2n−1 for the primary when the secondary Change Gear kit is "Ammo" (profile +0x688 == 1; 0x449787 → 0x44b598) |
| Default loadout and slots | Original | a new soldier (0x40ee14): sidearm .45, primary M4/M203, kit Ammo. Ten slots (0x44b598, keys 0-9 via 0x42de85): 0 laser designator, 1 knife, 2 sidearm, 3-5 the primary row's modes, 6-8 the kit row's, 9 frag grenade; rows from the item table 0x4E9610 (M4/M203 = burst, M203, single). Mission start on slot 3 (0x449791). OpenDF's `--loadout PRIMARY,KIT,SIDEARM` until Change Gear exists. All slot modes work (see AUDIT X15, X20) |
| Muzzle velocity | Original (units inferred: m/s) | gun table `+0x0C` (M4 1000, M40 950, MP5 SD 295, SAW 1000, .45 295) |
| Scope power | Original | player table `+0xA0`, used by the scope view (0x450600); allowed when `+0xA4` bit 0 is set |
| Fire rate and modes | Original | player table `+0x9C`: ticks between rounds at 60 ticks/s; `+0xA4` 0x20 full auto (refire on cooldown expiry while held, not with the binoculars up — [0x93367c] is the binocular flag, not the scope); bursts hard-coded for modes 6, 8, 21, 35, 36 in 0x46af90: three rounds a cooldown apart, finishing on their own (0x44988e); otherwise one shot per press (0x44a920). Ammunition per gun (+0x74): chambered flag, magazine, spares; the next round is chambered when the cooldown ends (0x449f58) |
| Reload time and rules | Original | 0x44a004: 0x78 = 120 ticks, the rounds left are discarded; 0x449f58: the empty magazine starts the reload. Launchers and grenades: the next round after the cooldown (0x44a318). Chambering after a reload into an empty chamber: inferred |
| Weapon text red | Original | 0x44b0a0 / 0x44b164: while reloading or with no ammunition at all |
| Ammunition readout | Original | 0x483447 by fire type: guns spares (with glyph) / magazine / chamber bar; launchers and grenades: rounds left / chamber bar at 140; icon frame per mode (0x484ae8) |
| M203 and LAW | Original | fire type 2 (0x44a120): from the model, no spread, kick 0x4000 back (the random drop's range is inferred); M203 rounds arm after 8 ticks (before: a bullet-like hit, effect 5 on objects), blast type 1; the LAW no gravity, arms at once, blast type 3 out of the water; no first-person model for the LAW (item 0). Rounds leave from the first-person model's hardpoint +0x90 (0x44b4a0) |
| Spread, damage, recoil | Original | see docs/AUDIT.md G3, G4, X2, V2b |
| Sounds | Original | gun table `+0x20..+0x2C` sound ids; see AUDIT S1-S3 |
| Knife | Original | slot 1, fire type 6: see AUDIT X15. A mission starts on slot 3 (0x449791), not the knife |

## Player and camera

| Item | Status | Source |
|---|---|---|
| Field of view | Original | 0x440ff0: FOV 90 (0x404489) across the view width, focal = W/2 for both axes → 58.72° vertical on 640×360. Wider screens keep the vertical angle (OpenDF's widescreen choice) |
| Scope view | Original | 0x450600: FOV parameter 28 / magnification, half-angle (fov+1)>>1 whole degrees, inset radius 100; M4 ×4 ≈ 8°, M40 ×8 ≈ 4° across the inset. Allowed when the weapon's table +0xA4 bit 0 is set |
| Binocular view (22° main view) | Original | see AUDIT X17 (0x44b870, 0x40423d) |
| Movement speeds | Original | the soldier's `.ANM` move table (`PLAYANIM.ANM`, via `anim_def` in ITEMS.DEF), read at run time: velocity in m/s per move, eight directions, one- or two-handed weapon. Running forward with a rifle 5.0 m/s (footage: ~5 m/s), pistol 6.0; walking (Shift) 1.5/1.0; crawling 1.0 forward and back, 0.5 sideways |
| Crouched movement | Original | DF.EXE moves a crouched soldier with the standing walk/run and keeps the crouch flag, so he crouches again when he stops (0x437902) |
| Walking moves 5 and 7 | Original | slot index decides (5 back-right, 7 forward-right); the ANM's walking comments are swapped (0x437583) |
| Moving while shooting (`*_S_*` moves) | Original | used while the refire time runs; missing entries have speed 0 (0x436fd0) |
| Eye heights, jump, step height, slope limit, radius | Original | see docs/AUDIT.md X3, X4 |
| Weapon bob, recoil kick, reload lowering | Original | none in DF.EXE except the gun-model kick (docs/AUDIT.md V2b) |

## World

| Item | Status | Source |
|---|---|---|
| 1 m per terrain pixel, map wraps every 1024 | Original | DF.EXE terrain sampling (0x432F94): cell = (x & 0x3FF) + ((y & 0x3FF0000) >> 6) on 16.16 coordinates; also building pads, bridges, footage waypoint distances |
| Mission y = north | Original data | building pads, bridges |
| No north/south mirroring | Original | grid naming code (0x43dce0) and the y = north convention of BMS/terrain sampling |
| Height per elevation step: 0.25 m | Original | DF.EXE: `elevation_byte << 14` in 16.16 metres (0x432F98); TRN `water_height` uses the same units (`<< 14`) |
| TRN keys | Original | sky_height in metres; sun_slope, camouflage never read; `type` lines only choose impact/footstep effects (0x4305b0) |
| Terrain colour, detail, fog, sky, clouds, view distance | Original | see docs/AUDIT.md V4 |
| Falling damage | Original | none: the landing code only zeroes the fall speed; no health write in DF.EXE depends on a fall |
| Swimming | Original | DF.EXE 0x436158/0x43621a/0x43752e/0x449918: see AUDIT X9; Floating/Swimming origin heights from PLAYER01.CHR anims 134/133 |
| Water splashes | Original | bullets: water plane 0x44f0e4, effect 2 (0x44fc20); falling in: effect 4 (0x4365a2) |
| Frag grenade, explosions, smoke | Original (approximations noted in AUDIT X11/X12) | DF.EXE 0x44b598, 0x44a36c, 0x44f3e4, 0x44fda8, 0x480b88, 0x480bfc |
| Water | Original | DF.EXE 0x43bfd8/0x43bd7a: W = T[(1−op)·lum + op·4r] over the already-drawn pixel's luminance; gradient from water palette 0..15 (0x431b3e); 16 texels/m, ±4-texel sine waves, phase tick·2; no fog; drawn last to 1023 m; under-water tint through r = 0 (0x43c500) |
| Model material flags, LOD, lighting, fog | Original | see docs/AUDIT.md X8 |

## HUD and text

| Item | Status | Source |
|---|---|---|
| Layout, art, font, colours | Original | DF.EXE HUD code (virtual 1024x768 layout, RGB colour table); the art and font of the original display mode that fits the window (640/800/1024; `POS%i`, `WEAP%i`, `F_SML%i` capped at 800, as DF.EXE picks them) drawn 1:1 relative to that mode; item by item in docs/AUDIT.md |
| "Carrying: Nothing", "Ammo reloaded", mission-end texts | Original | DF.EXE strings |
| Mission names ("Insurrection") | Original | `DFCAMPcc.BIN` entry m−1, read at run time |
| Radio messages ("Charlie 2 is down.", opening orders) | Original | mission events fire dialogs `dlg%03i`; clips and subtitles from `DFDLGnn.DBF` + `DFDLGnn.BIN`. Insurrection's opening "Proceed southwest and neutralize all hostiles." appears as in footage. Shown as text at once; the original plays the clips (audio) in sequence, subtitles with their clip |
| Triggered text | Original | campaign "Triggered Text" `ID%03i` (no shipped campaign uses it) |
| Mission end panel | Original | texts and success rule from DF.EXE (0x483094); events decide the winner |
| Goals screen (G) | Original | "Win Conditions:" `M%02iSUBGOAL%02i`, "Lose Conditions:" `M%02iSUBFAIL%02i` from the campaign "Goals" section, met goals highlighted; layout from DF.EXE 0x485004 (right-aligned at 625 from 72.5, 25-unit lines, shadowed) |
| Grid square names and map grid | Original + footage | DF.EXE 0x43DCE0: column = ⌊(x + 1000) / 100⌋ as A..Z, AA.., row = ⌊(1000 − y) / 100⌋ + 1, in mission coordinates (not the 1024 m terrain tile). Start of Insurrection is I12, as in footage; the command map's grid lines up with the terrain as in footage |
| Command map scale and zoom | Original | 0x43cfc8 / 0x42ddbf: default scale 1 = 0.625 px/m at 640, ×0.9 / ×1.1 per key |
| Waypoint route, names, box, compass dot, map route | Original + footage | route = first BMS path with flag 2 (DF.EXE 0x447856); names = campaign text "Waypoint Names"/`WPN%03i` with the numbers at BMS header +0xAC; arrival radius = waypoint record +0x1C; advance rule 0x449a39; box format and distance 0x4838ac. Insurrection gives IP, CP ALPHA, CP BRAVO, OBJ BREEZE, CP CHARLIE, EP; route shape matches the footage's command map. `.JIM` files are not used by DF.EXE |
| Distance at the start of Insurrection | Original | DF.EXE picks the first player-start marker in single player (0x442c47), as OpenDF does (29 m); the footage's 22 m is not explained, but the code decides |
| Map route markers | Original + footage | circles at first/last point, diamonds between; the current waypoint's marker blinks, shown while tick bit 4 is set (16 ticks on, 16 off; DF.EXE 0x43d975, counter 0xc70acc incremented per tick); footage ≈ 0.55 s cycle |
| Map player marker | Original | soldier marker (type 5) in the side colour, blue for side 1 (0x43e434) |
| "Next waypoint" key | Original | W, from DF.EXE's key table |

## Mission events

| Item | Status | Source |
|---|---|---|
| Event engine: schedule (every 128 ticks, one eighth per 16 ticks), condition chaining, NOT, delays with re-check, repeats | Original | DF.EXE 0x445914, 0x45c534, 0x45c48c |
| Conditions: team all dead / alive / losses / at least n alive / none lost, team inside area, event fired, campaign flags, load toggle, squad option | Original | 0x45c17c; areas from Area Trigger markers by tag (0x45c9a0) |
| Conditions needing AI: team spotted / engaging / alerted / reached waypoint | Original | see AUDIT A4/A5; carrying: AUDIT X19 |
| Actions: kill team, remove team, campaign flag, text, radio, end mission (win/lose/none), goals met, lose conditions | Original | 0x45c368 |
| Actions needing AI: follow path, AI properties, team speed | Original | see AUDIT A5 |
| Team membership and counts | Original | team = record byte +0x48, side = +0x43; counts over players, people and moving objects (0x4409f4) |
| Player death | Original | see AUDIT X13, A6 |

## Front end

| Item | Status | Source |
|---|---|---|
| Menu layout, fonts, buttons, screen flow, settings pages, credits, briefing map, transitions | Original | DF.MNU, DFMENU.BIN; see AUDIT F1-F12 |
| Menu music: stems, script, variables, stem timing | Original | DFMUSIC.SBF, DFMUSIC.BIN, DF.EXE VM 0x4ab5ce; see AUDIT F4 |
| Menu music loudness against sound effects | APPROXIMATION | the block gain (V>>1)>>shift is DF.EXE's; it is applied as a float, (pcm − 128)/128 · 2^−shift · V/255, in OpenDF's mixer, whose overall scale against DF.EXE's mixer was not measured |
| Settings and df.cfg | Original | see AUDIT F5, F6 |
| Renderer choices on the Colors row | APPROXIMATION | DF.EXE steps through the renderers it detected (0x4f5140); OpenDF offers all three |
| Flip Mouse direction | INFERRED | DF.EXE negates the vertical mouse count when flipmouse is 0 (0x42ebd6); which way that turned the view in DF is not traced, so OpenDF keeps its own default and flips it when the option is set |

## Not yet implemented, so not checkable

Multiplayer (screens, network).

| Item | Status | Source |
|---|---|---|
| Rounds against objects (vehicles, buildings, props) | APPROXIMATION | OpenDF tests the model's collision volumes (or its bounds); DF.EXE tests each posed mesh of the hit model in mesh space (0x44e440..), skipping meshes flagged by the model's pose routine, e.g. the helicopter's hit pose (0x478070: unrotated rotors; meshes 3 and 5 unhittable while the main rotor is slow, 2 and 4 when fast). Not yet traced or implemented |
