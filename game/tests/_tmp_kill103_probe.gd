extends SceneTree
## TEMPORARY diagnostic probe: why can't the elves finish entity 103?
## Mirrors retail_slice_runner's `_run_reference_battle` opening, then samples
## entity 103 health while the attack order is live. DELETE WHEN DONE.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")


func _initialize() -> void:
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	call_deferred("_run")


func _p(text: String) -> void:
	print("PROBE %s" % text)


func _run() -> void:
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	var slice = packed.instantiate()
	root.add_child(slice)
	var guard := 0
	while not bool(slice.ready_ok) and String(slice.failure_reason) == "" and guard < 4000:
		guard += 1
		await process_frame
	if not bool(slice.ready_ok):
		_p("boot failed: %s" % String(slice.failure_reason))
		quit(2)
		return
	_p("faction=%s" % String(slice.faction_manifest.get("faction", "?")))

	var sim = SimScript.new()
	sim.setup(slice.source_map_data.simulation_configuration(), slice.gameplay_rules)
	sim.ai_enabled = false

	# --- roster dump -------------------------------------------------------
	for entity_id in sim.entity_ids():
		var row: Dictionary = sim.entity(entity_id)
		_p("roster e%d team=%d type=%s obj=%s hp=%d dmg=%s dtype=%s range=%.2f minrange=%.2f delay=%s members=%s" % [
			entity_id, int(row.get("team", -1)), String(row.get("unit_type", "")),
			String(row.get("object_id", "")), int(row.get("health", 0)),
			str(row.get("damage", "?")), String(row.get("damage_type", "")),
			float(row.get("attack_range", 0.0)), float(row.get("minimum_attack_range", 0.0)),
			str(row.get("attack_delay_ticks", row.get("delay_ms", "?"))),
			str(row.get("member_count", "?")),
		])
	_p("missing_damage_type_units=%s" % str(sim.missing_damage_type_units))

	# --- line reinforcement (same selection logic as the runner) ------------
	var line_unit_type := ""
	var line_producer_kind := ""
	var rules_manifest: Dictionary = (slice.gameplay_rules.get("faction_manifest", {}) as Dictionary).get("unit_production_rules", {}) as Dictionary
	var unit_types: Array[String] = []
	for value in rules_manifest.keys():
		unit_types.append(String(value))
	unit_types.sort()
	for preferred_category in ["infantry", "ranged-infantry"]:
		if line_unit_type != "":
			break
		for unit_type in unit_types:
			var rule: Dictionary = rules_manifest[unit_type]
			var rule_kind := String(rule.get("producer_kind", ""))
			if String(rule.get("category", "")) != preferred_category or rule_kind == "" or rule_kind == "fortress":
				continue
			var min_prereq_count := 999
			for route_value in Array(rule.get("producer_routes", [])):
				var route_prereqs: Array = (route_value as Dictionary).get("prerequisites", []) as Array
				min_prereq_count = mini(min_prereq_count, route_prereqs.size())
			if min_prereq_count > 0:
				continue
			line_unit_type = unit_type
			line_producer_kind = rule_kind
			break
	_p("line_unit=%s producer=%s" % [line_unit_type, line_producer_kind])
	var reinforcement := _build_line_reinforcement(sim, line_unit_type, line_producer_kind)
	_p("reinforcement=%s" % str(reinforcement))

	sim.select_only(1)
	sim.toggle_selection(2)
	var attack_group: Array[int] = [1, 2]
	attack_group.append_array(reinforcement)
	sim.select_many(attack_group)
	var stage_point := Vector2(10.0, 14.0)
	sim.issue_move(attack_group, stage_point)
	for _index in range(1800):
		if _group_within(sim, attack_group, stage_point, 4.0):
			break
		sim.tick()
	_p("staged tick=%d" % int(sim.tick_index))

	_dump_target(sim, 102)
	_dump_target(sim, 103)
	_dump_target(sim, 101)
	_dump_attackers(sim, attack_group, 103)

	sim.issue_attack(sim.selected_ids.duplicate(), 102)
	var t102 := _kill_phase(sim, 102, 2400)
	_p("phase102 ticks=%d killed=%s" % [t102, str(int(sim.entity(102)["health"]) == 0)])

	_p("attack103 accepted=%d selected=%s" % [sim.issue_attack(sim.selected_ids.duplicate(), 103), str(sim.selected_ids)])
	var t103 := _kill_phase(sim, 103, 8000)
	_p("phase103 ticks=%d killed=%s hp=%d" % [t103, str(int(sim.entity(103)["health"]) == 0), int(sim.entity(103)["health"])])
	_dump_attackers(sim, attack_group, 103)
	quit(0)


func _kill_phase(sim, target: int, budget: int) -> int:
	var start := int(sim.tick_index)
	var last_hp := int(sim.entity(target)["health"])
	_p("phase%d start hp=%d" % [target, last_hp])
	for index in range(budget):
		if int(sim.entity(target)["health"]) == 0:
			return index
		sim.tick()
		if index % 200 == 199:
			var row: Dictionary = sim.entity(target)
			var hp := int(row.get("health", 0))
			_p("phase%d t+%d hp=%d (d=%d) members=%s state=%s pos=%s" % [
				target, index + 1, hp, hp - last_hp,
				str(row.get("member_health", "")), String(row.get("state", "")),
				str(row.get("position", Vector2.ZERO)),
			])
			last_hp = hp
	return budget


func _dump_target(sim, id: int) -> void:
	var row: Dictionary = sim.entity(id)
	var object_id := String(row.get("object_id", ""))
	var armor: Dictionary = sim._unit_armor.get(object_id, {})
	_p("target e%d type=%s obj=%s hp=%d members=%s armor_set=%s passthrough=%s damage_scalar=%s scalars=%s" % [
		id, String(row.get("unit_type", "")), object_id, int(row.get("health", 0)),
		str(row.get("member_health", "")), str(armor.get("set_id", armor.get("armor_id", "?"))),
		str(armor.get("passthrough", false)), str(armor.get("damage_scalar", 1.0)),
		str(armor.get("scalars", {})),
	])


func _dump_attackers(sim, ids: Array[int], victim: int) -> void:
	var victim_row: Dictionary = sim.entity(victim)
	for id in ids:
		if not sim.entities.has(id):
			continue
		var row: Dictionary = sim.entity(id)
		var dtype := String(row.get("damage_type", ""))
		var scalar := float(sim._member_armor_scalar(victim_row, dtype))
		_p("attacker e%d type=%s obj=%s hp=%d state=%s target=%d dmg=%s dtype=%s scalar=%.4f effective=%.2f range=%.2f pos=%s members=%s cd=%s" % [
			id, String(row.get("unit_type", "")), String(row.get("object_id", "")),
			int(row.get("health", 0)), String(row.get("state", "")), int(row.get("target_id", 0)),
			str(row.get("damage", "?")), dtype, scalar,
			float(row.get("damage", 0.0)) * scalar, float(row.get("attack_range", 0.0)),
			str(row.get("position", Vector2.ZERO)), str(row.get("member_health", "")),
			str(row.get("attack_cooldown", "?")),
		])


func _group_within(sim, ids: Array[int], point: Vector2, radius: float) -> bool:
	for id in ids:
		if not sim.entities.has(id):
			continue
		var entity: Dictionary = sim.entities[id]
		if int(entity.get("health", 0)) > 0 and Vector2(entity.get("position", Vector2.INF)).distance_to(point) > radius:
			return false
	return true


func _build_line_reinforcement(simulation, unit_type: String, producer_kind: String) -> Array[int]:
	if unit_type == "" or producer_kind == "":
		return []
	simulation.command_point_cap = maxi(int(simulation.command_point_cap), 300)
	var builder_ids: Array[int] = []
	for entity_id in simulation.entity_ids():
		var candidate: Dictionary = simulation.entity(entity_id)
		if int(candidate.get("team", -1)) == 0 and bool(candidate.get("is_builder", false)):
			builder_ids.append(entity_id)
	if builder_ids.is_empty():
		return []
	var anchor := Vector2(simulation.entity(1).get("position", Vector2.ZERO))
	var barracks := 0
	for dx in range(-36, 37, 6):
		for dy in range(-36, 37, 6):
			var result: Dictionary = simulation.issue_construct(builder_ids, producer_kind, anchor + Vector2(dx, dy))
			if bool(result.get("ok", false)):
				barracks = int(result.get("structure_id", 0))
				break
		if barracks != 0:
			break
	if barracks == 0:
		return []
	for _step in range(3000):
		if float(simulation.structure(barracks).get("construction_progress", 0.0)) >= 1.0:
			break
		simulation.tick()
	for _count in range(3):
		var queued: Dictionary = simulation.queue_unit(0, barracks, unit_type)
		if not bool(queued.get("ok", false)):
			break
		var complete := int((queued.get("item", {}) as Dictionary).get("complete_tick", simulation.tick_index))
		while simulation.tick_index < complete:
			simulation.tick()
	for _exit_tick in range(24):
		simulation.tick()
	var trained: Array[int] = []
	for entity_id in simulation.entity_ids():
		var candidate: Dictionary = simulation.entity(entity_id)
		if int(entity_id) >= 10 and int(candidate.get("team", -1)) == 0 and int(candidate.get("health", 0)) > 0 and String(candidate.get("unit_type", "")) == unit_type:
			trained.append(entity_id)
	trained.sort()
	return trained
