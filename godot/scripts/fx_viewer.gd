## FX viewer: plays any retail FXList or FXParticleSystem on a ground plane, drawn by the batched FXPlayer (simulation in C++ at a fixed
## 30 Hz, MultiMesh / GPU-particle shader rendering). Pick an effect from the list, press Play (or double-click); Space pauses, R replays,
## the wheel zooms, dragging orbits.
##
##   -- --fx=<name>          play this FXList on start
##   -- --ps=<name>          play this FXParticleSystem on start
##   -- --em=<name>          play this W3D model emitter on start (e_fire_sm, e_firesparks_sm, e_mumaflies, e_smoke_sm)
##   -- --object             play the FXList on a dummy object instead of at a position
##   -- --shots=<dir>        render the preset effect shots to <dir>/fx1-<name>.png, then quit
##   -- --shotlist=<file>    with --shots: take the shots from a file (one per line: name fx|ps retail_name frames distance pitch target_height [target_x])
##   -- --shot=<out.png> --frames=<n>   with --fx / --ps: step n client frames, save one screenshot, quit
##   -- --bench=<count>      play <count> particle systems at once, print the frame time over 300 frames, then quit
##   -- --bench-ps=<name>    (lane FX-3 round 2) with --bench: every system is <name> instead of a random mix
##   -- --soft-particles=on|off  (lane FX-3 round 2) the FXPlayer's soft particles (default: the project setting openbfme/rendering/soft_particles)
extends Node3D

# (screenshot name, kind fx|ps, retail name, frames to simulate, camera distance, camera pitch degrees, camera target height, camera target x)
const SHOTS := [
	["fire", "ps", "burningTreeFire", 45, 130.0, -12.0, 40.0, 0.0],
	["spell", "fx", "FX_GandalfLightningSwordBlastWeapon", 20, 420.0, -14.0, 20.0, -200.0],
	["explosion", "fx", "FX_OilBarrelExplosion", 10, 260.0, -12.0, 40.0, 0.0],
	["smoke", "ps", "CampFireSmoke", 80, 140.0, -12.0, 35.0, 0.0],
	["collapse", "fx", "FX_StructureMediumCollapse", 20, 600.0, -12.0, 100.0, 0.0],
	["stack", "stack", "CampFireSmoke+burningTreeFire+CampFireSmoke", 70, 130.0, -12.0, 40.0, 0.0],
	["w3d-fire", "em", "e_fire_sm", 90, 14.0, -10.0, 4.0, 0.0],
	["w3d-smoke", "em", "e_smoke_sm", 240, 60.0, -10.0, 25.0, 0.0],
	["w3d-flies", "em", "e_mumaflies", 90, 90.0, -10.0, 15.0, 0.0],
]

const EMITTERS := ["e_fire_sm", "e_firesparks_sm", "e_mumaflies", "e_smoke_sm"]

var _fs: RefCounted
var _fx: Node3D
var _camera: Camera3D
var _label: Label
var _list: ItemList
var _filter: LineEdit
var _mode: OptionButton
var _object_check: CheckBox
var _loop_check: CheckBox
var _yaw := deg_to_rad(20.0)
var _pitch := deg_to_rad(-18.0)
var _distance := 220.0
var _target := Vector3(0, 28, 0)
var _names_fx: PackedStringArray
var _names_ps: PackedStringArray
var _current := ""
var _current_is_fx := true
var _frames := 0
var _shots_dir := ""
var _shot_out := ""
var _shot_frames := 0
var _start_fx := ""
var _start_ps := ""
var _start_em := ""
var _bench := 0
var _bench_ps := ""   # lane FX-3 round 2
var _soft_arg := ""   # lane FX-3 round 2
var _bench_times: Array[float] = []
var _as_object := false
var _loop_timer := 0.0
var _shot_index := 0
var _shot_state := 0
var _shot_wait := 0
var _fixed_step := false
var _shots: Array = []
var _bench_spawns: Array = []


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--fx="):
			_start_fx = arg.substr(5)
		elif arg.begins_with("--ps="):
			_start_ps = arg.substr(5)
		elif arg.begins_with("--em="):
			_start_em = arg.substr(5)
		elif arg == "--object":
			_as_object = true
		elif arg.begins_with("--shots="):
			_shots_dir = arg.substr(8)
		elif arg.begins_with("--shotlist="):
			_load_shotlist(arg.substr(11))
		elif arg.begins_with("--shot="):
			_shot_out = arg.substr(7)
		elif arg.begins_with("--frames="):
			_shot_frames = int(arg.substr(9))
		elif arg.begins_with("--bench="):
			_bench = int(arg.substr(8))
		elif arg.begins_with("--bench-ps="):
			_bench_ps = arg.substr(11)
		elif arg.begins_with("--soft-particles="):
			_soft_arg = arg.substr(17)
	_fixed_step = not _shots_dir.is_empty() or not _shot_out.is_empty()
	if _shots.is_empty():
		_shots = SHOTS
	_build_stage()
	if not ClassDB.class_exists("FXPlayer"):
		_fail("openbfme extension is not loaded; run build.bat")
		return
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
	if not mount.ok:
		_fail("mount failed:\n" + "\n".join(mount.errors))
		return
	_fx = ClassDB.instantiate("FXPlayer")
	add_child(_fx)
	if not _soft_arg.is_empty():
		_fx.soft_particles = _soft_arg == "on"
	print("[fx viewer] soft particles ", _fx.soft_particles)
	var setup: Dictionary = _fx.setup(_fs)
	print("[fx viewer] setup: ", setup.get("particle_systems", 0), " particle systems, ", setup.get("fx_lists", 0), " FXLists")
	if not setup.ok:
		_fail("FX data failed to load: " + str(setup.errors))
		return
	_names_fx = _fx.list_fx_lists()
	_names_ps = _fx.list_particle_systems()
	_refill()
	if _bench > 0:
		_build_bench()
	elif _shot_out.is_empty():
		if not _start_fx.is_empty():
			_play(_start_fx, true)
		elif not _start_ps.is_empty():
			_play(_start_ps, false)


func _build_stage() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0) # the base game shows black outside the map (owner, 2026-10-06)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.6, 0.62)
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-52, -35, 0)
	sun.light_energy = 1.0
	add_child(sun)
	# the ground plane is the plane z = 0 of SAGE space (y = 0 here); a chequer makes depth readable
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(4000, 4000)
	ground.mesh = plane
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.26, 0.29, 0.22)
	mat.roughness = 1.0
	ground.material_override = mat
	ground.position = Vector3(0, -0.05, 0)
	add_child(ground)
	var grid := MeshInstance3D.new()
	var im := ImmediateMesh.new()
	im.surface_begin(Mesh.PRIMITIVE_LINES)
	for i in range(-20, 21):
		im.surface_add_vertex(Vector3(i * 50, 0, -1000))
		im.surface_add_vertex(Vector3(i * 50, 0, 1000))
		im.surface_add_vertex(Vector3(-1000, 0, i * 50))
		im.surface_add_vertex(Vector3(1000, 0, i * 50))
	im.surface_end()
	grid.mesh = im
	var gmat := StandardMaterial3D.new()
	gmat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	gmat.albedo_color = Color(0.36, 0.40, 0.32)
	grid.material_override = gmat
	add_child(grid)
	_camera = Camera3D.new()
	_camera.fov = 40
	_camera.far = 8000
	add_child(_camera)
	_update_camera()

	var ui := CanvasLayer.new()
	var panel := PanelContainer.new()
	panel.position = Vector2(8, 8)
	panel.custom_minimum_size = Vector2(300, 640)
	var box := VBoxContainer.new()
	panel.add_child(box)
	_label = Label.new()
	_label.text = "loading..."
	box.add_child(_label)
	_mode = OptionButton.new()
	_mode.add_item("FXList", 0)
	_mode.add_item("FXParticleSystem", 1)
	_mode.add_item("W3D emitter", 2)
	_mode.item_selected.connect(func(_i): _refill())
	box.add_child(_mode)
	_filter = LineEdit.new()
	_filter.placeholder_text = "filter (substring)"
	_filter.text_changed.connect(func(_t): _refill())
	box.add_child(_filter)
	_list = ItemList.new()
	_list.custom_minimum_size = Vector2(280, 420)
	_list.item_activated.connect(func(i): _play_selected(i))
	_list.item_selected.connect(func(i): _play_selected(i))
	box.add_child(_list)
	_object_check = CheckBox.new()
	_object_check.text = "play on an object (doFXObj)"
	_object_check.button_pressed = _as_object
	box.add_child(_object_check)
	_loop_check = CheckBox.new()
	_loop_check.text = "loop every 6 s"
	_loop_check.button_pressed = true
	box.add_child(_loop_check)
	var row := HBoxContainer.new()
	for spec in [["Play", "_on_play"], ["Pause", "_on_pause"], ["Clear", "_on_clear"]]:
		var b := Button.new()
		b.text = spec[0]
		b.pressed.connect(Callable(self, spec[1]))
		row.add_child(b)
	box.add_child(row)
	ui.add_child(panel)
	add_child(ui)
	if not _shots_dir.is_empty() or not _shot_out.is_empty() or _bench > 0:
		panel.visible = false


func _load_shotlist(path: String) -> void:
	for line in FileAccess.get_file_as_string(path).split("\n"):
		var t := line.strip_edges()
		if t.is_empty() or t.begins_with("#"):
			continue
		var f := t.split(" ", false)
		_shots.append([f[0], f[1], f[2], int(f[3]), float(f[4]), float(f[5]), float(f[6]), float(f[7]) if f.size() > 7 else 0.0])


func _fail(message: String) -> void:
	push_error(message)
	_label.text = "ERROR: " + message
	if _fixed_step or _bench > 0:
		get_tree().quit(1)


func _refill() -> void:
	_list.clear()
	var names := _names_fx if _mode.selected == 0 else (_names_ps if _mode.selected == 1 else PackedStringArray(EMITTERS))
	var f := _filter.text.to_lower()
	var shown := 0
	for n in names:
		if f.is_empty() or n.to_lower().contains(f):
			_list.add_item(n)
			shown += 1
			if shown >= 4000:
				break


func _play_selected(index: int) -> void:
	if _mode.selected == 2:
		_play_emitter(_list.get_item_text(index))
	else:
		_play(_list.get_item_text(index), _mode.selected == 0)


func _play_emitter(effect: String) -> void:
	_current = effect
	_fx.clear()
	_loop_timer = 0.0
	var r: Dictionary = _fx.play_w3d_emitter(effect, Vector3.ZERO)
	if not r.ok:
		_label.text = "emitter " + effect + ": " + str(r.error)
		push_error("emitter %s: %s" % [effect, r.error])
		return
	print("[fx viewer] playing W3D emitter ", effect)


func _play(effect: String, is_fx: bool) -> void:
	_current = effect
	_current_is_fx = is_fx
	_fx.clear()
	_loop_timer = 0.0
	var as_object := _object_check.button_pressed if _object_check else _as_object
	if is_fx:
		if not _fx.play_fx_list(effect, Vector3.ZERO, as_object):
			_label.text = "unknown FXList " + effect
			return
	else:
		if _fx.play_particle_system(effect, Vector3.ZERO) == 0:
			_label.text = "unknown particle system " + effect
			return
	print("[fx viewer] playing ", "FXList " if is_fx else "particle system ", effect)


func _on_play() -> void:
	if _current.is_empty():
		return
	if EMITTERS.has(_current):
		_play_emitter(_current)
	else:
		_play(_current, _current_is_fx)


func _on_pause() -> void:
	_fx.set_paused(not _fx.is_paused())


func _on_clear() -> void:
	_fx.clear()


func _update_camera() -> void:
	var offset := Vector3(0, 0, _distance).rotated(Vector3.RIGHT, _pitch).rotated(Vector3.UP, _yaw)
	_camera.position = _target + offset
	_camera.look_at(_target)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and event.button_mask & (MOUSE_BUTTON_MASK_LEFT | MOUSE_BUTTON_MASK_RIGHT):
		_yaw -= event.relative.x * 0.01
		_pitch = clamp(_pitch - event.relative.y * 0.01, -1.4, 1.4)
		_update_camera()
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			_distance = max(20.0, _distance * 0.9)
			_update_camera()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_distance = min(4000.0, _distance * 1.1)
			_update_camera()
	elif event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_SPACE:
			_on_pause()
		elif event.keycode == KEY_R:
			_on_play()


func _build_bench() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	# a mix of retail systems spread over a grid: CPU sprite systems and GPU systems
	var names := ["FireSmall", "BuildingFire", "SmokeBlack", "Dust", "FXFireGPU"]
	var pool: Array = []
	for n in _names_ps:
		pool.append(n)
	var rng := RandomNumberGenerator.new()
	rng.seed = 20261001
	var cols := int(ceil(sqrt(_bench)))
	for i in _bench:
		var n: String = _bench_ps if not _bench_ps.is_empty() else pool[rng.randi() % pool.size()]
		var p := Vector3((i % cols - cols / 2.0) * 70.0, 0, (i / cols - cols / 2.0) * 70.0)
		_bench_spawns.append([n, p])
		_fx.play_particle_system(n, p)
	_distance = 100.0 * cols * 0.7
	_pitch = deg_to_rad(-50)
	_target = Vector3.ZERO
	_update_camera()
	_label.text = "bench: %d systems" % _bench


func _process(delta: float) -> void:
	if _fx == null:
		return
	_frames += 1
	if _bench > 0:
		_fx.advance(delta)
		if _frames % 90 == 0:
			for sp in _bench_spawns: # finite systems end: keep the load up by respawning every 3 s
				_fx.play_particle_system(sp[0], sp[1])
		if _frames > 60 and _frames <= 360:
			_bench_times.append(delta * 1000.0)
			if _frames == 200:
				print("[fx bench] mid-run stats: ", _fx.get_stats())
		if _frames == 360:
			_bench_times.sort()
			var total := 0.0
			for t in _bench_times:
				total += t
			var avg := total / _bench_times.size()
			print("[fx bench] %d systems: avg %.2f ms (%.1f fps), median %.2f ms, p95 %.2f ms, worst %.2f ms; stats %s" % [
				_bench, avg, 1000.0 / avg, _bench_times[_bench_times.size() / 2], _bench_times[int(_bench_times.size() * 0.95)], _bench_times[-1], str(_fx.get_stats())])
			get_tree().quit(0)
		return
	if _fixed_step:
		_fx.advance(1.0 / 30.0)
		_process_shots()
		return
	_fx.advance(delta)
	if _loop_check and _loop_check.button_pressed and not _current.is_empty():
		_loop_timer += delta
		if _loop_timer > 6.0:
			_on_play()
	var s: Dictionary = _fx.get_stats()
	_label.text = "%s\nframe %d  systems %d  particles %d\nbatches %d  sprites %d  gpu %d  mesh verts %d\nstep %.0f us  build %.0f us  upload %.0f us" % [
		_current if not _current.is_empty() else "(choose an effect)", s.frame, s.systems, s.particles, s.batches, s.sprites, s.gpu_instances, s.mesh_vertices,
		s.step_us, s.build_us, s.upload_us]


func _save(path: String) -> bool:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var image := get_viewport().get_texture().get_image()
	var err := image.save_png(path)
	print("[fx viewer] screenshot %s (%dx%d) -> %s" % [path, image.get_width(), image.get_height(), error_string(err)])
	return err == OK


func _process_shots() -> void:
	if not _shot_out.is_empty():
		# single effect: start it on frame 1, step the requested frames, save, quit
		if _frames == 1:
			if not _start_fx.is_empty():
				_play(_start_fx, true)
			elif not _start_ps.is_empty():
				_play(_start_ps, false)
			elif not _start_em.is_empty():
				_play_emitter(_start_em)
		if _frames == _shot_frames + 3:
			var ok := _save(_shot_out)
			print("[fx viewer] stats ", _fx.get_stats(), " unverified ", _fx.get_unverified())
			get_tree().quit(0 if ok else 1)
		return
	# preset shots
	if _shot_index >= _shots.size():
		print("[fx viewer] unverified: ", _fx.get_unverified())
		get_tree().quit(0)
		return
	var shot: Array = _shots[_shot_index]
	if _shot_state == 0:
		var is_fx: bool = shot[1] == "fx"
		var names := _names_fx if is_fx else (PackedStringArray(EMITTERS) if shot[1] == "em" else _names_ps)
		if shot[1] == "stack":
			names = PackedStringArray(String(shot[2]).split("+"))
			for n in names:
				if not _names_ps.has(n):
					push_error("preset %s: no such particle system %s" % [shot[0], n])
					_shot_index += 1
					return
		elif not names.has(shot[2]):
			push_error("preset %s: no such effect %s" % [shot[0], shot[2]])
			_shot_index += 1
			return
		_distance = shot[4]
		_pitch = deg_to_rad(shot[5])
		_target = Vector3(shot[7], shot[6], 0)
		_update_camera()
		if shot[1] == "stack":
			# overlapping systems played in order at one place: smoke (alpha), fire (additive), smoke again; alpha and additive do not commute,
			# so the stack shows the compositing order (the first and last systems share one batch state but are separate draws)
			_fx.clear()
			for n in String(shot[2]).split("+"):
				_fx.play_particle_system(n, Vector3.ZERO)
		elif shot[1] == "em":
			_play_emitter(shot[2])
		else:
			_play(shot[2], is_fx)
		_shot_wait = shot[3]
		_shot_state = 1
	elif _shot_state == 1:
		_shot_wait -= 1
		if _shot_wait <= 0:
			_shot_state = 2
	elif _shot_state == 2:
		_shot_state = 3 # one more redraw so the last step is on screen
	elif _shot_state == 3:
		var path := "%s/fx1-%s.png" % [_shots_dir, shot[0]]
		if not _save(path):
			get_tree().quit(1)
			return
		print("[fx viewer] %s: stats %s" % [shot[0], str(_fx.get_stats())])
		_shot_index += 1
		_shot_state = 0
