extends SceneTree
## Real-input gate: drives the HUD with synthetic InputEventMouse* events (the
## same path a player's clicks take), not signal emission. Catches input-layer
## regressions — overlays swallowing clicks, dead zones, z-order — that
## `pressed.emit()` style tests cannot see.

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _click(position: Vector2) -> void:
	var move := InputEventMouseMotion.new()
	move.position = position
	move.global_position = position
	root.push_input(move)
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	press.position = position
	press.global_position = position
	root.push_input(press)
	var release := InputEventMouseButton.new()
	release.button_index = MOUSE_BUTTON_LEFT
	release.pressed = false
	release.position = position
	release.global_position = position
	root.push_input(release)


func _button_center(button: Button) -> Vector2:
	return button.get_global_rect().get_center()


func _hovered_name(position: Vector2) -> String:
	var move := InputEventMouseMotion.new()
	move.position = position
	move.global_position = position
	root.push_input(move)
	var hovered := root.gui_get_hovered_control()
	return hovered.name if hovered != null else "<none>"


func _run() -> void:
	var slice_script = load("res://src/retail_slice/retail_vertical_slice.gd")
	var slice = slice_script.new()
	root.add_child(slice)
	await process_frame
	_check("probe_slice_ready", bool(slice.ready_ok), slice.failure_reason)
	if not slice.ready_ok:
		_finish()
		return
	await process_frame

	# Select a combat battalion through the sim, sync, then click Stop and the
	# stance button with real events.
	slice.simulation.select_only(1)
	slice._sync_presentation()
	await process_frame
	var stop_button: Button = slice.hud.unit_action_buttons.get("stop")
	var stance_button: Button = slice.hud.unit_action_buttons.get("stance_toggle", slice.hud.unit_action_buttons.get("stance"))
	_check("probe_action_buttons_exist", stop_button != null, str(slice.hud.unit_action_buttons.keys()))
	if stop_button == null:
		_finish()
		return
	_check("probe_stop_button_visible", stop_button.visible and not stop_button.disabled)
	var stop_center := _button_center(stop_button)
	# Control experiment: a bare button directly under root proves whether the
	# pushed-event pipeline can press ANY control in this harness.
	var control_button := Button.new()
	control_button.text = "probe"
	control_button.position = Vector2(50, 50)
	control_button.size = Vector2(120, 40)
	root.add_child(control_button)
	var control_pressed := [false]
	control_button.pressed.connect(func() -> void: control_pressed[0] = true)
	await process_frame
	_click(control_button.get_global_rect().get_center())
	await process_frame
	print("RETAIL_INPUT_PROBE control_button_pressed=%s" % control_pressed[0])
	var layer_button := Button.new()
	layer_button.text = "layer"
	layer_button.position = Vector2(300, 50)
	layer_button.size = Vector2(120, 40)
	slice.hud_root.get_parent().add_child(layer_button)
	var layer_pressed := [false]
	layer_button.pressed.connect(func() -> void: layer_pressed[0] = true)
	await process_frame
	_click(layer_button.get_global_rect().get_center())
	await process_frame
	print("RETAIL_INPUT_PROBE layer_button_pressed=%s layer_class=%s" % [
		layer_pressed[0],
		slice.hud_root.get_parent().get_class(),
	])
	# Minimal repro: bare Node3D -> Control -> Button chain with no slice code.
	var bare_3d := Node3D.new()
	root.add_child(bare_3d)
	var bare_wrap := Control.new()
	bare_wrap.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	bare_wrap.mouse_filter = Control.MOUSE_FILTER_IGNORE
	bare_3d.add_child(bare_wrap)
	var bare_button := Button.new()
	bare_button.text = "bare"
	bare_button.position = Vector2(50, 150)
	bare_button.size = Vector2(110, 40)
	bare_wrap.add_child(bare_button)
	var bare_hit := [false]
	bare_button.pressed.connect(func() -> void: bare_hit[0] = true)
	await process_frame
	_click(bare_button.get_global_rect().get_center())
	await process_frame
	print("RETAIL_INPUT_PROBE bare_node3d_chain_pressed=%s" % bare_hit[0])
	# Depth bisect: identical buttons parented at each ancestry level.
	var depth_parents := {
		"slice_direct": slice,
		"hud_control": slice.hud,
		"panel": slice.hud.command_panel,
	}
	var offset_x := 500
	for label in depth_parents.keys():
		var probe_button := Button.new()
		probe_button.text = label
		probe_button.position = Vector2(offset_x, 50)
		probe_button.size = Vector2(110, 40)
		probe_button.top_level = true if depth_parents[label] is Node3D else false
		depth_parents[label].add_child(probe_button)
		var hit := [false]
		probe_button.pressed.connect(func() -> void: hit[0] = true)
		await process_frame
		_click(probe_button.get_global_rect().get_center())
		await process_frame
		print("RETAIL_INPUT_PROBE depth_%s_pressed=%s rect=%s" % [label, hit[0], probe_button.get_global_rect()])
		offset_x += 130
	var stop_pressed := [false]
	stop_button.pressed.connect(func() -> void: stop_pressed[0] = true)
	print("RETAIL_INPUT_PROBE hover_at_minimap=%s" % _hovered_name(slice.hud.minimap.get_global_rect().get_center()))
	print("RETAIL_INPUT_PROBE hover_at_stop=%s rect=%s" % [_hovered_name(stop_center), stop_button.get_global_rect()])
	var chain: Node = stop_button
	while chain != null and chain is CanvasItem:
		var canvas_item := chain as CanvasItem
		var filter_text := "-"
		var rect_text := "-"
		if canvas_item is Control:
			filter_text = str((canvas_item as Control).mouse_filter)
			rect_text = str((canvas_item as Control).get_global_rect())
		print("RETAIL_INPUT_PROBE chain node=%s class=%s visible=%s filter=%s rect=%s" % [
			canvas_item.name, canvas_item.get_class(), canvas_item.visible, filter_text, rect_text
		])
		chain = canvas_item.get_parent()
	await process_frame
	var stance_before := String(slice.simulation.entity(1).get("stance", "Battle"))
	_click(stop_center)
	await process_frame
	var feedback := String(slice.hud.feedback_label.text) if slice.hud.feedback_label != null else ""
	print("RETAIL_INPUT_PROBE stop_pressed_signal=%s" % stop_pressed[0])
	_check("probe_stop_click_reaches_handler", stop_pressed[0] or feedback.contains("Stop order"), "hovered=%s feedback=%s" % [_hovered_name(stop_center), feedback])
	if stance_button != null and stance_button.visible:
		_click(_button_center(stance_button))
		await process_frame
		var stance_after := String(slice.simulation.entity(1).get("stance", "Battle"))
		_check("probe_stance_click_toggles", stance_after != stance_before, "hovered=%s %s->%s" % [_hovered_name(_button_center(stance_button)), stance_before, stance_after])

	# Structure selection: train button must also accept real clicks.
	slice.simulation.clear_selection()
	slice.selected_structure_id = slice.simulation.structure_ids(0)[2] if slice.simulation.structure_ids(0).size() > 2 else 0
	slice._sync_presentation()
	await process_frame
	var train_button: Button = slice.hud.train_buttons.values()[0] if not slice.hud.train_buttons.is_empty() else null
	if train_button != null and train_button.visible:
		print("RETAIL_INPUT_PROBE hover_at_train=%s" % _hovered_name(_button_center(train_button)))
	slice.free()
	_finish()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_INPUT_PROBE PASS %s" % name)
	else:
		failed += 1
		print("RETAIL_INPUT_PROBE FAIL %s (%s)" % [name, detail])


func _finish() -> void:
	print("RETAIL_INPUT_PROBE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)
