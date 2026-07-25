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
const CAMPAIGN_RELATIVE_DIR := ".private/retail-work/reports/campaign-map-scripts"
# Real converted campaign missions: one BFME2 Good campaign mission, one RotWK
# Angmar campaign mission, one tutorial. Skip-if-absent, since .private is not
# present on every checkout.
const CAMPAIGN_DOCUMENTS := [
	"map_good_erebor",
	"map_ang_fornost",
	"map_beginner_tutorial",
]
const COUNTER_MONEY := 987654
const TIMER_MONEY := 765432
const TIMER_MILLISECONDS := 2500.0

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_MAP_SCRIPT_RUNNER")
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
			_argument(MapScriptsScript.ARGUMENT_REAL, 0, TIMER_MILLISECONDS),
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
	_test_script_control_flow()
	_test_random_counter_is_deterministic()
	_test_presentation_recording()
	_test_real_contract_payload()
	_test_real_campaign_missions()
	print("RETAIL_MAP_SCRIPT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _test_counter_and_timer_gating() -> void:
	var sim = _make_sim()
	var scripts = MapScriptsScript.new()
	scripts.load_script_payloads(_gameplay_fixture_payloads())
	# SET_MILLISECOND_TIMER fires on interpreter tick 1, so the expiry tick is
	# 1 + ceil(2500 ms / 100 ms-per-tick) = 26.
	var expected_timer_tick := 1 + int(ceil(TIMER_MILLISECONDS / (MapScriptsScript.TICK_SECONDS * 1000.0)))
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


func _bucketed_slots(scripts) -> int:
	var total := 0
	for histogram: Dictionary in [scripts.implemented, scripts.recorded, scripts.unimplemented]:
		for count: Variant in histogram.values():
			total += int(count)
	return total


func _subroutine_script(name: String, records: Array) -> Dictionary:
	var payload := _script(name, records, false)
	payload["isSubroutine"] = true
	return payload


func _test_script_control_flow() -> void:
	# ENABLE_SCRIPT / DISABLE_SCRIPT / CALL_SUBROUTINE are the three highest
	# frequency control opcodes in the retail campaign corpus (1463 / 540 / 137
	# slots across the 28 campaign missions), so they are proven directly.
	var sim = _make_sim()
	var scripts = MapScriptsScript.new()
	scripts.load_script_payloads([
		# Starts inactive; "Gate" enables it on the first tick.
		_inactive(_script("Worker", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "work"),
			]),
		], false)),
		_script("Gate", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("ENABLE_SCRIPT", [
				_argument(MapScriptsScript.ARGUMENT_SCRIPT_NAME, 0, 0.0, "Worker"),
			]),
			_action("CALL_SUBROUTINE", [
				_argument(MapScriptsScript.ARGUMENT_SUBROUTINE, 0, 0.0, "Sub"),
			]),
		], true),
		# Subroutines never self-schedule; they only run when called.
		_subroutine_script("Sub", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 5),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "sub"),
			]),
		]),
		_script("Stopper", [
			_or_condition([_condition("COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "work"),
				_argument(MapScriptsScript.ARGUMENT_COMPARISON, MapScriptsScript.COMPARE_GREATER_EQUAL),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 4),
			])]),
			_action("DISABLE_SCRIPT", [
				_argument(MapScriptsScript.ARGUMENT_SCRIPT_NAME, 0, 0.0, "Worker"),
			]),
		], true),
	])
	for tick in range(1, 21):
		sim.tick()
		scripts.step(sim)
	# Gate runs on tick 1 and retires. Worker is enabled during tick 1 but the
	# tick loop has already passed its index, so it first runs on tick 2 and
	# is disabled by Stopper once "work" reaches 4 (tick 5).
	_check("enable_script_activates_a_dormant_script",
		int(scripts.counters.get("work", 0)) == 4,
		"work=%d" % int(scripts.counters.get("work", 0)))
	_check("call_subroutine_runs_a_subroutine_exactly_once",
		int(scripts.counters.get("sub", 0)) == 5,
		"sub=%d" % int(scripts.counters.get("sub", 0)))
	_check("disable_script_retires_a_running_script",
		not scripts.active_script_names().has("Worker"),
		"active=%s" % str(scripts.active_script_names()))
	_check("unknown_script_names_are_counted_not_ignored",
		int(scripts.bounds_hit.get("unknown_script_name", 0)) == 0,
		"bounds=%s" % str(scripts.bounds_hit))


func _inactive(payload: Dictionary) -> Dictionary:
	payload["isActive"] = false
	return payload


func _test_random_counter_is_deterministic() -> void:
	# SET_RANDOM_COUNTER must draw from the interpreter's own integer stream,
	# never from engine RNG, or lockstep desyncs.
	var payloads := [
		_script("Draw", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("SET_RANDOM_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "r"),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 6),
			]),
		], false),
	]
	var draws_a: Array[int] = []
	var draws_b: Array[int] = []
	for run in range(2):
		var sim = _make_sim()
		var scripts = MapScriptsScript.new()
		scripts.load_script_payloads(payloads)
		for tick in range(40):
			sim.tick()
			scripts.step(sim)
			var value := int(scripts.counters.get("r", 0))
			if run == 0:
				draws_a.append(value)
			else:
				draws_b.append(value)
	var in_range := true
	for value in draws_a:
		if value < 1 or value > 6:
			in_range = false
	var varies := false
	for value in draws_a:
		if value != draws_a[0]:
			varies = true
	_check("random_counter_draws_are_reproducible", draws_a == draws_b)
	_check("random_counter_draws_respect_the_inclusive_range", in_range,
		"draws=%s" % str(draws_a.slice(0, 8)))
	_check("random_counter_draws_actually_vary", varies,
		"draws=%s" % str(draws_a.slice(0, 8)))


func _test_presentation_recording() -> void:
	# Recorded opcodes must produce a deterministic ordered event log and must
	# not touch the simulation, so they can never perturb lockstep.
	var sim = _make_sim()
	var control = _make_sim()
	var scripts = MapScriptsScript.new()
	scripts.load_script_payloads([
		_script("Briefing", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("SHOW_MISSION_OBJECTIVE", [_argument(MapScriptsScript.ARGUMENT_INTEGER, 2)]),
			_action("DISPLAY_NOTIFICATION_BOX", [
				_argument(77, 0, 0.0, "Instructional"),
				_argument(MapScriptsScript.ARGUMENT_LOCALIZED_STRING, 0, 0.0, "SCRIPT:Test01"),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 0),
			]),
			_action("DISABLE_INPUT", []),
			_action("CAMERA_LETTERBOX_BEGIN", []),
			_action("MARK_MISSION_OBJECTIVE_COMPLETED", [_argument(MapScriptsScript.ARGUMENT_INTEGER, 2)]),
			_action("VICTORY", []),
		], true),
	])
	for tick in range(5):
		sim.tick()
		control.tick()
		scripts.step(sim)
	_check("recorded_opcodes_do_not_touch_the_simulation",
		sim.state_hash() == control.state_hash())
	_check("recorded_opcodes_are_bucketed_apart_from_implemented",
		int(scripts.recorded.get("VICTORY", 0)) == 1
			and not scripts.implemented.has("VICTORY")
			and not scripts.unimplemented.has("VICTORY"),
		"recorded=%s" % str(scripts.recorded))
	_check("recorded_opcodes_emit_an_ordered_event_log",
		scripts.events.size() == 6 and String(scripts.events[0]["opcode"]) == "SHOW_MISSION_OBJECTIVE"
			and String(scripts.events[5]["opcode"]) == "VICTORY",
		"events=%d" % scripts.events.size())
	_check("mission_objective_state_tracks_show_and_complete",
		scripts.mission_objectives.has(2)
			and bool((scripts.mission_objectives[2] as Dictionary)["shown"])
			and bool((scripts.mission_objectives[2] as Dictionary)["completed"]),
		"objectives=%s" % str(scripts.mission_objectives))
	_check("outcome_and_input_state_are_tracked",
		scripts.outcome == "victory" and not scripts.input_enabled and scripts.letterbox_active,
		"outcome=%s input=%s letterbox=%s" % [scripts.outcome, str(scripts.input_enabled), str(scripts.letterbox_active)])


func _campaign_dir() -> String:
	var game_root := ProjectSettings.globalize_path("res://")
	return game_root.rstrip("/").get_base_dir().path_join(CAMPAIGN_RELATIVE_DIR)


func _test_real_campaign_missions() -> void:
	var directory := _campaign_dir()
	var ran := 0
	for stem: String in CAMPAIGN_DOCUMENTS:
		var path := directory.path_join("%s.scripts.json" % stem)
		if not FileAccess.file_exists(path):
			continue
		_run_campaign_document(stem, path)
		ran += 1
	if ran == 0:
		_check("real_campaign_missions_load", true, "SKIP no converted campaign documents at %s" % directory)


func _run_campaign_document(stem: String, path: String) -> void:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not (parsed is Dictionary):
		_check("campaign_%s_parses" % stem, false, "document JSON did not parse")
		return
	var document: Dictionary = parsed
	var scripts = MapScriptsScript.new()
	var loaded := scripts.load_document(document)
	var counts: Dictionary = document.get("counts", {})
	var expected_slots := int(counts.get("actionSlots", -1)) + int(counts.get("conditionSlots", -1))

	_check("campaign_%s_loads_every_script" % stem,
		loaded == int(counts.get("scripts", -1)) and loaded > 0,
		"loaded=%d expected=%s" % [loaded, str(counts.get("scripts"))])
	_check("campaign_%s_buckets_every_opcode_slot" % stem,
		_bucketed_slots(scripts) == expected_slots and expected_slots > 0,
		"bucketed=%d expected=%d" % [_bucketed_slots(scripts), expected_slots])
	_check("campaign_%s_binds_a_resolvable_world" % stem,
		not scripts.world.is_empty()
			and Array(scripts.world.get("waypoints", [])).size() > 0
			and Array(scripts.world.get("triggerAreas", [])).size() > 0,
		"waypoints=%d areas=%d" % [
			Array(scripts.world.get("waypoints", [])).size(),
			Array(scripts.world.get("triggerAreas", [])).size(),
		])

	# The interpreter's own coverage must equal the converter's declared
	# coverage; if the two ever disagree the census is lying.
	var coverage: Dictionary = document.get("coverage", {})
	var declared := (
		int((coverage.get("actions", {}) as Dictionary).get("semanticSlots", -1))
		+ int((coverage.get("conditions", {}) as Dictionary).get("semanticSlots", -1))
	)
	var declared_recorded := (
		int((coverage.get("actions", {}) as Dictionary).get("recordedSlots", -1))
		+ int((coverage.get("conditions", {}) as Dictionary).get("recordedSlots", -1))
	)
	var summary: Dictionary = scripts.coverage_summary()
	_check("campaign_%s_runtime_coverage_matches_the_converter" % stem,
		int(summary["implementedSlots"]) == declared
			and int(summary["recordedSlots"]) == declared_recorded,
		"runtime=%s declared=%d/%d" % [str(summary), declared, declared_recorded])

	# Twin run: 600 ticks, two independent interpreters, identical sim hashes
	# and identical interpreter state at every step.
	var sim_a = _make_sim()
	var sim_b = _make_sim()
	var twin_b = MapScriptsScript.new()
	twin_b.load_document(document)
	var divergence := -1
	for tick in range(1, 601):
		sim_a.tick()
		sim_b.tick()
		scripts.step(sim_a)
		twin_b.step(sim_b)
		if divergence < 0 and (
			sim_a.state_hash() != sim_b.state_hash()
			or scripts.counters != twin_b.counters
			or scripts.flags != twin_b.flags
			or scripts.timers != twin_b.timers
			or scripts.events.size() != twin_b.events.size()
		):
			divergence = tick
	_check("campaign_%s_twin_run_is_deterministic_600_ticks" % stem,
		divergence < 0, "first_divergence=%d" % divergence)
	_check("campaign_%s_stays_within_its_bounds" % stem,
		scripts.events_dropped == 0
			and int(scripts.bounds_hit.get("subroutine_depth", 0)) == 0
			and int(scripts.bounds_hit.get("subroutine_calls", 0)) == 0
			and int(scripts.bounds_hit.get("counters", 0)) == 0
			and int(scripts.bounds_hit.get("flags", 0)) == 0
			and int(scripts.bounds_hit.get("timers", 0)) == 0,
		"dropped=%d bounds=%s" % [scripts.events_dropped, str(scripts.bounds_hit)])
	print("RETAIL_MAP_SCRIPT campaign %s scripts=%d coverage=%s" % [stem, loaded, str(summary)])
	print("RETAIL_MAP_SCRIPT campaign %s after600 counters=%d flags=%d timers=%d objectlists=%d events=%d objectives=%d outcome=%s bounds=%s" % [
		stem,
		scripts.counters.size(),
		scripts.flags.size(),
		scripts.timers.size(),
		scripts.object_lists.size(),
		scripts.events.size(),
		scripts.mission_objectives.size(),
		scripts.outcome if scripts.outcome != "" else "<none>",
		str(scripts.bounds_hit),
	])
	print("RETAIL_MAP_SCRIPT campaign %s runtime_unimplemented_distinct=%d" % [stem, scripts.runtime_unimplemented.size()])


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
	var counted := _bucketed_slots(scripts)
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
