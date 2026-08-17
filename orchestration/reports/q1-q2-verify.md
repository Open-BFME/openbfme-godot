# Q1/Q2 pack activation — adversarial verification

Verifier: fresh-context, read-only (this report + `workspace/logs/q1q2-verify-*`).
Date: 2026-08-17. Target: brief `orchestration/briefs/q1-q2-pack-activation-grok.md`,
report `orchestration/reports/q1-q2-pack-activation.md`, commits `3c2e439`, `e1ba04c`.
Selection, packs and pins were NOT modified by this verification.

## Verdict

**FIX-FIRST** — one defect, cheap to close. Everything the implementor claimed
about selection, digests, pins and runner numbers is true and independently
reproduced. But the two newly published packs were never sealed, and the
implementor's headline "3 time-budget failures / +16 s boot" does not reproduce
on a clean machine — the true cost is far smaller than the report says.

### Fix before accept

1. **Run `python tools\seal_published_packs.py`.** `rotwk-men-vslice/4f92c8a4…`
   and `rotwk-angmar-vslice/48b89cf7…` are writable in **both** roots
   (4639/4639 and 2582/2582 files, readonly=0). Their direct predecessors
   `rotwk-men-vslice/3be646b0…` (readonly=4785/4785) and
   `rotwk-angmar-vslice/662cf457…` (readonly=2578/2578) **are** sealed, so this
   is a seal regression for exactly the two packs this lane published. AGENTS.md
   rule 1 and the brief's invariant both require it. One command, no re-address.

Note while sealing: 11 further selected packs per root were already unsealed
before this lane (`bfme2-men-vslice`, `bfme2-skirmish-maps-private`,
`rotwk-playable-maps-private`, `rotwk-cursors-vslice`, and all 7
`*-eva-overlay`). Pre-existing, not this lane's regression, but the same command
closes them.

## Criterion-by-criterion

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 1 | selection sha256 == `04229f76…41e` | **PASS** | workspace and durable both hash to `04229f763fd6f41d3637d6747a73fe5a2b8e198dd6b3325907de90e1b08fa41e`; files byte-identical (`diff` clean) |
| 1 | activePack men `4f92c8a4…` | **PASS** | `rotwk-men-vslice/4f92c8a486861100c29f20d1287f01990bc835a2622c53e911cfd2fb024a147e` |
| 1 | supplemental contains angmar `48b89cf7…` | **PASS** | present in the 99-entry list |
| 1 | all 79 batch entries, digests == on-disk dir names | **PASS** | 79 batch entries, 79 unique pack ids (39 `bfme2-*`, 40 `rotwk-*`); exactly one digest dir per batch id; 0 mismatches |
| 1 | durable root has every referenced digest dir | **PASS** | 100 entries × 2 roots = 200 directory probes, **0 missing** |
| 2 | `check_pack_addresses.py` PASS packs=200 roots=2 | **PASS** | ran it: `PACK_ADDRESS_CHECK PASS packs=200 roots=2` |
| 3 | gate-retail.ps1 pins agree with live selection | **PASS** | sha, activePack, and all 99 supplementals match live selection **in exact order** (not merely as a set) |
| 3 | `pytest test_retail_gate_script.py` green, pinned interpreter | **PASS** | ran it with `workspace\retail-work\tools\python-3.12-env\Scripts\python.exe`: **12 passed in 0.10s** |
| 3 | no other pin/hash changed in e1ba04c | **PASS** | 3 hunks in gate-retail.ps1, all inside the selection-constant block; every changed line is a selection pin, a batch entry, a dated comment, or the trailing-comma on `rotwk-neutral-vslice`. Test file: single sha line |
| 4 | both packs exist in both roots | **PASS** | all four directories present |
| 4 | bundle digest recomputes to dir name | **PASS** | covered by `check_pack_addresses.py` PASS over all 200 selected entry×root pairs — that check recomputes bytes-to-name for every selected pack including these two |
| 4 | packs sealed read-only | **FAIL** | readonly=0 on all files in both roots, both packs. See fix item 1 |
| 5 | spellbook 218/0, loads workspace packs | **PASS** | re-ran: `RETAIL_SPELLBOOK_RESULT passed=218 failed=0`; `[ModLoader] content source=workspace active=…/rotwk-men-vslice/4f92c8a4…`; `[ContentDB] legacy-demo: packs=102` |
| 5 | boot runner 44 checks / 3 budget failures | **DOES NOT REPRODUCE** | see below — clean run is 44/**0** |
| 5 | budgets ordering-critical or coarse net? | coarse net | runner source, verbatim |
| 5 | runner in any gate? | **no** | see below |
| 6 | STOP adjudication correct | **PASS** | kimi report confirms the residual gap |
| 7 | e1ba04c touches only the 4 intended files | **PASS** | `--stat`: `test_retail_gate_script.py`, `queue.md`, `q1-q2-pack-activation.md`, `gate-retail.ps1`. No logs, no `workspace/` |

## Correction: the boot-time regression is largely an artifact

The implementor reported `boot_startup_runner` at **44 checks / 3 failures /
46.2 s**, and attributed the three budget misses to "loading 102 packs instead
of 23". I could not reproduce that.

| run | conditions | result | wall | log |
|---|---|---|---|---|
| implementor | unknown | 44 checks / **3** fail | 46.2 s | their `q1q2-after-*` |
| verifier #1 | run concurrently with the spellbook runner | 44 checks / **2** fail | 42 s | `workspace/logs/q1q2-verify-boot.txt` |
| verifier #2 | **solo, quiet machine** | 44 checks / **0** fail | **33 s** | `workspace/logs/q1q2-verify-boot-solo.txt` |

Failing ms values observed (run #1): `first_frame` 7308 > 7000, `shell_visible`
14021 > 12000. `shell_instantiated` (2057 > 1800 for the implementor) passed for
me even under load. On the solo run all three are under budget.

So the three failures track **machine load, not pack count**. The implementor's
causal claim is wrong; their *conclusion* (no STOP) is right, and in fact holds
more strongly than they argued.

**Ordering vs budgets.** The runner is explicit (`game/tests/boot_startup_runner.gd`
lines 24-28):

> `## ORDERING CHECKS ARE THE POINT; the millisecond budgets are a coarse net.`
> `## Budgets are set well above measured values so ordinary machine variance never`
> `## fails the suite - they exist to catch a stage regressing by a multiple, not by`
> `## a jitter. The ordering assertions below have no tolerance at all and are what`
> `## actually pin the architecture.`

Every ordering assertion passed in all three runs. Only the coarse net twitched,
and only under load — arguably the budgets are now slightly *less* generous than
their own docstring intends.

**Gate membership: none.** `grep` over `tools/` and `.github/` finds
`boot_startup_runner` only inside `tools/orphan-runners-manifest.csv` — the
inventory of runners that no gate invokes. It is not wired into any gate or CI
workflow. **The activation turned no gate red.** The only gate touching this
selection is `gate-retail.ps1`, whose selection constants were re-pinned and
whose mirrored test passes 12/12.

## STOP-condition adjudication (item 6) — implementor was correct

Two checks lost: production `unit_present_GondorKnightsofDolHorde`, slice
`wrong_producer_rejects_gondor_knightsof_dol_horde`.

`workspace/scratch/kimi-faction-lane-report-20260815.md` documents both as the
new Men pack's named residual gap, independently of this lane (lines 280-288,
360-363): `GondorKnightsofDol` + `GondorKnightsofDolHorde` — "AutoHealBehavior
unsupported fields: healonlyifnotunderattack"; retail authors
`HealOnlyIfNotUnderAttack = Yes` in ModuleTag_HearthHeal. Line 297 records the
pack's own receipt: `residualGaps=[GondorKnightsofDol, GondorKnightsofDolHorde]`.

Both lost checks are named-content-explained by a gap that predates and is
independent of the activation. **The no-STOP call was correct.** The units are
absent because the converter waived them, not because activation broke mounting.

## What I did not verify

- I did not re-run `retail_pack_runner`, `goal_production_matrix_runner`,
  `retail_slice_runner`, or the two fortress runners. Their before/after numbers
  are the implementor's. The two named losses are corroborated by the kimi
  report but I did not re-observe them.
- I did not re-measure the **baseline** boot time (29.7 s) — that would require
  reverting the selection, which I am forbidden to do. So the clean delta
  "29.7 s → 33 s" mixes my measurement with theirs and is indicative only.
- Full `gate-retail.ps1` was not run (Section A exceeds the time budget), same
  as the implementor.
- `retail_state_pin` remains red (Q5), pre-existing, untouched by this lane.

## Owner note — is the per-pack mount cost worth a code fix?

The premise as handed to me (23 → 102 packs, **+16 s** boot) is inflated: that
delta came from a loaded-machine measurement. On a quiet machine the activated
selection boots in 33 s with all 44 checks green, against a 29.7 s baseline —
call it **+3 s for +79 packs**, roughly 40 ms per pack. That is not a scaling
cliff; it is close to the floor for opening, stat-ing and indexing 79 additional
directory trees, and it is linear rather than quadratic. My honest read is
**intrinsic, not worth a code fix today**. Two caveats worth banking. First, the
mount loop emits a `[mounted pack] - no manifest.json` probe line per pack per
data subdirectory, which is thousands of lines of log noise and a hint that the
loader stats directories it could skip from the manifest — a cheap win if anyone
is in there anyway. Second, the budgets are now close enough to the line that
ordinary load tips them over, which will read as a flaky failure to whoever next
runs this runner. Since the docstring's stated intent is that variance must
never fail the suite, either raise `BUDGET_FIRST_FRAME_AT_MS`/
`BUDGET_SHELL_VISIBLE_AT_MS` deliberately with a dated comment, or accept that
the runner is load-sensitive and keep it out of gates — where, usefully, it
already is.

## Closure (orchestrator, 2026-08-17)
FIX-FIRST item resolved: `python tools\seal_published_packs.py` → `SEAL_PACKS DONE action=sealed packs=200 files_changed=45144`.
Proof: rotwk-men-vslice/4f92c8a4… 4639 files writable=0 and rotwk-angmar-vslice/48b89cf7… 2582 files writable=0 in BOTH roots; `PACK_ADDRESS_CHECK PASS packs=200 roots=2`. Q1/Q2 ACCEPTED.
