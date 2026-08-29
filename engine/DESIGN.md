# OpenBfme.Sim - deterministic module-based simulation kernel

> **HISTORICAL C# EXPERIMENT - NOT PATCH 2.02 TARGET EVIDENCE.** The status,
> measurements, phases, test counts, and dual-run plan below are a frozen
> 2026-07-22 design receipt derived from BFME2 and a 2.01-era corpus. Preserve
> that measured content, but do not treat it as current implementation status,
> v9.7.7 coverage, or authority. The live gameplay authority is the GDScript
> `RetailSliceSim`; C# remains a non-shipping experiment unless an accepted
> trace-qualified cutover changes [architecture](../docs/ARCHITECTURE.md).
> Current target and evidence are governed by the
> [product scope](../contracts/rotwk-202-v9.7.7-product-scope.json),
> [retail baseline](../contracts/rotwk-202-v9.7.7-baseline.json), and
> [verification contract](../docs/VERIFICATION.md). New work must come from the
> [roadmap](../docs/ROADMAP.md) and
> [work-item ledger](../orchestration/work-items.json).

**Status:** P0 kernel complete + adversarially reviewed; P1 started - death pipeline
(OnDeath interception, IsDying), SlowDeath, Production, GettingBuilt landed with
tests (25 green as of 2026-07-22 late). Owner: engine lane (Fable, hands-on).
**Relation to PLAN.md:** implements Phase D with one approved deviation - instead of a
1:1 mechanical port of the GDScript sim followed by a redesign, the C# engine is built
module-first, and parity with the GDScript slice is proven by dual-running recorded
command logs (Phase D step 4 unchanged).

## Why module-first

The BFME2 1.06 + RotWK 2.01 corpus was measured on 2026-07-22 (report:
`workspace/retail-work/reports/rotwk-201-gap-analysis/`, adversarially re-derived):
the union module vocabulary is 339 distinct types (206 Behavior, 100
LocomotorSet/Locomotor, 18 Draw, 10 Body, 5 ClientUpdate - byte-exact per
`workspace/retail-work/reports/requirement-graph/`), of which only 6 are
RotWK-only. SAGE objects are
bags of data-configured modules; implementing the vocabulary generically makes every
faction, RotWK, and data-only mods (Edain-class) consumers of the same machinery.
Draw/client modules stay on the Godot side - this engine implements simulation
carriers only (Behavior, Body, Locomotor semantics, AI update).

## Hard rules

1. **No floats in simulation state or math.** All quantities are `Fixed64` (Q32.32)
   or integers. Cross-platform lockstep determinism is a construction guarantee,
   not a test outcome.
2. **No System.Random, no wall clock, no iteration over unordered containers.**
   Randomness is `DeterministicRandom` (PCG32) seeded per match and part of state.
3. **Zero engine/UI dependencies.** The library references nothing beyond the BCL.
   Headless by default; `dotnet test` is the primary dev loop.
4. **Single mutation path.** All external input enters as `SimCommand`
   (`tick, team, seq, type, args`) applied at tick start ordered by `(team, seq)` -
   identical contract to the GDScript lockstep layer, so netcode/replays carry over.
5. **Fail-closed module loading.** Instantiating a template whose module type has no
   registered implementation records an explicit gap (queryable, testable);
   it never silently skips.
6. **Canonical state hash + snapshot.** SHA-256 over a canonical binary writer;
   `Snapshot()/Restore()` round-trips bit-exactly. Same observability contract the
   GDScript sim gained for multiplayer.

## Phases

- **P0 (this commit):** Fixed64 + FixedVector2, PCG32, ids/teams, command queue,
  tick loop, module registry/lifecycle, canonical hash, snapshot/restore, and the
  determinism proof suite (twin-run 3000-tick hash equality, hash sensitivity,
  snapshot round-trip, command-order invariance, cross-run RNG stability).
- **P1:** top sim-side modules by measured object count - ActiveBody (1,771 objects),
  SlowDeathBehavior (1,194), StructureCollapseUpdate (656), ProductionUpdate (385),
  GettingBuiltBehavior (350), StructureBody (365), TerrainResourceBehavior (152),
  DestroyDie (385) - plus template loading from compiled pack manifests, and a
  scripted-scenario harness.
- **P2:** locomotors + pathing grid, weapons/armor/damage types, HordeContain,
  upgrades, spellbook/powers.
- **P3:** dual-run oracle - replay recorded GDScript-slice command logs in both sims,
  compare semantic traces (not hashes; representations differ), close divergences,
  cut over per PLAN Phase D, then 30 Hz and 8-player scale work.

## Layout

- `engine/OpenBfme.Sim/` - the library (this design).
- `engine/OpenBfme.Sim.Tests/` - xunit suite; determinism gates live here.
- `engine/OpenBfme.Stage1/` - **removed** (legacy proof-stage); history retains it if needed.
