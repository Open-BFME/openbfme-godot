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
## A build plot on the map was clicked. `index` is the plot's position in the
## region's own authored `BuildingSpot` list, so the screen can name it exactly.
signal plot_clicked(region_id: String, index: int)
## Emitted after the overlay has actually painted. The counters this view reports
## - banners drawn, labels drawn, labels held back - only exist AFTER the paint,
## so a mode line built during `refresh()` reported the previous frame's numbers
## and said "0 banners" over a map with six banners on it. The screen re-reads
## them here, and only them.
signal overlay_painted

const BundleScript = preload("res://src/wotr/wotr_map_bundle.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")
const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const ChromeScript = preload("res://src/wotr/wotr_chrome.gd")

const MARKER_RADIUS := 9.0
const PICK_SLOP := 7.0

## The army banner. Retail draws a stack as a standard carrying a portrait; this
## is a portrait plate on a staff in the owner's colour, at retail's own map
## position. The size is a presentation choice and reaches nothing.
const BANNER_WIDTH := 34.0
const BANNER_HEIGHT := 34.0
const BANNER_STAFF := 15.0
## How far apart stacked banners are fanned when several armies share a region.
## Wider than the banner itself, so two stacks in one region are two readable
## portraits rather than one portrait with an edge behind it.
const BANNER_FAN := 38.0
## At most this many banners are drawn per region; the rest are counted in the
## "+N" tail rather than piled into an unreadable heap.
const MAX_BANNERS_PER_REGION := 3

## Build-plot markers: retail decals a plot with a faction foundation model
## (`LMGFoundation` and its six siblings). No model is converted, so this draws a
## flat ring AT RETAIL'S OWN AUTHORED PLOT COORDINATE and says what it stands in
## for.
const PLOT_RADIUS := 9.0
const PLOT_PICK_SLOP := 6.0

## The radial build menu. Retail rings a selected plot with the structures that
## can go on it; the ring radius and icon size are presentation.
const RADIAL_RADIUS := 78.0
const RADIAL_ICON := 46.0

## Label placement. A label is only drawn when its box does not overlap one
## already placed, so a dense corner of the map shows the regions that matter
## rather than an unreadable pile.
const LABEL_FONT_SIZE := 13
const LABEL_PADDING := Vector2(6.0, 3.0)

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

## ZOOM RANGE, and where the two ends came from. `_zoom` multiplies the distance
## that fits the WHOLE MAP, so 1.0 is "all of Middle-earth" by construction.
##
## The old range was 0.35..2.6 - under 8x of travel, which is a nudge. Retail
## lets you go from reading a banner to seeing the whole board, and the owner
## asked for exactly that ("zoom way in and out like in a regular skirmish
## match").
##
## MIN 0.04 is the useful floor, not an arbitrary one: the fitted distance is
## about 7,000 world units, so 0.04 puts the camera ~280 units from its target
## on a map whose terrain tiles are ~1,200 units across - a fifth of one tile
## fills the panel, which is closer than the compiled terrain textures have detail
## for. Going further only magnifies texels. The camera's near plane is 1.0, so
## there is no clipping at that distance.
##
## MAX 1.35 is the ceiling because 1.0 already frames the entire map with a 6%
## margin; past ~1.35 the map is a small object in a large black field, which is
## the exact defect commit a1e7b2e removed. The extra third is there so the
## player can pull back off a corner he has panned to.
const MIN_ZOOM := 0.04
const MAX_ZOOM := 1.35
## One wheel notch. Bigger than the old 1.12 because the range is now ~34x and
## crossing it a 12% step at a time would take 31 notches.
const ZOOM_STEP := 1.22

## PITCH RANGE. Retail's strategic camera looks down at a fixed angle; this one
## is adjustable, from nearly overhead to a low oblique that shows the relief of
## the Misty Mountains. Not past -6 degrees: below that the camera is inside the
## terrain's own silhouette and the map folds into a line.
const MIN_PITCH_DEGREES := -88.0
const MAX_PITCH_DEGREES := -8.0
## Degrees per pixel of vertical drag while orbiting.
const PITCH_PER_PIXEL := 0.35
## Radians per pixel of horizontal drag while orbiting.
const YAW_PER_PIXEL := 0.006

## How far outside the map's own footprint the camera target may be dragged. Pan
## used to be unbounded, so one long drag put Middle-earth off the panel with no
## way back but a reset. A quarter of the map's span is enough to put a corner
## region in the middle of the panel and no more.
const PAN_MARGIN_FRACTION := 0.25
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

## RETAIL'S UI SURFACE - the atlas crops behind every icon - or null. Null means
## banners are drawn as plain plates in the owner's colour and the screen says
## which portraits are absent.
var ui = null
## Why there is no UI bundle, or "" when there is one.
var ui_reason := ""

## `region id -> Array[Dictionary]` of the army stacks standing there, already
## resolved by the screen: `{owner, kind, label, portrait_id, portrait_source}`.
## Read, never written.
var armies_by_region: Dictionary = {}
## `region id -> Array[Vector2]`, retail's own authored `BuildingSpot` points.
var plots_by_region: Dictionary = {}
## `region id -> String`, retail's English name when the string table converted.
var display_names: Dictionary = {}
## The plot the radial build menu is open on: `{region, index}` or `{}`.
var selected_plot: Dictionary = {}
## What that menu offers: `[{id, image_id, title, cost, turns}]`, supplied by the
## screen from retail's own `LivingWorldBuilding` records.
var radial_entries: Array[Dictionary] = []

## Army stacks whose portrait did not resolve, `army label -> reason`. Public so
## the screen can name every banner drawn as a bare faction plate.
var banners_without_portrait: Dictionary = {}
## How many banners were drawn, and how many region labels were held back because
## they collided with one already placed. Both reported rather than assumed.
var banners_drawn := 0
var labels_drawn := 0
var labels_suppressed := 0

var _world_positions: Dictionary = {}
var _screen_positions: Dictionary = {}
## `region id -> Array[Vector2]` in screen space, recomputed every draw.
var _plot_screen_positions: Dictionary = {}
## Screen-space rectangles the banners occupy this frame. Labels are placed
## AROUND them: a region name written across a portrait costs both.
var _banner_boxes: Array[Rect2] = []
## Mesh instances actually put in the world by the last `_rebuild_world()`.
## Reported rather than assumed: "the map loaded" and "the map is on screen" are
## two different claims and only the second one is what the player sees.
var _drawn_count := 0
var _camera_target := Vector3.ZERO
var _camera_distance := 1.0
var _zoom := 1.0
var _yaw := 0.0
## The live pitch. `DEFAULT_PITCH_DEGREES` is now only the value this opens at
## and returns to on reset; the player owns it after that, and `_fit_distance()`
## fits against THIS rather than the constant so the both-axes fit stays correct
## at any angle.
var _pitch_degrees := DEFAULT_PITCH_DEGREES
var _dragging := false
## True while the drag is orbiting rather than panning.
var _orbiting := false


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
	# CLIPPED TO THE MAP PANEL. A region whose marker projects outside the
	# viewport is still a legitimate projection - the camera can be panned or
	# zoomed so that half of Middle-earth is off the panel - but the label and the
	# graph edge that go with it must not be painted across the rest of the
	# screen. Without this, panning south wrote Mordor, Ithilien and Belfalas over
	# the seat table and the buttons.
	overlay.clip_contents = true
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
	_redraw()
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
	_redraw()
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
	_redraw()


## Bind retail's UI surface, or none plus the reason. Separate from the map and
## the territory bundles because it fails independently: Middle-earth can be on
## screen, shaded, with no portrait atlas converted, and that is a state the view
## reports rather than papers over.
func set_ui(loaded_ui, reason: String) -> void:
	ui = loaded_ui
	ui_reason = reason
	_redraw()
	if not has_ui():
		push_warning("[WotrMap3D] no living-world UI bundle; army banners carry no portraits. %s"
			% (reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		print("[WotrMap3D] NO PORTRAITS. %s" % (
			reason if not reason.is_empty() else "no reason was supplied, which is itself a defect"))
		return
	for line in ui.describe_load():
		print("[WotrMap3D]   ui: %s" % line)


func has_ui() -> bool:
	return ui != null and ui.loaded


## Feed the view the army stacks, the build plots and the region labels. All
## three are already resolved by the screen; nothing here reads the simulation.
func set_overlays(
	army_rows: Dictionary,
	plot_rows: Dictionary,
	labels: Dictionary,
	plot_selection: Dictionary,
	menu_entries: Array[Dictionary]
) -> void:
	armies_by_region = army_rows
	plots_by_region = plot_rows
	display_names = labels
	selected_plot = plot_selection
	radial_entries = menu_entries
	_redraw()


## Ask for a repaint. THE OVERLAY IS THE THING THAT DRAWS, and it is a CHILD
## Control with its own `draw` signal - so `queue_redraw()` on this node marked
## a node that paints nothing and the markers, rings and labels only ever
## appeared on the very first frame. Every selection, hover and territory change
## after that redrew nothing at all. Every request goes through here now, so the
## two can never drift apart again.
func _redraw() -> void:
	queue_redraw()
	if overlay != null:
		overlay.queue_redraw()


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
	_redraw()


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
	# THE LIVE PITCH, not the constant. The fit's whole point is that the map
	# occupies the panel at the angle it is actually being looked at; fitting
	# against a fixed -52 while the player is at -12 would frame a map nobody is
	# looking at. Both properties commit a1e7b2e established are preserved: both
	# axes are still fitted against the viewport's own aspect, and this function
	# still writes ONLY `_camera_distance`, so a resize re-fits without touching
	# the player's pan, zoom, yaw or pitch.
	var pitch := absf(deg_to_rad(_pitch_degrees))
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
	var pitch := deg_to_rad(_pitch_degrees)
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

## HOW BIG THE OVERLAY DRAWS ITSELF AT THIS ZOOM.
##
## Everything the overlay paints - banners, plot rings, the build ring, labels -
## is in SCREEN space, so without this it would stay the same pixel size while
## the world under it grew 34x. A banner that is a postage stamp over the whole
## map and still a postage stamp when one region fills the panel reads as a
## sticker on the glass rather than as a standard planted in a country.
##
## `_zoom` multiplies the whole-map distance, so it is SMALL when close in. The
## square root keeps the growth gentle - at the deepest zoom the world is 34x
## nearer and the banner only 2.4x bigger - and both ends are clamped so nothing
## grows without bound or shrinks into invisibility.
func _view_scale() -> float:
	return clampf(sqrt(1.0 / maxf(_zoom, 0.0001)), 0.85, 2.4)


## The label size at this zoom, on a tighter range than the marks: type that
## doubles is unreadable long before it is too small.
func _label_font_size() -> int:
	return int(round(float(LABEL_FONT_SIZE) * clampf(_view_scale(), 0.85, 1.45)))


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
		# read - the fill is - and once the ARMY BANNERS are drawn it is no longer
		# how an army is read either. So where both are present it shrinks to a
		# selection anchor. A region with no fill mesh keeps its full-size marker,
		# because for that region the marker is the only thing carrying ownership.
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

	_draw_build_plots()
	_draw_army_banners(font)
	_draw_region_labels(font)
	_draw_radial_menu(font)
	overlay_painted.emit()


# --- build plots ----------------------------------------------------------------

## Retail's own authored `BuildingSpot` points, drawn where retail authored them.
##
## Retail decals a plot with a faction foundation model - `LMGFoundation` for Men
## and one sibling per faction, seven in all, every one of them present in the
## archives. NONE IS CONVERTED, so this draws a flat ring instead and the screen
## names the model it is standing in for. The COORDINATE is retail's; only the
## ring is this project's.
##
## Plots are drawn only for the region under the pointer and the region selected.
## Retail shows them on the territory you are looking at, and drawing 150 regions
## worth at once would bury Middle-earth under rings.
## Which regions show their plots this frame. Retail shows them on the territory
## you are looking at; drawing 150 regions worth at once would bury Middle-earth
## under rings. Deterministic and total.
func plot_regions() -> PackedStringArray:
	var wanted: Array[String] = []
	for candidate in [selected_region, hover_region, selected_target,
			String(selected_plot.get("region", ""))]:
		var region_id := String(candidate)
		if region_id.is_empty() or wanted.has(region_id):
			continue
		if (plots_by_region.get(region_id, []) as Array).is_empty():
			continue
		wanted.append(region_id)
	return PackedStringArray(wanted)


## Project every shown plot into screen space. SEPARATE FROM THE DRAWING on
## purpose: a headless test can call this and assert that retail's authored plot
## points land on the map, which no test could do if the arithmetic only existed
## inside a `_draw` callback.
func project_plots() -> Dictionary:
	var projected: Dictionary = {}
	if camera == null or not has_map():
		return projected
	for region_id in plot_regions():
		var spots: Array = plots_by_region.get(region_id, []) as Array
		var points: Array[Vector2] = []
		for spot_value in spots:
			var spot := spot_value as Vector2
			var height := float(bundle.terrain_extent.get("z_max", 0.0))
			var sampled: Dictionary = bundle.sample_height(spot.x, spot.y)
			if bool(sampled["ok"]):
				height = float(sampled["height"])
			var world := BundleScript.world_to_godot(spot.x, spot.y, height)
			if camera.is_position_behind(world):
				# BEHIND THE CAMERA is not "at the origin". A sentinel far outside
				# any viewport keeps the index aligned with retail's authored plot
				# order without putting a pickable marker in the corner.
				points.append(Vector2(-100000.0, -100000.0))
				continue
			points.append(camera.unproject_position(world))
		projected[region_id] = points
	return projected


## The ring's radius at this zoom, so the icons stay clear of the plot marker and
## grow with everything else the overlay paints.
func radial_radius() -> float:
	return RADIAL_RADIUS * _view_scale()


## The screen boxes the radial ring occupies, given the plot it is open on.
## Also separate from the drawing, and for the same reason.
func radial_slots() -> Array[Dictionary]:
	var slots: Array[Dictionary] = []
	if radial_entries.is_empty() or selected_plot.is_empty():
		return slots
	var region_id := String(selected_plot.get("region", ""))
	var index := int(selected_plot.get("index", -1))
	var points: Array = _plot_screen_positions.get(region_id, []) as Array
	if index < 0 or index >= points.size():
		return slots
	var centre: Vector2 = points[index]
	var count := radial_entries.size()
	for slot in range(count):
		# Straight up first, then clockwise - the reading order of retail's own
		# ring, and fixed rather than dependent on how many entries there are.
		var angle := -PI * 0.5 + TAU * float(slot) / float(count)
		var at := centre + Vector2(cos(angle), sin(angle)) * radial_radius()
		var icon := RADIAL_ICON * _view_scale()
		slots.append({
			"box": Rect2(at - Vector2(icon * 0.5, icon * 0.5), Vector2(icon, icon)),
			"entry": radial_entries[slot],
		})
	return slots


func _draw_build_plots() -> void:
	_plot_screen_positions = project_plots()
	var open_region := String(selected_plot.get("region", ""))
	var open_index := int(selected_plot.get("index", -1))
	var region_ids: Array[String] = []
	for key in _plot_screen_positions.keys():
		region_ids.append(String(key))
	region_ids.sort()
	var scale := _view_scale()
	var radius := PLOT_RADIUS * scale
	for region_id in region_ids:
		var points: Array = _plot_screen_positions[region_id] as Array
		for index in range(points.size()):
			var at := points[index] as Vector2
			if at.x < -1000.0:
				continue
			var is_open := open_region == region_id and open_index == index
			var ring := ThemeScript.GOLD_BRIGHT if is_open else Color(0.95, 0.88, 0.62, 0.85)
			overlay.draw_circle(at, radius + 2.5, Color(0.03, 0.05, 0.03, 0.8))
			overlay.draw_arc(at, radius, 0.0, TAU, 24, ring, 2.5 if is_open else 2.0)
			# Retail's foundation decal is a square pad; two crossed marks read as
			# "a plot" without pretending to be the model this stands in for.
			var tick := radius * 0.45
			overlay.draw_line(at + Vector2(-tick, 0.0), at + Vector2(tick, 0.0), ring, 1.5)
			overlay.draw_line(at + Vector2(0.0, -tick), at + Vector2(0.0, tick), ring, 1.5)


# --- army banners ----------------------------------------------------------------

## ONE BANNER PER ARMY STACK, carrying retail's own portrait for that army.
##
## The portrait is an atlas crop resolved through retail's own authored links -
## the recruit button for that `PlayerArmy`, the one for that `HeroTemplateName`,
## or the owning template's `GarrisonSelectionPortraitName`. A stack whose
## portrait did NOT resolve gets a plate in the owner's colour with no image on
## it at all, and is named in `banners_without_portrait`. Nothing is substituted.
func _draw_army_banners(font: Font) -> void:
	banners_drawn = 0
	banners_without_portrait = {}
	_banner_boxes = []
	if armies_by_region.is_empty():
		return
	var region_ids: Array[String] = []
	for key in armies_by_region.keys():
		region_ids.append(String(key))
	region_ids.sort()
	var scale := _view_scale()
	var fan := BANNER_FAN * scale
	for region_id in region_ids:
		if not _screen_positions.has(region_id):
			continue
		var stacks: Array = armies_by_region[region_id] as Array
		if stacks.is_empty():
			continue
		var anchor: Vector2 = _screen_positions[region_id]
		var shown: int = mini(stacks.size(), MAX_BANNERS_PER_REGION)
		var span := float(shown - 1) * fan
		for index in range(shown):
			var stack := stacks[index] as Dictionary
			var at := anchor + Vector2(
				float(index) * fan - span * 0.5,
				-(BANNER_HEIGHT * scale * 0.5 + BANNER_STAFF * scale))
			_draw_one_banner(at, stack, font, scale)
			banners_drawn += 1
		if stacks.size() > shown and font != null:
			var tail := anchor + Vector2(
				span * 0.5 + BANNER_WIDTH * scale * 0.5 + 4.0, -BANNER_STAFF * scale)
			overlay.draw_string(font, tail, "+%d" % (stacks.size() - shown),
				HORIZONTAL_ALIGNMENT_LEFT, -1, 12, ThemeScript.PARCHMENT_DIM)


func _draw_one_banner(at: Vector2, stack: Dictionary, font: Font, scale: float) -> void:
	var owner_color := _color_of(int(stack.get("owner", -1)))
	var width := BANNER_WIDTH * scale
	var height := BANNER_HEIGHT * scale
	var plate := Rect2(at - Vector2(width * 0.5, height * 0.5), Vector2(width, height))
	_banner_boxes.append(plate.grow(3.0))
	# The staff, so a banner reads as standing ON the region rather than floating.
	var staff_length := BANNER_STAFF * scale
	overlay.draw_line(
		Vector2(at.x, plate.position.y + height),
		Vector2(at.x, plate.position.y + height + staff_length),
		Color(0.16, 0.13, 0.09, 0.95), maxf(2.0 * scale, 2.0))
	# RETAIL'S OWN FACTION STANDARD, from `reinforcementbanners_001.dds`, flown off
	# the staff. It is bound to the seat by a DERIVED correspondence rather than an
	# authored field - the converter emits it only when `Faction<X> -> Banner_<X>`
	# is a total bijection over retail's seven playable factions - and the screen
	# says so. It is NOT a substitute for a missing portrait: a banner with no
	# portrait still shows an empty plate and still gets named.
	var standard: Texture2D = null
	if has_ui():
		standard = ui.faction_banner(String(stack.get("template", "")))
	if standard != null:
		var pennant_height := staff_length * 0.9
		var pennant := Rect2(
			Vector2(at.x + 1.0, plate.position.y + height + 1.0),
			Vector2(pennant_height * 1.33, pennant_height))
		overlay.draw_texture_rect(standard, pennant, false)
	overlay.draw_rect(plate.grow(2.0), Color(0.05, 0.06, 0.05, 0.92))
	var portrait_id := String(stack.get("portrait_id", ""))
	var portrait: Texture2D = null
	if has_ui() and not portrait_id.is_empty():
		portrait = ui.image(portrait_id)
	if portrait != null:
		overlay.draw_texture_rect(portrait, plate, false)
	else:
		# NOT A STAND-IN PORTRAIT. A flat plate in the owner's colour, with the
		# faction initial, which is visibly not retail art - and the id that did
		# not resolve is recorded so the screen can name it.
		overlay.draw_rect(plate, Color(owner_color.r, owner_color.g, owner_color.b, 0.55))
		var label := String(stack.get("label", "?"))
		banners_without_portrait[label] = (
			("retail authors no portrait for this army in the living-world data"
				if portrait_id.is_empty()
				else "the id %s did not resolve to an atlas crop" % portrait_id))
		if font != null:
			var initial := label.substr(0, 1).to_upper()
			var glyph := int(round(18.0 * scale))
			var text_width := font.get_string_size(initial, HORIZONTAL_ALIGNMENT_LEFT, -1, glyph).x
			overlay.draw_string(font, plate.position + Vector2(
				(width - text_width) * 0.5, height * 0.5 + float(glyph) * 0.34),
				initial, HORIZONTAL_ALIGNMENT_LEFT, -1, glyph, Color(1, 1, 1, 0.85))
	# The owner's colour rides the frame whether or not the portrait resolved, so
	# whose banner it is never depends on recognising the face.
	overlay.draw_rect(plate, owner_color, false, 2.0)
	if String(stack.get("kind", "")) == "hero":
		overlay.draw_rect(plate.grow(3.0), ThemeScript.GOLD_BRIGHT, false, 1.0)


# --- region labels ----------------------------------------------------------------

## RETAIL'S NAMES, PLACED SO THEY CAN BE READ.
##
## Every region used to draw its label unconditionally, which piles Arnor,
## Ettenmoors and Fornost on top of each other in the north-west and makes all
## three unreadable. Labels are now placed in PRIORITY ORDER and a label whose box
## overlaps one already placed is HELD BACK and counted, rather than drawn into
## the pile. The priority is what the player is actually doing: the selection and
## its target first, then the pointer, then this seat's staging regions and the
## regions it can attack, then everything else by region id so the result is
## deterministic and does not flicker between frames.
func _draw_region_labels(font: Font) -> void:
	labels_drawn = 0
	labels_suppressed = 0
	if font == null:
		return
	var font_size := _label_font_size()
	var ordered := _label_order()
	# Seeded with the banners, so a name is never written across a portrait. The
	# banners were drawn first for exactly this reason.
	var placed: Array[Rect2] = _banner_boxes.duplicate()
	for row in ordered:
		var region_id := String(row["id"])
		if not _screen_positions.has(region_id):
			continue
		var point: Vector2 = _screen_positions[region_id]
		var text := String(display_names.get(region_id, region_id))
		var armies := int(row["armies"])
		if armies > 0:
			text += "  x%d" % armies
		var measured := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size)
		# Four places a label may sit, tried in order: right of the marker, left of
		# it, below it, above it. Trying alternates before giving up is what turns
		# "held back" from a common outcome into a rare one, and the order is fixed
		# so a label does not hop between frames.
		var offsets: Array[Vector2] = [
			Vector2(MARKER_RADIUS + 6.0, 4.0),
			Vector2(-(MARKER_RADIUS + 6.0 + measured.x), 4.0),
			Vector2(-measured.x * 0.5, MARKER_RADIUS + measured.y + 2.0),
			Vector2(-measured.x * 0.5, -(MARKER_RADIUS + 6.0)),
		]
		# THE ONES THE PLAYER IS ACTING ON ARE NEVER HELD BACK. A selection whose
		# name vanished because a neighbour got there first would be worse than
		# the overlap this whole function exists to remove.
		var forced := region_id == selected_region or region_id == selected_target or region_id == hover_region
		var origin := point + offsets[0]
		var box := Rect2()
		var found := false
		for offset in offsets:
			origin = point + offset
			box = Rect2(origin - Vector2(0.0, measured.y * 0.8), measured).grow_individual(
				LABEL_PADDING.x, LABEL_PADDING.y, LABEL_PADDING.x, LABEL_PADDING.y)
			var clash := false
			for other in placed:
				if other.intersects(box):
					clash = true
					break
			if not clash:
				found = true
				break
		if not found and not forced:
			labels_suppressed += 1
			continue
		placed.append(box)
		labels_drawn += 1
		var color := ThemeScript.TEXT_LEAF
		if forced:
			color = ThemeScript.GOLD_BRIGHT
		overlay.draw_rect(box, Color(0.03, 0.05, 0.03, 0.55))
		overlay.draw_string(font, origin, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, color)


## Regions in the order their labels get first refusal on the space. Total and
## deterministic: every region appears exactly once, and ties break on region id.
func _label_order() -> Array[Dictionary]:
	var scored: Array[Dictionary] = []
	for row in rows:
		var region_id := String(row["id"])
		var rank := 6
		if region_id == selected_region or region_id == selected_target:
			rank = 0
		elif region_id == hover_region:
			rank = 1
		elif Array(targets).has(region_id):
			rank = 2
		elif Array(staging).has(region_id):
			rank = 3
		elif int(row["armies"]) > 0:
			rank = 4
		elif int(row["owner"]) >= 0:
			rank = 5
		scored.append({"rank": rank, "id": region_id, "row": row})
	scored.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if int(a["rank"]) != int(b["rank"]):
			return int(a["rank"]) < int(b["rank"])
		return String(a["id"]) < String(b["id"]))
	var ordered: Array[Dictionary] = []
	for entry in scored:
		ordered.append(entry["row"] as Dictionary)
	return ordered


# --- the radial build menu ----------------------------------------------------------

## RETAIL'S RING OF BUILDING ICONS around the selected plot.
##
## Every entry is a `LivingWorldBuilding` retail marks `AvailableTo` this seat's
## template, with retail's own `ConstructButtonImage` on it. An entry whose image
## did not resolve draws an empty slot with its retail id under it - never a
## substitute icon.
##
## NOTHING HERE BUILDS ANYTHING. Construction is not in the simulation, and the
## screen says so beside the menu rather than letting a clickable ring imply a
## system that does not exist.
func _draw_radial_menu(font: Font) -> void:
	var slots := radial_slots()
	if slots.is_empty():
		return
	var region_id := String(selected_plot.get("region", ""))
	var index := int(selected_plot.get("index", -1))
	var centre: Vector2 = (_plot_screen_positions[region_id] as Array)[index]
	var ring_radius := radial_radius()
	overlay.draw_circle(centre, ring_radius + RADIAL_ICON * _view_scale() * 0.6,
		Color(0.03, 0.05, 0.03, 0.62))
	# RETAIL'S OWN RING. `apt_LivingWorldUI_1.tga` is the War of the Ring shell's
	# own texture sheet and it carries two elvish-script rings; the gold one is
	# picked by a stated rule over the sheet's pixels, never by a hand-written
	# index. With no bundle this falls back to a plain arc and the screen says the
	# UI bundle is absent.
	var elvish: Texture2D = ui.chrome_ring("gold") if has_ui() else null
	if elvish != null:
		var span := (ring_radius + RADIAL_ICON * _view_scale() * 0.5) * 2.0
		overlay.draw_texture_rect(elvish,
			Rect2(centre - Vector2(span * 0.5, span * 0.5), Vector2(span, span)), false)
	else:
		overlay.draw_arc(centre, ring_radius, 0.0, TAU, 64, Color(0.90, 0.84, 0.62, 0.4), 1.0)
	for slot_row in slots:
		var box := slot_row["box"] as Rect2
		var entry := slot_row["entry"] as Dictionary
		overlay.draw_rect(box.grow(2.0), Color(0.05, 0.06, 0.05, 0.95))
		var icon: Texture2D = null
		var image_id := String(entry.get("image_id", ""))
		if has_ui() and not image_id.is_empty():
			icon = ui.image(image_id)
		if icon != null:
			overlay.draw_texture_rect(icon, box, false)
		else:
			overlay.draw_rect(box, Color(0.18, 0.16, 0.12, 0.9))
			if font != null:
				overlay.draw_string(font, box.position + Vector2(3.0, RADIAL_ICON * 0.6),
					"no icon", HORIZONTAL_ALIGNMENT_LEFT, RADIAL_ICON - 6.0, 10,
					Color("#c8483f"))
		# RETAIL'S OWN RADIAL BORDER (`radialborders.dds`) around each slot, or a
		# plain rule when the bundle is absent.
		var border: Texture2D = ui.image("RadialBorder") if has_ui() else null
		if border != null:
			overlay.draw_texture_rect(border, box.grow(3.0), false)
		else:
			overlay.draw_rect(box, Color(0.90, 0.84, 0.62, 0.75), false, 1.0)
		if font != null:
			var caption := String(entry.get("title", entry.get("id", "?")))
			var cost := String(entry.get("cost", ""))
			if not cost.is_empty():
				caption += "  " + cost
			var caption_width := RADIAL_ICON + 84.0
			overlay.draw_string(font, box.position + Vector2(
					(RADIAL_ICON - caption_width) * 0.5, RADIAL_ICON + 12.0),
				caption, HORIZONTAL_ALIGNMENT_CENTER, caption_width, 11,
				ThemeScript.TEXT_LEAF)


func _color_of(owner: int) -> Color:
	if owner < 0 or owner >= owner_colors.size():
		return neutral_color
	return owner_colors[owner]


# --- input --------------------------------------------------------------------

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		var motion := event as InputEventMouseMotion
		if _dragging:
			if _orbiting:
				_orbit(motion.relative)
			else:
				_pan(motion.relative)
			return
		var hovered := region_at(motion.position)
		if hovered != hover_region:
			hover_region = hovered
			region_hovered.emit(hovered)
			_apply_territory_colors()
			_redraw()
		return
	if not (event is InputEventMouseButton):
		return
	var button := event as InputEventMouseButton
	match button.button_index:
		MOUSE_BUTTON_WHEEL_UP:
			if button.pressed:
				_set_zoom(_zoom / ZOOM_STEP)
		MOUSE_BUTTON_WHEEL_DOWN:
			if button.pressed:
				_set_zoom(_zoom * ZOOM_STEP)
		MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE:
			# RIGHT DRAG PANS, MIDDLE DRAG ORBITS - and so does right-drag with a
			# modifier held, because a laptop trackpad has no middle button and a
			# camera control nobody can reach is not a camera control. The choice
			# is stated in the screen's help line rather than left to be found.
			_dragging = button.pressed
			_orbiting = (button.button_index == MOUSE_BUTTON_MIDDLE
				or button.shift_pressed or button.alt_pressed)
		MOUSE_BUTTON_LEFT:
			if not button.pressed:
				return
			# A PLOT WINS OVER ITS REGION. Plots are only drawn for the region
			# already selected or hovered, so a click that lands on one is
			# unambiguously aimed at it - and the region under it is already the
			# selection, so nothing is reachable only through the region.
			var plot := plot_at(button.position)
			if not plot.is_empty():
				plot_clicked.emit(String(plot["region"]), int(plot["index"]))
				return
			var region_id := region_at(button.position)
			if not region_id.is_empty():
				region_clicked.emit(region_id)


## The build plot under a point, as `{region, index}`, or `{}`. Deterministic:
## regions are tried in sorted order and plots in their authored order, so a
## click never depends on dictionary iteration order.
func plot_at(point: Vector2) -> Dictionary:
	var best: Dictionary = {}
	var best_distance := PLOT_RADIUS * _view_scale() + PLOT_PICK_SLOP
	var region_ids: Array[String] = []
	for key in _plot_screen_positions.keys():
		region_ids.append(String(key))
	region_ids.sort()
	for region_id in region_ids:
		var points: Array = _plot_screen_positions[region_id] as Array
		for index in range(points.size()):
			var distance := (point - (points[index] as Vector2)).length()
			if distance <= best_distance:
				best = {"region": region_id, "index": index}
				best_distance = distance
	return best


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
	# A zoom changes how far the target may be dragged from the map, because the
	# clamp is expressed in map units and the useful slack shrinks as the camera
	# closes in. Re-clamping here means zooming out can never leave the target
	# somewhere a pan could not have put it.
	_clamp_camera_target()
	_apply_camera()
	_redraw()


## Orbit the camera. Yaw is a full circle; pitch is bounded so the camera cannot
## pass through the ground or over the top of it.
func _orbit(relative: Vector2) -> void:
	_yaw = wrapf(_yaw - relative.x * YAW_PER_PIXEL, -PI, PI)
	_pitch_degrees = clampf(
		_pitch_degrees + relative.y * PITCH_PER_PIXEL,
		MIN_PITCH_DEGREES, MAX_PITCH_DEGREES)
	# The fit depends on the pitch - a flatter angle needs the camera further
	# back for the same map - so the distance is re-fitted as the angle moves.
	# Still only `_camera_distance`; the player's zoom multiplies it untouched.
	_fit_distance()
	_apply_camera()
	_redraw()


## Keep the camera target inside the map's own footprint plus a margin. Without
## this a single long drag put Middle-earth off the panel entirely and the only
## way back was `reset_camera()`.
func _clamp_camera_target() -> void:
	if not has_map():
		return
	var extent: Dictionary = bundle.terrain_extent
	var x_min := float(extent["x_min"])
	var x_max := float(extent["x_max"])
	var y_min := float(extent["y_min"])
	var y_max := float(extent["y_max"])
	var margin_x := (x_max - x_min) * PAN_MARGIN_FRACTION
	var margin_y := (y_max - y_min) * PAN_MARGIN_FRACTION
	# `world_to_godot` is (x, z, -y), so the map's X maps to Godot X and the map's
	# Y maps to NEGATIVE Godot Z. The bounds are converted rather than assumed.
	var low := BundleScript.world_to_godot(x_min - margin_x, y_min - margin_y, 0.0)
	var high := BundleScript.world_to_godot(x_max + margin_x, y_max + margin_y, 0.0)
	_camera_target.x = clampf(_camera_target.x, minf(low.x, high.x), maxf(low.x, high.x))
	_camera_target.z = clampf(_camera_target.z, minf(low.z, high.z), maxf(low.z, high.z))


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
	_clamp_camera_target()
	_apply_camera()
	_redraw()


## Reset the camera to the framing the view opens with. Presentation only.
func reset_camera() -> void:
	_zoom = 1.0
	_yaw = 0.0
	_pitch_degrees = DEFAULT_PITCH_DEGREES
	_frame_camera()
	_redraw()


## Point the camera at one region at a given zoom, or at the whole map when
## `region_id` is empty. PRESENTATION ONLY - the same field a drag writes.
## Public so a capture or a test can put the camera somewhere reproducible
## instead of synthesising drag events.
func focus_region(region_id: String, zoom: float) -> void:
	if not region_id.is_empty() and _world_positions.has(region_id):
		_camera_target = _world_positions[region_id] as Vector3
	elif region_id.is_empty():
		_frame_camera()
	_zoom = clampf(zoom, MIN_ZOOM, MAX_ZOOM)
	_clamp_camera_target()
	_apply_camera()
	_redraw()


## Set the orbit directly, in radians of yaw and degrees of pitch, clamped to the
## same range a drag is. Also presentation only.
func set_orbit(yaw: float, pitch_degrees: float) -> void:
	_yaw = wrapf(yaw, -PI, PI)
	_pitch_degrees = clampf(pitch_degrees, MIN_PITCH_DEGREES, MAX_PITCH_DEGREES)
	_fit_distance()
	_apply_camera()
	_redraw()


## The camera's current framing, for the screen's help line and for a test that
## needs to assert the framing survived a resize. Read-only.
func camera_state() -> Dictionary:
	return {
		"zoom": _zoom,
		"yaw": _yaw,
		"pitch": _pitch_degrees,
		"target": _camera_target,
		"distance": _camera_distance,
	}
