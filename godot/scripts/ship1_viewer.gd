## SHIP-1 viewer: ships on Evendim's lake in a live retail game (the SHIP / HOVER movers, SEA_LEVEL, the shore spot of the transports, ship combat). For the
## lane's proof screenshots and clip.
##
##   godot --path godot res://scenes/ship1_viewer.tscn -- [options]
##
## Options (after `--`):
##   --shots=<dir>     saves <prefix>-board.png, -sail.png, -land.png, -battle.png, -sink.png, -crebain.png there
##   --prefix=<name>   the screenshot prefix (default ship1)
##   --speed=<f>       the logic speed (default 1.0)
##   --only=<part>     ferry (a horde boards a transport ship, crosses the lake, lands), battle (an Elven battle ship against a transport ship and an orc pit) or
##                     crebain (a flock of Crebain over the water); default all three
extends Node3D

const MAP := "map mp evendim"

var _shots := ""
var _prefix := "ship1"
var _speed := 1.0
var _only := ""
var _world: Node3D
var _camera: Camera3D
var _label: Label
var _local := -1
var _enemy := -1


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			_shots = arg.substr(8)
		elif arg.begins_with("--prefix="):
			_prefix = arg.substr(9)
		elif arg.begins_with("--speed="):
			_speed = float(arg.substr(8))
		elif arg.begins_with("--only="):
			_only = arg.substr(7)
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
		{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": false, "team": 1, "start_index": 1},
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
		if "Player_2" in str(pl) and _enemy < 0:
			_enemy = idx
		idx += 1
	_world.set_logic_thread(false)
	_world.set_shroud_drawn(false)
	_world.set_auto_advance(true)
	_world.set_time_scale(_speed)
	_world.set_local_player(_local)
	var layer := CanvasLayer.new()
	add_child(layer)
	_label = Label.new()
	_label.position = Vector2(12, 8)
	_label.add_theme_font_size_override("font_size", 22)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 5)
	layer.add_child(_label)
	_run()


func _fail(message: String) -> void:
	push_error(message)
	print("SHIP1VIEW FAIL: ", message)
	get_tree().quit(1)


func _sage_to_godot(p: Vector3) -> Vector3:
	return Vector3(p.x, p.z, -p.y)


func _aim(focus: Vector3, eye: Vector3) -> void:
	_camera.position = _sage_to_godot(eye)
	_camera.look_at(_sage_to_godot(focus), Vector3.UP)


func _wait(frames: int) -> void:
	for k in frames:
		await get_tree().process_frame


func _pos(id: int) -> Vector3:
	var o: Dictionary = _world.get_object(id)
	return Vector3(o.get("x", 0.0), o.get("y", 0.0), o.get("z", 0.0))


## the camera south of the point between two objects, looking north at it
func _frame_pair(a: int, b: int, extra := 0.0) -> void:
	var pa := _pos(a)
	var pb := _pos(b)
	var mid := (pa + pb) * 0.5
	var span := maxf(Vector2(pa.x - pb.x, pa.y - pb.y).length(), 250.0) + extra
	_aim(Vector3(mid.x, mid.y, mid.z), Vector3(mid.x - 60.0, mid.y - span * 0.9 - 150.0, mid.z + 180.0 + span * 0.45))


func _frame_one(a: int, dist := 450.0) -> void:
	var p := _pos(a)
	_aim(p, Vector3(p.x - 80.0, p.y - dist, p.z + dist * 0.6))


func _health(id: int) -> float:
	return float(_world.get_object(id).get("health", 0.0))


func _contained_by(id: int) -> int:
	return int(_world.get_object(id).get("contained_by", 0))


func _ferry() -> void:
	var ship: int = _world.create_object("EvilMenTransportShip", _enemy, 2000.0, 1140.0, -PI * 0.5)
	var horde: int = _world.create_object("MordorFighterHorde", _enemy, 2000.0, 850.0, PI * 0.5)
	await _wait(10)
	print("SHIP1VIEW ferry board ", JSON.stringify(_world.order_garrison([horde], ship)))
	var f0: int = _world.get_frame()
	while _world.get_frame() < f0 + 200 and _contained_by(horde) != ship:
		await get_tree().process_frame
		_frame_pair(ship, horde)
		_label.text = "A Mordor orc horde boards a transport ship at Evendim's south shore (the ship's shore spot, RW 0x6EFBB8)"
		if _world.get_frame() == f0 + 20:
			_save("board")
	await _wait(40)
	print("SHIP1VIEW ferry sail ", JSON.stringify(_world.debug_ai_move(ship, 1550.0, 4150.0)))
	f0 = _world.get_frame()
	var saved := false
	while _world.get_frame() < f0 + 400:
		await get_tree().process_frame
		_frame_one(ship, 520.0)
		var p := _pos(ship)
		_label.text = "The transport ship sails round the island on the water (SHIP mover RW 0x5E613F, SEA_LEVEL): height %.1f" % p.z
		if _world.get_frame() == f0 + 120 and not saved:
			_save("sail")
			saved = true
		if _world.get_frame() > f0 + 30 and not bool(_world.get_object(ship).get("moving", true)) and Vector2(p.x - 1550.0, p.y - 4150.0).length() < 60.0:
			break
	print("SHIP1VIEW ferry evacuate ", JSON.stringify(_world.order_evacuate(ship)))
	f0 = _world.get_frame()
	while _world.get_frame() < f0 + 90:
		await get_tree().process_frame
		_frame_pair(ship, horde, 100.0)
		_label.text = "On the north shore the horde lands on the shore spot and walks off (RW 0x868516)"
		if _world.get_frame() == f0 + 60:
			_save("land")
	var hp := _pos(horde)
	print("SHIP1VIEW ferry: horde at (%.0f, %.0f), contained by %d" % [hp.x, hp.y, _contained_by(horde)])
	await _clear([ship, horde])


func _battle() -> void:
	var bs: int = _world.create_object("ElvenBattleShip", _local, 2000.0, 1800.0, -PI * 0.5)
	var enemy_ship: int = _world.create_object("EvilMenTransportShip", _enemy, 2250.0, 1300.0, PI)
	var pit: int = _world.create_object("MordorOrcPit", _enemy, 1900.0, 950.0, PI * 0.5)
	await _wait(10)
	print("SHIP1VIEW battle move ", JSON.stringify(_world.debug_ai_move(bs, 2000.0, 1300.0)))
	var f0: int = _world.get_frame()
	var h0 := _health(enemy_ship)
	var p0 := _health(pit)
	var sunk := -1
	var saved := {}
	while _world.get_frame() < f0 + 260:
		await get_tree().process_frame
		_frame_pair(bs, enemy_ship, 200.0)
		var hs := _health(enemy_ship)
		_label.text = "An Elven battle ship sails in; its five archers shoot (12.5 an arrow on a ship, 0.5 on an orc pit)\nenemy transport ship %.1f / %.0f, orc pit %.1f / %.0f" % [hs, h0, _health(pit), p0]
		if hs < h0 and hs > 0.0 and not saved.has("battle"):
			_save("battle")
			saved["battle"] = true
		if hs <= 0.0 and sunk < 0:
			sunk = _world.get_frame()
		if sunk >= 0 and _world.get_frame() == sunk + 15:
			_save("sink")
	print("SHIP1VIEW battle: enemy ship %.1f -> %.1f, pit %.1f -> %.1f" % [h0, _health(enemy_ship), p0, _health(pit)])
	await _clear([bs, enemy_ship, pit])


func _crebain() -> void:
	var flock: int = _world.create_object("Crebain", _enemy, 2000.0, 700.0, PI * 0.5)
	await _wait(5)
	print("SHIP1VIEW crebain move ", JSON.stringify(_world.debug_ai_move(flock, 2000.0, 1700.0)))
	var f0: int = _world.get_frame()
	while _world.get_frame() < f0 + 90:
		await get_tree().process_frame
		_frame_one(flock, 400.0)
		var p := _pos(flock)
		_label.text = "Crebain (HOVER, SURFACE_RELATIVE_HEIGHT) fly out over the lake, climbing toward 50 above the water: %.0f above the lake bed" % (p.z - float(_world.get_ground_height(p.x, p.y)))
		if _world.get_frame() == f0 + 60:
			_save("crebain")
	await _clear([flock])


func _run() -> void:
	await get_tree().process_frame
	print("SHIP1VIEW local %d enemy %d" % [_local, _enemy])
	if _only.is_empty() or _only == "ferry":
		await _ferry()
	if _only.is_empty() or _only == "battle":
		await _battle()
	if _only.is_empty() or _only == "crebain":
		await _crebain()
	print("SHIP1VIEW DONE")
	get_tree().quit(0)


var _saved := {}


func _save(name: String) -> void:
	if _shots.is_empty() or _saved.has(name):
		return
	_saved[name] = true
	var img := get_viewport().get_texture().get_image()
	var path := _shots.path_join("%s-%s.png" % [_prefix, name])
	img.save_png(path)
	print("SHIP1VIEW SAVED ", path)


func _clear(ids: Array) -> void:
	for id in ids:
		if not _world.get_object(id).get("destroyed", true):
			_world.destroy_object(id)
	await _wait(10)
