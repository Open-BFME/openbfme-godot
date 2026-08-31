extends SceneTree
## Exact four-branch RotWK 2.02 v9.7.7 Thrall replacement proof.  The wrapper
## mounts an isolated, twice-cooked real Angmar pack; this runner consumes it
## through ContentDB and the shipping sim/config/queue/aura paths.

# Load the named script classes before RetailSliceSim, matching established
# runtime runners and avoiding dependency-order reliance on an ambient cache.
const EnvScript = preload("res://src/script/script_env.gd")
const ExecutorScript = preload("res://src/script/script_executor.gd")
const SIM_PATH := "res://src/retail_slice/retail_slice_sim.gd"
const MANIFEST_PATH := "res://src/retail_slice/retail_faction_manifest.gd"
const SLICE_PATH := "res://src/retail_slice/retail_vertical_slice.gd"
const MAP_DATA_PATH := "res://src/retail_slice/retail_map_data.gd"
const TARGETS := [
	"AngmarOrcWarriors",
	"AngmarWolfRiders",
	"AngmarRhudaurSpearmen",
	"AngmarRhudaurSlingers",
]

var failed := 0
var checks := 0
var _content_db: Node
var _unit_runtimes: Dictionary
var _structure_runtimes: Dictionary
var _manifest: Dictionary


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_content_db = root.get_node_or_null("ContentDB")
	await process_frame
	await process_frame
	_check(_content_db != null, "ContentDB autoload is required")
	if _content_db == null:
		_finish()
		return
	_check((_content_db.pack_roots as Array).size() > 0, "isolated selection mounted no packs")
	_check((_content_db.pack_roots as Array).size() == 1, "isolated selection must mount exactly one cooked pack")
	var expected_identity := OS.get_environment("OPENBFME_THRALL_PACK_IDENTITY")
	_check(expected_identity.length() == 64, "wrapper did not provide cooked identity")
	_check(String((_content_db.pack_roots as Array)[0]).replace("\\", "/").ends_with("/" + expected_identity), "ContentDB mounted a stale/fallback pack")
	_unit_runtimes = _content_db.get_playable_unit_runtimes()
	_structure_runtimes = _content_db.get_playable_structure_runtimes()
	_check(_unit_runtimes.has("AngmarThrallMaster"), "cooked pack lacks Thrall runtime")
	_check(_unit_runtimes.has("AngmarPorter"), "cooked pack lacks fortress-declared Angmar builder")
	_check(_structure_runtimes.has("AngmarDen"), "cooked pack lacks Den runtime")
	_check(_structure_runtimes.has("AngmarBarracks"), "cooked pack lacks Thrall's authored producer")
	_check(_structure_runtimes.has("AngmarFortress"), "cooked pack lacks manifest-required Angmar fortress")
	if (
		not _unit_runtimes.has("AngmarThrallMaster")
		or not _unit_runtimes.has("AngmarPorter")
		or not _structure_runtimes.has("AngmarDen")
		or not _structure_runtimes.has("AngmarBarracks")
		or not _structure_runtimes.has("AngmarFortress")
	):
		_finish()
		return

	# Scope the shipping manifest builder to the exact descriptor graph mounted
	# above: the Thrall, the Fortress-declared Porter bootstrap, its authored
	# Barracks producer, and the live Den aura.
	# Replacement targets ride the compiler graph and are normalized by config.
	var manifest_unit_runtimes := {
		"AngmarThrallMaster": _unit_runtimes["AngmarThrallMaster"],
		"AngmarPorter": _unit_runtimes["AngmarPorter"],
	}
	_structure_runtimes = {
		"AngmarBarracks": _structure_runtimes["AngmarBarracks"],
		"AngmarDen": _structure_runtimes["AngmarDen"],
		"AngmarFortress": _structure_runtimes["AngmarFortress"],
	}
	_manifest = load(MANIFEST_PATH).from_registries("angmar", manifest_unit_runtimes, _structure_runtimes)
	var builder_rules: Dictionary = {}
	var slice = load(SLICE_PATH).new()
	var map_data = load(MAP_DATA_PATH).new()
	map_data.local_transform_scale = 0.1
	slice.source_map_data = map_data
	slice.fieldable_unit_runtimes = manifest_unit_runtimes
	for builder_value in _manifest.get("builder_unit_ids", []) as Array:
		var rule: Dictionary = slice._faction_builder_unit_rule(String(builder_value))
		if not rule.is_empty():
			builder_rules[String(builder_value)] = rule
	slice.free()
	_check(String(_manifest.get("_error", "")) == "", "real Angmar manifest failed: %s" % String(_manifest.get("_error", "")))
	if String(_manifest.get("_error", "")) != "":
		_finish()
		return
	# The Porter is manifest/bootstrap evidence; its established builder
	# projection above is the production unit rule. Only the Thrall descriptor
	# enters the ordinary playable-unit runtime compiler.
	_unit_runtimes = {
		"AngmarThrallMaster": manifest_unit_runtimes["AngmarThrallMaster"],
	}

	var surface_sim = _new_sim(builder_rules)
	if surface_sim == null:
		_finish()
		return
	_spawn_thrall(surface_sim)
	var base_surface: Array[Dictionary] = surface_sim.battalion_upgrade_commands(1)
	var fake_wolf := _surface_for_target(surface_sim, base_surface, "AngmarWolfRiders")
	_check(base_surface.size() == 4, "base palette is not the exact four normal branches")
	_check(String(fake_wolf.get("command_id", "")) == "Command_UpgradeThrallMasterWolfRiders_Fake", "base palette lacks authored fake Wolf button")
	_check(not bool(fake_wolf.get("research_owned", true)), "fake Wolf button is not disabled")
	_add_live_den(surface_sim)
	surface_sim._step_attribute_modifier_auras()
	surface_sim._step_monitor_condition_updates()
	var den_surface: Array[Dictionary] = surface_sim.battalion_upgrade_commands(1)
	var real_wolf := _surface_for_target(surface_sim, den_surface, "AngmarWolfRiders")
	_check(String((surface_sim.entities[1] as Dictionary).get("command_set_id", "")) == "AngmarThrallMasterCommandSet_DenPresent", "authored aura USER_1 monitor transition did not select Den palette")
	_check(String(real_wolf.get("command_id", "")) == "Command_UpgradeThrallMasterWolfRiders" and bool(real_wolf.get("research_owned", false)), "Den palette lacks enabled real Wolf button")

	for target_value in TARGETS:
		var target := String(target_value)
		var first := _exercise(target, builder_rules)
		var second := _exercise(target, builder_rules)
		_check(bool(first.get("ok", false)), "%s production replacement failed: %s" % [target, first])
		_check(first == second, "%s replacement was nondeterministic" % target)
	_finish()


func _new_sim(builder_rules: Dictionary):
	var sim = load(SIM_PATH).new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"enable_fog_of_war": false,
		"faction_manifest": _manifest,
		"playable_unit_runtimes": _unit_runtimes,
		"playable_structure_runtimes": _structure_runtimes,
		"producer_kind_by_source_object": _manifest.get("producer_kind_registry", {}),
		"unit_rules": builder_rules.duplicate(true),
		"starting_resources": 1000000,
		"source_map_transform_scale": 0.1,
	})
	if String(sim.configuration_error) != "":
		_check(false, "production config refused cooked graph: %s" % String(sim.configuration_error))
		return null
	sim.setup({}, sim._rules)
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	for structure_id in sim.structure_ids():
		sim.structures.erase(structure_id)
	sim.team_command_points[0] = 0
	sim.team_resources[0] = 1000000
	sim.ai_enabled = false
	sim.base_loop_enabled = true
	return sim


func _spawn_thrall(sim) -> void:
	var document := _unit_runtimes["AngmarThrallMaster"] as Dictionary
	var projection: Dictionary = sim.PlayableUnitAdapter.simulation_rule(document)
	var unit_type := String(projection.get("unit_type", ""))
	var object_id := String(projection.get("object_id", ""))
	var production := sim._unit_production_rules.get(unit_type, {}) as Dictionary
	var rule := (sim._rules.get("unit_rules", {}) as Dictionary).get(object_id, {}) as Dictionary
	var cp := int(production.get("default_command_points", -1))
	_check(not projection.is_empty() and not production.is_empty() and not rule.is_empty() and cp > 0, "Thrall is not descriptor-backed in production config")
	sim._add_battalion(1, 0, Vector2.ZERO, "AngmarThrallMaster", object_id, unit_type, cp, rule)
	sim.team_command_points[0] = cp


func _add_live_den(sim) -> void:
	# The row identity is sourced from the loaded Den descriptor; attaching it
	# invokes the shipping structure module registry, not a copied aura fixture.
	var document := _structure_runtimes["AngmarDen"] as Dictionary
	var registration := document.get("registration", {}) as Dictionary
	var gameplay := registration.get("gameplay", {}) as Dictionary
	var source_id := String(document.get("objectId", ""))
	_check(source_id == "AngmarDen" and not (gameplay.get("moduleContracts", []) as Array).is_empty(), "Den descriptor lacks authored module contracts")
	sim.structures[100] = {
		"id": 100,
		"team": 0,
		"kind": "structure",
		"structure_kind": "den",
		"source_object_id": source_id,
		"object_id": source_id,
		"position": Vector2.ZERO,
		"health": 1000,
		"maximum_health": 1000,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": ["Upgrade_ObjectLevel1"],
		"applied_upgrades": {},
	}
	sim._attach_structure_module_contracts(sim.structures[100])
	_check((sim.structures[100] as Dictionary).has("attribute_modifier_auras"), "live Den did not attach authored aura")


func _surface_for_target(sim, rows: Array[Dictionary], target: String) -> Dictionary:
	var graph := sim._rules.get("angmar_thrall_replacement", {}) as Dictionary
	var upgrade_id := ""
	for branch_value in graph.get("branches", []) as Array:
		var branch := branch_value as Dictionary
		if String(branch.get("targetHordeId", "")) == target:
			upgrade_id = String(branch.get("upgradeId", ""))
	for row in rows:
		if String(row.get("upgrade_id", "")) == upgrade_id:
			return row
	return {}


func _exercise(target: String, builder_rules: Dictionary) -> Dictionary:
	var sim = _new_sim(builder_rules)
	if sim == null:
		return {"ok": false, "reason": "config"}
	_spawn_thrall(sim)
	if target == "AngmarWolfRiders":
		_add_live_den(sim)
		sim._step_attribute_modifier_auras()
		sim._step_monitor_condition_updates()
	var surface: Array[Dictionary] = sim.battalion_upgrade_commands(1)
	var command := _surface_for_target(sim, surface, target)
	if command.is_empty():
		return {"ok": false, "reason": "surface"}
	var queued: Dictionary = sim.queue_battalion_upgrade(0, 1, String(command.get("upgrade_id", "")))
	if not bool(queued.get("ok", false)):
		return {"ok": false, "reason": queued.get("reason", "queue")}
	var queue_ticks := int(command.get("duration_ticks", 0))
	for tick in range(1, queue_ticks + 1):
		sim.tick_index = tick
		sim._step_battalion_upgrades()
	if not sim.entities.has(1) or not (sim.entities[1] as Dictionary).has("thrall_replacement_pending"):
		return {"ok": false, "reason": "queue-did-not-start-replacement"}
	for offset in range(1, 10):
		sim.tick_index = queue_ticks + offset
		sim._step_battalion_upgrades()
		if String((sim.entities[1] as Dictionary).get("unit_type", "")) != "bfme2.object.angmar-thrall-master":
			return {"ok": false, "reason": "early-unpack"}
	sim.tick_index = queue_ticks + 10
	sim._step_battalion_upgrades()
	if String(((sim.entities[1] as Dictionary).get("thrall_replacement_pending", {}) as Dictionary).get("phase", "")) != "preparing":
		return {"ok": false, "reason": "missing-preparation-phase"}
	for offset in range(11, 20):
		sim.tick_index = queue_ticks + offset
		sim._step_battalion_upgrades()
		if String((sim.entities[1] as Dictionary).get("unit_type", "")) != "bfme2.object.angmar-thrall-master":
			return {"ok": false, "reason": "early-preparation"}
	sim.tick_index = queue_ticks + 20
	sim._step_battalion_upgrades()
	if not sim.entities.has(1) or sim.entities.size() != 1:
		return {"ok": false, "reason": "replacement-count"}
	var replacement := sim.entities[1] as Dictionary
	var graph := sim._rules.get("angmar_thrall_replacement", {}) as Dictionary
	var branch: Dictionary = {}
	for branch_value in graph.get("branches", []) as Array:
		if String((branch_value as Dictionary).get("targetHordeId", "")) == target:
			branch = branch_value as Dictionary
	var expected_cp := int(branch.get("commandPoints", -1))
	var receipts: Array = replacement.get("unsupported_semantics", []) as Array
	return {
		"ok": String(replacement.get("unit_type", "")) == String(branch.get("runtimeTargetUnitType", ""))
			and String(replacement.get("object_id", "")) == String(branch.get("runtimeTargetObjectId", ""))
			and int(replacement.get("command_points", -1)) == expected_cp
			and int(sim.team_command_points.get(0, -1)) == expected_cp
			and receipts == ["l5-replacement-transfer-unproved", "fx:FX_ThrallSummon", "audio:BoromirHorn"],
		"target": String(replacement.get("unit_type", "")),
		"object": String(replacement.get("object_id", "")),
		"cp": int(sim.team_command_points.get(0, -1)),
		"complete_tick": sim.tick_index,
		"state_hash": sim.state_hash(),
	}


func _check(condition: bool, message: String) -> void:
	checks += 1
	if condition:
		return
	failed += 1
	print("THRALL_CHECK_FAIL %s" % message)


func _finish() -> void:
	if failed == 0 and checks >= 19:
		print("ANGMAR_THRALL_REPLACEMENT PASS branches=4 failed=0")
		quit(0)
	else:
		print("ANGMAR_THRALL_REPLACEMENT FAIL branches=4 failed=%d checks=%d" % [failed, checks])
		quit(1)
