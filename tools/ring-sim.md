# One Ring simulation lane

The runtime consumes the compiled ring system, playable-unit, structure, and
map-script contracts. Packs built before ring prerequisite compilation remain
safe: an empty `BuildableRingHeroesMP` route receives the two retail ring gates
inside the simulation and emits `stale-pack-ring-prereqs-synthesized`. A
non-empty compiled prerequisite list always wins.

The deterministic fallback gives map scripts their tick-1 evaluation plus a
four-tick settling window. Its exact tick is non-load-bearing: an authored
Gollum created before the fallback suppresses it, and a script creation after
the fallback is absorbed into the existing ring Gollum. Every actual fallback
emits `RING_GOLLUM_FALLBACK`.

## Review fixes

- Ring-hero production now fails closed for stale packs with empty route
  prerequisites and returns `ring-heroes-disabled` when the match rule is off.
- Gollum detection/flee distances and ring pickup/delivery radii are converted
  from source units with the selected map transform scale.
- Ring state is lazily created in the hashed script-surface bag, so unseeded or
  restored matches retain `gollum_spawned` and carrier state.
- Script and fallback Gollum creation deduplicate in both orders; fallback
  diagnostics are emitted for real-map and missing-waypoint spawns alike.
- The simulation itself gates ring spawn waypoints and creep-team id-space
  allocation on the match rule.
- Presentation is rule-gated and reads ring object identity, holding status,
  and carrier offset from the merged runtime contract.
- Runtime-document object ids override system defaults, late-built delivery
  structures receive their delivery contract, and completed upgrade arrays
  retain sorted order.
- Rule-on fieldability and faction-manifest construction expose the mounted
  pack's ring hero through the faction fortress roster.
- The retail gate uses space indentation and pins the complete authoritative
  ring state hash.

All inexpensive review follow-ups were included; none from the review list
remain deferred.

## Validation

Against the selected private content root:

- ring mechanic: `141/0`, hash
  `2650094088785f8e521604f8dec1218c4c5f95fa0d9e8802110a424f3e8aa875`;
- mounted ring-hero manifest: `12/0`;
- CAH match: `72/0`;
- retail lockstep network: `37/0`;
- fortress command surface: Men `50/0`, Dwarves `64/0`;
- retail slice: `373` passed with all `32` named failures pinned and acceptance
  green;
- retail hero ability: `134/0` in three consecutive runs.

The reported tick-300 restore abort did not reproduce: all three hero-ability
runs passed `snapshot_round_trip_mid_everything` and reached their final result
marker. One run outlived the shell wrapper's 120-second capture window but then
completed normally with `134/0`; it emitted no script error or watchdog abort.
