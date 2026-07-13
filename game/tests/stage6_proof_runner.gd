extends SceneTree
## Deterministic Stage 6 proof: factions, research, matrix, and legal-safe art.

const WorldScript = preload("res://src/proof_stage6/proof_world.gd")

var passed: int = 0
var failed: int = 0
var document: Dictionary = {}
var repeat_hash: String = "00000000"


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	document = _load_document()
	_check("external_faction_document_loads", not document.is_empty())
	if document.is_empty():
		_finish()
		return
	_test_catalog_contract()
	_test_damage_armor_matrix()
	_test_research()
	_test_combat()
	_test_replay_hash()
	_finish()


func _test_catalog_contract() -> void:
	var world := WorldScript.new()
	_check("catalog_configures", world.configure(document) == "")
	_check("catalog_has_four_factions", world.catalog.faction_ids().size() == 4)
	_check("catalog_has_eight_roster_units", world.catalog.unit_ids().size() == 8)
	_check("catalog_has_three_damage_types", world.catalog.damage_types == ["kinetic", "piercing", "arcane"])
	_check("catalog_has_three_armor_classes", world.catalog.armor_classes.size() == 3)
	_check("catalog_has_three_research_upgrades", world.catalog.upgrade_ids().size() == 3)
	for faction_id: String in world.catalog.faction_ids():
		_check("faction_%s_has_two_units" % faction_id, world.catalog.roster_for(faction_id).size() == 2)
		_check("faction_%s_has_two_upgrades" % faction_id, Array(world.catalog.faction(faction_id)["upgradeIds"]).size() == 2)
		_check("faction_%s_color_is_valid" % faction_id, Color.html_is_valid(String(world.catalog.faction(faction_id)["teamColor"])))
	var coverage: Dictionary = world.catalog.art_coverage()
	_check("art_resolution_covers_every_roster_entry", int(coverage["resolved"]) == 8 and int(coverage["total"]) == 8)
	_check("art_resolution_reports_no_missing_units", Array(coverage["missing"]).is_empty())
	_check("art_resolution_is_legal_safe", bool(coverage["legal_safe"]))
	var shapes: Dictionary = {}
	for unit_id: String in world.catalog.unit_ids():
		var art: Dictionary = world.catalog.resolve_art(unit_id)
		_check("art_%s_is_generated_primitive" % unit_id, String(art.get("source", "")) == "generated-primitive" and bool(art.get("resolved", false)))
		shapes[String(art.get("shape", ""))] = true
	_check("roster_exercises_six_primitive_shapes", shapes.size() == 6)
	_check("bounded_probe_spawns_eighty_battalions", world.setup_battalion_probe(80) == 80 and world.entity_ids().size() == 80)
	_check("eighty_battalion_probe_state_is_valid", world.validate_state() == "")
	var unsafe := document.duplicate(true)
	unsafe["factions"][0]["roster"][0]["art"]["source"] = "retail-file"
	var rejected := WorldScript.new()
	_check("unsafe_art_source_is_rejected", rejected.configure(unsafe) != "")
	var missing_matrix := document.duplicate(true)
	missing_matrix["armorClasses"][0]["multipliers"].erase("arcane")
	rejected = WorldScript.new()
	_check("incomplete_damage_matrix_is_rejected", rejected.configure(missing_matrix) != "")


func _test_damage_armor_matrix() -> void:
	var world := _new_world()
	var kinetic_plate: Dictionary = world.damage_preview("aurora_vanguard", "verdant_golem", "aurora_compact", "verdant_league")
	_check("kinetic_vs_plate_uses_half_multiplier", bool(kinetic_plate["ok"]) and int(kinetic_plate["damage"]) == 24 and int(kinetic_plate["matrix_numerator"]) == 1 and int(kinetic_plate["matrix_denominator"]) == 2)
	var piercing_cloth: Dictionary = world.damage_preview("aurora_ranger", "ember_breaker", "aurora_compact", "ember_union")
	_check("piercing_vs_cloth_uses_three_halves", int(piercing_cloth["damage"]) == 54 and int(piercing_cloth["matrix_numerator"]) == 3 and int(piercing_cloth["matrix_denominator"]) == 2)
	var arcane_warded: Dictionary = world.damage_preview("ember_channeler", "verdant_sentinel", "ember_union", "verdant_league")
	_check("arcane_vs_warded_uses_half_multiplier", int(arcane_warded["damage"]) == 21)
	var kinetic_warded: Dictionary = world.damage_preview("ember_breaker", "verdant_sentinel", "ember_union", "verdant_league")
	_check("kinetic_vs_warded_uses_five_quarters", int(kinetic_warded["damage"]) == 70)
	_check("matrix_unknown_unit_is_rejected", not bool(world.damage_preview("missing", "verdant_sentinel", "ember_union", "verdant_league").get("ok", false)))


func _test_research() -> void:
	var world := _new_world()
	var baseline: Dictionary = world.damage_preview("aurora_vanguard", "verdant_golem", "aurora_compact", "verdant_league")
	var started: Dictionary = world.begin_research("aurora_compact", "tempered_edges")
	_check("research_start_is_data_driven", bool(started["ok"]) and int(started["cost"]) == 120 and int(started["complete_tick"]) == 4)
	_check("research_cost_is_debited_once", int(world.faction_states["aurora_compact"]["resources"]) == 380)
	_check("one_research_job_per_faction", String(world.begin_research("aurora_compact", "trueflight_vanes").get("reason", "")) == "research_busy")
	world.advance(3)
	_check("research_not_early", world.completed_upgrades("aurora_compact").is_empty())
	world.tick()
	_check("research_completes_on_declared_tick", world.completed_upgrades("aurora_compact") == ["tempered_edges"])
	var upgraded: Dictionary = world.damage_preview("aurora_vanguard", "verdant_golem", "aurora_compact", "verdant_league")
	_check("damage_research_changes_matching_type", int(baseline["damage"]) == 24 and int(upgraded["damage"]) == 30 and int(upgraded["damage_bonus_permille"]) == 250)
	_check("completed_research_cannot_repeat", String(world.begin_research("aurora_compact", "tempered_edges").get("reason", "")) == "already_researched")
	_check("unavailable_faction_upgrade_is_rejected", String(world.begin_research("aurora_compact", "woven_wards").get("reason", "")) == "upgrade_unavailable")
	var defender_baseline: Dictionary = world.damage_preview("ember_channeler", "verdant_sentinel", "ember_union", "verdant_league")
	_check("defender_armor_baseline_is_exact", int(defender_baseline["damage"]) == 21)
	_check("defender_research_starts", bool(world.begin_research("verdant_league", "woven_wards")["ok"]))
	world.advance(5)
	var defender_upgraded: Dictionary = world.damage_preview("ember_channeler", "verdant_sentinel", "ember_union", "verdant_league")
	_check("armor_research_reduces_incoming_damage", int(defender_upgraded["damage"]) == 16 and int(defender_upgraded["armor_bonus_permille"]) == 250)
	_check("research_state_remains_valid", world.validate_state() == "")


func _test_combat() -> void:
	var world := _new_world()
	var attacker: int = _entity_for_unit(world, "aurora_vanguard")
	var target: int = _entity_for_unit(world, "verdant_golem")
	_check("combat_fixture_entities_exist", attacker > 0 and target > 0)
	_check("movement_for_fixture_is_collision_safe", world.set_entity_position(target, Vector2i(3, 6)))
	var old_health: int = int(world.entity(target)["health"])
	var result: Dictionary = world.attack(attacker, target)
	_check("live_combat_uses_matrix", bool(result["ok"]) and int(result["damage"]) == 24 and int(world.entity(target)["health"]) == old_health - 24)
	var after_first: int = int(world.entity(target)["health"])
	var cooldown: Dictionary = world.attack(attacker, target)
	_check("cooldown_rejects_early_attack", String(cooldown.get("reason", "")) == "cooldown")
	_check("cooldown_rejection_is_transactional", int(world.entity(target)["health"]) == after_first and world.combat_events.size() == 1)
	world.advance(3)
	_check("declared_cooldown_allows_next_attack", bool(world.attack(attacker, target)["ok"]))
	var friend: int = _entity_for_unit(world, "aurora_ranger")
	_check("friendly_fire_is_rejected", String(world.attack(attacker, friend).get("reason", "")) == "friendly_target")
	world.set_entity_position(target, Vector2i(13, 8))
	world.advance(3)
	_check("range_is_authoritative", String(world.attack(attacker, target).get("reason", "")) == "target_out_of_range")
	_check("combat_state_remains_valid", world.validate_state() == "")


func _test_replay_hash() -> void:
	var first := _run_replay()
	var second := _run_replay()
	repeat_hash = first.state_hash_text()
	_check("repeat_replay_hash_equal", first.state_hash() == second.state_hash(), repeat_hash)
	_check("repeat_replay_snapshot_equal", first.snapshot() == second.snapshot())
	_check("replay_finishes_valid", first.validate_state() == "")
	var before: int = first.state_hash()
	first.faction_states["aurora_compact"]["resources"] += 1
	_check("hash_covers_faction_resources", first.state_hash() != before)
	first.faction_states["aurora_compact"]["resources"] -= 1
	_check("hash_restores_after_resource_mutation", first.state_hash() == before)
	first.entities[_entity_for_unit(first, "verdant_golem")]["ready_tick"] += 1
	_check("hash_covers_future_cooldown_state", first.state_hash() != before)


func _run_replay() -> RefCounted:
	var world := _new_world()
	world.begin_research("aurora_compact", "tempered_edges")
	world.advance(4)
	var attacker: int = _entity_for_unit(world, "aurora_vanguard")
	var target: int = _entity_for_unit(world, "verdant_golem")
	world.set_entity_position(target, Vector2i(3, 6))
	world.attack(attacker, target)
	world.advance(3)
	world.attack(attacker, target)
	return world


func _new_world() -> RefCounted:
	var world := WorldScript.new()
	assert(world.configure(document) == "")
	world.setup_showcase()
	return world


func _entity_for_unit(world: RefCounted, unit_id: String) -> int:
	for entity_id: int in world.entity_ids():
		if String(world.entity(entity_id).get("unit_id", "")) == unit_id:
			return entity_id
	return 0


func _load_document() -> Dictionary:
	var path := ProjectSettings.globalize_path("res://../content/openbfme-test/data/faction_rosters.json")
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	return parsed if typeof(parsed) == TYPE_DICTIONARY else {}


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE6_METRICS repeat_hash=%s assertions=%d factions=4 roster=8 art_resolved=8" % [repeat_hash, passed])
		print("STAGE6_GODOT_PROOF PASS authority=gdscript-proof assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE6_GODOT_PROOF FAIL authority=gdscript-proof assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
