# Open BFME

Modern, moddable RTS engine using **Godot 4.7** for presentation. The first
compatibility slice targets one BFME2 map and the Men faction while keeping the
runtime independent from BFME2, OpenSAGE, BIG, and W3D. Pure C# is the leading
authoritative-simulation option, pending the Phase 0 C#/typed-GDScript bakeoff.

**Current status:** the existing GDScript game is a broad synthetic prototype and donor
for camera, UI, scenarios, and presentation work. It is not the planned authoritative
simulation foundation. See [PLAN.md](PLAN.md) for the approved direction, two-week
feasibility program, and Day-14 go/no-go gates.

The current pack is generated from that prototype's `config.js` (**33 units, 42
buildings, 4 factions, 10 powers, 48 hero abilities, 7 upgrades**). Those counts
describe prototype breadth, not vertical-slice completion or BFME2 parity.

## Run

```bat
run_game.bat
```

### Stage 1 proof

Stage 1 is implemented as an isolated Phase 0 evidence slice: a playable primitive
battle arena backed by the same legal-safe bundle in typed GDScript and pure C#.
Neither candidate reads BFME2 files or donor-engine types at runtime.

```bat
run_stage1.bat
run_stage1_tests.bat
```

The gate validates the bundle and provenance, builds and tests the C# candidate,
tests the Godot candidate and presentation at 60/120/144/240 Hz, benchmarks 50
hordes / 750 members, and requires both candidates to finish the shared scenario
with the same authoritative hash.

### Stage 2–10 proof labs and release gate

Stages 2–9 are playable, neutral, legal-safe proof slices. They extend the evidence
program without claiming BFME2 parity, completing the Men/Fords vertical slice, or
choosing the production simulation architecture. Stage 10 hardens and verifies those
proofs; it is a private gold-candidate gate, not a public-release declaration.

| Stage | Play | Cumulative gate | Authority and verified replay |
|---|---|---|---|
| 2 — economy/base | `run_stage2.bat` | `run_stage2_tests.bat` | C# + typed GDScript, exact hash `2D9E7B79`; 7 C#, 30 Godot, 38 visual assertions |
| 3 — fortifications/fog | `run_stage3.bat` | `run_stage3_tests.bat` | typed-GDScript proof, hash `0F88CA5D`; 73 proof and 41 visual assertions |
| 4 — champions/abilities | `run_stage4.bat` | `run_stage4_tests.bat` | typed-GDScript proof, hash `D85E4D9B`; 99 proof and 68 visual assertions |
| 5 — spellbook/weather | `run_stage5.bat` | `run_stage5_tests.bat` | typed-GDScript proof, hash `E3397DC8`; 52 proof and 32 visual assertions |
| 6 — factions/upgrades/art | `run_stage6.bat` | `run_stage6_tests.bat` | typed-GDScript proof, hash `E383C87F`; 67 proof, 35 visual assertions, four factions and an 80-battalion primitive probe |
| 7 — deterministic AI | `run_stage7.bat` | `run_stage7_tests.bat` | typed-GDScript proof, hash `8D53B31F`; 85 proof and 32 visual assertions, including three starvation victories/no-softlocks |
| 8 — maps/save/formations | `run_stage8.bat` | `run_stage8_tests.bat` | typed-GDScript proof, hash `2B228D1F`; 49 proof and 27 visual assertions across four original maps |
| 9 — relic/audio/juice | `run_stage9.bat` | `run_stage9_tests.bat` | typed-GDScript proof, hash `6B4565C3`; 47 proof and 38 visual assertions with classic-mode disable support |
| 10 — hardening/gold gate | `run_stage10.bat` | `run_stage10_tests.bat` | full menu plus doctor, export safety, clean boot, legacy regression and deterministic 30-minute soak (`9887356C`) |

Each later gate includes every earlier gate. `run_stage10_tests.bat` therefore checks
bundle provenance, Stage 1 regression, Stage 2 cross-language parity, the Stage 3
`<100 ms` local-topology budget, every Stage 4–9 proof and playable scene, the
101-assertion legacy suite, both boot paths, export safety, and the accelerated
30-minute AI soak. Stages 3–9 are explicitly marked
`authority=gdscript-proof`; the non-.NET Godot build cannot supply a C# scene bridge,
so those labs do not overrule the Phase 0 language bakeoff.

Stage 2 uses the RTS camera and inherited formation controls; its bottom palette builds
the supply, production, and launcher structures. Stage 3 is a top-down topology lab:
click the blue scout, right-click to move, use `R` to rotate, multi-click walls and
press Enter, or select a gate and press Space. Stage 4 is a top-down combat lab:
click blue to select and red to inspect, use `1`–`5` for data-defined abilities,
`Q/W/E/D` for stances, `F/T` for fear/terror, and the lifecycle controls for revival.
Stage 5 presents the four-tier spellbook and shared target-mode state machine. Stage 6
switches among four primitive factions and exposes live research/matrix results. Stage
7 steps or runs the data-driven AI on Easy/Normal/Hard. Stage 8 switches maps and
exercises save slots, control groups, and formations. Stage 9 exposes the Auric Loop
objective, music/SFX intents, clean combat feedback, and classic-mode toggle.

Headless tests:

```bat
run_tests.bat
```

## Layout

| Path | Role |
|---|---|
| `game/` | Godot project |
| `game/data/base/` | Default content pack (JSON) |
| `game/mods/` | Drop-in mods |
| `game/src/sim/` | Pure simulation (no rendering) |
| `game/src/view/` | 3D presentation + input |
| `game/src/stage1_sim/` | Typed-GDScript Stage 1 simulation candidate |
| `game/src/proof_stage3/`–`game/src/proof_stage9/` | Isolated deterministic proof systems |
| `game/src/stage1/`–`game/src/stage9/` | Playable proof-stage presentation and input |
| `engine/OpenBfme.Stage1/` | Pure C# Stage 1 simulation candidate |
| `content/openbfme-test/` | Legal-safe shared content bundle |
| `game/tests/` | Stage 1–10 proof/visual/boot/soak tests plus focused private retail Stage 11–15 runners |
| `docs/BUILD_AND_RELEASE.md` | Toolchain, rebuild, and private release-gate instructions |
| `docs/KNOWN_ISSUES.md` | Current proof-stage and product boundaries |
| `_bfme2_extract/` | Extracted BFME2 INI (local) |
| `tools/extract_big.py` | BIG4 archive extractor |

## Controls

- **WASD / arrows / edge** — pan camera · **Q/E** rotate · **wheel** zoom · **Space** jump to fortress  
- **LMB** select / box select · **RMB** move or attack  
- **A** attack-move · **S** stop · **Z** cycle stance  
- **F / G / B** hero abilities  
- Bottom buttons: place buildings / train units  

## Prototype and proof systems currently covered

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

The legacy suite passes 101 assertions and now exits without leaked Godot resources.
The Stage 1 gate treats leak, warning, or error output as a failure.

## Art

Default pack resolves **icons + mesh paths** for all train/build roster entities via `data/base/assets/` (GLB/OBJ + portraits). Presentation uses `AssetFactory` (GLB load with kit fallback). Music: `assets/audio/music/{explore,battle,victory}.wav`.

## Tests

```bat
run_tests.bat
```

Expect the cumulative run to finish with `STAGE10_GATE PASS`. Use `run_doctor.bat`
for the exact Godot, .NET, and Python paths selected on the current machine.

## Private BFME II retail importer

The importer builds and selects private Godot content packs from a user-owned BFME II
1.06 installation. The checkout is private-first: the complete extracted effective
view, conversion cache, and selected packs live inside this repository's ignored
`.private` tree (`.private\retail-work` and `.private\content-packs`). Public release
work is deferred until the private game works; that later export is code-only and
excludes `.private` completely.

```bat
run_importer.bat F:\BFME2
run_importer_tests.bat
run_retail_pack_tests.bat
run_retail_slice.bat --test
run_retail_pipeline_tests.bat
```

### Current private conversion gate

The complete winning retail corpus has been extracted under `.private`; conversion is
being made executable in one bounded gate first: Men versus Men on Fords of Isen II.
That profile includes exact terrain, roads, map props, neutral structures, five Men
building lifecycles, Soldier/Archer/Tower Guard/Knight presentation, particles,
ambient audio, projectile evidence, and the APT/TGA Palantir closure. Unsupported
engine semantics remain explicit blockers and private parity mode fails closed instead
of substituting generic art.

Launch the currently selected private slice with `run_retail_slice.bat`. It remains
`vertical_slice_complete: false` until the checklist in [DIRECTION.md](DIRECTION.md)
passes on a freshly built pack and rendered Godot run. After that gate, the same
deterministic converters expand across every base-game faction and multiplayer map.

The generated bundle is reproducible from unchanged inputs and runs without BFME II,
Blender, OpenSAGE, FFmpeg, DDS, W3D, BIG, or `.map` files. It is private,
non-redistributable compatibility content. See
[docs/RETAIL_IMPORTER.md](docs/RETAIL_IMPORTER.md) for the full workflow and current
capability boundary.

The older `tools\extract_big.py` remains a research helper only. It is not the safe
production import boundary.
