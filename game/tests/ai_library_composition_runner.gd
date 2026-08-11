extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

## Every `_check` label this runner is expected to reach. A GDScript runtime
## error unwinds `_run()` straight to the engine, so `_finish()` never runs and
## the headless process idles forever looking like work in progress. The count
## turns any short run into a loud red, and the watchdog turns a wedge into a
## non-zero exit.
const EXPECTED_CHECKS := 26

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "AI_LIBRARY_COMPOSITE", 0, 0, true)
	_runner_watchdog.set_result_provider(
		func() -> Vector2i: return Vector2i(passed, failed)
	)
	call_deferred("_run")


func _check(label: String, condition: bool, detail: String = "") -> void:
	_runner_watchdog.note(label)
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("AI_LIBRARY_COMPOSITE FAIL %s %s" % [label, detail])


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


func _run() -> void:
	var fixture_path := OS.get_environment("OPENBFME_AI_COMPOSITE_FIXTURE")
	var value: Variant = JSON.parse_string(
		FileAccess.get_file_as_string(fixture_path)
	)
	_check("private fixture parses", typeof(value) == TYPE_DICTIONARY)
	if typeof(value) != TYPE_DICTIONARY:
		_finish()
		return

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
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
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
	var slice_script: GDScript = load(
		"res://src/retail_slice/retail_vertical_slice.gd"
	)
	_check("vertical slice script loads", slice_script != null)
	if slice_script == null:
		_finish()
		return
	var slice = slice_script.new()
	slice.simulation = sim
	var map_data_script: GDScript = (
		slice_script.get_script_constant_map().get("MapDataScript") as GDScript
	)
	if map_data_script == null:
		printerr("AI_LIBRARY_COMPOSITE FAIL vertical slice map-data bridge loads")
		failed += 1
		slice.free()
		sim.free()
		_finish()
		return
	var map_data = map_data_script.new()
	# A cooked composite for a map that declares Lib_GollumSpawn carries three
	# templates; every other map carries two. Drive the per-player closure
	# checks from the two-template baseline either way, and gate the bound
	# class against the real third template when the fixture ships one.
	var fixture_doc := (value as Dictionary).duplicate(true)
	var fixture_templates := fixture_doc["libraryTemplates"] as Array
	var fixture_has_bound_library := fixture_templates.size() == 3
	var base_value := fixture_doc.duplicate(true)
	if fixture_has_bound_library:
		(base_value["libraryTemplates"] as Array).remove_at(2)
		((base_value["source"] as Dictionary)["libraries"] as Array).remove_at(2)
	var composite_source := base_value.get("source", {}) as Dictionary
	var composite_map_source := composite_source.get("map", {}) as Dictionary
	var source_game := String(composite_source.get("game", ""))
	map_data.map_id = source_game + ".map.fords-of-isen-ii"
	map_data.source_virtual_path = String(composite_map_source.get("virtualPath", ""))
	map_data.source_sha256 = String(composite_map_source.get("sourceSha256", ""))
	map_data.source_bytes = int(composite_map_source.get("sourceBytes", 0))
	slice.source_map_data = map_data
	var invalid_documents: Array[Dictionary] = []
	var missing_source := base_value.duplicate(true)
	missing_source.erase("source")
	invalid_documents.append({"label": "missing source", "document": missing_source})
	var mismatched_map_path := base_value.duplicate(true)
	(
		(mismatched_map_path["source"] as Dictionary)["map"] as Dictionary
	)["virtualPath"] = "maps/map mp other map/map mp other map.map"
	invalid_documents.append({
		"label": "mismatched map virtual path",
		"document": mismatched_map_path,
	})
	var mismatched_library_path := base_value.duplicate(true)
	(
		(
			(mismatched_library_path["source"] as Dictionary)["libraries"]
			as Array
		)[0] as Dictionary
	)["virtualPath"] = "libraries/other/other.map"
	invalid_documents.append({
		"label": "mismatched source library virtual path",
		"document": mismatched_library_path,
	})
	var reordered_sources := base_value.duplicate(true)
	(
		(reordered_sources["source"] as Dictionary)["libraries"] as Array
	).reverse()
	invalid_documents.append({
		"label": "reordered source library virtual paths",
		"document": reordered_sources,
	})
	var mismatched_source := base_value.duplicate(true)
	(
		((mismatched_source["source"] as Dictionary)["libraries"] as Array)[0]
		as Dictionary
	)["sourceSha256"] = "0".repeat(64)
	invalid_documents.append({
		"label": "source/template identity mismatch",
		"document": mismatched_source,
	})
	var wrong_game := base_value.duplicate(true)
	(wrong_game["source"] as Dictionary)["game"] = (
		"rotwk" if source_game == "bfme2" else "bfme2"
	)
	invalid_documents.append({
		"label": "wrong source game",
		"document": wrong_game,
	})
	var wrong_map_sha := base_value.duplicate(true)
	((wrong_map_sha["source"] as Dictionary)["map"] as Dictionary)["sourceSha256"] = (
		"0".repeat(64)
	)
	invalid_documents.append({
		"label": "wrong active map hash",
		"document": wrong_map_sha,
	})
	var wrong_map_bytes := base_value.duplicate(true)
	var wrong_map_bytes_row := (
		(wrong_map_bytes["source"] as Dictionary)["map"] as Dictionary
	)
	wrong_map_bytes_row["sourceBytes"] = int(wrong_map_bytes_row["sourceBytes"]) + 1
	invalid_documents.append({
		"label": "wrong active map byte count",
		"document": wrong_map_bytes,
	})
	var missing_template := base_value.duplicate(true)
	(missing_template["libraryTemplates"] as Array).remove_at(1)
	invalid_documents.append({
		"label": "missing library template",
		"document": missing_template,
	})
	var reordered_templates := base_value.duplicate(true)
	(reordered_templates["libraryTemplates"] as Array).reverse()
	invalid_documents.append({
		"label": "reordered library templates",
		"document": reordered_templates,
	})
	# Lib_GollumSpawn is the map-player library class: it authors PlyrCreeps,
	# not the per-player "Player" placeholder, and retail binds it once to the
	# map's own creeps player. Synthesize that third template from the real
	# fixture so the runtime contract is gated without shipping a second pack.
	var bound_library_path := "libraries/lib_gollumspawn/lib_gollumspawn.map"
	var bound_identity := "5".repeat(64)
	var bound_template := {
		"identity": bound_identity,
		"instantiateFor": "mapPlayer",
		"playerPlaceholder": "PlyrCreeps",
		"world": {
			"available": true,
			"players": [
				{"index": 0, "name": ""},
				{"index": 1, "name": "PlyrCreeps"},
			],
			"teams": [
				{
					"index": 1,
					"name": "teamPlyrCreeps",
					"owner": "PlyrCreeps",
					"objectCount": 0,
					"namedMembers": [],
				},
			],
			"objects": [],
			"namedObjects": [],
		},
		"scripts": (
			(base_value["libraryTemplates"] as Array)[0] as Dictionary
		)["scripts"],
	}
	var bound_source_row := {
		"virtualPath": bound_library_path,
		"sourceBytes": 1024,
		"sourceSha256": bound_identity,
	}
	var bound_document := fixture_doc.duplicate(true)
	if not fixture_has_bound_library:
		(bound_document["libraryTemplates"] as Array).append(
			bound_template.duplicate(true)
		)
		((bound_document["source"] as Dictionary)["libraries"] as Array).append(
			bound_source_row.duplicate(true)
		)

	var wrong_class := bound_document.duplicate(true)
	(
		(wrong_class["libraryTemplates"] as Array)[2] as Dictionary
	)["instantiateFor"] = "aiPlayers"
	invalid_documents.append({
		"label": "bound library claiming the per-player class",
		"document": wrong_class,
	})
	var hijacked := bound_document.duplicate(true)
	(
		(hijacked["libraryTemplates"] as Array)[2] as Dictionary
	)["playerPlaceholder"] = "Player_1"
	invalid_documents.append({
		"label": "bound library retargeted at a concrete skirmish player",
		"document": hijacked,
	})
	var unbindable := bound_document.duplicate(true)
	var unbindable_template := (
		(unbindable["libraryTemplates"] as Array)[2] as Dictionary
	)
	unbindable_template["playerPlaceholder"] = "PlyrNotOnThisMap"
	invalid_documents.append({
		"label": "bound library naming a player the map lacks",
		"document": unbindable,
	})
	var wrong_bound_path := bound_document.duplicate(true)
	(
		((wrong_bound_path["source"] as Dictionary)["libraries"] as Array)[2]
		as Dictionary
	)["virtualPath"] = "libraries/other/other.map"
	invalid_documents.append({
		"label": "bound library with an unaudited source path",
		"document": wrong_bound_path,
	})
	var fourth := bound_document.duplicate(true)
	(fourth["libraryTemplates"] as Array).append(bound_template.duplicate(true))
	((fourth["source"] as Dictionary)["libraries"] as Array).append(
		bound_source_row.duplicate(true)
	)
	invalid_documents.append({
		"label": "a fourth library template",
		"document": fourth,
	})
	for invalid_case in invalid_documents:
		var invalid_document := invalid_case["document"] as Dictionary
		var invalid_normalized: Dictionary = (
			slice._normalized_map_scripts_document(invalid_document)
		)
		var refused_install: bool = not slice._install_map_scripts_document(
			invalid_document, "<negative>", false
		)
		_check(
			"atomically refuses %s" % String(invalid_case["label"]),
			not bool(invalid_normalized.get("ok", false))
			and refused_install
			and slice.script_runtimes.is_empty()
			and sim.registered_script_executor_teams().is_empty(),
			String(invalid_normalized.get("reason", "")),
		)
	# Runtime proves structural self-consistency, not retail-byte authenticity.
	# The normal startup path consumes an importer-audited pack whose receipt is
	# the trust boundary for those source bytes. Therefore a coupled mutation
	# remains structurally valid here while a one-sided mutation above refuses.
	var coupled_library_identity := base_value.duplicate(true)
	(
		(
			(coupled_library_identity["source"] as Dictionary)["libraries"]
			as Array
		)[0] as Dictionary
	)["sourceSha256"] = "4".repeat(64)
	(
		(coupled_library_identity["libraryTemplates"] as Array)[0] as Dictionary
	)["identity"] = "4".repeat(64)
	var coupled_normalized: Dictionary = slice._normalized_map_scripts_document(
		coupled_library_identity
	)
	_check(
		"coupled library identity remains structurally self-consistent",
		bool(coupled_normalized.get("ok", false)),
		String(coupled_normalized.get("reason", "")),
	)
	var bound_normalized: Dictionary = slice._normalized_map_scripts_document(
		bound_document
	)
	var bound_players: Array[String] = []
	var bound_payload_count := 0
	for source_value in bound_normalized.get("sources", []) as Array:
		var source_row := source_value as Dictionary
		bound_players.append(String(source_row.get("player", "")))
		if String(source_row.get("player", "")) == "PlyrCreeps":
			bound_payload_count = (source_row.get("scripts", []) as Array).size()
	bound_players.sort()
	_check(
		"bound library instantiates once, under the map's own creeps player",
		bool(bound_normalized.get("ok", false))
		and bound_players == ["Player_2", "PlyrCreeps"]
		and bound_payload_count
		== (
			((bound_document["libraryTemplates"] as Array)[2] as Dictionary)[
				"scripts"
			]
			as Array
		).size(),
		"%s %s" % [String(bound_normalized.get("reason", "")), str(bound_players)],
	)
	var normalized: Dictionary = slice._normalized_map_scripts_document(
		base_value
	)
	_check(
		"real composite normalizes",
		bool(normalized.get("ok", false)),
		String(normalized.get("reason", ""))
	)
	_check(
		"only concrete AI player receives libraries",
		(normalized.get("sources", []) as Array).size() == 1
		and String(((normalized["sources"] as Array)[0] as Dictionary).get("player", ""))
		== "Player_2",
		str(normalized.get("sources", []))
	)
	var real_marker_attested := false
	for row_value in normalized.get("teams", []) as Array:
		var row := row_value as Dictionary
		if (
			String(row.get("name", "")) == "Player_2_Inherit"
			and bool(row.get("marker_only", false))
			and int(row.get("object_count", -1)) == 6
		):
			real_marker_attested = true
			break
	_check(
		"real inheritance team carries source marker attestation",
		real_marker_attested
	)
	_check(
		"real composite installs",
		slice._install_map_scripts_document(
			base_value, fixture_path, false
		)
	)
	_check(
		"real library executor binds to Player_2",
		sim.registered_script_executor_teams() == [SimScript.ENEMY_TEAM]
		and slice.script_runtimes.size() == 1
	)
	var before := sim.script_team_owner("Player_2_Inherit")
	_check(
		"real inheritance team begins civilian-controlled",
		bool(before.get("ok", false))
		and int(before.get("owner", -1)) == SimScript.NEUTRAL_TEAM,
		str(before)
	)
	for unused in range(64):
		sim.tick()
		var current: Dictionary = sim.script_team_owner("Player_2_Inherit")
		if (
			bool(current.get("ok", false))
			and int(current.get("owner", -1)) == SimScript.ENEMY_TEAM
		):
			break
	var after := sim.script_team_owner("Player_2_Inherit")
	_check(
		"real decoded TEAM_TRANSFER_TO_PLAYER executes",
		bool(after.get("ok", false))
		and int(after.get("owner", -1)) == SimScript.ENEMY_TEAM,
		str(after)
	)
	# Teardown must survive a failed install: indexing an empty script_runtimes
	# raises, and that error unwinds `_run()` past `_finish()` — the hang this
	# runner used to produce instead of a red.
	if slice.script_runtimes.is_empty():
		_check(
			"teardown finds the installed script runtime",
			false,
			"script_runtimes is empty - an earlier install refused",
		)
	else:
		var runtime_world = (slice.script_runtimes[0] as Dictionary)["world"]
		for facet in runtime_world._facets.values():
			facet.world = null
		runtime_world._facets.clear()
	sim.unregister_script_executor(SimScript.ENEMY_TEAM)
	slice.script_runtimes = []
	slice.free()
	_finish()


func _finish() -> void:
	var ran := passed + failed
	var short_run := ran < EXPECTED_CHECKS
	if short_run:
		printerr(
			"AI_LIBRARY_COMPOSITE FAIL liveness: %d checks ran, %d expected — a section returned early"
			% [ran, EXPECTED_CHECKS]
		)
	print(
		"AI_LIBRARY_COMPOSITE_RESULT passed=%d failed=%d"
		% [passed, failed + (1 if short_run else 0)]
	)
	_runner_watchdog.stop()
	quit(0 if failed == 0 and not short_run else 1)
