extends SceneTree
## Water presentation must come from water.ini WaterSet + WaterTransparency,
## not the invented teal ripple.

const AtmosphereScript = preload("res://src/retail_slice/retail_sage_atmosphere.gd")
const WaterScript = preload("res://src/retail_slice/retail_water_surface.gd")
const BATTLEFIELD_PATH := "res://src/retail_slice/retail_fords_battlefield.gd"

const EXPECTED_CHECKS := 12
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "RETAIL_WATER_SURFACE")
	call_deferred("_run")


func _run() -> void:
	_test_water_sets()
	_test_material_uses_afternoon_contract()
	_test_night_has_no_scroll()
	_test_unknown_time_fails_closed()
	_test_invented_teal_is_gone()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("WATER_SURFACE PASS %s" % label)
	else:
		failed += 1
		printerr("WATER_SURFACE FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _test_water_sets() -> void:
	_check("four_water_sets_are_named", AtmosphereScript.TIME_OF_DAY_NAMES == ["MORNING", "AFTERNOON", "EVENING", "NIGHT"])
	var afternoon: Dictionary = AtmosphereScript.water_set("AFTERNOON")
	_check(
		"afternoon_diffuse_is_water_ini",
		afternoon.get("diffuse_rgba8", []) == [185, 185, 185, 255]
			and is_equal_approx(float(afternoon.get("u_scroll_per_ms", -1.0)), 0.002)
			and int(afternoon.get("water_repeat_count", 0)) == 32
			and String(afternoon.get("water_texture", "")) == "TSWater.tga",
		str(afternoon)
	)
	var transparency: Dictionary = AtmosphereScript.water_transparency()
	_check(
		"transparency_is_water_ini",
		transparency.get("standing_water_color_rgb8", []) == [255, 255, 255]
			and is_equal_approx(float(transparency.get("transparent_water_depth", -1.0)), 3.0)
			and String(transparency.get("standing_water_texture", "")) == "TWWater01.tga"
			and not bool(transparency.get("additive_blending", true)),
		str(transparency)
	)


func _test_material_uses_afternoon_contract() -> void:
	var builder = WaterScript.new()
	var material: ShaderMaterial = builder.build_material("AFTERNOON", "")
	_check("afternoon_material_builds", material != null, String(builder.error))
	if material == null:
		return
	var contract: Dictionary = builder.last_contract
	var albedo: Color = contract.get("albedo", Color.TRANSPARENT)
	var expected := (185.0 / 255.0) * (225.0 / 255.0)
	_check(
		"afternoon_albedo_is_vertex_times_diffuse",
		is_equal_approx(albedo.r, expected)
			and is_equal_approx(albedo.g, expected)
			and is_equal_approx(albedo.b, expected)
			and albedo.b < 0.80,
		str(albedo)
	)
	_check(
		"afternoon_scroll_and_repeat_are_bound",
		is_equal_approx(float(material.get_shader_parameter("u_scroll_per_ms")), 0.002)
			and is_equal_approx(float(material.get_shader_parameter("water_repeat")), 32.0)
			and is_equal_approx(float(material.get_shader_parameter("transparent_depth")), 3.0),
		"u=%s repeat=%s depth=%s" % [
			str(material.get_shader_parameter("u_scroll_per_ms")),
			str(material.get_shader_parameter("water_repeat")),
			str(material.get_shader_parameter("transparent_depth")),
		]
	)
	_check(
		"missing_pack_texture_is_named_not_invented",
		String(contract.get("surface_texture_status", "")) == "unresolved-in-pack"
			and String(contract.get("requested_surface_texture", "")) == "TWWater01.tga",
		str(contract.get("surface_texture_status"))
	)


func _test_night_has_no_scroll() -> void:
	var builder = WaterScript.new()
	var material: ShaderMaterial = builder.build_material("NIGHT", "")
	_check("night_material_builds", material != null, String(builder.error))
	if material == null:
		return
	_check(
		"night_scroll_is_zero",
		is_equal_approx(float(material.get_shader_parameter("u_scroll_per_ms")), 0.0)
			and is_equal_approx(float(material.get_shader_parameter("v_scroll_per_ms")), 0.0),
		"u=%s v=%s" % [
			str(material.get_shader_parameter("u_scroll_per_ms")),
			str(material.get_shader_parameter("v_scroll_per_ms")),
		]
	)


func _test_unknown_time_fails_closed() -> void:
	var builder = WaterScript.new()
	var material: ShaderMaterial = builder.build_material("TWILIGHT", "")
	_check("unknown_time_of_day_returns_null", material == null)
	_check("unknown_time_of_day_names_the_gap", String(builder.error).contains("unknown WaterSet"), String(builder.error))


func _test_invented_teal_is_gone() -> void:
	var source := FileAccess.get_file_as_string(BATTLEFIELD_PATH)
	_check(
		"battlefield_water_no_longer_hardcodes_teal",
		not source.contains("0.06, 0.25, 0.32")
			and not source.contains("0.08, 0.30, 0.39")
			and source.contains("WaterSurfaceScript"),
		"teal remnants still present"
	)


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("WATER_SURFACE FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("RETAIL_WATER_SURFACE_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
