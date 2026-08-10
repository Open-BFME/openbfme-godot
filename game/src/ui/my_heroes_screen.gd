extends Panel
## CREATE-A-HERO: the retail front end, rebuilt to the shape of the real one.
##
## FOUR SCREENS, because retail has four (see reference/create a hero menu):
##
##   MY HEROES                the saved roster on the left with its portrait
##                            thumbnails, the selected hero standing in the
##                            middle, and his APPEARANCE / POWERS / STATS /
##                            AWARDS detail on the right.
##   SELECT HERO CLASS        a row of class archetype icons, a row of subclass
##   AND TYPE                 portraits, what each one is, and a live
##                            five-bar stat preview. CANCEL / NEXT.
##   CUSTOMIZE APPEARANCE     two tabs, as retail has: the garment pickers, and
##                            the name plus the attribute point pool.
##                            BACK / NEXT.
##   CUSTOMIZE HERO POWERS    the power lattice: one row per prerequisite chain,
##                            one column per required hero level (1/3/7/10),
##                            connectors along each chain, the numbered Current
##                            Powers list, and the running Build Cost.
##
## THE SCREEN IS COMPOSED OF CARDS, not of controls dropped on a backdrop. Every
## page fills the window at 1920x1080 and at 2560x1440 with a header, a row of
## framed cards, and ONE footer bar that carries the back/forward pair and the
## status line - which is why no page has a band of empty screen under it any
## more and why the two buttons never move between pages.
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
## asset ships unnoticed. The one thing never shown is a raw content id: an id
## with no string in the pack is spaced into words, and a description with no
## string is left out rather than printed as a symbol.

const CahHeroes = preload("res://src/content/cah_heroes.gd")
const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const SubObjects = preload("res://src/ui/cah_sub_objects.gd")

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

## The CUSTOMIZE APPEARANCE tabs, which retail also splits in two.
const CUSTOM_TAB_GARMENTS := 0
const CUSTOM_TAB_ATTRIBUTES := 1

## The four columns of the powers grid. Retail authors exactly these four
## `CreateAHeroUIMinimumLevel` values and the grid has exactly four columns.
const POWER_LEVEL_COLUMNS := [1, 3, 7, 10]

const ICON_SIZE := Vector2(64, 64)
const PORTRAIT_SIZE := Vector2(76, 76)
## What an icon slot widens to when the pack carries no art for it, so the name
## standing in for the icon is readable rather than clipped to a stub.
const ICON_FALLBACK_WIDTH := 148.0
## The roster row's thumbnail, which is the subclass's own HICAH* button art.
const ROSTER_ICON_SIZE := Vector2i(44, 44)
## The window margin the screen's header and columns sit inside.
const MARGIN := Vector2(46, 26)
## HOW THE WINDOW IS DIVIDED. Retail stands the hero in roughly the left third
## and gives the panel being edited the rest. Both columns EXPAND, so the split
## is a proportion of whatever window the player has rather than a fixed 560px
## strip with everything else left over to the preview.
const PREVIEW_STRETCH := 1.0
const CONTROLS_STRETCH := 1.7
const PREVIEW_MIN_WIDTH := 330.0
const CONTROLS_MIN_WIDTH := 560.0
## How wide the pages themselves are allowed to get, however wide the window is.
## Retail draws this screen in a 4:3 panel on a full-bleed background; letting
## the cards have all 2560px of a modern window stretches five stat bars across
## a metre of desk.
const MAX_CONTENT_WIDTH := 1860.0
## The hero's card beside the powers list.
const POWERS_PREVIEW_HEIGHT := 200.0
## The powers lattice. The row label carries a whole chain name on two lines, so
## it is wide enough to never clip; the cells are big enough to read a retail
## 64px button in.
const POWER_LABEL_WIDTH := 250.0
const POWER_CELL_SIZE := Vector2(96, 62)
## Where the hero faces when a page opens. The converted skins stand with their
## back to +Z, so the preview is turned round to face the camera rather than
## showing the player the back of a helmet he is trying to choose.
const PREVIEW_DEFAULT_YAW := 270.0
const PREVIEW_DRAG_DEGREES_PER_PIXEL := 0.45

var _system: Dictionary = {}
var _profiles: Array[Dictionary] = []
var _selected_class := 0
var _selected_sub := 0
var _attributes: Dictionary = {}
var _appearance: Dictionary = {}
var _powers: Array = []
var _awards: Array = []
var _active_tab := TAB_STATS
var _active_custom_tab := CUSTOM_TAB_GARMENTS
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
var _page_stack: Control
var _footer_clamp: Control
var _next_button: Button
var _breadcrumb: HBoxContainer
var _breadcrumb_chips: Array[Label] = []
var _tab_buttons: Array[Button] = []
var _tab_panels: Array[Control] = []
var _custom_tab_buttons: Array[Button] = []
var _custom_tab_panels: Array[Control] = []
var _class_icon_row: HBoxContainer
var _sub_icon_row: HBoxContainer
var _class_title: Label
var _class_description: Label
var _sub_title: Label
var _sub_description: Label
var _class_stat_bars: VBoxContainer
var _appearance_rows: VBoxContainer
var _appearance_summary: VBoxContainer
var _points_label: Label
var _power_grid: GridContainer
var _power_grid_wrap: Control
var _power_links: Control
var _power_scroll: ScrollContainer
var _current_powers: VBoxContainer
var _build_cost_label: Label
var _slots_label: Label
var _power_hint: Label
var _preview_column: VBoxContainer
var _select_preview_slot: VBoxContainer
var _class_preview_slot: VBoxContainer
var _attributes_preview_slot: VBoxContainer
var _powers_preview_slot: VBoxContainer
var _preview_host: SubViewportContainer
var _preview_viewport: SubViewport
var _preview_root: Node3D
var _preview_pivot: Node3D
var _preview_camera: Camera3D
var _preview_model: Node3D
var _preview_note: Label
var _preview_caption: Label
var _backdrop_art: TextureRect
var _detail_portrait: TextureRect
var _detail_stats: VBoxContainer
var _detail_name: Label
var _detail_type: Label
var _awards_list: ItemList
var _detail_powers: VBoxContainer

var _attribute_sliders: Dictionary = {}
var _attribute_values: Dictionary = {}
var _preview_yaw := PREVIEW_DEFAULT_YAW
var _preview_dragging := false
var _loaded_model_id := ""
var _garment_status := ""


func _ready() -> void:
	theme = ThemeScript.create_theme(null)
	_content_db = get_node_or_null("/root/ContentDB")
	_ensure_built()
	refresh()


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


func set_backdrop_texture(texture: Texture2D) -> void:
	## The shell's OWN menu backdrop, handed over rather than re-chosen here.
	##
	## `main_menu.gd` already resolves one - the converted retail shell art when a
	## pack carries it, otherwise the local menu plates it cycles through - and a
	## second selection in this file would be a second answer to the same
	## question, free to disagree with the menu the player just came from.
	_ensure_built()
	if _backdrop_art == null:
		return
	_backdrop_art.texture = texture
	_backdrop_art.visible = texture != null


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


func garment_status() -> String:
	## What the preview is doing about garments, as one line the screen prints
	## and a runner can assert on: "mapped", "unmapped", "no-model" or "".
	return _garment_status


func _reset_working_loadout() -> void:
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	_attributes = CahHeroes.default_attributes(sub_row)
	_appearance = CahHeroes.default_appearance(sub_row)
	_powers = CahHeroes.default_powers(_system, _selected_class)
	_awards = []


# --------------------------------------------------------------------- layout


func _build() -> void:
	## THE SCREEN IS THE WHOLE WINDOW, and it paints its own ground.
	##
	## `main_menu.gd` anchors this FULL_RECT and takes its own furniture down (see
	## `FULL_WINDOW_PAGES`), so everything below is laid out against the window
	## rather than against a flyout's rectangle.
	mouse_filter = Control.MOUSE_FILTER_STOP
	clip_contents = true

	_backdrop_art = TextureRect.new()
	_backdrop_art.name = "Backdrop"
	_backdrop_art.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_backdrop_art.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_backdrop_art.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	_backdrop_art.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_backdrop_art.visible = false
	add_child(_backdrop_art)

	# The art is a landscape, not a background for small text. The scrim is what
	# makes twenty-odd rows of numbers legible on top of one.
	var scrim := ColorRect.new()
	scrim.name = "Scrim"
	scrim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	scrim.color = Color(0.02, 0.03, 0.05, 0.66)
	scrim.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(scrim)

	var root := VBoxContainer.new()
	root.name = "Root"
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.offset_left = MARGIN.x
	root.offset_top = MARGIN.y
	root.offset_right = -MARGIN.x
	root.offset_bottom = -MARGIN.y
	root.add_theme_constant_override("separation", 10)
	add_child(root)

	_build_header(root)

	# THE PAGES ARE CENTRED AND CAPPED, over a backdrop that is not.
	# `_clamp_page_width` keeps the cards together in the middle; the backdrop and
	# the header still span everything.
	_page_stack = Control.new()
	_page_stack.name = "Pages"
	# A BACKSTOP. Every page below is built to fit, but a pack with a longer
	# class name or a taller card must never be able to draw through the footer
	# bar, which is what a 1920x1080 window caught the summary card doing.
	_page_stack.clip_contents = true
	_page_stack.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_page_stack.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	root.add_child(_page_stack)
	var stack := _page_stack

	_pages = [
		_build_select_page(stack),
		_build_class_page(stack),
		_build_attributes_page(stack),
		_build_powers_page(stack),
	]

	_build_footer(root)
	resized.connect(_clamp_page_width)
	_clamp_page_width()

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


func _build_header(root: VBoxContainer) -> void:
	title_label = Label.new()
	title_label.name = "Title"
	title_label.text = "CREATE-A-HERO"
	title_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title_label.add_theme_font_size_override("font_size", 40)
	title_label.add_theme_color_override("font_color", Color(0.93, 0.88, 0.68, 0.98))
	title_label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.85))
	title_label.add_theme_constant_override("shadow_offset_x", 2)
	title_label.add_theme_constant_override("shadow_offset_y", 2)
	root.add_child(title_label)

	section_label = Label.new()
	section_label.name = "Section"
	section_label.text = "MY HEROES"
	section_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	section_label.add_theme_font_size_override("font_size", 19)
	section_label.add_theme_color_override("font_color", Color(0.74, 0.84, 0.72, 0.92))
	root.add_child(section_label)

	# THE THREE STEPS OF MAKING A HERO, so the player can see where they are and
	# how much is left. Retail's own flow is class -> appearance -> powers and
	# nothing in the old screen said so.
	_breadcrumb = HBoxContainer.new()
	_breadcrumb.name = "Breadcrumb"
	_breadcrumb.alignment = BoxContainer.ALIGNMENT_CENTER
	_breadcrumb.add_theme_constant_override("separation", 6)
	root.add_child(_breadcrumb)
	_breadcrumb_chips.clear()
	var steps := ["1  CLASS & TYPE", "2  APPEARANCE", "3  POWERS"]
	for index in range(steps.size()):
		if index > 0:
			var arrow := Label.new()
			arrow.text = "›"
			arrow.add_theme_color_override("font_color", Color(0.55, 0.62, 0.48, 0.7))
			_breadcrumb.add_child(arrow)
		var chip := Label.new()
		chip.text = String(steps[index])
		chip.add_theme_font_size_override("font_size", 15)
		_breadcrumb.add_child(chip)
		_breadcrumb_chips.append(chip)

	var rule := HSeparator.new()
	rule.name = "HeaderRule"
	root.add_child(rule)


func _build_footer(root: VBoxContainer) -> void:
	## ONE footer for the whole screen.
	##
	## Every page used to carry its own BACK/NEXT pair inside its own column, so
	## the pair moved, changed width and left a band of dead screen under short
	## pages. Anchored here it is the same bar in the same place on all four, and
	## the status line finally has somewhere to live that is not the bottom-left
	## corner of the window.
	_footer_clamp = Control.new()
	_footer_clamp.name = "FooterClamp"
	_footer_clamp.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	_footer_clamp.custom_minimum_size = Vector2(0, 62)
	root.add_child(_footer_clamp)

	var bar := PanelContainer.new()
	bar.name = "Footer"
	bar.theme_type_variation = "CahCard"
	bar.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_footer_clamp.add_child(bar)

	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 16)
	bar.add_child(row)

	back_button = Button.new()
	back_button.name = "Back"
	back_button.text = "MAIN MENU"
	back_button.custom_minimum_size = Vector2(230, 38)
	back_button.pressed.connect(_on_footer_back)
	row.add_child(back_button)

	status_label = Label.new()
	status_label.name = "Status"
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	status_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	status_label.add_theme_font_size_override("font_size", 15)
	row.add_child(status_label)

	_next_button = Button.new()
	_next_button.name = "Next"
	_next_button.theme_type_variation = "PrimaryButton"
	_next_button.text = "CREATE NEW HERO"
	_next_button.custom_minimum_size = Vector2(230, 38)
	_next_button.pressed.connect(_on_footer_next)
	row.add_child(_next_button)

	save_button = Button.new()
	save_button.name = "SaveHero"
	save_button.theme_type_variation = "PrimaryButton"
	save_button.text = "DONE"
	save_button.custom_minimum_size = Vector2(230, 38)
	save_button.visible = false
	save_button.pressed.connect(_on_save_pressed)
	row.add_child(save_button)


# ------------------------------------------------------------------ page 1


func _build_select_page(parent: Control) -> Control:
	var page := HBoxContainer.new()
	page.name = "SelectHero"
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page.add_theme_constant_override("separation", 14)
	parent.add_child(page)

	# LEFT: the saved roster.
	var roster_card := _card("SAVED HEROES")
	var roster_host := roster_card.get_parent() as Control
	roster_host.custom_minimum_size = Vector2(380, 0)
	roster_host.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	roster_host.size_flags_stretch_ratio = 1.0
	page.add_child(roster_host)

	var well := PanelContainer.new()
	well.theme_type_variation = "CahCardSunken"
	well.size_flags_vertical = Control.SIZE_EXPAND_FILL
	roster_card.add_child(well)
	hero_list = ItemList.new()
	hero_list.name = "HeroList"
	hero_list.fixed_icon_size = ROSTER_ICON_SIZE
	hero_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	hero_list.allow_reselect = true
	hero_list.add_theme_font_size_override("font_size", 17)
	hero_list.item_selected.connect(_on_hero_list_selected)
	well.add_child(hero_list)

	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 8)
	roster_card.add_child(buttons)
	new_hero_button = Button.new()
	new_hero_button.name = "NewHero"
	new_hero_button.text = "NEW HERO"
	new_hero_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	new_hero_button.pressed.connect(_on_new_hero_pressed)
	buttons.add_child(new_hero_button)
	delete_button = Button.new()
	delete_button.name = "DeleteHero"
	delete_button.text = "DELETE"
	delete_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	delete_button.pressed.connect(func() -> void: delete_selected())
	buttons.add_child(delete_button)

	# MIDDLE: the hero himself, which is the centre of every retail
	# Create-a-Hero screen.
	_select_preview_slot = VBoxContainer.new()
	_select_preview_slot.custom_minimum_size = Vector2(PREVIEW_MIN_WIDTH, 0)
	_select_preview_slot.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_select_preview_slot.size_flags_stretch_ratio = 1.25
	_select_preview_slot.size_flags_vertical = Control.SIZE_EXPAND_FILL
	page.add_child(_select_preview_slot)
	_build_preview()
	_select_preview_slot.add_child(_preview_column)

	# RIGHT: the tabs and the detail they swap.
	var right := VBoxContainer.new()
	right.custom_minimum_size = Vector2(380, 0)
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_stretch_ratio = 1.15
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 8)
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
		tab.add_theme_font_size_override("font_size", 16)
		tab.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var tab_id := int(entry["id"])
		tab.pressed.connect(func() -> void: _show_tab(tab_id))
		tabs.add_child(tab)
		_tab_buttons.append(tab)

	var detail_card := _card("")
	var detail_host := detail_card.get_parent() as Control
	detail_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(detail_host)
	var detail := Control.new()
	detail.size_flags_vertical = Control.SIZE_EXPAND_FILL
	detail.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	detail_card.add_child(detail)
	_tab_panels = [
		_build_detail_appearance(detail),
		_build_detail_powers(detail),
		_build_detail_stats(detail),
		_build_detail_awards(detail),
	]
	return page


func _build_detail_appearance(parent: Control) -> Control:
	var panel := VBoxContainer.new()
	panel.name = "DetailAppearance"
	panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	panel.add_theme_constant_override("separation", 6)
	parent.add_child(panel)
	_detail_portrait = TextureRect.new()
	_detail_portrait.custom_minimum_size = Vector2(160, 160)
	_detail_portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_detail_portrait.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	panel.add_child(_detail_portrait)
	_detail_name = Label.new()
	_detail_name.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_detail_name.add_theme_font_size_override("font_size", 22)
	_detail_name.add_theme_color_override("font_color", Color(0.93, 0.88, 0.68, 0.98))
	panel.add_child(_detail_name)
	_detail_type = Label.new()
	_detail_type.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_detail_type.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_detail_type.add_theme_font_size_override("font_size", 15)
	_detail_type.add_theme_color_override("font_color", Color(0.74, 0.82, 0.68, 0.9))
	panel.add_child(_detail_type)
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
	panel.add_child(_heading("CURRENT POWERS"))
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
	panel.add_child(_heading("HERO STATISTICS"))
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	panel.add_child(scroll)
	var stats_gutter := MarginContainer.new()
	stats_gutter.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	stats_gutter.add_theme_constant_override("margin_right", 14)
	scroll.add_child(stats_gutter)
	stats_bars = VBoxContainer.new()
	stats_bars.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	stats_bars.add_theme_constant_override("separation", 6)
	stats_gutter.add_child(stats_bars)
	return panel


func _build_detail_awards(parent: Control) -> Control:
	var panel := VBoxContainer.new()
	panel.name = "DetailAwards"
	panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	panel.visible = false
	parent.add_child(panel)
	var hint := Label.new()
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	hint.add_theme_font_size_override("font_size", 14)
	hint.text = "Awards unlock through play; the ones this hero has earned are ticked."
	panel.add_child(hint)
	_awards_list = ItemList.new()
	_awards_list.select_mode = ItemList.SELECT_MULTI
	_awards_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_awards_list.multi_selected.connect(func(_i: int, _s: bool) -> void: _sync_awards_from_list())
	panel.add_child(_awards_list)
	return panel


# ------------------------------------------------------------------ page 2


func _build_class_page(parent: Control) -> Control:
	var page := HBoxContainer.new()
	page.name = "SelectClass"
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page.visible = false
	page.add_theme_constant_override("separation", 14)
	parent.add_child(page)

	# The hero stands here. It is the same hero on all four pages, so the ONE
	# preview is reparented into whichever page is showing rather than each page
	# owning a second copy of the 3D scene.
	_class_preview_slot = VBoxContainer.new()
	_class_preview_slot.custom_minimum_size = Vector2(PREVIEW_MIN_WIDTH, 0)
	_class_preview_slot.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_class_preview_slot.size_flags_stretch_ratio = PREVIEW_STRETCH
	_class_preview_slot.size_flags_vertical = Control.SIZE_EXPAND_FILL
	page.add_child(_class_preview_slot)

	var right := VBoxContainer.new()
	right.custom_minimum_size = Vector2(CONTROLS_MIN_WIDTH, 0)
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_stretch_ratio = CONTROLS_STRETCH
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 10)
	page.add_child(right)

	var class_card := _card("CHOOSE A CLASS")
	right.add_child(class_card.get_parent())
	_class_icon_row = HBoxContainer.new()
	_class_icon_row.alignment = BoxContainer.ALIGNMENT_CENTER
	_class_icon_row.add_theme_constant_override("separation", 10)
	class_card.add_child(_class_icon_row)
	_class_title = Label.new()
	_class_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_class_title.add_theme_font_size_override("font_size", 22)
	_class_title.add_theme_color_override("font_color", Color(0.93, 0.88, 0.68, 0.98))
	class_card.add_child(_class_title)
	_class_description = Label.new()
	_class_description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_class_description.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_class_description.add_theme_font_size_override("font_size", 15)
	class_card.add_child(_class_description)

	var type_card := _card("CHOOSE A TYPE")
	right.add_child(type_card.get_parent())
	_sub_icon_row = HBoxContainer.new()
	_sub_icon_row.alignment = BoxContainer.ALIGNMENT_CENTER
	_sub_icon_row.add_theme_constant_override("separation", 10)
	type_card.add_child(_sub_icon_row)
	_sub_title = Label.new()
	_sub_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_sub_title.add_theme_font_size_override("font_size", 22)
	_sub_title.add_theme_color_override("font_color", Color(0.93, 0.88, 0.68, 0.98))
	type_card.add_child(_sub_title)
	_sub_description = Label.new()
	_sub_description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_sub_description.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_sub_description.add_theme_font_size_override("font_size", 15)
	type_card.add_child(_sub_description)

	var stat_card := _card("STARTING ATTRIBUTES")
	var stat_host := stat_card.get_parent() as Control
	stat_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(stat_host)
	var stat_scroll := ScrollContainer.new()
	stat_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	stat_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	stat_card.add_child(stat_scroll)
	var stat_gutter := MarginContainer.new()
	stat_gutter.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	stat_gutter.add_theme_constant_override("margin_right", 14)
	stat_scroll.add_child(stat_gutter)
	_class_stat_bars = VBoxContainer.new()
	_class_stat_bars.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_class_stat_bars.add_theme_constant_override("separation", 8)
	stat_gutter.add_child(_class_stat_bars)
	return page


# ------------------------------------------------------------------ page 3


func _build_attributes_page(parent: Control) -> Control:
	var page := HBoxContainer.new()
	page.name = "CustomizeAttributes"
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page.visible = false
	page.add_theme_constant_override("separation", 14)
	parent.add_child(page)

	_attributes_preview_slot = VBoxContainer.new()
	_attributes_preview_slot.custom_minimum_size = Vector2(PREVIEW_MIN_WIDTH, 0)
	_attributes_preview_slot.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_attributes_preview_slot.size_flags_stretch_ratio = PREVIEW_STRETCH
	_attributes_preview_slot.size_flags_vertical = Control.SIZE_EXPAND_FILL
	page.add_child(_attributes_preview_slot)

	var right := VBoxContainer.new()
	right.custom_minimum_size = Vector2(CONTROLS_MIN_WIDTH, 0)
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_stretch_ratio = CONTROLS_STRETCH
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 8)
	page.add_child(right)

	# TWO TABS, as retail has: what he is wearing, and who he is.
	var tabs := HBoxContainer.new()
	tabs.add_theme_constant_override("separation", 4)
	right.add_child(tabs)
	_custom_tab_buttons.clear()
	for entry in [
		{"id": CUSTOM_TAB_GARMENTS, "label": "GARMENTS & WEAPONS"},
		{"id": CUSTOM_TAB_ATTRIBUTES, "label": "NAME & ATTRIBUTES"},
	]:
		var tab := Button.new()
		tab.text = String(entry["label"])
		tab.toggle_mode = true
		tab.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		tab.add_theme_font_size_override("font_size", 17)
		var tab_id := int(entry["id"])
		tab.pressed.connect(func() -> void: _show_custom_tab(tab_id))
		tabs.add_child(tab)
		_custom_tab_buttons.append(tab)

	# THE TAB BODY HUGS ITS CONTENT and the summary takes the rest. Seven garment
	# rows given a 1440-tall window's worth of slack left half the page empty
	# under them; the hero's live numbers are what belongs there, because they
	# are what the player is watching while he dresses him.
	var body := VBoxContainer.new()
	body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_child(body)
	_custom_tab_panels = [
		_build_garment_panel(body),
		_build_attribute_panel(body),
	]

	var summary := _card("THIS HERO")
	var summary_host := summary.get_parent() as Control
	summary_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_child(summary_host)
	var summary_scroll := ScrollContainer.new()
	summary_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	summary_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	summary.add_child(summary_scroll)
	var summary_gutter := MarginContainer.new()
	summary_gutter.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	summary_gutter.add_theme_constant_override("margin_right", 14)
	summary_scroll.add_child(summary_gutter)
	_appearance_summary = VBoxContainer.new()
	_appearance_summary.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_appearance_summary.add_theme_constant_override("separation", 6)
	summary_gutter.add_child(_appearance_summary)
	return page


func _build_garment_panel(parent: Control) -> Control:
	var host := VBoxContainer.new()
	host.name = "GarmentPanel"
	host.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(host)
	var card := _card("EQUIPMENT")
	host.add_child(card.get_parent())
	_appearance_rows = VBoxContainer.new()
	_appearance_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_appearance_rows.add_theme_constant_override("separation", 10)
	card.add_child(_appearance_rows)
	# NO COLOUR TABS. Retail's three colour swatches recolour the house-colour
	# channel of the skin's textures, and the mounted packs carry no house-colour
	# mask for the Create-a-Hero skins - so the buttons could only ever have
	# repainted the whole hero or done nothing at all, which is what they did.
	# They are gone until the pack ships the mask; a dead control is worse than
	# an absent one.
	_preview_caption = Label.new()
	_preview_caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_preview_caption.add_theme_font_size_override("font_size", 14)
	_preview_caption.add_theme_color_override("font_color", Color(0.86, 0.78, 0.52, 0.92))
	card.add_child(_preview_caption)
	return host


func _build_attribute_panel(parent: Control) -> Control:
	var host := VBoxContainer.new()
	host.name = "AttributePanel"
	host.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	host.visible = false
	host.add_theme_constant_override("separation", 8)
	parent.add_child(host)

	var name_card := _card("HERO NAME")
	host.add_child(name_card.get_parent())
	name_edit = LineEdit.new()
	name_edit.name = "HeroName"
	name_edit.placeholder_text = "Name your hero"
	name_edit.max_length = CahHeroes.MAX_NAME_LENGTH
	name_edit.alignment = HORIZONTAL_ALIGNMENT_CENTER
	name_edit.add_theme_font_size_override("font_size", 22)
	name_edit.text_changed.connect(func(_text: String) -> void: _update_detail_stats())
	name_card.add_child(name_edit)

	var card := _card("ATTRIBUTE POINTS")
	host.add_child(card.get_parent())
	_points_label = Label.new()
	_points_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_points_label.add_theme_font_size_override("font_size", 18)
	card.add_child(_points_label)
	budget_label = _points_label

	attribute_rows = VBoxContainer.new()
	attribute_rows.name = "AttributeRows"
	attribute_rows.add_theme_constant_override("separation", 10)
	card.add_child(attribute_rows)

	var tools := HBoxContainer.new()
	tools.add_theme_constant_override("separation", 8)
	card.add_child(tools)
	var reset := Button.new()
	reset.text = "RESET"
	reset.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	reset.pressed.connect(_on_reset_attributes)
	tools.add_child(reset)
	var recommend := Button.new()
	recommend.text = "RECOMMENDED"
	recommend.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	recommend.tooltip_text = "Return to the loadout this class was authored with."
	recommend.pressed.connect(_on_reset_attributes)
	tools.add_child(recommend)
	return host


# ------------------------------------------------------------------ page 4


func _build_powers_page(parent: Control) -> Control:
	var page := HBoxContainer.new()
	page.name = "CustomizePowers"
	page.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	page.visible = false
	page.add_theme_constant_override("separation", 14)
	parent.add_child(page)

	# LEFT: the lattice of power chains.
	var lattice := _card("CHOOSE HERO POWERS")
	var lattice_host := lattice.get_parent() as Control
	lattice_host.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	lattice_host.size_flags_stretch_ratio = 2.1
	lattice_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
	page.add_child(lattice_host)

	var well := PanelContainer.new()
	well.theme_type_variation = "CahCardSunken"
	well.size_flags_vertical = Control.SIZE_EXPAND_FILL
	lattice.add_child(well)
	_power_scroll = ScrollContainer.new()
	_power_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_power_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	well.add_child(_power_scroll)
	# THE GRID AND THE CONNECTORS SHARE ONE RECTANGLE. The chevrons that say
	# "this one chains from the one on its left" are drawn over the grid's own
	# cell rectangles, so they cannot drift out of line with the cells the way a
	# second, independently laid-out strip of arrows would.
	_power_grid_wrap = Control.new()
	_power_grid_wrap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_power_scroll.add_child(_power_grid_wrap)
	_power_grid = GridContainer.new()
	_power_grid.columns = POWER_LEVEL_COLUMNS.size() + 1
	_power_grid.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_power_grid.add_theme_constant_override("h_separation", 18)
	_power_grid.add_theme_constant_override("v_separation", 6)
	_power_grid.sort_children.connect(_on_power_grid_sorted)
	_power_grid_wrap.add_child(_power_grid)
	_power_links = PowerLinks.new()
	_power_links.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_power_links.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_power_grid_wrap.add_child(_power_links)

	_power_hint = Label.new()
	_power_hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_power_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_power_hint.add_theme_font_size_override("font_size", 15)
	_power_hint.add_theme_color_override("font_color", Color(0.88, 0.80, 0.54, 0.95))
	lattice.add_child(_power_hint)

	# RIGHT: the hero, the numbered Current Powers list and the running cost.
	var right := VBoxContainer.new()
	right.custom_minimum_size = Vector2(380, 0)
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.size_flags_stretch_ratio = 1.0
	right.size_flags_vertical = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 8)
	page.add_child(right)

	_powers_preview_slot = VBoxContainer.new()
	_powers_preview_slot.custom_minimum_size = Vector2(0, POWERS_PREVIEW_HEIGHT)
	_powers_preview_slot.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_powers_preview_slot.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_powers_preview_slot.size_flags_stretch_ratio = 1.1
	right.add_child(_powers_preview_slot)

	var list_card := _card("CURRENT POWERS")
	var list_host := list_card.get_parent() as Control
	list_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
	list_host.size_flags_stretch_ratio = 1.4
	right.add_child(list_host)

	var head := HBoxContainer.new()
	list_card.add_child(head)
	_slots_label = Label.new()
	_slots_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_slots_label.add_theme_font_size_override("font_size", 15)
	head.add_child(_slots_label)
	var reset := Button.new()
	reset.text = "RESET"
	reset.pressed.connect(_on_reset_powers)
	head.add_child(reset)

	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	list_card.add_child(scroll)
	_current_powers = VBoxContainer.new()
	_current_powers.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_current_powers.add_theme_constant_override("separation", 6)
	scroll.add_child(_current_powers)

	var cost_row := HBoxContainer.new()
	list_card.add_child(cost_row)
	var cost_caption := Label.new()
	cost_caption.text = "BUILD COST"
	cost_caption.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cost_caption.add_theme_font_size_override("font_size", 18)
	cost_row.add_child(cost_caption)
	_build_cost_label = Label.new()
	_build_cost_label.add_theme_font_size_override("font_size", 24)
	_build_cost_label.add_theme_color_override("font_color", Color(0.93, 0.88, 0.68, 0.98))
	cost_row.add_child(_build_cost_label)
	return page


# ------------------------------------------------------------------- furniture


func _card(title: String) -> VBoxContainer:
	## One framed card. Returns the CONTENT box; the caller adds
	## `content.get_parent()` to its own column.
	var frame := PanelContainer.new()
	frame.theme_type_variation = "CahCard"
	var content := VBoxContainer.new()
	content.add_theme_constant_override("separation", 8)
	frame.add_child(content)
	if title != "":
		content.add_child(_heading(title))
	return content


func _heading(text: String) -> Label:
	var label := Label.new()
	label.text = text
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.add_theme_font_size_override("font_size", 15)
	label.add_theme_color_override("font_color", Color(0.72, 0.82, 0.62, 0.92))
	return label


func _clamp_page_width() -> void:
	if _page_stack == null:
		return
	var width := minf(
		maxf(size.x - 2.0 * MARGIN.x, CONTROLS_MIN_WIDTH + PREVIEW_MIN_WIDTH), MAX_CONTENT_WIDTH
	)
	_page_stack.custom_minimum_size.x = width
	if _footer_clamp != null:
		_footer_clamp.custom_minimum_size.x = width


# ------------------------------------------------------------------ page flow


func _show_page(page: int) -> void:
	_page = page
	for index in range(_pages.size()):
		_pages[index].visible = index == page
	_move_preview_to(page)
	_preview_yaw = PREVIEW_DEFAULT_YAW
	_apply_preview_yaw()
	save_button.visible = page == PAGE_POWERS
	_next_button.visible = page != PAGE_POWERS
	match page:
		PAGE_SELECT:
			section_label.text = "MY HEROES"
			back_button.text = "MAIN MENU"
			_next_button.text = "CREATE NEW HERO"
			_show_tab(_active_tab)
		PAGE_CLASS:
			section_label.text = "SELECT HERO CLASS AND TYPE"
			back_button.text = "CANCEL"
			_next_button.text = "NEXT"
		PAGE_ATTRIBUTES:
			section_label.text = "CUSTOMIZE APPEARANCE"
			back_button.text = "BACK"
			_next_button.text = "NEXT"
			_show_custom_tab(_active_custom_tab)
		PAGE_POWERS:
			section_label.text = "CUSTOMIZE HERO POWERS"
			back_button.text = "BACK"
	_update_breadcrumb()
	_update_preview()


func _update_breadcrumb() -> void:
	if _breadcrumb == null:
		return
	# The roster page is not a step of making a hero, so the trail is put away
	# rather than shown with nothing lit.
	_breadcrumb.visible = _page != PAGE_SELECT
	var current := _page - 1
	for index in range(_breadcrumb_chips.size()):
		var chip := _breadcrumb_chips[index]
		if index == current:
			chip.add_theme_color_override("font_color", Color(0.95, 0.90, 0.60, 1.0))
			chip.add_theme_font_size_override("font_size", 16)
		elif index < current:
			chip.add_theme_color_override("font_color", Color(0.70, 0.80, 0.62, 0.85))
			chip.add_theme_font_size_override("font_size", 15)
		else:
			chip.add_theme_color_override("font_color", Color(0.52, 0.58, 0.48, 0.6))
			chip.add_theme_font_size_override("font_size", 15)


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


func _show_custom_tab(tab_id: int) -> void:
	_active_custom_tab = tab_id
	for index in range(_custom_tab_buttons.size()):
		_custom_tab_buttons[index].button_pressed = index == tab_id
	for index in range(_custom_tab_panels.size()):
		_custom_tab_panels[index].visible = index == tab_id


func _on_footer_back() -> void:
	match _page:
		PAGE_SELECT: back_requested.emit()
		PAGE_CLASS: _show_page(PAGE_SELECT)
		PAGE_ATTRIBUTES: _show_page(PAGE_CLASS)
		PAGE_POWERS: _show_page(PAGE_ATTRIBUTES)


func _on_footer_next() -> void:
	match _page:
		PAGE_SELECT: _on_new_hero_pressed()
		PAGE_CLASS: _show_page(PAGE_ATTRIBUTES)
		PAGE_ATTRIBUTES: _show_page(PAGE_POWERS)


func _unhandled_key_input(event: InputEvent) -> void:
	## ENTER GOES ON, ESCAPE GOES BACK, on every page.
	##
	## Guarded on visibility because this screen is built once and then hidden
	## behind the shell: an invisible page must not eat the menu's own Escape.
	if not visible or not (event is InputEventKey) or not event.is_pressed() or event.is_echo():
		return
	var key := event as InputEventKey
	if key.keycode == KEY_ESCAPE:
		_on_footer_back()
		accept_event()
	elif key.keycode == KEY_ENTER or key.keycode == KEY_KP_ENTER:
		# A name being typed owns its own Enter.
		if name_edit != null and name_edit.has_focus():
			return
		if _page == PAGE_POWERS:
			if not save_button.disabled:
				_on_save_pressed()
		else:
			_on_footer_next()
		accept_event()


func _on_new_hero_pressed() -> void:
	_editing_hero_id = ""
	name_edit.text = ""
	_reset_working_loadout()
	_rebuild_all()
	_show_page(PAGE_CLASS)
	status_label.text = "Pick a class and a type, then set his appearance and his powers."


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


func _set_editor_enabled(enabled: bool) -> void:
	if name_edit != null:
		name_edit.editable = enabled
	for control in [class_option, sub_option, save_button, delete_button, new_hero_button, _next_button]:
		if control != null:
			control.disabled = not enabled


# ------------------------------------------------------------------- rebuilds


func _reload_hero_list() -> void:
	hero_list.clear()
	if _profiles.is_empty():
		# AN EMPTY LIST IS A STATE, not a blank box. Retail's roster starts empty
		# and the screen has to say what to do about it.
		hero_list.add_item("No heroes yet - press NEW HERO to make one.")
		hero_list.set_item_selectable(0, false)
		hero_list.set_item_disabled(0, true)
		return
	for profile in _profiles:
		var refusals: Array[String] = []
		if _system.is_empty():
			refusals.append("no class table is mounted")
		else:
			refusals = CahHeroes.validate_profile(_system, profile)
		var label := "%s   ·   %s" % [
			String(profile.get("name", "?")), _profile_type_label(profile)
		]
		if not refusals.is_empty():
			label += "   (unavailable: %s)" % refusals[0]
		hero_list.add_item(label)
		# Retail draws the hero's own HICAH* button art beside its name. When the
		# pack has none the row keeps the name and simply carries no thumbnail,
		# rather than a generic glyph that would make every hero look alike.
		var thumbnail := _icon_texture(_profile_button_image_id(profile))
		if thumbnail != null:
			hero_list.set_item_icon(hero_list.item_count - 1, thumbnail)


func _profile_button_image_id(profile: Dictionary) -> String:
	if _system.is_empty():
		return ""
	var sub_row := CahHeroes.sub_class_row(
		_system, int(profile.get("classIndex", -1)), int(profile.get("subClassIndex", -1))
	)
	return String(sub_row.get("buttonImageId", sub_row.get("iconImageId", "")))


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
	_clear_children(_class_icon_row)
	var registration: Dictionary = _system.get("registration", {}) as Dictionary
	for class_value in (registration.get("classes", []) as Array):
		var row := class_value as Dictionary
		var index := int(row.get("classIndex", -1))
		_class_icon_row.add_child(_icon_choice(
			String(row.get("iconImageId", "")),
			_readable(String(row.get("nameStringId", ""))),
			ICON_SIZE,
			index == _selected_class,
			func() -> void: set_class_selection(index, 0)
		))


func _rebuild_sub_icons() -> void:
	_clear_children(_sub_icon_row)
	var row := CahHeroes.class_row(_system, _selected_class)
	for sub_value in (row.get("subClasses", []) as Array):
		var sub := sub_value as Dictionary
		var index := int(sub.get("subClassIndex", -1))
		_sub_icon_row.add_child(_icon_choice(
			String(sub.get("iconImageId", "")),
			_readable(String(sub.get("nameStringId", ""))),
			PORTRAIT_SIZE,
			index == _selected_sub,
			func() -> void: set_class_selection(_selected_class, index)
		))


func _icon_choice(
	image_id: String, label: String, icon_size: Vector2, selected: bool, action: Callable
) -> Control:
	## One class or type: the retail icon over its own name.
	##
	## The name is UNDER the icon rather than only in a tooltip, because a row of
	## seven unlabelled crests is a memory test. When the pack has no art the
	## button carries the name itself and the caption is dropped, so the name is
	## never printed twice.
	var column := VBoxContainer.new()
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	column.add_theme_constant_override("separation", 4)
	var button := _icon_button(image_id, label, icon_size, selected)
	button.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	button.pressed.connect(action)
	column.add_child(button)
	if button.icon != null:
		var caption := Label.new()
		caption.text = label
		caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		caption.custom_minimum_size = Vector2(maxf(icon_size.x + 34.0, 110.0), 0)
		caption.add_theme_font_size_override("font_size", 13)
		caption.add_theme_color_override(
			"font_color",
			Color(0.95, 0.90, 0.60, 1.0) if selected else Color(0.76, 0.82, 0.70, 0.85)
		)
		column.add_child(caption)
	return column


func _icon_button(
	image_id: String, label: String, size: Vector2, selected: bool, fixed_width := false
) -> Button:
	## One retail icon, or a named refusal in its place.
	##
	## An icon the pack does not carry becomes a button captioned with what it
	## WOULD have been rather than a blank or a stand-in glyph, so a missing
	## atlas crop is visible on the screen instead of quietly looking fine.
	var button := Button.new()
	button.theme_type_variation = "CahIconSlot"
	button.toggle_mode = true
	button.button_pressed = selected
	button.custom_minimum_size = size
	button.tooltip_text = label
	var texture := _icon_texture(image_id)
	if texture != null:
		button.icon = texture
		button.expand_icon = true
		return button
	# NO ICON, SO THE NAME GETS THE ROOM THE ICON WOULD HAVE HAD. The three-letter
	# truncation this replaces turned a row of classes into "Her Arc Ist", which is
	# unreadable AND indistinguishable from a deliberate abbreviation.
	button.text = label
	button.tooltip_text = "%s\n(icon %s is not in the mounted pack)" % [label, image_id]
	# A grid cell is a COLUMN, and a column that grew to fit the longest power
	# name in it would drag its whole column out from under the level number that
	# labels it. Those get a wrapped caption drawn over the button instead (see
	# `_power_cell`); a free-standing class or type button takes the width its
	# name needs.
	if fixed_width:
		button.text = ""
		button.custom_minimum_size = size
	else:
		button.custom_minimum_size = Vector2(maxf(size.x, ICON_FALLBACK_WIDTH), size.y)
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
	_clear_children(attribute_rows)
	_attribute_sliders.clear()
	_attribute_values.clear()
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	for row_value in (sub_row.get("attributes", []) as Array):
		var row := row_value as Dictionary
		var group := String(row.get("groupName", ""))
		var low := int(row.get("minStep", 1))
		var high := int(row.get("maxStep", 20))
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 8)
		var label := Label.new()
		label.text = _attribute_label(group)
		label.custom_minimum_size = Vector2(120, 0)
		label.add_theme_font_size_override("font_size", 17)
		line.add_child(label)
		# Retail drives these with a left/right arrow pair; both are kept so the
		# step is reachable exactly as well as by dragging.
		var down := _stepper("◀")
		down.pressed.connect(
			func() -> void: set_attribute(group, maxi(low, int(_attributes.get(group, low)) - 1))
		)
		line.add_child(down)
		var slider := HSlider.new()
		slider.min_value = float(low)
		slider.max_value = float(high)
		slider.step = 1.0
		slider.value = float(_attributes.get(group, low))
		# A CAP, NOT AN EXPAND. Five sliders given a 2560px window's worth of
		# slack were a metre wide each and a single step moved the grabber by
		# less than its own width.
		slider.custom_minimum_size = Vector2(260, 0)
		slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		slider.size_flags_stretch_ratio = 1.0
		slider.value_changed.connect(func(value: float) -> void: set_attribute(group, int(value)))
		line.add_child(slider)
		var up := _stepper("▶")
		up.pressed.connect(
			func() -> void: set_attribute(group, mini(high, int(_attributes.get(group, low)) + 1))
		)
		line.add_child(up)
		var value_label := Label.new()
		value_label.custom_minimum_size = Vector2(76, 0)
		value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		value_label.add_theme_font_size_override("font_size", 17)
		line.add_child(value_label)
		attribute_rows.add_child(line)
		_attribute_sliders[group] = slider
		_attribute_values[group] = value_label


func _stepper(glyph: String) -> Button:
	var button := Button.new()
	button.theme_type_variation = "CahStepper"
	button.text = glyph
	button.custom_minimum_size = Vector2(38, 32)
	return button


func _rebuild_appearance_rows() -> void:
	_clear_children(_appearance_rows)
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	var choices: Dictionary = sub_row.get("appearanceChoices", {}) as Dictionary
	if choices.is_empty():
		var empty := Label.new()
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		empty.text = "This type has no appearance parts in the mounted pack."
		_appearance_rows.add_child(empty)
		return
	for group_value in choices.keys():
		var group := String(group_value)
		var upgrades: Array = choices[group_value] as Array
		if upgrades.is_empty():
			continue
		var labels := _appearance_option_labels(upgrades)
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 8)
		var label := Label.new()
		label.text = _appearance_group_label(group)
		label.custom_minimum_size = Vector2(180, 0)
		label.add_theme_font_size_override("font_size", 17)
		line.add_child(label)
		var left := _stepper("◀")
		line.add_child(left)
		var value_well := PanelContainer.new()
		value_well.theme_type_variation = "CahCardSunken"
		value_well.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line.add_child(value_well)
		var value_label := Label.new()
		value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		value_label.add_theme_font_size_override("font_size", 17)
		value_label.add_theme_color_override("font_color", Color(0.93, 0.88, 0.68, 0.98))
		value_well.add_child(value_label)
		var right := _stepper("▶")
		line.add_child(right)
		var count_label := Label.new()
		count_label.custom_minimum_size = Vector2(72, 0)
		count_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		count_label.add_theme_font_size_override("font_size", 14)
		count_label.add_theme_color_override("font_color", Color(0.70, 0.78, 0.64, 0.8))
		line.add_child(count_label)
		_appearance_rows.add_child(line)
		var apply := func(delta: int) -> void:
			var current := upgrades.find(String(_appearance.get(group, "")))
			if current < 0:
				current = 0
			var next := wrapi(current + delta, 0, upgrades.size())
			_appearance[group] = String(upgrades[next])
			value_label.text = String(labels[next])
			count_label.text = "%d / %d" % [next + 1, upgrades.size()]
			if delta != 0:
				_apply_preview_appearance()
		left.pressed.connect(func() -> void: apply.call(-1))
		right.pressed.connect(func() -> void: apply.call(1))
		apply.call(0)


func _appearance_group_label(group: String) -> String:
	## The garment slot's own retail label when the pack ships the string,
	## otherwise the de-namespaced group name - never the raw
	## `CreateAHero_ShoulderPlates`, and never `HelmetMenuLabel` either, which is
	## what resolving the label id through the id-tail fallback would print.
	for row_value in CahHeroes.appearance_groups(_system):
		var row := row_value as Dictionary
		if String(row.get("groupName", "")) == group:
			var label := _readable_prose(String(row.get("labelStringId", "")))
			if label != "":
				return label.trim_suffix(":")
	return _readable(group)


func _appearance_option_labels(upgrades: Array) -> Array[String]:
	## What the garment cycler prints for each option in one group.
	##
	## THE PART CODES ARE NOT NAMES. Retail names some bling for real - Gurthang,
	## Trollbane, Belthronding - and gives the rest a code: `CaptainOfGondor_CHB01`
	## is boot number one. With the pack's string table carrying neither, the
	## named ones are shown by name and the coded ones are numbered ("Style 1"),
	## because "Captain Of Gondor CHB01" on a menu is an internal id printed at
	## the player. The `n / m` counter beside the row is what keeps the position
	## in the list unambiguous either way.
	var out: Array[String] = []
	var style := 0
	for upgrade_value in upgrades:
		var upgrade := String(upgrade_value)
		var row := _appearance_option_row(upgrade)
		var resolved := _readable_prose(String(row.get("nameStringId", "")))
		if resolved != "":
			out.append(resolved)
			continue
		var tail := CahHeroes.option_label_for_upgrade(_system, upgrade)
		if tail.begins_with("No") and tail.length() > 2 and tail[2] == tail[2].to_upper():
			out.append("None")
			continue
		style += 1
		var words := tail.split(" ", false)
		if words.size() > 0 and _is_part_code(String(words[words.size() - 1])):
			out.append("Style %d" % style)
		else:
			out.append(_spaced_identifier(tail))
	return out


func _appearance_option_row(upgrade: String) -> Dictionary:
	for row_value in CahHeroes.appearance_options(_system):
		var row := row_value as Dictionary
		if String(row.get("upgradeName", "")) == upgrade:
			return row
	return {}


static func _is_part_code(word: String) -> bool:
	## `CHB01`, `CHBOD01`, `CHSP04` - all capitals with a number in them, which
	## is an artist's part code rather than anything a player should read.
	## `Gurthang` and `TrollBane` carry lower case and survive.
	if word.length() < 4:
		return false
	var has_digit := false
	for character in word:
		if character >= "0" and character <= "9":
			has_digit = true
		elif character != character.to_upper() or character == character.to_lower():
			return false
	return has_digit


func _rebuild_power_grid() -> void:
	_clear_children(_power_grid)
	if _power_links != null:
		(_power_links as PowerLinks).links.clear()
		_power_links.queue_redraw()
	var trees := CahHeroes.power_trees_for_class(_system, _selected_class)
	if trees.is_empty():
		var empty := Label.new()
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		empty.text = "No powers are offered to this class in the mounted pack."
		_power_grid.add_child(empty)
		return
	# THE TIER KEY IS THE FIRST ROW OF THE GRID, not a strip beside it. As a
	# sibling its cells were sized independently and drifted out of line with the
	# columns they label; inside the grid they align by construction. It leads
	# rather than trails because a key under a scrolling lattice is a key the
	# player has to go looking for.
	_append_power_level_header()
	var selected := {}
	for power_value in _powers:
		selected[String(power_value)] = true
	var links: Array = []
	for tree_value in trees:
		var tree := tree_value as Dictionary
		var label := Label.new()
		label.text = _chain_label(String(tree.get("labelStringId", tree.get("familyId", "?"))))
		label.custom_minimum_size = Vector2(POWER_LABEL_WIDTH, 0)
		# NO CLIPPING. "Command Create A Hero Shield" used to end at "Shielc".
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		label.add_theme_font_size_override("font_size", 16)
		_power_grid.add_child(label)
		var by_level := {}
		for level_value in (tree.get("levels", []) as Array):
			by_level[int((level_value as Dictionary).get("requiredHeroLevel", 1))] = level_value
		var previous_cell: Control = null
		var previous_id := ""
		for column_level in POWER_LEVEL_COLUMNS:
			if not by_level.has(column_level):
				var blank := Control.new()
				blank.custom_minimum_size = POWER_CELL_SIZE
				_power_grid.add_child(blank)
				continue
			var level := by_level[column_level] as Dictionary
			var power_id := String(level.get("powerId", ""))
			var cell := _power_cell(level, int(column_level), selected)
			_power_grid.add_child(cell)
			var prerequisite := String(level.get("prerequisitePowerId", ""))
			if previous_cell != null and prerequisite != "" and prerequisite == previous_id:
				links.append({
					"from": previous_cell, "to": cell, "on": selected.has(prerequisite)
				})
			previous_cell = cell
			previous_id = power_id
	if _power_links != null:
		(_power_links as PowerLinks).links = links
		_power_links.queue_redraw()


func _power_cell(level: Dictionary, column_level: int, selected: Dictionary) -> Control:
	## One power in the lattice: the retail button art, its tier, and the whole
	## of what it is in the tooltip.
	var power_id := String(level.get("powerId", ""))
	var name := _readable(String(level.get("nameStringId", power_id)))
	var cost := int(level.get("costIfSelected", 0))
	var prerequisite := String(level.get("prerequisitePowerId", ""))
	var is_selected := selected.has(power_id)
	var blocked := prerequisite != "" and not selected.has(prerequisite)

	var cell := _icon_button(
		String(level.get("buttonImageId", "")), name, POWER_CELL_SIZE, is_selected, true
	)
	if cell.icon == null:
		# NO ART, SO THE NAME IS DRAWN OVER THE SLOT AND WRAPPED. A fixed-width
		# cell captioned by the button's own `text` clipped "Mount / Dismount"
		# to "Mou"; a wrapped label inside the slot says the whole thing.
		var caption := Label.new()
		caption.text = name
		caption.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		caption.offset_left = 4
		caption.offset_right = -4
		caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		caption.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		caption.mouse_filter = Control.MOUSE_FILTER_IGNORE
		caption.add_theme_font_size_override("font_size", 12)
		cell.add_child(caption)

	# The tier badge. The column already says it; the badge says it again on the
	# cell itself so a power dragged out of context in a screenshot still reads.
	var badge := Label.new()
	badge.text = str(column_level)
	badge.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	badge.offset_left = -22
	badge.offset_top = 1
	badge.offset_right = -3
	badge.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	badge.mouse_filter = Control.MOUSE_FILTER_IGNORE
	badge.add_theme_font_size_override("font_size", 12)
	badge.add_theme_color_override("font_color", Color(0.92, 0.84, 0.52, 0.85))
	cell.add_child(badge)

	# NEEDS ITS PREREQUISITE is a state, not a refusal: the cell stays pressable
	# so the player can find out why, and says why in the hint when they do.
	cell.modulate = Color(1, 1, 1, 0.5) if blocked and not is_selected else Color(1, 1, 1, 1)

	var description := _readable_prose(String(level.get("descriptionStringId", "")))
	var tooltip := name
	if description != "":
		tooltip += "\n%s" % description
	tooltip += "\n\nUnlocks at hero level %d\n+%d to build cost" % [column_level, cost]
	if prerequisite != "":
		tooltip += "\nRequires %s" % _readable(_power_name_of(prerequisite))
	if is_selected:
		tooltip += "\n\nSelected - click to remove"
	cell.tooltip_text = tooltip
	cell.pressed.connect(func() -> void: _toggle_power(power_id))
	return cell


func _chain_label(string_id: String) -> String:
	## THE NAME OF ONE POWER CHAIN, with the scaffolding of a command id taken
	## off it.
	##
	## Retail names a command button `Command...Level1Name`, and with no string
	## for it in the pack the id-tail fallback prints "Command Create A Hero
	## Shield Crush Level1 Name" at the player - words, but not a name. The
	## leading `Command` and the trailing `LevelN` / `Name` are id scaffolding in
	## every case, so they come off; whatever the author actually called the
	## power is what is left.
	var label := _readable(string_id)
	if label == "":
		return ""
	var words := label.split(" ", false)
	var kept: Array[String] = []
	for word_value in words:
		kept.append(String(word_value))
	while kept.size() > 1:
		var last := kept[kept.size() - 1]
		if last == "Name" or (last.begins_with("Level") and last.length() > 5):
			kept.remove_at(kept.size() - 1)
			continue
		break
	if kept.size() > 1 and kept[0] == "Command":
		kept.remove_at(0)
	return " ".join(kept)


func _power_name_of(power_id: String) -> String:
	var row := CahHeroes.power_row(_system, power_id)
	return String(row.get("nameStringId", power_id))


func _append_power_level_header() -> void:
	var caption := Label.new()
	caption.text = "REQUIRED HERO LEVEL"
	caption.custom_minimum_size = Vector2(POWER_LABEL_WIDTH, 0)
	caption.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	caption.add_theme_font_size_override("font_size", 14)
	caption.add_theme_color_override("font_color", Color(0.72, 0.82, 0.62, 0.92))
	_power_grid.add_child(caption)
	for level in POWER_LEVEL_COLUMNS:
		var chip := PanelContainer.new()
		chip.theme_type_variation = "CahCardSunken"
		chip.custom_minimum_size = Vector2(POWER_CELL_SIZE.x, 0)
		var cell := Label.new()
		cell.text = str(level)
		cell.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		cell.add_theme_font_size_override("font_size", 20)
		cell.add_theme_color_override("font_color", Color(0.93, 0.88, 0.68, 0.98))
		chip.add_child(cell)
		_power_grid.add_child(chip)


func _on_power_grid_sorted() -> void:
	## The wrapper takes the grid's height so the scroll view knows how far it
	## goes, and the connectors are redrawn over the cells' new rectangles.
	if _power_grid_wrap != null:
		var wanted := _power_grid.get_combined_minimum_size().y
		if not is_equal_approx(_power_grid_wrap.custom_minimum_size.y, wanted):
			_power_grid_wrap.custom_minimum_size.y = wanted
	if _power_links != null:
		_power_links.queue_redraw()


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
			_power_hint.text = "%s needs %s first." % [
				_readable(String(row.get("nameStringId", power_id))),
				_readable(_power_name_of(prerequisite))
			]
			return
		_powers.append(power_id)
	_power_hint.text = ""
	_rebuild_power_grid()
	_rebuild_current_powers()
	_update_budget()


func _rebuild_current_powers() -> void:
	_clear_children(_current_powers)
	if _powers.is_empty():
		_power_hint.text = "Choose a level 1 power to start a chain."
		var empty := Label.new()
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		empty.add_theme_font_size_override("font_size", 15)
		empty.add_theme_color_override("font_color", Color(0.70, 0.78, 0.64, 0.8))
		empty.text = "Nothing equipped yet. Powers you pick on the left appear here in the order you take them."
		_current_powers.add_child(empty)
	for index in range(_powers.size()):
		var power_id := String(_powers[index])
		var row := CahHeroes.power_row(_system, power_id)
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 8)
		var ordinal := Label.new()
		ordinal.text = "%d." % (index + 1)
		ordinal.custom_minimum_size = Vector2(28, 0)
		ordinal.add_theme_color_override("font_color", Color(0.72, 0.80, 0.64, 0.85))
		line.add_child(ordinal)
		var texture := _icon_texture(String(row.get("buttonImageId", "")))
		if texture != null:
			var icon := TextureRect.new()
			icon.texture = texture
			icon.custom_minimum_size = Vector2(34, 34)
			icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
			line.add_child(icon)
		var label := Label.new()
		label.text = _readable(String(row.get("nameStringId", power_id)))
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		label.add_theme_font_size_override("font_size", 16)
		line.add_child(label)
		var cost := Label.new()
		cost.text = "+%d" % int(row.get("costIfSelected", 0))
		cost.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		cost.custom_minimum_size = Vector2(64, 0)
		cost.add_theme_color_override("font_color", Color(0.88, 0.80, 0.54, 0.9))
		line.add_child(cost)
		_current_powers.add_child(line)
	if _slots_label != null:
		_slots_label.text = "%d of %d slots" % [_powers.size(), CahHeroes.max_power_slots(_system)]
	_rebuild_detail_tabs()


func _rebuild_detail_tabs() -> void:
	if _detail_powers == null:
		return
	_clear_children(_detail_powers)
	if _powers.is_empty():
		var empty := Label.new()
		empty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		empty.add_theme_font_size_override("font_size", 15)
		empty.text = "This hero has no powers equipped."
		_detail_powers.add_child(empty)
	for index in range(_powers.size()):
		var row := CahHeroes.power_row(_system, String(_powers[index]))
		var label := Label.new()
		label.text = "%d.  %s" % [index + 1, _readable(String(row.get("nameStringId", _powers[index])))]
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
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
		_awards_list.add_item("No awards are authored for this type.")
		_awards_list.set_item_selectable(0, false)
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
		_points_label.text = "Points left to spend:  %d" % remaining
		_points_label.add_theme_color_override(
			"font_color",
			Color(0.93, 0.88, 0.68, 0.98) if remaining == 0 else Color(0.95, 0.72, 0.42, 0.98)
		)
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
	_clear_children(_class_stat_bars)
	var class_row := CahHeroes.class_row(_system, _selected_class)
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	_class_title.text = _readable(String(class_row.get("nameStringId", "")))
	_class_description.text = _readable_prose(String(class_row.get("descriptionStringId", "")))
	_class_description.visible = _class_description.text != ""
	_sub_title.text = _readable(String(sub_row.get("nameStringId", "")))
	_sub_description.text = _readable_prose(String(sub_row.get("descriptionStringId", "")))
	_sub_description.visible = _sub_description.text != ""
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
	var stats := CahHeroes.computed_stats(_system, sub_row, _attributes, _powers)
	var separator := HSeparator.new()
	_class_stat_bars.add_child(separator)
	for pair in [
		["Health", str(int(stats.get("health", 0)))],
		["Vision range", "%d" % int(round(float(stats.get("visionRange", 0.0))))],
		["Damage multiplier", "%.2fx" % float(stats.get("damageMultiplier", 1.0))],
		["Points to spend", str(CahHeroes.attribute_budget(sub_row))],
		["Command points", str(int(stats.get("commandPoints", 0)))],
		["Build cost", str(build_cost())],
	]:
		var line := HBoxContainer.new()
		var caption := Label.new()
		caption.text = String(pair[0])
		caption.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		caption.add_theme_font_size_override("font_size", 16)
		caption.add_theme_color_override("font_color", Color(0.74, 0.82, 0.68, 0.9))
		line.add_child(caption)
		var value := Label.new()
		value.text = String(pair[1])
		value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		value.add_theme_font_size_override("font_size", 16)
		line.add_child(value)
		_class_stat_bars.add_child(line)


func _update_detail_stats() -> void:
	if _detail_stats == null or _system.is_empty():
		return
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	var class_row := CahHeroes.class_row(_system, _selected_class)
	_detail_name.text = (
		name_edit.text if name_edit != null and name_edit.text != ""
		else _readable(String(sub_row.get("nameStringId", "")))
	)
	_detail_type.text = "%s  -  %s" % [
		_readable(String(class_row.get("nameStringId", ""))),
		_readable(String(sub_row.get("nameStringId", ""))),
	]
	_detail_portrait.texture = _icon_texture(String(sub_row.get("iconImageId", "")))
	_fill_stat_panel(_detail_stats)
	_fill_stat_panel(_appearance_summary)
	# THE STATS TAB USED TO BE EMPTIED HERE. `stats_bars` - the panel the STATS tab
	# shows, and the tab the screen opens on - was cleared on every rebuild while
	# the numbers were written into the APPEARANCE panel instead, so the first
	# thing the screen showed was a blank column with four tab buttons over it.
	_fill_stat_panel(stats_bars)


static func _clear_children(container: Node) -> void:
	## REMOVED FIRST, FREED AFTER. `queue_free` alone leaves the old rows in the
	## tree until the end of the frame, so the container is laid out once with the
	## outgoing children AND the incoming ones - which is how the powers grid came
	## to show a scrollbar for twice the content it was drawing.
	if container == null:
		return
	for child in container.get_children():
		container.remove_child(child)
		child.queue_free()


func _fill_stat_panel(panel: VBoxContainer) -> void:
	if panel == null:
		return
	_clear_children(panel)
	var class_row := CahHeroes.class_row(_system, _selected_class)
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	var stats := CahHeroes.computed_stats(_system, sub_row, _attributes, _powers)
	for pair in [
		["Class", _readable(String(class_row.get("nameStringId", "")))],
		["Type", _readable(String(sub_row.get("nameStringId", "")))],
		["Health", str(int(stats.get("health", 0)))],
		["Vision range", "%d" % int(round(float(stats.get("visionRange", 0.0))))],
		["Damage multiplier", "%.2fx" % float(stats.get("damageMultiplier", 1.0))],
		["Powers equipped", "%d of %d" % [_powers.size(), CahHeroes.max_power_slots(_system)]],
		["Command points", str(int(stats.get("commandPoints", 0)))],
		["Revive cost", str(int(stats.get("reviveCost", 0)))],
		["Build cost", str(build_cost())],
	]:
		var line := HBoxContainer.new()
		var caption := Label.new()
		caption.text = String(pair[0])
		caption.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		caption.add_theme_color_override("font_color", Color(0.74, 0.82, 0.68, 0.9))
		line.add_child(caption)
		var value := Label.new()
		value.text = String(pair[1])
		value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		line.add_child(value)
		panel.add_child(line)
	for row_value in (sub_row.get("attributes", []) as Array):
		var row := row_value as Dictionary
		var group := String(row.get("groupName", ""))
		_add_step_bar(
			panel,
			_attribute_label(group),
			int(_attributes.get(group, row.get("minStep", 1))),
			int(row.get("maxStep", 20))
		)


func _add_step_bar(parent: Control, label_text: String, step: int, maximum: int) -> void:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 8)
	var label := Label.new()
	label.text = label_text
	label.custom_minimum_size = Vector2(104, 0)
	label.add_theme_font_size_override("font_size", 16)
	line.add_child(label)
	var bar := ProgressBar.new()
	bar.theme_type_variation = "CahBar"
	bar.min_value = 0.0
	bar.max_value = float(maxi(1, maximum))
	bar.value = float(step)
	bar.show_percentage = false
	# CAPPED, like the sliders. A stat bar that expands into a 2560px window is a
	# metre of grey with a number at the end of it.
	bar.custom_minimum_size = Vector2(150, 16)
	bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	line.add_child(bar)
	var value := Label.new()
	value.text = "%d / %d" % [step, maximum]
	value.custom_minimum_size = Vector2(78, 0)
	value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	value.add_theme_font_size_override("font_size", 15)
	value.add_theme_color_override("font_color", Color(0.86, 0.84, 0.66, 0.95))
	line.add_child(value)
	parent.add_child(line)


# ------------------------------------------------------------------- 3D preview


func _build_preview() -> void:
	_preview_column = VBoxContainer.new()
	_preview_column.name = "HeroPreview"
	_preview_column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_preview_column.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_preview_column.add_theme_constant_override("separation", 6)

	var frame := PanelContainer.new()
	frame.theme_type_variation = "CahCardSunken"
	frame.size_flags_vertical = Control.SIZE_EXPAND_FILL
	frame.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_preview_column.add_child(frame)

	# A LIT FLOOR BEHIND THE HERO. On a sunken card the preview read as a black
	# rectangle punched through the page; a soft gradient gives him somewhere to
	# stand and separates his silhouette from the frame.
	var stage := TextureRect.new()
	var gradient := GradientTexture2D.new()
	gradient.fill = GradientTexture2D.FILL_RADIAL
	gradient.fill_from = Vector2(0.5, 0.62)
	gradient.fill_to = Vector2(0.5, 1.25)
	var ramp := Gradient.new()
	ramp.set_color(0, Color(0.12, 0.17, 0.11, 1.0))
	ramp.set_color(1, Color(0.015, 0.030, 0.020, 1.0))
	gradient.gradient = ramp
	stage.texture = gradient
	stage.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	stage.stretch_mode = TextureRect.STRETCH_SCALE
	stage.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	stage.mouse_filter = Control.MOUSE_FILTER_IGNORE
	frame.add_child(stage)

	var stack := VBoxContainer.new()
	stack.add_theme_constant_override("separation", 4)
	frame.add_child(stack)

	_preview_host = SubViewportContainer.new()
	_preview_host.stretch = true
	# A FLOOR, NOT A SIZE. The preview column expands to fill whichever slot it is
	# reparented into, so this only has to be big enough to be a hero.
	_preview_host.custom_minimum_size = Vector2(240, 200)
	_preview_host.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_preview_host.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_preview_host.mouse_filter = Control.MOUSE_FILTER_STOP
	_preview_host.gui_input.connect(_on_preview_input)
	_preview_host.tooltip_text = "Drag to turn the hero."
	stack.add_child(_preview_host)

	_preview_viewport = SubViewport.new()
	_preview_viewport.size = Vector2i(460, 780)
	_preview_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_preview_viewport.transparent_bg = true
	_preview_host.add_child(_preview_viewport)
	_preview_root = Node3D.new()
	_preview_viewport.add_child(_preview_root)
	_preview_pivot = Node3D.new()
	_preview_root.add_child(_preview_pivot)
	_preview_camera = Camera3D.new()
	_preview_camera.position = Vector3(0.0, 1.3, 3.4)
	_preview_camera.rotation_degrees = Vector3(-10.0, 0.0, 0.0)
	_preview_root.add_child(_preview_camera)
	var key := DirectionalLight3D.new()
	key.rotation_degrees = Vector3(-32.0, 152.0, 0.0)
	key.light_energy = 1.45
	_preview_root.add_child(key)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-12.0, -40.0, 0.0)
	fill.light_energy = 0.55
	fill.light_color = Color(0.78, 0.86, 1.0)
	_preview_root.add_child(fill)

	# The turn controls. Retail turns the hero with a pair of arrows under him;
	# dragging does the same thing for a mouse that is already over the model.
	var turn := HBoxContainer.new()
	turn.alignment = BoxContainer.ALIGNMENT_CENTER
	turn.add_theme_constant_override("separation", 10)
	stack.add_child(turn)
	var left := _stepper("◀")
	left.tooltip_text = "Turn left"
	left.pressed.connect(func() -> void: _turn_preview(-30.0))
	turn.add_child(left)
	var turn_label := Label.new()
	turn_label.text = "TURN"
	turn_label.add_theme_font_size_override("font_size", 13)
	turn_label.add_theme_color_override("font_color", Color(0.72, 0.80, 0.64, 0.8))
	turn.add_child(turn_label)
	var right := _stepper("▶")
	right.tooltip_text = "Turn right"
	right.pressed.connect(func() -> void: _turn_preview(30.0))
	turn.add_child(right)

	_preview_note = Label.new()
	_preview_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_preview_note.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_preview_note.add_theme_font_size_override("font_size", 13)
	_preview_note.add_theme_color_override("font_color", Color(0.88, 0.78, 0.52, 0.92))
	_preview_column.add_child(_preview_note)


func _on_preview_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.button_index == MOUSE_BUTTON_LEFT:
			_preview_dragging = button.pressed
	elif event is InputEventMouseMotion and _preview_dragging:
		_turn_preview((event as InputEventMouseMotion).relative.x * PREVIEW_DRAG_DEGREES_PER_PIXEL)


func _turn_preview(degrees: float) -> void:
	_preview_yaw = fmod(_preview_yaw + degrees, 360.0)
	_apply_preview_yaw()


func _apply_preview_yaw() -> void:
	if _preview_pivot != null:
		_preview_pivot.rotation_degrees = Vector3(0.0, _preview_yaw, 0.0)


func _update_preview() -> void:
	if _preview_root == null or _system.is_empty():
		return
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	# The creation-screen pose, which is a DIFFERENT mesh from the battlefield
	# one; retail authors both and shows this one in these screens.
	var surface := "creationScreen"
	var binding := CahHeroes.model_binding(sub_row, surface)
	var model_id := String(binding.get("model", ""))
	if model_id == "":
		surface = "battlefield"
		binding = CahHeroes.model_binding(sub_row, surface)
		model_id = String(binding.get("model", ""))
	if model_id == "":
		_drop_preview_model()
		_preview_note.text = "This type has no mesh in the mounted pack."
		_garment_status = "no-model"
		_set_garment_caption("")
		return
	if model_id != _loaded_model_id or _preview_model == null:
		_drop_preview_model()
		var path := _resolve_model_path(model_id)
		if path == "":
			# NAMED, NOT SUBSTITUTED. A capsule here is how a missing mesh ships
			# unnoticed; the model retail wants is stated instead.
			_preview_note.text = "Model %s is not in the mounted pack yet." % model_id
			_garment_status = "no-model"
			_set_garment_caption("")
			return
		var factory := _load_asset_factory()
		if factory == null:
			_preview_note.text = "The model loader is unavailable in this context."
			_garment_status = "no-model"
			return
		var visual: Node3D = factory.make_explicit_model_visual(path, 0)
		if visual == null:
			_preview_note.text = "Model %s could not be loaded from the pack." % model_id
			_garment_status = "no-model"
			return
		_preview_model = visual
		_loaded_model_id = model_id
		_preview_pivot.add_child(visual)
		_preview_note.text = ""
	_apply_preview_appearance(surface)
	_frame_preview(sub_row, _preview_model)
	_apply_preview_yaw()


func _drop_preview_model() -> void:
	if _preview_model != null:
		_preview_model.queue_free()
		_preview_model = null
	_loaded_model_id = ""


func _apply_preview_appearance(surface := "") -> void:
	## PUT ON WHAT HE IS WEARING AND TAKE OFF WHAT HE IS NOT.
	##
	## Retail bakes every garment variant into the one skin and switches their
	## visibility; the converted GLBs keep the retail part names, so the same
	## switch works here as soon as the pack says which option owns which part.
	if _preview_model == null:
		return
	if surface == "":
		surface = "creationScreen" if _loaded_model_id.ends_with("_C_SKN") else "battlefield"
	var sub_row := CahHeroes.sub_class_row(_system, _selected_class, _selected_sub)
	var plan := SubObjects.plan(_system, sub_row, _appearance, surface)
	if bool(plan.get("mapped", false)):
		var applied := SubObjects.apply(_preview_model, plan)
		_garment_status = "mapped"
		_set_garment_caption("")
		if int(applied.get("matched", 0)) == 0:
			# The map exists but names nothing this mesh carries, which is a
			# content mismatch worth saying out loud rather than a silent no-op.
			_garment_status = "mapped-unmatched"
			_set_garment_caption(
				"The pack maps garments to parts this mesh does not carry, so the preview cannot show them."
			)
		return
	# NO MAP IN THE PACK. Every variant the artist baked into the skin is showing
	# at once - four shields, six helmets - which is never a hero anyone chose.
	# The numbered siblings are collapsed to one so the preview is a person, and
	# the screen says plainly that the choices are not bound to the mesh yet.
	SubObjects.apply(_preview_model, SubObjects.collapse_variant_families(_preview_model))
	_garment_status = "unmapped"
	_set_garment_caption(
		"This content pack does not bind garment choices to the hero's mesh yet, "
		+ "so the preview shows his base kit and does not change as you cycle parts."
	)


func _set_garment_caption(text: String) -> void:
	if _preview_caption == null:
		return
	_preview_caption.text = text
	_preview_caption.visible = text != ""


static func _visible_bounds(root: Node3D) -> AABB:
	## The box around the parts of a model the player can actually see, in the
	## model's own space.
	var out := AABB()
	var found := false
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if not (node is MeshInstance3D):
			continue
		var instance := node as MeshInstance3D
		if instance.mesh == null or not instance.is_visible_in_tree():
			continue
		var local := root.global_transform.affine_inverse() * instance.global_transform
		var box := local * instance.get_aabb()
		out = box if not found else out.merge(box)
		found = true
	return out if found else AABB()


func _load_asset_factory() -> GDScript:
	if _asset_factory == null and ResourceLoader.exists(ASSET_FACTORY_PATH):
		_asset_factory = load(ASSET_FACTORY_PATH) as GDScript
	return _asset_factory


func _resolve_model_path(model_id: String) -> String:
	## The mounted pack's file for one retail model id.
	if _content_db == null or model_id == "":
		return ""
	if not _content_db.has_method("resolve_cah_model_path"):
		return ""
	var path := String(_content_db.call("resolve_cah_model_path", model_id))
	if path == "":
		return ""
	return path if FileAccess.file_exists(path) or ResourceLoader.exists(path) else ""


func _frame_preview(sub_row: Dictionary, visual: Node3D) -> void:
	## Frame the hero using the subclass's OWN authored camera.
	##
	## Retail authors a ViewInfo per subclass precisely because one framing does
	## not fit both a Wanderer and a Great Troll. When the pack carries it we use
	## it; otherwise the model's own bounds are used so a tall hero is still
	## whole in the viewport rather than cropped at a fixed distance.
	if visual == null:
		return
	var view: Dictionary = sub_row.get("viewInfo", {}) as Dictionary
	var factory := _load_asset_factory()
	if factory == null:
		return
	# THE BOUNDS OF WHAT IS SHOWING, not of everything in the file. The shared
	# helper measures every mesh in the scene including the garments that are
	# switched off, so a skin carrying a two-metre pike nobody equipped framed
	# the hero for a pike and stood him in the top third of his own card.
	var bounds := _visible_bounds(visual)
	if bounds.size == Vector3.ZERO:
		bounds = factory.model_aabb(visual)
	var height := maxf(0.1, bounds.size.y)
	visual.position = Vector3(0.0, -bounds.position.y, 0.0)
	# 1.35, not 1.85. The hero is the point of this column; at 1.85 he stood in
	# the middle of a card three times his height like a figurine in a display
	# case, which is exactly how the preview read as an afterthought.
	var distance := height * 1.30
	var pitch := -3.0
	if not view.is_empty():
		distance = maxf(distance, float(view.get("closeUpDist", distance)) * 0.06)
		pitch = float(view.get("closeUpPitch", -0.07)) * 60.0
	# HALF HIS HEIGHT, measured AFTER his feet were put on the floor above. Aiming
	# at `bounds.position.y + height * 0.5` aimed at where his middle was BEFORE
	# the shift, which on a model authored around its pelvis pointed the camera
	# below the floor and stood him in the top third of the card.
	_preview_camera.position = Vector3(0.0, height * 0.5, distance)
	_preview_camera.rotation_degrees = Vector3(pitch, 0.0, 0.0)


# ---------------------------------------------------------------------- labels


func _attribute_label(group_name: String) -> String:
	## The five names retail prints beside the bars.
	##
	## Matched on the UNSPACED tail, because the fallback label now spaces camel
	## case: `ArmorAttribute` reaches here as "Armor Attribute", and a match arm
	## written against the old spelling silently stopped firing - which is how the
	## bars came to be captioned "Damage Mult Attribute".
	var tail := _readable(group_name)
	match tail.replace(" ", ""):
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
	return _spaced_identifier(CahHeroes._tail_label(string_id))


func _readable_prose(string_id: String) -> String:
	## A DESCRIPTION, or nothing.
	##
	## A name can survive being recovered from its id - "Heroes Of The West" is
	## the class's actual name - but a paragraph cannot: `HelmetMenuDesc` spaced
	## out is not a sentence, it is an identifier with gaps in it. When the pack
	## carries no string for a description the screen leaves the line out.
	if string_id == "" or _content_db == null:
		return ""
	if not _content_db.has_method("get_retail_string"):
		return ""
	var resolved := String(_content_db.call("get_retail_string", string_id, ""))
	if resolved.strip_edges() == "":
		return ""
	return _without_hotkey_markers(resolved)


static func _spaced_identifier(tail: String) -> String:
	## The de-namespaced tail, made readable.
	##
	## Without a strings table `CreateAHero:ClassName_HeroesOfTheWest` reaches the
	## screen as `HeroesOfTheWest`, which is an identifier printed at the player.
	## Splitting the camel case is not a translation and does not pretend to be
	## one - the row is still identifiably the id it came from - but it is the
	## difference between a label and a symbol.
	var out := ""
	for index in range(tail.length()):
		var character := tail[index]
		if (
			index > 0
			and character == character.to_upper()
			and character != character.to_lower()
			and tail[index - 1] != " "
			and (
				tail[index - 1] == tail[index - 1].to_lower()
				or (index + 1 < tail.length() and tail[index + 1] == tail[index + 1].to_lower())
			)
		):
			out += " "
		out += character
	return out


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


class PowerLinks extends Control:
	## THE PREREQUISITE CONNECTORS, drawn over the lattice's own cell rectangles.
	##
	## A chain like Cripple Strike -> Improved -> Great is a real constraint, and
	## before this the only sign of it was a refusal when you clicked the second
	## one first. Each link is drawn from the right edge of the cell that must be
	## taken first to the left edge of the one it unlocks, lit when the
	## prerequisite is equipped and dim when it is not.
	var links: Array = []

	func _draw() -> void:
		for link_value in links:
			var link := link_value as Dictionary
			var from_cell: Control = link.get("from") as Control
			var to_cell: Control = link.get("to") as Control
			if from_cell == null or to_cell == null:
				continue
			if not is_instance_valid(from_cell) or not is_instance_valid(to_cell):
				continue
			var start := from_cell.position + Vector2(from_cell.size.x + 1.0, from_cell.size.y * 0.5)
			var end := to_cell.position + Vector2(-1.0, to_cell.size.y * 0.5)
			if end.x <= start.x:
				continue
			var colour := (
				Color(0.92, 0.84, 0.52, 0.95) if bool(link.get("on", false))
				else Color(0.62, 0.70, 0.56, 0.40)
			)
			draw_line(start, end, colour, 2.0)
			draw_line(end, end + Vector2(-7.0, -5.0), colour, 2.0)
			draw_line(end, end + Vector2(-7.0, 5.0), colour, 2.0)
