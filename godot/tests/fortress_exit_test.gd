# Retail regression: heroes and builders must leave the west Men fortress through its front door.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var fs = RetailFileSystem.new()
	assert(fs.mount_retail().ok)
	var world = GameWorld.new()
	root.add_child(world)
	assert(world.setup(fs).ok)
	var slots: Array = []
	for i in 8:
		slots.append({"state": 6 if i < 2 else 1, "name": "P%d" % i,
			"accepted": true, "color": i if i < 2 else -1,
			"start_pos": 1 - i if i < 2 else -1, "player_template": 3 if i < 2 else -1, "team": i})
	assert(world.prepare_new_game({"mode": "skirmish", "difficulty": 1,
		"map": "maps/map mp harlindon/map mp harlindon.map", "map_crc": 0,
		"map_size": 0, "map_mask": 0, "seed": 2723,
		"starting_cash": 4000, "slots": slots}).ok)
	var started: Dictionary = world.start_new_game({"logic_thread": false})
	assert(started.ok)
	for i in 10:
		world.advance(0.2)
	var before: Dictionary = {}
	var fortress: Dictionary = {}
	for id in world.get_object_ids():
		before[int(id)] = true
		var object: Dictionary = world.get_object(id)
		if object.get("template", "") == "MenFortressCitadel" and object.get("owner", "") == started.start.local_player:
			fortress = object
	assert(not fortress.is_empty())
	var hero_index := -1
	for hero in world.get_heroes(3):
		if hero.template == "GondorAragornMP": hero_index = int(hero.index)
	assert(hero_index != -1)
	world.queue_hero(3, int(fortress.id), hero_index)
	world.queue_unit(3, int(fortress.id), "MenPorter")
	var seen: Dictionary = {}
	var cleared: Dictionary = {}
	var forward := Vector2(cos(fortress.angle), sin(fortress.angle))
	for frame in 450:
		world.advance(0.2)
		for id in world.get_object_ids():
			if before.has(int(id)): continue
			var object: Dictionary = world.get_object(id)
			var template: String = object.get("template", "")
			if template not in ["GondorAragornMP", "MenPorter"]: continue
			seen[template] = true
			if cleared.has(template): continue
			var offset := Vector2(object.x - fortress.x, object.y - fortress.y)
			if offset.dot(forward) >= 80.0:
				cleared[template] = true
			else:
				# Follow the authored continuous doorway axis, including through the gate aperture.
				if abs(offset.cross(forward)) > 0.05:
					printerr("FAIL: %s deviated from the door centreline by %s" % [template, offset.cross(forward)])
					quit(1)
					return
	assert(seen.size() == 2, "Both hero and builder must finish production")
	assert(cleared.size() == 2, "Both hero and builder must clear the front door")
	print("PASS: west Men fortress hero and builder front-door exits")
	quit()
