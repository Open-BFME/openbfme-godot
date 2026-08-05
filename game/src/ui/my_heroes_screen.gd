extends Panel
## MY HEROES: the Create-a-Hero front end.
##
## Retail's bottom bar has always carried this entry and Open BFME has always
## had it greyed out. It is live now, for one lane end to end: name a hero, pick
## a class and subclass, spend the class's attribute points, save. A saved hero
## is then purchasable from the fortress hero roster in skirmish exactly like
## Aragorn is, because `CahHeroes.roster_document` emits the same
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
## produces it, and stays otherwise inert. That is deliberate - inventing a
## fallback table would let a player build heroes whose numbers came from nowhere.

const CahHeroes = preload("res://src/content/cah_heroes.gd")

signal back_requested

## Emitted after any create or delete, so a host that caches rosters can drop it.
signal roster_changed

var _system: Dictionary = {}
var _profiles: Array[Dictionary] = []
var _selected_class := 0
var _selected_sub := 0
## Working loadout for the hero being built: group name -> 1-based step.
var _attributes: Dictionary = {}

var name_edit: LineEdit
var class_option: OptionButton
var sub_option: OptionButton
var budget_label: Label
var status_label: Label
var save_button: Button
var back_button: Button
var hero_list: ItemList
var delete_button: Button
var attribute_rows: VBoxContainer
var limitations_label: Label

var _attribute_sliders: Dictionary = {}
var _attribute_values: Dictionary = {}


func _ready() -> void:
	_build()
	refresh()


func configure(system: Dictionary) -> void:
	## Hand the screen the mounted class table. Called before `refresh()` by the
	## shell, and directly by the headless runner so it can drive the whole
	## screen without a pack on disk.
	_system = system if CahHeroes.system_is_valid(system) else {}
	if is_inside_tree():
		refresh()


func system_available() -> bool:
	return not _system.is_empty()


func refresh() -> void:
	_profiles = CahHeroes.load_profiles()
	_reload_hero_list()
	if _system.is_empty():
		_set_editor_enabled(false)
		status_label.text = (
			"No mounted content pack provides the Create-a-Hero class table. "
			+ "Compile one with `openbfme-import compile-cah-system --assets-root <effective-assets>` "
			+ "and publish it into a pack as `cah.system`."
		)
		return
	_set_editor_enabled(true)
	_reload_classes()
	_reload_sub_classes()
	_reset_attributes()
	_rebuild_attribute_rows()
	_update_budget()


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


func set_attribute(group_name: String, step: int) -> void:
	_attributes[group_name] = step
	if _attribute_sliders.has(group_name):
		(_attribute_sliders[group_name] as HSlider).set_value_no_signal(float(step))
	_update_budget()


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
	var margin := MarginContainer.new()
	margin.name = "Margin"
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["margin_left", "margin_right", "margin_top", "margin_bottom"]:
		margin.add_theme_constant_override(side, 24)
	add_child(margin)

	var columns := HBoxContainer.new()
	columns.name = "Columns"
	columns.add_theme_constant_override("separation", 24)
	margin.add_child(columns)

	# LEFT: the heroes that already exist.
	var left := VBoxContainer.new()
	left.name = "SavedHeroes"
	left.custom_minimum_size = Vector2(280, 0)
	columns.add_child(left)
	left.add_child(_heading("SAVED HEROES"))
	hero_list = ItemList.new()
	hero_list.name = "HeroList"
	hero_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	left.add_child(hero_list)
	delete_button = Button.new()
	delete_button.name = "DeleteHero"
	delete_button.text = "DELETE"
	delete_button.pressed.connect(func() -> void: delete_selected())
	left.add_child(delete_button)

	# RIGHT: the editor.
	var right := VBoxContainer.new()
	right.name = "Editor"
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	columns.add_child(right)
	right.add_child(_heading("CREATE A HERO"))

	name_edit = LineEdit.new()
	name_edit.name = "HeroName"
	name_edit.placeholder_text = "Hero name"
	name_edit.max_length = CahHeroes.MAX_NAME_LENGTH
	right.add_child(name_edit)

	class_option = OptionButton.new()
	class_option.name = "ClassOption"
	class_option.item_selected.connect(
		func(index: int) -> void: set_class_selection(int(class_option.get_item_id(index)), 0)
	)
	right.add_child(class_option)

	sub_option = OptionButton.new()
	sub_option.name = "SubClassOption"
	sub_option.item_selected.connect(
		func(index: int) -> void: set_class_selection(_selected_class, int(sub_option.get_item_id(index)))
	)
	right.add_child(sub_option)

	budget_label = Label.new()
	budget_label.name = "BudgetLabel"
	right.add_child(budget_label)

	attribute_rows = VBoxContainer.new()
	attribute_rows.name = "AttributeRows"
	right.add_child(attribute_rows)

	limitations_label = Label.new()
	limitations_label.name = "Limitations"
	limitations_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	limitations_label.text = (
		"Armour, colour and power customisation are not available yet. "
		+ "A created hero uses its class's authored appearance and command set."
	)
	right.add_child(limitations_label)

	status_label = Label.new()
	status_label.name = "Status"
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	right.add_child(status_label)

	var buttons := HBoxContainer.new()
	buttons.name = "Buttons"
	right.add_child(buttons)
	save_button = Button.new()
	save_button.name = "SaveHero"
	save_button.text = "SAVE HERO"
	save_button.pressed.connect(_on_save_pressed)
	buttons.add_child(save_button)
	back_button = Button.new()
	back_button.name = "Back"
	back_button.text = "BACK"
	back_button.pressed.connect(func() -> void: back_requested.emit())
	buttons.add_child(back_button)


func _heading(text: String) -> Label:
	var label := Label.new()
	label.text = text
	return label


func _on_save_pressed() -> void:
	var refusals := create_hero(name_edit.text)
	if refusals.is_empty():
		status_label.text = "Saved."
		name_edit.text = ""
	else:
		status_label.text = "Not saved: %s." % ", ".join(refusals)


func _set_editor_enabled(enabled: bool) -> void:
	## `LineEdit` has no `disabled`, so the text field is gated with `editable`
	## and only the buttons take `disabled`. Assigning `disabled` to everything
	## would throw on the one control that is not a button.
	if name_edit != null:
		name_edit.editable = enabled
	for button in [class_option, sub_option, save_button, delete_button]:
		if button != null:
			button.disabled = not enabled


func _reload_hero_list() -> void:
	hero_list.clear()
	for profile in _profiles:
		var refusals: Array[String] = []
		if _system.is_empty():
			refusals.append("no class table is mounted")
		else:
			refusals = CahHeroes.validate_profile(_system, profile)
		var label := String(profile.get("name", "?"))
		if not refusals.is_empty():
			label += "  (unavailable: %s)" % refusals[0]
		hero_list.add_item(label)


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
		slider.value_changed.connect(
			func(value: float) -> void: set_attribute(group, int(value))
		)
		line.add_child(slider)
		var value_label := Label.new()
		value_label.name = "Value"
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
