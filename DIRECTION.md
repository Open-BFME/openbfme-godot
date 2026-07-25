# OpenBFME product direction

**Owner:** Jonathan, project owner
**Owns:** stable target, scope ladder, parity definition, and non-goals
**Does not own:** hashes, gate results, task queue, or implementation detail
**Update trigger:** the product target or scope changes
**Where current results live:** [STATUS.md](STATUS.md)

## North star

A modern, independently distributed, easily moddable RTS engine in Godot that
reproduces *The Lord of the Rings: The Battle for Middle-earth II* — including
*The Rise of the Witch-king* — from measured evidence taken off the original
game, not from guesswork or approximation.

The compatibility build converts content from a retail installation the user
already owns. Retail and converted-retail payloads never leave `.private`. A
public distribution contains project-authored code and legal-safe fixtures only.

## The target is RotWK 2.01

**Rise of the Witch-king 2.01 is the product baseline.** BFME2 1.06 is the base
game underneath it and remains the source of evidence for everything RotWK does
not change.

This replaces the earlier position, which pinned the target to BFME2 1.06 and
treated RotWK as "a separate future overlay". That framing is retired. RotWK is
not an overlay on the product; it is the product. Angmar is a playable faction,
not an extension.

Practical consequence: where 1.06 and 2.01 differ, 2.01 wins. Where a RotWK
capability is not yet converted, that is a gap to close, not a scope boundary.

## Scope ladder

The ladder orders **acceptance**, not code presence. A capability existing in the
tree does not advance a rung; only evidence does.

1. **Skirmish parity.** Every faction, playable against every other faction, on
   the official skirmish and multiplayer maps. Units, hordes, heroes and hero
   forms, ring heroes and ring mechanics, structures, walls and fortresses,
   powers, sciences and upgrades, neutral objects and creeps, naval play, and
   the AI ladder.
2. **Campaign.** The Good and Evil campaigns: campaign maps, scripting,
   objectives, cinematics, campaign UI and audio. Sequenced in
   [docs/CAMPAIGN_PLAN.md](docs/CAMPAIGN_PLAN.md).
3. **Multiplayer.** Self-hosted local, listen, and dedicated-server play for up
   to eight players over deterministic lockstep.
4. **Shell and persistence.** Create-a-Hero, the full skirmish shell, saves,
   replays, observers, and custom-map / scenario tooling.
5. **Modern product.** Accessibility, HD presentation packs, mod management,
   safe mode, diagnostics, and rollback updates — none of which may change the
   parity profile.

## What changed, and why it is written down

Three positions in the previous edition of this document have been overridden by
the owner and must not be reintroduced by inference from older material:

| Previously | Now |
|---|---|
| Target pinned to BFME2 1.06; RotWK a future overlay | RotWK 2.01 is the baseline |
| Campaigns "outside product scope, not later roadmap promises" | Campaigns are rung 2 of the ladder |
| Ladder rung 1 was "Men versus Men on Fords of Isen II" | Rung 1 is cross-faction skirmish parity |

Documents, contracts, and code comments elsewhere in the tree still carry the old
framing. Where they do, this document wins.

## Meaning of parity

"Near 1:1" means every included capability is **discovered from the effective
retail source corpus** and carries source, conversion, runtime, simulation,
presentation, oracle, and reliability evidence.

INI presence is not parity. Converted-asset counts are not parity. A screenshot
that looks right is not parity. Unknown, ambiguous, unsupported, substituted, or
unclassified requirements fail closed — the correct outcome for something we
cannot prove is a recorded blocker, never a plausible-looking guess.

Identifiers are discovered from the source data. This document deliberately
encodes no list of factions, heroes, maps, powers, or objects; hard-coding those
would let the corpus and the contract drift apart silently.

## Permanent product constraints

- Eight players maximum.
- Godot owns presentation, input, UI, audio, and desktop integration.
- The authoritative simulation is deterministic and separable from presentation;
  presentation stays render-rate independent.
- Multiplayer is server-refereed deterministic lockstep and self-hostable.
- No Steam dependency, no ranked service, no mandatory account.
- Gameplay and presentation mods are versioned and hashed separately.
- Parity never silently substitutes synthetic or generic replacement art. If the
  real asset is not available, the gap is recorded, not filled.
- Retail payloads stay under `.private` and never enter a public artifact.

## Non-goals

- **War of the Ring.** The strategic layer is out of scope. Its maps carry
  almost no scripting, and it lives in `livingworld*.ini` data that nothing in
  the pipeline imports. This is a scope decision, not a sequencing one.
- Synthetic gameplay features with no retail counterpart.
- Reproducing the retail online service, matchmaking, or account system.
- Broad speculative refactors of the importer or renderer that are not driven by
  a parity gap.
- Declaring any rung complete without its evidence gate.
