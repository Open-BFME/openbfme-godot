<p align="center">
  <img src="docs/assets/openbfme-readme-banner.png" alt="An original fantasy battlefield reconstructed as a modern game-engine wireframe" width="100%">
</p>

<h1 align="center">OpenBFME Engine</h1>

<p align="center">
  An experimental open-source Godot RTS project targeting RotWK 2.01 play,<br>
  powered by content converted locally from a game installation you own.
</p>

<p align="center">
  <img alt="Status: experimental alpha" src="https://img.shields.io/badge/status-experimental%20alpha-c58b31">
  <img alt="Godot 4.7" src="https://img.shields.io/badge/Godot-4.7-478cbf?logo=godotengine&logoColor=white">
  <img alt="RotWK 2.01" src="https://img.shields.io/badge/compatibility-RotWK%202.01-40513b">
  <img alt="License: GPL v3" src="https://img.shields.io/badge/license-GPLv3-663399">
</p>

> [!IMPORTANT]
> OpenBFME is an experimental engine project, not a finished game or a download
> of BFME2/RotWK. It does not distribute EA's game assets. The compatibility
> workflow requires a lawfully acquired **Rise of the Witch-king 2.01**
> installation (with BFME2 base as the overlay resolves it) and converts content
> locally on your computer.

> [!NOTE]
> This tree is **source + docs**, not a finished game or a prebuilt installer.
> It is an experimental alpha: clone, bootstrap tools, convert content from a
> game you own, then run headless gates or the skirmish shell. Public binary
> packages and a hardened first-time installer remain later work. See
> [STATUS.md](STATUS.md) before treating any path as “done.”

## What is OpenBFME?

OpenBFME is rebuilding *The Battle for Middle-earth II: The Rise of the
Witch-king* (and the BFME2 content it supersets) in Godot. Development is
**systems-first and iterative** against RotWK data — not a permanent one-map
vertical-slice freeze. The project has three goals:

1. Reproduce RotWK behavior and presentation through measured comparison with
   the original game (skirmish systems first; campaigns and War of the Ring on
   the product ladder).
2. Replace the aging proprietary runtime with an understandable, deterministic,
   self-hostable modern engine.
3. Give RTS developers and BFME/RotWK modders a practical base for new factions,
   maps, scenarios, presentation packs, and total conversions.

The importer understands retail SAGE formats with **RotWK as the default content
baseline**; the game runtime loads a versioned pack generated privately on the
user's machine. Proprietary retail content stays outside Git and outside public
releases. See `DIRECTION.md` for the system ladder and development model.

## Where the development tree is today

The development tree is much broader than the original one-map prototype. The
list below describes what exists in code and is exercised by headless gates; it
is not a completion or parity claim:

- a skirmish shell modeled on BFME2, with a main menu, N-player setup screen
  (multiple factions, alliances, per-AI difficulty), persistent graphics/audio
  options, seven faction choices, five map choices, colors, starting positions,
  starting resources, and command-point rules;
- local conversion and runtime manifests for all six BFME2 factions — Men,
  Elves, Dwarves, Isengard, Mordor, and Goblins/Wild — plus Angmar, imported
  from a user-owned Rise of the Witch-king 2.01 installation through the same
  data-driven, fail-closed pipeline;
- construction, production and cancellation, rally points, combat, armor and
  weapon upgrades, stances, formations, cavalry trample and knockback, hero
  experience, hero abilities (leadership auras, mounts, weapon toggles, and
  other retail-extracted effects), death and revival, control groups,
  per-team spellbooks, and victory/defeat with alliances and elimination;
- deterministic lockstep multiplayer foundations over ENet, with an in-game
  lobby (player names, chat, settings, per-peer faction choice) and
  cross-faction matchups presented through each team's own faction;
- five deterministic AI difficulty tiers with per-team AI controllers, plus
  opt-in neutral creep lairs with BFME2 guard behavior on maps that define
  them;
- a retail HUD lane that executes converted retail APT/ActionScript bytecode
  in a deterministic VM (all measured opcode tiers), alongside WND runtime
  semantics;
- source-derived terrain, roads, water, navigation, minimaps, start positions,
  and fortress placement for a five-map development set, plus an optional
  BFME1-style build-plot-only mode (default off);
- deterministic state signatures pinned per faction and enforced by headless
  gate runners (the Men battle signature is an asserted constant), with
  hundreds of focused runtime assertions per suite.

That breadth is real, but it is not a finished game. Men versus Men on Fords of
Isen II remains the most deeply verified slice; the other factions and maps have
substantial but uneven coverage, and presentation, reliability, and visual-oracle
work remain open. See [STATUS.md](STATUS.md) for the current audited evidence
and known failures.

| Capability | Current state |
|---|---|
| RotWK/BFME2 discovery, extraction, conversion, and provenance | Implemented across the active private packs (RotWK default) |
| Men versus Men on Fords of Isen II | Most deeply verified developer-playable slice |
| Seven faction runtime surfaces (six BFME2 + Angmar) | Converted, selectable, and exercised under per-faction gates |
| Five-map development set | All boot from source data; Fords has the strongest prop coverage |
| Main menu, N-player skirmish setup, options, HUD, and audio | Implemented under focused tests |
| Multiplayer | Deterministic lockstep + ENet with an in-game lobby; early and gate-verified, not yet a hardened production service |
| Skirmish AI | Five deterministic difficulty tiers, per-team controllers |
| Campaigns and War of the Ring | In scope, ladder steps 6 and 7; not started |
| Rise of the Witch-king | **Parity baseline** (2.01); Angmar and RotWK content in scope; campaigns/WOTR are later ladder steps |
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
Lawfully owned RotWK 2.01 install
  (BFME2 base as the layered catalog resolves it)
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

The current workflow is Windows-first and intended for developers. You need:

- a lawfully acquired **Rise of the Witch-king 2.01** install (BFME2 base as
  the overlay resolves it; BFME2-only is optional comparison only);
- Godot 4.7 (console build preferred for headless gates);
- Python 3.12 (the wizard bootstraps the pinned importer env);
- the .NET SDK selected by `global.json` for simulation projects.

**Recommended RotWK path** (systems factory + convert + optional launch):

```bat
set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
set ROTWK_INSTALL=C:\Path\To\RotWK
run_rotwk_one_button.bat "%ROTWK_INSTALL%"
```

**Guided wizard** (still valid; currently seeds a Men pack for classic
headless gates, and can validate RotWK with `--rotwk`):

```bat
python tools\onboard.py --install "C:\Path\To\BFME2" --rotwk "C:\Path\To\RotWK" --godot "%OPENBFME_GODOT%" --yes
```

Read [docs/ONBOARDING.md](docs/ONBOARDING.md) and
[docs/ROTWK_SYSTEMS_PATH.md](docs/ROTWK_SYSTEMS_PATH.md) before a long convert.

## Roadmap

Product scope is **RotWK 2.01**, systems-first (not a single-map freeze). The
ladder and non-goals live in [DIRECTION.md](DIRECTION.md). The active systems
objective is [docs/MILESTONE_CURRENT.md](docs/MILESTONE_CURRENT.md).

1. RotWK content pipeline, multi-map cook, and fail-closed convert.  
2. Asset / binding / multi-faction pack completeness.  
3. Simulation and AI driven from pack descriptors.  
4. Skirmish shell across official maps and factions.  
5. Campaigns and War of the Ring as later ladder steps.  
6. Multiplayer, Create-a-Hero polish, installer and accessibility last.

## Find your way around

| If you want to... | Start here |
|---|---|
| Understand the project in five minutes | [Documentation hub](docs/README.md) |
| Set up a fresh machine | [Onboarding](docs/ONBOARDING.md) |
| Check current passes and failures | [Status](STATUS.md) |
| Understand the engine boundaries | [Architecture](docs/ARCHITECTURE.md) |
| Learn how retail conversion stays private | [Content pipeline](docs/CONTENT_PIPELINE.md) |
| Run RotWK systems tooling | [RotWK systems path](docs/ROTWK_SYSTEMS_PATH.md) |
| Understand the parity standard | [BFME2 parity](docs/BFME2_PARITY.md) (RotWK-primary) |
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
