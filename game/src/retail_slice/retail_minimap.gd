class_name RetailMinimap
extends Control
## RETAIL'S RADAR IS A PARCHMENT MAP, NOT A PHOTOGRAPH OF THE BATTLEFIELD.
##
## Every RotWK map directory ships THREE images beside the `.map` file and they
## are not interchangeable:
##
##   `<map>.tga`      128x128, near-black with a blue water blob. Engine data,
##                    not something a player ever sees as-is.
##   `<map>_art.tga`  128x128, a SINGLE ink colour (76,44,1) carried entirely in
##                    the alpha channel: hand-drawn coastlines, ridge hatching
##                    and filled lakes. THIS is what retail composites inside
##                    the palantir bezel over the parchment - the sepia old-map
##                    look in every RotWK screenshot.
##   `<map>_pic.tga`  A full-colour PAINTING of the map's landmark (Amon Sul's
##                    fortress under a blue sky). Map-select and loading-screen
##                    art. NEVER the radar. MODALLY 220x220 24-bit with NO alpha
##                    channel: 17 of the 23 published previews are 220x220 and
##                    only 6 are 256x256, so an earlier "256x256" claim here was
##                    an Amon Sul coincidence generalised into a rule.
##
## The importer publishes the second as `assets/ui/maps/<slug>-art.png` and the
## third as `<slug>-preview.png`, and this control used to be handed the
## PREVIEW - so the radar showed a photographic painting of a fortress stretched
## across the bezel (owner bug, 2026-08-10).
##
## THE PAPER IS A RETAIL BITMAP. It is NOT synthesized here, and the claim that
## "no retail archive ships a parchment fill" (this header's own first fix,
## 2026-08-10) was FALSE. The cooked palantir atlas
## `assets/ui/palantir/atlases/apt-palantir-1-d9888d52cd89.png` carries the
## authored radar sheet at `RETAIL_PARCHMENT_REGION` - a 214px disc that is a
## continuous radial gradient from a lit centre (179,160,118) out through
## (162.7,141.5,95.2) mean inside r<60, falling to (15,10,7) by r=100 and to
## alpha 0 by r=110. That bitmap IS the paper AND its own rim vignette, so the
## procedural bake and the 14 concentric darkening arcs that used to stand in
## for it are gone; only the map's ink art is composited over it. With no pack
## mounted (no atlas) the radar draws one flat measured disc and nothing else -
## a visible degradation, not a hand-drawn imitation of retail.
##
## THE RADAR IS DRAWN IN SOURCE-GRID SPACE, NOT LOCAL SPACE. `retail_map_data`
## builds its local battlefield frame from the PLAYER-START AXIS, which is
## rotated against the map grid by an arbitrary angle per map (measured over the
## 21 art-bearing RotWK maps: 35.8 deg on Fords of Isen II, 155.3 on Amon Sul
## Fortress, 45.5 on Umbar). The ink art is authored in grid space, so painting
## it axis-aligned in local space would hang the drawing at a lie of an angle
## over the blips. Radar space here is (source.x, -source.y) - i.e. grid cell
## (gx, gy) times the horizontal scale, +y downward - which is exactly the ink
## texture's own pixel order.
##
## THAT REGISTRATION IS PINNED AGAINST REAL COOKED DATA, not against a stub:
## `minimap_parchment_runner`'s `ink_registers_against_the_cooked_heightmap`
## correlates every art alpha sample with the heightmap slope under it and
## requires the playable-crop mapping to beat both rivals. Measured over all 21
## art-bearing maps: playable crop mean r=+0.362, full bordered grid +0.086,
## vertical flip +0.213; the playable crop wins on 21/21 against the full grid
## and 20/21 against the flip. Amon Sul Fortress alone reads +0.316 / +0.030 /
## +0.068.
##
## MAPS WITHOUT INK. 21 of the 75 cooked RotWK maps publish `<slug>-art.png`;
## the other 54 fall back to bare parchment plus the synthetic water schematic.
## Two art files (`harlindon-art.png`, `weather-hills-art.png`) are ORPHANS -
## the pack publishes the image but cooks no map directory for it. That is an
## importer follow-up, not a radar bug.
##
## The separate imported preview remains art, never a false coordinate texture.

## The authored radar sheet inside the cooked palantir atlas. Measured, not
## guessed: outside this rectangle the atlas is spell/summon sprite work, and
## an earlier pass cropped the palantir ORB globe from the same sheet and
## stretched it over the disc (the "palantir icon over the radar" bug).
const RETAIL_PARCHMENT_REGION := Rect2i(4, 4, 214, 214)
## The ink colour retail authors into `_art.tga` (carried in the alpha channel;
## every non-transparent texel in the cooked PNG is exactly this RGB). Used for
## the no-ink water schematic; the ink art itself is drawn with its OWN colour
## rather than re-tinted.
const PAPER_INK := Color8(76, 44, 1)
## The bezel interior behind the sheet. The palantir ring atlas has a fully
## transparent middle, so without this the battlefield shows through the ring
## around the parchment disc's soft edge. Measured off the RotWK capture.
const BEZEL_GLASS := Color8(72, 53, 27)
## THE ONLY fallback paper: the mean of the retail disc inside r<60, drawn flat
## when no mounted pack carries the palantir atlas. Deliberately featureless -
## a degraded radar should look degraded.
const PAPER_FALLBACK := Color8(163, 142, 95)
## The ink sheet is a shade wider than the exact inscribed rectangle - retail's
## capture puts the drawn map at 0.74 of the ring's opening against an inscribed
## 0.707 - so it overfills by a few percent and lets the bezel clip the corners.
const PAPER_FILL := 1.06
const INK_OPACITY := 0.82
## Where the palantir bezel's opening is, as a fraction of the radar control's
## side. The retail control-bar ring (`apt-palantirexport-17` region (0,0,250,256)
## drawn into dock rect (19,6,375,384), which is how `retail_hud.gd` composes it)
## is not a clean circle around `RETAIL_RADAR_CENTER`: rays from the radar centre
## meet the ring's INNER edge as far out as 163.8px on the left and lose its
## OUTER edge as early as 161.3px on the right. 162px of 181 splits that - at
## worst a 2px sliver of world at the left rim, never parchment hanging outside
## the frame, which is how the old square backdrop spilled its corners.
const BEZEL_RADIUS_RATIO := 0.4475
const RADAR_DISC_SEGMENTS := 72

var simulation: RefCounted
## The local player's shroud, or null for a fog-off match / a legacy caller.
var shroud_overlay: RefCounted = null
var source_map_data: RefCounted
## Radar-space bounds: source grid units for a cooked map, local units for the
## unconfigured fallback.
var map_bounds := Rect2(Vector2(-60.0, -45.0), Vector2(120.0, 90.0))
var mapping_mode := "unconfigured"
var radar_space := "local-fallback"
## Kept as a PERMANENT false: the photographic `<map>_pic.tga` preview must
## never become the radar backdrop again. `retail_slice_runner` pins it.
var uses_source_preview_as_background := false
## The map's converted `_art.tga` ink overlay, or null for a map that publishes
## none (54 of the 75 cooked RotWK maps do not) - those keep bare parchment plus
## the synthetic water schematic.
var map_ink_art: Texture2D
var uses_map_ink_art := false
## RETAIL'S authored parchment sheet, cropped once out of the palantir atlas by
## `bind_retail_parchment`. Independent of the map, of the ink and of the zoom:
## it is the bezel's paper, so it never pans. Null until a mounted pack supplies
## the atlas, and `_draw` then falls back to one flat `PAPER_FALLBACK` disc.
var radar_paper: ImageTexture
var uses_retail_parchment := false
## "retail-atlas" or "flat-fallback" - which of the two the last draw used, so
## a missing pack reads as a named degradation in diagnostics rather than as a
## slightly duller radar nobody notices.
var parchment_source := "flat-fallback"
var source_geometry_loaded := false
var world_camera: Camera3D
## LOCAL-space camera focus, written by the slice every frame.
var camera_center := Vector2.ZERO
var radar_zoom := 1.0
var radar_zoom_target := 1.0
var zoom_response_seconds := 0.09
var last_center_request := Vector2.ZERO
## True while the left button is held on the radar: the camera follows the
## cursor until release (retail drag-scrub).
var scrubbing := false

signal center_requested(world_position: Vector2)
signal order_requested(world_position: Vector2)


func bind_retail_parchment(atlas: Texture2D) -> bool:
	## Crop retail's authored radar sheet out of the cooked palantir atlas.
	## Called once from the HUD's art pass; the sheet does not depend on the map,
	## so nothing here re-runs per match. Passing an atlas that is too small for
	## `RETAIL_PARCHMENT_REGION` binds NOTHING rather than a silently clamped
	## crop of whatever else is at (4,4).
	radar_paper = null
	uses_retail_parchment = false
	parchment_source = "flat-fallback"
	if atlas == null:
		queue_redraw()
		return false
	var image := atlas.get_image()
	if image == null or image.is_empty():
		queue_redraw()
		return false
	if (
		image.get_width() < RETAIL_PARCHMENT_REGION.end.x
		or image.get_height() < RETAIL_PARCHMENT_REGION.end.y
	):
		queue_redraw()
		return false
	var sheet := image.get_region(RETAIL_PARCHMENT_REGION)
	sheet.convert(Image.FORMAT_RGBA8)
	radar_paper = ImageTexture.create_from_image(sheet)
	uses_retail_parchment = true
	parchment_source = "retail-atlas"
	queue_redraw()
	return true


func bind_map_ink_art(texture: Texture2D) -> bool:
	## Bind the map's `_art.tga` conversion. Passing the photographic preview
	## here is the bug this control was rewritten for - callers must read the
	## map row's `art` field, not `preview`. The ink is drawn OVER the parchment
	## every frame rather than baked into it, so this touches no texture.
	map_ink_art = texture
	uses_map_ink_art = texture != null
	uses_source_preview_as_background = false
	queue_redraw()
	return uses_map_ink_art


func configure(
	sim: RefCounted,
	map_data: RefCounted = null,
	ink_art: Texture2D = null,
	shroud: RefCounted = null
) -> void:
	simulation = sim
	# Optional and last: every existing caller keeps its three-argument call and
	# gets a shroud-free radar, which is what a fog-off match wants.
	shroud_overlay = shroud
	source_map_data = map_data
	uses_source_preview_as_background = false
	if map_data != null and bool(map_data.ready):
		# grid_to_source_xy, not grid * horizontal_scale: the cooked grid is
		# anchored at SAGE's INNER map corner, so cell (border, border) is source
		# origin. Multiplying the raw cell index would slide the whole sheet by
		# the camera border - 400 source units on Amon Sul.
		var grid_min: Vector2 = map_data.grid_to_source_xy(
			float(map_data.playable_grid_min.x), float(map_data.playable_grid_min.y)
		)
		var grid_max: Vector2 = map_data.grid_to_source_xy(
			float(map_data.playable_grid_max.x), float(map_data.playable_grid_max.y)
		)
		map_bounds = Rect2(grid_min, grid_max - grid_min)
		radar_space = "source-grid"
		mapping_mode = "source-derived-local-transform"
		source_geometry_loaded = true
	else:
		map_bounds = Rect2(Vector2(-60.0, -45.0), Vector2(120.0, 90.0))
		radar_space = "local-fallback"
		mapping_mode = "fallback-schematic"
		source_geometry_loaded = false
	camera_center = _radar_to_world(map_bounds.get_center())
	# ALWAYS assigned, including null: configuring a map that publishes no ink
	# art has to CLEAR the previous map's drawing, or the radar keeps painting
	# the last map's coastlines over the new one.
	map_ink_art = ink_art
	uses_map_ink_art = ink_art != null
	queue_redraw()


func _process(delta: float) -> void:
	var response := 1.0 - exp(-maxf(delta, 0.0) / maxf(zoom_response_seconds, 0.001))
	radar_zoom = lerpf(radar_zoom, radar_zoom_target, response)
	queue_redraw()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var mouse := event as InputEventMouseButton
		if mouse.button_index == MOUSE_BUTTON_LEFT and not mouse.pressed:
			scrubbing = false
			accept_event()
			return
		if not mouse.pressed:
			return
		if mouse.button_index == MOUSE_BUTTON_WHEEL_UP:
			nudge_zoom(1)
			accept_event()
		elif mouse.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			nudge_zoom(-1)
			accept_event()
		elif mouse.button_index == MOUSE_BUTTON_LEFT:
			# Retail: press jumps the camera there, and HOLDING drags it — the
			# radar scrubs continuously under the cursor until the button is
			# released.
			scrubbing = true
			last_center_request = _canvas_to_world(mouse.position, _arena())
			center_requested.emit(last_center_request)
			accept_event()
		elif mouse.button_index == MOUSE_BUTTON_RIGHT:
			# Retail: right-click on the radar orders the selection to that
			# world point without moving the camera.
			order_requested.emit(_canvas_to_world(mouse.position, _arena()))
			accept_event()
	elif event is InputEventMouseMotion and scrubbing:
		# Godot keeps this control as the GUI mouse focus from press to release,
		# so the drag keeps scrubbing even when the cursor leaves the bezel;
		# _canvas_to_world clamps to the map edge, which is the retail feel.
		last_center_request = _canvas_to_world((event as InputEventMouseMotion).position, _arena())
		center_requested.emit(last_center_request)
		accept_event()


func nudge_zoom(direction: int) -> void:
	# One notch is deliberately meaningful; convergence is fast enough to feel
	# immediate without a camera-jarring single-frame pop.
	var factor := 1.32 if direction > 0 else (1.0 / 1.32)
	radar_zoom_target = clampf(radar_zoom_target * factor, 1.0, 2.8)


func set_zoom(value: float, immediate: bool = false) -> void:
	radar_zoom_target = clampf(value, 1.0, 2.8)
	if immediate:
		radar_zoom = radar_zoom_target
	queue_redraw()


# ----------------------------------------------------------------------- draw


func _arena() -> Rect2:
	## The DRAWN MAP's rectangle, INSCRIBED IN THE BEZEL, which is how retail
	## composes it: on the RotWK capture the drawing's straight left and right
	## edges are visible INSIDE the ring with paper still showing between them and
	## the metal, and its width comes to 0.74 of the ring's opening - the
	## inscribed rectangle of a square-ish map is 0.707. Filling the whole square
	## control instead (the old behaviour) throws the map's four corners away
	## under the frame. The PARCHMENT is a separate, larger disc (`_paper_square`)
	## that fills the whole opening and never pans with the map.
	var bounds := _visible_bounds()
	var aspect := maxf(bounds.size.x, 0.001) / maxf(bounds.size.y, 0.001)
	var diagonal := 2.0 * bezel_radius() * PAPER_FILL
	var height := diagonal / sqrt(1.0 + aspect * aspect)
	var paper := Vector2(height * aspect, height)
	return Rect2(Rect2(Vector2.ZERO, size).get_center() - paper * 0.5, paper)


func bezel_radius() -> float:
	return minf(size.x, size.y) * BEZEL_RADIUS_RATIO


func _paper_square() -> Rect2:
	## Where retail's parchment bitmap lands: the square that circumscribes the
	## bezel opening, so the authored disc's own edge falls exactly on the ring.
	var radius := bezel_radius()
	var center := Rect2(Vector2.ZERO, size).get_center()
	return Rect2(center - Vector2(radius, radius), Vector2(radius, radius) * 2.0)


func ink_sheet(arena: Rect2) -> Rect2:
	## Where the map's ink art is drawn, in canvas pixels: exactly `map_bounds`
	## through the radar transform, so texture UV (0,0) is the playable grid's
	## min corner and (1,1) its max corner. Pans and zooms with the map; the
	## parchment underneath does not. Public because the runners assert on it.
	var origin := _radar_to_canvas(map_bounds.position, arena)
	var end := _radar_to_canvas(map_bounds.end, arena)
	return Rect2(origin, end - origin)


func _radar_disc() -> PackedVector2Array:
	## The bezel's interior. The paper is a RECTANGLE (retail shows its straight
	## left/right edges inside the oval) so everything outside this disc has to
	## be cut, or the map's corners hang over the frame - which is exactly how
	## the photographic preview used to spill past the ring.
	var rect := Rect2(Vector2.ZERO, size)
	var center := rect.get_center()
	var radius := bezel_radius()
	var disc := PackedVector2Array()
	for index in RADAR_DISC_SEGMENTS:
		var angle := TAU * float(index) / float(RADAR_DISC_SEGMENTS)
		disc.append(center + Vector2(cos(angle), sin(angle)) * radius)
	return disc


func _draw() -> void:
	var arena := _arena()
	var disc := _radar_disc()
	var center := Rect2(Vector2.ZERO, size).get_center()
	var radius := bezel_radius()
	# Retail's bezel interior is opaque dark glass, not a hole: the ring atlas
	# ships a transparent middle, so the battlefield would otherwise show through
	# the ring around the paper. Measured off the capture at (58,42,21).
	draw_colored_polygon(disc, BEZEL_GLASS)
	# RETAIL'S OWN PARCHMENT, straight out of the palantir atlas. It carries its
	# lit centre AND its rim falloff, so there are no synthetic vignette arcs
	# over it any more - the darkening at the metal is authored.
	if radar_paper != null:
		var paper := _paper_square()
		var paper_quad := PackedVector2Array([
			paper.position,
			Vector2(paper.end.x, paper.position.y),
			paper.end,
			Vector2(paper.position.x, paper.end.y),
		])
		# CLIPPED to the bezel, like everything else. The authored sheet's soft
		# edge runs a couple of percent past its own opaque disc, which at the
		# bezel's scale put a faint ring of paper OUTSIDE the ring opening.
		for piece in Geometry2D.intersect_polygons(paper_quad, disc):
			var polygon: PackedVector2Array = piece
			var paper_uvs := PackedVector2Array()
			for point in polygon:
				paper_uvs.append((point - paper.position) / paper.size)
			draw_colored_polygon(polygon, Color.WHITE, paper_uvs, radar_paper)
	else:
		# No mounted pack carries the atlas. One flat measured disc, no imitation
		# grain, no fake vignette: the radar should read as degraded.
		draw_colored_polygon(disc, PAPER_FALLBACK)
	# The map's hand-drawn overlay, at its OWN authored colour (every cooked
	# `-art.png` texel is (76,44,1) with the strokes in alpha), laid over the
	# paper rather than baked into it - the paper is fixed to the bezel and the
	# drawing pans and zooms with the battlefield.
	var sheet := ink_sheet(arena)
	if map_ink_art != null and absf(sheet.size.x) > 0.5 and absf(sheet.size.y) > 0.5:
		var ink_quad := PackedVector2Array([
			sheet.position,
			Vector2(sheet.end.x, sheet.position.y),
			sheet.end,
			Vector2(sheet.position.x, sheet.end.y),
		])
		for piece in Geometry2D.intersect_polygons(ink_quad, disc):
			var polygon: PackedVector2Array = piece
			var uvs := PackedVector2Array()
			for point in polygon:
				uvs.append((point - sheet.position) / sheet.size)
			# INK_OPACITY, not 1.0: at 128px the authored strokes upsample to
			# ~3px solids, which read heavier than retail's thin anti-aliased
			# lines even though both carry the same (76,44,1).
			draw_colored_polygon(
				polygon, Color(1.0, 1.0, 1.0, INK_OPACITY), uvs, map_ink_art
			)
	# A map that publishes no ink art still gets its water read on the paper;
	# a map that DOES already carries its lakes and rivers in the ink, so the
	# schematic would only double-print them.
	if source_geometry_loaded and not uses_map_ink_art:
		_draw_source_geometry(arena, disc)
	# The shroud is drawn between the land and the blips, so the radar can never
	# leak what the battlefield hides: a fogged region is dimmed here by exactly
	# the retail alpha the terrain shader modulates by, and the blip loops below
	# skip anything the local player cannot currently see.
	if shroud_overlay != null and bool(shroud_overlay.enabled):
		_draw_shroud(arena, disc)
	if simulation != null:
		for id in simulation.entity_ids():
			var entity: Dictionary = simulation.entity(id)
			if int(entity.get("health", 0)) <= 0:
				continue
			# Units only. A unit in fog is gone from the radar, exactly as it is
			# gone from the battlefield.
			if shroud_overlay != null and not shroud_overlay.unit_visible(Vector2(entity["position"])):
				continue
			var point := _world_to_canvas(Vector2(entity["position"]), arena)
			if point.distance_to(center) > radius:
				continue
			var color := Color("56b5ff") if int(entity["team"]) == 0 else Color("ff6259")
			draw_circle(point, 3.4, Color(0.16, 0.11, 0.05, 0.85))
			draw_circle(point, 2.3, color)
		for id in simulation.structure_ids():
			var structure: Dictionary = simulation.structure(id)
			if int(structure.get("health", 0)) <= 0:
				continue
			# Structures survive into fog (see the named GhostObject deviation
			# in retail_shroud_overlay.gd): explored is enough.
			if shroud_overlay != null and not shroud_overlay.structure_visible(Vector2(structure["position"])):
				continue
			var point := _world_to_canvas(Vector2(structure["position"]), arena)
			if point.distance_to(center) > radius:
				continue
			var color := Color("56b5ff") if int(structure["team"]) == 0 else Color("ff6259")
			draw_rect(Rect2(point - Vector2(3.0, 3.0), Vector2(6.0, 6.0)), Color(0.16, 0.11, 0.05, 0.85), true)
			draw_rect(Rect2(point - Vector2(2.0, 2.0), Vector2(4.0, 4.0)), color, true)
	_draw_camera_footprint(arena, disc)


func _draw_shroud(arena: Rect2, disc: PackedVector2Array) -> void:
	## ONE textured polygon over the bezel disc, not one quad per shroud cell.
	## The grid is 183x183 cells on a slice map; a per-cell loop with a polygon
	## clip each would be 33,000 clipped draws EVERY redraw, and the radar
	## redraws every frame. The overlay hands over a premultiplied darkening
	## image instead (black, with alpha = 255 - retail visibility) so the whole
	## layer is a single `draw_colored_polygon` with the disc's own UVs.
	##
	## The UVs come back through `_canvas_to_world`, the exact inverse of the
	## mapping the blips use, so the shroud cannot drift away from the units it
	## is supposed to be hiding - including on a `source-grid` radar where the
	## forward mapping negates Y.
	if shroud_overlay == null or disc.size() < 3:
		return
	var texture: Texture2D = shroud_overlay.minimap_texture()
	if texture == null:
		return
	var grid: Rect2 = shroud_overlay.bounds()
	if grid.size.x <= 0.0 or grid.size.y <= 0.0:
		return
	var uvs := PackedVector2Array()
	uvs.resize(disc.size())
	for index in range(disc.size()):
		var world := _canvas_to_world(disc[index], arena)
		uvs[index] = (world - grid.position) / grid.size
	draw_colored_polygon(disc, Color(1.0, 1.0, 1.0, 1.0), uvs, texture)


func _draw_source_geometry(arena: Rect2, disc: PackedVector2Array) -> void:
	## Fallback read for a map with no `_art.tga`: the paper already IS the
	## land, so only the water and the map edge are inked.
	var ink_color := PAPER_INK
	ink_color.a = 0.30
	for polygon_value in source_map_data.standing_water_polygons:
		var source_polygon: PackedVector3Array = polygon_value
		var polygon := PackedVector2Array()
		for point in source_polygon:
			polygon.append(_world_to_canvas(Vector2(point.x, point.z), arena))
		_draw_radar_water_polygon(polygon, ink_color, disc)
	for river in source_map_data.river_strips:
		var sections: Array = river.get("sections", [])
		for index in range(sections.size() - 1):
			var first: PackedVector3Array = sections[index]
			var second: PackedVector3Array = sections[index + 1]
			var strip := PackedVector2Array([
				_world_to_canvas(Vector2(first[0].x, first[0].z), arena),
				_world_to_canvas(Vector2(second[0].x, second[0].z), arena),
				_world_to_canvas(Vector2(second[1].x, second[1].z), arena),
				_world_to_canvas(Vector2(first[1].x, first[1].z), arena),
			])
			_draw_radar_water_polygon(strip, ink_color, disc)
	for gate in source_map_data.ford_gates:
		var edge_a := _world_to_canvas(Vector2(gate.get("edge_a", Vector2.ZERO)), arena)
		var edge_b := _world_to_canvas(Vector2(gate.get("edge_b", Vector2.ZERO)), arena)
		# Paper-coloured, so a ford reads as a gap cut through the inked water.
		draw_line(edge_a, edge_b, PAPER_FALLBACK, 4.0, true)


func _draw_radar_water_polygon(polygon: PackedVector2Array, ink_color: Color, disc: PackedVector2Array) -> void:
	## Water shapes projected to radar scale can collapse (points merge, runs
	## go collinear) or self-intersect (river strips whose section orientation
	## flips form bowtie quads). Handing those to draw_colored_polygon fails
	## triangulation inside the renderer and spams an error EVERY redraw, so
	## fills are pre-validated; a shape that cannot fill still reads as an ink
	## outline instead of vanishing.
	var filled := _sanitized_radar_polygon(polygon)
	if not filled.is_empty():
		for piece in Geometry2D.intersect_polygons(filled, disc):
			var clipped := _sanitized_radar_polygon(piece)
			if not clipped.is_empty():
				draw_colored_polygon(clipped, ink_color)
	elif polygon.size() >= 2:
		var closed := polygon.duplicate()
		closed.append(polygon[0])
		draw_polyline(closed, ink_color, 1.0, true)


static func _sanitized_radar_polygon(polygon: PackedVector2Array) -> PackedVector2Array:
	## Returns a fill-safe copy (consecutive duplicates and the redundant
	## closing point removed), or an EMPTY array when the polygon cannot be
	## triangulated (fewer than 3 distinct points, collinear, self-crossing).
	## Callers must not fill an empty result. Pinned by
	## game/tests/minimap_geometry_guard_runner.gd.
	if polygon.size() < 3:
		return PackedVector2Array()
	var cleaned := PackedVector2Array()
	for point in polygon:
		if cleaned.is_empty() or not cleaned[cleaned.size() - 1].is_equal_approx(point):
			cleaned.append(point)
	while cleaned.size() >= 2 and cleaned[0].is_equal_approx(cleaned[cleaned.size() - 1]):
		cleaned.remove_at(cleaned.size() - 1)
	if cleaned.size() < 3:
		return PackedVector2Array()
	# Godot's ear-clipper ACCEPTS exactly-collinear rings (a zero-area fill
	# that renders nothing), so triangulability alone is not enough — reject
	# zero-area shapes too and let the caller's outline fallback draw them.
	var doubled_area := 0.0
	for index in range(cleaned.size()):
		var current := cleaned[index]
		var next := cleaned[(index + 1) % cleaned.size()]
		doubled_area += current.x * next.y - next.x * current.y
	if absf(doubled_area) <= 0.001:
		return PackedVector2Array()
	if Geometry2D.triangulate_polygon(cleaned).is_empty():
		return PackedVector2Array()
	return cleaned


# ------------------------------------------------------------------ transform


func _world_to_radar(world: Vector2) -> Vector2:
	## Local battlefield metres -> source grid units, +y downward (the ink
	## texture's own row order).
	if radar_space != "source-grid" or source_map_data == null:
		return world
	var horizontal: Vector2 = source_map_data.local_to_source_horizontal(world)
	return Vector2(horizontal.x, -horizontal.y)


func _radar_to_world(radar: Vector2) -> Vector2:
	if radar_space != "source-grid" or source_map_data == null:
		return radar
	var local: Vector3 = source_map_data.source_to_local(
		Vector3(radar.x, float(source_map_data.reference_elevation), -radar.y)
	)
	return Vector2(local.x, local.z)


func _world_to_canvas(world: Vector2, arena: Rect2) -> Vector2:
	return _radar_to_canvas(_world_to_radar(world), arena)


func _radar_to_canvas(radar: Vector2, arena: Rect2) -> Vector2:
	var visible_bounds := _visible_bounds()
	var safe_size := Vector2(maxf(visible_bounds.size.x, 1.0), maxf(visible_bounds.size.y, 1.0))
	var scale := minf(arena.size.x / safe_size.x, arena.size.y / safe_size.y)
	var rendered_size := safe_size * scale
	var origin := arena.position + (arena.size - rendered_size) * 0.5
	var normalized := (radar - visible_bounds.position) / safe_size
	return origin + Vector2(normalized.x * rendered_size.x, normalized.y * rendered_size.y)


func _canvas_to_world(canvas: Vector2, arena: Rect2) -> Vector2:
	var visible_bounds := _visible_bounds()
	var safe_size := Vector2(maxf(visible_bounds.size.x, 1.0), maxf(visible_bounds.size.y, 1.0))
	var scale := minf(arena.size.x / safe_size.x, arena.size.y / safe_size.y)
	var rendered_size := safe_size * scale
	var origin := arena.position + (arena.size - rendered_size) * 0.5
	var normalized := (canvas - origin) / rendered_size
	normalized.x = clampf(normalized.x, 0.0, 1.0)
	normalized.y = clampf(normalized.y, 0.0, 1.0)
	return _radar_to_world(visible_bounds.position + normalized * safe_size)


func _visible_bounds() -> Rect2:
	if radar_zoom <= 1.001:
		return map_bounds
	var visible_size := map_bounds.size / radar_zoom
	var half := visible_size * 0.5
	var center := _world_to_radar(camera_center)
	center.x = clampf(center.x, map_bounds.position.x + half.x, map_bounds.end.x - half.x)
	center.y = clampf(center.y, map_bounds.position.y + half.y, map_bounds.end.y - half.y)
	return Rect2(center - half, visible_size)


func _draw_camera_footprint(arena: Rect2, disc: PackedVector2Array) -> void:
	var center := _world_to_canvas(camera_center, arena)
	if world_camera != null and is_instance_valid(world_camera):
		var viewport_rect := world_camera.get_viewport().get_visible_rect()
		var screen_corners := [
			viewport_rect.position,
			Vector2(viewport_rect.end.x, viewport_rect.position.y),
			viewport_rect.end,
			Vector2(viewport_rect.position.x, viewport_rect.end.y),
		]
		var projected := PackedVector2Array()
		for screen_corner in screen_corners:
			var origin := world_camera.project_ray_origin(screen_corner)
			var direction := world_camera.project_ray_normal(screen_corner)
			var hit: Variant = Plane(Vector3.UP, 0.35).intersects_ray(origin, direction)
			if hit != null:
				# The camera frustum regularly spills past the playable edge;
				# retail clips the wedge at the map boundary, so the footprint
				# never escapes the parchment.
				var world_hit := _world_to_radar(Vector2((hit as Vector3).x, (hit as Vector3).z))
				world_hit.x = clampf(world_hit.x, map_bounds.position.x, map_bounds.end.x)
				world_hit.y = clampf(world_hit.y, map_bounds.position.y, map_bounds.end.y)
				projected.append(_radar_to_canvas(world_hit, arena))
		if projected.size() == 4:
			_draw_footprint_outline(projected, disc)
			if center.distance_to(Rect2(Vector2.ZERO, size).get_center()) <= bezel_radius():
				draw_circle(center, 1.8, Color("f0d47c"))
			return
	var visible_bounds := _visible_bounds()
	var footprint_radar := Vector2(visible_bounds.size.x * 0.17, visible_bounds.size.y * 0.14)
	var half := footprint_radar * 0.5
	var radar_center := _world_to_radar(camera_center)
	var points := PackedVector2Array([
		_radar_to_canvas(radar_center + Vector2(-half.x, -half.y), arena),
		_radar_to_canvas(radar_center + Vector2(half.x, -half.y), arena),
		_radar_to_canvas(radar_center + Vector2(half.x, half.y), arena),
		_radar_to_canvas(radar_center + Vector2(-half.x, half.y), arena),
	])
	_draw_footprint_outline(points, disc)
	draw_circle(center, 1.8, Color("f0d47c"))


func _draw_footprint_outline(quad: PackedVector2Array, disc: PackedVector2Array) -> void:
	## Retail's view wedge is a thin gold outline cut at the bezel.
	var gold := Color(0.92, 0.84, 0.55, 0.9)
	var pieces := Geometry2D.intersect_polygons(quad, disc)
	if pieces.is_empty():
		return
	for piece in pieces:
		var polygon: PackedVector2Array = piece
		if polygon.size() < 2:
			continue
		var closed := polygon.duplicate()
		closed.append(polygon[0])
		draw_polyline(closed, gold, 1.6, true)
