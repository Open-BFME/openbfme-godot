extends SceneTree
## UI PARITY lane camera (worktree kimi-ui-parity, UI-PARITY-BRIEF.md).
##
## Photographs the REAL vertical slice on the REAL selected Men pack so the HUD
## can be judged by eye against reference/owner-playtest-2026-08-18 and
## reference/INDEX.md ([hud]/[build] captures). Same reasoning as
## playtest_trebuchet_splash_runner.gd Part B: headless Godot does not render,
## so the layout runners prove coordinates but not pixels.
##
## IT ASSERTS NOTHING. It is a camera, not a test.
##
## Frames written to <worktree>/ui-frames/:
##   01-hud-idle.png              HUD at match start, nothing selected
##   02-fortress-radial.png       fortress selected: palantir wheel + world ring
##   03-fortress-heroes-page.png  the fortress hero (revivables) page, REF-35
##   04-tooltip.png               retail tooltip panel over a hovered socket
##
## Invocation (WINDOWED, one Godot at a time):
##   Godot_v4.7 --path game --script res://tests/ui_parity_capture_runner.gd
## with OPENBFME_CONTENT=<repo>/workspace/content-packs.

const SETTLE_FRAMES := 210
const SHOT_SETTLE := 24

var _slice = null
var _out_dir := ""
var _frames := 0


func _initialize() -> void:
	call_deferred("_run")


func _capture_dir() -> String:
	var configured := OS.get_environment("OPENBFME_UI_PARITY_CAPTURE_DIR").strip_edges()
	if configured != "":
		return configured.replace("\\", "/")
	return ProjectSettings.globalize_path("res://../ui-frames").replace("\\", "/")


func _shoot(label: String) -> void:
	await RenderingServer.frame_post_draw
	var image := root.get_texture().get_image()
	if image == null:
		print("[ui-parity-capture] MISS %s (no viewport image)" % label)
		return
	var path := "%s/%s.png" % [_out_dir, label]
	var error := image.save_png(path)
	print("[ui-parity-capture] %s (err=%d)" % [path, error])


func _settle(frames: int) -> void:
	for _i in frames:
		await process_frame


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		print("[ui-parity-capture] SKIPPED: headless display server; this camera needs a window")
		quit(0)
		return
	_out_dir = _capture_dir()
	DirAccess.make_dir_recursive_absolute(_out_dir)
	var size := Vector2i(1920, 1080)
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	DisplayServer.window_set_size(size)
	root.size = size
	await process_frame
	var packed := load("res://scenes/retail_vertical_slice.tscn") as PackedScene
	if packed == null:
		print("[ui-parity-capture] FAILED: vertical-slice scene did not load")
		quit(1)
		return
	_slice = packed.instantiate()
	root.add_child(_slice)
	var waited := 0
	while waited < 6000 and not bool(_slice.ready_ok) and String(_slice.failure_reason) == "":
		waited += 1
		await process_frame
	if not bool(_slice.ready_ok):
		print("[ui-parity-capture] FAILED: slice never became ready: %s" % String(_slice.failure_reason))
		quit(1)
		return
	print("[ui-parity-capture] slice ready after %d frames" % waited)
	var sim = _slice.simulation
	sim.ai_enabled = false
	# Let the world stream in before the first frame (see the trebuchet camera:
	# early frames render black while terrain and GLBs land).
	await _settle(SETTLE_FRAMES)
	await _shoot("01-hud-idle")

	# The local fortress, centred and selected.
	var fortress_id := 0
	for sid_value in sim.structure_ids():
		var row: Dictionary = sim.structure(int(sid_value))
		if int(row.get("team", -1)) == 0 and String(row.get("structure_kind", "")) == "fortress":
			fortress_id = int(sid_value)
			break
	if fortress_id == 0:
		print("[ui-parity-capture] FAILED: no local fortress in the booted slice")
		quit(1)
		return
	var fortress: Dictionary = sim.structure(fortress_id)
	print("[ui-parity-capture] fortress=%d production=%s" % [fortress_id, str(fortress.get("production", []))])
	_slice.camera_focus = Vector2(fortress.get("position", Vector2.ZERO))
	_slice._clamp_camera_focus()
	_slice._apply_camera_transform()
	sim.clear_selection()
	_slice.selected_structure_id = fortress_id
	_slice._sync_presentation()
	await _settle(SHOT_SETTLE)
	await _shoot("02-fortress-radial")
	# Diagnosis for the revivables page: what the wheel offers, page by page.
	var hud = _slice.hud
	print("[ui-parity-capture] radial page=%s buttons=%s" % [
		String(hud.radial_page), str(hud._radial_buttons.map(func(b): return b.name)),
	])
	hud.set_radial_page(hud.RADIAL_PAGE_HEROES)
	_slice._sync_presentation()
	await _settle(SHOT_SETTLE)
	print("[ui-parity-capture] heroes page buttons=%s" % [
		str(hud._radial_buttons.map(func(b): return b.name)),
	])
	await _shoot("03-fortress-heroes-page")

	# Retail tooltip over a hovered socket (direct hover path, the same entry
	# the focused runners use).
	hud.set_radial_page(hud.RADIAL_PAGE_MAIN)
	_slice._sync_presentation()
	await _settle(6)
	var hover_button: Button = null
	for button in hud._radial_buttons:
		if button.visible and String(button.get_meta("tooltip_group", "")) != "":
			hover_button = button
			break
	if hover_button != null:
		print("[ui-parity-capture] tooltip hover on %s group=%s" % [
			hover_button.name, String(hover_button.get_meta("tooltip_group", "")),
		])
		hud.show_retail_tooltip(hover_button)
		await _settle(6)
		await _shoot("04-tooltip")
		hud.retail_tooltip.hide_tooltip()
	else:
		print("[ui-parity-capture] MISS tooltip: no radial button carries a tooltip group")
	# The same retail tooltip over the IN-WORLD ring (REF-25: the hover box
	# appears for the buttons on the building too).
	var world_button: Button = null
	for candidate in hud.world_radial_buttons():
		if String((candidate as Button).get_meta("tooltip_group", "")) != "":
			world_button = candidate
			break
	if world_button != null:
		print("[ui-parity-capture] world-ring tooltip hover on %s group=%s" % [
			world_button.name, String(world_button.get_meta("tooltip_group", "")),
		])
		hud.show_retail_tooltip(world_button)
		await _settle(6)
		await _shoot("05-world-radial-tooltip")
		hud.retail_tooltip.hide_tooltip()
	else:
		print("[ui-parity-capture] MISS world-ring tooltip: no world button carries a tooltip group")
	print("[ui-parity-capture] done")
	quit(0)
