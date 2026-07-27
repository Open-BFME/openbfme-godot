class_name SageScriptWorld
extends RefCounted

## The interface a script handler is allowed to see of "the game".
##
## WHY THIS EXISTS
## ---------------
## The script vocabulary must be testable headless, long before most of the
## simulation exists, and must not drag the simulation into every test that
## touches a counter. Handlers therefore never import retail_slice_sim.gd (or
## any other concrete world); they take a SageScriptWorld. Two implementations
## are expected: SageScriptWorldStub for tests, and a thin adapter over the real
## simulation for the game.
##
##
## WHY THIS FILE IS BIG, AND WHY IT WAS WRITTEN IN ONE PASS
## ========================================================
## ~760 SAGE actions and conditions remain unimplemented. They are partitioned
## into 17 work packages, each of which is meant to be implemented independently
## and concurrently, with each package shipping exactly ONE new file under
## src/script/handlers/ plus its own test (see handlers/handlers_registry.gd).
##
## That only works if the world surface those packages need already exists.
## Seventeen agents each appending "just one method" to this file would collide
## on every merge. So the surface below was derived UP FRONT from the full
## handler catalog and covers all 17 packages. If you are implementing a work
## package and a method you need is missing, that is a finding to report, not a
## line to add: the gap is real and the owner needs to see it. Adding a method
## here is a deliberate, coordinated act.
##
##
## FACETS, NOT ONE FLAT INTERFACE
## ==============================
## The surface is grouped into FACETS - `teams()`, `units()`, `orders()`, ... -
## rather than being one flat list of ~250 methods, for three reasons:
##
##   1. A handler file names the one facet it uses, so reading a handler tells
##      you exactly which slice of the world it touches.
##   2. A partial world implements the facets it can and inherits refusal for
##      the rest, method by method, without a giant `supports()` token list.
##   3. Facets are INNER CLASSES of this script, not separate files. That was a
##      deliberate call: separate files would mean ~30 more scripts and, worse,
##      would tempt a package agent into "just adding my facet file", which is
##      the collision this design exists to prevent. One file, one owner.
##
## A concrete world overrides the `_make_*()` factory for the facets it can
## serve and returns its own subclass; everything else keeps refusing.
##
##
## FAIL-LOUD CONTRACT
## ==================
## Nothing here silently no-ops, and nothing returns a plausible-looking answer
## it did not compute.
##
##   * COMMANDS (mutators) return `bool`. `false` means "not implemented /
##     refused"; the handler must turn that into Dispatch.Status.WORLD_REFUSED,
##     which the dispatcher records as a structured gap.
##   * QUERIES (reads) return `SageWorldQuery`, which carries `ok` alongside the
##     value. A condition that cannot be evaluated MUST NOT return `false` as if
##     it had been evaluated - that is how a mission advances on a lie. Check
##     `ok` first, always. See script_world_query.gd.
##
## Every refusal names the exact facet method that refused, so the gap log
## identifies the missing WORLD surface and not merely the failing action.
##
##
## ARGUMENT CONVENTIONS (read before writing a handler)
## ====================================================
##   * Names are Strings exactly as the decoded script argument carries them:
##     player names, team names, object ("named unit") names, waypoint names,
##     area names, upgrade names, object types. No normalisation happens here.
##   * SAGE splits nearly every verb into a TEAM_ variant and a NAMED_ (single
##     object) variant. Those are NOT separate world methods. One method takes a
##     `Scope` plus a name - see `Scope` below - so TEAM_GUARD_POSITION and
##     UNIT_GUARD_POSITION reach the same world method with a different scope.
##   * The huge `..._USE_COMMANDBUTTON_ON_NEAREST_*` / `..._MOVE_TO_NEAREST_*`
##     families differ only in how they name their TARGET. Targets are therefore
##     a small tagged Dictionary built by the `target_*()` helpers below, not
##     dozens of near-identical methods.
##   * DURATIONS are always interpreter TICKS (SageScriptEnv.ticks_per_second),
##     never frames, seconds or milliseconds. The handler converts; the world
##     never has to guess a timebase.
##   * POSITIONS are Vector3 in world space.
##   * ENUM-valued script parameters (COMPARISON, RELATION, AI_MOOD, STANCE_TYPE,
##     EMOTION, ...) are passed as the raw int from SageScriptParamTypes. The
##     world is not asked to parse strings.
##
##
## PRESENTATION IS NOT PART OF THE SIMULATION
## ==========================================
## Camera (WP05), audio/video (WP06) and UI/radar/VFX (WP07) are 191 of the 760
## handlers and NONE of them may influence the simulation - they must not exist
## as far as lockstep is concerned. They are not facets; they are SINKS. See
## `SageScriptPresentationSink` at the bottom of this file.

const Query := preload("res://src/script/script_world_query.gd")

## Capability tokens. A world advertises what it can actually do.
##
## LEGACY, AND DELIBERATELY NOT GROWN. These three predate the facet surface and
## are still consulted by script_handlers_core.gd. New code does NOT add tokens:
## the command/query refusal path above is finer-grained (per method, not per
## subsystem) and self-identifying, so a token would only add a second, coarser
## way to say the same thing.
const CAP_PLAYER_MONEY := "player_money"
const CAP_RANDOM := "random"
const CAP_DEBUG_OUTPUT := "debug_output"


## Which kind of thing an order or query applies to. SAGE spells the same verb
## three times (NAMED_/TEAM_/PLAYER_); the world implements it once.
enum Scope {
	UNIT,    ## a single named object ("NAMED_...")
	TEAM,    ## a team roster ("TEAM_...")
	PLAYER,  ## everything a player owns ("PLAYER_...")
}

## How a target is named. Built by the `target_*()` helpers - do not hand-roll
## the dictionary, the tag values are an implementation detail.
enum TargetKind {
	OBJECT,                  ## a named object
	TEAM,                    ## a named team
	PLAYER,                  ## a player
	WAYPOINT,                ## a single waypoint
	WAYPOINT_PATH,           ## a named waypoint path, followed in order
	POSITION,                ## an explicit world position
	AREA,                    ## a named trigger area
	NEAREST_TYPE,            ## nearest object of an object type, optional owner
	NEAREST_KINDOF,          ## nearest object with a KINDOF bit, optional owner
	NEAREST_ENEMY_UNIT,      ## nearest enemy unit
	NEAREST_ENEMY_BUILDING,  ## nearest enemy building, optional building class
	NEAREST_GARRISONED,      ## nearest garrisoned building (WP03, blocked)
	MOST_VALUABLE,           ## highest-value object matching a filter
	SELF,                    ## the acting object itself
}


static func scope_label(scope: int) -> String:
	match scope:
		Scope.UNIT:
			return "unit"
		Scope.TEAM:
			return "team"
		Scope.PLAYER:
			return "player"
	return "scope-%d" % scope


# --- Target constructors --------------------------------------------------
#
# One tagged dictionary type instead of ~40 near-identical world methods. Keys
# beyond "kind" are documented per constructor; a world reads them by name.


static func target_object(object_name: String) -> Dictionary:
	return {"kind": TargetKind.OBJECT, "name": object_name}


static func target_team(team_name: String) -> Dictionary:
	return {"kind": TargetKind.TEAM, "name": team_name}


static func target_player(player_name: String) -> Dictionary:
	return {"kind": TargetKind.PLAYER, "name": player_name}


static func target_waypoint(waypoint: String) -> Dictionary:
	return {"kind": TargetKind.WAYPOINT, "name": waypoint}


static func target_waypoint_path(path: String, exact: bool = false) -> Dictionary:
	## `exact` distinguishes the ..._FOLLOW_WAYPOINTS_EXACT variants, which hold
	## formation rather than letting members path individually.
	return {"kind": TargetKind.WAYPOINT_PATH, "name": path, "exact": exact}


static func target_position(position: Vector3) -> Dictionary:
	return {"kind": TargetKind.POSITION, "position": position}


static func target_area(area: String) -> Dictionary:
	return {"kind": TargetKind.AREA, "name": area}


static func target_nearest_type(object_type: String, owner: String = "") -> Dictionary:
	## `owner` empty means "any owner"; the ..._OWNED_BY_PLAYER variants set it.
	return {"kind": TargetKind.NEAREST_TYPE, "name": object_type, "owner": owner}


static func target_nearest_kindof(kindof: String, owner: String = "") -> Dictionary:
	return {"kind": TargetKind.NEAREST_KINDOF, "name": kindof, "owner": owner}


static func target_nearest_enemy_unit() -> Dictionary:
	return {"kind": TargetKind.NEAREST_ENEMY_UNIT}


static func target_nearest_enemy_building(building_class: String = "") -> Dictionary:
	## `building_class` empty is the ..._NEAREST_ENEMY_BUILDING variant; set is
	## the ..._NEAREST_ENEMY_BUILDING_CLASS variant.
	return {"kind": TargetKind.NEAREST_ENEMY_BUILDING, "name": building_class}


static func target_nearest_garrisoned() -> Dictionary:
	return {"kind": TargetKind.NEAREST_GARRISONED}


static func target_most_valuable(filter_name: String = "") -> Dictionary:
	return {"kind": TargetKind.MOST_VALUABLE, "name": filter_name}


static func target_self() -> Dictionary:
	return {"kind": TargetKind.SELF}


# --- Refusal reporting ----------------------------------------------------


func _on_refusal(_method: String, _reason: String) -> void:
	## Called every time a facet method that this world does not implement is
	## invoked. The base world stays quiet: an unimplemented capability is an
	## expected, structured outcome that the dispatcher records as a gap.
	##
	## SageScriptWorldStub overrides this to push_error, because in a TEST an
	## unimplemented world method almost always means the test forgot to teach
	## the stub, and a quiet refusal there would look like a passing assertion.
	pass


# --- Facets ---------------------------------------------------------------
#
# `_facets` caches one instance per facet so a world can hold per-facet state.
# Override the matching `_make_*()` in a concrete world; never override the
# accessor itself.

var _facets: Dictionary = {}


func _bind(facet: Facet) -> Facet:
	facet.world = self
	return facet


func _facet(key: String, factory: Callable) -> Facet:
	if not _facets.has(key):
		_facets[key] = _bind(factory.call())
	return _facets[key]


func players() -> Players:
	return _facet("players", _make_players) as Players


func _make_players() -> Players:
	return Players.new()


func teams() -> Teams:
	return _facet("teams", _make_teams) as Teams


func _make_teams() -> Teams:
	return Teams.new()


func units() -> Units:
	return _facet("units", _make_units) as Units


func _make_units() -> Units:
	return Units.new()


func orders() -> Orders:
	return _facet("orders", _make_orders) as Orders


func _make_orders() -> Orders:
	return Orders.new()


func combat() -> Combat:
	return _facet("combat", _make_combat) as Combat


func _make_combat() -> Combat:
	return Combat.new()


func progression() -> Progression:
	return _facet("progression", _make_progression) as Progression


func _make_progression() -> Progression:
	return Progression.new()


func economy() -> Economy:
	return _facet("economy", _make_economy) as Economy


func _make_economy() -> Economy:
	return Economy.new()


func areas() -> Areas:
	return _facet("areas", _make_areas) as Areas


func _make_areas() -> Areas:
	return Areas.new()


func terrain() -> Terrain:
	return _facet("terrain", _make_terrain) as Terrain


func _make_terrain() -> Terrain:
	return Terrain.new()


func ai() -> Ai:
	return _facet("ai", _make_ai) as Ai


func _make_ai() -> Ai:
	return Ai.new()


func meta() -> Meta:
	return _facet("meta", _make_meta) as Meta


func _make_meta() -> Meta:
	return Meta.new()


func fog() -> Fog:
	return _facet("fog", _make_fog) as Fog


func _make_fog() -> Fog:
	return Fog.new()


func transport() -> Transport:
	return _facet("transport", _make_transport) as Transport


func _make_transport() -> Transport:
	return Transport.new()


# --- Presentation sinks ---------------------------------------------------
#
# Three independent sinks, one per presentation work package. They are separate
# instances of the SAME class: a sink has no domain logic to specialise, only a
# destination, and three near-identical classes would be three places to fix a
# bug. See the PresentationSink class comment for the whole contract.


func camera() -> PresentationSink:
	return _facet("camera", _make_camera) as PresentationSink


func _make_camera() -> PresentationSink:
	return PresentationSink.new("camera")


func audio() -> PresentationSink:
	return _facet("audio", _make_audio) as PresentationSink


func _make_audio() -> PresentationSink:
	return PresentationSink.new("audio")


func ui() -> PresentationSink:
	return _facet("ui", _make_ui) as PresentationSink


func _make_ui() -> PresentationSink:
	return PresentationSink.new("ui")


func supports(_capability: String) -> bool:
	## Base world supports nothing. Override with the tokens you implement.
	return false


# --- Time -----------------------------------------------------------------


func world_frame() -> int:
	## The world's own frame counter, for actions that read game time. The
	## script environment keeps its own tick count; this is the world's.
	return 0


# --- Deterministic randomness --------------------------------------------
#
# SAGE distinguishes simulation-random (must be identical on every peer) from
# client-random (explicitly desync-prone; the reference warns about it). Only
# simulation-random is exposed here. A world that cannot supply a deterministic
# stream must report CAP_RANDOM as unsupported rather than reaching for
# randi(), which would break lockstep.


func random_int(low: int, _high: int) -> int:
	return low


func random_real(low: float, _high: float) -> float:
	return low


# --- Player economy -------------------------------------------------------
#
# PRE-FACET API. script_handlers_core.gd (tranche 1) calls these two directly
# and gates on CAP_PLAYER_MONEY. They are kept exactly as they were so that
# tranche stays untouched; `economy()` forwards to them by default, so a world
# that implements only these two already serves the economy facet's money
# methods. New handlers use `economy()`.


func player_money(_player: String) -> int:
	return 0


func set_player_money(_player: String, _amount: int) -> bool:
	return false


# --- Diagnostics ----------------------------------------------------------


func debug_message(_channel: String, _text: String) -> bool:
	## Destination for DEBUG_STRING / DEBUG_MESSAGE_BOX / DEBUG_CRASH_BOX.
	## `channel` is the originating action name.
	return false


# ==========================================================================
# FACET BASE
# ==========================================================================


class Facet:
	extends RefCounted

	## Base of every simulation facet.
	##
	## Every method in every facet below has a one-line body that either answers
	## or refuses. A concrete world subclasses the facet and overrides only the
	## methods it can serve; everything it does not override keeps refusing, by
	## name, forever. There is no partial-implementation twilight state where a
	## method exists but quietly does nothing.

	var world: SageScriptWorld = null

	func _refuse_query(method: String, reason: String = "") -> SageWorldQuery:
		if world != null:
			world._on_refusal(method, reason)
		return SageWorldQuery.refused(method, reason)

	func _refuse_command(method: String, reason: String = "") -> bool:
		if world != null:
			world._on_refusal(method, reason)
		return false


# ==========================================================================
# PRESENTATION SINK - WP05 (camera), WP06 (audio/video), WP07 (UI/radar/VFX)
# ==========================================================================


class PresentationSink:
	extends Facet

	## Where presentation-only script actions go.
	##
	## WHY THESE ARE NOT FACETS
	## ------------------------
	## 191 of the 760 remaining handlers are camera, audio and UI. Not one of
	## them may influence the simulation: this project runs lockstep multiplayer,
	## where a camera move or a sound cue that changed sim state would desync
	## peers whose local presentation differs. Giving them typed world methods
	## would invite exactly that - a "camera position" the sim can read back.
	##
	## So presentation is a ONE-WAY channel. A handler describes what happened;
	## nothing is ever read back. `emit()` returns bool purely so an unattached
	## sink refuses honestly rather than pretending it played a sound.
	##
	## OP NAMING (binding contract, not a suggestion)
	## ----------------------------------------------
	## `op` is the action's canonical InternalName, verbatim and uppercase -
	## "CAMERA_MOVE_TO_SELECTION", "PLAY_SOUND_EFFECT_AT". No abbreviations, no
	## re-grouping: the presentation adapter switches on it, and a name that
	## does not match the vocabulary cannot be traced back to the map that
	## produced it.
	##
	## `values` is the decoded argument list in DECLARATION ORDER, read
	## positionally via SageScriptArgs (see script_args.gd for why position and
	## not type-search). It is the signature's arguments and nothing else: no
	## re-ordering, no defaults filled in, no extra context bolted on. A test
	## asserting against a recording therefore asserts against the map's own
	## arguments.
	##
	## WHY A RECORDING IS THE DESIGN, NOT A FALLBACK
	## ---------------------------------------------
	## The stub's sink (SageScriptWorldStub) RECORDS every call and reports it
	## accepted. That is deliberate and is the entire point: presentation has no
	## observable simulation effect, so the only way to test a camera handler at
	## all is to assert on what it emitted. This is the one place in this
	## subsystem where "accept and remember" is correct rather than a silent
	## fallback - and it is correct only because the recording is asserted on. A
	## sink whose recording nothing reads is dead code, not coverage.
	##
	## The BASE sink below records nothing and refuses everything, so a real
	## world that has not wired its presentation adapter produces a structured
	## gap per action instead of a silently missing cutscene.

	var channel: String = ""

	func _init(sink_channel: String = "") -> void:
		channel = sink_channel

	func emit(op: String, _values: Array) -> bool:
		## The whole surface of WP05, WP06 and WP07. One method, because a sink
		## has no semantics - only a destination.
		return _refuse_command("%s.emit(%s)" % [channel, op])


# ==========================================================================
# PLAYERS - WP01 (counter reads), WP17 (player scope), WP08 (meta queries)
# ==========================================================================


class Players:
	extends Facet

	## Player-scope reads and commands.

	func exists(_player: String) -> SageWorldQuery:
		## WP17. Whether the named player slot is present in this match.
		return _refuse_query("players.exists")

	func faction(_player: String) -> SageWorldQuery:
		## WP17 SKIRMISH_PLAYER_FACTION. String faction/side name.
		return _refuse_query("players.faction")

	func start_position(_player: String) -> SageWorldQuery:
		## WP17 START_POSITION_IS. Int start-spot index.
		return _refuse_query("players.start_position")

	func command_points_available(_player: String) -> SageWorldQuery:
		## WP01 SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER.
		return _refuse_query("players.command_points_available")

	func command_points_total(_player: String) -> SageWorldQuery:
		## WP01 SET_PLAYER_COMMAND_POINTS_TOTAL_TO_COUNTER.
		return _refuse_query("players.command_points_total")

	func command_points_used(_player: String) -> SageWorldQuery:
		## WP01 SET_PLAYER_COMMAND_POINTS_USED_TO_COUNTER.
		return _refuse_query("players.command_points_used")

	func override_command_points(_player: String, _amount: int) -> bool:
		## WP17 OVERRIDE_PLAYER_COMMAND_POINTS.
		return _refuse_command("players.override_command_points")

	func light_points(_player: String) -> SageWorldQuery:
		## WP01 SET_PLAYER_LIGHT_POINTS_TO_COUNTER, WP17 PLAYER_COMPARE_LIGHT_POINTS.
		return _refuse_query("players.light_points")

	func give_light_points(_player: String, _amount: int) -> bool:
		## WP17 PLAYER_GIVE_LIGHT_POINTS.
		return _refuse_command("players.give_light_points")

	func reset_light_points(_player: String) -> bool:
		## WP17 PLAYER_RESET_LIGHT_POINTS.
		return _refuse_command("players.reset_light_points")

	func change_light_point_level(_player: String, _delta: int) -> bool:
		## WP09 PLAYER_GIVE_LIGHT_POINT_LEVEL / PLAYER_REMOVE_LIGHT_POINT_LEVEL.
		return _refuse_command("players.change_light_point_level")

	func rank_level(_player: String) -> SageWorldQuery:
		## WP17 PLAYER_COMPARE_RANK. Int rank level.
		return _refuse_query("players.rank_level")

	func set_rank_level(_player: String, _level: int) -> bool:
		## WP17 PLAYER_SET_RANKLEVEL.
		return _refuse_command("players.set_rank_level")

	func add_rank_level(_player: String, _delta: int) -> bool:
		## WP17 PLAYER_ADD_RANKLEVEL.
		return _refuse_command("players.add_rank_level")

	func set_rank_level_limit(_player: String, _limit: int) -> bool:
		## WP12 PLAYER_SET_RANKLEVELLIMIT.
		return _refuse_command("players.set_rank_level_limit")

	func reached_level_cap(_player: String) -> SageWorldQuery:
		## WP17 PLAYER_HAS_REACHED_LEVEL_CAP.
		return _refuse_query("players.reached_level_cap")

	func set_max_spell_points(_player: String, _amount: int) -> bool:
		## WP17 PLAYER_SET_MAX_SPELLPOINTS.
		return _refuse_command("players.set_max_spell_points")

	func object_count_of_types(
		_player: String, _object_type_list: String, _include_dead: bool
	) -> SageWorldQuery:
		## WP01 SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER[_INCLUDE_DEAD],
		## WP17 PLAYER_HAS_OBJECT_COMPARISON. `_object_type_list` is an
		## OBJECT_TYPE_LIST name, not a single type - see Meta.object_list_*.
		return _refuse_query("players.object_count_of_types")

	func object_count_with_model_condition(
		_player: String, _model_condition: String
	) -> SageWorldQuery:
		## WP01 SET_COUNTER_TO_NUMBER_OBJECTS_PLAYER_OWNES_WITH_MODELCONDITION,
		## WP17 PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION.
		return _refuse_query("players.object_count_with_model_condition")

	func object_count_within_distance(
		_player: String, _object_type: String, _origin: String, _distance: float
	) -> SageWorldQuery:
		## WP11 PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT. `_origin` is a
		## named object.
		return _refuse_query("players.object_count_within_distance")

	func building_count(_player: String, _building_class: String) -> SageWorldQuery:
		## WP17 PLAYER_HAS_N_OR_FEWER_BUILDINGS / _FACTION_BUILDINGS. Empty
		## `_building_class` counts every building.
		return _refuse_query("players.building_count")

	func base_count(_player: String) -> SageWorldQuery:
		## WP17 PLAYER_HAS_N_OR_FEWER_BASES.
		return _refuse_query("players.base_count")

	func home_base(_player: String) -> SageWorldQuery:
		## WP17 FIND_HOME_BASE_OF_PLAYER. String object name of the home base.
		return _refuse_query("players.home_base")

	func power_produced(_player: String) -> SageWorldQuery:
		## WP17 PLAYER_HAS_POWER / PLAYER_HAS_NO_POWER / PLAYER_POWER_COMPARE_PERCENT.
		return _refuse_query("players.power_produced")

	func power_consumed(_player: String) -> SageWorldQuery:
		## WP17 PLAYER_EXCESS_POWER_COMPARE_VALUE (excess = produced - consumed).
		return _refuse_query("players.power_consumed")

	func relation_to(_player: String, _other: String) -> SageWorldQuery:
		## WP17 PLAYER_RELATES_PLAYER read side. Int from ParamTypes RELATION.
		return _refuse_query("players.relation_to")

	func set_relation_to(_player: String, _other: String, _relation: int) -> bool:
		## WP17 PLAYER_RELATES_PLAYER write side.
		return _refuse_command("players.set_relation_to")

	func has_discovered(_player: String, _other: String) -> SageWorldQuery:
		## WP17 SKIRMISH_PLAYER_HAS_DISCOVERED_PLAYER.
		return _refuse_query("players.has_discovered")

	func has_prerequisite_to_build(_player: String, _object_type: String) -> SageWorldQuery:
		## WP17 SKIRMISH_PLAYER_HAS_PREREQUISITE_TO_BUILD.
		return _refuse_query("players.has_prerequisite_to_build")

	func can_build_at_base(_player: String, _base: String, _object_type: String) -> SageWorldQuery:
		## WP17 CAN_BUILD_AT_BASE / CAN_BUILD_OBJECTTYPE_AT_BASE. The sourced
		## signatures are (PLAYER, UNIT) and (PLAYER, UNIT, OBJECT_TYPE): the
		## UNIT names the BASE the question is anchored to - a skirmish AI holds
		## several bases and the answer differs per base - so the base is a
		## required parameter, resolved through the shared object/unit-reference
		## namespace. Empty object_type is the "anything at all" variant that
		## CAN_BUILD_AT_BASE asks for.
		return _refuse_query("players.can_build_at_base")

	func lost_object_type(_player: String, _object_type: String) -> SageWorldQuery:
		## WP17 PLAYER_LOST_OBJECT_TYPE.
		return _refuse_query("players.lost_object_type")

	func is_in_planning_mode(_player: String) -> SageWorldQuery:
		## WP17 PLAYER_IS_IN_PLANNING_MODE.
		return _refuse_query("players.is_in_planning_mode")

	func set_base_construction_enabled(_player: String, _enabled: bool) -> bool:
		## WP17 PLAYER_ENABLE/DISABLE_BASE_CONSTRUCTION.
		return _refuse_command("players.set_base_construction_enabled")

	func set_base_construction_speed(_player: String, _factor: float) -> bool:
		## WP17 SET_BASE_CONSTRUCTION_SPEED.
		return _refuse_command("players.set_base_construction_speed")

	func set_factories_enabled(_player: String, _enabled: bool) -> bool:
		## WP17 PLAYER_ENABLE/DISABLE_FACTORIES.
		return _refuse_command("players.set_factories_enabled")

	func set_unit_construction_enabled(
		_player: String, _object_type: String, _enabled: bool
	) -> bool:
		## WP16 PLAYER_ENABLE/DISABLE_UNIT_CONSTRUCTION.
		return _refuse_command("players.set_unit_construction_enabled")

	func set_auto_build_enabled(_player: String, _enabled: bool) -> bool:
		## WP17 TOGGLE_AUTO_BUILD.
		return _refuse_command("players.set_auto_build_enabled")

	func exit_all_buildings(_player: String) -> bool:
		## WP17 PLAYER_EXIT_ALL_BUILDINGS.
		return _refuse_command("players.exit_all_buildings")

	func sell_everything(_player: String) -> bool:
		## WP17 PLAYER_SELL_EVERYTHING.
		return _refuse_command("players.sell_everything")

	func transfer_ownership(_from_player: String, _to_player: String) -> bool:
		## WP17 PLAYER_TRANSFER_OWNERSHIP_PLAYER.
		return _refuse_command("players.transfer_ownership")

	func repair_structure(_player: String, _object_name: String) -> bool:
		## WP17 PLAYER_REPAIR_NAMED_STRUCTURE.
		return _refuse_command("players.repair_structure")

	func force_emotion(_player: String, _emotion: int, _duration_ticks: int) -> bool:
		## WP17 PLAYER_FORCE_EMOTION. `_emotion` is a ParamTypes EMOTION int.
		return _refuse_command("players.force_emotion")

	func set_excluded_from_score_screen(_player: String, _excluded: bool) -> bool:
		## WP17 PLAYER_EXCLUDE_FROM_SCORE_SCREEN.
		return _refuse_command("players.set_excluded_from_score_screen")

	func add_skill_points(_player: String, _amount: int) -> bool:
		## WP13 PLAYER_ADD_SKILLPOINTS.
		return _refuse_command("players.add_skill_points")

	func select_skill_set(_player: String, _skill_set: int) -> bool:
		## WP13 PLAYER_SELECT_SKILLSET.
		return _refuse_command("players.select_skill_set")

	func eva_event_played_within(
		_player: String, _event: String, _seconds: float
	) -> SageWorldQuery:
		## WP17 HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS.
		return _refuse_query("players.eva_event_played_within")

	func units_near_last_eva_event(_player: String, _radius: float) -> SageWorldQuery:
		## WP17 IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_..._COMPARISON_INT.
		return _refuse_query("players.units_near_last_eva_event")

	func threat_at(_player: String, _threat_finder: String, _threat_type: String) -> SageWorldQuery:
		## WP01 SET_COUNTER_TO_THREAT_FINDER_THREAT. RotWK-only.
		return _refuse_query("players.threat_at")


# ==========================================================================
# TEAMS - WP15 (the largest package), plus team reads used by WP09/WP11/WP13
# ==========================================================================


class Teams:
	extends Facet

	## Team roster creation, membership and state.

	func exists(_team: String) -> SageWorldQuery:
		## WP15. Whether a team of that name is currently instantiated.
		return _refuse_query("teams.exists")

	func was_created(_team: String) -> SageWorldQuery:
		## WP15 TEAM_CREATED - an edge, true on the tick the team appeared.
		##
		## SOURCED, and it really IS an edge (unlike its object-scope sibling).
		## evaluateTeamCreated is `getTeamNamed(name)->isCreated()`, and
		## Team::m_created is set by setActive() and CLEARED the next time
		## Team::updateState runs - once per frame, from Player::update
		## (Team.h:204/312/322; Team.cpp:1806-1816 "Clears m_enteredExited,
		## checks & clears m_created"). It is save-persisted (Team::xfer,
		## Team.cpp:2646). So the surface's edge demand stands here - and it
		## must NOT be wired to exists(), which would refire one-shot AI init
		## on every evaluation. Contrast NAMED_CREATED, which the object-name
		## lane proved is literally getUnitNamed() != NULL.
		return _refuse_query("teams.was_created")

	func was_destroyed(_team: String) -> SageWorldQuery:
		## WP13 TEAM_DESTROYED.
		##
		## SOURCED, and it is NOT an edge and needs NO destruction records.
		## ScriptConditions::evaluateIsDestroyed is exactly
		## `theTeam ? !theTeam->hasAnyObjects() : false` - a LEVEL read of
		## present membership, with retail's own comment "Non existent team is
		## not destroyed" on the null arm. hasAnyObjects counts members that
		## are neither effectively-dead nor destroyed and are not projectiles,
		## inert objects or mines - STRUCTURES INCLUDED (Team.cpp
		## hasAnyObjects; its sibling hasAnyUnits, which TEAM_HAS_UNITS uses,
		## is the one that excludes structures). So this is "the team has no
		## living objects left", answerable from a living-membership census
		## alone. The surface's former "team destruction edge records" demand
		## was wrong and has been withdrawn.
		return _refuse_query("teams.was_destroyed")

	func was_discovered(_team: String, _by_player: String) -> SageWorldQuery:
		## WP15 TEAM_DISCOVERED.
		return _refuse_query("teams.was_discovered")

	func unit_count(_team: String) -> SageWorldQuery:
		## WP15 TEAM_HAS_UNITS and every "how many are left" gate.
		return _refuse_query("teams.unit_count")

	func count_with_kindof(_team: String, _kindof: String) -> SageWorldQuery:
		## WP15 TEAM_HAS_FEWER_THAN_X_UNITS_WITH_KINDOF.
		return _refuse_query("teams.count_with_kindof")

	func members(_team: String) -> SageWorldQuery:
		## WP15/WP13. Array of object names, for handlers that must fan out.
		return _refuse_query("teams.members")

	func owner(_team: String) -> SageWorldQuery:
		## WP15 TEAM_OWNED_BY_PLAYER. String player name.
		return _refuse_query("teams.owner")

	func leader(_team: String) -> SageWorldQuery:
		## WP15 TEAM_IS_LED_BY_UNIT.
		##
		## THIS SIGNATURE IS MISNAMED AND CANNOT SERVE ITS OWN MEMBER. The
		## BFME condition template (whale_scriptengine conditions worklist,
		## condition id 123, parameters [TEAM, UNIT]) reads
		## "Is team <TEAM> affected by leadership ability from unit <UNIT>" -
		## a LEADERSHIP-AURA test between a team and a named unit, not a
		## "who leads this team" designation. Answering it from a leader NAME
		## would be a different question with a different answer (a unit whose
		## leadership aura covers a team it does not lead answers true in
		## retail and false here). Leader designation is not a BFME concept on
		## this member; the surface annotation that called it one was wrong.
		return _refuse_query("teams.leader")

	func state(_team: String) -> SageWorldQuery:
		## WP15 TEAM_STATE_IS / TEAM_STATE_IS_NOT. String state name.
		return _refuse_query("teams.state")

	func set_state(_team: String, _state: String) -> bool:
		## WP15 TEAM_SET_STATE.
		return _refuse_command("teams.set_state")

	func custom_state(_team: String) -> SageWorldQuery:
		## WP15 TEAM_HAS_CUSTOM_STATE. Value: the Array of custom-state tokens
		## currently ENABLED on the team (sorted, unique). The writer's BOOLEAN
		## argument is what rules the shape: retail authors the same token with
		## both 1 and 0 (AI_ADVANCING x34 each way in the AI libraries), so a
		## team holds a SET of independent tokens, not one current value.
		return _refuse_query("teams.custom_state")

	func set_custom_state(_team: String, _state: String, _enabled: bool) -> bool:
		## WP15 TEAM_SET_CUSTOM_STATE(TEAM, TEAM_STATE, BOOLEAN) - "<TEAM> set
		## custom state <TEAM_STATE> to <BOOLEAN>". The BOOLEAN is `_enabled`,
		## the action's own third argument (sourced: action id 490 takes
		## parameter types [TEAM, TEAM_STATE, BOOLEAN] in the BFME1 script
		## metadata, and every one of the 385 retail library call sites carries
		## it in the payload's integer field as 0 or 1). It was MISSING from
		## this signature originally - WP15 gap-registered the member because
		## serving TEAM_SET_CUSTOM_STATE without the flag would invert the 183
		## corpus-wide clear sites (40 of them in the AI census scope).
		return _refuse_command("teams.set_custom_state")

	func threat(_team: String) -> SageWorldQuery:
		## WP15 SET_COUNTER_TO_TEAM_THREAT.
		return _refuse_query("teams.threat")

	func set_threat_level(_team: String, _level: int) -> bool:
		## WP09 TEAM_THREAT_LEVEL.
		return _refuse_command("teams.set_threat_level")

	func command_points_to_build(_team: String) -> SageWorldQuery:
		## WP15 SET_COMMAND_POINTS_TO_BUILD_TEAM_TO_COUNTER,
		## HAS_COMMAND_POINTS_TO_BUILD_TEAM.
		return _refuse_query("teams.command_points_to_build")

	func enemy_sighted(_team: String, _by_player: String) -> SageWorldQuery:
		## WP15 ENEMY_SIGHTED_BY_TEAM.
		return _refuse_query("teams.enemy_sighted")

	func contained_count(_team: String) -> SageWorldQuery:
		## WP15 TEAM_WAIT_FOR_NOT_CONTAINED_ALL / _PARTIAL: how many members are
		## currently inside something.
		return _refuse_query("teams.contained_count")

	func attacked_and_cannot_retaliate_count(_team: String) -> SageWorldQuery:
		## WP13 TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL / _ANY.
		return _refuse_query("teams.attacked_and_cannot_retaliate_count")

	func build(_player: String, _team: String) -> bool:
		## WP11 BUILD_TEAM - queue the team's units for production.
		##
		## SIGNATURE DEFECT: BFME's BUILD_TEAM (action id 70) takes ONE
		## parameter, [TEAM] - "Start building team <TEAM>". The PLAYER slot
		## here is invented; the owner comes from the team's own prototype.
		## Recorded rather than silently ignored.
		return _refuse_command("teams.build")

	func recruit(_team: String, _radius: float, _from_team: String) -> bool:
		## WP15 RECRUIT_TEAM / RECRUIT_TEAM_AT_TEAM / TEAM_RECRUIT_UNITS[_FROM_TEAM].
		## Empty `_from_team` recruits from the map at large.
		##
		## THIS SIGNATURE FITS ONLY THE RECRUIT_TEAM PAIR, AND THE INT IS NOW
		## RULED. BFME's own action templates split into two shapes:
		##   RECRUIT_TEAM(TEAM, REAL)                    id 180
		##   RECRUIT_TEAM_AT_TEAM(TEAM, REAL, TEAM)      id 181
		##     "Recruit an instance of <TEAM>, maximum recruiting distance
		##      (feet): <REAL> [from the team <TEAM>]" - the REAL is the radius
		##      this signature carries.
		##   TEAM_RECRUIT_UNITS(TEAM, INT, OBJECT_TYPE_LIST)            id 397
		##   TEAM_RECRUIT_UNITS_FROM_TEAM(TEAM, INT, OBJECT_TYPE_LIST, TEAM) 398
		##     "<TEAM> will recruit <INT> units of type <OBJECT_TYPE_LIST>
		##      from nearby recruitable allied teams / from <TEAM>."
		## The second pair has NO radius at all: its INT is a unit COUNT and
		## its type list selects which units. That closes WP15's open
		## "ruling on the INT" - the community reference's description was
		## garbled by placeholder substitution, but the BFME binary's own
		## uiStrings are unambiguous. NEEDED for the count pair: a separate
		## signature (team, count: int, object_type_list: String,
		## from_team: String).
		return _refuse_command("teams.recruit")

	func recruit_combo_units(_team: String, _from_team: String) -> bool:
		## WP15 RECRUIT_COMBO_UNITS_FROM_TEAM.
		return _refuse_command("teams.recruit_combo_units")

	func collect_nearby(_team: String, _radius: float) -> bool:
		## WP15 TEAM_COLLECT_NEARBY_FOR_TEAM.
		return _refuse_command("teams.collect_nearby")

	func set_available_for_recruitment(_team: String, _available: bool) -> bool:
		## WP15 TEAM_AVAILABLE_FOR_RECRUITMENT.
		return _refuse_command("teams.set_available_for_recruitment")

	func set_ai_recruitable(_team: String, _recruitable: bool) -> bool:
		## WP11 TEAM_SET_AI_RECRUITABLE_FLAG.
		return _refuse_command("teams.set_ai_recruitable")

	func delete(_team: String, _living_only: bool) -> bool:
		## WP15 TEAM_DELETE / TEAM_DELETE_LIVING.
		return _refuse_command("teams.delete")

	func merge_into(_team: String, _destination: String) -> bool:
		## WP15 TEAM_MERGE_INTO_TEAM.
		return _refuse_command("teams.merge_into")

	func transfer_to_player(_team: String, _player: String) -> bool:
		## WP15 TEAM_TRANSFER_TO_PLAYER.
		return _refuse_command("teams.transfer_to_player")

	func set_reference(_reference: String, _team: String) -> bool:
		## WP15 SET_TEAM_REFERENCE / SET_TEAM_REFERENCE_TO_REFERNECE [sic].
		return _refuse_command("teams.set_reference")

	func set_reference_to_nearest(
		_reference: String, _object_type: String, _player: String, _named_type: bool
	) -> bool:
		## WP15 SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER [sic] and its
		## _UNNAMED_TYPE_ sibling (`_named_type` false).
		return _refuse_command("teams.set_reference_to_nearest")

	func set_nearest_unit_of_type_to_reference(
		_team: String, _player: String, _object_type: String, _reference: String
	) -> bool:
		## WP15 TEAM_SET_PLAYERS_NEAREST_UNIT_OF_TYPE_TO_REFERENCE.
		return _refuse_command("teams.set_nearest_unit_of_type_to_reference")

	func adjust_priority(_team: String, _delta: int) -> bool:
		## WP15 TEAM_INCREASE/DECREASE_PRIORITY[_BY_VALUE]. The no-value variants
		## pass the engine default step; the handler decides that, not the world.
		##
		## CORRECTION: there is no "engine default step" for a handler to pass.
		## BFME's TEAM_INCREASE_PRIORITY (id 238) and TEAM_DECREASE_PRIORITY
		## (id 239) take [TEAM] only and read
		## "Increase/Reduce the AI priority for <TEAM> by its Success Priority
		## Increase / Failure Priority Decrease amount" - the step is authored
		## PER TEAM TEMPLATE, so serving the no-value variants needs the team
		## prototype's template info, not a constant. Both retail AI call sites
		## are the no-value spelling.
		return _refuse_command("teams.adjust_priority")

	func set_attitude(_team: String, _mood: int) -> bool:
		## WP15 TEAM_SET_ATTITUDE. `_mood` is a ParamTypes AI_MOOD int.
		return _refuse_command("teams.set_attitude")

	func set_emoticon(_team: String, _emoticon: String, _duration_ticks: int) -> bool:
		## WP15 TEAM_SET_EMOTICON.
		return _refuse_command("teams.set_emoticon")

	func force_emotion(_team: String, _emotion: int, _duration_ticks: int) -> bool:
		## WP15 TEAM_FORCE_EMOTION.
		return _refuse_command("teams.force_emotion")

	func set_flame_status(_team: String, _burning: bool) -> bool:
		## WP15 TEAM_SET_FLAME_STATUS.
		return _refuse_command("teams.set_flame_status")

	func set_model_condition(
		_team: String, _condition: String, _enabled: bool, _duration_ticks: int
	) -> bool:
		## WP15 TEAM_SET_MODELCONDITION[_FOR_DURATION]. `_duration_ticks` <= 0 is
		## the indefinite variant.
		return _refuse_command("teams.set_model_condition")

	func set_repulsor(_team: String, _enabled: bool) -> bool:
		## WP15 TEAM_SET_REPULSOR.
		return _refuse_command("teams.set_repulsor")

	func set_stealth_enabled(_team: String, _enabled: bool) -> bool:
		## WP15 TEAM_SET_STEALTH_ENABLED.
		return _refuse_command("teams.set_stealth_enabled")

	func set_strict_control_enabled(_team: String, _enabled: bool) -> bool:
		## WP15 TEAM_SET_STRICT_CONTROL_ENABLED.
		return _refuse_command("teams.set_strict_control_enabled")

	func set_house_color_enabled(_team: String, _enabled: bool) -> bool:
		## WP15 TEAM_ENABLE_HOUSE_COLOR.
		return _refuse_command("teams.set_house_color_enabled")

	func set_object_panel_flag(_team: String, _flag: String, _enabled: bool) -> bool:
		## WP15 TEAM_AFFECT_OBJECT_PANEL_FLAGS.
		return _refuse_command("teams.set_object_panel_flag")

	func set_close_range_weapon(_team: String, _enabled: bool) -> bool:
		## WP15 TEAM_TOGGLE_CLOSE_RANGE_WEAPON.
		return _refuse_command("teams.set_close_range_weapon")

	func set_override_relation_to_team(
		_team: String, _other_team: String, _relation: int
	) -> bool:
		## WP15 TEAM_SET_OVERRIDE_RELATION_TO_TEAM.
		return _refuse_command("teams.set_override_relation_to_team")

	func set_override_relation_to_player(_team: String, _player: String, _relation: int) -> bool:
		## WP15 TEAM_SET_OVERRIDE_RELATION_TO_PLAYER.
		return _refuse_command("teams.set_override_relation_to_player")

	func remove_override_relation(_team: String, _other: String, _other_is_team: bool) -> bool:
		## WP15 TEAM_REMOVE_OVERRIDE_RELATION_TO_TEAM / _TO_PLAYER,
		## PLAYER_REMOVE_OVERRIDE_RELATION_TO_TEAM.
		return _refuse_command("teams.remove_override_relation")

	func remove_all_override_relations(_team: String) -> bool:
		## WP15 TEAM_REMOVE_ALL_OVERRIDE_RELATIONS.
		return _refuse_command("teams.remove_all_override_relations")

	func set_player_override_relation_to_team(
		_player: String, _team: String, _relation: int
	) -> bool:
		## WP15 PLAYER_SET_OVERRIDE_RELATION_TO_TEAM.
		return _refuse_command("teams.set_player_override_relation_to_team")

	func harvest(_team: String) -> bool:
		## WP15 TEAM_HARVEST.
		return _refuse_command("teams.harvest")

	func repair_nearest(_team: String) -> bool:
		## WP15 TEAM_REPAIR_NEREST [sic].
		return _refuse_command("teams.repair_nearest")

	func panic(_team: String) -> bool:
		## WP15 TEAM_PANIC.
		return _refuse_command("teams.panic")

	func stop(_team: String, _disband: bool) -> bool:
		## WP15 TEAM_STOP / TEAM_STOP_AND_DISBAND.
		return _refuse_command("teams.stop")

	func exit_all(_team: String, _buildings_only: bool) -> bool:
		## WP15 TEAM_EXIT_ALL / TEAM_EXIT_ALL_BUILDINGS.
		return _refuse_command("teams.exit_all")

	func enter_object(_team: String, _object_name: String) -> bool:
		## WP15 TEAM_ENTER_NAMED.
		return _refuse_command("teams.enter_object")

	func spin_for_ticks(_team: String, _ticks: int) -> bool:
		## WP15 TEAM_SPIN_FOR_FRAMECOUNT / TEAM_SPIN_FOR_SECONDS - the handler
		## converts both into ticks.
		return _refuse_command("teams.spin_for_ticks")

	func wander(_team: String, _waypoint_path: String, _in_place: bool) -> bool:
		## WP15 TEAM_WANDER / TEAM_WANDER_IN_PLACE.
		return _refuse_command("teams.wander")

	func set_needs_open_gate(_team: String, _enabled: bool) -> bool:
		## WP15 TEAM_NEEDS_OPEN_GATE.
		return _refuse_command("teams.set_needs_open_gate")

	func execute_sequential_script(_team: String, _script: String, _looping: bool) -> bool:
		## WP15 TEAM_EXECUTE_SEQUENTIAL_SCRIPT[_LOOPING].
		return _refuse_command("teams.execute_sequential_script")

	func stop_sequential_script(_team: String) -> bool:
		## WP15 TEAM_STOP_SEQUENTIAL_SCRIPT.
		return _refuse_command("teams.stop_sequential_script")


# ==========================================================================
# UNITS - WP16 (named single-object operations and queries)
# ==========================================================================


class Units:
	extends Facet

	## Single named object reads and commands.

	func exists(_object_name: String) -> SageWorldQuery:
		## WP16. Whether a live object with that script name exists.
		return _refuse_query("units.exists")

	func position(_object_name: String) -> SageWorldQuery:
		## WP11/WP14. Vector3 world position.
		return _refuse_query("units.position")

	func owner(_object_name: String) -> SageWorldQuery:
		## WP16 NAMED_OWNED_BY_PLAYER. String player name.
		return _refuse_query("units.owner")

	func was_created(_object_name: String) -> SageWorldQuery:
		## WP16 NAMED_CREATED.
		return _refuse_query("units.was_created")

	func was_destroyed(_object_name: String) -> SageWorldQuery:
		## WP13 NAMED_DESTROYED / NAMED_NOT_DESTROYED.
		return _refuse_query("units.was_destroyed")

	func destroyed_by_object_type(_object_name: String) -> SageWorldQuery:
		## WP13 NAMED_DESTROYED_BY_OBJECTTYPE. String object type of the killer.
		return _refuse_query("units.destroyed_by_object_type")

	func is_dying(_object_name: String) -> SageWorldQuery:
		## WP16 NAMED_DYING - death animation started, object not yet gone.
		return _refuse_query("units.is_dying")

	func is_totally_dead(_object_name: String) -> SageWorldQuery:
		## WP16 NAMED_TOTALLY_DEAD.
		return _refuse_query("units.is_totally_dead")

	func was_discovered(_object_name: String, _by_player: String) -> SageWorldQuery:
		## WP16 NAMED_DISCOVERED.
		return _refuse_query("units.was_discovered")

	func is_selected(_object_name: String) -> SageWorldQuery:
		## WP16 NAMED_SELECTED. Selection is a per-player client fact; a lockstep
		## world must answer for the SIMULATION's notion of selection or refuse.
		return _refuse_query("units.is_selected")

	func type_is_selected(_player: String, _object_type: String) -> SageWorldQuery:
		## WP16 TYPE_SELECTED.
		return _refuse_query("units.type_is_selected")

	func type_was_sighted(_player: String, _object_type: String) -> SageWorldQuery:
		## WP16 TYPE_SIGHTED.
		return _refuse_query("units.type_was_sighted")

	func enemy_sighted(_object_name: String) -> SageWorldQuery:
		## WP16 ENEMY_SIGHTED.
		return _refuse_query("units.enemy_sighted")

	func health_percent(_object_name: String) -> SageWorldQuery:
		## WP13 UNIT_HEALTH. Float 0..100 to match the script parameter.
		return _refuse_query("units.health_percent")

	func is_webbed(_object_name: String) -> SageWorldQuery:
		## WP16 IS_UNIT_WEBBED.
		return _refuse_query("units.is_webbed")

	func is_building_empty(_object_name: String) -> SageWorldQuery:
		## WP16 NAMED_BUILDING_IS_EMPTY, UNIT_EMPTIED.
		return _refuse_query("units.is_building_empty")

	func gate_is_open(_object_name: String) -> SageWorldQuery:
		## WP16 GATE_IS_OPEN.
		return _refuse_query("units.gate_is_open")

	func siege_is_attached_to_wall(_object_name: String) -> SageWorldQuery:
		## WP16 IS_SEIGE_ATTACHED_TO_WALL [sic].
		return _refuse_query("units.siege_is_attached_to_wall")

	func threat(_object_name: String) -> SageWorldQuery:
		## WP16 SET_COUNTER_TO_UNIT_THREAT.
		return _refuse_query("units.threat")

	func skill_points(_object_name: String) -> SageWorldQuery:
		## WP13 UNIT_HAS_NUM_SKILL_POINTS.
		return _refuse_query("units.skill_points")

	func stance(_object_name: String) -> SageWorldQuery:
		## WP11 UNIT_USING_STANCE. Int from ParamTypes STANCE_TYPE.
		return _refuse_query("units.stance")

	func has_object_status(
		_scope: int, _name: String, _status: String, _require_all: bool
	) -> SageWorldQuery:
		## WP10 UNIT_HAS_OBJECT_STATUS, TEAM_ALL_HAS_OBJECT_STATUS,
		## TEAM_SOME_HAVE_OBJECT_STATUS. `_scope` is SageScriptWorld.Scope.
		return _refuse_query("units.has_object_status")

	func has_command_points_to_build(_player: String, _object_type: String) -> SageWorldQuery:
		## WP16 HAS_COMMAND_POINTS_TO_BUILD_UNIT.
		return _refuse_query("units.has_command_points_to_build")

	func has_delayed_carryover_of_type(_player: String, _object_type: String) -> SageWorldQuery:
		## WP16 HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE.
		return _refuse_query("units.has_delayed_carryover_of_type")

	func unowned_faction_unit_exists(_player: String) -> SageWorldQuery:
		## WP16 SKIRMISH_UNOWNED_FACTION_UNIT_EXISTS.
		return _refuse_query("units.unowned_faction_unit_exists")

	func create_object(
		_object_type: String, _player: String, _position: Vector3, _angle: float
	) -> SageWorldQuery:
		## WP16 CREATE_OBJECT. Returns the new object's script name so the
		## handler can bind a reference; refusal means nothing was spawned.
		return _refuse_query("units.create_object")

	func create_on_team_at(
		_object_type: String, _team: String, _target: Dictionary, _object_name: String
	) -> SageWorldQuery:
		## WP14 CREATE_NAMED_ON_TEAM_AT_WAYPOINT / CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT,
		## WP15 CREATE_NAMED_ON_TEAM_AT_OBJECTTYPE. Empty `_object_name` is the
		## unnamed variant. `_target` is a target_* dictionary.
		return _refuse_query("units.create_on_team_at")

	func spawn_at(
		_object_type: String, _player: String, _object_name: String,
		_position: Vector3, _angle: float
	) -> SageWorldQuery:
		## WP16 UNIT_SPAWN_NAMED_LOCATION_ORIENTATION.
		return _refuse_query("units.spawn_at")

	func create_revival_entry(
		_player: String, _object_type: String, _level: int, _from_carryover: bool
	) -> bool:
		## WP16 CREATE_UNIT_REVIVAL_ENTRY[_FROM_DELAYED_CARRYOVER_HERO],
		## WP09 CREATE_UNIT_REVIVAL_ENTRY_AT_LEVEL.
		return _refuse_command("units.create_revival_entry")

	func create_delayed_carryover_at(
		_player: String, _object_type: String, _target: Dictionary
	) -> bool:
		## WP14 CREATE_DELAYED_CARRYOVER_UNIT_AT_WAYPOINT.
		return _refuse_command("units.create_delayed_carryover_at")

	func delete(_object_name: String) -> bool:
		## WP16 NAMED_DELETE.
		return _refuse_command("units.delete")

	func delete_all_unmanned(_player: String) -> bool:
		## WP16 DELETE_ALL_UNMANNED.
		return _refuse_command("units.delete_all_unmanned")

	func set_team(_object_name: String, _team: String) -> bool:
		## WP15 UNIT_SET_TEAM.
		return _refuse_command("units.set_team")

	func transfer_ownership(_object_name: String, _player: String) -> bool:
		## WP16 NAMED_TRANSFER_OWNERSHIP_PLAYER.
		return _refuse_command("units.transfer_ownership")

	func set_reference(_reference: String, _object_name: String) -> bool:
		## WP16 SET_UNIT_REFERENCE / SET_UNIT_REFERENCE_TO_REFERENCE.
		return _refuse_command("units.set_reference")

	func set_selected(_object_name: String, _selected: bool) -> bool:
		## WP17 SELECT_OBJECT / DESELECT.
		return _refuse_command("units.set_selected")

	func set_attitude(_object_name: String, _mood: int) -> bool:
		## WP16 NAMED_SET_ATTITUDE.
		return _refuse_command("units.set_attitude")

	func set_emoticon(_object_name: String, _emoticon: String, _duration_ticks: int) -> bool:
		## WP16 NAMED_SET_EMOTICON.
		return _refuse_command("units.set_emoticon")

	func force_emotion(_object_name: String, _emotion: int, _duration_ticks: int) -> bool:
		## WP16 UNIT_FORCE_EMOTION.
		return _refuse_command("units.force_emotion")

	func set_held(_object_name: String, _held: bool) -> bool:
		## WP16 NAMED_SET_HELD.
		return _refuse_command("units.set_held")

	func set_repulsor(_object_name: String, _enabled: bool) -> bool:
		## WP16 NAMED_SET_REPULSOR.
		return _refuse_command("units.set_repulsor")

	func set_stealth_enabled(_object_name: String, _enabled: bool) -> bool:
		## WP16 NAMED_SET_STEALTH_ENABLED.
		return _refuse_command("units.set_stealth_enabled")

	func set_strict_control_enabled(_object_name: String, _enabled: bool) -> bool:
		## WP16 NAMED_SET_STRICT_CONTROL_ENABLED.
		return _refuse_command("units.set_strict_control_enabled")

	func set_house_color_enabled(_object_name: String, _enabled: bool) -> bool:
		## WP16 UNIT_ENABLE_HOUSE_COLOR.
		return _refuse_command("units.set_house_color_enabled")

	func set_object_panel_flag(_object_name: String, _flag: String, _enabled: bool) -> bool:
		## WP16 UNIT_AFFECT_OBJECT_PANEL_FLAGS.
		return _refuse_command("units.set_object_panel_flag")

	func set_special_weaponset(_object_name: String, _weaponset: String) -> bool:
		## WP16 NAMED_SET_SPECIAL_WEAPONSET.
		return _refuse_command("units.set_special_weaponset")

	func set_close_range_weapon(_object_name: String, _enabled: bool) -> bool:
		## WP16 NAMED_TOGGLE_CLOSE_RANGE_WEAPON.
		return _refuse_command("units.set_close_range_weapon")

	func set_topple_direction(_object_name: String, _direction: Vector3) -> bool:
		## WP16 NAMED_SET_TOPPLE_DIRECTION.
		return _refuse_command("units.set_topple_direction")

	func set_flame_status(_object_name: String, _burning: bool) -> bool:
		## WP16 UNIT_SET_FLAME_STATUS.
		return _refuse_command("units.set_flame_status")

	func set_model_condition(
		_object_name: String, _condition: String, _enabled: bool, _duration_ticks: int
	) -> bool:
		## WP16 UNIT_SET_MODELCONDITION[_FOR_DURATION].
		return _refuse_command("units.set_model_condition")

	func set_object_status(
		_scope: int, _name: String, _status: String, _enabled: bool
	) -> bool:
		## WP10 UNIT_CHANGE_OBJECT_STATUS / TEAM_CHANGE_OBJECT_STATUS.
		return _refuse_command("units.set_object_status")

	func shock(_object_name: String, _duration_ticks: int) -> bool:
		## WP16 NAMED_SHOCK.
		return _refuse_command("units.shock")

	func stop(_object_name: String, _flush_queue: bool) -> bool:
		## WP16 NAMED_STOP / NAMED_STOP_FLUSH.
		return _refuse_command("units.stop")

	func enter_object(_object_name: String, _target_object: String) -> bool:
		## WP16 NAMED_ENTER_NAMED.
		return _refuse_command("units.enter_object")

	func exit(_object_name: String, _all_contained: bool) -> bool:
		## WP16 NAMED_EXIT_ALL / NAMED_EXIT_BUILDING.
		return _refuse_command("units.exit")

	func exit_specific_building(_object_name: String, _building: String) -> bool:
		## WP16 EXIT_SPECIFIC_BUILDING.
		return _refuse_command("units.exit_specific_building")

	func destroy_all_contained(_object_name: String) -> bool:
		## WP13 UNIT_DESTROY_ALL_CONTAINED.
		return _refuse_command("units.destroy_all_contained")

	func set_gate_state(_object_name: String, _open: bool, _ready: bool) -> bool:
		## WP16 GATE_OPEN / GATE_CLOSE / GATE_READY.
		return _refuse_command("units.set_gate_state")

	func deploy_siege(_object_name: String, _target: Dictionary) -> bool:
		## WP16 DEPLOY_SIEGE_ON_NAMED_WALL, WP14 DEPLOY_[NAMED_]SIEGE_ON_WAYPOINT,
		## WP15 DEPLOY_SIEGE_NEAR_TEAM.
		return _refuse_command("units.deploy_siege")

	func retract_siege(_object_name: String) -> bool:
		## WP16 RETRACT_SIEGE_FROM_WALL.
		return _refuse_command("units.retract_siege")

	func set_cave_index(_object_name: String, _index: int) -> bool:
		## WP16 SET_CAVE_INDEX.
		return _refuse_command("units.set_cave_index")

	func set_hulk_lifetime(_object_name: String, _ticks: int) -> bool:
		## WP16 SCRIPTING_OVERRIDE_HULK_LIFETIME.
		return _refuse_command("units.set_hulk_lifetime")

	func set_warehouse_value(_object_name: String, _value: int) -> bool:
		## WP16 WAREHOUSE_SET_VALUE.
		return _refuse_command("units.set_warehouse_value")

	func build_structure_at(
		_object_name: String, _object_type: String, _target: Dictionary
	) -> bool:
		## WP14 NAMED_BUILD_STRUCTURE_AT_WAYPOINT.
		return _refuse_command("units.build_structure_at")

	func execute_sequential_script(_object_name: String, _script: String, _looping: bool) -> bool:
		## WP16 UNIT_EXECUTE_SEQUENTIAL_SCRIPT[_LOOPING].
		return _refuse_command("units.execute_sequential_script")

	func stop_sequential_script(_object_name: String) -> bool:
		## WP16 UNIT_STOP_SEQUENTIAL_SCRIPT.
		return _refuse_command("units.stop_sequential_script")


# ==========================================================================
# ORDERS - WP14 (movement/pathing), WP11 (guard/idle/stance), WP13 (attack,
# command buttons). One method per VERB; SAGE's NAMED_/TEAM_ split is a Scope.
# ==========================================================================


class Orders:
	extends Facet

	## Movement, guarding and ability orders. `_scope` is SageScriptWorld.Scope
	## and `_name` is the team or object name it selects. `_target` dictionaries
	## come from the SageScriptWorld.target_*() helpers.

	func move_to(_scope: int, _name: String, _target: Dictionary) -> bool:
		## WP14 MOVE_NAMED_UNIT_TO, MOVE_TEAM_TO, TEAM_MOVE_TO_NEAREST_OBJECT_OF_*,
		## UNIT/TEAM_MOVE_TOWARDS_NEAREST_OBJECT_TYPE.
		return _refuse_command("orders.move_to")

	func move_home(_scope: int, _name: String) -> bool:
		## WP14 MOVE_TEAM_HOME.
		return _refuse_command("orders.move_home")

	func follow_waypoint_path(_scope: int, _name: String, _target: Dictionary) -> bool:
		## WP14 NAMED/TEAM_FOLLOW_WAYPOINTS[_EXACT]. Use target_waypoint_path().
		return _refuse_command("orders.follow_waypoint_path")

	func attack_move_to(_scope: int, _name: String, _target: Dictionary) -> bool:
		## WP13 ATTACK_MOVE_NAMED_UNIT_TO, ATTACK_MOVE_TEAM_TO[_NAMED_OBJECT],
		## ATTACK_MOVE_TEAM_TO_NEAREST_OBJECT_OF_TYPE_OF_PLAYER,
		## TEAM_ATTACK_MOVE_FOLLOW_WAYPOINTS, *_ATTACK_MOVE_TOWARDS_NEAREST_*.
		return _refuse_command("orders.attack_move_to")

	func attack(_scope: int, _name: String, _target: Dictionary) -> bool:
		## WP13 NAMED_ATTACK_NAMED / NAMED_ATTACK_TEAM / TEAM_ATTACK_NAMED /
		## TEAM_ATTACK_TEAM.
		return _refuse_command("orders.attack")

	func attack_area(
		_scope: int, _name: String, _area: String, _duration_ticks: int
	) -> bool:
		## WP12 NAMED_ATTACK_AREA[_FOR_SECONDS], TEAM_ATTACK_AREA.
		## `_duration_ticks` <= 0 means indefinitely.
		return _refuse_command("orders.attack_area")

	func hunt(_scope: int, _name: String, _command_button: String) -> bool:
		## WP15 TEAM_HUNT, WP16 NAMED_HUNT, WP17 PLAYER_HUNT,
		## WP13 TEAM_HUNT_WITH_COMMAND_BUTTON (non-empty `_command_button`).
		return _refuse_command("orders.hunt")

	func face(_scope: int, _name: String, _target: Dictionary, _reverse: bool) -> bool:
		## WP14 NAMED/TEAM_FACE_WAYPOINT, WP15 TEAM_FACE_NAMED, WP16 NAMED_FACE_NAMED.
		return _refuse_command("orders.face")

	func stand_ground(_scope: int, _name: String) -> bool:
		## WP15 TEAM_STAND_GROUND, WP16 UNIT_STAND_GROUND.
		return _refuse_command("orders.stand_ground")

	func idle_for_ticks(_scope: int, _name: String, _ticks: int) -> bool:
		## WP11 TEAM/UNIT_IDLE_FOR_FRAMECOUNT / _FOR_SECONDS, IDLE_ALL_UNITS
		## (Scope.PLAYER). The handler converts frames and seconds to ticks.
		return _refuse_command("orders.idle_for_ticks")

	func guard(
		_scope: int, _name: String, _target: Dictionary, _duration_ticks: int
	) -> bool:
		## WP11 the whole *_GUARD* family: TEAM_GUARD, TEAM_GUARD_AREA[_FROM_POSITION],
		## TEAM_GUARD_OBJECT/POSITION/TEAM/NEAREST_KINDOF/IN_TUNNEL_NETWORK,
		## TEAM_GUARD_FOR_FRAMECOUNT/_SECONDS, NAMED_GUARD, UNIT_GUARD_*,
		## WP10 TEAM_GUARD_SUPPLY_CENTER. target_self() is the bare "guard here"
		## variant; `_duration_ticks` <= 0 means indefinitely.
		return _refuse_command("orders.guard")

	func guard_area_from_position(
		_scope: int, _name: String, _area: String, _position: Vector3
	) -> bool:
		## WP11 TEAM/UNIT_GUARD_AREA_FROM_POSITION - two locations at once, so it
		## does not fit the single-target `guard()` shape.
		return _refuse_command("orders.guard_area_from_position")

	func set_stopping_distance(_scope: int, _name: String, _distance: float) -> bool:
		## WP11 SET_STOPPING_DISTANCE, NAMED_SET_STOPPING_DISTANCE.
		return _refuse_command("orders.set_stopping_distance")

	func use_command_button(
		_scope: int, _name: String, _command_button: String, _target: Dictionary
	) -> bool:
		## WP13 the whole *_USE_COMMANDBUTTON_* family (~20 actions), including
		## the ABILITY / _AT_WAYPOINT / _ON_NAMED / _ON_NEAREST_* variants and
		## WP13 SKIRMISH_PERFORM_COMMANDBUTTON_ON_MOST_VALUABLE_OBJECT. Use
		## target_self() for the no-target ability variants.
		return _refuse_command("orders.use_command_button")

	func use_command_button_partial(
		_team: String, _command_button: String, _count: int, _target: Dictionary
	) -> bool:
		## WP13 TEAM_PARTIAL_USE_COMMANDBUTTON - a fraction of the team, so the
		## member count is part of the order.
		return _refuse_command("orders.use_command_button_partial")

	func set_auto_ability(
		_scope: int, _name: String, _command_button: String, _enabled: bool
	) -> bool:
		## WP13 NAME_SET_AUTO_ABILITY [sic], TEAM_SET_AUTO_ABILITY.
		return _refuse_command("orders.set_auto_ability")

	func apply_attack_priority_set(_scope: int, _name: String, _set_name: String) -> bool:
		## WP11 NAMED_APPLY_ATTACK_PRIORITY_SET, TEAM_APPLY_ATTACK_PRIORITY_SET.
		return _refuse_command("orders.apply_attack_priority_set")

	func fire_weapon_following_path(_object_name: String, _waypoint_path: String) -> bool:
		## WP13 NAMED_FIRE_WEAPON_FOLLOWING_WAYPOINT_PATH.
		return _refuse_command("orders.fire_weapon_following_path")

	func attack_follow_waypoints(_object_name: String, _waypoint_path: String) -> bool:
		## WP13 NAMED_ATTACK_FOLLOW_WAYPOINTS.
		return _refuse_command("orders.attack_follow_waypoints")

	func reached_waypoints_end(_scope: int, _name: String, _waypoint_path: String) -> SageWorldQuery:
		## WP14 NAMED/TEAM_REACHED_WAYPOINTS_END.
		return _refuse_query("orders.reached_waypoints_end")

	func can_path_to(_scope: int, _name: String, _target: Dictionary) -> SageWorldQuery:
		## WP14 NAMED/TEAM_CAN_PATH_TO_WAYPOINT, WP15/WP16 *_CAN_PATH_TO_OBJECT
		## and *_CAN_PATH_INTO_PLAYERS_NEAREST_BASE.
		return _refuse_query("orders.can_path_to")

	func distance_between(
		_scope_a: int, _name_a: String, _scope_b: int, _name_b: String
	) -> SageWorldQuery:
		## WP11 DISTANCE_BETWEEN_OBJ, DISTANCE_BETWEEN_TEAM. Float world units.
		return _refuse_query("orders.distance_between")

	func issued_formation_order(_player: String) -> SageWorldQuery:
		## WP11 PLAYER_ISSUED_FORMATION_ORDER.
		return _refuse_query("orders.issued_formation_order")

	func in_alt_formation(_object_name: String) -> SageWorldQuery:
		## WP08 UNIT_IN_ALT_FORMATION.
		return _refuse_query("orders.in_alt_formation")


# ==========================================================================
# COMBAT - WP13 (damage, kills, special powers)
# ==========================================================================


class Combat:
	extends Facet

	## Damage, death and special powers.

	func damage(_scope: int, _name: String, _amount: float) -> bool:
		## WP13 NAMED_DAMAGE, DAMAGE_MEMBERS_OF_TEAM.
		return _refuse_command("combat.damage")

	func kill(_scope: int, _name: String) -> bool:
		## WP13 NAMED_KILL, TEAM_KILL, PLAYER_KILL.
		return _refuse_command("combat.kill")

	func kill_horde_members(_team: String) -> bool:
		## WP13 KILL_HORDE_MEMBERS.
		return _refuse_command("combat.kill_horde_members")

	func set_health_percent(_scope: int, _name: String, _percent: float) -> bool:
		## WP13 TEAM_SET_HEALTH.
		return _refuse_command("combat.set_health_percent")

	func team_health_percent(_team: String) -> SageWorldQuery:
		## WP13 EVAL_TEAM_HEALTH. Float 0..100.
		return _refuse_query("combat.team_health_percent")

	func fire_special_power(
		_scope: int, _name: String, _power: String, _target: Dictionary
	) -> bool:
		## WP13 NAMED_FIRE_SPECIAL_POWER_AT_NAMED/_AT_WAYPOINT,
		## PLAYER_FIRE_SPECIAL_POWER_AT_WAYPOINT, FIRE_SPECIAL_POWER_ON_TEAM,
		## FIRE_SPECIAL_POWER_ON_NEAREST_OBJECTTYPE[_BY_PLAYER],
		## SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST.
		return _refuse_command("combat.fire_special_power")

	func set_special_power_countdown(
		_scope: int, _name: String, _power: String, _ticks: int, _relative: bool
	) -> bool:
		## WP13 NAMED_SET_SPECIAL_POWER_COUNTDOWN, PLAYER_SET_SPECIAL_POWER_COUNTDOWN
		## and NAMED_ADD_SPECIAL_POWER_COUNTDOWN (`_relative` true).
		return _refuse_command("combat.set_special_power_countdown")

	func set_special_power_countdown_running(
		_object_name: String, _power: String, _running: bool
	) -> bool:
		## WP13 NAMED_START/STOP_SPECIAL_POWER_COUNTDOWN.
		return _refuse_command("combat.set_special_power_countdown_running")

	func special_power_ready(_scope: int, _name: String, _power: String) -> SageWorldQuery:
		## WP13 SKIRMISH_SPECIAL_POWER_READY, SKIRMISH_FIRE_SPECIAL_POWER_ON_TEAM.
		return _refuse_query("combat.special_power_ready")

	func special_power_phase(
		_player: String, _power: String, _phase: String, _from_object: String
	) -> SageWorldQuery:
		## WP12/WP13 PLAYER_TRIGGERED/MIDWAY/COMPLETED_SPECIAL_POWER[_FROM_NAMED].
		## `_phase` is "triggered", "midway" or "completed"; empty `_from_object`
		## is the any-source variant.
		return _refuse_query("combat.special_power_phase")

	func command_button_ready_count(_team: String, _command_button: String) -> SageWorldQuery:
		## WP13 SKIRMISH_COMMAND_BUTTON_READY_ALL / _PARTIAL,
		## WP11 SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL / _PARTIAL.
		return _refuse_query("combat.command_button_ready_count")

	func attacked_by(_scope: int, _name: String, _attacker: Dictionary) -> SageWorldQuery:
		## WP13 NAMED/TEAM_ATTACKED_BY_PLAYER / _BY_OBJECTTYPE,
		## SKIRMISH_PLAYER_HAS_BEEN_ATTACKED_BY_PLAYER.
		return _refuse_query("combat.attacked_by")

	func attacked_and_cannot_retaliate(_object_name: String) -> SageWorldQuery:
		## WP13 UNIT_IS_ATTACKED_AND_CANNOT_RETALIATE.
		return _refuse_query("combat.attacked_and_cannot_retaliate")

	func kill_count_of_type(
		_player: String, _object_type: String, _is_kindof: bool
	) -> SageWorldQuery:
		## WP13 PLAYER_HAS_KILLED_TYPE_UNITS / _KINDOF_UNITS,
		## SET_PLAYER_KILLS_OF_TYPE_TO_COUNTER / _OF_KINDOF_TO_COUNTER.
		return _refuse_query("combat.kill_count_of_type")

	func buildings_destroyed_by(_player: String, _victim: String) -> SageWorldQuery:
		## WP13 PLAYER_DESTROYED_N_BUILDINGS_PLAYER.
		return _refuse_query("combat.buildings_destroyed_by")

	func player_all_destroyed(_player: String, _build_facilities_only: bool) -> SageWorldQuery:
		## WP13 PLAYER_ALL_DESTROYED / PLAYER_ALL_BUILDFACILITIES_DESTROYED.
		return _refuse_query("combat.player_all_destroyed")

	func attack_nearest_group_with_value(
		_team: String, _value: int, _comparison: int
	) -> bool:
		## WP13 SKIRMISH_ATTACK_NEAREST_GROUP_WITH_VALUE.
		return _refuse_command("combat.attack_nearest_group_with_value")

	func set_victim_selection_normal(_enabled: bool) -> bool:
		## WP12 CHOOSE_VICTIM_ALWAYS_USES_NORMAL.
		return _refuse_command("combat.set_victim_selection_normal")

	func set_bonuses_allowed(_object_name: String, _allowed: bool) -> bool:
		## WP12 OBJECT_ALLOW_BONUSES.
		return _refuse_command("combat.set_bonuses_allowed")

	func using_bloodthirsty(_player: String) -> SageWorldQuery:
		## WP08 ANY_UNITS_USING_BLOODTHIRSTY.
		return _refuse_query("combat.using_bloodthirsty")

	func using_autopickup(_object_name: String) -> SageWorldQuery:
		## WP08 UNIT_USING_AUTOPICKUP.
		return _refuse_query("combat.using_autopickup")


# ==========================================================================
# PROGRESSION - WP09 (experience, levels, upgrades, spellbook/science)
# ==========================================================================


class Progression:
	extends Facet

	## Experience, veterancy, upgrades and the spellbook.

	func experience_points(_scope: int, _name: String) -> SageWorldQuery:
		## WP09 SET_UNIT_EXPERIENCE_TO_COUNTER.
		return _refuse_query("progression.experience_points")

	func set_experience_points(_scope: int, _name: String, _points: int) -> bool:
		## WP09 UNIT_SET_EXPERIENCE_POINTS, TEAM_SET_EXPERIENCE_POINTS.
		return _refuse_command("progression.set_experience_points")

	func give_experience_points(_scope: int, _name: String, _points: int) -> bool:
		## WP09 UNIT_GIVE_EXPERIENCE_POINTS, TEAM_GIVE_EXPERIENCE_POINTS.
		return _refuse_command("progression.give_experience_points")

	func give_experience_levels(_scope: int, _name: String, _levels: int) -> bool:
		## WP09 UNIT/TEAM_GIVE_EXPERIENCE_LEVEL, UNIT_GAIN_LEVEL, TEAM_GAIN_LEVEL.
		return _refuse_command("progression.give_experience_levels")

	func level(_scope: int, _name: String) -> SageWorldQuery:
		## WP09 UNIT_IS_AT_LEVEL, NAMED_RANK_LEVEL, WP16 UNIT_COMPARE_RANK.
		return _refuse_query("progression.level")

	func gained_level(_object_name: String) -> SageWorldQuery:
		## WP09 UNIT_HAS_GAINED_LEVEL - an edge, not a level read.
		return _refuse_query("progression.gained_level")

	func set_max_level(_object_name: String, _level: int) -> bool:
		## WP09 UNIT_SET_MAX_LEVEL.
		return _refuse_command("progression.set_max_level")

	func units_leveled_up(_player: String) -> SageWorldQuery:
		## WP08 NUM_UNITS_LEVELED_UP.
		return _refuse_query("progression.units_leveled_up")

	func any_hero_reached_rank(_player: String, _rank: int) -> SageWorldQuery:
		## WP08 ANY_HERO_REACHED_RANK.
		return _refuse_query("progression.any_hero_reached_rank")

	func has_object_of_veterancy(_player: String, _veterancy: String) -> SageWorldQuery:
		## WP09 PLAYER_HAS_OBJECT_OF_VETERANCY.
		return _refuse_query("progression.has_object_of_veterancy")

	func set_experience_receiving(_player: String, _enabled: bool) -> bool:
		## WP09 PLAYER_AFFECT_RECEIVING_EXPERIENCE.
		return _refuse_command("progression.set_experience_receiving")

	func set_hero_experience_sharing(_player: String, _enabled: bool) -> bool:
		## WP09 SET_HERO_EXPERIENCE_SHARING.
		return _refuse_command("progression.set_hero_experience_sharing")

	func give_upgrade(_scope: int, _name: String, _upgrade: String) -> bool:
		## WP09 GIVE_PLAYER_UPGRADE, NAMED_RECEIVE_UPGRADE, TEAM_UPGRADE,
		## TEAM_GIVE_TEAM_UPGRADE, TEAM_GIVE_NEAREST_TEAM_UPGRADE.
		return _refuse_command("progression.give_upgrade")

	func has_upgrade(_scope: int, _name: String, _upgrade: String) -> SageWorldQuery:
		## WP09 PLAYER_BUILT_UPGRADE, PLAYER_BUILT_UPGRADE_FROM_NAMED.
		return _refuse_query("progression.has_upgrade")

	func unit_count_with_upgrade(_player: String, _upgrade: String) -> SageWorldQuery:
		## WP08 PLAYER_HAS_NUM_UNITS_WITH_UPGRADE.
		return _refuse_query("progression.unit_count_with_upgrade")

	func build_upgrade(_player: String, _upgrade: String) -> bool:
		## WP09 AI_PLAYER_BUILD_UPGRADE - queue it, do not grant it.
		return _refuse_command("progression.build_upgrade")

	func upgrade_nearest_wall(_player: String, _upgrade: String, _origin: String) -> bool:
		## WP09 UPGRADE_NEAREST_WALL.
		return _refuse_command("progression.upgrade_nearest_wall")

	func grant_science(_player: String, _science: String) -> bool:
		## WP09 PLAYER_GRANT_SCIENCE.
		return _refuse_command("progression.grant_science")

	func purchase_science(_player: String, _science: String) -> bool:
		## WP09 PLAYER_PURCHASE_SCIENCE.
		return _refuse_command("progression.purchase_science")

	func set_science_availability(
		_player: String, _science: String, _availability: String
	) -> bool:
		## WP09 PLAYER_SCIENCE_AVAILABILITY.
		return _refuse_command("progression.set_science_availability")

	func has_science(_player: String, _science: String) -> SageWorldQuery:
		## WP09 PLAYER_ACQUIRED_SCIENCE.
		return _refuse_query("progression.has_science")

	func can_purchase_science(_player: String, _science: String) -> SageWorldQuery:
		## WP09 PLAYER_CAN_PURCHASE_SCIENCE.
		return _refuse_query("progression.can_purchase_science")

	func science_purchase_points(_player: String) -> SageWorldQuery:
		## WP09 PLAYER_HAS_SCIENCEPURCHASEPOINTS.
		return _refuse_query("progression.science_purchase_points")


# ==========================================================================
# ECONOMY - WP10 (money, supply, time)
# ==========================================================================


class Economy:
	extends Facet

	## Resources and supply. The money methods default to the pre-facet
	## SageScriptWorld.player_money()/set_player_money() pair, so a world that
	## implements only those already serves them here.

	func money(player: String) -> SageWorldQuery:
		## WP10 PLAYER_HAS_CREDITS, WP01 SET_PLAYER_MONEY_TO_COUNTER.
		if world == null or not world.supports(SageScriptWorld.CAP_PLAYER_MONEY):
			return _refuse_query("economy.money")
		return SageWorldQuery.hit(world.player_money(player))

	func set_money(player: String, amount: int) -> bool:
		## WP10. Absolute set (PLAYER_SET_MONEY).
		if world == null:
			return _refuse_command("economy.set_money")
		if not world.set_player_money(player, amount):
			return _refuse_command("economy.set_money")
		return true

	func give_money(player: String, amount: int) -> bool:
		## WP10 PLAYER_GIVE_MONEY. Relative; negative takes money away.
		if world == null or not world.supports(SageScriptWorld.CAP_PLAYER_MONEY):
			return _refuse_command("economy.give_money")
		return world.set_player_money(player, world.player_money(player) + amount)

	func supply_source_state(_player: String, _safe: bool) -> SageWorldQuery:
		## WP10 SUPPLY_SOURCE_ATTACKED / SUPPLY_SOURCE_SAFE.
		return _refuse_query("economy.supply_source_state")

	func supplies_within_distance(
		_player: String, _origin: String, _distance: float
	) -> SageWorldQuery:
		## WP11 SKIRMISH_SUPPLIES_VALUE_WITHIN_DISTANCE.
		return _refuse_query("economy.supplies_within_distance")

	func value_in_area(_player: String, _area: String) -> SageWorldQuery:
		## WP12 SKIRMISH_VALUE_IN_AREA.
		return _refuse_query("economy.value_in_area")

	func resume_supply_trucking(_player: String) -> bool:
		## WP10 RESUME_SUPPLY_TRUCKING.
		return _refuse_command("economy.resume_supply_trucking")

	func build_supply_center(_player: String, _object_type: String, _distance: float) -> bool:
		## WP10 AI_PLAYER_BUILD_SUPPLY_CENTER.
		return _refuse_command("economy.build_supply_center")

	func set_scoring_enabled(_enabled: bool) -> bool:
		## WP17 ENABLE_SCORING / DISABLE_SCORING. Global, not per player.
		return _refuse_command("economy.set_scoring_enabled")


# ==========================================================================
# AREAS - WP12 trigger areas, plus waypoint geometry used by WP14
# ==========================================================================


class Areas:
	extends Facet

	## Trigger areas and waypoints - "where things are", not "what they are".

	func exists(_area: String) -> SageWorldQuery:
		## WP12 SKIRMISH_NAMED_AREA_EXIST.
		return _refuse_query("areas.exists")

	func contains(_area: String, _position: Vector3) -> SageWorldQuery:
		## WP12. Point-in-area, the primitive under most membership conditions.
		return _refuse_query("areas.contains")

	func member_count(_scope: int, _name: String, _area: String) -> SageWorldQuery:
		## WP12 TEAM_INSIDE_AREA_ENTIRELY / _PARTIALLY, NAMED_INSIDE_AREA,
		## NAMED_OUTSIDE_AREA, TEAM_OUTSIDE_AREA_ENTIRELY,
		## SKIRMISH_PLAYER_HAS_UNITS_IN_AREA, SKIRMISH_PLAYER_IS_OUTSIDE_AREA.
		## Answers "how many of `_name` are inside `_area` right now"; the
		## handler compares against teams.unit_count() for the ENTIRELY variants.
		return _refuse_query("areas.member_count")

	func transition_count(
		_scope: int, _name: String, _area: String, _entered: bool
	) -> SageWorldQuery:
		## WP12 NAMED_ENTERED_AREA / _EXITED_AREA, TEAM_ENTERED_AREA_ENTIRELY /
		## _PARTIALLY, TEAM_EXITED_AREA_*. These are EDGES since the last
		## evaluation, not membership - a world must not answer them from
		## containment alone.
		return _refuse_query("areas.transition_count")

	func unit_count_in_area(
		_player: String, _area: String, _filter: Dictionary
	) -> SageWorldQuery:
		## WP12 PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA and its _KIND_,
		## _COMPLETELY_BUILT and _WITH_UPGRADE (WP09) siblings. `_filter` keys:
		## "object_type", "kindof", "upgrade", "completely_built" (bool),
		## "under_attack" (bool, WP12 PLAYER_HAS_UNIT_TYPE_IN_TRIGGER_AREA_UNDER_ATTACK).
		return _refuse_query("areas.unit_count_in_area")

	func tree_count(_area: String) -> SageWorldQuery:
		## WP12 COMPARISON_TREES_IN_TRIGGER_AREA.
		return _refuse_query("areas.tree_count")

	func is_on_fire(_area: String) -> SageWorldQuery:
		## WP12 IS_AREA_ON_FIRE.
		return _refuse_query("areas.is_on_fire")

	func set_human_impassable(_area: String, _impassable: bool) -> bool:
		## WP12 HUMAN_IMPASSABLE_AREA.
		return _refuse_command("areas.set_human_impassable")

	func waypoint_position(_waypoint: String) -> SageWorldQuery:
		## WP14/WP12. Vector3; the primitive under every _AT_WAYPOINT action.
		return _refuse_query("areas.waypoint_position")

	func waypoint_path_exists(_path: String) -> SageWorldQuery:
		## WP14. Whether a named waypoint path is present on this map.
		return _refuse_query("areas.waypoint_path_exists")

	func object_inside_base(
		_object_or_type: String, _base_reference: String, _is_type: bool
	) -> SageWorldQuery:
		## WP11 OBJECT_OF_TYPE_OR_LIST_INSIDE_REFD_BASE.
		return _refuse_query("areas.object_inside_base")

	func base_transition_count(
		_scope: int, _name: String, _base_reference: String, _entered: bool, _entirely: bool
	) -> SageWorldQuery:
		## WP11 NAMED/TEAM_ENTERED/EXITED_NEAREST_BASE and _REFD_BASE variants.
		## Empty `_base_reference` is the NEAREST_BASE form.
		return _refuse_query("areas.base_transition_count")


# ==========================================================================
# TERRAIN - WP12 terrain, water, weather, bridges
# ==========================================================================


class Terrain:
	extends Facet

	func bridge_state(_bridge: String) -> SageWorldQuery:
		## WP12 BRIDGE_BROKEN / BRIDGE_REPAIRED. String state name.
		return _refuse_query("terrain.bridge_state")

	func set_water_height(_water: String, _height: float, _duration_ticks: int) -> bool:
		## WP12 WATER_CHANGE_HEIGHT / WATER_CHANGE_HEIGHT_OVER_TIME.
		## `_duration_ticks` <= 0 is the instant variant.
		return _refuse_command("terrain.set_water_height")

	func set_burn_rate(_target: Dictionary, _rate: float, _max_burn: float) -> bool:
		## WP12 CHANGE_BURN_RATE_AT_WAYPOINT / CHANGE_BURN_RATE_IN_AREA.
		return _refuse_command("terrain.set_burn_rate")

	func set_cloud_speed(_multiplier: float) -> bool:
		## WP12 MAP_CHANGE_CLOUD_SPEED.
		return _refuse_command("terrain.set_cloud_speed")

	func switch_border(_border_index: int) -> bool:
		## WP12 MAP_SWITCH_BORDER.
		return _refuse_command("terrain.switch_border")

	func set_logic_fog_state(_state: String) -> bool:
		## WP12 SET_LOGIC_FOG_STATE. Distinct from shroud (Fog facet, WP02):
		## this is the simulation-visible weather fog, not fog of war.
		return _refuse_command("terrain.set_logic_fog_state")

	func set_buildability(_object_type: String, _buildability: int) -> bool:
		## WP12 TECHTREE_MODIFY_BUILDABILITY_OBJECT. `_buildability` is a
		## ParamTypes BUILDABILITY_TYPE int.
		return _refuse_command("terrain.set_buildability")


# ==========================================================================
# AI - WP11 (skirmish AI, base building, attack priorities, reinforcements)
# ==========================================================================


class Ai:
	extends Facet

	func build_base_building(
		_building_type: String, _base: String, _result_reference: String
	) -> bool:
		## WP11 BUILD_BASE_BUILDING only. Sourced: (OBJECT_TYPE, UNIT, UNIT_REF)
		## - the engine template reads "Build building <OBJECT_TYPE> in base
		## <UNIT> and reference it as <UNIT_REF>". The base is the base OBJECT
		## the building goes into (resolved through the shared
		## object/unit-reference namespace) and the result reference is a
		## DESTINATION the new building is bound to; retail authors no player
		## (ownership follows the base). The _IN_SLOT and _PER_TACTICAL_MARKER
		## spellings carry extra load-bearing arguments (slot index; near/far
		## side + marker type) and need their own methods - see WP11's
		## GAP_BUILD_PER_MARKER for the sourced per-marker signature.
		return _refuse_command("ai.build_base_building")

	func build_on_foundation(_player: String, _foundation: String, _building_type: String) -> bool:
		## WP11 BUILD_BUILDING_ON_FOUNDATION.
		return _refuse_command("ai.build_on_foundation")

	func sell_on_foundation(_player: String, _foundation: String) -> bool:
		## WP11 SELL_BUILDING_ON_FOUNDATION.
		return _refuse_command("ai.sell_on_foundation")

	func skirmish_build(
		_player: String, _building_type: String, _placement: String, _count: int
	) -> bool:
		## WP11 SKIRMISH_BUILD_BUILDING, SKIRMISH_BUILD_BASE_DEFENSE_FRONT /
		## _FLANK, WP17 SKIRMISH_BUILD_STRUCTURE_FRONT / _FLANK. `_placement` is
		## "", "front" or "flank".
		return _refuse_command("ai.skirmish_build")

	func set_buildings_allowed(_player: String, _building_type: String, _allowed: bool) -> bool:
		## WP11 ALLOW_DISALLOW_ALL_BUILDINGS / ALLOW_DISALLOW_ONE_BUILDING. Empty
		## `_building_type` is the ALL variant.
		return _refuse_command("ai.set_buildings_allowed")

	func base_population(_player: String) -> SageWorldQuery:
		## WP11 SET_COUNTER_TO_BASE_POPULATION.
		return _refuse_query("ai.base_population")

	func base_unpack(_object_name: String, _free: bool, _result_reference: String) -> bool:
		## WP11 NAMED_BASE_UNPACK (`free` false) / NAMED_BASE_UNPACK_FREE
		## (`free` true). Sourced: (UNIT, UNIT_REF) - the base at <UNIT> unpacks
		## and the resulting base object is referenced as <UNIT_REF>. The result
		## reference is a DESTINATION every retail AI call site binds (paid
		## unpacks bind AI_EXPANSION_1..N; free unpacks bind AI_BASE, later read
		## as a plain UNIT), so the world must bind it in the shared
		## object/unit-reference namespace, resolved to the concrete unpacked
		## object at call time. An empty reference means "bind nothing".
		return _refuse_command("ai.base_unpack")

	func base_unpackable(_object_name: String, _player: String) -> SageWorldQuery:
		## WP11 NAMED_BASE_UNPACKABLE_FOR_PLAYER.
		return _refuse_query("ai.base_unpackable")

	func camps_should_unpack(_region: String) -> SageWorldQuery:
		## WP11 REGION_CAMPS_SHOULD_UNPACK.
		return _refuse_query("ai.camps_should_unpack")

	func create_reinforcement_team(
		_player: String, _team: String, _target: Dictionary
	) -> bool:
		## WP11 CREATE_REINFORCEMENT_TEAM[_AT_UNIT_POSITION].
		return _refuse_command("ai.create_reinforcement_team")

	func remove_reinforcement_army(_player: String, _army: String) -> bool:
		## WP11 REMOVE_REINFORCEMENT_ARMY.
		return _refuse_command("ai.remove_reinforcement_army")

	func set_attack_priority(
		_set_name: String, _target_kind: String, _target_name: String, _priority: int
	) -> bool:
		## WP11 SET_ATTACK_PRIORITY_THING / SET_ATTACK_PRIORITY_KIND_OF.
		## `_target_kind` is "thing" or "kindof".
		return _refuse_command("ai.set_attack_priority")

	func set_default_attack_priority(_set_name: String, _priority: int) -> bool:
		## WP11 SET_DEFAULT_ATTACK_PRIORITY.
		return _refuse_command("ai.set_default_attack_priority")

	func move_to_approach_path(_scope: int, _name: String, _path: int) -> bool:
		## WP11 SKIRMISH_MOVE_TO_APPROACH_PATH, WP17 SKIRMISH_FOLLOW_APPROACH_PATH.
		## `_path` is a ParamTypes SKIRMISH_APPROACH_PATH int.
		return _refuse_command("ai.move_to_approach_path")

	func set_view_guardband(_width: float, _height: float) -> bool:
		## WP11 RESIZE_VIEW_GUARDBAND. Affects AI awareness, not the camera.
		return _refuse_command("ai.set_view_guardband")


# ==========================================================================
# META - WP08 (campaign, LivingWorld, victory/defeat, game mode), plus the
# global time and object-list controls from WP10
# ==========================================================================


class Meta:
	extends Facet

	func game_mode(_mode: String) -> SageWorldQuery:
		## WP08 IS_GAME_MODE_ACTIVE, IS_GAME_IN_SKIRMISH_OR_MULTIPLAYER.
		return _refuse_query("meta.game_mode")

	func player_count(_include_observers: bool) -> SageWorldQuery:
		## WP08 COMPARE_NUM_PLAYERS_IN_GAME.
		return _refuse_query("meta.player_count")

	func multiplayer_outcome(_player: String, _outcome: String) -> SageWorldQuery:
		## WP08 MULTIPLAYER_PLAYER_DEFEAT, MULTIPLAYER_ALLIED_VICTORY / _DEFEAT.
		## `_outcome` is "defeat", "allied_victory" or "allied_defeat".
		return _refuse_query("meta.multiplayer_outcome")

	func declare_local_defeat(_player: String) -> bool:
		## WP08 LOCALDEFEAT.
		return _refuse_command("meta.declare_local_defeat")

	func mission_attempts() -> SageWorldQuery:
		## WP17 MISSION_ATTEMPTS.
		return _refuse_query("meta.mission_attempts")

	func exit_map(_player: String) -> bool:
		## WP12 MAP_EXIT.
		return _refuse_command("meta.exit_map")

	func set_time_frozen(_frozen: bool) -> bool:
		## WP10 FREEZE_TIME / UNFREEZE_TIME. SIMULATION time, which is why this
		## is a facet and not a presentation sink.
		return _refuse_command("meta.set_time_frozen")

	func object_list_change(_list_name: String, _object_type: String, _add: bool) -> bool:
		## WP10 OBJECTLIST_ADDOBJECTTYPE / OBJECTLIST_REMOVEOBJECTTYPE.
		return _refuse_command("meta.object_list_change")

	func banner_pressed(_player: String) -> SageWorldQuery:
		## WP08 BANNER_PRESSED.
		return _refuse_query("meta.banner_pressed")

	func zone_focus_more_than(_zone: String, _amount: int) -> SageWorldQuery:
		## WP08 ZONE_FOCUS_MORE_THAN.
		return _refuse_query("meta.zone_focus_more_than")

	func living_world_command(_command: String, _arguments: Dictionary) -> bool:
		## WP08 the LIVING_WORLD_* family (11 actions) plus
		## PLAYER/TEAM/UNIT_ASSIMILATE_WITH_ARMY_BY_NAME and
		## REINFORCEMENTS_DISPLAY_BANNER. LivingWorld is the campaign
		## strategic layer, which this project does not model at all; rather
		## than inventing eleven speculative signatures for a subsystem with no
		## design, the whole family funnels through one command carrying the
		## action name and its decoded arguments. Refusal is the expected answer
		## until a campaign layer exists.
		return _refuse_command("meta.living_world_command")

	func living_world_query(_query_name: String, _arguments: Dictionary) -> SageWorldQuery:
		## WP08 LIVING_WORLD_IS_REGION_REF_BOUND and future LivingWorld reads.
		return _refuse_query("meta.living_world_query")


# ==========================================================================
# FOG - WP02. BLOCKED: no fog-of-war subsystem exists in either simulation.
# ==========================================================================


class Fog:
	extends Facet

	## Shroud and reveal.
	##
	## WP02 is a BLOCKED package: neither the retail slice nor the current
	## simulation models fog of war at all. The surface is declared here so the
	## package's handlers have somewhere to land the day the subsystem is built,
	## and so a mission that shrouds the map fails at a named world method
	## instead of quietly running with full vision. Until then every call
	## refuses, which is the correct answer.

	func reveal(_player: String, _target: Dictionary, _permanent: bool) -> bool:
		## WP02 MAP_REVEAL_ALL[_PERM], MAP_REVEAL_AT_WAYPOINT, MAP_REVEAL_IN_TRIGGER,
		## MAP_REVEAL_PERMANENTLY_AT_WAYPOINT / _IN_TRIGGER.
		return _refuse_command("fog.reveal", "no fog-of-war subsystem exists yet")

	func shroud(_player: String, _target: Dictionary) -> bool:
		## WP02 MAP_SHROUD_ALL, MAP_SHROUD_AT_WAYPOINT.
		return _refuse_command("fog.shroud", "no fog-of-war subsystem exists yet")

	func undo_permanent_reveal(_player: String, _target: Dictionary) -> bool:
		## WP02 MAP_REVEAL_ALL_UNDO_PERM, MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT /
		## _IN_TRIGGER.
		return _refuse_command("fog.undo_permanent_reveal", "no fog-of-war subsystem exists yet")

	func set_border_shroud(_enabled: bool) -> bool:
		## WP02 ENABLE_BORDER_SHROUD / DISABLE_BORDER_SHROUD.
		return _refuse_command("fog.set_border_shroud", "no fog-of-war subsystem exists yet")


# ==========================================================================
# TRANSPORT - WP03 and WP04. BLOCKED: no passenger/garrison/capture subsystem.
# ==========================================================================


class Transport:
	extends Facet

	## Garrison, transport passengers, capture and teleport.
	##
	## WP03 and WP04 are BLOCKED packages for the same reason as Fog: the
	## simulation has no passenger-transport, garrison, allegiance-capture or
	## disguise model, and inventing one from the script layer would produce an
	## interface the eventual subsystem has to fight. Declared, refusing, honest.

	func garrison(_scope: int, _name: String, _target: Dictionary, _instantly: bool) -> bool:
		## WP03 NAMED/TEAM_GARRISON_NEAREST_BUILDING, _SPECIFIC_BUILDING[_INSTANTLY],
		## TEAM_GARRISON_TEAM_INSTANTLY, PLAYER_GARRISON_ALL_BUILDINGS.
		return _refuse_command("transport.garrison", "no garrison subsystem exists yet")

	func load_transports(_team: String) -> bool:
		## WP03 TEAM_LOAD_TRANSPORTS.
		return _refuse_command("transport.load_transports", "no transport subsystem exists yet")

	func passenger_count(_object_name: String) -> SageWorldQuery:
		## WP03 UNIT_HAS_PASSENGER, WP08 PLAYER_HAS_NUM_UNITS_LOADED_WITH_OBJECT.
		return _refuse_query("transport.passenger_count", "no transport subsystem exists yet")

	func garrisoned_count(_player: String) -> SageWorldQuery:
		## WP03 SKIRMISH_PLAYER_HAS_COMPARISON_GARRISONED.
		return _refuse_query("transport.garrisoned_count", "no garrison subsystem exists yet")

	func capture_nearest_unowned(_team: String) -> bool:
		## WP04 TEAM_CAPTURE_NEAREST_UNOWNED_FACTION_UNIT.
		return _refuse_command("transport.capture_nearest_unowned", "no capture subsystem exists yet")

	func captured_unit_count(_player: String) -> SageWorldQuery:
		## WP04 SKIRMISH_PLAYER_HAS_COMPARISON_CAPTURED_UNITS.
		return _refuse_query("transport.captured_unit_count", "no capture subsystem exists yet")

	func create_team_from_captured(_player: String, _team: String) -> bool:
		## WP04 PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS.
		return _refuse_command("transport.create_team_from_captured", "no capture subsystem exists yet")

	func teleport_to(_scope: int, _name: String, _target: Dictionary) -> bool:
		## WP04 UNIT_TELEPORT_TO_WAYPOINT, TEAM_TELEPORT_TO_WAYPOINT.
		return _refuse_command("transport.teleport_to", "no teleport support exists yet")

	func has_toggled_weapon(_object_name: String) -> SageWorldQuery:
		## WP04 UNIT_HAS_TOGGLED_WEAPON.
		return _refuse_query("transport.has_toggled_weapon", "no weapon-mode toggle model exists yet")
