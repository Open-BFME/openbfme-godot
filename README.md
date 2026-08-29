<p align="center">
  <img src="docs/assets/openbfme-readme-banner.png" alt="OpenBFME" width="100%">
</p>

<h1 align="center">OpenBFME</h1>

<p align="center">
  Experimental Godot port of <strong>Rise of the Witch-king Patch 2.02 v9.7.7</strong>.
</p>

<p align="center">
  <img alt="Status: developer alpha" src="https://img.shields.io/badge/status-developer%20alpha-c58b31">
  <img alt="Godot 4.7" src="https://img.shields.io/badge/Godot-4.7-478cbf?logo=godotengine&logoColor=white">
  <img alt="RotWK Patch 2.02 v9.7.7" src="https://img.shields.io/badge/target-RotWK%202.02%20v9.7.7-40513b">
  <img alt="License: Unlicense" src="https://img.shields.io/badge/license-Unlicense-blue">
</p>

OpenBFME is an independent Godot 4.7 reimplementation of *The Lord of the
Rings: The Battle for Middle-earth II - The Rise of the Witch-king*, targeting
the exact effective English game produced by Patch 2.02 v9.7.7. The target
includes the BFME2 1.06 base, the underlying RotWK 2.01 layer, and the pinned
Patch 2.02 v9.7.7 overlay. A nearby patch or plausible substitute is not parity.

This is a developer alpha, not a completed 1:1 port. Current runtime and
conversion work is substantial, but completion is accepted only through the
evidence contract in [docs/VERIFICATION.md](docs/VERIFICATION.md).

## Sources of truth

- [AGENTS.md](AGENTS.md) - contribution and evidence rules
- [DIRECTION.md](DIRECTION.md) - exact product outcome
- [PLAN.md](PLAN.md) - system map
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - executable truth and boundaries
- [docs/VERIFICATION.md](docs/VERIFICATION.md) - gates and completion semantics
- [docs/ROADMAP.md](docs/ROADMAP.md) - ordered program work
- [orchestration/work-items.json](orchestration/work-items.json) - only live task ledger
- [product scope](contracts/rotwk-202-v9.7.7-product-scope.json) and
  [retail baseline](contracts/rotwk-202-v9.7.7-baseline.json) - machine-readable target

Old queue files, receipts, counts, screenshots, and passing logs are historical
evidence. They are not current authority.

## Windows setup

You need a legal BFME2/RotWK installation plus the exact Patch 2.02 v9.7.7
overlay. Retail-derived and converted bytes remain under the ignored
`workspace/` tree and are never distributed by this repository.

```bat
set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
run_doctor.bat
py -3 tools\check-product-contracts.py --check
```

Prepare the pinned three-layer source and follow the current cook/select work
in [docs/ONBOARDING.md](docs/ONBOARDING.md). If an exact verified pack selection
already exists locally:

```bat
set OPENBFME_CONTENT=%CD%\workspace\content-packs
run_game.bat
```

A successful launch proves reachability only. It does not prove 2.02 behavior,
visual, audio, mode, or whole-product parity.

## Credits and inspiration

OpenBFME uses community format research and external tools, especially
[OpenSAGE](https://github.com/OpenSAGE/OpenSAGE) and the
[OpenSAGE BlenderPlugin](https://github.com/OpenSAGE/OpenSAGE.BlenderPlugin).
They are research/tool inputs, not a vendored runtime. See
[docs/OPENSAGE_GAP_MATRIX.md](docs/OPENSAGE_GAP_MATRIX.md) and
[docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).

## License

Repository source is released under the [Unlicense](LICENSE). That license does
not cover EA, Tolkien, Middle-earth, retail-game, or third-party assets and
trademarks. Users must supply a lawful installation; retail and converted retail
content must not be redistributed. OpenBFME is an unofficial fan project and is
not affiliated with EA, Middle-earth Enterprises, or the Tolkien Estate.
