class_name Stage5Hud
extends CanvasLayer
## Data-driven Stage 5 spellbook controls and authoritative state readout.

signal power_requested(code: int)
signal experience_requested(amount: int)
signal advance_requested
signal pause_requested
signal reset_requested
signal menu_requested

var power_buttons: Dictionary = {}
var tier_columns: Dictionary = {}
var root: Control
var sim_label: Label
var points_label: Label
var weather_label: Label
var targeting_label: Label
var entity_label: Label
var feedback_label: Label
var power_grid: GridContainer
var pause_button: Button
var xp_button: Button
var _definitions: Array[Dictionary] = []


func _ready() -> void:
	_build_interface()


func configure(definitions: Array[Dictionary]) -> void:
	_definitions = definitions.duplicate(true)
	_definitions.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return int(left["code"]) < int(right["code"]))
	for child: Node in power_grid.get_children():
		child.queue_free()
	power_buttons.clear()
	tier_columns.clear()
	for tier: int in range(1, 5):
		var tier_column := VBoxContainer.new()
		tier_column.custom_minimum_size.x = 150
		tier_column.add_theme_constant_override("separation", 6)
		var minimum_spent: int = 0
		for definition: Dictionary in _definitions:
			if int(definition["tier"]) == tier:
				minimum_spent = int(definition.get("minimumSpent", 0))
				break
		var tier_label := _section_label("TIER %d\nSPEND %d+" % [tier, minimum_spent])
		tier_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		tier_column.add_child(tier_label)
		power_grid.add_child(tier_column)
		tier_columns[tier] = tier_column
	for definition: Dictionary in _definitions:
		var code: int = int(definition["code"])
		var button := Button.new()
		button.custom_minimum_size = Vector2(150, 88)
		button.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
		button.tooltip_text = "Tier %d | Spend gate %d | Cost %d | Cooldown %d ticks | Target %s\n%s" % [
			int(definition["tier"]),
			int(definition.get("minimumSpent", 0)),
			int(definition["pointCost"]),
			int(definition["cooldownTicks"]),
			_friendly_mode(String(definition["targetMode"])),
			_effect_summary(definition),
		]
		button.pressed.connect(_emit_power.bind(code))
		(tier_columns[int(definition["tier"])] as VBoxContainer).add_child(button)
		power_buttons[code] = button


func refresh(world: RefCounted, armed_power_code: int, paused: bool) -> void:
	if world == null:
		return
	sim_label.text = "TICK %05d  |  HASH %s  |  %s" % [int(world.tick_index), String(world.state_hash_text()), "PAUSED" if paused else "LIVE 10 TICKS/S"]
	var state: Dictionary = world.team_state(0)
	points_label.text = "BLUE SPELLBOOK  |  AVAILABLE %d  |  SPENT %d  |  EARNED %d  |  XP REMAINDER %d/100" % [
		int(state.get("available_points", 0)), int(state.get("spent_points", 0)), int(state.get("earned_points", 0)), int(state.get("experience_remainder", 0)),
	]
	if Dictionary(world.weather).is_empty():
		weather_label.text = "GLOBAL WEATHER: clear"
	else:
		weather_label.text = "GLOBAL WEATHER: %s | OWNER BLUE | %d TICKS LEFT | UNIT %d / BUILDING %d DAMAGE PER TICK" % [
			String(world.weather.get("code", "weather")).to_upper(),
			int(world.weather.get("remaining_ticks", 0)),
			int(world.weather.get("unit_damage_per_tick", 0)),
			int(world.weather.get("building_damage_per_tick", 0)),
		]
	targeting_label.text = "TARGETING: click a %s" % _friendly_mode(String(world.power_definition(armed_power_code).get("targetMode", ""))) if armed_power_code != 0 else "TARGETING: none | locked button tries unlock; unlocked button arms or casts"
	_refresh_power_buttons(world, state, armed_power_code)
	_refresh_entities(world)
	pause_button.text = "Resume Simulation" if paused else "Pause Simulation"


func set_feedback(message: String, success: bool = false) -> void:
	feedback_label.text = message
	feedback_label.modulate = Color("80e7a5") if success else Color("ffd08a")


func _build_interface() -> void:
	root = Control.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)

	var top := PanelContainer.new()
	top.set_anchors_preset(Control.PRESET_TOP_WIDE)
	top.offset_left = 20
	top.offset_top = 16
	top.offset_right = -20
	top.offset_bottom = 140
	top.mouse_filter = Control.MOUSE_FILTER_IGNORE
	top.add_theme_stylebox_override("panel", _panel_style(Color("0a1621ef"), Color("456f86")))
	root.add_child(top)
	var header := VBoxContainer.new()
	header.add_theme_constant_override("separation", 2)
	top.add_child(header)
	var title := Label.new()
	title.text = "STAGE 5  |  SPELLBOOK AND WORLD POWER LAB"
	title.add_theme_font_size_override("font_size", 25)
	title.add_theme_color_override("font_color", Color("92dcff"))
	header.add_child(title)
	var subtitle := Label.new()
	subtitle.text = "Legal-safe neutral primitives | external powers.json | deterministic GDScript proof authority"
	subtitle.add_theme_color_override("font_color", Color("adc4d0"))
	header.add_child(subtitle)
	sim_label = _info_label("")
	sim_label.add_theme_color_override("font_color", Color("f7e2a0"))
	header.add_child(sim_label)
	points_label = _info_label("")
	points_label.add_theme_color_override("font_color", Color("a4efbd"))
	header.add_child(points_label)

	var side := PanelContainer.new()
	side.set_anchors_preset(Control.PRESET_RIGHT_WIDE)
	side.anchor_left = 1.0
	side.offset_left = -700
	side.offset_top = 152
	side.offset_right = -20
	side.offset_bottom = -18
	side.mouse_filter = Control.MOUSE_FILTER_STOP
	side.add_theme_stylebox_override("panel", _panel_style(Color("0b1723f5"), Color("3e6478")))
	root.add_child(side)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	side.add_child(scroll)
	var column := VBoxContainer.new()
	column.custom_minimum_size.x = 640
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 9)
	scroll.add_child(column)

	weather_label = _info_label("GLOBAL WEATHER: clear")
	weather_label.add_theme_color_override("font_color", Color("d5b8ff"))
	column.add_child(weather_label)
	targeting_label = _info_label("TARGETING: none")
	targeting_label.add_theme_color_override("font_color", Color("ffd695"))
	targeting_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(targeting_label)
	column.add_child(_separator())
	column.add_child(_section_label("TIER TREE | GENERATED FROM powers.json"))
	power_grid = GridContainer.new()
	power_grid.columns = 4
	power_grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	power_grid.add_theme_constant_override("h_separation", 6)
	power_grid.add_theme_constant_override("v_separation", 8)
	column.add_child(power_grid)
	column.add_child(_hint("Unlocks require prerequisites, tier spending, and points. Unlocked targeted powers arm the board; global powers cast immediately."))

	column.add_child(_section_label("SPELLBOOK PROGRESSION AND CLOCK"))
	var progression := GridContainer.new()
	progression.columns = 2
	progression.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	xp_button = _action_button("Grant Blue +100 Spellbook XP", func() -> void: experience_requested.emit(100))
	var large_xp := _action_button("Grant Blue +500 Spellbook XP", func() -> void: experience_requested.emit(500))
	var advance_button := _action_button("Advance One Tick", func() -> void: advance_requested.emit())
	pause_button = _action_button("Pause Simulation", func() -> void: pause_requested.emit())
	progression.add_child(xp_button)
	progression.add_child(large_xp)
	progression.add_child(advance_button)
	progression.add_child(pause_button)
	column.add_child(progression)

	entity_label = _info_label("ENTITIES")
	entity_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	entity_label.custom_minimum_size.y = 90
	column.add_child(entity_label)
	feedback_label = _info_label("Ready")
	feedback_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	feedback_label.custom_minimum_size.y = 50
	feedback_label.add_theme_color_override("font_color", Color("ffd08a"))
	column.add_child(feedback_label)

	var footer := HBoxContainer.new()
	footer.add_child(_action_button("Reset Lab", func() -> void: reset_requested.emit()))
	footer.add_child(_action_button("Main Menu", func() -> void: menu_requested.emit()))
	column.add_child(footer)
	column.add_child(_hint("Keys: 1-7 power | X +100 XP | Space pause | R reset | Esc menu | RMB cancels targeting"))


func _refresh_power_buttons(world: RefCounted, state: Dictionary, armed_power_code: int) -> void:
	for definition: Dictionary in _definitions:
		var code: int = int(definition["code"])
		var button: Button = power_buttons[code]
		var unlocked: bool = bool(world.is_power_unlocked(0, code))
		var status: String
		if unlocked:
			var cooldown: int = int(world.cooldown_remaining(0, code))
			status = "CAST READY" if cooldown == 0 else "COOLDOWN %d" % cooldown
		else:
			var prerequisites: Array[String] = []
			for raw_code: Variant in definition.get("prerequisites", []):
				prerequisites.append(str(int(raw_code)))
			status = "UNLOCK %d PT | REQ %s" % [int(definition["pointCost"]), "none" if prerequisites.is_empty() else ",".join(prerequisites)]
		if code == armed_power_code:
			status = "ARMED | CLICK BOARD"
		button.disabled = false
		button.text = "%d | %s\n%s\n%s" % [code, String(definition["displayName"]), _friendly_mode(String(definition["targetMode"])), status]
		button.modulate = Color("fff0bc") if code == armed_power_code else Color.WHITE
	var available: int = int(state.get("available_points", 0))
	if available == 0 and Array(state.get("unlocked", [])).size() < _definitions.size():
		xp_button.tooltip_text = "Earn 1 power point per 100 XP. You currently need more points for remaining unlocks."
	else:
		xp_button.tooltip_text = "Earn 1 deterministic power point per 100 XP."


func _refresh_entities(world: RefCounted) -> void:
	var rows: Array[String] = []
	for entity_id: int in world.entity_ids():
		var row: Dictionary = world.entity(entity_id)
		rows.append("%s %s #%d @ %s HP %d/%d" % [
			"BLUE" if int(row["team"]) == 0 else "RED",
			String(row["kind"]).to_upper(),
			entity_id,
			str(Vector2i(row["position"])),
			int(row["health"]),
			int(row["max_health"]),
		])
	entity_label.text = "LIVE AUTHORITATIVE ENTITIES\n" + "\n".join(rows)


func _emit_power(code: int) -> void:
	power_requested.emit(code)


func _friendly_mode(mode: String) -> String:
	return mode.replace("_", " ").to_upper() if mode != "" else "TARGET"


func _effect_summary(definition: Dictionary) -> String:
	var rows: Array[String] = []
	for raw_effect: Variant in definition.get("effects", []):
		var effect: Dictionary = raw_effect
		rows.append(String(effect.get("type", "effect")).replace("_", " ").capitalize())
	return ", ".join(rows)


func _info_label(value: String) -> Label:
	var label := Label.new()
	label.text = value
	label.add_theme_font_size_override("font_size", 15)
	label.add_theme_color_override("font_color", Color("d4e0e7"))
	return label


func _section_label(value: String) -> Label:
	var label := _info_label(value)
	label.add_theme_font_size_override("font_size", 17)
	label.add_theme_color_override("font_color", Color("8fdcff"))
	return label


func _hint(value: String) -> Label:
	var label := _info_label(value)
	label.add_theme_font_size_override("font_size", 13)
	label.add_theme_color_override("font_color", Color("8faab8"))
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	return label


func _action_button(value: String, callback: Callable) -> Button:
	var button := Button.new()
	button.text = value
	button.custom_minimum_size = Vector2(0, 38)
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.pressed.connect(callback)
	return button


func _separator() -> HSeparator:
	return HSeparator.new()


func _panel_style(background: Color, border: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = background
	style.border_color = border
	style.set_border_width_all(2)
	style.corner_radius_top_left = 8
	style.corner_radius_top_right = 8
	style.corner_radius_bottom_left = 8
	style.corner_radius_bottom_right = 8
	style.content_margin_left = 14
	style.content_margin_right = 14
	style.content_margin_top = 10
	style.content_margin_bottom = 10
	return style
