extends SceneTree

var _failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _check(ok: bool, message: String) -> void:
	print("MENU BACKDROP ", "ok " if ok else "FAIL ", message)
	if not ok:
		_failures += 1


func _menu_state(shell: Node) -> Dictionary:
	var state := {"stack": shell.shell_stack()}
	for button in shell.list_buttons(shell.shell_top_level()):
		state[String(button.path)] = button.hittable
	return state


func _run() -> void:
	if DisplayServer.get_name() == "headless" or OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("MENU BACKDROP SKIP: requires a GPU and ROTWK_INSTALL / BFME2_INSTALL")
		quit(77)
		return
	create_timer(180.0).timeout.connect(func(): quit(1))
	RenderingServer.set_default_clear_color(Color.BLACK)
	var folder := OS.get_environment("BACKDROP_TEST_DIR")
	if folder.is_empty():
		folder = OS.get_temp_dir().path_join("openbfme-menu-backdrop-%d" % OS.get_process_id())
	if DirAccess.make_dir_recursive_absolute(folder) != OK:
		_check(false, "cannot create screenshot directory")
		quit(1)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	_check(mount.ok, "retail mount: " + str(mount.get("errors", [])))
	if not mount.ok:
		quit(1)
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	_check(setup.ok, "world setup: " + str(setup.get("errors", [])))
	if not setup.ok:
		quit(1)
		return
	var viewport := SubViewport.new()
	viewport.size = Vector2i(1024, 768)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	shell.auto_process = false
	viewport.add_child(shell)
	var config := {"options_file": folder.path_join("Options.ini"), "skirmish_ini": folder.path_join("Skirmish.ini")}
	var style := OS.get_environment("BACKDROP_TEST_STYLE")
	if style.is_empty():
		style = "rotwk"
	_check(style in ["rotwk", "bfme2"], "supported presentation style: " + style)
	if style not in ["rotwk", "bfme2"]:
		quit(1)
		return
	if style == "bfme2":
		config.presentation_fs = fs.create_bfme2_shell()
		_check(config.presentation_fs != null, "BFME2 presentation source is available")
		if config.presentation_fs == null:
			quit(1)
			return
	var boot: Dictionary = shell.boot_shell(fs, world, config)
	_check(boot.ok, "shell boot: " + str(boot.get("errors", [])))
	if not boot.ok:
		quit(1)
		return
	var expected_style := "bfme2-106" if style == "bfme2" else "rotwk-201"
	_check(shell.get_shell_report().get("presentation_style", "") == expected_style, expected_style + " presentation provenance is reported")
	_check(shell.shell_push("MainMenu.apt"), "main menu opened")
	for frame in 20:
		shell.tick(0.033)
		shell.render(true)
	_check(shell.shell_invoke(shell.shell_top_level(), "ShowMainMenu", PackedStringArray()).ok, "main menu shown")
	for frame in 120:
		shell.tick(0.033)
		shell.render(true)
	var backdrop: Dictionary = shell.get_backdrop_state()
	_check(backdrop.backdrop_image == "ShellMapLowLOD", "static shell backdrop is drawn")
	var mapped: Dictionary = shell.shell_mapped_image(backdrop.backdrop_image)
	_check(mapped.ok, "mapped backdrop texture is available")
	if not mapped.ok:
		quit(1)
		return
	var logo: Dictionary = shell.shell_mapped_image("LogoWithShadow")
	_check(logo.ok, "main menu logo texture is available")
	if not logo.ok:
		quit(1)
		return
	var logo_clip: Dictionary = shell.instance_info(shell.shell_top_level(), "Image")
	var has_logo_geometry: bool = logo_clip.get("found", false) and logo_clip.has("local_bounds") and logo_clip.has("stage_transform")
	_check(has_logo_geometry, "authored logo clip bounds and transform are available")
	if not has_logo_geometry:
		quit(1)
		return
	var logo_bounds: Rect2 = logo_clip.local_bounds
	var logo_transform: Transform2D = logo_clip.stage_transform
	var logo_corners := PackedVector2Array([logo_bounds.position, Vector2(logo_bounds.end.x, logo_bounds.position.y),
		logo_bounds.end, Vector2(logo_bounds.position.x, logo_bounds.end.y)])
	var reference := SubViewport.new()
	reference.size = viewport.size
	reference.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(reference)
	var picture := Node2D.new()
	reference.add_child(picture)
	var reference_item := RenderingServer.canvas_item_create()
	RenderingServer.canvas_item_set_parent(reference_item, picture.get_canvas_item())
	RenderingServer.canvas_item_set_default_texture_filter(reference_item, RenderingServer.CANVAS_ITEM_TEXTURE_FILTER_LINEAR)
	RenderingServer.canvas_item_set_default_texture_repeat(reference_item, RenderingServer.CANVAS_ITEM_TEXTURE_REPEAT_DISABLED)
	var artwork_size: Vector2 = mapped.size
	var texture_size: Vector2 = mapped.texture.get_size()
	var region: Rect2 = mapped.region
	var uv_start := region.position / texture_size
	var uv_end := region.end / texture_size
	var uvs := PackedVector2Array([uv_start, Vector2(uv_end.x, uv_start.y), uv_end, Vector2(uv_start.x, uv_end.y)])
	var initial_menu := _menu_state(shell)
	var sizes := [Vector2i(1024, 768), Vector2i(1280, 720), Vector2i(1920, 1080), Vector2i(2560, 1440),
		Vector2i(3440, 1440), Vector2i(1280, 1024), Vector2i(768, 1024), Vector2i(1280, 720)]
	for index in sizes.size():
		var size: Vector2i = sizes[index]
		viewport.size = size
		reference.size = size
		var uniform_scale := minf(size.x / artwork_size.x, size.y / artwork_size.y)
		var drawing_size := artwork_size * uniform_scale
		var drawing_start := (Vector2(size) - drawing_size) * 0.5
		var drawing_end := drawing_start + drawing_size
		var corners := PackedVector2Array([drawing_start, Vector2(drawing_end.x, drawing_start.y), drawing_end, Vector2(drawing_start.x, drawing_end.y)])
		RenderingServer.canvas_item_clear(reference_item)
		RenderingServer.canvas_item_add_polygon(reference_item, corners, PackedColorArray([Color.WHITE]), uvs, mapped.texture.get_rid())
		var logo_start := drawing_start + (logo_transform * logo_corners[0]) * uniform_scale
		var logo_rect := Rect2(logo_start, Vector2.ZERO)
		for corner in logo_corners:
			logo_rect = logo_rect.expand(drawing_start + (logo_transform * corner) * uniform_scale)
		RenderingServer.canvas_item_add_texture_rect_region(reference_item, logo_rect, logo.texture.get_rid(), logo.region, Color.WHITE)
		shell.render(false)
		await process_frame
		await process_frame
		await RenderingServer.frame_post_draw
		var actual: Image = viewport.get_texture().get_image()
		var expected: Image = reference.get_texture().get_image()
		var stem := "%02d-%dx%d" % [index, size.x, size.y]
		_check(actual.save_png(folder.path_join("backdrop-" + stem + ".png")) == OK, stem + " screenshot saved")
		_check(expected.save_png(folder.path_join("reference-" + stem + ".png")) == OK, stem + " reference saved")
		var background_region := Rect2i(0, 0, size.x / 2, size.y / 2)
		_check(actual.get_region(background_region).get_data() == expected.get_region(background_region).get_data(), stem + " background pixels match independent aspect-fit reference")
		_check(_menu_state(shell) == initial_menu, stem + " menu stack and button hittability unchanged")
		var artwork_region := Rect2i(0, 0, size.x, size.y / 2)
		_check(Rect2(artwork_region).encloses(logo_rect.intersection(Rect2(Vector2.ZERO, Vector2(size)))), stem + " comparison region includes the complete visible logo")
		_check(actual.get_region(artwork_region).get_data() == expected.get_region(artwork_region).get_data(), stem + " background and independently placed logo pixels match reference")
	shell.free()
	viewport.free()
	RenderingServer.free_rid(reference_item)
	reference.free()
	world.free()
	await process_frame
	print("MENU BACKDROP ", "PASS" if _failures == 0 else "FAIL", ": ", _failures, " failures; screenshots ", folder)
	quit(0 if _failures == 0 else 1)
