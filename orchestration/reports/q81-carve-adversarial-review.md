# Q81 — sim carve, final state + adversarial review (2026-08-26)

## What shipped

`retail_slice_sim.gd` went from **31,351 lines to a ~7,000-line conductor**
(state declarations, ~940 one-line delegates, setup/config assembly, the tick
order, snapshot/command routing) over **31 subsystem modules**
(`retail_sim_*.gd`, all `extends RefCounted` with a WeakRef back-reference to
the sim). 22 drawers total; drawers 14–22 this session, each committed and
pushed individually with its own proof battery (commits b23f91a6 → 500e2964).

State stays on the sim — every module reads/writes `sim.<member>`; tests and
serialization are untouched. The six oldest modules (projectiles, economy,
experience, ai, combat, persistence) strip the leading underscore on
module-side names; everything later keeps names verbatim.

## Adversarial review (premise: every file was broken down wrong)

1. **Function census** (`workspace/scratch/adversarial-census.ps1`): all
   **1,128** functions of the pre-carve sim (42c20c13) exist today —
   **0 lost**, 0 unimplemented. 3 delegate-only names are intentional
   re-mappings from the original drawers (`_update_enemy_ai`,
   `ai_base_build_order` bake their arguments; `_step_projectiles` maps to
   `projectiles.step()`). **5 documented duplicates**: the passive-area
   helpers exist as sim statics (canonical — tests call them on the class)
   AND as contracts-module copies; dedup deferred, flagged below.
2. **State discipline**: unfiltered scan of all 31 modules — no `var`
   declarations remain in any module (only immutable, module-internal
   consts). Everything the range cuts swept (28 script-world decls, the
   spellbook state block, footprint caches, `local_seat_team`,
   `STRUCTURE_BLOCK_RADIUS`, two subsystem getters) was restored to the sim.
3. **Lifetime discipline**: all 31 modules use
   `_sim_ref = weakref(owning_sim)`; zero strong sim references. The
   script-env "lifetime witness" bug (module `self` passed where the SIM
   must witness) was caught by script_wiring 111→110 and fixed; a full
   bare-`self` sweep found no others.
4. **Encoding**: the pipeline's PS-5.1 double-encoding of UTF-8 comments
   (110 mojibake characters across 5 files) was detected and reversed;
   repo-wide scan is clean.
5. **Final battery — 24/24 green** (`workspace/logs/carve-final-battery.txt`,
   fin-*.txt): state/projectile/pathing pins byte-exact at their ledgered
   values; member combat 115/0; lockstep determinism 5/0 + network 37/0;
   castle boot 10/0, gate 47/0, wall walk 32/0, fixtures 32/0, garrison
   36/0; spellbook 218/0; stage14/15 31/0; script wiring 111/0; executor
   46/0; crush 15/0; formations 34/0; containers 30/0; horde contain 23/0;
   citadel 25/0; passive heal 19/0; physics 20/0; upgrade parity 62/0;
   CAH awards 12/0.

## Defects the carve caused and fixed (all landed)

- 3 typed-array boundary bugs (`[]` literal into `Array[int]` params) — one
  drawer-14, two latent from drawer-13; runtime-fatal, invisible to
  check-only.
- Script-env lifetime witness passed the module instead of the sim.
- Passive-area statics de-static'd in drawer 13 broke
  `passive_area_effect_heal_runner` at parse; original statics restored from
  42c20c13 — runner 19/0 again.
- Comment mojibake (behavior-neutral), reversed.

## Bugs the carve EXPOSED but did not cause (filed with attribution proofs)

Q94 ship_transport 36/2 · Q95 script-world runner 343/82 (Q80 fixture gap) ·
Q96 ring_mechanic 7 fails + hang (Q80 fixture gap) · Q97 ranger 3/2 ·
Q98 hero-ability 131/8, capturable_neutral 3/6, fog 81/10,
castle_skirmish_ai hang (all red at 42c20c13; suspect selected-pack drift) ·
Q98b fire_weapon_when_dead 22/1. Two were fixed on the spot: citadel
runner (Q80 manifest added → 25/0) and passive heal (19/0).

## Known debt (deliberate, ranked)

1. **Oversized modules**: module_contracts 7.3k and powers 4.0k lines —
   next split targets; config 2.1k and script_world 2.4k are cohesive and
   acceptable.
2. **Delegate double-hops**: some sim delegates route through a module that
   itself delegates back to `sim._other_subsystem()` (swallowed stubs from
   append cuts). Behaviorally correct; flatten opportunistically.
3. **Duplicate passive-area helpers** (sim statics canonical; contracts
   copies serve internal callers) — dedup with the contracts split.
