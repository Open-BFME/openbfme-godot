extends SceneTree
## GDScript assert() only logs a SCRIPT ERROR and the run carries on, so a failed one still ended in PASS / exit 0. Every check goes through
## _check: a failure is recorded and printed as FAIL, and _finish quits 1 when any failed.
var _failures: Array[String] = []

func _check(ok: bool, reason := "") -> bool:
	if not ok:
		_failures.append(reason)
		var stack := get_stack()
		printerr("FAIL: ", reason, " (line ", stack[1].line if stack.size() > 1 else 0, ")")
	return ok

func _finish(pass_message: String) -> void:
	if _failures.is_empty():
		print(pass_message)
		quit()
		return
	printerr(_failures.size(), " check(s) failed")
	quit(1)

class PreviewWorld extends Node3D:
	var info: Dictionary
	var angle := 0.0
	func cah_view_info(_cls: int, _sub: int) -> Dictionary:
		return info.duplicate(true)
	func cah_preview_apply(_id: int, _record: PackedByteArray) -> Dictionary:
		return {"ok": true, "portrait": "test"}
	func cah_preview_set_orientation(_id: int, value: float) -> Dictionary:
		angle = value
		return {"ok": true}

class PreviewShell extends Node2D:
	var state := {"up": true, "revision": 1, "camera_revision": 1, "class": 0, "subclass": 0,
		"record": PackedByteArray([1]), "page": "A"}
	func get_create_a_hero_view() -> Dictionary:
		return state
	func set_create_a_hero_portrait(_portrait: String) -> void:
		pass

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var view = load("res://scripts/create_a_hero_view.gd").new()
	root.add_child(view)
	view._cam = Camera3D.new()
	view.add_child(view._cam)
	view._cull_cam = Camera3D.new()
	view.add_child(view._cull_cam)
	view._view = SubViewport.new()
	view._view.size = Vector2i(root.get_visible_rect().size)
	view.add_child(view._view)
	view._world = PreviewWorld.new()
	view.add_child(view._world)
	view._shell = PreviewShell.new()
	view.add_child(view._shell)
	# Synthetic rows: hand-computed endpoints, not a second copy of the implementation.
	var info := {"ok": true, "near": [0.25, 0.2, 30.0, 40.0, 10.0],
		"far": [0.75, 0.4, 60.0, 80.0, 20.0], "close_up": [-0.2, 0.2, 50.0, 30.0, -5.0],
		"portrait": [2.0, 0.3, 30.0, 60.0, 10.0], "normal_cam": 0.5, "map_location": 0,
		"camera_angle": 0.0, "camera_offset": Vector3(0, -400, 300)}
	view._world.info = info
	view._locations = [{"id": 1, "x": 100.0, "y": 200.0, "z": 999.0, "angle": 0.0}]
	view.tick(1.0 / 60.0)
	_check(view.errors.is_empty())
	_check(view._t == 0.5)
	_check(view._yaw == 0.0)
	_check(is_equal_approx(view.camera_pose("A").fov, 0.83))
	_check(is_equal_approx(view._cam.fov, 50.0))
	view._view.size.x /= 2
	view._place_camera("A")
	_check(is_equal_approx(view._cam.fov, 25.0))
	_check(is_equal_approx(view._cull_cam.fov, 25.0))
	view._view.size.x *= 2
	view._place_camera("A")
	_check(view._cull_cam.global_transform.is_equal_approx(view._cam.global_transform))
	# Tolerance: Vector3/transform components are float32; pi/2 is retail's rounded float.
	_check(view._cam.position.is_equal_approx(Vector3(220, 90, -215)))
	_check(view.camera_pose("A").target.is_equal_approx(Vector3(160, 67.5, -215)))
	for fixture in [
		[0.0, Vector3(180, 60, -210), Vector3(140, 52.5, -210)],
		[1.0, Vector3(260, 120, -220), Vector3(180, 75, -220)]]:
		view._t = fixture[0]
		for page in ["C", "A"]:
			var pose: Dictionary = view.camera_pose(page)
			_check(pose.eye.is_equal_approx(fixture[1]))
			_check(pose.target.is_equal_approx(fixture[2]))
		_check(view.camera_pose("P").eye.is_equal_approx(Vector3(143.333333, 60, -195)))
		_check(view.camera_pose("P").target.is_equal_approx(Vector3(130, 62, -195)))
		_check(view.camera_pose("M").eye.is_equal_approx(Vector3(200, 90, -210)))
		_check(view.camera_pose("M").target.is_equal_approx(Vector3(160, 30, -210)))
	view._t = -1.0
	var expected := [0.5, 0.3, 45.0, 60.0, 15.0]
	for i in 5: _check(is_equal_approx(view.camera_pose("A").row[i], expected[i]))
	view._t = 0.5
	var unchanged: Dictionary = view.camera_pose("A")
	view._advance_controls({"rotate_right": true}, false)
	_check(is_equal_approx(view._yaw, 0.1))
	view._advance_controls({"rotate_left": true}, true)
	_check(is_equal_approx(view._yaw, -0.1))
	_check(view.camera_pose("A") == unchanged)
	view._advance_controls({"zoom_in": true}, false)
	_check(is_equal_approx(view._t, 0.45))
	view._advance_controls({"zoom_out": true}, true)
	_check(is_equal_approx(view._t, 0.55))
	for i in 40: view._advance_controls({"zoom_in": true}, false)
	_check(view._t == 0.0)
	view._advance_controls({"zoom_in": true, "zoom_out": true}, false)
	_check(view._t == 0.05) # RW 0x919C26 applies in before out.
	for i in 40: view._advance_controls({"zoom_out": true}, true)
	_check(view._t == 1.0)
	view._shell.state.camera_revision += 1
	view._shell.state.rotate_right = true
	view._shell.state.zoom_in = true
	view.tick(0.001)
	_check(is_equal_approx(view._t, 0.5)) # less than one 30 Hz client frame: no step yet (S-456)
	view.tick(1.0 / 30.0)
	_check(is_equal_approx(view._t, 0.45))
	_check(is_equal_approx(view._world.angle, 0.1))
	for i in 5: view.tick(1.0 / 150.0) # 150 fps: five render frames are one client frame, one step
	_check(is_equal_approx(view._t, 0.4))
	_check(is_equal_approx(view._world.angle, 0.2))
	view.tick(1.0) # a long frame catches up two steps, not 30
	_check(is_equal_approx(view._t, 0.3))
	_check(is_equal_approx(view._world.angle, 0.4))
	view._shell.state.rotate_right = false
	view._shell.state.zoom_in = false
	view._shell.state.revision += 1 # Cosmetic changes do not reset controls.
	view.tick(0.03)
	_check(is_equal_approx(view._t, 0.3))
	view._shell.state.camera_revision += 1
	view._shell.state.page = "P"
	view.tick(0.03)
	_check(view._t == 0.5)
	_check(view._world.angle == 0.0)
	for key in info:
		if key == "ok": continue
		view._info = info.duplicate(true)
		view._info.erase(key)
		_check(view.camera_pose("A").has("error"))
	for bad_row in [[], [1, 2], [0, 0, 0, 0, 0], [NAN, 1, 2, 3, 4], ["0", 1, 2, 3, 4]]:
		view._info = info.duplicate(true)
		view._info.near = bad_row
		_check(view.camera_pose("A").has("error"))
	view._info = info.duplicate(true)
	view._info.camera_offset = Vector3.ZERO
	_check(view.camera_pose("A").has("error"))
	view._locations = []
	view._apply({"class": 0, "subclass": 0})
	_check(view._info.is_empty())
	_check(view.errors.size() == 1)
	view._world.info = {"ok": false, "error": "missing camera settings"}
	view._apply({"class": 0, "subclass": 0})
	_check(view._info.is_empty())
	_check(view.errors.back() == "missing camera settings")
	_finish("CAH CAMERA PASS")
