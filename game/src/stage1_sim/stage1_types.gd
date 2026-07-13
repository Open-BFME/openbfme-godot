class_name Stage1Types
extends RefCounted
## Integer-only authoritative records for the Stage 1 proof.

enum Team { NONE = -1, BLUE = 0, RED = 1 }
enum OrderKind { STOP = 0, MOVE = 1, ATTACK_MOVE = 2, ATTACK_TARGET = 3 }

class Member extends RefCounted:
	var id: int
	var horde_id: int
	var team: int
	var slot: int
	var ranged: bool
	var position: Vector2i
	var previous_position: Vector2i
	var health: int = 100
	var max_health: int = 100
	var cooldown: int = 0
	var target_id: int = 0

	func _init(p_id: int, p_horde_id: int, p_team: int, p_slot: int, p_ranged: bool, p_position: Vector2i) -> void:
		id = p_id
		horde_id = p_horde_id
		team = p_team
		slot = p_slot
		ranged = p_ranged
		position = p_position
		previous_position = p_position

	func is_alive() -> bool:
		return health > 0

class Horde extends RefCounted:
	var id: int
	var team: int
	var anchor: Vector2i
	var previous_anchor: Vector2i
	var destination: Vector2i
	var order: int = OrderKind.STOP
	var target_id: int = 0
	var engaged_horde_id: int = 0
	var path: Array[Vector2i] = []
	var path_index: int = 0
	var path_revision: int = 0
	var members: Array[Member] = []

	func _init(p_id: int, p_team: int, p_anchor: Vector2i) -> void:
		id = p_id
		team = p_team
		anchor = p_anchor
		previous_anchor = p_anchor
		destination = p_anchor

	func alive_count() -> int:
		var result := 0
		for member in members:
			if member.is_alive():
				result += 1
		return result

	func is_alive() -> bool:
		return alive_count() > 0

class Fortress extends RefCounted:
	var id: int
	var team: int
	var position: Vector2i
	var health: int
	var max_health: int

	func _init(p_id: int, p_team: int, p_position: Vector2i, p_health: int = 5000) -> void:
		id = p_id
		team = p_team
		position = p_position
		health = p_health
		max_health = p_health

	func is_alive() -> bool:
		return health > 0

class Projectile extends RefCounted:
	var id: int
	var team: int
	var source_id: int
	var target_id: int
	var position: Vector2i
	var previous_position: Vector2i
	var last_target: Vector2i
	var damage: int = 18
	var active: bool = true

	func _init(p_id: int, p_team: int, p_source_id: int, p_target_id: int, p_position: Vector2i, p_target: Vector2i) -> void:
		id = p_id
		team = p_team
		source_id = p_source_id
		target_id = p_target_id
		position = p_position
		previous_position = p_position
		last_target = p_target

class Command extends RefCounted:
	var execute_tick: int
	var sequence: int
	var horde_id: int
	var kind: int
	var destination: Vector2i
	var target_id: int

	func _init(p_tick: int, p_sequence: int, p_horde_id: int, p_kind: int, p_destination: Vector2i, p_target_id: int = 0) -> void:
		execute_tick = p_tick
		sequence = p_sequence
		horde_id = p_horde_id
		kind = p_kind
		destination = p_destination
		target_id = p_target_id
