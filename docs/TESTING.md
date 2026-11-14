# Test runs

OpenDF can play itself from a timed input script, so test runs can be
watched live or recorded.

```
opendf --game games/DeltaForce mission C02M01 --input tests/runs/insurrection-start.txt --watch          # live window, real time
opendf --game games/DeltaForce mission C02M01 --input tests/runs/insurrection-start.txt --record run.mp4 # video (needs ffmpeg)
opendf --game games/DeltaForce mission C02M01 --frames 300 --screenshot shot.ppm                        # one frame, hidden window
```

`--record` runs in fixed 60 Hz steps, so a recording is the same every time;
`--watch` runs at the real frame rate. Both can be combined. Recordings go
in `runs/` (not committed).

## Input scripts

One command per line, `<seconds> <command> [argument]`, `#` starts a comment:

| Command | Meaning |
|---|---|
| `press KEY` | press a key once (SDL key names: `W`, `C`, `Tab`, `Left Shift`, `Return`, ...) |
| `hold KEY` / `release KEY` | keep a key down / let it go; `Fire` is the trigger |
| `turn DEG/S` | keep turning (negative = left); `turn 0` stops |
| `look DEG/S` | keep looking up (positive) or down |
| `face DEG` | turn smoothly to a compass heading (0 north, 90 east) |
| `end` | finish (the run also finishes after the last command) |

Other test options: `--camera x,y,h,yaw,pitch`, `--weapon SLOT` (0-9, as the keys), `--loadout PRIMARY,KIT,SIDEARM`, `--stance N`,
`--zoom N`, `--fire`, `--walk`, `--map`, `--kill-enemies`, `--viewmodel-only`, `--no-hud` (the 3D view across the whole window, for screenshots).

Debugging aids: `OPENDF_DEBUGHITS=1` prints every bullet hit (object, body
part, position; object −2 is water), every effect spawned and, for people,
their hit spheres. `OPENDF_DEBUGPLAYER=1` prints the player's hit points,
position, ground, eye, water level and land/FLOAT/SWIM state twice a second. `opendf --game … chr`
prints the stance origin heights and head sphere from PLAYER01.CHR.

The Delta Force 2 front end takes scripted input too: `OPENDF_FE2EVENTS="TICK OP ARGS; …"`
pushes real SDL events through its event loop (`m X Y` move, `c X Y` left click, `r X Y` right
click, `k NAME` key by SDL name; 640×480 screen coordinates; with `--screenshot` four ticks pass
per frame). Example, remapping STRAFE LEFT to K on the Keyboard page:
`OPENDF_FE2EVENTS="30 c 325 130; 40 k K" opendf --game games/DF2 play keyboard --screenshot k.ppm --frames 20`
(this saves the soldier file, so keep a copy of `df2plyrs.sav`).

More aids: `OPENDF_NOFLY=1` keeps a `--camera` start higher than 2 m
under the physics (the player falls instead of flying), e.g. onto the
C03M03 radio-station roof with `--camera 392,-560,8,90,-20`;
`tests/input/c03m03-crate.txt` (with `--camera 381,-563.5,0,90,0`) jumps onto
a crate. `OPENDF_DEBUGPLAYER` also prints the surface under the feet, CHUTE,
ARMORY (DF2 type-6 volume) and LADDER, and whether Request Weapons was
accepted. `OPENDF_DEBUGSWIM=1` prints people entering and leaving the water;
`OPENDF_DEBUGOBJ=1` lists the mission's objects with their positions.
