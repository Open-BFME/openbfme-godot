# Projectile + retail radius damage in the ordinary weapon pipeline (Sol lane)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md first (rules 1-10 are
binding). Claim a new row Q13 in orchestration/queue.md (owner=sol-projectiles; Q11/Q12 are taken)
as your first commit. Exclusive tree access. No branches/worktrees; git add by
explicit path; BANNED: git add -A, git reset/restore/clean/stash/amend, and ANY
find-replace sweep across files (rule 7). Long output → workspace/logs/.
Pinned interpreter: workspace\retail-work\tools\python-3.12-env\Scripts\python.exe.
Godot: tools\resolve-godot.bat.

## Why
Every ordinary weapon in the sim is instant hitscan against exactly one target:
`_step_member_attacks` (game/src/retail_slice/retail_slice_sim.gd:26901) applies
`_apply_member_damage` (:27038) on the hit tick with zero flight time and no
radius. Retail siege/AoE is therefore structurally wrong (a trebuchet hits one
battalion). Meanwhile `_apply_hero_cleave` (:27078, constants :27072-27073) is
an admitted invention that gives melee heroes splash retail does not author.
This lane makes ordinary weapons carry a sim-owned projectile with flight time
and applies retail warhead-nugget radius damage through the real armor path.

## Retail semantics (the oracle — workspace/retail-extract/data/ini/weapon.ini)
- BFME2 has NO PrimaryDamageRadius/SecondaryDamageRadius. Splash = one or more
  `DamageNugget { Damage Radius DamageTaperOff DamageType DamageFXType DeathType
  DamageScalar... }` on the WARHEAD, plus `RadiusDamageAffects` on the warhead.
  Trebuchet: `Weapon GondorTrebuchetRock` (:3125) → `ProjectileNugget
  { GondorTrebuchetRockProjectile / GondorTrebuchetRockWarhead }`;
  `GondorTrebuchetRockWarhead` (:3241): inner nugget Radius=20 full damage,
  outer nugget Radius=100 DamageTaperOff=50, `RadiusDamageAffects = ENEMIES
  NEUTRALS ALLIES`. WeaponSpeed=321.
- Archer: `GondorArcherBow` (:4551) WeaponSpeed 321 (Min 241/Max 481,
  ScaleWeaponSpeed=Yes), warhead `GondorArcherBowWarhead` (:4650) has
  `Radius=0.0` + `HitStoredTarget=Yes` — single-target BY DESIGN ("a miss
  hurts nobody"). Fire-arrow warhead (:4678) likewise all Radius=0.
- Melee (`GondorCavalrySword` :5209): MeleeWeapon=Yes, no ProjectileNugget, no
  WeaponSpeed, Radius=0. → melee gets NO projectile and NO radius.
- Scatter (`ScatterRadius`, `HitPercentage`, `ScatterTarget`, `CanBeDodged`) is
  OUT OF SCOPE for this lane (it consumes RNG and would move every state pin).
  Record it as a named unsupported semantic on the row, not as silent behavior.
- OpenSAGE reference (hint only, do NOT copy code; the sim already carries a
  warning at :26202-26207 about a bug copied from it):
  workspace/scratch/opensage-camera-oracle/src/OpenSage.Game/Logic/Object/Weapon/
  {WeaponTemplate.cs, Weapon.cs, WeaponEffects/ProjectileNugget.cs}.


## STATE PIN — read before writing code
`game/tests/retail_state_pin_runner.gd` pins a 3000-tick fixture (`EXPECTED_HASH`
:177, PIN_TICKS :38) whose covered surface explicitly includes combat/damage
(:33-34). Its text (:53-55): "A DIFFERING HASH IS A DEFECT, NEVER A NEW
BASELINE. Re-minting this value is the owner's decision alone and must be
stated explicitly." This lane WILL move that hash (member-attack timing changes;
deleting `_apply_hero_cleave` alone moves it if any hero melees in the fixture).
Rules for this lane:
- You do NOT re-mint. You record old hash → new hash and WHY it moved, in the
  report and on the Q13 row. The owner re-mints separately (see Q5 sequencing
  in orchestration/queue.md).
- `retail_lockstep_determinism_runner.gd` (twin-run agreement, snapshot
  round-trip, selection excluded from hash) MUST stay green — it is the
  relative-property proof and it does not depend on the absolute pin.
- The must-stay-green set beyond your DoD list: menu_skirmish_runner,
  retail_production_queue_runner, retail_lockstep_network_runner. Run them
  before/after; edit only the repo-root tools/gate-m2-focused.ps1 (worktree
  copies under .claude/ are stale mirrors).

## Implementation (shortest viable shape — anchors are current as of HEAD)
1. **Importer** — importer/openbfme_importer/playable_unit_compiler.py
   `_base_weapon_damage` (:1198-1266): each damage component currently keeps
   only `value` + `damageType`. Keep also `radius`, `damageTaperOff`,
   `deathType`, `damageFXType` (whitelist pattern already used by
   spellbook_compiler.py:2091). Surface the warhead's `radiusDamageAffects` on
   `resolved["combat"]` (:1691-1876). Add `WeaponSpeed` to the standalone
   weapon-profile list at :6242 for parity with :1699. Structure twin
   playable_structure_compiler.py:212-272 gets the same component fields.
   Do NOT change any other emitted field; the pack schema is contract-checked.
2. **Sim table** — new `projectiles: Dictionary` + `_next_projectile_id`
   (mirror `physics_objects` :1292 and its allocator). Row: id, attacker_id,
   member_index, target_id, target_kind, origin Vector2, launch_tick,
   impact_tick, projectile_object_id, damage components (with radius/taper),
   damage_type, radius_damage_affects, release token. Stepped from `tick()`
   between `_step_physics_objects()` (:13904) and the entity loop.
   Flight ticks: integer, exactly as :10096-10098
   (`max(1, ceil(distance / projectile_speed / TICK_SECONDS))`), only when the
   compiled row has `projectile_object_id != ""` and `projectile_speed > 0`;
   otherwise (melee / no projectile) keep today's instant path unchanged.
3. **Launch** — in `_step_member_attacks` at :27038: when projectile-capable,
   enqueue a projectile row instead of calling `_apply_member_damage`; keep
   cadence charged from launch (as :10109-10111 documents for structures);
   emit `combat.projectile_launched` with the payload shape of
   :10112-10120 (entity_id, target_id, projectile token, impact_tick,
   projectile_object_id).
4. **Impact resolver** — on `impact_tick`: re-pick a live member if the stored
   one died (as `_resolve_structure_weapon_impact` :10126 does), apply the
   direct hit via `_apply_member_damage` (:28718 — armor/DamageScalar/flanking
   come for free; NOT `_apply_area_damage_to_battalion` :9673 which bypasses
   armor). Then for every nugget with `radius > 0`, call a NEW
   `_apply_radius_damage(attacker_id, origin, radius, amount, damage_type,
   taper_off, affects, exclude_target_id)` shaped like `_apply_hero_cleave`
   (:27078): `_spatial_gather_sorted(origin, radius)` (:736, id-ordered) +
   exact distance re-test + `_apply_member_damage` per victim; plus a
   `structure_ids()` pass (structures are not in the spatial index — see
   :9608-9619) with `_apply_structure_damage`. Honor `RadiusDamageAffects`
   (ENEMIES/ALLIES/NEUTRALS via `_is_hostile`) and DamageTaperOff (linear
   falloff to (100-taper)% at the edge is the OpenSAGE reading — state which
   interpretation you chose and cite it). Emit `combat.projectile_impact`
   (+ existing `combat.hit`). Cancelled targets → `combat.projectile_cancelled`.
5. **Delete `_apply_hero_cleave`** (:27078) and its two constants (:27072-27073).
   Heroes get exactly what their warhead nuggets author.
6. **Lockstep** — add `projectiles` + `_next_projectile_id` to
   `_authoritative_state()` (:31284) using the EMPTY-IS-ABSENT convention
   (:31435-31437 pattern; the frozen 3000-tick pin must stay byte-identical for
   matches that never fire a projectile — see comments :31374-31380) and to
   `_restore_authoritative_state()` (:31532). No RNG consumption. Iterate with
   sorted keys only.
7. **Presentation** — retail_vertical_slice.gd:8164
   `_consume_structure_projectile_events()` is the template: extend it (or a
   sibling) to consume `combat.projectile_launched/impact/cancelled`, keyed
   `"%d:%d" % [entity_id, token]`, using projectile art from
   `openbfme.projectile-art-runtime` (projectile_art_compiler.py) by
   `projectile_object_id`. Retire the release-token-derived tween in
   retail_battalion.gd:1692 `_present_archer_member_attack` / :1962-2106 in
   favour of interpolating launch→impact_tick. If retiring it within this lane
   is too large, leave it wired but behind the sim event (no double arrows) and
   say so.

## Tests — failing-first, then green (rule: fast tests first)
- Extend game/tests/retail_member_combat_runner.gd (synthetic sim, currently
  pinned passed=49 in tools/gate-m2-focused.ps1:46): add checks that (a) a
  projectile-capable weapon does NOT damage on the hit tick and DOES damage on
  impact_tick; (b) flight ticks equal the :10098 formula; (c) a warhead with
  two nuggets (R=20 full, R=100 taper 50) damages a second battalion inside
  R=100 by the tapered amount and one outside by 0; (d) `RadiusDamageAffects =
  ENEMIES` spares allies; (e) melee weapon behaviour is byte-identical to
  before (no projectile row, same damage tick); (f) killing the target
  mid-flight retargets to a live member or cancels. Update the pin in
  gate-m2-focused.ps1 in the same commit with a dated comment.
- game/tests/weapon_cycle_model_conditions_runtime_runner.gd (EXPECTED_CHECKS
  22, asserts weapon-condition queries cannot move state_hash): add a check
  that a match with no projectile fired has an UNCHANGED state_hash vs the
  pre-lane value (prove EMPTY-IS-ABSENT); bump EXPECTED_CHECKS.
- game/tests/warhead_weapon_toggle_runtime_runner.gd (reads real retail ini):
  add an assertion that the compiled GondorTrebuchetRockWarhead carries two
  damage components with radius 20/100 and taper 0/50.
- New game/tests/projectile_table_runtime_runner.gd modeled on
  bezier_projectile_runtime_runner.gd: snapshot/restore round-trip with
  projectiles in flight reproduces identical state_hash.
- Importer: a pytest in importer/tests/ asserting the compiler now emits
  radius/damageTaperOff/deathType/damageFXType components for a fixture
  weapon and still emits identical output for a Radius=0 melee weapon.
- Re-pin retail_archer_projectile_presentation_runner (34/0 in
  gate-m2-focused.ps1:16) only if you changed the presenter; say so.

## Definition of Done (verbatim outputs in your report)
1. Importer targeted pytest (pinned interpreter, BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2) on the new/changed test modules → green; then the FULL importer suite → judged failure-by-failure vs the 6-failed/0-errors baseline (orchestration/queue.md Q6 lists them): zero new failures.
2. Runners green with new pins: retail_member_combat_runner, weapon_cycle_model_conditions_runtime_runner, warhead_weapon_toggle_runtime_runner, projectile_table_runtime_runner, bezier_projectile_runtime_runner (unchanged), retail_archer_projectile_presentation_runner, retail_spellbook_runner 218/0, retail_slice_runner (its RETAIL_SLICE_ACCEPTANCE line ≥ its current ratchet; name any moved known-failure).
3. `powershell -ExecutionPolicy Bypass -File tools\gate-m2-focused.ps1` → PASS.
4. Lockstep proof: game/tests/leak_assertion_runner.gd or the lockstep desync runner if one exists (search game/tests for "lockstep"/"desync") passes; state pin runners: if `retail_state_pin_runner` hash moves, that is EXPECTED because member combat now differs — record old/new hashes and DO NOT re-mint pins in this lane (Q5 owns pin policy); state plainly in the report.
5. `python tools\check_pack_addresses.py` PASS (you rebuilt no packs — this lane changes compiler output but does NOT republish; note that the change reaches shipped content only on the next recook, and add that as a queue row).
6. `powershell -ExecutionPolicy Bypass -File tools\gate-hygiene.ps1` PASS; `git status --porcelain` clean; commits prefixed `feat(sim):` / `feat(importer):` / `test(...)`, explicit paths, no logs.
7. Report orchestration/reports/projectile-splash-pipeline.md: design choices (taper interpretation, cadence charged at launch, what happens on target death), before/after runner table, hash movement, named unsupported semantics (scatter, HitPercentage, CanBeDodged, MinWeaponSpeed/MaxWeaponSpeed/ScaleWeaponSpeed, DelayTime per nugget, HitPassengerPercentage), and anything left undone.
