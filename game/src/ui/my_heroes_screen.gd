extends Panel
## MY HEROES: the Create-a-Hero front end.
##
## Layout follows the retail SELECT HERO shell (see reference/create a hero menu):
## dark full-bleed chrome, CREATE-A-HERO banner, left Name/Type roster, right
## tabbed detail (APPEARANCE / POWERS / STATS / AWARDS), DELETE / NEW HERO /
## MAIN MENU. Only the lanes we can actually drive are interactive; the rest
## state the gap honestly rather than painting dead controls.
##
## Live lane end to end: name a hero, pick class and subclass, spend the class's
## attribute points, save. A saved hero is purchasable from the fortress hero
## roster in skirmish because `CahHeroes.roster_document` emits the same
## `openbfme.playable-unit-runtime` schema every other hero uses.
##
## WHAT IT DOES NOT DO, and says so on the screen rather than in a changelog:
## armour and colour customisation, and picking a power loadout. Retail authors
## seven APPEARANCE bling groups and a per-class power tree; the importer does
## not compile either yet, so offering the controls would be offering nothing.
##
## WHY IT CAN BE EMPTY. The class table is content, not code: it is compiled out
## of retail INI by `openbfme-import compile-cah-system` and shipped in a content
## pack as `cah.system`. Until a mounted pack carries one there is nothing to
## build a hero out of, and this screen says exactly that, names the command that
## produces it, and stays otherwise inert.

const CahHeroes = preload("res://src/content/cah_heroes.gd")
const ThemeScript = preload("res://src/ui/openbfme_theme.gd")

signal back_requested

## Emitted after any create or delete, so a host that caches rosters can drop it.
signal roster_changed

const TAB_APPEARANCE := 0
const TAB_POWERS := 1
const TAB_STATS := 2
const TAB_AWARDS := 3

var _system: Dictionary = {}
var _profiles: Array[Dictionary] = []
var _selected_class := 0
var _selected_sub := 0
## Working loadout for the hero being built: group name -> 1-based step.
var _attributes: Dictionary = {}
var _active_tab := TAB_STATS
var _creating := true

var name_edit: LineEdit
var class_option: OptionButton
var sub_option: OptionButton
var budget_label: Label
var status_label: Label
var save_button: Button
var back_button: Button
var hero_list: ItemList
var delete_button: Button
var new_hero_button: Button
var attribute_rows: VBoxContainer
var limitations_label: Label
var title_label: Label
var section_label: Label
var tab_buttons: Array[Button] = []
var tab_body: Control
var editor_panel: VBoxContainer
var gap_panel: Label
var stats_summary: Label


var _attribute_sliders: Dictionary = {}
var _attribute_values: Dictionary = {}


func _ready() -> void:
	theme = ThemeScript.create_theme(null)
	_ensure_built()
	refresh()


func configure(system: Dictionary) -> void:
	## Hand the screen the mounted class table. Called before `refresh()` by the
	## shell, and directly by the headless runner so it can drive the whole
	## screen without a pack on disk.
	##
	## The shell can call this on the same frame as `add_child`, which is BEFORE
	## `_ready` runs. Building here keeps NEW HERO / MY HEROES from dying on a
	## null status_label the first time the button is pressed.
	_system = system if CahHeroes.system_is_valid(system) else {}
	_ensure_built()
	if is_inside_tree():
		refresh()


func _ensure_built() -> void:
	if status_label != null:
		return
	if theme == null:
		theme = ThemeScript.create_theme(null)
	_build()


func system_available() -> bool:
	return not _system.is_empty()


func refresh() -> void:
	_profiles = CahHeroes.load_profiles()
	_reload_hero_list()
	if _system.is_empty():
		_set_editor_enabled(false)
		_creating = false
		status_label.text = (
			"No mounted content pack provides the Create-a-Hero class table. "
			+ "Compile one with `openbfme-import compile-cah-system --assets-root <effective-assets>` "
			+ "and publish it into a pack as `cah.system`."
		)
		_show_tab(_active_tab)
		return
	_set_editor_enabled(true)
	_reload_classes()
	_reload_sub_classes()
	_reset_attributes()
	_rebuild_attribute_rows()
	_update_budget()
	_update_stats_summary()
	_show_tab(_active_tab)


func create_hero(hero_name: String) -> Array[String]:
	## Save the hero currently being edited. Returns [] on success, or every
	## reason it was refused. Public because the runner drives creation directly.
	if _system.is_empty():
		return ["no Create-a-Hero class table is mounted"]
	var profile := CahHeroes.new_profile(_system, hero_name, _selected_class, _selected_sub)
	profile["attributes"] = _attributes.duplicate()
	var refusals := CahHeroes.validate_profile(_system, profile)
	if not refusals.is_empty():
		return refusals
	if _profiles.size() >= CahHeroes.MAX_PROFILES:
		return ["there are already %d saved heroes" % CahHeroes.MAX_PROFILES]
	var error := CahHeroes.save_profile(profile)
	if error != "":
		return [error]
	refresh()
	roster_changed.emit()
	return []


func delete_selected() -> bool:
	var index := _selected_hero_index()
	if index < 0:
		return false
	var hero_id := String(_profiles[index].get("heroId", ""))
	if not CahHeroes.delete_profile(hero_id):
		return false
	refresh()
	roster_changed.emit()
	return true


func set_class_selection(class_index: int, sub_index: int) -> void:
	_selected_class = class_index
	_selected_sub = sub_index
	_reload_sub_classes()
	_reset_attributes()
	_rebuild_attribute_rows()
	_update_budget()
	_update_stats_summary()


func set_attribute(group_name: String, step: int) -> void:
	_attributes[group_name] = step
	if _attribute_sliders.has(group_name):
		(_attribute_sliders[group_name] as HSlider).set_value_no_signal(float(step))
	_update_budget()
	_update_stats_summary()


func working_attributes() -> Dictionary:
	return _attributes.duplicate()


func saved_profiles() -> Array[Dictionary]:
	return _profiles.duplicate(true)


func spend_and_budget() -> Vector2i:
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	return Vector2i(
		CahHeroes.attribute_spend(sub_row, _attributes),
		CahHeroes.attribute_budget(sub_row)
	)


func _build() -> void:
	## Full-bleed dark shell matching the retail SELECT HERO proportions as
	## closely as we can without shipping EA art.
	mouse_filter = Control.MOUSE_FILTER_STOP

	var root := VBoxContainer.new()
	root.name = "Root"
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.add_theme_constant_override("separation", 10)
	for side in ["margin_left", "margin_right", "margin_top", "margin_bottom"]:
		# MarginContainer would also work; offsets on the root keep hit-tests simple.
		pass
	root.offset_left = 36
	root.offset_top = 28
	root.offset_right = -36
	root.offset_bottom = -28
	add_child(root)
	# Godot 4: Control offsets need anchors when not using layout containers fully.
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.offset_left = 36
	root.offset_top = 28
	root.offset_right = -36
	root.offset_bottom = -28

	title_label = Label.new()
	title_label.name = "Title"
	title_label.text = "CREATE-A-HERO"
	title_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title_label.add_theme_font_size_override("font_size", 28)
	title_label.add_theme_color_override("font_color", Color(0.86, 0.90, 0.94, 0.95))
	root.add_child(title_label)

	section_label = Label.new()
	section_label.name = "Section"
	section_label.text = "SELECT HERO"
	section_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	section_label.add_theme_font_size_override("font_size", 18)
	section_label.add_theme_color_override("font_color", Color(0.78, 0.84, 0.90, 0.92))
	root.add_child(section_label)

	var body := HBoxContainer.new()
	body.name = "Body"
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 18)
	root.add_child(body)

	# ---- LEFT: roster -------------------------------------------------------
	var left := VBoxContainer.new()
	left.name = "SavedHeroes"
	left.custom_minimum_size = Vector2(360, 0)
	left.size_flags_vertical = Control.SIZE_EXPAND_FILL
	left.add_theme_constant_override("separation", 8)
	body.add_child(left)

	var list_header := HBoxContainer.new()
	list_header.name = "ListHeader"
	left.add_child(list_header)
	var name_header := Label.new()
	name_header.text = "Name"
	name_header.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_header.add_theme_color_override("font_color", Color(0.70, 0.78, 0.86, 0.9))
	list_header.add_child(name_header)
	var type_header := Label.new()
	type_header.text = "Type"
	type_header.custom_minimum_size = Vector2(150, 0)
	type_header.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	type_header.add_theme_color_override("font_color", Color(0.70, 0.78, 0.86, 0.9))
	list_header.add_child(type_header)

	hero_list = ItemList.new()
	hero_list.name = "HeroList"
	hero_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	hero_list.allow_reselect = true
	hero_list.item_selected.connect(_on_hero_list_selected)
	left.add_child(hero_list)

	var left_buttons := HBoxContainer.new()
	left_buttons.name = "LeftButtons"
	left_buttons.add_theme_constant_override("separation", 10)
	left.add_child(left_buttons)
	delete_button = Button.new()
	delete_button.name = "DeleteHero"
	delete_button.text = "DELETE HERO"
	delete_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	delete_button.pressed.connect(func() -> void: delete_selected())
	left_buttons.add_child(delete_button)
	new_hero_button = Button.new()
	new_hero_button.name = "NewHero"
	new_hero_button.text = "NEW HERO"
	new_hero_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	new_hero_button.pressed.connect(_on_new_hero_pressed)
	left_buttons.add_child(new_hero_button)

	# ---- RIGHT: tabs + detail -----------------------------------------------
	var right := VBoxContainer.new()
	right.name = "Detail"
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 10)
	body.add_child(right)

	var tabs := HBoxContainer.new()
	tabs.name = "Tabs"
	tabs.add_theme_constant_override("separation", 6)
	right.add_child(tabs)
	tab_buttons.clear()
	for entry in [
		{"id": TAB_APPEARANCE, "label": "APPEARANCE"},
		{"id": TAB_POWERS, "label": "POWERS"},
		{"id": TAB_STATS, "label": "STATS"},
		{"id": TAB_AWARDS, "label": "AWARDS"},
	]:
		var tab := Button.new()
		tab.name = "Tab_%s" % String(entry["label"])
		tab.text = String(entry["label"])
		tab.toggle_mode = true
		tab.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var tab_id := int(entry["id"])
		tab.pressed.connect(func() -> void: _show_tab(tab_id))
		tabs.add_child(tab)
		tab_buttons.append(tab)

	tab_body = Control.new()
	tab_body.name = "TabBody"
	tab_body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	tab_body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_child(tab_body)

	editor_panel = VBoxContainer.new()
	editor_panel.name = "Editor"
	editor_panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	editor_panel.add_theme_constant_override("separation", 8)
	tab_body.add_child(editor_panel)

	var editor_heading := Label.new()
	editor_heading.text = "NEW HERO"
	editor_heading.add_theme_font_size_override("font_size", 16)
	editor_panel.add_child(editor_heading)

	name_edit = LineEdit.new()
	name_edit.name = "HeroName"
	name_edit.placeholder_text = "Hero name"
	name_edit.max_length = CahHeroes.MAX_NAME_LENGTH
	editor_panel.add_child(name_edit)

	class_option = OptionButton.new()
	class_option.name = "ClassOption"
	class_option.item_selected.connect(
		func(index: int) -> void: set_class_selection(int(class_option.get_item_id(index)), 0)
	)
	editor_panel.add_child(class_option)

	sub_option = OptionButton.new()
	sub_option.name = "SubClassOption"
	sub_option.item_selected.connect(
		func(index: int) -> void: set_class_selection(_selected_class, int(sub_option.get_item_id(index)))
	)
	editor_panel.add_child(sub_option)

	budget_label = Label.new()
	budget_label.name = "BudgetLabel"
	editor_panel.add_child(budget_label)

	attribute_rows = VBoxContainer.new()
	attribute_rows.name = "AttributeRows"
	attribute_rows.size_flags_vertical = Control.SIZE_EXPAND_FILL
	editor_panel.add_child(attribute_rows)

	stats_summary = Label.new()
	stats_summary.name = "StatsSummary"
	stats_summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	editor_panel.add_child(stats_summary)

	limitations_label = Label.new()
	limitations_label.name = "Limitations"
	limitations_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	limitations_label.text = (
		"Armour, colour and power customisation are not available yet. "
		+ "A created hero uses its class's authored appearance and command set."
	)
	editor_panel.add_child(limitations_label)

	save_button = Button.new()
	save_button.name = "SaveHero"
	save_button.text = "SAVE HERO"
	save_button.pressed.connect(_on_save_pressed)
	editor_panel.add_child(save_button)

	gap_panel = Label.new()
	gap_panel.name = "GapPanel"
	gap_panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	gap_panel.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	gap_panel.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	gap_panel.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	gap_panel.visible = false
	tab_body.add_child(gap_panel)

	status_label = Label.new()
	status_label.name = "Status"
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	right.add_child(status_label)

	# ---- FOOTER -------------------------------------------------------------
	var footer := HBoxContainer.new()
	footer.name = "Footer"
	footer.add_theme_constant_override("separation", 12)
	root.add_child(footer)
	back_button = Button.new()
	back_button.name = "Back"
	back_button.text = "MAIN MENU"
	back_button.custom_minimum_size = Vector2(180, 40)
	back_button.pressed.connect(func() -> void: back_requested.emit())
	footer.add_child(back_button)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	footer.add_child(spacer)


func _on_new_hero_pressed() -> void:
	_creating = true
	section_label.text = "CREATE HERO"
	name_edit.text = ""
	_active_tab = TAB_STATS
	_show_tab(TAB_STATS)
	if not _system.is_empty():
		_set_editor_enabled(true)
		_reset_attributes()
		_rebuild_attribute_rows()
		_update_budget()
		_update_stats_summary()
		status_label.text = "Name the hero, spend the attribute budget exactly, then save."


func _on_hero_list_selected(index: int) -> void:
	if index < 0 or index >= _profiles.size():
		return
	_creating = false
	section_label.text = "SELECT HERO"
	var profile := _profiles[index]
	name_edit.text = String(profile.get("name", ""))
	_selected_class = int(profile.get("classIndex", 0))
	_selected_sub = int(profile.get("subClassIndex", 0))
	_attributes = (profile.get("attributes", {}) as Dictionary).duplicate()
	_reload_classes()
	_reload_sub_classes()
	_rebuild_attribute_rows()
	_update_budget()
	_update_stats_summary()
	_active_tab = TAB_STATS
	_show_tab(TAB_STATS)
	status_label.text = "Saved hero. Create a new one with NEW HERO, or delete this entry."


func _show_tab(tab_id: int) -> void:
	_active_tab = tab_id
	for i in range(tab_buttons.size()):
		tab_buttons[i].button_pressed = (i == tab_id)
	match tab_id:
		TAB_STATS:
			editor_panel.visible = true
			gap_panel.visible = false
		TAB_APPEARANCE:
			editor_panel.visible = false
			gap_panel.visible = true
			gap_panel.text = (
				"Appearance customisation is not compiled yet.\n"
				+ "Retail authors armour and colour groups per class; this build "
				+ "uses each class's default look until that lane ships."
			)
		TAB_POWERS:
			editor_panel.visible = false
			gap_panel.visible = true
			gap_panel.text = (
				"Power trees are not compiled yet.\n"
				+ "Retail's CREATE-A-HERO power grid (see reference/create a hero menu) "
				+ "needs a power-set converter before this tab can be interactive."
			)
		TAB_AWARDS:
			editor_panel.visible = false
			gap_panel.visible = true
			gap_panel.text = (
				"Awards are not tracked yet.\n"
				+ "This tab will list campaign and skirmish medals once the award "
				+ "document is mounted beside cah.system."
			)


func _on_save_pressed() -> void:
	var refusals := create_hero(name_edit.text)
	if refusals.is_empty():
		status_label.text = "Saved."
		name_edit.text = ""
		_creating = false
		section_label.text = "SELECT HERO"
	else:
		status_label.text = "Not saved: %s." % ", ".join(refusals)


func _set_editor_enabled(enabled: bool) -> void:
	## `LineEdit` has no `disabled`, so the text field is gated with `editable`
	## and only the buttons take `disabled`. Assigning `disabled` to everything
	## would throw on the one control that is not a button.
	if name_edit != null:
		name_edit.editable = enabled
	for button in [class_option, sub_option, save_button, delete_button, new_hero_button]:
		if button != null:
			button.disabled = not enabled
	if new_hero_button != null and _system.is_empty():
		new_hero_button.disabled = true
	if delete_button != null and _system.is_empty():
		delete_button.disabled = true


func _reload_hero_list() -> void:
	hero_list.clear()
	for profile in _profiles:
		var refusals: Array[String] = []
		if _system.is_empty():
			refusals.append("no class table is mounted")
		else:
			refusals = CahHeroes.validate_profile(_system, profile)
		var hero_name := String(profile.get("name", "?"))
		var type_name := _profile_type_label(profile)
		var label := "%s    %s" % [hero_name, type_name]
		if not refusals.is_empty():
			label += "  (unavailable: %s)" % refusals[0]
		hero_list.add_item(label)


func _profile_type_label(profile: Dictionary) -> String:
	if _system.is_empty():
		return "?"
	var class_row := CahHeroes.class_row(_system, int(profile.get("classIndex", -1)))
	var sub_row := CahHeroes.sub_class_row(
		_system, int(profile.get("classIndex", -1)), int(profile.get("subClassIndex", -1))
	)
	var class_label := _readable(String(class_row.get("nameStringId", "")))
	var sub_label := _readable(String(sub_row.get("nameStringId", "")))
	if sub_label != "?" and sub_label != "":
		return sub_label
	return class_label


func _selected_hero_index() -> int:
	var selected := hero_list.get_selected_items()
	if selected.is_empty():
		return -1
	var index := int(selected[0])
	return index if index >= 0 and index < _profiles.size() else -1


func _reload_classes() -> void:
	class_option.clear()
	var registration: Dictionary = _system.get("registration", {}) as Dictionary
	for class_value in (registration.get("classes", []) as Array):
		var row := class_value as Dictionary
		var index := int(row.get("classIndex", -1))
		class_option.add_item(_readable(String(row.get("nameStringId", ""))), index)
		if index == _selected_class:
			class_option.select(class_option.item_count - 1)


func _reload_sub_classes() -> void:
	sub_option.clear()
	var row := CahHeroes.class_row(_system, _selected_class)
	for sub_value in (row.get("subClasses", []) as Array):
		var sub := sub_value as Dictionary
		var index := int(sub.get("subClassIndex", -1))
		sub_option.add_item(_readable(String(sub.get("nameStringId", ""))), index)
		if index == _selected_sub:
			sub_option.select(sub_option.item_count - 1)


func _reset_attributes() -> void:
	## A new hero starts on its class's authored default loadout, which already
	## spends the whole budget. Starting at the minimum instead would hand the
	## player a hero weaker than anything retail ships and make "spend your
	## points" a chore rather than a choice.
	_attributes = CahHeroes.default_attributes(
		CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	)


func _rebuild_attribute_rows() -> void:
	for child in attribute_rows.get_children():
		child.queue_free()
	_attribute_sliders.clear()
	_attribute_values.clear()
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	for row_value in (sub_row.get("attributes", []) as Array):
		var row := row_value as Dictionary
		var group := String(row.get("groupName", ""))
		var line := HBoxContainer.new()
		line.name = group
		var label := Label.new()
		label.text = _readable(group)
		label.custom_minimum_size = Vector2(140, 0)
		line.add_child(label)
		var slider := HSlider.new()
		slider.name = "Slider"
		slider.min_value = float(row.get("minStep", 1))
		slider.max_value = float(row.get("maxStep", 20))
		slider.step = 1.0
		slider.value = float(_attributes.get(group, row.get("minStep", 1)))
		slider.custom_minimum_size = Vector2(220, 0)
		slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		slider.value_changed.connect(
			func(value: float) -> void: set_attribute(group, int(value))
		)
		line.add_child(slider)
		var value_label := Label.new()
		value_label.name = "Value"
		value_label.custom_minimum_size = Vector2(36, 0)
		line.add_child(value_label)
		attribute_rows.add_child(line)
		_attribute_sliders[group] = slider
		_attribute_values[group] = value_label


func _update_budget() -> void:
	var pair := spend_and_budget()
	budget_label.text = "Attribute points: %d / %d" % [pair.x, pair.y]
	for group_value in _attribute_values.keys():
		var group := String(group_value)
		(_attribute_values[group_value] as Label).text = str(int(_attributes.get(group, 0)))
	if save_button != null and not _system.is_empty():
		# The budget must be spent EXACTLY, which is what retail's own sixteen
		# subclasses do. Leaving points unspent is as invalid as overspending.
		save_button.disabled = pair.x != pair.y
		if pair.x != pair.y:
			status_label.text = "Spend exactly %d attribute points to save." % pair.y
		elif status_label.text.begins_with("Spend exactly"):
			status_label.text = ""


func _update_stats_summary() -> void:
	if stats_summary == null:
		return
	if _system.is_empty():
		stats_summary.text = ""
		return
	var class_row := CahHeroes.class_row(_system, _selected_class)
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	var build_cost := int(sub_row.get("buildCost", class_row.get("buildCost", 500)))
	stats_summary.text = "Class: %s\nType: %s\nBuild cost: %d" % [
		_readable(String(class_row.get("nameStringId", ""))),
		_readable(String(sub_row.get("nameStringId", ""))),
		build_cost,
	]


func _readable(string_id: String) -> String:
	## Until the localized-strings lane carries the CreateAHero namespace, show
	## the tail of the authored string id rather than the raw tag. This is a
	## presentation fallback and nothing computes on it.
	if string_id == "":
		return "?"
	var tail := string_id.get_slice(":", string_id.get_slice_count(":") - 1)
	for prefix in ["SubClassName_", "ClassName_", "CreateAHero_"]:
		if tail.begins_with(prefix):
			tail = tail.substr(prefix.length())
	return tail
