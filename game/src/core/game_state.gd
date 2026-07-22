extends Node
## Match-level state. Pure data + sim world reference.

enum Side { PLAYER = 0, ENEMY = 1, WILD = 2 }
enum Stance { AGGRESSIVE = 0, DEFENSIVE = 1, HOLD = 2 }

const SIDE_PLAYER := Side.PLAYER
const SIDE_ENEMY := Side.ENEMY
const SIDE_WILD := Side.WILD

var started: bool = false
var game_over: bool = false
var winner_side: int = -1
var paused: bool = false

var player_faction: String = "gondor"
var enemy_faction: String = "mordor"
var map_id: String = "anduin"
var difficulty: String = "normal"

## Retail vertical slice skirmish selection, written by the main menu's
## skirmish setup. The slice reads these only when OPENBFME_SLICE_FACTION is
## unset; the environment variable always wins for headless runners.
var retail_player_faction: String = "men"
var retail_enemy_faction: String = "men"
## RULES tab values for the retail slice. -1 keeps the slice's authored
## starting resources; the factor multiplies its authored command point cap.
var retail_initial_resources: int = -1
var retail_command_point_factor: float = 1.0
## Player start spot: 0 keeps the authored assignment (the human takes
## Player_2_Start); 1..N takes retail Player_N_Start and the AI takes another.
var retail_player_start_index: int = 0
## House colors chosen on the setup screen. Defaults are the authored slice
## team colors (Gondor blue vs red), so an untouched setup is byte-identical.
var retail_player_color: Color = Color(0.176, 0.302, 0.675)
var retail_enemy_color: Color = Color(0.651, 0.125, 0.110)
## Retail vertical slice map selection (bfme2.map.<slug>). The slice reads it
## only when OPENBFME_SLICE_MAP is unset; empty keeps the Fords of Isen II
## default. Selectable ids: bfme2.map.fords-of-isen-ii, bfme2.map.rivendell,
## bfme2.map.mount-doom, bfme2.map.dagorlad, bfme2.map.mordor.
var retail_map_id: String = ""

var resources: Dictionary = {0: 1500.0, 1: 1500.0, 2: 0.0}
var power_points: Dictionary = {0: 0.0, 1: 0.0}
var power_spent_tiers: Dictionary = {0: 0, 1: 0} # count of powers purchased for tier unlock
var unlocked_powers: Dictionary = {0: [], 1: []} # power ids bought
var upgrades: Dictionary = {0: {}, 1: {}} # research id -> true
var selected_ids: Array[int] = []
var control_groups: Dictionary = {} # 1-9 -> Array[int]
var formation: String = "line"

var hero_deaths: Dictionary = {}
var dead_heroes: Dictionary = {}
var weather: Dictionary = {} # {id, until, enemy_dmg_mul}
var stats: Dictionary = {}
var ring: Dictionary = {
	"active": false, "pos": Vector2.ZERO, "holder_id": -1, "dropped": false
}
var farsight_until: float = 0.0
var quake_shake_t: float = 0.0

var world = null
var ai = null # SkirmishAI
var stage_flags: Dictionary = {
	"economy": true, "walls": true, "fog": false, "heroes": true,
	"abilities": true, "veterancy": true, "stances": true, "fear": true,
	"revival": true, "powers": true, "research": true, "ai": true, "ring": true,
}

func default_stats() -> Dictionary:
	return {
		"units_trained": 0, "units_lost": 0, "enemies_slain": 0,
		"buildings_razed": 0, "powers_cast": 0, "gold_spent": 0.0,
	}

func reset_match() -> void:
	started = false
	game_over = false
	winner_side = -1
	paused = false
	resources = {
		0: float(ContentDB.globals.get("start_resources", 1500)),
		1: float(ContentDB.globals.get("start_resources", 1500)),
		2: 0.0,
	}
	power_points = {0: 0.0, 1: 0.0}
	power_spent_tiers = {0: 0, 1: 0}
	unlocked_powers = {0: [], 1: []}
	upgrades = {0: {}, 1: {}}
	selected_ids.clear()
	control_groups.clear()
	formation = "line"
	hero_deaths.clear()
	dead_heroes = {0: [], 1: []}
	weather = {}
	stats = default_stats()
	ring = {"active": false, "pos": Vector2.ZERO, "holder_id": -1, "dropped": false}
	farsight_until = 0.0
	quake_shake_t = 0.0
	world = null
	ai = null
	SimClock.reset()
	SimClock.paused = false
	SimClock.game_speed = 1.0

func set_selected(ids: Array) -> void:
	selected_ids.clear()
	for id in ids:
		selected_ids.append(int(id))
	Events.selection_changed.emit(selected_ids.duplicate())

func add_resources(side: int, amount: float) -> void:
	resources[side] = float(resources.get(side, 0.0)) + amount
	Events.resources_changed.emit(side, resources[side])

func spend(side: int, amount: float) -> bool:
	if float(resources.get(side, 0.0)) < amount:
		if side == SIDE_PLAYER:
			Events.toast.emit("Not enough resources")
		return false
	resources[side] = float(resources[side]) - amount
	stats["gold_spent"] = float(stats.get("gold_spent", 0.0)) + amount
	Events.resources_changed.emit(side, resources[side])
	return true

func spend_pp(side: int, amount: float) -> bool:
	if float(power_points.get(side, 0.0)) < amount:
		if side == SIDE_PLAYER:
			Events.toast.emit("Not enough Power Points")
		return false
	power_points[side] = float(power_points[side]) - amount
	return true

func add_pp(side: int, amount: float) -> void:
	power_points[side] = float(power_points.get(side, 0.0)) + amount

func has_upgrade(side: int, key: String) -> bool:
	var u: Dictionary = upgrades.get(side, {})
	return bool(u.get(key, false))

func set_upgrade(side: int, key: String) -> void:
	if not upgrades.has(side):
		upgrades[side] = {}
	upgrades[side][key] = true

func power_unlocked(side: int, power_id: String) -> bool:
	var list: Array = unlocked_powers.get(side, [])
	return list.has(power_id)

func can_unlock_power(side: int, power_id: String) -> bool:
	var p: Dictionary = ContentDB.get_power(power_id)
	if p.is_empty():
		return false
	if power_unlocked(side, power_id):
		return false
	var need := int(p.get("requires_tier_spent", 0))
	return int(power_spent_tiers.get(side, 0)) >= need

func unlock_power(side: int, power_id: String) -> bool:
	if not can_unlock_power(side, power_id):
		return false
	var p: Dictionary = ContentDB.get_power(power_id)
	var cost := float(p.get("cost_pp", 5))
	if not spend_pp(side, cost):
		return false
	var list: Array = unlocked_powers.get(side, [])
	list.append(power_id)
	unlocked_powers[side] = list
	power_spent_tiers[side] = int(power_spent_tiers.get(side, 0)) + 1
	return true

func enemy_of(a_side: int, b_side: int) -> bool:
	if a_side == SIDE_WILD or b_side == SIDE_WILD:
		return a_side != b_side
	return a_side != b_side

func set_control_group(n: int, ids: Array) -> void:
	var arr: Array[int] = []
	for id in ids:
		arr.append(int(id))
	control_groups[n] = arr

func recall_control_group(n: int) -> void:
	if control_groups.has(n):
		set_selected(control_groups[n])
