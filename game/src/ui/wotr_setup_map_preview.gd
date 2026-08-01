extends Control

## THE GAME SETUP SCREEN'S MAP PREVIEW: retail's own territory shapes, shaded by
## who claims them under the selected scenario.
##
## WHY IT IS NOT THE 3D MAP. The strategic screen draws Middle-earth as retail's
## `livingmap.w3d` mesh under a camera, because that is what retail draws once a
## campaign is running. The SETUP screen's preview is a flat plate - it exists to
## answer "who starts where", not "what does the world look like", and standing
## up a SubViewport, a camera and 64 textured sub-objects to answer that would
## cost the setup screen a second copy of the heaviest thing in the lane.
##
## WHAT THE SHAPES ARE. THEY ARE RETAIL'S. `wotr_region_geometry.gd` loads
## `lmr_fill.w3d` and `lmr_border.w3d` - one mesh per region, retail's own
## triangles, in retail's own world units - and this control projects them
## straight down (Godot x, z) onto the panel. Nothing is fitted, registered,
## smoothed or redrawn by hand. A region the bundle carries no mesh for IS NOT
## DRAWN and is named in `regions_without_geometry`, which the screen reports.
##
## WHAT THE COLOURS ARE. A region is filled in a seat's colour when the selected
## scenario's own `ownershipSets` give that region to that seat, and in the
## neutral tone otherwise. The ownership sets are the document's, converted from
## retail's scenario `.inc` files; the seat colours are the six
## `AvailableInWotR` blocks of `multiplayer.ini`. A scenario that claims nothing
## - retail's freeform ones claim nothing at all, by design - shades NOTHING, and
## the screen says the scenario is freeform rather than inventing start positions.
##
## WHAT THE GROUND UNDER THEM IS. RETAIL'S PAINTED MIDDLE-EARTH. The oracle's MAP
## tab shows the living world itself - green Eriador, the ochre Brown Lands, the
## black scar of Mordor, sea to the west - with only a handful of territories
## picked out in a player colour on top. Round two drew the territories on BLACK,
## which turned a map into a bar chart: six saturated blobs and no world.
##
## `set_terrain()` takes the SAME `wotr_map_bundle` the strategic screen's 3D view
## uses - retail's `livingmap.w3d` sub-objects, retail's `lm_*.dds` colour maps -
## and flattens the terrain tiles the way `set_geometry()` already flattens the
## region meshes: keep Godot x and z, drop height, keep every vertex's authored
## UV, and hand the triangles to the canvas with their own texture. Nothing is
## re-projected, re-registered or re-painted; the plan view IS the mesh seen from
## above, which is why the territory shapes land on the terrain they belong to
## without a fitting step. No bundle means no terrain and the preview falls back
## to the flat sea it always drew, with the reason on the screen's absence list.
##
## PRESENTATION ONLY. Hover lives here, reaches nothing, and is never hashed.

signal region_hovered(region_id: String)
## A territory was CLICKED. The setup screen uses this to place a freeform
## scenario's start territories; nothing here knows or cares what it is for.
signal region_selected(region_id: String)

const ChromeScript = preload("res://src/wotr/wotr_chrome.gd")

const NEUTRAL_EDGE := Color(0.38, 0.45, 0.50, 0.55)
const NEUTRAL_EDGE_OVER_TERRAIN := Color(0.07, 0.05, 0.04, 0.80)
const SEA := Color(0.035, 0.055, 0.078, 1.0)
const INSET := 12.0

## How hard a claimed territory is tinted. TWO SETS, because the two grounds want
## opposite things: over retail's painted terrain the tint is a WASH that has to
## let the map through (the oracle's coloured territories are translucent), and
## over the bare sea it is the only thing on screen and has to carry the shape by
## itself. Round two used the opaque pair on both and the map disappeared.
const OWNED_ALPHA_OVER_TERRAIN := 0.32
const HOVER_ALPHA_OVER_TERRAIN := 0.58
const OWNED_ALPHA_BARE := 0.70
const HOVER_ALPHA_BARE := 0.92
## Unclaimed regions get NO fill over the terrain - retail leaves them as the
## painted world - and the old grey wash only over the bare fallback.
const NEUTRAL_FILL_BARE := Color(0.20, 0.24, 0.27, 0.62)

## `region id -> PackedVector2Array` of flattened triangles (3 points per
## triangle), in retail world units. Built once per geometry load.
var _fill: Dictionary = {}
var _border: Dictionary = {}
## The union bounds of every projected triangle, in retail world units.
var _bounds := Rect2()
var _have_bounds := false

## `region id -> Color`. Absent means neutral.
var owner_colors: Dictionary = {}
## Drawn regions, and the ids the document declares that have no mesh here.
var drawn_regions: PackedStringArray = PackedStringArray()
var regions_without_geometry: PackedStringArray = PackedStringArray()
var hover_region := ""
## Non-empty when there is nothing to draw; painted in place of the map.
var unavailable_reason := ""

## Retail's flattened terrain tiles: `[{name, points, uvs, indices, texture}]`.
var _terrain: Array[Dictionary] = []
## Why the terrain is not being drawn, or "" when it is. Public so the screen can
## put it on the absence list rather than showing bare sea with no explanation.
var terrain_reason := "set_terrain() has not run"
## Terrain tiles retail's bundle carries but bound no colour map to, by name.
var terrain_tiles_without_texture: PackedStringArray = PackedStringArray()

var _last_transform := Transform2D()


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	# Retail's terrain mesh reaches far past the territories it carries, so the
	# plan view overhangs this panel on every side. Godot's own control clip is
	# what keeps it inside the bezel; without it the map paints over the setup
	# screen's Scenario and Territory columns.
	clip_contents = true


## Take retail's meshes. `region_ids` is the DOCUMENT's list, so a region the
## document declares and the geometry bundle lacks can be reported by name
## instead of quietly missing from the picture.
func set_geometry(geometry, region_ids: PackedStringArray) -> void:
	_fill = {}
	_border = {}
	_bounds = Rect2()
	_have_bounds = false
	drawn_regions = PackedStringArray()
	regions_without_geometry = PackedStringArray()
	if geometry == null or not geometry.loaded:
		unavailable_reason = (
			"no region geometry bundle is loaded, so retail's territory shapes "
			+ "cannot be drawn")
		queue_redraw()
		return
	unavailable_reason = ""
	var drawn: Array[String] = []
	var missing: Array[String] = []
	for value in region_ids:
		var region_id := String(value)
		var fill_points := _flatten(geometry.region_mesh(region_id, "fill"))
		if fill_points.is_empty():
			missing.append(region_id)
			continue
		_fill[region_id] = fill_points
		var border_points := _flatten(geometry.region_mesh(region_id, "border"))
		if not border_points.is_empty():
			_border[region_id] = border_points
		drawn.append(region_id)
		for point in fill_points:
			if _have_bounds:
				_bounds = _bounds.expand(point)
			else:
				_bounds = Rect2(point, Vector2.ZERO)
				_have_bounds = true
	drawn.sort()
	missing.sort()
	drawn_regions = PackedStringArray(drawn)
	regions_without_geometry = PackedStringArray(missing)
	queue_redraw()


func set_ownership(colors: Dictionary) -> void:
	owner_colors = colors
	queue_redraw()


## Retail's painted terrain, flattened to a plan view. `bundle` is a loaded
## `wotr_map_bundle`; anything else clears the terrain and leaves the bare sea.
##
## WHICH SUB-OBJECTS. The TERRAIN TILES ONLY - the ones retail names `LM_<n>` -
## by the same rule the bundle itself uses to decide what it can sample a height
## from. The rest of the 64 sub-objects are clouds, lava sequences, river
## overlays, impassable collision volumes and shader-only surfaces; a plan view
## that drew the cloud sheet would paint the world out, and a plan view that drew
## the collision volumes would paint it grey. A tile whose material never
## resolved a texture is SKIPPED and named, not filled with a stand-in colour.
func set_terrain(bundle) -> void:
	_terrain = []
	terrain_reason = ""
	terrain_tiles_without_texture = PackedStringArray()
	if bundle == null or not bundle.loaded:
		terrain_reason = (
			"no living-map bundle is loaded, so the preview draws retail's territory "
			+ "shapes over flat sea instead of over retail's painted Middle-earth")
		queue_redraw()
		return
	var untextured: Array[String] = []
	for value in bundle.sub_objects:
		var row := value as Dictionary
		var name := String(row.get("name", ""))
		if not (name.begins_with("LM_") and name.substr(3).is_valid_int()):
			continue
		if not bool(row.get("textured", false)):
			untextured.append(name)
			continue
		var material := row.get("material", null) as StandardMaterial3D
		var texture: Texture2D = null if material == null else material.albedo_texture
		if texture == null:
			untextured.append(name)
			continue
		var flat := _flatten_textured(row.get("mesh", null) as ArrayMesh)
		if flat.is_empty():
			continue
		flat["texture"] = texture
		flat["name"] = name
		_terrain.append(flat)
	untextured.sort()
	terrain_tiles_without_texture = PackedStringArray(untextured)
	if _terrain.is_empty():
		terrain_reason = (
			"the living-map bundle carries no textured LM_<n> terrain tile, so the "
			+ "preview draws retail's territory shapes over flat sea")
	queue_redraw()


## One terrain tile's triangles in the ground plane, with their authored UVs.
## Returns `{}` when the mesh carries no vertices, indices or UVs - a tile
## without UVs cannot be textured and is not guessed at.
static func _flatten_textured(mesh: ArrayMesh) -> Dictionary:
	if mesh == null or mesh.get_surface_count() <= 0:
		return {}
	var arrays: Array = mesh.surface_get_arrays(0)
	if arrays.size() <= Mesh.ARRAY_INDEX:
		return {}
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var uvs: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV]
	if vertices.is_empty() or indices.is_empty() or uvs.size() != vertices.size():
		return {}
	var points := PackedVector2Array()
	points.resize(vertices.size())
	for i in range(vertices.size()):
		points[i] = Vector2(vertices[i].x, vertices[i].z)
	return {"points": points, "uvs": uvs, "indices": indices}


## Flatten one ArrayMesh's triangles onto the ground plane. Godot's y is height
## and is DISCARDED - this is a top-down plate, and retail's own conversion
## already put x and z in the plan.
static func _flatten(mesh: ArrayMesh) -> PackedVector2Array:
	var points := PackedVector2Array()
	if mesh == null or mesh.get_surface_count() <= 0:
		return points
	var arrays: Array = mesh.surface_get_arrays(0)
	if arrays.size() <= Mesh.ARRAY_INDEX:
		return points
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	if vertices.is_empty() or indices.is_empty():
		return points
	points.resize(indices.size())
	for i in range(indices.size()):
		var vertex := vertices[indices[i]]
		points[i] = Vector2(vertex.x, vertex.z)
	return points


func _draw() -> void:
	# Per-draw counters, so `skipped_triangles` is this frame's answer and not a
	# running total nobody can interpret.
	skipped_triangles = 0
	total_triangles = 0
	var rect := Rect2(Vector2.ZERO, size)
	ChromeScript.draw_inset_glass(self, rect)
	var field := rect.grow(-INSET * 0.5)
	draw_rect(field, SEA)
	if not unavailable_reason.is_empty() or not _have_bounds:
		return
	# THE FIT IS ALWAYS FRAMED ON THE REGION BOUNDS, terrain or no terrain: the
	# territories are the subject and retail's terrain mesh runs well past the
	# last one, so fitting to the MESH would shrink Middle-earth into the middle
	# third with dead water round it.
	#
	# WHAT CHANGES WITH THE TERRAIN IS CONTAIN VERSUS COVER.
	#   * No terrain: CONTAIN. The shapes are the only thing on screen and losing
	#     one off an edge would lose a territory outright.
	#   * Terrain: COVER. The oracle's preview is a FILLED panel, cropped on every
	#     side - Forodwaith is cut off the top, Harad off the bottom, the western
	#     sea runs to the bezel - and a contained fit leaves the letterbox bars
	#     down both sides that the first terrain capture showed. Cropping costs
	#     nothing here because the terrain under the crop is still the same world
	#     and the territory list is the seat table, not the picture.
	var over_terrain := not _terrain.is_empty()
	var transform := _fit(field, over_terrain)
	_last_transform = transform
	for tile in _terrain:
		_draw_textured(tile, transform)
	# Neutral regions first, then claimed ones over them, so a claimed territory
	# is never half-hidden behind a neighbour drawn later.
	for pass_index in range(2):
		for region_id in drawn_regions:
			var claimed: bool = owner_colors.has(region_id)
			if claimed != (pass_index == 1):
				continue
			var tint := Color(0, 0, 0, 0)
			if claimed:
				var seat: Color = owner_colors[region_id]
				var owned := OWNED_ALPHA_OVER_TERRAIN if over_terrain else OWNED_ALPHA_BARE
				var hovered := HOVER_ALPHA_OVER_TERRAIN if over_terrain else HOVER_ALPHA_BARE
				tint = Color(seat.r, seat.g, seat.b,
					hovered if region_id == hover_region else owned)
			elif over_terrain:
				# Retail leaves an unclaimed territory as the painted world. Only
				# the hovered one is lifted, and with a pale wash rather than a
				# colour, so hover never reads as ownership.
				if region_id != hover_region:
					continue
				tint = Color(0.72, 0.82, 0.92, 0.22)
			else:
				tint = NEUTRAL_FILL_BARE
				if region_id == hover_region:
					tint = Color(tint.r, tint.g, tint.b, 0.85)
			_draw_soup(_fill[region_id] as PackedVector2Array, transform, tint)
	for region_id in drawn_regions:
		if not _border.has(region_id):
			continue
		# Over retail's painted terrain the unclaimed borders are DARK, the way
		# the oracle draws them - a near-black hairline round every territory. The
		# pale grey edge is for the bare fallback, where there is nothing but the
		# outlines and a dark line on dark sea would vanish.
		var edge := NEUTRAL_EDGE_OVER_TERRAIN if over_terrain else NEUTRAL_EDGE
		if region_id == hover_region:
			edge = ChromeScript.STEEL_ICE
		elif owner_colors.has(region_id):
			# A CLAIMED TERRITORY'S RIM IS ITS OWN COLOUR, DARKENED - never
			# lightened. Round two lightened it by a quarter and drew it over an
			# already-flooded map, which the adversarial read filed as "bright
			# saturated 1px strokes... a second layer of chroma noise on top of an
			# already over-chromatic map". Retail's rims are ink lines that happen
			# to carry the owner's hue, not highlights.
			var seat: Color = owner_colors[region_id]
			edge = Color(seat.r, seat.g, seat.b, 0.9).darkened(0.45) if over_terrain \
				else Color(seat.r, seat.g, seat.b, 0.95).lightened(0.25)
		_draw_soup(_border[region_id] as PackedVector2Array, transform, edge)
	# THE VIEWPORT FRAME, LAST AND ALWAYS. `draw_inset_glass` laid a bezel down
	# first and the covering terrain then painted straight over it, so the map ran
	# to the raw edge of the control.
	#
	# IT IS THE SCREEN'S THREE-PART EDGE, not a hairline: dark outer contour, lit
	# inner bevel on the top and left where the light is, shadow on the bottom and
	# right. Round two closed it with a 1px stroke like everything else and the
	# adversarial read was "Map viewport frame is a hairline... the map needs a
	# recessed metal frame so it sits *in* the screen."
	draw_rect(rect.grow(-1.0), Color(0.0, 0.0, 0.0, 0.9), false, 2.0)
	var light := Color(ChromeScript.STEEL_PALE.r, ChromeScript.STEEL_PALE.g,
		ChromeScript.STEEL_PALE.b, 0.55)
	var shade := Color(0.0, 0.0, 0.0, 0.6)
	draw_line(Vector2(2.5, 2.5), Vector2(rect.size.x - 2.5, 2.5), light, 2.0)
	draw_line(Vector2(2.5, 2.5), Vector2(2.5, rect.size.y - 2.5), light, 2.0)
	draw_line(Vector2(rect.size.x - 2.5, 2.5), Vector2(rect.size.x - 2.5, rect.size.y - 2.5),
		shade, 2.0)
	draw_line(Vector2(2.5, rect.size.y - 2.5), Vector2(rect.size.x - 2.5, rect.size.y - 2.5),
		shade, 2.0)
	draw_rect(rect.grow(-4.0), ChromeScript.STEEL_DARK, false, 2.0)


## DEGENERATE TRIANGLES ARE RETAIL'S, AND THE TRIANGULATOR IS NOT ASKED ABOUT THEM.
##
## Retail's `lmr_fill`/`lmr_border` soups legitimately carry zero-area triangles -
## collapsed edges are ordinary in exported strip geometry. The first version of
## this control handed each triangle to `draw_colored_polygon`, which TRIANGULATES
## whatever it is handed; a collinear triangle has no triangulation, so every one
## of them printed `Invalid polygon data, triangulation failed` - 1,228 errors on
## a single preview draw. Under repository policy an error on a required path is a
## gate failure.
##
## An area filter cut that to two, and two is still a gate failure. The reason a
## filter can never reach zero is that it is guessing where an implementation
## detail's tolerance lies: `Geometry2D.triangulate_polygon` rejects slivers whose
## AREA is finite but whose shape is degenerate to its own epsilon, so any
## threshold either leaks or eats real geometry.
##
## SO THE TRIANGULATOR IS BYPASSED. These points are ALREADY TRIANGLES - retail
## exported them as an indexed triangle list and `_flatten` kept that grouping -
## and `RenderingServer.canvas_item_add_triangle_array` takes an index list
## directly and rasterises it. There is nothing left to fail, which is why this is
## a fix and the threshold was a mitigation. It is also ONE draw call per region
## instead of one per triangle.
##
## The area test is KEPT, in SCREEN space, because a triangle that covers less
## than a hundredth of a pixel contributes nothing and counting them is how a
## future geometry change that starts collapsing whole regions gets noticed.
const MIN_TRIANGLE_AREA := 0.01

## How many triangles the last draw skipped, and how many it had. Kept as a
## counter rather than dropped silently: if a future geometry change starts
## collapsing whole regions, "the outline vanished" needs a number behind it.
var skipped_triangles := 0
var total_triangles := 0


func _draw_soup(points: PackedVector2Array, transform: Transform2D, tint: Color) -> void:
	var count := points.size() - (points.size() % 3)
	if count <= 0:
		return
	var vertices := PackedVector2Array()
	var indices := PackedInt32Array()
	var colors := PackedColorArray()
	for i in range(0, count, 3):
		var a := transform * points[i]
		var b := transform * points[i + 1]
		var c := transform * points[i + 2]
		total_triangles += 1
		# Twice the signed area, via the cross product of two edges.
		if absf((b - a).cross(c - a)) * 0.5 < MIN_TRIANGLE_AREA:
			skipped_triangles += 1
			continue
		var base := vertices.size()
		vertices.append(a)
		vertices.append(b)
		vertices.append(c)
		colors.append(tint)
		colors.append(tint)
		colors.append(tint)
		indices.append(base)
		indices.append(base + 1)
		indices.append(base + 2)
	if indices.is_empty():
		return
	RenderingServer.canvas_item_add_triangle_array(
		get_canvas_item(), indices, vertices, colors)


## One terrain tile, textured with retail's own colour map. The index list is
## retail's, unchanged - degenerate triangles included, because there is no
## triangulator here to reject them and a zero-area triangle rasterises to
## nothing - so the tile is ONE draw call and the plan view costs 20 of them.
func _draw_textured(tile: Dictionary, transform: Transform2D) -> void:
	var points: PackedVector2Array = tile["points"]
	var texture: Texture2D = tile["texture"]
	var screen := PackedVector2Array()
	screen.resize(points.size())
	for i in range(points.size()):
		screen[i] = transform * points[i]
	var colors := PackedColorArray()
	colors.resize(points.size())
	colors.fill(Color.WHITE)
	RenderingServer.canvas_item_add_triangle_array(
		get_canvas_item(), tile["indices"] as PackedInt32Array, screen, colors,
		tile["uvs"] as PackedVector2Array, PackedInt32Array(), PackedFloat32Array(),
		texture.get_rid())


## The uniform fit of retail's world bounds into `field`, aspect preserved and
## centred. Retail's +y is north; the conversion in `wotr_region_geometry` has
## already turned that into -z, so a straight (x, z) -> (x, y) mapping puts north
## at the top with no flip written here.
func _fit(field: Rect2, cover: bool = false) -> Transform2D:
	if _bounds.size.x <= 0.0 or _bounds.size.y <= 0.0:
		return Transform2D()
	var scale := (
		maxf(field.size.x / _bounds.size.x, field.size.y / _bounds.size.y) if cover
		else minf(field.size.x / _bounds.size.x, field.size.y / _bounds.size.y))
	var drawn_size := _bounds.size * scale
	var origin := field.position + (field.size - drawn_size) * 0.5 - _bounds.position * scale
	return Transform2D(0.0, Vector2(scale, scale), 0.0, origin)


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.pressed and button.button_index == MOUSE_BUTTON_LEFT:
			var picked := _pick(button.position)
			if not picked.is_empty():
				region_selected.emit(picked)
		return
	if not (event is InputEventMouseMotion):
		return
	var found := _pick((event as InputEventMouseMotion).position)
	if found == hover_region:
		return
	hover_region = found
	queue_redraw()
	region_hovered.emit(found)


## Which region is under a point. An honest point-in-triangle test over retail's
## own triangles - not a nearest-centre guess, which would claim a region the
## cursor is not over.
func _pick(at: Vector2) -> String:
	if not _have_bounds:
		return ""
	for region_id in drawn_regions:
		var points := _fill[region_id] as PackedVector2Array
		var count := points.size() - (points.size() % 3)
		for i in range(0, count, 3):
			if _inside(at,
					_last_transform * points[i],
					_last_transform * points[i + 1],
					_last_transform * points[i + 2]):
				return region_id
	return ""


static func _inside(point: Vector2, a: Vector2, b: Vector2, c: Vector2) -> bool:
	var d1 := (point - b).cross(a - b)
	var d2 := (point - c).cross(b - c)
	var d3 := (point - a).cross(c - a)
	var negative := d1 < 0.0 or d2 < 0.0 or d3 < 0.0
	var positive := d1 > 0.0 or d2 > 0.0 or d3 > 0.0
	return not (negative and positive)
