extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 37
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim(_contract(50.0, "Upgrade_Defiance", ["ANY", "+GondorMarketPlace"]), true, true)
	var dying := sim.structures[1000] as Dictionary
	var policy := ((dying.get("refund_die", []) as Array)[0] as Dictionary)
	_check("RefundDie_typed_executable_row_attaches", not policy.is_empty())
	_check("percentage_fraction_is_exact", is_equal_approx(float(policy.get("fraction", 0.0)), 0.5))
	_check("upgrade_condition_is_exact", String(policy.get("upgrade_required", "")) == "Upgrade_Defiance")
	_check("building_filter_tokens_are_exact", policy.get("building_required", []) == ["ANY", "+GondorMarketPlace"])
	_check("death_scope_is_generic_object", String(policy.get("death_scope", "")) == "object-death-edge")
	var before := sim.resources_for_team(Sim.PLAYER_TEAM)
	sim._apply_refund_die_on_death(dying)
	_check("cached_cost_uses_ceil", sim.resources_for_team(Sim.PLAYER_TEAM) == before + 301)
	_check("static_kind_cost_is_not_used", sim.resources_for_team(Sim.PLAYER_TEAM) != before + 500)
	_check("refund_event_emitted_once", _event_count(sim, "economy.refund") == 1)
	_check("death_dispatch_latched", bool(dying.get("refund_die_death_dispatched", false)))
	var digest := sim.state_hash(); var snapshot := sim.snapshot(); var restored := _sim(_contract(50.0), false, false)
	_check("snapshot_preserves_cached_cost_death_latch_and_hash", restored.restore(snapshot) and restored.state_hash() == digest and float((restored.structures[1000] as Dictionary).get("cached_build_cost", -1.0)) == 601.0 and bool((restored.structures[1000] as Dictionary).get("refund_die_death_dispatched", false)))
	sim._apply_refund_die_on_death(dying)
	_check("repeated_death_callback_does_not_double_refund", sim.resources_for_team(Sim.PLAYER_TEAM) == before + 301 and _event_count(sim, "economy.refund") == 1)

	var under_construction := _sim(_contract(50.0), false, false)
	(under_construction.structures[1000] as Dictionary)["object_status"] = {"UNDER_CONSTRUCTION": true, "SOLD": true}
	var uc_before := under_construction.resources_for_team(Sim.PLAYER_TEAM)
	under_construction._apply_refund_die_on_death(under_construction.structures[1000] as Dictionary)
	var uc_policy := (((under_construction.structures[1000] as Dictionary).get("refund_die", []) as Array)[0] as Dictionary)
	_check("under_construction_blocks_combat_refund", under_construction.resources_for_team(Sim.PLAYER_TEAM) == uc_before)
	_check("construction_guard_precedes_sold", String(uc_policy.get("death_blocker", "")) == "UNDER_CONSTRUCTION")
	_check("blocked_death_still_latches_dispatch", bool((under_construction.structures[1000] as Dictionary).get("refund_die_death_dispatched", false)))
	var progress_guard := _sim(_contract(50.0), false, false); (progress_guard.structures[1000] as Dictionary)["construction_progress"] = 0.5
	var progress_before := progress_guard.resources_for_team(Sim.PLAYER_TEAM); progress_guard._apply_refund_die_on_death(progress_guard.structures[1000] as Dictionary)
	_check("live_construction_progress_maps_to_retail_status_guard", progress_guard.resources_for_team(Sim.PLAYER_TEAM) == progress_before)

	var sold := _sim(_contract(50.0), false, false)
	(sold.structures[1000] as Dictionary)["object_status"] = {"SOLD": true}
	var sold_before := sold.resources_for_team(Sim.PLAYER_TEAM)
	sold._apply_refund_die_on_death(sold.structures[1000] as Dictionary)
	_check("sold_death_does_not_stack_refund", sold.resources_for_team(Sim.PLAYER_TEAM) == sold_before)

	var late_upgrade := _sim(_contract(50.0, "Upgrade_Defiance"), false, false)
	var late_before := late_upgrade.resources_for_team(Sim.PLAYER_TEAM)
	late_upgrade._apply_refund_die_on_death(late_upgrade.structures[1000] as Dictionary)
	late_upgrade.team_upgrades[Sim.PLAYER_TEAM] = {"Upgrade_Defiance": true}
	late_upgrade._apply_refund_die_on_death(late_upgrade.structures[1000] as Dictionary)
	_check("failed_requirement_has_no_later_chance", late_upgrade.resources_for_team(Sim.PLAYER_TEAM) == late_before)

	for rejected_status in ["EFFECTIVELY_DEAD", "DESTROYED"]:
		var dead_requirement := _sim(_contract(50.0, "", ["ANY", "+GondorMarketPlace"]), false, true)
		(dead_requirement.structures[1001] as Dictionary)["object_status"] = {rejected_status: true}
		var dead_before := dead_requirement.resources_for_team(Sim.PLAYER_TEAM)
		dead_requirement._apply_refund_die_on_death(dead_requirement.structures[1000] as Dictionary)
		_check("required_building_rejects_%s" % rejected_status, dead_requirement.resources_for_team(Sim.PLAYER_TEAM) == dead_before)
	var inert_requirement := _sim(_contract(50.0, "", ["ANY", "+GondorMarketPlace"]), false, true)
	(inert_requirement.structures[1001] as Dictionary)["kind_of"] = ["STRUCTURE", "GondorMarketPlace", "INERT"]
	var inert_before := inert_requirement.resources_for_team(Sim.PLAYER_TEAM)
	inert_requirement._apply_refund_die_on_death(inert_requirement.structures[1000] as Dictionary)
	_check("required_building_rejects_KINDOF_INERT", inert_requirement.resources_for_team(Sim.PLAYER_TEAM) == inert_before)

	for admitted_status in ["UNDER_CONSTRUCTION", "SOLD"]:
		var live_requirement := _sim(_contract(50.0, "", ["ANY", "+GondorMarketPlace"]), false, true)
		(live_requirement.structures[1001] as Dictionary)["object_status"] = {admitted_status: true}
		var live_before := live_requirement.resources_for_team(Sim.PLAYER_TEAM)
		live_requirement._apply_refund_die_on_death(live_requirement.structures[1000] as Dictionary)
		_check("requirement_does_not_invent_%s_exclusion" % admitted_status, live_requirement.resources_for_team(Sim.PLAYER_TEAM) == live_before + 301)

	var captured := _sim(_contract(50.0, "Upgrade_Defiance", ["ANY", "+GondorMarketPlace"]), false, false)
	var captured_row := captured.structures[1000] as Dictionary
	var player_before_capture := captured.resources_for_team(Sim.PLAYER_TEAM)
	var enemy_before_capture := captured.resources_for_team(Sim.ENEMY_TEAM)
	captured_row["team"] = Sim.ENEMY_TEAM
	captured.team_upgrades[Sim.ENEMY_TEAM] = {"Upgrade_Defiance": true}
	_add_market(captured, 1002, Sim.ENEMY_TEAM)
	_check("capture_itself_does_not_refund", captured.resources_for_team(Sim.PLAYER_TEAM) == player_before_capture and captured.resources_for_team(Sim.ENEMY_TEAM) == enemy_before_capture)
	captured._apply_refund_die_on_death(captured_row)
	_check("postcapture_death_credits_current_owner", captured.resources_for_team(Sim.ENEMY_TEAM) == enemy_before_capture + 301)
	_check("postcapture_death_does_not_credit_original_owner", captured.resources_for_team(Sim.PLAYER_TEAM) == player_before_capture)

	var dead_owner := _sim(_contract(50.0), false, false)
	(dead_owner.structures[1000] as Dictionary)["team"] = -1
	dead_owner._apply_refund_die_on_death(dead_owner.structures[1000] as Dictionary)
	_check("missing_current_owner_blocks_refund", not dead_owner.team_resources.has(-1))

	var zero := _sim(_contract(0.0), false, false)
	var zero_before := zero.resources_for_team(Sim.PLAYER_TEAM)
	zero._apply_refund_die_on_death(zero.structures[1000] as Dictionary)
	_check("zero_amount_emits_no_refund", zero.resources_for_team(Sim.PLAYER_TEAM) == zero_before and _event_count(zero, "economy.refund") == 0)
	_check("zero_amount_still_latches_death", bool((zero.structures[1000] as Dictionary).get("refund_die_death_dispatched", false)))

	var deferred := _sim(_contract(50.0, "", [], "deferred"), false, false)
	_check("deferred_sibling_row_does_not_attach", not (deferred.structures[1000] as Dictionary).has("refund_die"))
	var opaque := _contract(50.0); opaque["extraction"] = "opaque-authored"
	_check("opaque_contract_fails_closed", not (_sim(opaque, false, false).structures[1000] as Dictionary).has("refund_die"))
	var malformed := _contract(50.0); ((malformed["fields"] as Dictionary)["RefundPercent"] as Dictionary)["fraction"] = 1.5
	_check("malformed_fraction_fails_closed", not (_sim(malformed, false, false).structures[1000] as Dictionary).has("refund_die"))
	var missing_cost := _sim(_contract(50.0), false, false); (missing_cost.structures[1000] as Dictionary).erase("cached_build_cost")
	var missing_before := missing_cost.resources_for_team(Sim.PLAYER_TEAM); missing_cost._apply_refund_die_on_death(missing_cost.structures[1000] as Dictionary)
	_check("missing_cached_cost_fails_closed", missing_cost.resources_for_team(Sim.PLAYER_TEAM) == missing_before)

	var tribute := _sim(_contract(50.0), false, false)
	tribute.register_unit_module_contracts("MordorTributeCart", [_contract(100.0)])
	tribute.entities[777] = {"id": 777, "team": Sim.PLAYER_TEAM, "unit_type": "MordorTributeCart", "object_id": "MordorTributeCart", "health": 0, "maximum_health": 1, "cached_build_cost": 601}
	tribute._attach_module_contracts(tribute.entities[777] as Dictionary)
	_check("MordorTributeCart_generic_object_row_attaches", (tribute.entities[777] as Dictionary).has("refund_die"))
	var tribute_before := tribute.resources_for_team(Sim.PLAYER_TEAM)
	tribute._apply_refund_die_on_death(tribute.entities[777] as Dictionary)
	_check("MordorTributeCart_unconditional_full_refund", tribute.resources_for_team(Sim.PLAYER_TEAM) == tribute_before + 601)
	var charged := {"unit_type": "MordorTributeCart", "cached_build_cost": 347}; tribute._unit_production_rules["MordorTributeCart"] = {"default_cost": 999}
	tribute._cache_refund_die_build_cost(charged)
	_check("charged_production_cost_is_cached_not_recomputed", int(charged.get("cached_build_cost", -1)) == 347)

	var damage_path := _sim(_contract(50.0), false, false); (damage_path.structures[1000] as Dictionary)["health"] = 100
	var damage_before := damage_path.resources_for_team(Sim.PLAYER_TEAM); damage_path._apply_structure_damage(0, 1000, 1000, "SIEGE")
	_check("authoritative_combat_death_dispatches_refund", damage_path.resources_for_team(Sim.PLAYER_TEAM) == damage_before + 301)
	_check("typed_provenance_is_preserved", String(policy.get("tag", "")) == "ModuleTag_refund" and int(policy.get("line", 0)) == 10)

	if passed + failed != EXPECTED: failed += 1; printerr("REFUND_DIE_RUNTIME_FAIL liveness expected=%d actual=%d" % [EXPECTED, passed + failed])
	print("REFUND_DIE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _contract(percent: float, upgrade: String = "", building_filter: Array = [], status: String = "executable") -> Dictionary:
	var fields := {"RefundPercent": {"authored": "%s%%" % percent, "percent": percent, "fraction": percent / 100.0, "sourceIni": "fixture/object.ini", "line": 11}}
	if upgrade != "": fields["UpgradeRequired"] = {"authored": upgrade, "value": upgrade, "sourceIni": "fixture/object.ini", "line": 12}
	if not building_filter.is_empty(): fields["BuildingRequired"] = {"authored": " ".join(building_filter), "value": building_filter.duplicate(), "sourceIni": "fixture/object.ini", "line": 13}
	return {"module": "RefundDie", "runtimeStatus": status, "executable": status == "executable", "extraction": "typed", "tag": "ModuleTag_refund", "sourceIni": "fixture/object.ini", "line": 10, "fields": fields}

func _sim(contract: Dictionary, owns_upgrade: bool, has_required_building: bool) -> RetailSliceSim:
	# RefundDie has no tick/physics dependency. Avoid a full retail setup per
	# matrix row: initialize only the authoritative stores this behavior reads.
	var sim: RetailSliceSim = Sim.new(); sim.ai_enabled = false; sim.base_loop_enabled = false; sim.structures.clear(); sim.entities.clear(); sim.events.clear(); sim.team_resources[Sim.PLAYER_TEAM] = 1000; sim.team_resources[Sim.ENEMY_TEAM] = 1000
	sim._structure_build_rules["fixture"] = {"cost": 999}
	sim._structure_armor["fixture"] = {"damage_scalar": 1.0, "scalars": {"default": 1.0, "siege": 1.0}}
	sim.register_structure_module_contracts("FixtureRefund", [contract]); sim.structures[1000] = {"id": 1000, "team": Sim.PLAYER_TEAM, "source_object_id": "FixtureRefund", "structure_kind": "fixture", "health": 0, "maximum_health": 100, "construction_progress": 1.0, "cached_build_cost": 601}; sim._attach_structure_module_contracts(sim.structures[1000] as Dictionary)
	if owns_upgrade: sim.team_upgrades[Sim.PLAYER_TEAM] = {"Upgrade_Defiance": true}
	if has_required_building: _add_market(sim, 1001, Sim.PLAYER_TEAM)
	return sim

func _add_market(sim: RetailSliceSim, id: int, team: int) -> void:
	sim.structures[id] = {"id": id, "team": team, "source_object_id": "GondorMarketPlace", "structure_kind": "marketplace", "category": "structure", "kind_of": ["STRUCTURE", "GondorMarketPlace"], "health": 100, "maximum_health": 100, "construction_progress": 1.0}

func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var count := 0; for value in sim.events: if String((value as Dictionary).get("kind", "")) == kind: count += 1
	return count

func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("REFUND_DIE_RUNTIME_FAIL " + name)
