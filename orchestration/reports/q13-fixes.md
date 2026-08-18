# Q13 fixes — F1–F4 + queue rows

Date: 2026-08-17  
Lane: `grok-q13-fixes`  
Brief: `orchestration/briefs/q13-fixes-grok.md`  
Verifier: `orchestration/reports/q13-verify.md`  
Owner ruling (F1): `combat.damage.value` is the sum of every warhead DamageNugget at zero distance. GondorTrebuchet **780 stays**.

Selected content for every Godot measurement: `workspace/content-packs/selection.json`, active pack `rotwk-men-vslice/4f92c8a486861100c29f20d1287f01990bc835a2622c53e911cfd2fb024a147e`.

The frozen `retail_state_pin` hash did not move because the fixture is synthetic (`retail_state_pin_runner.gd` `_harness_rules()`): no hero, no `projectile_object_id` / `projectile_speed`. It is not evidence that live combat is unchanged. Archers on the current selection already take the projectile path. Recorded as Q16.

---

## Fixes

1. **Importer provenance + pin.** `_base_weapon_damage` now hoists top-level `expression` / `sourceIni` / `line` / `constantSourceIni` from the first component and publishes `valueSemantic = "sum-of-nugget-damage-at-zero-distance"`. Pytest pins GondorTrebuchet `damage.value == 780` with components `(20, 0)` / `(100, 50)` and provenance fields, and pins GondorFighter combat byte-identical to `d47d1f6`.
2. **`damageTypeSemantic` truthfulness.** `_authored_flat_damage_type` ignores nugget-nested `DamageType` rows that `_named_definition_values` flattens. `_apply_nugget_damage_types(..., flat_damage_type=)` names the actual source and never overwrites a disagreeing flat type. Callers still emit a unique flattened type so GondorFighter stays byte-identical. Pytest: one weapon with a true flat `DamageType`, one nugget-only.
3. **Latent wipe.** `_apply_weapon_mode` no longer blanks `damage_components` when the selected mode omits them. `retail_member_combat_runner` adds four checks (seed, attack ticks, mode toggle). Gate pin `111 → 115` with a dated comment that also records the pre-Q13 49-vs-98 rot and Q13's 98→111.
4. **Focused-gate step.** `projectile_table_runtime` marker stays the RESULT line; comment corrected. Runner `setup` now seeds only `fortress` so Q11 structure-armor `ERROR:` lines are not emitted by this step. The 13→40→68 note stays on `member_health_overlay`. The focused gate itself remains dead until Q15 (activePack `bfme2-men-vslice` vs Q1 `rotwk-men-vslice`).
5. **Queue.** Q15 DECISION (unassigned). Q16 READY (unassigned). Q13: fixes landed, awaiting re-verify.
6. Pins other than the named member-combat count, selection, packs, and the state pin were not touched.

---

## Definition of Done

### 1. Targeted pytest, then FULL importer suite — PASS

Targeted (changed modules) `workspace/logs/q13fix-importer-targeted.txt`:

```
318 passed in 282.22s (0:04:42)
```

FULL suite sequential, pinned interpreter, `BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`. Log: `workspace/logs/q13fix-importer-full.txt`.

```
= 6 failed, 3737 passed, 17 skipped, 2 warnings, 975 subtests passed in 2974.11s (0:49:34) =
```

Failure-by-failure against the Q6 baseline — all six, no others, 0 errors:

| Failure | In Q6 baseline |
|---|---|
| `test_playable_unit_import.py::test_effective_bfme2_neutral_recipe_blockers_are_closed` | yes |
| `test_rotwk_official_map_corpus.py::…test_official_corpus_is_72_maps_split_22_skirmish_50_wotr` | yes |
| `test_scripts_converter.py::…test_effective_official_skirmish_inheritance_teams_are_attested` | yes |
| `test_special_disguise_prerequisite.py::test_current_selected_rotwk_eowyn_pack_fails_new_prerequisite_closure` | yes |
| `test_w3d_chunk_backlog.py::…test_committed_census_dates_itself_and_is_current` | yes |
| `test_w3d_chunk_backlog.py::…test_committed_census_decode_corpus_figures_are_dated_fresh` | yes |

Passed count moved 3734 → 3737 because this lane added three importer tests (trebuchet pin extension is the same test; new: fighter A/B, flat semantic, nugget-only semantic). Zero new failures.

### 2. Runners — PASS

Sequential, one Godot process at a time, `OPENBFME_CONTENT=workspace\content-packs`. Logs `workspace/logs/q13fix-<runner>.txt`.

| Runner | Pin | Measured | Verdict |
|---|---|---|---|
| `retail_member_combat_runner` | 115/0 (was 111) | `RETAIL_MEMBER_COMBAT_RESULT passed=115 failed=0` | PASS |
| `weapon_cycle_model_conditions_runtime_runner` | 23/0 | `passed=23 failed=0` | PASS |
| `warhead_weapon_toggle_runtime_runner` | 35/0 | `passed=35 failed=0` | PASS |
| `projectile_table_runtime_runner` | 4/0 | `PROJECTILE_TABLE_RUNTIME_RESULT passed=4 failed=0` | PASS |
| `retail_spellbook_runner` | 218/0 | `passed=218 failed=0` | PASS |
| `retail_lockstep_determinism_runner` | 5/0 | `RETAIL_LOCKSTEP_RESULT passed=5 failed=0` | PASS |
| `retail_state_pin_runner` | UNCHANGED `0e4bcdbf…` | `hash=0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc` + `OK` | PASS |

### 3. A/B compile evidence — PASS

Log: `workspace/logs/q13fix-ab-compile.txt`. Pytest: `test_gondor_fighter_combat_is_byte_identical_to_d47d1f6` and `test_bfme2_gondor_trebuchet_compiles_retail_radius_components`.

**GondorFighter** combat block is byte-identical to `d47d1f6` (`json.dumps(..., sort_keys=True)`). Damage stays the flat shape `{value: 40, expression: GONDOR_SOLDIER_SWORD, sourceIni: data/ini/weapon.ini, line: 5526, constantSourceIni: data/ini/gamedata.ini}`.

**GondorTrebuchet** `combat.damage`:

- `value` = **780**
- `valueSemantic` = `sum-of-nugget-damage-at-zero-distance`
- `expression` = `GONDOR_TREBUCHET_DAMAGE`
- `sourceIni` = `data/ini/weapon.ini`
- `line` = 3248
- components: `(radius 20, taper 0)` and `(radius 100, taper 50)`, each 390

The warhead authors `DamageType = SIEGE` only inside DamageNugget blocks, so `damageTypeSemantic` truthfully says the weapon authors no flat DamageType.

### 4. Hygiene / addresses / commits — PASS

| Check | Result |
|---|---|
| `python tools\check_pack_addresses.py` | `PACK_ADDRESS_CHECK PASS packs=200 roots=2` |
| `tools\gate-hygiene.ps1` | `HYGIENE_GATE PASS root-files=25 tracked=2594` |
| `git status --porcelain` | empty after the chore commit |

Commits (prefixed as required):

- `d6e3acb` `fix(importer): restore combat.damage provenance and truthful type semantic`
- `f000569` `fix(sim): keep damage_components across weapon-mode ticks`
- `abcaf99` `chore(queue): land Q13 fixes and add Q15 Q16 follow-ups`

### 5. This report

`orchestration/reports/q13-fixes.md`.
