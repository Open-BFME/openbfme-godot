extends "res://src/stage1/stage1_arena.gd"
## Playable Stage 2 coordinator layered on the deterministic Stage 1 simulation.

const Stage2BundleScript = preload("res://src/stage1_sim/stage1_bundle.gd")

@onready var stage2_view: Stage2View = $Stage1View
@onready var stage2_hud: Stage2Hud = $Stage1Hud
@onready var placement_ghost: Stage2PlacementGhost = $PlacementGhost

var placement_type_code: int = 0
var rally_targeting: bool = false
var selected_building_id: int = 0
var preview_position: Vector2i = Vector2i.ZERO
var preview_valid: bool = false

func _ready() -> void:
	super._ready()
	stage2_hud.build_requested.connect(_on_build_requested)
	stage2_hud.train_requested.connect(_on_train_requested)
	stage2_hud.rally_requested.connect(_on_rally_requested)

func _restart() -> void:
	var bundle: Dictionary = Stage2BundleScript.load_bundle()
	world = Stage2BundleScript.build_stage2_world(bundle)
	current_snapshot = world.snapshot()
	selected_ids.clear()
	selected_building_id = 0
	placement_type_code = 0
	rally_targeting = false
	attack_move_armed = false
	camera_rig.command_mode = false
	accumulator = 0.0
	render_alpha = 1.0
	prebattle_seconds = 0.0
	displayed_winner = Stage1Types.Team.NONE
	last_alive_total = world.living_member_count(Stage1Types.Team.BLUE) + world.living_member_count(Stage1Types.Team.RED)
	hud.hide_winner()
	stage2_hud.configure_catalog(world.building_definitions(), world.blueprint_definitions(), world.stage2.rules.maximum_queue_length)
	stage2_hud.set_selection(0, "Build a supply structure and producer, then train five battalions")
	placement_ghost.disarm()
	arena_view.configure(current_snapshot)
	_refresh_stage2_presentation()
	var blue_fort := world.fortress_for(Stage1Types.Team.BLUE)
	if blue_fort != null:
		var blue_world: Vector3 = arena_view.fixed_to_world(blue_fort.position)
		camera_rig.focus_on(Vector2(blue_world.x + 11.0, blue_world.z))

func _process(delta: float) -> void:
	super._process(delta)
	if world != null:
		_update_stage2_hud()

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and placement_type_code != 0:
		_update_placement_preview((event as InputEventMouseMotion).position)
		get_viewport().set_input_as_handled()
		return
	if event is InputEventMouseButton:
		var mouse := event as InputEventMouseButton
		if mouse.button_index == MOUSE_BUTTON_RIGHT and mouse.pressed and (placement_type_code != 0 or rally_targeting):
			_cancel_targeting("Build/rally targeting cancelled")
			get_viewport().set_input_as_handled()
			return
		if mouse.button_index == MOUSE_BUTTON_LEFT and mouse.pressed:
			if placement_type_code != 0:
				_update_placement_preview(mouse.position)
				request_place_at(placement_type_code, preview_position)
				get_viewport().set_input_as_handled()
				return
			if rally_targeting:
				var destination: Variant = _snapped_ground_from_screen(mouse.position)
				if destination == null:
					stage2_hud.set_stage2_hint("Invalid rally target: point at the arena ground")
				else:
					request_rally_at(destination)
				get_viewport().set_input_as_handled()
				return
	if event is InputEventKey:
		var key := event as InputEventKey
		if key.pressed and not key.echo and key.keycode == KEY_ESCAPE and (placement_type_code != 0 or rally_targeting):
			_cancel_targeting("Build/rally targeting cancelled")
			get_viewport().set_input_as_handled()
			return
	super._unhandled_input(event)

func _finish_selection(rect: Rect2, click_position: Vector2, additive: bool) -> void:
	if rect.size.length() < 12.0:
		var building_id := _pick_blue_building(click_position, 54.0)
		if building_id != 0:
			_select_building(building_id)
			return
	selected_building_id = 0
	stage2_view.set_selected_building(0)
	super._finish_selection(rect, click_position, additive)
	if not selected_ids.is_empty():
		stage2_hud.show_build_menu()

func request_place_at(type_code: int, position: Vector2i) -> bool:
	if world == null:
		return false
	# The simulation revalidates the same snapped fixed-point position before it
	# spends resources or blocks navigation. The ghost is never authoritative.
	if not world.can_place_building(Stage1Types.Team.BLUE, type_code, position):
		stage2_hud.set_stage2_hint(_placement_failure_text(type_code, position))
		preview_valid = false
		placement_ghost.update_preview(arena_view.fixed_to_world(position), false)
		return false
	var building: Stage2Types.Building = world.try_place_building(Stage1Types.Team.BLUE, type_code, position)
	if building == null:
		stage2_hud.set_stage2_hint("Placement rejected by simulation authority; choose another cell")
		return false
	selected_building_id = building.id
	selected_ids.clear()
	placement_type_code = 0
	preview_valid = false
	placement_ghost.disarm()
	camera_rig.command_mode = false
	stage2_hud.set_stage2_hint("%s placed as building %d; construction has started" % [_building_name(type_code), building.id])
	_refresh_stage2_presentation()
	return true

func request_train(type_code: int) -> bool:
	if world == null:
		return false
	var building := world.get_building(selected_building_id)
	if building == null or building.destroyed:
		stage2_hud.set_stage2_hint("Invalid training target: select a blue production building")
		return false
	if not building.completed:
		stage2_hud.set_stage2_hint("Producer is still under construction")
		return false
	var definition := world.stage2.get_definition(building.type_code)
	var blueprint := world.stage2.get_blueprint(type_code)
	if definition == null or blueprint == null or not definition.train_type_codes.has(type_code):
		stage2_hud.set_stage2_hint("Invalid training target for this producer")
		return false
	if building.jobs.size() >= world.stage2.rules.maximum_queue_length:
		stage2_hud.set_stage2_hint("Training queue full (%d/%d)" % [building.jobs.size(), world.stage2.rules.maximum_queue_length])
		return false
	var economy := world.get_economy(building.team)
	if economy.resources < blueprint.cost:
		stage2_hud.set_stage2_hint("Insufficient supplies: need %d, have %d" % [blueprint.cost, economy.resources])
		return false
	var used := world.stage2.population_used(world, building.team)
	var reserved := world.stage2.population_reserved(building.team)
	if used + reserved + blueprint.population > economy.population_cap:
		stage2_hud.set_stage2_hint("Population cap reached: %d + %d queued / %d" % [used, reserved, economy.population_cap])
		return false
	if not world.try_train(selected_building_id, type_code):
		stage2_hud.set_stage2_hint("Training order rejected by simulation authority")
		return false
	stage2_hud.set_stage2_hint("%s queued (%d/%d slots)" % [_blueprint_name(type_code), building.jobs.size(), world.stage2.rules.maximum_queue_length])
	_refresh_stage2_presentation()
	return true

func request_rally_at(position: Vector2i) -> bool:
	if world == null:
		return false
	var building := world.get_building(selected_building_id)
	if building == null or building.destroyed or not building.completed:
		stage2_hud.set_stage2_hint("Invalid rally target: select a completed producer")
		return false
	var definition := world.stage2.get_definition(building.type_code)
	if definition == null or definition.role_code != Stage2Types.BuildingRole.PRODUCTION:
		stage2_hud.set_stage2_hint("Invalid rally target: selected building cannot train")
		return false
	var cell := world.grid.to_cell(position)
	if not world.grid.contains(cell) or world.grid.is_blocked(cell):
		stage2_hud.set_stage2_hint("Invalid rally target: choose walkable ground")
		return false
	if not world.try_set_rally(selected_building_id, position):
		stage2_hud.set_stage2_hint("Rally target rejected by simulation authority")
		return false
	rally_targeting = false
	camera_rig.command_mode = false
	stage2_hud.set_stage2_hint("Rally point set; new battalions will deploy toward the marker")
	_refresh_stage2_presentation()
	return true

func _on_build_requested(type_code: int) -> void:
	var definition := world.stage2.get_definition(type_code) if world != null else null
	if definition == null or definition.role_code == Stage2Types.BuildingRole.FORTRESS:
		stage2_hud.set_stage2_hint("Invalid build catalog entry")
		return
	placement_type_code = type_code
	rally_targeting = false
	selected_building_id = 0
	stage2_view.set_selected_building(0)
	placement_ghost.arm(Vector2i(definition.width_cells, definition.height_cells))
	camera_rig.command_mode = true
	stage2_hud.set_stage2_hint("Placing %s: green is valid, red is blocked; LMB confirm, RMB cancel" % _building_name(type_code))
	_update_placement_preview(get_viewport().get_mouse_position())

func _on_train_requested(type_code: int) -> void:
	request_train(type_code)

func _on_rally_requested() -> void:
	var building := world.get_building(selected_building_id) if world != null else null
	if building == null or not building.completed:
		stage2_hud.set_stage2_hint("Select a completed production building first")
		return
	rally_targeting = true
	placement_type_code = 0
	placement_ghost.disarm()
	camera_rig.command_mode = true
	stage2_hud.set_stage2_hint("Rally targeting: click walkable ground, or RMB to cancel")

func _update_placement_preview(screen_position: Vector2) -> void:
	if placement_type_code == 0 or world == null:
		return
	var ground: Variant = camera_rig.ground_point(screen_position)
	if ground == null:
		preview_valid = false
		placement_ghost.update_preview(placement_ghost.position, false)
		return
	var raw_fixed: Vector2i = arena_view.world_to_fixed(ground)
	var raw_cell := world.grid.to_cell(raw_fixed)
	var clamped_cell := Vector2i(clampi(raw_cell.x, 0, world.grid.width - 1), clampi(raw_cell.y, 0, world.grid.height - 1))
	preview_position = Stage1Grid.cell_center(clamped_cell)
	preview_valid = world.grid.contains(raw_cell) and world.can_place_building(Stage1Types.Team.BLUE, placement_type_code, preview_position)
	placement_ghost.update_preview(arena_view.fixed_to_world(preview_position), preview_valid)

func _snapped_ground_from_screen(screen_position: Vector2) -> Variant:
	var ground: Variant = camera_rig.ground_point(screen_position)
	if ground == null:
		return null
	var fixed: Vector2i = arena_view.world_to_fixed(ground)
	var cell := world.grid.to_cell(fixed)
	if not world.grid.contains(cell):
		return null
	return Stage1Grid.cell_center(cell)

func _pick_blue_building(screen_position: Vector2, radius: float) -> int:
	var best_id := 0
	var best_distance := radius
	for building: Dictionary in current_snapshot.get("buildings", []):
		if int(building.get("team", -1)) != Stage1Types.Team.BLUE or int(building.get("health", 0)) <= 0:
			continue
		var world_position: Vector3 = arena_view.fixed_to_world(building.get("position", Vector2i.ZERO), 0.8)
		var point := camera_rig.camera.unproject_position(world_position)
		var distance := point.distance_to(screen_position)
		if distance < best_distance:
			best_distance = distance
			best_id = int(building.get("id", 0))
	return best_id

func _select_building(building_id: int) -> void:
	var row := _building_row(building_id)
	if row.is_empty():
		return
	selected_building_id = building_id
	selected_ids.clear()
	attack_move_armed = false
	rally_targeting = false
	placement_type_code = 0
	placement_ghost.disarm()
	camera_rig.command_mode = false
	stage2_view.set_selected_building(building_id)
	stage2_hud.set_selection(0, "Building %d selected; use its training or rally controls" % building_id)
	stage2_hud.show_building(row)
	_play_command_tone(false)

func _update_stage2_hud() -> void:
	var economy := world.get_economy(Stage1Types.Team.BLUE)
	if economy != null:
		stage2_hud.set_economy(
			economy.resources,
			economy.total_earned,
			world.stage2.population_used(world, Stage1Types.Team.BLUE),
			world.stage2.population_reserved(Stage1Types.Team.BLUE),
			economy.population_cap
		)
	if selected_building_id != 0:
		var row := _building_row(selected_building_id)
		if row.is_empty() or int(row.get("health", 0)) <= 0:
			selected_building_id = 0
			stage2_view.set_selected_building(0)
			stage2_hud.show_build_menu()
		else:
			stage2_hud.show_building(row)

func _refresh_stage2_presentation() -> void:
	current_snapshot = world.snapshot()
	arena_view.render_snapshot(current_snapshot, 1.0, selected_ids)
	stage2_view.set_selected_building(selected_building_id)
	_update_stage2_hud()

func _building_row(building_id: int) -> Dictionary:
	for building: Dictionary in current_snapshot.get("buildings", []):
		if int(building.get("id", 0)) == building_id:
			return building
	return {}

func _placement_failure_text(type_code: int, position: Vector2i) -> String:
	var definition := world.stage2.get_definition(type_code)
	if definition == null or definition.role_code == Stage2Types.BuildingRole.FORTRESS:
		return "Invalid building type"
	var economy := world.get_economy(Stage1Types.Team.BLUE)
	if economy == null:
		return "No economy is available for the blue team"
	if economy.resources < definition.cost:
		return "Insufficient supplies: need %d, have %d" % [definition.cost, economy.resources]
	if not Stage2Placement.can_place(world.grid, position, definition):
		return "Invalid placement: footprint overlaps blocked or occupied ground"
	return "Invalid placement: a fortress or battalion occupies that footprint"

func _building_name(type_code: int) -> String:
	var definition := world.stage2.get_definition(type_code) if world != null else null
	return _friendly_name(definition.display_name if definition != null else "Building")

func _blueprint_name(type_code: int) -> String:
	var blueprint := world.stage2.get_blueprint(type_code) if world != null else null
	return _friendly_name(blueprint.display_name if blueprint != null else "Battalion")

func _friendly_name(value: String) -> String:
	var result := value
	if result.contains("."):
		result = result.get_slice(".", result.get_slice_count(".") - 1)
	return result.replace("-", " ").replace("_", " ").capitalize()

func _cancel_targeting(message: String) -> void:
	placement_type_code = 0
	rally_targeting = false
	preview_valid = false
	placement_ghost.disarm()
	camera_rig.command_mode = false
	stage2_hud.set_stage2_hint(message)
