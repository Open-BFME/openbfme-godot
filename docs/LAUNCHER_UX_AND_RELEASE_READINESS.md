# Launcher UX review + GitHub release readiness

Date: 2026-08-05 (integrity/provenance pass). Original: 2026-08-02, orchestrator
UX/branding pass + Sol engineering review (`.private/scratch/sol_launcher_review.md`).

## Branding (done)

| Asset | Role | Provenance |
|-------|------|------------|
| `Assets/openbfme-mark.png` | Window icon + top bar mark | Ledgered; owner attestation outstanding |
| `Assets/openbfme-banner.png` | Official README brand banner strip | Ledgered; **signed C2PA manifest** (OpenAI `gpt-image` 2.0, 2026-07-22) |
| `Assets/launcher-hero-bg.png` | Atmospheric left-hero background | Ledgered; owner attestation outstanding |

Hero copy: **Rise of the Witch-king · modern engine**. Assets are original brand
chrome, not retail EA art. All three now have rows in the provenance ledger
(`docs/THIRD_PARTY.md`, "Launcher chrome provenance"); two still need an owner
attestation naming the generator, tracked there as open items 6 and 7.

The banner is no longer a second copy in the launcher tree. It was byte-identical to
`docs/assets/openbfme-readme-banner.png`, so the `.csproj` links that tracked file as
the WPF resource `Assets\openbfme-banner.png` — same pack URI, 2.38 MB less repository,
no way for the launcher banner and the README banner to drift apart.

## How players get retail game files

Two paths, and the docs must not blur them.

**Primary and supported: your own lawfully owned installation.** Browse… to a BFME II /
Rise of the Witch-king folder you already have. Nothing is downloaded. This is the path
the FAQ, README and first-run copy lead with, and the only one that needs no trust in a
third party.

**Also available: the optional workshop download.** The launcher can fetch base-game
payloads from the BFME Ladder / All-in-One workshop. It ships enabled — no build flag,
no config switch — but it is not silent and it is not unverified:

| Control | Shape |
|---|---|
| One-time disclosure | Before the first download on a machine, a notice names `bfmeladder.com` as an unaffiliated community host and states the player must have the legal right to obtain the files. The acknowledgement is recorded to `third-party-download-disclosure.json` so it appears once. Informational — it does not block the flow. |
| Host allowlist | Metadata and file URLs must be credential-free HTTPS on a known BFME Ladder host, re-checked after redirects so a 302 cannot escape it. |
| **Pinned SHA-256 verification** | Every file is hashed while it downloads and compared against `launcher/OpenBFME.Launcher/retail-payload-manifest.json` before it is moved into the install tree. A mismatch aborts the run, deletes the partial, and reports the expected and actual digests. |
| Manifest-driven install set | Only paths the pinned manifest lists are written. A file the host offers but the manifest does not pin is never installed; a pinned file the host stops offering aborts the run. |
| Fail-closed when unpinned | An empty manifest makes the path refuse to run and say why. That is the state any fork starts in. |

Size-only verification is gone. Files of hundreds of megabytes were previously accepted
on length alone, which a substituted payload of the right size passes without effort.

### Where the pinned digests come from

The shipped manifest is **pinned from a maintainer's own lawfully owned BFME II and Rise
of the Witch-king installations** — 292 and 310 files, hashed locally with
`RetailPayloadManifest.PinFromInstall(...)`.

**The repository contains no retail bytes.** A SHA-256 digest is a 32-byte one-way
fingerprint; it is not the content and cannot be turned back into it. The manifest holds
only `{path, size, sha256}` — 110 KB describing 8.4 GB that stays on the maintainer's
disk.

This is the only honest way to populate the file. Copying sizes and hashes out of the
workshop API would merely re-state the host's own claims and leave the verification with
nothing external to check against. Digests must come from bytes a maintainer actually
possesses and has hashed. Populating this file any other way should be treated as a
regression.

Independently confirmed against the live workshop metadata (listing only — no payloads
fetched): the packages the provisioner downloads are `Vanilla (1.06)` and
`Vanilla (2.01)`, the same builds as the pinned installs. Every pinned path is offered
by the host, at identical declared sizes. Five intro-movie files the host offers
(`ealogo.vp6`, `newlinelogo.vp6`, `nlc_logo.vp6`, `te_logo.vp6`, `cs01.vp6`) are absent
from the maintainer's installs and are therefore unpinned and never installed — those
installs run without them. One file, `lang/englishaudio.big`, is listed by the host with
`Size: 0`; the provisioner ignores host-declared sizes entirely and uses the pinned size
and digest, so this has no effect.

To re-pin after an upstream change, run `PinFromInstall` against a verified installation
and record where the bytes came from.

### UX strengths

1. Honest product boundary — code-only + legal install called out in settings.
2. Fail-closed `release-source.json` embed — missing config fails the build.
3. Primary-monitor centering — multi-monitor friendly.
4. Wine path called out in channel/status text.
5. Checklist chips — Engine / Release / BFME / RotWK at a glance.
6. Dark/gold shell with clear PLAY CTA and settings drawer.

### UX issues (player-facing)

| Sev | Issue | Fix direction |
|-----|--------|---------------|
| **P0** | No GitHub `v*` playtest release yet | Admin control plane + first signed tag (below) |
| **P0** | Playtest channel is CLI-only (`--channel playtest`) | Settings channel selector + persist preference |
| **P0** | Play enables on engine presence, not valid `selection.json` | Read-only content selection validator; fail closed |
| **P1** | “Get ready” / Play / Update roles subtle on first run | 3-step coach: engine → retail → convert/select |
| **P1** | Convert (RotWK) says success without activating pack | **Fixed**: reports “published but not active”; Set active remains explicit |
| **P1** | Update UX: no size/version/restart CTA; feed clip at narrow width | Update review panel + constrained news cards |
| **P2** | Progress bar thin; long convert lacks stage labels | Download → Index → Convert → Ready |
| **P2** | Window.Icon is PNG not multi-size ICO | Package multi-res `.ico` |
| **P3** | Accent gold token drift App.xaml vs window | One `#C9A227` token |

### UX do-nots for release

- Do not ship retail BFME art in the launcher zip.
- Do not hide “you need a legal RotWK/BFME II install.”
- Do not auto-rewrite `selection.json` from the launcher without an explicit action.

## Sol engineering findings (summary)

Full write-up: `.private/scratch/sol_launcher_review.md`.

| Sev | Finding | Status after this pass |
|-----|---------|------------------------|
| **P0** | `release.yml` Python `-c` JSON quotes broken on Windows PowerShell (run #30730696988) | **Fixed**: `python -m tools.release_source --json` |
| **P0** | No GitHub envs / rulesets / signing secret / acceptance runner / `v*` tags | Still admin work |
| **P0** | Playtest channel not GUI-selectable | Still open (launcher PR) |
| **P0** | Play not gated on valid content selection; runtime sibling fallback | Still open (launcher + runtime PRs) |
| **P1** | Feed can rank wrong-channel releases as “ready” | Still open |
| **P1** | Tag helper allowed unsigned `-Push` (workflow rejects) | **Fixed**: `-Push` requires `-Signed`; clean-main checks |
| **P1** | Readiness script false green on public plane | **Fixed**: `LOCAL_BUILD_READY` vs `PUBLIC_RELEASE_NOT_READY` (exit 2) |
| **P1** | Retail discovery is only `game.dat` presence | **Fixed**: edition executable + importer-required core archive fingerprint; wrong/incomplete states surfaced |
| **P1** | Workshop download integrity not release-grade | **Fixed**: size-only checks replaced by pinned SHA-256 verification against a repo-tracked manifest; empty manifest = refuse to run |
| **P1** | Provenance ledger blocks GLBs/icons/music for public artifact | Still owner gate (game assets). Launcher chrome now ledgered; banner settled by signed C2PA, mark/hero await owner attestation |
| **P1** | Rollback can split game vs launcher pointers | Still open |

## Release cycle — what’s already built

| Piece | Status |
|-------|--------|
| `.github/workflows/release.yml` | Build → sign → optional VM accept → publish |
| `docs/RELEASE_CYCLE.md` | Operator cadence |
| `tools/release/Start-PlaytestRelease.ps1` | Tag helper (`-WhatIf` / `-Signed` / `-Push`) |
| `tools/release/Test-LaunchReleaseReadiness.ps1` | Local + optional GitHub preflight |
| `config/release-source.json` | The single source of the publish target (`owner/repository`) |
| Manifest RSA + public PEM | Fail-closed update path |
| Launcher self-contained publish | `dotnet publish -r win-x64` |

## Blocking for first public playtest cut

### Code (PR sequence — Sol order)

1. ~~Hosted source resolution~~ (done this pass — re-run `workflow_dispatch` to prove).
2. Persistent playtest channel + verified candidate presentation.
3. Validate selected content before Play; runtime fail-closed on bad selection.
4. ~~Edition-aware retail validation + honest RotWK activation state.~~ (done: marker-only and wrong-edition trees fail closed; conversion does not silently select).
5. Update/error UX and constrained news feed.
6. Provenance gate for shipped GLBs/icons/music (owner evidence or replace). Launcher
   chrome is done; `game/data/base/assets` is not.
7. ~~Populate `retail-payload-manifest.json` from a maintainer-verified installation~~
   (done: 602 files pinned from lawful BFME II / RotWK installs, digests only, verified
   end-to-end against those installs and cross-checked against the live workshop
   listing).

### Admin (GitHub Settings — cannot ship from code alone)

1. [ ] Branch ruleset on `main` (PR required; protect release workflow).
2. [ ] Tag ruleset on `v*` (no force-push/delete; signed tags only).
3. [ ] Environment **`release-signing`**: reviewers; secret `OPENBFME_RELEASE_SIGNING_KEY`.
4. [ ] Environment **`production-release`**: reviewers; only job with `contents: write`.
5. [ ] Self-hosted runner labels: `self-hosted, windows, x64, openbfme-release-vm` + lawful `BFME2_RETAIL_PATH`.
6. [ ] Successful unsigned `workflow_dispatch` (version `0.0.0-local`, channel playtest, no accept).
7. [ ] First real tag: `v0.1.0-playtest.1` via helper with **`-Signed -Push`**.
8. [ ] Confirm five assets on the GitHub Release page; launcher Update on channel playtest.

## Operator commands

```powershell
# Local chrome + scripts
./tools/release/Test-LaunchReleaseReadiness.ps1

# Honest public control-plane check (exit 2 = admin gaps)
./tools/release/Test-LaunchReleaseReadiness.ps1 -CheckGitHub

# Tag plan (no write)
./tools/release/Start-PlaytestRelease.ps1 -Version 0.1.0-playtest.1 -Signed -WhatIf

# After admin secrets + green workflow_dispatch:
./tools/release/Start-PlaytestRelease.ps1 -Version 0.1.0-playtest.1 -Signed -Push
```

Local launcher preview:

```bat
dotnet build launcher\OpenBFME.Launcher\OpenBFME.Launcher.csproj -c Release
dotnet run --project launcher\OpenBFME.Launcher\OpenBFME.Launcher.csproj -c Release -- --channel playtest
```

Launcher tests (prints `LAUNCHER_TESTS_PASS`, exit 0):

```bat
dotnet run --project launcher\OpenBFME.Launcher.Tests\OpenBFME.Launcher.Tests.csproj -c Debug
```

Integrity coverage lives in `pinned payload manifest`, `empty pinned manifest refusal`,
`pinned digest mismatch abort`, `pinned digest match installs`,
`workshop host allowlist rejection`, and `third-party download disclosure`. These are
hermetic — they use fixture manifests and a fake host, and do not need a retail install.

The shipped manifest was additionally verified end-to-end against the real
installations without downloading anything: the install tree was populated with hard
links to the lawful installs (same volume, no bytes copied, originals untouched), the
real cached workshop listing was served, and any payload fetch was blocked. Both games
completed with **0 installed / all skipped / 0 payload fetches** — 602 files and 8.4 GB
verified by SHA-256 through the production code path. Replacing one file with wrong
content caused exactly that file to be re-fetched, proving the check is live rather
than vacuous.

## Verdict

**Look & feel:** ready enough for screenshots / internal play — logo + hero art wired.

**Retail acquisition:** ready. Integrity is release-grade — pinned SHA-256 on every
installed byte, disclosed source host, host allowlist — and the manifest is populated
from a maintainer's lawful installations and verified end-to-end against them. Browse…
remains the primary supported path.

**Public GitHub playtest release cycle:** **not ready to cut `v0.x-playtest.N` yet.**
Local machinery is green (`LOCAL_BUILD_READY`). Public control plane is incomplete
(`PUBLIC_RELEASE_NOT_READY`). Do not tag until Sol P0 channel/selection gates and
admin envs/signing are closed.
