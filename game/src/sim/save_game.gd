class_name SaveGame
extends RefCounted
## Serialize / restore match fields for mid-combat save/load.

static func serialize(world: SimWorld) -> Dictionary:
	var bats: Array = []
	for id in world.battalions:
		var b = world.battalions[id]
		bats.append({
			"id": b.id, "type_id": b.type_id, "side": b.side, "faction": b.faction,
			"pos": [b.pos.x, b.pos.y], "hp": b.hp, "hp_max": b.hp_max, "dead": b.dead,
			"rank": b.rank, "xp": b.xp, "stance": b.stance,
			"order_type": b.order.type, "move_goal": [b.move_goal.x, b.move_goal.y],
			"has_move_goal": b.has_move_goal, "target_id": b.target_id,
			"ability_cds": b.ability_cds.duplicate(),
		})
	var buds: Array = []
	for id in world.buildings:
		var b = world.buildings[id]
		buds.append({
			"id": b.id, "type_id": b.type_id, "side": b.side, "faction": b.faction,
			"pos": [b.pos.x, b.pos.y], "hp": b.hp, "hp_max": b.hp_max, "dead": b.dead,
			"built": b.built, "build_progress": b.build_progress,
			"train_queue": b.train_queue.duplicate(true),
			"gate_open": b.gate_open,
		})
	return {
		"version": 1,
		"time": SimClock.time,
		"tick": SimClock.tick_index,
		"player_faction": GameState.player_faction,
		"enemy_faction": GameState.enemy_faction,
		"map_id": GameState.map_id,
		"difficulty": GameState.difficulty,
		"resources": GameState.resources.duplicate(),
		"power_points": GameState.power_points.duplicate(),
		"power_spent_tiers": GameState.power_spent_tiers.duplicate(),
		"unlocked_powers": GameState.unlocked_powers.duplicate(),
		"upgrades": GameState.upgrades.duplicate(true),
		"hero_deaths": GameState.hero_deaths.duplicate(),
		"dead_heroes": GameState.dead_heroes.duplicate(true),
		"stats": GameState.stats.duplicate(),
		"weather": GameState.weather.duplicate(true) if GameState.weather else {},
		"next_id": world.next_id,
		"battalions": bats,
		"buildings": buds,
		"game_over": GameState.game_over,
		"winner_side": GameState.winner_side,
	}

static func deserialize(world: SimWorld, data: Dictionary) -> bool:
	if int(data.get("version", 0)) < 1:
		return false
	world.clear()
	GameState.player_faction = String(data.get("player_faction", "gondor"))
	GameState.enemy_faction = String(data.get("enemy_faction", "mordor"))
	GameState.map_id = String(data.get("map_id", "anduin_sandbox"))
	GameState.difficulty = String(data.get("difficulty", "normal"))
	GameState.resources = data.get("resources", {0: 1500.0, 1: 1500.0, 2: 0.0})
	GameState.power_points = data.get("power_points", {0: 0.0, 1: 0.0})
	GameState.power_spent_tiers = data.get("power_spent_tiers", {0: 0, 1: 0})
	GameState.unlocked_powers = data.get("unlocked_powers", {0: [], 1: []})
	GameState.upgrades = data.get("upgrades", {0: {}, 1: {}})
	GameState.hero_deaths = data.get("hero_deaths", {})
	GameState.dead_heroes = data.get("dead_heroes", {0: [], 1: []})
	GameState.stats = data.get("stats", GameState.default_stats())
	GameState.weather = data.get("weather", {})
	GameState.game_over = bool(data.get("game_over", false))
	GameState.winner_side = int(data.get("winner_side", -1))
	SimClock.time = float(data.get("time", 0.0))
	SimClock.tick_index = int(data.get("tick", 0))
	world.next_id = int(data.get("next_id", 1))
	for raw in data.get("buildings", []):
		var d: Dictionary = raw
		var b := SimTypes.Building.new()
		b.id = int(d["id"])
		b.type_id = String(d["type_id"])
		b.side = int(d["side"])
		b.faction = String(d.get("faction", ""))
		var p: Array = d.get("pos", [0, 0])
		b.pos = Vector2(float(p[0]), float(p[1]))
		b.hp = float(d["hp"])
		b.hp_max = float(d["hp_max"])
		b.dead = bool(d.get("dead", false))
		b.built = bool(d.get("built", true))
		b.build_progress = float(d.get("build_progress", 1.0))
		b.train_queue = d.get("train_queue", [])
		b.gate_open = bool(d.get("gate_open", true))
		var def: Dictionary = ContentDB.get_building(b.type_id)
		b.radius = float(def.get("radius", 4.0))
		b.is_fortress = bool(def.get("fortress", false))
		b.is_wall = bool(def.get("wall", false))
		b.is_gate = bool(def.get("gate", false))
		b.is_tower = bool(def.get("tower", false))
		world.buildings[b.id] = b
	world.rebuild_obstacles()
	for raw in data.get("battalions", []):
		var d: Dictionary = raw
		var b := SimTypes.Battalion.new()
		b.id = int(d["id"])
		b.type_id = String(d["type_id"])
		b.side = int(d["side"])
		b.faction = String(d.get("faction", ""))
		var p: Array = d.get("pos", [0, 0])
		b.pos = Vector2(float(p[0]), float(p[1]))
		b.hp = float(d["hp"])
		b.hp_max = float(d["hp_max"])
		b.dead = bool(d.get("dead", false))
		b.rank = int(d.get("rank", 1))
		b.xp = float(d.get("xp", 0))
		b.stance = int(d.get("stance", 0))
		b.order.type = int(d.get("order_type", 0))
		var mg: Array = d.get("move_goal", [0, 0])
		b.move_goal = Vector2(float(mg[0]), float(mg[1]))
		b.has_move_goal = bool(d.get("has_move_goal", false))
		b.target_id = int(d.get("target_id", -1))
		b.ability_cds = d.get("ability_cds", {})
		var udef: Dictionary = ContentDB.get_unit(b.type_id)
		b.member_cap = int(udef.get("size", 1))
		b.members = maxi(1, int(ceil(b.member_cap * clampf(b.hp / maxf(1.0, b.hp_max), 0.0, 1.0))))
		world.battalions[b.id] = b
	GameState.started = true
	return true

static func save_to_user(slot: String, world: SimWorld) -> bool:
	var path := "user://saves/%s.json" % slot
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("user://saves"))
	var data := serialize(world)
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(JSON.stringify(data))
	return true

static func load_from_user(slot: String, world: SimWorld) -> bool:
	var path := "user://saves/%s.json" % slot
	if not FileAccess.file_exists(path):
		return false
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return false
	var json := JSON.new()
	if json.parse(f.get_as_text()) != OK:
		return false
	if typeof(json.data) != TYPE_DICTIONARY:
		return false
	return deserialize(world, json.data)
