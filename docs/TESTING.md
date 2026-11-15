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
| `wheel N` | turn the mouse wheel N notches (positive = forward; DF1: forward = previous weapon) |
| `end` | finish (the run also finishes after the last command) |

Other test options: `--camera x,y,h,yaw,pitch`, `--weapon SLOT` (0-9, as the keys), `--loadout PRIMARY,KIT,SIDEARM`, `--stance N`,
`--zoom N`, `--fire`, `--walk`, `--map`, `--kill-enemies`, `--viewmodel-only`, `--no-hud` (the 3D view across the whole window, for screenshots).
`--attributes N` sets the Difficulty bits for a direct mission run (DF1 df.cfg bits, e.g. 0x200 squad members off; DF2 df2.cfg bits, e.g. 0x20 squad members off, 0x10 enemy AI hard).

Debugging aids: `OPENDF_FPSLOG=1` prints the mission's frame rate to stderr twice a second (as the window title shows it). `OPENDF_DEBUGWEAPON=1` prints the player weapon state (slot, mode, magazine, spares, chamber, cooldown, reload, burst counter, queued sounds) whenever it changes. `OPENDF_DEBUGAI=1` also lists every soldier (side, weapon, position) at the start. `OPENDF_DEBUGHITS=1` prints every bullet hit (object, body
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

The Delta Force front end has the same aid as `OPENDF_FEEVENTS` (same syntax, plus `t TEXT` for typed
text); with it and `--screenshot` four menu ticks pass per frame, and the first frame's ticks come before
the first draw, so start events at tick 8 or later. Example, Settings from the Main Menu:
`OPENDF_FEEVENTS="8 c 320 203" opendf --game games/DeltaForce play main --screenshot w.ppm --frames 5`
(the front end saves the profiles on exit, so keep a copy of `player01.ply`).

The front ends' sound can be captured: `OPENDF_AUDIODUMP=FILE` opens no audio device and
mixes 0.016 s per menu tick into FILE as raw s16le stereo at 22050 Hz (with `--screenshot`,
four ticks per frame), e.g. `OPENDF_AUDIODUMP=brief.raw opendf --game games/DF2 play briefing
--screenshot b.ppm --frames 500`, then `ffmpeg -f s16le -ar 22050 -ac 2 -i brief.raw -af volumedetect -f null -`.

The Delta Force intro (INTRO.KDV) plays before the start screen. With `--screenshot` its clock
runs in fixed steps (frame N is always the same picture) and its frames count towards `--frames`:
`opendf --game games/DeltaForce play intro --screenshot i.ppm --frames 300` shows intro frame 300
(add `OPENDF_AUDIODUMP=intro.raw` for its sound). `OPENDF_INTROKEY=N` presses a key after N
frames (skips), `OPENDF_INTROCLICK=N` left-clicks (DF_LEAD then fades at once). `opendf kdv FILE
[OUTDIR [EVERY]]` decodes any KDV file (also the VIEWS.PFF award videos, after `extract OUT KDV`)
and writes every EVERY-th frame as a .ppm. Delta Force 2: `OPENDF_DEBUGVIDEO=1` prints the
binkplay command line DF2.EXE would run at start-up and what the Bink player did. The DF2 intro
(DF2INTRO.BIK) works the same way as DF1's: `opendf --game games/DF2 play intro --screenshot
i.ppm --frames 300` shows intro frame 300 (its frames count towards `--frames`, so `--frames 1400`
reaches Choose Soldier after the 1361-frame movie), `OPENDF_AUDIODUMP=FILE` records its sound,
`OPENDF_INTROKEY=N` / `OPENDF_INTROCLICK=N` skip it after N frames. `opendf bink FILE [OUTDIR
[EVERY|N,N,...]]` decodes any Bink 1 file, prints the header, keyframes, decode speed and the
soundtrack's RMS / peak per second, and writes every EVERY-th frame (or the listed ones) as a
.ppm and the soundtrack as OUTDIR/audio.wav; `OPENDF_BINKYUV=FILE` also writes every frame as raw
YUV 4:2:0, for comparing with another decoder, e.g. against `ffmpeg -i F.BIK -f framemd5 -pix_fmt
yuv420p`.

More aids: `OPENDF_NOFLY=1` keeps a `--camera` start higher than 2 m
under the physics (the player falls instead of flying), e.g. onto the
C03M03 radio-station roof with `--camera 392,-560,8,90,-20`;
`tests/input/c03m03-crate.txt` (with `--camera 381,-563.5,0,90,0`) jumps onto
a crate. `OPENDF_DEBUGPLAYER` also prints the surface under the feet, CHUTE,
ARMORY (DF2 type-6 volume) and LADDER, and whether Request Weapons was
accepted. `OPENDF_DEBUGSWIM=1` prints people entering and leaving the water;
`OPENDF_DEBUGOBJ=1` lists the mission's objects with their positions.
`OPENDF_TEAMSPEED="TEAM:KMH,..."` sets team speeds at the first tick (as the
script's set-speed action), e.g. `OPENDF_TEAMSPEED=10:40` starts D06M05's
train at once; with `OPENDF_NOFLY=1 --camera -491.5,1356,4,0,-15` the
player drops onto a flatcar and rides it. `OPENDF_DEBUGRIDE=1` lists every
120 ticks the people standing on an object (DF1 +0x100 / DF2 +0xF8), their
place on it and its velocity and turn; `=N` follows world object N every
10 ticks (move, height, LADDER). `OPENDF_AIPROP="TEAM:SUB:VALUE,..."` applies the script's AI-attribute action (action 3) at the
first tick, e.g. `OPENDF_AIPROP=38:15:0` releases C04M03's Black Hawk guns. `OPENDF_TESTLADDER=1` (DF2) sends the first
soldier allowed on ladders (BMS +0xC & 0x1000) running into the nearest
ladder (type-4 volume) at tick 30; `=ITEM` only that item's ladders,
`OPENDF_TESTLADDERWALK=1` walks instead (e.g. D06M11: the powcamp tower).
