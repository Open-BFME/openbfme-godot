class_name Stage4Hud
extends CanvasLayer
## Data-driven Stage 4 lab controls and live deterministic state readout.

signal ability_requested(code: int)
signal stance_requested(code: String)
signal xp_requested
signal fear_requested(terror: bool)
signal immunity_requested
signal casualty_requested
signal kill_requested
signal revive_requested
signal train_requested
signal pause_requested
signal reset_requested
signal menu_requested

var ability_buttons: Dictionary = {}
var stance_buttons: Dictionary = {}
var root: Control
var sim_label: Label
var resources_label: Label
var selection_label: Label
var roster_label: Label
var target_label: Label
var targeting_label: Label
var feedback_label: Label
var ability_grid: GridContainer
var pause_button: Button
var immunity_button: Button
var casualty_button: Button
var kill_button: Button
var revive_button: Button
var train_button: Button

var _ability_definitions: Array[Dictionary] = []


func _ready() -> void:
	_build_interface()


func configure(ability_definitions: Array[Dictionary]) -> void:
	_ability_definitions = ability_definitions.duplicate(true)
	_ability_definitions.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return int(left.get("code", 0)) < int(right.get("code", 0)))
	for child: Node in ability_grid.get_children():
		child.queue_free()
	ability_buttons.clear()
	for definition: Dictionary in _ability_definitions:
		var code: int = int(definition["code"])
		var button := Button.new()
		button.custom_minimum_size = Vector2(300, 68)
		button.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
		button.tooltip_text = "%s target · rank %d · cooldown %d ticks\nEffect: %s (%d / %d)" % [
			_friendly_mode(String(definition["target_mode"])),
			int(definition["rank_required"]),
			int(definition["cooldown_ticks"]),
			String(definition["effect"]).replace("_", " ").capitalize(),
			int(definition["magnitude"]),
			int(definition["secondary_magnitude"]),
		]
		button.pressed.connect(_emit_ability.bind(code))
		ability_grid.add_child(button)
		ability_buttons[code] = button


func refresh(world: RefCounted, selected_id: int, inspected_target_id: int, targeting_mode: String, paused: bool) -> void:
	if world == null:
		return
	sim_label.text = "TICK %05d  ·  HASH %s  ·  %s" % [int(world.tick_index), String(world.state_hash_text()), "PAUSED" if paused else "LIVE 10 TICKS/S"]
	var revival_cost: int = int(world.revival_cost(0))
	resources_label.text = "BLUE SUPPLIES  %d     REVIVAL  %s" % [int(world.resources[0]), "%d" % revival_cost if revival_cost > 0 else "—"]
	var selected: Dictionary = world.entity(selected_id)
	_refresh_selection(selected)
	_refresh_abilities(world, selected)
	_refresh_stances(selected)
	var inspected: Dictionary = world.entity(inspected_target_id)
	_refresh_target(world, inspected)
	targeting_label.text = "TARGETING: %s" % _friendly_mode(targeting_mode) if targeting_mode != "" else "TARGETING: none · click blue to select, red to inspect"
	pause_button.text = "Resume Simulation" if paused else "Pause Simulation"
	revive_button.disabled = revival_cost == 0
	kill_button.disabled = selected.is_empty() or String(selected.get("kind", "")) != "champion" or int(selected.get("team", -1)) != 0 or not bool(selected.get("alive", false))
	casualty_button.disabled = not _selected_or_default_squad_available(world, selected)
	train_button.disabled = false


func set_feedback(message: String, success: bool = false) -> void:
	feedback_label.text = message
	feedback_label.modulate = Color("89efb3") if success else Color("ffd08a")


func _build_interface() -> void:
	root = Control.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)

	var top_panel := PanelContainer.new()
	top_panel.set_anchors_preset(Control.PRESET_TOP_WIDE)
	top_panel.offset_left = 22.0
	top_panel.offset_top = 18.0
	top_panel.offset_right = -22.0
	top_panel.offset_bottom = 142.0
	top_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	top_panel.add_theme_stylebox_override("panel", _panel_style(Color("0b1722e8"), Color("417793")))
	root.add_child(top_panel)
	var header := VBoxContainer.new()
	header.add_theme_constant_override("separation", 3)
	top_panel.add_child(header)
	var title := Label.new()
	title.text = "STAGE 4  ·  CHAMPION & STATUS LAB"
	title.add_theme_font_size_override("font_size", 26)
	title.add_theme_color_override("font_color", Color("8edbff"))
	header.add_child(title)
	var subtitle := Label.new()
	subtitle.text = "Legal-safe neutral primitives · external champion rules · deterministic GDScript proof authority"
	subtitle.add_theme_color_override("font_color", Color("adc2cf"))
	header.add_child(subtitle)
	sim_label = Label.new()
	sim_label.add_theme_font_size_override("font_size", 17)
	sim_label.add_theme_color_override("font_color", Color("f7e2a0"))
	header.add_child(sim_label)
	resources_label = Label.new()
	resources_label.add_theme_font_size_override("font_size", 16)
	resources_label.add_theme_color_override("font_color", Color("a8f0c0"))
	header.add_child(resources_label)

	var side := PanelContainer.new()
	side.set_anchors_preset(Control.PRESET_RIGHT_WIDE)
	side.anchor_left = 1.0
	side.offset_left = -720.0
	side.offset_top = 158.0
	side.offset_right = -22.0
	side.offset_bottom = -18.0
	side.mouse_filter = Control.MOUSE_FILTER_STOP
	side.add_theme_stylebox_override("panel", _panel_style(Color("0c1825f2"), Color("3f667b")))
	root.add_child(side)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	side.add_child(scroll)
	var column := VBoxContainer.new()
	column.custom_minimum_size.x = 650.0
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 9)
	scroll.add_child(column)

	selection_label = _info_label("SELECTED: —")
	column.add_child(selection_label)
	roster_label = _info_label("ROSTER: —")
	roster_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(roster_label)
	target_label = _info_label("INSPECTED TARGET: —")
	target_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(target_label)
	targeting_label = _info_label("TARGETING: none")
	targeting_label.add_theme_color_override("font_color", Color("e8b4ff"))
	column.add_child(targeting_label)
	column.add_child(_separator())

	column.add_child(_section_label("ABILITIES · GENERATED FROM champions.json"))
	ability_grid = GridContainer.new()
	ability_grid.columns = 2
	ability_grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	ability_grid.add_theme_constant_override("h_separation", 8)
	ability_grid.add_theme_constant_override("v_separation", 8)
	column.add_child(ability_grid)
	column.add_child(_hint("Choose an ability, then click a highlighted cell or entity. Rank locks and cooldowns are authoritative."))

	column.add_child(_section_label("STANCE · LIVE DAMAGE / ARMOR / SPEED"))
	var stance_row := HBoxContainer.new()
	for code: String in ["balanced", "aggressive", "defensive", "hold"]:
		var stance_button := Button.new()
		stance_button.text = code.capitalize()
		stance_button.toggle_mode = true
		stance_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		stance_button.pressed.connect(_emit_stance.bind(code))
		stance_row.add_child(stance_button)
		stance_buttons[code] = stance_button
	column.add_child(stance_row)
	var xp_button := Button.new()
	xp_button.text = "Grant Selected Champion +100 XP"
	xp_button.pressed.connect(func() -> void: xp_requested.emit())
	column.add_child(xp_button)

	column.add_child(_section_label("FEAR / TERROR · INSPECTED RED TARGET"))
	var fear_row := HBoxContainer.new()
	var fear_button := Button.new()
	fear_button.text = "Apply Fear (20)"
	fear_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	fear_button.pressed.connect(func() -> void: fear_requested.emit(false))
	fear_row.add_child(fear_button)
	var terror_button := Button.new()
	terror_button.text = "Apply Terror (+20)"
	terror_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	terror_button.pressed.connect(func() -> void: fear_requested.emit(true))
	fear_row.add_child(terror_button)
	column.add_child(fear_row)
	immunity_button = Button.new()
	immunity_button.text = "Toggle Inspected Target Fear Immunity"
	immunity_button.pressed.connect(func() -> void: immunity_requested.emit())
	column.add_child(immunity_button)
	column.add_child(_hint("Fear checks resistance deterministically; terror adds the external bonus. Fleeing moves one safe cell per tick and recovers exactly."))

	column.add_child(_section_label("ROSTER / DEATH / REVIVAL"))
	var lifecycle_grid := GridContainer.new()
	lifecycle_grid.columns = 2
	lifecycle_grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	casualty_button = _action_button("Defeat One Blue Member", func() -> void: casualty_requested.emit())
	kill_button = _action_button("Kill Selected Blue Champion", func() -> void: kill_requested.emit())
	revive_button = _action_button("Revive Blue Champion", func() -> void: revive_requested.emit())
	train_button = _action_button("Try Train Duplicate Champion", func() -> void: train_requested.emit())
	lifecycle_grid.add_child(casualty_button)
	lifecycle_grid.add_child(kill_button)
	lifecycle_grid.add_child(revive_button)
	lifecycle_grid.add_child(train_button)
	column.add_child(lifecycle_grid)
	column.add_child(_hint("Replenishment restores concrete member IDs. Revival is team-local and costs base + rank scaling + each prior death."))

	feedback_label = _info_label("Ready. Select the blue Champion or cohort.")
	feedback_label.custom_minimum_size.y = 52.0
	feedback_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	feedback_label.add_theme_color_override("font_color", Color("ffd08a"))
	column.add_child(feedback_label)

	var footer := HBoxContainer.new()
	pause_button = _action_button("Pause Simulation", func() -> void: pause_requested.emit())
	var reset_button := _action_button("Reset Lab", func() -> void: reset_requested.emit())
	var menu_button := _action_button("Main Menu", func() -> void: menu_requested.emit())
	footer.add_child(pause_button)
	footer.add_child(reset_button)
	footer.add_child(menu_button)
	column.add_child(footer)
	column.add_child(_hint("Mouse: click blue to select, red to inspect · 1–5 abilities · Q/W/E/D stances · X XP · F fear · T terror · Space pause · R reset · Esc menu"))


func _refresh_selection(selected: Dictionary) -> void:
	if selected.is_empty():
		selection_label.text = "SELECTED: —"
		roster_label.text = "ROSTER: —"
		return
	var alive_text := "ALIVE" if bool(selected.get("alive", false)) else "DEAD"
	if String(selected.get("kind", "")) == "champion":
		selection_label.text = "SELECTED: BLUE CHAMPION #%d · %s · HP %d/%d · XP %d · RANK %d · %s · %s" % [
			int(selected["id"]), alive_text, int(selected["health"]), int(selected["max_health"]), int(selected["xp"]), int(selected["rank"]), String(selected["stance"]).to_upper(), String(selected["status"]).to_upper(),
		]
		var toggle_text: String = " · GUARD CHANNEL ACTIVE" if not Dictionary(selected.get("active_toggles", {})).is_empty() else " · guard channel off"
		roster_label.text = "FORMATION SIZE 1 · fear resistance %d · immunity %s%s" % [int(selected["fear_resistance"]), "ON" if bool(selected["fear_immune"]) else "RULE-BASED", toggle_text]
	else:
		var members: Array = selected.get("members", [])
		var member_rows: Array[String] = []
		for member_value: Variant in members:
			var member: Dictionary = member_value
			member_rows.append("#%d %d/%d%s" % [int(member["id"]), int(member["health"]), int(member["max_health"]), "" if bool(member["alive"]) else " DOWN"])
		selection_label.text = "SELECTED: BLUE SQUAD #%d · %s · %s · %s" % [int(selected["id"]), alive_text, String(selected["stance"]).to_upper(), String(selected["status"]).to_upper()]
		roster_label.text = "CONCRETE ROSTER: " + "  ·  ".join(member_rows)


func _refresh_abilities(world: RefCounted, selected: Dictionary) -> void:
	for definition: Dictionary in _ability_definitions:
		var code: int = int(definition["code"])
		var button: Button = ability_buttons[code]
		var is_champion: bool = not selected.is_empty() and String(selected.get("kind", "")) == "champion" and bool(selected.get("alive", false))
		var rank: int = int(selected.get("rank", 0))
		var ready_tick: int = int(Dictionary(selected.get("cooldowns", {})).get(code, 0)) if not selected.is_empty() else 0
		var remaining: int = maxi(0, ready_tick - int(world.tick_index))
		var lock_text := "READY"
		if not is_champion:
			lock_text = "CHAMPION ONLY"
		elif rank < int(definition["rank_required"]):
			lock_text = "LOCKED R%d" % int(definition["rank_required"])
		elif String(definition.get("activation_mode", "instant")) == "toggle":
			var active_text: String = "ACTIVE" if bool(world.is_toggle_active(int(selected.get("id", 0)), code)) else "OFF"
			lock_text = "%s · CD %d" % [active_text, remaining] if remaining > 0 else active_text
		elif remaining > 0:
			lock_text = "COOLDOWN %d" % remaining
		button.disabled = not is_champion or rank < int(definition["rank_required"]) or remaining > 0
		button.text = "%d · %s\n%s · %s" % [code, String(definition["name"]), _friendly_mode(String(definition["target_mode"])), lock_text]


func _refresh_stances(selected: Dictionary) -> void:
	var available: bool = not selected.is_empty() and bool(selected.get("alive", false)) and int(selected.get("team", -1)) == 0
	for key: Variant in stance_buttons.keys():
		var code: String = String(key)
		var button: Button = stance_buttons[code]
		button.disabled = not available
		button.set_pressed_no_signal(available and String(selected.get("stance", "")) == code)


func _refresh_target(world: RefCounted, inspected: Dictionary) -> void:
	if inspected.is_empty():
		target_label.text = "INSPECTED TARGET: — · click a red entity"
		immunity_button.disabled = true
		return
	var kind: String = String(inspected.get("kind", "")).to_upper()
	var life: String = "ALIVE" if bool(inspected.get("alive", false)) else "DEAD"
	var health_text: String
	if kind == "CHAMPION":
		health_text = "HP %d/%d" % [int(inspected["health"]), int(inspected["max_health"])]
	else:
		health_text = "MEMBERS %d/%d" % [world.living_member_ids(int(inspected["id"])).size(), Array(inspected.get("members", [])).size()]
	var immunity_reason: String = String(world.fear_immunity_reason(int(inspected["id"])))
	var immunity_text: String = "OFF" if immunity_reason == "" else "ON (%s)" % immunity_reason.replace("_", " ")
	target_label.text = "INSPECTED TARGET: RED %s #%d · %s · %s · %s · resistance %d · immunity %s" % [
		kind, int(inspected["id"]), life, health_text, String(inspected["status"]).to_upper(), int(inspected["fear_resistance"]), immunity_text,
	]
	immunity_button.disabled = int(inspected.get("team", -1)) != 1 or not bool(inspected.get("alive", false))


func _selected_or_default_squad_available(world: RefCounted, selected: Dictionary) -> bool:
	if not selected.is_empty() and String(selected.get("kind", "")) == "squad" and bool(selected.get("alive", false)):
		return true
	for entity_id: int in world.entity_ids():
		var row: Dictionary = world.entity(entity_id)
		if int(row.get("team", -1)) == 0 and String(row.get("kind", "")) == "squad" and bool(row.get("alive", false)):
			return true
	return false


func _emit_ability(code: int) -> void:
	ability_requested.emit(code)


func _emit_stance(code: String) -> void:
	stance_requested.emit(code)


func _friendly_mode(mode: String) -> String:
	match mode:
		"self": return "SELF"
		"position": return "POSITION"
		"friendly_entity": return "FRIENDLY ENTITY"
		"hostile_entity": return "HOSTILE ENTITY"
		_: return mode.replace("_", " ").to_upper()


func _section_label(text: String) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", 16)
	label.add_theme_color_override("font_color", Color("8edbff"))
	return label


func _info_label(text: String) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", 14)
	return label


func _hint(text: String) -> Label:
	var label := _info_label(text)
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.add_theme_color_override("font_color", Color("91aab8"))
	return label


func _separator() -> HSeparator:
	var separator := HSeparator.new()
	separator.modulate = Color("4b7184")
	return separator


func _action_button(text: String, callback: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.pressed.connect(callback)
	return button


func _panel_style(background: Color, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = background
	style.border_color = border
	style.set_border_width_all(1)
	style.corner_radius_top_left = 8
	style.corner_radius_top_right = 8
	style.corner_radius_bottom_left = 8
	style.corner_radius_bottom_right = 8
	style.content_margin_left = 14.0
	style.content_margin_right = 14.0
	style.content_margin_top = 10.0
	style.content_margin_bottom = 10.0
	return style
