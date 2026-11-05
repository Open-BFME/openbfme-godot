extends SceneTree
## Mounted-retail camera gestures. Run headless for event/camera checks, windowed on macOS for native monitor setup.
## Physical rotation delivery still needs a trackpad: see docs/MACOS_TRACKPAD.md.
var failures := 0

func check(ok: bool, message: String) -> bool:
	if not ok:
		failures += 1
		printerr("FAIL: ", message)
	return ok

func _initialize() -> void:
	_run.call_deferred()

func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		quit(77)
		return
	root.size = Vector2i(1280, 720)
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not check(fs.mount_retail().ok, "mount retail data"):
		quit(1)
		return
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not check(world.setup(fs).ok, "world setup"):
		quit(1)
		return
	if not check(world.load_map("map mp fall back 4p", {"logic_thread": false, "slots": [{"player": "Player_1", "faction": "FactionMen", "human": true}]}).ok, "load map"):
		quit(1)
		return
	world.set_auto_advance(false)
	var camera := Camera3D.new()
	root.add_child(camera)
	var hud = ClassDB.instantiate("InGameHudNode")
	root.add_child(hud)
	if not check(hud.setup(fs, world, camera, "Player_1", {"camera_start": Vector3(1800, 0, -1800)}).ok, "HUD setup"):
		quit(1)
		return
	hud.set_view_options({"edge_scroll_mode": "off"})
	for i in 3: await process_frame
	const POINT := Vector2(640, 300)
	var before: Dictionary = hud.get_camera()
	var state_hash: int = world.get_state_hash()
	var pan := InputEventPanGesture.new()
	pan.position = POINT
	pan.delta = Vector2(2, -2)
	root.push_input(pan, true)
	await process_frame
	var after: Dictionary = hud.get_camera()
	check(after.position != before.position, "two-finger pan moves the camera")
	check(after.height_above_ground == before.height_above_ground, "pan preserves zoom")
	check(after.angle == before.angle, "pan preserves angle")
	var pinch := InputEventMagnifyGesture.new()
	pinch.position = POINT
	pinch.factor = 1.25
	root.push_input(pinch, true)
	await process_frame
	check(hud.get_camera().height_above_ground == before.height_above_ground / 1.25, "spreading fingers zooms in proportionally: before=%s after=%s min=%s max=%s" % [before.height_above_ground, hud.get_camera().height_above_ground, before.min_height, before.max_height])
	pinch = pinch.duplicate()
	pinch.factor = 0.8
	root.push_input(pinch, true)
	await process_frame
	check(hud.get_camera().height_above_ground == before.height_above_ground, "inverse pinch restores zoom")
	hud.camera_set_angle_pitch(0.0, before.pitch)
	check(hud.trackpad_rotate(90.0, POINT), "native rotation adapter accepts world input")
	# tolerance: AppKit degrees become the camera's float32 radians.
	check(is_equal_approx(hud.get_camera().angle, PI / 2.0), "finger rotation changes camera heading")
	check(hud.get_camera().height_above_ground == before.height_above_ground, "rotation preserves zoom")
	check(hud.trackpad_rotate(-90.0, POINT), "inverse rotation accepted")
	check(hud.get_camera().angle == 0.0, "inverse rotation restores heading")

	var stable: Dictionary = hud.get_camera()
	for invalid in [0.0, -1.0, INF, NAN]:
		check(not hud.trackpad_zoom(invalid, POINT), "invalid pinch rejected")
	check(not hud.trackpad_rotate(INF, POINT), "invalid rotation rejected")
	check(not hud.trackpad_pan(Vector2(NAN, 0), POINT), "invalid pan rejected")
	check(not hud.trackpad_rotate(30.0, Vector2(-1, -1)), "outside-viewport rotation rejected")
	hud.set_input_enabled(false)
	check(not hud.trackpad_pan(Vector2(1, 1), POINT), "disabled pan rejected")
	check(not hud.trackpad_zoom(2.0, POINT), "disabled zoom rejected")
	check(not hud.trackpad_rotate(30.0, POINT), "disabled rotation rejected")
	hud.set_input_enabled(true)
	var radar: Rect2 = hud.get_radar_square()
	if check(radar.size != Vector2.ZERO, "radar hit region available"):
		check(not hud.trackpad_pan(Vector2(1, 1), radar.get_center()), "HUD pan stays on the HUD")
		check(not hud.trackpad_zoom(2.0, radar.get_center()), "HUD pinch stays on the HUD")
		check(not hud.trackpad_rotate(30.0, radar.get_center()), "HUD rotation stays on the HUD")
	check(hud.get_camera().position == stable.position and hud.get_camera().angle == stable.angle and hud.get_camera().height_above_ground == stable.height_above_ground, "rejected gestures leave camera unchanged")
	check(world.get_state_hash() == state_hash, "camera gestures leave simulation unchanged")
	if OS.get_name() == "macOS" and DisplayServer.get_name() != "headless":
		check(hud.get_camera().native_trackpad_rotation, "native rotation monitor installed")
	hud.queue_free()
	await process_frame
	print("MACOS TRACKPAD ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
