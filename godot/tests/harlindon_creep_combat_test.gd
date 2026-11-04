# Run with the retail installs mounted. Reproduces melee combat at Harlindon's northern camp.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var fs = RetailFileSystem.new()
	if not require_ok(fs.mount_retail().ok, "Mount failed"): return
	var world = GameWorld.new()
	root.add_child(world)
	if not require_ok(world.setup(fs).ok, "World setup failed"): return
	var slots: Array = []
	for i in 8:
		slots.append({"state": 6 if i < 2 else 1, "name": "P%d" % i,
			"accepted": true, "color": i if i < 2 else -1,
			"start_pos": i if i < 2 else -1, "player_template": 3 if i < 2 else -1, "team": i})
	if not require_ok(world.prepare_new_game({"mode": "skirmish", "difficulty": 1,
		"map": "maps/map mp harlindon/map mp harlindon.map", "map_crc": 0,
		"map_size": 0, "map_mask": 0, "seed": 2723,
		"starting_cash": 4000, "slots": slots}).ok, "Prepare failed"): return
	if not require_ok(world.start_new_game({"logic_thread": false, "wall_rebalance": false,
		"starting_walls": false}).ok, "Start failed"): return
	for i in 10:
		world.advance(0.2)
	var caves: Array = []
	for id in world.get_object_ids():
		var object: Dictionary = world.get_object(id)
		if object.get("template", "") == "MoriarGoblinLair":
			caves.append(object)
	if not require_ok(not caves.is_empty(), "Missing goblin lair"): return
	caves.sort_custom(func(a, b): return a.y > b.y)
	var cave: Dictionary = caves[0]
	var hero_id: int = world.create_object("GondorAragornMP", 3, cave.x - 120, cave.y, 0)
	# Let the spawned hero reveal the camp before sending a player command.
	for i in 5:
		world.advance(0.2)
	var initial_health: float = world.get_object(hero_id).health
	var retaliated := false
	for template in ["MordorGoblinArcher_Slaved", "MordorGoblinSwordsman_Slaved", "MoriarGoblinLair"]:
		var target: Dictionary = {}
		for id in world.get_object_ids():
			var object: Dictionary = world.get_object(id)
			if object.get("template", "") == template and Vector2(object.x, object.y).distance_to(Vector2(cave.x, cave.y)) < 400:
				target = object
				break
		if not require_ok(not target.is_empty(), "Missing camp target: " + template): return
		# Each target gets a fresh nearby hero so prior combat movement cannot strand the unit-target checks.
		world.destroy_object(hero_id)
		world.advance(0.2)
		hero_id = world.create_object("GondorAragornMP", 3, target.x - 40, target.y, 0)
		initial_health = world.get_object(hero_id).health
		for frame in 5: world.advance(0.2)
		if not require_ok(world.order_attack([hero_id], int(target.id)).ok, "Attack order rejected"): return
		var hit := false
		for i in 100:
			world.advance(0.2)
			var after: Dictionary = world.get_object(int(target.id))
			if not after.get("ok", false) or after.get("health", 0.0) < target.health:
				hit = true
				break
		if not require_ok(hit, "Aragorn failed to hit " + template): return
		for i in 20:
			world.advance(0.2)
		retaliated = retaliated or world.get_object(hero_id).health < initial_health
	# tolerance: any health loss proves that camp defenders can retaliate in upstream gameplay.
	if not require_ok(retaliated, "Creeps did not retaliate"): return
	print("PASS: Harlindon northern camp melee and creep retaliation in upstream gameplay")
	quit()

func require_ok(ok: bool, message: String) -> bool:
	if not ok:
		printerr("FAIL: ", message)
		quit(1)
	return ok
