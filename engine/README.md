# OpenBfme.Sim - the new core

This C# project is the simulation that replaces the placeholder GDScript sim
in `game/src/retail_slice`. It is the fleet's main target. See
`docs/REORG-PLAN.md` sections 2 and 7, and `DESIGN.md` here for the
module-first architecture it follows.

## Shape

- `OpenBfme.Sim/` kernel: fixed-point math, PCG32 RNG, object ids, command
  queue, tick loop, module registry, canonical hash, snapshot/restore.
- `OpenBfme.Sim/Modules/<Name>.cs` one file per SAGE module type, registered
  by its INI name through a directory-driven registry. Adding a module is
  adding a file.
- `OpenBfme.Sim.Tests/` xunit: determinism gates, per-module golden traces,
  dual-run against placeholder traces.

## Rules

- No floats in state. `Fixed64` and integers only.
- No `System.Random`, no wall clock, no unordered iteration.
- Zero engine dependencies. `dotnet test` is the dev loop.
- One mutation path: `SimCommand` ordered by `(team, seq)` at tick start.
- Unknown module type: structural kinds are a load error, cosmetic kinds
  record a gap. Both are queryable.
- Publishes `contracts/snapshot-v1.schema.json`; starts from
  `contracts/match-launch-v1.schema.json`.

## Object state transition

`ObjectStore` owns the renderer-facing structure-of-arrays: identity,
template/owner indexes, transforms, health, model state, animation state, and
flags. During the compatibility transition, `GameObject` still owns template
names, module instances and their serialized state, lifecycle flags, and the
legacy module-facing transform API. `SimWorld` deterministically mirrors those
legacy values into `ObjectStore`; new presentation code reads the store only.

## Lanes

- **Kernel** is one assigned lane until the skeleton is complete: objects as
  structure-of-arrays, 30 Hz default tick from `rules.tick_ms`, flow-field
  pathing, weapon and armor resolution, horde membership, snapshot writer.
- **`core-modules` queue** opens when the kernel lane declares the module
  interface stable. `python tools/fleet/work.py next` serves module types
  ranked by retail object count.

## Reference

Engine semantics: `workspace/reference/open-bfme-1` (BFME1 decompile and
Ghidra pseudo-C). INI is the balance authority.
