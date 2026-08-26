extends RefCounted
## Persistence core extracted from retail_slice_sim.gd (Q81 strangler-fig
## extraction #6): authoritative state assembly, canonical hashing,
## snapshot/restore. Verbatim move, compiler-guided sim. prefixes,
## pin-verified byte-identical.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()


func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)


func state_signature() -> String:
	var value: int = 0x811C9DC5
	for byte in JSON.stringify(sim.state_snapshot()).to_utf8_buffer():
		value = ((value ^ int(byte)) * 16777619) & 0xFFFFFFFF
	return "%08X" % value


func state_hash() -> String:
	var dynamic_state := authoritative_state()
	var static_state: Dictionary = {}
	for key in state_hash_static_keys():
		static_state[key] = dynamic_state[key]
		dynamic_state.erase(key)
	if sim._state_hash_static_digest.is_empty():
		var static_context := HashingContext.new()
		static_context.start(HashingContext.HASH_SHA256)
		static_context.update(var_to_bytes(canonicalize(static_state)))
		sim._state_hash_static_digest = static_context.finish()
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(sim._state_hash_static_digest)
	context.update(var_to_bytes(canonicalize(dynamic_state)))
	return context.finish().hex_encode()


func snapshot() -> PackedByteArray:
	# Event ordering is continuation metadata, not gameplay state: include it in
	# the restorable envelope without moving the owner-signed authoritative hash.
	var state := authoritative_state()
	state["next_event_sequence"] = sim._next_event_sequence
	return var_to_bytes(state)


func restore(bytes: PackedByteArray) -> bool:
	if bytes.is_empty():
		return false
	var decoded: Variant = bytes_to_var(bytes)
	if typeof(decoded) != TYPE_DICTIONARY:
		return false
	var state := decoded as Dictionary
	for required_key in ["tick_index", "entities", "structures", "team_resources", "pending_commands", "next_dynamic_id"]:
		if not state.has(required_key):
			return false
	restore_authoritative_state(state)
	sim.selected_ids.clear()
	sim.reset_control_groups()
	sim.events.clear()
	# Tick-derived, not authoritative: see `sim._member_fire_ticks`. A restored member
	# reports no FIRING_* until its next release rather than a guessed window.
	sim._member_fire_ticks.clear()
	sim.last_command_result = null
	sim._state_hash_static_digest.clear()
	return true


func state_hash_static_keys() -> Array[String]:
	return [
		"source_map_configured", "ford_gates", "source_player_starts", "playable_outline",
		"spawn_positions", "home_layout", "rules", "unit_production_rules",
		"production_unit_order", "ai_production_plan", "unit_damage_types",
		"unit_damage_components", "unit_armor",
		"unit_weapon_upgrades", "structure_armor", "spawn_roster", "structure_kinds",
		"seed_structure_kinds", "structure_max_health", "structure_build_rules",
		"unit_prerequisites", "structure_upgrade_contracts", "structure_upgrade_effects",
		"compiled_research_kinds", "unit_upgrade_commands", "unit_level_upgrades", "unit_banner_carriers",
		"spellbook_ready", "spellbook_document", "spellbook_powers", "spellbook_order",
		"spellbook_sciences", "spellbook_intrinsic", "science_to_power",
		"expansion_build_rules", "unit_ability_rules", "unit_experience_rules",
	]


func authoritative_state() -> Dictionary:
	# The FULL rules dictionary is hashed, faction_manifest included. A Q80
	# round excluded the raw manifest to keep pin fixtures plumbing-stable, but
	# the sim reads the raw manifest at runtime (producer_kind_registry at
	# :5619/:5891, per-team manifest fallback at :1185) — excluding it let two
	# lockstep peers with different manifests agree on setup hashes. Config
	# in the hash is a lockstep feature, not fragility; pin fixtures carry the
	# cost as a conscious, ledgered re-mint instead.
	var state := {
		"tick_index": sim.tick_index,
		"winner": sim.winner,
		"ai_enabled": sim.ai_enabled,
		"entities": sim.entities,
		"structures": sim.structures,
		"team_resources": sim.team_resources,
		"team_command_points": sim.team_command_points,
		"command_point_cap": sim.command_point_cap,
		"base_loop_enabled": sim.base_loop_enabled,
		"source_map_configured": sim.source_map_configured,
		"ford_gates": sim.ford_gates,
		"source_player_starts": sim.source_player_starts,
		"playable_outline": sim.playable_outline,
		"spawn_positions": sim._spawn_positions,
		"home_layout": sim._home_layout,
		"rules": sim._rules,
		"unit_production_rules": sim._unit_production_rules,
		"completed_hero_identities": sim._completed_hero_identities,
		"production_unit_order": sim._production_unit_order,
		"ai_production_plan": sim._ai_production_plan,
		"unit_damage_types": sim._unit_damage_types,
		"unit_damage_components": sim._unit_damage_components,
		"unit_armor": sim._unit_armor,
		"unit_weapon_upgrades": sim._unit_weapon_upgrades,
		"structure_armor": sim._structure_armor,
		"spawn_roster": sim._spawn_roster,
		"structure_kinds": sim._structure_kinds,
		"seed_structure_kinds": sim._seed_structure_kinds,
		"structure_max_health": sim._structure_max_health,
		"structure_build_rules": sim._structure_build_rules,
		"unit_prerequisites": sim._unit_prerequisites,
		"structure_upgrade_contracts": sim._structure_upgrade_contracts,
		"structure_upgrade_effects": sim._structure_upgrade_effects,
		"compiled_research_kinds": sim._compiled_research_kinds,
		"unit_upgrade_commands": sim._unit_upgrade_commands,
		"unit_level_upgrades": sim._unit_level_upgrades,
		"unit_banner_carriers": sim._unit_banner_carriers,
		"next_dynamic_id": sim._next_dynamic_id,
		"next_dynamic_structure_id": sim._next_dynamic_structure_id,
		"next_order_sequence": sim._next_order_sequence,
		"team_ai_state": sim._team_ai_state,
		"team_upgrades": sim.team_upgrades,
		"team_power_points": sim.team_power_points,
		"purchased_powers": sim.purchased_powers,
		"kills_toward_power_point": sim._kills_toward_power_point,
		"spellbook_ready": sim._spellbook_ready,
		"spellbook_document": sim._spellbook_document,
		"spellbook_powers": sim._spellbook_powers,
		"spellbook_order": sim._spellbook_order,
		"spellbook_sciences": sim._spellbook_sciences,
		"spellbook_intrinsic": sim._spellbook_intrinsic,
		"science_to_power": sim._science_to_power,
		"team_sciences": sim._team_sciences,
		"power_cooldown_until": sim._power_cooldown_until,
		"staged_purchases": sim._staged_purchases,
		"team_spellbooks": sim._team_spellbooks,
		"clock_paused": sim.clock_paused,
		"pending_power_effects": sim._pending_power_effects,
		"active_groves": sim._active_groves,
		"summon_despawn_ticks": sim._summon_despawn_ticks,
		"expansion_pads": sim.expansion_pads,
		"expansion_build_rules": sim._expansion_build_rules,
		"next_expansion_structure_id": sim._next_expansion_structure_id,
		"build_plots_only": sim.build_plots_only,
		"build_plots": sim.build_plots,
		"has_hero_units": sim._has_hero_units,
		"unit_ability_rules": sim._unit_ability_rules,
		"unit_experience_rules": sim._unit_experience_rules,
		"pending_commands": sim._pending_commands,
	}
	if sim.scenario_map_placements_enabled:
		state["scenario_map_placements_enabled"] = true
		state["scenario_map_placements"] = sim._scenario_map_placements
		state["next_scenario_unit_id"] = sim._next_scenario_unit_id
		state["next_scenario_structure_id"] = sim._next_scenario_structure_id
	# New bounty/passive state is byte-inert for legacy matches. Once authored
	# content or a cast makes it relevant, it becomes ordinary save/hash state.
	if not sim._structure_bounty_values.is_empty():
		state["structure_bounty_values"] = sim._structure_bounty_values
	# Passive scenario props are placement/presentation state. Empty remains
	# absent so every match without a neutral-prop pack keeps its legacy hash.
	if not sim.scenario_props.is_empty():
		state["scenario_props"] = sim.scenario_props
	if not sim.scenario_bezier_presentation_requests.is_empty():
		state["scenario_bezier_presentation_requests"] = sim.scenario_bezier_presentation_requests
		state["next_scenario_prop_id"] = sim._next_scenario_prop_id
	var has_consumed_nonpressable := false
	for consumed_value in sim._consumed_nonpressable_powers.values():
		if not (consumed_value as Dictionary).is_empty():
			has_consumed_nonpressable = true
			break
	if has_consumed_nonpressable:
		state["consumed_nonpressable_powers"] = sim._consumed_nonpressable_powers
	var has_scavenger := false
	for percent_value in sim._scavenger_bounty_percent.values():
		if float(percent_value) != 0.0:
			has_scavenger = true
			break
	if has_scavenger:
		state["scavenger_bounty_percent"] = sim._scavenger_bounty_percent
	# EMPTY-IS-ABSENT (the unpackable-bases contract below): the castle-fixture
	# lane only appears in the hashed state when a match opts in, so the frozen
	# cross-platform pin and every legacy runner stay byte-identical.
	if sim.castle_fixtures_enabled:
		state["castle_fixtures_enabled"] = true
		state["castle_fixture_placements"] = sim._castle_fixture_placements
		state["next_castle_fixture_id"] = sim._next_castle_fixture_id
	# EMPTY-IS-ABSENT, deliberately: a match with no configured base flags must
	# contribute NOTHING for this subsystem, so the frozen cross-platform state
	# pin keeps proving the addition is inert by default. An unconditional key
	# here would re-mint the pin for every scenario that never touches bases.
	if not sim.unpackable_bases.is_empty():
		state["unpackable_bases"] = sim.unpackable_bases
	if not sim._summon_aura_source_ids.is_empty():
		state["summon_aura_source_ids"] = sim._summon_aura_source_ids
	# EMPTY-IS-ABSENT, same contract as above: a match in which nobody casts a
	# reveal/field ping must contribute nothing, so the frozen cross-platform pin
	# stays valid for every scenario that never touches this lane.
	if not sim._field_pings.is_empty():
		state["field_pings"] = sim._field_pings
	# Same EMPTY-IS-ABSENT contract for the weather lane: a match in which
	# nobody casts Darkness or Freezing Rain contributes zero bytes.
	if not sim._weather_effects.is_empty():
		state["weather_effects"] = sim._weather_effects
	# These selected-pack contracts are absent in legacy/default scenarios. Keep
	# them out of the byte stream unless configured so unrelated state pins remain
	# stable, while selected retail matches still hash and snapshot the contracts.
	if not sim._banner_respawn_ticks_by_object.is_empty():
		state["banner_respawn_ticks_by_object"] = sim._banner_respawn_ticks_by_object
	if not sim._castle_behavior_by_source.is_empty():
		state["castle_behavior_by_source"] = sim._castle_behavior_by_source
	if not sim._cah_award_contracts.is_empty():
		state["cah_award_contracts"] = sim._cah_award_contracts
	if not sim._cah_award_tallies.is_empty():
		state["cah_award_tallies"] = sim._cah_award_tallies
	if not sim.cah_award_results.is_empty():
		state["cah_award_results"] = sim.cah_award_results
	# Same discipline for the map named-object namespace: a match that never
	# declares one contributes zero bytes (see the store's block comment).
	if not sim.map_named_object_namespace.is_empty():
		state["map_named_object_namespace"] = sim.map_named_object_namespace
	# Same discipline for the script unit references: a match whose scripts
	# never bind one contributes zero bytes (see the store's block comment).
	if not sim.script_unit_references.is_empty():
		state["script_unit_references"] = sim.script_unit_references
	if not sim.script_entity_references.is_empty():
		state["script_entity_references"] = sim.script_entity_references
	if not sim.create_object_die_pending.is_empty():
		state["create_object_die_pending"] = sim.create_object_die_pending
	if not sim.physics_objects.is_empty() or sim._next_physics_object_id != 50000:
		state["physics_objects"] = sim.physics_objects
		state["next_physics_object_id"] = sim._next_physics_object_id
	if not sim.projectiles.is_empty() or sim._next_projectile_id != 70000:
		state["projectiles"] = sim.projectiles
		state["next_projectile_id"] = sim._next_projectile_id
	if not sim.pickup_objects.is_empty() or sim._next_pickup_object_id != 60000:
		state["pickup_objects"] = sim.pickup_objects
		state["next_pickup_object_id"] = sim._next_pickup_object_id
	if not sim._shared_ability_cooldowns.is_empty():
		state["shared_ability_cooldowns"] = sim._shared_ability_cooldowns
	if not sim.respawn_schedules.is_empty():
		state["respawn_schedules"] = sim.respawn_schedules
	if sim.parity != null:
		var parity_state: Dictionary = sim.parity.to_state()
		if not parity_state.is_empty():
			state["parity"] = parity_state
	# And for the script-built OBJECT_TYPE_LIST stores: mutable match state
	# (retail persists them in save games), zero bytes until a script builds
	# one (see the store's block comment).
	if not sim.script_object_type_lists.is_empty():
		state["script_object_type_lists"] = sim.script_object_type_lists
	# Named team membership/flags are outcome-bearing. Empty remains absent so
	# scriptless matches retain their frozen hash.
	var script_team_view = sim._script_team_state_view()
	if not script_team_view.is_empty():
		state["script_teams"] = script_team_view
	if not sim.building_permissions_by_team.is_empty():
		state["building_permissions_by_team"] = sim.building_permissions_by_team
	if not sim.command_point_overrides_by_team.is_empty():
		state["command_point_overrides_by_team"] = sim.command_point_overrides_by_team
	# And for team behavior state (TEAM_STATE + custom-state token sets):
	# retail save-persists m_state (Team::xfer), the conditions that gate the
	# AI attack loops read it, and a peer adopting a snapshot must answer
	# TEAM_STATE_IS exactly as the peer that wrote the state. Zero bytes until
	# a script writes one (see the store's block comment).
	if not sim.team_behavior_states.is_empty():
		state["team_behavior_states"] = sim.team_behavior_states
	# Sequential-script heads are outcome-bearing AI behavior queues. Empty is
	# absent so scriptless matches keep frozen hash pins.
	if not sim.sequential_script_queues.is_empty():
		state["sequential_script_queues"] = sim.sequential_script_queues
	if not sim.production_controls_by_team.is_empty():
		state["production_controls_by_team"] = sim.production_controls_by_team
	if not sim.tech_buildability.is_empty():
		state["tech_buildability"] = sim.tech_buildability
	if not sim.script_team_references.is_empty():
		state["script_team_references"] = sim.script_team_references
	if not sim.player_progression.is_empty():
		state["player_progression"] = sim.player_progression
	if not sim.player_economy_extras.is_empty():
		state["player_economy_extras"] = sim.player_economy_extras
	if not sim.player_diplomacy_overrides.is_empty():
		state["player_diplomacy_overrides"] = sim.player_diplomacy_overrides
	if not sim.script_event_counts.is_empty():
		state["script_event_counts"] = sim.script_event_counts
	if not sim.containment.is_empty():
		state["containment"] = sim.containment
	if not sim.entity_container.is_empty():
		state["entity_container"] = sim.entity_container
	if not sim.script_areas.is_empty():
		state["script_areas"] = sim.script_areas
	if not sim.script_waypoints.is_empty():
		state["script_waypoints"] = sim.script_waypoints
	if not sim.script_waypoint_paths.is_empty():
		state["script_waypoint_paths"] = sim.script_waypoint_paths
	if not sim.team_created_edge.is_empty():
		state["team_created_edge"] = sim.team_created_edge
	if not sim.match_script_flags.is_empty():
		state["match_script_flags"] = sim.match_script_flags
	if not sim.attack_priority_names.is_empty():
		state["attack_priority_names"] = sim.attack_priority_names
	if not sim.script_surface_bag.is_empty():
		state["script_surface_bag"] = sim.script_surface_bag
	# Audio selector choice is deterministic presentation state. Keep it absent
	# until the first typed choice so matches without these client modules retain
	# their frozen hashes, while save/restore resumes the same weighted sequence.
	if sim._typed_audio_roll_sequence > 0:
		state["typed_audio_roll_sequence"] = sim._typed_audio_roll_sequence
	# Historical objective state is match state and hash-visible. Empty is
	# absent so scenarios that never field a hero retain their frozen pins.
	if not sim._hero_peak_ranks_by_team.is_empty():
		state["hero_peak_ranks_by_team"] = sim._hero_peak_ranks_by_team
	# And for the logic random stream: the six generator words are the entire
	# stream state (retail's theGameLogicSeed rides save games and its CRC is
	# sync-checked - GetGameLogicRandomSeedCRC). Zero bytes until the first
	# draw, so a scriptless match leaves the frozen pin untouched; an
	# adopting peer receives the words and continues the identical sequence.
	if not sim._logic_random_state.is_empty():
		state["logic_random_state"] = sim._logic_random_state
	# And for the script interpreter's own memory: hashed and serialized
	# through its canonical view (zero counters/false flags pruned, every
	# level sorted), so an untouched match contributes zero bytes and state
	# returned to pristine values returns to the pristine hash exactly.
	var script_env_view = sim._script_env_state_view()
	if not script_env_view.is_empty():
		state["script_env_state"] = script_env_view
	return state


func restore_authoritative_state(state: Dictionary) -> void:
	sim.tick_index = int(state["tick_index"])
	sim.winner = int(state["winner"])
	sim.ai_enabled = bool(state["ai_enabled"])
	sim.entities = state["entities"]
	sim.structures = state["structures"]
	sim.scenario_props = state.get("scenario_props", {})
	sim.scenario_bezier_presentation_requests = state.get(
		"scenario_bezier_presentation_requests", []
	)
	sim._next_scenario_prop_id = int(state.get("next_scenario_prop_id", 400000))
	sim._note_structure_table_mutation()
	# The structure table was just replaced wholesale; the id-keyed footprint
	# memo describes the old one.
	sim._structure_footprint_radius_cache.clear()
	sim.team_resources = state["team_resources"]
	sim.team_command_points = state["team_command_points"]
	sim.command_point_cap = int(state["command_point_cap"])
	sim.base_loop_enabled = bool(state["base_loop_enabled"])
	sim.source_map_configured = bool(state["source_map_configured"])
	sim.ford_gates = state["ford_gates"]
	sim.source_player_starts = state["source_player_starts"]
	sim.playable_outline = state["playable_outline"]
	sim._spawn_positions = state["spawn_positions"]
	sim._home_layout = state["home_layout"]
	sim._rules = state["rules"]
	sim._configure_death_weapon_rules_from_rules()
	sim.ring_mechanic_enabled = bool(sim._rules.get("allow_ring_heroes", false))
	sim._configure_ring_mechanic_contract()
	sim._unit_production_rules = state["unit_production_rules"]
	sim._completed_hero_identities = state["completed_hero_identities"]
	sim._production_unit_order = state["production_unit_order"]
	sim._ai_production_plan = state["ai_production_plan"]
	sim._unit_damage_types = state["unit_damage_types"]
	sim._unit_damage_components = state["unit_damage_components"]
	sim._unit_armor = state["unit_armor"]
	sim._unit_weapon_upgrades = state["unit_weapon_upgrades"]
	sim._structure_armor = state["structure_armor"]
	sim._spawn_roster = state["spawn_roster"]
	sim._structure_kinds = state["structure_kinds"]
	sim._seed_structure_kinds = state["seed_structure_kinds"]
	sim._structure_max_health = state["structure_max_health"]
	sim._structure_bounty_values = state.get("structure_bounty_values", {})
	sim._structure_build_rules = state["structure_build_rules"]
	sim._unit_prerequisites = state["unit_prerequisites"]
	sim._structure_upgrade_contracts = state["structure_upgrade_contracts"]
	sim._structure_upgrade_effects = state["structure_upgrade_effects"]
	sim._compiled_research_kinds = state["compiled_research_kinds"]
	sim._unit_upgrade_commands = state["unit_upgrade_commands"]
	sim._unit_level_upgrades = state["unit_level_upgrades"]
	sim._unit_banner_carriers = state.get("unit_banner_carriers", {})
	sim._banner_respawn_ticks_by_object = state.get("banner_respawn_ticks_by_object", {})
	sim._castle_behavior_by_source = state.get("castle_behavior_by_source", {})
	sim._cah_award_contracts = state.get("cah_award_contracts", {})
	sim._cah_award_tallies = state.get("cah_award_tallies", {})
	sim.cah_award_results = state.get("cah_award_results", {})
	sim._next_dynamic_id = state["next_dynamic_id"]
	sim._next_dynamic_structure_id = int(state["next_dynamic_structure_id"])
	sim._next_event_sequence = int(state.get("next_event_sequence", 1))
	sim._next_order_sequence = int(state["next_order_sequence"])
	sim._typed_audio_roll_sequence = int(state.get("typed_audio_roll_sequence", 0))
	sim._team_ai_state = state.get("team_ai_state", {})
	sim.team_upgrades = state["team_upgrades"]
	sim.team_power_points = state["team_power_points"]
	sim.purchased_powers = state["purchased_powers"]
	sim._kills_toward_power_point = state["kills_toward_power_point"]
	sim._spellbook_ready = bool(state["spellbook_ready"])
	sim._spellbook_document = state["spellbook_document"]
	sim._spellbook_command_points_upgrade = (
		(
			(sim._spellbook_document.get("registration", {}) as Dictionary).get(
				"spellBook", {}
			) as Dictionary
		).get("commandPointsUpgrade", {}) as Dictionary
	).duplicate(true)
	sim._spellbook_powers = state["spellbook_powers"]
	sim._spellbook_order = state["spellbook_order"]
	sim._spellbook_sciences = state["spellbook_sciences"]
	sim._spellbook_intrinsic = state["spellbook_intrinsic"]
	sim._science_to_power = state["science_to_power"]
	sim._team_sciences = state["team_sciences"]
	sim._power_cooldown_until = state["power_cooldown_until"]
	sim._consumed_nonpressable_powers = state.get("consumed_nonpressable_powers", sim._seed_team_map({}))
	sim._scavenger_bounty_percent = state.get("scavenger_bounty_percent", sim._seed_team_map(0.0))
	sim._staged_purchases = state["staged_purchases"]
	sim._team_spellbooks = state.get("team_spellbooks", {})
	sim.clock_paused = bool(state["clock_paused"])
	sim._pending_power_effects = state["pending_power_effects"]
	sim._active_groves = state["active_groves"]
	sim._summon_despawn_ticks = state["summon_despawn_ticks"]
	sim._summon_aura_source_ids = state.get("summon_aura_source_ids", {})
	var adopted_pings: Array[Dictionary] = []
	for ping_value in Array(state.get("field_pings", [])):
		if typeof(ping_value) == TYPE_DICTIONARY:
			adopted_pings.append(ping_value as Dictionary)
	sim._field_pings = adopted_pings
	var adopted_weather: Array[Dictionary] = []
	for weather_value in Array(state.get("weather_effects", [])):
		if typeof(weather_value) == TYPE_DICTIONARY:
			adopted_weather.append(weather_value as Dictionary)
	sim._weather_effects = adopted_weather
	sim._migrate_restored_weather_sources()
	sim.expansion_pads = state["expansion_pads"]
	sim._expansion_build_rules = state["expansion_build_rules"]
	sim._next_expansion_structure_id = int(state["next_expansion_structure_id"])
	sim.build_plots_only = bool(state.get("build_plots_only", false))
	sim.build_plots = state.get("build_plots", {})
	sim._has_hero_units = bool(state["has_hero_units"])
	sim._unit_ability_rules = state["unit_ability_rules"]
	sim._unit_experience_rules = state["unit_experience_rules"]
	sim._pending_commands = state["pending_commands"]
	# Absent when empty by construction (empty-is-absent hash discipline).
	sim.unpackable_bases = state.get("unpackable_bases", {})
	sim.map_named_object_namespace = state.get("map_named_object_namespace", {})
	sim.script_unit_references = state.get("script_unit_references", {})
	sim.script_entity_references = state.get("script_entity_references", {})
	sim.create_object_die_pending = state.get("create_object_die_pending", [])
	sim.physics_objects = state.get("physics_objects", {})
	sim._next_physics_object_id = int(state.get("next_physics_object_id", 50000))
	sim.projectiles = state.get("projectiles", {})
	sim._next_projectile_id = int(state.get("next_projectile_id", 70000))
	sim.pickup_objects = state.get("pickup_objects", {})
	sim._next_pickup_object_id = int(state.get("next_pickup_object_id", 60000))
	sim._shared_ability_cooldowns = state.get("shared_ability_cooldowns", {})
	sim.respawn_schedules = state.get("respawn_schedules", {})
	sim._ensure_parity()
	if state.has("parity"):
		sim.parity.from_state(state.get("parity", {}) as Dictionary)
	else:
		sim.parity.clear()
	sim.script_object_type_lists = state.get("script_object_type_lists", {})
	var restored_script_teams: Dictionary = state.get("script_teams", {})
	for script_team_name in sim.script_teams.keys():
		var configured := sim.script_teams[script_team_name] as Dictionary
		sim.script_teams[script_team_name] = sim._script_team_definition(configured)
	for script_team_name in restored_script_teams.keys():
		sim.script_teams[script_team_name] = (restored_script_teams[script_team_name] as Dictionary).duplicate(true)
	sim.building_permissions_by_team = state.get("building_permissions_by_team", {})
	sim.command_point_overrides_by_team = state.get("command_point_overrides_by_team", {})
	sim.team_behavior_states = state.get("team_behavior_states", {})
	sim.sequential_script_queues = state.get("sequential_script_queues", {})
	sim.production_controls_by_team = state.get("production_controls_by_team", {})
	sim.tech_buildability = state.get("tech_buildability", {})
	sim.script_team_references = state.get("script_team_references", {})
	sim.player_progression = state.get("player_progression", {})
	sim.player_economy_extras = state.get("player_economy_extras", {})
	sim.player_diplomacy_overrides = state.get("player_diplomacy_overrides", {})
	sim.script_event_counts = state.get("script_event_counts", {})
	sim.containment = state.get("containment", {})
	sim.entity_container = state.get("entity_container", {})
	sim.script_areas = state.get("script_areas", {})
	sim.script_waypoints = state.get("script_waypoints", {})
	sim.script_waypoint_paths = state.get("script_waypoint_paths", {})
	sim.team_created_edge = state.get("team_created_edge", {})
	sim.match_script_flags = state.get("match_script_flags", {})
	sim.attack_priority_names = state.get("attack_priority_names", {})
	sim.script_surface_bag = state.get("script_surface_bag", {})
	sim._hero_peak_ranks_by_team = state.get("hero_peak_ranks_by_team", {})
	# Absent when the minter never drew (empty-is-absent): the adopter then
	# also derives the words from the shared rules seed on ITS first draw.
	sim._logic_random_state = state.get("logic_random_state", [])
	# IN PLACE, never rebind: attached SageScriptEnvs share this dictionary by
	# reference (see the sim.script_env_state block comment), so a rebind here
	# would silently detach every live script environment from the boundary.
	sim.script_env_state.clear()
	sim.script_env_state.merge(state.get("script_env_state", {}))
	sim.scenario_map_placements_enabled = bool(state.get("scenario_map_placements_enabled", false))
	sim._scenario_map_placements = state.get("scenario_map_placements", [])
	sim._next_scenario_unit_id = int(state.get("next_scenario_unit_id", sim.SCENARIO_UNIT_FIRST_ID))
	sim._next_scenario_structure_id = int(state.get("next_scenario_structure_id", sim.SCENARIO_STRUCTURE_FIRST_ID))
	sim.castle_fixtures_enabled = bool(state.get("castle_fixtures_enabled", false))
	sim._castle_fixture_placements = state.get("castle_fixture_placements", [])
	sim._next_castle_fixture_id = int(state.get("next_castle_fixture_id", sim.CASTLE_FIXTURE_FIRST_ID))
	# Reconstruct the derived team registry + per-team manifest aliases from the
	# restored authoritative dicts. Roster order matches setup()'s ascending
	# seeding; the manifest tables realias the restored global tables. Neither is
	# part of the snapshot, so this does not affect the restored hash.
	sim._reseed_roster_from_state()
	sim._seed_team_manifest_tables()
	# Re-apply the legacy forge fallback into any cross-faction team's derived
	# (unhashed) contract table. Same-faction teams alias the restored global,
	# which already carries the provisionals, so this is a no-op there and never
	# mutates hashed state.
	sim._register_forge_upgrade_contracts()
	# Both clocks (sim tick and every attached env's interpreter tick) were
	# just set from one snapshot: rebase the recorded executor offsets from
	# those values - the same numbers every peer restoring this snapshot
	# holds, so the derived offset stays peer-identical.
	sim._rebase_script_executor_offsets()


func canonicalize(value: Variant) -> Variant:
	if typeof(value) == TYPE_DICTIONARY:
		var source := value as Dictionary
		var keys := source.keys()
		keys.sort_custom(canonical_key_less)
		var rows: Array = []
		for key in keys:
			rows.append([canonicalize(key), canonicalize(source[key])])
		return rows
	if typeof(value) == TYPE_ARRAY:
		var rows: Array = []
		for item in value as Array:
			rows.append(canonicalize(item))
		return rows
	return value


func canonical_key_less(a: Variant, b: Variant) -> bool:
	return "%02d:%s" % [typeof(a), var_to_str(a)] < "%02d:%s" % [typeof(b), var_to_str(b)]

# --- script surface residual bag (honest stores; not full retail subsystems) ---
