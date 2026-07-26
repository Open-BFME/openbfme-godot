extends SceneTree

## Deterministic proof runner for RetailMapScripts, the tier-1 skirmish
## map-script interpreter. Synthetic fixtures use the exact decoded JSON
## shape produced by the skirmish contract extractor; the final check loads
## one real decoded skirmish script source from the Part 1 contract JSON
## (skip-if-absent, since .private is not present on every checkout).
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/retail_map_script_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const MapScriptsScript = preload("res://src/retail_slice/retail_map_scripts.gd")

const CONTRACT_RELATIVE_PATH := ".private/retail-work/reports/skirmish-script-contract/skirmish_script_contract.json"
const COUNTER_MONEY := 987654
const TIMER_MONEY := 765432
## SET_MILLISECOND_TIMER carries SECONDS despite its name - see
## SageScriptCoreHandlers.MSEC_FAMILY_UNIT_IS_SECONDS for the measurement this
## was settled by. 3.0 s at the interpreter's 0.1 s tick is 30 ticks.
##
## This is a SYNTHETIC value chosen by this test, not a duration lifted from
## retail content. That matters: the .scb script libraries and the .map-embedded
## ScriptLists are different revisions of the same scripts and disagree on
## several durations, so an assertion pinned to a corpus duration would look
## verified while depending on which revision happened to load.
const TIMER_SECONDS := 3.0

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _make_sim():
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	return sim


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 1000,
		"ai_attack_delay_ticks": 4000,
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


# --- Fixture builders emitting the decoded contract JSON shape -------------


func _argument(argument_type: int, integer: int = 0, real: float = 0.0, text: String = "") -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _condition(opcode: String, arguments: Array, inverted: bool = false) -> Dictionary:
	return {
		"name": "Condition",
		"version": 6,
		"value": {
			"contentType": 0,
			"internalName": {"name": opcode, "wireTypeCode": 3},
			"arguments": arguments,
			"enabled": true,
			"inverted": inverted,
		},
	}


func _or_condition(conditions: Array) -> Dictionary:
	return {"name": "OrCondition", "version": 1, "value": {"records": conditions}}


func _action(opcode: String, arguments: Array, record_name: String = "ScriptAction") -> Dictionary:
	return {
		"name": record_name,
		"version": 3,
		"value": {
			"contentType": 0,
			"internalName": {"name": opcode, "wireTypeCode": 3},
			"arguments": arguments,
			"enabled": true,
		},
	}


func _script(name: String, records: Array, deactivate_upon_success: bool) -> Dictionary:
	return {
		"name": name,
		"comment": "",
		"conditionsComment": "",
		"actionsComment": "",
		"isActive": true,
		"deactivateUponSuccess": deactivate_upon_success,
		"activeInEasy": true,
		"activeInMedium": true,
		"activeInHard": true,
		"isSubroutine": false,
		"evaluationInterval": 0,
		"actionsFireSequentially": false,
		"loopActions": false,
		"loopCount": 0,
		"sequentialTargetType": 1,
		"sequentialTargetName": "",
		"scope": "ALL",
		"records": records,
	}


func _gameplay_fixture_payloads() -> Array:
	# Counter lane: c increments every tick; the gate fires exactly at c == 3.
	var increment := _script("Increment Counter", [
		_or_condition([_condition("CONDITION_TRUE", [])]),
		_action("INCREMENT_COUNTER", [
			_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
			_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "c"),
		]),
	], false)
	var counter_gate := _script("Counter Gate", [
		_or_condition([_condition("COUNTER", [
			_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "c"),
			_argument(MapScriptsScript.ARGUMENT_COMPARISON, MapScriptsScript.COMPARE_EQUAL),
			_argument(MapScriptsScript.ARGUMENT_INTEGER, 3),
		])]),
		_action("PLAYER_SET_MONEY", [
			_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "<This Player>"),
			_argument(MapScriptsScript.ARGUMENT_INTEGER, COUNTER_MONEY),
		]),
	], true)
	# Timer lane: the timer arms once on tick 1 and the gate fires on expiry.
	var arm_timer := _script("Arm Timer", [
		_or_condition([_condition("CONDITION_TRUE", [])]),
		_action("SET_MILLISECOND_TIMER", [
			_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "t"),
			_argument(MapScriptsScript.ARGUMENT_REAL, 0, TIMER_SECONDS),
		]),
	], true)
	var timer_gate := _script("Timer Gate", [
		_or_condition([_condition("TIMER_EXPIRED", [
			_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "t"),
		])]),
		_action("PLAYER_SET_MONEY", [
			_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "<This Player>"),
			_argument(MapScriptsScript.ARGUMENT_INTEGER, TIMER_MONEY),
		]),
	], true)
	return [increment, counter_gate, arm_timer, timer_gate]


func _run() -> void:
	_test_counter_and_timer_gating()
	_test_true_false_and_inversion()
	_test_twin_run_determinism()
	_test_unimplemented_accounting()
	_test_real_contract_payload()
	print("RETAIL_MAP_SCRIPT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _test_counter_and_timer_gating() -> void:
	var sim = _make_sim()
	var scripts = MapScriptsScript.new()
	scripts.load_script_payloads(_gameplay_fixture_payloads())
	# The timer is armed on interpreter tick 1 for TIMER_SECONDS (3.0 s). At the
	# interpreter's 10 ticks per second that is 30 ticks, so it expires on tick
	# 31 - written out as a literal below.
	#
	# It used to be computed as `1 + ceil(ms / (TICK_SECONDS * 1000))`, which is
	# the implementation's own conversion formula. That assertion compared the
	# code to itself and therefore held whether the argument was seconds or
	# milliseconds; it passed for the entire lifetime of a 1000x unit error.
	# Expected values here must stay literal constants derived from the retail
	# MEANING, never expressions sharing terms with the code under test.
	var expected_timer_tick := 31
	var counter_fire_tick := -1
	var timer_fire_tick := -1
	for tick in range(1, 61):
		sim.tick()
		scripts.step(sim)
		var money := int(sim.resources_for_team(0))
		if money == COUNTER_MONEY and counter_fire_tick < 0:
			counter_fire_tick = tick
		if money == TIMER_MONEY and timer_fire_tick < 0:
			timer_fire_tick = tick
	_check("counter_condition_gates_at_exact_tick", counter_fire_tick == 3,
		"fired=%d expected=3" % counter_fire_tick)
	_check("timer_condition_gates_at_exact_tick", timer_fire_tick == expected_timer_tick,
		"fired=%d expected=%d" % [timer_fire_tick, expected_timer_tick])
	_check("deactivate_upon_success_retires_fired_scripts",
		scripts.active_script_names() == ["Increment Counter"],
		"active=%s" % str(scripts.active_script_names()))
	_check("counter_keeps_advancing", int(scripts.counters.get("c", 0)) == 60,
		"c=%d" % int(scripts.counters.get("c", 0)))


func _test_true_false_and_inversion() -> void:
	var sim = _make_sim()
	var scripts = MapScriptsScript.new()
	scripts.load_script_payloads([
		_script("False Branch", [
			_or_condition([_condition("CONDITION_FALSE", [])]),
			_action("SET_FLAG", [
				_argument(MapScriptsScript.ARGUMENT_FLAG_NAME, 0, 0.0, "true_ran"),
				_argument(MapScriptsScript.ARGUMENT_BOOLEAN, 1),
			]),
			_action("SET_FLAG", [
				_argument(MapScriptsScript.ARGUMENT_FLAG_NAME, 0, 0.0, "false_ran"),
				_argument(MapScriptsScript.ARGUMENT_BOOLEAN, 1),
			], "ScriptActionFalse"),
		], false),
		_script("Inverted True", [
			_or_condition([_condition("CONDITION_TRUE", [], true)]),
			_action("SET_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "inv"),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 7),
			], "ScriptActionFalse"),
		], false),
	])
	sim.tick()
	scripts.step(sim)
	_check("false_condition_runs_only_false_actions",
		bool(scripts.flags.get("false_ran", false)) and not scripts.flags.has("true_ran"),
		"flags=%s" % str(scripts.flags))
	_check("inverted_true_condition_evaluates_false",
		int(scripts.counters.get("inv", 0)) == 7,
		"inv=%d" % int(scripts.counters.get("inv", 0)))


func _test_twin_run_determinism() -> void:
	var sim_a = _make_sim()
	var sim_b = _make_sim()
	var control = _make_sim()
	var scripts_a = MapScriptsScript.new()
	var scripts_b = MapScriptsScript.new()
	scripts_a.load_script_payloads(_gameplay_fixture_payloads())
	scripts_b.load_script_payloads(_gameplay_fixture_payloads())
	var twin_ok := true
	var first_divergence := -1
	for tick in range(1, 501):
		sim_a.tick()
		sim_b.tick()
		control.tick()
		scripts_a.step(sim_a)
		scripts_b.step(sim_b)
		if sim_a.state_hash() != sim_b.state_hash() and first_divergence < 0:
			first_divergence = tick
			twin_ok = false
	_check("twin_run_hash_equality_with_scripts_500_ticks", twin_ok,
		"first_divergence=%d" % first_divergence)
	_check("script_mutation_diverges_from_scriptless_control",
		sim_a.state_hash() != control.state_hash())


func _test_unimplemented_accounting() -> void:
	var sim = _make_sim()
	var scripts = MapScriptsScript.new()
	scripts.load_script_payloads([
		_script("Unknown Opcodes", [
			_or_condition([_condition("UNKNOWN_CONDITION_XYZ", [])]),
			_action("TOTALLY_UNKNOWN_ACTION", []),
			_action("TOTALLY_UNKNOWN_ACTION", [], "ScriptActionFalse"),
		], false),
	])
	var load_ok := (
		int(scripts.unimplemented.get("UNKNOWN_CONDITION_XYZ", 0)) == 1
		and int(scripts.unimplemented.get("TOTALLY_UNKNOWN_ACTION", 0)) == 2
	)
	_check("unimplemented_opcodes_counted_at_load", load_ok,
		"unimplemented=%s" % str(scripts.unimplemented))
	sim.tick()
	scripts.step(sim)
	# The unknown condition fails its block (fail-closed), so the false-branch
	# unknown action executes and both runtime misses are counted.
	var runtime_ok := (
		int(scripts.runtime_unimplemented.get("UNKNOWN_CONDITION_XYZ", 0)) == 1
		and int(scripts.runtime_unimplemented.get("TOTALLY_UNKNOWN_ACTION", 0)) == 1
	)
	_check("unimplemented_opcodes_counted_at_runtime", runtime_ok,
		"runtime=%s" % str(scripts.runtime_unimplemented))


func _contract_path() -> String:
	var game_root := ProjectSettings.globalize_path("res://")
	return game_root.rstrip("/").get_base_dir().path_join(CONTRACT_RELATIVE_PATH)


func _test_real_contract_payload() -> void:
	var path := _contract_path()
	if not FileAccess.file_exists(path):
		_check("real_contract_payload_loads", true, "SKIP contract absent at %s" % path)
		return
	var text := FileAccess.get_file_as_string(path)
	var parsed: Variant = JSON.parse_string(text)
	if not (parsed is Dictionary):
		_check("real_contract_payload_loads", false, "contract JSON did not parse")
		return
	var sources: Array = (parsed as Dictionary).get("sources", [])
	if sources.is_empty():
		_check("real_contract_payload_loads", false, "contract has no sources")
		return
	var source: Dictionary = sources[0]
	var scripts = MapScriptsScript.new()
	var loaded := scripts.load_contract_source(source)
	var expected_slots := 0
	for count: Variant in (source.get("actionOpcodes", {}) as Dictionary).values():
		expected_slots += int(count)
	for count: Variant in (source.get("conditionOpcodes", {}) as Dictionary).values():
		expected_slots += int(count)
	var counted := 0
	for count: Variant in scripts.implemented.values():
		counted += int(count)
	for count: Variant in scripts.unimplemented.values():
		counted += int(count)
	var sim = _make_sim()
	var stepped_ok := true
	for tick in range(10):
		sim.tick()
		scripts.step(sim)
	_check("real_contract_payload_loads",
		loaded == int(source.get("scriptCount", -1)) and loaded > 0,
		"loaded=%d expected=%s source=%s" % [loaded, str(source.get("scriptCount")), str(source.get("path"))])
	_check("real_payload_opcodes_fully_bucketed", counted == expected_slots and expected_slots > 0,
		"bucketed=%d expected=%d" % [counted, expected_slots])
	print("RETAIL_MAP_SCRIPT real source=%s implemented=%s" % [str(source.get("path")), str(scripts.implemented)])
	print("RETAIL_MAP_SCRIPT real source unimplemented=%s" % str(scripts.unimplemented))
	_check("real_payload_steps_without_crash", stepped_ok)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_MAP_SCRIPT PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("RETAIL_MAP_SCRIPT FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
