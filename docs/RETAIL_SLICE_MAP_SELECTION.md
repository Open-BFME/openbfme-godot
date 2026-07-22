# Retail slice map selection

Owner: retail slice runtime (`game/src/retail_slice`).
Audience: the menu/integration step that exposes skirmish map choice.

## Selectable maps

The slice resolves one of these ids (`bfme2.map.<slug>`), fail-closed on
anything else:

| Map id | Display name | Player starts | Source |
| --- | --- | --- | --- |
| `bfme2.map.fords-of-isen-ii` | Fords of Isen II | 2 | selected faction pack `files.entryMap` |
| `bfme2.map.rivendell` | Rivendell | 3 | five-maps pack catalog |
| `bfme2.map.mount-doom` | Mount Doom | 4 | five-maps pack catalog |
| `bfme2.map.dagorlad` | Dagorlad | 6 | five-maps pack catalog |
| `bfme2.map.mordor` | Mordor | 8 | five-maps pack catalog |

Display names come from each cooked `maps/<slug>/map.json` (`displayName`),
mirrored in the pack catalog `data/maps.json`. The slice always seeds a
two-player match from `Player_1_Start`/`Player_2_Start`; extra authored starts
are retained as source data only.

## Selection mechanism

Resolution order in `retail_vertical_slice._resolve_slice_map_id()`:

1. `OPENBFME_SLICE_MAP` environment variable (always wins, like
   `OPENBFME_SLICE_FACTION`), e.g.
   `OPENBFME_SLICE_MAP=bfme2.map.rivendell`.
2. `GameState.retail_map_id` (String, written by the menu's skirmish setup;
   `game/src/core/game_state.gd`). Empty means "no menu choice".
3. Default: `bfme2.map.fords-of-isen-ii` (the historical slice map,
   `RetailVerticalSlice.MAP_ID`).

Anything that is not a well-formed `bfme2.map.<slug>` id fails closed, as does
an id that cannot be resolved to a cooked map document. The resolved id and
pack root are exposed on the slice as `map_id` and `map_pack_root`.

Definition resolution in `retail_vertical_slice._resolve_slice_map_definition()`:

- The default Fords map is always resolved from the selected faction pack's
  `pack.json` `files.entryMap`. This keeps the Fords boot byte-exact even when
  a supplemental pack registers the same map id (the five-maps pack also
  cooks Fords; ContentDB merge order would otherwise hand the slice the
  roads-less five-maps variant).
- Any other id is taken from `ContentDB.get_bundle_map(id)` first (the
  registered-content path), then from the five-maps pack catalog directly
  (verification path while the pack is unregistered).

## Content registration (integration step)

Add the five-maps pack to `supplementalPacks` in
`.private/content-packs/selection.json` (do not commit retail bytes):

```json
{
  "activePack": "bfme2-men-vslice/<leaf-hash>",
  "schema": "openbfme.pack-selection",
  "schemaVersion": 0,
  "supplementalPacks": [
    "bfme2-five-maps-106-private"
  ]
}
```

The pack directory `bfme2-five-maps-106-private/` holds `pack.json` directly
(no hash-leaf layer), so the plain pack id is the entry. Its `pack.json`
declares `files.mapCatalog = data/maps.json`, so registration surfaces all
five maps through `ContentDB.get_bundle_map()` for menu listing.

Note for the menu: after registration, `ContentDB.get_bundle_map(
"bfme2.map.fords-of-isen-ii")` returns the five-maps variant (pack priority
904 loads after the Men pack's 900 and wins the merge). The slice itself is
immune (see above), but a menu that wants the slice-exact Fords definition
should read the selected faction pack's `files.entryMap` the same way.

## Headless verification

- Fords byte gate (245 checks): `run_retail_slice.bat --test`
  (`res://tests/retail_slice_runner.gd`).
- Per-map slice boot (33 checks): set `OPENBFME_SLICE_MAP` and run
  `res://tests/retail_slice_map_runner.gd`.
- Data-layer gate across the four new maps (55 checks, no slice scene):
  `res://tests/retail_map_data_runner.gd`.

## Per-map runtime profiles

`retail_map_data.gd` `MAP_RUNTIME_PROFILES` holds the per-map contract the
cooked documents cannot declare:

- Fords: requires the three authored ford crossings (`ford1`/`ford2`/`ford3`),
  re-reviews one source-impassable cell, and applies the reviewed retail
  crossing rule (cooked water blocks navigation except at ford corridors).
- Other maps: navigate on the source-authored passability grid alone (their
  water renders and rasterizes for diagnostics but does not block). Mordor's
  moat gap and Rivendell's shores are source-passable by design; blocking
  cooked water severed Mordor's two start banks.

Navigation regions extend the declared playable border to cover player starts
authored just outside it (Rivendell's `Player_2_Start`); camera bounds still
follow the declared border.

## Known per-map gaps (pack data, not runtime)

- No cooked roads layer for the four new maps (`roads`/`roadMaterials`
  documents absent from the pack; road wire objects remain in `objects.json`
  as unconverted source facts). Fords' roads load from the Men pack as before.
- All object placements in the four new maps are binding-unresolved (no GLB
  bindings in the pack), so no retail props render; unresolved placements are
  retained as non-rendered diagnostics, same as Fords' unresolved types.
- No per-map environment documents (fog/sky/lights use the global retail
  environment.ini afternoon rig, like Fords).
- No navmesh/routing graphs in the pack (`routingGraphStatus:
  empty-no-authored-navmesh`); navigation is the runtime's bounded A* over the
  source passability grid.
