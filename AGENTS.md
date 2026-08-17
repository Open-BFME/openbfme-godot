# AGENTS.md — read this before you touch anything

Open-BFME is a Godot 4.7 remake of *BFME2: Rise of the Witch-king* (2.01).
Work at the repository top level. **Do not create branches or worktrees.**

## Layout

| Path | What it is |
|---|---|
| `game/` | Godot runtime: sim, script engine, HUD/menus, test runners |
| `importer/` | Python asset pipeline (retail → content packs) + its pytest suite |
| `engine/` | C# determinism oracle. Not the shipping sim — a cross-check partner, run by CI |
| `launcher/` | WPF launcher, shipped artifact |
| `tools/` | Gates, publish/release scripts, one-shot operators |
| `contracts/` | Machine-validated product policy. Editing one? See rule 3 |
| `orchestration/` | Tracked: `queue.md`, `briefs/`, `reports/` |
| `docs/state/` | Tracked live ledgers (parity, playtest, recook, cook reports) |
| `workspace/` | Git-ignored local retail material + packs + toolchain + logs. Canonical paths in `workspace/manifest.json` |

## Retail material

Retail-derived files live under `workspace/`; use them freely. Git ignores `workspace/` and the publication-boundary CI scans tracked files for retail bytes and machine-absolute paths — that is the whole policy. Canonical keys in `workspace/manifest.json`: `retailInstall`, `pinnedPython`, `packsRoot`, `retailExtract`, `logsRoot`.

## Run the right lane

| Goal | Command | Notes |
|---|---|---|
| Importer tests | `run_importer_tests.bat` | Pinned interpreter `workspace\retail-work\tools\python-3.12-env\Scripts\python.exe`; `BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`. Baseline 2026-08-17: 6 failed / 0 errors, all pre-existing (queue Q6) |
| Godot runner | `<godot> --headless --path game --script res://tests/<runner>.gd` | Resolve via `tools\resolve-godot.bat`. Spellbook baseline: 218/0 |
| Offline systems gate | `tools\gate-rotwk-systems.ps1 -SkipLiveRetail` | |
| Retail gate | `run_retail_pipeline_tests.bat` | |
| Launch the game | `run_game.bat` | |
| Pack address check | `python tools\check_pack_addresses.py` | Seconds. Run after any pack work |
| Dist pipeline | `powershell -File tools\Test-DistPipeline.ps1` | Baseline: 25 checks |

## Publishing a build

**Release artifacts live in `dist<version>\` — nowhere else.** `workspace/content-packs/` is the
working selection, not a release. When a cook lands and the selection changes (new pack
digests, re-pinned `gate-retail.ps1`), the lane is not finished until it has produced a
versioned `dist<version>\` build (steps below) and named that version in its report and
queue row. Owner ruling 2026-08-17. Four steps, one command each.

| Step | Command | Notes |
|---|---|---|
| 1. Bump the number | edit `VERSION` (e.g. `0.2.1` → `0.2.2`) | The only place the version is decided |
| 2. Restate it everywhere | `powershell -File tools\Write-BuildInfo.ps1` | Then match `config/version` in `game\project.godot` and the label in `game\scenes\boot.tscn`. Commit all four |
| 3. Write the notes | `docs\patch-notes\v<version>.md` | Owner-facing: what changed for the player, plus `## Known gaps`. Publish refuses without it |
| 4. Publish | `powershell -File tools\Publish-DistBuild.ps1 -Godot <godot.exe> -AllowEnvDependentContent -AllowPackOwnedWotrData -AllowMissingWotrData` | Lands in `dist\v<version>\`. `-Rc` adds the launcher; `-Zip` for a share |

`powershell -File tools\Test-DistPipeline.ps1` checks steps 1–3 in ten to fifteen seconds. `-PreflightOnly` runs every check and the hand-off binding, then stops; `-FinishOnly` verifies an already-built `dist\v<version>\` and redoes notes, stamp and launcher (stamps the commit the BUNDLE records, not HEAD; refuses a mismatch unless `-AllowFinishFromOtherCommit`).

**Step 4 needs three switches today, and every one of them is a real defect somewhere else.**

| Switch | Why it is needed today | What retires it |
|---|---|---|
| `-Godot <path>` | `tools\resolve-godot.ps1` refuses maintainer-machine paths; this machine's Godot lives in one. The resolver is doing its job | Drop the binary in `.tools\godot\`, or set `OPENBFME_GODOT` |
| `-AllowEnvDependentContent` | The exported build does not resolve `content-packs\` beside its own exe. With no environment it finds something — 11 packs instead of 14, almost certainly the stale durable pack — so the folder is `run-with-log.bat`-only | Bundle-relative content resolution in the game |
| `-AllowPackOwnedWotrData` | The active pack now builds `data\living-world.json` into itself; the staging rule still expects that document only from workspace. Without the flag no release can be built from the current pack set | Packs stop carrying it, or staging treats a pack-supplied document as normal. Flag reaches documents only; directory bundles still refuse |

`-AllowMissingWotrData` is also needed while `OPENBFME_LIVING_WORLD_AI_TEMPLATE` is absent from workspace. Produce that artefact and it goes away.

- **`dist\` and `build\` are git-ignored and stay that way.** `Publish-DistBuild.ps1` asks git twice — before and after the build — whether the dist root is ignored *and* whether git tracks anything under it, and refuses on either. Patch notes, the scripts and this file are committed; nothing under `dist\` ever is.
- **`Publish-DistBuild.ps1` does not reimplement the build.** It wraps `tools\Build-PlayableBundle.ps1`. Fix build behaviour there.
- **A published folder should run with no environment set, and today's do not.** The wrapped build boots once with `OPENBFME_CONTENT` and once without; the publish refuses if they disagree. That refusal is firing, so `-AllowEnvDependentContent` is required and the folder is `run-with-log.bat`-only. Do not treat the flag as routine once the game resolves content beside its own exe.
- **`-Rc` builds the launcher and the install root it reads.** The launcher follows `current.json` to `versions\<version>\` and verifies those files by hash. GitHub publish is a different path (`tools\release\Publish-FirstPlaytestRc.ps1`) and needs signing keys.

## The rules that have actually bitten us

1. **Packs are immutable, and now they are sealed.** A directory named `<sha256>` promises its bytes hash to that name. Published pack files under `workspace/content-packs/` and the durable mirror are marked read-only, so an in-place cook fails with an access error **at the moment you make the mistake**. That error is the guard working — do not chmod your way past it.

   Cook into a staging directory outside the pack, compute `bundle_digest` on it, copy to `<root>/<pack-id>/<new-digest>` in **both** roots, then swap with `apply-selection-transaction` (stages, verifies both roots, all-or-nothing).

   `python tools\check_pack_addresses.py` proves every selected pack's bytes match its name; `tools\publish-durable-pack.ps1 -Verify` proves the two roots agree; `python tools\seal_published_packs.py` re-seals, and `--unseal` is there for a deliberate re-address or deletion.

2. **Bare `pytest` lies.** It picks up the wrong Pillow and fabricates ~40 failures. Use `run_importer_tests.bat`, or the pinned interpreter at `workspace\retail-work\tools\python-3.12-env\Scripts\python.exe`. Either way set `BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2` — the resolver never probes `workspace` on its own.

3. **Editing a contract or provenance file breaks its seal.** `contracts/*.json` carry a `policy_digest` (see `tools/check-product-contracts.py`); packs carry `provenance/manifest.json`. Change the content, re-seal or regenerate, or CI goes red with a mismatch that looks unrelated to your change.

4. **Zero grep hits does not mean dead.** Script handlers under `game/src/script/handlers/` are loaded by a directory scan; ~35 test runners in `game/tests/` are real coverage that no gate currently invokes. Prove deadness with evidence, not silence.

5. **No silent fallbacks.** A stale pack and a wrong ffmpeg once produced confidently wrong numbers that passed review. If a path falls back — to a cached pack, a PATH binary, a default — it must say so loudly or fail closed.

6. **`workspace/` is ordinary local material.** The policy is the Retail material paragraph above — do not invent a second one.

7. **Mechanical find-replace sweeps corrupt identifiers** (`workspace_root` was rewritten to a dotted-private `_root` form twice). Targeted edits only; oracle for a disputed name is `git show <pre-change-commit>:<path>`.

8. **Aggregate test counts hide regressions** — judge failure-by-failure against the named baseline. `py_compile` cannot catch a runtime `NameError`; run the tests.

9. **Hash-pinned artifacts** (generated profiles, evidence CSVs, contract `policy_digest`) are oracles: never edit the pin to match the file; regenerate through the real pipeline or restore the file. Reseal contracts with the recipe in `tools/check-product-contracts.py`.

10. **Implementor self-reports are unproven** until a fresh-context verifier re-runs the Definition of Done.

## Work protocol

- Claim a row in `orchestration/queue.md` before starting.
- Brief in `orchestration/briefs/`, report in `orchestration/reports/`.
- All logs to `workspace/logs/`. Root `.lane-*` is git-ignored and forbidden.
- One lane mutates the tree at a time.
- `git add` by explicit path. Banned: `git add -A`, `git reset`, `git restore`, `git clean`, `git stash`, `git commit --amend`.
- State which pack/commit numbers came from.

## Definition of done

A failing test first, then the fix, then the named baseline delta, then say which pack/commit the numbers came from. Report honestly: if a lane is red, say so with its output. Known-good baselines live in `docs/state/` and `orchestration/queue.md`, not in anyone's memory.
