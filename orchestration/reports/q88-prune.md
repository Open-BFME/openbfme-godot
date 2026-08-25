# Q88 — Pack prune (executed; round-2 verified with disclosed collateral)

Lane history: v1 implementation had a repo-root off-by-one that invalidated its
entire dry-run (round-1 verifier: FIX FIRST, 10 items). v2 fixed the root,
widened pin sources, and executed. Round-2 verifier: FIX AGAIN — records and
scanner hardening only; the deletion itself was independently re-derived as
safe for every selection/gate/runner/dist pin. This report is the corrected
record (round-2 items applied 2026-08-25 by claude).

## Execution record

- Deleted: **11 bundles, 0 failures** (commit 79f2a4aa; spot-verified gone from
  disk by the round-2 verifier).
- Bytes freed: **32,166,834,171 bytes (29.96 GB)** — sum of pre-delete bundle
  sizes from the corrected dry-run (`workspace/logs/q88-prune/dry-run.txt`),
  NOT a free-space delta. The earlier figure 31,919,187,666 that appeared in a
  prior version of this report was from the broken v1 run and is void.
- No log of the v2 `-Execute` run exists — v2 wrote only the dry-run table
  (the file previously named `execute.log` was a pre-fix dry-run against the
  wrong root and has been deleted). The script now writes a distinct
  `execute.txt` with per-item outcomes on every `-Execute`. Execution is
  provable from disk state + the round-2 verifier's spot checks.

## Count reconciliation (472 / 313 / 11)

Keep-set counts UNIQUE bundle ids; scans count physical directories.
313 unique kept ids = 148 present in BOTH roots + 165 in one root
→ 148×2 + 165 = 461 surviving directories; 461 + 11 deleted = 472 scanned.
Confirmed on disk post-prune: workspace 296 + durable 165 = 461.

## Regression check (round-1 verifier's required proof) — PASS

All three bundles the broken v1 would have deleted are in the KEEP set with
pin reasons (dry-run.txt): `rotwk-men-vslice/8f40f2af…` (previous-digest +
runner + gate pins), `rotwk-men-vslice/b361ec5f…` (previous-digest + dist pin),
`rotwk-mordor-vslice/82143fea…` (previous-digest + runner + gate pins).

## Post-prune proofs — ALL PASS (run by orchestrator, logs in workspace/logs/q88-prune/)

- [x] `check_pack_addresses.py` → `PACK_ADDRESS_CHECK PASS packs=200 roots=2`
- [x] `publish-durable-pack.ps1 -Verify` → "Durable cache matches the workspace
  selection (100 pack bundle(s))", exit 0
- [x] Retail slice boot → `RETAIL_STATE_PIN ticks=3000 hash=2d13b881… OK
  hash matches the pinned value`; `CASTLE_LIVE_BOOT_RESULT passed=10 failed=0`
- [x] Gate digest presence → 100 gate-pinned digests, 0 missing from disk

## DISCLOSED COLLATERAL (round-2 finding)

Two bundles pinned ONLY in Python tooling + docs were deleted:
- `rotwk-skirmish-maps-private/bc6ab089…` — pinned at
  `tools/close_goal_prop_bindings.py:1073-1076` and named in advance by
  `docs/TOPOLOGY.md:185` as exactly this risk.
- `rotwk-skirmish-maps-private/goal-official-72` — name-addressed bundle,
  structurally unpinnable by the digest regex.

Impact contained: neither was selected in any root; both fed a one-shot repair
script already applied; all proofs above pass without them. Irreversible.
`docs/TOPOLOGY.md` (:107/:185/:197) and `close_goal_prop_bindings.py` are
annotated; its defaults now require explicit `--pack` args to re-run.

## Scanner hardening from the loss (applied + re-proven)

- Pin scan now covers `tools/**/*.py` in four forms: `packid/digest`,
  path-literal, pathlib multi-line (`/ "content-packs" / "id" / "bundle"`),
  and bare 64-hex digests (matched against any on-disk pack id). Re-run
  dry-run finds 4 python pins incl. both lost bundles, now listed `pinned`.
- Name-addressed bundle dirs refused without `-AllowNamedBundles`.
- Hard-fail (never permissive) on: selection load failure, 0 gate pins,
  0 runner pins, dist selection parse failure.
- Path guard normalizes both comparands; `-Execute`/`-DryRun` mutually
  exclusive; `-Execute` writes `execute.txt` with per-item outcomes.
- Post-hardening dry-run steady state: prune list 0 bundles, keep-set 315
  (313 + the two lost bundles' pin entries, recorded without `found`).

## Standing policy

See AGENTS.md "Content pack pruning (Q88)" — corrected to describe actual
behavior: keeps ALL digests of selected pack ids (over-keep, not "one
previous"), scans gd recursively, includes dist + python pin sources.
