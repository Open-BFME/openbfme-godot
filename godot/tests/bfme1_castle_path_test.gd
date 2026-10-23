## BFME1_INSTALL=/path/to/BFME1 godot --headless --path godot --script res://tests/bfme1_castle_path_test.gd
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 SKIP: set BFME1_INSTALL")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_bfme1_path(install)
	print("BFME1 MOUNT ", mounted)
	if not mounted.ok: quit(1); return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	print("BFME1 SETUP ", setup.get("templates"), " templates, ", setup.get("factions"), " factions")
	for error in setup.errors: print("BFME1 ERROR ", error)
	if not setup.ok: quit(1); return
	for case in [{"faction":4,"map":"maps/map mp fords of isen/map mp fords of isen.map","distance":400.0}, {"faction":3,"map":"maps/map mp anorien/map mp anorien.map","distance":550.0}]:
		var slots: Array = []
		for i in 8:
			slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
		slots[0] = {"state":6,"name":"PathTest","accepted":true,"color":0,"start_pos":0,"player_template":case.faction,"team":0}
		slots[1] = {"state":6,"name":"Opponent","accepted":true,"color":1,"start_pos":1,"player_template":5,"team":1}
		assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":case.map,"seed":9001,"starting_cash":2000,"slots":slots}).ok)
		assert(world.start_new_game({"logic_thread":false}).ok)
		for i in 30: world.advance(0.2)
		var horde := {}
		var keep := {}
		var gate := {}
		for id in world.get_object_ids():
			var o: Dictionary = world.get_object(id)
			if o.get("owner", "") != "Player_1": continue
			if o.template.ends_with("Horde") and horde.is_empty(): horde = o
			if "GateDoor" in o.template or "Postern" in o.template or "RohanCastleDoor" in o.template: gate = o
			if "Keep" in o.template or "Citadel" in o.template: keep = o
		assert(not keep.is_empty() and not gate.is_empty() and not horde.is_empty())
		var center := Vector2(keep.x, keep.y)
		var direction := (center - Vector2(gate.x, gate.y)).normalized()
		var goal: Vector2 = center + direction * float(case.distance)
		assert(world.order_move([horde.id], goal.x, goal.y, {}).ok)
		for i in 600:
			world.advance(0.2)
		var final: Dictionary = world.get_object(horde.id)
		# tolerance: orders snap to 10-unit navigation cells and locomotors stop within their configured arrival distance.
		assert(Vector2(final.x, final.y).distance_to(goal) < 20.0, str(final) + " goal " + str(goal))
		for member_id in final.members:
			var member: Dictionary = world.get_object(member_id)
			# tolerance: soldiers keep their formation slots around the battalion destination; allow 100 world units for its footprint.
			assert(Vector2(member.x, member.y).distance_to(goal) < 100.0, str(member) + " goal " + str(goal))
		print("BFME1 CASTLE PATH PASS faction ", case.faction, ": battalion and all members arrive after one order")
		assert(world.clear_game_data(false).ok)

	quit()
