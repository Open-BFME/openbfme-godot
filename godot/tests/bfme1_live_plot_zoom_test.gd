## Live BFME1 skirmish regression. Requires --auto --advance=600 and a graphical renderer.
## Use --start-spot=1 on Fords of Isen to exercise the formerly occluded castle.
extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var game = load("res://scenes/game.tscn").instantiate()
	game._auto_task_started = true
	root.add_child(game)
	while not game._menu_revealed: await process_frame
	game._shell.shell_fscommand("AptMainMenu::Skirmish", "")
	for i in 120: await process_frame
	assert(await game._auto_lobby_and_start())
	for i in 10000:
		await process_frame
		# tolerance: wait for a logic update after match setup before inspecting the live objects.
		if game._state == game.State.PLAYING and game._hud != null and game._world.get_frame() >= 2: break
	assert(game._state == game.State.PLAYING)
	var plots: Array = []
	var center := Vector2.ZERO
	for object in game._world.get_player_objects(game._local_name):
		if str(object.get("template", "")).ends_with("BuildingFoundation"):
			plots.append(object.id)
			center += Vector2(object.x,object.y)
	assert(not plots.is_empty())
	center /= plots.size()
	print("LIVE PLOTS center ", center," ids ",plots)
	var overlays: Array = []
	for node in game._world.find_children("*", "MultiMeshInstance3D", true, false):
		var model := str(node.name).to_lower()
		if model.begins_with("gbfoundationx") or model.begins_with("ibfoundationx"):
			overlays.append(node)
	assert(not overlays.is_empty())
	game._hud.set_view_options({"zoom_out_factor":3.0})
	for factor in [1.0, 1.5, 2.0, 2.5, 3.0]:
		game._hud.camera_look_at(center)
		game._hud.camera_set_height(float(game._hud.get_camera().max_height) * factor)
		for i in 100:
			await process_frame
			game._hud.camera_look_at(center)
		await RenderingServer.frame_post_draw
		# Compare the same plot with only the floor-overlay nodes hidden. A posed
		# instance or a correct shader alone does not prove that the floor is visible.
		game._world.set_time_scale(0.0)
		var shown := root.get_texture().get_image()
		for overlay in overlays: overlay.visible = false
		await process_frame
		await RenderingServer.frame_post_draw
		var hidden := root.get_texture().get_image()
		for overlay in overlays: overlay.visible = true
		var plot = game._world.get_object(plots[0])
		var point := root.get_camera_3d().unproject_position(Vector3(plot.x, plot.z, -plot.y))
		var different := 0
		for y in range(int(point.y) - 5, int(point.y) + 6):
			for x in range(int(point.x) - 5, int(point.x) + 6):
				if shown.get_pixel(x,y) != hidden.get_pixel(x,y): different += 1
		# tolerance: any changed pixel proves the overlay contributes at the plot; no colour golden is assumed.
		assert(different > 0, "Floor hidden at zoom %s" % factor)
		game._world.set_time_scale(1.0)
		await process_frame
		await RenderingServer.frame_post_draw
		var shots := OS.get_environment("BFME1_PLOT_SCREENSHOTS")
		if not shots.is_empty():
			DirAccess.make_dir_recursive_absolute(shots)
			root.get_texture().get_image().save_png(shots.path_join("live-%sx.png" % factor))
		print("LIVE PLOTS PASS zoom ", factor, " changed plot pixels ", different, " logic frame ", game._world.get_frame())
	quit()
