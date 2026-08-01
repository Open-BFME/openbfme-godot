# Getting started

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

## What you need

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

## 1. Clone the source

`config/release-source.json` is the single place the repository target is
written down. Read it from there rather than copying a URL, because the target
has already changed twice and the retired names must never be reintroduced — a
retired GitHub name can be claimed by someone else, which turns a stale clone
URL into a supply-chain problem rather than a broken link.

```bat
git clone https://<host>/<repository>.git
```

Substitute `host` and `repository` from `config/release-source.json`, or resolve
them programmatically with `tools/release_source.py`.

Do not place a BFME installation, extracted archive, or converted pack inside a
tracked repository directory. The importer owns the private workspace.

## 2. Tell OpenBFME where Godot is

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

## 3. Run the doctor

```bat
run_doctor.bat
```

The doctor checks the local toolchain and reports actionable missing
dependencies. Treat warnings and errors as failures for the compatibility path.

## 4. Import your local BFME2 content

```bat
rem RotWK systems-first (preferred):
run_rotwk_systems.bat "F:\RotWK"

rem Legacy BFME2 Men/Fords profile build:
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

## 5. Launch the current slice

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

## Common failures

### Godot 4.7 was not found

Set `OPENBFME_GODOT` to the full executable path and rerun the command.

### The BFME2 install is rejected

Verify that the path is the game installation root and that it represents the
supported BFME2 1.06 source. Do not point the importer at a copied selection of
files or at a converted pack.

### Import stopped partway through

Rerun the same command with the same installation and private state root. The
pipeline validates cached work before reusing it. Do not delete `.private` as a
first troubleshooting step.

### The game launches without expected retail content

Stop and inspect the importer/selection diagnostics. Strict compatibility mode
must fail closed; a loose or synthetic development pack is not evidence that the
retail slice imported correctly.

## Useful next reading

- [Content pipeline](CONTENT_PIPELINE.md)
- [Current status](../STATUS.md)
- [Verification](VERIFICATION.md)
- [FAQ](FAQ.md)
