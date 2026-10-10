## OpenBFME (lane CAH-1): the Create-a-Hero builder's 3D view.
##
## Retail (RotWK, RW 0x91A018): the main menu's Create-a-Hero pushes CreateAHero.apt and starts Maps\CreateAHero\CreateAHero.map behind it (game mode 7, the
## "map mode"); the movie's CreateAHero::DrawMapComponent clip draws the tactical view in its rectangle (RW 0x91A3A9); the hero being built is applied to the
## map's preview object of its subclass's MapLocation (RW 0x9C0E03 / 0x9BFCF9) and the camera is placed from the subclass's ViewInfo (RW 0x9BFE74).
## Here: the GameWorld loads the map (with its terrain); a camera renders it into a SubViewport whose texture the shell draws into the DrawMapComponent clip's
## rectangle (lane CAH-2; the window's own 3D is off while the builder is up). Each frame the shell's view state (AptMenuPlayer.get_create_a_hero_view) is read: a new
## revision applies the shown record to the preview object (GameWorld.cah_preview_apply, TheCreateAHeroSystem + 0x18C set: cah_builder), the held
## rotate / zoom buttons turn and zoom the camera.
## The map locations are RW 0x9C09FC's (GameWorld.cah_preview_locations: an object named "<x>_<n>" is location n). INFERENCE (stop S-1405): the camera is an
## approximation of RW 0x9BFE74 (the ground point Dist ahead of the hero's facing and Shift aside, then the view's pitch); the near / far zoom blend and the
## close-up / portrait views of the Powers / Manager pages are not reproduced exactly.
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
var _record := PackedByteArray()
var _object := -1
var _yaw := 0.0
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
	_cam.keep_aspect = Camera3D.KEEP_HEIGHT
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
	if view.get("rotate_left", false):
		_yaw -= delta * 1.5
	if view.get("rotate_right", false):
		_yaw += delta * 1.5
	if view.get("zoom_in", false):
		_t = minf(1.0, _t + delta * 0.8)
	if view.get("zoom_out", false):
		_t = maxf(0.0, _t - delta * 0.8)
	_place_camera(str(view.get("page", "")))


func _apply(view: Dictionary) -> void:
	_info = _world.cah_view_info(int(view.get("class", 0)), int(view.get("subclass", 0)))
	var loc := int(_info.get("map_location", 0))
	if _locations.is_empty():
		return
	if loc < 0 or loc >= _locations.size() or int(_locations[loc].get("id", 0)) == 0:
		var e := "map location %d of class %d subclass %d is no object of the map (%d locations)" % [loc, view.get("class", 0), view.get("subclass", 0), _locations.size()]
		if not errors.has(e):
			errors.append(e)
			printerr("CAH preview: ", e)
		return
	var l: Dictionary = _locations[loc]
	_object = int(l.id)
	_target = Vector3(l.x, l.z, -l.y)
	_hero_sage = Vector3(l.x, l.y, l.z)
	_facing = float(l.angle)
	if _t < 0.0:
		_t = clampf(float(_info.get("normal_cam", 0.0)), 0.0, 1.0)
	var r: Dictionary = _world.cah_preview_apply(_object, _record)
	if not r.ok:
		errors.append(str(r.get("error", "")))
		printerr("CAH preview: ", r.get("error", ""))


func _place_camera(page: String) -> void:
	# lane CAH-2: RW 0x9BFE74. The ViewInfo row (RW 0xD9EE08: [pitch, zoom, floor, dist, shift]) is the near row blended to the far row by t (RW 0x9BF2E1 ..
	# 0x9BF360 = BFME2 decomp Rva005B01D3Lerp.cpp: a + (b - a) * t, t < 0 -> NormalCam +0x60), or the close-up (+0x30) / portrait (+0x48) row while the
	# screen's fixed view (+0x178) is on. The angle is the map location's (+4) plus CameraAngle (+0x64); the camera's ground point is the hero's position
	# plus (cos a * dist - sin a * shift, sin a * dist + cos a * shift), and RW 0x489B77 records it with height Floor, heading a + pi/2 (DAT 0xBD89D0), Pitch,
	# Zoom and the field of view 0.83 (DAT 0xC2D8F0) for the tactical view (TheTacticalView vslot 0x60).
	# INFERENCE (S-1405): the eye stands at the ground point, Floor * Zoom above the hero's ground, looking back along the heading at Pitch below the
	# horizon (vslot 0x60's use of the record was not read); the fixed view is taken on the Powers page (close-up); RotateLeft / RotateRight turn the
	# angle, ZoomIn / ZoomOut move t.
	if _cam == null or _info.is_empty():
		return
	var near: Array = _info.get("near", [0.0, 1.0, 20.0, 40.0, 0.0])
	var far: Array = _info.get("far", near)
	var v: Array = []
	if page == "P":
		v = _info.get("close_up", near)
	else:
		var t := clampf(_t, 0.0, 1.0)
		for i in 5:
			v.append(float(near[i]) + (float(far[i]) - float(near[i])) * t)
	var pitch: float = float(v[0])
	var zoom: float = float(v[1])
	var floor_h: float = float(v[2])
	var dist: float = float(v[3])
	var shift: float = float(v[4])
	var a: float = _facing + float(_info.get("camera_angle", 0.0)) + _yaw
	# SAGE x, y, z -> Godot x, z, -y
	var ex := _hero_sage.x + cos(a) * dist - sin(a) * shift
	var ey := _hero_sage.y + sin(a) * dist + cos(a) * shift
	var ez := _hero_sage.z + floor_h * zoom
	var dir := Vector3(-cos(a) * cos(pitch), -sin(a) * cos(pitch), -sin(pitch))
	var eye := Vector3(ex, ez, -ey)
	var look := eye + Vector3(dir.x, dir.z, -dir.y) * 10.0
	_cam.look_at_from_position(eye, look, Vector3.UP)
	_cull_cam.global_transform = _cam.global_transform
	_cull_cam.fov = _cam.fov
	_cull_cam.near = _cam.near
	_cull_cam.far = _cam.far
	_cull_cam.keep_aspect = _cam.keep_aspect
