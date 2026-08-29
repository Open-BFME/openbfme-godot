# Product direction

OpenBFME has one product target: an exact, clean-room Godot port of **The Lord
of the Rings: The Battle for Middle-earth II - The Rise of the Witch-king,
Patch 2.02 v9.7.7**.

This document owns the outcome and enduring product principles. It does not own
live task state or completion declarations.

## Authority

- [Product scope](contracts/rotwk-202-v9.7.7-product-scope.json) defines the
  included product domains and completeness claims.
- [Retail baseline](contracts/rotwk-202-v9.7.7-baseline.json) and the
  [English overlay](contracts/rotwk-202-v9.7.7-english-overlay.json) identify
  the accepted original-game source.
- [Architecture](docs/ARCHITECTURE.md) defines current executable authority and
  target boundaries.
- [Verification](docs/VERIFICATION.md) defines acceptable evidence.
- [Roadmap](docs/ROADMAP.md) orders the program.
- [Work items](orchestration/work-items.json) are the only live task ledger.

## Exact source target

Patch 2.02 v9.7.7 is a three-layer effective source:

```text
layer 0: Patch 2.02 official-2 v9.7.7 English overlay
layer 1: RotWK 2.01 installation
layer 2: BFME2 1.06 installation
```

Archive precedence, inheritance, references, and loose-file rules determine the
effective winners. Work against another patch, an incomplete layer set, or an
unverified archive composition cannot satisfy this target.

## North star

The finished product reproduces the complete player-facing and simulation
behavior of the pinned game in Godot, including:

- skirmish factions, units, heroes, hordes, structures, fortresses, powers,
  upgrades, neutral objects, naval play, maps, rules, AI, and scripts;
- the retail shell, HUD, localization, input behavior, options, profiles,
  Create-a-Hero, replay/save flows, audio, animation, particles, and effects;
- Good, Evil, and Angmar campaign content, objectives, scripted missions,
  cinematics, and transitions;
- War of the Ring strategic play and tactical handoff; and
- multiplayer behavior required by the product-scope contract.

Ordering is not exclusion. A domain may be sequenced after shared foundations
without being removed from the complete-product denominator.

## Development model

Work is systems-first and evidence-first:

1. Select a bounded row from `orchestration/work-items.json`.
2. Identify the exact effective 2.02 source and runtime consumer.
3. Reproduce the named gap with the smallest focused check.
4. Implement through the canonical importer/runtime boundary.
5. Verify conversion, strict loading, behavior, and any required visual/audio
   oracle dimensions against one frozen identity.
6. Integrate only after independent review.

Broad file generation, parser coverage, module recognition, asset counts, or a
launchable scene are useful lower-level evidence. None is a parity percentage.

## System program

The program widens through these dependent systems. Their current sequence and
owners live in the roadmap and work-item ledger.

1. **Source authority** - pinned layers, archive precedence, effective winners,
   provenance, and a complete discovery denominator.
2. **Neutral content IR and cook** - deterministic schemas for objects, maps,
   UI, scripts, audio, animation, effects, and strategic data.
3. **Immutable bundle composition** - a small, comprehensible selected bundle
   set with verified addresses and no runtime dependency on importer caches.
4. **Authoritative simulation** - deterministic commands, state, events, RNG,
   economy, production, combat, movement, powers, AI, scripts, persistence,
   replay, and state digests.
5. **Godot presentation** - terrain, world objects, animations, FX, audio,
   camera, input, HUD, and shell as projections of authoritative state.
6. **Skirmish closure** - every retail-reachable setup and complete-match path
   across the source-discovered denominator.
7. **Authored modes** - tutorials, campaigns, Create-a-Hero, War of the Ring,
   cinematics, credits, and their transitions.
8. **Multiplayer and recovery** - lobbies, deterministic sequencing, reconnect,
   observer, persistence, and required scale/reliability behavior.
9. **Product and release** - localization, accessibility, mod boundaries,
   containment, packaging, diagnostics, and repeatable release qualification.

## Runtime authority

The current executable gameplay authority is the GDScript `RetailSliceSim`
path described in `docs/ARCHITECTURE.md`. Godot presentation constructs and
consumes that simulator today. The standalone C# engine is an experiment, not
shipping authority and not completion evidence.

A future authority change requires an explicit architecture decision plus
exact command/state/event trace equivalence. No broad rewrite may discard
accepted behavior or split gameplay truth between two simulators.

## Parity rules

- Discover requirements from the effective source; do not maintain convenient
  hand-authored denominators in prose.
- Missing, ambiguous, unsupported, substituted, or unclassified requirements
  fail closed.
- Strict retail paths do not use synthetic data, generic art, guessed rules, or
  silent fallback.
- Simulation, visual, and audio fidelity are separate required evidence lanes.
- A pack address must match its bytes, and the runtime must prove which
  selection it mounted.
- Original-game observation and source evidence outrank implementation intent.
- `SKIP` never means success.

The exact evidence ladder and complete definition of done are in
[docs/VERIFICATION.md](docs/VERIFICATION.md).

## Historical work

Earlier engineering established useful importer, pack, GDScript simulation,
HUD, map, W3D, APT, save/load, lockstep, launcher, and release foundations.
Much of it was built or measured against historical 2.01-era selections and
bounded Men/Fords slices. Preserve it as implementation and regression evidence,
but do not carry its checked status, counts, pins, or parity claims into 2.02.

Every reused capability must be re-bound to the pinned v9.7.7 source and pass
the current evidence contract.

## Non-goals

- Treating a convincing skirmish slice as the whole product.
- Improving or rebalancing retail behavior inside the parity profile.
- Loading raw retail archives from the shipping runtime.
- Maintaining separate predecessor-edition and v9.7.7 parity programs.
- Counting code, parsers, runners, converted assets, or dispatchable calls as
  product completion.
- Allowing historical queue entries, receipts, or screenshots to overrule the
  current contracts and work-item ledger.
