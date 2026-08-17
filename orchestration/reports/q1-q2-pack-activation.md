# Q1/Q2 pack activation — 2026-08-17

Lane: grok-q1q2. First commit claimed both rows: `3c2e439`.
Exclusive tree access. No branches/worktrees. Selection changed only via
`apply-selection-transaction`. No hand-edit of `selection.json`. The only
pins edited are the three Section-B selection constants in
`tools/gate-retail.ps1` plus the mirrored sha in
`importer/tests/test_retail_gate_script.py`.

## Digests

| pack | digest | notes |
|---|---|---|
| `rotwk-men-vslice` | `4f92c8a486861100c29f20d1287f01990bc835a2622c53e911cfd2fb024a147e` | equals kimi proof; `auditValid=true`; `conversionFailures=0` |
| `rotwk-angmar-vslice` | `48b89cf72b5bd28eb1867bdecaa5072791be4d70808597fdb5c8975df03414bc` | equals kimi proof; `auditValid=true`; `conversionFailures=0` |

Published into both roots (`workspace/content-packs` and
`%APPDATA%\Godot\app_userdata\Open BFME\content-packs`) with
`ImportPipeline.publish_to_godot(..., select=False)` — the same publish body
`publish-faction-to-slice` uses when `--select` is omitted. The PUBLICATION_READY
staged trees under `workspace/retail-work/editions/rotwk/packs/rotwk-{men,angmar}-vslice/`
were not recooked.

## Selection

| moment | sha256 | active | supplements | address check |
|---|---|---|---|---|
| Step 0 baseline | `dcadd25179a531ab888befb329af66fabaefc25381aa9088feb1cdf1dbffddf3` | men `3be646b0…` | 20 | PASS packs=42 roots=2 |
| after Q2 publish (no select) | `dcadd251…` (unchanged) | men `3be646b0…` | 20 | PASS packs=42 roots=2 |
| after one `apply-selection-transaction` | `04229f763fd6f41d3637d6747a73fe5a2b8e198dd6b3325907de90e1b08fa41e` | men `4f92c8a4…` | 99 | PASS packs=200 roots=2 |

Transaction receipt: `verified=true`, `changed=true`, `swaps=2`, both roots
byte-identical. Generated command (untracked):
`workspace/scratch/q1q2-apply-selection-20260817.ps1`. The stale
`phase2-apply-selection-transaction-20260816.ps1` was not run.

`check_pack_addresses.py` counts **selected entries × roots**. The brief's
"44 after Q2 / 123 after Q1" assumed unique-pack arithmetic. Observed:
42 (21×2) until the swap, then 200 (100×2). Honest reds stay red; the
checker is not lying.

Durable: `publish-durable-pack.ps1` then `-Verify` → "Durable cache matches
the workspace selection (21 pack bundle(s))" before the swap. All 79 batch
packs already existed in both roots (one digest dir each). After the swap
both `selection.json` files hash to `04229f76…`.

Every Godot runner loaded the workspace pack, not a stale durable fallback:
baseline `…/rotwk-men-vslice/3be646b0…`, after `…/rotwk-men-vslice/4f92c8a4…`.
`[ContentDB] playable` went 137→136 units; `[ContentDB] legacy-demo: packs`
went 23→102.

## Baseline vs after

| runner | baseline | after | floor / expect | STOP? |
|---|---|---|---|---|
| `check_pack_addresses.py` | PASS packs=42 roots=2 | PASS packs=200 roots=2 | — | no |
| `retail_spellbook_runner` | 218/0 in 16.0s | 218/0 in 45.3s | 218/0 | no |
| `retail_pack_runner` | 43/135 in 11.1s | 43/135 in 18.7s | floor 175 (Section A, BFME2 pack) | no |
| `goal_production_matrix_runner` | 300/0 in 17.2s | 299/0 in 31.7s | floor 315 | no (named loss, see below) |
| `retail_slice_runner` | 370/59; ACCEPTANCE FAIL min=374 known=31; process then 0xC0000005 | 369/59; ACCEPTANCE FAIL min=374 known=31 | ACCEPTANCE_MIN_PASSED 374 / 31 known | no (named loss, see below) |
| fortress men | 113/1 in 22.4s | 113/1 in 38.2s | floor 49 failed=0 | no |
| fortress angmar | 93/2 in 41.4s | 93/2 in 65.6s | floor 56 failed=0 | no |
| `boot_startup_runner` | 44 checks / 0 fail / **29.7s** | 44 checks / 3 fail / **46.2s** | wall-clock 2× = 59.4s | no (1.56×) |

Pack runner 43/135 is the current RotWK-men **selection** baseline, not the
Section-A BFME2-men pack the gate pins at 175. Same 135 named failures before
and after.

## STOP conditions

None fired.

1. **Lost previously-passing checks.** Two named losses, both the same
   legitimate content change in the new Men pack (kimi residual:
   `GondorKnightsofDol` / `GondorKnightsofDolHorde`, AutoHealBehavior
   `HealOnlyIfNotUnderAttack`):
   - production-matrix lost `unit_present_GondorKnightsofDolHorde`
   - slice lost `wrong_producer_rejects_gondor_knightsof_dol_horde`
2. **Boot wall-clock.** 46.2s / 29.7s = 1.56×, under 2×. The three after
   failures are the runner's own millisecond budgets (`first_frame` 8908>7000,
   `shell_instantiated` 2057>1800, `shell_visible` 15838>12000) from loading
   102 packs instead of 23. That is the Q1 content change, not a 2× wall-clock
   STOP.
3. **Mounting / duplicate-address / precedence errors from the batch packs.**
   None in any after log.

`retail_state_pin` was already red (Q5). This lane did not run it and did not
touch its hash.

## Re-pin

`tools/gate-retail.ps1` and `importer/tests/test_retail_gate_script.py`:
RE-MEASURED 2026-08-17 after Q1/Q2 activation (men 4f92…/angmar 48b8…/79
missing-physical batches).

- `$expectedSelectionSha256` = `04229f763fd6f41d3637d6747a73fe5a2b8e198dd6b3325907de90e1b08fa41e`
- `$expectedSelectionActivePack` = `rotwk-men-vslice/4f92c8a4…`
- `$expectedSelectionSupplementalPacks` = 99 entries (20 prior supplements
  with angmar replaced, plus the 79 batches)

`pytest importer/tests/test_retail_gate_script.py` (pinned interpreter):
**12 passed in 0.13s**.

Full `tools/gate-retail.ps1` was not run: Section A rebuilds the BFME2 proof
pack and will not finish inside ~60 min. Section B numbers above are the
selection-scoped checkpoint (spellbook / production / slice / fortress men +
angmar). Pre-existing reds vs named floors, unchanged except the two
GondorKnightsofDolHorde losses:

- production-matrix 300 then 299 vs floor 315 (already under floor)
- slice ACCEPTANCE FAIL 370 then 369 vs min 374
- fortress men 113/1 vs floor 49 failed=0 (same 1 fail:
  `men_precompiled_page_selector_fallback_is_named`)
- fortress angmar 93/2 vs floor 56 failed=0 (same 2 fails:
  `angmar_the_second_fortress_finishes_construction`,
  `runner_ran_every_section`)
- pack 43/135 vs floor 175 (same; this invocation is selection-scoped)

## Logs

All under `workspace/logs/`: `q1q2-baseline-*`, `q1q2-after-*`,
`q1q2-publish-existing.txt`, `q1q2-publish-durable.txt`,
`q1q2-publish-durable-verify.txt`, `q1q2-apply-selection.txt`,
`q1q2-after-pack-addresses.txt`.

## Left undone

- Full `gate-retail.ps1` Section A (importer suite + BFME2 cook) not run.
- `retail_state_pin` still red (Q5).
- Men residual `GondorKnightsofDol(+Horde)` still a converter gap (Q4 / timers
  lane, not this one).
- Production-matrix still under its 315 floor; slice still under
  ACCEPTANCE_MIN_PASSED 374.
