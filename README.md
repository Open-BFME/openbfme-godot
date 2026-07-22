<p align="center">
  <img src="docs/assets/openbfme-readme-banner.png" alt="An original fantasy battlefield reconstructed as a modern game-engine wireframe" width="100%">
</p>

<h1 align="center">OpenBFME Engine</h1>

<p align="center">
  An experimental open-source Godot RTS project targeting BFME2 skirmish play,<br>
  powered by content converted locally from a game installation you own.
</p>

<p align="center">
  <img alt="Status: experimental alpha" src="https://img.shields.io/badge/status-experimental%20alpha-c58b31">
  <img alt="Godot 4.7" src="https://img.shields.io/badge/Godot-4.7-478cbf?logo=godotengine&logoColor=white">
  <img alt="BFME2 1.06" src="https://img.shields.io/badge/compatibility-BFME2%201.06-40513b">
  <img alt="License: GPL v3" src="https://img.shields.io/badge/license-GPLv3-663399">
</p>

> [!IMPORTANT]
> OpenBFME is an experimental engine project, not a finished game or a download
> of BFME2. It does not distribute EA's game assets. The compatibility workflow
> requires a lawfully acquired BFME2 1.06 installation and converts content
> locally on your computer.

> [!NOTE]
> The public GitHub repository is currently a documentation preview. Engine,
> importer, and runtime source will follow only after the active rewrite and a
> clean code-export review. The commands below describe the developer workflow
> but cannot be used from the documentation-only snapshot by itself.

## What is OpenBFME?

OpenBFME is rebuilding the skirmish side of *The Battle for Middle-earth II* in
Godot. The project has three goals:

1. Reproduce BFME2 skirmish behavior and presentation through measured
   comparison with the original game.
2. Replace the aging proprietary runtime with an understandable, deterministic,
   self-hostable modern engine.
3. Give RTS developers and BFME modders a practical base for new factions, maps,
   scenarios, presentation packs, and total conversions.

The importer understands BFME2's source formats; the game runtime loads a
versioned pack generated privately on the user's machine. Proprietary retail
content stays outside Git and outside public releases.

## What the recent code audit found

The development tree is much broader than the original one-map prototype. Kimi's
UI rewrite is currently changing this surface, so the list below describes the
most recent audit rather than a frozen release identity:

- a skirmish shell modeled on BFME2, with a main menu, setup screen, persistent graphics/audio
  options, six faction choices, five map choices, colors, starting positions,
  starting resources, and command-point rules;
- local conversion and runtime manifests for Men, Elves, Dwarves, Isengard,
  Mordor, and Goblins/Wild; all six are exercised, but none currently has a
  fully green faction suite;
- a large Men roster with heroes, infantry, archers, cavalry, siege, builders,
  production buildings, fortress parts, walls, and upgrades;
- construction, production and cancellation, rally points, combat, armor and
  weapon upgrades, stances, formations, cavalry trample, hero experience,
  abilities, death and revival, control groups, and victory/defeat;
- a broadly exercised Men spellbook runtime plus compiled spellbook documents
  for the other factions; non-Men casting coverage is incomplete and currently
  has failing assertions;
- AI base construction, production, attacks, and defeat handling;
- source-derived terrain, roads, water, navigation, minimaps, start positions,
  and fortress placement for a five-map development set; and
- deterministic state signatures and hundreds of focused runtime assertions.

That is real progress, but it is not a current completion claim. Men versus Men on Fords
of Isen II remains the best-covered developer-playable alpha slice. The other factions execute
substantial gameplay suites but still have failing assertions and stale pinned
signatures. Four of the five maps boot with source terrain and navigation but do
not yet have Fords' prop coverage. Runtime teardown leaks and visual-oracle work
also remain open. See [STATUS.md](STATUS.md) for the current test evidence.

| Capability | Most recent audited state |
|---|---|
| BFME2 1.06 discovery, extraction, conversion, and provenance | Implemented across the active private packs |
| Men versus Men on Fords of Isen II | Best-covered developer-playable alpha slice; suite still fails |
| Six faction runtime surfaces | Converted, selectable, and exercised; not yet fully green |
| Five-map development set | All boot; only Fords has strong prop coverage |
| Main menu, skirmish setup, options, HUD, and audio | Implemented under focused tests |
| Deterministic multiplayer and dedicated servers | Planned architecture; not implemented |
| Campaigns and War of the Ring | Explicitly outside project scope |
| Rise of the Witch-king | Outside current BFME2 scope |
| Public binary or polished installer | Not available |

## Why this project exists

This began as a joke and an AI benchmark: could frontier models turn the raw
pieces of BFME into something usable in Godot? After a few hours of human
direction and a few days of AI-assisted coding, the project owner recalls having
a surprisingly playable prototype. That personal timeline is part of the
project's origin story, not a reproducible benchmark result.

The larger motivation is the community. BFME modders have spent years doing
remarkable work within the limits of an old proprietary engine. OpenBFME aims to
give those developers a modern starting point without redistributing the
original game.

## How it works

```text
Lawfully owned BFME2 1.06 installation
              |
              v
  Python importer + pinned converters
              |
              v
  Private, local, versioned runtime pack
              |
              +--> deterministic authoritative simulation
              |
              +--> Godot rendering, input, UI, and audio
```

- `importer/` discovers, extracts, converts, validates, and records provenance.
- `game/` contains the Godot client and current playable runtime.
- `engine/` contains the path toward a pure deterministic simulation layer.
- `contracts/` contains machine-readable product and modding policies.
- `.private/` contains local retail inputs and converted output and is never a
  public-release input.

Strict private compatibility paths are designed to fail when required retail
evidence is missing. Focused tests enforce that rule on covered paths; complete
repository-wide fallback auditing remains release work.

## Try the development build

The current workflow is Windows-first and intended for developers. You need a
lawfully acquired BFME2 1.06 installation, Godot 4.7, Python 3.12, and the .NET
SDK selected by `global.json`.

```bat
set OPENBFME_GODOT=C:\Tools\Godot\Godot_v4.7-stable_win64.exe
run_doctor.bat
run_importer.bat "D:\Games\BFME2"
run_retail_slice.bat
```

Use your actual Godot and BFME2 paths. Read the full
[getting-started guide](docs/GETTING_STARTED.md) before importing.

## Roadmap

1. Finish and freeze Men versus Men on Fords of Isen II.
2. Harden the full Men roster across the selected five-map set.
3. Bring all six BFME2 factions and official skirmish systems to green runtime
   and original-game evidence.
4. Add deterministic, self-hosted multiplayer for up to eight players.
5. Complete the skirmish shell, replays, observers, Create-a-Hero, and broader
   map and modding tools.
6. Add accessibility, HD presentation packs, safe updates, rollback, and a
   polished installer without weakening compatibility checks.

Campaign material and War of the Ring are not part of this roadmap. The stable
scope and non-goals live in [DIRECTION.md](DIRECTION.md).

## Find your way around

| If you want to... | Start here |
|---|---|
| Understand the project in five minutes | [Documentation hub](docs/README.md) |
| Install and run the developer build | [Getting started](docs/GETTING_STARTED.md) |
| Check current passes and failures | [Status](STATUS.md) |
| Understand the engine boundaries | [Architecture](docs/ARCHITECTURE.md) |
| Learn how retail conversion stays private | [Content pipeline](docs/CONTENT_PIPELINE.md) |
| Understand the parity standard | [BFME2 parity](docs/BFME2_PARITY.md) |
| Read the modding direction | [Modding](docs/MODDING.md) |
| Contribute safely | [Contributing](CONTRIBUTING.md) |
| Understand the use of AI | [AI development](docs/AI_DEVELOPMENT.md) |
| Ask a common question | [FAQ](docs/FAQ.md) |

## Built with AI, judged by evidence

OpenBFME has been built with extensive AI assistance under human direction and
testing. The project owner reports that Fable 5, ChatGPT Sol, and Kimi K3
contributed substantial implementation and review work. The current Git history
does not preserve model-level attribution for individual changes, so those
credits are owner testimony rather than repository-verifiable authorship.

That origin is part of the experiment, not proof that the result is correct.
Claims are accepted only when backed by source evidence, focused tests, runtime
behavior, original-game comparison, and human review. See
[docs/AI_DEVELOPMENT.md](docs/AI_DEVELOPMENT.md).

## Contributing

The repository is being prepared for wider collaboration. Good contributions are
narrow, reproducible, and tied either to observed BFME2 behavior or a documented
modern-engine contract.

Do not submit retail assets, converted content, game packs, original-game
captures, secrets, personal configuration, or agent instruction files. Start
with [CONTRIBUTING.md](CONTRIBUTING.md).

## License and legal notice

The proposed public source is distributed under the GNU General Public License
v3.0; the repository now carries its own [LICENSE](LICENSE). That license applies
to code the project is authorized to license, not to *The Lord of the Rings*,
BFME2, or third-party content. Third-party provenance and notice review remains a
publication gate.

OpenBFME is an unofficial fan project. It is not affiliated with, endorsed by,
or sponsored by Electronic Arts, Middle-earth Enterprises, the Tolkien Estate,
Embracer Group, or their licensors. Related names, trademarks, characters, and
original game assets belong to their respective owners.

Users must supply their own lawfully acquired copy of BFME2. Retail and converted
retail assets must never be committed, uploaded, bundled, or redistributed with
this project.
