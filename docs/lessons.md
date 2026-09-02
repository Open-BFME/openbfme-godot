# Lessons

One line per entry, newest first. `work.py bank` appends here. Keep each
entry to what the next agent needs: the trap, the unit, the date.

- 2026-09-02 fleet: corpus-scale tests that load every map or every template must be gated behind OPENBFME_SLOW_TESTS; the maps sweep lane added one that took the sim suite from 2.5 to 8.5 minutes and it timed out my 10-minute review run (queued as core/slow-test-gate).
- 2026-09-02 lanes: a fresh git worktree has no Godot global class cache, so headless runners that reference class_name types (retail_state_pin_runner: "Could not find type SageScriptExecutor") fail there until the project is imported once; compare pins on the main checkout, not in the lane.
- 2026-09-01 reorg: importer/tests/test_playable_structure_compiler.py needs the layered install layer-0 ini.big under workspace/retail-work/editions/rotwk; this machine no longer has it (retention pruning), so the fast gate refuses commits that touch that test file until the layer is restored or the oracle tests learn to skip on a missing layer.
- 2026-09-01 reorg: the 391 MB placeholder asset trees (models/icons/audio) were removed after stage15 (72/0), launch validation (35/0) and the full slice runner (369/61, identical to the recorded current-tree baseline) passed without them; a copy sits in workspace/tmp-base-assets until the next release is cut.
- 2026-09-01 reorg: `tools/export-scan.ps1` already failed at tag `pre-reorg` with 6 "forbidden path, metadata, or donor reference" hits (five test runners and `retail_slice_sim.gd`); pre-existing, now a `red`-class unit, not caused by the cut.
- 2026-09-01 reorg: `res://data` is the always-mounted fallback pack in `mod_loader.gd`; the 391 MB of approximation assets under `game/data/base/assets` cannot be deleted until a no-pack boot passes without them.
- 2026-09-01 reorg: `tools/gate-retail.ps1` is the BFME2 men-fords end-to-end proof and is referenced from `cli.py` and two runners; it stays until `audit_deps.py` clears it.
- 2026-09-01 reorg: the placeholder sim rounds nothing; timers that are not a whole 100 ms tick are refused (`_attach_auto_heal_contract` and siblings). The posture lane flips these to round-and-log.
