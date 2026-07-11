extends Node3D
## Binds SimWorld to 3D view + input. Stages 1â€“4 interactive match.

const SimWorldScript = preload("res://src/sim/sim_world.gd")

@onready var camera_rig: Node3D = $CameraRig
@onready var cam: Camera3D = $CameraRig/Camera3D
@onready var ground: StaticBody3D = $Ground
@onready var entities: Node3D = $Entities
@onready var hud: CanvasLayer = $HUD

var world: SimWorld
var views: Dictionary = {} # id -> Node3D
var box_selecting: bool = false
var box_start: Vector2 = Vector2.ZERO
var attack_move_arm: bool = false
var placing_type: String = ""
var power_arm: String = ""
var stance_cycle_i: int = 0
var place_ghost: MeshInstance3D = null

func _ready() -> void:
	_build_readable_terrain()
	_start_match()
	SimClock.ticked.connect(_on_tick)
	SimClock.start()
	Events.toast.emit("PLACEHOLDER ART â€” colored squads + boxes (not final models)")

func _build_readable_terrain() -> void:
	## Replace flat green slab with a checker field so it doesn't read as one blob.
	var mesh_node: MeshInstance3D = ground.get_node_or_null("Mesh")
	if mesh_node:
		mesh_node.visible = false
	var root := Node3D.new()
	root.name = "TerrainVisual"
	add_child(root)
	var half := 22
	var cell := 12.0
	for z in range(-half, half):
		for x in range(-half, half):
			var tile := MeshInstance3D.new()
			var box := BoxMesh.new()
			box.size = Vector3(cell * 0.98, 0.15, cell * 0.98)
			tile.mesh = box
			tile.position = Vector3((x + 0.5) * cell, -0.05, (z + 0.5) * cell)
			var mat := StandardMaterial3D.new()
			var checker := ((x + z) % 2) == 0
			if checker:
				mat.albedo_color = Color(0.28, 0.36, 0.22)
			else:
				mat.albedo_color = Color(0.34, 0.40, 0.26)
			mat.roughness = 0.92
			tile.material_override = mat
			root.add_child(tile)
	# base pads so player/enemy starts read clearly
	_add_base_pad(Vector3(-70, 0.02, -70), Color(0.25, 0.40, 0.70))
	_add_base_pad(Vector3(70, 0.02, 70), Color(0.55, 0.18, 0.16))

func _add_base_pad(pos: Vector3, color: Color) -> void:
	var pad := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(36, 0.12, 36)
	pad.mesh = box
	pad.position = pos
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.roughness = 0.85
	pad.material_override = mat
	add_child(pad)

func _start_match() -> void:
	GameState.reset_match()
	var map: Dictionary = ContentDB.get_map(GameState.map_id)
	var seed := int(map.get("seed", 1337))
	world = SimWorldScript.new(seed)
	GameState.world = world
	GameState.started = true
	GameState.stage_flags["fog"] = false
	# Prefer map starts; fall back to mert player/enemy bases from globals
	var pb: Dictionary = ContentDB.globals.get("player_base", {"x": -128, "z": -128})
	var eb: Dictionary = ContentDB.globals.get("enemy_base", {"x": 128, "z": 128})
	var ps: Array = map.get("player_start", [pb.get("x", -128), pb.get("z", -128)])
	var es: Array = map.get("enemy_start", [eb.get("x", 128), eb.get("z", 128)])
	var p0 := Vector2(float(ps[0]), float(ps[1]))
	var p1 := Vector2(float(es[0]), float(es[1]))
	var pf := GameState.player_faction
	var ef := GameState.enemy_faction
	var pfac: Dictionary = ContentDB.get_faction(pf)
	var efac: Dictionary = ContentDB.get_faction(ef)
	var pfort := String(pfac.get("fortress", "fortress"))
	var efort := String(efac.get("fortress", "mfortress"))
	world.spawn_building(pfort, GameState.SIDE_PLAYER, p0, true, pf)
	world.spawn_building(efort, GameState.SIDE_ENEMY, p1, true, ef)
	_seed_faction_base(world, GameState.SIDE_PLAYER, pf, p0)
	_seed_faction_base(world, GameState.SIDE_ENEMY, ef, p1)
	# starters + a second free squad so the match opens playable
	var i := 0
	for su in pfac.get("starting_units", ["soldier"]):
		world.spawn_battalion(String(su), GameState.SIDE_PLAYER, p0 + Vector2(14 + i * 4, 14), pf)
		i += 1
	# extra free infantry so first 30s isn't empty
	world.spawn_battalion(String((pfac.get("starting_units", ["soldier"]) as Array)[0]), GameState.SIDE_PLAYER, p0 + Vector2(18, 10), pf)
	i = 0
	for su2 in efac.get("starting_units", ["orc"]):
		world.spawn_battalion(String(su2), GameState.SIDE_ENEMY, p1 + Vector2(-14 - i * 4, -14), ef)
		i += 1
	world.spawn_battalion(String((efac.get("starting_units", ["orc"]) as Array)[0]), GameState.SIDE_ENEMY, p1 + Vector2(-18, -10), ef)
	# wall kit near player (faction wall keys)
	var walls: Dictionary = pfac.get("wall_keys", {})
	var wkey := String(walls.get("wall", "wall"))
	var gkey := String(walls.get("gate", "gate"))
	var tkey := String((pfac.get("ai_plan", {}) as Dictionary).get("tower_key", "tower"))
	if ContentDB.buildings.has(wkey):
		world.spawn_building(wkey, GameState.SIDE_PLAYER, p0 + Vector2(36, -8), true, pf)
		world.spawn_building(wkey, GameState.SIDE_PLAYER, p0 + Vector2(42, -8), true, pf)
	if ContentDB.buildings.has(gkey):
		world.spawn_building(gkey, GameState.SIDE_PLAYER, p0 + Vector2(48, -8), true, pf)
	if ContentDB.buildings.has(tkey):
		world.spawn_building(tkey, GameState.SIDE_PLAYER, p0 + Vector2(54, -8), true, pf)
	if GameState.stage_flags.get("ring", true):
		world.spawn_ring((p0 + p1) * 0.5)
	var ai := SkirmishAI.new()
	ai.setup(GameState.SIDE_ENEMY, ef, GameState.difficulty)
	GameState.ai = ai
	# grant a little opening PP so spellbook is usable mid-skirmish
	GameState.add_pp(GameState.SIDE_PLAYER, 8)
	GameState.add_pp(GameState.SIDE_ENEMY, 6)
	camera_rig.global_position = Vector3(p0.x + 10, 0, p0.y + 10)
	cam.position = Vector3(0, 38, 34)
	cam.look_at(camera_rig.global_position)
	_sync_all_views()
	if GameAudio:
		GameAudio.set_music_state("explore")
	Events.toast.emit("%s vs %s — destroy the enemy fortress" % [pf.capitalize(), ef.capitalize()])
	Events.match_started.emit()
	if hud and hud.has_method("bind_match"):
		hud.bind_match(self)

func _seed_faction_base(w, side: int, faction: String, base: Vector2) -> void:
	var f: Dictionary = ContentDB.get_faction(faction)
	var plan: Dictionary = f.get("ai_plan", {})
	# place 2 econ + first production + well using slots like mert
	var econ_key := String(plan.get("econ_key", plan.get("economy", ["farm"])[0] if (plan.get("economy", []) as Array).size() else "farm"))
	var slots: Array = plan.get("econ_slots", [])
	for i in mini(2, maxi(1, slots.size())):
		var sl: Dictionary = slots[i] if i < slots.size() else {"x": 20 + i * 12, "z": 16}
		var pos := base + Vector2(float(sl.get("x", 20)), float(sl.get("z", 16))) * (1.0 if side == GameState.SIDE_PLAYER else -1.0)
		if ContentDB.buildings.has(econ_key):
			w.spawn_building(econ_key, side, pos, true, faction)
	var prod: Array = plan.get("production", [])
	var pit_slots: Array = plan.get("pit_slots", [])
	for i in mini(2, prod.size()):
		var btype := String(prod[i])
		var sl2: Dictionary = pit_slots[i] if i < pit_slots.size() else {"x": -20, "z": 20}
		var pos2 := base + Vector2(float(sl2.get("x", -20)), float(sl2.get("z", 20))) * (1.0 if side == GameState.SIDE_PLAYER else -1.0)
		if ContentDB.buildings.has(btype):
			w.spawn_building(btype, side, pos2, true, faction)
	var well := String(plan.get("well_key", ""))
	if well != "" and ContentDB.buildings.has(well):
		w.spawn_building(well, side, base + Vector2(-20, -24) * (1.0 if side == GameState.SIDE_PLAYER else -1.0), true, faction)

func _on_tick(dt: float) -> void:
	if GameState.paused or GameState.game_over:
		_sync_all_views()
		if hud and hud.has_method("refresh"):
			hud.refresh()
		return
	if GameState.ai and GameState.stage_flags.get("ai", true):
		GameState.ai.tick(world, dt)
	world.tick(dt)
	# adaptive music
	if GameAudio and SimClock.tick_index % 20 == 0:
		var fighting := false
		for b in world.living_battalions():
			if b.side == GameState.SIDE_PLAYER and b.target_id >= 0:
				fighting = true
				break
		GameAudio.set_music_state("battle" if fighting else "explore")
		if GameState.game_over:
			GameAudio.set_music_state("victory")
	if GameState.quake_shake_t > 0.0:
		GameState.quake_shake_t = maxf(0.0, GameState.quake_shake_t - dt)
		cam.h_offset = randf_range(-0.3, 0.3)
		cam.v_offset = randf_range(-0.2, 0.2)
	else:
		cam.h_offset = 0.0
		cam.v_offset = 0.0
	_sync_all_views()
	if hud and hud.has_method("refresh"):
		hud.refresh()

func _sync_all_views() -> void:
	var live: Dictionary = {}
	for b in world.living_battalions():
		live[b.id] = true
		_sync_battalion_view(b)
	for b in world.living_buildings():
		live[b.id] = true
		_sync_building_view(b)
	# purge
	var drop: Array = []
	for id in views:
		if not live.has(id):
			drop.append(id)
	for id in drop:
		views[id].queue_free()
		views.erase(id)

func _sync_battalion_view(b: SimTypes.Battalion) -> void:
	var node: Node3D = views.get(b.id, null)
	if node == null:
		node = _make_battalion_mesh(b)
		entities.add_child(node)
		views[b.id] = node
	node.global_position = Vector3(b.pos.x, 0.0, b.pos.y)
	node.rotation.y = -b.facing + PI
	var ring: MeshInstance3D = node.get_node_or_null("SelectRing")
	if ring:
		ring.visible = GameState.selected_ids.has(b.id)
	node.visible = world.is_visible_to_player(b.pos) or b.side == GameState.SIDE_PLAYER
	var label: Label3D = node.get_node_or_null("Label")
	if label:
		var udef := ContentDB.get_unit(b.type_id)
		label.text = "%s\n%d/%d" % [String(udef.get("name", b.type_id)), int(b.hp), int(b.hp_max)]
	var hpfill: MeshInstance3D = node.get_node_or_null("HpFill")
	if hpfill:
		var frac := clampf(b.hp / maxf(1.0, b.hp_max), 0.05, 1.0)
		hpfill.scale.x = frac
		hpfill.position.x = (frac - 1.0) * 1.2

func _sync_building_view(b: SimTypes.Building) -> void:
	var node: Node3D = views.get(b.id, null)
	if node == null:
		node = _make_building_mesh(b)
		entities.add_child(node)
		views[b.id] = node
	node.global_position = Vector3(b.pos.x, 0.0, b.pos.y)
	var scale_y := 1.0
	if not b.built:
		scale_y = maxf(0.2, b.build_progress)
	node.scale = Vector3(1, scale_y, 1)
	node.visible = world.is_visible_to_player(b.pos) or b.side == GameState.SIDE_PLAYER
	var label: Label3D = node.get_node_or_null("Label")
	if label:
		var bdef := ContentDB.get_building(b.type_id)
		label.text = String(bdef.get("name", b.type_id))

func _make_battalion_mesh(b: SimTypes.Battalion) -> Node3D:
	var root := Node3D.new()
	root.name = "B%d" % b.id
	var udef := ContentDB.get_unit(b.type_id)
	var visual := AssetFactory.make_unit_visual(b.type_id, b.side)
	visual.name = "Body"
	root.add_child(visual)
	var ring := MeshInstance3D.new()
	ring.name = "SelectRing"
	var disc := CylinderMesh.new()
	disc.top_radius = maxf(2.2, b.radius * 1.6)
	disc.bottom_radius = maxf(2.2, b.radius * 1.6)
	disc.height = 0.08
	ring.mesh = disc
	ring.position.y = 0.06
	var rmat := StandardMaterial3D.new()
	rmat.albedo_color = Color(0.45, 1.0, 0.35, 0.65)
	rmat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	rmat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	ring.material_override = rmat
	ring.visible = false
	root.add_child(ring)
	var hpbg := MeshInstance3D.new()
	hpbg.name = "HpBg"
	var bgmesh := BoxMesh.new()
	bgmesh.size = Vector3(2.4, 0.18, 0.12)
	hpbg.mesh = bgmesh
	hpbg.position = Vector3(0, 3.6, 0)
	var bgmat := StandardMaterial3D.new()
	bgmat.albedo_color = Color(0.1, 0.1, 0.1)
	bgmat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	hpbg.material_override = bgmat
	root.add_child(hpbg)
	var hpfill := MeshInstance3D.new()
	hpfill.name = "HpFill"
	var fmesh := BoxMesh.new()
	fmesh.size = Vector3(2.4, 0.14, 0.14)
	hpfill.mesh = fmesh
	hpfill.position = Vector3(0, 3.6, 0.02)
	var fmat := StandardMaterial3D.new()
	fmat.albedo_color = Color(0.25, 0.9, 0.3) if b.side == GameState.SIDE_PLAYER else Color(0.95, 0.25, 0.2)
	fmat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	hpfill.material_override = fmat
	root.add_child(hpfill)
	var label := Label3D.new()
	label.name = "Label"
	label.text = String(udef.get("name", b.type_id))
	label.font_size = 48
	label.pixel_size = 0.035
	label.position = Vector3(0, 4.1, 0)
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.outline_size = 8
	root.add_child(label)
	return root

func _make_building_mesh(b: SimTypes.Building) -> Node3D:
	var root := Node3D.new()
	root.name = "Bd%d" % b.id
	var visual := AssetFactory.make_building_visual(b.type_id, b.side)
	visual.name = "Body"
	root.add_child(visual)
	var label := Label3D.new()
	label.name = "Label"
	var bdef := ContentDB.get_building(b.type_id)
	label.text = String(bdef.get("name", b.type_id))
	label.font_size = 56
	label.pixel_size = 0.04
	label.position = Vector3(0, 8.0 if b.is_fortress else 5.5, 0)
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.outline_size = 10
	root.add_child(label)
	return root

func _side_color(side: int, hero: bool) -> Color:
	if side == GameState.SIDE_PLAYER:
		return Color(0.25, 0.55, 1.0) if not hero else Color(1.0, 0.85, 0.15)
	if side == GameState.SIDE_ENEMY:
		return Color(0.95, 0.22, 0.18) if not hero else Color(0.55, 0.15, 0.75)
	return Color(0.85, 0.65, 0.25)

func _process(delta: float) -> void:
	_update_camera(delta)

func _update_camera(delta: float) -> void:
	var speed := 40.0
	var move := Vector3.ZERO
	if Input.is_action_pressed("cam_forward"):
		move.z -= 1
	if Input.is_action_pressed("cam_back"):
		move.z += 1
	if Input.is_action_pressed("cam_left"):
		move.x -= 1
	if Input.is_action_pressed("cam_right"):
		move.x += 1
	if move != Vector3.ZERO:
		move = move.normalized().rotated(Vector3.UP, camera_rig.rotation.y) * speed * delta
		camera_rig.global_position += move
	if Input.is_action_pressed("cam_rotate_left"):
		camera_rig.rotate_y(1.2 * delta)
	if Input.is_action_pressed("cam_rotate_right"):
		camera_rig.rotate_y(-1.2 * delta)
	# edge pan
	var m := get_viewport().get_mouse_position()
	var vs := get_viewport().get_visible_rect().size
	var edge := 12.0
	var edge_move := Vector3.ZERO
	if m.x < edge:
		edge_move.x -= 1
	elif m.x > vs.x - edge:
		edge_move.x += 1
	if m.y < edge:
		edge_move.z -= 1
	elif m.y > vs.y - edge:
		edge_move.z += 1
	if edge_move != Vector3.ZERO:
		edge_move = edge_move.normalized().rotated(Vector3.UP, camera_rig.rotation.y) * speed * delta
		camera_rig.global_position += edge_move

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_WHEEL_UP and mb.pressed:
			cam.position.y = maxf(20.0, cam.position.y - 4.0)
			cam.position.z = maxf(20.0, cam.position.z - 3.0)
		elif mb.button_index == MOUSE_BUTTON_WHEEL_DOWN and mb.pressed:
			cam.position.y = minf(90.0, cam.position.y + 4.0)
			cam.position.z = minf(90.0, cam.position.z + 3.0)
		elif mb.button_index == MOUSE_BUTTON_LEFT:
			if mb.pressed:
				if power_arm != "":
					var pgp: Variant = _ground_pick()
					var ppos: Vector2 = Vector2(camera_rig.global_position.x, camera_rig.global_position.z)
					if pgp != null:
						ppos = pgp as Vector2
					if not GameState.power_unlocked(GameState.SIDE_PLAYER, power_arm):
						GameState.unlock_power(GameState.SIDE_PLAYER, power_arm)
					world.cast_power(GameState.SIDE_PLAYER, power_arm, ppos)
					if GameAudio:
						GameAudio.play_ui_click()
					power_arm = ""
					return
				if placing_type != "":
					var gp: Variant = _ground_pick()
					if gp != null:
						var built = world.place_building(placing_type, GameState.SIDE_PLAYER, gp)
						if built:
							placing_type = ""
							if GameAudio:
								GameAudio.play_ui_click()
					return
				box_selecting = true
				box_start = mb.position
			else:
				if box_selecting:
					_finish_box_select(mb.position)
					box_selecting = false
		elif mb.button_index == MOUSE_BUTTON_RIGHT and mb.pressed:
			if placing_type != "" or power_arm != "":
				placing_type = ""
				power_arm = ""
				Events.toast.emit("Cancelled")
				return
			var gp2: Variant = _ground_pick()
			if gp2 == null:
				return
			var hit: Variant = _pick_entity(mb.position)
			if attack_move_arm:
				world.order_attack_move(GameState.selected_ids, gp2)
				attack_move_arm = false
			elif hit is Array and hit.size() == 2:
				world.order_attack(GameState.selected_ids, int(hit[0]), int(hit[1]))
				if GameAudio:
					GameAudio.play_combat()
			else:
				world.order_move(GameState.selected_ids, gp2)
	if event is InputEventKey and event.pressed and not event.echo:
		var k := event as InputEventKey
		if k.keycode == KEY_A and not Input.is_key_pressed(KEY_CTRL):
			# only arm attack-move if not camera (camera uses physical A via action). Use shift+A style: standalone when selection exists
			if GameState.selected_ids.size() > 0:
				attack_move_arm = true
				Events.toast.emit("Attack-move armed")
		if k.keycode == KEY_S and GameState.selected_ids.size() > 0:
			world.order_stop(GameState.selected_ids)
		if k.keycode == KEY_Z:
			stance_cycle_i = (stance_cycle_i + 1) % 3
			world.set_stance(GameState.selected_ids, stance_cycle_i)
			var names := ["Aggressive", "Defensive", "Hold"]
			Events.toast.emit("Stance: %s" % names[stance_cycle_i])
		if k.keycode == KEY_F:
			_cast_selected_ability(0)
		if k.keycode == KEY_G:
			_cast_selected_ability(1)
		if k.keycode == KEY_B:
			_cast_selected_ability(2)
		if k.keycode == KEY_SPACE:
			var f: Variant = world.fortress_for(GameState.SIDE_PLAYER)
			if f:
				camera_rig.global_position = Vector3(f.pos.x, 0, f.pos.y)
		if k.keycode == KEY_ESCAPE:
			GameState.paused = not GameState.paused
			SimClock.paused = GameState.paused
			Events.toast.emit("Paused" if GameState.paused else "Resumed")
		if k.keycode == KEY_EQUAL or k.keycode == KEY_KP_ADD:
			SimClock.game_speed = minf(4.0, SimClock.game_speed + 0.5)
			Events.toast.emit("Speed %.1f" % SimClock.game_speed)
		if k.keycode == KEY_MINUS or k.keycode == KEY_KP_SUBTRACT:
			SimClock.game_speed = maxf(0.5, SimClock.game_speed - 0.5)
			Events.toast.emit("Speed %.1f" % SimClock.game_speed)
		if k.keycode == KEY_F5:
			if SaveGame.save_to_user("slot1", world):
				Events.toast.emit("Saved slot1")
		if k.keycode == KEY_F9:
			if SaveGame.load_from_user("slot1", world):
				_rebuild_views_from_world()
				Events.toast.emit("Loaded slot1")
		# control groups
		if k.keycode >= KEY_1 and k.keycode <= KEY_9:
			var n := int(k.keycode - KEY_0)
			if k.ctrl_pressed or k.shift_pressed:
				GameState.set_control_group(n, GameState.selected_ids)
				Events.toast.emit("Group %d set" % n)
			else:
				GameState.recall_control_group(n)
		# spellbook hotkeys 1-4 with Ctrl for powers (non-group)
		if k.keycode == KEY_H:
			_cast_power_hot("heal")
		if k.keycode == KEY_J:
			_cast_power_hot("sunflare")
		if k.keycode == KEY_K:
			_cast_power_hot("earthquake")
		if k.keycode == KEY_L:
			_cast_power_hot("reinforce")

func _cast_power_hot(power_id: String) -> void:
	var gp: Variant = _ground_pick()
	var pos: Vector2 = Vector2(camera_rig.global_position.x, camera_rig.global_position.z)
	if gp != null:
		pos = gp as Vector2
	# ensure unlock then cast
	if not GameState.power_unlocked(GameState.SIDE_PLAYER, power_id):
		GameState.add_pp(GameState.SIDE_PLAYER, 50) # allow testing / mid-match
		GameState.unlock_power(GameState.SIDE_PLAYER, power_id)
	world.cast_power(GameState.SIDE_PLAYER, power_id, pos)
	if GameAudio:
		GameAudio.play_ui_click()

func _rebuild_views_from_world() -> void:
	for id in views.keys():
		views[id].queue_free()
	views.clear()
	_sync_all_views()

func _cast_selected_ability(index: int) -> void:
	for id in GameState.selected_ids:
		var b: Variant = world.get_battalion(id)
		if b == null or b.dead:
			continue
		var udef := ContentDB.get_unit(b.type_id)
		if not bool(udef.get("hero", false)):
			continue
		var abs: Array = udef.get("abilities", [])
		if index < abs.size():
			world.try_cast_ability(b.id, String(abs[index]))
			return

func _finish_box_select(end_pos: Vector2) -> void:
	var a := box_start
	var b := end_pos
	if a.distance_to(b) < 6.0:
		var hit: Variant = _pick_entity(end_pos)
		if hit is Array:
			var id := int(hit[0])
			var kind := int(hit[1])
			if kind == SimTypes.EntityKind.BATTALION:
				var bat: Variant = world.get_battalion(id)
				if bat and bat.side == GameState.SIDE_PLAYER:
					GameState.set_selected([id])
					return
			elif kind == SimTypes.EntityKind.BUILDING:
				var bud: Variant = world.get_building(id)
				if bud and bud.side == GameState.SIDE_PLAYER:
					GameState.set_selected([id])
					return
		GameState.set_selected([])
		return
	var rect := Rect2(a, b - a).abs()
	var ids: Array = []
	for bat in world.living_battalions():
		if bat.side != GameState.SIDE_PLAYER:
			continue
		var sp := cam.unproject_position(Vector3(bat.pos.x, 1.0, bat.pos.y))
		if rect.has_point(sp):
			ids.append(bat.id)
	GameState.set_selected(ids)

func _ground_pick() -> Variant:
	var mouse := get_viewport().get_mouse_position()
	var from := cam.project_ray_origin(mouse)
	var dir := cam.project_ray_normal(mouse)
	if absf(dir.y) < 0.0001:
		return null
	var t := -from.y / dir.y
	if t < 0.0:
		return null
	var hit := from + dir * t
	return Vector2(hit.x, hit.z)

func _pick_entity(screen_pos: Vector2) -> Variant:
	var best_id := -1
	var best_kind := -1
	var best_d := 28.0
	for bat in world.living_battalions():
		if not world.is_visible_to_player(bat.pos) and bat.side != GameState.SIDE_PLAYER:
			continue
		var sp := cam.unproject_position(Vector3(bat.pos.x, 1.0, bat.pos.y))
		var d := sp.distance_to(screen_pos)
		if d < best_d:
			best_d = d
			best_id = bat.id
			best_kind = SimTypes.EntityKind.BATTALION
	for bud in world.living_buildings():
		if not world.is_visible_to_player(bud.pos) and bud.side != GameState.SIDE_PLAYER:
			continue
		var sp2 := cam.unproject_position(Vector3(bud.pos.x, 2.0, bud.pos.y))
		var d2 := sp2.distance_to(screen_pos)
		if d2 < best_d:
			best_d = d2
			best_id = bud.id
			best_kind = SimTypes.EntityKind.BUILDING
	if best_id >= 0:
		return [best_id, best_kind]
	return null

func begin_place(type_id: String) -> void:
	placing_type = type_id
	power_arm = ""
	Events.toast.emit("Place %s — left-click valid ground" % type_id)

func arm_power(power_id: String) -> void:
	power_arm = power_id
	placing_type = ""

func train_at_selected(unit_type: String) -> void:
	for id in GameState.selected_ids:
		var b: Variant = world.get_building(id)
		if b and not b.dead and b.side == GameState.SIDE_PLAYER:
			world.enqueue_train(b.id, unit_type)
			return
	# fallback: any player barracks/fortress that can train
	for bud in world.living_buildings():
		if bud.side != GameState.SIDE_PLAYER:
			continue
		var def := ContentDB.get_building(bud.type_id)
		var trains: Array = def.get("trains", [])
		var heroes: Array = def.get("heroes", [])
		if trains.has(unit_type) or heroes.has(unit_type):
			world.enqueue_train(bud.id, unit_type)
			return

