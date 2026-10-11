## Lane UI-5: the big gold PLAY button of "Parchment & Steel" (.play: a vertical gradient #e9be56 -> #a77a22, 10 px corners, a dark ring
## and a warm glow, the word in Cinzel). A Button with empty styles: the body is drawn here, the word is a child Label (children draw over
## their parent), dimmed with the button when it is disabled.
extends Button

const Style := preload("res://scripts/ui/style.gd")

var word: Label


static func make(text: String, min_size: Vector2, font_px := 27) -> Button:
	var b: Button = load("res://scripts/ui/gold_button.gd").new()
	b.custom_minimum_size = min_size
	b.focus_mode = Control.FOCUS_ALL
	for st in ["normal", "hover", "pressed", "disabled", "focus", "hover_pressed"]:
		b.add_theme_stylebox_override(st, StyleBoxEmpty.new())
	var l := Label.new()
	l.text = text
	l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	l.set_anchors_preset(Control.PRESET_FULL_RECT)
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	Style.text(l, "Cinzel", 700, font_px, Style.INK, 4)
	b.add_child(l)
	b.word = l
	b.mouse_entered.connect(b.queue_redraw)
	b.mouse_exited.connect(b.queue_redraw)
	return b


func _draw() -> void:
	var r := Rect2(Vector2.ZERO, size)
	var lift := 2.0 if is_hovered() and not disabled else 0.0
	r.position.y -= lift
	var glow := StyleBoxFlat.new()
	glow.bg_color = Color(0, 0, 0, 0.45)
	glow.set_corner_radius_all(12)
	glow.shadow_color = Color(240 / 255.0, 190 / 255.0, 80 / 255.0, 0.25 if disabled else 0.45)
	glow.shadow_size = 18
	glow.anti_aliasing = true
	draw_style_box(glow, r.grow(3))
	var top := Style.GOLD_TOP if not disabled else Style.GOLD_TOP.darkened(0.45)
	var bottom := Style.GOLD_BOTTOM if not disabled else Style.GOLD_BOTTOM.darkened(0.45)
	if is_hovered() and not disabled:
		top = top.lightened(0.08)
	var pts := PackedVector2Array()
	var cols := PackedColorArray()
	var rad := 10.0
	for corner in [[r.position + Vector2(r.size.x - rad, rad), -PI / 2], [r.end - Vector2(rad, rad), 0.0],
			[r.position + Vector2(rad, r.size.y - rad), PI / 2], [r.position + Vector2(rad, rad), PI]]:
		for i in 7:
			var a: float = corner[1] + PI / 2 * i / 6.0
			var p: Vector2 = corner[0] + Vector2(cos(a), sin(a)) * rad
			pts.append(p)
			cols.append(top.lerp(bottom, clampf((p.y - r.position.y) / r.size.y, 0.0, 1.0)))
	draw_polygon(pts, cols)
	if has_focus():
		var f := StyleBoxFlat.new()
		f.draw_center = false
		f.border_color = Style.ACCENT
		f.set_border_width_all(1)
		f.set_corner_radius_all(12)
		draw_style_box(f, r.grow(2))
	word.position.y = -lift
	word.modulate.a = 0.5 if disabled else 1.0
