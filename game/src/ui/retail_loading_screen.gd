class_name RetailLoadingScreen
extends CanvasLayer
## Retail-style match loading screen (reference/INDEX.md REF-08): ornate dark
## green frame, LOADING title, map preview left, map description center, a
## player table (name/level/team/army/progress), and a real progress readout
## driven by the match load pipeline. Built programmatically so the scene file
## stays a one-node shell and the slice can adopt or spawn it identically.
##
## Progress contract: set_load_progress() is fed by the actual load phases /
## asset queue of the retail slice — the bar is monotonic and never simulated.
## Both player rows mirror the same real match-load ratio (this machine loads
## both armies' content in one pipeline, exactly like retail's peer progress).

const GROUP_NAME := "retail_loading_screen"

const COLOR_BACKDROP := Color(0.016, 0.027, 0.024)
const COLOR_PANEL := Color(0.035, 0.051, 0.045)
const COLOR_FRAME := Color(0.23, 0.30, 0.18)
const COLOR_FRAME_INNER := Color(0.42, 0.52, 0.30)
const COLOR_TITLE := Color(0.85, 0.79, 0.35)
const COLOR_TEXT := Color(0.81, 0.88, 0.78)
const COLOR_TEXT_DIM := Color(0.62, 0.70, 0.63)
const COLOR_BAR_FILL := Color(0.42, 0.56, 0.24)
const COLOR_PLAYER_BLUE := Color(0.36, 0.49, 0.79)
const COLOR_ENEMY_RED := Color(0.75, 0.22, 0.17)

var _map_title_label: Label
var _description_label: Label
var _art_rect: TextureRect
var _preview_rect: TextureRect
var _art_frame: PanelContainer
var _preview_frame: PanelContainer
var _table_grid: GridContainer
var _phase_label: Label
var _rows: Array[Dictionary] = []
var _progress_ratio := 0.0
var _fading := false


func _ready() -> void:
	layer = 50
	add_to_group(GROUP_NAME)
	_build_ui()
	set_load_progress(_progress_ratio, "")


func configure_identity(identity: Dictionary) -> void:
	## Map title/description/art plus the player rows. Textures arrive already
	## loaded by the caller (pack art is validated content there, not here).
	var map_name := String(identity.get("map_name", ""))
	if map_name != "":
		_map_title_label.text = map_name
	var description := String(identity.get("description", ""))
	_description_label.text = description
	_description_label.visible = description != ""
	var art: Texture2D = identity.get("art")
	var preview: Texture2D = identity.get("preview")
	if art != null:
		_art_rect.texture = art
		_art_frame.visible = true
	if preview != null:
		_preview_rect.texture = preview
		_preview_frame.visible = true
	var players: Array = identity.get("players", [])
	if not players.is_empty():
		_build_player_rows(players)


func configure_boot(title: String) -> void:
	## APPLICATION STARTUP shape of the same screen. The match shape below wants
	## a map plate, a description and a per-player progress table; at app startup
	## none of those exist yet - ContentDB has not been asked for a map and there
	## are no players - so rendering the empty five-column header would be a UI
	## claiming to describe a match that has not been chosen.
	##
	## This keeps the identical ornate frame and progress readout and drops the
	## match-only furniture, so the startup screen and the match screen are
	## visibly the same surface rather than two separate loading screens.
	## When a splash still is present it becomes the plate art for boot only.
	_map_title_label.text = title
	_description_label.text = "Open-source RTS · content stays on your machine"
	_description_label.visible = true
	_preview_frame.visible = false
	var splash := _load_boot_splash_texture()
	if splash != null:
		_art_rect.texture = splash
		_art_frame.visible = true
	else:
		_art_frame.visible = false
	for child in _table_grid.get_children():
		child.queue_free()
	_rows.clear()
	_table_grid.visible = false
	# One shared bar for the startup phase, built in the table's place so
	# set_load_progress() drives startup and match loads through one code path.
	var host := HBoxContainer.new()
	host.alignment = BoxContainer.ALIGNMENT_CENTER
	host.add_theme_constant_override("separation", 10)
	var bar := ProgressBar.new()
	bar.custom_minimum_size = Vector2(520, 20)
	bar.min_value = 0.0
	bar.max_value = 100.0
	bar.value = _progress_ratio * 100.0
	bar.show_percentage = false
	bar.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	var bar_bg := StyleBoxFlat.new()
	bar_bg.bg_color = Color(0.05, 0.07, 0.05)
	bar_bg.border_color = COLOR_FRAME_INNER
	bar_bg.set_border_width_all(1)
	var bar_fill := StyleBoxFlat.new()
	bar_fill.bg_color = COLOR_BAR_FILL
	bar.add_theme_stylebox_override("background", bar_bg)
	bar.add_theme_stylebox_override("fill", bar_fill)
	host.add_child(bar)
	var percent := _table_label("0%", COLOR_TITLE, 15)
	host.add_child(percent)
	(_table_grid.get_parent() as CenterContainer).add_child(host)
	_rows.append({"bar": bar, "percent": percent})
	set_load_progress(_progress_ratio, "")


func _load_boot_splash_texture() -> Texture2D:
	## Optional authored splash / ring plate. Prefer res:// then absolute path.
	for path in [
		"res://data/base/assets/ui/menu/splash_ring.png",
		"res://icon_ring.png",
	]:
		if ResourceLoader.exists(path):
			var texture := load(path) as Texture2D
			if texture != null:
				return texture
		var absolute := ProjectSettings.globalize_path(path)
		if FileAccess.file_exists(absolute):
			var image := Image.load_from_file(absolute)
			if image != null and not image.is_empty():
				return ImageTexture.create_from_image(image)
	return null


func set_load_progress(ratio: float, phase_label: String) -> void:
	# Monotonic by construction: a later phase can never move the bar backward
	# (scene bootstrap hands its floor over to the slice's phase weights).
	_progress_ratio = maxf(_progress_ratio, clampf(ratio, 0.0, 1.0))
	for row in _rows:
		(row["bar"] as ProgressBar).value = _progress_ratio * 100.0
		(row["percent"] as Label).text = "%d%%" % int(round(_progress_ratio * 100.0))
	if phase_label != "":
		_phase_label.text = phase_label


func get_load_progress() -> float:
	return _progress_ratio


func fade_out_and_free() -> void:
	if _fading:
		return
	_fading = true
	# This node is a CanvasLayer, which has no `modulate` — tweening it raised
	# "the tweened property does not exist" on every launch and the screen
	# vanished instantly instead of fading. Fade the CanvasItem children in
	# parallel, which is where the visible content actually lives.
	var faded: Array[CanvasItem] = []
	for child in get_children():
		if child is CanvasItem:
			faded.append(child as CanvasItem)
	var tween := create_tween()
	tween.tween_interval(0.15)
	if faded.is_empty():
		tween.tween_callback(queue_free)
		return
	tween.set_parallel(true)
	for item in faded:
		tween.tween_property(item, "modulate", Color(1, 1, 1, 0), 0.35)
	tween.set_parallel(false)
	tween.tween_callback(queue_free)


func _build_ui() -> void:
	var backdrop := ColorRect.new()
	backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	backdrop.color = COLOR_BACKDROP
	add_child(backdrop)
	# Ornate retail frame: outer border, inset hairline border, corner diamonds.
	var outer := _border_panel(COLOR_FRAME, 4)
	outer.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	backdrop.add_child(outer)
	var inner := _border_panel(COLOR_FRAME_INNER, 1)
	inner.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT, Control.PRESET_MODE_MINSIZE, 10)
	backdrop.add_child(inner)
	for corner in [Vector2(10, 10), Vector2(-10, 10), Vector2(10, -10), Vector2(-10, -10)]:
		var diamond := Panel.new()
		diamond.custom_minimum_size = Vector2(16, 16)
		diamond.size = Vector2(16, 16)
		diamond.rotation = PI / 4.0
		var style := StyleBoxFlat.new()
		style.bg_color = COLOR_FRAME_INNER
		diamond.add_theme_stylebox_override("panel", style)
		var anchor := Control.PRESET_TOP_LEFT
		if corner.x < 0 and corner.y < 0:
			anchor = Control.PRESET_BOTTOM_RIGHT
		elif corner.x < 0:
			anchor = Control.PRESET_TOP_RIGHT
		elif corner.y < 0:
			anchor = Control.PRESET_BOTTOM_LEFT
		diamond.set_anchors_preset(anchor)
		var sign := Vector2(signf(corner.x), signf(corner.y))
		diamond.position = Vector2(18, 18) * sign - Vector2(8, 8)
		backdrop.add_child(diamond)

	var column := VBoxContainer.new()
	column.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT, Control.PRESET_MODE_MINSIZE, 24)
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	column.add_theme_constant_override("separation", 16)
	backdrop.add_child(column)

	var title := Label.new()
	title.text = "LOADING"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 40)
	title.add_theme_color_override("font_color", COLOR_TITLE)
	column.add_child(title)

	_map_title_label = Label.new()
	_map_title_label.text = ""
	_map_title_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_map_title_label.add_theme_font_size_override("font_size", 24)
	_map_title_label.add_theme_color_override("font_color", COLOR_TEXT)
	column.add_child(_map_title_label)

	var middle := HBoxContainer.new()
	middle.alignment = BoxContainer.ALIGNMENT_CENTER
	middle.add_theme_constant_override("separation", 24)
	column.add_child(middle)
	_art_frame = _framed_texture(360, 240)
	_art_rect = _art_frame.get_meta("rect")
	_art_frame.visible = false
	middle.add_child(_art_frame)
	_description_label = Label.new()
	_description_label.custom_minimum_size = Vector2(520, 240)
	_description_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_description_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_description_label.add_theme_font_size_override("font_size", 17)
	_description_label.add_theme_color_override("font_color", COLOR_TEXT)
	_description_label.visible = false
	middle.add_child(_description_label)
	_preview_frame = _framed_texture(360, 240)
	_preview_rect = _preview_frame.get_meta("rect")
	_preview_frame.visible = false
	middle.add_child(_preview_frame)

	_table_grid = GridContainer.new()
	_table_grid.columns = 5
	_table_grid.add_theme_constant_override("h_separation", 28)
	_table_grid.add_theme_constant_override("v_separation", 8)
	var table_center := CenterContainer.new()
	table_center.custom_minimum_size = Vector2(0, 200)
	table_center.add_child(_table_grid)
	column.add_child(table_center)
	for header in ["Player Name", "Level", "Team", "Army", "Progress"]:
		var header_label := _table_label(header, COLOR_TEXT_DIM, 16)
		header_label.custom_minimum_size = _column_width(header)
		_table_grid.add_child(header_label)

	_phase_label = Label.new()
	_phase_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_phase_label.add_theme_font_size_override("font_size", 15)
	_phase_label.add_theme_color_override("font_color", COLOR_TEXT_DIM)
	column.add_child(_phase_label)


func _build_player_rows(players: Array) -> void:
	for row in _rows:
		(row["bar"] as ProgressBar).queue_free()
	for child in _table_grid.get_children():
		if child is Label and (child as Label).text in ["Player Name", "Level", "Team", "Army", "Progress"]:
			continue
		child.queue_free()
	_rows.clear()
	for player_value in players:
		var player: Dictionary = player_value
		var name_color: Color = player.get("color", COLOR_PLAYER_BLUE)
		var name_label := _table_label(String(player.get("name", "Player")), name_color, 17)
		name_label.custom_minimum_size = _column_width("Player Name")
		_table_grid.add_child(name_label)
		var level_badge := PanelContainer.new()
		level_badge.custom_minimum_size = Vector2(28, 28)
		var badge_style := StyleBoxFlat.new()
		badge_style.bg_color = name_color
		badge_style.set_border_width_all(1)
		badge_style.border_color = COLOR_FRAME_INNER
		level_badge.add_theme_stylebox_override("panel", badge_style)
		var badge_text := Label.new()
		badge_text.text = str(int(player.get("level", 1)))
		badge_text.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		badge_text.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		badge_text.add_theme_color_override("font_color", Color.WHITE)
		badge_text.add_theme_font_size_override("font_size", 15)
		level_badge.add_child(badge_text)
		var level_cell := CenterContainer.new()
		level_cell.custom_minimum_size = _column_width("Level")
		level_cell.add_child(level_badge)
		_table_grid.add_child(level_cell)
		var team_label := _table_label(String(player.get("team", "-")), COLOR_TEXT_DIM, 16)
		team_label.custom_minimum_size = _column_width("Team")
		_table_grid.add_child(team_label)
		var army_label := _table_label(String(player.get("army", "")), name_color, 17)
		army_label.custom_minimum_size = _column_width("Army")
		_table_grid.add_child(army_label)
		var progress_cell := HBoxContainer.new()
		progress_cell.custom_minimum_size = _column_width("Progress")
		progress_cell.add_theme_constant_override("separation", 10)
		var bar := ProgressBar.new()
		bar.custom_minimum_size = Vector2(200, 18)
		bar.min_value = 0.0
		bar.max_value = 100.0
		bar.value = _progress_ratio * 100.0
		bar.show_percentage = false
		bar.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		var bar_bg := StyleBoxFlat.new()
		bar_bg.bg_color = Color(0.05, 0.07, 0.05)
		bar_bg.border_color = COLOR_FRAME_INNER
		bar_bg.set_border_width_all(1)
		var bar_fill := StyleBoxFlat.new()
		bar_fill.bg_color = COLOR_BAR_FILL
		bar.add_theme_stylebox_override("background", bar_bg)
		bar.add_theme_stylebox_override("fill", bar_fill)
		progress_cell.add_child(bar)
		var percent := _table_label("0%", COLOR_TITLE, 15)
		progress_cell.add_child(percent)
		_table_grid.add_child(progress_cell)
		_rows.append({"bar": bar, "percent": percent})
	# Apply whatever progress existed before the rows were built.
	set_load_progress(_progress_ratio, "")


func _framed_texture(width: int, height: int) -> PanelContainer:
	var frame := PanelContainer.new()
	frame.custom_minimum_size = Vector2(width, height)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.008, 0.012, 0.011)
	style.border_color = COLOR_FRAME_INNER
	style.set_border_width_all(2)
	style.content_margin_left = 4
	style.content_margin_right = 4
	style.content_margin_top = 4
	style.content_margin_bottom = 4
	frame.add_theme_stylebox_override("panel", style)
	var rect := TextureRect.new()
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	frame.add_child(rect)
	frame.set_meta("rect", rect)
	return frame


func _border_panel(color: Color, width: int) -> Panel:
	var panel := Panel.new()
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0, 0, 0, 0)
	style.draw_center = false
	style.border_color = color
	style.set_border_width_all(width)
	panel.add_theme_stylebox_override("panel", style)
	return panel


func _table_label(text: String, color: Color, size: int) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", color)
	return label


func _column_width(column_name: String) -> Vector2:
	match column_name:
		"Player Name":
			return Vector2(240, 30)
		"Level":
			return Vector2(90, 30)
		"Team":
			return Vector2(90, 30)
		"Army":
			return Vector2(220, 30)
		_:
			return Vector2(300, 30)


static func signf(value: float) -> float:
	return -1.0 if value < 0.0 else 1.0
