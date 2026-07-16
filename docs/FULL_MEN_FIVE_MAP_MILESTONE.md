# Full Men and five-map private compatibility milestone

> **Deferred and superseded as a milestone:** the active DoD is
> `docs/MILESTONE_CURRENT.md`; broader content sequencing lives in `PLAN.md`.

This is the active long-running implementation objective requested by Jonathan,
project owner. It expands the existing proof slice without redefining partial evidence
as completion.

## Frozen target

- Game data and gameplay authority: BFME2 patch 1.06.
- Faction: the complete normal-skirmish Men dependency closure.
- Mode: local skirmish with AI; online networking is not included.
- Maps: Fords of Isen II, Rivendell, Mount Doom, Dagorlad, and Mordor, using the exact
  registry-backed virtual paths in `MULTIPLAYER_MAP_CONVERTER.md`.
- Private retail content remains outside git and public exports.

ROTWK 2.01 is a separate future overlay. It adds and changes Men units, walls, upgrades,
and spellbook content, so its data cannot be merged into a BFME2 1.06 parity claim. The
supplied ROTWK video is useful for shared control-bar layout observation, but it is not
the BFME2 gameplay or roster oracle.

## Meaning of 1:1

Conversion necessarily changes W3D, DDS/TGA, audio, and map container formats. Here,
1:1 means every required retail dependency is accounted for and its visible, audible,
spatial, and gameplay result is measured against BFME2 1.06 oracle evidence. Missing,
ambiguous, unsupported, or substituted requirements fail closed.

Private 1:1 mode may not use generated images, procedural audio, repository-authored
palantir art, or generic models as silent fallbacks. Those assets remain valid for the
redistributable legal-safe pack, but private parity mode must identify a missing retail
dependency instead of hiding it.

## First resolved full-faction leaf checkpoint

Run:

```powershell
.private\retail-work\tools\python-3.12-env\Scripts\python.exe `
  tools\openbfme_import.py --json census-faction --install F:\BFME2 --faction men
```

The payload-free report is in the ignored private workspace at:

```text
.private\retail-work\reports\men-faction-leaf-census.json
```

Two complete runs of the expanded gameplay-definition census produced report SHA-256
`1299e97f8dd00808b64cecff186c236cdea87681396c8df8d6e41323eae17835`.
The effective BFME2 1.06 graph currently proves:

- 18 unique faction/hero/spellbook/fortress-composite roots;
- 57 command-reachable object definitions;
- 53 command sets and 160 command buttons;
- all 41 reachable upgrades resolve to typed, payload-free definition leaves;
- 38 total reachable special-power identifiers, including the exact 12-power Men
  spellbook and its 12 directly reachable sciences; prerequisite traversal expands the
  resolved science closure to 26 definitions;
- 160 mapped-image references yield 159 exact crop definitions across 78 compiled
  DDS/TGA atlas leaves;
  atlas leaves; the remaining `UPGondor_Banner` SelectPortrait is an explicit
  retail-authored source-null reference, not an invented fallback;
- all 380 reachable localization identifiers resolve. The shipped English catalog has
  source duplicates affecting 11 requested IDs, including 10 conflicting duplicates;
  the explicit BFME2 compatibility policy is source-order first-wins, with those 10
  retained as oracle-review evidence;
- 105 reachable object/command/gameplay-definition audio roots resolve through 115
  `AudioEvent` definitions and ten `Multisound` definitions to 474 exact sample leaves;
- four upgrade FX-list identifiers are retained as explicit next-layer dependencies;
- 552 unique mapped-image/audio source leaves are bound to exact archive entries; and
- zero missing or ambiguous command-level definitions.

This checkpoint is deliberately named
`command-ui-localization-audio-gameplay-definition-leaves`. It proves deterministic
source resolution for the current command-reachable graph, including typed upgrade,
science, and special-power edges and payload-free assignment digests. It does not
claim that models, animations, materials, FX-list bodies, every future runtime state,
Godot UI/audio routing, or oracle scenarios are complete. The private report includes
localized value hashes rather than retail string bodies; private pack generation will
consume the decoded values directly without checking them into this repository.

## Required implementation order

1. Materialize the complete effective BFME2 1.06 asset view under the ignored private
   workspace with a deterministic provenance manifest. This removes repeated archive
   access from later closure/conversion work without treating asset presence as parity.
2. Extend the census with typed W3D hierarchy, animation, material/texture, FX,
   weapon/projectile, and construction/damage/destruction leaves. Mapped-image,
   localization, event/sample audio, and upgrade/science/special-power definition
   resolution are now implemented.
3. Keep the generated bounded full-Men import profile in sync with that graph; the
   verified current profile contains 81 resources selecting 634 exact files with zero
   missing required inputs.
4. Replace the private Fords Soldier/Barracks HUD path with source-derived control-bar,
   command art, strings, and event-routed audio. Do not build a universal APT runtime
   before this exact path is accepted.
5. Complete units, hordes, heroes, builder, structures, walls, expansions, upgrades,
   powers, projectiles, FX, and all required lifecycle/animation states.
6. Complete the five maps' setup data, terrain presentation, object bindings,
   buildability, triggers/scripts, standing waves, AI, and deterministic dynamic
   navigation.
7. Capture BFME2 1.06 oracle scenarios for UI placement, scale, timing, movement,
   formation, economy, combat, powers, audio intent, AI, and victory behavior.
8. Automate the guided conversion only after the exact dependency and parity gates are
   stable.

## Completion gate

Completion requires all five maps to run full Men skirmishes without fallback or silent
omission, deterministic replay hashes across render rates, exact command/UI/audio
routing, map-specific routing tests, BFME2 oracle comparisons, viewport coverage,
performance/leak checks, a 30-minute soak, and both repository gates:

```bat
run_retail_pipeline_tests.bat
run_stage10_tests.bat
```
