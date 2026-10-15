extends SceneTree
func _initialize():
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("shellmap1", {"seed":0,"terrain":false}).ok)
	for value in [0.0,0.5,1.0,2.0,100.0]:
		var result = world.execute_lua("return setGameSpeed(%s)" % value)
		assert(result.ok,result.output)
		world.update_lua()
		assert(is_equal_approx(world.get_time_scale(),value))
	print("GAME SPEED INTEGRATION PASS")
	world.queue_free()
	quit()
