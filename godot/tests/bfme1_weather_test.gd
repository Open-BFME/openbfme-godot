## Standalone textures and rainy matches use BFME1's flat DDS layout.
extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty():
		print("SKIP: set BFME1_INSTALL")
		quit(77)
		return
	create_timer(120).timeout.connect(func(): push_error("BFME1 weather test timed out"); quit(1))
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_bfme1_path(install).ok)
	var textures = ClassDB.instantiate("W3DModelBuilder")
	var rain = textures.load_texture(fs, "exraindrop.tga")
	assert(rain != null, str(textures.get_report()))
	assert(rain.get_meta("retail_path") == "art\\textures\\exraindrop.dds")
	assert(textures.get_report().errors.is_empty(), str(textures.get_report()))
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	for name in ["ered lithui", "nimrodel"]:
		var slots: Array = []
		for i in 8:
			slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
		slots[0] = {"state":6,"name":"WeatherTest","accepted":true,"color":0,"start_pos":0,"player_template":4,"team":0}
		slots[1] = {"state":2,"name":"Enemy","accepted":true,"color":1,"start_pos":1,"player_template":5,"team":1}
		var map_path: String = "maps/map mp %s/map mp %s.map" % [name, name]
		assert(world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map_crc":0,"map_size":0,"map_mask":0,"map":map_path,"seed":1747,"starting_cash":2000,"slots":slots}).ok)
		var started: Dictionary = world.start_new_game({"logic_thread":false})
		assert(started.ok, str(started.get("errors")))
		for i in 5: world.advance(0.2)
		print("BFME1 WEATHER match started: ", name)
	world.free()
	print("BFME1 WEATHER PASS")
	quit()
