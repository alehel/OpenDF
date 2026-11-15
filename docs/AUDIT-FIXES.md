# Audit fixes

What the 2026-11 audit of OpenDF against the original executables fixed. Two full audits compared every function of
the GOG builds (Delta Force 1.00.03.03p, Delta Force 2 1.06.15; the reference since 2026-11-15) with OpenDF: the first
against the CD builds, moved onto GOG afterwards, the second directly against GOG with a typed decompile and a field
cross-reference. Their findings were fixed in four batches by severity, each change reviewed against the EXE.
Generated from the fix agents' own reports. Each entry is the problem as the audit reported it; it now behaves as the GOG
EXE does. Where both audits reported the same problem it can appear twice.

| Batch | Scope | Fixed | Partly fixed | Skipped |
|---|---|---|---|---|
| 1 | first audit, high severity (+ CD/GOG EXE support) | 83 | 4 | 0 |
| 2 | second audit, high severity | 27 | 0 | 0 |
| 3 | medium severity | 169 | 15 | 16 |
| 4 | low severity | 237 | 21 | 42 |
| **Total** | | **516** | **40** | **58** |

Plus 88 follow-up fixes from the reviews of the batches, and the Delta Force 2 player
parachute (the marker pass GOG Df2.exe 0x45c4b0 / CD 0x4544a0, found while checking the audit's method).

Skipped items were already fixed by an earlier batch or turned out wrong when the EXE was re-read; the reason is listed.

## Fixed

### Delta Force

**Missions and scripts**

- Player start ignores the BMS multiplayer-only flags (cause of the C02M01 wrong start) *(batch 1)*
- Multiplayer-only BMS objects (flag 0x10/0x20/0x80) are spawned in single player *(batch 1)*
- Script condition class 5 always true; DF.EXE makes it false when Squad Members is off *(batch 1)*
- Single player keeps multiplayer-only BMS records (wrong player start on C02M01 and others, extra enemies) *(batch 1)*
- BMS record filter: the GOG build keeps flag-0x20 records in single player, and OpenDF applies no filter *(batch 1)*
- Script AI property 2/3/4/14/15 with value 0 sets the bit instead of clearing it (DF1) *(batch 1)*
- DF1 'set AI attribute' with value 0 sets the flag instead of clearing it *(batch 1)*
- Area condition tests AI objects at their mission start positions *(batch 1)*
- 'Team inside area' tests AI teams at their mission start positions, not where they are *(batch 1)*
- Script 'kill group' only kills people *(batch 1)*
- Campaign flags are saved at the win action instead of at mission exit *(batch 2)*
- Single-point paths are not given the 'stop at end' flag at load *(batch 2)*
- 'Follow path 0' (used to halt teams) is ignored *(batch 2)*
- Repeating and delayed events re-fire in the same visit their timer expires *(batch 3)*
- Mission scripts stop when the mission ends; GOG keeps running them *(batch 3)*
- Mission-start (flag 2) and mission-end (flag 4) events scheduled wrongly; flag 16 excluded *(batch 3)*
- Team counts: dead player alive, removed-at-start counted in totals, no 63-tick recount *(batch 3)*
- Space replay does not record the finished attempt *(batch 3)*
- Condition 1.2 ('team engaging team') ignores the target team *(batch 3)*
- Teams marked 'alerted' from the record alert byte and by AI property 5 *(batch 3)*
- Script AI property actions never reach vehicles and helicopters *(batch 3)*
- Kill-team action does not set off mission-placed satchels *(batch 3)*
- Dropped goal item keeps its mission height and floats *(batch 3)*
- In-game briefing {CV:n} tests the live script flags instead of the soldier's saved flags *(batch 3)*
- Player death ends the mission with side 0 every 64 ticks instead of side 2 every 63 *(batch 4)*
- Initial think stagger uses a different counter than GOG *(batch 4)*
- AI 'no one within 2 m' check differs in range, axes and who counts *(batch 4)*
- Player animation-event voices always use the male phrase set *(batch 4)*

**Menus (front end)**

- Briefing map never starts on the 'Briefing Map Center' marker (wrong BMS group) *(batch 1)*
- Briefing map never starts on the 'Briefing Map Center' point: wrong BMS group tested *(batch 1)*
- Debriefing {ADD}<key> is not expanded: the literal 'Win5a' is printed and the rest of the text is lost *(batch 1)*
- Campaign map mission dots are drawn without colour keying, so each dot shows red corner pixels (the reported mission selector highlight bug) *(batch 1)*
- Campaign map mission dots drawn without colour key: red corners around every dot (the mission selector highlight bug) *(batch 1)*
- Campaign-map mission dots drawn opaque: red square corners instead of round dots (user-reported highlight bug) *(batch 1)*
- Campaign map mission dots drawn without the palette-index-0 key: red corners around every dot (the reported 'mission selector highlight looks off') *(batch 1)*
- Menu font glyph bands start one row early and are one row too tall; line height 13 instead of 12 *(batch 2)*
- Briefing-map route lines and link crosshair drawn blended (dim olive) instead of solid 0xFFFFE000 *(batch 2)*
- Settings slider knobs drawn opaque: palette index 0 (red stripe) not keyed *(batch 2)*
- Screen-wipe light bar: black pixels blended in instead of skipped *(batch 3)*
- Clicking elsewhere while editing a text field does not commit it; hover keeps running during edits *(batch 3)*
- Campaign page overview heading lacks the selected mission's name *(batch 3)*
- Briefing first word still drawn when the text is scrolled *(batch 3)*
- Word wrap tests word + trailing space instead of the word alone (briefing draw, briefing line count, teletype boxes) *(batch 3)*
- {CV:} expansion keeps the space before a following '{' tag, shifting briefing links *(batch 3)*
- In-game briefing {CV:n} tests stale live flags instead of the profile's campaign word *(batch 3)*
- Keyboard Remap has 46 rows in GOG (Previous Waypoint, Server) with DFDK.KEY defaults; OpenDF has 44 CD rows *(batch 3)*
- No df_wait.pcx loading screen or 'Loading Text' lines between the briefing and the mission *(batch 3)*
- Debriefing win/lose text, kills line and music Var02 use this run's result instead of the profile's mission-done flag *(batch 3)*
- Single-image type-1 buttons with attrib 0x800|0x1000 drawn opaque instead of index-0 keyed *(batch 4)*
- Mouse-held buttons show the pushed frame, and linked buttons get no hover/grass5 *(batch 4)*
- Campaign select buttons: extra MENUBTN1 on press and no hover frame *(batch 4)*
- Change Gear category tabs: extra MENUBTN1, current tab re-pressable, no hover highlight *(batch 4)*
- Choose Soldier help texts for creating and recreating a soldier are swapped *(batch 4)*
- Typing in menu text fields: no Ttclick sound and no upper-casing *(batch 4)*
- CHANGE NAME and DELETE SOLDIER hidden for an empty slot and during the delete dialog *(batch 4)*
- Empty soldier rows read 'NEW OPERATIVE' instead of NSEMPTY 'New Operative' *(batch 4)*
- Keys without a character fire the first hot-key-less button (GOG quirk) *(batch 4)*
- Hovering a slider plays grass5 and sends ev6/ev7 *(batch 4)*
- GOG intro video: a left click ends the video and does not skip the logo screen *(batch 4)*
- Intro video clock keeps running while the window is inactive *(batch 4)*
- Centred menu text uses a fractional half width instead of integer w>>1 *(batch 4)*
- Menu art is GL_LINEAR filtered (strip cells bleed) and slider knob offsets are fractional *(batch 4)*
- Menu lines lose half a pixel at each end (GOG DDA draws both endpoints) *(batch 4)*
- Teletype stops clicking one tick early *(batch 4)*
- Keyboard Remap hint teletypes at 2 characters per tick instead of 1 *(batch 4)*
- Undefined glyphs draw nothing instead of glyph 0 (box) *(batch 4)*
- Edit cursor blinks at half DF1's rate *(batch 4)*
- Menu screenshot key (P with modifier) not implemented *(batch 4)*

**Command map and waypoints**

- Command map markers of groups 0-2 drawn at their mission-file start positions, not where the objects are now *(batch 1)*
- Multiplayer-only objects (record +0x0C bits 0x10/0x20/0x80) are created and shown on the single-player command map *(batch 1)*
- Player is frozen (no move, turn or fire) while the DF1 command map is open *(batch 2)*
- DF1 inset terrain map and elevation shading (mapterrain F10 / mapelevation F11) missing *(batch 3)*
- Map markers (objects and GPS soldiers) drawn at the nearest wrapped copy instead of the continuous position *(batch 3)*
- Command map stays open after the player dies *(batch 3)*
- GOG PrevWaypoint action, its keyboard row (and KBDSERVER) and PLY version 0x1b missing *(batch 3)*
- BigMap and MissionGoals close each other; Escape closes goals *(batch 4)*
- Friendly tags and binocular readout drawn over the command map *(batch 4)*
- Map scale, map, goals and old-message toggles not reset at mission start *(batch 4)*
- Command map draw order: route lines under markers, soldiers over objects *(batch 4)*
- Marker type 7 X drawn at 0.7r *(batch 4)*
- Grid labels near the map edges are not suppressed *(batch 4)*
- Compass rose fixed at y 315 *(batch 4)*
- Water is not tinted on the command map *(batch 4)*
- Command map colour map bilinear/mipmap filtered *(batch 4)*

**Bullets, damage and explosions**

- Destroyed gnrc/bldg objects (barrel, barrels, oiltank, dish, radar...) never explode; DF1 does a delayed plain explosion with blast *(batch 1)*
- Destroyed gnrc objects (oil drum 'barrel', 'barrels', generators, oil tank, SA2, silkworm, radar, dish...) never explode *(batch 1)*
- Land mine: wrong explosion on contact (60 shrapnel 1 m up) and no explosion at all when shot or blasted *(batch 1)*
- Stepping on a DF1 land mine fires 60 shrapnel 1 m up instead of none at the mine *(batch 1)*
- Round tracer lines are not drawn *(batch 2)*
- Rounds pass through corpses *(batch 3)*
- Rounds use CD's one-sided face test; GOG accepts any plane crossing *(batch 3)*
- Blast damage to people and the player ignores objects; the player is not handled like a person *(batch 3)*
- Explosion AI noise is +/-200 m in OpenDF; GOG's radius is 200 raw 16.16 units *(batch 3)*
- Suppressed guns (gun flag 0x80) still mark the shooter as having fired *(batch 3)*
- DF1 AI and vehicle guns use the CD gun table instead of GOG's *(batch 3)*
- Explosive barrel hidden as soon as it is destroyed; DF1 keeps it drawn and hittable for the 10-tick fuse *(batch 3)*
- Rounds skip people whose origin is behind the segment start, and the 1.5 m test uses the unclamped line *(batch 4)*
- Smoke generators added for helicopter, satchel, claymore and fire-mission blasts *(batch 4)*
- Satchel/claymore blast centre raised 0.2 m; DF1 strike clamped to the water surface *(batch 4)*
- Claymore fragment fan is mirrored *(batch 4)*
- Round terrain test: GOG's coarse corner-byte / fine 1/16 m march (0x47e390) not ported *(batch 4)*
- Unarmed explosive round hitting a person shows a dirt puff *(batch 4)*
- Zero-damage blast does not reach the object's handler *(batch 4)*
- Round hits alert the target even when armour blocks the damage; friendly-fire option 0x4000 missing *(batch 4)*
- Objects stacked on a destroyed crate stay floating *(batch 4)*
- Bird trees (btre) never release sparrows *(batch 4)*

**AI perception**

- DF1 searchlights (glight, ai/move 'slgt') never sweep, track or spot *(batch 1)*
- DF1 sight lines use the husk (or skip the object) once it is destroyed; the EXE always tests the intact model *(batch 3)*
- DF1 sight terrain march: no detail elevation and not the EXE's coarse/fine march *(batch 3)*
- 'Fired' (x6 visibility) mark ignores gun flag 0x80 and wrongly needs a shot sound *(batch 3)*
- Alert no-target soldiers: gunshot turn skips the height test, and the 0x800000 turn is skipped after a heard shot *(batch 4)*
- After hearing a shot (relaxed/cautious), DF1 barks, thinks 0x10 and returns *(batch 4)*
- Relaxed/cautious scan success thinks 8 ticks (DF1: 0x10) and marks seen/lastKnown too early *(batch 4)*
- An alert soldier with no target that spots someone does not mark its team as having spotted *(batch 4)*
- Target-scan score uses float eye distance and a continuous angle *(batch 4)*
- Noise and fired-flag clear periods are one tick short *(batch 4)*

**AI behaviour and vehicles**

- Black Hawk attack approach turns the wrong way, so its door gun can never bear *(batch 1)*
- DF1 searchlights (slgt) are not implemented; four DF1 missions place them *(batch 1)*
- Advancing soldiers' terrain probe: wrong distances, no random flank side, different search *(batch 1)*
- Parked or landed helicopters always spin their rotors and play the rotor loop *(batch 2)*
- Script alert (AI attribute 5) wrongly raises the team-alerted condition (df1-ai_behaviour-78) *(batch 3)*
- AI-attribute actions never reach helicopters and vehicles (df1-ai_behaviour-79) *(batch 3)*
- Helicopter target acquisition applies perception twice and uses a static alert byte (df1-ai_behaviour-82) *(batch 3)*
- Helicopters never set the 'team spotted team' bits (df1-ai_behaviour-83) *(batch 3)*
- Vehicle (and AI) rnd() returns only even numbers; tail-rotor damage twice as fast (df1-ai_behaviour-84) *(batch 3)*
- Helicopter altitude controller simplified (df1-ai_behaviour-85) *(batch 3)*
- Return-home ignores the load position and keeps only the first path end (df1-ai_behaviour-86) *(batch 3)*
- Destroyed DF1 searchlight makes no blast, effect or sound (df1-ai_behaviour-88) *(batch 3)*
- Smoke generators have no owner (df1-ai_behaviour-94) *(batch 3)*
- Follow-path start point and reaction timing differ *(batch 4)*
- Helicopter door-gun rounds make no AI noise *(batch 4)*
- Fresh door-gun target is tracked at once and the sight timer runs only with lift *(batch 4)*
- Helicopter idle mode thresholds differ *(batch 4)*
- Helicopter approach/return bands and aim range use Euclidean distance *(batch 4)*
- veh0 lateral turn term skipped on clockwise turns (CD sign bug kept) *(batch 4)*

**Player movement and controls**

- Crouch and Prone keys are toggles in DF.EXE, plus the platform lift when standing up *(batch 1)*
- Mover collision: 3-D origin box with smallest-axis push, no 0.5 m volume step, touch items and volume-less models never push (df1-player-24) *(batch 3)*
- Press commands ignored while Shift or Ctrl is held; jump is a press event (df1-player-25) *(batch 3)*
- Slope sliding and uphill test run while standing on an object (df1-player-26) *(batch 3)*
- Player move number: Jumping in the air and while sliding, Walking uphill, SLW/SCW/KNIFE still poses (df1-player-29) *(batch 3)*
- Selectable outside views F2/F5/F6/F7 and camera keys (df1-player-33) *(batch 3)*
- FO camera inset F9 / Tab (df1-player-34) *(batch 3)*
- Slide gradient sampled over 1 m bilinear instead of raw cells ±2 (4 m) *(batch 4)*
- Terrain slide moves twice as fast as GOG *(batch 4)*
- While sliding the player can become airborne or enter water; GOG's slide branch does neither *(batch 4)*
- No splash sound when the player falls into water *(batch 4)*
- DF1 quit-box Y/N keys come from DFGAME.BIN Misc STRMISC09/10 in GOG *(batch 4)*

**Weapons**

- Knife needs a 'chambered' flag in OpenDF and can stop working after a quick weapon switch *(batch 1)*
- Reload key does nothing for launchers, grenades and charges; GOG starts their load cooldown, and its end uses a spare even when one is already chambered *(batch 3)*
- First-person model of grenade, claymore and satchel stays visible when none are left *(batch 3)*
- DF1 Barrett recoil push applied only in DF2 *(batch 4)*
- Ammo refill cancels a reload in progress *(batch 4)*
- Fire-mission steering stops within 1 m and loses a tick *(batch 4)*
- DF1 fire-mission blast uses DF2's water rule *(batch 4)*
- Motion claymore range, measuring points and angle window *(batch 4)*
- Satchel/claymore ground snap from the feet *(batch 4)*
- Ammo crate message: GOG pushes empty STRMSIC17 line *(batch 4)*
- GOG seeded hash RNG for shrapnel *(batch 4)*
- Help screen lists weapons by long name *(batch 4)*
- GOG 'cycleb' action missing *(batch 4)*

**HUD**

- Player Health Easy health bar (0x483834) is never drawn *(batch 1)*
- Target identification / Break X (red X and name) missing in the main view and the scope *(batch 2)*
- Laser designator: 'No Target' blink unscoped, arms forced in scope, crosshairs option ignored *(batch 3)*
- Letterbox 2 does not move the HUD bars, messages, end panel and dead text next to the view *(batch 3)*
- Weapon name red rule differs from 0x4843b0 *(batch 3)*
- FO camera (F9 inset, Tab cycle) not implemented *(batch 3)*
- Friendly tags ignore GOG's people draw list (occlusion, range, hidden flag, Player Tags option) and omit the player's own body when dead *(batch 3)*
- Bright-green outline missing on the Paused, End Mission?, Messages, Briefing and Help boxes *(batch 4)*
- Panel darkening and HUD colours use the 8-bit path instead of the GOG default 32-bit renderer *(batch 4)*
- Help screen map key rows drawn 25 units too low *(batch 4)*
- Help screen weapon rows use long names instead of the short names *(batch 4)*
- GOG DFGAME.BIN texts: 'You are dead.' and 'Lose Condiitions:' *(batch 4)*
- Goals list does not stop at the first missing goal *(batch 4)*
- Help, briefing, Old Messages and Goals overlays not closed at mission start / replay *(batch 4)*
- Old Messages history not cleared on mission load / replay *(batch 4)*
- DF1 messages not cut to 119 characters *(batch 4)*
- Briefing word wrap differs from 0x42c4a0 *(batch 4)*
- Briefing paging: next page should repeat the last drawn line *(batch 4)*
- Briefing text insets are virtual units instead of screen pixels *(batch 4)*
- F8 picture-in-picture missing *(batch 4)*
- Enter/Space on the end panel act while the briefing is open *(batch 4)*
- GOG Audio Macros box (A) missing *(batch 4)*

**Rendering**

- Palm trees draw all six meshes, break caps included, instead of the palm routine's per-state set *(batch 1)*
- Palm trees draw all six meshes instead of the state's meshes (palm draw 0x479148) *(batch 1)*
- "tree" models: foliage meshes never turn to face the camera *(batch 1)*
- gnrc objects never animate (flags, fountain, BRDM, rock, saw) *(batch 1)*
- Terrain ring distances: detail and far-ring bands and the maximum terrain distance do not match 0x438404 *(batch 1)*
- TRN gamma/filter/saturation should grade only the terrain colour and detail palettes; everything else gets the df.cfg gamma *(batch 1)*
- TRN gamma/saturation/filter applied to sky, fog, water and highlight colours that DF.EXE leaves ungraded *(batch 1)*
- DF1 skinned meshes (soldier joints) are not skinned *(batch 2)*
- Letterbox size not read from / saved to df.cfg window_size (df1-render-215) *(batch 3)*
- Letterbox 2 does not move HUD bars with the 3D view (df1-render-216) *(batch 3)*
- Night missions: model highlight should be pure green (df1-render-217) *(batch 3)*
- Back side of two-sided 0x7F materials ignores the next material (df1-render-218) *(batch 3)*
- Animated materials ignore the frame slot; people never get their slot-1 variant (df1-render-219) *(batch 3)*
- Animated frame swaps only the texture, not the whole material (df1-render-220) *(batch 3)*
- Under water: textured surface overhead missing, < instead of <= (df1-render-221) *(batch 3)*
- Draw order: translucent faces before water, under-water tint before view model (df1-render-222) *(batch 3)*
- Detail shade 0 means detail colour replaces (df1-render-223) *(batch 3)*
- Terrain surface types matched against raw palette instead of graded (df1-render-224) *(batch 3)*
- Scope inset: ring/LOD focal over-scaled by 640/200 (df1-render-225) *(batch 3)*
- Far terrain rings use max-pooled height mips (df1-render-236) *(batch 3)*
- Water splash effects (ef3d) billboarded instead of world-oriented *(batch 4)*
- DF1 rounds and effects skip LOD choice and 1020 m / sphere / 0.5 px culls *(batch 4)*
- Held weapon drawn at every distance instead of hidden past its LOD count *(batch 4)*
- Body height snaps to the new frame's contacts instead of easing *(batch 4)*
- Transition target frame byte 0 lands on frame 1 in GOG *(batch 4)*
- Held death frame does not re-apply its root offset *(batch 4)*
- Cloud drift runs on wall-clock time *(batch 4)*
- Sky-cloud mips without pre-threshold and per-level -1 *(batch 4)*
- Water mips and mip selection differ *(batch 4)*
- Specular power loop capped at 8 squarings *(batch 4)*
- HUD panels darken by 1/2 in true colour, not 10/31 *(batch 4)*
- True-colour HUD colour values differ *(batch 4)*
- DF1 sky gradient step ignores view FOV/focal *(batch 4)*
- Detail brightening target uses graded/post sky colour 16 *(batch 4)*

**Sound and music**

- Dialog gap byte read from the wrong offset (always 0) *(batch 1)*
- Radio dialog lines wait for the clip length plus the gap; GOG waits only the gap, counted from the line's start *(batch 2)*
- DF1 GOG random numbers: shrapnel, explosion effects, ambience waits and binocular offset use 0x477860, reseeded to 0x5FC per burst *(batch 3)*
- Bullet whiz-by skipped on a round's hitting step; 40 m gate tested only the step start and used t > 0 *(batch 3)*
- Radio subtitles shown when dialog volume is 0 *(batch 4)*
- Sound trigger: definition pitch random drawn once per sound instead of once per layer *(batch 4)*
- Sound trigger: out-of-range layers skip their random draws and are not started *(batch 4)*
- One-shot voice stealing scans start order instead of fixed slots 0..15 and skips no pending voices *(batch 4)*
- One-shots capped at 10 voices outside missions *(batch 4)*
- DF1 voice gain/pitch updates applied to voices not yet started *(batch 4)*
- Looping sounds re-mixed every 8 render frames instead of every 8 60 Hz ticks *(batch 4)*
- DF1 front-end music stream volume uses cd_volume directly *(batch 4)*
- DF1 music VM opcode 0x33 sub-ops 8/9 and 2..7 not as in Df.exe *(batch 4)*
- GOG second sound bank NEW.PWF (audio macros, ids 451-470) not loaded *(batch 4)*

**Files and formats**

- GOG DF1 scrambles ITEMS.DEF, LOOP.DEF and every .ANM with a headerless scheme that OpenDF does not undo *(batch 1)*
- GOG profile is PLY 0x1B with 46 key bindings from DFDK.KEY (PrevWaypoint E, server Home); OpenDF kept 44 rows / 0x1A and had no Previous Waypoint *(batch 3)*
- GOG writes all nine profile files including unused slots; OpenDF skipped unused ones so a deleted soldier came back *(batch 3)*
- BMS type 3034 resolved to 'inhut2' in OpenDF but to the index-0 'null' marker in Df.exe (two extra huts in C04M05) *(batch 3)*
- df1-formats-159 Empty profile slots: NSEMPTY raw label, BLACKHAWK only when no PLY file loads *(batch 4)*
- df1-formats-161 df.cfg defaults: GOG attributes 2, internet_address 0.0.0.0, country/red_password/blue_password/mp_verbose *(batch 4)*
- df1-formats-162 df.cfg tokenizer (space/tab/comma, token[2]) and unclamped values *(batch 4)*
- df1-formats-164 3DI materials: no texture by name *(batch 4)*
- df1-formats-165 GOG intro ends on mouse press/right-middle release/wheel; click does not mark DF_LEAD *(batch 4)*
- df1-formats-166 Archive precedence: dfbase.pff first, first entry wins *(batch 4)*
- df1-formats-167 PrintScreen screenshot command *(batch 4)*
- df1-formats-168 KDV i42K skip test on the whole colour *(batch 4)*
- df1-formats-169 Item names from DFGAME.BIN 'Item Names' STR_ITM%04i *(batch 4)*

**GOG-only behaviour**

- DF1 GOG Previous Waypoint (E), 'server' (Home) key rows, help rows, waypoint skip rules and PLY v0x1b profile upgrade (df1-misc-207) *(batch 3)*
- Mouse wheel cycles weapons in GOG DF1 (wheel up = cycleb previous slot, wheel down = cycle next) (df1-misc-208) *(batch 3)*
- Pause blocks every action in OpenDF; GOG only drops actions whose flag byte (indexed by action id) has bit 1 set (df1-misc-209) *(batch 3)*
- Intro video: a left press, right/middle buttons or the wheel end INTRO.KDV in GOG, and clicks no longer mark DF_LEAD as skipped *(batch 4)*
- GOG DF1 HUD and overlay strings come from DFGAME.BIN (Overlays/Misc); OpenDF DF1 hard-coded the CD wording *(batch 4)*

**GOG-only behaviour**

- GOG DF1 scrambles ITEMS.DEF, LOOP.DEF and all .ANM files (no header, key 0x04960552); OpenDF reads them raw *(batch 1)*

### Delta Force 2

**Missions and scripts**

- Objects for multiplayer only (record flag 0x10 / 0x80) are spawned in single player, including player starts *(batch 1)*
- Multiplayer-only BMS records (flags 0x10/0x20/0x80) are loaded in single player; player start picks a multiplayer start marker (explains DF1 C02M01) *(batch 1)*
- Object yaw read from the whole dword at +0x34 instead of the int16; pitch at +0x36 ignored *(batch 1)*
- Script 'kill team' (action 2) kills only people; DF2 destroys the team's buildings and vehicles too *(batch 1)*
- Condition 1/13 'team p1 has hit team p2' is not implemented *(batch 1)*
- Area trigger (condition 1.10) uses spawn positions for everyone but the player, and ignores the marker z range *(batch 1)*
- Pungi sticks (101066) do no harm: DF2 takes 2 hp per contact when running, jumping or falling onto them *(batch 1)*
- Campaign switches are saved when action 8 runs, before the win event sets its own switch *(batch 2)*
- One-point paths are not marked 'stop at end' at load *(batch 2)*
- Kill team on team 1 does not kill the player *(batch 2)*
- Flag-2 events should run once at load and flag-4 events at mission end; OpenDF uses the DF1 flag-4 start rule *(batch 3)*
- AI property 6 (unalert) does not clear the team alert state tested by condition 1/3 *(batch 3)*
- Condition 1/2 'team p1 engaging team p2' ignores the target team *(batch 3)*
- Repeating events refire one visit (128 ticks) early *(batch 3)*
- Grenade crate (104118) cannot be picked up *(batch 3)*
- Single-player wind option (sp_wind) is ignored in missions *(batch 3)*
- Team-in-area still counts objects removed by action 4 *(batch 4)*
- Team totals and alive counts are live in OpenDF but cached in GOG *(batch 4)*
- Follow-path chooses the start point differently and re-thinks at once *(batch 4)*
- Teleport action skips dead team members (and the player for team 1) *(batch 4)*
- Action 5 cannot clear a switch *(batch 4)*
- Scope zero is not carried between missions (soldier +0x5C4) *(batch 4)*
- DF2 mission loading screen (df2_wait.pcx and Loading Text lines) is missing *(batch 4)*
- Triggered Text (action 6) is looked up in the DF1 campaign file, not the mission's text table *(batch 4)*

**Menus (front end)**

- Per-menu button order: DF2 sorts buttons by name before the z-order exchange sort; OpenDF keeps file order, so the Campaign world map covers the region buttons *(batch 1)*
- Campaign map shows other-campaign and locked region/mission buttons that DF2 hides *(batch 1)*
- Briefing map: marker objects (route lines, numbered yellow waypoint icons, team icons) not read or drawn *(batch 1)*
- Briefing map shows only numbered path points; DF2.EXE plots every BMS marker (team icons, numbered route with lines) *(batch 1)*
- Menu hit test excludes C/callback-less buttons and includes type-4 pictures (df2-frontend-405) *(batch 2)*
- Briefing map uses 1024 colour map at 1 m/px instead of <terrain>.JPG tiled 4x4 (df2-frontend-422) *(batch 2)*
- Enter/Escape menu hotkeys do nothing (df2-frontend-408) *(batch 3)*
- DF2 menu cursor CUR_TARG.TGA not drawn (df2-frontend-410) *(batch 3)*
- Connector lines missing on Choose Soldier and Main; Main help text not cleared (df2-frontend-411) *(batch 3)*
- Briefing scroll range, unbounded Down, 5 px inset (df2-frontend-412) *(batch 3)*
- Word-wrap pitch hA+4 and GOG wrap algorithm (df2-frontend-414) *(batch 3)*
- Scope sensitivity missing; mouse sensitivity clamp (df2-frontend-417) *(batch 3)*
- Campaign page blink, per-campaign region, re-click rules, Main resets campaign (df2-frontend-418) *(batch 3)*
- Replay (Space) after a win not recorded (df2-frontend-430) *(batch 3)*
- Push button leave/release (0x433f20 / 0x433a30) *(batch 4)*
- Click elsewhere while editing soldier name commits *(batch 4)*
- Keys during 12-tick entry stage kept *(batch 4)*
- Campaign/QM brief insets, QM rows wrapped FRK12N *(batch 4)*
- Slider geometry (p3=260, clamp) *(batch 4)*
- Change Gear magazine/range colon *(batch 4)*
- Choose Soldier stats colon, key, empty slot, ACCEPT greyed *(batch 4)*
- Campaign map title placement *(batch 4)*
- Owner draws get 0xC while leaving *(batch 4)*
- Hot-key flash counter per draw branch *(batch 4)*
- DF2_WAIT.PCX loading screen *(batch 4)*
- Quick Missions lists folder *.bms, +0x78 filter *(batch 4)*
- Menu sounds ignore channel *(batch 4)*

**Command map and waypoints**

- Command map text buttons use the toggle state as the frame; EXE frame is hover/pressed *(batch 1)*
- Text buttons use the toggle state as the frame instead of hover/pressed *(batch 1)*
- GOG command map has six tabs (Map, VON, Players, Team, Weapons, Mission); OpenDF had the CD's five *(batch 3)*
- Player/Team list rows start at y 240, hit only on the name; Team headings are three labels at y 204 *(batch 3)*
- Mission tab text: 15-px lines, empty lines skipped, clipped at 704, two scroll bars, settings box at (704,216) in 0xc06000 *(batch 3)*
- Command-map and radar person colours: player orange, teammates green, other sides red; non-person side 4 is 0xff027f *(batch 3)*
- Names toggle labels every drawn named object (+0x54) in its colour with a black outline *(batch 3)*
- Grid reference and map column labels do not wrap past x < -1000 m or y > 1000 m *(batch 3)*
- Mission route holds up to 16 points in DF2, OpenDF stops at 8 *(batch 3)*
- Action buttons: GOG removed 'Send Message to' and builds none on the VON tab *(batch 4)*
- Team list keeps dead teammates (only removed objects are skipped) *(batch 4)*
- Clicking the tab that is already open re-runs its enter hook *(batch 4)*
- The Game Paused box is not drawn over the command map *(batch 4)*
- Any held mouse button activates tabs, rows and buttons *(batch 4)*
- The command-map cursor is not kept inside the 640x480 screen *(batch 4)*
- Opening or closing the command map does not re-centre the pointer *(batch 4)*
- People toggle hides the player's own blip on the command map *(batch 4)*
- Info panel name line is object +0x54, not the ITEMS.DEF name *(batch 4)*
- Map filters: decorations go with Buildings, only listed waypoint ids with Waypoints, destroyed S&D decorations hidden *(batch 4)*
- Waypoint panel only for 6005/6089, not every marker *(batch 4)*
- North arrow and 'N' missing when Gridlines is on *(batch 4)*
- Grid labels lack the black outline and the edge-fit rule *(batch 4)*
- Previous waypoint (Shift + NextWaypoint) and the route-count rules are missing *(batch 4)*
- Shape 7 X is drawn at ±0.7r instead of ±r *(batch 4)*
- Map-to-world conversion floors to whole metres from an integer centre *(batch 4)*
- Weapon type 9 has a picture in GOG (WEAP frame 4) *(batch 4)*
- Command map draw order: route lines over object blips; selected object redrawn last *(batch 4)*
- Blips and selected-object positions use the nearest wrapped copy, not absolute mission coordinates *(batch 4)*

**Bullets, damage and explosions**

- Rounds stop on faces with no impact material (fences) instead of passing through *(batch 1)*
- Rounds stop at object faces with no impact material (fences); DF2 lets them through at full energy *(batch 1)*
- Rounds stop on object faces with no impact material (fences): DF2 lets them through *(batch 1)*
- GOG DF2.EXE ammo table moved and re-laid out, and its values changed; OpenDF reads the CD layout *(batch 1)*
- Rounds never over-penetrate people (flesh cost 100*(hp2+1), round goes on) *(batch 2)*
- Items with a negative second hp value (hp2 < 0) do not stop rounds *(batch 2)*
- Bullet tracers (team-coloured fading segments) are not drawn *(batch 2)*
- DF2 round explosions use effect 3 (and smoke) instead of 0x13 on terrain / 0x14 on objects and people *(batch 3)*
- Big blast (satchel, ibom, air strike, boat): wrong effect, extra smoke, 0.2 m lift, no AI noise, fragments not offset by heading *(batch 3)*
- Impacts and round explosions make no AI noise (terrain, people, water, blasts) *(batch 3)*
- Bullet impacts on CAL classes 7-11 (road, mud, ice, QS) give no effect, sound or noise *(batch 3)*
- Footsteps on CAL classes 8-11 (road, mud, ice, QS) are silent *(batch 3)*
- Blast damage to people and the player ignores walls and objects as cover *(batch 3)*
- Blasts and fragments carry no owner: same-side objects not spared, kills and team hits not credited *(batch 3)*
- Building collapse crush hits only people and vehicles *(batch 3)*
- DF2 delay grenade invisible in flight and its fuse has no heading variation *(batch 3)*
- Common blasts add a smoke generator; effects not drawn from the shared generator (df2-ballistics-319) *(batch 4)*
- Blast class +1 inside object box; amount truncation (df2-ballistics-325) *(batch 4)*
- Building collapse: oil-rig dust rise, per-step draw, effect draws (df2-ballistics-327) *(batch 4)*
- Claymore fan mirrored, lifted 0.2 m, smoke generator (df2-ballistics-330) *(batch 4)*
- Shotgun pellet pattern from heading-seeded generator (df2-ballistics-331) *(batch 4)*
- Flag 0x100 rounds damage noak items (df2-ballistics-332) *(batch 4)*
- Footstep water splash slot 0x17 (df2-ballistics-333) *(batch 4)*
- Delay grenade water/terrain/back-off/underwater/wind (df2-ballistics-335) *(batch 4)*
- Whiz-by for grenades / proximity test (df2-ballistics-337) *(batch 4)*
- AI sight ignores the detail-elevation raise (df2-ballistics-339) *(batch 4)*

**AI perception**

- Searchlight scan filters targets by team instead of side *(batch 1)*
- Soldiers holding a target never re-scan or switch to a closer, visible enemy *(batch 1)*
- Scan interval distance +0xAC is only set when the player is spotted *(batch 2)*
- DF2 team share (0x414350) is DF1's radio: no same-target refresh, no dead-target news, wrong range and state *(batch 3)*
- After hearing a noise DF2 barks and ends the think; OpenDF is silent and keeps going *(batch 3)*
- Dead-friend alert runs every think and ignores the scan's conditions *(batch 3)*
- Helicopter acquisition applies perception twice and never marks 'spotted' *(batch 3)*
- Searchlight 'spotted' mark has its arguments reversed *(batch 3)*
- Cover-me order bonus missing from the DF2 scan *(batch 3)*
- Spot reactions: relaxed spot thinks in 16, alert spot sets the 'spotted' bit, scan sets no last-known/seen *(batch 4)*
- Scan distance uses true length from the eye instead of floor(approxLen) from the body origin *(batch 4)*
- DF2 sight-line terrain test differs from 0x46e740 *(batch 4)*
- Noise list cleared every 434 ticks instead of 435 (and fired flags every 496 instead of 497) *(batch 4)*

**AI behaviour and vehicles**

- BRDM armoured cars never use their turret gun *(batch 1)*
- BRDM turret gun (aim, slew and burst fire) is missing *(batch 1)*
- Vultures (vult) have no AI: they never fly, circle, call or die with a sound *(batch 3)*
- Penguins (peng) have no AI: no waddling along paths, diving, swimming or death sound *(batch 3)*
- Fish / piranhas (prnh) have no AI: no swimming, chasing or biting swimmers *(batch 3)*
- AI soldiers ignore the player (and use a wrong segment test) when checking for a friend in the line of fire *(batch 3)*
- Destroyed ground vehicles (veh0, BRDM) coast and keep tilting instead of freezing *(batch 3)*
- DF2 helicopter altitude controller simplified (no water/object floor, no climb multiplier, band shift or anticipation; the bank is fine) *(batch 3)*
- Helicopter types: per-id rest heights, Black-Hawk-only attack yaw offsets, and per-id door-gun limits *(batch 3)*
- DF2 vehicle lateral term uses an unsigned sine index, and the veh0 steer only eases while moving *(batch 4)*
- DF2 helicopter targeting: no 'spotted' marks, perception applied twice, no night reduction *(batch 4)*
- Searchlight: spotted mark arguments reversed, the scan miss/draw fail handling, the init-think scan and dead-target handling *(batch 4)*
- AI rounds fired with the muzzle under water are not refused *(batch 4)*
- AI aim: the 'seen' clamp is not written back and errMul is not an integer *(batch 4)*
- Script follow-path start point: 3D, unwrapped, last of ties, think untouched *(batch 4)*

**Player movement and controls**

- DF2 move chooser: weapons without hand class use the plain Walking/Running moves (faster) *(batch 1)*
- GOG ladder climbing: up or down follows the view pitch, back keys no longer climb down, crouched players climb *(batch 1)*
- Ladder contact does not zero vertical speed, and the ladder no-gravity rule ignores prone (player and AI) *(batch 2)*
- Push-out against an object that also has a ladder volume is applied in full *(batch 3)*
- Crouch and Prone keys are toggles applied on the odd tick, with platform lifts *(batch 3)*
- Jump while crouched: DF2 jumps and stays crouched, OpenDF stands up *(batch 3)*
- Slope slide step applied on both ticks; DF2 slides on odd ticks only and keeps the move step on even ticks *(batch 3)*
- Slope and slide test runs while standing on an object *(batch 3)*
- Uphill walk-forcing is DF1 behaviour; DF2 sets 0x80000 but never reads it *(batch 3)*
- Terrain gradient for sliding: DF2 uses a +-2 cell central difference at the floor cell *(batch 3)*
- Scope view not suppressed while dead, airborne, swimming or on a ladder; zeroing works with the scope down *(batch 3)*
- scopemag key (E) and selectable scope magnification missing *(batch 3)*
- Mouse button presses do not dispatch bound one-shot actions (middle = Prone); right button hardwired to scope *(batch 3)*
- Mouse wheel: weapon cycle (both directions) or scope zeroing not implemented *(batch 3)*
- Shift/Ctrl key filter and Shift-reverse missing *(batch 3)*
- Mouse sensitivity clamped to 0..255 instead of 1..511 with 0 -> 128 *(batch 3)*
- Keyboard turn and look halved by NoRun; DF2 keeps the full rate *(batch 4)*
- Player's fall into water is silent; DF2 plays sound 0x8A *(batch 4)*
- Parachute canopy: player always gets the PFoil model, and canopies sit at the feet not the origin *(batch 4)*
- Fall damage divisor is 10354, not 10360.5 *(batch 4)*
- zoomin/zoomout map scale are held commands that repeat *(batch 4)*
- No input flush at mission start *(batch 4)*

**Weapons**

- DF2 weapon tables are read at CD addresses: with the GOG Df2.exe initDf2 fails and DF2 plays with Delta Force 1 weapons *(batch 1)*
- Manned guns do not switch the player to the gun's weapon (slots 11/12, modes 0x37/0x38) *(batch 2)*
- GOG video camera (gear/secondary type 36, mode 48, kind 13) does nothing in OpenDF *(batch 2)*
- Delay grenade: no random fuse and invisible in flight *(batch 3)*
- Suppressed weapons still mark the shooter as having fired *(batch 3)*
- Variable scope magnification (scopemag, E / Shift+E) and its '%2ix' readout missing *(batch 3)*
- DF2 scope view shown and zero/mag keys active while airborne, swimming, on a ladder, dead or in an outside view *(batch 3)*
- DF2 knife keeps a chamber test, so a slot switch during its cooldown leaves it unusable *(batch 3)*
- Ammo crates 4082 / 4089 (full refill) and 4118 (grenade refill) not handled; wrong message; refill cancels a reload *(batch 3)*
- Underwater fire rule: delay grenade (mode 0x31) exempt with 1/8 speed, burst cleared, AI blocked too *(batch 4)*
- Scope zeroing works without the scope up and is not kept in the soldier profile *(batch 4)*
- Reload key on launchers, grenades, claymores and satchels does nothing; cooldown end loads a spare only into an empty chamber *(batch 4)*
- Radio detonator press is silent and starts a cooldown *(batch 4)*
- Body-gear slot 7 (kind-0 modes) emptied instead of held *(batch 4)*
- Request Weapons resets the slot to 3, the scope flag and all projectiles in flight *(batch 4)*
- Satchel/claymore/camera placement height uses the feet, not the body origin *(batch 4)*

**HUD**

- DF2 friend/neutral target identification under the crosshair and in the scope is missing *(batch 1)*
- DF2 friendly tags use DF1's logic: 2 states instead of 3, wrong name source, placement and blue *(batch 1)*
- DF2 scope inset FOV uses the CD/DF1 integer formula (28/mag) instead of GOG's 28.125/mag fixed-point FOV *(batch 2)*
- Variable scope magnification (scopemag, E / Shift+E) and its '%2ix' readout missing *(batch 3)*
- Full-screen night vision is not suppressed while the scope inset is shown *(batch 3)*
- Scope night tint follows the mission night flag instead of the NVG toggle *(batch 3)*
- DF2 HUD text gets a black drop shadow the EXE never draws *(batch 3)*
- F_SML640.PCX cannot be loaded, so 640-mode HUD and the command map use F_SML800 *(batch 3)*
- HUD strips drawn over the overlay boxes instead of under them *(batch 3)*
- Forward Observer teammate view (F11) and Tab cycling missing *(batch 3)*
- Friendly aim pick scans every world object instead of the organics table *(batch 3)*
- Player waypoints are not marked in the 3D view *(batch 3)*
- Recent Messages overlay (R) uses the CD layout; GOG 0x448e40 shows both wrapped histories *(batch 4)*
- Laser designator crosshair rules (no crosshair + blinking STROVER90 unscoped, forced when scoped) *(batch 4)*
- Laser designator scope reticle drawn as a square instead of a diamond *(batch 4)*
- Scope inner cross segments tinted navy instead of darkened 50% *(batch 4)*
- Friendly tags drawn over the scope inset *(batch 4)*
- Crosshair gap on a manned gun should be W*1024/65536 *(batch 4)*
- Goals overlay always draws both headers *(batch 4)*
- Audio macros box (A key) and its 1-0 selection missing *(batch 4)*
- HUD message histories survive a mission replay *(batch 4)*
- Scope range readout ignores zeroing and caps at 2000 m *(batch 4)*
- Radar blips drawn after the rad frame instead of before *(batch 4)*
- Weapon name readiness wrong for kind 13 (video camera) *(batch 4)*
- Command map weapon picture for type 9 (M4/M203) missing *(batch 4)*
- Picture-in-picture first-person inset (F8 'pinp') in outside views missing *(batch 4)*

**Rendering**

- DF2 objects and effects are culled at DF1's 1020 m instead of DF2's 4080 m *(batch 1)*
- DF2 objects are culled at 1020 m (DF1's distance) instead of DF2's 4080 m *(batch 1)*
- GL far plane at 1126 m clips the terrain (and water) far short of the original's terrain distance *(batch 1)*
- Double-sided faces with material flag 0x4000 and slot 0x7f must use the next material on their back side *(batch 1)*
- DF2 skinned person meshes: per-vertex bone index ignored *(batch 1)*
- HIND and KA29 rotors are not posed: their 'hind'/'ka29' draw handlers are missing *(batch 1)*
- DF2 textures flagged 0x10 colour-graded in OpenDF but not in GOG *(batch 3)*
- DF2 model grade applied after shading instead of baked in before *(batch 3)*
- DF2 models get Blinn specular that Df2.exe never computes *(batch 3)*
- DF2 model intensity not capped; DF1 shade/highlight rows used *(batch 3)*
- Night vision applied before the weapon, and in scope/external views *(batch 3)*
- Under-water tint drawn inside the water pass instead of last *(batch 3)*
- DF2 weapon drawn after the frame with its own depth clear *(batch 3)*
- Above-water plane drawn before the opaque objects *(batch 3)*
- Player waypoint markers in the 3D view (0x450070) missing *(batch 3)*
- Single-player wind option (sp_wind) ignored *(batch 3)*
- After load the runtime grade is the df2.cfg gamma table: smoke/dust use it, not the TRN grade *(batch 4)*
- DF2 height smoothing multiplies by 0x8d3 >> 16 *(batch 4)*
- Rain/snow spawn around the camera eye instead of the player origin *(batch 4)*
- Weather comment says DF2's D3D frame draws no rain/snow *(batch 4)*
- Animation wrap/chain applies the landing frame's root motion *(batch 4)*
- Animation step fires the left frame's event late and frame-0 events on a move change *(batch 4)*
- Airborne people should show frame 0 of the Jumping move's animation *(batch 4)*
- DF2 water mip level from GL isotropic LOD *(batch 4)*
- DF2 clouds bilinear/trilinear instead of point-sampled per-row level *(batch 4)*
- Scope inner cross segments are a 50% darkening, not a navy blend *(batch 4)*

**Sound and music**

- Duplicate sound definitions in DF2.PWF: DF2 uses the first, OpenDF the last *(batch 1)*
- DF2 mission ambient loop markers ('Sound: loop, <region>') never play *(batch 1)*
- DF2 ambient loop markers (Sound: loop / loop2 items) are never attached *(batch 1)*
- DF2 ambient sound markers (snd0..snd3) never play *(batch 2)*
- Dialogue file name with an extension in the mission header loads no radio dialogue *(batch 2)*
- Bullet whiz (0x41): grenades excluded, step start or end inside the 40 m window, t >= 0 *(batch 3)*
- DF2 Havoc, Hind and Ka-29 rotor loop: OpenDF plays the Black Hawk loop, GOG the gazelle loop *(batch 3)*
- GOG terrain classes road/mud/ice/QS (8-11) give no footsteps or terrain impacts in OpenDF *(batch 3)*
- Dialog volume 0 should also hide the radio subtitles *(batch 4)*
- Footstep events play while the walker is airborne *(batch 4)*
- DF2 decorations' ambient think runs every 8 ticks, not 32 *(batch 4)*
- Loop emitter refresh runs per rendered frame, not per 60 Hz tick *(batch 4)*
- DF2 menu sounds: channel cut, fixed volumes and SFX factor *(batch 4)*
- DF2 front-end music Var1 forced every frame *(batch 4)*
- Sound layers: per-layer definition pitch, no range skip before the pick, zero-volume voices *(batch 4)*
- Definition flag 1 / layer flag 0x20 exclusion *(batch 4)*
- No NEW.PWF override bank *(batch 4)*
- DF2 looping voices start at a random offset and repeat from sample 1 *(batch 4)*
- DF2 music stream: queue count and gain latch *(batch 4)*

**Files and formats**

- DF2 menu button lists not built in DF2.EXE's order: Campaign world map hides the region buttons *(batch 1)*
- df2plyrs.sav: OpenDF only reads and writes version 10, so GOG (version 14) soldier files are discarded and then overwritten *(batch 1)*
- DF2 f_sml HUD/command-map font loaded with DF1's loader: F_SML640 never loads, F_SML800 band 9 mis-cut *(batch 3)*
- Command-map object/team names use ITEMS.DEF names instead of PeopleNames STRNAME%03i / Unknown *(batch 3)*
- 3DI v8 material texture index is signed in Df2.exe; OpenDF compares unsigned bytes and falls back by name *(batch 3)*
- JPEG decoding uses libjpeg's correct IDCT and fancy upsampling; Df2.exe's IDCT reads the wrong coefficient for the row-6 term *(batch 3)*
- f_LG font drawn opaque; GOG alpha-blends it by palette blue *(batch 4)*
- Missing menu text id shows '??TTID??' instead of '??<key>??' *(batch 4)*
- df2.cfg rewritten without GOG's mp_gameport / mp_vonport / mp_usealtnwport / flagreturntime lines *(batch 4)*
- PFF decryption is per entry in Df2.exe (flag at entry+2 bit 0, key 0x312a4ce) *(batch 4)*
- DF2.MNU reader ignores [LABEL] sections and does not trim keys like Df2.exe's config reader *(batch 4)*
- DF2 TRN parser: Df2.exe record defaults, case-insensitive keys, and height_scale/turbidity not reproduced *(batch 4)*
- DF2 .CAL surface table: line counting and token rules differ from Df2.exe for unusual files *(batch 4)*

**GOG-only behaviour**

- Chat/system messages are not word-wrapped to the HUD.DEF message boxes (df2-misc-518) *(batch 3)*
- Adjustable scope power (Scope Mag, key E) with the 'NNx' readout (df2-misc-521) *(batch 3)*
- DF2 in-game help (F1) as GOG's paginated binding list (df2-misc-524) *(batch 3)*
- DF2 far terrain rings lack the per-tile detail tint (df2-misc-525) *(batch 3)*
- Recent Messages panel: full-width box over the 3D view, chat and system histories in two columns *(batch 4)*
- Mission load assigns default radar shapes and labels (0x467430) *(batch 4)*

### Both games

**CD and GOG EXE support**

- build-detection *(batch 1)*
- df1-tables-gog *(batch 1)*
- df2-tables-gog *(batch 1)*
- find-exe-gog *(batch 1)*
- launcher-gog *(batch 1)*
- docs *(batch 1)*

## Partly fixed (what remains is in the note)

### Delta Force

**Missions and scripts**

- BMS record filter flags 0x10/0x20/0x40/0x80 ignored: multiplayer-only objects and co-op player starts are loaded in single player (explains the C02M01 wrong st… *(batch 1)* — Fixed the filter and start behaviour the title describes (it duplicates mponly-2 and start-1). Skipped the unrelated text in its fix and gogNote fields: removing the case-8 campaign-flag snapshot, saving the flags at mission exit (GOG 0x401e70) and keeping th…
- Single-player profile stats: kills credited on lost missions; other two stats never updated *(batch 3)* — Kills (+0x664) are now added only for won missions, including wins replayed with Space. +0x678 and +0x674 are still not updated: their per-slot sources in the 0x67f714 block were not decoded. OpenDF does not show them anywhere.
- Event scheduler phase: first visit on tick 0 and eighth index reset per mission *(batch 4)* — The tick-0 part was wrong: the current code already does ++ticks_ % 16, so the first visit is on tick 16. Fixed the part that was real: the eighth index is now a file-static g_eighth in Script.cpp, never reset, so it carries over between missions. The shared…
- Tick counter stops during pause; GOG's 0x6e471c keeps running (blinks, death-cam orbit) *(batch 4)* — Added frameClock_: it advances with ticks_, and also from realDt while paused. Moved these DF1 uses to it: the map route-point blink (0x48e070), the 'No Target' blinks (0x402300 and the 0x466090 scope), and the dead orbit (0x404640, DF1 only; DF2 keeps ticks_…

**Menus (front end)**

- df.cfg showgun ignored, and in-mission option toggles are not copied back to the menu/df.cfg *(batch 3)* — Added GameOptions.showGun (seeds gunVisible_) and MissionOutcome.crosshairs/showGun, written back to cfg_ after runGame. The other words 0x407980 copies back (flipmouse, mousescale, gamma, picinpic, 0x67f508/0x67f50c, 0x67f564) are not changed by OpenDF missi…
- Soldier slot events: no pushed frame or stats on press, current empty slot re-clickable, hover during delete *(batch 4)* — Not done: GOG restarts the pressed soldier's record teletypes at press (0x408800). OpenDF still restarts them when the selection changes, so the only difference is timing within the press. Everything else is implemented.
- Credits text alignment flags mapped differently; non-text credit objects not drawn *(batch 4)* — Alignment fixed. The optional picture/line/rect/circle objects are not implemented because the shipped CREDITS.SCR has none.
- GOG does not redraw menu pages while the window is unfocused or minimised *(batch 4)* — Drawing is skipped only while minimised (with a 10 ms sleep). Drawing an unfocused window is kept and documented in AUDIT F1 as an OpenDF amenity, because on macOS a newly opened window may lack key focus and would otherwise stay black.

**Bullets, damage and explosions**

- Wooden crates, furniture, cacti and palms: no gdeb debris, puffs or sounds on destruction *(batch 3)* — Game::woodDebris adds wdsm (1 puff, 6 wooddebris1/2, sound 0x139), wdmd (10 puffs, 32 pieces, 0x13A) and cact (8 puffs, 48 of 104058..62, 0x130), with the GOG offsets, velocities and draw order. A new debris Effect follows 0x453530 (velocity, yaw/pitch/roll i…
- Palm trees never break (no sections, sway, debris or PALM_TRUNK/PALM_EXPLODE) *(batch 3)* — The destruction part is done: at hp<=0, 0x44e6b0(-1) throws pieces by state (48/16/40) up the leaning trunk (palm2 1018 leans along yaw+0x70000000 by 0x1000 per step, the others yaw+0x80000000 by 0x400), adds 20 palmdebris5 when state is 0, and plays sound 0x…
- Effect, puff, shrapnel and blast-death random numbers: GOG's seeded 16-bit generator vs OpenDF's unseeded CD rand *(batch 4)* — Most of this was already in place: gogRand() / shrapRand() / shrapSeed() for effects and shrapnel, and the seeds at 0x4606b0 / 0x460950 / 0x460a30. Added now: spawnPuff uses the GOG generator (roll rand<<16 first, a full turn, then scale (rand+0x20000)>>4), a…

**AI perception**

- The dead-friend alert runs every think instead of inside the scan, with the wrong distance and quartering *(batch 4)* — Removed the per-think loop. DF1 now does the check inside every scan (the EXE runs 0x45e280 from the relaxed, alert and has-target paths too), and the result lifts the quarter. DF2's identical check moved into a shared deadFriendScan helper; DF2 output was id…

**AI behaviour and vehicles**

- After reaching a waypoint, soldiers and vehicles keep heading at the reached point (df1-ai_behaviour-95) *(batch 3)* — Vehicles (all kinds) and DF1 soldiers now turn to the next point on arrival. Not applied to DF2 soldiers: their walker in Df2.exe was not located, so they keep the old behaviour. DF1 veh0's own think (0x429bc0 gap) is not decompiled; it follows the same patte…
- Destroyed helicopter: deceleration not doubled, no repeat blast at ground impact *(batch 4)* — The finding was mostly out of date. The current code already ports the 0x451701..0x451755 impact (floor max(terrain, probe)+rest, blasts whether or not already destroyed, vz not reset) and already doubled the ramp, but only without lift. 0x44ffc0 doubles when…

**Weapons**

- Weapon cheats 0x78 / 0x79 / 0x7c not implemented *(batch 4)* — These are commands in the key table (GOG 0x4fb2fc, flag 0x100000, no key), and the CD table has them too. They are added as bindable actions, together with the infinite-ammo flag (no mag-- at 0x483240, no spares-- at 0x483654/0x4838a4). Still missing: the '~'…

**Rendering**

- Picture-in-picture squad-mate inset views missing (df1-render-234) *(batch 3)* — Implemented the pinp (F8, cmd 0x1e) and fo (F9, cmd 0x1f) actions, the 0x484a50 cycle, the 0x466d90 draw with the target hidden (0x4854d0), the inset's own focal and rings 2.2. Not done: command 0x1d (picinpic toggle, named 'tracers' in the key table, no defa…
- No muzzle flash on third-person weapons *(batch 4)* — The player's outside-view weapon now draws mesh 1/2 from the player's flash bits, with counter [0xc23800] stepping 1,2,0. Not done for AI/team-mates: no instruction found that clears person +0x6d bits 8/0x10, either in the xref, typed/untyped decompiles or th…
- Water ends at a 1023 m square instead of 1023 m view depth *(batch 4)* — Quad half-size 1449 m, discard past 0x3feffff/65536 view depth. The near-horizon band skip (|denominator| < 0x81/0x101) is not modelled. AUDIT V6 updated.

### Delta Force 2

**Missions and scripts**

- DF2 front end does not pass the female soldier or the face to the mission *(batch 3)* — g.female = soldier +0x278 > 9 (0x43c280). Game.cpp already picks the female DF2 soldier items from o_.female. The face index (0x4c5ad8 table, minus 10) is not passed, because OpenDF draws no head skin.
- Post-mission profile stats: GOG counts every single-player win, adds kills only on a win, and covers quick missions *(batch 3)* — On a win (campaign or quick): +0x29c += kills and +0x2a4 += 1 (no first-time test); a campaign mission ([0x4c3f88] set, [0x4c3f70] >= 0) also gets +0x30+4*idx = 1. Nothing is added on a loss. The flags word is now that of the soldier's campaign +0x28: campaig…
- First periodic event pass runs at tick 0 instead of the 16th tick *(batch 4)* — The tick-16 part was already fixed (++ticks_ % 16). Added the optional part: for DF2 the eighth index is now a global, g_df2Visit, that is never reset ([0x4e029c]), so it carries across missions. DF1's [0x4f2be4] is also never reset, but DF1 was left alone as…

**Menus (front end)**

- Key bindings: GOG has 59 rebindable actions, OpenDF has the CD's 48 *(batch 1)* — kDf2Keys now has 59 rows (kDf2KeyRows) with GOG labels, defaults and commands, read from the GOG Df2.exe tables 0x4c8144 and 0x4c7190 and from 0x441140. Rows 45/46 are now INCREASE/DECREASE_SCOPE_ZERO. The slider range is 50 (0x43f44f). Defaults, the repair,…
- Profile stats after a mission (+0x2b0/+0x2ac/+0x5c4) *(batch 4)* — The win-only kills and the every-win completed count were already in place. [0x67cdac] is the scope zeroing written by 0x44c770, not the magnification. It is now carried in through GameOptions.df2Zeroing (0x43c280). It is written back after every finished run…

**Command map and waypoints**

- Mission start resets the Players/Team scroll positions and pan *(batch 4)* — The scroll resets were already in the code (scrollPlayers, scrollTeam and scrollSettings are zeroed at mission start). Only the pan reset (panX / panZ) was missing and has been added.

**Bullets, damage and explosions**

- Glass / breakable meshes (material 0x10000000) never shatter; explosive rounds detonate on glass *(batch 3)* — Done: intact 0x10000000 faces on an object that is not a husk. LAW, impact and M203 rounds (armed or not) and delay grenades with more than 10 ticks left fly through without a blast and make a 40 m noise (0x453fe7). Bullets give no impact effect. Each mesh br…
- Cactus and oak destruction debris and oak ambient (df2-ballistics-328) *(batch 4)* — Done: oakt is in the DF2 ambient list. A live oak plays its sound 4 m up and waits ((r>>14)+5)<<6, with r from 0x41ea20. A destroyed oak waits 0x400. Skipped: the cactus and oak debris (0x429880 / 0x429a80). They spawn DF2 debris objects through 0x414850 whos…
- Penetration re-runs a whole tick (df2-ballistics-334) *(batch 4)* — Most of this was already right: passes already trace to the original tick end, gravity/drag/age are applied once, and passes reset each tick. The remaining gap is fixed: drag band and curve now use the tick-start speed (local_30), passed into df2Drag.

**AI perception**

- Wildlife perception (piranha, vulture, bird tree/sparrow) not implemented *(batch 4)* — The finding was out of date: piranha (prnh) and vulture (vult) were already in src/game/Wildlife.cpp. Bird trees (btre: 0x42d170 think, 0x42b070 scan, ENV_SPARROW_CALL, three item-104051 sparrows) and sparrows (spar: think 0x42d030, move 0x42d0d0, removed abo…

**AI behaviour and vehicles**

- DF2 palm trees: no partial breaking, frond/trunk debris or sounds when shot *(batch 4)* — Done: a DF2 palm at hp <= 0 now throws its palmdebris pieces with sound 300, using the existing woodDebris palm branch. Still open, as in DF1: the per-hit stage counters (+0xB4/+0xB8 over 10 calling 0x429e40(part)), the falling palmsection (0x42eae0 / psec 0x…

**Player movement and controls**

- DF2 third-person views and camera keys not implemented *(batch 3)* — Done: modes 0-3 (0x414240/0x413dd0), held Camera* keys, death forcing mode 1 (0x465f90), body drawn, gun/crosshair/target ID hidden, scope blocked, listener at the camera. Not done: the weapon in the body's hands, shots from the muzzle (0x44dd50 -> 0x416f60,…

**Rendering**

- Translucent faces not depth-sorted across the frame *(batch 3)* — DF2 objects' translucent parts and all DF2 effects (smk2 puffs, explosions) are now deferred and drawn far first by view depth, one sort per draw rather than per face as in 0x44c480. Faces inside one model keep file order, and there is one flush instead of GO…
- DF2 terrain ring table (0x472440) not used *(batch 3)* — For gen2: nearEnd = F/(2*p1), farBase = F/(2*p2), far mip k up to 8, terrain to 256*F/p2 m (capped by fog). Divisors: 2.0 main view, 3.2 binoculars, scope as DF1, 2.2 for the camera inset. Not done: the Turbo option (4 near bands). Commit 29fc928.
- DF2 far terrain lacks the detail-tile mean blend and height-weighted mips *(batch 3)* — New texColorFar_ is the colour map blended per cell as ((c*0x101>>4)*(s_t*0x101>>4))>>16 + m_t, saturated, with s_t = 255 - mean alpha and m_t the mean alpha-weighted colour. The far bands sample it (mip 0 too); the near rings keep the raw map. Not done: 0x46…
- DF2 near-ground relief uses DF1's e>=128 rule *(batch 3)* — For DF2, every near sample is now raised by e/256 m with no threshold. It uses the nearest texel of a 2x2-max pyramid (as 0x46d970) at level ceil(log2(dist/(F/p1/64))). DF2 tiles with no elevation map default to 0. OpenDF's 1/4 m mesh samples the finest bands…
- DF2 binocular FOV is 22.5 degrees in GOG *(batch 4)* — DF2 uses tan(11.25 deg) and df2FovDeg 22.5; DF1 keeps 22. The sky step is now llround(fov*65536)/W. Not done: driving the binocular state from moves 0x68/0x72/5; OpenDF keeps its existing inferred raised-state logic.

**Sound and music**

- DF2 voice allocation: 16 slots, pending, reserved 10-15 *(batch 4)* — DF2 (dfGame_ 2) uses 16 index-ordered slots. A not-yet-mixed voice counts as pending: replacing in place is the same as promotion at the next mix. Reserved flags are set after stopAll at DF2 mission load, and stopAll clears them. The front end has its own Aud…
- Music VM data and return stacks reset every tick *(batch 4)* — stack_ and returns_ are cleared at the start of Music::tick() when df2_. The +0x1c event push at 0x49627b..0x49628f (return stack gets +0x14, data stack gets the byte and 0, then dispatches via [ecx+0x40]) is not ported, as the finding said to read the handle…

**GOG-only behaviour**

- Zoomed mouse gain: GOG acceleration curve and Scope Scale (df2-misc-520) *(batch 3)* — DF2 mouse counts are now summed per frame and turned by 0x41eb50's gain: sens<<14 (sens under 1 becomes 0x80, max 0x1ff), or while the scope is up 0x40000 + 0x1000*accel*r^2 in the EXE's integer steps. Accel comes from soldier +0x5d8 + 0x80 (0x43c280), passed…

**GOG-only behaviour**

- Emplaced .50 cal guns (EWeap attrib, type-7 volumes, mounting) missing *(batch 1)* — Done. (1) The attrib parser now reads 4Team as 0x4000 and EWeap as 0x8000 (GOG 0x422cca / 0x422cfa). (2) df2Overlap: a type-7 volume sets flag 8 and does not push; df2PushOut puts those guns in Df2Contact::mounts. (3) World::df2Mount ports 0x4145a0 with its g…

### Both games

**CD and GOG EXE support**

- gog-df1-mission-run *(batch 1)* — GOG DF1 missions need the ITEMS.DEF/LOOP.DEF/.ANM descrambler from another worktree (branch worktree-wf_0f975b94-046-10, commit 1701f7b). This is not an EXE-table issue and was not included. Out of scope and not changed: the built-in fallback tables (kBuiltin…

## Review follow-ups

**Batch 1**

- [fixed] df1:player: Prone + Space stands the soldier up and then jumps on the very next frame. in.jump is the held key state (Game.cpp:5340 down(Action::Jump)). The prone branch (line 112) sets Stand but leaves jumpArme…
- [fixed] df1:map: The new command-map loop skips any world object with `removed` set (`if (wo && wo->removed) continue;`). In DF1 a picked-up goal item (4008/4018/4021/4034/4038/4077, Game.cpp ~1349-1353) gets `remove…
- [fixed] df1:ballistics: Only the `gnrc` half of the finding is fixed. The findings (df1-ballistics-1 title, gogExe) also name the bldg handler (GOG 0x453eb0, call to 0x453270 at 0x453f5a). In the GOG decompile the containin…
- [fixed] df1:ai_perception: In tickDf1Lights, the yaw turn uses the wrong speed. DF.EXE's turn (CD 0x47d31c, which is GOG 0x451850; I disassembled the CD bytes because Ghidra has no function for either) loads the yaw speed +0xe…
- [fixed] df1:mission: teamInArea (DF1 path) skips every object with o.removed. Script removal matches the EXE, because GOG 0x45f5b0 -> 0x45da30 zeroes the record and so its team becomes 0. A carried DF1 goal item is diffe…
- [fixed] df2:formats: saveSoldiers now writes the 'FPBC' version 14 header, but the records it writes are still in the CD's v10 layout. Soldier::defaults() is still the CD's 0x431c00: it fills only 48 key rows (0x90 dword…
- [fixed] df2:ballistics: The AI noise radius per face is wrong for most materials. The comment, commit message and docs say every face makes a 5 m noise (20 m for intact glass), and the code sets impacts_.back().noise = glas…
- [fixed] df1:render: Ring divisors are wrong when the DF1 binoculars are up. Game.cpp:5883 calls drawWorld(view, proj, binoculars_ && !playerDead()), so the binocular main view takes the zoomView branch at Game.cpp:2465-…
- [fixed] df1:render: The branch is cut from 20c6195, not cb31b6c. Its merge-base with cb31b6c is 20c6195, so `git diff cb31b6c..HEAD` shows the later commits (fullscreen 757459c, terrain frustum culling e6594ff, launcher…
- [fixed] df2:hud: The branch is based on 20c6195, not cb31b6c. Twelve main-line commits are missing, so `git diff cb31b6c..HEAD` shows them as reverted (fullscreen, Music, Terrain culling, launcher fonts and others).…
- [fixed] df2:hud: In an outside view, GOG Df2.exe 0x4642b0 returns straight after the 0x4189b0 pick (`if (DAT_00bf2b40 != 0) return local_20;`), before any crosshair, tick or name drawing. In drawHud the new call `if…
- [fixed] df2:frontend: The new GOG reserved-key rule (0x43e6c0) lets more keys be bound, but keyName() still only has the CD's name table plus '/', '\' and '''. GOG's 0x43f9b0 calls 0x43f520 first. That function names many…
- [fixed] df1:ai_behaviour: tickDf1Lights' turn (GOG Df.exe 0x451850) snaps and steps with the yaw speed after this tick's update. The disassembly reads +0xec into esi at 0x451885, before the speed changes at 0x4518c2 / 0x45190…
- [fixed] both:exe-builds: The word GOG Df2.exe inserts at +0x0C of each ammunition record is not an unknown field. It is the Extra Ammo clip count. GOG's ammo refill (gog2 0x44dfc0) starts each gun at n-1 spare clips, the sam…
- [fixed] both:exe-builds: load() now rejects every executable whose size and CRC-32 don't match the four known builds. Before this change, any DF.EXE / DF2.EXE was read and the existing sanity checks (M4 item 4057 / 30-round…
- [fixed] df2:render: The far plane is now 262144 m and the near plane stays at 0.1 m, on a 24-bit integer depth buffer (main.cpp / Game.cpp ask for SDL_GL_DEPTH_SIZE 24). Depth resolution is about z^2/(near*2^24): roughl…
- [fixed] df2:new: The player's 0x4145a0 call always passes dead=false (`world.df2Mount(-3, g, dir, false)`). In GOG Df2.exe the person move 0x4467d0 runs for the dead player as well, and 0x4145a0 clears both links whe…
- [fixed] df2:new: The 0x447120 heading clamp (±45° of the gun's heading) and pitch clamp (±22.5°) run only inside tickDf2 on odd 60 Hz ticks, that is 30 times a second. Game changes player_.yaw and player_.pitch from…
- [fixed] df2:new: While mounted, the player still fires his own weapon (the gun's weapon, HUD slots 11/12, is listed as not done). The shot starts from player_.eye(), which is now hardpoint 2 on the gun through its po…
- [fixed] df2:mission: The branch forks from 20c6195, not cb31b6c. Twelve commits on cb31b6c (launcher fonts, terrain culling, fullscreen, Music, Window and so on) are missing from it, so `git diff cb31b6c..HEAD` shows the…

**Batch 2**

- [fixed] df2:ai_perception: The new 0x41e880 port in bestTarget runs `b * 11` on int32_t. In GOG this is a 32-bit imul that silently wraps, but in C++ signed overflow is undefined behaviour. It overflows once the middle axis pa…
- [fixed] df2:player: The AI prone guard df2Prone(o) (moveNumber 110..127) is invented, and the commit says so ('(OpenDF)'). It is not what GOG Df2.exe does. The EXE tests organics +0x74 & 0x20 at 0x446e15 and 0x4470ac. T…
- [fixed] df2:ballistics: A round that goes through something but whose next pass would start at or past the end of the tick's segment gets the same tracer recorded twice. In the `goesOn && !spent` branch where `dot(next - fr…
- [fixed] df2:ballistics: A spent underwater round still damages people. GOG 0x453860 lines 74-77 set ttl = 0 for a round under the water slower than 0x4000 a tick, but the pass loop still traces. In case 3, line 420 then ski…
- [fixed] df2:weapons: drawCameraInset overrides only the eye (eyeOverride_ -> viewEye()). drawWorldPass still takes `fwd = viewDir()`, which returns player_.forward() while the player is alive. That player direction feeds…

**Batch 3**

- [fixed] df1:audio: DF1 land mines are not reseeded. In GOG Df.exe the mine goes off through 0x446fb0, which calls 0x4606b0 (gog1t/00446fb0 line 27), and 0x4606b0 starts with FUN_00477850(0x5fc) before its 0x45dc70 effe…
- [fixed] df1:frontend: Commit 8915e37 adds the GOG rows 44 (KBDPREVWP -> "PrevWaypoint") and 45 (KBDSERVER -> "server"), and renames row 28 to "AudioMacros". These bindings are pushed into GameOptions.bindings, but Control…
- [fixed] df1:ballistics: Game::woodDebris, wdmd branch: the debris pieces spawn 0.5 m too high. In GOG Df.exe 0x44efc0, the object copy at 0x44f0bd (EDI = original ESP+0x114) overwrites the puff's z+0x8000 with the object's…
- [fixed] df1:misc: df1PauseGated() treats Briefing as pause/dead-gated in every DF1 build. That is true only for GOG Df.exe. In the CD DF.EXE, 0x42d990 reads byte[0x4df51e + aid*0x2c], and for aid 0x25 that byte comes…
- [fixed] df1:misc: The SDL_MOUSEWHEEL branch runs for every DF1 build (only `!df2Hud_.on` is checked). The CD DF.EXE's window proc 0x48e010 does pass 0x100/0x200 per 120 of wheel delta, but the CD's in-game mouse hook…
- [fixed] df1:misc: NextWaypoint now always calls df1StepWaypoint(1), which uses the GOG 0x484c80 rules (nothing with fewer than 2 entries, skip entries with no object) on CD installs too. The new comment ('CD DF.EXE: (…
- [fixed] df1:mission: Delta Force flag-4 (mission-end) events are never visited, because the code assumes single player keeps [0x67f528] at 0. That premise is wrong. The single-player mission launch writes it to 1: at 0x4…
- [fixed] df1:mission: Df.exe records a won attempt replayed with Space at once: 0x404e30 calls 0x401e70, which calls 0x407de0 and then 0x407980, so the soldier record is updated immediately. OpenDF only accumulates earlie…
- [fixed] df1:ai_behaviour: The new DF1 move (meant to be GOG Df.exe 0x451590) still guards the impact destroy with `!v.dead && o.hp > 0`. The EXE does not. At 0x4516c4..0x45171f, when vz < 0 and z drops below max(terrain, +0xd…
- [fixed] df1:ai_behaviour: heloTargeting and fly() are shared with Delta Force 2, which the code still runs through fly() (the df2Rules branches). The DF1-only fixes are applied to DF2 helicopters without being checked against…
- [fixed] df2:audio: Line 1357 applies the whiz-by grenade exclusion `!(b.flags & 0x1010)` to both DF2 builds. Only GOG Df2.exe has that test (`test dword [ecx+0x64],0x10100000` at 0x4547a2). The CD DF2.EXE whiz block at…
- [fixed] df1:player: Mission load does not reset the new DF1 view state. GOG Df.exe 0x4829f0 memsets 0xc23820..+0x648, which clears [0xc23a34] (FO inset on) and [0xc23a1c] (dead-cam), and mission load then calls 0x42ac10…
- [fixed] df1:player: The first-person HUD items still draw in the outside views. In GOG 0x402300, the code returns straight after the target pick when [0x6ba3ec] != 0. The crosshair (the [0x67f504] lines), the same-side…
- [fixed] df1:player: In the FO inset from a team-mate, the living player's body is missing while the player is in first person. The new body draw in drawWorldPass requires outsideView() != 0. But 0x466d90 renders with [0…
- [not_applicable] df2:formats: df2PersonName() lost its `o.person` guard (now `o.missionIndex < 0 ? "" : df2RecordName(...)`), but drawDf2TargetId (Game.cpp ~5997) still uses it for the red name over the crosshair. GOG Df2.exe 0x4…
- [not_applicable] df1:render: The squad-inset keys are bound to the wrong DF.EXE commands. GOG Df.exe's command table (file offset 0xf9af0, records of 0x2c bytes) lists id 0x1d 'pinp' with VK 0x77 (F8), id 0x1e 'fo' with VK 0x78…
- [not_applicable] df1:render: The squad-inset state is not reset when a mission starts. GOG 0x4829f0 runs at every mission start: it memsets 0xc23820..+0x648, which clears [0xc23a34] (inset off), then sets [0xc23a38] = the player…
- [fixed] df1:render: drawSquadInset puts the mate's eye at the wrong height. 0x466d90 uses the target's organic origin (+0x24..+0x2c, the body origin) plus [0xc23a18], the player's eye offset above his origin (0xb333 = 0…
- [fixed] df2:ballistics: A delay grenade (CD 0x1000) that hits the ground or an object with 10 or fewer ticks left goes off once in OpenDF. In the GOG EXE it goes off twice. The ground case (0x453dd2) and the object case (0x…
- [fixed] df2:ballistics: Sparing objects on the owner's side is GOG-only, but it is applied to every DF2 install. GOG 0x454b60 has `if (param_2[4] && owner->side == obj->side) return` (0x454f52). Its CD counterpart, DF2.EXE…
- [fixed] df2:ballistics: Footsteps and bullet impacts on CAL classes 7-11 are GOG-only, but they are enabled for CD installs too. CD DF2.EXE 0x413920 (footsteps, GOG 0x415780) only has cases 1-6. CD 0x413b70 (ground impacts,…
- [fixed] df2:ballistics: The boat's five big blasts have no owner. GOG 0x402206..0x4022a3 passes the boat's +0xF8 (who killed it, written by 0x454b60 / 0x453510) as owner to every 0x45f910 call. OpenDF calls bigBlast(c + d,…
- [fixed] df2:ballistics: df2-ballistics-329 (palm trees: staged sections, palmsection/palmdebris, sounds 0x12E/300; 0x42ed40/0x429e40/0x42eae0) is not addressed at all, and docs/OTHER-GAMES.md says nothing about it. df2-ball…
- [fixed] df2:frontend: wrapped() now uses GOG 0x41c0e0's line pitch (height('A') + 4) for every install, including the CD's DF2.EXE. I checked CD 0x419d50 (decomp/df2): it advances iVar5 + 3. So CD installs regress from +3…
- [fixed] df2:frontend: The record lambda in run() always adds 1 to +0x2a4 on a win, and the comment calls this "GOG only". I checked CD 0x437550: it has no +0x2a4 increment (it only adds +0x29c, +0x2b0 and +0x2ac), so CD p…
- [fixed] df2:frontend: The new DF2 mouse path (about line 6431) applies GOG 0x41eb50's quadratic scoped rate to every DF2 build. Before this branch the CD build turned at a fixed eighth while scoped (CD 0x41adec..0x41ae13)…
- [fixed] df2:frontend: Two open items in the fix file are not handled on this branch, and neither the commits nor the docs mention them. df2-frontend-424: the command map still forces 640 (Game.cpp:6985 `useHudMode(df2Hud_…
- [fixed] df2:map: Row hits are limited to tabs 1..3 (`if (m.tab >= 1 && m.tab <= 3)` in df2MapClick), but the GOG EXE tests the list on every tab. 0x40c1e0 runs at the start of every command-map draw (0x40bda0), which…
- [fixed] df2:map: Older code, but this branch now shows the mismatch. The list rows (and their hit rects) name a teammate with df2SelName(Df2Sel{2,i}), which returns `world_.objects[i].item->name`, the ItemDef type na…
- [fixed] df2:hud: The scope night tint now follows nvg_ for every DF2 build, but the CD Df2.exe scope draw 0x44e2a0 (matched to GOG 0x455be0) still tests the mission header's night flag (`DAT_00d42ad8._2_1_ & 0x10`, i…
- [fixed] df2:hud: The magnification clamp in df2Zoom() and the kept [0x79a698] apply to every DF2 build because they are gated on df2_. match-df2.tsv lists 0x44e6a0 (the clamp) as new in GOG with no CD counterpart, an…
- [fixed] df2:hud: The '%2ix' magnification readout at (700, 480) is drawn for every DF2 build. CD 0x44e2a0 prints only the '%4im' zeroing at (700, 500); only GOG 0x455be0 adds the second sprintf of [0x79a698]. CD inst…
- [fixed] df2:mission: The kills that fc98ada adds to the profile are always 0. Game::outcome() (line ~310) counts kills by walking world_.objects, but runGame() calls game.outcome() after game.run() returns, and run() has…
- [fixed] df2:mission: A Space replay does not credit the run that was just won. In GOG Df2.exe the replay path 0x466ad0 calls 0x464140 -> 0x43c6f0 before reloading (0x466ba0(1)). Each finished run, including one that ends…
- [fixed] df2:misc: Behaviour that only GOG Df2.exe has is turned on for every DF2 install, so CD DF2.EXE users get it too. The gates are arsenal_.df2(), df2Hud_.on and an unconditional readout, never a GOG check. Four…
- [fixed] df2:misc: df2ScopeActive() approximates GOG 0x455790 as scopeShown() && !playerDead(), and its comment says the +0x64 & 0x422004 mask means 'not dead'. That mask includes 0x400000, the ladder flag (Player::onL…
- [fixed] df2:ai_behaviour: Signed int32 overflow (undefined behaviour) when the penguin redraws its heading while swimming (line ~361): `uint32_t((int32_t(rnd() & 0x7FFF) + 0x3C000) * 0x4000)` multiplies in int. Even the small…
- [fixed] df2:ai_behaviour: The 0x41e880 helper `approx()` (lines 27-33) brings back the int32 overflow UB that commit d85d76d just removed from Ai.cpp. `(b * 11) >> 5` and the sum are computed in int32_t, and the EXE's imul/ad…
- [fixed] df2:ai_behaviour: Animation accumulators overflow int32 (UB). The penguin waddle phase `a.phase += (a.speed + 0x4000) << 4` (0xC0000 a tick on land) and `a.phase += 8 * a.speed + 0x40000` (swimming) grow without bound…
- [fixed] df2:player: GOG-only scope behaviour is gated on df2_ instead of df2Gog_, so it leaks into CD DF2 installs. CD DF2.EXE has no scopemag record, no [0x79a698], no 0x44e6a0 and no 0x455790 (match-df2.tsv lists 0x44…
- [fixed] df2:render: The new DF2 model path is chosen per model with `g->gen2 = src.version >= 8`, but the DF2 grade is switched on for the whole scene, because Terrain::gradeLutTexture() now returns the TRN table for ge…
- [fixed] df2:render: In drawWorldPass the above-water plane (the waterMid block) is drawn straight after the opaque `visible` loop. Opaque draws that come later in the same function then land after a plane that has alrea…

**Batch 4**

- [fixed] df1:misc: playIntro now uses the GOG mouse behaviour for every install. Any press, a right or middle release, or a wheel notch ends INTRO.KDV, and the old `introLeadSkip_ = true` on a left press is gone. The f…
- [fixed] df1:audio: startDf1's round robin reads layer.variants[layer.next] without bounding it. The old start() used `layer.next++ % count`. layer.next is loaded straight from the file's u16 at layer+2 (Pwf.cpp), and P…
- [fixed] df1:ai_perception: For DF1, scan() no longer sets lastKnown, unseen/seen or flag 0x80. The EXE gets away with that because of two steps OpenDF's DF1 has-target path does not have. (a) GOG 0x45b993/0x45b9a3 -> 0x45b70c…
- [fixed] df1:frontend: textInput() now upper-cases typed characters in both builds. Only the GOG Df.exe does that: 0x40d9e0 passes the character through 0x49bf20 (toupper). The CD DF.EXE edit branch (0x406bf8, LAB_00406ea0…
- [fixed] df1:frontend: writeMenuShot() always writes GOG's sshot%03d.pcx (000..998). The CD DF.EXE has the same Shift+P branch in 0x406bf8 (`'P'` with word 2 == 0x100), but it calls 0x426afc. That function writes `screen%0…
- [fixed] df1:frontend: key() decides whether a key has a character with `(sym >= 32 && sym < 127) || ...`. Any key whose SDL keycode is a character above 126 counts as character-less: ø, æ, å, dead keys like ¨ ^ ` and othe…
- [fixed] df1:formats: firstWins_ is switched on by DFBASE.PFF and stays on for every archive mounted after it, including the CD's raw MISSIONS.PFF, AUDIO.PFF and VIEWS.PFF packs. DF.EXE never opens those packs at run time…
- [fixed] df1:mission: df1ThinkCounter_ (GOG [0x4f821c]) and endCheckClock_ (GOG [0x6e474c]) are documented as "never reset" and process-wide, but they are Game members. runGame() (Game.cpp ~9079) builds a new Game for eve…
- [fixed] df1:ballistics: This PR routes the GOG blast death-type draw (0x465977) through the seeded generator: `world_.rand16 = shrapRand`, plus `shrapRand() < 0x4000` in explode()'s player branch. In GOG the blast is not ru…
- [fixed] df1:ballistics: The DF1 fire-mission fragment yaw is `-rand<<16`, and the heading parameter is ignored. GOG 0x4477f0 sets local_20 = 0 (DF yaw 0) and calls 0x4470c0 -> 0x460950, so each fragment's DF yaw is 0 + rand…
- [deferred] df1:ballistics: The fix file has 13 findings, and finding df1-ballistics-119 (DF1 piranha bites: prnh 0x4528c0/0x4526a0, blast type 4 handled by 0x465b30, the effect+sound helper 0x4476e0, penguin 0x453f30) is not a…
- [fixed] df1:ai_behaviour: Debug output was left in by commit e56d51e. Line 60 in VehicleSystem::init prints `std::fprintf(stderr, "DBGH helo side %d flags %x\n", ...)` for every helicopter at load. Line 857 in doorGun prints…
- [fixed] df1:ai_behaviour: The DF1 flat range for the door-gun aim (heloTargeting, about line 766) and for each round's pitch (doorGun, about line 830) uses `std::floor(std::sqrt(...))`. That is a floor integer square root, bu…
- [fixed] df1:weapons: Game::drawHelp, the short-name loop: for an empty slot (mode 0), both builds print weapon_modes[0].short_name, so they do not skip the row. GOG 0x42be00 lines 70-87 and CD 0x48232c lines 66-84 index…
- [fixed] df2:ai_behaviour: The new door-gun underwater check (around lines 821-829) says the mode is 0x35 and takes sounds[3] from mode 53's ammo. In GOG Df2.exe the door-gun fire routine 0x42bc10, called from the helo think a…
- [fixed] df2:ai_perception: A reference into a std::vector is used after a push_back that can free it. World::spawn calls objects.push_back (World.cpp:203), and world.objects is a std::vector<WorldObject>. birdTreeThink (around…
- [fixed] df2:ai_perception: The DF2 sight-line length uses a floor integer square root, but Df2.exe's 0x41e830 rounds. In GOG 0x41e830, after the bit-by-bit floor root r, it does `if (r - 1 <= n - r*r) r++` (and returns n uncha…
- [fixed] df2:misc: df2DefaultLabels() only visits objects with missionGroup == 0. GOG 0x467430 starts at items[0] (0xc834e0, EBX=0xc83534=+0x54) and walks 0xe00 records of 0x114 bytes, which ends at 0xd74ce0. By the 0x…
- [fixed] df2:misc: The pass iterates world_.objects. World::build drops records whose item has no graphic or model, or whose type is 'effect', and that is typically marker items. For those records, forDf2Shapes falls b…
- [fixed] df2:player: The held zoomin/zoomout map zoom (new block near line 8288) is polled once per render frame, outside the fixed 60 Hz tick loop, so how fast it zooms depends on the frame rate. A 144 Hz display zooms…
- [not_applicable] df2:map: df2-map-289 (absolute mission positions) is never actually used in DF2. The new df2ObjectPos() reads mapTrack_, but trackMapPositions() is only called from drawDf1Map(), which only runs through drawC…
- [fixed] df2:hud: df2-hud-403 (the 'hudcolor' action, GOG Df2.exe 0x41f150 case 10: [0x67cd08] cycles 0..5, [0x67d378] = table [0x67d360][idx], saved and restored by 0x43c160) is still open in decomp/fix/b4-df2-hud.js…
- [fixed] df2:render: The binocular FOV 22.5 is gated on arsenal_.df2(), so a CD DF2.EXE install also gets 22.5. The CD build stores integer 0x16 (CD 0x45c610 line 111: local_4 = 0x16), and the record, the new code commen…
- [fixed] df2:render: The weather is now centred on player_.origin() every tick for every DF2 install, and the old weather_.setCamera(viewEye()) after drawWorld was deleted. The CD build reads its centre from DAT_004ee264…
- [not_applicable] df2:ballistics: The coarse terrain test in the new DF2 sight-line march (around line 938) compares against twice the right height: `if (p.y > 2.f * terrain->df2RaisedCell(p.x, p.z))`. GOG 0x46e740 tests `z <= word *…
- [fixed] df2:ballistics: In df2Drag, 'no wind under the water' (`windy = water < b.pos.y`) applies to both DF2 builds, but only the GOG EXE does this. The CD 0x44c2f0 (decompile lines ~1084-1105) always pulls toward the wind…
- [fixed] df2:frontend: Once the Quick Missions page has been shown, refreshQuick() calls Vfs::addLoose for every *.bms in the game folder, and Vfs::read/exists then prefer that file to the archive everywhere. GOG 0x442960…

## Skipped (with the reason)

### Delta Force

**Missions and scripts**

- Mission logic runs at 60 Hz; GOG ticks every 16 ms (62.5 Hz) *(batch 3)* — This changes the timebase of every per-tick system in both games: about 57 sites across Game.cpp's main loop, Weapons kTick, World and Player clocks, Vehicles, Ai and Terrain, and the fixed 60 Hz --record steps. Changing only the script/logic loop would put t…

**Command map and waypoints**

- NextWaypoint does not skip route entries whose object was never created *(batch 4)* — Already fixed on the integration branch. The audit's opendfLoc was stale, so nothing was changed.

**Bullets, damage and explosions**

- Detail-tile bumps ignored by DF1 sight lines *(batch 4)* — Already fixed on the integration branch before this task. No change made.
- Piranha bites (blast type 4) and the effect+sound helper not implemented *(batch 4)* — This would mean porting a whole creature AI (0x4526a0 + 0x4525d0 mover + 0x4528c0, penguin 0x453f30, blast type 4 0x465b30) for content no Delta Force mission contains, so nothing would change for the player. Documented as not ported in AUDIT X12.

**AI behaviour and vehicles**

- DF1 wildlife (seagulls, sparrows, vultures) has no behaviour (df1-ai_behaviour-76) *(batch 3)* — Skipped, not wrong. This is a new subsystem: four routines to disassemble, a wing-flap draw routine and new object kinds. It needs its own task rather than a partial port.
- Follow-path action with path 0 (stop) is ignored (df1-ai_behaviour-77) *(batch 3)* — Already fixed before this pass; the audit's claim of 'path <= 0' no longer matches the code.
- No DF1 crosshair/scope friend identification (df1-ai_behaviour-89) *(batch 3)* — Already fixed before this pass.

**Player movement and controls**

- Uphill forced walk: dot with move direction, needs one axis >0.7 and both <1.3 *(batch 4)* — Already done at 7363103. The dot uses moveAngle = yaw - moveDirection*pi/4, and the else-if chain already means both axes are under 1.3, one is over 0.7, and the player is not sliding. The sign matches GOG's (local_48, local_44)·(gx, gy).
- Picture-in-picture of the player's own view (picinpic) not implemented *(batch 4)* — Depends on the outside-camera feature, which does not exist. Without it the feature cannot be reached.
- GOG-only PrevWaypoint (cmd 0x2f, key E) and backward weapon cycle (cmd 0xc0 cycleb) missing *(batch 4)* — Already done at 7363103.
- Paused/dead command filter does not follow the table flag-1 rule *(batch 4)* — Already done at 7363103 (AUDIT K4). The table is indexed by command id, so only scope and briefing (GOG) have flag bit 0. The finding's list of blocked commands is wrong.

**Weapons**

- Eye easing applied to the whole eye height *(batch 4)* — Not done. In GOG the eye is the object origin plus the eased offset, and on a stance change the origin stays where it is: the feet then fall or are pushed up. OpenDF's DF1 land physics keeps the feet and switches landOrigin() instantly. Easing only the offset…

**HUD**

- DF1 crosshair still drawn while dead (external camera) *(batch 4)* — Already fixed at 7363103: the DF1 crosshair, 'No Target' and target-ID branches in drawHud check !playerDead() && outsideView()==0 (0x402300). No change needed.
- Help screen lacks the GOG 'Prev Waypoint' and 'Server Screen' rows *(batch 4)* — Already present at 7363103: drawHelp adds HELPPREVWP/HELPSERVER for Df1Gog, and KeyMap.h has the GOG rows 44/45.
- GOG Previous Waypoint key (E) missing *(batch 4)* — Already implemented at 7363103: Action::PrevWaypoint and df1StepWaypoint for Df1Gog (0x484d00).

**Rendering**

- Bullet tracer streaks are not drawn (df1-render-214) *(batch 3)* — This was already fixed on the integration branch. The finding was written against older code.
- Command map stays open after the player dies *(batch 4)* — Already fixed in 7363103.
- Terrain colour mips averaged in KDV space and re-quantized *(batch 4)* — Needs 0x47fee0's candidate-bucket tables (0xbffac8/0xc01ac8, an approximate nearest search) and the 0x500b0c decode table to be reproduced. That is too much for this pass and there is no reference implementation in OpenDF.
- Detail-tile mips average only non-zero indices *(batch 4)* — OpenDF's DF1 near rings read detail level 0 only (texelFetch level 0; vertex bumps sample the base level), so the GL mips are never used. Matching GOG needs its per-ring detail-level choice (the 4 near-ring spans 0x489ff0/0x48a9c0/0x48b420/0x48bd20) and the 0…
- Model textures/material colours not quantized to 254-colour per-set palettes *(batch 4)* — Needs a faithful emulation of median cut 0x47edd0, 0x486670/0x47e6d0/0x486880 and the 0x47fee0 buckets. That is a large standalone task, as the finding itself says (verify median-cut details first).
- Decal faces flat nearest depth vs polygon offset *(batch 4)* — Needs each triangle's minimum view depth in the fragment stage (a geometry shader or per-vertex copies of the other two vertices). Not done here; polygon offset left as is.
- 2000-face queue limit per frame not modelled *(batch 4)* — The finding needs a project decision (faithful budget or a documented engine limit). Left for the user.
- Birds drawn static: no bird wing flap *(batch 4)* — Depends on the bird mover, which is not implemented, so there is no flap angle to drive the pose.

**Files and formats**

- df1-formats-163 showgun ignored / F3-F4 not saved back *(batch 4)* — Already implemented on the integration branch; the finding was stale. OpenDF missions never change picinpic or mousescale, so their copy-back would be a no-op.

**GOG-only behaviour**

- GOG DF1 uses the rol11/rol4 PRNG (0x477860), reseeded to 0x5FC by the satchel, grenade-fan and blast-only bursts *(batch 4)* — Skipped because it is already fixed. The remaining rand()*2 effectRng_ branch runs only when the exe is the CD DF.EXE: it is that EXE's own generator (0x435db4, whose seed 0x435dbc is a bare ret), kept on purpose by the existing per-build gate. One possible s…

### Delta Force 2

**Missions and scripts**

- Space replay does not record the finished attempt (profile update and switches) *(batch 4)* — Already fixed at 7363103. The Space handler pushes each finished run into replayed_ (0x466ad0 -> 0x464140) and calls leaveMissionFlags(). FrontendDf2 then applies the 0x43c6f0 record to every replayed run. The audit's report is out of date.
- Mission-load default labels and radar shapes (0x467430) are not applied *(batch 4)* — Not implemented. The STROVER id table in 0x467430 is long and partly unclear in the decompile, and the names also need the radar/map name drawing (0x4273b0), which belongs to the HUD area. Per the audit, the affected item ids appear only in multiplayer BMS, s…

**Menus (front end)**

- Command map at 800x600 / 1024x768 (df2-frontend-424) *(batch 3)* — Not done here: the finding itself is right. Switching useHudMode alone would be wrong, because drawDf2CommandMap and df2MapClick are written in 640x480 pixels throughout: 128-px tiles, 8-px edges, cursors, WEAP640, F_LG640, 16-px button rows, and the radar's…
- DF2 in-game help screen is DF1's layout (df2-frontend-432) *(batch 3)* — Not done here because it is a whole feature port, not a contained fix. It needs: the runtime binding table (0x4e02a8, 0x200 records of 0x70 with category +0x2c and HelpScreenActions descriptions, 0x41e310); pages grouped by category, at most 14 rows each (0x4…
- Slider press pages by user1 *(batch 4)* — Already fixed at the start commit (sliderPress). Only sliderEvent was rewired, as part of the mouse handler port.
- Briefing map sliders / step 10 / MAP CENTER / click *(batch 4)* — Already fixed at the start commit: mapCentre (0x43cdf0), ±10 arrows, mb_MapCenter, and the mb_Map event-3 press. but_mb_mpimg is type 1, so the new handler delivers event 3 to it.

**Command map and waypoints**

- Tab x positions use integer gaps and a fixed 80..96 hit band *(batch 4)* — Already fixed in the current code: df2MapTabs uses int gap = (615 - x) / 6 with a gap/2 shift, the draw and the click share it, and the click tests y 80..96 inclusive. The finding was out of date.

**Bullets, damage and explosions**

- Palm trees never break (staged sections, palmsection and palmdebris missing) *(batch 3)* — This needs the palm think 0x42ed40, 0x429e40 and 0x42eae0 with the psec mover, the palmsection and palmdebris items 104064-104068/104075 and their sounds. That is a new debris/mover subsystem OpenDF lacks, too large for this pass. Still open.
- Piranha bite never happens (df2-ballistics-338) *(batch 4)* — Already ported: WildlifeSystem::fishAttack (0x42d920) does the 0.75 m bite, effect 0x10, sound 0x49, the 1/8 kind-4 hit and the 0x100 cooldown. The finding's 'no prnh AI' is out of date.

**AI behaviour and vehicles**

- Seagull and sparrow AI absent (not placed in shipped missions) *(batch 4)* — Skipped. No shipped mission places 104050 or 104051, so nobody sees the gap. A faithful port needs both movers: the seagull's has a death fall with bounce and sound 0x48, an altitude band, a capped yaw rate and the +0xB0 wing flap. It also needs a wing-flap d…

**Player movement and controls**

- Scoped mouse sensitivity: fixed 0x40000 base plus scopescale acceleration, not mousescale/8 *(batch 4)* — Already fixed at 7363103. The finding cited old code.

**Weapons**

- DF2 tracers and underwater round trails are not drawn *(batch 3)* — Already fixed before this pass. The audit said 'no tracer code in src', but 91fd493 already has DF2 tracers that match 0x451ae0, 0x452290 and 0x452610: the colour by side, the 0x1c90 / 0x60 conditions, white under water, and the life 0x1e fade.
- Weapon cycling: no reverse cycle (Shift), wrong wrap from the binoculars *(batch 4)* — Already fixed at 7363103: it follows 0x44d3c0, with Shift for reverse and the wrap from slot 10.
- Grenade, satchel and claymore hand model still drawn when none are left *(batch 4)* — Already fixed at 7363103: drawViewModel returns when !arsenal_.viewModelShown(), which applies the df2Flags 0x100 rule from 0x44ea10.
- Crosshair spread accumulator (df2.cfg attributes 0x1000 single player / 0x800 multiplayer) not kept *(batch 4)* — Not done: it needs the shrink and crosshair-draw logic, which belongs to the HUD/crosshair area in Game.cpp, outside this focused weapons pass. Left open.
- Missing DF2 ammo cheats (SunAndSteel refill, DieWithYourBootsOn infinite ammo) *(batch 4)* — Not done: these are action-table commands reached through the command queue (0x420670), and OpenDF has no typed-cheat or console entry path. The flag would be dead code without one, and the other DF2 cheats (TheTrooper, StillLife, Revelations) are just as abs…
- F1 help screen uses DF1's layout and weapon names instead of DF2's paged action list *(batch 4)* — Already fixed at 7363103.

**HUD**

- HUD chat/system messages are not word-wrapped to the HUD item width *(batch 4)* — Already implemented at 7363103: raw rings plus wrapped rings filled by df2Wrap (0x41be90 rule) and re-wrapped on a mode change (0x4215e0). The finding was stale.
- F1 in DF2 shows DF1's fixed help screen; GOG has a paged, sorted binding list *(batch 4)* — Already implemented at 7363103 (0x41e370/0x41e3c0/0x41e620, paging with wrap). The finding was stale.
- Video camera (VCam, F12) picture-in-picture missing *(batch 4)* — Already implemented at 7363103 (0x4568a0 inset, VCam toggle, camera steering). The finding was stale.
- HUD colour cycling ('hudcolor') not implemented *(batch 4)* — The action can never be triggered in the GOG build: it is unbound, cannot be bound, and its value is not saved. [0x67d378] therefore always stays white, which OpenDF already draws. No user-visible difference, so not implemented.

**Rendering**

- DF2 scope inset FOV 28.125/zoom in 16.16 *(batch 3)* — Already fixed at the base commit: the DF2 branch uses 0x46500000 / (mag*640) and halves it with the fraction kept.
- DF2 stored height map lacks the +2*max detail bias *(batch 3)* — Not done. The main readers are the terrain line-of-sight and bullet tests in World.cpp (AI and ballistics, worked on by other agents). These need 0x46e420's raw-cell march instead of bilinear heightAt, and 0x46e610/0x46e740/0x46e870/0x46eaa0 still need tracin…
- DF2 LOD draw-code handlers incomplete; null codes should draw nothing *(batch 3)* — The null-code part is wrong: every v8 model reachable through the VFS has a known code in each of its LODs. BOOKCASE, GLASS2 and BARRELX, cited as null, are 'gnrc'. Porting the remaining handlers (tank, palm, tree, lamp, bird, fish and others) is a separate p…
- Video Camera item and team-cam PIP missing *(batch 3)* — The Video Camera inset (mode 2) is already implemented at 91fd493 (drawCameraInset; commit 91fd493). The team cam (mode 1, [0x79a6a8], 0x4559d0 checks the selected team member) is still missing; it is a separate feature.
- GOG's palette grade packs channels with sign extension (borrow and wrap) *(batch 4)* — OpenDF grades DF2 in shaders, in float, at draw time and often after filtering or mixing, so the per-entry borrow can't be reproduced faithfully. Doing it properly means moving DF2 grading to a CPU bake at load across Models and Terrain, which is what the fin…
- Terrain-row occlusion pre-cull (0x472110 + 0x44f3f0) not implemented *(batch 4)* — The pre-cull combines a per-row limit from the software band table (band top rows, 2000 m above the terrain) with a read of the software z-buffer rows (0xc303a4) at the object's top row. OpenDF's GL terrain has neither the band top rows nor a CPU depth buffer…
- Cloud drift timed by wall clock instead of the game tick counter *(batch 4)* — The finding's premise (clouds stop when paused) is wrong: GOG's counter runs every loop tick, paused or not, so 60/s real time already matches it. Only added a comment in Terrain.cpp and a line in OTHER-GAMES.
- View model of an exhausted throwable is still drawn *(batch 4)* — Already implemented: Game::drawViewModel returns when !arsenal_.viewModelShown(), which checks the DF2 mode's raw +0x3C flag 0x100 against spares and chambered rounds (Weapons.h).

**Sound and music**

- Radio dialogue lines spaced by clip length plus gap; DF2 uses the gap byte alone and frees the slot at once *(batch 3)* — Already fixed by commit f8ae759 before this audit's base. The finding describes old code.

**Files and formats**

- Type-4 (anim) image buttons get a hit box, so the Quick Mission/Demo map picture acts as a 'back' button *(batch 3)* — Already fixed in the current code by commit 2da0218. layout's image branch zeroes w/h and sizes only type 1 (w, h/4) and types 2/3 (w, h, then the p3 override). Type 4 stays 0x0, so hit() never accepts it. No change needed.

**GOG-only behaviour**

- Video camera gear (mode 48, item 4138): deployable camera, inset, steering (df2-misc-523) *(batch 3)* — Already done on the integration branch before this run, so the finding is out of date. The new scoped-gain code keeps routing mouse turns to arsenal_.turnCamera when cameraSteered().
- Scope inset hidden while dead, airborne, swimming, on a ladder or with a single-player PIP *(batch 4)* — Already fixed at 7363103 (commits 7313902, 54e8984, efa65da); the audit's Weapons.h:256 citation is out of date. GOG 0x455790's dead/airborne/swim/ladder, zoom, outside-view and moving tests are all in place, GOG only, matching the CD/GOG split from efa65da.

