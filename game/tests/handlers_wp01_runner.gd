extends SceneTree

## Proof runner for the handler-package convention and the WP01 pilot.
##
## Covers, in order:
##   1. registration   - deterministic scan order, one file per package,
##                       duplicate names are a loud error and never
##                       last-writer-wins
##   2. WP01 handlers  - counters from seconds, counters from world reads,
##                       subroutines, countdown display
##   3. positional args- the signatures in this package disagree about where the
##                       counter goes; a type-searching reader would pass some
##                       of these and silently fail others
##   4. honest refusal - a world read that cannot be answered leaves the counter
##                       ALONE and reports WORLD_REFUSED; it never writes 0
##   5. blocked packs  - WP02/WP03/WP04 are registered as blocked, produce a
##                       distinct gap reason, and are not counted as coverage
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp01_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")
const Env := preload("res://src/script/script_env.gd")
const WorldStub := preload("res://src/script/script_world_stub.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Wp02 := preload("res://src/script/handlers/wp02_blocked_fog_of_war.gd")
const Wp03 := preload("res://src/script/handlers/wp03_blocked_transport_garrison.gd")
const Wp04 := preload("res://src/script/handlers/wp04_blocked_misc_missing.gd")

const PLAYER := "<This Player>"

## The interpreter runs at 10 ticks per second. Every expected value below is a
## LITERAL derived from that and from the retail meaning of the action - never
## an expression rebuilt from the code under test. See the timer-unit commit:
## an assertion that recomputes the implementation's own formula agrees with it
## whatever the units are, and one of those hid a 1000x error indefinitely.
const TICKS_PER_SECOND := 10

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function
## on the spot without propagating, so every later _check() in that function
## silently never runs and `failed` never moves - the runner then prints a
## zero-failure result and exits 0 with SCRIPT ERROR lines above it. This is
## the exact count a HEALTHY run makes; if the run makes any other number,
## something aborted (or an assertion was added without updating this) and
## the result is not to be trusted.
const EXPECTED_CHECKS := 48

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration_is_deterministic()
	_test_duplicate_registration_is_loud()
	_test_counters_from_seconds()
	_test_counters_from_world_reads()
	_test_positional_argument_order()
	_test_refusal_leaves_the_counter_alone()
	_test_call_subroutine()
	_test_countdown_display_reaches_the_ui_sink()
	_test_blocked_packages()
	_report()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("HANDLERS_WP01 FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("HANDLERS_WP01_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(argument_type: int, integer: int = 0, real: float = 0.0, text: String = "") -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _counter_arg(name: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, name)


func _player_arg(name: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_PLAYER, 0, 0.0, name)


func _real_arg(value: float) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_REAL, 0, value)


func _compare_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COMPARISON, value)


func _text_arg(value: String) -> Dictionary:
	# Parameter types this repo has observed no code for are read from the text
	# field and their code is not asserted on.
	return _argument(99, 0, 0.0, value)


func _record(name: String, arguments: Array, inverted: bool = false) -> Dictionary:
	return {
		"contentType": 0,
		"internalName": {"name": name, "wireTypeCode": 3},
		"arguments": arguments,
		"enabled": true,
		"inverted": inverted,
	}


func _harness() -> Dictionary:
	## A dispatch carrying tranche 1 plus every auto-registered handler package,
	## which is how the real game will build it.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	var outcome := Registry.register_all(dispatch)
	var world := WorldStub.new(20260726)
	return {
		"dispatch": dispatch,
		"env": Env.new(),
		"world": world,
		"registration": outcome,
	}


func _act(harness: Dictionary, name: String, arguments: Array) -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


func _cond(harness: Dictionary, name: String, arguments: Array) -> bool:
	return (harness["dispatch"] as SageScriptDispatch).evaluate_condition(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


# --- 1. Registration ------------------------------------------------------


func _test_registration_is_deterministic() -> void:
	var paths := Registry.module_paths()
	var sorted_paths := paths.duplicate()
	sorted_paths.sort()
	_check("module_scan_is_sorted", paths == sorted_paths, str(paths))
	_check(
		"registry_infrastructure_is_not_scanned_as_a_package",
		not paths.has("res://src/script/handlers/_registry.gd"),
		str(paths)
	)

	var harness := _harness()
	var outcome: Dictionary = harness["registration"]
	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"every_module_declared_a_package",
		int(outcome["modules"]) == paths.size(),
		"modules=%d paths=%d" % [int(outcome["modules"]), paths.size()]
	)
	# Open-world on purpose. This runner is the registration pilot, not a census:
	# 17 handler packages are landing incrementally, so asserting an exact package
	# list would make every new package fail a test it has nothing to do with, and
	# the obvious "fix" is to keep editing this list - which teaches everyone to
	# edit assertions when they go red. Assert the tranche-1 four are present and
	# that the set is sorted (deterministic load order); later arrivals are fine.
	var packages: Array = outcome["packages"]
	var required := [
		"WP01-core-script-state",
		"WP02-blocked-fog-of-war",
		"WP03-blocked-transport-garrison",
		"WP04-blocked-misc-missing",
	]
	var absent_packages: Array = required.filter(func(name): return not packages.has(name))
	_check(
		"the_tranche_one_packages_are_present",
		absent_packages.is_empty(),
		"missing=%s present=%s" % [str(absent_packages), str(packages)]
	)
	var sorted_packages: Array = packages.duplicate()
	sorted_packages.sort()
	_check(
		"packages_register_in_deterministic_order",
		packages == sorted_packages,
		str(packages)
	)

	# Two runs must produce identical registration, or handler order could differ
	# between machines - which for a lockstep game is a desync waiting to happen.
	var twin := _harness()
	_check(
		"two_runs_register_identically",
		(harness["dispatch"] as SageScriptDispatch).implemented_actions()
		== (twin["dispatch"] as SageScriptDispatch).implemented_actions()
		and (harness["dispatch"] as SageScriptDispatch).blocked_names()
		== (twin["dispatch"] as SageScriptDispatch).blocked_names()
	)

	# Every WP01 member must actually be registered.
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var expected_actions := [
		"CALL_SUBROUTINE",
		"DISABLE_COUNTDOWN_TIMER_DISPLAY",
		"ENABLE_COUNTDOWN_TIMER_DISPLAY",
		"SET_COUNTER_IN_SECONDS",
		"SET_COUNTER_TO_NUMBER_OBJECTS_PLAYER_OWNES_WITH_MODELCONDITION",
		"SET_COUNTER_TO_THREAT_FINDER_THREAT",
		"SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER",
		"SET_PLAYER_COMMAND_POINTS_TOTAL_TO_COUNTER",
		"SET_PLAYER_COMMAND_POINTS_USED_TO_COUNTER",
		"SET_PLAYER_LIGHT_POINTS_TO_COUNTER",
		"SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER",
		"SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER_INCLUDE_DEAD",
		"SET_RANDOM_COUNTER_IN_SECONDS",
	]
	var missing: Array[String] = []
	for name: String in expected_actions:
		if not dispatch.action_handlers.has(name):
			missing.append(name)
	_check("every_wp01_action_is_registered", missing.is_empty(), str(missing))
	_check(
		"the_wp01_condition_is_registered",
		dispatch.condition_handlers.has("COUNTER_SECONDS")
	)


func _test_duplicate_registration_is_loud() -> void:
	# The convention's core promise: two packages claiming one name is an error
	# that names both, and the FIRST registration survives. If this silently
	# overwrote, a merge could swap a handler with no diff to show for it.
	var dispatch := Dispatch.new()
	var registrar := Registry.Registrar.new()
	registrar.dispatch = dispatch

	registrar.current_package = "WPAA-first"
	registrar.action("NO_OP", _first_handler)
	registrar.current_package = "WPBB-second"
	registrar.action("NO_OP", _second_handler)

	_check(
		"duplicate_registration_is_recorded_as_an_error",
		registrar.errors.size() == 1,
		str(registrar.errors)
	)
	_check(
		"duplicate_error_names_both_packages",
		registrar.errors.size() == 1
		and String(registrar.errors[0]).contains("WPAA-first")
		and String(registrar.errors[0]).contains("WPBB-second"),
		str(registrar.errors)
	)
	_check(
		"first_registration_wins_not_last",
		dispatch.action_handlers["NO_OP"] == Callable(self, "_first_handler"),
		"kept=%s" % str((dispatch.action_handlers["NO_OP"] as Callable).get_method())
	)

	# Colliding with tranche 1, which registers outside the package system, is
	# the same error rather than a quiet override.
	var core_dispatch := Dispatch.new()
	CoreHandlers.register_all(core_dispatch)
	var core_registrar := Registry.Registrar.new()
	core_registrar.dispatch = core_dispatch
	core_registrar.current_package = "WPCC-collides-with-core"
	core_registrar.action("SET_COUNTER", _first_handler)
	_check(
		"colliding_with_tranche_one_is_an_error",
		core_registrar.errors.size() == 1
		and String(core_registrar.errors[0]).contains("script_handlers_core.gd"),
		str(core_registrar.errors)
	)


func _first_handler(_ctx: Dictionary) -> int:
	return Dispatch.Status.OK


func _second_handler(_ctx: Dictionary) -> int:
	return Dispatch.Status.OK


# --- 2. Counters from a time in seconds -----------------------------------


func _test_counters_from_seconds() -> void:
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]

	# 3 seconds at 10 ticks per second is 30. Written out, not computed.
	_check(
		"set_counter_in_seconds_is_served",
		_act(harness, "SET_COUNTER_IN_SECONDS", [_counter_arg("c"), _real_arg(3.0)])
		== Dispatch.Status.OK
	)
	_check(
		"set_counter_in_seconds_stores_ticks",
		env.counter("c") == 30, "c=%d expected=30" % env.counter("c")
	)

	# The retail-scale value the timer-unit ruling turned on: half an hour.
	_act(harness, "SET_COUNTER_IN_SECONDS", [_counter_arg("daybreak"), _real_arg(1800.0)])
	_check(
		"half_hour_counter_is_eighteen_thousand_ticks",
		env.counter("daybreak") == 18000,
		"daybreak=%d expected=18000" % env.counter("daybreak")
	)

	# COUNTER_SECONDS converts its own operand the same way, so a counter set to
	# N seconds compares equal to N seconds. Round-tripping is the property that
	# actually matters to a map author.
	_check(
		"counter_seconds_round_trips_with_set_counter_in_seconds",
		_cond(harness, "COUNTER_SECONDS", [
			_counter_arg("c"), _compare_arg(ParamTypes.COMPARE_EQUAL), _real_arg(3.0)
		])
	)
	_check(
		"counter_seconds_respects_the_operator",
		_cond(harness, "COUNTER_SECONDS", [
			_counter_arg("c"), _compare_arg(ParamTypes.COMPARE_LESS), _real_arg(4.0)
		])
		and not _cond(harness, "COUNTER_SECONDS", [
			_counter_arg("c"), _compare_arg(ParamTypes.COMPARE_GREATER), _real_arg(4.0)
		])
	)

	# Deterministic random, in range, and converted to ticks.
	_act(harness, "SET_RANDOM_COUNTER_IN_SECONDS", [
		_counter_arg("r"), _real_arg(1.0), _real_arg(2.0)
	])
	var drawn := env.counter("r")
	var twin := _harness()
	_act(twin, "SET_RANDOM_COUNTER_IN_SECONDS", [
		_counter_arg("r"), _real_arg(1.0), _real_arg(2.0)
	])
	_check(
		"random_counter_in_seconds_is_deterministic_and_in_tick_range",
		drawn == (twin["env"] as SageScriptEnv).counter("r") and drawn >= 10 and drawn <= 20,
		"drawn=%d expected 10..20" % drawn
	)

	var no_random := _harness()
	(no_random["world"] as SageScriptWorldStub).refuse(SageScriptWorld.CAP_RANDOM)
	_check(
		"random_counter_in_seconds_refuses_without_a_random_stream",
		_act(no_random, "SET_RANDOM_COUNTER_IN_SECONDS", [
			_counter_arg("r"), _real_arg(1.0), _real_arg(2.0)
		]) == Dispatch.Status.WORLD_REFUSED
	)


# --- 3. Counters from a world read ----------------------------------------


func _test_counters_from_world_reads() -> void:
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]
	var world: SageScriptWorldStub = harness["world"]

	world.set_player_scalar(PLAYER, "command_points_available", 12)
	world.set_player_scalar(PLAYER, "command_points_total", 40)
	world.set_player_scalar(PLAYER, "command_points_used", 28)
	world.set_player_scalar(PLAYER, "light_points", 5)

	_act(harness, "SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER", [
		_player_arg(PLAYER), _counter_arg("available")
	])
	_act(harness, "SET_PLAYER_COMMAND_POINTS_TOTAL_TO_COUNTER", [
		_player_arg(PLAYER), _counter_arg("total")
	])
	_act(harness, "SET_PLAYER_COMMAND_POINTS_USED_TO_COUNTER", [
		_player_arg(PLAYER), _counter_arg("used")
	])
	_act(harness, "SET_PLAYER_LIGHT_POINTS_TO_COUNTER", [
		_player_arg(PLAYER), _counter_arg("light")
	])

	# Three near-identical actions differing only in which scalar they read. If
	# any two were wired to the same facet method the values would collide, so
	# distinct fixture values are the assertion.
	_check(
		"command_point_actions_read_their_own_scalar",
		env.counter("available") == 12
		and env.counter("total") == 40
		and env.counter("used") == 28,
		"available=%d total=%d used=%d" % [
			env.counter("available"), env.counter("total"), env.counter("used")
		]
	)
	_check("light_points_reach_the_counter", env.counter("light") == 5)


# --- 4. Positional argument order -----------------------------------------


func _test_positional_argument_order() -> void:
	# The trap this whole convention exists for. These signatures disagree about
	# where the counter lives, and two of them put two STRINGS next to each other
	# where a type-search cannot tell them apart at all.
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]
	var world: SageScriptWorldStub = harness["world"]

	# SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER(OBJECT_TYPE_LIST, PLAYER, COUNTER)
	# The fixture is keyed on (player, list) in that order; a handler that
	# swapped them would query ("OrcTypes", "<This Player>"), find no fixture,
	# and refuse - so an OK status here IS the ordering assertion.
	world.set_player_object_count(PLAYER, "OrcTypes", false, 9)
	world.set_player_object_count(PLAYER, "OrcTypes", true, 14)

	var alive := _act(harness, "SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER", [
		_text_arg("OrcTypes"), _player_arg(PLAYER), _counter_arg("orcs")
	])
	_check(
		"ownership_counter_reads_list_then_player_then_counter",
		alive == Dispatch.Status.OK and env.counter("orcs") == 9,
		"status=%d orcs=%d expected=9" % [alive, env.counter("orcs")]
	)

	# The INCLUDE_DEAD variant shares the signature and differs only in the flag.
	_act(harness, "SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER_INCLUDE_DEAD", [
		_text_arg("OrcTypes"), _player_arg(PLAYER), _counter_arg("orcs_with_dead")
	])
	_check(
		"include_dead_variant_passes_the_flag_through",
		env.counter("orcs_with_dead") == 14,
		"orcs_with_dead=%d expected=14" % env.counter("orcs_with_dead")
	)

	# SET_COUNTER_TO_NUMBER_OBJECTS_..._WITH_MODELCONDITION(PLAYER, MODEL_CONDITION, COUNTER)
	# Player FIRST here - the opposite of the action above, with the same three
	# argument types involved.
	world.set_player_model_condition_count(PLAYER, "RUBBLE", 4)
	var model_status := _act(
		harness, "SET_COUNTER_TO_NUMBER_OBJECTS_PLAYER_OWNES_WITH_MODELCONDITION",
		[_player_arg(PLAYER), _text_arg("RUBBLE"), _counter_arg("rubble")]
	)
	_check(
		"model_condition_counter_reads_player_then_condition_then_counter",
		model_status == Dispatch.Status.OK and env.counter("rubble") == 4,
		"status=%d rubble=%d expected=4" % [model_status, env.counter("rubble")]
	)

	# SET_COUNTER_TO_THREAT_FINDER_THREAT(COUNTER, THREAT_FINDER, THREAT_TYPE, PLAYER)
	# Counter FIRST and player LAST - the reverse of the command-point actions.
	world.set_player_scalar(PLAYER, "threat_at|Finder_01|Ground", 77)
	var threat_status := _act(harness, "SET_COUNTER_TO_THREAT_FINDER_THREAT", [
		_counter_arg("threat"), _text_arg("Finder_01"), _text_arg("Ground"), _player_arg(PLAYER)
	])
	_check(
		"threat_finder_reads_counter_first_and_player_last",
		threat_status == Dispatch.Status.OK and env.counter("threat") == 77,
		"status=%d threat=%d expected=77" % [threat_status, env.counter("threat")]
	)

	# Arity is enforced before the handler runs, so a signature drift cannot be
	# absorbed silently by positional reads.
	_check(
		"short_argument_list_is_rejected",
		_act(harness, "SET_COUNTER_TO_THREAT_FINDER_THREAT", [_counter_arg("threat")])
		== Dispatch.Status.BAD_ARGUMENTS
	)


# --- 5. Honest refusal ----------------------------------------------------


func _test_refusal_leaves_the_counter_alone() -> void:
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]
	var world: SageScriptWorldStub = harness["world"]
	# The point of this test IS the refusal path, so quiet refusals are asked
	# for explicitly. The refusal still lands in refusal_log and is asserted on.
	world.expect_refusals()

	env.set_counter("cp", 7)
	var status := _act(harness, "SET_PLAYER_COMMAND_POINTS_USED_TO_COUNTER", [
		_player_arg("Nobody"), _counter_arg("cp")
	])
	_check("unanswerable_read_reports_world_refused", status == Dispatch.Status.WORLD_REFUSED)
	_check(
		"unanswerable_read_does_not_write_a_zero",
		env.counter("cp") == 7,
		"cp=%d expected=7 (untouched)" % env.counter("cp")
	)
	_check(
		"refusal_identifies_the_missing_world_method",
		world.refused("players.command_points_used"),
		str(world.refusal_log)
	)
	_check(
		"refusal_is_recorded_as_a_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.has(
			"action", "SET_PLAYER_COMMAND_POINTS_USED_TO_COUNTER",
			GapLog.REASON_WORLD_REFUSED
		),
		str((harness["dispatch"] as SageScriptDispatch).gaps.to_lines())
	)


# --- 6. Subroutines -------------------------------------------------------


var _subroutines_called: Array[String] = []


func _test_call_subroutine() -> void:
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]

	# With no runner installed the call is UNSUPPORTED and recorded, rather than
	# quietly queued for a consumer that may never exist.
	var unserved := _act(harness, "CALL_SUBROUTINE", [_text_arg("Wave Two")])
	_check("call_subroutine_without_a_runner_is_unsupported",
		unserved == Dispatch.Status.UNSUPPORTED)
	_check(
		"call_subroutine_without_a_runner_records_a_gap",
		(harness["dispatch"] as SageScriptDispatch).gaps.has(
			"action", "CALL_SUBROUTINE", GapLog.REASON_UNIMPLEMENTED
		)
	)

	env.subroutine_runner = _run_subroutine
	_subroutines_called.clear()
	var served := _act(harness, "CALL_SUBROUTINE", [_text_arg("Wave Two")])
	_check("call_subroutine_reaches_the_installed_runner",
		served == Dispatch.Status.OK and _subroutines_called == ["Wave Two"],
		str(_subroutines_called))

	# A runner that does not know the subroutine is an honest failure, not OK.
	var missing := _act(harness, "CALL_SUBROUTINE", [_text_arg("No Such Script")])
	_check("unknown_subroutine_is_unsupported", missing == Dispatch.Status.UNSUPPORTED)


func _run_subroutine(name: String) -> bool:
	if name == "No Such Script":
		return false
	_subroutines_called.append(name)
	return true


# --- 7. Presentation sink -------------------------------------------------


func _test_countdown_display_reaches_the_ui_sink() -> void:
	var harness := _harness()
	var world: SageScriptWorldStub = harness["world"]

	_check(
		"enable_countdown_display_is_served",
		_act(harness, "ENABLE_COUNTDOWN_TIMER_DISPLAY", []) == Dispatch.Status.OK
	)
	_act(harness, "DISABLE_COUNTDOWN_TIMER_DISPLAY", [])

	var sink := world.ui() as SageScriptWorldStub.RecordingSink
	_check(
		"countdown_display_is_recorded_on_the_ui_channel",
		sink.ops() == ["ENABLE_COUNTDOWN_TIMER_DISPLAY", "DISABLE_COUNTDOWN_TIMER_DISPLAY"],
		str(sink.ops())
	)
	_check(
		"recorded_op_carries_its_argument",
		sink.values_for("ENABLE_COUNTDOWN_TIMER_DISPLAY") == [true]
		and sink.values_for("DISABLE_COUNTDOWN_TIMER_DISPLAY") == [false],
		"%s %s" % [
			str(sink.values_for("ENABLE_COUNTDOWN_TIMER_DISPLAY")),
			str(sink.values_for("DISABLE_COUNTDOWN_TIMER_DISPLAY")),
		]
	)
	_check(
		"presentation_did_not_touch_the_simulation",
		(harness["env"] as SageScriptEnv).snapshot()["counters"] == {}
		and (harness["env"] as SageScriptEnv).snapshot()["flags"] == {}
	)

	# A world with no UI adapter must refuse into a gap, not silently succeed.
	# The BASE SageScriptWorld is exactly that world.
	var bare := Dispatch.new()
	Registry.register_all(bare)
	var bare_status := bare.execute_action(
		_record("ENABLE_COUNTDOWN_TIMER_DISPLAY", []), Env.new(), SageScriptWorld.new(), "fixture"
	)
	_check(
		"a_world_without_a_ui_sink_refuses_rather_than_pretending",
		bare_status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % bare_status
	)


# --- 8. Blocked packages --------------------------------------------------


func _test_blocked_packages() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var expected_blocked := (
		Wp02.BLOCKED_ACTIONS.size()
		+ Wp03.BLOCKED_ACTIONS.size() + Wp03.BLOCKED_CONDITIONS.size()
		+ Wp04.BLOCKED_ACTIONS.size() + Wp04.BLOCKED_CONDITIONS.size()
	)
	# Assert the tranche-1 blocked members are ALL registered, not that they are
	# the only ones. Later packages gap-register their own blocked members, so a
	# `== 32` equality turns every future package into a false failure here.
	# The property that matters is that nothing in WP02/03/04 goes missing.
	_check(
		"every_tranche_one_blocked_member_is_registered",
		dispatch.blocked_names().size() >= expected_blocked and expected_blocked == 32,
		"registered=%d tranche_one_expected=%d" % [
			dispatch.blocked_names().size(), expected_blocked
		]
	)
	var tranche_one_blocked: Array = (
		Wp02.BLOCKED_ACTIONS
		+ Wp03.BLOCKED_ACTIONS + Wp03.BLOCKED_CONDITIONS
		+ Wp04.BLOCKED_ACTIONS + Wp04.BLOCKED_CONDITIONS
	)
	var absent: Array = tranche_one_blocked.filter(
		func(name): return not dispatch.blocked_names().has(name)
	)
	_check(
		"no_tranche_one_blocked_member_went_missing",
		absent.is_empty(),
		"absent=%s" % str(absent)
	)

	var status := _act(harness, "MAP_SHROUD_ALL", [_player_arg(PLAYER)])
	_check("a_blocked_action_reports_blocked", status == Dispatch.Status.BLOCKED,
		"status=%d" % status)
	_check(
		"a_blocked_action_records_its_own_gap_reason",
		dispatch.gaps.has("action", "MAP_SHROUD_ALL", GapLog.REASON_BLOCKED_SUBSYSTEM)
		and not dispatch.gaps.has("action", "MAP_SHROUD_ALL", GapLog.REASON_UNIMPLEMENTED),
		str(dispatch.gaps.to_lines())
	)
	_check(
		"the_blocked_gap_names_the_missing_subsystem",
		String(
			dispatch.gaps.entries[
				"action|MAP_SHROUD_ALL|%s" % GapLog.REASON_BLOCKED_SUBSYSTEM
			]["detail"]
		).contains("fog of war"),
		str(dispatch.gaps.to_lines())
	)

	# A blocked condition is FALSE, like any condition that could not be
	# evaluated - an unevaluable gate must never fire.
	_check(
		"a_blocked_condition_is_false",
		not _cond(harness, "UNIT_HAS_PASSENGER", [_text_arg("Gimli")])
	)

	# And none of them count as coverage.
	var overlap: Array[String] = []
	for name: String in dispatch.blocked_names():
		if dispatch.implemented_actions().has(name) or dispatch.implemented_conditions().has(name):
			overlap.append(name)
	_check("blocked_members_are_not_counted_as_implemented", overlap.is_empty(), str(overlap))
	_check(
		"coverage_reports_the_blocked_set_separately",
		# >= 32, not == 32: the point is that blocked members are counted in their
		# own bucket rather than inflating implemented coverage. Pinning the total
		# would make this fail every time a later package registers a blocked
		# member, which is exactly the behaviour we want it to keep doing.
		(dispatch.coverage()["blocked_on_subsystem"] as Array).size() >= 32
	)


# --- Reporting ------------------------------------------------------------


func _report() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var coverage := dispatch.coverage()
	print(
		"HANDLERS_WP01 COVERAGE actions implemented=%d conditions implemented=%d blocked=%d deliberate=%d"
		% [
			int((coverage["actions"] as Dictionary)["implemented"]),
			int((coverage["conditions"] as Dictionary)["implemented"]),
			(coverage["blocked_on_subsystem"] as Array).size(),
			(coverage["deliberately_refused"] as Array).size(),
		]
	)
	print("HANDLERS_WP01 PACKAGES %s" % str((harness["registration"] as Dictionary)["packages"]))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP01 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP01 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
