# Contracts

Versioned schemas at the seams that let lanes run in parallel. Producer and
consumer both test against the golden fixture in `fixtures/`. Changing a
schema is its own unit and bumps the version in the file name.

| Schema | Producer | Consumer | Status |
|---|---|---|---|
| `snapshot-v1.schema.json` | sim (engine/, placeholder) | presentation (game/src/view) | draft v1 |
| `match-launch-v1.schema.json` | shell (game/src/ui) | sim | draft v1 |
| `bundle-v1.schema.json` | importer cook | sim + presentation | draft v1 |
| `command-v1.schema.json` | shell/net | sim | draft v1 |
| `replay-v1.schema.json` | sim host | sim host + match shell | draft v1 |
| `lockstep-wire-v1.schema.json` | native match peers | native match peers | draft v1 |

`fixtures/bundle-v1.json` is the complete generic-cook oracle produced from
the fixture trees under `importer/tests/fixtures/cook/`; it includes Object
templates plus Weapon, Armor, DamageFX, Locomotor, LocomotorSet, horde
formation, Upgrade, Science, SpecialPower, CommandButton, and CommandSet
tables. Every named-definition table retains all authored assignments as typed
fields and promotes repeated keys to ordered lists. CommandSet rows retain
authored slot order. `fixtures/bundle-v1-objects.json` is the focused
Object-family producer oracle retained for `cook.objects`. Both are canonical
JSON. The complete fixture is the schema-validation fixture; the Object-only
fixture deliberately exercises that producer before the remaining tables are
composed.

Archive identity JSON (`*-archives.json`, `*-baseline.json`,
`*-overlay.json`) pins which retail files are the source. They are data, not
governance, and the importer and launcher read them.

## Snapshot v1

A snapshot is what the sim publishes once per logic tick for presentation
to draw. It is packed arrays indexed by object slot, never nodes or nested
objects, so the renderer can upload it as-is and interpolate between two
of them. The sim never reads anything back.

The interactive host also accepts `{"op":"step","ticks":K,"format":"packed"}`.
This does not replace snapshot-v1: the reply carries the same snapshot envelope,
but `objects` is a base64 blob with format
`openbfme.snapshot.objects.packed.v1`. `data` is Brotli-compressed then base64
encoded; `uncompressed_bytes` bounds decoding. The decoded blob is
column-major, little-endian, with four bytes per value. Its columns are, in order:

1. int32: `id`, `template`, `owner`, `state`, `anim`, `flags`
2. float32: `x`, `y`, `z`, `yaw`, `health`, `max_health`, `anim_frame`

Each column contains exactly `object_count` values. `hordes`, `players`, and
`events` remain JSON because they are small and variable-width. The default
step format remains the full snapshot-v1 JSON contract for tools and tests.
The first packed reply and any reply after slot identity changes has `full:true`.
Otherwise `full:false`, `base_tick`, and `slots` identify changed object slots;
the same thirteen columns then contain one value per listed slot. Consumers
apply a delta only when `base_tick` equals their previous snapshot tick.

## Match launch v1

What the shell hands the sim to start a match: players with seats, teams,
factions, colors, AI difficulty; map identity; rules (tick rate, command
points, starting resources, fog, game speed); pack identity; seed. Save and
replay files embed one.

## Command v1

One seat's command bundle for one simulation tick. Each bundle carries a
monotonic seat sequence and an ordered list of typed commands; the sim resolves
seat ownership through match-launch and merges bundles by `(team, seq)` before
the tick begins. Coordinates remain retail world units on the wire and are
converted directly to deterministic fixed-point values by the sim.

## Lockstep wire v1

Native-core peers use a host-relayed star with seat 0 as host. Each peer runs
its own sidecar and exchanges only newline-delimited JSON over reliable ENet
channels. The implementation uses `ENetMultiplayerPeer` through the raw
`MultiplayerPeer` packet API (no scene RPC), with control, bundle, and hash
traffic on channels 0, 1, and 2. The line terminator is part of the framing
contract even though one ENet packet contains exactly one line.

The handshake pins the byte-canonical match launch, bundle
`effective_tree_sha256`, map identity, and input delay. For tick T, a peer may
advance only after it has one bundle from every seat. Empty bundles are valid
on this wire and are not submitted to the sidecar. Non-empty bundles are
submitted in `(resolved team asc, seq asc, seat asc)` order. Seat 0 relays each
guest bundle and hash to all other guests. Rejoin transfers the host's `save`
state plus its still-buffered future bundles; the guest restores through the
sidecar `join` operation before resuming.
