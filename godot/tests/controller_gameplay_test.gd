extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var loaded = world.load_map("map mp fall back 4p", {"slots": [
		{"player":"Player_1", "faction":"FactionMen", "human":true},
		{"player":"Player_2", "faction":"FactionMordor", "human":false}]})
	assert(loaded.ok, str(loaded))
	var player = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then return id end end")
	assert(player.ok and not player.output.is_empty(), str(player))
	var position := Vector2(1800, 1800)
	var id = world.create_object("MenPorter", int(player.output), position.x, position.y, 0)
	assert(id > 0)
	world.advance(0.2)
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	var setup = hud.setup(fs, world, camera, "Player_1", {"camera_start":Vector3(position.x,0,-position.y), "alternate_mouse":true})
	assert(setup.ok, str(setup))
	hud.set_controller_layout(true)
	var pan_start: Vector2 = hud.get_camera().position
	hud.camera_pan(Vector2(20, 10))
	var full_pan: Vector2 = hud.get_camera().position - pan_start
	assert(full_pan.length() > 0)
	hud.camera_look_at(pan_start)
	hud.camera_pan(Vector2(10, 5))
	var half_pan: Vector2 = hud.get_camera().position - pan_start
	assert(half_pan.distance_to(full_pan * 0.5) < 0.01)
	hud.camera_look_at(pan_start)
	await process_frame
	await process_frame
	var pixel = hud.world_to_pixel(position)
	hud.inject_mouse_move(pixel)
	assert(hud.get_controller_primary_button() == MOUSE_BUTTON_LEFT)
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT, true, pixel)
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT, false, pixel)
	await process_frame
	await process_frame
	assert(id in hud.get_selection(), str(hud.get_selection()))
	world.advance(0.2)
	position.x += 200
	hud.camera_look_at(position)
	await process_frame
	await process_frame
	pixel = hud.world_to_pixel(position)
	hud.inject_mouse_move(pixel)
	assert(hud.get_controller_primary_button() == MOUSE_BUTTON_RIGHT, str(hud.get_state()))
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT, true, pixel)
	hud.inject_mouse_button(MOUSE_BUTTON_RIGHT, false, pixel)
	await process_frame
	await process_frame
	assert("MSG_DO_MOVETO" in str(hud.get_message_log()), str(hud.get_message_log()))
	assert(id in hud.get_selection())
	await create_timer(1.5).timeout
	assert(hud.controller_attack_move())
	await process_frame
	await process_frame
	assert("MSG_DO_ATTACKMOVETO" in str(hud.get_message_log()))
	var attack_hint := false
	for hint in hud.get_move_hints().hints:
		attack_hint = attack_hint or hint.get("attack_move", false)
	assert(attack_hint, str(hud.get_move_hints()))
	var marker = load("res://scripts/move_hints.gd").new()
	root.add_child(marker)
	assert(marker.setup(hud, fs, world).ok)
	marker._process(0)
	assert(marker._attack_marker != null and marker._attack_marker.visible)
	marker.queue_free()
	var owner := int(player.output)
	var friend = world.create_object("MenPorter", owner, 2000, 1800, 0)
	var building = world.create_object("GondorBarracks", owner, 2200, 1800, 0)
	world.advance(0.2)
	# A selects a friendly object even while another unit is selected.
	for target in [{"id":friend, "at":Vector2(2000,1800)}, {"id":building, "at":Vector2(2200,1800)}]:
		pixel = await _aim(hud, target.at)
		assert(hud.get_controller_primary_button() == MOUSE_BUTTON_LEFT, str(hud.get_state()))
		hud.inject_mouse_button(MOUSE_BUTTON_LEFT, true, pixel)
		hud.inject_mouse_button(MOUSE_BUTTON_LEFT, false, pixel)
		await process_frame
		await process_frame
		assert(target.id in hud.get_selection(), str(hud.get_selection()))
		world.advance(0.2)
	hud.select_controller_group(PackedInt64Array([id, friend]))
	pixel = await _aim(hud, Vector2(2000,1800))
	hud.controller_deselect()
	assert(id in hud.get_selection() and not friend in hud.get_selection())
	await process_frame
	world.advance(0.2)
	assert(id in hud.get_logic_selection() and not friend in hud.get_logic_selection())
	# B over an unselected object keeps the remaining group; empty ground clears it.
	pixel = await _aim(hud, Vector2(2200,1800))
	hud.controller_deselect()
	assert(id in hud.get_selection())
	pixel = await _aim(hud, Vector2(2200,2000))
	var before_clear = str(hud.get_message_log()).count("MSG_DO_MOVETO")
	hud.controller_deselect()
	assert(hud.get_selection().is_empty())
	await process_frame
	world.advance(0.2)
	assert(hud.get_logic_selection().is_empty())
	assert(str(hud.get_message_log()).count("MSG_DO_MOVETO") == before_clear)
	# Context actions still take precedence over selection.
	var opponent = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_2' then return id end end")
	var fighter = world.create_object("GondorFighter", owner, 2400, 1800, 0)
	assert(world.create_object("MenPorter", int(opponent.output), 2600, 1800, 0) > 0)
	var archers = world.create_object("GondorArcherHorde", owner, 2400, 2200, 0)
	assert(world.create_object("GondorKeep", owner, 2600, 2200, 0) > 0)
	world.advance(0.4)
	hud.select_controller_group(PackedInt64Array([fighter]))
	pixel = await _aim(hud, Vector2(2600,1800))
	assert(hud.get_controller_primary_button() == MOUSE_BUTTON_RIGHT)
	hud.select_controller_group(PackedInt64Array([archers]))
	pixel = await _aim(hud, Vector2(2600,2200))
	assert(hud.get_controller_primary_button() == MOUSE_BUTTON_RIGHT)
	# Holding A anchors native placement; stick aiming rotates without panning.
	var voiced_groups_before: int = hud.get_message_log().count("MSG_CREATE_SELECTED_GROUP b1 o%d" % id)
	hud.select_controller_group(PackedInt64Array([id]))
	await process_frame
	await process_frame
	assert(hud.get_message_log().count("MSG_CREATE_SELECTED_GROUP b1 o%d" % id) == voiced_groups_before + 1, str(hud.get_message_log()))
	await process_frame
	world.advance(0.2)
	await process_frame
	await process_frame
	var construction := {}
	for command in hud.get_command_buttons():
		if command.get("template", "") == "GondorBarracks":
			construction = command
	assert(not construction.is_empty(), str(hud.get_command_buttons()))
	assert(hud.activate_command(construction.slot, construction.in_palantir, false))
	hud.set_placement_snap(true, 75)
	pixel = await _aim(hud, Vector2(2817,2611))
	for i in 4: await process_frame
	var snapped: Dictionary = hud.get_placement()
	assert(absf(snapped.x / 75.0 - roundf(snapped.x / 75.0)) < 0.001, str(snapped))
	assert(absf(snapped.y / 75.0 - roundf(snapped.y / 75.0)) < 0.001, str(snapped))
	assert(hud.get_placement_ghost_state().grid_visible)
	assert(hud.get_placement_ghost_state().grid_spacing == 75)
	assert(hud.get_placement_ghost_state().footprint_visible)
	assert(hud.get_placement_ghost_state().footprint_count == 1)
	assert(hud.get_controller_primary_button(200) == MOUSE_BUTTON_LEFT) # placement never uses assistance
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT, true, pixel)
	var locked_camera: Vector2 = hud.get_camera().position
	hud.controller_placement_aim(Vector2.RIGHT)
	var right_angle: float = hud.get_placement().angle
	assert(absf(right_angle / (PI/4) - roundf(right_angle / (PI/4))) < .001)
	var anchor := Vector2(hud.get_placement().x, hud.get_placement().y)
	hud.controller_placement_aim(Vector2.DOWN)
	assert(absf(angle_difference(right_angle, float(hud.get_placement().angle))) > 0.2)
	assert(Vector2(hud.get_placement().x, hud.get_placement().y).distance_to(anchor) < 0.1)
	assert(hud.get_camera().position == locked_camera)
	hud.controller_placement_aim(Vector2(.7,.3))
	var snapped_angle: float = hud.get_placement().angle
	assert(absf(snapped_angle / (PI/4) - roundf(snapped_angle / (PI/4))) < .001)
	hud.set_placement_snap(false, 75)
	for i in 4: await process_frame
	assert(not hud.get_placement_ghost_state().grid_visible)
	assert(not hud.get_placement_ghost_state().footprint_visible)
	var orders_before := str(hud.get_message_log()).count("MSG_DOZER_CONSTRUCT")
	hud.controller_deselect()
	await process_frame
	await process_frame
	assert(not hud.get_placement().placing)
	hud.inject_mouse_button(MOUSE_BUTTON_LEFT, false, pixel)
	await process_frame
	assert(str(hud.get_message_log()).count("MSG_DOZER_CONSTRUCT") == orders_before)
	# Assistance selects a nearby unit without warping the pointer or sending
	# a ground order; direct hits and a disabled/small radius retain normal input.
	hud.select_controller_group(PackedInt64Array([id]))
	pixel = await _aim(hud, Vector2(2000,1800))
	hud.inject_mouse_move(pixel)
	assert(hud.get_controller_primary_button(200) == MOUSE_BUTTON_LEFT)
	assert(id in hud.get_selection()) # a direct hit waits for the normal click
	var nearby_ground := Vector2.ZERO
	for offset in [Vector2(0,60), Vector2(-60,0), Vector2(60,0), Vector2(0,-60)]:
		if int(hud.pick_probe(pixel + offset, friend).picked) == 0:
			nearby_ground = pixel + offset
			break
	assert(nearby_ground != Vector2.ZERO)
	hud.inject_mouse_move(nearby_ground)
	assert(hud.get_controller_primary_button(0) == MOUSE_BUTTON_RIGHT, str(hud.get_state()))
	assert(hud.get_controller_primary_button(20) == MOUSE_BUTTON_RIGHT)
	var assisted_target: Dictionary = hud.get_controller_target(120)
	assert(assisted_target.id == friend and assisted_target.assisted and assisted_target.can_select)
	assert(id in hud.get_selection() and not friend in hud.get_selection())
	await process_frame
	await process_frame
	assert(friend in hud.get_icon_ui().bar_objects, str(hud.get_icon_ui()))
	assert(hud.get_controller_target(0).is_empty())
	await process_frame
	await process_frame
	assert(not friend in hud.get_icon_ui().bar_objects)
	hud.inject_mouse_move(nearby_ground)
	var assisted_voices_before: int = hud.get_message_log().count("MSG_CREATE_SELECTED_GROUP b1 o%d" % friend)
	assert(hud.get_controller_primary_button(120) == 0)
	assert(friend in hud.get_selection() and not id in hud.get_selection())
	await process_frame
	world.advance(0.2)
	assert(friend in hud.get_logic_selection())
	assert(hud.get_message_log().count("MSG_CREATE_SELECTED_GROUP b1 o%d" % friend) == assisted_voices_before + 1, str(hud.get_message_log()))
	pixel = await _aim(hud, Vector2(2000,1800))
	assert(hud.controller_select_area(0, false))
	assert(hud.controller_select_area(150, false))
	for i in 4: await process_frame
	assert(hud.get_placement_ghost_state().selection_circle_visible)
	assert(hud.controller_select_area(150, true))
	await process_frame
	await process_frame
	assert(friend in hud.get_selection())
	assert(not building in hud.get_selection())
	assert(not hud.get_placement_ghost_state().selection_circle_visible)
	hud.queue_free()
	camera.queue_free()
	await process_frame
	world.queue_free()
	await process_frame
	print("CONTROLLER GAMEPLAY PASS")
	quit()

func _aim(hud: Node, position: Vector2) -> Vector2:
	hud.camera_look_at(position)
	await process_frame
	await process_frame
	var pixel: Vector2 = hud.world_to_pixel(position)
	hud.inject_mouse_move(pixel)
	return pixel
