# OpenBFME — Private Compatibility Build Plan

**Status:** active private full-content compatibility build; public release is a later sanitized code-only product

**Date:** 2026-07-13

**Reference game:** The Battle for Middle-earth II, patch 1.06

**Reference install:** `F:\BFME2`

**First integration proof:** Men-versus-Men on Fords of Isen II

**Active private target:** first materialize the complete effective BFME2 1.06 retail
asset view inside the ignored workspace, then deliver the complete Men faction on Fords of Isen II,
Rivendell, Mount Doom, Dagorlad, and Mordor, with source-derived UI, audio, terrain,
navigation, AI, models, animations, materials, FX, lighting, construction, damage,
collapse, and rubble states measured against the original game
**Preferred presentation engine:** Godot 4.7 .NET, subject to the Phase 0 language-boundary bakeoff

## 1. Decision

OpenBFME will be a new, modern, moddable RTS engine. It will not be an OpenSAGE fork,
a continuation of the SAGE executable, or a Godot wrapper around BFME2 data formats.

The intended pipeline is:

```text
User-owned BFME2 install
        |
        v
Importer coordinator
  (.NET + Blender Python + pinned format tools)
        |
        v
Versioned OpenBFME content bundle
        |
        +------> deterministic game simulation
        |
        +------> Godot presentation at 60/120/144/240 Hz
```

The original installation is the authoritative private content source and behavioral
reference. The importer copies the exact required working set into the ignored
`.private` workspace in this checkout. The runtime still consumes only converted
OpenBFME bundles; it must not interpret BIG, W3D, INI, or OpenSAGE types during play.
A later public-release sanitization process will produce a clean code-only tree and may
add a legal-safe original/free pack.

This private compatibility implementation is approved. Proof gates remain mandatory
quality evidence and may change an unsafe or incorrect implementation, but they are not
permission gates that defer full-asset extraction or the authorized Men/five-map work.

## 2. Product boundaries

### North star

- Render and input presentation suitable for 144/240 Hz displays.
- Gameplay timing independent of render rate.
- A deterministic, replayable simulation with measured compatibility behavior.
- A data-driven content API that supports BFME2 compatibility content and wholly new
  factions, maps, models, rules, and audiovisual assets.
- A local retail importer that first materializes the complete effective retail view,
  then builds dependency-closed private runtime packs under `.private`.
- A committed legal-safe test pack for CI and engine development.
- A later playable original/free pack; the test pack alone does not satisfy this goal.
- No permanent dependency on BFME2 skeletons, filenames, behaviors, factions, or art.

### First vertical slice

- **Map:** Fords of Isen II.
- **Faction:** Men.
- **Matchup:** Men versus Men.
- **Buildings:** Fortress, Farm, Barracks, Archery Range, Stable.
- **Units:** Gondor Soldier, Gondor Archer, Tower Guard, Gondor Knight.
- **Loop:** build, gather resources, train, move, attack, and destroy the enemy Fortress.
- **AI:** one deterministic build plan capable of completing the loop.
- **Compatibility target:** only the BFME2 1.06 behavior exercised by this slice.
- **Retail content:** generated locally from a verified user installation and retained
  only under the git-ignored `.private` development workspace.

All BFME2 1.06 Men heroes, units, powers, upgrades, walls, fortress components, and
visual lifecycle support used by normal skirmish play are in the active private scope.
Campaigns, War of the Ring, tutorials, online networking, custom maps, non-Men factions,
and ROTWK 2.01 gameplay remain outside this milestone.

“1:1” means measured BFME2 1.06 compatibility for the complete Men/five-map oracle
matrix, including graphical, spatial, audiovisual, simulation, construction, damage,
collapse, rubble, UI, routing, and AI evidence. Asset presence alone is not parity.

## 3. Non-negotiable architecture

### Importer/runtime firewall

Allowed dependencies:

```text
Importer coordinator -> pinned donor tools and local conversion processes
Importer coordinator -> OpenBFME schema
Simulation           -> OpenBFME schema
Godot presentation   -> Simulation + OpenBFME schema
```

Forbidden dependencies:

```text
Simulation         -X-> BFME2, OpenSAGE, BIG, W3D, Blender, donor types
Godot presentation -X-> BFME2, OpenSAGE, BIG, W3D, Blender, donor types
Content schema     -X-> serialized donor classes or mandatory BFME filenames
```

The importer is a coordinator, not necessarily one executable. It owns a stable CLI,
manifest, cache, resumability, reports, and process boundaries while invoking the best
pinned tool for each conversion stage:

- .NET for orchestration, schema validation, INI semantics, dependency closure, and
  diagnostics.
- Blender Python in deterministic headless mode for W3D/rig/animation conversion.
- Audited OpenSAGE and BFME2 modding utilities for format knowledge and isolated parsing.
- Godot import/cook steps only for runtime-native caches.

Every stage exchanges declared files plus machine-readable manifests. No stage reaches
into another tool's in-memory types.

### Simulation/presentation firewall

- Authoritative state advances only through an explicit scheduler and tick-stamped
  commands.
- The renderer consumes snapshots and events; it owns no health, damage, resources,
  pathing, cooldown, target, production, AI, or victory truth.
- Godot interpolates between authoritative snapshots every render frame.
- Camera, cursor, selection feedback, UI, and audio remain render-rate responsive.
- Spawns and teleports explicitly reset interpolation.
- Running at 144 or 240 Hz must not change the outcome of a replay.

### Language choice is conditional

Pure C# remains the leading simulation choice, but Phase 0 must compare it with typed
GDScript before the repository is reorganized around it. The bakeoff uses the same horde
scenario and measures:

- deterministic replay behavior;
- implementation complexity and test ergonomics;
- snapshot-transfer and garbage-collection cost;
- 300- and 1,500-member performance;
- ease of keeping Godot types out of authoritative state;
- agent reviewability and compiler/static-analysis quality.

C# wins unless the evidence shows material integration cost without a corresponding
simulation or tooling advantage. Presentation may remain GDScript either way. The
existing prototype is a presentation and scenario donor, not authoritative simulation.

## 4. Proof foundation and standing evidence work

This work began as a two-week feasibility program and produced the proof stages now in
the repository. It continues where the full build needs new evidence, but it is no
longer a calendar or permission gate. Its useful outputs are oracle observations,
focused fixtures, disposable spikes, performance measurements, and architecture
decisions that directly constrain production code.

### Workstream A: original-game oracle

INIs describe configuration, not complete runtime behavior. Before claiming parity,
capture BFME2 1.06 as a black-box oracle on the reference machine.

Create at least these fixtures:

| Area | Scenario | Recorded evidence |
|---|---|---|
| Timing | Idle match and timed move | observed logic cadence, distance, duration |
| Input | Select and issue move/stop/attack | latency and order semantics |
| Horde | Turn, narrow passage, split obstruction | slots, facing, cohesion, recovery |
| Combat | Soldier horde versus Soldier horde | acquisition, attack cadence, member deaths |
| Ranged | Archer target at several distances | range, projectile timing, retarget behavior |
| Cavalry | Knight move, collision, and attack | locomotion, formation, crush/impact if scoped |
| Economy | Farm income over time | first payout, interval, modifiers |
| Production | Build and train scoped objects | costs, timing, spawn behavior |
| Structures | Placement and destruction | footprint, clearance, rubble/victory timing |
| Navigation | Static and newly placed blockers | replanning and blocked-order behavior |
| Map | Fords starts, water, crossings, build plots | coordinates, passability, ownership |
| AI | One representative match opening | order sequence and required rule surface |

Each fixture contains the BFME2 version, map, starting state, exact actions, timestamps,
video or screenshots where useful, extracted configuration references, observations,
tolerance, and confidence. Automate capture where reliable; otherwise use a documented
manual protocol. Retail-derived evidence stays outside distributable artifacts.

The oracle determines the simulation scheduler. Do not assume that the original uses a
single global cadence or that 30 Hz is correct. Measure movement, weapons, economy,
production, AI, and animation-facing behavior. OpenBFME may use a rational multi-rate
scheduler while presenting at any display refresh rate.

### Workstream B: one-asset conversion proof

Convert one Gondor Soldier dependency closure:

- model and subobjects;
- skeleton and skinning;
- texture/material assignments;
- idle, move, primary attack, and death capability graphs;
- portrait/icon;
- select and attack audio;
- provenance and source hashes.

Animation mapping is not a four-string dictionary. The proof must represent conditional
variants, weapon state, mounted/unmounted state where applicable, transitions,
subobject visibility, loop/root-motion metadata, sound/FX events, and graceful fallback.
Only capabilities used by the slice need implementation, but the schema must report
unsupported conditions rather than discard them.

The conversion must be repeatable headlessly, validate without Blender or donor tools
at runtime, and produce the same manifest/content hashes from unchanged input.

### Workstream C: representative map proof

Import a representative Fords of Isen II region containing terrain variation, water, a
crossing, props, a start position, buildable and unbuildable ground, and static blockers.

Prove:

- coordinate handedness, origin, rotation, and scale;
- terrain geometry/material layers and water placement;
- object transforms and semantic classifications;
- passability, footprints, terrain costs, and buildability;
- correspondence with the oracle capture;
- a deterministic cooked result and visual contact sheet.

Do not invent a permanent `nav.bin` format during the spike. The source bundle stores
neutral map facts; a versioned cook step may produce Godot caches and an authoritative
pathing grid. The binary layout is standardized only after the representative proof
shows what data is actually required.

Audit all Fords scripts/triggers before selecting it permanently. If unsupported map
logic is essential to a normal skirmish, either implement that small dependency or use
a simpler map for the first slice. The map name is subordinate to a shippable loop.

### Workstream D: horde, navigation, and language proof

Implement the same small arena twice: pure C# and typed GDScript. Use legal-safe
primitive assets. One 15-member horde must navigate, turn, encounter an obstruction,
engage another horde, lose members, and finish with a deterministic state hash.

The candidate horde algorithm is:

1. The horde requests one global path for its anchor and footprint class.
2. A deterministic formation generator assigns stable member slots by entity ID and
   rank definition.
3. Members follow a bounded local corridor toward their slots; they do not own global
   navigation agents.
4. Separation examines neighbors in stable ID order and applies capped fixed-point
   corrections.
5. Melee engagement allocates a bounded set of contact slots in stable attacker/target
   order; unallocated members queue or seek another eligible target.
6. Cohesion has explicit soft and hard radii. Stragglers receive catch-up movement;
   irrecoverably blocked members trigger a deterministic horde replan, never teleport
   silently.
7. Death releases a slot. Remaining slots are reassigned only at declared formation
   transitions to avoid reshuffling every tick.
8. Replenishment creates a new member entity and fills the next deterministic vacancy.

The candidate navigation model is:

- versioned fixed-cell authoritative grid derived from neutral terrain facts;
- deterministic A* or hierarchical A* with specified neighbor order and tie-breaking;
- footprint classes for hordes and buildings;
- separate static terrain, temporary occupancy, and dynamic-building blocker layers;
- deterministic invalidation/replan rules;
- no authoritative dependency on Godot NavigationServer or physics query ordering;
- fixed-point positions and costs unless the bakeoff proves another representation is
  repeatable across supported machines.

Path quality, formation coherence, and cost at 20 hordes/300 members are measured. A
1,500-member stress case identifies the likely ceiling; it is not a slice requirement.

### Workstream E: renderer and snapshot proof

Render the legal-safe horde through Godot at 60, 120, 144, and 240 Hz while the same
simulation replay runs underneath it. Prove:

- identical authoritative hashes at every render rate;
- smooth interpolated transforms with no tick-rate stepping;
- immediate selection/cursor feedback and defined command scheduling;
- bounded allocations and snapshot-transfer cost;
- clean shutdown with no RID, ObjectDB, orphan resource, or error output.

The existing 101 assertions do not pass this gate while Godot reports leaks.

## 5. Standing architecture quality gates

The affected production path is accepted only when its relevant gates pass:

1. **Oracle:** at least 20 repeatable observations cover the scoped loop and produce a
   documented scheduler recommendation.
2. **Asset:** one retail unit converts headlessly with usable rig, material, animation,
   audio, capability, and provenance data.
3. **Map:** the representative region matches terrain, water, transforms, buildability,
   and passability closely enough for a controlled scenario.
4. **Horde:** real members move, form, engage, take damage, and die without per-member
   global pathfinding.
5. **Determinism:** repeated runs and all tested render rates produce identical hashes.
6. **Presentation:** motion is smooth at 144 Hz and has no authoritative render coupling.
7. **Runtime independence:** the proof runs from the converted bundle with BFME2,
   Blender, OpenSAGE, and conversion tools unavailable.
8. **Language:** the C#/typed-GDScript decision is recorded from measurements.
9. **Clean gate:** one command builds and tests the proof with no hidden Godot errors or
   leaks.
10. **Containment:** private retail inputs and derivatives remain under `.private` and
    the code-only export scan cannot reach them.

Redesign the affected component if asset animation states cannot be expressed cleanly, Fords depends
on a large unimplemented scripting system, deterministic horde movement requires a
global path per soldier, cross-language transfer consumes a material frame fraction,
or donor-specific concepts leak into required runtime types.

A failed Fords gate does not automatically kill OpenBFME. It may change the first map,
conversion strategy, schema, simulation language, or compatibility ambition. A failed
runtime-independence or deterministic-horde gate is an architectural failure that
blocks completion of the affected runtime path, not private asset extraction or
unrelated evidence work.

## 6. Architecture used by the active build

The expected, but still evidence-dependent, repository shape is:

```text
engine/
  OpenBfme.Schema/
  OpenBfme.Sim/
    Commands/
    Scheduler/
    Horde/
    Navigation/
    Movement/
    Combat/
    Economy/
    Production/
    AI/
    Replay/
    Snapshot/
  OpenBfme.Sim.Tests/
game/
  Godot presentation, input, UI, audio, animation, visual tests
importer/
  coordinator, adapters, manifests, fixtures, reports
tools/
  stable build/import/test/audit/gate entrypoint
content/
  openbfme-test only; no retail-derived files
oracle/
  legal-safe protocols, schemas, measurements, and comparison tooling
docs/
  architecture decisions, provenance, compatibility matrix, legal policy
```

No replacement for the current large `SimWorld` may become another god object. Systems
communicate through narrow data contracts and have focused tests.

### Content bundle v0

```text
pack.json
data/
  objects.json
  weapons.json
  armor.json
  locomotion.json
  behaviors.json
  animation_capabilities.json
maps/
  <map-id>/map.json
assets/
  models/
  textures/
  audio/
  ui/
provenance/
  manifest.json
```

Cooked platform/engine caches live in a declared cache section or external cache and
can be regenerated from the neutral facts. Schema v0 has stable IDs, explicit units and
coordinate conventions, content hashes, capability requirements, unknown-field
diagnostics, path containment, size/count limits, and no executable mod scripts.

One validated bundle is the only packaging product in v0. General dependency solving,
hot reload, executable plugins, marketplaces, and multiple concurrent rulesets wait
for a second real pack or external mod requirement.

### Retail importer product requirements

After the proof, the coordinator must:

- detect or accept the install directory and verify patch/language;
- diagnose missing or modified archives;
- calculate the exact slice dependency closure;
- resume interrupted work and cache by input/tool/config hashes;
- sandbox archive paths and enforce decompression size/count limits;
- emit converted, missing, substituted, unsupported, and provenance reports;
- keep retail and converted data contained under `.private` inside the local checkout,
  and exclude it from git and later public/code-only exports;
- support a dry run and deterministic audit mode;
- continue after one failed optional asset while failing required capabilities clearly.

The initial conversion order is one Gondor Soldier, one horde, the representative map
region, full Fords, then the exact four-unit/five-building closure. Converting the full
installation is not a milestone.

## 7. Compatibility and testing strategy

Maintain a machine-readable compatibility matrix. Each row names a BFME2 behavior or
scenario and one of: `measured`, `implemented`, `verified`, `partial`, `unsupported`, or
`not scoped`. “Verified” requires an oracle fixture plus an automated OpenBFME scenario.

The stable repository command should eventually expose:

```text
openbfme doctor
openbfme oracle validate
openbfme import asset <manifest>
openbfme import map <manifest>
openbfme audit-assets <pack>
openbfme run-scenario <fixture>
openbfme compare-parity <fixture>
openbfme benchmark <scenario>
openbfme gate phase0
openbfme gate vertical-slice
```

Required merge gates after Phase 0:

- build with warnings as errors;
- schema and malicious-input validation;
- focused unit/property tests for authoritative systems;
- replay hashes across repeat runs and supported configurations;
- headless Godot boot and named gameplay scenarios;
- zero Godot error/leak/orphan output;
- screenshot/contact-sheet comparisons for named scenes;
- performance and allocation budgets on the reference machine;
- runtime assembly/reference scan for donor dependencies;
- proprietary hash/path/provenance scan;
- clean import reproducibility and cache-invalidation tests.

## 8. Autonomous development harness

There is no missing magical MCP server that makes this project safe to build
autonomously. Godot/Blender MCPs may improve observation and interaction, but the
critical capability is a repository-specific, restartable harness.

Use three persistent responsibility lanes under one integration owner:

1. **Importer/content:** formats, conversion, schemas, map facts, provenance.
2. **Simulation:** scheduler, replay, hordes, navigation, combat, economy, AI.
3. **Godot:** snapshot bridge, renderer, animation, UI, audio, visual/performance QA.

Hundreds of subagents are a queue of bounded jobs, not hundreds of simultaneous editors.
Every job declares its ID, lane, allowed paths, immutable inputs, expected outputs,
acceptance commands, performance budget, prohibited data, dependencies, retry count,
and escalation condition.

Before unattended operation, add:

- root `AGENTS.md` with legal and architectural boundaries;
- machine-readable task manifests and decision records;
- isolated worktrees and one integration queue;
- stale-lock detection, heartbeat, retry ceiling, and crash/reboot recovery;
- immutable test ownership or separate test-review approval;
- artifact/cache directories outside source and automatic cleanup budgets;
- merge conflict and contract-change escalation to the integration owner;
- reproducible toolchain lock/report for Godot, .NET, Blender, Java, and donors;
- logs and status reports that never contain or upload proprietary content.

Godot Forge, a small audited Godot editor/runtime bridge, Blender MCP, and Ralph-style
task loops are optional conveniences. They are not sources of truth and are not allowed
to bypass the stable CLI or merge gates.

## 9. Existing code and donor policy

Do not fork an existing public Godot RTS as the foundation. Audited candidates contain
useful camera, selection, construction-preview, minimap/fog, or spatial-query ideas but
do not provide the required deterministic simulation, replay boundary, BFME horde
semantics, importer contract, or automated verification.

The current OpenBFME prototype is already adequate as a presentation donor. Public RTS
donor spikes are removed from the critical path. Adopt a component only when an actual
slice requirement appears and a small spike proves it reduces maintained complexity.

OpenSAGE is a research and importer donor, not the runtime. Its parsers and schemas are
valuable; parser presence is not evidence that a BFME2 behavior is implemented or
correct. `chipgw/openbfme` is historical research only. Every copied implementation
requires a pinned source, license, file-level provenance, and compatibility review.

## 10. Active production milestones

There is no remaining calendar-based permission gate. Sequence the work by dependency
and accept each milestone only with the named evidence:

| Milestone | Acceptance evidence |
|---|---|
| Complete effective retail view | Every winning archive entry extracted, hashed, manifested, and repeatable under `.private` |
| Full Men dependency closure | Every normal-skirmish Men definition and physical leaf resolved or explicitly failed closed |
| Fords full-match gate | Complete Men-versus-Men loop with retail UI/audio/presentation and oracle comparisons |
| Five-map private target | Full skirmishes, deterministic navigation/AI, map-specific evidence, performance, and soak gates |
| Later code-only public source | Sanitized project-authored tree proves no `.private` or retail-derived payload is reachable |

Continuous agents shorten mechanical implementation and test generation. They do not
remove ambiguity in reverse engineering, animation repair, game-feel comparison, map
semantics, licensing, integration, or art direction. The schedule is reset whenever an
oracle or import assumption fails.

Active production sequence:

1. Freeze schema/scheduler/horde/navigation ADRs from Phase 0 evidence.
2. Build the placeholder economy, construction, production, combat, victory, and AI loop.
3. Expand the importer to the exact Men dependency closure.
4. Import and verify full Fords or activate the simpler-map fallback.
5. Replace placeholder presentation incrementally with imported local content.
6. Add guided import UX, diagnostics, resumability, cache repair, and uninstall behavior.
7. Run parity, performance, visual, leak, and 30-minute soak gates.
8. Ship the engine plus legal-safe test pack; the user creates the retail pack locally.
9. Begin the original/free pack as a separately staffed content milestone.

## 11. Vertical-slice definition of done

### Gameplay and compatibility

- A Men-versus-Men skirmish starts on the selected Fords-compatible map.
- The five buildings can be placed, built, damaged, and destroyed.
- The four unit types can be trained, selected, moved, stopped, and ordered to attack.
- Members form, navigate, acquire targets, attack, die, and replenish according to the
  verified slice compatibility matrix.
- Economy, production, combat, Fortress destruction, and victory match declared oracle
  tolerances.
- The deterministic AI can build, train, attack, and win.

### Modern presentation

- Camera and input feedback are responsive at 60/120/144/240 Hz.
- Unit movement and animation do not visibly step at the simulation cadence.
- Replays have identical outcomes regardless of render rate.
- UI has smoke coverage at 1080p, 1440p, 4K, and ultrawide.
- The reference vertical-slice army targets 144 rendered FPS on the recorded reference
  machine; performance budgets are finalized from Phase 0 measurements.

### Import, runtime, and modding

- A verified retail install can be selected and converted through one guided flow.
- Import is resumable, deterministic, cache-correct, and diagnostically useful.
- The runtime works after removing access to the retail install and all donor tools.
- No proprietary or converted retail asset exists in git or a public engine
  distribution; private development builds load only from ignored `.private` packs.
- Replays reproduce exact hashes and a 30-minute normal match completes without crash,
  leak, softlock, or unbounded memory growth.
- A legal-safe sample override changes one unit's model, name, health, and weapon without
  engine code changes; invalid data produces actionable errors.

## 12. Private content policy and later public release

The current product is a private local compatibility build. Retail extraction and
conversion inside the ignored `.private` tree are implementation work, not a release
blocker. Public distribution decisions are deliberately deferred to a separate
code-only sanitization and review phase.

Before donor code is copied or public builds are distributed:

- choose and add the engine license; GPLv3 is the current candidate, but OpenSAGE's
  EA-derived files and additional terms need file-level review;
- maintain `docs/THIRD_PARTY.md` with source, pinned commit, license, copied files, and
  modifications;
- distinguish behavior learned from observation/documentation from copied code;
- export only project-authored code, documentation, and repository-authored fixtures in
  the later public release;
- store private generated retail caches only under the ignored `.private` workspace;
- scan git, releases, logs, screenshots, fixtures, and exports for retail signatures;
- use an independent public name and branding review;
- obtain qualified legal advice before public positioning and distribution.

Nothing in this private build policy declares retail material public or redistributable.
The later public-source process must make its own release and branding decisions after
the private game works.

## 13. Immediate execution order

1. Extract and hash-manifest the complete effective BFME2 1.06 archive view under
   `.private\retail-work`; do not materialize superseded duplicate entries by default.
2. Finish the typed Men dependency graph for objects, inheritance, models, animations,
   materials, textures, weapons, projectiles, FX, audio, UI, and lifecycle states.
3. Generate the bounded full-Men conversion profile from that graph and fail closed on
   every unresolved, ambiguous, or unsupported requirement.
4. Convert and audit every required Men model, hierarchy, animation, texture, material,
   effect, sound, UI image, string, and building lifecycle state.
5. Complete the five maps' terrain, water, object placement, setup, buildability,
   navigation, routing, triggers, scripts, lighting, and AI inputs.
6. Bind the converted data to the authoritative simulation and exact retail UI/audio
   runtime paths without generic private-mode fallbacks.
7. Capture and compare the BFME2 1.06 oracle matrix for visual, spatial, timing,
   gameplay, UI, audio, routing, AI, damage, collapse, and rubble parity.
8. Remove obsolete proof scaffolding only after coverage has moved to the production
   path, then run the focused checks, retail pipeline gate, Stage 10 gate, rendered
   validation, and soak test.

## 14. Execution checkpoint (2026-07-12)

This checkpoint records implementation evidence inside the approved private build.
Language, scheduler, map model, horde algorithm, pathing model, schema, and performance
budgets remain evidence-driven architecture decisions, not reasons to pause authorized
content work.

Recorded implementation evidence in the current repository:

- proof Stages 1-10;
- a deterministic, private retail importer that extracts and converts scoped BFME2
  content into the ignored `.private` workspace without placing payloads in git or a
  public export;
- an exact Fords map-fact cook covering height, passability, terrain, water, objects,
  and waypoints, with those facts mounted for runtime consumption; and
- an integrated Godot retail-slice checkpoint through Stage 15. It starts with four
  imported Gondor Soldier battalions and can create additional Soldier battalions through
  the bounded production loop described below.

The next map-import milestone is limited to the frozen official BFME2 1.06
multiplayer/skirmish corpus; it does not add online networking or reopen campaign, WotR,
tutorial, shell, or custom-map scope. Its named requirements and deletion decisions live
in `docs/MULTIPLAYER_MAP_CONVERTER.md`. A payload-free corpus census must precede new
generalized conversion work.

Jonathan subsequently authorized the complete BFME2 1.06 Men dependency closure on the
five evidence-selected maps as a persistent private compatibility goal. Its exact scope,
version boundary, dependency census, UI/audio rules, and completion gates live in
`docs/FULL_MEN_FIVE_MAP_MILESTONE.md`. ROTWK 2.01 evidence remains a separately versioned
reference/overlay and cannot silently change the BFME2 1.06 target.

The expanded generated-leaf checkpoint is now implemented. The current 57-object
command graph resolves all 41 upgrades, 38 special powers, and 26 sciences as typed,
payload-free definition leaves. It resolves 159 mapped images across 78 compiled
DDS/TGA atlases plus one explicit source-null banner portrait, all 380 requested text
IDs under an explicit source-order duplicate policy, and 105 audio roots through 115
events/ten multisounds to 474 exact sample leaves. The generated profile contains 81
bounded resources selecting 634 exact files with zero missing required inputs, and its
private pack build passed semantic provenance audit. This is deterministic
private-workspace census/conversion evidence, not runtime parity: typed
model/animation/material/FX-list/weapon/projectile/lifecycle leaves, Godot manifest
consumption, and BFME2 oracle review remain active work.

Stages 11-15 are implementation checkpoints inside the private retail proof, not a new
completion definition or a replacement for the full Men/five-map gates:

| Stage | Implemented evidence | Deliberate boundary |
|---|---|---|
| 11 - control groups | Deterministic groups 1-9 support assign, recall, prune, reset, snapshot, and replay-signature coverage. | Groups contain only living player Gondor Soldier battalions; Archer, Tower Guard, and Knight presentation closures exist in the private pack but are absent from the playable runtime roster. |
| 12 - order feedback | Authoritative snapshots carry destination, remaining route/cells, ford, and order sequence. Selected battalions present a route line and destination flag; rejected orders preserve the prior valid route and arrival clears it. | Routing is still the bounded static cooked Fords grid, without full building/dynamic-obstacle invalidation or oracle parity coverage. |
| 13 - equipment and attack timing | W3D conversion fails closed on retained helper/collision/volume or ambiguous box geometry, canonicalizes/restores/revalidates proven equipment attachments, and converts exact source-proven additive equipment materials without touching ordinary textures. The private pack now proves four core presentation clips for Archer, Tower Guard, and Knight in addition to the 23-clip Soldier closure. Runtime attack windup and cadence remain derived from the imported Soldier rules. | The three added units are presentation conversions, not complete animation-state, simulation, production, or combat-parity closures. |
| 14 - bounded base authority | Each team receives the five scoped structure roles. A Farm pays deterministic resources and a Barracks can queue one supported 15-member Gondor Soldier horde. The private pack now contains intact zero-clip hierarchical GLBs for all five structures. | The models are not yet wired to complete placement, construction, damage, destruction, Archer/Guard/Knight production, or economy parity. |
| 15 - integrated match loop | The private Godot scene connects the base authority to the HUD, control-group strip, Soldier queue, source-driven minimap, audio settings, simple shared-contract enemy production/attack behavior, Fortress victory/defeat, and outcome splash. | The AI does not execute the full BFME2 Men build plan, and the UI/presentation is not yet accepted against the complete viewport, performance, and oracle matrices. |

The retail profile remains explicitly `vertical_slice_complete: false`. The formal
vertical slice still requires playable runtime integration for imported Gondor Archer,
Tower Guard, and Knight; complete placement/build/damage lifecycles for all five
imported intact structures; the full four-unit economy, production, and deterministic AI build plan;
Godot rendering of the exact cooked map materials, resolution of the remaining object models; buildability and full
building-aware/dynamic navigation; oracle parity evidence; and the full
performance/input/viewport matrix. Passing the current proof gates therefore
demonstrates scoped feasibility; it does not waive the remaining full milestone
evidence, and no Stage 11-15 implementation may change the profile to a completed state.

## 15. Final judgment

This is the right route because it preserves the three things the project actually
needs: modern presentation, a clean moddable engine, and optional reuse of locally owned
retail content. It is also the shortest honest route: prove the original behavior,
asset conversion, map conversion, deterministic horde model, and Godot boundary before
building the game around them.

The plan succeeds by keeping every claim evidence-backed. Fords is the first complete
match gate; the active private product is the full Men faction across the selected five
maps. The same proven pipeline can then expand to the rest of BFME2-compatible content.
