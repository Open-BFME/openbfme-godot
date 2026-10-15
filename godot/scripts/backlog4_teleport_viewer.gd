## BACKLOG-4 viewer (stop S-813): a real hero teleport in a live retail game, for the lane's before / after proof clips. Angmar's Karsh casts his Blink
## (TeleportSpecialAbilityUpdate, RW 0x8964A0: the hero is moved by RW 0x696E63(location, 1) at the trigger) 300 units east on Evendim; the camera holds
## still over both points and every render frame around the jump is saved, so the frames drawn between the two logic frames show whether the hero slides
## across the jump (the plain setPosition) or is drawn at the destination (the teleport form's two recordTransform calls and forced gather).
##
##   godot --write-movie <out.avi> --fixed-fps 30 --path godot res://scenes/backlog4_teleport_viewer.tscn -- --shots=<dir> --prefix=<name> --caption=<text>
extends Node3D

const MAP := "map mp evendim"
const START := Vector3(1800.0, 800.0, 0.0)
const DEST := Vector3(2100.0, 800.0, 0.0)

var _shots := ""
var _prefix := "teleport"
var _caption := ""
var _world: Node3D
var _camera: Camera3D
var _label: Label
var _local := -1


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			_shots = arg.substr(8)
		elif arg.begins_with("--prefix="):
			_prefix = arg.substr(9)
		elif arg.begins_with("--caption="):
			_caption = arg.substr(10)
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		_fail("mount failed: %s" % [mount.errors])
		return
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.45, 0.55, 0.7)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	_camera = Camera3D.new()
	_camera.fov = 45
	_camera.near = 2.0
	_camera.far = 60000.0
	add_child(_camera)
	var builder: RefCounted = ClassDB.instantiate("MapTerrainBuilder")
	var root: Node3D = builder.build_map(fs, MAP, {"markers": false, "fog": false})
	if root == null:
		_fail("build_map failed: %s" % [builder.get_report().errors])
		return
	add_child(root)
	_world = ClassDB.instantiate("GameWorld")
	var setup: Dictionary = _world.setup(fs)
	if not setup.ok:
		_fail("object world failed: %s" % [setup.errors])
		return
	var slots := [
		{"player": "Player_1", "faction": "FactionAngmar", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMen", "human": false, "team": 1, "start_index": 1},
	]
	var rep: Dictionary = _world.load_map(MAP, {"slots": slots, "seed": 4711})
	if not rep.ok:
		_fail("load_map failed: %s" % [rep.errors.slice(0, 5)])
		return
	root.add_child(_world)
	var idx := 0
	for pl in rep.players:
		if "Player_1" in str(pl) and _local < 0:
			_local = idx
		idx += 1
	_world.set_logic_thread(false)
	_world.set_shroud_drawn(false)
	_world.set_auto_advance(true)
	_world.set_time_scale(1.0)
	_world.set_local_player(_local)
	var layer := CanvasLayer.new()
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(12, 40) # Movie Maker on the Deck cuts the top ~30 px
	_label.add_theme_font_size_override("font_size", 20)
	layer.add_child(_label)
	_run()


func _fail(message: String) -> void:
	print("B4VIEW FAIL ", message)
	get_tree().quit(1)


func _sage_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x, p.z, -p.y)


func _pos(id: int) -> Vector3:
	var o: Dictionary = _world.get_object(id)
	return Vector3(o.get("x", 0.0), o.get("y", 0.0), o.get("z", 0.0))


func _save(tag: String) -> void:
	if _shots.is_empty():
		return
	var img := get_viewport().get_texture().get_image()
	img.save_png("%s/%s-%s.png" % [_shots, _prefix, tag])


func _run() -> void:
	var karsh: int = _world.create_object("AngmarKarsh", _local, START.x, START.y, 0.0)
	if karsh <= 0:
		_fail("AngmarKarsh was not created")
		return
	var mid := (START + DEST) * 0.5
	var ground := float(_world.get_object(karsh).get("z", 0.0))
	_camera.fov = 40
	_camera.position = _sage_to_godot(Vector3(mid.x, mid.y - 300.0, ground + 170.0))
	_camera.look_at(_sage_to_godot(Vector3(mid.x, mid.y, ground)), Vector3.UP)
	for k in 45:
		await get_tree().process_frame
	_world.debug_stealth_scenario(karsh, "unpause", "") # the Blink starts paused (StartsPaused: a level unlocks it)
	print("B4VIEW cast ", _world.cast_object_power_at(_local, karsh, "SpecialAbilityKarshBlink", DEST.x, DEST.y))
	var before := _pos(karsh)
	var jump := -1
	var render := 0
	var frame0: int = _world.get_frame()
	while _world.get_frame() < frame0 + 60:
		await get_tree().process_frame
		render += 1
		var p := _pos(karsh)
		if jump < 0 and Vector2(p.x - before.x, p.y - before.y).length() > 100.0:
			jump = _world.get_frame()
			print("B4VIEW jump at logic frame ", jump, " from ", before, " to ", p)
		var tag := "logic %d, render frame %d" % [_world.get_frame(), render]
		_label.text = "%s\nKarsh's Blink (TeleportSpecialAbilityUpdate, RW 0x696E63): %s" % [_caption, tag]
		# the render frames from the logic frame before the jump to the one after it: the in-between frames are the proof
		if jump >= 0 and _world.get_frame() <= jump + 1:
			_save("jump%+d-r%02d" % [_world.get_frame() - jump, render])
		elif jump < 0 and _world.get_frame() >= frame0 + 8:
			_save("pre-r%02d" % render)
		before = p if jump < 0 else before
	if jump < 0:
		_fail("Karsh did not teleport within 60 logic frames")
		return
	for k in 30:
		await get_tree().process_frame
	print("B4VIEW done")
	get_tree().quit(0)
