class_name Stage6Hud
extends CanvasLayer
## Data-driven controls and readout for faction, roster, research, and combat.

signal faction_requested(faction_id: String)
signal roster_requested(unit_id: String)
signal research_requested(upgrade_id: String)
signal attack_requested
signal step_requested
signal pause_requested
signal performance_probe_requested
signal reset_requested
signal menu_requested

var faction_buttons: Dictionary = {}
var roster_buttons: Dictionary = {}
var upgrade_buttons: Dictionary = {}
var title_label: Label
var sim_label: Label
var faction_label: Label
var selection_label: Label
var matrix_label: Label
var research_label: Label
var coverage_label: Label
var feedback_label: Label
var faction_row: HBoxContainer
var roster_box: VBoxContainer
var upgrade_box: VBoxContainer
var pause_button: Button
var attack_button: Button
var _active_faction_id: String = ""


func _ready() -> void:
	_build_interface()


func configure(catalog: RefCounted) -> void:
	_clear_children(faction_row)
	faction_buttons.clear()
	for faction_id: String in catalog.faction_ids():
		var definition: Dictionary = catalog.faction(faction_id)
		var button := Button.new()
		button.text = String(definition["displayName"])
		button.tooltip_text = "Inspect this legal-safe faction roster"
		button.pressed.connect(func() -> void: faction_requested.emit(faction_id))
		faction_row.add_child(button)
		faction_buttons[faction_id] = button
	_active_faction_id = ""


func refresh(world: RefCounted, active_faction_id: String, attacker_id: int, target_id: int, paused: bool) -> void:
	if world == null:
		return
	if active_faction_id != _active_faction_id:
		_active_faction_id = active_faction_id
		_rebuild_faction_context(world.catalog, active_faction_id)
	var state: Dictionary = world.faction_states.get(active_faction_id, {})
	var faction: Dictionary = world.catalog.faction(active_faction_id)
	sim_label.text = "TICK %04d  |  HASH %s  |  %s" % [int(world.tick_index), String(world.state_hash_text()), "PAUSED" if paused else "LIVE 8 TICKS/S"]
	faction_label.text = "%s  |  SUPPLIES %d  |  UPGRADES %s" % [String(faction.get("displayName", active_faction_id)), int(state.get("resources", 0)), ", ".join(PackedStringArray(state.get("completed_upgrades", []))) if not Array(state.get("completed_upgrades", [])).is_empty() else "none"]
	var attacker: Dictionary = world.entity(attacker_id)
	var target: Dictionary = world.entity(target_id)
	selection_label.text = "ATTACKER: %s #%d  ->  TARGET: %s #%d" % [_entity_name(world, attacker), attacker_id, _entity_name(world, target), target_id]
	var preview: Dictionary = {}
	if not attacker.is_empty() and not target.is_empty():
		preview = world.damage_preview(String(attacker["unit_id"]), String(target["unit_id"]), String(attacker["faction_id"]), String(target["faction_id"]))
	matrix_label.text = "MATRIX: %s vs %s  |  damage %d  |  ratio %d/%d  |  attack +%d  armor +%d permille" % [String(preview.get("damage_type", "—")), String(preview.get("armor_class", "—")), int(preview.get("damage", 0)), int(preview.get("matrix_numerator", 0)), int(preview.get("matrix_denominator", 1)), int(preview.get("damage_bonus_permille", 0)), int(preview.get("armor_bonus_permille", 0))]
	var research: Dictionary = state.get("research", {})
	research_label.text = "RESEARCH: idle" if research.is_empty() else "RESEARCH: %s completes at tick %d" % [String(research["upgrade_id"]), int(research["complete_tick"])]
	var coverage: Dictionary = world.catalog.art_coverage()
	coverage_label.text = "ART RESOLUTION: %d/%d legal-safe generated primitives  |  missing %d" % [int(coverage["resolved"]), int(coverage["total"]), Array(coverage["missing"]).size()]
	pause_button.text = "Resume" if paused else "Pause"
	attack_button.disabled = attacker.is_empty() or target.is_empty() or not bool(attacker.get("alive", false)) or not bool(target.get("alive", false))
	for faction_id: String in faction_buttons:
		(faction_buttons[faction_id] as Button).disabled = faction_id == active_faction_id


func set_feedback(message: String, success: bool = false) -> void:
	feedback_label.text = message
	feedback_label.modulate = Color("83e6a5") if success else Color("ffd08a")


func _rebuild_faction_context(catalog: RefCounted, faction_id: String) -> void:
	_clear_children(roster_box)
	_clear_children(upgrade_box)
	roster_buttons.clear()
	upgrade_buttons.clear()
	for row: Dictionary in catalog.roster_for(faction_id):
		var unit_id := String(row["unitId"])
		var button := Button.new()
		button.text = "%s  |  %s / %s" % [String(row["displayName"]), String(row["damageType"]), String(row["armorClass"])]
		button.pressed.connect(func() -> void: roster_requested.emit(unit_id))
		roster_box.add_child(button)
		roster_buttons[unit_id] = button
	var faction: Dictionary = catalog.faction(faction_id)
	for raw_upgrade_id: Variant in Array(faction.get("upgradeIds", [])):
		var upgrade_id := String(raw_upgrade_id)
		var definition: Dictionary = catalog.upgrade(upgrade_id)
		var button := Button.new()
		button.text = "%s  |  %d supplies / %d ticks" % [String(definition["displayName"]), int(definition["cost"]), int(definition["researchTicks"])]
		button.pressed.connect(func() -> void: research_requested.emit(upgrade_id))
		upgrade_box.add_child(button)
		upgrade_buttons[upgrade_id] = button


func _build_interface() -> void:
	var root := Control.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)
	var top := PanelContainer.new()
	top.position = Vector2(24, 18)
	top.size = Vector2(1872, 125)
	top.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.add_child(top)
	var header := VBoxContainer.new()
	top.add_child(header)
	title_label = Label.new()
	title_label.text = "STAGE 6  |  FACTIONS, RESEARCH & DAMAGE MATRIX"
	title_label.add_theme_font_size_override("font_size", 25)
	header.add_child(title_label)
	sim_label = Label.new()
	header.add_child(sim_label)
	faction_row = HBoxContainer.new()
	faction_row.add_theme_constant_override("separation", 10)
	header.add_child(faction_row)

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
	faction_label = _info_label(18, Color("8edbff"))
	column.add_child(faction_label)
	selection_label = _info_label(16, Color("f4dd98"))
	column.add_child(selection_label)
	matrix_label = _info_label(15, Color("e5b9ff"))
	matrix_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(matrix_label)
	research_label = _info_label(15, Color("a9d7ee"))
	column.add_child(research_label)
	coverage_label = _info_label(15, Color("8fe5ad"))
	column.add_child(coverage_label)
	column.add_child(HSeparator.new())
	column.add_child(_section("DEPLOYED ROSTER  |  select attacker"))
	roster_box = VBoxContainer.new()
	column.add_child(roster_box)
	column.add_child(_section("FACTION RESEARCH"))
	upgrade_box = VBoxContainer.new()
	column.add_child(upgrade_box)
	var action_row := HBoxContainer.new()
	action_row.add_theme_constant_override("separation", 8)
	column.add_child(action_row)
	attack_button = _button("Resolve Attack", func() -> void: attack_requested.emit())
	action_row.add_child(attack_button)
	action_row.add_child(_button("Step 1 Tick", func() -> void: step_requested.emit()))
	action_row.add_child(_button("80-Battalion Probe", func() -> void: performance_probe_requested.emit()))
	pause_button = _button("Pause", func() -> void: pause_requested.emit())
	action_row.add_child(pause_button)
	action_row.add_child(_button("Reset", func() -> void: reset_requested.emit()))
	action_row.add_child(_button("Menu", func() -> void: menu_requested.emit()))
	feedback_label = _info_label(15, Color("ffd08a"))
	feedback_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	feedback_label.custom_minimum_size.y = 80
	column.add_child(feedback_label)


func _button(text: String, callback: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.pressed.connect(callback)
	return button


func _section(text: String) -> Label:
	var label := _info_label(16, Color("8edbff"))
	label.text = text
	return label


func _info_label(size: int, color: Color) -> Label:
	var label := Label.new()
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", color)
	return label


func _entity_name(world: RefCounted, row: Dictionary) -> String:
	return "—" if row.is_empty() else String(world.catalog.unit(String(row["unit_id"])).get("displayName", row["unit_id"]))


func _clear_children(node: Node) -> void:
	for child: Node in node.get_children():
		node.remove_child(child)
		child.queue_free()
