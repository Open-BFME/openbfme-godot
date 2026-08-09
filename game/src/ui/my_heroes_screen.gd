extends Panel
## CREATE-A-HERO: the retail front end, rebuilt to the shape of the real one.
##
## FOUR SCREENS, because retail has four (see reference/create a hero menu):
##
##   SELECT HERO              roster on the left (Name | Type), the selected
##                            hero previewed on the right under
##                            APPEARANCE / POWERS / STATS / AWARDS tabs.
##   SELECT HERO CLASS        a row of class archetype icons, a row of subclass
##   AND TYPE                 portraits, the description of each, and a live
##                            five-bar stat preview. CANCEL / NEXT.
##   CUSTOMIZE ATTRIBUTES     the name field, the appearance part pickers with
##                            left/right arrows, the three colour tabs, and the
##                            attribute point pool with its own arrows plus
##                            RESET / RECOMMEND. BACK / NEXT.
##   CUSTOMIZE HERO POWERS    the power grid: one row per prerequisite chain,
##                            one column per required hero level (1/3/7/10),
##                            the numbered Current Powers list, and Build Cost.
##                            BACK / DONE.
##
## WHY THE NUMBERS ARE NOT IN THIS FILE. Every value shown here - the class and
## subclass tables, the twenty-step attribute ladders, the 144 powers with their
## level gates and prices, the level chain, the per-subclass mesh and camera
## framing - is compiled from retail INI by `cah_system_compiler.py` and read off
## the mounted `cah.system`. This screen decides layout and nothing else. A
## screen that computed a stat would be a second implementation of the rules,
## free to disagree with the one the hero actually spawns with.
##
## ICONS AND MESHES DEGRADE LOUDLY. Retail art is resolved through the pack. When
## an icon or a mesh is not in the mounted pack the slot says so by name instead
## of drawing a substitute, because a plausible-looking stand-in is how a missing
## asset ships unnoticed.

const CahHeroes = preload("res://src/content/cah_heroes.gd")
const ThemeScript = preload("res://src/ui/openbfme_theme.gd")

## Loaded on demand rather than preloaded. `asset_factory.gd` reaches the
## `ContentDB` autoload at class scope, so preloading it here would make this
## screen - and anything that preloads this screen, including the gate - fail to
## compile in a context where the autoload is not registered.
const ASSET_FACTORY_PATH := "res://src/view/asset_factory.gd"
var _asset_factory: GDScript = null

signal back_requested
signal roster_changed

## The four retail screens.
const PAGE_SELECT := 0
const PAGE_CLASS := 1
const PAGE_ATTRIBUTES := 2
const PAGE_POWERS := 3

## The SELECT HERO detail tabs.
const TAB_APPEARANCE := 0
const TAB_POWERS := 1
const TAB_STATS := 2
const TAB_AWARDS := 3

## The four columns of the powers grid. Retail authors exactly these four
## `CreateAHeroUIMinimumLevel` values and the grid has exactly four columns.
const POWER_LEVEL_COLUMNS := [1, 3, 7, 10]

const ICON_SIZE := Vector2(56, 56)
const PORTRAIT_SIZE := Vector2(64, 64)
## The powers grid. Retail gives the row label roughly a third of the grid and
## draws the power icons large enough to read; 76x40 cells left the screen
## looking like a spreadsheet rather than the reference.
const POWER_LABEL_WIDTH := 300.0
const POWER_CELL_SIZE := Vector2(104, 52)

var _system: Dictionary = {}
var _profiles: Array[Dictionary] = []
var _selected_class := 0
var _selected_sub := 0
var _attributes: Dictionary = {}
var _appearance: Dictionary = {}
var _powers: Array = []
var _awards: Array = []
var _active_tab := TAB_STATS
var _page := PAGE_SELECT
var _content_db: Node = null
var _editing_hero_id := ""

# Nodes the gate and the callers reach for by name.
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
var title_label: Label
var section_label: Label
var stats_bars: VBoxContainer

var _pages: Array[Control] = []
var _tab_buttons: Array[Button] = []
var _tab_panels: Array[Control] = []
var _class_icon_row: HBoxContainer
var _sub_icon_row: HBoxContainer
var _class_description: Label
var _sub_description: Label
var _class_stat_bars: VBoxContainer
var _appearance_rows: VBoxContainer
var _colour_tabs: HBoxContainer
var _points_label: Label
var _power_grid: GridContainer
var _current_powers: VBoxContainer
var _build_cost_label: Label
var _power_hint: Label
var _preview_column: VBoxContainer
var _select_preview_slot: VBoxContainer
var _class_preview_slot: VBoxContainer
var _attributes_preview_slot: VBoxContainer
var _powers_preview_slot: VBoxContainer
var _preview_host: SubViewportContainer
var _preview_viewport: SubViewport
var _preview_root: Node3D
var _preview_camera: Camera3D
var _preview_model: Node3D
var _preview_note: Label
var _detail_portrait: TextureRect
var _detail_stats: VBoxContainer
var _detail_name: Label
var _awards_list: ItemList
var _detail_powers: VBoxContainer

var _attribute_sliders: Dictionary = {}
var _attribute_values: Dictionary = {}
var _selected_colour_slot := 0
var _preview_spin := 0.0


func _ready() -> void:
	theme = ThemeScript.create_theme(null)
	_content_db = get_node_or_null("/root/ContentDB")
	_ensure_built()
	set_process(true)
	refresh()


func _process(delta: float) -> void:
	if _preview_model == null or _preview_host == null or not _preview_host.visible:
		return
	_preview_spin += delta * 0.5
	_preview_model.rotation.y = _preview_spin


func configure(system: Dictionary) -> void:
	_system = system if CahHeroes.system_is_valid(system) else {}
	_content_db = get_node_or_null("/root/ContentDB")
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
		status_label.text = (
			"No mounted content pack provides the Create-a-Hero class table. "
			+ "Compile one with `openbfme-import compile-cah-system --assets-root <effective-assets>` "
			+ "and publish it into a pack as `cah.system`."
		)
		_show_page(PAGE_SELECT)
		return
	_set_editor_enabled(true)
	# THE STALE-MESSAGE BUG. The screen is configured once before ContentDB has
	# mounted its packs and again afterwards. Without this the first pass's "no
	# class table" refusal stayed on screen underneath a fully populated editor,
	# which is exactly how a working screen reads as broken.
	if status_label.text.begins_with("No mounted content pack"):
		status_label.text = ""
	_reload_classes()
	_reload_sub_classes()
	if _attributes.is_empty():
		_reset_working_loadout()
	_rebuild_all()
	_show_page(_page)


func _rebuild_all() -> void:
	_rebuild_class_icons()
	_rebuild_sub_icons()
	_rebuild_attribute_rows()
	_rebuild_appearance_rows()
	_rebuild_power_grid()
	_rebuild_current_powers()
	_rebuild_detail_tabs()
	_update_budget()
	_update_class_preview()
	_update_preview()


# ------------------------------------------------------------------ public API


func create_hero(hero_name: String) -> Array[String]:
	if _system.is_empty():
		return ["no Create-a-Hero class table is mounted"]
	var profile := CahHeroes.new_profile(_system, hero_name, _selected_class, _selected_sub)
	if _editing_hero_id != "":
		profile["heroId"] = _editing_hero_id
	profile["attributes"] = _attributes.duplicate()
	profile["appearance"] = _appearance.duplicate()
	profile["powers"] = _powers.duplicate()
	profile["awards"] = _awards.duplicate()
	var refusals := CahHeroes.validate_profile(_system, profile)
	if not refusals.is_empty():
		return refusals
	if _editing_hero_id == "" and _profiles.size() >= CahHeroes.MAX_PROFILES:
		return ["there are already %d saved heroes" % CahHeroes.MAX_PROFILES]
	var error := CahHeroes.save_profile(profile)
	if error != "":
		return [error]
	_editing_hero_id = ""
	refresh()
	roster_changed.emit()
	return []


func delete_selected() -> bool:
	var index := _selected_hero_index()
	if index < 0:
		return false
	if not CahHeroes.delete_profile(String(_profiles[index].get("heroId", ""))):
		return false
	refresh()
	roster_changed.emit()
	return true


func set_class_selection(class_index: int, sub_index: int) -> void:
	_selected_class = class_index
	_selected_sub = sub_index
	_reload_sub_classes()
	_reset_working_loadout()
	_rebuild_all()


func set_attribute(group_name: String, step: int) -> void:
	_attributes[group_name] = step
	if _attribute_sliders.has(group_name):
		(_attribute_sliders[group_name] as HSlider).set_value_no_signal(float(step))
	_update_budget()
	_update_class_preview()


func working_attributes() -> Dictionary:
	return _attributes.duplicate()


func working_powers() -> Array:
	return _powers.duplicate()


func saved_profiles() -> Array[Dictionary]:
	return _profiles.duplicate(true)


func spend_and_budget() -> Vector2i:
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	return Vector2i(
		CahHeroes.attribute_spend(sub_row, _attributes),
		CahHeroes.attribute_budget(sub_row)
	)


func build_cost() -> int:
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	return int(
		CahHeroes.computed_stats(_system, sub_row, _attributes, _powers).get("buildCost", 0)
	)


func _reset_working_loadout() -> void:
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	_attributes = CahHeroes.default_attributes(sub_row)
	_appearance = CahHeroes.default_appearance(sub_row)
	_powers = CahHeroes.default_powers(_system, _selected_class)
	_awards = []


# --------------------------------------------------------------------- layout


func _build() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	var root := VBoxContainer.new()
	root.name = "Root"
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.offset_left = 24
	root.offset_top = 14
	root.offset_right = -24
	root.offset_bottom = -14
	root.add_theme_constant_override("separation", 6)
	add_child(root)

	title_label = Label.new()
	title_label.name = "Title"
	title_label.text = "CREATE-A-HERO"
	title_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title_label.add_theme_font_size_override("font_size", 30)
	title_label.add_theme_color_override("font_color", Color(0.86, 0.90, 0.94, 0.96))
	root.add_child(title_label)

	section_label = Label.new()
	section_label.name = "Section"
	section_label.text = "SELECT HERO"
	section_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	section_label.add_theme_font_size_override("font_size", 17)
	root.add_child(section_label)

	var stack := Control.new()
	stack.name = "Pages"
	stack.size_flags_vertical = Control.SIZE_EXPAND_FILL
	stack.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root.add_child(stack)

	_pages = [
		_build_select_page(stack),
		_build_class_page(stack),
		_build_attributes_page(stack),
		_build_powers_page(stack),
	]

	status_label = Label.new()
	status_label.name = "Status"
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	root.add_child(status_label)

	# Hidden mirrors of the class/subclass selection. The icon rows are the real
	# control; these keep the old scripted API (and the gate) working without a
	# second source of truth.
	class_option = OptionButton.new()
	class_option.name = "ClassOption"
	class_option.visible = false
	class_option.item_selected.connect(
		func(index: int) -> void: set_class_selection(int(class_option.get_item_id(index)), 0)
	)
	root.add_child(class_option)
	sub_option = OptionButton.new()
	sub_option.name = "SubClassOption"
	sub_option.visible = false
	sub_option.item_selected.connect(
		func(index: int) -> void: set_class_selection(_selected_class, int(sub_option.get_item_id(index)))
	)
	root.add_child(sub_option)


func _build_select_page(parent: Control) -> Control:
	var page := HBoxContainer.new()
	page.name = "SelectHero"
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page.add_theme_constant_override("separation", 14)
	parent.add_child(page)

	# LEFT: the Name | Type roster.
	var left := VBoxContainer.new()
	left.custom_minimum_size = Vector2(360, 0)
	left.size_flags_vertical = Control.SIZE_EXPAND_FILL
	left.add_theme_constant_override("separation", 6)
	page.add_child(left)
	var header := HBoxContainer.new()
	left.add_child(header)
	var name_header := Label.new()
	name_header.text = "Name"
	name_header.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_header.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	header.add_child(name_header)
	var type_header := Label.new()
	type_header.text = "Type"
	type_header.custom_minimum_size = Vector2(150, 0)
	type_header.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	header.add_child(type_header)

	hero_list = ItemList.new()
	hero_list.name = "HeroList"
	hero_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	hero_list.allow_reselect = true
	hero_list.item_selected.connect(_on_hero_list_selected)
	left.add_child(hero_list)

	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 8)
	left.add_child(buttons)
	delete_button = Button.new()
	delete_button.name = "DeleteHero"
	delete_button.text = "DELETE HERO"
	delete_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	delete_button.pressed.connect(func() -> void: delete_selected())
	buttons.add_child(delete_button)
	new_hero_button = Button.new()
	new_hero_button.name = "NewHero"
	new_hero_button.text = "NEW HERO"
	new_hero_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	new_hero_button.pressed.connect(_on_new_hero_pressed)
	buttons.add_child(new_hero_button)

	back_button = Button.new()
	back_button.name = "Back"
	back_button.text = "MAIN MENU"
	back_button.custom_minimum_size = Vector2(180, 36)
	back_button.pressed.connect(func() -> void: back_requested.emit())
	left.add_child(back_button)

	# RIGHT: tabs over the preview.
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 6)
	page.add_child(right)

	var tabs := HBoxContainer.new()
	tabs.add_theme_constant_override("separation", 4)
	right.add_child(tabs)
	_tab_buttons.clear()
	for entry in [
		{"id": TAB_APPEARANCE, "label": "APPEARANCE"},
		{"id": TAB_POWERS, "label": "POWERS"},
		{"id": TAB_STATS, "label": "STATS"},
		{"id": TAB_AWARDS, "label": "AWARDS"},
	]:
		var tab := Button.new()
		tab.text = String(entry["label"])
		tab.toggle_mode = true
		tab.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var tab_id := int(entry["id"])
		tab.pressed.connect(func() -> void: _show_tab(tab_id))
		tabs.add_child(tab)
		_tab_buttons.append(tab)

	var body := HBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 12)
	right.add_child(body)

	# The 3D preview, which is the centre of every retail Create-a-Hero screen.
	_select_preview_slot = VBoxContainer.new()
	_select_preview_slot.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_select_preview_slot.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_child(_select_preview_slot)
	_build_preview()
	_select_preview_slot.add_child(_preview_column)

	# The detail column the tabs swap.
	var detail := Control.new()
	detail.custom_minimum_size = Vector2(330, 0)
	detail.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_child(detail)
	_tab_panels = [
		_build_detail_appearance(detail),
		_build_detail_powers(detail),
		_build_detail_stats(detail),
		_build_detail_awards(detail),
	]
	return page


func _build_preview() -> void:
	_preview_column = VBoxContainer.new()
	_preview_column.name = "HeroPreview"
	_preview_column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_preview_column.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var parent := _preview_column
	_preview_host = SubViewportContainer.new()
	_preview_host.stretch = true
	_preview_host.custom_minimum_size = Vector2(300, 340)
	_preview_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_preview_host.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(_preview_host)
	_preview_viewport = SubViewport.new()
	_preview_viewport.size = Vector2i(300, 340)
	_preview_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_preview_viewport.transparent_bg = true
	_preview_host.add_child(_preview_viewport)
	_preview_root = Node3D.new()
	_preview_viewport.add_child(_preview_root)
	_preview_camera = Camera3D.new()
	_preview_camera.position = Vector3(0.0, 1.3, 3.4)
	_preview_camera.rotation_degrees = Vector3(-10.0, 0.0, 0.0)
	_preview_root.add_child(_preview_camera)
	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-38.0, 34.0, 0.0)
	key.light_energy = 1.15
	_preview_root.add_child(key)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-16.0, -128.0, 0.0)
	fill.light_energy = 0.4
	_preview_root.add_child(fill)

	_preview_note = Label.new()
	_preview_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_preview_note.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_preview_note.add_theme_font_size_override("font_size", 12)
	parent.add_child(_preview_note)


func _build_detail_appearance(parent: Control) -> Control:
	var panel := VBoxContainer.new()
	panel.name = "DetailAppearance"
	panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	panel.add_theme_constant_override("separation", 4)
	parent.add_child(panel)
	_detail_portrait = TextureRect.new()
	_detail_portrait.custom_minimum_size = Vector2(150, 150)
	_detail_portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_detail_portrait.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	panel.add_child(_detail_portrait)
	_detail_name = Label.new()
	_detail_name.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_detail_name.add_theme_font_size_override("font_size", 16)
	panel.add_child(_detail_name)
	_detail_stats = VBoxContainer.new()
	_detail_stats.size_flags_vertical = Control.SIZE_EXPAND_FILL
	panel.add_child(_detail_stats)
	return panel


func _build_detail_powers(parent: Control) -> Control:
	var panel := VBoxContainer.new()
	panel.name = "DetailPowers"
	panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	panel.visible = false
	parent.add_child(panel)
	var heading := Label.new()
	heading.text = "Current Powers"
	panel.add_child(heading)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	panel.add_child(scroll)
	_detail_powers = VBoxContainer.new()
	_detail_powers.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_detail_powers)
	return panel


func _build_detail_stats(parent: Control) -> Control:
	var panel := VBoxContainer.new()
	panel.name = "DetailStats"
	panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	panel.visible = false
	parent.add_child(panel)
	stats_bars = VBoxContainer.new()
	stats_bars.size_flags_vertical = Control.SIZE_EXPAND_FILL
	panel.add_child(stats_bars)
	return panel


func _build_detail_awards(parent: Control) -> Control:
	var panel := VBoxContainer.new()
	panel.name = "DetailAwards"
	panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	panel.visible = false
	parent.add_child(panel)
	var hint := Label.new()
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	hint.text = "Awards unlock through play; the ones this hero has earned are ticked."
	panel.add_child(hint)
	_awards_list = ItemList.new()
	_awards_list.select_mode = ItemList.SELECT_MULTI
	_awards_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_awards_list.multi_selected.connect(func(_i: int, _s: bool) -> void: _sync_awards_from_list())
	panel.add_child(_awards_list)
	return panel


func _build_class_page(parent: Control) -> Control:
	var page := HBoxContainer.new()
	page.name = "SelectClass"
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page.visible = false
	page.add_theme_constant_override("separation", 14)
	parent.add_child(page)

	# The hero stands here. Retail shows him full height on the left of both the
	# class and the attributes screens, and it is the same hero on both, so the
	# one preview is reparented into whichever page is showing rather than each
	# page owning a second copy of the 3D scene.
	_class_preview_slot = VBoxContainer.new()
	_class_preview_slot.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_class_preview_slot.size_flags_vertical = Control.SIZE_EXPAND_FILL
	page.add_child(_class_preview_slot)

	var right := VBoxContainer.new()
	right.custom_minimum_size = Vector2(560, 0)
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 6)
	page.add_child(right)

	right.add_child(_heading("CLASS"))
	_class_icon_row = HBoxContainer.new()
	_class_icon_row.add_theme_constant_override("separation", 4)
	right.add_child(_class_icon_row)
	_class_description = Label.new()
	_class_description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_class_description.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_class_description.custom_minimum_size = Vector2(0, 44)
	right.add_child(_class_description)

	right.add_child(_heading("TYPE"))
	_sub_icon_row = HBoxContainer.new()
	_sub_icon_row.add_theme_constant_override("separation", 4)
	right.add_child(_sub_icon_row)
	_sub_description = Label.new()
	_sub_description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_sub_description.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_sub_description.custom_minimum_size = Vector2(0, 40)
	right.add_child(_sub_description)

	_class_stat_bars = VBoxContainer.new()
	_class_stat_bars.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(_class_stat_bars)

	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 10)
	right.add_child(footer)
	footer.add_child(_footer_button("CANCEL", func() -> void: _show_page(PAGE_SELECT)))
	var pad := Control.new()
	pad.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	footer.add_child(pad)
	footer.add_child(_footer_button("NEXT", func() -> void: _show_page(PAGE_ATTRIBUTES)))
	return page


func _build_attributes_page(parent: Control) -> Control:
	var page := HBoxContainer.new()
	page.name = "CustomizeAttributes"
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page.visible = false
	page.add_theme_constant_override("separation", 14)
	parent.add_child(page)

	_attributes_preview_slot = VBoxContainer.new()
	_attributes_preview_slot.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_attributes_preview_slot.size_flags_vertical = Control.SIZE_EXPAND_FILL
	page.add_child(_attributes_preview_slot)

	var right := VBoxContainer.new()
	right.custom_minimum_size = Vector2(560, 0)
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 6)
	page.add_child(right)

	name_edit = LineEdit.new()
	name_edit.name = "HeroName"
	name_edit.placeholder_text = "Hero name"
	name_edit.max_length = CahHeroes.MAX_NAME_LENGTH
	name_edit.alignment = HORIZONTAL_ALIGNMENT_CENTER
	right.add_child(name_edit)

	right.add_child(_heading("APPEARANCE"))
	var appearance_scroll := ScrollContainer.new()
	appearance_scroll.custom_minimum_size = Vector2(0, 190)
	right.add_child(appearance_scroll)
	_appearance_rows = VBoxContainer.new()
	_appearance_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	appearance_scroll.add_child(_appearance_rows)

	var colours := Label.new()
	colours.text = "Select Colors:"
	right.add_child(colours)
	_colour_tabs = HBoxContainer.new()
	_colour_tabs.add_theme_constant_override("separation", 4)
	right.add_child(_colour_tabs)
	for slot in range(3):
		var tab := Button.new()
		tab.text = str(slot + 1)
		tab.toggle_mode = true
		tab.button_pressed = slot == 0
		tab.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var captured := slot
		tab.pressed.connect(func() -> void: _select_colour_slot(captured))
		_colour_tabs.add_child(tab)

	right.add_child(_heading("ATTRIBUTES"))
	_points_label = Label.new()
	_points_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_points_label.add_theme_font_size_override("font_size", 15)
	right.add_child(_points_label)
	budget_label = _points_label

	attribute_rows = VBoxContainer.new()
	attribute_rows.name = "AttributeRows"
	attribute_rows.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(attribute_rows)

	var tools := HBoxContainer.new()
	tools.add_theme_constant_override("separation", 8)
	right.add_child(tools)
	var reset := Button.new()
	reset.text = "RESET"
	reset.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	reset.pressed.connect(_on_reset_attributes)
	tools.add_child(reset)
	var recommend := Button.new()
	recommend.text = "RECOMMEND"
	recommend.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	recommend.pressed.connect(_on_reset_attributes)
	tools.add_child(recommend)

	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 10)
	right.add_child(footer)
	footer.add_child(_footer_button("BACK", func() -> void: _show_page(PAGE_CLASS)))
	var pad := Control.new()
	pad.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	footer.add_child(pad)
	footer.add_child(_footer_button("NEXT", func() -> void: _show_page(PAGE_POWERS)))
	return page


func _build_powers_page(parent: Control) -> Control:
	var page := HBoxContainer.new()
	page.name = "CustomizePowers"
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page.visible = false
	page.add_theme_constant_override("separation", 12)
	parent.add_child(page)

	# LEFT: the grid of power chains.
	var left := VBoxContainer.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.size_flags_vertical = Control.SIZE_EXPAND_FILL
	left.add_theme_constant_override("separation", 4)
	page.add_child(left)
	left.add_child(_heading("Choose Hero Powers"))

	var grid_scroll := ScrollContainer.new()
	grid_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	left.add_child(grid_scroll)
	# One label column plus one column per authored level tier.
	_power_grid = GridContainer.new()
	_power_grid.columns = POWER_LEVEL_COLUMNS.size() + 1
	_power_grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_power_grid.add_theme_constant_override("h_separation", 6)
	_power_grid.add_theme_constant_override("v_separation", 4)
	grid_scroll.add_child(_power_grid)

	# The "Required Hero Level" strip is emitted as the LAST ROW OF THE GRID by
	# `_rebuild_power_grid`, not as a sibling HBoxContainer. As a sibling its
	# cells were sized independently and drifted out of line with the columns
	# they label - the numbers sat ~12px left of their own icons, so the grid
	# read as if every power sat one tier off. Inside the grid they align by
	# construction.

	# Retail keeps the hero on this screen too, under the grid beside the power
	# ring, so the player can see who they are spending the cost on.
	_powers_preview_slot = VBoxContainer.new()
	_powers_preview_slot.custom_minimum_size = Vector2(0, 260)
	_powers_preview_slot.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.add_child(_powers_preview_slot)

	_power_hint = Label.new()
	_power_hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_power_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	left.add_child(_power_hint)

	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 10)
	left.add_child(footer)
	footer.add_child(_footer_button("BACK", func() -> void: _show_page(PAGE_ATTRIBUTES)))
	var pad := Control.new()
	pad.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	footer.add_child(pad)
	save_button = Button.new()
	save_button.name = "SaveHero"
	save_button.text = "DONE"
	save_button.custom_minimum_size = Vector2(160, 34)
	save_button.pressed.connect(_on_save_pressed)
	footer.add_child(save_button)

	# RIGHT: the numbered Current Powers list and the running Build Cost.
	var right := VBoxContainer.new()
	right.custom_minimum_size = Vector2(320, 0)
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 4)
	page.add_child(right)
	var head := HBoxContainer.new()
	right.add_child(head)
	var current := Label.new()
	current.text = "Current Powers"
	current.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(current)
	var reset := Button.new()
	reset.text = "RESET"
	reset.pressed.connect(_on_reset_powers)
	head.add_child(reset)

	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(scroll)
	_current_powers = VBoxContainer.new()
	_current_powers.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_current_powers)

	var cost_row := HBoxContainer.new()
	right.add_child(cost_row)
	var cost_caption := Label.new()
	cost_caption.text = "Build Cost"
	cost_caption.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cost_row.add_child(cost_caption)
	_build_cost_label = Label.new()
	_build_cost_label.add_theme_font_size_override("font_size", 16)
	cost_row.add_child(_build_cost_label)
	return page


func _heading(text: String) -> Label:
	var label := Label.new()
	label.text = text
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.add_theme_font_size_override("font_size", 15)
	label.add_theme_color_override("font_color", Color(0.78, 0.86, 0.95, 0.95))
	return label


func _footer_button(text: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.custom_minimum_size = Vector2(160, 34)
	button.pressed.connect(action)
	return button


# ------------------------------------------------------------------ page flow


func _show_page(page: int) -> void:
	_page = page
	for index in range(_pages.size()):
		_pages[index].visible = index == page
	_move_preview_to(page)
	match page:
		PAGE_SELECT:
			section_label.text = "SELECT HERO"
			_show_tab(_active_tab)
		PAGE_CLASS:
			section_label.text = "SELECT HERO CLASS AND TYPE"
		PAGE_ATTRIBUTES:
			section_label.text = "CUSTOMIZE ATTRIBUTES"
		PAGE_POWERS:
			section_label.text = "CUSTOMIZE HERO POWERS"
	_update_preview()


func _move_preview_to(page: int) -> void:
	## ONE hero, reparented. Every retail Create-a-Hero page shows the same hero
	## standing beside the panel being edited; building a second SubViewport per
	## page would mean four 3D scenes to keep in step, and any drift between them
	## would read as the preview lying about what is being edited.
	if _preview_column == null:
		return
	var slot: Control = null
	match page:
		PAGE_SELECT: slot = _select_preview_slot
		PAGE_CLASS: slot = _class_preview_slot
		PAGE_ATTRIBUTES: slot = _attributes_preview_slot
		PAGE_POWERS: slot = _powers_preview_slot
	if slot == null or _preview_column.get_parent() == slot:
		return
	if _preview_column.get_parent() != null:
		_preview_column.get_parent().remove_child(_preview_column)
	slot.add_child(_preview_column)


func _show_tab(tab_id: int) -> void:
	_active_tab = tab_id
	for index in range(_tab_buttons.size()):
		_tab_buttons[index].button_pressed = index == tab_id
	for index in range(_tab_panels.size()):
		_tab_panels[index].visible = index == tab_id


func _on_new_hero_pressed() -> void:
	_editing_hero_id = ""
	name_edit.text = ""
	_reset_working_loadout()
	_rebuild_all()
	_show_page(PAGE_CLASS)
	status_label.text = "Pick a class and type, then set attributes and powers."


func _on_hero_list_selected(index: int) -> void:
	if index < 0 or index >= _profiles.size():
		return
	var profile := _profiles[index]
	_editing_hero_id = String(profile.get("heroId", ""))
	name_edit.text = String(profile.get("name", ""))
	_selected_class = int(profile.get("classIndex", 0))
	_selected_sub = int(profile.get("subClassIndex", 0))
	_attributes = (profile.get("attributes", {}) as Dictionary).duplicate()
	_appearance = (profile.get("appearance", {}) as Dictionary).duplicate()
	_powers = (profile.get("powers", []) as Array).duplicate()
	_awards = (profile.get("awards", []) as Array).duplicate()
	_reload_classes()
	_reload_sub_classes()
	_rebuild_all()
	_show_page(PAGE_SELECT)


func _on_save_pressed() -> void:
	var refusals := create_hero(name_edit.text)
	if refusals.is_empty():
		status_label.text = "Saved."
		name_edit.text = ""
		_show_page(PAGE_SELECT)
	else:
		status_label.text = "Not saved: %s." % ", ".join(refusals)


func _on_reset_attributes() -> void:
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	_attributes = CahHeroes.default_attributes(sub_row)
	_rebuild_attribute_rows()
	_update_budget()
	_update_class_preview()


func _on_reset_powers() -> void:
	_powers.clear()
	_rebuild_power_grid()
	_rebuild_current_powers()
	_update_budget()


func _select_colour_slot(slot: int) -> void:
	_selected_colour_slot = slot
	for index in range(_colour_tabs.get_child_count()):
		(_colour_tabs.get_child(index) as Button).button_pressed = index == slot


func _set_editor_enabled(enabled: bool) -> void:
	if name_edit != null:
		name_edit.editable = enabled
	for control in [class_option, sub_option, save_button, delete_button, new_hero_button]:
		if control != null:
			control.disabled = not enabled


# ------------------------------------------------------------------- rebuilds


func _reload_hero_list() -> void:
	hero_list.clear()
	for profile in _profiles:
		var refusals: Array[String] = []
		if _system.is_empty():
			refusals.append("no class table is mounted")
		else:
			refusals = CahHeroes.validate_profile(_system, profile)
		var label := "%-22s  %s" % [
			String(profile.get("name", "?")), _profile_type_label(profile)
		]
		if not refusals.is_empty():
			label += "   (unavailable: %s)" % refusals[0]
		hero_list.add_item(label)


func _profile_type_label(profile: Dictionary) -> String:
	if _system.is_empty():
		return "?"
	var sub_row := CahHeroes.sub_class_row(
		_system, int(profile.get("classIndex", -1)), int(profile.get("subClassIndex", -1))
	)
	var sub_label := _readable(String(sub_row.get("nameStringId", "")))
	if sub_label != "?" and sub_label != "":
		return sub_label
	return _readable(String(
		CahHeroes.class_row(_system, int(profile.get("classIndex", -1))).get("nameStringId", "")
	))


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


func _rebuild_class_icons() -> void:
	for child in _class_icon_row.get_children():
		child.queue_free()
	var registration: Dictionary = _system.get("registration", {}) as Dictionary
	for class_value in (registration.get("classes", []) as Array):
		var row := class_value as Dictionary
		var index := int(row.get("classIndex", -1))
		var button := _icon_button(
			String(row.get("iconImageId", "")),
			_readable(String(row.get("nameStringId", ""))),
			ICON_SIZE,
			index == _selected_class
		)
		button.pressed.connect(func() -> void: set_class_selection(index, 0))
		_class_icon_row.add_child(button)


func _rebuild_sub_icons() -> void:
	for child in _sub_icon_row.get_children():
		child.queue_free()
	var row := CahHeroes.class_row(_system, _selected_class)
	for sub_value in (row.get("subClasses", []) as Array):
		var sub := sub_value as Dictionary
		var index := int(sub.get("subClassIndex", -1))
		var button := _icon_button(
			String(sub.get("iconImageId", "")),
			_readable(String(sub.get("nameStringId", ""))),
			PORTRAIT_SIZE,
			index == _selected_sub
		)
		button.pressed.connect(func() -> void: set_class_selection(_selected_class, index))
		_sub_icon_row.add_child(button)


func _icon_button(image_id: String, label: String, size: Vector2, selected: bool) -> Button:
	## One retail icon, or a named refusal in its place.
	##
	## An icon the pack does not carry becomes a button captioned with what it
	## WOULD have been rather than a blank or a stand-in glyph, so a missing
	## atlas crop is visible on the screen instead of quietly looking fine.
	var button := Button.new()
	button.toggle_mode = true
	button.button_pressed = selected
	button.custom_minimum_size = size
	button.tooltip_text = label
	var texture := _icon_texture(image_id)
	if texture != null:
		button.icon = texture
		button.expand_icon = true
	else:
		button.text = label.substr(0, 3)
		button.tooltip_text = "%s\n(icon %s is not in the mounted pack)" % [label, image_id]
	return button


func _icon_texture(image_id: String) -> Texture2D:
	if image_id == "" or _content_db == null:
		return null
	if not _content_db.has_method("resolve_retail_ui_image_path"):
		return null
	var path := String(_content_db.call("resolve_retail_ui_image_path", image_id))
	if path == "" or not FileAccess.file_exists(path):
		return null
	var image := Image.load_from_file(path)
	if image == null or image.is_empty():
		return null
	return ImageTexture.create_from_image(image)


func _rebuild_attribute_rows() -> void:
	for child in attribute_rows.get_children():
		child.queue_free()
	_attribute_sliders.clear()
	_attribute_values.clear()
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	for row_value in (sub_row.get("attributes", []) as Array):
		var row := row_value as Dictionary
		var group := String(row.get("groupName", ""))
		var low := int(row.get("minStep", 1))
		var high := int(row.get("maxStep", 20))
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 6)
		var label := Label.new()
		label.text = _attribute_label(group)
		label.custom_minimum_size = Vector2(120, 0)
		line.add_child(label)
		var slider := HSlider.new()
		slider.min_value = float(low)
		slider.max_value = float(high)
		slider.step = 1.0
		slider.value = float(_attributes.get(group, low))
		slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		slider.value_changed.connect(func(value: float) -> void: set_attribute(group, int(value)))
		line.add_child(slider)
		# Retail drives these with a left/right arrow pair; both are kept so the
		# step is reachable exactly as well as by dragging.
		var down := Button.new()
		down.text = "<"
		down.custom_minimum_size = Vector2(28, 0)
		down.pressed.connect(
			func() -> void: set_attribute(group, maxi(low, int(_attributes.get(group, low)) - 1))
		)
		line.add_child(down)
		var up := Button.new()
		up.text = ">"
		up.custom_minimum_size = Vector2(28, 0)
		up.pressed.connect(
			func() -> void: set_attribute(group, mini(high, int(_attributes.get(group, low)) + 1))
		)
		line.add_child(up)
		var value_label := Label.new()
		value_label.custom_minimum_size = Vector2(30, 0)
		value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		line.add_child(value_label)
		attribute_rows.add_child(line)
		_attribute_sliders[group] = slider
		_attribute_values[group] = value_label


func _rebuild_appearance_rows() -> void:
	for child in _appearance_rows.get_children():
		child.queue_free()
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	var choices: Dictionary = sub_row.get("appearanceChoices", {}) as Dictionary
	if choices.is_empty():
		var empty := Label.new()
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		empty.text = "This subclass has no appearance parts in the mounted pack."
		_appearance_rows.add_child(empty)
		return
	for group_value in choices.keys():
		var group := String(group_value)
		var upgrades: Array = choices[group_value] as Array
		if upgrades.is_empty():
			continue
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 6)
		var label := Label.new()
		label.text = _readable(group)
		label.custom_minimum_size = Vector2(130, 0)
		line.add_child(label)
		var value_label := Label.new()
		value_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line.add_child(value_label)
		var left := Button.new()
		left.text = "<"
		left.custom_minimum_size = Vector2(30, 0)
		line.add_child(left)
		var right := Button.new()
		right.text = ">"
		right.custom_minimum_size = Vector2(30, 0)
		line.add_child(right)
		_appearance_rows.add_child(line)
		var apply := func(delta: int) -> void:
			var current := upgrades.find(String(_appearance.get(group, "")))
			if current < 0:
				current = 0
			var next := wrapi(current + delta, 0, upgrades.size())
			_appearance[group] = String(upgrades[next])
			value_label.text = CahHeroes.option_label_for_upgrade(_system, String(upgrades[next]))
			_update_preview()
		left.pressed.connect(func() -> void: apply.call(-1))
		right.pressed.connect(func() -> void: apply.call(1))
		apply.call(0)


func _rebuild_power_grid() -> void:
	for child in _power_grid.get_children():
		child.queue_free()
	var trees := CahHeroes.power_trees_for_class(_system, _selected_class)
	if trees.is_empty():
		var empty := Label.new()
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		empty.text = "No powers are offered to this class in the mounted pack."
		_power_grid.add_child(empty)
		return
	var selected := {}
	for power_value in _powers:
		selected[String(power_value)] = true
	for tree_value in trees:
		var tree := tree_value as Dictionary
		var label := Label.new()
		label.text = _readable(String(tree.get("labelStringId", tree.get("familyId", "?"))))
		label.custom_minimum_size = Vector2(POWER_LABEL_WIDTH, 0)
		label.clip_text = true
		_power_grid.add_child(label)
		# One cell per authored level column; a chain that skips a tier leaves
		# its cell empty, exactly as the retail grid does.
		var by_level := {}
		for level_value in (tree.get("levels", []) as Array):
			by_level[int((level_value as Dictionary).get("requiredHeroLevel", 1))] = level_value
		for column_level in POWER_LEVEL_COLUMNS:
			if not by_level.has(column_level):
				var blank := Control.new()
				blank.custom_minimum_size = Vector2(POWER_CELL_SIZE.x, POWER_CELL_SIZE.y)
				_power_grid.add_child(blank)
				continue
			var level := by_level[column_level] as Dictionary
			var power_id := String(level.get("powerId", ""))
			var cell := _icon_button(
				String(level.get("buttonImageId", "")),
				_readable(String(level.get("nameStringId", power_id))),
				POWER_CELL_SIZE,
				selected.has(power_id)
			)
			var cost := int(level.get("costIfSelected", 0))
			cell.tooltip_text = "%s\n+%d Hero Cost\nRequires level %d%s" % [
				_readable(String(level.get("nameStringId", power_id))),
				cost,
				column_level,
				(
					"\nRequires %s" % _readable(String(level.get("prerequisitePowerId", "")))
					if String(level.get("prerequisitePowerId", "")) != "" else ""
				),
			]
			cell.pressed.connect(func() -> void: _toggle_power(power_id))
			_power_grid.add_child(cell)
	_append_power_level_header()


func _append_power_level_header() -> void:
	var caption := Label.new()
	caption.text = "Required Hero Level"
	caption.custom_minimum_size = Vector2(POWER_LABEL_WIDTH, 0)
	caption.add_theme_color_override("font_color", Color(0.74, 0.83, 0.94, 0.95))
	_power_grid.add_child(caption)
	for level in POWER_LEVEL_COLUMNS:
		var cell := Label.new()
		cell.text = str(level)
		cell.custom_minimum_size = Vector2(POWER_CELL_SIZE.x, 0)
		cell.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		cell.add_theme_font_size_override("font_size", 16)
		_power_grid.add_child(cell)


func _toggle_power(power_id: String) -> void:
	if _powers.has(power_id):
		# Removing a power removes everything downstream of it, because the
		# prerequisite chain is a real constraint - leaving an orphaned upper
		# tier selected would make the loadout unsaveable with no visible cause.
		_powers.erase(power_id)
		var changed := true
		while changed:
			changed = false
			for candidate in _powers.duplicate():
				var row := CahHeroes.power_row(_system, String(candidate))
				var prerequisite := String(row.get("prerequisitePowerId", ""))
				if prerequisite != "" and not _powers.has(prerequisite):
					_powers.erase(candidate)
					changed = true
	else:
		if _powers.size() >= CahHeroes.max_power_slots(_system):
			status_label.text = "Power slots full (%d)." % CahHeroes.max_power_slots(_system)
			return
		var row := CahHeroes.power_row(_system, power_id)
		var prerequisite := String(row.get("prerequisitePowerId", ""))
		if prerequisite != "" and not _powers.has(prerequisite):
			_power_hint.text = "%s requires %s first." % [
				_readable(String(row.get("nameStringId", power_id))), _readable(prerequisite)
			]
			return
		_powers.append(power_id)
	_power_hint.text = ""
	_rebuild_power_grid()
	_rebuild_current_powers()
	_update_budget()


func _rebuild_current_powers() -> void:
	for child in _current_powers.get_children():
		child.queue_free()
	if _powers.is_empty():
		_power_hint.text = "Select a Level 1 power to go in your Hero's Current Powers."
	for index in range(_powers.size()):
		var power_id := String(_powers[index])
		var row := CahHeroes.power_row(_system, power_id)
		var line := HBoxContainer.new()
		var ordinal := Label.new()
		ordinal.text = "%d." % (index + 1)
		ordinal.custom_minimum_size = Vector2(26, 0)
		line.add_child(ordinal)
		var label := Label.new()
		label.text = _readable(String(row.get("nameStringId", power_id)))
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		label.clip_text = true
		line.add_child(label)
		var texture := _icon_texture(String(row.get("buttonImageId", "")))
		if texture != null:
			var icon := TextureRect.new()
			icon.texture = texture
			icon.custom_minimum_size = Vector2(28, 28)
			icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
			line.add_child(icon)
		_current_powers.add_child(line)
	_rebuild_detail_tabs()


func _rebuild_detail_tabs() -> void:
	if _detail_powers == null:
		return
	for child in _detail_powers.get_children():
		child.queue_free()
	for index in range(_powers.size()):
		var row := CahHeroes.power_row(_system, String(_powers[index]))
		var label := Label.new()
		label.text = "%d.  %s" % [index + 1, _readable(String(row.get("nameStringId", _powers[index])))]
		_detail_powers.add_child(label)
	_rebuild_awards_list()
	_update_detail_stats()


func _rebuild_awards_list() -> void:
	if _awards_list == null:
		return
	_awards_list.clear()
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	var awards: Array = sub_row.get("awards", []) as Array
	if awards.is_empty():
		_awards_list.add_item("(no awards authored for this subclass)")
		return
	var owned := {}
	for award_value in _awards:
		owned[String(award_value)] = true
	for award_value in awards:
		var award_id := String(award_value)
		var index := _awards_list.item_count
		_awards_list.add_item(_readable(award_id))
		_awards_list.set_item_metadata(index, award_id)
		if owned.has(award_id):
			_awards_list.select(index, false)


func _sync_awards_from_list() -> void:
	_awards.clear()
	for index in _awards_list.get_selected_items():
		var value: Variant = _awards_list.get_item_metadata(index)
		if value != null:
			_awards.append(String(value))


# --------------------------------------------------------------------- readouts


func _update_budget() -> void:
	var pair := spend_and_budget()
	var remaining := pair.y - pair.x
	if _points_label != null:
		_points_label.text = "Distribute Attribute Points:  %d" % remaining
	for group_value in _attribute_values.keys():
		var group := String(group_value)
		(_attribute_values[group_value] as Label).text = str(int(_attributes.get(group, 0)))
	if _build_cost_label != null:
		_build_cost_label.text = str(build_cost())
	if save_button != null and not _system.is_empty():
		save_button.disabled = pair.x != pair.y
		if pair.x != pair.y:
			status_label.text = "Spend exactly %d attribute points to save." % pair.y
		elif status_label.text.begins_with("Spend exactly"):
			status_label.text = ""


func _update_class_preview() -> void:
	if _class_stat_bars == null or _system.is_empty():
		return
	for child in _class_stat_bars.get_children():
		child.queue_free()
	var class_row := CahHeroes.class_row(_system, _selected_class)
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	_class_description.text = _readable(String(class_row.get("nameStringId", "")))
	_sub_description.text = _readable(String(sub_row.get("nameStringId", "")))
	# THE FIVE BARS RETAIL SHOWS ARE THE ATTRIBUTE STEPS, not derived numbers:
	# on the reference screens a Great Troll reads Armor 12 / Power 13 / Health
	# 13 / Heal Rate 7 / Vision 2, which are its steps out of twenty.
	for row_value in (sub_row.get("attributes", []) as Array):
		var row := row_value as Dictionary
		var group := String(row.get("groupName", ""))
		_add_step_bar(
			_class_stat_bars,
			_attribute_label(group),
			int(_attributes.get(group, row.get("minStep", 1))),
			int(row.get("maxStep", 20))
		)


func _update_detail_stats() -> void:
	if _detail_stats == null or _system.is_empty():
		return
	for child in _detail_stats.get_children():
		child.queue_free()
	var class_row := CahHeroes.class_row(_system, _selected_class)
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	_detail_name.text = name_edit.text if name_edit != null and name_edit.text != "" else _readable(String(sub_row.get("nameStringId", "")))
	_detail_portrait.texture = _icon_texture(String(sub_row.get("iconImageId", "")))
	for pair in [
		["Class", _readable(String(class_row.get("nameStringId", "")))],
		["Type", _readable(String(sub_row.get("nameStringId", "")))],
		["Build Cost", str(build_cost())],
	]:
		var line := HBoxContainer.new()
		var caption := Label.new()
		caption.text = String(pair[0])
		caption.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line.add_child(caption)
		var value := Label.new()
		value.text = String(pair[1])
		line.add_child(value)
		_detail_stats.add_child(line)
	for row_value in (sub_row.get("attributes", []) as Array):
		var row := row_value as Dictionary
		var group := String(row.get("groupName", ""))
		_add_step_bar(
			_detail_stats,
			_attribute_label(group),
			int(_attributes.get(group, row.get("minStep", 1))),
			int(row.get("maxStep", 20))
		)
	if stats_bars != null:
		for child in stats_bars.get_children():
			child.queue_free()


func _add_step_bar(parent: Control, label_text: String, step: int, maximum: int) -> void:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	var label := Label.new()
	label.text = label_text
	label.custom_minimum_size = Vector2(96, 0)
	line.add_child(label)
	var bar := ProgressBar.new()
	bar.min_value = 0.0
	bar.max_value = float(maxi(1, maximum))
	bar.value = float(step)
	bar.show_percentage = false
	bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.custom_minimum_size = Vector2(110, 14)
	line.add_child(bar)
	var value := Label.new()
	value.text = str(step)
	value.custom_minimum_size = Vector2(28, 0)
	value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	line.add_child(value)
	parent.add_child(line)


# ------------------------------------------------------------------- 3D preview


func _update_preview() -> void:
	if _preview_root == null or _system.is_empty():
		return
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	# The creation-screen pose, which is a DIFFERENT mesh from the battlefield
	# one; retail authors both and shows this one in these screens.
	var binding := CahHeroes.model_binding(sub_row, "creationScreen")
	var model_id := String(binding.get("model", ""))
	if model_id == "":
		binding = CahHeroes.model_binding(sub_row, "battlefield")
		model_id = String(binding.get("model", ""))
	if _preview_model != null:
		_preview_model.queue_free()
		_preview_model = null
	if model_id == "":
		_preview_note.text = "This subclass has no mesh in the mounted pack."
		return
	var path := _resolve_model_path(model_id)
	if path == "":
		# NAMED, NOT SUBSTITUTED. A capsule here is how a missing mesh ships
		# unnoticed; the model retail wants is stated instead.
		_preview_note.text = "Model %s is not in the mounted pack yet." % model_id
		return
	var factory := _load_asset_factory()
	if factory == null:
		_preview_note.text = "The model loader is unavailable in this context."
		return
	var visual: Node3D = factory.make_explicit_model_visual(path, 0)
	if visual == null:
		_preview_note.text = "Model %s could not be loaded from the pack." % model_id
		return
	_preview_model = visual
	_preview_root.add_child(visual)
	_frame_preview(sub_row, visual)
	_preview_note.text = ""


func _load_asset_factory() -> GDScript:
	if _asset_factory == null and ResourceLoader.exists(ASSET_FACTORY_PATH):
		_asset_factory = load(ASSET_FACTORY_PATH) as GDScript
	return _asset_factory


func _resolve_model_path(model_id: String) -> String:
	if _content_db == null:
		return ""
	for method in ["resolve_cah_model_path", "resolve_model_path"]:
		if _content_db.has_method(method):
			var path := String(_content_db.call(method, model_id))
			if path != "" and FileAccess.file_exists(path):
				return path
	return ""


func _frame_preview(sub_row: Dictionary, visual: Node3D) -> void:
	## Frame the hero using the subclass's OWN authored camera.
	##
	## Retail authors a ViewInfo per subclass precisely because one framing does
	## not fit both a Wanderer and a Great Troll. When the pack carries it we use
	## it; otherwise the model's own bounds are used so a tall hero is still
	## whole in the viewport rather than cropped at a fixed distance.
	var view: Dictionary = sub_row.get("viewInfo", {}) as Dictionary
	var factory := _load_asset_factory()
	if factory == null:
		return
	var bounds: AABB = factory.model_aabb(visual)
	var height := maxf(0.1, bounds.size.y)
	visual.position = Vector3(0.0, -bounds.position.y, 0.0)
	var distance := height * 2.2
	var pitch := -8.0
	if not view.is_empty():
		distance = maxf(distance, float(view.get("closeUpDist", distance)) * 0.08)
		pitch = float(view.get("closeUpPitch", -0.14)) * 60.0
	_preview_camera.position = Vector3(0.0, height * 0.55, distance)
	_preview_camera.rotation_degrees = Vector3(pitch, 0.0, 0.0)


# ---------------------------------------------------------------------- labels


func _attribute_label(group_name: String) -> String:
	## The five names retail prints beside the bars.
	var tail := _readable(group_name)
	match tail:
		"ArmorAttribute": return "Armor"
		"DamageMultAttribute": return "Power"
		"HealthMultAttribute": return "Health"
		"AutoHealAttribute": return "Heal Rate"
		"VisionAttribute": return "Vision"
	return tail


func _readable(string_id: String) -> String:
	## The player-facing text for an authored string id.
	##
	## Retail writes `CONTROLBAR:SuperiorCallReinforcements` and ships the
	## English text in `lotr.str`, which the pack carries as its strings table.
	## Resolving through it is the difference between the screen reading
	## "Superior Call Reinforcements" and "SpecialAbilityHotWCri" - and the
	## reference screens show the former. The de-namespaced tail is the fallback
	## when a pack has no string for the id, which keeps the row identifiable
	## instead of blank.
	if string_id == "":
		return ""
	if _content_db != null and _content_db.has_method("get_retail_string"):
		var resolved := String(_content_db.call("get_retail_string", string_id, ""))
		if resolved.strip_edges() != "":
			return _without_hotkey_markers(resolved)
	return CahHeroes._tail_label(string_id)


static func _without_hotkey_markers(text: String) -> String:
	## Retail marks a label's keyboard accelerator with `&`, Windows-style:
	## `Ca&ll Reinforcements` underlines the second L. Godot does not consume
	## the marker, so left alone it prints literally and the screen reads
	## "Ca&ll Reinforcements". `&&` is retail's escape for a real ampersand.
	var out := ""
	var index := 0
	while index < text.length():
		if text[index] == "&":
			if index + 1 < text.length() and text[index + 1] == "&":
				out += "&"
				index += 2
				continue
			index += 1
			continue
		out += text[index]
		index += 1
	return out
