extends Control
## Player-facing menu shell. Proof-stage breadth remains available, but it no
## longer competes with the vertical slice on the main page.

const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const NavDiamondsScript = preload("res://src/ui/openbfme_nav_diamonds.gd")
const SliceScript = preload("res://src/retail_slice/retail_vertical_slice.gd")
const FactionManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")
const MultiplayerLobbyScript = preload("res://src/ui/multiplayer_lobby.gd")
const LockstepSessionScript = preload("res://src/retail_slice/retail_lockstep_session.gd")

const PAGE_MAIN := "main"
const PAGE_SOLO := "solo"
const PAGE_MULTIPLAYER := "multiplayer"
const PAGE_MP_LOBBY := "mp_lobby"
const PAGE_OPTIONS := "options"
const PAGE_DEVELOPER := "developer"
const PAGE_STATS := "stats"

## BFME2 skirmish factions. `id` is the lowercase source object-id prefix the
## retail slice resolves through RetailFactionManifest; `name` is the
## player-facing BFME2 label.
const RETAIL_FACTIONS: Array[Dictionary] = [
	{"id": "men", "name": "Men"},
	{"id": "elves", "name": "Elves"},
	{"id": "dwarves", "name": "Dwarves"},
	{"id": "isengard", "name": "Isengard"},
	{"id": "mordor", "name": "Mordor"},
	{"id": "wild", "name": "Goblins"},
]
const RETAIL_MAP_NAME := "Fords of Isen II"
const NOT_CONVERTED_SUFFIX := " (not converted)"
## Selectable retail vertical-slice maps (five-maps pack + host entry map).
const RETAIL_MAP_CHOICES: Array[Dictionary] = [
	{"id": "bfme2.map.fords-of-isen-ii", "name": "Fords of Isen II"},
	{"id": "bfme2.map.rivendell", "name": "Rivendell"},
	{"id": "bfme2.map.mount-doom", "name": "Mount Doom"},
	{"id": "bfme2.map.dagorlad", "name": "Dagorlad"},
	{"id": "bfme2.map.mordor", "name": "Mordor"},
]
## RULES tab values (retail-typical ladders; 1200/1X are the slice's authored
## defaults, so a fresh setup matches the historical slice exactly).
const RULES_RESOURCE_VALUES: Array[int] = [500, 1000, 1200, 2000, 5000, 10000, 50000]
const RULES_DEFAULT_RESOURCES := 1200
const RULES_FACTOR_VALUES: Array[float] = [0.5, 1.0, 2.0, 4.0]
const RULES_DEFAULT_FACTOR := 1.0
## Build Mode default: false = BFME2 freeform placement (byte-identical default).
const RULES_DEFAULT_BUILD_PLOTS_ONLY := false
## BFME2 1.06 house-color rows for the setup's Color dropdowns. Defaults are
## Blue for the player and Red for the AI — the authored slice team colors.
const HOUSE_COLORS: Array[Dictionary] = [
	{"name": "Blue", "color": Color8(45, 77, 172)},
	{"name": "Red", "color": Color8(166, 32, 28)},
	{"name": "Green", "color": Color8(46, 125, 50)},
	{"name": "Yellow", "color": Color8(214, 198, 46)},
	{"name": "Orange", "color": Color8(217, 124, 30)},
	{"name": "Purple", "color": Color8(124, 63, 160)},
	{"name": "Teal", "color": Color8(46, 158, 155)},
	{"name": "Pink", "color": Color8(214, 107, 168)},
]
## AI difficulty tiers, matching the sim's AI_DIFFICULTY_PROFILES (easy/medium/
## hard/brutal/morgoth). Medium is the sim's AI_DEFAULT_DIFFICULTY, so a default
## AI row is byte-identical to the legacy single-AI setup.
const RETAIL_AI_DIFFICULTIES: Array[Dictionary] = [
	{"id": "easy", "name": "Easy"},
	{"id": "medium", "name": "Medium"},
	{"id": "hard", "name": "Hard"},
	{"id": "brutal", "name": "Brutal"},
	{"id": "morgoth", "name": "Morgoth"},
]
const RETAIL_AI_DEFAULT_DIFFICULTY := "medium"
## Simulation team ids assigned to player rows, skipping NEUTRAL_TEAM (2) which
## the sim reserves for capturable/prop owners. Row i takes TEAM_ID_POOL[i].
const NEUTRAL_TEAM_ID := 2
const TEAM_ID_POOL: Array[int] = [0, 1, 3, 4, 5, 6, 7, 8]
const CONTROLLER_HUMAN := "human"
const CONTROLLER_AI := "ai"

@onready var center: Control = $Center
@onready var menu_frame: Panel = $MenuFrame
@onready var main_heading: Label = $Center/MainHeading
@onready var solo_btn: Button = $Center/Solo
@onready var multiplayer_btn: Button = $Center/Multiplayer
@onready var options_btn: Button = $Center/Options
@onready var quit_btn: Button = $Center/Quit
@onready var subpage_nav: Control = $Center/SubpageNav
@onready var sub_solo_btn: Button = $Center/SubpageNav/Solo
@onready var sub_options_btn: Button = $Center/SubpageNav/Options
@onready var sub_quit_btn: Button = $Center/SubpageNav/Quit

@onready var solo_flyout: Panel = $Center/SoloFlyout
@onready var multiplayer_flyout: Panel = $Center/MultiplayerFlyout
@onready var stats_screen: Panel = $Center/StatsScreen

@onready var options_screen = $Center/OptionsScreen

@onready var developer_frame: Panel = $Center/DeveloperFrame
@onready var developer_heading: Label = $Center/DeveloperHeading
@onready var tests_btn: Button = $Center/Tests
@onready var developer_back_btn: Button = $Center/DeveloperBack
@onready var developer_access_btn: Button = $DeveloperAccess
@onready var status: Label = $Center/Status

var current_page := PAGE_MAIN
var stage_buttons: Array[Button] = []
var _skirmish_availability: Dictionary = {}
var _skirmish_map_notes: Dictionary = {}
var _nav_diamonds: Control
var _slice_probe_instance = null
var _shell_font: Font = null
var _content_db: Node
var _launch_in_progress := false
var _game_state: Node
## GAME LOBBY panel (built in _ready over the NETWORK flyout's rectangle) and
## the pre-game lockstep session it drives. The session lives here only between
## a successful host/join and the lobby's launch/leave; the lobby's _process
## polls it — the menu never does.
var multiplayer_lobby: Panel
var _lobby_session


func _ready() -> void:
	# Guard: any scene arriving here must find an unpaused tree (a pause-open
	# exit from the slice must never leave the menu frozen).
	get_tree().paused = false
	_content_db = get_node_or_null("/root/ContentDB")
	_game_state = get_node_or_null("/root/GameState")
	if _content_db == null or _game_state == null:
		push_error("OpenBFME menu requires the ContentDB and GameState autoloads.")
		return
	_shell_font = _load_retail_font()
	theme = ThemeScript.create_theme(_shell_font)
	# GAME LOBBY panel: same rectangle as the NETWORK flyout it follows, hidden
	# until a host/join connects a session.
	multiplayer_lobby = MultiplayerLobbyScript.new()
	multiplayer_lobby.name = "MultiplayerLobby"
	multiplayer_lobby.position = multiplayer_flyout.position
	multiplayer_lobby.size = multiplayer_flyout.size
	multiplayer_lobby.visible = false
	center.add_child(multiplayer_lobby)
	_populate_skirmish_options()
	_populate_rules_options()
	_populate_color_options()
	_collect_stage_buttons()
	_connect_actions()
	options_screen.configure({"font": _shell_font})
	options_screen.closed.connect(func(_applied: bool) -> void: _show_page(PAGE_MAIN))
	_build_nav_diamonds()
	_show_page(PAGE_MAIN)
	# Stored display/graphics settings apply from the first frame onward so the
	# shell and the slice share one window/quality state.
	call_deferred("_apply_boot_settings")
	status.text = "Content: %d units, %d buildings, %d factions, %d maps, %d powers" % [
		(_content_db.get("units") as Dictionary).size(), (_content_db.get("buildings") as Dictionary).size(),
		(_content_db.get("factions") as Dictionary).size(), (_content_db.get("maps") as Dictionary).size(),
		(_content_db.get("powers") as Dictionary).size()
	]


func _apply_boot_settings() -> void:
	options_screen.apply_stored_settings()


func _exit_tree() -> void:
	# The classification/map-resolution probe never enters the tree, so it
	# cannot rely on automatic cleanup.
	if _slice_probe_instance != null:
		_slice_probe_instance.free()
		_slice_probe_instance = null
	# A menu freed with a live pre-game session (external scene change) must
	# release the socket; the peer receives the notified disconnect.
	if _lobby_session != null:
		_lobby_session.close()
		_lobby_session = null


func _load_retail_font() -> Font:
	## The converted packs ship Albertus MT under assets/ui/palantir/fonts.
	## Using it at runtime keeps the shell on converted retail art without
	## copying font bytes into the repository; when no pack carries the face,
	## the theme simply keeps Godot's default font (styled UI, not a fake).
	for pack_root in _font_pack_root_candidates():
		var fonts_dir := pack_root.path_join("assets/ui/palantir/fonts")
		var dir := DirAccess.open(fonts_dir)
		if dir == null:
			continue
		for file in dir.get_files():
			if file.get_extension() != "otf" and file.get_extension() != "ttf":
				continue
			var font := FontFile.new()
			if font.load_dynamic_font(fonts_dir.path_join(file)) == OK:
				return font
	return null


func _font_pack_root_candidates() -> Array[String]:
	var roots: Dictionary = {}
	var member := _content_db.call("get_bundle_object", SliceScript.SOLDIER_OBJECT_ID) as Dictionary
	roots[String(member.get("_pack_root", ""))] = true
	for registry in [_content_db.call("get_playable_unit_runtimes"), _content_db.call("get_playable_structure_runtimes")]:
		for document_value in (registry as Dictionary).values():
			roots[String((document_value as Dictionary).get("_pack_root", ""))] = true
	var ordered: Array[String] = []
	for root_value in roots.keys():
		var root := String(root_value)
		if root != "":
			ordered.append(root)
	ordered.sort()
	return ordered


func _build_nav_diamonds() -> void:
	_nav_diamonds = NavDiamondsScript.new()
	_nav_diamonds.name = "NavDiamonds"
	_nav_diamonds.z_index = 3
	add_child(_nav_diamonds)
	var nav_buttons: Array[Button] = [solo_btn, options_btn, quit_btn, sub_solo_btn, sub_options_btn, sub_quit_btn]
	_nav_diamonds.watch(nav_buttons)


func _populate_skirmish_options() -> void:
	# Mirror the slice's leading fail-closed gate: when the men pack's bundle
	# content is absent, reload once before judging faction availability.
	if not (_content_db.get("bundle_objects") as Dictionary).has(SliceScript.SOLDIER_OBJECT_ID):
		_content_db.call("reload")
	_skirmish_availability.clear()
	for faction in RETAIL_FACTIONS:
		var faction_id := String(faction["id"])
		_skirmish_availability[faction_id] = _retail_faction_availability(faction_id)
	_populate_row_controls()
	# Populate every known retail map the slice can boot (five-maps pack +
	# host Fords entry). Maps the slice cannot resolve stay listed but are
	# disabled with the honest reason the slice would refuse them.
	_skirmish_map_notes.clear()
	for map_index in RETAIL_MAP_CHOICES.size():
		var choice: Dictionary = RETAIL_MAP_CHOICES[map_index]
		var map_id := String(choice["id"])
		var note := retail_map_availability(map_id)
		_skirmish_map_notes[map_id] = note
		var available := note == ""
		var map_doc := _content_db.call("get_bundle_map", map_id) as Dictionary
		var players := int(map_doc.get("playerCount", 0))
		var row := Button.new()
		row.name = "MapRow%d" % map_index
		row.toggle_mode = true
		row.alignment = HORIZONTAL_ALIGNMENT_LEFT
		row.text = String(choice["name"]) + ("" if available else " (unavailable)")
		# Retail's map list is a compact table of text rows, not ornate buttons.
		row.custom_minimum_size = Vector2(0, 28)
		for state in ["normal", "hover", "pressed", "disabled"]:
			var row_box := StyleBoxFlat.new()
			row_box.bg_color = Color(0.05, 0.16, 0.09, 0.72) if state != "pressed" else Color(0.16, 0.34, 0.14, 0.92)
			if state == "hover":
				row_box.bg_color = Color(0.10, 0.26, 0.11, 0.88)
			if state == "disabled":
				row_box.bg_color = Color(0.04, 0.10, 0.06, 0.55)
			row_box.border_color = Color(0.30, 0.62, 0.28, 0.85) if state == "pressed" else Color(0.18, 0.42, 0.20, 0.6)
			row_box.set_border_width_all(1)
			row_box.content_margin_left = 12.0
			row.add_theme_stylebox_override(state, row_box)
		row.add_theme_font_size_override("font_size", 16)
		row.disabled = not available
		if not available:
			row.tooltip_text = "Unavailable: %s" % note
		row.set_meta("map_id", map_id)
		row.set_meta("player_count", players)
		var players_label := Label.new()
		players_label.name = "Players"
		players_label.text = str(players) if players > 0 else "?"
		players_label.set_anchors_and_offsets_preset(Control.PRESET_CENTER_RIGHT)
		players_label.offset_left = -48
		players_label.offset_right = -16
		players_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		players_label.add_theme_font_size_override("font_size", 15)
		players_label.add_theme_color_override("font_color", Color("b7dc94"))
		players_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		row.add_child(players_label)
		row.pressed.connect(_on_map_row_pressed.bind(map_index))
		solo_flyout.map_rows_host.add_child(row)
		solo_flyout.map_rows.append({"button": row, "map_id": map_id, "players": players})
	_select_first_available_map_row()


func _populate_rules_options() -> void:
	solo_flyout.initial_resources_opt.clear()
	for value in RULES_RESOURCE_VALUES:
		solo_flyout.initial_resources_opt.add_item(str(value))
		solo_flyout.initial_resources_opt.set_item_metadata(solo_flyout.initial_resources_opt.item_count - 1, value)
	solo_flyout.cp_factor_opt.clear()
	for factor in RULES_FACTOR_VALUES:
		var label := "%sX" % str(factor).trim_suffix(".0")
		solo_flyout.cp_factor_opt.add_item(label)
		solo_flyout.cp_factor_opt.set_item_metadata(solo_flyout.cp_factor_opt.item_count - 1, factor)
	solo_flyout.build_mode_opt.clear()
	solo_flyout.build_mode_opt.add_item("BFME2 Freeform")
	solo_flyout.build_mode_opt.set_item_metadata(solo_flyout.build_mode_opt.item_count - 1, false)
	solo_flyout.build_mode_opt.add_item("BFME1 Plots")
	solo_flyout.build_mode_opt.set_item_metadata(solo_flyout.build_mode_opt.item_count - 1, true)
	_select_option_by_metadata_value(solo_flyout.initial_resources_opt, RULES_DEFAULT_RESOURCES)
	_select_option_by_metadata_value(solo_flyout.cp_factor_opt, RULES_DEFAULT_FACTOR)
	_select_option_by_metadata_value(solo_flyout.build_mode_opt, RULES_DEFAULT_BUILD_PLOTS_ONLY)


func _populate_color_options() -> void:
	# Colors are populated per row by _populate_row_controls(); kept as a thin
	# entry point so the boot sequence order in _ready() stays explicit.
	_populate_row_controls()


func _populate_row_controls() -> void:
	## Fills every current player row's Army / Difficulty / Team / Color dropdowns
	## and reflects each row's controller. Re-run whenever the row set is rebuilt
	## (add/remove/map-capacity clamp) so the new rows carry valid options.
	for row in range(solo_flyout.row_army_opts.size()):
		_populate_row_army(row)
		_populate_row_difficulty(row)
		_populate_row_team(row)
		_populate_row_color(row)
		_apply_row_controller(row)


func _populate_row_army(row: int) -> void:
	var option: OptionButton = solo_flyout.row_army_opts[row]
	option.clear()
	for faction in RETAIL_FACTIONS:
		var faction_id := String(faction["id"])
		var note := String(_skirmish_availability.get(faction_id, ""))
		option.add_item(String(faction["name"]) + (NOT_CONVERTED_SUFFIX if note != "" else ""))
		var index := option.item_count - 1
		option.set_item_metadata(index, faction_id)
		option.set_item_disabled(index, note != "")
		if note != "":
			option.set_item_tooltip(index, "Not converted: %s" % note)
	_select_first_enabled(option)


func _populate_row_difficulty(row: int) -> void:
	var option: OptionButton = solo_flyout.row_difficulty_opts[row]
	option.clear()
	for tier in RETAIL_AI_DIFFICULTIES:
		option.add_item(String(tier["name"]))
		option.set_item_metadata(option.item_count - 1, String(tier["id"]))
	_select_option_by_metadata_value(option, RETAIL_AI_DEFAULT_DIFFICULTY)


func _populate_row_team(row: int) -> void:
	## The retail Team column is the alliance grouping: rows sharing a number are
	## allied. Default assigns each row its own number (row i -> i+1) so a fresh
	## setup is free-for-all (every team mutually hostile).
	var option: OptionButton = solo_flyout.team_dropdowns[row]
	option.clear()
	option.disabled = false
	for number in range(1, solo_flyout.MAX_PLAYER_ROWS + 1):
		option.add_item(str(number))
		option.set_item_metadata(option.item_count - 1, number)
	option.select(mini(row, option.item_count - 1))
	option.tooltip_text = "Team/alliance: rows sharing a number fight as allies"


func _populate_row_color(row: int) -> void:
	var option: OptionButton = solo_flyout.color_dropdowns[row]
	option.clear()
	for entry in HOUSE_COLORS:
		option.add_item(String(entry["name"]))
		option.set_item_metadata(option.item_count - 1, entry["color"])
	option.select(row % HOUSE_COLORS.size())
	_on_color_changed(row)


func _apply_row_controller(row: int) -> void:
	var option: OptionButton = solo_flyout.row_controller_opts[row]
	var is_human := option.selected >= 0 and option.get_item_text(option.selected) == "Human"
	solo_flyout.set_row_controller_is_human(row, is_human)


func _selected_row_difficulty(row: int) -> String:
	var option: OptionButton = solo_flyout.row_difficulty_opts[row]
	if option.selected < 0:
		return RETAIL_AI_DEFAULT_DIFFICULTY
	return String(option.get_item_metadata(option.selected))


func _selected_row_alliance(row: int) -> int:
	var option: OptionButton = solo_flyout.team_dropdowns[row]
	if option.selected < 0:
		return row + 1
	return int(option.get_item_metadata(option.selected))


func _selected_row_color(row: int) -> Color:
	var option: OptionButton = solo_flyout.color_dropdowns[row]
	if option.selected < 0:
		return HOUSE_COLORS[row % HOUSE_COLORS.size()]["color"]
	return option.get_item_metadata(option.selected)


func _row_is_human(row: int) -> bool:
	var option: OptionButton = solo_flyout.row_controller_opts[row]
	return option.selected >= 0 and option.get_item_text(option.selected) == "Human"


func _on_rows_changed() -> void:
	_populate_row_controls()
	_refresh_start_row()
	_refresh_skirmish_launch_state()


func _on_controller_changed(row: int) -> void:
	# Exactly one local human: choosing Human on a row demotes every other row to
	# AI (radio behavior), mirroring retail skirmish where you hold a single slot.
	if _row_is_human(row):
		for other in range(solo_flyout.row_controller_opts.size()):
			if other != row and _row_is_human(other):
				solo_flyout.row_controller_opts[other].select(1)
	elif _human_row_index() == -1:
		# The human slot may never vanish; keep this row human if it was the last.
		solo_flyout.row_controller_opts[row].select(0)
	for other in range(solo_flyout.row_controller_opts.size()):
		_apply_row_controller(other)
	_refresh_skirmish_launch_state()


func _human_row_index() -> int:
	for row in range(solo_flyout.row_controller_opts.size()):
		if _row_is_human(row):
			return row
	return -1


func _on_color_changed(row: int) -> void:
	var option: OptionButton = solo_flyout.color_dropdowns[row]
	if option.selected < 0:
		return
	var color: Color = option.get_item_metadata(option.selected)
	solo_flyout.color_swatches[row].color = color
	# Rows 0/1 also drive the legacy two-side color fields so an unchanged default
	# setup writes byte-identical GameState.
	if row == 0:
		_game_state.set("retail_player_color", color)
	elif row == 1:
		_game_state.set("retail_enemy_color", color)


func _refresh_start_row() -> void:
	for child in solo_flyout.start_row.get_children():
		child.queue_free()
	solo_flyout.start_buttons.clear()
	solo_flyout.start_note.visible = false
	var map_id := _selected_skirmish_map()
	if map_id == "":
		return
	var starts := _read_map_start_indices(map_id)
	if starts.is_empty():
		solo_flyout.start_note.text = "Player starts are not converted for this map"
		solo_flyout.start_note.visible = true
		return
	for start_index in starts:
		var button := Button.new()
		button.name = "Start%d" % start_index
		button.text = str(start_index)
		button.toggle_mode = true
		button.custom_minimum_size = Vector2(44, 30)
		button.tooltip_text = "Take Player_%d_Start for your side; the AI takes another" % start_index
		button.pressed.connect(_on_start_button_pressed.bind(start_index))
		solo_flyout.start_row.add_child(button)
		solo_flyout.start_buttons.append(button)
	_sync_start_button_states()


func _on_start_button_pressed(start_index: int) -> void:
	var current := int(_game_state.get("retail_player_start_index"))
	_game_state.set("retail_player_start_index", 0 if current == start_index else start_index)
	_sync_start_button_states()


func _sync_start_button_states() -> void:
	var current := int(_game_state.get("retail_player_start_index"))
	for button in solo_flyout.start_buttons:
		button.button_pressed = int(button.text) == current


func _read_map_start_indices(map_id: String) -> Array[int]:
	var indices: Array[int] = []
	var map_doc := _skirmish_map_document(map_id)
	var source_path := String(map_doc.get("_source", ""))
	if source_path == "":
		var pack_root := String(map_doc.get("_pack_root", ""))
		var map_relative := String(map_doc.get("map", ""))
		if pack_root != "" and map_relative != "":
			source_path = pack_root.path_join(map_relative)
	if source_path == "":
		return indices
	var document := _read_bounded_json(source_path.get_base_dir().path_join("waypoints.json"), SliceScript.MAP_DOCUMENT_MAX_BYTES)
	for start_name in (document.get("playerStarts", {}) as Dictionary).keys():
		var row := (document.get("playerStarts", {}) as Dictionary)[start_name] as Dictionary
		var player_index := int(row.get("playerIndex", -1))
		if player_index > 0 and not indices.has(player_index):
			indices.append(player_index)
	indices.sort()
	return indices


func _select_option_by_metadata_value(option: OptionButton, metadata_value: Variant) -> void:
	for index in range(option.item_count):
		if option.get_item_metadata(index) == metadata_value:
			option.select(index)
			return


func _select_first_available_map_row() -> void:
	for index in solo_flyout.map_rows.size():
		if not (solo_flyout.map_rows[index]["button"] as Button).disabled:
			_on_map_row_pressed(index)
			return
	_on_map_row_pressed(-1)


func _on_map_row_pressed(index: int) -> void:
	solo_flyout.set_selected_map_row(index)
	# The selected map's authored player count bounds how many rows the setup can
	# add. Clamping down may rebuild rows (rows_changed re-populates them).
	var capacity := _selected_map_player_capacity()
	if capacity >= solo_flyout.MIN_PLAYER_ROWS:
		solo_flyout.set_max_player_count(capacity)
	_refresh_map_preview()
	_refresh_map_description()
	_refresh_start_row()
	_refresh_skirmish_launch_state()


func _selected_map_player_capacity() -> int:
	## The selected map's authored player count, bounded to the setup ceiling. The
	## number of authored player starts is the hard cap (a team needs a spawn), so
	## the smaller of playerCount and the resolvable start count wins.
	var map_id := _selected_skirmish_map()
	if map_id == "":
		return solo_flyout.MIN_PLAYER_ROWS
	var map_doc := _skirmish_map_document(map_id)
	var declared := int(map_doc.get("playerCount", 0))
	var starts := _read_map_start_indices(map_id).size()
	var capacity := declared
	if starts > 0:
		capacity = starts if declared <= 0 else mini(declared, starts)
	return maxi(solo_flyout.MIN_PLAYER_ROWS, mini(capacity, solo_flyout.MAX_PLAYER_ROWS))


func _select_first_enabled(option: OptionButton) -> void:
	for index in range(option.item_count):
		if not option.is_item_disabled(index):
			option.select(index)
			return
	option.select(-1)


func _retail_faction_availability(faction_id: String) -> String:
	## "" when the slice can mount the faction, else the fail-closed note. Men
	## always requires the host pack gate. Every faction — Men included once
	## converted runtimes exist — runs exactly the manifest resolution the
	## slice runs over the slice's own fieldable-unit classification, so the
	## menu never errors on a unit the slice would exclude nor passes one the
	## slice cannot field.
	if faction_id == FactionManifestScript.DEFAULT_FACTION:
		var pack_error := _men_pack_gate_error()
		if pack_error != "":
			return pack_error
	var fieldable := _slice_fieldable_unit_runtimes(faction_id)
	var structures: Dictionary = _content_db.call("get_playable_structure_runtimes")
	if faction_id == FactionManifestScript.DEFAULT_FACTION and fieldable.is_empty() and structures.is_empty():
		return ""
	var manifest: Dictionary = FactionManifestScript.from_registries(
		faction_id, fieldable, structures
	)
	return String(manifest.get("_error", ""))


func _slice_fieldable_unit_runtimes(faction_id: String) -> Dictionary:
	## Read-only consumption of the slice's roster classification: the same
	## fieldableUnit set retail_vertical_slice hands to from_registries. The
	## classifier only touches ContentDB and instance vars, so a bare probe
	## instance runs it without booting the slice.
	var probe := _slice_probe()
	if probe == null:
		return {}
	probe._classify_faction_units(faction_id)
	return (probe.get("fieldable_unit_runtimes") as Dictionary).duplicate(true)


func _slice_probe() -> RetailVerticalSlice:
	if _slice_probe_instance == null:
		_slice_probe_instance = SliceScript.new()
	return _slice_probe_instance


func _selected_faction_pack_root() -> String:
	var member := _content_db.call("get_bundle_object", SliceScript.SOLDIER_OBJECT_ID) as Dictionary
	return String(member.get("_pack_root", ""))


func _men_pack_gate_error() -> String:
	## The first fail-closed checks retail_vertical_slice runs for the default
	## Men manifest: soldier/horde/map bundle documents, the soldier animation
	## capability, and the selected pack identity.
	var member := _content_db.call("get_bundle_object", SliceScript.SOLDIER_OBJECT_ID) as Dictionary
	var horde := _content_db.call("get_bundle_object", SliceScript.SOLDIER_HORDE_ID) as Dictionary
	var map_definition := _content_db.call("get_bundle_map", SliceScript.MAP_ID) as Dictionary
	if member.is_empty() or horde.is_empty() or map_definition.is_empty():
		return "the private bfme2-men-vslice pack is not selected (run run_importer.bat)"
	var capability := _content_db.call("get_animation_capability", String(member.get("animationCapabilityId", ""))) as Dictionary
	if capability.is_empty():
		return "the bfme2-men-vslice pack soldier animation capability is missing"
	var pack_root := String(member.get("_pack_root", ""))
	if pack_root == "" or String((ModLoader._read_json(pack_root.path_join("pack.json")) as Dictionary).get("id", "")) != FactionManifestScript.DEFAULT_PACK_ID:
		return "the selected content pack is not %s" % FactionManifestScript.DEFAULT_PACK_ID
	return ""


func get_retail_faction_availability() -> Dictionary:
	return _skirmish_availability.duplicate()


func retail_launch_error() -> String:
	## "" when a skirmish launch may proceed, else the player-facing reason it
	## is blocked. A blocked launch never falls back silently. Every row is
	## validated fail-closed: its faction must be convertible (the same per-faction
	## availability signal the slice uses), the roster must contain at least two
	## mutually-hostile alliances, and each team must claim a distinct authored
	## player start.
	var host_error := _men_pack_gate_error()
	if host_error != "":
		return "The retail slice host pack is unavailable: %s." % host_error
	var map_id := _selected_skirmish_map()
	if map_id == "":
		return "No retail map is selectable. Ensure bfme2-five-maps is selected."
	var row_count: int = solo_flyout.row_army_opts.size()
	for row in range(row_count):
		var faction_id := _selected_skirmish_faction(solo_flyout.row_army_opts[row])
		if faction_id == "":
			return "No converted faction is selectable yet. Convert a faction pack first."
		var note := String(_skirmish_availability.get(faction_id, "not converted"))
		if note != "":
			return "Player %d faction %s is not converted yet: %s." % [row + 1, _retail_faction_display_name(faction_id), note]
	if _human_row_index() == -1:
		return "One slot must be the local player (Human)."
	var alliances: Dictionary = {}
	for row in range(row_count):
		alliances[_selected_row_alliance(row)] = true
	if alliances.size() < 2:
		return "All players share one team; a skirmish needs at least two hostile teams."
	if _assign_start_indices(map_id, row_count).is_empty():
		return "This map provides fewer authored player starts than the %d players selected." % row_count
	return ""


func _assign_start_indices(map_id: String, row_count: int) -> Array[int]:
	## One distinct authored player start per row, or [] when the map cannot seat
	## every team. Row 0 (the human) keeps its manually chosen start when valid;
	## the rest take the remaining authored starts in ascending order.
	var starts := _read_map_start_indices(map_id)
	if starts.size() < row_count:
		return []
	var assigned: Array[int] = []
	var pool := starts.duplicate()
	var human_start := int(_game_state.get("retail_player_start_index"))
	if human_start > 0 and pool.has(human_start):
		assigned.append(human_start)
		pool.erase(human_start)
	else:
		assigned.append(int(pool.pop_front()))
	for _row in range(1, row_count):
		assigned.append(int(pool.pop_front()))
	return assigned


func _selected_skirmish_faction(option: OptionButton) -> String:
	if option.selected < 0:
		return ""
	return String(option.get_item_metadata(option.selected))


func _retail_faction_display_name(faction_id: String) -> String:
	for faction in RETAIL_FACTIONS:
		if String(faction["id"]) == faction_id:
			return String(faction["name"])
	return faction_id.capitalize()


func _retail_map_display_name(map_id: String) -> String:
	for map_choice in RETAIL_MAP_CHOICES:
		if String(map_choice["id"]) == map_id:
			return String(map_choice["name"])
	return map_id.trim_prefix("bfme2.map.").capitalize()


func _skirmish_map_document(map_id: String) -> Dictionary:
	## For the Fords entry map the selected faction pack's files.entryMap is the
	## document the slice boots; every other map resolves from registered
	## content. Shared by the preview and the description panel.
	if map_id == SliceScript.MAP_ID:
		var probe := _slice_probe()
		if probe != null:
			var entry_doc: Dictionary = probe._resolve_pack_entry_map_definition(_selected_faction_pack_root(), map_id)
			if not entry_doc.is_empty():
				return entry_doc
	return _content_db.call("get_bundle_map", map_id) as Dictionary


func _refresh_map_preview(_index: int = 0) -> void:
	## Shows the selected map's converted preview art from the pack that owns
	## the map document. For the Fords entry map that is the selected faction
	## pack's files.entryMap — the same document the slice boots — so a
	## supplemental map pack's variant never shadows the selected pack's art.
	## Fail closed: no synthetic stand-in is ever drawn when the art is absent.
	var map_id := _selected_skirmish_map()
	var texture: Texture2D = null
	var caption := ""
	if map_id != "":
		var map_doc := _skirmish_map_document(map_id)
		caption = String(map_doc.get("name", map_doc.get("displayName", "")))
		var preview_rel := String(map_doc.get("preview", ""))
		var pack_root := String(map_doc.get("_pack_root", ""))
		if preview_rel != "" and pack_root != "":
			var path := String(_content_db.call("resolve_asset", preview_rel, pack_root))
			if path != "" and FileAccess.file_exists(path):
				var image := Image.load_from_file(path)
				if image != null and not image.is_empty():
					texture = ImageTexture.create_from_image(image)
	solo_flyout.preview_image.texture = texture
	solo_flyout.preview_image.visible = texture != null
	solo_flyout.preview_caption.text = caption if caption != "" else _retail_map_display_name(map_id)


func _refresh_map_description() -> void:
	## Description text comes only from the map's authored document. No map in
	## the converted packs carries a description field today, so the panel
	## honestly records that instead of inventing lore.
	var map_id := _selected_skirmish_map()
	solo_flyout.description_title.text = _retail_map_display_name(map_id) if map_id != "" else ""
	var description := ""
	if map_id != "":
		var map_doc := _skirmish_map_document(map_id)
		description = String(map_doc.get("description", map_doc.get("lore", ""))).strip_edges()
	solo_flyout.description_label.text = description if description != "" else "No authored description has been converted for this map yet."


func _refresh_skirmish_launch_state(_index: int = 0) -> void:
	var launch_error := retail_launch_error()
	solo_flyout.play_btn.disabled = launch_error != ""
	if launch_error != "":
		solo_flyout.hint_label.text = launch_error
		return
	var pending: Array[String] = []
	for faction in RETAIL_FACTIONS:
		if String(_skirmish_availability.get(String(faction["id"]), "")) != "":
			pending.append(String(faction["name"]).to_upper())
	solo_flyout.hint_label.text = "ALL FACTIONS CONVERTED" if pending.is_empty() else "NOT CONVERTED YET: %s" % ", ".join(pending)


func apply_skirmish_selection() -> bool:
	## Validates the skirmish setup fail-closed and records it on GameState
	## for the retail slice. On failure GameState is left untouched and the
	## launch button re-syncs to the blocked state.
	var launch_error := retail_launch_error()
	if launch_error != "":
		_refresh_skirmish_launch_state()
		return false
	# A solo launch is always single-player: clear any multiplayer selection a
	# previous NETWORK visit left behind so the slice never hosts by surprise.
	_game_state.set("retail_mp_mode", "")
	var map_id := _selected_skirmish_map()
	# Legacy two-side fields: row 0 is the human, the first AI slot is the "enemy".
	# They stay authoritative for the byte-identical default launch and remain the
	# fallback whenever no N-team descriptor list is present (retail_team_setup []).
	var human_row := maxi(0, _human_row_index())
	var enemy_row := _first_non_human_row()
	_game_state.set("retail_player_faction", _selected_skirmish_faction(solo_flyout.row_army_opts[human_row]))
	_game_state.set("retail_enemy_faction", _selected_skirmish_faction(solo_flyout.row_army_opts[enemy_row]))
	_game_state.set("retail_map_id", map_id if map_id != "" else SliceScript.MAP_ID)
	_game_state.set("retail_initial_resources", _selected_rules_resources())
	_game_state.set("retail_command_point_factor", _selected_rules_factor())
	_game_state.set("retail_build_plots_only", _selected_build_plots_only())
	# N-team descriptor list: authoritative when present. Only written for setups
	# the legacy pair cannot express (>2 rows, or a non-medium AI tier); the exact
	# legacy default clears it so the slice keeps its proven two-team path and the
	# pinned battle signature is untouched.
	if _setup_is_advanced():
		_game_state.set("retail_team_setup", _build_team_descriptors(map_id))
	else:
		_game_state.set("retail_team_setup", [])
	return true


func _first_non_human_row() -> int:
	for row in range(solo_flyout.row_army_opts.size()):
		if not _row_is_human(row):
			return row
	return mini(1, solo_flyout.row_army_opts.size() - 1)


func _setup_is_advanced() -> bool:
	## True when the setup carries something the legacy two-side fields cannot
	## represent: more than two rows, or any AI slot on a non-default difficulty.
	if solo_flyout.row_army_opts.size() > 2:
		return true
	for row in range(solo_flyout.row_army_opts.size()):
		if not _row_is_human(row) and _selected_row_difficulty(row) != RETAIL_AI_DEFAULT_DIFFICULTY:
			return true
	return false


func _build_team_descriptors(map_id: String) -> Array:
	## The full N-team roster the slice hands to the sim. One descriptor per row,
	## each carrying its sim team id (skipping NEUTRAL_TEAM), faction, controller,
	## AI difficulty, alliance number, house color, and a distinct authored start.
	var row_count: int = solo_flyout.row_army_opts.size()
	var starts := _assign_start_indices(map_id, row_count)
	var descriptors: Array = []
	for row in range(row_count):
		var is_human := _row_is_human(row)
		descriptors.append({
			"team": TEAM_ID_POOL[row],
			"faction": _selected_skirmish_faction(solo_flyout.row_army_opts[row]),
			"controller": CONTROLLER_HUMAN if is_human else CONTROLLER_AI,
			"difficulty": RETAIL_AI_DEFAULT_DIFFICULTY if is_human else _selected_row_difficulty(row),
			"alliance": _selected_row_alliance(row),
			"color": _selected_row_color(row),
			"start_index": starts[row] if row < starts.size() else 0,
		})
	return descriptors


func _selected_rules_resources() -> int:
	var option: OptionButton = solo_flyout.initial_resources_opt
	if option.selected < 0:
		return RULES_DEFAULT_RESOURCES
	return int(option.get_item_metadata(option.selected))


func _selected_rules_factor() -> float:
	var option: OptionButton = solo_flyout.cp_factor_opt
	if option.selected < 0:
		return RULES_DEFAULT_FACTOR
	return float(option.get_item_metadata(option.selected))


func _selected_build_plots_only() -> bool:
	var option: OptionButton = solo_flyout.build_mode_opt
	if option.selected < 0:
		return RULES_DEFAULT_BUILD_PLOTS_ONLY
	return bool(option.get_item_metadata(option.selected))


func _on_rules_changed(_index: int = 0) -> void:
	_game_state.set("retail_initial_resources", _selected_rules_resources())
	_game_state.set("retail_command_point_factor", _selected_rules_factor())
	_game_state.set("retail_build_plots_only", _selected_build_plots_only())


func _on_rules_reset() -> void:
	_select_option_by_metadata_value(solo_flyout.initial_resources_opt, RULES_DEFAULT_RESOURCES)
	_select_option_by_metadata_value(solo_flyout.cp_factor_opt, RULES_DEFAULT_FACTOR)
	_select_option_by_metadata_value(solo_flyout.build_mode_opt, RULES_DEFAULT_BUILD_PLOTS_ONLY)
	_on_rules_changed()


func _selected_skirmish_map() -> String:
	if solo_flyout.selected_map_row < 0 or solo_flyout.selected_map_row >= solo_flyout.map_rows.size():
		return ""
	var row: Dictionary = solo_flyout.map_rows[solo_flyout.selected_map_row]
	if (row["button"] as Button).disabled:
		return ""
	return String(row["map_id"])


func retail_map_availability(map_id: String) -> String:
	## "" when the slice resolves and boots the map, else the honest reason it
	## is disabled. The verdict is the slice's own resolution (registered
	## content, selected-pack entry map, env-required five-maps catalog with
	## schema-validated map documents) so the menu never offers a map the
	## slice would refuse; the reason chain mirrors the same steps.
	var probe := _slice_probe()
	if probe == null:
		return "slice map resolution is unavailable"
	probe.selected_pack_root = _selected_faction_pack_root()
	var resolved: Dictionary = probe._resolve_slice_map_definition(map_id)
	if not resolved.is_empty():
		return ""
	if map_id == SliceScript.MAP_ID:
		return "the selected pack's files.entryMap is missing or invalid; the slice boots Fords only from the selected pack"
	if (_content_db.get("bundle_maps") as Dictionary).has(map_id):
		return ""
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	if content_root == "" or not DirAccess.dir_exists_absolute(content_root):
		return "not registered in selection.json and OPENBFME_CONTENT is unset; the slice requires it for catalog maps"
	var pack_root := ModLoader.resolve_pack_path(content_root, SliceScript.FIVE_MAPS_PACK_ID)
	if pack_root == "" or not ModLoader.path_is_within(content_root, pack_root) or not DirAccess.dir_exists_absolute(pack_root):
		return "not registered in selection.json and the %s pack is not present under OPENBFME_CONTENT" % SliceScript.FIVE_MAPS_PACK_ID
	var catalog := _read_bounded_json(pack_root.path_join("data/maps.json"), SliceScript.MAP_CATALOG_MAX_BYTES)
	if String(catalog.get("schema", "")) != "openbfme.map-catalog" or int(catalog.get("schemaVersion", -1)) != 0:
		return "the %s catalog failed schema validation" % SliceScript.FIVE_MAPS_PACK_ID
	var rows: Variant = catalog.get("maps", null)
	if typeof(rows) != TYPE_ARRAY:
		return "the %s catalog failed schema validation" % SliceScript.FIVE_MAPS_PACK_ID
	for row_value in rows as Array:
		var row := row_value as Dictionary
		if row == null or String(row.get("id", "")) != map_id:
			continue
		var map_relative := String(row.get("map", ""))
		if map_relative == "" or not ModLoader.is_safe_relative_path(map_relative):
			return "its catalog row does not declare a safe map document path"
		var map_doc := _read_bounded_json(pack_root.path_join(map_relative), SliceScript.MAP_DOCUMENT_MAX_BYTES)
		if String(map_doc.get("schema", "")) != "openbfme.map" or int(map_doc.get("schemaVersion", -1)) != 0:
			return "its map document is missing or failed schema validation"
		if String(map_doc.get("id", "")) != map_id:
			return "its map document id does not match the catalog row"
		return "unresolvable by the slice"
	return "not registered in selection.json and absent from the %s catalog" % SliceScript.FIVE_MAPS_PACK_ID


func _read_bounded_json(path: String, maximum_bytes: int) -> Dictionary:
	## Bounded JSON read matching the slice's pack-document guardrails.
	if maximum_bytes <= 0 or not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > maximum_bytes:
		return {}
	var text := file.get_as_text()
	file.close()
	var value: Variant = JSON.parse_string(text)
	return (value as Dictionary) if typeof(value) == TYPE_DICTIONARY else {}


func _collect_stage_buttons() -> void:
	stage_buttons.clear()
	for stage_index in range(1, 10):
		var button := get_node("Center/Stage%d" % stage_index) as Button
		# Retain a concrete hidden-page hit/readability rectangle for the Stage 10
		# compatibility gate, which intentionally invokes these developer routes
		# without first presenting the developer page.
		button.custom_minimum_size = Vector2(620.0, 44.0)
		button.size = Vector2(maxf(button.size.x, 620.0), maxf(button.size.y, 44.0))
		stage_buttons.append(button)


func _connect_actions() -> void:
	solo_btn.pressed.connect(func() -> void: _show_page(PAGE_SOLO))
	multiplayer_btn.pressed.connect(func() -> void: _show_page(PAGE_MULTIPLAYER))
	options_btn.pressed.connect(_on_options)
	quit_btn.pressed.connect(func() -> void: get_tree().quit())
	multiplayer_flyout.host_requested.connect(_on_multiplayer_host)
	multiplayer_flyout.join_requested.connect(_on_multiplayer_join)
	multiplayer_flyout.back_requested.connect(func() -> void: _show_page(PAGE_MAIN))
	multiplayer_lobby.launch_confirmed.connect(_on_lobby_launch_confirmed)
	multiplayer_lobby.leave_requested.connect(_on_lobby_leave)
	sub_solo_btn.pressed.connect(func() -> void: _show_page(PAGE_SOLO))
	sub_options_btn.pressed.connect(_on_options)
	sub_quit_btn.pressed.connect(func() -> void: get_tree().quit())
	solo_flyout.army_changed.connect(_refresh_skirmish_launch_state)
	solo_flyout.color_changed.connect(_on_color_changed)
	solo_flyout.rows_changed.connect(_on_rows_changed)
	solo_flyout.controller_changed.connect(_on_controller_changed)
	solo_flyout.team_changed.connect(func(_row: int) -> void: _refresh_skirmish_launch_state())
	solo_flyout.play_pressed.connect(_on_retail)
	solo_flyout.main_menu_pressed.connect(func() -> void: _show_page(PAGE_MAIN))
	solo_flyout.stats_pressed.connect(func() -> void: _show_page(PAGE_STATS))
	solo_flyout.rules_reset_btn.pressed.connect(_on_rules_reset)
	solo_flyout.initial_resources_opt.item_selected.connect(_on_rules_changed)
	solo_flyout.cp_factor_opt.item_selected.connect(_on_rules_changed)
	solo_flyout.build_mode_opt.item_selected.connect(_on_rules_changed)
	stats_screen.back_pressed.connect(func() -> void: _show_page(PAGE_SOLO))
	developer_access_btn.pressed.connect(func() -> void: _show_page(PAGE_DEVELOPER))
	developer_back_btn.pressed.connect(func() -> void: _show_page(PAGE_MAIN))
	tests_btn.pressed.connect(_on_tests)
	for stage_index in range(stage_buttons.size()):
		stage_buttons[stage_index].pressed.connect(_on_stage.bind(stage_index + 1))


func show_page(page: String) -> bool:
	if page not in [PAGE_MAIN, PAGE_SOLO, PAGE_OPTIONS, PAGE_DEVELOPER, PAGE_STATS]:
		return false
	_show_page(page)
	return true


func get_current_page() -> String:
	return current_page


func _show_page(page: String) -> void:
	current_page = page
	# The main bar stays visible under the compact NETWORK flyout, matching the
	# retail shell where flyouts open above the persistent bottom bar (REF-01).
	_set_nodes_visible(_main_page_nodes(), page == PAGE_MAIN or page == PAGE_MULTIPLAYER or page == PAGE_MP_LOBBY)
	_set_nodes_visible(_solo_page_nodes(), page == PAGE_SOLO)
	_set_nodes_visible(_multiplayer_page_nodes(), page == PAGE_MULTIPLAYER)
	_set_nodes_visible(_mp_lobby_page_nodes(), page == PAGE_MP_LOBBY)
	_set_nodes_visible(_options_page_nodes(), page == PAGE_OPTIONS)
	_set_nodes_visible(_developer_page_nodes(), page == PAGE_DEVELOPER)
	_set_nodes_visible(_stats_page_nodes(), page == PAGE_STATS)
	menu_frame.visible = page != PAGE_DEVELOPER
	# Subpage nav is a thin alternate strip — keep it off when the main bottom
	# bar is the intended chrome so both bars never stack and fight input.
	subpage_nav.visible = false
	if _nav_diamonds != null:
		_nav_diamonds.queue_redraw()
	# Developer tools remain deliberately absent from the player-facing surface;
	# F10 and show_page("developer") preserve access for proof work.
	developer_access_btn.visible = false
	match page:
		PAGE_MAIN:
			if solo_btn.visible:
				solo_btn.grab_focus()
		PAGE_SOLO:
			_refresh_skirmish_launch_state()
			if solo_flyout.player_army_opt.visible:
				solo_flyout.player_army_opt.grab_focus()
		PAGE_MULTIPLAYER:
			if multiplayer_flyout.host_button != null and multiplayer_flyout.host_button.visible:
				multiplayer_flyout.host_button.grab_focus()
		PAGE_MP_LOBBY:
			if multiplayer_lobby.leave_button != null and multiplayer_lobby.leave_button.visible:
				multiplayer_lobby.leave_button.grab_focus()
		PAGE_OPTIONS:
			if options_screen.visible and options_screen.window_mode_opt != null:
				options_screen.window_mode_opt.grab_focus()
		PAGE_DEVELOPER:
			if not stage_buttons.is_empty() and stage_buttons[0].visible:
				stage_buttons[0].grab_focus()
		PAGE_STATS:
			if stats_screen.back_btn.visible:
				stats_screen.back_btn.grab_focus()


func _main_page_nodes() -> Array[Control]:
	return [main_heading, solo_btn, multiplayer_btn, options_btn, quit_btn]


func _solo_page_nodes() -> Array[Control]:
	return [solo_flyout]


func _multiplayer_page_nodes() -> Array[Control]:
	return [multiplayer_flyout]


func _mp_lobby_page_nodes() -> Array[Control]:
	return [multiplayer_lobby]


func _options_page_nodes() -> Array[Control]:
	return [options_screen]


func _stats_page_nodes() -> Array[Control]:
	return [stats_screen]


func _developer_page_nodes() -> Array[Control]:
	var nodes: Array[Control] = [developer_frame, developer_heading]
	nodes.append_array(stage_buttons)
	nodes.append(tests_btn)
	nodes.append(developer_back_btn)
	nodes.append(status)
	return nodes


func _set_nodes_visible(nodes: Array[Control], visible_value: bool) -> void:
	for node in nodes:
		if node == null:
			continue
		node.visible = visible_value
		# Hidden main-bar controls must not keep intercepting clicks under the
		# solo flyout (that felt like the menu "spazzing" when Solo was open).
		# Do not touch BaseButton.disabled — launch/faction gates own that bit.
		# Containers use PASS so children (OptionButtons in grids) still receive
		# input when the page is shown; IGNORE only when the page is hidden.
		if visible_value:
			if node is BaseButton or node is Range:
				node.mouse_filter = Control.MOUSE_FILTER_STOP
			elif node is Container or node is Panel or node is PanelContainer:
				node.mouse_filter = Control.MOUSE_FILTER_PASS
			else:
				# Labels / pure chrome: do not steal clicks from buttons below.
				node.mouse_filter = Control.MOUSE_FILTER_IGNORE
		else:
			node.mouse_filter = Control.MOUSE_FILTER_IGNORE


func _on_options() -> void:
	options_screen.open()
	_show_page(PAGE_OPTIONS)


## Validates and records a NETWORK selection on GameState without any scene
## change (the runner exercises this seam directly). "" mode is rejected.
func apply_multiplayer_selection(mode: String, address: String, port: int) -> bool:
	if mode != "host" and mode != "join":
		return false
	if port < multiplayer_flyout.PORT_MIN or port > multiplayer_flyout.PORT_MAX:
		return false
	if mode == "join" and not address.strip_edges().is_valid_ip_address():
		return false
	var host_error := retail_launch_error()
	if host_error != "":
		multiplayer_flyout.set_status("Cannot start: %s" % host_error, true)
		return false
	# The tier-1 network scenario is the proven lockstep path: authored slice
	# defaults (Men vs Men on Fords), no rules overrides, host=team 0.
	_game_state.set("retail_player_faction", "men")
	_game_state.set("retail_enemy_faction", "men")
	_game_state.set("retail_map_id", SliceScript.MAP_ID)
	_game_state.set("retail_initial_resources", -1)
	_game_state.set("retail_command_point_factor", 1.0)
	_game_state.set("retail_build_plots_only", false)
	_game_state.set("retail_player_start_index", 0)
	_game_state.set("retail_mp_mode", mode)
	_game_state.set("retail_mp_address", address.strip_edges() if mode == "join" else "127.0.0.1")
	_game_state.set("retail_mp_port", port)
	return true


func _on_multiplayer_host(port: int) -> void:
	_launch_multiplayer("host", "127.0.0.1", port)


func _on_multiplayer_join(address: String, port: int) -> void:
	_launch_multiplayer("join", address, port)


func _launch_multiplayer(mode: String, address: String, port: int) -> void:
	if _launch_in_progress:
		return
	# The selection seam stays: it validates fail-closed and records the
	# transport fields (mode/address/port) the slice will read. Its tier-1
	# men/men faction writes are provisional — the GAME LOBBY overwrites the
	# whole selection (retail_team_setup and friends) at launch.
	if not apply_multiplayer_selection(mode, address, port):
		return
	var session = LockstepSessionScript.new()
	var session_error: Error = session.host(port) if mode == "host" else session.join(address, port)
	if session_error != OK:
		multiplayer_flyout.set_status(
			"Could not %s on %s:%d (error %d)." % ["host" if mode == "host" else "join", address, port, session_error],
			true
		)
		return
	_lobby_session = session
	multiplayer_flyout.set_busy(true)
	multiplayer_lobby.open(session, mode == "host", String(_game_state.get("retail_mp_player_name")))
	_show_page(PAGE_MP_LOBBY)


func _on_lobby_launch_confirmed() -> void:
	# Both peers verified the byte-identical roster and wrote GameState (incl.
	# retail_team_setup) inside the lobby; the menu only owns the scene change.
	if _launch_in_progress:
		return
	_launch_in_progress = true
	multiplayer_lobby.close_lobby()
	if _lobby_session != null:
		# Graceful, frame-async drain: a hard close here provably drops the
		# host's still-un-acked lobby.launch echo and strands the guest. The
		# bounded drain never hangs the launch; UDP has no TIME_WAIT, so once
		# closed the slice's lockstep session re-binds the same port at boot.
		var session = _lobby_session
		session.begin_graceful_close()
		for _frame in range(30):
			if session.poll_graceful_close():
				break
			await get_tree().process_frame
		session.close()
	_lobby_session = null
	get_tree().change_scene_to_file("res://scenes/retail_loading_boot.tscn")


func _on_lobby_leave() -> void:
	# The lobby already closed the session (notified disconnect) before
	# emitting leave_requested.
	_lobby_session = null
	multiplayer_flyout.set_busy(false)
	_show_page(PAGE_MULTIPLAYER)


func _on_retail() -> void:
	# Rapid re-presses must not double-fire: the first change frees this menu
	# and a second then errors on the freed tree.
	if _launch_in_progress:
		return
	if apply_skirmish_selection():
		_launch_in_progress = true
		# Route through the loading-boot scene: the retail loading screen shows
		# immediately, fetches the slice scene on a thread, and the slice then
		# adopts the same screen for its real per-phase progress.
		get_tree().change_scene_to_file("res://scenes/retail_loading_boot.tscn")


func _on_stage(stage_index: int) -> void:
	var suffix := "arena" if stage_index <= 2 else "lab"
	get_tree().change_scene_to_file("res://scenes/stage%d_%s.tscn" % [stage_index, suffix])


func _on_tests() -> void:
	var runner := load("res://tests/run_stage_tests.gd")
	if runner == null:
		status.text = "Self-test runner is unavailable."
		return
	var instance = runner.new()
	var report: String = instance.run_all()
	status.text = report
	print(report)


func _unhandled_key_input(event: InputEvent) -> void:
	if not event.pressed or event.echo:
		return
	if event.keycode == KEY_ESCAPE and current_page == PAGE_MP_LOBBY:
		# Escaping the lobby is a LEAVE, never a silent page swap: the session
		# must close (notified disconnect) or the peer would wait forever.
		multiplayer_lobby._on_leave_pressed()
		get_viewport().set_input_as_handled()
	elif event.keycode == KEY_ESCAPE and current_page != PAGE_MAIN:
		_show_page(PAGE_MAIN)
		get_viewport().set_input_as_handled()
	elif event.keycode == KEY_F10:
		_show_page(PAGE_MAIN if current_page == PAGE_DEVELOPER else PAGE_DEVELOPER)
		get_viewport().set_input_as_handled()
