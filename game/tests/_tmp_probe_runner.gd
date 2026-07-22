extends SceneTree
## Temporary diagnostic probe for the elves produced-hero revival failure.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")


func _initialize() -> void:
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	call_deferred("_run")


func _run() -> void:
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	if not bool(slice.ready_ok):
		print("PROBE boot failed: %s" % String(slice.failure_reason))
		quit(2)
		return
	var roster_rules: Dictionary = slice.gameplay_rules.duplicate(true)
	roster_rules.merge({"starting_resources": 60000, "command_point_cap": 1000}, true)
	var sim = SimScript.new()
	sim.setup(slice.source_map_data.simulation_configuration(), roster_rules)
	sim.ai_enabled = false
	var fortress: int = sim.producer_id(0, "fortress")
	var spawn_hero_type := ""
	var spawn_hero_id := 0
	for entity_id in sim.entity_ids():
		var row: Dictionary = sim.entity(entity_id)
		if int(row.get("team", -1)) == 0 and String(sim.production_rule_category(String(row.get("unit_type", "")))) == "hero":
			spawn_hero_type = String(row.get("unit_type", ""))
			spawn_hero_id = entity_id
			break
	print("PROBE spawn_hero_type=%s id=%d" % [spawn_hero_type, spawn_hero_id])
	if spawn_hero_type == "":
		quit(2)
		return
	(sim.entities[spawn_hero_id] as Dictionary)["health"] = 0
	var queue_result: Dictionary = sim.queue_unit(0, fortress, spawn_hero_type)
	print("PROBE fallen requeue: %s" % str(queue_result))
	var hero_rule: Dictionary = (sim._rules.get("unit_production_rules", {}) as Dictionary).get(spawn_hero_type, {})
	print("PROBE hero_rule build_ticks=%s" % str(hero_rule.get("default_build_ticks", "?")))
	sim.advance(800)
	var produced_id := 0
	for entity_id in sim.entity_ids():
		var row: Dictionary = sim.entity(entity_id)
		if int(row.get("team", -1)) == 0 and String(row.get("unit_type", "")) == spawn_hero_type and entity_id >= 10:
			produced_id = entity_id
			break
	print("PROBE produced_id=%d" % produced_id)
	if produced_id == 0:
		quit(2)
		return
	var produced: Dictionary = sim.entity(produced_id)
	print("PROBE produced health=%s member_health=%s unit_type=%s object_id=%s damage_type=%s" % [
		str(produced.get("health", "")), str(produced.get("member_health", "")),
		str(produced.get("unit_type", "")), str(produced.get("object_id", "")),
		str(produced.get("damage_type", "")),
	])
	var attacker: Dictionary = sim.entity(spawn_hero_id)
	print("PROBE attacker id=%d damage_type=%s health=%s" % [spawn_hero_id, str(attacker.get("damage_type", "")), str(attacker.get("health", ""))])
	print("PROBE identities before kill: %s" % str(sim._completed_hero_identities.keys()))
	sim._apply_damage(spawn_hero_id, produced_id, 999999)
	var after: Dictionary = sim.entity(produced_id)
	print("PROBE after kill: health=%s member_health=%s state=%s" % [
		str(after.get("health", "")), str(after.get("member_health", "")), str(after.get("state", "")),
	])
	print("PROBE identities after kill: %s" % str(sim._completed_hero_identities.keys()))
	var hero_armor: Dictionary = sim._unit_armor.get(String(produced.get("object_id", "")), {})
	print("PROBE victim armor rule: %s scalar(slash)=%s" % [str(hero_armor.get("set_id", "")), str((hero_armor.get("scalars", {}) as Dictionary).get("slash", "?"))])
	var requeue: Dictionary = sim.queue_unit(0, fortress, spawn_hero_type)
	print("PROBE revival requeue: %s" % str(requeue))
	quit(0)
