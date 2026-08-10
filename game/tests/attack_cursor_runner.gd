extends SceneTree
## The attack cursor must be retail's red `SCCAttack` art, shown for an attack
## intent and only for an attack intent.
##
## Retail truth (RotWK `data/ini/mouse.ini`, read out of `ini.big`):
##   MouseCursor AttackObj          Texture = SCCAttack
##   MouseCursor ForceAttackObj     Texture = SCCAttack
##   MouseCursor ForceAttackGround  Texture = SCCAttack
##   MouseCursor Target             Texture = SCCAttack
##   MouseCursor OutRange           Texture = SCCAttack
## `SCCAttack` is `data/cursors/sccattack.ani`, a loose Win32 RIFF/ACON cursor
## (never packed into a BIG): 6 frames of 32x32 24bpp, hotspot X:15 Y:15,
## `seq  = 0 1 2 3 4 5 4 3 1 0` at `rate = 7` jiffies per step (7/60 s), i.e. a
## 1.167 s ping-pong of four red arrowheads converging on the target.
##
## What this runner can and cannot prove: the intent decision, the animation
## clock, the decode of a pack row, and the fallback are all pure and asserted
## here. Whether the OS actually paints that bitmap cannot be observed from a
## headless SceneTree - `Input.set_custom_mouse_cursor` is a no-op without a
## display server - so the presentation call itself is asserted only to be
## reachable and non-crashing, and is verified for real by running the game.

const Controller = preload("res://src/retail_slice/retail_cursor_controller.gd")

## Retail `sccattack.ani` header values, kept here as the external oracle this
## runner checks a mounted cursor pack against.
const RETAIL_FRAME_COUNT := 6
const RETAIL_SEQUENCE := [0, 1, 2, 3, 4, 5, 4, 3, 1, 0]
const RETAIL_STEP_SECONDS := 7.0 / 60.0
const RETAIL_SIZE := 32
const RETAIL_HOTSPOT := Vector2(15, 15)

## LIVENESS (rulebook T3): a headless coroutine that aborts silently looks
## exactly like a clean pass. Every test appends its name here and the run only
## reports green when all of them arrived. Raise this when you add a test;
## never lower it.
const EXPECTED_TESTS := 7
## Floor on asserted checks, so a test that returns early after one assert
## cannot pass for the whole suite either.
const MINIMUM_CHECKS := 45

var passed := 0
var failed := 0
var _completed: Array[String] = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_intent_selection()
	_completed.append("intent")
	_test_configure_decodes_a_pack_row()
	_completed.append("configure")
	_test_configure_refuses_malformed_rows()
	_completed.append("refusals")
	_test_animation_clock_follows_the_retail_sequence()
	_completed.append("clock")
	_test_fallback_shapes()
	_completed.append("fallback")
	_test_slice_asks_for_hostility_not_team_one()
	_completed.append("slice-wiring")
	_test_mounted_pack_matches_the_retail_cursor()
	_completed.append("mounted-pack")

	if _completed.size() != EXPECTED_TESTS:
		failed += 1
		push_error("ATTACK_CURSOR_FAIL liveness: %d/%d tests reported (%s)" % [
			_completed.size(), EXPECTED_TESTS, ", ".join(_completed)
		])
	if passed + failed < MINIMUM_CHECKS:
		failed += 1
		push_error("ATTACK_CURSOR_FAIL liveness: only %d checks ran, expected at least %d" % [
			passed + failed, MINIMUM_CHECKS
		])
	print("ATTACK_CURSOR_RESULT passed=%d failed=%d tests=%d" % [passed, failed, _completed.size()])
	quit(0 if failed == 0 else 1)


func _test_intent_selection() -> void:
	## The whole bug in one table: an attack intent needs a live selection, a
	## free pointer, and an enemy under it.
	_check(
		"attack_when_selection_and_enemy",
		Controller.select_intent(true, false, true) == Controller.INTENT_ATTACK
	)
	_check(
		"default_without_a_selection",
		Controller.select_intent(false, false, true) == Controller.INTENT_DEFAULT
	)
	_check(
		"default_over_empty_ground",
		Controller.select_intent(true, false, false) == Controller.INTENT_DEFAULT
	)
	_check(
		"default_while_a_command_is_armed",
		Controller.select_intent(true, true, true) == Controller.INTENT_DEFAULT
	)


func _test_configure_decodes_a_pack_row() -> void:
	var controller = Controller.new()
	_check("unconfigured_has_no_art", not controller.has_art())
	_check("unconfigured_frame_index_is_absent", controller.frame_index_at(0.0) == -1)
	var row := _synthetic_row(3, [0, 1, 2, 1], [0.25, 0.25, 0.25, 0.25], Vector2(4, 5))
	_check("configure_accepts_a_well_formed_row", controller.configure(row))
	_check("configure_reports_no_error", controller.last_error() == "")
	_check("frames_decoded", controller.frame_count() == 3)
	_check("sequence_preserved", Array(controller.sequence()) == [0, 1, 2, 1])
	_check("hotspot_preserved", controller.hotspot() == Vector2(4, 5))
	_check("cycle_is_the_sum_of_steps", absf(controller.cycle_seconds() - 1.0) < 0.001)
	_check("frame_texture_resolves", controller.frame_texture(0) != null)
	_check("frame_texture_out_of_range_is_null", controller.frame_texture(99) == null)


func _test_configure_refuses_malformed_rows() -> void:
	## Refusal must be loud and total: a half-decoded cursor would animate into
	## a blank frame rather than fall back to the stock crosshair.
	var cases := {
		"empty_row": {},
		"no_frames": {"width": 8, "height": 8, "frames": [], "sequence": [0], "frameSeconds": [0.1]},
		"bad_size": _mutate(_synthetic_row(1, [0], [0.1], Vector2.ZERO), "width", 0),
		"bad_base64": _mutate(
			_synthetic_row(1, [0], [0.1], Vector2.ZERO),
			"frames",
			[{"png": "!!!not base64!!!"}]
		),
		"sequence_out_of_range": _mutate(
			_synthetic_row(1, [0], [0.1], Vector2.ZERO), "sequence", [5]
		),
		"timing_length_mismatch": _mutate(
			_synthetic_row(2, [0, 1], [0.1, 0.1], Vector2.ZERO), "frameSeconds", [0.1]
		),
	}
	for label in cases.keys():
		var controller = Controller.new()
		var accepted: bool = controller.configure(cases[label])
		_check("refuses_%s" % label, not accepted)
		_check("refusal_is_named_%s" % label, controller.last_error() != "")
		_check("refusal_leaves_no_art_%s" % label, not controller.has_art())


func _test_animation_clock_follows_the_retail_sequence() -> void:
	var controller = Controller.new()
	# Retail timing: ten steps of 7/60 s over six frames, ping-ponging.
	var seconds: Array = []
	for _index in RETAIL_SEQUENCE.size():
		seconds.append(RETAIL_STEP_SECONDS)
	_check(
		"configure_retail_shaped_row",
		controller.configure(
			_synthetic_row(RETAIL_FRAME_COUNT, RETAIL_SEQUENCE, seconds, RETAIL_HOTSPOT)
		)
	)
	_check("retail_cycle_is_1_167s", absf(controller.cycle_seconds() - 10.0 * RETAIL_STEP_SECONDS) < 0.002)
	_check("clock_starts_on_step_0", controller.frame_index_at(0.0) == RETAIL_SEQUENCE[0])
	for step in RETAIL_SEQUENCE.size():
		# Sample the middle of each step so a boundary rounding error cannot
		# make a wrong implementation look right.
		var when := (float(step) + 0.5) * RETAIL_STEP_SECONDS
		_check(
			"clock_step_%d_shows_frame_%d" % [step, RETAIL_SEQUENCE[step]],
			controller.frame_index_at(when) == RETAIL_SEQUENCE[step]
		)
	_check(
		"clock_wraps_after_a_full_cycle",
		controller.frame_index_at(controller.cycle_seconds() + 0.5 * RETAIL_STEP_SECONDS)
			== RETAIL_SEQUENCE[0]
	)
	_check("clock_ignores_negative_time", controller.frame_index_at(-5.0) == RETAIL_SEQUENCE[0])


func _test_fallback_shapes() -> void:
	## With no art mounted the slice must still show the crosshair it always
	## showed, not the plain arrow.
	_check(
		"attack_falls_back_to_the_crosshair",
		Controller.fallback_shape_for(Controller.INTENT_ATTACK) == Input.CURSOR_CROSS
	)
	_check(
		"default_falls_back_to_the_arrow",
		Controller.fallback_shape_for(Controller.INTENT_DEFAULT) == Input.CURSOR_ARROW
	)
	# apply() must be safe to call headless, with and without art.
	var controller = Controller.new()
	controller.apply(Controller.INTENT_ATTACK, 0.0)
	controller.configure(_synthetic_row(2, [0, 1], [0.1, 0.1], Vector2.ZERO))
	controller.apply(Controller.INTENT_ATTACK, 0.05)
	controller.apply(Controller.INTENT_DEFAULT, 0.05)
	_check("apply_is_headless_safe", true)


func _test_slice_asks_for_hostility_not_team_one() -> void:
	## The hover cursor used to hit-test `_closest_battalion(point, 1)`: a
	## hardcoded enemy team. On a guest seat that drew the attack cursor over
	## the player's OWN army and never over the real enemy, and it disagreed
	## with the right-click order, which resolves hostility from the local seat.
	var source := FileAccess.get_file_as_string("res://src/retail_slice/retail_vertical_slice.gd")
	_check("vertical_slice_source_readable", source.length() > 0)
	var start := source.find("func _update_hover_cursor")
	_check("hover_cursor_function_present", start >= 0)
	if start < 0:
		return
	var body := source.substr(start, source.find("func _ensure_cursor_controller") - start)
	_check("hover_cursor_uses_local_hostility", body.contains("_closest_hostile_battalion"))
	_check("hover_cursor_checks_hostile_structures", body.contains("_closest_hostile_structure"))
	_check("hover_cursor_drops_the_hardcoded_team", not body.contains("_closest_battalion(point, 1)"))
	_check("hover_cursor_delegates_intent", body.contains("RetailCursorController.select_intent"))


func _test_mounted_pack_matches_the_retail_cursor() -> void:
	## Oracle check against a mounted cursor pack. Skipped, loudly, when no
	## pack is mounted so the runner stays useful on a machine with no retail
	## content - but it can never PASS by finding nothing.
	var db = root.get_node_or_null("ContentDB")
	_check("content_db_autoload_available", db != null)
	if db == null:
		return
	db.reload()
	for pack_root_value in db.pack_roots:
		print("[ATTACK_CURSOR] pack: %s" % String(pack_root_value))
	var row: Dictionary = db.get_retail_cursor("attackobj")
	if row.is_empty():
		# A skip is only honest on a machine with no retail content at all.
		# When the operator pointed the run at a content root, a missing cursor
		# pack is the regression this runner exists to catch, not a skip.
		if OS.get_environment("OPENBFME_CONTENT").strip_edges() != "":
			_check("cursor_pack_mounted_under_OPENBFME_CONTENT", false)
			push_error("ATTACK_CURSOR_FAIL no cursor pack under OPENBFME_CONTENT (gap=%s)" % db.retail_cursor_gap_reason("attackobj"))
			return
		print("ATTACK_CURSOR_SKIP no_cursor_pack_mounted (gap=%s)" % db.retail_cursor_gap_reason("attackobj"))
		return
	_check("pack_cursor_is_32x32", int(row.get("width", 0)) == RETAIL_SIZE and int(row.get("height", 0)) == RETAIL_SIZE)
	_check("pack_cursor_names_sccattack", String(row.get("texture", "")).to_lower() == "sccattack")
	_check("pack_cursor_has_six_frames", (row.get("frames", []) as Array).size() == RETAIL_FRAME_COUNT)
	# JSON numbers arrive as floats; compare the retail step order as ints.
	var pack_sequence: Array = []
	for step_value in (row.get("sequence", []) as Array):
		pack_sequence.append(int(step_value))
	_check("pack_cursor_sequence_is_retail", pack_sequence == RETAIL_SEQUENCE)
	var spot: Dictionary = row.get("hotspot", {})
	_check(
		"pack_cursor_hotspot_is_15_15",
		int(spot.get("x", -1)) == int(RETAIL_HOTSPOT.x) and int(spot.get("y", -1)) == int(RETAIL_HOTSPOT.y)
	)
	var controller = Controller.new()
	_check("pack_cursor_configures", controller.configure(row))
	if not controller.has_art():
		push_error("ATTACK_CURSOR_FAIL pack_cursor_error=%s" % controller.last_error())
		return
	# The bug was "wrong cursor", so assert the art is actually RED: retail's
	# SCCAttack is red arrowheads on transparency, nothing else.
	var texture: Texture2D = controller.frame_texture(controller.frame_index_at(0.0))
	_check("pack_cursor_frame_resolves", texture != null)
	if texture == null:
		return
	var image := texture.get_image()
	var opaque := 0
	var reddish := 0
	for y in image.get_height():
		for x in image.get_width():
			var pixel := image.get_pixel(x, y)
			if pixel.a <= 0.0:
				continue
			opaque += 1
			if pixel.r > 0.35 and pixel.r > pixel.g * 1.5 and pixel.r > pixel.b * 1.5:
				reddish += 1
	_check("pack_cursor_has_opaque_pixels", opaque > 0)
	_check("pack_cursor_is_mostly_red", opaque > 0 and float(reddish) / float(opaque) > 0.5)
	# The old presentation was the OS crosshair: no art at all.
	_check("pack_cursor_is_not_fully_opaque_block", opaque < image.get_width() * image.get_height())
	# Every retail intent that names SCCAttack must resolve to the same art.
	for alias in ["forceattackobj", "forceattackground", "target", "outrange"]:
		var aliased: Dictionary = db.get_retail_cursor(alias)
		_check(
			"alias_%s_resolves_to_the_same_art" % alias,
			not aliased.is_empty() and aliased.get("source", "") == row.get("source", "")
		)


func _synthetic_row(
	frame_count: int,
	sequence: Array,
	seconds: Array,
	spot: Vector2
) -> Dictionary:
	var frames: Array = []
	for index in frame_count:
		var image := Image.create(8, 8, false, Image.FORMAT_RGBA8)
		image.fill(Color(1.0, float(index) / float(maxi(frame_count, 1)), 0.0, 1.0))
		frames.append({"png": Marshalls.raw_to_base64(image.save_png_to_buffer())})
	return {
		"texture": "SCCAttack",
		"source": "sccattack.ani",
		"width": 8,
		"height": 8,
		"hotspot": {"x": int(spot.x), "y": int(spot.y)},
		"frames": frames,
		"sequence": sequence.duplicate(),
		"frameSeconds": seconds.duplicate(),
	}


func _mutate(row: Dictionary, key: String, value: Variant) -> Dictionary:
	var copy := row.duplicate(true)
	copy[key] = value
	return copy


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("ATTACK_CURSOR_FAIL %s" % label)
