# Retail mod compatibility (lane MODCOMPAT-1)

Goal (owner, 2026-10-28): every retail RotWK mod works in OpenBFME as it does in retail RotWK 2.01, including its own menus and movies.
Mods are data for the retail engine, so the way there is the retail engine itself: every gap a mod finds is a retail code path that is not
ported yet, ported from the decomp with its RW evidence like any other retail behaviour. A mod that ships its own game program (Edain
Unchained's `game.dat`, Age of the Ring) is the exception: its program's changes are decoded and rebuilt as named features
(docs/MODS.md, "Mods that change the game program").

## How progress is measured

1. **The gap census** (`tools/mod/gap_census.py`, `logs/mod-gap-census.md`): one headless skirmish per All In One package; the unported
   modules, script actions and conditions each game ran into, ranked by how many packages hit them, and (the static side, which sizes a
   module port) every object template of the mod whose behavior modules name an unported class. The porting lanes take it top-down.
2. **The per-mod checklist** below, against retail RotWK 2.01 running the same mod on a Windows machine (the reference captures). A mod is
   compatible when every row passes.

## The per-mod checklist

For each mod (its All In One package, or its `-mod` folder), with the same files in retail and in OpenBFME:

| # | Check | Retail reference | OpenBFME | Pass when |
|---|---|---|---|---|
| 1 | It mounts | the mod's files in the game folder / `-mod` | `--mod-install` / `--mod` (the launcher's Play) | no mount error; the identity names the mod |
| 2 | Its data loads | retail's own load | the start log's INI messages | every message is one retail also hits (the mod's own data bugs), none ours |
| 3 | The intro movies | a capture of the start-up from launch to the main menu | the same start-up | the same movies in the same order (the mod's own where it has them), with sound |
| 4 | The main menu | a screenshot of the main menu after its intro | the same | the same layout, text, images and background movie |
| 5 | Every shell screen | a screenshot of each screen reached from the main menu: skirmish setup, options, multiplayer / LAN, the campaign menus, the mod's own screens | the same | each screen matches: buttons, lists, text, images; every button leads where retail's does |
| 6 | A skirmish | a skirmish on a map the mod ships or a retail map it supports, each of its factions once | the same setup | it starts; the factions, heroes, buildings and spell book are the mod's; 3000 frames against a hard computer player run without an error or a stop the mod causes |
| 7 | Two players | a LAN game of two copies with the mod | two peers with the mod | the lobby accepts both; 3000 frames in lockstep (0 desyncs) |
| 8 | The in-game interface | screenshots of the control bar, the build menus, the hero bar (Edain Unchained's), the palantir | the same | the same buttons, icons, tooltips and counters |
| 9 | A save and a load | a skirmish saved and loaded | the same | the loaded game continues; OpenBFME's loaded game hashes as the uninterrupted one |
| 10 | Its maps | each map the mod adds, loaded in a skirmish | the same | every map starts and its scripts run |

A row that fails names the gap: the retail function (RW address) it needs, or the mod program change it needs. It goes on the census list.

## The order

1. Movies and menus first (what a player sees first): the mod's intro and menu movies (fixed: a mod's `Data\Movies`, and an install-folder
   mod's movies, which retail has in its game folder), then each shell screen against the retail screenshots.
2. Then the census list top-down, by how many mods each gap reaches: the AI's script actions and conditions, the unported modules.
3. Age of the Ring: its game program decoded (a function-by-function comparison with retail 2.01's) and each change rebuilt as a feature.

Mods covered first: Edain 4.8.5.3, Edain Unchained 4.4.3, then the rest of the All In One RotWK packages, then Age of the Ring.
