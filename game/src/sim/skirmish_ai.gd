class_name SkirmishAI
extends RefCounted
## Budgeted skirmish AI: economy → production → waves. Difficulty from globals.

var side: int = GameState.SIDE_ENEMY
var faction_id: String = "mordor"
var difficulty: String = "normal"
var bank_construction: float = 0.0
var bank_troops: float = 0.0
var bank_tech: float = 0.0
var wave_timer: float = 25.0
var build_i: int = 0
var prod_i: int = 0
var hero_spawned: bool = false
var counters: Dictionary = {
	"builds": 0, "trains": 0, "waves": 0, "research": 0, "orders": 0
}
var _plan: Dictionary = {}
var _diff: Dictionary = {}

func setup(p_side: int, p_faction: String, p_diff: String) -> void:
	side = p_side
	faction_id = p_faction
	difficulty = p_diff
	var f: Dictionary = ContentDB.get_faction(faction_id)
	_plan = f.get("ai_plan", {})
	var diffs: Dictionary = ContentDB.globals.get("difficulties", {})
	_diff = diffs.get(difficulty, diffs.get("normal", {}))
	bank_construction = 200.0
	bank_troops = 250.0
	bank_tech = 80.0
	wave_timer = 20.0
	build_i = 0
	prod_i = 0
	hero_spawned = false

func tick(world: SimWorld, dt: float) -> void:
	if GameState.game_over:
		return
	# income into banks
	var mul := float(_diff.get("income_mul", 1.0))
	# AI also receives from farms via GameState.resources; siphon into banks
	var res: float = float(GameState.resources.get(side, 0.0))
	if res > 50.0:
		var take := minf(res * 0.15, 40.0) * dt * 2.0
		if take > 0.0 and GameState.spend(side, take):
			bank_construction += take * 0.4 * mul
			bank_troops += take * 0.45 * mul
			bank_tech += take * 0.15 * mul
	_try_build(world)
	_try_research(world)
	_try_train(world)
	_try_hero(world)
	wave_timer -= dt
	if wave_timer <= 0.0:
		_launch_wave(world)
		var base_cd := 35.0 / maxf(0.5, float(_diff.get("wave_mul", 1.0)))
		wave_timer = base_cd + world.rng.randf_range(-4.0, 6.0)

func _fortress(world: SimWorld):
	return world.fortress_for(side)

func _base_pos(world: SimWorld) -> Vector2:
	var f = _fortress(world)
	if f:
		return f.pos
	return Vector2(70, 70) if side == GameState.SIDE_ENEMY else Vector2(-70, -70)

func _try_build(world: SimWorld) -> void:
	var econ: Array = _plan.get("economy", [])
	var prod: Array = _plan.get("production", [])
	var queue: Array = []
	queue.append_array(econ)
	queue.append_array(prod)
	if build_i >= queue.size():
		return
	var type_id := String(queue[build_i])
	var def: Dictionary = ContentDB.get_building(type_id)
	var cost := float(def.get("cost", 300))
	if bank_construction < cost:
		return
	# count existing of type
	var have := 0
	for id in world.buildings:
		var b = world.buildings[id]
		if not b.dead and b.side == side and b.type_id == type_id:
			have += 1
	var want := 1
	if econ.has(type_id):
		want = econ.count(type_id)
	if have >= want and not prod.has(type_id):
		build_i += 1
		return
	if have >= 1 and prod.has(type_id):
		build_i += 1
		return
	var base := _base_pos(world)
	var ang := float(build_i) * 0.9
	var pos := base + Vector2(cos(ang), sin(ang)) * (16.0 + float(build_i) * 3.0)
	# AI places finished buildings (skip long construction softlock)
	if bank_construction < cost:
		return
	if not world.can_place(type_id, side, pos):
		build_i += 1
		return
	bank_construction -= cost
	var placed = world.spawn_building(type_id, side, pos, true, faction_id)
	if placed:
		counters["builds"] = int(counters.get("builds", 0)) + 1
		build_i += 1
	else:
		bank_construction += cost

func _try_research(world: SimWorld) -> void:
	var keys := ["forged_blades", "fire_arrows", "heavy_armor"]
	for k in keys:
		if GameState.has_upgrade(side, k):
			continue
		var rdef: Dictionary = ContentDB.get_research(k)
		if rdef.is_empty():
			continue
		var cost := float(rdef.get("cost", 400))
		if bank_tech < cost:
			continue
		# find a building that offers research
		for id in world.buildings:
			var b = world.buildings[id]
			if b.dead or b.side != side or not b.built:
				continue
			var bdef: Dictionary = ContentDB.get_building(b.type_id)
			var list: Array = bdef.get("research", [])
			if list.has(k):
				GameState.resources[side] = float(GameState.resources.get(side, 0.0)) + cost
				if world.enqueue_research(b.id, k):
					bank_tech -= cost
					counters["research"] = int(counters.get("research", 0)) + 1
				return

func _try_train(world: SimWorld) -> void:
	if world.count_battalions(side) >= SimWorld.MAX_BATTALIONS_PER_SIDE:
		return
	var army: Array = _plan.get("army", ["orc"])
	if army.is_empty():
		return
	var type_id := String(army[prod_i % army.size()])
	var udef: Dictionary = ContentDB.get_unit(type_id)
	var cost := float(udef.get("cost", 150))
	# fund troops bank from side resources if needed
	if bank_troops < cost:
		var need := cost - bank_troops
		if float(GameState.resources.get(side, 0.0)) >= need:
			if GameState.spend(side, need):
				bank_troops += need
	if bank_troops < cost:
		return
	for id in world.buildings:
		var b = world.buildings[id]
		if b.dead or b.side != side or not b.built:
			continue
		if b.train_queue.size() > 0:
			continue
		var bdef: Dictionary = ContentDB.get_building(b.type_id)
		var trains: Array = bdef.get("trains", [])
		if trains.has(type_id):
			GameState.resources[side] = float(GameState.resources.get(side, 0.0)) + cost
			if world.enqueue_train(b.id, type_id):
				bank_troops -= cost
				prod_i += 1
				counters["trains"] = int(counters.get("trains", 0)) + 1
			return
	# force-spawn if production exists but queue blocked
	for id2 in world.buildings:
		var b2 = world.buildings[id2]
		if b2.dead or b2.side != side or not b2.built:
			continue
		var bdef2: Dictionary = ContentDB.get_building(b2.type_id)
		if (bdef2.get("trains", []) as Array).has(type_id):
			var u = world.spawn_battalion(type_id, side, b2.pos + Vector2(6, 0), faction_id)
			if u:
				bank_troops -= cost
				prod_i += 1
				counters["trains"] = int(counters.get("trains", 0)) + 1
			return

func _try_hero(world: SimWorld) -> void:
	if hero_spawned:
		return
	if world.count_battalions(side) < 3:
		return
	var hero := String(_plan.get("hero", ""))
	if hero == "":
		return
	var udef: Dictionary = ContentDB.get_unit(hero)
	var cost := float(udef.get("cost", 800))
	if bank_troops < cost * 0.5:
		return
	var fort = _fortress(world)
	if fort == null:
		return
	GameState.resources[side] = float(GameState.resources.get(side, 0.0)) + cost
	if world.enqueue_train(fort.id, hero):
		bank_troops -= cost * 0.5
		hero_spawned = true
		counters["trains"] = int(counters.get("trains", 0)) + 1

func _launch_wave(world: SimWorld) -> void:
	var target = world.fortress_for(GameState.SIDE_PLAYER if side == GameState.SIDE_ENEMY else GameState.SIDE_ENEMY)
	if target == null:
		return
	var n := 0
	var want := int(round(float(_plan.get("wave_size", 4)) * float(_diff.get("wave_mul", 1.0))))
	for id in world.battalions:
		var b = world.battalions[id]
		if b.dead or b.side != side:
			continue
		if n >= want and b.order.type != SimTypes.OrderType.NONE and b.order.type != SimTypes.OrderType.STOP:
			continue
		b.order.type = SimTypes.OrderType.ATTACK_MOVE
		b.has_move_goal = true
		b.move_goal = target.pos + Vector2(world.rng.randf_range(-10, 10), world.rng.randf_range(-10, 10))
		b.target_id = -1
		n += 1
		counters["orders"] = int(counters.get("orders", 0)) + 1
	if n > 0:
		counters["waves"] = int(counters.get("waves", 0)) + 1
