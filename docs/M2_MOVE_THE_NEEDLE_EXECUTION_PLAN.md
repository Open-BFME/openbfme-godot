# M2 move-the-needle execution plan

**Date:** 2026-07-15  
**Authority:** `DIRECTION.md`, `docs/M2_MEN_FORDS_DOD.md`, retail BFME2 1.06 effective source tree  
**Scope:** private Men versus Men on Fords of Isen II only

## Decision

The project does not need another converter subsystem or broader content census
before M2. The shortest path to a meaningful result is to restore one playable,
identity-consistent selected bundle, then close its visual oracle and reliability
gates. Work that does not change those gates is deferred.

## Live audit findings

1. The source tree, selected pack, and evidence identity have drifted apart.
   `DIRECTION.md` and the pending oracle name bundle `1b213661...`, while
   `.private/content-packs/selection.json` selects `52694a49...`.
2. The live slice gate currently stops at roster validation because the source
   now requires `bfme2.object.men-porter`, but the selected pack predates the
   builder asset/profile work.
3. The combat remediation is behavior-tested but not published: Archer dual
   weapons, individual targets, staggered fire, stances, target acknowledgement,
   corpse expiry, and radar mapping exist only in the dirty source identity.
4. Building lifecycle implementation is not the main blocker. Its focused gates
   pass 136 lifecycle assertions and 105 damage/audio/effect assertions.
5. The actual missing gameplay system was the Porter construction loop. Retail
   evidence establishes:
   - `MenPorterCommandSet` exposes Farm, Barracks, Archery Range, Stable, and
     Fortress construction.
   - Those commands are `DOZER_CONSTRUCT` and use `BCFarm`, `BGBarracks`,
     `BGArcheryRange`, `BGStables`, and `BGFortress`.
   - Farm is 300 / 22 seconds; Barracks 300 / 30; Archery Range 300 / 30;
     Stable 600 / 45; Fortress 5000 / 120.
   - MenPorter is 500 health, 60 speed, 25 vision, with source idle, move,
     death, run-to-work, and work clips.
6. Human oracle approval and the 30-minute soak are terminal gates, but doing
   them before source freeze would invalidate their identity immediately.

## Exact execution order

### P0-A: finish builder gameplay

- Convert and register MenPorter model, skeleton, textures, portrait, audio
  leaves, and work clips.
- Spawn one noncombatant builder per side.
- Show source construction buttons only for an all-builder selection.
- Arm placement, reject invalid/occupied/out-of-bounds sites, deduct exact cost,
  route the builder, enter the work animation state, advance authoritative
  construction progress for the INI duration, and activate economy/production
  only after completion.
- Add cancellation, interrupted-builder, rebuild/repair, and AI construction
  only after the happy path is deterministic.

Acceptance:

```bat
Godot_v4.7-stable_win64_console.exe --headless --path game --script res://tests/retail_builder_construction_runner.gd
```

### P0-B: produce one authoritative completion profile

- Re-run the existing additive profile composers in their documented order.
- Ensure the completion profile contains the Archer clips/rules, stance sources,
  Porter resources, Porter runtime definitions, construction UI leaves, and all
  prior terrain/HUD/building additions.
- Update exact resource counts and expected profile SHA only from the generated
  output; never hand-wave a stale hash.
- Run planning twice and require identical canonical JSON and zero missing
  required sources.

Acceptance:

```bat
python -m pytest importer/tests/test_retail_unit_rules.py importer/tests/test_retail_slice_profile.py importer/tests/test_integrity.py -q
```

### P0-C: rebuild and publish one immutable selected bundle

- Build twice without publishing and require identical bundle hashes.
- Publish only the second verified output through the integration-owner path.
- Verify `selection.json`, pack provenance, runtime object origins, and absence
  of a `.building` transaction.
- Update `DIRECTION.md`, DoD expected identity, gate sentinels, and oracle
  workspace together. There must be one profile hash and one bundle hash.

Acceptance:

```bat
run_retail_slice.bat --test
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate-m2-focused.ps1
```

### P1: close player-visible parity defects

Run the game at 1920x1080 and review, in this order:

1. radar orientation, aspect, click mapping, unit/structure dots, camera polygon;
2. Archer bow draw/release, projectile timing, melee switch, and attack audio;
3. target marker lifetime and selection ownership;
4. stance command art and Hold Ground/Battle/Aggressive behavior;
5. building construction, production door, damage, collapse, rubble, audio,
   debris, and particle density;
6. corpse density and the 60-second cleanup boundary;
7. builder placement, work animation, interruption, and completion.

Every defect becomes a focused behavior or capture assertion before a fix.

### P2: freeze and approve the canonical oracle

- Freeze source and create a new oracle workspace for the new identity.
- Capture all 47 required retail/Godot pairs.
- Record differences; fix severity-0/1 issues; recapture invalidated rows.
- Do not approve by similarity score alone. Human review remains required.

### P3: reliability and final declaration

- Freeze performance thresholds before the soak.
- Run three clean starts and 1,800 active seconds.
- Require three completed matches, identical deterministic signatures, no
  forbidden diagnostics, and bounded memory growth.
- Run `run_m2_acceptance.bat -IntegrationOwnerPublish` only after oracle and
  reliability evidence target the same identity.

## Deferred until M2 is green

- Other factions or maps
- Campaign, War of the Ring, or multiplayer
- Full W3D corpus conversion
- Generic engine refactors
- Additional HUD reverse engineering that is not visible in one of the 47
  required captures
- Synthetic replacement content

## Implementation status from this pass

- P0-A happy path implemented and behavior-tested: exact five-role costs/times,
  source UI commands, placement validation, builder travel/work state,
  authoritative progress, completion event, and economy activation.
- P0-B profile inputs now include MenPorter and the prior combat remediation,
  but the complete generated profile still needs recomposition.
- P0-C is intentionally not started against the incomplete profile.
