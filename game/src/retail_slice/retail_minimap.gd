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
##                    the palantir bezel over a tan paper fill - the sepia
##                    old-map look in every RotWK screenshot.
##   `<map>_pic.tga`  256x256, a full-colour PAINTING of the map's landmark
##                    (Amon Sul's fortress under a blue sky). Map-select and
##                    loading-screen art. NEVER the radar.
##
## The importer publishes the second as `assets/ui/maps/<slug>-art.png` and the
## third as `<slug>-preview.png`, and this control used to be handed the
## PREVIEW - so the radar showed a photographic painting of a fortress stretched
## across the bezel (owner bug, 2026-08-10). It now takes the ink art and paints
## the paper itself, because no retail archive ships a parchment fill bitmap:
## every palantir frame atlas (`apt-palantirexport-11/14/17/20`) is a ring with
## a fully transparent interior, so the paper is engine-drawn in retail too.
##
## THE RADAR IS DRAWN IN SOURCE-GRID SPACE, NOT LOCAL SPACE. `retail_map_data`
## builds its local battlefield frame from the PLAYER-START AXIS, which is
## rotated against the map grid by an arbitrary angle per map (measured over the
## 21 published RotWK maps: 35.8 deg on Fords of Isen II, 155.3 on Amon Sul
## Fortress, 45.5 on Umbar). The ink art is authored in grid space, so painting
## it axis-aligned in local space would hang the drawing at a lie of an angle
## over the blips. Radar space here is (source.x, -source.y) - i.e. grid cell
## (gx, gy) times the horizontal scale, +y downward - which is exactly the ink
## texture's own pixel order (verified against the cooked heightmap: ink ridges
## land on heightmap slope ridges under the identity orientation, and on the
## PLAYABLE core crop `playable_grid_min..playable_grid_max`, not the full grid
## including the camera border).
##
## The separate imported preview remains art, never a false coordinate texture.

## Parchment palette measured off the retail RotWK capture's radar interior
## (mean paper (162,140,94), lit centre (171,151,107), ink (76,44,1) straight
## out of `_art.tga`).
const PAPER_BASE := Color8(171, 149, 105)
const PAPER_EDGE := Color8(118, 98, 63)
const PAPER_INK := Color8(76, 44, 1)
## The bezel interior around the paper, measured off the same capture.
const BEZEL_GLASS := Color8(72, 53, 27)
## Rim darkening inside the bezel, so the glass reads as a lit dish rather than
## a flat brown ring - retail's radar interior falls off to near-black at the
## metal. Applied as concentric bands over `BEZEL_GLASS`.
const BEZEL_VIGNETTE_BANDS := 14
const BEZEL_VIGNETTE_SPAN := 0.34
const BEZEL_VIGNETTE_ALPHA := 0.34
## Retail's sheet is a shade wider than the exact inscribed rectangle - measured
## at 0.74 of the ring's opening against an inscribed 0.707 - so it overfills by
## a few percent and lets the bezel clip the corners.
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
## Baked-paper resolution on the long axis. The ink source is only 128px, so
## anything past a few hundred is upsampled blur for no gain.
const PAPER_MAX_DIMENSION := 384
const PAPER_GRAIN := 7.0
const PAPER_SEED := 0x5A6E1D
const RADAR_DISC_SEGMENTS := 72

var simulation: RefCounted
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
## none (53 of the 76 cooked RotWK maps do not) - those keep bare parchment plus
## the synthetic water schematic.
var map_ink_art: Texture2D
var uses_map_ink_art := false
## Parchment (plus ink, when the map ships it) baked once per configure and
## covering exactly `map_bounds`.
var radar_paper: ImageTexture
var source_geometry_loaded := false
var world_camera: Camera3D
## LOCAL-space camera focus, written by the slice every frame.
var camera_center := Vector2.ZERO
var radar_zoom := 1.0
var radar_zoom_target := 1.0
var zoom_response_seconds := 0.09
var last_center_request := Vector2.ZERO

signal center_requested(world_position: Vector2)
signal order_requested(world_position: Vector2)


func bind_map_ink_art(texture: Texture2D) -> bool:
	## Bind the map's `_art.tga` conversion. Passing the photographic preview
	## here is the bug this control was rewritten for - callers must read the
	## map row's `art` field, not `preview`.
	map_ink_art = texture
	uses_map_ink_art = texture != null
	uses_source_preview_as_background = false
	_rebuild_radar_paper()
	queue_redraw()
	return uses_map_ink_art


func configure(sim: RefCounted, map_data: RefCounted = null, ink_art: Texture2D = null) -> void:
	simulation = sim
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
	if ink_art != null:
		map_ink_art = ink_art
		uses_map_ink_art = true
	_rebuild_radar_paper()
	queue_redraw()


func _process(delta: float) -> void:
	var response := 1.0 - exp(-maxf(delta, 0.0) / maxf(zoom_response_seconds, 0.001))
	radar_zoom = lerpf(radar_zoom, radar_zoom_target, response)
	queue_redraw()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and (event as InputEventMouseButton).pressed:
		var mouse := event as InputEventMouseButton
		if mouse.button_index == MOUSE_BUTTON_WHEEL_UP:
			nudge_zoom(1)
			accept_event()
		elif mouse.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			nudge_zoom(-1)
			accept_event()
		elif mouse.button_index == MOUSE_BUTTON_LEFT:
			last_center_request = _canvas_to_world(mouse.position, _arena())
			center_requested.emit(last_center_request)
			accept_event()
		elif mouse.button_index == MOUSE_BUTTON_RIGHT:
			# Retail: right-click on the radar orders the selection to that
			# world point without moving the camera.
			order_requested.emit(_canvas_to_world(mouse.position, _arena()))
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


# ------------------------------------------------------------------ parchment


func _rebuild_radar_paper() -> void:
	## Bake the paper once. Retail ships no parchment bitmap (see the header),
	## so the fill is synthesized at the measured palette and the map's ink is
	## composited into it; zoom then only moves UVs across this one texture.
	radar_paper = null
	if map_bounds.size.x <= 0.0 or map_bounds.size.y <= 0.0:
		return
	var aspect := map_bounds.size.x / map_bounds.size.y
	var width := PAPER_MAX_DIMENSION
	var height := PAPER_MAX_DIMENSION
	if aspect >= 1.0:
		height = maxi(8, int(round(float(PAPER_MAX_DIMENSION) / aspect)))
	else:
		width = maxi(8, int(round(float(PAPER_MAX_DIMENSION) * aspect)))
	var ink_bytes := PackedByteArray()
	var ink_stride := 0
	if map_ink_art != null:
		var ink_image := map_ink_art.get_image()
		if ink_image != null and not ink_image.is_empty():
			ink_image = ink_image.duplicate()
			ink_image.convert(Image.FORMAT_RGBA8)
			ink_image.resize(width, height, Image.INTERPOLATE_BILINEAR)
			ink_bytes = ink_image.get_data()
			ink_stride = width * 4
	var random := RandomNumberGenerator.new()
	random.seed = PAPER_SEED
	var bytes := PackedByteArray()
	bytes.resize(width * height * 4)
	var fade_span := maxf(4.0, minf(float(width), float(height)) * 0.10)
	var feather_span := maxf(2.0, minf(float(width), float(height)) * 0.035)
	for y in height:
		# Laid-paper fibres: retail's radar interior reads as horizontal grain.
		var fibre := sin(float(y) * 0.9) * 2.5
		for x in width:
			var edge_distance := minf(
				minf(float(x), float(width - 1 - x)),
				minf(float(y), float(height - 1 - y))
			)
			var edge_mix := clampf(1.0 - edge_distance / fade_span, 0.0, 1.0) * 0.75
			var red := lerpf(PAPER_BASE.r8, PAPER_EDGE.r8, edge_mix) + fibre
			var green := lerpf(PAPER_BASE.g8, PAPER_EDGE.g8, edge_mix) + fibre
			var blue := lerpf(PAPER_BASE.b8, PAPER_EDGE.b8, edge_mix) + fibre
			var grain := random.randf_range(-PAPER_GRAIN, PAPER_GRAIN)
			red += grain
			green += grain
			blue += grain
			var index := (y * width + x) * 4
			if ink_stride > 0:
				# INK_OPACITY, not 1.0: at 128px the authored strokes upsample to
				# 3px solids, which read heavier than retail's thin anti-aliased
				# lines even though both use the same (76,44,1).
				var ink_alpha := (float(ink_bytes[y * ink_stride + x * 4 + 3]) / 255.0) * INK_OPACITY
				if ink_alpha > 0.0:
					red = lerpf(red, float(PAPER_INK.r8), ink_alpha)
					green = lerpf(green, float(PAPER_INK.g8), ink_alpha)
					blue = lerpf(blue, float(PAPER_INK.b8), ink_alpha)
			bytes[index] = clampi(int(round(red)), 0, 255)
			bytes[index + 1] = clampi(int(round(green)), 0, 255)
			bytes[index + 2] = clampi(int(round(blue)), 0, 255)
			# The sheet's edge FEATHERS into the bezel glass. Retail's paper has
			# no hard rectangular border either - its straight edges are soft.
			bytes[index + 3] = clampi(
				int(round(255.0 * clampf(edge_distance / feather_span, 0.0, 1.0))), 0, 255
			)
	radar_paper = ImageTexture.create_from_image(
		Image.create_from_data(width, height, false, Image.FORMAT_RGBA8, bytes)
	)


# ----------------------------------------------------------------------- draw


func _arena() -> Rect2:
	## The paper rectangle, INSCRIBED IN THE BEZEL, which is how retail composes
	## it: on the RotWK capture the parchment's straight left and right edges are
	## visible INSIDE the ring with dark glass between them and the metal, and the
	## paper's width comes to 0.74 of the ring's opening - the inscribed rectangle
	## of a square-ish map is 0.707. Filling the whole square control instead (the
	## old behaviour) throws the map's four corners away under the frame.
	var bounds := _visible_bounds()
	var aspect := maxf(bounds.size.x, 0.001) / maxf(bounds.size.y, 0.001)
	var diagonal := 2.0 * bezel_radius() * PAPER_FILL
	var height := diagonal / sqrt(1.0 + aspect * aspect)
	var paper := Vector2(height * aspect, height)
	return Rect2(Rect2(Vector2.ZERO, size).get_center() - paper * 0.5, paper)


func bezel_radius() -> float:
	return minf(size.x, size.y) * BEZEL_RADIUS_RATIO


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
	var paper_origin := _radar_to_canvas(map_bounds.position, arena)
	var paper_end := _radar_to_canvas(map_bounds.end, arena)
	var paper_size := paper_end - paper_origin
	if radar_paper != null and absf(paper_size.x) > 0.5 and absf(paper_size.y) > 0.5:
		var quad := PackedVector2Array([
			paper_origin,
			Vector2(paper_end.x, paper_origin.y),
			paper_end,
			Vector2(paper_origin.x, paper_end.y),
		])
		for piece in Geometry2D.intersect_polygons(quad, disc):
			var polygon: PackedVector2Array = piece
			var uvs := PackedVector2Array()
			for point in polygon:
				uvs.append((point - paper_origin) / paper_size)
			draw_colored_polygon(polygon, Color.WHITE, uvs, radar_paper)
	# The rim falls off OVER the paper, not just around it: retail's sheet is
	# visibly darker where it runs under the metal.
	var band_width := radius * BEZEL_VIGNETTE_SPAN / float(BEZEL_VIGNETTE_BANDS)
	for band in BEZEL_VIGNETTE_BANDS:
		# RAMPED, not flat: equal-alpha bands would read as a hard ring. The
		# outermost band carries the full darkening and each step inward carries
		# less, so the falloff is a gradient rather than a stack of circles.
		var strength := BEZEL_VIGNETTE_ALPHA * (1.0 - float(band) / float(BEZEL_VIGNETTE_BANDS))
		draw_arc(
			center, radius - band_width * (float(band) + 0.5), 0.0, TAU, RADAR_DISC_SEGMENTS,
			Color(0.0, 0.0, 0.0, strength), band_width, true
		)
	# A map that publishes no ink art still gets its water read on the paper;
	# a map that DOES already carries its lakes and rivers in the ink, so the
	# schematic would only double-print them.
	if source_geometry_loaded and not uses_map_ink_art:
		_draw_source_geometry(arena, disc)
	if simulation != null:
		for id in simulation.entity_ids():
			var entity: Dictionary = simulation.entity(id)
			if int(entity.get("health", 0)) <= 0:
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
			var point := _world_to_canvas(Vector2(structure["position"]), arena)
			if point.distance_to(center) > radius:
				continue
			var color := Color("56b5ff") if int(structure["team"]) == 0 else Color("ff6259")
			draw_rect(Rect2(point - Vector2(3.0, 3.0), Vector2(6.0, 6.0)), Color(0.16, 0.11, 0.05, 0.85), true)
			draw_rect(Rect2(point - Vector2(2.0, 2.0), Vector2(4.0, 4.0)), color, true)
	_draw_camera_footprint(arena, disc)


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
		draw_line(edge_a, edge_b, PAPER_BASE, 4.0, true)


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
