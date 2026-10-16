extends SceneTree
class FakeHud extends Node:
	var activated: Array = []
	var store_open := false
	var command_count := 6
	var queue_count := 3
	var production_progress := 0.25
	var selected_ids: Array = [42]
	var selection_available := true
	var placing := false
	var sell_first := false
	func get_placement() -> Dictionary: return {"placing":placing,"has_ghost":placing}
	func get_cursor_image() -> Dictionary: return {"name":"Select"}
	func get_state() -> Dictionary: return {"gui_command":false}
	func get_controller_target(_radius: float) -> Dictionary: return {"can_select":selection_available}
	func is_ready() -> bool: return true
	func get_command_buttons() -> Array:
		var result := []
		for i in command_count:
			result.append({"slot":i,"name":"Command%d"%i,"state":1,"in_palantir":true,"image":"", "queued":queue_count if i == 0 else 0, "timer":production_progress if i == 0 else -1.0,"command":"SELL" if sell_first and i == 0 else "UNIT_BUILD"})
		return result
	func get_command_icon(_name: String): return null
	func get_controller_objects(_role: String) -> Array: return [{"id":42,"name":"Builder"}]
	func get_spellbook_state() -> Dictionary:
		return {"slots": [], "slot_buttons": [], "store": {"open": store_open, "sciences": ["Heal"], "states": ["_up"]}}
	func open_spell_store() -> bool:
		store_open = true
		return true
	func close_spell_store() -> void: store_open = false
	func spell_store_click(index: int) -> void: activated.append(["purchase",index])
	func get_selection() -> Array: return selected_ids
	func select_controller_group(ids: PackedInt64Array) -> void: activated.append(ids)
	func select_object_with_message(id: int) -> void: activated.append(id)
	func activate_command(slot: int, _arc: bool, cancel: bool) -> bool:
		activated.append([slot,cancel])
		return true
func _initialize() -> void:
	call_deferred("_run")
func _run() -> void:
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.execute_lua("bfme.console.reload()").ok)
	var bridge := preload("res://scripts/controller_input.gd").new()
	root.add_child(bridge)
	bridge.set_process(false)
	var live := FakeHud.new()
	root.add_child(live)
	var ui := preload("res://scripts/controller_ui.gd").new()
	ui.bridge = bridge
	var lua_calls := {"count":0}
	ui.execute = func(code: String) -> Dictionary:
		lua_calls.count += 1
		return world.execute_lua(code)
	ui.hud = func() -> Node: return live
	root.add_child(ui)
	assert(ui._shortcut_layout.size() == 4)
	lua_calls.count = 0
	ui._toggle(true)
	assert(lua_calls.count <= 2, "Trigger-open Lua calls: " + str(lua_calls.count))
	live.selected_ids = []
	ui._update_reticle(live, true)
	assert(ui._reticle.texture.atlas == preload("res://scripts/controller_art.gd").texture("reticle_a"))
	live.selection_available = false
	ui._update_reticle(live, true)
	assert(ui._reticle.texture.atlas == preload("res://scripts/controller_art.gd").texture("reticle_yellow"))
	live.selected_ids = [42]
	assert(ui._commands.get_child_count() == 6)
	var circle: Button = ui._commands.get_child(0)
	assert(circle._has_point(circle.size * 0.5))
	assert(not circle._has_point(Vector2.ZERO))
	assert(circle.text.is_empty())
	assert(circle._queue_count.visible and circle._queue_count.text == "3")
	assert(circle._clock.visible and circle._clock.progress == 0.25)
	assert(not ui._commands.get_child(1)._clock.visible)
	assert(circle._glyph.visible)
	for i in range(1, ui._commands.get_child_count()):
		var unfocused = ui._commands.get_child(i)
		assert(not unfocused._glyph.visible and not unfocused._queue_count.visible)
	live.queue_count = 2
	live.production_progress = 0.75
	ui._refresh()
	assert(ui._commands.get_child(0)._queue_count.text == "2")
	assert(ui._commands.get_child(0)._clock.progress == 0.75)
	live.production_progress = 1.0
	ui._refresh()
	assert(not ui._commands.get_child(0)._clock.visible)
	assert(not ui._commands.get_child(1)._glyph.visible)
	live.queue_count = 1
	ui._refresh()
	assert(not ui._commands.get_child(0)._queue_count.visible)
	live.queue_count = 2
	live.sell_first = true
	ui._refresh()
	assert(ui._entries.back().command == "SELL")
	assert(ui._entries.front().command != "SELL")
	live.sell_first = false
	ui._refresh()
	live.placing = true
	ui._update_reticle(live,true)
	assert(not ui._reticle.visible)
	live.placing = false
	ui._update_reticle(live,true)
	assert(ui._reticle.visible)
	var help := ui._entry_tooltip({"name":"internal", "title":"Build Farm", "description":"Produces resources.", "cost":300, "queued":2, "state":3})
	assert(help.contains("Build Farm") and help.contains("Produces resources.") and help.contains("300 gold") and help.contains("Not enough resources"))
	for i in 4: ui.action("nextCommand")
	assert(ui._state[2] == "5")
	# A new selected building starts at command 1; refreshes preserve navigation.
	live.selected_ids = [99]
	ui._refresh()
	assert(ui._state[0] == "commands" and ui._state[2] == "1")
	ui.action("nextCommand")
	ui._refresh()
	assert(ui._state[2] == "2")
	live.selected_ids = [42]
	ui._refresh()
	assert(ui._state[2] == "1")
	for i in 4: ui.action("nextCommand")

	assert(ui._commands.get_child_count() == 6)
	ui.action("pressCommand")
	assert(live.activated.back() == [4,false])
	ui.action("rightPressCommand")
	assert(live.activated.back() == [4,true])
	ui.action("shortcutUp")
	ui.action("shortcutUp")
	ui.action("shortcutUp")
	ui.action("pressCommand")
	assert(ui._state[0] == "builders")
	ui.action("pressCommand")
	assert(live.activated.back() == PackedInt64Array([42]))
	ui.action("backPalette")
	ui.action("shortcutUp")
	ui.action("shortcutDown")
	assert(ui._state[1] == "commands")
	ui.action("saveFavorites")
	assert(ui._favorites == PackedInt64Array([42]))
	ui.action("backPalette")
	ui.action("shortcutUp")
	ui.action("pressCommand")
	assert(ui._state[0] == "powers")
	ui.action("pressCommand")
	assert(live.store_open)
	ui.action("pressCommand")
	assert(live.activated.back() == ["purchase",0])
	ui.action("rightPressCommand")
	assert(not live.store_open)
	assert(world.execute_lua("bfme.console.reload()").ok)
	ui._refresh()
	assert(ui._state[0] == "commands" and ui._state[2] == "1")
	live.command_count = 33
	ui._refresh()
	await process_frame
	await process_frame
	assert(ui._commands.get_child_count() == 33)
	var last: Control = ui._commands.get_child(32)
	assert(last.get_global_rect().end.x <= root.get_visible_rect().size.x + 1.0, str(last.get_global_rect()))
	assert(last.size.x < 72.0)
	ui._toggle(false)
	assert(not ui._panel.visible)
	ui.queue_free()
	bridge.queue_free()
	world.queue_free()
	live.queue_free()
	await process_frame
	print("CONTROLLER UI PASS")
	quit()
