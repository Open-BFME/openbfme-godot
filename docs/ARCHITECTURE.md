Owner: Integration owner
Owns: Component boundaries, authoritative state ownership, stable runtime data flow, and accepted architecture decisions.
Does not own: Current progress, milestone acceptance, retail completeness counts, or task scheduling.
Last verified commit: `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
Update trigger: An accepted architecture decision changes a boundary, authority, protocol, or pack contract.
Validation: Review boundary tests and architecture-decision evidence against the current implementation; volatile results belong in `STATUS.md`.

# OpenBFME architecture

## System boundary

```text
User-owned BFME2 installation
        -> Python importer and pinned converters
        -> private simulation and presentation packs
        -> authoritative deterministic simulation / MatchHost
        -> Godot presentation, input, audio, and local UI state
```

Only the importer may understand BFME2 source formats. Runtime code consumes versioned OpenBFME pack schemas. Retail payloads stay under `.private` and are never committed, logged, exported, or transferred between multiplayer peers.

## State ownership

| Concern | Authority | Contract |
|---|---|---|
| Retail extraction, conversion, provenance | Python importer | Deterministic private pack plus source/tool identities |
| Game truth | Pure C# simulation | Integer/fixed deterministic state, stable iteration, explicit RNG |
| Match sequencing | `MatchHost` | Accepted commands, digests, checkpoints, replay tail |
| Rendering and audiovisual presentation | Godot | Read-only projection of simulation state and events |
| Input, camera, selection, control groups, UI | Godot client | Local state; excluded from authoritative hashes |
| Gameplay compatibility | Simulation-pack digest | Identical on server and participating clients |
| Visual/audio replacement | Presentation-pack digest | May differ without changing simulation |

The simulation must not contain Godot, OpenSAGE, BIG, W3D, Blender, camera, selection, or UI types. The importer must not become a second gameplay authority. Godot must not silently invent parity behavior when authoritative data is missing.

## Implemented

- The current product has a Godot retail-slice runtime and a Python retail importer/conversion pipeline.
- Retail and converted retail data use private workspace roots.
- A gameplay implementation exists for the current Men/Fords slice, with focused validation surfaces.

Implemented does not mean final architecture: the current simulator remains a migration source until the C# cutover is proven.

## Decided

- Rise of the Witch-king 2.01 is the compatibility target. BFME2 1.06 is the
  base game underneath it and the source of evidence for everything 2.01 does
  not change.
- Maximum match size is eight players.
- Production simulation cadence is 30 Hz; rendering cadence is independent.
- Networking is server-refereed deterministic lockstep. Commands are normal traffic; snapshots are recovery checkpoints, not the primary synchronization model.
- The server sequences valid commands and does not stall indefinitely for a late client.
- A single `MatchHost` contract serves local, listen-server, and headless dedicated-server execution.
- The current simulation is mechanically ported before cadence changes or redesign.
- Private parity loads only the selected strict retail pack and fails closed. Synthetic definitions cannot fill missing retail data.
- Simulation and presentation packs are independently versioned and hashed.

## Unimplemented or unproven

- Pure C# as the sole authoritative simulator.
- The verified 10 Hz behavior port followed by a separately reviewed 30 Hz conversion.
- Canonical command serialization, state digests, checkpoints, replay, reconnect, and observer join.
- Qualified two-to-eight-player lockstep under latency, loss, jitter, reordering, and maximum legal load.
- Complete retail feature/evidence coverage and full BFME2 game-mode parity.
- Stable public mod schemas, dedicated-server packaging, and independent cross-platform replay agreement.

No item in this section may be described as implemented based on a type stub, parsed source field, converted asset, compiler success, or plan entry alone.

## Determinism rules

- Use separate `PlayerId` and `TeamId` values and stable entity/horde/ability/tick identifiers.
- Use integer resources, health, timers, angles, and deterministic fixed-precision coordinates.
- Seed explicit RNG streams from stable match, subsystem, and entity inputs.
- Define stable collection order and deterministic pathfinding tie-breaks.
- Hash only authoritative simulation state.
- Preserve seconds-based behavior with explicit rational tick conversion when moving to 30 Hz.

Detailed command, numeric, replay, and network contracts belong in `docs/SIMULATION_PROTOCOL.md`.
