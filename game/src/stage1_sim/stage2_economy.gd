class_name Stage2Economy
extends RefCounted
## Deterministic Stage 2 economy state. Stage1World owns tick orchestration.

var rules := Stage2Types.Rules.new()
var economies: Array[Stage2Types.TeamEconomy] = []
var buildings: Array[Stage2Types.Building] = []
var definitions: Array[Stage2Types.BuildingDefinition] = []
var blueprints: Array[Stage2Types.UnitBlueprint] = []
var commands: Array[Stage2Types.EconomyCommand] = []
var next_command_index: int = 0
var next_building_id: int = 200
var next_job_id: int = 1
var horde_population: Dictionary = {}

var _next_command_sequence: int = 0

func configure(data: Dictionary, world: Stage1World) -> String:
	if String(data.get("schema", "")) != "openbfme.economy" or int(data.get("schemaVersion", -1)) != 0:
		return "economy_schema"
	rules.version = int(data.get("rulesVersion", 0))
	var rule_data: Dictionary = data.get("rules", {})
	rules.maximum_queue_length = int(rule_data.get("maximumTrainQueue", 0))
	rules.construction_health_ramp_code = 1 if String(rule_data.get("constructionHealthRamp", "")) == "linear-floor-minimum-one" else -1
	rules.building_blocks_at_code = 0 if String(rule_data.get("buildingBlocksNavigationAt", "")) == "placement" else -1
	rules.queued_population_counts_code = 1 if bool(rule_data.get("queuedBattalionsCountTowardPopulation", false)) else 0
	rules.spawn_search_radius_cells = int(rule_data.get("spawnSearchMaximumRadiusCells", 0))
	rules.spawn_search_order.clear()
	for direction: String in rule_data.get("spawnSearchOrder", []):
		rules.spawn_search_order.append(["north", "east", "south", "west"].find(direction))
	var efficiency: Dictionary = data.get("farmEfficiency", {})
	rules.efficiency_radius_subcells = int(efficiency.get("radiusSubcells", 0))
	rules.base_efficiency_permille = int(efficiency.get("basePermille", 0))
	rules.neighbor_penalty_permille = int(efficiency.get("penaltyPerNeighborPermille", 0))
	rules.minimum_efficiency_permille = int(efficiency.get("minimumPermille", 0))
	if rules.version <= 0 or rules.maximum_queue_length <= 0 or rules.construction_health_ramp_code != 1 or rules.building_blocks_at_code != 0 or rules.spawn_search_radius_cells <= 0 or rules.spawn_search_order != [0, 1, 2, 3]:
		return "economy_rules"
	economies.clear()
	for side: Dictionary in data.get("sides", []):
		economies.append(Stage2Types.TeamEconomy.new(int(side.get("team", -1)), int(side.get("startingResources", -1)), int(side.get("populationCap", -1))))
	economies.sort_custom(func(a: Stage2Types.TeamEconomy, b: Stage2Types.TeamEconomy) -> bool: return a.team < b.team)
	definitions.clear()
	for row: Dictionary in data.get("buildings", []):
		var role := ["fortress", "resource", "production"].find(String(row.get("role", "")))
		var definition := Stage2Types.BuildingDefinition.new(int(row.get("typeCode", -1)), role)
		definition.object_id = String(row.get("objectId", ""))
		definition.display_name = String(row.get("displayName", definition.object_id))
		definition.cost = int(row.get("cost", -1))
		definition.construction_ticks = int(row.get("constructionTicks", -1))
		definition.maximum_health = int(row.get("maximumHealth", -1))
		var footprint: Dictionary = row.get("footprint", {})
		definition.width_cells = int(footprint.get("widthCells", 0))
		definition.height_cells = int(footprint.get("heightCells", 0))
		definition.build_menu_slot = int(row.get("buildMenuSlot", -1))
		var income: Dictionary = row.get("income", {})
		definition.income_amount = int(income.get("amount", 0))
		definition.income_interval_ticks = int(income.get("intervalTicks", 0))
		for value in row.get("trains", []):
			definition.train_type_codes.append(int(value))
		definition.train_type_codes.sort()
		definitions.append(definition)
	definitions.sort_custom(func(a: Stage2Types.BuildingDefinition, b: Stage2Types.BuildingDefinition) -> bool: return a.type_code < b.type_code)
	blueprints.clear()
	for row: Dictionary in data.get("hordeBlueprints", []):
		var blueprint := Stage2Types.UnitBlueprint.new(int(row.get("typeCode", -1)))
		blueprint.id = String(row.get("id", ""))
		blueprint.display_name = String(row.get("displayName", blueprint.id))
		blueprint.member_count = int(row.get("memberCount", 0))
		blueprint.ranged_count = int(row.get("rangedCount", 0))
		blueprint.cost = int(row.get("cost", -1))
		blueprint.production_ticks = int(row.get("productionTicks", 0))
		blueprint.population = int(row.get("population", 0))
		blueprint.train_menu_slot = int(row.get("trainMenuSlot", -1))
		blueprints.append(blueprint)
	blueprints.sort_custom(func(a: Stage2Types.UnitBlueprint, b: Stage2Types.UnitBlueprint) -> bool: return a.type_code < b.type_code)
	for horde in world.hordes:
		horde_population[horde.id] = 1
	return validate_definitions()

func validate_definitions() -> String:
	if economies.size() != 2 or definitions.is_empty() or blueprints.is_empty():
		return "economy_counts"
	var prior := -1
	for definition in definitions:
		if definition.type_code <= prior or definition.role_code < 0 or definition.cost < 0 or definition.construction_ticks < 0 or definition.maximum_health <= 0 or definition.width_cells <= 0 or definition.height_cells <= 0:
			return "building_definition_%d" % definition.type_code
		if definition.role_code != Stage2Types.BuildingRole.FORTRESS and definition.construction_ticks == 0:
			return "building_construction_ticks_%d" % definition.type_code
		prior = definition.type_code
	prior = -1
	for blueprint in blueprints:
		if blueprint.type_code <= prior or blueprint.member_count <= 0 or blueprint.ranged_count < 0 or blueprint.ranged_count > blueprint.member_count or blueprint.cost < 0 or blueprint.production_ticks <= 0 or blueprint.population <= 0:
			return "blueprint_%d" % blueprint.type_code
		prior = blueprint.type_code
	return ""

func get_economy(team: int) -> Stage2Types.TeamEconomy:
	for economy in economies:
		if economy.team == team:
			return economy
	return null

func get_building(entity_id: int) -> Stage2Types.Building:
	for building in buildings:
		if building.id == entity_id:
			return building
	return null

func get_definition(type_code: int) -> Stage2Types.BuildingDefinition:
	for definition in definitions:
		if definition.type_code == type_code:
			return definition
	return null

func get_blueprint(type_code: int) -> Stage2Types.UnitBlueprint:
	for blueprint in blueprints:
		if blueprint.type_code == type_code:
			return blueprint
	return null

func can_place(world: Stage1World, team: int, type_code: int, position: Vector2i) -> bool:
	var economy := get_economy(team)
	var definition := get_definition(type_code)
	if economy == null or definition == null or definition.role_code == Stage2Types.BuildingRole.FORTRESS or economy.resources < definition.cost or not Stage2Placement.can_place(world.grid, position, definition):
		return false
	var footprint: Dictionary = {}
	for cell in Stage2Placement.footprint_cells(position, definition.width_cells, definition.height_cells):
		footprint[cell] = true
	for fortress in world.fortresses:
		if not fortress.is_alive():
			continue
		var checked_declared_footprint := false
		for fortress_definition in definitions:
			if fortress_definition.role_code != Stage2Types.BuildingRole.FORTRESS:
				continue
			checked_declared_footprint = true
			for fortress_cell in Stage2Placement.footprint_cells(fortress.position, fortress_definition.width_cells, fortress_definition.height_cells):
				if footprint.has(fortress_cell):
					return false
		if not checked_declared_footprint and footprint.has(world.grid.to_cell(fortress.position)):
			return false
	for horde in world.hordes:
		if not horde.is_alive():
			continue
		if footprint.has(world.grid.to_cell(horde.anchor)):
			return false
		for member in horde.members:
			if member.is_alive() and footprint.has(world.grid.to_cell(member.position)):
				return false
	return true

func schedule_place(team: int, type_code: int, position: Vector2i, tick_index: int, execute_tick: int = -1, sequence: int = -1) -> void:
	_schedule(Stage2Types.CommandKind.PLACE, team, 0, type_code, position, tick_index, execute_tick, sequence)

func schedule_train(building_id: int, type_code: int, tick_index: int, execute_tick: int = -1, sequence: int = -1) -> void:
	_schedule(Stage2Types.CommandKind.TRAIN, -1, building_id, type_code, Vector2i.ZERO, tick_index, execute_tick, sequence)

func schedule_rally(building_id: int, position: Vector2i, tick_index: int, execute_tick: int = -1, sequence: int = -1) -> void:
	_schedule(Stage2Types.CommandKind.RALLY, -1, building_id, 0, position, tick_index, execute_tick, sequence)

func _schedule(kind: int, team: int, building_id: int, type_code: int, destination: Vector2i, tick_index: int, execute_tick: int, sequence: int) -> void:
	var actual_tick := tick_index + 1 if execute_tick < 0 else execute_tick
	var actual_sequence := _next_command_sequence if sequence < 0 else sequence
	_next_command_sequence = maxi(_next_command_sequence, actual_sequence + 1)
	commands.append(Stage2Types.EconomyCommand.new(actual_tick, actual_sequence, kind, team, building_id, type_code, destination))
	commands.sort_custom(_command_before)

func _command_before(left: Stage2Types.EconomyCommand, right: Stage2Types.EconomyCommand) -> bool:
	if left.execute_tick != right.execute_tick:
		return left.execute_tick < right.execute_tick
	if left.sequence != right.sequence:
		return left.sequence < right.sequence
	if left.kind != right.kind:
		return left.kind < right.kind
	if left.team != right.team:
		return left.team < right.team
	if left.building_id != right.building_id:
		return left.building_id < right.building_id
	return left.type_code < right.type_code

func apply_commands(world: Stage1World) -> void:
	while next_command_index < commands.size() and commands[next_command_index].execute_tick <= world.tick_index:
		var command := commands[next_command_index]
		next_command_index += 1
		match command.kind:
			Stage2Types.CommandKind.PLACE:
				_place(world, command.team, command.type_code, command.destination)
			Stage2Types.CommandKind.TRAIN:
				_train(world, command.building_id, command.type_code)
			Stage2Types.CommandKind.RALLY:
				_set_rally(world, command.building_id, command.destination)

func _place(world: Stage1World, team: int, type_code: int, position: Vector2i) -> Stage2Types.Building:
	if not can_place(world, team, type_code, position):
		return null
	var definition := get_definition(type_code)
	var economy := get_economy(team)
	economy.resources -= definition.cost
	var building := Stage2Types.Building.new(next_building_id, type_code, team, position, definition.maximum_health, definition.construction_ticks)
	next_building_id += 1
	buildings.append(building)
	buildings.sort_custom(func(a: Stage2Types.Building, b: Stage2Types.Building) -> bool: return a.id < b.id)
	Stage2Placement.set_footprint_blocked(world.grid, building, definition, true)
	return building

func _set_rally(world: Stage1World, building_id: int, position: Vector2i) -> bool:
	var building := get_building(building_id)
	if building == null or building.destroyed or get_definition(building.type_code).role_code != Stage2Types.BuildingRole.PRODUCTION or position.x < 0 or position.x >= world.grid.width * Stage1Grid.CELL_SIZE or position.y < 0 or position.y >= world.grid.height * Stage1Grid.CELL_SIZE:
		return false
	building.rally_point = position
	building.has_rally_point = true
	return true

func _train(world: Stage1World, building_id: int, type_code: int) -> bool:
	var building := get_building(building_id)
	var blueprint := get_blueprint(type_code)
	if building == null or blueprint == null or building.destroyed or not building.completed:
		return false
	var definition := get_definition(building.type_code)
	var economy := get_economy(building.team)
	if definition == null or economy == null or not definition.train_type_codes.has(type_code) or building.jobs.size() >= rules.maximum_queue_length or economy.resources < blueprint.cost:
		return false
	if not Stage2Production.can_reserve(world, economy, buildings, horde_population, blueprint.population):
		return false
	economy.resources -= blueprint.cost
	building.jobs.append(Stage2Types.ProductionJob.new(next_job_id, type_code, blueprint.production_ticks, blueprint.population, world.tick_index))
	next_job_id += 1
	return true

func advance_construction(world: Stage1World) -> void:
	for building in buildings:
		Stage2Construction.advance(building, get_definition(building.type_code), world.tick_index)

func advance_income(world: Stage1World) -> void:
	_refresh_efficiency()
	for building in buildings:
		if building.destroyed or not building.completed:
			continue
		var definition := get_definition(building.type_code)
		if definition.role_code != Stage2Types.BuildingRole.RESOURCE or definition.income_interval_ticks <= 0 or building.next_income_tick < 0 or world.tick_index < building.next_income_tick:
			continue
		var payout := definition.income_amount * building.efficiency_permille / 1000
		var economy := get_economy(building.team)
		economy.resources += payout
		economy.total_earned += payout
		building.next_income_tick += definition.income_interval_ticks

func farm_efficiency(building: Stage2Types.Building) -> int:
	var definition := get_definition(building.type_code)
	if definition == null or definition.role_code != Stage2Types.BuildingRole.RESOURCE or building.destroyed or not building.completed:
		return 0
	var radius_squared := rules.efficiency_radius_subcells * rules.efficiency_radius_subcells
	var neighbors := 0
	for candidate in buildings:
		if candidate.id == building.id or candidate.team != building.team or candidate.destroyed or not candidate.completed or get_definition(candidate.type_code).role_code != Stage2Types.BuildingRole.RESOURCE:
			continue
		var delta := candidate.position - building.position
		if delta.x * delta.x + delta.y * delta.y <= radius_squared:
			neighbors += 1
	return maxi(rules.minimum_efficiency_permille, rules.base_efficiency_permille - neighbors * rules.neighbor_penalty_permille)

func _refresh_efficiency() -> void:
	for building in buildings:
		building.efficiency_permille = farm_efficiency(building)

func advance_production(world: Stage1World) -> void:
	for building in buildings:
		if building.destroyed or not building.completed or building.jobs.is_empty():
			continue
		var job := building.jobs[0]
		job.remaining_ticks -= 1
		if job.remaining_ticks > 0:
			continue
		var spawn := Stage2Placement.find_spawn(world.grid, building.position, rules.spawn_search_radius_cells, false)
		if spawn.x < 0:
			job.remaining_ticks = 0
			continue
		var blueprint := get_blueprint(job.type_code)
		var horde := world.add_horde_composition(building.team, spawn, blueprint.member_count, blueprint.ranged_count)
		horde_population[horde.id] = blueprint.population
		building.jobs.pop_front()
		if building.has_rally_point:
			var rally := Stage2Placement.find_spawn(world.grid, building.rally_point, rules.spawn_search_radius_cells, true)
			if rally.x >= 0 and rally != spawn:
				world.schedule_command(horde.id, Stage1Types.OrderKind.MOVE, rally, 0, world.tick_index)

func damage_building(world: Stage1World, entity_id: int, amount: int) -> bool:
	var building := get_building(entity_id)
	if building == null or building.destroyed or amount <= 0:
		return false
	var destroyed := Stage2Construction.apply_damage(building, amount)
	if not destroyed:
		return true
	var definition := get_definition(building.type_code)
	Stage2Placement.set_footprint_blocked(world.grid, building, definition, false)
	building.jobs.clear()
	building.next_income_tick = -1
	return true

func population_used(world: Stage1World, team: int) -> int:
	var economy := get_economy(team)
	return 0 if economy == null else Stage2Production.used_population(world, economy, horde_population)

func population_reserved(team: int) -> int:
	var economy := get_economy(team)
	return 0 if economy == null else Stage2Production.reserved_population(economy, buildings)

func validate_state(world: Stage1World) -> String:
	for economy in economies:
		if economy.resources < 0 or economy.total_earned < 0 or economy.population_cap < 0 or population_used(world, economy.team) + population_reserved(economy.team) > economy.population_cap:
			return "economy_team_%d" % economy.team
	var ids: Dictionary = {}
	for building in buildings:
		var definition := get_definition(building.type_code)
		if ids.has(building.id) or definition == null or building.health < 0 or building.health > building.construction_health_cap or building.construction_health_cap > building.maximum_health or building.progress_ticks < 0 or building.progress_ticks > building.construction_ticks or building.completed != (building.progress_ticks >= building.construction_ticks) or building.destroyed != (building.health == 0):
			return "economy_building_%d" % building.id
		ids[building.id] = true
		for job in building.jobs:
			if job.remaining_ticks < 0 or get_blueprint(job.type_code) == null:
				return "economy_job_%d" % job.id
	return ""
