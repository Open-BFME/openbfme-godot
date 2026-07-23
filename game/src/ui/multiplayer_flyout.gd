extends Panel

## MULTIPLAYER > NETWORK flyout, modeled on the retail shell (reference
## REF-01: MULTIPLAYER flyout; REF-13: Online settings' IP/port field
## conventions). Tier-1 scope is the proven lockstep path: host or join a
## 2-player LAN/direct match on the default slice scenario. Validation lives
## here; the menu owns GameState handoff and the scene transition.

signal host_requested(port: int)
signal join_requested(address: String, port: int)
signal back_requested

const DEFAULT_PORT := 26015
const PORT_MIN := 1024
const PORT_MAX := 65535

var host_port_edit: LineEdit
var join_address_edit: LineEdit
var join_port_edit: LineEdit
var host_button: Button
var join_button: Button
var back_button: Button
var status_label: Label


func _ready() -> void:
	_build()


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
	var host_heading := _section_label("HostHeading", "HOST GAME", 96)
	add_child(host_heading)
	add_child(_field_label("HostPortLabel", "Port", Vector2(60, 130)))
	host_port_edit = _line_edit("HostPort", str(DEFAULT_PORT), Vector2(150, 124), 120)
	add_child(host_port_edit)
	host_button = Button.new()
	host_button.name = "HostButton"
	host_button.text = "HOST GAME"
	host_button.position = Vector2(size.x - 260, 122)
	host_button.size = Vector2(200, 40)
	host_button.theme_type_variation = "NavButton"
	host_button.pressed.connect(_on_host_pressed)
	add_child(host_button)

	# --- JOIN GAME block -----------------------------------------------------
	var join_heading := _section_label("JoinHeading", "JOIN GAME", 196)
	add_child(join_heading)
	add_child(_field_label("JoinAddressLabel", "Host IP", Vector2(60, 230)))
	join_address_edit = _line_edit("JoinAddress", "127.0.0.1", Vector2(150, 224), 220)
	add_child(join_address_edit)
	add_child(_field_label("JoinPortLabel", "Port", Vector2(392, 230)))
	join_port_edit = _line_edit("JoinPort", str(DEFAULT_PORT), Vector2(444, 224), 120)
	add_child(join_port_edit)
	join_button = Button.new()
	join_button.name = "JoinButton"
	join_button.text = "JOIN GAME"
	join_button.position = Vector2(size.x - 260, 222)
	join_button.size = Vector2(200, 40)
	join_button.theme_type_variation = "NavButton"
	join_button.pressed.connect(_on_join_pressed)
	add_child(join_button)

	# --- status + back -------------------------------------------------------
	status_label = Label.new()
	status_label.name = "Status"
	status_label.text = "The host shares their LAN IP; the guest enters it and joins."
	status_label.position = Vector2(60, 300)
	status_label.size = Vector2(size.x - 120, 52)
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
	set_status("Hosting on port %s - waiting for a challenger..." % host_port_edit.text.strip_edges())
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
