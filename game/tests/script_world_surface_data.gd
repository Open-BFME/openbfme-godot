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
##
## The runner asserts coherence in every direction it can: every refusing
## method annotated, every annotation naming a real method and a declared
## subsystem, every declared subsystem referenced, every census member routed
## exactly once, every route citing real methods, and every route declared
## "backed" citing only methods the probe saw answer.
##
## HOW THE VOCABULARY -> WORLD-METHOD MAPPING WAS ESTABLISHED
## ==========================================================
## Two sources, recorded per route as `mappingSource`:
##   * "handler"  - a handler package under src/script/handlers/ (or
##                  script_handlers_core.gd) actually calls the method, or
##                  gap-registers the member naming the exact missing
##                  signature. Read from the handler code. HIGH confidence.
##   * "declared" - no handler exists yet; the route is the one script_world.gd's
##                  own per-method doc comments declare for that InternalName.
##                  The surface was derived from the full handler catalog, so
##                  this is design intent, not observed dispatch. MEDIUM
##                  confidence - a future handler could route differently, and
##                  the runner's route checks would then need updating.
## Routes with `signatureGap: true` are members a handler PROVED cannot be
## served by the current facet signature (a load-bearing argument has no
## parameter). Those need the facet-signature packet IN ADDITION to whatever
## sim subsystem blocks the method itself; the ranking reports that overlap
## separately rather than double-counting.

## Missing-subsystem taxonomy. `id` -> what building it actually requires.
const SUBSYSTEMS := {
	"adapter-only": (
		"No new sim state at all: answerable today from existing public sim "
		+ "state for bound names (e.g. team health aggregates over the "
		+ "member-health arrays). Refuses only because the adapter has not "
		+ "implemented it - the cheapest bucket on this list."
	),
	"attack-priorities": (
		"Named attack-priority sets: a store of per-thing/per-kindof priority "
		+ "tables plus target-selection hooks in the sim's combat AI that "
		+ "consult the applied set."
	),
	"base-building-ai": (
		"The skirmish-AI base model. Its heaviest-traffic slice is BUILT: base "
		+ "flags with unpack economics, the unpack pair's result references, "
		+ "and per-base buildability (the 64-per-pass ai_economy_execution "
		+ "poll, the unpack pair and the CAN_BUILD pair are served). Still "
		+ "missing: foundations, tactical markers, base regions and "
		+ "population, approach paths, home-base identity, reinforcement "
		+ "armies, view guardband."
	),
	"command-button-abilities": (
		"Command-button -> ability resolution on battalions: executing and "
		+ "auto-toggling abilities by button name, and readiness counts per "
		+ "team. The sim models spellbook powers per player, not per-object "
		+ "command buttons."
	),
	"diplomacy-overrides": (
		"Mutable relation state: per-player and per-team relation overrides "
		+ "layered over the fixed alliance-derived relation the sim computes "
		+ "today (set, remove, remove-all)."
	),
	"economy-model-extras": (
		"Economy state beyond the single team-resources number: supply "
		+ "sources and trucking, area value censuses, warehouse values, "
		+ "harvesting, scoring, and the (likely Generals-vestigial) power "
		+ "produced/consumed model."
	),
	"entity-lifecycle-api": (
		"A public sim API for scripted spawn/despawn/damage/kill/teleport/"
		+ "ownership-transfer that honors command-point accounting, "
		+ "member-health arrays and the corpse flow - today's spawn path is "
		+ "private and raw writes would bypass all three (005bcd8's own "
		+ "finding)."
	),
	"entity-status-flags": (
		"Per-entity/per-team status bits the sim rules would have to consult: "
		+ "object status, model conditions, stealth, held, repulsor, flame, "
		+ "web, shock, panel flags, weaponset switches, emoticons."
	),
	"event-ledger": (
		"Recorded simulation events with edge semantics: attacked-by "
		+ "(player/type), cannot-retaliate, kills by type/kindof, buildings "
		+ "destroyed per victim, EVA event memory, formation orders issued, "
		+ "objects lost by type."
	),
	"experience-model": (
		"A public experience surface over the authored XP chains: level and "
		+ "XP reads, grants, gained-level edges, level caps, veterancy "
		+ "censuses, XP-receiving/sharing toggles - for units, teams and "
		+ "players (unit/team scope also needs the name/team registries)."
	),
	"facet-signature-packet": (
		"Not a sim subsystem: the coordinated signature-correction packet on "
		+ "script_world.gd collecting every gap-registered missing parameter "
		+ "(custom-state enable flag, threat radius, recruit type list, "
		+ "stand-ground clear flag, nearest-team anchor, tactical-marker "
		+ "builds, wall-upgrade shape) plus the remaining defects 005bcd8 "
		+ "reported. Three corrections have landed: base-anchored "
		+ "buildability (players.can_build_at_base grew the base), the unpack "
		+ "result references (ai.base_unpack grew its UNIT_REF destination) "
		+ "and the base-anchored build (ai.build_base_building was reshaped "
		+ "to the sourced OBJECT_TYPE/UNIT/UNIT_REF form)."
	),
	"garrison-transport-capture": (
		"The WP03/WP04 blocked block: passenger/transport containers, "
		+ "garrisons, tunnel networks, building emptiness, capture "
		+ "allegiance, exits. No container model exists in either sim."
	),
	"hero-revival-carryover": (
		"A hero revival and delayed-carryover ledger: revival entries at "
		+ "level, carryover units materialised at waypoints, per-type "
		+ "carryover queries."
	),
	"living-world": (
		"The LivingWorld campaign strategic layer, deliberately unmodelled; "
		+ "the whole family funnels through one command/query pair until a "
		+ "campaign layer exists."
	),
	"map-geometry": (
		"Map geometry in the sim: waypoints, waypoint paths, trigger-area "
		+ "polygons, point-in-area tests, path-following and pathability "
		+ "queries, passability edits. The sim currently knows positions but "
		+ "no named geometry."
	),
	"match-config-metadata": (
		"Match-configuration metadata: start-spot indices, game-mode token, "
		+ "mission attempts, planning mode. Facts of match setup the sim is "
		+ "never told today."
	),
	"match-lifecycle-controls": (
		"Scripted match-lifecycle controls: per-player local defeat, map "
		+ "exit, and a time-freeze design that cannot deadlock the script "
		+ "layer (set_time_frozen is refused by 005bcd8's deadlock finding, "
		+ "not by missing state: freezing the tick would freeze the scripts, "
		+ "so a scripted UNFREEZE could never run)."
	),
	"object-name-registry": (
		"WHAT IS LEFT of the registry gap 005bcd8 named, after the shared "
		+ "object / unit-reference namespace was measured against the retail "
		+ "corpus and the retail semantics were sourced. The eight reads and "
		+ "the bind that namespace can answer are now BUILT (exists, "
		+ "was_created, was_destroyed, is_dying, position, owner, "
		+ "health_percent, set_reference) - and they needed NO new simulation "
		+ "state and NO edge records: the GPL ScriptEngine derives this whole "
		+ "family from the current name table plus the object's dead flag "
		+ "(NAMED_NOT_DESTROYED is evaluateNamedUnitExists; NAMED_CREATED is "
		+ "literally getUnitNamed() != NULL, with the engine's own '@todo - "
		+ "evaluate created, not exists' above it), so the 'per-name created/"
		+ "destroyed edge records' this entry used to demand were never a "
		+ "retail thing. WHAT REMAINS is a genuinely different binding: every "
		+ "entry in the namespace is a base flag or a STRUCTURE, and the four "
		+ "members still here are BATTALION state (stance, stop, alt "
		+ "formation) or need an object-removal edge this simulation does not "
		+ "have (is_totally_dead - a killed structure row is never removed, so "
		+ "retail's nulled-pointer state never occurs). Zero retail AI call "
		+ "sites route here any more."
	),
	"object-type-identity": (
		"Retail object-type identity on sim rows. Its heaviest-traffic slice "
		+ "is BUILT: rows resolve retail type names through recorded rule "
		+ "provenance / runtime-id slugs / the manifest kind registries, the "
		+ "script-built OBJECT_TYPE_LIST stores live in the sim (hashed, "
		+ "empty-is-absent), and the type censuses plus nearest-of-type "
		+ "search serve PLAYER_HAS_OBJECT_COMPARISON's 100 AI call sites. "
		+ "Still missing here: KindOf bit sets ON A ROW'S OWN IDENTITY, and "
		+ "model-condition flags. Precisely: pack documents DO carry KindOf "
		+ "arrays - 72 of them, in 6 hero documents, inside ability-effect "
		+ "summon leaves (registration/abilities[n]/effect/leaves/objects[m]/"
		+ "kindOf) - but none on the document's own object identity, which is "
		+ "what a row would need. 'The importer extracts none' was an "
		+ "overclaim; the conclusion is unchanged."
	),
	"order-verbs": (
		"Simulation order types beyond move/attack-move/attack/stance: guard "
		+ "(timed, area, object), hunt, idle-for, wander, face, move-home, "
		+ "repair-nearest, spin, stopping distance."
	),
	"player-progression-state": (
		"Player-level progression state: rank levels and caps, skill points "
		+ "and skill sets, light points, spell-point caps, command-point "
		+ "overrides."
	),
	"presentation-adapter": (
		"A client-side presentation adapter draining the camera/audio/ui "
		+ "sinks; simulation-free by design, zero lockstep risk, zero retail "
		+ "AI call sites - 191 handler members, none of them AI traffic."
	),
	"production-controls": (
		"Per-player production gating and structure verbs: base-construction/"
		+ "factory/unit-construction toggles, construction speed, auto-build, "
		+ "sell-everything, repair, prerequisite and buildability evaluation."
	),
	"progression-grant-api": (
		"Public grant surfaces that bypass production HONESTLY: outright "
		+ "upgrade grants (distinct from build_upgrade's queue), free science "
		+ "grants, science availability edits."
	),
	"sequential-scripts": (
		"Sequential-script execution bound to teams/units: per-subject "
		+ "sequential queues the script executor owns and the sim can start, "
		+ "loop and stop (ai_attack_execution drives every behaviour script "
		+ "through this)."
	),
	"sim-selection-model": (
		"A SIMULATION notion of selection. sim.selected_ids is per-seat "
		+ "presentation state, explicitly not lockstep; backing a script "
		+ "condition with it would desync (005bcd8's finding). Needs a design "
		+ "decision before it needs code."
	),
	"special-power-extras": (
		"Special-power state beyond the modelled owned/ready/cast pair: "
		+ "writable and pausable countdowns (player- and object-scoped) and "
		+ "triggered/midway/completed phase events per cast."
	),
	"spatial-queries": (
		"Deterministic spatial search surface: nearest-of-type/kindof/"
		+ "most-valuable object selection, distance between named subjects, "
		+ "radius-scoped threat evaluation, threat finders."
	),
	"team-behavior-state": (
		"Per-team behaviour state. Its AI-traffic slice is BUILT: the "
		+ "TEAM_STATE string (retail's Team::m_state - one unvalidated "
		+ "case-sensitive string per team, default \"\", save-persisted) and "
		+ "the custom-state token SETS live in the sim "
		+ "(team_behavior_states, hashed empty-is-absent), serving the six "
		+ "members the attack loops gate on - TEAM_SET_CUSTOM_STATE's 40 "
		+ "call sites included, now that the facet carries its enable flag. "
		+ "Still missing here: attitude/mood CONSUMPTION (the retail write "
		+ "is a per-member broadcast of the raw AI_MOOD int, but the AIUpdate "
		+ "mood matrix that consumes it - idle-acquire modes, move-to-attack-"
		+ "move conversion, INI-authored range adjustments - is unmodeled, "
		+ "and a store-only serve would be a silent semantic no-op), the "
		+ "EmotionTracker (undecompiled in the research tree, parse-only in "
		+ "OpenSAGE, EMOTION ordinals unestablished), and the AI_PANIC state "
		+ "machine (waypoint-path flight; the facet signature also lacks the "
		+ "action's WAYPOINT_PATH argument)."
	),
	"team-registry": (
		"The WorldBuilder sub-player team model: teams instantiated from "
		+ "templates, membership and leaders, priorities, team references, "
		+ "recruitment, merge/delete/transfer, created/destroyed edges. Bound "
		+ "default rosters exist; script teams do not."
	),
	"terrain-dynamics": (
		"Terrain/water/weather dynamics: bridges, water heights, burn rates "
		+ "and area fire, cloud speed, map borders, logic fog, tree censuses."
	),
	"vision-and-discovery": (
		"A per-player vision/discovery model: fog of war and shroud, "
		+ "sighted/discovered edges for players, teams, units and types. "
		+ "Neither sim models visibility at all (WP02 blocked block)."
	),
	"walls-and-siege": (
		"Walls, gates and siege attachment: wall segments and upgrade "
		+ "targets, gate open/close/ready state, siege tower deploy/retract."
	),
}

## Per refusing method: blocking subsystem + the specific missing sim state.
const BLOCKED := {
	# --- ai -----------------------------------------------------------------
	"ai.base_population": {"subsystem": "base-building-ai", "requires": "a base-population count per AI base"},
	"ai.build_on_foundation": {"subsystem": "base-building-ai", "requires": "foundation objects a building can be placed on"},
	"ai.camps_should_unpack": {"subsystem": "base-building-ai", "requires": "camp regions with unpack pacing state"},
	"ai.create_reinforcement_team": {"subsystem": "base-building-ai", "requires": "reinforcement army templates and spawn anchors (retail anchors on the reference NAMED_BASE_UNPACK_FREE binds)"},
	"ai.move_to_approach_path": {"subsystem": "base-building-ai", "requires": "authored approach paths per base"},
	"ai.remove_reinforcement_army": {"subsystem": "base-building-ai", "requires": "a reinforcement army roster to remove from"},
	"ai.sell_on_foundation": {"subsystem": "base-building-ai", "requires": "foundation objects plus a sell/refund path"},
	"ai.set_attack_priority": {"subsystem": "attack-priorities", "requires": "named priority sets keyed by thing/kindof"},
	"ai.set_buildings_allowed": {"subsystem": "production-controls", "requires": "per-player per-type build permission flags the production rules consult"},
	"ai.set_default_attack_priority": {"subsystem": "attack-priorities", "requires": "a default priority slot per named set"},
	"ai.set_view_guardband": {"subsystem": "base-building-ai", "requires": "an AI awareness guardband the wave logic reads"},
	"ai.skirmish_build": {"subsystem": "base-building-ai", "requires": "front/flank placement zones per base"},
	# --- areas --------------------------------------------------------------
	"areas.base_transition_count": {"subsystem": "base-building-ai", "requires": "base regions plus per-subject enter/exit edge records"},
	"areas.contains": {"subsystem": "map-geometry", "requires": "trigger-area polygons and a point-in-area test"},
	"areas.exists": {"subsystem": "map-geometry", "requires": "named trigger areas loaded from map data"},
	"areas.is_on_fire": {"subsystem": "terrain-dynamics", "requires": "area fire state"},
	"areas.member_count": {"subsystem": "map-geometry", "requires": "trigger areas plus per-subject membership counting"},
	"areas.object_inside_base": {"subsystem": "base-building-ai", "requires": "base regions plus object/type containment tests"},
	"areas.set_human_impassable": {"subsystem": "map-geometry", "requires": "per-area passability the human pathing respects"},
	"areas.transition_count": {"subsystem": "map-geometry", "requires": "trigger areas plus entered/exited EDGE records since last evaluation (containment alone must not answer these)"},
	"areas.tree_count": {"subsystem": "terrain-dynamics", "requires": "map tree objects countable per area"},
	"areas.unit_count_in_area": {"subsystem": "map-geometry", "requires": "trigger areas plus type/kindof/upgrade/built filters over contained objects"},
	"areas.waypoint_path_exists": {"subsystem": "map-geometry", "requires": "named waypoint paths from map data"},
	"areas.waypoint_position": {"subsystem": "map-geometry", "requires": "named waypoints from map data - the primitive under every _AT_WAYPOINT action"},
	# --- presentation sinks -------------------------------------------------
	"audio.emit": {"subsystem": "presentation-adapter", "requires": "an audio/video sink adapter on the client"},
	"camera.emit": {"subsystem": "presentation-adapter", "requires": "a camera sink adapter on the client"},
	"ui.emit": {"subsystem": "presentation-adapter", "requires": "a UI/radar/VFX sink adapter on the client"},
	# --- combat -------------------------------------------------------------
	"combat.attack_nearest_group_with_value": {"subsystem": "spatial-queries", "requires": "value-weighted group search near a team"},
	"combat.attacked_and_cannot_retaliate": {"subsystem": "event-ledger", "requires": "attacked-without-retaliation-path records per named object (plus the name binding)"},
	"combat.attacked_by": {"subsystem": "event-ledger", "requires": "attacked-by records keyed by player and object type"},
	"combat.buildings_destroyed_by": {"subsystem": "event-ledger", "requires": "per-victim building-kill tallies"},
	"combat.command_button_ready_count": {"subsystem": "command-button-abilities", "requires": "per-member ability readiness by command button"},
	"combat.damage": {"subsystem": "entity-lifecycle-api", "requires": "a public damage entry point honoring member-health arrays, CP release and the corpse flow (raw health writes bypass all three)"},
	"combat.kill": {"subsystem": "entity-lifecycle-api", "requires": "a public kill entry point honoring the corpse flow"},
	"combat.kill_count_of_type": {"subsystem": "event-ledger", "requires": "kill tallies keyed by object type and kindof"},
	"combat.kill_horde_members": {"subsystem": "entity-lifecycle-api", "requires": "member-granular kill API within a battalion row"},
	"combat.set_bonuses_allowed": {"subsystem": "entity-status-flags", "requires": "a per-entity bonus-eligibility flag the damage pipeline consults (plus the name binding)"},
	"combat.set_health_percent": {"subsystem": "entity-lifecycle-api", "requires": "a public health-write API honoring member-health arrays"},
	"combat.set_special_power_countdown": {"subsystem": "special-power-extras", "requires": "writable per-power countdowns (per object needs the name binding too)"},
	"combat.set_special_power_countdown_running": {"subsystem": "special-power-extras", "requires": "pausable per-object power countdowns"},
	"combat.set_victim_selection_normal": {"subsystem": "attack-priorities", "requires": "a victim-selection mode switch in target selection"},
	"combat.special_power_phase": {"subsystem": "special-power-extras", "requires": "triggered/midway/completed phase events per power cast"},
	"combat.team_health_percent": {"subsystem": "adapter-only", "requires": "nothing new: aggregate member_health over living_ids for a bound roster - EVAL_TEAM_HEALTH's 4 AI call sites are waiting on adapter work only"},
	"combat.using_autopickup": {"subsystem": "command-button-abilities", "requires": "auto-pickup behaviour flags per object (plus the name binding)"},
	"combat.using_bloodthirsty": {"subsystem": "command-button-abilities", "requires": "bloodthirsty ability usage tracking per player"},
	# --- economy ------------------------------------------------------------
	"economy.build_supply_center": {"subsystem": "economy-model-extras", "requires": "supply-center build orders in the AI economy"},
	"economy.resume_supply_trucking": {"subsystem": "economy-model-extras", "requires": "a supply-trucking loop to resume"},
	"economy.set_scoring_enabled": {"subsystem": "economy-model-extras", "requires": "a scoring model to toggle"},
	"economy.supplies_within_distance": {"subsystem": "economy-model-extras", "requires": "supply objects with values, searchable by distance"},
	"economy.supply_source_state": {"subsystem": "economy-model-extras", "requires": "supply sources with attacked/safe state"},
	"economy.value_in_area": {"subsystem": "economy-model-extras", "requires": "per-object value plus trigger-area geometry"},
	# --- fog ----------------------------------------------------------------
	"fog.reveal": {"subsystem": "vision-and-discovery", "requires": "a fog-of-war grid to reveal (WP02: no subsystem exists in either sim)"},
	"fog.set_border_shroud": {"subsystem": "vision-and-discovery", "requires": "border shroud state"},
	"fog.shroud": {"subsystem": "vision-and-discovery", "requires": "a fog-of-war grid to shroud"},
	"fog.undo_permanent_reveal": {"subsystem": "vision-and-discovery", "requires": "permanent-reveal bookkeeping to undo"},
	# --- meta ---------------------------------------------------------------
	"meta.banner_pressed": {"subsystem": "living-world", "requires": "campaign banner UI events"},
	"meta.declare_local_defeat": {"subsystem": "match-lifecycle-controls", "requires": "per-player defeat declaration distinct from match resolution"},
	"meta.exit_map": {"subsystem": "match-lifecycle-controls", "requires": "a scripted map-exit path"},
	"meta.game_mode": {"subsystem": "match-config-metadata", "requires": "the match's mode token told to the sim at setup"},
	"meta.living_world_command": {"subsystem": "living-world", "requires": "the LivingWorld strategic layer (11-action family funneled through one command)"},
	"meta.living_world_query": {"subsystem": "living-world", "requires": "LivingWorld region state to read"},
	"meta.mission_attempts": {"subsystem": "match-config-metadata", "requires": "an attempt counter carried across mission restarts"},
	"meta.set_time_frozen": {"subsystem": "match-lifecycle-controls", "requires": "a freeze design that does not freeze the script layer with it - 005bcd8's deadlock finding, not missing state (clock_paused exists)"},
	"meta.zone_focus_more_than": {"subsystem": "living-world", "requires": "campaign zone-focus state"},
	# --- orders -------------------------------------------------------------
	"orders.apply_attack_priority_set": {"subsystem": "attack-priorities", "requires": "named priority sets applicable per subject"},
	"orders.attack_area": {"subsystem": "map-geometry", "requires": "trigger areas as attack targets (plus a timed area-attack order)"},
	"orders.attack_follow_waypoints": {"subsystem": "map-geometry", "requires": "waypoint paths plus an attack-along-path order"},
	"orders.can_path_to": {"subsystem": "map-geometry", "requires": "a pathability query against the real route provider"},
	"orders.distance_between": {"subsystem": "spatial-queries", "requires": "a sourced distance semantic between team/object subjects (nearest member? centroid?) before any number is answered"},
	"orders.face": {"subsystem": "order-verbs", "requires": "a facing order (and target geometry for waypoint variants)"},
	"orders.fire_weapon_following_path": {"subsystem": "map-geometry", "requires": "waypoint paths plus a fire-along-path order"},
	"orders.follow_waypoint_path": {"subsystem": "map-geometry", "requires": "waypoint paths (exact/formation variant included)"},
	"orders.guard": {"subsystem": "order-verbs", "requires": "a guard order type with targets and durations - the whole *_GUARD* family lands here"},
	"orders.guard_area_from_position": {"subsystem": "order-verbs", "requires": "a two-location guard order plus area geometry"},
	"orders.hunt": {"subsystem": "order-verbs", "requires": "a hunt order type (TEAM_HUNT is 6 AI sites; command-button variant also needs abilities)"},
	"orders.idle_for_ticks": {"subsystem": "order-verbs", "requires": "a timed idle order"},
	"orders.in_alt_formation": {"subsystem": "object-name-registry", "requires": "a BATTALION in the object namespace. Formation state is modelled - on battalions. Every entry the shared object/unit-reference namespace holds is a base flag or a structure, and the retail AI corpus never authors a battalion in an object-name slot (all 885 object-name arguments across the shipped libraries are flags, markers or references to bases/buildings), so serving this would resolve to a structure and answer about formation state no structure has"},
	"orders.issued_formation_order": {"subsystem": "event-ledger", "requires": "per-player order-event records"},
	"orders.move_home": {"subsystem": "order-verbs", "requires": "a home anchor per team plus a move-home order"},
	"orders.reached_waypoints_end": {"subsystem": "map-geometry", "requires": "waypoint paths plus per-subject path-progress state"},
	"orders.set_auto_ability": {"subsystem": "command-button-abilities", "requires": "auto-cast toggles per command button"},
	"orders.set_stopping_distance": {"subsystem": "order-verbs", "requires": "per-subject stopping-distance state the movement rules consult"},
	"orders.use_command_button": {"subsystem": "command-button-abilities", "requires": "command-button -> ability execution with target resolution (~20-action family)"},
	"orders.use_command_button_partial": {"subsystem": "command-button-abilities", "requires": "ability execution for a counted subset of a team (plus the signature's REAL-fraction defect from 005bcd8)"},
	# --- players ------------------------------------------------------------
	"players.add_rank_level": {"subsystem": "player-progression-state", "requires": "player rank levels"},
	"players.add_skill_points": {"subsystem": "player-progression-state", "requires": "player skill points"},
	"players.base_count": {"subsystem": "base-building-ai", "requires": "a notion of distinct bases per player"},
	"players.change_light_point_level": {"subsystem": "player-progression-state", "requires": "light-point levels"},
	"players.eva_event_played_within": {"subsystem": "event-ledger", "requires": "EVA event timestamps per player (plus the seconds-vs-ticks defect from 005bcd8)"},
	"players.exit_all_buildings": {"subsystem": "garrison-transport-capture", "requires": "garrisoned units to exit"},
	"players.force_emotion": {"subsystem": "team-behavior-state", "requires": "an EmotionTracker model: the tracker is undecompiled in the BFME research tree and parse-only in OpenSAGE, the EMOTION parameter's integer ordinals are established by neither source, and retail emotions are logic-affecting (RUN_AWAY_PANIC, PreventPlayerCommands) - serving any authored integer would guess which emotion it names. The facet's duration_ticks int is also a reported defect: the sourced action carries REAL seconds"},
	"players.give_light_points": {"subsystem": "player-progression-state", "requires": "light points"},
	"players.has_discovered": {"subsystem": "vision-and-discovery", "requires": "per-player discovery records"},
	"players.has_prerequisite_to_build": {"subsystem": "production-controls", "requires": "tech-tree prerequisite evaluation per object type"},
	"players.home_base": {"subsystem": "base-building-ai", "requires": "home-base identity per player"},
	"players.is_in_planning_mode": {"subsystem": "match-config-metadata", "requires": "planning-mode state"},
	"players.light_points": {"subsystem": "player-progression-state", "requires": "light points"},
	"players.lost_object_type": {"subsystem": "event-ledger", "requires": "loss records keyed by object type"},
	"players.object_count_with_model_condition": {"subsystem": "object-type-identity", "requires": "model-condition flags countable per player (no model-condition state machine exists in the sim)"},
	"players.object_count_within_distance": {"subsystem": "spatial-queries", "requires": "type-filtered distance census from a named origin"},
	"players.override_command_points": {"subsystem": "production-controls", "requires": "a per-team command-point cap override (the cap is global today)"},
	"players.power_consumed": {"subsystem": "economy-model-extras", "requires": "a power model (Generals-era vocabulary; needs sourcing that BFME2 uses it at all)"},
	"players.power_produced": {"subsystem": "economy-model-extras", "requires": "a power model (Generals-era vocabulary; needs sourcing that BFME2 uses it at all)"},
	"players.rank_level": {"subsystem": "player-progression-state", "requires": "player rank levels"},
	"players.reached_level_cap": {"subsystem": "player-progression-state", "requires": "player rank levels with caps"},
	"players.repair_structure": {"subsystem": "production-controls", "requires": "a structure repair path (cost, rate) plus the name binding"},
	"players.reset_light_points": {"subsystem": "player-progression-state", "requires": "light points"},
	"players.select_skill_set": {"subsystem": "player-progression-state", "requires": "skill sets"},
	"players.sell_everything": {"subsystem": "production-controls", "requires": "a sell/refund path over everything owned"},
	"players.set_auto_build_enabled": {"subsystem": "production-controls", "requires": "an auto-build loop to toggle"},
	"players.set_base_construction_enabled": {"subsystem": "production-controls", "requires": "a base-construction gate the AI build loop consults"},
	"players.set_base_construction_speed": {"subsystem": "production-controls", "requires": "a construction-speed factor"},
	"players.set_excluded_from_score_screen": {"subsystem": "economy-model-extras", "requires": "a score screen to exclude from"},
	"players.set_factories_enabled": {"subsystem": "production-controls", "requires": "a factory production gate"},
	"players.set_max_spell_points": {"subsystem": "player-progression-state", "requires": "a mutable spell-point cap"},
	"players.set_rank_level": {"subsystem": "player-progression-state", "requires": "player rank levels"},
	"players.set_rank_level_limit": {"subsystem": "player-progression-state", "requires": "player rank levels with caps"},
	"players.set_relation_to": {"subsystem": "diplomacy-overrides", "requires": "mutable player-to-player relations (the read is backed; the write would contradict the fixed alliance rule)"},
	"players.set_unit_construction_enabled": {"subsystem": "production-controls", "requires": "per-type production gates"},
	"players.start_position": {"subsystem": "match-config-metadata", "requires": "start-spot indices told to the sim at setup - START_POSITION_IS is 8 AI sites"},
	"players.threat_at": {"subsystem": "spatial-queries", "requires": "threat finders (RotWK-only vocabulary)"},
	"players.transfer_ownership": {"subsystem": "entity-lifecycle-api", "requires": "a wholesale ownership-transfer API honoring CP accounting"},
	"players.units_near_last_eva_event": {"subsystem": "event-ledger", "requires": "EVA event positions plus a radius census"},
	# --- progression --------------------------------------------------------
	"progression.experience_points": {"subsystem": "experience-model", "requires": "XP reads per unit/team scope (plus the name/team registries)"},
	"progression.gained_level": {"subsystem": "experience-model", "requires": "gained-level EDGE records per named object"},
	"progression.give_experience_levels": {"subsystem": "experience-model", "requires": "a public level-award API over the authored chains"},
	"progression.give_experience_points": {"subsystem": "experience-model", "requires": "a public XP-award API"},
	"progression.give_upgrade": {"subsystem": "progression-grant-api", "requires": "an outright upgrade grant distinct from build_upgrade's paid queue"},
	"progression.grant_science": {"subsystem": "progression-grant-api", "requires": "a free science grant path (purchase exists and is backed)"},
	"progression.has_object_of_veterancy": {"subsystem": "experience-model", "requires": "veterancy censuses (and the gap-registered type/comparison/level parameters - 21 AI sites)"},
	"progression.level": {"subsystem": "experience-model", "requires": "level reads per unit/team scope (any_hero_reached_rank shows the chains exist; the scoped read needs the registries)"},
	"progression.set_experience_points": {"subsystem": "experience-model", "requires": "a public XP-set API"},
	"progression.set_experience_receiving": {"subsystem": "experience-model", "requires": "an XP-receiving toggle the award path consults"},
	"progression.set_hero_experience_sharing": {"subsystem": "experience-model", "requires": "hero XP sharing"},
	"progression.set_max_level": {"subsystem": "experience-model", "requires": "per-object level caps"},
	"progression.set_science_availability": {"subsystem": "progression-grant-api", "requires": "per-science availability states the purchase path consults"},
	"progression.units_leveled_up": {"subsystem": "experience-model", "requires": "level-up counts per player"},
	"progression.upgrade_nearest_wall": {"subsystem": "walls-and-siege", "requires": "wall segments and markers (and the gap-registered five-argument shape with reference binding - 13 AI sites)"},
	# --- teams --------------------------------------------------------------
	"teams.adjust_priority": {"subsystem": "team-registry", "requires": "team priorities"},
	"teams.attacked_and_cannot_retaliate_count": {"subsystem": "event-ledger", "requires": "cannot-retaliate records per member"},
	"teams.build": {"subsystem": "team-registry", "requires": "team templates buildable through production"},
	"teams.collect_nearby": {"subsystem": "team-registry", "requires": "recruitment of nearby units into a team"},
	"teams.command_points_to_build": {"subsystem": "team-registry", "requires": "CP costing over a team template"},
	"teams.contained_count": {"subsystem": "garrison-transport-capture", "requires": "container membership per member"},
	"teams.count_with_kindof": {"subsystem": "object-type-identity", "requires": "KindOf bit sets on rows - the importer extracts no KindOf into playable documents (only one capability-evidence string), so there is nothing authored to consult; deriving bits from the category field would be invention"},
	"teams.delete": {"subsystem": "team-registry", "requires": "sub-player teams to delete (living-only variant included)"},
	"teams.enemy_sighted": {"subsystem": "vision-and-discovery", "requires": "sighting records per team"},
	"teams.enter_object": {"subsystem": "garrison-transport-capture", "requires": "container entry"},
	"teams.execute_sequential_script": {"subsystem": "sequential-scripts", "requires": "per-team sequential queues (24 AI sites drive every behaviour script through this)"},
	"teams.exit_all": {"subsystem": "garrison-transport-capture", "requires": "container exit (buildings-only variant included)"},
	"teams.force_emotion": {"subsystem": "team-behavior-state", "requires": "an EmotionTracker model (see players.force_emotion - same missing tracker, unestablished EMOTION ordinals, and the seconds-vs-ticks signature defect)"},
	"teams.harvest": {"subsystem": "economy-model-extras", "requires": "a harvesting loop"},
	"teams.leader": {"subsystem": "team-registry", "requires": "team leader designation"},
	"teams.members": {"subsystem": "team-registry", "requires": "membership lists carrying script names (also needs the name registry to be useful)"},
	"teams.merge_into": {"subsystem": "team-registry", "requires": "sub-player teams with transferable membership"},
	"teams.panic": {"subsystem": "team-behavior-state", "requires": "the AI_PANIC state machine (retail: per-member flight along a WAYPOINT_PATH with logic-random wander offsets, the PANICKING model condition, save-persisted state) plus waypoint paths from map geometry - AND a facet signature fix: the sourced action is TEAM_PANIC(TEAM, WAYPOINT_PATH) and this method has no slot for the path, so serving it would drop where the team flees to"},
	"teams.recruit": {"subsystem": "team-registry", "requires": "recruitment (and the gap-registered type-list parameter plus the INT-semantic ruling)"},
	"teams.recruit_combo_units": {"subsystem": "team-registry", "requires": "combo-horde recruitment"},
	"teams.remove_all_override_relations": {"subsystem": "diplomacy-overrides", "requires": "override relation storage"},
	"teams.remove_override_relation": {"subsystem": "diplomacy-overrides", "requires": "override relation storage"},
	"teams.repair_nearest": {"subsystem": "order-verbs", "requires": "a repair-nearest order"},
	"teams.set_ai_recruitable": {"subsystem": "team-registry", "requires": "an AI-recruitable flag per team"},
	"teams.set_attitude": {"subsystem": "team-behavior-state", "requires": "mood CONSUMPTION, not storage: the sourced write is a one-shot broadcast of the raw AI_MOOD int onto each member (no clamp; the authored -3 is stored verbatim, treated as NORMAL by the mood matrix default arm while raw >=AI_NORMAL comparisons fail), but retail consumes it through the AIUpdate mood matrix - idle-acquire modes per mood, move-to-attack-move conversion for ALERT/AGGRESSIVE, INI-authored range adjustments - which the sim does not model; a store-only serve would return OK and behave nothing like retail"},
	"teams.set_available_for_recruitment": {"subsystem": "team-registry", "requires": "recruitment availability per team"},
	"teams.set_close_range_weapon": {"subsystem": "entity-status-flags", "requires": "weapon-range toggles per member"},
	"teams.set_emoticon": {"subsystem": "entity-status-flags", "requires": "emoticon state per team"},
	"teams.set_flame_status": {"subsystem": "entity-status-flags", "requires": "flame status bits"},
	"teams.set_house_color_enabled": {"subsystem": "entity-status-flags", "requires": "house-color flags"},
	"teams.set_model_condition": {"subsystem": "entity-status-flags", "requires": "model-condition flags with durations"},
	"teams.set_nearest_unit_of_type_to_reference": {"subsystem": "spatial-queries", "requires": "nearest-of-type search anchored on a team, feeding the reference store"},
	"teams.set_needs_open_gate": {"subsystem": "walls-and-siege", "requires": "gate-interaction pathing flags"},
	"teams.set_object_panel_flag": {"subsystem": "entity-status-flags", "requires": "object panel flags"},
	"teams.set_override_relation_to_player": {"subsystem": "diplomacy-overrides", "requires": "team-to-player override relations"},
	"teams.set_override_relation_to_team": {"subsystem": "diplomacy-overrides", "requires": "team-to-team override relations"},
	"teams.set_player_override_relation_to_team": {"subsystem": "diplomacy-overrides", "requires": "player-to-team override relations"},
	"teams.set_reference": {"subsystem": "team-registry", "requires": "a TEAM_REF store (destination-first signature; see WP15)"},
	"teams.set_reference_to_nearest": {"subsystem": "spatial-queries", "requires": "nearest-team-of-type search (and the gap-registered anchor-team parameter)"},
	"teams.set_repulsor": {"subsystem": "entity-status-flags", "requires": "repulsor flags"},
	"teams.set_stealth_enabled": {"subsystem": "entity-status-flags", "requires": "stealth flags the vision/combat rules consult"},
	"teams.set_strict_control_enabled": {"subsystem": "entity-status-flags", "requires": "strict-control flags"},
	"teams.set_threat_level": {"subsystem": "spatial-queries", "requires": "MIS-ATTRIBUTED per WP09's finding: retail uses TEAM_THREAT_LEVEL as a radius-scoped CONDITION, not a set command; nothing should ever call this - kept annotated so the mis-attribution stays visible"},
	"teams.spin_for_ticks": {"subsystem": "order-verbs", "requires": "a timed spin/busy order"},
	"teams.stop_sequential_script": {"subsystem": "sequential-scripts", "requires": "stoppable per-team sequential queues"},
	"teams.threat": {"subsystem": "spatial-queries", "requires": "team threat evaluation (the AI's real usage carries a radius - gap-registered signature)"},
	"teams.transfer_to_player": {"subsystem": "team-registry", "requires": "team ownership transfer - 32 AI sites, ALL of them the multiplayer inherit path (ai_mp_inherit_management)"},
	"teams.wander": {"subsystem": "order-verbs", "requires": "a wander order (path and in-place variants)"},
	"teams.was_created": {"subsystem": "team-registry", "requires": "team creation EDGE records (level-triggered exists() must not serve this - it would refire one-shot AI init every evaluation)"},
	"teams.was_destroyed": {"subsystem": "team-registry", "requires": "team destruction edge records"},
	"teams.was_discovered": {"subsystem": "vision-and-discovery", "requires": "discovery records per team"},
	# --- terrain ------------------------------------------------------------
	"terrain.bridge_state": {"subsystem": "terrain-dynamics", "requires": "bridge objects with broken/repaired state"},
	"terrain.set_buildability": {"subsystem": "production-controls", "requires": "a mutable tech-tree buildability table"},
	"terrain.set_burn_rate": {"subsystem": "terrain-dynamics", "requires": "burn-rate state at waypoints/areas"},
	"terrain.set_cloud_speed": {"subsystem": "terrain-dynamics", "requires": "cloud state (presentation-adjacent but declared simulation by the vocabulary)"},
	"terrain.set_logic_fog_state": {"subsystem": "terrain-dynamics", "requires": "simulation-visible weather fog (distinct from fog of war)"},
	"terrain.set_water_height": {"subsystem": "terrain-dynamics", "requires": "water bodies with animatable heights"},
	"terrain.switch_border": {"subsystem": "terrain-dynamics", "requires": "map border sets"},
	# --- transport ----------------------------------------------------------
	"transport.capture_nearest_unowned": {"subsystem": "garrison-transport-capture", "requires": "capturable unowned faction units"},
	"transport.captured_unit_count": {"subsystem": "garrison-transport-capture", "requires": "capture records per player"},
	"transport.create_team_from_captured": {"subsystem": "garrison-transport-capture", "requires": "captured-unit rosters plus team creation"},
	"transport.garrison": {"subsystem": "garrison-transport-capture", "requires": "garrisonable buildings with occupancy"},
	"transport.garrisoned_count": {"subsystem": "garrison-transport-capture", "requires": "garrison occupancy counts"},
	"transport.has_toggled_weapon": {"subsystem": "entity-status-flags", "requires": "weapon-mode toggle state per object"},
	"transport.load_transports": {"subsystem": "garrison-transport-capture", "requires": "transport containers with capacity"},
	"transport.passenger_count": {"subsystem": "garrison-transport-capture", "requires": "passenger counts per container"},
	"transport.teleport_to": {"subsystem": "entity-lifecycle-api", "requires": "a position-write API that re-validates collision/formation state"},
	# --- units --------------------------------------------------------------
	"units.build_structure_at": {"subsystem": "map-geometry", "requires": "waypoint targets for builder construction (builder production exists)"},
	"units.create_delayed_carryover_at": {"subsystem": "hero-revival-carryover", "requires": "a delayed-carryover ledger plus waypoint spawn anchors"},
	"units.create_object": {"subsystem": "entity-lifecycle-api", "requires": "a public spawn API - the sim's spawn path is private and a raw insert would skip CP accounting and corpse scheduling (005bcd8)"},
	"units.create_on_team_at": {"subsystem": "entity-lifecycle-api", "requires": "a public spawn API plus team placement targets"},
	"units.create_revival_entry": {"subsystem": "hero-revival-carryover", "requires": "a revival ledger with levels and carryover sourcing"},
	"units.delete": {"subsystem": "entity-lifecycle-api", "requires": "a public despawn API honoring CP release (plus the name binding)"},
	"units.delete_all_unmanned": {"subsystem": "entity-lifecycle-api", "requires": "unmanned-object identification plus a despawn API"},
	"units.deploy_siege": {"subsystem": "walls-and-siege", "requires": "siege attachment to walls/waypoints"},
	"units.destroy_all_contained": {"subsystem": "garrison-transport-capture", "requires": "container contents to destroy"},
	"units.destroyed_by_object_type": {"subsystem": "event-ledger", "requires": "killer-type records per named object"},
	"units.enemy_sighted": {"subsystem": "vision-and-discovery", "requires": "sighting records per object"},
	"units.enter_object": {"subsystem": "garrison-transport-capture", "requires": "container entry per named pair"},
	"units.execute_sequential_script": {"subsystem": "sequential-scripts", "requires": "per-unit sequential queues"},
	"units.exit": {"subsystem": "garrison-transport-capture", "requires": "container exit"},
	"units.exit_specific_building": {"subsystem": "garrison-transport-capture", "requires": "targeted container exit"},
	"units.force_emotion": {"subsystem": "team-behavior-state", "requires": "an EmotionTracker model (see players.force_emotion), plus the object-name registry for the unit spelling"},
	"units.gate_is_open": {"subsystem": "walls-and-siege", "requires": "gate state per named structure"},
	"units.has_delayed_carryover_of_type": {"subsystem": "hero-revival-carryover", "requires": "carryover ledger queries by type"},
	"units.has_object_status": {"subsystem": "entity-status-flags", "requires": "object-status bit reads per scope"},
	"units.is_building_empty": {"subsystem": "garrison-transport-capture", "requires": "occupancy per named building"},
	"units.is_selected": {"subsystem": "sim-selection-model", "requires": "a lockstep selection notion - sim.selected_ids is per-seat presentation state and would desync (005bcd8)"},
	"units.is_totally_dead": {"subsystem": "object-name-registry", "requires": "an object-REMOVAL edge. Retail reads the name-table pointer having gone NULL (evaluateNamedUnitTotallyDead: the object left the world), which is why its sibling is_dying - pointer alive, object effectively dead - IS served. This simulation never removes a destroyed structure row: it keeps it at health 0 forever, so the pointer-NULL state never occurs and the method could only ever answer a permanent false, which would strand any script gated on NAMED_TOTALLY_DEAD"},
	"units.is_webbed": {"subsystem": "entity-status-flags", "requires": "web status"},
	"units.retract_siege": {"subsystem": "walls-and-siege", "requires": "siege detachment"},
	"units.set_attitude": {"subsystem": "team-behavior-state", "requires": "mood consumption (see teams.set_attitude) plus the object-name registry for the named spelling"},
	"units.set_cave_index": {"subsystem": "garrison-transport-capture", "requires": "cave/tunnel network membership"},
	"units.set_close_range_weapon": {"subsystem": "entity-status-flags", "requires": "weapon-range toggles"},
	"units.set_emoticon": {"subsystem": "entity-status-flags", "requires": "emoticon state"},
	"units.set_flame_status": {"subsystem": "entity-status-flags", "requires": "flame status"},
	"units.set_gate_state": {"subsystem": "walls-and-siege", "requires": "gate open/close/ready state (GATE_OPEN and GATE_CLOSE each have one AI call site)"},
	"units.set_held": {"subsystem": "entity-status-flags", "requires": "held (movement-frozen) status"},
	"units.set_house_color_enabled": {"subsystem": "entity-status-flags", "requires": "house-color flags"},
	"units.set_hulk_lifetime": {"subsystem": "entity-lifecycle-api", "requires": "corpse lifetime overrides in the corpse flow"},
	"units.set_model_condition": {"subsystem": "entity-status-flags", "requires": "model-condition flags with durations"},
	"units.set_object_panel_flag": {"subsystem": "entity-status-flags", "requires": "object panel flags"},
	"units.set_object_status": {"subsystem": "entity-status-flags", "requires": "object-status bit writes per scope"},
	"units.set_repulsor": {"subsystem": "entity-status-flags", "requires": "repulsor flags"},
	"units.set_selected": {"subsystem": "sim-selection-model", "requires": "a lockstep selection notion to write"},
	"units.set_special_weaponset": {"subsystem": "entity-status-flags", "requires": "weaponset switching"},
	"units.set_stealth_enabled": {"subsystem": "entity-status-flags", "requires": "stealth flags"},
	"units.set_strict_control_enabled": {"subsystem": "entity-status-flags", "requires": "strict-control flags"},
	"units.set_team": {"subsystem": "team-registry", "requires": "sub-player teams a named object can join"},
	"units.set_topple_direction": {"subsystem": "entity-status-flags", "requires": "topple physics parameters"},
	"units.set_warehouse_value": {"subsystem": "economy-model-extras", "requires": "warehouse objects with values"},
	"units.shock": {"subsystem": "entity-status-flags", "requires": "a timed shock/stun status"},
	"units.siege_is_attached_to_wall": {"subsystem": "walls-and-siege", "requires": "siege attachment state"},
	"units.skill_points": {"subsystem": "experience-model", "requires": "skill points per named object"},
	"units.spawn_at": {"subsystem": "entity-lifecycle-api", "requires": "a public spawn API with name+orientation"},
	"units.stance": {"subsystem": "object-name-registry", "requires": "a BATTALION in the object namespace. Stances are modelled (issue_set_stance exists) - on battalions; the namespace holds only base flags and structures (see orders.in_alt_formation for the corpus evidence), which have no stance to read"},
	"units.stop": {"subsystem": "object-name-registry", "requires": "a BATTALION in the object namespace. Stop orders are modelled (issue_stop exists) - on battalions; the namespace holds only base flags and structures (see orders.in_alt_formation for the corpus evidence), which have no order queue to flush, so every call would be a silent no-op returning true"},
	"units.stop_sequential_script": {"subsystem": "sequential-scripts", "requires": "stoppable per-unit sequential queues"},
	"units.threat": {"subsystem": "spatial-queries", "requires": "per-object threat evaluation (the AI's real usage carries a radius)"},
	"units.transfer_ownership": {"subsystem": "entity-lifecycle-api", "requires": "a single-object ownership transfer honoring CP accounting"},
	"units.type_is_selected": {"subsystem": "sim-selection-model", "requires": "a lockstep selection notion queryable by type"},
	"units.type_was_sighted": {"subsystem": "vision-and-discovery", "requires": "sighted-type records per player"},
	"units.unowned_faction_unit_exists": {"subsystem": "object-type-identity", "requires": "a sourced semantic for 'unowned faction unit': the sim now owns neutral (capturable) and creep teams, but nothing pins whether retail means neutral-owned UNITS only or capturable structures too, and the readings diverge exactly when neutral structures exist"},
	"units.was_discovered": {"subsystem": "vision-and-discovery", "requires": "discovery records per object"},
	# --- base world ---------------------------------------------------------
}

## Argument shapes that still refuse on BACKED methods. A backed method is not
## a finished method; routing attributes call sites that need a refused shape
## to the subsystem that blocks that shape.
const RESTRICTIONS := {
	"ai.base_unpack": "acts as the bound script player (the sourced action carries no player); base flags of the sim's unpackable-base table only",
	"ai.base_unpackable": "base flags of the sim's unpackable-base table only; the player must resolve (a bound name, or '<This Player>' with a script player bound)",
	"ai.build_base_building": "acts as the bound script player; building types with an expansion rule and bases reachable through the shared object/unit-reference namespace only",
	"combat.fire_special_power": "PLAYER scope, spellbook powers, explicit POSITION targets only",
	"combat.player_all_destroyed": "full variant only; build_facilities_only refuses (no build-facility classification)",
	"combat.special_power_ready": "PLAYER scope, powers of the match's spellbook document only",
	"economy.money": "bound players only (facet path refuses unbound; the pre-facet path cannot)",
	"meta.multiplayer_outcome": "defeat/allied_victory/allied_defeat tokens only",
	"meta.object_list_change": "non-empty list and type names only (\"\" names nothing in the retail vocabulary); set semantics, duplicate adds and absent removes succeed as retail no-ops",
	"orders.attack": "TEAM/PLAYER scope attacking a bound TEAM target only",
	"orders.attack_move_to": "TEAM/PLAYER scope with explicit POSITION targets only",
	"orders.move_to": "TEAM/PLAYER scope with POSITION or NEAREST_TYPE targets; UNIT scope needs object-name-registry; a NEAREST_TYPE naming only types the sim cannot field refuses (retail's authored targets are mostly map-placed markers, but also plot flags, treasure and real siege unit types - the refusal is about fieldability, not about markers), and waypoint/area targets need map geometry",
	"players.object_count_of_types": "the player must resolve (bound names, '<This Player>', the plural enemies/allies aggregate tokens - aggregates SUM); the singular '<This Player's Enemy>' token refuses (no current-enemy model); creep-guard battalions and legacy synthetic ids without recorded provenance are not countable",
	"orders.stand_ground": "TEAM/PLAYER scope, engage only - the vocabulary's boolean CLEAR is inexpressible (facet-signature-packet)",
	"players.building_count": "empty class (count everything) or a structure kind this sim models",
	"progression.has_science": "sciences of the GLOBAL spellbook tree only; per-team overrides refuse",
	"progression.has_upgrade": "PLAYER scope, modelled upgrade ids only",
	"players.can_build_at_base": "the player must resolve ('<This Player>' included); bases through the shared object namespace; a non-empty object type must have an expansion rule (false for an unmodeled type would be a guess)",
	"progression.unit_count_with_upgrade": "modelled upgrade ids only (zero for an unknown id would be a guess)",
	"teams.custom_state": "bound team names only; '<This Team>' - the spelling retail authors at essentially every call site - refuses until an executing-team (sequential-script) context exists, and the script player's whole roster would be the wrong team",
	"teams.set_custom_state": "bound team names only (the '<This Team>' restriction above); an empty token refuses; writes refuse after match resolution",
	"teams.set_state": "bound team names only (the '<This Team>' restriction above); writes refuse after match resolution",
	"teams.state": "bound team names only (the '<This Team>' restriction above); an unbound name refuses rather than reproducing retail's nonexistent-team false (unbound is not proof of nonexistence while the sub-player team registry is unmodeled)",
	"teams.stop": "disband=false only (no disband model)",
	"units.exists": "names the shared object / unit-reference namespace holds (base flags, and references bound to a flag or a structure) only - an unknown name REFUSES rather than borrowing retail's false, which is grounded in a complete name table this namespace is a subset of",
	"units.health_percent": "names the shared object / unit-reference namespace holds (base flags, and references bound to a flag or a structure) only - an unknown name REFUSES rather than borrowing retail's false, which is grounded in a complete name table this namespace is a subset of; and a name that resolves to a LIVE STRUCTURE - a packed base flag refuses, because the flag row's health is the fortress it would unpack into, not the flag object's own",
	"units.is_dying": "names the shared object / unit-reference namespace holds (base flags, and references bound to a flag or a structure) only - an unknown name REFUSES rather than borrowing retail's false, which is grounded in a complete name table this namespace is a subset of. Answers retail's pointer-alive-but-effectively-dead reading (a structure row at health 0); its sibling is_totally_dead refuses, see the blocked table",
	"units.owner": "names the shared object / unit-reference namespace holds (base flags, and references bound to a flag or a structure) only - an unknown name REFUSES rather than borrowing retail's false, which is grounded in a complete name table this namespace is a subset of; and a name that resolves to a LIVE STRUCTURE whose team has a bound script player name. A packed base flag refuses: retail's flag object belongs to the neutral player, which this simulation models as no team and for which no script player name exists",
	"units.position": "names the shared object / unit-reference namespace holds (base flags, and references bound to a flag or a structure) only - an unknown name REFUSES rather than borrowing retail's false, which is grounded in a complete name table this namespace is a subset of; a name whose object is gone refuses (a removed object has no position, and the last-known reading would be an invention)",
	"units.set_reference": "names the shared object / unit-reference namespace holds (base flags, and references bound to a flag or a structure) only - an unknown name REFUSES rather than borrowing retail's false, which is grounded in a complete name table this namespace is a subset of as the SOURCE; the destination may not shadow a base flag, and the world must have a script player bound (references are stored per script player). The source is resolved NOW and stored as a handle - a flag name for a flag, a structure id for a structure - never as the source string",
	"units.was_created": "names the shared object / unit-reference namespace holds (base flags, and references bound to a flag or a structure) only - an unknown name REFUSES rather than borrowing retail's false, which is grounded in a complete name table this namespace is a subset of. Retail's NAMED_CREATED is literally getUnitNamed() != NULL (the engine's own todo admits the member is misnamed), so this does NOT consult the dead flag - unlike exists()",
	"units.was_destroyed": "names the shared object / unit-reference namespace holds (base flags, and references bound to a flag or a structure) only - an unknown name REFUSES rather than borrowing retail's false, which is grounded in a complete name table this namespace is a subset of. Retail's hybrid: an object still present answers its effectively-dead flag, an object gone answers true. A packed base flag answers false - the flag object is alive",
	"units.has_command_points_to_build": "the player must resolve ('<This Player>' included); unit types with a production rule only (an unmodeled type's cost is unknowable, so it refuses)",
	"world.player_money": "no refusal channel: an UNBOUND player reads 0 through this legacy path (reported base-class defect; economy.money is the honest surface)",
}

## Retail-AI census member -> world route. See the class comment for how the
## mapping was established and what the confidence labels mean.
const VOCABULARY_ROUTING := {
	# --- env: served entirely by SageScriptEnv, no world surface involved ---
	"CALL_SUBROUTINE": {"route": "env", "worldMethods": [], "mappingSource": "handler", "note": "needs a subroutine_runner installed on the env, not a world method"},
	"CONDITION_TRUE": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"COUNTER_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"COUNTER_MATH_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"COUNTER_MATH_VALUE": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"DECREMENT_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"DISABLE_SCRIPT": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"ENABLE_SCRIPT": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"FLAG": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"INCREMENT_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"NO_OP": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"SET_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"SET_COUNTER_TO_COUNTER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"SET_FLAG": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"SET_MILLISECOND_TIMER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"SET_TIMER": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	"TIMER_EXPIRED": {"route": "env", "worldMethods": [], "mappingSource": "handler"},
	# --- backed: routes only to methods the probe saw answer ----------------
	"AI_PLAYER_BUILD_UPGRADE": {"route": "backed", "worldMethods": ["progression.build_upgrade"], "mappingSource": "handler"},
	"DEBUG_STRING": {"route": "backed", "worldMethods": ["world.debug_message"], "mappingSource": "handler"},
	"PLAYER_CAN_PURCHASE_SCIENCE": {"route": "backed", "worldMethods": ["progression.can_purchase_science"], "mappingSource": "handler"},
	"PLAYER_HAS_CREDITS": {"route": "backed", "worldMethods": ["economy.money"], "mappingSource": "declared"},
	"PLAYER_PURCHASE_SCIENCE": {"route": "backed", "worldMethods": ["progression.purchase_science"], "mappingSource": "handler"},
	"SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER": {"route": "backed", "worldMethods": ["players.command_points_available"], "mappingSource": "handler"},
	"SET_PLAYER_MONEY_TO_COUNTER": {"route": "backed", "worldMethods": ["world.set_player_money"], "mappingSource": "handler"},
	"SKIRMISH_PLAYER_FACTION": {"route": "backed", "worldMethods": ["players.faction"], "mappingSource": "handler"},
	"TEAM_HAS_UNITS": {"route": "backed", "worldMethods": ["teams.unit_count"], "mappingSource": "handler"},
	"TEAM_STOP": {"route": "backed", "worldMethods": ["teams.stop"], "mappingSource": "handler"},
	"BUILD_BASE_BUILDING": {"route": "backed", "worldMethods": ["ai.build_base_building"], "mappingSource": "handler", "note": "served through the corrected (building_type, base, result_reference) signature"},
	"CAN_BUILD_AT_BASE": {"route": "backed", "worldMethods": ["players.can_build_at_base"], "mappingSource": "handler", "note": "served through the corrected base-carrying signature; empty object_type is the 'anything at all' form"},
	"CAN_BUILD_OBJECTTYPE_AT_BASE": {"route": "backed", "worldMethods": ["players.can_build_at_base"], "mappingSource": "handler"},
	"NAMED_BASE_UNPACK": {"route": "backed", "worldMethods": ["ai.base_unpack"], "mappingSource": "handler", "note": "served through the corrected signature carrying the UNIT_REF destination"},
	"NAMED_BASE_UNPACKABLE_FOR_PLAYER": {"route": "backed", "worldMethods": ["ai.base_unpackable"], "mappingSource": "handler"},
	"NAMED_BASE_UNPACK_FREE": {"route": "backed", "worldMethods": ["ai.base_unpack"], "mappingSource": "handler"},
	# --- blocked: waiting on a subsystem (and sometimes a signature fix) ----
	"BUILD_BASE_BUILDING_PER_TACTICAL_MARKER": {"route": "blocked", "worldMethods": ["ai.build_base_building"], "blockingSubsystem": "base-building-ai", "signatureGap": true, "mappingSource": "handler", "note": "still needs its own facet method (near/far side + marker type) plus tactical-marker sim state"},
	"CREATE_OBJECT": {"route": "blocked", "worldMethods": ["units.create_object"], "blockingSubsystem": "entity-lifecycle-api", "mappingSource": "declared"},
	"CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION": {"route": "blocked", "worldMethods": ["ai.create_reinforcement_team"], "blockingSubsystem": "base-building-ai", "mappingSource": "handler"},
	"EVAL_TEAM_HEALTH": {"route": "blocked", "worldMethods": ["combat.team_health_percent"], "blockingSubsystem": "adapter-only", "mappingSource": "declared"},
	"GATE_CLOSE": {"route": "blocked", "worldMethods": ["units.set_gate_state"], "blockingSubsystem": "walls-and-siege", "mappingSource": "declared"},
	"GATE_OPEN": {"route": "blocked", "worldMethods": ["units.set_gate_state"], "blockingSubsystem": "walls-and-siege", "mappingSource": "declared"},
	"NAMED_NOT_DESTROYED": {"route": "backed", "worldMethods": ["units.was_destroyed"], "mappingSource": "declared", "note": "the world method answers, but only for names the shared namespace holds. Measured on the shipped libraries, this member's 23 authored arguments are AI_GATE (6), AI_BASE (3), AI_FARM_LAST_BUILT (2), BASE_FLAG_1..8 (8), three AI_HERO_* and START_SPAWN: the flags and AI_BASE (bound by the backed NAMED_BASE_UNPACK_FREE) resolve today, while AI_GATE and the heroes are bound by SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER and AI_FARM_LAST_BUILT by BUILD_BASE_BUILDING_PER_TACTICAL_MARKER - both still blocked, so those names have nothing to resolve to yet"},
	"NAMED_OWNED_BY_PLAYER": {"route": "blocked", "worldMethods": ["units.owner"], "blockingSubsystem": "facet-signature-packet", "signatureGap": true, "mappingSource": "handler", "note": "units.owner IS served now - but this member does not reach it. EVERY authored call site spells the PLAYER argument '<This Player>' (48 of 48 in the shipped libraries, measured), and the handler consumes the comparison itself, so it refuses the placeholder rather than answering a confident wrong false against a literal. The registry is no longer what blocks this member; the missing world-side signature is. NEEDED: units.is_owned_by(object_name: String, player: String) -> SageWorldQuery, with world-side token resolution - the exact signature wp16_ai_units.gd already gap-registers"},
	"NAMED_USE_COMMANDBUTTON_ABILITY": {"route": "blocked", "worldMethods": ["orders.use_command_button"], "blockingSubsystem": "command-button-abilities", "mappingSource": "declared"},
	"PLAYER_DESTROYED_N_BUILDINGS_PLAYER": {"route": "blocked", "worldMethods": ["combat.buildings_destroyed_by"], "blockingSubsystem": "event-ledger", "mappingSource": "declared"},
	"PLAYER_ENABLE_BASE_CONSTRUCTION": {"route": "blocked", "worldMethods": ["players.set_base_construction_enabled"], "blockingSubsystem": "production-controls", "mappingSource": "handler"},
	"PLAYER_ENABLE_UNIT_CONSTRUCTION": {"route": "blocked", "worldMethods": ["players.set_unit_construction_enabled"], "blockingSubsystem": "production-controls", "mappingSource": "declared"},
	"PLAYER_HAS_OBJECT_COMPARISON": {"route": "backed", "worldMethods": ["players.object_count_of_types"], "mappingSource": "handler", "note": "the formerly heaviest blocked member (100 call sites), served through the sim's object-type identity census; aggregate player tokens sum"},
	"PLAYER_HAS_OBJECT_OF_VETERANCY": {"route": "blocked", "worldMethods": ["progression.has_object_of_veterancy"], "blockingSubsystem": "experience-model", "signatureGap": true, "mappingSource": "handler"},
	"PLAYER_SELL_EVERYTHING": {"route": "blocked", "worldMethods": ["players.sell_everything"], "blockingSubsystem": "production-controls", "mappingSource": "handler"},
	"SET_COUNTER_TO_TEAM_THREAT": {"route": "blocked", "worldMethods": ["teams.threat"], "blockingSubsystem": "spatial-queries", "signatureGap": true, "mappingSource": "handler"},
	"SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER": {"route": "backed", "worldMethods": ["players.object_count_of_types"], "mappingSource": "handler"},
	"SET_RANDOM_COUNTER": {"route": "backed", "worldMethods": ["world.random_int"], "mappingSource": "handler", "note": "served by the sim-owned retail GameLogic stream (logic_random_int); the spell-list-choice gate c930b68 measured"},
	"SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER": {"route": "blocked", "worldMethods": ["teams.set_reference_to_nearest"], "blockingSubsystem": "spatial-queries", "signatureGap": true, "mappingSource": "handler"},
	"SET_TEAM_REFERENCE": {"route": "blocked", "worldMethods": ["teams.set_reference"], "blockingSubsystem": "team-registry", "mappingSource": "handler"},
	"SET_UNIT_REFERENCE": {"route": "backed", "worldMethods": ["units.set_reference"], "mappingSource": "declared", "note": "measured on the shipped libraries, all 40 authored SOURCE arguments are map-placed markers: BASE_FLAG_1..16 (32 sites, which the simulation's unpackable-base table models and which bind) and BASE_SPAWN_1..8 (8 sites, which it does not model and which refuse per-argument)"},
	"SET_UNIT_REFERENCE_TO_REFERENCE": {"route": "backed", "worldMethods": ["units.set_reference"], "mappingSource": "declared", "note": "all 16 authored sites copy AI_EXPANSION_1..16 into AI_CURRENT_CONSTRUCTION_SITE; AI_EXPANSION_n is bound by NAMED_BASE_UNPACK, which is already backed, so this chain resolves end to end"},
	"START_POSITION_IS": {"route": "blocked", "worldMethods": ["players.start_position"], "blockingSubsystem": "match-config-metadata", "mappingSource": "handler"},
	"TEAM_CHANGE_OBJECT_STATUS": {"route": "blocked", "worldMethods": ["units.set_object_status"], "blockingSubsystem": "entity-status-flags", "mappingSource": "declared"},
	"TEAM_CREATED": {"route": "blocked", "worldMethods": ["teams.was_created"], "blockingSubsystem": "team-registry", "mappingSource": "handler"},
	"TEAM_DELETE": {"route": "blocked", "worldMethods": ["teams.delete"], "blockingSubsystem": "team-registry", "mappingSource": "handler"},
	"TEAM_DESTROYED": {"route": "blocked", "worldMethods": ["teams.was_destroyed"], "blockingSubsystem": "team-registry", "mappingSource": "declared"},
	"TEAM_EXECUTE_SEQUENTIAL_SCRIPT": {"route": "blocked", "worldMethods": ["teams.execute_sequential_script"], "blockingSubsystem": "sequential-scripts", "mappingSource": "handler"},
	"TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING": {"route": "blocked", "worldMethods": ["teams.execute_sequential_script"], "blockingSubsystem": "sequential-scripts", "mappingSource": "handler", "note": "counts 0/1 are served by the handler, >=2 is a signature gap (boolean cannot express finite repeats); all 5 retail sites pass 0"},
	"TEAM_EXIT_ALL_BUILDINGS": {"route": "blocked", "worldMethods": ["teams.exit_all"], "blockingSubsystem": "garrison-transport-capture", "mappingSource": "handler"},
	"TEAM_GUARD_FOR_SECONDS": {"route": "blocked", "worldMethods": ["orders.guard"], "blockingSubsystem": "order-verbs", "mappingSource": "handler"},
	"TEAM_GUARD_TEAM": {"route": "blocked", "worldMethods": ["orders.guard"], "blockingSubsystem": "order-verbs", "mappingSource": "handler"},
	"TEAM_HAS_CUSTOM_STATE": {"route": "backed", "worldMethods": ["teams.custom_state"], "mappingSource": "handler", "note": "the value-shape ambiguity WP15 reported is RULED: the backed world answers the ARRAY of enabled tokens (the writer's boolean proves a set); the handler keeps its String tolerance for stub worlds"},
	"TEAM_HUNT": {"route": "blocked", "worldMethods": ["orders.hunt"], "blockingSubsystem": "order-verbs", "mappingSource": "handler"},
	"TEAM_IDLE_FOR_SECONDS": {"route": "blocked", "worldMethods": ["orders.idle_for_ticks"], "blockingSubsystem": "order-verbs", "mappingSource": "handler"},
	"TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL": {"route": "blocked", "worldMethods": ["teams.attacked_and_cannot_retaliate_count"], "blockingSubsystem": "event-ledger", "mappingSource": "declared"},
	"TEAM_LOAD_TRANSPORTS": {"route": "blocked", "worldMethods": ["transport.load_transports"], "blockingSubsystem": "garrison-transport-capture", "mappingSource": "handler"},
	"TEAM_MERGE_INTO_TEAM": {"route": "blocked", "worldMethods": ["teams.merge_into"], "blockingSubsystem": "team-registry", "mappingSource": "handler"},
	"TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE": {"route": "blocked", "worldMethods": ["orders.move_to"], "blockingSubsystem": "base-building-ai", "mappingSource": "handler", "note": "NEAREST_TYPE resolves over fieldable types; blocked because no target type in this census scope is fieldable yet. Targets are MOSTLY map-placed markers (CombatAreas, HighGround) but NOT exclusively - Economy_Flags (EconomyPlotFlag) and Treasure_Chests appear here too, and outside this scope lib_defense_behaviors targets Artillery_Units (GondorTrebuchet/IsengardBallista/MordorCatapult), real unit types this path WILL serve once fieldable. Do not read this as 'markers only'"},
	"TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER": {"route": "blocked", "worldMethods": ["orders.move_to"], "blockingSubsystem": "base-building-ai", "mappingSource": "handler", "note": "NEAREST_TYPE resolves including owner tokens; blocked because no target type in this census scope is fieldable yet. Predominantly marker types (Center/Flank/Backdoor nodes), but see the sibling member's note - the corpus also authors real unit-type targets, so the block is 'these types are not fieldable', not 'markers only'"},
	"TEAM_RECRUIT_UNITS": {"route": "blocked", "worldMethods": ["teams.recruit"], "blockingSubsystem": "team-registry", "signatureGap": true, "mappingSource": "handler"},
	"TEAM_RECRUIT_UNITS_FROM_TEAM": {"route": "blocked", "worldMethods": ["teams.recruit"], "blockingSubsystem": "team-registry", "signatureGap": true, "mappingSource": "handler"},
	"TEAM_SET_ATTITUDE": {"route": "blocked", "worldMethods": ["teams.set_attitude"], "blockingSubsystem": "team-behavior-state", "mappingSource": "handler"},
	"TEAM_SET_CUSTOM_STATE": {"route": "backed", "worldMethods": ["teams.set_custom_state"], "mappingSource": "handler", "note": "served through the corrected signature carrying the BOOLEAN enable flag WP15's gap registration demanded; the flag reads from the payload integer field, both polarities delivered un-inverted"},
	"TEAM_SET_STATE": {"route": "backed", "worldMethods": ["teams.set_state"], "mappingSource": "handler", "note": "retail's doSetTeamState is a bare Team::setState - storage IS the semantic; tokens are content-defined and stored unvalidated"},
	"TEAM_STAND_GROUND": {"route": "blocked", "worldMethods": ["orders.stand_ground"], "blockingSubsystem": "facet-signature-packet", "signatureGap": true, "mappingSource": "handler", "note": "the method is BACKED but can only engage; both retail sites author the CLEAR the signature cannot carry"},
	"TEAM_STATE_IS": {"route": "backed", "worldMethods": ["teams.state"], "mappingSource": "handler", "note": "exact case-sensitive comparison (AsciiString strcmp); the empty string is retail's default for a team never set"},
	"TEAM_STATE_IS_NOT": {"route": "backed", "worldMethods": ["teams.state"], "mappingSource": "handler", "note": "not composed as the negation of TEAM_STATE_IS: a refused read must stay false for BOTH spellings (retail answers false to both for a nonexistent team)"},
	"TEAM_STOP_SEQUENTIAL_SCRIPT": {"route": "blocked", "worldMethods": ["teams.stop_sequential_script"], "blockingSubsystem": "sequential-scripts", "mappingSource": "handler"},
	"TEAM_THREAT_LEVEL": {"route": "blocked", "worldMethods": ["teams.threat"], "blockingSubsystem": "spatial-queries", "signatureGap": true, "mappingSource": "handler", "note": "condition, not action - WP09's mis-attribution finding"},
	"TEAM_TRANSFER_TO_PLAYER": {"route": "blocked", "worldMethods": ["teams.transfer_to_player"], "blockingSubsystem": "team-registry", "mappingSource": "handler"},
	"TEAM_USE_COMMANDBUTTON_ABILITY": {"route": "blocked", "worldMethods": ["orders.use_command_button"], "blockingSubsystem": "command-button-abilities", "mappingSource": "declared"},
	"TYPE_SIGHTED": {"route": "blocked", "worldMethods": ["units.type_was_sighted"], "blockingSubsystem": "vision-and-discovery", "mappingSource": "declared"},
	"UNIT_HEALTH": {"route": "backed", "worldMethods": ["units.health_percent"], "mappingSource": "declared", "note": "the world method answers for a name resolving to a live structure. None of the 15 authored arguments measured in the shipped libraries resolves TODAY - two are AI_GATE and thirteen are AI_HERO_*, all bound by SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER, which is still blocked - so this is method-level backing, not call-site backing"},
	"UNIT_SET_TEAM": {"route": "blocked", "worldMethods": ["units.set_team"], "blockingSubsystem": "team-registry", "mappingSource": "handler"},
	"UNIT_THREAT_LEVEL": {"route": "blocked", "worldMethods": ["units.threat"], "blockingSubsystem": "spatial-queries", "signatureGap": true, "mappingSource": "handler", "note": "condition, not action - WP09's mis-attribution finding"},
	"UPGRADE_NEAREST_WALL": {"route": "blocked", "worldMethods": ["progression.upgrade_nearest_wall"], "blockingSubsystem": "walls-and-siege", "signatureGap": true, "mappingSource": "handler"},
}
