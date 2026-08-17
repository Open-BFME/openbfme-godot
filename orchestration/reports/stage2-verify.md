# Stage 2a verification — adversarial, fresh context (2026-08-17)

Verifier: Opus 5, read-only. Contract: `orchestration/briefs/stage2-triage-kimi.md`.
Lane report: `orchestration/reports/stage2-triage.md`. Commits under review:
`08e048b`, `7dfa71d`, `84e00ba`, `5890c5f`, `6db3f7d`.

Every claim below was re-measured from disk/git in this session; nothing was
taken from the lane's report.

## Verdict: ACCEPT

## DoD 1 — docs/state/ promotion — PASS

- `git ls-files docs/state` → exactly the 5 expected files
  (missing-physical-cook-report, parity-ledger, playtest-program,
  reachable-runtime-summary, recook-checklist); all tracked, all present on disk.
- `git grep -nE "[A-Za-z]:\Users|C:/Users|F:\\" -- docs/state` → **no matches**
  (exit 1). Broader sweep `grep -rnE "[A-Za-z]:[\/]" docs/state/` returns a
  single hit, `res://data/base/assets/ui/*.png` — a Godot engine path, not a
  developer-machine path.
- Each file carries the required one-line promotion header naming source path
  and date 2026-08-17.
- No retail payload: largest file is 13,359 B; total 88 KB. No INI section
  headers (`^(Object|Weapon|Armor|CommandSet|FXList|ObjectCreationList)\s+`
  → zero hits), no binary blobs. The only `W3D` hits are prose/table cells
  (`W3DHordeModelDraw: 70` counts, a "W3D GLB" column header) — status and
  numbers, exactly as specified.

## DoD 2 — orchestration/queue.md — PASS

`orchestration/queue.md` is committed (`7dfa71d`), has the "How to use this
file" claim/update header, defines READY/BLOCKED/DECISION, and carries 9 rows
Q1-Q9 in the required id/item/evidence/status/owner shape (all `unassigned`).

Independently re-verified evidence claims:

| claim | re-measured result | verdict |
|---|---|---|
| (a) 79 `*missing-physical-20260816-batch-*` dirs | `ls -d workspace/content-packs/*missing-physical-20260816-batch-* \| wc -l` → **79** | PASS |
| (b) live men activePack ≠ `4f92c8a4…` | `selection.json` `activePack` = `rotwk-men-vslice/3be646b007e086152f6136b19549c9ec5a62a099d107e10e635ad5d39c5b317b`; angmar supplemental = `662cf457…`. PUBLICATION_READY digests `4f92c8a4…` / `48b89cf7…` confirmed at `workspace/scratch/kimi-faction-lane-report-20260815.md` lines ~306-314. Selection never swapped. | PASS |
| (c) only angmar eva-overlay has `semanticFieldCoverage` | Resolved all 7 overlay pack dirs by digest from `selection.json` and grepped each: men/elves/dwarves/isengard/mordor/wild = 0 hits, angmar = 1 hit. | PASS |
| (d) 15 REGRESSION rows in FAILURE_TRIAGE_TABLE.md | 16 `REGRESSION` matches: **15 table rows** (lines 19-33) plus one summary line `- **All 15: REGRESSION**`. The row count is 15. | PASS |
| (e) brief-castle-L3..L10 exist | All 8 present in `workspace/orchestration/fable-wave/castle-lanes/`; only L1/L2a/L2b have `report-castle-*.md`. | PASS |

Spot-checked the remaining evidence paths — all exist:
`workspace/scratch/phase2-apply-selection-transaction-20260816.ps1`,
`workspace/scratch/opus35-cook-report.md`,
`game/tests/retail_state_pin_runner.gd`,
`game/src/retail_slice/retail_structure.gd`.

## DoD 3 — purge docket and execution — PASS

- Docket `orchestration/reports/stage2-purge-docket.md` committed at `84e00ba`
  (before deletion), execution record appended at `5890c5f`.
- Every DELETE-verdict row (lines 13-35) falls inside an approved class:
  `cah-capture-*` (4), `cah-v2-*` (6), `cah-base-*` (1), `cap-r*` (10),
  `capture-r4*` (7), loose logs with prefixes attach-/drawable-/dual-/
  foundation-/refund- (21), and `workspace/orchestration/{queue,locks,metrics}.json`.
  **No DELETE row outside the approved classes.**
- (i) Gone from disk: `ls -d` over every approved-class glob returns nothing;
  the three orchestration JSONs are absent.
- (ii) KEEP list intact — all 14 brief-named KEEP paths present:
  content-packs, retail-work/{editions,tools,oracle}, retail-oracle, scratch,
  orchestration/fable-wave, playtest-program.md, INDEX.json,
  onboard.config.json, logs, manifest.json, retail-extract, review.
  `workspace/retail-work/oracle/captures/` still present (33 MB, contains the
  bfme2-fords-men reference-video frames) — the reference-video oracle survived.
- HOLD list spot-checked present on disk: 5 `wall-*.log`, `spawn-watchdog.log`,
  `hm-gate-steps.ps1`, `scratch-mirror.ps1`, `scratch-oldsel`, `tmp-review`,
  `worktrees`, `orchestration/{GROK-HANDOFF.md,claims,wotr}`, and
  retail-work/{backups,cache,jobs,packs,profiles,reports,captures,videos}.
- Coverage check: every entry now present at `workspace/` top level appears in
  the docket, so nothing was silently deleted-and-unlisted.

Caveat (not a failure): the "~677 MB freed" figure cannot be re-measured after
the fact. It is consistent with the docket's own per-entry sizes (capture dirs
sum to ~677 MB) and with the reported free-space delta, so it is plausible but
verified only by arithmetic, not by independent measurement.

## DoD 4 — pack address check — PASS

`python tools/check_pack_addresses.py` → `PACK_ADDRESS_CHECK PASS packs=42 roots=2`
(exit 0).

## DoD 5 — clean tree, explicit-path staging — PASS (with the lane's stated caveat)

- `git diff --stat` and `git diff --cached --stat` are both empty: **nothing
  tracked is modified, nothing staged**.
- `git show --stat` on each of the 5 commits: they touch only
  `docs/state/*.md` (5 files), `orchestration/queue.md`,
  `orchestration/reports/stage2-purge-docket.md` (x2), and
  `orchestration/reports/stage2-triage.md`. **Zero `workspace/` paths, zero
  logs, zero transcripts committed.**
- `git status --porcelain` shows 11 untracked entries. All 11 have mtimes
  between 2026-08-16 01:40 and 2026-08-17 14:47, i.e. **all pre-date the
  lane's first commit at 14:51:24**. They are prior-lane debris plus this
  lane's own input brief. The letter of "porcelain clean" is unmet; the intent
  (no uncommitted lane output, no tracked drift) is met and the lane disclosed
  it accurately.

## Discrepancies found

None material. Two precision notes for the record:

1. `grep -c REGRESSION FAILURE_TRIAGE_TABLE.md` returns 16, not 15 — the extra
   hit is the file's own summary line. Row count is 15 as claimed.
2. Q6 in queue.md names 5 distinct test names with one marked `(x2)` to reach
   6 failures; readers scanning for six names will miscount.

Everything else the lane reported matched independent measurement.
