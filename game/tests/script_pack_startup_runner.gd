extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0
var _had_content_environment := false
var _prior_content_environment := ""


func _initialize() -> void:
	call_deferred("_run")


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("SCRIPT_PACK_STARTUP FAIL %s %s" % [label, detail])


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 40.0,
		"vision_range_source": 400.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_health": 200,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": is_builder,
	}


func _complete_structure_armor() -> Dictionary:
	var result := {}
	for kind in SimScript.STRUCTURE_KINDS:
		result[kind] = {"default": 1.0}
	return result


func _configured_simulation():
	var sim := SimScript.new()
	OS.set_environment("OPENBFME_STARTER_ARMY", "0")
	sim._rules = {
		"enable_base_loop": false,
		"spawn_initial_battalions": false,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		"faction_manifest": {
			"structure_kinds": SimScript.STRUCTURE_KINDS.duplicate(),
			"seed_structure_kinds": SimScript.STRUCTURE_KINDS.duplicate(),
			"structure_armor": _complete_structure_armor(),
		},
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(
				SimScript.SOLDIER_HORDE_ID, false
			),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(
				SimScript.ARCHER_OBJECT_ID, false
			),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(
				SimScript.TOWER_GUARD_OBJECT_ID, false
			),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(
				SimScript.KNIGHT_OBJECT_ID, false
			),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(
				SimScript.BUILDER_OBJECT_ID, true
			),
		},
	}
	sim.configure_team_roster([
		{
			"team": SimScript.PLAYER_TEAM,
			"faction": "men",
			"is_ai": false,
			"start_index": 0,
		},
		{
			"team": SimScript.ENEMY_TEAM,
			"faction": "men",
			"is_ai": true,
			"start_index": 1,
		},
	])
	sim.setup({}, {})
	return sim


func _cleanup_runtime(slice, sim) -> void:
	for runtime_value in slice.script_runtimes:
		var runtime := runtime_value as Dictionary
		var runtime_world = runtime.get("world")
		if runtime_world == null:
			continue
		for facet in runtime_world._facets.values():
			facet.world = null
		runtime_world._facets.clear()
	for team in sim.registered_script_executor_teams():
		sim.unregister_script_executor(int(team))
	slice.script_runtimes = []
	slice.free()


func _run() -> void:
	var pack_root := OS.get_environment("OPENBFME_SCRIPT_PACK").replace("\\", "/")
	var expected_map_id := OS.get_environment("OPENBFME_SCRIPT_MAP_ID")
	_check("scratch pack environment is explicit", pack_root != "")
	_check("expected map id is explicit", expected_map_id != "")
	if pack_root == "" or expected_map_id == "":
		_finish()
		return

	var mod_loader = root.get_node_or_null("ModLoader")
	var content_db = root.get_node_or_null("ContentDB")
	_check("normal content autoloads are available", mod_loader != null and content_db != null)
	if mod_loader == null or content_db == null:
		_finish()
		return

	_had_content_environment = OS.has_environment("OPENBFME_CONTENT")
	_prior_content_environment = OS.get_environment("OPENBFME_CONTENT")
	OS.set_environment("OPENBFME_CONTENT", pack_root)
	content_db.reload()

	_check(
		"scratch pack mounts through ModLoader",
		content_db.pack_roots.has(pack_root),
		str(content_db.pack_roots)
	)
	var map_definition: Dictionary = content_db.get_bundle_map(expected_map_id)
	_check(
		"map is discovered through ContentDB",
		not map_definition.is_empty(),
		expected_map_id
	)
	if map_definition.is_empty():
		_finish()
		return
	var source_path := String(map_definition.get("_source", ""))
	_check(
		"map source belongs to mounted scratch pack",
		source_path != "" and mod_loader.path_is_within(pack_root, source_path),
		source_path
	)
	var map_root := source_path.get_base_dir()
	var scripts_path := String(
		mod_loader.resolve_pack_path(map_root, "scripts.json")
	)
	_check(
		"scripts.json resolves from discovered map root",
		scripts_path != "" and FileAccess.file_exists(scripts_path),
		scripts_path
	)
	var document: Variant = mod_loader._read_json(scripts_path)
	_check(
		"discovered scripts document is schema v2",
		typeof(document) == TYPE_DICTIONARY
		and String((document as Dictionary).get("schema", "")) == "openbfme.map-scripts"
		and int((document as Dictionary).get("schemaVersion", -1)) == 2
	)
	if typeof(document) == TYPE_DICTIONARY:
		var composite := document as Dictionary
		var source := composite.get("source", {}) as Dictionary
		var map_source := source.get("map", {}) as Dictionary
		var libraries := source.get("libraries", []) as Array
		var templates := composite.get("libraryTemplates", []) as Array
		_check(
			"exact two-library closure is retained",
			libraries.size() == 2
			and templates.size() == 2
			and String(map_source.get("virtualPath", ""))
				== "maps/map mp fords of isen ii/map mp fords of isen ii.map"
			and String((libraries[0] as Dictionary).get("virtualPath", ""))
				== "libraries/ai_initialize/ai_initialize.map"
			and String((libraries[1] as Dictionary).get("virtualPath", ""))
				== "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map"
		)
		_check(
			"artifact order matches template order",
			libraries.size() == 2
			and templates.size() == 2
			and String((libraries[0] as Dictionary).get("sourceSha256", ""))
				== String((templates[0] as Dictionary).get("identity", ""))
			and String((libraries[1] as Dictionary).get("sourceSha256", ""))
				== String((templates[1] as Dictionary).get("identity", ""))
		)
	if expected_map_id.begins_with("bfme2."):
		var sim = _configured_simulation()
		var slice_script: GDScript = load(
			"res://src/retail_slice/retail_vertical_slice.gd"
		)
		_check("vertical slice script loads", slice_script != null)
		if slice_script != null:
			var slice = slice_script.new()
			slice.simulation = sim
			var map_data_script: GDScript = (
				slice_script.get_script_constant_map().get("MapDataScript")
				as GDScript
			)
			_check("vertical slice map-data bridge loads", map_data_script != null)
			if map_data_script == null:
				_cleanup_runtime(slice, sim)
				_finish()
				return
			var map_data = map_data_script.new()
			var active_source := map_definition.get("source", {}) as Dictionary
			map_data.pack_root = pack_root
			map_data.map_root = map_root
			map_data.map_id = expected_map_id
			map_data.source_virtual_path = String(
				active_source.get("virtualPath", "")
			)
			map_data.source_sha256 = String(active_source.get("sha256", ""))
			map_data.source_bytes = int(active_source.get("sourceBytes", 0))
			slice.source_map_data = map_data
			slice._install_map_scripts()
			_check(
				"normal map startup registers Player_2 executor",
				sim.registered_script_executor_teams()
					== [SimScript.ENEMY_TEAM]
				and slice.script_runtimes.size() == 1,
				str(sim.registered_script_executor_teams())
			)
			var before := sim.script_team_owner("Player_2_Inherit")
			_check(
				"inheritance team begins civilian-controlled",
				bool(before.get("ok", false))
				and int(before.get("owner", -1)) == SimScript.NEUTRAL_TEAM,
				str(before)
			)
			for unused in range(64):
				sim.tick()
				var current: Dictionary = sim.script_team_owner(
					"Player_2_Inherit"
				)
				if (
					bool(current.get("ok", false))
					and int(current.get("owner", -1))
						== SimScript.ENEMY_TEAM
				):
					break
			var after := sim.script_team_owner("Player_2_Inherit")
			_check(
				"startup executes decoded TEAM_TRANSFER_TO_PLAYER",
				bool(after.get("ok", false))
				and int(after.get("owner", -1)) == SimScript.ENEMY_TEAM,
				str(after)
			)
			_cleanup_runtime(slice, sim)
	_finish()


func _finish() -> void:
	if _had_content_environment:
		OS.set_environment("OPENBFME_CONTENT", _prior_content_environment)
	else:
		OS.unset_environment("OPENBFME_CONTENT")
	print(
		"SCRIPT_PACK_STARTUP_RESULT passed=%d failed=%d"
		% [passed, failed]
	)
	quit(0 if failed == 0 else 1)
