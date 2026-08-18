# Test registry consolidation — one manifest, named-check pins, one `test-all` (Sol; Opus review)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md (rules 1-10). Claim
row Q23 in orchestration/queue.md (owner=sol-testreg). Standard lane rules.
No pack builds/selection changes. This lane changes TEST PLUMBING ONLY — no
sim, importer, or runner assertion logic changes.

## Why (measured 2026-08-17)
- 345+ `game/tests/*_runner.gd`; ~200 wired to some gate, the rest orphaned;
  gates are PowerShell scripts with inline hashtables of integer pass counts
  (tools/gate-m2-focused.ps1, gate-retail.ps1 Section A/B floors,
  gate-rotwk-systems.ps1, gate-orphan-runners.ps1 + orphan-runners-manifest.csv).
- Integer pins rot silently: `member_health_overlay` pinned 13 while the runner
  emitted 15; `member_combat` pinned 49 while it emitted 98; the M2 focused gate
  has been DEAD since the Q1 selection swap because tools/m2-oracle-common.ps1:36
  hardcodes an activePack prefix (queue Q15).
- Aggregate counts hide regressions (Stage-1 verify: 10 new failures inside an
  improved total). Verifiers had to diff failure NAMES by hand every lane.

## Deliverable
1. `tools/test-registry.json` (tracked): one entry per runner —
   `{ id, script, tier: "gate"|"nightly"|"orphan"|"retired", pinned_checks:
   [named check ids that must PASS], known_failures: [named], floor: int|null,
   needs: ["selection","retail-install","network"], timeout_s, owner_lane,
   result_regex }`. Seed it by MEASURING each runner once (sequentially; log
   each) and by lifting the current pins/floors from the four gate scripts —
   record every discrepancy you find between a pinned integer and the measured
   count as a row in the report (do not "fix" the runner; record).
2. `tools/test-all.ps1` — runs a tier (default `gate`) sequentially with
   per-runner timeouts, parses each runner's `*_RESULT` + per-check PASS/FAIL
   lines, and fails on: any pinned check not PASS, any known_failure that
   PASSES (stale pin → must be updated consciously), any NEW failure name not
   in known_failures, any runner not producing its result marker (silent
   abort), forbidden diagnostics (`SCRIPT ERROR`, unlisted `ERROR:`). Emits a
   machine-readable delta report (`workspace/logs/test-all-<stamp>.json`) and a
   one-screen human summary. `-Update` mode rewrites pins from a green run
   ONLY when invoked with `-IUnderstandThisMintsPins` and writes a dated
   provenance note per changed row.
3. Migrate the four gate scripts to call `test-all.ps1 -Tier gate` for their
   runner sections (keep their non-runner steps — importer floors, pack checks,
   pins — untouched), and retire the inline hashtables. Selection-dependent
   preconditions (m2-oracle-common.ps1:36) become `needs:` entries that SKIP
   with a loud reason rather than silently fail — but keep the M2 gate red/skip
   until Q15 is decided; do not retarget it yourself.
4. Orphan tier: import tools/orphan-runners-manifest.csv into the registry as
   `tier: "orphan"` with measured status; nothing about their assertions changes.
5. AGENTS.md "Run the right lane": replace the per-gate rows with
   `tools\test-all.ps1 -Tier gate|nightly` and one line on `-Update`.

## Tests — failing-first
- A registry-parse unit test (pytest, importer/tests or tools/tests) that
  fails on duplicate ids, unknown tiers, or a script path that does not exist.
- A fake-runner fixture proving each fail mode: pinned check missing; known
  failure passing; new failure name; missing result marker; forbidden
  diagnostic.

## Definition of Done
1. `tools\test-all.ps1 -Tier gate` runs to completion on the current selection;
   summary pasted; every red is either pre-existing (named, with queue row) or
   explained — zero silent.
2. The four migrated gates produce the same PASS/FAIL verdicts as before the
   migration on the same tree (paste both runs).
3. Discrepancy table (pinned int vs measured) in the report; each becomes a
   queue row or a conscious pin update with provenance.
4. Full importer suite unchanged (6 Q6 names, 0 errors); hygiene PASS; pack
   addresses PASS; git status clean.
5. Report orchestration/reports/test-registry-consolidation.md; Opus review
   before acceptance (this touches every gate).
