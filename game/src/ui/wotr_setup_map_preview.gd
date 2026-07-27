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
## PRESENTATION ONLY. Hover lives here, reaches nothing, and is never hashed.

signal region_hovered(region_id: String)

const ChromeScript = preload("res://src/wotr/wotr_chrome.gd")

const NEUTRAL_FILL := Color(0.20, 0.24, 0.27, 0.62)
const NEUTRAL_EDGE := Color(0.38, 0.45, 0.50, 0.55)
const SEA := Color(0.035, 0.055, 0.078, 1.0)
const OWNED_ALPHA := 0.70
const HOVER_ALPHA := 0.92
const INSET := 12.0

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

var _last_transform := Transform2D()


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP


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
	var rect := Rect2(Vector2.ZERO, size)
	ChromeScript.draw_inset_glass(self, rect)
	var field := rect.grow(-INSET * 0.5)
	draw_rect(field, SEA)
	if not unavailable_reason.is_empty() or not _have_bounds:
		return
	var transform := _fit(field)
	_last_transform = transform
	# Neutral regions first, then claimed ones over them, so a claimed territory
	# is never half-hidden behind a neighbour drawn later.
	for pass_index in range(2):
		for region_id in drawn_regions:
			var claimed: bool = owner_colors.has(region_id)
			if claimed != (pass_index == 1):
				continue
			var tint: Color = NEUTRAL_FILL
			if claimed:
				var seat: Color = owner_colors[region_id]
				tint = Color(seat.r, seat.g, seat.b,
					HOVER_ALPHA if region_id == hover_region else OWNED_ALPHA)
			elif region_id == hover_region:
				tint = Color(NEUTRAL_FILL.r, NEUTRAL_FILL.g, NEUTRAL_FILL.b, 0.85)
			_draw_soup(_fill[region_id] as PackedVector2Array, transform, tint)
	for region_id in drawn_regions:
		if not _border.has(region_id):
			continue
		var edge := NEUTRAL_EDGE
		if region_id == hover_region:
			edge = ChromeScript.STEEL_ICE
		elif owner_colors.has(region_id):
			var seat: Color = owner_colors[region_id]
			edge = Color(seat.r, seat.g, seat.b, 0.95).lightened(0.25)
		_draw_soup(_border[region_id] as PackedVector2Array, transform, edge)


func _draw_soup(points: PackedVector2Array, transform: Transform2D, tint: Color) -> void:
	var count := points.size() - (points.size() % 3)
	if count <= 0:
		return
	var triangle := PackedVector2Array()
	triangle.resize(3)
	for i in range(0, count, 3):
		triangle[0] = transform * points[i]
		triangle[1] = transform * points[i + 1]
		triangle[2] = transform * points[i + 2]
		draw_colored_polygon(triangle, tint)


## The uniform fit of retail's world bounds into `field`, aspect preserved and
## centred. Retail's +y is north; the conversion in `wotr_region_geometry` has
## already turned that into -z, so a straight (x, z) -> (x, y) mapping puts north
## at the top with no flip written here.
func _fit(field: Rect2) -> Transform2D:
	if _bounds.size.x <= 0.0 or _bounds.size.y <= 0.0:
		return Transform2D()
	var scale := minf(field.size.x / _bounds.size.x, field.size.y / _bounds.size.y)
	var drawn_size := _bounds.size * scale
	var origin := field.position + (field.size - drawn_size) * 0.5 - _bounds.position * scale
	return Transform2D(0.0, Vector2(scale, scale), 0.0, origin)


func _gui_input(event: InputEvent) -> void:
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
