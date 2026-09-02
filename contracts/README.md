# Contracts

Versioned schemas at the seams that let lanes run in parallel. Producer and
consumer both test against the golden fixture in `fixtures/`. Changing a
schema is its own unit and bumps the version in the file name.

| Schema | Producer | Consumer | Status |
|---|---|---|---|
| `snapshot-v1.schema.json` | sim (engine/, placeholder) | presentation (game/src/view) | draft v1 |
| `match-launch-v1.schema.json` | shell (game/src/ui) | sim | draft v1 |
| `bundle-v1.schema.json` | importer cook | sim + presentation | not written |
| `command-v1.schema.json` | shell/net | sim | draft v1 |

Archive identity JSON (`*-archives.json`, `*-baseline.json`,
`*-overlay.json`) pins which retail files are the source. They are data, not
governance, and the importer and launcher read them.

## Snapshot v1

A snapshot is what the sim publishes once per logic tick for presentation
to draw. It is packed arrays indexed by object slot, never nodes or nested
objects, so the renderer can upload it as-is and interpolate between two
of them. The sim never reads anything back.

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
