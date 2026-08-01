<p align="center">
  <img src="docs/assets/openbfme-readme-banner.png" alt="OpenBFME" width="100%">
</p>

<h1 align="center">OpenBFME</h1>

<p align="center">
  Experimental open-source RTS engine in Godot for <strong>Rise of the Witch-king 2.01</strong>.<br>
  Converts content locally from a game install you own. No EA assets in this repo.
</p>

<p align="center">
  <img alt="Status: experimental alpha" src="https://img.shields.io/badge/status-experimental%20alpha-c58b31">
  <img alt="Godot 4.7" src="https://img.shields.io/badge/Godot-4.7-478cbf?logo=godotengine&logoColor=white">
  <img alt="RotWK 2.01" src="https://img.shields.io/badge/target-RotWK%202.01-40513b">
  <img alt="License: Unlicense" src="https://img.shields.io/badge/license-Unlicense-blue">
</p>

> **Not a finished game.** Not a download of BFME2/RotWK. You need a legal RotWK 2.01
> install (BFME2 base as the importer layers it). Converted packs stay under local
> `.private/` and are never committed.

## Quick start (Windows)

```bat
git clone https://github.com/Open-BFME/openbfme-godot.git
cd openbfme-godot

set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
set ROTWK_INSTALL=C:\Path\To\RotWK

:: Offline tools check (no install required)
powershell -File tools\gate-rotwk-systems.ps1 -SkipLiveRetail

:: Factory path only (does not select a play pack by itself)
run_rotwk_one_button.bat "%ROTWK_INSTALL%"

:: Fresh machine: convert multi-map pack, select it, optional launch
:: ( --publish rewrites .private\content-packs\selection.json )
run_rotwk_one_button.bat "%ROTWK_INSTALL%" --multi-map --build --publish --launch

:: Or launch if a pack is already selected
run_game.bat
```

Godot is resolved from `OPENBFME_GODOT`, or `.tools\godot\`, or `godot` on PATH.
There is no machine-specific fallback path. Details: [docs/ONBOARDING.md](docs/ONBOARDING.md).

## What this is

OpenBFME is a **code-only** Godot 4.7 project plus a Python importer for SAGE
formats. Default content baseline is **RotWK 2.01** (`importer` CLI
`DEFAULT_GAME = "rotwk"`). BFME2 alone is optional comparison (`--game bfme2`).

| Path | Role |
|---|---|
| `game/` | Godot client (menu, skirmish, HUD, multiplayer foundations) |
| `importer/` | Discover â†’ extract â†’ convert â†’ pack (fail-closed) |
| `engine/` | Deterministic sim library (.NET) |
| `tools/` | Onboard, RotWK systems factory, gates, release tooling |
| `contracts/` | Product / modding policy JSON |
| `.private/` | **Local only** â€” retail inputs and converted packs (gitignored) |

Development is **systems-first** against RotWK data (maps, convert, binding,
packs, sim), not a permanent one-map freeze. Product ladder and non-goals:
[DIRECTION.md](DIRECTION.md). Active systems work:
[docs/MILESTONE_CURRENT.md](docs/MILESTONE_CURRENT.md).

## Status (honest)

- Public **source** alpha. No polished installer or official binary drop.
- Skirmish shell, multi-faction import path, map cook / binding tooling, and
  headless gates exist in-tree. Coverage is uneven.
- Men/Fords remains the deepest **legacy** gate surface; it is not the product strategy.
- Volatile evidence: [STATUS.md](STATUS.md).

## Docs

| Doc | Use it for |
|---|---|
| [docs/ONBOARDING.md](docs/ONBOARDING.md) | Setup, env vars, convert, launch |
| [docs/ROTWK_SYSTEMS_PATH.md](docs/ROTWK_SYSTEMS_PATH.md) | Operator commands for systems factory |
| [docs/CONTENT_PIPELINE.md](docs/CONTENT_PIPELINE.md) | Import / packs / containment |
| [docs/FAQ.md](docs/FAQ.md) | Legality, scope, common questions |
| [docs/README.md](docs/README.md) | Full doc map |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to contribute without leaking retail |

## Contributing

Narrow PRs with a focused check. Never commit retail or converted packs, secrets,
or absolute personal paths. Run `powershell -File tools\export-scan.ps1` before
proposing a public-facing change.

## License

Project source is **[Unlicense](LICENSE)** — public domain; do whatever you want
with this code. That does **not** cover EA / Tolkien / Middle-earth game assets
or trademarks. You still need a legal game install for conversion, and you must
not redistribute retail or converted retail content with this project.
Unofficial fan project — not affiliated with EA, Middle-earth Enterprises, or
the Tolkien Estate.
