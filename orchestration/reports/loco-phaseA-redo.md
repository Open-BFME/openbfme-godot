# Locomotion Phase A — REDO

Date: 2026-08-18. Isolated worktree lane.
Spec: `orchestration/reports/loco-phaseA-verify.md` (10-item FIX-FIRST list),
plus `orchestration/briefs/loco-phaseA-locomotor-table-sol.md`.
Prior attempt `06d4f89`/`ea93908` was REJECTED and reverted by `8156449`/`ee9a222`.

Retail oracle for every number below: `workspace/retail-extract/data/ini` — the
BFME2 1.06 extraction whose line anchors match the brief (HumanLocomotor :142,
HorseLocomotor :1026, CatapultLocomotor :1683, FellBeastLocomotor :2334,
WhirlpoolLocomotor :3167, 123 templates in locomotor.ini).

## The ten FIX-FIRST items

### 1. `return <value>` in a void function — DONE

`_step_route` is `-> void`; both refusal paths now use a bare `return`.
Godot `--check-only --script` is clean on every `.gd` this lane touched, re-run
after the last edit:

| Script | `--check-only` |
|---|---|
| `game/src/retail_slice/retail_slice_sim.gd` | clean (banner only) |
| `game/src/retail_slice/playable_unit_runtime_adapter.gd` | clean |
| `game/tests/retail_slice_runner.gd` | clean |
| `game/tests/retail_authored_movement_runner.gd` | clean |
| `game/tests/retail_formation_movement_runner.gd` | clean |
| `game/src/retail_slice/retail_vertical_slice.gd` | only `Compile Error: Identifier not found: ContentDB`, an autoload that `--check-only --script` does not register. Proof it is an artefact: `res://tests/retail_slice_runner.gd` preloads the same chain, checks clean, and the full runner boots the slice and completes 429 checks. |

### 2. `retail_slice_runner.gd:3923` — DONE

Now compares the authored value directly, with no scale factor.

### 3. Percent fields refused bare numbers — DONE

SAGE's `INI::parsePercentToReal` has `%` in its separator set, so `0%` and a bare
`0` parse identically (`scanReal(token) / 100`). The compiler accepts both and
records which was authored (`percentSignAuthored`). Covers all four percent
fields: minTurnSpeed, backingUpSpeed, lift, chargeSpeed.

A second over-strictness of the same class was found and fixed: the reverted
`_binding` required `Condition` on every `LocomotorSet`. `Condition` is optional
in SAGE and defaults to SET_NORMAL. Retail authors it on all 715 blocks under
data/ini/object, but three `test_hero_summon_leaf_closure` fixtures do not.
Absence is now the recorded default (`conditionAuthored: false`).

test_neutral_wildlife_noncombatant: 9 passed, 0 failed (was 4 failed).

### 4. `game/data/retail_module_census.json` regenerated, never hand-edited — DONE

Regenerated through the census module's own functions
(`module_census.scan_module_support` + `census_json_bytes`). Measured delta:
`consumerFiles` gained exactly `locomotor_compiler`, `consumerFileCount` +1, and
ZERO member status/consumedBy/refusedBy/refusalReasons changes. 2 insertions,
1 deletion.

The full CLI refuses on this host with `SKIP: observed module kinds lack an A-E
classification: GiantBirdSlowDeathBehavior, ReflectDamage` — PRE-EXISTING,
proven by running the test file in the untouched main checkout:

| Tree | Result |
|---|---|
| main checkout, no lane changes | 2 failed / 31 passed |
| this worktree, before regeneration | 3 failed / 30 passed (those two plus `test_committed_census_statuses_match_current_importer_source`) |
| this worktree, after regeneration | 2 failed / 31 passed — exactly main's two names |

The retail-corpus half of the census (trees, declarationSites) was not touched.

### 5. `test_bfme2_neutral_visual_recipe_oracle[CINE_GrnDrgn_Flying]` — DONE, mechanism isolated

Root cause, measured. Retail does NOT keep the whole Locomotor table in
`data/ini/locomotor.ini`. `data/ini/object/cinematic/cinematiclocomotor.ini`
authors five more: CINE_EgretLocomotor, CINE_CrowLocomotor,
CINE_CrowLocomotor_Med, CINE_CrowLocomotor_Low, Cine_DragonLocomotor.

`CINE_GrnDrgn_Flying` binds `Cine_DragonLocomotor`
(object/cinematic/cinematicobjects.ini:28772). The reverted compiler opened only
locomotor.ini, so the template vanished, `movement` came back empty,
`_simulation_contract` never set `resolved["movement"]`, and
`playable_unit_pack_compiler.py:1870` — which creates the `move`
`transform-locomotion` presentation only when
`isinstance(resolved_simulation.get("movement"), Mapping)` — stopped creating it.
The `move` state then had no clip (its only animation WUDrogoth_FLYS is
retail-absent) and closure refused at :1921.

Fix: `compile_locomotor_templates` scans every .ini/.inc document. Header regex
is `^\s*Locomotor\s+(?!=)(\S+)\s*$`, so a `Locomotor = X` assignment inside a
LocomotorSet can never be read as a template header — verified zero false
positives by scanning all three oracle trees.

Failing-first guard shipped:
`test_every_referenced_locomotor_resolves_to_a_compiled_template`. With a
locomotor.ini-only reader five references dangle; with the fix, zero.

test_bfme2_neutral_visual_recipe_oracle: 10 passed, 0 failed.

### 6. `RETAIL_CAVALRY_MAX_TURN_WITHOUT_REFORM_DEGREES` — DELETED, coupling worked through

| Deleted | Was at |
|---|---|
| `RETAIL_FALLBACK_TURN_RATE_DEGREES := 180.0` | retail_slice_sim.gd:28455 |
| `RETAIL_MAX_TURN_WITHOUT_REFORM_DEGREES := 45.0` | :28459 |
| `RETAIL_CAVALRY_MAX_TURN_WITHOUT_REFORM_DEGREES := 100.0` | :28460 |
| orphaned RETAIL_FALLBACK_TURN_RATE_DEGREES docstring | :28451-28458 |
| stale comment "Pin fixtures invent 180 with no source" | :28470 |
| stale comment "Fall back to a snappy ramp (10x max speed)" | :28554 |

`_retail_reform_threshold_degrees` is now one line: the authored value or -1.0.

Retail authors MaxTurnWithoutReform on 12 of 128 templates (45 on six plus
AODHorde and the two WallScaling variants, 55 on SlowMeleeHordeLocomotor, 100 on
NormalCavalryHordeLocomotor / NormalSpiderlingHordeLocomotor /
WargCavalryHordeLocomotor). The importer compiles it; 126 of the 494 shipped
movement blocks carry it. For the other 116 templates retail has no reform gate
at all, so:

- `_should_reform` — unchanged: gates on an authored value being present.
- `_step_retail_heading` — NEW explicit branch, because -1.0 compared against
  `absf(delta_angle)` would otherwise mean ALWAYS reform, the exact opposite of
  "no gate". A row with no authored value now always wheels: it turns and keeps
  advancing, however sharp the turn.
- `_should_honor_turn_rate` — deliberately LEFT AS IT WAS. The verifier feared
  the 30 named cavalry rows would silently stop honouring their turn rate. They
  do not: the adapter stamps `turn_rate_source = "locomotor"` on every normalized
  row whose locomotor compiled a rate (playable_unit_runtime_adapter.gd:1044),
  which is the guard's second clause. Changing the guard WOULD have moved the
  owner-signed 3000-tick state pin, because synthetic pin fixtures invent 180
  with no source and must keep snapping. The reform GATE disappears; turn-rate
  honouring does not. Now written into the function's docstring.

Proof over the shipped roster: `retail_authored_movement_runner` check
`reform_threshold_is_authored_or_absent` asserts, for every spawned row, that the
threshold equals the authored value or is exactly the "no gate" -1.0 — never a
constant. Fixture proof: new
`retail_formation_movement_runner._test_unauthored_reform_threshold_never_reforms`
drives a 170-degree turn on a row with no authored field and asserts it still
advances. The two fixtures that had relied on the deleted constants now carry the
AUTHORED 45 / 100 on the row, exactly as the shipped packs do.

### 7. All six readers through the one canonical compiler — DONE

| Reader | Before | Now |
|---|---|---|
| playable_unit_compiler.py | `_named_definition_values("Locomotor", ...)` | canonical compiler |
| cah_system_compiler.py:1281 | own `parse_flat_named_blocks(raw, "Locomotor")` | canonical compiler |
| retail_unit_rules.py:535 | own `_one_named_block` + `_block_assignment` | canonical compiler locates the authored assignment; this module's `_number()` provenance shape is preserved byte-for-byte so hashed pack documents do not move |
| spellbook_compiler.py:1711 | own `_project_locomotor` expression scraper (~65 lines) | canonical compiler (~25 lines) |
| ring_system_compiler.py | read only the object's LocomotorSet Speed; never read the template | now reads HumanLocomotor through the canonical compiler for Gollum |
| m3_pack_expansion.py:1539 | read only the trebuchet's LocomotorSet Speed | now reads CatapultLocomotor through the canonical compiler |

`git grep -n "from .locomotor_compiler import"` returns all six modules.

Honest scope note: for ring_system_compiler and m3_pack_expansion what was
private was the TEMPLATE read — which simply did not exist, and that is exactly
why the sim had to invent those numbers. Their per-object LocomotorSet Speed
reads still use each module's generic block helpers over an already-parsed
lineage; those are not a locomotor parser, and swapping them for the
document-based binding compiler would be churn with no behaviour change.

`compile_object_locomotor_sets` now feeds real pack documents. Not test-only any
more: `_simulation_contract` emits `resolved["locomotorSets"]` on every unit
document, merged along the object's inheritance chain (`lineage_locomotor_sets`,
base first, derived overrides). That is the 711-binding table (SET_WANDER,
SET_PANIC, SET_MOUNTED, ...) travelling with the unit instead of living in a test.

### 8. The remaining invented movement values — DELETED

| Anchor (pre-change) | Was | Now |
|---|---|---|
| retail_slice_sim.gd:3156-3160 Gollum accel = braking = speed, turn 360 | invented | reads `parent["movement"]`; ring_system_compiler emits it from HumanLocomotor (neutralunits.ini:324 binds it; locomotor.ini:142 authors 510/510, TurnTime 500 -> 720 deg/s). Stale pack means a loud NAMED GAP and the rule is refused. |
| retail_slice_sim.gd:4159-4166 trebuchet accel = braking = speed, "Provisional" turn 360 | invented | reads `unit.movement`; m3_pack_expansion emits it from CatapultLocomotor. The 360 was not even right for this edition: RotWK 2.01 authors TurnTime = 3000 -> 120 deg/s, and the shipped doc-driven gondortrebuchet.json already carries 1000/1000/120. |
| retail_slice_sim.gd:7321 `.get("turnRateDegreesPerSecond", 360.0)` and `.get(..., speed)` | invented | `_spellbook_summon_rule` refuses the summon with a named push_error if any of the three is missing; all 126 shipped locomotor rows carry them. |
| retail_slice_sim.gd:7310 `response_scale` | 1.5x fudge | gone |
| playable_unit_runtime_adapter.gd:872 `HORDE_LOCOMOTION_RESPONSE_SCALE := 1.5` (applied :916-917) | fudge | deleted; authored accel/braking used unscaled |
| retail_vertical_slice.gd:1518-1522 builder accel = braking = 60.0, turn 360 | invented literals | reads `builder_simulation["movement"]`; loud NAMED GAP otherwise |
| retail_vertical_slice.gd:2793-2799 response scale on accel/braking | fudge | deleted |
| retail_slice_sim.gd:28550-28555 accel/braking = `max_speed * 10.0` | invented ramp | push_error("unauthored locomotor ...") + bare return |
| retail_slice_sim.gd:28496 fallback 180 turn rate | invented | push_error("unauthored locomotor turn rate for ...") |

The one shipped gap, NAMED: `bfme2-men-vslice/.../data/m3/trebuchet-runtime.json`
ships `movement = {mode, sourceTemplate: "CatapultLocomotor", speed: 37}` and no
response fields. Both halves of the brief's option were taken: the importer now
binds it from CatapultLocomotor (closes on the next recook) and until then the
runtime names the gap loudly and refuses to move that one unit rather than
keeping the fudge:

```
NAMED GAP: bfme2.object.gondor-trebuchet carries no authored acceleration,
braking, turnRateDegreesPerSecond; the M3 trebuchet contract predates the
CatapultLocomotor binding and the unit will not move until the pack is recooked
```

printerr, deliberately NOT push_warning: tools/gate-m2-focused.ps1 treats any
WARNING in a runner's output as a defect, and the pinned
slice_start_roster_presentation runner boots this path. A first attempt using
push_warning put 2 forbidden WARNING lines into that runner's stderr — caught and
fixed here, not left for a verifier.

Retail leaves exactly one template with no Acceleration/Braking/TurnTime:
WhirlpoolLocomotor (locomotor.ini:3167), referenced only by ElvenWhirlpool, a
stationary map effect. That is the only legitimate "no values" case.

### 9. Census assertions tightened — DONE

`test_effective_retail_census` now asserts equalities and where templates live:
by_file == {locomotor.ini: 123, object/cinematic/cinematiclocomotor.ini: 5};
len(templates) == 128; len(bindings) == 420; unique_refs == 97;
total_refs == 711.

Unique 97 and total 711 match the verifier's independent count EXACTLY. The one
number that differs is templates: the verifier's 123 is locomotor.ini alone; 128
is the whole table, and the extra five are precisely what item 5 was about —
asserting 123 would re-introduce the bug. Both figures are asserted, so the
distinction cannot silently drift. Independently reproduced by
`workspace/logs/locoA/census_probe.py`: templates 128 / objects 420 / unique
referenced 97 / total bindings 711 / referenced but undefined [].

The verifier's grep cross-check of 715 raw `Locomotor =` lines vs 711 rows
stands: bindings are keyed by condition per object, so an object repeating a
Condition collapses to one row (later wins, as SAGE does). Four such collapses.
Left as-is and flagged for Phase B, as the verifier suggested.

### 10. Behavioural movement runner — DONE

New `game/tests/retail_authored_movement_runner.gd`. Boots the real RotWK Men
skirmish (rotwk.map.adorn-river) off the live selection with
OPENBFME_STARTER_ARMY=1, and:

- asserts every SPAWNED row carries authored acceleration, braking, turn rate;
- asserts every CONFIGURED UNIT RULE does too — this covers cavalry and siege,
  which this map's start roster does not field — with the trebuchet as the single
  named, explained exception;
- asserts no reform threshold comes from a constant;
- issues a real move order to one unit of each class present and asserts the
  position actually changes.

13 checks, 0 failures, with a pinned EXPECTED_CHECKS liveness guard so a silent
coroutine abort cannot report green.

```
AUTHORED_MOVEMENT_CATEGORY_CENSUS { "hero": 2, "ranged-infantry": 2, "": 2, "infantry": 1 }
PASS every_spawned_row_carries_authored_movement
PASS every_unit_rule_carries_authored_movement_or_is_a_named_gap
PASS reform_threshold_is_authored_or_absent
PASS infantry_moves_on_a_move_order
PASS ranged_infantry_moves_on_a_move_order
AUTHORED_MOVEMENT_SKIP category=cavalry reason=not-on-the-start-roster
AUTHORED_MOVEMENT_SKIP category=siege reason=not-on-the-start-roster
PASS hero_moves_on_a_move_order
PASS _moves_on_a_move_order        (men-porter, the builder)
PASS at_least_one_class_moved
AUTHORED_MOVEMENT_RESULT passed=13 failed=0
```

Stated honestly: cavalry and siege POSITION DELTAS are not proven on this
surface — the RotWK Men skirmish start roster is hero + infantry + archers +
porter, and the runner says so out loud instead of scoring a silent pass. Their
authored-values claim IS proven, over the whole fieldable faction, by the
unit-rules check.

## Field coverage

Compiled with meaning (27 keys in `_FIELD_SPECS`): surfaces, zAxisBehavior,
minTurnSpeed, appearance, braking, acceleration, turnRate, turnTime,
turnTimeDamaged, stickToGround, canMoveBackwards, backingUpSpeed, fastTurnRadius,
slowTurnRadius, turnPivotOffset, formationPriority, hasSuspension,
closeEnoughDist, preferredHeight, preferredAttackHeight, lift,
maxTurnWithoutReform, waitForFormation, scalesWalls, chargeSpeed,
slideIntoPlaceTime, maxThrustAngle.

Everything else retail authors inside a Locomotor block is preserved verbatim in
`unsupported[]` with key/raw/sourceIni/line — nothing is dropped. Unknown keys on
a LocomotorSet are preserved the same way.

Emitted onto each unit's `movement` block, only when authored (absent stays
absent): acceleration, braking, turnRateDegreesPerSecond, turnTimeMs,
turnTimeDamagedMs, fastTurnRadius, slowTurnRadius, minTurnSpeed, turnPivotOffset,
maxTurnWithoutReformDegrees, canMoveBackwards, backingUpSpeed, closeEnoughDist,
surfaces, zAxisBehavior, appearance, formationPriority, waitForFormation,
locomotorId. Plus `locomotorSets` at the simulation level.

TurnRate semantics: retail authors the field NOWHERE in the corpus, so TurnTime
(ms) is the real source, converted 360000 / TurnTime the way OpenSAGE's shared
SAGE core does (LocomotorTemplate.cs:94). The previous TurnRate-preferred-over-
TurnTime fallback is KEPT, not silently dropped (two importer fixtures author
TurnRate), and is now covered by a test.

## A second over-strictness caught by running the routed readers

Routing `retail_unit_rules.py` through the canonical compiler surfaced a real
regression that the narrow readers had been hiding. RotWK 2.01's merged
`editions/rotwk/cache/effective-assets/data/ini/locomotor.ini:2802` reads:

```
  FastTurnRadius        = 12.0 Can turn in a 10 foot radius circle when moving. ;,;
```

— the `;` comment marker was relocated by whatever produced that tree. The old
`_template_locomotor` read only Acceleration/Braking/TurnRate/TurnTime and never
looked at FastTurnRadius, so it never noticed. The new compiler parses ALL
fields and was hard-failing the whole document, breaking three tests that pass
on main.

Fix: a KNOWN field whose authored text cannot be resolved is RECORDED in
`unsupported[]` with its raw text and the reason, not guessed at and not fatal.
The field becomes absent, which every consumer treats as a loud gap. Covered by
`test_unresolvable_known_field_is_recorded_not_fatal`.

Measured blast radius across all three oracle trees — exactly ONE field, on a
template nothing needs it from:

```
== workspace/retail-extract                                     128 templates, 0 unresolved
== workspace/retail-work/cache/effective-assets                  128 templates, 0 unresolved
== workspace/retail-work/editions/rotwk/cache/effective-assets   143 templates, 1 unresolved
     PorterLocomotor FastTurnRadius 2802 '12.0 Can turn in a 10 foot radius circle when moving.'
```

## Runners

All sequential, one Godot at a time, live selection
`OPENBFME_CONTENT=<repo>\workspace\content-packs`. Logs under `workspace/logs/locoA/`.

| Runner | Required | Measured | SCRIPT ERROR | Invalid access | `unauthored locomotor` |
|---|---|---|---|---|---|
| retail_member_combat_runner | 115/0 | 115/0 | 0 | 0 | 0 |
| retail_spellbook_runner | 218/0 | 218/0 | 0 | 0 | 0 |
| slice_start_roster_presentation_runner | 22/0 | 22/0 | 0 | 0 | 0 |
| retail_lockstep_determinism_runner | 5/0 | 5/0 | 0 | 0 | 0 |
| retail_formation_movement_runner | report count (was 31) | 34/0 (+3, the new no-reform-gate test) | 0 | 0 | 0 |
| boot_startup_runner | 44/0 | 44 checks, 0 failures | 0 | 0 | 0 |
| retail_authored_movement_runner (new) | — | 13/0 | 0 | 0 | 0 |
| retail_state_pin_runner | record hash | `0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc` — UNMOVED, "OK hash matches the pinned value". Not re-minted. | 0 | 0 | 0 |
| retail_slice_runner | failure NAMES, 0 new | 0 new failure names | 0 | 0 | 0 |

`retail_state_pin_runner` was run twice, before and after the last sim edit;
identical hash both times. The pin does not move because no pack was rebuilt and
the shipped movement blocks are unchanged; it will move on the recook that lands
the new fields, which is expected and is Phase B's to record.

### retail_slice_runner failure names

`workspace/logs/q14fin-*` predates three commits that landed on main after it
(`a7c9904`, `fc0713c`, `2d45bf4`), so I took a fresh baseline from the UNTOUCHED
main checkout on the same content selection rather than judging against a stale
file.

```
main checkout:  RETAIL_SLICE_RESULT passed=371 failed=59
                RETAIL_SLICE_ACCEPTANCE FAIL min_passed=374 pinned_known_failures=31
this worktree:  RETAIL_SLICE_RESULT passed=370 failed=59
                RETAIL_SLICE_ACCEPTANCE FAIL min_passed=374 pinned_known_failures=31

failure-name diff (LC_ALL=C, sorted, unique): 87 names each
  NEW  (in mine, not in main):  <empty>
  GONE (in main, not in mine):  <empty>
```

The ACCEPTANCE FAIL is PRE-EXISTING and identical on both trees; the q14fin
baseline shows the same line.

The single `passed` delta is `dock_geometry_mirrors_the_authored_apt_placements`,
a check that does NOT exist in my branch's copy of the runner — it is present
only in the shared checkout's working tree
(`.../open-bfme/game/tests/retail_slice_runner.gd:758`), i.e. another lane's
in-flight edit to the shared tree. Not a failure, and not mine.

### A real regression I introduced, found and fixed before reporting

The first retail_slice_runner run on this branch ABORTED SILENTLY at 306 passes,
right after `construction_animation_is_manual_progress_not_looping`, exiting 0
with no RESULT line — the classic silent coroutine abort. Cause:
`retail_slice_sim.gd:4580-4583` hard-indexes `unit_rule["acceleration"]`,
`["turn_rate_degrees_per_second"]`, `["braking"]` when spawning an entity, and my
trebuchet rule now legitimately omits them on the stale pack, so spawning the
trebuchet raised Invalid access and killed the rest of the run. Fixed at the
spawn site by reading through `.get(..., 0.0)` — NOT a silent fallback: 0.0 is
exactly the sentinel `_step_route` and `_retail_turn_rate_degrees` refuse on,
loudly and by name. Confirmed by re-running the full runner to completion.

## Importer tests

Pinned interpreter, `BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`,
sequential (no `-n`), targeted only — the orchestrator runs the full suite after merge.

| Selection | Result |
|---|---|
| test_locomotor_compiler.py (new) | 12 passed |
| test_neutral_wildlife_noncombatant.py | 9 passed (was 4 failed) |
| test_bfme2_neutral_visual_recipe_oracle.py | 10 passed (was 2 failed) |
| test_module_census.py | 31 passed / 2 failed — identical name set to the untouched main checkout |
| test_ini_compile_remainders, test_crush_locomotor_compile, test_hero_summon_leaf_closure | all passed |
| combined run of the above | 84 passed / 2 failed (the two pre-existing census names only) |
| routed readers `-k "ring_system or m3_pack or spellbook or cah or retail_unit_rules or faction_slice_pack or playable_unit"` | 635 passed / 6 failed |

### The 6 routed-reader failures are ALL pre-existing — proven by name against main

Re-run of the same test files against the UNTOUCHED main checkout, pinned
interpreter, same env:

| Test | main checkout | this worktree | Verdict |
|---|---|---|---|
| test_playable_unit_compiler::test_horde_dispatch_graphs_cover_exact_effective_retail_corpora | FAIL | FAIL | pre-existing |
| test_playable_unit_compiler::test_canonical_retail_elven_grace_and_deadeye_effects_are_implemented[rotwk-retail-rotwk-200-529-7] | FAIL | FAIL | pre-existing |
| test_playable_unit_compiler::test_retail_dwarven_demolisher_emits_exact_nonhero_toggle_ability[rotwk-retail-rotwk-422-381] | FAIL | FAIL | pre-existing |
| test_playable_unit_compiler::test_canonical_retail_eowyn_emits_binary_closed_disguise_ability[rotwk-retail-rotwk-1045] | FAIL | FAIL | pre-existing |
| test_playable_unit_import::test_effective_bfme2_neutral_recipe_blockers_are_closed | FAIL | FAIL | pre-existing (a named Q6 baseline failure) |
| test_retail_unit_rules::test_shroud_clearing_range_is_compiled_separately_from_vision[rotwk-corpus1] | FAIL | FAIL | pre-existing |

main: `5 failed, 257 passed` for the two playable_unit files, plus
`1 failed, 5 passed` for test_retail_unit_rules. The failure reasons are
unrelated to locomotion: `NeutralPackProfileError: neutral catalog summary is
invalid` and `ValueError: unsupported stance field 'SPELL_DAMAGE' in
ArcherHordeStanceHoldGround`.

ZERO new importer failure names.

## Hygiene

`git grep -n "HORDE_LOCOMOTION_RESPONSE_SCALE\|RETAIL_FALLBACK_TURN_RATE_DEGREES\|RETAIL_MAX_TURN_WITHOUT_REFORM\|RETAIL_CAVALRY_MAX_TURN\|max_speed \* 10"`

- `game/` (src AND tests): ZERO hits.
- Repo-wide: hits only in `orchestration/briefs/loco-phaseA-locomotor-table-sol.md`
  and `orchestration/reports/loco-phaseA-verify.md` — the two documents that name
  the constants in prose.

No packs were rebuilt, published or selected. `selection.json` untouched. All
logs under `workspace/logs/locoA/`. Nothing under `dist/` or `build/`.

## What I could not verify / left open

1. Cavalry and siege POSITION DELTAS. Not on the RotWK Men skirmish start
   roster. Their authored values are proven via the unit-rules check; their
   motion is not. Closing this needs a runner that produces from a Stable and a
   Workshop, or a map whose roster fields them.
2. The widened `movement` block through a real cook. No pack was rebuilt
   (correctly — the brief forbade it), so the playable-unit contract and the
   runtime adapter have not seen the new shape on real cooked bytes. This is the
   largest remaining risk and it is the same one the verifier named. The recook
   also closes the trebuchet named gap and WILL move the state pin.
3. `tools/gate-m2-focused.ps1` was NOT re-pinned. Neither new/changed runner can
   go in as-is: retail_formation_movement_runner emits 56 pre-existing
   `ERROR: [RetailSliceSim] structure armor: kind '...' has no compiled armor
   contract` lines, and retail_authored_movement_runner emits the pre-existing
   `WARNING: Compiled map environment.json is absent for rotwk.map.adorn-river`.
   `$forbiddenDiagnostics` forbids both. Wiring them needs those unrelated lines
   fixed first. Flagged rather than worked around; I did not weaken the regex.
4. The two test_module_census failures (GiantBirdSlowDeathBehavior,
   ReflectDamage unclassified) are pre-existing on this host and block the full
   `python -m openbfme_importer.module_census` CLI. I regenerated only the
   source-scan half through the module's own functions and left the
   retail-corpus half untouched; a verifier should confirm that is acceptable,
   or fix the classification gap and regenerate wholesale.
5. Four-binding census collapse (715 raw `Locomotor =` lines vs 711 rows):
   per-object repeated Condition collapses to one row, later wins. Believed
   correct (that is what SAGE does) but not proven against the engine. Phase B.
6. The shared checkout was being edited by another lane during this work
   (`dock_geometry_mirrors_the_authored_apt_placements` exists there and not on
   my base). My main-tree baselines therefore reflect that lane's tree, not clean
   `main`. It affected exactly one PASSING check name and no failures.
