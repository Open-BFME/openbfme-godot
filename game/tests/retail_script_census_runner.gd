extends SceneTree

## Execution census: what the retail AI actually DOES when its decoded script
## libraries run through the PRODUCTION seam (sim.tick() stepping a wired
## executor), and - the part that becomes the next work queue - what refuses.
##
## Loads every source of the Part 1 skirmish script contract (8 skirmish
## maps + 5 libraries, 770 scripts) into one wired executor over the
## synthetic harness sim, runs CENSUS_TICKS sim ticks, and reports:
##
##   * the load census (implemented / unimplemented / blocked / deliberate /
##     unknown, by opcode occurrence),
##   * runtime totals (scripts fired, conditions evaluated, action outcomes
##     by dispatch status),
##   * the top gap-log entries by count - each one a (kind, name, reason)
##     with the script and tick that first hit it.
##
## DIAGNOSTIC runner: it is expected green whenever the contract JSON is
## present and SKIPs (exit 0, stated) when it is not - the contract lives in
## .private/retail-work and is not part of the repository. Override the
## location with OPENBFME_SCRIPT_CONTRACT.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/retail_script_census_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")
const ExecutorScript = preload("res://src/script/script_executor.gd")
const ManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

const CONTRACT_RELATIVE_PATH := ".private/retail-work/reports/skirmish-script-contract/skirmish_script_contract.json"
const CENSUS_TICKS := 600
const TOP_GAPS := 30

const PLAYER := "CensusPlayer"
const ENEMY := "CensusEnemy"


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var path := _contract_path()
	if not FileAccess.file_exists(path):
		print("RETAIL_SCRIPT_CENSUS SKIP contract absent at %s" % path)
		quit(0)
		return
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if typeof(parsed) != TYPE_DICTIONARY:
		printerr("RETAIL_SCRIPT_CENSUS FAIL contract JSON did not parse")
		quit(1)
		return

	var sim = _make_sim()
	var world = WorldScript.new(sim)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_player(ENEMY, SimScript.ENEMY_TEAM)
	world.bind_script_player(PLAYER)
	var executor = ExecutorScript.new(world)

	var loaded := 0
	var sources := 0
	for source_value in (parsed as Dictionary).get("sources", []) as Array:
		var source := source_value as Dictionary
		sources += 1
		for row_value in source.get("scripts", []) as Array:
			var payload: Dictionary = (row_value as Dictionary).get("payload", {})
			if not payload.is_empty():
				executor.load_script_payload(payload)
				loaded += 1
	if loaded == 0:
		printerr("RETAIL_SCRIPT_CENSUS FAIL no scripts loaded")
		quit(1)
		return
	if not sim.attach_script_env(executor.env, SimScript.PLAYER_TEAM) \
			or not sim.register_script_executor(executor, SimScript.PLAYER_TEAM):
		printerr("RETAIL_SCRIPT_CENSUS FAIL wiring refused")
		quit(1)
		return

	for _tick in CENSUS_TICKS:
		sim.tick()

	var report: Dictionary = executor.load_report()
	print("RETAIL_SCRIPT_CENSUS sources=%d scripts=%d subroutines=%d ticks=%d" % [
		sources, loaded, int(report["subroutines"]), CENSUS_TICKS,
	])
	var census: Dictionary = report["census"]
	for bucket in ["implemented", "unimplemented", "blocked", "deliberate", "unknown"]:
		var histogram: Dictionary = census[bucket]
		var occurrences := 0
		for count in histogram.values():
			occurrences += int(count)
		print("CENSUS %s opcodes=%d occurrences=%d" % [bucket, histogram.size(), occurrences])
	print("RUNTIME scripts_fired=%d conditions_evaluated=%d subroutine_calls=%d wiring_faults=%d" % [
		executor.scripts_fired,
		executor.conditions_evaluated,
		executor.subroutine_calls_served,
		sim.script_wiring_faults,
	])
	var outcome_keys: Array = executor.action_outcomes.keys()
	outcome_keys.sort()
	for key in outcome_keys:
		print("ACTIONS %s=%d" % [key, int(executor.action_outcomes[key])])
	# The load-bearing pair this census exists to watch: the heaviest members
	# in the retail AI sit behind the SKIRMISH_PLAYER_FACTION spell-system
	# gates, so their execution counts state directly whether those gates
	# opened (0 = the whole spell lane sat behind a closed gate).
	print("EXECUTED condition SKIRMISH_PLAYER_FACTION=%d" % int(executor.condition_executions.get("SKIRMISH_PLAYER_FACTION", 0)))
	print("EXECUTED condition PLAYER_CAN_PURCHASE_SCIENCE=%d" % int(executor.condition_executions.get("PLAYER_CAN_PURCHASE_SCIENCE", 0)))
	print("EXECUTED action PLAYER_PURCHASE_SCIENCE=%d" % int(executor.action_executions.get("PLAYER_PURCHASE_SCIENCE", 0)))
	var by_reason: Dictionary = executor.dispatch.gaps.by_reason()
	var reason_keys: Array = by_reason.keys()
	reason_keys.sort()
	for reason in reason_keys:
		print("GAPS reason=%s count=%d" % [reason, int(by_reason[reason])])

	# Top gap entries by count (count desc, then key asc for a stable order).
	var rows: Array = []
	for row_value in executor.dispatch.gaps.entries.values():
		rows.append(row_value)
	rows.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if int(a["count"]) != int(b["count"]):
			return int(a["count"]) > int(b["count"])
		var a_key := "%s|%s|%s" % [a["kind"], a["name"], a["reason"]]
		var b_key := "%s|%s|%s" % [b["kind"], b["name"], b["reason"]]
		return a_key < b_key
	)
	for index in mini(TOP_GAPS, rows.size()):
		var row: Dictionary = rows[index]
		print("TOP_GAP %2d. %s %s reason=%s count=%d first_script='%s' detail='%s'" % [
			index + 1, row["kind"], row["name"], row["reason"], int(row["count"]),
			row["first_script"], row["detail"],
		])
	quit(0)


func _contract_path() -> String:
	var override := OS.get_environment("OPENBFME_SCRIPT_CONTRACT").strip_edges()
	if override != "":
		return override
	var game_root := ProjectSettings.globalize_path("res://")
	return game_root.rstrip("/").get_base_dir().path_join(CONTRACT_RELATIVE_PATH)


func _make_sim():
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	# Factions ON the roster, PRODUCTION-SHAPED: the vertical slice's
	# _menu_sim_team_roster stamps lowercase PACK FACTION IDS ("men",
	# "isengard") on descriptors, and the sim translates them to retail side
	# tokens through the retail_faction_sides rules table (the same table the
	# slice injects). The census previously hand-fed the capitalised side
	# tokens directly, which made SKIRMISH_PLAYER_FACTION pass HERE while
	# every live match answered false - the census must never diverge from
	# the production descriptor shape again.
	sim.configure_team_roster([
		{"team": SimScript.PLAYER_TEAM, "faction": "men", "is_ai": false},
		{"team": SimScript.ENEMY_TEAM, "faction": "isengard", "is_ai": true},
	])
	sim.setup({}, {})
	sim.ai_enabled = true
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()
	sim.force_ai_construction_complete()
	return sim


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		# The pack-faction -> retail-side table the vertical slice injects into
		# every real match's rules; the census loads the same versioned file so
		# its faction gates exercise the production translation path.
		"retail_faction_sides": ManifestScript.retail_faction_sides(),
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


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
