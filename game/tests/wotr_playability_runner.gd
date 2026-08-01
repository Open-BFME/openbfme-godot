extends SceneTree

## One behavior-level check for the three player-facing failures reported from a
## real War of the Ring playthrough. This is intentionally a single gameplay
## runner, not a unit-test matrix.

const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const ScreenScript = preload("res://src/ui/wotr_screen.gd")

var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var found := SessionScript.locate_document(_content_roots())
	if not bool(found.get("ok", false)):
		_fail("document", String(found.get("reason", "")))
		_finish()
		return
	var session = _begin_session(found["document"] as Dictionary)
	if session == null:
		_finish()
		return

	var screen := ScreenScript.new()
	screen.build()
	screen.configure(session, [], "", _content_roots())
	root.add_child(screen)
	await process_frame

	# The authored expander is a real Button action, not a direct field write.
	screen.set_objectives_open(true)
	screen.objectives_expander.pressed.emit()
	var collapsed := not screen.objectives_open
	if not collapsed:
		_fail("collapsed", "the objectives expander did not close its panel")

	# Select one active hero army and issue a real individual move through the
	# session. Prefer friendly ground so the resulting region changes at once.
	var selected := 0
	var moved := 0
	var active: int = session.state.active_player()
	for army_key in session.state.armies.keys():
		var army_id := int(army_key)
		var army := session.state.armies[army_id] as Dictionary
		if int(army.get("owner", -1)) != active \
				or String(army.get("kind", "")) != StateScript.ARMY_HERO:
			continue
		var from_region := String(army.get("region", ""))
		for neighbour in session.world.neighbours(from_region):
			if session.state.owner_of(neighbour) != active:
				continue
			var hit: Dictionary = screen.map3d._army_hit_boxes.get(army_id, {})
			if hit.is_empty():
				continue
			var left := InputEventMouseButton.new()
			left.button_index = MOUSE_BUTTON_LEFT
			left.pressed = true
			left.position = (hit["box"] as Rect2).get_center()
			screen.map3d._gui_input(left)
			selected = 1 if screen.selected_army_id == army_id else 0
			var destination: Vector2 = screen.map3d._screen_positions.get(
				String(neighbour), Vector2(-10000.0, -10000.0))
			var right_down := InputEventMouseButton.new()
			right_down.button_index = MOUSE_BUTTON_RIGHT
			right_down.pressed = true
			right_down.position = destination
			screen.map3d._gui_input(right_down)
			var right_up := InputEventMouseButton.new()
			right_up.button_index = MOUSE_BUTTON_RIGHT
			right_up.pressed = false
			right_up.position = destination
			screen.map3d._gui_input(right_up)
			moved = 1 if String((session.state.armies[army_id] as Dictionary).get(
				"region", "")) == String(neighbour) else 0
			break
		if moved == 1:
			break
	if selected != 1:
		_fail("selected", "no active Hero army could be selected")
	if moved != 1:
		_fail("moved", "no selected Hero army moved to adjacent friendly territory")

	# Raise one legal structure through the real catalogue/state path, refresh the
	# screen, and require a converted 3D building marker to stand on its plot.
	var structured := 0
	session.state.treasury[active] = 100000
	for region_value in session.state.regions_owned_by(active):
		var region_id := String(region_value)
		var plots: Dictionary = session.build_plots(region_id)
		if int(plots.get("free", 0)) <= 0:
			continue
		for option_value in session.build_options(region_id):
			var option := option_value as Dictionary
			if not bool(option.get("can_build", false)):
				continue
			var built: Dictionary = session.commit_build(
				region_id, String(option.get("building", "")))
			if not bool(built.get("ok", false)):
				continue
			screen.refresh()
			structured = 1 if screen.map3d != null \
				and screen.map3d.structure_markers_standing > 0 else 0
			break
		if structured == 1:
			break
	if structured != 1:
		_fail("structured", "a built structure produced no standing 3D marker; flat=%s rows=%s"
			% [str(screen.map3d.structure_markers_flat),
				str(screen._structures_by_region())])

	print("WOTR_PLAYABILITY_RESULT selected=%d moved=%d structured=%d collapsed=%d" % [
		selected, moved, structured, 1 if collapsed else 0])
	screen.queue_free()
	await process_frame
	_finish()


func _begin_session(document: Dictionary):
	var probe := SessionScript.new()
	var world = load("res://src/wotr/wotr_world.gd").new()
	if not world.load_from_dict(document, ""):
		_fail("world", str(world.errors))
		return null
	probe.world = world
	var availability: Dictionary = {}
	for faction in SessionScript.FACTION_BINDINGS.values():
		availability[String(faction)] = ""
	var options := probe.seat_options(availability)
	var scenarios := probe.startable_scenarios(2)
	if options.size() < 2 or scenarios.is_empty():
		_fail("seating", "the retail document has no two-seat start")
		return null
	var seats: Array = []
	for index in 2:
		seats.append({
			"template": String((options[index] as Dictionary).get("template", "")),
			"team": index + 1,
			"controller": StateScript.CONTROLLER_HUMAN if index == 0
				else StateScript.CONTROLLER_AI,
		})
	var session := SessionScript.new()
	if not session.begin(document, world.campaign_name, String(scenarios[0]), seats):
		_fail("begin", str(session.refusals))
		return null
	return session


func _content_roots() -> Array:
	var roots: Array = []
	var configured := OS.get_environment(SessionScript.CONTENT_ENV)
	if not configured.is_empty() and DirAccess.dir_exists_absolute(configured):
		for child in DirAccess.get_directories_at(configured):
			var parent := configured.path_join(child)
			for digest in DirAccess.get_directories_at(parent):
				roots.append(parent.path_join(digest))
	return roots


func _fail(name: String, reason: String) -> void:
	failed += 1
	printerr("WOTR_PLAYABILITY FAIL %s: %s" % [name, reason])


func _finish() -> void:
	quit(0 if failed == 0 else 1)
