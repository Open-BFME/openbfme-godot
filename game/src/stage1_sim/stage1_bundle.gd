class_name Stage1Bundle
extends RefCounted
## Minimal v0 loader for the committed legal-safe Stage 1 bundle.

static func default_root() -> String:
	return ProjectSettings.globalize_path("res://../content/openbfme-test")

static func load_bundle(root_path: String = "") -> Dictionary:
	var root := default_root() if root_path == "" else root_path
	var pack := _read_json(root.path_join("pack.json"))
	if pack.is_empty():
		return {}
	var files: Dictionary = pack.get("files", {})
	return {
		"root": root,
		"pack": pack,
		"map": _read_json(root.path_join(String(files.get("entryMap", "")))),
		"stage2_map": _read_json(root.path_join(String(files.get("stage2Map", "")))),
		"economy": _read_json(root.path_join(String(files.get("economy", "")))),
		"objects": _read_json(root.path_join(String(files.get("objects", "")))),
		"weapons": _read_json(root.path_join(String(files.get("weapons", "")))),
		"locomotion": _read_json(root.path_join(String(files.get("locomotion", "")))),
	}

static func verify_contract(bundle: Dictionary) -> String:
	if bundle.is_empty():
		return "bundle_missing"
	var pack: Dictionary = bundle.get("pack", {})
	var map: Dictionary = bundle.get("map", {})
	if String(pack.get("id", "")) != "openbfme-test":
		return "pack_id"
	if int(pack.get("schemaVersion", -1)) != 0 or int(map.get("schemaVersion", -1)) != 0:
		return "schema_version"
	var grid: Dictionary = map.get("grid", {})
	if int(grid.get("cellSizeSubcells", 0)) != Stage1Grid.CELL_SIZE:
		return "cell_size"
	if int(grid.get("widthCells", 0)) != 64 or int(grid.get("heightCells", 0)) != 64:
		return "arena_size"
	var weapons := _by_id(bundle.get("weapons", {}).get("weapons", []))
	var melee: Dictionary = weapons.get("test.weapon.practice-baton", {})
	var ranged: Dictionary = weapons.get("test.weapon.foam-sphere-launcher", {})
	if int(melee.get("damage", 0)) != Stage1World.MELEE_DAMAGE or int(melee.get("maximumRangeSubcells", 0)) != Stage1World.MELEE_RANGE or int(melee.get("cooldownTicks", 0)) != Stage1World.MELEE_COOLDOWN:
		return "melee_rules"
	var projectile: Dictionary = ranged.get("projectile", {})
	if int(ranged.get("damage", 0)) != 18 or int(ranged.get("maximumRangeSubcells", 0)) != Stage1World.RANGED_RANGE or int(ranged.get("cooldownTicks", 0)) != Stage1World.RANGED_COOLDOWN or int(projectile.get("speedSubcellsPerTick", 0)) != Stage1World.PROJECTILE_MOVE_PER_TICK:
		return "ranged_rules"
	var locomotion := _by_id(bundle.get("locomotion", {}).get("locomotion", []))
	var foot: Dictionary = locomotion.get("test.locomotion.foot", {})
	if int(foot.get("maximumSpeedSubcellsPerTick", 0)) != Stage1World.MEMBER_MOVE_PER_TICK or int(foot.get("hordeAnchorSpeedSubcellsPerTick", 0)) != Stage1World.HORDE_MOVE_PER_TICK:
		return "locomotion_rules"
	return ""

static func build_world(bundle: Dictionary) -> Stage1World:
	assert(verify_contract(bundle) == "")
	return _build_world_from_map(bundle, bundle.map, true)

static func verify_stage2_contract(bundle: Dictionary) -> String:
	var base_error := verify_contract(bundle)
	if base_error != "":
		return base_error
	var economy: Dictionary = bundle.get("economy", {})
	var map: Dictionary = bundle.get("stage2_map", {})
	if String(economy.get("schema", "")) != "openbfme.economy" or int(economy.get("schemaVersion", -1)) != 0 or int(economy.get("rulesVersion", -1)) != 2:
		return "economy_contract"
	if String(map.get("id", "")) != "test.map.primitive-economy" or int(map.get("scenario", {}).get("simulationTicks", 0)) != 1500:
		return "stage2_map"
	return ""

static func build_stage2_world(bundle: Dictionary) -> Stage1World:
	assert(verify_stage2_contract(bundle) == "")
	var world := _build_world_from_map(bundle, bundle.stage2_map, false)
	assert(world.enable_stage2(bundle.economy) == "")
	return world

static func schedule_stage2_command(world: Stage1World, command: Dictionary) -> String:
	var tick := int(command.get("executeTick", -1))
	var sequence := int(command.get("sequence", -1))
	match String(command.get("order", "")):
		"place-building":
			world.order_place_building(int(command.get("team", -1)), int(command.get("typeCode", -1)), _position(command.destination), tick, sequence)
		"train":
			world.order_train(int(command.get("buildingEntityId", 0)), int(command.get("typeCode", -1)), tick, sequence)
		"set-rally":
			world.order_set_rally(int(command.get("buildingEntityId", 0)), _position(command.destination), tick, sequence)
		"move", "attack-move", "attack-target", "stop":
			var horde_id := int(command.get("hordeEntityId", 0))
			if world.get_horde(horde_id) == null:
				return "stage2_horde_not_produced"
			var kind := Stage1Types.OrderKind.STOP
			match String(command.order):
				"move": kind = Stage1Types.OrderKind.MOVE
				"attack-move": kind = Stage1Types.OrderKind.ATTACK_MOVE
				"attack-target": kind = Stage1Types.OrderKind.ATTACK_TARGET
			world.schedule_command(horde_id, kind, _position(command.get("destination", {"xSubcells": 0, "ySubcells": 0})), int(command.get("targetEntityId", 0)), tick)
		_:
			return "stage2_command_kind"
	return ""

static func _build_world_from_map(bundle: Dictionary, map: Dictionary, schedule_initial_commands: bool) -> Stage1World:
	var grid_def: Dictionary = map.grid
	var world := Stage1World.new(int(grid_def.widthCells), int(grid_def.heightCells))
	world.setup_empty(false)
	for blocker: Dictionary in map.staticBlockers:
		for y in range(int(blocker.yCell), int(blocker.yCell) + int(blocker.heightCells)):
			for x in range(int(blocker.xCell), int(blocker.xCell) + int(blocker.widthCells)):
				world.grid.set_blocked(Vector2i(x, y))
	var sides: Array[Dictionary] = []
	for value in map.sides:
		sides.append(value)
	sides.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a.team) < int(b.team))
	for side in sides:
		var fortress_position := _position(side.fortress.position)
		var fortress := world.add_fortress(int(side.team), fortress_position, _object_health(bundle, String(side.fortress.objectId)))
		assert(fortress.id == int(side.fortress.entityId))
	for side in sides:
		var member_count := 0
		var ranged_count := 0
		for composition: Dictionary in side.horde.composition:
			member_count += int(composition.count)
			if String(composition.objectId) == "test.object.member.ranged":
				ranged_count += int(composition.count)
		var ranged_every := member_count / ranged_count if ranged_count > 0 else 0
		var horde := world.add_horde(int(side.team), _position(side.horde.anchor), member_count, ranged_every)
		assert(horde.id == int(side.horde.entityId))
		assert(horde.members[0].id == int(side.horde.firstMemberEntityId))
	if not schedule_initial_commands:
		return world
	var commands: Array[Dictionary] = []
	for value in map.scenario.commands:
		commands.append(value)
	commands.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a.sequence) < int(b.sequence))
	for command in commands:
		var kind := Stage1Types.OrderKind.STOP
		match String(command.order):
			"move": kind = Stage1Types.OrderKind.MOVE
			"attack-move": kind = Stage1Types.OrderKind.ATTACK_MOVE
			"attack-target": kind = Stage1Types.OrderKind.ATTACK_TARGET
		world.schedule_command(
			int(command.hordeEntityId),
			kind,
			_position(command.destination),
			int(command.get("targetEntityId", 0)),
			int(command.executeTick)
		)
	return world

static func _read_json(path: String) -> Dictionary:
	if path == "" or not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	return parsed if parsed is Dictionary else {}

static func _position(value: Dictionary) -> Vector2i:
	return Vector2i(int(value.xSubcells), int(value.ySubcells))

static func _by_id(rows: Array) -> Dictionary:
	var result: Dictionary = {}
	for row: Dictionary in rows:
		result[String(row.get("id", ""))] = row
	return result

static func _object_health(bundle: Dictionary, object_id: String) -> int:
	var objects := _by_id(bundle.get("objects", {}).get("objects", []))
	return int(objects.get(object_id, {}).get("maximumHealth", 100))
