# BFME2 visual-oracle notes

## Source priority

1. A locally launched BFME2 1.06 match is the authoritative visual oracle.
2. Extracted BFME2 1.06 APT/WND/INI/W3D/map data is the authoritative asset and
   layout source.
3. The user-provided [gameplay video](https://www.youtube.com/watch?v=z6ZI6wY_LYE)
   is a secondary motion/composition reference. It is Rise of the Witch-king,
   not base-game BFME2 1.06, so edition-specific art or rules must not silently
   override the retail 1.06 data.

## Observed frames

- `00:14`: empty command grid, circular Palantir/minimap at bottom left, vertical
  right-edge command strip, hero portrait near the lower center, translucent
  build radius, terrain decals, and strong warm directional shadows.
- `06:51`: populated base composition. The control bar is an irregular authored
  composite rather than a rectangular panel; resource and command-point counters
  sit in the lower Palantir frame; expansion/build plots use authored circular
  ground markers; buildings cast long directional shadows.
- `12:20`: late-base composition confirms the same HUD shell and placement-marker
  language at higher command-point count. Terrain uses several blended grass,
  dirt, rock, path, and decal layers rather than a single ground texture.

## Conversion consequences

- `SGCommandBar` must not be used as a guessed monolithic image. The shipped HUD
  is assembled from Palantir APT movies, RU geometry, TGA atlases, and
  `window/controlbar.wnd`.
- Private parity mode must render the exact composite or fail closed. A generic
  `Panel`, circular placeholder, or synthetic command grid is not parity.
- The map renderer needs authored terrain layers, decals, props, water, fog/sky,
  and directional lighting together; correct geometry alone will not match.
- Expansion/build-plot markers are part of the retail presentation and should be
  driven by exact build-plot/gameplay state, not emitted as always-on debug rings.
- Camera framing, minimap scale, HUD anchoring, and shadow direction belong in
  rendered comparison tests after the selected pack is rebuilt.

The Godot half of that comparison is captured by
`game/tests/retail_render_capture_runner.gd`. It refuses headless rendering and
retail-derived pixels outside `.private/scratch/rendered-qa`, waits for the
composed private slice to pass its runtime readiness gate, captures a fixed
1920x1080 Forward+ frame, and reports whether the exact linear-fog compositor
actually dispatched. A captured frame is evidence for comparison, not an
automatic parity claim.
