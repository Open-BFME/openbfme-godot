# Generated map definitions

The skirmish map browser generates missing feature definitions in an isolated headless worker process, prioritizing the map selected when generation starts. The menu polls completed results at most every 300 ms; map parsing and template loading never run in its frame loop. It uses a separate object-template world, reads the map and its map.ini/solo.ini overrides, and never starts a match or builds preview geometry for this scan.

Definitions are JSON under `user://map-definitions/v<schema>-<content signature>/`. The content signature covers the mounted gameplay archives' verified hashes and order. Definitions from another schema or content set are regenerated. Files are written through a temporary file; failed scans are displayed and are not cached as successful definitions.

Each definition records:

- Capturable structures: template, category, original owner, and map-space x/y/z.
- Counts for outposts, inns, signal fires, and other capturable structures.
- Capture flags separately, so flags do not double-count their associated buildings.
- Fortress start names and coordinates, plus `is_fortress`.
- Scan errors and unresolved object templates.

Capturable structures use the template's `STRUCTURE` and `CAPTURABLE` or `LINKED_TO_FLAG` flags. `CAPTUREFLAG` objects are recorded separately. Category names are descriptive hints from the template name; an unfamiliar mod building remains recorded under `other`. Fortress detection uses `Player_N_Start` waypoints with `waypointType = 5`, the same map flag that suppresses a generated starting base in the game. It does not rely on map names.

The browser offers fortress/standard layout and outpost/inn/signal-fire filters. Unscanned or failed definitions do not count as standard maps. Closing the browser terminates the worker without waiting for a map parse; reopening reuses saved definitions and queues missing entries.

Validation: `godot/tests/skirmish_map_definitions_test.gd` checks Buckland and Minas Tirith, their capturable counts, fortress classification, and cache reuse. `godot/tests/skirmish_map_browser_test.gd` checks each feature filter.
