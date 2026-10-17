## Search the native skirmish map list without changing the selected map.
extends CanvasLayer

var fs: RefCounted
var world: Node # the game's GameWorld: the browser resolves the fortress model with it instead of loading its own
var preload_frames := -1 # frames until the browser preloads; -1: not scheduled
var browser: CanvasLayer
var browse: Button
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
	browse = Button.new()
	browse.name = "ExploreMaps"
	browse.text = "Explore maps"
	browse.tooltip_text = "Open the 3D map browser"
	var button_style := StyleBoxFlat.new()
	button_style.bg_color = Color("3a2f17")
	button_style.border_color = Color("d7bd7d")
	button_style.set_border_width_all(2)
	button_style.set_corner_radius_all(3)
	browse.add_theme_stylebox_override("normal", button_style)
	var hover: StyleBoxFlat = button_style.duplicate()
	hover.bg_color = Color("5a4823")
	browse.add_theme_stylebox_override("hover", hover)
	browse.add_theme_stylebox_override("pressed", hover)
	browse.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	browse.add_theme_color_override("font_color", Color("f3dc9a"))
	browse.add_theme_color_override("font_hover_color", Color("fff2c8"))
	browse.pressed.connect(_open_browser)
	add_child(browse)
	browse.hide()

func _make_browser() -> void:
	if not is_instance_valid(browser):
		browser = preload("res://scripts/skirmish_map_browser.gd").new()
		browser.shell = shell
		browser.fs = fs
		browser.resolver = world
		add_child(browser)

func _open_browser():
	_make_browser()
	preload_frames = -1
	browser.show_browser()

func _search(text: String) -> void:
	if is_instance_valid(shell):
		shell.lobby_apply({"map_search": text})

func _process(_delta: float) -> void:
	if not is_instance_valid(shell):
		field.hide();browse.hide()
		return
	var stack: PackedStringArray = shell.shell_stack()
	var active: bool = shell.is_visible_in_tree() and not stack.is_empty() and stack[-1].to_lower() == "skirmish.apt"
	var rect: Dictionary = shell.lobby_gadget_rect("map-list") if active else {}
	active = active and rect.get("found", false) and rect.get("enabled", false)
	if not active:
		if field.has_focus(): field.release_focus()
		field.hide();browse.hide()
		preload_frames = -1
		return
	# Preload the browser's object templates and the current map's preview once the skirmish screen has drawn (a few frames in), so
	# Explore maps opens on a finished preview.
	if not is_instance_valid(browser) and preload_frames < 0:
		preload_frames = 10
	elif preload_frames > 0:
		preload_frames -= 1
		if preload_frames == 0:
			_make_browser()
			browser.prepare()
	if not field.visible:
		field.text = shell.lobby_state().get("map_search", "")
	field.show();browse.visible=not is_instance_valid(browser) or not browser.open
	var scale := get_viewport().get_visible_rect().size / Vector2(1024, 768)
	field.position = Vector2(rect.x, maxf(0, rect.y - 108)) * scale
	field.size = Vector2(minf(rect.w, 220), 28) * scale
	browse.position = field.position + Vector2(field.size.x + 8 * scale.x, 0)
	browse.size = Vector2(120, 28) * scale
	browse.add_theme_font_size_override("font_size", maxi(12, roundi(15 * scale.y)))
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
