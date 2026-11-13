# How the original game looks and behaves

Reference notes taken from a 9-minute recording of the original game (campaign
1, Peru, mission 1 "Insurrection", 1152×720 capture) and the CD's manual.
Positions are fractions of the screen width (x) and height (y).

## Start-up and menus

1. Black screen, then the **Delta Force logo** (metallic triangle on a dark red
   world map, "© 1998 NovaLogic, Inc.").
2. **Choose Soldier**: list of 8 operative slots ("NEW OPERATIVE"), portrait
   on the right with *Change Name* / *Delete Soldier*, a stats box (operative
   name, campaign kills, completed missions, multiplayer kills/deaths/team
   wins). Footer buttons: *Accept Soldier*, *Exit Game*.
3. **Settings**, with a tab selector cycling through:
   - *General Settings*: Input (mouse sensitivity slider, flip mouse), Video
     (colours "16 million", resolution "800 by 600", turbo, windowed), Audio
     (SFX / music / dialog volume sliders).
   - *Difficulty Settings*: enemy AI, player health, squad members, tracers,
     friendly-fire warning, power-ups, GPS icons ("Show everything").
   - *Message Macros*: F1–F10 radio messages ("The package is secure",
     "Prepare to assault on my mark", ... "Son, it's gonna be a long day!").
   - Footer: *Accept Settings*, *Exit Game*.
4. **Single Player Game**: campaign list (Peru, Chad, Indonesia, Uzbekistan,
   Novaya Zemlya), a campaign map ("Campaign Map: SELECT MISSION") with
   mission markers, an overview box. Footer: *Back*, *Accept Mission*, *Exit Game*.
5. **Mission Briefing**: satellite map on the left with the route (waypoints
   as a numbered polyline, start/checkpoints/objectives), a crosshair-style
   pan cursor, a text panel with the scrolling briefing (Mission, Execution,
   Expected Enemy Forces, Allied Support, Situation) and a caption box under
   the map that follows what is highlighted ("Eliminate all mercenaries at
   this base", "Alpha Team insertion point", ...). Buttons: *Change Gear*,
   *Back*, *Start Mission*, *Exit Game*.
6. **Change Gear**: tabs *Primary Weapon*, *Secondary Weapon*, *Sidearm*; a
   large picture of the selected weapon (`M4_M203.PCX`, `M40A1.PCX`,
   `MP5SD.PCX`, `SAW249.PCX`, `SPEC45.PCX`, `HS22.PCX`, ...), a description box,
   and a "Current Gear" list. Footer: *Accept Current Gear*.

All menu screens share one look: dark green panels with bright green
outlines and scanline texture over a dimmed military backdrop, a round
crosshair mouse cursor (`CURSOR*.PCX`), green text in the `F_SML` font.
Panel art: `DF_PNL*.PCX`, `*_PLT.PCX`, `BTN_*.PCX`, `HSLD_*`/`VSLD_*`
(sliders), backgrounds `DF_*.PCX`.

## In-game screen

The 3D view sits between two black HUD bars, each about 12% of the screen
height, outlined in green.

**Top bar** (y 0–0.122):

| Element | Position | Content |
|---|---|---|
| Stance icon | x 0.05–0.10 | green soldier silhouette: standing / crouching / prone (`POS640.PCX`, 50×150, three 50×50 frames, prone at the top) |
| Weapon line | x 0.115, y 0.02 | "M4 Assault Rifle, Single Shot" (names from the `DFGAME.BIN` string table) |
| Magazines box | x 0.115–0.21, y 0.06–0.10 | magazine icon + spare magazines |
| Rounds box | x 0.22–0.31 | rounds in the magazine |
| Chamber bar | x 0.32–0.33 | solid green when a round is chambered |
| Weapon silhouette | x 0.36–0.48 | green picture of the weapon (`WEAP640.PCX`: 13 frames of 43 px — frag grenade, laser designator, satchel, Barrett, M4/M203, LAW, claymore, HS .22, SAW, M40, .45, MP5, knife) |
| Divider | x 0.547 | |
| Compass | centre x 0.597, y 0.061 | north-up ring with N/E/S/W; blue needle = your heading; green dot on the ring = direction to the next waypoint |
| Grid box | x 0.654–0.752 | grid square, e.g. "H12": 100 m squares in mission coordinates from (−1000, +1000); letters west→east (A..Z, AA..), numbers north→south (DF.EXE 0x43DCE0) |
| Carrying box | x 0.761–0.987 | "Carrying: Nothing" (or Black Box, Laptop, Code Book, Case, ...) |
| Waypoint box | x 0.654–0.987, y 0.07–0.11 | "WP3: CP BRAVO (83m)" (format `WP%i: %s (%s)`) |

**Bottom bar** (y 0.875–1): radio/information messages on the left in light
grey ("Proceed southwest and neutralize all hostiles.", "Charlie 2 is down.");
divider at x 0.673; player name ("ROK") and mission name ("Insurrection") in
green on the right.

**Crosshair:** small green cross with a centre gap. Friendly squad members
get blue name tags floating over them ("CHARLIE ONE").

**Zoom / scope:** not full-screen. A magnified circle appears on the right
(centre x 0.75, middle of the 3D view, radius 0.25 of the screen height) with
a thin green ring, short green crosshair ticks and the range to what is under
the crosshair at its lower right ("265m"). The normal view continues around
it and the first-person weapon is hidden while zoomed.

**Command map (C):** drawn over the 3D view (bars stay). A zoomed-in
satellite map around the player with a bright green 100 m grid (75 px per square at 640×480), column letters
along the top and row numbers down the left, the waypoint route as a green
polyline with circles, enemies as red marks, friendlies blue, and a compass
rose at the lower right.

**Mission end:** a translucent dark panel with a green outline near the top of
the 3D view: "Congratulations! Mission Completed Successfully." / "Hit Enter
to end mission." / "Hit Space Bar to replay mission".

**Weapon names** (from `DFGAME.BIN`): "SpecOps .45 caliber pistol", "High
Standard .22 suppressed pistol", "M4 Assault Rifle, Single Shot", "M4 Assault
Rifle, Burst", "M203 40mm grenade launcher", "SAW Auto. Weap., Full Auto",
"H&K MP5 SD, Single Shot / Burst / Full Auto", "M40 Sniper Rifle",
"Barret .50 Caliber Sniper Rifle", "LAW Light Anti-tank Weapon", "Frag
Grenade", "Satchel Charge", "Claymore mine, ...", "Laser Designator", "Knife".
