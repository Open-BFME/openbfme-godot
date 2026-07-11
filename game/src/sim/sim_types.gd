class_name SimTypes
extends RefCounted

enum OrderType { NONE, MOVE, ATTACK, ATTACK_MOVE, STOP, GARRISON, BUILD, TRAIN }
enum EntityKind { BATTALION, BUILDING, PROJECTILE }

class Order:
	var type: int = OrderType.NONE
	var dest: Vector2 = Vector2.ZERO
	var target_id: int = -1
	var target_kind: int = EntityKind.BATTALION
	var ability_id: String = ""
	var meta: Dictionary = {}

class Battalion:
	var id: int = 0
	var type_id: String = ""
	var side: int = 0
	var faction: String = ""
	var pos: Vector2 = Vector2.ZERO
	var facing: float = 0.0
	var hp: float = 1.0
	var hp_max: float = 1.0
	var dead: bool = false
	var stance: int = 0 # GameState.Stance.AGGRESSIVE
	var order := Order.new()
	var attack_cd: float = 0.0
	var target_id: int = -1
	var target_kind: int = EntityKind.BATTALION
	var home: Vector2 = Vector2.ZERO
	var rank: int = 1
	var xp: float = 0.0
	var members: int = 1
	var member_cap: int = 1
	var fear_t: float = 0.0
	var fear_immune_t: float = 0.0
	var knockdown_t: float = 0.0
	var cowering: bool = false
	var dmg_buff_t: float = 0.0
	var dmg_buff_mul: float = 1.0
	var dmg_debuff_t: float = 0.0
	var dmg_debuff_mul: float = 1.0
	var ability_cds: Dictionary = {}
	var out_of_combat_t: float = 0.0
	var mounted: bool = false
	var garrisoned_in: int = -1
	var radius: float = 1.2
	var selected: bool = false
	var move_goal: Vector2 = Vector2.ZERO
	var has_move_goal: bool = false
	var summon_life: float = 0.0 # >0 means temporary summon
	var trample_cd: float = 0.0

class Building:
	var id: int = 0
	var type_id: String = ""
	var side: int = 0
	var faction: String = ""
	var pos: Vector2 = Vector2.ZERO
	var facing: float = 0.0
	var hp: float = 1.0
	var hp_max: float = 1.0
	var dead: bool = false
	var built: bool = true
	var build_progress: float = 1.0
	var train_queue: Array = [] # Array of {kind, id, t, tmax}
	var rally: Vector2 = Vector2.ZERO
	var has_rally: bool = false
	var radius: float = 4.0
	var is_fortress: bool = false
	var is_wall: bool = false
	var is_gate: bool = false
	var is_tower: bool = false
	var gate_open: bool = true
	var tower_cd: float = 0.0
	var garrison: Array[int] = []
	var income_t: float = 0.0

class Projectile:
	var id: int = 0
	var pos: Vector2 = Vector2.ZERO
	var vel: Vector2 = Vector2.ZERO
	var target_id: int = -1
	var target_kind: int = EntityKind.BATTALION
	var dmg: float = 0.0
	var speed: float = 40.0
	var lifetime: float = 3.0
	var side: int = 0
	var source_id: int = -1
	var dead: bool = false
	var splash: float = 0.0
