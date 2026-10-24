# Shared battalion path retry

Extracted from BFME1 gameplay PR #109. This is an inferred OpenBFME correction,
not a verified retail algorithm. It runs only under the Uber simulation profile
(`Pathfinder::uberLimits()`), for battalions in BFME1, BFME2 and RotWK data. The
Faithful profile keeps RotWK 2.01's `findPath` (RW 0x6FE7CB), which returns the
corridor search's result with no second search.

The coarse corridor can leave a battalion at a partial endpoint behind a castle
wall even when an open gate provides a longer route. If the hierarchical search
fails or returns a partial path, retry once with the existing cell search without
corridor ordering. Each search retains its configured cell limit. Replace the
original result only if the retry completes; otherwise retain the original
partial route. A closed barrier remains impassable, and goal step-back retains
its partial-result status. Individual units keep their existing search behavior.
The shared pathfinder uses the existing `PK_HORDE` kind without consulting the game data.

`test_pathperf1_hier.cpp` runs a longer route around an open-ended wall under
both profiles (Faithful keeps the partial route, cell count and pathfinder hash
of a build without the retry; Uber arrives), preserves individual-unit behavior,
and keeps Uber paths on the starting side of a sealed wall.
`godot/tests/bfme1_castle_path_test.gd` issues one move behind Gondor's castle on
Fords of Isen and Rohan's castle on Anorien in an Uber game and checks the battalion and soldiers:

```sh
BFME1_INSTALL=/path/to/BFME1 godot --headless --path godot --script res://tests/bfme1_castle_path_test.gd
```

The installed-asset test also requires working BFME1 opening armies; that setup
is implemented in #109 and is not included in this standalone pathfinding fix.
No retail game data is included.
