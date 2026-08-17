# Q2 + Q1 — publish Men/Angmar packs, swap selection, activate 79 batch packs

Repo: C:\Users\Jonathan\Desktop\open-bfme. Owner ruled 2026-08-17: finish queue
rows Q2 then Q1 (orchestration/queue.md). Claim both rows (owner=grok-q1q2)
as your first commit. Exclusive tree access. Rules: no branches/worktrees;
git add by explicit path; BANNED: git add -A, git reset/restore/clean/stash/
amend, any find-replace sweep. Long output → files under workspace/logs/.
Pinned interpreter: workspace\retail-work\tools\python-3.12-env\Scripts\python.exe
(check workspace/manifest.json). BFME2_INSTALL for anything that needs it:
<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2.
Godot: tools\resolve-godot.bat (fallback C:\Users\Jonathan\Downloads\godot47\).

## Non-negotiable invariants (from AGENTS.md rules 1, 5, 9)
- Published pack dirs `<root>/<pack-id>/<sha256>` are immutable and sealed.
  Never edit inside one; a new pack is a NEW digest dir in BOTH roots
  (workspace/content-packs and the durable root
  `%APPDATA%\Godot\app_userdata\Open BFME\content-packs`).
- Selection changes ONLY via `apply-selection-transaction` (all-or-nothing,
  verifies both roots). Never hand-edit selection.json.
- The retail gate pins the selection byte-for-byte
  (tools/gate-retail.ps1:57 `$expectedSelectionSha256`, `$expectedSelectionActivePack`,
  `$expectedSelectionSupplementalPacks`; mirrored in
  importer/tests/test_retail_gate_script.py:94). The gate's own comment says:
  when the selection legitimately moves, RE-MEASURE and update these constants
  IN THE SAME CHANGE, and say so. Do exactly that — once, at the end, after Q1.
  Never edit any OTHER pin/hash to make a check pass.
- Confirm which pack root a run actually loaded (stale durable fallback trap).

## Step 0 — Baseline (record verbatim in the report)
- `python tools\check_pack_addresses.py` (expect PASS packs=42 roots=2)
- sha256 of workspace/content-packs/selection.json (expect dcadd251…)
- Godot runners, logs to workspace/logs/q1q2-baseline-<name>.txt:
  retail_spellbook_runner (expect 218/0), retail_pack_runner (floor 175),
  goal_production_matrix_runner (floor 315), retail_slice_runner (reads its own
  RETAIL_SLICE_ACCEPTANCE line; ACCEPTANCE_MIN_PASSED 374 with 31 named known
  failures), fortress surface runner(s) for men and angmar if present
  (gate-retail.ps1 ~line 609 names them; floors men=49 angmar=56).
- Boot-time baseline: run the retail slice boot runner or whichever headless
  boot the readiness-budget test uses (search game/tests for "readiness" /
  "budget"); record wall-clock seconds.

## Q2 — publish + select the ready Men and Angmar packs
1. Read importer/openbfme_importer/cli.py:1098-1230 (`publish-faction-to-slice`)
   and pipeline.py:2454-3050 (`update_selection_entry`,
   `apply_selection_transaction`) to learn the exact publish semantics. The
   staged trees are workspace/retail-work/editions/rotwk/packs/rotwk-men-vslice/
   and rotwk-angmar-vslice/ (loose, pre-publish shape; kimi's proof digests
   were men 4f92c8a4…, angmar 48b89cf7… — see
   workspace/scratch/kimi-faction-lane-report-20260815.md:296-334).
2. Publish men, then angmar, WITHOUT `--select` (selection untouched), game
   rotwk (the CLI default), state root workspace\retail-work. Log each.
   Record the resulting bundle digests. If they equal kimi's digests, good. If
   they DIFFER (publish re-bundled), that is acceptable ONLY if the publish
   receipt shows auditValid true / conversionFailures 0 — report the new digest
   and proceed; otherwise STOP and report.
3. Mirror to the durable root: `powershell -ExecutionPolicy Bypass -File
   tools\publish-durable-pack.ps1` (read its params first; then `-Verify`).
4. `check_pack_addresses.py` → PASS (packs=44 roots=2 expected).
5. Do NOT apply the selection yet — Q1 and Q2 land in ONE transaction below.

## Q1 — activate the 79 missing-physical batch packs
1. The generated script workspace/scratch/phase2-apply-selection-transaction-20260816.ps1
   is STALE in three ways: it uses `.private` paths and the old cpython path;
   it keeps the OLD men/angmar digests; and it was never run. Do not run it.
   Instead generate a NEW arg list: active-pack = new men digest; supplemental =
   the current 20 supplemental packs with angmar replaced by the new angmar
   digest, PLUS all 79 batch packs (`{bfme2,rotwk}-missing-physical-20260816-batch-*`
   under workspace/content-packs — read the digest dir name of each; there
   must be exactly one per pack id). Save the generated command as
   workspace/scratch/q1q2-apply-selection-<date>.ps1 (untracked) for the record.
2. Ensure the durable root has all 79 batch packs (mirror step; verify).
3. Run `apply-selection-transaction` ONCE with the full list. Log it. It must
   report success and verify both roots.
4. `check_pack_addresses.py` → PASS (packs = 44 + 79 = 123 expected, roots=2).

## Checkpoint — measure before re-pinning (STOP conditions)
Re-run every Step-0 runner with the new selection, logs to
workspace/logs/q1q2-after-<name>.txt. Compare failure-by-failure vs baseline.
- STOP and report (do not re-pin, do not commit pins) if: any runner LOSES a
  previously-passing check that is not explained by content that legitimately
  changed (name it); or boot wall-clock exceeds 2x baseline; or any runner
  reports mounting/duplicate-address/precedence errors from the batch packs.
  In that case the selection stays applied (owner decides rollback), and the
  report says so plainly.
- Expected/allowed: retail_state_pin was ALREADY red before this lane (Q5);
  its hash may move again — note it, do not "fix" it.

## Re-pin (only if checkpoint passes)
Update tools/gate-retail.ps1 (`$expectedSelectionSha256`,
`$expectedSelectionActivePack`, `$expectedSelectionSupplementalPacks` — full
new list, 99 entries) and importer/tests/test_retail_gate_script.py:94 to the
new measured selection sha, with a dated comment "RE-MEASURED 2026-08-17 after
Q1/Q2 activation (men 4f92…/angmar 48b8…/79 missing-physical batches)". Run
`pytest importer/tests/test_retail_gate_script.py` (pinned interpreter) → green.
Then run `powershell -ExecutionPolicy Bypass -File tools\gate-retail.ps1` if it
completes within ~60 min (log it); if it is longer, run its Section-B runner
list individually and cite floors. Any NEW red vs its previous known state
must be named.

## Finish
- Update orchestration/queue.md rows Q1 and Q2 to CLOSED with evidence
  (digests, selection sha, runner numbers).
- Commit(s): `chore(content): publish men/angmar packs and re-pin selection
  (Q2)`, `chore(content): activate 79 missing-physical batch packs (Q1)`,
  or one combined; explicit paths; NO logs, NO workspace/ files.
- Report orchestration/reports/q1-q2-pack-activation.md: baseline vs after
  table per runner, digests, both roots verified, boot time before/after,
  the re-pin diff, anything left undone. Honest reds stay red.
