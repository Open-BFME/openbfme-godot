## Search the native skirmish map list without changing the selected map.
extends CanvasLayer

var shell: Node
var field: LineEdit

func _ready() -> void:
	layer = 15
	field = LineEdit.new()
	field.name = "MapSearch"
	field.placeholder_text = "Search maps…"
	field.tooltip_text = "Filter maps by name"
	field.clear_button_enabled = true
	field.max_length = 128
	var style := StyleBoxFlat.new()
	style.bg_color = Color("1c1a18")
	style.border_color = Color("8a7954")
	style.set_border_width_all(1)
	style.content_margin_left = 8
	style.content_margin_right = 8
	field.add_theme_stylebox_override("normal", style)
	var focused: StyleBoxFlat = style.duplicate()
	focused.border_color = Color("d7bd7d")
	field.add_theme_stylebox_override("focus", focused)
	field.add_theme_color_override("font_color", Color("eee3c7"))
	field.add_theme_color_override("font_placeholder_color", Color("b6ac94"))
	field.text_changed.connect(_search)
	field.text_submitted.connect(func(_text: String) -> void: field.release_focus())
	add_child(field)
	field.hide()

func _search(text: String) -> void:
	if is_instance_valid(shell):
		shell.lobby_apply({"map_search": text})

func _process(_delta: float) -> void:
	if not is_instance_valid(shell):
		field.hide()
		return
	var stack: PackedStringArray = shell.shell_stack()
	var active: bool = shell.is_visible_in_tree() and not stack.is_empty() and stack[-1].to_lower() == "skirmish.apt"
	var rect: Dictionary = shell.lobby_gadget_rect("map-list") if active else {}
	active = active and rect.get("found", false) and rect.get("enabled", false)
	if not active:
		if field.has_focus(): field.release_focus()
		field.hide()
		return
	if not field.visible:
		field.text = shell.lobby_state().get("map_search", "")
	field.show()
	var scale := get_viewport().get_visible_rect().size / Vector2(1024, 768)
	field.position = Vector2(rect.x, maxf(0, rect.y - 108)) * scale
	field.size = Vector2(minf(rect.w, 260), 28) * scale
	field.add_theme_font_size_override("font_size", maxi(12, roundi(15 * scale.y)))

func _input(event: InputEvent) -> void:
	if not field.visible or not field.has_focus(): return
	if event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		if not field.text.is_empty():
			field.clear()
			_search("")
		else:
			field.release_focus()
		get_viewport().set_input_as_handled()
	elif event is InputEventMouseButton and event.pressed and not field.get_global_rect().has_point(event.position):
		field.release_focus()
