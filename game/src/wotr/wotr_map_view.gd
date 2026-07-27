extends Control

## THE 3D WAR OF THE RING MAP. Retail's own Middle-earth mesh under a camera,
## with the region graph drawn over it at retail's own world coordinates.
##
## HOW REGIONS LAND IN THE RIGHT PLACE - the whole point of this file:
##
## The living-world document's `centerPoint` values and the living map's vertex
## data are the SAME coordinate space. That is measured, not assumed, and the
## measurement rides in the bundle manifest where a test can read it:
##
##   * the 20 terrain tiles tile a 5x4 grid of ~1204 x ~1205 unit cells, each
##     cell occupied exactly once - so the bone transform is applied correctly;
##   * the nine landmark sub-objects (Minas Tirith, Orthanc, Helm's Deep, Erebor,
##     Dol Guldur, Rivendell, Osgiliath, the Black Gate, Cirith Ungol) sit within
##     140 world units of their region's authored centre on a map 6021 units
##     wide - so document space and map space are the same space, at scale 1.
##
## A region is therefore placed at its authored (x, y) with its HEIGHT SAMPLED
## from retail's terrain triangles at that exact point. Sampling shipped geometry
## is derivation; picking a height that looks right would be invention, and when
## no triangle covers the point this says so rather than guessing.
##
## WHAT IT REFUSES TO DRAW:
##
## * A region with no authored `centerPoint` is NOT placed. Retail derives those
##   markers from per-region mesh data that this bundle does not carry, so the
##   screen lists them separately instead of dropping them at a plausible spot.
##   BFME2 authors exactly one such region: Rhun.
## * A sub-object whose texture did not resolve is drawn flat grey, never with a
##   substitute image.
##
## PRESENTATION ONLY. The camera, the hover highlight and the zoom live in this
## Control and reach nothing. No value here is ever hashed, and the only way a
## click becomes a battle is the screen calling `session.commit_attack()`.

signal region_clicked(region_id: String)
signal region_hovered(region_id: String)

const BundleScript = preload("res://src/wotr/wotr_map_bundle.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")
const ThemeScript = preload("res://src/ui/openbfme_theme.gd")

const MARKER_RADIUS := 9.0
const PICK_SLOP := 7.0

## How far above retail's terrain the territory fills and borders are lifted so
## they do not z-fight the ground they lie on. This is RETAIL'S OWN NUMBER, not a
## tuned one: `livingworld.ini` sets `ArmyLineHeightBias = 3.0` for exactly this
## problem - "this is added to the height of each point so it doesn't conflict
## with the terrain" - and the fills are the same kind of surface.
const TERRITORY_HEIGHT_BIAS := 3.0
## The border is lifted slightly further so it draws over its own fill.
const BORDER_HEIGHT_BIAS := 4.5

## How opaque an owned territory is. Retail shades the fill and lets the terrain
## read through it; a solid fill would bury Middle-earth under flat colour.
const TERRITORY_ALPHA := 0.46
const TERRITORY_ALPHA_HOVER := 0.62
const TERRITORY_ALPHA_SELECTED := 0.74
## Retail's own neutral-region colour, from `livingworldregioneffects.ini`
## (`NeutralRegionColor = R:245 G:245 B:245`), used at a much lower alpha so an
## unclaimed region reads as unclaimed rather than as a seventh player.
const NEUTRAL_TERRITORY_ALPHA := 0.10

## Camera framing, in retail world units. The default pitch looks down the map
## the way retail's does without pretending to reproduce its exact framing.
const DEFAULT_PITCH_DEGREES := -52.0
const MIN_ZOOM := 0.35
const MAX_ZOOM := 2.6
## Breathing room around the fitted map, so the coastline is not flush with the
## panel edge. 1.0 would be an exact fit.
const FRAMING_MARGIN := 1.06

var bundle: BundleScript = null
## Retail's per-region territory geometry, when a bundle has been converted.
## Null means regions are drawn as markers only, and the screen says so.
var region_geometry: RegionGeometryScript = null
## Why there is no territory geometry, or "" when there is.
var region_geometry_reason := ""
## Why there is no 3D map, or "" when there is one. Non-empty means this view
## draws the reason instead of a map.
var unavailable_reason := ""

## Regions actually SHADED on the map this frame, and the ones the strategic
## layer knows about that no fill mesh covers. Both public so the screen can
## name the second rather than leave a silent hole in Middle-earth.
var shaded_regions: PackedStringArray = PackedStringArray()
var unshaded_regions: PackedStringArray = PackedStringArray()
## Regions placed from geometry the converter DERIVED (an area-weighted centroid
## of retail's own fill triangles) rather than from an authored `CenterPoint`.
## Reported separately because the two are different claims.
var centroid_placed_regions: PackedStringArray = PackedStringArray()

var _territory_root: Node3D = null
## `region id -> {fill: MeshInstance3D, fill_material: StandardMaterial3D,
## border: MeshInstance3D}`.
var _territory_nodes: Dictionary = {}

## Region rows as `wotr_session.region_rows()` returns them. Read, never written.
var rows: Array[Dictionary] = []
var selected_region := ""
var selected_target := ""
var hover_region := ""
var targets: PackedStringArray = PackedStringArray()
var staging: PackedStringArray = PackedStringArray()
var neighbours_by_region: Dictionary = {}
var owner_colors: Array[Color] = []
var neutral_color := Color("#5a6656")

## Regions placed on the map, and regions that could not be. Both are public so
## the screen can report the second honestly.
var placed_regions: PackedStringArray = PackedStringArray()
var unplaced_regions: PackedStringArray = PackedStringArray()
## Regions whose height could not be sampled from retail terrain. They are still
## placed - their authored x/y is real - but the screen says the height is not.
var unsampled_heights: PackedStringArray = PackedStringArray()

var viewport_container: SubViewportContainer
var viewport: SubViewport
var world_root: Node3D
var camera: Camera3D
var overlay: Control

var _world_positions: Dictionary = {}
var _screen_positions: Dictionary = {}
## Mesh instances actually put in the world by the last `_rebuild_world()`.
## Reported rather than assumed: "the map loaded" and "the map is on screen" are
## two different claims and only the second one is what the player sees.
var _drawn_count := 0
var _camera_target := Vector3.ZERO
var _camera_distance := 1.0
var _zoom := 1.0
var _yaw := 0.0
var _dragging := false


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	if viewport_container == null:
		build()


func build() -> void:
	viewport_container = SubViewportContainer.new()
	viewport_container.name = "MapViewport"
	# The viewport is sized explicitly to match this control, so the overlay's
	# 2D coordinates and the camera's projected coordinates are the SAME pixels.
	# With `stretch` on, the container would resize the viewport itself and a
	# marker could drift from the ground it is standing on.
	viewport_container.stretch = false
	viewport_container.set_anchors_preset(Control.PRESET_FULL_RECT)
	viewport_container.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(viewport_container)

	viewport = SubViewport.new()
	viewport.name = "MapWorld"
	viewport.transparent_bg = false
	viewport.handle_input_locally = false
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport_container.add_child(viewport)

	world_root = Node3D.new()
	world_root.name = "LivingMap"
	viewport.add_child(world_root)

	camera = Camera3D.new()
	camera.name = "MapCamera"
	camera.fov = 45.0
	camera.near = 1.0
	camera.far = 60000.0
	viewport.add_child(camera)

	# Retail lights the living map warmly from the south-west. This is one
	# directional light and an ambient fill - not retail's lighting rig, and not
	# claimed to be.
	var light := DirectionalLight3D.new()
	light.name = "MapSun"
	light.light_energy = 1.15
	light.light_color = Color(1.0, 0.96, 0.88)
	light.rotation_degrees = Vector3(-48.0, 38.0, 0.0)
	viewport.add_child(light)

	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.02, 0.04, 0.05)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.42, 0.44, 0.46)
	environment.ambient_light_energy = 0.85
	var camera_attributes := CameraAttributesPractical.new()
	camera.environment = environment
	camera.attributes = camera_attributes

	overlay = Control.new()
	overlay.name = "MapOverlay"
	overlay.set_anchors_preset(Control.PRESET_FULL_RECT)
	overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	overlay.draw.connect(_draw_overlay)
	add_child(overlay)

	resized.connect(_on_resized)
	_on_resized()


## Bind a loaded bundle, or none plus the reason. Both are legitimate states -
## and BOTH are said out loud. This view used to contain no print, warning or
## error of any kind, so a failure to build retail's map produced a screen that
## looked slightly wrong and a log that said nothing at all; the only way to find
## it was to notice. That is the failure this logging exists to make impossible.
func set_bundle(loaded_bundle, reason: String) -> void:
	if viewport_container == null:
		build()
	bundle = loaded_bundle
	unavailable_reason = reason
	_rebuild_world()
	_frame_camera()
	queue_redraw()
	if not has_map():
		push_warning("[WotrMap3D] no retail map bound; this view will draw its refusal instead. %s"
			% (reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		print("[WotrMap3D] NO 3D MAP. %s" % (
			reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		return
	print("[WotrMap3D] drawing %d of %d retail sub-objects (%d held back: impassable volumes, animated ambient cards, multi-stage water)" % [
		_drawn_count, bundle.sub_objects.size(), bundle.sub_objects.size() - _drawn_count])


## Bind retail's region territory geometry, or none plus the reason. Separate
## from `set_bundle` because the two bundles fail independently: retail's map can
## be present with no territory shapes converted, and that is a legitimate state
## the screen reports rather than hides.
func set_region_geometry(geometry, reason: String) -> void:
	if viewport_container == null:
		build()
	region_geometry = geometry
	region_geometry_reason = reason
	_rebuild_territories()
	_recompute_world_positions()
	_apply_territory_colors()
	queue_redraw()
	if not has_territories():
		push_warning("[WotrMap3D] no region territory geometry; regions are drawn as markers. %s"
			% (reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		print("[WotrMap3D] NO TERRITORY SHADING. %s" % (
			reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		return
	print("[WotrMap3D] territory shading from retail geometry: %d regions filled, %d bordered, %d triangles" % [
		shaded_regions.size(), _bordered_count(), region_geometry.total_triangles])
	for line in region_geometry.describe_load():
		print("[WotrMap3D]   %s" % line)


func has_map() -> bool:
	return bundle != null and bundle.loaded


func has_territories() -> bool:
	return region_geometry != null and region_geometry.loaded and not shaded_regions.is_empty()


func _bordered_count() -> int:
	var count := 0
	for key in _territory_nodes.keys():
		if (_territory_nodes[key] as Dictionary).has("border"):
			count += 1
	return count


## How many retail sub-objects are actually standing in the 3D world right now.
## `has_map()` says the bytes parsed; this says something is on screen.
func drawn_mesh_count() -> int:
	return _drawn_count


## Feed the view the strategic picture. Pure presentation: nothing here is
## written back, and the arrays are the screen's own already-computed ones.
func set_regions(
	region_rows: Array[Dictionary],
	adjacency: Dictionary,
	staged: PackedStringArray,
	attack_targets: PackedStringArray,
	selection: String,
	target: String
) -> void:
	rows = region_rows
	neighbours_by_region = adjacency
	staging = staged
	targets = attack_targets
	selected_region = selection
	selected_target = target
	_recompute_world_positions()
	_apply_territory_colors()
	queue_redraw()


func _on_resized() -> void:
	if viewport == null:
		return
	var view_size := size
	if view_size.x < 1.0 or view_size.y < 1.0:
		view_size = Vector2(1.0, 1.0)
	viewport.size = Vector2i(int(view_size.x), int(view_size.y))
	# The fit depends on the viewport's ASPECT, so a resize has to redo it. Only
	# the distance is recomputed: where the camera is looking and how far the
	# player has zoomed are his, and a resize must not throw them away.
	_fit_distance()
	_apply_camera()
	queue_redraw()


# --- the 3D world -------------------------------------------------------------

func _rebuild_world() -> void:
	if world_root == null:
		return
	_drawn_count = 0
	for child in world_root.get_children():
		world_root.remove_child(child)
		child.queue_free()
	if not has_map():
		return
	for entry in bundle.sub_objects:
		# Retail's impassable volumes were never meant to be seen, and the
		# ambient cards (smoke, cloud layer, lava planes, the text plane) are
		# animated in retail and static here. Both are LOADED and reported; they
		# are simply not drawn, which is a presentation choice this file states
		# rather than a gap it hides.
		if bool(entry["collision"]) or bool(entry["ambient"]) or bool(entry["shader_only"]):
			continue
		var instance := MeshInstance3D.new()
		instance.name = String(entry["name"])
		instance.mesh = entry["mesh"]
		instance.material_override = entry["material"]
		world_root.add_child(instance)
		_drawn_count += 1
	# The territories are rebuilt with the world, because clearing `world_root`
	# above destroyed the node that held them.
	_territory_root = null
	_territory_nodes = {}
	_rebuild_territories()


## Stand retail's per-region fill and border meshes in the world, one node per
## region. THE SHAPES ARE RETAIL'S; only the colour is this project's, and the
## colour is a presentation value that reaches nothing.
##
## A region the bundle has no fill mesh for gets NO NODE. It keeps its marker and
## is named in `unshaded_regions`, because a region silently drawn in a
## neighbour's shape would be worse than one drawn in none.
func _rebuild_territories() -> void:
	if world_root == null:
		return
	if _territory_root != null and is_instance_valid(_territory_root):
		world_root.remove_child(_territory_root)
		_territory_root.queue_free()
	_territory_root = null
	_territory_nodes = {}
	shaded_regions = PackedStringArray()
	unshaded_regions = PackedStringArray()
	if region_geometry == null or not region_geometry.loaded:
		return

	_territory_root = Node3D.new()
	_territory_root.name = "Territories"
	world_root.add_child(_territory_root)

	var region_ids: Array[String] = []
	for key in region_geometry.by_region.keys():
		region_ids.append(String(key))
	region_ids.sort()

	var shaded: Array[String] = []
	for region_id in region_ids:
		var fill_mesh: ArrayMesh = region_geometry.region_mesh(region_id, "fill")
		if fill_mesh == null:
			continue
		var slot: Dictionary = {}

		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		# The fill lies ON the terrain, so it must not write depth or it would
		# occlude the markers and the landmarks standing in it.
		material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
		material.cull_mode = BaseMaterial3D.CULL_DISABLED
		material.render_priority = 1
		material.albedo_color = neutral_color

		var fill := MeshInstance3D.new()
		fill.name = "Fill_%s" % region_id
		fill.mesh = fill_mesh
		fill.material_override = material
		fill.position = Vector3(0.0, TERRITORY_HEIGHT_BIAS, 0.0)
		_territory_root.add_child(fill)
		slot["fill"] = fill
		slot["fill_material"] = material

		var border_mesh: ArrayMesh = region_geometry.region_mesh(region_id, "border")
		if border_mesh != null:
			var border_material := StandardMaterial3D.new()
			border_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			border_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			border_material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
			border_material.cull_mode = BaseMaterial3D.CULL_DISABLED
			border_material.render_priority = 2
			# RETAIL'S OWN BORDER COLOUR: `livingworldregioneffects.ini` sets
			# `RegionBorderColor = R:30 G:6 B:6`, which the living-world document
			# carries through as `regionEffects[].colors.regionBorder`.
			border_material.albedo_color = Color8(30, 6, 6, 235)
			var border := MeshInstance3D.new()
			border.name = "Border_%s" % region_id
			border.mesh = border_mesh
			border.material_override = border_material
			border.position = Vector3(0.0, BORDER_HEIGHT_BIAS, 0.0)
			_territory_root.add_child(border)
			slot["border"] = border
			slot["border_material"] = border_material

		_territory_nodes[region_id] = slot
		shaded.append(region_id)
	shaded.sort()
	shaded_regions = PackedStringArray(shaded)


## Push ownership onto the territory materials. Pure presentation, run whenever
## the strategic picture changes; nothing here is read back.
func _apply_territory_colors() -> void:
	if _territory_nodes.is_empty():
		return
	var owner_by_region: Dictionary = {}
	for row in rows:
		owner_by_region[String(row["id"])] = int(row["owner"])
	var missing: Array[String] = []
	for row in rows:
		var region_id := String(row["id"])
		if not _territory_nodes.has(region_id):
			missing.append(region_id)
			continue
		var slot := _territory_nodes[region_id] as Dictionary
		var material := slot["fill_material"] as StandardMaterial3D
		var owner := int(row["owner"])
		var color := _color_of(owner)
		var alpha := TERRITORY_ALPHA if owner >= 0 and owner < owner_colors.size() else NEUTRAL_TERRITORY_ALPHA
		if region_id == selected_region or region_id == selected_target:
			alpha = TERRITORY_ALPHA_SELECTED
		elif region_id == hover_region:
			alpha = TERRITORY_ALPHA_HOVER
		elif Array(targets).has(region_id):
			alpha = TERRITORY_ALPHA_HOVER
		color.a = alpha
		material.albedo_color = color
	missing.sort()
	unshaded_regions = PackedStringArray(missing)


func _frame_camera() -> void:
	if not has_map():
		return
	var extent: Dictionary = bundle.terrain_extent
	var x_min := float(extent["x_min"])
	var x_max := float(extent["x_max"])
	var y_min := float(extent["y_min"])
	var y_max := float(extent["y_max"])
	_camera_target = BundleScript.world_to_godot(
		(x_min + x_max) * 0.5, (y_min + y_max) * 0.5,
		(float(extent["z_min"]) + float(extent["z_max"])) * 0.5)
	_fit_distance()
	_apply_camera()


## Fit retail's whole map into the viewport it is actually being drawn in.
##
## THE FRAMING THIS REPLACES fitted the map's LONGER axis into the camera's
## VERTICAL field of view. Two things were wrong with that and they compounded:
## the strategic viewport is wide (it ran 1240x548, aspect 2.26), so the vertical
## field is the tight one and fitting the long axis to it wastes the width; and
## the map is looked at down a -52 degree pitch, which foreshortens its depth to
## sin(52) = 0.79 of itself before it reaches the screen. The result was retail's
## Middle-earth drawn about a quarter of the size of the panel holding it,
## floating in a black field - which is exactly what "the 3D bit is not 3D" looks
## like from the outside.
##
## This fits BOTH axes: the width against the horizontal field derived from the
## viewport's own aspect, the pitched depth-plus-relief against the vertical
## field, and takes whichever needs the camera further back.
func _fit_distance() -> void:
	if camera == null or not has_map():
		return
	var extent: Dictionary = bundle.terrain_extent
	var width := float(extent["x_max"]) - float(extent["x_min"])
	var depth := float(extent["y_max"]) - float(extent["y_min"])
	var relief := float(extent["z_max"]) - float(extent["z_min"])
	var pitch := absf(deg_to_rad(DEFAULT_PITCH_DEGREES))
	# What the map actually occupies vertically on screen at this pitch.
	var vertical_span := depth * sin(pitch) + relief * cos(pitch)
	var aspect := 16.0 / 9.0
	if viewport != null and viewport.size.y > 0:
		aspect = float(viewport.size.x) / float(viewport.size.y)
	# Godot's Camera3D keeps the VERTICAL field, so `fov` is the vertical one and
	# the horizontal follows the aspect.
	var half_vertical := tan(deg_to_rad(camera.fov * 0.5))
	var half_horizontal := half_vertical * aspect
	var for_width := (width * 0.5) / maxf(half_horizontal, 0.0001)
	var for_depth := (vertical_span * 0.5) / maxf(half_vertical, 0.0001)
	_camera_distance = maxf(for_width, for_depth) * FRAMING_MARGIN


func _apply_camera() -> void:
	if camera == null:
		return
	var pitch := deg_to_rad(DEFAULT_PITCH_DEGREES)
	var distance := _camera_distance * _zoom
	var offset := Vector3(
		sin(_yaw) * cos(pitch), -sin(pitch), cos(_yaw) * cos(pitch)) * distance
	# `look_at_from_position` rather than `look_at`, because this runs before the
	# view is inside a tree when a test drives it directly and `look_at` requires
	# a global transform.
	camera.look_at_from_position(_camera_target + offset, _camera_target, Vector3.UP)


# --- region placement ---------------------------------------------------------

func _recompute_world_positions() -> void:
	_world_positions = {}
	var placed: Array[String] = []
	var unplaced: Array[String] = []
	var unsampled: Array[String] = []
	var from_centroid: Array[String] = []
	for row in rows:
		var region_id := String(row["id"])
		var authored := row["position"] as Vector2
		if not bool(row["has_position"]):
			# Retail leaves `CustomCenterPoint` off for a handful of regions and
			# derives the marker from the region's OWN MESH. `livingmap.w3d`
			# carries no such mesh, which is why these used to be listed as
			# unplaceable - but `lmr_fill.w3d` does, and the converter computes an
			# area-weighted centroid of retail's own triangles for every region in
			# it. That is derivation from shipped geometry, so it may be used; it
			# is recorded separately from an authored point so the screen can say
			# which of the two a marker is standing on.
			if region_geometry != null and region_geometry.derived_centroids.has(region_id):
				authored = region_geometry.derived_centroids[region_id] as Vector2
				from_centroid.append(region_id)
			else:
				unplaced.append(region_id)
				continue
		var height := float(bundle.terrain_extent.get("z_max", 0.0)) if has_map() else 0.0
		if has_map():
			var sampled: Dictionary = bundle.sample_height(authored.x, authored.y)
			if bool(sampled["ok"]):
				height = float(sampled["height"])
			else:
				unsampled.append(region_id)
		_world_positions[region_id] = BundleScript.world_to_godot(
			authored.x, authored.y, height)
		placed.append(region_id)
	placed.sort()
	unplaced.sort()
	unsampled.sort()
	from_centroid.sort()
	placed_regions = PackedStringArray(placed)
	unplaced_regions = PackedStringArray(unplaced)
	unsampled_heights = PackedStringArray(unsampled)
	centroid_placed_regions = PackedStringArray(from_centroid)


## Where each placed region lands on screen this frame. Recomputed from the
## camera every draw; it is a projection, never stored state.
func _project_positions() -> void:
	_screen_positions = {}
	if camera == null or not has_map():
		return
	for key in _world_positions.keys():
		var world_position: Vector3 = _world_positions[key]
		if camera.is_position_behind(world_position):
			continue
		_screen_positions[String(key)] = camera.unproject_position(world_position)


# --- overlay ------------------------------------------------------------------

func _draw_overlay() -> void:
	if not has_map():
		overlay.draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.05, 0.03, 0.85))
		var font := get_theme_default_font()
		if font != null and not unavailable_reason.is_empty():
			var text := "RETAIL 3D MAP UNAVAILABLE\n\n%s" % unavailable_reason
			overlay.draw_multiline_string(
				font, Vector2(28.0, 48.0), text, HORIZONTAL_ALIGNMENT_LEFT,
				size.x - 56.0, 15, -1, ThemeScript.GOLD)
		return

	_project_positions()

	# Region graph edges, drawn under the markers.
	for region_id in _screen_positions.keys():
		var from_point: Vector2 = _screen_positions[region_id]
		for neighbour_value in neighbours_by_region.get(region_id, PackedStringArray()):
			var neighbour := String(neighbour_value)
			if not _screen_positions.has(neighbour):
				continue
			if neighbour < String(region_id):
				continue
			overlay.draw_line(
				from_point, _screen_positions[neighbour],
				Color(0.92, 0.86, 0.66, 0.32), 1.5)

	var font := get_theme_default_font()
	for row in rows:
		var region_id := String(row["id"])
		if not _screen_positions.has(region_id):
			continue
		var point: Vector2 = _screen_positions[region_id]
		var color := _color_of(int(row["owner"]))
		var has_army := int(row["armies"]) > 0
		# ONCE THE TERRITORY IS SHADED, the marker is no longer how ownership is
		# read - the fill is - so it shrinks to what it actually still says: that
		# an army stands here. A region with no fill mesh keeps its full-size
		# marker, because for that region the marker is the only thing carrying
		# ownership at all.
		var shaded := _territory_nodes.has(region_id)
		var radius := MARKER_RADIUS + (2.0 if has_army else 0.0)
		if shaded:
			radius = (MARKER_RADIUS * 0.62) if has_army else (MARKER_RADIUS * 0.34)
		overlay.draw_circle(point, radius + 2.0, Color(0.03, 0.05, 0.03, 0.85))
		overlay.draw_circle(point, radius, color)
		if region_id == selected_region:
			overlay.draw_arc(point, radius + 6.0, 0.0, TAU, 28, ThemeScript.GOLD_BRIGHT, 3.0)
		elif Array(targets).has(region_id):
			overlay.draw_arc(point, radius + 6.0, 0.0, TAU, 28, Color("#c8483f"), 2.0)
		elif Array(staging).has(region_id):
			overlay.draw_arc(point, radius + 4.0, 0.0, TAU, 28, Color(0.85, 0.92, 0.75, 0.6), 1.5)
		if region_id == selected_target:
			overlay.draw_arc(point, radius + 10.0, 0.0, TAU, 28, Color("#e8623f"), 3.0)
		if region_id == hover_region:
			overlay.draw_arc(point, radius + 13.0, 0.0, TAU, 28, Color(1.0, 1.0, 1.0, 0.55), 1.5)
		if font != null:
			var label := region_id
			if int(row["armies"]) > 0:
				label += "  x%d" % int(row["armies"])
			var label_at := point + Vector2(radius + 5.0, 4.0)
			overlay.draw_string(font, label_at + Vector2(1.0, 1.0), label,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color(0.0, 0.0, 0.0, 0.85))
			overlay.draw_string(font, label_at, label,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, ThemeScript.TEXT_LEAF)


func _color_of(owner: int) -> Color:
	if owner < 0 or owner >= owner_colors.size():
		return neutral_color
	return owner_colors[owner]


# --- input --------------------------------------------------------------------

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		var motion := event as InputEventMouseMotion
		if _dragging:
			_pan(motion.relative)
			return
		var hovered := region_at(motion.position)
		if hovered != hover_region:
			hover_region = hovered
			region_hovered.emit(hovered)
			_apply_territory_colors()
			queue_redraw()
		return
	if not (event is InputEventMouseButton):
		return
	var button := event as InputEventMouseButton
	match button.button_index:
		MOUSE_BUTTON_WHEEL_UP:
			if button.pressed:
				_set_zoom(_zoom / 1.12)
		MOUSE_BUTTON_WHEEL_DOWN:
			if button.pressed:
				_set_zoom(_zoom * 1.12)
		MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE:
			_dragging = button.pressed
		MOUSE_BUTTON_LEFT:
			if not button.pressed:
				return
			var region_id := region_at(button.position)
			if not region_id.is_empty():
				region_clicked.emit(region_id)


## The region under a point in this control's space, or "". Deterministic: ties
## are broken by sorted region id, never by iteration order.
func region_at(point: Vector2) -> String:
	if _screen_positions.is_empty():
		_project_positions()
	var best := ""
	var best_distance := MARKER_RADIUS + PICK_SLOP
	var ids: Array[String] = []
	for key in _screen_positions.keys():
		ids.append(String(key))
	ids.sort()
	for region_id in ids:
		var distance := (point - (_screen_positions[region_id] as Vector2)).length()
		if distance <= best_distance:
			best = region_id
			best_distance = distance
	return best


func _set_zoom(value: float) -> void:
	_zoom = clampf(value, MIN_ZOOM, MAX_ZOOM)
	_apply_camera()
	queue_redraw()


func _pan(relative: Vector2) -> void:
	if not has_map():
		return
	var scale := _camera_distance * _zoom * 0.0016
	# Local basis, not global: the camera's parent viewport sits at the origin,
	# and `global_transform` would require the view to be inside a tree.
	var right := camera.transform.basis.x
	var forward := -camera.transform.basis.z
	forward.y = 0.0
	if forward.length() > 0.0001:
		forward = forward.normalized()
	_camera_target -= right * relative.x * scale
	_camera_target += forward * relative.y * scale
	_apply_camera()
	queue_redraw()


## Reset the camera to the framing the view opens with. Presentation only.
func reset_camera() -> void:
	_zoom = 1.0
	_yaw = 0.0
	_frame_camera()
	queue_redraw()
