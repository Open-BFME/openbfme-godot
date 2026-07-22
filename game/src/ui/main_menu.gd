extends Control
## Player-facing menu shell. Proof-stage breadth remains available, but it no
## longer competes with the vertical slice on the main page.

const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const NavDiamondsScript = preload("res://src/ui/openbfme_nav_diamonds.gd")
const SliceScript = preload("res://src/retail_slice/retail_vertical_slice.gd")
const FactionManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

const PAGE_MAIN := "main"
const PAGE_SOLO := "solo"
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
	var army_options: Array[OptionButton] = [solo_flyout.player_army_opt, solo_flyout.enemy_army_opt]
	for option in army_options:
		option.clear()
	for faction in RETAIL_FACTIONS:
		var faction_id := String(faction["id"])
		var note := _retail_faction_availability(faction_id)
		_skirmish_availability[faction_id] = note
		var label := String(faction["name"]) + (NOT_CONVERTED_SUFFIX if note != "" else "")
		for option in army_options:
			option.add_item(label)
			var index := option.item_count - 1
			option.set_item_metadata(index, faction_id)
			option.set_item_disabled(index, note != "")
			if note != "":
				option.set_item_tooltip(index, "Not converted: %s" % note)
	_select_first_enabled(solo_flyout.player_army_opt)
	_select_first_enabled(solo_flyout.enemy_army_opt)
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
	_select_option_by_metadata_value(solo_flyout.initial_resources_opt, RULES_DEFAULT_RESOURCES)
	_select_option_by_metadata_value(solo_flyout.cp_factor_opt, RULES_DEFAULT_FACTOR)


func _populate_color_options() -> void:
	for row in range(solo_flyout.color_dropdowns.size()):
		var option: OptionButton = solo_flyout.color_dropdowns[row]
		option.clear()
		for entry in HOUSE_COLORS:
			option.add_item(String(entry["name"]))
			option.set_item_metadata(option.item_count - 1, entry["color"])
	solo_flyout.color_dropdowns[0].select(0)
	solo_flyout.color_dropdowns[1].select(1)
	_on_color_changed(0)
	_on_color_changed(1)


func _on_color_changed(row: int) -> void:
	var option: OptionButton = solo_flyout.color_dropdowns[row]
	if option.selected < 0:
		return
	var color: Color = option.get_item_metadata(option.selected)
	solo_flyout.color_swatches[row].color = color
	_game_state.set("retail_player_color" if row == 0 else "retail_enemy_color", color)


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
	_refresh_map_preview()
	_refresh_map_description()
	_refresh_start_row()
	_refresh_skirmish_launch_state()


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
	## is blocked. A blocked launch never falls back silently.
	var host_error := _men_pack_gate_error()
	if host_error != "":
		return "The retail slice host pack is unavailable: %s." % host_error
	var player_id := _selected_skirmish_faction(solo_flyout.player_army_opt)
	var enemy_id := _selected_skirmish_faction(solo_flyout.enemy_army_opt)
	if player_id == "" or enemy_id == "":
		return "No converted faction is selectable yet. Convert a faction pack first."
	for side in [["Player", player_id], ["Enemy", enemy_id]]:
		var note := String(_skirmish_availability.get(String(side[1]), "not converted"))
		if note != "":
			return "%s faction %s is not converted yet: %s." % [String(side[0]), _retail_faction_display_name(String(side[1])), note]
	if player_id != enemy_id:
		return "Cross-faction matches are not seeded by the slice yet; pick the same faction for both sides."
	var map_id := _selected_skirmish_map()
	if map_id == "":
		return "No retail map is selectable. Ensure bfme2-five-maps is selected."
	return ""


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
	_game_state.set("retail_player_faction", _selected_skirmish_faction(solo_flyout.player_army_opt))
	_game_state.set("retail_enemy_faction", _selected_skirmish_faction(solo_flyout.enemy_army_opt))
	var map_id := _selected_skirmish_map()
	_game_state.set("retail_map_id", map_id if map_id != "" else SliceScript.MAP_ID)
	_game_state.set("retail_initial_resources", _selected_rules_resources())
	_game_state.set("retail_command_point_factor", _selected_rules_factor())
	return true


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


func _on_rules_changed(_index: int = 0) -> void:
	_game_state.set("retail_initial_resources", _selected_rules_resources())
	_game_state.set("retail_command_point_factor", _selected_rules_factor())


func _on_rules_reset() -> void:
	_select_option_by_metadata_value(solo_flyout.initial_resources_opt, RULES_DEFAULT_RESOURCES)
	_select_option_by_metadata_value(solo_flyout.cp_factor_opt, RULES_DEFAULT_FACTOR)
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
	options_btn.pressed.connect(_on_options)
	quit_btn.pressed.connect(func() -> void: get_tree().quit())
	sub_solo_btn.pressed.connect(func() -> void: _show_page(PAGE_SOLO))
	sub_options_btn.pressed.connect(_on_options)
	sub_quit_btn.pressed.connect(func() -> void: get_tree().quit())
	solo_flyout.army_changed.connect(_refresh_skirmish_launch_state)
	solo_flyout.color_changed.connect(_on_color_changed)
	solo_flyout.play_pressed.connect(_on_retail)
	solo_flyout.main_menu_pressed.connect(func() -> void: _show_page(PAGE_MAIN))
	solo_flyout.stats_pressed.connect(func() -> void: _show_page(PAGE_STATS))
	solo_flyout.rules_reset_btn.pressed.connect(_on_rules_reset)
	solo_flyout.initial_resources_opt.item_selected.connect(_on_rules_changed)
	solo_flyout.cp_factor_opt.item_selected.connect(_on_rules_changed)
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
	_set_nodes_visible(_main_page_nodes(), page == PAGE_MAIN)
	_set_nodes_visible(_solo_page_nodes(), page == PAGE_SOLO)
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
	return [main_heading, solo_btn, options_btn, quit_btn]


func _solo_page_nodes() -> Array[Control]:
	return [solo_flyout]


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
	if event.keycode == KEY_ESCAPE and current_page != PAGE_MAIN:
		_show_page(PAGE_MAIN)
		get_viewport().set_input_as_handled()
	elif event.keycode == KEY_F10:
		_show_page(PAGE_MAIN if current_page == PAGE_DEVELOPER else PAGE_DEVELOPER)
		get_viewport().set_input_as_handled()
