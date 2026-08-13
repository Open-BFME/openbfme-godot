extends SceneTree
## Base-game WeatherData types and weather.ini particle knobs.

const AtmosphereScript = preload("res://src/retail_slice/retail_sage_atmosphere.gd")
const WeatherFxScript = preload("res://src/retail_slice/retail_weather_fx.gd")

const EXPECTED_CHECKS := 12
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "RETAIL_WEATHER_FX")
	call_deferred("_run")


func _run() -> void:
	_test_weather_ini_knobs()
	_test_weather_data_types()
	_test_map_snow()
	_test_unknown_fails_closed()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("WEATHER_FX PASS %s" % label)
	else:
		failed += 1
		printerr("WEATHER_FX FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _test_weather_ini_knobs() -> void:
	var knobs: Dictionary = AtmosphereScript.weather()
	_check(
		"weather_ini_snow_knobs",
		bool(knobs.get("snow_enabled", false))
			and not bool(knobs.get("is_snowing", true))
			and String(knobs.get("snow_texture", "")) == "EXRainDrop.tga"
			and is_equal_approx(float(knobs.get("snow_speed", 0.0)), 50.0)
			and is_equal_approx(float(knobs.get("snow_spacing", 0.0)), 30.0)
			and is_equal_approx(float(knobs.get("snow_box_height", 0.0)), 300.0),
		str(knobs)
	)
	_check(
		"weather_data_names_are_complete",
		AtmosphereScript.WEATHER_DATA_NAMES == ["RAINY", "CLOUDYRAINY", "SUNNY", "CLOUDY", "NONE"]
	)


func _test_weather_data_types() -> void:
	var fx: Node3D = WeatherFxScript.new()
	root.add_child(fx)
	_check("configure_normal_weather", String(fx.call("configure", "NORMAL", 1.0)) == "")
	_check("sunny_has_no_precip_or_lightning", _kind_is(fx, "SUNNY", "none", false))
	_check("cloudy_has_no_precip_or_lightning", _kind_is(fx, "CLOUDY", "none", false))
	_check("none_has_no_precip", _kind_is(fx, "NONE", "none", false))
	_check("rainy_rains_and_can_lightning", _kind_is(fx, "RAINY", "rain", true) and int(fx.get("particle_count")) > 0)
	_check("cloudyrainy_rains_and_can_lightning", _kind_is(fx, "CLOUDYRAINY", "rain", true))
	fx.free()


func _test_map_snow() -> void:
	var fx: Node3D = WeatherFxScript.new()
	root.add_child(fx)
	_check("snowy_map_with_none_data_snows", String(fx.call("configure", "SNOWY", 1.0)) == "" and _kind_is(fx, "NONE", "snow", false) and int(fx.get("particle_count")) > 0)
	fx.call("_process", 0.25)
	_check("snow_particles_advance", int(fx.get("particle_count")) > 0)
	fx.free()


func _test_unknown_fails_closed() -> void:
	var fx: Node3D = WeatherFxScript.new()
	root.add_child(fx)
	_check("unknown_map_weather_fails_closed", String(fx.call("configure", "HAIL", 1.0)).contains("unknown map weather"))
	fx.call("configure", "NORMAL", 1.0)
	_check("unknown_weather_data_fails_closed", String(fx.call("set_weather_data", "BLIZZARD")).contains("unknown WeatherData"))
	fx.free()


func _kind_is(fx: Node, weather_data: String, kind: String, lightning: bool) -> bool:
	var applied := String(fx.call("set_weather_data", weather_data))
	if applied != "":
		return false
	return String(fx.get("active_kind")) == kind and bool(fx.get("has_lightning")) == lightning


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("WEATHER_FX FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("RETAIL_WEATHER_FX_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
