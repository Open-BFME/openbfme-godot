# OpenBFME system plan - RotWK Patch 2.02 v9.7.7

This is the durable system map, not a completion checklist. Live ordering is in
[docs/ROADMAP.md](docs/ROADMAP.md); live ownership and status exist only in
[orchestration/work-items.json](orchestration/work-items.json).

## Governing contracts

- [Product scope](contracts/rotwk-202-v9.7.7-product-scope.json)
- [Retail baseline](contracts/rotwk-202-v9.7.7-baseline.json)
- [English overlay](contracts/rotwk-202-v9.7.7-english-overlay.json)
- [Architecture](docs/ARCHITECTURE.md)
- [Verification](docs/VERIFICATION.md)

No item below is complete because an older plan marked it complete. Completion
requires current v9.7.7 evidence at every level required by verification.

## 1. Establish the exact source authority

Build and protect one reproducible three-layer English source catalog. Resolve
archive precedence, inheritance, references, maps, media, localization, and all
unknown source families into an effective-winner graph.

Exit condition: every product-scope root is classified; every archive and
effective path is identity-bound; unknowns remain explicit; source closure is a
machine-evaluated result.

The source program is a strict four-layer pipeline. Agents must never collapse
one layer into the next:

1. **Effective tree** - archive precedence and byte provenance for all 53,433
   raw records and 48,566 winners.
2. **Lexical census** - exhaustive syntax accounting for definitions,
   assignments, modules, scripts, and asset-reference sites. Opaque and
   unresolved rows remain census results, not semantic closure.
3. **Semantic requirement graph** - execute every product root query, resolve
   typed reference/provenance edges, classify domain and map/mode membership,
   and retain every residual as a blocker.
4. **Evidence and routing** - project each semantic obligation into stable
   requirements/objectives and apply the integration-owner routing contract to
   one terminal envelope, exact mutable-file set, prerequisites, command, and
   required evidence dimensions.

Only the fourth layer can feed autonomous worker-lane generation. Counts from
the first two layers never authorize a worker to infer a semantic target or a
code owner.

## 2. Define the neutral content model

Separate source parsing from runtime interpretation. Version schemas for object
facts, modules, weapons, armor, locomotion, hordes, powers, upgrades, AI,
scripts, maps, W3D/APT/WND presentation, audio, effects, cinematics, campaigns,
and War of the Ring.

Exit condition: authored facts retain provenance and unresolved semantics;
importers do not pre-bake guessed gameplay decisions.

## 3. Make cooking deterministic and comprehensible

Consolidate the importer into edition-aware `doctor`, `catalog`, `cook`,
`verify`, `publish`, and `select` operations. Produce a small stable bundle set
instead of a history-shaped chain of repair overlays.

Exit condition: clean and repeated cooks are byte-identical; every bundle
address verifies; publication is immutable and transactional; runtime needs no
source install or importer cache.

## 4. Close the authoritative simulation

Preserve GDScript as current authority while extracting stable boundaries for
commands, world/entity state, economy/production, movement/pathing,
combat/damage, powers/upgrades, AI/scripts, persistence/replay, and deterministic
digests.

Exit condition: every required gameplay rule is source/oracle-backed, runs in
the authoritative match loop, produces deterministic state/events, and has no
strict-path fallback.

## 5. Close Godot presentation

Separate match bootstrap, input/camera, terrain/world projection, entity
presentation, animation, FX, audio, HUD, and shell from authoritative state.
Consume authored W3D, APT, WND, texture, audio, and effect data without
procedural substitutes in parity mode.

Exit condition: source-discovered presentation rows load from exact selected
bundles and pass matched-condition visual/audio oracles.

## 6. Qualify skirmish end to end

Exercise source-discovered maps, factions, teams, handicaps, rules, AI,
neutral/naval systems, ring and custom heroes, saves, replays, victory/defeat,
and the complete shell-to-match-to-shell journey.

Exit condition: protected private gates complete representative and boundary
matches with deterministic replay/save restoration and zero unexpected
diagnostics; the full skirmish denominator has required oracle evidence.

## 7. Implement every authored mode

Convert and run tutorials, Good/Evil/Angmar campaigns, objectives, scripted
missions, cinematics, Create-a-Hero authoring, War of the Ring strategic play,
tactical handoff, credits, and mode-specific persistence/UI/audio.

Exit condition: no product-scope mode remains disabled, substituted, or
represented only by parsed data or an isolated screen.

## 8. Qualify multiplayer and recovery

Define canonical command serialization and session identity. Verify required
player counts, lobbies, sequencing, latency/loss/reordering, late commands,
checkpoints, reconnect, observer, desync handling, and self-hosted deployment.

Exit condition: supported platforms agree on authoritative digests and recover
according to one tested protocol without weakening single-player parity.

## 9. Finish product and release boundaries

Complete localization, accessibility, options, profiles, mod compatibility,
safe mode, diagnostics, launcher/update flow, code-only distribution,
third-party provenance, performance budgets, and reliability qualification.

Exit condition: a reproducible release contains only approved public bytes,
builds its private content locally from the pinned source, and passes the L6
qualification defined in `docs/VERIFICATION.md`.

## Historical engineering foundation - not 2.02 completion

Before the v9.7.7 retarget, the repository accumulated real engineering work:

- retail archive/catalog tooling, deterministic conversion, immutable pack
  publication, selection transactions, and provenance checks;
- playable multi-faction GDScript skirmish foundations, subsystem extraction,
  deterministic ticks, lockstep experiments, save/load, and replay tests;
- HUD, radial command, shell, W3D material, APT screen, map, animation, audio,
  and War of the Ring prototypes;
- importer and runtime censuses for modules, scripts, AI, objects, assets, and
  maps; and
- Windows launcher, bundle, containment, and release tooling.

Those accomplishments were developed and measured primarily against historical
2.01-era packs and bounded slices. They are useful code and regression evidence,
but none is a checked 2.02 parity item. Old counts, digests, state pins, runner
totals, audit scores, and queue closures must be regenerated or reverified
against the pinned v9.7.7 identity before reuse.

## Program decisions

- The exact target is Patch 2.02 v9.7.7; a moving or unversioned "2.02" target
  is unacceptable.
- Complete means the entire product-scope contract, not a skirmish-only release.
- GDScript is the current executable authority. C# remains an experiment unless
  a trace-qualified cutover is separately accepted.
- Retail mode fails closed. Synthetic fixtures remain test/development inputs,
  never parity substitutes.
- Original-game oracle review is required; builders do not approve their own
  fidelity claims.
