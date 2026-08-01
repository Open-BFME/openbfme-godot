extends RefCounted

## Annotation tables behind data/script_world_surface.json, consumed and
## verified by script_world_surface_runner.gd.
##
## WHAT IS MEASURED AND WHAT IS ANALYSIS
## =====================================
## The backed/refused classification of every method is MEASURED by the
## runner's probe; nothing here can change it. These tables carry the parts a
## probe cannot observe:
##   * SUBSYSTEMS      - the missing-subsystem taxonomy and what building each
##                       one actually requires
##   * BLOCKED         - per refusing method: which subsystem blocks it and
##                       the specific sim state it is waiting for
##   * RESTRICTIONS    - per backed method: the argument shapes that still
##                       refuse (a backed method is not a finished method)
##   * VOCABULARY_ROUTING - which world method(s) each of the 91 retail-AI
##                       census members reaches, and what blocks it

## Missing-subsystem taxonomy. Codex full-parity REJECT re-blocks inventing paths.
const SUBSYSTEMS := {
	"supply-center-build": (
		"AI_PLAYER_BUILD_SUPPLY_CENTER: expansion construct for supply-center "
		+ "object types at distance (distance argument unused until pad model)"
	),
	"radius-threat": "retail-sourced radius threat evaluator (allies/hostiles, formula)",
	"wall-upgrade": "wall segment ownership/marker/cost/timing upgrade pipeline",
	"tactical-marker-placement": "marker near/far placement consumed by construction",
	"passenger-transport": "authored Contain/transport eligibility and capacity",
	"command-button-ability": "command-set special-power execution (not ready=false)",
	"reinforcement-army": "authored reinforcement team template spawn",
}

## Per refusing method: blocking subsystem + the specific missing sim state.
const BLOCKED := {
	"economy.build_supply_center": {
		"subsystem": "supply-center-build",
		"requires": "modeled expansion kind for supply object type + free pad",
	},
	"teams.threat": {
		"subsystem": "radius-threat",
		"requires": "retail-sourced team threat evaluation",
	},
	"teams.threat_within_radius": {
		"subsystem": "radius-threat",
		"requires": "retail-sourced radius threat with ally exclusion",
	},
	"teams.set_threat_level": {
		"subsystem": "radius-threat",
		"requires": "TEAM_THREAT_LEVEL is a condition, not a bag write",
	},
	"units.threat": {
		"subsystem": "radius-threat",
		"requires": "retail-sourced unit threat evaluation",
	},
	"units.threat_within_radius": {
		"subsystem": "radius-threat",
		"requires": "retail-sourced radius unit threat evaluation",
	},
	"progression.upgrade_nearest_wall": {
		"subsystem": "wall-upgrade",
		"requires": "sourced wall upgrade pipeline",
	},
	"progression.upgrade_nearest_wall_bound": {
		"subsystem": "wall-upgrade",
		"requires": "marker + ownership + cost/timing wall upgrade",
	},
	"ai.build_base_building_per_tactical_marker": {
		"subsystem": "tactical-marker-placement",
		"requires": "construction consumes marker near/far placement",
	},
	"transport.load_transports": {
		"subsystem": "passenger-transport",
		"requires": "authored Contain modules / transport KindOf",
	},
	"orders.use_command_button": {
		"subsystem": "command-button-ability",
		"requires": "special-power/command-set execution",
	},
	"orders.use_command_button_partial": {
		"subsystem": "command-button-ability",
		"requires": "partial special-power execution",
	},
	"ai.create_reinforcement_team": {
		"subsystem": "reinforcement-army",
		"requires": "authored reinforcement team template",
	},
	"ai.remove_reinforcement_army": {
		"subsystem": "reinforcement-army",
		"requires": "authored reinforcement army remove",
	},
}

## Argument shapes that still refuse on BACKED methods.
const RESTRICTIONS := {
	"ai.base_unpack": "acts as the bound script player (the sourced action carries no player); base flags of the sim's unpackable-base table only",
	"ai.base_unpackable": "base flags of the sim's unpackable-base table only; the player must resolve (a bound name, or '<This Player>' with a script player bound)",
	"ai.build_base_building": "acts as the bound script player; building types with an expansion rule and bases reachable through the shared object/unit-reference namespace only",
	"ai.build_base_building_per_tactical_marker": "requires registered tactical markers of marker_type (or a waypoint named as the marker type)",
	"combat.fire_special_power": "PLAYER scope, spellbook powers, explicit POSITION targets only",
	"combat.player_all_destroyed": "full variant only; build_facilities_only refuses (no build-facility classification)",
	"combat.special_power_ready": "PLAYER scope, powers of the match's spellbook document only",
	"economy.money": "bound players only (facet path refuses unbound; the pre-facet path cannot)",
	"meta.multiplayer_outcome": "defeat/allied_victory/allied_defeat tokens only",
	"meta.object_list_change": "non-empty list and type names only (\"\" names nothing in the retail vocabulary); set semantics, duplicate adds and absent removes succeed as retail no-ops",
	"orders.attack": "TEAM/PLAYER scope attacking a bound TEAM target only",
	"orders.attack_move_to": "TEAM/PLAYER scope with explicit POSITION targets only",
	"orders.move_to": "TEAM/PLAYER scope with POSITION or NEAREST_TYPE targets; UNIT scope needs object-name-registry; a NEAREST_TYPE naming only types the sim cannot field refuses (retail's authored targets are mostly map-placed markers, but also plot flags, treasure and real siege unit types - the refusal is about fieldability, not about markers), and waypoint/area targets need map geometry",
	"orders.use_command_button": "command button must map to a registered ability id on a living scoped unit",
	"players.object_count_of_types": "the player must resolve (bound names, '<This Player>', the plural enemies/allies aggregate tokens - aggregates SUM); the singular '<This Player's Enemy>' token refuses (no current-enemy model); creep-guard battalions and legacy synthetic ids without recorded provenance are not countable",
	"players.start_position": "the player must resolve (a bound name, or '<This Player>' with a script player bound) and its roster descriptor must carry an authoritative nonnegative internal start_index; the answer is the corresponding script-facing one-based Player_N_Start number",
	"orders.stand_ground": "TEAM/PLAYER scope only; TEAM requires a complete authoritative named script-team membership and changes only its entity handles, while PLAYER changes the resolved whole roster. '<This Team>' refuses without executing-team context. Enabled selects HoldGround and clear selects ordinary Battle (a status setter, not restoration of a previous stance)",
	"players.building_count": "empty class (count everything) or a structure kind this sim models",
	"progression.has_science": "sciences of the GLOBAL spellbook tree only; per-team overrides refuse",
	"progression.has_upgrade": "PLAYER scope, modelled upgrade ids only",
	"players.can_build_at_base": "the player must resolve ('<This Player>' included); bases through the shared object namespace; a non-empty object type must have an expansion rule (false for an unmodeled type would be a guess)",
	"progression.unit_count_with_upgrade": "modelled upgrade ids only (zero for an unknown id would be a guess)",
	"progression.upgrade_nearest_wall_bound": "base must resolve to structure/waypoint; nearest wall-kind structure required",
	"teams.custom_state": "bound team names only; '<This Team>' - the spelling retail authors at essentially every call site - refuses until an executing-team (sequential-script) context exists, and the script player's whole roster would be the wrong team",
	"teams.threat_within_radius": "team must resolve with a living anchor member",
	"transport.load_transports": "structures need transport capacity; ownership and free slots required",
	"units.set_gate_state": "name must resolve to a live structure",
	"units.threat_within_radius": "name must be a live entity id",
}

## Vocabulary routing for the 91 retail-AI census members.
const VOCABULARY_ROUTING := {
	"FLAG": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"SET_FLAG": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"DEBUG_STRING": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"DECREMENT_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"ENABLE_SCRIPT": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"SET_MILLISECOND_TIMER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"SET_TIMER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"PLAYER_PURCHASE_SCIENCE": {"route": "backed", "worldMethods": ["progression.purchase_science"], "mappingSource": "handler"},
	"PLAYER_CAN_PURCHASE_SCIENCE": {"route": "backed", "worldMethods": ["progression.can_purchase_science"], "mappingSource": "handler"},
	"COUNTER_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"CALL_SUBROUTINE": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"SET_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"PLAYER_HAS_OBJECT_COMPARISON": {"route": "backed", "worldMethods": ["players.object_count_of_types"], "mappingSource": "handler", "note": "the formerly heaviest blocked member (100 call sites), served through the sim's object-type identity census; aggregate player tokens sum"},
	"NO_OP": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"CONDITION_TRUE": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"DISABLE_SCRIPT": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"COUNTER_MATH_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"NAMED_BASE_UNPACKABLE_FOR_PLAYER": {"route": "backed", "worldMethods": ["ai.base_unpackable"], "mappingSource": "handler"},
	"SET_COUNTER_TO_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"COUNTER_MATH_VALUE": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"TEAM_SET_CUSTOM_STATE": {"route": "backed", "worldMethods": ["teams.set_custom_state"], "mappingSource": "handler", "note": "served through the corrected signature carrying the BOOLEAN enable flag WP15's gap registration demanded; the flag reads from the payload integer field, both polarities delivered un-inverted"},
	"CAN_BUILD_AT_BASE": {"route": "backed", "worldMethods": ["players.can_build_at_base"], "mappingSource": "handler", "note": "served through the corrected base-carrying signature; empty object_type is the 'anything at all' form"},
	"TEAM_TRANSFER_TO_PLAYER": {"route": "backed", "worldMethods": ["teams.transfer_to_player"], "mappingSource": "handler", "note": "all 32 sites per tree address PlyrCivilian/Player_N_Inherit and <This Player>; the converter source-attests every effective official-skirmish inheritance team in both trees as marker-only, schema-v2 composes the real ai_initialize and ai_mp_inherit_management libraries per concrete AI Player_N, and real BFME2 plus RotWK Fords composites execute the decoded action through the simulation-owned controller mutation"},
	"TEAM_SET_STATE": {"route": "backed", "worldMethods": ["teams.set_state"], "mappingSource": "handler", "note": "retail's doSetTeamState is a bare Team::setState - storage IS the semantic; tokens are content-defined and stored unvalidated"},
	"NAMED_OWNED_BY_PLAYER": {"route": "backed", "worldMethods": ["units.is_owned_by"], "mappingSource": "handler", "note": "the bounded 14-library AI census contains 32 BFME2 and 32 RotWK sites; a broader authored-library scan contains 48 per tree after adding multiplayer_human and multiplayer_start_teams. All pass BASE_FLAG_1..16 and '<This Player>'; the token resolves in the world against the bound script player"},
	"NAMED_BASE_UNPACK": {"route": "backed", "worldMethods": ["ai.base_unpack"], "mappingSource": "handler", "note": "served through the corrected signature carrying the UNIT_REF destination"},
	"TIMER_EXPIRED": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"TEAM_EXECUTE_SEQUENTIAL_SCRIPT": {"route": "backed", "worldMethods": ["teams.execute_sequential_script"], "mappingSource": "handler", "note": "queues a named script on the team's sequential head, idles the team, and progresses with calling/condition team latched so <This Team> resolves; unit sequential remains blocked"},
	"SET_UNIT_REFERENCE": {"route": "backed", "worldMethods": ["units.set_reference"], "mappingSource": "declared", "note": "measured on the shipped libraries, all 40 authored SOURCE arguments are map-placed markers: BASE_FLAG_1..16 (32 sites, which the simulation's unpackable-base table models and which bind) and BASE_SPAWN_1..8 (8 sites, which it does not model and which refuse per-argument)"},
	"SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER": {"route": "backed", "worldMethods": ["players.object_count_of_types"], "mappingSource": "handler"},
	"AI_PLAYER_BUILD_UPGRADE": {"route": "backed", "worldMethods": ["progression.build_upgrade"], "mappingSource": "handler"},
	"TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER": {"route": "backed", "worldMethods": ["orders.move_to"], "mappingSource": "handler", "note": "NEAREST_TYPE resolves including owner tokens; blocked because no target type in this census scope is fieldable yet. Predominantly marker types (Center/Flank/Backdoor nodes), but see the sibling member's note - the corpus also authors real unit-type targets, so the block is 'these types are not fieldable', not 'markers only'"},
	"PLAYER_HAS_OBJECT_OF_VETERANCY": {"route": "backed", "worldMethods": ["progression.has_object_of_veterancy"], "mappingSource": "handler", "note": "existential living-object current-rank predicate over exact type identity; observed retail list-like token expands through the established list-first resolver"},
	"TEAM_USE_COMMANDBUTTON_ABILITY": {
		"route": "blocked",
		"worldMethods": ["orders.use_command_button"],
		"blockingSubsystem": "command-button-ability",
		"mappingSource": "declared",
	},
	"TEAM_CHANGE_OBJECT_STATUS": {"route": "backed", "worldMethods": ["units.set_object_status"], "mappingSource": "handler", "note": "exact authored OBJECT_STATUS names on living entity members of the team; polarity set/clear is load-bearing"},
	"SKIRMISH_PLAYER_FACTION": {"route": "backed", "worldMethods": ["players.faction"], "mappingSource": "handler"},
	"UNIT_THREAT_LEVEL": {
		"route": "blocked",
		"worldMethods": ["units.threat_within_radius"],
		"blockingSubsystem": "radius-threat",
		"mappingSource": "handler",
	},
	"PLAYER_HAS_CREDITS": {"route": "backed", "worldMethods": ["economy.money"], "mappingSource": "handler"},
	"NAMED_NOT_DESTROYED": {"route": "backed", "worldMethods": ["units.was_destroyed"], "mappingSource": "declared", "note": "the world method answers, but only for names the shared namespace holds"},
	"CAN_BUILD_OBJECTTYPE_AT_BASE": {"route": "backed", "worldMethods": ["players.can_build_at_base"], "mappingSource": "handler"},
	"TEAM_HAS_CUSTOM_STATE": {"route": "backed", "worldMethods": ["teams.custom_state"], "mappingSource": "handler", "note": "the value-shape ambiguity WP15 reported is RULED: the backed world answers the ARRAY of enabled tokens (the writer's boolean proves a set); the handler keeps its String tolerance for stub worlds"},
	"SET_UNIT_REFERENCE_TO_REFERENCE": {"route": "backed", "worldMethods": ["units.set_reference"], "mappingSource": "declared", "note": "all 16 authored sites copy AI_EXPANSION_1..16 into AI_CURRENT_CONSTRUCTION_SITE; AI_EXPANSION_n is bound by NAMED_BASE_UNPACK, which is already backed, so this chain resolves end to end"},
	"INCREMENT_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"BUILD_BASE_BUILDING": {"route": "backed", "worldMethods": ["ai.build_base_building"], "mappingSource": "handler", "note": "served through the corrected (building_type, base, result_reference) signature"},
	"BUILD_BASE_BUILDING_PER_TACTICAL_MARKER": {
		"route": "blocked",
		"worldMethods": ["ai.build_base_building_per_tactical_marker"],
		"blockingSubsystem": "tactical-marker-placement",
		"mappingSource": "handler",
	},
	"CREATE_OBJECT": {"route": "backed", "worldMethods": ["units.create_object"], "mappingSource": "declared"},
	"CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION": {
		"route": "blocked",
		"worldMethods": ["ai.create_reinforcement_team"],
		"blockingSubsystem": "reinforcement-army",
		"mappingSource": "handler",
	},
	"EVAL_TEAM_HEALTH": {"route": "backed", "worldMethods": ["combat.team_health_percent"], "mappingSource": "declared", "note": "combat.team_health_percent answers weighted current/max over complete script-team entity membership, integer-rounded; empty teams answer 0; '<This Team>' still needs executing-team context"},
	"GATE_CLOSE": {"route": "backed", "worldMethods": ["units.set_gate_state"], "mappingSource": "declared", "note": "requires live structure name; sets structure.gate_open"},
	"GATE_OPEN": {"route": "backed", "worldMethods": ["units.set_gate_state"], "mappingSource": "declared", "note": "requires live structure name; sets structure.gate_open"},
	"NAMED_BASE_UNPACK_FREE": {"route": "backed", "worldMethods": ["ai.base_unpack"], "mappingSource": "handler"},
	"NAMED_USE_COMMANDBUTTON_ABILITY": {
		"route": "blocked",
		"worldMethods": ["orders.use_command_button"],
		"blockingSubsystem": "command-button-ability",
		"mappingSource": "declared",
	},
	"PLAYER_DESTROYED_N_BUILDINGS_PLAYER": {"route": "backed", "worldMethods": ["combat.buildings_destroyed_by"], "mappingSource": "declared"},
	"PLAYER_ENABLE_BASE_CONSTRUCTION": {"route": "backed", "worldMethods": ["players.set_base_construction_enabled"], "mappingSource": "handler"},
	"PLAYER_ENABLE_UNIT_CONSTRUCTION": {"route": "backed", "worldMethods": ["players.set_unit_construction_enabled"], "mappingSource": "declared"},
	"PLAYER_SELL_EVERYTHING": {"route": "backed", "worldMethods": ["players.sell_everything"], "mappingSource": "handler"},
	"SET_COUNTER_TO_TEAM_THREAT": {
		"route": "blocked",
		"worldMethods": ["teams.threat"],
		"blockingSubsystem": "radius-threat",
		"mappingSource": "handler",
	},
	"SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER": {"route": "backed", "worldMethods": ["players.command_points_available"], "mappingSource": "handler"},
	"SET_PLAYER_MONEY_TO_COUNTER": {"route": "backed", "worldMethods": ["world.set_player_money"], "mappingSource": "handler"},
	"SET_RANDOM_COUNTER": {"route": "backed", "worldMethods": ["world.random_int"], "mappingSource": "handler", "note": "served by the sim-owned retail GameLogic stream (logic_random_int); the spell-list-choice gate c930b68 measured"},
	"SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER": {"route": "backed", "worldMethods": ["teams.set_reference_to_nearest"], "mappingSource": "handler", "note": "teams.set_reference_to_nearest uses first-member anchor position (not all-object centroid); structure hits bind the unit-reference store; entities bind script_entity_references"},
	"SET_TEAM_REFERENCE": {"route": "backed", "worldMethods": ["teams.set_reference"], "mappingSource": "handler"},
	"START_POSITION_IS": {"route": "backed", "worldMethods": ["players.start_position"], "mappingSource": "handler", "note": "all 8 BFME2 and all 8 RotWK retail-AI sites compare <This Player> to authored Player_1_Start..Player_8_Start numbers; the world returns internal getMpStartIndex plus one"},
	"TEAM_CREATED": {"route": "backed", "worldMethods": ["teams.was_created"], "mappingSource": "handler"},
	"TEAM_DELETE": {"route": "backed", "worldMethods": ["teams.delete"], "mappingSource": "handler"},
	"TEAM_DESTROYED": {"route": "backed", "worldMethods": ["teams.was_destroyed"], "mappingSource": "handler", "note": "served for bound team names as retail's level !hasAnyObjects() read; all 5 AI call sites author the <This Team> token, which still refuses"},
	"TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING": {"route": "backed", "worldMethods": ["teams.execute_sequential_script"], "mappingSource": "handler", "note": "counts 0/1 are served by the handler (forever/once); >=2 is a signature gap (boolean cannot express finite repeats); all 5 retail AI sites pass 0"},
	"TEAM_EXIT_ALL_BUILDINGS": {"route": "backed", "worldMethods": ["teams.exit_all"], "mappingSource": "handler"},
	"TEAM_GUARD_FOR_SECONDS": {"route": "backed", "worldMethods": ["orders.guard"], "mappingSource": "handler", "note": "duration>0 still refuses; zero-duration POSITION/self/TEAM guard is served"},
	"TEAM_GUARD_TEAM": {"route": "backed", "worldMethods": ["orders.guard"], "mappingSource": "handler"},
	"TEAM_HAS_UNITS": {"route": "backed", "worldMethods": ["teams.unit_count"], "mappingSource": "handler"},
	"TEAM_HUNT": {"route": "backed", "worldMethods": ["orders.hunt"], "mappingSource": "handler", "note": "empty command button only"},
	"TEAM_IDLE_FOR_SECONDS": {"route": "backed", "worldMethods": ["orders.idle_for_ticks"], "mappingSource": "handler"},
	"TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL": {"route": "backed", "worldMethods": ["teams.attacked_and_cannot_retaliate_count"], "mappingSource": "declared"},
	"TEAM_LOAD_TRANSPORTS": {
		"route": "blocked",
		"worldMethods": ["transport.load_transports"],
		"blockingSubsystem": "passenger-transport",
		"mappingSource": "handler",
	},
	"TEAM_MERGE_INTO_TEAM": {"route": "backed", "worldMethods": ["teams.merge_into"], "mappingSource": "handler", "note": "transfers living source members onto destination owner team"},
	"TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE": {"route": "backed", "worldMethods": ["orders.move_to"], "mappingSource": "handler", "note": "NEAREST_TYPE resolves over fieldable types; blocked because no target type in this census scope is fieldable yet"},
	"TEAM_RECRUIT_UNITS": {"route": "backed", "worldMethods": ["teams.recruit"], "mappingSource": "handler", "note": "teams.recruit distance form: transfers living units within radius onto the destination owner; INT unit-count recruit spelling is a signature gap"},
	"TEAM_RECRUIT_UNITS_FROM_TEAM": {"route": "backed", "worldMethods": ["teams.recruit"], "mappingSource": "handler", "note": "teams.recruit with from_team source; INT unit-count spelling is a signature gap"},
	"TEAM_SET_ATTITUDE": {"route": "backed", "worldMethods": ["teams.set_attitude"], "mappingSource": "handler", "note": "mood matrix maps attitude to stance + mood_attack_check_rate_ticks"},
	"TEAM_STATE_IS": {"route": "backed", "worldMethods": ["teams.state"], "mappingSource": "handler", "note": "exact case-sensitive comparison (AsciiString strcmp); the empty string is retail's default for a team never set"},
	"TEAM_STATE_IS_NOT": {"route": "backed", "worldMethods": ["teams.state"], "mappingSource": "handler", "note": "not composed as the negation of TEAM_STATE_IS: a refused read must stay false for BOTH spellings (retail answers false to both for a nonexistent team)"},
	"TEAM_STOP": {"route": "backed", "worldMethods": ["teams.stop"], "mappingSource": "handler"},
	"TEAM_STAND_GROUND": {"route": "backed", "worldMethods": ["orders.stand_ground"], "mappingSource": "handler", "note": "both BFME2 and RotWK retail-AI sites author FALSE; CLEAR selects ordinary Battle rather than inverting the action into HoldGround"},
	"TEAM_STOP_SEQUENTIAL_SCRIPT": {"route": "backed", "worldMethods": ["teams.stop_sequential_script"], "mappingSource": "handler", "note": "clears every sequential head/chain entry for the named team"},
	"TEAM_THREAT_LEVEL": {
		"route": "blocked",
		"worldMethods": ["teams.threat_within_radius"],
		"blockingSubsystem": "radius-threat",
		"mappingSource": "handler",
	},
	"TYPE_SIGHTED": {"route": "backed", "worldMethods": ["units.type_was_sighted"], "mappingSource": "declared"},
	"UNIT_HEALTH": {"route": "backed", "worldMethods": ["units.health_percent"], "mappingSource": "declared"},
	"UNIT_SET_TEAM": {"route": "backed", "worldMethods": ["units.set_team"], "mappingSource": "handler"},
	"UPGRADE_NEAREST_WALL": {
		"route": "blocked",
		"worldMethods": ["progression.upgrade_nearest_wall_bound"],
		"blockingSubsystem": "wall-upgrade",
		"mappingSource": "handler",
	},
}
