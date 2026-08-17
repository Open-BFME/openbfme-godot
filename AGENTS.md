# AGENTS.md — read this before you touch anything

Open-BFME is a Godot 4.7 remake of *BFME2: Rise of the Witch-king* (2.01).
Retail assets are converted from the operator's own install; none ship here.

Work at the repository top level. **Do not create branches or worktrees.**

## Where things are

| Path | What it is |
|---|---|
| `game/` | Godot runtime: sim, script engine, HUD/menus, test runners |
| `importer/` | Python asset pipeline (retail → content packs) + its pytest suite |
| `engine/` | C# determinism oracle. NOT the shipping sim — a cross-check partner, run by CI |
| `launcher/` | WPF launcher, shipped artifact |
| `tools/` | Gates, publish/release scripts, one-shot operators |
| `contracts/` | Machine-validated product policy. Editing one? See "digests" below |
| `workspace/` | Retail-derived material, packs, evidence. **Never commit, never publish** |

## Run the right lane

| Goal | Command | Notes |
|---|---|---|
| Importer tests | `run_importer_tests.bat` | Needs `BFME2_INSTALL` (see below) |
| Godot runner | `<godot> --headless --path game --script res://tests/<runner>.gd` | Resolve via `tools\resolve-godot.bat` |
| Offline systems gate | `tools\gate-rotwk-systems.ps1 -SkipLiveRetail` | |
| Retail gate | `run_retail_pipeline_tests.bat` | |
| Launch the game | `run_game.bat` | |
| Pack address check | `python tools\check_pack_addresses.py` | Seconds. Run it after any pack work |

## Publishing a build

When a major thing lands, the beta gets a number and a folder you can copy to
another machine and run. Four steps, one command each.

| Step | Command | Notes |
|---|---|---|
| 1. Bump the number | edit `VERSION` (e.g. `0.2.1` → `0.2.2`) | One line. The only place the version is decided |
| 2. Restate it everywhere | `powershell -File tools\Write-BuildInfo.ps1` | Then match `config/version` in `game\project.godot` and the label in `game\scenes\boot.tscn`. Commit all four |
| 3. Write the notes | `docs\patch-notes\v<version>.md` | Owner-facing plain language: what changed for the player, and a `## Known gaps` section. The publish refuses without it |
| 4. Publish | `powershell -File tools\Publish-DistBuild.ps1 -Godot <godot.exe> -AllowEnvDependentContent -AllowPackOwnedWotrData -AllowMissingWotrData` | Lands in `dist\v<version>\`. Add `-Rc` for the release candidate (launcher included), `-Zip` to hand it over a share |

`powershell -File tools\Test-DistPipeline.ps1` checks steps 1-3 in ten to
fifteen seconds — run it before you spend an hour and a half on step 4. Two switches
exist because that hour and a half is real: `-PreflightOnly` runs every check
and the hand-off binding and stops before building; `-FinishOnly` verifies an
already-built `dist\v<version>\` and redoes only the notes, stamp and launcher
(it stamps the commit the BUNDLE records, not HEAD, and refuses if the two
differ unless you pass `-AllowFinishFromOtherCommit`).

**Step 4 needs three switches today, and every one of them is a real defect
somewhere else.** None is a preference; each is listed with what retires it.

| Switch | Why it is needed today | What retires it |
|---|---|---|
| `-Godot <path>` | `tools\resolve-godot.ps1` deliberately refuses to know about maintainer-machine paths, and this machine's Godot lives in one (`C:\Users\Jonathan\Downloads\godot47\`). Nothing is wrong; the resolver is doing its job | Drop the binary in `.tools\godot\`, or set `OPENBFME_GODOT`, and the flag stops being needed |
| `-AllowEnvDependentContent` | The exported build does not resolve `content-packs\` beside its own exe. With no environment set it finds *something* — 11 packs instead of 14, almost certainly the stale durable pack in the user profile — so the folder is `run-with-log.bat`-only, not double-clickable. The publish refuses without this flag, deliberately | Bundle-relative content resolution in the game. Until then every published folder is bat-launcher-only and its patch notes say so |
| `-AllowPackOwnedWotrData` | The active content pack now builds `data\living-world.json` into itself, and the staging rule was written when only the private workspace could supply it. Without the flag **no release can be built from the current pack set at all** | Either the packs stop carrying it, or the staging plan learns that a pack-supplied document is the normal case. The flag only reaches documents; directory bundles still refuse |

`-AllowMissingWotrData` is also needed while `OPENBFME_LIVING_WORLD_AI_TEMPLATE`
is absent from the private workspace. Produce that artefact and it goes away.

- **`dist\` and `build\` are git-ignored and stay that way.** The published
  folder carries converted retail content packs. `Publish-DistBuild.ps1` asks git
  twice — before and after the build — whether the dist root is ignored *and*
  whether git tracks anything under it, and refuses on either. Patch notes, the
  scripts and this file are committed; nothing under `dist\` ever is.
- **`Publish-DistBuild.ps1` does not reimplement the build.** It wraps
  `tools\Build-PlayableBundle.ps1`, which exports, stages the packs
  `selection.json` names, proves the staged bytes hash to the source packs and
  boots the result headless twice. Fix build behaviour there, not in the wrapper.
- **A published folder should run with no environment set, and today's do not.**
  The wrapped build boots the export once with `OPENBFME_CONTENT` and once
  without, and the publish refuses if the two runs disagree. That refusal is
  firing on every build right now, so `-AllowEnvDependentContent` is required
  and the folder is `run-with-log.bat`-only. Say that to testers; the patch
  notes lead with it. Do not treat the flag as routine once the game resolves
  content beside its own exe — at that point its absence is a regression.
- **`-Rc` builds the launcher and the install root it reads.** The launcher has
  no "browse to a game" control: it follows `current.json` to
  `versions\<version>\` and verifies those files by hash. The publish writes that
  layout and then runs `tools\release\Test-LauncherHeadless.ps1` against the exe
  it just produced. This is the LOCAL release-candidate shape — publishing to
  GitHub is a different path (`tools\release\Publish-FirstPlaytestRc.ps1`) and
  needs signing keys.

## The rules that have actually bitten us

1. **Packs are immutable, and now they are sealed.** A directory named `<sha256>`
   promises its bytes hash to that name. Published pack files under
   `workspace/content-packs/` and the durable mirror are marked read-only, so an
   in-place cook fails with an access error **at the moment you make the mistake**.
   That error is the guard working, not a bug — do not chmod your way past it.

   The flow that works: cook into a staging directory OUTSIDE the pack, compute
   `bundle_digest` on it, copy to `<root>/<pack-id>/<new-digest>` in **both** roots,
   then swap with `apply-selection-transaction` (it stages, verifies both roots, and
   is all-or-nothing). Sealing costs this nothing — publishing writes a new
   directory, and the read-only bit propagates when it is copied.

   `python tools\check_pack_addresses.py` proves every selected pack's bytes match
   its name (seconds — run it before claiming done); `tools\publish-durable-pack.ps1
   -Verify` proves the two roots agree; `python tools\seal_published_packs.py`
   re-seals, and `--unseal` is there for a deliberate re-address or deletion.
   This rule was broken three times in one night before the seal existed.

2. **Bare `pytest` lies.** It picks up the wrong Pillow and fabricates ~40
   failures. Use `run_importer_tests.bat`, or the pinned interpreter at
   `workspace\retail-work\tools\python-3.12-env\Scripts\python.exe`. Either way set
   `BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`
   — the resolver never probes `workspace` on its own.

3. **Editing a contract or provenance file breaks its seal.** `contracts/*.json`
   carry a `policy_digest` (see `tools/check-product-contracts.py`); packs carry
   `provenance/manifest.json`. Change the content, re-seal or regenerate, or CI
   goes red with a mismatch that looks unrelated to your change.

4. **Zero grep hits does not mean dead.** Script handlers under
   `game/src/script/handlers/` are loaded by a directory scan; ~35 test runners in
   `game/tests/` are real coverage that no gate currently invokes. Prove deadness
   with evidence, not silence.

5. **No silent fallbacks.** A stale pack and a wrong ffmpeg once produced
   confidently wrong numbers that passed review. If a path falls back — to a cached
   pack, a PATH binary, a default — it must say so loudly or fail closed.

6. **`workspace/` is retail material.** Never commit it, never publish converted
   retail bytes. The publication-boundary CI job scans for this, and for
   developer-machine paths (home directories, drive letters) in tracked files.

## Definition of done

A failing test first, then the fix, then the named baseline delta, then say which
pack/commit the numbers came from. Report honestly: if a lane is red, say so with
its output. Known-good baselines live in `workspace/playtest-program.md`, not in
anyone's memory.
