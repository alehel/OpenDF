# games/

Put each supported game here, one folder per game, containing the game's
own files (for example the contents of its CD). OpenDF never ships or
commits game data.

| Folder | Game | What goes inside |
|---|---|---|
| `DeltaForce/` | Delta Force (NovaLogic, 1998) | the contents of the CD (with `DFSETUP/` holding `DF.EXE` and the `.PFF` archives) |

Run a game by pointing OpenDF at its folder:

```
opendf --game games/DeltaForce mission C02M01
```
