# Local RotWK 2.02 multiplayer maps and visual assets

`tools/import_rotwk_202_maps.py` builds the optional, untracked
`godot/mods/rotwk_202_maps.big` from a locally installed `__patch202.big`:

```sh
python3 tools/import_rotwk_202_maps.py \
  --source '/path/to/RotWK/__patch202.big' \
  --rotwk workspace/aio-install/RotWK \
  --bfme2 workspace/aio-install/BFME2
```

The importer selects multiplayer entries from the patch's map cache, copies the
files in their map directories, and merges those entries into the retail map
cache. Retail campaign entries remain available. Names and descriptions are copied from the patch language archive into each
map's local `map.str`, preserving existing local labels. The importer defaults
to `lang/englishpatch202.big` beside the source; `--strings` selects another
language archive. The global string table remains unchanged.

When this pack exists, the Godot runtime mounts it over both the world and menu
archive views. The normal export includes `mods/*.big`; packaged apps extract
this archive to `user://mods` before mounting it. Remove the source pack and
re-export to return to the retail map list.

The pack also imports all `art/` entries from the patch: W3D models, compiled
textures, and terrain textures, including replacements needed by those models.
The patch terrain INIs supply the corresponding texture definitions.

This imports maps, visual assets and local scripts/INIs, not the global 2.02 balance patch.
Maps requiring 2.02-specific global objects or behavior may need further support.

Validation: `godot/tests/rotwk_202_maps_test.gd` checks archive ownership, the
lobby list, search and description, loads the added crystal-ore model and texture,
and loads Amon Amarth. With the local patch imported on
2026-10-16, the lobby contains 309 maps (303 imported multiplayer entries).

To include the generated winter Buckland variant, add:

```sh
--buckland-snow '/path/to/outputs/snow-buckland/Buckland Snow'
```

This copies the generated `.map`, both previews, and `map.ini` into a separate
`maps/buckland snow/` directory, listed as **Buckland Snow**. Its cache uses
stock Buckland's unchanged four-player bounds and starting positions. Its SAGE
checksum is unknown (zero), rather than incorrectly inheriting the stock CRC.
The smoke test `godot/tests/buckland_snow_test.gd` finds it in the lobby and
starts a skirmish on it.
