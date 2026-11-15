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
- `.JIM` identified as briefing-map annotations; DF.EXE never reads them (waypoint names come from the campaign text)
- BMS events, objectives and the per-object AI fields (done with M6)

## M4: Player ✅
- First-person soldier: run (default) / walk, jump, stand / crouch / prone, slope limits
- Collision against the models' own collision volumes (bounds as a fallback), standing on bridge decks
- Weapons: every weapon of the loadout table, from DF.EXE's tables (fire modes, magazines, reloads,
  recoil, spread, damage); simulated bullets with drop, drag, penetration and tracers; muzzle flash
- Damage: people go down, soft objects break into their husk models
- The original's HUD layout and art (see docs/ORIGINAL-UI.md), zoom inset, command map; original key layout where possible
- Knife, frag grenades, M203, LAW, satchels, claymores, binoculars, laser designator and fire
  missions; player health; waypoint names; footstep materials

## M5: Audio ✅
- WAV playback from `AUDIO.PFF` (SFX, radio chatter)
- `DFMUSIC.SBF` music bank sequenced by `DFMUSIC.BIN` (menu music, DF.EXE's integer mixer)
- Positional 3D sound

## M6: AI and mission logic ✅
- Enemy and friendly soldiers: patrol, alert, cover, attack
- Mission scripting: triggers, objectives, win/fail, debrief
- Vehicles, helicopters and static weapons used in the campaigns
- Still open: Delta Force's piranhas and penguins (docs/AUDIT.md X28)

## M7: Front end ✅
- Menus from `DF.MNU` / `DFMENU.BIN`, player profiles, settings
- KDV video decoder (the intro; docs/FORMATS.md "KDV video")
- Bink 1 video decoder (DF2 / Land Warrior intros; docs/FORMATS.md "Bink video")
- Save game progress per campaign
- The launcher (docs/LAUNCHER.md) and OpenDF's own fullscreen toggle

## M8: Delta Force 2 ✅
- DF2's formats, rules, HUD, command map, front end and missions, from DF2.EXE (docs/OTHER-GAMES.md)

## M9: Audit against the original executables ✅
- Two function-by-function audits of OpenDF against the GOG builds (Delta Force 1.00.03.03p,
  Delta Force 2 1.06.15), now the reference; CD and GOG executables and installs both supported
- What they fixed: docs/AUDIT-FIXES.md; what is still partly done: its "Partly fixed" section

## Next
- Land Warrior (docs/OTHER-GAMES.md "Suggested order")

## Later
- LAN multiplayer (deathmatch, team, CTF, KotH) with a modern protocol
- Widescreen / high-resolution HUD scaling, controller support
