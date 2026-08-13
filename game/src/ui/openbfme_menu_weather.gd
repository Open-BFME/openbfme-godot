class_name OpenBFMEMenuWeather
extends Control
## Light motion over the painted menu plates. The stills stay the stills;
## this only draws what the picture already implies: leaves in an autumn vale,
## snow in a high pass, embers over Mordor, motes in a sun shaft.

const RECIPE_LEAVES := "leaves"
const RECIPE_SNOW := "snow"
const RECIPE_EMBERS := "embers"
const RECIPE_MOTES := "motes"
const RECIPE_ASH := "ash"
const RECIPE_MIST := "mist"
const RECIPE_SPARK := "spark"

var _recipe_id := ""
var _time := 0.0
var _particles: Array[Dictionary] = []
var _soft_blob: ImageTexture
var _rng := RandomNumberGenerator.new()


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_soft_blob = _make_soft_blob()
	set_process(true)


func set_source(path: String) -> void:
	var recipe_id := _recipe_from_path(path)
	if recipe_id == _recipe_id and not _particles.is_empty():
		return
	_recipe_id = recipe_id
	_rebuild()
	queue_redraw()


static func recipe_id_for(path: String) -> String:
	return _recipe_from_path(path)


func particle_count() -> int:
	return _particles.size()


func particle_kinds() -> PackedStringArray:
	var seen := {}
	for particle in _particles:
		seen[String(particle.get("kind", ""))] = true
	return PackedStringArray(seen.keys())


func _process(delta: float) -> void:
	if not is_visible_in_tree() or _particles.is_empty():
		return
	_time += delta
	_step(delta)
	queue_redraw()


func _draw() -> void:
	if size.x <= 1.0 or size.y <= 1.0:
		return
	_draw_ambient()
	for particle in _particles:
		_draw_particle(particle)


func _draw_ambient() -> void:
	match _recipe_id:
		"gorge_dawn", "argonath_haze":
			var pulse := 0.018 + 0.010 * sin(_time * 0.35)
			draw_rect(Rect2(Vector2.ZERO, size), Color(1.0, 0.96, 0.88, pulse))
		"rivendell_vale":
			var band := Rect2(size.x * 0.40, size.y * 0.38, size.x * 0.14, size.y * 0.34)
			var scroll := fmod(_time * 90.0, 48.0)
			for index in range(7):
				var y := band.position.y + fmod(scroll + float(index) * 22.0, band.size.y)
				var alpha := 0.045 * (1.0 - absf((y - band.get_center().y) / (band.size.y * 0.5)))
				draw_line(
					Vector2(band.position.x + band.size.x * 0.25, y),
					Vector2(band.end.x - band.size.x * 0.20, y + 10.0),
					Color(0.92, 0.96, 1.0, maxf(0.0, alpha)),
					1.2, true
				)
		"mordor_gate":
			var glow := 0.035 + 0.02 * sin(_time * 0.8)
			draw_rect(
				Rect2(0.0, size.y * 0.72, size.x, size.y * 0.28),
				Color(1.0, 0.28, 0.05, glow)
			)


func _draw_particle(particle: Dictionary) -> void:
	var pos := Vector2(float(particle["x"]) * size.x, float(particle["y"]) * size.y)
	var kind := String(particle["kind"])
	var radius := float(particle["size"]) * minf(size.x, size.y)
	var color: Color = particle["color"]
	var life_fade := _life_fade(float(particle["age"]), float(particle["life"]))
	color.a *= life_fade
	match kind:
		RECIPE_LEAVES:
			_draw_leaf(pos, radius, float(particle["rot"]), color)
		RECIPE_SNOW:
			if float(particle["size"]) > 0.004:
				var tip := pos + Vector2(float(particle["vx"]), float(particle["vy"])).normalized() * radius * 3.4
				draw_line(pos, tip, color, maxf(1.0, radius * 0.55), true)
			else:
				draw_texture_rect(_soft_blob, Rect2(pos - Vector2(radius, radius), Vector2(radius * 2.0, radius * 2.0)), false, color)
		RECIPE_EMBERS:
			var core := color.lightened(0.35)
			core.a = color.a
			draw_texture_rect(_soft_blob, Rect2(pos - Vector2(radius * 1.8, radius * 1.8), Vector2(radius * 3.6, radius * 3.6)), false, color)
			draw_circle(pos, maxf(0.6, radius * 0.35), core)
		RECIPE_MIST:
			var mist_size := Vector2(radius * 8.0, radius * 3.4)
			draw_texture_rect(_soft_blob, Rect2(pos - mist_size * 0.5, mist_size), false, color)
		_:
			draw_texture_rect(_soft_blob, Rect2(pos - Vector2(radius, radius), Vector2(radius * 2.0, radius * 2.0)), false, color)


func _draw_leaf(pos: Vector2, radius: float, rot: float, color: Color) -> void:
	var points := PackedVector2Array()
	var local := [
		Vector2(0.0, -radius * 1.6),
		Vector2(radius * 0.85, 0.0),
		Vector2(0.0, radius * 1.15),
		Vector2(-radius * 0.75, 0.05),
	]
	var cosine := cos(rot)
	var sine := sin(rot)
	for point in local:
		points.append(pos + Vector2(point.x * cosine - point.y * sine, point.x * sine + point.y * cosine))
	draw_colored_polygon(points, color)
	var vein := color.lightened(0.18)
	vein.a = color.a * 0.7
	draw_line(points[0], points[2], vein, 1.0, true)


func _step(delta: float) -> void:
	for index in range(_particles.size()):
		var particle: Dictionary = _particles[index]
		var kind := String(particle["kind"])
		var phase := float(particle["phase"])
		var flutter := sin(_time * float(particle["flutter"]) + phase)
		particle["x"] = float(particle["x"]) + (float(particle["vx"]) + flutter * float(particle["sway"])) * delta
		particle["y"] = float(particle["y"]) + float(particle["vy"]) * delta
		particle["rot"] = float(particle["rot"]) + float(particle["vr"]) * delta
		particle["age"] = float(particle["age"]) + delta
		if kind == RECIPE_EMBERS:
			particle["vy"] = float(particle["vy"]) - 0.012 * delta
		if _offscreen(particle) or float(particle["age"]) >= float(particle["life"]):
			_respawn(particle, true)
		_particles[index] = particle


func _offscreen(particle: Dictionary) -> bool:
	var x := float(particle["x"])
	var y := float(particle["y"])
	return x < -0.08 or x > 1.08 or y < -0.10 or y > 1.10


func _rebuild() -> void:
	_particles.clear()
	_rng.seed = hash(_recipe_id) & 0x7fffffff
	match _recipe_id:
		"rivendell_vale":
			_spawn_group(RECIPE_LEAVES, 34)
			_spawn_group(RECIPE_MOTES, 10)
			_spawn_group(RECIPE_SPARK, 8)
		"fortress_dusk":
			_spawn_group(RECIPE_LEAVES, 8)
			_spawn_group(RECIPE_MOTES, 16)
			_spawn_group(RECIPE_MIST, 4)
		"gorge_dawn":
			_spawn_group(RECIPE_MOTES, 28)
			_spawn_group(RECIPE_MIST, 3)
			_spawn_group(RECIPE_SPARK, 12)
		"misty_pass":
			_spawn_group(RECIPE_SNOW, 52)
			_spawn_group(RECIPE_MIST, 4)
		"argonath_haze":
			_spawn_group(RECIPE_MOTES, 18)
			_spawn_group(RECIPE_MIST, 5)
			_spawn_group(RECIPE_LEAVES, 5)
		"mordor_gate":
			_spawn_group(RECIPE_EMBERS, 30)
			_spawn_group(RECIPE_ASH, 22)
			_spawn_group(RECIPE_MIST, 3)
		_:
			_spawn_group(RECIPE_MOTES, 16)


func _spawn_group(kind: String, count: int) -> void:
	for _index in range(count):
		var particle := {"kind": kind}
		_respawn(particle, false)
		_particles.append(particle)


func _respawn(particle: Dictionary, wrapping: bool) -> void:
	var kind := String(particle.get("kind", RECIPE_MOTES))
	particle["kind"] = kind
	particle["phase"] = _rng.randf() * TAU
	particle["flutter"] = _rng.randf_range(0.6, 2.2)
	particle["age"] = 0.0 if wrapping else _rng.randf() * 8.0
	match kind:
		RECIPE_LEAVES:
			_fill_leaf(particle, wrapping)
		RECIPE_SNOW:
			_fill_snow(particle, wrapping)
		RECIPE_EMBERS:
			_fill_ember(particle, wrapping)
		RECIPE_ASH:
			_fill_ash(particle, wrapping)
		RECIPE_MIST:
			_fill_mist(particle, wrapping)
		RECIPE_SPARK:
			_fill_spark(particle, wrapping)
		_:
			_fill_mote(particle, wrapping)


func _fill_leaf(particle: Dictionary, wrapping: bool) -> void:
	var from_left := _rng.randf() < 0.55
	if wrapping:
		particle["y"] = -0.04
		particle["x"] = _rng.randf_range(0.02, 0.98)
	elif _recipe_id == "rivendell_vale":
		particle["x"] = _rng.randf_range(0.00, 0.28) if from_left else _rng.randf_range(0.70, 1.0)
		particle["y"] = _rng.randf_range(0.18, 0.78)
	else:
		particle["x"] = _rng.randf()
		particle["y"] = _rng.randf_range(0.0, 0.85)
	particle["vx"] = _rng.randf_range(0.012, 0.045)
	particle["vy"] = _rng.randf_range(0.035, 0.085)
	particle["sway"] = _rng.randf_range(0.015, 0.045)
	particle["vr"] = _rng.randf_range(-1.4, 1.4)
	particle["rot"] = _rng.randf() * TAU
	particle["size"] = _rng.randf_range(0.0045, 0.0095)
	particle["life"] = _rng.randf_range(10.0, 18.0)
	particle["color"] = [
		Color(0.78, 0.52, 0.14, 0.72),
		Color(0.86, 0.38, 0.12, 0.68),
		Color(0.92, 0.70, 0.22, 0.70),
		Color(0.62, 0.28, 0.10, 0.66),
	][_rng.randi() % 4]


func _fill_snow(particle: Dictionary, wrapping: bool) -> void:
	if wrapping:
		particle["x"] = _rng.randf_range(-0.05, 1.05)
		particle["y"] = -0.05
	else:
		particle["x"] = _rng.randf()
		particle["y"] = _rng.randf()
	particle["vx"] = _rng.randf_range(0.06, 0.16)
	particle["vy"] = _rng.randf_range(0.07, 0.18)
	particle["sway"] = _rng.randf_range(0.01, 0.03)
	particle["vr"] = 0.0
	particle["rot"] = 0.0
	particle["size"] = _rng.randf_range(0.0016, 0.0055)
	particle["life"] = _rng.randf_range(6.0, 12.0)
	particle["color"] = Color(0.90, 0.95, 1.0, _rng.randf_range(0.22, 0.55))


func _fill_ember(particle: Dictionary, wrapping: bool) -> void:
	if wrapping:
		particle["x"] = _rng.randf_range(0.05, 0.95)
		particle["y"] = 1.04
	else:
		particle["x"] = _rng.randf()
		particle["y"] = _rng.randf_range(0.45, 1.02)
	particle["vx"] = _rng.randf_range(-0.03, 0.05)
	particle["vy"] = _rng.randf_range(-0.12, -0.045)
	particle["sway"] = _rng.randf_range(0.02, 0.06)
	particle["vr"] = 0.0
	particle["rot"] = 0.0
	particle["size"] = _rng.randf_range(0.0018, 0.0048)
	particle["life"] = _rng.randf_range(4.0, 9.0)
	particle["color"] = [
		Color(1.0, 0.42, 0.08, 0.75),
		Color(1.0, 0.62, 0.16, 0.70),
		Color(0.95, 0.22, 0.04, 0.62),
	][_rng.randi() % 3]


func _fill_ash(particle: Dictionary, wrapping: bool) -> void:
	if wrapping:
		particle["x"] = _rng.randf()
		particle["y"] = -0.04
	else:
		particle["x"] = _rng.randf()
		particle["y"] = _rng.randf()
	particle["vx"] = _rng.randf_range(0.01, 0.05)
	particle["vy"] = _rng.randf_range(0.02, 0.055)
	particle["sway"] = _rng.randf_range(0.015, 0.04)
	particle["vr"] = _rng.randf_range(-0.6, 0.6)
	particle["rot"] = _rng.randf() * TAU
	particle["size"] = _rng.randf_range(0.0014, 0.0032)
	particle["life"] = _rng.randf_range(8.0, 16.0)
	particle["color"] = Color(0.18, 0.14, 0.12, _rng.randf_range(0.18, 0.40))


func _fill_mist(particle: Dictionary, wrapping: bool) -> void:
	if wrapping:
		particle["x"] = -0.08 if float(particle.get("vx", 0.02)) >= 0.0 else 1.08
		particle["y"] = _rng.randf_range(0.45, 0.88)
	else:
		particle["x"] = _rng.randf()
		particle["y"] = _rng.randf_range(0.42, 0.90)
	var leftward := _recipe_id == "misty_pass"
	particle["vx"] = _rng.randf_range(-0.018, -0.008) if leftward else _rng.randf_range(0.006, 0.016)
	particle["vy"] = _rng.randf_range(-0.004, 0.004)
	particle["sway"] = 0.004
	particle["vr"] = 0.0
	particle["rot"] = 0.0
	particle["size"] = _rng.randf_range(0.045, 0.09)
	particle["life"] = _rng.randf_range(16.0, 28.0)
	if _recipe_id == "mordor_gate":
		particle["color"] = Color(0.45, 0.16, 0.06, 0.07)
		particle["y"] = _rng.randf_range(0.55, 0.92)
	elif _recipe_id == "misty_pass":
		particle["color"] = Color(0.72, 0.84, 0.86, 0.08)
	else:
		particle["color"] = Color(0.86, 0.90, 0.92, 0.06)


func _fill_mote(particle: Dictionary, wrapping: bool) -> void:
	if wrapping:
		particle["x"] = _rng.randf()
		particle["y"] = 1.04 if float(particle.get("vy", -0.01)) < 0.0 else -0.04
	else:
		particle["x"] = _rng.randf_range(0.18, 0.82)
		particle["y"] = _rng.randf_range(0.08, 0.72)
	particle["vx"] = _rng.randf_range(-0.01, 0.018)
	particle["vy"] = _rng.randf_range(-0.012, 0.008)
	particle["sway"] = _rng.randf_range(0.006, 0.018)
	particle["vr"] = 0.0
	particle["rot"] = 0.0
	particle["size"] = _rng.randf_range(0.0012, 0.0028)
	particle["life"] = _rng.randf_range(8.0, 16.0)
	if _recipe_id == "fortress_dusk":
		particle["color"] = Color(1.0, 0.78, 0.38, 0.28)
	elif _recipe_id == "rivendell_vale":
		particle["color"] = Color(1.0, 0.88, 0.45, 0.26)
	else:
		particle["color"] = Color(0.95, 0.97, 1.0, 0.22)


func _fill_spark(particle: Dictionary, wrapping: bool) -> void:
	if wrapping:
		particle["x"] = _rng.randf_range(0.36, 0.56)
		particle["y"] = 0.36
	else:
		particle["x"] = _rng.randf_range(0.36, 0.56)
		particle["y"] = _rng.randf_range(0.38, 0.72)
	particle["vx"] = _rng.randf_range(-0.01, 0.01)
	particle["vy"] = _rng.randf_range(0.10, 0.22)
	particle["sway"] = 0.01
	particle["vr"] = 0.0
	particle["rot"] = 0.0
	particle["size"] = _rng.randf_range(0.0010, 0.0022)
	particle["life"] = _rng.randf_range(1.6, 3.4)
	particle["color"] = Color(0.92, 0.96, 1.0, 0.28)


func _life_fade(age: float, life: float) -> float:
	if life <= 0.08:
		return 1.0
	var t := clampf(age / life, 0.0, 1.0)
	if t < 0.12:
		return t / 0.12
	if t > 0.82:
		return (1.0 - t) / 0.18
	return 1.0


static func _recipe_from_path(path: String) -> String:
	var stem := path.get_file().get_basename().to_lower()
	if stem.begins_with("backdrop_"):
		stem = stem.substr("backdrop_".length())
	return stem


static func _make_soft_blob() -> ImageTexture:
	var image := Image.create(32, 32, false, Image.FORMAT_RGBA8)
	var centre := Vector2(15.5, 15.5)
	for y in range(32):
		for x in range(32):
			var distance := Vector2(float(x), float(y)).distance_to(centre) / 15.5
			var alpha := clampf(1.0 - distance, 0.0, 1.0)
			alpha *= alpha
			image.set_pixel(x, y, Color(1, 1, 1, alpha))
	return ImageTexture.create_from_image(image)
