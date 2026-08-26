# Q88 — prune content packs to one good set (standing disk policy)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md. Claim row Q88 in
orchestration/queue.md (owner=codex-q88). Explicit-path git; logs →
workspace/logs/q88-prune/. THIS LANE DELETES DATA — dry-run first, keep-set
proof before any Remove-Item, no deletion outside the roots named below.

## Why (owner 2026-08-25)
workspace/content-packs holds ~226 bundle directories across ~109 pack ids;
exactly one set is selected. Nothing prunes; the owner's disk keeps filling.
Prior art: the v0.2.6 finish report contains a disk-prune recipe
(orchestration/reports/, search "prune") — read it first. Q57(a) also recorded
write_json_atomic .tmp orphans; Q77(d) recorded freshly published UNSELECTED
faction packs (d115f938 etc.) — owner ruling "we just need 1 good pack"
resolves Q77(d) toward PRUNE, not select.

## Keep-set (compute and PROVE before deleting anything)
A bundle survives if ANY of:
1. Referenced by selection.json in EITHER root (workspace/content-packs AND
   the durable root %APPDATA%\Godot\app_userdata\Open BFME\content-packs) —
   active + every supplemental entry.
2. The immediately previous digest of each currently selected pack id (one
   rollback step), if identifiable from selection history/reports.
3. Pinned by any gate: grep tools/gate-*.ps1 (Section B pin lists) and
   game/tests/*runner.gd for hard-coded bundle digests; every digest that
   appears survives.
4. Referenced by the shipped dist (dist/v0.2.12 manifest/run config), if it
   points at digests.
Everything else in those two roots is delete-eligible. NEVER touch
workspace/retail-work (caches/oracle), dist/, or workspace/reference/.

## Deliverable
1. tools/prune-content-packs.ps1: `-DryRun` (default) emits a full table
   bundle → keep|delete → reason → bytes; `-Execute` deletes only what the
   same run's dry-run listed. Also sweeps *.json.tmp orphans under the pack
   roots and workspace/retail-work/cache/converted (Q57a shape) — list them
   in the table.
2. Run dry-run, save workspace/logs/q88-prune/dry-run.txt, THEN execute.
3. Post-prune proof: check_pack_addresses.py PASS packs/roots unchanged;
   publish-durable-pack.ps1 -Verify agrees; boot the retail slice on selected
   packs (fords + one castle map) green; gate-retail Section B digests all
   still present on disk.
4. Standing policy line added to AGENTS.md operations section (or the house
   doc the verifier will accept): publishes prune to selected+previous per
   pack id; cite this lane.

## Definition of Done (verbatim in report)
1. Dry-run table archived; bytes freed stated (before/after `Get-PSDrive` or
   directory size proof).
2. All post-prune proofs green (logs saved).
3. git clean (script + doc line committed, `feat(tools):`).
4. Report orchestration/reports/q88-prune.md; update rows Q88 and Q77(d)
   (resolved: pruned per owner one-good-pack ruling — or, if any d115f938-era
   pack is newer than selected and looks intended, STOP and flag for owner
   instead of deleting).
