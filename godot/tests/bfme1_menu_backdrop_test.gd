extends SceneTree

var _failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _check(ok: bool, message: String) -> void:
	print("BFME1 MENU ", "ok " if ok else "FAIL ", message)
	if not ok:
		_failures += 1


func _movie(shell: Node) -> Dictionary:
	for movie in shell.get_movies():
		if movie.path == "backdrop:FrontEnd":
			return movie
	return {}


func _packed_image(fs: RefCounted, stem: String) -> Image:
	var colour := Image.new()
	if colour.load_jpg_from_buffer(fs.read_file("art/textures/" + stem + ".jpg")) != OK:
		return null
	colour.convert(Image.FORMAT_RGBA8)
	var mask_path := "art/textures/" + stem + ".png"
	if fs.file_exists(mask_path):
		var mask_bytes: PackedByteArray = fs.read_file(mask_path)
		var mask := Image.new()
		if mask.load_png_from_buffer(mask_bytes) != OK:
			return null
		mask.convert(Image.FORMAT_RGBA8)
		mask.resize(colour.get_width(), colour.get_height(), Image.INTERPOLATE_NEAREST)
		for vertical in colour.get_height():
			for horizontal in colour.get_width():
				var pixel := colour.get_pixel(horizontal, vertical)
				var alpha := mask.get_pixel(horizontal, vertical)
				colour.set_pixel(horizontal, vertical, Color(pixel.r * alpha.r, pixel.g * alpha.g, pixel.b * alpha.b, alpha.a))
	return colour


func _run() -> void:
	var install := OS.get_environment("BFME1_INSTALL")
	if install.is_empty() or DisplayServer.get_name() == "headless":
		print("BFME1 MENU SKIP: requires BFME1_INSTALL and GPU rendering")
		quit(77)
		return
	create_timer(300.0, true, false, true).timeout.connect(func(): quit(1))
	RenderingServer.set_default_clear_color(Color.BLACK)
	var folder := OS.get_environment("BFME1_MENU_TEST_DIR")
	if folder.is_empty():
		folder = OS.get_temp_dir().path_join("openbfme-bfme1-menu-%d" % OS.get_process_id())
	_check(DirAccess.make_dir_recursive_absolute(folder) == OK, "screenshot directory created")
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_bfme1_path(install)
	_check(mounted.ok, "BFME1 mount: " + str(mounted.get("errors", [])))
	if not mounted.ok:
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
	viewport.size = Vector2i(1280, 720)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	viewport.add_child(shell)
	var boot: Dictionary = shell.boot_shell(fs, world, {"options_file": folder.path_join("Options.ini"), "skirmish_ini": folder.path_join("Skirmish.ini")})
	_check(boot.ok, "shell boot: " + str(boot.get("errors", [])))
	if not boot.ok:
		quit(1)
		return
	_check(shell.shell_push("MainMenu.apt"), "main menu opened")
	await create_timer(2.0).timeout
	shell.auto_process = false
	var movie := _movie(shell)
	_check(not movie.is_empty(), "FrontEnd movie starts without an injected background command")
	if movie.is_empty():
		print(shell.get_shell_report())
		quit(1)
		return
	_check(movie.error.is_empty() and movie.frame > 0 and movie.loop, "FrontEnd advances and loops with no decoder error: " + str(movie))
	var backdrop: Dictionary = shell.shell_mapped_image("ShellMapLowLOD")
	var logo: Dictionary = shell.shell_mapped_image("LogoWithShadow")
	_check(backdrop.ok and logo.ok, "backdrop and logo mapped textures load")
	if not backdrop.ok or not logo.ok:
		quit(1)
		return
	for asset in [["installload", backdrop], ["logowithshadow", logo]]:
		var expected := _packed_image(fs, asset[0])
		_check(expected != null and asset[1].texture.get_image().get_data() == expected.get_data(), asset[0] + " matches independently decoded JPEG/PNG pixels")
	var movie_info: Dictionary = world.get_movie("FrontEnd")
	var stream = ClassDB.instantiate("VP6MovieStream")
	var opened: Dictionary = stream.open(movie_info.full_path)
	_check(opened.ok and opened.alpha, "retail FrontEnd movie has an alpha stream")
	_check(stream.advance_to(movie.frame), "independent decoder reaches the displayed frame")
	var movie_texture: Texture2D = stream.get_texture()
	var logo_clip: Dictionary = shell.instance_info(shell.shell_top_level(), "Image")
	var logo_bounds: Rect2 = logo_clip.local_bounds
	var logo_transform: Transform2D = logo_clip.stage_transform
	var reference := SubViewport.new()
	reference.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(reference)
	var reference_item := RenderingServer.canvas_item_create()
	RenderingServer.canvas_item_set_parent(reference_item, reference.find_world_2d().canvas)
	RenderingServer.canvas_item_set_default_texture_filter(reference_item, RenderingServer.CANVAS_ITEM_TEXTURE_FILTER_LINEAR)
	RenderingServer.canvas_item_set_default_texture_repeat(reference_item, RenderingServer.CANVAS_ITEM_TEXTURE_REPEAT_DISABLED)
	var initial_buttons: Array = shell.list_buttons(shell.shell_top_level())
	var texture_size: Vector2 = backdrop.texture.get_size()
	var uv_start: Vector2 = backdrop.region.position / texture_size
	var uv_end: Vector2 = backdrop.region.end / texture_size
	var uvs := PackedVector2Array([uv_start, Vector2(uv_end.x, uv_start.y), uv_end, Vector2(uv_start.x, uv_end.y)])
	for size in [Vector2i(1024, 768), Vector2i(1280, 720), Vector2i(1920, 1080), Vector2i(3440, 1440), Vector2i(1280, 720)]:
		viewport.size = size
		reference.size = size
		var scale := minf(size.x / backdrop.size.x, size.y / backdrop.size.y)
		var artwork := Rect2((Vector2(size) - backdrop.size * scale) * 0.5, backdrop.size * scale)
		var logo_start := artwork.position + (logo_transform * logo_bounds.position) * scale
		var logo_end := artwork.position + (logo_transform * logo_bounds.end) * scale
		RenderingServer.canvas_item_clear(reference_item)
		var corners := PackedVector2Array([artwork.position, Vector2(artwork.end.x, artwork.position.y), artwork.end, Vector2(artwork.position.x, artwork.end.y)])
		RenderingServer.canvas_item_add_polygon(reference_item, corners, PackedColorArray([Color.WHITE]), uvs, backdrop.texture.get_rid())
		RenderingServer.canvas_item_add_texture_rect(reference_item, artwork, movie_texture.get_rid())
		RenderingServer.canvas_item_add_texture_rect_region(reference_item, Rect2(logo_start, logo_end - logo_start), logo.texture.get_rid(), logo.region, Color.WHITE)
		shell.render(false)
		await process_frame
		RenderingServer.force_draw(false)
		var actual: Image = viewport.get_texture().get_image()
		var expected: Image = reference.get_texture().get_image()
		var region := Rect2i(0, 0, size.x, size.y / 2)
		var label := "%dx%d" % [size.x, size.y]
		_check(actual.get_region(region).get_data() == expected.get_region(region).get_data(), label + " backdrop, alpha movie and top-right logo match independent composition")
		_check(shell.list_buttons(shell.shell_top_level()) == initial_buttons, label + " menu buttons unchanged")
		_check(actual.save_png(folder.path_join("menu-" + label + ".png")) == OK, label + " screenshot saved")
		_check(expected.save_png(folder.path_join("reference-" + label + ".png")) == OK, label + " reference saved")
	_check(shell.get_shell_report().errors.is_empty(), "no initial mapped image errors")
	for command in ["Skirmish", "Options", "Skirmish", "Options"]:
		var before := _movie(shell)
		shell.auto_process = true
		_check(shell.shell_fscommand("AptMainMenu::" + command, "false"), command + " opens through its main menu command")
		await create_timer(2.0).timeout
		_check(shell.get_backdrop_state().background_mode == 1, command + " shows the authored submenu background")
		shell.shell_pop()
		await create_timer(2.0).timeout
		shell.auto_process = false
		var returned := _movie(shell)
		_check(not returned.is_empty() and returned.error.is_empty() and returned.frames_decoded > before.frames_decoded, command + " return keeps the movie advancing")
		_check(shell.get_backdrop_state().background_mode == 0, command + " return hides the covering submenu background")
		_check(stream.advance_to(returned.frame), command + " independent decoder reaches the returned menu frame")
		await process_frame
		RenderingServer.force_draw(false)
		var actual: Image = viewport.get_texture().get_image()
		var expected: Image = reference.get_texture().get_image()
		var region := Rect2i(0, 0, viewport.size.x, viewport.size.y / 2)
		_check(actual.get_region(region).get_data() == expected.get_region(region).get_data(), command + " return restores the independently composed movie and logo")
		_check(actual.save_png(folder.path_join("return-" + command.to_lower() + ".png")) == OK, command + " return screenshot saved")
	shell.auto_process = true
	Engine.time_scale = 128.0
	await create_timer(float(opened.duration_ms) / 1000.0 + 2.0).timeout
	Engine.time_scale = 1.0
	shell.auto_process = false
	var looped := _movie(shell)
	_check(not looped.is_empty() and looped.error.is_empty() and looped.frames_decoded > looped.frame + 1, "FrontEnd restarts after its end without reopening the texture: " + str(looped))
	shell.auto_process = true
	shell.shell_show_shell_map(false)
	await create_timer(0.1).timeout
	_check(_movie(shell).is_empty() and shell.get_backdrop_state().backdrop_image.is_empty(), "entering gameplay removes the movie and backdrop")
	shell.shell_show_shell_map(true)
	await create_timer(0.1).timeout
	_check(not _movie(shell).is_empty() and shell.get_backdrop_state().backdrop_image == "ShellMapLowLOD", "returning to the menu restores the movie and backdrop")
	_check(shell.get_report().movie_errors.is_empty(), "no movie playback errors")
	var backdrop_errors: Array[String] = []
	for error in shell.get_shell_report().errors:
		var lower: String = error.to_lower()
		if lower.contains("shellmaplowlod") or lower.contains("installload") or lower.contains("logowithshadow"):
			backdrop_errors.append(error)
	_check(backdrop_errors.is_empty(), "no backdrop or logo errors after navigation: " + str(backdrop_errors))
	print("BFME1 MENU submenu diagnostics: ", shell.get_shell_report().errors)
	shell.free()
	viewport.free()
	RenderingServer.free_rid(reference_item)
	reference.free()
	world.free()
	print("BFME1 MENU ", "PASS" if _failures == 0 else "FAIL", ": ", _failures, " failures; screenshots ", folder)
	quit(0 if _failures == 0 else 1)
