# OpenBFME implementation plan

**Owner:** integration owner
**Owns:** permanent engineering decisions and milestone order
**Does not own:** volatile status, task assignments, retail identifiers, or gate results
**Last verified commit:** `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
**Update trigger:** an approved architecture or milestone-order decision changes
**Validation:** canonical documents linked from `README.md`

## Decisions

- BFME2 1.06 only until base-game completion.
- Eight players maximum.
- Python/pinned tools for conversion, pure C# for deterministic simulation,
  Godot/GDScript for presentation.
- Server-refereed deterministic lockstep.
- Retail parity fails closed and loads only the strict selected pack.
- Simulation and presentation mods have independent contracts and hashes.
- Self-hosted local/listen/dedicated play; no mandatory central service.
- Agent work uses an owner-controlled bounded loop with hostile review and a
  proposal-only retrospective.

## Phase A — Control plane

1. Preserve current user changes and record source/pack identity.
2. Establish the private queue, locks, metrics, canonical documents, policy
   contracts, and retrospective skill.
3. Remove contradictory authority only after its unique evidence has migrated.
4. Pilot one importer packet, one runtime packet, and one high-risk boundary
   packet before increasing concurrency.

## Phase B — Complete and freeze M2

1. Enforce selected-pack-only runtime definitions.
2. Complete the exact Men/Fords presentation and playable loop.
3. Approve all required retail/Godot oracle pairs for one identity tuple.
4. Complete the real-time reliability gate.
5. Pass `run_m2_acceptance.bat -IntegrationOwnerPublish` and tag the baseline.

## Phase C — Delete obsolete authority

1. Replace stale literal/count tests with semantic contracts.
2. Connect valuable music, performance, and input runners.
3. Transfer source-backed assertions from Stage/legacy systems.
4. Remove proof Stage 1–10 product routes, gates, sources, and large synthetic
   base content.
5. Remove legacy `SimWorld`/`MatchController`, preserving required presentation
   utilities such as `asset_factory.gd`.

## Phase D — Production simulator

1. Write a porting guide and state-ownership table.
2. Build language-independent movement, economy, and combat traces.
3. Mechanically port the current 10 Hz simulator to pure C# without redesign.
4. Dual-run identical commands, cut over once, then delete GDScript rules.
5. Convert to 30 Hz in a separate oracle-reviewed change.
6. Remove selection/control groups from authoritative state.

## Phase E — Lockstep and eight-player scale

Implement `MatchConfig`, `GameCommand`, `AcceptedCommandBatch`, `StateDigest`,
`Checkpoint`, `ReplayHeader`, `PlayerView`, and `MatchHost`.

- Same-frame presentation feedback.
- Two-tick default online command buffer, adapting between two and six.
- Digest every 30 ticks.
- Checkpoint every 300 ticks; retain six.
- Reconnect/observer join from checkpoint plus command tail.
- Late commands are rejected without stalling the match.
- Qualify the maximum legal eight-player BFME2 load plus 25% headroom.

## Phase F — Content completeness

1. Full Men and every Men hero across the five-map oracle set.
2. All six factions, every retail hero, Ring mechanics, siege, naval, neutral
   objects, walls, fortress customization, powers, upgrades, UI, audio, and AI.
3. Classify every effective retail map payload and complete all supported
   skirmish/multiplayer maps.
4. Ship the independent eight-player multiplayer beta.
5. Complete Good/Evil campaigns, WotR, Create-a-Hero, shell, saves, replays,
   observer flow, and custom-map/scenario tools.

## Phase G — Modern product

- HD/PBR presentation packs and validation.
- Accessibility, rebinding, scaling, ultrawide, subtitles, reduced motion, and
  high-contrast selection.
- Mod manager, safe mode, last-known-good pack, doctor/resume/launch flow,
  sanitized support bundles, and immutable updater rollback.

No phase advances from prose or file counts. Its generated coverage rows,
focused checks, adversarial review, and owner gate must be green.
