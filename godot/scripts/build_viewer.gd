## Construction viewer / recorded scenario (lane BUILD-1): starts a skirmish through the logic's new-game path (so the starting base of the faction is the real one: the
## fortress unpacked from Bases.big, its build plots and the Porters), puts the in-game HUD on it and either lets you play (select a Porter, press a build button, move the ghost,
## click) or runs a scripted scenario with the HUD's own input path.
##
##   godot --path godot res://scenes/build_viewer.tscn -- [options]
##
## Options (after `--`):
##   --faction=<Name>     the local player's faction (default FactionMen; FactionElves, FactionDwarves, FactionIsengard, FactionMordor, FactionWild, FactionAngmar)
##   --map=<key>          the map cache key (default "maps/map mp evendim/map mp evendim.map")
##   --scenario=<name>    fortress: the starting fortress; build: select a Porter, press the barracks button, place, watch it rise (screenshots);
##                        showcase: the same at a pace made for a recorded video (use with `--write-movie out.avi --fixed-fps 30`)
##   --shots=<dir>        the scenario's named screenshots (build1-<state>.png)
##   --quit-after=<n>     quit after n frames of free play
##   --ai-faction=<Name>  (scenario ai_attack, lane AI-1) the Medium computer's faction (default FactionMordor) in slot 1 at start position 1, seed 1234, 1500 cash;
##                        the camera watches the computer's base grow (fast forward), then follows its first attack team to the local base (screens ai1-*.png)
##   --scenario=fog       (lane VIS-1) the shroud: the explored start (retail UseShroud No), a scout of the local player walking to the computer's base and
##                        clearing the fog, the fog closing behind it, the radar; screens vis1-*.png, the shroud report printed as VIS1VIEW lines
##   --percent-shots=<prefix>  (build / showcase, lane RENDER-2) also save <prefix>-construction-<p>.png when the building reaches 0, 25, 50, 75 and 100 percent
##   --ai-prefix=<name>   (ai_attack) the screenshot prefix (default ai1); --until-destroyed: follow the attack until a local structure is destroyed (<prefix>-destroyed.png)
##   --scenario=ai_tactics  (lane AI-2) a computer (--ai-faction, --ai-state=3 Medium / 4 Hard, --seed) against the idle local player: fast forward until its first
##                        offensive tactic starts, then the camera follows that tactic's teams from the gathering to the engagement; the overlay names the tactic
##                        (get_ai_report), its step, team sizes and waypoints (markers on the ground). Screens ai2-*.png, AI2VIEW log lines
##   --scenario=castle_fall  (lane CASTLE-1) Mordor siege (6 MordorCatapult, 4 MordorMountainTroll created for the computer next to the local fortress) is ordered onto
##                        the local keep; when the keep dies the fortress (KeepDeathKillsEverything) kills its pads' buildings and leaves the game; the local player's
##                        remaining units are then killed (a scripted hit) so the victory rules can decide; the victory report is shown. Screens castle1-*.png
##   --scenario=walls_timeline  (lane BUILD-4) a Men wall span from the outermost pad, watched low from the side with --wall-side (else along the span); BUILD4VIEW
##                        lines give every piece's construction percent per logic frame; screens build4-*.png
extends Node3D

const TEMPLATES := {"FactionMen": 3, "FactionElves": 5, "FactionDwarves": 6, "FactionIsengard": 7, "FactionMordor": 8, "FactionWild": 9, "FactionAngmar": 10}
const BARRACKS := {
	"FactionMen": "GondorBarracks", "FactionElves": "ElvenBarracks", "FactionDwarves": "DwarfBarracks", "FactionIsengard": "IsengardUrukPit",
	"FactionMordor": "MordorOrcPit", "FactionWild": "GoblinCave", "FactionAngmar": "AngmarBarracks"}

var _map := "maps/map mp evendim/map mp evendim.map"
var _faction := "FactionMen"
var _scenario := ""
var _shots_dir := ""
var _quit_after := -1
var _percent_prefix := ""

var _fs: RefCounted
var _world: Node3D
var _camera: Camera3D
var _hud: Node2D
var _layer: CanvasLayer
var _env: WorldEnvironment
var _sun: DirectionalLight3D
var _label: Label
var _focus := Vector2.ZERO
var _cam_target := Vector2.ZERO
var _cam_height := 760.0
var _cam_back := 620.0
var _local := ""
var _ghost: Node3D
var _ghost_template := ""
var _ghost_tint := -1
var _builder: RefCounted
var _free_frames := 0
var _ai_faction := "FactionMordor"
var _ai_state := 3
var _seed_arg := -1
var _ai_prefix := "ai1"
var _ai_until_destroyed := false
var _start_objects: Array = []
var _wall_side := false


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--faction="):
			_faction = arg.substr(10)
		elif arg == "--wall-side":
			_wall_side = true
		elif arg.begins_with("--map="):
			_map = arg.substr(6)
		elif arg.begins_with("--scenario="):
			_scenario = arg.substr(11)
		elif arg.begins_with("--shots="):
			_shots_dir = arg.substr(8)
		elif arg.begins_with("--quit-after="):
			_quit_after = int(arg.substr(13))
		elif arg.begins_with("--ai-faction="):
			_ai_faction = arg.substr(13)
		elif arg.begins_with("--percent-shots="):
			_percent_prefix = arg.substr(16)
		elif arg.begins_with("--ai-prefix="):
			_ai_prefix = arg.substr(12)
		elif arg.begins_with("--ai-state="):
			_ai_state = int(arg.substr(11))
		elif arg.begins_with("--seed="):
			_seed_arg = int(arg.substr(7))
		elif arg == "--until-destroyed":
			_ai_until_destroyed = true
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
	if not mount.ok:
		_fail("mount failed: %s" % [mount.errors])
		return
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0) # the base game shows black outside the map (owner, 2026-10-06)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	_env = WorldEnvironment.new()
	_env.environment = env
	add_child(_env)
	_camera = Camera3D.new()
	_camera.fov = 45
	_camera.near = 2.0
	_camera.far = 60000.0
	add_child(_camera)
	_world = ClassDB.instantiate("GameWorld")
	add_child(_world)
	var setup: Dictionary = _world.setup(_fs)
	if not setup.ok:
		_fail("object world failed: %s" % [setup.errors])
		return
	var slots: Array = []
	for i in 8:
		slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	slots[0] = {"state": 6, "name": "Gimli", "accepted": true, "color": 0, "start_pos": 0, "player_template": TEMPLATES[_faction], "team": -1}
	slots[1] = {"state": 2, "name": "Easy AI", "accepted": true, "color": 1, "start_pos": 4, "player_template": TEMPLATES["FactionMordor"], "team": -1}
	var game_seed := 4711
	var cash := 5000
	if _scenario == "ai_attack" or _scenario == "ai_tactics":
		# lane AI-1: a Medium computer (slot state 3) against the idle local player, the setup of the retail attack tests
		slots[0] = {"state": 6, "name": "Human", "accepted": false, "color": 0, "start_pos": 0, "player_template": TEMPLATES[_faction], "team": 0}
		slots[1] = {"state": _ai_state if _scenario == "ai_tactics" else 3, "name": "Computer", "accepted": false, "color": 1, "start_pos": 1, "player_template": TEMPLATES[_ai_faction], "team": 1}
		for i in range(2, 8):
			slots[i].accepted = false
		game_seed = _seed_arg if _seed_arg >= 0 else 1234
		cash = 1500
	var message := {"mode": "skirmish", "difficulty": 1, "rank_points": 0, "map": _map, "map_crc": 0, "map_size": 0, "map_mask": 0, "seed": game_seed, "starting_cash": cash, "slots": slots}
	var prep: Dictionary = _world.prepare_new_game(message)
	if not prep.ok:
		_fail("prepare_new_game: %s" % [prep.errors])
		return
	var rep: Dictionary = _world.start_new_game({})
	if not rep.ok:
		_fail("start_new_game: %s" % [rep.errors.slice(0, 5)])
		return
	_start_objects = rep.start.starting_objects
	for e in rep.start.errors:
		print("START ERROR ", e)
	_local = str(rep.start.get("local_player", ""))
	print("BUILDVIEW started: local %s, %d objects, %d starting objects" % [_local, _world.get_object_count(), rep.start.starting_objects.size()])
	_apply_lighting(rep)
	var start: Dictionary = rep.start
	if start.get("have_local_start", false):
		var s: Vector3 = start.local_start
		_focus = Vector2(s.x, -s.z)
	else:
		_focus = Vector2.ZERO
	_cam_target = _focus
	_place_camera()
	_layer = CanvasLayer.new()
	_layer.layer = 10
	add_child(_layer)
	_hud = ClassDB.instantiate("InGameHudNode")
	_layer.add_child(_hud)
	_label = Label.new()
	_label.position = Vector2(10, 6)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	_layer.add_child(_label)
	_world.set_auto_advance(true)
	_builder = ClassDB.instantiate("W3DModelBuilder")
	await get_tree().process_frame
	# CAM-1: the HUD owns the camera (the retail tactical camera) and starts it on the focus point
	var hs: Dictionary = _hud.setup(_fs, _world, _camera, _local, {"camera_start": Vector3(_focus.x, 0.0, -_focus.y)})
	print("HUD setup: ok=%s errors=%s" % [hs.ok, hs.errors])
	if not hs.ok:
		_fail("HUD setup failed")
		return
	if not _scenario.is_empty():
		_run_scenario()


func _fail(message: String) -> void:
	push_error(message)
	print("BUILDVIEW FAIL: ", message)
	get_tree().quit(1)


func _apply_lighting(rep: Dictionary) -> void:
	if not rep.has("lighting"):
		return
	var lit: Dictionary = rep.lighting
	_env.environment.ambient_light_color = lit.ambient
	_env.environment.ambient_light_energy = 1.0
	_sun = DirectionalLight3D.new()
	_sun.light_color = lit.diffuse
	_sun.shadow_enabled = false
	var dir: Vector3 = lit.direction
	if dir.length() > 0.0001:
		_sun.transform = Transform3D(Basis.looking_at(dir.normalized(), Vector3.UP if absf(dir.normalized().y) < 0.99 else Vector3.RIGHT), Vector3.ZERO)
	add_child(_sun)


func _place_camera() -> void:
	# CAM-1: the HUD's tactical camera looks at the target; the height is kept inside the retail camera's range (as hud_viewer does)
	if _hud == null or not _hud.has_method("camera_look_at"):
		return
	_hud.camera_look_at(_cam_target)
	_hud.camera_set_height(clampf(_cam_height * 300.0 / 900.0, 120.0, 300.0))


func _process(_delta: float) -> void:
	if _hud == null or not _hud.is_ready():
		return
	# (the arrow keys, the wheel and the mouse scroll / rotate the retail camera through the HUD's own translator, lane CAM-1)
	_update_ghost()
	var pl: Dictionary = _hud.get_placement()
	if _scenario != "ai_tactics": # (ai_tactics writes its own overlay)
		_label.text = "selected %s  frame %d  placing %s legal %s" % [_hud.get_selection(), _world.get_frame(), pl.get("template", ""), pl.get("legal", "-")]
	_free_frames += 1
	if _quit_after > 0 and _scenario.is_empty() and _free_frames >= _quit_after:
		get_tree().quit(0)


## The placement ghost: the template's own model at the pointer's site, tinted green when the site is legal and red when it is not (retail's cue).
func _update_ghost() -> void:
	var pl: Dictionary = _hud.get_placement()
	if not pl.get("placing", false) or not pl.get("has_ghost", false):
		if _ghost != null:
			_ghost.queue_free()
			_ghost = null
			_ghost_template = ""
		return
	var tmpl := str(pl.template)
	if _ghost == null or _ghost_template != tmpl:
		if _ghost != null:
			_ghost.queue_free()
		var model: String = _world.get_template_model(tmpl)
		_ghost = null
		_ghost_template = tmpl
		_ghost_tint = -1
		if model.is_empty():
			return
		_ghost = _builder.build_model(_fs, model)
		if _ghost == null:
			return
		add_child(_ghost)
	var legal := int(pl.legal)
	if legal != _ghost_tint:
		_ghost_tint = legal
		var mat := StandardMaterial3D.new()
		mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		mat.albedo_color = Color(0.3, 1.0, 0.3, 0.55) if legal == 0 else Color(1.0, 0.25, 0.2, 0.55)
		_tint(_ghost, mat)
	var h: float = _world.get_ground_height(pl.x, pl.y)
	_ghost.position = Vector3(pl.x, h, -pl.y)
	_ghost.rotation = Vector3(0, float(pl.angle), 0)


func _tint(node: Node, mat: Material) -> void:
	if node is MeshInstance3D:
		(node as MeshInstance3D).material_override = mat
	for c in node.get_children():
		_tint(c, mat)


func _save(path: String) -> void:
	var image := get_viewport().get_texture().get_image()
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var err := image.save_png(path)
	print("BUILDVIEW screenshot %s -> %s (%dx%d)" % [path, error_string(err), image.get_width(), image.get_height()])


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


## waits until the logic has run `n` more frames (the world advances in real time: the render rate of a software-rendered window is not the game's)
func _wait_logic(n: int) -> void:
	var target: int = _world.get_frame() + n
	while _world.get_frame() < target:
		await get_tree().process_frame


func _own_objects(suffix: String) -> Array:
	var out: Array = []
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if o.get("ok", false) and o.get("owner", "") == _local and str(o.get("template", "")).ends_with(suffix):
			out.append(o)
	return out


func _click_world(pos: Vector2) -> void:
	var px: Vector2 = _hud.world_to_pixel(pos)
	_hud.inject_mouse_move(px)
	await _frames(2)
	_hud.inject_mouse_button(1, true, px, false)
	await _frames(2)
	_hud.inject_mouse_button(1, false, px, false)
	await _frames(3)


func _legal_site(near: Vector2) -> Vector2:
	# a ring search for a point where the placement code says the barracks may stand: the ghost is moved there and the HUD's own verdict is read
	var tmpl: String = BARRACKS[_faction]
	for ring in range(2, 14):
		for k in 12:
			var a := TAU * float(k) / 12.0
			var p := near + Vector2(cos(a), sin(a)) * 60.0 * ring
			var px: Vector2 = _hud.world_to_pixel(p)
			_hud.inject_mouse_move(px)
			await _frames(2)
			var pl: Dictionary = _hud.get_placement()
			if pl.get("has_ghost", false) and int(pl.legal) == 0 and str(pl.template) == tmpl:
				return p
	return near


func _run_scenario() -> void:
	await _frames(40)
	var shots := _shots_dir if not _shots_dir.is_empty() else "user://screens"
	if _scenario == "ai_attack":
		await _run_ai_attack(shots)
		return
	if _scenario == "ai_tactics":
		await _run_ai_tactics(shots)
		return
	if _scenario == "fog":
		await _run_fog(shots)
		return
	if _scenario == "walls":
		await _run_walls(shots)
		return
	if _scenario == "walls_timeline":
		await _run_walls_timeline(shots)
		return
	if _scenario == "castle_fall":
		await _run_castle_fall(shots)
		return
	var slow := _scenario == "showcase"
	if _scenario == "fortress":
		_cam_height = 900.0
		_cam_back = 760.0
		_place_camera()
		await _frames(30)
		_save(shots.path_join("build1-fortress-%s.png" % _faction.substr(7).to_lower()))
		print("BUILDVIEW fortress objects: ", _world.get_object_count())
		get_tree().quit(0)
		return
	var porters := _own_objects("Porter")
	if porters.is_empty():
		_fail("the local player has no Porter")
		return
	var porter: Dictionary = porters[0]
	_cam_target = Vector2(porter.x, porter.y)
	_cam_height = 620.0
	_cam_back = 520.0
	_place_camera()
	await _frames(60 if slow else 20)
	if slow:
		_cam_height = 900.0
		_cam_back = 760.0
		_cam_target = _focus
		_place_camera()
		await _frames(60)
		_cam_target = Vector2(porter.x, porter.y)
		_cam_height = 620.0
		_cam_back = 520.0
		_place_camera()
		await _frames(30)
	await _click_world(Vector2(porter.x, porter.y))
	await _frames(40 if slow else 20)
	print("BUILDVIEW selected: ", _hud.get_selection(), " command set ", _hud.get_state().get("control_bar", {}).get("command_set", ""))
	var tmpl: String = BARRACKS[_faction]
	var ok: bool = _hud.press_command_button(tmpl)
	print("BUILDVIEW press ", tmpl, ": ", ok, " placement ", _hud.get_placement())
	if not ok:
		_fail("the command bar did not accept the %s button" % tmpl)
		return
	await _frames(30 if slow else 10)
	var site: Vector2 = await _legal_site(Vector2(porter.x, porter.y) + Vector2(-300, 200))
	# a few moves so the ghost is seen on its way, then the screenshot of the placement mode
	await _frames(40 if slow else 10)
	_save(shots.path_join("build1-placement-%s.png" % _faction.substr(7).to_lower()))
	var px: Vector2 = _hud.world_to_pixel(site)
	_hud.inject_mouse_move(px)
	await _frames(2)
	_hud.inject_mouse_button(1, true, px, false)
	await _frames(2)
	_hud.inject_mouse_button(1, false, px, false)
	await _frames(10)
	print("BUILDVIEW placed: ", _hud.get_placement(), " log ", _hud.get_message_log().slice(-2))
	# the Porter walks and works; follow the site
	var building_id := -1
	for i in 600:
		building_id = int(_world.find_object_by_template(tmpl))
		if building_id >= 0:
			break
		await get_tree().process_frame
	if building_id < 0:
		_fail("the building was never made")
		return
	var b: Dictionary = _world.get_object(building_id)
	_cam_target = Vector2(b.x, b.y)
	_cam_height = 520.0
	_cam_back = 440.0
	_place_camera()
	var shot_mid := false
	var percent_next := 0
	var start_frame: int = _world.get_frame()
	while _world.get_frame() < start_frame + 900:
		await get_tree().process_frame
		b = _world.get_object(building_id)
		# RENDER-2: the build-up at 0 / 25 / 50 / 75 percent (100: the finished building below)
		if not _percent_prefix.is_empty() and percent_next <= 75 and b.get("under_construction", false) and float(b.get("construction_percent", -1.0)) >= float(percent_next):
			_save(shots.path_join("%s-construction-%d.png" % [_percent_prefix, percent_next]))
			print("BUILDVIEW %d%%: percent %s at logic frame %d" % [percent_next, b.get("construction_percent", "?"), _world.get_frame()])
			percent_next += 25
		if not shot_mid and float(b.get("construction_percent", 0.0)) >= 45.0:
			shot_mid = true
			_save(shots.path_join("build1-construction-%s.png" % _faction.substr(7).to_lower()))
			print("BUILDVIEW under construction: percent ", b.get("construction_percent", "?"), " at logic frame ", _world.get_frame())
		if b.get("ok", false) and not b.get("under_construction", true):
			break
	await _wait_logic(12 if not slow else 40)
	_save(shots.path_join("build1-complete-%s.png" % _faction.substr(7).to_lower()))
	if not _percent_prefix.is_empty():
		_save(shots.path_join("%s-construction-100.png" % _percent_prefix))
	if slow:
		await _wait_logic(25)
	print("BUILDVIEW done: ", _world.get_object(building_id))
	get_tree().quit(0)


## lane AI-1: the computer's base grows, then its first SimpleAttack team marches to the local base (the camera only watches; the logic is the game's)
func _run_castle_fall(shots: String) -> void:
	# lane CASTLE-1: the local fortress, its keep, the computer
	var keep := -1
	var centre := -1
	var enemy := ""
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if not o.get("ok", false):
			continue
		if str(o.owner) == _local and str(o.template).ends_with("FortressCitadel"):
			keep = id
		if str(o.owner) == _local and str(o.template).ends_with("Fortress"):
			centre = id
	for so in _start_objects:
		var eo: Dictionary = _world.get_object(int(so.id))
		if eo.get("ok", false) and str(eo.owner) != _local and not str(eo.owner).is_empty():
			enemy = str(eo.owner)
	if keep < 0 or enemy.is_empty():
		_fail("castle_fall needs the local keep and a computer player (keep %d, enemy '%s')" % [keep, enemy])
		return
	var k: Dictionary = _world.get_object(keep)
	var kp := Vector2(k.x, k.y)
	print("CASTLE1VIEW keep %d %s health %.0f, centre %d, computer %s" % [keep, k.template, float(k.health), centre, enemy])
	_cam_target = kp
	_cam_height = 820.0
	_cam_back = 680.0
	_place_camera()
	await _frames(20)
	_save(shots.path_join("castle1-fortress.png"))
	# the siege, out of the keep's reach to the side of the computer's base
	var away := (_cam_target - kp).normalized()
	var base := Vector2.ZERO
	var n := 0
	for so in _start_objects:
		var bo: Dictionary = _world.get_object(int(so.id))
		if bo.get("ok", false) and str(bo.owner) == enemy:
			base += Vector2(bo.x, bo.y)
			n += 1
	if n > 0:
		away = ((base / float(n)) - kp).normalized()
	var side := Vector2(-away.y, away.x)
	# the computer's player index (create_object takes the index of the player list): the first index whose object belongs to it
	var enemy_index := -1
	var siege: Array = []
	var p0 := kp + away * 330.0 - side * 2.5 * 45.0
	for idx in 16:
		var probe: int = _world.create_object("MordorCatapult", idx, p0.x, p0.y, atan2(-away.y, -away.x))
		if probe < 0:
			continue
		if str(_world.get_object(probe).get("owner", "")) == enemy:
			enemy_index = idx
			siege.append(probe)
			break
		_world.destroy_object(probe)
	if enemy_index < 0:
		_fail("no player index for %s" % enemy)
		return
	for i in range(1, 6):
		var p := kp + away * 330.0 + side * (float(i) - 2.5) * 45.0
		var id: int = _world.create_object("MordorCatapult", enemy_index, p.x, p.y, atan2(-away.y, -away.x))
		if id >= 0:
			siege.append(id)
	for i in 4:
		var p := kp + away * 260.0 + side * (float(i) - 1.5) * 60.0
		var id: int = _world.create_object("MordorMountainTroll", enemy_index, p.x, p.y, atan2(-away.y, -away.x))
		if id >= 0:
			siege.append(id)
	print("CASTLE1VIEW siege ", siege.size(), " objects")
	if siege.is_empty():
		_fail("no siege object could be made")
		return
	_world.order_attack(siege, keep)
	_cam_target = kp + away * 120.0
	_cam_height = 900.0
	_cam_back = 740.0
	_place_camera()
	_world.set_time_scale(2.0)
	var shot_hit := false
	var max_hp: float = float(k.max_health)
	var fell_frame := -1
	for i in 30000:
		await get_tree().process_frame
		var kd: Dictionary = _world.get_object(keep)
		var cd: Dictionary = _world.get_object(centre) if centre >= 0 else {}
		_label.text = "frame %d  keep %s  fortress %s" % [_world.get_frame(), ("%.0f / %.0f" % [float(kd.health), max_hp]) if kd.get("ok", false) else "gone", "standing" if cd.get("ok", false) else "gone"]
		if not shot_hit and kd.get("ok", false) and float(kd.health) < max_hp * 0.6:
			shot_hit = true
			_save(shots.path_join("castle1-siege.png"))
		if fell_frame < 0 and (not kd.get("ok", false) or float(kd.health) <= 0.0):
			fell_frame = _world.get_frame()
			_world.set_time_scale(1.0)
			print("CASTLE1VIEW the keep falls at frame %d" % fell_frame)
		if fell_frame >= 0 and _world.get_frame() == fell_frame + 3:
			_save(shots.path_join("castle1-falling.png"))
			print("CASTLE1VIEW fortress centre after the keep's death: ", cd)
		if fell_frame >= 0 and _world.get_frame() >= fell_frame + 60:
			break
	if fell_frame < 0:
		_fail("the keep did not fall")
		return
	_save(shots.path_join("castle1-fallen.png"))
	# the local player's remaining units (its builders keep it in the game under rule 2): a scripted hit
	var killed := 0
	for id in _world.get_object_ids():
		var lo: Dictionary = _world.get_object(id)
		if lo.get("ok", false) and str(lo.owner) == _local and float(lo.get("health", 0.0)) > 0.0:
			if _world.build2_damage(id, 1.0e9):
				killed += 1
	print("CASTLE1VIEW local objects hit: ", killed)
	var vr: Dictionary = {}
	for i in 600:
		await get_tree().process_frame
		vr = _world.get_victory_report()
		if vr.get("single_alliance", false) and not vr.get("events", []).is_empty():
			break
	print("CASTLE1VIEW VICTORY: ", vr)
	var banner := Label.new()
	var lines := PackedStringArray()
	for ev in vr.get("events", []):
		if ev.kind == "player_defeated":
			lines.append("%s has been defeated (frame %d)" % [ev.player_name, ev.frame])
		else:
			lines.append("%s and allies are the last standing (frame %d)" % [ev.player_name, ev.frame])
	banner.text = ("VICTORY\n" if vr.get("local_victorious", false) else ("DEFEAT\n" if vr.get("local_defeated", false) else "")) + "\n".join(lines)
	banner.add_theme_font_size_override("font_size", 44)
	banner.add_theme_color_override("font_color", Color(1.0, 0.85, 0.4))
	banner.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	banner.add_theme_constant_override("outline_size", 8)
	banner.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	banner.set_anchors_and_offsets_preset(Control.PRESET_TOP_WIDE)
	banner.offset_top = 120.0
	var layer := CanvasLayer.new()
	layer.layer = 100
	layer.add_child(banner)
	add_child(layer)
	await _frames(150)
	_save(shots.path_join("castle1-defeat.png"))
	print("CASTLE1VIEW done")
	get_tree().quit(0 if vr.get("local_defeated", false) else 1)


func _run_ai_attack(shots: String) -> void:
	var ai_owner := ""
	var ai_base := Vector2.ZERO
	var n := 0
	for so in _start_objects:
		var o: Dictionary = _world.get_object(int(so.id))
		if o.get("ok", false) and so.structure and str(o.owner) != _local and not str(o.owner).is_empty():
			ai_owner = str(o.owner)
			ai_base += Vector2(o.x, o.y)
			n += 1
	if n == 0:
		_fail("no computer base")
		return
	ai_base /= float(n)
	print("AI1VIEW computer %s base at %s, local %s at %s" % [ai_owner, ai_base, _local, _focus])
	var home := _focus
	_cam_height = 1100.0
	_cam_back = 900.0
	_cam_target = ai_base
	_place_camera()
	_world.set_time_scale(8.0 if _ai_until_destroyed else 6.0)
	var shot_early := false
	var shot_grown := false
	var following := false
	var arrived_frame := -1
	var destroyed_frame := -1
	var shot_fight := false
	var last_report := 0
	var local_structures: Array = []
	for so in _start_objects:
		if so.structure and str(_world.get_object(int(so.id)).get("owner", "")) == _local:
			local_structures.append(int(so.id))
	while true:
		await get_tree().process_frame
		var frame: int = _world.get_frame()
		if frame / 300 != last_report:
			last_report = frame / 300
			var hp := ""
			for id in local_structures:
				var ls: Dictionary = _world.get_object(id)
				hp += "%s %.0f " % [ls.get("template", "gone"), float(ls.get("health", -1.0))]
			print("AI1VIEW frame %d local structures: %s" % [frame, hp])
		_label.text = "frame %d (%.0f s)  computer %s (Medium %s)" % [frame, frame / 5.0, ai_owner, _ai_faction.substr(7)]
		if not shot_early and frame >= 150:
			shot_early = true
			_save(shots.path_join(_ai_prefix + "-base-early.png"))
		if not shot_grown and frame >= 1000:
			shot_grown = true
			_save(shots.path_join(_ai_prefix + "-base-grown.png"))
		# the computer's horde nearest to the local base
		var best := Vector2.ZERO
		var best_d := INF
		for id in _world.get_object_ids():
			var o: Dictionary = _world.get_object(id)
			if not o.get("ok", false) or str(o.owner) != ai_owner or int(o.get("contained_by", 0)) != 0 or not str(o.template).ends_with("Horde"):
				continue
			var p := Vector2(o.x, o.y)
			var d := p.distance_to(home)
			if d < best_d:
				best_d = d
				best = p
		if not following and best_d < ai_base.distance_to(home) * 0.75:
			following = true
			_world.set_time_scale(3.0)
			_cam_height = 760.0
			_cam_back = 620.0
			print("AI1VIEW the attack is on its way at frame %d" % frame)
		if following:
			_cam_target = _cam_target.lerp(best, 0.08) if best_d < INF else _cam_target
			_place_camera()
		if arrived_frame < 0 and best_d < 450.0:
			arrived_frame = frame
			_world.set_time_scale(4.0 if _ai_until_destroyed else 1.0)
			# the local player's buildings at that moment (the big-health objects: the fortress citadel the attack targets, farms, ...)
			local_structures.clear()
			for id in _world.get_object_ids():
				var lo: Dictionary = _world.get_object(id)
				if lo.get("ok", false) and str(lo.owner) == _local and float(lo.get("max_health", 0.0)) >= 1000.0:
					local_structures.append(id)
			print("AI1VIEW the attack arrives at frame %d" % frame)
			_save(shots.path_join(_ai_prefix + "-attack-arrives.png"))
		if arrived_frame >= 0 and not _ai_until_destroyed and frame >= arrived_frame + 75:
			_save(shots.path_join(_ai_prefix + "-attack-fight.png"))
			break
		if arrived_frame >= 0 and _ai_until_destroyed:
			if not shot_fight and frame >= arrived_frame + 150:
				shot_fight = true
				_save(shots.path_join(_ai_prefix + "-attack-fight.png"))
			if destroyed_frame < 0:
				for id in local_structures:
					var ls: Dictionary = _world.get_object(id)
					if not ls.get("ok", false) or float(ls.get("health", 1.0)) <= 0.0:
						destroyed_frame = frame
						print("AI1VIEW a local structure fell at frame %d" % frame)
						_save(shots.path_join(_ai_prefix + "-destroyed.png"))
						break
			elif frame >= destroyed_frame + 50:
				_save(shots.path_join(_ai_prefix + "-aftermath.png"))
				break
		if frame > 5000:
			print("AI1VIEW stopped at frame 5000 (arrived %d, destroyed %d)" % [arrived_frame, destroyed_frame])
			break
	get_tree().quit(0)


## lane VIS-1: the shroud scenario (see the header)
func _shroud_line(tag: String) -> void:
	var r: Dictionary = _world.get_shroud_report()
	print("VIS1VIEW %s frame %d: cells %dx%d (size %s) clear %d fogged %d shrouded %d, hidden objects %d, pending unlooks %d, UseShroud %s" % [tag, _world.get_frame(),
		r.cells_x, r.cells_y, r.cell_size, r.clear, r.fogged, r.shrouded, r.hidden_objects, r.pending_unlooks, r.use_shroud])


func _run_fog(shots: String) -> void:
	# a Gondor soldier of the local player walks from its base to the middle of the map (the retail camera's height limits keep the view low)
	var r0: Dictionary = _world.get_shroud_report()
	var centre := Vector2(float(r0.cells_x) * float(r0.cell_size) * 0.5, float(r0.cells_y) * float(r0.cell_size) * 0.5)
	var scout_id: int = _world.create_object("GondorFighter", int(r0.local_player), _focus.x + 150.0, _focus.y - 150.0)
	if scout_id < 0:
		_fail("fog: the scout could not be made")
		return
	_cam_height = 1300.0
	_cam_target = _focus
	_place_camera()
	await _frames(20)
	_shroud_line("start")
	_save(shots.path_join("vis1-shroud-start.png"))
	_world.set_shroud_drawn(false)
	await _frames(5)
	_save(shots.path_join("vis1-no-shroud-reference.png"))
	_world.set_shroud_drawn(true)
	_world.order_move([scout_id], centre.x, centre.y)
	_world.set_time_scale(3.0)
	var shot_reveal := false
	var shot_trail := false
	var start_frame: int = _world.get_frame()
	var start_pos := _focus
	var trail_at := Vector2.ZERO
	while true:
		await get_tree().process_frame
		var frame: int = _world.get_frame()
		var o: Dictionary = _world.get_object(scout_id)
		if not o.get("ok", false):
			print("VIS1VIEW the scout is gone at frame %d" % frame)
			break
		var p := Vector2(o.x, o.y)
		_cam_target = _cam_target.lerp(p, 0.1)
		_place_camera()
		var r: Dictionary = _world.get_shroud_report()
		_label.text = "frame %d  shroud (local player): clear %d fogged %d shrouded %d cells  hidden objects %d" % [frame, r.clear, r.fogged, r.shrouded, r.hidden_objects]
		var walked := p.distance_to(start_pos)
		if not shot_reveal and walked > 700.0:
			shot_reveal = true
			trail_at = p
			_shroud_line("walking")
			_save(shots.path_join("vis1-units-revealing.png"))
		if not shot_trail and walked > 1300.0:
			shot_trail = true
			_shroud_line("trail")
			_cam_target = trail_at.lerp(p, 0.5)
			_place_camera()
			await _frames(5)
			_save(shots.path_join("vis1-fog-over-explored.png"))
		if p.distance_to(centre) < 150.0 or frame > start_frame + 900:
			_shroud_line("arrived")
			_save(shots.path_join("vis1-scout-at-centre.png"))
			break
	_world.set_time_scale(1.0)
	await _frames(10)
	_save(shots.path_join("vis1-radar.png"))
## lane BUILD-2: --scenario=walls (FactionMen): a structure rises on a fortress pad (its spawned worker builds it), a wall hub on the outermost pad builds a span outward
## (MSG_WALL_HUB_CONSTRUCT_SPAN: the segments rise in a wave), then the structure is damaged and a Porter repairs it (MSG_DO_REPAIR). Shots build2-*.png.
func _look_at(x: float, y: float, height: float, back: float) -> void:
	_cam_target = Vector2(x, y)
	_cam_height = height
	_cam_back = back
	_place_camera()


func _run_walls(shots: String) -> void:
	var built: Dictionary = _world.build2_construct_on_plot(_local, "MenTrebuchetSideExpansion")
	if not built.get("ok", false):
		_fail("construct on a pad: %s" % built.get("error", ""))
		return
	var bid: int = int(built.id)
	_look_at(float(built.x), float(built.y), 420.0, 360.0)
	var next := 25
	var start: int = _world.get_frame()
	while _world.get_frame() < start + 400:
		await get_tree().process_frame
		var b: Dictionary = _world.get_object(bid)
		var pc := float(b.get("construction_percent", -1.0))
		if next <= 75 and pc >= float(next):
			_save(shots.path_join("build2-construction-%d.png" % next))
			print("BUILDVIEW build2 construction %d%% at frame %d (health %s)" % [next, _world.get_frame(), b.get("health", "?")])
			next += 25
		if pc == -1.0 and not b.get("under_construction", true):
			break
	await _wait_logic(10)
	_save(shots.path_join("build2-construction-100.png"))
	var hub: Dictionary = _world.build2_wall_hub(_local, "MenWallHubSmallExpansion")
	if not hub.get("ok", false):
		_fail("wall hub: %s" % hub.get("error", ""))
		return
	var hx := float(hub.x)
	var hy := float(hub.y)
	var dir := Vector2(hx - float(hub.cx), hy - float(hub.cy)).normalized()
	var end := Vector2(hx, hy) + dir * 400.0
	var mid := (Vector2(hx, hy) + end) * 0.5
	_look_at(mid.x, mid.y, 620.0, 520.0)
	await _wait_logic(10)
	_save(shots.path_join("build2-wall-hub.png"))
	var ok: bool = _world.build2_wall_span(int(hub.hub), "MenWallHubSmall", hx, hy, end.x, end.y, 1 << 13)
	print("BUILDVIEW build2 wall span sent: ", ok)
	await _wait_logic(80)
	_save(shots.path_join("build2-wall-rising.png"))
	await _wait_logic(240)
	_save(shots.path_join("build2-wall-complete.png"))
	var segs := 0
	for id in _world.get_object_ids():
		var o: Dictionary = _world.get_object(id)
		if str(o.get("template", "")).begins_with("MenWall") and str(o.get("owner", "")) == _local:
			segs += 1
	print("BUILDVIEW build2 wall pieces: ", segs)
	# repair: the finished structure loses half its health, a Porter repairs it
	var b2: Dictionary = _world.get_object(bid)
	_world.build2_damage(bid, float(b2.get("max_health", 0.0)) * 0.5)
	var porters := _own_objects("Porter")
	if porters.is_empty():
		_fail("no Porter for the repair")
		return
	_world.build2_repair([int(porters[0].id)], bid)
	_look_at(float(built.x), float(built.y), 520.0, 440.0)
	await _wait_logic(40)
	_save(shots.path_join("build2-repair.png"))
	print("BUILDVIEW build2 repair started: health ", _world.get_object(bid).get("health", "?"))
	start = _world.get_frame()
	while _world.get_frame() < start + 900:
		await get_tree().process_frame
		var r: Dictionary = _world.get_object(bid)
		if float(r.get("health", 0.0)) >= float(r.get("max_health", 1.0)):
			break
	_save(shots.path_join("build2-repaired.png"))
	print("BUILDVIEW build2 repaired: ", _world.get_object(bid).get("health", "?"), " at frame ", _world.get_frame())
	get_tree().quit(0)

## lane BUILD-4: --scenario=walls_timeline (FactionMen): a wall hub on the outermost pad builds a span outward and the camera watches it from the side, low;
## every logic frame prints each piece's construction percent in the order of the span (BUILD4VIEW lines: frame, then percent per piece by distance from
## the hub; -1 = complete). Screens build4-*.png every 40 logic frames; use with `--write-movie out.avi --fixed-fps 30` for the clip.
func _run_walls_timeline(shots: String) -> void:
	var hub: Dictionary = _world.build2_wall_hub(_local, "MenWallHubSmallExpansion")
	if not hub.get("ok", false):
		_fail("wall hub: %s" % hub.get("error", ""))
		return
	var hx := float(hub.x)
	var hy := float(hub.y)
	var dir := Vector2(hx - float(hub.cx), hy - float(hub.cy)).normalized()
	if _wall_side:
		dir = Vector2(-dir.y, dir.x) # across the camera's view: the wall's long face to the camera
	var end := Vector2(hx, hy) + dir * 400.0
	var mid := (Vector2(hx, hy) + end) * 0.5
	_look_at(mid.x, mid.y, 360.0, 300.0)
	await _wait_logic(10)
	var before := {}
	for id in _world.get_object_ids():
		before[id] = true
	var ok: bool = _world.build2_wall_span(int(hub.hub), "MenWallHubSmall", hx, hy, end.x, end.y, 1 << 13)
	print("BUILD4VIEW span sent: ", ok, " at frame ", _world.get_frame())
	var pieces: Array = []
	var start: int = _world.get_frame()
	var last := -1
	var shot := 0
	while _world.get_frame() < start + 900:
		await get_tree().process_frame
		var f: int = _world.get_frame()
		if f == last:
			continue
		last = f
		if pieces.is_empty():
			for id in _world.get_object_ids():
				var o: Dictionary = _world.get_object(id)
				if not before.has(id) and str(o.get("template", "")).begins_with("MenWall"):
					pieces.append({"id": id, "d": Vector2(float(o.x) - hx, float(o.y) - hy).length(), "t": o.template})
			pieces.sort_custom(func(a, b): return a.d < b.d)
			if not pieces.is_empty():
				print("BUILD4VIEW pieces: ", pieces.map(func(p): return "%s@%.0f" % [p.t, p.d]))
		var row: Array = []
		var done := true
		for p in pieces:
			var pc := float(_world.get_object(int(p.id)).get("construction_percent", -1.0))
			row.append("%.1f" % pc)
			done = done and pc == -1.0
		print("BUILD4VIEW %d %s" % [f - start, " ".join(row)])
		if (f - start) % 40 == 0:
			_save(shots.path_join("build4-wall-%03d.png" % shot))
			shot += 1
		if done and not pieces.is_empty():
			break
	await _wait_logic(10)
	_save(shots.path_join("build4-wall-complete.png"))
	get_tree().quit(0)

## lane AI-2: one AI attack wave from the gathering to the engagement (see the header)
func _tactic_centre(t: Dictionary) -> Vector2:
	var c := Vector2.ZERO
	var n := 0
	for team in t.teams:
		for id in team.members:
			var o: Dictionary = _world.get_object(int(id))
			if o.get("ok", false):
				c += Vector2(o.x, o.y)
				n += 1
	return c / float(n) if n > 0 else Vector2.INF


func _marker(p: Vector2, color: Color) -> MeshInstance3D:
	var m := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = 14.0
	cyl.bottom_radius = 14.0
	cyl.height = 160.0
	m.mesh = cyl
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.material_override = mat
	add_child(m)
	m.global_position = Vector3(p.x, 120.0, -p.y)
	return m


func _run_ai_tactics(shots: String) -> void:
	_world.set_shroud_drawn(false) # the computer's wave gathers in its own base, under the local player's shroud
	_label.position = Vector2(10, 90) # (the movie writer keeps the bottom 720 rows of a taller window)
	_cam_height = 1100.0
	_cam_target = _focus
	_place_camera()
	_world.set_time_scale(8.0)
	var picked := {}
	var picked_player := -1
	var picked_kind := ""
	var markers: Array = []
	var last_desc := ""
	var shots_taken := {}
	var engaged_frame := -1
	while true:
		await get_tree().process_frame
		var frame: int = _world.get_frame()
		var report: Array = _world.get_ai_report()
		if picked.is_empty():
			_label.text = "frame %d (%.0f s): the computer builds and gathers its first wave (fast forward)" % [frame, frame / 5.0]
			for t in report:
				if t.started and t.kind != "FarmKillSquad" and t.teams.size() > 0:
					picked = t
					picked_player = int(t.player)
					picked_kind = str(t.kind)
					_world.set_time_scale(1.5)
					var c0 := _tactic_centre(t)
					if c0 != Vector2.INF:
						_cam_target = c0
						_cam_height = 760.0
						_place_camera()
					print("AI2VIEW frame %d: %s started (%d teams), waypoints %s, target %s" % [frame, picked_kind, t.teams.size(), t.waypoints, t.get("target", "-")])
					for w in t.waypoints:
						markers.append(_marker(w, Color(1.0, 0.85, 0.2)))
					if t.get("has_target", false):
						markers.append(_marker(t.target, Color(1.0, 0.25, 0.2)))
					break
			if frame > 6000:
				print("AI2VIEW no offensive tactic by frame 6000")
				break
			continue
		# the same tactic in this frame's report (same player, kind and first member)
		var cur := {}
		for t in report:
			if int(t.player) == picked_player and str(t.kind) == picked_kind:
				cur = t
		if cur.is_empty():
			print("AI2VIEW frame %d: %s ended" % [frame, picked_kind])
			_save(shots.path_join("ai2-ended.png"))
			break
		var c := _tactic_centre(cur)
		if c != Vector2.INF:
			_cam_target = _cam_target.lerp(c, 0.25)
			_cam_height = 760.0
			_place_camera()
		var sizes := ""
		for team in cur.teams:
			sizes += "%d " % team.members.size()
		var stage := "gathering"
		if picked_kind == "FormationAttack":
			stage = ["gathered, waiting for the team to stand", "forming up facing the target", "attack-move to the target", "second attack-move"][clampi(int(cur.step), 0, 3)]
		elif picked_kind == "FlankAttack" and int(cur.step) > 0:
			stage = ["to its own centre", "to the flank point beside the target", "behind the target", "onto the target"][clampi(int(cur.step) - 1, 0, 3)]
		elif picked_kind == "PincerAttack" and int(cur.step) > 0:
			stage = "team 0 waypoint %d, team 1 waypoint %d" % [int(cur.step), int(cur.step2)]
		elif cur.started:
			stage = "attacking"
		var near: bool = c != Vector2.INF and cur.get("has_target", false) and c.distance_to(cur.target) < 700.0
		if near and engaged_frame < 0:
			engaged_frame = frame
			_world.set_time_scale(1.0)
			print("AI2VIEW frame %d: the wave reaches the target area" % frame)
		var desc := "%s step %d/%d" % [picked_kind, int(cur.step), int(cur.step2)]
		if desc != last_desc:
			last_desc = desc
			print("AI2VIEW frame %d: %s (%s), teams %s" % [frame, desc, stage, sizes])
			if not shots_taken.has(desc):
				shots_taken[desc] = true
				_save(shots.path_join("ai2-%s-step%d.png" % [picked_kind, int(cur.step)]))
		_label.text = "frame %d (%.0f s)  %s: %s  teams %s %s" % [frame, frame / 5.0, picked_kind, stage, sizes, ("  engaging" if engaged_frame >= 0 else "")]
		if engaged_frame >= 0 and frame >= engaged_frame + 300:
			_save(shots.path_join("ai2-engaged.png"))
			break
		if frame > 9000:
			break
	get_tree().quit(0)
