class_name SimWorld
extends RefCounted
## Pure simulation. No Node3D. Stages 1â€“4 systems live here.

var MAP_HALF: float = 210.0
const MAX_BATTALIONS_PER_SIDE := 16

var battalions: Dictionary = {} # id -> SimTypes.Battalion
var buildings: Dictionary = {}
var projectiles: Dictionary = {}
var next_id: int = 1
var obstacles: Array = [] # {pos: Vector2, radius: float, id: int, blocks_nav: bool, gate: bool, open: bool}
var fog_enabled: bool = true
var fog_w: int = 64
var fog_h: int = 64
var fog_explored: PackedByteArray = PackedByteArray() # player exploration
var fog_visible: PackedByteArray = PackedByteArray()
var rng := RandomNumberGenerator.new()

func _init(seed: int = 1337) -> void:
	rng.seed = seed
	MAP_HALF = float(ContentDB.globals.get("map_half", 210))
	fog_explored.resize(fog_w * fog_h)
	fog_visible.resize(fog_w * fog_h)
	fog_explored.fill(0)
	fog_visible.fill(0)

func clear() -> void:
	battalions.clear()
	buildings.clear()
	projectiles.clear()
	obstacles.clear()
	next_id = 1
	fog_explored.fill(0)
	fog_visible.fill(0)

func _uid() -> int:
	var id := next_id
	next_id += 1
	return id

# ---------- spawn ----------

func spawn_battalion(type_id: String, side: int, pos: Vector2, faction: String = ""):
	var def: Dictionary = ContentDB.get_unit(type_id)
	if def.is_empty():
		push_error("Unknown unit type: %s" % type_id)
		return null
	var b := SimTypes.Battalion.new()
	b.id = _uid()
	b.type_id = type_id
	b.side = side
	b.faction = faction if faction != "" else String(def.get("faction", ""))
	b.pos = pos
	b.home = pos
	b.hp_max = float(def.get("hp", 100))
	b.hp = b.hp_max
	b.member_cap = int(def.get("size", 1))
	b.members = b.member_cap
	b.radius = 1.0 + 0.15 * b.member_cap
	b.rank = 1
	battalions[b.id] = b
	Events.entity_spawned.emit(&"battalion", b.id)
	return b

func spawn_building(type_id: String, side: int, pos: Vector2, instant: bool = true, faction: String = ""):
	var def: Dictionary = ContentDB.get_building(type_id)
	if def.is_empty():
		push_error("Unknown building type: %s" % type_id)
		return null
	var b := SimTypes.Building.new()
	b.id = _uid()
	b.type_id = type_id
	b.side = side
	b.faction = faction if faction != "" else String(def.get("faction", ""))
	b.pos = pos
	b.hp_max = float(def.get("hp", 1000))
	b.radius = float(def.get("radius", 4.0))
	b.is_fortress = bool(def.get("fortress", false))
	b.is_wall = bool(def.get("wall", false))
	b.is_gate = bool(def.get("gate", false))
	b.is_tower = bool(def.get("tower", false))
	b.gate_open = true
	if instant:
		b.built = true
		b.build_progress = 1.0
		b.hp = b.hp_max
	else:
		b.built = false
		b.build_progress = 0.0
		b.hp = b.hp_max * 0.15
	buildings[b.id] = b
	_register_obstacle(b)
	Events.entity_spawned.emit(&"building", b.id)
	return b

func _register_obstacle(b: SimTypes.Building) -> void:
	if b.dead:
		return
	# gates: block enemies only via custom path check; still obstacle when closed
	obstacles.append({
		"id": b.id,
		"pos": b.pos,
		"radius": b.radius * 0.9,
		"blocks_nav": not b.is_gate or not b.gate_open,
		"gate": b.is_gate,
		"open": b.gate_open,
		"side": b.side,
	})

func rebuild_obstacles() -> void:
	obstacles.clear()
	for id in buildings:
		var b: SimTypes.Building = buildings[id]
		if not b.dead and (b.built or b.build_progress > 0.05):
			_register_obstacle(b)

# ---------- queries ----------

func get_battalion(id: int):
	return battalions.get(id, null)

func get_building(id: int):
	return buildings.get(id, null)

func living_battalions() -> Array:
	var out: Array = []
	for id in battalions:
		var b: SimTypes.Battalion = battalions[id]
		if not b.dead and b.garrisoned_in < 0:
			out.append(b)
	return out

func living_buildings() -> Array:
	var out: Array = []
	for id in buildings:
		var b: SimTypes.Building = buildings[id]
		if not b.dead:
			out.append(b)
	return out

func fortress_for(side: int):
	for id in buildings:
		var b: SimTypes.Building = buildings[id]
		if not b.dead and b.side == side and b.is_fortress:
			return b
	return null

func count_battalions(side: int) -> int:
	var n := 0
	for id in battalions:
		var b: SimTypes.Battalion = battalions[id]
		if not b.dead and b.side == side:
			n += 1
	return n

# ---------- orders ----------

func order_move(ids: Array, dest: Vector2, formation: String = "") -> void:
	var form := formation if formation != "" else String(GameState.formation)
	var living: Array = []
	for id in ids:
		var b: SimTypes.Battalion = get_battalion(int(id))
		if b == null or b.dead:
			continue
		if b.fear_t > 0.0 or b.knockdown_t > 0.0:
			continue
		living.append(b)
	var i := 0
	for b in living:
		var slot := formation_slot(i, living.size(), form)
		b.order.type = SimTypes.OrderType.MOVE
		b.order.dest = dest + slot
		b.order.target_id = -1
		b.has_move_goal = true
		b.move_goal = b.order.dest
		b.target_id = -1
		i += 1

func order_attack_move(ids: Array, dest: Vector2, formation: String = "") -> void:
	var form := formation if formation != "" else String(GameState.formation)
	var living: Array = []
	for id in ids:
		var b: SimTypes.Battalion = get_battalion(int(id))
		if b == null or b.dead:
			continue
		living.append(b)
	var i := 0
	for b in living:
		var slot := formation_slot(i, living.size(), form)
		b.order.type = SimTypes.OrderType.ATTACK_MOVE
		b.order.dest = dest + slot
		b.has_move_goal = true
		b.move_goal = b.order.dest
		b.target_id = -1
		i += 1

func order_attack(ids: Array, target_id: int, target_kind: int) -> void:
	for id in ids:
		var b: SimTypes.Battalion = get_battalion(int(id))
		if b == null or b.dead or b.side != GameState.SIDE_PLAYER:
			continue
		b.order.type = SimTypes.OrderType.ATTACK
		b.order.target_id = target_id
		b.order.target_kind = target_kind
		b.target_id = target_id
		b.target_kind = target_kind
		b.has_move_goal = false

func order_stop(ids: Array) -> void:
	for id in ids:
		var b: SimTypes.Battalion = get_battalion(int(id))
		if b == null or b.dead:
			continue
		b.order.type = SimTypes.OrderType.STOP
		b.has_move_goal = false
		b.target_id = -1

func set_stance(ids: Array, stance: int) -> void:
	for id in ids:
		var b: SimTypes.Battalion = get_battalion(int(id))
		if b and not b.dead and b.side == GameState.SIDE_PLAYER:
			b.stance = stance

func formation_slot(i: int, n: int, formation: String = "line") -> Vector2:
	## Public so tests can assert wedge ≠ line offsets.
	var form := formation if formation != "" else "line"
	if n <= 1:
		return Vector2.ZERO
	match form:
		"column":
			return Vector2(0.0, float(i) * 2.6)
		"wedge":
			# chevron pointing +Z (forward); index 0 at tip
			var row := 0
			var placed := 0
			var idx := i
			while idx >= (row + 1):
				idx -= (row + 1)
				row += 1
			var width := row + 1
			var col := idx
			return Vector2((float(col) - float(width - 1) * 0.5) * 2.4, float(row) * 2.4)
		"loose":
			var cols_l := mini(n, 4)
			var row_l := int(i / cols_l)
			var col_l := int(i % cols_l)
			return Vector2((col_l - cols_l * 0.5) * 3.6, row_l * 3.6)
		_: # line
			return Vector2((float(i) - float(n - 1) * 0.5) * 2.6, 0.0)

func _formation_slot(i: int, n: int) -> Vector2:
	return formation_slot(i, n, String(GameState.formation))

# ---------- economy / build / train ----------

func can_place(type_id: String, side: int, pos: Vector2) -> bool:
	var def := ContentDB.get_building(type_id)
	if def.is_empty():
		return false
	var r := float(def.get("radius", 4.0))
	if absf(pos.x) > MAP_HALF - r or absf(pos.y) > MAP_HALF - r:
		return false
	for id in buildings:
		var o: SimTypes.Building = buildings[id]
		if o.dead:
			continue
		if o.pos.distance_to(pos) < (o.radius + r) * 0.95:
			return false
	# near own building or fortress
	var near_own := false
	for id in buildings:
		var o: SimTypes.Building = buildings[id]
		if o.dead or o.side != side:
			continue
		if o.pos.distance_to(pos) < 55.0:
			near_own = true
			break
	return near_own

func place_building(type_id: String, side: int, pos: Vector2):
	var def := ContentDB.get_building(type_id)
	var cost := float(def.get("cost", 0))
	if not can_place(type_id, side, pos):
		if side == GameState.SIDE_PLAYER:
			Events.toast.emit("Cannot place here")
		return null
	if not GameState.spend(side, cost):
		return null
	var b: Variant = spawn_building(type_id, side, pos, false)
	return b

func enqueue_research(building_id: int, research_id: String) -> bool:
	var b: SimTypes.Building = get_building(building_id)
	if b == null or b.dead or not b.built:
		return false
	if GameState.has_upgrade(b.side, research_id):
		return false
	var bdef := ContentDB.get_building(b.type_id)
	var list: Array = bdef.get("research", [])
	if not list.has(research_id):
		return false
	var rdef := ContentDB.get_research(research_id)
	if rdef.is_empty():
		return false
	var cost := float(rdef.get("cost", 400))
	if not GameState.spend(b.side, cost):
		return false
	b.train_queue.append({
		"kind": "research", "id": research_id,
		"t": 0.0, "tmax": float(rdef.get("time", 20)),
	})
	return true

func enqueue_train(building_id: int, unit_type: String) -> bool:
	var b: SimTypes.Building = get_building(building_id)
	if b == null or b.dead or not b.built:
		return false
	var udef := ContentDB.get_unit(unit_type)
	var bdef := ContentDB.get_building(b.type_id)
	var trains: Array = bdef.get("trains", [])
	if not trains.has(unit_type) and not trains.has(String(unit_type)):
		# allow heroes listed under heroes
		var heroes: Array = bdef.get("heroes", [])
		if not heroes.has(unit_type):
			return false
	if count_battalions(b.side) >= MAX_BATTALIONS_PER_SIDE:
		if b.side == GameState.SIDE_PLAYER:
			Events.toast.emit("Army limit reached")
		return false
	# revival cost for dead heroes
	var cost := float(udef.get("cost", 0))
	if bool(udef.get("hero", false)):
		var deaths := int(GameState.hero_deaths.get(unit_type, 0))
		if deaths > 0:
			var base_mul := float(ContentDB.globals.get("hero_revive_base_mul", 0.6))
			var per := float(ContentDB.globals.get("hero_revive_death_mul", 0.2))
			cost = cost * (base_mul + per * deaths)
	if not GameState.spend(b.side, cost):
		return false
	var t := float(udef.get("build_time", 8))
	b.train_queue.append({"kind": "unit", "id": unit_type, "t": 0.0, "tmax": t})
	return true

func farm_efficiency(building: SimTypes.Building) -> float:
	var g: Dictionary = ContentDB.globals
	var radius := float(g.get("farm_efficiency_radius", 40))
	var neighbor_mul := float(g.get("farm_neighbor_mul", 0.75))
	var floor_v := float(g.get("farm_efficiency_floor", 0.4))
	var def := ContentDB.get_building(building.type_id)
	if not bool(def.get("economy", false)):
		return 1.0
	var neighbors := 0
	for id in buildings:
		var o: SimTypes.Building = buildings[id]
		if o.dead or o.id == building.id or o.side != building.side:
			continue
		var odef := ContentDB.get_building(o.type_id)
		if not bool(odef.get("economy", false)):
			continue
		if o.pos.distance_to(building.pos) <= radius:
			neighbors += 1
	var eff := 1.0
	for i in neighbors:
		eff *= neighbor_mul
	return maxf(floor_v, eff)

# ---------- combat helpers ----------

func _unit_def(b: SimTypes.Battalion) -> Dictionary:
	return ContentDB.get_unit(b.type_id)

func attack_range(b: SimTypes.Battalion, target_is_building: bool = false) -> float:
	var d := _unit_def(b)
	var r := float(d.get("range", 5.0))
	return r

func vision_range(b: SimTypes.Battalion) -> float:
	return float(_unit_def(b).get("vision", 32))

func damage_mul(source: SimTypes.Battalion, target_battalion: SimTypes.Battalion, target_building: SimTypes.Building) -> float:
	var sdef := _unit_def(source)
	var mul := 1.0
	if source.dmg_buff_t > 0.0:
		mul *= source.dmg_buff_mul
	if source.dmg_debuff_t > 0.0:
		mul *= source.dmg_debuff_mul
	if source.cowering:
		mul *= float(ContentDB.globals.get("cower_damage_mul", 0.75))
	mul *= 1.0 + 0.10 * float(source.rank - 1)
	# research
	if GameState.has_upgrade(source.side, "forged_blades") and not bool(sdef.get("ranged", false)):
		mul *= float(ContentDB.get_research("forged_blades").get("melee_dmg_mul", 1.2))
	if GameState.has_upgrade(source.side, "fire_arrows") and bool(sdef.get("ranged", false)):
		mul *= float(ContentDB.get_research("fire_arrows").get("ranged_dmg_mul", 1.2))
	# weather
	if GameState.weather and float(GameState.weather.get("until", 0)) > SimClock.time:
		var wside = int(GameState.weather.get("caster_side", -1))
		if wside >= 0 and GameState.enemy_of(source.side, wside):
			mul *= float(GameState.weather.get("enemy_dmg_mul", 1.0))
		elif wside >= 0 and source.side == wside:
			mul *= float(GameState.weather.get("ally_dmg_mul", 1.0))
	# difficulty AI outgoing
	if source.side == GameState.SIDE_ENEMY and GameState.ai:
		mul *= float(ContentDB.globals.get("difficulties", {}).get(GameState.difficulty, {}).get("dmg_mul", 1.0))
	var dmg_type := String(sdef.get("damage_type", "slash"))
	if target_building:
		var armor := "building"
		if target_building.is_wall:
			armor = "wall"
		mul *= ContentDB.matrix_mul(dmg_type, armor)
		if bool(sdef.get("siege", false)):
			mul *= float(ContentDB.globals.get("siege_vs_building", 2.0))
	if target_battalion:
		var tdef := _unit_def(target_battalion)
		var armor2 := String(tdef.get("armor_type", "infantry_light"))
		mul *= ContentDB.matrix_mul(dmg_type, armor2)
		if bool(sdef.get("spear", false)) and (bool(tdef.get("cavalry", false)) or bool(tdef.get("monster", false))):
			mul *= float(ContentDB.globals.get("spear_vs_mounted", 2.5))
		if bool(sdef.get("ranged", false)) and bool(tdef.get("monster", false)):
			mul *= float(ContentDB.globals.get("pierce_vs_monster", 0.65))
		# armor research on target
		if GameState.has_upgrade(target_battalion.side, "heavy_armor") and not bool(tdef.get("ranged", false)):
			mul *= float(ContentDB.get_research("heavy_armor").get("melee_armor_mul", 0.85))
		if GameState.has_upgrade(target_battalion.side, "ranged_armor") and bool(sdef.get("ranged", false)):
			mul *= float(ContentDB.get_research("ranged_armor").get("ranged_armor_mul", 0.85))
	return mul

func take_damage_battalion(b: SimTypes.Battalion, dmg: float, source: SimTypes.Battalion = null) -> void:
	if b.dead:
		return
	b.hp -= dmg
	b.out_of_combat_t = 0.0
	var cap := effective_member_cap(b)
	b.members = maxi(1, int(ceil(cap * clampf(b.hp / b.hp_max, 0.0, 1.0))))
	if source:
		_award_xp(source, dmg)
	if b.hp <= 0.0:
		_kill_battalion(b, source)

func take_damage_building(b: SimTypes.Building, dmg: float, source: SimTypes.Battalion = null) -> void:
	if b.dead:
		return
	b.hp -= dmg
	if source:
		_award_xp(source, dmg * 0.5)
	if b.hp <= 0.0:
		_kill_building(b, source)

func effective_member_cap(b: SimTypes.Battalion) -> int:
	var base := int(_unit_def(b).get("size", 1))
	if b.rank >= 2 and not bool(_unit_def(b).get("hero", false)):
		return base # banner carrier visual only for now
	return base

func _award_xp(b: SimTypes.Battalion, amount: float) -> void:
	if not GameState.stage_flags.get("veterancy", true):
		return
	if bool(_unit_def(b).get("hero", false)) or true:
		b.xp += amount
		var thresholds: Array = ContentDB.globals.get("veterancy_thresholds", [0, 300, 800, 1600, 2800])
		var new_rank := 1
		for i in thresholds.size():
			if b.xp >= float(thresholds[i]):
				new_rank = i + 1
		b.rank = mini(new_rank, 5)
		# power points for kills happen on death

func _kill_battalion(b: SimTypes.Battalion, source: SimTypes.Battalion = null) -> void:
	if b.dead:
		return
	b.dead = true
	b.hp = 0.0
	b.members = 0
	GameState.stats["units_lost"] = int(GameState.stats.get("units_lost", 0)) + 1
	var udef := _unit_def(b)
	if bool(udef.get("hero", false)):
		GameState.hero_deaths[b.type_id] = int(GameState.hero_deaths.get(b.type_id, 0)) + 1
		var list: Array = GameState.dead_heroes.get(b.side, [])
		if not list.has(b.type_id):
			list.append(b.type_id)
		GameState.dead_heroes[b.side] = list
	if source and source.side in [GameState.SIDE_PLAYER, GameState.SIDE_ENEMY]:
		GameState.add_pp(source.side, float(ContentDB.globals.get("pp_per_battalion", 8)))
		GameState.stats["enemies_slain"] = int(GameState.stats.get("enemies_slain", 0)) + 1
	if GameState.ring.get("holder_id", -1) == b.id:
		GameState.ring["holder_id"] = -1
		GameState.ring["dropped"] = true
		GameState.ring["pos"] = b.pos
	Events.entity_died.emit(&"battalion", b.id)

func _kill_building(b: SimTypes.Building, source: SimTypes.Battalion = null) -> void:
	if b.dead:
		return
	b.dead = true
	b.hp = 0.0
	for gid in b.garrison:
		var u: SimTypes.Battalion = get_battalion(gid)
		if u and not u.dead:
			u.garrisoned_in = -1
			u.pos = b.pos + Vector2(rng.randf_range(-6, 6), rng.randf_range(-6, 6))
	b.garrison.clear()
	rebuild_obstacles()
	if source and source.side in [GameState.SIDE_PLAYER, GameState.SIDE_ENEMY]:
		GameState.add_pp(source.side, float(ContentDB.globals.get("pp_per_building", 20)))
		GameState.stats["buildings_razed"] = int(GameState.stats.get("buildings_razed", 0)) + 1
	Events.entity_died.emit(&"building", b.id)
	if b.is_fortress:
		GameState.game_over = true
		if b.side == GameState.SIDE_PLAYER:
			GameState.winner_side = GameState.SIDE_ENEMY
		else:
			GameState.winner_side = GameState.SIDE_PLAYER
		Events.match_ended.emit(GameState.winner_side)

# ---------- abilities ----------

func try_cast_ability(battalion_id: int, ability_id: String, target_pos: Vector2 = Vector2.ZERO, target_id: int = -1) -> bool:
	if not GameState.stage_flags.get("abilities", true):
		return false
	var b: SimTypes.Battalion = get_battalion(battalion_id)
	if b == null or b.dead:
		return false
	var udef := _unit_def(b)
	var abl_list: Array = udef.get("abilities", [])
	if not abl_list.has(ability_id):
		return false
	var adef := ContentDB.get_ability(ability_id)
	if adef.is_empty():
		return false
	# rank gate: 1st free, 2nd needs rank 2, 3rd+ needs rank 3 (mert-style)
	var idx := abl_list.find(ability_id)
	var req_rank := 1
	if idx >= 2:
		req_rank = 3
	elif idx == 1:
		req_rank = 2
	req_rank = maxi(req_rank, int(adef.get("req_rank", 1)))
	if b.rank < req_rank:
		if b.side == GameState.SIDE_PLAYER:
			Events.toast.emit("Requires rank %d" % req_rank)
		return false
	var cd_left := float(b.ability_cds.get(ability_id, 0.0))
	if cd_left > 0.0:
		return false
	var ok := _execute_ability(b, adef, target_pos, target_id)
	if ok:
		var cd := float(adef.get("cooldown", 30))
		cd *= maxf(0.5, 1.0 - 0.05 * float(b.rank - 1))
		b.ability_cds[ability_id] = cd
		Events.ability_used.emit(b.id, StringName(ability_id))
	return ok

func _execute_ability(b: SimTypes.Battalion, adef: Dictionary, target_pos: Vector2, target_id: int) -> bool:
	var kind := String(adef.get("kind", "nova_damage"))
	match kind:
		"nova_damage":
			var radius := float(adef.get("radius", 12))
			var dmg := float(adef.get("damage", 100))
			for id in battalions:
				var o: SimTypes.Battalion = battalions[id]
				if o.dead or not GameState.enemy_of(b.side, o.side):
					continue
				if o.pos.distance_to(b.pos) <= radius:
					take_damage_battalion(o, dmg, b)
					if float(adef.get("knockback", 0)) > 0.0:
						var dir := (o.pos - b.pos).normalized()
						o.pos += dir * float(adef.get("knockback", 0))
						o.knockdown_t = float(adef.get("knockdown", 0.5))
			return true
		"heal_nova":
			var radius := float(adef.get("radius", 20))
			var frac := float(adef.get("heal_frac", 0.4))
			for id in battalions:
				var o: SimTypes.Battalion = battalions[id]
				if o.dead or o.side != b.side:
					continue
				if o.pos.distance_to(b.pos) <= radius:
					o.hp = minf(o.hp_max, o.hp + o.hp_max * frac)
			return true
		"fear_nova":
			var radius := float(adef.get("radius", 18))
			var dur := float(adef.get("duration", 4))
			for id in battalions:
				var o: SimTypes.Battalion = battalions[id]
				if o.dead or not GameState.enemy_of(b.side, o.side):
					continue
				if bool(_unit_def(o).get("hero", false)):
					continue
				if o.rank >= 5:
					continue
				if o.pos.distance_to(b.pos) <= radius:
					o.fear_t = dur
					o.cowering = true
			return true
		"buff_self":
			b.dmg_buff_t = float(adef.get("duration", 8))
			b.dmg_buff_mul = float(adef.get("dmg_mul", 1.4))
			return true
		"summon":
			var summons: Array = adef.get("summons", [])
			if summons.is_empty() and adef.get("unit"):
				summons = [{"typeKey": adef.get("unit"), "count": int(adef.get("count", 1)), "lifetime": float(adef.get("duration", 30))}]
			for s in summons:
				var sk := String(s.get("typeKey", s.get("unit", "soldier")))
				var cnt := int(s.get("count", 1))
				var life := float(s.get("lifetime", 30))
				for i in cnt:
					var ang := float(i) * TAU / maxf(1.0, float(cnt))
					var pos := b.pos + Vector2(cos(ang), sin(ang)) * float(s.get("spread", 5))
					var u = spawn_battalion(sk, b.side, pos, b.faction)
					if u:
						u.summon_life = life
						if s.get("hp"):
							u.hp_max = float(s.get("hp"))
							u.hp = u.hp_max
			return true
		"single_target":
			var t: SimTypes.Battalion = get_battalion(target_id)
			if t == null or t.dead:
				# nearest enemy
				t = _find_enemy(b, float(adef.get("range", 40)))
			if t == null:
				return false
			take_damage_battalion(t, float(adef.get("damage", 200)), b)
			return true
		_:
			return false

# ---------- tick ----------

func tick(dt: float) -> void:
	if GameState.game_over:
		return
	_tick_buildings(dt)
	_tick_battalions(dt)
	_tick_projectiles(dt)
	_tick_terror(dt)
	tick_summons(dt)
	tick_ring(dt)
	if fog_enabled and GameState.stage_flags.get("fog", true):
		_update_fog()

func _tick_buildings(dt: float) -> void:
	for id in buildings.keys():
		var b: SimTypes.Building = buildings[id]
		if b.dead:
			continue
		var def := ContentDB.get_building(b.type_id)
		# construction
		if not b.built:
			var bt := float(def.get("build_time", 10))
			b.build_progress = minf(1.0, b.build_progress + dt / maxf(0.1, bt))
			b.hp = maxf(b.hp, b.hp_max * maxf(0.15, b.build_progress))
			if b.build_progress >= 1.0:
				b.built = true
				b.hp = b.hp_max
				rebuild_obstacles()
				Events.building_completed.emit(b.id)
			continue
		# economy
		if bool(def.get("economy", false)) and GameState.stage_flags.get("economy", true):
			b.income_t += dt
			if b.income_t >= 1.0:
				b.income_t = 0.0
				var inc := float(def.get("income", 8)) * farm_efficiency(b)
				GameState.add_resources(b.side, inc)
		# train queue
		if b.train_queue.size() > 0:
			var job: Dictionary = b.train_queue[0]
			job["t"] = float(job.get("t", 0.0)) + dt
			b.train_queue[0] = job
			if float(job["t"]) >= float(job.get("tmax", 1.0)):
				b.train_queue.pop_front()
				var kind := String(job.get("kind", "unit"))
				if kind == "research":
					GameState.set_upgrade(b.side, String(job["id"]))
				else:
					var ut := String(job["id"])
					var spawn_pos := b.pos + Vector2(0, b.radius + 3)
					if b.has_rally:
						spawn_pos = b.pos + (b.rally - b.pos).normalized() * (b.radius + 3)
					var u: Variant = spawn_battalion(ut, b.side, spawn_pos, b.faction)
					GameState.stats["units_trained"] = int(GameState.stats.get("units_trained", 0)) + 1
					if u and b.has_rally:
						u.order.type = SimTypes.OrderType.MOVE
						u.order.dest = b.rally
						u.has_move_goal = true
						u.move_goal = b.rally
					if u and bool(ContentDB.get_unit(ut).get("hero", false)):
						var list: Array = GameState.dead_heroes.get(b.side, [])
						list.erase(ut)
						GameState.dead_heroes[b.side] = list
		# tower fire
		if b.is_tower or (b.is_fortress and bool(def.get("auto_attack", true))):
			b.tower_cd = maxf(0.0, b.tower_cd - dt)
			if b.tower_cd <= 0.0:
				var rng_r := float(def.get("tower_range", 48))
				var dmg := float(def.get("tower_damage", 12))
				var t: Variant = _find_enemy_near(b.pos, b.side, rng_r)
				if t:
					take_damage_battalion(t, dmg, null)
					b.tower_cd = float(def.get("tower_interval", 2.5))

func _tick_battalions(dt: float) -> void:
	for id in battalions.keys():
		var b: SimTypes.Battalion = battalions[id]
		if b.dead or b.garrisoned_in >= 0:
			continue
		var def := _unit_def(b)
		# timers
		b.attack_cd = maxf(0.0, b.attack_cd - dt)
		b.fear_t = maxf(0.0, b.fear_t - dt)
		b.fear_immune_t = maxf(0.0, b.fear_immune_t - dt)
		b.knockdown_t = maxf(0.0, b.knockdown_t - dt)
		b.dmg_buff_t = maxf(0.0, b.dmg_buff_t - dt)
		b.dmg_debuff_t = maxf(0.0, b.dmg_debuff_t - dt)
		b.trample_cd = maxf(0.0, b.trample_cd - dt)
		b.out_of_combat_t += dt
		for ak in b.ability_cds.keys():
			b.ability_cds[ak] = maxf(0.0, float(b.ability_cds[ak]) - dt)
		if b.fear_t <= 0.0:
			b.cowering = false
		if b.knockdown_t > 0.0:
			continue
		# fear flee
		if b.fear_t > 0.0 and GameState.stage_flags.get("fear", true):
			var away := _fear_dir(b)
			_move_battalion(b, away, float(def.get("speed", 10)) * float(ContentDB.globals.get("flee_speed_mul", 2.0)), dt)
			continue
		# acquire target
		var hold := b.stance == GameState.Stance.HOLD
		var aggro_mul := 1.0
		if b.stance == GameState.Stance.DEFENSIVE:
			aggro_mul = 0.7
		elif b.stance == GameState.Stance.HOLD:
			aggro_mul = 0.0
		if b.order.type == SimTypes.OrderType.ATTACK and b.target_id >= 0:
			pass
		elif b.order.type != SimTypes.OrderType.MOVE and aggro_mul > 0.0:
			var vis := vision_range(b) * aggro_mul
			if b.order.type == SimTypes.OrderType.ATTACK_MOVE:
				vis = vision_range(b)
			var enemy: Variant = _find_enemy(b, vis)
			if enemy:
				b.target_id = enemy.id
				b.target_kind = SimTypes.EntityKind.BATTALION
		# defensive return
		if b.stance == GameState.Stance.DEFENSIVE and b.target_id < 0:
			if b.pos.distance_to(b.home) > 4.0:
				b.has_move_goal = true
				b.move_goal = b.home
		# combat
		var engaged := false
		if b.target_id >= 0:
			var tbat: Variant = get_battalion(b.target_id) if b.target_kind == SimTypes.EntityKind.BATTALION else null
			var tbud: Variant = get_building(b.target_id) if b.target_kind == SimTypes.EntityKind.BUILDING else null
			if tbat and tbat.dead:
				b.target_id = -1
				tbat = null
			if tbud and tbud.dead:
				b.target_id = -1
				tbud = null
			var tpos := Vector2.ZERO
			var valid := false
			if tbat:
				tpos = tbat.pos
				valid = true
			elif tbud:
				tpos = tbud.pos
				valid = true
			if valid:
				var dist := b.pos.distance_to(tpos)
				var ar := attack_range(b)
				if tbud:
					ar += tbud.radius * 0.5
				if dist <= ar + 0.4:
					engaged = true
					b.has_move_goal = false
					if b.attack_cd <= 0.0:
						_do_attack(b, tbat, tbud)
						b.attack_cd = float(def.get("attack_interval", 1.0))
				elif not hold:
					b.has_move_goal = true
					b.move_goal = tpos
		# move order
		if not engaged and b.has_move_goal:
			var to := b.move_goal - b.pos
			if to.length() < 1.2:
				b.has_move_goal = false
				if b.order.type == SimTypes.OrderType.MOVE or b.order.type == SimTypes.OrderType.ATTACK_MOVE:
					if b.order.type == SimTypes.OrderType.MOVE:
						b.order.type = SimTypes.OrderType.STOP
				b.home = b.pos
			else:
				var spd := float(def.get("speed", 10))
				if b.cowering:
					spd *= float(ContentDB.globals.get("cower_speed_mul", 0.85))
				_move_battalion(b, to.normalized(), spd, dt)
		# replenishment
		if GameState.stage_flags.get("veterancy", true) and b.out_of_combat_t > float(ContentDB.globals.get("replenish_delay", 15)):
			if b.hp < b.hp_max:
				b.hp = minf(b.hp_max, b.hp + b.hp_max * float(ContentDB.globals.get("replenish_frac", 0.02)) * dt)
				b.members = maxi(1, int(ceil(effective_member_cap(b) * b.hp / b.hp_max)))

func _do_attack(b: SimTypes.Battalion, tbat: SimTypes.Battalion, tbud: SimTypes.Building) -> void:
	var def := _unit_def(b)
	var base := float(def.get("dmg", 10))
	var mul := damage_mul(b, tbat, tbud)
	var dmg := base * mul
	_play_combat_sfx(b)
	if bool(def.get("ranged", false)):
		var p := SimTypes.Projectile.new()
		p.id = _uid()
		p.pos = b.pos
		p.target_id = tbat.id if tbat else tbud.id
		p.target_kind = SimTypes.EntityKind.BATTALION if tbat else SimTypes.EntityKind.BUILDING
		p.dmg = dmg
		p.side = b.side
		p.source_id = b.id
		p.speed = 45.0
		var aim := (tbat.pos if tbat else tbud.pos)
		p.vel = (aim - b.pos).normalized() * p.speed
		projectiles[p.id] = p
	else:
		if tbat:
			take_damage_battalion(tbat, dmg, b)
			# monster knockback
			if bool(def.get("monster", false)) and GameState.stage_flags.get("fear", true):
				tbat.knockdown_t = float(ContentDB.globals.get("monster_knockdown", 1.0))
				var dir := (tbat.pos - b.pos).normalized()
				tbat.pos += dir * float(ContentDB.globals.get("monster_knockback", 3.5))
		elif tbud:
			take_damage_building(tbud, dmg, b)

func _tick_projectiles(dt: float) -> void:
	var dead_ids: Array = []
	for id in projectiles:
		var p: SimTypes.Projectile = projectiles[id]
		if p.dead:
			dead_ids.append(id)
			continue
		p.lifetime -= dt
		if p.lifetime <= 0.0:
			p.dead = true
			dead_ids.append(id)
			continue
		var aim := p.pos
		if p.target_kind == SimTypes.EntityKind.BATTALION:
			var t: Variant = get_battalion(p.target_id)
			if t == null or t.dead:
				p.dead = true
				dead_ids.append(id)
				continue
			aim = t.pos
		else:
			var t2: Variant = get_building(p.target_id)
			if t2 == null or t2.dead:
				p.dead = true
				dead_ids.append(id)
				continue
			aim = t2.pos
		var dir := (aim - p.pos)
		if dir.length() < 1.5:
			var src: Variant = get_battalion(p.source_id)
			if p.target_kind == SimTypes.EntityKind.BATTALION:
				var tb: Variant = get_battalion(p.target_id)
				if tb:
					take_damage_battalion(tb, p.dmg, src)
			else:
				var tbu: Variant = get_building(p.target_id)
				if tbu:
					take_damage_building(tbu, p.dmg, src)
			p.dead = true
			dead_ids.append(id)
		else:
			p.pos += dir.normalized() * p.speed * dt
	for id in dead_ids:
		projectiles.erase(id)

func _tick_terror(dt: float) -> void:
	if not GameState.stage_flags.get("fear", true):
		return
	var terror_units: Array = ContentDB.globals.get("terror_units", ["nazgul", "mouthsauron", "shelob", "balrog"])
	var radius := float(ContentDB.globals.get("terror_radius", 20))
	for id in battalions:
		var src: SimTypes.Battalion = battalions[id]
		if src.dead or not terror_units.has(src.type_id):
			continue
		for oid in battalions:
			var o: SimTypes.Battalion = battalions[oid]
			if o.dead or not GameState.enemy_of(src.side, o.side):
				continue
			if bool(_unit_def(o).get("hero", false)):
				continue
			if o.rank >= 3:
				continue
			if o.fear_immune_t > 0.0:
				continue
			if o.pos.distance_to(src.pos) <= radius:
				# first contact flee
				if o.fear_t <= 0.0:
					o.fear_t = float(ContentDB.globals.get("terror_duration", 3))
					o.fear_immune_t = float(ContentDB.globals.get("terror_immune", 30))
					o.cowering = true

func _fear_dir(b: SimTypes.Battalion) -> Vector2:
	var terror_units: Array = ContentDB.globals.get("terror_units", ["nazgul"])
	var acc := Vector2.ZERO
	for id in battalions:
		var s: SimTypes.Battalion = battalions[id]
		if s.dead or not terror_units.has(s.type_id):
			continue
		if not GameState.enemy_of(b.side, s.side):
			continue
		var d := b.pos - s.pos
		if d.length_squared() > 0.01:
			acc += d.normalized()
	if acc.length_squared() < 0.01:
		return Vector2.RIGHT.rotated(rng.randf() * TAU)
	return acc.normalized()

func _move_battalion(b: SimTypes.Battalion, dir: Vector2, speed: float, dt: float) -> void:
	if dir.length_squared() < 0.0001:
		return
	dir = dir.normalized()
	var step := dir * speed * dt
	var next := b.pos + step
	next = _steer_around(b, next, dir)
	next.x = clampf(next.x, -MAP_HALF, MAP_HALF)
	next.y = clampf(next.y, -MAP_HALF, MAP_HALF)
	var prev := b.pos
	b.pos = next
	b.facing = dir.angle()
	# cavalry trample (from mert entities.js)
	var udef := _unit_def(b)
	if bool(udef.get("cavalry", false)) and prev.distance_to(next) > 0.05:
		_apply_trample(b)

func _apply_trample(cav: SimTypes.Battalion) -> void:
	var dmg_base := float(ContentDB.globals.get("trample_damage", 8))
	var rehit := float(ContentDB.globals.get("trample_rehit", 2.5))
	var members := maxi(1, cav.members)
	var dmg := dmg_base * float(members)
	for id in battalions:
		var v: SimTypes.Battalion = battalions[id]
		if v.dead or v.id == cav.id:
			continue
		if not GameState.enemy_of(cav.side, v.side):
			continue
		if cav.pos.distance_to(v.pos) > cav.radius + v.radius + 1.2:
			continue
		if v.trample_cd > 0.0:
			continue
		v.trample_cd = rehit
		take_damage_battalion(v, dmg, cav)
		v.knockdown_t = maxf(v.knockdown_t, 1.0)
		# spear reflect
		if bool(_unit_def(v).get("spear", false)):
			take_damage_battalion(cav, dmg * 2.5, v)

func _steer_around(b: SimTypes.Battalion, next: Vector2, dir: Vector2) -> Vector2:
	for obs in obstacles:
		if not obs.get("blocks_nav", true):
			# open gate: own side can pass; enemies blocked if closed only
			if obs.get("gate", false) and obs.get("open", true):
				continue
		# enemy cannot pass closed gate; friendly can if open handled above
		if obs.get("gate", false) and not obs.get("open", true):
			pass
		var op: Vector2 = obs["pos"]
		var r: float = float(obs["radius"]) + b.radius
		if next.distance_to(op) < r:
			var push := (next - op)
			if push.length_squared() < 0.001:
				push = Vector2.RIGHT.rotated(b.facing + PI * 0.5)
			next = op + push.normalized() * r
			# slide
			var tangent := Vector2(-dir.y, dir.x)
			next += tangent * 0.4
	return next

func _find_enemy(b: SimTypes.Battalion, radius: float):
	var best: SimTypes.Battalion = null
	var best_d := radius
	for id in battalions:
		var o: SimTypes.Battalion = battalions[id]
		if o.dead or o.garrisoned_in >= 0:
			continue
		if not GameState.enemy_of(b.side, o.side):
			continue
		var d := b.pos.distance_to(o.pos)
		if d < best_d:
			best_d = d
			best = o
	return best

func _find_enemy_near(pos: Vector2, side: int, radius: float):
	var best: SimTypes.Battalion = null
	var best_d := radius
	for id in battalions:
		var o: SimTypes.Battalion = battalions[id]
		if o.dead or not GameState.enemy_of(side, o.side):
			continue
		var d := pos.distance_to(o.pos)
		if d < best_d:
			best_d = d
			best = o
	return best

# ---------- fog ----------

func _world_to_fog(p: Vector2) -> Vector2i:
	var fx := int(floor((p.x + MAP_HALF) / (MAP_HALF * 2.0) * fog_w))
	var fy := int(floor((p.y + MAP_HALF) / (MAP_HALF * 2.0) * fog_h))
	return Vector2i(clampi(fx, 0, fog_w - 1), clampi(fy, 0, fog_h - 1))

func _update_fog() -> void:
	fog_visible.fill(0)
	for id in battalions:
		var b: SimTypes.Battalion = battalions[id]
		if b.dead or b.side != GameState.SIDE_PLAYER:
			continue
		_paint_vision(b.pos, vision_range(b))
	for id in buildings:
		var b: SimTypes.Building = buildings[id]
		if b.dead or b.side != GameState.SIDE_PLAYER or not b.built:
			continue
		var vis := float(ContentDB.get_building(b.type_id).get("vision", 40))
		_paint_vision(b.pos, vis)
	Events.fog_dirty.emit()

func _paint_vision(pos: Vector2, radius: float) -> void:
	var cell := (MAP_HALF * 2.0) / float(fog_w)
	var r_cells := int(ceil(radius / cell))
	var c := _world_to_fog(pos)
	for y in range(c.y - r_cells, c.y + r_cells + 1):
		for x in range(c.x - r_cells, c.x + r_cells + 1):
			if x < 0 or y < 0 or x >= fog_w or y >= fog_h:
				continue
			var wx := -MAP_HALF + (x + 0.5) * cell
			var wy := -MAP_HALF + (y + 0.5) * cell
			if Vector2(wx, wy).distance_to(pos) <= radius:
				var idx := y * fog_w + x
				fog_visible[idx] = 1
				fog_explored[idx] = 1

func is_visible_to_player(pos: Vector2) -> bool:
	if not fog_enabled or not GameState.stage_flags.get("fog", true):
		return true
	var c := _world_to_fog(pos)
	return fog_visible[c.y * fog_w + c.x] != 0

# ---------- powers (stage 5) ----------

func cast_power(side: int, power_id: String, target_pos: Vector2 = Vector2.ZERO) -> bool:
	if not GameState.stage_flags.get("powers", true):
		return false
	if not GameState.power_unlocked(side, power_id):
		# auto-unlock path for free cast attempt after unlock_power already spent PP cost
		if not GameState.can_unlock_power(side, power_id) and not GameState.power_unlocked(side, power_id):
			return false
	var pdef: Dictionary = ContentDB.get_power(power_id)
	if pdef.is_empty():
		return false
	# If not yet unlocked, unlock (spends PP as unlock cost). If already unlocked, free to cast once unlocked.
	if not GameState.power_unlocked(side, power_id):
		if not GameState.unlock_power(side, power_id):
			return false
	var kind := String(pdef.get("kind", ""))
	var ok := false
	match kind:
		"heal_area":
			var radius := float(pdef.get("radius", 18))
			var frac := float(pdef.get("heal_frac", 0.35))
			for id in battalions:
				var b: SimTypes.Battalion = battalions[id]
				if b.dead or b.side != side:
					continue
				if b.pos.distance_to(target_pos) <= radius:
					b.hp = minf(b.hp_max, b.hp + b.hp_max * frac)
			ok = true
		"reveal":
			# paint fog around point
			_paint_vision(target_pos, float(pdef.get("radius", 40)))
			GameState.farsight_until = SimClock.time + float(pdef.get("duration", 20))
			ok = true
		"damage_area":
			var radius2 := float(pdef.get("radius", 16))
			var dmg := float(pdef.get("damage", 200))
			for id in battalions:
				var o: SimTypes.Battalion = battalions[id]
				if o.dead or not GameState.enemy_of(side, o.side):
					continue
				if o.pos.distance_to(target_pos) <= radius2:
					take_damage_battalion(o, dmg, null)
			ok = true
		"weather":
			GameState.weather = {
				"id": String(pdef.get("weather", pdef.get("id", "darkness"))),
				"until": SimClock.time + float(pdef.get("duration", 30)),
				"enemy_dmg_mul": float(pdef.get("enemy_dmg_mul", 0.75 if String(pdef.get("id", "")).find("dark") >= 0 else 1.0)),
				"ally_dmg_mul": float(pdef.get("ally_dmg_mul", 1.25 if String(pdef.get("id", "")).find("cloud") >= 0 or String(pdef.get("id", "")).find("break") >= 0 else 1.0)),
				"caster_side": side,
			}
			ok = true
		"quake":
			var radius3 := float(pdef.get("radius", 28))
			var bdmg := float(pdef.get("building_damage", 400))
			for id in buildings:
				var bu: SimTypes.Building = buildings[id]
				if bu.dead or not GameState.enemy_of(side, bu.side):
					continue
				if bu.pos.distance_to(target_pos) <= radius3:
					take_damage_building(bu, bdmg, null)
			GameState.quake_shake_t = 1.2
			ok = true
		"summon":
			var ut := String(pdef.get("unit", "soldier"))
			var count := int(pdef.get("count", 1))
			var life := float(pdef.get("lifetime", 40))
			var fac := GameState.player_faction if side == GameState.SIDE_PLAYER else GameState.enemy_faction
			for i in count:
				var ang := float(i) * TAU / maxf(1.0, float(count))
				var pos := target_pos + Vector2(cos(ang), sin(ang)) * 4.0
				var u = spawn_battalion(ut, side, pos, fac)
				if u:
					u.summon_life = life
			ok = true
		_:
			ok = false
	if ok:
		GameState.stats["powers_cast"] = int(GameState.stats.get("powers_cast", 0)) + 1
		Events.toast.emit("Power: %s" % String(pdef.get("name", power_id)))
	return ok

func tick_summons(dt: float) -> void:
	for id in battalions.keys():
		var b: SimTypes.Battalion = battalions[id]
		if b.dead:
			continue
		if b.summon_life > 0.0:
			b.summon_life = maxf(0.0, b.summon_life - dt)
			if b.summon_life <= 0.0:
				_kill_battalion(b, null)

func spawn_ring(pos: Vector2) -> void:
	GameState.ring = {"active": true, "pos": pos, "holder_id": -1, "dropped": true}

func tick_ring(dt: float) -> void:
	if not GameState.stage_flags.get("ring", true):
		return
	if not GameState.ring.get("active", false):
		return
	var holder := int(GameState.ring.get("holder_id", -1))
	if holder >= 0:
		var h = get_battalion(holder)
		if h == null or h.dead:
			GameState.ring["holder_id"] = -1
			GameState.ring["dropped"] = true
		else:
			GameState.ring["pos"] = h.pos
			# buff holder
			h.dmg_buff_t = maxf(h.dmg_buff_t, 0.5)
			h.dmg_buff_mul = maxf(h.dmg_buff_mul, 1.25)
		return
	# pickup
	var pos: Vector2 = GameState.ring.get("pos", Vector2.ZERO)
	for id in battalions:
		var b: SimTypes.Battalion = battalions[id]
		if b.dead or b.side == GameState.SIDE_WILD:
			continue
		if bool(_unit_def(b).get("hero", false)) and b.pos.distance_to(pos) < 4.0:
			GameState.ring["holder_id"] = b.id
			GameState.ring["dropped"] = false
			Events.toast.emit("The Ring is claimed!")
			return

func _play_combat_sfx(_source: SimTypes.Battalion) -> void:
	if GameAudio != null and GameAudio.has_method("play_combat"):
		GameAudio.play_combat()

