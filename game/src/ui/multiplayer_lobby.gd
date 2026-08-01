extends Panel

## MULTIPLAYER game lobby, entered after the NETWORK flyout's host/join
## connects. Modeled on the retail GAME SETUP screen (reference REF-09/REF-10:
## per-player rows with Army/Team/Color columns; REF-13: Online settings
## conventions) in the same green shell language as the rest of the menu.
##
## The panel is a thin view over RetailLockstepSession's pre-game lobby
## protocol: profiles (name/faction/color/ready) are per-seat, chat is
## broadcast, and match settings, seat alliances and launch are HOST-
## authoritative (read-only labels on a guest). Launch is a host-only roster
## echo every peer verifies byte-equal before accepting. On an accepted launch
## EVERY peer writes the identical GameState selection (retail_team_setup and
## friends) and `launch_confirmed` fires — the menu owns the actual scene change.
##
## SEATS. There are MAX_SEATS rows. Row 0 is always the local player and is the
## only editable one; rows 1..N-1 mirror the other occupied seats in ascending
## seat order, and unoccupied rows are hidden. The TEAM column shows the seat's
## ALLIANCE — which side it fights on — which the host can reassign. Its default
## is seat + 1 (free-for-all), so a two-player lobby still reads 1 and 2 exactly
## as it did before seats existed.

signal launch_confirmed
signal leave_requested

const SessionScript = preload("res://src/retail_slice/retail_lockstep_session.gd")

const FACTION_NAMES: Array[String] = ["Men", "Elves", "Dwarves", "Isengard", "Mordor", "Goblins", "Angmar"]
const MAP_NAMES: Array[String] = ["Fords of Isen II", "Rivendell", "Mount Doom", "Dagorlad", "Mordor"]
const DEFAULT_HOST_NAME := "Player"
const DEFAULT_GUEST_NAME := "Challenger"

const MAX_SEATS := SessionScript.MAX_SEATS
const REMOTE_ROWS := MAX_SEATS - 1
## Row geometry. main_menu gives this panel the NETWORK flyout's rectangle,
## which is 800x504 — NOT the roomier size a standalone test may construct — so
## eight seat rows, the settings block, the chat panel, the status line and the
## action row all have to fit inside 504px without a scroll container. Hence the
## narrow columns AND the settings block sitting beside the rows rather than
## under them. ROW_HEIGHT is 31 because that is the effective minimum height
## Godot gives a themed LineEdit/OptionButton here: asking for less does not
## shrink the control, it just makes the rows silently overlap.
## mp_eight_peer_runner asserts the result really does fit the real rectangle.
const ROW_TOP := 58.0
const ROW_PITCH := 32.0
const ROW_HEIGHT := 31.0
const COL_NAME_X := 30.0
const COL_NAME_W := 150.0
const COL_ARMY_X := 186.0
const COL_ARMY_W := 120.0
const COL_TEAM_X := 312.0
const COL_TEAM_W := 56.0
const COL_COLOR_X := 374.0
const COL_COLOR_W := 110.0
const COL_READY_X := 490.0
## Settings column, to the right of the seat rows.
const SETTINGS_X := 530.0
const SETTINGS_W := 240.0

var session
var is_host := false

var heading_label: Label
var name_edit: LineEdit
var army_opt: OptionButton
var color_opt: OptionButton
var local_team_label: Label
var local_team_opt: OptionButton
var local_ready_check: CheckBox
## Row 1 (the primary remote seat). These keep their original names because the
## two-player callers and runners read them directly; the full set lives in the
## *_labels arrays below, of which these are element 0.
var remote_name_label: Label
var remote_army_label: Label
var remote_team_label: Label
var remote_color_label: Label
var remote_ready_check: CheckBox
var remote_name_labels: Array[Label] = []
var remote_army_labels: Array[Label] = []
var remote_team_labels: Array[Label] = []
var remote_team_opts: Array[OptionButton] = []
var remote_color_labels: Array[Label] = []
var remote_ready_checks: Array[CheckBox] = []
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
## Seat ids currently shown in remote rows, in row order. Lets a TEAM
## OptionButton on row k know which seat it is reassigning.
var _remote_row_seats: Array[int] = []


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
	local_team_opt.select(0 if is_host else 1)
	local_ready_check.button_pressed = false
	chat_log_label.text = ""
	_set_settings_editable(is_host)
	_apply_settings_to_controls(SessionScript.lobby_default_settings())
	if session != null:
		session.lobby_updated.connect(_on_lobby_updated)
		session.lobby_chat_received.connect(_on_lobby_chat)
		session.lobby_launch_accepted.connect(_on_lobby_launch_accepted)
		if session.has_signal("join_refused"):
			session.join_refused.connect(_on_join_refused)
	set_status("Waiting for players to connect..." if is_host else "Connecting to the host...")
	_refresh_seat_rows()
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
		if session.has_signal("join_refused") and session.join_refused.is_connected(_on_join_refused):
			session.join_refused.disconnect(_on_join_refused)
	session = null
	visible = false


func _process(_delta: float) -> void:
	if session == null:
		return
	session.poll()
	var connected := bool(session.connected) and bool(session.handshake_complete)
	if connected and not _was_connected:
		_was_connected = true
		set_status("Connected. Pick your army and ready up.")
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
	heading_label.position = Vector2(0, 6)
	heading_label.size = Vector2(size.x, 26)
	heading_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	heading_label.add_theme_font_size_override("font_size", 20)
	heading_label.add_theme_color_override("font_color", Color(0.82, 0.9, 0.78))
	add_child(heading_label)

	# --- player rows, REF-09 column layout (left column) ---------------------
	var columns := [
		["PLAYER", COL_NAME_X, COL_NAME_W],
		["ARMY", COL_ARMY_X, COL_ARMY_W],
		["TEAM", COL_TEAM_X, COL_TEAM_W],
		["COLOR", COL_COLOR_X, COL_COLOR_W],
		["READY", COL_READY_X, 40.0],
	]
	for column_value in columns:
		var column := column_value as Array
		var header := Label.new()
		header.name = "Header%s" % String(column[0]).capitalize()
		header.text = String(column[0])
		header.position = Vector2(float(column[1]), 36)
		header.size = Vector2(float(column[2]), 18)
		header.add_theme_font_size_override("font_size", 12)
		header.add_theme_color_override("font_color", Color(0.85, 0.78, 0.5))
		add_child(header)

	# Local row (always row 0 on screen; the seat id depends on join order).
	name_edit = LineEdit.new()
	name_edit.name = "LocalName"
	name_edit.text = DEFAULT_HOST_NAME
	name_edit.max_length = SessionScript.LOBBY_NAME_MAX
	name_edit.position = Vector2(COL_NAME_X, ROW_TOP)
	name_edit.size = Vector2(COL_NAME_W, ROW_HEIGHT)
	name_edit.text_changed.connect(func(_text: String) -> void: _on_profile_edited())
	add_child(name_edit)
	army_opt = OptionButton.new()
	army_opt.name = "LocalArmy"
	for index in SessionScript.LOBBY_FACTION_IDS.size():
		army_opt.add_item(FACTION_NAMES[index])
		army_opt.set_item_metadata(index, SessionScript.LOBBY_FACTION_IDS[index])
	army_opt.select(0)
	army_opt.position = Vector2(COL_ARMY_X, ROW_TOP)
	army_opt.size = Vector2(COL_ARMY_W, ROW_HEIGHT)
	army_opt.item_selected.connect(func(_index: int) -> void: _on_profile_edited())
	add_child(army_opt)
	local_team_label = _row_label("LocalTeam", "1", Vector2(COL_TEAM_X, ROW_TOP + 5), COL_TEAM_W)
	add_child(local_team_label)
	local_team_opt = _alliance_option("LocalTeamOpt", Vector2(COL_TEAM_X, ROW_TOP))
	local_team_opt.item_selected.connect(func(index: int) -> void: _on_alliance_selected(-1, index))
	add_child(local_team_opt)
	color_opt = OptionButton.new()
	color_opt.name = "LocalColor"
	for index in SessionScript.LOBBY_COLOR_NAMES.size():
		color_opt.add_item(SessionScript.LOBBY_COLOR_NAMES[index])
		color_opt.set_item_metadata(index, index)
	color_opt.select(0)
	color_opt.position = Vector2(COL_COLOR_X, ROW_TOP)
	color_opt.size = Vector2(COL_COLOR_W, ROW_HEIGHT)
	color_opt.item_selected.connect(func(_index: int) -> void: _on_profile_edited())
	add_child(color_opt)
	local_ready_check = CheckBox.new()
	local_ready_check.name = "LocalReady"
	local_ready_check.position = Vector2(COL_READY_X, ROW_TOP)
	local_ready_check.toggled.connect(_on_ready_toggled)
	add_child(local_ready_check)

	# Remote rows: read-only mirrors of the other seats' announced profiles.
	for row in REMOTE_ROWS:
		var row_y := ROW_TOP + ROW_PITCH * (row + 1)
		var suffix := str(row + 1)
		var name_label := _row_label("RemoteName%s" % suffix, "...", Vector2(COL_NAME_X, row_y + 5), COL_NAME_W)
		add_child(name_label)
		remote_name_labels.append(name_label)
		var army_label := _row_label("RemoteArmy%s" % suffix, "-", Vector2(COL_ARMY_X, row_y + 5), COL_ARMY_W)
		add_child(army_label)
		remote_army_labels.append(army_label)
		var team_label := _row_label("RemoteTeam%s" % suffix, str(row + 2), Vector2(COL_TEAM_X, row_y + 5), COL_TEAM_W)
		add_child(team_label)
		remote_team_labels.append(team_label)
		var team_option := _alliance_option("RemoteTeamOpt%s" % suffix, Vector2(COL_TEAM_X, row_y))
		var captured_row := row
		team_option.item_selected.connect(func(index: int) -> void: _on_alliance_selected(captured_row, index))
		add_child(team_option)
		remote_team_opts.append(team_option)
		var color_label := _row_label("RemoteColor%s" % suffix, "-", Vector2(COL_COLOR_X, row_y + 5), COL_COLOR_W)
		add_child(color_label)
		remote_color_labels.append(color_label)
		var ready_check := CheckBox.new()
		ready_check.name = "RemoteReady%s" % suffix
		ready_check.position = Vector2(COL_READY_X, row_y)
		ready_check.disabled = true
		add_child(ready_check)
		remote_ready_checks.append(ready_check)
	remote_name_label = remote_name_labels[0]
	remote_army_label = remote_army_labels[0]
	remote_team_label = remote_team_labels[0]
	remote_color_label = remote_color_labels[0]
	remote_ready_check = remote_ready_checks[0]

	# --- host settings block (REF-10 rules column) ---------------------------
	# Placed BESIDE the seat rows rather than under them: eight rows plus a
	# stacked settings block does not fit the 504px rectangle main_menu hands
	# this panel, and silently drawing off the bottom edge is not an option.
	var settings_heading := Label.new()
	settings_heading.name = "SettingsHeading"
	settings_heading.text = "GAME SETTINGS"
	settings_heading.position = Vector2(SETTINGS_X, 36)
	settings_heading.size = Vector2(SETTINGS_W, 18)
	settings_heading.add_theme_font_size_override("font_size", 14)
	settings_heading.add_theme_color_override("font_color", Color(0.85, 0.78, 0.5))
	add_child(settings_heading)

	var map_y := ROW_TOP + 18.0
	add_child(_field_label("MapLabel", "Map", Vector2(SETTINGS_X, ROW_TOP)))
	map_opt = OptionButton.new()
	map_opt.name = "MapOpt"
	for index in SessionScript.LOBBY_MAP_IDS.size():
		map_opt.add_item(MAP_NAMES[index])
		map_opt.set_item_metadata(index, SessionScript.LOBBY_MAP_IDS[index])
	map_opt.select(0)
	map_opt.position = Vector2(SETTINGS_X, map_y)
	map_opt.size = Vector2(SETTINGS_W, ROW_HEIGHT)
	map_opt.item_selected.connect(func(_index: int) -> void: _send_settings())
	add_child(map_opt)
	map_value_label = _row_label("MapValue", MAP_NAMES[0], Vector2(SETTINGS_X, map_y + 5), SETTINGS_W)
	add_child(map_value_label)

	var resources_label_y := map_y + ROW_HEIGHT + 6.0
	var resources_y := resources_label_y + 18.0
	add_child(_field_label("ResourcesLabel", "Resources", Vector2(SETTINGS_X, resources_label_y)))
	resources_opt = OptionButton.new()
	resources_opt.name = "ResourcesOpt"
	for index in SessionScript.LOBBY_RESOURCE_VALUES.size():
		var amount: int = SessionScript.LOBBY_RESOURCE_VALUES[index]
		resources_opt.add_item(str(amount))
		resources_opt.set_item_metadata(index, amount)
	resources_opt.select(SessionScript.LOBBY_RESOURCE_VALUES.find(1200))
	resources_opt.position = Vector2(SETTINGS_X, resources_y)
	resources_opt.size = Vector2(SETTINGS_W, ROW_HEIGHT)
	resources_opt.item_selected.connect(func(_index: int) -> void: _send_settings())
	add_child(resources_opt)
	resources_value_label = _row_label("ResourcesValue", "1200", Vector2(SETTINGS_X, resources_y + 5), SETTINGS_W)
	add_child(resources_value_label)

	var cp_label_y := resources_y + ROW_HEIGHT + 6.0
	var cp_y := cp_label_y + 18.0
	add_child(_field_label("CpLabel", "CP Factor", Vector2(SETTINGS_X, cp_label_y)))
	cp_opt = OptionButton.new()
	cp_opt.name = "CpOpt"
	for index in SessionScript.LOBBY_CP_FACTORS.size():
		var factor: float = SessionScript.LOBBY_CP_FACTORS[index]
		cp_opt.add_item("%sX" % String.num(factor, 1))
		cp_opt.set_item_metadata(index, factor)
	cp_opt.select(SessionScript.LOBBY_CP_FACTORS.find(1.0))
	cp_opt.position = Vector2(SETTINGS_X, cp_y)
	cp_opt.size = Vector2(SETTINGS_W, ROW_HEIGHT)
	cp_opt.item_selected.connect(func(_index: int) -> void: _send_settings())
	add_child(cp_opt)
	cp_value_label = _row_label("CpValue", "1.0X", Vector2(SETTINGS_X, cp_y + 5), SETTINGS_W)
	add_child(cp_value_label)

	var build_label_y := cp_y + ROW_HEIGHT + 6.0
	var build_y := build_label_y + 18.0
	add_child(_field_label("BuildModeLabel", "Build Mode", Vector2(SETTINGS_X, build_label_y)))
	build_mode_opt = OptionButton.new()
	build_mode_opt.name = "BuildModeOpt"
	build_mode_opt.add_item("Freeform (BFME2)")
	build_mode_opt.set_item_metadata(0, false)
	build_mode_opt.add_item("Build Plots (BFME1)")
	build_mode_opt.set_item_metadata(1, true)
	build_mode_opt.select(0)
	build_mode_opt.position = Vector2(SETTINGS_X, build_y)
	build_mode_opt.size = Vector2(SETTINGS_W, ROW_HEIGHT)
	build_mode_opt.item_selected.connect(func(_index: int) -> void: _send_settings())
	add_child(build_mode_opt)
	build_mode_value_label = _row_label("BuildModeValue", "Freeform (BFME2)", Vector2(SETTINGS_X, build_y + 5), SETTINGS_W)
	add_child(build_mode_value_label)

	# --- chat (full width, below both columns) -------------------------------
	var chat_top := ROW_TOP + ROW_PITCH * MAX_SEATS + 6.0
	var chat_log_height := maxf(52.0, size.y - chat_top - 130.0)
	var chat_heading := Label.new()
	chat_heading.name = "ChatHeading"
	chat_heading.text = "CHAT"
	chat_heading.position = Vector2(COL_NAME_X, chat_top)
	chat_heading.size = Vector2(200, 18)
	chat_heading.add_theme_font_size_override("font_size", 14)
	chat_heading.add_theme_color_override("font_color", Color(0.85, 0.78, 0.5))
	add_child(chat_heading)
	chat_log_label = RichTextLabel.new()
	chat_log_label.name = "ChatLog"
	chat_log_label.position = Vector2(COL_NAME_X, chat_top + 20.0)
	chat_log_label.size = Vector2(size.x - 60, chat_log_height)
	chat_log_label.scroll_following = true
	chat_log_label.add_theme_font_size_override("normal_font_size", 13)
	add_child(chat_log_label)
	var chat_edit_y := chat_top + 24.0 + chat_log_height
	chat_edit = LineEdit.new()
	chat_edit.name = "ChatEdit"
	chat_edit.max_length = SessionScript.LOBBY_CHAT_MAX
	chat_edit.placeholder_text = "Say something..."
	chat_edit.position = Vector2(COL_NAME_X, chat_edit_y)
	chat_edit.size = Vector2(size.x - 180, ROW_HEIGHT)
	chat_edit.text_submitted.connect(func(_text: String) -> void: _on_chat_send())
	add_child(chat_edit)
	chat_send_button = Button.new()
	chat_send_button.name = "ChatSend"
	chat_send_button.text = "SEND"
	chat_send_button.position = Vector2(size.x - 140, chat_edit_y)
	chat_send_button.size = Vector2(110, ROW_HEIGHT)
	chat_send_button.pressed.connect(_on_chat_send)
	add_child(chat_send_button)

	# --- status + action row -------------------------------------------------
	status_label = Label.new()
	status_label.name = "Status"
	status_label.text = ""
	status_label.position = Vector2(COL_NAME_X, size.y - 70)
	status_label.size = Vector2(size.x - 60, 18)
	status_label.clip_text = true
	status_label.add_theme_font_size_override("font_size", 12)
	status_label.add_theme_color_override("font_color", Color(0.66, 0.76, 0.7, 0.85))
	add_child(status_label)

	ready_button = Button.new()
	ready_button.name = "ReadyButton"
	ready_button.text = "READY"
	ready_button.position = Vector2(COL_NAME_X, size.y - 44)
	ready_button.size = Vector2(150, 38)
	ready_button.theme_type_variation = "NavButton"
	ready_button.pressed.connect(func() -> void: local_ready_check.button_pressed = not local_ready_check.button_pressed)
	add_child(ready_button)

	launch_button = Button.new()
	launch_button.name = "LaunchButton"
	launch_button.text = "LAUNCH GAME"
	launch_button.position = Vector2(size.x / 2.0 - 100, size.y - 44)
	launch_button.size = Vector2(200, 38)
	launch_button.theme_type_variation = "NavButton"
	launch_button.disabled = true
	launch_button.pressed.connect(_on_launch_pressed)
	add_child(launch_button)

	leave_button = Button.new()
	leave_button.name = "LeaveButton"
	leave_button.text = "LEAVE"
	leave_button.position = Vector2(size.x - 180, size.y - 44)
	leave_button.size = Vector2(150, 38)
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
	label.size = Vector2(SETTINGS_W, 18)
	label.add_theme_font_size_override("font_size", 13)
	return label


## Alliance picker (the TEAM column). Only ever visible on the host, which is
## the sole authority for which side a seat fights on.
func _alliance_option(node_name: String, at: Vector2) -> OptionButton:
	var option := OptionButton.new()
	option.name = node_name
	for index in SessionScript.LOBBY_ALLIANCE_MAX:
		var alliance := SessionScript.LOBBY_ALLIANCE_MIN + index
		option.add_item(str(alliance))
		option.set_item_metadata(index, alliance)
	option.select(0)
	option.position = at
	option.size = Vector2(COL_TEAM_W, ROW_HEIGHT)
	option.visible = false
	return option


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
	# The alliance pickers follow the same host-authoritative rule.
	local_team_opt.visible = editable
	local_team_label.visible = not editable


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


## Host-only. Reassigns the alliance of the local seat (row < 0) or of the seat
## shown in remote row `row`. The session clears everyone's ready flag, because
## the match they agreed to is not the match this would now start.
func _on_alliance_selected(row: int, index: int) -> void:
	if session == null or not is_host:
		return
	var seat := int(session.local_seat)
	if row >= 0:
		if row >= _remote_row_seats.size():
			return
		seat = _remote_row_seats[row]
	if seat < 0:
		return
	var alliance := SessionScript.LOBBY_ALLIANCE_MIN + index
	if not session.send_lobby_seat_alliance(seat, alliance):
		set_status("Could not change that team assignment.", true)
		return
	if local_ready_check.button_pressed:
		local_ready_check.button_pressed = false


func _on_chat_send() -> void:
	if session == null:
		return
	var text := chat_edit.text.strip_edges()
	if text == "":
		return
	# Validate the text itself first so the error is honest: invalid text never
	# transmits (the session re-checks fail-closed), and a transport problem is
	# never blamed on the message length.
	if not SessionScript.lobby_chat_valid(text):
		set_status(
			"Message not sent: chat is limited to %d printable ASCII characters." % SessionScript.LOBBY_CHAT_MAX,
			true
		)
		return
	var connected: bool = bool(session.connected) and bool(session.handshake_complete)
	if connected and not session.send_lobby_chat(text):
		set_status("Message not sent: the connection is not ready. Try again.", true)
		return
	# Alone in the lobby there is no peer to transmit to: the line lands in the
	# local log only. A guest who joins later simply never sees pre-join history
	# (retail-consistent). When connected, the send above already reached the peer.
	_append_chat_line(local_profile_fields()["name"], text)
	chat_edit.text = ""


func _on_lobby_chat(team: int, text: String) -> void:
	_append_chat_line(_name_for_team(team), text)


## Author label for an incoming chat line, resolved through the seat table so a
## six-player lobby does not attribute everything to "the other player".
func _name_for_team(team: int) -> String:
	if session == null:
		return "?"
	for row_value in session.lobby_seats:
		var row := row_value as Dictionary
		if int(row.get("team", -1)) != team:
			continue
		if bool(row.get("announced", false)) and String(row.get("name", "")) != "":
			return String(row["name"])
		return "Seat %d" % (int(row.get("seat", 0)) + 1)
	return String(session.lobby_remote_profile.get("name", DEFAULT_GUEST_NAME if is_host else DEFAULT_HOST_NAME))


func _append_chat_line(author: String, text: String) -> void:
	if chat_log_label.text != "":
		chat_log_label.text += "\n"
	chat_log_label.text += "%s: %s" % [author, text]


func _on_lobby_updated() -> void:
	_refresh_seat_rows()
	if not is_host and session != null and not session.lobby_settings.is_empty():
		_apply_settings_to_controls(session.lobby_settings)
	_refresh_buttons()


func _on_join_refused(reason: String) -> void:
	set_status("The host refused the join: %s" % reason, true)
	_refresh_buttons()


## Fills row 0 from the local controls and rows 1..N from the session's seat
## table, hiding rows for seats nobody occupies.
func _refresh_seat_rows() -> void:
	var seats: Array = session.lobby_seats if session != null else []
	var local_seat := int(session.local_seat) if session != null else -1
	_remote_row_seats.clear()
	for row_value in seats:
		var row := row_value as Dictionary
		var seat := int(row.get("seat", -1))
		if seat < 0 or seat == local_seat:
			continue
		_remote_row_seats.append(seat)
	# Row 0's alliance display follows the table when this seat is in it.
	for row_value in seats:
		var row := row_value as Dictionary
		if int(row.get("seat", -1)) != local_seat:
			continue
		var alliance := int(row.get("alliance", 1))
		local_team_label.text = str(alliance)
		local_team_opt.select(clampi(alliance - SessionScript.LOBBY_ALLIANCE_MIN, 0, local_team_opt.item_count - 1))
	for row in REMOTE_ROWS:
		var occupied := row < _remote_row_seats.size()
		remote_name_labels[row].visible = occupied
		remote_army_labels[row].visible = occupied
		remote_color_labels[row].visible = occupied
		remote_ready_checks[row].visible = occupied
		remote_team_labels[row].visible = occupied and not is_host
		remote_team_opts[row].visible = occupied and is_host
		if not occupied:
			continue
		var record := _seat_record(_remote_row_seats[row])
		var alliance := int(record.get("alliance", row + 2))
		remote_team_labels[row].text = str(alliance)
		remote_team_opts[row].select(
			clampi(alliance - SessionScript.LOBBY_ALLIANCE_MIN, 0, remote_team_opts[row].item_count - 1)
		)
		if not bool(record.get("announced", false)):
			remote_name_labels[row].text = "Waiting..."
			remote_army_labels[row].text = "-"
			remote_color_labels[row].text = "-"
			remote_ready_checks[row].button_pressed = false
			continue
		remote_name_labels[row].text = String(record.get("name", "?"))
		var faction_index: int = SessionScript.LOBBY_FACTION_IDS.find(String(record.get("faction", "men")))
		remote_army_labels[row].text = FACTION_NAMES[faction_index] if faction_index >= 0 else "?"
		var color_index := clampi(int(record.get("color", 0)), 0, SessionScript.LOBBY_COLOR_NAMES.size() - 1)
		remote_color_labels[row].text = SessionScript.LOBBY_COLOR_NAMES[color_index]
		remote_ready_checks[row].button_pressed = bool(record.get("ready", false))
	if _remote_row_seats.is_empty():
		# No peer yet: keep the original single-remote-row "waiting" presentation
		# so a two-player lobby looks exactly as it did.
		remote_name_labels[0].visible = true
		remote_army_labels[0].visible = true
		remote_color_labels[0].visible = true
		remote_ready_checks[0].visible = true
		remote_team_labels[0].visible = not is_host
		remote_team_opts[0].visible = is_host
		remote_name_labels[0].text = "Waiting..."
		remote_army_labels[0].text = "-"
		remote_color_labels[0].text = "-"
		remote_ready_checks[0].button_pressed = false


func _seat_record(seat: int) -> Dictionary:
	if session == null:
		return {}
	for row_value in session.lobby_seats:
		var row := row_value as Dictionary
		if int(row.get("seat", -1)) == seat:
			return row
	return {}


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
	# EVERY occupied seat must have announced and confirmed. all_seats_ready()
	# is the session's own gate, so the button can never offer a launch the
	# protocol would then refuse.
	var everyone_ready := false
	if connected:
		everyone_ready = local_ready_check.button_pressed and not _profile_dirty \
			and bool(session.all_seats_ready())
	launch_button.disabled = not (is_host and everyone_ready)
	if connected and is_host and not everyone_ready and not _launched:
		var pending: Array = session.seats_not_ready()
		if session.seat_count() < 2:
			set_status("Waiting for players to connect...")
		elif not pending.is_empty():
			set_status("Waiting on: %s" % ", ".join(pending))


func _on_launch_pressed() -> void:
	if session == null or _launched:
		return
	_announce_profile()
	if not session.send_lobby_launch():
		var pending: Array = session.seats_not_ready()
		if pending.is_empty():
			set_status("Launch needs at least two players in the lobby.", true)
		else:
			set_status("Launch needs every player ready. Waiting on: %s" % ", ".join(pending), true)


func _on_lobby_launch_accepted(roster: Array) -> void:
	if _launched:
		return
	_launched = true
	_apply_launch_to_game_state(roster)
	set_status("All players ready - starting the match...")
	launch_confirmed.emit()


## Every peer runs this with a byte-identical roster + settings, so the
## GameState selection (which seeds the deterministic sim) matches everywhere.
## retail_mp_mode/address/port were already written at connect time and stay
## untouched here.
func _apply_launch_to_game_state(roster: Array) -> void:
	var game_state := get_node_or_null("/root/GameState")
	if game_state == null or session == null or roster.size() < 2:
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
	# N-player additions. The vertical slice needs to know WHICH descriptor is
	# this machine's in a roster that is no longer just [me, them]; the
	# two-player fields above remain exactly as they were.
	game_state.set("retail_mp_local_seat", int(session.local_seat))
	game_state.set("retail_mp_local_team", int(session.local_team))
	game_state.set("retail_mp_seat_count", roster.size())


func _on_leave_pressed() -> void:
	if session != null:
		session.close()
	close_lobby()
	leave_requested.emit()
