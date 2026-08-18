# Q26 verify — admit retail untriggered SubObjectsUpgrade

Date: 2026-08-18 · Verifier: grok-q26-verify (fresh context) · Implementor report: `orchestration/reports/q26-subobjectsupgrade-untriggered.md`

HEAD measured: `ca8899bc1d87f98932f74d63a9fc34b52a7217b8`. This lane did not re-run the 46-minute full importer suite (log present, complete, internally consistent). This lane did not re-run faction convert (coverage + convert-log JSON were read from disk). This lane did not publish, select, or cook.

## Verdict

**ACCEPT.** All four DoD criteria PASS on evidence re-measured here. One status caveat (pre-existing untracked Q14 report) and two process nits that are not DoD fails.

## Criterion table

| # | Criterion | Result | Evidence I measured |
|---|---|---|---|
| 1 | New tests green; FULL importer suite (sequential, pinned interpreter, `BFME2_INSTALL` set) → exactly the 6 Q6 names, 0 errors, log `workspace/logs/q26-importer-full.txt` | **PASS** | Targeted re-run (pinned `python-3.12-env`, `PYTHONPATH=importer`, `BFME2_INSTALL=workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`): `7 passed in 0.17s` — all seven names in `importer/tests/test_sub_objects_upgrade_contract.py` PASSED. Full-suite log exists (539870 bytes), header uses the pinned interpreter, `collected 3765 items`. Summary line 4199: `= 6 failed, 3742 passed, 17 skipped, 2 warnings, 975 subtests passed in 2760.16s (0:46:00) =`. Arithmetic: 6+3742+17=3765. No `ERROR` / `error` pytest outcomes. The six `FAILED` names (lines 4182–4195) are exactly the Q6 set (see below). |
| 2 | `import-faction --game rotwk --faction elves --convert` → `converterGapCount` 0 and NoldorWarrior / NoldorWarriorHorde converted (was gaps=2). Spot-check men + one more. | **PASS** | `elves-coverage.json` `summary.converterGapCount=0`, `convertedCount=50`, `objectCount=52`, `excludedCount=2`. Object `NoldorWarrior` `status: converted`; object `NoldorWarriorHorde` `status: converted`. Convert log `workspace/logs/q26-elves-convert.txt` (UTF-16) JSON: `converter_gap_count=0`, `converted_count=50`, `ready=true`. Progress lines: `NoldorWarriorHorde (converted, 64220ms)` and `NoldorWarrior (converted, 67491ms)`. Descriptor `elves/objects/noldorwarrior/descriptor.json:4888-4893` emits `untriggered: true`, `reason: no-active-triggeredby-authored`, `runtimeStatus: deferred`, tag `ForgedBlades_Upgrade` (same on the horde descriptor). Men coverage: `converterGapCount=0`, `convertedCount=58` / `objectCount=60`; men convert-log tail: `converted_count=58`, `converter_gap_count=0`, `ready=true`. Dwarves coverage: `converterGapCount=0`, `convertedCount=54` / `objectCount=56`; dwarves convert-log JSON: `converter_gap_count=0`, `converted_count=54`, `ready=true`. |
| 3 | `check_pack_addresses` PASS; `gate-hygiene` PASS; git status clean; commits `fix(importer):` / `test(importer):`, explicit paths | **PASS** (status caveat) | Re-ran `python tools\check_pack_addresses.py` → `PACK_ADDRESS_CHECK PASS packs=200 roots=2` (also written to `workspace/logs/q26-verify-pack-addresses.txt`). Re-ran `tools\gate-hygiene.ps1` → `HYGIENE_GATE PASS root-files=25 tracked=2606`. Commits: `b72b024` `fix(importer): admit retail untriggered SubObjectsUpgrade` — 1 file, `importer/openbfme_importer/module_contracts.py` (+5/−3). `2e16cde` `test(importer): cover untriggered SubObjectsUpgrade and NoldorWarrior` — 1 file, `importer/tests/test_sub_objects_upgrade_contract.py` (+160/−12). Range `bc206d0..ca8899b` only those two importer files plus `orchestration/queue.md` and `orchestration/reports/q26-subobjectsupgrade-untriggered.md`. `git status --porcelain` is not empty: `?? orchestration/reports/q14-recook-v024.md`. That file is dated 2026-08-17 22:56 (before this lane), is not in any Q26 commit, and is the only untracked tracked-tree path. No modified tracked files. |
| 4 | Report `orchestration/reports/q26-subobjectsupgrade-untriggered.md`. Add queue row Q27 with the specified text. | **PASS** | Report is on disk (and was amended by `ca8899b` to record hashes). `orchestration/queue.md` Q27 item column is verbatim: `typed-module over-strictness cluster — contracts that raise on retail-authored-but-inert shapes (SubObjectsUpgrade done; audit the other 6 requires-TriggeredBy contracts + men GondorKnightsofDol AutoHealBehavior healonlyifnotunderattack) — one lane to admit them as deferred rows.` Status `READY`, owner `unassigned`. Q26 row is `CLOSED`. |

## Surgical-scope check (H)

`git show b72b024` replaces only the `compile_sub_objects_upgrades` raise at the former `:707-710` with:

```
fields["untriggered"] = True
fields["reason"] = "no-active-triggeredby-authored"
```

The other six `requires TriggeredBy` raises are still present and still raise:

| Contract | Line (HEAD) | Still raises? |
|---|---|---|
| AttributeModifierUpgrade | 498 | yes (`requires TriggeredBy and AttributeModifier`) |
| GeometryUpgrade | 593 | yes |
| ModelConditionUpgrade | 996 | yes |
| BuildableHeroListUpgrade | 1619 | yes |
| AllowBannerSpawnUpgrade | 1643 | yes |
| ObjectCreationUpgrade | 6336 | yes |

`SubObjectsUpgrade` no longer contains `requires TriggeredBy`. Unsupported-field raise at `:671-674` is unchanged (covered by `test_sub_objects_upgrade_unknown_field_still_raises`). `_sub_objects_upgrade_row_has_closed_runtime` still returns false without `TriggeredBy`, so the untriggered row is `runtimeStatus: deferred`.

`CACHE_VERSION` in `faction_object_cache.py` is still `5`. `tools/check-product-contracts.py` seals `contracts/*.json` only — `module_contracts.py` has no `policy_digest`. Not a DoD fail: the brief said bump only if the contract *shape* changed; the new flags live inside `fields`.

## Full-suite failure names (from the log, not the implementor report)

| Failure | In Q6 baseline? |
|---|---|
| `test_playable_unit_import.py::test_effective_bfme2_neutral_recipe_blockers_are_closed` | yes |
| `test_rotwk_official_map_corpus.py::OfficialRotwkMapCorpusTests::test_official_corpus_is_72_maps_split_22_skirmish_50_wotr` | yes |
| `test_scripts_converter.py::InheritanceMarkerRealCorpusTests::test_effective_official_skirmish_inheritance_teams_are_attested` | yes |
| `test_special_disguise_prerequisite.py::test_current_selected_rotwk_eowyn_pack_fails_new_prerequisite_closure` | yes |
| `test_w3d_chunk_backlog.py::CommittedCensusProvenanceTests::test_committed_census_dates_itself_and_is_current` | yes |
| `test_w3d_chunk_backlog.py::CommittedCensusProvenanceTests::test_committed_census_decode_corpus_figures_are_dated_fresh` | yes |

None new, none gone.

## Targeted tests I re-ran

Pinned interpreter, `PYTHONPATH=importer`, `BFME2_INSTALL` set. All PASSED:

- `test_sub_objects_upgrade_show_hide_is_executable`
- `test_sub_objects_upgrade_triggered_row_is_byte_identical_to_pre_q26`
- `test_sub_objects_upgrade_texture_row_stays_deferred`
- `test_sub_objects_upgrade_commented_triggeredby_is_untriggered`
- `test_sub_objects_upgrade_absent_triggeredby_is_untriggered`
- `test_sub_objects_upgrade_unknown_field_still_raises`
- `test_noldor_warrior_retail_untriggered_sub_objects_upgrade`

## Other-faction coverage on disk (not re-converted by this verifier)

DoD asked for men + one more. I read men and dwarves (Q26 convert logs dated 2026-08-18 03:31 / 03:40). I also *read* the other coverage files already on disk; I did not re-run those converts and I do not treat them as Q26-produced:

| Faction | `convertedCount` | `converterGapCount` | Q26 convert log? |
|---|---|---|---|
| elves | 50 | 0 | yes (`q26-elves-convert.txt`) |
| men | 58 | 0 | yes (`q26-men-convert.txt`) |
| dwarves | 54 | 0 | yes (`q26-dwarves-convert.txt`) |
| isengard | 53 | 0 | no |
| mordor | 53 | 0 | no |
| wild | 46 | 0 | no |
| angmar | 47 | 1 | no — residual gap, disclosed by implementor as Q14, not this contract |

## Not verified / skipped

- Did not re-run the 46-minute full importer suite. Judged the existing log: complete session, pinned interpreter, collected==failed+passed+skipped, six named failures only.
- Did not re-run `import-faction --convert` for elves/men/dwarves. Read coverage JSON + convert-log JSON + the two Noldor descriptors.
- Did not convert isengard / mordor / wild / angmar.
- Did not publish, select, or recook an elves pack (out of scope; selected elves pack still predates this contract).
- Did not run Godot.
- Hygiene tracked count is 2606 here vs implementor 2605 — expected: their hygiene log is timestamped 04:28:06, before `00ed759` added the report file. Gate still PASS.

## Process nits (not DoD fails)

- Q26 implementation commits (`b72b024`, `2e16cde`, `00ed759`, `ca8899b`) omit the standing `Co-Authored-By` trailer. Only the brief commit `bc206d0` has it. DoD 3 asked for prefixes + explicit paths, which they have.
- Working tree is not porcelain-clean solely because of pre-existing `?? orchestration/reports/q14-recook-v024.md` (2026-08-17, Q14 debris). This verifier did not touch it.

## Commits in the verified range

```
ca8899b chore(queue): record Q26 report commit hashes
00ed759 chore(queue): close Q26, file Q27 over-strictness cluster
2e16cde test(importer): cover untriggered SubObjectsUpgrade and NoldorWarrior
b72b024 fix(importer): admit retail untriggered SubObjectsUpgrade
bc206d0 docs(q26): brief — admit retail untriggered SubObjectsUpgrade
```
