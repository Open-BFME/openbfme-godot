extends RefCounted

## Retail shroud / fog-of-war grid.
##
## THE THREE STATES, and why they are three and not two.
## SAGE keeps ONE signed 16-bit value per (cell, player). OpenSAGE recovered the
## layout from retail .sav files and its debug view decodes the encoding
## exactly (PartitionCellManager.DrawDiagnostic):
##
##     State <  0   CLEAR    - magnitude is the number of active lookers
##     State == 0   SHROUDED - never seen; drawn solid
##     State == 1   FOGGED   - seen once, remembered, not currently seen
##     anything else -> the engine throws
##
## This module keeps the same three states as an explicit look COUNT plus an
## explored BIT rather than one packed short. The packing buys nothing in
## GDScript and it hides the transition that matters: when the last looker
## leaves a cell the count falls to zero and the cell becomes FOGGED - it never
## returns to SHROUDED. Only an explicit script shroud does that.
##
## RETAIL VALUES, cited (rotwk cache/effective-assets, never the layered tree):
##
##   data/ini/gamedata.ini:9024   UnlookPersistDuration = 1
##   data/ini/gamedata.ini:9026   ShroudColor = R:255 G:255 B:255
##   data/ini/gamedata.ini:9027   ClearAlpha  = 255
##   data/ini/gamedata.ini:9028   FogAlpha    = 127
##   data/ini/gamedata.ini:9029   ShroudAlpha = 0   ; "0 is opaque, 255 is clear"
##   data/ini/gamedata.ini:8627   PartitionCellSize = 40.0
##
## Note the INVERTED alpha convention on 9029: the authored byte is how much of
## the world you can see, not how much tint is drawn. This module carries the
## retail bytes verbatim and lets the presentation invert them once, at the
## point of drawing, so nothing downstream has to remember which way round it is.
##
## THE CELL SIZE IS THE ONE CONTESTED NUMBER, and it is recorded as contested.
## OpenSAGE dimensions the shroud grid off `GameData PartitionCellSize` (40.0)
## because in its recovered layout the shroud values LIVE ON the partition cell
## array - `PartitionCell.Values[player].State`. Reading the retail INI alone
## you would instead guess the terrain cell (10.0 units), which is what
## gamedatadebug.ini's `DebugVisibilityTileWidth = 10.0` hints at - but that
## file ships with a header saying it is never shipped, and it describes a
## DEBUG overlay, not the live grid. The savegame layout is the stronger
## oracle, so 40.0 it is; the constant is named and isolated so a later lane
## with better evidence changes one line. Retail visibly interpolates between
## cell centres when drawing, which is why a 40-unit grid does not look blocky.
##
## WHAT THIS MODULE DELIBERATELY IS NOT.
## It is not authoritative simulation state. It is DERIVED - a pure function of
## the entity rows the authoritative sim already hashes, recomputed each tick -
## and it contributes ZERO bytes to `RetailSliceSim._authoritative_state()`.
## That is not a dodge around the 3000-tick state pin; it is the correct
## classification. Retail's shroud does not feed the simulation until targeting
## rules are enforced against it, and this lane deliberately does not enforce
## them (see the report's follow-up list). The moment a later lane makes fog
## refuse an attack order, fog stops being derived and MUST move into the
## hashed bag - keyed only when the mechanic is enabled, the way
## `script_surface_bag["ring_mechanic"]` is.
##
## Determinism: every operation here is integer cell arithmetic over an
## insertion-ordered set of teams, with no randomness and no iteration over an
## unordered container. Two instances driven with the same call sequence produce
## byte-identical `to_state()`; `retail_fog_of_war_runner.gd` asserts it. Peers
## in a lockstep match each compute every player's grid from their own identical
## simulation, so the grids agree without being transmitted.


## gamedata.ini:8627. Source (retail world) units per shroud cell.
const RETAIL_PARTITION_CELL_SIZE := 40.0

## gamedata.ini:9027-9029, verbatim and in retail's inverted convention:
## 0 is opaque shroud, 255 is fully clear ground.
const RETAIL_SHROUD_ALPHA := 0
const RETAIL_FOG_ALPHA := 127
const RETAIL_CLEAR_ALPHA := 255

## gamedata.ini:9026. One white tint; the three states are three alphas of it,
## not three colours (BFME2 dropped Generals' ClearShroudColor/PartialShroudColor).
const RETAIL_SHROUD_COLOR := Color(1.0, 1.0, 1.0)

enum { SHROUDED = 0, FOGGED = 1, CLEAR = 2 }

## Half-extent used when no map bounds are known. Large enough to contain every
## slice battlefield in sim units; the grid is allocated lazily per team so an
## oversized default costs nothing until a team actually looks.
const DEFAULT_HALF_EXTENT := 96.0

var enabled := false
var cell_size := 1.0
var origin := Vector2.ZERO
var cells_x := 0
var cells_y := 0
## ENABLE_BORDER_SHROUD / DISABLE_BORDER_SHROUD. Presentation-only today: the
## flag is carried so the script verbs have somewhere honest to land and the
## overlay can draw the map border black, and it is reported in `to_state()`.
var border_shroud := true

## team -> PackedInt32Array, one entry per cell: how many lookers cover it now.
var _look: Dictionary = {}
## team -> PackedByteArray, one entry per cell: 1 once the team has ever seen it.
var _explored: Dictionary = {}
## team -> PackedInt32Array of cell indices stamped since the last begin, so a
## pass can be cleared in O(stamped) instead of O(grid).
var _touched: Dictionary = {}
## team -> PackedByteArray: cells held clear by a permanent reveal.
var _permanent: Dictionary = {}
## Ordered records {name, team, center, radius}. Retail keys permanent reveals
## by a MapRevealName so MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT can drop one
## without disturbing the others.
var _permanent_reveals: Array = []


static func cell_size_for_scale(transform_scale: float) -> float:
	## A retail length in source units becomes sim units by the map's own
	## local_transform_scale, so the shroud cell follows the same conversion as
	## every VisionRange the compiler emits. A non-positive scale would collapse
	## the grid to a point, so it falls back to 1:1 rather than dividing by zero.
	if transform_scale <= 0.0:
		return RETAIL_PARTITION_CELL_SIZE
	return RETAIL_PARTITION_CELL_SIZE * transform_scale


func configure(bounds_min: Vector2, bounds_max: Vector2, transform_scale: float) -> void:
	cell_size = cell_size_for_scale(transform_scale)
	origin = bounds_min
	var extent := bounds_max - bounds_min
	# SAGE adds one cell of slop per axis on Zero Hour and later engines, which
	# BFME2 is (OpenSAGE PartitionCellManager, `extra`). Kept so a unit standing
	# exactly on the far border still has a cell.
	cells_x = maxi(1, int(ceil(maxf(0.0, extent.x) / cell_size)) + 1)
	cells_y = maxi(1, int(ceil(maxf(0.0, extent.y) / cell_size)) + 1)
	_look.clear()
	_explored.clear()
	_touched.clear()
	_permanent.clear()
	_permanent_reveals.clear()


func configure_default(transform_scale: float) -> void:
	configure(
		Vector2(-DEFAULT_HALF_EXTENT, -DEFAULT_HALF_EXTENT),
		Vector2(DEFAULT_HALF_EXTENT, DEFAULT_HALF_EXTENT),
		transform_scale
	)


# --- Cell arithmetic --------------------------------------------------------


func cell_index(position: Vector2) -> Vector2i:
	return Vector2i(
		int(floor((position.x - origin.x) / cell_size)),
		int(floor((position.y - origin.y) / cell_size))
	)


func _flat(cx: int, cy: int) -> int:
	## -1 for any cell outside the configured extent. Callers treat that as
	## permanently shrouded rather than clamping, because clamping would smear
	## an off-map look along the border.
	if cx < 0 or cy < 0 or cx >= cells_x or cy >= cells_y:
		return -1
	return cy * cells_x + cx


func _flat_at(position: Vector2) -> int:
	var cell := cell_index(position)
	return _flat(cell.x, cell.y)


func _cells_in_circle(center: Vector2, radius: float) -> PackedInt32Array:
	## Circle-versus-cell-rectangle, the same shape SAGE's own stamp primitive
	## uses (OpenSAGE PartitionCellManager.GetCells: AABB prefilter, then an
	## exact RectangleF.Intersects(center, radius) per candidate). A cell is in
	## the set when the circle touches ANY part of it, so the reached set can
	## exceed the radius by up to half a cell diagonal - never by a full cell,
	## which is what a naive centre-distance test would give at the corners.
	var out := PackedInt32Array()
	var r := maxf(0.0, radius)
	var local := center - origin
	var min_x := int(floor((local.x - r) / cell_size))
	var max_x := int(floor((local.x + r) / cell_size))
	var min_y := int(floor((local.y - r) / cell_size))
	var max_y := int(floor((local.y + r) / cell_size))
	min_x = maxi(0, min_x)
	min_y = maxi(0, min_y)
	max_x = mini(cells_x - 1, max_x)
	max_y = mini(cells_y - 1, max_y)
	var r_squared := r * r
	for cy in range(min_y, max_y + 1):
		var cell_min_y := origin.y + float(cy) * cell_size
		var cell_max_y := cell_min_y + cell_size
		var nearest_y := clampf(center.y, cell_min_y, cell_max_y)
		var dy := center.y - nearest_y
		for cx in range(min_x, max_x + 1):
			var cell_min_x := origin.x + float(cx) * cell_size
			var cell_max_x := cell_min_x + cell_size
			var nearest_x := clampf(center.x, cell_min_x, cell_max_x)
			var dx := center.x - nearest_x
			if dx * dx + dy * dy <= r_squared:
				out.append(cy * cells_x + cx)
	return out


# --- Team storage -----------------------------------------------------------


func _ensure_team(team: int) -> bool:
	if team < 0:
		return false
	if _look.has(team):
		return true
	if cells_x <= 0 or cells_y <= 0:
		return false
	var count := cells_x * cells_y
	var looks := PackedInt32Array()
	looks.resize(count)
	looks.fill(0)
	var explored := PackedByteArray()
	explored.resize(count)
	explored.fill(0)
	var permanent := PackedByteArray()
	permanent.resize(count)
	permanent.fill(0)
	_look[team] = looks
	_explored[team] = explored
	_permanent[team] = permanent
	_touched[team] = PackedInt32Array()
	return true


func teams() -> Array:
	return _look.keys()


# --- The per-tick look pass -------------------------------------------------


func begin_look_pass() -> void:
	## Drops last pass's clear set. Explored is NOT touched: that is the whole
	## point of the fog state - the ground stays remembered.
	for team in _look.keys():
		var looks: PackedInt32Array = _look[team]
		var touched: PackedInt32Array = _touched[team]
		for index in touched:
			looks[index] = 0
		_touched[team] = PackedInt32Array()


func add_look(team: int, center: Vector2, radius: float) -> void:
	_stamp(team, center, radius)


func commit_look_pass() -> void:
	## Permanent reveals are re-stamped after the movement-driven looks so a
	## script reveal cannot be erased by the unit that walked out of it.
	for record_value in _permanent_reveals:
		var record: Dictionary = record_value
		_stamp(
			int(record["team"]), Vector2(record["center"]), float(record["radius"])
		)


func _stamp(team: int, center: Vector2, radius: float) -> void:
	if not _ensure_team(team):
		return
	var looks: PackedInt32Array = _look[team]
	var explored: PackedByteArray = _explored[team]
	var touched: PackedInt32Array = _touched[team]
	for index in _cells_in_circle(center, radius):
		if looks[index] == 0:
			touched.append(index)
		looks[index] += 1
		explored[index] = 1
	_touched[team] = touched


# --- Queries ----------------------------------------------------------------


func state_at(team: int, position: Vector2) -> int:
	if not _look.has(team):
		return SHROUDED
	var index := _flat_at(position)
	if index < 0:
		return SHROUDED
	if (_look[team] as PackedInt32Array)[index] > 0:
		return CLEAR
	if (_explored[team] as PackedByteArray)[index] != 0:
		return FOGGED
	return SHROUDED


func state_at_cell(team: int, cx: int, cy: int) -> int:
	if not _look.has(team):
		return SHROUDED
	var index := _flat(cx, cy)
	if index < 0:
		return SHROUDED
	if (_look[team] as PackedInt32Array)[index] > 0:
		return CLEAR
	if (_explored[team] as PackedByteArray)[index] != 0:
		return FOGGED
	return SHROUDED


func is_visible(team: int, position: Vector2) -> bool:
	return state_at(team, position) == CLEAR


func is_explored(team: int, position: Vector2) -> bool:
	return state_at(team, position) != SHROUDED


func visibility_alpha(team: int, position: Vector2) -> int:
	## The retail byte, in retail's inverted convention (255 = fully visible).
	match state_at(team, position):
		CLEAR:
			return RETAIL_CLEAR_ALPHA
		FOGGED:
			return RETAIL_FOG_ALPHA
		_:
			return RETAIL_SHROUD_ALPHA


static func alpha_for_state(state: int) -> int:
	match state:
		CLEAR:
			return RETAIL_CLEAR_ALPHA
		FOGGED:
			return RETAIL_FOG_ALPHA
		_:
			return RETAIL_SHROUD_ALPHA


# --- Script verbs -----------------------------------------------------------


func reveal(
	team: int, center: Vector2, radius: float, permanent: bool, reveal_name: String = ""
) -> void:
	if not _ensure_team(team):
		return
	if permanent:
		_permanent_reveals.append({
			"name": reveal_name,
			"team": team,
			"center": center,
			"radius": radius,
		})
		var permanent_mask: PackedByteArray = _permanent[team]
		for index in _cells_in_circle(center, radius):
			permanent_mask[index] = 1
	_stamp(team, center, radius)


func shroud(team: int, center: Vector2, radius: float) -> void:
	## MAP_SHROUD_AT_WAYPOINT. Retail's UIName calls it "add fog", but the fog
	## state is the REMEMBERED state, and a script that re-shrouds ground is
	## asking for the ground to be unknown again - so this clears the explored
	## bit too, taking the cells all the way back to SHROUDED. Cells held by a
	## permanent reveal are exempt, which is what makes "permanent" mean
	## anything.
	if not _look.has(team):
		return
	var looks: PackedInt32Array = _look[team]
	var explored: PackedByteArray = _explored[team]
	var permanent_mask: PackedByteArray = _permanent[team]
	for index in _cells_in_circle(center, radius):
		if permanent_mask[index] != 0:
			continue
		looks[index] = 0
		explored[index] = 0


func undo_permanent_reveal(team: int, center: Vector2, radius: float) -> void:
	## MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT in its positional form: drops
	## every permanent record whose centre falls inside the named circle.
	if not _look.has(team):
		return
	var kept: Array = []
	var dropped := false
	for record_value in _permanent_reveals:
		var record: Dictionary = record_value
		if int(record["team"]) == team \
				and Vector2(record["center"]).distance_to(center) <= maxf(0.0, radius):
			dropped = true
			continue
		kept.append(record)
	if not dropped:
		return
	_permanent_reveals = kept
	_rebuild_permanent(team)


func undo_permanent_reveal_named(team: int, reveal_name: String) -> bool:
	## The named form retail actually uses. Returns false when no reveal by that
	## name is held, so a script that undoes a reveal it never made is a visible
	## no-op rather than a silent one.
	var kept: Array = []
	var dropped := false
	for record_value in _permanent_reveals:
		var record: Dictionary = record_value
		if int(record["team"]) == team and String(record["name"]) == reveal_name:
			dropped = true
			continue
		kept.append(record)
	if not dropped:
		return false
	_permanent_reveals = kept
	_rebuild_permanent(team)
	return true


func _rebuild_permanent(team: int) -> void:
	if not _permanent.has(team):
		return
	var permanent_mask: PackedByteArray = _permanent[team]
	permanent_mask.fill(0)
	var looks: PackedInt32Array = _look[team]
	var touched: PackedInt32Array = _touched[team]
	# Dropping a permanent reveal must drop the clear it was holding, or the
	# undo would be invisible until the next look pass. Explored survives: the
	# ground WAS seen.
	looks.fill(0)
	_touched[team] = PackedInt32Array()
	touched = _touched[team]
	for record_value in _permanent_reveals:
		var record: Dictionary = record_value
		if int(record["team"]) != team:
			continue
		for index in _cells_in_circle(Vector2(record["center"]), float(record["radius"])):
			permanent_mask[index] = 1
	_stamp_permanent_for(team)


func _stamp_permanent_for(team: int) -> void:
	for record_value in _permanent_reveals:
		var record: Dictionary = record_value
		if int(record["team"]) != team:
			continue
		_stamp(team, Vector2(record["center"]), float(record["radius"]))


func reveal_all(team: int, permanent: bool) -> void:
	## MAP_REVEAL_ALL / MAP_REVEAL_ALL_PERM. The whole-map extent expressed as a
	## circle that contains the grid, so it goes through exactly the same stamp
	## as every other reveal.
	var half := Vector2(float(cells_x), float(cells_y)) * cell_size * 0.5
	reveal(team, origin + half, half.length() + cell_size, permanent, "__reveal_all__")


func shroud_all(team: int) -> void:
	## MAP_SHROUD_ALL.
	var half := Vector2(float(cells_x), float(cells_y)) * cell_size * 0.5
	shroud(team, origin + half, half.length() + cell_size)


# --- Presentation feed ------------------------------------------------------


func alpha_image(team: int) -> Image:
	## One byte per cell in retail's convention (255 clear, 127 fog, 0 shroud),
	## ready to upload as an L8 texture and sample by world XZ with linear
	## filtering - which is how retail gets a smooth edge out of a 40-unit grid.
	var image := Image.create_empty(maxi(1, cells_x), maxi(1, cells_y), false, Image.FORMAT_L8)
	if not _look.has(team):
		image.fill(Color(0.0, 0.0, 0.0, 1.0))
		return image
	var looks: PackedInt32Array = _look[team]
	var explored: PackedByteArray = _explored[team]
	var bytes := PackedByteArray()
	bytes.resize(cells_x * cells_y)
	for index in range(cells_x * cells_y):
		if looks[index] > 0:
			bytes[index] = RETAIL_CLEAR_ALPHA
		elif explored[index] != 0:
			bytes[index] = RETAIL_FOG_ALPHA
		else:
			bytes[index] = RETAIL_SHROUD_ALPHA
	return Image.create_from_data(cells_x, cells_y, false, Image.FORMAT_L8, bytes)


func grid_bounds() -> Rect2:
	return Rect2(origin, Vector2(float(cells_x), float(cells_y)) * cell_size)


# --- Serialization (determinism proof, save games) --------------------------


func to_state() -> Dictionary:
	var looks := {}
	var explored := {}
	var permanent := {}
	for team in _look.keys():
		looks[team] = (_look[team] as PackedInt32Array).duplicate()
		explored[team] = (_explored[team] as PackedByteArray).duplicate()
		permanent[team] = (_permanent[team] as PackedByteArray).duplicate()
	return {
		"cell_size": cell_size,
		"origin": origin,
		"cells_x": cells_x,
		"cells_y": cells_y,
		"border_shroud": border_shroud,
		"look": looks,
		"explored": explored,
		"permanent": permanent,
		"permanent_reveals": _permanent_reveals.duplicate(true),
	}


func from_state(state: Dictionary) -> void:
	cell_size = float(state.get("cell_size", cell_size))
	origin = Vector2(state.get("origin", origin))
	cells_x = int(state.get("cells_x", cells_x))
	cells_y = int(state.get("cells_y", cells_y))
	border_shroud = bool(state.get("border_shroud", true))
	_look.clear()
	_explored.clear()
	_permanent.clear()
	_touched.clear()
	for team in (state.get("look", {}) as Dictionary).keys():
		_look[team] = ((state["look"] as Dictionary)[team] as PackedInt32Array).duplicate()
		_explored[team] = (
			(state.get("explored", {}) as Dictionary).get(team, PackedByteArray())
			as PackedByteArray
		).duplicate()
		_permanent[team] = (
			(state.get("permanent", {}) as Dictionary).get(team, PackedByteArray())
			as PackedByteArray
		).duplicate()
		# The touched list is a clearing optimisation, not state. A restored
		# grid rebuilds it from the look counts so the next begin_look_pass
		# clears exactly what a live grid would have cleared.
		var rebuilt := PackedInt32Array()
		var looks: PackedInt32Array = _look[team]
		for index in range(looks.size()):
			if looks[index] > 0:
				rebuilt.append(index)
		_touched[team] = rebuilt
	_permanent_reveals = (state.get("permanent_reveals", []) as Array).duplicate(true)
