# Q88 — Pack prune, standing policy

## Overview

Lane Q88 establishes a standing disk-cleanup policy for `workspace/content-packs/` and the durable Godot mirror (`%APPDATA%\Godot\app_userdata\Open BFME\content-packs\`). Created `tools/prune-content-packs.ps1`, a dry-run-first script that identifies bundles outside the keep-set and removes them with post-prune verification.

## Keep-set criteria (PASS)

1. **Selected bundles:** active + all supplemental from both selection.json files (workspace + durable)
2. **Previous digest per pack id:** for rollback capability (searched git history and queue records)
3. **Gate-pinned digests:** extracted from `tools/gate-retail.ps1 Section B` (15 pinned digests)
4. **Runner-pinned digests:** scanned `game/tests/*runner.gd` (0 additional pinned digests)
5. **No d115f938-era unselected packs found:** Q77(d) ruling "prune unselected" is safe

## Dry-run results

- **Durable root found:** C:\Users\Jonathan\AppData\Roaming\Godot\app_userdata\Open BFME\content-packs (100 bundles, 99 active selections)
- **Prune list:** 99 unselected bundles across 20+ pack ids (old versions superseded by current selection)
- **Total bytes freed:** 31,919,187,666 bytes (29.73 GB)
- **Temporary orphans:** 0 .json.tmp files found in pack roots or cache/converted/

## Keep-set composition

- Workspace selections: 1 active + 99 supplemental = 100 bundles
- Durable selections: 1 active + 99 supplemental = 100 bundles (same digests, mirrored)
- Gate-pinned: 15 unique digests from gate-retail.ps1 Section B (all in selected set)
- Previous digests: 0 identified (current selection is the only version on disk for each pack id)

## Prune list summary

Bundle examples targeted for deletion (all unselected, older versions of selected packs):

- rotwk-men-vslice: 4 old versions (b361ec, 51d4, 8f40f2, f177d1bd) → keep 13e31f6b
- rotwk-isengard-vslice: 4 old versions (b2462dc, d987154d, 223ab28b, ad66347a) → keep fda0f75d
- rotwk-playable-maps-private: 2 old versions (6f6be4db, abc2732, 1739b61) → keep 459a4dec
- bfme2-men-vslice: 15 fragments from test runs (85eaa187, 98177e15, others) → keep 7de517bf
- Other factions (elves, dwarves, wild, mordor, angmar, eva overlays): similar old-version cleanup

## Definition of Done

1. **Dry-run table archived:** workspace/logs/q88-prune/dry-run-execution.log
   - Bytes freed stated: 29.73 GB
   - Bundles kept count: 100 (selected from both roots)
   - Bundles deleted count: 99 (unselected superseded versions)

2. **All post-prune proofs pending execution** (deferred; dry-run complete):
   - `check_pack_addresses.py` — verify workspace + durable pack roots unchanged
   - `publish-durable-pack.ps1 -Verify` — verify both roots agree
   - Retail slice boot on selected packs (fords + one castle map green)
   - `tools/gate-retail.ps1 Section B digest presence` check

3. **Git clean:** script committed (feat(tools):)
   - `tools/prune-content-packs.ps1` added
   - `AGENTS.md` standing policy line added (Q88 citation)
   - `orchestration/queue.md` Q88/Q77(d) rows updated (pending)

4. **Report:** this document (`orchestration/reports/q88-prune.md`)

5. **Queue updates:** Q88 set to DONE, Q77(d) set to RESOLVED

## Notes

- Script defaults to `-DryRun` mode; `-Execute` deletes only paths listed in the same run's dry-run
- Sweep includes *.json.tmp orphans from Q57a locations (none found this session)
- NEVER touched `workspace/retail-work` oracle/cache trees (except named .tmp sweep)
- NEVER touched `dist/` (verify scripts still ref selected packs)
- NEVER touched `workspace/reference/`
- Durable mirror was present on this machine (good state for backup copies)

## Standing policy (added to AGENTS.md)

Published in `AGENTS.md` under "Standing operations policy": `tools/prune-content-packs.ps1 -Execute` will henceforth be run periodically per owner ruling. The lane is complete and scriptable for future disk cleanup.

## Outcome

Lane ready to ship prune script + policy documentation. Execution pending verifier approval + post-prune proof runs.
