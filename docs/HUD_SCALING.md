# In-game HUD widescreen scaling

BFME2/RotWK's APT stage is still mapped to the viewport using the existing
independent X/Y scale factors (the donor-derived mapping tracked by S-130).
Previously that mapping also widened the Palantir, minimap, portraits and
right-hand construction buttons on widescreen displays.

`AptPalantir::setViewportSize` applies a presentation-only horizontal
correction to the HUD's top-level clips when the viewport is wider than the
authored stage. Its factor is `min(1, scaleY / scaleX)`. Combined with the
stage mapping, HUD artwork then uses the height-derived scale in both axes.
The Palantir stays at the bottom left, the construction bar stays at the
right edge, and the planning-mode indicator stays horizontally centred.
The dynamically positioned help box and button-flash stage retain their
existing mappings. BFME1, front-end menus, 4:3 and narrower viewports are
unchanged.

The separately loaded Powers purchase screen (`SpellStore.apt`) uses the
same correction on its movie root, anchored at the stage's horizontal
centre. Its tree, icons, text, help and purchase controls retain their
proportions together, including when opened on an already-wide viewport
or resized while open. The banner and translucent shade in `Title.instance1`
cancel that correction in their parent's coordinate space, retaining the
full-width stage mapping and authored animation/colour. The title plate,
text and `SpellStoreBackground` View3D globe stay proportional. BFME1 keeps
its existing mapping.

This is a widescreen presentation correction, not a newly verified retail
transform rule. S-130's target-code uncertainty remains open.

`AptCharacterInst::viewportTransform` is separate from the authored and
script-controlled `matrix`, so it neither accumulates across resizes nor
overrides timeline animation. Render-list generation and global transforms
use it together; button hit meshes, native image/radar rectangles and input
therefore follow the same geometry. Radar-ping positions are converted back
through the radar overlay's correction before being passed to the movie.

Regression coverage:

- `input: viewport transforms*`: rendering, hit meshes and script placement.
- `hud2 palantir: widescreen*`: installed RotWK panels at 4:3, 16:9 and 21:9,
  edge anchoring, resizing and both panels' input hit regions.
- `spell store widescreen:*`: Powers movie proportions, centring, rendered
  native-image transforms, resizing and reopening, including 7680x4320 and
  3440x1440; rendered banner/shade geometry at both edges and authored closing fades.
- `play1 spell store by the mouse:*`: all seven factions at 4:3, 16:9 and
  21:9; full-width backdrop coverage, opening, buying, Reset and Accept
  through actual mouse input.
