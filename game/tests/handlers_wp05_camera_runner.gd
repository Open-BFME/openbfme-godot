extends SceneTree

## Proof runner for WP05-camera.
##
## The package is presentation-only, so the ONLY observable effect of a handler
## is what it emitted. Every assertion below is therefore against the stub's
## recording sinks - which is the design, not a fallback (see the
## PresentationSink comment in script_world.gd).
##
## Covers, in order:
##   1. registration   - 67 actions served, 7 conditions blocked, 74 members,
##                       no registration errors
##   2. every action   - all 67 dispatched with hand-written arguments, each
##                       recorded once, on the right channel, under its own
##                       canonical name, with the exact values expected
##   3. argument trap  - every fixture argument carries integer AND real AND text
##                       (as sage_scb.py does), with DECOY values in the fields
##                       the declared type does not use; a type-searching or
##                       wrong-field read cannot pass
##   4. verbatim       - seconds and frames reach the sink unconverted
##   5. honest refusal - unknown parameter token and mismatched arity are
##                       BAD_ARGUMENTS; a world with no adapter is WORLD_REFUSED
##   6. conditions     - all 7 report BLOCKED, produce a blocked-subsystem gap
##                       naming the missing read surface, evaluate FALSE whether
##                       or not the record inverts them, and are never counted
##                       as coverage
##   7. no simulation  - running all 67 leaves counters and flags untouched
##   8. determinism    - two runs record identically
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     -s res://tests/handlers_wp05_camera_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const WorldStub := preload("res://src/script/script_world_stub.gd")
const GapLog := preload("res://src/script/script_gaps.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")
const Args := preload("res://src/script/script_args.gd")
const Wp05 := preload("res://src/script/handlers/wp05_camera.gd")

## Argument type code for a parameter type this repo has observed no code for.
## SageScriptArgs.validate() declines to assert on these, which is the honest
## behaviour for an unsourced code - see script_param_types.gd.
const UNOBSERVED_CODE := 99

## Decoy payloads. sage_scb.py writes integer, real AND text for every
## non-position argument, so each fixture slot below carries all three and the
## two the declared parameter type does NOT use hold these. If a handler read by
## wire code, by type-search, or from the wrong field, it would surface one of
## these instead of the expected value.
const DECOY_INT := 424242
const DECOY_REAL := 424242.5
const DECOY_TEXT := "DECOY-NOT-A-NAME"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_registration()
	_test_every_action_reaches_its_channel()
	_test_positional_argument_traps()
	_test_values_are_forwarded_verbatim()
	_test_bad_arguments_are_refused()
	_test_a_world_without_an_adapter_refuses()
	_test_blocked_conditions()
	_test_presentation_did_not_touch_the_simulation()
	_test_recording_is_deterministic()
	_report()
	print("HANDLERS_WP05_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


# --- Fixture helpers ------------------------------------------------------


func _slot(code: int, integer: int, real: float, text: String) -> Dictionary:
	return {"argumentType": code, "integer": integer, "real": real, "text": text}


func _real_arg(value: float) -> Dictionary:
	return _slot(ParamTypes.ARGUMENT_REAL, DECOY_INT, value, DECOY_TEXT)


func _int_arg(value: int) -> Dictionary:
	return _slot(ParamTypes.ARGUMENT_INTEGER, value, DECOY_REAL, DECOY_TEXT)


func _bool_arg(value: bool) -> Dictionary:
	return _slot(ParamTypes.ARGUMENT_BOOLEAN, 1 if value else 0, DECOY_REAL, DECOY_TEXT)


func _percent_arg(value: int) -> Dictionary:
	# PERCENT has no observed wire code; its payload is the integer field.
	return _slot(UNOBSERVED_CODE, value, DECOY_REAL, DECOY_TEXT)


func _enum_arg(value: int) -> Dictionary:
	# SHAKE_INTENSITY travels as its raw enum int.
	return _slot(UNOBSERVED_CODE, value, DECOY_REAL, DECOY_TEXT)


func _name_arg(value: String) -> Dictionary:
	# WAYPOINT / WAYPOINT_PATH / UNIT / TRIGGER_AREA / CAMERA / CAMERA_ANIMATION.
	return _slot(UNOBSERVED_CODE, DECOY_INT, DECOY_REAL, value)


func _record(name: String, arguments: Array, inverted: bool = false) -> Dictionary:
	return {
		"contentType": 0,
		"internalName": {"name": name, "wireTypeCode": 3},
		"arguments": arguments,
		"enabled": true,
		"inverted": inverted,
	}


func _harness() -> Dictionary:
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


func _cond(harness: Dictionary, name: String, arguments: Array, inverted: bool = false) -> bool:
	return (harness["dispatch"] as SageScriptDispatch).evaluate_condition(
		_record(name, arguments, inverted), harness["env"], harness["world"], "fixture"
	)


func _sink(harness: Dictionary, channel: String) -> SageScriptWorldStub.RecordingSink:
	var world: SageScriptWorldStub = harness["world"]
	return (world.ui() if channel == "ui" else world.camera()) as SageScriptWorldStub.RecordingSink


# --- The fixture table ----------------------------------------------------
#
# All 67 actions, with arguments AND expected recorded values written out by
# hand. Both sides are literals: nothing here is rebuilt from the vocabulary
# table or from the handler, so a signature drift under this package shows up as
# a failing value rather than as two derived lists agreeing with each other.
#
# Values are deliberately distinct across actions, so an action recorded under a
# neighbour's name or arguments leaking between calls would fail an assertion
# rather than coincide.


func _cases() -> Array:
	return [
		# --- no arguments -------------------------------------------------
		{"name": "CAMERA_BLOOM_EFFECT_BEGIN", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_BLOOM_EFFECT_END", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_BW_MODE_END", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_DISABLE_SLAVE_MODE", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_ENABLE_SLAVE_MODE", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_LETTERBOX_BEGIN", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_LETTERBOX_END", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_MOD_FREEZE_ANGLE", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_MOD_FREEZE_TIME", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_MOTION_BLUR_END_FOLLOW", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_MOVE_HOME", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_REMOVE_AREA_RESTRICTION", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_STOP_FOLLOW", "channel": "camera", "args": [], "expect": []},
		{"name": "CAMERA_STOP_TETHER_NAMED", "channel": "camera", "args": [], "expect": []},
		{"name": "DRAW_SKYBOX_BEGIN", "channel": "camera", "args": [], "expect": []},
		{"name": "DRAW_SKYBOX_END", "channel": "camera", "args": [], "expect": []},
		{"name": "MOVE_CAMERA_TO_SELECTION", "channel": "camera", "args": [], "expect": []},
		# HIDE_UI / SHOW_UI are the two members routed to the UI channel.
		{"name": "HIDE_UI", "channel": "ui", "args": [], "expect": []},
		{"name": "SHOW_UI", "channel": "ui", "args": [], "expect": []},

		# --- single INT ---------------------------------------------------
		{
			"name": "CAMERA_BW_MODE_BEGIN", "channel": "camera",
			"args": [_int_arg(30)], "expect": [30],
		},
		{
			"name": "CAMERA_MOD_SET_FINAL_SPEED_MULTIPLIER", "channel": "camera",
			"args": [_int_arg(2)], "expect": [2],
		},
		{
			"name": "CAMERA_MOD_SET_ROLLING_AVERAGE", "channel": "camera",
			"args": [_int_arg(5)], "expect": [5],
		},
		{
			"name": "CAMERA_MOTION_BLUR_FOLLOW", "channel": "camera",
			"args": [_int_arg(33)], "expect": [33],
		},
		{
			"name": "CAMERA_RING_MODE_START", "channel": "camera",
			"args": [_int_arg(11)], "expect": [11],
		},
		{
			"name": "CAMERA_RING_MODE_END", "channel": "camera",
			"args": [_int_arg(12)], "expect": [12],
		},
		{
			"name": "OVERSIZE_TERRAIN", "channel": "camera",
			"args": [_int_arg(3)], "expect": [3],
		},

		# --- single REAL --------------------------------------------------
		{
			"name": "CAMERA_SET_AUDIBLE_DISTANCE", "channel": "camera",
			"args": [_real_arg(350.0)], "expect": [350.0],
		},
		{
			"name": "SET_CAMERA_CLIP_DEPTH_MULTIPLIER", "channel": "camera",
			"args": [_real_arg(1.75)], "expect": [1.75],
		},

		# --- single BOOLEAN -----------------------------------------------
		{
			"name": "LOCK_CAMERA", "channel": "camera",
			"args": [_bool_arg(true)], "expect": [true],
		},
		{
			"name": "LOCK_CAMERA_ANGLE_AND_HEIGHT", "channel": "camera",
			"args": [_bool_arg(false)], "expect": [false],
		},
		{
			"name": "LOCK_CAMERA_RESET", "channel": "camera",
			"args": [_bool_arg(true)], "expect": [true],
		},
		{
			"name": "LOCK_CAMERA_ROTATION", "channel": "camera",
			"args": [_bool_arg(false)], "expect": [false],
		},
		{
			"name": "LOCK_CAMERA_SCROLL", "channel": "camera",
			"args": [_bool_arg(true)], "expect": [true],
		},
		{
			"name": "LOCK_CAMERA_ZOOM", "channel": "camera",
			"args": [_bool_arg(false)], "expect": [false],
		},
		{
			"name": "TERRAIN_RENDER_DISABLE", "channel": "camera",
			"args": [_bool_arg(true)], "expect": [true],
		},

		# --- single name --------------------------------------------------
		{
			"name": "CAMERA_MOD_FINAL_LOOK_TOWARD", "channel": "camera",
			"args": [_name_arg("FinalLook")], "expect": ["FinalLook"],
		},
		{
			"name": "CAMERA_MOD_LOOK_TOWARD", "channel": "camera",
			"args": [_name_arg("MidLook")], "expect": ["MidLook"],
		},
		{
			"name": "CAMERA_RESTRICT_TO_AREA", "channel": "camera",
			"args": [_name_arg("CameraBox")], "expect": ["CameraBox"],
		},
		{
			"name": "MOVE_CAMERA_BY_ANIMATION", "channel": "camera",
			"args": [_name_arg("IntroCam01")], "expect": ["IntroCam01"],
		},

		# --- SHAKE_INTENSITY enum, passed as its raw int (SEVERE = 3) ------
		{
			"name": "SCREEN_SHAKE", "channel": "camera",
			"args": [_enum_arg(3)], "expect": [3],
		},

		# --- mixed, name first ---------------------------------------------
		{
			"name": "CAMERA_ADD_SHAKER_AT", "channel": "camera",
			"args": [_name_arg("ShakePoint"), _real_arg(1.5), _real_arg(2.5), _real_arg(3.5)],
			"expect": ["ShakePoint", 1.5, 2.5, 3.5],
		},
		{
			"name": "CAMERA_FOLLOW_NAMED", "channel": "camera",
			"args": [_name_arg("Gimli"), _bool_arg(true), _real_arg(12.5)],
			"expect": ["Gimli", true, 12.5],
		},
		{
			"name": "CAMERA_TETHER_NAMED", "channel": "camera",
			"args": [_name_arg("Frodo"), _bool_arg(false), _real_arg(4.25)],
			"expect": ["Frodo", false, 4.25],
		},
		{
			"name": "CAMERA_MOTION_BLUR_JUMP", "channel": "camera",
			"args": [_name_arg("JumpTo"), _bool_arg(true)],
			"expect": ["JumpTo", true],
		},
		{
			"name": "NAMED_SET_CAMERA_FADING", "channel": "camera",
			"args": [_name_arg("Watchtower"), _bool_arg(true)],
			"expect": ["Watchtower", true],
		},
		{
			"name": "CAMERA_LOOK_TOWARD_OBJECT", "channel": "camera",
			"args": [
				_name_arg("Legolas"), _real_arg(1.0), _real_arg(2.0), _real_arg(3.0),
				_real_arg(4.0), _real_arg(5.0),
			],
			"expect": ["Legolas", 1.0, 2.0, 3.0, 4.0, 5.0],
		},
		{
			"name": "CAMERA_LOOK_TOWARD_WAYPOINT", "channel": "camera",
			"args": [
				_name_arg("LookAt"), _real_arg(6.0), _real_arg(7.0), _real_arg(8.0),
				_bool_arg(false),
			],
			"expect": ["LookAt", 6.0, 7.0, 8.0, false],
		},
		{
			"name": "RESET_CAMERA", "channel": "camera",
			"args": [_name_arg("HomeView"), _real_arg(2.0), _real_arg(0.5), _real_arg(0.5)],
			"expect": ["HomeView", 2.0, 0.5, 0.5],
		},
		{
			"name": "MOVE_CAMERA_TO", "channel": "camera",
			"args": [
				_name_arg("CamMarker"), _real_arg(4.0), _real_arg(0.5), _real_arg(1.0),
				_real_arg(1.25),
			],
			"expect": ["CamMarker", 4.0, 0.5, 1.0, 1.25],
		},
		{
			"name": "MOVE_CAMERA_ALONG_SPLINE_PATH", "channel": "camera",
			"args": [
				_name_arg("FlyIn"), _real_arg(8.0), _real_arg(0.5), _real_arg(1.0),
				_real_arg(1.5), _real_arg(0.25),
			],
			"expect": ["FlyIn", 8.0, 0.5, 1.0, 1.5, 0.25],
		},
		{
			"name": "MOVE_CAMERA_LOCATOR_ALONG_SPLINE_PATH", "channel": "camera",
			"args": [
				_name_arg("LookPath"), _real_arg(9.0), _real_arg(0.6), _real_arg(1.1),
				_real_arg(1.6), _real_arg(0.35),
			],
			"expect": ["LookPath", 9.0, 0.6, 1.1, 1.6, 0.35],
		},
		# The package's sharpest positional trap: two WAYPOINTs split by two
		# REALs. "The waypoint argument" is ambiguous here and a type-search
		# would aim the camera at its own start point.
		{
			"name": "SETUP_CAMERA", "channel": "camera",
			"args": [
				_name_arg("CamStart"), _real_arg(0.5), _real_arg(1.25), _name_arg("CamLookAt"),
			],
			"expect": ["CamStart", 0.5, 1.25, "CamLookAt"],
		},

		# --- REAL then two PERCENTs ----------------------------------------
		{
			"name": "CAMERA_MOD_SET_FINAL_PITCH", "channel": "camera",
			"args": [_real_arg(0.75), _percent_arg(20), _percent_arg(40)],
			"expect": [0.75, 20, 40],
		},
		{
			"name": "CAMERA_MOD_SET_FINAL_ZOOM", "channel": "camera",
			"args": [_real_arg(0.5), _percent_arg(25), _percent_arg(45)],
			"expect": [0.5, 25, 45],
		},

		# --- two BOOLEANs ---------------------------------------------------
		{
			"name": "CAMERA_MOTION_BLUR", "channel": "camera",
			"args": [_bool_arg(true), _bool_arg(false)],
			"expect": [true, false],
		},

		# --- three REALs ----------------------------------------------------
		{
			"name": "CAMERA_SET_DEFAULT", "channel": "camera",
			"args": [_real_arg(1.0), _real_arg(90.0), _real_arg(1.5)],
			"expect": [1.0, 90.0, 1.5],
		},

		# --- four REALs (the adjust/rotate family) --------------------------
		{
			"name": "FOCAL_LENGTH_CAMERA", "channel": "camera",
			"args": [_real_arg(35.0), _real_arg(2.0), _real_arg(0.5), _real_arg(0.25)],
			"expect": [35.0, 2.0, 0.5, 0.25],
		},
		{
			"name": "PITCH_CAMERA", "channel": "camera",
			"args": [_real_arg(0.8), _real_arg(3.0), _real_arg(0.5), _real_arg(0.5)],
			"expect": [0.8, 3.0, 0.5, 0.5],
		},
		{
			"name": "ROLL_CAMERA", "channel": "camera",
			"args": [_real_arg(15.0), _real_arg(2.5), _real_arg(0.4), _real_arg(0.6)],
			"expect": [15.0, 2.5, 0.4, 0.6],
		},
		{
			"name": "ROTATE_CAMERA", "channel": "camera",
			"args": [_real_arg(1.0), _real_arg(6.0), _real_arg(0.5), _real_arg(0.5)],
			"expect": [1.0, 6.0, 0.5, 0.5],
		},
		{
			"name": "ROTATE_CAMERA_LOCKED", "channel": "camera",
			"args": [_real_arg(2.0), _real_arg(7.0), _real_arg(0.6), _real_arg(0.7)],
			"expect": [2.0, 7.0, 0.6, 0.7],
		},
		{
			"name": "ROTATE_CAMERA_TO_ANGLE", "channel": "camera",
			"args": [_real_arg(270.0), _real_arg(5.0), _real_arg(0.3), _real_arg(0.8)],
			"expect": [270.0, 5.0, 0.3, 0.8],
		},
		{
			"name": "ZOOM_CAMERA", "channel": "camera",
			"args": [_real_arg(0.35), _real_arg(3.5), _real_arg(0.9), _real_arg(1.2)],
			"expect": [0.35, 3.5, 0.9, 1.2],
		},

		# --- the fade family: two REALs then three INTs ----------------------
		{
			"name": "CAMERA_FADE_ADD", "channel": "camera",
			"args": [
				_real_arg(0.0), _real_arg(1.0), _int_arg(5), _int_arg(10), _int_arg(15),
			],
			"expect": [0.0, 1.0, 5, 10, 15],
		},
		{
			"name": "CAMERA_FADE_MULTIPLY", "channel": "camera",
			"args": [
				_real_arg(1.0), _real_arg(0.0), _int_arg(6), _int_arg(11), _int_arg(16),
			],
			"expect": [1.0, 0.0, 6, 11, 16],
		},
		{
			"name": "CAMERA_FADE_SATURATE", "channel": "camera",
			"args": [
				_real_arg(0.5), _real_arg(1.0), _int_arg(7), _int_arg(12), _int_arg(17),
			],
			"expect": [0.5, 1.0, 7, 12, 17],
		},
		{
			"name": "CAMERA_FADE_SUBTRACT", "channel": "camera",
			"args": [
				_real_arg(0.25), _real_arg(0.75), _int_arg(8), _int_arg(13), _int_arg(18),
			],
			"expect": [0.25, 0.75, 8, 13, 18],
		},
	]


# --- 1. Registration ------------------------------------------------------


func _test_registration() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var outcome: Dictionary = harness["registration"]

	_check(
		"registration_reports_no_errors",
		(outcome["errors"] as Array).is_empty(),
		str(outcome["errors"])
	)
	_check(
		"wp05_is_registered_as_a_package",
		(outcome["packages"] as Array).has("WP05-camera"),
		str(outcome["packages"])
	)

	_check(
		"the_package_declares_all_seventy_four_members",
		Wp05.CAMERA_ACTIONS.size() + Wp05.UI_ACTIONS.size() + Wp05.BLOCKED_CONDITIONS.size() == 74,
		"camera=%d ui=%d blocked=%d" % [
			Wp05.CAMERA_ACTIONS.size(), Wp05.UI_ACTIONS.size(), Wp05.BLOCKED_CONDITIONS.size()
		]
	)
	_check(
		"sixty_seven_actions_and_seven_conditions",
		Wp05.CAMERA_ACTIONS.size() + Wp05.UI_ACTIONS.size() == 67
		and Wp05.BLOCKED_CONDITIONS.size() == 7
	)

	var missing_actions: Array[String] = []
	for name: String in Wp05.CAMERA_ACTIONS + Wp05.UI_ACTIONS:
		if not dispatch.action_handlers.has(name):
			missing_actions.append(name)
	_check("every_wp05_action_is_registered", missing_actions.is_empty(), str(missing_actions))

	var missing_conditions: Array[String] = []
	for name: String in Wp05.BLOCKED_CONDITIONS:
		if not dispatch.condition_handlers.has(name):
			missing_conditions.append(name)
	_check(
		"every_wp05_condition_is_registered_as_blocked",
		missing_conditions.is_empty(), str(missing_conditions)
	)

	# The fixture table must cover the package exactly - a member added to the
	# handler without a case here would otherwise ship untested.
	var declared: Array[String] = []
	for name: String in Wp05.CAMERA_ACTIONS + Wp05.UI_ACTIONS:
		declared.append(name)
	declared.sort()
	var covered: Array[String] = []
	for case: Dictionary in _cases():
		covered.append(String(case["name"]))
	covered.sort()
	_check(
		"the_fixture_table_covers_every_action_exactly_once",
		declared == covered,
		"declared=%d covered=%d" % [declared.size(), covered.size()]
	)


# --- 2. Every action reaches its channel ----------------------------------


func _test_every_action_reaches_its_channel() -> void:
	var harness := _harness()

	var not_ok: Array[String] = []
	for case: Dictionary in _cases():
		if _act(harness, String(case["name"]), case["args"] as Array) != Dispatch.Status.OK:
			not_ok.append(String(case["name"]))
	_check("all_sixty_seven_actions_are_served", not_ok.is_empty(), str(not_ok))

	var camera_sink := _sink(harness, "camera")
	var ui_sink := _sink(harness, "ui")

	var wrong_values: Array[String] = []
	var wrong_count: Array[String] = []
	for case: Dictionary in _cases():
		var name := String(case["name"])
		var sink := ui_sink if String(case["channel"]) == "ui" else camera_sink
		if sink.count_for(name) != 1:
			wrong_count.append("%s x%d" % [name, sink.count_for(name)])
			continue
		if sink.values_for(name) != case["expect"]:
			wrong_values.append(
				"%s recorded=%s expected=%s" % [name, str(sink.values_for(name)), str(case["expect"])]
			)
	_check(
		"each_action_is_recorded_exactly_once_on_its_own_channel",
		wrong_count.is_empty(), str(wrong_count)
	)
	_check(
		"each_action_records_exactly_the_arguments_the_map_gave_it",
		wrong_values.is_empty(), str(wrong_values)
	)

	# Channel separation, asserted both ways: the two UI members must not appear
	# on the camera channel and no camera member may leak onto the UI channel.
	_check(
		"hide_ui_and_show_ui_are_recorded_on_the_ui_channel",
		ui_sink.ops() == ["HIDE_UI", "SHOW_UI"],
		str(ui_sink.ops())
	)
	_check(
		"the_ui_members_are_not_also_on_the_camera_channel",
		camera_sink.count_for("HIDE_UI") == 0 and camera_sink.count_for("SHOW_UI") == 0
	)
	_check(
		"the_camera_channel_carries_exactly_the_sixty_five_camera_members",
		camera_sink.records.size() == 65 and camera_sink.records.size() == Wp05.CAMERA_ACTIONS.size(),
		"records=%d declared=%d" % [camera_sink.records.size(), Wp05.CAMERA_ACTIONS.size()]
	)
	# Letterbox hides the HUD too, but it is a camera MODE and stays on camera.
	_check(
		"letterbox_stays_on_the_camera_channel",
		camera_sink.count_for("CAMERA_LETTERBOX_BEGIN") == 1
		and ui_sink.count_for("CAMERA_LETTERBOX_BEGIN") == 0
	)


# --- 3. Positional argument traps -----------------------------------------


func _test_positional_argument_traps() -> void:
	## Every fixture slot carries a decoy in the two payload fields the declared
	## type does not use, so the assertions in test 2 already fail on a
	## wrong-field read. This test spells out the two cases where a type-search
	## reads a plausible WRONG SLOT rather than a wrong field - the failure mode
	## that produces a working-looking cutscene pointed at the wrong place.
	var harness := _harness()
	var camera_sink := _sink(harness, "camera")

	_act(harness, "SETUP_CAMERA", [
		_name_arg("CamStart"), _real_arg(0.5), _real_arg(1.25), _name_arg("CamLookAt")
	])
	var setup: Array = camera_sink.values_for("SETUP_CAMERA")
	_check(
		"setup_camera_keeps_its_two_waypoints_in_declaration_order",
		setup.size() == 4 and setup[0] == "CamStart" and setup[3] == "CamLookAt",
		str(setup)
	)
	_check(
		"setup_camera_did_not_collapse_the_two_waypoints",
		setup.size() == 4 and setup[0] != setup[3],
		str(setup)
	)

	# CAMERA_FADE_ADD(REAL, REAL, INT, INT, INT): the three frame counts are
	# interchangeable by type and only position tells them apart.
	_act(harness, "CAMERA_FADE_ADD", [
		_real_arg(0.0), _real_arg(1.0), _int_arg(5), _int_arg(10), _int_arg(15)
	])
	var fade: Array = camera_sink.values_for("CAMERA_FADE_ADD")
	_check(
		"fade_keeps_increase_hold_and_decrease_in_declaration_order",
		fade == [0.0, 1.0, 5, 10, 15],
		str(fade)
	)
	_check(
		"fade_frame_counts_are_integers_not_the_real_field_decoy",
		fade.size() == 5 and fade[2] is int and fade[2] != DECOY_REAL,
		str(fade)
	)

	# A name argument must come from `text`, never from the integer field that
	# sage_scb.py also populates - and a REAL from `real`, never from the text
	# decoy sitting beside it in the same slot.
	_act(harness, "CAMERA_ADD_SHAKER_AT", [
		_name_arg("ShakePoint"), _real_arg(1.5), _real_arg(2.5), _real_arg(3.5)
	])
	var shaker: Array = camera_sink.values_for("CAMERA_ADD_SHAKER_AT")
	_check(
		"a_waypoint_name_is_read_from_the_text_field",
		shaker.size() == 4 and shaker[0] == "ShakePoint",
		str(shaker)
	)
	# And a real must come from `real`, never from the text decoy.
	_check(
		"a_real_is_read_from_the_real_field",
		shaker.size() == 4 and shaker[1] is float and shaker[1] == 1.5,
		str(shaker)
	)


# --- 4. Verbatim values ---------------------------------------------------


func _test_values_are_forwarded_verbatim() -> void:
	## The presentation sink takes the map's own numbers. The interpreter runs at
	## 10 ticks per second, so a handler that converted durations would turn 2.5
	## seconds into 25 - and there would be nothing downstream to tell which side
	## had done it. These literals are chosen so a tick conversion cannot coincide.
	var harness := _harness()
	var camera_sink := _sink(harness, "camera")

	_act(harness, "CAMERA_ADD_SHAKER_AT", [
		_name_arg("ShakePoint"), _real_arg(1.5), _real_arg(2.5), _real_arg(3.5)
	])
	var shaker: Array = camera_sink.values_for("CAMERA_ADD_SHAKER_AT")
	_check(
		"a_duration_in_seconds_is_not_converted_to_ticks",
		shaker == ["ShakePoint", 1.5, 2.5, 3.5],
		str(shaker)
	)

	# CAMERA_BW_MODE_BEGIN's argument is a FRAME count, a third timebase again.
	_act(harness, "CAMERA_BW_MODE_BEGIN", [_int_arg(30)])
	_check(
		"a_frame_count_is_not_converted_either",
		camera_sink.values_for("CAMERA_BW_MODE_BEGIN") == [30],
		str(camera_sink.values_for("CAMERA_BW_MODE_BEGIN"))
	)


# --- 5. Honest refusal ----------------------------------------------------


func _test_bad_arguments_are_refused() -> void:
	# The dispatcher rejects a mismatched arity before the handler runs.
	var harness := _harness()
	_check(
		"a_short_argument_list_is_bad_arguments",
		_act(harness, "SETUP_CAMERA", [_name_arg("CamStart")]) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"a_long_argument_list_is_bad_arguments",
		_act(harness, "LOCK_CAMERA", [_bool_arg(true), _bool_arg(false)])
		== Dispatch.Status.BAD_ARGUMENTS
	)

	# The handler's own guards, exercised directly because the dispatcher would
	# normally stop both cases first. A parameter type this package has no
	# decoding rule for must refuse rather than fall back to reading text.
	var unknown_ctx := _direct_ctx(
		"MOVE_CAMERA_TO", ["OBJECT_STATUS"], [_name_arg("Whatever")]
	)
	_check(
		"an_undecodable_parameter_type_is_bad_arguments_not_a_text_fallback",
		Wp05._camera_action(unknown_ctx) == Dispatch.Status.BAD_ARGUMENTS
	)
	_check(
		"the_undecodable_refusal_names_the_offending_token",
		String(unknown_ctx.get("detail", "")).contains("OBJECT_STATUS"),
		String(unknown_ctx.get("detail", ""))
	)

	var arity_ctx := _direct_ctx("CAMERA_SET_DEFAULT", ["REAL", "REAL", "REAL"], [_real_arg(1.0)])
	_check(
		"the_handler_refuses_a_mismatched_arity_on_its_own",
		Wp05._camera_action(arity_ctx) == Dispatch.Status.BAD_ARGUMENTS
	)


func _direct_ctx(name: String, params: Array, arguments: Array) -> Dictionary:
	## A call context built by hand, for the handler guards the dispatcher would
	## otherwise short-circuit. `params` is a synthetic signature, which is the
	## only way to present this package with a parameter type its real
	## signatures do not contain.
	return {
		"name": name,
		"entry": {"params": params},
		"args": Args.create({"params": params}, arguments),
		"env": Env.new(),
		"world": WorldStub.new(1),
		"script_name": "fixture",
		"result": false,
	}


func _test_a_world_without_an_adapter_refuses() -> void:
	## The BASE SageScriptWorld has sinks that record nothing and refuse
	## everything, which is the honest state of a game whose presentation adapter
	## is not wired. A cutscene that silently does not play is the outcome this
	## prevents.
	var bare := Dispatch.new()
	Registry.register_all(bare)

	var camera_status := bare.execute_action(
		_record("ZOOM_CAMERA", [
			_real_arg(0.35), _real_arg(3.5), _real_arg(0.9), _real_arg(1.2)
		]),
		Env.new(), SageScriptWorld.new(), "fixture"
	)
	_check(
		"a_world_without_a_camera_sink_refuses_rather_than_pretending",
		camera_status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % camera_status
	)
	_check(
		"the_camera_refusal_is_recorded_as_a_gap",
		bare.gaps.has("action", "ZOOM_CAMERA", GapLog.REASON_WORLD_REFUSED),
		str(bare.gaps.to_lines())
	)

	var ui_status := bare.execute_action(
		_record("HIDE_UI", []), Env.new(), SageScriptWorld.new(), "fixture"
	)
	_check(
		"a_world_without_a_ui_sink_refuses_hide_ui_too",
		ui_status == Dispatch.Status.WORLD_REFUSED,
		"status=%d" % ui_status
	)


# --- 6. The seven blocked conditions --------------------------------------


func _test_blocked_conditions() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]

	# Arguments per condition, written out - a blocked handler still goes through
	# arity validation, so a wrong list would be BAD_ARGUMENTS and mask the
	# blocked status this test is about.
	var arguments := {
		"CAMERA_ENTERED_AREA": [_name_arg("CameraBox")],
		"CAMERA_HIT_SPECIFIC_SPLINE_WAYPOINT": [_name_arg("SplinePoint")],
		"CAMERA_MOVEMENT_FINISHED": [],
		"CAMERA_RESET": [],
		"CAMERA_ROTATE_DISTANCE": [_real_arg(90.0)],
		"CAMERA_SCROLL_DISTANCE": [_real_arg(250.0)],
		"CAMERA_ZOOM_DISTANCE": [_real_arg(75.0)],
	}
	_check(
		"the_blocked_fixture_covers_all_seven_conditions",
		arguments.size() == Wp05.BLOCKED_CONDITIONS.size(),
		"fixture=%d declared=%d" % [arguments.size(), Wp05.BLOCKED_CONDITIONS.size()]
	)

	var not_false: Array[String] = []
	var not_false_inverted: Array[String] = []
	var missing_gap: Array[String] = []
	for name: String in Wp05.BLOCKED_CONDITIONS:
		var args: Array = arguments[name]
		# THE RULE THIS TEST EXISTS FOR: a condition that could not be evaluated
		# is false, and stays false when the record inverts it. If inversion were
		# applied to an unevaluated answer, every blocked camera gate written as
		# "NOT camera reset" would fire immediately and the mission would advance
		# on a lie.
		if _cond(harness, name, args):
			not_false.append(name)
		if _cond(harness, name, args, true):
			not_false_inverted.append(name)
		if not dispatch.gaps.has("condition", name, GapLog.REASON_BLOCKED_SUBSYSTEM):
			missing_gap.append(name)
	_check("every_blocked_camera_condition_is_false", not_false.is_empty(), str(not_false))
	_check(
		"an_unevaluable_condition_is_false_even_when_inverted",
		not_false_inverted.is_empty(), str(not_false_inverted)
	)
	_check(
		"every_blocked_condition_records_a_blocked_subsystem_gap",
		missing_gap.is_empty(), str(missing_gap)
	)

	# The gap must name the MISSING SURFACE, not merely the failing condition -
	# otherwise it reads as backlog rather than as a design decision waiting on
	# the world's owner.
	var key := "condition|CAMERA_MOVEMENT_FINISHED|%s" % GapLog.REASON_BLOCKED_SUBSYSTEM
	var detail := String((dispatch.gaps.entries[key] as Dictionary)["detail"])
	_check(
		"the_blocked_gap_names_the_missing_camera_read_surface",
		detail.contains("camera state read-back"),
		detail
	)

	# Never counted as coverage, and never reported as an ordinary unimplemented.
	var counted: Array[String] = []
	for name: String in Wp05.BLOCKED_CONDITIONS:
		if dispatch.implemented_conditions().has(name):
			counted.append(name)
		if dispatch.gaps.has("condition", name, GapLog.REASON_UNIMPLEMENTED):
			counted.append("%s (unimplemented gap)" % name)
	_check(
		"blocked_camera_conditions_are_not_counted_as_implemented",
		counted.is_empty(), str(counted)
	)
	_check(
		"the_blocked_set_includes_all_seven_camera_conditions",
		Wp05.BLOCKED_CONDITIONS.all(func(n: String) -> bool: return dispatch.blocked_names().has(n))
	)


# --- 7. No simulation state was touched -----------------------------------


func _test_presentation_did_not_touch_the_simulation() -> void:
	var harness := _harness()
	var world: SageScriptWorldStub = harness["world"]
	for case: Dictionary in _cases():
		_act(harness, String(case["name"]), case["args"] as Array)

	var env: SageScriptEnv = harness["env"]
	var snapshot := env.snapshot()
	_check(
		"no_camera_action_wrote_a_counter_or_a_flag",
		(snapshot["counters"] as Dictionary).is_empty()
		and (snapshot["flags"] as Dictionary).is_empty(),
		str(snapshot)
	)
	# The stub push_errors on any unimplemented SIMULATION facet method, so a
	# handler that reached for one would already be loud. This asserts the
	# quieter property: it never even asked.
	_check(
		"no_camera_action_asked_a_simulation_facet_for_anything",
		world.refusal_log.is_empty(),
		str(world.refusal_log)
	)


# --- 8. Determinism -------------------------------------------------------


func _test_recording_is_deterministic() -> void:
	var first := _harness()
	var second := _harness()
	for case: Dictionary in _cases():
		_act(first, String(case["name"]), case["args"] as Array)
		_act(second, String(case["name"]), case["args"] as Array)
	_check(
		"two_runs_record_the_same_camera_stream",
		_sink(first, "camera").records == _sink(second, "camera").records
	)
	_check(
		"two_runs_record_the_same_ui_stream",
		_sink(first, "ui").records == _sink(second, "ui").records
	)


# --- Reporting ------------------------------------------------------------


func _report() -> void:
	var harness := _harness()
	var dispatch: SageScriptDispatch = harness["dispatch"]
	var coverage := dispatch.coverage()
	print(
		(
			"HANDLERS_WP05 COVERAGE wp05_actions=%d wp05_blocked_conditions=%d "
			+ "total_actions_implemented=%d total_blocked=%d"
		) % [
			Wp05.CAMERA_ACTIONS.size() + Wp05.UI_ACTIONS.size(),
			Wp05.BLOCKED_CONDITIONS.size(),
			int((coverage["actions"] as Dictionary)["implemented"]),
			(coverage["blocked_on_subsystem"] as Array).size(),
		]
	)
	print("HANDLERS_WP05 PACKAGES %s" % str((harness["registration"] as Dictionary)["packages"]))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("HANDLERS_WP05 PASS %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	else:
		failed += 1
		printerr("HANDLERS_WP05 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
