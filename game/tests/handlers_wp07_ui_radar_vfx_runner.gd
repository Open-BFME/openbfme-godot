extends SceneTree

## Proof runner for WP07-ui-radar-vfx.
##
## The package is 51 actions that all do the same thing - describe something to
## the UI presentation sink - plus 7 members declared blocked. That shape means
## the interesting failures are not "does it work" but:
##
##   1. does every name reach the sink AT ALL, on the right channel, under its
##      own op name
##   2. does every name emit exactly as many values as its signature declares
##      (the 51 handlers share six bodies by signature shape; a name registered
##      against the wrong body still returns OK and still records something -
##      it just quietly emits the wrong number of values)
##   3. are the values in DECLARATION ORDER, read from the right field, for the
##      signatures where a type-search would look correct
##   4. do the blocked members refuse loudly instead of emitting
##   5. do the three blocked conditions stay false even when inverted, and get
##      recorded rather than answered
##
## Test 2 is the load-bearing one and is driven off the vocabulary's own
## signature table, so it covers all 51 names without 51 hand-written cases.
## Tests 3 and 5 are hand-written literals, because a sweep that derives its
## expectation from the same table as the code cannot catch an ordering bug.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp07_ui_radar_vfx_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")
const Env := preload("res://src/script/script_env.gd")
const WorldStub := preload("res://src/script/script_world_stub.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Wp07 := preload("res://src/script/handlers/wp07_ui_radar_vfx.gd")

## Every action WP07 SERVES. Written out rather than derived from the module, so
## that deleting a registration fails this file instead of shrinking the
## expectation with it.
const SERVED_ACTIONS := [
	"CAMEO_FLASH",
	"CLOSE_OBJECTIVES_SCREEN",
	"COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT",
	"COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE",
	"DIM_WORLD_LIGHTS",
	"DISABLE_INPUT",
	"DISABLE_OBJECTIVES_SCREEN",
	"DISABLE_PLANNING_MODE",
	"DISABLE_SPELL_STORE",
	"DISPLAY_COUNTDOWN_TIMER",
	"DISPLAY_COUNTER",
	"DISPLAY_NOTIFICATION_BOX",
	"DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE",
	"DISPLAY_TEXT",
	"ENABLE_HOUSE_COLOR",
	"ENABLE_INPUT",
	"ENABLE_OBJECTIVES_SCREEN",
	"ENABLE_PLANNING_MODE",
	"ENABLE_SPELL_STORE",
	"EVA_SET_ENABLED_DISABLED",
	"FLASH_OBJECTIVES_BUTTON",
	"FLASH_PLANNING_MODE_BUTTON",
	"FLASH_SPELL_STORE_BUTTON",
	"HERO_SELECT_BUTTON_FLASH",
	"HIDE_COUNTDOWN_TIMER",
	"HIDE_COUNTER",
	"HIDE_MISSION_OBJECTIVE",
	"MARK_MISSION_OBJECTIVE_COMPLETED",
	"MARK_MISSION_OBJECTIVE_NOT_COMPLETED",
	"NAMED_CUSTOM_COLOR",
	"NAMED_FLASH",
	"NAMED_FLASH_WHITE",
	"OBJECT_CREATE_RADAR_EVENT",
	"OBJECT_FORCE_SELECT",
	"OPTIONS_SET_DRAWICON_UI_MODE",
	"OPTIONS_SET_OCCLUSION_MODE",
	"OPTIONS_SET_PARTICLE_CAP_MODE",
	"RADAR_CREATE_EVENT",
	"RADAR_DISABLE",
	"RADAR_ENABLE",
	"RADAR_FORCE_ENABLE",
	"RADAR_REVERT_TO_NORMAL",
	"REFRESH_RADAR",
	"RESTORE_WORLD_LIGHTS",
	"SELECT_BUILDER_BUTTON_FLASH",
	"SET_TREE_SWAY",
	"SHOW_MILITARY_CAPTION",
	"SHOW_MISSION_OBJECTIVE",
	"TEAM_CREATE_RADAR_EVENT",
	"TEAM_FLASH",
	"TEAM_FLASH_WHITE",
]

## The sixteen zero-argument actions, which must emit [] and not a synthesised
## stand-in value.
const NO_ARGUMENT_ACTIONS := [
	"CLOSE_OBJECTIVES_SCREEN",
	"DIM_WORLD_LIGHTS",
	"DISABLE_INPUT",
	"DISABLE_OBJECTIVES_SCREEN",
	"DISABLE_PLANNING_MODE",
	"DISABLE_SPELL_STORE",
	"ENABLE_INPUT",
	"ENABLE_OBJECTIVES_SCREEN",
	"ENABLE_PLANNING_MODE",
	"ENABLE_SPELL_STORE",
	"RADAR_DISABLE",
	"RADAR_ENABLE",
	"RADAR_FORCE_ENABLE",
	"RADAR_REVERT_TO_NORMAL",
	"REFRESH_RADAR",
	"RESTORE_WORLD_LIGHTS",
]

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_every_member_is_registered()
	_test_every_served_action_reaches_the_sink_with_its_full_signature()
	_test_zero_argument_actions_emit_nothing()
	_test_values_are_positional_and_literal()
	_test_enum_and_position_arguments_read_the_right_field()
	_test_durations_are_passed_through_unconverted()
	_test_presentation_does_not_touch_the_simulation()
	_test_arity_is_enforced()
	_test_a_world_without_a_ui_sink_refuses()
	_test_the_match_outcome_family_is_blocked()
	_test_the_ui_state_conditions_are_blocked()
	_report()
	print("HANDLERS_WP07_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _argument(
	argument_type: int, integer: int = 0, real: float = 0.0, text: String = ""
) -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _int_arg(value: int) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_INTEGER, value)


func _real_arg(value: float) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_REAL, 0, value)


func _bool_arg(value: bool) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_BOOLEAN, 1 if value else 0)


func _counter_arg(name: String) -> Dictionary:
	return _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, name)


func _text_arg(value: String) -> Dictionary:
	# Parameter types this repo has observed no SCB code for are read from the
	# text field and their code is not asserted on by SageScriptArgs.validate().
	return _argument(99, 0, 0.0, value)


func _enum_arg(value: int) -> Dictionary:
	# RADAR_EVENT / COLOR: no observed SCB code, payload is the integer.
	return _argument(99, value)


func _position_arg(value: Vector3) -> Dictionary:
	return {
		"argumentType": ParamTypes.ARGUMENT_POSITION,
		"position": [value.x, value.y, value.z],
	}


func _synthetic_arg(param: String, index: int) -> Dictionary:
	## One argument of the declared type, for the all-names sweep. Every field
	## is populated with a DIFFERENT value so that a handler reading the wrong
	## field of the right slot is still visible in the recording.
	if param == "COORD3D":
		return _position_arg(Vector3(float(index), float(index) + 0.5, float(index) + 0.25))
	var code := ParamTypes.argument_type_for_param(param)
	if code == ParamTypes.UNKNOWN_CODE:
		code = 99
	return _argument(code, 100 + index, float(index) + 0.5, "arg%d" % index)


func _synthetic_arguments(name: String) -> Array:
	var params: Array = Vocabulary.action_by_name(name).get("params", [])
	var out: Array = []
	for index in range(params.size()):
		out.append(_synthetic_arg(String(params[index]), index))
	return out


func _record(name: String, arguments: Array, inverted: bool = false) -> Dictionary:
	return {
		"contentType": 0,
		"internalName": {"name": name, "wireTypeCode": 3},
		"arguments": arguments,
		"enabled": true,
		"inverted": inverted,
	}


func _harness() -> Dictionary:
	## Tranche 1 plus every auto-registered handler package, which is how the
	## real game builds it.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	var outcome := Registry.register_all(dispatch)
	return {
		"dispatch": dispatch,
		"env": Env.new(),
		"world": WorldStub.new(20260726),
		"registration": outcome,
	}


func _act(harness: Dictionary, name: String, arguments: Array) -> int:
	return (harness["dispatch"] as SageScriptDispatch).execute_action(
		_record(name, arguments), harness["env"], harness["world"], "fixture"
	)


func _cond(harness: Dictionary, name: String, inverted: bool = false) -> bool:
	return (harness["dispatch"] as SageScriptDispatch).evaluate_condition(
		_record(name, [], inverted), harness["env"], harness["world"], "fixture"
	)


func _sink(harness: Dictionary) -> SageScriptWorldStub.RecordingSink:
	return (harness["world"] as SageScriptWorldStub).ui() as SageScriptWorldStub.RecordingSink


func _emitted(harness: Dictionary, name: String, arguments: Array) -> Array:
	## Dispatches one action into a clean sink and returns what it recorded.
	## Returns [] for a refusal, which the caller's expectation will not match.
	var sink := _sink(harness)
	sink.clear()
	if _act(harness, name, arguments) != Dispatch.Status.OK:
		return []
	return sink.values_for(name)


# --- 1. Registration ------------------------------------------------------


func _test_every_member_is_registered() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var outcome: Dictionary = harness["registration"]

	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"wp07_is_present_as_a_package",
		(outcome["packages"] as Array).has("WP07-ui-radar-vfx"),
		str(outcome["packages"])
	)

	# 51 served + 4 blocked actions + 3 blocked conditions = the 58 members.
	_check(
		"the_package_covers_all_fifty_eight_members",
		SERVED_ACTIONS.size() == 51
		and Wp07.BLOCKED_OUTCOME_ACTIONS.size() == 4
		and Wp07.BLOCKED_UI_STATE_CONDITIONS.size() == 3,
		"served=%d outcome=%d conditions=%d"
		% [
			SERVED_ACTIONS.size(),
			Wp07.BLOCKED_OUTCOME_ACTIONS.size(),
			Wp07.BLOCKED_UI_STATE_CONDITIONS.size(),
		]
	)

	var missing: Array[String] = []
	for name: String in SERVED_ACTIONS:
		if not dispatch.action_handlers.has(name):
			missing.append(name)
	_check("every_served_action_has_a_handler", missing.is_empty(), str(missing))

	# Served names count as coverage; blocked ones must not.
	var uncounted: Array[String] = []
	for name: String in SERVED_ACTIONS:
		if not dispatch.implemented_actions().has(name):
			uncounted.append(name)
	_check("served_actions_are_counted_as_coverage", uncounted.is_empty(), str(uncounted))

	var miscounted: Array[String] = []
	for name: String in Wp07.BLOCKED_OUTCOME_ACTIONS:
		if dispatch.implemented_actions().has(name):
			miscounted.append(name)
	for name: String in Wp07.BLOCKED_UI_STATE_CONDITIONS:
		if dispatch.implemented_conditions().has(name):
			miscounted.append(name)
	_check(
		"blocked_members_are_never_counted_as_coverage", miscounted.is_empty(), str(miscounted)
	)

	# A served name that is also blocked would mean the file registered it
	# twice; the registry would have logged an error, but assert the outcome.
	var both: Array[String] = []
	for name: String in Wp07.BLOCKED_OUTCOME_ACTIONS:
		if SERVED_ACTIONS.has(name):
			both.append(name)
	_check("no_member_is_both_served_and_blocked", both.is_empty(), str(both))


# --- 2. Every name reaches the sink, with its whole signature -------------


func _test_every_served_action_reaches_the_sink_with_its_full_signature() -> void:
	## The load-bearing test. Six shared bodies serve 51 names; the failure this
	## catches is a name registered against the wrong body, which still returns
	## OK and still records - it just emits the wrong number of values.
	var harness := _harness()
	var sink := _sink(harness)

	var not_served: Array[String] = []
	var wrong_channel: Array[String] = []
	var wrong_arity: Array[String] = []
	var never_recorded: Array[String] = []

	for name: String in SERVED_ACTIONS:
		sink.clear()
		var params: Array = Vocabulary.action_by_name(name).get("params", [])
		if _act(harness, name, _synthetic_arguments(name)) != Dispatch.Status.OK:
			not_served.append(name)
			continue
		if sink.count_for(name) != 1:
			never_recorded.append("%s x%d" % [name, sink.count_for(name)])
			continue
		if String((sink.records[0] as Dictionary)["channel"]) != "ui":
			wrong_channel.append(
				"%s -> %s" % [name, String((sink.records[0] as Dictionary)["channel"])]
			)
		var values: Array = sink.values_for(name)
		if values.size() != params.size():
			wrong_arity.append(
				"%s emitted %d of %d" % [name, values.size(), params.size()]
			)

	_check("every_served_action_is_served", not_served.is_empty(), str(not_served))
	_check(
		"every_served_action_records_exactly_once_under_its_own_name",
		never_recorded.is_empty(),
		str(never_recorded)
	)
	_check("every_recording_is_on_the_ui_channel", wrong_channel.is_empty(), str(wrong_channel))
	_check(
		"every_action_emits_one_value_per_declared_parameter",
		wrong_arity.is_empty(),
		str(wrong_arity)
	)


# --- 3. Zero-argument actions ---------------------------------------------


func _test_zero_argument_actions_emit_nothing() -> void:
	# An empty signature emits []. Not [true], not [the op name]: the sink
	# contract forbids defaults filled in, and a stand-in value here would be
	# indistinguishable from a real argument to the presentation adapter.
	var harness := _harness()
	var sink := _sink(harness)
	var offenders: Array[String] = []
	for name: String in NO_ARGUMENT_ACTIONS:
		sink.clear()
		_act(harness, name, [])
		if sink.values_for(name) != []:
			offenders.append("%s -> %s" % [name, str(sink.values_for(name))])
	_check(
		"zero_argument_actions_emit_an_empty_value_list",
		offenders.is_empty(),
		str(offenders)
	)

	# And they are still distinguishable from one another, because the op name
	# travels even when the value list does not.
	sink.clear()
	_act(harness, "RADAR_ENABLE", [])
	_act(harness, "RADAR_DISABLE", [])
	_act(harness, "REFRESH_RADAR", [])
	_check(
		"one_shared_body_still_records_distinct_op_names",
		sink.ops() == ["RADAR_ENABLE", "RADAR_DISABLE", "REFRESH_RADAR"],
		str(sink.ops())
	)


# --- 4. Positional order, hand-written -------------------------------------


func _test_values_are_positional_and_literal() -> void:
	## Every expectation below is a LITERAL. A sweep that rebuilds its
	## expectation from the same signature table the handler reads would agree
	## with the handler whatever order it used.
	var harness := _harness()

	# CAMEO_FLASH(COMMAND_BUTTON, INT) - name first, seconds second.
	_check(
		"cameo_flash_emits_button_then_seconds",
		_emitted(harness, "CAMEO_FLASH", [_text_arg("Command_ArrowVolley"), _int_arg(3)])
		== ["Command_ArrowVolley", 3],
		str(_emitted(harness, "CAMEO_FLASH", [_text_arg("Command_ArrowVolley"), _int_arg(3)]))
	)

	# DISPLAY_COUNTER(COUNTER, LOCALIZED_TEXT) - two strings, and the counter is
	# first. Reversed, this still looks like a plausible recording.
	_check(
		"display_counter_emits_counter_then_label",
		_emitted(
			harness, "DISPLAY_COUNTER",
			[_counter_arg("reinforcements"), _text_arg("OBJ:Reinforcements")]
		)
		== ["reinforcements", "OBJ:Reinforcements"]
	)

	# COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT(COMMAND_BUTTON, OBJECT_TYPE, INT)
	# Two adjacent strings that only an index can tell apart.
	_check(
		"commandbar_add_emits_button_then_type_then_slot",
		_emitted(
			harness, "COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT",
			[_text_arg("Command_Sword"), _text_arg("GondorSoldier"), _int_arg(7)]
		)
		== ["Command_Sword", "GondorSoldier", 7]
	)

	# DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE
	#   (NOTIFICATION_BOX_TYPE, LOCALIZED_TEXT, INT, OBJECT_TYPE)
	# Three strings around one int, with the third string AFTER the int. A
	# reader that gathered strings then numbers would emit the same four values
	# in the order ["Warning", "OBJ:Ambush", "MordorOrc", 0] and look fine.
	var notification := _emitted(
		harness, "DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE",
		[_text_arg("Warning"), _text_arg("OBJ:Ambush"), _int_arg(0), _text_arg("MordorOrc")]
	)
	_check(
		"notification_with_image_keeps_the_int_in_the_middle",
		notification == ["Warning", "OBJ:Ambush", 0, "MordorOrc"],
		str(notification)
	)

	# The zero-second variant means "indefinite", so a 0 must survive as a
	# value rather than being treated as absent.
	_check(
		"a_zero_duration_is_emitted_not_dropped",
		notification.size() == 4 and int(notification[2]) == 0,
		str(notification)
	)

	# SET_TREE_SWAY(ANGLE, ANGLE, ANGLE, INT, REAL) - three reals in a row that
	# all mean different things, then an int, then another real.
	var sway := _emitted(
		harness, "SET_TREE_SWAY",
		[_real_arg(45.0), _real_arg(12.5), _real_arg(3.25), _int_arg(30), _real_arg(0.75)]
	)
	_check(
		"set_tree_sway_keeps_its_three_angles_in_order",
		sway == [45.0, 12.5, 3.25, 30, 0.75],
		str(sway)
	)

	# OBJECT_FORCE_SELECT(TEAM, OBJECT_TYPE, BOOLEAN, DIALOG) - the boolean sits
	# between two strings and is followed by a third.
	_check(
		"object_force_select_emits_all_four_in_order",
		_emitted(
			harness, "OBJECT_FORCE_SELECT",
			[
				_text_arg("StrikeTeam"), _text_arg("GondorSoldier"),
				_bool_arg(true), _text_arg("DIALOG:Advance"),
			]
		)
		== ["StrikeTeam", "GondorSoldier", true, "DIALOG:Advance"]
	)

	# SHOW_MILITARY_CAPTION(LOCALIZED_TEXT, REAL) - the only string+real pair.
	_check(
		"military_caption_emits_text_then_real_seconds",
		_emitted(
			harness, "SHOW_MILITARY_CAPTION", [_text_arg("OBJ:Briefing"), _real_arg(7.5)]
		)
		== ["OBJ:Briefing", 7.5]
	)

	# BOOLEAN false must arrive as false, not as "absent".
	_check(
		"a_false_boolean_is_emitted_as_false",
		_emitted(harness, "EVA_SET_ENABLED_DISABLED", [_bool_arg(false)]) == [false]
	)
	_check(
		"a_true_boolean_is_emitted_as_true",
		_emitted(harness, "OPTIONS_SET_OCCLUSION_MODE", [_bool_arg(true)]) == [true]
	)

	# Single-argument shapes.
	_check(
		"display_text_emits_its_one_string",
		_emitted(harness, "DISPLAY_TEXT", [_text_arg("OBJ:Hello")]) == ["OBJ:Hello"]
	)
	_check(
		"hide_counter_emits_the_counter_name",
		_emitted(harness, "HIDE_COUNTER", [_counter_arg("reinforcements")])
		== ["reinforcements"]
	)
	_check(
		"mission_objective_actions_emit_their_index",
		_emitted(harness, "MARK_MISSION_OBJECTIVE_COMPLETED", [_int_arg(2)]) == [2]
	)


# --- 5. Field selection ----------------------------------------------------


func _test_enum_and_position_arguments_read_the_right_field() -> void:
	## RADAR_EVENT and COLOR are numeric in the reference. Both were missing
	## from ParamTypes.PAYLOAD_FIELD_FOR_PARAM when this package was written,
	## which would have defaulted them to the text field; 45b0b43 added the
	## rows. These assertions pin the recording to the NUMBER, and the shared
	## table to the rows this package depends on - a revert of either fails
	## here rather than silently emptying a value, which is the whole failure
	## mode, since sage_scb.py fills every field so a wrong read yields "".
	var harness := _harness()

	_check(
		"the_shared_table_reads_radar_event_and_colour_as_integers",
		ParamTypes.payload_field_for_param("RADAR_EVENT") == "integer"
		and ParamTypes.payload_field_for_param("COLOR") == "integer",
		"RADAR_EVENT=%s COLOR=%s"
		% [
			ParamTypes.payload_field_for_param("RADAR_EVENT"),
			ParamTypes.payload_field_for_param("COLOR"),
		]
	)
	# An enum is a named integer, so every ENUMS member needs a payload row or
	# it silently falls through to text. WP07 only uses RADAR_EVENT, but the
	# invariant is cheap to assert and the bug it catches is invisible.
	_check(
		"every_parameter_enum_still_has_a_payload_row",
		(ParamTypes.enums_missing_payload_rows() as Array).is_empty(),
		str(ParamTypes.enums_missing_payload_rows())
	)

	# RADAR_EVENT: UnderAttack = 3.
	var radar := _emitted(
		harness, "OBJECT_CREATE_RADAR_EVENT", [_text_arg("Gandalf"), _enum_arg(3)]
	)
	_check(
		"radar_event_is_emitted_as_its_enum_integer",
		radar == ["Gandalf", 3] and typeof(radar[1]) == TYPE_INT,
		str(radar)
	)
	_check(
		"team_radar_event_reads_the_team_then_the_enum",
		_emitted(harness, "TEAM_CREATE_RADAR_EVENT", [_text_arg("StrikeTeam"), _enum_arg(5)])
		== ["StrikeTeam", 5]
	)

	# COLOR: the reference's packed values. blue = 255.
	var colored := _emitted(
		harness, "NAMED_CUSTOM_COLOR", [_text_arg("Gandalf"), _enum_arg(255)]
	)
	_check(
		"custom_color_is_emitted_as_its_packed_integer",
		colored == ["Gandalf", 255] and typeof(colored[1]) == TYPE_INT,
		str(colored)
	)
	# white = -1: a negative packed colour must survive as a negative number.
	_check(
		"a_negative_packed_colour_survives",
		_emitted(harness, "NAMED_CUSTOM_COLOR", [_text_arg("Gandalf"), _enum_arg(-1)])
		== ["Gandalf", -1]
	)

	# COORD3D carries three floats and NO integer/real/text payload at all, so
	# reading it from any other field yields "" - which would still record.
	var event := _emitted(
		harness, "RADAR_CREATE_EVENT",
		[_position_arg(Vector3(120.0, 8.0, -64.0)), _enum_arg(1)]
	)
	_check(
		"radar_create_event_reads_a_position_then_the_enum",
		event.size() == 2
		and event[0] is Vector3
		and (event[0] as Vector3) == Vector3(120.0, 8.0, -64.0)
		and int(event[1]) == 1,
		str(event)
	)


# --- 6. Durations are passed through -------------------------------------


func _test_durations_are_passed_through_unconverted() -> void:
	## A DELIBERATE DECISION, PINNED HERE SO IT CANNOT DRIFT SILENTLY. The sink
	## contract says a recording carries the map's own arguments, so a flash of
	## five seconds records 5. It does NOT record 50 interpreter ticks. The
	## world's tick convention governs FACET methods, which have to agree on a
	## timebase; a sink has none. If this is ever reversed it must be reversed
	## for the camera and audio sinks in the same commit.
	var harness := _harness()
	_check(
		"a_five_second_flash_records_five_not_fifty_ticks",
		_emitted(harness, "NAMED_FLASH", [_text_arg("Gandalf"), _int_arg(5)])
		== ["Gandalf", 5]
	)
	_check(
		"a_four_second_button_flash_records_four",
		_emitted(harness, "FLASH_OBJECTIVES_BUTTON", [_int_arg(4)]) == [4]
	)
	_check(
		"a_real_valued_caption_duration_records_its_seconds",
		_emitted(
			harness, "SHOW_MILITARY_CAPTION", [_text_arg("OBJ:Briefing"), _real_arg(2.5)]
		)
		== ["OBJ:Briefing", 2.5]
	)


# --- 7. Presentation is not simulation ------------------------------------


func _test_presentation_does_not_touch_the_simulation() -> void:
	var harness := _harness()
	var env: SageScriptEnv = harness["env"]

	# DISPLAY_COUNTER / HIDE_COUNTER / DISPLAY_COUNTDOWN_TIMER name a counter
	# and a timer. They must not create, read or write either - they tell the
	# HUD what to show. A handler that touched env here would make a HUD action
	# part of the lockstep state.
	_act(harness, "DISPLAY_COUNTER", [_counter_arg("reinforcements"), _text_arg("OBJ:R")])
	_act(harness, "HIDE_COUNTER", [_counter_arg("reinforcements")])
	_act(harness, "DISPLAY_COUNTDOWN_TIMER", [_counter_arg("assault"), _text_arg("OBJ:A")])
	_act(harness, "HIDE_COUNTDOWN_TIMER", [_counter_arg("assault")])
	for name: String in SERVED_ACTIONS:
		_act(harness, name, _synthetic_arguments(name))

	var snapshot := env.snapshot()
	_check(
		"no_counter_was_created_by_a_hud_action",
		(snapshot["counters"] as Dictionary) == {},
		str(snapshot["counters"])
	)
	_check(
		"no_flag_was_created_by_a_hud_action",
		(snapshot["flags"] as Dictionary) == {},
		str(snapshot["flags"])
	)
	_check(
		"no_timer_was_created_by_a_hud_action",
		(snapshot["timers"] as Dictionary) == {},
		str(snapshot["timers"])
	)

	# And nothing in this package asked the world for anything it could refuse -
	# every member is either a sink emission or blocked.
	_check(
		"no_simulation_facet_was_consulted",
		(harness["world"] as SageScriptWorldStub).refusal_log.is_empty(),
		str((harness["world"] as SageScriptWorldStub).refusal_log)
	)


# --- 8. Arity -------------------------------------------------------------


func _test_arity_is_enforced() -> void:
	# Positional reads are only safe because arity is checked first. A short
	# list must be rejected before the handler runs, not read as empty strings.
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	_check(
		"a_short_argument_list_is_rejected",
		_act(harness, "SET_TREE_SWAY", [_real_arg(45.0), _real_arg(12.5)])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_argument_list_is_rejected",
		_act(harness, "NAMED_FLASH", [_text_arg("Gandalf"), _int_arg(5), _int_arg(5)])
		== Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"an_argument_list_on_a_zero_argument_action_is_rejected",
		_act(harness, "RADAR_ENABLE", [_int_arg(1)]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_rejected_call_emits_nothing",
		_sink(harness).count_for("SET_TREE_SWAY") == 0,
		str(_sink(harness).ops())
	)
	_check(
		"a_rejected_call_is_recorded_as_a_gap",
		dispatch.gaps.has("action", "SET_TREE_SWAY", GapLog.REASON_BAD_ARGUMENTS)
	)


# --- 9. No UI adapter -----------------------------------------------------


func _test_a_world_without_a_ui_sink_refuses() -> void:
	# The BASE SageScriptWorld has no UI adapter. Every action in this package
	# must refuse into a gap there rather than reporting a HUD change that never
	# happened. Checked across all 51, because one handler that ignored the
	# emit() return value would pass a single-name spot check.
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	var env := Env.new()
	var world := SageScriptWorld.new()

	var wrongly_accepted: Array[String] = []
	for name: String in SERVED_ACTIONS:
		var status := dispatch.execute_action(
			_record(name, _synthetic_arguments(name)), env, world, "fixture"
		)
		if status != Dispatch.Status.WORLD_REFUSED:
			wrongly_accepted.append("%s -> %d" % [name, status])
	_check(
		"every_action_refuses_when_the_world_has_no_ui_sink",
		wrongly_accepted.is_empty(),
		str(wrongly_accepted)
	)
	_check(
		"the_refusal_is_recorded_as_a_world_refused_gap",
		dispatch.gaps.has("action", "NAMED_FLASH", GapLog.REASON_WORLD_REFUSED)
	)
	_check(
		"a_refusing_world_served_no_actions_from_this_package",
		dispatch.served_actions == 0,
		"served=%d" % dispatch.served_actions
	)


# --- 10. The match-outcome family -----------------------------------------


func _test_the_match_outcome_family_is_blocked() -> void:
	## VICTORY / DEFEAT / QUICKVICTORY / VICTORY_SCREEN decide, or display the
	## decision of, a match. There is no world command for that, so they must
	## refuse loudly. The assertion that matters most is the second one: they
	## must not have quietly become UI recordings, which would look like
	## coverage and do nothing.
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var sink := _sink(harness)
	sink.clear()

	var wrong_status: Array[String] = []
	for name: String in Wp07.BLOCKED_OUTCOME_ACTIONS:
		var status := _act(harness, name, [])
		if status != Dispatch.Status.BLOCKED:
			wrong_status.append("%s -> %d" % [name, status])
	_check("the_outcome_family_reports_blocked", wrong_status.is_empty(), str(wrong_status))
	_check(
		"the_outcome_family_emitted_nothing_to_the_ui_sink",
		sink.records.is_empty(),
		str(sink.ops())
	)

	var missing_gap: Array[String] = []
	for name: String in Wp07.BLOCKED_OUTCOME_ACTIONS:
		if not dispatch.gaps.has("action", name, GapLog.REASON_BLOCKED_SUBSYSTEM):
			missing_gap.append(name)
	_check(
		"each_outcome_action_records_a_blocked_subsystem_gap",
		missing_gap.is_empty(),
		str(missing_gap)
	)
	_check(
		"the_outcome_gap_is_not_plain_unimplemented",
		not dispatch.gaps.has("action", "VICTORY", GapLog.REASON_UNIMPLEMENTED),
		str(dispatch.gaps.to_lines())
	)
	var victory_detail := String(
		dispatch.gaps.entries[
			"action|VICTORY|%s" % GapLog.REASON_BLOCKED_SUBSYSTEM
		]["detail"]
	)
	_check(
		"the_outcome_gap_names_the_missing_subsystem",
		victory_detail.contains("match outcome"),
		victory_detail
	)
	# The ruling's own words, so the gap a map produces says why it is blocked
	# and who has to decide - not merely that something is missing.
	_check(
		"the_outcome_gap_states_the_pending_owner_decision",
		victory_detail.contains("no match-end routing in Meta")
		and victory_detail.contains("owner decision pending"),
		victory_detail
	)


# --- 11. The three UI-state conditions ------------------------------------


func _test_the_ui_state_conditions_are_blocked() -> void:
	## A condition that could not be evaluated must never look like an answer.
	## Here that has three parts: it evaluates false, it evaluates false EVEN
	## WHEN INVERTED (an inverted unevaluable condition that returned true would
	## open a gate on a lie), and it is recorded rather than served.
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	var wrongly_true: Array[String] = []
	for name: String in Wp07.BLOCKED_UI_STATE_CONDITIONS:
		if _cond(harness, name):
			wrongly_true.append(name)
		if _cond(harness, name, true):
			wrongly_true.append("%s (inverted)" % name)
	_check(
		"an_unevaluable_ui_condition_is_false_in_both_polarities",
		wrongly_true.is_empty(),
		str(wrongly_true)
	)
	_check(
		"an_unevaluable_ui_condition_is_never_counted_as_served",
		dispatch.served_conditions == 0,
		"served_conditions=%d" % dispatch.served_conditions
	)

	var missing_gap: Array[String] = []
	for name: String in Wp07.BLOCKED_UI_STATE_CONDITIONS:
		if not dispatch.gaps.has("condition", name, GapLog.REASON_BLOCKED_SUBSYSTEM):
			missing_gap.append(name)
	_check(
		"each_ui_condition_records_a_blocked_subsystem_gap",
		missing_gap.is_empty(),
		str(missing_gap)
	)
	_check(
		"the_ui_condition_gap_names_the_missing_surface",
		String(
			dispatch.gaps.entries[
				"condition|SPELL_STORE_IS_OPEN|%s" % GapLog.REASON_BLOCKED_SUBSYSTEM
			]["detail"]
		).contains("readback"),
		str(dispatch.gaps.to_lines())
	)


# --- Reporting ------------------------------------------------------------


func _report() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var coverage := dispatch.coverage()
	print(
		"HANDLERS_WP07 COVERAGE served=%d blocked_actions=%d blocked_conditions=%d"
		% [
			SERVED_ACTIONS.size(),
			Wp07.BLOCKED_OUTCOME_ACTIONS.size(),
			Wp07.BLOCKED_UI_STATE_CONDITIONS.size(),
		]
	)
	print(
		"HANDLERS_WP07 DISPATCH actions=%d conditions=%d blocked=%d"
		% [
			int((coverage["actions"] as Dictionary)["implemented"]),
			int((coverage["conditions"] as Dictionary)["implemented"]),
			(coverage["blocked_on_subsystem"] as Array).size(),
		]
	)
	print("HANDLERS_WP07 PACKAGES %s" % str((harness["registration"] as Dictionary)["packages"]))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP07 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP07 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
