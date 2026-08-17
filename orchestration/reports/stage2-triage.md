# Stage 2a triage — execution report (2026-08-17)

Lane: `orchestration/briefs/stage2-triage-kimi.md`. Base: stage 1 accepted at
`db3491c` (`orchestration/reports/stage1-verify.md` round 4).

## Task 1 — ledger promotion to `docs/state/`

Commit `08e048b` — `chore(stage2): promote live ledgers from workspace to docs/state`.

All five sources existed and were copied (not moved); originals remain in
`workspace/`. Each promoted file carries a one-line header noting source path
and promotion date 2026-08-17:

- `workspace/playtest-program.md` → `docs/state/playtest-program.md`
- `workspace/scratch/current-reachable-runtime-summary-20260815.md` → `docs/state/reachable-runtime-summary.md`
- `workspace/scratch/PARITY-LEDGER.md` → `docs/state/parity-ledger.md`
- `workspace/scratch/recook-checklist-20260816.md` → `docs/state/recook-checklist.md`
- `workspace/scratch/phase2-report-20260816.md` → `docs/state/missing-physical-cook-report.md`

Machine paths stripped/relativized before commit:
- `playtest-program.md`: `F:\BFME2 base` / `F:\RotWK expansion` →
  `<retail-install>\...` (retail install root is machine-specific).
- `missing-physical-cook-report.md`: two
  `C:\Users\Jonathan\Desktop\open-bfme\.private\scratch\...` references →
  `workspace/scratch/...` (repo-relative, current post-rename layout).

Post-strip scan over `docs/state/` for `[A-Za-z]:\`, `Users\Jonathan`:
zero real hits (one `res://data/...` engine-path false positive). All content
is numbers/paths/status — no retail-derived payload found, so nothing was held
back. `.private/...` *relative* references inside the ledgers were left
verbatim per the brief (only absolute machine paths are stripped); they are
historical quotes, not live paths.

## Task 2 — `orchestration/queue.md`

Commit `7dfa71d` — `chore(stage2): create single live work queue orchestration/queue.md`.

Nine rows seeded (Q1–Q9) with the "How to use this file" claim/update header.
Every row's evidence was verified to exist before listing:

- Q1: counted the batch dirs — exactly **79** under `workspace/content-packs/`;
  `workspace/scratch/phase2-apply-selection-transaction-20260816.ps1` exists.
- Q2: `workspace/scratch/kimi-faction-lane-report-20260815.md` lines 306–314
  (men `4f92c8a4...`, angmar `48b89cf7...`, both PUBLICATION_READY);
  cross-checked `workspace/content-packs/selection.json` — live selection is
  still men `3be646b0...` / angmar `662cf457...`, i.e. never swapped.
- Q3: scanned all 7 eva-overlay packs named in `selection.json` for
  `semanticFieldCoverage`: 6 LACK (men, elves, dwarves, isengard, mordor,
  wild), 1 HAS (angmar) — matches the brief's 6-of-7 claim.
- Q4: `docs/state/recook-checklist.md` (promoted) +
  `game/src/retail_slice/retail_structure.gd` (WellHealFX consumer, grep-verified).
- Q5: `game/tests/retail_state_pin_runner.gd` exists;
  `workspace/scratch/opus35-cook-report.md` lines 504–507 record it as an
  unowned carry-over from round 34.
- Q6: six names taken verbatim from `stage1-verify.md` round 3 criterion 2 /
  round 4 caveat 3.
- Q7: `FAILURE_TRIAGE_TABLE.md` read — 15 verdict rows, all REGRESSION, causal
  commits named (17 table lines total incl. header/separator).
- Q8: `workspace/orchestration/fable-wave/castle-lanes/` listed —
  `brief-castle-L3.md` … `brief-castle-L10.md` all present; only L1/L2a/L2b
  have reports.
- Q9: `stage1-verify.md` round 4 caveat 2 (stale `.private/retail-work`
  virtualPath labels inside the pinned generated profile).

## Task 3 — purge docket + deletions

- Docket: `orchestration/reports/stage2-purge-docket.md`, committed **before**
  any deletion (`84e00ba`). Every entry under `workspace/` top level and
  `workspace/retail-work/` has path, measured size, KEEP/DELETE/HOLD verdict
  and a one-line reason.
- Deletions executed: 27 capture-session dirs (`cah-capture-*` x4, `cah-v2-*`
  x6, `cah-base-*` x1, `cap-r*` x10, `capture-r4*` x7), 21 loose top-level
  logs (approved prefixes only), and
  `workspace/orchestration/{queue,locks,metrics}.json`.
- **Freed: ~677 MB** (du-sum 677 MB + 104,204 B logs + 60,269 B JSON;
  C: free space 211,464 MB → 212,084 MB).
- Post-deletion `ls` over every approved-class pattern: "No such file or
  directory". HOLD list re-verified present. Zero deletions outside approved
  classes.
- Execution record appended to the docket (`5890c5f`).

### HOLD list (owner review)

- `workspace/wall-*.log` (5 files, 47,835 B), `workspace/spawn-watchdog.log`
  (1,350 B) — loose logs, but `wall-*`/`spawn-*` are NOT approved prefixes.
- `workspace/hm-gate-steps.ps1`, `workspace/scratch-mirror.ps1` — top-level
  scripts, no approved class.
- `workspace/scratch-oldsel/` (~8.7 GiB) — probable old-selection backup;
  likely dead, but no approved class covers it.
- `workspace/tmp-review/` (1.6 MB), `workspace/worktrees/` (1.4 GiB),
  `workspace/orchestration/GROK-HANDOFF.md`, `workspace/orchestration/claims/`,
  `workspace/orchestration/wotr/` (~30.4 GiB).
- All of `workspace/retail-work/` except the KEEP entries: `backups/`
  (15.8 GB), `cache/` (19.8 GB), `capture-junction-target/`, `captures/`
  (625 MB), `catalog/`, `jobs/` (23.8 GB), `livingmap/`, `livingmap-regions/`,
  `livingworld-autoresolve/`, `livingworld-markers/`,
  `livingworld-region-images/`, `load-captures/`, 3 ring-icon files, `packs/`
  (8.3 GB — holds the Q2 men/angmar PUBLICATION_READY staging trees),
  `profiles/` (holds the Q9 pinned generated profile), `reports/` (2.0 GB),
  `retail-payload-manifest.populated-20260810.json`, `scratch/`, `strategic-ui/`,
  `tmp/`, `videos/`.

## Definition of Done

| # | Item | Verdict | Measured output |
|---|------|---------|-----------------|
| 1 | `docs/state/` exists, promoted files committed, absolute paths stripped | **PASS** | 5 files listed by `ls docs/state/`; commit `08e048b`; path scan post-strip: zero `[A-Za-z]:\` / `Users\Jonathan` hits (one `res://` false positive) |
| 2 | `orchestration/queue.md` committed; evidence paths verified | **PASS** | commit `7dfa71d`; verification per item above (79 dirs counted, selection.json cross-checked, EVA scan 6 LACK/1 HAS, triage table 15 rows, L3–L10 briefs listed) |
| 3 | Docket committed; approved deletions executed; freed bytes reported; zero deletions outside approved classes | **PASS** | docket `84e00ba`, execution record `5890c5f`; freed ~677 MB; post-deletion `ls` confirms all approved-class paths gone, all HOLD entries present |
| 4 | `check_pack_addresses.py` PASS packs=42 roots=2 | **PASS** | `PACK_ADDRESS_CHECK PASS packs=42 roots=2` (run after deletions, re-run after all commits) |
| 5 | `git status --porcelain` clean after commits; commits prefixed `chore(stage2):`, staged by explicit path | **PASS on intent, letter caveat** | verbatim output below; all four commits (`08e048b`, `7dfa71d`, `84e00ba`, `5890c5f`) prefixed `chore(stage2):` and staged by explicit path only |

DoD-5 verbatim `git status --porcelain` after all lane commits:

```
?? FAILURE_TRIAGE_TABLE.md
?? FINAL_STATUS.txt
?? PHANTOMS.md
?? gate-wiring-final-brief.md
?? orchestration/briefs/stage1-fixes-r2.md
?? orchestration/briefs/stage1-fixes-r3.md
?? orchestration/briefs/stage1-fixes.md
?? orchestration/briefs/stage2-triage-kimi.md
?? orchestration/reports/stage1-fixes-r2.md
?? orchestration/reports/stage1-fixes-r3.md
?? orchestration/reports/stage1-verify.md
```

Nothing tracked is modified and nothing from this lane is uncommitted. The 11
residual entries are all **untracked files that pre-date this lane**: the root
debris is documented in `stage1-verify.md` round-4 caveat 5 ("untracked agent
debris at root … deletable at leisure"), and the `orchestration/` entries are
stage-1 briefs/reports plus this lane's own brief. Committing or deleting
other lanes' files is outside this brief's mandate (no approved class covers
them), so they are left as-is and flagged here for the owner.
