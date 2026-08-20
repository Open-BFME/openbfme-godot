# Q15 report - retarget dead M2 gate to RotWK Men

Date: 2026-08-19

Source revision before this lane: `c0a4f2c3657c11aa6e566574c769a30e2dc2b777`.
Live selection: `rotwk-men-vslice/51d4885433869fa6290498eeac597b8cc8ac79e540d073dbf03c9c0d0184df3c` from `C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs\selection.json`.

## Outcome

The dead BFME2 selection guard is retargeted to the current immutable RotWK Men selection. The cheap live-selection guard and all touched tests pass. The required full `gate-m2-focused.ps1` run is still red after reaching the runners: `bound_props_runtime` emits a forbidden Godot error for the independently tracked Q33-importer missing `w3d_tree_draw_type_ids` table. This lane does not suppress that error and does not claim the requested green AFTER result.

No selection, pack, provenance, or other content file was changed. No pack was cooked, selected, published, unsealed, or resealed.

## Retargeted and retained checks

| Check | Disposition | Reason |
|---|---|---|
| Content root | Retargeted | `Get-M2OracleContext` now honors explicit `OPENBFME_CONTENT`, prints `content_source=OPENBFME_CONTENT`, and derives the sibling workspace paths. This lets a worktree use the owner-designated live workspace without a silent fallback. The root must still be named `content-packs`. |
| Selection schema | Added fail-closed check | Requires `openbfme.pack-selection` schema version 0 before reading `activePack`. |
| Active pack shape | Retargeted | Requires `^rotwk-men-vslice/[0-9a-f]{64}$`; the immutable digest requirement is unchanged. |
| Strict completion profile file and `0bc2e767...` hash | Kept | The active RotWK pack declares this exact profile as `m3Recipe.baseProfile`; it is still the honest M2 base oracle, not an obsolete BFME2-only input. |
| RotWK pack/base relationship | Added fail-closed check | Requires pack schema/id/completion flags plus `openbfme.m3-men-pack-recipe`, base profile id `men-fords-v0-complete-generated`, and the pinned base-profile SHA. |
| Pack and provenance file existence | Kept | Missing selected pack or provenance still fails. |
| Provenance identity | Retargeted | Requires the retail-import v1 contract, matching pack contract, immutable faction-slice profile id/hash shapes, `source_game=rotwk-retail-user-owned`, and zero incomplete rows. The RotWK provenance profile is not falsely equated with the distinct M2 base profile. |
| Provenance audit | Added fail-closed check | Requires `provenance/audit.json` to match the provenance contract/profile/hash and to be semantic, non-light, valid, and error-free. |
| Bundle SHA, source revision, dirty-state digest | Kept | Oracle/capture/reliability evidence remains bound to the selected bundle and exact worktree identity. |
| Oracle path containment, capture schema, 47 unique capture ids | Kept | No path or capture integrity check was removed or relaxed. |
| Focused runner diagnostics | Kept | The forbidden diagnostic regex remains unchanged; this is why the Q33 Godot error keeps the full gate red. |

No prior check lacked an honest RotWK equivalent. The old provenance assertion was split into the real two-level contract: pinned M2 base profile through `pack.m3Recipe.baseProfile`, and audited RotWK faction-slice provenance through `manifest.json` plus `audit.json`.

## Fast failing-first test

Failing first:

```powershell
$env:PYTHONPATH='<worktree>\importer'
& 'C:\Users\Jonathan\Desktop\open-bfme\workspace\retail-work\tools\python-3.12-env\Scripts\python.exe' -m pytest importer/tests/test_m2_gate_script.py::test_m2_oracle_selection_guard_tracks_rotwk_men -q
```

Before implementation: 1 failed because `m2-oracle-common.ps1` lacked `^rotwk-men-vslice/...`.

After implementation: 1 passed in 0.18s.

Live seconds-scale guard:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\test-m2-oracle-selection.ps1 -ContentRoot 'C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs'
```

Result: `M2_ORACLE_SELECTION_TEST PASS`, active pack `51d48854...`, base profile `0bc2e767...`.
Log: `<worktree>\workspace\logs\q15-fast-selection-test.txt`.

## Required gate evidence

BEFORE command used the unmodified main-checkout gate against the live workspace:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\Users\Jonathan\Desktop\open-bfme\tools\gate-m2-focused.ps1 -GodotPath C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe
```

Result: exit 1, `M2_FOCUSED_GATE FAIL Selection does not name an immutable Men/Fords bundle.`
Log: `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\q15-gate-before.txt`.

The worktree received the required Godot cache initialization. A one-time full import was also needed for audio import artifacts:

```powershell
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' --headless --path game --import
```

Result: exit 0.
Log: `<worktree>\workspace\logs\q15-godot-import.txt`.

AFTER command:

```powershell
$env:OPENBFME_CONTENT='C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\gate-m2-focused.ps1 -GodotPath C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe
```

Result: exit 1. `animated_prop_runtime` and `archer_projectile_presentation_runtime` pass. `bound_props_runtime` reports its own 30/0 result but emits this forbidden diagnostic first:

```text
ERROR: The object does not have any 'meta' values with the key 'w3d_tree_draw_type_ids'.
at: _authored_tree_sway_type_ids (res://src/retail_slice/retail_fords_battlefield.gd:1091)
```

Log: `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\q15-gate-after.txt`.

This is not a check that can be honestly removed for RotWK. `orchestration/queue.md` Q33-importer states that no selected pack carries this table and owns emitting it plus a recook. The runtime currently calls `get_meta(..., null)` for the documented absent-table case, which Godot 4.7 logs as an error. Q15 is limited to `tools/ + tests` and forbids content operations, so neither the runtime fix nor importer/recook work is included.

## Other verification

```powershell
& '<pinned-python>' -m pytest importer/tests/test_m2_gate_script.py -q
```

Result: 11 passed in 2.04s.
Log: `<worktree>\workspace\logs\q15-test-m2-gate-script.txt`.

```powershell
& '<pinned-python>' -m pytest importer/tests/test_castle_*.py -q
```

Result: 174 passed in 531.94s.
Log: `<worktree>\workspace\logs\q15-castle-pytests.txt`.

## Not done

- The full focused gate is not green; Q15 is therefore not marked FIXED.
- No diagnostic was suppressed or excluded.
- No `game/src` runtime change was made.
- Q33-importer was not claimed, implemented, or recooked.
- Nothing was pushed, selected, cooked, or published.
