# Q13 — ordinary projectiles and retail radius damage

Date: 2026-08-17  
Owner: `sol-projectiles`  
Implementation HEAD: `fabb94e`  
Selected pack used by every Godot measurement: Men `4f92c8a486861100c29f20d1287f01990bc835a2622c53e911cfd2fb024a147e` from `workspace/content-packs/selection.json`.

## Result

The implementation is committed, but Q13 is **not accepted/closed**. Its focused tests and lockstep proofs are green; the broad repository gates are not. The full importer run exhausted Windows process resources at 39%, `retail_slice_runner` is below its existing acceptance ratchet, hygiene is blocked by two pre-existing root files, and the resource exhaustion prevented `gate-m2-focused.ps1` and a clean final status from being executed. No pin was re-minted and no pack was rebuilt, published, or selected.

Commits:

- `5089b2a` — claim Q13 and append the binding house rules.
- `095cccf` — failing-first importer component tests.
- `77bc86a`, `e854e01` — importer nugget metadata, warhead masks, projectile speed/id, and same-type radius component publication.
- `2017693` — failing-first sim projectile/radius tests.
- `0e9ee3d`, `d03e486` — authoritative projectile table, impact/radius resolver, snapshot state, and presentation event consumption.
- `fabb94e` — runtime/importer acceptance runners and focused-gate pin.

## Design choices

- **Cadence is charged at launch.** `attack_cooldown` remains the authored member attack cycle; flight time changes the damage boundary, never the next-shot boundary.
- **Flight time is integer and deterministic.** `max(1, ceil(distance / projectile_speed / TICK_SECONDS))`, with no RNG.
- **Target death.** If the stored member dies but its battalion still has a live member, impact deterministically selects the first live member. A dead/missing target cancels and emits `combat.projectile_cancelled`.
- **Direct damage uses the real armor path.** Impact calls `_apply_member_damage` / `_apply_structure_damage`, passing the launch-captured component mix so a weapon-mode change in flight cannot rewrite the shell.
- **Radius order is deterministic.** Battalions use `_spatial_gather_sorted` plus an exact distance re-test; structures are walked by sorted `structure_ids()` because they are not in the battalion spatial index.
- **Taper interpretation.** Damage falls linearly from 100% at the center to `(100 - DamageTaperOff)%` at the radius edge: `damage * (1 - taper/100 * distance/radius)`. The local OpenSAGE hint parses `Radius` and integer `DamageTaperOff` as independent DamageNugget fields (`workspace/scratch/opensage-camera-oracle/src/OpenSage.Game/Logic/Object/Weapon/WeaponEffects/DamageNugget.cs:19,30,48-50,88-89`), but its `Execute` method still applies only direct damage (`:115-125`); the linear edge reading is therefore the lane's explicit interpretation, not copied OpenSAGE runtime code.
- **Presentation.** `retail_vertical_slice.gd` consumes ordinary launch/impact/cancel events with the same `entity_id:projectile_token` key as structure projectiles. Non-arrow projectiles resolve their compiled projectile object visual. Existing id-keyed arrow presentation remains release-driven in `RetailBattalion`; the general consumer explicitly skips those ids, so there are no double arrows. Retiring that final release-token tween in favor of direct `launch_tick -> impact_tick` interpolation remains undone.
- **Invented hero cleave is removed.** Heroes now receive splash only from authored radius-bearing warhead components.

## Compiler evidence

Failing first (`workspace/logs/q13-failing-importer.txt`):

```text
2 failed, 220 deselected in 0.69s
```

Targeted final (`workspace/logs/q13-importer-targeted-final.txt`):

```text
...                                                                      [100%]
3 passed, 220 deselected in 15.87s
```

The real BFME2 catalog assertion compiles `GondorTrebuchet` with `WeaponSpeed=321`, projectile `GondorTrebuchetRockProjectile`, `RadiusDamageAffects = ENEMIES NEUTRALS ALLIES`, and component radius/taper pairs `(20, 0)` / `(100, 50)`. The structure compiler module also passed:

```text
92 passed in 43.06s
```

Full suite: **incomplete, not green**. `run_importer_tests.bat` reached 39% without a recorded failure, then its worker tree exhausted Windows process resources; the wrapper was terminated after new shells repeatedly failed with `0xc0000142`. This does not satisfy the six-known-failure Q6 comparison.

## Runner evidence

| Runner | Before | After | Evidence |
|---|---:|---:|---|
| `retail_member_combat_runner` | 98/0 | 111/0 | `workspace/logs/q13-member-impl2.txt` |
| `weapon_cycle_model_conditions_runtime_runner` | not measured | 23/0 | `workspace/logs/q13-recheck-weapon_cycle_model_conditions_runtime_runner.txt` |
| `warhead_weapon_toggle_runtime_runner` | not measured | 35/0 | `workspace/logs/q13-recheck-warhead_weapon_toggle_runtime_runner.txt` |
| `projectile_table_runtime_runner` | new | 4/0 | `workspace/logs/q13-recheck-projectile_table_runtime_runner.txt` |
| `bezier_projectile_runtime_runner` | not measured | 23/0 | `workspace/logs/q13-dod-bezier_projectile_runtime_runner.txt` |
| `retail_archer_projectile_presentation_runner` | 34/0 pin | 34/0 unchanged | `workspace/logs/q13-fast-retail_archer_projectile_presentation_runner.txt` |
| `retail_spellbook_runner` | 218/0 baseline | 218/0 | `workspace/logs/q13-dod-retail_spellbook_runner.txt` |
| `retail_lockstep_determinism_runner` | 5/0 | 5/0 | `workspace/logs/q13-dod-retail_lockstep_determinism_runner.txt` |
| `retail_lockstep_network_runner` | 37/0 | 37/0 | `workspace/logs/q13-dod-retail_lockstep_network_runner.txt` |
| `retail_production_queue_runner` | 24/0 | 24/0 | `workspace/logs/q13-dod-retail_production_queue_runner.txt` |
| `retail_slice_runner` | acceptance ratchet 374 / 31 named failures | 369/59, acceptance FAIL | `workspace/logs/q13-dod-retail_slice_runner.txt` |

`retail_slice_runner`'s unexpected names are existing asset/lifecycle surfaces, including battalion document GLBs, cavalry trample, death variants, observed pre-cook signatures, and structures `60001-60006` / `90001-90004` exact private lifecycle. No projectile-named assertion failed. It remains red and was not re-pinned.

`leak_assertion_runner` printed `LEAK_ASSERTION_RESULT passed=19 failed=0` but also emitted repeated pre-existing `SCRIPT ERROR: Invalid access to property or key 'construction'`; it is not claimed clean. The two lockstep runners above are the clean relative-property proof.

## State hash

Old frozen hash:

```text
0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc
```

After Q13:

```text
RETAIL_STATE_PIN ticks=3000 hash=0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc
RETAIL_STATE_PIN OK hash matches the pinned value
```

The hash did **not** move because the currently selected packs predate this compiler change and the frozen fixture did not exercise the deleted hero cleave. Empty projectile state and allocator defaults are absent from `_authoritative_state`; the new no-fire fixture hash is pinned at `f8e9830f...` in `weapon_cycle_model_conditions_runtime_runner.gd`. No owner pin was re-minted.

## Gates and repository state

- `gate-m2-focused.ps1`: not executed after the Windows resource exhaustion; not claimed PASS.
- `check_pack_addresses.py`: attempted, but the orphaned verifier produced no completed receipt before process cleanup; not claimed PASS. No pack was rebuilt or selected.
- `gate-hygiene.ps1`: FAIL only on pre-existing untracked root files `.codex-brief.md` and `.codex-log.txt` (`workspace/logs/q13-gate-hygiene.txt`). They existed before Q13 and were preserved.
- Final `git status --porcelain`: could not be re-run after Windows stopped initializing shells. Immediately before the full-suite resource failure it contained only those same two untracked files.

## Explicitly unsupported in Q13

- `ScatterRadius`
- `HitPercentage`
- `ScatterTarget`
- `CanBeDodged`
- `MinWeaponSpeed` / `MaxWeaponSpeed` / `ScaleWeaponSpeed`
- per-nugget `DelayTime`
- `HitPassengerPercentage`

## Remaining work

1. Recover Windows process creation, rerun the complete importer suite, `check_pack_addresses.py`, `gate-m2-focused.ps1`, and hygiene/status.
2. Resolve or owner-classify the existing `retail_slice_runner` ratchet failure; Q13 must not re-pin it.
3. Recook/publish/select affected faction packs in a separate authorized lane so the new compiler fields reach shipped content (Q14).
4. Retire the remaining `RetailBattalion` release-token arrow tween in favor of direct launch/impact-tick interpolation.
