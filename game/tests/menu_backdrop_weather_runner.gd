extends SceneTree
## Painted menu plates carry a matching weather overlay: leaves on Rivendell,
## snow on the pass, embers on Mordor.

const WeatherScript = preload("res://src/ui/openbfme_menu_weather.gd")

const EXPECTED_CHECKS := 8
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "MENU_BACKDROP_WEATHER")
	call_deferred("_run")


func _run() -> void:
	_test_recipe_ids()
	_test_rivendell_has_leaves()
	_test_mordor_has_embers()
	_test_pass_has_snow()
	_test_particles_move()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("MENU_WEATHER PASS %s" % label)
	else:
		failed += 1
		printerr("MENU_WEATHER FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _test_recipe_ids() -> void:
	_check(
		"rivendell_recipe_id",
		WeatherScript.recipe_id_for("res://data/base/assets/ui/menu/backdrop_rivendell_vale.png") == "rivendell_vale"
	)
	_check(
		"mordor_recipe_id",
		WeatherScript.recipe_id_for("res://data/base/assets/ui/menu/backdrop_mordor_gate.png") == "mordor_gate"
	)


func _test_rivendell_has_leaves() -> void:
	var weather: Node = _make_weather("res://data/base/assets/ui/menu/backdrop_rivendell_vale.png")
	var kinds: PackedStringArray = weather.call("particle_kinds")
	_check("rivendell_spawns_leaves", kinds.find("leaves") >= 0, ",".join(kinds))
	_check("rivendell_has_enough_particles", int(weather.call("particle_count")) >= 20, str(weather.call("particle_count")))
	weather.free()


func _test_mordor_has_embers() -> void:
	var weather: Node = _make_weather("res://data/base/assets/ui/menu/backdrop_mordor_gate.png")
	var kinds: PackedStringArray = weather.call("particle_kinds")
	_check("mordor_spawns_embers", kinds.find("embers") >= 0, ",".join(kinds))
	weather.free()


func _test_pass_has_snow() -> void:
	var weather: Node = _make_weather("res://data/base/assets/ui/menu/backdrop_misty_pass.png")
	var kinds: PackedStringArray = weather.call("particle_kinds")
	_check("misty_pass_spawns_snow", kinds.find("snow") >= 0, ",".join(kinds))
	weather.free()


func _test_particles_move() -> void:
	var weather: Node = _make_weather("res://data/base/assets/ui/menu/backdrop_rivendell_vale.png")
	var before: Array[Vector2] = []
	var particles: Array = weather.get("_particles")
	for particle_value in particles:
		var particle: Dictionary = particle_value
		before.append(Vector2(float(particle["x"]), float(particle["y"])))
	weather.call("_process", 0.5)
	var moved := 0
	var after: Array = weather.get("_particles")
	for index in range(mini(before.size(), after.size())):
		var now_row: Dictionary = after[index]
		var now := Vector2(float(now_row["x"]), float(now_row["y"]))
		if now.distance_to(before[index]) > 0.0005:
			moved += 1
	_check("particles_advance_over_time", moved >= 8, "moved=%d of %d" % [moved, before.size()])
	weather.call("set_source", "res://data/base/assets/ui/menu/backdrop_mordor_gate.png")
	var switched: PackedStringArray = weather.call("particle_kinds")
	_check("switching_plate_rebuilds_recipe", switched.find("embers") >= 0 and switched.find("leaves") < 0, ",".join(switched))
	weather.free()


func _make_weather(path: String) -> Node:
	var weather: Node = WeatherScript.new()
	root.add_child(weather)
	weather.set("size", Vector2(1920, 1080))
	weather.call("set_source", path)
	return weather


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("MENU_WEATHER FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("MENU_BACKDROP_WEATHER_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
