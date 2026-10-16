extends SceneTree
class FakeHud extends Node:
	var placement := {}
	var build_actions: Array = []
	func get_placement() -> Dictionary: return placement
	func touch_build(action: String, _value: Vector2) -> bool:
		build_actions.append(action)
		return true
	var cleared := false
	var keys: Array = []
	func deselect_all() -> void: cleared = true
	func inject_key(key: int, down: bool) -> void: keys.append([key,down])
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var hud := FakeHud.new()
	root.add_child(hud)
	var toolbar = preload("res://scripts/touch_toolbar.gd").new()
	var playing := {"value":true}
	var menu := {"opened":false, "console":false}
	toolbar.hud = func() -> Node: return hud
	toolbar.gameplay = func() -> bool: return playing.value
	toolbar.open_menu = func() -> void: menu.opened = true
	toolbar.open_console = func() -> void: menu.console = true
	root.add_child(toolbar)
	await process_frame
	toolbar._process(0)
	assert(toolbar.bar.visible and toolbar.bar.get_child_count() == 4)
	toolbar.bar.get_child(0).pressed.emit()
	toolbar.bar.get_child(1).pressed.emit()
	toolbar.bar.get_child(2).pressed.emit()
	toolbar.bar.get_child(3).pressed.emit()
	assert(menu.console)
	assert(toolbar.bar.get_child(0).custom_minimum_size.y >= 72)
	assert(toolbar.menu_panel.get_theme_stylebox("panel").bg_color.a == 1.0)
	toolbar.compact_mode = true
	toolbar._process(0)
	assert(toolbar.menu_button.visible and not toolbar.bar.visible)
	toolbar.menu_button.pressed.emit()
	toolbar._process(0)
	assert(toolbar.bar.visible)
	root.size = Vector2i(1920, 886)
	await process_frame
	await process_frame
	toolbar._process(0)
	var menu_rect: Rect2 = toolbar.menu_panel.get_global_rect()
	assert(menu_rect.position.x >= 0 and menu_rect.end.x <= 1920)
	assert(Engine.get_meta("touch_toolbar_rect").encloses(menu_rect))
	toolbar.bar.get_child(0).pressed.emit()
	toolbar._process(0)
	assert(not toolbar.bar.visible)
	assert(hud.cleared and hud.keys == [[31,true],[31,false]] and menu.opened)
	hud.placement = {"placing":true,"template":"GondorBarracks","anchored":false,"legal":0}
	toolbar._process(0)
	assert(toolbar.placement_panel.visible and toolbar.build_button.disabled)
	hud.placement.anchored = true
	hud.placement.touch_rotating = true
	toolbar._process(0)
	assert(not toolbar.build_button.disabled)
	toolbar._placement_action("Rotate left")
	toolbar._placement_action("Build")
	toolbar._placement_action("Cancel")
	assert(hud.build_actions == ["rotate","confirm","cancel"])
	hud.placement.legal = 3
	toolbar._process(0)
	assert(toolbar.build_button.disabled)
	playing.value = false
	toolbar._process(0)
	assert(not toolbar.bar.visible)
	assert(not Engine.get_meta("touch_toolbar_rect").has_area())
	toolbar.queue_free()
	await process_frame
	print("TOUCH TOOLBAR PASS")
	quit()
