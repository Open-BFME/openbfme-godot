# Onboarding

This is the ten-minute path for a new contributor or player machine. It uses
the guided wizard in `tools/onboard.py`, which automates the checks and
commands that the rest of this page explains manually.

> **Documentation-preview notice:** the public GitHub repository is currently a
> documentation snapshot. The wizard and the commands below require the full
> engine/importer source tree.

## What you need before starting

- Windows 10 or 11 with PowerShell.
- A lawfully acquired *The Battle for Middle-earth II* installation updated to
  1.06 (optionally Rise of the Witch-king updated to 2.01 for the Angmar
  faction import).
- Godot 4.7 stable for Windows. Keep the executable **outside** the repository
  checkout; the project never bundles or commits it. The `_console.exe`
  variant is preferred because headless test output is visible with it.
- Git.
- Disk space for the private conversion workspace (tens of gigabytes for a
  full multi-faction working set).

Everything converted from your install stays in the ignored `.private/`
directory. Nothing retail-derived is ever committed, uploaded, or shared.

## The quick path: the onboarding wizard

From the repository root:

```bat
.private\retail-work\tools\python-3.12-env\Scripts\python.exe tools\onboard.py
```

On a machine that has never bootstrapped the importer environment, run the
wizard with any system Python 3 first; it will offer to bootstrap the pinned
importer environment for you:

```bat
python tools\onboard.py
```

The wizard walks four fail-closed steps:

1. **Prerequisites** — importer Python env (offers to bootstrap it), your
   Godot 4.7 executable (asked once, then persisted to the local untracked
   config at `.private/onboard.config.json`), and git. Blender and FFmpeg for
   the conversion lanes are pinned, bootstrapped tools verified through the
   importer doctor — you do not install them yourself.
2. **Retail source** — you point it at your BFME2 install; identity is
   validated read-only and fail-closed through the importer `doctor` command
   (wrong folder, missing archives, or wrong patch level stop the wizard with
   the doctor's report). Add `--rotwk PATH` to validate a RotWK 2.01 install
   as well.
3. **Content** — if a published Men pack is already selected in
   `.private/content-packs/selection.json`, it is verified in place. Otherwise
   the wizard runs the two-step conversion (`import-faction --faction men
   --convert`, then `publish-faction-to-slice --faction men --select`). The first
   conversion is long (roughly 30-60 minutes cold); it is resumable and
   cache-backed.
4. **Verification** — the key headless gates run against the selected pack:
   `retail_slice_runner` (hundreds of deterministic checks including the
   pinned Men battle signature) and `menu_skirmish_runner` (menu and skirmish
   setup). The wizard prints a pass/fail summary and launch instructions.

Non-interactive mode for CI or scripted setup:

```bat
python tools\onboard.py --install <BFME2> --godot C:\Tools\Godot\Godot_v4.7-stable_win64_console.exe --yes
```

Useful flags: `--rotwk PATH` (validate a RotWK 2.01 install), `--skip-gates`
(setup only), `--force-convert` (reconvert even when a pack exists),
`--config PATH`, `--state-root PATH`, `--content-root PATH`.

Exit codes: `0` = set up and gates green, `1` = setup finished but a gate
failed, `2` = a fail-closed setup step stopped the wizard.

## After the wizard: playing and developing

- `run_game.bat` — launch the game (main menu, skirmish setup, multiplayer
  lobby). Set `OPENBFME_GODOT` to your Godot executable.
- `run_retail_slice.bat` — launch the retail vertical slice directly;
  `run_retail_slice.bat --test` reruns its headless gate.
- In-game settings persist under Godot's `user://` directory
  (`%APPDATA%\Godot\app_userdata\...` on Windows).
- The active pack selection lives in `.private/content-packs/selection.json`;
  the `OPENBFME_CONTENT` environment variable overrides the content root for
  test runs.

## The manual path

The wizard only sequences existing commands. The equivalent manual steps:

```bat
:: 1. Toolchain diagnosis (fail-closed)
run_doctor.bat

:: 2. Validate the install and build/publish the Men pack
set "PY=.private\retail-work\tools\python-3.12-env\Scripts\python.exe"
%PY% tools\openbfme_import.py doctor --install <BFME2>
%PY% tools\openbfme_import.py import-faction --install <BFME2> --faction men --convert
%PY% tools\openbfme_import.py publish-faction-to-slice --install <BFME2> --faction men --select

:: 3. Verify with the headless gates
set OPENBFME_CONTENT=%CD%\.private\content-packs
"C:\Tools\Godot\Godot_v4.7-stable_win64_console.exe" --headless --path game --script res://tests/retail_slice_runner.gd
"C:\Tools\Godot\Godot_v4.7-stable_win64_console.exe" --headless --path game --script res://tests/menu_skirmish_runner.gd
```

Additional factions follow the same two-step pattern with `--faction elves`,
`dwarves`, `isengard`, `mordor`, `wild`, or (with `--game rotwk` against a
RotWK 2.01 install) `angmar`.

For the full background read the long-form walkthrough below, the
[content pipeline](CONTENT_PIPELINE.md), and [VERIFICATION.md](VERIFICATION.md).

## Troubleshooting

- **The wizard rejects my install** — point it at the installation root (the
  folder containing `lotrbfme2.exe`), not a copied selection of files. The
  doctor's report names exactly what is missing.
- **Godot not found** — pass `--godot` once; the path persists in
  `.private/onboard.config.json`. The repo convention keeps Godot outside the
  checkout, so there is no bundled fallback.
- **A gate fails** — a failing check is a real signal, not noise; the runners
  fail closed. Check [STATUS.md](../STATUS.md) for currently known failures
  before filing an issue, and include the runner output when reporting.
- **Conversion stopped partway** — rerun the same command; extraction and
  conversion are cache-validated and resumable. Do not delete `.private` as a
  first troubleshooting step.

---

<!-- merged from docs/ONBOARDING.md -->

## Reference: the full manual workflow

Everything above is the short path. What follows is the long-form walkthrough,
kept for when a step fails or you want to understand what the wizard automates.

OpenBFME is currently a Windows-first developer project. There is no supported
installer or public binary release yet. Expect rough edges and read this guide
before starting the private retail import.

> **Prefer the guided path:** `python tools\onboard.py` walks prerequisites,
> install validation, content conversion/verification, and the headless gates
> in one wizard. See [ONBOARDING.md](ONBOARDING.md) for the ten-minute
> walkthrough. This page documents the underlying manual workflow.

> **Documentation-preview notice:** the public GitHub repository does not yet
> contain the engine/importer source required by these commands. This guide is
> published so contributors can review the intended workflow before the clean
> code snapshot is approved. Do not expect the documentation-only checkout to
> run the game.

### What you need

- Windows 10 or 11.
- A lawfully acquired installation of *The Battle for Middle-earth II* updated
  to version 1.06.
- Godot 4.7 stable.
- PowerShell 5.1 or later.
- Enough free disk space for private extraction, conversion tools, caches, and
  converted output.

Python dependencies and private conversion tools are bootstrapped into the
ignored `.private/retail-work` workspace. The repository pins the .NET SDK in
`global.json` for the simulation projects.

### 1. Clone the source

```bat
git clone https://github.com/Ancalgonn/open-bfme-engine.git
cd open-bfme-engine
```

Do not place a BFME installation, extracted archive, or converted pack inside a
tracked repository directory. The importer owns the private workspace.

### 2. Tell OpenBFME where Godot is

Set `OPENBFME_GODOT` to your Godot 4.7 executable for the current Command Prompt:

```bat
set OPENBFME_GODOT=C:\Tools\Godot\Godot_v4.7-stable_win64.exe
```

PowerShell equivalent:

```powershell
$env:OPENBFME_GODOT = 'C:\Tools\Godot\Godot_v4.7-stable_win64.exe'
```

The current scripts still contain maintainer-machine fallback paths. Setting the
environment variable explicitly avoids those fallbacks until portable discovery
is completed.

### 3. Run the doctor

```bat
run_doctor.bat
```

The doctor checks the local toolchain and reports actionable missing
dependencies. Treat warnings and errors as failures for the compatibility path.

### 4. Import your local BFME2 content

```bat
run_importer.bat "D:\Games\BFME2"
```

Replace the example path with your BFME2 1.06 installation directory. The
importer will:

1. bootstrap or attest its private tools;
2. inspect the installation without modifying it;
3. discover the required source closure;
4. extract exact entries into a private cache;
5. convert them in isolated jobs;
6. validate provenance and containment; and
7. assemble an immutable local runtime pack.

The first build can be lengthy. Verified cache and conversion work is designed
to be resumable. Do not copy or share the resulting `.private` directory.

An experimental graphical entry point also exists:

```bat
import_gui.bat
```

The command-line importer remains the clearer diagnostic path while the GUI is
being hardened.

### 5. Launch the current slice

```bat
run_retail_slice.bat
```

For its focused headless validation:

```bat
run_retail_slice.bat --test
```

The final milestone acceptance command is intentionally not a newcomer smoke
test. It is identity-bound, integration-owner-only, and depends on private human
oracle evidence.

### Common failures

#### Godot 4.7 was not found

Set `OPENBFME_GODOT` to the full executable path and rerun the command.

#### The BFME2 install is rejected

Verify that the path is the game installation root and that it represents the
supported BFME2 1.06 source. Do not point the importer at a copied selection of
files or at a converted pack.

#### Import stopped partway through

Rerun the same command with the same installation and private state root. The
pipeline validates cached work before reusing it. Do not delete `.private` as a
first troubleshooting step.

#### The game launches without expected retail content

Stop and inspect the importer/selection diagnostics. Strict compatibility mode
must fail closed; a loose or synthetic development pack is not evidence that the
retail slice imported correctly.

### Useful next reading

- [Content pipeline](CONTENT_PIPELINE.md)
- [Current status](../STATUS.md)
- [Verification](VERIFICATION.md)
- [FAQ](FAQ.md)
