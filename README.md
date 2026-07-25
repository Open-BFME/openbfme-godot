<p align="center">
  <img src="docs/assets/openbfme-readme-banner.png" alt="An original fantasy battlefield reconstructed as a modern game-engine wireframe" width="100%">
</p>

<h1 align="center">OpenBFME Engine</h1>

<p align="center">
  An experimental open-source Godot RTS project targeting BFME2 / Rise of the<br>
  Witch-king play, powered by content converted locally from a game you own.
</p>

<p align="center">
  <img alt="Status: experimental alpha" src="https://img.shields.io/badge/status-experimental%20alpha-c58b31">
  <img alt="Godot 4.7" src="https://img.shields.io/badge/Godot-4.7-478cbf?logo=godotengine&logoColor=white">
  <img alt="RotWK 2.01" src="https://img.shields.io/badge/compatibility-RotWK%202.01-40513b">
  <img alt="License: GPL v3" src="https://img.shields.io/badge/license-GPLv3-663399">
</p>

> [!IMPORTANT]
> OpenBFME is an experimental engine project, not a finished game or a download
> of BFME2. It does not distribute EA's game assets. The compatibility workflow
> requires a lawfully acquired BFME2 1.06 or Rise of the Witch-king 2.01
> installation and converts content locally on your computer.

> [!NOTE]
> The public GitHub repository is currently a documentation preview. Engine,
> importer, and runtime source will follow only after the active rewrite and a
> clean code-export review. The commands below describe the developer workflow
> but cannot be used from the documentation-only snapshot by itself.

## What is OpenBFME?

OpenBFME is rebuilding *The Battle for Middle-earth II* and *The Rise of the
Witch-king* in Godot. The project has three goals:

1. Reproduce the original game's behavior and presentation through measured
   comparison with it.
2. Replace the aging proprietary runtime with an understandable, deterministic,
   self-hostable modern engine.
3. Give RTS developers and BFME modders a practical base for new factions, maps,
   scenarios, presentation packs, and total conversions.

The importer understands BFME2's source formats; the game runtime loads a
versioned pack generated privately on the user's machine. Proprietary retail
content stays outside Git and outside public releases.

## Where the development tree is today

The development tree is much broader than the original one-map prototype. The
list below describes what exists in code and is exercised by headless gates; it
is not a completion or parity claim:

- a skirmish shell modeled on BFME2, with a main menu, N-player setup screen
  (multiple factions, alliances, per-AI difficulty), persistent graphics/audio
  options, seven faction choices, map selection, colors, starting positions,
  starting resources, and command-point rules;
- local conversion and runtime manifests for seven playable factions - Men,
  Elves, Dwarves, Isengard, Mordor, Goblins/Wild, and Angmar - all through
  the same data-driven, fail-closed pipeline;
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
  and fortress placement for the cooked skirmish maps, plus an optional
  BFME1-style build-plot-only mode (default off);
- deterministic state signatures pinned per faction and enforced by headless
  gate runners (the Men battle signature is an asserted constant), with
  hundreds of focused runtime assertions per suite.

That breadth is real, but it is not a finished game. Cross-faction skirmish
runs, but coverage across the seven factions is uneven - Mordor is materially
behind the others - and presentation, reliability, and visual-oracle work remain
open. See [STATUS.md](STATUS.md) for the current audited evidence and the known
failures.

| Capability | Current state |
|---|---|
| Discovery, extraction, conversion, and provenance | Implemented across the active private packs |
| Cross-faction skirmish | Playable: any of the seven factions against any other |
| Seven faction runtime surfaces | Converted, selectable, and exercised under per-faction gates |
| Cooked skirmish maps | Eight boot from source data; Fords of Isen II has the strongest prop coverage |
| Main menu, N-player skirmish setup, options, HUD, and audio | Implemented under focused tests |
| Multiplayer | Deterministic lockstep + ENet with an in-game lobby; early and gate-verified, not yet a hardened production service |
| Skirmish AI | Five deterministic difficulty tiers, per-team controllers |
| Campaigns | In scope and in progress; sequenced in [docs/CAMPAIGN_PLAN.md](docs/CAMPAIGN_PLAN.md) |
| War of the Ring | Out of scope |
| Rise of the Witch-king | RotWK 2.01 is the product baseline; Angmar is a playable faction |
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
Lawfully owned BFME2 1.06 / RotWK 2.01 install
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
lawfully acquired BFME2 1.06 or RotWK 2.01 installation, Godot 4.7, Python
3.12, and the .NET SDK selected by `global.json`.

The guided onboarding wizard checks prerequisites, validates your install
fail-closed, converts or verifies the Men content pack, and runs the headless
verification gates:

```bat
python tools\onboard.py
```

Non-interactive equivalent (CI or scripted setup):

```bat
python tools\onboard.py --install "D:\Games\BFME2" --godot "C:\Tools\Godot\Godot_v4.7-stable_win64_console.exe" --yes
```

The manual command path still works:

```bat
set OPENBFME_GODOT=C:\Tools\Godot\Godot_v4.7-stable_win64.exe
run_doctor.bat
run_importer.bat "D:\Games\BFME2"
run_retail_slice.bat
```

Use your actual Godot and game paths. Read the
[onboarding guide](docs/ONBOARDING.md) for the walkthrough, the manual command
path, and troubleshooting.

## Roadmap

1. Bring all seven factions and the official skirmish systems to green runtime
   and original-game evidence, on the official skirmish and multiplayer maps.
2. Deliver the Good and Evil campaigns: maps, scripting, objectives, cinematics.
3. Harden the deterministic, self-hosted multiplayer foundations (lockstep +
   lobby exist today) toward reliable play for up to eight players.
4. Complete the skirmish shell, saves, replays, observers, Create-a-Hero, and
   broader map and modding tools.
5. Add accessibility, HD presentation packs, safe updates, rollback, and a
   polished installer without weakening compatibility checks.

War of the Ring is not part of this roadmap. The stable scope and non-goals live
in [DIRECTION.md](DIRECTION.md).

## Find your way around

| If you want to... | Start here |
|---|---|
| Understand the project in five minutes | [Documentation hub](docs/README.md) |
| Set up a fresh machine and run the developer build | [Onboarding](docs/ONBOARDING.md) |
| Check current passes and failures | [Status](STATUS.md) |
| Understand the engine boundaries | [Architecture](docs/ARCHITECTURE.md) |
| Learn how retail conversion stays private | [Content pipeline](docs/CONTENT_PIPELINE.md) |
| Understand the parity standard | [Parity](docs/BFME2_PARITY.md) |
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

Users must supply their own lawfully acquired copy of the game. Retail and converted
retail assets must never be committed, uploaded, bundled, or redistributed with
this project.
