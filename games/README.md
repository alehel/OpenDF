# games/

Put each supported game here, one folder per game, containing the game's
own files (the contents of its CD, or a GOG install). OpenDF never ships or
commits game data.

| Folder | Game | What goes inside |
|---|---|---|
| `DeltaForce/` | Delta Force (NovaLogic, 1998) | the contents of the CD (with `DFSETUP/` holding `DF.EXE` and the `.PFF` archives) |
| `DF2/` | Delta Force 2 (NovaLogic, 1999) | the contents of the CD (with `DF2SETUP/` holding `DF2.EXE` and the `.PFF` archives) |
| `GOG/DeltaForce/app`, `GOG/DF2/app` (any name) | the GOG releases | the installed GOG folder (`Df.exe` / `Df2.exe` next to the `.pff` archives), or the `app` folder innoextract makes from GOG's offline installer |

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

A GOG install needs no import: point OpenDF at its folder (or add it in the
launcher). The importer does not read GOG's installer `.exe`.

Run a game by pointing OpenDF at its folder:

```
opendf --game games/DeltaForce mission C02M01
```
