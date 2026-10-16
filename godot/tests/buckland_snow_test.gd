extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var key := "maps/buckland snow/buckland snow.map"
	assert(fs.file_exists(key))
	for leaf in ["buckland snow_art.tga", "buckland snow_pic.tga", "map.ini"]:
		assert(fs.file_exists("maps/buckland snow/" + leaf))
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {"skirmish_ini":"/tmp/openbfme-snow-map-test.ini"}).ok)
	assert(shell.shell_push("Skirmish.apt"))
	for i in 80: shell.tick(0.03)
	assert(shell.lobby_apply({"profile":"BucklandSnowTest"}).ok)
	for i in 30: shell.tick(0.03)
	assert(shell.lobby_apply({"map_search":"Buckland Snow"}).ok)
	assert(key in shell.lobby_state().map_keys, str(shell.lobby_state()))
	assert(shell.lobby_apply({"map":key}).ok)
	var slots: Array = []
	for i in 8: slots.append({"state":1,"name":"Closed","accepted":true,"color":-1,"start_pos":-1,"player_template":-1,"team":-1})
	slots[0] = {"state":6,"name":"Snow","accepted":true,"color":0,"start_pos":0,"player_template":3,"team":0}
	var prepared = world.prepare_new_game({"mode":"skirmish","difficulty":1,"rank_points":0,"map":key,"map_crc":0,"map_size":0,"map_mask":0,"seed":10,"starting_cash":2000,"slots":slots})
	assert(prepared.ok, str(prepared))
	var started = world.start_new_game({"starting_walls":false,"logic_thread":false})
	assert(started.ok, str(started))
	for i in 4: world.advance(0.2)
	print("BUCKLAND SNOW PASS: listed, selected, skirmish started")
	quit()
