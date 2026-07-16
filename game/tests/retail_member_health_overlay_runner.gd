extends SceneTree

const OverlayScript = preload("res://src/retail_slice/retail_member_health_overlay.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
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

	print("RETAIL_MEMBER_HEALTH_OVERLAY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


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
