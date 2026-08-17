# Open-BFME live work queue

Seeded 2026-08-17 by the stage-2a triage lane
(`orchestration/briefs/stage2-triage-kimi.md`). This is the single live queue:
nothing cooked-but-unshipped or claimed-but-idle is invisible.

## How to use this file

- **Claim before you start.** Every lane writes its name into the `owner`
  column of a row before touching that work. An `unassigned` row is fair game;
  a claimed row is not.
- **Update on finish.** When the work lands, change the row's status and cite
  the commit/pack/gate output that proves it. A row is closed by evidence, not
  by assertion — delete it only once the evidence is in the row's history or a
  linked report.
- **Statuses:** `READY` (unblocked, can start now), `BLOCKED` (named
  prerequisite), `DECISION` (owner ruling required before any lane acts).
- **New work enters here first.** If it is cooked-but-unshipped or
  claimed-but-idle and it is not in this table, it is invisible — add the row.

## Queue

| id | item | evidence | status | owner |
|----|------|----------|--------|-------|
| Q1 | 79 missing-physical batch packs cooked, never activated (`{bfme2,rotwk}-missing-physical-20260816-batch-*`, 38,507 converted files); apply script `phase2-apply-selection-transaction-20260816.ps1` generated but never run | `workspace/content-packs/` (79 batch dirs counted 2026-08-17); `workspace/scratch/phase2-apply-selection-transaction-20260816.ps1`; `docs/state/missing-physical-cook-report.md` | READY | grok-q1q2 |
| Q2 | Men/Angmar vslice packs PUBLICATION_READY, selection never swapped (men bundle `4f92c8a4...`, angmar bundle `48b89cf7...`; live selection still men `3be646b0...`, angmar `662cf457...`) | `workspace/scratch/kimi-faction-lane-report-20260815.md` (digests at lines 306-314); `workspace/content-packs/selection.json` | READY | grok-q1q2 |
| Q3 | EVA recompose: 6 of 7 live eva-overlay packs lack `semanticFieldCoverage` (stale vs merged runtime); only `rotwk-angmar-eva-overlay` carries it | `workspace/content-packs/selection.json` supplemental packs; verified by scan 2026-08-17 | READY | unassigned |
| Q4 | Men faction republish (flaming trebuchet rock + WellHealFX particle); recook must emit the moduleContracts kinds or presentation work stays unbound on shipped content | `docs/state/recook-checklist.md`; `game/src/retail_slice/retail_structure.gd` (WellHealFX runtime consumer) | READY | unassigned |
| Q5 | `retail_state_pin` drift — red, un-re-minted owner decision carried since round 34; unowned | `game/tests/retail_state_pin_runner.gd`; `workspace/scratch/opus35-cook-report.md` (lines 504-507) | BLOCKED | unassigned |
| Q6 | 6 pre-existing importer failures, red since before the 2026-08-16 rename: `test_playable_unit_import`, `test_rotwk_official_map_corpus`, `test_scripts_converter`, `test_special_disguise_prerequisite`, `test_w3d_chunk_backlog` (x2) | `orchestration/reports/stage1-verify.md` round 3 criterion 2 / round 4 caveat 3 | READY | unassigned |
| Q7 | 15 triaged runner regressions with named causal commits (drawable/FXEvent refactor `7874318`, frame-clock `a733ffc`/`20f6c9c`, weapon-cycle `629b72f`, ParticleSysBone `ba1b701`, Men profile `5120925`, naval `b4f88c1`); owners never re-tested | `orchestration/reports/failure-triage-table.md` (15 verdict rows, all REGRESSION) | READY | unassigned |
| Q8 | Castle lanes L3-L10 briefs written, never executed (L1/L2a/L2b have reports; L3-L10 are brief-only) | `workspace/orchestration/fable-wave/castle-lanes/brief-castle-L3.md` … `brief-castle-L10.md` | READY | unassigned |
| Q9 | men-fords generated profile regeneration — re-mint `virtualPaths`; the 3 labels inside `men-fords-v0-complete.generated.json` still read `.private/retail-work`, a directory that no longer exists (labels are stale; the pinned identity `0bc2e767...` must be preserved by regenerating through the importer, not hand-editing) | `orchestration/reports/stage1-verify.md` round 4 caveat 2 | READY | unassigned |
| Q10 | Stage-1 intermediate commits `9c19da5`/`72a5665` left 80 MB of briefly-tracked `game/.private/` blobs (own-runtime WotR screenshots, runner logs, one agent transcript) in public history; untracked at HEAD. Owner ruled 2026-08-17: leave history as-is, no filter-repo. Recorded, closed. | `orchestration/reports/stage1-verify.md` round 4 caveat 4; `git log --diff-filter=A 9c19da5 72a5665 -- game/.private` | CLOSED (owner decision) | owner |
