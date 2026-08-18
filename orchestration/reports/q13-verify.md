# Q13 verification — fresh-context adversarial verifier + Opus review

Date: 2026-08-17
Verifier: opus-q13-verify (read-only except this file and `workspace/logs/q13verify-*`)
Under review: `d47d1f6..fabb94e` (8 commits) + report `2304a12`
Brief: `orchestration/briefs/projectile-splash-pipeline-sol.md`
Implementor report: `orchestration/reports/projectile-splash-pipeline.md`

**Verdict: FIX-FIRST. Q13 must not close yet.**
Every number the implementor reported reproduces exactly. The lane's own DoD is now
materially proven except item 3, which is *environmentally* unprovable (see below).
The blocking items are review findings, not measurement failures: the importer change
is **not** additive, and one sim line is a latent regression that arms itself at the
Q14 recook.

Selected content for every Godot measurement: `workspace/content-packs/selection.json`,
active pack `rotwk-men-vslice/4f92c8a486861100c29f20d1287f01990bc835a2622c53e911cfd2fb024a147e`.

---

## Part A — DoD re-proved

### 1. FULL importer suite — PASS (zero new failures)

`run_importer_tests.bat` with the pinned interpreter and
`BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`.
Log: `workspace/logs/q13-verify-importer-full.txt`.

```
= 6 failed, 3734 passed, 17 skipped, 2 warnings, 975 subtests passed in 3151.85s (0:52:31) =
```

Failure-by-failure against the Q6 baseline (`orchestration/queue.md` Q6) — all six, no others:

| Failure | In Q6 baseline |
|---|---|
| `test_playable_unit_import.py::test_effective_bfme2_neutral_recipe_blockers_are_closed` | yes |
| `test_rotwk_official_map_corpus.py::…test_official_corpus_is_72_maps_split_22_skirmish_50_wotr` | yes |
| `test_scripts_converter.py::…test_effective_official_skirmish_inheritance_teams_are_attested` | yes |
| `test_special_disguise_prerequisite.py::test_current_selected_rotwk_eowyn_pack_fails_new_prerequisite_closure` | yes |
| `test_w3d_chunk_backlog.py::…test_committed_census_dates_itself_and_is_current` | yes (x2) |
| `test_w3d_chunk_backlog.py::…test_committed_census_decode_corpus_figures_are_dated_fresh` | yes (x2) |

0 errors. **Zero new failures = PASS.** The implementor could not finish this run; it
completes fine on this machine in 52 minutes with no parallelism (`run_importer_tests.bat`
has no `-n`/xdist flag — process exhaustion was not caused by test parallelism).
Caveat: the wrapper exits 1 because pytest returns non-zero on the six known failures, so
the trailing `doctor`/`plan` smoke checks in the .bat did not execute.

### 2. Runners — every implementor number reproduced exactly

Run sequentially, one Godot process at a time, `OPENBFME_CONTENT=workspace\content-packs`.
Logs `workspace/logs/q13verify-<runner>.txt`.

| Runner | Claimed | Measured | Verdict |
|---|---|---|---|
| `retail_member_combat_runner` | 111/0 | `RETAIL_MEMBER_COMBAT_RESULT passed=111 failed=0` | PASS |
| `weapon_cycle_model_conditions_runtime_runner` | 23/0 | `passed=23 failed=0` | PASS |
| `warhead_weapon_toggle_runtime_runner` | 35/0 | `passed=35 failed=0` | PASS |
| `projectile_table_runtime_runner` | 4/0 | `passed=4 failed=0` | PASS |
| `retail_archer_projectile_presentation_runner` | 34/0 | `passed=34 failed=0` | PASS |
| `retail_spellbook_runner` | 218/0 | `passed=218 failed=0` | PASS |
| `bezier_projectile_runtime_runner` | 23/0 | `passed=23 failed=0` | PASS |
| `retail_lockstep_determinism_runner` | 5/0 | `RETAIL_LOCKSTEP_RESULT passed=5 failed=0` | PASS |
| `retail_lockstep_network_runner` | 37/0 | `RETAIL_LOCKSTEP_NET_RESULT passed=37 failed=0` | PASS |
| `retail_state_pin_runner` | UNCHANGED `0e4bcdbf…` | `hash=0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc` + `OK` | PASS |

### The state-pin statement (asked for explicitly)

**The pin did not move, and the implementor's explanation of why is wrong.**

The frozen fixture is synthetic. `retail_state_pin_runner.gd:296-325` builds the sim from
`_harness_rules()` — five hand-written unit rules (soldier, archer, tower guard, knight,
builder) — and never loads a compiled pack. Therefore:

- **No hero cleave to lose.** `_apply_hero_cleave` returned immediately unless
  `row["category"] == "hero"`; no fixture unit is a hero. Deleting it cannot move this hash.
- **No projectile to fire.** The fixture rules carry no `projectile_object_id` /
  `projectile_speed`, so `_member_weapon_has_projectile()` is false for every attack and
  the instant path runs unchanged.

The report says the hash held "because the currently selected packs predate this compiler
change". That is false and it matters: the **shipped** Men pack already carries
`combat.projectileObjectId = "GondorArcherArrow"` and `combat.projectileSpeed.value = 321`
(`…/rotwk-men-vslice/4f92c8a4…/data/playable-units/gondorarcherhorde.json`). Archers in a
real match on today's selection now fly arrows with flight time. The unmoved pin is
evidence about a synthetic fixture only — **it is not evidence that live combat is unchanged.**
No pin was re-minted; correct per the brief.

### 3. `tools\gate-m2-focused.ps1` — CANNOT RUN (blocked by Q1, not by Q13)

```
M2_FOCUSED_GATE FAIL Selection does not name an immutable Men/Fords bundle.
```

`tools/m2-oracle-common.ps1:36` requires `selection.activePack` to match
`^bfme2-men-vslice/[0-9a-f]{64}$`. Q1's activation (closed 2026-08-17, before Q13) swapped
the active pack to `rotwk-men-vslice/4f92c8a4…`, so the gate throws in
`Get-M2OracleContext` before executing a single runner. Q13 touched neither
`selection.json` nor the oracle script. Log: `workspace/logs/q13verify-gate-m2-focused.txt`.
**This deserves its own queue row: the focused gate has been dead since Q1.**

Because a pin-by-pin answer was requested, I emulated the gate: same runner/marker table
parsed out of `gate-m2-focused.ps1`, same `$forbiddenDiagnostics` regex, with
`OPENBFME_TERRAIN_PACK/ROAD_PACK/TEST_PACK` pointed at the pack the gate was written
around (`bfme2-men-vslice/7de517bf…`). Logs `workspace/logs/q13verify-gate-emulation.txt`,
`…-emulation2.txt`, per-step `…-gate-<step>.txt` / `…-gate2-<step>.txt`.

Result: **13 of 28 steps PASS.** Of the 15 non-PASS, exactly two are Q13-owned and both
match their marker:

| Step | Verdict | Reading |
|---|---|---|
| `member_combat_runtime` | FAIL-DIAGNOSTICS, marker `passed=111 failed=0` MATCHES | New pin is correct. Fails only on the pre-existing Q11 `ERROR: [RetailSliceSim] structure armor: kind 'farm'/'barracks'/… has no compiled armor contract` lines — 103 of them are already in the pre-lane log `workspace/logs/q13-before-retail_member_combat_runner.txt` |
| `projectile_table_runtime` (new step Q13 added) | FAIL-DIAGNOSTICS, marker `passed=4 failed=0` MATCHES | Same Q11 ERROR lines. **Q13 added a gate step that cannot pass the gate's own regex.** |

The other 13 are pre-existing rot/regressions in surfaces Q13 does not touch:
`environment_runtime` 37 vs pin 33, `four_unit_audio` 163 vs 159, `four_unit_hud` 99 vs 134,
`hud_apt_runtime` 99 vs 67, `production_queue_runtime` 24 vs 16,
`member_health_overlay_runtime` 48/4 vs 68, `structure_lifecycle_runtime` 126/10 vs 136,
`full_terrain_runtime` 1/2 vs 29, plus `builder_construction`/`hero_mounted_*`
FAIL-DIAGNOSTICS. I did not re-run these at `d47d1f6`, so "pre-existing" here rests on
(a) they are unrelated surfaces, (b) Q11 documents the ERROR class, (c) the pre-lane
member-combat log shows the same diagnostics.

**On the pin comment.** Your brief said the implementor changed a rotten 13→40 pin. That is
a misattribution: the `13 → 40 → 68` comment block belongs to `member_health_overlay_runtime`
and predates this lane (`git diff d47d1f6..fabb94e -- tools/gate-m2-focused.ps1` touches only
two lines). What Q13 actually did was `member_combat_runtime` **49 → 111**. That pin *was*
rotten: the pre-lane runner emitted **98** against a pinned 49. Q13's real delta is 98 → 111
(+13 new checks). The added comment ("Re-measured 2026-08-17 after Q13 added…") reads as if
the whole 49→111 move is Q13's; it is not, and the pre-existing 49-vs-98 rot is not called out.

### 4. `retail_slice_runner` — red, but ZERO delta attributable to Q13

Fresh run: `workspace/logs/q13verify-retail_slice_runner.txt`

```
RETAIL_SLICE_RESULT passed=369 failed=59
RETAIL_SLICE_ACCEPTANCE FAIL min_passed=374 pinned_known_failures=31
```

Failure-NAME diff (87 `RETAIL_SLICE FAIL <name>` lines each side):

- pre-Q13 `workspace/logs/q1q2-after-slice.txt` vs post-Q13 `workspace/logs/q13-dod-retail_slice_runner.txt`: **new = 0, fixed = 0**
- pre-Q13 `q1q2-after-slice.txt` vs **my fresh run**: **new = 0, fixed = 0**

Both sides also report the identical 369/59. **Pre-existing (Q7-adjacent), not a Q13
regression.** Q13 correctly did not re-pin the ratchet.

### 5. Hygiene / addresses / tree

| Check | Result |
|---|---|
| `python tools\check_pack_addresses.py` | `PACK_ADDRESS_CHECK PASS packs=200 roots=2` — PASS |
| `tools\gate-hygiene.ps1` | `HYGIENE_GATE PASS root-files=25 tracked=2591` — PASS |
| `git status --porcelain` | empty at review start. The two `.codex-*` root files the implementor reported are gone. This report file makes it non-empty again, as expected. |

The implementor also appended a "House rules" block to its own brief and added the Q13 queue
row; both are append-only and weaken no DoD clause.

---

## Part B — adversarial code review

### Verified correct

- **(a) EMPTY-IS-ABSENT is symmetric.** `_authoritative_state()` (`retail_slice_sim.gd:31718`)
  emits `projectiles`/`next_projectile_id` only when `not projectiles.is_empty() or
  _next_projectile_id != 70000`; `_restore_authoritative_state()` (`:31934`) reads them with
  exactly those defaults. `weapon_cycle_model_conditions_runtime_runner` pins a no-fire hash
  and asserts both keys absent. Once any projectile has ever launched the allocator keeps the
  keys present — correct, that is real divergent state.
- **(b) Determinism.** No RNG, no `Time.`/`OS.` reads, no unsorted iteration in the new code.
  `_step_projectiles` sorts `projectiles.keys()`; `_apply_radius_damage` uses
  `_spatial_gather_sorted` and a sorted `structure_ids()`. Flight time is integer
  (`maxi(1, ceili(distance / speed / TICK_SECONDS))`). Both lockstep runners green.
  Speed and radius are both multiplied by `source_scale`, so flight time is scale-invariant.
- **(c) Melee is byte-identical.** Sim: non-projectile weapons take the original
  `_apply_member_damage` + bonus-nugget loop, only refactored into
  `_apply_member_bonus_nuggets` — the sole behavioural deletion is `_apply_hero_cleave`
  (intended; no reference survives anywhere in `game/`, `tools/`, `docs/`). Importer: A/B
  compile of `GondorFighter` (melee, `Radius = 0`) is **IDENTICAL** old vs new.
- **(d) `_apply_radius_damage` is shaped right.** Excludes `exclude_target_id` (the direct
  target) on both the battalion and structure passes; re-tests exact distance after the
  spatial gather; goes through `_apply_member_damage` / `_apply_structure_damage` (armor,
  DamageScalar, flanking) and **never** `_apply_area_damage_to_battalion`; honours
  ENEMIES/ALLIES/NEUTRALS via `_radius_relation_allowed` over `_is_hostile` /
  `_is_combatant_team`; taper is `1 - taper/100 * d/r`, i.e. `(100-taper)%` at the edge,
  which is the interpretation the brief named.
- **(e) Mid-flight target death** retargets to the first live member, else emits
  `combat.projectile_cancelled` and erases the row. Covered by two runner checks.
- **(g) No double arrows.** `retail_vertical_slice.gd:8192` skips launches whose
  `projectile_object_id` is in `BattalionScript.VISIBLE_ARROW_PROJECTILE_IDS`, and that set
  (`GondorArcherArrow`, `GoodFactionArrow`, `EvilFactionArrow`) is *exactly* the guard
  `RetailBattalion` itself uses at `:1963` and `:2504`. Verified, claim holds.

### FIX-FIRST (blocking)

**F1 — the importer change is NOT additive; it rewrites `combat.damage` and doubles the
trebuchet.** Method: compiled 12 units twice against the same BFME2 catalog, once with
`importer/` at HEAD and once with `playable_unit_compiler.py` + `playable_structure_compiler.py`
restored from `d47d1f6` into a temp tree; diffed `resolved.combat`.

- `_simulation_contract` now takes `combat["damage"]` from `_base_weapon_damage(warhead)`
  instead of the warhead's flat `Damage` row. For every warhead-carrying weapon the `damage`
  **document shape changes**: old `{value, expression, sourceIni, line, constantSourceIni}`
  → new `{value, components[], semantic}`. The provenance keys `expression`/`sourceIni`/`line`
  are **removed** from the top level (they move inside `components[0]`). That is a field
  removal, not an addition.
- `GondorTrebuchet` `combat.damage.value` moves **390 → 780**. The old compiler recognised
  the two identical `GONDOR_TREBUCHET_DAMAGE` nuggets as one value with `equivalentSources`;
  the new one sums them. 780 is arguably retail-correct for a dead-centre hit (both nuggets
  cover distance 0) — but it is a 2× change to a shipped balance number that the report never
  mentions, and the runtime reads exactly this `.value` as `member_damage`.
- Archer/Ranger/Uruk-Crossbow `.value` are unchanged (25/65/65); `damageType` was not
  clobbered in any sampled unit.

Nothing in `contracts/` or any test pins these fields, so no gate would have caught it.
Needed before Q14: an owner ruling on 390-vs-780 and a test that pins it.

**F2 — `damageTypeSemantic` is now emitted with a false statement.**
`_apply_nugget_damage_types(combat)` is now called *unconditionally* in three places
(`playable_unit_compiler.py` `_simulation_contract` and `_weapon_mode_profile`,
`playable_structure_compiler.py` `_structure_combat_contract`), where before it ran only when
no flat DamageType existed. Empirically it now adds
`damageTypeSemantic = "the weapon authors no flat DamageType; every base DamageNugget authors
the same type"` to `GondorArcher`, `GondorRanger`, `IsengardUrukCrossbow`, `GondorTrebuchet` —
all of which *do* have a flat block-level DamageType, which the very next line above already
consumed. It also re-assigns `combat["damageType"]` from the nuggets, so a weapon whose block
DamageType differs from its nugget DamageType would be silently overwritten (no such case
appeared in my 12-unit sample, but nothing prevents it).

**F3 — `_apply_weapon_mode` wipes the multi-nugget damage mix (latent regression, arms at Q14).**
`retail_slice_sim.gd:27458`:

```gdscript
	if not selected.has("damage_components"):
		row["damage_components"] = []
```

`_apply_weapon_mode` is called from `:26412` on **every attack step** (distance-based mode
selection) and has no early-out when the mode is unchanged. Before Q13, `damage_components`
was set once in `_add_battalion` and never cleared. So any unit with weapon modes whose
selected mode profile carries no `damageComponents` now loses its authored per-nugget armor
mix on the first attack tick, and damage falls back to the single flat `damage_type` column —
the exact defect the Arwen HERO+SLASH work was done to fix.

Inert **today**: I grepped every selected faction pack (men/elves/dwarves/isengard/mordor/wild/
angmar, both bfme2 and rotwk vslices) — **zero** playable-unit documents carry
`damageComponents`, so every row's `damage_components` is already `[]`. It arms the moment
Q14 recooks, because F1/F2 are precisely what start emitting that field. Needs a failing-first
test now.

**F4 — Q13 added a focused-gate step that cannot pass the gate.**
`projectile_table_runtime` matches its marker but trips `$forbiddenDiagnostics` on the Q11
structure-armor `ERROR:` lines (`sim.setup({})` emits them). Either land Q11 first or do not
add the step. Same story for the `member_combat_runtime` step, but that one was already in
the table.

### Non-blocking findings

1. **Splash lands at stale coordinates.** `origin` is the target's position captured at
   *launch*; the direct hit homes to wherever the target is at impact. A moving target takes
   full direct damage while its neighbours are splashed around where it used to be.
2. **A dead target cancels the whole projectile**, so a trebuchet rock whose target dies
   mid-flight deals *no* splash at all. Retail detonates regardless. The report states the
   cancel rule but not this consequence.
3. **Untested surfaces of the new code**: the `structure_ids()` splash branch (structures
   taking radius damage) has zero coverage, and `RadiusDamageAffects` is only tested in its
   negative form ("ENEMIES spares allies") — the ALLIES and NEUTRALS positive paths are
   unexercised. So is the claim that non-arrow projectiles resolve a compiled visual: no
   runner asserts a trebuchet rock ever gets a node.
4. **Attacker death asymmetry**: `_apply_radius_damage` returns early when
   `not entities.has(attacker_id)`, so a projectile from a dead attacker lands its direct hit
   but silently drops its splash.
5. Splash calls `_apply_member_damage(..., attack_sequence = 0)`; the cleave it replaces
   passed the attacker row's real `attack_sequence`.
6. Two damage-component paths disagree on the default source scale:
   `retail_slice_sim.gd:3390` uses `_rules.get("source_map_transform_scale", 1.0)` while the
   adapter call at `:3333` uses `0.0`. Harmless while the key is always present; a trap if it
   ever is not.
7. `_add_battalion` now prefers `unit_rule["damage_components"]` (adapter-built, key order
   `value, damage_type, …`) over `_unit_damage_components` (sim-built, key order
   `damage_type, value, …`). `state_hash()` is `var_to_bytes` over a canonicalised dict, so if
   canonicalisation does not sort keys this silently moves every real-content hash once packs
   carry components. Untested either way today (no pack has them).
8. Perf: each radius-bearing nugget walks and sorts the whole `structure_ids()` list per
   impact. Fine for one trebuchet, worth watching for mass siege.
9. Report accuracy nits beyond the pin explanation: the runner table's "before" column says
   "not measured" for four runners that the lane could trivially have measured, and the gate
   pin comment does not disclose the pre-existing 49-vs-98 rot.

---

## Can Q13 close?

**No.** Close it when:

1. F1 gets an owner ruling (390 → 780) plus a pinning test, and F2's false semantic is fixed —
   both must land *before* Q14 recooks, since that is when the compiler output reaches players.
2. F3 ships a failing-first test and a fix (or an explicit owner-accepted note that the
   wipe is intended).
3. F4 is resolved — either Q11 lands or the new gate step comes back out.
4. A new queue row records that `tools/gate-m2-focused.ps1` is dead until its
   `bfme2-men-vslice` activePack assertion is reconciled with Q1's `rotwk-men-vslice` swap.
5. The Q13 row and report are corrected on *why* the state pin did not move (synthetic
   fixture, not stale packs) and on the fact that archers on the current selection already
   take the new projectile path.

Items 1/2/4/5 of the DoD are proven. Item 3 is unprovable in this environment for reasons
that predate the lane. `retail_slice_runner` stays red with a measured zero delta.

## Commands to re-run any claim here

```
run_importer_tests.bat                                            (BFME2_INSTALL as above)
<godot> --headless --audio-driver WASAPI --path game --script res://tests/<runner>.gd
powershell -ExecutionPolicy Bypass -File tools\gate-m2-focused.ps1 -GodotPath <godot>
python tools\check_pack_addresses.py
powershell -ExecutionPolicy Bypass -File tools\gate-hygiene.ps1
```

---

# Round 2 — re-verification of the Q13 fix lane

Date: 2026-08-17
Under review: `1480f77..e1b3f70` (`d6e3acb`, `f000569`, `abcaf99`, `e1b3f70`)
Fix report: `orchestration/reports/q13-fixes.md`; brief `orchestration/briefs/q13-fixes-grok.md`
Owner ruling accepted upstream: `combat.damage.value` = sum of every warhead DamageNugget at
zero distance; GondorTrebuchet **780 stays**, now pinned and labelled.

**Verdict: ACCEPT. Q13 can close.** All four blocking findings are genuinely fixed, each with
evidence I reproduced myself rather than read from the fix lane's report.

## F1 — provenance restored, 780 pinned — FIXED (one cosmetic residual)

Re-ran my own A/B compile (12 units, same BFME2 catalog, HEAD `importer/` vs `d47d1f6`
compilers in a temp tree). Raw: `%TEMP%\q13ab\r2new.json` vs `old.json`.

| Unit | top-level keys removed | `damage` keys removed | `damage.value` |
|---|---|---|---|
| `GondorFighter` (melee) | — | — | **combat block IDENTICAL to `d47d1f6`** |
| `GondorArcher` | none | none | 25 → 25 |
| `GondorRanger` | none | none | 65 → 65 |
| `IsengardUrukCrossbow` | none | none | 65 → 65 |
| `GondorTrebuchet` | none | `equivalentSources` | 390 → **780** |

`expression`, `sourceIni`, `line`, `constantSourceIni` are back at the top of `damage` with
their original values on every warhead weapon; the additions are `components`, `semantic`,
`valueSemantic = "sum-of-nugget-damage-at-zero-distance"`. Round 1's "provenance keys removed"
defect is gone.

Residual, **non-blocking**: the trebuchet's `damage.equivalentSources`
(`[{line 3248}, {line 3259}]`) is still dropped. No information is lost — `components[]` now
carries both nuggets with their own `line` 3248 / 3259, which is strictly richer than the old
dedup marker. Worth one line in a follow-up row, not a re-open.

## F2 — `damageTypeSemantic` truthful — FIXED

`_authored_flat_damage_type` excludes DamageType rows whose `line` belongs to a DamageNugget
block (`_named_definition_values` is a flat line scan, which is what produced the false
"flat type" reading), and `_apply_nugget_damage_types(..., flat_damage_type=)` now names the
real source and **never re-assigns a disagreeing flat type**. Three call sites updated
consistently (unit `_simulation_contract`, `_weapon_mode_profile`, structure
`_structure_combat_contract`).

Confirmed on real retail data by my A/B: every sampled weapon authors DamageType only inside
nuggets, so the emitted string "the weapon authors no flat DamageType; every base DamageNugget
authors the same type" is now literally true, and `damageType` is unchanged in every case
(PIERCE/PIERCE, SIEGE/SIEGE). Both branches are pinned by new pytests
(`test_flat_damage_type_semantic_names_the_flat_source` asserts `"no flat DamageType" not in`
the string; `test_nugget_only_damage_type_semantic_names_the_nugget_source` asserts it is).

## F3 — weapon-mode wipe removed, with a real failing-first proof — FIXED

`retail_slice_sim.gd` `_apply_weapon_mode` no longer blanks `damage_components`; the mode still
overwrites them when it compiles its own mix.

I did not take the new checks on trust. I copied `game/` out of tree to
`%TEMP%\q13ab\game-old`, replaced **only** `src/retail_slice/retail_slice_sim.gd` with
`git show fabb94e:…` (the one-hunk difference), and ran the **round-2 runner** against it:

```
RETAIL_MEMBER_COMBAT FAIL damage_components_survive_attack_ticks ([])
RETAIL_MEMBER_COMBAT FAIL damage_components_survive_weapon_mode_toggle (close [])
RETAIL_MEMBER_COMBAT_RESULT passed=113 failed=2
```

Log: `workspace/logs/q13v2-failing-first-on-fabb94e.txt`. The `([])` is the wipe itself. At
HEAD the same runner is 115/0. That is a genuine failing-first test for F3, not a green
assertion.

Behavioural note on the new semantics: a mode without its own components now inherits the
unit-rule mix, which is exactly pre-Q13 behaviour; a mode *with* components overrides it, which
is new but unreachable until packs carry mode-level `damageComponents`. Projectile fields are
still erased on a mode that lacks them, so no phantom splash can leak across a mode switch.

## F4 — the gate step Q13 added now passes — FIXED

`projectile_table_runtime_runner` seeds `structure_kinds: ["fortress"]`, so the Q11
structure-armor `ERROR:` lines are no longer emitted. Emulating the gate's own check against my
fresh run:

```
member_combat_runtime    markerMatches=True  noForbiddenDiagnostics=False
projectile_table_runtime markerMatches=True  noForbiddenDiagnostics=True
```

Zero forbidden diagnostics in the projectile-table log (grep count 0). The step Q13 introduced
would now pass. `member_combat_runtime` still trips the regex on the same Q11 armor errors —
that step and that error class both predate Q13 (123 such lines at HEAD, 103 in the pre-lane
log), so it is not this lane's defect, but it does mean the focused gate cannot go green on
Q11 alone even after Q15 revives it.

## Runners and suite — reproduced independently

| Item | Claimed | I measured | Log |
|---|---|---|---|
| `retail_member_combat_runner` | 115/0 | `passed=115 failed=0` | `workspace/logs/q13v2-retail_member_combat_runner.txt` |
| `projectile_table_runtime_runner` | 4/0 | `passed=4 failed=0` | `workspace/logs/q13v2-projectile_table_runtime_runner.txt` |
| `retail_state_pin_runner` | UNCHANGED | `hash=0e4bcdbf…` + `OK` | `workspace/logs/q13v2-retail_state_pin_runner.txt` |
| changed-module pytest | 318 passed | `318 passed in 276.76s` | `workspace/logs/q13v2-importer-targeted.txt` |

Full importer suite: I decoded the lane's UTF-16 log `workspace/logs/q13fix-importer-full.txt`
and checked it rather than burning a second 50-minute run —
`= 6 failed, 3737 passed, 17 skipped, 2 warnings, 975 subtests passed in 2974.11s =`, 0 errors,
and the six `FAILED` names are exactly the Q6 set. 3734 → 3737 is accounted for by the three
tests this lane added. My own independent 318-test run of the two changed compiler modules
covers the code that actually moved.

Queue: Q15 (focused gate dead since Q1, DECISION) and Q16 (state pin covers no projectile
combat) are recorded with the right evidence. `git status --porcelain` was empty at the start
of round 2; it is dirty now only because of this report.

## Still open (recorded, not blocking the close)

Round 1's non-blocking findings were out of scope for the fix brief and remain true. They
deserve one queue row rather than a re-open:

1. Splash uses the target's position captured at launch while the direct hit homes to the
   target's current position.
2. A target that dies mid-flight cancels the projectile entirely, so the rock deals no splash.
3. No test covers the structure branch of `_apply_radius_damage`, nor the ALLIES / NEUTRALS
   positive paths of `RadiusDamageAffects`, nor that a non-arrow projectile ever resolves a
   visual.
4. A projectile whose attacker died lands its direct hit but silently drops its splash.
5. Splash passes `attack_sequence = 0`.
6. `source_map_transform_scale` defaults disagree between `retail_slice_sim.gd:3333` (`0.0`)
   and `:3390` (`1.0`).
7. `damage.equivalentSources` dropped for warhead weapons (F1 residual above).
