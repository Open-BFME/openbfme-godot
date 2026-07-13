# OpenBFME Stage 1–2 authoritative simulation

This directory is an isolated, legal-safe C# feasibility implementation. It uses only
the .NET runtime and synthetic primitive data. It does not read BFME2 files, use donor
types, reference Godot, or depend on OpenSAGE, BIG, W3D, NuGet packages, rendering,
physics-query ordering, wall-clock time, or floating-point authoritative values.

## Run

With the .NET 8 SDK or newer:

```powershell
dotnet build engine/OpenBfme.Stage1/OpenBfme.Stage1.csproj
dotnet run --project engine/OpenBfme.Stage1/OpenBfme.Stage1.csproj -- self-test
dotnet run --project engine/OpenBfme.Stage1/OpenBfme.Stage1.csproj -- stage2-self-test
dotnet run --project engine/OpenBfme.Stage1/OpenBfme.Stage1.csproj -- stage2-bundle-test content/openbfme-test
dotnet run --project engine/OpenBfme.Stage1/OpenBfme.Stage1.csproj -- benchmark 300
dotnet run --project engine/OpenBfme.Stage1/OpenBfme.Stage1.csproj -- scenario combat20 360
```

All commands emit one-record-per-line, whitespace-delimited `key=value` output and
return nonzero on invalid input, a failed self-test, invalid state, or a benchmark below
30 authoritative ticks per second. `self-test` is the test-equivalent command because
the project deliberately has no external test-framework package.

## Authoritative model

- The scheduler advances at a declared 30 ticks/second. It never reads elapsed time.
- Positions, path costs, hit points, cooldowns, damage, and movement are integers. One
  path cell is 1,000 subcells. Integer square root is used for movement normalization.
- IDs are monotonically assigned and all ambiguous choices use stable ID order.
- Each horde owns exactly one deterministic grid A* path for its anchor. Members have no
  global path. A* expands north, east, south, west and orders equal candidates by total
  cost, heuristic, Y, X, then insertion sequence.
- Real members retain stable formation slots, integer positions, health, cooldown,
  targets, melee/ranged roles, and death state. Formation separation is pairwise in ID
  order and corrections are capped.
- Melee damage resolves through deterministic events. Ranged attacks create real,
  stable-ID integer projectiles that travel each tick and damage a living target on hit.
- Two fortresses own health and team identity; destroying one sets the winner.
- The canonical replay hash is FNV-1a 32 over ordered grid blockers, fortresses, hordes,
  horde paths, members, and active projectiles.

## Tick-stamped orders

`SimCommand` carries `ExecuteTick`, `Sequence`, `HordeId`, `Kind`, destination, and an
optional target ID. Commands execute in `(tick, sequence, horde ID, kind)` order.

| Order | Behavior |
|---|---|
| `Move` | Replaces the horde anchor path and does not auto-acquire enemies. |
| `Stop` | Clears the path and all attack/engagement targets. |
| `AttackMove` | Follows the path and acquires the nearest eligible enemy with ID ties. |
| `AttackTarget` | Paths toward and focuses a member, horde, or fortress entity ID. |

The legal-safe arena has a static vertical wall and a nine-cell gap. The obstacle test
starts away from the gap, verifies that the path detours through it, and checks that the
anchor and every living member stay on walkable cells. Other self-tests cover 20v20
melee/projectile resolution, exact hash repeatability, fortress victory, command timing,
stable IDs/slots/one-path ownership, runtime independence, invalid-state detection, and
a measured 50-horde/750-member benchmark.

## Stage 2 economy

The optional Stage 2 layer adds integer resources and population, transactional
odd-cell building placement, construction-health ramps, completed-farm income and
efficiency, FIFO train queues, stable building/job IDs, and deterministic production
spawn/rally fallback. Buildings block the authoritative navigation grid immediately.
The layer runs before the existing movement/combat systems each fixed tick and extends
the canonical hash only when enabled, so the Stage 1 replay stream remains unchanged.
