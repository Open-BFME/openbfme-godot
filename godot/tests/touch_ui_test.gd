extends SceneTree
## Native building button queue clocks, including the ClockOverlay texture path.
func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	assert(world.load_map("map mp fall back 4p", {"slots": [
		{"player":"Player_1", "faction":"FactionMen", "human":true},
		{"player":"Player_2", "faction":"FactionMordor", "human":false}]}).ok)
	var owner: Dictionary = world.execute_lua("for id,p in pairs(bfme.players) do if p.name=='Player_1' then p.money=100000; return id end end")
	assert(owner.ok, str(owner))
	world.update_lua()
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	assert(hud.setup(fs, world, camera, "Player_1", {"camera_start":Vector3(1800,0,-1800)}).ok)
	for test in [
		{"building":"GondorBarracks", "command":"Command_ConstructGondorFighterHorde"},
		{"building":"MenFortressCitadel", "command":"Command_PurchaseUpgradeMenFortressBanners"}]:
		var id: int = world.create_object(test.building, int(owner.output), 1800, 1800, 0)
		assert(id > 0)
		world.advance(0.2)
		hud.select_controller_group(PackedInt64Array([id]))
		for i in 4: await process_frame
		if test.building == "MenFortressCitadel":
			for button in hud.get_command_buttons():
				if button.name == "Command_SelectUpgradesMenFortress" and not button.in_palantir:
					assert(hud.activate_command(button.slot, false, false))
					break
			for i in 3: await process_frame
		var command := {}
		for button in hud.get_command_buttons():
			if button.name == test.command: command = button; break
		assert(not command.is_empty(), str(hud.get_command_buttons()))
		var tap_rect := Rect2()
		for button in hud.get_hud6_state().radial.buttons:
			if button.name == test.command: tap_rect = button.rect
		assert(tap_rect.has_area(), str(hud.get_hud6_state()))
		# Deliberately leave the last mouse position elsewhere; touch emits no hover.
		hud.inject_mouse_move(Vector2(10,10))
		hud.inject_mouse_button(MOUSE_BUTTON_LEFT, true, tap_rect.get_center())
		hud.inject_mouse_button(MOUSE_BUTTON_LEFT, false, tap_rect.get_center())
		for i in 5:
			world.advance(0.2)
			await process_frame
		var state: Dictionary = hud.get_hud6_state().radial
		var found := false
		for button in state.buttons:
			if button.name == test.command:
				assert(button.timer > 0 and button.timer < 1, str(button))
				found = true
		assert(found, str(state))
		assert(state.clock_draws >= 2, str(state))
		print("TOUCH UI ", test.building, " clock draws=", state.clock_draws)
		assert(world.destroy_object(id))
		world.advance(0.2)
		for i in 3: await process_frame
	print("TOUCH UI PASS")
	quit()
