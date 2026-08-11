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
##
## THE LOOK PASS IS INCREMENTAL, and it has to be. The first cut of this module
## cleared and re-stamped every looker's disc every tick. Measured on the slice
## battlefield - a 183x183 grid, 120 lookers, a compiled 470-source deshroud
## range - that full rebuild cost 19.5 MILLISECONDS PER TICK (13.8ms of it in
## `_cells_in_circle` alone), which at 30Hz is 58% of a core spent on fog before
## a single frame is drawn. The owner's first fog-on playtest reported exactly
## that: "fog of war is laggy on start, super laggy".
##
## So a looker is now a KEYED, REFCOUNTED registration:
##
##   * `add_look(team, centre, radius, key)` upserts. If the looker has not
##     changed CELL and has not changed radius since the last pass, the call
##     does nothing at all - which is the steady state for a stationary army.
##   * Moving out of a cell unstamps the old disc (-1) and stamps the new (+1).
##   * `commit_look_pass()` unstamps every keyed looker the pass did not refresh,
##     which is how a dead unit stops seeing.
##
## The disc is therefore quantised to the looker's CELL rather than its exact
## position, and that is retail's own model rather than a shortcut: SAGE keeps
## the shroud ON the partition cell array and re-evaluates an object's look when
## the object changes partition cell (OpenSAGE PartitionCellManager tracks a
## per-object cell and reacts to `OnObjectMoved`). The quantisation moves an
## edge by at most half a cell - 20 source units on an 800-unit fortress
## deshroud - and the reached set is still the exact circle-versus-cell-rect
## test, just taken from the cell centre.
##
## The template that test collapses to is worth writing down, because it is why
## a stamp is now three array operations per cell and no floating point at all.
## In cell units with the disc centred on cell (0,0)'s centre, the nearest point
## of cell (dx,dy) is |dx|-0.5 away in x (0 when dx is 0), so the row test is
## just `|dy| <= rc + 0.5` and the span is `|dx| <= sqrt(rc^2 - ndy^2) + 0.5`.
## `_disc_spans()` caches those spans per radius; `_stamp_cells()` walks them,
## and because every row is one contiguous interval, a looker that steps one
## cell is re-stamped by `_restamp_moved` as a per-row interval difference -
## about fifty cells rather than the nine hundred and sixty two a clear-and-
## refill would touch.
##
## The presentation feed is maintained the same way. Rebuilding the L8 alpha
## image and the RGBA radar layer with a per-cell GDScript loop cost 2.4ms and
## 4.2ms respectively EVERY rebuild (7.5 times a second). Both byte planes are
## now written by `_stamp_cells` for the cells that actually changed, so
## `alpha_image()` and `radar_image()` are C++ memcpys, and `revision()` lets the
## overlay skip the upload entirely when nothing moved.


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
## team -> PackedByteArray, one entry per cell: the retail visibility byte
## (255 clear / 127 fog / 0 shroud), kept in step with `_look`/`_explored` by
## `_stamp_cells` so the presentation never has to walk the grid to build it.
var _alpha: Dictionary = {}
## team -> PackedByteArray, four entries per cell: the radar's darkening layer,
## black with alpha = 255 - visibility. Same reason as `_alpha`.
var _radar: Dictionary = {}
## team -> int, bumped whenever any cell's visibility byte changed. The overlay
## compares it and skips the texture upload when nothing moved.
var _revision: Dictionary = {}
## team -> PackedByteArray: cells held clear by a permanent reveal.
var _permanent: Dictionary = {}

## Keyed lookers, insertion-ordered: key -> {team, cx, cy, radius, seen}. A key
## is whatever the caller uses to mean "the same looker as last pass" (the sim
## uses the entity id, and the negated structure id). Key 0 is reserved for
## UNKEYED looks, which are transient: stamped immediately and dropped by the
## next `begin_look_pass`, which is what a one-shot script reveal wants.
var _lookers: Dictionary = {}
## Transient (unkeyed) stamps live for exactly one pass: [team, cx, cy, radius].
var _transient: Array = []
## radius bucket -> PackedInt32Array of (dy, dx_min, dx_max) triples.
var _disc_row_cache: Dictionary = {}
## Set by `from_state`: the restored look counts are owned by lookers that no
## longer exist, so the next pass drops them before stamping. See `from_state`.
var _looks_stale := false
## Monotonic look-pass counter; a looker whose `seen` is not this number was not
## refreshed by the current pass and is dropped by `commit_look_pass`.
var _pass_index := 0

## The four planes of ONE team, bound by `_bind_team` for the duration of a
## stamp. `_apply_span` runs once per row of a disc - twenty five times for a
## ranger, four times that for a moved looker - and fetching the planes out of
## their dictionaries inside it made the row loop pay four dictionary lookups
## and four Variant casts to write two bytes. Binding once per stamp instead
## took the all-moving worst case from 6.9ms a tick to well under two.
var _bound_team := -1
var _bound_look := PackedInt32Array()
var _bound_explored := PackedByteArray()
var _bound_alpha := PackedByteArray()
var _bound_radar := PackedByteArray()


func _bind_team(team: int) -> bool:
	if _bound_team == team:
		return true
	if not _ensure_team(team):
		return false
	# Shared by reference with the dictionaries, not copied: a Godot packed array
	# held in a container is one refcounted buffer, so writes through this
	# binding land in the grid the queries read.
	_bound_look = _look[team]
	_bound_explored = _explored[team]
	_bound_alpha = _alpha[team]
	_bound_radar = _radar[team]
	_bound_team = team
	return true


func _unbind_team() -> void:
	## Any wholesale replacement of a team's planes must drop the binding, or a
	## later stamp would write through a stale buffer.
	_bound_team = -1
	_bound_look = PackedInt32Array()
	_bound_explored = PackedByteArray()
	_bound_alpha = PackedByteArray()
	_bound_radar = PackedByteArray()
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
	_alpha.clear()
	_radar.clear()
	_revision.clear()
	_permanent.clear()
	_permanent_reveals.clear()
	_lookers.clear()
	_transient.clear()
	# The row spans are in CELL units, so a new cell size invalidates them.
	_disc_row_cache.clear()
	_unbind_team()


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
	##
	## The disc is taken from the CENTRE of the cell that contains `center`, so
	## this enumerates exactly the cells `_stamp_cells` writes. There is one
	## geometry in this module and this is it - a second, exact-position variant
	## would let the permanent-reveal MASK and the stamp that fills it disagree
	## about the rim of the same circle.
	var out := PackedInt32Array()
	var base := cell_index(center)
	var spans := _disc_spans(radius)
	var dy_max := spans[0]
	for dy in range(-dy_max, dy_max + 1):
		var cy := base.y + dy
		if cy < 0 or cy >= cells_y:
			continue
		var half := spans[1 + dy + dy_max]
		var x0 := maxi(0, base.x - half)
		var x1 := mini(cells_x - 1, base.x + half)
		if x0 > x1:
			continue
		var row_base := cy * cells_x
		for index in range(row_base + x0, row_base + x1 + 1):
			out.append(index)
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
	var alpha := PackedByteArray()
	alpha.resize(count)
	alpha.fill(RETAIL_SHROUD_ALPHA)
	# Black everywhere, fully opaque: an unlooked grid hides the whole radar.
	var radar := PackedByteArray()
	radar.resize(count * 4)
	radar.fill(0)
	for index in range(count):
		radar[index * 4 + 3] = 255 - RETAIL_SHROUD_ALPHA
	_look[team] = looks
	_explored[team] = explored
	_permanent[team] = permanent
	_alpha[team] = alpha
	_radar[team] = radar
	_revision[team] = 0
	return true


func teams() -> Array:
	return _look.keys()


func revision(team: int) -> int:
	## Bumped once per stamp that changed at least one cell's visibility byte.
	## Presentation-only: the overlay skips its texture upload when this has not
	## moved since the last one, which is most frames of most matches.
	return int(_revision.get(team, 0))


# --- The disc template ------------------------------------------------------


func _disc_spans(radius: float) -> PackedInt32Array:
	## The disc of `radius` centred on the CENTRE of cell (0,0), as one half-span
	## per row: `[dy_max, m(-dy_max), ..., m(0), ..., m(dy_max)]`, where the disc
	## covers exactly `|dx| <= m(dy)` on row `dy`.
	##
	## See the class comment: with the disc on a cell centre the exact
	## circle-versus-cell-rectangle test collapses to |dy| <= rc + 0.5 for the
	## row and |dx| <= sqrt(rc^2 - ndy^2) + 0.5 for the span, where
	## ndy = max(0, |dy| - 0.5). No per-cell floating point survives into the
	## stamp, and because every row is a contiguous interval a MOVED looker can
	## be re-stamped as a per-row interval difference rather than a full clear
	## and refill (`_restamp_moved`).
	##
	## Bucketed to 1/256 of a cell so that float noise in a compiled range
	## cannot grow the cache without bound; a 256th of a 40-source-unit cell is
	## 0.16 source units, far below anything retail authors.
	var rc := maxf(0.0, radius) / cell_size
	var bucket := int(round(rc * 256.0))
	if _disc_row_cache.has(bucket):
		return _disc_row_cache[bucket]
	var quantised := float(bucket) / 256.0
	var dy_max := int(floor(quantised + 0.5))
	var spans := PackedInt32Array()
	spans.resize(2 + 2 * dy_max)
	spans[0] = dy_max
	for dy in range(-dy_max, dy_max + 1):
		var ndy := maxf(0.0, absf(float(dy)) - 0.5)
		var span_squared := quantised * quantised - ndy * ndy
		# `dy_max` is floor(rc + 0.5), so ndy never exceeds rc and this is never
		# negative; clamped anyway rather than trusting float rounding at the rim.
		var dx_max := 0
		if span_squared > 0.0:
			dx_max = int(floor(sqrt(span_squared) + 0.5))
		spans[1 + dy + dy_max] = dx_max
	_disc_row_cache[bucket] = spans
	return spans


# --- The per-tick look pass -------------------------------------------------


func begin_look_pass() -> void:
	## Marks every keyed looker stale and drops last pass's transient stamps.
	## Explored is NOT touched: that is the whole point of the fog state - the
	## ground stays remembered.
	if _looks_stale:
		_looks_stale = false
		for team in _look.keys():
			_reset_team_looks(int(team))
	for stamp_value in _transient:
		var stamp: Array = stamp_value
		_stamp_cells(int(stamp[0]), int(stamp[1]), int(stamp[2]), float(stamp[3]), -1)
	_transient.clear()
	# Staleness is a PASS NUMBER, not a flag swept over every looker: a looker is
	# stale when the number it last recorded is not the current one. Sweeping the
	# flag cost 355us a tick on a 120-looker battlefield, for bookkeeping that
	# `commit_look_pass` has to walk the same set to act on anyway.
	_pass_index += 1


func add_look(team: int, center: Vector2, radius: float, key: int = 0) -> void:
	## `key` identifies the looker across passes. A non-zero key makes the look
	## INCREMENTAL: a looker that has not changed cell or radius since the last
	## pass costs one dictionary lookup and two comparisons, and nothing is
	## touched. Key 0 is a transient one-shot stamp that `begin_look_pass` drops.
	if not _ensure_team(team):
		return
	var cell := cell_index(center)
	if key == 0:
		_stamp_cells(team, cell.x, cell.y, radius, 1)
		_transient.append([team, cell.x, cell.y, radius])
		return
	var existing: Variant = _lookers.get(key)
	if existing != null:
		var record: Dictionary = existing
		if int(record["team"]) == team \
				and int(record["cx"]) == cell.x and int(record["cy"]) == cell.y \
				and is_equal_approx(float(record["radius"]), radius):
			record["seen"] = _pass_index
			return
		if int(record["team"]) == team and is_equal_approx(float(record["radius"]), radius):
			# Same army, same range, one cell to the left: the two discs overlap
			# almost entirely, so only the RIM moves. Re-stamping both in full
			# would touch 962 cells for a one-cell step that actually changes 50.
			_restamp_moved(
				team, int(record["cx"]), int(record["cy"]), cell.x, cell.y, radius
			)
			record["cx"] = cell.x
			record["cy"] = cell.y
			record["seen"] = _pass_index
			return
		_stamp_cells(
			int(record["team"]), int(record["cx"]), int(record["cy"]),
			float(record["radius"]), -1
		)
	_stamp_cells(team, cell.x, cell.y, radius, 1)
	_lookers[key] = {
		"team": team, "cx": cell.x, "cy": cell.y, "radius": radius,
		"seen": _pass_index,
	}


func commit_look_pass() -> void:
	## Drops every keyed looker the pass did not refresh - a dead unit, a razed
	## building - by unstamping exactly what it had stamped. Permanent reveals
	## are lookers with their own keys and are never dropped here, so a script
	## reveal still cannot be erased by the unit that walked out of it.
	var stale: Array = []
	for key in _lookers.keys():
		var record: Dictionary = _lookers[key]
		# A permanent reveal is never stale. It is a looker so that it holds a
		# refcount of its own, not so that it can expire.
		if bool(record.get("permanent", false)):
			continue
		if int(record["seen"]) != _pass_index:
			stale.append(key)
	for key in stale:
		var record: Dictionary = _lookers[key]
		_stamp_cells(
			int(record["team"]), int(record["cx"]), int(record["cy"]),
			float(record["radius"]), -1
		)
		_lookers.erase(key)


func _stamp_cells(team: int, cx: int, cy: int, radius: float, delta: int) -> void:
	## The whole hot path. `delta` is +1 to add a looker and -1 to remove one;
	## the look count is a refcount, so a cell covered by two armies survives
	## one of them leaving. Every write that changes the cell's VISIBILITY also
	## updates the two presentation byte planes, so nothing downstream has to
	## walk the grid.
	if not _bind_team(team):
		return
	var spans := _disc_spans(radius)
	var dy_max := spans[0]
	var changed := false
	for dy in range(-dy_max, dy_max + 1):
		var half := spans[1 + dy + dy_max]
		if _apply_span(cy + dy, cx - half, cx + half, delta):
			changed = true
	if changed:
		_revision[team] = int(_revision.get(team, 0)) + 1


func _restamp_moved(
	team: int, old_cx: int, old_cy: int, new_cx: int, new_cy: int, radius: float
) -> void:
	## A looker that changed cell without changing team or radius. Both discs
	## have the SAME row profile, and every row of a disc is one contiguous
	## interval, so per row the work is at most two interval differences: the
	## part of the old row the new row no longer covers (release) and the part of
	## the new row the old row did not cover (acquire). For the one-cell step a
	## locomotor actually takes that is two cells per row instead of two full
	## rows.
	if not _bind_team(team):
		return
	var spans := _disc_spans(radius)
	var dy_max := spans[0]
	var changed := false
	for y in range(mini(old_cy, new_cy) - dy_max, maxi(old_cy, new_cy) + dy_max + 1):
		var old_dy := y - old_cy
		var new_dy := y - new_cy
		var has_old := absi(old_dy) <= dy_max
		var has_new := absi(new_dy) <= dy_max
		if not has_old and not has_new:
			continue
		if not has_new:
			var released := spans[1 + old_dy + dy_max]
			if _apply_span(y, old_cx - released, old_cx + released, -1):
				changed = true
			continue
		if not has_old:
			var acquired := spans[1 + new_dy + dy_max]
			if _apply_span(y, new_cx - acquired, new_cx + acquired, 1):
				changed = true
			continue
		var old_half := spans[1 + old_dy + dy_max]
		var new_half := spans[1 + new_dy + dy_max]
		var old_x0 := old_cx - old_half
		var old_x1 := old_cx + old_half
		var new_x0 := new_cx - new_half
		var new_x1 := new_cx + new_half
		if old_x1 < new_x0 or new_x1 < old_x0:
			# The two rows do not even touch: no difference to take.
			if _apply_span(y, old_x0, old_x1, -1):
				changed = true
			if _apply_span(y, new_x0, new_x1, 1):
				changed = true
			continue
		# Overlapping intervals: [old \ new] on each side, then [new \ old].
		if old_x0 < new_x0 and _apply_span(y, old_x0, new_x0 - 1, -1):
			changed = true
		if old_x1 > new_x1 and _apply_span(y, new_x1 + 1, old_x1, -1):
			changed = true
		if new_x0 < old_x0 and _apply_span(y, new_x0, old_x0 - 1, 1):
			changed = true
		if new_x1 > old_x1 and _apply_span(y, old_x1 + 1, new_x1, 1):
			changed = true
	if changed:
		_revision[team] = int(_revision.get(team, 0)) + 1


func _apply_span(y: int, x0: int, x1: int, delta: int) -> bool:
	## One contiguous run of cells on one row of the team bound by `_bind_team`.
	## Returns true when at least one cell's VISIBILITY changed, which is what
	## the revision counter tracks - a refcount going from 2 to 3 changes nothing
	## anyone can see.
	if y < 0 or y >= cells_y:
		return false
	var lo := maxi(0, x0)
	var hi := mini(cells_x - 1, x1)
	if lo > hi:
		return false
	var looks := _bound_look
	var explored := _bound_explored
	var alpha := _bound_alpha
	var radar := _bound_radar
	var row_base := y * cells_x
	var changed := false
	for index in range(row_base + lo, row_base + hi + 1):
		var before := looks[index]
		# Clamped at zero. The refcount invariant is maintained by
		# `_reset_team_looks` dropping every outstanding stamp whenever a script
		# verb zeroes the counts wholesale, so an underflow should be
		# unreachable - but a shipped grid that went negative would pin a cell
		# CLEAR forever, and that is not a failure worth being purist about.
		var after := maxi(0, before + delta)
		looks[index] = after
		if after > 0:
			if before <= 0:
				explored[index] = 1
				alpha[index] = RETAIL_CLEAR_ALPHA
				radar[index * 4 + 3] = 255 - RETAIL_CLEAR_ALPHA
				changed = true
		elif before > 0:
			# The last looker left: retail falls back to FOGGED, never to
			# SHROUDED. Only an explicit script shroud reshrouds ground.
			alpha[index] = RETAIL_FOG_ALPHA
			radar[index * 4 + 3] = 255 - RETAIL_FOG_ALPHA
			changed = true
	return changed


func _stamp(team: int, center: Vector2, radius: float) -> void:
	## Transient one-shot stamp used by the script reveal verbs.
	add_look(team, center, radius, 0)


# --- Queries ----------------------------------------------------------------


static func state_for_alpha(alpha_byte: int) -> int:
	## The visibility byte is the single source of truth for a cell's state, so
	## every reader - the model, the overlay's cached fast path, the radar -
	## decodes it here and cannot drift into a fourth answer.
	if alpha_byte >= RETAIL_CLEAR_ALPHA:
		return CLEAR
	if alpha_byte > RETAIL_SHROUD_ALPHA:
		return FOGGED
	return SHROUDED


func state_at(team: int, position: Vector2) -> int:
	if not _alpha.has(team):
		return SHROUDED
	var index := _flat_at(position)
	if index < 0:
		return SHROUDED
	return state_for_alpha((_alpha[team] as PackedByteArray)[index])


func state_at_cell(team: int, cx: int, cy: int) -> int:
	if not _alpha.has(team):
		return SHROUDED
	var index := _flat(cx, cy)
	if index < 0:
		return SHROUDED
	return state_for_alpha((_alpha[team] as PackedByteArray)[index])


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
		# A permanent reveal is a LOOKER that never goes stale, not a stamp that
		# has to be reapplied every commit. It holds its own refcount on every
		# cell it covers, so the unit that walks out of it cannot take the clear
		# with it, and dropping the reveal releases exactly what it held.
		_register_permanent_looker(_permanent_reveals.size() - 1)
		return
	_stamp(team, center, radius)


func _permanent_looker_key(record_index: int) -> String:
	return "perm:%d" % record_index


func _register_permanent_looker(record_index: int) -> void:
	if record_index < 0 or record_index >= _permanent_reveals.size():
		return
	var record: Dictionary = _permanent_reveals[record_index]
	var team := int(record["team"])
	if not _ensure_team(team):
		return
	var center := Vector2(record["center"])
	var cell := cell_index(center)
	var radius := float(record["radius"])
	_stamp_cells(team, cell.x, cell.y, radius, 1)
	_lookers[_permanent_looker_key(record_index)] = {
		"team": team, "cx": cell.x, "cy": cell.y, "radius": radius,
		"seen": _pass_index, "permanent": true,
	}


func _reset_team_looks(team: int) -> void:
	## Wholesale look reset, used by the script verbs that zero the clear set
	## outright (MAP_SHROUD_*, dropping a permanent reveal). Everything holding a
	## refcount on this team's cells is dropped WITHOUT unstamping - the counts
	## are about to be zeroed anyway, and unstamping a zeroed grid is what would
	## corrupt it - and the surviving permanent reveals are then re-stamped.
	## Explored survives: the ground WAS seen.
	if not _look.has(team):
		return
	var looks: PackedInt32Array = _look[team]
	var explored: PackedByteArray = _explored[team]
	var alpha: PackedByteArray = _alpha[team]
	var radar: PackedByteArray = _radar[team]
	looks.fill(0)
	for index in range(looks.size()):
		var value := RETAIL_FOG_ALPHA if explored[index] != 0 else RETAIL_SHROUD_ALPHA
		alpha[index] = value
		radar[index * 4 + 3] = 255 - value
	_revision[team] = int(_revision.get(team, 0)) + 1
	var dropped: Array = []
	for key in _lookers.keys():
		if int((_lookers[key] as Dictionary)["team"]) == team:
			dropped.append(key)
	for key in dropped:
		_lookers.erase(key)
	var kept_transient: Array = []
	for stamp_value in _transient:
		if int((stamp_value as Array)[0]) != team:
			kept_transient.append(stamp_value)
	_transient = kept_transient
	for record_index in range(_permanent_reveals.size()):
		if int((_permanent_reveals[record_index] as Dictionary)["team"]) == team:
			_register_permanent_looker(record_index)


func shroud(team: int, center: Vector2, radius: float) -> void:
	## MAP_SHROUD_AT_WAYPOINT. Retail's UIName calls it "add fog", but the fog
	## state is the REMEMBERED state, and a script that re-shrouds ground is
	## asking for the ground to be unknown again - so this clears the explored
	## bit too, taking the cells all the way back to SHROUDED. Cells held by a
	## permanent reveal are exempt, which is what makes "permanent" mean
	## anything.
	if not _look.has(team):
		return
	var explored: PackedByteArray = _explored[team]
	var permanent_mask: PackedByteArray = _permanent[team]
	for index in _cells_in_circle(center, radius):
		if permanent_mask[index] != 0:
			continue
		explored[index] = 0
	_reset_team_looks(team)


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
	for record_value in _permanent_reveals:
		var record: Dictionary = record_value
		if int(record["team"]) != team:
			continue
		for index in _cells_in_circle(Vector2(record["center"]), float(record["radius"])):
			permanent_mask[index] = 1
	# Dropping a permanent reveal must drop the clear it was holding, or the undo
	# would be invisible until the next look pass. The reset re-registers the
	# survivors; explored survives, because the ground WAS seen.
	_reset_team_looks(team)


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
	##
	## The plane is maintained cell-by-cell by `_stamp_cells`, so this is a
	## memcpy and not a 33,489-iteration GDScript loop. It used to be the loop,
	## and that cost 2.4ms every rebuild.
	if not _alpha.has(team):
		var empty := Image.create_empty(
			maxi(1, cells_x), maxi(1, cells_y), false, Image.FORMAT_L8
		)
		empty.fill(Color(0.0, 0.0, 0.0, 1.0))
		return empty
	return Image.create_from_data(
		cells_x, cells_y, false, Image.FORMAT_L8, _alpha[team] as PackedByteArray
	)


func radar_image(team: int) -> Image:
	## The radar's darkening layer: black, with alpha = 255 - visibility, so the
	## minimap can draw the whole shroud as ONE textured polygon with no shader
	## and no inversion at the draw site. Same reason as `alpha_image` for
	## maintaining it incrementally - building it per rebuild cost 4.2ms.
	if not _radar.has(team):
		var empty := Image.create_empty(
			maxi(1, cells_x), maxi(1, cells_y), false, Image.FORMAT_RGBA8
		)
		empty.fill(Color(0.0, 0.0, 0.0, 1.0))
		return empty
	return Image.create_from_data(
		cells_x, cells_y, false, Image.FORMAT_RGBA8, _radar[team] as PackedByteArray
	)


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
	_alpha.clear()
	_radar.clear()
	_revision.clear()
	_permanent.clear()
	_lookers.clear()
	_transient.clear()
	_disc_row_cache.clear()
	_unbind_team()
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
		# The visibility planes are derived, not serialized: a saved grid carries
		# the counts and the explored bits, and the two byte planes are rebuilt
		# from them once, here, rather than being trusted from a file.
		var looks: PackedInt32Array = _look[team]
		var explored: PackedByteArray = _explored[team]
		var alpha := PackedByteArray()
		alpha.resize(looks.size())
		var radar := PackedByteArray()
		radar.resize(looks.size() * 4)
		radar.fill(0)
		for index in range(looks.size()):
			var value := RETAIL_SHROUD_ALPHA
			if looks[index] > 0:
				value = RETAIL_CLEAR_ALPHA
			elif explored[index] != 0:
				value = RETAIL_FOG_ALPHA
			alpha[index] = value
			radar[index * 4 + 3] = 255 - value
		_alpha[team] = alpha
		_radar[team] = radar
		_revision[team] = 1
	_permanent_reveals = (state.get("permanent_reveals", []) as Array).duplicate(true)
	# THE RESTORED GRID HAS COUNTS BUT NO LOOKERS. Nothing holds the refcounts
	# the file carries, so the next live pass would stamp its lookers ON TOP of
	# them and no cell would ever fall back to fog again. The counts are dropped
	# at the start of the next pass instead - which is exactly what the old
	# rebuilt-touched-list did - and re-earned by the lookers that still exist.
	_looks_stale = not _look.is_empty()
