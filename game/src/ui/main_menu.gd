extends Control
## Player-facing menu shell. Proof-stage breadth remains available, but it no
## longer competes with the vertical slice on the main page.

const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const UserSettingsScript = preload("res://src/ui/user_settings.gd")

const PAGE_MAIN := "main"
const PAGE_SOLO := "solo"
const PAGE_OPTIONS := "options"
const PAGE_DEVELOPER := "developer"

@onready var center: Control = $Center
@onready var menu_frame: Panel = $MenuFrame
@onready var main_heading: Label = $Center/MainHeading
@onready var solo_btn: Button = $Center/Solo
@onready var options_btn: Button = $Center/Options
@onready var quit_btn: Button = $Center/Quit
@onready var subpage_nav: Control = $Center/SubpageNav
@onready var sub_solo_btn: Button = $Center/SubpageNav/Solo
@onready var sub_options_btn: Button = $Center/SubpageNav/Options
@onready var sub_quit_btn: Button = $Center/SubpageNav/Quit

@onready var solo_flyout: Panel = $Center/SoloFlyout
@onready var solo_heading: Label = $Center/SoloHeading
@onready var retail_btn: Button = $Center/Retail
@onready var retail_hint: Label = $Center/RetailHint
@onready var legacy_grid: GridContainer = $Center/LegacyGrid
@onready var player_opt: OptionButton = $Center/LegacyGrid/Player
@onready var enemy_opt: OptionButton = $Center/LegacyGrid/Enemy
@onready var map_opt: OptionButton = $Center/LegacyGrid/Map
@onready var diff_opt: OptionButton = $Center/LegacyGrid/Difficulty
@onready var start_btn: Button = $Center/Start
@onready var solo_back_btn: Button = $Center/SoloBack

@onready var options_frame: Panel = $Center/OptionsFrame
@onready var options_heading: Label = $Center/OptionsHeading
@onready var music_row: HBoxContainer = $Center/MusicRow
@onready var music_slider: HSlider = $Center/MusicRow/Slider
@onready var music_percent: Label = $Center/MusicRow/Percent
@onready var voice_row: HBoxContainer = $Center/VoiceRow
@onready var voice_slider: HSlider = $Center/VoiceRow/Slider
@onready var voice_percent: Label = $Center/VoiceRow/Percent
@onready var mute_toggle: CheckButton = $Center/Mute
@onready var audio_hint: Label = $Center/AudioHint
@onready var reset_audio_btn: Button = $Center/ResetAudio
@onready var options_back_btn: Button = $Center/OptionsBack

@onready var developer_frame: Panel = $Center/DeveloperFrame
@onready var developer_heading: Label = $Center/DeveloperHeading
@onready var tests_btn: Button = $Center/Tests
@onready var developer_back_btn: Button = $Center/DeveloperBack
@onready var developer_access_btn: Button = $DeveloperAccess
@onready var status: Label = $Center/Status

var current_page := PAGE_MAIN
var stage_buttons: Array[Button] = []
var _loading_settings := false
var _content_db: Node
var _game_state: Node


func _ready() -> void:
	_content_db = get_node_or_null("/root/ContentDB")
	_game_state = get_node_or_null("/root/GameState")
	if _content_db == null or _game_state == null:
		push_error("OpenBFME menu requires the ContentDB and GameState autoloads.")
		return
	theme = ThemeScript.create_theme()
	_populate_legacy_options()
	_collect_stage_buttons()
	_connect_actions()
	_load_audio_controls()
	_show_page(PAGE_MAIN)
	status.text = "Content: %d units, %d buildings, %d factions, %d maps, %d powers" % [
		(_content_db.get("units") as Dictionary).size(), (_content_db.get("buildings") as Dictionary).size(),
		(_content_db.get("factions") as Dictionary).size(), (_content_db.get("maps") as Dictionary).size(),
		(_content_db.get("powers") as Dictionary).size()
	]


func _populate_legacy_options() -> void:
	player_opt.clear()
	enemy_opt.clear()
	map_opt.clear()
	diff_opt.clear()
	for faction_id in ["gondor", "mordor", "elves", "goblins"]:
		var faction: Dictionary = _content_db.call("get_faction", faction_id) as Dictionary
		var display_name := String(faction.get("name", faction_id.capitalize()))
		player_opt.add_item(display_name)
		player_opt.set_item_metadata(player_opt.item_count - 1, faction_id)
		enemy_opt.add_item(display_name)
		enemy_opt.set_item_metadata(enemy_opt.item_count - 1, faction_id)
	if player_opt.item_count > 0:
		player_opt.select(0)
	if enemy_opt.item_count > 1:
		enemy_opt.select(1)
	var map_ids: Array = (_content_db.get("maps") as Dictionary).keys()
	map_ids.sort()
	for map_id_variant in map_ids:
		var map_id := String(map_id_variant)
		var map_definition: Dictionary = _content_db.call("get_map", map_id) as Dictionary
		map_opt.add_item(String(map_definition.get("name", map_id)))
		map_opt.set_item_metadata(map_opt.item_count - 1, map_id)
	for difficulty_id in ["easy", "normal", "hard"]:
		diff_opt.add_item(difficulty_id.capitalize())
		diff_opt.set_item_metadata(diff_opt.item_count - 1, difficulty_id)
	if diff_opt.item_count > 1:
		diff_opt.select(1)


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
	options_btn.pressed.connect(_on_options)
	quit_btn.pressed.connect(func() -> void: get_tree().quit())
	sub_solo_btn.pressed.connect(func() -> void: _show_page(PAGE_SOLO))
	sub_options_btn.pressed.connect(_on_options)
	sub_quit_btn.pressed.connect(func() -> void: get_tree().quit())
	retail_btn.pressed.connect(_on_retail)
	start_btn.pressed.connect(_on_start)
	solo_back_btn.pressed.connect(func() -> void: _show_page(PAGE_MAIN))
	options_back_btn.pressed.connect(func() -> void: _show_page(PAGE_MAIN))
	reset_audio_btn.pressed.connect(_on_reset_audio)
	developer_access_btn.pressed.connect(func() -> void: _show_page(PAGE_DEVELOPER))
	developer_back_btn.pressed.connect(func() -> void: _show_page(PAGE_MAIN))
	tests_btn.pressed.connect(_on_tests)
	for stage_index in range(stage_buttons.size()):
		stage_buttons[stage_index].pressed.connect(_on_stage.bind(stage_index + 1))
	music_slider.value_changed.connect(_on_music_volume_changed)
	voice_slider.value_changed.connect(_on_voice_volume_changed)
	mute_toggle.toggled.connect(_on_mute_toggled)


func show_page(page: String) -> bool:
	if page not in [PAGE_MAIN, PAGE_SOLO, PAGE_OPTIONS, PAGE_DEVELOPER]:
		return false
	_show_page(page)
	return true


func get_current_page() -> String:
	return current_page


func _show_page(page: String) -> void:
	current_page = page
	_set_nodes_visible(_main_page_nodes(), page == PAGE_MAIN)
	_set_nodes_visible(_solo_page_nodes(), page == PAGE_SOLO)
	_set_nodes_visible(_options_page_nodes(), page == PAGE_OPTIONS)
	_set_nodes_visible(_developer_page_nodes(), page == PAGE_DEVELOPER)
	menu_frame.visible = page != PAGE_DEVELOPER
	subpage_nav.visible = page == PAGE_SOLO or page == PAGE_OPTIONS
	# Developer tools remain deliberately absent from the player-facing surface;
	# F10 and show_page("developer") preserve access for proof work.
	developer_access_btn.visible = false
	match page:
		PAGE_MAIN:
			solo_btn.grab_focus()
		PAGE_SOLO:
			retail_btn.grab_focus()
		PAGE_OPTIONS:
			music_slider.grab_focus()
		PAGE_DEVELOPER:
			stage_buttons[0].grab_focus()


func _main_page_nodes() -> Array[Control]:
	return [main_heading, solo_btn, options_btn, quit_btn]


func _solo_page_nodes() -> Array[Control]:
	return [solo_flyout, solo_heading, retail_btn, retail_hint, legacy_grid, start_btn, solo_back_btn]


func _options_page_nodes() -> Array[Control]:
	return [options_frame, options_heading, music_row, voice_row, mute_toggle, audio_hint, reset_audio_btn, options_back_btn]


func _developer_page_nodes() -> Array[Control]:
	var nodes: Array[Control] = [developer_frame, developer_heading]
	nodes.append_array(stage_buttons)
	nodes.append(tests_btn)
	nodes.append(developer_back_btn)
	nodes.append(status)
	return nodes


func _set_nodes_visible(nodes: Array[Control], visible_value: bool) -> void:
	for node in nodes:
		node.visible = visible_value


func _on_options() -> void:
	_load_audio_controls()
	_show_page(PAGE_OPTIONS)


func _load_audio_controls() -> void:
	_loading_settings = true
	var settings: Dictionary = UserSettingsScript.load_audio()
	music_slider.value = float(settings["music_volume"])
	voice_slider.value = float(settings["voice_sfx_volume"])
	mute_toggle.button_pressed = bool(settings["muted"])
	_loading_settings = false
	_update_audio_labels()
	audio_hint.text = "Changes are saved automatically."


func _on_music_volume_changed(_value: float) -> void:
	_update_audio_labels()
	_persist_audio_controls()


func _on_voice_volume_changed(_value: float) -> void:
	_update_audio_labels()
	_persist_audio_controls()


func _on_mute_toggled(_enabled: bool) -> void:
	_persist_audio_controls()


func _update_audio_labels() -> void:
	music_percent.text = "%d%%" % roundi(music_slider.value * 100.0)
	voice_percent.text = "%d%%" % roundi(voice_slider.value * 100.0)


func _persist_audio_controls() -> void:
	if _loading_settings:
		return
	var error: Error = UserSettingsScript.save_audio(music_slider.value, voice_slider.value, mute_toggle.button_pressed)
	if error != OK:
		audio_hint.text = "Settings could not be saved: %s" % error_string(error)
	else:
		audio_hint.text = "Changes saved automatically."


func _on_reset_audio() -> void:
	var error: Error = UserSettingsScript.reset_audio()
	if error != OK:
		audio_hint.text = "Defaults could not be saved: %s" % error_string(error)
		return
	_load_audio_controls()
	audio_hint.text = "Audio defaults restored."


func _on_start() -> void:
	if player_opt.selected < 0 or enemy_opt.selected < 0 or map_opt.selected < 0 or diff_opt.selected < 0:
		retail_hint.text = "Legacy prototype content is not available in this pack."
		return
	_game_state.set("player_faction", String(player_opt.get_item_metadata(player_opt.selected)))
	_game_state.set("enemy_faction", String(enemy_opt.get_item_metadata(enemy_opt.selected)))
	_game_state.set("map_id", String(map_opt.get_item_metadata(map_opt.selected)))
	_game_state.set("difficulty", String(diff_opt.get_item_metadata(diff_opt.selected)))
	get_tree().change_scene_to_file("res://scenes/match.tscn")


func _on_retail() -> void:
	get_tree().change_scene_to_file("res://scenes/retail_vertical_slice.tscn")


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
	if event.keycode == KEY_ESCAPE and current_page != PAGE_MAIN:
		_show_page(PAGE_MAIN)
		get_viewport().set_input_as_handled()
	elif event.keycode == KEY_F10:
		_show_page(PAGE_MAIN if current_page == PAGE_DEVELOPER else PAGE_DEVELOPER)
		get_viewport().set_input_as_handled()
