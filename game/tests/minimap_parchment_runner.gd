extends SceneTree
## THE RADAR IS A PARCHMENT MAP DRAWN IN SOURCE-GRID SPACE.
##
## Two separate bugs are pinned here, both of which shipped:
##
##  1. THE BACKDROP WAS THE WRONG TEXTURE. Retail map directories carry
##     `<map>_art.tga` (single ink colour in the alpha channel - the hand-drawn
##     sepia overlay retail composites in the palantir) and `<map>_pic.tga` (a
##     full-colour painting of the map's landmark, for the loading screen). The
##     importer publishes them as `<slug>-art.png` and `<slug>-preview.png`, and
##     the radar was wired to the PREVIEW: a photograph of Amon Sul's fortress
##     under a blue sky, stretched across the bezel.
##
##  2. THE INK WOULD HAVE BEEN HUNG AT THE WRONG ANGLE. `retail_map_data` builds
##     its local frame from the PLAYER-START AXIS, which is rotated against the
##     map grid by an arbitrary per-map angle (35.76 deg on Fords of Isen II).
##     The ink art is authored in grid space, so the radar has to draw in grid
##     space or the drawing and the blips disagree. The stub below uses the real
##     Fords angle, and `paper_corners_track_the_source_grid` goes red the moment
##     anyone maps the art axis-aligned in local space again.
##
## Usage:
##   Godot_v4.7 --headless --path game --script tests/minimap_parchment_runner.gd

const MinimapScript := preload("res://src/retail_slice/retail_minimap.gd")

## Fords of Isen II's measured player-start axis angle against source +X.
const FORDS_AXIS_DEGREES := 35.76
const RADAR_SIZE := Vector2(362.0, 362.0)

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
	## so a flipped or rotated bake is visible as ink in the wrong quadrant.
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	image.fill(Color(0.30, 0.17, 0.004, 0.0))
	for y in size:
		for x in size:
			if opaque_region.has_point(Vector2i(x, y)):
				image.set_pixel(x, y, Color(0.30, 0.17, 0.004, 1.0))
	return ImageTexture.create_from_image(image)


func _mean_color(image: Image, region: Rect2i) -> Color:
	var total := Vector3.ZERO
	var count := 0
	for y in range(region.position.y, region.end.y):
		for x in range(region.position.x, region.end.x):
			var pixel := image.get_pixel(x, y)
			total += Vector3(pixel.r, pixel.g, pixel.b)
			count += 1
	if count == 0:
		return Color.BLACK
	total /= float(count)
	return Color(total.x, total.y, total.z)


func _init() -> void:
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
	_check(
		"bare_map_still_bakes_parchment",
		minimap.radar_paper != null and not minimap.uses_map_ink_art
	)
	_check(
		"photographic_preview_is_never_a_backdrop",
		not bool(minimap.uses_source_preview_as_background)
	)

	# Retail's radar interior measured off the RotWK capture: mean (162,140,94).
	# A tan paper has r > g > b with a wide red/blue split; the fortress preview
	# that used to be drawn here is sky-blue dominated, so this rejects it.
	var bare_paper: Image = minimap.radar_paper.get_image()
	var bare_mean := _mean_color(bare_paper, Rect2i(Vector2i.ZERO, bare_paper.get_size()))
	_check(
		"parchment_palette_is_retail_sepia",
		bare_mean.r > bare_mean.g and bare_mean.g > bare_mean.b
			and (bare_mean.r - bare_mean.b) > 0.15
			and bare_mean.r > 0.45 and bare_mean.r < 0.80,
		"mean=(%.3f,%.3f,%.3f)" % [bare_mean.r, bare_mean.g, bare_mean.b]
	)

	# ------------------------------------------------------------ ink compose
	# Ink stamped in the TOP-LEFT quarter only.
	var ink := _ink_texture(128, Rect2i(0, 0, 64, 64))
	_check("bind_map_ink_art_reports_bound", bool(minimap.bind_map_ink_art(ink)))
	_check("ink_art_rebakes_the_paper", minimap.radar_paper != null and bool(minimap.uses_map_ink_art))
	var inked: Image = minimap.radar_paper.get_image()
	var half := Vector2i(inked.get_size().x / 2, inked.get_size().y / 2)
	# One pixel of slack on each inside edge: the ink is bilinearly resampled to
	# the bake resolution, so the quadrant boundary is a gradient, not a cliff.
	var top_left := _mean_color(inked, Rect2i(Vector2i(2, 2), half - Vector2i(6, 6)))
	var bottom_right := _mean_color(inked, Rect2i(half + Vector2i(4, 4), half - Vector2i(8, 8)))
	_check(
		"ink_darkens_the_paper_where_it_is_opaque",
		top_left.r < bottom_right.r - 0.15 and top_left.get_luminance() < bottom_right.get_luminance(),
		"topleft=%.3f bottomright=%.3f" % [top_left.r, bottom_right.r]
	)
	_check(
		"ink_bake_keeps_the_authored_orientation",
		top_left.r < 0.45 and bottom_right.r > 0.45,
		"topleft=%.3f bottomright=%.3f" % [top_left.r, bottom_right.r]
	)

	# ------------------------------------------------------ source-grid layout
	# The paper rectangle is the PLAYABLE source grid, so the grid's corners must
	# land on the drawn rectangle's corners even though the local frame is turned
	# 35.76 deg against them.
	var arena: Rect2 = minimap._arena()
	var grid_min := Vector2(map_data.playable_grid_min)
	var grid_max := Vector2(map_data.playable_grid_max)
	var canvas_min: Vector2 = minimap._world_to_canvas(map_data.local_at_grid(grid_min), arena)
	var canvas_max: Vector2 = minimap._world_to_canvas(map_data.local_at_grid(grid_max), arena)
	var paper_origin: Vector2 = minimap._radar_to_canvas(minimap.map_bounds.position, arena)
	var paper_end: Vector2 = minimap._radar_to_canvas(minimap.map_bounds.end, arena)
	_check(
		"paper_corners_track_the_source_grid",
		canvas_min.distance_to(paper_origin) < 0.5 and canvas_max.distance_to(paper_end) < 0.5,
		"min %s vs %s / max %s vs %s" % [canvas_min, paper_origin, canvas_max, paper_end]
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
		canvas_top_right.distance_to(Vector2(paper_end.x, paper_origin.y)) < 0.5
			and canvas_bottom_left.distance_to(Vector2(paper_origin.x, paper_end.y)) < 0.5,
		"tr %s / bl %s (paper %s..%s)" % [canvas_top_right, canvas_bottom_left, paper_origin, paper_end]
	)
	# The grid centre is the centre of the paper.
	var canvas_centre: Vector2 = minimap._world_to_canvas(
		map_data.local_at_grid((grid_min + grid_max) * 0.5), arena
	)
	_check(
		"grid_centre_is_the_paper_centre",
		canvas_centre.distance_to((paper_origin + paper_end) * 0.5) < 0.5,
		"%s vs %s" % [canvas_centre, (paper_origin + paper_end) * 0.5]
	)

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
	# paper for a unit standing inside the playable grid.
	var simulation := StubSimulation.new()
	simulation.entities[1] = {
		"position": map_data.local_at_grid(Vector2(120.0, 150.0)), "team": 0, "health": 100
	}
	simulation.structures[2] = {
		"position": map_data.local_at_grid(Vector2(440.0, 460.0)), "team": 1, "health": 500
	}
	minimap.configure(simulation, map_data, ink)
	_check("configure_keeps_the_ink_art", minimap.map_ink_art == ink and bool(minimap.uses_map_ink_art))
	var paper_rect := Rect2(paper_origin, paper_end - paper_origin)
	var blip_a: Vector2 = minimap._world_to_canvas(Vector2(simulation.entities[1]["position"]), arena)
	var blip_b: Vector2 = minimap._world_to_canvas(Vector2(simulation.structures[2]["position"]), arena)
	_check(
		"blips_land_on_the_paper",
		paper_rect.has_point(blip_a) and paper_rect.has_point(blip_b),
		"a=%s b=%s rect=%s" % [blip_a, blip_b, paper_rect]
	)
	# The two probes are at opposite ends of the grid, so a mapping that collapsed
	# or mirrored would put them in the same place.
	_check("blips_are_separated_like_their_cells", blip_a.distance_to(blip_b) > RADAR_SIZE.x * 0.4)

	minimap.free()
	print("MINIMAP_PARCHMENT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)
