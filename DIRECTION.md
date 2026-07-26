# OpenBFME product direction

**Owner:** Jonathan, project owner
**Owns:** stable target, scope ladder, parity definition, and non-goals
**Does not own:** current hashes, gate results, task queue, or implementation detail
**Last verified commit:** `ad370cc9b02bdec600564cf1c606e70833faa97a`
**Update trigger:** the product target or scope changes
**Validation:** `contracts/bfme2-106-product-scope.json`

## North star

Create a modern, independently distributed and easily moddable RTS engine in
Godot that reproduces BFME2 1.06 skirmish play through measured
retail/original-game evidence.

The compatibility build uses locally converted content from a user-owned retail
installation. Retail and converted retail payloads stay under `.private`. A later
public distribution contains project-authored code and legal-safe fixtures only.

## Scope ladder

1. Complete and freeze Men versus Men on Fords of Isen II.
2. Complete the full Men faction, including every BFME2 1.06 Men hero, on the
   selected five-map oracle set.
3. Complete all six BFME2 factions, Ring mechanics, naval gameplay, neutral
   objects, and official skirmish/multiplayer maps.
4. Ship self-hosted local, listen, and dedicated-server play for up to eight
   players using deterministic lockstep.
5. Complete Create-a-Hero, the skirmish shell, saves, replays, observers, and
   custom-map/scenario tooling.
6. Complete the Good and Evil campaigns, including campaign maps, mission
   scripting, objectives, and cinematics.
7. Complete War of the Ring: the strategic layer, territories, armies, and the
   strategic-to-tactical handoff, single-player and multiplayer.
8. Add modern accessibility, HD presentation packs, mod management, safe mode,
   diagnostics, and rollback updates without changing the parity profile.

RotWK is a layered overlay and cannot change BFME2 1.06 evidence.

The ladder orders acceptance, not code presence. The development tree already
carries unaccepted surfaces from steps 2 and 3 — six faction runtimes, a
five-map development set, heroes, powers, and spellbooks — while step 1 remains
the only active acceptance target. Presence of those surfaces does not advance
the ladder; only the evidence gates in [docs/MILESTONE_CURRENT.md](docs/MILESTONE_CURRENT.md)
do. Current runtime evidence lives in [STATUS.md](STATUS.md).

## Meaning of parity

"Near 1:1" means every included capability is discovered from the effective
BFME2 1.06 source corpus and has the required source, conversion, runtime,
simulation, presentation, oracle, and reliability evidence.

INI presence and converted-asset counts are not parity. Unknown, ambiguous,
unsupported, substituted, or unclassified requirements fail closed.

## Permanent product constraints

- Eight players maximum.
- Godot owns presentation, input, UI, audio, and desktop integration.
- Pure C# owns deterministic authoritative simulation.
- Production simulation targets 30 Hz; presentation remains render-rate independent.
- Multiplayer is server-refereed deterministic lockstep and self-hostable.
- No Steam, ranked-service, or mandatory-account dependency.
- Gameplay and presentation mods are versioned and hashed separately.
- Private parity never silently uses synthetic or generic replacement art.
- The Good and Evil campaigns, campaign maps and scripting, and War of the Ring
  are in scope. Owner decision, 2026-07-26, reversing the previous exclusion.
  They sit late in the scope ladder because they depend on the faction, hero,
  and scripting work below them, not because they are optional.

## Active milestone

The binary active contract is [docs/MILESTONE_CURRENT.md](docs/MILESTONE_CURRENT.md).
Current evidence and blockers live only in [STATUS.md](STATUS.md).

## Non-goals before M2 acceptance

- New synthetic proof-stage features.
- Campaign or War of the Ring implementation before their scope-ladder steps
  are reached. They are in scope; they are not the active milestone.
- Broad importer, presentation, or architecture refactors, except under an
  authorized cleanup packet with two reviewers.
- Public-release automation beyond containment checks.
- Declaring completion without the identity-bound oracle and reliability gate.
