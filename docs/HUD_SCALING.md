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

## Movies a mod authored for 16:9

Edain 4.8.5.4 (`___edain_apt_widescreen.big`: Palantir, StrategicPalantir,
SpellStore) and Patch 2.02 9.8.6 (`__patch202.big`: Palantir, SpellStore)
squeeze their HUD clips to 3/4 of their width, so that retail's stretched
stage shows them in proportion on 16:9 (0.75 x 4/3 = 1). The construction
bar, for example, is placed at `a` 0.75 with `tx` 256, its right edge on the
stage's. The correction above came on top of that: 0.5625 of the width on
16:9, a squashed construction bar, Palantir and Powers tree.

`AptWidescreen::authoredSqueeze` (GameClient/GUI/AptScreens/AptWidescreen.h)
reads such a movie from its placements: a clip of the root timeline whose
`a / d` is 0.75 (rotation-free), or ten such placements anywhere (2.02's
SpellStore squeezes inside its tree). The correction is then
`min(1, (scaleY / scaleX) / 0.75)`: on 16:9 the movie looks as it does in
retail with the mod, wider windows stay proportional, 4:3 and narrower are
retail's. The Palantir, the Powers screen and each loaded part of the War of
the Ring HUD are read on their own; the anchors stay as above. No movie of
BFME2 1.06 or RotWK 2.01 meets the rule (every `.apt` of both installs: at
most two such placements, nested, in StrategicDetailsTray), so retail data
keeps the correction unchanged. This too is an OpenBFME inference about mod
data, not a retail rule.

- `mod widescreen: ...` (test_hud_mod_widescreen.cpp): the rule, the
  correction at 4:3 .. 32:9 and the per-movie cache, without data.
- `mod widescreen retail: ...`: every movie of both retail installs reads
  as authored for 4:3.
- `mod widescreen data: ...`: with `OPENBFME_WIDESCREEN_MOD_ARCHIVES` set to
  the mods' archives (`;`-separated), their Palantir, SpellStore and
  StrategicPalantir read as authored for 16:9, and the construction bar ends
  at the height-derived scale on its right edge at 16:9, 21:9 and 32:9.

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

War of the Ring uses the same presentation correction in
`AptStrategicHUD::setViewportSize`, for both RotWK and BFME2 data. The
Palantir, stats and attached selection details tray stay on the left;
END PHASE stays on the right; the checklist, dialog frame (battle prompts
and army transfers) and new-turn indicator stay centred. The correction is
applied to the loaded HUD parts after movie updates and before rendering
or pointer-coordinate conversion, including when parts load late. Resizing
does not alter their authored animation matrices. The viewer's Fit mode
already scales uniformly and removes the correction. BFME1 has no strategic
HUD and is unaffected; simulation rules are unchanged in every edition.

As in the battle HUD, the dynamically positioned help and radial stages
retain their existing coordinate mapping.

- `retail strategic HUD widescreen:*`: installed RotWK and BFME2 panels at
  4:3, 16:9, 21:9, 8K and a narrower viewport, repeated resizing, Fit mode,
  authored matrices and END PHASE mouse input.
- `godot/tests/wotr_hud_viewport_test.gd`: installed RotWK and BFME2 through
  the real Godot shell, viewport, canvas and window-input paths. GPU corner
  artwork is compared with a 4:3 reference at the same height through repeated
  widescreen resizing and Fit. END PHASE clicks use independent window pixels,
  including input after a resize before the next tick/render. Run without
  `--headless`; missing GPU/install prerequisites exit 77.
