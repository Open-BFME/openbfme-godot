# M2 Men/Fords strict definition of done

> **Superseded:** `docs/MILESTONE_CURRENT.md` is the only active M2 DoD. This
> file is retained temporarily for migration review and must not direct work.

This document is the binary completion contract for the private BFME2 1.06
Men-versus-Men Fords of Isen II vertical slice. `DIRECTION.md` defines the
product target; this document defines the evidence required to mark that target
complete.

No result may be borrowed from another Git revision, import profile, or content
bundle. `vertical_slice_complete` remains `false` until every gate below passes
for one identity tuple:

```text
git revision + dirty-state digest + profile SHA-256 + bundle SHA-256
```

## Frozen scope

- BFME2 1.06 only.
- Men versus Men on Fords of Isen II.
- Retail payloads and converted retail outputs remain below `.private`.
- RotWK, campaign, War of the Ring, multiplayer, other factions, other maps,
  full-corpus conversion, and new synthetic stage product work do not count
  toward this milestone.

## Gate 1: exact reproducible pack

- Profile ID is `men-fords-v1`.
- Profile SHA-256 is
  `365c11634473c3cd553a8bb64109371edbc07501a9d7654589c2befdd3138a53`.
- The plan resolves 378 resources, 2,572 source-entry projections, and 2,538
  unique selected files with zero missing required inputs.
- Build A and Build B are byte-identical.
- Semantic provenance is valid and contains no incomplete conversion reason.
- The build emits no unsupported-feature, missing-texture, warning, leak, or
  orphan diagnostic.
- A clean private state root can reproduce the same bundle.

## Gate 2: immutable publication and selection

- The exact bundle is published below `.private/content-packs/bfme2-men-vslice`.
- `.private/content-packs/selection.json` names that immutable bundle.
- Godot mounts the same bundle and all required runtime definitions originate
  from it.
- No stale or implicit fallback pack supplies a required asset.
- No `bfme2-men-vslice.building` transaction remains after publication.
- A failed publication leaves the previous selection unchanged.

## Gate 3: retail graphical closure

- Soldier, Archer, Tower Guard, and Knight battalions each render 15 imported
  retail members with their required model, materials, skeleton, equipment,
  idle, move, attack, and death presentation.
- A selected Men battalion uses the source `MenGenericLevel1`
  `SHADOW_MERGE_DECAL` contract and both converted retail decal textures; the
  oracle approves its merged footprint, color, opacity, and throb timing.
- Archer arrows, streak, impact effect, and weapon audio use the selected-pack
  retail contracts.
- Fortress, Farm, Barracks, Archery Range, and Stable use selected-pack retail
  presentation for every lifecycle state required by their source definitions,
  including bib and Fortress door presentation.
- Fords terrain uses the cooked retail height, blend, texture, road, water,
  fog, sky, particle, and prop contracts exercised by the focused runners.
- No placeholder cube, procedural masonry, generic silent substitute, or
  repository-authored parity art appears in a required runtime or capture path.
- Every unresolved retail type is explicitly listed and absent from the
  required playable and capture paths.

## Gate 4: retail UI and audio closure

- The control bar, radar, portraits, command buttons, fonts, resource display,
  selection state, production state, victory state, and defeat state are bound
  to the selected private pack.
- All four unit production commands expose the correct producer, price, build
  time, command points, label, portrait, and button.
- Selected battalions expose converted retail portraits and the source-cropped
  Attack Move and Stop buttons; each button executes its matching order.
- Required selection, movement, attack, damage, death, construction,
  destruction, victory, and defeat audio events resolve and play.
- Explore, combat, victory, and defeat music transitions work.
- Music, voice, and mute controls work during the live slice.
- No required UI or audio route falls back silently.

## Gate 5: complete playable loop

From a clean launch:

- Selection, movement, Attack Move, Stop, attack orders, the source
  `SCMoveHint` destination indicator, and control groups work.
- The three named ford crossings are navigable and water outside valid
  crossings remains blocked.
- Farm income works.
- All four unit types train from their declared producers.
- All five structure roles are present and usable for each side.
- Combat reaches attack, damage, death, structure damage, rubble, victory, and
  defeat states.
- A player-win scenario destroys the enemy Fortress and completes.
- An unassisted enemy-AI scenario builds, trains, attacks, destroys the player
  Fortress, and completes.
- Three consecutive clean-start matches finish without a stalled queue,
  navigation request, AI state, or outcome transition.

## Gate 6: automated acceptance

The selected immutable bundle passes the exact expectations in
`tools/gate-retail.ps1`, including:

- importer suite: expected full count, zero failures;
- Stage 11/12 groups and routes: 26 passed, 0 failed;
- Stage 14/15 base loop: 31 passed, 0 failed;
- Stage 15 menu and audio: 25 passed, 0 failed;
- retail pack runtime: 175 passed, 0 failed;
- playable retail slice: 208 passed, 0 failed;
- external pack security: 64 passed, 0 failed;
- legacy regression: 101 passed, 0 failed; and
- export firewall and firewall self-test pass.

The final M2 wrapper also passes all 21 focused retail runtime contracts,
including per-member combat/health, the source-backed selection merge decal,
authoritative Archer projectile/impact presentation, and the retail MenPorter
construction loop against the same selected bundle.

Any error, warning, leak, orphan, remaining resource, or RID-allocation
diagnostic fails this gate.

## Gate 7: canonical retail-versus-Godot oracle

The private oracle manifest must contain approved retail and Godot captures for
these required IDs:

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

Every capture row records the same profile and bundle identity, viewport,
camera state, retail image SHA-256, Godot image SHA-256, approval state, and
notes. Completion requires zero unresolved severity-0 or severity-1 visual or
audiovisual differences. Approval from an older identity tuple is invalid.

The identity-bound operator workflow is:

1. Freeze source changes, then run `tools/new-m2-oracle-workspace.ps1`.
2. Before the final soak, run `tools/new-m2-oracle-approval.ps1` with explicit
   average-FPS, one-percent-low, peak-memory, and memory-growth thresholds.
   The command refuses to overwrite an existing threshold record.
3. Pose the exact retail or Godot scenario and use
   `tools/capture-m2-oracle-frame.ps1`. It captures only the exact named
   window, verifies the PNG dimensions, hashes it, and revokes any prior review
   for that row.
4. Compare both images and run `tools/review-m2-oracle-capture.ps1`. A row with
   any severity-0 or severity-1 difference remains unapproved.
5. After all 47 pairs pass and the final 30-minute reliability run passes,
   `tools/finalize-m2-oracle-approval.ps1` verifies every file, hash, reviewer,
   threshold, identity field, restart, and soak metric before setting the
   private approval to final.

These tools create evidence only below `.private/retail-work/oracle`; they do
not navigate the original game, invent a camera state, or auto-approve a
comparison.

## Gate 8: reliability

- A 30-minute live soak completes on the integration machine.
- No crash, hang, multi-second gameplay stall, missing-resource diagnostic,
  warning, leak, orphan, or continuing memory growth occurs.
- Three consecutive restarts mount the same bundle and reproduce the same
  deterministic acceptance signature.
- The integration evidence records viewport, renderer, average frame rate,
  one-percent-low frame rate, peak memory, and final memory. Numeric performance
  thresholds must be frozen in the oracle approval before final sign-off; they
  may not be relaxed after observing a failing run.

## Gate 9: containment and retention

- Git contains no retail payload.
- The export firewall finds no `.private` payload.
- The selected pack, effective source view, profiles, manifests, reports, and
  pinned tools remain retained.
- Cleanup is dry-run first and cannot remove the selected bundle.
- Failed transactions, obsolete job roots, and scratch artifacts are pruned
  only after reproducibility is proven.

## Final declaration rule

`run_m2_acceptance.bat -IntegrationOwnerPublish` is the final integration-owner
entry point. It must pass on the same identity tuple as the private oracle
approval. There are no waivers, fixture-only substitutions, inherited results,
or "mostly working" completion states.
