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
	_test_named_object_registry()
	_test_named_object_spatial_and_ownership()
	_test_team_registry()
	_test_registry_orders_and_bounds()
	_test_named_object_entity_binding()
	_test_world_object_instantiation()
	_test_object_creation()
	_test_reinforcement_teams()
	_test_trigger_area_predicates()
	_test_audio_completion()
	_test_shroud_and_discovery()
	_test_registry_twin_run_determinism()
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
	_test_recorded_presentation_families()


func _test_recorded_presentation_families() -> void:
	# Every recorded opcode, driven at once against a scriptless control. The
	# bucket is only honest if the whole of it - not a sample - provably leaves
	# the simulation untouched, so this exercises each declared opcode and then
	# asserts hash equality with a simulation that ran no scripts at all.
	var sim = _make_sim()
	var control = _make_sim()
	var scripts = MapScriptsScript.new()
	var records: Array = [_or_condition([_condition("CONDITION_TRUE", [])])]
	var declared: Array = MapScriptsScript.RECORDED_ACTIONS.keys()
	declared.sort()
	for opcode: String in declared:
		records.append(_action(opcode, _recorded_arguments(opcode)))
	scripts.load_document(_world_document([_script("Every Recorded Opcode", records, true)]))
	scripts.player_team_bindings["PlyrGood"] = 0
	scripts.player_team_bindings["PlyrEvil"] = 1
	for tick in range(5):
		sim.tick()
		control.tick()
		scripts.step(sim)
	_check("every_recorded_opcode_leaves_the_simulation_untouched",
		sim.state_hash() == control.state_hash())
	_check("every_recorded_opcode_reaches_a_runtime_branch",
		scripts.runtime_unimplemented.is_empty()
			and int(scripts.coverage_summary()["recordedSlots"]) == declared.size()
			# The script's own CONDITION_TRUE is the only implemented slot.
			and int(scripts.coverage_summary()["implementedSlots"]) == 1
			and int(scripts.coverage_summary()["unimplementedSlots"]) == 0,
		"runtime_unimplemented=%s coverage=%s" % [
			str(scripts.runtime_unimplemented), str(scripts.coverage_summary())])
	_check("every_recorded_opcode_emits_exactly_one_event",
		scripts.events.size() == declared.size() and scripts.events_dropped == 0,
		"events=%d declared=%d" % [scripts.events.size(), declared.size()])
	# Spot-check the payloads of the families this tranche added, so the event
	# log is proved to carry the authored arguments and not just an opcode name.
	var by_opcode: Dictionary = {}
	for event: Dictionary in scripts.events:
		by_opcode[String(event["opcode"])] = event
	_check("radar_events_carry_their_subject_and_kind",
		String((by_opcode["OBJECT_CREATE_RADAR_EVENT"] as Dictionary)["object"]) == "Captain"
			and String((by_opcode["OBJECT_CREATE_RADAR_EVENT"] as Dictionary)["radarEvent"]) == "Construction"
			and (by_opcode["RADAR_CREATE_EVENT"] as Dictionary)["position"] == Vector2(12.0, -34.0),
		"events=%s" % str(by_opcode.get("OBJECT_CREATE_RADAR_EVENT", {})))
	_check("audio_and_flash_events_carry_their_authored_arguments",
		String((by_opcode["AUDIO_MAKE_SOUND_IMMUNE_TO_FADE"] as Dictionary)["audioEvent"]) == "AngmarDefeat01"
			and String((by_opcode["ENABLE_OBJECT_SOUND"] as Dictionary)["object"]) == "Captain"
			and String((by_opcode["CAMEO_FLASH"] as Dictionary)["commandButton"]) == "Command_ToggleStance"
			and int((by_opcode["CAMEO_FLASH"] as Dictionary)["count"]) == 10,
		"cameo=%s" % str(by_opcode.get("CAMEO_FLASH", {})))
	_check("hud_and_movie_events_carry_their_authored_arguments",
		String((by_opcode["PLAY_MOVIE_IN_GAME"] as Dictionary)["movie"]) == "Fornost_End"
			and String((by_opcode["DISPLAY_COUNTER"] as Dictionary)["counter"]) == "souls"
			and String((by_opcode["MUSIC_PLAY_TRACK_FINITE_TIMES"] as Dictionary)["track"]) == "LoseScreenEvil",
		"movie=%s" % str(by_opcode.get("PLAY_MOVIE_IN_GAME", {})))


func _recorded_arguments(opcode: String) -> Array:
	## Authored-shaped arguments for one recorded opcode, in the argument types
	## the retail campaign corpus writes for it. Opcodes whose payload this
	## runner does not spot-check still get their real signature, so the
	## "reaches a runtime branch" proof is not run against empty slots.
	var integer := MapScriptsScript.ARGUMENT_INTEGER
	var real := MapScriptsScript.ARGUMENT_REAL
	var boolean := MapScriptsScript.ARGUMENT_BOOLEAN
	match opcode:
		"OBJECT_CREATE_RADAR_EVENT":
			return [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(MapScriptsScript.ARGUMENT_RADAR_EVENT_TYPE, 0, 0.0, "Construction"),
			]
		"TEAM_CREATE_RADAR_EVENT":
			return [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
				_argument(MapScriptsScript.ARGUMENT_RADAR_EVENT_TYPE, 0, 0.0, "Construction"),
			]
		"RADAR_CREATE_EVENT":
			var position := _argument(MapScriptsScript.ARGUMENT_POSITION)
			position["position"] = [12.0, 34.0, 0.0]
			return [position, _argument(MapScriptsScript.ARGUMENT_RADAR_EVENT_TYPE, 0, 0.0, "Battle")]
		"ENABLE_OBJECT_SOUND":
			return [_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain")]
		"AUDIO_MAKE_SOUND_IMMUNE_TO_FADE", "SOUND_DISABLE_TYPE":
			return [_argument(MapScriptsScript.ARGUMENT_SOUND_TYPE, 0, 0.0, "AngmarDefeat01")]
		"AUDIO_SET_REVERB_ROOM_TYPE":
			return [_argument(MapScriptsScript.ARGUMENT_REVERB_ROOM_TYPE, 0, 0.0, "Cave")]
		"AUDIO_SET_REVERB_SUPPRESSION_POLYGON":
			return [_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard")]
		"AUDIO_FADE_VOLUME":
			return [_argument(real, 0, 1.0), _argument(real), _argument(real, 0, 3.0),
				_argument(real, 0, 10.0), _argument(real)]
		"AUDIO_PUSH_MUSIC":
			return [_argument(MapScriptsScript.ARGUMENT_MUSIC_TRACK, 0, 0.0, "AcGood04"),
				_argument(boolean, 1), _argument(boolean, 1)]
		"MUSIC_PLAY_TRACK_FINITE_TIMES":
			return [_argument(MapScriptsScript.ARGUMENT_MUSIC_TRACK, 0, 0.0, "LoseScreenEvil"),
				_argument(integer, 1), _argument(boolean), _argument(boolean)]
		"MUSIC_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY":
			return [_argument(MapScriptsScript.ARGUMENT_MUSIC_TRACK, 0, 0.0, "SX-GoodAction03"),
				_argument(integer, 1), _argument(boolean), _argument(boolean),
				_argument(MapScriptsScript.ARGUMENT_FLAG_NAME, 0, 0.0, "Intro Music Done")]
		"MUSIC_SET_VOLUME":
			return [_argument(real, 0, 0.5)]
		"MUSIC_RETURN_TO_MUSIC_SCRIPTING":
			return [_argument(boolean, 1), _argument(boolean, 1)]
		"MUSIC_RESET_MUSIC_SCRIPTING_SYSTEM", "EVA_SET_ENABLED_DISABLED", "ENABLE_HOUSE_COLOR", "LOCK_CAMERA":
			return [_argument(boolean, 1)]
		"CAMEO_FLASH":
			return [_argument(MapScriptsScript.ARGUMENT_COMMAND_BUTTON, 0, 0.0, "Command_ToggleStance"),
				_argument(integer, 10)]
		"NAMED_FLASH", "NAMED_FLASH_WHITE":
			return [_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(integer, 5)]
		"TEAM_FLASH", "TEAM_FLASH_WHITE":
			return [_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
				_argument(integer, 5)]
		"SELECT_BUILDER_BUTTON_FLASH", "FLASH_SPELL_STORE_BUTTON", "FLASH_OBJECTIVES_BUTTON":
			return [_argument(integer, 10)]
		"HERO_SELECT_BUTTON_FLASH":
			return [_argument(MapScriptsScript.ARGUMENT_HERO_BUTTON, 0, 0.0, "ElvenElrond"),
				_argument(integer, 2)]
		"CAMERA_FADE_ADD", "CAMERA_FADE_MULTIPLY", "CAMERA_FADE_SUBTRACT":
			return [_argument(real, 0, 1.0), _argument(real), _argument(integer, 15),
				_argument(integer, 5), _argument(integer, 15)]
		"CAMERA_FOLLOW_NAMED":
			return [_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(boolean), _argument(real, 0, 500.0)]
		"CAMERA_LOOK_TOWARD_OBJECT":
			return [_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(real), _argument(real), _argument(real), _argument(real), _argument(real)]
		"CAMERA_MOD_LOOK_TOWARD":
			return [_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Middle")]
		"CAMERA_LOOK_TOWARD_WAYPOINT":
			return [_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Middle"),
				_argument(real), _argument(real), _argument(real), _argument(boolean)]
		"SETUP_CAMERA":
			return [_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Start"),
				_argument(real, 0, 0.85), _argument(real, 0, 0.85),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "End")]
		"CAMERA_RESTRICT_TO_AREA":
			return [_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard")]
		"MOVE_CAMERA_ALONG_SPLINE_PATH":
			return [_argument(MapScriptsScript.ARGUMENT_WAYPOINT_PATH, 0, 0.0, "Patrol"),
				_argument(real, 0, 4.0), _argument(real), _argument(real), _argument(real),
				_argument(real)]
		"MOVE_CAMERA_BY_ANIMATION":
			return [_argument(MapScriptsScript.ARGUMENT_CAMERA_ANIMATION, 0, 0.0, "Look at Army")]
		"ROTATE_CAMERA", "PITCH_CAMERA", "FOCAL_LENGTH_CAMERA":
			return [_argument(real, 0, 10.0), _argument(real, 0, 300.0),
				_argument(real, 0, 1.0), _argument(real, 0, 1.0)]
		"SCREEN_SHAKE":
			return [_argument(MapScriptsScript.ARGUMENT_SCREEN_SHAKE_INTENSITY, 4)]
		"MOVE_CAMERA_TO":
			return [_argument(MapScriptsScript.ARGUMENT_CAMERA_WAYPOINT, 0, 0.0, "Middle"),
				_argument(real, 0, 4.0), _argument(real), _argument(real), _argument(real)]
		"RESET_CAMERA":
			return [_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Start"),
				_argument(real, 0, 2.0), _argument(real), _argument(real)]
		"ZOOM_CAMERA":
			return [_argument(real, 0, 1.0), _argument(real, 0, 2.0), _argument(real), _argument(real)]
		"DISPLAY_COUNTER":
			return [_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "souls"),
				_argument(MapScriptsScript.ARGUMENT_LOCALIZED_STRING, 0, 0.0, "SCRIPT:Souls")]
		"HIDE_COUNTER", "HIDE_COUNTDOWN_TIMER":
			return [_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "souls")]
		"DISPLAY_COUNTDOWN_TIMER":
			return [_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "souls"),
				_argument(MapScriptsScript.ARGUMENT_LOCALIZED_STRING, 0, 0.0, "SCRIPT:Souls")]
		"DISPLAY_NOTIFICATION_BOX":
			return [_argument(MapScriptsScript.ARGUMENT_NOTIFICATION_KIND, 0, 0.0, "Instructional"),
				_argument(MapScriptsScript.ARGUMENT_LOCALIZED_STRING, 0, 0.0, "SCRIPT:Test01"),
				_argument(integer)]
		"DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE":
			return [_argument(MapScriptsScript.ARGUMENT_NOTIFICATION_KIND, 0, 0.0, "NewObjective"),
				_argument(MapScriptsScript.ARGUMENT_LOCALIZED_STRING, 0, 0.0, "SCRIPT:Objective_E07_05"),
				_argument(integer, 5),
				_argument(MapScriptsScript.ARGUMENT_OBJECT_TYPE, 0, 0.0, "EreborThrone")]
		"PLAY_MOVIE_IN_GAME":
			return [_argument(MapScriptsScript.ARGUMENT_MOVIE, 0, 0.0, "Fornost_End"),
				_argument(boolean)]
		"SHOW_MISSION_OBJECTIVE", "HIDE_MISSION_OBJECTIVE", "MARK_MISSION_OBJECTIVE_COMPLETED":
			return [_argument(integer, 2)]
		"SHOW_MILITARY_CAPTION":
			return [_argument(MapScriptsScript.ARGUMENT_LOCALIZED_STRING, 0, 0.0, "SCRIPT:Caption"),
				_argument(real, 0, 3.0)]
		"SPEECH_PLAY":
			return [_argument(MapScriptsScript.ARGUMENT_DIALOG, 0, 0.0, "AngmarVictory01"),
				_argument(boolean)]
		"SOUND_PLAY_NAMED":
			return [_argument(MapScriptsScript.ARGUMENT_SOUND, 0, 0.0, "MTrBas_Soldier001"),
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain")]
		"PLAY_SOUND_EFFECT":
			return [_argument(MapScriptsScript.ARGUMENT_SOUND, 0, 0.0, "MTrBas_Soldier001")]
		"PLAY_SOUND_EFFECT_AT":
			return [_argument(MapScriptsScript.ARGUMENT_SOUND, 0, 0.0, "MTrBas_Soldier001"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Middle")]
		"PLAY_SOUND_EFFECT_AT_TEAM":
			return [_argument(MapScriptsScript.ARGUMENT_SOUND, 0, 0.0, "MTrBas_Soldier001"),
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team")]
		_:
			# The remaining recorded opcodes are authored with no arguments at
			# all (HIDE_UI, REFRESH_RADAR, the letterbox pair, the outcome
			# family, the background-sound pair, the camera stop/home pair).
			return []


# --- Named-object registry (B1) and team system (B2) ----------------------
#
# A synthetic world in the exact shape the converter's `world` section emits,
# so the registry proofs exercise the same binding path a real mission takes.


func _fixture_world() -> Dictionary:
	return {
		"available": true,
		"players": [
			{"index": 0, "name": "PlyrGood", "displayName": "PlyrGood",
				"faction": "FactionMen", "isHuman": true, "allies": "", "enemies": "PlyrEvil"},
			{"index": 1, "name": "PlyrEvil", "displayName": "PlyrEvil",
				"faction": "FactionMordor", "isHuman": false, "allies": "", "enemies": "PlyrGood"},
		],
		"teams": [
			{"index": 0, "name": "Guard Team", "owner": "PlyrGood",
				"objectCount": 3, "namedMembers": ["Gate Tower", "Captain"]},
			{"index": 1, "name": "Raider Team", "owner": "PlyrEvil",
				"objectCount": 1, "namedMembers": ["Raider"]},
			{"index": 2, "name": "Empty Team", "owner": "PlyrEvil",
				"objectCount": 0, "namedMembers": []},
			# A reinforcement template: no authored objects, an authored unit
			# composition. Slot 1 is a type the harness content can produce,
			# slot 2 one it cannot, so both spawn paths are exercised.
			{"index": 3, "name": "Reinforcements", "owner": "PlyrGood",
				"objectCount": 0, "namedMembers": [], "units": [
					{"slot": 1, "type": SimScript.SOLDIER_OBJECT_ID,
						"minCount": 2, "maxCount": 2},
					{"slot": 2, "type": "campaign.object.not-in-this-pack",
						"minCount": 1, "maxCount": 1},
				]},
			{"index": 4, "name": "No Template", "owner": "PlyrGood",
				"objectCount": 0, "namedMembers": [], "units": []},
		],
		"waypoints": [
			{"id": 1, "name": "Start", "godotPosition": [0.0, 0.0, 0.0]},
			{"id": 2, "name": "Middle", "godotPosition": [10.0, 0.0, 0.0]},
			{"id": 3, "name": "End", "godotPosition": [20.0, 0.0, 0.0]},
			{"id": 4, "name": "Far", "godotPosition": [100.0, 0.0, 100.0]},
		],
		"waypointPaths": [
			{"label": "Patrol", "ordered": true, "waypointIds": [1, 2, 3]},
			{"label": "Tangled", "ordered": false, "waypointIds": [1, 3]},
		],
		"triggerAreas": [
			{"id": 1, "name": "Courtyard", "godotXZPoints": [
				[-5.0, -5.0], [5.0, -5.0], [5.0, 5.0], [-5.0, 5.0]]},
			{"id": 2, "name": "Outfield", "godotXZPoints": [
				[90.0, 90.0], [110.0, 90.0], [110.0, 110.0], [90.0, 110.0]]},
		],
		"namedObjects": [
			{"name": "Captain", "typeName": "GondorCaptain", "godotPosition": [0.0, 0.0, 0.0],
				"godotYawRadians": 0.0, "originalOwner": "PlyrGood/Guard Team",
				"owner": "PlyrGood", "team": "Guard Team"},
			{"name": "Gate Tower", "typeName": "GondorTower", "godotPosition": [40.0, 0.0, 40.0],
				"godotYawRadians": 0.0, "originalOwner": "PlyrGood/Guard Team",
				"owner": "PlyrGood", "team": "Guard Team"},
			{"name": "Raider", "typeName": "MordorOrc", "godotPosition": [100.0, 0.0, 100.0],
				"godotYawRadians": 0.0, "originalOwner": "PlyrEvil/Raider Team",
				"owner": "PlyrEvil", "team": "Raider Team"},
		],
	}


func _world_document(payloads: Array) -> Dictionary:
	var rows: Array = []
	for payload: Variant in payloads:
		rows.append({"payload": payload})
	return {
		"schema": "openbfme.map-scripts",
		"schemaVersion": 1,
		"source": {"container": "map", "sourceBytes": 0, "sourceSha256": "fixture"},
		"world": _fixture_world(),
		"scripts": rows,
	}


func _once(name: String, records: Array) -> Dictionary:
	return _script(name, records, true)


func _test_named_object_registry() -> void:
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_once("Delete Raider", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("NAMED_DELETE", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Raider"),
			]),
			_action("NAMED_SET_ATTITUDE", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(MapScriptsScript.ARGUMENT_ATTITUDE, MapScriptsScript.ATTITUDE_AGGRESSIVE),
			]),
		]),
		_script("Watch Raider", [
			_or_condition([_condition("NAMED_DESTROYED", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Raider"),
			])]),
			_action("SET_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "raider_gone"),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
			]),
		], false),
		_script("Watch Captain", [
			_or_condition([_condition("NAMED_NOT_DESTROYED", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "captain_alive"),
			]),
		], false),
		_script("Watch Ghost", [
			# A name this mission never authored must fail closed on both the
			# positive and the negative form, never answer "alive".
			_or_condition([_condition("NAMED_NOT_DESTROYED", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "No Such Object"),
			])]),
			_action("SET_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "ghost"),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
			]),
		], false),
	]))
	_check("registry_seeds_every_authored_named_object",
		scripts.named_objects.size() == 3 and scripts.waypoints.size() == 4
			and scripts.trigger_areas.size() == 2 and scripts.script_teams.size() == 5,
		"objects=%d waypoints=%d areas=%d" % [
			scripts.named_objects.size(), scripts.waypoints.size(), scripts.trigger_areas.size()])
	_check("registry_refuses_unordered_waypoint_paths",
		scripts.waypoint_paths.size() == 1 and scripts.waypoint_paths.has("Patrol")
			and int(scripts.bounds_hit.get("unordered_waypoint_path", 0)) == 1,
		"paths=%s bounds=%s" % [str(scripts.waypoint_paths.keys()), str(scripts.bounds_hit)])

	var sim = _make_sim()
	for tick in range(4):
		sim.tick()
		scripts.step(sim)
	_check("named_delete_makes_named_destroyed_true",
		int(scripts.counters.get("raider_gone", 0)) == 1
			and bool(scripts.named_object_state("Raider")["deleted"]),
		"counters=%s" % str(scripts.counters))
	_check("named_not_destroyed_tracks_a_living_object",
		int(scripts.counters.get("captain_alive", 0)) == 4,
		"captain_alive=%d" % int(scripts.counters.get("captain_alive", 0)))
	_check("unknown_named_objects_fail_closed_and_are_counted",
		not scripts.counters.has("ghost")
			and int(scripts.bounds_hit.get("unknown_named_object", 0)) == 4,
		"counters=%s bounds=%s" % [str(scripts.counters), str(scripts.bounds_hit)])
	_check("named_set_attitude_stores_the_authored_value",
		int(scripts.named_object_state("Captain")["attitude"])
			== MapScriptsScript.ATTITUDE_AGGRESSIVE
			and int(scripts.bounds_hit.get("attitude_unmapped", 0)) == 0,
		"captain=%s" % str(scripts.named_object_state("Captain")))


func _test_named_object_spatial_and_ownership() -> void:
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_script("Captain In Courtyard", [
			_or_condition([_condition("NAMED_INSIDE_AREA", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "inside"),
			]),
		], false),
		_script("Tower In Courtyard", [
			_or_condition([_condition("NAMED_INSIDE_AREA", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Gate Tower"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "outside"),
			]),
		], false),
		_script("Ownership", [
			_or_condition([_condition("NAMED_OWNED_BY_PLAYER", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Raider"),
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "PlyrGood"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "good_owns_raider"),
			]),
		], false),
		_once("Transfer", [
			_or_condition([_condition("COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "inside"),
				_argument(MapScriptsScript.ARGUMENT_COMPARISON, MapScriptsScript.COMPARE_GREATER_EQUAL),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 2),
			])]),
			_action("TEAM_TRANSFER_TO_PLAYER", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "PlyrEvil/Raider Team"),
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "PlyrGood"),
			]),
		]),
	]))
	var sim = _make_sim()
	for tick in range(6):
		sim.tick()
		scripts.step(sim)
	_check("named_inside_area_answers_a_point_in_polygon_query",
		int(scripts.counters.get("inside", 0)) == 6
			and not scripts.counters.has("outside"),
		"inside=%d outside=%d" % [
			int(scripts.counters.get("inside", 0)), int(scripts.counters.get("outside", 0))])
	# The transfer fires on tick 2, so ownership reads PlyrGood from tick 3 on.
	_check("team_transfer_to_player_reowns_named_members",
		int(scripts.counters.get("good_owns_raider", 0)) == 4
			and String(scripts.named_object_state("Raider")["owner"]) == "PlyrGood",
		"count=%d owner=%s" % [
			int(scripts.counters.get("good_owns_raider", 0)),
			String(scripts.named_object_state("Raider")["owner"])])
	_check("qualified_team_arguments_resolve_to_the_bare_team",
		int(scripts.bounds_hit.get("unknown_script_team", 0)) == 0,
		"bounds=%s" % str(scripts.bounds_hit))


func _test_team_registry() -> void:
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_script("Guard Alive", [
			_or_condition([_condition("TEAM_HAS_UNITS", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "guard_units"),
			]),
		], false),
		_script("Empty Destroyed", [
			_or_condition([_condition("TEAM_DESTROYED", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Empty Team"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "empty_dead"),
			]),
		], false),
		_script("Raider Destroyed", [
			_or_condition([_condition("TEAM_DESTROYED", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Raider Team"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "raiders_dead"),
			]),
		], false),
		_once("Kill Raider", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("NAMED_DELETE", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Raider"),
			]),
		]),
		_once("Move Captain", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("UNIT_SET_TEAM", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Empty Team"),
			]),
			_action("TEAM_SET_ATTITUDE", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
				_argument(MapScriptsScript.ARGUMENT_ATTITUDE, MapScriptsScript.ATTITUDE_PASSIVE),
			]),
		]),
	]))
	var guard_before: Dictionary = scripts.team_state("Guard Team")
	var sim = _make_sim()
	for tick in range(5):
		sim.tick()
		scripts.step(sim)
	_check("team_has_units_counts_unnamed_authored_members",
		int(guard_before["anonymous_members"]) == 1
			and int(guard_before["living_members"]) == 3
			and int(scripts.counters.get("guard_units", 0)) == 5,
		"guard=%s counted=%d" % [str(guard_before), int(scripts.counters.get("guard_units", 0))])
	_check("team_destroyed_is_true_for_a_team_with_no_members",
		int(scripts.counters.get("empty_dead", 0)) >= 1,
		"empty_dead=%d" % int(scripts.counters.get("empty_dead", 0)))
	# The Raider dies on tick 1, so its team reads destroyed from tick 2 on.
	_check("team_destroyed_follows_its_last_member_out_of_the_world",
		int(scripts.counters.get("raiders_dead", 0)) == 4,
		"raiders_dead=%d" % int(scripts.counters.get("raiders_dead", 0)))
	var empty_after: Dictionary = scripts.team_state("Empty Team")
	_check("unit_set_team_moves_a_named_member_between_teams",
		Array(empty_after["members"]).has("Captain")
			and not Array(scripts.team_state("Guard Team")["members"]).has("Captain")
			and String(scripts.named_object_state("Captain")["team"]) == "Empty Team",
		"empty=%s guard=%s" % [
			str(empty_after["members"]), str(scripts.team_state("Guard Team")["members"])])
	_check("team_set_attitude_reaches_the_teams_named_members",
		int(scripts.team_state("Guard Team")["attitude"]) == MapScriptsScript.ATTITUDE_PASSIVE
			and int(scripts.named_object_state("Gate Tower")["attitude"])
				== MapScriptsScript.ATTITUDE_PASSIVE,
		"team=%d tower=%d" % [
			int(scripts.team_state("Guard Team")["attitude"]),
			int(scripts.named_object_state("Gate Tower")["attitude"])])


func _test_registry_orders_and_bounds() -> void:
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_once("Order Everything", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("MOVE_NAMED_UNIT_TO", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "End"),
			]),
			_action("NAMED_FOLLOW_WAYPOINTS_EXACT", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Gate Tower"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT_PATH, 0, 0.0, "Patrol"),
			]),
			_action("NAMED_FOLLOW_WAYPOINTS_EXACT", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT_PATH, 0, 0.0, "Tangled"),
			]),
			_action("MOVE_NAMED_UNIT_TO", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Gate Tower"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Nowhere"),
			]),
			_action("TEAM_HUNT", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Raider Team"),
			]),
			_action("TEAM_MERGE_INTO_TEAM", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Raider Team"),
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
			]),
		]),
	]))
	var sim = _make_sim()
	var control = _make_sim()
	sim.tick()
	control.tick()
	scripts.step(sim)
	var captain: Dictionary = scripts.named_object_state("Captain")
	var tower: Dictionary = scripts.named_object_state("Gate Tower")
	_check("move_named_unit_to_resolves_its_waypoint",
		String(captain["order"]["kind"]) == "move"
			and (captain["order"]["destination"] as Vector2) == Vector2(20.0, 0.0),
		"captain order=%s" % str(captain["order"]))
	_check("named_follow_waypoints_exact_resolves_an_ordered_path",
		String(tower["order"]["kind"]) == "follow_waypoints"
			and bool(tower["order"]["exact"])
			and Array(tower["order"]["points"]).size() == 3,
		"tower order=%s" % str(tower["order"]))
	_check("unknown_waypoints_and_unordered_paths_are_refused_not_guessed",
		int(scripts.bounds_hit.get("unknown_waypoint", 0)) == 1
			and int(scripts.bounds_hit.get("unknown_waypoint_path", 0)) == 1,
		"bounds=%s" % str(scripts.bounds_hit))
	_check("orders_without_a_bound_entity_are_deferred_and_counted",
		int(scripts.deferred_orders.get("MOVE_NAMED_UNIT_TO", 0)) == 1
			and int(scripts.deferred_orders.get("NAMED_FOLLOW_WAYPOINTS_EXACT", 0)) == 1
			and int(scripts.deferred_orders.get("TEAM_HUNT", 0)) == 1,
		"deferred=%s" % str(scripts.deferred_orders))
	var guard: Dictionary = scripts.team_state("Guard Team")
	_check("team_merge_into_team_moves_members_and_empties_the_source",
		Array(guard["members"]).has("Raider")
			and int(guard["anonymous_members"]) == 1
			and int(scripts.team_state("Raider Team")["living_members"]) == 0,
		"guard=%s raider=%s" % [str(guard), str(scripts.team_state("Raider Team"))])
	_check("registry_orders_do_not_touch_an_unbound_simulation",
		sim.state_hash() == control.state_hash())


func _test_named_object_entity_binding() -> void:
	# The registry has to stay correct across a bound entity's death, and an
	# order aimed at a bound object has to reach the simulation instead of
	# being deferred.
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_script("Watch Captain", [
			_or_condition([_condition("NAMED_DESTROYED", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "captain_dead"),
			]),
		], false),
		_once("Order Captain", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("MOVE_NAMED_UNIT_TO", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Start"),
			]),
		]),
	]))
	var sim = _make_sim()
	var ids := sim.living_ids(0)
	if ids.is_empty():
		_check("named_object_binding_tracks_a_live_entity", false, "harness sim spawned no entity")
		return
	var entity_id: int = ids[0]
	scripts.player_team_bindings["PlyrGood"] = 0
	var bound := scripts.bind_named_object("Captain", entity_id)
	# The registry follows the bound entity's position, in document space.
	sim.tick()
	scripts.step(sim)
	var captain: Dictionary = scripts.named_object_state("Captain")
	_check("named_object_binding_tracks_a_live_entity",
		bound and (captain["position"] as Vector2) == (sim.entity(entity_id)["position"] as Vector2)
			and not bool(captain["destroyed"]),
		"bound=%s captain=%s" % [str(bound), str(captain)])
	_check("orders_for_a_bound_object_reach_the_simulation",
		not scripts.deferred_orders.has("MOVE_NAMED_UNIT_TO")
			and String(sim.entity(entity_id).get("order_kind", "")) == "move",
		"deferred=%s order_kind=%s" % [
			str(scripts.deferred_orders), str(sim.entity(entity_id).get("order_kind", ""))])
	# Kill the entity underneath the registry.
	var row: Dictionary = sim.entity(entity_id)
	row["health"] = 0
	sim.tick()
	scripts.step(sim)
	sim.tick()
	scripts.step(sim)
	_check("named_destroyed_follows_a_bound_entity_out_of_the_simulation",
		int(scripts.counters.get("captain_dead", 0)) == 2
			and bool(scripts.named_object_state("Captain")["destroyed"])
			and int(scripts.named_object_state("Captain")["entity_id"]) == -1,
		"counter=%d captain=%s" % [
			int(scripts.counters.get("captain_dead", 0)),
			str(scripts.named_object_state("Captain"))])


func _test_world_object_instantiation() -> void:
	# The mission's authored starting objects have to reach the simulation, and
	# a type this content cannot produce has to stay registry-only and be named
	# rather than substituted.
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_once("Order Captain", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("MOVE_NAMED_UNIT_TO", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "End"),
			]),
		]),
	]))
	# Author the Captain as a type the harness content carries, so exactly one
	# of the three authored objects can be instantiated.
	(scripts.named_objects["Captain"] as Dictionary)["type_name"] = SimScript.SOLDIER_OBJECT_ID
	scripts.player_team_bindings["PlyrGood"] = 0
	scripts.player_team_bindings["PlyrEvil"] = 1
	var sim = _make_sim()
	var before := sim.living_ids(0).size()
	var instantiated: int = scripts.instantiate_world_objects(sim)
	_check("instantiate_world_objects_places_the_authored_starting_objects",
		instantiated == 1
			and sim.living_ids(0).size() == before + 1
			and int(scripts.named_object_state("Captain")["entity_id"]) >= 0,
		"instantiated=%d living=%d captain=%s" % [
			instantiated, sim.living_ids(0).size(),
			str(scripts.named_object_state("Captain"))])
	_check("an_object_type_the_content_lacks_is_named_not_substituted",
		int(scripts.unavailable_object_types.get("GondorTower", 0)) == 1
			and int(scripts.unavailable_object_types.get("MordorOrc", 0)) == 1
			and int(scripts.named_object_state("Gate Tower")["entity_id"]) == -1
			and not bool(scripts.named_object_state("Gate Tower")["destroyed"]),
		"unavailable=%s" % str(scripts.unavailable_object_types))
	# An instantiated object's orders stop deferring: that is the whole point.
	sim.tick()
	scripts.step(sim)
	_check("instantiation_turns_a_deferred_order_into_a_real_one",
		not scripts.deferred_orders.has("MOVE_NAMED_UNIT_TO")
			and String(sim.entity(
				int(scripts.named_object_state("Captain")["entity_id"])
			).get("order_kind", "")) == "move",
		"deferred=%s" % str(scripts.deferred_orders))


func _test_object_creation() -> void:
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_once("Spawn", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("CREATE_NAMED_ON_TEAM_AT_WAYPOINT", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Relief Captain"),
				_argument(MapScriptsScript.ARGUMENT_OBJECT_TYPE, 0, 0.0, SimScript.SOLDIER_OBJECT_ID),
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "PlyrGood/Guard Team"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Middle"),
			]),
			_action("CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT", [
				_argument(MapScriptsScript.ARGUMENT_OBJECT_TYPE, 0, 0.0, "campaign.object.absent"),
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Middle"),
			]),
			_action("CREATE_NAMED_ON_TEAM_AT_WAYPOINT", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Ghost"),
				_argument(MapScriptsScript.ARGUMENT_OBJECT_TYPE, 0, 0.0, SimScript.SOLDIER_OBJECT_ID),
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Nowhere"),
			]),
		]),
		_once("Order The New Captain", [
			_or_condition([_condition("COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "never"),
				_argument(MapScriptsScript.ARGUMENT_COMPARISON, MapScriptsScript.COMPARE_EQUAL),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 0),
			])]),
			_action("MOVE_NAMED_UNIT_TO", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Relief Captain"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "End"),
			]),
		]),
	]))
	scripts.player_team_bindings["PlyrGood"] = 0
	var sim = _make_sim()
	var before := sim.living_ids(0).size()
	sim.tick()
	scripts.step(sim)
	var relief: Dictionary = scripts.named_object_state("Relief Captain")
	_check("create_named_on_team_at_waypoint_registers_and_instantiates",
		not relief.is_empty()
			and (relief["position"] as Vector2) == Vector2(10.0, 0.0)
			and String(relief["team"]) == "Guard Team"
			and String(relief["owner"]) == "PlyrGood"
			and int(relief["entity_id"]) >= 0
			and sim.living_ids(0).size() == before + 1,
		"relief=%s living=%d" % [str(relief), sim.living_ids(0).size()])
	_check("a_created_object_joins_its_team_roster",
		Array(scripts.team_state("Guard Team")["members"]).has("Relief Captain"),
		"guard=%s" % str(scripts.team_state("Guard Team")))
	# The unnamed spawn is a real row on the team, addressable by nobody: the
	# team started with two named members plus one authored anonymous one.
	_check("create_unnamed_on_team_at_waypoint_rows_an_unaddressable_object",
		int(scripts.team_state("Guard Team")["living_members"]) == 5
			and int(scripts.unavailable_object_types.get("campaign.object.absent", 0)) == 1,
		"guard=%s unavailable=%s" % [
			str(scripts.team_state("Guard Team")), str(scripts.unavailable_object_types)])
	_check("a_creation_at_an_unknown_waypoint_is_refused_not_placed",
		scripts.named_object_state("Ghost").is_empty()
			and int(scripts.bounds_hit.get("unknown_waypoint", 0)) == 1,
		"bounds=%s" % str(scripts.bounds_hit))
	# And the order aimed at the created object reaches the simulation.
	sim.tick()
	scripts.step(sim)
	_check("orders_to_a_created_object_are_not_deferred",
		not scripts.deferred_orders.has("MOVE_NAMED_UNIT_TO"),
		"deferred=%s" % str(scripts.deferred_orders))


func _test_reinforcement_teams() -> void:
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_once("Reinforce", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("CREATE_REINFORCEMENT_TEAM", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Reinforcements"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Far"),
			]),
			_action("CREATE_REINFORCEMENT_TEAM", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "No Template"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Far"),
			]),
		]),
		_script("Reinforcements Arrived", [
			_or_condition([_condition("TEAM_HAS_UNITS", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Reinforcements"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "arrived"),
			]),
		], false),
	]))
	scripts.player_team_bindings["PlyrGood"] = 0
	var sim = _make_sim()
	var before := sim.living_ids(0).size()
	sim.tick()
	scripts.step(sim)
	var team: Dictionary = scripts.team_state("Reinforcements")
	_check("create_reinforcement_team_instantiates_the_authored_composition",
		int(team["living_members"]) == 3
			and sim.living_ids(0).size() == before + 2
			and int(scripts.unavailable_object_types.get(
				"campaign.object.not-in-this-pack", 0)) == 1,
		"team=%s living=%d unavailable=%s" % [
			str(team), sim.living_ids(0).size(), str(scripts.unavailable_object_types)])
	_check("an_empty_reinforcement_template_creates_nothing_and_says_so",
		int(scripts.team_state("No Template")["living_members"]) == 0
			and int(scripts.bounds_hit.get(
				"reinforcement_team_without_composition", 0)) == 1,
		"bounds=%s" % str(scripts.bounds_hit))
	sim.tick()
	scripts.step(sim)
	_check("a_reinforced_team_answers_team_has_units",
		int(scripts.counters.get("arrived", 0)) == 2,
		"arrived=%d" % int(scripts.counters.get("arrived", 0)))


func _test_trigger_area_predicates() -> void:
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_once("Reinforce Outfield", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("CREATE_REINFORCEMENT_TEAM", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Reinforcements"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Far"),
			]),
		]),
		_script("Good Holds The Courtyard", [
			_or_condition([_condition("SKIRMISH_PLAYER_HAS_UNITS_IN_AREA", [
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "PlyrGood"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "courtyard"),
			]),
		], false),
		_script("Local Player Holds The Courtyard", [
			_or_condition([_condition("SKIRMISH_PLAYER_HAS_UNITS_IN_AREA", [
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "<Local Player>"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "local"),
			]),
		], false),
		_script("Two Captains In The Courtyard", [
			_or_condition([_condition("PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA", [
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "PlyrGood"),
				_argument(MapScriptsScript.ARGUMENT_COMPARISON, MapScriptsScript.COMPARE_GREATER_EQUAL),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 2),
				_argument(MapScriptsScript.ARGUMENT_OBJECT_TYPE_OR_LIST, 0, 0.0, "GondorCaptain"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "two_captains"),
			]),
		], false),
		_script("One Captain In The Courtyard", [
			_or_condition([_condition(
				"PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_COMPLETELY_BUILT", [
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "PlyrGood"),
				_argument(MapScriptsScript.ARGUMENT_COMPARISON, MapScriptsScript.COMPARE_EQUAL),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_OBJECT_TYPE_OR_LIST, 0, 0.0, "GondorCaptain"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "one_captain"),
			]),
		], false),
		_script("Reinforcements Reached The Outfield", [
			_or_condition([_condition("TEAM_INSIDE_AREA_ENTIRELY", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Reinforcements"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Outfield"),
				_argument(MapScriptsScript.ARGUMENT_AREA_MEMBER_FILTER,
					MapScriptsScript.AREA_MEMBER_FILTER_ALL),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "entirely"),
			]),
		], false),
		_script("Guard Team Partly In The Courtyard", [
			_or_condition([_condition("TEAM_INSIDE_AREA_PARTIALLY", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard"),
				_argument(MapScriptsScript.ARGUMENT_AREA_MEMBER_FILTER,
					MapScriptsScript.AREA_MEMBER_FILTER_ALL),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "partially"),
			]),
		], false),
		_script("Reinforcements Filtered By An Unmapped Value", [
			_or_condition([_condition("TEAM_INSIDE_AREA_PARTIALLY", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Reinforcements"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Outfield"),
				_argument(MapScriptsScript.ARGUMENT_AREA_MEMBER_FILTER, 3),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "filtered"),
			]),
		], false),
	]))
	scripts.player_team_bindings["PlyrGood"] = 0
	scripts.player_team_bindings["PlyrEvil"] = 1
	var sim = _make_sim()
	for tick in range(3):
		sim.tick()
		scripts.step(sim)
	# The Captain sits at the origin, inside the Courtyard; the Gate Tower and
	# the Raider are far outside it.
	_check("skirmish_player_has_units_in_area_sees_the_registry",
		int(scripts.counters.get("courtyard", 0)) == 3,
		"courtyard=%d" % int(scripts.counters.get("courtyard", 0)))
	_check("an_unresolved_local_player_fails_closed_and_is_counted",
		not scripts.counters.has("local")
			and int(scripts.bounds_hit.get("scope_relative_player", 0)) == 3,
		"local=%d bounds=%s" % [
			int(scripts.counters.get("local", 0)), str(scripts.bounds_hit)])
	_check("a_typed_trigger_area_comparison_counts_only_that_type",
		int(scripts.counters.get("one_captain", 0)) == 3
			and not scripts.counters.has("two_captains"),
		"one=%d two=%d" % [
			int(scripts.counters.get("one_captain", 0)),
			int(scripts.counters.get("two_captains", 0))])
	# The reinforcements spawned at Far (100, 100), which is inside Outfield.
	_check("team_inside_area_entirely_answers_over_created_members",
		int(scripts.counters.get("entirely", 0)) == 3,
		"entirely=%d team=%s" % [
			int(scripts.counters.get("entirely", 0)),
			str(scripts.team_state("Reinforcements"))])
	# Guard Team carries one authored object with no name, so its membership is
	# not fully tracked and the predicate refuses rather than under-reporting.
	_check("a_team_with_untracked_members_refuses_the_area_predicate",
		not scripts.counters.has("partially")
			and int(scripts.bounds_hit.get("team_untracked_members", 0)) == 3,
		"partially=%d bounds=%s" % [
			int(scripts.counters.get("partially", 0)), str(scripts.bounds_hit)])
	_check("an_unmapped_area_member_filter_refuses_the_predicate",
		not scripts.counters.has("filtered")
			and int(scripts.bounds_hit.get("team_area_filter_unmapped", 0)) == 3,
		"filtered=%d bounds=%s" % [
			int(scripts.counters.get("filtered", 0)), str(scripts.bounds_hit)])
	# With a local player bound, the same predicate resolves.
	var hosted = MapScriptsScript.new()
	hosted.load_document(_world_document([
		_script("Local Player Holds The Courtyard", [
			_or_condition([_condition("SKIRMISH_PLAYER_HAS_UNITS_IN_AREA", [
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "<Local Player>"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "local"),
			]),
		], false),
	]))
	hosted.local_player = "PlyrGood"
	hosted.player_team_bindings["PlyrGood"] = 0
	var hosted_sim = _make_sim()
	hosted_sim.tick()
	hosted.step(hosted_sim)
	_check("a_host_supplied_local_player_resolves_the_predicate",
		int(hosted.counters.get("local", 0)) == 1
			and int(hosted.bounds_hit.get("scope_relative_player", 0)) == 0,
		"local=%d bounds=%s" % [
			int(hosted.counters.get("local", 0)), str(hosted.bounds_hit)])


func _audio_wait_script(name: String, audio_event: String, counter: String) -> Dictionary:
	return _script(name, [
		_or_condition([_condition("HAS_FINISHED_AUDIO", [
			_argument(MapScriptsScript.ARGUMENT_SOUND, 0, 0.0, audio_event),
		])]),
		_action("INCREMENT_COUNTER", [
			_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
			_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, counter),
		]),
	], false)


func _test_audio_completion() -> void:
	# B6. HAS_FINISHED_AUDIO is answered from the authored length of the audio
	# event and the tick index: the condition itself arms the wait, holds false
	# until the length has elapsed, then reports complete once and re-arms.
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_audio_wait_script("Wait Briefing", "Briefing", "briefing_done"),
		_audio_wait_script("Wait Missing", "NotInThisPack", "missing_done"),
	]))
	# One second of speech at ten logic ticks per second.
	scripts.set_audio_event_durations({"Briefing": 1.0, "": 2.0, "Bad": -1.0})
	var sim = _make_sim()
	var first_complete := -1
	for tick in range(1, 31):
		sim.tick()
		scripts.step(sim)
		if first_complete < 0 and int(scripts.counters.get("briefing_done", 0)) > 0:
			first_complete = tick
	_check("an_audio_wait_holds_for_the_authored_length",
		first_complete == 11,
		"first_complete=%d" % first_complete)
	_check("an_audio_wait_retires_and_re_arms",
		int(scripts.counters.get("briefing_done", 0)) == 2,
		"briefing_done=%d" % int(scripts.counters.get("briefing_done", 0)))
	# Retail reads an audio event the content does not carry as zero length, so
	# it completes on its first evaluation. That is a fail-open, so every one of
	# them is counted and can never pass as authored pacing.
	_check("an_audio_event_without_an_authored_length_completes_immediately",
		int(scripts.counters.get("missing_done", 0)) == 30
			and int(scripts.bounds_hit.get("audio_event_length_unknown", 0)) == 30,
		"missing_done=%d bounds=%s" % [
			int(scripts.counters.get("missing_done", 0)), str(scripts.bounds_hit)])
	_check("a_malformed_audio_length_is_refused_not_stored",
		int(scripts.bounds_hit.get("audio_event_length_refused", 0)) == 2
			and scripts.audio_event_durations.size() == 1,
		"durations=%s bounds=%s" % [
			str(scripts.audio_event_durations), str(scripts.bounds_hit)])
	# The pending list is bounded, and a refused wait is never complete.
	var flooded = MapScriptsScript.new()
	var flood_records: Array = [_or_condition([_condition("CONDITION_TRUE", [])])]
	var flood_blocks: Array = []
	for index in range(MapScriptsScript.MAX_AUDIO_WAITS + 8):
		flood_blocks.append(_condition("HAS_FINISHED_AUDIO", [
			_argument(MapScriptsScript.ARGUMENT_SOUND, 0, 0.0, "Speech%04d" % index),
		], true))
	flood_records = [_or_condition(flood_blocks)]
	var flood_durations: Dictionary = {}
	for index in range(MapScriptsScript.MAX_AUDIO_WAITS + 8):
		flood_durations["Speech%04d" % index] = 60.0
	flooded.load_document(_world_document([_script("Flood", flood_records, false)]))
	flooded.set_audio_event_durations(flood_durations)
	var flood_sim = _make_sim()
	flood_sim.tick()
	flooded.step(flood_sim)
	_check("the_audio_wait_list_is_bounded",
		flooded._audio_waits.size() == MapScriptsScript.MAX_AUDIO_WAITS
			and int(flooded.bounds_hit.get("audio_waits", 0)) == 8,
		"waits=%d bounds=%s" % [flooded._audio_waits.size(), str(flooded.bounds_hit)])


func _reveal_area_action(area: String, player: String, handle: String) -> Dictionary:
	return _action("MAP_REVEAL_PERMANENTLY_IN_TRIGGER", [
		_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, area),
		_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, player),
		_argument(MapScriptsScript.ARGUMENT_REVEAL_NAME, 0, 0.0, handle),
	])


func _discovery_script(name: String, object_name: String, player: String, counter: String) -> Dictionary:
	return _script(name, [
		_or_condition([_condition("NAMED_DISCOVERED", [
			_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, object_name),
			_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, player),
		])]),
		_action("INCREMENT_COUNTER", [
			_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
			_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, counter),
		]),
	], false)


func _test_shroud_and_discovery() -> void:
	# Permanent map reveals and the discovery predicates that read them. The
	# Raider sits at (100, 100), inside the Outfield area and on top of the Far
	# waypoint; nothing else in the fixture world is near it, so a discovery
	# there can only come from a reveal or from a unit's sight.
	# The reveal scripts are loaded ahead of the predicates that read them, so
	# each tick files its reveal before the same tick's discovery question - the
	# retail script list is evaluated in source order and this fixture relies on
	# exactly that.
	var scripts = MapScriptsScript.new()
	scripts.load_document(_world_document([
		_inactive(_script("Reveal", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_reveal_area_action("Outfield", "PlyrGood", "Reveal Outfield"),
		], true)),
		_inactive(_script("Unreveal", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("MAP_UNDO_REVEAL_PERMANENTLY_IN_TRIGGER", [
				_argument(MapScriptsScript.ARGUMENT_REVEAL_NAME, 0, 0.0, "Reveal Outfield"),
			]),
			# A handle this mission never filed, and a reveal with no radius:
			# both are refusals, not approximations.
			_action("MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT", [
				_argument(MapScriptsScript.ARGUMENT_REVEAL_NAME, 0, 0.0, "Never Filed"),
			]),
			_action("MAP_REVEAL_PERMANENTLY_AT_WAYPOINT", [
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Far"),
				_argument(MapScriptsScript.ARGUMENT_REAL, 0, 0.0),
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "PlyrGood"),
				_argument(MapScriptsScript.ARGUMENT_REVEAL_NAME, 0, 0.0, "Zero Radius"),
			]),
		], true)),
		_inactive(_script("Reveal Far For All", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("MAP_REVEAL_PERMANENTLY_AT_WAYPOINT", [
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Far"),
				_argument(MapScriptsScript.ARGUMENT_REAL, 0, 5.0),
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0,
					MapScriptsScript.PLAYER_ALL_REFERENCE),
				_argument(MapScriptsScript.ARGUMENT_REVEAL_NAME, 0, 0.0, "Reveal Far"),
			]),
		], true)),
		_discovery_script("Good Sees Raider", "Raider", "PlyrGood", "good_sees"),
		_script("Team Seen", [
			_or_condition([_condition("TEAM_DISCOVERED", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Raider Team"),
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "PlyrGood"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "team_seen"),
			]),
		], false),
	]))
	scripts.player_team_bindings["PlyrGood"] = 0
	scripts.player_team_bindings["PlyrEvil"] = 1
	var sim = _make_sim()

	# Tick 1: nothing revealed and nothing of PlyrGood's is instantiated, so the
	# Raider is shrouded and the missing sight is counted rather than assumed.
	sim.tick()
	scripts.step(sim)
	_check("an_unrevealed_object_is_not_discovered",
		int(scripts.counters.get("good_sees", 0)) == 0
			and int(scripts.counters.get("team_seen", 0)) == 0
			and int(scripts.bounds_hit.get("discovery_vision_unavailable", 0)) > 0
			and int(scripts.bounds_hit.get("discovery_ignores_stealth", 0)) == 1,
		"counters=%s bounds=%s" % [str(scripts.counters), str(scripts.bounds_hit)])

	# Tick 2: a permanent area reveal makes it discovered, for that player only.
	scripts._set_script_active("Reveal", true)
	sim.tick()
	scripts.step(sim)
	_check("a_permanent_area_reveal_discovers_what_is_inside_it",
		int(scripts.counters.get("good_sees", 0)) == 1
			and int(scripts.counters.get("team_seen", 0)) == 1
			and scripts.permanent_reveals.has("Reveal Outfield"),
		"counters=%s reveals=%s" % [str(scripts.counters), str(scripts.permanent_reveals)])

	# Tick 3: undoing the handle re-shrouds it, an unknown handle is refused,
	# and a zero-radius reveal is refused rather than stored.
	scripts._set_script_active("Reveal", false)
	scripts._set_script_active("Unreveal", true)
	sim.tick()
	scripts.step(sim)
	_check("undoing_a_reveal_handle_re_shrouds_the_region",
		int(scripts.counters.get("good_sees", 0)) == 1
			and not scripts.permanent_reveals.has("Reveal Outfield")
			and not scripts.permanent_reveals.has("Zero Radius")
			and int(scripts.bounds_hit.get("unknown_reveal_handle", 0)) == 1
			and int(scripts.bounds_hit.get("reveal_radius_unreadable", 0)) == 1,
		"counters=%s bounds=%s" % [str(scripts.counters), str(scripts.bounds_hit)])

	# Tick 4: "<All Players>" expands to the mission's SidesList players.
	scripts._set_script_active("Unreveal", false)
	scripts._set_script_active("Reveal Far For All", true)
	sim.tick()
	scripts.step(sim)
	var far_reveal: Dictionary = scripts.permanent_reveals.get("Reveal Far", {})
	_check("all_players_expands_to_every_sideslist_player",
		int(scripts.counters.get("good_sees", 0)) == 2
			and Array(far_reveal.get("players", PackedStringArray())) == ["PlyrGood", "PlyrEvil"],
		"reveal=%s" % str(far_reveal))

	# A whole-map permanent reveal, and the sight of a player's own units. The
	# Captain is authored at the origin, 100 units of vision short of the
	# Raider, so only a unit created next to the Raider can see it.
	var sighted = MapScriptsScript.new()
	sighted.load_document(_world_document([
		_script("Post A Guard", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("CREATE_NAMED_ON_TEAM_AT_WAYPOINT", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Outpost"),
				_argument(MapScriptsScript.ARGUMENT_OBJECT_TYPE, 0, 0.0, SimScript.SOLDIER_OBJECT_ID),
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Far"),
			]),
		], true),
		_discovery_script("Good Sees Raider", "Raider", "PlyrGood", "good_sees"),
	]))
	sighted.player_team_bindings["PlyrGood"] = 0
	sighted.player_team_bindings["PlyrEvil"] = 1
	var sighted_sim = _make_sim()
	sighted_sim.tick()
	sighted.step(sighted_sim)
	_check("a_players_own_unit_sight_discovers_a_nearby_object",
		int(sighted.counters.get("good_sees", 0)) == 1
			and not sighted.named_object_state("Outpost").is_empty()
			and int(sighted.named_object_state("Outpost")["entity_id"]) >= 0,
		"counters=%s outpost=%s" % [
			str(sighted.counters), str(sighted.named_object_state("Outpost"))])

	var everything = MapScriptsScript.new()
	everything.load_document(_world_document([
		_inactive(_script("Reveal Everything", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("MAP_REVEAL_ALL_PERM", [
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "PlyrGood"),
			]),
		], true)),
		_inactive(_script("Shroud Everything", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("MAP_REVEAL_ALL_UNDO_PERM", [
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "PlyrGood"),
			]),
		], true)),
		_discovery_script("Good Sees The Far Corner", "Raider", "PlyrGood", "far_corner_seen"),
	]))
	everything.player_team_bindings["PlyrGood"] = 0
	everything.player_team_bindings["PlyrEvil"] = 1
	var everything_sim = _make_sim()
	# Tick 1: nothing revealed, so the far corner of the map is shrouded even to
	# the player who owns what stands there.
	everything_sim.tick()
	everything.step(everything_sim)
	var shrouded_before := int(everything.counters.get("far_corner_seen", 0)) == 0
	# Tick 2: the whole-map permanent reveal, filed before the predicate reads it.
	everything._set_script_active("Reveal Everything", true)
	everything_sim.tick()
	everything.step(everything_sim)
	var revealed_after_perm := bool(
		everything.permanently_revealed_players.get("PlyrGood", false))
	# Tick 3: the undo, likewise filed before the predicate reads it.
	everything._set_script_active("Shroud Everything", true)
	everything_sim.tick()
	everything.step(everything_sim)
	_check("a_whole_map_permanent_reveal_can_be_undone",
		shrouded_before
			and revealed_after_perm
			and int(everything.counters.get("far_corner_seen", 0)) == 1
			and not everything.permanently_revealed_players.has("PlyrGood"),
		"shrouded_before=%s counters=%s revealed=%s" % [
			str(shrouded_before), str(everything.counters),
			str(everything.permanently_revealed_players)])


func _test_registry_twin_run_determinism() -> void:
	# Registry state must be a pure function of the document and the tick, so
	# two interpreters fed the same mission stay bit-identical under lockstep.
	var payloads := [
		_script("Churn", [
			_or_condition([_condition("TEAM_HAS_UNITS", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
			])]),
			_action("NAMED_SET_ATTITUDE", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Captain"),
				_argument(MapScriptsScript.ARGUMENT_ATTITUDE, MapScriptsScript.ATTITUDE_ALERT),
			]),
			_action("MOVE_TEAM_TO", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Middle"),
			]),
			_action("UNIT_SET_TEAM", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Raider"),
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Guard Team"),
			]),
		], false),
		# B4: a reinforcement draw and a named creation every tick, so the
		# splitmix64 draw stream, the created-object ordinal and the order in
		# which entity ids are allocated all have to match tick for tick.
		_script("Reinforce", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("CREATE_REINFORCEMENT_TEAM", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Reinforcements"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Far"),
			]),
			_action("CREATE_NAMED_ON_TEAM_AT_WAYPOINT", [
				_argument(MapScriptsScript.ARGUMENT_UNIT_NAME, 0, 0.0, "Relief"),
				_argument(MapScriptsScript.ARGUMENT_OBJECT_TYPE, 0, 0.0,
					SimScript.SOLDIER_OBJECT_ID),
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Reinforcements"),
				_argument(MapScriptsScript.ARGUMENT_WAYPOINT, 0, 0.0, "Start"),
			]),
		], false),
		# B3: an area predicate over both halves of the spatial index.
		_script("Count The Outfield", [
			_or_condition([_condition("TEAM_INSIDE_AREA_PARTIALLY", [
				_argument(MapScriptsScript.ARGUMENT_TEAM, 0, 0.0, "Reinforcements"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Outfield"),
				_argument(MapScriptsScript.ARGUMENT_AREA_MEMBER_FILTER,
					MapScriptsScript.AREA_MEMBER_FILTER_ALL),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "outfield"),
			]),
		], false),
		_script("Good In The Courtyard", [
			_or_condition([_condition("SKIRMISH_PLAYER_HAS_UNITS_IN_AREA", [
				_argument(MapScriptsScript.ARGUMENT_PLAYER, 0, 0.0, "PlyrGood"),
				_argument(MapScriptsScript.ARGUMENT_TRIGGER_AREA, 0, 0.0, "Courtyard"),
			])]),
			_action("INCREMENT_COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 1),
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "courtyard"),
			]),
		], false),
		# B6: an audio wait that arms, retires and re-arms every 3 ticks, so the
		# pending-wait table has to advance identically on both peers.
		_audio_wait_script("Wait On Speech", "Briefing", "speech_done"),
		# Shroud: a permanent reveal filed and undone on alternate ticks, with a
		# discovery predicate reading it, so the reveal list, its iteration
		# order and the discovery answers all have to match tick for tick.
		_script("Reveal The Outfield", [
			_or_condition([_condition("COUNTER", [
				_argument(MapScriptsScript.ARGUMENT_COUNTER_NAME, 0, 0.0, "outfield"),
				_argument(MapScriptsScript.ARGUMENT_COMPARISON, MapScriptsScript.COMPARE_GREATER),
				_argument(MapScriptsScript.ARGUMENT_INTEGER, 0),
			])]),
			_reveal_area_action("Outfield", MapScriptsScript.PLAYER_ALL_REFERENCE, "Reveal Outfield"),
		], false),
		_discovery_script("Raider Is Seen", "Raider", "PlyrGood", "raider_seen"),
	]
	var a = MapScriptsScript.new()
	var b = MapScriptsScript.new()
	a.load_document(_world_document(payloads))
	b.load_document(_world_document(payloads))
	a.set_audio_event_durations({"Briefing": 0.3})
	b.set_audio_event_durations({"Briefing": 0.3})
	a.player_team_bindings["PlyrGood"] = 0
	b.player_team_bindings["PlyrGood"] = 0
	a.player_team_bindings["PlyrEvil"] = 1
	b.player_team_bindings["PlyrEvil"] = 1
	var sim_a = _make_sim()
	var sim_b = _make_sim()
	a.instantiate_world_objects(sim_a)
	b.instantiate_world_objects(sim_b)
	var divergence := -1
	for tick in range(1, 201):
		sim_a.tick()
		sim_b.tick()
		a.step(sim_a)
		b.step(sim_b)
		if divergence < 0 and (
			sim_a.state_hash() != sim_b.state_hash()
			or a.named_objects != b.named_objects
			or a.script_teams != b.script_teams
			or a.deferred_orders != b.deferred_orders
			or a.bounds_hit != b.bounds_hit
			or a.counters != b.counters
			or a.unavailable_object_types != b.unavailable_object_types
			or a.world_objects_instantiated != b.world_objects_instantiated
			or a._audio_waits != b._audio_waits
			or a.permanent_reveals != b.permanent_reveals
			or a.permanently_revealed_players != b.permanently_revealed_players
		):
			divergence = tick
	_check("registry_twin_run_is_deterministic_200_ticks", divergence < 0,
		"first_divergence=%d" % divergence)
	# The run has to have actually exercised creation and the area predicates,
	# or the proof above is vacuous.
	_check("the_twin_run_exercised_object_creation_and_area_queries",
		a.named_objects.size() > 200
			and int(a.counters.get("outfield", 0)) >= 199
			and int(a.counters.get("courtyard", 0)) >= 199,
		"objects=%d outfield=%d courtyard=%d" % [
			a.named_objects.size(),
			int(a.counters.get("outfield", 0)),
			int(a.counters.get("courtyard", 0))])
	# Likewise for the two features this tranche added: the audio wait has to
	# have completed and re-armed many times, and the discovery predicate has to
	# have answered true off the permanent reveal the run filed.
	_check("the_twin_run_exercised_audio_waits_and_discovery",
		int(a.counters.get("speech_done", 0)) >= 40
			and a.permanent_reveals.has("Reveal Outfield")
			and int(a.counters.get("raider_seen", 0)) >= 190
			and int(a.bounds_hit.get("discovery_queries", 0)) == 0,
		"speech_done=%d raider_seen=%d reveals=%d" % [
			int(a.counters.get("speech_done", 0)),
			int(a.counters.get("raider_seen", 0)),
			a.permanent_reveals.size()])
	# 200 ticks of reinforcements push the team roster past
	# MAX_TEAM_NAMED_MEMBERS. The overflow must be counted rather than grow the
	# allocation, and the twin run above must stay identical across it.
	_check("registry_stays_within_its_bounds",
		int(a.bounds_hit.get("named_objects", 0)) == 0
			and int(a.bounds_hit.get("script_teams", 0)) == 0
			and int(a.bounds_hit.get("created_objects", 0)) == 0
			and int(a.bounds_hit.get("area_queries", 0)) == 0
			and int(a.bounds_hit.get("area_query_scan", 0)) == 0
			and int(a.bounds_hit.get("team_named_members", 0)) > 0
			and Array(a.team_state("Reinforcements")["members"]).size()
				== MapScriptsScript.MAX_TEAM_NAMED_MEMBERS,
		"bounds=%s roster=%d" % [
			str(a.bounds_hit),
			Array(a.team_state("Reinforcements")["members"]).size()])


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
