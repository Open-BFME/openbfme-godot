extends Panel
## Retail GAME SETUP layout (REF-09/10/11): "skirmish" title and subtitle,
## MAP/RULES tabs, a map list with player counts, the parchment preview, a
## description panel, the player rows table, and the
## MAIN MENU / STATS / PROFILE / PLAY bottom bar. This container only builds
## and exposes the controls; main_menu.gd populates and drives them.

signal map_row_selected(index: int)
signal army_changed
signal color_changed(row: int)
signal start_changed(index: int)
signal rules_changed
signal rules_reset_requested
signal play_pressed
signal main_menu_pressed
signal stats_pressed

const TAB_MAP := "map"
const TAB_RULES := "rules"

var map_rows: Array[Dictionary] = []
var map_rows_host: VBoxContainer
var preview_image: TextureRect
var preview_caption: Label
var description_title: Label
var description_label: Label
var player_army_opt: OptionButton
var enemy_army_opt: OptionButton
var hint_label: Label
var play_btn: Button
var main_menu_btn: Button
var stats_btn: Button
var profile_btn: Button
var map_tab_btn: Button
var rules_tab_btn: Button
var initial_resources_opt: OptionButton
var cp_factor_opt: OptionButton
var custom_heroes_toggle: CheckButton
var ring_heroes_toggle: CheckButton
var rules_reset_btn: Button
var hero_dropdowns: Array[OptionButton] = []
var team_dropdowns: Array[OptionButton] = []
var color_swatches: Array[ColorRect] = []
var color_dropdowns: Array[OptionButton] = []
var handicap_dropdowns: Array[OptionButton] = []
var start_row: HBoxContainer
var start_note: Label
var start_buttons: Array[Button] = []
var map_content: Control
var rules_content: Control
var selected_map_row := -1


func _ready() -> void:
	_build()
	_set_tab(TAB_MAP)


func _build() -> void:
	var title := _label(self, "Title", "skirmish", Vector2(0, 8), Vector2(size.x, 52), 40, Color("9ec97e"))
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	var subtitle := _label(self, "Subtitle", "GAME SETUP", Vector2(0, 58), Vector2(size.x, 26), 17, Color("d8e6da"))
	subtitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER

	map_tab_btn = _button(self, "MapTab", "MAP", Vector2(30, 96), Vector2(180, 42))
	rules_tab_btn = _button(self, "RulesTab", "RULES", Vector2(214, 96), Vector2(180, 42))
	map_tab_btn.pressed.connect(func() -> void: _set_tab(TAB_MAP))
	rules_tab_btn.pressed.connect(func() -> void: _set_tab(TAB_RULES))

	map_content = Control.new()
	map_content.name = "MapContent"
	map_content.position = Vector2(30, 148)
	map_content.size = Vector2(size.x - 60, 380)
	add_child(map_content)
	_build_map_content()
	rules_content = Control.new()
	rules_content.name = "RulesContent"
	rules_content.position = Vector2(30, 148)
	rules_content.size = Vector2(size.x - 60, 380)
	rules_content.visible = false
	add_child(rules_content)
	_build_rules_content()
	_build_player_rows()
	_build_bottom_bar()


func _build_map_content() -> void:
	var list_panel := _panel(map_content, "MapListPanel", Rect2(0, 0, 560, 380))
	var list_header := _label(list_panel, "ListHeader", "Map Name", Vector2(18, 8), Vector2(380, 24), 15, Color("b7dc94"))
	var players_header := _label(list_panel, "PlayersHeader", "Players", Vector2(430, 8), Vector2(110, 24), 15, Color("b7dc94"))
	players_header.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	# The list scrolls once more maps land; the bar stays a thin retail-style
	# strip instead of crowding the rows.
	var scroll := ScrollContainer.new()
	scroll.name = "MapListScroll"
	scroll.position = Vector2(12, 40)
	scroll.size = Vector2(536, 328)
	list_panel.add_child(scroll)
	var rows_host := VBoxContainer.new()
	rows_host.name = "MapRows"
	rows_host.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows_host.add_theme_constant_override("separation", 2)
	scroll.add_child(rows_host)
	map_rows_host = rows_host

	var preview_panel := _panel(map_content, "PreviewPanel", Rect2(580, 0, 630, 380))
	preview_image = TextureRect.new()
	preview_image.name = "SetupMapPreviewImage"
	preview_image.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	preview_image.offset_left = 12
	preview_image.offset_top = 12
	preview_image.offset_right = -12
	preview_image.offset_bottom = -52
	preview_image.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	preview_image.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	preview_image.mouse_filter = Control.MOUSE_FILTER_IGNORE
	preview_panel.add_child(preview_image)
	preview_caption = _label(preview_panel, "SetupMapPreviewCaption", "", Vector2(12, 340), Vector2(606, 28), 17, Color("e3c98d"))
	preview_caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	start_row = HBoxContainer.new()
	start_row.name = "StartRow"
	start_row.position = Vector2(12, 302)
	start_row.size = Vector2(606, 32)
	start_row.alignment = BoxContainer.ALIGNMENT_CENTER
	start_row.add_theme_constant_override("separation", 8)
	preview_panel.add_child(start_row)
	start_note = _label(preview_panel, "StartNote", "", Vector2(12, 302), Vector2(606, 32), 13, Color("a9c08a"))
	start_note.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	start_note.visible = false

	var description_panel := _panel(map_content, "DescriptionPanel", Rect2(1230, 0, map_content.size.x - 1230, 380))
	description_title = _label(description_panel, "SetupDescriptionTitle", "", Vector2(16, 10), Vector2(description_panel.size.x - 32, 30), 20, Color("e3c98d"))
	description_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	description_label = _label(description_panel, "SetupDescription", "", Vector2(16, 50), Vector2(description_panel.size.x - 32, 316), 15, Color("d8e6da"))
	description_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	description_label.vertical_alignment = VERTICAL_ALIGNMENT_TOP


func _build_rules_content() -> void:
	var resources_label := _label(rules_content, "InitialResourcesLabel", "Initial Resources", Vector2(0, 18), Vector2(220, 30), 17, Color("b7dc94"))
	initial_resources_opt = _option(rules_content, "InitialResources", Vector2(230, 14), Vector2(220, 40))
	var factor_label := _label(rules_content, "CpFactorLabel", "Command Point Factor", Vector2(0, 66), Vector2(220, 30), 17, Color("b7dc94"))
	cp_factor_opt = _option(rules_content, "CpFactor", Vector2(230, 62), Vector2(220, 40))
	custom_heroes_toggle = CheckButton.new()
	custom_heroes_toggle.name = "CustomHeroesToggle"
	custom_heroes_toggle.text = "Allow Custom Heroes"
	custom_heroes_toggle.tooltip_text = "Create-a-Hero is not a converted feature"
	custom_heroes_toggle.disabled = true
	custom_heroes_toggle.position = Vector2(560, 16)
	custom_heroes_toggle.size = Vector2(320, 36)
	rules_content.add_child(custom_heroes_toggle)
	ring_heroes_toggle = CheckButton.new()
	ring_heroes_toggle.name = "RingHeroesToggle"
	ring_heroes_toggle.text = "Allow Ring Heroes"
	ring_heroes_toggle.tooltip_text = "Ring Heroes are not a converted feature"
	ring_heroes_toggle.disabled = true
	ring_heroes_toggle.position = Vector2(560, 64)
	ring_heroes_toggle.size = Vector2(320, 36)
	rules_content.add_child(ring_heroes_toggle)
	rules_reset_btn = _button(rules_content, "RulesReset", "RESET", Vector2(rules_content.size.x - 290, rules_content.size.y - 60), Vector2(260, 48))


func _build_player_rows() -> void:
	var table := Panel.new()
	table.name = "PlayerRows"
	table.theme_type_variation = "OverlayPanel"
	table.position = Vector2(30, 540)
	table.size = Vector2(size.x - 60, 168)
	add_child(table)
	var header_y := 10.0
	_label(table, "HeaderPlayer", "Player", Vector2(20, header_y), Vector2(220, 24), 15, Color("b7dc94"))
	_label(table, "HeaderArmy", "Army", Vector2(260, header_y), Vector2(300, 24), 15, Color("b7dc94"))
	_label(table, "HeaderHero", "Hero", Vector2(580, header_y), Vector2(240, 24), 15, Color("b7dc94"))
	_label(table, "HeaderTeam", "Team", Vector2(840, header_y), Vector2(160, 24), 15, Color("b7dc94"))
	_label(table, "HeaderColor", "Color", Vector2(1020, header_y), Vector2(120, 24), 15, Color("b7dc94"))
	_label(table, "HeaderHandicap", "Handicap", Vector2(1160, header_y), Vector2(160, 24), 15, Color("b7dc94"))
	var row_data := [
		{"name": "Player 1", "army_var": "player_army_opt"},
		{"name": "Player 2 (AI)", "army_var": "enemy_army_opt"},
	]
	for index in row_data.size():
		var row: Dictionary = row_data[index]
		var y := 44.0 + index * 74.0
		_label(table, "RowName%d" % index, String(row["name"]), Vector2(20, y + 10), Vector2(220, 30), 16, Color("d8e6da"))
		var army := _option(table, "PlayerArmy" if index == 0 else "EnemyArmy", Vector2(260, y), Vector2(300, 42))
		army.item_selected.connect(func(_i: int) -> void: army_changed.emit())
		set(String(row["army_var"]), army)
		var hero := _option(table, "Hero%d" % index, Vector2(580, y), Vector2(240, 42))
		hero.add_item("-")
		hero.select(0)
		hero.disabled = true
		hero.tooltip_text = "Create-a-Hero is not a converted feature"
		hero_dropdowns.append(hero)
		var team := _option(table, "Team%d" % index, Vector2(840, y), Vector2(160, 42))
		team.add_item("-")
		team.select(0)
		team.disabled = true
		team.tooltip_text = "Fixed two-sided match; alternate teams are not supported by the slice"
		team_dropdowns.append(team)
		var color_opt := _option(table, "Color%d" % index, Vector2(1016, y), Vector2(120, 42))
		color_opt.item_selected.connect(func(_i: int) -> void: color_changed.emit(index))
		color_dropdowns.append(color_opt)
		var swatch := ColorRect.new()
		swatch.name = "ColorSwatch%d" % index
		swatch.position = Vector2(1146, y + 5)
		swatch.size = Vector2(32, 32)
		swatch.mouse_filter = Control.MOUSE_FILTER_IGNORE
		table.add_child(swatch)
		color_swatches.append(swatch)
		var handicap := _option(table, "Handicap%d" % index, Vector2(1190, y), Vector2(160, 42))
		handicap.add_item("0%")
		handicap.select(0)
		handicap.disabled = true
		handicap.tooltip_text = "Handicap scaling is not supported by the slice"
		handicap_dropdowns.append(handicap)


func _build_bottom_bar() -> void:
	main_menu_btn = _button(self, "MainMenuButton", "MAIN MENU", Vector2(30, size.y - 66), Vector2(240, 52))
	main_menu_btn.pressed.connect(func() -> void: main_menu_pressed.emit())
	stats_btn = _button(self, "StatsButton", "STATS", Vector2(290, size.y - 66), Vector2(240, 52))
	stats_btn.pressed.connect(func() -> void: stats_pressed.emit())
	profile_btn = _button(self, "ProfileButton", "PROFILE", Vector2(550, size.y - 66), Vector2(240, 52))
	profile_btn.disabled = true
	profile_btn.tooltip_text = "Profiles are not tracked yet"
	hint_label = _label(self, "RetailHint", "", Vector2(830, size.y - 60), Vector2(700, 26), 13, Color("a9c08a"))
	hint_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	hint_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	play_btn = _button(self, "Retail", "PLAY", Vector2(size.x - 290, size.y - 66), Vector2(260, 52))
	play_btn.theme_type_variation = "PrimaryButton"
	play_btn.pressed.connect(func() -> void: play_pressed.emit())


func _set_tab(tab: String) -> void:
	var map_active := tab == TAB_MAP
	map_content.visible = map_active
	rules_content.visible = not map_active
	# Retail: the active tab reads lit, not disabled (REF-09/11).
	map_tab_btn.toggle_mode = true
	rules_tab_btn.toggle_mode = true
	map_tab_btn.button_pressed = map_active
	rules_tab_btn.button_pressed = not map_active


func set_selected_map_row(index: int) -> void:
	selected_map_row = index
	for row_index in map_rows.size():
		var row_button := map_rows[row_index]["button"] as Button
		row_button.button_pressed = row_index == index


func _panel(parent: Control, node_name: String, rect: Rect2) -> Panel:
	var panel := Panel.new()
	panel.name = node_name
	panel.theme_type_variation = "OverlayPanel"
	panel.position = rect.position
	panel.size = rect.size
	parent.add_child(panel)
	return panel


func _label(parent: Control, node_name: String, text: String, position: Vector2, label_size: Vector2, font_size: int, color: Color) -> Label:
	var label := Label.new()
	label.name = node_name
	label.text = text
	label.position = position
	label.size = label_size
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", color)
	parent.add_child(label)
	return label


func _button(parent: Control, node_name: String, text: String, position: Vector2, button_size: Vector2) -> Button:
	var button := Button.new()
	button.name = node_name
	button.text = text
	button.position = position
	button.size = button_size
	button.custom_minimum_size = button_size
	parent.add_child(button)
	return button


func _option(parent: Control, node_name: String, position: Vector2, option_size: Vector2) -> OptionButton:
	var option := OptionButton.new()
	option.name = node_name
	option.position = position
	option.size = option_size
	option.custom_minimum_size = Vector2(option_size.x, 40)
	parent.add_child(option)
	return option
