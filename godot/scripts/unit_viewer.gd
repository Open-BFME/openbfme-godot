## First-unit viewer: mounts pure RotWK 2.01, loads one retail unit (Gondor soldier)
## through the openbfme extension and shows it in bind pose with an orbit camera.
##
## Drag with the left/right mouse button to orbit, wheel to zoom.
## `-- --screenshot=<path>` saves the viewport to <path> after a few frames and quits.
## `-- --model=<W3D name>` (lane UI-1) shows that model instead (e.g. RBBarracks, whose RBBarracks.tga no archive holds: retail's missing texture)
## `-- --distance=<d>` the camera distance (default 40)
extends Node3D

const UNIT_INI := "data\\ini\\object\\goodfaction\\units\\men\\gondorfighter.ini"
const UNIT_OBJECT := "GondorFighter"

var _clock: RefCounted
var _camera: Camera3D
var _label: Label
var _yaw := deg_to_rad(35.0)
var _pitch := deg_to_rad(-12.0)
var _distance := 40.0
var _target := Vector3(0, 9, 0)
var _screenshot_path := ""
var _frames := 0
var _model := ""


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--screenshot="):
			_screenshot_path = arg.substr("--screenshot=".length())
		elif arg.begins_with("--model="):
			_model = arg.substr("--model=".length())
		elif arg.begins_with("--distance="):
			_distance = float(arg.substr("--distance=".length()))
			_target = Vector3(0, _distance * 0.2, 0)

	_build_stage()
	if not ClassDB.class_exists("RetailFileSystem"):
		_fail("openbfme extension is not loaded; run build.bat")
		return
	_clock = ClassDB.instantiate("LogicClock")

	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		_fail("mount failed:\n" + "\n".join(mount.errors))
		return
	var model := _model
	if model.is_empty():
		var ini_bytes: PackedByteArray = fs.read_file(UNIT_INI)
		var ini := ini_bytes.get_string_from_utf8()
		model = RetailIni.find_default_model(ini, UNIT_OBJECT)
	if model.is_empty():
		_fail("no DefaultModelConditionState Model for %s in %s" % [UNIT_OBJECT, UNIT_INI])
		return
	var builder: RefCounted = ClassDB.instantiate("W3DModelBuilder")
	var unit: Node3D = builder.build_model(fs, model)
	var report: Dictionary = builder.get_report()
	if unit == null or (not report.errors.is_empty() and _model.is_empty()):
		_fail("model %s: %s" % [model, "\n".join(report.errors)])
		if unit == null:
			return
	add_child(unit)
	if not _model.is_empty():
		_label.text = "%s: %s" % [model, "; ".join(report.errors)]
		print("UNIT VIEW ", model, " errors ", report.errors)
		return
	_label.text = "%s (%s)  %d meshes, hierarchy %s" % [UNIT_OBJECT, model, report.meshes.size(), report.get("hierarchy", "?")]


func _build_stage() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0) # the base game shows black outside the map (owner, 2026-10-06)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.55, 0.6)
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.light_energy = 1.2
	add_child(sun)

	_camera = Camera3D.new()
	_camera.fov = 40
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
			_distance = max(5.0, _distance * 0.9)
			_update_camera()
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			_distance = min(400.0, _distance * 1.1)
			_update_camera()


func _process(delta: float) -> void:
	if _clock != null:
		# Logic would run here, LOGICFRAMES_PER_SECOND (5) times a second.
		_clock.advance(delta)
	_frames += 1
	if not _screenshot_path.is_empty() and _frames == 30:
		var image := get_viewport().get_texture().get_image()
		DirAccess.make_dir_recursive_absolute(_screenshot_path.get_base_dir())
		var err := image.save_png(_screenshot_path)
		print("screenshot %s -> %s" % [_screenshot_path, error_string(err)])
		get_tree().quit(0 if err == OK else 1)
