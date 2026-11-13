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

Other test options: `--camera x,y,h,yaw,pitch`, `--weapon N`, `--stance N`,
`--zoom N`, `--fire`, `--walk`, `--map`, `--kill-enemies`, `--viewmodel-only`.

Debugging aids: `OPENDF_DEBUGHITS=1` prints every bullet hit (object, body
part, position; object −2 is water), every effect spawned and, for people,
their hit spheres. `OPENDF_DEBUGPLAYER=1` prints the player's hit points,
position, ground, eye, water level and land/FLOAT/SWIM state twice a second. `opendf --game … chr`
prints the stance origin heights and head sphere from PLAYER01.CHR.
