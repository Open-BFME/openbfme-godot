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

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: cah camera retail requires ROTWK_INSTALL and BFME2_INSTALL")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not _check(fs.mount_retail().ok):
		_finish("")
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not _check(world.setup(fs).ok):
		_finish("")
		return
	_check(not world.cah_view_info(-1, 0).ok)
	_check(not world.cah_preview_set_orientation(1, 0).ok)
	if not _check(world.load_map("createahero", {"seed": 1, "terrain": false}).ok):
		_finish("")
		return
	_check(world.cah_builder(true))
	var info: Dictionary = world.cah_view_info(0, 1) # Hero of the West / Shield Maiden.
	if not _check(info.ok, str(info)):
		_finish("")
		return
	_check(info.normal_cam == 0.5)
	_check(info.camera_offset.x == 0.0)
	_check(info.camera_offset.z == 300.0)
	_check(is_equal_approx(info.camera_offset.y, -300.0 / tan(deg_to_rad(37.5))))
	var locations: Array = world.cah_preview_locations()
	if not _check(not locations.is_empty(), "no createahero preview locations"):
		_finish("")
		return
	var loc: Dictionary = locations[info.map_location]
	if not _check(not loc.is_empty(), "no preview location for the view"):
		_finish("")
		return
	var view = load("res://scripts/create_a_hero_view.gd").new()
	root.add_child(view)
	view._info = info
	view._hero_sage = Vector3(loc.x, loc.y, loc.z)
	view._facing = loc.angle
	view._t = info.normal_cam
	var normal: Dictionary = view.camera_pose("A")
	if not _check(not normal.has("error"), str(normal)):
		_finish("")
		return
	_check(normal.fov == 0.83)
	_check(view.camera_pose("C") == normal)
	for i in 5: _check(is_equal_approx(normal.row[i], (info.near[i] + info.far[i]) * 0.5))
	_check(view.camera_pose("P").row == info.close_up)
	_check(view.camera_pose("M").row == info.portrait)
	view._advance_controls({"zoom_in": true}, false)
	_check(is_equal_approx(view._t, 0.45))
	for i in 40: view._advance_controls({"zoom_out": true}, true)
	_check(view._t == 1.0)
	if not _check(world.cah_preview_set_orientation(loc.id, loc.angle + 0.1).ok):
		_finish("")
		return
	var rotated: Array = world.cah_preview_locations()
	_check(is_equal_approx(rotated[info.map_location].angle, loc.angle + 0.1))
	_check(not world.cah_preview_set_orientation(-1, 0).ok)
	_check(not world.cah_preview_set_orientation(loc.id, NAN).ok)
	_check(not world.cah_view_info(0, 4294967296).ok)
	_check(world.cah_builder(false))
	_check(not world.cah_preview_set_orientation(loc.id, 0).ok)
	world.clear_game_data(false)
	_finish("CAH CAMERA RETAIL PASS")
