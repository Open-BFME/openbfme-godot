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
| `.private/` | Retail-derived material, packs, evidence. **Never commit, never publish** |

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
| 4. Publish | `powershell -File tools\Publish-DistBuild.ps1` | Lands in `dist\v<version>\`. Add `-Rc` for the release candidate (launcher included), `-Zip` to hand it over a share |

`powershell -File tools\Test-DistPipeline.ps1` checks steps 1-3 in about a
second — run it before you spend half an hour on step 4.

- **`dist\` and `build\` are git-ignored and stay that way.** The published
  folder carries converted retail content packs. `Publish-DistBuild.ps1` asks git
  twice — before and after the build — whether the dist root is ignored *and*
  whether git tracks anything under it, and refuses on either. Patch notes, the
  scripts and this file are committed; nothing under `dist\` ever is.
- **`Publish-DistBuild.ps1` does not reimplement the build.** It wraps
  `tools\Build-PlayableBundle.ps1`, which exports, stages the packs
  `selection.json` names, proves the staged bytes hash to the source packs and
  boots the result headless twice. Fix build behaviour there, not in the wrapper.
- **A published folder must run with no environment set.** The wrapped build
  boots the export once with `OPENBFME_CONTENT` and once without, and the
  publish refuses if the two runs do not reach the same content census.
  `-AllowEnvDependentContent` overrides it and says so loudly in the output —
  never use it for a folder going to another machine.
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
   `.private/content-packs/` and the durable mirror are marked read-only, so an
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
   `.private\retail-work\tools\python-3.12-env\Scripts\python.exe`. Either way set
   `BFME2_INSTALL=<repo>\.private\retail-work\editions\rotwk\layered-install\layer-1-bfme2`
   — the resolver never probes `.private` on its own.

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

6. **`.private/` is retail material.** Never commit it, never publish converted
   retail bytes. The publication-boundary CI job scans for this, and for
   developer-machine paths (home directories, drive letters) in tracked files.

## Definition of done

A failing test first, then the fix, then the named baseline delta, then say which
pack/commit the numbers came from. Report honestly: if a lane is red, say so with
its output. Known-good baselines live in `.private/playtest-program.md`, not in
anyone's memory.
