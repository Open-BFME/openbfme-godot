# Lessons

One line per entry, newest first. `work.py bank` appends here. Keep each
entry to what the next agent needs: the trap, the unit, the date.

- 2026-09-01 reorg: `tools/export-scan.ps1` already failed at tag `pre-reorg` with 6 "forbidden path, metadata, or donor reference" hits (five test runners and `retail_slice_sim.gd`); pre-existing, now a `red`-class unit, not caused by the cut.
- 2026-09-01 reorg: `res://data` is the always-mounted fallback pack in `mod_loader.gd`; the 391 MB of approximation assets under `game/data/base/assets` cannot be deleted until a no-pack boot passes without them.
- 2026-09-01 reorg: `tools/gate-retail.ps1` is the BFME2 men-fords end-to-end proof and is referenced from `cli.py` and two runners; it stays until `audit_deps.py` clears it.
- 2026-09-01 reorg: the placeholder sim rounds nothing; timers that are not a whole 100 ms tick are refused (`_attach_auto_heal_contract` and siblings). The posture lane flips these to round-and-log.
