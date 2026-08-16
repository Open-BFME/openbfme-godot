extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const PackCapability = preload("res://src/content/pack_capability.gd")
const UnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
var passed := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	_selected_pack_effect_graph_check()
	var sim := _sim()
	_check("typed_effect_is_castable", bool(sim.spellbook_power("SpellBookScavenger").get("castable", false)))
	sim.team_power_points[0] = 5
	var purchase: Dictionary = sim.purchase_power(0, "SpellBookScavenger")
	_check("purchase_triggers_passive", bool(purchase.get("ok", false)) and bool(purchase.get("passive_activated", false)))
	_check("nonpressable_preserved", bool(sim.spellbook_power("SpellBookScavenger").get("nonpressable", false)))
	_check("ui_marks_nonpressable", bool(((sim.spellbook_ui_state(0).get("powers", {}) as Dictionary).get("SpellBookScavenger", {}) as Dictionary).get("nonpressable", false)))
	_check("manual_cast_rejected", String(sim.cast_power(0, "SpellBookScavenger", Vector2.ZERO).get("reason", "")) == "power-already-activated")
	var hud_script: GDScript = load("res://src/retail_slice/retail_hud.gd")
	var hud: Control = hud_script.new()
	root.add_child(hud)
	hud.powers_dock = Control.new()
	hud.add_child(hud.powers_dock)
	hud._refresh_powers_dock(sim.purchased_powers[0], sim.spellbook_ui_state(0))
	_check("nonpressable_never_docks", not hud.powers_dock_buttons.has("SpellBookScavenger"))
	hud.free()

	# One authored member death pays exactly once; repeating damage on the corpse
	# cannot cross the prior-health boundary again.
	var before: int = sim.resources_for_team(0)
	sim._apply_damage(1, 2, 100, "battalion", "NORMAL", "unresistable")
	_check("enemy_member_award", sim.resources_for_team(0) == before + 75)
	sim._apply_damage(1, 2, 100, "battalion", "NORMAL", "unresistable")
	_check("dead_member_no_double_award", sim.resources_for_team(0) == before + 75)

	# Friendly and self deaths never pay, even when the victim authors a value.
	sim._add_battalion(3, 0, Vector2(3, 0), "Friendly", "Victim", "Victim")
	before = sim.resources_for_team(0)
	sim._apply_damage(1, 3, 100, "battalion", "NORMAL", "unresistable")
	_check("friendly_kill_no_award", sim.resources_for_team(0) == before)
	sim._add_battalion(4, 0, Vector2(4, 0), "Self", "Victim", "Victim")
	sim._apply_damage(4, 4, 100, "battalion", "NORMAL", "unresistable")
	_check("self_kill_no_award", sim.resources_for_team(0) == before)

	# An absent BountyValue is not guessed from cost/health/category.
	sim._add_battalion(5, 1, Vector2(5, 0), "No bounty", "NoBounty", "NoBounty")
	sim._apply_damage(1, 5, 100, "battalion", "NORMAL", "unresistable")
	_check("absent_bounty_no_award", sim.resources_for_team(0) == before)

	# Structure BountyValue comes from its own compiled scalar map and is paid on
	# the shared structure zero-health boundary, once.
	sim._structure_bounty_values["lair"] = 999
	sim._team_structure_bounty_values[1] = {"lair": 240}
	sim.structures[50] = {"id": 50, "team": 1, "structure_kind": "lair", "health": 100, "maximum_health": 100, "damage_remainders": {}, "queue": [], "upgrade_queue": [], "position": Vector2.ZERO}
	sim._apply_structure_damage(1, 50, 1000, "unresistable")
	_check("enemy_structure_award", sim.resources_for_team(0) == before + 240)
	sim._apply_structure_damage(1, 50, 1000, "unresistable")
	_check("dead_structure_no_double_award", sim.resources_for_team(0) == before + 240)

	# Ownership comes from the living killer, not the local/player team.
	var enemy_team_sim := _sim()
	enemy_team_sim.team_power_points[1] = 5
	enemy_team_sim.purchase_power(1, "SpellBookScavenger")
	enemy_team_sim._add_battalion(20, 1, Vector2(20, 0), "Enemy attacker", "Attacker", "Attacker")
	enemy_team_sim._add_battalion(21, 0, Vector2(21, 0), "Player victim", "Victim", "Victim")
	var player_before: int = enemy_team_sim.resources_for_team(0)
	var enemy_before: int = enemy_team_sim.resources_for_team(1)
	enemy_team_sim._apply_damage(20, 21, 100, "battalion", "NORMAL", "unresistable")
	_check("killer_team_receives_award", enemy_team_sim.resources_for_team(1) == enemy_before + 75 and enemy_team_sim.resources_for_team(0) == player_before)

	# Activation and once-only consumption are authoritative save/hash state.
	var hash_before: String = sim.state_hash()
	var bytes: PackedByteArray = sim.snapshot()
	var restored := Sim.new()
	_check("snapshot_restores", restored.restore(bytes))
	_check("snapshot_hash_roundtrip", restored.state_hash() == hash_before)
	_check("restored_once_per_match", String(restored.cast_power(0, "SpellBookScavenger", Vector2.ZERO).get("reason", "")) == "power-already-activated")
	restored._add_battalion(9, 1, Vector2(9, 0), "Restored victim", "Victim", "Victim")
	var restored_before: int = restored.resources_for_team(0)
	restored._apply_damage(1, 9, 100, "battalion", "NORMAL", "unresistable")
	_check("restored_scavenger_remains_active", restored.resources_for_team(0) == restored_before + 75)

	# A malformed purchase-time trigger is transactional: nothing authoritative
	# moves when preflight refuses it.
	var invalid := _sim()
	invalid.team_power_points[0] = 5
	var invalid_row := ((invalid._team_tree(0).get("powers", {}) as Dictionary).get("SpellBookScavenger", {}) as Dictionary)
	(invalid_row.get("effect", {}) as Dictionary)["bounty_percent"] = -1.0
	var invalid_hash: String = invalid.state_hash()
	var invalid_result: Dictionary = invalid.purchase_power(0, "SpellBookScavenger")
	_check("invalid_purchase_refused", String(invalid_result.get("reason", "")) == "invalid-scavenger-contract")
	_check("invalid_purchase_rolls_back", invalid.power_points(0) == 5 and not invalid.has_power(0, "SpellBookScavenger") and invalid.state_hash() == invalid_hash)

	# RESET rolls a staged CommandTrigger back too; ACCEPT commits it.
	var reset_sim := _sim()
	reset_sim.team_power_points[0] = 5
	reset_sim.purchase_power(0, "SpellBookScavenger")
	var reset_result: Dictionary = reset_sim.reset_spellbook_purchases(0)
	var reset_before: int = reset_sim.resources_for_team(0)
	reset_sim._apply_damage(1, 2, 100, "battalion", "NORMAL", "unresistable")
	_check("reset_rolls_back_passive", bool(reset_result.get("ok", false)) and reset_sim.power_points(0) == 5 and not reset_sim.has_power(0, "SpellBookScavenger") and reset_sim.resources_for_team(0) == reset_before)

	print("SCAVENGER_BOUNTY_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _selected_pack_effect_graph_check() -> void:
	var mod_loader = root.get_node_or_null("ModLoader")
	var content_db = root.get_node_or_null("ContentDB")
	_check("selected_pack_mod_loader", mod_loader != null)
	_check("selected_pack_content_db", content_db != null)
	if mod_loader == null or content_db == null: return
	var fighter_doc := content_db.get_playable_unit_runtime("GondorFighterHorde") as Dictionary
	var absent_doc := fighter_doc.duplicate(true)
	var absent_resolved := ((((absent_doc.get("registration", {}) as Dictionary).get("simulation", {}) as Dictionary).get("resolved", {}) as Dictionary))
	absent_resolved.erase("bountyValue")
	var absent_rule := UnitAdapter.simulation_rule(absent_doc, false)
	var injected_doc := fighter_doc.duplicate(true)
	var injected_resolved := ((((injected_doc.get("registration", {}) as Dictionary).get("simulation", {}) as Dictionary).get("resolved", {}) as Dictionary))
	injected_resolved["bountyValue"] = {"value": 4, "expression": "GONDOR_SOLDIER_BOUNTY_VALUE", "sourceIni": "fixture.ini", "line": 1}
	var injected_rule := UnitAdapter.simulation_rule(injected_doc, false)
	_check("adapter_preserves_authored_bounty_and_absence", not absent_rule.has("bounty_value") and int(injected_rule.get("bounty_value", -1)) == 4)
	var doc: Dictionary = {}
	for pack_root_value in mod_loader.list_pack_roots():
		var pack_root := String(pack_root_value)
		if not PackCapability.provides_faction(pack_root, "Wild"): continue
		var pack := mod_loader._read_json(pack_root.path_join("pack.json")) as Dictionary
		for key_value in (pack.get("files", {}) as Dictionary).keys():
			var key := String(key_value)
			if not key.begins_with("spellbook."): continue
			var relative := String((pack.get("files", {}) as Dictionary).get(key, ""))
			var candidate := mod_loader._read_json(mod_loader.resolve_pack_path(pack_root, relative)) as Dictionary
			if String((candidate.get("target", {}) as Dictionary).get("faction", "")) == "Wild":
				doc = candidate
				break
		if not doc.is_empty(): break
	_check("selected_wild_spellbook_document", not doc.is_empty())
	if doc.is_empty(): return
	var raw_power: Dictionary = {}
	for value in (((doc.get("registration", {}) as Dictionary).get("powerTree", {}) as Dictionary).get("powers", []) as Array):
		if String((value as Dictionary).get("id", "")) == "SpellBookScavenger": raw_power = value as Dictionary
	var raw_fields := (((raw_power.get("effect", {}) as Dictionary).get("fields", [])) as Array)
	var exact_graph: bool = String((raw_power.get("effect", {}) as Dictionary).get("module", "")) == "ScavengerSpecialPower" and ((raw_power.get("cast", {}) as Dictionary).get("options", []) as Array).has("NONPRESSABLE")
	for field_value in raw_fields:
		var field := field_value as Dictionary
		if String(field.get("key", "")) == "BountyPercent": exact_graph = exact_graph and String(field.get("value", "")) == "1.0"
	_check("selected_pack_exact_scavenger_graph", exact_graph)
	var selected_sim := Sim.new()
	_check("selected_pack_scavenger_configures", selected_sim.configure_spellbook_runtime(doc), selected_sim.spellbook_error())
	var row := selected_sim.spellbook_power("SpellBookScavenger")
	_check("selected_pack_scavenger_is_castable", bool(row.get("castable", false)), String(row.get("locked_reason", "")))
	_check("selected_pack_scavenger_effect", String((row.get("effect", {}) as Dictionary).get("kind", "")) == "scavenger_bounty" and is_equal_approx(float((row.get("effect", {}) as Dictionary).get("bounty_percent", -1.0)), 1.0) and bool(row.get("nonpressable", false)))

func _sim() -> Object:
	var sim := Sim.new()
	_check("spellbook_configures", sim.configure_spellbook_runtime(_document()), sim.spellbook_error())
	var rules := {"Attacker": _rule(0), "Victim": _rule(75), "NoBounty": _rule(-1)}
	for required_id in [Sim.SOLDIER_OBJECT_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID, Sim.BUILDER_OBJECT_ID]:
		rules[required_id] = _rule(-1)
	sim.setup({}, {"unit_rules": rules, "source_map_transform_scale": 1.0})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim.team_resources[0] = 0
	sim.team_resources[1] = 0
	sim._add_battalion(1, 0, Vector2.ZERO, "Attacker", "Attacker", "Attacker")
	sim._add_battalion(2, 1, Vector2(2, 0), "Victim", "Victim", "Victim")
	return sim

func _rule(bounty: int) -> Dictionary:
	var row := {"horde_id":"Fixture", "category":"infantry", "speed":1.0, "speed_source":1.0, "acceleration":1.0, "acceleration_source":1.0, "turn_rate_degrees_per_second":180.0, "braking":1.0, "braking_source":1.0, "attack_range":1.0, "attack_range_source":1.0, "minimum_attack_range":0.0, "minimum_attack_range_source":0.0, "vision_range":10.0, "vision_range_source":10.0, "delay_between_shots_ms":100.0, "pre_attack_delay_ms":0.0, "firing_duration_ms":0.0, "attack_period_ticks":1, "pre_attack_ticks":0, "firing_duration_ticks":0, "member_health":100, "member_damage":100, "member_count":1, "formation_positions":[Vector3.ZERO], "provenance":{}}
	if bounty >= 0: row["bounty_value"] = bounty
	return row

func _document() -> Dictionary:
	return {"schema":"openbfme.spellbook-runtime", "registration":{"spellBook":{"intrinsicSciences":[]}, "powerTree":{"sciences":[{"id":"SCIENCE_Scavenger", "pointCostMP":{"value":5}, "purchase":{"slot":1}, "prerequisiteGroups":[]}], "powers":[{"id":"SpellBookScavenger", "requiredSciences":["SCIENCE_Scavenger"], "reloadTimeMs":{"value":0, "expression":"0"}, "cast":{"slot":1, "options":["NONPRESSABLE"]}, "effect":{"module":"ScavengerSpecialPower", "fields":[{"key":"BountyPercent", "value":"1.0"}], "references":{}}}]}, "leaves":{"attributeModifiers":[], "objects":[], "objectCreationLists":[], "weapons":[]}}}

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("SCAVENGER_BOUNTY PASS %s" % name)
	else:
		failed += 1
		print("SCAVENGER_BOUNTY FAIL %s %s" % [name, detail])
