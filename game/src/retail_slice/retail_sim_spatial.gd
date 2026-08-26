extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Spatial index carved out of retail_slice_sim.gd (drawer 21): cell hashing, rebuild/sync, hostile gathering, nearest-hostile ring search, structure spatial index.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _spatial_axis_cell(value: float) -> int:
	return clampi(floori(value / sim.SPATIAL_CELL_SIZE), -sim.SPATIAL_CELL_LIMIT, sim.SPATIAL_CELL_LIMIT)


func _spatial_key(cx: int, cy: int) -> int:
	return cx * sim.SPATIAL_CELL_STRIDE + cy


func _spatial_rebuild() -> void:
	## Full O(n) rebuild. Runs once per tick before anything queries, so the
	## index cannot drift from `sim.entities` across restore/spawn/despawn seams.
	sim._spatial_cells.clear()
	sim._spatial_entity_cell.clear()
	sim._spatial_entity_team.clear()
	sim._spatial_team_box.clear()
	sim._spatial_hostile_teams.clear()
	for key in sim.entities.keys():
		var row = sim.entities[key] as Dictionary
		if not bool(row.get("presentation_hidden", false)):
			_spatial_sync(row)


func _spatial_sync(row: Dictionary) -> void:
	## File `row` under the cell its current position falls in, moving it out of
	## its previous cell if it changed. Called after every position write.
	var id := int(row.get("id", 0))
	if id == 0:
		return
	var team := int(row.get("team", -1))
	var position := Vector2(row.get("position", Vector2.ZERO))
	var cx := _spatial_axis_cell(position.x)
	var cy := _spatial_axis_cell(position.y)
	var key := _spatial_key(cx, cy)
	var previous: Variant = sim._spatial_entity_cell.get(id)
	if previous != null:
		var previous_team = int(sim._spatial_entity_team.get(id, team))
		if int(previous) == key and previous_team == team:
			return
		var previous_cells: Dictionary = sim._spatial_cells.get(previous_team, {}) as Dictionary
		var old_bucket: Array = previous_cells.get(int(previous), []) as Array
		old_bucket.erase(id)
		if old_bucket.is_empty():
			previous_cells.erase(int(previous))
	if not sim._spatial_cells.has(team):
		sim._spatial_cells[team] = {}
		# A team appearing for the first time (first summon, first creep spawn)
		# invalidates the cached hostile-team lists: a battalion stepped later in
		# this same tick must be able to acquire it, exactly as the old full scan
		# over `sim.entities` would have.
		sim._spatial_hostile_teams.clear()
	var cells: Dictionary = sim._spatial_cells[team]
	if not cells.has(key):
		cells[key] = []
	(cells[key] as Array).append(id)
	sim._spatial_entity_cell[id] = key
	sim._spatial_entity_team[id] = team
	var box: Variant = sim._spatial_team_box.get(team)
	if box == null:
		sim._spatial_team_box[team] = [cx, cx, cy, cy]
	else:
		var extents: Array = box as Array
		extents[0] = mini(int(extents[0]), cx)
		extents[1] = maxi(int(extents[1]), cx)
		extents[2] = mini(int(extents[2]), cy)
		extents[3] = maxi(int(extents[3]), cy)


func _spatial_hostile_team_list(team: int) -> Array:
	## Teams hostile to `team` that currently have indexed battalions. Cached per
	## rebuild; sim._is_hostile() is then paid once per team rather than per candidate.
	var cached: Variant = sim._spatial_hostile_teams.get(team)
	if cached != null:
		return cached as Array
	var result: Array = []
	for other_value in sim._spatial_cells.keys():
		var other := int(other_value)
		if sim._is_hostile(team, other):
			result.append(other)
	result.sort()
	sim._spatial_hostile_teams[team] = result
	return result


func _spatial_gather(point: Vector2, radius: float) -> Array[int]:
	## Every indexed id, on any team, whose cell overlaps the axis-aligned box
	## around the disc. A conservative superset: callers re-apply the exact
	## distance test. The returned order is unspecified - sort it when order is
	## observable.
	var result: Array[int] = []
	if radius < 0.0:
		return result
	for team_value in sim._spatial_cells.keys():
		var box: Variant = sim._spatial_team_box.get(int(team_value))
		if box == null:
			continue
		var extents: Array = box as Array
		var low_cx := maxi(_spatial_axis_cell(point.x - radius), int(extents[0]))
		var high_cx := mini(_spatial_axis_cell(point.x + radius), int(extents[1]))
		var low_cy := maxi(_spatial_axis_cell(point.y - radius), int(extents[2]))
		var high_cy := mini(_spatial_axis_cell(point.y + radius), int(extents[3]))
		var cells: Dictionary = sim._spatial_cells[int(team_value)]
		for cx in range(low_cx, high_cx + 1):
			for cy in range(low_cy, high_cy + 1):
				var bucket: Variant = cells.get(_spatial_key(cx, cy))
				if bucket == null:
					continue
				for id in bucket as Array:
					result.append(int(id))
	return result


func _spatial_gather_sorted(point: Vector2, radius: float) -> Array[int]:
	## _spatial_gather() in ascending id order, for callers whose visit order is
	## observable (damage application, event emission, modifier grants).
	var result := _spatial_gather(point, radius)
	result.sort()
	return result


## Broad-phase for _deflect_around_structures (lane L2b item 6). A castle map
## seeds hundreds of live sim.structures (Carn Dum: 260) and the deflection loop
## used to walk ALL of them per moving entity per tick. Structures never move,
## so a spatial bucket index over their centres stays valid until the table is
## mutated; `sim._structures_mutation_serial` is bumped at every mutation site and
## the index rebuilds lazily on the first query after a change.
##
## Exactness contract: the gather returns every structure whose blocking disc
## (radius <= sim.STRUCTURE_DEFLECT_GATHER_RADIUS) can overlap the query point, in
## ascending id order — the same visit order as the old full scan. Any centre
## outside the gathered box is further than the maximum radius away, and the
## deflection loop skips those rows with zero side effects, so the result is
## byte-identical to the full scan.



func _note_structure_table_mutation() -> void:
	sim._structures_mutation_serial += 1


func _structure_spatial_index() -> Dictionary:
	if sim._structure_spatial_serial != sim._structures_mutation_serial:
		sim._structure_spatial_cells.clear()
		for id_value in sim.structures.keys():
			var row: Dictionary = sim.structures[id_value]
			var position := Vector2(row.get("position", Vector2.ZERO))
			var key := _spatial_key(_spatial_axis_cell(position.x), _spatial_axis_cell(position.y))
			if not sim._structure_spatial_cells.has(key):
				sim._structure_spatial_cells[key] = []
			(sim._structure_spatial_cells[key] as Array).append(int(id_value))
		sim._structure_spatial_serial = sim._structures_mutation_serial
	return sim._structure_spatial_cells


func _structure_ids_near(position: Vector2) -> Array[int]:
	return _structure_ids_within_gather_radius(position, sim.STRUCTURE_DEFLECT_GATHER_RADIUS)


func _structure_ids_within_gather_radius(position: Vector2, gather_radius: float) -> Array[int]:
	var index := _structure_spatial_index()
	var low_cx := _spatial_axis_cell(position.x - gather_radius)
	var high_cx := _spatial_axis_cell(position.x + gather_radius)
	var low_cy := _spatial_axis_cell(position.y - gather_radius)
	var high_cy := _spatial_axis_cell(position.y + gather_radius)
	var result: Array[int] = []
	for cx in range(low_cx, high_cx + 1):
		for cy in range(low_cy, high_cy + 1):
			var bucket: Variant = index.get(_spatial_key(cx, cy))
			if bucket == null:
				continue
			for id_value in bucket as Array:
				result.append(int(id_value))
	result.sort()
	return result


## Effectively unbounded search range for callers that scanned every hostile.
## The sweep is still cheap: ring_limit below is clamped to the union of the
## hostile teams' occupied cell boxes, so this bounds the tie-break, not the work.


func _spatial_nearest_hostile(
	source: Dictionary, team: int, origin: Vector2, limit: float, filters: int,
	prefer_lowest_id: bool = false
) -> int:
	## Nearest living hostile battalion within `limit` of `origin`, reproducing
	## the old full scan exactly.
	##
	## The old scans walked ascending ids with `if distance <= best`, so the
	## winner is the minimum distance with the HIGHEST id among exact ties, and
	## `best` starting at `limit` means a candidate at exactly `limit` is
	## accepted. The update rule below encodes that as a total order, which makes
	## the result independent of visit order and therefore safe to compute from
	## an expanding ring sweep.
	if limit <= 0.0:
		return 0
	var hostile_teams := _spatial_hostile_team_list(team)
	if hostile_teams.is_empty():
		return 0
	# Union of the hostile teams' occupied boxes: nothing outside it can match,
	# so the sweep is clipped to it and the ring count is capped by it.
	var box_min_cx := 0
	var box_max_cx := -1
	var box_min_cy := 0
	var box_max_cy := -1
	for team_value in hostile_teams:
		var extents: Array = sim._spatial_team_box.get(int(team_value), []) as Array
		if extents.is_empty():
			continue
		if box_max_cx < box_min_cx:
			box_min_cx = int(extents[0])
			box_max_cx = int(extents[1])
			box_min_cy = int(extents[2])
			box_max_cy = int(extents[3])
		else:
			box_min_cx = mini(box_min_cx, int(extents[0]))
			box_max_cx = maxi(box_max_cx, int(extents[1]))
			box_min_cy = mini(box_min_cy, int(extents[2]))
			box_max_cy = maxi(box_max_cy, int(extents[3]))
	if box_max_cx < box_min_cx:
		return 0

	var best_id := 0
	var best_distance := limit
	var origin_cx := _spatial_axis_cell(origin.x)
	var origin_cy := _spatial_axis_cell(origin.y)
	# Hoisted filter state: _can_engage_battalion() only consults the candidate's
	# `flying` flag when the source is a melee attacker, and sim._stealth_active() is
	# a tick comparison. Both are resolved once here so the inner loop over
	# candidates makes no function calls beyond the distance itself.
	var reject_flyers = (filters & sim.SPATIAL_FILTER_NOT_FLYING) != 0
	if (filters & sim.SPATIAL_FILTER_ENGAGE) != 0 and sim._is_melee_attacker(source):
		reject_flyers = true
	var check_stealth = (filters & sim.SPATIAL_FILTER_STEALTH) != 0
	# Rings beyond this are entirely outside `limit` or outside the occupied box.
	var ring_limit = floori(limit / sim.SPATIAL_CELL_SIZE) + 2
	ring_limit = mini(ring_limit, maxi(
		maxi(absi(origin_cx - box_min_cx), absi(origin_cx - box_max_cx)),
		maxi(absi(origin_cy - box_min_cy), absi(origin_cy - box_max_cy))
	))
	for ring in range(0, ring_limit + 1):
		# Every point of a ring-`ring` cell is at least (ring - 1) * cell away
		# from `origin`, so once that floor passes the best distance found, no
		# further ring can contain a candidate that wins the tie-break.
		if ring > 0 and float(ring - 1) * sim.SPATIAL_CELL_SIZE > best_distance:
			break
		var offsets := _spatial_ring_offsets(ring)
		var offset_index := 0
		var offset_count := offsets.size()
		while offset_index < offset_count:
			var cx: int = origin_cx + offsets[offset_index]
			var cy: int = origin_cy + offsets[offset_index + 1]
			offset_index += 2
			if cx < box_min_cx or cx > box_max_cx:
				continue
			if cy < box_min_cy or cy > box_max_cy:
				continue
			var cell_key := _spatial_key(cx, cy)
			for team_value in hostile_teams:
				var bucket: Variant = (sim._spatial_cells[int(team_value)] as Dictionary).get(cell_key)
				if bucket == null:
					continue
				for id_value in bucket as Array:
					var candidate := int(id_value)
					var candidate_row: Variant = sim.entities.get(candidate)
					if candidate_row == null:
						continue
					var candidate_dict: Dictionary = candidate_row
					if int(candidate_dict.get("health", 0)) <= 0:
						continue
					if reject_flyers and bool(candidate_dict.get("flying", false)):
						continue
					var distance := origin.distance_to(Vector2(candidate_dict.get("position", Vector2.ZERO)))
					if check_stealth and sim._stealth_active(candidate_dict):
						var detection_source := float(candidate_dict.get("invisibility_detection_range_source", -1.0))
						var detection_range = detection_source * float(sim._rules.get("source_unit_scale", 0.1))
						if detection_source < 0.0 or distance > detection_range:
							continue
					var wins := distance < best_distance
					if not wins and distance == best_distance:
						# Exact equality, never is_equal_approx: a tolerance
						# comparison is not transitive, so it cannot define the
						# total order a ring sweep needs.
						wins = (candidate < best_id or best_id == 0) if prefer_lowest_id else (candidate > best_id)
					if wins:
						best_distance = distance
						best_id = candidate
	return best_id


func _spatial_ring_offsets(ring: int) -> PackedInt32Array:
	## Cell offsets at Chebyshev distance exactly `ring`, as interleaved dx/dy.
	## Walked as a perimeter so the sweep stays O(ring) per ring rather than
	## O(ring^2), and cached because the table never varies.
	while sim._spatial_ring_cache.size() <= ring:
		var index = sim._spatial_ring_cache.size()
		var offsets := PackedInt32Array()
		if index == 0:
			offsets.append(0)
			offsets.append(0)
		else:
			for dx in range(-index, index + 1):
				offsets.append(dx)
				offsets.append(-index)
				offsets.append(dx)
				offsets.append(index)
			for dy in range(-index + 1, index):
				offsets.append(-index)
				offsets.append(dy)
				offsets.append(index)
				offsets.append(dy)
		sim._spatial_ring_cache.append(offsets)
	return sim._spatial_ring_cache[ring]


