<p align="center">
  <img src="docs/assets/openbfme-readme-banner.png" alt="OpenBFME" width="100%">
</p>

<h1 align="center">OpenBFME</h1>

<p align="center">
  Experimental open-source RTS engine in Godot for <strong>Rise of the Witch-king 2.01</strong>.
</p>

<p align="center">
  <img alt="Status: experimental alpha" src="https://img.shields.io/badge/status-experimental%20alpha-c58b31">
  <img alt="Godot 4.7" src="https://img.shields.io/badge/Godot-4.7-478cbf?logo=godotengine&logoColor=white">
  <img alt="RotWK 2.01" src="https://img.shields.io/badge/target-RotWK%202.01-40513b">
  <img alt="License: Unlicense" src="https://img.shields.io/badge/license-Unlicense-blue">
</p>

OpenBFME is a Godot 4.7 remake of *BFME2: Rise of the Witch-king* (2.01),
focused on single-player skirmish across all 7 factions. Layout, lanes, and
the agent contract: [AGENTS.md](AGENTS.md). Product strategy: [DIRECTION.md](DIRECTION.md).

Retail-derived files live under `workspace/`; use them freely. Git ignores
`workspace/` and the publication-boundary CI scans tracked files for retail
bytes and machine-absolute paths — that is the whole policy.

## How to run (Windows)

```bat
set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
set ROTWK_INSTALL=C:\Path\To\RotWK
run_rotwk_one_button.bat "%ROTWK_INSTALL%" --multi-map --build --publish --launch
```

Already converted: `run_game.bat`. Offline tools check:
`powershell -File tools\gate-rotwk-systems.ps1 -SkipLiveRetail`.
Setup detail: [docs/ONBOARDING.md](docs/ONBOARDING.md).

## Credits and inspiration

OpenBFME is an independent Godot reimplementation. SAGE format understanding
and conversion tooling lean on work pioneered by the community, especially:

- **[OpenSAGE](https://github.com/OpenSAGE/OpenSAGE)** - open SAGE engine research
  and reference implementation. We use it as research / comparison (map cook
  gaps, format notes), not as a vendored runtime. See
  [docs/OPENSAGE_GAP_MATRIX.md](docs/OPENSAGE_GAP_MATRIX.md).
- **[OpenSAGE BlenderPlugin](https://github.com/OpenSAGE/OpenSAGE.BlenderPlugin)** -
  pinned **external** W3D reader for our private Blender convert lane (LGPL;
  not copied into this repo's runtime). Toolchain ledger:
  [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).

Those projects keep their own licenses. This repo's source is Unlicense; we do
not relicense OpenSAGE code by referencing it.

## License

Project source is **[Unlicense](LICENSE)** - public domain; do whatever you want
with this code. That does **not** cover EA / Tolkien / Middle-earth game assets
or trademarks. You still need a legal game install for conversion, and you must
not redistribute retail or converted retail content with this project.
Unofficial fan project - not affiliated with EA, Middle-earth Enterprises, or
the Tolkien Estate.
