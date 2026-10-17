# Batch map metadata and previews

`export_maps.py` exports maps from the same mounted, localized skirmish catalog
used by the in-game map browser. It does not parse an independent approximation
of MapCache.ini. Mount your normal retail installation as described in the project
setup, build the GDExtension, and use the project's compatible Godot version
(currently 4.7). No retail assets are included in this tool.

From the repository root:

```sh
python tools/maps/export_maps.py --godot /path/to/godot --out /path/to/map-export --list
python tools/maps/export_maps.py --godot /path/to/godot --out /path/to/map-export --all
python tools/maps/export_maps.py --godot /path/to/godot --out /path/to/map-export --map 'exact key from --list' --map 'another key'
python tools/maps/export_maps.py --godot /path/to/godot --out /path/to/map-export --maps-file selection.json
```

`selection.json` is a JSON array of exact catalog keys. Maps must already be
mounted and present in the game's skirmish catalog, including custom maps.
Use a directory outside the checkout for exports. `GODOT` can supply the executable
instead of `--godot`. A private temporary skirmish profile isolates catalog setup
from the player's saved lobby settings.

The output contains:

- `catalog.json`: all available keys and localized names/descriptions, player
  counts, official/custom flags, and start positions.
- `maps.json`: versioned aggregate metadata for the selected maps, including
  mounted content signature, terrain dimensions/elevations, texture/object/road
  counts, capturables and fortress-map detection, plus errors and preview status.
- `<id>.json`: each map's metadata. IDs hash the full catalog key.
- `<id>.png`: a rendered overview of the playable terrain, textures, water, roads,
  and placed map objects. No lobby faction-preview fortresses are fabricated.
- `index.html`: an escaped, static browser gallery. Open it directly in a browser;
  it works offline without a server or external JavaScript dependencies.

Start coordinates use SAGE map coordinates; terrain camera vectors use Godot
coordinates (SAGE x/y maps to Godot x/-z). `starts[].index` is zero-based, matching
in-game lobby assignments. Metadata uses the active game's localization.

## Rendering and limitations

This implementation uses the requested screenshot fallback. It does **not** claim
to produce a merged 3D model: terrain relies on layered atlas shader blending,
and W3D objects use GPU bone-palette transforms. Direct Godot glTF export would
lose those poses/materials. A faithful GLB exporter needs a separate mesh and
material baking implementation. The reason is also recorded in preview metadata.

Screenshots require a rendering display and a working GPU/graphics driver; Linux
servers can use a configured virtual display/software Vulkan renderer. For
metadata only, use `--metadata-only`, which launches Godot headlessly and skips
all preview meshes. Options `--width`/`--height` default to 1600×1000; the camera
fits the map for either aspect ratio. Screenshots are static previews with fog
and animation disabled, using the map's lighting, as in the map browser.

Each map runs in a fresh process, with a configurable `--timeout` (300 seconds
by default). Failures are recorded per map, remaining maps continue, and the
command returns nonzero if any metadata or requested preview is incomplete.
Reruns overwrite selected maps and rebuild the gallery; stale screenshots for
those maps are removed. Other old export files are not deleted. For a clean
collection, use a new output directory. Keep generated assets local unless you
have the rights to distribute the underlying retail content.

Driver validation (no game data required):

```sh
python -m unittest discover -s tools/maps -p 'test_export_maps.py'
```
