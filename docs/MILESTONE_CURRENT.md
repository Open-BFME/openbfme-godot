# Current milestone: M2 Men/Fords

**Owner:** integration owner
**Owns:** binary completion contract for the active milestone
**Does not own:** broader product scope or volatile pack identity
**Last verified commit:** `ad370cc9b02bdec600564cf1c606e70833faa97a`
**Update trigger:** acceptance evidence or milestone scope changes
**Validation:** `run_m2_acceptance.bat -IntegrationOwnerPublish`

## Identity rule

All evidence must target one tuple:

```text
git revision + dirty-state digest + profile SHA-256 + bundle SHA-256
```

No result may be borrowed from another identity. `vertical_slice_complete`
remains false until final acceptance.

## Scope

- BFME2 1.06.
- Men versus Men on Fords of Isen II.
- Four Men battalions: Soldier, Archer, Tower Guard, Knight.
- Fortress, Farm, Barracks, Archery Range, Stable, and MenPorter construction.
- Exact selected-pack terrain, water, roads, props, fog, sky, particles, HUD,
  portraits, command art, audio, music, animations, effects, and lifecycles used
  by the required scenarios.
- Player and deterministic enemy-AI win/lose loops.

Campaign, WotR, multiplayer, other factions/maps, RotWK, full-corpus conversion,
and synthetic proof work do not satisfy this milestone.

## Completion gates

### 1. Exact pack, publication, and selection

- Forced Build A and Build B are byte-identical and a clean private state root
  reproduces the same bundle.
- Semantic provenance is complete, with no missing, ambiguous, unsupported, or
  incomplete required dependency and no warning/leak/orphan diagnostic.
- The exact bundle is published below `.private/content-packs/bfme2-men-vslice`,
  and `selection.json` names that immutable bundle.
- Godot mounts that bundle explicitly. Every required runtime definition comes
  from it; no stale, implicit, loose, or synthetic fallback supplies a required
  asset or rule.
- No `.building` transaction remains. Failed publication leaves the previously
  selected bundle unchanged.

### 2. Retail graphical closure

- Soldier, Archer, Tower Guard, and Knight battalions each render 15 imported
  retail members with their required models, materials, skeletons, equipment,
  idle, move, attack, and death presentation.
- A selected Men battalion uses the source `MenGenericLevel1`
  `SHADOW_MERGE_DECAL` contract and both converted retail decal textures. The
  oracle approves its footprint, color, opacity, and throb timing.
- Archer arrows, streak, impact effect, attachment origin, timing, and weapon
  audio use the selected-pack retail contracts.
- Fortress, Farm, Barracks, Archery Range, and Stable use selected-pack retail
  presentation for every required construction, intact, damaged, destruction,
  and rubble state, including bib and Fortress-door presentation.
- Fords uses cooked retail height, blend, textures, roads, water, fog, sky,
  particles, and props for every required scenario.
- No placeholder cube, procedural masonry, generic silent substitute, or
  repository-authored parity art appears in a required runtime or capture path.
  Any unresolved retail type is listed and excluded from those paths.

### 3. Retail UI and audio closure

- Control bar, radar, portraits, command buttons, fonts, resource display,
  selection, production, victory, and defeat presentation come from the
  selected pack.
- All four unit production commands expose the source producer, price, build
  time, command-point cost, label, portrait, and button.
- Selected battalions expose retail portraits and source-cropped Attack Move
  and Stop buttons, and each command executes the matching order.
- Required selection, movement, attack, damage, death, construction,
  destruction, victory, and defeat events resolve and play.
- Explore, combat, victory, and defeat music transitions work. Music, voice,
  and mute controls remain usable during the live slice.
- No required UI or audio route falls back silently.

### 4. Complete playable loop

From a clean launch:

- Selection, movement, Attack Move, Stop, attack orders, the source
  `SCMoveHint` destination indicator, and control groups work.
- The north, center, and south named ford crossings are navigable; water
  outside valid crossings remains blocked.
- MenPorter placement and construction use the retail command path; Farm income
  works; all four unit types train from their declared producers.
- Fortress, Farm, Barracks, Archery Range, and Stable are present and usable by
  both sides.
- Combat reaches member attack, damage, death, structure damage, destruction,
  rubble, victory, and defeat without separating authoritative state from its
  visible/audio result.
- A player-win scenario destroys the enemy Fortress and completes.
- An unassisted enemy-AI scenario builds, trains, attacks, destroys the player
  Fortress, and completes.
- Three consecutive clean-start matches finish without a stalled production
  queue, navigation request, AI state, or outcome transition.

### 5. Focused and integration automation

- Every runner declared by `tools/gate-m2-focused.ps1` passes on the same
  identity tuple, including member combat/health, selection decal, Archer
  projectile/impact, MenPorter construction, terrain, HUD/audio, production,
  structure lifecycle, and effects.
- `tools/gate-retail.ps1` passes importer discovery, strict repeat builds,
  semantic provenance, selected-pack runtime, external-pack security, retained
  legacy regression, and export-firewall contracts.
- Test-suite and runner totals are diagnostic output, not milestone
  requirements; the named semantic contracts are the requirement.
- Any error, warning, leak, orphan, remaining-resource, or RID-allocation
  diagnostic fails acceptance.

### 6. Oracle and reliability

- Every capture pair below is human-approved for the same identity tuple, with
  zero unresolved severity-0 or severity-1 audiovisual difference.
- Each row preserves viewport, camera state, retail and Godot image digests,
  reviewer, and notes. Evidence from another identity is stale.
- A 1,800-active-second live soak completes without crash, hang, multi-second
  gameplay stall, missing-resource diagnostic, warning, leak, orphan, or
  continuing memory growth.
- Three consecutive restarts mount the same bundle and reproduce the same
  deterministic acceptance signature.
- Viewport, renderer, average FPS, one-percent-low FPS, peak memory, and memory
  growth are recorded against thresholds frozen before the final soak; a
  threshold cannot be relaxed after observing a failure.

### 7. Containment and retention

- Git and public export contain no retail payload.
- The selected bundle, effective source view, profiles, manifests, reports,
  oracle evidence, and pinned tools remain retained.
- Cleanup is dry-run first and cannot remove the selected bundle. Failed
  transactions and scratch jobs are pruned only after reproducibility is proven.

## Required oracle IDs

```text
map-overview
ford-north
ford-center
ford-south
player-base
enemy-base
unit-soldier-idle
unit-soldier-move
unit-soldier-attack
unit-soldier-death
unit-archer-idle
unit-archer-move
unit-archer-attack
unit-archer-death
unit-tower-guard-idle
unit-tower-guard-move
unit-tower-guard-attack
unit-tower-guard-death
unit-knight-idle
unit-knight-move
unit-knight-attack
unit-knight-death
structure-fortress-construction
structure-fortress-intact
structure-fortress-damaged
structure-fortress-rubble
structure-farm-construction
structure-farm-intact
structure-farm-damaged
structure-farm-rubble
structure-barracks-construction
structure-barracks-intact
structure-barracks-damaged
structure-barracks-rubble
structure-archery-range-construction
structure-archery-range-intact
structure-archery-range-damaged
structure-archery-range-rubble
structure-stable-construction
structure-stable-intact
structure-stable-damaged
structure-stable-rubble
hud-default
hud-unit-selected
hud-production
hud-victory
hud-defeat
```

`tools/m2-oracle-common.ps1` owns the executable capture-ID list. This document
and that list must match exactly.

## Final declaration

Only the integration owner runs:

```bat
run_m2_acceptance.bat -IntegrationOwnerPublish
```

There are no waivers, fixture substitutions, inherited results, or partial
completion states.
