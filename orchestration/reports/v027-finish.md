# v0.2.7 alpha finish — 2026-08-19 (castle sieges)

Shipped `dist\v0.2.7\` + `dist\v0.2.7.zip` (13,876,236,696 bytes, sha256
`3b9e8f0f25eb7e4679c88532a794e14824acd1a46705a5d1684d7f0bb40e32e4`).
Publish-DistBuild exit 0 in 33 min (`workspace/logs/v027fin-publish.txt`).
Test-DistPipeline 25/25. Tree clean at publish (95e4b40).

## What this build carries

**Castle siege lanes, all merged to main with adversarial review gates:**
- **L3 defendable gates** (f8986f5; 2 reviews + fix-ups d6ef154/34ed226/dcfb83f):
  OpenByDefault, friendly auto-open rectangle (450x225 authored), auto-reset,
  authored-geometry blocking (Closed/OpenLeft/OpenRight disc sets), portal
  rules from the authored FakePathfindPortalBehaviour only, destroyed gate =
  permanent breach, lockstep toggle_gate + authored radial button
  (Command_ToggleGate labels/icon). castle_gate_runner 41/0.
- **L4 garrisonable towers** (0e8b78b; review + fix-ups 821c812): retail
  HordeGarrisonContain semantics (ContainMax in hordes, PassengerFilter,
  damage isolation 0, evict-alive, neutral capture), fabricated
  DEFAULT_TRANSPORT_CAPACITY deleted, WP03 garrison verbs unblocked,
  containment in state_snapshot. castle_garrison_runner 36/0.
- **L5 wall defenses + structure weapons** (83b212b; review + e7d0de0):
  importer resolves Min:/Max: DelayBetweenShots and conditional
  ProjectileNuggets — weaponless structures 64 -> 1 across all factions; wall
  hubs take mutually-exclusive authored upgrades and fire.
  castle_wall_defense_runner 14/0.
- **L6 importer half, walkable-wall data** (8fdca79 + aaaef69, Grok 4.6):
  walk-surface mesh roles compiled, HLOD pivots + W3D HIDDEN extras kept on
  structure GLBs (no-bake narrowed to hidden-proxy models), MenWallRamp
  admitted. Nav layer = next lane (Q51).
- **L7 siege AI** (d238f3c importer + ba0b625 runtime; 2 reviews): castle maps
  get the retail AI library composition + per-map AIBase layouts
  (skirmishaidata.ini + .bse); AI anchors to the castle start, builds on
  authored sites with real porter travel, attacks. Fixed the pre-existing
  waypoint-transform ordering bug (ALL named waypoints resolved to (0,0)).
  castle_skirmish_ai_runner 35/0 (Erebor/Carn Dum/Helm's Deep).
- **L9 presentation** (4ca9635; 2 reviews + 8b7b5c3/2a6fda2): destroyed pieces
  swap to authored rubble or hide (shroud-safe), KindOf selection, minimap,
  fog flags. castle_fixture_presentation_runner 27/0.
- **Fortress passive income** (9819203): the citadel's authored
  AutoDepositUpdate (25 / 6000 ms) binds from the deferred composite table —
  fortresses earn for player and AI. fortress_passive_income_runner 8/0
  (failing-first 4/3 without the fix).
- **Importer trailing-prose fix** (c90149c): retail's uncommented prose after
  a mesh name no longer kills the cook (helmsdeepbuildings.ini:3372).

**Content:** 7 faction packs recooked (116 min, PACK_PROOF 7/7; angmar
gaps=1 = AngmarWallPosternGateSmall, same honest residual): men `51d48854`,
elves `05e4b2a6`, dwarves `088d9019`, isengard `ad66347a`, mordor `975c3d6a`,
wild `765e92a7`, angmar `a3635f32`. Maps pack republished twice (first died on
the trailing-prose line): `rotwk-playable-maps-private/abc27325` (74 maps,
proof_ok) — every castle map ships fixtures.json with gate
commandSet/commandSetRows/aiGateUpdate/fakePathfindPortal + garrison blocks +
walkSurfaces, plus ai_bases.json and scripts.json.
Selection sha `f8c3b255` -> `bbdaa97e` (one transaction, both roots verified).

## Checkpoint (new selection, sequential)

state pin 0e4bcdbf OK; castle live boot 8/0 on erebor/helms-deep/minas-tirith/
carn-dum/black-gate; gate 41/0; garrison 36/0; wall_defense 14/0; presentation
27/0; skirmish AI 35/0 (Erebor sites now authored-ai-base:Men from
ai_bases.json); admission 47/0; lobby 11/0; fixture_spawn 32/0; loader 37/0;
roster 22/0; spellbook 218/0; hud 121/0; radial 48/0; mounted 9/0; mappers
21/0; combat 115/0; queue 29/0; formation 18/0; plots 24/0; income 8/0; boot
44/0; retail_slice_runner 371/59, failure NAMES identical to the v0.2.6
baseline (88-line sets, empty diff).

## Shipped-export proof (OPENBFME_CONTENT=dist\v0.2.7\content-packs)

roster 22/0 mounting men `51d48854`; castle live boot Erebor 8/0;
castle_gate_runner 41/0 from the shipped packs; ai_bases.json present in the
shipped Erebor map.

## Known gaps

- Walkable wall tops: data cooked (walkSurfaces + pivot-correct GLBs); the
  layered pathing runtime is Q51.
- Siege AI runtime-proven on 3 of 10 castle maps; the other 7 are compiled
  (authored layouts or generic fallback) but not runtime-tested.
- `_authored_site_foundation_fixture_contains` branch unreachable on shipped
  data (no foundation-role fixtures) and untested (review follow-up).
- Loco B; formation modifier plumbing; Q15/Q16; EVA overlays prior build.
