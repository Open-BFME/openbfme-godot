extends SceneTree

## Headless selected-content proof for RetailSliceSim banner lifecycle and
## CastleBehavior unpacking. Safe to invoke bare from the repository root:
##   Godot --headless --path game --script res://tests/banner_castle_silent_playtest_runner.gd

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

var _runner_watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_runner_watchdog.start(self, "BANNER_CASTLE_SILENT_PLAYTEST", 600_000)
	call_deferred("_run")


func _run() -> void:
	print("BANNER_CASTLE_SILENT_PLAYTEST start")
	var content_db = root.get_node_or_null("/root/ContentDB")
	if content_db == null:
		_fail("ContentDB autoload missing")
		_finish()
		return
	if (content_db.pack_roots as Array).is_empty():
		# Older bare --script launches did not discover the workspace cache.
		# Reload the repository selection explicitly; never rewrite selection.json.
		var workspace := ProjectSettings.globalize_path("res://../workspace/content-packs")
		OS.set_environment("OPENBFME_CONTENT", workspace)
		content_db.reload()
	_check(not (content_db.pack_roots as Array).is_empty(), "selected content roots loaded")
	if (content_db.pack_roots as Array).is_empty():
		_finish()
		return

	var runtimes: Dictionary = content_db.get_playable_unit_runtimes()
	var structures: Dictionary = content_db.get_playable_structure_runtimes()
	var fighter := _runtime(runtimes, "GondorFighterHorde")
	var banner_doc := _runtime(runtimes, "GondorInfantryBanner")
	var fortress_doc := _runtime(structures, "MenFortress")
	_check(not fighter.is_empty(), "GondorFighterHorde runtime present")
	_check(not banner_doc.is_empty(), "GondorInfantryBanner runtime present")
	_check(not fortress_doc.is_empty(), "MenFortress runtime present")
	if fighter.is_empty() or banner_doc.is_empty() or fortress_doc.is_empty():
		_finish()
		return

	_test_banner_lifecycle(fighter)
	_test_castle_unpack(fortress_doc)
	_finish()


func _test_banner_lifecycle(fighter: Dictionary) -> void:
	var source_rule := Adapter.simulation_rule(fighter)
	var unit_rule := Adapter.normalized_unit_rule(source_rule, 0.1)
	_check(not unit_rule.is_empty(), "fighter normalized simulation rule")
	if unit_rule.is_empty():
		return
	var member_id := Adapter.runtime_member_id(fighter)
	var unit_id := Adapter.runtime_unit_id(fighter)
	var sim = Sim.new()
	sim._rules = {"unit_rules": {member_id: unit_rule}, "source_unit_scale": 0.1}
	var banner_rule: Dictionary = sim._compiled_banner_carrier(fighter)
	_check(not banner_rule.is_empty(), "banner contract compiles from selected target")
	_check(int(banner_rule.get("min_level", -1)) == 2, "rank-2 minimum preserved")
	_check(int(banner_rule.get("banner_max_health", 0)) == 200, "authored banner health preserved")
	_check(int(banner_rule.get("respawn_ticks", -1)) == 200, "max 20s respawn timer becomes 200 ticks")
	if banner_rule.is_empty():
		return
	sim._unit_banner_carriers[unit_id] = banner_rule
	sim._unit_banner_carriers[member_id] = banner_rule
	var horde_id := 9001
	sim._add_battalion(horde_id, Sim.PLAYER_TEAM, Vector2(5.0, 7.0), "GondorFighterHorde", member_id, unit_id, 0)
	_check(sim.entities.has(horde_id), "fighter horde spawned")
	if not sim.entities.has(horde_id):
		return
	var horde: Dictionary = sim.entities[horde_id]
	_check(not bool(horde.get("banner_carrier_spawned", false)), "rank 1 has no banner")
	horde["level"] = 2
	sim._refresh_banner_carrier_state(horde)
	var banner_id := int(horde.get("banner_entity_id", 0))
	_check(bool(horde.get("banner_carrier_spawned", false)), "rank 2 spawns banner")
	_check(banner_id != 0 and sim.entities.has(banner_id), "real banner entity registered")
	if banner_id == 0 or not sim.entities.has(banner_id):
		return
	var banner: Dictionary = sim.entities[banner_id]
	_check(bool(banner.get("is_banner_carrier", false)), "banner entity carries owner link")
	_check(int(banner.get("banner_parent_entity_id", 0)) == horde_id, "banner owner is fighter horde")
	_check(int(banner.get("maximum_health", 0)) == 200, "banner entity uses authored health")

	var content_db = root.get_node_or_null("/root/ContentDB")
	var definition: Dictionary = content_db.get_bundle_object(String(banner_rule["object_id"]))
	var mesh_path := String(content_db.resolve_mesh_path(definition))
	_check(not definition.is_empty() and mesh_path.to_lower().ends_with(".glb") and FileAccess.file_exists(mesh_path), "banner presentation binds an authored GLB")

	# Selected Men fighter does not destroy on banner death; it must wait the
	# authored max timer and not return one tick early.
	sim.script_kill_entity(banner_id)
	_check(int(horde.get("health", 0)) > 0, "Men fighter survives banner death")
	_check(not bool(horde.get("banner_carrier_spawned", true)), "dead banner presentation clears")
	_check(int(horde.get("banner_respawn_ticks_remaining", -1)) == 200, "respawn lower bound arms at 200")
	_step_banner_lifecycle(sim, 199)
	_check(not bool(horde.get("banner_carrier_spawned", false)), "banner absent through tick 199")
	_step_banner_lifecycle(sim, 1)
	var replacement_id := int(horde.get("banner_entity_id", 0))
	_check(replacement_id != 0 and replacement_id != banner_id, "banner respawns at authored tick 200")

	# Exercise the authored destroy flag independently of Men's false value.
	var destroy_sim = _banner_fixture(unit_rule, member_id, unit_id, banner_rule, true, 9101)
	var destroy_parent: Dictionary = destroy_sim.entities[9101]
	var destroy_banner_id := int(destroy_parent.get("banner_entity_id", 0))
	destroy_sim.script_kill_entity(destroy_banner_id)
	_step_banner_lifecycle(destroy_sim, 1)
	_check(int(destroy_parent.get("health", 1)) == 0, "destroyHordeOnBannerDeath kills owning horde")

	# No timer means permanent absence; never invent a respawn.
	var no_timer_rule := banner_rule.duplicate(true)
	no_timer_rule["respawn_ticks"] = -1
	var no_timer_sim = _banner_fixture(unit_rule, member_id, unit_id, no_timer_rule, false, 9201)
	var no_timer_parent: Dictionary = no_timer_sim.entities[9201]
	var no_timer_banner_id := int(no_timer_parent.get("banner_entity_id", 0))
	no_timer_sim.script_kill_entity(no_timer_banner_id)
	_step_banner_lifecycle(no_timer_sim, 500)
	_check(not bool(no_timer_parent.get("banner_carrier_spawned", false)), "no authored timer means no respawn")


func _banner_fixture(unit_rule: Dictionary, member_id: String, unit_id: String, source_rule: Dictionary, destroy: bool, parent_id: int):
	var sim = Sim.new()
	sim._rules = {"unit_rules": {member_id: unit_rule}, "source_unit_scale": 0.1}
	var rule := source_rule.duplicate(true)
	rule["destroy_horde_on_death"] = destroy
	sim._unit_banner_carriers[member_id] = rule
	sim._unit_banner_carriers[unit_id] = rule
	sim._add_battalion(parent_id, Sim.PLAYER_TEAM, Vector2.ZERO, "banner-fixture", member_id, unit_id, 0)
	var parent: Dictionary = sim.entities[parent_id]
	parent["level"] = int(rule.get("min_level", 2))
	sim._refresh_banner_carrier_state(parent)
	return sim


func _step_banner_lifecycle(sim, ticks: int) -> void:
	for _index in range(ticks):
		sim.tick_index += 1
		sim._step_banner_carriers()


func _test_castle_unpack(fortress_doc: Dictionary) -> void:
	var sim = Sim.new()
	sim._rules = {
		"unit_rules": {},
		"source_unit_scale": 0.1,
		"faction_manifest": {"structure_source_object_ids": {"fortress": ["MenFortress"]}},
	}
	var castle: Dictionary = sim._compiled_castle_behavior(fortress_doc)
	_check(not castle.is_empty(), "MenFortress castleBehavior compiles")
	_check((castle.get("pieces", []) as Array).size() == 7, "Men castle contract has exactly 7 pieces")
	if castle.is_empty():
		return
	var fortress_id := 7001
	sim.structures[fortress_id] = {
		"id": fortress_id,
		"team": Sim.PLAYER_TEAM,
		"kind": "structure",
		"structure_kind": "fortress",
		"position": Vector2(10.0, 20.0),
		"elevation": 3.0,
		"facing_radians": 0.25,
		"health": 7500,
		"maximum_health": 7500,
		"construction_progress": 1.0,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"queue": [],
		"damage_remainders": {},
	}
	sim.configure_castle_behaviors({"MenFortress": castle})
	var owner: Dictionary = sim.structures[fortress_id]
	var child_ids: Array = owner.get("castle_piece_structure_ids", []) as Array
	_check(bool(owner.get("castle_behavior_unpacked", false)), "castle unpacks on first spawn/place")
	_check(child_ids.size() == 7, "seven authoritative castle piece structures registered")
	var source_ids: Array[String] = []
	for child_id_value in child_ids:
		var child: Dictionary = sim.structures.get(int(child_id_value), {})
		if not child.is_empty():
			source_ids.append(String(child.get("source_object_id", "")))
			_check(String(child.get("object_id", "")).begins_with("bfme2.object."), "castle piece resolves converted runtime template")
	_check(source_ids.count("MenFortressCitadel") == 1, "citadel materialized once")
	_check(source_ids.count("MenFortressExpansionPadSide") == 2, "two side pads materialized")
	_check(source_ids.count("MenFortressExpansionPadCorner") == 4, "four corner pads materialized")
	var first: Dictionary = sim.structures.get(int(child_ids[0]), {}) if not child_ids.is_empty() else {}
	_check(Vector2(first.get("position", Vector2.INF)).is_equal_approx(Vector2(10.0, 26.2)), "piece XYZ offset uses selected source scale")
	_check(is_equal_approx(float(first.get("facing_radians", 0.0)), 0.25 + 1.5707963705062866), "piece angle is relative to fortress")
	var before: int = sim.structures.size()
	sim._unpack_castle_behavior_for_structure(fortress_id)
	_check(sim.structures.size() == before, "castle unpacks exactly once")


func _runtime(registry: Dictionary, object_id: String) -> Dictionary:
	if registry.has(object_id):
		return (registry[object_id] as Dictionary).duplicate(true)
	for key in registry.keys():
		if String(key).to_lower() == object_id.to_lower():
			return (registry[key] as Dictionary).duplicate(true)
	return {}


func _check(condition: bool, name: String) -> void:
	if condition:
		passed += 1
		print("  PASS %s" % name)
	else:
		failed += 1
		printerr("  FAIL %s" % name)


func _fail(message: String) -> void:
	failed += 1
	printerr("  FAIL %s" % message)


func _finish() -> void:
	if failed == 0:
		print("BANNER_CASTLE_SILENT_PLAYTEST PASS checks=%d" % passed)
		quit(0)
	else:
		printerr("BANNER_CASTLE_SILENT_PLAYTEST FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)
