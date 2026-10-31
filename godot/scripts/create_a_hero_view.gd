## OpenBFME (lane CAH-1): the Create-a-Hero builder's 3D view.
##
## Retail (RotWK, RW 0x91A018): the main menu's Create-a-Hero pushes CreateAHero.apt and starts Maps\CreateAHero\CreateAHero.map behind it (game mode 7, the
## "map mode"); the movie's CreateAHero::DrawMapComponent clip draws the tactical view in its rectangle (RW 0x91A3A9); the hero being built is applied to the
## map's preview object of its subclass's MapLocation (RW 0x9C0E03 / 0x9BFCF9) and the camera is placed from the subclass's ViewInfo (RW 0x9BFE74).
## Here: the GameWorld loads the map (with its terrain); a camera renders it into a SubViewport whose texture the shell draws into the DrawMapComponent clip's
## rectangle (lane CAH-2; the window's own 3D is off while the builder is up). Each frame the shell's view state (AptMenuPlayer.get_create_a_hero_view) is read: a new
## revision applies the shown record to the preview object (GameWorld.cah_preview_apply, TheCreateAHeroSystem + 0x18C set: cah_builder), the held
## rotate / zoom buttons turn the hero and zoom the camera.
## The map locations are RW 0x9C09FC's.
extends Node

const MAP := "createahero" # Maps\\CreateAHero\\CreateAHero.map (RW 0xC7CBF0)

var _world: Node3D
var _shell: Node2D
var _cam: Camera3D
var _view: SubViewport
var _cull_cam: Camera3D
var _root_3d_was_disabled := false
var _locations: Array = []
var _revision := -1
var _camera_revision := -1
var _record := PackedByteArray()
var _object := -1
var _yaw := 0.0
var _control_time := 0.0 # seconds not yet spent on control steps (one step per 30 Hz client frame, retail FramesPerSecondLimit 30, S-456)
var _t := -1.0 # the near -> far blend (RW 0x9BF360's t); < 0 until the first hero: NormalCam
var _hero_sage := Vector3.ZERO
var _target := Vector3.ZERO
var _facing := 0.0
var _info: Dictionary = {}
var report: Dictionary = {}
var errors: Array = []


func begin(world: Node3D, shell: Node2D, size: Vector2i) -> bool:
	_world = world
	_shell = shell
	var rep: Dictionary = _world.load_map(MAP, {"seed": 1, "terrain": true})
	report = rep
	if not rep.ok:
		errors = rep.get("errors", [])
		return false
	_world.cah_builder(true)
	_locations = _world.cah_preview_locations()
	print("CAH map mode: %s objects, %d map locations %s" % [str(rep.get("objects", 0)), _locations.size(), JSON.stringify(_locations)])
	# lane CAH-2: RW 0x91A3A9 draws the tactical view into the CreateAHero::DrawMapComponent clip's rectangle only: the camera renders the map mode's world
	# into a viewport whose texture the shell draws into that clip (AptMenuPlayer.set_create_a_hero_view_texture); the window's own 3D is off meanwhile
	_view = SubViewport.new()
	_view.name = "CreateAHeroViewport"
	_view.size = size
	_view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_view.msaa_3d = get_viewport().msaa_3d
	add_child(_view)
	_cam = Camera3D.new()
	_cam.name = "CreateAHeroCamera"
	_cam.fov = rad_to_deg(0.83) # RW 0x9BFE74: DAT 0xC2D8F0, horizontal as the tactical view's (TacticalCamera: View + 0x6C)
	_cam.keep_aspect = Camera3D.KEEP_WIDTH
	_cam.near = 1.0
	_cam.far = 20000.0
	_view.add_child(_cam)
	_cam.current = true
	# the world's pose culling and depth sort ask the window's camera (W3DInstancer: get_viewport().get_camera_3d()): a twin of the view's camera stays
	# current there (the window draws no 3D meanwhile)
	_cull_cam = Camera3D.new()
	_cull_cam.name = "CreateAHeroCullCamera"
	add_child(_cull_cam)
	_cull_cam.current = true
	_root_3d_was_disabled = get_viewport().disable_3d
	get_viewport().disable_3d = true
	_shell.set_create_a_hero_view_texture(_view.get_texture())
	_world.set_auto_advance(true)
	return true


func end() -> void:
	if _cam != null:
		_cam.current = false
	if _cull_cam != null:
		_cull_cam.current = false
	if _shell != null:
		_shell.set_create_a_hero_view_texture(null)
	if _view != null:
		get_viewport().disable_3d = _root_3d_was_disabled
	if _world != null:
		_world.set_auto_advance(false)
		_world.cah_builder(false)
		_world.clear_game_data(false)


func tick(delta: float) -> void:
	var view: Dictionary = _shell.get_create_a_hero_view()
	if not view.get("up", false):
		return
	var rect: Rect2 = view.get("view_rect", Rect2())
	if rect.size.x >= 2.0 and rect.size.y >= 2.0 and Vector2i(rect.size) != _view.size:
		_view.size = Vector2i(rect.size)
	if int(view.get("revision", -1)) != _revision:
		_revision = int(view.revision)
		_record = view.get("record", PackedByteArray())
		if _record.size() > 0:
			_apply(view)
		else:
			_info = {}
			_object = -1
	if _info.is_empty():
		return
	if int(view.camera_revision) != _camera_revision:
		_camera_revision = int(view.camera_revision)
		_yaw = 0.0
		_t = float(_info.normal_cam)
		_control_time = 0.0
	# RW 0x919C26 steps once per client frame and retail draws 30 of them a second (S-456): held buttons step at that rate whatever the
	# render rate; a long frame catches up two steps at most
	_control_time = minf(_control_time + delta, 2.0 / 30.0)
	while _control_time >= 1.0 / 30.0:
		_control_time -= 1.0 / 30.0
		_advance_controls(view, Input.is_key_pressed(KEY_SHIFT))
	var orientation: Dictionary = _world.cah_preview_set_orientation(_object, _facing + _yaw)
	if not orientation.ok:
		_fail(str(orientation.error))
		return
	_place_camera(str(view.get("page", "")))


func _advance_controls(view: Dictionary, shifted: bool) -> void:
	# RW 0x919C26 / 0x9BF019..0x9BF0CD: fixed steps, right/left then in/out.
	var turn := 0.2 if shifted else 0.1
	var zoom := 0.1 if shifted else 0.05
	if view.get("rotate_right", false):
		_yaw += turn
	if view.get("rotate_left", false):
		_yaw -= turn
	if view.get("zoom_in", false):
		_t = maxf(0.0, _t - zoom)
	if view.get("zoom_out", false):
		_t = minf(1.0, _t + zoom)


func _fail(error: String) -> void:
	if not errors.has(error):
		errors.append(error)
		printerr("CAH preview: ", error)
	_info = {}
	_object = -1


func _validate_info() -> String:
	for key in ["near", "far", "close_up", "portrait"]:
		if not _info.has(key) or not _info[key] is Array or _info[key].size() != 5:
			return "invalid or missing ViewInfo row: " + key
		for value in _info[key]:
			if typeof(value) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(value)):
				return "non-finite or non-numeric ViewInfo row: " + key
		if float(_info[key][1]) <= 0.0:
			return "non-positive ViewInfo zoom: " + key
	for key in ["normal_cam", "camera_angle"]:
		if not _info.has(key) or typeof(_info[key]) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(_info[key])):
			return "invalid or missing ViewInfo field: " + key
	if float(_info.normal_cam) < 0.0 or float(_info.normal_cam) > 1.0:
		return "NormalCam is outside 0..1"
	if not _info.has("map_location") or typeof(_info.map_location) != TYPE_INT or int(_info.map_location) < 0:
		return "invalid or missing MapLocation"
	if not _info.has("camera_offset") or not _info.camera_offset is Vector3:
		return "missing map camera offset"
	var offset: Vector3 = _info.camera_offset
	if not offset.is_finite() or offset.z <= 0.0:
		return "invalid map camera offset"
	return ""


func _apply(view: Dictionary) -> void:
	if not view.has("class") or not view.has("subclass"):
		_fail("missing hero class or subclass")
		return
	_info = _world.cah_view_info(int(view["class"]), int(view.subclass))
	if not _info.get("ok", false):
		_fail(str(_info.get("error", "Create-a-Hero view information is unavailable")))
		return
	var error := _validate_info()
	if not error.is_empty():
		_fail(error)
		return
	var loc := int(_info.map_location)
	if loc < 0 or loc >= _locations.size() or int(_locations[loc].get("id", 0)) == 0:
		_fail("map location %d of class %d subclass %d is no object of the map (%d locations)" % [loc, view["class"], view.subclass, _locations.size()])
		return
	var l: Dictionary = _locations[loc]
	_object = int(l.id)
	_target = Vector3(l.x, l.z, -l.y)
	_hero_sage = Vector3(l.x, l.y, l.z)
	_facing = float(l.angle)
	if _t < 0.0:
		_t = float(_info.normal_cam)
	var r: Dictionary = _world.cah_preview_apply(_object, _record)
	if r.ok:
		_shell.set_create_a_hero_portrait(str(r.portrait))
	if not r.ok:
		_fail(str(r.error))


func camera_pose(page: String) -> Dictionary:
	var error := _validate_info()
	if not error.is_empty():
		return {"error": error}
	if page not in ["M", "C", "A", "P", "B"]:
		return {"error": "unknown Create-a-Hero page: " + page}
	var near: Array = _info.near
	var far: Array = _info.far
	var v: Array = []
	if page == "P":
		v = _info.close_up
	elif page == "M":
		v = _info.portrait
	else:
		var t := float(_info.normal_cam) if _t < 0.0 else _t
		for i in 5:
			v.append(float(near[i]) + (float(far[i]) - float(near[i])) * t)
	# AptMyHero::rva005B1019 (BFME2 RVA 0x5B1019, RW 0x9BFE74, tier B).
	# Pitch is fxPitch, not an Euler angle; hero rotation does not rotate the camera.
	var pitch: float = float(v[0])
	var zoom: float = float(v[1])
	var floor_h: float = float(v[2])
	var dist: float = float(v[3])
	var shift: float = float(v[4])
	var a: float = _facing + float(_info.camera_angle)
	var offset: Vector3 = _info.camera_offset * zoom
	var heading := a + 1.5707964
	var point := _hero_sage + Vector3(cos(a) * dist - sin(a) * shift, sin(a) * dist + cos(a) * shift, 0)
	# W3DView::moveAlongWaypointPath (RW 0x48688A) discards point.z.
	# W3DCamTransform::rva00102F83 (RW 0x502858, tier A): named mode 1,
	# non-live/non-realZoom, zero pitch rotation/roll/targetZAdjustment.
	var factor := 1.0 - floor_h / offset.z
	var eye := Vector3(offset.x * cos(heading) - offset.y * sin(heading),
		offset.x * sin(heading) + offset.y * cos(heading), offset.z) * factor
	eye += Vector3(point.x, point.y, floor_h)
	var target := Vector3(point.x, point.y, floor_h)
	if pitch <= 1.0:
		target.z = eye.z - (eye.z - target.z) * pitch
	else:
		eye.x = target.x + (eye.x - target.x) / pitch
		eye.y = target.y + (eye.y - target.y) / pitch
	if not eye.is_finite() or not target.is_finite() or eye.is_equal_approx(target):
		return {"error": "degenerate Create-a-Hero camera transform"}
	return {"eye": Vector3(eye.x, eye.z, -eye.y), "target": Vector3(target.x, target.z, -target.y), "row": v, "fov": 0.83}


func _place_camera(page: String) -> void:
	if _cam == null or _info.is_empty():
		return
	var pose := camera_pose(page)
	if pose.has("error"):
		_fail(str(pose.error))
		return
	# RW 0x65BE82 DrawMapView -> W3DView::setWidth (RW 0x4874A7,
	# BFME2 RVA 0x87B81, tier A) replaces the named camera's FOV for drawing.
	var display_width := get_viewport().get_visible_rect().size.x
	if display_width <= 0.0 or _view.size.x <= 0:
		_fail("invalid Create-a-Hero viewport width")
		return
	_cam.fov = float(_view.size.x) / display_width * 50.0
	_cam.look_at_from_position(pose.eye, pose.target, Vector3.UP)
	_cull_cam.global_transform = _cam.global_transform
	_cull_cam.fov = _cam.fov
	_cull_cam.near = _cam.near
	_cull_cam.far = _cam.far
	_cull_cam.keep_aspect = _cam.keep_aspect
