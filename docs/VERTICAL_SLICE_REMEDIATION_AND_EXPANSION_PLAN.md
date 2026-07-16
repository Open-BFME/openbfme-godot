# Men/Fords vertical-slice remediation and BFME2 expansion plan

**Date:** 2026-07-15  
**Scope authority:** `DIRECTION.md` and `docs/M2_MEN_FORDS_DOD.md`  
**Milestone:** private BFME2 1.06, Men versus Men, Fords of Isen II

## Verdict

The slice is not ready for an M2 completion declaration. The reported issues
are not optional polish: they expose missing combat semantics, command
feedback, unit behavior, corpse lifetime, retail radar composition, the
builder/build loop, and live building audiovisual execution.

The existing green assertions prove many selected-pack contracts and runtime
routes, but several prove only that data is present or that a request can be
recorded. They do not yet prove the retail behavior the player sees and hears.
The 47-pair visual oracle also does not cover the newly identified behaviors.

This plan keeps the current hard scope cut: BFME2 1.06 Men versus Men on Fords.
No other faction, map, campaign, War of the Ring, or multiplayer work should
preempt the remediation gates below.

## Current gap audit

| Area | Current implementation | Required result | Priority |
| --- | --- | --- | --- |
| Archer weapons | `retail_unit_rules.py` selects one base primary weapon. Runtime stores one range, damage, timing set, damage type, and attack clip per unit. | Import every applicable Archer `WeaponSet`/weapon condition needed for ranged and close combat. Select the weapon by source conditions and distance. Use separate INI-derived damage, range, timing, projectile, audio, and animation contracts. | P0 |
| Archer presentation | The selected profile exposes one Archer attack clip and the projectile presenter fires whenever a member attack token changes. | Ranged attacks play the retail bow attack/release presentation and create an arrow only at the proven fire event. Close-range attacks play the retail melee animation, create no arrow, and resolve the melee weapon. | P0 |
| Individual combat | Health, attack tokens, and damage application are per member, and starts are staggered. Target choice is a deterministic index hash, while facing and projectiles still aim at the battalion root. | Each living attacker owns a target-member assignment, faces that member, attacks its world transform, retargets when it dies or becomes invalid, and applies damage to that exact member. Formation-level orders remain authoritative. | P0 |
| Volley stagger | A fixed four-tick window currently yields only 0.0-0.3 seconds of deterministic staggering. Structure hits are deliberately collapsed onto a common volley boundary. | Derive the admissible timing/random-delay behavior from retail data and an original-game oracle. Keep deterministic replay, but prevent synchronized whole-horde firing and preserve individual structure impacts where retail does. | P0 |
| Attack feedback | Right-click attack changes simulation state and HUD text only. The existing order indicator is a ground movement hint. | Show the source-backed hostile target/order marker above the acquired battalion or structure immediately after an accepted right-click attack. Bind it to the authoritative target, hide it on cancel/death/retarget, and do not reuse a movement flag. | P0 |
| Stances | There is no stance field, stance command, stance UI, modifier evaluation, or idle auto-acquisition. Attack Move is an order, not a stance. | Implement the three retail Men stances as persistent battalion state. Extract their exact command buttons, modifiers, acquisition/chase rules, and transitions. Idle auto-acquisition must follow stance rules without overwriting explicit player orders. | P0 |
| Corpses | A dead battalion remains in `entities`; its presentation remains allocated after death. There is no death timestamp, corpse phase, despawn, or distance/budget culling. | Preserve the authored death animation and corpse for 60 seconds by default, then remove it deterministically. Add off-camera update throttling, a global corpse budget, oldest/off-camera eviction, and clean disposal of projectiles, impacts, overlays, skeletons, and nodes. | P1 |
| Radar | `retail_minimap.gd` explicitly draws a schematic circle, flat map polygon, water polygons, dots, and a guessed camera footprint. It does not use the retail radar/map raster shown in the ShareX reference. | Reproduce the retail circular Palantir/radar: exact frame and buttons, parchment/map raster or a source-proven runtime render, orientation/crop, camera trapezoid, player colors, unit/structure markers, fog/shroud, alerts, and coordinate-accurate clicking. | P0 |
| Building sound | Lifecycle audio IDs can be dispatched, but the live build must still prove audible selection, construction, ambient, damage, collapse, and destruction routes with source range/priority/loop behavior. | Every one of the five Men buildings audibly exercises all source-required events in a live capture. Missing or rejected routes fail the slice instead of becoming silent. | P0 |
| Building animation | Lifecycle GLBs and declared clip selection exist, but exact construction, production-door, ambient, damage, collapse, and rubble timing remains partly oracle-blocked. | Drive every authored clip from deterministic simulation events and source/oracle timing. Construction progress scrubs the correct clip; production uses doors; damage and collapse reach authored terminal states. | P0 |
| Building clutter/FX | Exact FX and particle IDs are catalogued, but current documentation says dynamic emitters, follow-bone behavior, condition timing, FX offsets, sound, and view shake are not implemented. | Implement the selected source particle families and FX-list executor needed by the five structures: debris, dust, fire/smoke, collapse clutter, bone following, offsets, start/stop conditions, sound, and view shake. No generic substitute. | P0 |
| Builder/build loop | The match seeds all five structure roles. There is no Men builder entity, builder command set, placement ghost, construction task, or builder participation. | Start with the source-correct Men builder/build authority. The player selects a builder, opens the retail construction command set, places valid buildings, pays cost, constructs them over source time, can cancel, and reaches production/economy through built structures. AI uses the same rules. | P0 |

## Scope changes to the M2 definition of done

Before M2 can pass, `docs/M2_MEN_FORDS_DOD.md` must be tightened in these ways:

1. Replace “place or use the five structure roles” with a required builder-led
   construction loop for both player and AI.
2. Make dual ranged/melee Archer weapon selection, animations, values, and
   projectile suppression a required gameplay and presentation gate.
3. Require spatial per-member targets and individually timed attacks, not just
   per-member health arrays and attack tokens.
4. Require all three source stances, their UI, modifiers, chase/acquisition
   behavior, and persistence.
5. Require a source-backed accepted-attack target marker.
6. Require a 60-second corpse lifecycle plus allocation/culling limits.
7. Replace the generic “radar is bound to the pack” assertion with rendered
   retail-radar parity and coordinate tests.
8. Require live audible and visible proof for all five building lifecycles,
   including dynamic FX, construction, doors, collapse, and rubble.

“Full micro-parity for every weapon timing” can remain a broader non-goal, but
the weapon mode, damage/range/timing values used by this roster, member-level
attack behavior, and the visibly correct animation/event frame are no longer
outside the slice.

## Ordered remediation program

Each package ends with a focused gate. Do not defer all behavior validation to
the final 30-minute soak.

### R0 - Freeze and measure the failing baseline

**Goal:** make every reported defect reproducible against one identity tuple.

- Freeze git dirty-state digest, selected profile SHA, selected bundle SHA,
  viewport, renderer, and input sequence.
- Add retail/Godot capture scenarios for: Archer ranged attack, Archer close
  attack, staggered volley, member versus member melee, each stance, accepted
  target marker, radar default/click mapping, corpse at death/59s/61s, builder
  placement/construction, and each building lifecycle.
- Record live audio event IDs and accepted/rejected route results while also
  verifying that audible output occurred.
- Do not approve the existing 47-pair oracle while these severity-0 gameplay
  and audiovisual gaps remain.

**Exit:** deterministic reproduction script and unapproved evidence rows exist
for every gap.

### R1 - Source-complete combat contract

**Importer work**

- Generalize `retail_unit_rules.py` from one base primary weapon to an ordered
  weapon-set contract containing source conditions, weapon slot, range,
  minimum range, damage nuggets/warheads, damage type, preattack, firing,
  between-shot timing, projectile, audio, and model-condition consequences.
- Close the Archer's ranged and close-combat animation leaves and prove the
  animation-state/model-condition mapping from the effective INIs and a retail
  runtime capture.
- Fail if two applicable weapon sets are ambiguous or a required leaf is
  missing.

**Simulation work**

- Store a weapon-mode state per member, not one weapon on the battalion.
- Evaluate the source condition set at attack time. Close range selects melee;
  ranged distance selects bow. Add hysteresis only if retail evidence proves
  it.
- Store `member_target_id`/`member_target_index`, attack phase, next allowed
  attack tick, and deterministic stagger seed per living member.
- Assign targets spatially using member world positions and reachable frontage;
  retain valid targets and retarget deterministically on death/separation.
- Apply damage to the assigned member. Aim/facing and projectile impact use the
  target member's world transform.

**Presentation work**

- Split Archer ranged and melee animation states.
- Spawn arrows only from the source-proven ranged fire event. Melee never calls
  the projectile controller.
- Preserve per-member animation phase variation and deterministic stagger.

**Focused gates to add**

- `retail_archer_dual_weapon_runner.gd`
- `retail_member_targeting_runner.gd`
- `retail_member_attack_timing_runner.gd`

**Exit:** ranged and close attacks use different INI-derived values and retail
clips; 15 attackers can visibly engage multiple enemy members without one
root-aimed synchronized volley.

### R2 - Commands, target acknowledgement, and stances

- Extract the exact stance command definitions, modifiers, button art, labels,
  keyboard bindings, and source behavior for the three stances.
- Add persistent stance state to battalion snapshots and deterministic save/
  replay state.
- Separate explicit attack, attack-move, idle acquisition, chase, hold/return,
  and stop semantics. Explicit orders always have clear precedence.
- Import and display the retail hostile target acknowledgement marker above
  the actual target entity. Associate it with the accepted order sequence.
- Expose stance buttons through the existing retail HUD host bridge and show
  the active state.

**Focused gates to add**

- `retail_stance_behavior_runner.gd`
- `retail_attack_target_indicator_runner.gd`

**Exit:** all three stances have observably different source-backed behavior;
right-click attack has immediate, correct, self-clearing target feedback.

### R3 - Corpse lifecycle and combat allocation budget

- Add `death_tick`, `corpse_expire_tick`, and presentation phase to individual
  members. Do not treat a defeated battalion root as a permanently live entity.
- Default retention is exactly 600 simulation ticks (60 seconds at the current
  fixed 0.1-second tick) after death presentation begins, unless the retail
  oracle proves a different timer for this roster.
- Stop AI, navigation, acquisition, collision, and combat updates immediately;
  retain only the minimum death/corpse presentation state.
- Pool or instance-share static corpse presentation where safe. Throttle
  off-camera animation/skeleton updates, cap retained corpses, and evict oldest
  off-camera corpses first when the budget is exceeded.
- Dispose target markers, arrows, impacts, health overlays, audio handles, and
  member nodes without orphan/RID diagnostics.

**Focused gate to add:** `retail_corpse_lifecycle_runner.gd`, plus a stress scene
with several hundred deaths and fixed memory/FPS thresholds.

**Exit:** normal corpses remain visible through 59 seconds, are absent after 60
seconds, and a mass battle has bounded node count and memory.

### R4 - Retail radar replacement

- Treat the latest ShareX retail radar capture as the initial visual reference,
  not as sufficient coordinate evidence by itself.
- Extract the exact HUD frame/buttons from the selected APT/WND/mapped-image
  closure and identify the retail map-radar raster generation path.
- If retail uses a precomputed map image, convert and bind it. If it renders a
  source-derived radar texture, implement that exact layer composition. Do not
  keep the current flat polygon schematic under retail framing.
- Prove map north/orientation, aspect/circular crop, playable bounds, world to
  radar transform, camera footprint shape, unit and structure icon classes,
  team colors, fog/shroud, alert pings, zoom behavior, and click recentering.
- Use fixed world landmarks and both player starts as coordinate fixtures.

**Focused gates to add**

- `retail_radar_mapping_runner.gd` for numeric round trips and landmark error.
- Identity-bound `hud-radar-default`, `hud-radar-units`, `hud-radar-shroud`,
  and `hud-radar-click` retail/Godot image pairs.

**Exit:** no procedural schematic remains in the private parity route and radar
click error stays within a frozen world-space tolerance at the center and rim.

### R5 - Building audiovisual and destruction execution

- Promote the selected Men lifecycle contracts to the only live authority and
  verify that every one of the five runtime objects actually receives them.
- Finish cross-family particle precedence using retail runtime evidence.
- Implement the bounded FX-list/particle executor needed by the five
  structures: dynamic emitters, follow-bone transforms, offsets, authored
  ordering, condition-driven start/stop, audio edges, view shake, and cleanup.
- Drive construction, idle/ambient, production-door, damaged, really-damaged,
  collapse, rubble, and terminal transitions from deterministic simulation.
- Route selection, construction loop/start/stop, ambient, damage, collapse,
  and destruction audio with source attenuation, probability, concurrency,
  priority, and attack/body/decay loop semantics.
- Add structure debris/clutter budget and culling separate from unit corpses.

**Focused gates to add**

- Extend `retail_structure_lifecycle_runner.gd` from contract checks to live
  transition timing.
- Extend `retail_structure_damage_effects_runner.gd` to assert rendered emitter
  creation, bone following, stop conditions, and cleanup.
- Add `retail_structure_audio_live_runner.gd` with loop and attenuation probes.
- Add five identity-bound construction/damage/collapse/rubble audiovisual
  sequences, not single still frames only.

**Exit:** all five structures visibly animate and shed source clutter/effects,
and every required sound is audible in a live match with no rejected route.

### R6 - Men builder and real construction loop

**Content/importer**

- Add the source Men builder object/horde if applicable, model, equipment,
  locomotor, command set, portrait, build buttons, voices, animations, and all
  five building cost/time/placement contracts.
- Extract terrain, slope, overlap, build-radius, fortress/plot, cancellation,
  refund, and builder participation rules used by this slice.

**Simulation/UI**

- Spawn the source-correct starting builder authority instead of gifting all
  completed structures.
- Add select, move, build-menu, placement preview, rotate if source-supported,
  confirm, cancel, resource reservation/refund, construction task, assistance,
  interruption, death, and completion states.
- Replace generic placement art with source-backed footprint/radius/validity
  feedback.
- Make player and AI use the same build command path and legality checks.
- Keep only the starting Fortress or other initial structures proven by the
  exact retail skirmish start; do not use seeded completed buildings to bypass
  construction.

**Focused gates to add**

- `retail_builder_command_runner.gd`
- `retail_build_placement_runner.gd`
- `retail_builder_construction_runner.gd`
- `retail_ai_build_loop_runner.gd`

**Exit:** from a clean retail-authentic start, both sides can build the five
roles and then train/fight/destroy a Fortress without test-only spawning.

### R7 - Rebaseline and final integration gate

- Update the exact M2 runner counts only after the new gates exist; never weaken
  old assertions to make room for them.
- Expand the oracle manifest with the new behavioral/audiovisual scenarios.
- Run the smallest focused tests during development. The integration owner then
  runs `run_retail_pipeline_tests.bat`, the exact focused M2 wrapper, all
  identity-bound oracle reviews, and finally
  `run_m2_acceptance.bat -IntegrationOwnerPublish`.
- Repeat the 30-minute soak with mass combat, corpse churn, repeated building
  destruction, radar use, builder construction, stance switching, and three
  clean restarts.

**Exit:** zero severity-0/1 differences, zero silent routes, zero leaks/orphans,
bounded corpse/debris allocation, and a complete builder-led match.

## Recommended execution order and path locks

Only one integration owner should edit the shared simulation contract. If work
is delegated later, use these non-overlapping locks:

| Package | Primary lock | Must not edit |
| --- | --- | --- |
| Combat importer | `importer/openbfme_importer/retail_unit_rules.py`, focused importer tests/profile fragments | Runtime simulation/HUD |
| Combat runtime | `game/src/retail_slice/retail_slice_sim.gd`, `retail_battalion.gd`, focused runners | Importer and radar |
| Commands/stances | stance contract module, HUD bindings, focused runners | Combat internals except reviewed interface |
| Corpses | corpse lifecycle/pool module and runner | Importer/HUD/radar |
| Radar | `retail_minimap.gd`, radar-only conversion/profile module and tests | Simulation except read-only snapshot API |
| Building AV | `retail_structure.gd`, bounded FX/audio consumers and focused runners | Combat/radar/builder |
| Builder loop | builder/build/placement modules, builder content profile, focused runners | Building AV internals except reviewed interface |

The integration owner should land R1 before R2/R3, land R4 and R5 independently
after R0, then integrate R6 after the building lifecycle API is stable.

## Expansion plan after the Men/Fords gate

Do not jump directly from the current slice to “everything at once.” Reuse the
new source-driven systems in widening rings, with a playable gate at each ring.

### E1 - Complete Men on Fords

- Add the remaining Men infantry, cavalry, siege, heroes, builders, hordes,
  upgrades, banners, formations, special powers, spellbook, fortress upgrades,
  walls, gates, expansions, neutral captures, and full AI strategy.
- Generalize the dual-weapon/condition engine, member targeting, stances,
  construction, lifecycle FX/audio, HUD command sets, and corpse budgets without
  adding Men-specific hard-coded branches.
- Exit with one complete Men mirror skirmish on Fords and full replay/save
  determinism.

### E2 - All six BFME2 factions on Fords

- Add one faction at a time through the same census -> deterministic conversion
  -> runtime contract -> rendered/behavioral oracle path.
- Recommended order is whichever faction has the highest converter reuse and
  lowest unsupported W3D/FX surface after a fresh census; do not guess the order
  now.
- Each faction gate requires economy, builder, complete roster, structures,
  upgrades, powers, AI, UI/audio, victory/defeat, and mirror-match soak.
- Exit with all base-game factions completing AI and player skirmishes on one
  map.

### E3 - Multiplayer map closure

- Generalize the proven Fords terrain, roads, water, sky, fog, props, particles,
  radar generation, navigation, scripts, creeps, tech structures, and spawn
  handling across the 46 shipped multiplayer maps.
- Resolve every unclassified map chunk family and add corpus-level Godot
  navmesh/routing proof. Structural cook alone is not map completion.
- Gate maps in representative families first, then run the complete corpus.
- Exit with every shipped multiplayer map loadable, navigable, radar-correct,
  and able to finish an AI match.

### E4 - Full skirmish rules and AI parity

- Close remaining armor/damage matrices, veterancy, leadership, fear, stealth,
  crush, garrison, transport, siege, powers, upgrades, weather, neutral logic,
  command points, resource scaling, handicaps, teams, and victory conditions.
- Replace the basic slice AI with source-compatible build orders, scouting,
  threat response, targeting, retreat, expansion, and difficulty behavior.
- Add deterministic replay and long soak matrices across factions and maps.

### E5 - Multiplayer

- Start only after deterministic simulation, input serialization, replay, save,
  and desync diagnostics are stable in E4.
- Implement lobby/setup, peer or authoritative transport as selected by project
  architecture, lockstep/input delay, checksums, reconnect policy, NAT/service
  integration, observer/replay, and adversarial validation.
- Do not couple retail asset payload transfer to multiplayer; peers validate
  compatible private pack identities locally.

### E6 - Campaign and War of the Ring

- Convert scripted maps, missions, cinematics, objectives, dialogs, hero state,
  persistence, strategic layer, rewards, and campaign-specific UI/audio.
- Treat campaign scripting and WotR as separate products with their own corpus
  census and acceptance manifests.

### E7 - Public code-only release preparation

- Keep this separate from private-game completion. Run the export firewall,
  provenance scan, reproducible builds, dependency/license inventory, security
  review, installer/update path, crash reporting policy, accessibility/options,
  localization, and legal-safe empty/fixture pack experience.
- Publish no retail payload, derived private pack, capture, or private oracle.

## Project-level scoreboard

Track completion by playable evidence, never raw converted-file counts alone:

1. **Slice behavior:** dual weapons, member targeting, stagger, stances, target
   feedback, corpses, builders, building AV, retail radar.
2. **Faction closure:** roster/economy/buildings/upgrades/powers/UI/audio/AI per
   faction.
3. **Map closure:** render, navigation, radar, scripts, props, environment, and
   completed-match proof per map.
4. **Converter closure:** deterministic supported formats plus rendered or
   behavioral backtests; “evidence-only” is not runtime support.
5. **Reliability:** deterministic signatures, restart/soak matrices, FPS,
   one-percent-low, peak/final memory, and allocation budgets.
6. **Containment:** all retail inputs/outputs/evidence remain below `.private`.

## Immediate next action

Start R0 and R1 together only after the integration owner freezes the current
identity tuple. The first implementation target should be the Archer dual-
weapon contract plus a failing focused runner. It is the smallest change that
forces the simulation, animation, projectile, per-member targeting, and test
model to stop treating a battalion attack as one generic action.
