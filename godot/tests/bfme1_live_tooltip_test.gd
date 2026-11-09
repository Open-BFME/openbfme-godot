## Native BFME1 tooltip in the real game scene; requires --auto and a BFME1 installation.
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
		# tolerance: wait for a logic update after match setup before selecting a live plot.
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
	game._hud.camera_look_at(center)
	for i in 100:
		await process_frame
		game._hud.camera_look_at(center)
	var plot = game._world.get_object(plots[0])
	var pixel := root.get_camera_3d().unproject_position(Vector3(plot.x,plot.z,-plot.y))
	game._hud.inject_mouse_move(pixel)
	game._hud.inject_mouse_button(1,true,pixel,false)
	game._hud.inject_mouse_button(1,false,pixel,false)
	for i in 90: await process_frame
	assert(game._hud.get_selection().has(plots[0]))
	var radial = game._hud.get_hud6_state().radial
	assert(not radial.buttons.is_empty())
	game._hud.inject_mouse_move(radial.buttons[0].rect.get_center())
	for i in 180:
		game._hud.inject_mouse_move(radial.buttons[0].rect.get_center())
		await process_frame
	var help = game._hud.get_hud6_state().help
	assert(help.render_name == "HelpBoxText" and help.shown, str(help))
	# tolerance: the native clip must draw some text; its font-dependent operation count is not fixed.
	assert(help.text_ops > 0, str(help))
	assert(str(help.calls).contains("ShowHelpBox("), str(help))
	assert(game._hud.get_report().errors.is_empty(), str(game._hud.get_report().errors))
	print("LIVE NATIVE TOOLTIP PASS ",help)
	await RenderingServer.frame_post_draw
	var shot := OS.get_environment("BFME1_TOOLTIP_SCREENSHOT")
	if not shot.is_empty(): root.get_texture().get_image().save_png(shot)
	quit()
