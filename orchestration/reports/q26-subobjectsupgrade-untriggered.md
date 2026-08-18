# Q26 — admit retail's untriggered SubObjectsUpgrade

Date: 2026-08-18 · Owner: grok-q26 · Brief: orchestration/briefs/q26-subobjectsupgrade-untriggered-grok.md
Baseline HEAD: `bc206d044b0e383b888ffa45a620958e7d7f814e` · Final HEAD: (queue/report commit on top of `2e16cde`)
Selected pack(s) used for every Godot measurement: none — this lane did not run Godot and did not change selection.

## Result (one paragraph)

Implemented; awaiting verify. `compile_sub_objects_upgrades` now admits a SubObjectsUpgrade whose `TriggeredBy` is absent or commented as a deferred `untriggered: true` row with `reason: "no-active-triggeredby-authored"`. A row that still authors TriggeredBy is byte-identical to HEAD `bc206d0`. Elves convert `converterGapCount` moved 2 → 0; NoldorWarrior and NoldorWarriorHorde both converted. The other six `requires TriggeredBy` contracts were not touched. No pack build or selection change. CACHE_VERSION left at 5: the stored row envelope is unchanged (the new flags live inside `fields`); `module_contracts.py` is already mixed into the unit/structure compiler identity via import discovery (elves compilerIdentity `31059e01…` → `4470cab5…`).

## Commits

- `b72b024077b01f8208debdc3bd08161cfe72e305` — `fix(importer): admit retail untriggered SubObjectsUpgrade`
- `2e16cde11073f53b8c9696b2f25eee96defbc3af` — `test(importer): cover untriggered SubObjectsUpgrade and NoldorWarrior`
- (this commit) — `chore(queue): close Q26, file Q27 over-strictness cluster`

## Design choices

Retail `noldorwarrior.ini:1694-1695` authors:

```
Behavior = SubObjectsUpgrade ForgedBlades_Upgrade
;		TriggeredBy	= Upgrade_ElvenForgedBlades
	ShowSubObjects	= Forged_Blade
End
```

The trigger is commented out, so the upgrade can never fire. The typed contract used to raise `ModuleContractError: … SubObjectsUpgrade requires TriggeredBy`, which turned two shipped elven units (member + horde-extension projecting the member lineage through `compile_all_module_contracts`) into converter gaps.

House match: sibling typed contracts already keep never-executable shapes as `runtimeStatus: "deferred"` plus an explicit reason (see `_SUB_OBJECTS_UPGRADE_DEFERRED`). This lane adds `fields.untriggered = True` and `fields.reason = "no-active-triggeredby-authored"`, then still goes through `_row(..., runtime_status=deferred)` because `_sub_objects_upgrade_row_has_closed_runtime` stays false without TriggeredBy. Armor-compiler's skip-the-shell convention was not copied: the brief asked for an emitted inert row with provenance.

The horde-extension path has no second TriggeredBy guard. `playable_unit_compiler` concatenates `compile_all_module_contracts(member_lineage)` onto the container rows; fixing the typed extractor covers both NoldorWarrior and NoldorWarriorHorde. `_sub_object_upgrades_contract` already skipped missing TriggeredBy and was left alone.

Unsupported-field raises are unchanged (scope-creep guard). AttributeModifierUpgrade, GeometryUpgrade, ModelConditionUpgrade, BuildableHeroListUpgrade, AllowBannerSpawnUpgrade, and ObjectCreationUpgrade still raise on missing TriggeredBy. Filed as Q27.

No `policy_digest` on `module_contracts.py`. `CACHE_VERSION` was not bumped: the artifact envelope keys are the same; optional fields inside `fields` are not an envelope change. The import-graph compiler identity already invalidates unit/structure DDC.

## Definition of Done — verbatim

| # | Criterion | Result | Evidence (log path + the actual line) |
|---|---|---|---|
| 1 | New tests green; FULL importer suite (sequential, pinned interpreter, BFME2_INSTALL set) → exactly the 6 Q6 names, 0 errors, log workspace/logs/q26-importer-full.txt | PASS | Targeted: `7 passed in 0.41s` (`importer/tests/test_sub_objects_upgrade_contract.py`). Full: `= 6 failed, 3742 passed, 17 skipped, 2 warnings, 975 subtests passed in 2760.16s (0:46:00) =` in `workspace/logs/q26-importer-full.txt`. Failures are the six Q6 names; 0 errors. Passed 3737 → 3742 is the five new tests. |
| 2 | `openbfme-import import-faction --game rotwk --faction elves --convert` → converterGapCount 0 and NoldorWarrior/NoldorWarriorHorde converted (was gaps=2). Spot-check men + one more. | PASS | Before (Q14 r3, `workspace/logs/q14-recook-r3.txt:44`): `converted=48 gaps=2 complete=False`. After (`workspace/logs/q26-elves-convert.txt`): `"converter_gap_count": 0`, `"converted_count": 50`, `"ready": true`. Coverage `elves-coverage.json`: NoldorWarrior / NoldorWarriorHorde `status: converted`; descriptor `fields.untriggered: true`, `reason: no-active-triggeredby-authored`. Men: `converter_gap_count: 0` converted 58/60 (`workspace/logs/q26-men-convert.txt`). Dwarves: `converter_gap_count: 0` converted 54/56 (`workspace/logs/q26-dwarves-convert.txt`). |
| 3 | check_pack_addresses PASS; gate-hygiene PASS; git status clean; commits `fix(importer):` / `test(importer):`, explicit paths | PASS (status caveat) | `PACK_ADDRESS_CHECK PASS packs=200 roots=2` (`workspace/logs/q26-pack-addresses.txt`). `HYGIENE_GATE PASS root-files=25 tracked=2605` (`workspace/logs/q26-gate-hygiene.txt`). Commits by explicit path. Working tree after those commits is clean except pre-existing untracked `orchestration/reports/q14-recook-v024.md` (Q14 debris; this lane did not touch it). |
| 4 | Report this file. Add queue row Q27 as specified. | PASS | This file. Q27 added to `orchestration/queue.md`. |

## Runner table (before → after)

No Godot runners in this lane.

| Convert | Baseline (Q14 r3) | After | Pin |
|---|---|---|---|
| elves | converted=48 gaps=2 complete=False | converted=50 gaps=0 complete=True | `workspace/logs/q26-elves-convert.txt` |
| men | converted=58 gaps=0 complete=True | converted=58 gaps=0 complete=True | `workspace/logs/q26-men-convert.txt` |
| dwarves | converted=54 gaps=0 complete=True | converted=54 gaps=0 complete=True | `workspace/logs/q26-dwarves-convert.txt` |

## Failure-by-name delta

Full importer suite vs Q6 baseline — same six names, none new, none gone:

| Failure | In Q6 baseline |
|---|---|
| `test_playable_unit_import.py::test_effective_bfme2_neutral_recipe_blockers_are_closed` | yes |
| `test_rotwk_official_map_corpus.py::…test_official_corpus_is_72_maps_split_22_skirmish_50_wotr` | yes |
| `test_scripts_converter.py::…test_effective_official_skirmish_inheritance_teams_are_attested` | yes |
| `test_special_disguise_prerequisite.py::test_current_selected_rotwk_eowyn_pack_fails_new_prerequisite_closure` | yes |
| `test_w3d_chunk_backlog.py::…test_committed_census_dates_itself_and_is_current` | yes |
| `test_w3d_chunk_backlog.py::…test_committed_census_decode_corpus_figures_are_dated_fresh` | yes |

## Pins moved

None.

## Named unsupported semantics / follow-ups

- Q27: audit the other six `requires TriggeredBy` contracts plus men GondorKnightsofDol `AutoHealBehavior` `HealOnlyIfNotUnderAttack` — admit retail-authored-but-inert shapes as deferred rows. This lane did not expand to those.
- Elves pack recook/publish is still Q14's job. This lane converted only; it did not publish or select. The currently selected elves pack still predates this contract change.

## Not verified / left undone

- Did not re-publish or select an elves pack (brief: no pack builds/selection).
- Did not convert isengard / mordor / wild / angmar (spot-checked men + dwarves only). Angmar still had a Q14 residual gap=1 unrelated to this contract.
- Did not bump `CACHE_VERSION` or reseal product-contract digests (no digest on this file; envelope unchanged).
- Verifier has not yet re-run the DoD.
