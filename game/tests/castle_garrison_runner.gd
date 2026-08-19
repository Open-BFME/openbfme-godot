extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Watchdog = preload("res://tests/runner_watchdog.gd")
const EXPECTED := 36
var passed := 0
var failed := 0
var watchdog := Watchdog.new()
var unit_template: Dictionary = {}


func _initialize() -> void:
	watchdog.start(self, "CASTLE_GARRISON", 600000)
	call_deferred("_run")


func _run() -> void:
	var erebor_rows := await _seeded_rows("rotwk.map.wor-erebor")
	_check("erebor_seeded_tower_found", not (erebor_rows.get("garrison", {}) as Dictionary).is_empty())
	_check("erebortower2_seeded_without_container", not (erebor_rows.get("defect", {}) as Dictionary).has("horde_transport"))
	if (erebor_rows.get("garrison", {}) as Dictionary).is_empty():
		_finish(); return
	var sim := _fixture_sim(erebor_rows["garrison"] as Dictionary)
	var tower_id := int((erebor_rows["garrison"] as Dictionary)["id"])
	for id in [1, 2, 3]:
		_submit(sim, "issue_garrison", id, tower_id, id)
		sim.advance(1)
	_check("erebor_three_hordes_enter_lockstep", sim.passenger_count(tower_id) == 3)
	_submit(sim, "issue_garrison", 4, tower_id, 4); sim.advance(1)
	_check("erebor_fourth_horde_refused", String((sim.last_command_result as Dictionary).get("reason", "")) == "capacity-full")
	var cavalry_sim := _fixture_sim(erebor_rows["garrison"] as Dictionary)
	_submit(cavalry_sim, "issue_garrison", 5, tower_id, 5); cavalry_sim.advance(1)
	_check("erebor_cavalry_refused", String((cavalry_sim.last_command_result as Dictionary).get("reason", "")) == "passenger-filter-refused")
	var summoned_sim := _fixture_sim(erebor_rows["garrison"] as Dictionary)
	_submit(summoned_sim, "issue_garrison", 6, tower_id, 6); summoned_sim.advance(1)
	_check("erebor_summoned_refused", String((summoned_sim.last_command_result as Dictionary).get("reason", "")) == "passenger-filter-refused")
	var named_sim := _fixture_sim(erebor_rows["garrison"] as Dictionary)
	_submit(named_sim, "issue_garrison", 7, tower_id, 7); named_sim.advance(1)
	_check("erebor_named_exclusion_refused", String((named_sim.last_command_result as Dictionary).get("reason", "")) == "passenger-filter-refused")
	var first := sim.entities[1] as Dictionary
	_check("contained_horde_hidden_stopped", bool(first.get("presentation_hidden", false)) and Vector2(first.get("position")).is_equal_approx(Vector2((sim.structures[tower_id] as Dictionary).get("position"))))
	_check("contained_horde_not_targetable", not sim._target_alive(1, "battalion"))
	(sim.entities[90] as Dictionary)["health"] = 1000
	(sim.entities[90] as Dictionary)["maximum_health"] = 1000
	(sim.entities[90] as Dictionary)["member_health"] = [1000]
	(sim.entities[90] as Dictionary)["member_positions"] = [Vector2((sim.structures[tower_id] as Dictionary).get("position")) + Vector2(1, 0)]
	var enemy_before := 1000
	(first as Dictionary)["target_id"] = 90
	(first as Dictionary)["target_kind"] = "battalion"
	for _i in 20: sim.advance(1)
	_check("occupant_attacks_from_tower", int((sim.entities[90] as Dictionary).get("health", 0)) < enemy_before)
	var occupant_health := int(first.get("health", 0))
	sim._apply_structure_damage(90, tower_id, 10)
	_check("damage_percent_zero_isolates_occupants", int(first.get("health", 0)) == occupant_health)
	sim._apply_structure_damage(90, tower_id, 999999)
	sim.advance(1)
	_check("tower_death_evicts_alive", not sim.entity_container.has(1) and int(first.get("health", 0)) == occupant_health)
	var exit_expected := Vector2((sim.structures[tower_id] as Dictionary).get("position")) + sim._retail_source_to_sim_offset(Vector2(50.0, 0.0))
	_check("erebor_exit_uses_authored_offset", Vector2(first.get("position")).is_equal_approx(exit_expected))
	var defect := erebor_rows["defect"] as Dictionary
	var defect_sim := _fixture_sim(defect)
	var defect_result := defect_sim.load_transport_entity(int(defect["id"]), 1)
	_check("erebortower2_accepts_nobody", String(defect_result.get("reason", "")) == "typed-horde-transport-contract-missing")

	var grey_rows := await _seeded_rows("rotwk.map.wor-grey-havens")
	var grey := _fixture_sim(grey_rows["garrison"] as Dictionary)
	var grey_id := int((grey_rows["garrison"] as Dictionary)["id"])
	_submit(grey, "issue_garrison", 1, grey_id, 1); grey.advance(1)
	_check("grey_neutral_tower_captured", int((grey.structures[grey_id] as Dictionary).get("team", -1)) == 0)
	_submit(grey, "issue_garrison", 2, grey_id, 2); grey.advance(1)
	_check("grey_capacity_one_enforced", String((grey.last_command_result as Dictionary).get("reason", "")) == "capacity-full")

	var a := _fixture_sim(erebor_rows["garrison"] as Dictionary)
	var b := _fixture_sim(erebor_rows["garrison"] as Dictionary)
	for peer in [a, b]:
		_submit(peer, "issue_garrison", 1, tower_id, 1)
		peer.advance(1)
	_check("same_commands_same_signature", a.state_signature() == b.state_signature())
	_check("containment_in_state_snapshot_when_nonempty", not a.containment.is_empty() and a.state_snapshot().has("containment"))
	_check("containment_in_serialize_surface_when_nonempty", a._authoritative_state().has("containment"))
	# Negative control: garrison on ONE peer only => signatures differ.
	var c := _fixture_sim(erebor_rows["garrison"] as Dictionary)
	var d := _fixture_sim(erebor_rows["garrison"] as Dictionary)
	_submit(c, "issue_garrison", 1, tower_id, 1)
	c.advance(1)
	d.advance(1)
	_check("one_sided_garrison_changes_signature", c.state_signature() != d.state_signature())
	# The EXIT command through the lockstep queue empties the tower.
	_check("command_issue_exit_garrison_submitted", a.submit_command({"tick": a.tick_index + 1, "team": 0, "seq": 9, "type": "issue_exit_garrison", "args": {"entity_id": 1}}))
	a.advance(1)
	_check("exit_command_empties_tower", a.containment.is_empty() and not a.entity_container.has(1))
	_check("empty_containment_is_absent", not a._authoritative_state().has("containment") and not a._authoritative_state().has("entity_container") and not a.state_snapshot().has("containment"))
	a._ensure_parity()
	_check("fabricated_capacity_deleted", int(a.parity.transport_capacity_for_structure({"kind": "tower"})) == 0)
	_finish()


func _seeded_rows(map_id: String) -> Dictionary:
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	OS.set_environment("OPENBFME_SLICE_MAP", map_id)
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + 300000
	while Time.get_ticks_msec() < deadline and not bool(slice.ready_ok) and String(slice.failure_reason) == "":
		await process_frame
	var found := {"garrison": {}, "defect": {}}
	if bool(slice.ready_ok):
		for eid in slice.simulation.entity_ids():
			var entity: Dictionary = slice.simulation.entity(eid)
			if int(entity.get("team", -1)) == 0 and not bool(entity.get("is_banner_carrier", false)) and not bool(entity.get("is_builder", false)):
				unit_template = entity.duplicate(true)
				break
		for sid in slice.simulation.structure_ids():
			var row: Dictionary = slice.simulation.structure(sid)
			if String(row.get("castle_fixture_type", "")) in ["EBGarrisonableTower", "GHGarrisonableTower"] and (found["garrison"] as Dictionary).is_empty(): found["garrison"] = row.duplicate(true)
			if String(row.get("castle_fixture_type", "")) == "Erebortower2" and (found["defect"] as Dictionary).is_empty(): found["defect"] = row.duplicate(true)
	root.remove_child(slice); slice.free(); await process_frame
	return found


func _fixture_sim(tower: Dictionary) -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {})
	sim.ai_enabled = false; sim.base_loop_enabled = false; sim.winner = -1
	sim.entities.clear(); sim.structures.clear()
	sim._structure_armor["castle_fixture"] = {"set_id": "runner", "damage_scalar": 1.0, "scalars": {"default": 1.0}}
	var copy := tower.duplicate(true)
	copy["team"] = Sim.CASTLE_CIVILIAN_TEAM
	copy["health"] = 1500; copy["maximum_health"] = 1500; copy["indestructible"] = false
	sim.structures[int(copy["id"])] = copy
	for id in [1, 2, 3, 4, 5, 6, 7]: sim.entities[id] = _unit(id, 0, ["CAVALRY"] if id == 5 else ["INFANTRY", "SUMMONED"] if id == 6 else ["INFANTRY"])
	(sim.entities[7] as Dictionary)["unit_type"] = "rotwk.object.angmar-thrall-master"
	sim.entities[90] = _unit(90, 1, ["INFANTRY"])
	(sim.entities[90] as Dictionary)["position"] = Vector2(copy.get("position")) + Vector2(1, 0)
	return sim


func _unit(id: int, team: int, kinds: Array) -> Dictionary:
	var row := unit_template.duplicate(true)
	if row.is_empty():
		row = {"member_health": [100], "member_maximum_health": 100, "member_count": 1, "member_damage": 10, "attack_period_ticks": 2, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "attack_range": 10.0, "vision_range": 10.0}
	row["id"] = id; row["team"] = team; row["health"] = 100; row["maximum_health"] = 100; row["member_health"] = [100]
	row["position"] = Vector2.ZERO; row["destination"] = Vector2.ZERO; row["route"] = []; row["route_cells"] = []; row["route_ford"] = ""
	row["state"] = "idle"; row["target_id"] = 0; row["target_kind"] = "battalion"; row["attack_cooldown"] = 0; row["attack_windup"] = 0
	row["attack_move"] = false; row["order_kind"] = ""; row["current_speed"] = 0.0; row["knockdown_ticks"] = 0; row["production_exit_start_tick"] = -1
	var members := maxi(1, (row.get("member_health", []) as Array).size())
	row["member_positions"] = []
	row["member_attack_tokens"] = []; row["member_attack_start_ticks"] = []; row["member_attack_hit_ticks"] = []; row["member_target_ids"] = []; row["member_target_indices"] = []; row["member_weapon_modes"] = []; row["member_attack_release_tokens"] = []
	for _i in members:
		(row["member_positions"] as Array).append(Vector2.ZERO); (row["member_attack_tokens"] as Array).append(0); (row["member_attack_start_ticks"] as Array).append(-1); (row["member_attack_hit_ticks"] as Array).append(-1); (row["member_target_ids"] as Array).append(0); (row["member_target_indices"] as Array).append(-1); (row["member_weapon_modes"] as Array).append("default"); (row["member_attack_release_tokens"] as Array).append(0)
	row["category"] = "cavalry" if kinds.has("CAVALRY") else "infantry"; row["kind_of"] = kinds; row["object_status"] = {}; row["stance"] = "Battle"
	for key in ["presentation_hidden", "contained_can_attack", "transport_prior_status", "transport_prior_vision_range", "cower_until_tick", "stun_until_tick", "capture_channel", "grab_passenger_channel", "fling_passenger_channel", "repair_structure_channel", "dominate_enemy_channel", "activate_module_channel", "siege_deploy_channel", "volley_channel"]: row.erase(key)
	return row


func _submit(sim: RetailSliceSim, kind: String, entity_id: int, structure_id: int, seq: int) -> void:
	_check("command_%s_%d_submitted" % [kind, seq], sim.submit_command({"tick": sim.tick_index + 1, "team": 0, "seq": seq, "type": kind, "args": {"entity_id": entity_id, "structure_id": structure_id}}))


func _check(name: String, ok: bool) -> void:
	if ok: passed += 1; print("CASTLE_GARRISON PASS " + name)
	else: failed += 1; printerr("CASTLE_GARRISON FAIL " + name)


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED: failed += 1; printerr("CASTLE_GARRISON FAIL liveness ran=%d expected=%d" % [ran, EXPECTED])
	print("CASTLE_GARRISON_RESULT passed=%d failed=%d" % [passed, failed])
	watchdog.stop(); quit(0 if failed == 0 else 1)
