# OpenBFME release cycle

How playtest and stable builds get onto GitHub so the launcher can update.

The machinery already lives in [`.github/workflows/release.yml`](../.github/workflows/release.yml)
and [`BUILD_AND_RELEASE.md`](BUILD_AND_RELEASE.md). This page is the **operator cadence**:
what to cut, when, and which buttons to press.

## Channels

| Channel | When | Tag shape | GitHub Release |
|---------|------|-----------|----------------|
| **playtest** | Weekly (or on demand) | `v0.1.0-playtest.N` or `v0.1.0-rc.N` | **Pre-release** |
| **stable** | When playtest is good enough | `v0.1.0` (no pre-release suffix) | Full release |
| **nightly** | Optional automation later | `v0.0.0-nightly.YYYYMMDD` | Pre-release |

The launcher:

- stable → prefers non-prerelease; falls back through the GitHub Releases API if `/releases/latest` is empty  
- playtest / nightly → scans Releases API (including prereleases) for `release-manifest.json`

## Prerequisites (one-time, repo admin)

Configure these in **GitHub → Settings** before the first real publish. The workflow
YAML alone is not enough (see BUILD_AND_RELEASE.md “Security gates”).

1. **Branch ruleset** on `main` — require PR review; protect `.github/workflows/release.yml`.
2. **Tag ruleset** on `v*` — restrict who can create tags; no force-push / delete.
3. **Environment `release-signing`** — required reviewers, no admin bypass; secret
   `OPENBFME_RELEASE_SIGNING_KEY` (private PEM only in this environment).
4. **Environment `production-release`** — required reviewers; only this job gets
   `contents: write` to create the GitHub Release.
5. Confirm `config/release-source.json` `repository` equals the GitHub repo
   this tree is published to. That file is the only place the target may be
   written down; read it rather than copying the name into docs or scripts.

## Cadence (recommended)

### Playtest (every week or when ready)

```text
1. Merge what you want onto main.
2. Choose the next SemVer playtest version, e.g. 0.1.0-playtest.1
3. Create an annotated, signed tag:
     git tag -s -a v0.1.0-playtest.1 -m "Playtest 0.1.0-playtest.1"
     git push origin v0.1.0-playtest.1
4. Watch Actions → "windows release".
5. After sign + publish succeed, the launcher "Check for update" on channel
   playtest should find release-manifest.json.
```

Or dry-run without publishing:

```text
Actions → windows release → Run workflow
  version: 0.0.0-local
  channel: playtest
  run_acceptance: false
```

That builds the unsigned artifact only (no signing secret, no GitHub Release).

### Stable

```text
1. Playtest has been soak-tested.
2. Tag without a pre-release suffix:
     git tag -s -a v0.1.0 -m "OpenBFME 0.1.0"
     git push origin v0.1.0
3. Same workflow; publish is a full (non-prerelease) GitHub Release.
```

## What a release contains

Exactly five public assets (see BUILD_AND_RELEASE.md):

- `OpenBFME-<version>-windows-x64.zip`
- `OpenBFME-Launcher-<version>-windows-x64.zip`
- `release-manifest.json`
- `release-manifest.json.sig`
- `SHA256SUMS.txt`

The launcher refuses any manifest that fails RSA verify against the pinned public key.

## Operator script

From a clean main checkout (Windows, `gh` + `git` available). The hosted workflow
requires a **GitHub-verified signed annotated tag**, so production push always
uses `-Signed`:

```powershell
# Local + public preflight (public incomplete exits 2)
./tools/release/Test-LaunchReleaseReadiness.ps1
./tools/release/Test-LaunchReleaseReadiness.ps1 -CheckGitHub

# Preview only — does not tag or push
./tools/release/Start-PlaytestRelease.ps1 -Version 0.1.0-playtest.1 -Signed -WhatIf

# Create signed annotated tag and push (triggers windows release)
./tools/release/Start-PlaytestRelease.ps1 -Version 0.1.0-playtest.1 -Signed -Push
```

`-Push` without `-Signed` is refused. Local-only unsigned tags require
`-AllowUnsignedLocal` (never for publish).

## Current gap (honest)

As of the first playtest cycle setup, **no `v*` release may exist yet**. The launcher
UI reports that clearly. Until the first signed tag publishes, players still get:

- silent BFME II / RotWK download via the workshop path  
- convert + local Godot launch from source  

They do **not** get one-click engine install until a release is published.

## Linux players

The published artifact is still the **Windows** self-contained zip. Linux users run
that binary under Wine — see [LAUNCHER_LINUX_WINE.md](LAUNCHER_LINUX_WINE.md).
There is no separate Linux GUI package in the release set.

## Checklist before each playtest cut

- [ ] `main` is green enough for playtesters  
- [ ] `config/release-source.json` still points at this repo  
- [ ] Version SemVer is unique and not already a tag  
- [ ] Signing environment secret is present (for real publish)  
- [ ] After publish: open the GitHub Release page and confirm all five assets  
- [ ] Point a clean machine launcher at `--channel playtest` and hit Check for update  

## Preflight (local)

```powershell
./tools/release/Test-LaunchReleaseReadiness.ps1
./tools/release/Test-LaunchReleaseReadiness.ps1 -CheckGitHub   # optional, needs gh
```

Launcher branding / first-run UX notes: [LAUNCHER_UX_AND_RELEASE_READINESS.md](LAUNCHER_UX_AND_RELEASE_READINESS.md).
