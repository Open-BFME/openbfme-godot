extends SceneTree

const OverlayScript = preload("res://src/retail_slice/retail_member_health_overlay.gd")

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_MEMBER_HEALTH_OVERLAY_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_check("source_minimum_infantry_width", is_equal_approx(OverlayScript.SOURCE_MINIMUM_INFANTRY_WIDTH_PIXELS, 40.0))
	_check("source_fixed_height", is_equal_approx(OverlayScript.SOURCE_HEIGHT_PIXELS, 3.0))
	_check("source_outline_width", is_equal_approx(OverlayScript.SOURCE_OUTLINE_PIXELS, 1.0))
	_check("far_zoom_uses_source_minimum_width", is_equal_approx(OverlayScript.source_health_width_for_zoom(1.0), 40.0))
	_check("close_zoom_uses_source_camera_height_ratio", is_equal_approx(OverlayScript.source_health_width_for_zoom(0.0), 100.0))
	_check("health_width_is_bounded_outside_zoom_range", is_equal_approx(OverlayScript.source_health_width_for_zoom(-1.0), 100.0) and is_equal_approx(OverlayScript.source_health_width_for_zoom(2.0), 40.0))
	_check("enemy_health_is_always_visible", OverlayScript.should_show_battalion(1, false))
	_check("friendly_health_requires_selection", not OverlayScript.should_show_battalion(0, false) and OverlayScript.should_show_battalion(0, true))
	_check_colors("full_health", 1.0, Color(0.0, 1.0, 0.0, 1.0), Color(0.0, 0.5, 0.0, 1.0))
	_check_colors("three_quarter_health", 0.75, Color(0.25, 1.0, 0.0, 1.0), Color(0.25, 0.5, 0.0, 1.0))
	_check_colors("half_health", 0.5, Color(0.5, 1.0, 0.0, 1.0), Color(0.5, 0.5, 0.0, 1.0))
	_check_colors("damaged_health", 0.4, Color(1.0, 0.8, 0.0, 1.0), Color(0.5, 0.4, 0.0, 1.0))
	_check_colors("really_damaged_health", 0.25, Color(1.0, 0.25, 0.0, 1.0), Color(0.5, 0.25, 0.0, 1.0))
	await _check_rank_chevrons()

	print("RETAIL_MEMBER_HEALTH_OVERLAY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check_rank_chevrons() -> void:
	## Veterancy pips: the battalion's live level rides the overlay rows, and
	## rank 2+ draws one placeholder chevron per earned rank above the bar.
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.global_position = Vector3(0.0, 30.0, 30.0)
	camera.look_at(Vector3.ZERO, Vector3.UP)
	camera.current = true
	var veteran := FakeBattalion.new(3, 2, true)
	var rookie := FakeBattalion.new(1, 1, true)
	var overlay := OverlayScript.new()
	root.add_child(overlay)
	overlay.configure(null, camera, {"veteran": veteran, "rookie": rookie})
	await process_frame
	await process_frame
	overlay._draw()
	_check(
		"rank_chevrons_track_experience_level",
		overlay.rendered_chevron_count == 2,
		"chevrons=%d" % overlay.rendered_chevron_count
	)
	_check("rank_one_draws_no_chevrons", overlay.rendered_bar_count == 3, "bars=%d" % overlay.rendered_bar_count)
	veteran.free()
	rookie.free()
	overlay.free()
	camera.free()


class FakeBattalion:
	extends Node
	var team := 0
	var selected := true
	var _level := 1
	var _members := 1


	func _init(level: int, members: int, is_selected: bool) -> void:
		_level = level
		_members = members
		selected = is_selected


	func member_health_overlay_rows() -> Array[Dictionary]:
		var rows: Array[Dictionary] = []
		for index in range(_members):
			rows.append({
				"member_index": index,
				"health_ratio": 1.0,
				"world_position": Vector3(float(index), 0.0, 0.0),
				"experience_level": _level,
			})
		return rows


func _check_colors(name: String, ratio: float, expected_fill: Color, expected_outline: Color) -> void:
	var colors: Dictionary = OverlayScript.source_health_colors(ratio)
	_check(name, (colors.fill as Color).is_equal_approx(expected_fill) and (colors.outline as Color).is_equal_approx(expected_outline), str(colors))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_MEMBER_HEALTH_OVERLAY PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_MEMBER_HEALTH_OVERLAY FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
