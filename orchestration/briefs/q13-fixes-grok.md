# Q13 fixes — four blocking review findings + gate rows

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md, then
orchestration/reports/q13-verify.md (the verifier's findings — ground truth),
then orchestration/reports/projectile-splash-pipeline.md (implementor report).
Exclusive tree access. Surgical per-file edits; NO sweeps; git add by explicit
path; BANNED git add -A / reset / restore / clean / stash / amend. Long output
→ workspace/logs/. Pinned interpreter
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe;
BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2.
Run pytest WITHOUT parallelism and Godot runners sequentially (the machine ran
out of process handles once today).

## Owner ruling on finding 1 (damage total)
`combat.damage.value` = SUM of every warhead DamageNugget's damage at zero
distance (retail SAGE applies each nugget independently; a target inside both
trebuchet discs takes both). So GondorTrebuchet 780 is CORRECT and stays. What
must be fixed is that the change was silent and lossy:

## Fix list
1. **Importer provenance + pin** (importer/openbfme_importer/playable_unit_compiler.py,
   `_base_weapon_damage` ~:1198-1266 and the warhead path from e854e01):
   restore the top-level `expression` / `sourceIni` / `line` fields on
   `combat.damage` that the old compiler emitted (A/B against
   `git show d47d1f6:importer/openbfme_importer/playable_unit_compiler.py` to see
   the exact prior shape). Add a `valueSemantic` string on `combat.damage`
   ("sum-of-nugget-damage-at-zero-distance"). Add/extend an importer pytest that
   pins GondorTrebuchet damage.value == 780 with two components (radius 20 taper
   0; radius 100 taper 50) AND pins that a single-nugget weapon (GondorFighter)
   compiles IDENTICALLY to d47d1f6 output (byte-identity of its combat block).
2. **`damageTypeSemantic` truthfulness**: `_apply_nugget_damage_types` became
   unconditional in three places (verifier names them) and now writes "the
   weapon authors no flat DamageType" on weapons that DO author one. Make the
   semantic string reflect the actual source (flat vs nugget-derived) and only
   re-assign `damageType` from nuggets when no flat DamageType is authored (or
   when they agree). Pytest: one weapon with flat DamageType, one nugget-only.
3. **Latent wipe** (game/src/retail_slice/retail_slice_sim.gd `_apply_weapon_mode`):
   it unconditionally sets `row["damage_components"] = []` every attack tick.
   Change so damage_components are (re)derived from the selected weapon mode's
   compiled components, never blanked when the mode carries components. Add a
   check to retail_member_combat_runner: a battalion whose unit rule carries
   two damage components still has both after N ticks of attacking and after a
   weapon-mode toggle. Re-pin the runner count in tools/gate-m2-focused.ps1
   with a dated comment (repo-root copy only).
4. **Focused-gate step regex**: the `projectile_table_runtime` step Q13 added
   to tools/gate-m2-focused.ps1 cannot match the gate's own result regex — fix
   the marker/regex so it can (see verifier's note), and correct the misplaced
   comment the verifier flagged (the "13→40" note belongs to
   member_health_overlay; member_combat is 98→111 — say so accurately).
5. **Queue rows** (orchestration/queue.md): add Q15 "tools/gate-m2-focused.ps1
   dead since Q1: tools/m2-oracle-common.ps1:36 requires activePack
   ^bfme2-men-vslice/; selection is now rotwk-men-vslice — decide: retarget the
   gate to the RotWK Men pack or retire it" (status DECISION, owner unassigned).
   Add Q16 "retail_state_pin fixture is synthetic and fires no ranged weapon;
   pin does not cover projectile combat — extend fixture (conscious re-mint) or
   add a second projectile-covering pin" (READY). Update Q13 row: fixes landed,
   awaiting re-verify.
6. Do NOT touch: pins other than the ones named; selection; packs; the state
   pin.

## Definition of Done (verbatim outputs in your report)
1. Targeted pytest (new/changed importer test modules) green; then FULL importer
   suite (sequential) → judged failure-by-failure: exactly the 6 Q6 names, 0
   errors. Log workspace/logs/q13fix-importer-full.txt.
2. Runners green with pins: retail_member_combat_runner (new count),
   weapon_cycle_model_conditions_runtime_runner 23/0,
   warhead_weapon_toggle_runtime_runner 35/0, projectile_table_runtime_runner
   4/0, retail_spellbook_runner 218/0, retail_lockstep_determinism_runner 5/0,
   retail_state_pin_runner UNCHANGED 0e4bcdbf….
3. A/B compile evidence: GondorFighter combat block byte-identical to d47d1f6;
   GondorTrebuchet damage.value 780 with two radius components and provenance
   fields present.
4. `python tools\check_pack_addresses.py` PASS packs=200 roots=2;
   `tools\gate-hygiene.ps1` PASS; git status clean; commits prefixed
   `fix(importer):` / `fix(sim):` / `chore(queue):`, explicit paths.
5. Report orchestration/reports/q13-fixes.md.
