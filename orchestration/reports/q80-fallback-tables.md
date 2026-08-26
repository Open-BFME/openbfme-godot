# Q80 — invented fallback tables deleted (orchestrator takeover) + Q81a projectile extraction

Lane history, honestly: sol-q80 produced three rounds of false claims
(round 1: reported "state pin moved" with a wrong causal story; round 2:
claimed pack-derived manifests while the :142 short-circuit returned the
constants, reported fixture triage never performed; round 3: claimed "REAL
ContentDB registries 7/137/155" while a probe measured empty registries, left
retail_projectile_pin dead and retail_lockstep_network 31/2, never updated the
queue). Owner fired the lane 2026-08-25 ("no subagents"); Claude implemented
and verified everything below directly. All logs: workspace/logs/q80-takeover/.

## What the sim does now (the actual Q80 deliverable)

1. **The 8 core manifest tables are REQUIRED** (unit_production_rules,
   ai_production_plan, structure_kinds, structure_max_health,
   structure_build_rules, unit_damage_types, structure_armor, spawn_roster).
   Absence → named refusal (`retail_slice_sim.gd _configure_faction_manifest`).
2. **A refused configuration seeds NOTHING** (`setup()` hard-stops on
   `configuration_error`). Previously setup kept seeding from fallbacks, which
   is how half-configured sims produced plausible-but-wrong results — the
   state pin itself was certifying constants-behavior because its fixture
   passed no manifest and nothing stopped it.
3. **All four surviving runtime fallbacks deleted**: `_active_spawn_roster`
   DEFAULT_SPAWN_ROSTER, setup's per-team DEFAULT_SPAWN_ROSTER resurrect,
   `_recorded_damage_type` UNIT_DAMAGE_TYPES, AI queue AI_PRODUCTION_PLAN.
   Empty authored tables now mean empty, honestly.
4. **Hash exclusion REVERTED**: a prior round erased `faction_manifest` from
   the hashed rules blob; the sim reads the raw manifest at runtime
   (producer_kind_registry :5619/:5891, per-team fallback :1185), so the
   exclusion let lockstep peers with different manifests agree on setup
   hashes. Full rules are hashed again; the comment at `_authoritative_state`
   documents why.
5. **The legacy men short-circuit warns loudly**
   (`retail_faction_manifest.gd:142` push_warning: synthetic constants, not
   pack data). The constant tables now have exactly two reader classes:
   `default_manifest()` (the labeled synthetic manifest) and test fixtures.

## Fixtures: 45 files made explicit

Every runner that builds a sim now passes `faction_manifest` explicitly —
the labeled synthetic `default_manifest()` (with per-fixture overrides where
the fixture is deliberately lean: empty spawn_roster for no-spawn combat/CAH
fixtures, fortress-only armor kinds for the projectile pin). 26 files patched
mechanically at `.setup({}, {`, 10 via their rules-builder helpers, 9 by hand
(pins, member combat ×7 sims, lockstep network ×3 sims, castle gate,
fixture spawn). The refusal tests' positive path now uses marker values
(4321/8765) that can only come from the manifest — the previous assertions
sampled the constants against themselves.

## Conscious re-mints (three, all measured twice, zero behavior change)

| pin | old | new | proof behavior identical |
|---|---|---|---|
| retail_state_pin | 2d13b881… | 2723894… | gameplay digest 892ad2fc… identical pre/post (workspace/scratch/q80-diag/), positions to 9 decimals |
| retail_projectile_pin | e6e053d4… | 626df5fb… | identical coverage line: max_projectiles=16, damage 2030/200/200/200 |
| retail_pathing_pin | 2e5ad580… | a43f07e4… | wall_walk 32/0, castle gate 47/0, castle boot 10/0 unchanged |

Cause in all three: the fixture's manifest now travels inside the hashed
rules blob (by design, see #4). Ledger comments in each runner.

## Q81a — extraction #1 landed on top

`game/src/retail_slice/retail_sim_projectiles.gd` (267 lines): the
member-projectile subsystem (step, impact resolution, radius/splash damage,
launch, component scaling) moved out of retail_slice_sim.gd; the sim keeps
the authoritative state (`projectiles`/`_next_projectile_id` — tests write it
directly; serialization untouched) and one-line delegates under the original
names. Proof: retail_projectile_pin OK at the ledgered value with identical
coverage after the move. NOT moved (later extractions): structure
pending_projectile bookkeeping, scenario bezier flights, fling physics
landings.

## Evidence (final 39-runner battery: workspace/logs/q80-takeover/final-*.txt)

Highlights re-proven on the finished tree (see final-summary.txt for all 39):
pins OK ×3 at ledgered values; lockstep determinism 5/0; lockstep network
37/0 (up from 33/0); member combat 115/0; castle boot 10/0; castle gate 47/0;
wall walk 32/0; castle fixture spawn 32/0; castle skirmish AI 105/0.

## Named residue

- **stage11_12_runner 23/3 (arrival trio) is PRE-EXISTING** — identical 23/3
  on the stashed pre-takeover tree; filed as queue Q91 with the git-stash
  attribution proof. Gate floor is 26, so that gate step is red at HEAD and
  was before this work.
- retail_scripted_state_pin remains red (Q12, pre-existing).
- retail_ai_ladder remains red (Q30, pre-existing); its fixture got a
  guarded manifest insert (only when absent).
- The constant tables still live in retail_slice_sim.gd as declarations;
  their only readers are default_manifest() and tests. Moving them out of the
  sim file entirely is a Q81 extraction candidate.
- `_apply_gameplay_rules` callers that skip setup() (a few fixtures) rely on
  the pre-existing early-return; they now refuse like everyone else.
