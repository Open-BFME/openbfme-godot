## Weather viewer (lane WEATHER-1): a live game seen from the RTS camera while its weather plays: a snowy map's snow, Isengard's Freezing Rain (the
## snow manager's spell ramp) and Mordor's Darkness (the cloud effect's darkening of the terrain and the models). For the lane's screenshots; the spell
## is bought and cast through GameWorld's command API (purchase_science / cast_special_power), as a player's clicks would.
##
##   godot --path godot res://scenes/weather_viewer.tscn -- [options]
##
## Options (after `--`):
##   --scenario=<snow|rain|darkness>   (default snow: map mp withered heath; rain / darkness: map mp fall back 4p)
##   --map=<name>            another map
##   --seconds=<s>           game seconds before the shot (default: snow 3, rain 8, darkness 10)
##   --height=<h>            the camera's height above the look-at point (default 420)
##   --shots=<dir>           saves weather-<scenario>-before.png and weather-<scenario>.png there, then quits
extends Node3D

var _scenario := "snow"
var _map_name := ""
var _seconds := -1.0
var _height := 420.0
var _shots := ""
var _fs: RefCounted
var _world: Node3D
var _camera: Camera3D
var _label: Label
var _local := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--scenario="):
			_scenario = arg.substr(11)
		elif arg.begins_with("--map="):
			_map_name = arg.substr(6)
		elif arg.begins_with("--seconds="):
			_seconds = float(arg.substr(10))
		elif arg.begins_with("--height="):
			_height = float(arg.substr(9))
		elif arg.begins_with("--shots="):
			_shots = arg.substr(8)
	if _map_name == "":
		_map_name = "map mp withered heath" if _scenario == "snow" else "map mp fall back 4p"
	if _seconds < 0.0:
		_seconds = {"snow": 3.0, "rain": 8.0, "darkness": 10.0}.get(_scenario, 3.0)
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
	if not mount.ok:
		_fail("mount failed: %s" % [mount.errors])
		return
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	_camera = Camera3D.new()
	_camera.fov = 45.0
	_camera.near = 2.0
	_camera.far = 60000.0
	add_child(_camera)
	_world = ClassDB.instantiate("GameWorld")
	var setup: Dictionary = _world.setup(_fs)
	if not setup.ok:
		_fail("object world failed: %s" % [setup.errors])
		return
	var faction := "FactionIsengard" if _scenario == "rain" else "FactionMordor"
	var slots := [
		{"player": "Player_1", "faction": faction, "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMen", "human": false, "team": 1, "start_index": 1},
	]
	var rep: Dictionary = _world.load_map(_map_name, {"slots": slots, "seed": 4711, "terrain": true, "test_hooks": true})
	if not rep.ok:
		_fail("load_map failed: %s" % [rep.errors.slice(0, 5)])
		return
	add_child(_world)
	var idx := 0
	for pl in rep.players:
		if "Player_1" in str(pl):
			_local = idx
			break
		idx += 1
	var layer := CanvasLayer.new()
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(12, 8)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	layer.add_child(_label)
	# the weather is the map's (Weather.ini, the map's map.ini / solo.ini): a missing or unreadable file leaves no report to play
	var wr: Dictionary = _world.get_weather_report()
	if not wr.get("loaded", false):
		_fail("the weather did not load: %s" % [wr.get("errors", ["GameWorld has no weather"])])
		return
	_run()


func _fail(message: String) -> void:
	push_error(message)
	print("WEATHERVIEW FAIL: ", message)
	get_tree().quit(1)


## the camera over the local player's base (its first structure), RetailCamera's pitch 37.5 degrees
func _look_at_base() -> Vector2:
	var at := Vector2.ZERO
	var n := 0
	for o in _world.get_player_objects("Player_1"):
		var d: Dictionary = o if o is Dictionary else _world.get_object(int(o))
		if d.get("ok", true) and d.has("x"):
			at += Vector2(d.x, d.y)
			n += 1
	if n == 0:
		# no structure of Player_1 (a map without a start for it): the centre of every object
		for id in _world.get_object_ids():
			var o: Dictionary = _world.get_object(id)
			if o.get("ok", false):
				at += Vector2(o.x, o.y)
				n += 1
	if n > 0:
		at /= n
	var g := Vector3(at.x, float(_world.get_ground_height(at.x, at.y)), -at.y)
	var pitch := deg_to_rad(37.5)
	_camera.position = g + Vector3(0.0, _height, _height / tan(pitch))
	_camera.look_at(g, Vector3.UP)
	return at


func _wait(seconds: float) -> void:
	var t := 0.0
	while t < seconds:
		# the screenshots step fixed frames of 1 / 30 s (the same shot every run); the interactive playback follows the time the frames took (review of
		# #59: a fixed step per drawn frame ran 4.8 times too fast at 144 Hz), at most 0.25 s a frame
		var dt := 1.0 / 30.0 if _shots != "" else clampf(get_process_delta_time(), 0.0, 0.25)
		_world.advance(dt)
		t += dt
		var wr: Dictionary = _world.get_weather_report()
		_label.text = "%s on %s  t %.1f s  snow phase %d spacing %.1f particles %d  cloud phase %d counter %d  sound weather %d" % [
			_scenario, _map_name, t, wr.snow.phase, wr.snow.spacing, wr.snow.particles, wr.cloud.phase, wr.cloud.counter, wr.sound.weather]
		await get_tree().process_frame


func _shot(name: String) -> void:
	if _shots == "":
		return
	await RenderingServer.frame_post_draw
	var img: Image = get_viewport().get_texture().get_image()
	var path := _shots.path_join("weather-%s.png" % name)
	img.save_png(path)
	print("WEATHERVIEW shot ", path)


func _run() -> void:
	var at := _look_at_base()
	await _wait(1.0)
	await _shot(_scenario + "-before")
	if _scenario == "rain" or _scenario == "darkness":
		_world.debug_add_science_points(_local, 500)
		var buy: Array = ["SCIENCE_PalantirVision", "SCIENCE_WildMenAllies", "SCIENCE_FreezingRain"] if _scenario == "rain" else ["SCIENCE_EyeofSauron", "SCIENCE_Barricade", "SCIENCE_Darkness"]
		for s in buy:
			_world.purchase_science(_local, s)
			await _wait(0.4)
		var power := "SpellBookFreezingRain" if _scenario == "rain" else "SpellBookDarkness"
		# the Palantir's spell book button, then a click on the ground (the player's input path, InGameSpellBookModel)
		var ok := false
		var bar: Dictionary = _world.get_spell_bar(_local)
		for b in bar.get("buttons", []):
			if String(b.power) == power and b.usable:
				ok = _world.spell_bar_press(_local, int(b.index))
				if ok and b.needs_position:
					ok = _world.spell_bar_click_world(_local, at.x, at.y)
		print("WEATHERVIEW cast %s at %s: %s" % [power, at, ok])
		if not ok:
			# a map loaded without starting bases has no structure the spell book's requirements filter accepts: the logged test hook sets the weather
			# the power would set (ChangeWeather RAINY / CLOUDY for its WeatherDuration, here 120 s)
			var r: Dictionary = _world.debug_set_weather(2 if _scenario == "rain" else 1, 600)
			print("WEATHERVIEW weather set by the test hook: ", r)
	await _wait(_seconds)
	var wr: Dictionary = _world.get_weather_report()
	print("WEATHERVIEW report ", JSON.stringify(wr))
	var book2: Dictionary = _world.get_spellbook(_local)
	print("WEATHERVIEW logic weather ", book2.get("weather", {}), " frame ", _world.get_frame())
	var snow: Node = _world.get_node_or_null("Weather/Snow")
	if snow:
		var mm: MultiMesh = snow.multimesh
		var mat: ShaderMaterial = snow.material_override
		print("WEATHERVIEW snow node visible %s instances %d mesh surfaces %d tex %s box %s ceiling %s traveled %s quad %s camera %s" % [
			snow.visible, mm.instance_count, mm.mesh.get_surface_count() if mm.mesh else -1, mat.get_shader_parameter("tex"),
			mat.get_shader_parameter("box"), mat.get_shader_parameter("ceiling"), mat.get_shader_parameter("traveled"), mat.get_shader_parameter("quad_half"), _camera.global_position])
		if OS.get_cmdline_user_args().has("--debug-snow"):
			mat.set_shader_parameter("quad_half", 30.0)
			mat.set_shader_parameter("tint", Color(1, 0, 0, 1))
			await _shot(_scenario + "-debug")
	await _shot(_scenario)
	if _shots != "":
		get_tree().quit(0)
		return
	# interactive: the weather goes on until the window is closed (review: #59)
	while true:
		await _wait(1.0)
