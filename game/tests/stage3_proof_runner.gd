extends SceneTree
## godot --headless --path game -s res://tests/stage3_proof_runner.gd

const TopologyScript = preload("res://src/proof_stage3/topology_grid.gd")
const VisionScript = preload("res://src/proof_stage3/vision_system.gd")
const StructureScript = preload("res://src/proof_stage3/structure_system.gd")
const WorldScript = preload("res://src/proof_stage3/proof_world.gd")

var passed: int = 0
var failed: int = 0
var repeat_hash_text: String = "00000000"
var local_edit_ms: float = 0.0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "STAGE3_PROOF_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_test_snap_and_quarter_rotation()
	_test_external_definition_root()
	_test_atomic_invalid_chain()
	_test_live_unit_occupancy_transactions()
	_test_gate_path_rules_and_replan()
	_test_tower_target_cooldown_and_projectile()
	_test_attachment_charge_and_compatibility()
	_test_fog_reveal_persistence_hide_and_filter()
	_test_state_hash_mutation_sensitivity()
	_test_repeat_hash()
	_test_local_topology_edit_budget()
	print("STAGE3_METRICS local_edit_ms=%.3f repeat_hash=%s assertions=%d" % [local_edit_ms, repeat_hash_text, passed + failed])
	if failed == 0:
		print("STAGE3_GODOT_PROOF PASS authority=gdscript-proof assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE3_GODOT_PROOF FAIL authority=gdscript-proof assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _definitions() -> Dictionary:
	return {
		"schema": "openbfme.defenses",
		"schemaVersion": 0,
		"structures": [{
			"id": "wall",
			"kind": "wall",
			"maximumHealth": 400,
			"cost": 10,
			"blocksMovement": true,
		},
		{
			"id": "gate",
			"kind": "gate",
			"maximumHealth": 500,
			"cost": 30,
			"blocksMovement": true,
		},
		{
			"id": "tower",
			"kind": "tower",
			"maximumHealth": 600,
			"cost": 80,
			"blocksMovement": true,
			"canFire": true,
			"rangeCells": 6,
			"cooldownTicks": 3,
			"projectileSpeedCellsPerTick": 1,
			"damage": 25,
			"visionRadiusCells": 5,
		},
		{
			"id": "wall_tower",
			"kind": "wall_tower",
			"maximumHealth": 300,
			"cost": 45,
			"attachment": true,
			"compatibleBaseKinds": ["wall"],
			"canFire": true,
			"rangeCells": 4,
			"cooldownTicks": 5,
			"projectileSpeedCellsPerTick": 1,
			"damage": 18,
			"visionRadiusCells": 4,
		}],
	}


func _world(width: int = 16, height: int = 10) -> WorldScript:
	var world := WorldScript.new(width, height, _definitions()) as WorldScript
	world.structures.set_team_resources(WorldScript.TEAM_BLUE, 10000)
	world.structures.set_team_resources(WorldScript.TEAM_RED, 10000)
	return world


func _center(cell: Vector2i) -> Vector2i:
	return TopologyScript.cell_center(cell)


func _test_external_definition_root() -> void:
	var path: String = ProjectSettings.globalize_path("res://../content/openbfme-test/data/defenses.json")
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	_check("external_defenses_json_parses", parsed is Dictionary)
	if not parsed is Dictionary:
		return
	var world := WorldScript.new(10, 8, parsed as Dictionary) as WorldScript
	world.structures.set_team_resources(0, 1000)
	var placed: Dictionary = world.place_structure("tower", 0, _center(Vector2i(3, 3)))
	var tower: StructureScript.StructureRecord = world.structures.get_structure(int(placed.get("id", 0)))
	_check("external_definition_array_indexes_by_id", bool(placed.get("ok", false)) and tower != null and tower.definition_id == "tower")
	_check("external_definition_fields_drive_runtime", tower != null and tower.max_health == 1200 and world.structures.resources_for(0) == 750 and int(tower.definition.get("rangeCells", 0)) == 8)


func _test_snap_and_quarter_rotation() -> void:
	var topology := TopologyScript.new(8, 8) as TopologyScript
	_check("snap_uses_integer_cell_center", topology.snap_position(Vector2i(1499, 2499)) == _center(Vector2i(1, 2)))
	_check("quarter_rotation_wraps_positive", TopologyScript.normalize_quarter_rotation(5) == 1)
	_check("quarter_rotation_wraps_negative", TopologyScript.normalize_quarter_rotation(-1) == 3)
	_check("quarter_rotation_directions", TopologyScript.quarter_direction(0) == Vector2i.RIGHT and TopologyScript.quarter_direction(1) == Vector2i.DOWN and TopologyScript.quarter_direction(2) == Vector2i.LEFT and TopologyScript.quarter_direction(3) == Vector2i.UP)
	var world: WorldScript = _world(8, 8)
	var placed: Dictionary = world.place_structure("wall", WorldScript.TEAM_BLUE, Vector2i(1499, 2499), 5)
	var wall: StructureScript.StructureRecord = world.structures.get_structure(int(placed.get("id", 0)))
	_check("wall_segment_places_snapped", bool(placed.get("ok", false)) and wall != null and wall.cell == Vector2i(1, 2) and wall.position == _center(Vector2i(1, 2)))
	_check("wall_segment_stores_quarter_rotation", wall != null and wall.rotation_quarters == 1)
	_check("wall_segment_blocks_both_teams", world.topology.is_blocked(Vector2i(1, 2), 0) and world.topology.is_blocked(Vector2i(1, 2), 1))


func _test_atomic_invalid_chain() -> void:
	var world: WorldScript = _world(8, 8)
	var resources_before: int = world.structures.resources_for(0)
	var revision_before: int = world.topology.revision
	var invalid_chain: Array[Dictionary] = [
		{"definition": "wall", "owner": 0, "position": _center(Vector2i(1, 1)), "rotation_quarters": 0},
		{"definition": "wall", "owner": 0, "position": _center(Vector2i(2, 1)), "rotation_quarters": 0},
		{"definition": "wall", "owner": 0, "position": _center(Vector2i(4, 1)), "rotation_quarters": 0},
	]
	var rejected: Dictionary = world.place_chain(invalid_chain)
	_check("invalid_chain_rejected", not bool(rejected.get("ok", true)) and String(rejected.get("error", "")) == "chain_not_contiguous")
	_check("invalid_chain_is_atomic_structures", world.structures.structures.is_empty())
	_check("invalid_chain_is_atomic_topology", world.topology.revision == revision_before and world.topology.mask_at(Vector2i(1, 1)) == 0)
	_check("invalid_chain_is_atomic_charge", world.structures.resources_for(0) == resources_before)
	var valid: Dictionary = world.place_structure("wall", 0, _center(Vector2i(1, 1)))
	_check("invalid_chain_does_not_consume_stable_id", bool(valid.get("ok", false)) and int(valid.get("id", 0)) == 1)


func _test_live_unit_occupancy_transactions() -> void:
	var world: WorldScript = _world(10, 8)
	world.add_unit(0, _center(Vector2i(3, 3)), 100, 2, 1500)
	var resources_before: int = world.structures.resources_for(0)
	var revision_before: int = world.topology.revision
	var next_id_before: int = world.structures.next_structure_id()
	var rejected_single: Dictionary = world.place_structure("wall", 0, _center(Vector2i(3, 3)))
	_check("live_unit_blocks_authoritative_structure_placement", not bool(rejected_single.get("ok", true)) and String(rejected_single.get("error", "")) == "live_unit_occupied")
	_check("live_unit_structure_rejection_transactional", world.structures.resources_for(0) == resources_before and world.topology.revision == revision_before and world.structures.next_structure_id() == next_id_before and world.structures.structures.is_empty())
	var occupied_chain: Array[Dictionary] = [
		{"definition": "wall", "owner": 0, "position": _center(Vector2i(2, 3)), "rotation_quarters": 0},
		{"definition": "gate", "owner": 0, "position": _center(Vector2i(3, 3)), "rotation_quarters": 0},
	]
	var rejected_chain: Dictionary = world.place_chain(occupied_chain)
	_check("live_unit_blocks_authoritative_chain_placement", not bool(rejected_chain.get("ok", true)) and String(rejected_chain.get("error", "")) == "live_unit_occupied")
	_check("live_unit_chain_rejection_transactional", world.structures.resources_for(0) == resources_before and world.topology.revision == revision_before and world.structures.next_structure_id() == next_id_before and world.structures.structures.is_empty())
	var accepted: Dictionary = world.place_structure("wall", 0, _center(Vector2i(4, 3)))
	_check("occupancy_rejection_does_not_consume_stable_id", bool(accepted.get("ok", false)) and int(accepted.get("id", 0)) == next_id_before)


func _test_gate_path_rules_and_replan() -> void:
	var world: WorldScript = _world(9, 5)
	var chain: Array[Dictionary] = []
	for y: int in range(5):
		chain.append({
			"definition": "gate" if y == 2 else "wall",
			"owner": 0,
			"position": _center(Vector2i(4, y)),
			"rotation_quarters": 1,
		})
	var placement: Dictionary = world.place_chain(chain)
	var gate_id: int = int((placement.get("ids", []) as Array)[2]) if bool(placement.get("ok", false)) else 0
	var start := Vector2i(1, 2)
	var destination := Vector2i(7, 2)
	_check("closed_gate_blocks_owner", world.topology.find_path(start, destination, 0).is_empty())
	_check("closed_gate_blocks_enemy", world.topology.find_path(start, destination, 1).is_empty())
	var revision_after_build: int = world.topology.revision
	_check("gate_opens_for_owner", world.structures.set_gate_open(gate_id, true) and world.topology.find_path(start, destination, 0).has(Vector2i(4, 2)))
	_check("open_gate_still_blocks_enemy", world.topology.find_path(start, destination, 1).is_empty())
	_check("gate_state_increments_topology_once", world.topology.revision == revision_after_build + 1)
	var blue: WorldScript.UnitRecord = world.add_unit(0, _center(start), 100, 2)
	_check("owner_order_uses_open_gate", world.order_move(blue.id, destination) and world.path_contains(blue.id, Vector2i(4, 2)))
	world.tick()
	var cell_before_close: Vector2i = blue.cell
	world.structures.set_gate_open(gate_id, false)
	var closed_revision: int = world.topology.revision
	world.tick()
	_check("topology_change_triggers_replan", blue.replan_count == 1 and blue.path_revision == closed_revision)
	_check("replan_never_crosses_closed_gate", blue.cell == cell_before_close and blue.path.is_empty())
	var gate: StructureScript.StructureRecord = world.structures.get_structure(gate_id)
	world.structures.damage_structure(gate_id, gate.max_health)
	var blue_path: Array[Vector2i] = world.topology.find_path(start, destination, 0)
	var red_path: Array[Vector2i] = world.topology.find_path(start, destination, 1)
	_check("destroyed_gate_passes_owner", blue_path.has(Vector2i(4, 2)))
	_check("destroyed_gate_passes_enemy", red_path.has(Vector2i(4, 2)))
	_check("destroyed_gate_advances_revision", world.topology.revision == closed_revision + 1)


func _test_tower_target_cooldown_and_projectile() -> void:
	var world: WorldScript = _world(12, 8)
	var placement: Dictionary = world.place_structure("tower", 0, _center(Vector2i(2, 3)))
	var tower: StructureScript.StructureRecord = world.structures.get_structure(int(placement.get("id", 0)))
	var high_id: WorldScript.UnitRecord = world.add_unit(1, _center(Vector2i(2, 6)), 100, 0, 2000)
	var low_id: WorldScript.UnitRecord = world.add_unit(1, _center(Vector2i(5, 3)), 100, 0, 1900)
	world.tick()
	_check("tower_target_order_distance_then_id", tower.last_fired_target_id == low_id.id and high_id.health == 100)
	_check("tower_spawns_projectile", world.structures.active_projectile_count() == 1)
	_check("tower_enters_cooldown", tower.cooldown_remaining == 3)
	world.tick()
	_check("tower_cooldown_prevents_refire", world.structures.active_projectile_count() == 1 and tower.cooldown_remaining == 2)
	world.tick()
	world.tick()
	_check("tower_projectile_delivers_damage", low_id.health == 75 and high_id.health == 100)
	_check("tower_projectile_resolves", world.structures.active_projectile_count() == 0)


func _test_attachment_charge_and_compatibility() -> void:
	var world: WorldScript = _world(12, 8)
	world.structures.set_team_resources(0, 250)
	var wall_result: Dictionary = world.place_structure("wall", 0, _center(Vector2i(3, 3)))
	var wall_id: int = int(wall_result.get("id", 0))
	var before_attachment: int = world.structures.resources_for(0)
	var topology_before_attachment: int = world.topology.revision
	var attached: Dictionary = world.place_structure("wall_tower", 0, _center(Vector2i(3, 3)), 0, wall_id)
	var attachment: StructureScript.StructureRecord = world.structures.get_structure(int(attached.get("id", 0)))
	_check("compatible_wall_tower_attaches", bool(attached.get("ok", false)) and attachment != null and attachment.attach_to_id == wall_id)
	_check("attachment_charges_exact_cost", world.structures.resources_for(0) == before_attachment - 45)
	_check("attachment_does_not_change_walk_topology", world.topology.revision == topology_before_attachment)
	var enemy: WorldScript.UnitRecord = world.add_unit(1, _center(Vector2i(6, 3)), 100, 0)
	world.tick()
	_check("wall_tower_attachment_can_fire", attachment.last_fired_target_id == enemy.id)
	var tower_result: Dictionary = world.place_structure("tower", 0, _center(Vector2i(8, 3)))
	var tower_id: int = int(tower_result.get("id", 0))
	var before_reject: int = world.structures.resources_for(0)
	var rejected: Dictionary = world.place_structure("wall_tower", 0, _center(Vector2i(8, 3)), 0, tower_id)
	_check("incompatible_attachment_rejected", not bool(rejected.get("ok", true)) and String(rejected.get("error", "")) == "incompatible_attachment_base")
	_check("rejected_attachment_not_charged", world.structures.resources_for(0) == before_reject)


func _test_fog_reveal_persistence_hide_and_filter() -> void:
	var world: WorldScript = _world(16, 9)
	var blue: WorldScript.UnitRecord = world.add_unit(0, _center(Vector2i(2, 2)), 100, 3, 1100)
	var red: WorldScript.UnitRecord = world.add_unit(1, _center(Vector2i(4, 2)), 100, 2, 2100)
	var tower_result: Dictionary = world.place_structure("tower", 1, _center(Vector2i(5, 2)))
	var red_tower_id: int = int(tower_result.get("id", 0))
	world.recompute_visibility()
	var revealed: Dictionary = world.team_filtered_snapshot(0)
	_check("fog_reveals_enemy_unit_in_range", _snapshot_has_id(revealed["units"], red.id))
	_check("fog_reveals_enemy_structure_in_range", _snapshot_has_id(revealed["structures"], red_tower_id))
	_check("fog_marks_revealed_cell_explored", world.vision.is_explored(0, red.cell) and world.vision.is_visible(0, red.cell))
	world.set_unit_cell(blue.id, Vector2i(0, 8))
	world.recompute_visibility()
	var hidden: Dictionary = world.team_filtered_snapshot(0)
	_check("fog_visibility_clears_each_recompute", not world.vision.is_visible(0, red.cell))
	_check("fog_exploration_persists", world.vision.is_explored(0, red.cell))
	_check("snapshot_hides_unseen_enemy_unit", not _snapshot_has_id(hidden["units"], red.id))
	_check("snapshot_hides_unseen_enemy_structure", not _snapshot_has_id(hidden["structures"], red_tower_id))
	_check("snapshot_always_contains_own_unit", _snapshot_has_id(hidden["units"], blue.id))
	var red_snapshot: Dictionary = world.team_filtered_snapshot(1)
	_check("team_filter_keeps_own_entities", _snapshot_has_id(red_snapshot["units"], red.id) and _snapshot_has_id(red_snapshot["structures"], red_tower_id))


func _test_state_hash_mutation_sensitivity() -> void:
	var world: WorldScript = _world(12, 8)
	var placement: Dictionary = world.place_structure("tower", 0, _center(Vector2i(2, 3)))
	var tower: StructureScript.StructureRecord = world.structures.get_structure(int(placement.get("id", 0)))
	var target: WorldScript.UnitRecord = world.add_unit(1, _center(Vector2i(6, 3)), 100, 2, 1900)
	world.tick()
	var projectile_ids: Array = world.structures.projectiles.keys()
	projectile_ids.sort()
	var projectile: StructureScript.ProjectileRecord = world.structures.projectiles.get(projectile_ids[0]) as StructureScript.ProjectileRecord if not projectile_ids.is_empty() else null
	_check("hash_mutation_fixture_has_future_state", tower != null and target != null and projectile != null)
	if tower == null or target == null or projectile == null:
		return
	_check("next_id_accessors_report_authoritative_state", world.next_unit_id() == 1901 and world.structures.next_structure_id() == 2 and world.structures.next_projectile_id() == 100001)
	var base_hash: int = world.state_hash()

	world._next_unit_id += 1
	var changed: bool = world.state_hash() != base_hash
	world._next_unit_id -= 1
	_check("hash_covers_next_unit_id", changed and world.state_hash() == base_hash)

	world.structures._next_structure_id += 1
	changed = world.state_hash() != base_hash
	world.structures._next_structure_id -= 1
	_check("hash_covers_next_structure_id", changed and world.state_hash() == base_hash)

	world.structures._next_projectile_id += 1
	changed = world.state_hash() != base_hash
	world.structures._next_projectile_id -= 1
	_check("hash_covers_next_projectile_id", changed and world.state_hash() == base_hash)

	target.max_health += 1
	changed = world.state_hash() != base_hash
	target.max_health -= 1
	_check("hash_covers_unit_max_health", changed and world.state_hash() == base_hash)

	target.vision_radius += 1
	changed = world.state_hash() != base_hash
	target.vision_radius -= 1
	_check("hash_covers_unit_vision_radius", changed and world.state_hash() == base_hash)

	tower.max_health += 1
	changed = world.state_hash() != base_hash
	tower.max_health -= 1
	_check("hash_covers_structure_max_health", changed and world.state_hash() == base_hash)

	tower.cooldown_remaining += 1
	changed = world.state_hash() != base_hash
	tower.cooldown_remaining -= 1
	_check("hash_covers_structure_cooldown", changed and world.state_hash() == base_hash)

	tower.last_fired_target_id += 1
	changed = world.state_hash() != base_hash
	tower.last_fired_target_id -= 1
	_check("hash_covers_structure_last_target", changed and world.state_hash() == base_hash)

	var old_kind: String = tower.kind
	tower.kind = "wall"
	changed = world.state_hash() != base_hash
	tower.kind = old_kind
	_check("hash_covers_structure_kind", changed and world.state_hash() == base_hash)

	var local_range: int = int(tower.definition.get("rangeCells", 0))
	tower.definition["rangeCells"] = local_range + 1
	changed = world.state_hash() != base_hash
	tower.definition["rangeCells"] = local_range
	_check("hash_covers_structure_definition_values", changed and world.state_hash() == base_hash)

	var global_tower: Dictionary = world.structures.definitions["tower"]
	var global_cost: int = int(global_tower.get("cost", 0))
	global_tower["cost"] = global_cost + 1
	changed = world.state_hash() != base_hash
	global_tower["cost"] = global_cost
	_check("hash_covers_unbuilt_definition_future", changed and world.state_hash() == base_hash)

	projectile.damage += 1
	changed = world.state_hash() != base_hash
	projectile.damage -= 1
	_check("hash_covers_projectile_damage", changed and world.state_hash() == base_hash)

	projectile.speed_cells += 1
	changed = world.state_hash() != base_hash
	projectile.speed_cells -= 1
	_check("hash_covers_projectile_speed", changed and world.state_hash() == base_hash)

	projectile.last_target_cell += Vector2i.DOWN
	changed = world.state_hash() != base_hash
	projectile.last_target_cell -= Vector2i.DOWN
	_check("hash_covers_projectile_last_target_cell", changed and world.state_hash() == base_hash)


func _test_repeat_hash() -> void:
	var first: WorldScript = _build_repeat_world()
	var second: WorldScript = _build_repeat_world()
	first.advance(24)
	second.advance(24)
	repeat_hash_text = first.state_hash_text()
	_check("repeat_run_hash_equal", first.state_hash() == second.state_hash(), "hash=%s" % first.state_hash_text())
	_check("repeat_run_state_valid", first.validate_state() == "" and second.validate_state() == "", first.validate_state())


func _build_repeat_world() -> WorldScript:
	var world: WorldScript = _world(18, 10)
	var chain: Array[Dictionary] = [
		{"definition": "wall", "owner": 0, "position": _center(Vector2i(7, 3)), "rotation_quarters": 1},
		{"definition": "gate", "owner": 0, "position": _center(Vector2i(7, 4)), "rotation_quarters": 1},
		{"definition": "wall", "owner": 0, "position": _center(Vector2i(7, 5)), "rotation_quarters": 1},
	]
	var placed: Dictionary = world.place_chain(chain)
	var gate_id: int = int((placed["ids"] as Array)[1])
	world.structures.set_gate_open(gate_id, true)
	world.place_structure("tower", 0, _center(Vector2i(5, 4)))
	var blue: WorldScript.UnitRecord = world.add_unit(0, _center(Vector2i(2, 4)), 140, 3, 1001)
	var red: WorldScript.UnitRecord = world.add_unit(1, _center(Vector2i(12, 4)), 140, 3, 2001)
	world.order_move(blue.id, Vector2i(11, 4))
	world.order_move(red.id, Vector2i(8, 8))
	return world


func _test_local_topology_edit_budget() -> void:
	var world: WorldScript = _world(32, 24)
	var start_usec: int = Time.get_ticks_usec()
	var result: Dictionary = world.place_structure("gate", 0, _center(Vector2i(16, 12)))
	var gate_id: int = int(result.get("id", 0))
	world.structures.set_gate_open(gate_id, true)
	var path: Array[Vector2i] = world.topology.find_path(Vector2i(1, 12), Vector2i(30, 12), 0)
	var elapsed_usec: int = Time.get_ticks_usec() - start_usec
	local_edit_ms = float(elapsed_usec) / 1000.0
	_check("local_edit_updates_revision", bool(result.get("ok", false)) and world.topology.revision == 2)
	_check("local_edit_keeps_path_service_live", not path.is_empty())
	_check("local_edit_under_100ms", elapsed_usec < 100000, "elapsed_ms=%.3f" % local_edit_ms)


func _snapshot_has_id(entries: Array, entity_id: int) -> bool:
	for raw_entry: Variant in entries:
		var entry: Dictionary = raw_entry
		if int(entry.get("id", 0)) == entity_id:
			return true
	return false
