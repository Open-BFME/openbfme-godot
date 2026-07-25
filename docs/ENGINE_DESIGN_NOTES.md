# Engine design notes — scaling Open-BFME to a full port

> **Scaling doctrine, not an implementation plan.** Accepted system boundaries
> live in [ARCHITECTURE.md](ARCHITECTURE.md) and
> [SIMULATION_PROTOCOL.md](SIMULATION_PROTOCOL.md); this document is the
> performance and data-shape reasoning behind them.

**Updated:** 2026-07-16
**Scope:** architecture and performance doctrine for the multi-faction port.
These are standing decisions; deviations need a written reason.

## 1. The prime directive: factions are data, not code

BFME2 itself is one engine plus INI data — EA added factions without adding
engine code. We inherit that rule: **adding a faction must mean adding pack
data only, zero new GDScript.** The current sim still hardcodes Men constants
(`SOLDIER_HORDE_ID`, `UNIT_PRODUCTION_RULES`, `STRUCTURE_KINDS`); the M3
milestone begins with the pivot that keys everything off pack object
definitions. No second faction lands before that pivot, or we hardcode a
second faction's worth of constants we immediately unwind.

## 2. Mirror SAGE's module system

SAGE object INIs declare behavior modules (`Body`, `Locomotor`, `WeaponSet`,
`ProductionUpdate`, `SpecialPower…Update`). The scalable sim is the same
shape: an entity is a bag of generic modules instantiated from its pack
definition, one implementation per module type, all variation in data. This
is also the honest architecture for a port — our module names map 1:1 onto
the INI we compare against for parity.

## 3. Simulation doctrine

- **Battalion granularity is sacred.** The sim simulates battalions
  (~100–200 entities in a big match); individual soldiers exist only in
  presentation. This is BFME's own trick and the single biggest reason the
  port is feasible: 3,000 visible soldiers is ~200 sim entities.
- **Fixed 0.1s tick, deterministic, CPU-only.** Determinism is the foundation
  for the replay-signature gates today and lockstep multiplayer later. GPUs
  do not promise bit-identical floats across vendors; nothing authoritative
  ever runs there.
- **Spatial hash grid** replaces all-pairs range scans before entity counts
  grow (M3). **Flow fields** serve mass movement to shared destinations; one
  field serves any number of battalions.
- If the tick loop ever dominates, it moves to C#/GDExtension as a unit —
  struct-of-arrays over packed arrays, zero allocations in the hot loop.
  Decide the language boundary early; porting idioms later is the expensive
  part.

## 4. Presentation doctrine

- **Nothing updates every frame except the camera and the near field.**
  Rate-layering is worth more than any micro-optimization: sim 10Hz, member
  steering 30Hz near / ~8Hz far / 0 off-screen (see `retail_formation.gd`
  LOD intervals), AI thinking ~2Hz, audio culled to the loudest N voices.
- **The scene graph is the enemy, not the renderer.** Godot nodes tax every
  frame. Skeletal `Node3D` members exist only in the hero field
  (~200–300 units). Beyond that: **vertex-animation-texture (VAT) MultiMesh
  tiers** — animation baked to texture at import time (the importer already
  routes every animation through Blender; the conversion cache makes the bake
  ~free), played entirely on GPU, one draw call per archetype. Distant units
  crossfade to marching-cycle VAT; corpses freeze to a single VAT frame.
- **Formation movement** (implemented, `retail_formation.gd`): members steer
  individually toward moving formation slots (arrive behavior, catch-up speed
  cap, neighbor separation); heading changes >120° trigger greedy nearest-slot
  reassignment (the retail rank-crossing reform). Root motion is fully
  compensated so members are world-stable — and corpses stay where they fell.
  Run-animation playback rate tracks actual member speed (foot-slide fix).
  Implemented in battalion-local space deliberately: flipping member
  coordinate semantics across the codebase was rejected as needless blast
  radius.

## 5. GPU boundary

The GPU gets work that is massively parallel, visual-only, and never read
back: skinning, particles, the linear-fog compute compositor, terrain
lighting in-shader, and (next) VAT crowd animation. The CPU keeps everything
authoritative: sim, pathfinding, steering that feeds gameplay state. Readback
stalls and driver nondeterminism are the two failure modes this line
prevents.

## 6. Frame budget (144 fps target = 6.9 ms)

| System | Budget |
|---|---|
| Sim tick (amortized) | 1.0 ms |
| Member steering + animation state | 1.5 ms |
| Render submit (CPU) | 1.5 ms |
| UI / audio / input | 0.5 ms |
| Headroom (spikes, GC, driver) | 2.4 ms — never spent |

Budgets are enforced, not aspirational: the M3 stress scene (N battalions,
scripted brawl, fixed camera path) runs as a gate with frame-time pass/fail.
Optimization without a fixed benchmark is astrology. Watch hitches, not
averages — allocation churn shows up as spikes players feel.

## 7. Code organization

- New systems land in new files (`retail_formation.gd`, `retail_tooltip.gd`,
  `retail_side_command_bar.gd` set the pattern), target ~800 lines per file.
- No file grows past ~2,000 lines: if a change would cross that, the
  extraction happens as part of that change (move code verbatim, keep thin
  delegates so runner references stay valid). No big-bang refactors.
- Machine-derived files (`retail_hud_apt_runtime.gd`) are exempt — they are
  oracle-shaped and splitting buys nothing.

## 8. Testing shape at scale

Exact-count pins (today's runners) are right for one faction and
unmaintainable for six. From M3: **schema validators** (every pack object
must carry complete locomotor/weapon/lifecycle contracts), **golden replays**
per faction (deterministic sim signatures), and the **capture-pair oracle**
for visuals. Exact pins remain only on the shared engine.

## 9. What is deliberately rejected

- Chasing headline unit counts (70k) — BFME gameplay saturates ~1,500
  visible; build for 3× the real maximum.
- Per-unit navmesh agents or physics — battalion pathing + member steering is
  the correct fidelity; individual agents are the classic RTS project-killer.
- Premature ECS rewrites — battalion granularity already provides the entity
  reduction; data-orient the hot loops, not the codebase.
- GPU-driven sim/pathfinding — see §5.
- Coordinate-semantics flips and other cross-cutting refactors sold as
  cleanups — blast radius must pay for itself in player-visible value.

## 10. Content pipeline

Conversion is the port's factory floor: content-addressed caching, batched
attestation, and parallel Blender workers (importer task I1) make rebuilds
proportional to *new* content, not total content. The VAT bake (§4) belongs
in this pipeline, cached like everything else. Reproducibility contract
stands: cold and warm builds must match byte-for-byte; the bundle digest
intentionally includes the toolchain hash, so pipeline changes mint new
identities with identical content — verify content-equivalence, then move the
identity pins from measured output, never by hand.
