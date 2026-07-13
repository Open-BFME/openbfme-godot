class_name Stage7Hud
extends CanvasLayer
## Data-driven difficulty, execution, and finite-resource evidence controls.

signal difficulty_requested(difficulty_id: String)
signal step_requested
signal run_requested
signal starvation_requested
signal pause_requested
signal reset_requested
signal menu_requested

var difficulty_buttons: Dictionary = {}
var sim_label: Label
var economy_label: Label
var plan_label: Label
var job_label: Label
var victory_label: Label
var event_label: Label
var feedback_label: Label
var difficulty_row: HBoxContainer
var pause_button: Button
var run_button: Button


func _ready() -> void:
	_build_interface()


func configure(catalog: RefCounted) -> void:
	_clear_children(difficulty_row)
	difficulty_buttons.clear()
	for difficulty_id: String in catalog.difficulty_ids():
		var definition: Dictionary = catalog.difficulty(difficulty_id)
		var button := Button.new()
		button.text = "%s  think %d / income %d%% / attack %d%%" % [String(definition["displayName"]), int(definition["thinkIntervalTicks"]), int(definition["incomePermille"]) / 10, int(definition["attackPermille"]) / 10]
		button.pressed.connect(func() -> void: difficulty_requested.emit(difficulty_id))
		difficulty_row.add_child(button)
		difficulty_buttons[difficulty_id] = button


func refresh(world: RefCounted, paused: bool) -> void:
	if world == null:
		return
	var difficulty: Dictionary = world.difficulty
	sim_label.text = "TICK %04d  |  HASH %s  |  %s" % [int(world.tick_index), String(world.state_hash_text()), "PAUSED" if paused else "LIVE 8 TICKS/S"]
	economy_label.text = "SUPPLIES %d  |  FINITE DEPOSIT %d/%d  |  EXTRACTOR %s  |  ARMY %d" % [int(world.resources), int(world.finite_resource_remaining), int(world.scenario["finiteResourceAmount"]), "BUILT" if world.extractor_built else "NOT BUILT", int(world.army_size)]
	var current: Dictionary = world.current_step()
	plan_label.text = "%s  |  STEP %d/%d: %s  |  think every %d ticks" % [String(world.catalog.plan(world.plan_id)["displayName"]), mini(int(world.plan_index) + 1, int(world.planner.step_count())), int(world.planner.step_count()), String(current.get("action", "complete")).to_upper(), int(difficulty["thinkIntervalTicks"])]
	job_label.text = "JOB: idle" if world.active_job.is_empty() else "JOB: %s %s completes tick %d" % [String(world.active_job["action"]), String(world.active_job["object_id"]), int(world.active_job["complete_tick"])]
	if world.victory:
		victory_label.text = "VICTORY at tick %d  |  %s attack  |  fortress 0/%d" % [int(world.tick_index), "FALLBACK" if world.fallback_attack else "PLANNED", int(world.enemy_fortress_maximum)]
		victory_label.modulate = Color("83e6a5")
	else:
		victory_label.text = "ENEMY CORE %d/%d  |  %s" % [int(world.enemy_fortress_health), int(world.enemy_fortress_maximum), "FALLBACK ATTACK" if world.fallback_attack else "ATTACKING" if world.attack_started else "PLANNING"]
		victory_label.modulate = Color("ff9a92")
	event_label.text = _event_text(world.command_log)
	pause_button.text = "Resume" if paused else "Pause"
	run_button.disabled = world.victory
	for difficulty_id: String in difficulty_buttons:
		(difficulty_buttons[difficulty_id] as Button).disabled = difficulty_id == world.difficulty_id


func set_feedback(message: String, success: bool = false) -> void:
	feedback_label.text = message
	feedback_label.modulate = Color("83e6a5") if success else Color("ffd08a")


func _event_text(events: Array) -> String:
	var skipped: int = 0
	var fallback_ordered: bool = false
	for event: Dictionary in events:
		if String(event.get("type", "")) == "skip":
			skipped += 1
		if String(event.get("type", "")) == "attack_order" and bool(Dictionary(event.get("details", {})).get("fallback", false)):
			fallback_ordered = true
	var summary: String = "AUTHORITATIVE EVENT LOG (latest first)"
	if skipped > 0:
		summary += "  |  skipped %d: finite_resources_exhausted" % skipped
	if fallback_ordered:
		summary += "  |  attack_order fallback=true"
	var lines: PackedStringArray = [summary]
	var first: int = maxi(0, events.size() - 9)
	for index: int in range(events.size() - 1, first - 1, -1):
		var event: Dictionary = events[index]
		lines.append("%03d  t%03d  %s  %s" % [int(event["sequence"]), int(event["tick"]), String(event["type"]), str(event["details"])])
	return "\n".join(lines)


func _build_interface() -> void:
	var root := Control.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)
	var top := PanelContainer.new()
	top.position = Vector2(24, 18)
	top.size = Vector2(1872, 126)
	top.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(top)
	var header := VBoxContainer.new()
	top.add_child(header)
	var title := Label.new()
	title.text = "STAGE 7  |  DETERMINISTIC AI & FINITE-RESOURCE VICTORY LOOP"
	title.add_theme_font_size_override("font_size", 24)
	header.add_child(title)
	sim_label = Label.new()
	header.add_child(sim_label)
	difficulty_row = HBoxContainer.new()
	difficulty_row.add_theme_constant_override("separation", 8)
	header.add_child(difficulty_row)

	var side := PanelContainer.new()
	side.position = Vector2(844, 164)
	side.size = Vector2(1052, 850)
	root.add_child(side)
	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 16)
	margin.add_theme_constant_override("margin_right", 16)
	margin.add_theme_constant_override("margin_top", 14)
	margin.add_theme_constant_override("margin_bottom", 14)
	side.add_child(margin)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 9)
	margin.add_child(column)
	economy_label = _label(16, Color("8edbff"))
	column.add_child(economy_label)
	plan_label = _label(16, Color("f4dd98"))
	column.add_child(plan_label)
	job_label = _label(15, Color("b9cbd4"))
	column.add_child(job_label)
	victory_label = _label(18, Color("ff9a92"))
	column.add_child(victory_label)
	column.add_child(HSeparator.new())
	var action_row := HBoxContainer.new()
	action_row.add_theme_constant_override("separation", 8)
	column.add_child(action_row)
	action_row.add_child(_button("Step 1 Tick", func() -> void: step_requested.emit()))
	run_button = _button("Run to Victory", func() -> void: run_requested.emit())
	action_row.add_child(run_button)
	action_row.add_child(_button("Zero-Resource Probe", func() -> void: starvation_requested.emit()))
	pause_button = _button("Pause", func() -> void: pause_requested.emit())
	action_row.add_child(pause_button)
	action_row.add_child(_button("Reset", func() -> void: reset_requested.emit()))
	action_row.add_child(_button("Menu", func() -> void: menu_requested.emit()))
	event_label = _label(14, Color("b9d0da"))
	event_label.custom_minimum_size.y = 470
	event_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(event_label)
	feedback_label = _label(15, Color("ffd08a"))
	feedback_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	feedback_label.custom_minimum_size.y = 70
	column.add_child(feedback_label)


func _button(text: String, callback: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.pressed.connect(callback)
	return button


func _label(size: int, color: Color) -> Label:
	var label := Label.new()
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", color)
	return label


func _clear_children(node: Node) -> void:
	for child: Node in node.get_children():
		node.remove_child(child)
		child.queue_free()
