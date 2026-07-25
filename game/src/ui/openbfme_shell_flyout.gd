extends Panel
## Upward main-menu flyout (REF-01/02/04/05): a dark green-glass panel with the
## bright green rim that opens ABOVE the bottom-bar button that spawned it,
## stacking item bars vertically, with ornamental corner brackets and a
## downward wedge at the top edge. Chrome is repository-authored; nothing is
## copied from retail assets.
##
## Built procedurally by main_menu.gd via this script's static `build(button,
## items)`, where each item is {"id": String, "label": String, "enabled": bool,
## "tooltip": String}. Disabled rows stay listed with an honest tooltip; they
## never emit `item_selected`.
##
## Deliberately no `class_name`: the headless test runners execute as `--script`
## main loops where global script classes are not registered, so referring to
## one here would fail to compile the whole menu.

signal item_selected(id: String)

const PANEL_WIDTH := 300.0
## Vertical gap between the panel's bottom edge and the anchor button's top
## edge; the nav triangle indicator lives in this gap (REF-02).
const ANCHOR_GAP := 26.0
const CONTENT_MARGIN := 16.0
const ORNAMENT_COLOR := Color(0.45, 0.90, 0.45, 0.9)
const ORNAMENT_DIM := Color(0.24, 0.52, 0.26, 0.9)

var anchor_button: Button
var item_buttons: Array[Button] = []


static func build(anchor: Button, items: Array) -> Panel:
	var flyout = (load("res://src/ui/openbfme_shell_flyout.gd") as GDScript).new()
	flyout.name = "%sFlyoutMenu" % anchor.name
	flyout.anchor_button = anchor
	flyout.theme_type_variation = "FlyoutPanel"
	flyout.visible = false
	flyout.mouse_filter = Control.MOUSE_FILTER_PASS
	flyout.z_index = 5
	var list := VBoxContainer.new()
	list.name = "Items"
	list.add_theme_constant_override("separation", 6)
	list.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	list.offset_left = CONTENT_MARGIN
	list.offset_right = -CONTENT_MARGIN
	list.offset_top = CONTENT_MARGIN
	list.offset_bottom = -CONTENT_MARGIN
	flyout.add_child(list)
	for item_value in items:
		var item := item_value as Dictionary
		var row := Button.new()
		row.name = "Item%s" % String(item["id"]).capitalize().replace(" ", "")
		row.text = String(item["label"])
		row.theme_type_variation = "FlyoutItem"
		row.custom_minimum_size = Vector2(0, 38)
		row.disabled = not bool(item.get("enabled", false))
		var tooltip := String(item.get("tooltip", ""))
		if tooltip != "":
			row.tooltip_text = tooltip
		if not row.disabled:
			row.pressed.connect(flyout._on_item_pressed.bind(String(item["id"])))
		list.add_child(row)
		flyout.item_buttons.append(row)
	return flyout


func _ready() -> void:
	resized.connect(queue_redraw)
	get_viewport().size_changed.connect(_reposition)
	if anchor_button != null:
		anchor_button.item_rect_changed.connect(_reposition)
	_reposition()


func open() -> void:
	_reposition()
	visible = true
	for row in item_buttons:
		if not row.disabled:
			row.grab_focus()
			break


func _on_item_pressed(id: String) -> void:
	visible = false
	item_selected.emit(id)


func _reposition() -> void:
	if anchor_button == null:
		return
	var rows := maxi(1, item_buttons.size())
	var height := CONTENT_MARGIN * 2.0 + float(rows) * 38.0 + float(rows - 1) * 6.0
	var anchor_rect := anchor_button.get_global_rect()
	# The flyout is a sibling of the bar buttons inside the full-rect Center
	# node, so global and local coordinates coincide; clamp horizontally so a
	# corner button's flyout never leaves the viewport (REF-04 opens flush-left).
	var x := anchor_rect.get_center().x - PANEL_WIDTH * 0.5
	var viewport_width := get_viewport_rect().size.x
	x = clampf(x, 10.0, viewport_width - PANEL_WIDTH - 10.0)
	position = Vector2(x, anchor_rect.position.y - ANCHOR_GAP - height)
	size = Vector2(PANEL_WIDTH, height)
	queue_redraw()


func _draw() -> void:
	# Ornamental frame accents echoing the retail flyout: L-shaped corner
	# brackets and a downward double-wedge centered on the top edge.
	var bracket := 14.0
	var inset := 5.0
	var corners := [
		[Vector2(inset, inset), Vector2(1, 0), Vector2(0, 1)],
		[Vector2(size.x - inset, inset), Vector2(-1, 0), Vector2(0, 1)],
		[Vector2(inset, size.y - inset), Vector2(1, 0), Vector2(0, -1)],
		[Vector2(size.x - inset, size.y - inset), Vector2(-1, 0), Vector2(0, -1)],
	]
	for corner_value in corners:
		var origin: Vector2 = corner_value[0]
		var horizontal: Vector2 = corner_value[1]
		var vertical: Vector2 = corner_value[2]
		draw_line(origin, origin + horizontal * bracket, ORNAMENT_COLOR, 2.0, true)
		draw_line(origin, origin + vertical * bracket, ORNAMENT_COLOR, 2.0, true)
	var top_center := Vector2(size.x * 0.5, 0.0)
	draw_colored_polygon(PackedVector2Array([
		top_center + Vector2(-16.0, 0.0), top_center + Vector2(16.0, 0.0),
		top_center + Vector2(0.0, 9.0),
	]), ORNAMENT_DIM)
	draw_colored_polygon(PackedVector2Array([
		top_center + Vector2(-8.0, 0.0), top_center + Vector2(8.0, 0.0),
		top_center + Vector2(0.0, 5.0),
	]), ORNAMENT_COLOR)
	# Small down-wedge on the bottom edge pointing at the anchor button.
	var bottom_center := Vector2(size.x * 0.5, size.y)
	if anchor_button != null:
		bottom_center.x = clampf(
			anchor_button.get_global_rect().get_center().x - global_position.x,
			20.0, size.x - 20.0
		)
	draw_colored_polygon(PackedVector2Array([
		bottom_center + Vector2(-9.0, 0.0), bottom_center + Vector2(9.0, 0.0),
		bottom_center + Vector2(0.0, 7.0),
	]), ORNAMENT_DIM)
