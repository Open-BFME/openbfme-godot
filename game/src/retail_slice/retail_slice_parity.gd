extends RefCounted

## Authoritative slice subsystems for residual script-world parity:
## FoW grid, pathability ledger, wall upgrades, tactical markers, transport
## capacity helpers, OCL leaf registry, mood map, presentation/meta flags.
##
## IMPORTANT: Do NOT hold a strong back-reference to RetailSliceSim. Both are
## RefCounted; sim->parity->sim cycles leak and can AV after partial free
## (Windows "memory at 0x...58 could not be read"). Methods that need the sim
## take it as an argument.

const FOG_CELL_SIZE := 50.0
const PATH_CELL_SIZE := 25.0
const DEFAULT_TRANSPORT_CAPACITY := {
	"fortress": 8,
	"castle": 6,
	"wall": 0,
	"farm": 0,
	"barracks": 2,
	"archery_range": 2,
	"stable": 2,
	"default": 4,
}

## AI_MOOD-like ints used by TEAM_SET_ATTITUDE (ParamTypes AI_MOOD).
const MOOD_PASSIVE := 0
const MOOD_GUARD := 1
const MOOD_AGGRESSIVE := 2
const MOOD_ATTACK := 3

## team -> "cx,cy" -> true (explored/revealed)
var fog_revealed: Dictionary = {}
## team -> "cx,cy" -> true (permanent reveal; shroud cannot remove)
var fog_permanent: Dictionary = {}
var fog_border_shroud: bool = false

## team -> threat override (script SET_COUNTER style / set_threat_level)
var threat_overrides: Dictionary = {}
## last computed threat answers (query cache; hash-backed consumer trail)
var threat_query_cache: Dictionary = {}

## "cx,cy" -> true means impassable for path queries
var path_impassable: Dictionary = {}
## "cx,cy" -> true means explicitly pathable (overrides default open)
var path_walkable: Dictionary = {}

## marker_type -> Array of {name, position, near:Vector2, far:Vector2}
var tactical_markers: Dictionary = {}

## structure_id -> wall upgrade rows {upgrade, tick}
var wall_upgrades: Dictionary = {}

## OCL id -> converted leaf dictionary (createObjects, etc.)
var ocl_leaves: Dictionary = {}
## object id -> leaf for hatch resolution
var object_leaves: Dictionary = {}
var weapon_leaves: Dictionary = {}

## team -> scoring excluded
var score_excluded: Dictionary = {}
var scoring_enabled: bool = true
var time_frozen: bool = false
var exit_map_requested: bool = false
var living_world_commands: Array = []

## presentation sinks (audio/camera/ui)
var presentation_log: Array = []


func clear() -> void:
	fog_revealed.clear()
	fog_permanent.clear()
	fog_border_shroud = false
	threat_overrides.clear()
	threat_query_cache.clear()
	path_impassable.clear()
	path_walkable.clear()
	tactical_markers.clear()
	wall_upgrades.clear()
	# OCL leaves stay across clear only when re-registered by pack load.
	ocl_leaves.clear()
	object_leaves.clear()
	weapon_leaves.clear()
	score_excluded.clear()
	scoring_enabled = true
	time_frozen = false
	exit_map_requested = false
	living_world_commands.clear()
	presentation_log.clear()


func to_state() -> Dictionary:
	var state := {}
	if not fog_revealed.is_empty():
		state["fog_revealed"] = fog_revealed.duplicate(true)
	if not fog_permanent.is_empty():
		state["fog_permanent"] = fog_permanent.duplicate(true)
	if fog_border_shroud:
		state["fog_border_shroud"] = true
	if not threat_overrides.is_empty():
		state["threat_overrides"] = threat_overrides.duplicate(true)
	if not threat_query_cache.is_empty():
		state["threat_query_cache"] = threat_query_cache.duplicate(true)
	if not path_impassable.is_empty():
		state["path_impassable"] = path_impassable.duplicate(true)
	if not path_walkable.is_empty():
		state["path_walkable"] = path_walkable.duplicate(true)
	if not tactical_markers.is_empty():
		state["tactical_markers"] = tactical_markers.duplicate(true)
	if not wall_upgrades.is_empty():
		state["wall_upgrades"] = wall_upgrades.duplicate(true)
	if not score_excluded.is_empty():
		state["score_excluded"] = score_excluded.duplicate(true)
	if not scoring_enabled:
		state["scoring_enabled"] = false
	if time_frozen:
		state["time_frozen"] = true
	if exit_map_requested:
		state["exit_map_requested"] = true
	if not living_world_commands.is_empty():
		state["living_world_commands"] = living_world_commands.duplicate(true)
	if not presentation_log.is_empty():
		# Cap for hash stability on long matches
		var tail: Array = presentation_log.slice(maxi(0, presentation_log.size() - 64))
		state["presentation_log"] = tail
	return state


func from_state(state: Dictionary) -> void:
	fog_revealed = state.get("fog_revealed", {})
	fog_permanent = state.get("fog_permanent", {})
	fog_border_shroud = bool(state.get("fog_border_shroud", false))
	threat_overrides = state.get("threat_overrides", {})
	threat_query_cache = state.get("threat_query_cache", {})
	path_impassable = state.get("path_impassable", {})
	path_walkable = state.get("path_walkable", {})
	tactical_markers = state.get("tactical_markers", {})
	wall_upgrades = state.get("wall_upgrades", {})
	score_excluded = state.get("score_excluded", {})
	scoring_enabled = bool(state.get("scoring_enabled", true))
	time_frozen = bool(state.get("time_frozen", false))
	exit_map_requested = bool(state.get("exit_map_requested", false))
	living_world_commands = state.get("living_world_commands", [])
	presentation_log = state.get("presentation_log", [])


# --- FoW -------------------------------------------------------------------

func _fog_key(pos: Vector2) -> String:
	var cx := int(floor(pos.x / FOG_CELL_SIZE))
	var cy := int(floor(pos.y / FOG_CELL_SIZE))
	return "%d,%d" % [cx, cy]


func _cells_in_radius(center: Vector2, radius: float) -> Array:
	var out: Array = []
	var r := maxf(0.0, radius)
	var cell_r := int(ceil(r / FOG_CELL_SIZE)) + 1
	var ocx := int(floor(center.x / FOG_CELL_SIZE))
	var ocy := int(floor(center.y / FOG_CELL_SIZE))
	for dx in range(-cell_r, cell_r + 1):
		for dy in range(-cell_r, cell_r + 1):
			var cx := ocx + dx
			var cy := ocy + dy
			var cell_center := Vector2(
				(float(cx) + 0.5) * FOG_CELL_SIZE,
				(float(cy) + 0.5) * FOG_CELL_SIZE
			)
			if cell_center.distance_to(center) <= r + FOG_CELL_SIZE * 0.71:
				out.append("%d,%d" % [cx, cy])
	return out


func fog_reveal(team: int, center: Vector2, radius: float, permanent: bool) -> void:
	if team < 0:
		return
	if not fog_revealed.has(team):
		fog_revealed[team] = {}
	if permanent and not fog_permanent.has(team):
		fog_permanent[team] = {}
	for key in _cells_in_radius(center, radius):
		(fog_revealed[team] as Dictionary)[key] = true
		if permanent:
			(fog_permanent[team] as Dictionary)[key] = true


func fog_shroud(team: int, center: Vector2, radius: float) -> void:
	if team < 0 or not fog_revealed.has(team):
		return
	var rev: Dictionary = fog_revealed[team]
	var perm: Dictionary = fog_permanent.get(team, {})
	for key in _cells_in_radius(center, radius):
		if perm.has(key):
			continue
		rev.erase(key)


func fog_undo_permanent(team: int, center: Vector2, radius: float) -> void:
	if team < 0:
		return
	var perm: Dictionary = fog_permanent.get(team, {})
	var rev: Dictionary = fog_revealed.get(team, {})
	for key in _cells_in_radius(center, radius):
		perm.erase(key)
		rev.erase(key)


func fog_is_revealed(team: int, pos: Vector2) -> bool:
	if team < 0:
		return false
	var key := _fog_key(pos)
	return (fog_revealed.get(team, {}) as Dictionary).has(key) or (
		fog_permanent.get(team, {}) as Dictionary
	).has(key)


# --- Threat (slice combat-weight) ------------------------------------------

func entity_threat_weight(row: Dictionary) -> float:
	## OpenBFME slice threat: living combat weight = sum(member_health fraction)
	## * member_damage. Not a reverse-sourced SAGE formula; authoritative for
	## this sim's AI/script threat reads.
	if int(row.get("health", 0)) <= 0:
		return 0.0
	var member_health: Array = row.get("member_health", []) as Array
	var living := 0
	var max_members := maxi(1, int(row.get("member_count", member_health.size())))
	if member_health.is_empty():
		living = 1 if int(row.get("health", 0)) > 0 else 0
	else:
		for h in member_health:
			if int(h) > 0:
				living += 1
	var damage := float(row.get("member_damage", row.get("damage", 10)))
	return float(living) * damage * (float(living) / float(max_members))


func threat_in_radius(sim: Object, owner_team: int, origin: Vector2, radius: float) -> float:
	if threat_overrides.has(owner_team):
		return float(threat_overrides[owner_team])
	if sim == null:
		return 0.0
	var total := 0.0
	var r := maxf(0.0, radius)
	for eid in sim.entities.keys():
		var row: Dictionary = sim.entities[eid]
		if int(row.get("health", 0)) <= 0:
			continue
		var other := int(row.get("team", -1))
		if other == owner_team:
			continue
		if origin.distance_to(row.get("position", Vector2.ZERO)) <= r:
			total += entity_threat_weight(row)
	return total


func set_threat_override(team: int, level: float) -> void:
	threat_overrides[team] = level
	threat_query_cache["override:%d" % team] = level


func cache_threat_query(key: String, value: float) -> float:
	## Record a threat query answer into hash-backed state so pure formula
	## reads leave an authoritative trail (AI/script can re-read the cache).
	threat_query_cache[key] = value
	return value


# --- Path ------------------------------------------------------------------

func _path_key(pos: Vector2) -> String:
	var cx := int(floor(pos.x / PATH_CELL_SIZE))
	var cy := int(floor(pos.y / PATH_CELL_SIZE))
	return "%d,%d" % [cx, cy]


func set_path_impassable(pos: Vector2, impassable: bool) -> void:
	var key := _path_key(pos)
	if impassable:
		path_impassable[key] = true
		path_walkable.erase(key)
	else:
		path_impassable.erase(key)
		path_walkable[key] = true


func can_path_between(from: Vector2, to: Vector2) -> bool:
	## Straight-line cell walk: all cells on segment must not be impassable.
	var steps := maxi(1, int(ceil(from.distance_to(to) / (PATH_CELL_SIZE * 0.5))))
	for i in range(steps + 1):
		var t := float(i) / float(steps)
		var p := from.lerp(to, t)
		var key := _path_key(p)
		if path_impassable.has(key):
			return false
	return true


# --- Walls -----------------------------------------------------------------

func is_wall_kind(kind: String) -> bool:
	var k := kind.to_lower()
	return (
		k.contains("wall")
		or k.contains("castle")
		or k.contains("gate")
		or k.contains("hub")
		or k.contains("trebuchet")
	)


func apply_wall_upgrade(sim: Object, structure_id: int, upgrade: String) -> Dictionary:
	if sim == null or not sim.structures.has(structure_id):
		return {"ok": false, "reason": "structure missing"}
	var row: Dictionary = sim.structures[structure_id]
	var kind := String(row.get("kind", row.get("building_type", "")))
	if not is_wall_kind(kind) and kind != "":
		if not is_wall_kind(String(row.get("building_type", ""))):
			return {"ok": false, "reason": "structure is not a wall kind"}
	var completed: Array = row.get("completed_upgrades", []) as Array
	if not completed.has(upgrade):
		completed.append(upgrade)
	row["completed_upgrades"] = completed
	sim.structures[structure_id] = row
	var history: Array = wall_upgrades.get(structure_id, []) as Array
	history.append({"upgrade": upgrade, "tick": int(sim.tick_index)})
	wall_upgrades[structure_id] = history
	return {"ok": true, "reason": "", "structure_id": structure_id}


# --- Tactical markers ------------------------------------------------------

func register_tactical_marker(
	marker_type: String, name: String, position: Vector2, near_offset: Vector2, far_offset: Vector2
) -> void:
	if marker_type == "":
		return
	if not tactical_markers.has(marker_type):
		tactical_markers[marker_type] = []
	(tactical_markers[marker_type] as Array).append({
		"name": name,
		"position": position,
		"near": near_offset,
		"far": far_offset,
	})


func placement_for_marker(marker_type: String, near_or_far: String, near_base: Vector2) -> Dictionary:
	var rows: Array = tactical_markers.get(marker_type, []) as Array
	if rows.is_empty():
		# Fall back: any registered waypoint-like marker name match
		return {"ok": false, "reason": "no tactical markers of type '%s'" % marker_type}
	var best: Dictionary = {}
	var best_dist := INF
	for row_value in rows:
		var row := row_value as Dictionary
		var pos: Vector2 = row.get("position", Vector2.ZERO)
		var d := near_base.distance_to(pos)
		if d < best_dist:
			best_dist = d
			best = row
	if best.is_empty():
		return {"ok": false, "reason": "marker search failed"}
	var side := near_or_far.to_lower()
	var offset: Vector2 = best.get("near", Vector2(20, 0))
	if side == "far" or side == "1":
		offset = best.get("far", Vector2(-20, 0))
	return {
		"ok": true,
		"position": Vector2(best.get("position", Vector2.ZERO)) + offset,
		"marker": String(best.get("name", "")),
	}


# --- Transport capacity ----------------------------------------------------

func transport_capacity_for_structure(row: Dictionary) -> int:
	if row.has("transport_capacity"):
		return maxi(0, int(row["transport_capacity"]))
	var kind := String(row.get("kind", row.get("building_type", ""))).to_lower()
	for key in DEFAULT_TRANSPORT_CAPACITY.keys():
		if key != "default" and kind.contains(String(key)):
			return int(DEFAULT_TRANSPORT_CAPACITY[key])
	# Explicit transport/garrison kinds
	if kind.contains("transport") or kind.contains("garrison") or kind.contains("tower"):
		return 6
	return int(DEFAULT_TRANSPORT_CAPACITY["default"])


func can_load_entity(sim: Object, structure_id: int, entity_id: int) -> Dictionary:
	if sim == null:
		return {"ok": false, "reason": "no sim"}
	if not sim.structures.has(structure_id):
		return {"ok": false, "reason": "structure missing"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity missing"}
	var srow: Dictionary = sim.structures[structure_id]
	var erow: Dictionary = sim.entities[entity_id]
	if int(srow.get("health", 0)) <= 0:
		return {"ok": false, "reason": "structure destroyed"}
	if int(erow.get("health", 0)) <= 0:
		return {"ok": false, "reason": "entity dead"}
	if int(srow.get("team", -1)) != int(erow.get("team", -2)):
		return {"ok": false, "reason": "ownership mismatch"}
	if sim.entity_container.has(entity_id):
		return {"ok": false, "reason": "already contained"}
	var cap := transport_capacity_for_structure(srow)
	if cap <= 0:
		return {"ok": false, "reason": "structure has no transport capacity"}
	var used: int = sim.passenger_count(structure_id)
	if used >= cap:
		return {"ok": false, "reason": "capacity full"}
	return {"ok": true, "reason": ""}


# --- Mood ------------------------------------------------------------------

func apply_attitude_mood(row: Dictionary, mood: int) -> void:
	## Map AI mood into stance + mood acquire cadence the sim already steps.
	row["attitude"] = mood
	match mood:
		MOOD_PASSIVE:
			row["stance"] = "HoldGround"
			row["mood_attack_check_rate_ticks"] = 0
		MOOD_GUARD:
			row["stance"] = "HoldGround"
			row["mood_attack_check_rate_ticks"] = 30
			row["mood_randomize_next_check"] = true
		MOOD_AGGRESSIVE:
			row["stance"] = "Battle"
			row["mood_attack_check_rate_ticks"] = 15
			row["mood_randomize_next_check"] = true
		MOOD_ATTACK:
			row["stance"] = "Battle"
			row["mood_attack_check_rate_ticks"] = 5
			row["mood_randomize_next_check"] = true
		_:
			row["stance"] = "Battle"
			row["mood_attack_check_rate_ticks"] = 20
			row["mood_randomize_next_check"] = true


# --- Presentation / meta ---------------------------------------------------

func emit_presentation(sim: Object, channel: String, payload: Dictionary) -> void:
	var tick := 0
	if sim != null:
		tick = int(sim.tick_index)
	presentation_log.append({
		"channel": channel,
		"payload": payload.duplicate(true),
		"tick": tick,
	})
	if sim != null and sim.has_method("_emit_event"):
		sim._emit_event("presentation.%s" % channel, 0, 0, payload)


func register_ocl_leaf(ocl_id: String, leaf: Dictionary) -> void:
	if ocl_id != "":
		ocl_leaves[ocl_id] = leaf.duplicate(true)


func register_object_leaf(object_id: String, leaf: Dictionary) -> void:
	if object_id != "":
		object_leaves[object_id] = leaf.duplicate(true)


func register_weapon_leaf(weapon_id: String, leaf: Dictionary) -> void:
	if weapon_id != "":
		weapon_leaves[weapon_id] = leaf.duplicate(true)
