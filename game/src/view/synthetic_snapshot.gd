class_name SyntheticSnapshot
extends RefCounted
## Deterministic snapshot-v1 producer for presentation and render benchmarks.
##
## This is deliberately not a simulation. It owns only synthetic fixture state,
## publishes the snapshot contract's packed arrays, and never touches GameState
## or any other runtime simulation object.

const SCHEMA := "openbfme.snapshot.v1"
const TICK_MS := 33
const FIELD_SIZE := 4000.0
const HORDE_SIZE := 20
const PLAYER_COUNT := 4
const DYING_FLAG := 4
const DYING_TICKS := 18

var _rng := RandomNumberGenerator.new()
var _seed := 1
var _tick := 0
var _template_count := 1
var _members: Array[Dictionary] = []
var _events: Array[Dictionary] = []


func _init(seed_value: int = 1, object_count: int = 0, template_count: int = 1) -> void:
	_seed = seed_value
	_template_count = maxi(1, template_count)
	_rng.seed = seed_value
	_build_members(maxi(0, object_count))
	_events = _spawn_events()


func snapshot() -> Dictionary:
	var objects := _packed_objects()
	var payload := {
		"schema": SCHEMA,
		"tick": _tick,
		"tick_ms": TICK_MS,
		"object_count": _members.size(),
		"objects": objects,
		"hordes": _packed_hordes(),
		"players": _packed_players(),
		"events": _events.duplicate(true),
	}
	var digest := _state_hash(payload)
	# Keep the public document in the contract's field order. The hash is over
	# every authoritative field except itself.
	return {
		"schema": payload["schema"],
		"tick": payload["tick"],
		"tick_ms": payload["tick_ms"],
		"hash": digest,
		"object_count": payload["object_count"],
		"objects": payload["objects"],
		"hordes": payload["hordes"],
		"players": payload["players"],
		"events": payload["events"],
	}


func next_tick() -> Dictionary:
	_tick += 1
	_events.clear()
	for index in range(_members.size() - 1, -1, -1):
		var member := _members[index]
		var death_tick := int(member["death_tick"])
		if death_tick >= 0 and _tick >= death_tick + DYING_TICKS:
			_members.remove_at(index)
			continue
		if death_tick >= 0 and _tick == death_tick:
			member["health"] = 0.0
			member["flags"] = int(member["flags"]) | DYING_FLAG
			member["state"] = 4
			member["anim"] = 3
			member["anim_frame"] = 0.0
			_events.append({"kind": "death", "object": int(member["id"])})
			continue
		if (int(member["flags"]) & DYING_FLAG) != 0:
			member["anim_frame"] = float(_tick - death_tick)
			continue

		var fighting := (int(member["id"]) + _seed + _tick / 24) % 9 == 0
		if fighting:
			member["state"] = 2
			member["anim"] = 2
			member["anim_frame"] = fposmod(float(_tick) * 0.85 + float(member["phase"]), 24.0)
		else:
			member["state"] = 1
			member["anim"] = 1
			member["anim_frame"] = fposmod(float(_tick) * 0.55 + float(member["phase"]), 30.0)
			var speed := float(member["speed"])
			var heading := float(member["heading"])
			member["x"] = fposmod(float(member["x"]) + cos(heading) * speed, FIELD_SIZE)
			member["z"] = fposmod(float(member["z"]) + sin(heading) * speed, FIELD_SIZE)

	if not _members.is_empty() and _events.is_empty():
		var event_member := _members[_tick % _members.size()]
		if _tick % 15 == 0:
			_events.append({
				"kind": "sound",
				"object": int(event_member["id"]),
				"name": "SyntheticMarch",
			})
		else:
			_events.append({
				"kind": "damage",
				"object": int(event_member["id"]),
				"target": int(event_member["id"]),
				"amount": 0.0,
			})
	return snapshot()


func _build_members(count: int) -> void:
	var horde_count := maxi(1, ceili(float(count) / float(HORDE_SIZE)))
	var horde_columns := maxi(1, ceili(sqrt(float(horde_count))))
	var block_spacing := FIELD_SIZE / float(horde_columns + 1)
	for zero_index in count:
		var horde_index := zero_index / HORDE_SIZE
		var member_index := zero_index % HORDE_SIZE
		var block_x := horde_index % horde_columns
		var block_z := horde_index / horde_columns
		var heading := _rng.randf_range(-PI, PI)
		var local_x := float(member_index % 5 - 2) * 8.0
		var local_z := float(member_index / 5 - 2) * 8.0
		var center_x := block_spacing * float(block_x + 1)
		var center_z := block_spacing * float(block_z + 1)
		var death_tick := -1
		# A sparse, deterministic subset visibly enters a death state and remains
		# present with flag bit 4 before removal.
		if count > 0 and zero_index == posmod(_seed, count):
			death_tick = 72 + _rng.randi_range(0, 24)
		_members.append({
			"id": zero_index + 1,
			"template": horde_index % _template_count,
			"owner": horde_index % PLAYER_COUNT,
			"x": clampf(center_x + local_x, 0.0, FIELD_SIZE),
			"y": 0.0,
			"z": clampf(center_z + local_z, 0.0, FIELD_SIZE),
			"yaw": heading,
			"heading": heading,
			"health": 100.0,
			"max_health": 100.0,
			"state": 1,
			"anim": 1,
			"anim_frame": _rng.randf_range(0.0, 30.0),
			"phase": _rng.randf_range(0.0, 30.0),
			"flags": 1,
			"speed": _rng.randf_range(1.8, 3.2),
			"horde": horde_index,
			"death_tick": death_tick,
		})


func _packed_objects() -> Dictionary:
	var packed := {
		"id": [], "template": [], "owner": [], "x": [], "y": [], "z": [],
		"yaw": [], "health": [], "max_health": [], "state": [], "anim": [],
		"anim_frame": [], "flags": [],
	}
	for member in _members:
		for key in packed.keys():
			(packed[key] as Array).append(member[key])
	return packed


func _packed_hordes() -> Array[Dictionary]:
	var by_horde: Dictionary = {}
	for member in _members:
		var horde_index := int(member["horde"])
		if not by_horde.has(horde_index):
			by_horde[horde_index] = {
				"id": 100000 + horde_index,
				"owner": int(member["owner"]),
				"template": int(member["template"]),
				"members": [],
				"formation": horde_index % 3,
			}
		(by_horde[horde_index]["members"] as Array).append(int(member["id"]))
	var result: Array[Dictionary] = []
	var keys: Array = by_horde.keys()
	keys.sort()
	for key in keys:
		result.append(by_horde[key])
	return result


func _packed_players() -> Array[Dictionary]:
	var command_points := [0, 0, 0, 0]
	for member in _members:
		command_points[int(member["owner"])] += 1
	var players: Array[Dictionary] = []
	for player in PLAYER_COUNT:
		players.append({
			"index": player,
			"resources": 1500 + player * 125 + _seed * 3 + _tick,
			"command_points": command_points[player],
			"command_points_max": 10000,
			"power_points": (_tick / 90 + player) % 20,
		})
	return players


func _spawn_events() -> Array[Dictionary]:
	var events: Array[Dictionary] = []
	for member in _members:
		events.append({"kind": "spawn", "object": int(member["id"])})
	return events


func _state_hash(payload: Dictionary) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(JSON.stringify(payload).to_utf8_buffer())
	return context.finish().hex_encode()
