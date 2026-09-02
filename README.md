<p align="center">
  <img src="docs/assets/openbfme-readme-banner.png" alt="OpenBFME" width="100%">
</p>

<h1 align="center">OpenBFME</h1>

<p align="center">
  A Godot port of <strong>The Battle for Middle-earth II: Rise of the Witch-king</strong>, built to be the complete game and better than retail where it counts.
</p>

<p align="center">
  <img alt="Status: alpha" src="https://img.shields.io/badge/status-alpha-c58b31">
  <img alt="Godot 4.7" src="https://img.shields.io/badge/Godot-4.7-478cbf?logo=godotengine&logoColor=white">
  <img alt="Data: RotWK 2.02 v9.7.7" src="https://img.shields.io/badge/data-RotWK%202.02%20v9.7.7-40513b">
  <img alt="License: Unlicense" src="https://img.shields.io/badge/license-Unlicense-blue">
</p>

OpenBFME reads your own copy of the game (BFME2 1.06 + RotWK 2.01 + Patch
2.02 v9.7.7) and rebuilds it in Godot 4.7. Balance numbers come straight
from the retail INI files, so mods and the 2.02 balance carry over. The
engine underneath is new: higher tick rate, instanced rendering for very
large armies, working internet multiplayer with replays and reconnect, and
an AI that plays the map.

The repository contains no game assets. You need to own the game.

## What works today

Skirmish across all seven factions on the converted maps, castle sieges,
lockstep multiplayer, War of the Ring, Create-a-Hero, saves, and the retail
shell and HUD driven by the original APT screens. Campaigns and tutorials
are not converted yet. The current gameplay core is a placeholder that is
being replaced by the new engine in `engine/`; see the plan.

`python tools/fleet/progress.py` prints the whole-game scoreboard.

## Play

Download the launcher from the releases page of the repository named in
`config/release-source.json`. It locates or downloads the retail files,
converts them on your machine, and launches the game.

## Run from source

Prerequisites: Windows, Git, Godot 4.7, a RotWK install, and Python 3.12
(the importer bootstraps its own pinned environment).

```text
git clone https://github.com/Open-BFME/openbfme-godot.git
cd openbfme-godot
run_rotwk_one_button.bat C:\Path\To\RotWK --convert-factions --multi-map --build --publish
run_game.bat
```

`run_tests.bat` runs the importer suite, the engine suite, and the headless
slice runner.

## Contribute

Read `AGENTS.md`. It is one screen: pull, take a unit from the queue, run
its oracle, commit, push. `docs/REORG-PLAN.md` is the plan and the product
bar. `docs/MODDING.md` covers mods, `docs/BUILD_AND_RELEASE.md` covers
releases.

## Layout

```text
engine/      new C# simulation core (kernel + one file per SAGE module type)
importer/    Python conversion of BIG, INI, W3D, APT, maps, scripts, audio
game/        Godot project: shell, presentation, placeholder sim
launcher/    WPF launcher: download, convert, select packs, update
contracts/   schemas between shell, sim, presentation, and bundles
tools/       fleet tools, run scripts, release pipeline
```

## Credits

Format knowledge builds on [OpenSAGE](https://github.com/OpenSAGE/OpenSAGE)
and the [OpenSAGE BlenderPlugin](https://github.com/OpenSAGE/OpenSAGE.BlenderPlugin).
Engine semantics reference the [Open-BFME-1](https://github.com/Open-BFME/Open-BFME-1)
decompile. Third-party provenance is in `docs/THIRD_PARTY.md`.

This project is not affiliated with EA. You must own the game.
