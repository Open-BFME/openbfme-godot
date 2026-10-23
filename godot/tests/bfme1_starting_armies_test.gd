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
	for faction in [3, 4, 5, 6]: # BFME1 store: Rohan, Gondor, Isengard, Mordor
		var slots: Array = []
		for i in 8:
			slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
		slots[0] = {"state":6,"name":"Test","accepted":true,"color":0,"start_pos":0,"player_template":faction,"team":0}
		slots[1] = {"state":6,"name":"Other","accepted":true,"color":1,"start_pos":1,"player_template":faction,"team":1}
		var prepared: Dictionary = world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/map mp fords of isen/map mp fords of isen.map","seed":9001,"starting_cash":2000,"slots":slots})
		assert(prepared.ok, str(prepared))
		var loaded: Dictionary = world.start_new_game({"logic_thread":false})
		assert(loaded.ok, str(loaded.get("errors")))
		world.set_shroud_drawn(true)
		for i in 30: world.advance(0.2)
		var hordes: Array = []
		for oid in world.get_object_ids():
			var object = world.get_object(oid)
			if object.get("owner", "") == "Player_1" and object.get("template", "").ends_with("Horde"):
				hordes.append(object)
		# The mounted BFME1 library defines the faction's opening army.
		# Isengard starts with one Uruk battalion; the other factions have two.
		assert(hordes.size() == (1 if faction == 5 else 2), str(hordes))
		var other_hordes: Array = []
		for oid in world.get_object_ids():
			var object = world.get_object(oid)
			if object.get("owner", "") == "Player_2" and object.get("template", "").ends_with("Horde"):
				other_hordes.append(object)
		assert(other_hordes.size() == hordes.size(), "Each player gets its own opening army")
		# The side-local START_BASE reference must keep the armies at different bases.
		var first = Vector2(hordes[0].x, hordes[0].y)
		var other = Vector2(other_hordes[0].x, other_hordes[0].y)
		# tolerance: starts differ by thousands of units; only separation matters here.
		assert(first.distance_to(other) > 1000)
		for horde in hordes:
			assert(not horde.members.is_empty(), str(horde))
		var scripts = world.get_script_report()
		for action in ["SET_UNIT_REFERENCE", "CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION"]:
			assert(not scripts.unported_actions.has(action), action)
		if faction == 4:
			# Retail GondorCastleBaseKeep: primary BOX with 19-unit half extents
			# (goodfactionbuildings.ini); the extra cylinders fit inside that box.
			# Check actual soldier positions, not just the invisible horde anchors.
			for horde in hordes + other_hordes:
				var keep := {}
				for oid in world.get_object_ids():
					var candidate = world.get_object(oid)
					if candidate.template == "GondorCastleBaseKeep" and candidate.owner == horde.owner:
						keep = candidate
						break
				assert(not keep.is_empty())
				for member_id in horde.members:
					var member = world.get_object(member_id)
					var relative := Vector2(member.x - keep.x, member.y - keep.y).rotated(-float(keep.angle))
					assert(not Rect2(-19, -19, 38, 38).has_point(relative), "Starting soldier inside capital: " + str(member))
		if faction == 4:
			var camera = Camera3D.new()
			root.add_child(camera)
			var hud = ClassDB.instantiate("InGameHudNode")
			root.add_child(hud)
			var hud_setup = hud.setup(fs, world, camera, "Player_1", {"camera_start": Vector3(hordes[0].x, hordes[0].z, -hordes[0].y)})
			assert(hud_setup.ok, str(hud_setup))
			hud.inject_key(0x10, true) # DirectInput Q
			await process_frame
			hud.inject_key(0x10, false)
			await process_frame
			print("Q SELECTION ", hud.get_selection())
			assert(not hud.get_selection().is_empty(), "Q selects the opening army")
			for horde in hordes:
				assert(hud.get_selection().has(horde.id), "Q includes every local battalion")
			for horde in other_hordes:
				assert(not hud.get_selection().has(horde.id), "Q excludes the other player")
			hud.queue_free()
			camera.queue_free()
			await process_frame
		print("BFME1 STARTING ARMY ", faction, ": ", hordes.size(), " battalions")
	var fx = ClassDB.instantiate("FXPlayer")
	root.add_child(fx)
	var fx_setup = fx.setup(fs)
	assert(fx_setup.ok, str(fx_setup))
	for name in ["arrowFire", "burningTreeFire", "BalrogWingSmoke", "BalrogSwordFire"]:
		# tolerance: valid system IDs are positive and assigned at runtime.
		assert(fx.play_particle_system(name, Vector3.ZERO) > 0, name)
	for i in 30: fx.step_once()
	fx.rebuild()
	var stats = fx.get_stats()
	# tolerance: particle counts vary by mounted definitions; this verifies texture loading was exercised.
	assert(stats.sprites > 0, str(stats))
	assert(stats.missing_textures.is_empty(), str(stats.missing_textures))
	print("BFME1 PARTICLE TEXTURES PASS")
	print("BFME1 STARTING ARMIES PASS")
	quit(0)
