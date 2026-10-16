extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted = fs.mount_retail()
	assert(mounted.ok, str(mounted))
	var inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	assert(inst.setup(fs).ok)
	for model in ["EBWallTower", "IBWallTower"]:
		assert(inst.add_model(model) >= 0, model)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup = world.setup(fs)
	assert(setup.ok, str(setup))
	var slots := [{"player":"Player_1","faction":"FactionMen","human":true}]
	assert(world.load_map("map mp fall back 4p",{"slots":slots,"wall_rebalance":false}).ok)
	var player = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then return id end end")
	assert(player.ok)
	var native_hub = world.create_object("MenWallHubSmall",int(player.output),1800,1800,0)
	var native_health: float = world.get_object(native_hub).max_health
	var loaded = world.load_map("map mp fall back 4p",{"slots":slots,"wall_rebalance":true})
	assert(loaded.ok, str(loaded))
	player = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then return id end end")
	var hub = world.create_object("MenWallHubSmall",int(player.output),1800,1800,0)
	assert(is_equal_approx(float(world.get_object(hub).max_health),native_health*30),str(world.get_object(hub)))
	for template in ["BFME2LuaElvenWallTower","BFME2LuaIsengardWallTower"]:
		var tower = world.create_object(template,int(player.output),2200,2200,0)
		assert(tower > 0, template)
		assert(world.get_object(tower).max_health > 0)
	var start_slots: Array = []
	for i in 8: start_slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	start_slots[0] = {"state":6,"name":"Balance","accepted":true,"color":0,"start_pos":0,"player_template":3,"team":0}
	var prep = world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp evendim/map mp evendim.map","seed":9001,"starting_cash":2000,"slots":start_slots})
	assert(prep.ok, str(prep))
	var started = world.start_new_game({"starting_walls":true,"logic_thread":false})
	assert(started.ok, str(started))
	for i in 8: world.advance(0.2)
	var gates := 0
	var walls := 0
	var gate_objects: Array[Dictionary] = []
	var segments: Array[Dictionary] = []
	for object_id in world.get_object_ids():
		var object: Dictionary = world.get_object(object_id)
		if object.get("owner", "") != "Player_1": continue
		if "Wall" in object.template:
			walls += 1
			assert(not object.under_construction, str(object))
			if "Gate" in object.template:
				gates += 1
				gate_objects.append(object)
			if "WallSegment" in object.template and object.get("health", 0) > 0:
				segments.append(object)
	assert(walls >= 16, "Starting walls: " + str(walls))
	assert(gates >= 2, "Starting gates: " + str(gates))
	# Auto walls leave the first surviving tile two tile-spacings from the gate
	# center: the center and one tile on each side were replaced, not five tiles.
	for gate in gate_objects:
		var normal := Vector2(cos(gate.angle), sin(gate.angle))
		var along := Vector2(-normal.y, normal.x)
		var offsets: Array[float] = []
		for segment in segments:
			var delta := Vector2(segment.x - gate.x, segment.y - gate.y)
			if absf(delta.dot(normal)) < 1.0:
				offsets.append(delta.dot(along))
		offsets.sort()
		var spacing := INF
		var left := INF
		var right := INF
		for i in offsets.size():
			if offsets[i] < 0: left = minf(left, -offsets[i])
			else: right = minf(right, offsets[i])
			if i > 0: spacing = minf(spacing, offsets[i] - offsets[i-1])
		assert(is_finite(spacing) and spacing > 0)
		assert(absf(left / spacing - 2.0) < 0.1, "Auto gate left gap: " + str(offsets))
		assert(absf(right / spacing - 2.0) < 0.1, "Auto gate right gap: " + str(offsets))
	inst.queue_free()
	world.queue_free()
	await process_frame
	print("WALL BALANCE PASS")
	quit()
