# games/

Put each supported game here, one folder per game, containing the game's
own files (for example the contents of its CD). OpenDF never ships or
commits game data.

| Folder | Game | What goes inside |
|---|---|---|
| `DeltaForce/` | Delta Force (NovaLogic, 1998) | the contents of the CD (with `DFSETUP/` holding `DF.EXE` and the `.PFF` archives) |
| `DF2/` | Delta Force 2 (NovaLogic, 1999) | the contents of the CD (with `DF2SETUP/`); not playable in OpenDF yet |

Copy a CD in with `opendf import`, which picks the folder name for you:

```
opendf import /Volumes/DELTAFORCE2
opendf import D:\
opendf import ~/Downloads/DeltaForce.iso
```

The source can be the mounted disc, any folder holding a copy, an `.iso`, a raw
`.bin`/`.cue` or `.img` image, or a raw disc device. Prefer this over copying in
Finder or Explorer: the Delta Force 2 CD lists its big archives at about ten
times their real size as copy protection, so a plain copy fails or comes out
about 5 GB too big. On macOS, import an image file directly rather than
mounting it, because macOS sometimes mounts only part of an image.

Run a game by pointing OpenDF at its folder:

```
opendf --game games/DeltaForce mission C02M01
```
