# OpenBFME deterministic simulation protocol

> **Owner:** Simulation and networking integration owner
> **Owns:** Authoritative state, numeric rules, ticks, commands, ordering, RNG, digests, checkpoints, replay, reconnect, and lockstep contracts.
> **Does not own:** Godot presentation, local input bindings, content conversion, visual parity, matchmaking, or mod authoring UX.
> **Update trigger:** An authoritative data type, command, cadence, serialization, hashing, recovery, or compatibility rule changes.
> **Validation:** Language-independent command/state traces, cross-process replay digests, and deterministic network recovery scenarios defined in `VERIFICATION.md`.

The exact product target is RotWK Patch 2.02 v9.7.7. Authority is the
[product scope](../contracts/rotwk-202-v9.7.7-product-scope.json),
[retail baseline](../contracts/rotwk-202-v9.7.7-baseline.json),
[architecture](ARCHITECTURE.md), and [verification](VERIFICATION.md). Protocol
work is sequenced in [ROADMAP.md](ROADMAP.md) and owned only by
[work-items.json](../orchestration/work-items.json).

## Implementation status

The current executable gameplay authority is the GDScript
`game/src/retail_slice/retail_slice_sim.gd` (`RetailSliceSim`) constructed by
the live Godot path documented in `ARCHITECTURE.md`. The standalone
`engine/OpenBfme.Sim` C# project is a non-shipping experiment. Its unit tests do
not establish Godot integration, authority, or Patch 2.02 parity.

Earlier Men/Fords measurements, 10 Hz slice observations, Stage proof hashes,
and dual-run plans are historical 2.01-era regression evidence. They have not
been retargeted to v9.7.7 and must not support current cadence, behavior,
networking, or completion claims. Current `state_signature()`-style sentinels
also are not a canonical versioned network digest unless a work item proves the
serialization boundary and excludes all client-local concepts.

The command, serialization, lockstep, digest, checkpoint, replay, reconnect,
and cadence sections below describe a target protocol design. A type, interval,
or algorithm written here is not implemented or accepted merely because it is
documented. Each requires an owned work item and the evidence in
`VERIFICATION.md`.

Preserve the current authoritative behavior and cadence while extracting a
language-independent command/state/event trace. A future C# cutover is allowed
only by an explicit architecture decision and exact trace equivalence; cadence
and network changes remain separate reviewed changes.

## Authority boundary

There is exactly one gameplay authority per accepted build: currently
`RetailSliceSim`. Any future `OpenBFME.Sim` authority must remain a pure .NET
library with no Godot, renderer, audio, OpenSAGE, BIG, W3D, Blender,
operating-system input, wall-clock, filesystem enumeration, or transport types.
A future `MatchHost` would own local, listen-server, and headless dedicated
execution through the same simulation contract.

Godot may collect input, maintain selection/control groups/camera/UI, predict immediate
order feedback, interpolate snapshots and consume `SimEvent`s. It must not mutate game
truth. Render cadence and presentation LOD never affect authoritative results.

## Core identities and values

Protocol-facing types are explicit and cannot be conflated:

- `PlayerId`: command ownership and slot identity;
- `TeamId`: alliance, victory and hostility identity;
- `EntityId`: stable authoritative entity identity;
- `HordeId`: stable battalion identity where distinct from an entity;
- `AbilityId`: stable content-defined ability identity;
- `SimTick`: monotonically increasing authoritative tick;
- `MatchSeed`: immutable match seed; and
- `CommandSequence`: server-assigned total ordering within a match.

Resources, health, command points, charges and timers use integers. Coordinates use
signed 64-bit fixed point with 1/1024 BFME-unit precision. Angles and movement
remainders are integer-defined. Overflow behavior, rounding direction and coordinate
conversion are part of the protocol and must be tested at boundaries.

At most eight player slots participate in a match. `PlayerId` and `TeamId` remain
separate even in two-player scenarios.

## Determinism rules

- All authoritative iteration has a declared stable order, normally by stable ID.
- Dictionaries, sets, filesystem order, locale and platform hash codes never choose an
  authoritative result.
- Pathfinding and target-selection ties have explicit integer tie-breakers.
- Every random decision uses an explicit deterministic stream derived from match seed,
  subsystem and stable entity identity.
- No floating-point physics query or GPU result enters authoritative state.
- Hordes own global paths and deterministic formation slots; members own real health,
  attacks, experience, status, death and authoritative events.
- Commands are validated against the canonical state at their execution tick.
- Invalid or late commands are rejected as explicit results and do not partially
  mutate state.

## Commands

`GameCommand` contains at least:

```text
protocol version
match id
issuing PlayerId
client command id
requested execution tick
command kind
ordered target entity IDs, if applicable
integer target position/angle, if applicable
content IDs and bounded command-specific payload
```

Required command kinds are move, attack, attack-move, stop, stance, construct,
cancel, train, upgrade, power, hero purchase, hero revive, mount state and surrender.
Selection and control-group changes are never `GameCommand`s.

The server validates ownership, visibility where required, content compatibility,
resource cost, cooldown, target shape, payload bounds and allowed execution window. It
assigns `CommandSequence` and publishes an `AcceptedCommandBatch`. Every peer applies
the same accepted batches in the same order.

## Target scheduling proposal (unimplemented)

- Simulation cadence: 30 Hz.
- Local single-player command target: next tick.
- Online buffer: two ticks by default, adapting only within two to six ticks.
- The server never stalls the match indefinitely for a slow client.
- Commands arriving outside the accepted window are rejected explicitly.
- Cursor response, order marker, UI acknowledgement and acknowledgement audio may occur
  in the same rendered frame but cannot change authoritative state.
- AI and fog may run on deterministic sub-schedules; their exact divisors are protocol
  configuration, not wall-clock timers.

## Target public interfaces (unimplemented)

The proposed production boundary exposes these versioned concepts:

- `MatchConfig`: protocol, simulation pack digest, map, seed, slots, teams, options,
  cadence and deterministic subsystem schedules.
- `GameCommand`: bounded player intent.
- `AcceptedCommandBatch`: server sequence plus accepted/rejected command results for a
  tick range.
- `SimSnapshot`: immutable presentation-facing state at an authoritative tick.
- `StateDigest`: canonical protocol-versioned digest of authoritative state.
- `SimEvent`: ordered gameplay events for presentation and diagnostics.
- `Checkpoint`: versioned complete authoritative restore state.
- `ReplayHeader`: engine/protocol/content identities, configuration and seed.
- `PlayerView`: visibility-filtered view where appropriate.
- `MatchHost`: canonical command sequencing, simulation advance, digest, checkpoint,
  replay and recovery coordinator.

Serialization is canonical and independently specified. Field order, integer encoding,
collection ordering, version negotiation and rejection behavior must not depend on a
runtime's default JSON, reflection or dictionary behavior.

## Target digest, checkpoint and replay cadence (unimplemented)

- Emit a `StateDigest` every 30 ticks.
- Emit a `Checkpoint` every 300 ticks.
- The server retains at least the latest six checkpoints.
- A replay records the header, accepted command stream, digests and checkpoints.
- Replaying without presentation must reproduce every recorded digest.

The digest excludes client-local selection, control groups, camera, cursor, UI,
presentation interpolation, sound/particle state and network timing. It includes all
data that can affect future authoritative results, including RNG stream positions,
pending commands, deterministic schedules and content/configuration identities.

## Reconnect, observers and desync recovery

A reconnecting client or joining observer receives a compatible checkpoint followed by
the ordered accepted-command tail. It validates the restored digest before presenting
live state.

On a digest mismatch, the client reports the tick and both digests, restores from a
server checkpoint, applies the command tail and revalidates. A peer is disconnected
after three resynchronizations within five minutes. Diagnostics contain protocol and
digest metadata, never retail payloads.

Normal traffic carries commands, digests and recovery data rather than per-unit
transform snapshots. Checkpoint transfer is bounded and validated before allocation.

## Compatibility handshake

Before joining, peers agree on:

- engine and simulation-protocol versions;
- canonical simulation-pack digest and dependency order;
- map simulation digest;
- deterministic `MatchConfig`; and
- enabled authoritative code-plugin identities, if the server permits them.

Presentation-pack differences are allowed only when they cannot affect simulation.
Transport authentication, rate limits and command validation are required; the initial
self-hosted transport does not imply ranked trust, account identity or transport
confidentiality.

## Completion evidence

This protocol is implemented only when the exact Patch 2.02 v9.7.7 identity is
bound and local, listen, and dedicated hosts replay the same scenarios to
identical digests across supported platforms and render rates, while latency,
jitter, loss, reordering, reconnect, observer join, and injected-desync tests
pass. Historical Men/Fords traces, types, or compiling stubs do not satisfy
that result.
