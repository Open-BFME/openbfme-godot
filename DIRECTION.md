# OpenBFME product direction

**Owner:** Jonathan, project owner
**Owns:** stable target, scope ladder, parity definition, and non-goals
**Does not own:** current hashes, gate results, task queue, or implementation detail
**Last verified commit:** `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
**Update trigger:** the product target or scope changes
**Validation:** `contracts/bfme2-106-product-scope.json`

## North star

Create a modern, independently distributed and easily moddable RTS engine in
Godot that reproduces BFME2 1.06 through measured retail/original-game evidence.

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
5. Complete the Good and Evil campaigns, War of the Ring, Create-a-Hero, shell,
   saves, replays, observers, and custom-map/scenario tooling.
6. Add modern accessibility, HD presentation packs, mod management, safe mode,
   diagnostics, and rollback updates without changing the parity profile.

RotWK is a separate future overlay and cannot change BFME2 1.06 evidence.

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

## Active milestone

The binary active contract is [docs/MILESTONE_CURRENT.md](docs/MILESTONE_CURRENT.md).
Current evidence and blockers live only in [STATUS.md](STATUS.md).

## Non-goals before M2 acceptance

- New synthetic proof-stage features.
- Campaign, WotR, multiplayer, or RotWK implementation.
- Broad importer, presentation, or architecture refactors.
- Public-release automation beyond containment checks.
- Declaring completion without the identity-bound oracle and reliability gate.
