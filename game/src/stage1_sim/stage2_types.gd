class_name Stage2Types
extends RefCounted
## Integer-only authoritative economy, construction, and production records.

enum CommandKind { PLACE = 0, TRAIN = 1, RALLY = 2 }
enum BuildingRole { FORTRESS = 0, RESOURCE = 1, PRODUCTION = 2 }

class Rules extends RefCounted:
	var version: int = 1
	var maximum_queue_length: int = 5
	var construction_health_ramp_code: int = 1
	var building_blocks_at_code: int = 0
	var queued_population_counts_code: int = 1
	var spawn_search_order: Array[int] = [0, 1, 2, 3]
	var efficiency_radius_subcells: int = 10000
	var base_efficiency_permille: int = 1000
	var neighbor_penalty_permille: int = 250
	var minimum_efficiency_permille: int = 250
	var spawn_search_radius_cells: int = 6

class TeamEconomy extends RefCounted:
	var team: int
	var resources: int
	var total_earned: int = 0
	var population_cap: int

	func _init(p_team: int, p_resources: int, p_population_cap: int) -> void:
		team = p_team
		resources = p_resources
		population_cap = p_population_cap

class BuildingDefinition extends RefCounted:
	var type_code: int
	var object_id: String
	var display_name: String
	var role_code: int
	var cost: int
	var construction_ticks: int
	var maximum_health: int
	var width_cells: int
	var height_cells: int
	var build_menu_slot: int
	var income_amount: int
	var income_interval_ticks: int
	var train_type_codes: Array[int] = []

	func _init(p_type_code: int, p_role_code: int) -> void:
		type_code = p_type_code
		role_code = p_role_code

class UnitBlueprint extends RefCounted:
	var type_code: int
	var id: String
	var display_name: String
	var member_count: int
	var ranged_count: int
	var cost: int
	var production_ticks: int
	var population: int
	var train_menu_slot: int

	func _init(p_type_code: int) -> void:
		type_code = p_type_code

class ProductionJob extends RefCounted:
	var id: int
	var type_code: int
	var remaining_ticks: int
	var population_reserved: int
	var enqueued_tick: int

	func _init(p_id: int, p_type_code: int, p_remaining_ticks: int, p_population_reserved: int, p_enqueued_tick: int) -> void:
		id = p_id
		type_code = p_type_code
		remaining_ticks = p_remaining_ticks
		population_reserved = p_population_reserved
		enqueued_tick = p_enqueued_tick

class Building extends RefCounted:
	var id: int
	var type_code: int
	var team: int
	var position: Vector2i
	var health: int = 0
	var maximum_health: int
	var construction_health_cap: int = 1
	var progress_ticks: int = 0
	var construction_ticks: int
	var damage_taken: int = 0
	var completed: bool = false
	var destroyed: bool = false
	var efficiency_permille: int = 1000
	var next_income_tick: int = -1
	var rally_point: Vector2i
	var has_rally_point: bool = false
	var jobs: Array[ProductionJob] = []

	func _init(p_id: int, p_type_code: int, p_team: int, p_position: Vector2i, p_maximum_health: int, p_construction_ticks: int) -> void:
		id = p_id
		type_code = p_type_code
		team = p_team
		position = p_position
		maximum_health = p_maximum_health
		construction_ticks = p_construction_ticks
		health = 1
		rally_point = p_position

	func is_alive() -> bool:
		return not destroyed and health > 0

	func is_complete() -> bool:
		return completed

class EconomyCommand extends RefCounted:
	var execute_tick: int
	var sequence: int
	var kind: int
	var team: int
	var building_id: int
	var type_code: int
	var destination: Vector2i

	func _init(p_tick: int, p_sequence: int, p_kind: int, p_team: int, p_building_id: int, p_type_code: int, p_destination: Vector2i) -> void:
		execute_tick = p_tick
		sequence = p_sequence
		kind = p_kind
		team = p_team
		building_id = p_building_id
		type_code = p_type_code
		destination = p_destination
