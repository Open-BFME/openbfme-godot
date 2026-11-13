## Godot integration regression for #309: both RotWK and BFME2 strategic HUDs.
## Run with private ROTWK_INSTALL / BFME2_INSTALL and a GPU (no --headless):
##   godot --path godot --script res://tests/wotr_hud_viewport_test.gd
## Exit 0 pass, 1 fail, 77 prerequisites unavailable. No captures are written.
##
## A 4:3 render supplies the artwork reference. At the SAME height, widening
## the viewport must retain its pixel footprint at the left/right anchors.
## This checks the actual canvas/GPU output, without reproducing the correction
## matrix. Input uses fixed window pixels, never find_button/globalMatrix.
extends SceneTree

var failures := 0

func _initialize() -> void:
	run.call_deferred()

func check(ok: bool, message: String) -> bool:
	print("WOTR HUD ", "ok " if ok else "FAIL ", message)
	if not ok:
		failures += 1
	return ok

func tick(shell: Node, count := 120) -> void:
	for i in count:
		shell.tick(1.0 / 30.0)

func capture(shell: Node, viewport: SubViewport) -> Image:
	shell.render(true)
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var image := viewport.get_texture().get_image()
	image.convert(Image.FORMAT_RGBA8)
	return image

func same_artwork(reference: Image, actual: Image, area: Rect2i, offset: Vector2i, message: String) -> void:
	var expected := reference.get_region(area).get_data()
	var observed := actual.get_region(Rect2i(area.position + offset, area.size)).get_data()
	if not check(expected.size() == observed.size(), message + " readback size"):
		return
	var changed := 0
	var painted := 0
	for i in range(0, expected.size(), 4):
		if expected[i + 3] != 0:
			painted += 1
		for channel in 4:
			if absi(int(expected[i + channel]) - int(observed[i + channel])) > 2:
				changed += 1
				break
	# tolerance: a nonempty retail artwork reference must exercise more than a blank canvas.
	check(painted > 1000, message + " reference contains artwork")
	# tolerance: fractional-height scaling changes glyph/triangle edge rasterization (720px END PHASE: 216/9600 pixels on Metal);
	# at least 97% of pixels must still agree within 2/255. Widened artwork changes the interior as well as those thin edges.
	check(changed <= area.get_area() * 3 / 100, message + " retains 4:3 pixels (%d changed)" % changed)

func mouse(shell: Node, position: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = position
	shell.get_viewport().push_input(motion, true)
	for down in [true, false]:
		var button := InputEventMouseButton.new()
		button.position = position
		button.button_index = MOUSE_BUTTON_LEFT
		button.pressed = down
		shell.get_viewport().push_input(button, true)

func click_end_phase(shell: Node, position: Vector2, message: String) -> void:
	shell.strategic_hud({}) # drain requests before each independent click
	mouse(shell, position)
	tick(shell)
	var requests: Array = shell.strategic_hud({}).requests
	check(requests == ["end_turn"], message + " dispatches exactly one end_turn: " + str(requests))
	var motion := InputEventMouseMotion.new()
	motion.position = Vector2(512, 384)
	shell.get_viewport().push_input(motion, true)
	tick(shell) # settle the authored click/hover animation before comparing artwork

func click_outside(shell: Node, position: Vector2, message: String) -> void:
	shell.strategic_hud({})
	mouse(shell, position)
	tick(shell)
	check(shell.strategic_hud({}).requests == [], message + " outside END PHASE dispatches nothing")

func exercise(fs: RefCounted, world: Node, edition: String, folder: String) -> void:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(1024, 768)
	viewport.transparent_bg = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	shell.auto_process = false
	viewport.add_child(shell)
	var config := {"options_file": folder.path_join(edition + "-Options.ini"), "skirmish_ini": folder.path_join(edition + "-Skirmish.ini")}
	if edition == "BFME2":
		config.presentation_fs = fs.create_bfme2_shell()
		if not check(config.presentation_fs != null, "BFME2 presentation source"):
			viewport.free()
			return
	if not check(shell.boot_shell(fs, world, config).ok, edition + " shell boot"):
		viewport.free()
		return
	check(shell.get_shell_report().presentation_style == ("bfme2-106" if edition == "BFME2" else "rotwk-201"), edition + " presentation provenance")
	var opened: Dictionary = shell.strategic_hud({"kind": "open"})
	if not check(opened.ok and opened.open, edition + " strategic HUD opens"):
		viewport.free()
		return
	shell.shell_show_shell_map(false) # the game removes the front-end picture behind its strategic map
	shell.shell_hide_background()
	tick(shell)
	var state: Dictionary = shell.strategic_hud({})
	check(state.initialized and state.parts.size() == 9, edition + " retail HUD and nine parts initialized")
	var reference := await capture(shell, viewport)
	# Stable corners: lower-left Palantir and upper-right END PHASE frame.
	# Measured from the 1024x768 artwork: END PHASE spans about x857..1023,
	# y0..57. The fixed point (900,28) is inside; (824,28) is outside.
	var left := Rect2i(0, 568, 210, 200)
	var right := Rect2i(856, 0, 168, 64)
	for width in [2048, 2560, 1536, 1024, 2048]:
		viewport.size = Vector2i(width, 768)
		# No tick here: render must update the correction itself after resize.
		var image := await capture(shell, viewport)
		same_artwork(reference, image, left, Vector2i.ZERO, edition + " %d render left" % width)
		same_artwork(reference, image, right, Vector2i(width - 1024, 0), edition + " %d render right" % width)
	# Fit places a 1024x768 stage in the middle of this 2048x768 viewport.
	# Its artwork must match 4:3 exactly, with no leftover stretch correction.
	shell.fit = true
	var fitted := await capture(shell, viewport)
	same_artwork(reference, fitted, left, Vector2i(512, 0), edition + " Fit left")
	same_artwork(reference, fitted, right, Vector2i(512, 0), edition + " Fit right")
	# Keep the movie frozen throughout the image comparisons above: authored
	# animation must not be mistaken for a viewport geometry change.
	click_end_phase(shell, Vector2(1412, 28), edition + " Fit window click")
	shell.fit = false
	await capture(shell, viewport)
	for width in [1536, 2560, 2048]:
		viewport.size = Vector2i(width, 768)
		click_end_phase(shell, Vector2(width - 124, 28), edition + " %d window click" % width)
		# The old stretched hit area extends here; the corrected artwork does not.
		click_outside(shell, Vector2(width - 200, 28), edition + " %d window miss" % width)
	# Input arrives before either tick or render after this resize. A point near
	# the left of the drawn button distinguishes the corrected hit mesh from a
	# stale 2048-wide transform. Never prime it with window_to_stage/find_button.
	viewport.size = Vector2i(1024, 768)
	click_end_phase(shell, Vector2(900, 28), edition + " input before tick/render")
	# A height change also exercises the usual 16:9 and 21:9 window sizes.
	viewport.size = Vector2i(960, 720)
	var reference720 := await capture(shell, viewport)
	for width in [1280, 1680, 1280]:
		viewport.size = Vector2i(width, 720)
		var image := await capture(shell, viewport)
		same_artwork(reference720, image, Rect2i(0, 520, 190, 200), Vector2i.ZERO, edition + " %dx720 left" % width)
		same_artwork(reference720, image, Rect2i(800, 0, 160, 60), Vector2i(width - 960, 0), edition + " %dx720 right" % width)
	for width in [1280, 1680, 1280]:
		viewport.size = Vector2i(width, 720)
		click_end_phase(shell, Vector2(width - 116, 26), edition + " %dx720 window click" % width)
	# Tick must leave newly loaded/updated clips ready before any render or
	# pointer conversion. Observe the same frozen state on either side of render;
	# no expected matrix or button centre is calculated from these diagnostics.
	viewport.size = Vector2i(1024, 768)
	shell.render(true)
	viewport.size = Vector2i(2048, 768)
	tick(shell, 1)
	var before: Dictionary = shell.instance_info(state.level, "main.endTurnButton")
	shell.render(true)
	var after: Dictionary = shell.instance_info(state.level, "main.endTurnButton")
	check(before.found and after.found, edition + " tick diagnostic clip exists")
	check(before.stage_transform == after.stage_transform, edition + " tick updates viewport before render")
	viewport.free()

func run() -> void:
	if DisplayServer.get_name() == "headless" or OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("WOTR HUD SKIP: requires GPU and ROTWK_INSTALL / BFME2_INSTALL")
		quit(77)
		return
	create_timer(180.0).timeout.connect(func(): printerr("WOTR HUD FAIL: timeout"); quit(1))
	if not check(ClassDB.class_exists("AptMenuPlayer"), "extension loaded"):
		quit(1)
		return
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not check(fs.mount_retail().ok, "retail mount"):
		quit(1)
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not check(world.setup(fs).ok, "world setup"):
		world.free()
		quit(1)
		return
	var folder := OS.get_temp_dir().path_join("openbfme-wotr-hud-%d" % OS.get_process_id())
	if not check(DirAccess.make_dir_recursive_absolute(folder) == OK, "temporary preferences directory"):
		world.free()
		quit(1)
		return
	for edition in ["RotWK", "BFME2"]:
		await exercise(fs, world, edition, folder)
	world.free()
	print("WOTR HUD ", "PASS" if failures == 0 else "FAIL", ": ", failures, " failures")
	quit(0 if failures == 0 else 1)
