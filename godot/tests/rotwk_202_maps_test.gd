extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted = fs.mount_retail()
	assert(mounted.ok, str(mounted))
	var key := "maps/map mp amon amarth/map mp amon amarth.map"
	assert("rotwk_202_maps.big" in fs.get_archive_for_file(key))
	assert(not "rotwk_202_maps.big" in fs.get_archive_for_file("data/ini/gamedata.ini"))
	for asset in ["art/w3d/cu/cucrysore.w3d", "art/compiledtextures/cu/customcrystalore.dds", "data/ini/terrain.ini"]:
		assert("rotwk_202_maps.big" in fs.get_archive_for_file(asset), asset)
	var instancer = ClassDB.instantiate("W3DInstancer")
	root.add_child(instancer)
	assert(instancer.setup(fs).ok)
	assert(instancer.add_model("CUCrysOre") >= 0, str(instancer.get_errors()))
	var builder = ClassDB.instantiate("W3DModelBuilder")
	assert(builder.load_texture(fs, "customcrystalore.dds") != null)
	builder = null
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup = world.setup(fs)
	assert(setup.ok, str(setup))
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {"skirmish_ini":"/tmp/openbfme-202-maps-test.ini"}).ok)
	assert(shell.shell_push("Skirmish.apt"))
	for i in 80: shell.tick(0.03)
	assert(shell.lobby_apply({"profile":"Maps202Test"}).ok)
	for i in 30: shell.tick(0.03)
	var state: Dictionary = shell.lobby_state()
	assert(state.map_keys.size() >= 303, str(state.map_keys.size()))
	assert(shell.lobby_apply({"map_search":"Amon Amarth"}).ok)
	assert(key in shell.lobby_state().map_keys, str(shell.lobby_state()))
	assert(shell.lobby_apply({"map":key}).ok)
	shell.drawn_texts(true)
	shell.render(true)
	await process_frame
	shell.render(true)
	await process_frame
	var description_found := false
	for entry in shell.drawn_texts(false):
		if "mountain of fate" in String(entry.text): description_found = true
	assert(description_found, str(shell.drawn_texts(false)))
	var loaded = world.load_map("map mp amon amarth", {"slots":[{"player":"Player_1","faction":"FactionMen","human":true}]})
	assert(loaded.ok, str(loaded))
	print("ROTWK 2.02 MAPS PASS: ", state.map_keys.size(), " maps; Amon Amarth loaded")
	quit()
