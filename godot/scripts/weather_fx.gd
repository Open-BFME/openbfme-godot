## OpenBFME. GPL-3.0.
## Lane WEATHERFX-1 (NOT RETAIL, the owner 2026-10-29: "experimental awesome new weather effects"): the Enhanced look's weather, drawn on top of the
## retail weather of lane WEATHER-1 (GodotWeather) and driven only by it: the map's own snow (map.ini IsSnowing), Freezing Rain (the logic weather
## RAINY, its lightning), Darkness (CLOUDY, the cloud effect's darkening). A map without weather gets none of this. Presentation only: it reads
## GameWorld.get_weather_fx_state (the last presented frame's client weather) and the camera, sets shader globals (project.godot [shader_globals],
## shaders/weather_fx.gdshaderinc) and its own nodes; it never touches the simulation or its random numbers (its own RandomNumberGenerator).
##
## The effects, each separately switchable (`effects`, game.gd --weatherfx-effects=a,b,...):
##   rain       falling rain streaks (shaders/weather_rain.gdshader): wind slant, at least a pixel wide at any distance, soft where they meet the ground
##   wet        the ground darkens as the rain soaks it, puddles in the hollows with a sky sheen and sun glint; it dries off slowly after the rain
##   ripples    rings spreading in the puddles while it rains (with wet)
##   snow       small soft flakes drifting in gusts; none close to the camera (they would hide the units)
##   snowcover  snow settling on the upward ground while it snows (melting back when it stops)
##   storm      lightning: a flash that lights the ground and the models (the sun brightened a moment) and a forked bolt from the sky, at every retail lightning and,
##              in a weather whose WeatherData has HasLightning (RAINY: Freezing Rain), every 5 .. 14 s (retail never strikes there, see _process)
##   clouds     dark cloud shadows drifting over the map in rain and Darkness
##   mist       thin low mist only in the valleys and over water in rain and snow; Darkness as a moving purple gloom
## Readability first (the owner 2026-10-29): no effect may wash out the playfield; weatherfx1_player.gd's readability check measures it.
##   wind       trees sway harder in the rain and storm (the drawn vegetation sway, GameWorld.set_weather_fx_sway)
## While it snows (with snow or snowcover on) the tone mapping gets a snow grade (_apply_grade): readability on a bright snowy map.
## Only in a live game's 3D view (live(), lane WEATHERFX-2): never over a menu, the score screen or a loading screen; game.gd frees the node when the
## game is left, which turns every global and the grade off (_exit_tree).
## The detail level scales it: Low draws none, Medium half the drops / flakes and no mist, High and above everything.
extends Node3D

const EFFECTS := ["rain", "wet", "ripples", "snow", "snowcover", "storm", "clouds", "mist", "wind"]
const GLOBALS := ["wfx_enabled", "wfx_time", "wfx_wet", "wfx_ripples", "wfx_snow", "wfx_clouds", "wfx_cloud_offset", "wfx_flash", "wfx_gloom"]
const RAIN_DROPS := 8000
const SNOW_FLAKES := 4500
# the logic's weather (GlobalWeatherSystem, WeatherData order RW 0xC22CF8)
const W_NONE := 0
const W_CLOUDY := 1
const W_RAINY := 2
const W_CLOUDYRAINY := 3

## true while Options "Look" is "enhanced" (game_options.gd apply_look); the effects follow it at the next frame
static var look_enhanced := false

var effects := {} # effect name -> bool (all on by default)
var globals := {} # the shader globals as last set (_set_global)
var detail := 1.0 # 0 (Low: nothing) .. 1 (High)

var _world: Node3D
var _camera: Camera3D
var _audio: Node
var _rng := RandomNumberGenerator.new()
var _active := false
var _time := 0.0
var _cloud_offset := Vector2.ZERO
var _rain := 0.0       # 0 .. 1 rain falling now (eased)
var _snow := 0.0       # 0 .. 1 snow falling now (eased)
var _wet := 0.0
var _cover := 0.0
var _gloom := 0.0
var _storm := 0.0      # 0 .. 1 how stormy (rain with lightning)
var _flash := 0.0
var _flash_t := -1.0
var _flashes := -1
var _next_strike := -1.0 # seconds to this look's own next lightning in a weather whose WeatherData has HasLightning
var _look := Vector3.ZERO
var _ground := 0.0
var _low := 0.0        # the low ground of the view, for the mist
var _low_t := 0.0
var _rain_node: MultiMeshInstance3D
var _snow_node: MultiMeshInstance3D
var _mist_node: MeshInstance3D
var _bolt: MeshInstance3D
var _bolt_mat: StandardMaterial3D
var _sun: DirectionalLight3D # the game's sun (game.gd _apply_lighting): a strike brightens it for the models; null: no model flash
var _sun_energy := -1.0       # the sun's own energy while a strike brightens it (-1: not brightened)
var _last_state := {}
var _noise_tex: ImageTexture
var _env: Environment # the game's environment (the snow grade); null: no grade
var _grade := 0.0     # 0 .. 1 the snow grade in effect
const AGX_WHITE := 16.29   # Godot's AgX defaults, the Enhanced look's (game_options.gd apply_look sets no other)
const AGX_CONTRAST := 1.25
const SNOW_AGX_WHITE := 2.0
const SNOW_AGX_CONTRAST := 1.8
var _stats := {"frames": 0, "strikes": 0}

## `world` the GameWorld, `camera` the game camera; `audio` GameAudio (unused for now: retail's LightningFX already plays the thunder)
## `sun` the game's sun: a strike brightens it for a moment (the models' flash; the ground flashes through wfx_flash). No light of its own: since the
## object shadows (PR #136) every directional light adds the ground again in terrain.gdshader's light(), so a second one would double the ground
func setup(world: Node3D, camera: Camera3D, audio: Node = null, only: PackedStringArray = PackedStringArray(), env: Environment = null,
		sun: DirectionalLight3D = null) -> void:
	_world = world
	_camera = camera
	_env = env
	_sun = sun
	_audio = audio
	name = "WeatherFX"
	_rng.seed = 0x5745 # presentation only, its own generator
	for e in EFFECTS:
		effects[e] = only.is_empty() or only.has(e)
	_rain_node = _particles(RAIN_DROPS, false)
	_snow_node = _particles(SNOW_FLAKES, true)
	_mist_node = MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(1.0, 1.0)
	_mist_node.mesh = plane
	var mm := ShaderMaterial.new()
	mm.shader = load("res://shaders/weather_mist.gdshader")
	_mist_node.material_override = mm
	_mist_node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_mist_node.visible = false
	add_child(_mist_node)
	_bolt = MeshInstance3D.new()
	_bolt_mat = StandardMaterial3D.new()
	_bolt_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_bolt_mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	_bolt_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_bolt_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	_bolt_mat.no_depth_test = false
	_bolt_mat.vertex_color_use_as_albedo = true
	_bolt.material_override = _bolt_mat
	_bolt.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_bolt.visible = false
	add_child(_bolt)
	_set_globals_off()
	# the shaders' noise: one seamless fractal texture (FastNoiseLite, baked once per game; cheaper on an iGPU than noise computed per pixel)
	var noise := FastNoiseLite.new()
	noise.noise_type = FastNoiseLite.TYPE_VALUE_CUBIC
	noise.seed = 1871
	noise.frequency = 1.0 / 32.0
	noise.fractal_octaves = 4
	var img := noise.get_seamless_image(256, 256, false, false, 0.1, true)
	img.generate_mipmaps()
	_noise_tex = ImageTexture.create_from_image(img)
	(_mist_node.material_override as ShaderMaterial).set_shader_parameter("noise_tex", _noise_tex)


func _particles(count: int, snow: bool) -> MultiMeshInstance3D:
	var quad := ArrayMesh.new()
	var a := []
	a.resize(Mesh.ARRAY_MAX)
	a[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(1, 1, 0), Vector3(0, 1, 0)])
	a[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)])
	a[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2, 0, 2, 3])
	quad.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, a)
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = quad
	mm.instance_count = count
	for i in count:
		mm.set_instance_transform(i, Transform3D.IDENTITY)
	var node := MultiMeshInstance3D.new()
	node.name = "WeatherFXSnow" if snow else "WeatherFXRain"
	node.multimesh = mm
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/weather_rain.gdshader")
	mat.set_shader_parameter("snow", snow)
	if snow:
		mat.set_shader_parameter("fall_speed", 38.0)
		mat.set_shader_parameter("opacity", 0.6)
	node.material_override = mat
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	node.extra_cull_margin = 16384.0 # the vertex shader places the drops anywhere in the box: never frustum-cull the node
	node.visible = false
	add_child(node)
	return node


## a shader global, mirrored in `globals` (the renderer cannot be asked for them at run time; tests read the mirror)
func _set_global(name: String, value: Variant) -> void:
	globals[name] = value
	RenderingServer.global_shader_parameter_set(name, value)


func _set_globals_off() -> void:
	for g in GLOBALS:
		_set_global(g, Vector2.ZERO if g == "wfx_cloud_offset" else 0.0)


func _restore_sun() -> void:
	if _sun_energy >= 0.0 and _sun != null and is_instance_valid(_sun):
		_sun.light_energy = _sun_energy
	_sun_energy = -1.0


func _exit_tree() -> void:
	_set_globals_off()
	_restore_sun()
	_apply_grade(0.0)
	if is_instance_valid(_world) and _world.has_method("set_weather_fx_sway"):
		_world.set_weather_fx_sway(1.0)


## { active, rain, snow, wet, cover, gloom, storm, flash, strikes, frames, effects } for tests and captures
func report() -> Dictionary:
	return {"active": _active, "rain": _rain, "snow": _snow, "wet": _wet, "cover": _cover, "gloom": _gloom, "storm": _storm, "flash": _flash,
		"strikes": _stats.strikes, "frames": _stats.frames, "grade": _grade, "effects": effects.duplicate(), "detail": detail, "state": _last_state,
		"globals": globals.duplicate(), "drawn": {"rain": _rain_node.visible, "snow": _snow_node.visible, "mist": _mist_node.visible,
		"bolt": _bolt.visible}}


## lane WEATHERFX-2 (the owner 2026-10-29: "a washout on the menus with the snow"): the weather belongs to a live game's 3D view only: the game's
## camera draws the window (game.gd makes it current when a game starts and gives it up when the game is left: the score screen, the menus, a
## Restart or a load) and the world is shown (game.gd hides it behind the loading screen). Anything else (a menu, the score screen, a loading
## screen, the black bars around the menu art) gets no weather particles, snow cover, mist or snow grade; the next frame of a live game brings them back
func live() -> bool:
	return _camera.current and _camera.is_inside_tree() and _world.is_visible_in_tree()


func _on(e: String) -> bool:
	return effects.get(e, false) and detail > 0.0


func _process(delta: float) -> void:
	if not is_instance_valid(_world) or not is_instance_valid(_camera):
		return
	var st: Dictionary = _world.get_weather_fx_state() if look_enhanced and live() else {}
	if not st.get("loaded", false) or detail <= 0.0:
		if _active:
			_deactivate()
		return
	_active = true
	_last_state = st
	_stats.frames += 1
	var dt := clampf(delta, 0.0, 0.1)
	_time = fmod(_time + dt, 3600.0)
	var weather := int(st.weather)
	var raining: bool = (weather == W_RAINY or weather == W_CLOUDYRAINY) and bool(st.snow_draw)
	var snowing: bool = bool(st.map_snowing) and bool(st.snow_draw) and not raining
	var dark: Color = st.darkening
	var darkness := clampf((dark.r + dark.g + dark.b) / 3.0 / 0.35, 0.0, 1.0) if bool(st.cloud_active) and not raining else 0.0
	_rain = move_toward(_rain, 1.0 if raining else 0.0, dt / 3.0)
	_snow = move_toward(_snow, 1.0 if snowing else 0.0, dt / 4.0)
	_wet = move_toward(_wet, 1.0 if raining else 0.0, dt / (25.0 if raining else 90.0))
	_cover = move_toward(_cover, 1.0 if snowing else 0.0, dt / (120.0 if snowing else 150.0))
	_gloom = move_toward(_gloom, darkness, dt / 2.0)
	_storm = move_toward(_storm, 1.0 if raining else 0.0, dt / 4.0) # RAINY is retail's lightning weather (WeatherData HasLightning)
	var wind2: Vector2 = st.wind
	var wind := Vector2(wind2.x, -wind2.y) * 30.0 + Vector2(40.0, 25.0) * _storm # Godot x / z units per second
	_cloud_offset = Vector2(fmod(_cloud_offset.x + dt * (14.0 + 30.0 * _storm), 100000.0), fmod(_cloud_offset.y + dt * (9.0 + 20.0 * _storm), 100000.0))
	_update_view()
	# lightning: every retail lightning start (the cloud effect's LightningFX) strikes here too
	var flashes := int(st.flashes)
	if _flashes >= 0 and flashes > _flashes and _on("storm"):
		_strike(st.flash_position)
	_flashes = flashes
	# RotWK's cloud effect rolls its lightning only in the dark phase of CLOUDY, whose WeatherData says HasLightning = No ("Bug #9644"), so the
	# lightning of RAINY (HasLightning = Yes, Freezing Rain) never shows in retail; this look gives that weather the storm its data asks for
	if raining and bool(st.get("has_lightning", false)) and _on("storm"):
		if _next_strike < 0.0:
			_next_strike = _rng.randf_range(2.5, 5.0)
		_next_strike -= dt
		if _next_strike <= 0.0:
			_strike(st.flash_position)
			_next_strike = _rng.randf_range(5.0, 14.0)
	else:
		_next_strike = -1.0
	_update_flash(dt)
	var light := Vector3(1.0 - dark.r, 1.0 - dark.g, 1.0 - dark.b)
	_update_rain(wind, light)
	_update_snow(wind, light)
	_update_mist(dt, light)
	_apply_grade(_snow if _on("snow") or _on("snowcover") else 0.0)
	_set_global("wfx_noise_tex", _noise_tex)
	_set_global("wfx_enabled", 1.0)
	_set_global("wfx_time", _time)
	_set_global("wfx_wet", _wet if _on("wet") else 0.0)
	_set_global("wfx_ripples", _rain if _on("ripples") and _on("wet") else 0.0)
	_set_global("wfx_snow", _cover if _on("snowcover") else 0.0)
	_set_global("wfx_clouds", clampf(_rain * 0.45 + _gloom * 0.35, 0.0, 1.0) if _on("clouds") else 0.0)
	_set_global("wfx_cloud_offset", _cloud_offset)
	_set_global("wfx_flash", _flash)
	_set_global("wfx_gloom", _gloom * 0.5 if _on("mist") else 0.0)
	if _world.has_method("set_weather_fx_sway"):
		var gust := 0.5 + 0.5 * sin(_time * 0.6) * sin(_time * 0.23 + 1.0)
		_world.set_weather_fx_sway(1.0 + (_rain * 1.2 + _storm * 0.8) * (0.6 + 0.8 * gust) if _on("wind") else 1.0)


## the snow grade (readability, the owner 2026-10-29: "the snow kinda washes out the gameplay"): AgX tone mapping compresses a bright snowy
## playfield to ~0.8 of the Original look's contrast; while it snows the tone mapping gets more contrast and an earlier white, which brings the
## contrast and the units' outlines back to ~0.95 of the Original look (weatherfx1_player.gd's readability check measures it)
func _apply_grade(amount: float) -> void:
	_grade = amount
	if _env == null:
		return
	_env.tonemap_agx_white = lerpf(AGX_WHITE, SNOW_AGX_WHITE, amount)
	_env.tonemap_agx_contrast = lerpf(AGX_CONTRAST, SNOW_AGX_CONTRAST, amount)


func _deactivate() -> void:
	_active = false
	_apply_grade(0.0)
	_rain_node.visible = false
	_snow_node.visible = false
	_mist_node.visible = false
	_bolt.visible = false
	_restore_sun()
	_set_globals_off()
	if _world.has_method("set_weather_fx_sway"):
		_world.set_weather_fx_sway(1.0)


## the ground point at the centre of the view (the camera's ray against the ground, two refinements)
func _update_view() -> void:
	var o := _camera.global_position
	var f := -_camera.global_transform.basis.z
	var g := float(_world.get_weather_fx_ground(o.x, -o.z))
	var p := o
	for i in 3:
		if f.y > -0.05:
			p = o + Vector3(f.x, 0.0, f.z).normalized() * 600.0
			break
		var t := (o.y - g) / -f.y
		p = o + f * t
		g = float(_world.get_weather_fx_ground(p.x, -p.z))
	_look = Vector3(p.x, g, p.z)
	_ground = g


func _update_rain(wind: Vector2, light: Vector3) -> void:
	var show := _on("rain") and _rain > 0.01
	_rain_node.visible = show
	if not show:
		return
	var mat := _rain_node.material_override as ShaderMaterial
	var h := clampf(_camera.global_position.y - _ground, 150.0, 2000.0)
	var span := clampf(h * 2.2, 700.0, 2600.0)
	var top := minf(h * 0.6, 450.0) # not up to the camera: a drop close to it is a large, costly streak
	mat.set_shader_parameter("box_min", Vector3(_look.x - span * 0.5, _ground - 40.0, _look.z - span * 0.5))
	mat.set_shader_parameter("box_size", Vector3(span, top + 40.0, span))
	mat.set_shader_parameter("fall_speed", 650.0)
	mat.set_shader_parameter("wind", wind * 2.0)
	mat.set_shader_parameter("drop_length", 26.0)
	mat.set_shader_parameter("drop_width", 0.45)
	mat.set_shader_parameter("px_world", _px_world())
	mat.set_shader_parameter("visible_fraction", _rain * detail)
	mat.set_shader_parameter("opacity", 0.42)
	mat.set_shader_parameter("light", light)
	mat.set_shader_parameter("flash", _flash)
	mat.set_shader_parameter("time", _time)


func _update_snow(wind: Vector2, light: Vector3) -> void:
	var show := _on("snow") and _snow > 0.01
	_snow_node.visible = show
	if not show:
		return
	var mat := _snow_node.material_override as ShaderMaterial
	var h := clampf(_camera.global_position.y - _ground, 150.0, 2000.0)
	var span := clampf(h * 2.0, 600.0, 2400.0)
	var top := minf(h * 0.9, 600.0)
	mat.set_shader_parameter("box_min", Vector3(_look.x - span * 0.5, _ground - 20.0, _look.z - span * 0.5))
	mat.set_shader_parameter("box_size", Vector3(span, top + 20.0, span))
	mat.set_shader_parameter("wind", wind + Vector2(12.0, 6.0))
	mat.set_shader_parameter("gust", 1.0)
	mat.set_shader_parameter("px_world", _px_world())
	mat.set_shader_parameter("visible_fraction", _snow * detail)
	mat.set_shader_parameter("light", light)
	mat.set_shader_parameter("flash", _flash)
	mat.set_shader_parameter("time", _time)


func _update_mist(dt: float, light: Vector3) -> void:
	var density := 0.0
	var tint := Vector3(0.74, 0.77, 0.82)
	var swirl := 0.0
	if _gloom > 0.01:
		density = 0.22 * _gloom
		tint = Vector3(0.22, 0.16, 0.3)
		swirl = 1.0
	elif _rain > 0.01 or _snow > 0.01:
		# thin and only low in the valleys (readability: never a grey veil over the playfield, the owner 2026-10-29)
		density = 0.22 * maxf(_rain, _snow)
		tint = Vector3(0.8, 0.83, 0.88) if _snow > _rain else Vector3(0.6, 0.64, 0.7)
	var show := _on("mist") and density > 0.01 and detail >= 0.75
	_mist_node.visible = show
	if not show:
		return
	# the low ground of the view, sampled once a second (5 x 5 points), the sheet eased toward it
	_low_t -= dt
	if _low_t <= 0.0:
		_low_t = 1.0
		var lo := INF
		for i in 5:
			for j in 5:
				var x := _look.x + (i - 2) * 220.0
				var z := _look.z + (j - 2) * 220.0
				lo = minf(lo, float(_world.get_weather_fx_ground(x, -z)))
		_low = lo if _low == 0.0 else lerpf(_low, lo, 0.3)
	var height := _low + (30.0 if _gloom > 0.01 else 18.0) # low: a unit in a hollow is not veiled (readability)
	var span := clampf((_camera.global_position.y - _ground) * 3.0, 1200.0, 4000.0)
	_mist_node.global_transform = Transform3D(Basis().scaled(Vector3(span, 1.0, span)), Vector3(_look.x, height, _look.z))
	var mat := _mist_node.material_override as ShaderMaterial
	mat.set_shader_parameter("density", density)
	mat.set_shader_parameter("tint", tint * (light if _gloom <= 0.01 else Vector3.ONE))
	mat.set_shader_parameter("depth_range", 120.0 if _gloom > 0.01 else 60.0)
	mat.set_shader_parameter("drift", _cloud_offset)
	mat.set_shader_parameter("time", _time)
	mat.set_shader_parameter("swirl", swirl)


func _px_world() -> float:
	var h := float(get_viewport().get_visible_rect().size.y)
	return 2.0 * tan(deg_to_rad(_camera.fov) * 0.5) / maxf(h, 1.0)


## a lightning strike: the flash envelope and a forked bolt near the view (where retail plays LightningFX, the map's centre, is usually off screen)
func _strike(_retail_position: Vector3) -> void:
	_stats.strikes += 1
	_flash_t = 0.0
	# the ground point: in the upper two thirds of the view; the top: above the screen's top edge (up and away from the camera)
	var fwd := -_camera.global_transform.basis.z
	var flat := Vector3(fwd.x, 0.0, fwd.z).normalized()
	var side := flat.cross(Vector3.UP)
	var h := _camera.global_position.y - _ground
	var ground := _look + flat * _rng.randf_range(0.0, h * 0.6) + side * _rng.randf_range(-h * 0.7, h * 0.7)
	ground.y = float(_world.get_weather_fx_ground(ground.x, -ground.z))
	var top := ground + Vector3.UP * (h * 1.3) + flat * (h * 0.9) + side * _rng.randf_range(-h * 0.2, h * 0.2)
	_bolt.mesh = _bolt_mesh(top, ground)
	_bolt.visible = true
	if _sun != null and is_instance_valid(_sun) and _sun_energy < 0.0:
		_sun_energy = _sun.light_energy


## the bolt: a jagged main channel (midpoint displacement) with two or three branches, camera-facing ribbons, white-hot core in vertex colour
func _bolt_mesh(top: Vector3, bottom: Vector3) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var main := _jag(top, bottom, 7, 140.0)
	_ribbon(st, main, 5.0, Color(1.0, 1.0, 1.0, 1.0))
	_ribbon(st, main, 34.0, Color(0.45, 0.55, 1.0, 0.45))
	for b in _rng.randi_range(2, 3):
		var k := _rng.randi_range(int(main.size() * 0.2), int(main.size() * 0.7))
		var start: Vector3 = main[k]
		var end := start + Vector3(_rng.randf_range(-260, 260), -_rng.randf_range(200, 500), _rng.randf_range(-260, 260))
		var br := _jag(start, end, 5, 70.0)
		_ribbon(st, br, 3.0, Color(0.85, 0.9, 1.0, 0.9))
		_ribbon(st, br, 14.0, Color(0.45, 0.55, 1.0, 0.3))
	return st.commit()


func _jag(a: Vector3, b: Vector3, levels: int, amount: float) -> Array:
	var pts := [a, b]
	var amt := amount
	for l in levels:
		var next := []
		for i in pts.size() - 1:
			next.append(pts[i])
			var m: Vector3 = (pts[i] + pts[i + 1]) * 0.5
			m += Vector3(_rng.randf_range(-amt, amt), _rng.randf_range(-amt, amt) * 0.3, _rng.randf_range(-amt, amt))
			next.append(m)
		next.append(pts[pts.size() - 1])
		pts = next
		amt *= 0.55
	return pts


## one continuous camera-facing ribbon along `pts`: a centre line in `col` and edges fading to nothing (shared joints, no overlapping quads)
func _ribbon(st: SurfaceTool, pts: Array, width: float, col: Color) -> void:
	var eye := _camera.global_position
	var n := pts.size()
	var sides: Array = []
	for i in n:
		var p: Vector3 = pts[i]
		var d: Vector3 = ((pts[mini(i + 1, n - 1)] as Vector3) - (pts[maxi(i - 1, 0)] as Vector3)).normalized()
		sides.append(d.cross((eye - p).normalized()).normalized() * width * 0.5)
	var edge := Color(col.r, col.g, col.b, 0.0)
	for i in n - 1:
		var p0: Vector3 = pts[i]
		var p1: Vector3 = pts[i + 1]
		var s0: Vector3 = sides[i]
		var s1: Vector3 = sides[i + 1]
		for half in [-1.0, 1.0]:
			st.set_color(edge)
			st.add_vertex(p0 + s0 * half)
			st.set_color(col)
			st.add_vertex(p0)
			st.add_vertex(p1)
			st.set_color(edge)
			st.add_vertex(p0 + s0 * half)
			st.set_color(col)
			st.add_vertex(p1)
			st.set_color(edge)
			st.add_vertex(p1 + s1 * half)


## the flash: a bright stroke, a flicker and an after-flash over 0.7 s
func _update_flash(dt: float) -> void:
	if _flash_t < 0.0:
		_flash = 0.0
		return
	_flash_t += dt
	var t := _flash_t
	var f := 0.0
	if t < 0.05:
		f = 1.0
	elif t < 0.1:
		f = 0.25
	elif t < 0.17:
		f = 0.85
	elif t < 0.3:
		f = lerpf(0.5, 0.1, (t - 0.17) / 0.13)
	elif t < 0.38:
		f = 0.45
	elif t < 0.7:
		f = lerpf(0.3, 0.0, (t - 0.38) / 0.32)
	else:
		_flash_t = -1.0
		_bolt.visible = false
		_restore_sun()
	_flash = f * 0.5 * detail
	if _sun_energy >= 0.0 and is_instance_valid(_sun):
		_sun.light_energy = _sun_energy * (1.0 + f * 1.5)
	_bolt_mat.albedo_color = Color(1.0, 1.0, 1.0, clampf(f * 1.5, 0.0, 1.0) if t < 0.4 else 0.0)
	if _bolt_mat.albedo_color.a <= 0.0:
		_bolt.visible = false
