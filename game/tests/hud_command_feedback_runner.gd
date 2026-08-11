extends SceneTree
## Fast gate for three playtest reports about HUD command feedback. No slice
## boot and no content pack: every subject here is presentation logic that can
## be driven directly, so this runs in well under a second.
##
##   B. "Right-click on a production building should show the rally-point
##      banner so I know where units will go."  -> RetailRallyIndicator.
##   C is gated in fortress_command_surface_runner instead: it needs a LIVE
##   RetailHud and camera, and RetailHud cannot be compiled in a bare SceneTree
##   (it references the ContentDB autoload at class-compile time).
##   D. "The radar camera is weird and doesn't work correctly."
##      -> RetailMinimap left-drag scrubbing.
##
## Run:
##   godot --headless --path game --script res://tests/hud_command_feedback_runner.gd

const MinimapScript = preload("res://src/retail_slice/retail_minimap.gd")
const RallyIndicatorScript = preload("res://src/retail_slice/retail_rally_indicator.gd")

## Every _check in this file. Guards against a silent coroutine/runtime abort
## skipping a whole section and still reporting green.
const EXPECTED_CHECKS := 16

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_check_minimap_drag_scrubs_the_camera()
	_check_rally_indicator()
	# Liveness guard. A GDScript runtime error aborts the enclosing function
	# WITHOUT failing anything, so a runner that only counts failures reports a
	# confident green while whole sections never ran. Pin the count.
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		print("HUD_COMMAND_FEEDBACK FAIL runner_ran_every_check | expected %d checks, ran %d — a section aborted silently" % [
			EXPECTED_CHECKS, passed + failed - 1
		])
	print("HUD_COMMAND_FEEDBACK_RESULT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)


# --------------------------------------------------------------- D: the radar


func _check_minimap_drag_scrubs_the_camera() -> void:
	## Retail's radar scrubs: you press and HOLD, and the camera follows the
	## cursor until you let go. Ours only acted on the press, so dragging across
	## the radar did nothing — the report's "weird and doesn't work correctly".
	var minimap := MinimapScript.new()
	minimap.size = Vector2(240, 240)
	root.add_child(minimap)

	var centers: Array[Vector2] = []
	var orders: Array[Vector2] = []
	minimap.center_requested.connect(func(point: Vector2) -> void: centers.append(point))
	minimap.order_requested.connect(func(point: Vector2) -> void: orders.append(point))

	# A press still jumps the camera (unchanged behaviour).
	minimap._gui_input(_button(MOUSE_BUTTON_LEFT, true, Vector2(90.0, 110.0)))
	_check("minimap_press_centres_the_camera", centers.size() == 1, "emitted %d" % centers.size())
	_check("minimap_press_starts_scrubbing", minimap.scrubbing, "press did not arm the drag")

	# Dragging while held keeps centring, and on a NEW world point each time.
	minimap._gui_input(_motion(Vector2(130.0, 110.0)))
	minimap._gui_input(_motion(Vector2(150.0, 140.0)))
	_check(
		"minimap_drag_keeps_centring_the_camera",
		centers.size() == 3,
		"expected 3 centre requests across press+2 motions, got %d" % centers.size()
	)
	_check(
		"minimap_drag_tracks_the_cursor",
		centers.size() == 3 and centers[0] != centers[1] and centers[1] != centers[2],
		"the drag reported the same world point twice: %s" % str(centers)
	)

	# Release ends the drag; motion afterwards must NOT move the camera, or the
	# view would follow the mouse forever.
	minimap._gui_input(_button(MOUSE_BUTTON_LEFT, false, Vector2(150.0, 140.0)))
	_check("minimap_release_ends_scrubbing", not minimap.scrubbing, "release left the drag armed")
	minimap._gui_input(_motion(Vector2(60.0, 60.0)))
	_check(
		"minimap_motion_after_release_is_ignored",
		centers.size() == 3,
		"the camera kept following the mouse after release (%d requests)" % centers.size()
	)

	# Right-click still orders without moving the camera.
	minimap._gui_input(_button(MOUSE_BUTTON_RIGHT, true, Vector2(100.0, 100.0)))
	_check("minimap_right_click_still_orders", orders.size() == 1, "emitted %d" % orders.size())
	_check(
		"minimap_right_click_does_not_move_the_camera",
		centers.size() == 3,
		"a right-click moved the camera"
	)
	# A right-press must not arm the left-drag scrub.
	_check("minimap_right_click_does_not_arm_scrubbing", not minimap.scrubbing, "right press armed the drag")

	minimap.queue_free()


# ------------------------------------------------------------ B: rally banner


func _check_rally_indicator() -> void:
	var indicator := RallyIndicatorScript.new()
	root.add_child(indicator)

	_check("rally_indicator_starts_hidden", not indicator.showing_rally and not indicator.visible, "")

	indicator.show_rally(Vector2(12.0, -4.0), 3.5)
	_check(
		"rally_indicator_shows_at_the_rally_point",
		indicator.showing_rally
			and indicator.visible
			and indicator.position.is_equal_approx(Vector3(12.0, 3.5, -4.0)),
		"visible=%s position=%s" % [str(indicator.visible), str(indicator.position)]
	)

	# Per-frame visibility churn on world/HUD nodes is a known click- and
	# hover-killing hazard here, and _sync_presentation calls in every frame:
	# repeating the same state must not WRITE `visible` at all. Counting the
	# writes (via the visibility_changed signal) is the only way to see that —
	# comparing the value before and after would hold even if it were rewritten
	# every frame.
	# GDScript lambdas capture by VALUE, so the counter lives in an Array (a
	# reference) — an int would silently stay 0 and make this check vacuous.
	var visibility_writes: Array[int] = [0]
	indicator.visibility_changed.connect(func() -> void: visibility_writes[0] += 1)
	for _frame in range(8):
		indicator.show_rally(Vector2(12.0, -4.0), 3.5)
	_check(
		"rally_indicator_does_not_churn_visibility",
		visibility_writes[0] == 0 and indicator.visible and indicator.showing_rally,
		"8 identical syncs produced %d visibility changes" % visibility_writes[0]
	)
	# ...and the counter is proven live: a REAL transition must move it.
	indicator.clear_rally()
	_check(
		"rally_indicator_visibility_counter_is_live",
		visibility_writes[0] == 1,
		"hiding the banner produced %d visibility changes, so the churn check above proves nothing" % visibility_writes[0]
	)
	indicator.show_rally(Vector2(12.0, -4.0), 3.5)

	indicator.clear_rally()
	_check(
		"rally_indicator_clears",
		not indicator.showing_rally and not indicator.visible,
		"clear left the banner up"
	)

	# The pack gap is NAMED rather than papered over with invented art, and the
	# name must be the one RETAIL actually authors — an invented id would make
	# the resolve-by-id path permanently dead. Oracle: pure RotWK 2.01
	# data/ini/object/system/system.ini:1201, `Object RallyPointMarker` ->
	# `Model = RallyFlag_SKN`, idle `RallyFlag_SKL.RallyFlag_WAVA`; the compiled
	# path follows the sibling `SCMoveHint` -> `scmovehint.glb` convention that
	# the shipped packs already demonstrate.
	_check(
		"rally_indicator_names_retails_own_rally_model",
		RallyIndicatorScript.RETAIL_RALLY_MODEL_NAME == "RallyFlag_SKN"
			and RallyIndicatorScript.RETAIL_RALLY_IDLE_ANIMATION == "RallyFlag_SKL.RallyFlag_WAVA"
			and RallyIndicatorScript.RETAIL_RALLY_MODEL_PATH == "assets/models/system/rallyflag_skn.glb",
		"model=%s anim=%s path=%s" % [
			RallyIndicatorScript.RETAIL_RALLY_MODEL_NAME,
			RallyIndicatorScript.RETAIL_RALLY_IDLE_ANIMATION,
			RallyIndicatorScript.RETAIL_RALLY_MODEL_PATH,
		]
	)
	# configure() against a pack that does NOT carry it must leave the indicator
	# honestly synthetic rather than silently claiming retail art.
	indicator.configure("")
	_check(
		"rally_indicator_stays_synthetic_without_pack_art",
		indicator.art_source_is_synthetic and indicator.retail_model_path == "",
		"art_source_is_synthetic=%s retail_model_path=%s" % [
			str(indicator.art_source_is_synthetic), indicator.retail_model_path
		]
	)
	indicator.queue_free()


# ------------------------------------------------------------------- helpers


func _button(index: int, pressed: bool, at: Vector2) -> InputEventMouseButton:
	var event := InputEventMouseButton.new()
	event.button_index = index
	event.pressed = pressed
	event.position = at
	return event


func _motion(at: Vector2) -> InputEventMouseMotion:
	var event := InputEventMouseMotion.new()
	event.position = at
	return event


func _check(name: String, ok: bool, detail: String) -> bool:
	if ok:
		passed += 1
		print("HUD_COMMAND_FEEDBACK PASS %s" % name)
	else:
		failed += 1
		print("HUD_COMMAND_FEEDBACK FAIL %s | %s" % [name, detail])
	return ok
