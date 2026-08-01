# Onboarding

Windows-first path for a new machine: clone this source tree, bootstrap the
pinned importer tools, convert content from a game install **you own**, then
run headless gates or the skirmish shell.

This is an **experimental alpha**, not a polished installer. Expect long first
converts, fail-closed doctor errors when paths are wrong, and uneven coverage
across factions/maps. Check [STATUS.md](../STATUS.md) before treating any
result as product-complete.

## Portable environment (no maintainer machine paths)

Nothing in the tree assumes a personal home-folder Godot layout or a fixed
retail drive letter. Set what you need:

| Variable | Purpose |
|---|---|
| `OPENBFME_GODOT` | Godot 4.7 executable (preferred). Also accepts `GODOT_CONSOLE` / `GODOT_EXE` / `GODOT`. |
| `ROTWK_INSTALL` | Rise of the Witch-king install root (`game.dat`). |
| `BFME2_INSTALL` | BFME2 base when layering / Men wizard paths need it. |
| `OPENBFME_CONTENT` | Content-packs root (default: `.private\content-packs`). |
| `OPENBFME_IMPORT_ROOT` | Importer private workspace (default: `.private\retail-work`). |
| `OPENBFME_FFMPEG` | Optional pinned FFmpeg override for bootstrap-tools. |
| `OPENBFME_HERO_PACK` | Required for hero-selection surface tools (pack dir with `pack.json`). |
| `OPENBFME_RANGER_PROFILE` | Required for ranger level-2 gate (profile JSON path). |
| `OPENBFME_WORKTREE` | Optional checkout path for `run_worktree_game.bat`. |

Godot resolution order (`tools/resolve-godot.bat` / `.ps1`): env →
`.tools\godot\` (local drop, gitignored) → `godot` on PATH → **fail closed**
with setup text. There is no Downloads fallback.

## What you need

- Windows 10 or 11 with PowerShell.
- Git.
- **Rise of the Witch-king 2.01** (product baseline). The importer resolves the
  BFME2 base as a layered overlay; a BFME2 1.06-only install is optional
  comparison (`--game bfme2`), not the product target.
- Godot **4.7** stable for Windows. Keep the executable **outside** the repo.
  Prefer the `_console.exe` build so headless gate output is visible.
- Disk space for `.private/` (tens of gigabytes for a multi-faction set).
- Python 3.12 available on PATH for first bootstrap (the wizard can install the
  pinned importer env under `.private/retail-work`).
- .NET SDK version from `global.json` if you build simulation projects.

Everything converted stays under ignored `.private/`. Nothing retail-derived is
committed, uploaded, or shared.

## 1. Clone

Active repository (also recorded in `config/release-source.json`):

```bat
git clone https://github.com/Open-BFME/openbfme-godot.git
cd openbfme-godot
```

Do **not** place a retail install, extract, or converted pack inside a tracked
directory. The importer owns `.private/`.

## 2. Recommended: RotWK systems path

```bat
set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
set ROTWK_INSTALL=C:\Path\To\RotWK

:: Offline gate (no retail required)
powershell -File tools\gate-rotwk-systems.ps1 -SkipLiveRetail

:: Full operator path: doctor, census, map cook, binding, faction plans
run_rotwk_systems.bat

:: One-button convert path (optional multi-map / publish / launch)
run_rotwk_one_button.bat "%ROTWK_INSTALL%"
run_rotwk_one_button.bat "%ROTWK_INSTALL%" --launch
```

Details: [ROTWK_SYSTEMS_PATH.md](ROTWK_SYSTEMS_PATH.md).

`selection.json` is only rewritten when an explicit publish/select path runs
(integration-owner authority). Workers should not flip active pack selection.

## 3. Guided wizard (Men pack + classic headless gates)

The wizard still seeds the **historical Men pack** used by
`retail_slice_runner` / `menu_skirmish_runner`. Use it when you want those gates
green quickly; use the RotWK path above for the product baseline.

```bat
python tools\onboard.py
```

Non-interactive:

```bat
python tools\onboard.py --install "C:\Path\To\BFME2" --rotwk "C:\Path\To\RotWK" --godot "%OPENBFME_GODOT%" --yes
```

Steps the wizard runs:

1. **Prerequisites** — importer Python env (bootstraps if missing), Godot path
   (persisted in untracked `.private/onboard.config.json`), git.
2. **Retail source** — fail-closed `doctor` on BFME2; optional `--rotwk`.
3. **Content** — verify selected Men pack or convert+publish Men
   (`import-faction --convert` then `publish-faction-to-slice --select`).
   Cold convert is long (often 30–60+ minutes); resumable and cache-backed.
4. **Verification** — `retail_slice_runner` and `menu_skirmish_runner`.

Exit codes: `0` set up + gates green, `1` setup ok but gate failed, `2`
fail-closed setup stop.

Useful flags: `--skip-gates`, `--force-convert`, `--config`, `--state-root`,
`--content-root`.

## 4. After setup: play and develop

- `run_game.bat` — main menu / skirmish / multiplayer lobby (`OPENBFME_GODOT`).
- `run_retail_slice.bat` — legacy vertical slice launch; `--test` reruns its gate.
- Settings live under Godot `user://` (`%APPDATA%\Godot\app_userdata\...`).
- Active pack selection: `.private/content-packs/selection.json`.
  Override content root with `OPENBFME_CONTENT` for tests.

## Manual Men path (equivalent to the wizard)

```bat
run_doctor.bat

set "PY=.private\retail-work\tools\python-3.12-env\Scripts\python.exe"
%PY% tools\openbfme_import.py doctor --install "C:\Path\To\BFME2"
%PY% tools\openbfme_import.py import-faction --install "C:\Path\To\BFME2" --faction men --convert
%PY% tools\openbfme_import.py publish-faction-to-slice --install "C:\Path\To\BFME2" --faction men --select

set OPENBFME_CONTENT=%CD%\.private\content-packs
"%OPENBFME_GODOT%" --headless --path game --script res://tests/retail_slice_runner.gd
"%OPENBFME_GODOT%" --headless --path game --script res://tests/menu_skirmish_runner.gd
```

Other factions: same two-step pattern with `--faction elves|dwarves|isengard|mordor|wild`,
or `--game rotwk --faction angmar` against RotWK.

## Troubleshooting

- **Doctor rejects install** — point at the install root (folder with
  `lotrbfme2.exe` / RotWK `game.dat`), not a partial file copy.
- **Godot not found** — set `OPENBFME_GODOT` or pass `--godot`; no maintainer
  machine fallback is shipped.
- **Gate fails** — fail-closed is intentional. Check [STATUS.md](../STATUS.md)
  and include runner output when reporting.
- **Convert stopped mid-way** — rerun the same command; caches resume. Do not
  delete `.private` as a first step.
- **export-scan fails before you publish** — never commit retail payloads or
  personal absolute paths. Run `powershell -File tools\export-scan.ps1`.

## Related docs

- [CONTENT_PIPELINE.md](CONTENT_PIPELINE.md) — import / packs / containment
- [ROTWK_SYSTEMS_PATH.md](ROTWK_SYSTEMS_PATH.md) — systems factory commands
- [VERIFICATION.md](VERIFICATION.md) — gate doctrine
- [DIRECTION.md](../DIRECTION.md) — product scope and ladder
- [MILESTONE_CURRENT.md](MILESTONE_CURRENT.md) — active systems objective
