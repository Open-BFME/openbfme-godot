## Lane WEATHERFX-1: the Enhanced look's weather (scripts/weather_fx.gd) against a stub GameWorld, no retail data and no renderer readback:
##   * the Original look draws none of it: no node shown, every shader global 0, the vegetation sway left at 1;
##   * under Enhanced each of the nine effects shows in the weather that drives it, and alone shows only itself;
##   * a map without weather gets nothing, Low detail nothing, Medium half the drops and no mist;
##   * a strike brightens the game's sun for a moment (the models' flash) and always gives it back its energy;
##   * it only reads the world (get_weather_fx_state, get_weather_fx_ground: neither waits for the logic worker) and sets the drawn sway: no other GameWorld call (the simulation is untouched);
##   * the snow grade (more tone-mapping contrast while it snows) only under Enhanced snow, undone when the look goes back to Original;
##   * the readability measures (scripts/weather_readability.gd) pass an unchanged frame and fail a washed-out one;
##   * lane WEATHERFX-2: no weather with no game running: the game's camera given up (the score screen, the menus) or the world hidden (the loading
##     screen) turns everything off, the node freed (the game left) too, and the camera back brings it back; screens_match (the menus' check) passes
##     the same screen and an animated one and fails specks, a grey veil and lit black bars.
##   godot --headless --path godot --script res://tests/weatherfx_test.gd
extends SceneTree

const WeatherFX := preload("res://scripts/weather_fx.gd")
const Readability := preload("res://scripts/weather_readability.gd")
const ALLOWED := ["get_weather_fx_state", "get_weather_fx_ground", "set_weather_fx_sway"]


class StubWorld extends Node3D:
	var state := {}
	var calls := {}
	var sway := 1.0

	func _note(m: String) -> void:
		calls[m] = int(calls.get(m, 0)) + 1

	func get_weather_fx_state() -> Dictionary:
		_note("get_weather_fx_state")
		return state

	func get_weather_fx_ground(_x: float, _y: float) -> float:
		_note("get_weather_fx_ground")
		return 10.0

	func set_weather_fx_sway(scale: float) -> void:
		_note("set_weather_fx_sway")
		sway = scale


var failures := 0
var env: Environment # the last run's environment (the snow grade)
var sun: DirectionalLight3D # the last run's sun (the strikes brighten it)
var max_sun := 0.0 # its highest energy in the run
var checks := 0


func check(ok: bool, what: String) -> void:
	checks += 1
	if not ok:
		failures += 1
		print("WEATHERFX TEST FAIL: ", what)


func weather_state(kind: String) -> Dictionary:
	var st := {"loaded": true, "weather": 0, "map_snowing": false, "snow_draw": false, "snow_particles": 0, "snow_spacing": 30.0, "snow_speed": 50.0,
		"snow_phase": 0, "wind": Vector2.ZERO, "cloud_active": false, "darkening": Color(0, 0, 0), "lightning": 0.0, "lightning_on": false,
		"has_lightning": false, "flashes": 0, "flash_position": Vector3.ZERO}
	match kind:
		"snow":
			st.map_snowing = true
			st.snow_draw = true
			st.snow_particles = 24000
		"rain":
			st.weather = 2
			st.snow_draw = true
			st.snow_particles = 34000
			st.has_lightning = true
		"darkness":
			st.weather = 1
			st.cloud_active = true
			st.darkening = Color(0.39, 0.39, 0.12)
		"unloaded":
			st = {}
	return st


## a WeatherFX over a stub world in `kind`'s weather, run `seconds` of 0.1 s frames; returns [fx, world]
func run(kind: String, enhanced: bool, only: PackedStringArray = PackedStringArray(), detail := 1.0, seconds := 9.0) -> Array:
	var world := StubWorld.new()
	world.state = weather_state(kind)
	root.add_child(world)
	var cam := Camera3D.new()
	root.add_child(cam)
	cam.current = true # the game's camera draws the window (game.gd _enter_game): a live game (weather_fx.gd live())
	cam.global_position = Vector3(500, 420, 500)
	cam.look_at(Vector3(500, 10, 0))
	WeatherFX.look_enhanced = enhanced
	var fx = WeatherFX.new()
	env = Environment.new()
	sun = DirectionalLight3D.new()
	sun.light_energy = 1.0
	root.add_child(sun)
	max_sun = 1.0
	fx.setup(world, cam, null, only, env, sun)
	fx.detail = detail
	root.add_child(fx)
	fx.set_process(false)
	var t := 0.0
	while t < seconds:
		fx._process(0.1)
		max_sun = maxf(max_sun, sun.light_energy)
		t += 0.1
	return [fx, world, cam, sun]


func done(r: Array) -> void:
	for n in r:
		n.queue_free()


func g(fx, name: String) -> float:
	return float(fx.globals.get(name, 0.0))


func all_off(fx, world, label: String) -> void:
	var rep: Dictionary = fx.report()
	check(not rep.active, label + ": not active")
	for k in rep.drawn:
		check(not rep.drawn[k], "%s: %s not drawn" % [label, k])
	for name in WeatherFX.GLOBALS:
		if name != "wfx_cloud_offset":
			check(g(fx, name) == 0.0, "%s: %s is 0 (%s)" % [label, name, fx.globals.get(name)])
	check(world.sway == 1.0, label + ": the vegetation sway stays 1")
	check(sun.light_energy == 1.0, label + ": the sun at its own energy")
	check(is_equal_approx(env.tonemap_agx_contrast, WeatherFX.AGX_CONTRAST) and is_equal_approx(env.tonemap_agx_white, WeatherFX.AGX_WHITE),
		label + ": no snow grade")


## the shown parts of each effect in its weather: a Callable(fx, world) -> bool
func shows(effect: String) -> Array:
	match effect:
		"rain": return ["rain", func(fx, _w): return fx.report().drawn.rain]
		"wet": return ["rain", func(fx, _w): return g(fx, "wfx_wet") > 0.0]
		"ripples": return ["rain", func(fx, _w): return g(fx, "wfx_ripples") > 0.0]
		"storm": return ["rain", func(fx, _w): return int(fx.report().strikes) > 0 and max_sun > 1.0]
		"snow": return ["snow", func(fx, _w): return fx.report().drawn.snow]
		"snowcover": return ["snow", func(fx, _w): return g(fx, "wfx_snow") > 0.0]
		"clouds": return ["darkness", func(fx, _w): return g(fx, "wfx_clouds") > 0.0]
		"mist": return ["darkness", func(fx, _w): return fx.report().drawn.mist and g(fx, "wfx_gloom") > 0.0]
		"wind": return ["rain", func(_fx, w): return w.sway > 1.0]
	return []


func _initialize() -> void:
	_run_all.call_deferred() # the nodes must be inside the tree (the root enters it after _initialize)


func _run_all() -> void:
	await process_frame
	# the Original look: nothing in any weather
	for kind in ["snow", "rain", "darkness"]:
		var r := run(kind, false)
		all_off(r[0], r[1], "original " + kind)
		done(r)
	# no weather, Low detail: nothing
	var none := run("unloaded", true)
	all_off(none[0], none[1], "enhanced without weather")
	done(none)
	var low := run("rain", true, PackedStringArray(), 0.0)
	all_off(low[0], low[1], "enhanced at Low detail")
	done(low)
	# each effect on under Enhanced in its weather, alone and with the others; alone, the other effects stay off
	for effect in WeatherFX.EFFECTS:
		var sh := shows(effect)
		var r := run(sh[0], true)
		check(sh[1].call(r[0], r[1]), "enhanced %s: %s shows with every effect on" % [sh[0], effect])
		done(r)
		var only := PackedStringArray([effect])
		if effect == "ripples":
			only.append("wet") # the rings are drawn in the wet ground's puddles
		r = run(sh[0], true, only)
		check(sh[1].call(r[0], r[1]), "enhanced %s: %s shows alone" % [sh[0], effect])
		for other in WeatherFX.EFFECTS:
			if not only.has(other):
				var osh := shows(other)
				if osh[0] == sh[0]:
					check(not osh[1].call(r[0], r[1]), "enhanced %s: %s alone leaves %s off" % [sh[0], effect, other])
		for m in r[1].calls:
			check(ALLOWED.has(m), "enhanced %s: only reads the world (called %s)" % [sh[0], m])
		done(r)
	# a weather drives only its own effects: no rain things in snow, no snow things in rain
	var snow := run("snow", true)
	check(env.tonemap_agx_contrast > WeatherFX.AGX_CONTRAST and env.tonemap_agx_white < WeatherFX.AGX_WHITE, "snow: the snow grade raises the contrast")
	check(not snow[0].report().drawn.rain and g(snow[0], "wfx_wet") == 0.0 and int(snow[0].report().strikes) == 0, "snow: no rain, wet ground or lightning")
	done(snow)
	var rain := run("rain", true)
	check(is_equal_approx(env.tonemap_agx_contrast, WeatherFX.AGX_CONTRAST), "rain: no snow grade")
	check(not rain[0].report().drawn.snow and g(rain[0], "wfx_snow") == 0.0, "rain: no flakes or snow cover")
	var full: float = (rain[0]._rain_node.material_override as ShaderMaterial).get_shader_parameter("visible_fraction")
	done(rain)
	# Medium: half the drops, no mist
	var med := run("rain", true, PackedStringArray(), 0.5)
	var half: float = (med[0]._rain_node.material_override as ShaderMaterial).get_shader_parameter("visible_fraction")
	check(is_equal_approx(half, full * 0.5), "Medium detail: half the drops (%s of %s)" % [half, full])
	check(not med[0].report().drawn.mist, "Medium detail: no mist")
	done(med)
	# switching back to Original in a game turns everything off at the next frame (the snow grade too)
	for kind in ["rain", "snow"]:
		var back := run(kind, true)
		WeatherFX.look_enhanced = false
		back[0]._process(0.1)
		all_off(back[0], back[1], kind + " after switching back to Original")
		done(back)
	# lane WEATHERFX-2: nothing outside a live game's 3D view, whatever the weather of the game that was played
	for kind in ["snow", "rain", "darkness"]:
		var left := run(kind, true)
		check(left[0].report().active, kind + ": active in the game")
		left[2].current = false # game.gd gives the camera up when the game is left (the score screen, then the menus)
		left[0]._process(0.1)
		all_off(left[0], left[1], kind + " with the game's camera given up (a menu)")
		left[2].current = true
		left[1].hide() # game.gd hides the world behind the loading screen
		left[0]._process(0.1)
		all_off(left[0], left[1], kind + " behind the loading screen (the world hidden)")
		left[1].show()
		for i in 30:
			left[0]._process(0.1)
		check(left[0].report().active, kind + ": back in the game it draws again")
		done(left)
	var gone := run("snow", true)
	check(env.tonemap_agx_contrast > WeatherFX.AGX_CONTRAST, "snow: the grade is on in the game")
	root.remove_child(gone[0])
	gone[0].free() # game.gd _remove_weather_fx: the game is left
	check(is_equal_approx(env.tonemap_agx_contrast, WeatherFX.AGX_CONTRAST) and is_equal_approx(env.tonemap_agx_white, WeatherFX.AGX_WHITE),
		"snow: the grade is off once the node is freed")
	for name in WeatherFX.GLOBALS:
		var v = RenderingServer.global_shader_parameter_get(name)
		check(v == null or (v is float and v == 0.0) or (v is Vector2 and v == Vector2.ZERO), "snow: %s is 0 once the node is freed (%s)" % [name, v])
	done([gone[1], gone[2], gone[3]])
	# the menus' check: the same screen passes, an animated one (b equals the later Original frame) passes, specks / a veil / lit bars fail
	var menu := Image.create(320, 180, false, Image.FORMAT_RGB8)
	for y in 180:
		for x in 320:
			var v := 0.0 if x < 40 or x >= 280 else 0.2 + 0.4 * float((x / 9 + y / 7) % 2)
			menu.set_pixel(x, y, Color(v, v * 0.95, v * 0.9))
	check(Readability.screens_match(menu, menu.duplicate(), menu).ok, "menus: the same screen passes")
	var later := menu.duplicate()
	for x in range(100, 140):
		later.set_pixel(x, 50, Color(1, 1, 1))
	check(Readability.screens_match(menu, later.duplicate(), later).ok, "menus: an animated pixel the later Original frame shows too passes")
	var specks := menu.duplicate()
	for i in 400:
		specks.set_pixel((i * 37) % 320, (i * 53) % 180, Color(1, 1, 1))
	var sm: Dictionary = Readability.screens_match(menu, specks, menu)
	check(not sm.ok and sm.why.contains("specks"), "menus: white specks over the screen fail (%s)" % sm.why)
	var bars := menu.duplicate()
	for y in 180:
		for x in 40:
			bars.set_pixel(x, y, Color(0.08, 0.08, 0.08))
	var bm: Dictionary = Readability.screens_match(menu, bars, menu)
	check(not bm.ok and bm.why.contains("black pixels"), "menus: lit black bars fail (%s)" % bm.why)
	var grey := menu.duplicate()
	for y in 180:
		for x in 320:
			grey.set_pixel(x, y, grey.get_pixel(x, y).lerp(Color(0.6, 0.6, 0.6), 0.2))
	check(not Readability.screens_match(menu, grey, menu).ok, "menus: a grey veil over the menu fails")
	# lane INGAMEBG-1: an in-game panel's backdrop: the dark in-game background (the battlefield dimmed) passes, a cream veil and a pale globe fail
	var field := Image.create(320, 180, false, Image.FORMAT_RGB8)
	for y in 180:
		for x in 320:
			var v := 0.2 + 0.3 * float((x / 6 + y / 5) % 2)
			field.set_pixel(x, y, Color(v, v * 0.85, v * 0.6))
	var dimmed := field.duplicate()
	var veiled := field.duplicate()
	for y in 180:
		for x in 320:
			var c := field.get_pixel(x, y)
			dimmed.set_pixel(x, y, c * 0.25 + Color(0.02, 0.03, 0.03))
			veiled.set_pixel(x, y, c.lerp(Color(0.96, 0.86, 0.75), 0.8))
	check(Readability.panel_backdrop(field, dimmed, "pause").ok, "panels: the battlefield dimmed behind the pause menu passes (%s)" % Readability.panel_backdrop(field, dimmed, "pause").why)
	var pv: Dictionary = Readability.panel_backdrop(field, veiled, "pause")
	check(not pv.ok and pv.why.contains("veil"), "panels: a cream veil behind the pause menu fails (%s)" % pv.why)
	check(not Readability.panel_backdrop(field, field.duplicate(), "pause").ok, "panels: no backdrop at all (the battlefield as it is) fails")
	check(Readability.panel_backdrop(field, dimmed, "powers").ok, "panels: a dark globe on the dimmed battlefield passes")
	var pale := dimmed.duplicate()
	for y in range(108, 128):
		for x in range(138, 182):
			pale.set_pixel(x, y, Color(0.9, 0.88, 0.8))
	var pg: Dictionary = Readability.panel_backdrop(field, pale, "powers")
	check(not pg.ok and pg.why.contains("globe"), "panels: a pale globe fails (%s)" % pg.why)
	# the readability measures: the same frame passes, a washed-out one (a grey veil over the playfield) fails
	var img := Image.create(320, 180, false, Image.FORMAT_RGB8)
	for y in 180:
		for x in 320:
			var v := 0.3 + 0.3 * float((x / 6 + y / 6) % 2) + 0.05 * sin(x * 0.7)
			img.set_pixel(x, y, Color(v, v * 0.9, v * 0.7))
	var units := [Vector2(200, 90), Vector2(250, 60)]
	var a: Dictionary = Readability.measure(img, units)
	check(Readability.compare(a, a).fails.is_empty(), "readability: an unchanged frame passes")
	var veil := img.duplicate()
	for y in 180:
		for x in 320:
			veil.set_pixel(x, y, veil.get_pixel(x, y).lerp(Color(0.8, 0.82, 0.86), 0.5))
	var w: Dictionary = Readability.compare(a, Readability.measure(veil, units))
	check(not w.fails.is_empty(), "readability: a grey veil fails (%s)" % [w.ratios])
	var dark := img.duplicate()
	dark.adjust_bcs(0.5, 1.0, 1.0)
	check(not Readability.compare(a, Readability.measure(dark, units)).fails.is_empty(), "readability: a frame at half brightness fails")
	WeatherFX.look_enhanced = false
	print("WEATHERFX TEST: %d checks, %d failed" % [checks, failures])
	quit(1 if failures > 0 else 0)
