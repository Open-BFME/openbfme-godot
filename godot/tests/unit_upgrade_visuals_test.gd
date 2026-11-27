## Real unit upgrade materials and forged-blade visibility. Requires retail files and a GPU.
## -- --captures=DIR saves gameplay viewport captures; no extracted assets are written.
extends SceneTree
var failures := 0

func _initialize() -> void:
	call_deferred("_run")

func check(ok: bool, message: String) -> void:
	print("UNIT UPGRADE VISUALS ", "ok " if ok else "FAIL ", message)
	if not ok:
		failures += 1

func frames(count: int) -> void:
	for i in count:
		await process_frame
		RenderingServer.force_draw(true, 0.0)

func _run() -> void:
	if DisplayServer.get_name() == "headless":
		print("SKIP: requires a GPU")
		quit(77)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_retail()
	if not mounted.ok:
		printerr("UNIT UPGRADE VISUALS mount failed: ", mounted)
		quit(1)
		return
	var vp := SubViewport.new()
	vp.size = Vector2i(768, 640)
	vp.own_world_3d = true
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(vp)
	var world = ClassDB.instantiate("GameWorld")
	vp.add_child(world)
	var setup: Dictionary = world.setup(fs)
	check(setup.ok, "world setup")
	if not setup.ok:
		quit(1)
		return
	var loaded: Dictionary = world.load_map("map mp evendim", {"seed": 42, "animations": false, "fog": false})
	check(loaded.ok, "load retail map")
	if not loaded.ok:
		quit(1)
		return
	world.set_auto_advance(false)
	world.set_shroud_drawn(false)
	if world.has_node("StaticObjects"):
		world.get_node("StaticObjects").hide()
	var units: Array[int] = []
	for i in 3:
		units.append(world.create_object("ElvenLorienWarrior", 0, 800.0, 800.0 + i * 18, 0.0))
	world.advance(0.2)
	var info: Dictionary = world.get_object(units[1])
	var center := Vector3(float(info.x), float(info.z) + 8.0, -float(info.y))
	var camera := Camera3D.new()
	vp.add_child(camera)
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 70.0
	camera.far = 1000.0
	camera.position = center + Vector3(65, 32, 60)
	camera.look_at(center)
	camera.current = true
	await frames(12)
	var before: Image = vp.get_texture().get_image()
	check(world.debug_grant_upgrade(0, "Upgrade_ElvenHeavyArmor"), "grant heavy armor")
	world.advance(0.2)
	await frames(8)
	info = world.get_object(units[1])
	print("UNIT UPGRADE VISUALS armored model: ", info.get("shown_models", []))
	check(str(info.get("shown_models", [])).to_lower().contains("eulorwarha.tga"), "live model cache changes to the armor texture variant")
	var armor: Image = vp.get_texture().get_image()
	check(before.get_data() != armor.get_data(), "armor changes rendered pixels")
	check(world.debug_grant_upgrade(0, "Upgrade_ElvenForgedBlades"), "grant forged blades")
	world.advance(0.2)
	await frames(8)
	info = world.get_object(units[1])
	var hidden: PackedStringArray = info.get("hidden_subobjects", PackedStringArray())
	check(not str(hidden).to_lower().contains("forged_blade"), "forged-blade mesh is no longer hidden")
	var upgraded: Image = vp.get_texture().get_image()
	check(armor.get_data() != upgraded.get_data(), "forged blades change rendered pixels")
	check(str(info.get("shown_models", [])).to_lower().contains("eulorwarha.tga"), "weapon upgrade retains the armor texture")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--captures="):
			var folder := arg.trim_prefix("--captures=")
			DirAccess.make_dir_recursive_absolute(folder)
			check(before.save_png(folder.path_join("unit-upgrades-before.png")) == OK, "save before capture")
			check(armor.save_png(folder.path_join("unit-heavy-armor.png")) == OK, "save armor capture")
			check(upgraded.save_png(folder.path_join("unit-upgrades-after.png")) == OK, "save armor and blades capture")
	vp.queue_free()
	await process_frame
	print("UNIT UPGRADE VISUALS ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
