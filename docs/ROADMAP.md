# Roadmap

Each milestone ends with something you can run.

## M1: Data access and terrain ✅
- PFF2/PFF3 reader, PFF2 decryption, `list` / `extract` tools
- PCX, TRN parsing
- GPU terrain renderer (chunked LOD, wrap-around, detail tiles), sky, clouds, water, haze
- Fly / walk camera with ground collision

## M2: Objects in the world ✅
- `.3DI` decoded: textures, LODs, materials, cut-out/translucent/multiply blending (359/362 files)
- `.3DO`/`.AI` identified as a legacy library, mostly unused; header layout documented
- Model viewer mode (`opendf models`), `check` command
- Objects placed on terrain at true scale (1/256 m units)

## M3: Missions ✅
- World scale calibrated from mission data: 1 m per terrain pixel, Y north, heading clockwise from north (height ≈ 0.25 m/step, estimated)
- `ITEMS.DEF` parsed; `.BMS` object records decoded (position, height, heading, type)
- `mission` mode: every object placed, camera at the player start, Tab cycles missions
- `.JIM` identified as briefing-map annotations (moved to M7)
- Still open: BMS triggers/objectives and the per-object AI fields (needed in M6)

## M4: Player ✅
- First-person soldier: run (default) / walk, jump, stand / crouch / prone, slope limits
- Collision against the models' own collision volumes (bounds as a fallback), standing on bridge decks
- Weapons: .45, M4 single/auto, M40 with 4×/10× scope, MP5SD, M249; magazines, reloads, recoil,
  simulated bullets with drop and travel time, muzzle flash from the models' flash materials
- Damage: people go down, soft objects break into their husk models
- The original's HUD layout and art (see docs/ORIGINAL-UI.md), zoom inset, command map; original key layout where possible
- Still open: knife, frag grenades, M203, satchels, binoculars, laser designator; player health
  (comes with enemies in M6); waypoint names (`.JIM`); footstep materials (with audio in M5);
  the remaining weapon numbers in `DF.EXE`'s weapon table (fire rate, damage, ...)

## M5: Audio
- WAV playback from `AUDIO.PFF` (SFX, radio chatter)
- `DFMUSIC.SBF` music bank sequenced by `DFMUSIC.BIN` (done: menu music)
- Positional 3D sound

## M6: AI and mission logic
- Enemy and friendly soldiers: patrol, alert, cover, attack
- Mission scripting: triggers, objectives, win/fail, debrief
- Vehicles and static weapons used in the campaigns

## M7: Front end
- Menus from `DF.MNU` / `DFMENU.BIN`, player profiles, settings
- KDV video decoder (the intro; done, docs/FORMATS.md "KDV video")
- Save game progress per campaign

## Later
- LAN multiplayer (deathmatch, team, CTF, KotH) with a modern protocol
- Widescreen / high-resolution HUD scaling, controller support
