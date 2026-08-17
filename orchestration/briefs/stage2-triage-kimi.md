# Stage 2a — ledger promotion, live queue, purge docket (Kimi lane)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Stage 1 (`.private` → `workspace/`
rename) is complete and verified — see orchestration/reports/stage1-verify.md.
You have exclusive tree access. Rules: no branches/worktrees; git add by
explicit path only; BANNED: git reset/restore/clean/stash/amend, git add -A.
`workspace/` is git-ignored — nothing under it can ever be committed.

## Task 1 — Promote live ledgers into tracked docs/state/

Create `docs/state/` and copy (not move) these files into it, verbatim, each
with a one-line header noting source path and promotion date 2026-08-17:
- workspace/playtest-program.md → docs/state/playtest-program.md
- workspace/scratch/current-reachable-runtime-summary-20260815.md → docs/state/reachable-runtime-summary.md
- workspace/scratch/PARITY-LEDGER.md (if it exists; search workspace/scratch) → docs/state/parity-ledger.md
- workspace/scratch/recook-checklist-20260816.md → docs/state/recook-checklist.md
- workspace/scratch/phase2-report-20260816.md → docs/state/missing-physical-cook-report.md
BEFORE committing each: scan it for absolute machine paths (C:\Users\...) and
strip/relativize them (the publication-boundary CI rejects developer-machine
paths). Content is numbers/paths/status — no retail bytes; if you find anything
that looks like retail-derived payload data, leave that file unpromoted and note it.

## Task 2 — Create the single live queue: orchestration/queue.md

One tracked file whose job is: nothing cooked-but-unshipped or claimed-but-idle
is invisible. Seed it from the evidence (verify each item exists before listing):
1. 79 missing-physical batch packs cooked, never activated — dirs
   workspace/content-packs/{bfme2,rotwk}-missing-physical-20260816-batch-*;
   apply script workspace/scratch/phase2-apply-selection-transaction-20260816.ps1
   (never run). DECISION NEEDED by owner.
2. Men/Angmar vslice packs PUBLICATION_READY, selection never swapped
   (digests in workspace/scratch/kimi-faction-lane-report-20260815.md).
3. EVA recompose: 6 of 7 live eva-overlay packs lack semanticFieldCoverage
   (stale vs merged runtime).
4. Men faction republish (flaming trebuchet rock + WellHealFX particle).
5. retail_state_pin drift (red since 2026-08-16, unowned).
6. 6 pre-existing importer failures (see orchestration/reports/stage1-verify.md
   round-3/4 for names).
7. 15 triaged runner regressions with named causal commits
   (FAILURE_TRIAGE_TABLE.md) — owners never re-tested.
8. Castle lanes L3–L10 briefs written, never executed
   (workspace/orchestration/fable-wave/castle-lanes/).
9. men-fords generated profile regeneration (re-mint virtualPaths; see
   stage1-verify.md round-4 caveat 2).
Format: one row each — id, item, evidence path, status (READY/BLOCKED/DECISION),
owner (unassigned). Plus a "How to use this file" header: every lane claims an
item here before starting and updates it on finish.

## Task 3 — Purge docket, then delete the approved classes

Write orchestration/reports/stage2-purge-docket.md enumerating dead material
under workspace/ (top level and workspace/retail-work/): for each entry — path,
approx size, verdict KEEP / DELETE / HOLD with one-line reason.
Approved-for-deletion classes (owner ratified "restructure + purge dead"):
- workspace/cah-capture-*, workspace/cah-v2-*, workspace/cah-base-*,
  workspace/cap-r*, workspace/capture-r4* (capture-session dirs, superseded)
- Loose *.log files at workspace/ top level (attach-*, dual-*, refund-*,
  foundation-*, drawable-*)
- workspace/orchestration/queue.json, locks.json, metrics.json (stale Jul 18
  queue mechanism, superseded by orchestration/queue.md)
KEEP (do not touch): content-packs, retail-work/editions, retail-work/tools,
retail-work/oracle (reference video!), retail-oracle, scratch (still has live
apply scripts), orchestration/fable-wave (briefs/reports = institutional
history; castle-lanes L3-L10 are queued work), playtest-program.md, INDEX.json,
onboard.config.json, logs/, manifest.json, retail-extract, review.
Anything not clearly in an approved class → HOLD, not DELETE. Precedent: a prior
deletion docket had owner-KEEP items (M2 lane material) — when in doubt, HOLD.
Execute the DELETEs only after the docket is written. Record freed bytes.

## Definition of Done
1. docs/state/ exists with the promoted files, absolute paths stripped;
   committed.
2. orchestration/queue.md committed; every seeded row's evidence path verified
   real (state which you verified).
3. Purge docket committed; approved deletions executed; freed bytes reported;
   zero deletions outside the approved classes (HOLD list intact).
4. `python tools\check_pack_addresses.py` still PASS packs=42 roots=2 (proves
   you didn't touch pack roots).
5. `git status --porcelain` clean after commits; commits prefixed
   `chore(stage2):`, staged by explicit path.

Report: orchestration/reports/stage2-triage.md — per task what you did, DoD
items with verbatim output, the HOLD list for owner review.
