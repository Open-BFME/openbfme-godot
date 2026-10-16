# Map wall-hub markers

`godot/data/wall_hub_markers.lua` contains stock BFME2 and RotWK placements. Each source map records its game, archive, map path, grid dimensions, and every marker's SAGE x/y/z, angle in radians, object flags, template, and complete property dictionary. This includes `GenericAIObjectID`, `GenericAIObjectName`, and `GenericAIObjectWallHubNumber` for the ordered hub groups.

Regenerate from local retail installations and the imported 2.02 map pack:

```sh
python3 tools/extract_wall_hub_markers.py \
  --bfme2 workspace/aio-install/BFME2 \
  --rotwk workspace/aio-install/RotWK \
  --maps-202 godot/mods/rotwk_202_maps.big
```

The generated `equivalents` table maps exact 2.02 map paths to a stock source only when their grid dimensions and border match and the 2.02 map has no native hubs. RotWK stock takes precedence over BFME2 stock; both source records are preserved. The current pack restores Buckland, Minhiriath, and Rohan. Renamed variants are not assumed to share a layout.

The engine loads the Lua data in an isolated interpreter. During a match, `bfme.map.getWallHubSites()` returns the fallback records for that map, including their property dictionaries. Options → Interface → Show farm and wall-hub map markers renders these as blue rings/crosses. Native wall-hub placements take precedence. Ground height is sampled from the active map; gameplay objects and pathfinding are unaffected.
