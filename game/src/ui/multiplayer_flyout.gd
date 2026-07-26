extends Panel

## MULTIPLAYER > NETWORK flyout, modeled on the retail shell (reference
## REF-01: MULTIPLAYER flyout; REF-13: Online settings' IP/port field
## conventions). Tier-1 scope is the proven lockstep path: host or join a
## 2-player LAN/direct match on the default slice scenario. Validation lives
## here; the menu owns GameState handoff and the scene transition.
##
## Discovery (LanDiscoveryScript) is purely ADDITIVE: while this page is open
## the flyout listens for host beacons on the LAN and on the Radmin VPN virtual
## LAN (26.0.0.0/8) and lists them, so a player can pick a game instead of
## typing an IP. Every manual path still works exactly as before, and a
## discovery socket that cannot bind only costs the player the list.
##
## Radmin is a third-party product: it is referred to in PLAIN TEXT only, with
## no logo or brand asset, and rows are marked with the shell's own gold/green
## palette rather than any vendor colour.

signal host_requested(port: int)
signal join_requested(address: String, port: int)
signal back_requested

const LanDiscoveryScript = preload("res://src/net/lan_discovery.gd")

const DEFAULT_PORT := 26015
const PORT_MIN := 1024
const PORT_MAX := 65535
const DEFAULT_MAP_NAME := "Fords of Isen II"
const DEFAULT_HOST_NAME := "Player"
const MAX_PLAYERS := 2

var host_port_edit: LineEdit
var join_address_edit: LineEdit
var join_port_edit: LineEdit
var host_button: Button
var join_button: Button
var back_button: Button
var status_label: Label
var local_address_edit: LineEdit
var network_hint_label: Label
var games_list: ItemList
var games_hint_label: Label

## Discovery module + the keys currently mirrored into games_list, in list
## order (ItemList metadata would be lost on the rebuilds below).
var discovery
var _listed_keys: Array[String] = []
## Session whose lobby is being advertised, so a game that has filled up stops
## appearing in everyone else's list. Set by the menu at host time.
var _advertised_session = null
var _advertised_full := false


func _ready() -> void:
	_build()
	discovery = LanDiscoveryScript.new()
	discovery.game_added.connect(func(_key: String, _record: Dictionary) -> void: _refresh_games_list())
	discovery.game_updated.connect(_on_game_record_updated)
	discovery.game_removed.connect(func(_key: String) -> void: _refresh_games_list())
	visibility_changed.connect(_on_visibility_changed)
	_refresh_local_address()
	_refresh_games_list()
	set_process(true)
	if visible:
		_on_visibility_changed()


func _exit_tree() -> void:
	if discovery != null:
		discovery.close()


func _process(delta: float) -> void:
	if discovery == null:
		return
	# A filled lobby is not joinable: stop beaconing so the entry ages out of
	# every browser within the normal 5s timeout.
	if _advertised_session != null and not _advertised_full and bool(_advertised_session.get("connected")):
		_advertised_full = true
		discovery.stop_advertising()
	discovery.poll(delta)


func _build() -> void:
	var heading := Label.new()
	heading.name = "Heading"
	heading.text = "NETWORK"
	heading.position = Vector2(0, 18)
	heading.size = Vector2(size.x, 34)
	heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	heading.add_theme_font_size_override("font_size", 24)
	heading.add_theme_color_override("font_color", Color(0.82, 0.9, 0.78))
	add_child(heading)

	var subtitle := Label.new()
	subtitle.name = "Subtitle"
	subtitle.text = "Deterministic lockstep - 2 players - Fords of Isen II"
	subtitle.position = Vector2(0, 52)
	subtitle.size = Vector2(size.x, 24)
	subtitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	subtitle.add_theme_font_size_override("font_size", 13)
	subtitle.add_theme_color_override("font_color", Color(0.66, 0.76, 0.7, 0.85))
	add_child(subtitle)

	# --- HOST GAME block -----------------------------------------------------
	var host_heading := _section_label("HostHeading", "HOST GAME", 84)
	add_child(host_heading)
	add_child(_field_label("HostPortLabel", "Port", Vector2(60, 118)))
	host_port_edit = _line_edit("HostPort", str(DEFAULT_PORT), Vector2(150, 112), 120)
	add_child(host_port_edit)
	host_button = Button.new()
	host_button.name = "HostButton"
	host_button.text = "HOST GAME"
	host_button.position = Vector2(size.x - 260, 110)
	host_button.size = Vector2(200, 40)
	host_button.theme_type_variation = "NavButton"
	host_button.pressed.connect(_on_host_pressed)
	add_child(host_button)

	# The address guests actually need, read-only but selectable so the host can
	# copy it straight out of the field (REF-13 IP field shape).
	add_child(_field_label("LocalAddressLabel", "Your IP", Vector2(60, 160)))
	local_address_edit = _line_edit("LocalAddress", "127.0.0.1", Vector2(150, 154), 300)
	local_address_edit.editable = false
	add_child(local_address_edit)

	network_hint_label = Label.new()
	network_hint_label.name = "NetworkHint"
	network_hint_label.text = ""
	network_hint_label.position = Vector2(60, 196)
	network_hint_label.size = Vector2(size.x - 120, 22)
	network_hint_label.add_theme_font_size_override("font_size", 12)
	network_hint_label.add_theme_color_override("font_color", Color(0.66, 0.76, 0.7, 0.85))
	add_child(network_hint_label)

	# --- JOIN GAME block -----------------------------------------------------
	var join_heading := _section_label("JoinHeading", "JOIN GAME", 224)
	add_child(join_heading)
	add_child(_field_label("JoinAddressLabel", "Host IP", Vector2(60, 264)))
	join_address_edit = _line_edit("JoinAddress", "127.0.0.1", Vector2(150, 258), 220)
	add_child(join_address_edit)
	add_child(_field_label("JoinPortLabel", "Port", Vector2(392, 264)))
	join_port_edit = _line_edit("JoinPort", str(DEFAULT_PORT), Vector2(444, 258), 120)
	add_child(join_port_edit)
	join_button = Button.new()
	join_button.name = "JoinButton"
	join_button.text = "JOIN GAME"
	join_button.position = Vector2(size.x - 260, 256)
	join_button.size = Vector2(200, 40)
	join_button.theme_type_variation = "NavButton"
	join_button.pressed.connect(_on_join_pressed)
	add_child(join_button)

	# --- discovered games ----------------------------------------------------
	var games_heading := _section_label("GamesHeading", "GAMES ON YOUR NETWORK", 306)
	games_heading.size = Vector2(300, 24)
	games_heading.add_theme_font_size_override("font_size", 15)
	add_child(games_heading)

	games_hint_label = Label.new()
	games_hint_label.name = "GamesHint"
	games_hint_label.text = ""
	games_hint_label.position = Vector2(360, 308)
	games_hint_label.size = Vector2(size.x - 420, 22)
	games_hint_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	games_hint_label.add_theme_font_size_override("font_size", 12)
	games_hint_label.add_theme_color_override("font_color", Color(0.66, 0.76, 0.7, 0.85))
	add_child(games_hint_label)

	games_list = ItemList.new()
	games_list.name = "GamesList"
	games_list.position = Vector2(60, 332)
	games_list.size = Vector2(size.x - 120, 60)
	games_list.allow_reselect = true
	games_list.add_theme_font_size_override("font_size", 13)
	games_list.item_selected.connect(_on_game_selected)
	games_list.item_activated.connect(_on_game_activated)
	add_child(games_list)

	# --- status + back -------------------------------------------------------
	status_label = Label.new()
	status_label.name = "Status"
	status_label.text = "Pick a listed game, or share the host IP and enter it by hand."
	status_label.position = Vector2(60, 396)
	status_label.size = Vector2(size.x - 120, 28)
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.add_theme_font_size_override("font_size", 13)
	status_label.add_theme_color_override("font_color", Color(0.66, 0.76, 0.7, 0.85))
	add_child(status_label)

	back_button = Button.new()
	back_button.name = "BackButton"
	back_button.text = "BACK"
	back_button.position = Vector2(size.x / 2.0 - 100, size.y - 78)
	back_button.size = Vector2(200, 44)
	back_button.theme_type_variation = "NavButton"
	back_button.pressed.connect(func() -> void: back_requested.emit())
	add_child(back_button)


func _section_label(node_name: String, text: String, y: float) -> Label:
	var label := Label.new()
	label.name = node_name
	label.text = text
	label.position = Vector2(60, y)
	label.size = Vector2(size.x - 120, 26)
	label.add_theme_font_size_override("font_size", 17)
	label.add_theme_color_override("font_color", Color(0.85, 0.78, 0.5))
	return label


func _field_label(node_name: String, text: String, at: Vector2) -> Label:
	var label := Label.new()
	label.name = node_name
	label.text = text
	label.position = at
	label.size = Vector2(84, 26)
	label.add_theme_font_size_override("font_size", 14)
	return label


func _line_edit(node_name: String, initial: String, at: Vector2, width: float) -> LineEdit:
	var edit := LineEdit.new()
	edit.name = node_name
	edit.text = initial
	edit.position = at
	edit.size = Vector2(width, 38)
	return edit


## Lobby transition support: while the menu is opening a session and moving to
## the GAME LOBBY panel, the flyout's inputs lock so a double-click can never
## start two sessions; returning from the lobby unlocks them.
func set_busy(busy: bool) -> void:
	host_button.disabled = busy
	join_button.disabled = busy
	back_button.disabled = busy
	host_port_edit.editable = not busy
	join_address_edit.editable = not busy
	join_port_edit.editable = not busy
	games_list.focus_mode = Control.FOCUS_NONE if busy else Control.FOCUS_ALL
	games_list.mouse_filter = Control.MOUSE_FILTER_IGNORE if busy else Control.MOUSE_FILTER_STOP


# --- discovery -----------------------------------------------------------------

## Browsing follows the page: the socket only exists while the NETWORK flyout
## is on screen, so an idle main menu holds no bound port.
func _on_visibility_changed() -> void:
	if discovery == null:
		return
	if visible:
		_refresh_local_address()
		discovery.start_browsing()
		_refresh_games_list()
	elif not discovery.advertising:
		# A host keeps its beacon (and its own view of the port) while the GAME
		# LOBBY page is up; a guest releases the socket immediately.
		discovery.stop_browsing()


## Called by the menu once session.host(port) has actually bound, so a failed
## host never advertises a game nobody can join.
func start_advertising(game_port: int, host_name: String, map_name: String, session = null) -> void:
	if discovery == null:
		return
	_advertised_session = session
	_advertised_full = false
	discovery.start_advertising({
		"host_name": host_name if host_name.strip_edges() != "" else DEFAULT_HOST_NAME,
		"map_name": map_name if map_name.strip_edges() != "" else DEFAULT_MAP_NAME,
		"faction_name": "Men",
		"players": 1,
		"max_players": MAX_PLAYERS,
		"game_port": game_port,
	})


func stop_advertising() -> void:
	_advertised_session = null
	_advertised_full = false
	if discovery != null:
		discovery.stop_advertising()
		if not visible:
			discovery.stop_browsing()


## "Your IP" + the Radmin line. Radmin VPN is named in plain text only; the
## project ships no Radmin brand asset.
func _refresh_local_address() -> void:
	if local_address_edit == null:
		return
	var address: String = LanDiscoveryScript.preferred_local_address()
	local_address_edit.text = address
	var radmin: String = LanDiscoveryScript.local_radmin_address()
	if radmin != "":
		network_hint_label.text = "Radmin VPN detected (%s) - give this address to players outside your LAN." % radmin
		network_hint_label.add_theme_color_override("font_color", Color(0.85, 0.78, 0.5))
	else:
		network_hint_label.text = "No Radmin network found. Same-LAN play works as-is; over the internet both players need Radmin VPN (or similar)."
		network_hint_label.add_theme_color_override("font_color", Color(0.66, 0.76, 0.7, 0.85))


func _refresh_games_list() -> void:
	if games_list == null or discovery == null:
		return
	var selected_key := ""
	var selected_items := games_list.get_selected_items()
	if selected_items.size() > 0 and selected_items[0] < _listed_keys.size():
		selected_key = _listed_keys[selected_items[0]]
	games_list.clear()
	_listed_keys.clear()
	for value in discovery.entries():
		var record := value as Dictionary
		var key := String(record.get("key", ""))
		var index := games_list.add_item(_game_row_text(record))
		games_list.set_item_tooltip(index, "%s:%d" % [String(record.get("address", "")), int(record.get("gamePort", 0))])
		if String(record.get("network", "")) == LanDiscoveryScript.NETWORK_RADMIN:
			games_list.set_item_custom_fg_color(index, Color(0.85, 0.78, 0.5))
		_listed_keys.append(key)
		if key == selected_key:
			games_list.select(index)
	_refresh_games_hint()


## A beacon lands once a second per host; rewriting the row in place keeps the
## player's selection and scroll position steady instead of rebuilding the list
## under their cursor. Only a genuinely new key forces a rebuild.
func _on_game_record_updated(key: String, record: Dictionary) -> void:
	var index := _listed_keys.find(key)
	if index < 0:
		_refresh_games_list()
		return
	games_list.set_item_text(index, _game_row_text(record))


func _game_row_text(record: Dictionary) -> String:
	return "%s  -  %s  -  %d/%d  -  %s  %s" % [
		String(record.get("hostName", "Player")),
		String(record.get("mapName", "Unknown map")),
		int(record.get("players", 1)),
		int(record.get("maxPlayers", MAX_PLAYERS)),
		LanDiscoveryScript.network_label(String(record.get("network", ""))),
		String(record.get("address", "")),
	]


func _refresh_games_hint() -> void:
	if games_hint_label == null or discovery == null:
		return
	if not discovery.browsing:
		games_hint_label.text = "Discovery unavailable - use Host IP"
		return
	if _listed_keys.is_empty():
		games_hint_label.text = "Searching..."
		return
	games_hint_label.text = "%d found" % _listed_keys.size()


func _on_game_selected(index: int) -> void:
	if index < 0 or index >= _listed_keys.size():
		return
	var record: Dictionary = discovery.entry(_listed_keys[index])
	if record.is_empty():
		return
	join_address_edit.text = String(record.get("address", ""))
	join_port_edit.text = str(int(record.get("gamePort", DEFAULT_PORT)))
	set_status("Selected %s on %s. Press JOIN GAME." % [
		String(record.get("hostName", "Player")), String(record.get("address", "")),
	])


func _on_game_activated(index: int) -> void:
	_on_game_selected(index)
	if not join_button.disabled:
		_on_join_pressed()


func set_status(message: String, is_error: bool = false) -> void:
	status_label.text = message
	status_label.add_theme_color_override(
		"font_color",
		Color(0.9, 0.45, 0.4) if is_error else Color(0.66, 0.76, 0.7, 0.85)
	)


## "" on success, else the player-facing validation error. Never silent.
func validate_port(text: String) -> String:
	var trimmed := text.strip_edges()
	if not trimmed.is_valid_int():
		return "Port must be a number between %d and %d." % [PORT_MIN, PORT_MAX]
	var port := int(trimmed)
	if port < PORT_MIN or port > PORT_MAX:
		return "Port must be between %d and %d." % [PORT_MIN, PORT_MAX]
	return ""


func validate_address(text: String) -> String:
	var trimmed := text.strip_edges()
	if trimmed.is_empty():
		return "Enter the host's IP address."
	if not trimmed.is_valid_ip_address():
		return "'%s' is not a valid IP address." % trimmed
	return ""


func _on_host_pressed() -> void:
	var port_error := validate_port(host_port_edit.text)
	if port_error != "":
		set_status(port_error, true)
		return
	_refresh_local_address()
	set_status("Hosting on %s:%s - waiting for a challenger..." % [
		local_address_edit.text, host_port_edit.text.strip_edges(),
	])
	host_requested.emit(int(host_port_edit.text.strip_edges()))


func _on_join_pressed() -> void:
	var address_error := validate_address(join_address_edit.text)
	if address_error != "":
		set_status(address_error, true)
		return
	var port_error := validate_port(join_port_edit.text)
	if port_error != "":
		set_status(port_error, true)
		return
	set_status("Joining %s..." % join_address_edit.text.strip_edges())
	join_requested.emit(join_address_edit.text.strip_edges(), int(join_port_edit.text.strip_edges()))
