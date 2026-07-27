class_name RetailSliceScriptWorld
extends SageScriptWorld

## SageScriptWorld backed by a live RetailSliceSim - the first script world
## whose answers come from a real simulation instead of test fixtures.
##
## SCOPE DISCIPLINE (read before adding a method)
## ==============================================
## This adapter implements EXACTLY the facet methods the RetailSliceSim can
## answer truthfully from its own state today. Everything else inherits the
## base-class refusal untouched. A refusal is recorded by the dispatcher as a
## structured gap; a plausible-but-wrong answer silently corrupts a match, so
## "could be faked" is never implemented here. If a method you need refuses,
## the gap table in the packet report is the specification for closing it -
## teach the SIM the state first, then extend this adapter.
##
## NAME BINDINGS
## =============
## Script arguments name players, teams and objects with strings; the sim
## models teams as small ints with no name registry. The integrator therefore
## binds script names to sim teams before the match starts, identically on
## every peer (bindings are match configuration, exactly like the roster):
##
##     world.bind_player("Player_1", RetailSliceSim.PLAYER_TEAM)
##     world.bind_team("teamPlayer_1", RetailSliceSim.PLAYER_TEAM)
##
## BINDINGS ARE DECLARED EXHAUSTIVE: every player and default team a map
## script can address must be bound. That contract is what lets
## players.exists()/teams.exists() answer false for an unbound name instead
## of refusing - an unbound name IS "not in this match".
##
## A bound script "team" is a player's whole roster (the SAGE default team).
## Sub-player script teams (the WorldBuilder team registry) are not modeled
## by the sim and stay refused.
##
## THE SCRIPT PLAYER. Retail executes each AI player's script libraries in
## that player's context, and several base-building actions carry NO player
## argument at all (NAMED_BASE_UNPACK unpacks "my" base; the sourced
## signature has no player slot). The integrator therefore also binds WHOSE
## scripts this world instance executes - match configuration exactly like
## the name bindings, identical on every peer:
##
##     world.bind_script_player("Player_1")   # a name bound via bind_player
##
## Playerless AI actions act as that player; the "<This Player>" token the
## retail AI authors resolves to it too (see _player_team_for_token). Without
## the binding both refuse - honestly, naming what is missing.
##
## THE OBJECT / UNIT-REFERENCE NAMESPACE (WP16's contract). Script object
## names and unit references share ONE namespace, because retail binds
## AI_BASE as a UNIT_REF and then reads it where a plain UNIT is declared.
## This adapter's slice of that namespace has two kinds of entry:
##   * base-flag names - owned by the SIM's unpackable-base table (match
##     configuration; never rebindable);
##   * unit references - bound by actions that carry a UNIT_REF destination
##     (base_unpack, build_base_building), stored as handles RESOLVED AT
##     CALL TIME (structure ids), never as source strings, so a later
##     rebinding of the source cannot silently re-aim an existing reference.
##     References are mutable by design (retail re-points
##     AI_CURRENT_CONSTRUCTION_SITE constantly).
## The reference store LIVES IN THE SIM (script_unit_references, keyed by
## this world's script-player team), NOT in this adapter: a bound reference
## changes what a later script action does, which makes it sim-outcome-
## bearing state that save/load and late-join must reproduce. A peer that
## adopts a snapshot and rebuilds its worlds resolves every reference
## exactly as the peer that minted them. This adapter holds no reference
## state of its own - only configuration (the name bindings above).
## Everything else ("NAMED_..." vocabulary over map-placed units) still has
## no binding: sim entity rows carry display names, not script identities,
## so every other object-name-keyed method refuses. That remains the single
## largest METHOD gap (the object-name-registry subsystem).
##
## DETERMINISM
## ===========
## This world runs inside the lockstep simulation, so every answer and every
## order must be bit-identical on every peer:
##   * Every iteration that could affect an answer walks sim id lists that
##     are explicitly sorted (entity_ids/structure_ids/living_ids), never raw
##     Dictionary order.
##   * The one "nearest" selection (orders.attack target pick) uses an exact
##     total order - strictly-less distance, ties to the LOWEST id - the same
##     discipline as the sim's own AI wave targeting (_spatial_nearest_hostile
##     with prefer_lowest_id). No is_equal_approx anywhere: tolerance
##     comparison is not transitive and cannot define a total order.
##   * Condition-backing (query) methods are strictly read-only: conditions
##     are evaluated an unpredictable number of times, so any mutation there
##     would desync peers. The runner asserts state_hash() stability across
##     repeated query evaluation.
##
## GRANULARITY NOTE
## ================
## A sim entity row is one battalion (retail horde object) - the scripting
## object unit of BFME2. Counting methods (teams.unit_count,
## progression.unit_count_with_upgrade) therefore count battalion rows, which
## matches "objects on the team" in the SAGE sense, NOT individual horde
## members.
##
## KNOWN LIE SURFACE (pre-facet money API)
## =======================================
## SageScriptWorld.player_money(player) -> int has no refusal channel: tranche
## 1 handlers (script_handlers_core.gd) gate only on CAP_PLAYER_MONEY and then
## trust the int. For an UNBOUND player this adapter can only return 0 there.
## The economy() facet override below refuses unbound players properly; the
## legacy path's inability to refuse per player is reported as a base-class
## design defect, not worked around here.

const ParamTypes := preload("res://src/script/script_param_types.gd")

var sim: RetailSliceSim = null

## Script player name -> sim team id. One name per team, one team per name.
var _player_teams: Dictionary = {}
## Sim team id -> script player name (reverse of _player_teams; used by
## teams.owner()).
var _team_players: Dictionary = {}
## Script team name -> sim team id. Several team names may alias one sim team
## (SAGE aliases the default team), so this map is many-to-one.
var _team_names: Dictionary = {}

## The player whose script libraries this world instance executes (see the
## class comment). "" until bind_script_player is called.
var _script_player: String = ""

## science id -> power id, derived once from the sim's GLOBAL spellbook
## document (authored order; deterministic). Rebuilt lazily so a world created
## before configure_spellbook_runtime() still sees the tree.
var _science_powers: Dictionary = {}
var _science_powers_ready := false


func _init(backing_sim: RetailSliceSim = null) -> void:
	sim = backing_sim


# --- Bindings -------------------------------------------------------------


func bind_player(player_name: String, team: int) -> bool:
	## Bind a script player name to a rostered sim team. Rejects empty names,
	## unknown teams, rebinding a name to a different team, and a second name
	## on a team that already has one (owner() needs the mapping 1:1).
	if sim == null or player_name == "" or not sim.team_ids().has(team):
		return false
	if _player_teams.has(player_name):
		return int(_player_teams[player_name]) == team
	if _team_players.has(team):
		return false
	_player_teams[player_name] = team
	_team_players[team] = player_name
	return true


func bind_team(team_name: String, team: int) -> bool:
	## Bind a script team name (a player's DEFAULT team - the whole roster) to
	## a rostered sim team. Aliases are allowed; rebinding to a different team
	## is not.
	if sim == null or team_name == "" or not sim.team_ids().has(team):
		return false
	if _team_names.has(team_name):
		return int(_team_names[team_name]) == team
	_team_names[team_name] = team
	return true


func bind_script_player(player_name: String) -> bool:
	## Declare WHOSE script libraries this world instance executes (see the
	## class comment). The name must already be bound via bind_player, so the
	## script player always resolves to a rostered team; rebinding to a
	## DIFFERENT name is rejected (the executing player is match
	## configuration, not runtime state).
	if sim == null or player_name == "" or not _player_teams.has(player_name):
		return false
	if _script_player != "":
		return _script_player == player_name
	_script_player = player_name
	return true


const THIS_PLAYER_TOKEN := "<This Player>"
## The aggregate player tokens the retail AI libraries author on counting and
## nearest-object members (exact spellings from the decoded corpus). Each
## resolves to a SET of sim teams relative to the bound script player; the
## censuses over them SUM (SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER writes ONE
## counter from an enemies-token census, so the aggregate is a total, not a
## per-player disjunction). The singular "<This Player's Enemy>" is NOT here:
## it names the AI's current-enemy choice, a model the sim does not carry, so
## it refuses rather than guessing which enemy.
const THIS_PLAYERS_ENEMIES_TOKEN := "<This Player's Enemies>"
const THIS_PLAYERS_ALLIES_TOKEN := "<This Player's Allies incl Self>"
const THIS_PLAYERS_ENEMY_TOKEN := "<This Player's Enemy>"
const ALL_PLAYERS_TOKEN := "<All Players>"


func _player_team_for_token(player: String) -> int:
	## Player-argument resolution for the base-building surface, where the
	## retail AI authors the literal "<This Player>" token: the token resolves
	## to the bound script player; anything else is a plain player-name
	## binding. -1 when unresolvable.
	if player == THIS_PLAYER_TOKEN:
		return _bound_player_team(_script_player) if _script_player != "" else -1
	return _bound_player_team(player)


func _script_player_team() -> int:
	## The acting team for AI actions that carry no player argument. -1 when
	## no script player is bound.
	if _script_player == "":
		return -1
	return _bound_player_team(_script_player)


func _census_teams_for_player(player: String) -> Dictionary:
	## Resolve a player argument to the SORTED team set a census aggregates
	## over: a bound name is one team; the aggregate tokens (constants above)
	## resolve relative to the bound script player. Answers {"teams": Array}
	## or {"reason": String}. Read-only - condition paths resolve through
	## here.
	##
	## The enemies set is the hostile ROSTERED combatants plus the creep
	## owner when creep camps are seeded (retail's PlyrCreeps is at war with
	## every player, and creep camp structures carry countable retail type
	## names). The neutral capturable-structure owner is EXCLUDED: retail's
	## PlyrCivilian relation is Neutral, not Enemy. Sets are sorted, so
	## summation order is fixed regardless (integer sums commute).
	if sim == null:
		return {"reason": "no simulation attached"}
	match player:
		THIS_PLAYER_TOKEN:
			var own_team := _script_player_team()
			if own_team < 0:
				return {"reason": "'<This Player>' cannot resolve: no script player is bound (bind_script_player)"}
			return {"teams": [own_team]}
		THIS_PLAYERS_ENEMIES_TOKEN, THIS_PLAYERS_ALLIES_TOKEN:
			var anchor_team := _script_player_team()
			if anchor_team < 0:
				return {
					"reason":
					"'%s' cannot resolve: no script player is bound (bind_script_player)" % player
				}
			var wanted_relation := int(
				(ParamTypes.ENUMS["RELATION"] as Dictionary)[
					"Enemy" if player == THIS_PLAYERS_ENEMIES_TOKEN else "Friend"
				]
			)
			var teams: Array = []
			for team_value in sim.team_ids():
				var team := int(team_value)
				if team == anchor_team:
					if player == THIS_PLAYERS_ALLIES_TOKEN:
						teams.append(team)
					continue
				if _relation_between(anchor_team, team) == wanted_relation:
					teams.append(team)
			if player == THIS_PLAYERS_ENEMIES_TOKEN and sim.creep_lairs_enabled:
				teams.append(RetailSliceSim.CREEP_TEAM)
			teams.sort()
			return {"teams": teams}
		THIS_PLAYERS_ENEMY_TOKEN:
			return {
				"reason":
				"the singular '<This Player's Enemy>' token names the AI's "
				+ "current-enemy choice, a model this simulation does not carry "
				+ "(refusing rather than guessing which enemy)"
			}
	var bound_team := _bound_player_team(player)
	if bound_team < 0:
		return {"reason": "player '%s' is not bound to a simulation team" % player}
	return {"teams": [bound_team]}


func resolve_script_object(name: String) -> Dictionary:
	## The ONE lookup for the shared object / unit-reference namespace (class
	## comment): a bound unit reference answers its resolved handle
	## ({"kind": "structure", "id": int}); a sim base-flag name answers
	## {"kind": "base_flag", "name": String}; {} means unknown. Read-only -
	## conditions resolve through here, so it may not mutate anything.
	## References are read from the SIM's store under this world's script-
	## player team (a world with no script player bound can have bound
	## nothing). The shadowing invariant guarantees a name is never both a
	## reference and a flag, so the lookup order carries no meaning.
	if sim == null:
		return {}
	var team := _script_player_team()
	if team >= 0:
		var reference_id: int = sim.script_unit_reference(team, name)
		if reference_id != 0:
			return {"kind": "structure", "id": reference_id}
	if sim.unpackable_bases.has(name):
		return {"kind": "base_flag", "name": name}
	return {}


func _unit_reference_rejection(reference: String) -> String:
	## "" when `reference` may be bound (or is empty - "bind nothing", which
	## the surface allows although no retail AI call site authors it);
	## otherwise the refusal reason. Base-flag names are owned by the sim's
	## table and may never be shadowed: the namespace is shared, so a
	## reference that eclipsed a flag would silently re-aim every later read
	## of that flag. Callers MUST clear this check BEFORE mutating the sim,
	## so a rejected binding never leaves a half-applied action behind.
	if reference == "" or sim == null or not sim.unpackable_bases.has(reference):
		return ""
	return (
		"'%s' names a base flag; flag names are owned by the simulation's "
		+ "unpackable-base table and cannot be rebound as unit references"
	) % reference


func _bind_unit_reference(reference: String, structure_id: int) -> void:
	## Bind (or re-point) a unit reference to a concrete structure - resolved
	## NOW, stored as a handle, never as a source string (class comment). An
	## empty reference binds nothing. Only call after _unit_reference_rejection
	## answered "". The handle is written into the SIM's authoritative store
	## under this world's script-player team; both callers resolved that team
	## before mutating the sim, so it is never -1 here.
	if reference == "":
		return
	sim.bind_script_unit_reference(_script_player_team(), reference, structure_id)


func _bound_player_team(player: String) -> int:
	## -1 when the name is unbound or the sim is absent.
	if sim == null or not _player_teams.has(player):
		return -1
	return int(_player_teams[player])


func _bound_team(team_name: String) -> int:
	if sim == null or not _team_names.has(team_name):
		return -1
	return int(_team_names[team_name])


# --- Shared read-only helpers ---------------------------------------------


func _relation_between(team_a: int, team_b: int) -> int:
	## ParamTypes RELATION int for two ROSTERED teams, mirroring the sim's
	## alliance rule (retail_slice_sim._is_hostile for rostered combatants):
	## same team or shared non-null alliance id -> Friend, everything else ->
	## Enemy (rostered free-for-all default). Neutral is unreachable for bound
	## players because only rostered combatant teams can be bound.
	var relation: Dictionary = ParamTypes.ENUMS["RELATION"]
	if team_a == team_b:
		return int(relation["Friend"])
	var alliance_a: Variant = sim.team_alliance(team_a)
	var alliance_b: Variant = sim.team_alliance(team_b)
	if alliance_a != null and alliance_b != null and alliance_a == alliance_b:
		return int(relation["Friend"])
	return int(relation["Enemy"])


func _queued_command_points(team: int) -> int:
	## Command points reserved by production queues. Read-only mirror of the
	## sim's private _queued_command_points_for_team (FINDING: the sim should
	## expose this publicly). Iterates sorted structure ids; summation is
	## order-independent regardless.
	var total := 0
	for structure_id in sim.structure_ids(team):
		for item_value in Array((sim.structures[structure_id] as Dictionary).get("queue", [])):
			if typeof(item_value) == TYPE_DICTIONARY:
				total += int((item_value as Dictionary).get("command_points", 0))
	return total


func _team_is_defeated(team: int) -> bool:
	## Read-only mirror of retail_slice_sim._team_defeated (FINDING: the sim
	## should expose this publicly so this cannot drift): base-loop matches
	## eliminate a team when its fortress is razed; non-base matches when no
	## living battalion remains.
	if sim.base_loop_enabled:
		var fortress: int = sim.fortress_id(team)
		return fortress != 0 and int((sim.structures[fortress] as Dictionary).get("health", 0)) <= 0
	return sim.living_ids(team).is_empty()


func _science_power_map() -> Dictionary:
	## science id -> power id from the GLOBAL spellbook document, in authored
	## power order (spellbook_power_ids() is the doc's deterministic order).
	if not _science_powers_ready:
		_science_powers.clear()
		if sim.spellbook_available():
			for power_id in sim.spellbook_power_ids():
				var science_id := String(sim.spellbook_power(String(power_id)).get("science_id", ""))
				if science_id != "" and not _science_powers.has(science_id):
					_science_powers[science_id] = String(power_id)
			_science_powers_ready = true
	return _science_powers


func _known_upgrade_id(team: int, upgrade: String) -> bool:
	## Whether the sim models this upgrade id at all: a research contract key,
	## or an equipment id a contract's completion applies (the research id
	## itself, plus the recorded fire-arrows two-button mapping).
	if sim.structure_upgrade_contracts_for_team(team).has(upgrade):
		return true
	for equipment_value in RetailSliceSim.FORGE_UPGRADE_EQUIPMENT.values():
		if Array(equipment_value).has(upgrade):
			return true
	return false


static func _sim_point(position: Vector3) -> Vector2:
	## Script positions are Vector3 world space; the sim is 2D. The
	## presentation maps sim (x, y) -> world (x, height, z) everywhere
	## (retail_vertical_slice.gd), so the inverse is (x, z).
	return Vector2(position.x, position.z)


func _resolve_base_structure(base: String) -> Dictionary:
	## Resolve a base argument through the shared object namespace down to a
	## sim structure. Read-only. Answers exactly one of:
	##   {"id": int}       - a concrete base structure (a bound reference, or
	##                       a base flag that has been unpacked - the flag name
	##                       keeps resolving to the base it became);
	##   {"packed": true}  - a base flag no one has unpacked: there IS no base
	##                       object there yet (conditions answer false,
	##                       commands refuse);
	##   {}                - a name outside the namespace entirely.
	var handle := resolve_script_object(base)
	if handle.is_empty():
		return {}
	if String(handle.get("kind", "")) == "base_flag":
		var row: Dictionary = sim.unpackable_base_state(String(handle.get("name", "")))
		if int(row.get("unpacked_by", -1)) < 0:
			return {"packed": true}
		return {"id": int(row.get("structure_id", 0))}
	return {"id": int(handle.get("id", 0))}


# --- Base-world surface ---------------------------------------------------


func supports(capability: String) -> bool:
	## CAP_RANDOM is deliberately absent: the sim carries no deterministic
	## random stream, and reaching for randi() would break lockstep. Refusing
	## the capability is the honest answer until the sim owns a seeded stream.
	if sim == null:
		return false
	return capability in [CAP_PLAYER_MONEY, CAP_DEBUG_OUTPUT]


func world_frame() -> int:
	return sim.tick_index if sim != null else 0


func player_money(player: String) -> int:
	## Pre-facet API without a refusal channel - see the class comment. Bound
	## players answer real team resources; unbound players can only get 0
	## here. economy() below is the honest surface.
	var team := _bound_player_team(player)
	if team < 0:
		return 0
	return sim.resources_for_team(team)


func set_player_money(player: String, amount: int) -> bool:
	var team := _bound_player_team(player)
	if team < 0:
		return false
	sim.team_resources[team] = amount
	return true


func debug_message(channel: String, text: String) -> bool:
	## Deterministic side-effect-free sink for DEBUG_STRING and friends: the
	## text reaches the log and no simulation state moves.
	print("[script-debug %s] %s" % [channel, text])
	return true


# --- Facet factories ------------------------------------------------------


func _make_players() -> Players:
	return SlicePlayers.new()


func _make_teams() -> Teams:
	return SliceTeams.new()


func _make_orders() -> Orders:
	return SliceOrders.new()


func _make_combat() -> Combat:
	return SliceCombat.new()


func _make_progression() -> Progression:
	return SliceProgression.new()


func _make_economy() -> Economy:
	return SliceEconomy.new()


func _make_meta() -> Meta:
	return SliceMeta.new()


func _make_units() -> Units:
	return SliceUnits.new()


func _make_ai() -> Ai:
	return SliceAi.new()


# ==========================================================================
# PLAYERS
# ==========================================================================


class SlicePlayers:
	extends SageScriptWorld.Players

	## Implemented: exists, faction, the three command-point reads,
	## building_count, relation_to, can_build_at_base (the base-anchored
	## buildability read, against the CORRECTED signature that carries the
	## base - WP17's reported defect, since fixed), and object_count_of_types
	## (the retail AI's single heaviest blocked read, served through the
	## sim's object-type identity census). Everything else refuses.

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func _team_or_refuse(method: String, player: String) -> Dictionary:
		var w := _world()
		if w == null or w.sim == null:
			return {"query": _refuse_query(method, "no simulation attached")}
		var team := w._bound_player_team(player)
		if team < 0:
			return {"query": _refuse_query(method, "player '%s' is not bound to a simulation team" % player)}
		return {"team": team}

	func exists(player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("players.exists", "no simulation attached")
		# Bindings are declared exhaustive (class comment), so an unbound name
		# IS absent from the match - false is an answer here, not a dodge.
		return SageWorldQuery.hit(w._bound_player_team(player) >= 0)

	func faction(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.faction", player)
		if resolved.has("query"):
			return resolved["query"]
		var descriptor: Dictionary = _world().sim.team_descriptor(int(resolved["team"]))
		var faction_name := String(descriptor.get("faction", ""))
		if faction_name == "":
			return _refuse_query(
				"players.faction",
				"team descriptor for player '%s' carries no faction" % player
			)
		return SageWorldQuery.hit(faction_name)

	func command_points_available(player: String) -> SageWorldQuery:
		## cap - committed - queue-reserved: exactly the headroom the sim's own
		## queue_unit admission rule computes, not an invented notion.
		var resolved := _team_or_refuse("players.command_points_available", player)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var team := int(resolved["team"])
		return SageWorldQuery.hit(
			w.sim.command_point_cap
			- w.sim.command_points_for_team(team)
			- w._queued_command_points(team)
		)

	func command_points_total(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.command_points_total", player)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(_world().sim.command_point_cap)

	func command_points_used(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.command_points_used", player)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(_world().sim.command_points_for_team(int(resolved["team"])))

	func building_count(player: String, building_class: String) -> SageWorldQuery:
		## Empty class counts every living structure. A non-empty class is
		## served only when it names a structure kind the sim models for this
		## team; retail building-class tokens outside that vocabulary refuse
		## rather than counting zero of something the sim cannot see.
		var resolved := _team_or_refuse("players.building_count", player)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var team := int(resolved["team"])
		if building_class == "":
			return SageWorldQuery.hit(w.sim.living_structure_ids(team).size())
		var known: Array = w.sim.structure_kinds_for_team(team).duplicate()
		for kind_value in w.sim.seed_structure_kinds_for_team(team):
			if not known.has(kind_value):
				known.append(kind_value)
		if not known.has(building_class):
			return _refuse_query(
				"players.building_count",
				"building class '%s' is not a structure kind this simulation models" % building_class
			)
		var count := 0
		for structure_id in w.sim.living_structure_ids(team):
			if String((w.sim.structures[structure_id] as Dictionary).get("structure_kind", "")) == building_class:
				count += 1
		return SageWorldQuery.hit(count)

	func relation_to(player: String, other: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.relation_to", player)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var other_team := w._bound_player_team(other)
		if other_team < 0:
			return _refuse_query(
				"players.relation_to",
				"player '%s' is not bound to a simulation team" % other
			)
		return SageWorldQuery.hit(w._relation_between(int(resolved["team"]), other_team))

	func can_build_at_base(player: String, base: String, object_type: String) -> SageWorldQuery:
		## CAN_BUILD_AT_BASE (empty object_type - "anything at all") /
		## CAN_BUILD_OBJECTTYPE_AT_BASE. Retail authors "<This Player>" here,
		## so the player resolves through the script-player token rule.
		##
		## STRICTLY READ-ONLY: this backs conditions the AI economy loop polls
		## repeatedly.
		##
		## The answer is pad availability: true iff the resolved base is a
		## living, completed base structure of THIS player's team with a free
		## expansion pad that accepts the asked-for kind (any configured kind
		## for the empty variant). Money is deliberately not consulted -
		## affordability is the build ACTION's concern. A truthful FALSE
		## covers: a base flag no one has unpacked (no base object exists
		## there yet), someone else's base, a razed or vanished base, and no
		## matching free pad. A REFUSAL covers what the sim cannot see at
		## all: an unresolvable player, a name outside the object namespace,
		## and an object type outside the expansion rules (false there would
		## be a guess about a building the sim does not model).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("players.can_build_at_base", "no simulation attached")
		var team := w._player_team_for_token(player)
		if team < 0:
			return _refuse_query(
				"players.can_build_at_base",
				"player '%s' is not bound to a simulation team" % player
				if player != RetailSliceScriptWorld.THIS_PLAYER_TOKEN
				else "'<This Player>' cannot resolve: no script player is bound (bind_script_player)"
			)
		var wanted_kind := ""
		if object_type != "":
			wanted_kind = w.sim.expansion_kind_for_object_id(object_type)
			if wanted_kind == "":
				return _refuse_query(
					"players.can_build_at_base",
					"object type '%s' is not an expansion the simulation models (false would be a guess)" % object_type
				)
		var resolved_base := w._resolve_base_structure(base)
		if resolved_base.is_empty():
			return _refuse_query(
				"players.can_build_at_base",
				"base '%s' is not a bound unit reference or base flag" % base
			)
		if resolved_base.has("packed"):
			# No base object exists at the flag yet - a truthful "no".
			return SageWorldQuery.hit(false)
		var structure_id := int(resolved_base.get("id", 0))
		var row: Dictionary = w.sim.structures.get(structure_id, {})
		if (
			row.is_empty()
			or int(row.get("team", -1)) != team
			or int(row.get("health", 0)) <= 0
			or float(row.get("construction_progress", 0.0)) < 1.0
		):
			return SageWorldQuery.hit(false)
		var free_kinds: Array = w.sim.expansion_commands_for(structure_id)
		if object_type == "":
			return SageWorldQuery.hit(not free_kinds.is_empty())
		return SageWorldQuery.hit(free_kinds.has(wanted_kind))

	func object_count_of_types(
		player: String, object_type_list: String, include_dead: bool
	) -> SageWorldQuery:
		## PLAYER_HAS_OBJECT_COMPARISON (124 retail-AI call sites together with
		## WP01's SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER pair ride on this one
		## method) - so STRICTLY READ-ONLY: it is a condition path evaluated an
		## unpredictable number of times.
		##
		## `object_type_list` resolves list-first with a single-type fallback
		## (sim.resolve_object_type_names - the retail engine's own rule; the
		## corpus authors both spellings). The player argument accepts bound
		## names and the aggregate tokens (_census_teams_for_player), and an
		## aggregate is a SUM across the resolved teams - the counter-writing
		## action proves retail's aggregate is a single total. The count
		## itself is the sim's exact census over recorded row identity; a name
		## the simulation cannot field counts a TRUE zero (no instance can
		## exist in this match), which is also retail's answer for a list
		## nobody has built yet. An empty list name refuses: "" names nothing
		## in the retail vocabulary.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("players.object_count_of_types", "no simulation attached")
		if object_type_list == "":
			return _refuse_query(
				"players.object_count_of_types",
				"empty OBJECT_TYPE_LIST name (neither a list nor a type)"
			)
		var resolved := w._census_teams_for_player(player)
		if resolved.has("reason"):
			return _refuse_query("players.object_count_of_types", String(resolved["reason"]))
		var names: Array = w.sim.resolve_object_type_names(object_type_list)
		var total := 0
		for team_value in Array(resolved["teams"]):
			total += w.sim.count_objects_of_types(int(team_value), names, include_dead)
		return SageWorldQuery.hit(total)


# ==========================================================================
# TEAMS
# ==========================================================================


class SliceTeams:
	extends SageScriptWorld.Teams

	## Implemented: exists, unit_count, owner, stop (non-disband). A bound
	## script team is a player's whole roster; sub-player teams, team state
	## machines, discovery, threat and recruitment all refuse.

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func _team_or_refuse(method: String, team_name: String) -> Dictionary:
		var w := _world()
		if w == null or w.sim == null:
			return {"query": _refuse_query(method, "no simulation attached")}
		var team := w._bound_team(team_name)
		if team < 0:
			return {"query": _refuse_query(method, "team '%s' is not bound to a simulation team" % team_name)}
		return {"team": team}

	func exists(team: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("teams.exists", "no simulation attached")
		return SageWorldQuery.hit(w._bound_team(team) >= 0)

	func unit_count(team: String) -> SageWorldQuery:
		## Living battalion rows (retail horde objects) - see the granularity
		## note in the class comment.
		var resolved := _team_or_refuse("teams.unit_count", team)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(_world().sim.living_ids(int(resolved["team"])).size())

	func owner(team: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("teams.owner", team)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var sim_team := int(resolved["team"])
		if not w._team_players.has(sim_team):
			return _refuse_query(
				"teams.owner", "no player name is bound to simulation team %d" % sim_team
			)
		return SageWorldQuery.hit(String(w._team_players[sim_team]))

	func stop(team: String, disband: bool) -> bool:
		if disband:
			return _refuse_command(
				"teams.stop", "the simulation has no disband model (TEAM_STOP_AND_DISBAND)"
			)
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.stop", "no simulation attached")
		var sim_team := w._bound_team(team)
		if sim_team < 0:
			return _refuse_command(
				"teams.stop", "team '%s' is not bound to a simulation team" % team
			)
		if w.sim.winner != -1:
			return _refuse_command("teams.stop", "the match is already resolved")
		# Delivered to every commandable member; an empty roster is a vacuous
		# success, exactly like the retail action on an empty team.
		w.sim.issue_stop(w.sim.living_ids(sim_team), sim_team)
		return true


# ==========================================================================
# ORDERS
# ==========================================================================


class SliceOrders:
	extends SageScriptWorld.Orders

	## Implemented: move_to (POSITION and NEAREST_TYPE targets),
	## attack_move_to (POSITION targets), attack (TEAM targets) and
	## stand_ground, for TEAM and PLAYER scopes. UNIT scope needs the missing
	## object-name binding; waypoint/area/named-object targets need map
	## geometry the sim does not model.
	##
	## stand_ground maps to the sim's own retail-sourced "HoldGround" stance
	## (issue_set_stance) - BFME2's stance system IS its stand-ground
	## mechanism; the choice is documented in the packet report.

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func _scope_team(scope: int, name: String) -> Dictionary:
		var w := _world()
		if w == null or w.sim == null:
			return {"reason": "no simulation attached"}
		var team := -1
		match scope:
			SageScriptWorld.Scope.TEAM:
				team = w._bound_team(name)
				if team < 0:
					return {"reason": "team '%s' is not bound to a simulation team" % name}
			SageScriptWorld.Scope.PLAYER:
				team = w._bound_player_team(name)
				if team < 0:
					return {"reason": "player '%s' is not bound to a simulation team" % name}
			_:
				return {
					"reason":
					"%s scope needs a script-object name binding the simulation does not model"
					% SageScriptWorld.scope_label(scope)
				}
		if w.sim.winner != -1:
			return {"reason": "the match is already resolved"}
		return {"team": team}

	func move_to(scope: int, name: String, target: Dictionary) -> bool:
		var resolved := _scope_team(scope, name)
		if resolved.has("reason"):
			return _refuse_command("orders.move_to", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		match int(target.get("kind", -1)):
			SageScriptWorld.TargetKind.POSITION:
				w.sim.issue_move(
					w.sim.living_ids(team),
					RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO)),
					"order.move",
					team
				)
				return true
			SageScriptWorld.TargetKind.NEAREST_TYPE:
				return _move_to_nearest_type(team, target)
		return _refuse_command(
			"orders.move_to",
			"only POSITION and NEAREST_TYPE targets are answerable (no waypoint/area/object geometry)"
		)

	func _move_to_nearest_type(team: int, target: Dictionary) -> bool:
		## TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE[_OWNED_BY_PLAYER]: move the
		## whole roster to the nearest living object matching an
		## OBJECT_TYPE_LIST argument (list-first, single-type fallback).
		##
		## The empty owner is target_nearest_type's documented "any owner"
		## sentinel; "<All Players>" (authored once in the corpus) is the same
		## set spelled explicitly. Any other owner resolves through the census
		## token rule, so "<This Player's Enemies>"-style aggregates search
		## every resolved team.
		##
		## THE SEARCH ANCHOR is the position of the moving roster's
		## lowest-living-id battalion, and the winner is the sim's exact total
		## order (nearest_object_of_types: distance, kind, lowest id) - fully
		## deterministic, no is_equal_approx.
		##
		## HONESTY SPLIT, deliberate: a type list naming NOTHING this
		## simulation can ever field refuses (the retail AI's authored targets
		## are map-placed tactical markers - Center1, CombatArea01 - which no
		## sim subsystem models yet; a silent no-op would bury that gap).
		## A FIELDABLE type with zero living instances right now is a truthful
		## retail no-op: the retail action moves nobody when nothing matches.
		var w := _world()
		var type_names: Array = w.sim.resolve_object_type_names(String(target.get("name", "")))
		var owner := String(target.get("owner", ""))
		var owner_teams: Array = []
		if owner != "" and owner != RetailSliceScriptWorld.ALL_PLAYERS_TOKEN:
			var resolved_owner := w._census_teams_for_player(owner)
			if resolved_owner.has("reason"):
				return _refuse_command("orders.move_to", String(resolved_owner["reason"]))
			owner_teams = resolved_owner["teams"]
		var any_fieldable := false
		for name_value in type_names:
			if w.sim.fieldable_object_type(String(name_value)):
				any_fieldable = true
				break
		if not any_fieldable:
			return _refuse_command(
				"orders.move_to",
				(
					"no type named by '%s' is an object this simulation can field "
					+ "(retail's nearest-of-type moves target map-placed marker "
					+ "objects, which no sim subsystem models)"
				) % String(target.get("name", ""))
			)
		var movers := w.sim.living_ids(team)
		if movers.is_empty():
			# Nothing left to command: vacuous delivery, like the retail
			# action on an emptied team.
			return true
		var origin := Vector2((w.sim.entities[movers[0]] as Dictionary).get("position", Vector2.ZERO))
		var nearest: Dictionary = w.sim.nearest_object_of_types(origin, type_names, owner_teams)
		if not bool(nearest.get("found", false)):
			# A fieldable type with no living instance: retail moves nobody.
			return true
		w.sim.issue_move(movers, Vector2(nearest.get("position", Vector2.ZERO)), "order.move", team)
		return true

	func attack_move_to(scope: int, name: String, target: Dictionary) -> bool:
		var resolved := _scope_team(scope, name)
		if resolved.has("reason"):
			return _refuse_command("orders.attack_move_to", String(resolved["reason"]))
		if int(target.get("kind", -1)) != SageScriptWorld.TargetKind.POSITION:
			return _refuse_command(
				"orders.attack_move_to",
				"only explicit POSITION targets are answerable (no waypoint/area/object geometry)"
			)
		var w := _world()
		var team := int(resolved["team"])
		w.sim.issue_attack_move(
			w.sim.living_ids(team),
			RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO)),
			team
		)
		return true

	func attack(scope: int, name: String, target: Dictionary) -> bool:
		var resolved := _scope_team(scope, name)
		if resolved.has("reason"):
			return _refuse_command("orders.attack", String(resolved["reason"]))
		if int(target.get("kind", -1)) != SageScriptWorld.TargetKind.TEAM:
			return _refuse_command(
				"orders.attack",
				"only TEAM targets are answerable (named single objects have no binding)"
			)
		var w := _world()
		var team := int(resolved["team"])
		var target_team := w._bound_team(String(target.get("name", "")))
		if target_team < 0:
			return _refuse_command(
				"orders.attack",
				"target team '%s' is not bound to a simulation team" % String(target.get("name", ""))
			)
		var attacker_ids := w.sim.living_ids(team)
		if attacker_ids.is_empty():
			# Nothing left to command: vacuous delivery, like the retail action
			# on an emptied team.
			return true
		var candidates := w.sim.living_ids(target_team)
		if candidates.is_empty():
			return _refuse_command(
				"orders.attack", "target team has no living battalion to designate"
			)
		# Deterministic target pick: nearest living member of the target team
		# to the LOWEST-id attacker, under the exact total order
		# (distance ascending, then id ascending). The tie-break is written
		# out explicitly so the winner is the minimum under that order no
		# matter how the candidates are visited - the same discipline as the
		# sim's own AI wave targeting (_spatial_nearest_hostile with
		# prefer_lowest_id). Never is_equal_approx: a tolerance comparison is
		# not transitive and cannot define a total order.
		var origin := Vector2((w.sim.entities[attacker_ids[0]] as Dictionary).get("position", Vector2.ZERO))
		var best_id := 0
		var best_distance := 0.0
		for candidate in candidates:
			var distance := origin.distance_squared_to(
				Vector2((w.sim.entities[candidate] as Dictionary).get("position", Vector2.ZERO))
			)
			var wins := best_id == 0 or distance < best_distance
			if not wins and distance == best_distance:
				wins = candidate < best_id
			if wins:
				best_id = candidate
				best_distance = distance
		w.sim.issue_attack(attacker_ids, best_id, team)
		return true

	func stand_ground(scope: int, name: String) -> bool:
		var resolved := _scope_team(scope, name)
		if resolved.has("reason"):
			return _refuse_command("orders.stand_ground", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		w.sim.issue_set_stance(w.sim.living_ids(team), "HoldGround", team)
		return true


# ==========================================================================
# COMBAT
# ==========================================================================


class SliceCombat:
	extends SageScriptWorld.Combat

	## Implemented: fire_special_power / special_power_ready (player-scope
	## spellbook powers at POSITION targets) and player_all_destroyed (full
	## variant). Damage/kill need a public sim damage API (finding).

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func _player_scope_team(scope: int, name: String) -> Dictionary:
		var w := _world()
		if w == null or w.sim == null:
			return {"reason": "no simulation attached"}
		if scope != SageScriptWorld.Scope.PLAYER:
			return {
				"reason":
				"the simulation models special powers per player only (%s scope refused)"
				% SageScriptWorld.scope_label(scope)
			}
		var team := w._bound_player_team(name)
		if team < 0:
			return {"reason": "player '%s' is not bound to a simulation team" % name}
		return {"team": team}

	func fire_special_power(
		scope: int, name: String, power: String, target: Dictionary
	) -> bool:
		var resolved := _player_scope_team(scope, name)
		if resolved.has("reason"):
			return _refuse_command("combat.fire_special_power", String(resolved["reason"]))
		if int(target.get("kind", -1)) != SageScriptWorld.TargetKind.POSITION:
			return _refuse_command(
				"combat.fire_special_power",
				"only explicit POSITION targets are answerable (no waypoint/object geometry)"
			)
		var w := _world()
		var result: Dictionary = w.sim.cast_power(
			int(resolved["team"]),
			power,
			RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO))
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"combat.fire_special_power",
				"simulation rejected cast of '%s': %s" % [power, String(result.get("reason", ""))]
			)
		return true

	func special_power_ready(scope: int, name: String, power: String) -> SageWorldQuery:
		var resolved := _player_scope_team(scope, name)
		if resolved.has("reason"):
			return _refuse_query("combat.special_power_ready", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		var cooldown: Dictionary = w.sim.power_cooldown_state(team, power)
		if cooldown.is_empty():
			return _refuse_query(
				"combat.special_power_ready",
				"power '%s' is not in this match's spellbook document" % power
			)
		return SageWorldQuery.hit(
			w.sim.has_power(team, power) and int(cooldown.get("remaining_ticks", 0)) == 0
		)

	func player_all_destroyed(player: String, build_facilities_only: bool) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("combat.player_all_destroyed", "no simulation attached")
		if build_facilities_only:
			return _refuse_query(
				"combat.player_all_destroyed",
				"no sourced build-facility classification exists for slice structures"
			)
		var team := w._bound_player_team(player)
		if team < 0:
			return _refuse_query(
				"combat.player_all_destroyed",
				"player '%s' is not bound to a simulation team" % player
			)
		return SageWorldQuery.hit(
			w.sim.living_ids(team).is_empty() and w.sim.living_structure_ids(team).is_empty()
		)


# ==========================================================================
# PROGRESSION
# ==========================================================================


class SliceProgression:
	extends SageScriptWorld.Progression

	## Implemented: player-scope upgrade reads (has_upgrade,
	## unit_count_with_upgrade), build_upgrade (queues real research), the
	## science surface backed by the sim's spellbook (has_science,
	## can_purchase_science, purchase_science, science_purchase_points), and
	## any_hero_reached_rank from authored experience chains. Unit/team
	## experience mutation needs the missing object-name binding plus a public
	## award API.

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func _player_team(player: String) -> Dictionary:
		var w := _world()
		if w == null or w.sim == null:
			return {"reason": "no simulation attached"}
		var team := w._bound_player_team(player)
		if team < 0:
			return {"reason": "player '%s' is not bound to a simulation team" % player}
		return {"team": team}

	func _science_gate(team: int) -> String:
		## Reasons the science surface cannot answer for this team; "" = clear.
		var w := _world()
		if not w.sim.spellbook_available():
			return "no spellbook document is configured (%s)" % w.sim.spellbook_error()
		if w.sim.team_has_spellbook_override(team):
			return "team %d runs a per-team spellbook override this adapter cannot read publicly" % team
		return ""

	func has_upgrade(scope: int, name: String, upgrade: String) -> SageWorldQuery:
		if scope != SageScriptWorld.Scope.PLAYER:
			return _refuse_query(
				"progression.has_upgrade",
				"%s scope needs the missing object-name binding" % SageScriptWorld.scope_label(scope)
			)
		var resolved := _player_team(name)
		if resolved.has("reason"):
			return _refuse_query("progression.has_upgrade", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		var owned := (w.sim.team_upgrades.get(team, {}) as Dictionary).has(upgrade)
		if owned:
			return SageWorldQuery.hit(true)
		if not w._known_upgrade_id(team, upgrade):
			return _refuse_query(
				"progression.has_upgrade",
				"upgrade '%s' is not modeled by this simulation (false would be a guess)" % upgrade
			)
		return SageWorldQuery.hit(false)

	func unit_count_with_upgrade(player: String, upgrade: String) -> SageWorldQuery:
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_query("progression.unit_count_with_upgrade", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		var count := 0
		for id in w.sim.living_ids(team):
			if ((w.sim.entities[id] as Dictionary).get("applied_upgrades", {}) as Dictionary).has(upgrade):
				count += 1
		if count == 0 and not w._known_upgrade_id(team, upgrade):
			return _refuse_query(
				"progression.unit_count_with_upgrade",
				"upgrade '%s' is not modeled by this simulation (zero would be a guess)" % upgrade
			)
		return SageWorldQuery.hit(count)

	func build_upgrade(player: String, upgrade: String) -> bool:
		## AI_PLAYER_BUILD_UPGRADE: queue the research, never grant it. The
		## producing structure is picked deterministically: the lowest living
		## structure id of the contract's kind that accepts the queue.
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_command("progression.build_upgrade", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		var contract: Dictionary = w.sim.structure_upgrade_contracts_for_team(team).get(upgrade, {})
		if contract.is_empty():
			return _refuse_command(
				"progression.build_upgrade",
				"upgrade '%s' has no research contract in this simulation" % upgrade
			)
		var wanted_kind := String(contract.get("structure_kind", ""))
		var last_reason := "no living structure of kind '%s'" % wanted_kind
		for structure_id in w.sim.living_structure_ids(team):
			if String((w.sim.structures[structure_id] as Dictionary).get("structure_kind", "")) != wanted_kind:
				continue
			var result: Dictionary = w.sim.queue_structure_upgrade(team, structure_id, upgrade)
			if bool(result.get("ok", false)):
				return true
			last_reason = String(result.get("reason", ""))
		return _refuse_command(
			"progression.build_upgrade",
			"simulation rejected queueing '%s': %s" % [upgrade, last_reason]
		)

	func has_science(player: String, science: String) -> SageWorldQuery:
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_query("progression.has_science", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		var gate := _science_gate(team)
		if gate != "":
			return _refuse_query("progression.has_science", gate)
		if (w.sim.owned_sciences(team) as Array).has(science):
			return SageWorldQuery.hit(true)
		if w._science_power_map().has(science):
			return SageWorldQuery.hit(false)
		return _refuse_query(
			"progression.has_science",
			"science '%s' is not in this match's spellbook tree (false would be a guess)" % science
		)

	func can_purchase_science(player: String, science: String) -> SageWorldQuery:
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_query("progression.can_purchase_science", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		var gate := _science_gate(team)
		if gate != "":
			return _refuse_query("progression.can_purchase_science", gate)
		if not w._science_power_map().has(science):
			return _refuse_query(
				"progression.can_purchase_science",
				"science '%s' is not in this match's spellbook tree" % science
			)
		var verdict: Dictionary = w.sim.can_purchase_power(
			team, String(w._science_power_map()[science])
		)
		return SageWorldQuery.hit(bool(verdict.get("ok", false)))

	func purchase_science(player: String, science: String) -> bool:
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_command("progression.purchase_science", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		var gate := _science_gate(team)
		if gate != "":
			return _refuse_command("progression.purchase_science", gate)
		if not w._science_power_map().has(science):
			return _refuse_command(
				"progression.purchase_science",
				"science '%s' is not in this match's spellbook tree" % science
			)
		var result: Dictionary = w.sim.purchase_power(
			team, String(w._science_power_map()[science])
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"progression.purchase_science",
				"simulation rejected purchase of '%s': %s" % [science, String(result.get("reason", ""))]
			)
		return true

	func science_purchase_points(player: String) -> SageWorldQuery:
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_query("progression.science_purchase_points", String(resolved["reason"]))
		return SageWorldQuery.hit(_world().sim.power_points(int(resolved["team"])))

	func any_hero_reached_rank(player: String, rank: int) -> SageWorldQuery:
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_query("progression.any_hero_reached_rank", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		var unauthored_hero := false
		for id in w.sim.living_ids(team):
			var row: Dictionary = w.sim.entities[id]
			if String(row.get("category", "")) != "hero":
				continue
			var state: Dictionary = w.sim.experience_state(id)
			if state.is_empty():
				# A hero without an authored experience chain has no rank the
				# sim can vouch for; remember it and refuse below unless some
				# authored hero already answers true.
				unauthored_hero = true
				continue
			if int(state.get("level", 1)) >= rank:
				return SageWorldQuery.hit(true)
		if unauthored_hero:
			return _refuse_query(
				"progression.any_hero_reached_rank",
				"a living hero carries no authored experience chain, so its rank is unknown"
			)
		return SageWorldQuery.hit(false)


# ==========================================================================
# ECONOMY
# ==========================================================================


class SliceEconomy:
	extends SageScriptWorld.Economy

	## Full money surface for BOUND players, with per-player refusal (the
	## pre-facet base API cannot refuse - see the class comment).

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func money(player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("economy.money", "no simulation attached")
		var team := w._bound_player_team(player)
		if team < 0:
			return _refuse_query(
				"economy.money", "player '%s' is not bound to a simulation team" % player
			)
		return SageWorldQuery.hit(w.sim.resources_for_team(team))

	func set_money(player: String, amount: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("economy.set_money", "no simulation attached")
		if w._bound_player_team(player) < 0:
			return _refuse_command(
				"economy.set_money", "player '%s' is not bound to a simulation team" % player
			)
		return w.set_player_money(player, amount)

	func give_money(player: String, amount: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("economy.give_money", "no simulation attached")
		var team := w._bound_player_team(player)
		if team < 0:
			return _refuse_command(
				"economy.give_money", "player '%s' is not bound to a simulation team" % player
			)
		return w.set_player_money(player, w.sim.resources_for_team(team) + amount)


# ==========================================================================
# META
# ==========================================================================


class SliceMeta:
	extends SageScriptWorld.Meta

	## Implemented: player_count, multiplayer_outcome and object_list_change
	## (the sim-owned OBJECT_TYPE_LIST stores). set_time_frozen is
	## deliberately refused even though the sim has clock_paused: freezing it
	## also freezes whatever drives the script layer, so a scripted UNFREEZE
	## could never run - a deadlock, which is worse than a refusal (finding).

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func object_list_change(list_name: String, object_type: String, add: bool) -> bool:
		## OBJECTLIST_ADDOBJECTTYPE (add) / OBJECTLIST_REMOVEOBJECTTYPE. The
		## store is SIM-owned match state (global namespace, like retail's
		## ScriptEngine table - see the sim's block comment), so the mutation
		## rides the snapshot/hash boundary. Set semantics: duplicate adds
		## and absent removes are retail no-ops that still succeed. Type-list
		## names are a separate retail namespace from object names and base
		## flags (resolve_script_object never consults them), so no shadowing
		## rule applies here.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("meta.object_list_change", "no simulation attached")
		var result: Dictionary = w.sim.change_object_type_list(list_name, object_type, add)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"meta.object_list_change",
				"the simulation rejected the list edit: %s" % String(result.get("reason", ""))
			)
		return true

	func player_count(_include_observers: bool) -> SageWorldQuery:
		## Every rostered team is a player; the sim models no observers, so
		## both variants coincide.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("meta.player_count", "no simulation attached")
		return SageWorldQuery.hit(w.sim.team_ids().size())

	func multiplayer_outcome(player: String, outcome: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("meta.multiplayer_outcome", "no simulation attached")
		var team := w._bound_player_team(player)
		if team < 0:
			return _refuse_query(
				"meta.multiplayer_outcome",
				"player '%s' is not bound to a simulation team" % player
			)
		var relation: Dictionary = RetailSliceScriptWorld.ParamTypes.ENUMS["RELATION"]
		match outcome:
			"defeat":
				return SageWorldQuery.hit(w._team_is_defeated(team))
			"allied_victory":
				return SageWorldQuery.hit(
					w.sim.winner != -1
					and w._relation_between(team, w.sim.winner) == int(relation["Friend"])
				)
			"allied_defeat":
				return SageWorldQuery.hit(
					w.sim.winner != -1
					and w._relation_between(team, w.sim.winner) == int(relation["Enemy"])
				)
		return _refuse_query(
			"meta.multiplayer_outcome", "unknown outcome token '%s'" % outcome
		)


# ==========================================================================
# UNITS
# ==========================================================================


class SliceUnits:
	extends SageScriptWorld.Units

	## Implemented: has_command_points_to_build. Everything else on the facet
	## keeps the base refusal - most of it needs the object-name registry.
	## unowned_faction_unit_exists stays REFUSED deliberately even though the
	## sim now owns neutral (capturable) and creep teams: no source pins
	## whether retail's "unowned faction unit" means neutral-owned UNITS only
	## or includes capturable structures, and the two readings diverge exactly
	## when the sim's neutral structures exist - so an answer would be a
	## guess about which question is being asked.

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func has_command_points_to_build(player: String, object_type: String) -> SageWorldQuery:
		## HAS_COMMAND_POINTS_TO_BUILD_UNIT: "<PLAYER> has enough command
		## points to build a <OBJECT_TYPE>". STRICTLY READ-ONLY (condition
		## path). The player resolves like the base-building surface (bound
		## names plus the "<This Player>" token); the type resolves through
		## the production rules (retail name or runtime id ->
		## trainable_unit_type_for), and the cost is the SAME number
		## queue_unit will commit (sim.unit_command_point_cost), compared
		## against the same headroom the queue admission rule computes - so
		## this condition can never disagree with the production it gates.
		## A type outside the production rules refuses: its cost is not
		## derivable from anything the sim records, and a guessed answer
		## would steer the AI's build loop.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.has_command_points_to_build", "no simulation attached")
		var team := w._player_team_for_token(player)
		if team < 0:
			return _refuse_query(
				"units.has_command_points_to_build",
				"player '%s' is not bound to a simulation team" % player
				if player != RetailSliceScriptWorld.THIS_PLAYER_TOKEN
				else "'<This Player>' cannot resolve: no script player is bound (bind_script_player)"
			)
		if object_type == "":
			return _refuse_query(
				"units.has_command_points_to_build", "empty object type names no unit"
			)
		var unit_type: String = w.sim.trainable_unit_type_for(team, object_type)
		var cost: int = w.sim.unit_command_point_cost(unit_type) if unit_type != "" else -1
		if cost < 0:
			return _refuse_query(
				"units.has_command_points_to_build",
				(
					"object type '%s' is not a unit this simulation's production "
					+ "rules model, so its command-point cost is unknowable"
				) % object_type
			)
		var headroom: int = (
			w.sim.command_point_cap
			- w.sim.command_points_for_team(team)
			- w._queued_command_points(team)
		)
		return SageWorldQuery.hit(headroom >= cost)


# ==========================================================================
# AI (skirmish base building)
# ==========================================================================


class SliceAi:
	extends SageScriptWorld.Ai

	## Implemented: base_unpackable, base_unpack and build_base_building - the
	## unpack pair and the base-anchored build, against the CORRECTED
	## signatures (unpack carries its UNIT_REF destination; the build carries
	## the base anchor and its UNIT_REF, and no invented player). The acting
	## team for the playerless actions is the bound script player (class
	## comment). Everything else on the facet still refuses: foundations,
	## tactical markers, camp regions, base population, approach paths and
	## reinforcement armies are unmodeled sim state, and the per-marker build
	## additionally still needs its own facet method (WP11's
	## GAP_BUILD_PER_MARKER names the sourced signature).

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func base_unpackable(object_name: String, player: String) -> SageWorldQuery:
		## NAMED_BASE_UNPACKABLE_FOR_PLAYER - the single most-polled retail AI
		## condition (64 sites, one library's economy loop), so STRICTLY
		## READ-ONLY. True iff the flag is still packed while the match runs;
		## the sim models no per-player flag restrictions, so the player
		## argument's job is resolution ("<This Player>" included), after
		## which the answer is the same for every rostered player. Money is
		## not consulted (the sim documents why on base_flag_unpackable).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("ai.base_unpackable", "no simulation attached")
		var team := w._player_team_for_token(player)
		if team < 0:
			return _refuse_query(
				"ai.base_unpackable",
				"player '%s' is not bound to a simulation team" % player
				if player != RetailSliceScriptWorld.THIS_PLAYER_TOKEN
				else "'<This Player>' cannot resolve: no script player is bound (bind_script_player)"
			)
		var verdict: Dictionary = w.sim.base_flag_unpackable(object_name)
		if not bool(verdict.get("known", false)):
			return _refuse_query(
				"ai.base_unpackable",
				"'%s' is not a base flag this simulation models (false would be a guess)" % object_name
			)
		return SageWorldQuery.hit(bool(verdict.get("unpackable", false)))

	func base_unpack(object_name: String, free: bool, result_reference: String) -> bool:
		## NAMED_BASE_UNPACK (free=false) / NAMED_BASE_UNPACK_FREE (free=true).
		## The action carries no player: the actor is the bound script player.
		## On success the resulting base structure is bound to
		## result_reference, resolved NOW (retail binds AI_EXPANSION_1..N /
		## AI_BASE and later reads them as plain units). The reference is
		## validated BEFORE the sim moves, so a rejected binding costs nothing.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.base_unpack", "no simulation attached")
		var team := w._script_player_team()
		if team < 0:
			return _refuse_command(
				"ai.base_unpack",
				"no script player is bound (bind_script_player), and the sourced action carries no player argument to act as"
			)
		var rejection := w._unit_reference_rejection(result_reference)
		if rejection != "":
			return _refuse_command("ai.base_unpack", rejection)
		var result: Dictionary = w.sim.unpack_base(team, object_name, free)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"ai.base_unpack",
				"simulation rejected unpacking '%s': %s" % [object_name, String(result.get("reason", ""))]
			)
		w._bind_unit_reference(result_reference, int(result.get("structure_id", 0)))
		return true

	func build_base_building(building_type: String, base: String, result_reference: String) -> bool:
		## BUILD_BASE_BUILDING: build a <building_type> in the first available
		## slot of the base object <base> and bind the new building to
		## <result_reference>. The actor is the bound script player; the sim
		## enforces that the resolved base belongs to it (wrong-owner refuses
		## rather than building at someone else's base). The pad pick is the
		## sim's deterministic first-free-matching-pad-by-index rule
		## (issue_expansion_construct with no requested pad).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.build_base_building", "no simulation attached")
		var team := w._script_player_team()
		if team < 0:
			return _refuse_command(
				"ai.build_base_building",
				"no script player is bound (bind_script_player), and the sourced action carries no player argument to act as"
			)
		var kind: String = w.sim.expansion_kind_for_object_id(building_type)
		if kind == "":
			return _refuse_command(
				"ai.build_base_building",
				"object type '%s' is not an expansion the simulation models" % building_type
			)
		var resolved_base := w._resolve_base_structure(base)
		if resolved_base.is_empty():
			return _refuse_command(
				"ai.build_base_building",
				"base '%s' is not a bound unit reference or base flag" % base
			)
		if resolved_base.has("packed"):
			return _refuse_command(
				"ai.build_base_building",
				"base flag '%s' has not been unpacked; there is no base to build in" % base
			)
		var rejection := w._unit_reference_rejection(result_reference)
		if rejection != "":
			return _refuse_command("ai.build_base_building", rejection)
		var result: Dictionary = w.sim.issue_expansion_construct(
			team, int(resolved_base.get("id", 0)), kind
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"ai.build_base_building",
				"simulation rejected building '%s' at base '%s': %s" % [
					building_type, base, String(result.get("reason", ""))
				]
			)
		w._bind_unit_reference(result_reference, int(result.get("structure_id", 0)))
		return true
