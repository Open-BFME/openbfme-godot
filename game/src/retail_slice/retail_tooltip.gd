class_name RetailTooltip
extends Control
## Retail 1.06 command tooltip. Renders a gold-framed, near-black panel with a
## centered bold title, a "Cost: N ... Shortcut: X" treasure row (either side
## hidden when its source is absent), and one or two description lines. Colors
## are sampled from the retail palette already used by retail_hud.gd (dark
## #0a0a08 background, gold #d9c996 / #f1d06e accents). Text never invents
## lore: callers pass strings sourced from the pack's strings.json (or the
## spec fallbacks).

const BG_COLOR := Color("0a0a08")
const BORDER_COLOR := Color("d9c996")
const TITLE_COLOR := Color("f1d06e")
const COST_COLOR := Color("f1d06e")
const SHORTCUT_COLOR := Color("c7b98a")
const BODY_COLOR := Color("d7c9a0")
const TREASURE_COLOR := Color("d6aa55")
const MAX_PANEL_WIDTH := 420.0

var _panel: PanelContainer
var _column: VBoxContainer
var _header_row: HBoxContainer
var _title_label: Label
var _shortcut_label: Label
var _cost_row: HBoxContainer
var _cost_icon: Label
var _cost_label: Label
var _command_points_label: Label
var _description_label: Label
var _font: FontFile


func _ready() -> void:
	if _panel == null:
		_build()


func _build() -> void:
	name = "RetailTooltip"
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	visible = false
	z_index = 40
	_panel = PanelContainer.new()
	_panel.name = "TooltipPanel"
	_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var box := StyleBoxFlat.new()
	box.bg_color = BG_COLOR
	box.border_color = BORDER_COLOR
	box.set_border_width_all(2)
	box.set_corner_radius_all(3)
	box.content_margin_left = 12
	box.content_margin_right = 12
	box.content_margin_top = 9
	box.content_margin_bottom = 9
	box.shadow_color = Color(0, 0, 0, 0.7)
	box.shadow_size = 6
	_panel.add_theme_stylebox_override("panel", box)
	add_child(_panel)

	_column = VBoxContainer.new()
	_column.add_theme_constant_override("separation", 4)
	_column.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_panel.add_child(_column)

	_header_row = HBoxContainer.new()
	_header_row.add_theme_constant_override("separation", 22)
	_header_row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_column.add_child(_header_row)

	_title_label = Label.new()
	_title_label.name = "TooltipTitle"
	_title_label.add_theme_color_override("font_color", TITLE_COLOR)
	_title_label.add_theme_font_size_override("font_size", 18)
	_title_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_header_row.add_child(_title_label)

	# Retail row two: "◆ Cost: N" pinned left, "Shortcut: X" pinned right.
	_cost_row = HBoxContainer.new()
	_cost_row.name = "TooltipCostRow"
	_cost_row.add_theme_constant_override("separation", 6)
	_cost_row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_column.add_child(_cost_row)
	_cost_icon = Label.new()
	_cost_icon.text = "◆"
	_cost_icon.add_theme_color_override("font_color", TREASURE_COLOR)
	_cost_icon.add_theme_font_size_override("font_size", 16)
	_cost_row.add_child(_cost_icon)
	_cost_label = Label.new()
	_cost_label.name = "TooltipCost"
	_cost_label.add_theme_color_override("font_color", COST_COLOR)
	_cost_label.add_theme_font_size_override("font_size", 15)
	_cost_row.add_child(_cost_label)
	var spacer := Control.new()
	spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_cost_row.add_child(spacer)
	_shortcut_label = Label.new()
	_shortcut_label.name = "TooltipShortcut"
	_shortcut_label.add_theme_color_override("font_color", SHORTCUT_COLOR)
	_shortcut_label.add_theme_font_size_override("font_size", 14)
	_shortcut_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_shortcut_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_cost_row.add_child(_shortcut_label)

	# Retail row three (unit commands only): "Command Points: N", centered.
	_command_points_label = Label.new()
	_command_points_label.name = "TooltipCommandPoints"
	_command_points_label.add_theme_color_override("font_color", COST_COLOR)
	_command_points_label.add_theme_font_size_override("font_size", 15)
	_command_points_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_command_points_label.visible = false
	_column.add_child(_command_points_label)

	_description_label = Label.new()
	_description_label.name = "TooltipDescription"
	_description_label.add_theme_color_override("font_color", BODY_COLOR)
	_description_label.add_theme_font_size_override("font_size", 14)
	_description_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_description_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_description_label.custom_minimum_size.x = MAX_PANEL_WIDTH - 24.0
	# Retail tooltips run long effect lists (Banners five lines, upgrade level
	# effects) — never clamp the line count.
	_column.add_child(_description_label)


func set_retail_font(font: FontFile) -> void:
	if _panel == null:
		_build()
	_font = font
	if font == null:
		return
	for label in [_title_label, _shortcut_label, _cost_label, _command_points_label, _description_label]:
		(label as Label).add_theme_font_override("font", font)


## title: bold command name. cost: -1 to hide the cost row, otherwise the exact
## value passed by the HUD (never invented here). shortcut: "" to hide.
## description: pack description text, any number of lines.
## command_points: -1 to hide the retail "Command Points: N" row.
func show_content(title: String, cost: int, shortcut: String, description: String, command_points: int = -1) -> void:
	if _panel == null:
		_build()
	# Retail strings carry "&" hotkey markers (e.g. "Build Archer&ty Range");
	# the runtime strips the marker like the retail text layout does.
	_title_label.text = _strip_hotkey_markers(title)
	_shortcut_label.text = "Shortcut: %s" % shortcut if shortcut != "" else ""
	_shortcut_label.visible = shortcut != ""
	_cost_row.visible = cost >= 0
	_cost_label.text = "Cost: %d" % cost if cost >= 0 else ""
	_command_points_label.text = "Command Points: %d" % command_points if command_points > 0 else ""
	_command_points_label.visible = command_points > 0
	_description_label.text = _strip_hotkey_markers(description)
	_description_label.visible = description != ""
	visible = true


static func _strip_hotkey_markers(text: String) -> String:
	return text.replace("&&", "\u0001").replace("&", "").replace("\u0001", "&")


## Retail "Shortcut: X" letter: the hotkey marker in the localized label
## ("Build Fa&rm" -> R, "Gondor &Tower Guards" -> T, "Research Hea&vy Armor"
## -> V). Returns "" when the label has no marker. Never invented.
static func extract_hotkey_letter(label: String) -> String:
	var index := 0
	while index < label.length():
		var found := label.find("&", index)
		if found < 0:
			return ""
		if found + 1 < label.length() and label[found + 1] == "&":
			index = found + 2
			continue
		if found + 1 >= label.length():
			return ""
		return label[found + 1].to_upper()
	return ""


func hide_tooltip() -> void:
	visible = false


func title_text() -> String:
	return _title_label.text if _title_label != null else ""


func cost_text() -> String:
	return _cost_label.text if _cost_label != null else ""


func cost_row_visible() -> bool:
	return _cost_row != null and _cost_row.visible


func description_text() -> String:
	return _description_label.text if _description_label != null else ""


## Places the panel above the bottom control bar and clamps it inside the
## screen so it can never render off-screen.
func place_above_bar(anchor_center_x: float, bar_top_y: float, screen_size: Vector2) -> void:
	if _panel == null:
		_build()
	_panel.reset_size()
	var panel_size := _panel.get_combined_minimum_size()
	var pos := Vector2(anchor_center_x - panel_size.x * 0.5, bar_top_y - panel_size.y - 8.0)
	pos.x = clampf(pos.x, 8.0, maxf(8.0, screen_size.x - panel_size.x - 8.0))
	pos.y = clampf(pos.y, 8.0, maxf(8.0, screen_size.y - panel_size.y - 8.0))
	position = pos
	_panel.position = Vector2.ZERO


## Retail 1.06 anchor: the tooltip panel sits at a fixed spot bottom-center
## (REF-25/32/41 all show it centered ~45% width, top edge ~53.5% height)
## regardless of which command is hovered.
func place_retail_anchor(screen_size: Vector2) -> void:
	if _panel == null:
		_build()
	_panel.reset_size()
	var panel_size := _panel.get_combined_minimum_size()
	var pos := Vector2(screen_size.x * 0.448 - panel_size.x * 0.5, screen_size.y * 0.535)
	pos.x = clampf(pos.x, 8.0, maxf(8.0, screen_size.x - panel_size.x - 8.0))
	pos.y = clampf(pos.y, 8.0, maxf(8.0, screen_size.y - panel_size.y - 8.0))
	position = pos
	_panel.position = Vector2.ZERO
