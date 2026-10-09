## W3D viewer: mounts pure RotWK 2.01 and draws retail models through the W3DInstancer (GPU skinning, one MultiMesh per
## HLOD sub object, no Skeleton3D). The Gondor soldier plays its idle clip in front of a row of five other models.
##
## Drag to orbit, wheel to zoom.
##   -- --shots=<dir>      write w3d2-animated.png and three frames at different animation times, then quit
##   -- --bench[=<count>]  1,000 (or <count>) animated soldiers: print frame time and fps over 300 frames, then quit
##   -- --threads=<n>      worker threads for the pose evaluation (default: the instancer's choice)
extends Node3D

const SOLDIER := "GUMAArms_SKN"
const SOLDIER_IDLE := "GUManMocap_IDLB"
const SOLDIER_RUN := "GUManMocap_RUNB"

var _fs: RefCounted
var _inst: Node3D
var _camera: Camera3D
var _label: Label
var _yaw := deg_to_rad(18.0)
var _pitch := deg_to_rad(-14.0)
var _distance := 250.0
var _target := Vector3(0, 26, 10)
var _shots_dir := ""
var _bench := false
var _bench_count := 1000
var _frames := 0
var _frame_times: Array[float] = []
var _bench_start_usec := 0
var _last_usec := 0
var _step := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			_shots_dir = arg.substr("--shots=".length())
		elif arg == "--probe":
			_probe = true
		elif arg == "--bench":
			_bench = true
		elif arg.begins_with("--bench="):
			_bench = true
			_bench_count = int(arg.substr("--bench=".length()))
		elif arg.begins_with("--threads="):
			_threads = int(arg.substr("--threads=".length()))
	_build_stage()
	if not ClassDB.class_exists("W3DInstancer"):
		_fail("openbfme extension is not loaded; run build.bat")
		return
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
	if not mount.ok:
		_fail("mount failed:\n" + "\n".join(mount.errors))
		return
	_inst = ClassDB.instantiate("W3DInstancer")
	add_child(_inst)
	var setup: Dictionary = _inst.setup(_fs)
	if not setup.ok:
		_fail("instancer setup failed: " + str(setup.errors))
		return
	if _threads > 0:
		_inst.set_worker_threads(_threads)
	if _probe:
		_build_probe()
	elif _bench:
		_build_bench()
	else:
		_build_showcase()


var _threads := 0
var _probe := false
var _text := ""


func _model(name: String) -> int:
	var id: int = _inst.add_model(name)
	if id < 0:
		_fail("model %s failed: %s" % [name, str(_inst.get_errors())])
		return -1
	var report: Dictionary = _inst.get_model_report(id)
	var line := "%s: %d pivots, %d draw items, %d surfaces" % [name, report.pivots, report.draw_items, report.surfaces]
	print("[w3d viewer] ", line)
	for n in report.notes:
		print("[w3d viewer]   note: ", n)
	for e in report.errors:
		push_error("[w3d viewer] " + e)
	_text += line + "\n"
	return id


func _build_showcase() -> void:
	var soldier := _model(SOLDIER)
	var hero := _model("GUAragorn_SKN")
	var cavalry := _model("GUCavalry_SKN")
	var barracks := _model("GBBarracks_SKN")
	var tree := _model("PTree08")
	var crate := _model("PCrate")
	if min(soldier, hero, cavalry, barracks, tree, crate) < 0:
		return

	var info: Dictionary = _inst.get_clip_info(soldier, SOLDIER_IDLE)
	print("[w3d viewer] soldier idle clip: ", info)
	if not info.ok:
		_fail("idle clip: " + str(info.get("error", "?")))
		return
	_text += "idle clip %s: %d frames at %d fps\n" % [info.name, info.frames, info.frame_rate]

	# a W3D unit faces +X; Godot keeps +X, so a yaw of -90 degrees turns it toward the camera on +Z
	var face := Basis(Vector3.UP, deg_to_rad(-60))
	# the soldier, in front
	_inst.add_instance(soldier, Transform3D(face, Vector3(0, 0, 70)), SOLDIER_IDLE, 0.0, 1.0)
	_inst.add_instance(soldier, Transform3D(face, Vector3(-24, 0, 70)), SOLDIER_RUN, 0.2, 1.0)
	_inst.add_instance(soldier, Transform3D(face, Vector3(24, 0, 70)), SOLDIER_IDLE, 1.7, 1.0)
	# the row behind: hero, cavalry, barracks, tree, prop
	var row_z := 15.0
	_inst.add_instance(hero, Transform3D(face, Vector3(-120, 0, row_z)), "GUAragorn_SKL.GUAragorn_IDLA", 0.0, 1.0)
	_inst.add_instance(cavalry, Transform3D(face, Vector3(-70, 0, row_z)), "GUCavalry_IDLA", 0.0, 1.0)
	_inst.add_instance(barracks, Transform3D(Basis(Vector3.UP, deg_to_rad(-30)), Vector3(5, 0, row_z - 45)), "", 0.0, 1.0)
	_inst.add_instance(crate, Transform3D(face, Vector3(78, 0, row_z)), "", 0.0, 1.0)
	_inst.add_instance(tree, Transform3D(Basis(), Vector3(125, 0, row_z - 15)), "", 0.0, 1.0)
	_text += "row: Aragorn (idle), cavalry (idle), barracks, tree, crate"
	_label.text = _text
	_inst.set_playing(_shots_dir.is_empty())


## Shader coverage: one probe mesh per distinct generated shader permutation of the whole corpus. Godot prints a shader
## error for every permutation that does not compile; this mode only draws them and counts frames.
func _build_probe() -> void:
	var result: Dictionary = _inst.probe_shaders(Vector3(0, 0, 0))
	print("[w3d probe] ", result)
	_distance = 900.0
	_pitch = deg_to_rad(-60)
	_yaw = 0.0
	_target = Vector3(600, 0, 200)
	_update_camera()
	_label.text = "shader probe: %s permutations" % str(result.get("permutations", "?"))


func _build_bench() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	var soldier := _model(SOLDIER)
	if soldier < 0:
		return
	var cols := 50
	var rows := int(ceil(float(_bench_count) / cols))
	var spacing := 14.0
	var rng := RandomNumberGenerator.new()
	rng.seed = 20260930
	var face := Basis(Vector3.UP, deg_to_rad(-90))
	var idle: Dictionary = _inst.get_clip_info(soldier, SOLDIER_IDLE)
	var run: Dictionary = _inst.get_clip_info(soldier, SOLDIER_RUN)
	print("[w3d bench] clips: ", idle, " ", run)
	for i in _bench_count:
		var x := (i % cols - cols / 2.0) * spacing
		var z := (i / cols - rows / 2.0) * spacing
		var clip := SOLDIER_RUN if (i % 2) == 0 else SOLDIER_IDLE
		_inst.add_instance(soldier, Transform3D(face, Vector3(x, 0, z)), clip, rng.randf() * 4.0, 0.9 + rng.randf() * 0.2)
	_distance = 520.0
	_pitch = deg_to_rad(-40)
	_yaw = 0.0
	_target = Vector3(0, 0, 0)
	_update_camera()
	_label.text = "benchmark: %d animated soldiers" % _bench_count
	_inst.update_now()
	print("[w3d bench] instancer stats after the first update: ", _inst.get_stats())
	_bench_start_usec = Time.get_ticks_usec()
	_last_usec = _bench_start_usec


func _build_stage() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0) # the base game shows black outside the map (owner, 2026-10-06)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.62, 0.62, 0.66)
	env.ambient_light_energy = 0.8
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-52, -35, 0)
	sun.light_energy = 1.3
	add_child(sun)

	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(9000, 9000)
	ground.mesh = plane
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.30, 0.34, 0.26)
	mat.roughness = 1.0
	ground.material_override = mat
	ground.position = Vector3(0, -0.2, 0)
	add_child(ground)

	_camera = Camera3D.new()
	_camera.fov = 38
	_camera.far = 6000
	add_child(_camera)
	_update_camera()

	var ui := CanvasLayer.new()
	_label = Label.new()
	_label.position = Vector2(12, 8)
	ui.add_child(_label)
	add_child(ui)


func _fail(message: String) -> void:
	push_error(message)
	_label.text = "ERROR: " + message
	_label.modulate = Color(1, 0.4, 0.4)
	if not _shots_dir.is_empty() or _bench:
		get_tree().quit(1)


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


func _process(_delta: float) -> void:
	if _inst == null:
		return
	_frames += 1
	if _probe:
		if _frames == 90:
			print("[w3d probe] drew 90 frames; errors: ", _inst.get_errors())
			get_tree().quit(0)
		return
	if _bench:
		_process_bench()
	elif not _shots_dir.is_empty():
		_process_shots()


# Shots: the full view at t = 0, then a close view of the soldiers at three animation times (the clock is pinned, so each
# image is exactly that pose). The differences between the close frames are printed.
const SHOT_TIMES := [0.0, 1.6, 3.2]
var _shot_images: Array[Image] = []


func _grab() -> Image:
	return get_viewport().get_texture().get_image()


func _process_shots() -> void:
	if _frames == 20:
		_inst.set_global_time(0.0)
		_inst.update_now()
	elif _frames == 24:
		if not _save_image(_grab(), "%s/w3d2-animated.png" % _shots_dir):
			get_tree().quit(1)
		_distance = 80.0
		_pitch = deg_to_rad(-6)
		_yaw = deg_to_rad(8)
		_target = Vector3(0, 9, 70)
		_update_camera()
	elif _frames >= 30:
		var phase := (_frames - 30) / 6
		var sub := (_frames - 30) % 6
		if phase >= SHOT_TIMES.size():
			for i in range(1, _shot_images.size()):
				var diff := _count_different(_shot_images[0], _shot_images[i])
				print("[w3d viewer] close frame %d differs from frame 0 in %d pixels" % [i, diff])
			get_tree().quit(0)
		elif sub == 0:
			_inst.set_global_time(SHOT_TIMES[phase])
			_inst.update_now()
		elif sub == 4:
			var img := _grab()
			_shot_images.append(img)
			if not _save_image(img, "%s/w3d2-anim-%d.png" % [_shots_dir, phase]):
				get_tree().quit(1)


func _save_image(image: Image, path: String) -> bool:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var err := image.save_png(path)
	print("[w3d viewer] screenshot %s (%dx%d) -> %s" % [path, image.get_width(), image.get_height(), error_string(err)])
	return err == OK


func _count_different(a: Image, b: Image) -> int:
	var n := 0
	var da := a.get_data()
	var db := b.get_data()
	for i in range(0, da.size(), 4):
		if da[i] != db[i] or da[i + 1] != db[i + 1] or da[i + 2] != db[i + 2]:
			n += 1
	return n


func _process_bench() -> void:
	var now := Time.get_ticks_usec()
	var dt := float(now - _last_usec) / 1000.0
	_last_usec = now
	if _frames == 100 and not _shots_dir.is_empty():
		_save_image(_grab(), "%s/w3d2-bench.png" % _shots_dir)
	# warm up 60 frames (shader compilation, texture upload), then measure 300
	if _frames > 60 and _frames <= 360:
		_frame_times.append(dt)
	if _frames == 360:
		_frame_times.sort()
		var total := 0.0
		for t in _frame_times:
			total += t
		var avg := total / _frame_times.size()
		var p95: float = _frame_times[int(_frame_times.size() * 0.95)]
		var stats: Dictionary = _inst.get_stats()
		var size := get_viewport().get_visible_rect().size
		print("[w3d bench] %d soldiers, %dx%d, %d frames: avg %.2f ms (%.1f fps), median %.2f ms, p95 %.2f ms, worst %.2f ms" % [
			_bench_count, size.x, size.y, _frame_times.size(), avg, 1000.0 / avg, _frame_times[_frame_times.size() / 2], p95, _frame_times[-1]])
		print("[w3d bench] instancer: ", stats)
		print("[w3d bench] errors: ", _inst.get_errors())
		get_tree().quit(0)
