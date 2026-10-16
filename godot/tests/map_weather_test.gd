extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.position = Vector3(1800, 500, -1800)
	camera.rotation_degrees.x = -60
	camera.make_current()
	var loaded: Dictionary = world.load_map("map mp withered heath")
	assert(loaded.ok, str(loaded.get("errors")))
	var snow: CPUParticles3D = world.get_node("MapSnowfall")
	# Changing the emitter transform must not translate flakes already in flight.
	assert(not snow.local_coords)
	assert(snow.emission_box_extents.y == 0)
	assert(snow.preprocess >= snow.lifetime)
	assert(snow.mesh.material.get_shader_parameter("snow_texture") != null)
	await process_frame
	var before := snow.global_position
	camera.position.x += 250
	await process_frame
	await process_frame
	assert(is_equal_approx(snow.global_position.x - before.x, 250))
	assert(not snow.local_coords)
	assert(is_equal_approx(snow.mesh.material.get_shader_parameter("snow_centre").x - before.x, 250))
	print("MAP WEATHER PASS: world-space flakes, moving emitter, fully filled snowfall column")
	quit()
