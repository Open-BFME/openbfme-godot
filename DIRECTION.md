# OpenBFME — Active direction (integration owner)

**Updated:** 2026-07-15
**Owner:** integration orchestrator (this session)
**Authority:** overrides exploratory stage work when they conflict

## North star

Private **BFME2 1.06** playable skirmish port in **Godot 4.7**, using retail assets from a user-owned install, converted into private packs under `.private` only.

## Private-project operating assumption

- Local extraction, conversion, integration, and playtesting of the user-owned
  retail install are active project work; they are not blocked on a separate
  legal-review or public-release process.
- `.private` is the in-repository working area for the complete retail and
  converted-retail payload. Its containment is a technical release firewall,
  not a claim that the private compatibility build must use replacement art.
- After the private game works, any public release is a distinct code-only
  cleanup: exclude `.private`, scan the export, and publish only our engine,
  importer, tests, and legal-safe fixtures.
- After the Men/Fords gate, expand the same deterministic conversion path to
  the rest of the BFME2 base-game factions, units, structures, effects, UI,
  audio, and multiplayer maps. Do not replace that closure with synthetic art.

## Retail corpus state

- Complete effective-view extraction is done under `.private`: 40,129 winning
  files / 4,200,142,436 bytes, independently hashed twice with zero missing,
  extra, corrupt, unclassified, unstable, metadata, link, or case-collision
  anomalies.
- The retail registry resolves 46 shipped official multiplayer maps. Five
  additional multiplayer registry records have no payload in the retail
  archives and are tracked as stale source records, not converter failures.
- Extraction completeness is not conversion or runtime completeness. Every
  format still needs a deterministic converter, Godot binding, and rendered or
  behavioral acceptance gate before it is counted as working.
- The first full-corpus coverage ledger reconciles all 40,129 winners as 1,607
  currently converted, 15,329 with a proven converter, 23,147 evidence-only,
  40 not yet supported, and 6 not needed at runtime. These are source-file
  states, not gameplay-completion percentages.
- All 46 multiplayer map sources pass the strict structural cook, but none yet
  has a corpus-level Godot navmesh/routing proof and all still contain
  unclassified map chunk families. Only 28 of 12,751 W3D sources currently
  have attested pack outputs. These are the next expansion bottlenecks after
  the Men/Fords rendered gate.

## This week’s definition of done (M2 visual slice)

The binary integration and evidence contract is
[`docs/M2_MEN_FORDS_DOD.md`](docs/M2_MEN_FORDS_DOD.md).

A human can launch the retail path and play **Men vs Men on Fords of Isen II** where:

### Graphical / audiovisual fidelity (primary)
- All four Men units use imported retail models/materials/anims (Soldier, Archer, Tower Guard, Knight)
- All five structures use imported retail models (no procedural masonry fallback when pack GLBs exist)
- Fords terrain uses cooked retail terrain textures with Godot blending (not flat gray/decimated-only look)
- High-count map props resolve to converted retail meshes where conversion succeeds; unresolved types are listed, not silently faked as “done”
- Unit portraits, command buttons, and control-bar art come from converted retail UI leaves
- Music + unit VO + SFX for the slice roster route from the private pack

### Playable loop (required, rules may be approximate)
- Select / move / attack / death with imported presentation
- Farm income + train all four units from production buildings
- Place or use the five structure roles; damage/destroy enemy Fortress → win/lose
- Enemy AI completes a basic build/train/attack loop

### Explicit non-goals this week
- New synthetic proof stages 1–10 features
- Campaign / WotR / multiplayer
- Full oracle micro-parity for every weapon timing
- Declaring `vertical_slice_complete: true` without the checklist above
- Writing retail bytes outside `.private`
- Silent generic props presented as retail fidelity

## Architecture (unchanged)
```
F:\BFME2 → importer → .private content pack → Godot runtime (no BIG/W3D/INI/map at play)
```

## Scoreboard (daily)
1. Visual: terrain blend, props resolved %, structures retail-bound, 4 units on field
2. UI/audio: retail command art + strings + events wired
3. Play: four-unit production, fortress victory, AI finishes a match
4. Gates: focused retail tests green; do not weaken assertions

### Current integration snapshot (2026-07-15)

- The completion profile plans cleanly from `F:\BFME2` with zero missing
  required inputs and 2,559 unique selected files. Its SHA-256 is
  `0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d`.
- Independent strict Build A and Build B runs produced the same immutable
  bundle byte-for-byte:
  `69fd5efe0dfd77a9475250a102c52691044dff0b7d8216b873d725dd22de4cc1`.
  The selected publication contains 2,253 checked files, 2,240 declared
  outputs, and 2,593 semantic-provenance records with no incomplete conversion
  reason and a valid audit. No `.building` transaction remains. This bundle
  adds the retail-exact per-unit gameplay rules (locomotor speed, attack
  range, weapon timing, horde formation), the MenPorter builder loop, and the
  four retail HUD UI-sound leaves with their six audio-registry events.
- The selected pack is the only authority for the slice. Its terrain, roads,
  water, fog, static and animated props, particles, four Men battalions, five
  structure roles, Archer projectile and impact presentation, audio, and
  APT/WND HUD compose in one live Forward+ scene.
- Battalion movement remains squad-authoritative while health, damage, attack
  tokens, attack/death presentation, and health bars are per member. Selection
  now uses one source-textured merged formation outline rather than invented
  per-member rings or an opaque fill. Archer attacks use the exact selected-pack
  streak, impact model, weapon/impact leaves, and independently timed member
  poses.
- Unit selection now exposes the converted retail portrait plus source-cropped
  Attack Move and Stop commands. Ground movement uses the retail
  `MoveHint -> SCMoveHint` W3D and its three authored textures; no procedural
  route ribbon or destination flag is used in private parity mode.
- All 239 playable-slice assertions pass (retail_slice_runner), plus 175
  pack-runner and 12 builder-construction assertions, both with and without
  the `OPENBFME_CONTENT` override. The 21-runner focused gate
  (`tools/gate-m2-focused.ps1`) passes against the same profile and bundle.
  Scripted-battle budgets reflect retail movement speeds (victory by tick
  ~12,058 within a 14,000 budget; unassisted defeat by ~31,576 within
  36,000).
- A fresh 1920x1080 Forward+ capture on the NVIDIA RTX 4090 proves the composed
  selected Archer attack, merged selection decal, per-member health, terrain,
  structures, fog, HUD, and projectile path. This diagnostic render is not a
  retail-parity approval.
- Source work is ready to freeze for the canonical oracle. Completion still
  requires all 47 identity-bound retail/Godot capture pairs to receive human
  approval with zero severity-0/1 differences, followed by the pre-thresholded
  1,800-second live soak with three restarts/matches and the final integration-
  owner acceptance entry point. Original-game capture also requires the
  operator to approve one Windows elevation prompt; automation cannot approve
  that security boundary.
- Known fidelity questions remain deliberately open until the oracle answers
  them: exact selection color/opacity/throb, roster/material lighting, sky,
  shadows and water, structure construction/damage/rubble and production-door
  clips, HUD raster/compositor details, and exact animation/projectile timing.
  None may be waived or replaced with synthetic presentation.

## Worker law
- One integration owner merges and re-issues contracts
- Every Codex/subagent task names allowed paths, forbidden paths, acceptance command
- No open-ended “improve the engine” briefs
- Fail closed in private parity mode
- Path locks: concurrent workers must not edit the same files
