# Onboarding

Windows-first path: clone source â†’ set env â†’ bootstrap tools â†’ convert from a
game you own â†’ run gates or the skirmish shell.

Experimental alpha. First convert can take a long time. Wrong install paths fail
closed on purpose. See [STATUS.md](../STATUS.md) for known limits.

## Requirements

- Windows 10/11, Git, PowerShell
- **RotWK 2.01** with BFME2 base (layered install). BFME2-only is optional
  comparison (`--game bfme2`), not the product target
- Godot **4.7** (console build preferred for headless output)
- Python 3.12 on PATH for first bootstrap
- Disk for `.private/` (multi-faction sets are tens of GB)
- .NET SDK from `global.json` only if you build `engine/`

Converted output stays in ignored `.private/`. Never commit it.

## Environment

| Variable | Purpose |
|---|---|
| `OPENBFME_GODOT` | Godot 4.7 exe (also `GODOT_CONSOLE` / `GODOT_EXE` / `GODOT`) |
| `ROTWK_INSTALL` | RotWK root containing `game.dat` |
| `BFME2_INSTALL` | BFME2 base when layering / Men wizard needs it |
| `OPENBFME_CONTENT` | Packs root (default `.private\content-packs`) |
| `OPENBFME_IMPORT_ROOT` | Importer workspace (default `.private\retail-work`) |

Godot resolution (`tools/resolve-godot.bat` / `.ps1`): env â†’ `.tools\godot\`
(gitignored drop) â†’ `godot` on PATH â†’ fail closed.

## Clone

```bat
git clone https://github.com/Open-BFME/openbfme-godot.git
cd openbfme-godot
```

## Path A â€” RotWK systems (recommended)

Matches the active product baseline and tools under `tools/rotwk_*.py`.

```bat
set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
set ROTWK_INSTALL=C:\Path\To\RotWK

powershell -File tools\gate-rotwk-systems.ps1 -SkipLiveRetail
run_rotwk_systems.bat

:: Factory / convert only (does not rewrite pack selection)
run_rotwk_one_button.bat "%ROTWK_INSTALL%"

:: Fresh checkout: cook multi-map pack, select it, launch
run_rotwk_one_button.bat "%ROTWK_INSTALL%" --multi-map --build --publish --launch
```

Operator detail: [ROTWK_SYSTEMS_PATH.md](ROTWK_SYSTEMS_PATH.md).

`selection.json` rewrites **only** with explicit `--publish` / select authority.

## Path B â€” Men pack wizard (legacy gates)

Seeds the historical Men pack used by `retail_slice_runner` /
`menu_skirmish_runner`.

```bat
python tools\onboard.py --install "C:\Path\To\BFME2" --rotwk "C:\Path\To\RotWK" --godot "%OPENBFME_GODOT%" --yes
```

Or interactively: `python tools\onboard.py`.

Flags: `--skip-gates`, `--force-convert`. Exit `0` = setup + gates green,
`1` = setup ok but gate failed, `2` = setup stop.

## Launch

```bat
run_game.bat
run_retail_slice.bat
run_retail_slice.bat --test
```

## Manual Men convert (same as wizard)

```bat
run_doctor.bat
set "PY=.private\retail-work\tools\python-3.12-env\Scripts\python.exe"
%PY% tools\openbfme_import.py doctor --install "C:\Path\To\BFME2"
%PY% tools\openbfme_import.py import-faction --install "C:\Path\To\BFME2" --faction men --convert
%PY% tools\openbfme_import.py publish-faction-to-slice --install "C:\Path\To\BFME2" --faction men --select
```

Other factions: `--faction elves|dwarves|isengard|mordor|wild`, or
`--game rotwk --faction angmar`.

## Troubleshooting

| Symptom | What to do |
|---|---|
| Doctor rejects install | Point at install root (`game.dat` / `lotrbfme2.exe`), not a partial copy |
| Godot not found | Set `OPENBFME_GODOT` or drop binary under `.tools\godot\` |
| Gate fails | Fail-closed is intentional; check [STATUS.md](../STATUS.md) |
| Convert stopped mid-way | Rerun the same command; caches resume. Avoid deleting `.private` first |
| Pre-publish scan | `powershell -File tools\export-scan.ps1` |

## Related

- [CONTENT_PIPELINE.md](CONTENT_PIPELINE.md)
- [VERIFICATION.md](VERIFICATION.md)
- [DIRECTION.md](../DIRECTION.md)
