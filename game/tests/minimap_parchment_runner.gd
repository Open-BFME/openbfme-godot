extends SceneTree
## THE RADAR IS RETAIL'S PARCHMENT SHEET WITH THE MAP'S INK DRAWN OVER IT, IN
## SOURCE-GRID SPACE.
##
## Three separate bugs are pinned here, all of which shipped:
##
##  1. THE BACKDROP WAS THE WRONG TEXTURE. Retail map directories carry
##     `<map>_art.tga` (single ink colour in the alpha channel - the hand-drawn
##     sepia overlay retail composites in the palantir) and `<map>_pic.tga` (a
##     full-colour painting of the map's landmark, for the loading screen;
##     modally 220x220 with no alpha, NOT 256x256 - that was one map's size
##     mistaken for the rule). The importer publishes them as `<slug>-art.png`
##     and `<slug>-preview.png`, and the radar was wired to the PREVIEW: a
##     photograph of Amon Sul's fortress under a blue sky, stretched across the
##     bezel.
##
##  2. THE PAPER WAS INVENTED. The first fix claimed no retail archive ships a
##     parchment bitmap and synthesized one (procedural grain, fourteen
##     concentric vignette arcs). FALSE: the cooked palantir atlas
##     `assets/ui/palantir/atlases/apt-palantir-1-*.png` carries the authored
##     sheet at `RetailMinimap.RETAIL_PARCHMENT_REGION` - a lit 214px disc with
##     its own rim falloff. `parchment_*` below reads that bitmap out of a
##     MOUNTED PACK and asserts the control's paper against it, so re-introducing
##     a synthesized fill goes red on the real numbers, not on taste.
##
##  3. THE INK WOULD HAVE BEEN HUNG AT THE WRONG ANGLE. `retail_map_data` builds
##     its local frame from the PLAYER-START AXIS, which is rotated against the
##     map grid by an arbitrary per-map angle (35.76 deg on Fords of Isen II).
##     The ink art is authored in grid space, so the radar has to draw in grid
##     space or the drawing and the blips disagree. The stub below uses the real
##     Fords angle, and `paper_corners_track_the_source_grid` goes red the moment
##     anyone maps the art axis-aligned in local space again.
##
## AND ONE REGISTRATION PIN AGAINST REAL COOKED DATA, which no stub can give:
## `ink_registers_against_the_cooked_heightmap` correlates the published art's
## alpha with the heightmap slope beneath it under three rival mappings. Measured
## across all 21 art-bearing RotWK maps: playable crop mean r=+0.362, full
## bordered grid +0.086, vertical flip +0.213, with the playable crop winning
## 21/21 against the full grid and 20/21 against the flip. Amon Sul Fortress -
## the map this asserts on - reads +0.316 / +0.030 / +0.068.
##
## Env: OPENBFME_CONTENT. The pack-backed checks FAIL rather than skip when no
## pack is mounted; a registration pin that quietly passes on an empty machine
## is not a pin.
##
## Usage:
##   Godot_v4.7 --headless --path game --script tests/minimap_parchment_runner.gd

const MinimapScript := preload("res://src/retail_slice/retail_minimap.gd")
## LOADED AT RUNTIME, not preloaded: `retail_map_data.gd` refers to the ModLoader
## autoload, which does not exist yet while this script's preloads are compiled.
## Preloading it fails the whole dependency with "Identifier not found: ModLoader"
## and leaves `.new()` returning nothing.
const MAP_DATA_SCRIPT_PATH := "res://src/retail_slice/retail_map_data.gd"

## Fords of Isen II's measured player-start axis angle against source +X.
const FORDS_AXIS_DEGREES := 35.76
const RADAR_SIZE := Vector2(362.0, 362.0)
## The map the registration pin runs on. Chosen for MARGIN, not convenience:
## its playable-crop correlation clears both rivals by more than 0.24.
const PIN_MAP_ID := "rotwk.map.amon-sul-fortress"
const PALANTIR_ATLAS_DIR := "assets/ui/palantir/atlases"
const PALANTIR_ATLAS_PREFIX := "apt-palantir-1-"
## Retail's authored sheet, measured off the cooked atlas region. Tolerances are
## a couple of 8-bit steps: this is a byte comparison against a shipped bitmap,
## not a judgement about how paper should look.
const PARCHMENT_CENTRE_RGB := Vector3(179.0, 160.3, 118.2)
const PARCHMENT_INNER_RGB := Vector3(162.5, 141.4, 95.2)
const PARCHMENT_TOLERANCE := 3.0
## The authored ink colour in every cooked `<slug>-art.png`.
const INK_RGB := Vector3(76.0, 44.0, 1.0)
## Floors for the registration pin, from the measured Amon Sul values above with
## headroom: the mapping must actually correlate, and it must BEAT its rivals.
const REGISTRATION_FLOOR := 0.25
const REGISTRATION_MARGIN := 0.15

var passed := 0
var failed := 0


func _check(name: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("MINIMAP_PARCHMENT PASS %s" % name)
	else:
		failed += 1
		print("MINIMAP_PARCHMENT FAIL %s | %s" % [name, detail])


## Minimal stand-in for `retail_map_data.gd`: the same rotated local transform,
## the same playable-grid accessors, no cooked pack required.
class StubMapData:
	extends RefCounted

	var ready := true
	var horizontal_scale := 10.0
	var border_width := 30
	var playable_grid_min := Vector2i(30, 30)
	var playable_grid_max := Vector2i(530, 530)
	var reference_elevation := 25.0
	var local_transform_origin := Vector2(2800.0, -2800.0)
	var local_axis_x := Vector2.RIGHT
	var local_axis_z := Vector2.DOWN
	var local_transform_scale := 0.0264923
	var standing_water_polygons: Array = []
	var river_strips: Array = []
	var ford_gates: Array = []
	var map_outline := PackedVector2Array()
	var local_bounds := Rect2()

	func _init(axis_degrees: float) -> void:
		local_axis_x = Vector2.RIGHT.rotated(deg_to_rad(axis_degrees))
		local_axis_z = Vector2(-local_axis_x.y, local_axis_x.x)

	func source_to_local(source_position: Vector3) -> Vector3:
		var delta := Vector2(source_position.x, source_position.z) - local_transform_origin
		return Vector3(
			delta.dot(local_axis_x) * local_transform_scale,
			(source_position.y - reference_elevation) * local_transform_scale,
			delta.dot(local_axis_z) * local_transform_scale
		)

	func local_to_source_horizontal(local_position: Vector2) -> Vector2:
		return (
			local_transform_origin
			+ local_axis_x * (local_position.x / local_transform_scale)
			+ local_axis_z * (local_position.y / local_transform_scale)
		)

	## Same anchoring as the cooked map: the grid starts at SAGE's INNER corner,
	## so cell (border, border) is source origin.
	func grid_to_source_xy(grid_x: float, grid_y: float) -> Vector2:
		var border := float(border_width)
		return Vector2((grid_x - border) * horizontal_scale, (grid_y - border) * horizontal_scale)

	## The local-space point that sits on source grid cell (gx, gy).
	func local_at_grid(cell: Vector2) -> Vector2:
		var horizontal := grid_to_source_xy(cell.x, cell.y)
		var local := source_to_local(
			Vector3(horizontal.x, reference_elevation, -horizontal.y)
		)
		return Vector2(local.x, local.z)


class StubSimulation:
	extends RefCounted

	var entities: Dictionary = {}
	var structures: Dictionary = {}

	func entity_ids() -> Array:
		return entities.keys()

	func entity(id: int) -> Dictionary:
		return entities[id]

	func structure_ids() -> Array:
		return structures.keys()

	func structure(id: int) -> Dictionary:
		return structures[id]


func _ink_texture(size: int, opaque_region: Rect2i) -> ImageTexture:
	## A deliberately ASYMMETRIC ink stamp: opaque only inside `opaque_region`,
	## so a flipped or rotated mapping is visible as ink in the wrong quadrant.
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	image.fill(Color(0.30, 0.17, 0.004, 0.0))
	for y in size:
		for x in size:
			if opaque_region.has_point(Vector2i(x, y)):
				image.set_pixel(x, y, Color(0.30, 0.17, 0.004, 1.0))
	return ImageTexture.create_from_image(image)


func _radial_mean(image: Image, inner: float, outer: float) -> Vector3:
	var centre := Vector2(image.get_width() - 1, image.get_height() - 1) * 0.5
	var total := Vector3.ZERO
	var count := 0
	for y in image.get_height():
		for x in image.get_width():
			var distance := Vector2(x, y).distance_to(centre)
			if distance < inner or distance >= outer:
				continue
			var pixel := image.get_pixel(x, y)
			total += Vector3(pixel.r, pixel.g, pixel.b) * 255.0
			count += 1
	if count == 0:
		return Vector3(-1.0, -1.0, -1.0)
	return total / float(count)


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_run_geometry_checks()
	await process_frame
	await process_frame
	_run_pack_checks()
	print("MINIMAP_PARCHMENT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)


# ------------------------------------------------------------------- geometry


func _run_geometry_checks() -> void:
	var map_data := StubMapData.new(FORDS_AXIS_DEGREES)
	var minimap = MinimapScript.new()
	minimap.size = RADAR_SIZE

	# ---------------------------------------------------------------- binding
	minimap.configure(null, map_data, null)
	_check(
		"configure_uses_source_grid_space",
		String(minimap.radar_space) == "source-grid"
			and String(minimap.mapping_mode) == "source-derived-local-transform",
		"radar_space=%s mapping=%s" % [minimap.radar_space, minimap.mapping_mode]
	)
	# NO ATLAS BOUND = NO PAPER, and it must say so. The whole point of deleting
	# the procedural bake is that an unmounted pack now degrades visibly instead
	# of quietly painting a hand-made imitation of retail.
	_check(
		"unbound_atlas_degrades_to_the_flat_fallback",
		minimap.radar_paper == null
			and not bool(minimap.uses_retail_parchment)
			and String(minimap.parchment_source) == "flat-fallback",
		"paper=%s source=%s" % [minimap.radar_paper, minimap.parchment_source]
	)
	_check("bind_retail_parchment_rejects_a_null_atlas", not bool(minimap.bind_retail_parchment(null)))
	# A too-small texture must bind NOTHING rather than a clamped crop of
	# whatever happens to sit at (4,4) in some other sheet.
	var undersized := ImageTexture.create_from_image(
		Image.create(64, 64, false, Image.FORMAT_RGBA8)
	)
	_check(
		"bind_retail_parchment_rejects_an_undersized_atlas",
		not bool(minimap.bind_retail_parchment(undersized)) and minimap.radar_paper == null
	)
	_check(
		"photographic_preview_is_never_a_backdrop",
		not bool(minimap.uses_source_preview_as_background)
	)

	# ------------------------------------------------------------ ink binding
	# Ink stamped in the TOP-LEFT quarter only.
	var ink := _ink_texture(128, Rect2i(0, 0, 64, 64))
	_check("bind_map_ink_art_reports_bound", bool(minimap.bind_map_ink_art(ink)))
	_check("ink_art_is_held_for_drawing", minimap.map_ink_art == ink and bool(minimap.uses_map_ink_art))
	# Reconfiguring onto a map that publishes no art has to CLEAR the last map's
	# drawing. `configure` used to only ever ASSIGN a non-null texture, so the
	# previous map's coastlines survived the switch.
	minimap.configure(null, map_data, null)
	_check(
		"configure_with_no_art_clears_the_previous_map",
		minimap.map_ink_art == null and not bool(minimap.uses_map_ink_art)
	)

	# ------------------------------------------------------ source-grid layout
	# The ink sheet is the PLAYABLE source grid, so the grid's corners must land
	# on the drawn rectangle's corners even though the local frame is turned
	# 35.76 deg against them.
	minimap.configure(null, map_data, ink)
	var arena: Rect2 = minimap._arena()
	var sheet: Rect2 = minimap.ink_sheet(arena)
	var grid_min := Vector2(map_data.playable_grid_min)
	var grid_max := Vector2(map_data.playable_grid_max)
	var canvas_min: Vector2 = minimap._world_to_canvas(map_data.local_at_grid(grid_min), arena)
	var canvas_max: Vector2 = minimap._world_to_canvas(map_data.local_at_grid(grid_max), arena)
	# The ink texture's UV origin is the sheet's origin, so this IS the
	# orientation assertion: art texel (0,0) sits on playable grid min.
	_check(
		"paper_corners_track_the_source_grid",
		canvas_min.distance_to(sheet.position) < 0.5 and canvas_max.distance_to(sheet.end) < 0.5,
		"min %s vs %s / max %s vs %s" % [canvas_min, sheet.position, canvas_max, sheet.end]
	)
	# The grid's other two corners must land on the other two rectangle corners,
	# NOT rotated off them - the check that actually fails under a local-space
	# mapping, where a turned map paints a diamond.
	var canvas_top_right: Vector2 = minimap._world_to_canvas(
		map_data.local_at_grid(Vector2(grid_max.x, grid_min.y)), arena
	)
	var canvas_bottom_left: Vector2 = minimap._world_to_canvas(
		map_data.local_at_grid(Vector2(grid_min.x, grid_max.y)), arena
	)
	_check(
		"rotated_local_frame_still_paints_an_axis_aligned_map",
		canvas_top_right.distance_to(Vector2(sheet.end.x, sheet.position.y)) < 0.5
			and canvas_bottom_left.distance_to(Vector2(sheet.position.x, sheet.end.y)) < 0.5,
		"tr %s / bl %s (sheet %s)" % [canvas_top_right, canvas_bottom_left, sheet]
	)
	# The grid centre is the centre of the sheet.
	var canvas_centre: Vector2 = minimap._world_to_canvas(
		map_data.local_at_grid((grid_min + grid_max) * 0.5), arena
	)
	_check(
		"grid_centre_is_the_sheet_centre",
		canvas_centre.distance_to(sheet.get_center()) < 0.5,
		"%s vs %s" % [canvas_centre, sheet.get_center()]
	)

	# THE PAPER IS BOLTED TO THE BEZEL; only the drawing moves. Retail's sheet
	# is the dish's own lit surface, so zooming the radar must slide the map
	# across it rather than magnifying the paper's lit centre with it.
	var paper_square: Rect2 = minimap._paper_square()
	minimap.radar_zoom = 2.0
	# Off-centre but clear of the edge clamp, or the zoomed view would pin to the
	# map's corner and the sheet's ORIGIN would land back where it started.
	minimap.camera_center = map_data.local_at_grid((grid_min + grid_max) * 0.5 + Vector2(80.0, 60.0))
	var zoomed_arena: Rect2 = minimap._arena()
	var zoomed_sheet: Rect2 = minimap.ink_sheet(zoomed_arena)
	_check(
		"parchment_is_fixed_to_the_bezel_while_the_map_pans",
		minimap._paper_square().is_equal_approx(paper_square)
			and zoomed_sheet.size.x > sheet.size.x * 1.5
			and not zoomed_sheet.position.is_equal_approx(sheet.position),
		"paper %s->%s sheet %s->%s" % [paper_square, minimap._paper_square(), sheet, zoomed_sheet]
	)
	minimap.radar_zoom = 1.0

	# Clicking the radar has to come back out in LOCAL space, or the camera jumps
	# to a mirrored point on every rotated map.
	var probe_local := map_data.local_at_grid(Vector2(180.0, 420.0))
	var round_trip: Vector2 = minimap._canvas_to_world(minimap._world_to_canvas(probe_local, arena), arena)
	_check(
		"canvas_round_trip_returns_local_space",
		round_trip.distance_to(probe_local) < 0.05,
		"%s vs %s" % [round_trip, probe_local]
	)

	# ------------------------------------------------------------------ blips
	# Blips read from the live simulation in local space and must land inside the
	# sheet for a unit standing inside the playable grid.
	var simulation := StubSimulation.new()
	simulation.entities[1] = {
		"position": map_data.local_at_grid(Vector2(120.0, 150.0)), "team": 0, "health": 100
	}
	simulation.structures[2] = {
		"position": map_data.local_at_grid(Vector2(440.0, 460.0)), "team": 1, "health": 500
	}
	minimap.configure(simulation, map_data, ink)
	_check("configure_keeps_the_ink_art", minimap.map_ink_art == ink and bool(minimap.uses_map_ink_art))
	var blip_a: Vector2 = minimap._world_to_canvas(Vector2(simulation.entities[1]["position"]), arena)
	var blip_b: Vector2 = minimap._world_to_canvas(Vector2(simulation.structures[2]["position"]), arena)
	_check(
		"blips_land_on_the_sheet",
		sheet.has_point(blip_a) and sheet.has_point(blip_b),
		"a=%s b=%s rect=%s" % [blip_a, blip_b, sheet]
	)
	# The two probes are at opposite ends of the grid, so a mapping that collapsed
	# or mirrored would put them in the same place.
	_check("blips_are_separated_like_their_cells", blip_a.distance_to(blip_b) > RADAR_SIZE.x * 0.4)

	minimap.free()


# ----------------------------------------------------------------- real packs


func _find_palantir_atlas(pack_roots: Array) -> String:
	for root_value in pack_roots:
		var directory := String(root_value).path_join(PALANTIR_ATLAS_DIR)
		var names := DirAccess.get_files_at(directory)
		if names == null:
			continue
		for name_value in names:
			var file_name := String(name_value)
			if file_name.begins_with(PALANTIR_ATLAS_PREFIX) and file_name.ends_with(".png"):
				return directory.path_join(file_name)
	return ""


func _run_pack_checks() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		_check("content_db_available", false, "no ContentDB autoload; set OPENBFME_CONTENT")
		return
	var pack_roots: Array = content_db.get("pack_roots") as Array
	_check("mounted_packs_present", not pack_roots.is_empty(), "no pack roots mounted")
	if pack_roots.is_empty():
		return
	_run_parchment_checks(pack_roots)
	_run_registration_pin(content_db)


func _run_parchment_checks(pack_roots: Array) -> void:
	## Assert the control's paper against RETAIL'S BYTES. Every number here was
	## measured off the cooked atlas; nothing is a preference.
	var atlas_path := _find_palantir_atlas(pack_roots)
	_check("palantir_atlas_found_in_a_mounted_pack", atlas_path != "", "looked under %s" % PALANTIR_ATLAS_DIR)
	if atlas_path == "":
		return
	var atlas_image := Image.new()
	if atlas_image.load(atlas_path) != OK:
		_check("palantir_atlas_loads", false, atlas_path)
		return
	var minimap = MinimapScript.new()
	minimap.size = RADAR_SIZE
	var bound: bool = minimap.bind_retail_parchment(ImageTexture.create_from_image(atlas_image))
	_check(
		"retail_parchment_binds_from_the_palantir_atlas",
		bound and minimap.radar_paper != null and bool(minimap.uses_retail_parchment)
			and String(minimap.parchment_source) == "retail-atlas",
		"bound=%s source=%s" % [bound, minimap.parchment_source]
	)
	if not bound:
		minimap.free()
		return
	var sheet: Image = minimap.radar_paper.get_image()
	var region: Rect2i = MinimapScript.RETAIL_PARCHMENT_REGION
	_check(
		"parchment_is_the_authored_region_not_a_resample",
		sheet.get_width() == region.size.x and sheet.get_height() == region.size.y,
		"%dx%d vs %s" % [sheet.get_width(), sheet.get_height(), region.size]
	)
	var centre := _radial_mean(sheet, 0.0, 10.0)
	_check(
		"parchment_centre_is_retails_lit_paper",
		centre.distance_to(PARCHMENT_CENTRE_RGB) < PARCHMENT_TOLERANCE * sqrt(3.0),
		"centre=(%.1f,%.1f,%.1f) expected=%s" % [centre.x, centre.y, centre.z, PARCHMENT_CENTRE_RGB]
	)
	var inner := _radial_mean(sheet, 0.0, 60.0)
	_check(
		"parchment_inner_mean_matches_the_measurement",
		inner.distance_to(PARCHMENT_INNER_RGB) < PARCHMENT_TOLERANCE * sqrt(3.0),
		"inner=(%.1f,%.1f,%.1f) expected=%s" % [inner.x, inner.y, inner.z, PARCHMENT_INNER_RGB]
	)
	# The bitmap carries its OWN rim falloff, which is why the fourteen
	# synthetic vignette arcs are gone. A flat fill, or a crop of some other
	# sprite, would not fall off monotonically from centre to rim.
	var mid := _radial_mean(sheet, 60.0, 70.0)
	var rim := _radial_mean(sheet, 90.0, 100.0)
	_check(
		"parchment_carries_its_own_rim_vignette",
		centre.x > inner.x and inner.x > mid.x and mid.x > rim.x and (centre.x - rim.x) > 100.0,
		"centre=%.1f inner=%.1f mid=%.1f rim=%.1f" % [centre.x, inner.x, mid.x, rim.x]
	)
	# Retail's paper is sepia: r > g > b with a wide red/blue split. The
	# photographic preview that used to be drawn here is sky-blue dominated.
	_check(
		"parchment_reads_as_retail_sepia",
		inner.x > inner.y and inner.y > inner.z and (inner.x - inner.z) > 40.0,
		"inner=(%.1f,%.1f,%.1f)" % [inner.x, inner.y, inner.z]
	)
	minimap.free()


func _run_registration_pin(content_db) -> void:
	## THE REGISTRATION PIN. Everything above this line would still pass if the
	## ink art were mapped onto the wrong rectangle of the world, because a stub
	## has no ground truth to disagree with. This correlates the published art's
	## alpha against the cooked heightmap's slope under three rival mappings and
	## requires the shipping one to win.
	var definition: Dictionary = content_db.call("get_bundle_map", PIN_MAP_ID) as Dictionary
	_check("registration_pin_map_is_mounted", not definition.is_empty(), PIN_MAP_ID)
	if definition.is_empty():
		return
	var pack_root := String(definition.get("_pack_root", ""))
	var art_relative := String(definition.get("art", ""))
	_check(
		"registration_pin_map_publishes_ink_art",
		pack_root != "" and art_relative != "",
		"pack_root=%s art=%s" % [pack_root, art_relative]
	)
	if pack_root == "" or art_relative == "":
		return
	var art := Image.new()
	if art.load(pack_root.path_join(art_relative)) != OK:
		_check("registration_pin_art_loads", false, art_relative)
		return
	art.convert(Image.FORMAT_RGBA8)
	# The art carries ONE colour with the drawing in alpha. If that ever stops
	# being true the ink would have to be re-tinted rather than drawn as-is, so
	# the assumption is asserted rather than trusted.
	var foreign_colour := 0
	for y in art.get_height():
		for x in art.get_width():
			var pixel := art.get_pixel(x, y)
			var rgb := Vector3(pixel.r, pixel.g, pixel.b) * 255.0
			if rgb.distance_to(INK_RGB) > 1.5:
				foreign_colour += 1
	_check(
		"cooked_art_carries_only_the_authored_ink_colour",
		foreign_colour == 0,
		"off-colour texels=%d of %d" % [foreign_colour, art.get_width() * art.get_height()]
	)

	var map_data_script = load(MAP_DATA_SCRIPT_PATH)
	if map_data_script == null:
		_check("registration_pin_map_data_script_loads", false, MAP_DATA_SCRIPT_PATH)
		return
	var map_data = map_data_script.new()
	var loaded := bool(map_data.load_from_pack(pack_root, definition))
	_check("registration_pin_map_loads", loaded, String(map_data.error))
	if not loaded:
		return
	# THE MAPPING UNDER TEST IS THE CONTROL'S OWN. `map_bounds` is whatever
	# `configure` decided the radar covers, in radar space, and the art's UVs are
	# stretched across exactly that rectangle by `ink_sheet`. Reading it back here
	# is what makes this a pin on the shipping code rather than a restatement of
	# the hypothesis that happens to sit beside it.
	var minimap = MinimapScript.new()
	minimap.size = RADAR_SIZE
	minimap.configure(null, map_data, null)
	var shipping: Rect2 = minimap.map_bounds
	minimap.free()
	var playable_min: Vector2 = map_data.grid_to_source_xy(
		float(map_data.playable_grid_min.x), float(map_data.playable_grid_min.y)
	)
	var playable_max: Vector2 = map_data.grid_to_source_xy(
		float(map_data.playable_grid_max.x), float(map_data.playable_grid_max.y)
	)
	_check(
		"radar_bounds_are_the_playable_source_crop",
		shipping.position.is_equal_approx(playable_min)
			and shipping.end.is_equal_approx(playable_max),
		"bounds=%s expected %s..%s" % [shipping, playable_min, playable_max]
	)
	var full_grid_bounds := Rect2(
		map_data.grid_to_source_xy(0.0, 0.0),
		map_data.grid_to_source_xy(float(map_data.width - 1), float(map_data.height - 1))
			- map_data.grid_to_source_xy(0.0, 0.0)
	)
	var playable := _ink_slope_correlation(art, map_data, shipping, false)
	var full_grid := _ink_slope_correlation(art, map_data, full_grid_bounds, false)
	var flipped := _ink_slope_correlation(art, map_data, shipping, true)
	print("[minimap-registration] %s playable=%.4f full_grid=%.4f vflip=%.4f" % [
		PIN_MAP_ID, playable, full_grid, flipped
	])
	_check(
		"ink_registers_against_the_cooked_heightmap",
		playable > REGISTRATION_FLOOR,
		"playable r=%.4f floor=%.2f" % [playable, REGISTRATION_FLOOR]
	)
	# The playable CROP, not the whole bordered grid: SAGE's camera border is
	# unauthored filler and mapping the art across it slides every stroke inward.
	_check(
		"playable_crop_beats_the_full_bordered_grid",
		playable > full_grid + REGISTRATION_MARGIN,
		"playable=%.4f full_grid=%.4f" % [playable, full_grid]
	)
	# IDENTITY row order, not flipped: the art's first row is the grid's low-y
	# edge. A flip is the single most plausible wrong guess and it survives every
	# stub check, because a stub has no terrain to disagree with.
	_check(
		"identity_row_order_beats_a_vertical_flip",
		playable > flipped + REGISTRATION_MARGIN,
		"playable=%.4f vflip=%.4f" % [playable, flipped]
	)


func _ink_slope_correlation(art: Image, map_data, bounds: Rect2, flip_rows: bool) -> float:
	## Pearson correlation between art alpha and heightmap slope magnitude, with
	## the art's UV square stretched across `bounds` in RADAR SPACE and converted
	## to grid cells the same way the map itself does. Slope is the central
	## difference of the raw uint16 samples, which needs no vertical scale to
	## rank hypotheses against each other.
	var width := art.get_width()
	var height := art.get_height()
	if width < 2 or height < 2:
		return NAN
	var alphas := PackedFloat32Array()
	var slopes := PackedFloat32Array()
	alphas.resize(width * height)
	slopes.resize(width * height)
	for v in height:
		for u in width:
			var fraction_y := float(v) / float(height - 1)
			if flip_rows:
				fraction_y = 1.0 - fraction_y
			var source := bounds.position + Vector2(
				float(u) / float(width - 1) * bounds.size.x, fraction_y * bounds.size.y
			)
			var cell: Vector2 = map_data.source_xy_to_grid(source.x, source.y)
			# Clamped one cell inside the heightmap so the central difference never
			# reads an out-of-range zero. Without this the FULL-GRID hypothesis
			# gets a fake cliff along every border row and is rejected for the
			# wrong reason - a comparative test must not stack its own rivals.
			var grid_x := clampi(int(round(cell.x)), 1, int(map_data.width) - 2)
			var grid_y := clampi(int(round(cell.y)), 1, int(map_data.height) - 2)
			var dx: int = int(map_data.height_raw_at(grid_x + 1, grid_y)) - int(map_data.height_raw_at(grid_x - 1, grid_y))
			var dy: int = int(map_data.height_raw_at(grid_x, grid_y + 1)) - int(map_data.height_raw_at(grid_x, grid_y - 1))
			var index := v * width + u
			alphas[index] = art.get_pixel(u, v).a
			slopes[index] = sqrt(float(dx * dx + dy * dy))
	var count := float(alphas.size())
	var mean_alpha := 0.0
	var mean_slope := 0.0
	for index in alphas.size():
		mean_alpha += alphas[index]
		mean_slope += slopes[index]
	mean_alpha /= count
	mean_slope /= count
	var covariance := 0.0
	var alpha_variance := 0.0
	var slope_variance := 0.0
	for index in alphas.size():
		var da := alphas[index] - mean_alpha
		var ds := slopes[index] - mean_slope
		covariance += da * ds
		alpha_variance += da * da
		slope_variance += ds * ds
	if alpha_variance <= 0.0 or slope_variance <= 0.0:
		return NAN
	return covariance / sqrt(alpha_variance * slope_variance)
