extends Panel

## THE WAR OF THE RING STRATEGIC SCREEN: the map, the armies, and the one button
## that starts a battle.
##
## It draws RETAIL'S OWN 3D MAP when one has been converted. `livingmap.w3d` -
## the whole of Middle-earth as 64 sub-objects with retail's compiled textures -
## decodes through this project's W3D scanner with no unsupported chunks, and
## `openbfme_importer.livingmap_bundle` turns it into the bundle
## `wotr_map_bundle.gd` loads. Regions sit on that mesh at their AUTHORED world
## coordinates, because the document's `centerPoint` values and the map's
## vertices are the same coordinate space at scale 1 - measured, and the
## measurement travels in the bundle manifest.
##
## With no bundle converted the screen falls back to the flat 2D region graph it
## has always drawn, and SAYS which one the owner is looking at. A 2D graph
## labelled as one is honest; a 2D graph presented as retail's map is not.
##
## Still missing from retail's presentation, and named rather than faked: the
## `LivingWorldUI.apt` shell, army models walking between regions, the turn-phase
## banner, and retail's ambient animation (the Eye of Sauron, circling eagles and
## fellbeasts, drifting cloud borders).
##
## THREE RULES IT KEEPS:
##
## 1. NOTHING ON SCREEN IS INVENTED. A region retail did not give a custom centre
##    point (BFME2 authors exactly one: Rhun) is NOT placed at a plausible
##    coordinate - it is listed separately and labelled. The battlefield a battle
##    is fought on is labelled a stand-in, because it is one. A map sub-object
##    whose texture did not resolve is drawn flat grey, never with a substitute.
##
## 2. SELECTION IS PRESENTATION UNTIL IT IS COMMITTED. `selected_region`,
##    `selected_target` and the hover highlight live on the session's
##    presentation fields, never enter a hash, and never reach the simulation.
##    The moment they decide a battle they go through `session.commit_attack()`,
##    which mints the commitment the strategic hash covers - the ONLY door.
##
## 3. A REFUSAL IS SHOWN, NOT SWALLOWED. Every path that cannot proceed prints
##    the strategic layer's own reason into the message line.

signal back_requested
## Emitted with the session's commit result once a battle has been ADMITTED into
## the strategic state. The menu owns the scene change; this screen owns the
## commitment.
signal battle_committed(configured: Dictionary)
signal turn_ended

const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const BundleScript = preload("res://src/wotr/wotr_map_bundle.gd")
const MapViewScript = preload("res://src/wotr/wotr_map_view.gd")

## Seat colours. Fixed by seat index and never player-chosen: a colour is
## presentation, and a presentation value that varied per session would be one
## more thing to keep out of the hash.
const SEAT_COLORS: Array[Color] = [
	Color("#4d7fd6"), Color("#c8483f"), Color("#5aa552"), Color("#d0b03c"),
	Color("#a763c9"), Color("#3fb0ad"),
]
const NEUTRAL_COLOR := Color("#5a6656")

const MAP_INSET := 44.0
const MARKER_RADIUS := 11.0

var session: SessionScript = null
## Why War of the Ring is unavailable, or "" when it is. Non-empty means the map
## area shows the reason and nothing else.
var unavailable_reason := ""
## Pack map ids the tactical layer can actually boot, supplied by the menu.
var available_map_ids: Array = []

var heading_label: Label
var status_label: Label
## The flat 2D region graph. Kept as the honest fallback for when no living-map
## bundle has been converted, and labelled as a fallback when it is showing.
var map_view: Control
## Retail's 3D map, when a bundle is available.
var map3d: Control
var map_mode_label: Label
var map_bundle: BundleScript = null
## Why there is no 3D map, or "" when there is one.
var map_reason := ""
var detail_label: RichTextLabel
var message_label: Label
var unplaced_label: Label
var unplaced_host: VBoxContainer
var attack_button: Button
var end_turn_button: Button
var back_button: Button

var _rows: Array[Dictionary] = []
var _row_by_id: Dictionary = {}
var _targets: PackedStringArray = PackedStringArray()
var _moves: PackedStringArray = PackedStringArray()
var _staging: PackedStringArray = PackedStringArray()
var _screen_positions: Dictionary = {}


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	if heading_label == null:
		build()


func build() -> void:
	heading_label = Label.new()
	heading_label.name = "Heading"
	heading_label.text = "WAR OF THE RING"
	heading_label.position = Vector2(30, 16)
	heading_label.add_theme_font_size_override("font_size", 26)
	heading_label.add_theme_color_override("font_color", ThemeScript.GOLD_BRIGHT)
	add_child(heading_label)

	status_label = Label.new()
	status_label.name = "Status"
	status_label.position = Vector2(30, 54)
	status_label.add_theme_font_size_override("font_size", 16)
	status_label.add_theme_color_override("font_color", ThemeScript.TEXT_LEAF)
	add_child(status_label)

	map_view = Control.new()
	map_view.name = "MapView"
	map_view.position = Vector2(24, 84)
	map_view.custom_minimum_size = Vector2(1240, 620)
	map_view.size = Vector2(1240, 620)
	map_view.mouse_filter = Control.MOUSE_FILTER_STOP
	map_view.draw.connect(_draw_map)
	map_view.gui_input.connect(_on_map_input)
	add_child(map_view)

	map3d = MapViewScript.new()
	map3d.name = "Map3D"
	map3d.position = map_view.position
	map3d.custom_minimum_size = map_view.custom_minimum_size
	map3d.size = map_view.size
	map3d.visible = false
	map3d.region_clicked.connect(_on_region_clicked)
	map3d.region_hovered.connect(_on_region_hovered)
	add_child(map3d)

	map_mode_label = Label.new()
	map_mode_label.name = "MapMode"
	# Below the message line, so it never overlaps the map area.
	map_mode_label.position = Vector2(24, 738)
	map_mode_label.custom_minimum_size = Vector2(1240, 20)
	map_mode_label.add_theme_font_size_override("font_size", 13)
	map_mode_label.add_theme_color_override("font_color", ThemeScript.PARCHMENT_DIM)
	add_child(map_mode_label)

	var side_x := 1290.0
	unplaced_label = Label.new()
	unplaced_label.name = "UnplacedHeading"
	unplaced_label.position = Vector2(side_x, 84)
	unplaced_label.add_theme_font_size_override("font_size", 14)
	unplaced_label.add_theme_color_override("font_color", ThemeScript.PARCHMENT_DIM)
	add_child(unplaced_label)

	unplaced_host = VBoxContainer.new()
	unplaced_host.name = "UnplacedRegions"
	unplaced_host.position = Vector2(side_x, 108)
	unplaced_host.custom_minimum_size = Vector2(540, 0)
	unplaced_host.size = Vector2(540, 0)
	add_child(unplaced_host)

	detail_label = RichTextLabel.new()
	detail_label.name = "Detail"
	detail_label.bbcode_enabled = true
	detail_label.fit_content = false
	detail_label.scroll_active = true
	detail_label.position = Vector2(side_x, 200)
	detail_label.custom_minimum_size = Vector2(540, 420)
	detail_label.size = Vector2(540, 420)
	detail_label.add_theme_font_size_override("normal_font_size", 16)
	add_child(detail_label)

	message_label = Label.new()
	message_label.name = "Message"
	message_label.position = Vector2(24, 712)
	message_label.custom_minimum_size = Vector2(1240, 24)
	message_label.add_theme_font_size_override("font_size", 15)
	message_label.add_theme_color_override("font_color", ThemeScript.GOLD)
	add_child(message_label)

	attack_button = Button.new()
	attack_button.name = "Attack"
	attack_button.text = "ATTACK"
	attack_button.position = Vector2(side_x, 636)
	attack_button.custom_minimum_size = Vector2(250, 44)
	attack_button.size = Vector2(250, 44)
	attack_button.disabled = true
	attack_button.pressed.connect(_on_attack_pressed)
	add_child(attack_button)

	end_turn_button = Button.new()
	end_turn_button.name = "EndTurn"
	end_turn_button.text = "END TURN"
	end_turn_button.position = Vector2(side_x + 262, 636)
	end_turn_button.custom_minimum_size = Vector2(250, 44)
	end_turn_button.size = Vector2(250, 44)
	end_turn_button.pressed.connect(_on_end_turn_pressed)
	add_child(end_turn_button)

	back_button = Button.new()
	back_button.name = "Back"
	back_button.text = "MAIN MENU"
	back_button.position = Vector2(side_x, 690)
	back_button.custom_minimum_size = Vector2(250, 40)
	back_button.size = Vector2(250, 40)
	back_button.pressed.connect(func() -> void: back_requested.emit())
	add_child(back_button)


## Bind a live session, or NO session plus the reason there is none. Both are
## legitimate states and the screen shows either honestly; what it never does is
## show a map when `bound_session` is null.
func configure(bound_session, map_ids: Array, reason: String, pack_roots: Array = []) -> void:
	if heading_label == null:
		build()
	session = bound_session
	available_map_ids = map_ids.duplicate()
	unavailable_reason = reason
	load_map_bundle(pack_roots)
	refresh()


## Find and load retail's converted 3D map. Separate from `configure()` so a test
## can drive it directly, and so a failure to load the MAP never stops the
## strategic layer from working - the 2D fallback is a real screen, not an error
## state.
func load_map_bundle(pack_roots: Array = []) -> bool:
	if heading_label == null:
		build()
	var bundle := BundleScript.new()
	var located: Dictionary = bundle.locate_and_load(pack_roots)
	if bool(located.get("ok", false)):
		map_bundle = bundle
		map_reason = ""
	else:
		map_bundle = null
		map_reason = String(located.get("reason", ""))
	map3d.set_bundle(map_bundle, map_reason)
	var has_3d: bool = map3d.has_map()
	map3d.visible = has_3d
	map_view.visible = not has_3d
	return has_3d


func refresh() -> void:
	if heading_label == null:
		return
	_rows = []
	_row_by_id = {}
	_targets = PackedStringArray()
	_moves = PackedStringArray()
	_staging = PackedStringArray()
	if session == null or session.state == null:
		status_label.text = "UNAVAILABLE"
		detail_label.text = "[color=#e1c77d]War of the Ring is unavailable.[/color]\n\n%s" % unavailable_reason
		attack_button.disabled = true
		end_turn_button.disabled = true
		_clear_unplaced()
		unplaced_label.text = ""
		map_view.queue_redraw()
		_refresh_map_mode_label()
		return

	_rows = session.region_rows()
	for row in _rows:
		_row_by_id[String(row["id"])] = row
	_staging = session.staging_regions()
	if not session.selected_region.is_empty():
		_targets = session.attack_targets(session.selected_region)
		_moves = session.movement_targets(session.selected_region)

	var state: StateScript = session.state
	var seat := state.active_player()
	var seat_row: Dictionary = state.players[seat] as Dictionary if seat != StateScript.NEUTRAL else {}
	status_label.text = "TURN %d  ROUND %d   ACTIVE SEAT %d (%s, %s)   DOCUMENT %s (%s)   CAMPAIGN %s / %s" % [
		state.turn_index + 1,
		state.round_index() + 1,
		seat,
		String(seat_row.get("template", "?")),
		String(seat_row.get("controller", "?")),
		session.document_path.get_file(),
		session.document_source,
		session.world.campaign_name,
		session.scenario_name,
	]
	end_turn_button.disabled = false
	attack_button.disabled = not can_attack_now()
	_rebuild_unplaced()
	_refresh_detail()
	_refresh_map()


## Push the strategic picture into whichever map is showing. Strictly one-way:
## the map is handed already-computed rows and never writes anything back.
func _refresh_map() -> void:
	if map3d != null and map3d.has_map():
		var adjacency: Dictionary = {}
		for region_id in session.world.region_ids:
			adjacency[String(region_id)] = session.world.neighbours(String(region_id))
		map3d.owner_colors = SEAT_COLORS
		map3d.neutral_color = NEUTRAL_COLOR
		map3d.set_regions(
			_rows, adjacency, _staging, _targets,
			session.selected_region, session.selected_target)
		_refresh_map_mode_label()
		return
	map_view.queue_redraw()
	_refresh_map_mode_label()


func _refresh_map_mode_label() -> void:
	if map_mode_label == null:
		return
	if map3d == null or not map3d.has_map():
		map_mode_label.text = (
			"MAP: flat 2D region graph (fallback). Retail's 3D map is not converted here - %s"
			% map_reason)
		return
	var notes: Array[String] = []
	notes.append("MAP: retail livingmap.w3d, %d sub-objects, %d regions placed at authored world coordinates" % [
		map_bundle.sub_objects.size(), map3d.placed_regions.size()])
	if map3d.unplaced_regions.size() > 0:
		notes.append("%d region(s) unplaced (no authored centre point)" % map3d.unplaced_regions.size())
	if map3d.unsampled_heights.size() > 0:
		notes.append("%d region height(s) not sampled from terrain" % map3d.unsampled_heights.size())
	if map_bundle.untextured_sub_objects.size() > 0:
		notes.append("%d sub-object(s) drawn untextured: %s" % [
			map_bundle.untextured_sub_objects.size(),
			", ".join(Array(map_bundle.untextured_sub_objects))])
	# Everything retail draws that this lane does not, named on screen. A map
	# quietly missing its rivers and its ocean shader would look finished.
	var not_drawn: Array[String] = []
	for entry in map_bundle.sub_objects:
		if bool(entry["collision"]) or bool(entry["ambient"]) or bool(entry["shader_only"]):
			not_drawn.append(entry["name"] as String)
	not_drawn.sort()
	if not_drawn.size() > 0:
		notes.append("NOT DRAWN (%d): retail's impassable volumes, its animated ambient cards and multi-stage water overlays - %s" % [
			not_drawn.size(), ", ".join(not_drawn)])
	map_mode_label.text = "   |   ".join(notes)


## True when the current selection is a legal, committable attack.
func can_attack_now() -> bool:
	if session == null or session.state == null:
		return false
	if not session.state.pending_battle.is_empty():
		return false
	if session.selected_region.is_empty() or session.selected_target.is_empty():
		return false
	return Array(_targets).has(session.selected_target)


## Select a region to stage from. Returns false (with a shown reason) when the
## region cannot stage an attack this turn.
func select_region(region_id: String) -> bool:
	if session == null or session.state == null:
		return false
	if not _row_by_id.has(region_id):
		_message("%s is not a region of this campaign." % region_id)
		return false
	if not Array(_staging).has(region_id):
		var owner := session.state.owner_of(region_id)
		if owner != session.state.active_player():
			_message("%s is not yours to attack from." % _display_of(region_id))
		else:
			_message("%s holds no army to attack with." % _display_of(region_id))
		session.selected_region = ""
		session.selected_target = ""
		refresh()
		return false
	session.selected_region = region_id
	session.selected_target = ""
	_message("")
	refresh()
	return true


## Choose the adjacent region to attack. Refuses anything the strategic layer
## does not report as attackable from the staged region.
func select_target(region_id: String) -> bool:
	if session == null or session.state == null:
		return false
	if session.selected_region.is_empty():
		_message("Choose one of your own regions to attack from first.")
		return false
	if not Array(_targets).has(region_id):
		_message("%s cannot be attacked from %s." % [_display_of(region_id), _display_of(session.selected_region)])
		return false
	session.selected_target = region_id
	_message("")
	refresh()
	return true


## Commit the selected attack. THE ONLY path from this screen to a battle, and it
## carries the target region and nothing else - the attacker, the armies, the
## factions and the ground are all derived by the strategic layer and recorded in
## the commitment.
func commit_selected_attack() -> Dictionary:
	if not can_attack_now():
		_message("There is no committable attack selected.")
		return {"ok": false}
	var configured: Dictionary = session.commit_attack(session.selected_target, available_map_ids)
	if not bool(configured.get("ok", false)):
		_message("Attack refused: %s" % ", ".join(Array(configured.get("refusals", PackedStringArray()))))
		refresh()
		return configured
	refresh()
	battle_committed.emit(configured)
	return configured


func end_turn() -> void:
	if session == null or session.state == null:
		return
	if not session.state.pending_battle.is_empty():
		_message("A battle is still in flight; it must resolve before the turn passes.")
		return
	session.state.advance_turn()
	session.selected_region = ""
	session.selected_target = ""
	_message("")
	refresh()
	turn_ended.emit()


func show_message(text: String) -> void:
	_message(text)


# --- detail panel ------------------------------------------------------------

func _refresh_detail() -> void:
	var lines: Array[String] = []
	var state: StateScript = session.state
	lines.append("[color=#e1c77d]YOUR REGIONS WITH AN ARMY[/color]")
	if _staging.is_empty():
		lines.append("  none - this seat has no army standing in a region it owns")
	for region_id in _staging:
		var row := _row_by_id[region_id] as Dictionary
		lines.append("  %s   armies %d   CP %d" % [_display_of(region_id), int(row["armies"]), int(row["command_points"])])
	lines.append("")
	if session.selected_region.is_empty():
		lines.append("Select one of your regions to see what it can attack.")
	else:
		lines.append("[color=#e1c77d]ATTACKING FROM %s[/color]" % _display_of(session.selected_region))
		if _targets.is_empty():
			lines.append("  no adjacent region can be attacked from here")
		if not _moves.is_empty():
			lines.append("  march to: %s" % ", ".join(Array(_moves)))
		for target in _targets:
			var marker := ">" if target == session.selected_target else " "
			var row := _row_by_id[target] as Dictionary
			lines.append("%s %s   owner %s   armies %d" % [
				marker, _display_of(target), _owner_name(int(row["owner"])), int(row["armies"])])
	lines.append("")
	if not session.selected_target.is_empty():
		var target_row := _row_by_id[session.selected_target] as Dictionary
		var region_map := String(target_row["map_name"])
		var bindings := session.battlefield_bindings(available_map_ids)
		var battlefield := String(bindings.get(region_map, ""))
		lines.append("[color=#e1c77d]BATTLE[/color]")
		lines.append("  region map   %s" % region_map)
		if battlefield.is_empty():
			lines.append("  [color=#c8483f]battlefield   NONE - no pack map is available to stand in for it, so this attack cannot be fought[/color]")
		else:
			lines.append("  battlefield  %s" % battlefield)
			lines.append("  [color=#a9b39a]STAND-IN: no MAP WOR region map is cooked in any content pack, so this battle is fought on a converted skirmish map instead. The battlefield is recorded in the commitment.[/color]")
	if not state.pending_battle.is_empty():
		lines.append("")
		lines.append("[color=#c8483f]A battle for %s is in flight.[/color]" % _display_of(String(state.pending_battle.get("region", ""))))
	detail_label.text = "\n".join(lines)


func _rebuild_unplaced() -> void:
	_clear_unplaced()
	var unplaced: Array[Dictionary] = []
	for row in _rows:
		if not bool(row["has_position"]):
			unplaced.append(row)
	if unplaced.is_empty():
		unplaced_label.text = ""
		return
	# NOT drawn on the map, and said so. Retail falls back to the region's own
	# sub-object when `CustomCenterPoint` is absent - and `livingmap.w3d` does NOT
	# carry per-region sub-objects. Its 64 sub-objects are 20 terrain tiles, the
	# coast and water, the impassable volumes, the ambient cards and eleven named
	# landmarks; there is no `Rhun` mesh in it to take a centre from. So the
	# position genuinely does not exist in the converted map, and a coordinate
	# chosen here would be invented map data.
	unplaced_label.text = "REGIONS WITH NO AUTHORED MAP POSITION (%d) - retail takes these from a per-region sub-object that livingmap.w3d does not carry" % unplaced.size()
	for row in unplaced:
		var button := Button.new()
		var region_id := String(row["id"])
		button.name = "Unplaced_%s" % region_id
		button.text = "%s   (%s)" % [_display_of(region_id), _owner_name(int(row["owner"]))]
		button.custom_minimum_size = Vector2(0, 26)
		button.pressed.connect(_on_region_clicked.bind(region_id))
		unplaced_host.add_child(button)


func _clear_unplaced() -> void:
	if unplaced_host == null:
		return
	for child in unplaced_host.get_children():
		unplaced_host.remove_child(child)
		child.queue_free()


# --- the map -----------------------------------------------------------------

func _draw_map() -> void:
	var size := map_view.size
	if session == null or session.state == null:
		map_view.draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.05, 0.03, 0.6))
		return
	_screen_positions = _compute_screen_positions(size)
	map_view.draw_rect(Rect2(Vector2.ZERO, size), Color(0.02, 0.05, 0.03, 0.6))
	# Edges first, so markers sit on top of them.
	for region_id in session.world.region_ids:
		if not _screen_positions.has(region_id):
			continue
		for neighbour in session.world.neighbours(region_id):
			if not _screen_positions.has(neighbour):
				continue
			if String(neighbour) < String(region_id):
				continue
			map_view.draw_line(
				_screen_positions[region_id], _screen_positions[neighbour],
				Color(0.35, 0.45, 0.32, 0.45), 1.5)
	var font := get_theme_default_font()
	for row in _rows:
		var region_id := String(row["id"])
		if not _screen_positions.has(region_id):
			continue
		var point: Vector2 = _screen_positions[region_id]
		var color := _owner_color(int(row["owner"]))
		var radius := MARKER_RADIUS + (2.0 if int(row["armies"]) > 0 else 0.0)
		map_view.draw_circle(point, radius, color)
		map_view.draw_arc(point, radius, 0.0, TAU, 24, Color(0.05, 0.09, 0.05, 0.9), 2.0)
		if region_id == session.selected_region:
			map_view.draw_arc(point, radius + 6.0, 0.0, TAU, 28, ThemeScript.GOLD_BRIGHT, 3.0)
		elif Array(_targets).has(region_id):
			map_view.draw_arc(point, radius + 6.0, 0.0, TAU, 28, Color("#c8483f"), 2.0)
		elif Array(_staging).has(region_id):
			map_view.draw_arc(point, radius + 4.0, 0.0, TAU, 28, Color(0.85, 0.92, 0.75, 0.55), 1.5)
		if region_id == session.selected_target:
			map_view.draw_arc(point, radius + 10.0, 0.0, TAU, 28, Color("#e8623f"), 3.0)
		if font != null:
			var label := "%s%s" % [region_id, "  x%d" % int(row["armies"]) if int(row["armies"]) > 0 else ""]
			map_view.draw_string(font, point + Vector2(radius + 5.0, 4.0), label,
				HORIZONTAL_ALIGNMENT_LEFT, -1, 13, ThemeScript.TEXT_LEAF)


## Authored region coordinates scaled into the view. Pure presentation: the
## transform is derived from the authored extent every frame and reaches nothing
## but the drawing. Regions without an authored point are ABSENT from the result
## rather than defaulted into a corner.
func _compute_screen_positions(size: Vector2) -> Dictionary:
	var placed: Array[Dictionary] = []
	for row in _rows:
		if bool(row["has_position"]):
			placed.append(row)
	var positions: Dictionary = {}
	if placed.is_empty():
		return positions
	var minimum := (placed[0]["position"] as Vector2)
	var maximum := minimum
	for row in placed:
		var point := row["position"] as Vector2
		minimum = Vector2(minf(minimum.x, point.x), minf(minimum.y, point.y))
		maximum = Vector2(maxf(maximum.x, point.x), maxf(maximum.y, point.y))
	var span := maximum - minimum
	var usable := size - Vector2(MAP_INSET * 2.0, MAP_INSET * 2.0)
	var scale_x := usable.x / span.x if span.x > 0.0 else 1.0
	var scale_y := usable.y / span.y if span.y > 0.0 else 1.0
	var factor := minf(scale_x, scale_y)
	for row in placed:
		var point := row["position"] as Vector2
		var local := (point - minimum) * factor
		# Retail's strategic Y grows northward; screen Y grows down.
		positions[String(row["id"])] = Vector2(
			MAP_INSET + local.x,
			size.y - MAP_INSET - local.y)
	return positions


func _on_map_input(event: InputEvent) -> void:
	if session == null or session.state == null:
		return
	if event is InputEventMouseMotion:
		var hovered := _region_at(event.position)
		if hovered != session.hover_region:
			session.hover_region = hovered
		return
	if not (event is InputEventMouseButton):
		return
	var button := event as InputEventMouseButton
	if not button.pressed or button.button_index != MOUSE_BUTTON_LEFT:
		return
	var region_id := _region_at(button.position)
	if region_id.is_empty():
		return
	_on_region_clicked(region_id)


## Hover from the 3D map. Presentation only: it lands on the session's per-seat
## presentation field, which `authoritative_state()` does not read and no hash
## covers.
func _on_region_hovered(region_id: String) -> void:
	if session == null:
		return
	session.hover_region = region_id


## A click means "stage here" on one of your own armed regions and "attack here"
## on anything the staged region can reach. Deterministic and total: the target
## reading is tried first only when a staging region is already chosen, so a
## region that is both never depends on click order.
func _on_region_clicked(region_id: String) -> void:
	if not session.selected_region.is_empty():
		# The two readings are DISJOINT by construction - an attack target is a
		# region this seat does not own and a march target is one it does - so the
		# order below is a statement of that fact rather than a tie-break.
		if Array(_targets).has(region_id):
			select_target(region_id)
			return
		if Array(_moves).has(region_id):
			move_to(region_id)
			return
	select_region(region_id)


## March the staged region's armies into an adjacent region this seat owns. This
## is a STRATEGIC COMMAND, not a selection: it changes the authoritative state
## and therefore the hash, which is exactly what it should do - retail seats both
## sides deep in their own territory, so without it no attack is ever reachable.
func move_to(region_id: String) -> bool:
	if session == null or session.state == null:
		return false
	if session.selected_region.is_empty():
		_message("Choose one of your own regions to march from first.")
		return false
	var from_region := session.selected_region
	var result: Dictionary = session.move_armies(from_region, region_id)
	if not bool(result.get("ok", false)):
		_message("March refused: %s" % ", ".join(Array(result.get("refusals", PackedStringArray()))))
		refresh()
		return false
	session.selected_region = region_id
	session.selected_target = ""
	_message("Marched %d army/armies from %s to %s." % [
		(result["moved"] as PackedInt32Array).size(), _display_of(from_region), _display_of(region_id)])
	refresh()
	return true


func _region_at(point: Vector2) -> String:
	var best := ""
	var best_distance := MARKER_RADIUS + 6.0
	var ids: Array[String] = []
	for key in _screen_positions.keys():
		ids.append(String(key))
	ids.sort()
	for region_id in ids:
		var distance := (point - (_screen_positions[region_id] as Vector2)).length()
		if distance <= best_distance:
			best = region_id
			best_distance = distance
	return best


# --- internals ---------------------------------------------------------------

func _on_attack_pressed() -> void:
	commit_selected_attack()


func _on_end_turn_pressed() -> void:
	end_turn()


func _message(text: String) -> void:
	if message_label != null:
		message_label.text = text


## The label a region is shown under. The document's `displayName` is a STRING
## TABLE KEY (`LW:DisplayNameArnor`), not a name, and no living-world string
## table is converted; printing the key would be noise and inventing a name would
## be worse. Retail's own region id is the honest label until the string table
## lane exists.
func _display_of(region_id: String) -> String:
	return region_id


func _owner_name(owner: int) -> String:
	if session == null or session.state == null:
		return "?"
	if owner == StateScript.NEUTRAL:
		return "unclaimed"
	if owner < 0 or owner >= session.state.players.size():
		return "?"
	return String((session.state.players[owner] as Dictionary).get("template", "seat %d" % owner))


func _owner_color(owner: int) -> Color:
	if owner == StateScript.NEUTRAL or owner < 0:
		return NEUTRAL_COLOR
	return SEAT_COLORS[owner % SEAT_COLORS.size()]
