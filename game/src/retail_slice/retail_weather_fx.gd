class_name RetailWeatherFx
extends Node3D
## Battlefield weather presenter driven by weather.ini Weather / WeatherData
## and the map's WorldInfo weather (NORMAL / SNOWY). Spellbook ChangeWeather
## (CLOUDY / RAINY) overrides the visual type while the sim window is live.

const AtmosphereScript = preload("res://src/retail_slice/retail_sage_atmosphere.gd")

const KIND_NONE := "none"
const KIND_RAIN := "rain"
const KIND_SNOW := "snow"

var map_weather := AtmosphereScript.DEFAULT_MAP_WEATHER
var weather_data_name := "NONE"
var active_kind := KIND_NONE
var has_lightning := false
var weather_sound := ""
var particle_count := 0
var local_scale := 1.0
var error := ""
var last_contract: Dictionary = {}

var _rng := RandomNumberGenerator.new()
var _particles: Array[Dictionary] = []
var _quad: ImageTexture
var _time := 0.0
var _lightning_timer := 0.0
var _lightning_left := 0.0
var _multimesh_instance: MultiMeshInstance3D
var _flash_light: OmniLight3D


func configure(map_weather_name: String, local_units_per_source: float) -> String:
	error = ""
	var weather := AtmosphereScript.normalize_map_weather(map_weather_name)
	if weather == "":
		error = "unknown map weather '%s'" % map_weather_name
		return error
	if not is_finite(local_units_per_source) or local_units_per_source <= 0.0:
		error = "weather presenter requires a positive map scale"
		return error
	map_weather = weather
	local_scale = local_units_per_source
	_rng.seed = 0x57A7E4E4
	_quad = _make_soft_quad()
	if _multimesh_instance == null:
		_multimesh_instance = MultiMeshInstance3D.new()
		_multimesh_instance.name = "SageWeatherParticles"
		_multimesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(_multimesh_instance)
	if _flash_light == null:
		_flash_light = OmniLight3D.new()
		_flash_light.name = "SageLightningFlash"
		_flash_light.light_energy = 0.0
		_flash_light.omni_range = 400.0 * local_scale
		_flash_light.shadow_enabled = false
		add_child(_flash_light)
	_rebuild()
	return ""


func set_weather_data(name: String) -> String:
	var key := AtmosphereScript.normalize_weather_data(name)
	if key == "":
		return "unknown WeatherData '%s'" % name
	weather_data_name = key
	_rebuild()
	return ""


func clear_weather_data() -> void:
	weather_data_name = "NONE"
	_rebuild()


func set_camera_anchor(world_position: Vector3) -> void:
	if _multimesh_instance != null:
		_multimesh_instance.global_position = world_position
	if _flash_light != null:
		_flash_light.global_position = world_position + Vector3(0.0, 80.0 * local_scale, 0.0)


func runtime_contract() -> Dictionary:
	return last_contract.duplicate(true)


func _rebuild() -> void:
	var knobs: Dictionary = AtmosphereScript.weather()
	var data: Dictionary = AtmosphereScript.weather_data(weather_data_name)
	if data.is_empty() and weather_data_name != "NONE":
		error = "WeatherData '%s' is missing" % weather_data_name
		return
	has_lightning = bool(data.get("has_lightning", false))
	weather_sound = String(data.get("weather_sound", ""))
	var raining := weather_data_name in ["RAINY", "CLOUDYRAINY"]
	var snowing := map_weather == "SNOWY" or bool(knobs.get("is_snowing", false))
	if raining:
		active_kind = KIND_RAIN
	elif snowing and bool(knobs.get("snow_enabled", false)):
		active_kind = KIND_SNOW
	else:
		active_kind = KIND_NONE
	_particles.clear()
	if active_kind != KIND_NONE:
		_spawn_particles(knobs)
	particle_count = _particles.size()
	_sync_multimesh()
	last_contract = {
		"schema": "openbfme.sage-weather-fx",
		"schema_version": 0,
		"map_weather": map_weather,
		"weather_data": weather_data_name,
		"kind": active_kind,
		"has_lightning": has_lightning,
		"weather_sound": weather_sound,
		"particle_count": particle_count,
		"snow_texture": String(knobs.get("snow_texture", "")),
		"snow_speed": float(knobs.get("snow_speed", 0.0)),
		"snow_box_height": float(knobs.get("snow_box_height", 0.0)),
		"snow_spacing": float(knobs.get("snow_spacing", 0.0)),
		"number_tiles": int(knobs.get("number_tiles", 0)),
		"cloud_offset_per_second": knobs.get("cloud_offset_per_second", []),
		"texture_status": "unresolved-in-pack",
	}
	set_process(active_kind != KIND_NONE or has_lightning)
	set_meta("weather_contract", last_contract.duplicate(true))


func _spawn_particles(knobs: Dictionary) -> void:
	var spacing := maxf(float(knobs.get("snow_spacing", 30.0)), 1.0)
	var tiles := maxi(int(knobs.get("number_tiles", 4)), 1)
	var height := float(knobs.get("snow_box_height", 300.0)) * local_scale
	var speed := float(knobs.get("snow_speed", 50.0)) * local_scale
	var extent := spacing * 8.0 * local_scale
	var count := mini(tiles * 64, 256)
	var quad_size := float(knobs.get("snow_quad_size", 10.0)) * local_scale * 0.08
	for index in count:
		var particle := {
			"x": _rng.randf_range(-extent, extent),
			"y": _rng.randf_range(0.0, height),
			"z": _rng.randf_range(-extent, extent),
			"speed": speed * _rng.randf_range(0.85, 1.15),
			"size": quad_size,
			"phase": _rng.randf() * TAU,
		}
		_particles.append(particle)


func _process(delta: float) -> void:
	_time += delta
	if active_kind != KIND_NONE:
		_step_particles(delta)
		_sync_multimesh()
	if has_lightning:
		_step_lightning(delta)
	else:
		_lightning_left = 0.0
		if _flash_light != null:
			_flash_light.light_energy = 0.0


func _step_particles(delta: float) -> void:
	var knobs: Dictionary = AtmosphereScript.weather()
	var height := float(knobs.get("snow_box_height", 300.0)) * local_scale
	var amplitude := float(knobs.get("snow_amplitude", 0.0)) * local_scale
	var freq_x := float(knobs.get("snow_frequency_scale_x", 0.0))
	var freq_y := float(knobs.get("snow_frequency_scale_y", 0.0))
	var extent := float(knobs.get("snow_spacing", 30.0)) * 8.0 * local_scale
	for particle in _particles:
		particle["y"] = float(particle["y"]) - float(particle["speed"]) * delta
		if amplitude != 0.0:
			particle["x"] = float(particle["x"]) + sin(_time * freq_x + float(particle["phase"])) * amplitude * delta
			particle["z"] = float(particle["z"]) + cos(_time * freq_y + float(particle["phase"])) * amplitude * delta
		if float(particle["y"]) < 0.0:
			particle["y"] = height
			particle["x"] = _rng.randf_range(-extent, extent)
			particle["z"] = _rng.randf_range(-extent, extent)


func _step_lightning(delta: float) -> void:
	var knobs: Dictionary = AtmosphereScript.weather()
	var cloud: Dictionary = AtmosphereScript.cloud_effect()
	if _lightning_left > 0.0:
		_lightning_left -= delta
		var intensity: Array = cloud.get("lightning_intensity", [0.05, 0.4])
		var energy := float(intensity[1]) if intensity.size() > 1 else 0.4
		if _flash_light != null:
			_flash_light.light_energy = energy if _lightning_left > 0.0 else 0.0
		return
	if _flash_light != null:
		_flash_light.light_energy = 0.0
	var chance := float(knobs.get("lightning_chance", 0.0))
	if weather_data_name in ["RAINY", "CLOUDYRAINY"]:
		chance = maxf(chance, float(cloud.get("lightning_chance", 0.0)))
	_lightning_timer += delta
	if _lightning_timer < 1.0:
		return
	_lightning_timer = 0.0
	if _rng.randf() > chance:
		return
	var duration_frames := int(knobs.get("lightning_duration_frames", 50))
	_lightning_left = float(duration_frames) / 30.0


func _sync_multimesh() -> void:
	if _multimesh_instance == null:
		return
	if _particles.is_empty():
		_multimesh_instance.multimesh = null
		_multimesh_instance.visible = false
		return
	_multimesh_instance.visible = true
	var mesh := QuadMesh.new()
	mesh.size = Vector2.ONE
	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.billboard_mode = BaseMaterial3D.BILLBOARD_ENABLED
	material.albedo_texture = _quad
	if active_kind == KIND_RAIN:
		material.albedo_color = Color(0.72, 0.78, 0.88, 0.55)
	else:
		material.albedo_color = Color(0.92, 0.95, 1.0, 0.70)
	mesh.material = material
	var multi := MultiMesh.new()
	multi.transform_format = MultiMesh.TRANSFORM_3D
	multi.mesh = mesh
	multi.instance_count = _particles.size()
	for index in _particles.size():
		var particle: Dictionary = _particles[index]
		var size := float(particle.get("size", 1.0))
		if active_kind == KIND_RAIN:
			size = Vector2(size * 0.15, size * 1.6).y
		var transform := Transform3D(Basis.IDENTITY.scaled(Vector3.ONE * maxf(size, 0.02)), Vector3(
			float(particle["x"]),
			float(particle["y"]),
			float(particle["z"])
		))
		multi.set_instance_transform(index, transform)
	_multimesh_instance.multimesh = multi


func _make_soft_quad() -> ImageTexture:
	var image := Image.create(16, 16, false, Image.FORMAT_RGBA8)
	var center := Vector2(7.5, 7.5)
	for y in 16:
		for x in 16:
			var distance := Vector2(float(x), float(y)).distance_to(center) / 8.0
			var alpha := clampf(1.0 - distance, 0.0, 1.0)
			image.set_pixel(x, y, Color(1.0, 1.0, 1.0, alpha * alpha))
	return ImageTexture.create_from_image(image)
