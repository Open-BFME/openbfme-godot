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

const HouseColorScript := preload("res://src/retail_slice/retail_house_color.gd")
## Retail reference capture: the view box spans about one fifth of the radar
## disc. Cap the projected quad's longest map-space axis to that observed scale
## after bounding far rays at the camera focus plane.
const CAMERA_FOOTPRINT_MAX_MAP_FRACTION := 0.20

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
## `MappedImage RadarViewBoxEdge`: handcreatedmappedimages.ini authors the crop
## at Left:1 Top:0 Right:8 Bottom:8 in an 8x8 source texture.
const RETAIL_VIEW_BOX_EDGE_SIZE := Vector2i(7, 8)
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
## Authored gold camera/view-box edge, supplied by the shared interface-art
## index. The selected packs predate this crop, so the procedural line remains
## a named fallback until a later immutable pack cook publishes it.
var retail_view_box_edge: Texture2D
var uses_retail_view_box_edge := false
var view_box_edge_source := "procedural-fallback"
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
## The radar viewport and bezel rectangle as they were at the moment of press.
## Every motion in one drag is measured against these, never against the live
## values, which the drag itself is busy moving. See the press branch.
var _scrub_bounds := Rect2()
var _scrub_arena := Rect2()
## Precomputed from authored fixture placement centres/yaws. Wall spans use
## half the nearest same-type placement spacing, so a 125-piece curtain reads
## as a curtain rather than 125 identical structure dots.
var _castle_fixture_markers: Dictionary = {}
var _bound_castle_fixture_ids: Dictionary = {}

signal center_requested(world_position: Vector2)
signal order_requested(world_position: Vector2)


func blip_color_for_team(team: int) -> Color:
	## Radar markers are presentation, but their color is not independent art:
	## retail uses the same match-selected house color as the unit/structure.
	var fallback := Color(HouseColorScript.TEAM_COLORS.get(team, Color.WHITE))
	return HouseColorScript.color_for_team(team, fallback)


func blip_layers_for(kind: String, team: int) -> Array[Dictionary]:
	## Headless-verifiable draw contract. The renderer consumes these exact
	## layers, so one returned layer proves there is no invented halo underneath
	## the authored house-colour marker.
	if kind == "unit":
		return [{"shape": "circle", "radius": 2.3, "color": blip_color_for_team(team)}]
	if kind == "structure":
		return [{"shape": "square", "half_size": 2.0, "color": blip_color_for_team(team)}]
	return []


func bind_retail_view_box_edge(texture: Texture2D) -> bool:
	retail_view_box_edge = null
	uses_retail_view_box_edge = false
	view_box_edge_source = "procedural-fallback"
	if texture == null:
		queue_redraw()
		return false
	var image := texture.get_image()
	if image == null or image.is_empty():
		queue_redraw()
		return false
	if Vector2i(image.get_width(), image.get_height()) != RETAIL_VIEW_BOX_EDGE_SIZE:
		queue_redraw()
		return false
	retail_view_box_edge = texture
	uses_retail_view_box_edge = true
	view_box_edge_source = "RadarViewBoxEdge"
	queue_redraw()
	return true


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
	_build_castle_fixture_markers()
	queue_redraw()


func _build_castle_fixture_markers() -> void:
	_castle_fixture_markers.clear()
	if simulation == null:
		return
	var wall_rows: Array[Dictionary] = []
	for id_value in simulation.structure_ids():
		var id := int(id_value)
		var row: Dictionary = simulation.structure(id)
		if String(row.get("structure_kind", "")) != "castle_fixture":
			continue
		if not _bound_castle_fixture_ids.has(id):
			continue
		var kind_of: Array[String] = []
		for token in row.get("castle_fixture_kind_of", []) as Array:
			kind_of.append(String(token).to_upper())
		if kind_of.has("INERT") or kind_of.has("UNATTACKABLE"):
			continue
		var role := String(row.get("castle_fixture_role", ""))
		var marker := {
			"id": id,
			"role": role,
			"type_name": String(row.get("castle_fixture_type", "")),
			"position": Vector2(row.get("position", Vector2.ZERO)),
			"yaw": float(row.get("facing_radians", 0.0)),
			"team": int(row.get("team", -1)),
			"half_length": 0.0,
		}
		_castle_fixture_markers[id] = marker
		if role == "wall":
			wall_rows.append(marker)
	for marker in wall_rows:
		var nearest := INF
		for other in wall_rows:
			if int(other["id"]) == int(marker["id"]) or String(other["type_name"]) != String(marker["type_name"]):
				continue
			nearest = minf(nearest, Vector2(marker["position"]).distance_to(Vector2(other["position"])))
		if is_finite(nearest) and nearest > 0.0:
			marker["half_length"] = nearest * 0.5
			_castle_fixture_markers[int(marker["id"])] = marker


func set_castle_fixture_bound_ids(ids: Array) -> void:
	_bound_castle_fixture_ids.clear()
	for id_value in ids:
		_bound_castle_fixture_ids[int(id_value)] = true
	_build_castle_fixture_markers()
	queue_redraw()


func castle_fixture_marker_count() -> int:
	## Contract count, independent of this local player's current shroud. A
	## marker can exist but be temporarily gated from `_draw`; dead rows do not.
	var count := 0
	for id_value in _castle_fixture_markers:
		var row: Dictionary = simulation.structure(int(id_value)) if simulation != null else {}
		if int(row.get("health", 0)) > 0:
			count += 1
	return count


func _castle_fixture_marker_visible(id: int) -> bool:
	if simulation == null:
		return false
	var row: Dictionary = simulation.structure(id)
	if row.is_empty() or int(row.get("health", 0)) <= 0:
		return false
	var position := Vector2(row.get("position", Vector2.ZERO))
	var kind_of: Array = row.get("castle_fixture_kind_of", [])
	var persists := kind_of.has("DONT_HIDE_IF_FOGGED") or kind_of.has("NEVER_CULL_FOR_MP")
	return persists or shroud_overlay == null or shroud_overlay.structure_visible(position)


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
			#
			# The radar frame is LATCHED at press. Above radar_zoom 1.0 the
			# visible bounds are recentred on `camera_center`, and the slice
			# writes camera_center = camera_focus every frame — so re-reading
			# the live viewport per motion event measures each sample against a
			# viewport the PREVIOUS sample just moved. That is a positive
			# feedback loop: a perfectly stationary held cursor walks the camera
			# across the map and pins it to the edge. Measuring the whole drag
			# in the frame it started in removes the loop entirely.
			scrubbing = true
			_scrub_bounds = _visible_bounds()
			_scrub_arena = _arena()
			last_center_request = _canvas_to_world_within(mouse.position, _scrub_arena, _scrub_bounds)
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
		# the mapping clamps to the map edge, which is the retail feel.
		# Latched frame, not the live one — see the press branch above.
		last_center_request = _canvas_to_world_within(
			(event as InputEventMouseMotion).position, _scrub_arena, _scrub_bounds
		)
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
	# THE RADAR MAP IS FULLY DRAWN, SHROUD OR NOT. Retail's [hud] captures are
	# unambiguous: at match start (reference/in game ui.jpg, REF-52) the whole
	# parchment disc shows the full map ink with zero black fog, and the
	# key/evenstar/flag row sits on the metal frame arc ABOVE an unbroken disc.
	# Darkening unexplored cells to opaque black here was the owner's "missing
	# gaps in the radar" (owner playtest 2026-08-18). The shroud still gates
	# what the radar REVEALS: unit blips in fog are skipped below, and
	# structures need explored ground, exactly like retail. Only the terrain
	# darkening is gone — retail never blacks out the parchment.
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
			_draw_blip(point, "unit", int(entity["team"]))
		_draw_castle_fixture_markers(arena, center, radius)
		for id in simulation.structure_ids():
			var structure: Dictionary = simulation.structure(id)
			if String(structure.get("structure_kind", "")) == "castle_fixture":
				continue
			if int(structure.get("health", 0)) <= 0:
				continue
			# Structures survive into fog (see the named GhostObject deviation
			# in retail_shroud_overlay.gd): explored is enough.
			if shroud_overlay != null and not shroud_overlay.structure_visible(Vector2(structure["position"])):
				continue
			var point := _world_to_canvas(Vector2(structure["position"]), arena)
			if point.distance_to(center) > radius:
				continue
			_draw_blip(point, "structure", int(structure["team"]))
	_draw_camera_footprint(arena, disc)


func _draw_blip(point: Vector2, kind: String, team: int) -> void:
	for layer in blip_layers_for(kind, team):
		var color := Color(layer["color"])
		if String(layer["shape"]) == "circle":
			draw_circle(point, float(layer["radius"]), color)
		elif String(layer["shape"]) == "square":
			var half_size := float(layer["half_size"])
			draw_rect(Rect2(point - Vector2.ONE * half_size, Vector2.ONE * half_size * 2.0), color, true)


func _draw_castle_fixture_markers(arena: Rect2, center: Vector2, radius: float) -> void:
	for id_value in _castle_fixture_markers:
		var id := int(id_value)
		if not _castle_fixture_marker_visible(id):
			continue
		var marker := _castle_fixture_markers[id] as Dictionary
		var position := Vector2(marker["position"])
		var point := _world_to_canvas(position, arena)
		if point.distance_to(center) > radius:
			continue
		var color := blip_color_for_team(int(marker["team"]))
		if int(marker["team"]) < 0:
			color = Color("d8bd7c")
		var half_length := float(marker.get("half_length", 0.0))
		if String(marker["role"]) == "wall" and half_length > 0.0:
			var yaw := float(marker["yaw"])
			var direction := Vector2(cos(yaw), -sin(yaw))
			var first := _world_to_canvas(position - direction * half_length, arena)
			var second := _world_to_canvas(position + direction * half_length, arena)
			draw_line(first, second, color, 2.0, true)
		else:
			draw_rect(Rect2(point - Vector2(1.5, 1.5), Vector2(3.0, 3.0)), color, true)


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
	return _canvas_to_world_within(canvas, arena, _visible_bounds())


func _canvas_to_world_within(canvas: Vector2, arena: Rect2, visible_bounds: Rect2) -> Vector2:
	## Mapping against an EXPLICIT radar viewport. A held drag must pass the
	## viewport it started with (see `_scrub_bounds`), not re-read the live one.
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
		var radar_polygon := camera_footprint_radar_polygon(world_camera)
		var projected := PackedVector2Array()
		for point in radar_polygon:
			projected.append(_radar_to_canvas(point, arena))
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


func camera_footprint_radar_polygon(camera_value: Camera3D) -> PackedVector2Array:
	## Bound the four screen-corner rays at the camera's focus plane before they
	## can run to a near-horizon ground hit. The old code clamped each final hit
	## independently to map_bounds, collapsing two far corners into one and
	## turning the view box into a wedge. These rays remain four rays throughout.
	if camera_value == null or not is_instance_valid(camera_value):
		return PackedVector2Array()
	var ground_plane := Plane(Vector3.UP, 0.35)
	var camera_origin := camera_value.global_position
	var focus_world := Vector3(camera_center.x, 0.35, camera_center.y)
	var focus_distance := camera_origin.distance_to(focus_world)
	if focus_distance <= 0.001:
		return PackedVector2Array()
	var viewport_rect := camera_value.get_viewport().get_visible_rect()
	var screen_corners := [
		viewport_rect.position,
		Vector2(viewport_rect.end.x, viewport_rect.position.y),
		viewport_rect.end,
		Vector2(viewport_rect.position.x, viewport_rect.end.y),
	]
	var projected := PackedVector2Array()
	for screen_corner in screen_corners:
		var origin := camera_value.project_ray_origin(screen_corner)
		var direction := camera_value.project_ray_normal(screen_corner)
		var hit: Variant = ground_plane.intersects_ray(origin, direction)
		if hit != null:
			var hit_point := hit as Vector3
			var hit_distance := origin.distance_to(hit_point)
			if hit_distance > focus_distance:
				hit_point = origin + direction * focus_distance
			var world_hit := _world_to_radar(Vector2(hit_point.x, hit_point.z))
			projected.append(world_hit)
	return _fit_camera_footprint_inside_map(projected)


func _fit_camera_footprint_inside_map(quad: PackedVector2Array) -> PackedVector2Array:
	## Round-3 (verifier G1): the footprint is the camera's TRUE quad clipped
	## against the map rectangle - retail's box tracks the real camera and is
	## clipped at the map edge. The earlier centroid-scale-and-translate fit
	## made the box non-representative at wide zoom and near edges.
	if quad.size() != 4 or map_bounds.size.x <= 0.0 or map_bounds.size.y <= 0.0:
		return PackedVector2Array()
	var bounds_polygon := PackedVector2Array([
		map_bounds.position,
		Vector2(map_bounds.end.x, map_bounds.position.y),
		map_bounds.end,
		Vector2(map_bounds.position.x, map_bounds.end.y),
	])
	var pieces := Geometry2D.intersect_polygons(quad, bounds_polygon)
	if pieces.is_empty():
		return PackedVector2Array()
	var best := pieces[0]
	var best_area := _polygon_area(best)
	for piece in pieces:
		var area := _polygon_area(piece)
		if area > best_area:
			best = piece
			best_area = area
	if best_area <= 0.0001:
		return PackedVector2Array()
	return best


static func _polygon_area(points: PackedVector2Array) -> float:
	var area := 0.0
	for index in points.size():
		var a := points[index]
		var b := points[(index + 1) % points.size()]
		area += a.x * b.y - b.x * a.y
	return absf(area) * 0.5


static func _bounds_of_points(points: PackedVector2Array) -> Rect2:
	var minimum := points[0]
	var maximum := points[0]
	for point in points:
		minimum.x = minf(minimum.x, point.x)
		minimum.y = minf(minimum.y, point.y)
		maximum.x = maxf(maximum.x, point.x)
		maximum.y = maxf(maximum.y, point.y)
	return Rect2(minimum, maximum - minimum)


func _draw_footprint_outline(quad: PackedVector2Array, disc: PackedVector2Array) -> void:
	## Retail's view wedge is the authored RadarViewBoxEdge tiled along the
	## clipped polygon. A named procedural fallback keeps old packs playable.
	var gold := Color(0.92, 0.84, 0.55, 0.9)
	var pieces := Geometry2D.intersect_polygons(quad, disc)
	if pieces.is_empty():
		return
	for piece in pieces:
		var polygon: PackedVector2Array = piece
		if polygon.size() < 2:
			continue
		if retail_view_box_edge != null:
			for index in polygon.size():
				_draw_retail_view_box_segment(
					polygon[index], polygon[(index + 1) % polygon.size()]
				)
		else:
			var closed := polygon.duplicate()
			closed.append(polygon[0])
			draw_polyline(closed, gold, 1.6, true)


func _draw_retail_view_box_segment(first: Vector2, second: Vector2) -> void:
	var delta := second - first
	var length := delta.length()
	if length <= 0.01 or retail_view_box_edge == null:
		return
	var source_width := float(retail_view_box_edge.get_width())
	draw_set_transform(first, delta.angle() - PI * 0.5)
	draw_texture_rect(
		retail_view_box_edge,
		Rect2(Vector2(-source_width * 0.5, 0.0), Vector2(source_width, length)),
		true
	)
	draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
