extends SceneTree

const SOAK_TICKS: int = 18_000
const FIXED_DELTA: float = 0.1

var passed: int = 0
var failed: int = 0
var game_state: Node
var content_db: Node
var sim_world_script: Script
var ai_script: Script


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	game_state = root.get_node_or_null("GameState")
	content_db = root.get_node_or_null("ContentDB")
	sim_world_script = load("res://src/sim/sim_world.gd")
	ai_script = load("res://src/sim/skirmish_ai.gd")
	if game_state == null or content_db == null or sim_world_script == null or ai_script == null:
		print("STAGE10_SOAK_PROOF FAIL missing autoloads")
		quit(2)
		return
	var game_audio: Node = root.get_node_or_null("GameAudio")
	if game_audio != null:
		game_audio.set("enabled", false)
		game_audio.set("music_enabled", false)
	var first: Dictionary = _run_soak(910_247)
	var second: Dictionary = _run_soak(910_247)
	_check("soak_advances_exactly_30_minutes", int(first.get("ticks", 0)) == SOAK_TICKS and is_equal_approx(float(first.get("seconds", 0.0)), 1800.0))
	_check("soak_replay_hash_matches", String(first.get("hash", "")) == String(second.get("hash", "")), "%s/%s" % [first.get("hash", ""), second.get("hash", "")])
	_check("soak_replay_metrics_match", int(first.get("matches", -1)) == int(second.get("matches", -2)) and int(first.get("actions", -1)) == int(second.get("actions", -2)))
	_check("soak_ai_remains_active", int(first.get("actions", 0)) >= 20, "actions=%d" % int(first.get("actions", 0)))
	_check("soak_entities_are_bounded", int(first.get("peak_entities", 99_999)) <= 512, "peak=%d" % int(first.get("peak_entities", -1)))
	_check("soak_projectiles_are_bounded", int(first.get("peak_projectiles", 99_999)) <= 256, "peak=%d" % int(first.get("peak_projectiles", -1)))
	_check("soak_completes_inside_budget", int(first.get("elapsed_ms", 99_999)) <= 30_000, "elapsed_ms=%d" % int(first.get("elapsed_ms", -1)))
	print("STAGE10_SOAK_METRICS hash=%s ticks=%d simulated_seconds=1800 matches=%d actions=%d peak_entities=%d peak_projectiles=%d elapsed_ms=%d" % [
		first.get("hash", ""), SOAK_TICKS, int(first.get("matches", 0)), int(first.get("actions", 0)),
		int(first.get("peak_entities", 0)), int(first.get("peak_projectiles", 0)), int(first.get("elapsed_ms", 0))
	])
	_cleanup()
	await process_frame
	_finish()


func _run_soak(seed: int) -> Dictionary:
	var started_ms: int = Time.get_ticks_msec()
	var match_index: int = 0
	var completed_matches: int = 0
	var total_actions: int = 0
	var peak_entities: int = 0
	var peak_projectiles: int = 0
	var history: PackedStringArray = PackedStringArray()
	var setup: Dictionary = _setup_match(seed)
	var world = setup["world"]
	var blue_ai = setup["blue_ai"]
	var red_ai = setup["red_ai"]
	for tick_index: int in range(SOAK_TICKS):
		blue_ai.tick(world, FIXED_DELTA)
		red_ai.tick(world, FIXED_DELTA)
		world.tick(FIXED_DELTA)
		var entity_count: int = world.battalions.size() + world.buildings.size()
		peak_entities = maxi(peak_entities, entity_count)
		peak_projectiles = maxi(peak_projectiles, world.projectiles.size())
		if entity_count > 512 or world.projectiles.size() > 256:
			failed += 1
			print("FAIL soak_runtime_bound tick=%d entities=%d projectiles=%d" % [tick_index, entity_count, world.projectiles.size()])
			break
		for side: int in [0, 1]:
			var resources_by_side: Dictionary = game_state.get("resources")
			var resources: float = float(resources_by_side.get(side, 0.0))
			if not is_finite(resources) or resources < -0.01:
				failed += 1
				print("FAIL soak_resource_invariant tick=%d side=%d value=%s" % [tick_index, side, resources])
				break
		if bool(game_state.get("game_over")) and tick_index + 1 < SOAK_TICKS:
			total_actions += _ai_actions(blue_ai) + _ai_actions(red_ai)
			history.append(_world_signature(world, blue_ai, red_ai))
			completed_matches += 1
			match_index += 1
			setup = _setup_match(seed + match_index * 101)
			world = setup["world"]
			blue_ai = setup["blue_ai"]
			red_ai = setup["red_ai"]
	total_actions += _ai_actions(blue_ai) + _ai_actions(red_ai)
	history.append(_world_signature(world, blue_ai, red_ai))
	var canonical := "ticks=%d|matches=%d|actions=%d|%s" % [SOAK_TICKS, completed_matches, total_actions, "||".join(history)]
	return {
		"hash": _fnv1a32(canonical),
		"ticks": SOAK_TICKS,
		"seconds": float(SOAK_TICKS) * FIXED_DELTA,
		"matches": completed_matches,
		"actions": total_actions,
		"peak_entities": peak_entities,
		"peak_projectiles": peak_projectiles,
		"elapsed_ms": Time.get_ticks_msec() - started_ms,
	}


func _setup_match(seed: int) -> Dictionary:
	game_state.call("reset_match")
	var stage_flags: Dictionary = game_state.get("stage_flags")
	stage_flags["ai"] = true
	stage_flags["economy"] = true
	stage_flags["research"] = true
	stage_flags["veterancy"] = true
	game_state.set("resources", {0: 5000.0, 1: 5000.0})
	var world = sim_world_script.new(seed)
	world.fog_enabled = false
	game_state.set("world", world)
	_spawn_side(world, 0, "gondor", Vector2(-70.0, -70.0), Vector2(1.0, 1.0))
	_spawn_side(world, 1, "mordor", Vector2(70.0, 70.0), Vector2(-1.0, -1.0))
	world.rebuild_obstacles()
	var blue_ai = ai_script.new()
	blue_ai.setup(0, "gondor", "normal")
	var red_ai = ai_script.new()
	red_ai.setup(1, "mordor", "normal")
	game_state.set("ai", red_ai)
	return {"world": world, "blue_ai": blue_ai, "red_ai": red_ai}


func _spawn_side(world, side: int, faction_id: String, base: Vector2, direction: Vector2) -> void:
	var faction: Dictionary = content_db.call("get_faction", faction_id)
	var fortress_id: String = String(faction.get("fortress", ""))
	var plan: Dictionary = faction.get("ai_plan", {})
	world.spawn_building(fortress_id, side, base, true, faction_id)
	var economy: Array = plan.get("economy", [])
	if not economy.is_empty():
		world.spawn_building(String(economy[0]), side, base + Vector2(18.0 * direction.x, 0.0), true, faction_id)
	var production: Array = plan.get("production", [])
	if not production.is_empty():
		world.spawn_building(String(production[0]), side, base + Vector2(0.0, 18.0 * direction.y), true, faction_id)
	var army: Array = plan.get("army", [])
	for index: int in range(mini(2, army.size())):
		world.spawn_battalion(String(army[index]), side, base + direction * float(10 + index * 4), faction_id)


func _ai_actions(ai) -> int:
	var result: int = 0
	for key: String in ["builds", "trains", "waves", "research", "orders"]:
		result += int(ai.counters.get(key, 0))
	return result


func _world_signature(world, blue_ai, red_ai) -> String:
	var parts: PackedStringArray = PackedStringArray()
	parts.append("next=%d" % world.next_id)
	parts.append("over=%d" % int(bool(game_state.get("game_over"))))
	parts.append("winner=%d" % int(game_state.get("winner_side")))
	var resources_by_side: Dictionary = game_state.get("resources")
	parts.append("r0=%d" % roundi(float(resources_by_side.get(0, 0.0)) * 1000.0))
	parts.append("r1=%d" % roundi(float(resources_by_side.get(1, 0.0)) * 1000.0))
	parts.append("a0=%d" % _ai_actions(blue_ai))
	parts.append("a1=%d" % _ai_actions(red_ai))
	var battalion_ids: Array = world.battalions.keys()
	battalion_ids.sort()
	for id_value: Variant in battalion_ids:
		var unit = world.battalions[id_value]
		parts.append("u:%d:%s:%d:%d:%d:%d:%d:%d" % [
			unit.id, unit.type_id, unit.side, roundi(unit.pos.x * 1000.0), roundi(unit.pos.y * 1000.0),
			roundi(unit.hp * 1000.0), int(unit.dead), unit.rank
		])
	var building_ids: Array = world.buildings.keys()
	building_ids.sort()
	for id_value: Variant in building_ids:
		var building = world.buildings[id_value]
		parts.append("b:%d:%s:%d:%d:%d:%d:%d:%d" % [
			building.id, building.type_id, building.side, roundi(building.pos.x * 1000.0), roundi(building.pos.y * 1000.0),
			roundi(building.hp * 1000.0), int(building.dead), int(building.built)
		])
	return "|".join(parts)


func _fnv1a32(value: String) -> String:
	var hash_value: int = 2_166_136_261
	for byte: int in value.to_utf8_buffer():
		hash_value = ((hash_value ^ byte) * 16_777_619) & 0xffffffff
	return "%08X" % hash_value


func _cleanup() -> void:
	game_state.call("reset_match")
	var game_audio: Node = root.get_node_or_null("GameAudio")
	if game_audio != null:
		if is_instance_valid(game_audio.get("sfx_player")):
			game_audio.get("sfx_player").call("stop")
		if is_instance_valid(game_audio.get("music_player")):
			game_audio.get("music_player").call("stop")


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE10_SOAK_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE10_SOAK_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
