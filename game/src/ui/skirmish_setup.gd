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
## N-team setup signals: the row set was rebuilt (add/remove/map-capacity clamp),
## a row's controller (Human/AI) flipped, or a row's Team/alliance changed. The
## main menu re-populates and re-validates on each.
signal rows_changed
signal controller_changed(row: int)
signal team_changed(row: int)
signal hero_changed(row: int)

const TAB_MAP := "map"
const TAB_RULES := "rules"
const NativeMatchLaunchScript := preload("res://src/present/native_match_launch.gd")
## Skirmish capacity floor/ceiling. Two rows is the retail default (one human +
## one AI); the ceiling is the largest skirmish the sim's roster admits.
const MIN_PLAYER_ROWS := 2
const MAX_PLAYER_ROWS := 8

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
var build_mode_opt: OptionButton
var custom_heroes_toggle: CheckButton
var ring_heroes_toggle: CheckButton
var engine_opt: OptionButton
var rules_reset_btn: Button
var hero_dropdowns: Array[OptionButton] = []
var team_dropdowns: Array[OptionButton] = []
var color_swatches: Array[ColorRect] = []
var color_dropdowns: Array[OptionButton] = []
var handicap_dropdowns: Array[OptionButton] = []
## Per-row controls added for the N-team setup. Index aligns with the other
## per-row arrays; `player_army_opt`/`enemy_army_opt` alias rows 0 and 1 so the
## legacy two-side callers keep working unchanged.
var row_army_opts: Array[OptionButton] = []
var row_controller_opts: Array[OptionButton] = []
var row_difficulty_opts: Array[OptionButton] = []
var row_name_labels: Array[Label] = []
var rows_host: VBoxContainer
var add_player_btn: Button
var remove_player_btn: Button
var player_row_count := MIN_PLAYER_ROWS
var max_player_count := MIN_PLAYER_ROWS
var _rows_table: Panel
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
	var build_mode_label := _label(rules_content, "BuildModeLabel", "Build Mode", Vector2(0, 114), Vector2(220, 30), 17, Color("b7dc94"))
	build_mode_opt = _option(rules_content, "BuildMode", Vector2(230, 110), Vector2(220, 40))
	build_mode_opt.tooltip_text = "BFME2 Freeform builds anywhere; BFME1 Plots restricts construction to fixed build plots"
	_label(rules_content, "EngineLabel", "Game Engine", Vector2(0, 162), Vector2(220, 30), 17, Color("b7dc94"))
	engine_opt = _option(rules_content, "GameEngine", Vector2(230, 158), Vector2(220, 40))
	engine_opt.add_item("Retail slice")
	engine_opt.set_item_metadata(0, "placeholder")
	engine_opt.add_item("Native core")
	engine_opt.set_item_metadata(1, "native")
	engine_opt.set_item_disabled(1, not NativeMatchLaunchScript.is_available())
	engine_opt.select(0)
	engine_opt.tooltip_text = "Native core uses the same retail setup, loading screen and HUD"
	custom_heroes_toggle = CheckButton.new()
	custom_heroes_toggle.name = "CustomHeroesToggle"
	custom_heroes_toggle.text = "Allow Custom Heroes"
	custom_heroes_toggle.tooltip_text = "Field the heroes made in MY HEROES; off refuses them for every player"
	custom_heroes_toggle.button_pressed = true
	custom_heroes_toggle.position = Vector2(560, 16)
	custom_heroes_toggle.size = Vector2(320, 36)
	rules_content.add_child(custom_heroes_toggle)
	ring_heroes_toggle = CheckButton.new()
	ring_heroes_toggle.name = "RingHeroesToggle"
	ring_heroes_toggle.text = "Allow Ring Heroes"
	ring_heroes_toggle.tooltip_text = "Allow the One Ring and each faction's Ring Hero"
	ring_heroes_toggle.button_pressed = true
	ring_heroes_toggle.position = Vector2(560, 64)
	ring_heroes_toggle.size = Vector2(320, 36)
	rules_content.add_child(ring_heroes_toggle)
	rules_reset_btn = _button(rules_content, "RulesReset", "RESET", Vector2(rules_content.size.x - 290, rules_content.size.y - 60), Vector2(260, 48))


## Column x-origins for a player row (retail REF-09/10 column set plus the
## Type/Difficulty columns the N-team sim needs). Kept as constants so the
## header labels and every rebuilt row share one layout.
const COL_NAME_X := 16.0
const COL_ARMY_X := 150.0
const COL_TYPE_X := 372.0
const COL_DIFF_X := 500.0
const COL_HERO_X := 648.0
const COL_TEAM_X := 826.0
const COL_COLOR_X := 946.0
const COL_SWATCH_X := 1064.0
const COL_HANDICAP_X := 1108.0
const ROW_HEIGHT := 42.0


func _build_player_rows() -> void:
	var table := Panel.new()
	table.name = "PlayerRows"
	table.theme_type_variation = "OverlayPanel"
	table.position = Vector2(30, 540)
	table.size = Vector2(size.x - 60, 168)
	add_child(table)
	_rows_table = table
	var header_y := 8.0
	_label(table, "HeaderPlayer", "Player", Vector2(COL_NAME_X + 4, header_y), Vector2(140, 24), 14, Color("b7dc94"))
	_label(table, "HeaderArmy", "Army", Vector2(COL_ARMY_X, header_y), Vector2(210, 24), 14, Color("b7dc94"))
	_label(table, "HeaderType", "Type", Vector2(COL_TYPE_X, header_y), Vector2(120, 24), 14, Color("b7dc94"))
	_label(table, "HeaderDifficulty", "Difficulty", Vector2(COL_DIFF_X, header_y), Vector2(140, 24), 14, Color("b7dc94"))
	_label(table, "HeaderHero", "Hero", Vector2(COL_HERO_X, header_y), Vector2(170, 24), 14, Color("b7dc94"))
	_label(table, "HeaderTeam", "Team", Vector2(COL_TEAM_X, header_y), Vector2(110, 24), 14, Color("b7dc94"))
	_label(table, "HeaderColor", "Color", Vector2(COL_COLOR_X, header_y), Vector2(110, 24), 14, Color("b7dc94"))
	_label(table, "HeaderHandicap", "Handicap", Vector2(COL_HANDICAP_X, header_y), Vector2(120, 24), 14, Color("b7dc94"))

	# Add / Remove sit on the header strip at the far right; the main menu binds
	# them and re-populates the rebuilt rows.
	remove_player_btn = _button(table, "RemovePlayer", "-", Vector2(table.size.x - 92, header_y - 2), Vector2(36, 28))
	remove_player_btn.tooltip_text = "Remove the last AI slot"
	add_player_btn = _button(table, "AddPlayer", "+", Vector2(table.size.x - 50, header_y - 2), Vector2(36, 28))
	add_player_btn.tooltip_text = "Add an AI slot (bounded by the map's player capacity)"
	add_player_btn.pressed.connect(func() -> void: add_player_row())
	remove_player_btn.pressed.connect(func() -> void: remove_player_row())

	var scroll := ScrollContainer.new()
	scroll.name = "PlayerRowsScroll"
	scroll.position = Vector2(8, 38)
	scroll.size = Vector2(table.size.x - 16, table.size.y - 46)
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	table.add_child(scroll)
	rows_host = VBoxContainer.new()
	rows_host.name = "PlayerRowsList"
	rows_host.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows_host.add_theme_constant_override("separation", 4)
	scroll.add_child(rows_host)
	_rebuild_player_rows()


func _rebuild_player_rows() -> void:
	## (Re)creates exactly `player_row_count` rows. Row 0 is the local human;
	## rows 1..N default to AI. Per-row control arrays are rebuilt in lockstep and
	## `player_army_opt`/`enemy_army_opt` re-alias rows 0/1 for the legacy callers.
	for child in rows_host.get_children():
		child.queue_free()
	hero_dropdowns.clear()
	team_dropdowns.clear()
	color_swatches.clear()
	color_dropdowns.clear()
	handicap_dropdowns.clear()
	row_army_opts.clear()
	row_controller_opts.clear()
	row_difficulty_opts.clear()
	row_name_labels.clear()
	player_army_opt = null
	enemy_army_opt = null
	var row_width := rows_host.size.x if rows_host.size.x > 0 else _rows_table.size.x - 24
	for index in player_row_count:
		var is_human := index == 0
		var row := Control.new()
		row.name = "PlayerRow%d" % index
		row.custom_minimum_size = Vector2(row_width, ROW_HEIGHT)
		rows_host.add_child(row)

		var name_label := _label(row, "RowName%d" % index, _row_name(index, is_human), Vector2(COL_NAME_X, 6), Vector2(140, 30), 15, Color("d8e6da"))
		row_name_labels.append(name_label)

		var army := _option(row, "PlayerArmy" if index == 0 else ("EnemyArmy" if index == 1 else "Army%d" % index), Vector2(COL_ARMY_X, 0), Vector2(210, ROW_HEIGHT))
		army.item_selected.connect(func(_i: int) -> void: army_changed.emit())
		row_army_opts.append(army)
		if index == 0:
			player_army_opt = army
		elif index == 1:
			enemy_army_opt = army

		var controller := _option(row, "Type%d" % index, Vector2(COL_TYPE_X, 0), Vector2(120, ROW_HEIGHT))
		controller.add_item("Human")
		controller.add_item("AI")
		controller.select(0 if is_human else 1)
		controller.item_selected.connect(func(_i: int) -> void: controller_changed.emit(index))
		row_controller_opts.append(controller)

		var difficulty := _option(row, "Difficulty%d" % index, Vector2(COL_DIFF_X, 0), Vector2(140, ROW_HEIGHT))
		row_difficulty_opts.append(difficulty)

		var hero := _option(row, "Hero%d" % index, Vector2(COL_HERO_X, 0), Vector2(170, ROW_HEIGHT))
		# The rows are filled in by the menu, which is the only thing that can see
		# the saved heroes and the mounted class table. Until then every row reads
		# "-" - the same thing it reads when the player brings no hero at all.
		hero.add_item("-")
		hero.set_item_metadata(0, "")
		hero.select(0)
		hero.item_selected.connect(func(_i: int) -> void: hero_changed.emit(index))
		hero_dropdowns.append(hero)

		var team := _option(row, "Team%d" % index, Vector2(COL_TEAM_X, 0), Vector2(110, ROW_HEIGHT))
		team.item_selected.connect(func(_i: int) -> void: team_changed.emit(index))
		team.tooltip_text = "Team/alliance: rows sharing a number are allied"
		team_dropdowns.append(team)

		var color_opt := _option(row, "Color%d" % index, Vector2(COL_COLOR_X, 0), Vector2(110, ROW_HEIGHT))
		color_opt.item_selected.connect(func(_i: int) -> void: color_changed.emit(index))
		color_dropdowns.append(color_opt)
		var swatch := ColorRect.new()
		swatch.name = "ColorSwatch%d" % index
		swatch.position = Vector2(COL_SWATCH_X, 5)
		swatch.size = Vector2(30, 30)
		swatch.mouse_filter = Control.MOUSE_FILTER_IGNORE
		row.add_child(swatch)
		color_swatches.append(swatch)

		var handicap := _option(row, "Handicap%d" % index, Vector2(COL_HANDICAP_X, 0), Vector2(120, ROW_HEIGHT))
		handicap.add_item("0%")
		handicap.select(0)
		handicap.disabled = true
		handicap.tooltip_text = "Handicap scaling is not supported by Open BFME yet (difficulty tiers scale AI resources instead)"
		handicap_dropdowns.append(handicap)
	_sync_row_affordances()


func _row_name(index: int, is_human: bool) -> String:
	return "Player %d%s" % [index + 1, "" if is_human else " (AI)"]


func set_max_player_count(maximum: int) -> void:
	## Bound the row count to the selected map's capacity. Clamps the current
	## count down when a smaller map is chosen; the floor is always MIN_PLAYER_ROWS.
	max_player_count = clampi(maximum, MIN_PLAYER_ROWS, MAX_PLAYER_ROWS)
	var clamped := clampi(player_row_count, MIN_PLAYER_ROWS, max_player_count)
	if clamped != player_row_count:
		player_row_count = clamped
		_rebuild_player_rows()
		rows_changed.emit()
	else:
		_sync_row_affordances()


func add_player_row() -> void:
	if player_row_count >= max_player_count:
		return
	player_row_count += 1
	_rebuild_player_rows()
	rows_changed.emit()


func remove_player_row() -> void:
	if player_row_count <= MIN_PLAYER_ROWS:
		return
	player_row_count -= 1
	_rebuild_player_rows()
	rows_changed.emit()


func _sync_row_affordances() -> void:
	if add_player_btn != null:
		add_player_btn.disabled = player_row_count >= max_player_count
	if remove_player_btn != null:
		remove_player_btn.disabled = player_row_count <= MIN_PLAYER_ROWS


func set_row_controller_is_human(row: int, is_human: bool) -> void:
	## Reflects the resolved controller onto a row: the name gains/loses the
	## "(AI)" suffix and the Difficulty dropdown only applies to AI rows.
	if row < 0 or row >= row_name_labels.size():
		return
	row_name_labels[row].text = _row_name(row, is_human)
	var difficulty := row_difficulty_opts[row]
	difficulty.disabled = is_human
	difficulty.modulate = Color(1, 1, 1, 0.35 if is_human else 1.0)
	if is_human:
		difficulty.tooltip_text = "Difficulty applies to AI slots only"
	else:
		difficulty.tooltip_text = ""


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
	play_btn.pressed.connect(_on_play_pressed)


func _on_play_pressed() -> void:
	if not native_core_selected():
		play_pressed.emit()
		return
	var map_id := ""
	var map_name := ""
	if selected_map_row >= 0 and selected_map_row < map_rows.size():
		var row := map_rows[selected_map_row] as Dictionary
		map_id = String(row.get("map_id", ""))
		var button := row.get("button") as Button
		map_name = button.text if button != null else ""
	var document := build_native_match_launch(map_id, map_name)
	if document.is_empty():
		hint_label.text = "NATIVE CORE COULD NOT RESOLVE THIS SETUP"
		return
	var game_state := get_node_or_null("/root/GameState")
	if game_state == null:
		hint_label.text = "NATIVE CORE COULD NOT REACH GAME STATE"
		return
	game_state.set_meta("native_match_launch", document.duplicate(true))
	get_tree().change_scene_to_file("res://scenes/retail_loading_boot.tscn")


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


func native_core_available() -> bool:
	return NativeMatchLaunchScript.is_available()


func native_core_selected() -> bool:
	return (
		engine_opt != null
		and engine_opt.selected >= 0
		and String(engine_opt.get_item_metadata(engine_opt.selected)) == "native"
	)


func build_native_match_launch(retail_map_id: String = "", retail_map_name: String = "") -> Dictionary:
	return NativeMatchLaunchScript.build_from_setup(self, retail_map_id, retail_map_name)


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
