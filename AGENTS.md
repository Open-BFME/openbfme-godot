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

## The rules that have actually bitten us

1. **Packs are immutable.** A directory named `<sha256>` promises its bytes hash
   to that name. Never cook, patch, or hand-edit inside a published pack under
   `.private/content-packs/` or the durable mirror. Republish a **new** digest and
   swap with `apply-selection-transaction` (it stages, verifies both roots, and is
   all-or-nothing). This rule has been broken three times; each time the runtime
   kept loading a pack whose address was a lie. `tools\check_pack_addresses.py`
   now catches it in seconds — run it before you claim done.

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
