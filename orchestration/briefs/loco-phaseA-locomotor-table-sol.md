# Locomotion — Phase A: compile the full Locomotor template table, delete every movement fudge (Sol)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md (rules 1-10 binding;
rule 7 NO sweeps). Claim row Q20 in orchestration/queue.md (owner=sol-loco-A).
Exclusive tree access; no branches/worktrees; git add by explicit path; BANNED
git add -A / reset / restore / clean / stash / amend. Long output →
workspace/logs/. Pinned interpreter
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe;
BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2.
Sequential pytest / sequential Godot. Do NOT rebuild/publish/select packs.

## Why
Retail authors 123 Locomotor templates (workspace/retail-extract/data/ini/
locomotor.ini); base objects bind 94 of them via 693 `LocomotorSet { Condition
= SET_*; Locomotor = X; Speed = n }` references (Speed lives on the OBJECT).
The importer keeps 4-6 fields and drops the rest; the sim fills gaps with
invented constants. This phase makes movement DATA-driven: full template table
compiled with provenance, per-object binding, and every fudge deleted so an
unauthored value is a loud compile error, not a guess. Position stays Vector2;
NO serialization/shape change to hashed rows beyond the new authored fields.

## Oracle facts (anchors verified 2026-08-17)
- Templates: HumanLocomotor :142 (Surfaces=GROUND RUBBLE, TurnTime=500,
  TurnTimeDamaged=500, FastTurnRadius=3, SlowTurnRadius=1, Acceleration=510,
  Braking=510, FormationPriority=MELEE1, MinTurnSpeed=0%, ZAxisBehavior=
  NO_Z_MOTIVE_FORCE, Appearance=TWO_LEGS, StickToGround=Yes, CanMoveBackwards=
  Yes, BackingUpSpeed=33%); HorseLocomotor :1026 (TurnTime=1500, Accel=1500,
  Braking=2000, Slow/FastTurnRadius=0/48, TurnPivotOffset=1, MinTurnSpeed=10%,
  AccDecTrigger=0.5, Appearance=FOUR_WHEELS, CloseEnoughDist=2.0,
  CanMoveBackwards=No); CatapultLocomotor :1683 (Slow=Fast TurnRadius=24,
  TurnTime=1000, Accel=Braking=1000, MinTurnSpeed=66%, TurnPivotOffset=-0.264,
  UseTerrainSmoothing=Yes …); FellBeastLocomotor :2334 (Surfaces=AIR,
  ZAxisBehavior=SURFACE_RELATIVE_HEIGHT, PreferredHeight=121,
  PreferredAttackHeight=5, Lift=100%, TurnTime=3500, Accel=400, Braking=1000,
  MinTurnSpeed=30%, SlideIntoPlaceTime=900, Swoop* …).
- Field census across the file: Surfaces, ZAxisBehavior, MinTurnSpeed,
  Appearance, Braking, Acceleration, TurnTime, StickToGround, CanMoveBackwards,
  FastTurnRadius, TurnPivotOffset, FormationPriority, HasSuspension,
  CloseEnoughDist, PreferredHeight, Lift, MaxTurnWithoutReform, WaitForFormation,
  ScalesWalls, ChargeSpeed, SlideIntoPlaceTime, MaxThrustAngle (1) — compile
  ALL of them; unknown keys go to `unsupported` with line numbers.
- Condition histogram in object INIs: SET_NORMAL 422, SET_WANDER 120,
  SET_BURNINGDEATH 60, SET_PANIC 34, SET_MOUNTED 18, SET_SCARED 15.
- Importer readers today (all narrow): playable_unit_compiler.py:1736-1775
  (locomotor id resolution :1703-1717), retail_unit_rules.py:536-567,
  cah_system_compiler.py:1296-1305 (only place turn radii survive, CAH-only),
  spellbook_compiler.py:1719-1774, ring_system_compiler.py:206,
  m3_pack_expansion.py:1539. OpenSAGE TurnTime semantic already cited at
  retail_unit_rules.py:954.
- Sim fudges to DELETE (game/src/retail_slice/retail_slice_sim.gd unless noted):
  acceleration = max_speed*10 :28550-28552; braking = max_speed*10 :28554-28555;
  RETAIL_FALLBACK_TURN_RATE_DEGREES 180 :28455 (used :28496);
  RETAIL_MAX_TURN_WITHOUT_REFORM_DEGREES 45 :28459 and the cavalry 100 :28460
  (category-keyed — replace with authored MaxTurnWithoutReform where authored;
  where retail authors none, the behaviour must be "no reform gate", stated
  explicitly, not a guessed number); HORDE_LOCOMOTION_RESPONSE_SCALE 1.5
  (playable_unit_runtime_adapter.gd:872, applied :916-917; retail_slice_sim.gd
  :7319-7323; retail_vertical_slice.gd:1518) — a pure fudge on authored values;
  turn_rate 360.0 defaults for Gollum :3158 and trebuchet :4161 ("Provisional")
  and the .get() default :7321; acceleration=braking=speed for Gollum
  :3156-3160 and trebuchet :4159-4166. Movement fields are inside state_hash
  (entities are in the dynamic half, :31567), so this WILL move movement pins —
  that is expected and recorded, not re-minted by you.

## Deliverable
1. Importer: a single `locomotor_compiler.py` (or extension of the existing
   locomotor reader — pick ONE canonical reader and make the other five call
   it) that emits an interned locomotor template table (`locomotors/<name>` or
   the house doc convention) with every field + provenance, and per-object
   `locomotorSets: {SET_NORMAL: {locomotorId, speed, sourceIni, line}, …}`.
   Speed stays per-object. Unknown fields → `unsupported`.
2. Runtime adapter + sim: read acceleration/braking/turnTime/turnTimeDamaged/
   turn radii/minTurnSpeed/turnPivotOffset/maxTurnWithoutReform/canMoveBackwards
   /backingUpSpeed/closeEnoughDist/surfaces/zAxisBehavior/appearance/
   formationPriority from the compiled binding. Delete every fudge listed
   above. Where a unit has NO authored value for a field the mover needs, fail
   loudly at load (push_error + `unsupported_semantics` row) — never invent.
   Do NOT implement new turn physics in this phase (that is Phase B): the mover
   keeps its current math but consumes authored inputs only.
3. Presentation: HORDE_LOCOMOTION_RESPONSE_SCALE removal will change feel;
   that is correct — retail values are the oracle.

## Tests — failing-first
- importer/tests/test_locomotor_compiler.py: golden extraction of the four
  templates above (exact values); census 123 templates / 94 referenced / 693
  refs (verify counts yourself; assert what you measure and cite);
  per-object binding for GondorFighter (HumanLocomotor, SET_NORMAL speed) and a
  cavalry + siege + flyer object; unknown-key preservation.
- game/tests: extend the movement runner(s) that cover
  _step_route/_step_retail_heading (find them: grep game/tests for
  "retail_formation_movement", "turn_rate", "acceleration") with: (a) a
  no-fallback assertion — spawning every unit in the selected Men pack yields
  zero "unauthored locomotor" diagnostics; (b) a horse row's acceleration is
  exactly 1500-derived (no 1.5×); (c) trebuchet turn rate derives from
  TurnTime=1000, not 360.
- retail_lockstep_determinism_runner must stay 5/0; retail_state_pin_runner:
  record hash before/after (fixture is synthetic — likely unmoved; say so).

## Definition of Done (verbatim outputs)
1. New tests green; FULL importer suite (sequential) → exactly the 6 Q6 names,
   0 errors (log workspace/logs/loco-A-importer-full.txt).
2. Runners: the movement runner(s) with re-pinned counts (repo-root
   tools/gate-m2-focused.ps1 only, dated comment), retail_member_combat_runner
   115/0, retail_spellbook_runner 218/0, retail_lockstep_determinism_runner 5/0,
   retail_slice_runner failure NAMES unchanged vs workspace/logs/q13verify-*
   (name any delta), state pin hash recorded.
3. `git grep -n "HORDE_LOCOMOTION_RESPONSE_SCALE\|max_speed \* 10\|RETAIL_FALLBACK_TURN_RATE_DEGREES"`
   → zero hits.
4. check_pack_addresses PASS; gate-hygiene PASS; git status clean; commits
   `feat(importer):` / `feat(sim):` / `test(...)`, explicit paths, no logs.
5. Report orchestration/reports/loco-phaseA.md: field coverage table (compiled
   vs unsupported), fudges deleted with anchors, before/after runner table,
   pins moved (old→new), and the Phase B seam (turn model) with anchors. Add
   queue row Q21 for Phase B.
