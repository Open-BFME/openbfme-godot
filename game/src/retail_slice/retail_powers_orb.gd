class_name RetailPowersOrb
extends Control
## The retail spellbook palantir: the green orb "powers" screen.
##
## Layout is authored, not invented: icons sit in tier rows (5/10/15/25
## points) ordered by the document's authored palantir purchase slots, and the
## prerequisite forks come from the sim's doc-derived tree state. The orb
## sphere, socket rings, glow, and RESET/ACCEPT chrome are STYLED UI — the
## selected pack ships the 12 spellbook icon crops but no powers-screen
## orb/sphere art, so nothing here is presented as retail art. Power icons
## themselves are the converted retail crops bound by the HUD.
##
## The orb is state-dumb: refresh_state() consumes the sim's
## spellbook_ui_state() dictionary and never derives tree logic itself.

signal power_purchase_requested(power_id: String, cost: int)
signal power_cast_requested(power_id: String)
signal powers_reset_requested
signal powers_accepted
signal feedback_requested(text: String, is_error: bool)

const POWER_BUTTON_SIZE := Vector2(96, 96)
const POWER_ICON_MAX := 76.0
const BADGE_SIZE := Vector2(26, 26)
# Retail palantir geometry (fractions of sphere diameter D, center-relative):
# header above the tier rows, four rows, action buttons below the sphere.
const HEADER_Y := -0.405
const FIRST_ROW_Y := -0.235
const ROW_SPACING := 0.186
const COLUMN_SPACING := 0.29
const BUTTONS_Y := 0.475
const SPHERE_HEIGHT_FRACTION := 0.84
const SPHERE_WIDTH_FRACTION := 0.60
const COLOR_LOCKED_ICON := Color(0.42, 0.46, 0.42, 0.85)
const COLOR_LOCKED_RING := Color(0.22, 0.32, 0.22, 0.55)
const COLOR_PURCHASABLE_RING := Color(0.35, 0.95, 0.35, 0.95)
const COLOR_OWNED_ICON := Color(1.05, 1.08, 0.92)
const COLOR_OWNED_RING := Color(0.75, 0.85, 0.35, 0.9)
const COLOR_GOLD := Color("e9d489")
const COLOR_HEADER := Color(0.62, 0.95, 0.55)

var power_buttons: Array[Button] = []
var reset_button: Button
var accept_button: Button
var power_points_value: Label
var status_label: Label

var _rows: Array = []
var _row_by_power: Dictionary = {}
var _state: Dictionary = {}
var _font: Font
var _sphere: Control
var _tree_layer: Control
var _connectors: Control
var _pulse_clock := 0.0


func _init() -> void:
	name = "RetailPowersOrb"
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	visible = false


func _ready() -> void:
	resized.connect(_relayout)


func configure(rows: Array, retail_font: Font = null) -> void:
	## rows: [{power_id, icon_id, cost, purchase_slot, label, tooltip}]. Icons
	## are bound later via set_power_icon once the HUD validates pack art.
	_font = retail_font
	_rows = rows.duplicate(true)
	_row_by_power.clear()
	for row_value in _rows:
		_row_by_power[String((row_value as Dictionary).get("power_id", ""))] = row_value
	_build_chrome()
	_build_tree()
	_relayout()


func set_power_icon(power_id: String, texture: Texture2D, socket: StyleBox = null) -> void:
	for button in power_buttons:
		if String(button.get_meta("power_id", "")) != power_id:
			continue
		button.icon = texture
		if socket != null:
			for state_name in ["normal", "hover", "pressed", "disabled", "focus"]:
				button.add_theme_stylebox_override(state_name, socket)


func set_socket_style(socket: StyleBox) -> void:
	for button in power_buttons:
		for state_name in ["normal", "hover", "pressed", "disabled", "focus"]:
			button.add_theme_stylebox_override(state_name, socket)


func refresh_state(state: Dictionary) -> void:
	_state = state
	var points := int(state.get("points", 0))
	if power_points_value != null:
		power_points_value.text = "%d" % points
	var powers: Dictionary = state.get("powers", {}) as Dictionary
	for button in power_buttons:
		var power_id := String(button.get_meta("power_id", ""))
		var power_state: Dictionary = powers.get(power_id, {}) as Dictionary
		_apply_button_state(button, power_state)
	if _tree_layer != null:
		_tree_layer.queue_redraw()
	if _connectors != null:
		_connectors.queue_redraw()


func power_state(power_id: String) -> Dictionary:
	return (_state.get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary


func _build_chrome() -> void:
	for child in get_children():
		remove_child(child)
		child.free()
	power_buttons.clear()
	var backdrop := ColorRect.new()
	backdrop.name = "OrbBackdrop"
	backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	backdrop.color = Color(0.01, 0.03, 0.02, 0.88)
	backdrop.mouse_filter = Control.MOUSE_FILTER_STOP
	backdrop.gui_input.connect(func(event: InputEvent) -> void:
		# Clicking the dimmed battlefield chrome accepts and closes (retail has
		# no cancel path out of the palantir).
		if event is InputEventMouseButton and (event as InputEventMouseButton).pressed:
			powers_accepted.emit()
	)
	add_child(backdrop)
	_sphere = _OrbSphereScript.new()
	_sphere.name = "OrbSphere"
	_sphere.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_sphere)
	# Retail's spellbook orb has NO title text above the sphere (REF-22/23) —
	# only the power-points header rides there.
	var header := Label.new()
	header.name = "PowerPointsHeader"
	header.text = "POWER POINTS TO SPEND:"
	header.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	header.add_theme_font_size_override("font_size", 19)
	header.add_theme_color_override("font_color", COLOR_HEADER)
	header.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if _font != null:
		header.add_theme_font_override("font", _font)
	add_child(header)
	power_points_value = Label.new()
	power_points_value.name = "PowerPointsValue"
	power_points_value.text = "0"
	power_points_value.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	power_points_value.add_theme_font_size_override("font_size", 22)
	power_points_value.add_theme_color_override("font_color", COLOR_HEADER)
	power_points_value.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if _font != null:
		power_points_value.add_theme_font_override("font", _font)
	add_child(power_points_value)
	_connectors = _OrbConnectorsScript.new(self)
	_connectors.name = "OrbConnectors"
	_connectors.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_connectors)
	_tree_layer = _OrbTreeLayerScript.new(self)
	_tree_layer.name = "OrbTreeLayer"
	_tree_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_tree_layer)
	status_label = Label.new()
	status_label.name = "OrbStatus"
	status_label.text = ""
	status_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status_label.add_theme_font_size_override("font_size", 15)
	status_label.add_theme_color_override("font_color", Color("bfe0f2"))
	status_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if _font != null:
		status_label.add_theme_font_override("font", _font)
	add_child(status_label)
	reset_button = _chrome_button("RESET")
	reset_button.name = "OrbReset"
	reset_button.pressed.connect(func() -> void:
		powers_reset_requested.emit()
	)
	add_child(reset_button)
	accept_button = _chrome_button("ACCEPT")
	accept_button.name = "OrbAccept"
	accept_button.pressed.connect(func() -> void:
		powers_accepted.emit()
	)
	add_child(accept_button)


func _chrome_button(text: String) -> Button:
	var button := Button.new()
	button.text = text
	button.custom_minimum_size = Vector2(170, 44)
	button.add_theme_font_size_override("font_size", 20)
	# Retail spellbook buttons are green-framed with gold text (orb capture);
	# hover/press follow the same warm-glow convention as the rest of the HUD
	# (retail_hud.gd _wire_button_feel: 1.22/1.16/1.02 hover, 0.82 dip).
	button.add_theme_color_override("font_color", COLOR_GOLD)
	button.add_theme_color_override("font_hover_color", Color("fff3c2"))
	button.add_theme_color_override("font_pressed_color", Color("b8a55e"))
	if _font != null:
		button.add_theme_font_override("font", _font)
	for state_name in ["normal", "hover", "pressed"]:
		var style := StyleBoxFlat.new()
		style.bg_color = Color(0.04, 0.16, 0.05, 0.95) if state_name == "normal" else Color(0.07, 0.24, 0.08, 0.95)
		style.border_color = Color(0.55, 0.9, 0.4) if state_name == "hover" else Color(0.3, 0.7, 0.28)
		style.set_border_width_all(2)
		style.set_corner_radius_all(4)
		style.content_margin_left = 24
		style.content_margin_right = 24
		button.add_theme_stylebox_override(state_name, style)
	button.mouse_entered.connect(func() -> void:
		if not button.disabled:
			button.self_modulate = Color(1.22, 1.16, 1.02)
	)
	button.mouse_exited.connect(func() -> void:
		button.self_modulate = Color.WHITE
	)
	button.button_down.connect(func() -> void:
		button.self_modulate = Color(0.82, 0.78, 0.7)
	)
	button.button_up.connect(func() -> void:
		button.self_modulate = Color(1.22, 1.16, 1.02) if button.is_hovered() else Color.WHITE
	)
	return button


func _build_tree() -> void:
	for row_value in _rows:
		var row := row_value as Dictionary
		var power_id := String(row.get("power_id", ""))
		var button := Button.new()
		button.name = "Power%d" % int(row.get("purchase_slot", 0))
		button.size = POWER_BUTTON_SIZE
		button.expand_icon = true
		button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
		button.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
		button.add_theme_constant_override("icon_max_width", int(POWER_ICON_MAX))
		button.set_meta("power_id", power_id)
		button.set_meta("power_cost", int(row.get("cost", 0)))
		button.tooltip_text = String(row.get("tooltip", ""))
		button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
		for state_name in ["normal", "hover", "pressed", "disabled", "focus"]:
			button.add_theme_stylebox_override(state_name, StyleBoxEmpty.new())
		button.pressed.connect(_on_power_pressed.bind(power_id))
		button.mouse_entered.connect(_on_power_hover.bind(power_id))
		button.mouse_exited.connect(_on_power_hover_exit)
		add_child(button)
		var badge := Label.new()
		badge.name = "Cost"
		badge.text = "%d" % int(row.get("cost", 0))
		badge.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		badge.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		badge.add_theme_font_size_override("font_size", 15)
		badge.add_theme_color_override("font_color", COLOR_GOLD)
		var badge_style := StyleBoxFlat.new()
		badge_style.bg_color = Color(0.02, 0.1, 0.03, 0.95)
		badge_style.border_color = Color(0.35, 0.75, 0.3)
		badge_style.set_border_width_all(1)
		badge_style.set_corner_radius_all(13)
		badge.add_theme_stylebox_override("normal", badge_style)
		badge.mouse_filter = Control.MOUSE_FILTER_IGNORE
		badge.set_meta("badge", true)
		button.add_child(badge)
		var sweep := _CooldownSweepScript.new()
		sweep.name = "CooldownSweep"
		sweep.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		sweep.mouse_filter = Control.MOUSE_FILTER_IGNORE
		sweep.visible = false
		button.add_child(sweep)
		power_buttons.append(button)


func _apply_button_state(button: Button, power_state: Dictionary) -> void:
	if power_state.is_empty():
		# No sim state (doc-less fixture): keep icons, mark unavailable.
		button.self_modulate = COLOR_LOCKED_ICON
		button.disabled = false
		return
	var sweep := button.get_node_or_null("CooldownSweep")
	var owned := bool(power_state.get("owned", false))
	var purchasable := bool(power_state.get("purchasable", false))
	var cooldown: Dictionary = power_state.get("cooldown", {}) as Dictionary
	var remaining := int(cooldown.get("remaining_ticks", 0))
	button.disabled = false
	if owned:
		button.self_modulate = COLOR_OWNED_ICON
	elif purchasable:
		# Pulsed by _process through the tree layer glow; icon stays bright.
		button.self_modulate = Color(1.15, 1.25, 1.05)
	else:
		button.self_modulate = COLOR_LOCKED_ICON
	if sweep != null:
		if owned and remaining > 0:
			sweep.visible = true
			sweep.set("progress", float(cooldown.get("progress", 0.0)))
			sweep.set("remaining_seconds", float(remaining) * 0.1)
			sweep.queue_redraw()
		else:
			sweep.visible = false


func _process(delta: float) -> void:
	if not visible:
		return
	_pulse_clock += delta
	if _tree_layer != null:
		_tree_layer.queue_redraw()


func _on_power_hover(power_id: String) -> void:
	var row: Dictionary = _row_by_power.get(power_id, {}) as Dictionary
	var power_state := power_state(power_id)
	var text := String(row.get("label", power_id))
	var tooltip := String(row.get("tooltip", ""))
	if tooltip != "":
		text = "%s\n%s" % [text, tooltip]
	var reason := _state_reason_text(power_state)
	if reason != "":
		text = "%s\n%s" % [text, reason]
	status_label.text = text


func _on_power_hover_exit() -> void:
	status_label.text = ""


func _state_reason_text(power_state: Dictionary) -> String:
	if power_state.is_empty():
		return ""
	if bool(power_state.get("owned", false)):
		var cooldown: Dictionary = power_state.get("cooldown", {}) as Dictionary
		if int(cooldown.get("remaining_ticks", 0)) > 0:
			return "Recharging."
		if not bool(power_state.get("castable", false)):
			return "Not castable yet: %s" % String(power_state.get("effect_locked_reason", ""))
		return "Owned — click to cast."
	var reason := String(power_state.get("locked_reason", ""))
	match reason:
		"prerequisites-unmet":
			return "Locked: purchase the connecting powers first."
		"insufficient-power-points":
			return "Locked: not enough power points."
		"":
			return ""
		_:
			return "Locked: %s" % reason


func _on_power_pressed(power_id: String) -> void:
	var power_state := power_state(power_id)
	var row: Dictionary = _row_by_power.get(power_id, {}) as Dictionary
	var cost := int(row.get("cost", power_state.get("cost", 0)))
	if power_state.is_empty():
		feedback_requested.emit("The spellbook tree is unavailable.", true)
		return
	if bool(power_state.get("owned", false)):
		if not bool(power_state.get("castable", false)):
			feedback_requested.emit("That power is not castable yet: %s" % String(power_state.get("effect_locked_reason", "")), true)
			return
		var cooldown: Dictionary = power_state.get("cooldown", {}) as Dictionary
		var remaining := int(cooldown.get("remaining_ticks", 0))
		if remaining > 0:
			feedback_requested.emit("Power recharging (%ds)." % ceili(float(remaining) * 0.1), true)
			return
		if bool(power_state.get("needs_target_pos", false)):
			power_cast_requested.emit(power_id)
		else:
			# Instant (no-target) powers cast immediately from the orb.
			power_cast_requested.emit(power_id)
		return
	if bool(power_state.get("purchasable", false)):
		power_purchase_requested.emit(power_id, cost)
		return
	feedback_requested.emit(_state_reason_text(power_state).trim_prefix("Locked: "), true)


func _tier_groups() -> Array:
	## Tier rows: group by doc MP cost ascending (5/10/15/25), authored
	## purchase-slot order inside each tier. This reproduces the retail
	## palantir grid exactly: the authored MenSpellStoreCommandSet slots 1-12
	## with the source INI's tier breaks (commandset.ini: 1-3 / 4-7 / 8-10 /
	## 11-12) give the 3/4/3/2 rows — Heal/Rally/ElvenWood,
	## LoneTower/ArrowVolley/TomBombadil/Hobbits, Rohan/CloudBreak/Dunedain,
	## ArmyoftheDead/Earthquake — matching the retail orb capture.
	var tiers: Dictionary = {}
	var tier_order: Array = []
	for row_value in _rows:
		var row := row_value as Dictionary
		var cost := int(row.get("cost", 0))
		if not tiers.has(cost):
			tiers[cost] = []
			tier_order.append(cost)
		(tiers[cost] as Array).append(row)
	tier_order.sort()
	var groups: Array = []
	for cost in tier_order:
		var group: Array = tiers[cost]
		group.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			return int(a.get("purchase_slot", 0)) < int(b.get("purchase_slot", 0))
		)
		groups.append(group)
	return groups


func _sphere_metrics() -> Dictionary:
	var viewport := size
	if viewport.x <= 0.0 or viewport.y <= 0.0:
		viewport = Vector2(1920, 1080)
	var diameter := minf(viewport.y * SPHERE_HEIGHT_FRACTION, viewport.x * SPHERE_WIDTH_FRACTION)
	var center := Vector2(viewport.x * 0.5, viewport.y * 0.52)
	return {"center": center, "diameter": diameter}


func power_anchor(power_id: String) -> Vector2:
	for button in power_buttons:
		if String(button.get_meta("power_id", "")) == power_id:
			return button.position + button.size * 0.5
	return Vector2.ZERO


func _relayout() -> void:
	if _sphere == null:
		return
	var metrics := _sphere_metrics()
	var center: Vector2 = metrics["center"]
	var diameter: float = metrics["diameter"]
	_sphere.position = center - Vector2(diameter, diameter) * 0.5
	_sphere.size = Vector2(diameter, diameter)
	_sphere.queue_redraw()
	_connectors.position = Vector2.ZERO
	_connectors.size = size
	_tree_layer.position = Vector2.ZERO
	_tree_layer.size = size
	var header := get_node_or_null("PowerPointsHeader") as Label
	if header != null:
		header.position = center + Vector2(-260.0, diameter * HEADER_Y)
		header.size = Vector2(520, 28)
	if power_points_value != null:
		power_points_value.position = center + Vector2(-260.0, diameter * (HEADER_Y + 0.045))
		power_points_value.size = Vector2(520, 30)
	var groups := _tier_groups()
	var button_index := 0
	for tier_index in groups.size():
		var group: Array = groups[tier_index]
		var row_y: float = center.y + diameter * (FIRST_ROW_Y + ROW_SPACING * float(tier_index))
		for column_index in group.size():
			if button_index >= power_buttons.size():
				break
			var column_offset := (float(column_index) - float(group.size() - 1) * 0.5) * COLUMN_SPACING
			var button := power_buttons[button_index]
			button.position = Vector2(center.x + diameter * column_offset, row_y) - button.size * 0.5
			var badge := button.get_node_or_null("Cost") as Label
			if badge != null:
				badge.position = Vector2((button.size.x - BADGE_SIZE.x) * 0.5, button.size.y - BADGE_SIZE.y * 0.62)
				badge.size = BADGE_SIZE
			button_index += 1
	if reset_button != null and accept_button != null:
		var button_y: float = center.y + diameter * BUTTONS_Y
		reset_button.position = Vector2(center.x - reset_button.custom_minimum_size.x - 14.0, button_y)
		accept_button.position = Vector2(center.x + 14.0, button_y)
	if status_label != null:
		status_label.position = Vector2(center.x - 320.0, center.y + diameter * BUTTONS_Y + 56.0)
		status_label.size = Vector2(640, 60)
	if _connectors != null:
		_connectors.queue_redraw()
	if _tree_layer != null:
		_tree_layer.queue_redraw()


## --- Styled chrome drawing helpers (UI only, not retail art) ---

class _OrbSphereScript:
	extends Control
	## Styled green palantir sphere: concentric shaded circles + a specular
	## highlight. The pack ships no powers-screen orb art; this is chrome.
	func _draw() -> void:
		var radius := size.x * 0.5
		var center := size * 0.5
		draw_circle(center, radius, Color(0.02, 0.06, 0.025, 0.96))
		var steps := 28
		for step in range(steps, 0, -1):
			var t := float(step) / float(steps)
			var shade := Color(0.03 + 0.10 * (1.0 - t), 0.10 + 0.30 * (1.0 - t), 0.04 + 0.08 * (1.0 - t), 0.95)
			draw_circle(center, radius * t, shade)
		var rim := Color(0.30, 0.75, 0.28, 0.9)
		draw_arc(center, radius - 2.0, 0.0, TAU, 128, rim, 3.0, true)
		draw_arc(center, radius * 0.965, 0.0, TAU, 128, Color(0.12, 0.4, 0.12, 0.8), 6.0, true)
		# Specular highlight upper-left, like the retail render.
		var highlight_center := center + Vector2(-radius * 0.38, -radius * 0.42)
		for step in range(10, 0, -1):
			var t := float(step) / 10.0
			draw_circle(highlight_center, radius * 0.16 * t, Color(0.55, 0.95, 0.5, 0.05 * (1.0 - t) + 0.01))


class _OrbConnectorsScript:
	extends Control
	## Prerequisite forks: from each prerequisite power's badge down/out to the
	## dependent power. Bright when the dependent is owned/purchasable.
	var _orb: RetailPowersOrb

	func _init(orb: RetailPowersOrb) -> void:
		_orb = orb

	func _draw() -> void:
		var powers: Dictionary = (_orb._state.get("powers", {}) as Dictionary)
		for power_id_value in powers.keys():
			var power_id := String(power_id_value)
			var power_state: Dictionary = powers[power_id]
			for prereq_value in power_state.get("prereq_power_ids", []) as Array:
				var prereq_id := String(prereq_value)
				var from_point := _orb.power_anchor(prereq_id)
				var to_point := _orb.power_anchor(power_id)
				if from_point == Vector2.ZERO or to_point == Vector2.ZERO:
					continue
				var active := bool(power_state.get("owned", false)) or bool(power_state.get("purchasable", false))
				var prereq_owned := bool((powers.get(prereq_id, {}) as Dictionary).get("owned", false))
				var color := Color(0.35, 0.9, 0.35, 0.85) if active else Color(0.2, 0.45, 0.2, 0.5)
				if prereq_owned and active:
					color = Color(0.55, 1.0, 0.45, 0.95)
				var from_edge := from_point + Vector2(0, 16)
				var to_edge := to_point + Vector2(0, -30)
				var mid := Vector2((from_edge.x + to_edge.x) * 0.5, (from_edge.y + to_edge.y) * 0.5)
				draw_line(from_edge, mid, color, 2.0, true)
				draw_line(mid, to_edge, color, 2.0, true)


class _OrbTreeLayerScript:
	extends Control
	## Socket rings + the purchasable glow behind each power icon.
	var _orb: RetailPowersOrb

	func _init(orb: RetailPowersOrb) -> void:
		_orb = orb

	func _draw() -> void:
		var powers: Dictionary = (_orb._state.get("powers", {}) as Dictionary)
		for button in _orb.power_buttons:
			var power_id := String(button.get_meta("power_id", ""))
			var power_state: Dictionary = powers.get(power_id, {}) as Dictionary
			var center := button.position + button.size * 0.5
			var ring_radius := button.size.x * 0.52
			var ring_color := RetailPowersOrb.COLOR_LOCKED_RING
			if bool(power_state.get("owned", false)):
				ring_color = RetailPowersOrb.COLOR_OWNED_RING
			elif bool(power_state.get("purchasable", false)):
				ring_color = RetailPowersOrb.COLOR_PURCHASABLE_RING
				# Pulsing glow for purchasable picks (retail's "spend me" cue).
				var pulse := 0.5 + 0.28 * sin(_orb._pulse_clock * 3.2 + float(_orb.power_buttons.find(button)))
				draw_circle(center, ring_radius + 6.0, Color(0.3, 0.95, 0.3, 0.22 * pulse))
				draw_circle(center, ring_radius + 2.0, Color(0.35, 1.0, 0.35, 0.30 * pulse))
			draw_arc(center, ring_radius, 0.0, TAU, 64, ring_color, 2.5, true)


class _CooldownSweepScript:
	extends Control
	## Dark radial sweep over an owned power while its reload runs.
	var progress := 1.0
	var remaining_seconds := 0.0

	func _draw() -> void:
		var center := size * 0.5
		var radius := size.x * 0.52
		var remaining_fraction := clampf(1.0 - progress, 0.0, 1.0)
		if remaining_fraction > 0.0:
			# Sector from straight up, sweeping clockwise by the undone fraction.
			var start_angle := -PI * 0.5 - TAU * remaining_fraction
			var points := PackedVector2Array([center])
			var steps := 24
			for step in range(steps + 1):
				var angle := start_angle + TAU * remaining_fraction * (float(step) / float(steps))
				points.append(center + Vector2(cos(angle), sin(angle)) * radius)
			draw_colored_polygon(points, Color(0.0, 0.05, 0.0, 0.72))
		if remaining_seconds > 0.5:
			var font := ThemeDB.fallback_font
			var text := "%d" % ceili(remaining_seconds)
			var text_size := font.get_string_size(text, HORIZONTAL_ALIGNMENT_CENTER, -1, 22)
			draw_string(font, center - Vector2(text_size.x * 0.5, -text_size.y * 0.3), text, HORIZONTAL_ALIGNMENT_CENTER, -1, 22, Color(0.8, 1.0, 0.8))
