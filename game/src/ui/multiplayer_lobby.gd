extends Panel

## MULTIPLAYER game lobby, entered after the NETWORK flyout's host/join
## connects. Modeled on the retail GAME SETUP screen (reference REF-09/REF-10:
## per-player rows with Army/Team/Color columns; REF-13: Online settings
## conventions) in the same green shell language as the rest of the menu.
##
## The panel is a thin view over RetailLockstepSession's pre-game lobby
## protocol: profiles (name/faction/color/ready) are per-peer, chat is
## symmetric, match settings are HOST-authoritative (read-only labels on the
## guest), and launch is a host-only roster echo both sides verify byte-equal
## before accepting. On an accepted launch BOTH peers write the identical
## GameState selection (retail_team_setup and friends) and `launch_confirmed`
## fires — the menu owns the actual scene change.

signal launch_confirmed
signal leave_requested

const SessionScript = preload("res://src/retail_slice/retail_lockstep_session.gd")

const FACTION_NAMES: Array[String] = ["Men", "Elves", "Dwarves", "Isengard", "Mordor", "Goblins"]
const MAP_NAMES: Array[String] = ["Fords of Isen II", "Rivendell", "Mount Doom", "Dagorlad", "Mordor"]
const DEFAULT_HOST_NAME := "Player"
const DEFAULT_GUEST_NAME := "Challenger"

var session
var is_host := false

var heading_label: Label
var name_edit: LineEdit
var army_opt: OptionButton
var color_opt: OptionButton
var local_team_label: Label
var local_ready_check: CheckBox
var remote_name_label: Label
var remote_army_label: Label
var remote_team_label: Label
var remote_color_label: Label
var remote_ready_check: CheckBox
var map_opt: OptionButton
var resources_opt: OptionButton
var cp_opt: OptionButton
var build_mode_opt: OptionButton
var map_value_label: Label
var resources_value_label: Label
var cp_value_label: Label
var build_mode_value_label: Label
var chat_log_label: RichTextLabel
var chat_edit: LineEdit
var chat_send_button: Button
var status_label: Label
var ready_button: Button
var launch_button: Button
var leave_button: Button

var _launched := false
var _was_connected := false
var _profile_dirty := false


func _ready() -> void:
	_build()
	set_process(false)


## Binds a connected (or connecting) session and shows the lobby. host_flag
## must match the session's own side; local_default_name seeds the editable
## name field ("" keeps Player/Challenger).
func open(active_session, host_flag: bool, local_default_name: String = "") -> void:
	session = active_session
	is_host = host_flag
	_launched = false
	_was_connected = false
	_profile_dirty = false
	heading_label.text = "GAME LOBBY - HOSTING" if is_host else "GAME LOBBY - JOINED"
	var default_name := local_default_name.strip_edges()
	if default_name == "" or not SessionScript.lobby_name_valid(default_name):
		default_name = DEFAULT_HOST_NAME if is_host else DEFAULT_GUEST_NAME
	name_edit.text = default_name
	local_team_label.text = "1" if is_host else "2"
	remote_team_label.text = "2" if is_host else "1"
	local_ready_check.button_pressed = false
	remote_ready_check.button_pressed = false
	chat_log_label.text = ""
	_set_settings_editable(is_host)
	_apply_settings_to_controls(SessionScript.lobby_default_settings())
	if session != null:
		session.lobby_updated.connect(_on_lobby_updated)
		session.lobby_chat_received.connect(_on_lobby_chat)
		session.lobby_launch_accepted.connect(_on_lobby_launch_accepted)
	set_status("Waiting for the challenger to connect..." if is_host else "Connecting to the host...")
	_refresh_remote_row()
	_refresh_buttons()
	visible = true
	set_process(true)


func close_lobby() -> void:
	set_process(false)
	if session != null:
		if session.lobby_updated.is_connected(_on_lobby_updated):
			session.lobby_updated.disconnect(_on_lobby_updated)
		if session.lobby_chat_received.is_connected(_on_lobby_chat):
			session.lobby_chat_received.disconnect(_on_lobby_chat)
		if session.lobby_launch_accepted.is_connected(_on_lobby_launch_accepted):
			session.lobby_launch_accepted.disconnect(_on_lobby_launch_accepted)
	session = null
	visible = false


func _process(_delta: float) -> void:
	if session == null:
		return
	session.poll()
	var connected := bool(session.connected) and bool(session.handshake_complete)
	if connected and not _was_connected:
		_was_connected = true
		set_status("Challenger connected. Pick your army and ready up." if is_host else "Connected. Pick your army and ready up.")
		_announce_profile()
		if is_host:
			_send_settings()
	elif connected and _profile_dirty:
		_announce_profile()
	if _was_connected and not bool(session.connected) and not _launched:
		set_status("Connection lost. Leave the lobby to return to the menu.", true)
		_refresh_buttons()


func _build() -> void:
	heading_label = Label.new()
	heading_label.name = "Heading"
	heading_label.text = "GAME LOBBY"
	heading_label.position = Vector2(0, 14)
	heading_label.size = Vector2(size.x, 34)
	heading_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	heading_label.add_theme_font_size_override("font_size", 24)
	heading_label.add_theme_color_override("font_color", Color(0.82, 0.9, 0.78))
	add_child(heading_label)

	# --- player rows, REF-09 column layout ----------------------------------
	var columns := [
		["PLAYER", 40.0, 190.0],
		["ARMY", 240.0, 150.0],
		["TEAM", 400.0, 60.0],
		["COLOR", 470.0, 130.0],
		["READY", 610.0, 70.0],
	]
	for column_value in columns:
		var column := column_value as Array
		var header := Label.new()
		header.name = "Header%s" % String(column[0]).capitalize()
		header.text = String(column[0])
		header.position = Vector2(float(column[1]), 56)
		header.size = Vector2(float(column[2]), 22)
		header.add_theme_font_size_override("font_size", 13)
		header.add_theme_color_override("font_color", Color(0.85, 0.78, 0.5))
		add_child(header)

	# Local row (row 0 on screen; sim team depends on side).
	name_edit = LineEdit.new()
	name_edit.name = "LocalName"
	name_edit.text = DEFAULT_HOST_NAME
	name_edit.max_length = SessionScript.LOBBY_NAME_MAX
	name_edit.position = Vector2(40, 82)
	name_edit.size = Vector2(190, 34)
	name_edit.text_changed.connect(func(_text: String) -> void: _on_profile_edited())
	add_child(name_edit)
	army_opt = OptionButton.new()
	army_opt.name = "LocalArmy"
	for index in SessionScript.LOBBY_FACTION_IDS.size():
		army_opt.add_item(FACTION_NAMES[index])
		army_opt.set_item_metadata(index, SessionScript.LOBBY_FACTION_IDS[index])
	army_opt.select(0)
	army_opt.position = Vector2(240, 82)
	army_opt.size = Vector2(150, 34)
	army_opt.item_selected.connect(func(_index: int) -> void: _on_profile_edited())
	add_child(army_opt)
	local_team_label = _row_label("LocalTeam", "1", Vector2(400, 88), 60)
	add_child(local_team_label)
	color_opt = OptionButton.new()
	color_opt.name = "LocalColor"
	for index in SessionScript.LOBBY_COLOR_NAMES.size():
		color_opt.add_item(SessionScript.LOBBY_COLOR_NAMES[index])
		color_opt.set_item_metadata(index, index)
	color_opt.select(0)
	color_opt.position = Vector2(470, 82)
	color_opt.size = Vector2(130, 34)
	color_opt.item_selected.connect(func(_index: int) -> void: _on_profile_edited())
	add_child(color_opt)
	local_ready_check = CheckBox.new()
	local_ready_check.name = "LocalReady"
	local_ready_check.position = Vector2(614, 84)
	local_ready_check.toggled.connect(_on_ready_toggled)
	add_child(local_ready_check)

	# Remote row: read-only mirror of the peer's announced profile.
	remote_name_label = _row_label("RemoteName", "...", Vector2(40, 130), 190)
	add_child(remote_name_label)
	remote_army_label = _row_label("RemoteArmy", "-", Vector2(240, 130), 150)
	add_child(remote_army_label)
	remote_team_label = _row_label("RemoteTeam", "2", Vector2(400, 130), 60)
	add_child(remote_team_label)
	remote_color_label = _row_label("RemoteColor", "-", Vector2(470, 130), 130)
	add_child(remote_color_label)
	remote_ready_check = CheckBox.new()
	remote_ready_check.name = "RemoteReady"
	remote_ready_check.position = Vector2(614, 126)
	remote_ready_check.disabled = true
	add_child(remote_ready_check)

	# --- host settings block (REF-10 rules column) ---------------------------
	var settings_heading := Label.new()
	settings_heading.name = "SettingsHeading"
	settings_heading.text = "GAME SETTINGS"
	settings_heading.position = Vector2(40, 176)
	settings_heading.size = Vector2(300, 24)
	settings_heading.add_theme_font_size_override("font_size", 16)
	settings_heading.add_theme_color_override("font_color", Color(0.85, 0.78, 0.5))
	add_child(settings_heading)

	add_child(_field_label("MapLabel", "Map", Vector2(40, 210)))
	map_opt = OptionButton.new()
	map_opt.name = "MapOpt"
	for index in SessionScript.LOBBY_MAP_IDS.size():
		map_opt.add_item(MAP_NAMES[index])
		map_opt.set_item_metadata(index, SessionScript.LOBBY_MAP_IDS[index])
	map_opt.select(0)
	map_opt.position = Vector2(160, 204)
	map_opt.size = Vector2(210, 32)
	map_opt.item_selected.connect(func(_index: int) -> void: _send_settings())
	add_child(map_opt)
	map_value_label = _row_label("MapValue", MAP_NAMES[0], Vector2(160, 210), 210)
	add_child(map_value_label)

	add_child(_field_label("ResourcesLabel", "Resources", Vector2(40, 248)))
	resources_opt = OptionButton.new()
	resources_opt.name = "ResourcesOpt"
	for index in SessionScript.LOBBY_RESOURCE_VALUES.size():
		var amount: int = SessionScript.LOBBY_RESOURCE_VALUES[index]
		resources_opt.add_item(str(amount))
		resources_opt.set_item_metadata(index, amount)
	resources_opt.select(SessionScript.LOBBY_RESOURCE_VALUES.find(1200))
	resources_opt.position = Vector2(160, 242)
	resources_opt.size = Vector2(210, 32)
	resources_opt.item_selected.connect(func(_index: int) -> void: _send_settings())
	add_child(resources_opt)
	resources_value_label = _row_label("ResourcesValue", "1200", Vector2(160, 248), 210)
	add_child(resources_value_label)

	add_child(_field_label("CpLabel", "CP Factor", Vector2(40, 286)))
	cp_opt = OptionButton.new()
	cp_opt.name = "CpOpt"
	for index in SessionScript.LOBBY_CP_FACTORS.size():
		var factor: float = SessionScript.LOBBY_CP_FACTORS[index]
		cp_opt.add_item("%sX" % String.num(factor, 1))
		cp_opt.set_item_metadata(index, factor)
	cp_opt.select(SessionScript.LOBBY_CP_FACTORS.find(1.0))
	cp_opt.position = Vector2(160, 280)
	cp_opt.size = Vector2(210, 32)
	cp_opt.item_selected.connect(func(_index: int) -> void: _send_settings())
	add_child(cp_opt)
	cp_value_label = _row_label("CpValue", "1.0X", Vector2(160, 286), 210)
	add_child(cp_value_label)

	add_child(_field_label("BuildModeLabel", "Build Mode", Vector2(40, 324)))
	build_mode_opt = OptionButton.new()
	build_mode_opt.name = "BuildModeOpt"
	build_mode_opt.add_item("Freeform (BFME2)")
	build_mode_opt.set_item_metadata(0, false)
	build_mode_opt.add_item("Build Plots (BFME1)")
	build_mode_opt.set_item_metadata(1, true)
	build_mode_opt.select(0)
	build_mode_opt.position = Vector2(160, 318)
	build_mode_opt.size = Vector2(210, 32)
	build_mode_opt.item_selected.connect(func(_index: int) -> void: _send_settings())
	add_child(build_mode_opt)
	build_mode_value_label = _row_label("BuildModeValue", "Freeform (BFME2)", Vector2(160, 324), 210)
	add_child(build_mode_value_label)

	# --- chat ----------------------------------------------------------------
	var chat_heading := Label.new()
	chat_heading.name = "ChatHeading"
	chat_heading.text = "CHAT"
	chat_heading.position = Vector2(400, 176)
	chat_heading.size = Vector2(200, 24)
	chat_heading.add_theme_font_size_override("font_size", 16)
	chat_heading.add_theme_color_override("font_color", Color(0.85, 0.78, 0.5))
	add_child(chat_heading)
	chat_log_label = RichTextLabel.new()
	chat_log_label.name = "ChatLog"
	chat_log_label.position = Vector2(400, 204)
	chat_log_label.size = Vector2(size.x - 440, 112)
	chat_log_label.scroll_following = true
	chat_log_label.add_theme_font_size_override("normal_font_size", 13)
	add_child(chat_log_label)
	chat_edit = LineEdit.new()
	chat_edit.name = "ChatEdit"
	chat_edit.max_length = SessionScript.LOBBY_CHAT_MAX
	chat_edit.placeholder_text = "Say something..."
	chat_edit.position = Vector2(400, 322)
	chat_edit.size = Vector2(size.x - 530, 32)
	chat_edit.text_submitted.connect(func(_text: String) -> void: _on_chat_send())
	add_child(chat_edit)
	chat_send_button = Button.new()
	chat_send_button.name = "ChatSend"
	chat_send_button.text = "SEND"
	chat_send_button.position = Vector2(size.x - 120, 320)
	chat_send_button.size = Vector2(80, 34)
	chat_send_button.pressed.connect(_on_chat_send)
	add_child(chat_send_button)

	# --- status + action row -------------------------------------------------
	status_label = Label.new()
	status_label.name = "Status"
	status_label.text = ""
	status_label.position = Vector2(40, size.y - 118)
	status_label.size = Vector2(size.x - 80, 40)
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.add_theme_font_size_override("font_size", 13)
	status_label.add_theme_color_override("font_color", Color(0.66, 0.76, 0.7, 0.85))
	add_child(status_label)

	ready_button = Button.new()
	ready_button.name = "ReadyButton"
	ready_button.text = "READY"
	ready_button.position = Vector2(40, size.y - 72)
	ready_button.size = Vector2(180, 44)
	ready_button.theme_type_variation = "NavButton"
	ready_button.pressed.connect(func() -> void: local_ready_check.button_pressed = not local_ready_check.button_pressed)
	add_child(ready_button)

	launch_button = Button.new()
	launch_button.name = "LaunchButton"
	launch_button.text = "LAUNCH GAME"
	launch_button.position = Vector2(size.x / 2.0 - 110, size.y - 72)
	launch_button.size = Vector2(220, 44)
	launch_button.theme_type_variation = "NavButton"
	launch_button.disabled = true
	launch_button.pressed.connect(_on_launch_pressed)
	add_child(launch_button)

	leave_button = Button.new()
	leave_button.name = "LeaveButton"
	leave_button.text = "LEAVE"
	leave_button.position = Vector2(size.x - 220, size.y - 72)
	leave_button.size = Vector2(180, 44)
	leave_button.theme_type_variation = "NavButton"
	leave_button.pressed.connect(_on_leave_pressed)
	add_child(leave_button)


func _row_label(node_name: String, text: String, at: Vector2, width: float) -> Label:
	var label := Label.new()
	label.name = node_name
	label.text = text
	label.position = at
	label.size = Vector2(width, 24)
	label.add_theme_font_size_override("font_size", 14)
	return label


func _field_label(node_name: String, text: String, at: Vector2) -> Label:
	var label := Label.new()
	label.name = node_name
	label.text = text
	label.position = at
	label.size = Vector2(110, 26)
	label.add_theme_font_size_override("font_size", 14)
	return label


func set_status(message: String, is_error: bool = false) -> void:
	status_label.text = message
	status_label.add_theme_color_override(
		"font_color",
		Color(0.9, 0.45, 0.4) if is_error else Color(0.66, 0.76, 0.7, 0.85)
	)


func _set_settings_editable(editable: bool) -> void:
	map_opt.visible = editable
	resources_opt.visible = editable
	cp_opt.visible = editable
	build_mode_opt.visible = editable
	map_value_label.visible = not editable
	resources_value_label.visible = not editable
	cp_value_label.visible = not editable
	build_mode_value_label.visible = not editable


func local_profile_fields() -> Dictionary:
	var player_name := name_edit.text.strip_edges()
	if not SessionScript.lobby_name_valid(player_name):
		player_name = DEFAULT_HOST_NAME if is_host else DEFAULT_GUEST_NAME
	return {
		"name": player_name,
		"faction": String(army_opt.get_item_metadata(maxi(0, army_opt.selected))),
		"color": int(color_opt.get_item_metadata(maxi(0, color_opt.selected))),
		"ready": local_ready_check.button_pressed,
	}


func _announce_profile() -> void:
	if session == null:
		return
	var fields := local_profile_fields()
	if session.send_lobby_profile(
		String(fields["name"]), String(fields["faction"]), int(fields["color"]), bool(fields["ready"])
	):
		_profile_dirty = false
	_refresh_buttons()


func _on_profile_edited() -> void:
	# Any identity change drops the local ready flag (retail convention) and
	# re-announces on the next poll.
	if local_ready_check.button_pressed:
		local_ready_check.button_pressed = false
		return  # toggled() re-enters here via _on_ready_toggled -> announce
	_profile_dirty = true
	_refresh_buttons()


func _on_ready_toggled(_pressed: bool) -> void:
	_profile_dirty = true
	_refresh_buttons()


func _on_chat_send() -> void:
	if session == null:
		return
	var text := chat_edit.text.strip_edges()
	if text == "":
		return
	if session.send_lobby_chat(text):
		_append_chat_line(local_profile_fields()["name"], text)
		chat_edit.text = ""
	else:
		set_status("Message not sent: chat is limited to %d printable characters." % SessionScript.LOBBY_CHAT_MAX, true)


func _on_lobby_chat(_team: int, text: String) -> void:
	var peer_name := String(session.lobby_remote_profile.get("name", DEFAULT_GUEST_NAME if is_host else DEFAULT_HOST_NAME)) if session != null else "?"
	_append_chat_line(peer_name, text)


func _append_chat_line(author: String, text: String) -> void:
	if chat_log_label.text != "":
		chat_log_label.text += "\n"
	chat_log_label.text += "%s: %s" % [author, text]


func _on_lobby_updated() -> void:
	_refresh_remote_row()
	if not is_host and session != null and not session.lobby_settings.is_empty():
		_apply_settings_to_controls(session.lobby_settings)
	_refresh_buttons()


func _refresh_remote_row() -> void:
	var profile: Dictionary = session.lobby_remote_profile if session != null else {}
	if profile.is_empty():
		remote_name_label.text = "Waiting..."
		remote_army_label.text = "-"
		remote_color_label.text = "-"
		remote_ready_check.button_pressed = false
		return
	remote_name_label.text = String(profile.get("name", "?"))
	var faction_index: int = SessionScript.LOBBY_FACTION_IDS.find(String(profile.get("faction", "men")))
	remote_army_label.text = FACTION_NAMES[faction_index] if faction_index >= 0 else "?"
	var color_index := clampi(int(profile.get("color", 0)), 0, SessionScript.LOBBY_COLOR_NAMES.size() - 1)
	remote_color_label.text = SessionScript.LOBBY_COLOR_NAMES[color_index]
	remote_ready_check.button_pressed = bool(profile.get("ready", false))


func _selected_settings() -> Dictionary:
	return {
		"map_id": String(map_opt.get_item_metadata(maxi(0, map_opt.selected))),
		"resources": int(resources_opt.get_item_metadata(maxi(0, resources_opt.selected))),
		"cp_factor": float(cp_opt.get_item_metadata(maxi(0, cp_opt.selected))),
		"build_plots": bool(build_mode_opt.get_item_metadata(maxi(0, build_mode_opt.selected))),
	}


func _send_settings() -> void:
	if session == null or not is_host:
		return
	var settings := _selected_settings()
	session.send_lobby_settings(
		String(settings["map_id"]), int(settings["resources"]),
		float(settings["cp_factor"]), bool(settings["build_plots"])
	)


func _apply_settings_to_controls(settings: Dictionary) -> void:
	var map_index: int = SessionScript.LOBBY_MAP_IDS.find(String(settings.get("map_id", "")))
	if map_index >= 0:
		map_opt.select(map_index)
		map_value_label.text = MAP_NAMES[map_index]
	var resources_index: int = SessionScript.LOBBY_RESOURCE_VALUES.find(int(settings.get("resources", 1200)))
	if resources_index >= 0:
		resources_opt.select(resources_index)
		resources_value_label.text = str(SessionScript.LOBBY_RESOURCE_VALUES[resources_index])
	var cp_index: int = SessionScript.LOBBY_CP_FACTORS.find(float(settings.get("cp_factor", 1.0)))
	if cp_index >= 0:
		cp_opt.select(cp_index)
		cp_value_label.text = "%sX" % String.num(SessionScript.LOBBY_CP_FACTORS[cp_index], 1)
	var build_plots := bool(settings.get("build_plots", false))
	build_mode_opt.select(1 if build_plots else 0)
	build_mode_value_label.text = "Build Plots (BFME1)" if build_plots else "Freeform (BFME2)"


func _refresh_buttons() -> void:
	var connected: bool = session != null and bool(session.connected) and bool(session.handshake_complete)
	ready_button.disabled = not connected
	launch_button.visible = is_host
	var both_ready := false
	if connected:
		both_ready = local_ready_check.button_pressed and not _profile_dirty \
			and bool(session.lobby_remote_profile.get("ready", false))
	launch_button.disabled = not (is_host and both_ready)


func _on_launch_pressed() -> void:
	if session == null or _launched:
		return
	_announce_profile()
	if not session.send_lobby_launch():
		set_status("Launch requires both players to be ready.", true)


func _on_lobby_launch_accepted(roster: Array) -> void:
	if _launched:
		return
	_launched = true
	_apply_launch_to_game_state(roster)
	set_status("All players ready - starting the match...")
	launch_confirmed.emit()


## Both peers run this with byte-identical roster + settings, so the GameState
## selection (which seeds the deterministic sim) matches on both machines.
## retail_mp_mode/address/port were already written at connect time and stay
## untouched here.
func _apply_launch_to_game_state(roster: Array) -> void:
	var game_state := get_node_or_null("/root/GameState")
	if game_state == null or session == null or roster.size() != 2:
		return
	var settings: Dictionary = session.lobby_launch_settings
	var host_descriptor := roster[0] as Dictionary
	var guest_descriptor := roster[1] as Dictionary
	game_state.set("retail_player_faction", String(host_descriptor.get("faction", "men")))
	game_state.set("retail_enemy_faction", String(guest_descriptor.get("faction", "men")))
	game_state.set("retail_map_id", String(settings.get("map_id", "bfme2.map.fords-of-isen-ii")))
	game_state.set("retail_initial_resources", int(settings.get("resources", 1200)))
	game_state.set("retail_command_point_factor", float(settings.get("cp_factor", 1.0)))
	game_state.set("retail_build_plots_only", bool(settings.get("build_plots", false)))
	game_state.set("retail_player_start_index", 0)
	game_state.set("retail_player_color", host_descriptor.get("color", SessionScript.LOBBY_HOUSE_COLORS[0]))
	game_state.set("retail_enemy_color", guest_descriptor.get("color", SessionScript.LOBBY_HOUSE_COLORS[1]))
	game_state.set("retail_team_setup", roster.duplicate(true))
	game_state.set("retail_mp_player_name", String(local_profile_fields()["name"]))
	game_state.set("retail_mp_peer_name", String(session.lobby_remote_profile.get("name", DEFAULT_GUEST_NAME if is_host else DEFAULT_HOST_NAME)))


func _on_leave_pressed() -> void:
	if session != null:
		session.close()
	close_lobby()
	leave_requested.emit()
