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
## Row styling is applied HERE, per button, rather than through a shared theme
## type variation. The shell theme (openbfme_theme.gd) is owned by another lane
## on this branch; keeping the row look self-contained means this file can be
## added without editing it, and a later theme that publishes a "FlyoutItem"
## variation can simply take over by deleting `_style_row`.
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
const ROW_HEIGHT := 38.0
const ROW_SEPARATION := 6.0
const ORNAMENT_COLOR := Color(0.45, 0.90, 0.45, 0.9)
const ORNAMENT_DIM := Color(0.24, 0.52, 0.26, 0.9)
## Flyout item bars (REF-02/04/05): translucent dark bars with a lit top edge
## and a gold left accent; the hovered bar turns bright lime like retail.
const ITEM_BAR := Color(0.045, 0.115, 0.055, 0.62)
const ITEM_BAR_EDGE := Color(0.78, 0.90, 0.66, 0.55)
const TEXT_PALE_GOLD := Color("#ddd6a8")
const TEXT_PALE_GOLD_BRIGHT := Color("#f4eecb")
const TEXT_ON_LIME := Color("#12300f")
const TEXT_DISABLED := Color(0.62, 0.62, 0.50, 0.42)

var anchor_button: Button
var item_buttons: Array[Button] = []
## Item id -> its row button, so a row whose availability is decided after the
## flyout is built (WAR OF THE RING) can be re-stated without a rebuild.
var _rows_by_id: Dictionary = {}


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
	list.add_theme_constant_override("separation", int(ROW_SEPARATION))
	list.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	list.offset_left = CONTENT_MARGIN
	list.offset_right = -CONTENT_MARGIN
	list.offset_top = CONTENT_MARGIN
	list.offset_bottom = -CONTENT_MARGIN
	flyout.add_child(list)
	for item_value in items:
		var item := item_value as Dictionary
		var item_id := String(item["id"])
		var row := Button.new()
		row.name = "Item%s" % item_id.capitalize().replace(" ", "")
		row.text = String(item["label"])
		row.custom_minimum_size = Vector2(0, ROW_HEIGHT)
		flyout._style_row(row)
		# Every row routes through the same handler; `_on_item_pressed` is the
		# single place that refuses a disabled row, so a row re-enabled later
		# needs no new connection.
		row.pressed.connect(flyout._on_item_pressed.bind(item_id))
		list.add_child(row)
		flyout.item_buttons.append(row)
		flyout._rows_by_id[item_id] = row
		flyout.set_item_state(item_id, bool(item.get("enabled", false)), String(item.get("tooltip", "")))
	return flyout


func _ready() -> void:
	resized.connect(queue_redraw)
	get_viewport().size_changed.connect(_reposition)
	if anchor_button != null:
		anchor_button.item_rect_changed.connect(_reposition)
	_reposition()


## Re-state one row. A disabled row STAYS LISTED and carries the reason as its
## tooltip: hiding it would tell a player nothing, and a greyed row with no
## explanation tells them just as little.
func set_item_state(item_id: String, enabled: bool, tooltip: String) -> bool:
	var row := _rows_by_id.get(item_id, null) as Button
	if row == null:
		return false
	row.disabled = not enabled
	row.tooltip_text = tooltip
	return true


func item_row(item_id: String) -> Button:
	return _rows_by_id.get(item_id, null) as Button


func open() -> void:
	_reposition()
	visible = true
	for row in item_buttons:
		if not row.disabled:
			row.grab_focus()
			break


func _on_item_pressed(id: String) -> void:
	var row := _rows_by_id.get(id, null) as Button
	if row != null and row.disabled:
		return
	visible = false
	item_selected.emit(id)


func _style_row(row: Button) -> void:
	row.add_theme_font_size_override("font_size", 17)
	row.add_theme_color_override("font_color", TEXT_PALE_GOLD)
	row.add_theme_color_override("font_hover_color", TEXT_ON_LIME)
	row.add_theme_color_override("font_pressed_color", TEXT_ON_LIME)
	row.add_theme_color_override("font_focus_color", TEXT_PALE_GOLD_BRIGHT)
	row.add_theme_color_override("font_disabled_color", TEXT_DISABLED)
	for state in ["normal", "hover", "pressed", "focus", "disabled"]:
		row.add_theme_stylebox_override(state, _item_box(state))


static func _item_box(state: String) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	match state:
		"hover":
			box.bg_color = Color(0.55, 0.76, 0.36, 0.95)
			box.border_color = Color(0.88, 1.0, 0.68, 0.95)
		"pressed":
			box.bg_color = Color(0.44, 0.64, 0.29, 0.97)
			box.border_color = Color(0.88, 1.0, 0.68, 0.95)
		"disabled":
			box.bg_color = Color(0.035, 0.075, 0.045, 0.48)
			box.border_color = Color(ITEM_BAR_EDGE, 0.22)
		"focus":
			box.bg_color = ITEM_BAR
			box.border_color = Color(0.88, 0.78, 0.49, 0.8)
		_:
			box.bg_color = ITEM_BAR
			box.border_color = ITEM_BAR_EDGE
	box.border_width_top = 1
	box.border_width_left = 3
	box.content_margin_left = 14.0
	box.content_margin_right = 14.0
	box.content_margin_top = 7.0
	box.content_margin_bottom = 7.0
	return box


func _reposition() -> void:
	if anchor_button == null:
		return
	var rows := maxi(1, item_buttons.size())
	var height := CONTENT_MARGIN * 2.0 + float(rows) * ROW_HEIGHT + float(rows - 1) * ROW_SEPARATION
	var anchor_rect := anchor_button.get_global_rect()
	# The flyout is a sibling of the bar buttons inside the full-rect Center
	# node, so global and local coordinates coincide; clamp horizontally so a
	# corner button's flyout never leaves the viewport (REF-04 opens flush-left).
	var x := anchor_rect.get_center().x - PANEL_WIDTH * 0.5
	var viewport_width := get_viewport_rect().size.x
	x = clampf(x, 10.0, maxf(10.0, viewport_width - PANEL_WIDTH - 10.0))
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
			20.0, maxf(20.0, size.x - 20.0)
		)
	draw_colored_polygon(PackedVector2Array([
		bottom_center + Vector2(-9.0, 0.0), bottom_center + Vector2(9.0, 0.0),
		bottom_center + Vector2(0.0, 7.0),
	]), ORNAMENT_DIM)
