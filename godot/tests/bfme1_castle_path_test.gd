## BFME1_INSTALL=/path/to/BFME1 godot --headless --path godot --script res://tests/bfme1_castle_path_test.gd
extends SceneTree
func _initialize() -> void: call_deferred("_run")
# a failed check quits with 1: an assert would stop _run and leave the headless SceneTree running (and release builds strip asserts with their calls)
func _fail(message: String) -> void:
	print("BFME1 CASTLE PATH FAIL ", message)
	quit(1)
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
	# the retry runs only under the Uber simulation profile (Faithful keeps RotWK's single corridor search, RW 0x6FE7CB)
	var profile: Dictionary = world.set_simulation_profile("uber")
	if not profile.ok: _fail("set_simulation_profile: " + str(profile)); return
	for case in [{"faction":4,"map":"maps/map mp fords of isen/map mp fords of isen.map","distance":400.0}, {"faction":3,"map":"maps/map mp anorien/map mp anorien.map","distance":550.0}]:
		var slots: Array = []
		for i in 8:
			slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
		slots[0] = {"state":6,"name":"PathTest","accepted":true,"color":0,"start_pos":0,"player_template":case.faction,"team":0}
		slots[1] = {"state":6,"name":"Opponent","accepted":true,"color":1,"start_pos":1,"player_template":5,"team":1}
		if not world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":case.map,"seed":9001,"starting_cash":2000,"simulation_profile":1,"slots":slots}).ok: _fail("prepare_new_game"); return
		if not world.start_new_game({"logic_thread":false}).ok: _fail("start_new_game"); return
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
		if keep.is_empty() or gate.is_empty() or horde.is_empty(): _fail("no keep, gate or battalion: " + str([keep, gate, horde])); return
		var center := Vector2(keep.x, keep.y)
		var direction := (center - Vector2(gate.x, gate.y)).normalized()
		var goal: Vector2 = center + direction * float(case.distance)
		if not world.order_move([horde.id], goal.x, goal.y, {}).ok: _fail("order_move"); return
		for i in 600:
			world.advance(0.2)
		var final: Dictionary = world.get_object(horde.id)
		# tolerance: orders snap to 10-unit navigation cells and locomotors stop within their configured arrival distance.
		if Vector2(final.x, final.y).distance_to(goal) >= 20.0: _fail(str(final) + " goal " + str(goal)); return
		for member_id in final.members:
			var member: Dictionary = world.get_object(member_id)
			# tolerance: soldiers keep their formation slots around the battalion destination; allow 100 world units for its footprint.
			if Vector2(member.x, member.y).distance_to(goal) >= 100.0: _fail(str(member) + " goal " + str(goal)); return
		print("BFME1 CASTLE PATH PASS faction ", case.faction, ": battalion and all members arrive after one order")
		if not world.clear_game_data(false).ok: _fail("clear_game_data"); return

	quit()
