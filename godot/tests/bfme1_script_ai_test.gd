## First BFME1 authored script-AI regression; use -- --map="map mp rhun" for camps.
## Run with BFME1_INSTALL; supports --headless.
extends SceneTree

func _initialize() -> void:
	create_timer(120).timeout.connect(func(): push_error("BFME1 script AI test timed out"); quit(1))
	call_deferred("_run")

func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("BFME1 SKIP: set BFME1_INSTALL")
		quit(77)
		return
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_bfme1_path(install).ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var map := "map mp fords of isen"
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--map="): map = argument.trim_prefix("--map=")
	var slots: Array = []
	for i in 8:
		slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	slots[0] = {"state":4,"name":"AI","accepted":true,"color":0,"start_pos":0,"player_template":4,"team":0}
	slots[1] = {"state":6,"name":"Observer","accepted":true,"color":1,"start_pos":1,"player_template":5,"team":1}
	assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":"maps/%s/%s.map" % [map, map],"seed":9001,"starting_cash":2000,"slots":slots}).ok)
	assert(world.start_new_game({"logic_thread":false}).ok)
	for i in 1000:
		world.advance(0.2)
		if i % 100 == 0: await process_frame
	var buildings: Array = []
	for object in world.get_player_objects("Player_1"):
		if object.structure: buildings.append(object.template)
	# These are selected by the mounted AI libraries, never an engine build list.
	assert(buildings.has("GondorFarm"), str(buildings))
	assert(buildings.has("GondorForge"), str(buildings))
	assert(buildings.has("GondorMarketPlace"), str(buildings))
	var report: Dictionary = world.get_script_report()
	for action in ["BUILD_BASE_BUILDING_PER_TACTICAL_MARKER", "NAMED_BASE_UNPACK_FREE", "SET_PLAYER_MONEY_TO_COUNTER", "TEAM_MOVE_TO_NEAREST_OBJECT_OF_KINDOF"]:
		assert(not report.unported_actions.has(action), action)
	var marker_stop := "[S-5017] BFME1 tactical-marker construction uses first free castle pad; native proximity selection is unported"
	assert(report.notes.has(marker_stop), str(report.notes))
	assert(report.notes.has("[S-5017] BFME1 script base references resolve the keep through its CastleMember link"))
	assert(report.notes.has("[S-5017] BFME1 player-object rank condition uses live matching objects; native eligibility is unverified"))
	# Expansion safety checks remain explicit until their native helpers are ported.
	assert(report.unported_conditions.has("UNIT_THREAT_LEVEL"))
	assert(world.get_report().errors.is_empty(), str(world.get_report().errors))
	print("BFME1 SCRIPT AI PASS ", map, ": authored AI builds castle/camp economy from 2000 cash; expansion threat checks remain unported")
	quit()
