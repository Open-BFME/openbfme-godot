extends SceneTree

const Main := preload("res://scenes/main.tscn")
const SIZES := [Vector2i(1280, 720), Vector2i(2560, 1440)]
const SCALES := [1.0, 1.25, 1.5]
var app: Control
var failures := 0
var baseline := OS.get_environment("LAUNCHER_UX_BASELINE") == "1"
var scenario := OS.get_environment("LAUNCHER_UX_SCENARIO")
var shots := OS.get_environment("LAUNCHER_UX_SCREENSHOTS")


func _initialize() -> void:
	_run.call_deferred()


func check(ok: bool, what: String) -> void:
	if not ok:
		print("FIRST_RUN finding ", scenario, ": ", what)
		if not baseline:
			failures += 1


func settle() -> void:
	for i in 600:
		if app._thread == null and not app._social.busy:
			break
		await create_timer(0.01).timeout
	check(app._thread == null and not app._social.busy, "fixture operation completed")
	for i in 5:
		await process_frame


func texts(node: Node) -> String:
	var out := ""
	if node is Window and not node.visible:
		return out
	if node is Control and not node.is_visible_in_tree():
		return out
	if node is Label or node is BaseButton:
		out += node.text + "\n"
	for child in node.get_children():
		out += texts(child)
	return out


func controls(node: Node, include_disabled := false) -> Array[Control]:
	var out: Array[Control] = []
	if node is Window and not node.visible:
		return out
	if node is Control:
		if not node.is_visible_in_tree():
			return out
		if node is BaseButton and node.disabled and not include_disabled:
			return out
		if node.focus_mode == Control.FOCUS_ALL:
			out.append(node)
	for child in node.get_children():
		out.append_array(controls(child, include_disabled))
	return out


func press(action: String) -> void:
	var event := InputEventAction.new()
	event.action = action
	event.pressed = true
	Input.parse_input_event(event)
	await process_frame
	event = InputEventAction.new()
	event.action = action
	event.pressed = false
	Input.parse_input_event(event)
	await process_frame


func focus_walk(scope: Control, modal := false) -> void:
	var expected := controls(scope)
	var navigable := controls(scope, true)
	check(not expected.is_empty(), "nonempty focus inventory")
	if expected.is_empty():
		return
	for c in expected:
		if c is Button and c.text == "" and not "word" in c:
			check(c.tooltip_text != "", "icon-only button has a tooltip: " + str(c.get_path()))
	# No grab_focus per target: traverse with actual input, so unreachable controls fail.
	expected[0].grab_focus()
	await settle()
	var seen := {}
	var trace := PackedStringArray()
	for i in navigable.size() + 2:
		var focused := root.gui_get_focus_owner()
		if focused != null:
			seen[focused] = true
			trace.append(str(focused.get_path()))
			check(focused.get_global_rect().intersects(root.get_visible_rect()), "focused control is scrolled into view: " + str(focused.get_path()))
			if modal:
				check(scope.is_ancestor_of(focused), "dialog keeps keyboard focus")
		await press("ui_focus_next")
	for c in expected:
		check(seen.has(c), "Tab reaches " + str(c.get_path()))
		if not seen.has(c):
			print("FIRST_RUN trace ", trace)
			print("FIRST_RUN expected ", expected.map(func(b): return str(b.get_path())))
	# Reverse traversal must cover the same controls too.
	seen.clear()
	for i in navigable.size() + 2:
		var focused := root.gui_get_focus_owner()
		if focused != null:
			seen[focused] = true
		await press("ui_focus_prev")
	for c in expected:
		check(seen.has(c), "Shift+Tab reaches " + str(c.get_path()))
	# Godot's directional-focus graph is what keyboard arrows and the controller's
	# D-pad use outside text editing/popups. Inspect every reachable edge, not proximity.
	seen = {expected[0]: true}
	var pending: Array[Control] = [expected[0]]
	while not pending.is_empty():
		var origin: Control = pending.pop_front()
		for direction in [SIDE_LEFT, SIDE_RIGHT, SIDE_TOP, SIDE_BOTTOM]:
			var next := origin.find_valid_focus_neighbor(direction)
			if next != null and navigable.has(next) and not seen.has(next):
				seen[next] = true
				pending.append(next)
	for c in expected:
		check(seen.has(c), "directional focus graph reaches " + str(c.get_path()))
	if modal:
		seen.clear()
		for i in expected.size() + 2:
			var focused := root.gui_get_focus_owner()
			if focused != null:
				seen[focused] = true
			await press("ui_down")
		for c in expected:
			check(seen.has(c), "gamepad direction reaches dialog " + c.text)
	print("FIRST_RUN focus ", app._screen, " modal=", modal, " controls=", expected.size())


func inspect_text(node: Node) -> void:
	if node is Control and not node.is_visible_in_tree():
		return
	if node is Label and node.text != "":
		if node.autowrap_mode == TextServer.AUTOWRAP_OFF and not node.clip_text:
			var width: float = node.get_theme_font("font").get_string_size(node.text, HORIZONTAL_ALIGNMENT_LEFT, -1,
				node.get_theme_font_size("font_size")).x
			check(width <= node.size.x + 1.0 or node.tooltip_text == node.text, "uncut text: " + node.text.left(65))
		check(node.get_minimum_size().y <= node.size.y + 1.0, "wrapped text height: " + node.text.left(65))
	for child in node.get_children():
		inspect_text(child)


func _run() -> void:
	root.content_scale_mode = Window.CONTENT_SCALE_MODE_CANVAS_ITEMS
	root.content_scale_aspect = Window.CONTENT_SCALE_ASPECT_EXPAND
	app = Main.instantiate()
	root.add_child(app)
	await settle()
	check(app._demo == "", "not a screenshot demo: real first startup")
	match scenario:
		"no-install", "found", "unsupported-version", "wrong-language", "no-network", "update-down":
			pass
		"disk-full":
			# Fault injection at the UI completion boundary; never fill the developer's disk.
			app._after_install("Not installed: writing VERSION failed (is the disk full?)", "writing VERSION failed (is the disk full?)")
		"download-http", "download-unknown":
			var detail := ("HTTP 503 " if scenario == "download-http" else "Unrecognized transfer failure ") + \
				"https://private-host.example/files /opt/private-install/version C:\\private-install\\version private-server.internal"
			if scenario == "download-http":
				app._after_aio({"ok": false, "kind": app.AioInstall.NETWORK, "error": detail})
			else:
				app._after_install("Not installed: " + detail, detail)
		"corrupt-download":
			check(not app._latest.is_empty(), "signed fixture release is available")
			app._update.pressed.emit()
			await settle()
			check(app.updater.playable().is_empty(), "corrupt download installs nothing")
		"lobby-down", "signin-unavailable":
			app._show_screen("multiplayer")
			app._multiplayer._name.text = "Fixture player"
			app._multiplayer._connect.pressed.emit()
			await settle()
			check(not app._social.is_signed_in(), "failed sign-in stays signed out")
		"channels":
			app._show_screen("settings")
			app._settings._channel.select(1)
			app._settings._channel.item_selected.emit(1)
			await settle()
			check(app.updater.channel() == "preview" and app._latest.get("tag") == "v0.3.0-preview.1", "Nightly requests the nightly fixture")
			app._settings._channel.select(0)
			app._settings._channel.item_selected.emit(0)
			await settle()
			check(app.updater.channel() == "stable" and app._latest.get("tag") == "v0.2.0", "Stable requests the stable fixture")
		_:
			check(false, "unknown scenario")
	var key_text := {
		"no-install": ["Not found", "Download the games"],
		"found": ["Folders found", "checked when the game starts"],
		"unsupported-version": ["Folders found", "checked when the game starts"],
		"wrong-language": ["Folders found", "checked when the game starts"],
		"no-network": ["Could not check for updates", "Check your internet connection"],
		"update-down": ["Could not check for updates", "retry later"],
		"disk-full": ["Not enough disk space", "Free up space", "Copy details"],
		"download-http": ["retry the download", "Copy details"],
		"download-unknown": ["retry the download", "Copy details"],
		"corrupt-download": ["could not be verified", "Check for updates", "Copy details"],
		"lobby-down": ["lobby could not be reached", "try Sign in again", "offline"],
		"signin-unavailable": ["not available on this server", "offline"],
		"channels": ["Stable", "Nightly gets a new test build"],
	}
	for pixels in SIZES:
		for scale in SCALES:
			# Check production DPI window sizing independently of the explicit canvas zoom.
			var fitted: Vector2i = app.fit_size("Windows", 1.0, int(96 * scale), Vector2(pixels))
			if scale == 1.0:
				check(fitted == Vector2i.ZERO, "100% retains project window size")
			else:
				check(fitted.x <= pixels.x and fitted.y <= pixels.y, "DPI initial window fits the display")
			root.size = pixels
			root.content_scale_size = Vector2i(1280, 720)
			root.content_scale_mode = Window.CONTENT_SCALE_MODE_CANVAS_ITEMS
			root.content_scale_aspect = Window.CONTENT_SCALE_ASPECT_EXPAND
			root.content_scale_factor = scale
			await settle()
			print("FIRST_RUN canvas ", root.get_final_transform().get_scale(), " logical ", root.get_visible_rect().size)
			check(is_equal_approx(root.get_final_transform().get_scale().x, pixels.x / 1280.0 * scale), "requested UI scale is rendered")
			var shown := texts(app)
			if scenario in ["download-http", "download-unknown"]:
				for secret in ["private-host.example", "/opt/private-install/version", "C:\\private-install\\version", "private-server.internal"]:
					check(not shown.contains(secret), "no diagnostic path or host in visible text")
					check(app._message._details.contains(secret), "Copy details retains path and host diagnostics")
				check(not app._status.text.contains("check for updates"), "download failure is not update-check guidance")
			for needle in key_text[scenario]:
				check(shown.contains(needle), "key text: " + needle)
			if scenario in ["disk-full", "corrupt-download"]:
				check(not app._message._text.text.contains("SHA-256") and not app._message._text.text.contains("VERSION"), "technical detail stays behind Copy details")
				check(app._message._details.contains("VERSION" if scenario == "disk-full" else "SHA-256"), "copied details retain diagnostic")
			inspect_text(app)
			if shots == "":
				await focus_walk(app._message if app._message.visible else app, app._message.visible)
			if shots != "" and DisplayServer.get_name() != "headless":
				RenderingServer.force_draw(false)
				var path := shots.path_join("%s-%dx%d-%d.png" % [scenario, pixels.x, pixels.y, int(scale * 100)])
				check(root.get_texture().get_image().save_png(path) == OK, "saved screenshot")
			print("FIRST_RUN case %s@%dx%d@%d" % [scenario, pixels.x, pixels.y, int(scale * 100)])
	# Visit every main screen and settings page, not only the screenshot's controls.
	app._message.visible = false
	if shots != "":
		_finish()
		return
	for pixels in SIZES:
		for scale in SCALES:
			root.size = pixels
			root.content_scale_factor = scale
			for screen in app._screens:
				app._show_screen(screen)
				await settle()
				if screen == "settings":
					for tab in app._settings._pages:
						app._settings.show_tab(tab)
						await settle()
						await focus_walk(app)
				else:
					await focus_walk(app)
					if screen == "multiplayer":
						app._multiplayer._recover_toggle.button_pressed = true
						await settle()
						check(app._multiplayer._recover.visible, "recovery field opens")
						await focus_walk(app)
						app._multiplayer._recover_toggle.button_pressed = false
			if scenario == "no-install":
				var previous: Control = app._check
				previous.grab_focus()
				var confirmed := [false]
				app._message.ask("Install an older version?", "An older test build.", "Install", func() -> void: confirmed[0] = true)
				await settle()
				check(root.gui_get_focus_owner() == app._message._cancel, "question focuses safe Cancel action")
				await focus_walk(app._message, true)
				await press("ui_cancel")
				check(not app._message.visible and not confirmed[0], "Escape cancels without installing")
				check(root.gui_get_focus_owner() == previous, "closing dialog restores focus")
	_finish()


func _finish() -> void:
	var verdict := "BASELINE" if baseline else "OK" if failures == 0 else "FAILED"
	print("FIRST_RUN %s %s: 6 cases" % [verdict, scenario])
	app.queue_free()
	await process_frame
	quit(0 if failures == 0 else 1)
