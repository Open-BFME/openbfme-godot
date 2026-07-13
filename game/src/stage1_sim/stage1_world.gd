class_name Stage1World
extends RefCounted
## Legal-safe typed-GDScript candidate for the deterministic Stage 1 arena.
## Authoritative state is integer-only and has no scene, physics, or render dependency.

const TICKS_PER_SECOND := 30
const HORDE_MOVE_PER_TICK := 360
const MEMBER_MOVE_PER_TICK := 430
const PROJECTILE_MOVE_PER_TICK := 900
const ENGAGEMENT_DISTANCE := 7000
const ANCHOR_HOLD_DISTANCE := 1500
const MELEE_RANGE := 950
const RANGED_RANGE := 6000
const FORTRESS_RANGE := 1500
const MELEE_DAMAGE := 22
const MELEE_COOLDOWN := 8
const RANGED_COOLDOWN := 12

var grid: Stage1Grid
var tick_index: int = 0
var winner: int = Stage1Types.Team.NONE
var hordes: Array[Stage1Types.Horde] = []
var fortresses: Array[Stage1Types.Fortress] = []
var projectiles: Array[Stage1Types.Projectile] = []
var stage2_enabled: bool = false
var stage2: Stage2Economy = null

var _commands: Array[Stage1Types.Command] = []
var _next_command_index: int = 0
var _next_command_sequence: int = 0
var _next_horde_id: int = 100
var _next_member_id: int = 1000
var _next_projectile_id: int = 100000

func _init(width: int = 48, height: int = 32) -> void:
	grid = Stage1Grid.new(width, height)

func reset() -> void:
	tick_index = 0
	winner = Stage1Types.Team.NONE
	hordes.clear()
	fortresses.clear()
	projectiles.clear()
	stage2_enabled = false
	stage2 = null
	_commands.clear()
	_next_command_index = 0
	_next_command_sequence = 0
	_next_horde_id = 100
	_next_member_id = 1000
	_next_projectile_id = 100000
	grid.clear()

func setup_demo(hordes_per_side: int = 6, members_per_horde: int = 15) -> void:
	reset()
	_build_arena_obstacles()
	var blue_fort := add_fortress(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(3, 16)))
	var red_fort := add_fortress(Stage1Types.Team.RED, Stage1Grid.cell_center(Vector2i(44, 16)))
	var blue_ids: Array[int] = []
	var red_ids: Array[int] = []
	for i in hordes_per_side:
		var y := 5 if hordes_per_side == 1 else 5 + (i * 22 / (hordes_per_side - 1))
		var blue := add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(7 + i % 2, y)), members_per_horde, 5)
		var red := add_horde(Stage1Types.Team.RED, Stage1Grid.cell_center(Vector2i(40 - i % 2, 31 - y)), members_per_horde, 5)
		blue_ids.append(blue.id)
		red_ids.append(red.id)
	order_attack_move(blue_ids, red_fort.position)
	order_attack_move(red_ids, blue_fort.position)

func setup_empty(with_obstacles: bool = true) -> void:
	reset()
	if with_obstacles:
		_build_arena_obstacles()

func _build_arena_obstacles() -> void:
	# Two-cell spine with an eight-cell crossing in the middle.
	for y in grid.height:
		if y >= 12 and y <= 19:
			continue
		grid.set_blocked(Vector2i(23, y))
		grid.set_blocked(Vector2i(24, y))
	# A small rock cluster makes the selected route visibly bend again.
	for y in range(8, 12):
		for x in range(14, 17):
			grid.set_blocked(Vector2i(x, y))

func add_fortress(team: int, position: Vector2i, health: int = 5000) -> Stage1Types.Fortress:
	assert(team == Stage1Types.Team.BLUE or team == Stage1Types.Team.RED)
	assert(grid.is_walkable(position))
	var entity_id := 1 if team == Stage1Types.Team.BLUE else 2
	assert(get_fortress(entity_id) == null)
	var fortress := Stage1Types.Fortress.new(entity_id, team, position, health)
	fortresses.append(fortress)
	fortresses.sort_custom(func(a: Stage1Types.Fortress, b: Stage1Types.Fortress) -> bool: return a.id < b.id)
	return fortress

func add_horde(team: int, anchor: Vector2i, member_count: int = 15, ranged_every: int = 0) -> Stage1Types.Horde:
	assert(team == Stage1Types.Team.BLUE or team == Stage1Types.Team.RED)
	assert(member_count > 0 and grid.is_walkable(anchor))
	var horde := Stage1Types.Horde.new(_next_horde_id, team, anchor)
	_next_horde_id += 1
	for slot in member_count:
		var desired := anchor + formation_offset(slot, member_count)
		var position := desired if grid.is_walkable(desired) else anchor
		var is_ranged := ranged_every > 0 and ((slot + 1) % ranged_every == 0)
		horde.members.append(Stage1Types.Member.new(_next_member_id, horde.id, team, slot, is_ranged, position))
		_next_member_id += 1
	hordes.append(horde)
	return horde

func add_horde_composition(team: int, anchor: Vector2i, member_count: int, ranged_count: int) -> Stage1Types.Horde:
	assert(ranged_count >= 0 and ranged_count <= member_count)
	assert(ranged_count == 0 or member_count % ranged_count == 0)
	var ranged_every := member_count / ranged_count if ranged_count > 0 else 0
	return add_horde(team, anchor, member_count, ranged_every)

func enable_stage2(economy_data: Dictionary) -> String:
	if stage2_enabled:
		return "stage2_already_enabled"
	stage2 = Stage2Economy.new()
	var error := stage2.configure(economy_data, self)
	if error != "":
		stage2 = null
		return error
	stage2_enabled = true
	return ""

func get_economy(team: int) -> Stage2Types.TeamEconomy:
	return stage2.get_economy(team) if stage2_enabled else null

func get_building(entity_id: int) -> Stage2Types.Building:
	return stage2.get_building(entity_id) if stage2_enabled else null

func can_place_building(team: int, type_code: int, position: Vector2i) -> bool:
	return stage2_enabled and stage2.can_place(self, team, type_code, position)

func order_place_building(team: int, type_code: int, position: Vector2i, execute_tick: int = -1, sequence: int = -1) -> void:
	assert(stage2_enabled)
	stage2.schedule_place(team, type_code, position, tick_index, execute_tick, sequence)

func order_train(building_id: int, blueprint_code: int, execute_tick: int = -1, sequence: int = -1) -> void:
	assert(stage2_enabled)
	stage2.schedule_train(building_id, blueprint_code, tick_index, execute_tick, sequence)

func order_set_rally(building_id: int, position: Vector2i, execute_tick: int = -1, sequence: int = -1) -> void:
	assert(stage2_enabled)
	stage2.schedule_rally(building_id, position, tick_index, execute_tick, sequence)

func try_place_building(team: int, type_code: int, position: Vector2i) -> Stage2Types.Building:
	return stage2._place(self, team, type_code, position) if stage2_enabled else null

func try_train(building_id: int, blueprint_code: int) -> bool:
	return stage2_enabled and stage2._train(self, building_id, blueprint_code)

func try_set_rally(building_id: int, position: Vector2i) -> bool:
	return stage2_enabled and stage2._set_rally(self, building_id, position)

func damage_building(building_id: int, amount: int) -> bool:
	return stage2_enabled and stage2.damage_building(self, building_id, amount)

func building_definitions() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	if not stage2_enabled:
		return result
	for definition in stage2.definitions:
		result.append({
			"type_code": definition.type_code,
			"object_id": definition.object_id,
			"display_name": definition.display_name,
			"role": definition.role_code,
			"cost": definition.cost,
			"construction_ticks": definition.construction_ticks,
			"max_health": definition.maximum_health,
			"width_cells": definition.width_cells,
			"height_cells": definition.height_cells,
			"build_menu_slot": definition.build_menu_slot,
			"income_amount": definition.income_amount,
			"income_interval_ticks": definition.income_interval_ticks,
			"trains": definition.train_type_codes.duplicate(),
		})
	result.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var a_slot := int(a.build_menu_slot)
		var b_slot := int(b.build_menu_slot)
		if a_slot != b_slot:
			return a_slot < b_slot
		return int(a.type_code) < int(b.type_code)
	)
	return result

func blueprint_definitions() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	if not stage2_enabled:
		return result
	for blueprint in stage2.blueprints:
		result.append({
			"type_code": blueprint.type_code,
			"id": blueprint.id,
			"display_name": blueprint.display_name,
			"member_count": blueprint.member_count,
			"ranged_count": blueprint.ranged_count,
			"cost": blueprint.cost,
			"production_ticks": blueprint.production_ticks,
			"population": blueprint.population,
			"train_menu_slot": blueprint.train_menu_slot,
		})
	result.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var a_slot := int(a.train_menu_slot)
		var b_slot := int(b.train_menu_slot)
		if a_slot != b_slot:
			return a_slot < b_slot
		return int(a.type_code) < int(b.type_code)
	)
	return result

func get_horde(entity_id: int) -> Stage1Types.Horde:
	for horde in hordes:
		if horde.id == entity_id:
			return horde
	return null

func get_member(entity_id: int) -> Stage1Types.Member:
	for horde in hordes:
		for member in horde.members:
			if member.id == entity_id:
				return member
	return null

func get_fortress(entity_id: int) -> Stage1Types.Fortress:
	for fortress in fortresses:
		if fortress.id == entity_id:
			return fortress
	return null

func fortress_for(team: int) -> Stage1Types.Fortress:
	for fortress in fortresses:
		if fortress.team == team:
			return fortress
	return null

func alive_horde_ids(team: int) -> Array[int]:
	var result: Array[int] = []
	for horde in hordes:
		if horde.team == team and horde.is_alive():
			result.append(horde.id)
	return result

func living_member_count(team: int) -> int:
	var result := 0
	for horde in hordes:
		if horde.team == team:
			result += horde.alive_count()
	return result

func schedule_command(horde_id: int, kind: int, destination: Vector2i, target_id: int = 0, execute_tick: int = -1) -> void:
	var command_horde := get_horde(horde_id)
	assert(command_horde != null)
	if kind == Stage1Types.OrderKind.ATTACK_TARGET and _entity_team(target_id) == command_horde.team:
		return
	var command_tick := tick_index + 1 if execute_tick < 0 else execute_tick
	assert(command_tick >= tick_index)
	_commands.append(Stage1Types.Command.new(command_tick, _next_command_sequence, horde_id, kind, destination, target_id))
	_next_command_sequence += 1
	_commands.sort_custom(_command_before)

func order_move(ids: Array, destination: Vector2i) -> void:
	for value in ids:
		schedule_command(int(value), Stage1Types.OrderKind.MOVE, destination)

func order_stop(ids: Array) -> void:
	for value in ids:
		var horde := get_horde(int(value))
		if horde != null:
			schedule_command(horde.id, Stage1Types.OrderKind.STOP, horde.anchor)

func order_attack_move(ids: Array, destination: Vector2i) -> void:
	for value in ids:
		schedule_command(int(value), Stage1Types.OrderKind.ATTACK_MOVE, destination)

func order_attack(ids: Array, target_id: int) -> void:
	var destination := _entity_position(target_id)
	for value in ids:
		schedule_command(int(value), Stage1Types.OrderKind.ATTACK_TARGET, destination, target_id)

func _command_before(left: Stage1Types.Command, right: Stage1Types.Command) -> bool:
	if left.execute_tick != right.execute_tick:
		return left.execute_tick < right.execute_tick
	if left.sequence != right.sequence:
		return left.sequence < right.sequence
	return left.horde_id < right.horde_id

func tick() -> void:
	_capture_previous_positions()
	if stage2_enabled:
		stage2.apply_commands(self)
		stage2.advance_construction(self)
		stage2.advance_income(self)
		stage2.advance_production(self)
	_apply_commands()
	_update_engagements()
	_move_horde_anchors()
	_move_members_to_formation()
	_resolve_member_combat()
	_move_projectiles()
	_check_victory()
	tick_index += 1

func advance(tick_count: int) -> void:
	for _i in tick_count:
		tick()

func _capture_previous_positions() -> void:
	for horde in hordes:
		horde.previous_anchor = horde.anchor
		for member in horde.members:
			member.previous_position = member.position
	for projectile in projectiles:
		projectile.previous_position = projectile.position

func _apply_commands() -> void:
	while _next_command_index < _commands.size() and _commands[_next_command_index].execute_tick <= tick_index:
		var command := _commands[_next_command_index]
		_next_command_index += 1
		var horde := get_horde(command.horde_id)
		if horde == null or not horde.is_alive():
			continue
		horde.order = command.kind
		horde.target_id = command.target_id if command.kind == Stage1Types.OrderKind.ATTACK_TARGET else 0
		horde.engaged_horde_id = 0
		if command.kind == Stage1Types.OrderKind.STOP:
			horde.destination = horde.anchor
			horde.path.clear()
			horde.path_index = 0
			continue
		var destination := command.destination
		if command.kind == Stage1Types.OrderKind.ATTACK_TARGET:
			destination = _entity_position(command.target_id)
		_set_horde_path(horde, destination)

func _set_horde_path(horde: Stage1Types.Horde, destination: Vector2i) -> void:
	destination = _clamp_to_world(destination)
	horde.destination = destination
	horde.path = grid.find_path(horde.anchor, destination)
	horde.path_index = 1 if horde.path.size() > 1 else 0
	horde.path_revision += 1

func _update_engagements() -> void:
	for horde in hordes:
		horde.engaged_horde_id = 0
		if not horde.is_alive():
			continue
		if horde.order == Stage1Types.OrderKind.ATTACK_TARGET:
			var target_member := get_member(horde.target_id)
			if target_member != null and target_member.is_alive():
				horde.engaged_horde_id = target_member.horde_id
				continue
			var target_horde := get_horde(horde.target_id)
			if target_horde != null and target_horde.is_alive():
				horde.engaged_horde_id = target_horde.id
			continue
		if horde.order != Stage1Types.OrderKind.ATTACK_MOVE:
			continue
		var best_distance := ENGAGEMENT_DISTANCE * ENGAGEMENT_DISTANCE
		var best_id := 0
		for candidate in hordes:
			if candidate.team == horde.team or not candidate.is_alive():
				continue
			var distance := _distance_squared(horde.anchor, candidate.anchor)
			if distance < best_distance or (distance == best_distance and (best_id == 0 or candidate.id < best_id)):
				best_distance = distance
				best_id = candidate.id
		horde.engaged_horde_id = best_id

func _move_horde_anchors() -> void:
	for horde in hordes:
		if not horde.is_alive() or horde.order == Stage1Types.OrderKind.STOP:
			continue
		if _should_hold_anchor(horde):
			continue
		if horde.path_index < horde.path.size() and grid.is_blocked(horde.path[horde.path_index]):
			_set_horde_path(horde, horde.destination)
		var following_path := horde.path_index < horde.path.size()
		var target: Vector2i
		if following_path:
			target = Stage1Grid.cell_center(horde.path[horde.path_index])
		elif horde.anchor != horde.destination:
			target = horde.destination
		else:
			continue
		var candidate := _move_towards(horde.anchor, target, HORDE_MOVE_PER_TICK)
		if grid.is_walkable(candidate):
			horde.anchor = candidate
		if following_path and horde.anchor == target:
			horde.path_index += 1

func _should_hold_anchor(horde: Stage1Types.Horde) -> bool:
	if horde.engaged_horde_id != 0:
		var target := get_horde(horde.engaged_horde_id)
		if target != null and _distance_squared(horde.anchor, target.anchor) <= ANCHOR_HOLD_DISTANCE * ANCHOR_HOLD_DISTANCE:
			return true
	if horde.order == Stage1Types.OrderKind.ATTACK_TARGET:
		var fortress := get_fortress(horde.target_id)
		if fortress != null and fortress.is_alive() and _distance_squared(horde.anchor, fortress.position) <= FORTRESS_RANGE * FORTRESS_RANGE:
			return true
	return false

func _move_members_to_formation() -> void:
	for horde in hordes:
		var member_count := horde.members.size()
		for member in horde.members:
			if not member.is_alive():
				continue
			var slot_position := _clamp_to_world(horde.anchor + _oriented_formation_offset(horde, member.slot, member_count))
			var movement_target := _shared_corridor_target(horde, member, slot_position)
			_try_move_member(member, movement_target, MEMBER_MOVE_PER_TICK)
		_apply_stable_separation(horde)

func _shared_corridor_target(horde: Stage1Types.Horde, member: Stage1Types.Member, formation_slot: Vector2i) -> Vector2i:
	if horde.path.is_empty() or _distance_squared(member.position, horde.anchor) <= 2200 * 2200:
		return formation_slot
	var nearest_index := 0
	var nearest_distance := 0x7fffffffffffffff
	for index in horde.path.size():
		var distance := _distance_squared(member.position, Stage1Grid.cell_center(horde.path[index]))
		if distance < nearest_distance:
			nearest_distance = distance
			nearest_index = index
	var look_ahead := mini(nearest_index + 2, horde.path.size() - 1)
	return Stage1Grid.cell_center(horde.path[look_ahead])

func _apply_stable_separation(horde: Stage1Types.Horde) -> void:
	for left_index in horde.members.size():
		var left := horde.members[left_index]
		if not left.is_alive():
			continue
		for right_index in range(left_index + 1, horde.members.size()):
			var right := horde.members[right_index]
			if not right.is_alive() or _distance_squared(left.position, right.position) >= 300 * 300:
				continue
			var delta := right.position - left.position
			if absi(delta.x) >= absi(delta.y):
				var direction := (1 if left.id < right.id else -1) if delta.x == 0 else signi(delta.x)
				_try_nudge(left, left.position + Vector2i(-direction * 24, 0))
				_try_nudge(right, right.position + Vector2i(direction * 24, 0))
			else:
				var direction := signi(delta.y)
				_try_nudge(left, left.position + Vector2i(0, -direction * 24))
				_try_nudge(right, right.position + Vector2i(0, direction * 24))

func _resolve_member_combat() -> void:
	var damage_events: Array[Array] = []
	var melee_contacts: Dictionary = {}
	for horde in hordes:
		var target_horde := get_horde(horde.engaged_horde_id) if horde.engaged_horde_id != 0 else null
		if target_horde != null and not target_horde.is_alive():
			target_horde = null
		var target_fortress := _find_fortress_target(horde)
		for member in horde.members:
			if not member.is_alive():
				continue
			if member.cooldown > 0:
				member.cooldown -= 1
			if target_fortress != null:
				member.target_id = target_fortress.id
				_attack_or_approach(member, target_fortress.position, target_fortress.id, true, damage_events)
				continue
			var target_member := _nearest_eligible_member(member, target_horde, melee_contacts) if target_horde != null else null
			if target_member != null:
				member.target_id = target_member.id
				if not member.ranged:
					melee_contacts[target_member.id] = int(melee_contacts.get(target_member.id, 0)) + 1
				_attack_or_approach(member, target_member.position, target_member.id, false, damage_events)
			else:
				member.target_id = 0
	_apply_damage(damage_events)

func _find_fortress_target(horde: Stage1Types.Horde) -> Stage1Types.Fortress:
	if horde.order == Stage1Types.OrderKind.ATTACK_TARGET:
		var explicit := get_fortress(horde.target_id)
		if explicit != null and explicit.team != horde.team and explicit.is_alive():
			return explicit
		return null
	if horde.order != Stage1Types.OrderKind.ATTACK_MOVE or horde.engaged_horde_id != 0:
		return null
	var best: Stage1Types.Fortress = null
	var best_distance := ENGAGEMENT_DISTANCE * ENGAGEMENT_DISTANCE
	for fortress in fortresses:
		if fortress.team == horde.team or not fortress.is_alive():
			continue
		var distance := _distance_squared(horde.anchor, fortress.position)
		if distance < best_distance or (distance == best_distance and (best == null or fortress.id < best.id)):
			best = fortress
			best_distance = distance
	return best

func _attack_or_approach(attacker: Stage1Types.Member, target_position: Vector2i, target_id: int, target_is_fortress: bool, damage_events: Array[Array]) -> void:
	var attack_range := RANGED_RANGE if attacker.ranged else (FORTRESS_RANGE if target_is_fortress else MELEE_RANGE)
	if _distance_squared(attacker.position, target_position) > attack_range * attack_range:
		return
	if attacker.cooldown != 0:
		return
	if attacker.ranged:
		projectiles.append(Stage1Types.Projectile.new(
			_next_projectile_id,
			attacker.team,
			attacker.id,
			target_id,
			attacker.position,
			target_position
		))
		_next_projectile_id += 1
		attacker.cooldown = RANGED_COOLDOWN
	else:
		damage_events.append([attacker.id, target_id, MELEE_DAMAGE])
		attacker.cooldown = MELEE_COOLDOWN

func _move_projectiles() -> void:
	var damage_events: Array[Array] = []
	for projectile in projectiles:
		if not projectile.active:
			continue
		var target := _living_entity(projectile.target_id)
		if not target.is_empty():
			projectile.last_target = target.position
		projectile.position = _clamp_to_world(_move_towards(projectile.position, projectile.last_target, PROJECTILE_MOVE_PER_TICK))
		if projectile.position != projectile.last_target:
			continue
		projectile.active = false
		if not target.is_empty() and int(target.team) != projectile.team:
			damage_events.append([projectile.source_id, projectile.target_id, projectile.damage])
	_apply_damage(damage_events)
	var active_projectiles: Array[Stage1Types.Projectile] = []
	for projectile in projectiles:
		if projectile.active:
			active_projectiles.append(projectile)
	projectiles = active_projectiles

func _apply_damage(events: Array[Array]) -> void:
	for damage in events:
		var target_id := int(damage[1])
		var amount := int(damage[2])
		var member := get_member(target_id)
		if member != null and member.is_alive():
			member.health = maxi(0, member.health - amount)
			continue
		var fortress := get_fortress(target_id)
		if fortress != null and fortress.is_alive():
			fortress.health = maxi(0, fortress.health - amount)

func _check_victory() -> void:
	if fortresses.size() < 2:
		return
	var blue_alive := false
	var red_alive := false
	for fortress in fortresses:
		if fortress.team == Stage1Types.Team.BLUE and fortress.is_alive():
			blue_alive = true
		elif fortress.team == Stage1Types.Team.RED and fortress.is_alive():
			red_alive = true
	if blue_alive and not red_alive:
		winner = Stage1Types.Team.BLUE
	elif red_alive and not blue_alive:
		winner = Stage1Types.Team.RED
	else:
		winner = Stage1Types.Team.NONE

func _nearest_eligible_member(attacker: Stage1Types.Member, target_horde: Stage1Types.Horde, melee_contacts: Dictionary) -> Stage1Types.Member:
	var best: Stage1Types.Member = null
	var best_distance := 0x7fffffffffffffff
	for candidate in target_horde.members:
		if not candidate.is_alive():
			continue
		var distance := _distance_squared(attacker.position, candidate.position)
		if not attacker.ranged and (int(melee_contacts.get(candidate.id, 0)) >= 4 or distance > MELEE_RANGE * MELEE_RANGE):
			continue
		if distance < best_distance or (distance == best_distance and (best == null or candidate.id < best.id)):
			best = candidate
			best_distance = distance
	return best

func _entity_team(entity_id: int) -> int:
	var member := get_member(entity_id)
	if member != null:
		return member.team
	var horde := get_horde(entity_id)
	if horde != null:
		return horde.team
	var fortress := get_fortress(entity_id)
	if fortress != null:
		return fortress.team
	return Stage1Types.Team.NONE

func _living_entity(entity_id: int) -> Dictionary:
	var member := get_member(entity_id)
	if member != null and member.is_alive():
		return {"team": member.team, "position": member.position}
	var fortress := get_fortress(entity_id)
	if fortress != null and fortress.is_alive():
		return {"team": fortress.team, "position": fortress.position}
	return {}

func _entity_position(entity_id: int) -> Vector2i:
	var living := _living_entity(entity_id)
	if not living.is_empty():
		return living.position
	var horde := get_horde(entity_id)
	if horde != null:
		return horde.anchor
	return Vector2i.ZERO

func _try_move_member(member: Stage1Types.Member, target: Vector2i, maximum_step: int) -> void:
	var candidate := _clamp_to_world(_move_towards(member.position, target, maximum_step))
	if grid.is_walkable(candidate):
		member.position = candidate
		return
	var horizontal := Vector2i(candidate.x, member.position.y)
	if grid.is_walkable(horizontal):
		member.position = horizontal
		return
	var vertical := Vector2i(member.position.x, candidate.y)
	if grid.is_walkable(vertical):
		member.position = vertical

func _try_nudge(member: Stage1Types.Member, position: Vector2i) -> void:
	position = _clamp_to_world(position)
	if grid.is_walkable(position):
		member.position = position

func validate_state() -> String:
	var ids: Dictionary = {}
	for fortress in fortresses:
		if ids.has(fortress.id) or fortress.health < 0 or fortress.health > fortress.max_health or not grid.is_walkable(fortress.position):
			return "fortress_%d" % fortress.id
		ids[fortress.id] = true
	for horde in hordes:
		if ids.has(horde.id) or not grid.is_walkable(horde.anchor) or not _in_world(horde.destination) or horde.path_index < 0 or horde.path_index > horde.path.size():
			return "horde_%d" % horde.id
		ids[horde.id] = true
		var slots: Dictionary = {}
		var previous_member_id := -1
		for member in horde.members:
			if ids.has(member.id) or member.id <= previous_member_id or slots.has(member.slot) or member.health < 0 or member.health > member.max_health or member.cooldown < 0 or not grid.is_walkable(member.position):
				return "member_%d" % member.id
			if member.horde_id != horde.id or member.team != horde.team:
				return "membership_%d" % member.id
			ids[member.id] = true
			slots[member.slot] = true
			previous_member_id = member.id
	for projectile in projectiles:
		if ids.has(projectile.id) or not _in_world(projectile.position):
			return "projectile_%d" % projectile.id
		ids[projectile.id] = true
	if stage2_enabled:
		var economy_error := stage2.validate_state(self)
		if economy_error != "":
			return economy_error
	return ""

func snapshot() -> Dictionary:
	var horde_rows: Array[Dictionary] = []
	for horde in hordes:
		var member_rows: Array[Dictionary] = []
		for member in horde.members:
			member_rows.append({
				"id": member.id,
				"slot": member.slot,
				"ranged": member.ranged,
				"position": member.position,
				"previous_position": member.previous_position,
				"health": member.health,
				"max_health": member.max_health,
			})
		horde_rows.append({
			"id": horde.id,
			"team": horde.team,
			"anchor": horde.anchor,
			"previous_anchor": horde.previous_anchor,
			"order": horde.order,
			"target_id": horde.target_id,
			"alive": horde.alive_count(),
			"members": member_rows,
		})
	var fortress_rows: Array[Dictionary] = []
	for fortress in fortresses:
		fortress_rows.append({
			"id": fortress.id,
			"team": fortress.team,
			"position": fortress.position,
			"health": fortress.health,
			"max_health": fortress.max_health,
		})
	var projectile_rows: Array[Dictionary] = []
	for projectile in projectiles:
		projectile_rows.append({
			"id": projectile.id,
			"team": projectile.team,
			"position": projectile.position,
			"previous_position": projectile.previous_position,
		})
	var economy_rows: Array[Dictionary] = []
	var building_rows: Array[Dictionary] = []
	if stage2_enabled:
		for economy in stage2.economies:
			economy_rows.append({
				"team": economy.team,
				"resources": economy.resources,
				"total_earned": economy.total_earned,
				"population_used": stage2.population_used(self, economy.team),
				"population_reserved": stage2.population_reserved(economy.team),
				"population_cap": economy.population_cap,
			})
		for building in stage2.buildings:
			var definition := stage2.get_definition(building.type_code)
			var queue_rows: Array[Dictionary] = []
			for job in building.jobs:
				queue_rows.append({
					"id": job.id,
					"type_code": job.type_code,
					"remaining_ticks": job.remaining_ticks,
					"enqueued_tick": job.enqueued_tick,
					"population_reserved": job.population_reserved,
				})
			building_rows.append({
				"id": building.id,
				"type_code": building.type_code,
				"role": definition.role_code,
				"team": building.team,
				"position": building.position,
				"health": building.health,
				"max_health": building.maximum_health,
				"progress_ticks": building.progress_ticks,
				"construction_ticks": building.construction_ticks,
				"complete": building.completed,
				"destroyed": building.destroyed,
				"efficiency_permille": stage2.farm_efficiency(building),
				"next_income_tick": building.next_income_tick,
				"has_rally": building.has_rally_point,
				"rally": building.rally_point,
				"queue": queue_rows,
				"trains": definition.train_type_codes.duplicate(),
			})
	return {
		"tick": tick_index,
		"winner": winner,
		"scale": Stage1Grid.CELL_SIZE,
		"grid_size": Vector2i(grid.width, grid.height),
		"blocked": grid.blocked_cells(),
		"hordes": horde_rows,
		"fortresses": fortress_rows,
		"projectiles": projectile_rows,
		"economies": economy_rows,
		"buildings": building_rows,
	}

func state_hash() -> int:
	var hash := 2166136261
	hash = _hash_int(hash, tick_index)
	hash = _hash_int(hash, winner)
	hash = _hash_int(hash, TICKS_PER_SECOND)
	hash = _hash_int(hash, grid.width)
	hash = _hash_int(hash, grid.height)
	hash = _hash_int(hash, _next_horde_id)
	hash = _hash_int(hash, _next_member_id)
	hash = _hash_int(hash, _next_projectile_id)
	hash = _hash_int(hash, -5)
	hash = _hash_int(hash, _commands.size() - _next_command_index)
	for index in range(_next_command_index, _commands.size()):
		var command := _commands[index]
		for value in [command.execute_tick, command.sequence, command.horde_id, command.kind, command.destination.x, command.destination.y, command.target_id]:
			hash = _hash_int(hash, int(value))
	for cell in grid.blocked_cells():
		hash = _hash_int(hash, cell.x)
		hash = _hash_int(hash, cell.y)
	hash = _hash_int(hash, -10)
	for fortress in fortresses:
		for value in [fortress.id, fortress.team, fortress.position.x, fortress.position.y, fortress.health, fortress.max_health]:
			hash = _hash_int(hash, int(value))
	hash = _hash_int(hash, -20)
	for horde in hordes:
		for value in [horde.id, horde.team, horde.anchor.x, horde.anchor.y, horde.destination.x, horde.destination.y, horde.order, horde.target_id, horde.engaged_horde_id, horde.path_index, horde.path_revision]:
			hash = _hash_int(hash, int(value))
		hash = _hash_int(hash, horde.path.size())
		for cell in horde.path:
			hash = _hash_int(hash, cell.x)
			hash = _hash_int(hash, cell.y)
		for member in horde.members:
			for value in [member.id, member.horde_id, member.team, member.slot, int(member.ranged), member.position.x, member.position.y, member.health, member.max_health, member.cooldown, member.target_id]:
				hash = _hash_int(hash, int(value))
		hash = _hash_int(hash, -30)
	hash = _hash_int(hash, -40)
	for projectile in projectiles:
		for value in [projectile.id, projectile.team, projectile.source_id, projectile.target_id, projectile.position.x, projectile.position.y, projectile.last_target.x, projectile.last_target.y, projectile.damage, int(projectile.active)]:
			hash = _hash_int(hash, int(value))
	if not stage2_enabled:
		return hash & 0xffffffff
	hash = _hash_int(hash, -50)
	hash = _hash_int(hash, 1)
	for value in [
		stage2.rules.version,
		stage2.next_building_id,
		stage2.next_job_id,
		stage2.rules.maximum_queue_length,
		stage2.rules.construction_health_ramp_code,
		stage2.rules.building_blocks_at_code,
		stage2.rules.queued_population_counts_code,
		stage2.rules.spawn_search_radius_cells,
		stage2.rules.spawn_search_order.size(),
	]:
		hash = _hash_int(hash, int(value))
	for direction in stage2.rules.spawn_search_order:
		hash = _hash_int(hash, int(direction))
	for value in [
		stage2.rules.efficiency_radius_subcells,
		stage2.rules.base_efficiency_permille,
		stage2.rules.neighbor_penalty_permille,
		stage2.rules.minimum_efficiency_permille,
		stage2.economies.size(),
	]:
		hash = _hash_int(hash, int(value))
	for economy in stage2.economies:
		for value in [economy.team, economy.resources, stage2.population_used(self, economy.team), stage2.population_reserved(economy.team), economy.population_cap, economy.total_earned]:
			hash = _hash_int(hash, int(value))
	hash = _hash_int(hash, -51)
	hash = _hash_int(hash, stage2.commands.size() - stage2.next_command_index)
	for index in range(stage2.next_command_index, stage2.commands.size()):
		var economy_command := stage2.commands[index]
		for value in [economy_command.execute_tick, economy_command.sequence, economy_command.kind, economy_command.team, economy_command.building_id, economy_command.type_code, economy_command.destination.x, economy_command.destination.y]:
			hash = _hash_int(hash, int(value))
	hash = _hash_int(hash, -60)
	hash = _hash_int(hash, stage2.definitions.size())
	for definition in stage2.definitions:
		for value in [definition.type_code, definition.role_code, definition.cost, definition.construction_ticks, definition.maximum_health, definition.width_cells, definition.height_cells, definition.build_menu_slot, definition.income_amount, definition.income_interval_ticks, definition.train_type_codes.size()]:
			hash = _hash_int(hash, int(value))
		for type_code in definition.train_type_codes:
			hash = _hash_int(hash, int(type_code))
	hash = _hash_int(hash, -61)
	hash = _hash_int(hash, stage2.blueprints.size())
	for blueprint in stage2.blueprints:
		for value in [blueprint.type_code, blueprint.member_count, blueprint.ranged_count, blueprint.cost, blueprint.production_ticks, blueprint.population, blueprint.train_menu_slot]:
			hash = _hash_int(hash, int(value))
	hash = _hash_int(hash, -70)
	hash = _hash_int(hash, stage2.buildings.size())
	for building in stage2.buildings:
		for value in [building.id, building.team, building.type_code, building.position.x, building.position.y, building.health, building.construction_health_cap, building.progress_ticks, int(building.completed), int(building.destroyed), int(building.has_rally_point), building.rally_point.x, building.rally_point.y, building.next_income_tick, stage2.farm_efficiency(building), building.jobs.size()]:
			hash = _hash_int(hash, int(value))
		for job in building.jobs:
			for value in [job.id, job.type_code, job.remaining_ticks, job.enqueued_tick, job.population_reserved]:
				hash = _hash_int(hash, int(value))
	return hash & 0xffffffff

func state_hash_text() -> String:
	return "%08X" % state_hash()

func _hash_int(hash: int, value: int) -> int:
	var result := hash
	var unsigned := value & 0xffffffff
	for shift in [0, 8, 16, 24]:
		result = ((result ^ ((unsigned >> shift) & 0xff)) * 16777619) & 0xffffffff
	return result

static func formation_offset(slot: int, member_count: int) -> Vector2i:
	var columns := mini(5, member_count)
	var rows := (member_count + columns - 1) / columns
	var column := slot % columns
	var row := slot / columns
	return Vector2i(((column * 2) - (columns - 1)) * 325, ((row * 2) - (rows - 1)) * 325)

func _oriented_formation_offset(horde: Stage1Types.Horde, slot: int, member_count: int) -> Vector2i:
	var local := formation_offset(slot, member_count)
	var target := Stage1Grid.cell_center(horde.path[horde.path_index]) if horde.path_index < horde.path.size() else horde.destination
	var delta := target - horde.anchor
	var forward: Vector2i
	if absi(delta.x) >= absi(delta.y) and delta.x != 0:
		forward = Vector2i(signi(delta.x), 0)
	elif delta.y != 0:
		forward = Vector2i(0, signi(delta.y))
	else:
		forward = Vector2i(1, 0) if horde.team == Stage1Types.Team.BLUE else Vector2i(-1, 0)
	var right := Vector2i(-forward.y, forward.x)
	return right * local.x + forward * local.y

func _in_world(position: Vector2i) -> bool:
	return position.x >= 0 and position.x < grid.width * Stage1Grid.CELL_SIZE and position.y >= 0 and position.y < grid.height * Stage1Grid.CELL_SIZE

func _clamp_to_world(position: Vector2i) -> Vector2i:
	return Vector2i(
		clampi(position.x, 0, grid.width * Stage1Grid.CELL_SIZE - 1),
		clampi(position.y, 0, grid.height * Stage1Grid.CELL_SIZE - 1)
	)

func _distance_squared(a: Vector2i, b: Vector2i) -> int:
	var dx := a.x - b.x
	var dy := a.y - b.y
	return dx * dx + dy * dy

func _move_towards(from: Vector2i, to: Vector2i, maximum_step: int) -> Vector2i:
	var delta := to - from
	var distance_squared := delta.x * delta.x + delta.y * delta.y
	if distance_squared == 0 or distance_squared <= maximum_step * maximum_step:
		return to
	var distance := _integer_sqrt(distance_squared)
	if distance == 0:
		return from
	var step_x := delta.x * maximum_step / distance
	var step_y := delta.y * maximum_step / distance
	if step_x == 0 and delta.x != 0:
		step_x = signi(delta.x)
	if step_y == 0 and delta.y != 0:
		step_y = signi(delta.y)
	return from + Vector2i(step_x, step_y)

func _integer_sqrt(value: int) -> int:
	if value <= 0:
		return 0
	var n := value
	var result := 0
	var bit := 1 << 62
	while bit > n:
		bit >>= 2
	while bit != 0:
		if n >= result + bit:
			n -= result + bit
			result = (result >> 1) + bit
		else:
			result >>= 1
		bit >>= 2
	return result
