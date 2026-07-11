# Open BFME

Desktop BFME2-inspired skirmish RTS in **Godot 4.7**.

**Playability goal:** port depth from `middle-earth-rts` (the browser prototype), not empty stage checklists.

Current pack is generated from that prototype’s `config.js` (**33 units, 42 buildings, 4 factions, 10 powers, 48 hero abilities, 7 upgrades**).

## Run

```bat
C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64.exe --path C:\Users\Jonathan\Desktop\open-bfme\game
```

Headless tests:

```bat
C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe --headless --path C:\Users\Jonathan\Desktop\open-bfme\game -s res://tests/cli_runner.gd
```

## Layout

| Path | Role |
|---|---|
| `game/` | Godot project |
| `game/data/base/` | Default content pack (JSON) |
| `game/mods/` | Drop-in mods |
| `game/src/sim/` | Pure simulation (no rendering) |
| `game/src/view/` | 3D presentation + input |
| `game/tests/` | Stage 1–4 self-tests |
| `_bfme2_extract/` | Extracted BFME2 INI (local) |
| `tools/extract_big.py` | BIG4 archive extractor |

## Controls

- **WASD / arrows / edge** — pan camera · **Q/E** rotate · **wheel** zoom · **Space** jump to fortress  
- **LMB** select / box select · **RMB** move or attack  
- **A** attack-move · **S** stop · **Z** cycle stance  
- **F / G / B** hero abilities  
- Bottom buttons: place buildings / train units  

## Stages covered (v1 gold)

1. Battalions move, fight, fortress win  
2. Economy, placement, train queues, farm efficiency  
3. Walls, gates, towers, fog of war  
4. Heroes, abilities, rank gates, fear, stances, veterancy, revival  
5. Power points + spellbook tiers (Heal / Sunflare / Darkness / Earthquake / summons)  
6. Four factions (Gondor, Mordor, Elves, Goblins), damage matrix, research  
7. Skirmish AI (Easy/Normal/Hard) budgets, builds, trains, waves  
8. 4 maps, save/load (F5/F9), pause, game speed, control groups, victory stats  
9. Audio (music states + SFX), combat juice hooks, One Ring lite  
10. Headless suite + boot evidence  

## Art

Default pack resolves **icons + mesh paths** for all train/build roster entities via `data/base/assets/` (GLB/OBJ + portraits). Presentation uses `AssetFactory` (GLB load with kit fallback). Music: `assets/audio/music/{explore,battle,victory}.wav`.

## Tests

```bat
run_tests.bat
```

Expect: `STAGE TESTS: … passed, 0 failed`

## BFME2 assets

Optional extract (not required to run):

```bat
python tools\extract_big.py F:\BFME2\ini.big -o _bfme2_extract
```
