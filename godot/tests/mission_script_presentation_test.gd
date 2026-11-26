## Mounted-data checks for shared tutorial/campaign reveals and drawable markers.
extends SceneTree
var _failures := 0
func _check(condition: bool, message := "") -> void:
	if not condition:
		_failures += 1
		printerr("MISSION CHECK FAIL: ", message, " ", get_stack())
func _finish() -> void:
	if _failures == 0: print("MISSION SCRIPT PASS")
	quit(0 if _failures == 0 else 1)
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	create_timer(120).timeout.connect(func(): printerr("MISSION SCRIPT TIMEOUT"); quit(1))
	var game = load("res://scripts/game.gd").new()
	game._intro = "off"
	root.add_child(game)
	while game._campaign_flow == null or game._state not in [game.State.MENU, game.State.FAILED]: await process_frame
	_check(game._state == game.State.MENU, game._failed)
	await game._wait_main_ready()
	game._world.set_logic_thread(false)
	game._campaign_flow.hooks = true
	game._campaign_flow.start_tutorial("Basic")
	while game._state not in [game.State.PLAYING, game.State.FAILED]: await process_frame
	_check(game._state == game.State.PLAYING, game._failed)
	game._world.set_auto_advance(false)
	for i in 5:
		game._world.advance(0.2)
		await process_frame
	_assert_unexplored_shroud(game._world)
	_assert_opening_reveal(game._world)
	await _assert_spotlight(game)
	game.free()
	_finish()

func _assert_unexplored_shroud(world: Node) -> void:
	var report: Dictionary = world.get_shroud_report()
	_check(report.ok)
	var cells: PackedByteArray = world.get_shroud_cells()
	_check(cells.has(2)) # never explored cells remain shrouded
	var hidden := cells.find(2)
	var x := hidden % int(report.cells_x)
	var y := hidden / int(report.cells_x)
	var checked := false
	for mesh in world.get_node("Terrain").find_children("*", "MeshInstance3D", true, false):
		for surface in mesh.mesh.get_surface_count():
			var material = mesh.get_active_material(surface)
			if material is ShaderMaterial and material.shader.code.contains("terrain_common"):
				_check(material.get_shader_parameter("shroud_enabled") == true)
				var texture: Texture2D = material.get_shader_parameter("shroud_tex")
				_check(texture != null)
				# The texture has a one-texel border. Unexplored cells multiply terrain RGB by zero.
				_check(texture.get_image().get_pixel(x + 1, y + 1).r == 0.0)
				checked = true
				break
		if checked: break
	_check(checked, "A terrain material must bind the black unexplored shroud texture")

func _assert_opening_reveal(world: Node) -> void:
	# Retail Basic tutorial Map Setup: permanent radius 550 at this waypoint, plus its start-area trigger.
	var waypoint: Dictionary = world.get_waypoint("WP - Move Fighters Here")
	_check(waypoint.ok)
	_check(world.get_shroud_status_at(waypoint.x, waypoint.y) == 0)
	var report: Dictionary = world.get_shroud_report()
	var x := floori(float(waypoint.x) / float(report.cell_size))
	var y := floori(float(waypoint.y) / float(report.cell_size))
	var checked := false
	for mesh in world.get_node("Terrain").find_children("*", "MeshInstance3D", true, false):
		for surface in mesh.mesh.get_surface_count():
			var material = mesh.get_active_material(surface)
			if material is ShaderMaterial and material.shader.code.contains("terrain_common"):
				var texture: Texture2D = material.get_shader_parameter("shroud_tex")
				_check(texture.get_image().get_pixel(x + 1, y + 1).r == 1.0)
				checked = true
				break
		if checked: break
	_check(checked, "The scripted opening reveal must reach the terrain's shroud texture")

## lane DRAWDECAL-1: the object's decal shadow in this frame's list for the HUD's selection decal grid (empty: none)
func _spotlight(world: Node, object_id: int) -> Dictionary:
	for e in world.get_decal_shadows():
		if int(e.object) == object_id: return e
	return {}

func _assert_spotlight(game: Node) -> void:
	var world: Node = game._world
	var waypoint: Dictionary = world.get_waypoint("WP - Spotlight Fighters")
	_check(world.debug_script_action("CREATE_NAMED_ON_TEAM_AT_WAYPOINT", [
		{"type":14, "string":"Test Spotlight"}, {"type":15, "string":"InGameMarker"},
		{"type":3, "string":"PlyrCivilian/teamPlyrCivilian"}, {"type":7, "string":"WP - Spotlight Fighters"}]).ok)
	world.advance(0.2)
	for i in 3: await process_frame
	var marker: Dictionary = world.debug_script_unit("Test Spotlight")
	_check(marker.ok, str(marker))
	var decal := _spotlight(world, marker.id)
	_check(not decal.is_empty(), "The retail marker's additive shadow must be a ground decal")
	if decal.is_empty(): return
	_check(decal.size == 135.0 and decal.size_y == 135.0)
	_check(decal.x == marker.x and decal.y == marker.y) # no ShadowOffsetX / Y
	_check(decal.texture == "EXSpotLight.tga")
	_check(int(decal.style) == 0x800) # SHADOW_ADDITIVE_DECAL_DYNAMIC: added to the surface (sel_alpha_at)
	# SHADOW_ADDITIVE_DECAL_DYNAMIC fades in: ShadowOpacityStart 0 to Peak 255 over FadeInTime 1000 ms (RW 0x7323CE), white throughout
	_check((int(decal.color) & 0xFFFFFF) == 0xFFFFFF)
	_check((int(decal.color) >> 24) < 255, "The marker's spotlight must still be fading in (alpha %d)" % (int(decal.color) >> 24))
	for i in 8:
		world.advance(0.2)
		await process_frame
	decal = _spotlight(world, marker.id)
	_check(not decal.is_empty() and int(decal.color) == 0xFFFFFFFF, "The faded-in spotlight is white at Peak 255 (%s)" % str(decal))
	var shot := OS.get_environment("SPOTLIGHT_TEST_SHOT")
	if not shot.is_empty():
		game._hud.camera_look_at(Vector2(waypoint.x, waypoint.y))
		game._hud.camera_set_height(400.0)
		game._campaign_flow._cine._process(0.0)
		await create_timer(0.5).timeout
		await RenderingServer.frame_post_draw
		_check(root.get_texture().get_image().save_png(shot) == OK)
	# Position follows the ordinary drawable transform, not a fixed tutorial coordinate.
	_check(world.debug_script_place_at("Test Spotlight", waypoint.x + 40.0, waypoint.y + 20.0).ok)
	world.advance(0.2)
	for i in 3: await process_frame
	marker = world.debug_script_unit("Test Spotlight")
	decal = _spotlight(world, marker.id)
	_check(not decal.is_empty() and decal.x == marker.x and decal.y == marker.y)
	_check(world.debug_script_action("NAMED_KILL", [{"type":14, "string":"Test Spotlight"}]).ok)
	for i in 10:
		world.advance(0.2)
		await process_frame
	_check(_spotlight(world, marker.id).is_empty(), "Removing the scripted marker must remove its ground spotlight")
