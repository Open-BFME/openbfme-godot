extends SceneTree
## Asset-free visibility transitions through the real preview tick. No GPU readback or retail files.
## Manual: godot --headless --path godot --script res://tests/wotr_preview_visibility_test.gd

const Preview = preload("res://scripts/wotr_preview.gd")
var _failures: Array[String] = []

class FakeShell extends RefCounted:
	var state := {"found": false}
	func strategic_setup(_request: Dictionary) -> Dictionary:
		return state
	func stage_to_window(point: Vector2) -> Vector2:
		return point

class FakeGame extends Node:
	var _shell := FakeShell.new()

class FakeLivingWorld extends RefCounted:
	var lighting_calls := 0
	func apply_map_lighting() -> void:
		lighting_calls += 1

class FakeRegionFx extends Node3D:
	func update_gamma() -> void:
		pass
	func clear_regions() -> void:
		pass

func _check(ok: bool, message: String) -> void:
	if not ok:
		_failures.append(message)
		printerr("FAIL: ", message)

func _initialize() -> void:
	var game := FakeGame.new()
	var preview := Preview.new()
	preview.init(game)
	# Use an already-built synthetic preview: test the actual tick without loading a retail map.
	preview.vp = SubViewport.new()
	preview.vp.size = Vector2i(512, 384)
	preview.vp.render_target_update_mode = SubViewport.UPDATE_DISABLED
	preview.add_child(preview.vp)
	preview.built_campaign = "fixture"
	var living_world := FakeLivingWorld.new()
	preview.lw = living_world
	preview.region_fx = FakeRegionFx.new()
	preview.vp.add_child(preview.region_fx)
	var process_mode: int = preview.vp.process_mode
	# Reproduce the original runtime state explicitly: its builder enabled ALWAYS.
	# A hidden transition must stop that existing render, independently of the new
	# builder's initially-disabled mode or the reveal implementation below.
	preview.view.visible = true
	preview.vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	preview.pressed_button = MOUSE_BUTTON_LEFT
	game._shell.state = {"found": true, "visible": false}
	preview.tick()
	_check(not preview.view.visible, "already-visible setup hides its image")
	_check(preview.pressed_button == -1, "covering or leaving the setup cancels an unfinished map click")
	_check(preview.vp.render_target_update_mode == SubViewport.UPDATE_DISABLED, "already-rendering preview stops when setup becomes hidden")
	_check(preview.vp.process_mode == process_mode, "already-rendering preview preserves its animation processing mode")
	# Reset the synthetic viewport to test the full unopened/open/close/reopen path.
	preview.view.visible = false
	preview.vp.render_target_update_mode = SubViewport.UPDATE_DISABLED
	game._shell.state = {"found": false}
	preview.tick()
	_check(not preview.view.visible, "unopened setup stays hidden")
	_check(preview.vp.render_target_update_mode == SubViewport.UPDATE_DISABLED, "unopened setup does not render")
	game._shell.state = {"found": true, "visible": true, "region_campaign": "fixture",
		"rect": {"x": 0, "y": 0, "w": 512, "h": 384}, "holders": [], "next_slot": -1, "next_color": 0}
	preview.tick()
	_check(preview.view.visible, "opening the setup shows the preview")
	_check(preview.vp.render_target_update_mode == SubViewport.UPDATE_ALWAYS, "visible preview renders")
	_check(living_world.lighting_calls == 1, "first reveal restores map lighting")
	game._shell.state.visible = false
	preview.tick()
	_check(not preview.view.visible, "closing the setup hides its image")
	_check(preview.vp.render_target_update_mode == SubViewport.UPDATE_DISABLED, "hidden preview stops drawing its map")
	_check(preview.vp.process_mode == process_mode, "hiding preserves the animation processing mode")
	preview.tick()
	_check(preview.vp.render_target_update_mode == SubViewport.UPDATE_DISABLED, "hidden repeated ticks keep rendering stopped")
	game._shell.state.visible = true
	preview.tick()
	_check(preview.view.visible, "reopening shows the retained preview")
	_check(preview.vp.render_target_update_mode == SubViewport.UPDATE_ALWAYS, "reopening resumes rendering")
	_check(living_world.lighting_calls == 2, "reopening restores map lighting again")
	game._shell.state = {"found": false}
	preview.tick()
	_check(preview.vp.render_target_update_mode == SubViewport.UPDATE_DISABLED, "leaving the setup entirely stops rendering")
	preview.free()
	game.free()
	if _failures.is_empty():
		print("WOTR PREVIEW VISIBILITY PASS")
		quit()
	else:
		quit(1)
