## Same mapped rims and opaque RGB artwork as ControlBarRadialMenu's circles.
extends Button
var source: Node
var artwork: Texture2D
var selected := false
var evil := false
var _halo: TextureRect
var _glyph: TextureRect
var caption := ""
var _rim: TextureRect
var _picture: TextureRect
var _label: Label
var _rims: Dictionary = {}

func _ready() -> void:
	focus_mode = Control.FOCUS_NONE
	mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	for state in ["normal", "hover", "pressed", "disabled", "focus"]:
		add_theme_stylebox_override(state, StyleBoxEmpty.new())
	for name_text in ["RadialBorder", "RadialOver", "RadialPush"]:
		_rims[name_text] = source.get_command_icon(name_text)
	_picture = TextureRect.new()
	_picture.texture = artwork
	_picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_picture.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var shader := Shader.new()
	shader.code = """shader_type canvas_item;
	uniform vec2 rect_size = vec2(1.0);
	varying vec2 local_uv;
	varying vec4 tint;
	void vertex() { local_uv = VERTEX / rect_size; tint = COLOR; }
	void fragment() {
		float edge = length(local_uv - vec2(0.5));
		float alpha = 1.0 - smoothstep(0.5 - fwidth(edge), 0.5, edge);
		COLOR = vec4(texture(TEXTURE, UV).rgb * tint.rgb, alpha * tint.a);
	}"""
	var material_instance := ShaderMaterial.new()
	material_instance.shader = shader
	_picture.material = material_instance
	add_child(_picture)
	_label = Label.new()
	_label.text = caption
	_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_label.add_theme_color_override("font_color", Color("f2dca1"))
	_label.visible = artwork == null
	add_child(_label)
	_rim = TextureRect.new()
	_rim.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_rim.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_rim)
	_halo = TextureRect.new()
	_halo.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_halo.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_halo)
	_glyph = TextureRect.new()
	_glyph.texture = preload("res://scripts/controller_art.gd").texture("a_button")
	_glyph.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_glyph.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_glyph)
	resized.connect(_layout)
	mouse_entered.connect(_update_rim)
	mouse_exited.connect(_update_rim)
	button_down.connect(_update_rim)
	button_up.connect(_update_rim)
	_layout()
	_update_rim()

func _has_point(point: Vector2) -> bool:
	return ((point - size * 0.5) / (size * 0.5)).length_squared() <= 1.0

func _draw() -> void:
	if artwork == null:
		draw_circle(size * 0.5, minf(size.x, size.y) * 0.42, Color("172422"))

func _layout() -> void:
	var border: Texture2D = _rims.get("RadialBorder")
	var ratio := 48.0 / float(border.get_width()) if border != null else 0.85
	_picture.size = size * ratio
	_picture.position = (size - _picture.size) * 0.5
	(_picture.material as ShaderMaterial).set_shader_parameter("rect_size", _picture.size)
	_rim.size = size
	_label.size = size * 0.75
	_label.position = size * 0.125
	_label.add_theme_font_size_override("font_size", maxi(8, int(size.x / 6.0)))
	_halo.size = size * 1.5
	_halo.position = -size * 0.25
	_glyph.size = size * 0.38
	_glyph.position = size * Vector2(0.63, 0.71)
	queue_redraw()

func _process(_delta: float) -> void:
	var active := selected or is_hovered()
	_halo.visible = active
	_glyph.visible = active
	if active:
		_halo.texture = preload("res://scripts/controller_art.gd").frame("select_ring" if evil else "select_ring_good")

func _update_rim() -> void:
	_rim.texture = _rims.get("RadialPush" if is_pressed() else "RadialOver" if selected or is_hovered() else "RadialBorder")
