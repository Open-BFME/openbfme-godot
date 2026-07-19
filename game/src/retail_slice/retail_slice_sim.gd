class_name RetailSliceSim
extends RefCounted

const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

const MAX_RETAINED_EVENT_HISTORY := 2048
const MAX_RETAINED_EVENTS_PER_KIND := 32
const MAX_RETAINED_STRUCTURE_TARGETS_PER_KIND := 256
## Deterministic battalion-level gameplay used by the private retail slice.
## Positions are X/Z world coordinates stored as Vector2 values.

const TICK_SECONDS := 0.1
const PLAYER_TEAM := 0
const ENEMY_TEAM := 1
const MEMBER_ATTACK_STAGGER_WINDOW_TICKS := 4
const CORPSE_LIFETIME_TICKS := 600
const STANCE_ORDER: Array[String] = ["HoldGround", "Battle", "Aggressive"]
const STRUCTURE_KINDS: Array[String] = ["fortress", "farm", "barracks", "archery_range", "stable"]
const STRUCTURE_MAX_HEALTH: Dictionary = {
	"fortress": 7500,
	"farm": 2000,
	"barracks": 3000,
	"archery_range": 3000,
	"stable": 3000,
}
const STRUCTURE_BUILD_RULES: Dictionary = {
	"farm": {"cost": 300, "seconds": 22.0},
	"barracks": {"cost": 300, "seconds": 30.0},
	"archery_range": {"cost": 300, "seconds": 30.0},
	"stable": {"cost": 600, "seconds": 45.0},
	"fortress": {"cost": 5000, "seconds": 120.0},
}
const PLAYER_STRUCTURE_BASE := 1000
const ENEMY_STRUCTURE_BASE := 2000
const SOLDIER_OBJECT_ID := "bfme2.object.gondor-fighter"
const ARCHER_OBJECT_ID := "bfme2.object.gondor-archer"
const TOWER_GUARD_OBJECT_ID := "bfme2.object.gondor-tower-guard"
const KNIGHT_OBJECT_ID := "bfme2.object.gondor-knight"
const RANGER_OBJECT_ID := "bfme2.object.gondor-ranger"
const RANGER_HORDE_ID := "bfme2.object.gondor-ranger-horde"
const TREBUCHET_OBJECT_ID := "bfme2.object.gondor-trebuchet"
const BUILDER_OBJECT_ID := "bfme2.object.men-porter"
const SOLDIER_HORDE_ID := "bfme2.object.gondor-fighter-horde"
const PRODUCTION_DOOR_INSET_RADIUS := 0.9
const PRODUCTION_EXIT_RADIUS := 4.25
const PRODUCTION_EXIT_DURATION_TICKS := 18
const UNIT_DAMAGE_TYPES: Dictionary = {
	SOLDIER_OBJECT_ID: "slash",
	TOWER_GUARD_OBJECT_ID: "specialist",
	ARCHER_OBJECT_ID: "pierce",
	RANGER_OBJECT_ID: "pierce",
	KNIGHT_OBJECT_ID: "cavalry",
	TREBUCHET_OBJECT_ID: "siege",
}
# BFME2 1.06 FortressArmor keeps the source 7,500 hit points but applies
# damage-type resistance. The old slice skipped this layer, making infantry
# appear to erase a fortress despite its correct displayed maximum health.
const FORTRESS_ARMOR_SCALARS: Dictionary = {
	"slash": 0.20,
	"specialist": 0.12,
	"pierce": 0.01,
	"cavalry": 0.20,
}
const UNIT_PRODUCTION_RULES: Dictionary = {
	SOLDIER_HORDE_ID: {
		"producer_kind": "barracks",
		"object_id": SOLDIER_OBJECT_ID,
		"display_name": "Gondor Soldiers",
		"cost_rule": "soldier_cost",
		"build_ticks_rule": "soldier_build_ticks",
		"command_points_rule": "soldier_command_points",
		"default_cost": 200,
		"default_build_ticks": 200,
		"default_command_points": 60,
	},
	TOWER_GUARD_OBJECT_ID: {
		"producer_kind": "barracks",
		"object_id": TOWER_GUARD_OBJECT_ID,
		"display_name": "Tower Guard",
		"cost_rule": "tower_guard_cost",
		"build_ticks_rule": "tower_guard_build_ticks",
		"command_points_rule": "tower_guard_command_points",
		"default_cost": 400,
		"default_build_ticks": 200,
		"default_command_points": 60,
	},
	ARCHER_OBJECT_ID: {
		"producer_kind": "archery_range",
		"object_id": ARCHER_OBJECT_ID,
		"display_name": "Gondor Archers",
		"cost_rule": "archer_cost",
		"build_ticks_rule": "archer_build_ticks",
		"command_points_rule": "archer_command_points",
		"default_cost": 200,
		"default_build_ticks": 200,
		"default_command_points": 60,
	},
	KNIGHT_OBJECT_ID: {
		"producer_kind": "stable",
		"object_id": KNIGHT_OBJECT_ID,
		"display_name": "Gondor Knights",
		"cost_rule": "knight_cost",
		"build_ticks_rule": "knight_build_ticks",
		"command_points_rule": "knight_command_points",
		"default_cost": 550,
		"default_build_ticks": 250,
		"default_command_points": 80,
	},
}
const AI_PRODUCTION_PLAN: Array[String] = [
	SOLDIER_HORDE_ID,
	TOWER_GUARD_OBJECT_ID,
	ARCHER_OBJECT_ID,
	KNIGHT_OBJECT_ID,
]
# Default faction roster: entries flagged requires_unit_rule spawn only when the
# selected pack's unit rules define that object (the two Builder battalions).
const DEFAULT_SPAWN_ROSTER: Array = [
	{"id": 1, "team": PLAYER_TEAM, "anchor": "player_spawn_primary", "name": "Gondor Soldiers", "object_id": SOLDIER_OBJECT_ID, "unit_type": SOLDIER_HORDE_ID},
	{"id": 2, "team": PLAYER_TEAM, "anchor": "player_spawn_secondary", "name": "Gondor Archers", "object_id": ARCHER_OBJECT_ID, "unit_type": ARCHER_OBJECT_ID},
	{"id": 101, "team": ENEMY_TEAM, "anchor": "enemy_spawn_primary", "name": "Enemy Soldiers", "object_id": SOLDIER_OBJECT_ID, "unit_type": SOLDIER_HORDE_ID},
	{"id": 102, "team": ENEMY_TEAM, "anchor": "enemy_spawn_secondary", "name": "Enemy Tower Guard", "object_id": TOWER_GUARD_OBJECT_ID, "unit_type": TOWER_GUARD_OBJECT_ID},
	{"id": 103, "team": ENEMY_TEAM, "anchor": "enemy_reserve", "name": "Enemy Gondor Knights", "object_id": KNIGHT_OBJECT_ID, "unit_type": KNIGHT_OBJECT_ID},
	{"id": 3, "team": PLAYER_TEAM, "anchor": "player_builder", "name": "Builder", "object_id": BUILDER_OBJECT_ID, "unit_type": BUILDER_OBJECT_ID, "command_points": 0, "requires_unit_rule": true},
	{"id": 104, "team": ENEMY_TEAM, "anchor": "enemy_builder", "name": "Enemy Builder", "object_id": BUILDER_OBJECT_ID, "unit_type": BUILDER_OBJECT_ID, "command_points": 0, "requires_unit_rule": true},
]


func initial_battalion_count() -> int:
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	var count := 0
	for entry_value in _active_spawn_roster():
		var entry := entry_value as Dictionary
		if bool(entry.get("requires_unit_rule", false)) and not configured_unit_rules.has(String(entry.get("object_id", ""))):
			continue
		count += 1
	return count


func _active_spawn_roster() -> Array:
	return _spawn_roster if not _spawn_roster.is_empty() else DEFAULT_SPAWN_ROSTER

var tick_index := 0
var winner := -1
var ai_enabled := true
var selected_ids: Array[int] = []
var control_groups: Dictionary = {}
var events: Array[Dictionary] = []
var _event_digest := 0x811C9DC5
var entities: Dictionary = {}
var structures: Dictionary = {}
var team_resources: Dictionary = {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
var team_command_points: Dictionary = {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
var command_point_cap := 200
var base_loop_enabled := false
var source_map_configured := false
var ford_gates: Array[Dictionary] = []
var source_player_starts: Dictionary = {}
var route_provider: RefCounted
var playable_outline := PackedVector2Array()
var last_route_rejection := ""
var _spawn_positions: Dictionary = {}
var _home_layout: Dictionary = {}
var _rules: Dictionary = {}
var configuration_error := ""
var _unit_production_rules: Dictionary = {}
var _completed_hero_identities: Dictionary = {}
var _production_unit_order: Array[String] = []
var _ai_production_plan: Array[String] = []
var _unit_damage_types: Dictionary = {}
var _spawn_roster: Array = []
var _structure_kinds: Array[String] = []
var _structure_max_health: Dictionary = {}
var _structure_build_rules: Dictionary = {}
var _unit_prerequisites: Dictionary = {}
var _structure_upgrade_contracts: Dictionary = {}
var _next_dynamic_id: Dictionary = {PLAYER_TEAM: 10, ENEMY_TEAM: 110}
var _next_dynamic_structure_id := 3000
var _next_event_sequence := 1
var _next_order_sequence := 1
var _music_state := ""
var _enemy_ai_construction_attempted := false
var _enemy_ai_construction_resolved := false


func setup(map_configuration: Dictionary = {}, gameplay_rules: Dictionary = {}) -> void:
	if not map_configuration.is_empty():
		_apply_map_configuration(map_configuration)
	elif _spawn_positions.is_empty():
		_apply_fallback_configuration()
	tick_index = 0
	winner = -1
	ai_enabled = true
	selected_ids.clear()
	reset_control_groups()
	events.clear()
	_event_digest = 0x811C9DC5
	entities.clear()
	structures.clear()
	_completed_hero_identities.clear()
	_next_event_sequence = 1
	_next_order_sequence = 1
	_music_state = ""
	_enemy_ai_construction_attempted = false
	_enemy_ai_construction_resolved = false
	last_route_rejection = ""
	team_power_points = {PLAYER_TEAM: 1, ENEMY_TEAM: 1}
	purchased_powers = {PLAYER_TEAM: [], ENEMY_TEAM: []}
	_kills_toward_power_point = {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
	_apply_gameplay_rules(gameplay_rules if not gameplay_rules.is_empty() else _rules)
	_next_dynamic_id = {PLAYER_TEAM: 10, ENEMY_TEAM: 110}
	_next_dynamic_structure_id = 3000
	if bool(_rules.get("spawn_initial_battalions", true)):
		var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
		for entry_value in _active_spawn_roster():
			var entry := entry_value as Dictionary
			var object_id := String(entry.get("object_id", ""))
			if bool(entry.get("requires_unit_rule", false)) and not configured_unit_rules.has(object_id):
				continue
			_add_battalion(
				int(entry.get("id", 0)),
				int(entry.get("team", PLAYER_TEAM)),
				_spawn_anchor_position(String(entry.get("anchor", ""))),
				String(entry.get("name", "")),
				object_id,
				String(entry.get("unit_type", object_id)),
				int(entry.get("command_points", -1))
			)
	if base_loop_enabled:
		_initialize_base_loop()
	_emit_music("explore")


func _apply_map_configuration(configuration: Dictionary) -> void:
	var configured_spawns: Variant = configuration.get("spawn_positions", {})
	var configured_gates: Variant = configuration.get("ford_gates", [])
	var configured_starts: Variant = configuration.get("player_starts", {})
	var configured_provider: Variant = configuration.get("route_provider")
	var configured_outline: Variant = configuration.get("playable_outline", PackedVector2Array())
	if typeof(configured_spawns) != TYPE_DICTIONARY or typeof(configured_gates) != TYPE_ARRAY or typeof(configured_starts) != TYPE_DICTIONARY or typeof(configured_provider) != TYPE_OBJECT or typeof(configured_outline) != TYPE_PACKED_VECTOR2_ARRAY:
		_apply_fallback_configuration()
		return
	if configured_provider == null or not configured_provider.has_method("query_route"):
		_apply_fallback_configuration()
		return
	var candidate_spawns: Dictionary = configured_spawns
	for id in [1, 2, 101, 102]:
		if not candidate_spawns.has(id) or typeof(candidate_spawns[id]) != TYPE_VECTOR2:
			_apply_fallback_configuration()
			return
	var candidate_gates: Array = configured_gates
	if candidate_gates.size() != 3:
		_apply_fallback_configuration()
		return
	var validated_gates: Array[Dictionary] = []
	for value in candidate_gates:
		if typeof(value) != TYPE_DICTIONARY:
			_apply_fallback_configuration()
			return
		var gate: Dictionary = value
		if typeof(gate.get("edge_a")) != TYPE_VECTOR2 or typeof(gate.get("edge_b")) != TYPE_VECTOR2 or typeof(gate.get("center")) != TYPE_VECTOR2:
			_apply_fallback_configuration()
			return
		validated_gates.append(gate.duplicate(true))
	_spawn_positions = candidate_spawns.duplicate(true)
	ford_gates = validated_gates
	source_player_starts = (configured_starts as Dictionary).duplicate(true)
	route_provider = configured_provider as RefCounted
	playable_outline = (configured_outline as PackedVector2Array).duplicate()
	var configured_home_layout: Variant = configuration.get("home_layout", {})
	_home_layout = (configured_home_layout as Dictionary).duplicate(true) if typeof(configured_home_layout) == TYPE_DICTIONARY else {}
	source_map_configured = bool(configuration.get("source_map_configured", false))


func _apply_fallback_configuration() -> void:
	_spawn_positions = {
		1: Vector2(-38.0, -14.0),
		2: Vector2(-38.0, 14.0),
		101: Vector2(38.0, -14.0),
		102: Vector2(38.0, 14.0),
	}
	ford_gates = [
		{"name": "fallback-south", "edge_a": Vector2(-11.0, -23.0), "edge_b": Vector2(11.0, -23.0), "center": Vector2(0.0, -23.0)},
		{"name": "fallback-center", "edge_a": Vector2(-11.0, 0.0), "edge_b": Vector2(11.0, 0.0), "center": Vector2.ZERO},
		{"name": "fallback-north", "edge_a": Vector2(-11.0, 23.0), "edge_b": Vector2(11.0, 23.0), "center": Vector2(0.0, 23.0)},
	]
	source_player_starts.clear()
	route_provider = null
	playable_outline = PackedVector2Array()
	_home_layout.clear()
	source_map_configured = false


func _apply_gameplay_rules(gameplay_rules: Dictionary) -> void:
	_rules = gameplay_rules.duplicate(true)
	configuration_error = ""
	if not _configure_faction_manifest():
		return
	_unit_prerequisites.clear()
	_structure_upgrade_contracts.clear()
	_configure_ranger_runtime_contract()
	_configure_trebuchet_runtime_contract()
	_configure_playable_unit_runtime_contracts()
	_configure_manifest_builders()
	_validate_faction_manifest_coherence()
	base_loop_enabled = bool(_rules.get("enable_base_loop", false))
	command_point_cap = maxi(60, int(_rules.get("command_point_cap", 200)))
	team_resources = {
		PLAYER_TEAM: maxi(0, int(_rules.get("starting_resources", 1200 if base_loop_enabled else 0))),
		ENEMY_TEAM: maxi(0, int(_rules.get("starting_resources", 1200 if base_loop_enabled else 0))),
	}
	team_command_points = {PLAYER_TEAM: 120, ENEMY_TEAM: 120}


func _configure_faction_manifest() -> bool:
	## Every faction-scoped table flows through the manifest. Absent keys
	## default to exactly the historical Gondor constants, so a rules
	## dictionary without a manifest stays byte-identical to the old behavior.
	var manifest_value: Variant = _rules.get("faction_manifest", {})
	if typeof(manifest_value) != TYPE_DICTIONARY:
		configuration_error = "Faction manifest is not a dictionary"
		return false
	var manifest := manifest_value as Dictionary
	if manifest.has("_error"):
		configuration_error = "Faction manifest is invalid: %s" % String(manifest.get("_error", ""))
		return false
	var typed_expectations := {
		"unit_production_rules": TYPE_DICTIONARY,
		"ai_production_plan": TYPE_ARRAY,
		"structure_kinds": TYPE_ARRAY,
		"structure_max_health": TYPE_DICTIONARY,
		"structure_build_rules": TYPE_DICTIONARY,
		"unit_damage_types": TYPE_DICTIONARY,
		"spawn_roster": TYPE_ARRAY,
		"builder_unit_ids": TYPE_ARRAY,
	}
	for key in typed_expectations:
		if manifest.has(key) and typeof(manifest.get(key)) != int(typed_expectations[key]):
			configuration_error = "Faction manifest field '%s' has the wrong type" % String(key)
			return false
	_unit_production_rules = (manifest.get("unit_production_rules", UNIT_PRODUCTION_RULES) as Dictionary).duplicate(true)
	var plan: Array = Array(manifest.get("ai_production_plan", AI_PRODUCTION_PLAN))
	var kinds: Array = Array(manifest.get("structure_kinds", STRUCTURE_KINDS))
	for table in [plan, kinds]:
		for value in table as Array:
			if typeof(value) != TYPE_STRING or String(value).strip_edges() == "":
				configuration_error = "Faction manifest plan or structure kinds contain a non-string entry"
				return false
	_production_unit_order.assign(plan)
	_ai_production_plan.assign(plan)
	_structure_kinds.assign(kinds)
	_structure_max_health = (manifest.get("structure_max_health", STRUCTURE_MAX_HEALTH) as Dictionary).duplicate(true)
	_structure_build_rules = (manifest.get("structure_build_rules", STRUCTURE_BUILD_RULES) as Dictionary).duplicate(true)
	_unit_damage_types = (manifest.get("unit_damage_types", UNIT_DAMAGE_TYPES) as Dictionary).duplicate(true)
	_spawn_roster = (manifest.get("spawn_roster", DEFAULT_SPAWN_ROSTER) as Array).duplicate(true)
	for kind_value in _structure_kinds:
		var kind := String(kind_value)
		if int(_structure_max_health.get(kind, 0)) <= 0:
			configuration_error = "Faction manifest structure kind '%s' has no positive maximum health" % kind
			return false
		var build_rule: Dictionary = _structure_build_rules.get(kind, {}) as Dictionary
		if int(build_rule.get("cost", -1)) < 0 or float(build_rule.get("seconds", 0.0)) <= 0.0:
			configuration_error = "Faction manifest structure kind '%s' has no valid build rule" % kind
			return false
	return true


func _configure_manifest_builders() -> void:
	## A data-driven faction names its builder via the structure documents'
	## authored construct routes; the flag rides the normalized unit rule.
	if configuration_error != "":
		return
	var manifest: Dictionary = _rules.get("faction_manifest", {}) as Dictionary
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_id := String(builder_value)
		if not configured_unit_rules.has(builder_id):
			configuration_error = "Faction manifest builder unit '%s' has no configured unit rule" % builder_id
			return
		var builder_rule: Dictionary = configured_unit_rules[builder_id] as Dictionary
		builder_rule["is_builder"] = true
		configured_unit_rules[builder_id] = builder_rule
	_rules["unit_rules"] = configured_unit_rules


func _validate_faction_manifest_coherence() -> void:
	## Only an explicitly supplied manifest is validated end-to-end; legacy
	## rule dictionaries keep their historical permissiveness.
	if configuration_error != "" or not _rules.has("faction_manifest"):
		return
	var manifest: Dictionary = _rules.get("faction_manifest", {}) as Dictionary
	if manifest.is_empty():
		return
	for unit_type_value in _ai_production_plan:
		var unit_type := String(unit_type_value)
		if not _unit_production_rules.has(unit_type):
			configuration_error = "Faction manifest AI plan entry '%s' has no production rule" % unit_type
			return
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	for entry_value in _spawn_roster:
		if typeof(entry_value) != TYPE_DICTIONARY:
			configuration_error = "Faction manifest spawn roster contains a non-object entry"
			return
		var entry := entry_value as Dictionary
		var object_id := String(entry.get("object_id", ""))
		if object_id == "" or int(entry.get("id", 0)) <= 0:
			configuration_error = "Faction manifest spawn roster entry is missing its identity"
			return
		if not bool(entry.get("requires_unit_rule", false)) and not configured_unit_rules.has(object_id):
			configuration_error = "Faction manifest spawn roster entry '%s' has no configured unit rule" % object_id
			return


func _configure_playable_unit_runtime_contracts() -> void:
	var value: Variant = _rules.get("playable_unit_runtimes", {})
	if typeof(value) != TYPE_DICTIONARY:
		configuration_error = "Playable-unit runtime registry is not a dictionary"
		return
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	var producer_kinds: Dictionary = _rules.get("producer_kind_by_source_object", {}) as Dictionary
	var object_ids: Array[String] = []
	for object_id_value in (value as Dictionary).keys():
		object_ids.append(String(object_id_value))
	object_ids.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	for object_id in object_ids:
		var document_value: Variant = (value as Dictionary).get(object_id)
		if typeof(document_value) != TYPE_DICTIONARY:
			configuration_error = "Playable-unit runtime '%s' is invalid" % object_id
			return
		var simulation := PlayableUnitAdapter.simulation_rule(document_value as Dictionary)
		if simulation.is_empty():
			configuration_error = "Playable-unit runtime '%s' has unresolved simulation evidence" % object_id
			return
		var unit_type := String(simulation["unit_type"])
		var member_id := String(simulation["object_id"])
		if (
			_unit_production_rules.has(unit_type)
			and String((_unit_production_rules[unit_type] as Dictionary).get("object_id", "")) != member_id
		):
			configuration_error = "Playable-unit runtime '%s' collides with production '%s'" % [object_id, unit_type]
			return
		var unit_rule := PlayableUnitAdapter.normalized_unit_rule(simulation, float(_rules.get("source_map_transform_scale", 0.0)))
		if unit_rule.is_empty():
			unit_rule = (configured_unit_rules.get(member_id, {}) as Dictionary).duplicate(true)
		if unit_rule.is_empty():
			configuration_error = "Playable-unit runtime '%s' has no normalized unit rule" % object_id
			return
		var producers: Array = simulation.get("producers", [])
		if producers.is_empty():
			configuration_error = "Playable-unit runtime '%s' has no producer" % object_id
			return
		var resolved_producers: Array[Dictionary] = []
		var resolved_producer_kinds: Array[String] = []
		for producer_value in producers:
			var producer := producer_value as Dictionary
			var source_producer := String(producer.get("producer_source_object_id", ""))
			var producer_kind := String(producer_kinds.get(source_producer, ""))
			if producer_kind == "":
				configuration_error = "Playable-unit runtime '%s' producer '%s' is not loaded by this faction slice" % [object_id, source_producer]
				return
			var route := producer.duplicate(true)
			route["producer_kind"] = producer_kind
			resolved_producers.append(route)
			if not resolved_producer_kinds.has(producer_kind):
				resolved_producer_kinds.append(producer_kind)
		var primary_producer := resolved_producers[0]
		configured_unit_rules[member_id] = unit_rule
		_unit_production_rules[unit_type] = {
			"category": String(simulation.get("category", "")),
			"producer_kind": String(primary_producer["producer_kind"]),
			"producer_kinds": resolved_producer_kinds,
			"producer_routes": resolved_producers,
			"producer_source_object_id": String(primary_producer["producer_source_object_id"]),
			"object_id": member_id,
			"display_name": String(simulation["display_name"]),
			"default_cost": int(simulation["default_cost"]),
			"default_build_ticks": int(simulation["default_build_ticks"]),
			"default_command_points": int(simulation["default_command_points"]),
			"command_id": String(primary_producer.get("command_id", "")),
			"command_slot": int(primary_producer.get("slot", 0)),
		}
		if not _production_unit_order.has(unit_type):
			_production_unit_order.append(unit_type)
		var prerequisites_by_producer: Dictionary = {}
		for route in resolved_producers:
			var producer_kind := String(route["producer_kind"])
			var candidate: Array = (route.get("prerequisites", []) as Array).duplicate()
			if not prerequisites_by_producer.has(producer_kind):
				prerequisites_by_producer[producer_kind] = candidate
				continue
			var existing: Array = prerequisites_by_producer[producer_kind]
			var candidate_is_subset := candidate.all(func(value: Variant) -> bool: return existing.has(value))
			var existing_is_subset := existing.all(func(value: Variant) -> bool: return candidate.has(value))
			if candidate_is_subset:
				prerequisites_by_producer[producer_kind] = candidate
			elif not existing_is_subset:
				configuration_error = "Playable-unit runtime '%s' has ambiguous prerequisite variants for '%s'" % [object_id, producer_kind]
				return
		_unit_prerequisites[unit_type] = prerequisites_by_producer
	_rules["unit_rules"] = configured_unit_rules


func _configure_ranger_runtime_contract() -> void:
	var value: Variant = _rules.get("ranger_runtime", {})
	if typeof(value) != TYPE_DICTIONARY:
		configuration_error = "Ranger runtime contract is not a dictionary"
		return
	var contract := value as Dictionary
	if contract.is_empty():
		return
	if (
		String(contract.get("schema", "")) != "openbfme.ranger-runtime-contract"
		or int(contract.get("schemaVersion", -1)) != 0
		or String(contract.get("capabilityStatus", "")) != "rules-and-prerequisite-ready"
	):
		configuration_error = "Ranger runtime contract identity is invalid"
		return
	var production_value: Variant = contract.get("production")
	var prerequisite_value: Variant = contract.get("prerequisite")
	var unit_rule_value: Variant = _rules.get("ranger_unit_rule")
	if typeof(production_value) != TYPE_DICTIONARY or typeof(prerequisite_value) != TYPE_DICTIONARY or typeof(unit_rule_value) != TYPE_DICTIONARY:
		configuration_error = "Ranger runtime contract is missing production, prerequisite, or normalized unit rule"
		return
	var production := production_value as Dictionary
	var prerequisite := prerequisite_value as Dictionary
	var unit_rule := unit_rule_value as Dictionary
	var command_sets_value: Variant = contract.get("commandSets")
	var train_options_value: Variant = prerequisite.get("trainCommandOptions")
	var upgrade_options_value: Variant = prerequisite.get("options")
	if typeof(command_sets_value) != TYPE_ARRAY or typeof(train_options_value) != TYPE_ARRAY or typeof(upgrade_options_value) != TYPE_ARRAY:
		configuration_error = "Ranger runtime contract options are invalid"
		return
	if not _ranger_command_sets_are_valid(command_sets_value as Array):
		configuration_error = "Ranger runtime command-set transition is invalid"
		return
	var required_unit_rule_fields: Array[String] = [
		"horde_id", "member_count", "member_health", "member_damage",
		"speed", "speed_source", "acceleration", "acceleration_source",
		"turn_rate_degrees_per_second", "braking", "braking_source",
		"attack_range", "attack_range_source", "minimum_attack_range",
		"minimum_attack_range_source", "vision_range", "vision_range_source",
		"delay_between_shots_ms", "pre_attack_delay_ms", "firing_duration_ms",
		"attack_period_ticks", "pre_attack_ticks", "firing_duration_ticks",
		"clip_size", "clip_reload_time_ms", "continuous_fire_one",
		"continuous_fire_coast_ticks", "continuous_fire_rate_multiplier",
		"formation_positions", "provenance",
	]
	for field in required_unit_rule_fields:
		if not unit_rule.has(field):
			configuration_error = "Ranger normalized unit rule is missing %s" % field
			return
	var upgrade_id := String(prerequisite.get("upgradeId", ""))
	var upgrade_cost := int(prerequisite.get("cost", -1))
	var upgrade_ticks := roundi(float(prerequisite.get("buildTimeSeconds", -1.0)) / TICK_SECONDS)
	var ranger_cost := int(production.get("buildCost", -1))
	var ranger_ticks := roundi(float(production.get("buildTime", -1.0)) / TICK_SECONDS)
	var ranger_command_points := int(production.get("commandPoints", -1))
	if (
		String(production.get("id", "")) != "GondorRangerHorde"
		or String(prerequisite.get("trainCommandId", "")) != "Command_ConstructGondorRangerHorde"
		or not (train_options_value as Array).has("NEED_UPGRADE")
		or upgrade_id != "Upgrade_GondorArcheryRangeLevel2"
		or String(prerequisite.get("type", "")) != "OBJECT"
		or not (upgrade_options_value as Array).has("CANCELABLE")
		or int(prerequisite.get("levelsToGain", 0)) != 1
		or int(prerequisite.get("levelCap", 0)) != 3
		or String(prerequisite.get("fromCommandSet", "")) != "GondorArcheryCommandSet"
		or String(prerequisite.get("toCommandSet", "")) != "GondorArcheryCommandSetLevel2"
		or upgrade_cost < 0
		or upgrade_ticks <= 0
		or ranger_cost < 0
		or ranger_ticks <= 0
		or ranger_command_points <= 0
		or String(unit_rule.get("horde_id", "")) != RANGER_HORDE_ID
		or int(unit_rule.get("member_count", 0)) != 10
	):
		configuration_error = "Ranger runtime contract values are invalid"
		return
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	configured_unit_rules[RANGER_OBJECT_ID] = unit_rule.duplicate(true)
	_rules["unit_rules"] = configured_unit_rules
	_unit_production_rules[RANGER_HORDE_ID] = {
		"producer_kind": "archery_range",
		"object_id": RANGER_OBJECT_ID,
		"display_name": "Ithilien Rangers",
		"default_cost": ranger_cost,
		"default_build_ticks": ranger_ticks,
		"default_command_points": ranger_command_points,
	}
	_production_unit_order.append(RANGER_HORDE_ID)
	_unit_prerequisites[RANGER_HORDE_ID] = upgrade_id
	_structure_upgrade_contracts[upgrade_id] = {
		"structure_kind": "archery_range",
		"cost": upgrade_cost,
		"duration_ticks": upgrade_ticks,
		"levels_to_gain": 1,
		"level_cap": 3,
		"cancelable": true,
		"from_command_set": String(prerequisite.get("fromCommandSet", "")),
		"to_command_set": String(prerequisite.get("toCommandSet", "")),
	}


func _configure_trebuchet_runtime_contract() -> void:
	var value: Variant = _rules.get("trebuchet_runtime", {})
	if typeof(value) != TYPE_DICTIONARY:
		configuration_error = "Trebuchet runtime contract is not a dictionary"
		return
	var contract := value as Dictionary
	if contract.is_empty():
		return
	if (
		String(contract.get("schema", "")) != "openbfme.trebuchet-runtime-contract"
		or int(contract.get("schemaVersion", -1)) != 0
		or String(contract.get("capabilityStatus", "")) != "bounded-direct-structure-ready"
	):
		configuration_error = "Trebuchet runtime contract identity is invalid"
		return
	var production: Dictionary = contract.get("production", {}) as Dictionary
	var unit: Dictionary = contract.get("unit", {}) as Dictionary
	var movement: Dictionary = unit.get("movement", {}) as Dictionary
	var combat: Dictionary = contract.get("combat", {}) as Dictionary
	var workshop: Dictionary = contract.get("workshop", {}) as Dictionary
	var workshop_stats: Dictionary = workshop.get("stats", {}) as Dictionary
	var scale := float(_rules.get("source_map_transform_scale", 0.0))
	if (
		scale <= 0.0
		or String(production.get("id", "")) != "GondorTrebuchet"
		or String(unit.get("objectId", "")) != "GondorTrebuchet"
		or String(workshop_stats.get("id", "")) != "GondorWorkshop"
		or String(workshop.get("trainCommandId", "")) != "Command_ConstructGondorTrebuchet"
		or String(movement.get("mode", "")) != "existing-generic-unit-path"
		or String(combat.get("scope", "")) != "direct-structure-first-slice"
		or String(combat.get("damageType", "")).to_lower() != "siege"
	):
		configuration_error = "Trebuchet runtime contract values are invalid"
		return
	var speed_source := float(movement.get("speed", 0.0))
	var attack_range_source := float(combat.get("attackRange", 0.0))
	var minimum_range_source := float(combat.get("minimumAttackRange", 0.0))
	var vision_source := float(unit.get("visionRange", 0.0))
	var delay_ms := float(combat.get("delayBetweenShotsMs", 0.0))
	var pre_attack_ms := float(combat.get("preAttackDelayMs", 0.0))
	var firing_ms := float(combat.get("firingDurationMs", 0.0))
	var health := int(unit.get("maximumHealth", 0))
	var damage := int(combat.get("damage", 0))
	var build_cost := int(production.get("buildCost", -1))
	var build_ticks := roundi(float(production.get("buildTime", -1.0)) / TICK_SECONDS)
	var command_points := int(production.get("commandPoints", -1))
	var workshop_cost := int(workshop_stats.get("buildCost", -1))
	var workshop_ticks := roundi(float(workshop_stats.get("buildTime", -1.0)) / TICK_SECONDS)
	var workshop_health := int(workshop_stats.get("maxHealth", 0))
	if (
		speed_source <= 0.0
		or attack_range_source <= 0.0
		or minimum_range_source <= 0.0
		or vision_source <= 0.0
		or attack_range_source <= minimum_range_source
		or health <= 0
		or damage <= 0
		or build_cost <= 0
		or build_ticks <= 0
		or command_points <= 0
		or workshop_cost <= 0
		or workshop_ticks <= 0
		or workshop_health <= 0
		or delay_ms <= 0.0
		or pre_attack_ms <= 0.0
		or firing_ms <= 0.0
	):
		configuration_error = "Trebuchet runtime numeric contract is invalid"
		return
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	configured_unit_rules[TREBUCHET_OBJECT_ID] = {
		"horde_id": TREBUCHET_OBJECT_ID,
		"member_count": 1,
		"member_health": health,
		"member_damage": damage,
		"speed": speed_source * scale,
		"speed_source": speed_source,
		"acceleration": speed_source * scale,
		"acceleration_source": speed_source,
		"turn_rate_degrees_per_second": 360.0,
		"braking": speed_source * scale,
		"braking_source": speed_source,
		"attack_range": attack_range_source * scale,
		"attack_range_source": attack_range_source,
		"minimum_attack_range": minimum_range_source * scale,
		"minimum_attack_range_source": minimum_range_source,
		"vision_range": vision_source * scale,
		"vision_range_source": vision_source,
		"delay_between_shots_ms": delay_ms,
		"pre_attack_delay_ms": pre_attack_ms,
		"firing_duration_ms": firing_ms,
		"attack_period_ticks": maxi(1, roundi(delay_ms / (TICK_SECONDS * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (TICK_SECONDS * 1000.0))),
		"firing_duration_ticks": maxi(0, roundi(firing_ms / (TICK_SECONDS * 1000.0))),
		"clip_size": int(combat.get("clipSize", 0)),
		"clip_reload_time_ms": 0.0,
		"continuous_fire_one": 0,
		"continuous_fire_coast_ticks": 0,
		"continuous_fire_rate_multiplier": 1.0,
		"formation_positions": [Vector3.ZERO],
		"provenance": {"contractSources": contract.get("sources", []).duplicate(true)},
	}
	_rules["unit_rules"] = configured_unit_rules
	_unit_production_rules[TREBUCHET_OBJECT_ID] = {
		"producer_kind": "workshop",
		"object_id": TREBUCHET_OBJECT_ID,
		"display_name": "Gondor Trebuchet",
		"default_cost": build_cost,
		"default_build_ticks": build_ticks,
		"default_command_points": command_points,
	}
	_production_unit_order.append(TREBUCHET_OBJECT_ID)
	_structure_max_health["workshop"] = workshop_health
	_structure_build_rules["workshop"] = {"cost": workshop_cost, "seconds": float(workshop_stats.get("buildTime", 0.0))}


func _ranger_command_sets_are_valid(command_sets: Array) -> bool:
	var expected := {
		"GondorArcheryCommandSet": "Command_PurchaseUpgradeGondorArcheryRangeLevel2",
		"GondorArcheryCommandSetLevel2": "Command_PurchaseUpgradeGondorArcheryRangeLevel3",
	}
	var matched: Dictionary = {}
	for command_set_value in command_sets:
		if typeof(command_set_value) != TYPE_DICTIONARY:
			return false
		var command_set := command_set_value as Dictionary
		var command_set_id := String(command_set.get("id", ""))
		if not expected.has(command_set_id):
			continue
		var commands_value: Variant = command_set.get("commands")
		if typeof(commands_value) != TYPE_ARRAY:
			return false
		var has_ranger := false
		var has_upgrade := false
		for command_value in commands_value as Array:
			if typeof(command_value) != TYPE_DICTIONARY:
				return false
			var command := command_value as Dictionary
			var command_id := String(command.get("id", ""))
			var slot := int(command.get("slot", 0))
			has_ranger = has_ranger or (command_id == "Command_ConstructGondorRangerHorde" and slot == 2)
			has_upgrade = has_upgrade or (command_id == String(expected[command_set_id]) and slot == 4)
		if not has_ranger or not has_upgrade:
			return false
		matched[command_set_id] = true
	return matched.size() == expected.size()


func _initialize_base_loop() -> void:
	structures.clear()
	var layout := _home_layout if not _home_layout.is_empty() else _derive_home_layout()
	for team in [PLAYER_TEAM, ENEMY_TEAM]:
		var team_layout: Dictionary = layout.get(team, layout.get(str(team), {}))
		var base_id := PLAYER_STRUCTURE_BASE if team == PLAYER_TEAM else ENEMY_STRUCTURE_BASE
		for index in range(_structure_kinds.size()):
			var kind := _structure_kinds[index]
			var position := Vector2(team_layout.get(kind, _fallback_structure_position(team, index)))
			var maximum_health := int(_structure_max_health[kind])
			var production: Array[String] = []
			for unit_type in _production_unit_order:
				var production_rule: Dictionary = _unit_production_rules[unit_type]
				var producer_kinds_for_rule: Array = production_rule.get("producer_kinds", [String(production_rule.get("producer_kind", ""))])
				if producer_kinds_for_rule.has(kind):
					production.append(unit_type)
			structures[base_id + index + 1] = {
				"id": base_id + index + 1,
				"team": team,
				"kind": "structure",
				"structure_kind": kind,
				"name": kind.replace("_", " ").capitalize(),
				"position": position,
				"rally": Vector2(team_layout.get("rally", _fallback_rally_position(team))),
				"health": maximum_health,
				"maximum_health": maximum_health,
				"construction_progress": 1.0,
				"level": 1,
				"completed_upgrades": [],
				"upgrade_queue": [],
				"production": production,
				"queue": [],
				"damage_remainders": {},
				"income_per_payout": int(_rules.get("farm_income", 25)) if kind == "farm" else 0,
			}


func _derive_home_layout() -> Dictionary:
	var player_center := (Vector2(_spawn_positions[1]) + Vector2(_spawn_positions[2])) * 0.5
	var enemy_center := (Vector2(_spawn_positions[101]) + Vector2(_spawn_positions[102])) * 0.5
	var map_center := (player_center + enemy_center) * 0.5
	var result: Dictionary = {}
	for team in [PLAYER_TEAM, ENEMY_TEAM]:
		var anchor := player_center if team == PLAYER_TEAM else enemy_center
		var outward := anchor.direction_to(map_center) * -1.0
		if outward.length_squared() < 0.01:
			outward = Vector2.LEFT if team == PLAYER_TEAM else Vector2.RIGHT
		var side := Vector2(-outward.y, outward.x)
		result[team] = {
			"fortress": anchor + outward * 10.0,
			"farm": anchor + side * 11.0 + outward * 2.0,
			"barracks": anchor - side * 11.0 + outward * 1.0,
			"archery_range": anchor + side * 18.0 - outward * 3.0,
			"stable": anchor - side * 18.0 - outward * 3.0,
			"rally": anchor - outward * 8.0,
		}
	return result


func _fallback_structure_position(team: int, index: int) -> Vector2:
	var anchor := (Vector2(_spawn_positions[1]) + Vector2(_spawn_positions[2])) * 0.5 if team == PLAYER_TEAM else (Vector2(_spawn_positions[101]) + Vector2(_spawn_positions[102])) * 0.5
	var sign_value := -1.0 if team == PLAYER_TEAM else 1.0
	return anchor + Vector2(sign_value * (8.0 + float(index) * 2.5), (float(index) - 2.0) * 7.0)


func _fallback_rally_position(team: int) -> Vector2:
	return (Vector2(_spawn_positions[1]) + Vector2(_spawn_positions[2])) * 0.5 if team == PLAYER_TEAM else (Vector2(_spawn_positions[101]) + Vector2(_spawn_positions[102])) * 0.5


func _builder_spawn_position(team: int) -> Vector2:
	var layout := _home_layout if not _home_layout.is_empty() else _derive_home_layout()
	var team_layout: Dictionary = layout.get(team, layout.get(str(team), {}))
	return Vector2(team_layout.get("rally", _fallback_rally_position(team)))


func _spawn_anchor_position(anchor: String) -> Vector2:
	## Named map anchors keep the faction roster data-driven while spawn
	## geometry stays derived from the cooked source map's player starts.
	match anchor:
		"player_spawn_primary":
			return Vector2(_spawn_positions[1])
		"player_spawn_secondary":
			return Vector2(_spawn_positions[2])
		"enemy_spawn_primary":
			return Vector2(_spawn_positions[101])
		"enemy_spawn_secondary":
			return Vector2(_spawn_positions[102])
		"enemy_reserve":
			return (Vector2(_spawn_positions[101]) + Vector2(_spawn_positions[102])) * 0.5
		"player_builder":
			return Vector2(_spawn_positions[1]) + Vector2(0.0, 4.0)
		"enemy_builder":
			return _builder_spawn_position(ENEMY_TEAM) if base_loop_enabled else Vector2(_spawn_positions[101]) + Vector2(0.0, -4.0)
	return Vector2(_spawn_positions[1])


func _add_battalion(
	id: int,
	team: int,
	at: Vector2,
	display_name: String,
	object_id: String = SOLDIER_OBJECT_ID,
	unit_type: String = SOLDIER_HORDE_ID,
	command_points: int = -1
) -> void:
	var unit_rules_value: Variant = _rules.get("unit_rules", {})
	var unit_rule: Dictionary = (
		(unit_rules_value as Dictionary).get(object_id, {}) as Dictionary
		if typeof(unit_rules_value) == TYPE_DICTIONARY
		else {}
	)
	if unit_rule.is_empty():
		push_error("RetailSliceSim missing selected-pack unit rule for %s" % object_id)
		return
	var member_health := maxi(1, int(unit_rule.get("member_health", _rules.get("member_health", 200))))
	var member_count := maxi(1, int(unit_rule.get("member_count", 0)))
	var maximum_health := member_health * member_count
	var member_health_values: Array[int] = []
	var member_attack_tokens: Array[int] = []
	var member_attack_start_ticks: Array[int] = []
	var member_attack_hit_ticks: Array[int] = []
	var member_attack_release_tokens: Array[int] = []
	var member_corpse_expire_ticks: Array[int] = []
	var member_target_indices: Array[int] = []
	var member_weapon_modes: Array[String] = []
	for _member_index in range(member_count):
		member_health_values.append(member_health)
		member_attack_tokens.append(0)
		member_attack_start_ticks.append(-1)
		member_attack_hit_ticks.append(-1)
		member_attack_release_tokens.append(0)
		member_corpse_expire_ticks.append(-1)
		member_target_indices.append(-1)
		member_weapon_modes.append(String(unit_rule.get("default_weapon_mode", "default")))
	var member_damage := maxi(1, int(unit_rule.get("member_damage", 0)))
	var fallback_weapon := {
		"name": "legacy-default",
		"attack_range": float(unit_rule["attack_range"]),
		"attack_range_source": float(unit_rule["attack_range_source"]),
		"minimum_attack_range": float(unit_rule["minimum_attack_range"]),
		"minimum_attack_range_source": float(unit_rule["minimum_attack_range_source"]),
		"delay_between_shots_ms": float(unit_rule["delay_between_shots_ms"]),
		"pre_attack_delay_ms": float(unit_rule["pre_attack_delay_ms"]),
		"firing_duration_ms": float(unit_rule["firing_duration_ms"]),
		"attack_period_ticks": maxi(1, int(unit_rule["attack_period_ticks"])),
		"pre_attack_ticks": maxi(0, int(unit_rule["pre_attack_ticks"])),
		"firing_duration_ticks": maxi(0, int(unit_rule["firing_duration_ticks"])),
		"member_damage": member_damage,
	}
	var weapon_modes: Dictionary = (unit_rule.get("weapon_modes", {}) as Dictionary).duplicate(true)
	if weapon_modes.is_empty():
		weapon_modes["default"] = fallback_weapon
	var battalion_damage := member_damage * member_count
	var committed_command_points := command_points
	if committed_command_points < 0:
		committed_command_points = _production_rule_value(unit_type, "command_points_rule", "default_command_points")
	entities[id] = {
		"id": id,
		"team": team,
		"name": display_name,
		"object_id": object_id,
		"position": at,
		"facing": Vector2.RIGHT if team == PLAYER_TEAM else Vector2.LEFT,
		"destination": at,
		"route": [],
		"route_cells": [],
		"route_ford": "",
		"order_sequence": 0,
		"state": "idle",
		"target_id": 0,
		"target_kind": "battalion",
		"health": maximum_health,
		"maximum_health": maximum_health,
		"member_maximum_health": member_health,
		"member_health": member_health_values,
		"damage": battalion_damage,
		"member_damage": member_damage,
		"speed": float(unit_rule["speed"]),
		"speed_source": float(unit_rule["speed_source"]),
		"acceleration": float(unit_rule["acceleration"]),
		"acceleration_source": float(unit_rule["acceleration_source"]),
		"turn_rate_degrees_per_second": float(unit_rule["turn_rate_degrees_per_second"]),
		"braking": float(unit_rule["braking"]),
		"braking_source": float(unit_rule["braking_source"]),
		"attack_range": float(unit_rule["attack_range"]),
		"attack_range_source": float(unit_rule["attack_range_source"]),
		"minimum_attack_range": float(unit_rule["minimum_attack_range"]),
		"minimum_attack_range_source": float(unit_rule["minimum_attack_range_source"]),
		"vision_range": float(unit_rule["vision_range"]),
		"vision_range_source": float(unit_rule["vision_range_source"]),
		"damage_type": String((_unit_damage_types if not _unit_damage_types.is_empty() else UNIT_DAMAGE_TYPES).get(object_id, "slash")),
		"delay_between_shots_ms": float(unit_rule["delay_between_shots_ms"]),
		"pre_attack_delay_ms": float(unit_rule["pre_attack_delay_ms"]),
		"firing_duration_ms": float(unit_rule["firing_duration_ms"]),
		"attack_period_ticks": maxi(1, int(unit_rule["attack_period_ticks"])),
		"pre_attack_ticks": maxi(0, int(unit_rule["pre_attack_ticks"])),
		"firing_duration_ticks": maxi(0, int(unit_rule["firing_duration_ticks"])),
		"attack_cooldown": 0,
		"attack_windup": 0,
		"attack_sequence": 0,
		"continuous_fire_count": 0,
		"continuous_fire_expiration_tick": -1,
		"member_attack_tokens": member_attack_tokens,
		"member_attack_start_ticks": member_attack_start_ticks,
		"member_attack_hit_ticks": member_attack_hit_ticks,
		"member_attack_release_tokens": member_attack_release_tokens,
		"member_corpse_expire_ticks": member_corpse_expire_ticks,
		"corpse_expire_tick": -1,
		"member_target_indices": member_target_indices,
		"member_weapon_modes": member_weapon_modes,
		"weapon_modes": weapon_modes,
		"default_weapon_mode": String(unit_rule.get("default_weapon_mode", "default")),
		"close_weapon_mode": String(unit_rule.get("close_weapon_mode", "")),
		"close_weapon_switch_distance": float(unit_rule.get("close_weapon_switch_distance", 0.0)),
		"close_weapon_switch_distance_source": float(unit_rule.get("close_weapon_switch_distance_source", 0.0)),
		"unsupported_close_weapon": bool(unit_rule.get("unsupported_close_weapon", false)),
		"clip_size": int(unit_rule.get("clip_size", 0)),
		"clip_reload_time_ms": float(unit_rule.get("clip_reload_time_ms", 0.0)),
		"continuous_fire_one": int(unit_rule.get("continuous_fire_one", 0)),
		"continuous_fire_coast_ticks": int(unit_rule.get("continuous_fire_coast_ticks", 0)),
		"continuous_fire_rate_multiplier": float(unit_rule.get("continuous_fire_rate_multiplier", 1.0)),
		"active_weapon_mode": String(unit_rule.get("default_weapon_mode", "default")),
		"stance": String((unit_rule.get("stances", {}) as Dictionary).get("default", "Battle")),
		"stance_contract": (unit_rule.get("stances", {}) as Dictionary).duplicate(true),
		"order_kind": "",
		"is_builder": bool(unit_rule.get("is_builder", false)),
		"construction_id": 0,
		"member_count": member_count,
		"horde_id": String(unit_rule["horde_id"]),
		"formation_positions": Array(unit_rule["formation_positions"]).duplicate(),
		"retail_rule_provenance": (unit_rule["provenance"] as Dictionary).duplicate(true),
		"unit_type": unit_type,
		"command_points": committed_command_points,
		"production_producer_id": 0,
		"production_exit_start_tick": -1,
		"production_exit_duration_ticks": 0,
		"production_exit_progress": 1.0,
		"production_exit_origin": at,
		"production_exit_destination": at,
		"production_rally": at,
	}


func entity(id: int) -> Dictionary:
	return entities.get(id, {})


func entity_ids() -> Array[int]:
	var result: Array[int] = []
	for value in entities.keys():
		result.append(int(value))
	result.sort()
	return result


func living_ids(team: int) -> Array[int]:
	var result: Array[int] = []
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row["team"]) == team and int(row["health"]) > 0:
			result.append(id)
	return result


func structure(id: int) -> Dictionary:
	return structures.get(id, {})


func structure_ids(team: int = -1) -> Array[int]:
	var result: Array[int] = []
	for value in structures.keys():
		var id := int(value)
		if team < 0 or int((structures[id] as Dictionary).get("team", -1)) == team:
			result.append(id)
	result.sort()
	return result


func living_structure_ids(team: int) -> Array[int]:
	var result: Array[int] = []
	for id in structure_ids(team):
		if int((structures[id] as Dictionary).get("health", 0)) > 0:
			result.append(id)
	return result


func fortress_id(team: int) -> int:
	for id in structure_ids(team):
		if String((structures[id] as Dictionary).get("structure_kind", "")) == "fortress":
			return id
	return 0


func producer_id(team: int, kind: String = "barracks") -> int:
	for id in structure_ids(team):
		var row: Dictionary = structures[id]
		if String(row.get("structure_kind", "")) == kind and int(row.get("health", 0)) > 0:
			return id
	return 0


func resources_for_team(team: int) -> int:
	return int(team_resources.get(team, 0))


func command_points_for_team(team: int) -> int:
	return int(team_command_points.get(team, 0))


func _production_rule_value(unit_type: String, rule_key: String, default_key: String) -> int:
	var production_rule: Dictionary = _unit_production_rules.get(unit_type, {})
	if production_rule.is_empty():
		return 0
	var gameplay_rule := String(production_rule.get(rule_key, ""))
	return int(_rules.get(gameplay_rule, int(production_rule.get(default_key, 0))))


func production_rule_ids() -> Array[String]:
	return _production_unit_order.duplicate()


func structure_build_rule_ids() -> Array[String]:
	var result: Array[String] = []
	for value in _structure_build_rules.keys():
		result.append(String(value))
	return result


func structure_build_rule(kind: String) -> Dictionary:
	return (_structure_build_rules.get(kind, {}) as Dictionary).duplicate(true)


func structure_maximum_health(kind: String) -> int:
	return int(_structure_max_health.get(kind, 0))


func required_upgrade_for_unit(unit_type: String, producer_kind: String = "") -> String:
	var required := required_upgrades_for_unit(unit_type, producer_kind)
	return String(required[0]) if not required.is_empty() else ""


func required_upgrades_for_unit(unit_type: String, producer_kind: String = "") -> Array:
	var value: Variant = _unit_prerequisites.get(unit_type, "")
	if typeof(value) == TYPE_DICTIONARY:
		return Array((value as Dictionary).get(producer_kind, [])).duplicate()
	return [String(value)] if String(value) != "" else []


func queue_structure_upgrade(team: int, structure_id: int, upgrade_id: String) -> Dictionary:
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not structures.has(structure_id):
		return {"ok": false, "reason": "unknown-structure"}
	var building: Dictionary = structures[structure_id]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(building.get("health", 0)) <= 0 or float(building.get("construction_progress", 0.0)) < 1.0:
		return {"ok": false, "reason": "structure-unavailable"}
	var contract: Dictionary = _structure_upgrade_contracts.get(upgrade_id, {})
	if contract.is_empty() or String(contract.get("structure_kind", "")) != String(building.get("structure_kind", "")):
		return {"ok": false, "reason": "unsupported-upgrade"}
	if Array(building.get("completed_upgrades", [])).has(upgrade_id):
		return {"ok": false, "reason": "already-completed"}
	var queue: Array = building.get("upgrade_queue", [])
	if not queue.is_empty():
		return {"ok": false, "reason": "upgrade-in-progress"}
	if int(building.get("level", 1)) >= int(contract.get("level_cap", 1)):
		return {"ok": false, "reason": "level-cap"}
	var cost := maxi(0, int(contract.get("cost", 0)))
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	var duration_ticks := maxi(1, int(contract.get("duration_ticks", 1)))
	var item := {
		"upgrade_id": upgrade_id,
		"cost": cost,
		"queued_tick": tick_index,
		"duration_ticks": duration_ticks,
		"complete_tick": tick_index + duration_ticks,
		"cancelable": bool(contract.get("cancelable", false)),
	}
	queue.append(item)
	building["upgrade_queue"] = queue
	team_resources[team] = resources_for_team(team) - cost
	_emit_event("upgrade.queued", structure_id, 0, {"team": team, "upgrade_id": upgrade_id, "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "structure_id": structure_id, "item": item.duplicate(true)}


func structure_upgrade_queue_state(structure_id: int) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	if not structures.has(structure_id):
		return result
	for item_value in Array((structures[structure_id] as Dictionary).get("upgrade_queue", [])):
		if typeof(item_value) != TYPE_DICTIONARY:
			continue
		var item := item_value as Dictionary
		var duration_ticks := maxi(1, int(item.get("duration_ticks", 1)))
		var elapsed_ticks := clampi(tick_index - int(item.get("queued_tick", tick_index)), 0, duration_ticks)
		var row := item.duplicate(true)
		row["elapsed_ticks"] = elapsed_ticks
		row["progress"] = float(elapsed_ticks) / float(duration_ticks)
		result.append(row)
	return result


func _queued_command_points_for_team(team: int) -> int:
	var total := 0
	for structure_id in structure_ids(team):
		for item_value in Array((structures[structure_id] as Dictionary).get("queue", [])):
			if typeof(item_value) == TYPE_DICTIONARY:
				total += int((item_value as Dictionary).get("command_points", 0))
	return total


## --- Powers (provisional economy; exact retail spellbook costs/tree are an
## --- M3 INI extraction item; magnitudes documented as provisional) ---
const POWER_POINT_KILLS := 3
const POWER_CAST_RADIUS := 15.0
const RALLY_DURATION_TICKS := 300
var team_power_points := {PLAYER_TEAM: 1, ENEMY_TEAM: 1}
var purchased_powers := {PLAYER_TEAM: [], ENEMY_TEAM: []}
var _kills_toward_power_point := {PLAYER_TEAM: 0, ENEMY_TEAM: 0}


func power_points(team: int) -> int:
	return int(team_power_points.get(team, 0))


func has_power(team: int, power_id: String) -> bool:
	return (purchased_powers.get(team, []) as Array).has(power_id)


func purchase_power(team: int, power_id: String, cost: int) -> Dictionary:
	if has_power(team, power_id):
		return {"ok": false, "reason": "already-purchased"}
	if power_points(team) < cost:
		return {"ok": false, "reason": "insufficient-power-points"}
	team_power_points[team] = power_points(team) - cost
	(purchased_powers[team] as Array).append(power_id)
	_emit_event("power.purchased", 0, 0, {"team": team, "power_id": power_id, "cost": cost})
	return {"ok": true, "reason": ""}


func award_power_kill(team: int) -> void:
	_kills_toward_power_point[team] = int(_kills_toward_power_point.get(team, 0)) + 1
	if int(_kills_toward_power_point[team]) >= POWER_POINT_KILLS:
		_kills_toward_power_point[team] = 0
		team_power_points[team] = power_points(team) + 1
		_emit_event("power.point_earned", 0, 0, {"team": team, "points": power_points(team)})


func cast_heal(team: int, point: Vector2) -> Dictionary:
	if not has_power(team, "SBGood_Heal"):
		return {"ok": false, "reason": "power-not-purchased"}
	var healed := 0
	for id in living_ids(team):
		var row: Dictionary = entities[id]
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > POWER_CAST_RADIUS:
			continue
		var maximum_member := int(row.get("member_maximum_health", 1))
		var health_values: Array = row.get("member_health", [])
		var restored := 0
		for member_index in health_values.size():
			if int(health_values[member_index]) > 0 and int(health_values[member_index]) < maximum_member:
				restored += maximum_member - int(health_values[member_index])
				health_values[member_index] = maximum_member
		if restored > 0:
			row["member_health"] = health_values
			row["health"] = 0
			for value in health_values:
				row["health"] = int(row["health"]) + int(value)
			healed += 1
	if healed == 0:
		return {"ok": false, "reason": "no-wounded-allies-in-range"}
	_emit_event("power.cast", 0, 0, {"team": team, "power_id": "SBGood_Heal", "battalions": healed})
	return {"ok": true, "reason": "", "battalions": healed}


func cast_rally(team: int, point: Vector2) -> Dictionary:
	if not has_power(team, "SBGood_RallyingCall"):
		return {"ok": false, "reason": "power-not-purchased"}
	var rallied := 0
	for id in living_ids(team):
		var row: Dictionary = entities[id]
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > POWER_CAST_RADIUS:
			continue
		row["rally_until_tick"] = tick_index + RALLY_DURATION_TICKS
		rallied += 1
	if rallied == 0:
		return {"ok": false, "reason": "no-allies-in-range"}
	_emit_event("power.cast", 0, 0, {"team": team, "power_id": "SBGood_RallyingCall", "battalions": rallied})
	return {"ok": true, "reason": "", "battalions": rallied}


func validate_construct_site(builder_ids: Array[int], structure_kind: String, point: Vector2) -> Dictionary:
	# Non-mutating dry run of issue_construct's admission checks so the
	# placement ghost can tint valid/invalid while the player aims.
	return issue_construct(builder_ids, structure_kind, point, true)


func queue_unit(team: int, producer: int, unit_type: String = SOLDIER_HORDE_ID) -> Dictionary:
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not structures.has(producer):
		return {"ok": false, "reason": "unknown-producer"}
	var building: Dictionary = structures[producer]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(building.get("health", 0)) <= 0 or float(building.get("construction_progress", 0.0)) < 1.0:
		return {"ok": false, "reason": "producer-unavailable"}
	if not Array(building.get("production", [])).has(unit_type):
		return {"ok": false, "reason": "unsupported-unit"}
	var production_rule: Dictionary = _unit_production_rules.get(unit_type, {})
	if production_rule.is_empty():
		return {"ok": false, "reason": "unsupported-unit"}
	if String(production_rule.get("category", "")) == "hero" and hero_unavailable(team, unit_type):
		return {"ok": false, "reason": "hero-unavailable"}
	var required_upgrades := required_upgrades_for_unit(unit_type, String(building.get("structure_kind", "")))
	for required_upgrade_value in required_upgrades:
		var required_upgrade := String(required_upgrade_value)
		if not Array(building.get("completed_upgrades", [])).has(required_upgrade):
			return {"ok": false, "reason": "missing-upgrade", "required_upgrade": required_upgrade}
	var queue: Array = building.get("queue", [])
	if queue.size() >= maxi(1, int(_rules.get("maximum_queue", 5))):
		return {"ok": false, "reason": "queue-full"}
	var cost := maxi(0, _production_rule_value(unit_type, "cost_rule", "default_cost"))
	var command_cost := maxi(1, _production_rule_value(unit_type, "command_points_rule", "default_command_points"))
	var queued_command_points := _queued_command_points_for_team(team)
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources"}
	if command_points_for_team(team) + queued_command_points + command_cost > command_point_cap:
		return {"ok": false, "reason": "command-point-cap"}
	var build_ticks := maxi(1, _production_rule_value(unit_type, "build_ticks_rule", "default_build_ticks"))
	var starts_at := tick_index if queue.is_empty() else int((queue.back() as Dictionary).get("complete_tick", tick_index))
	var item := {
		"unit_type": unit_type,
		"cost": cost,
		"command_points": command_cost,
		"queued_tick": tick_index,
		"start_tick": starts_at,
		"duration_ticks": build_ticks,
		"complete_tick": starts_at + build_ticks,
	}
	for route_value in Array(production_rule.get("producer_routes", [])):
		var route := route_value as Dictionary
		if String(route.get("producer_kind", "")) == String(building.get("structure_kind", "")):
			item["command_id"] = String(route.get("command_id", ""))
			break
	queue.append(item)
	building["queue"] = queue
	team_resources[team] = resources_for_team(team) - cost
	_emit_event("production.queued", producer, 0, {"team": team, "unit_type": unit_type, "command_id": String(item.get("command_id", "")), "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "producer_id": producer, "item": item.duplicate(true)}


func hero_unavailable(team: int, unit_type: String) -> bool:
	var production_rule: Dictionary = _unit_production_rules.get(unit_type, {})
	if String(production_rule.get("category", "")) != "hero":
		return false
	if _completed_hero_identities.has("%d:%s" % [team, unit_type]):
		return true
	for structure_value in structures.values():
		var structure := structure_value as Dictionary
		if int(structure.get("team", -1)) != team:
			continue
		for item_value in Array(structure.get("queue", [])):
			if String((item_value as Dictionary).get("unit_type", "")) == unit_type:
				return true
	for entity_value in entities.values():
		var row := entity_value as Dictionary
		if (
			int(row.get("team", -1)) == team
			and int(row.get("health", 0)) > 0
			and String(row.get("unit_type", "")) == unit_type
		):
			return true
	return false


func production_queue_state(producer: int) -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	if not structures.has(producer):
		return rows
	var queue: Array = (structures[producer] as Dictionary).get("queue", [])
	for index in range(queue.size()):
		if typeof(queue[index]) != TYPE_DICTIONARY:
			continue
		var item: Dictionary = queue[index]
		var start_tick := int(item.get("start_tick", item.get("queued_tick", tick_index)))
		var complete_tick := int(item.get("complete_tick", start_tick + 1))
		var duration_ticks := maxi(1, int(item.get("duration_ticks", complete_tick - start_tick)))
		var active := index == 0
		var elapsed_ticks := clampi(tick_index - start_tick, 0, duration_ticks) if active else 0
		rows.append({
			"index": index,
			"unit_type": String(item.get("unit_type", SOLDIER_HORDE_ID)),
			"cost": int(item.get("cost", 0)),
			"command_points": int(item.get("command_points", 0)),
			"queued_tick": int(item.get("queued_tick", start_tick)),
			"start_tick": start_tick,
			"complete_tick": complete_tick,
			"duration_ticks": duration_ticks,
			"elapsed_ticks": elapsed_ticks,
			"progress": float(elapsed_ticks) / float(duration_ticks) if active else 0.0,
			"active": active,
		})
	return rows


func cancel_queued_unit(team: int, producer: int, queue_index: int = 0) -> Dictionary:
	if not structures.has(producer):
		return {"ok": false, "reason": "unknown-producer"}
	var building: Dictionary = structures[producer]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	var queue: Array = building.get("queue", [])
	if queue_index < 0 or queue_index >= queue.size():
		return {"ok": false, "reason": "unknown-queue-item"}
	var cancelled: Dictionary = queue[queue_index]
	queue.remove_at(queue_index)
	var cursor := tick_index if queue_index == 0 else int((queue[queue_index - 1] as Dictionary).get("complete_tick", tick_index))
	for index in range(queue_index, queue.size()):
		var item: Dictionary = queue[index]
		var prior_start := int(item.get("start_tick", item.get("queued_tick", cursor)))
		var prior_complete := int(item.get("complete_tick", prior_start + 1))
		var duration_ticks := maxi(1, int(item.get("duration_ticks", prior_complete - prior_start)))
		item["start_tick"] = cursor
		item["duration_ticks"] = duration_ticks
		item["complete_tick"] = cursor + duration_ticks
		cursor += duration_ticks
	building["queue"] = queue
	var refund := maxi(0, int(cancelled.get("cost", 0)))
	team_resources[team] = resources_for_team(team) + refund
	_emit_event("production.cancelled", producer, 0, {
		"team": team,
		"unit_type": String(cancelled.get("unit_type", SOLDIER_HORDE_ID)),
		"queue_index": queue_index,
		"refund": refund,
	})
	return {
		"ok": true,
		"reason": "",
		"producer_id": producer,
		"queue_index": queue_index,
		"refund": refund,
		"item": cancelled.duplicate(true),
	}


func select_only(id: int) -> bool:
	if not _is_commandable(id):
		return false
	selected_ids.assign([id])
	_emit_event("voice.select", id, 0)
	return true


func select_many(ids: Array[int]) -> int:
	# Multi-select (box selection / same-type selection): keeps only
	# commandable player battalions; one select voice line for the group.
	var accepted: Array[int] = []
	for id in ids:
		if _is_commandable(id) and not accepted.has(id):
			accepted.append(id)
	if accepted.is_empty():
		return 0
	selected_ids.assign(accepted)
	_emit_event("voice.select", accepted[0], 0)
	return accepted.size()


func toggle_selection(id: int) -> bool:
	if not _is_commandable(id):
		return false
	if selected_ids.has(id):
		selected_ids.erase(id)
	else:
		selected_ids.append(id)
		selected_ids.sort()
		_emit_event("voice.select", id, 0)
	return true


func clear_selection() -> void:
	selected_ids.clear()


func assign_control_group(group: int, ids: Array[int]) -> Dictionary:
	if group < 1 or group > 9:
		return {"ok": false, "reason": "invalid-group", "group": group, "entity_ids": []}
	var assigned: Array[int] = []
	var candidates: Array[int] = ids.duplicate()
	candidates.sort()
	for id in candidates:
		if assigned.has(id) or not _is_living_player(id):
			continue
		assigned.append(id)
	control_groups[group] = assigned.duplicate()
	return {"ok": true, "reason": "", "group": group, "entity_ids": assigned}


func recall_control_group(group: int) -> Array[int]:
	if group < 1 or group > 9:
		return []
	_prune_control_group(group)
	var recalled: Array[int] = []
	for value: Variant in Array(control_groups.get(group, [])):
		recalled.append(int(value))
	return recalled


func prune_control_groups() -> void:
	for group in range(1, 10):
		_prune_control_group(group)


func reset_control_groups() -> void:
	control_groups.clear()
	for group in range(1, 10):
		control_groups[group] = []


func control_groups_snapshot() -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	for group in range(1, 10):
		var ids: Array[int] = []
		for value: Variant in Array(control_groups.get(group, [])):
			ids.append(int(value))
		ids.sort()
		rows.append({"group": group, "entity_ids": ids})
	return rows


# Keep a singular spelling available to presentation callers without making
# the serialized contract depend on a Dictionary's key ordering.
func control_group_snapshot() -> Array[Dictionary]:
	return control_groups_snapshot()


func issue_move(ids: Array[int], destination: Vector2) -> int:
	var accepted_ids: Array[int] = []
	last_route_rejection = ""
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable(id):
			continue
		var row: Dictionary = entities[id]
		if not _assign_route(row, destination):
			continue
		row["target_id"] = 0
		row["target_kind"] = "battalion"
		row["attack_windup"] = 0
		row["attack_move"] = false
		_clear_member_targets(row)
		row["state"] = "run"
		row["order_kind"] = "move"
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		last_route_rejection = ""
		_emit_event("order.move", accepted_ids[0], 0)
	return accepted_ids.size()


func issue_attack(ids: Array[int], target_id: int) -> int:
	var target_kind := "battalion" if entities.has(target_id) else ("structure" if structures.has(target_id) else "")
	if target_kind == "":
		return 0
	var target: Dictionary = entities[target_id] if target_kind == "battalion" else structures[target_id]
	if int(target["health"]) <= 0:
		return 0
	var accepted_ids: Array[int] = []
	last_route_rejection = ""
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable(id):
			continue
		var row: Dictionary = entities[id]
		if int(row["team"]) == int(target["team"]):
			continue
		if not _assign_route(row, Vector2(target["position"])):
			continue
		row["target_id"] = target_id
		row["target_kind"] = target_kind
		row["attack_windup"] = 0
		row["attack_move"] = false
		_clear_member_targets(row)
		row["state"] = "run"
		row["order_kind"] = "attack"
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		last_route_rejection = ""
		_emit_event("voice.attack", accepted_ids[0], target_id)
		_emit_music("battle")
	return accepted_ids.size()


func issue_attack_move(ids: Array[int], destination: Vector2) -> int:
	var accepted := issue_move(ids, destination)
	if accepted <= 0:
		return 0
	for id in ids:
		if not _is_commandable(id):
			continue
		var row: Dictionary = entities[id]
		if Vector2(row.get("destination", row["position"])).is_equal_approx(destination):
			row["attack_move"] = true
			row["attack_move_destination"] = destination
			row["order_kind"] = "attack_move"
	return accepted


func issue_stop(ids: Array[int]) -> int:
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable(id):
			continue
		var row: Dictionary = entities[id]
		row["target_id"] = 0
		row["target_kind"] = "battalion"
		row["attack_windup"] = 0
		row["attack_move"] = false
		_clear_member_attack_schedule(row)
		_clear_member_targets(row)
		_clear_pending_route(row, true)
		row["state"] = "idle"
		row["order_kind"] = ""
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		_emit_event("order.stop", accepted_ids[0], 0)
	return accepted_ids.size()


func issue_toggle_stance(ids: Array[int]) -> int:
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable(id):
			continue
		var row: Dictionary = entities[id]
		var index := STANCE_ORDER.find(String(row.get("stance", "Battle")))
		row["stance"] = STANCE_ORDER[posmod(index + 1, STANCE_ORDER.size())]
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		_emit_event("order.stance", accepted_ids[0], 0, {"stance": String((entities[accepted_ids[0]] as Dictionary)["stance"])})
	return accepted_ids.size()


func issue_set_stance(ids: Array[int], stance: String) -> int:
	if not STANCE_ORDER.has(stance):
		return 0
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable(id):
			continue
		(entities[id] as Dictionary)["stance"] = stance
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		_emit_event("order.stance", accepted_ids[0], 0, {"stance": stance})
	return accepted_ids.size()


func advance(ticks: int) -> void:
	for _index in range(maxi(0, ticks)):
		tick()


func tick() -> void:
	if winner != -1:
		tick_index += 1
		_cleanup_expired_corpses()
		return
	tick_index += 1
	if base_loop_enabled:
		_step_economy()
		_step_structure_upgrades()
		_step_production()
	if ai_enabled and tick_index % 15 == 0:
		_update_enemy_ai()
	for id in entity_ids():
		_step_entity(id)
	_step_construction()
	_cleanup_expired_corpses()
	_resolve_victory()


func _step_economy() -> void:
	var interval := maxi(1, int(_rules.get("farm_payout_ticks", 50)))
	if tick_index % interval != 0:
		return
	for id in structure_ids():
		var row: Dictionary = structures[id]
		var income := int(row.get("income_per_payout", 0))
		if income <= 0 or int(row.get("health", 0)) <= 0 or float(row.get("construction_progress", 0.0)) < 1.0:
			continue
		var team := int(row.get("team", -1))
		team_resources[team] = resources_for_team(team) + income
		_emit_event("economy.payout", id, 0, {"team": team, "amount": income})


func _step_structure_upgrades() -> void:
	for structure_id in structure_ids():
		var building: Dictionary = structures[structure_id]
		if int(building.get("health", 0)) <= 0:
			continue
		var queue: Array = building.get("upgrade_queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if tick_index < int(item.get("complete_tick", tick_index + 1)):
			continue
		var upgrade_id := String(item.get("upgrade_id", ""))
		var contract: Dictionary = _structure_upgrade_contracts.get(upgrade_id, {})
		if contract.is_empty():
			configuration_error = "queued structure upgrade lost its contract"
			continue
		queue.pop_front()
		building["upgrade_queue"] = queue
		var completed: Array = building.get("completed_upgrades", [])
		if not completed.has(upgrade_id):
			completed.append(upgrade_id)
		building["completed_upgrades"] = completed
		building["level"] = mini(
			int(contract.get("level_cap", 1)),
			int(building.get("level", 1)) + int(contract.get("levels_to_gain", 0))
		)
		building["command_set"] = String(contract.get("to_command_set", ""))
		_emit_event("upgrade.completed", structure_id, 0, {
			"team": int(building.get("team", -1)),
			"upgrade_id": upgrade_id,
			"level": int(building["level"]),
			"command_set": String(building["command_set"]),
		})


func _step_production() -> void:
	for id in structure_ids():
		var building: Dictionary = structures[id]
		if int(building.get("health", 0)) <= 0:
			continue
		var queue: Array = building.get("queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if tick_index < int(item.get("complete_tick", tick_index + 1)):
			continue
		queue.pop_front()
		building["queue"] = queue
		var team := int(building.get("team", -1))
		var new_id := int(_next_dynamic_id.get(team, 10 if team == PLAYER_TEAM else 110))
		_next_dynamic_id[team] = new_id + 1
		var production_origin := Vector2(building.get("position", Vector2.ZERO))
		var rally := Vector2(building.get("rally", production_origin))
		var exit_direction := production_origin.direction_to(rally)
		if exit_direction.length_squared() <= 0.000001:
			exit_direction = Vector2.RIGHT if team == PLAYER_TEAM else Vector2.LEFT
		var door_point := production_origin + exit_direction * PRODUCTION_DOOR_INSET_RADIUS
		var create_point := production_origin + exit_direction * PRODUCTION_EXIT_RADIUS
		var unit_type := String(item.get("unit_type", SOLDIER_HORDE_ID))
		var production_rule: Dictionary = _unit_production_rules.get(unit_type, _unit_production_rules[SOLDIER_HORDE_ID])
		var object_id := String(production_rule.get("object_id", SOLDIER_OBJECT_ID))
		var display_name := String(production_rule.get("display_name", "Gondor Soldiers"))
		var committed_command_points := int(item.get("command_points", 60))
		# QueueProductionExitUpdate uses a create point at the producer doorway,
		# reveals the horde there, and only then sends it to the rally point.
		_add_battalion(new_id, team, door_point, display_name, object_id, unit_type, committed_command_points)
		if String(production_rule.get("category", "")) == "hero":
			_completed_hero_identities["%d:%s" % [team, unit_type]] = true
		var produced: Dictionary = entities[new_id]
		produced["production_producer_id"] = id
		produced["production_exit_start_tick"] = tick_index
		produced["production_exit_duration_ticks"] = PRODUCTION_EXIT_DURATION_TICKS
		produced["production_exit_progress"] = 0.0
		produced["production_exit_origin"] = door_point
		produced["production_exit_destination"] = create_point
		produced["production_rally"] = rally
		produced["facing"] = exit_direction
		team_command_points[team] = command_points_for_team(team) + committed_command_points
		_emit_event("production.complete", id, new_id, {
			"team": team,
			"unit_type": unit_type,
			"object_id": object_id,
			"production_origin": production_origin,
			"create_point": create_point,
			"rally": rally,
			"exit_duration_ticks": PRODUCTION_EXIT_DURATION_TICKS,
			"exit_route_accepted": false,
		})


func _step_entity(id: int) -> void:
	var row: Dictionary = entities[id]
	if int(row["health"]) <= 0:
		row["state"] = "death"
		_clear_pending_route(row, true)
		return
	row["attack_cooldown"] = maxi(0, int(row["attack_cooldown"]) - 1)
	if _step_production_exit(row):
		return
	if bool(row.get("is_builder", false)):
		var construction_id := int(row.get("construction_id", 0))
		if construction_id != 0 and structures.has(construction_id):
			var site: Dictionary = structures[construction_id]
			if float(site.get("construction_progress", 1.0)) < 1.0:
				var site_position := Vector2(site.get("position", row["position"]))
				# A successful navigation query may end at the closest walkable
				# perimeter cell rather than the structure's obstructed center.
				# Exhausting that accepted route is arrival; requiring another
				# invented center-distance strands real-map construction sites.
				if String(row.get("order_kind", "")) == "construct" and ((row["route"] as Array).is_empty() or Vector2(row["position"]).distance_to(site_position) <= 2.0):
					_clear_pending_route(row, true)
					row["state"] = "construct"
					return
		if not (row["route"] as Array).is_empty():
			_step_route(row)
		else:
			row["state"] = "idle"
		return
	var target_id := int(row["target_id"])
	if target_id == 0 and (row["route"] as Array).is_empty() and not bool(row.get("attack_move", false)):
		var auto_target := _nearest_auto_target(row)
		if not auto_target.is_empty():
			target_id = int(auto_target["id"])
			row["target_id"] = target_id
			row["target_kind"] = String(auto_target["kind"])
			row["order_kind"] = "auto_attack"
	if target_id == 0 and bool(row.get("attack_move", false)) and not (row["route"] as Array).is_empty():
		var acquired := _nearest_attack_move_target(row)
		if acquired != 0:
			row["target_id"] = acquired
			row["target_kind"] = "battalion"
			target_id = acquired
	if target_id != 0:
		var target_kind := String(row.get("target_kind", "battalion"))
		if not _target_alive(target_id, target_kind):
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			row["attack_windup"] = 0
			_clear_member_attack_schedule(row)
			_clear_member_targets(row)
			if bool(row.get("attack_move", false)) and _assign_route(row, Vector2(row.get("attack_move_destination", row["position"]))):
				row["state"] = "run"
			else:
				_clear_pending_route(row, true)
				row["state"] = "idle"
			return
		var target_position := _target_position(target_id, target_kind)
		# Retail/OpenSAGE range is center-to-center: Weapon.cs:54-58 compares the
		# attacker's Translation against the target position without adding either
		# object's bounding radius.
		var distance := Vector2(row["position"]).distance_to(target_position)
		var selected_weapon_mode := _weapon_mode_for_distance(row, distance)
		if selected_weapon_mode == "unsupported-close":
			row["state"] = "idle"
			row["attack_windup"] = 0
			_clear_pending_route(row, true)
			_clear_member_attack_schedule(row)
			return
		_apply_weapon_mode(row, selected_weapon_mode)
		var minimum_range := float(row.get("minimum_attack_range", 0.0))
		if distance <= float(row["attack_range"]) and (minimum_range <= 0.0 or distance >= minimum_range):
			row["state"] = "attack"
			_clear_pending_route(row, true)
			_step_member_attacks(id, row, target_id, target_kind)
			return
		row["attack_windup"] = 0
		_clear_member_attack_schedule(row)
		if (row["route"] as Array).is_empty():
			if not _assign_route(row, target_position):
				row["target_id"] = 0
				_clear_pending_route(row, true)
				row["state"] = "idle"
				return
		row["state"] = "run"
		_step_route(row)
		return
	if not (row["route"] as Array).is_empty():
		row["state"] = "run"
		_step_route(row)
	else:
		row["attack_move"] = false
		row["state"] = "idle"


func issue_construct(ids: Array[int], structure_kind: String, position: Vector2, dry_run: bool = false) -> Dictionary:
	return _issue_construct_for_team(PLAYER_TEAM, ids, structure_kind, position, dry_run)


func _issue_construct_for_team(team: int, ids: Array[int], structure_kind: String, position: Vector2, dry_run: bool = false) -> Dictionary:
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if team != PLAYER_TEAM and team != ENEMY_TEAM:
		return {"ok": false, "reason": "invalid-team"}
	if not _structure_build_rules.has(structure_kind):
		return {"ok": false, "reason": "unsupported-structure"}
	if playable_outline.size() >= 3 and not Geometry2D.is_point_in_polygon(position, playable_outline):
		return {"ok": false, "reason": "outside-playable-area"}
	for existing_id in structure_ids():
		var existing_position := Vector2((structures[existing_id] as Dictionary).get("position", Vector2.ZERO))
		if existing_position.distance_to(position) < 7.0:
			return {"ok": false, "reason": "site-obstructed"}
	var builder_id := 0
	for value in ids:
		var id := int(value)
		if not entities.has(id):
			continue
		var row: Dictionary = entities[id]
		if int(row.get("team", -1)) != team or int(row.get("health", 0)) <= 0 or not bool(row.get("is_builder", false)):
			continue
		builder_id = id
		break
	if builder_id == 0:
		return {"ok": false, "reason": "builder-required"}
	var build_rule: Dictionary = _structure_build_rules[structure_kind]
	var cost := int(build_rule["cost"])
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	if dry_run:
		return {"ok": true, "reason": "", "dry_run": true, "cost": cost}
	var structure_id := _next_dynamic_structure_id
	_next_dynamic_structure_id += 1
	var maximum_health := int(_structure_max_health[structure_kind])
	var production: Array[String] = []
	for unit_type in _production_unit_order:
		var production_rule: Dictionary = _unit_production_rules[unit_type]
		if String(production_rule.get("producer_kind", "")) == structure_kind:
			production.append(unit_type)
	var build_ticks := maxi(1, roundi(float(build_rule["seconds"]) / TICK_SECONDS))
	structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": structure_kind,
		"name": structure_kind.replace("_", " ").capitalize(),
		"position": position,
		"rally": position + Vector2(4.0, 0.0),
		"health": maximum_health,
		"maximum_health": maximum_health,
		"construction_progress": 0.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"construction_build_ticks": build_ticks,
		"construction_elapsed_ticks": 0,
		"builder_id": builder_id,
		"production": production,
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": int(_rules.get("farm_income", 25)) if structure_kind == "farm" else 0,
	}
	team_resources[team] = resources_for_team(team) - cost
	var builder: Dictionary = entities[builder_id]
	builder["construction_id"] = structure_id
	builder["order_kind"] = "construct"
	builder["target_id"] = 0
	_clear_member_targets(builder)
	if not _assign_route(builder, position):
		structures.erase(structure_id)
		team_resources[team] = resources_for_team(team) + cost
		builder["construction_id"] = 0
		return {"ok": false, "reason": last_route_rejection if last_route_rejection != "" else "route-rejected"}
	_emit_event("construction.started", builder_id, structure_id, {"team": team, "structure_kind": structure_kind, "cost": cost, "build_ticks": build_ticks})
	return {"ok": true, "builder_id": builder_id, "structure_id": structure_id, "cost": cost, "build_ticks": build_ticks}


func _step_construction() -> void:
	for structure_id in structure_ids():
		var site: Dictionary = structures[structure_id]
		if float(site.get("construction_progress", 1.0)) >= 1.0:
			continue
		var builder_id := int(site.get("builder_id", 0))
		if not entities.has(builder_id):
			continue
		var builder: Dictionary = entities[builder_id]
		if int(builder.get("health", 0)) <= 0 or String(builder.get("state", "")) != "construct":
			continue
		var elapsed := int(site.get("construction_elapsed_ticks", 0)) + 1
		var build_ticks := maxi(1, int(site.get("construction_build_ticks", 1)))
		site["construction_elapsed_ticks"] = elapsed
		site["construction_progress"] = minf(1.0, float(elapsed) / float(build_ticks))
		if elapsed >= build_ticks:
			builder["construction_id"] = 0
			builder["order_kind"] = ""
			builder["state"] = "idle"
			if int(site.get("team", -1)) == ENEMY_TEAM:
				_enemy_ai_construction_resolved = true
			_emit_event("construction.completed", builder_id, structure_id, {"team": int(site.get("team", -1)), "structure_kind": String(site.get("structure_kind", ""))})
func _nearest_attack_move_target(row: Dictionary) -> int:
	var enemy_team := ENEMY_TEAM if int(row.get("team", PLAYER_TEAM)) == PLAYER_TEAM else PLAYER_TEAM
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var limit := maxf(float(row.get("attack_range", 1.0)), float(row.get("vision_range", 17.5)))
	var result := 0
	var best := limit
	for candidate in living_ids(enemy_team):
		var distance := origin.distance_to(Vector2((entities[candidate] as Dictionary).get("position", Vector2.ZERO)))
		if distance <= best:
			best = distance
			result = candidate
	return result


func _nearest_auto_target(row: Dictionary) -> Dictionary:
	var enemy_team := ENEMY_TEAM if int(row.get("team", PLAYER_TEAM)) == PLAYER_TEAM else PLAYER_TEAM
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var stance := String(row.get("stance", "Battle"))
	var stance_state := _stance_state(row, stance)
	var limit := float(row.get("vision_range", 0.0)) * float(stance_state.get("visionMultiplier", 1.0))
	if stance == "HoldGround":
		var modes: Dictionary = row.get("weapon_modes", {}) as Dictionary
		var default_mode: Dictionary = modes.get(String(row.get("default_weapon_mode", "default")), {}) as Dictionary
		limit = float(default_mode.get("attack_range", row.get("attack_range", 0.0)))
	if limit <= 0.0:
		return {}
	var best_id := 0
	var best_kind := ""
	var best_distance := limit
	for candidate in living_ids(enemy_team):
		var distance := origin.distance_to(Vector2((entities[candidate] as Dictionary).get("position", Vector2.ZERO)))
		if distance <= best_distance:
			best_distance = distance
			best_id = candidate
			best_kind = "battalion"
	for candidate in living_structure_ids(enemy_team):
		var distance := origin.distance_to(Vector2((structures[candidate] as Dictionary).get("position", Vector2.ZERO)))
		if distance <= best_distance:
			best_distance = distance
			best_id = candidate
			best_kind = "structure"
	return {"id": best_id, "kind": best_kind} if best_id != 0 else {}


func _step_production_exit(row: Dictionary) -> bool:
	var duration := int(row.get("production_exit_duration_ticks", 0))
	var start_tick := int(row.get("production_exit_start_tick", -1))
	if duration <= 0 or start_tick < 0:
		row["production_exit_progress"] = 1.0
		return false
	var elapsed := maxi(0, tick_index - start_tick)
	var progress := clampf(float(elapsed) / float(duration), 0.0, 1.0)
	row["production_exit_progress"] = progress
	var exit_origin := Vector2(row.get("production_exit_origin", row.get("position", Vector2.ZERO)))
	var exit_destination := Vector2(row.get("production_exit_destination", exit_origin))
	row["position"] = exit_origin.lerp(exit_destination, smoothstep(0.0, 1.0, progress))
	var exit_direction := exit_origin.direction_to(exit_destination)
	if exit_direction.length_squared() > 0.000001:
		row["facing"] = exit_direction
	if elapsed >= duration:
		row["production_exit_start_tick"] = -1
		row["production_exit_duration_ticks"] = 0
		row["production_exit_progress"] = 1.0
		var rally := Vector2(row.get("production_rally", exit_destination))
		if not rally.is_equal_approx(exit_destination) and _assign_route(row, rally):
			row["state"] = "run"
		else:
			_clear_pending_route(row, true)
			row["state"] = "idle"
		_emit_event("production.exit_complete", int(row.get("production_producer_id", 0)), int(row.get("id", 0)), {
			"rally": rally,
			"exit_destination": exit_destination,
		})
		return false
	# The horde presentation reveals retail members through the door one by one.
	# Keep its authoritative root at the source-derived doorway until all members
	# have emerged, then let the already accepted rally route advance normally.
	row["state"] = "run"
	return true


func _step_member_attacks(attacker_id: int, row: Dictionary, target_id: int, target_kind: String) -> void:
	var member_health_values: Array = row.get("member_health", [])
	var start_ticks: Array = row.get("member_attack_start_ticks", [])
	var hit_ticks: Array = row.get("member_attack_hit_ticks", [])
	var tokens: Array = row.get("member_attack_tokens", [])
	var target_indices: Array = row.get("member_target_indices", [])
	var weapon_modes: Array = row.get("member_weapon_modes", [])
	var release_tokens: Array = row.get("member_attack_release_tokens", [])
	if member_health_values.is_empty() or start_ticks.size() != member_health_values.size() or hit_ticks.size() != member_health_values.size() or tokens.size() != member_health_values.size() or target_indices.size() != member_health_values.size() or weapon_modes.size() != member_health_values.size() or release_tokens.size() != member_health_values.size():
		return
	if target_kind == "battalion":
		_ensure_member_target_assignments(row, entities[target_id] as Dictionary)
		target_indices = row.get("member_target_indices", [])
	if int(row.get("attack_cooldown", 0)) == 0:
		var pre_attack_ticks := maxi(0, int(row.get("pre_attack_ticks", 0)))
		var maximum_stagger := maxi(0, MEMBER_ATTACK_STAGGER_WINDOW_TICKS - 1)
		var coast_ticks := maxi(0, int(row.get("continuous_fire_coast_ticks", 0)))
		var expiration_tick := int(row.get("continuous_fire_expiration_tick", -1))
		if expiration_tick < 0 or tick_index >= expiration_tick:
			row["continuous_fire_count"] = 0
		var continuous_threshold := maxi(0, int(row.get("continuous_fire_one", 0)))
		var rate_multiplier := maxf(1.0, float(row.get("continuous_fire_rate_multiplier", 1.0)))
		var base_reload_or_delay_ms := float(row.get("delay_between_shots_ms", 0.0))
		if int(row.get("clip_size", 0)) == 1:
			base_reload_or_delay_ms = float(row.get("clip_reload_time_ms", base_reload_or_delay_ms))
		# SAGE captures the possible-next-shot frame before FiringTracker promotes
		# the shot count into the next continuous-fire tier.
		var coast_anchor_ms := base_reload_or_delay_ms
		if continuous_threshold > 0 and int(row.get("continuous_fire_count", 0)) > continuous_threshold:
			coast_anchor_ms = floorf(coast_anchor_ms / rate_multiplier)
		row["attack_sequence"] = int(row.get("attack_sequence", 0)) + 1
		row["continuous_fire_count"] = int(row.get("continuous_fire_count", 0)) + 1
		var attack_sequence := int(row["attack_sequence"])
		for member_index in range(member_health_values.size()):
			if int(member_health_values[member_index]) <= 0:
				start_ticks[member_index] = -1
				hit_ticks[member_index] = -1
				continue
			var stagger := posmod(attacker_id + member_index * 3 + attack_sequence, MEMBER_ATTACK_STAGGER_WINDOW_TICKS)
			weapon_modes[member_index] = String(row.get("active_weapon_mode", "default"))
			start_ticks[member_index] = tick_index + stagger
			# Every member owns its attack boundary. This avoids the old whole-
			# horde structure impact while preserving deterministic replay.
			hit_ticks[member_index] = tick_index + stagger + pre_attack_ticks
		var reload_or_delay_ms := base_reload_or_delay_ms
		if continuous_threshold > 0 and int(row["continuous_fire_count"]) > continuous_threshold:
			reload_or_delay_ms = floorf(reload_or_delay_ms / rate_multiplier)
		var cadence_ms := (
			float(row.get("pre_attack_delay_ms", 0.0))
			+ float(row.get("firing_duration_ms", 0.0))
			+ reload_or_delay_ms
		)
		var cadence_ticks := maxi(1, roundi(cadence_ms / (TICK_SECONDS * 1000.0)))
		var coast_anchor_ticks := maxi(1, roundi(coast_anchor_ms / (TICK_SECONDS * 1000.0)))
		row["continuous_fire_expiration_tick"] = tick_index + coast_anchor_ticks + coast_ticks
		row["attack_cooldown"] = maxi(
			cadence_ticks,
			pre_attack_ticks + maximum_stagger + 1
		)
		row["attack_windup"] = pre_attack_ticks + maximum_stagger
		_emit_event("combat.swing", attacker_id, target_id, {
			"attack_sequence": attack_sequence,
			"living_members": _living_member_count(row),
		})
	for member_index in range(member_health_values.size()):
		if int(member_health_values[member_index]) <= 0:
			continue
		if int(start_ticks[member_index]) == tick_index:
			tokens[member_index] = int(tokens[member_index]) + 1
			start_ticks[member_index] = -1
			_emit_event("combat.member_swing", attacker_id, target_id, {
				"member_index": member_index,
				"target_member_index": int(target_indices[member_index]),
				"weapon_mode": String(weapon_modes[member_index]),
				"member_attack_token": int(tokens[member_index]),
				"attack_sequence": int(row.get("attack_sequence", 0)),
			})
		if int(hit_ticks[member_index]) == tick_index:
			hit_ticks[member_index] = -1
			if String(weapon_modes[member_index]) != "close":
				release_tokens[member_index] = int(release_tokens[member_index]) + 1
				_emit_event("combat.member_fire", attacker_id, target_id, {
					"member_index": member_index,
					"target_member_index": int(target_indices[member_index]),
					"weapon_mode": String(weapon_modes[member_index]),
					"member_release_token": int(release_tokens[member_index]),
				})
			if _target_alive(target_id, target_kind):
				var forced_target := int(target_indices[member_index]) if target_kind == "battalion" else -1
				_apply_member_damage(
					attacker_id,
					member_index,
					target_id,
					maxi(1, roundi(float(row.get("member_damage", 1)) * float(_stance_state(row).get("damageMultiplier", 1.0)))),
					target_kind,
					int(row.get("attack_sequence", 0)),
					forced_target
				)
	row["member_attack_start_ticks"] = start_ticks
	row["member_attack_hit_ticks"] = hit_ticks
	row["member_attack_tokens"] = tokens
	row["member_target_indices"] = target_indices
	row["member_weapon_modes"] = weapon_modes
	row["member_attack_release_tokens"] = release_tokens
	row["attack_windup"] = maxi(0, int(row.get("attack_windup", 0)) - 1)


func _clear_member_attack_schedule(row: Dictionary) -> void:
	var start_ticks: Array = row.get("member_attack_start_ticks", [])
	var hit_ticks: Array = row.get("member_attack_hit_ticks", [])
	for index in range(start_ticks.size()):
		start_ticks[index] = -1
	for index in range(hit_ticks.size()):
		hit_ticks[index] = -1
	row["member_attack_start_ticks"] = start_ticks
	row["member_attack_hit_ticks"] = hit_ticks


func _clear_member_targets(row: Dictionary) -> void:
	var targets: Array = row.get("member_target_indices", [])
	for index in range(targets.size()):
		targets[index] = -1
	row["member_target_indices"] = targets


func _weapon_mode_for_distance(row: Dictionary, distance: float) -> String:
	var close_mode := String(row.get("close_weapon_mode", ""))
	var switch_distance := float(row.get("close_weapon_switch_distance", 0.0))
	if bool(row.get("unsupported_close_weapon", false)) and switch_distance > 0.0 and distance <= switch_distance:
		return "unsupported-close"
	if close_mode != "" and switch_distance > 0.0 and distance <= switch_distance:
		return close_mode
	return String(row.get("default_weapon_mode", "default"))


func _stance_state(row: Dictionary, requested: String = "") -> Dictionary:
	var contract: Dictionary = row.get("stance_contract", {}) as Dictionary
	var states: Dictionary = contract.get("states", {}) as Dictionary
	var stance := requested if requested != "" else String(row.get("stance", "Battle"))
	var selected: Dictionary = states.get(stance, {}) as Dictionary
	if not selected.is_empty():
		return selected
	return {
		"damageMultiplier": 1.0,
		"incomingDamageMultiplier": 1.0,
		"visionMultiplier": 1.0,
		"speedMultiplier": 1.0,
	}


func _apply_weapon_mode(row: Dictionary, mode: String) -> void:
	var modes: Dictionary = row.get("weapon_modes", {}) as Dictionary
	var selected: Dictionary = modes.get(mode, modes.get(String(row.get("default_weapon_mode", "default")), {})) as Dictionary
	if selected.is_empty():
		return
	var prior := String(row.get("active_weapon_mode", ""))
	row["active_weapon_mode"] = mode
	for field in [
		"attack_range", "attack_range_source", "minimum_attack_range",
		"minimum_attack_range_source", "delay_between_shots_ms",
		"pre_attack_delay_ms", "firing_duration_ms", "attack_period_ticks",
		"pre_attack_ticks", "firing_duration_ticks", "member_damage", "clip_size",
		"clip_reload_time_ms", "continuous_fire_one", "continuous_fire_coast_ticks",
		"continuous_fire_rate_multiplier",
	]:
		if selected.has(field):
			row[field] = selected[field]
	if prior != "" and prior != mode:
		_clear_member_attack_schedule(row)


func _ensure_member_target_assignments(attacker: Dictionary, target: Dictionary) -> void:
	var attacker_health: Array = attacker.get("member_health", [])
	var target_health: Array = target.get("member_health", [])
	var assignments: Array = attacker.get("member_target_indices", [])
	if assignments.size() != attacker_health.size() or target_health.is_empty():
		return
	var use_counts: Array[int] = []
	use_counts.resize(target_health.size())
	use_counts.fill(0)
	for member_index in range(assignments.size()):
		var candidate := int(assignments[member_index])
		if int(attacker_health[member_index]) <= 0 or candidate < 0 or candidate >= target_health.size() or int(target_health[candidate]) <= 0:
			assignments[member_index] = -1
		else:
			use_counts[candidate] += 1
	for member_index in range(assignments.size()):
		if int(attacker_health[member_index]) <= 0 or int(assignments[member_index]) >= 0:
			continue
		var attacker_position := _member_world_position(attacker, member_index)
		var best_index := -1
		var best_score := INF
		for target_index in range(target_health.size()):
			if int(target_health[target_index]) <= 0:
				continue
			var target_position := _member_world_position(target, target_index)
			var score := float(use_counts[target_index]) * 10000.0 + attacker_position.distance_squared_to(target_position)
			if score < best_score:
				best_score = score
				best_index = target_index
		if best_index >= 0:
			assignments[member_index] = best_index
			use_counts[best_index] += 1
	attacker["member_target_indices"] = assignments


func _member_world_position(row: Dictionary, member_index: int) -> Vector2:
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var positions: Array = row.get("formation_positions", [])
	if member_index < 0 or member_index >= positions.size() or typeof(positions[member_index]) != TYPE_VECTOR3:
		return origin
	var slot: Vector3 = positions[member_index]
	var local := Vector2(slot.x, slot.z)
	var facing := Vector2(row.get("facing", Vector2.RIGHT))
	if facing.length_squared() <= 0.000001:
		return origin + local
	return origin + local.rotated(facing.angle())


func _living_member_count(row: Dictionary) -> int:
	var result := 0
	for health_value in Array(row.get("member_health", [])):
		if int(health_value) > 0:
			result += 1
	return result


const STRUCTURE_BLOCK_RADIUS := {"fortress": 7.5, "farm": 4.5, "barracks": 4.5, "archery_range": 4.5, "stable": 4.5}


func _deflect_around_structures(position: Vector2, attack_target_id: int) -> Vector2:
	# Battalions slide around building footprints instead of clipping through
	# them. The battalion's own attack target is exempt so melee can close in.
	for structure_id in structure_ids():
		if structure_id == attack_target_id:
			continue
		var structure_row: Dictionary = structures[structure_id]
		if int(structure_row.get("health", 0)) <= 0:
			continue
		# Construction sites do not block movement: builders must reach their
		# own site, and scaffolding is passable until the structure completes.
		if float(structure_row.get("construction_progress", 1.0)) < 1.0:
			continue
		var radius := float(STRUCTURE_BLOCK_RADIUS.get(String(structure_row.get("structure_kind", "")), 4.5))
		var center := Vector2(structure_row.get("position", Vector2.ZERO))
		var offset := position - center
		var distance := offset.length()
		if distance < radius and distance > 0.001:
			position = center + offset / distance * radius
	return position


func _step_route(row: Dictionary) -> void:
	var route: Array = row["route"]
	if route.is_empty():
		return
	var position := Vector2(row["position"])
	var waypoint := Vector2(route[0])
	var step_distance := float(row["speed"]) * float(_stance_state(row).get("speedMultiplier", 1.0)) * TICK_SECONDS
	var movement_direction := position.direction_to(waypoint)
	if movement_direction.length_squared() > 0.000001:
		row["facing"] = movement_direction
	if position.distance_to(waypoint) <= step_distance:
		position = waypoint
		route.pop_front()
	else:
		position += position.direction_to(waypoint) * step_distance
	position = _deflect_around_structures(position, int(row.get("target_id", 0)))
	row["position"] = position
	row["route"] = route
	if route.is_empty():
		_clear_pending_route(row, int(row["target_id"]) == 0)
		if int(row["target_id"]) == 0:
			row["state"] = "idle"


func _apply_damage(attacker_id: int, target_id: int, amount: int, target_kind: String = "battalion") -> void:
	if target_kind == "structure":
		_apply_structure_damage(attacker_id, target_id, amount)
		return
	if not entities.has(target_id):
		return
	var target: Dictionary = entities[target_id]
	var remaining := maxi(0, amount)
	while remaining > 0 and int(target.get("health", 0)) > 0:
		var target_member := _choose_target_member(target, attacker_id, 0, int(target.get("attack_sequence", 0)))
		if target_member < 0:
			break
		var health_values: Array = target.get("member_health", [])
		var applied := mini(remaining, int(health_values[target_member]))
		_apply_member_damage(attacker_id, -1, target_id, applied, "battalion", int(target.get("attack_sequence", 0)), target_member)
		remaining -= applied


func _apply_member_damage(
	attacker_id: int,
	attacker_member_index: int,
	target_id: int,
	amount: int,
	target_kind: String,
	attack_sequence: int,
	forced_target_member: int = -1
) -> void:
	if target_kind == "structure":
		_apply_structure_damage(attacker_id, target_id, amount)
		return
	if not entities.has(target_id):
		return
	var target: Dictionary = entities[target_id]
	if int(target.get("health", 0)) <= 0:
		return
	var health_values: Array = target.get("member_health", [])
	var target_member := forced_target_member
	if target_member < 0:
		target_member = _choose_target_member(target, attacker_id, attacker_member_index, attack_sequence)
	if target_member < 0 or target_member >= health_values.size():
		return
	var prior_health := int(health_values[target_member])
	if prior_health <= 0:
		return
	var stance_adjusted_amount := maxi(0, roundi(float(amount) * float(_stance_state(target).get("incomingDamageMultiplier", 1.0))))
	if entities.has(attacker_id) and tick_index < int((entities[attacker_id] as Dictionary).get("rally_until_tick", -1)):
		# Rallying Call: provisional +50% damage while the buff lasts (exact
		# retail magnitude is an M3 INI extraction item).
		stance_adjusted_amount = int(ceil(stance_adjusted_amount * 1.5))
	health_values[target_member] = maxi(0, prior_health - stance_adjusted_amount)
	target["member_health"] = health_values
	var aggregate_health := 0
	for health_value in health_values:
		aggregate_health += int(health_value)
	target["health"] = aggregate_health
	_emit_event("combat.hit", attacker_id, target_id, {
		"attacker_member_index": attacker_member_index,
		"target_member_index": target_member,
		"amount": mini(prior_health, stance_adjusted_amount),
		"target_member_health": int(health_values[target_member]),
	})
	if prior_health > 0 and int(health_values[target_member]) == 0:
		var corpse_ticks: Array = target.get("member_corpse_expire_ticks", [])
		if target_member < corpse_ticks.size():
			corpse_ticks[target_member] = tick_index + CORPSE_LIFETIME_TICKS
			target["member_corpse_expire_ticks"] = corpse_ticks
		_emit_event("battalion.member_defeated", attacker_id, target_id, {"member_index": target_member})
	if int(target["target_id"]) == 0 and int(target["health"]) > 0:
		var attacker_position := Vector2((entities[attacker_id] as Dictionary)["position"])
		if _assign_route(target, attacker_position):
			target["target_id"] = attacker_id
			_stamp_order_sequence([target_id])
			_emit_music("battle")
	if int(target["health"]) == 0:
		target["corpse_expire_tick"] = tick_index + CORPSE_LIFETIME_TICKS
		target["state"] = "death"
		target["target_id"] = 0
		_clear_pending_route(target, true)
		selected_ids.erase(target_id)
		if base_loop_enabled:
			var target_team := int(target.get("team", -1))
			team_command_points[target_team] = maxi(0, command_points_for_team(target_team) - int(target.get("command_points", _rules.get("soldier_command_points", 60))))
		prune_control_groups()
		if entities.has(attacker_id):
			award_power_kill(int((entities[attacker_id] as Dictionary).get("team", -1)))
		_emit_event("battalion.defeated", attacker_id, target_id)


func _cleanup_expired_corpses() -> void:
	var expired: Array[int] = []
	for id in entity_ids():
		var row: Dictionary = entities[id]
		var expire_tick := int(row.get("corpse_expire_tick", -1))
		if int(row.get("health", 0)) <= 0 and expire_tick >= 0 and tick_index >= expire_tick:
			expired.append(id)
	for id in expired:
		entities.erase(id)
		selected_ids.erase(id)
		_emit_event("battalion.corpse_expired", id, 0)
	if not expired.is_empty():
		prune_control_groups()


func _choose_target_member(
	target: Dictionary,
	attacker_id: int,
	attacker_member_index: int,
	attack_sequence: int
) -> int:
	var health_values: Array = target.get("member_health", [])
	if health_values.is_empty():
		return -1
	var start := posmod(attacker_id * 31 + maxi(0, attacker_member_index) * 17 + attack_sequence * 13, health_values.size())
	for offset in range(health_values.size()):
		var candidate := posmod(start + offset, health_values.size())
		if int(health_values[candidate]) > 0:
			return candidate
	return -1


func _apply_structure_damage(attacker_id: int, target_id: int, amount: int) -> void:
	if not structures.has(target_id):
		return
	var target: Dictionary = structures[target_id]
	if int(target.get("health", 0)) <= 0:
		return
	var damage_type := "default"
	if entities.has(attacker_id):
		damage_type = String((entities[attacker_id] as Dictionary).get("damage_type", "default"))
	var scalar := 1.0
	if String(target.get("structure_kind", "")) == "fortress":
		scalar = float(FORTRESS_ARMOR_SCALARS.get(damage_type, 0.25))
	var remainder_by_type: Dictionary = target.get("damage_remainders", {})
	var accumulated := float(remainder_by_type.get(damage_type, 0.0)) + float(maxi(0, amount)) * scalar
	var applied := floori(accumulated)
	remainder_by_type[damage_type] = accumulated - float(applied)
	target["damage_remainders"] = remainder_by_type
	if applied <= 0:
		return
	target["health"] = maxi(0, int(target["health"]) - applied)
	_emit_event("combat.hit_structure", attacker_id, target_id, {
		"raw_amount": maxi(0, amount),
		"applied_amount": applied,
		"damage_type": damage_type,
		"armor_scalar": scalar,
	})
	if int(target["health"]) == 0:
		var queue: Array = target.get("queue", [])
		# Queued costs stay spent, matching the deterministic no-refund contract.
		queue.clear()
		target["queue"] = queue
		var upgrade_queue: Array = target.get("upgrade_queue", [])
		upgrade_queue.clear()
		target["upgrade_queue"] = upgrade_queue
		_emit_event("structure.destroyed", attacker_id, target_id)


func _target_alive(target_id: int, target_kind: String) -> bool:
	if target_kind == "structure":
		return structures.has(target_id) and int((structures[target_id] as Dictionary).get("health", 0)) > 0
	return entities.has(target_id) and int((entities[target_id] as Dictionary).get("health", 0)) > 0


func _target_position(target_id: int, target_kind: String) -> Vector2:
	var row: Dictionary = structures.get(target_id, {}) if target_kind == "structure" else entities.get(target_id, {})
	return Vector2(row.get("position", Vector2.ZERO))


func _update_enemy_ai() -> void:
	if base_loop_enabled and not _enemy_ai_construction_attempted:
		_enemy_ai_construction_attempted = true
		if not _start_enemy_ai_farm():
			_enemy_ai_construction_resolved = true
	if base_loop_enabled and not _enemy_ai_construction_resolved and not _enemy_ai_construction_is_viable():
		_abandon_enemy_ai_construction()
		_enemy_ai_construction_resolved = true
	if base_loop_enabled and not _enemy_ai_construction_resolved:
		return
	if base_loop_enabled and tick_index % maxi(15, int(_rules.get("ai_queue_interval_ticks", 60))) == 0:
		for unit_type in (_ai_production_plan if not _ai_production_plan.is_empty() else AI_PRODUCTION_PLAN):
			var production_rule: Dictionary = _unit_production_rules.get(unit_type, {})
			if production_rule.is_empty():
				continue
			var producer := producer_id(ENEMY_TEAM, String(production_rule.get("producer_kind", "")))
			if producer != 0:
				queue_unit(ENEMY_TEAM, producer, unit_type)
	# Give the player one full Soldier production window before the first wave.
	# Economy/production still advance symmetrically during the preparation time.
	if base_loop_enabled and tick_index < maxi(0, int(_rules.get("ai_attack_delay_ticks", 0))):
		return
	var players := living_ids(PLAYER_TEAM)
	var player_fortress := fortress_id(PLAYER_TEAM) if base_loop_enabled else 0
	if players.is_empty() and player_fortress == 0:
		return
	for id in living_ids(ENEMY_TEAM):
		var row: Dictionary = entities[id]
		# MenPorters construct; they are not combat battalions and have no weapon.
		if bool(row.get("is_builder", false)):
			continue
		if int(row["target_id"]) != 0:
			continue
		var target_id := 0
		var target_kind := "battalion"
		if players.is_empty() and player_fortress != 0:
			target_id = player_fortress
			target_kind = "structure"
		else:
			target_id = players[0]
			var closest_distance := Vector2(row["position"]).distance_to(Vector2((entities[target_id] as Dictionary)["position"]))
			for candidate in players:
				var distance := Vector2(row["position"]).distance_to(Vector2((entities[candidate] as Dictionary)["position"]))
				if distance < closest_distance or (is_equal_approx(distance, closest_distance) and candidate < target_id):
					target_id = candidate
					closest_distance = distance
		var target_position := _target_position(target_id, target_kind)
		var target_distance := Vector2(row["position"]).distance_to(target_position)
		if target_kind == "structure":
			# Once no defending battalion remains, the known Fortress is the
			# strategic objective. Assign it before routing so the target's own
			# footprint is exempt and melee units can close to weapon range.
			if _assign_route(row, target_position):
				row["target_id"] = target_id
				row["target_kind"] = target_kind
				row["attack_windup"] = 0
				row["state"] = "run"
				_stamp_order_sequence([id])
				_emit_music("battle")
			continue
		var vision_range := maxf(float(row.get("attack_range", 1.15)), float(row.get("vision_range", 17.5)))
		if target_distance > vision_range:
			# Strategic AI can advance toward the opposing base, but it does not gain
			# a live combat target or attack animation through unexplored distance.
			# Advance toward the candidate we are actually trying to acquire. Routing
			# every unseen target to the fortress can strand the AI at the fortress
			# when a surviving battalion is outside its short infantry vision radius.
			# Once no player battalions remain, target_position is the fortress.
			var strategic_destination := target_position
			if (row["route"] as Array).is_empty() or Vector2(row.get("destination", row["position"])).distance_to(strategic_destination) > 1.0:
				if _assign_route(row, strategic_destination):
					row["target_id"] = 0
					row["target_kind"] = "battalion"
					row["attack_windup"] = 0
					row["state"] = "run"
					_stamp_order_sequence([id])
			continue
		if _assign_route(row, target_position):
			row["target_id"] = target_id
			row["target_kind"] = target_kind
			row["attack_windup"] = 0
			row["state"] = "run"
			_stamp_order_sequence([id])
			_emit_music("battle")


func _start_enemy_ai_farm() -> bool:
	var builder_ids: Array[int] = []
	for id in living_ids(ENEMY_TEAM):
		if bool((entities[id] as Dictionary).get("is_builder", false)):
			builder_ids.append(id)
	if builder_ids.is_empty():
		return false
	var builder_position := Vector2((entities[builder_ids[0]] as Dictionary).get("position", Vector2.ZERO))
	# A bounded clockwise search is deterministic and uses the same admission,
	# obstruction, route, cost, and construction path as a player MenPorter.
	for direction in [Vector2.UP, Vector2.RIGHT, Vector2.DOWN, Vector2.LEFT]:
		var candidate: Vector2 = builder_position + direction * 10.0
		var result := _issue_construct_for_team(ENEMY_TEAM, builder_ids, "farm", candidate)
		if bool(result.get("ok", false)):
			return true
	return false


func _enemy_ai_construction_is_viable() -> bool:
	for id in living_ids(ENEMY_TEAM):
		var builder: Dictionary = entities[id]
		if not bool(builder.get("is_builder", false)):
			continue
		var construction_id := int(builder.get("construction_id", 0))
		if construction_id != 0 and structures.has(construction_id) and int((structures[construction_id] as Dictionary).get("health", 0)) > 0:
			return true
	return false


func _abandon_enemy_ai_construction() -> void:
	for id in entity_ids():
		var builder: Dictionary = entities[id]
		if int(builder.get("team", -1)) != ENEMY_TEAM or not bool(builder.get("is_builder", false)) or int(builder.get("construction_id", 0)) == 0:
			continue
		var construction_id := int(builder.get("construction_id", 0))
		if structures.has(construction_id):
			var site: Dictionary = structures[construction_id]
			site["builder_id"] = 0
			if int(site.get("health", 0)) > 0 and float(site.get("construction_progress", 1.0)) < 1.0:
				site["health"] = 0
				_emit_event("structure.destroyed", 0, construction_id, {"reason": "construction-builder-unavailable"})
		builder["construction_id"] = 0
		builder["order_kind"] = ""
		_clear_pending_route(builder, true)
		if int(builder.get("health", 0)) > 0:
			builder["state"] = "idle"


func _build_route(from: Vector2, to: Vector2) -> Array[Vector2]:
	var result := _query_route(from, to)
	if not bool(result.get("valid", false)):
		return []
	var points: Array[Vector2] = []
	points.assign(result.get("points", []))
	return points


func _assign_route(row: Dictionary, destination: Vector2) -> bool:
	var result := _query_route(Vector2(row["position"]), destination)
	if not bool(result.get("valid", false)):
		last_route_rejection = String(result.get("reason", "route-rejected"))
		return false
	var points: Array[Vector2] = []
	points.assign(result.get("points", []))
	if points.is_empty():
		last_route_rejection = String(result.get("reason", ""))
		if last_route_rejection.is_empty():
			last_route_rejection = "empty-route"
		return false
	var cells: Array[Vector2i] = []
	cells.assign(result.get("cells", []))
	row["destination"] = destination
	row["route"] = points
	row["route_cells"] = cells
	row["route_ford"] = String(result.get("ford_name", ""))
	return not points.is_empty()


func _query_route(from: Vector2, to: Vector2) -> Dictionary:
	if route_provider != null and route_provider.has_method("query_route"):
		var value: Variant = route_provider.call("query_route", from, to)
		if typeof(value) == TYPE_DICTIONARY:
			return value as Dictionary
	# The non-retail fallback remains bounded and direct. The selected retail
	# slice cannot reach this branch because configuration requires a provider.
	return {"valid": true, "reason": "", "points": [to], "cells": [], "ford_name": ""}


func _resolve_victory() -> void:
	if base_loop_enabled:
		var player_fortress := fortress_id(PLAYER_TEAM)
		var enemy_fortress := fortress_id(ENEMY_TEAM)
		if player_fortress != 0 and int((structures[player_fortress] as Dictionary).get("health", 0)) <= 0:
			winner = ENEMY_TEAM
		elif enemy_fortress != 0 and int((structures[enemy_fortress] as Dictionary).get("health", 0)) <= 0:
			winner = PLAYER_TEAM
		else:
			return
	else:
		var players_alive := living_ids(PLAYER_TEAM)
		var enemies_alive := living_ids(ENEMY_TEAM)
		if players_alive.is_empty():
			winner = ENEMY_TEAM
		elif enemies_alive.is_empty():
			winner = PLAYER_TEAM
		else:
			return
	for id in living_ids(winner):
		var row: Dictionary = entities[id]
		row["target_id"] = 0
		_clear_pending_route(row, true)
		row["state"] = "victory"
	if winner == PLAYER_TEAM:
		_emit_event("match.victory", 0, 0)
		_emit_music("victory")
	else:
		_emit_event("match.defeat", 0, 0)
		_emit_music("defeat")


func _is_commandable(id: int) -> bool:
	return entities.has(id) and int((entities[id] as Dictionary)["team"]) == PLAYER_TEAM and int((entities[id] as Dictionary)["health"]) > 0 and winner == -1


func _is_living_player(id: int) -> bool:
	return entities.has(id) and int((entities[id] as Dictionary)["team"]) == PLAYER_TEAM and int((entities[id] as Dictionary)["health"]) > 0


func _prune_control_group(group: int) -> void:
	var retained: Array[int] = []
	for value: Variant in Array(control_groups.get(group, [])):
		var id := int(value)
		if _is_living_player(id) and not retained.has(id):
			retained.append(id)
	retained.sort()
	control_groups[group] = retained


func _stamp_order_sequence(ids: Array[int]) -> int:
	var sequence := _next_order_sequence
	_next_order_sequence += 1
	for id in ids:
		if entities.has(id):
			(entities[id] as Dictionary)["order_sequence"] = sequence
	return sequence


func _clear_pending_route(row: Dictionary, settle_destination: bool) -> void:
	row["route"] = []
	row["route_cells"] = []
	row["route_ford"] = ""
	if settle_destination:
		row["destination"] = Vector2(row["position"])


func _emit_music(state: String) -> void:
	if state == _music_state:
		return
	_music_state = state
	_emit_event("music.%s" % state, 0, 0)


func _emit_event(kind: String, entity_id: int, target_id: int, data: Dictionary = {}) -> void:
	var event := {
		"sequence": _next_event_sequence,
		"tick": tick_index,
		"kind": kind,
		"entity_id": entity_id,
		"target_id": target_id,
	}
	for key in data.keys():
		if not event.has(key):
			event[key] = data[key]
	events.append(event)
	for byte in JSON.stringify(event).to_utf8_buffer():
		_event_digest = ((_event_digest ^ int(byte)) * 16777619) & 0xFFFFFFFF
	_next_event_sequence += 1


func compact_consumed_events() -> int:
	if events.size() <= MAX_RETAINED_EVENT_HISTORY:
		return 0
	var retained: Array[Dictionary] = []
	var retained_by_kind: Dictionary = {}
	var retained_structure_targets: Dictionary = {}
	for event in events:
		var kind := String(event.get("kind", ""))
		var retention_key := kind
		var retention_limit := MAX_RETAINED_EVENTS_PER_KIND
		if kind == "combat.hit_structure" or kind == "structure.destroyed":
			if int(retained_structure_targets.get(kind, 0)) >= MAX_RETAINED_STRUCTURE_TARGETS_PER_KIND:
				continue
			retention_key = "%s:%d" % [kind, int(event.get("target_id", 0))]
			retention_limit = 1
		var retained_count := int(retained_by_kind.get(retention_key, 0))
		if retained_count >= retention_limit:
			continue
		retained.append(event)
		retained_by_kind[retention_key] = retained_count + 1
		if kind == "combat.hit_structure" or kind == "structure.destroyed":
			retained_structure_targets[kind] = int(retained_structure_targets.get(kind, 0)) + 1
		if retained.size() >= MAX_RETAINED_EVENT_HISTORY:
			break
	var removed := events.size() - retained.size()
	events = retained
	return removed


func state_snapshot() -> Dictionary:
	var rows: Array[Dictionary] = []
	for id in entity_ids():
		var entity_row: Dictionary = entities[id]
		var position := Vector2(entity_row["position"])
		var destination := Vector2(entity_row["destination"])
		var route_rows: Array[Array] = []
		for value: Variant in Array(entity_row.get("route", [])):
			var point := Vector2(value)
			route_rows.append([snappedf(point.x, 0.001), snappedf(point.y, 0.001)])
		var route_cell_rows: Array[Array] = []
		for value: Variant in Array(entity_row.get("route_cells", [])):
			var cell := Vector2i(value)
			route_cell_rows.append([cell.x, cell.y])
		rows.append({
			"id": id,
			"team": int(entity_row["team"]),
			"name": String(entity_row.get("name", "")),
			"object_id": String(entity_row.get("object_id", SOLDIER_OBJECT_ID)),
			"unit_type": String(entity_row.get("unit_type", SOLDIER_HORDE_ID)),
			"command_points": int(entity_row.get("command_points", 0)),
			"position": [snappedf(position.x, 0.001), snappedf(position.y, 0.001)],
			"facing": [snappedf(float((entity_row.get("facing", Vector2.RIGHT) as Vector2).x), 0.001), snappedf(float((entity_row.get("facing", Vector2.RIGHT) as Vector2).y), 0.001)],
			"destination": [snappedf(destination.x, 0.001), snappedf(destination.y, 0.001)],
			"state": String(entity_row["state"]),
			"target": int(entity_row["target_id"]),
			"target_kind": String(entity_row.get("target_kind", "battalion")),
			"health": int(entity_row["health"]),
			"member_maximum_health": int(entity_row.get("member_maximum_health", 0)),
			"member_health": Array(entity_row.get("member_health", [])).duplicate(),
			"attack_cooldown": int(entity_row.get("attack_cooldown", 0)),
			"attack_windup": int(entity_row.get("attack_windup", 0)),
			"attack_sequence": int(entity_row.get("attack_sequence", 0)),
			"continuous_fire_count": int(entity_row.get("continuous_fire_count", 0)),
			"continuous_fire_expiration_tick": int(entity_row.get("continuous_fire_expiration_tick", -1)),
			"member_attack_tokens": Array(entity_row.get("member_attack_tokens", [])).duplicate(),
			"member_attack_start_ticks": Array(entity_row.get("member_attack_start_ticks", [])).duplicate(),
			"member_attack_hit_ticks": Array(entity_row.get("member_attack_hit_ticks", [])).duplicate(),
			"attack_range": snappedf(float(entity_row.get("attack_range", 0.0)), 0.001),
			"vision_range": snappedf(float(entity_row.get("vision_range", 0.0)), 0.001),
			"damage_type": String(entity_row.get("damage_type", "")),
			"production_producer_id": int(entity_row.get("production_producer_id", 0)),
			"production_exit_start_tick": int(entity_row.get("production_exit_start_tick", -1)),
			"production_exit_duration_ticks": int(entity_row.get("production_exit_duration_ticks", 0)),
			"production_exit_progress": snappedf(float(entity_row.get("production_exit_progress", 1.0)), 0.001),
			"route": route_rows,
			"route_cells": route_cell_rows,
			"route_ford": String(entity_row.get("route_ford", "")),
			"order_sequence": int(entity_row.get("order_sequence", 0)),
		})
	var structure_rows: Array[Dictionary] = []
	for id in structure_ids():
		var structure_row: Dictionary = structures[id]
		var position := Vector2(structure_row.get("position", Vector2.ZERO))
		var rally := Vector2(structure_row.get("rally", Vector2.ZERO))
		var queue_rows: Array[Dictionary] = []
		for item_value in Array(structure_row.get("queue", [])):
			if typeof(item_value) != TYPE_DICTIONARY:
				continue
			var item: Dictionary = item_value
			queue_rows.append({
				"unit_type": String(item.get("unit_type", "")),
				"cost": int(item.get("cost", 0)),
				"command_points": int(item.get("command_points", 0)),
				"queued_tick": int(item.get("queued_tick", 0)),
				"start_tick": int(item.get("start_tick", item.get("queued_tick", 0))),
				"duration_ticks": int(item.get("duration_ticks", 0)),
				"complete_tick": int(item.get("complete_tick", 0)),
			})
		var upgrade_queue_rows: Array[Dictionary] = []
		for item_value in Array(structure_row.get("upgrade_queue", [])):
			if typeof(item_value) != TYPE_DICTIONARY:
				continue
			var item: Dictionary = item_value
			upgrade_queue_rows.append({
				"upgrade_id": String(item.get("upgrade_id", "")),
				"cost": int(item.get("cost", 0)),
				"queued_tick": int(item.get("queued_tick", 0)),
				"duration_ticks": int(item.get("duration_ticks", 0)),
				"complete_tick": int(item.get("complete_tick", 0)),
				"cancelable": bool(item.get("cancelable", false)),
			})
		structure_rows.append({
			"id": id,
			"team": int(structure_row.get("team", -1)),
			"kind": String(structure_row.get("structure_kind", "")),
			"position": [snappedf(position.x, 0.001), snappedf(position.y, 0.001)],
			"rally": [snappedf(rally.x, 0.001), snappedf(rally.y, 0.001)],
			"health": int(structure_row.get("health", 0)),
			"maximum_health": int(structure_row.get("maximum_health", 0)),
			"construction_progress": snappedf(float(structure_row.get("construction_progress", 0.0)), 0.001),
			"level": int(structure_row.get("level", 1)),
			"completed_upgrades": Array(structure_row.get("completed_upgrades", [])).duplicate(),
			"damage_remainders": (structure_row.get("damage_remainders", {}) as Dictionary).duplicate(true),
			"queue": queue_rows,
			"upgrade_queue": upgrade_queue_rows,
		})
	var gate_rows: Array[Dictionary] = []
	for gate in ford_gates:
		var edge_a := Vector2(gate.get("edge_a", Vector2.ZERO))
		var edge_b := Vector2(gate.get("edge_b", Vector2.ZERO))
		gate_rows.append({
			"name": String(gate.get("name", "")),
			"edge_a": [snappedf(edge_a.x, 0.001), snappedf(edge_a.y, 0.001)],
			"edge_b": [snappedf(edge_b.x, 0.001), snappedf(edge_b.y, 0.001)],
		})
	var completed_hero_identities: Array[String] = []
	for identity_value in _completed_hero_identities.keys():
		completed_hero_identities.append(String(identity_value))
	completed_hero_identities.sort()
	return {
		"tick": tick_index,
		"winner": winner,
		"base_loop_enabled": base_loop_enabled,
		"resources": [resources_for_team(PLAYER_TEAM), resources_for_team(ENEMY_TEAM)],
		"command_points": [command_points_for_team(PLAYER_TEAM), command_points_for_team(ENEMY_TEAM)],
		"command_point_cap": command_point_cap,
		"next_dynamic_ids": [int(_next_dynamic_id.get(PLAYER_TEAM, 10)), int(_next_dynamic_id.get(ENEMY_TEAM, 110))],
		"selected": selected_ids.duplicate(),
		"control_groups": control_groups_snapshot(),
		"next_order_sequence": _next_order_sequence,
		"music": _music_state,
		"source_map_configured": source_map_configured,
		"ford_gates": gate_rows,
		"completed_hero_identities": completed_hero_identities,
		"entities": rows,
		"structures": structure_rows,
		"next_event_sequence": _next_event_sequence,
		"event_digest": _event_digest,
	}


func state_signature() -> String:
	var value: int = 0x811C9DC5
	for byte in JSON.stringify(state_snapshot()).to_utf8_buffer():
		value = ((value ^ int(byte)) * 16777619) & 0xFFFFFFFF
	return "%08X" % value
