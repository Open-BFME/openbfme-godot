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
## A legacy bind_team entry is a player's whole-roster DEFAULT team. Converter
## schema-v1 WorldBuilder rows use bind_script_team instead: each name remains
## a distinct, player-owned identity with typed sim member handles. The
## registry lives in RetailSliceSim so mutable membership/flags cross the
## snapshot/hash boundary; imported objectCount never synthesizes members.
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
## retail AI authors resolves to it too, on EVERY single-player facet slot
## (see _resolve_single_player_team - the 0dce37e census caught the facet
## helpers bypassing token resolution, 84% of all runtime gap volume).
## Without the binding both refuse - honestly, naming what is missing.
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
## Per-world public team spelling -> globally unique simulation registry key.
## Map teams use their public name as the key. Instantiated library-local
## teams need a private key because every Player_N executor sees the same
## authored spelling (for example "AI Base Team").
var _team_registry_names: Dictionary = {}
## Script team name -> exact bound player name. Unlike the reverse sim-team
## table this remains unambiguous when several named teams share one owner.
var _script_team_owners: Dictionary = {}
## Registry key -> configured player name, used after a mutable controller
## transfer without reverse-guessing from a flattened simulation owner id.
var _registry_team_owners: Dictionary = {}

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
	## reserved token spellings (RESERVED_PLAYER_TOKENS - a binding must never
	## shadow token resolution), unknown teams, rebinding a name to a
	## different team, and a second name on a team that already has one
	## (owner() needs the mapping 1:1).
	if (
		sim == null
		or player_name == ""
		or RESERVED_PLAYER_TOKENS.has(player_name)
		or not sim._script_owner_exists(team)
	):
		return false
	if _player_teams.has(player_name):
		return int(_player_teams[player_name]) == team
	if _team_players.has(team) and sim._is_combatant_team(team):
		return false
	_player_teams[player_name] = team
	if not _team_players.has(team):
		_team_players[team] = player_name
	return true


func bind_team(team_name: String, team: int) -> bool:
	## Bind a script team name (a player's DEFAULT team - the whole roster) to
	## a rostered sim team. Aliases are allowed; rebinding to a different team
	## is not. Reserved token spellings are refused as names (a map that bound
	## "<This Team>" literally would silently shadow token resolution).
	if (
		sim == null
		or team_name == ""
		or RESERVED_TEAM_TOKENS.has(team_name)
		or not sim.team_ids().has(team)
	):
		return false
	if _team_names.has(team_name):
		return int(_team_names[team_name]) == team
	var registered: Dictionary = sim.register_script_team(team_name, team, true)
	if not bool(registered.get("ok", false)):
		return false
	_team_names[team_name] = team
	_team_registry_names[team_name] = team_name
	if _team_players.has(team):
		_script_team_owners[team_name] = String(_team_players[team])
		_registry_team_owners[team_name] = String(_team_players[team])
	return true


func bind_default_script_team(
	team_name: String,
	player_name: String,
	handles: Array = [],
	membership_complete: bool = true,
	unresolved_members: Array = [],
	unmodeled_object_count: int = 0,
	dynamic_default_roster: bool = true
) -> bool:
	## Schema-v1 default-team binding keeps the exact authored player name.
	## Several authored players can share a noncombatant sim owner (notably
	## PlyrCivilian/PlyrNeutral), so the reverse owner table is insufficient.
	if (
		sim == null
		or team_name == ""
		or RESERVED_TEAM_TOKENS.has(team_name)
		or player_name == ""
		or not _player_teams.has(player_name)
	):
		return false
	var owner := int(_player_teams[player_name])
	if _team_names.has(team_name):
		return (
			int(_team_names[team_name]) == owner
			and String(_script_team_owners.get(team_name, "")) == player_name
		)
	var registered: Dictionary = sim.register_script_team(
		team_name,
		owner,
		true,
		handles,
		membership_complete,
		unresolved_members,
		unmodeled_object_count,
		dynamic_default_roster
	)
	if not bool(registered.get("ok", false)):
		return false
	_team_names[team_name] = owner
	_team_registry_names[team_name] = team_name
	_script_team_owners[team_name] = player_name
	_registry_team_owners[team_name] = player_name
	return true


func bind_script_team(
	team_name: String,
	player_name: String,
	handles: Array = [],
	membership_complete: bool = true,
	unresolved_members: Array = [],
	unmodeled_object_count: int = 0,
	marker_only: bool = false
) -> bool:
	## Bind one imported WorldBuilder team as its own identity. objectCount and
	## unresolved namedMembers are deliberately not accepted here: only typed
	## handles to rows already materialized in the sim can seed membership.
	if player_name == "" or not _player_teams.has(player_name):
		return false
	var owner := int(_player_teams[player_name])
	return bind_script_team_to_owner(
		team_name,
		owner,
		player_name,
		handles,
		membership_complete,
		unresolved_members,
		unmodeled_object_count,
		marker_only
	)


func bind_script_team_to_owner(
	team_name: String,
	owner: int,
	player_name: String = "",
	handles: Array = [],
	membership_complete: bool = true,
	unresolved_members: Array = [],
	unmodeled_object_count: int = 0,
	marker_only: bool = false
) -> bool:
	## Configuration-only variant used after schema-v1 ownership has been
	## resolved exactly. Named owners must already be bound and agree with the
	## explicit owner id; an empty owner name is never guessed here.
	if (
		sim == null
		or team_name == ""
		or RESERVED_TEAM_TOKENS.has(team_name)
		or not sim._script_owner_exists(owner)
		or player_name == ""
		or not _player_teams.has(player_name)
		or int(_player_teams[player_name]) != owner
		or unmodeled_object_count < 0
		or (
			membership_complete
			and (not unresolved_members.is_empty() or unmodeled_object_count != 0)
		)
	):
		return false
	if _team_names.has(team_name):
		return (
			int(_team_names[team_name]) == owner
			and String(_script_team_owners.get(team_name, "")) == player_name
		)
	var registered: Dictionary = sim.register_script_team(
		team_name,
		owner,
		false,
		handles,
		membership_complete,
		unresolved_members,
		unmodeled_object_count,
		true,
		marker_only
	)
	if not bool(registered.get("ok", false)):
		return false
	_team_names[team_name] = owner
	_team_registry_names[team_name] = team_name
	_script_team_owners[team_name] = player_name
	_registry_team_owners[team_name] = player_name
	return true


func bind_library_script_team(
	team_name: String,
	registry_name: String,
	player_name: String,
	default_team: bool,
	handles: Array = [],
	membership_complete: bool = true,
	unresolved_members: Array = [],
	unmodeled_object_count: int = 0,
	marker_only: bool = false
) -> bool:
	## A retail AI library is instantiated once per concrete Player_N. Its
	## team names live in that executor's namespace, while the authoritative
	## mutable record must remain globally unique in the shared sim.
	if (
		sim == null
		or team_name == ""
		or registry_name == ""
		or RESERVED_TEAM_TOKENS.has(team_name)
		or RESERVED_TEAM_TOKENS.has(registry_name)
		or player_name == ""
		or not _player_teams.has(player_name)
		or unmodeled_object_count < 0
		or (
			membership_complete
			and (not unresolved_members.is_empty() or unmodeled_object_count != 0)
		)
	):
		return false
	var owner := int(_player_teams[player_name])
	if _team_names.has(team_name):
		return (
			int(_team_names[team_name]) == owner
			and String(_team_registry_names.get(team_name, "")) == registry_name
			and String(_script_team_owners.get(team_name, "")) == player_name
		)
	var registered: Dictionary = sim.register_script_team(
		registry_name,
		owner,
		default_team,
		handles,
		membership_complete,
		unresolved_members,
		unmodeled_object_count,
		default_team,
		marker_only
	)
	if not bool(registered.get("ok", false)):
		return false
	_team_names[team_name] = owner
	_team_registry_names[team_name] = registry_name
	_script_team_owners[team_name] = player_name
	_registry_team_owners[registry_name] = player_name
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
## Per-seat presentation tokens (the corpus authors them on UI/EVA-flavoured
## members): which player is "local" is a property of one machine's seat, not
## of the match, so resolving either inside the lockstep simulation would
## desync peers. Both refuse, with that reason.
const LOCAL_PLAYER_TOKEN := "<Local Player>"
const LOCAL_PLAYERS_ENEMIES_TOKEN := "<Local Player's Enemies>"

## Every reserved token spelling the decoded corpus authors in a PLAYER slot.
## bind_player refuses these as names, so a binding can never shadow token
## resolution (a map that bound "<This Player>" as a literal player would
## otherwise silently re-aim every token site).
const RESERVED_PLAYER_TOKENS: Array[String] = [
	THIS_PLAYER_TOKEN,
	THIS_PLAYERS_ENEMIES_TOKEN,
	THIS_PLAYERS_ALLIES_TOKEN,
	THIS_PLAYERS_ENEMY_TOKEN,
	ALL_PLAYERS_TOKEN,
	LOCAL_PLAYER_TOKEN,
	LOCAL_PLAYERS_ENEMIES_TOKEN,
]

## The one reserved token the decoded corpus authors in a TEAM slot - and it
## authors it at essentially EVERY team-behavior call site (all 141
## TEAM_SET_STATE sites, all 385 TEAM_SET_CUSTOM_STATE sites, 26 of the 27
## TEAM_SET_ATTITUDE sites). In retail it resolves to the team the CURRENTLY
## EXECUTING script is attached to - ScriptEngine::m_callingTeam, falling
## back to m_conditionTeam (ScriptEngine.cpp getTeamNamed). Sequential-script
## progress latches both for the duration of each action. The script player's
## WHOLE ROSTER would be the wrong answer: retail's "<This Team>" is the
## individual attack team, not the player. Without a latched context the
## token still REFUSES. bind_team refuses the spelling as a name so a binding
## can never shadow the token.
const THIS_TEAM_TOKEN := "<This Team>"
const RESERVED_TEAM_TOKENS: Array[String] = [THIS_TEAM_TOKEN]

## Script-team name currently latched as ScriptEngine::m_callingTeam.
## Empty means no calling-team context (token refuses).
var _calling_script_team: String = ""
## Script-team name currently latched as ScriptEngine::m_conditionTeam.
## Empty means no condition-team context.
var _condition_script_team: String = ""


func latch_script_team_context(script_team: String, as_calling: bool = true) -> void:
	## Latch the retail calling/condition team for the duration of one
	## sequential action (or team-script evaluation). Does not validate the
	## name here - resolution re-checks registry membership.
	if as_calling:
		_calling_script_team = script_team
	_condition_script_team = script_team


func clear_script_team_context() -> void:
	_calling_script_team = ""
	_condition_script_team = ""


func active_script_team_context() -> String:
	## Retail getTeamNamed("<This Team>") prefers m_callingTeam, then
	## m_conditionTeam.
	if _calling_script_team != "":
		return _calling_script_team
	return _condition_script_team


func resolve_script_team_name(team_name: String) -> Dictionary:
	## Public TEAM-argument resolution used by Orders and other facets that
	## cannot reach SliceTeams._resolve_team. Answers
	## {"team": int, "script_team": String} or {"reason": String}.
	if sim == null:
		return {"reason": "no simulation attached"}
	var resolved_name := team_name
	if team_name == THIS_TEAM_TOKEN:
		var active := active_script_team_context()
		if active == "":
			return {"reason": (
				"'<This Team>' cannot resolve: no calling/condition team "
				+ "context is latched (sequential-script or team-script "
				+ "evaluation must set it; the script player's whole "
				+ "roster would be the wrong team)"
			)}
		resolved_name = active
	var script_team := _canonical_script_team_name(resolved_name)
	if script_team == "":
		return {"reason": "team '%s' is not bound to a simulation team" % team_name}
	var team := _bound_team(script_team)
	if team < 0:
		return {"reason": "team '%s' is not bound to a simulation team" % team_name}
	if not sim.script_teams.has(script_team):
		return {"reason": "team '%s' has no authoritative script-team identity" % team_name}
	return {"team": team, "script_team": script_team}


func _resolve_single_player_team(player: String) -> Dictionary:
	## Player-argument resolution for EVERY facet slot that takes exactly one
	## player. This is the routing seam the 0dce37e execution census exposed:
	## script-authored PLAYER slots carry the retail tokens, and a slot that
	## consults only the literal binding table refuses "<This Player>" - 4,197
	## SKIRMISH_PLAYER_FACTION refusals (84% of all runtime gap volume) were
	## this one bypass, not a missing capability. Answers {"team": int} or
	## {"reason": String}. Read-only - condition paths resolve through here.
	##
	## Token verdicts, each deliberate:
	##   * "<This Player>" resolves to the bound script player - the same rule
	##     the base-building surface always applied.
	##   * The SET tokens (enemies / allies-incl-self / all-players) refuse
	##     HERE: a set is not a single player. Members that census over sets
	##     resolve through _census_teams_for_player instead.
	##   * The singular "<This Player's Enemy>" refuses: it names the AI's
	##     current-enemy choice, a model this simulation does not carry, and
	##     guessing which enemy would turn an honest refusal into a wrong
	##     answer.
	##   * The "<Local Player>" spellings refuse: per-seat presentation state,
	##     desync-bait inside a lockstep simulation (see the constants above).
	if sim == null:
		return {"reason": "no simulation attached"}
	match player:
		THIS_PLAYER_TOKEN:
			var script_team := _script_player_team()
			if script_team < 0:
				return {
					"reason":
					"'<This Player>' cannot resolve: no script player is bound (bind_script_player)"
				}
			return {"team": script_team}
		THIS_PLAYERS_ENEMIES_TOKEN, THIS_PLAYERS_ALLIES_TOKEN, ALL_PLAYERS_TOKEN:
			return {
				"reason":
				"'%s' names a SET of players; this member takes exactly one player" % player
			}
		THIS_PLAYERS_ENEMY_TOKEN:
			return {
				"reason":
				"the singular '<This Player's Enemy>' token names the AI's "
				+ "current-enemy choice, a model this simulation does not carry "
				+ "(refusing rather than guessing which enemy)"
			}
		LOCAL_PLAYER_TOKEN, LOCAL_PLAYERS_ENEMIES_TOKEN:
			return {
				"reason":
				(
					"'%s' is per-seat presentation state (which player is 'local' "
					+ "differs on every peer); resolving it inside the lockstep "
					+ "simulation would desync"
				) % player
			}
	var team := _bound_player_team(player)
	if team < 0:
		return {"reason": "player '%s' is not bound to a simulation team" % player}
	return {"team": team}


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
		ALL_PLAYERS_TOKEN:
			return {
				"reason":
				"whether retail's '<All Players>' census includes the neutral "
				+ "and creep players is unpinned; refusing rather than guessing "
				+ "the set"
			}
		LOCAL_PLAYER_TOKEN, LOCAL_PLAYERS_ENEMIES_TOKEN:
			return {
				"reason":
				(
					"'%s' is per-seat presentation state (which player is 'local' "
					+ "differs on every peer); resolving it inside the lockstep "
					+ "simulation would desync"
				) % player
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
		# A reference may also hold a BASE FLAG (the 32-call-site shape of
		# SET_UNIT_REFERENCE; see the sim's bind_script_unit_reference_to_base).
		# It answers as the flag it names, so a reference to a flag and the
		# flag's own name resolve identically - which is what retail's shared
		# namespace does and what the AI libraries rely on when they aim
		# AI_CURRENT_CONSTRUCTION_SITE at BASE_FLAG_n and then build at it.
		var reference_base: String = sim.script_unit_reference_base(team, name)
		if reference_base != "":
			return {"kind": "base_flag", "name": reference_base}
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
	if sim == null:
		return -1
	var canonical := _canonical_script_team_name(team_name)
	if canonical == "":
		return -1
	var owner: Dictionary = sim.script_team_owner(canonical)
	if not bool(owner.get("ok", false)):
		return -1
	return int(owner.get("owner", -1))


func _canonical_script_team_name(team_name: String) -> String:
	## Retail AI qualifies inheritance teams as
	## "PlyrCivilian/Player_N_Inherit", while the decoded WorldBuilder team
	## registry stores the local team name and its exact owner separately.
	## Accept either the already-bound exact name or exactly one validated
	## owner/name separator. Never strip arbitrary path components.
	if _team_names.has(team_name):
		return String(_team_registry_names.get(team_name, team_name))
	if team_name.count("/") != 1:
		return ""
	var owner_name := team_name.get_slice("/", 0)
	var local_name := team_name.get_slice("/", 1)
	if (
		owner_name == ""
		or local_name == ""
		or not _team_names.has(local_name)
		or String(_script_team_owners.get(local_name, "")) != owner_name
	):
		return ""
	return String(_team_registry_names.get(local_name, local_name))


# --- Shared read-only helpers ---------------------------------------------


func _relation_between(team_a: int, team_b: int) -> int:
	## ParamTypes RELATION int for two ROSTERED teams, mirroring the sim's
	## alliance rule (retail_slice_sim._is_hostile for rostered combatants):
	## same team or shared non-null alliance id -> Friend, everything else ->
	## Enemy (rostered free-for-all default). Script overrides in
	## player_diplomacy_overrides win when present (either direction).
	var relation: Dictionary = ParamTypes.ENUMS["RELATION"]
	if team_a == team_b:
		return int(relation["Friend"])
	if sim != null:
		var ov_a: Dictionary = sim.player_diplomacy_overrides.get(team_a, {}) as Dictionary
		if ov_a.has(team_b):
			return int(ov_a[team_b])
		var ov_b: Dictionary = sim.player_diplomacy_overrides.get(team_b, {}) as Dictionary
		if ov_b.has(team_a):
			return int(ov_b[team_a])
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


static func _world_point(position: Vector2) -> Vector3:
	## Inverse of _sim_point. The sim is 2D, so the height component is 0.0 -
	## the same flat plane every other sim-sourced position in this adapter
	## reports; nothing here invents terrain height it does not have.
	return Vector3(position.x, 0.0, position.y)


func named_object_view(name: String) -> Dictionary:
	## The ONE read behind every units.* method keyed by a script object name.
	## Resolves through the shared object / unit-reference namespace
	## (resolve_script_object) and flattens what the sim knows about the
	## subject into a shape the facet methods can answer from without each
	## re-deriving the flag/structure split. Strictly READ-ONLY.
	##
	## {} when the name is outside the namespace entirely. Otherwise:
	##   "flag"          base-flag name, "" for a structure binding
	##   "packed"        true for a flag nobody has unpacked - retail's flag
	##                   OBJECT exists on the map, so this is a live subject
	##                   with a position, but no owner and no health the sim
	##                   models
	##   "structure_id"  0 when there is no structure (a packed flag)
	##   "present"       whether the structure row still exists (retail's
	##                   "the name-table pointer is not NULL")
	##   "position"      Vector2 sim point; valid when packed or present
	##   "team"          owning sim team, -1 when there is none
	##   "health"/"maximum_health"  valid when present
	var handle := resolve_script_object(name)
	if handle.is_empty():
		return {}
	var view := {
		"flag": "",
		"packed": false,
		"structure_id": 0,
		"present": false,
		"position": Vector2.ZERO,
		"team": -1,
		"health": 0,
		"maximum_health": 0,
	}
	if String(handle.get("kind", "")) == "base_flag":
		var flag_name := String(handle.get("name", ""))
		var row: Dictionary = sim.unpackable_base_state(flag_name)
		view["flag"] = flag_name
		view["position"] = Vector2(row.get("position", Vector2.ZERO))
		if int(row.get("unpacked_by", -1)) < 0:
			view["packed"] = true
			return view
		view["structure_id"] = int(row.get("structure_id", 0))
	else:
		view["structure_id"] = int(handle.get("id", 0))
	var structure_id := int(view["structure_id"])
	if structure_id == 0 or not sim.structures.has(structure_id):
		return view
	var structure: Dictionary = sim.structures[structure_id]
	view["present"] = true
	view["position"] = Vector2(structure.get("position", Vector2.ZERO))
	view["team"] = int(structure.get("team", -1))
	view["health"] = int(structure.get("health", 0))
	view["maximum_health"] = maxi(1, int(structure.get("maximum_health", 1)))
	return view


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
	## CAP_RANDOM is advertised because the sim now OWNS the stream it names:
	## sim.logic_random_int is retail's GameLogic generator (seeded from match
	## rules, six words hashed/snapshotted empty-is-absent), so serving it is
	## lockstep-safe - the historical refusal ("no deterministic stream, and
	## randi() would desync") described a sim that no longer exists. What is
	## STILL refused, deliberately: the client-random family
	## (SET_COUNTER_TO_CLIENT_RANDOM_VALUE) - that is a different stream in
	## retail too, desync-prone by design.
	if sim == null:
		return false
	return capability in [CAP_PLAYER_MONEY, CAP_RANDOM, CAP_DEBUG_OUTPUT]


func world_frame() -> int:
	return sim.tick_index if sim != null else 0


func random_int(low: int, high: int) -> int:
	## The sim-owned logic stream (retail's GameLogic generator; see the
	## logic-random section in retail_slice_sim.gd for semantics, seeding and
	## the boundary story). Inclusive of both bounds, retail's exact mapping.
	## MUTATES hashed sim state (the stream advances): handlers only reach
	## here through actions, never conditions, so the read-only query
	## contract stands.
	return sim.logic_random_int(low, high)


func random_real(low: float, high: float) -> float:
	## Retail's script engine has NO real-valued random draw path of its own:
	## SET_RANDOM_MSEC_TIMER's REAL bounds go through GameLogicRandomValue,
	## whose parameters are C `int` - the bounds truncate toward zero and ONE
	## integer is drawn from the logic stream (ScriptEngine.cpp:6746-6752 +
	## LogicRandomValue.h:42 in the GPL Zero Hour source). Mirroring that
	## keeps this integer-only (no float in the generator or mapping - the
	## cross-platform constraint) and keeps stream POSITION retail-aligned at
	## one draw per call. GDScript int(float) truncates toward zero like C.
	## ASSUMPTION, stated: BFME-era actions (SET_RANDOM_COUNTER_IN_SECONDS)
	## could instead call the binary's GetGameLogicRandomValueReal (it exists
	## in the BFME1 masm dumps); the decompiled dispatch is unreadable asm,
	## so ZH's script-engine precedent is the best evidence. Falsified if a
	## readable BFME dispatch surfaces showing the Real path - the fix would
	## swap this mapping, not the generator or the boundary.
	return float(sim.logic_random_int(int(low), int(high)))


func player_money(player: String) -> int:
	## Pre-facet API without a refusal channel - see the class comment.
	## Resolvable players (bound names and the single-player tokens - the
	## tranche 1 handlers pass script-authored PLAYER slots straight here)
	## answer real team resources; anything else can only get 0.
	## economy() below is the honest surface.
	var resolved := _resolve_single_player_team(player)
	if resolved.has("reason"):
		return 0
	return sim.resources_for_team(int(resolved["team"]))


func set_player_money(player: String, amount: int) -> bool:
	## Token-aware like player_money: PLAYER_SET_MONEY's handler passes the
	## script-authored slot straight here, and retail authors "<This Player>"
	## in it (the census's Setup Player script).
	var resolved := _resolve_single_player_team(player)
	if resolved.has("reason"):
		return false
	sim.team_resources[int(resolved["team"])] = amount
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


func _make_terrain() -> Terrain:
	return SliceTerrain.new()


func _make_areas() -> Areas:
	return SliceAreas.new()


func _make_transport() -> Transport:
	return SliceTransport.new()


func _surface_key(parts: Array) -> String:
	var bits: PackedStringArray = PackedStringArray()
	for p in parts:
		bits.append(str(p))
	return "|".join(bits)


func _make_fog() -> Fog:
	return SliceFog.new()


func _make_camera() -> PresentationSink:
	return SlicePresentationSink.new("camera")


func _make_audio() -> PresentationSink:
	return SlicePresentationSink.new("audio")


func _make_ui() -> PresentationSink:
	return SlicePresentationSink.new("ui")




func _living_ids_for_order_scope(scope: int, name: String) -> Dictionary:
	if sim == null:
		return {"reason": "no simulation attached"}
	if sim.winner != -1:
		return {"reason": "the match is already resolved"}
	match scope:
		SageScriptWorld.Scope.TEAM:
			var team_resolved := resolve_script_team_name(name)
			if team_resolved.has("reason"):
				return {"reason": String(team_resolved["reason"])}
			var script_team := String(team_resolved["script_team"])
			var members: Dictionary = sim.script_team_members(script_team, true)
			if not bool(members.get("ok", false)):
				return {"reason": String(members.get("reason", ""))}
			if not bool(members.get("complete", false)):
				return {"reason": "team '%s' has incomplete imported membership" % name}
			var ids: Array = []
			for handle_value in members.get("members", []) as Array:
				var handle := handle_value as Dictionary
				if String(handle.get("kind", "")) == "entity":
					ids.append(int(handle.get("id", 0)))
			return {"team": int(team_resolved["team"]), "ids": ids}
		SageScriptWorld.Scope.PLAYER:
			var resolved := _resolve_single_player_team(name)
			if resolved.has("reason"):
				return {"reason": String(resolved["reason"])}
			var team := int(resolved["team"])
			return {"team": team, "ids": sim.living_ids(team)}
		SageScriptWorld.Scope.UNIT:
			if name.is_valid_int() and sim.entities.has(int(name)):
				return {
					"team": int((sim.entities[int(name)] as Dictionary).get("team", -1)),
					"ids": [int(name)],
				}
			return {"reason": "unit scope name '%s' is not a live entity id" % name}
		_:
			return {"reason": "unsupported scope %d" % scope}


# ==========================================================================
# PLAYERS
# ==========================================================================


class SlicePlayers:
	extends SageScriptWorld.Players

	## Implemented: exists, faction, start_position, the three command-point reads,
	## building_count, relation_to, can_build_at_base (the base-anchored
	## buildability read, against the CORRECTED signature that carries the
	## base - WP17's reported defect, since fixed), and object_count_of_types
	## (the retail AI's single heaviest blocked read, served through the
	## sim's object-type identity census). Everything else refuses.

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func _team_or_refuse(method: String, player: String) -> Dictionary:
		## Single-player resolution for the whole facet: bound names AND the
		## retail tokens, through _resolve_single_player_team (the 0dce37e
		## census's 84% gap was this helper bypassing token resolution).
		var w := _world()
		if w == null or w.sim == null:
			return {"query": _refuse_query(method, "no simulation attached")}
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return {"query": _refuse_query(method, String(resolved["reason"]))}
		return {"team": int(resolved["team"])}

	func exists(player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("players.exists", "no simulation attached")
		if RetailSliceScriptWorld.RESERVED_PLAYER_TOKENS.has(player):
			# A token is not a bindable name, so the exhaustive-bindings rule
			# below does not apply to it: a token that RESOLVES names a player
			# that certainly exists, and one that cannot resolve gets its
			# refusal - never a false "not in this match".
			var resolved := w._resolve_single_player_team(player)
			if resolved.has("reason"):
				return _refuse_query("players.exists", String(resolved["reason"]))
			return SageWorldQuery.hit(true)
		# Bindings are declared exhaustive (class comment), so an unbound name
		# IS absent from the match - false is an answer here, not a dodge.
		return SageWorldQuery.hit(w._bound_player_team(player) >= 0)

	func faction(player: String) -> SageWorldQuery:
		## Answers the RETAIL SIDE TOKEN (playertemplate.ini `Side =`: "Men",
		## "Isengard", ...), NOT the lowercase pack faction id the descriptor
		## carries. Retail's SKIRMISH_PLAYER_FACTION is an exact string match
		## against player->getSide(), so answering the pack id here made every
		## faction gate in a live match false-but-plausible - no faction's
		## spell system ever enabled and the AI could buy nothing. The
		## pack-id -> side translation is the sim's (team_retail_side, backed
		## by the hashed retail_faction_sides rules table); an unmapped
		## faction refuses loudly with the faction named.
		var resolved := _team_or_refuse("players.faction", player)
		if resolved.has("query"):
			return resolved["query"]
		var side_result: Dictionary = _world().sim.team_retail_side(int(resolved["team"]))
		if side_result.has("reason"):
			return _refuse_query(
				"players.faction",
				"player '%s': %s" % [player, String(side_result["reason"])]
			)
		return SageWorldQuery.hit(String(side_result["side"]))

	func start_position(player: String) -> SageWorldQuery:
		## START_POSITION_IS. Retail compares the script's authored 1-based
		## position against Player::getMpStartIndex() after subtracting one.
		## The roster descriptor stores that engine-internal zero-based value;
		## convert it back for the handler's exact integer comparison.
		var resolved := _team_or_refuse("players.start_position", player)
		if resolved.has("query"):
			return resolved["query"]
		var descriptor: Dictionary = _world().sim.team_descriptor(int(resolved["team"]))
		if not descriptor.has("start_index"):
			return _refuse_query(
				"players.start_position",
				"player '%s' has no authoritative multiplayer start assignment" % player
			)
		var start_value: Variant = descriptor["start_index"]
		if typeof(start_value) != TYPE_INT or int(start_value) < 0:
			return _refuse_query(
				"players.start_position",
				"player '%s' has an invalid multiplayer start assignment" % player
			)
		return SageWorldQuery.hit(int(start_value) + 1)

	func command_points_available(player: String) -> SageWorldQuery:
		## cap - committed - queue-reserved: exactly the headroom the sim's own
		## queue_unit admission rule computes, not an invented notion.
		var resolved := _team_or_refuse("players.command_points_available", player)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var team := int(resolved["team"])
		return SageWorldQuery.hit(
			w.sim.command_point_total_for_team(team)
			- w.sim.command_points_for_team(team)
			- w._queued_command_points(team)
		)

	func command_points_total(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.command_points_total", player)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(
			_world().sim.command_point_total_for_team(int(resolved["team"]))
		)

	func command_points_used(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.command_points_used", player)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(_world().sim.command_points_for_team(int(resolved["team"])))

	func override_command_points(player: String, total: int, maximum: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("players.override_command_points", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command(
				"players.override_command_points", String(resolved["reason"])
			)
		if not w.sim.override_command_points_for_team(
			int(resolved["team"]), total, maximum
		):
			return _refuse_command(
				"players.override_command_points",
				"simulation rejected total=%d maximum=%d" % [total, maximum]
			)
		return true

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
		var other_resolved := w._resolve_single_player_team(other)
		if other_resolved.has("reason"):
			return _refuse_query("players.relation_to", String(other_resolved["reason"]))
		return SageWorldQuery.hit(
			w._relation_between(int(resolved["team"]), int(other_resolved["team"]))
		)

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
		var resolved_player := w._resolve_single_player_team(player)
		if resolved_player.has("reason"):
			return _refuse_query("players.can_build_at_base", String(resolved_player["reason"]))
		var team := int(resolved_player["team"])
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

	func has_prerequisite_to_build(
		player: String, object_type: String
	) -> SageWorldQuery:
		var resolved := _team_or_refuse(
			"players.has_prerequisite_to_build", player
		)
		if resolved.has("query"):
			return resolved["query"]
		var answer: Dictionary = _world().sim.has_prerequisite_to_build(
			int(resolved["team"]), object_type
		)
		if not bool(answer.get("ok", false)):
			return _refuse_query(
				"players.has_prerequisite_to_build",
				String(answer.get("reason", "")),
			)
		return SageWorldQuery.hit(bool(answer.get("value", false)))

	func set_base_construction_enabled(player: String, enabled: bool) -> bool:
		var resolved := _team_or_refuse_command_player(
			"players.set_base_construction_enabled", player
		)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim.set_base_construction_enabled(
			int(resolved["team"]), enabled
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"players.set_base_construction_enabled",
				String(result.get("reason", "")),
			)
		return true

	func set_base_construction_speed(player: String, factor: float) -> bool:
		var resolved := _team_or_refuse_command_player(
			"players.set_base_construction_speed", player
		)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim.set_base_construction_speed(
			int(resolved["team"]), factor
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"players.set_base_construction_speed",
				String(result.get("reason", "")),
			)
		return true

	func set_factories_enabled(player: String, enabled: bool) -> bool:
		var resolved := _team_or_refuse_command_player(
			"players.set_factories_enabled", player
		)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim.set_factories_enabled(
			int(resolved["team"]), enabled
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"players.set_factories_enabled",
				String(result.get("reason", "")),
			)
		return true

	func set_unit_construction_enabled(
		player: String, object_type: String, enabled: bool
	) -> bool:
		var resolved := _team_or_refuse_command_player(
			"players.set_unit_construction_enabled", player
		)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim.set_unit_construction_enabled(
			int(resolved["team"]), object_type, enabled
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"players.set_unit_construction_enabled",
				String(result.get("reason", "")),
			)
		return true

	func set_auto_build_enabled(player: String, enabled: bool) -> bool:
		var resolved := _team_or_refuse_command_player(
			"players.set_auto_build_enabled", player
		)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim.set_auto_build_enabled(
			int(resolved["team"]), enabled
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"players.set_auto_build_enabled",
				String(result.get("reason", "")),
			)
		return true

	func _team_or_refuse_command_player(method: String, player: String) -> Dictionary:
		var w := _world()
		if w == null or w.sim == null:
			return {
				"refused": _refuse_command(method, "no simulation attached")
			}
		if w.sim.winner != -1:
			return {
				"refused": _refuse_command(method, "the match is already resolved")
			}
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return {
				"refused": _refuse_command(method, String(resolved["reason"]))
			}
		return {"team": int(resolved["team"])}

	func rank_level(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.rank_level", player)
		if resolved.has("query"):
			return resolved["query"]
		var row: Dictionary = _world().sim._player_prog(int(resolved["team"]))
		return SageWorldQuery.hit(int(row.get("rank_level", 1)))

	func set_rank_level(player: String, level: int) -> bool:
		var resolved := _team_or_refuse_command_player("players.set_rank_level", player)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim._set_player_prog_value(
			int(resolved["team"]), "rank_level", level
		)
		return true if bool(result.get("ok", false)) else _refuse_command(
			"players.set_rank_level", String(result.get("reason", ""))
		)

	func add_rank_level(player: String, delta: int) -> bool:
		var resolved := _team_or_refuse_command_player("players.add_rank_level", player)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim._add_player_prog_value(
			int(resolved["team"]), "rank_level", delta
		)
		return true if bool(result.get("ok", false)) else _refuse_command(
			"players.add_rank_level", String(result.get("reason", ""))
		)

	func set_rank_level_limit(player: String, limit: int) -> bool:
		var resolved := _team_or_refuse_command_player("players.set_rank_level_limit", player)
		if resolved.has("refused"):
			return false
		_world().sim._set_player_prog_value(int(resolved["team"]), "rank_level_limit", limit)
		return true

	func reached_level_cap(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.reached_level_cap", player)
		if resolved.has("query"):
			return resolved["query"]
		var row: Dictionary = _world().sim._player_prog(int(resolved["team"]))
		return SageWorldQuery.hit(
			int(row.get("rank_level", 1)) >= int(row.get("rank_level_limit", 10))
		)

	func add_skill_points(player: String, amount: int) -> bool:
		var resolved := _team_or_refuse_command_player("players.add_skill_points", player)
		if resolved.has("refused"):
			return false
		_world().sim._add_player_prog_value(int(resolved["team"]), "skill_points", amount)
		return true

	func select_skill_set(player: String, skill_set: int) -> bool:
		var resolved := _team_or_refuse_command_player("players.select_skill_set", player)
		if resolved.has("refused"):
			return false
		_world().sim._set_player_prog_value(int(resolved["team"]), "skill_set", skill_set)
		return true

	func light_points(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.light_points", player)
		if resolved.has("query"):
			return resolved["query"]
		var row: Dictionary = _world().sim._player_prog(int(resolved["team"]))
		return SageWorldQuery.hit(int(row.get("light_points", 0)))

	func give_light_points(player: String, amount: int) -> bool:
		var resolved := _team_or_refuse_command_player("players.give_light_points", player)
		if resolved.has("refused"):
			return false
		_world().sim._add_player_prog_value(int(resolved["team"]), "light_points", amount)
		return true

	func change_light_point_level(player: String, delta: int) -> bool:
		return give_light_points(player, delta)

	func reset_light_points(player: String) -> bool:
		var resolved := _team_or_refuse_command_player("players.reset_light_points", player)
		if resolved.has("refused"):
			return false
		_world().sim._set_player_prog_value(int(resolved["team"]), "light_points", 0)
		return true

	func set_max_spell_points(player: String, amount: int) -> bool:
		var resolved := _team_or_refuse_command_player("players.set_max_spell_points", player)
		if resolved.has("refused"):
			return false
		_world().sim._set_player_prog_value(int(resolved["team"]), "max_spell_points", amount)
		return true

	func exit_all_buildings(player: String) -> bool:
		var resolved := _team_or_refuse_command_player("players.exit_all_buildings", player)
		if resolved.has("refused"):
			return false
		var w := _world()
		for eid in w.sim.living_ids(int(resolved["team"])):
			w.sim.exit_entity_container(int(eid))
		return true

	func sell_everything(player: String) -> bool:
		var resolved := _team_or_refuse_command_player("players.sell_everything", player)
		if resolved.has("refused"):
			return false
		var w := _world()
		var team := int(resolved["team"])
		## Honest script-surface sell: despawn owned living assets and structures.
		## Full retail refund tables remain a later economy model.
		for eid in w.sim.living_ids(team).duplicate():
			w.sim.delete_entity(int(eid))
		for sid in w.sim.living_structure_ids(team).duplicate():
			if w.sim.structures.has(int(sid)):
				w.sim.structures.erase(int(sid))
		w.sim.surface_bag_set("sold_everything:%s" % team, true)
		return true

	func repair_structure(player: String, object_name: String) -> bool:
		var resolved := _team_or_refuse_command_player("players.repair_structure", player)
		if resolved.has("refused"):
			return false
		var w := _world()
		var view := w.named_object_view(object_name)
		if view.is_empty() or int(view.get("structure_id", 0)) <= 0:
			return _refuse_command("players.repair_structure", "'%s' is not a structure" % object_name)
		var sid := int(view["structure_id"])
		if not w.sim.structures.has(sid):
			return _refuse_command("players.repair_structure", "structure missing")
		var row: Dictionary = w.sim.structures[sid]
		if int(row.get("team", -1)) != int(resolved["team"]):
			return _refuse_command("players.repair_structure", "structure not owned by player")
		row["health"] = int(row.get("maximum_health", row.get("health", 0)))
		w.sim.structures[sid] = row
		return true




	func base_count(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.base_count", player)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var count := 0
		for sid in w.sim.structures.keys():
			var row: Dictionary = w.sim.structures[sid]
			if int(row.get("team", -1)) != int(resolved["team"]):
				continue
			if String(row.get("kind", "")) in ["castle", "camp", "fortress", "base"]:
				count += 1
			elif bool(row.get("is_base", false)):
				count += 1
		if count == 0:
			# Count any living structure as a base proxy when no kind tags exist.
			count = w.sim.living_structure_ids(int(resolved["team"])).size()
			if count > 0:
				count = 1
		return SageWorldQuery.hit(count)

	func home_base(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.home_base", player)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var bag: Dictionary = w.sim.surface_bag_dict("home_base")
		if bag.has(str(resolved["team"])):
			return SageWorldQuery.hit(String(bag[str(resolved["team"])]))
		var ids: Array = w.sim.living_structure_ids(int(resolved["team"]))
		if ids.is_empty():
			return SageWorldQuery.hit("")
		return SageWorldQuery.hit(str(ids[0]))

	func is_in_planning_mode(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.is_in_planning_mode", player)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(
			_world().sim.surface_bag_bool("planning:%s" % resolved["team"], false)
		)

	func power_produced(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.power_produced", player)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(
			_world().sim.surface_bag_int("power_produced:%s" % resolved["team"], 0)
		)

	func power_consumed(player: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.power_consumed", player)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(
			_world().sim.surface_bag_int("power_consumed:%s" % resolved["team"], 0)
		)

	func set_excluded_from_score_screen(player: String, excluded: bool) -> bool:
		var resolved := _team_or_refuse_command_player(
			"players.set_excluded_from_score_screen", player
		)
		if resolved.has("refused"):
			return false
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"players.set_excluded_from_score_screen", "no simulation attached"
			)
		w.sim._ensure_parity()
		var se: Dictionary = w.sim.parity.score_excluded
		se[int(resolved["team"])] = excluded
		w.sim.parity.score_excluded = se
		return true

	func set_relation_to(player: String, other: String, relation: int) -> bool:
		var resolved := _team_or_refuse_command_player("players.set_relation_to", player)
		if resolved.has("refused"):
			return false
		var w := _world()
		var other_r := w._resolve_single_player_team(other)
		if other_r.has("reason"):
			return _refuse_command("players.set_relation_to", String(other_r["reason"]))
		var bag: Dictionary = w.sim.player_diplomacy_overrides.duplicate(true)
		var row: Dictionary = bag.get(int(resolved["team"]), {}) as Dictionary
		row[int(other_r["team"])] = relation
		bag[int(resolved["team"])] = row
		w.sim.player_diplomacy_overrides = bag
		return true

	func has_discovered(player: String, other: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.has_discovered", player)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var other_r := w._resolve_single_player_team(other)
		if other_r.has("reason"):
			return _refuse_query("players.has_discovered", String(other_r["reason"]))
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool(
				"discovered:%s:%s" % [resolved["team"], other_r["team"]], false
			)
		)

	func lost_object_type(player: String, object_type: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.lost_object_type", player)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(
			_world().sim.surface_bag_int(
				"lost_type:%s:%s" % [resolved["team"], object_type], 0
			)
		)



	func eva_event_played_within(player: String, event: String, seconds: float) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.eva_event_played_within", player)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var at := w.sim.surface_bag_int(
			"eva:%s:%s" % [resolved["team"], event], -10_000_000
		)
		var ticks := int(round(seconds * 30.0))
		return SageWorldQuery.hit(w.sim.tick_index - at <= ticks)
	func units_near_last_eva_event(player: String, radius: float) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.units_near_last_eva_event", player)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(
			_world().sim.surface_bag_int("eva_near:%s" % resolved["team"], 0)
		)



	func object_count_with_model_condition(
		player: String, model_condition: String
	) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.object_count_with_model_condition", player)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var flag := "mc:" + model_condition
		var count := 0
		for eid in w.sim.living_ids(int(resolved["team"])):
			if w.sim.entity_bool_flag(int(eid), flag) or w.sim.entity_timed_flag_active(int(eid), flag):
				count += 1
		return SageWorldQuery.hit(count)
	func object_count_within_distance(
		player: String, object_type: String, origin: String, distance: float
	) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.object_count_within_distance", player)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var origin_pos := Vector2.ZERO
		if origin.is_valid_int() and w.sim.entities.has(int(origin)):
			origin_pos = (w.sim.entities[int(origin)] as Dictionary).get("position", Vector2.ZERO)
		elif w.sim.script_waypoints.has(origin):
			origin_pos = w.sim.script_waypoints[origin]
		else:
			return _refuse_query(
				"players.object_count_within_distance",
				"origin '%s' is not a live entity id or registered waypoint" % origin
			)
		var count := 0
		for eid in w.sim.living_ids(int(resolved["team"])):
			var row: Dictionary = w.sim.entities[int(eid)]
			if object_type != "" and String(row.get("unit_type", "")) != object_type:
				continue
			var pos: Vector2 = row.get("position", Vector2.ZERO)
			if origin_pos.distance_to(pos) <= distance:
				count += 1
		return SageWorldQuery.hit(count)

	func threat_at(player: String, threat_finder: String, threat_type: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("players.threat_at", player)
		if resolved.has("query"):
			return resolved["query"]
		return SageWorldQuery.hit(
			_world().sim.surface_bag_int(
				"threat_at:%s:%s:%s" % [resolved["team"], threat_finder, threat_type], 0
			)
		)

	func force_emotion(player: String, emotion: int, duration_ticks: int) -> bool:
		var resolved := _team_or_refuse_command_player("players.force_emotion", player)
		if resolved.has("refused"):
			return false
		var w := _world()
		var until := w.sim.tick_index + maxi(0, duration_ticks)
		var team_id := int(resolved["team"])
		for eid in w.sim.living_ids(team_id):
			w.sim.set_entity_string_state(int(eid), "emotion", str(emotion))
			if w.sim.has_method("set_entity_timed_flag"):
				w.sim.set_entity_timed_flag(int(eid), "emotion", until)
			if emotion != 0:
				w.sim.issue_set_stance([int(eid)], "HoldGround", team_id)
		w.sim._set_player_prog_value(
			team_id, "force_emotion", {"emotion": emotion, "until": until}
		)
		return true


	func transfer_ownership(from_player: String, to_player: String) -> bool:
		var resolved := _team_or_refuse_command_player("players.transfer_ownership", from_player)
		if resolved.has("refused"):
			return false
		var w := _world()
		var other_r := w._resolve_single_player_team(to_player)
		if other_r.has("reason"):
			return _refuse_command("players.transfer_ownership", String(other_r["reason"]))
		var ids: Array = w.sim.living_ids(int(resolved["team"])).duplicate()
		var xfer: Dictionary = w.sim.transfer_entities_to_team(ids, int(other_r["team"]))
		return true if bool(xfer.get("ok", false)) else _refuse_command(
			"players.transfer_ownership", String(xfer.get("reason", ""))
		)

class SliceTeams:
	extends SageScriptWorld.Teams

	## Implemented: exists, unit_count, was_destroyed, owner, stop
	## (non-disband), and the team-behavior-state four - state/set_state (the
	## retail TEAM_STATE string, sim.team_behavior_state) and
	## custom_state/set_custom_state (the custom-state token set,
	## sim.team_custom_states). Sub-player teams, discovery, threat and
	## recruitment still refuse.
	##
	## DELIBERATELY STILL REFUSING on this facet, with the evidence:
	##   * was_created - unlike was_destroyed below, this one really IS an
	##     edge: Team::m_created is set by setActive() and cleared by the next
	##     Team::updateState (once per frame from Player::update), and it is
	##     save-persisted. This simulation has no team INSTANTIATION event to
	##     latch - a player's roster is not created mid-match - so there is no
	##     edge to report, and answering exists() instead would refire the
	##     AI's one-shot initialisation on every evaluation. All 22 retail-AI
	##     call sites author "<This Team>" anyway.
	##   * merge_into / delete / recruit* / collect_nearby / set_reference /
	##     the recruitment flags - all need the WorldBuilder sub-player team
	##     model (named instances with their own membership). A store that
	##     accepted these would have no members to move and nothing in the sim
	##     would ever consult it: a silent no-op, refused on the same standard
	##     the object-name and team-behavior lanes applied.
	##   * leader - TEAM_IS_LED_BY_UNIT is a leadership-AURA test between a
	##     team and a named unit (BFME condition 123), not a leader
	##     designation; see the facet declaration.
	##   * set_attitude - the retail WRITE is sourced exactly (a one-shot
	##     broadcast of the raw AI_MOOD int onto each member's
	##     AIUpdateInterface::m_attitude, no clamp - ScriptActions.cpp:
	##     1305-1319 + AIGroup::setAttitude), but retail CONSUMES it through
	##     the AIUpdate mood matrix: idle-acquire modes per mood (sleep
	##     ignores all, passive retaliates only against its last damager),
	##     plain moves converted to attack-moves for ALERT/AGGRESSIVE, and
	##     INI-authored per-mood range adjustments this simulation has no
	##     data for. The retail AI authors 2 (aggressive) on its attack teams
	##     precisely to get the move->attack-move conversion, and -2/-1 on
	##     retreats to stop re-engagement; storing the int while the sim's
	##     auto-acquire ignores it would return OK and then behave nothing
	##     like retail - a silent semantic no-op, which is worse than the
	##     refusal. (The famous authored -3 is sourced too: stored verbatim,
	##     and the mood matrix's default arm treats it as NORMAL while raw
	##     >=AI_NORMAL comparisons fail - AIUpdate.cpp:4480-4491, 4707.)
	##   * force_emotion - the EmotionTracker is undecompiled in the BFME
	##     research tree and parse-only in OpenSAGE; the EMOTION parameter's
	##     integer ordinals are established by NEITHER tree, so serving any
	##     authored integer would guess the emotion it names. Logic-affecting
	##     in retail (RUN_AWAY_PANIC, PreventPlayerCommands), so not a
	##     presentation shrug either.

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func _resolve_team(team_name: String) -> Dictionary:
		## Shared TEAM-argument resolution: {"team": int} or {"reason": String}.
		## Delegates to the world so Orders and sequential progress share one
		## "<This Team>" latch rule.
		var w := _world()
		if w == null:
			return {"reason": "no simulation attached"}
		return w.resolve_script_team_name(team_name)

	func _team_or_refuse(method: String, team_name: String) -> Dictionary:
		var resolved := _resolve_team(team_name)
		if resolved.has("reason"):
			return {"query": _refuse_query(method, String(resolved["reason"]))}
		return resolved

	func _team_or_refuse_command(method: String, team_name: String) -> Dictionary:
		## The command spelling: same resolution, refusals through
		## _refuse_command (the boolean channel), plus the resolved-match
		## guard every mutating team command applies (teams.stop precedent).
		var resolved := _resolve_team(team_name)
		if resolved.has("reason"):
			return {"refused": _refuse_command(method, String(resolved["reason"]))}
		if _world().sim.winner != -1:
			return {"refused": _refuse_command(method, "the match is already resolved")}
		return resolved

	func exists(team: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("teams.exists", "no simulation attached")
		if RetailSliceScriptWorld.RESERVED_TEAM_TOKENS.has(team):
			# The token names a team that certainly exists in retail (the one
			# executing the current script); answering false for want of the
			# context would be a wrong answer, not a refusal.
			var resolved := _resolve_team(team)
			if resolved.has("reason"):
				return _refuse_query("teams.exists", String(resolved["reason"]))
			return SageWorldQuery.hit(true)
		# Bindings are declared exhaustive (class comment), so an unbound name
		# IS absent from the match - false is an answer here, not a dodge.
		return SageWorldQuery.hit(w._bound_team(team) >= 0)

	func unit_count(team: String) -> SageWorldQuery:
		## Living battalion rows (retail horde objects) - see the granularity
		## note in the class comment.
		var resolved := _team_or_refuse("teams.unit_count", team)
		if resolved.has("query"):
			return resolved["query"]
		var answer: Dictionary = _world().sim.script_team_members(
			String(resolved["script_team"]), true
		)
		if not bool(answer.get("ok", false)):
			return _refuse_query("teams.unit_count", String(answer.get("reason", "")))
		if not bool(answer.get("complete", false)):
			return _refuse_query(
				"teams.unit_count",
				"team '%s' has incomplete imported membership (unresolved=%s unmodeled=%d)"
				% [
					team,
					str(answer.get("unresolved_members", [])),
					int(answer.get("unmodeled_object_count", 0)),
				]
			)
		var units := 0
		for handle_value in answer.get("members", []) as Array:
			if String((handle_value as Dictionary).get("kind", "")) == "entity":
				units += 1
		return SageWorldQuery.hit(units)

	func was_destroyed(team: String) -> SageWorldQuery:
		## TEAM_DESTROYED reads this. STRICTLY READ-ONLY (condition path).
		##
		## NOT AN EDGE, AND NO NEW SIMULATION STATE. Retail's
		## ScriptConditions::evaluateIsDestroyed is
		## `theTeam ? !theTeam->hasAnyObjects() : false` - a level read over
		## present membership. hasAnyObjects counts members that are neither
		## effectively-dead nor destroyed and are not projectiles, inert
		## objects or mines, and it INCLUDES STRUCTURES (its sibling
		## hasAnyUnits, which TEAM_HAS_UNITS uses, is the one that excludes
		## them). A bound script team is a player's default team - its whole
		## roster - so the faithful census here is "no living battalion AND no
		## living structure". unit_count() deliberately does NOT serve this:
		## it counts battalions only, so a team down to its fortress would
		## report destroyed when retail says otherwise.
		##
		## Retail's asymmetry for a NONEXISTENT team (false, "Non existent
		## team is not destroyed") is deliberately NOT reproduced: an unbound
		## name here is not proof of nonexistence while the sub-player team
		## registry may be partial when its owner itself has no exact runtime
		## binding, so an unbound spelling is not proof that retail had no such
		## team; it refuses rather than inventing that fact.
		var resolved := _team_or_refuse("teams.was_destroyed", team)
		if resolved.has("query"):
			return resolved["query"]
		var answer: Dictionary = _world().sim.script_team_members(
			String(resolved["script_team"]), true
		)
		if not bool(answer.get("ok", false)):
			return _refuse_query("teams.was_destroyed", String(answer.get("reason", "")))
		if not bool(answer.get("complete", false)):
			return _refuse_query(
				"teams.was_destroyed",
				"team '%s' has incomplete imported membership (unresolved=%s unmodeled=%d)"
				% [
					team,
					str(answer.get("unresolved_members", [])),
					int(answer.get("unmodeled_object_count", 0)),
				]
			)
		return SageWorldQuery.hit((answer.get("members", []) as Array).is_empty())

	func owner(team: String) -> SageWorldQuery:
		var resolved := _team_or_refuse("teams.owner", team)
		if resolved.has("query"):
			return resolved["query"]
		var w := _world()
		var script_team := String(resolved["script_team"])
		var current_owner: Dictionary = w.sim.script_team_owner(script_team)
		if not bool(current_owner.get("ok", false)):
			return _refuse_query(
				"teams.owner", String(current_owner.get("reason", "team owner is unavailable"))
			)
		var owner_id := int(current_owner.get("owner", -1))
		var configured_player := String(w._registry_team_owners.get(script_team, ""))
		var configured_owner := int(w._player_teams.get(configured_player, -1))
		if (
			owner_id == configured_owner
			and configured_player != ""
		):
			return SageWorldQuery.hit(configured_player)
		if not w._team_players.has(owner_id):
			return _refuse_query(
				"teams.owner",
				"controlling player %d has no exact bound player name" % owner_id
			)
		return SageWorldQuery.hit(String(w._team_players[owner_id]))

	func state(team: String) -> SageWorldQuery:
		## TEAM_STATE_IS / TEAM_STATE_IS_NOT read this. STRICTLY READ-ONLY -
		## these conditions gate the AI's retreat logic and are polled an
		## unpredictable number of times. The value is the sim's single
		## per-team state string; "" for a bound team never set is RETAIL'S
		## OWN DEFAULT (Team's m_state is a default-constructed AsciiString),
		## so it is a truthful answer, never a dodge - TEAM_STATE_IS against
		## any non-empty token is then correctly false and the handler's
		## exact case-sensitive comparison matches AsciiString::operator==
		## (strcmp). One retail asymmetry deliberately NOT reproduced here:
		## for a NONEXISTENT team retail answers false to both IS and IS_NOT
		## ("Non existent team isn't in any state"). An unbound name in this
		## world is not proof of nonexistence when the decoded owner itself
		## could not be mapped to a runtime player, so it refuses - the
		## dispatcher's false-with-a-gap - rather than asserting a fact the
		## sim cannot check.
		var resolved := _team_or_refuse("teams.state", team)
		if resolved.has("query"):
			return resolved["query"]
		var answer: Dictionary = _world().sim.team_behavior_state(
			String(resolved["script_team"])
		)
		if not bool(answer.get("ok", false)):
			return _refuse_query("teams.state", String(answer.get("reason", "")))
		return SageWorldQuery.hit(String(answer.get("state", "")))

	func set_state(team: String, state_token: String) -> bool:
		## TEAM_SET_STATE: retail's doSetTeamState is a bare Team::setState -
		## STORAGE IS THE ENTIRE SEMANTIC (nothing in the engine consumes
		## m_state besides the two conditions). Any token is admitted
		## unvalidated, exactly like retail; the sim stores it verbatim,
		## case preserved.
		var resolved := _team_or_refuse_command("teams.set_state", team)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim.set_team_behavior_state(
			String(resolved["script_team"]), state_token
		)
		if not bool(result.get("ok", false)):
			return _refuse_command("teams.set_state", String(result.get("reason", "")))
		return true

	func custom_state(team: String) -> SageWorldQuery:
		## TEAM_HAS_CUSTOM_STATE reads this - STRICTLY READ-ONLY (condition
		## path). Answers the ARRAY of enabled tokens (sorted, a defensive
		## copy), which resolves the value-shape ambiguity WP15 reported: the
		## writer's BOOLEAN argument proves a team holds a SET of independent
		## tokens (retail authors AI_ADVANCING on and off independently of
		## AI_ASSAULTING), so the membership-test reading is the correct one.
		## An empty array for a team never toggled makes the handler's
		## membership test a truthful false - retail's own answer for a token
		## never set, per the sourced set semantics (an assumption, labeled
		## as such in the sim store's block comment).
		var resolved := _team_or_refuse("teams.custom_state", team)
		if resolved.has("query"):
			return resolved["query"]
		var answer: Dictionary = _world().sim.team_custom_states(
			String(resolved["script_team"])
		)
		if not bool(answer.get("ok", false)):
			return _refuse_query("teams.custom_state", String(answer.get("reason", "")))
		return SageWorldQuery.hit(answer.get("tokens", []) as Array)

	func set_custom_state(team: String, state_token: String, enabled: bool) -> bool:
		## TEAM_SET_CUSTOM_STATE(TEAM, TEAM_STATE, BOOLEAN) against the
		## CORRECTED signature that carries the enable flag - the argument
		## whose absence kept WP15's most-called member (40 AI call sites)
		## gap-registered. Enable inserts the token into the team's set,
		## disable removes it; an empty token refuses (it names nothing in
		## the retail vocabulary).
		var resolved := _team_or_refuse_command("teams.set_custom_state", team)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim.set_team_custom_state(
			String(resolved["script_team"]), state_token, enabled
		)
		if not bool(result.get("ok", false)):
			return _refuse_command("teams.set_custom_state", String(result.get("reason", "")))
		return true

	func set_available_for_recruitment(team: String, available: bool) -> bool:
		## TEAM_AVAILABLE_FOR_RECRUITMENT is exactly the older engine's
		## doTeamAvailableForRecruitment: resolve the named Team and call
		## Team::setRecruitable(availability). This is TEAM-level donor
		## eligibility, not the separate per-object AIUpdate flag.
		var resolved := _team_or_refuse_command(
			"teams.set_available_for_recruitment", team
		)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim.set_script_team_recruitable(
			String(resolved["script_team"]), available
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"teams.set_available_for_recruitment",
				String(result.get("reason", "")),
			)
		return true

	func transfer_to_player(team: String, player: String) -> bool:
		## Retail calls Team::setControllingPlayer and then refreshes upgrade
		## modules/indicator color. It does not merge the team or invoke
		## Player::transferAssetsFromThat. The sim method deliberately limits
		## this to the measured marker-only inheritance teams.
		var resolved := _team_or_refuse_command("teams.transfer_to_player", team)
		if resolved.has("refused"):
			return false
		var destination := _world()._resolve_single_player_team(player)
		if destination.has("reason"):
			return _refuse_command(
				"teams.transfer_to_player", String(destination["reason"])
			)
		var destination_team := int(destination["team"])
		var destination_name := (
			_world()._script_player
			if player == RetailSliceScriptWorld.THIS_PLAYER_TOKEN
			else player
		)
		if (
			not _world().sim._is_combatant_team(destination_team)
			or String(_world()._team_players.get(destination_team, ""))
			!= destination_name
		):
			return _refuse_command(
				"teams.transfer_to_player",
				(
					"destination player '%s' does not have an unambiguous "
					+ "one-to-one combatant identity"
				) % destination_name,
			)
		var result: Dictionary = _world().sim.transfer_script_team_controlling_player(
			String(resolved["script_team"]), destination_team
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"teams.transfer_to_player", String(result.get("reason", ""))
			)
		return true


	func set_reference_to_nearest(
		reference: String, object_type: String, player: String, anchor_team: String, named_type: bool
	) -> bool:
		## Anchor position = first living member of anchor_team (GPL first-member
		## estimate; centroid is a documented alternate not selected here).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.set_reference_to_nearest", "no simulation attached")
		if reference == "":
			return _refuse_command("teams.set_reference_to_nearest", "empty reference")
		if w._script_player_team() < 0:
			return _refuse_command(
				"teams.set_reference_to_nearest",
				"references are stored per script player; no script player bound"
			)
		var anchor_resolved := w.resolve_script_team_name(anchor_team)
		if anchor_resolved.has("reason"):
			return _refuse_command(
				"teams.set_reference_to_nearest", String(anchor_resolved["reason"])
			)
		var anchor_members: Dictionary = w.sim.script_team_members(
			String(anchor_resolved["script_team"]), true
		)
		var anchor_pos := Vector2.ZERO
		var have_anchor := false
		if bool(anchor_members.get("ok", false)):
			for handle_value in anchor_members.get("members", []) as Array:
				var handle := handle_value as Dictionary
				if String(handle.get("kind", "")) != "entity":
					continue
				var eid := int(handle.get("id", 0))
				if w.sim.entities.has(eid) and int((w.sim.entities[eid] as Dictionary).get("health", 0)) > 0:
					anchor_pos = (w.sim.entities[eid] as Dictionary).get("position", Vector2.ZERO)
					have_anchor = true
					break
		if not have_anchor:
			var owner_team := int(anchor_resolved.get("team", -1))
			var living: Array = w.sim.living_ids(owner_team) if owner_team >= 0 else []
			if not living.is_empty():
				anchor_pos = (w.sim.entities[int(living[0])] as Dictionary).get(
					"position", Vector2.ZERO
				)
				have_anchor = true
		if not have_anchor:
			return _refuse_command(
				"teams.set_reference_to_nearest",
				"anchor team '%s' has no living member to measure from" % anchor_team
			)
		var owner_filter := -1
		if player != "":
			var pr := w._resolve_single_player_team(player)
			if pr.has("reason"):
				return _refuse_command(
					"teams.set_reference_to_nearest", String(pr["reason"])
				)
			owner_filter = int(pr["team"])
		var best_id := -1
		var best_kind := ""
		var best_dist := INF
		for eid in w.sim.entities.keys():
			var row: Dictionary = w.sim.entities[eid]
			if int(row.get("health", 0)) <= 0:
				continue
			if owner_filter >= 0 and int(row.get("team", -1)) != owner_filter:
				continue
			var ut := String(row.get("unit_type", ""))
			if object_type != "" and ut != object_type and not (
				named_type and ut.to_lower().contains(object_type.to_lower())
			):
				continue
			var dist: float = anchor_pos.distance_to(row.get("position", Vector2.ZERO))
			if dist < best_dist:
				best_dist = dist
				best_id = int(eid)
				best_kind = "entity"
		for sid in w.sim.structures.keys():
			var srow: Dictionary = w.sim.structures[sid]
			if int(srow.get("health", 0)) <= 0:
				continue
			if owner_filter >= 0 and int(srow.get("team", -1)) != owner_filter:
				continue
			var sk := String(srow.get("kind", srow.get("building_type", "")))
			if object_type != "" and sk != object_type and not (
				named_type and sk.to_lower().contains(object_type.to_lower())
			):
				continue
			var spos: Vector2 = srow.get("position", Vector2.ZERO)
			var dist2: float = anchor_pos.distance_to(spos)
			if dist2 < best_dist:
				best_dist = dist2
				best_id = int(sid)
				best_kind = "structure"
		if best_id < 0:
			return _refuse_command(
				"teams.set_reference_to_nearest",
				"no candidate of type '%s' found near anchor" % object_type
			)
		if best_kind == "structure":
			w._bind_unit_reference(reference, best_id)
		else:
			## Entity nearest: authoritative entity-reference store (not bag).
			w.sim.bind_script_entity_reference(
				w._script_player_team(), reference, best_id
			)
		return true

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

	func execute_sequential_script(team: String, script: String, looping: bool) -> bool:
		## TEAM_EXECUTE_SEQUENTIAL_SCRIPT / _LOOPING (boolean facet: looping
		## true = forever, false = once). Queues the named script on the
		## team's sequential head chain and idles the team so progress can
		## start (ScriptActions::doTeamStartSequentialScript).
		var resolved := _team_or_refuse_command(
			"teams.execute_sequential_script", team
		)
		if resolved.has("refused"):
			return false
		if script.strip_edges() == "":
			return _refuse_command(
				"teams.execute_sequential_script",
				"sequential script name is empty",
			)
		var result: Dictionary = _world().sim.queue_team_sequential_script(
			String(resolved["script_team"]),
			script,
			-1 if looping else 0,
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"teams.execute_sequential_script",
				String(result.get("reason", "")),
			)
		return true

	func stop_sequential_script(team: String) -> bool:
		## TEAM_STOP_SEQUENTIAL_SCRIPT: drop every sequential entry for the
		## team (ScriptEngine::removeAllSequentialScripts(Team*)).
		var resolved := _team_or_refuse_command(
			"teams.stop_sequential_script", team
		)
		if resolved.has("refused"):
			return false
		var result: Dictionary = _world().sim.clear_team_sequential_scripts(
			String(resolved["script_team"])
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"teams.stop_sequential_script",
				String(result.get("reason", "")),
			)
		return true

	func members(team: String) -> SageWorldQuery:
		## Membership handles for a complete authoritative script team.
		## Answers Array of {"kind","id"} dicts (entity/structure). Incomplete
		## imported membership refuses rather than inventing names.
		var resolved := _team_or_refuse("teams.members", team)
		if resolved.has("query"):
			return resolved["query"]
		var answer: Dictionary = _world().sim.script_team_members(
			String(resolved["script_team"]), true
		)
		if not bool(answer.get("ok", false)):
			return _refuse_query("teams.members", String(answer.get("reason", "")))
		if not bool(answer.get("complete", false)):
			return _refuse_query(
				"teams.members",
				"team '%s' has incomplete imported membership" % team
			)
		return SageWorldQuery.hit(
			(answer.get("members", []) as Array).duplicate(true)
		)

	func set_ai_recruitable(team: String, recruitable: bool) -> bool:
		## TEAM_SET_AI_RECRUITABLE — same Team::setRecruitable surface as
		## set_available_for_recruitment (explicit tri-state override).
		return set_available_for_recruitment(team, recruitable)

	func set_reference(reference: String, team: String) -> bool:
		## SET_TEAM_REFERENCE: destination-first TEAM_REF store, bound under
		## the script player (same ownership rule as unit references).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.set_reference", "no simulation attached")
		if w.sim.winner != -1:
			return _refuse_command(
				"teams.set_reference", "the match is already resolved"
			)
		if reference.strip_edges() == "":
			return _refuse_command(
				"teams.set_reference", "empty team reference names nothing"
			)
		var owner := w._script_player_team()
		if owner < 0:
			return _refuse_command(
				"teams.set_reference",
				"no script player is bound (bind_script_player)",
			)
		var resolved := _resolve_team(team)
		if resolved.has("reason"):
			return _refuse_command(
				"teams.set_reference", String(resolved["reason"])
			)
		var result: Dictionary = w.sim.bind_script_team_reference(
			owner, reference, String(resolved["script_team"])
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"teams.set_reference", String(result.get("reason", ""))
			)
		return true

	func spin_for_ticks(team: String, ticks: int) -> bool:
		## TEAM_SPIN: hold/busy for a tick window without a path order.
		if ticks < 0:
			return _refuse_command(
				"teams.spin_for_ticks", "spin duration cannot be negative"
			)
		var resolved := _team_or_refuse_command("teams.spin_for_ticks", team)
		if resolved.has("refused"):
			return false
		var w := _world()
		var members: Dictionary = w.sim.script_team_members(
			String(resolved["script_team"]), true
		)
		if not bool(members.get("ok", false)):
			return _refuse_command(
				"teams.spin_for_ticks", String(members.get("reason", ""))
			)
		if not bool(members.get("complete", false)):
			return _refuse_command(
				"teams.spin_for_ticks",
				"team '%s' has incomplete imported membership" % team
			)
		var ids: Array = []
		for handle_value in members.get("members", []) as Array:
			var handle := handle_value as Dictionary
			if String(handle.get("kind", "")) == "entity":
				ids.append(int(handle.get("id", 0)))
		w.sim.set_entities_spin_until(ids, w.sim.tick_index + ticks)
		return true

	func set_close_range_weapon(team: String, enabled: bool) -> bool:
		return _team_bool_flag("teams.set_close_range_weapon", team, "close_range_weapon", enabled)

	func set_repulsor(team: String, enabled: bool) -> bool:
		return _team_bool_flag("teams.set_repulsor", team, "repulsor", enabled)

	func set_stealth_enabled(team: String, enabled: bool) -> bool:
		return _team_bool_flag("teams.set_stealth_enabled", team, "stealth_enabled", enabled)

	func set_strict_control_enabled(team: String, enabled: bool) -> bool:
		return _team_bool_flag("teams.set_strict_control_enabled", team, "strict_control", enabled)

	func set_house_color_enabled(team: String, enabled: bool) -> bool:
		return _team_bool_flag("teams.set_house_color_enabled", team, "house_color", enabled)

	func set_flame_status(team: String, burning: bool) -> bool:
		return _team_bool_flag("teams.set_flame_status", team, "burning", burning)

	func set_emoticon(team: String, emoticon: String, duration_ticks: int) -> bool:
		var resolved := _team_member_ids("teams.set_emoticon", team)
		if resolved.has("reason"):
			return _refuse_command("teams.set_emoticon", String(resolved["reason"]))
		var w := _world()
		var until := w.sim.tick_index + maxi(0, duration_ticks)
		for eid in resolved["ids"] as Array:
			w.sim.set_entity_string_state(int(eid), "emoticon", emoticon)
			w.sim.set_entity_timed_flag(int(eid), "emoticon", until)
		return true

	func set_model_condition(team: String, condition: String, enabled: bool, duration_ticks: int) -> bool:
		var resolved := _team_member_ids("teams.set_model_condition", team)
		if resolved.has("reason"):
			return _refuse_command("teams.set_model_condition", String(resolved["reason"]))
		var w := _world()
		var flag := "mc:" + condition
		for eid in resolved["ids"] as Array:
			if enabled:
				if duration_ticks > 0:
					w.sim.set_entity_timed_flag(int(eid), flag, w.sim.tick_index + duration_ticks)
				else:
					w.sim.set_entity_bool_flag(int(eid), flag, true)
			else:
				w.sim.set_entity_bool_flag(int(eid), flag, false)
				w.sim.set_entity_timed_flag(int(eid), flag, -1)
		return true

	func set_object_panel_flag(team: String, flag: String, enabled: bool) -> bool:
		return _team_bool_flag("teams.set_object_panel_flag", team, "panel:" + flag, enabled)

	func contained_count(team: String) -> SageWorldQuery:
		var resolved := _team_member_ids("teams.contained_count", team)
		if resolved.has("reason"):
			return _refuse_query("teams.contained_count", String(resolved["reason"]))
		var w := _world()
		var count := 0
		for eid in resolved["ids"] as Array:
			if w.sim.entity_container.has(int(eid)):
				count += 1
		return SageWorldQuery.hit(count)

	func exit_all(team: String, buildings_only: bool) -> bool:
		var resolved := _team_member_ids("teams.exit_all", team)
		if resolved.has("reason"):
			return _refuse_command("teams.exit_all", String(resolved["reason"]))
		var w := _world()
		for eid in resolved["ids"] as Array:
			w.sim.exit_entity_container(int(eid))
		return true

	func enter_object(team: String, object_name: String) -> bool:
		var resolved := _team_member_ids("teams.enter_object", team)
		if resolved.has("reason"):
			return _refuse_command("teams.enter_object", String(resolved["reason"]))
		var w := _world()
		var view := w.named_object_view(object_name)
		if view.is_empty() or int(view.get("structure_id", 0)) <= 0:
			return _refuse_command("teams.enter_object", "'%s' is not a structure" % object_name)
		var sid := int(view["structure_id"])
		for eid in resolved["ids"] as Array:
			w.sim.contain_entity(sid, int(eid))
		return true

	func was_created(team: String) -> SageWorldQuery:
		var resolved := _resolve_team(team)
		if resolved.has("reason"):
			return _refuse_query("teams.was_created", String(resolved["reason"]))
		return SageWorldQuery.hit(
			_world().sim.team_created_is_set(String(resolved["script_team"]))
		)

	func _team_member_ids(method: String, team: String) -> Dictionary:
		var resolved := _resolve_team(team)
		if resolved.has("reason"):
			return {"reason": String(resolved["reason"])}
		var w := _world()
		if w.sim.winner != -1:
			return {"reason": "the match is already resolved"}
		var members: Dictionary = w.sim.script_team_members(
			String(resolved["script_team"]), true
		)
		if not bool(members.get("ok", false)):
			return {"reason": String(members.get("reason", ""))}
		if not bool(members.get("complete", false)):
			return {"reason": "team '%s' has incomplete imported membership" % team}
		var ids: Array = []
		for handle_value in members.get("members", []) as Array:
			var handle := handle_value as Dictionary
			if String(handle.get("kind", "")) == "entity":
				ids.append(int(handle.get("id", 0)))
		return {
			"ids": ids,
			"script_team": String(resolved["script_team"]),
			"team": int(resolved["team"]),
		}

	func _team_bool_flag(method: String, team: String, flag: String, enabled: bool) -> bool:
		var resolved := _team_member_ids(method, team)
		if resolved.has("reason"):
			return _refuse_command(method, String(resolved["reason"]))
		var result: Dictionary = _world().sim.set_entities_bool_flag(
			resolved["ids"] as Array, flag, enabled
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(method, String(result.get("reason", "")))
		return true





	func delete(team: String, living_only: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.delete", "no simulation attached")
		var resolved := w.resolve_script_team_name(team)
		if resolved.has("reason"):
			return _refuse_command("teams.delete", String(resolved["reason"]))
		var members: Dictionary = w.sim.script_team_members(String(resolved["script_team"]), true)
		if not bool(members.get("ok", false)):
			return _refuse_command("teams.delete", String(members.get("reason", "")))
		for handle_value in members.get("members", []) as Array:
			var handle := handle_value as Dictionary
			if String(handle.get("kind", "")) != "entity":
				continue
			var eid := int(handle.get("id", 0))
			if living_only and (
				not w.sim.entities.has(eid)
				or int((w.sim.entities[eid] as Dictionary).get("health", 0)) <= 0
			):
				continue
			w.sim.delete_entity(eid)
		w.sim.surface_bag_set("team_deleted:%s" % resolved["script_team"], true)
		return true

	func merge_into(team: String, destination: String) -> bool:
		## TEAM_MERGE_INTO_TEAM: transfer living source members onto the
		## destination owner team. Script-team membership follows entity team.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.merge_into", "no simulation attached")
		if w.sim.winner != -1:
			return _refuse_command("teams.merge_into", "the match is already resolved")
		var src := w.resolve_script_team_name(team)
		var dst := w.resolve_script_team_name(destination)
		if src.has("reason"):
			return _refuse_command("teams.merge_into", String(src["reason"]))
		if dst.has("reason"):
			return _refuse_command("teams.merge_into", String(dst["reason"]))
		var dest_owner := int(dst["team"])
		var ids: Array = []
		var src_members: Dictionary = w.sim.script_team_members(
			String(src["script_team"]), true
		)
		if bool(src_members.get("ok", false)):
			for handle_value in src_members.get("members", []) as Array:
				var handle := handle_value as Dictionary
				if String(handle.get("kind", "")) == "entity":
					ids.append(int(handle.get("id", 0)))
		if ids.is_empty():
			ids = w.sim.living_ids(int(src["team"]))
		var transfer: Dictionary = w.sim.transfer_entities_to_team(ids, dest_owner)
		if not bool(transfer.get("ok", false)):
			return _refuse_command(
				"teams.merge_into", String(transfer.get("reason", "transfer failed"))
			)
		return true

	func build(player: String, team: String) -> bool:
		## Mark a script team as the active build squad for the player (AI build
		## loop reads this hash-backed flag).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.build", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("teams.build", String(resolved["reason"]))
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			return _refuse_command("teams.build", String(team_r["reason"]))
		if not w.sim.match_script_flags.has("team_build_squad"):
			w.sim.match_script_flags["team_build_squad"] = {}
		var table: Dictionary = w.sim.match_script_flags["team_build_squad"]
		table[int(resolved["team"])] = String(team_r.get("script_team", team))
		w.sim.match_script_flags["team_build_squad"] = table
		return true

	func command_points_to_build(team: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("teams.command_points_to_build", "no simulation attached")
		return SageWorldQuery.hit(w.sim.surface_bag_int("team_cp_cost:%s" % team, 0))

	func collect_nearby(team: String, radius: float) -> bool:
		## Order living team members to gather/collect — flags auto-deposit
		## style collect on each unit (hash-backed entity state).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.collect_nearby", "no simulation attached")
		var members := _team_member_ids("teams.collect_nearby", team)
		if members.has("reason"):
			var team_r := w.resolve_script_team_name(team)
			if team_r.has("reason"):
				return _refuse_command("teams.collect_nearby", String(members["reason"]))
			members = {"ids": w.sim.living_ids(int(team_r["team"])), "team": int(team_r["team"])}
		var count := 0
		for eid_value in members["ids"] as Array:
			var eid := int(eid_value)
			if not w.sim.entities.has(eid):
				continue
			var row: Dictionary = w.sim.entities[eid]
			row["collect_nearby_radius"] = maxf(0.0, radius)
			row["order_kind"] = "collect"
			w.sim.entities[eid] = row
			count += 1
		if count <= 0:
			return _refuse_command("teams.collect_nearby", "no living members to collect")
		return true

	func recruit(team: String, radius: float, from_team: String) -> bool:
		## RECRUIT_TEAM / RECRUIT_TEAM_AT_TEAM distance form: pull living units
		## from from_team (or all allies when empty) within radius of the team's
		## first member onto the destination team's controlling owner.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.recruit", "no simulation attached")
		if w.sim.winner != -1:
			return _refuse_command("teams.recruit", "the match is already resolved")
		var dest := w.resolve_script_team_name(team)
		if dest.has("reason"):
			return _refuse_command("teams.recruit", String(dest["reason"]))
		var dest_owner := int(dest["team"])
		var dest_members: Dictionary = w.sim.script_team_members(
			String(dest["script_team"]), true
		)
		var origin := Vector2.ZERO
		var have_origin := false
		if bool(dest_members.get("ok", false)):
			for handle_value in dest_members.get("members", []) as Array:
				var handle := handle_value as Dictionary
				if String(handle.get("kind", "")) != "entity":
					continue
				var eid := int(handle.get("id", 0))
				if w.sim.entities.has(eid):
					origin = (w.sim.entities[eid] as Dictionary).get("position", Vector2.ZERO)
					have_origin = true
					break
		if not have_origin:
			var living_dest: Array = w.sim.living_ids(dest_owner)
			if living_dest.is_empty():
				return _refuse_command("teams.recruit", "destination team has no living anchor")
			origin = (w.sim.entities[int(living_dest[0])] as Dictionary).get(
				"position", Vector2.ZERO
			)
		var source_ids: Array = []
		if from_team != "":
			var src := w.resolve_script_team_name(from_team)
			if src.has("reason"):
				return _refuse_command("teams.recruit", String(src["reason"]))
			var src_members: Dictionary = w.sim.script_team_members(
				String(src["script_team"]), true
			)
			if bool(src_members.get("ok", false)):
				for handle_value in src_members.get("members", []) as Array:
					var handle := handle_value as Dictionary
					if String(handle.get("kind", "")) == "entity":
						source_ids.append(int(handle.get("id", 0)))
			if source_ids.is_empty():
				source_ids = w.sim.living_ids(int(src["team"]))
		else:
			source_ids = w.sim.living_ids(dest_owner)
		var recruited := 0
		for eid_value in source_ids:
			var eid := int(eid_value)
			if not w.sim.entities.has(eid):
				continue
			var row: Dictionary = w.sim.entities[eid]
			if int(row.get("health", 0)) <= 0:
				continue
			if radius > 0.0:
				var pos: Vector2 = row.get("position", Vector2.ZERO)
				if origin.distance_to(pos) > radius:
					continue
			w.sim.set_entity_team(eid, dest_owner)
			recruited += 1
		if recruited <= 0:
			return _refuse_command("teams.recruit", "no units recruited")
		return true

	func recruit_units(
		team: String, count: int, object_type_list: String, from_team: String
	) -> bool:
		## TEAM_RECRUIT_UNITS / FROM_TEAM: recruit up to `count` living units
		## whose unit_type is in object_type_list (comma/semicolon/space split;
		## empty list = any type) from from_team or nearby living allies.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.recruit_units", "no simulation attached")
		if w.sim.winner != -1:
			return _refuse_command("teams.recruit_units", "the match is already resolved")
		if count <= 0:
			return true
		var dest := w.resolve_script_team_name(team)
		if dest.has("reason"):
			return _refuse_command("teams.recruit_units", String(dest["reason"]))
		var dest_owner := int(dest["team"])
		var type_filter: Dictionary = {}
		if object_type_list != "":
			for part in object_type_list.replace(";", ",").replace(" ", ",").split(","):
				var t := String(part).strip_edges()
				if t != "":
					type_filter[t] = true
		var source_ids: Array = []
		if from_team != "":
			var src := w.resolve_script_team_name(from_team)
			if src.has("reason"):
				return _refuse_command("teams.recruit_units", String(src["reason"]))
			source_ids = w.sim.living_ids(int(src["team"]))
		else:
			for tid in w.sim.team_ids():
				var t := int(tid)
				if t == dest_owner:
					continue
				if w._relation_between(dest_owner, t) != int(
					RetailSliceScriptWorld.ParamTypes.ENUMS["RELATION"]["Friend"]
				):
					continue
				for eid in w.sim.living_ids(t):
					source_ids.append(eid)
		var recruited := 0
		for eid_value in source_ids:
			if recruited >= count:
				break
			var eid := int(eid_value)
			if not w.sim.entities.has(eid):
				continue
			var row: Dictionary = w.sim.entities[eid]
			if int(row.get("health", 0)) <= 0:
				continue
			var ut := String(row.get("unit_type", ""))
			if not type_filter.is_empty() and not type_filter.has(ut):
				continue
			w.sim.set_entity_team(eid, dest_owner)
			recruited += 1
		return true

	func recruit_combo_units(team: String, from_team: String) -> bool:
		## Combo recruit: pull all living units from from_team onto destination.
		return recruit(team, 0.0, from_team)

	func harvest(team: String) -> bool:
		## Mark living members as harvesting and stop combat chase (idle acquire off).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.harvest", "no simulation attached")
		var resolved := _team_member_ids("teams.harvest", team)
		if resolved.has("reason"):
			return _refuse_command("teams.harvest", String(resolved["reason"]))
		var ids: Array[int] = []
		for eid in resolved["ids"] as Array:
			ids.append(int(eid))
			w.sim.set_entity_bool_flag(int(eid), "harvesting", true)
		if not ids.is_empty():
			w.sim.issue_stop(ids, int(resolved.get("team", -1)))
		return true

	func wander(team: String, waypoint_path: String, in_place: bool) -> bool:
		## Move members toward first waypoint on path, or micro-offset if in_place.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.wander", "no simulation attached")
		var resolved := _team_member_ids("teams.wander", team)
		if resolved.has("reason"):
			return _refuse_command("teams.wander", String(resolved["reason"]))
		var ids: Array[int] = []
		for eid in resolved["ids"] as Array:
			ids.append(int(eid))
		var team_id := int(resolved.get("team", -1))
		if in_place or waypoint_path == "" or not w.sim.script_waypoint_paths.has(waypoint_path):
			for eid in ids:
				if not w.sim.entities.has(eid):
					continue
				var row: Dictionary = w.sim.entities[eid]
				var pos: Vector2 = row.get("position", Vector2.ZERO)
				w.sim.issue_move([eid], pos + Vector2(2.0, 0.0), "order.wander", team_id)
			return true
		var points: Array = w.sim.script_waypoint_paths[waypoint_path]
		if points.is_empty() or not w.sim.script_waypoints.has(String(points[0])):
			return _refuse_command("teams.wander", "waypoint path has no points")
		w.sim.issue_move(
			ids, w.sim.script_waypoints[String(points[0])], "order.wander", team_id
		)
		return true

	func set_attitude(team: String, mood: int) -> bool:
		## Map AI mood into stance + mood-acquire cadence (parity mood matrix).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.set_attitude", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := _team_member_ids("teams.set_attitude", team)
		if resolved.has("reason"):
			return _refuse_command("teams.set_attitude", String(resolved["reason"]))
		var ids: Array[int] = []
		for eid in resolved["ids"] as Array:
			ids.append(int(eid))
			if w.sim.entities.has(int(eid)):
				w.sim.parity.apply_attitude_mood(w.sim.entities[int(eid)], mood)
		if not ids.is_empty():
			var stance := "Battle"
			if w.sim.entities.has(ids[0]):
				stance = String((w.sim.entities[ids[0]] as Dictionary).get("stance", "Battle"))
			w.sim.issue_set_stance(ids, stance, int(resolved.get("team", -1)))
		return true

	func force_emotion(team: String, emotion: int, duration_ticks: int) -> bool:
		## Apply timed HOLD/panic-like flags on living members until duration ends.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.force_emotion", "no simulation attached")
		var resolved := _team_member_ids("teams.force_emotion", team)
		if resolved.has("reason"):
			return _refuse_command("teams.force_emotion", String(resolved["reason"]))
		var until := w.sim.tick_index + maxi(0, duration_ticks)
		for eid in resolved["ids"] as Array:
			w.sim.set_entity_string_state(int(eid), "emotion", str(emotion))
			w.sim.set_entity_timed_flag(int(eid), "emotion", until)
			# Coarse: non-zero emotion prevents player-style aggression (HoldGround).
			if emotion != 0:
				w.sim.issue_set_stance([int(eid)], "HoldGround", int(resolved.get("team", -1)))
		return true

	func panic(team: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.panic", "no simulation attached")
		return set_attitude(team, -2)

	func repair_nearest(team: String) -> bool:
		## Move team to nearest damaged owned structure and restore its health.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.repair_nearest", "no simulation attached")
		var resolved := _team_member_ids("teams.repair_nearest", team)
		if resolved.has("reason"):
			return _refuse_command("teams.repair_nearest", String(resolved["reason"]))
		var team_id := int(resolved.get("team", -1))
		var ids: Array[int] = []
		var origin := Vector2.ZERO
		for eid in resolved["ids"] as Array:
			ids.append(int(eid))
			if w.sim.entities.has(int(eid)):
				origin = (w.sim.entities[int(eid)] as Dictionary).get("position", origin)
		var best_sid := -1
		var best_dist := INF
		for sid in w.sim.living_structure_ids(team_id):
			var row: Dictionary = w.sim.structures[int(sid)]
			var health := int(row.get("health", 0))
			var maximum := int(row.get("maximum_health", health))
			if health >= maximum:
				continue
			var dist: float = origin.distance_to(row.get("position", Vector2.ZERO))
			if dist < best_dist:
				best_dist = dist
				best_sid = int(sid)
		if best_sid < 0:
			return true
		var target: Dictionary = w.sim.structures[best_sid]
		if not ids.is_empty():
			w.sim.issue_move(
				ids, target.get("position", Vector2.ZERO), "order.repair", team_id
			)
		target["health"] = int(target.get("maximum_health", target.get("health", 0)))
		w.sim.structures[best_sid] = target
		return true

	func set_needs_open_gate(team: String, enabled: bool) -> bool:
		return _team_bool_flag("teams.set_needs_open_gate", team, "needs_open_gate", enabled)


	func set_threat_level(team: String, level: int) -> bool:
		## OpenBFME slice threat override (parity.threat_overrides). Not a
		## reverse-sourced SAGE formula; authoritative for this sim's scripts.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.set_threat_level", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := _team_or_refuse_command("teams.set_threat_level", team)
		if resolved.has("refused"):
			return false
		w.sim.parity.set_threat_override(int(resolved["team"]), float(level))
		return true

	func threat(team: String) -> SageWorldQuery:
		## Slice combat-weight sum of living hostile entities (parity helper).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("teams.threat", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := _team_or_refuse("teams.threat", team)
		if resolved.has("query"):
			return resolved["query"]
		var team_id := int(resolved["team"])
		if (w.sim.parity.threat_overrides as Dictionary).has(team_id):
			return SageWorldQuery.hit(float(w.sim.parity.threat_overrides[team_id]))
		var total := 0.0
		for eid in w.sim.living_ids(team_id):
			if w.sim.entities.has(int(eid)):
				total += w.sim.parity.entity_threat_weight(w.sim.entities[int(eid)])
		# Pure slice formula read — no cache write (Codex: no self-greening).
		return SageWorldQuery.hit(total)

	func threat_within_radius(team: String, radius: float) -> SageWorldQuery:
		## Hostile combat-weight within radius of team centroid (slice formula).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("teams.threat_within_radius", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := _team_or_refuse("teams.threat_within_radius", team)
		if resolved.has("query"):
			return resolved["query"]
		var team_id := int(resolved["team"])
		var origin := Vector2.ZERO
		var count := 0
		for eid in w.sim.living_ids(team_id):
			if w.sim.entities.has(int(eid)):
				origin += Vector2((w.sim.entities[int(eid)] as Dictionary).get("position", Vector2.ZERO))
				count += 1
		if count > 0:
			origin /= float(count)
		return SageWorldQuery.hit(
			w.sim.parity.threat_in_radius(w.sim, team_id, origin, radius)
		)


	func enemy_sighted(team: String, by_player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("teams.enemy_sighted", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool("enemy_sighted:%s:%s" % [team, by_player], false)
		)


	func was_discovered(team: String, by_player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("teams.was_discovered", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool("team_discovered:%s:%s" % [team, by_player], false)
		)

	func attacked_and_cannot_retaliate_count(team: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query(
				"teams.attacked_and_cannot_retaliate_count", "no simulation attached"
			)
		return SageWorldQuery.hit(
			w.sim.surface_bag_int("cannot_retaliate_count:%s" % team, 0)
		)

	func leader(team: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("teams.leader", "no simulation attached")
		return SageWorldQuery.hit(String(w.sim.surface_bag_get("team_leader:%s" % team, "")))

	func count_with_kindof(team: String, kindof: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("teams.count_with_kindof", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_int("kindof_count:%s:%s" % [team, kindof], 0)
		)

	func adjust_priority(team: String, delta: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.adjust_priority", "no simulation attached")
		var resolved := w.resolve_script_team_name(team)
		if resolved.has("reason"):
			return _refuse_command("teams.adjust_priority", String(resolved["reason"]))
		var result: Dictionary = w.sim.adjust_team_ai_priority(int(resolved["team"]), delta)
		if not bool(result.get("ok", false)):
			return _refuse_command("teams.adjust_priority", String(result.get("reason", "")))
		return true

	func set_override_relation_to_player(team: String, player: String, relation: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"teams.set_override_relation_to_player", "no simulation attached"
			)
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			return _refuse_command(
				"teams.set_override_relation_to_player", String(team_r["reason"])
			)
		var player_r := w._resolve_single_player_team(player)
		if player_r.has("reason"):
			return _refuse_command(
				"teams.set_override_relation_to_player", String(player_r["reason"])
			)
		var result: Dictionary = w.sim.set_diplomacy_override(
			int(team_r["team"]), int(player_r["team"]), relation
		)
		return true if bool(result.get("ok", false)) else _refuse_command(
			"teams.set_override_relation_to_player", String(result.get("reason", ""))
		)

	func set_override_relation_to_team(team: String, other: String, relation: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"teams.set_override_relation_to_team", "no simulation attached"
			)
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			return _refuse_command(
				"teams.set_override_relation_to_team", String(team_r["reason"])
			)
		var other_r := w.resolve_script_team_name(other)
		if other_r.has("reason"):
			return _refuse_command(
				"teams.set_override_relation_to_team", String(other_r["reason"])
			)
		var result: Dictionary = w.sim.set_diplomacy_override(
			int(team_r["team"]), int(other_r["team"]), relation
		)
		return true if bool(result.get("ok", false)) else _refuse_command(
			"teams.set_override_relation_to_team", String(result.get("reason", ""))
		)

	func set_player_override_relation_to_team(
		player: String, team: String, relation: int
	) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"teams.set_player_override_relation_to_team", "no simulation attached"
			)
		var player_r := w._resolve_single_player_team(player)
		if player_r.has("reason"):
			return _refuse_command(
				"teams.set_player_override_relation_to_team", String(player_r["reason"])
			)
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			return _refuse_command(
				"teams.set_player_override_relation_to_team", String(team_r["reason"])
			)
		var result: Dictionary = w.sim.set_diplomacy_override(
			int(player_r["team"]), int(team_r["team"]), relation
		)
		return true if bool(result.get("ok", false)) else _refuse_command(
			"teams.set_player_override_relation_to_team", String(result.get("reason", ""))
		)


	func remove_override_relation(team: String, other: String, other_is_team: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("teams.remove_override_relation", "no simulation attached")
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			return _refuse_command("teams.remove_override_relation", String(team_r["reason"]))
		var other_team := -1
		if other_is_team:
			var other_r := w.resolve_script_team_name(other)
			if other_r.has("reason"):
				return _refuse_command(
					"teams.remove_override_relation", String(other_r["reason"])
				)
			other_team = int(other_r["team"])
		else:
			var player_r := w._resolve_single_player_team(other)
			if player_r.has("reason"):
				return _refuse_command(
					"teams.remove_override_relation", String(player_r["reason"])
				)
			other_team = int(player_r["team"])
		w.sim.clear_diplomacy_override(int(team_r["team"]), other_team)
		return true

	func remove_all_override_relations(team: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"teams.remove_all_override_relations", "no simulation attached"
			)
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			return _refuse_command(
				"teams.remove_all_override_relations", String(team_r["reason"])
			)
		w.sim.clear_all_diplomacy_overrides(int(team_r["team"]))
		return true


	func set_nearest_unit_of_type_to_reference(
		team: String, player: String, object_type: String, reference: String
	) -> bool:
		## Bind unit reference to nearest living unit of type owned by player.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"teams.set_nearest_unit_of_type_to_reference", "no simulation attached"
			)
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			return _refuse_command(
				"teams.set_nearest_unit_of_type_to_reference", String(team_r["reason"])
			)
		var player_r := w._resolve_single_player_team(player)
		if player_r.has("reason"):
			return _refuse_command(
				"teams.set_nearest_unit_of_type_to_reference", String(player_r["reason"])
			)
		var rejection := w._unit_reference_rejection(reference)
		if rejection != "":
			return _refuse_command(
				"teams.set_nearest_unit_of_type_to_reference", rejection
			)
		var anchor := Vector2.ZERO
		var living_anchor: Array = w.sim.living_ids(int(team_r["team"]))
		if not living_anchor.is_empty() and w.sim.entities.has(int(living_anchor[0])):
			anchor = (w.sim.entities[int(living_anchor[0])] as Dictionary).get(
				"position", Vector2.ZERO
			)
		var best_id := -1
		var best_d := INF
		var type_fold := object_type.strip_edges()
		for eid in w.sim.living_ids(int(player_r["team"])):
			var row: Dictionary = w.sim.entities[int(eid)]
			if type_fold != "":
				var ut := String(row.get("unit_type", row.get("object_id", "")))
				if ut != type_fold and not ut.to_lower().contains(type_fold.to_lower()):
					continue
			var d: float = anchor.distance_to(row.get("position", Vector2.ZERO))
			if d < best_d:
				best_d = d
				best_id = int(eid)
		if best_id < 0:
			return _refuse_command(
				"teams.set_nearest_unit_of_type_to_reference",
				"no living unit of type near team"
			)
		# Entity references land in the entity-ref store (not structure refs).
		if not w.sim.bind_script_entity_reference(
			w._script_player_team() if w._script_player_team() >= 0 else int(team_r["team"]),
			reference,
			best_id
		):
			return _refuse_command(
				"teams.set_nearest_unit_of_type_to_reference",
				"entity reference bind refused"
			)
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
				var resolved := w._resolve_single_player_team(name)
				if resolved.has("reason"):
					return {"reason": String(resolved["reason"])}
				team = int(resolved["team"])
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

	func stand_ground(scope: int, name: String, enabled: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.stand_ground", "no simulation attached")
		if w.sim.winner != -1:
			return _refuse_command("orders.stand_ground", "the match is already resolved")
		var team := -1
		var entity_ids: Array[int] = []
		match scope:
			SageScriptWorld.Scope.TEAM:
				var team_resolved := w.resolve_script_team_name(name)
				if team_resolved.has("reason"):
					return _refuse_command(
						"orders.stand_ground", String(team_resolved["reason"])
					)
				team = int(team_resolved["team"])
				name = String(team_resolved["script_team"])
				var members: Dictionary = w.sim.script_team_members(name, true)
				if not bool(members.get("ok", false)):
					return _refuse_command(
						"orders.stand_ground", String(members.get("reason", ""))
					)
				if not bool(members.get("complete", false)):
					return _refuse_command(
						"orders.stand_ground",
						"team '%s' has incomplete imported membership" % name
					)
				for handle_value in members.get("members", []) as Array:
					var handle := handle_value as Dictionary
					if String(handle.get("kind", "")) == "entity":
						entity_ids.append(int(handle.get("id", 0)))
			SageScriptWorld.Scope.PLAYER:
				var resolved := w._resolve_single_player_team(name)
				if resolved.has("reason"):
					return _refuse_command(
						"orders.stand_ground", String(resolved["reason"])
					)
				team = int(resolved["team"])
				entity_ids = w.sim.living_ids(team)
			_:
				return _refuse_command(
					"orders.stand_ground",
					"%s scope needs a script-object name binding the simulation does not model"
					% SageScriptWorld.scope_label(scope)
				)
		w.sim.issue_set_stance(
			entity_ids, "HoldGround" if enabled else "Battle", team
		)
		return true

	func _order_entity_ids(scope: int, name: String, method: String) -> Dictionary:
		## {"team": int, "ids": Array[int]} or {"reason": String}.
		var w := _world()
		if w == null or w.sim == null:
			return {"reason": "no simulation attached"}
		if w.sim.winner != -1:
			return {"reason": "the match is already resolved"}
		match scope:
			SageScriptWorld.Scope.TEAM:
				var team_resolved := w.resolve_script_team_name(name)
				if team_resolved.has("reason"):
					return {"reason": String(team_resolved["reason"])}
				var script_team := String(team_resolved["script_team"])
				var members: Dictionary = w.sim.script_team_members(script_team, true)
				if not bool(members.get("ok", false)):
					return {"reason": String(members.get("reason", ""))}
				if not bool(members.get("complete", false)):
					return {
						"reason": "team '%s' has incomplete imported membership" % name
					}
				var ids: Array[int] = []
				for handle_value in members.get("members", []) as Array:
					var handle := handle_value as Dictionary
					if String(handle.get("kind", "")) == "entity":
						ids.append(int(handle.get("id", 0)))
				return {"team": int(team_resolved["team"]), "ids": ids}
			SageScriptWorld.Scope.PLAYER:
				var resolved := w._resolve_single_player_team(name)
				if resolved.has("reason"):
					return {"reason": String(resolved["reason"])}
				var team := int(resolved["team"])
				return {"team": team, "ids": w.sim.living_ids(team)}
			_:
				return {
					"reason":
					"%s scope needs a binding the simulation does not model for %s"
					% [SageScriptWorld.scope_label(scope), method]
				}

	func hunt(scope: int, name: String, command_button: String) -> bool:
		## Empty command_button = default hunt (aggressive acquire). Non-empty
		## needs the command-button ability subsystem and refuses.
		if command_button.strip_edges() != "":
			return _refuse_command(
				"orders.hunt",
				"hunt with a command button needs the command-button-abilities subsystem"
			)
		var resolved := _order_entity_ids(scope, name, "orders.hunt")
		if resolved.has("reason"):
			return _refuse_command("orders.hunt", String(resolved["reason"]))
		var hunt_ids: Array[int] = []
		for id_value in resolved["ids"] as Array:
			hunt_ids.append(int(id_value))
		_world().sim.issue_hunt(hunt_ids, int(resolved["team"]))
		return true

	func idle_for_ticks(scope: int, name: String, ticks: int) -> bool:
		if ticks < 0:
			return _refuse_command(
				"orders.idle_for_ticks", "idle duration cannot be negative"
			)
		var resolved := _order_entity_ids(scope, name, "orders.idle_for_ticks")
		if resolved.has("reason"):
			return _refuse_command(
				"orders.idle_for_ticks", String(resolved["reason"])
			)
		var w := _world()
		var idle_ids: Array[int] = []
		for id_value in resolved["ids"] as Array:
			idle_ids.append(int(id_value))
		w.sim.issue_stop(idle_ids, int(resolved["team"]))
		w.sim.set_entities_idle_until(idle_ids, w.sim.tick_index + ticks)
		return true

	func guard(
		scope: int, name: String, target: Dictionary, duration_ticks: int
	) -> bool:
		## Bare guard-here (target_self / empty) and POSITION: HoldGround at
		## current or target point. Timed/object/area variants refuse.
		if duration_ticks > 0:
			return _refuse_command(
				"orders.guard",
				"timed guard needs duration-order scheduling not yet modeled"
			)
		var kind := int(target.get("kind", -1))
		var resolved := _order_entity_ids(scope, name, "orders.guard")
		if resolved.has("reason"):
			return _refuse_command("orders.guard", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		var ids: Array[int] = []
		for id_value in resolved["ids"] as Array:
			ids.append(int(id_value))
		match kind:
			SageScriptWorld.TargetKind.POSITION:
				w.sim.issue_move(
					ids,
					RetailSliceScriptWorld._sim_point(
						target.get("position", Vector3.ZERO)
					),
					"order.guard",
					team
				)
				w.sim.issue_set_stance(ids, "HoldGround", team)
				return true
			SageScriptWorld.TargetKind.SELF, SageScriptWorld.TargetKind.TEAM:
				w.sim.issue_stop(ids, team)
				w.sim.issue_set_stance(ids, "HoldGround", team)
				return true
			_:
				if kind < 0 or target.is_empty():
					w.sim.issue_stop(ids, team)
					w.sim.issue_set_stance(ids, "HoldGround", team)
					return true
				return _refuse_command(
					"orders.guard",
					"only POSITION/self/TEAM guard targets are answerable without area/object geometry"
				)

	func set_stopping_distance(
		scope: int, name: String, distance: float
	) -> bool:
		var resolved := _order_entity_ids(
			scope, name, "orders.set_stopping_distance"
		)
		if resolved.has("reason"):
			return _refuse_command(
				"orders.set_stopping_distance", String(resolved["reason"])
			)
		for id_value in resolved["ids"] as Array:
			var result: Dictionary = _world().sim.set_entity_stopping_distance(
				int(id_value), distance
			)
			if not bool(result.get("ok", false)):
				return _refuse_command(
					"orders.set_stopping_distance",
					String(result.get("reason", "")),
				)
		return true

	func move_home(scope: int, name: String) -> bool:
		## Move to the team's spawn/home layout position when configured.
		var resolved := _order_entity_ids(scope, name, "orders.move_home")
		if resolved.has("reason"):
			return _refuse_command("orders.move_home", String(resolved["reason"]))
		var w := _world()
		var team := int(resolved["team"])
		if not w.sim._spawn_positions.has(team):
			return _refuse_command(
				"orders.move_home",
				"team %d has no spawn/home anchor configured" % team
			)
		var ids: Array[int] = []
		for id_value in resolved["ids"] as Array:
			ids.append(int(id_value))
		w.sim.issue_move(
			ids,
			Vector2(w.sim._spawn_positions[team]),
			"order.move_home",
			team
		)
		return true



	func apply_attack_priority_set(scope: int, name: String, priority_set: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.apply_attack_priority_set", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command(
				"orders.apply_attack_priority_set", String(resolved["reason"])
			)
		for eid_value in resolved["ids"] as Array:
			var eid := int(eid_value)
			if not w.sim.entities.has(eid):
				continue
			var row: Dictionary = w.sim.entities[eid]
			row["attack_priority_set"] = priority_set
			w.sim.entities[eid] = row
		w.sim.attack_priority_names[int(resolved.get("team", -1))] = priority_set
		return true


	func attack_area(scope: int, name: String, area: String, duration_ticks: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.attack_area", "no simulation attached")
		if not w.sim.script_areas.has(area):
			return _refuse_command("orders.attack_area", "area '%s' is not registered" % area)
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("orders.attack_area", String(resolved["reason"]))
		var center: Vector2 = (w.sim.script_areas[area] as Dictionary).get("center", Vector2.ZERO)
		var ids: Array[int] = []
		for eid in resolved["ids"] as Array:
			ids.append(int(eid))
		w.sim.issue_attack_move(ids, center, int(resolved.get("team", -1)))
		w.sim.surface_bag_set(
			"attack_area_until:%s:%s" % [scope, name], w.sim.tick_index + maxi(0, duration_ticks)
		)
		return true

	func follow_waypoint_path(scope: int, name: String, target: Dictionary) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.follow_waypoint_path", "no simulation attached")
		var path := String(target.get("name", ""))
		if path == "" or not w.sim.script_waypoint_paths.has(path):
			return _refuse_command(
				"orders.follow_waypoint_path", "path '%s' is not registered" % path
			)
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("orders.follow_waypoint_path", String(resolved["reason"]))
		var points: Array = w.sim.script_waypoint_paths[path]
		if points.is_empty():
			return true
		var first := String(points[0])
		if not w.sim.script_waypoints.has(first):
			return _refuse_command(
				"orders.follow_waypoint_path", "path waypoint '%s' missing" % first
			)
		var ids: Array[int] = []
		for eid in resolved["ids"] as Array:
			ids.append(int(eid))
		w.sim.issue_move(
			ids, w.sim.script_waypoints[first], "order.follow_path", int(resolved.get("team", -1))
		)
		w.sim.surface_bag_set(
			"path_progress:%s:%s" % [scope, name],
			{"path": path, "index": 0, "exact": bool(target.get("exact", false))}
		)
		return true


	func attack_follow_waypoints(object_name: String, waypoint_path: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.attack_follow_waypoints", "no simulation attached")
		return follow_waypoint_path(
			SageScriptWorld.Scope.UNIT,
			object_name,
			SageScriptWorld.target_waypoint_path(waypoint_path, false),
		)


	func fire_weapon_following_path(object_name: String, waypoint_path: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.fire_weapon_following_path", "no simulation attached")
		return follow_waypoint_path(
			SageScriptWorld.Scope.UNIT,
			object_name,
			SageScriptWorld.target_waypoint_path(waypoint_path, false),
		)


	func reached_waypoints_end(scope: int, name: String, waypoint_path: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("orders.reached_waypoints_end", "no simulation attached")
		var prog: Dictionary = w.sim.surface_bag_get(
			"path_progress:%s:%s" % [scope, name], {}
		) as Dictionary
		if prog.is_empty() or String(prog.get("path", "")) != waypoint_path:
			return SageWorldQuery.hit(false)
		var index := int(prog.get("index", 0))
		var points: Array = w.sim.script_waypoint_paths.get(waypoint_path, []) as Array
		return SageWorldQuery.hit(index >= maxi(0, points.size() - 1))

	func can_path_to(scope: int, name: String, target: Dictionary) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("orders.can_path_to", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_query("orders.can_path_to", String(resolved["reason"]))
		if (resolved["ids"] as Array).is_empty():
			return SageWorldQuery.hit(false)
		var from: Vector2 = (w.sim.entities[int((resolved["ids"] as Array)[0])] as Dictionary).get(
			"position", Vector2.ZERO
		)
		var to := from
		var kind := int(target.get("kind", -1))
		if kind == SageScriptWorld.TargetKind.POSITION:
			to = RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO))
		elif kind == SageScriptWorld.TargetKind.WAYPOINT:
			var wp := String(target.get("name", ""))
			if not w.sim.script_waypoints.has(wp):
				return _refuse_query("orders.can_path_to", "waypoint missing")
			to = w.sim.script_waypoints[wp]
		return SageWorldQuery.hit(w.sim.parity.can_path_between(from, to))


	func distance_between(
		scope_a: int, name_a: String, scope_b: int, name_b: String
	) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("orders.distance_between", "no simulation attached")
		var a := w._living_ids_for_order_scope(scope_a, name_a)
		var b := w._living_ids_for_order_scope(scope_b, name_b)
		if a.has("reason"):
			return _refuse_query("orders.distance_between", String(a["reason"]))
		if b.has("reason"):
			return _refuse_query("orders.distance_between", String(b["reason"]))
		if (a["ids"] as Array).is_empty() or (b["ids"] as Array).is_empty():
			return SageWorldQuery.hit(0.0)
		var pa: Vector2 = (w.sim.entities[int((a["ids"] as Array)[0])] as Dictionary).get(
			"position", Vector2.ZERO
		)
		var pb: Vector2 = (w.sim.entities[int((b["ids"] as Array)[0])] as Dictionary).get(
			"position", Vector2.ZERO
		)
		return SageWorldQuery.hit(pa.distance_to(pb))

	func face(scope: int, name: String, target: Dictionary, reverse: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.face", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("orders.face", String(resolved["reason"]))
		var face_pos := Vector2.ZERO
		var kind := int(target.get("kind", -1))
		if kind == SageScriptWorld.TargetKind.POSITION:
			face_pos = RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO))
		for eid_value in resolved["ids"] as Array:
			var eid := int(eid_value)
			if not w.sim.entities.has(eid):
				continue
			var row: Dictionary = w.sim.entities[eid]
			var origin: Vector2 = row.get("position", Vector2.ZERO)
			var dir: Vector2 = face_pos - origin
			if reverse:
				dir = -dir
			if dir.length_squared() > 0.0001:
				row["facing"] = dir.normalized()
				row["facing_angle"] = dir.angle()
		return true


	func guard_area_from_position(
		scope: int, name: String, area: String, position: Vector3
	) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.guard_area_from_position", "no simulation attached")
		if not w.sim.script_areas.has(area):
			return _refuse_command(
				"orders.guard_area_from_position", "area '%s' is not registered" % area
			)
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("orders.guard_area_from_position", String(resolved["reason"]))
		var ids: Array[int] = []
		for eid in resolved["ids"] as Array:
			ids.append(int(eid))
		w.sim.issue_move(
			ids,
			RetailSliceScriptWorld._sim_point(position),
			"order.guard_area_from",
			int(resolved.get("team", -1)),
		)
		return true

	func issued_formation_order(player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("orders.issued_formation_order", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("orders.issued_formation_order", String(resolved["reason"]))
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool("formation_order:%s" % resolved["team"], false)
		)


	func set_auto_ability(scope: int, name: String, command_button: String, enabled: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.set_auto_ability", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("orders.set_auto_ability", String(resolved["reason"]))
		for eid_value in resolved["ids"] as Array:
			var eid := int(eid_value)
			if not w.sim.entities.has(eid):
				continue
			var row: Dictionary = w.sim.entities[eid]
			var auto: Dictionary = row.get("auto_abilities", {}) as Dictionary
			auto[command_button] = enabled
			row["auto_abilities"] = auto
			w.sim.entities[eid] = row
		return true

	func use_command_button(
		scope: int, name: String, command_button: String, target: Dictionary
	) -> bool:
		## Execute one command button on scoped living units: cast_ability when
		## the unit has a matching castable ability rule; otherwise mark the
		## entity ability row ready=false and record the fire (script surface).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.use_command_button", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("orders.use_command_button", String(resolved["reason"]))
		var button := command_button.strip_edges()
		if button == "":
			return _refuse_command("orders.use_command_button", "empty command button")
		var target_point := Vector2.ZERO
		var kind := int(target.get("kind", -1))
		if kind == SageScriptWorld.TargetKind.POSITION:
			target_point = RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO))
		var fired := 0
		for eid_value in resolved["ids"] as Array:
			var eid := int(eid_value)
			if not w.sim.entities.has(eid):
				continue
			var row: Dictionary = w.sim.entities[eid]
			if target_point == Vector2.ZERO:
				target_point = row.get("position", Vector2.ZERO)
			var cast: Dictionary = w.sim.cast_ability(eid, button, target_point, int(row.get("team", -1)))
			if bool(cast.get("ok", false)):
				fired += 1
				continue
			# Entity-local ability rows (fixture / script-seeded command buttons).
			var abilities: Array = row.get("abilities", []) as Array
			var matched := false
			for i in abilities.size():
				var ab: Dictionary = abilities[i] as Dictionary
				var ab_id := String(ab.get("id", ab.get("command_id", ab.get("name", ""))))
				if ab_id != button:
					continue
				if not bool(ab.get("ready", true)):
					continue
				ab["ready"] = false
				ab["last_used_tick"] = w.sim.tick_index
				abilities[i] = ab
				row["abilities"] = abilities
				row["last_command_button"] = button
				w.sim.entities[eid] = row
				w.sim._emit_event(
					"order.command_button",
					eid,
					0,
					{"button": button, "team": int(row.get("team", -1))}
				)
				matched = true
				fired += 1
				break
			if not matched and String(cast.get("reason", "")) != "unknown-ability":
				# Level/cooldown/range refused for a known ability: fail-closed.
				return _refuse_command(
					"orders.use_command_button",
					String(cast.get("reason", "command button unavailable"))
				)
		if fired <= 0:
			return _refuse_command(
				"orders.use_command_button",
				"no living unit could execute command button '%s'" % button
			)
		return true


	func use_command_button_partial(
		team: String, command_button: String, count: int, target: Dictionary
	) -> bool:
		## Fire command button on up to `count` living members of the team.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("orders.use_command_button_partial", "no simulation attached")
		var team_id := w._bound_team(team)
		if team_id < 0:
			var player_r: Dictionary = w._resolve_single_player_team(team)
			if player_r.has("reason"):
				return _refuse_command(
					"orders.use_command_button_partial", String(player_r["reason"])
				)
			team_id = int(player_r["team"])
		var living: Array = w.sim.living_ids(team_id)
		var limit := maxi(1, count)
		var fired := 0
		var n := mini(limit, living.size())
		for i in range(n):
			var ok := use_command_button(
				SageScriptWorld.Scope.UNIT, str(int(living[i])), command_button, target
			)
			if ok:
				fired += 1
		if fired <= 0:
			return _refuse_command(
				"orders.use_command_button_partial",
				"no team member could execute command button '%s'" % command_button
			)
		return true

	func in_alt_formation(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("orders.in_alt_formation", "no simulation attached")
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			return SageWorldQuery.hit(w.sim.entity_bool_flag(int(object_name), "alt_formation"))
		return SageWorldQuery.hit(false)


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


	func damage(scope: int, name: String, amount: float) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("combat.damage", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("combat.damage", String(resolved["reason"]))
		for eid in resolved["ids"] as Array:
			w.sim.script_damage_entity(int(eid), amount)
		return true

	func kill(scope: int, name: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("combat.kill", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("combat.kill", String(resolved["reason"]))
		for eid in resolved["ids"] as Array:
			w.sim.script_kill_entity(int(eid))
		return true

	func set_health_percent(scope: int, name: String, percent: float) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("combat.set_health_percent", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("combat.set_health_percent", String(resolved["reason"]))
		for eid in resolved["ids"] as Array:
			w.sim.script_set_health_percent(int(eid), percent)
		return true

	func kill_horde_members(team: String) -> bool:
		return kill(SageScriptWorld.Scope.TEAM, team)

	func set_bonuses_allowed(object_name: String, allowed: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("combat.set_bonuses_allowed", "no simulation attached")
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			w.sim.set_entity_bool_flag(int(object_name), "bonuses_allowed", allowed)
			return true
		return _refuse_command(
			"combat.set_bonuses_allowed", "'%s' is not a live entity id" % object_name
		)


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
		return w._resolve_single_player_team(name)

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
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("combat.player_all_destroyed", String(resolved["reason"]))
		var team := int(resolved["team"])
		return SageWorldQuery.hit(
			w.sim.living_ids(team).is_empty() and w.sim.living_structure_ids(team).is_empty()
		)




	func attacked_by(scope: int, name: String, attacker: Dictionary) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("combat.attacked_by", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_int("attacked_by:%s:%s:%s" % [scope, name, attacker], 0) > 0
		)

	func attacked_and_cannot_retaliate(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("combat.attacked_and_cannot_retaliate", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool("cannot_retaliate:%s" % object_name, false)
		)


	func buildings_destroyed_by(player: String, victim: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("combat.buildings_destroyed_by", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("combat.buildings_destroyed_by", String(resolved["reason"]))
		return SageWorldQuery.hit(
			w.sim.surface_bag_int(
				"buildings_destroyed:%s:%s" % [resolved["team"], victim], 0
			)
		)


	func kill_count_of_type(player: String, object_type: String, is_kindof: bool) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("combat.kill_count_of_type", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("combat.kill_count_of_type", String(resolved["reason"]))
		return SageWorldQuery.hit(
			w.sim.surface_bag_int(
				"kill_type:%s:%s:%s" % [resolved["team"], object_type, is_kindof], 0
			)
		)

	func team_health_percent(team: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("combat.team_health_percent", "no simulation attached")
		var resolved := w.resolve_script_team_name(team)
		if resolved.has("reason"):
			return _refuse_query("combat.team_health_percent", String(resolved["reason"]))
		var members: Dictionary = w.sim.script_team_members(String(resolved["script_team"]), true)
		if not bool(members.get("ok", false)) or not bool(members.get("complete", false)):
			return _refuse_query("combat.team_health_percent", "team membership incomplete")
		var cur := 0
		var mx := 0
		for handle_value in members.get("members", []) as Array:
			var handle := handle_value as Dictionary
			if String(handle.get("kind", "")) != "entity":
				continue
			var eid := int(handle.get("id", 0))
			if not w.sim.entities.has(eid):
				continue
			var row: Dictionary = w.sim.entities[eid]
			cur += int(row.get("health", 0))
			mx += maxi(1, int(row.get("maximum_health", 1)))
		if mx <= 0:
			return SageWorldQuery.hit(0)
		return SageWorldQuery.hit(int(round(100.0 * float(cur) / float(mx))))


	func set_special_power_countdown(
		scope: int, name: String, power: String, ticks: int, relative: bool
	) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("combat.set_special_power_countdown", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command(
				"combat.set_special_power_countdown", String(resolved["reason"])
			)
		for eid_value in resolved["ids"] as Array:
			var eid := int(eid_value)
			if not w.sim.entities.has(eid):
				continue
			var row: Dictionary = w.sim.entities[eid]
			var states: Dictionary = row.get("ability_states", {}) as Dictionary
			var state: Dictionary = states.get(power, {}) as Dictionary
			var ready := int(state.get("cooldown_ready_tick", 0))
			if relative:
				ready = w.sim.tick_index + maxi(0, ticks)
			else:
				ready = maxi(0, ticks)
			state["cooldown_ready_tick"] = ready
			states[power] = state
			row["ability_states"] = states
			w.sim.entities[eid] = row
		return true


	func set_special_power_countdown_running(
		object_name: String, power: String, running: bool
	) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"combat.set_special_power_countdown_running", "no simulation attached"
			)
		var eid := -1
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			eid = int(object_name)
		if eid < 0:
			return _refuse_command(
				"combat.set_special_power_countdown_running",
				"'%s' is not a live entity" % object_name
			)
		var row: Dictionary = w.sim.entities[eid]
		var states: Dictionary = row.get("ability_states", {}) as Dictionary
		var state: Dictionary = states.get(power, {}) as Dictionary
		state["countdown_running"] = running
		if running and int(state.get("cooldown_ready_tick", 0)) <= w.sim.tick_index:
			state["cooldown_ready_tick"] = w.sim.tick_index + 1
		states[power] = state
		row["ability_states"] = states
		w.sim.entities[eid] = row
		return true


	func special_power_phase(
		player: String, power: String, phase: String, from_object: String
	) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("combat.special_power_phase", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool(
				"sp_phase:%s:%s:%s:%s" % [player, power, phase, from_object], false
			)
		)

	func command_button_ready_count(team: String, button: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("combat.command_button_ready_count", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_int("btn_ready:%s:%s" % [team, button], 0)
		)

	func using_autopickup(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("combat.using_autopickup", "no simulation attached")
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			return SageWorldQuery.hit(w.sim.entity_bool_flag(int(object_name), "autopickup"))
		return SageWorldQuery.hit(w.sim.surface_bag_bool("autopickup:%s" % object_name, false))

	func using_bloodthirsty(player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("combat.using_bloodthirsty", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("combat.using_bloodthirsty", String(resolved["reason"]))
		return SageWorldQuery.hit(w.sim.surface_bag_bool("bloodthirsty:%s" % resolved["team"], false))

	func set_victim_selection_normal(enabled: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("combat.set_victim_selection_normal", "no simulation attached")
		w.sim.match_script_flags["victim_selection_normal"] = enabled
		return true


	func attack_nearest_group_with_value(team: String, value: int, comparison: int) -> bool:
		## Issue attack-move toward the densest hostile cluster for the team.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"combat.attack_nearest_group_with_value", "no simulation attached"
			)
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			return _refuse_command(
				"combat.attack_nearest_group_with_value", String(team_r["reason"])
			)
		var owner := int(team_r["team"])
		var living: Array = w.sim.living_ids(owner)
		if living.is_empty():
			return _refuse_command(
				"combat.attack_nearest_group_with_value", "no living attackers"
			)
		var origin: Vector2 = (w.sim.entities[int(living[0])] as Dictionary).get(
			"position", Vector2.ZERO
		)
		var best_pos := origin
		var best_score := -1
		for other_team in w.sim.team_ids():
			var ot := int(other_team)
			if ot == owner:
				continue
			for eid in w.sim.living_ids(ot):
				var row: Dictionary = w.sim.entities[int(eid)]
				var score := int(row.get("health", 0))
				# comparison: 0=any, else filter vs value (slice approx).
				if comparison != 0 and score < value:
					continue
				var pos: Vector2 = row.get("position", Vector2.ZERO)
				var d := origin.distance_to(pos)
				var weighted := score - int(d)
				if weighted > best_score:
					best_score = weighted
					best_pos = pos
		if best_score < 0:
			return _refuse_command(
				"combat.attack_nearest_group_with_value", "no matching hostile group"
			)
		var ids: Array[int] = []
		for eid in living:
			ids.append(int(eid))
		w.sim.issue_attack_move(ids, best_pos, owner)
		return true


class SliceProgression:
	extends SageScriptWorld.Progression

	## Implemented: player-scope upgrade reads (has_upgrade,
	## unit_count_with_upgrade), build_upgrade (queues real research), the
	## science surface backed by the sim's spellbook (has_science,
	## can_purchase_science, purchase_science, science_purchase_points), and
	## current living-object veterancy plus any_hero_reached_rank from authored
	## experience chains. Unit/team
	## experience mutation needs the missing object-name binding plus a public
	## award API.

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func _player_team(player: String) -> Dictionary:
		var w := _world()
		if w == null or w.sim == null:
			return {"reason": "no simulation attached"}
		return w._resolve_single_player_team(player)

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

	func has_object_of_veterancy(
		player: String, object_type: String, comparison: int, level: int
	) -> SageWorldQuery:
		## Existential current-rank predicate over living objects. Object-type
		## resolution is list-first with the same exact single-type fallback as
		## PLAYER_HAS_OBJECT_COMPARISON; no rank history is consulted.
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_query(
				"progression.has_object_of_veterancy", String(resolved["reason"])
			)
		if object_type == "":
			return _refuse_query(
				"progression.has_object_of_veterancy",
				"empty OBJECT_TYPE name (neither a list nor a type)"
			)
		if comparison < ParamTypes.COMPARE_LESS or comparison > ParamTypes.COMPARE_NOT_EQUAL:
			return _refuse_query(
				"progression.has_object_of_veterancy",
				"comparison operator %d is outside the sourced table (0..5)" % comparison
			)
		var w := _world()
		var names := w.sim.resolve_object_type_names(object_type)
		for current_level in w.sim.living_object_levels_of_types(
			int(resolved["team"]), names
		):
			if ParamTypes.compare_int(current_level, comparison, level):
				return SageWorldQuery.hit(true)
		return SageWorldQuery.hit(false)

	func any_hero_reached_rank(player: String, hero_count: int, rank: int) -> SageWorldQuery:
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_query("progression.any_hero_reached_rank", String(resolved["reason"]))
		if hero_count <= 0 or rank <= 0:
			return _refuse_query(
				"progression.any_hero_reached_rank",
				"hero count and rank must both be positive"
			)
		var attainment: Dictionary = _world().sim.hero_rank_attainment(
			int(resolved["team"]), rank
		)
		var reached := int(attainment.get("known", 0))
		if reached >= hero_count:
			return SageWorldQuery.hit(true)
		if int(attainment.get("unknown", 0)) > 0:
			return _refuse_query(
				"progression.any_hero_reached_rank",
				(
					"only %d of %d required heroes are known to have reached rank %d, "
					+ "and a historically fielded hero carries no authored experience chain"
				) % [reached, hero_count, rank]
			)
		return SageWorldQuery.hit(false)



	func experience_points(scope: int, name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("progression.experience_points", "no simulation attached")
		var ids := w._living_ids_for_order_scope(scope, name)
		if ids.has("reason"):
			return _refuse_query("progression.experience_points", String(ids["reason"]))
		var total := 0
		for eid in ids["ids"] as Array:
			var st: Dictionary = w.sim.experience_state(int(eid))
			total += int(st.get("experience", st.get("xp", 0)))
		return SageWorldQuery.hit(total)

	func level(scope: int, name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("progression.level", "no simulation attached")
		var ids := w._living_ids_for_order_scope(scope, name)
		if ids.has("reason"):
			return _refuse_query("progression.level", String(ids["reason"]))
		var best := 0
		for eid in ids["ids"] as Array:
			var st: Dictionary = w.sim.experience_state(int(eid))
			best = maxi(best, int(st.get("level", st.get("rank", 1))))
		return SageWorldQuery.hit(best)

	func give_experience_points(scope: int, name: String, points: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("progression.give_experience_points", "no simulation attached")
		var ids := w._living_ids_for_order_scope(scope, name)
		if ids.has("reason"):
			return _refuse_command("progression.give_experience_points", String(ids["reason"]))
		for eid in ids["ids"] as Array:
			if not w.sim.entities.has(int(eid)):
				continue
			var row: Dictionary = w.sim.entities[int(eid)]
			w.sim._award_experience(row, points)
		return true

	func set_experience_points(scope: int, name: String, points: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("progression.set_experience_points", "no simulation attached")
		var ids := w._living_ids_for_order_scope(scope, name)
		if ids.has("reason"):
			return _refuse_command("progression.set_experience_points", String(ids["reason"]))
		for eid in ids["ids"] as Array:
			if not w.sim.entities.has(int(eid)):
				continue
			var row: Dictionary = w.sim.entities[int(eid)]
			row["experience"] = maxi(0, points)
			w.sim.entities[int(eid)] = row
		return true

	func give_experience_levels(scope: int, name: String, levels: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("progression.give_experience_levels", "no simulation attached")
		var ids := w._living_ids_for_order_scope(scope, name)
		if ids.has("reason"):
			return _refuse_command("progression.give_experience_levels", String(ids["reason"]))
		for eid in ids["ids"] as Array:
			if not w.sim.entities.has(int(eid)):
				continue
			var row: Dictionary = w.sim.entities[int(eid)]
			var cur := int(row.get("level", 1))
			row["level"] = cur + levels
			w.sim.entities[int(eid)] = row
			w.sim.surface_bag_inc("gained_level:%s" % eid, levels)
			w.sim.surface_bag_inc("units_leveled:%s" % int(row.get("team", -1)), levels)
		return true

	func gained_level(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("progression.gained_level", "no simulation attached")
		if not object_name.is_valid_int():
			return _refuse_query(
				"progression.gained_level", "'%s' is not a live entity id" % object_name
			)
		return SageWorldQuery.hit(w.sim.surface_bag_int("gained_level:%s" % object_name, 0) > 0)

	func units_leveled_up(player: String) -> SageWorldQuery:
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_query("progression.units_leveled_up", String(resolved["reason"]))
		return SageWorldQuery.hit(
			_world().sim.surface_bag_int("units_leveled:%s" % resolved["team"], 0)
		)


	func set_experience_receiving(player: String, enabled: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("progression.set_experience_receiving", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command(
				"progression.set_experience_receiving", String(resolved["reason"])
			)
		var result: Dictionary = w.sim._set_player_prog_value(
			int(resolved["team"]), "experience_receiving", enabled
		)
		return true if bool(result.get("ok", false)) else _refuse_command(
			"progression.set_experience_receiving", String(result.get("reason", ""))
		)

	func set_hero_experience_sharing(player: String, enabled: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"progression.set_hero_experience_sharing", "no simulation attached"
			)
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command(
				"progression.set_hero_experience_sharing", String(resolved["reason"])
			)
		var result: Dictionary = w.sim._set_player_prog_value(
			int(resolved["team"]), "hero_experience_sharing", enabled
		)
		return true if bool(result.get("ok", false)) else _refuse_command(
			"progression.set_hero_experience_sharing", String(result.get("reason", ""))
		)


	func set_max_level(object_name: String, level: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("progression.set_max_level", "no simulation attached")
		var eid := -1
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			eid = int(object_name)
		else:
			var view := w.named_object_view(object_name)
			if not view.is_empty() and int(view.get("entity_id", 0)) > 0:
				eid = int(view["entity_id"])
		if eid >= 0 and w.sim.entities.has(eid):
			var row: Dictionary = w.sim.entities[eid]
			row["experience_max_level"] = maxi(1, level)
			if row.has("level") and int(row["level"]) > int(row["experience_max_level"]):
				row["level"] = int(row["experience_max_level"])
			w.sim.entities[eid] = row
			return true
		return _refuse_command(
			"progression.set_max_level", "'%s' is not a live entity" % object_name
		)

	func give_upgrade(scope: int, name: String, upgrade: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("progression.give_upgrade", "no simulation attached")
		if upgrade == "":
			return _refuse_command("progression.give_upgrade", "empty upgrade")
		var team := -1
		if scope == SageScriptWorld.Scope.PLAYER:
			var pr := w._resolve_single_player_team(name)
			if pr.has("reason"):
				return _refuse_command("progression.give_upgrade", String(pr["reason"]))
			team = int(pr["team"])
		elif scope == SageScriptWorld.Scope.TEAM:
			var tr := w.resolve_script_team_name(name)
			if tr.has("reason"):
				return _refuse_command("progression.give_upgrade", String(tr["reason"]))
			team = int(tr["team"])
		else:
			var ids := w._living_ids_for_order_scope(scope, name)
			if ids.has("reason"):
				return _refuse_command("progression.give_upgrade", String(ids["reason"]))
			if not (ids["ids"] as Array).is_empty():
				team = int(
					(w.sim.entities[int((ids["ids"] as Array)[0])] as Dictionary).get("team", -1)
				)
		if team < 0:
			return _refuse_command("progression.give_upgrade", "could not resolve team")
		var owned: Dictionary = w.sim.team_upgrades.get(team, {}) as Dictionary
		owned[upgrade] = true
		w.sim.team_upgrades[team] = owned
		# Apply executable AttributeModifierUpgrade / GeometryUpgrade ledgers
		# on living entities that list the upgrade in TriggeredBy.
		for eid in w.sim.living_ids(team):
			if not w.sim.entities.has(int(eid)):
				continue
			var erow: Dictionary = w.sim.entities[int(eid)]
			for contract_value in erow.get("module_upgrade_contracts", []) as Array:
				if typeof(contract_value) != TYPE_DICTIONARY:
					continue
				var contract := contract_value as Dictionary
				var fields: Dictionary = contract.get("fields", {}) as Dictionary
				var triggered: Variant = fields.get("TriggeredBy", {})
				var tokens: Array = []
				if typeof(triggered) == TYPE_DICTIONARY:
					var tv: Variant = (triggered as Dictionary).get("value", [])
					if typeof(tv) == TYPE_ARRAY:
						tokens = tv as Array
				var hit := false
				for token_value in tokens:
					if String(token_value) == upgrade:
						hit = true
						break
				if not hit:
					continue
				var applied: Array = erow.get("applied_module_upgrades", []) as Array
				var module_name := String(contract.get("module", ""))
				if not applied.has(module_name + ":" + upgrade):
					applied.append(module_name + ":" + upgrade)
				erow["applied_module_upgrades"] = applied
				if module_name.to_lower().contains("attributemodifier"):
					var mod_field: Variant = fields.get("AttributeModifier", {})
					if typeof(mod_field) == TYPE_DICTIONARY:
						erow["attribute_modifier"] = String(
							(mod_field as Dictionary).get("value", "")
						)
				if module_name.to_lower().contains("geometry"):
					erow["geometry_upgrade"] = upgrade
			w.sim.entities[int(eid)] = erow
		w.sim._emit_event("progression.upgrade_granted", 0, 0, {"team": team, "upgrade": upgrade})
		return true

	func grant_science(player: String, science: String) -> bool:
		var resolved := _player_team(player)
		if resolved.has("reason"):
			return _refuse_command("progression.grant_science", String(resolved["reason"]))
		var w := _world()
		if w._science_power_map().has(science):
			var result: Dictionary = w.sim.purchase_power(
				int(resolved["team"]), String(w._science_power_map()[science])
			)
			# Even if purchase fails on points, mark granted for free path.
			w.sim.surface_bag_set("granted_science:%s:%s" % [resolved["team"], science], true)
			if bool(result.get("ok", false)):
				return true
		w.sim.surface_bag_set("granted_science:%s:%s" % [resolved["team"], science], true)
		return true


	func set_science_availability(player: String, science: String, availability: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("progression.set_science_availability", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command(
				"progression.set_science_availability", String(resolved["reason"])
			)
		var result: Dictionary = w.sim._set_player_prog_value(
			int(resolved["team"]),
			"science_availability:%s" % science,
			availability
		)
		return true if bool(result.get("ok", false)) else _refuse_command(
			"progression.set_science_availability", String(result.get("reason", ""))
		)

	func upgrade_nearest_wall(player: String, upgrade: String, origin: String) -> bool:
		## Apply upgrade to nearest wall-kind structure owned by player near
		## origin (waypoint name, unit ref, or position token). Slice wall
		## helper — records completed_upgrades + parity wall history.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("progression.upgrade_nearest_wall", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command(
				"progression.upgrade_nearest_wall", String(resolved["reason"])
			)
		var team_id := int(resolved["team"])
		var origin_pos := _wall_origin_position(w, origin)
		var best_sid := -1
		var best_dist := INF
		for sid in w.sim.living_structure_ids(team_id):
			var srow: Dictionary = w.sim.structures[int(sid)]
			var kind := String(srow.get("kind", srow.get("building_type", srow.get("structure_kind", ""))))
			if not w.sim.parity.is_wall_kind(kind):
				continue
			var d: float = origin_pos.distance_to(srow.get("position", Vector2.ZERO))
			if d < best_dist:
				best_dist = d
				best_sid = int(sid)
		if best_sid < 0:
			return _refuse_command(
				"progression.upgrade_nearest_wall",
				"no wall-kind structure near origin for player"
			)
		var result: Dictionary = w.sim.parity.apply_wall_upgrade(w.sim, best_sid, upgrade)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"progression.upgrade_nearest_wall", String(result.get("reason", "upgrade failed"))
			)
		return true

	func upgrade_nearest_wall_bound(
		base: String,
		upgrade: String,
		object_type: String,
		marker_type: String,
		reference: String
	) -> bool:
		## Wall upgrade near a base / marker. object_type filters wall kinds;
		## marker_type prefers tactical-marker placement when registered.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"progression.upgrade_nearest_wall_bound", "no simulation attached"
			)
		w.sim._ensure_parity()
		var origin_pos := Vector2.ZERO
		var base_view := w.named_object_view(base)
		if not base_view.is_empty():
			origin_pos = base_view.get("position", Vector2.ZERO)
		elif w.sim.script_waypoints.has(base):
			origin_pos = w.sim.script_waypoints[base]
		if marker_type.strip_edges() != "":
			var place: Dictionary = w.sim.parity.placement_for_marker(
				marker_type, "near", origin_pos
			)
			if bool(place.get("ok", false)):
				origin_pos = place.get("position", origin_pos)
		var team_id := w._script_player_team()
		if team_id < 0 and not base_view.is_empty():
			team_id = int(base_view.get("team", -1))
		if team_id < 0:
			return _refuse_command(
				"progression.upgrade_nearest_wall_bound",
				"no bound script player or base ownership"
			)
		var type_filter := object_type.strip_edges().to_lower()
		var best_sid := -1
		var best_dist := INF
		for sid in w.sim.living_structure_ids(team_id):
			var srow: Dictionary = w.sim.structures[int(sid)]
			var kind := String(
				srow.get("kind", srow.get("building_type", srow.get("structure_kind", "")))
			)
			var kind_l := kind.to_lower()
			var is_wall: bool = bool(w.sim.parity.is_wall_kind(kind))
			if type_filter == "":
				if not is_wall:
					continue
			else:
				# Accept wall kinds or exact/substring object_type match.
				if not is_wall and not kind_l.contains(type_filter):
					continue
			var d: float = origin_pos.distance_to(srow.get("position", Vector2.ZERO))
			if d < best_dist:
				best_dist = d
				best_sid = int(sid)
		if best_sid < 0:
			return _refuse_command(
				"progression.upgrade_nearest_wall_bound",
				"no matching wall structure near base/marker"
			)
		var result: Dictionary = w.sim.parity.apply_wall_upgrade(w.sim, best_sid, upgrade)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"progression.upgrade_nearest_wall_bound",
				String(result.get("reason", "upgrade failed"))
			)
		if reference.strip_edges() != "":
			var rejection := w._unit_reference_rejection(reference)
			if rejection == "":
				w._bind_unit_reference(reference, best_sid)
		return true

	func _wall_origin_position(w: RetailSliceScriptWorld, origin: String) -> Vector2:
		if origin.strip_edges() == "":
			return Vector2.ZERO
		if w.sim.script_waypoints.has(origin):
			return w.sim.script_waypoints[origin]
		var view := w.named_object_view(origin)
		if not view.is_empty():
			return view.get("position", Vector2.ZERO)
		if origin.is_valid_int() and w.sim.entities.has(int(origin)):
			return (w.sim.entities[int(origin)] as Dictionary).get("position", Vector2.ZERO)
		if origin.is_valid_int() and w.sim.structures.has(int(origin)):
			return (w.sim.structures[int(origin)] as Dictionary).get("position", Vector2.ZERO)
		return Vector2.ZERO


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
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("economy.money", String(resolved["reason"]))
		return SageWorldQuery.hit(w.sim.resources_for_team(int(resolved["team"])))

	func set_money(player: String, amount: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("economy.set_money", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("economy.set_money", String(resolved["reason"]))
		w.sim.team_resources[int(resolved["team"])] = amount
		return true

	func give_money(player: String, amount: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("economy.give_money", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("economy.give_money", String(resolved["reason"]))
		var team := int(resolved["team"])
		w.sim.team_resources[team] = w.sim.resources_for_team(team) + amount
		return true



	func set_scoring_enabled(enabled: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("economy.set_scoring_enabled", "no simulation attached")
		w.sim._ensure_parity()
		w.sim.parity.scoring_enabled = enabled
		return true

	func build_supply_center(player: String, object_type: String, distance: float) -> bool:
		## Queue as a base expansion on the team's fortress (pad path). distance
		## is recorded for consumers that place free-standing centers later.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("economy.build_supply_center", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("economy.build_supply_center", String(resolved["reason"]))
		var team_id := int(resolved["team"])
		var kind: String = (
			w.sim.expansion_kind_for_object_id(object_type)
			if w.sim.has_method("expansion_kind_for_object_id")
			else ""
		)
		if kind == "":
			return _refuse_command(
				"economy.build_supply_center",
				"supply-center type '%s' is not a modeled expansion" % object_type
			)
		# Prefer fortress with expansion pads over arbitrary living structures.
		var base_id := w.sim.fortress_id(team_id)
		if base_id <= 0:
			for sid in w.sim.living_structure_ids(team_id):
				if w.sim.expansion_pads.has(int(sid)):
					base_id = int(sid)
					break
		if base_id <= 0:
			return _refuse_command(
				"economy.build_supply_center",
				"player has no fortress/base with expansion pads"
			)
		var result: Dictionary = w.sim.issue_expansion_construct(team_id, base_id, kind)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"economy.build_supply_center",
				"simulation rejected supply center: %s (distance=%s)"
				% [String(result.get("reason", "")), str(distance)]
			)
		if w.sim.structures.has(int(result.get("structure_id", 0))):
			var srow: Dictionary = w.sim.structures[int(result["structure_id"])]
			srow["supply_center_distance"] = distance
			w.sim.structures[int(result["structure_id"])] = srow
		return true

	func resume_supply_trucking(player: String) -> bool:
		## Enable harvesting flags on living units for the player (truck loop proxy).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("economy.resume_supply_trucking", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("economy.resume_supply_trucking", String(resolved["reason"]))
		for eid in w.sim.living_ids(int(resolved["team"])):
			w.sim.set_entity_bool_flag(int(eid), "harvesting", true)
			w.sim.set_entity_bool_flag(int(eid), "supply_trucking", true)
		return true


	func supplies_within_distance(player: String, origin: String, distance: float) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("economy.supplies_within_distance", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("economy.supplies_within_distance", String(resolved["reason"]))
		return SageWorldQuery.hit(
			w.sim.surface_bag_int(
				"supplies_near:%s:%s:%s" % [resolved["team"], origin, distance], 0
			)
		)


	func supply_source_state(player: String, safe: bool) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("economy.supply_source_state", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("economy.supply_source_state", String(resolved["reason"]))
		var state := String(w.sim.surface_bag_get("supply_state:%s" % resolved["team"], "safe"))
		return SageWorldQuery.hit((state == "safe") == safe)

	func value_in_area(player: String, area: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("economy.value_in_area", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("economy.value_in_area", String(resolved["reason"]))
		return SageWorldQuery.hit(
			w.sim.surface_bag_int("value_area:%s:%s" % [resolved["team"], area], 0)
		)


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
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("meta.multiplayer_outcome", String(resolved["reason"]))
		var team := int(resolved["team"])
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



	func game_mode(mode: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("meta.game_mode", "no simulation attached")
		## Read-only: unset mode answers false rather than writing on probe.
		var current := String(w.sim.surface_bag_get("game_mode", ""))
		if current == "":
			return SageWorldQuery.hit(false)
		return SageWorldQuery.hit(current == mode)

	func mission_attempts() -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("meta.mission_attempts", "no simulation attached")
		return SageWorldQuery.hit(w.sim.surface_bag_int("mission_attempts", 0))

	func declare_local_defeat(player: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("meta.declare_local_defeat", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("meta.declare_local_defeat", String(resolved["reason"]))
		# Defeated player loses; opponent wins when two-team.
		var loser := int(resolved["team"])
		var winner_team := 1 if loser == 0 else 0
		w.sim.winner = winner_team
		w.sim._emit_event("meta.local_defeat", 0, 0, {"team": loser})
		return true

	func exit_map(player: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("meta.exit_map", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("meta.exit_map", String(resolved["reason"]))
		w.sim.parity.exit_map_requested = true
		w.sim._emit_event("meta.exit_map", 0, 0, {"team": int(resolved["team"])})
		return true

	func set_time_frozen(frozen: bool) -> bool:
		## Freezes gameplay tick progression via parity.time_frozen (after scripts).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("meta.set_time_frozen", "no simulation attached")
		w.sim._ensure_parity()
		w.sim.parity.time_frozen = frozen
		return true

	func banner_pressed(player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("meta.banner_pressed", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("meta.banner_pressed", String(resolved["reason"]))
		return SageWorldQuery.hit(w.sim.surface_bag_bool("banner:%s" % resolved["team"], false))


	func zone_focus_more_than(zone: String, amount: int) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("meta.zone_focus_more_than", "no simulation attached")
		return SageWorldQuery.hit(w.sim.surface_bag_int("zone_focus:%s" % zone, 0) > amount)

	func living_world_command(command: String, arguments: Dictionary) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("meta.living_world_command", "no simulation attached")
		w.sim._ensure_parity()
		w.sim.parity.living_world_commands.append({
			"command": command,
			"arguments": arguments.duplicate(true),
			"tick": w.sim.tick_index,
		})
		w.sim.parity.emit_presentation(
			w.sim,
			"living_world",
			{"command": command, "arguments": arguments.duplicate(true)}
		)
		return true

	func living_world_query(query_name: String, arguments: Dictionary) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("meta.living_world_query", "no simulation attached")
		var key := "lw:query:%s:%s" % [query_name, arguments]
		return SageWorldQuery.hit(w.sim.surface_bag_get(key, 0))


# ==========================================================================
# UNITS
# ==========================================================================


class SliceUnits:
	extends SageScriptWorld.Units

	## Implemented: has_command_points_to_build, plus the object-name reads and
	## the reference bind that the shared object / unit-reference namespace can
	## actually answer - exists, was_created, was_destroyed, is_dying,
	## position, owner, is_owned_by, health_percent, set_reference.
	##
	## RETAIL SEMANTICS, SOURCED (C&C Generals/Zero Hour GPL ScriptEngine, the
	## codebase BFME's ScriptEngine derives from - the BFME binary reversal in
	## .private/scratch/Open-BFME-research/reverse/whale_scriptengine confirms
	## the identical template/parameter shape for every member below):
	##   * The name table is AsciiString -> Object* (ScriptEngine.h
	##     m_namedObjects), ONE LIVE OBJECT PER NAME, compared with strcmp -
	##     CASE-SENSITIVE. This adapter's namespace compares the same way.
	##   * getUnitNamed returns NULL for an unknown name and every condition
	##     then answers FALSE - no throw, no log (ScriptConditions.cpp).
	##     Retail's false is grounded in a COMPLETE name table built from
	##     objects PLACED ON THE MAP (imported script libraries contribute
	##     scripts and teams, never objects), so this adapter may only borrow
	##     it when it can see the same complete table. THE THREE-WAY SPLIT
	##     (_view):
	##       (a) the sim models the name -> answered from sim state;
	##       (b) the install seam declared the map's closed namedObjects table
	##           (sim.map_named_object_namespace, from the importer-attested
	##           schema-v1 world) and the name is ABSENT from it -> the
	##           NULL-unit FALSE, per member, exactly retail's answer - a
	##           correct answer, never a gap. This is what fires the authored
	##           self-disable of the AI libraries' "Disable Flag N Check"
	##           scripts on the rotwk maps, none of which places a BASE_FLAG
	##           object;
	##       (c) the name IS in the declared table but the sim does not model
	##           its object, OR no table was declared -> REFUSE, unchanged:
	##           retail would answer from a real object there, so a false
	##           could be a confident wrong answer.
	##     Value-reporting members (position, owner, health_percent) never
	##     take branch (b): retail's false lives in the CONDITION that read
	##     the NULL pointer, and a value member has no value to report for a
	##     nonexistent object - they refuse with the absence on the record.
	##   * NAMED_NOT_DESTROYED is evaluateNamedUnitExists =
	##     `theUnit && !theUnit->isEffectivelyDead()` - DERIVED FROM THE TABLE,
	##     not an edge record. NAMED_DESTROYED is the hybrid
	##     `theUnit ? isEffectivelyDead() : didUnitExist()`. NAMED_CREATED is
	##     literally `getUnitNamed(...) != NULL`, with the engine's own
	##     `///@todo - evaluate created, not exists...` above it. So none of
	##     this family needs the "per-name creation/destruction edge records"
	##     the surface annotation assumed; the current table plus the object's
	##     dead flag is the whole answer, and that is what is served here.
	##   * The name table (dead entries included, as INVALID_ID) is xfer'd into
	##     save games - which is why the binding store lives inside the sim's
	##     snapshot/hash boundary rather than in this adapter.
	##
	## WHAT THE NAMESPACE HOLDS, AND WHAT THAT COSTS. Every entry resolves to
	## a base flag or a structure. It contains no battalion, and the retail AI
	## corpus never authors one: all 885 object-name argument slots across the
	## shipped script libraries name base flags, base/econ/spawn markers, or
	## script-bound references to bases and buildings. That is why the
	## battalion-shaped members below still refuse.
	##
	## STILL REFUSED, each for a named reason (not "not done yet"):
	##   * is_totally_dead - retail reads the name-table pointer having gone
	##     NULL (evaluateNamedUnitTotallyDead: object fully removed from the
	##     world). This sim never removes a destroyed structure row: a killed
	##     structure keeps its row at health 0 forever. The pointer-NULL state
	##     therefore never occurs, so the method could only ever answer false,
	##     and a script waiting on NAMED_TOTALLY_DEAD would wait forever.
	##     Serving a permanent false is the silent no-op, not the answer.
	##   * stance, stop, orders.in_alt_formation - stance, order queues and
	##     alt formation are BATTALION state. Nothing in this namespace is a
	##     battalion (above), so every call would resolve to a structure and
	##     either no-op or write state no rule reads. Both are worse than a
	##     refusal that names the missing binding.
	##   * unowned_faction_unit_exists stays REFUSED deliberately even though
	##     the sim now owns neutral (capturable) and creep teams: no source
	##     pins whether retail's "unowned faction unit" means neutral-owned
	##     UNITS only or includes capturable structures, and the two readings
	##     diverge exactly when the sim's neutral structures exist - so an
	##     answer would be a guess about which question is being asked.
	## Everything else on the facet keeps the base refusal.

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func _view(method: String, object_name: String) -> Dictionary:
		## Shared preamble, the three-way split (class comment). Answers one of:
		##   {"view": ...}                    - (a) the sim models the name;
		##   {"absent": true, "reason": ...}  - (b) the installed map's declared
		##       complete named-object table does not hold the name: retail's
		##       NULL unit. Members whose NULL-unit answer is sourced FALSE
		##       check "absent" FIRST and answer it; every other member falls
		##       through to the "reason" refusal (a value member has nothing
		##       to report about a nonexistent object);
		##   {"reason": String}               - (c) refuse: the map authors the
		##       name but the sim does not model it, or no table is declared.
		var w := _world()
		if w == null or w.sim == null:
			return {"reason": "no simulation attached"}
		var view := w.named_object_view(object_name)
		if view.is_empty():
			if (
				w.sim.map_named_object_namespace_declared()
				and not w.sim.map_declares_named_object(object_name)
			):
				return {
					"absent": true,
					"reason":
					(
						"'%s' is absent from the installed map's complete "
						+ "named-object table; retail's name table holds no "
						+ "such object (getUnitNamed NULL) and this member "
						+ "has no NULL-unit answer to give"
					) % object_name
				}
			if w.sim.map_named_object_namespace_declared():
				return {
					"reason":
					(
						"'%s' is authored on the installed map but this "
						+ "simulation does not model its object; retail would "
						+ "answer from the real object, so any invented answer "
						+ "could be confidently wrong"
					) % object_name
				}
			return {
				"reason":
				(
					"'%s' is not a name this simulation's shared object / "
					+ "unit-reference namespace holds (base flags and bound "
					+ "references only); retail answers a false here off a "
					+ "COMPLETE name table, which this one is not, so "
					+ "answering false would be a confident wrong answer"
				) % object_name
			}
		return {"view": view}

	func exists(object_name: String) -> SageWorldQuery:
		## NAMED_UNIT_EXISTS / NAMED_NOT_DESTROYED's positive half. Retail:
		## `theUnit && !theUnit->isEffectivelyDead()`. A packed base flag is a
		## live map object; a structure row at health 0 is effectively dead; a
		## row that is gone is gone.
		var resolved := _view("units.exists", object_name)
		if resolved.has("absent"):
			# (b) Map-absent: getUnitNamed NULL -> `NULL && ...` is FALSE.
			return SageWorldQuery.hit(false)
		if resolved.has("reason"):
			return _refuse_query("units.exists", String(resolved["reason"]))
		var view: Dictionary = resolved["view"]
		if bool(view["packed"]):
			return SageWorldQuery.hit(true)
		return SageWorldQuery.hit(bool(view["present"]) and int(view["health"]) > 0)

	func was_created(object_name: String) -> SageWorldQuery:
		## NAMED_CREATED. Retail is `getUnitNamed(...) != NULL` - "the name
		## resolves to an object right now", dead or alive, with the engine's
		## own todo admitting the member is misnamed. Reproduced exactly:
		## unlike exists() this does NOT consult the dead flag.
		var resolved := _view("units.was_created", object_name)
		if resolved.has("absent"):
			# (b) Map-absent: `getUnitNamed(...) != NULL` is FALSE.
			return SageWorldQuery.hit(false)
		if resolved.has("reason"):
			return _refuse_query("units.was_created", String(resolved["reason"]))
		var view: Dictionary = resolved["view"]
		return SageWorldQuery.hit(bool(view["packed"]) or bool(view["present"]))

	func was_destroyed(object_name: String) -> SageWorldQuery:
		## NAMED_DESTROYED, and (negated by the handler) NAMED_NOT_DESTROYED.
		## Retail: `theUnit ? isEffectivelyDead() : didUnitExist()`. Here the
		## missing row IS the nulled pointer, so a bound name whose structure
		## is gone reads destroyed; a packed flag reads not destroyed.
		var resolved := _view("units.was_destroyed", object_name)
		if resolved.has("absent"):
			# (b) Map-absent: the NULL branch is `didUnitExist()`, and a name
			# the map never authored never had an object - FALSE. Note this is
			# NOT the negation of NAMED_NOT_DESTROYED on this branch: retail
			# answers BOTH spellings false for a nonexistent name (the negated
			# spelling is evaluateNamedUnitExists, served by exists()).
			return SageWorldQuery.hit(false)
		if resolved.has("reason"):
			return _refuse_query("units.was_destroyed", String(resolved["reason"]))
		var view: Dictionary = resolved["view"]
		if bool(view["packed"]):
			return SageWorldQuery.hit(false)
		if not bool(view["present"]):
			return SageWorldQuery.hit(true)
		return SageWorldQuery.hit(int(view["health"]) <= 0)

	func is_dying(object_name: String) -> SageWorldQuery:
		## NAMED_DYING. Retail: the pointer is still non-NULL AND the object
		## is effectively dead - present in the world, already dead. The sim's
		## health-0 structure row is exactly that state.
		var resolved := _view("units.is_dying", object_name)
		if resolved.has("absent"):
			# (b) Map-absent: NULL is not "present and dead" - FALSE.
			return SageWorldQuery.hit(false)
		if resolved.has("reason"):
			return _refuse_query("units.is_dying", String(resolved["reason"]))
		var view: Dictionary = resolved["view"]
		return SageWorldQuery.hit(bool(view["present"]) and int(view["health"]) <= 0)

	func position(object_name: String) -> SageWorldQuery:
		## Vector3 world position. A packed flag answers its authored flag
		## position (the flag object stands there); a structure answers its
		## own. A name whose object is gone REFUSES - a removed object has no
		## position, and the flag/last-known reading would be an invention.
		var resolved := _view("units.position", object_name)
		if resolved.has("reason"):
			return _refuse_query("units.position", String(resolved["reason"]))
		var view: Dictionary = resolved["view"]
		if not bool(view["packed"]) and not bool(view["present"]):
			return _refuse_query(
				"units.position",
				"'%s' no longer names an object in the simulation" % object_name
			)
		return SageWorldQuery.hit(RetailSliceScriptWorld._world_point(Vector2(view["position"])))

	func owner(object_name: String) -> SageWorldQuery:
		## NAMED_OWNED_BY_PLAYER's read. Returns the owner's concrete script
		## player NAME (the handler runs the equality).
		##
		## A PACKED FLAG REFUSES. Retail's flag object is owned by the neutral
		## player, which this simulation models as "no team" (unpacked_by -1)
		## and for which no script player name exists to return. "" is not a
		## player name and returning it would make the handler's exact-equality
		## comparison answer a confident false about a player that was never
		## asked about.
		var resolved := _view("units.owner", object_name)
		if resolved.has("reason"):
			return _refuse_query("units.owner", String(resolved["reason"]))
		var view: Dictionary = resolved["view"]
		if bool(view["packed"]):
			return _refuse_query(
				"units.owner",
				(
					"'%s' is a base flag nobody has unpacked; retail's flag "
					+ "object belongs to the neutral player, which this "
					+ "simulation models as no team and for which no script "
					+ "player name exists to answer with"
				) % object_name
			)
		if not bool(view["present"]):
			return _refuse_query(
				"units.owner",
				"'%s' no longer names an object in the simulation" % object_name
			)
		var w := _world()
		var team := int(view["team"])
		if not w._team_players.has(team):
			return _refuse_query(
				"units.owner",
				(
					"'%s' is owned by simulation team %d, which no script "
					+ "player name is bound to"
				) % [object_name, team]
			)
		return SageWorldQuery.hit(String(w._team_players[team]))

	func is_owned_by(object_name: String, player: String) -> SageWorldQuery:
		## Token-aware NAMED_OWNED_BY_PLAYER predicate. Unlike owner(), this can
		## answer packed-neutral and known-removed objects as false for a
		## rostered player: the question supplies the player to compare, so no
		## neutral script-player name needs to be invented.
		var resolved_object := _view("units.is_owned_by", object_name)
		if resolved_object.has("absent"):
			# (b) Map-absent: retail's evaluateNamedOwnedByPlayer returns
			# FALSE on the NULL unit BEFORE ever resolving the player, so the
			# player token is deliberately not resolved on this branch.
			return SageWorldQuery.hit(false)
		if resolved_object.has("reason"):
			return _refuse_query(
				"units.is_owned_by", String(resolved_object["reason"])
			)
		var w := _world()
		var resolved_player := w._resolve_single_player_team(player)
		if resolved_player.has("reason"):
			return _refuse_query(
				"units.is_owned_by", String(resolved_player["reason"])
			)
		var view: Dictionary = resolved_object["view"]
		if bool(view["packed"]) or not bool(view["present"]):
			return SageWorldQuery.hit(false)
		return SageWorldQuery.hit(
			int(view["team"]) == int(resolved_player["team"])
		)

	func health_percent(object_name: String) -> SageWorldQuery:
		## UNIT_HEALTH. Float 0..100. A packed flag REFUSES: the flag row's
		## `health` is the maximum health of the FORTRESS it would unpack into
		## (configure_unpackable_bases), not the flag object's own health, and
		## reporting it would answer a question about a different object.
		var resolved := _view("units.health_percent", object_name)
		if resolved.has("reason"):
			return _refuse_query("units.health_percent", String(resolved["reason"]))
		var view: Dictionary = resolved["view"]
		if bool(view["packed"]):
			return _refuse_query(
				"units.health_percent",
				(
					"'%s' is a base flag nobody has unpacked; the flag row's "
					+ "health is the fortress it would become, not the flag "
					+ "object's own"
				) % object_name
			)
		if not bool(view["present"]):
			return _refuse_query(
				"units.health_percent",
				"'%s' no longer names an object in the simulation" % object_name
			)
		return SageWorldQuery.hit(
			100.0 * float(view["health"]) / float(int(view["maximum_health"]))
		)

	func set_object_status(
		scope: int, name: String, status: String, enabled: bool
	) -> bool:
		## UNIT_CHANGE_OBJECT_STATUS / TEAM_CHANGE_OBJECT_STATUS.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_object_status", "no simulation attached")
		if w.sim.winner != -1:
			return _refuse_command(
				"units.set_object_status", "the match is already resolved"
			)
		if status.strip_edges() == "":
			return _refuse_command(
				"units.set_object_status", "empty object status names nothing"
			)
		var resolved := _living_entity_ids_for_status_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command(
				"units.set_object_status", String(resolved["reason"])
			)
		var result: Dictionary = w.sim.set_entities_object_status(
			resolved["ids"] as Array, status, enabled
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"units.set_object_status", String(result.get("reason", ""))
			)
		return true

	func has_object_status(
		scope: int, name: String, status: String, require_all: bool
	) -> SageWorldQuery:
		## UNIT_HAS_OBJECT_STATUS / TEAM_ALL_HAS_OBJECT_STATUS /
		## TEAM_SOME_HAVE_OBJECT_STATUS. require_all selects ALL vs SOME.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.has_object_status", "no simulation attached")
		if status.strip_edges() == "":
			return _refuse_query(
				"units.has_object_status", "empty object status names nothing"
			)
		var resolved := _living_entity_ids_for_status_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_query(
				"units.has_object_status", String(resolved["reason"])
			)
		var living: Array = resolved["ids"] as Array
		if living.is_empty():
			return SageWorldQuery.hit(false)
		var hits := 0
		for id_value in living:
			if w.sim.entity_has_object_status(int(id_value), status):
				hits += 1
		if require_all:
			return SageWorldQuery.hit(hits == living.size())
		return SageWorldQuery.hit(hits > 0)

	func _living_entity_ids_for_status_scope(scope: int, name: String) -> Dictionary:
		## {"ids": Array} or {"reason": String}. Entity-only: structures refuse
		## on UNIT scope because this surface is the unit object-status model.
		var w := _world()
		match scope:
			SageScriptWorld.Scope.UNIT:
				var view := w.named_object_view(name)
				if view.is_empty():
					return {
						"reason": (
							"'%s' is not a name this simulation's shared object / "
							+ "unit-reference namespace holds"
						)
						% name
					}
				if bool(view.get("packed", false)):
					return {"reason": "'%s' is a packed base flag" % name}
				if not bool(view.get("present", false)):
					return {
						"reason": (
							"'%s' no longer names an object in the simulation" % name
						)
					}
				var entity_id := int(view.get("entity_id", view.get("id", 0)))
				if entity_id <= 0 or not w.sim.entities.has(entity_id):
					return {
						"reason": (
							"'%s' does not resolve to a living entity (structures "
							+ "are outside this object-status surface)"
						)
						% name
					}
				if int((w.sim.entities[entity_id] as Dictionary).get("health", 0)) <= 0:
					return {"ids": []}
				return {"ids": [entity_id]}
			SageScriptWorld.Scope.TEAM:
				var team_resolved := w.resolve_script_team_name(name)
				if team_resolved.has("reason"):
					return {"reason": String(team_resolved["reason"])}
				var members: Dictionary = w.sim.script_team_members(
					String(team_resolved["script_team"]), true
				)
				if not bool(members.get("ok", false)):
					return {"reason": String(members.get("reason", ""))}
				if not bool(members.get("complete", false)):
					return {
						"reason": (
							"team '%s' has incomplete imported membership" % name
						)
					}
				var out: Array = []
				for handle_value in members.get("members", []) as Array:
					var handle := handle_value as Dictionary
					if String(handle.get("kind", "")) == "entity":
						out.append(int(handle.get("id", -1)))
				return {"ids": out}
			SageScriptWorld.Scope.PLAYER:
				var player_resolved := w._resolve_single_player_team(name)
				if player_resolved.has("reason"):
					return {"reason": String(player_resolved["reason"])}
				return {"ids": w.sim.living_ids(int(player_resolved["team"]))}
			_:
				return {
					"reason": "object status scope %d is not supported" % scope
				}


	func set_held(object_name: String, held: bool) -> bool:
		return _unit_bool_flag("units.set_held", object_name, "held", held)

	func set_repulsor(object_name: String, enabled: bool) -> bool:
		return _unit_bool_flag("units.set_repulsor", object_name, "repulsor", enabled)

	func set_stealth_enabled(object_name: String, enabled: bool) -> bool:
		return _unit_bool_flag("units.set_stealth_enabled", object_name, "stealth_enabled", enabled)

	func set_strict_control_enabled(object_name: String, enabled: bool) -> bool:
		return _unit_bool_flag("units.set_strict_control_enabled", object_name, "strict_control", enabled)

	func set_house_color_enabled(object_name: String, enabled: bool) -> bool:
		return _unit_bool_flag("units.set_house_color_enabled", object_name, "house_color", enabled)

	func set_close_range_weapon(object_name: String, enabled: bool) -> bool:
		return _unit_bool_flag("units.set_close_range_weapon", object_name, "close_range_weapon", enabled)

	func set_flame_status(object_name: String, burning: bool) -> bool:
		return _unit_bool_flag("units.set_flame_status", object_name, "burning", burning)

	func is_webbed(object_name: String) -> SageWorldQuery:
		return _unit_bool_query("units.is_webbed", object_name, "webbed")

	func set_special_weaponset(object_name: String, weaponset: String) -> bool:
		return _unit_string_state("units.set_special_weaponset", object_name, "weaponset", weaponset)

	func set_emoticon(object_name: String, emoticon: String, duration_ticks: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_emoticon", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.set_emoticon", String(ids["reason"]))
		w.sim.set_entity_string_state(int(ids["id"]), "emoticon", emoticon)
		w.sim.set_entity_timed_flag(int(ids["id"]), "emoticon", w.sim.tick_index + maxi(0, duration_ticks))
		return true

	func set_model_condition(object_name: String, condition: String, enabled: bool, duration_ticks: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_model_condition", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.set_model_condition", String(ids["reason"]))
		var flag := "mc:" + condition
		if enabled:
			if duration_ticks > 0:
				w.sim.set_entity_timed_flag(int(ids["id"]), flag, w.sim.tick_index + duration_ticks)
			else:
				w.sim.set_entity_bool_flag(int(ids["id"]), flag, true)
		else:
			w.sim.set_entity_bool_flag(int(ids["id"]), flag, false)
			w.sim.set_entity_timed_flag(int(ids["id"]), flag, -1)
		return true

	func set_object_panel_flag(object_name: String, flag: String, enabled: bool) -> bool:
		return _unit_bool_flag("units.set_object_panel_flag", object_name, "panel:" + flag, enabled)

	func set_topple_direction(object_name: String, direction: Vector3) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_topple_direction", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.set_topple_direction", String(ids["reason"]))
		w.sim.set_entity_string_state(
			int(ids["id"]), "topple_direction", "%s,%s,%s" % [direction.x, direction.y, direction.z]
		)
		return true

	func shock(object_name: String, duration_ticks: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.shock", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.shock", String(ids["reason"]))
		w.sim.set_entity_bool_flag(int(ids["id"]), "held", true)
		w.sim.set_entity_timed_flag(int(ids["id"]), "shock", w.sim.tick_index + maxi(0, duration_ticks))
		return true

	func delete(object_name: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.delete", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.delete", String(ids["reason"]))
		var result: Dictionary = w.sim.delete_entity(int(ids["id"]))
		return true if bool(result.get("ok", false)) else _refuse_command(
			"units.delete", String(result.get("reason", ""))
		)

	func set_team(object_name: String, team: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_team", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.set_team", String(ids["reason"]))
		var resolved := w.resolve_script_team_name(team)
		if resolved.has("reason"):
			return _refuse_command("units.set_team", String(resolved["reason"]))
		var result: Dictionary = w.sim.set_entity_team(int(ids["id"]), int(resolved["team"]))
		return true if bool(result.get("ok", false)) else _refuse_command(
			"units.set_team", String(result.get("reason", ""))
		)

	func enter_object(object_name: String, target_object: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.enter_object", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.enter_object", String(ids["reason"]))
		var view := w.named_object_view(target_object)
		if view.is_empty() or int(view.get("structure_id", 0)) <= 0:
			return _refuse_command("units.enter_object", "'%s' is not a structure" % target_object)
		var result: Dictionary = w.sim.contain_entity(int(view["structure_id"]), int(ids["id"]))
		return true if bool(result.get("ok", false)) else _refuse_command(
			"units.enter_object", String(result.get("reason", ""))
		)

	func exit(object_name: String, all_contained: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.exit", "no simulation attached")
		if all_contained:
			var view := w.named_object_view(object_name)
			if view.is_empty() or int(view.get("structure_id", 0)) <= 0:
				return _refuse_command("units.exit", "'%s' is not a structure" % object_name)
			var sid := int(view["structure_id"])
			for eid in (w.sim.containment.get(sid, []) as Array).duplicate():
				w.sim.exit_entity_container(int(eid))
			return true
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.exit", String(ids["reason"]))
		w.sim.exit_entity_container(int(ids["id"]))
		return true

	func is_building_empty(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.is_building_empty", "no simulation attached")
		var view := w.named_object_view(object_name)
		if view.is_empty() or int(view.get("structure_id", 0)) <= 0:
			return _refuse_query("units.is_building_empty", "'%s' is not a structure" % object_name)
		return SageWorldQuery.hit(w.sim.passenger_count(int(view["structure_id"])) == 0)

	func destroy_all_contained(object_name: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.destroy_all_contained", "no simulation attached")
		var view := w.named_object_view(object_name)
		if view.is_empty() or int(view.get("structure_id", 0)) <= 0:
			return _refuse_command("units.destroy_all_contained", "'%s' is not a structure" % object_name)
		var sid := int(view["structure_id"])
		for eid in (w.sim.containment.get(sid, []) as Array).duplicate():
			w.sim.script_kill_entity(int(eid))
			w.sim.exit_entity_container(int(eid))
		return true

	func _unit_entity_id(object_name: String) -> Dictionary:
		var w := _world()
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			return {"id": int(object_name)}
		return {
			"reason": (
				"'%s' is not addressable as a live entity id; use numeric entity ids for unit-scope surfaces"
				% object_name
			)
		}

	func _unit_bool_flag(method: String, object_name: String, flag: String, enabled: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(method, "no simulation attached")
		if w.sim.winner != -1:
			return _refuse_command(method, "the match is already resolved")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command(method, String(ids["reason"]))
		var result: Dictionary = w.sim.set_entity_bool_flag(int(ids["id"]), flag, enabled)
		return true if bool(result.get("ok", false)) else _refuse_command(
			method, String(result.get("reason", ""))
		)

	func _unit_bool_query(method: String, object_name: String, flag: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query(method, "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_query(method, String(ids["reason"]))
		return SageWorldQuery.hit(w.sim.entity_bool_flag(int(ids["id"]), flag))

	func _unit_string_state(method: String, object_name: String, key: String, value: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(method, "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command(method, String(ids["reason"]))
		var result: Dictionary = w.sim.set_entity_string_state(int(ids["id"]), key, value)
		return true if bool(result.get("ok", false)) else _refuse_command(
			method, String(result.get("reason", ""))
		)


	func set_reference(reference: String, object_name: String) -> bool:
		## SET_UNIT_REFERENCE(UNIT_REF, UNIT) and
		## SET_UNIT_REFERENCE_TO_REFERENCE(UNIT_REF, UNIT_REF) - destination
		## FIRST. Both spellings land here because the namespace is shared, and
		## the retail corpus proves the sharing: AI_BASE is written into a
		## UNIT_REF slot by NAMED_BASE_UNPACK_FREE and then read in plain UNIT
		## slots by NAMED_NOT_DESTROYED, TEAM_GUARD_OBJECT and
		## UNIT_THREAT_LEVEL; AI_EXPANSION_n is written by NAMED_BASE_UNPACK
		## and read as the SOURCE of all 16 SET_UNIT_REFERENCE_TO_REFERENCE
		## sites.
		##
		## THE SOURCE IS RESOLVED NOW AND STORED AS A HANDLE, never as the
		## source string: SET_UNIT_REFERENCE_TO_REFERENCE copies the source's
		## CURRENT binding, so a stored string would alias the destination to
		## the source's future values instead.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_reference", "no simulation attached")
		if reference == "":
			return _refuse_command(
				"units.set_reference", "an empty reference names no destination"
			)
		var rejection := w._unit_reference_rejection(reference)
		if rejection != "":
			return _refuse_command("units.set_reference", rejection)
		if w._script_player_team() < 0:
			return _refuse_command(
				"units.set_reference",
				(
					"references are stored per script player and this world has "
					+ "no script player bound, so there is no namespace to bind in"
				)
			)
		var view := w.named_object_view(object_name)
		if view.is_empty():
			return _refuse_command(
				"units.set_reference",
				(
					"'%s' is not a name this simulation's shared object / "
					+ "unit-reference namespace holds, so there is no handle to "
					+ "bind - binding the string would leave a dangling reference"
				) % object_name
			)
		if String(view["flag"]) != "":
			return w.sim.bind_script_unit_reference_to_base(
				w._script_player_team(), reference, String(view["flag"])
			)
		if not bool(view["present"]):
			return _refuse_command(
				"units.set_reference",
				"'%s' no longer names an object in the simulation" % object_name
			)
		w._bind_unit_reference(reference, int(view["structure_id"]))
		return true

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
		var resolved_player := w._resolve_single_player_team(player)
		if resolved_player.has("reason"):
			return _refuse_query(
				"units.has_command_points_to_build", String(resolved_player["reason"])
			)
		var team := int(resolved_player["team"])
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
			w.sim.command_point_total_for_team(team)
			- w.sim.command_points_for_team(team)
			- w._queued_command_points(team)
		)
		return SageWorldQuery.hit(headroom >= cost)




	func create_object(
		object_type: String, player: String, position: Vector3, angle: float
	) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.create_object", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("units.create_object", String(resolved["reason"]))
		var result: Dictionary = w.sim.script_spawn_entity(
			object_type, int(resolved["team"]), RetailSliceScriptWorld._sim_point(position)
		)
		if not bool(result.get("ok", false)):
			return _refuse_query("units.create_object", String(result.get("reason", "spawn failed")))
		w.sim.surface_bag_set("spawn_angle:%s" % result.get("entity_id", 0), angle)
		return SageWorldQuery.hit(str(int(result.get("entity_id", 0))))


	func spawn_at(
		object_type: String, player: String, object_name: String, position: Vector3, angle: float
	) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.spawn_at", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("units.spawn_at", String(resolved["reason"]))
		var result: Dictionary = w.sim.script_spawn_entity(
			object_type, int(resolved["team"]), RetailSliceScriptWorld._sim_point(position)
		)
		if not bool(result.get("ok", false)):
			return _refuse_query("units.spawn_at", String(result.get("reason", "")))
		w.sim.surface_bag_set("spawn_name:%s" % object_name, int(result.get("entity_id", 0)))
		w.sim.surface_bag_set("spawn_angle:%s" % object_name, angle)
		return SageWorldQuery.hit(object_name)



	func create_on_team_at(
		object_type: String, team: String, target: Dictionary, object_name: String
	) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.create_on_team_at", "no simulation attached")
		var resolved := w.resolve_script_team_name(team)
		if resolved.has("reason"):
			return _refuse_query("units.create_on_team_at", String(resolved["reason"]))
		var pos := Vector2.ZERO
		if int(target.get("kind", -1)) == SageScriptWorld.TargetKind.POSITION:
			pos = RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO))
		elif int(target.get("kind", -1)) == SageScriptWorld.TargetKind.WAYPOINT:
			var wp := String(target.get("name", ""))
			if not w.sim.script_waypoints.has(wp):
				return _refuse_query("units.create_on_team_at", "waypoint missing")
			pos = w.sim.script_waypoints[wp]
		var result: Dictionary = w.sim.script_spawn_entity(
			object_type, int(resolved["team"]), pos
		)
		if not bool(result.get("ok", false)):
			return _refuse_query("units.create_on_team_at", String(result.get("reason", "")))
		w.sim.surface_bag_set("spawn_name:%s" % object_name, int(result.get("entity_id", 0)))
		return SageWorldQuery.hit(object_name)
	func transfer_ownership(object_name: String, player: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.transfer_ownership", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.transfer_ownership", String(ids["reason"]))
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("units.transfer_ownership", String(resolved["reason"]))
		var result: Dictionary = w.sim.set_entity_team(int(ids["id"]), int(resolved["team"]))
		return true if bool(result.get("ok", false)) else _refuse_command(
			"units.transfer_ownership", String(result.get("reason", ""))
		)

	func skill_points(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.skill_points", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_query("units.skill_points", String(ids["reason"]))
		return SageWorldQuery.hit(
			int(w.sim.surface_bag_get("skill_points:%s" % ids["id"], 0))
		)

	func is_selected(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.is_selected", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return SageWorldQuery.hit(false)
		return SageWorldQuery.hit(w.sim.selected_ids.has(int(ids["id"])))

	func set_selected(object_name: String, selected: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_selected", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.set_selected", String(ids["reason"]))
		var eid := int(ids["id"])
		if selected:
			if not w.sim.selected_ids.has(eid):
				w.sim.selected_ids.append(eid)
		else:
			w.sim.selected_ids.erase(eid)
		return true

	func type_is_selected(player: String, object_type: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.type_is_selected", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool("type_selected:%s:%s" % [player, object_type], false)
		)

	func enemy_sighted(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.enemy_sighted", "no simulation attached")
		return SageWorldQuery.hit(w.sim.surface_bag_bool("unit_enemy_sighted:%s" % object_name, false))

	func was_discovered(object_name: String, by_player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.was_discovered", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool("unit_discovered:%s:%s" % [object_name, by_player], false)
		)

	func type_was_sighted(player: String, object_type: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.type_was_sighted", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool("type_sighted:%s:%s" % [player, object_type], false)
		)

	func threat(object_name: String) -> SageWorldQuery:
		## Slice combat-weight for one living unit (pure formula read).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.threat", "no simulation attached")
		w.sim._ensure_parity()
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_query("units.threat", String(ids["reason"]))
		var eid := int(ids["id"])
		if not w.sim.entities.has(eid):
			return _refuse_query("units.threat", "entity missing")
		return SageWorldQuery.hit(
			w.sim.parity.entity_threat_weight(w.sim.entities[eid])
		)

	func threat_within_radius(object_name: String, radius: float) -> SageWorldQuery:
		## Hostile combat-weight within radius of this unit (pure formula read).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.threat_within_radius", "no simulation attached")
		w.sim._ensure_parity()
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_query("units.threat_within_radius", String(ids["reason"]))
		var eid := int(ids["id"])
		if not w.sim.entities.has(eid):
			return _refuse_query("units.threat_within_radius", "entity missing")
		var row: Dictionary = w.sim.entities[eid]
		return SageWorldQuery.hit(
			w.sim.parity.threat_in_radius(
				w.sim,
				int(row.get("team", -1)),
				row.get("position", Vector2.ZERO),
				radius
			)
		)

	func type_sighted_by(
		observer: String, object_type: String, owner: String
	) -> SageWorldQuery:
		## Ledger-backed sighting: true when a discovery record was written for
		## this observer/type/owner triple (scripts/AI can also seed via bag).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.type_sighted_by", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool(
				"type_sighted_by:%s:%s:%s" % [observer, object_type, owner], false
			)
		)

	func create_object_on_team(
		object_type: String, team: String, position: Vector3, angle: float
	) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.create_object_on_team", "no simulation attached")
		var resolved := w.resolve_script_team_name(team)
		if resolved.has("reason"):
			return _refuse_query("units.create_object_on_team", String(resolved["reason"]))
		var result: Dictionary = w.sim.script_spawn_entity(
			object_type, int(resolved["team"]), RetailSliceScriptWorld._sim_point(position)
		)
		if not bool(result.get("ok", false)):
			return _refuse_query(
				"units.create_object_on_team", String(result.get("reason", "spawn failed"))
			)
		var eid := int(result.get("entity_id", 0))
		w.sim.surface_bag_set("spawn_angle:%s" % eid, angle)
		return SageWorldQuery.hit(str(eid))

	func set_attitude(object_name: String, mood: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_attitude", "no simulation attached")
		w.sim._ensure_parity()
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.set_attitude", String(ids["reason"]))
		var eid := int(ids["id"])
		if w.sim.entities.has(eid):
			w.sim.parity.apply_attitude_mood(w.sim.entities[eid], mood)
			w.sim.issue_set_stance(
				[eid],
				String((w.sim.entities[eid] as Dictionary).get("stance", "Battle")),
				int((w.sim.entities[eid] as Dictionary).get("team", -1))
			)
		return true

	func force_emotion(object_name: String, emotion: int, duration_ticks: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.force_emotion", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.force_emotion", String(ids["reason"]))
		var eid := int(ids["id"])
		var until := w.sim.tick_index + maxi(0, duration_ticks)
		w.sim.set_entity_string_state(eid, "emotion", str(emotion))
		if w.sim.has_method("set_entity_timed_flag"):
			w.sim.set_entity_timed_flag(eid, "emotion", until)
		if emotion != 0 and w.sim.entities.has(eid):
			var team := int((w.sim.entities[eid] as Dictionary).get("team", -1))
			w.sim.issue_set_stance([eid], "HoldGround", team)
		return true


	func set_gate_state(object_name: String, open: bool, ready: bool) -> bool:
		## Write gate open/ready onto a live structure. Refuse when the name is
		## not a structure (bag success would invent a gate).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_gate_state", "no simulation attached")
		var view := w.named_object_view(object_name)
		if view.is_empty() or int(view.get("structure_id", 0)) <= 0:
			return _refuse_command(
				"units.set_gate_state",
				"'%s' is not a live structure gate target" % object_name
			)
		var sid := int(view["structure_id"])
		if not w.sim.structures.has(sid):
			return _refuse_command("units.set_gate_state", "structure missing")
		var row: Dictionary = w.sim.structures[sid]
		row["gate_open"] = open
		row["gate_ready"] = ready
		w.sim.structures[sid] = row
		return true

	func gate_is_open(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.gate_is_open", "no simulation attached")
		var view := w.named_object_view(object_name)
		if view.is_empty() or int(view.get("structure_id", 0)) <= 0:
			return _refuse_query(
				"units.gate_is_open",
				"'%s' is not a live structure" % object_name
			)
		var sid := int(view["structure_id"])
		if not w.sim.structures.has(sid):
			return _refuse_query("units.gate_is_open", "structure missing")
		return SageWorldQuery.hit(
			bool((w.sim.structures[sid] as Dictionary).get("gate_open", false))
		)


	func deploy_siege(object_name: String, target: Dictionary) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.deploy_siege", "no simulation attached")
		var eid := -1
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			eid = int(object_name)
		if eid < 0:
			return _refuse_command("units.deploy_siege", "'%s' is not a live entity" % object_name)
		var row: Dictionary = w.sim.entities[eid]
		row["siege_deployed"] = true
		row["siege_target"] = target.duplicate(true)
		w.sim.entities[eid] = row
		return true

	func retract_siege(object_name: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.retract_siege", "no simulation attached")
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			var row: Dictionary = w.sim.entities[int(object_name)]
			row["siege_deployed"] = false
			row.erase("siege_target")
			w.sim.entities[int(object_name)] = row
			return true
		return _refuse_command("units.retract_siege", "'%s' is not a live entity" % object_name)

	func siege_is_attached_to_wall(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.siege_is_attached_to_wall", "no simulation attached")
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			return SageWorldQuery.hit(
				bool((w.sim.entities[int(object_name)] as Dictionary).get("siege_deployed", false))
			)
		return SageWorldQuery.hit(false)

	func set_cave_index(object_name: String, index: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_cave_index", "no simulation attached")
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			var row: Dictionary = w.sim.entities[int(object_name)]
			row["cave_index"] = index
			w.sim.entities[int(object_name)] = row
			return true
		if object_name.is_valid_int() and w.sim.structures.has(int(object_name)):
			var srow: Dictionary = w.sim.structures[int(object_name)]
			srow["cave_index"] = index
			w.sim.structures[int(object_name)] = srow
			return true
		return _refuse_command("units.set_cave_index", "'%s' is not a live object" % object_name)

	func set_hulk_lifetime(object_name: String, ticks: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_hulk_lifetime", "no simulation attached")
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			var row: Dictionary = w.sim.entities[int(object_name)]
			row["hulk_expire_tick"] = w.sim.tick_index + maxi(0, ticks)
			w.sim.entities[int(object_name)] = row
			return true
		return _refuse_command(
			"units.set_hulk_lifetime", "'%s' is not a live entity" % object_name
		)

	func set_warehouse_value(object_name: String, value: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.set_warehouse_value", "no simulation attached")
		if object_name.is_valid_int() and w.sim.structures.has(int(object_name)):
			var srow: Dictionary = w.sim.structures[int(object_name)]
			srow["warehouse_value"] = value
			w.sim.structures[int(object_name)] = srow
			return true
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			var row: Dictionary = w.sim.entities[int(object_name)]
			row["warehouse_value"] = value
			w.sim.entities[int(object_name)] = row
			return true
		return _refuse_command(
			"units.set_warehouse_value", "'%s' is not a live object" % object_name
		)

	func delete_all_unmanned(player: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.delete_all_unmanned", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("units.delete_all_unmanned", String(resolved["reason"]))
		for eid in w.sim.living_ids(int(resolved["team"])).duplicate():
			if w.sim.entity_bool_flag(int(eid), "unmanned"):
				w.sim.delete_entity(int(eid))
		return true


	func destroyed_by_object_type(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.destroyed_by_object_type", "no simulation attached")
		return SageWorldQuery.hit(
			String(w.sim.surface_bag_get("destroyed_by_type:%s" % object_name, ""))
		)


	func execute_sequential_script(team: String, script: String, looping: bool) -> bool:
		## Queue sequential behavior on the sim's sequential_script_queues store.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.execute_sequential_script", "no simulation attached")
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			return _refuse_command(
				"units.execute_sequential_script", String(team_r["reason"])
			)
		var key := String(team_r.get("script_team", team))
		if not w.sim.sequential_script_queues.has(key):
			w.sim.sequential_script_queues[key] = []
		var queue: Array = w.sim.sequential_script_queues[key]
		queue.append({"script": script, "loop": looping, "tick": w.sim.tick_index})
		w.sim.sequential_script_queues[key] = queue
		return true

	func stop_sequential_script(team: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.stop_sequential_script", "no simulation attached")
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			return _refuse_command(
				"units.stop_sequential_script", String(team_r["reason"])
			)
		var key := String(team_r.get("script_team", team))
		w.sim.sequential_script_queues.erase(key)
		return true
	func exit_specific_building(object_name: String, building: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.exit_specific_building", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.exit_specific_building", String(ids["reason"]))
		w.sim.exit_entity_container(int(ids["id"]))
		return true


	func build_structure_at(
		object_name: String, object_type: String, target: Dictionary
	) -> bool:
		## Builder unit constructs expansion of object_type near target position.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.build_structure_at", "no simulation attached")
		var team_id := -1
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			team_id = int((w.sim.entities[int(object_name)] as Dictionary).get("team", -1))
		if team_id < 0:
			team_id = w._script_player_team()
		if team_id < 0:
			return _refuse_command("units.build_structure_at", "no owner team for builder")
		var kind: String = w.sim.expansion_kind_for_object_id(object_type)
		if kind == "":
			if not w.sim.match_script_flags.has("structure_build_at"):
				w.sim.match_script_flags["structure_build_at"] = {}
			var table: Dictionary = w.sim.match_script_flags["structure_build_at"]
			table[object_name] = {"type": object_type, "target": target.duplicate(true)}
			w.sim.match_script_flags["structure_build_at"] = table
			return true
		var base_id := w.sim.fortress_id(team_id)
		if base_id <= 0:
			for sid in w.sim.living_structure_ids(team_id):
				if w.sim.expansion_pads.has(int(sid)):
					base_id = int(sid)
					break
		if base_id <= 0:
			return _refuse_command("units.build_structure_at", "no fortress/base pads")
		var result: Dictionary = w.sim.issue_expansion_construct(team_id, base_id, kind)
		return true if bool(result.get("ok", false)) else _refuse_command(
			"units.build_structure_at", String(result.get("reason", "construct refused"))
		)

	func create_revival_entry(
		player: String, object_type: String, level: int, from_carryover: bool
	) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.create_revival_entry", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("units.create_revival_entry", String(resolved["reason"]))
		var team_id := int(resolved["team"])
		if not w.sim.match_script_flags.has("revival_queue"):
			w.sim.match_script_flags["revival_queue"] = {}
		var queue_table: Dictionary = w.sim.match_script_flags["revival_queue"]
		var rows: Array = queue_table.get(team_id, []) as Array
		rows.append({
			"object_type": object_type,
			"level": level,
			"from_carryover": from_carryover,
			"tick": w.sim.tick_index,
		})
		queue_table[team_id] = rows
		w.sim.match_script_flags["revival_queue"] = queue_table
		return true

	func create_delayed_carryover_at(
		player: String, object_type: String, target: Dictionary
	) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.create_delayed_carryover_at", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command(
				"units.create_delayed_carryover_at", String(resolved["reason"])
			)
		var team_id := int(resolved["team"])
		if not w.sim.match_script_flags.has("delayed_carryover"):
			w.sim.match_script_flags["delayed_carryover"] = {}
		var table: Dictionary = w.sim.match_script_flags["delayed_carryover"]
		var rows: Array = table.get(team_id, []) as Array
		rows.append({
			"object_type": object_type,
			"target": target.duplicate(true),
			"tick": w.sim.tick_index,
		})
		table[team_id] = rows
		w.sim.match_script_flags["delayed_carryover"] = table
		return true

	func has_delayed_carryover_of_type(player: String, object_type: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.has_delayed_carryover_of_type", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query(
				"units.has_delayed_carryover_of_type", String(resolved["reason"])
			)
		return SageWorldQuery.hit(
			w.sim.script_surface_bag.has("carryover:%s:%s" % [resolved["team"], object_type])
		)

	func is_totally_dead(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.is_totally_dead", "no simulation attached")
		# Entity fully removed from world, or structure at health 0 with gone flag.
		if object_name.is_valid_int():
			return SageWorldQuery.hit(not w.sim.entities.has(int(object_name)))
		var view := w.named_object_view(object_name)
		if view.is_empty():
			return SageWorldQuery.hit(true)
		return SageWorldQuery.hit(not bool(view.get("present", false)) and not bool(view.get("packed", false)))

	func stance(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.stance", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_query("units.stance", String(ids["reason"]))
		return SageWorldQuery.hit(
			String(w.sim.entity_string_state(int(ids["id"]), "stance"))
		)

	func stop(object_name: String, flush_queue: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("units.stop", "no simulation attached")
		var ids := _unit_entity_id(object_name)
		if ids.has("reason"):
			return _refuse_command("units.stop", String(ids["reason"]))
		var arr: Array[int] = [int(ids["id"])]
		w.sim.issue_stop(arr, int((w.sim.entities[int(ids["id"])] as Dictionary).get("team", -1)))
		if flush_queue:
			w.sim.surface_bag_set("flush_queue:%s" % ids["id"], true)
		return true


	func unowned_faction_unit_exists(player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("units.unowned_faction_unit_exists", "no simulation attached")
		# Honest empty answer unless neutral ownership tags appear.
		for eid in w.sim.entities.keys():
			var row: Dictionary = w.sim.entities[eid]
			if int(row.get("health", 0)) <= 0:
				continue
			if String(row.get("ownership", "")) == "neutral":
				return SageWorldQuery.hit(true)
		return SageWorldQuery.hit(false)

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

	func set_buildings_allowed(player: String, building_type: String, allowed: bool) -> bool:
		## ALLOW_DISALLOW_ONE_BUILDING only. The handler never passes the empty
		## type reserved for the separate ALL-buildings opcode.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.set_buildings_allowed", "no simulation attached")
		if building_type == "":
			return _refuse_command(
				"ai.set_buildings_allowed",
				"the all-buildings variant is outside this implementation packet"
			)
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("ai.set_buildings_allowed", String(resolved["reason"]))
		if not w.sim.set_building_allowed(int(resolved["team"]), building_type, allowed):
			return _refuse_command(
				"ai.set_buildings_allowed",
				"simulation rejected building permission for '%s'" % building_type
			)
		return true

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
		var resolved_player := w._resolve_single_player_team(player)
		if resolved_player.has("reason"):
			return _refuse_query("ai.base_unpackable", String(resolved_player["reason"]))
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

	func build_base_building_per_tactical_marker(
		building_type: String,
		near_or_far: String,
		marker_type: String,
		base: String,
		result_reference: String
	) -> bool:
		## Place expansion using tactical-marker near/far offset to pick the
		## free pad nearest the marker placement, then issue_expansion_construct.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command(
				"ai.build_base_building_per_tactical_marker", "no simulation attached"
			)
		w.sim._ensure_parity()
		var team := w._script_player_team()
		if team < 0:
			return _refuse_command(
				"ai.build_base_building_per_tactical_marker",
				"no script player is bound (bind_script_player)"
			)
		var kind: String = w.sim.expansion_kind_for_object_id(building_type)
		if kind == "":
			return _refuse_command(
				"ai.build_base_building_per_tactical_marker",
				"object type '%s' is not an expansion the simulation models" % building_type
			)
		var resolved_base := w._resolve_base_structure(base)
		if resolved_base.is_empty():
			return _refuse_command(
				"ai.build_base_building_per_tactical_marker",
				"base '%s' is not a bound unit reference or base flag" % base
			)
		if resolved_base.has("packed"):
			return _refuse_command(
				"ai.build_base_building_per_tactical_marker",
				"base flag '%s' has not been unpacked" % base
			)
		var base_id := int(resolved_base.get("id", 0))
		var base_pos: Vector2 = (w.sim.structures.get(base_id, {}) as Dictionary).get(
			"position", Vector2.ZERO
		)
		var place: Dictionary = w.sim.parity.placement_for_marker(
			marker_type, near_or_far, base_pos
		)
		if not bool(place.get("ok", false)):
			return _refuse_command(
				"ai.build_base_building_per_tactical_marker",
				String(place.get("reason", "marker placement failed"))
			)
		var target_pos: Vector2 = place.get("position", base_pos)
		# Prefer the free pad nearest the marker placement (pad-at-point proxy).
		var pad_index := -1
		if w.sim.expansion_pads.has(base_id):
			var pads: Array = w.sim.expansion_pads[base_id]
			var best_d := INF
			for i in pads.size():
				var pad: Dictionary = pads[i]
				if int(pad.get("expansion_structure_id", 0)) != 0:
					continue
				var d: float = target_pos.distance_to(pad.get("position", Vector2.ZERO))
				if d < best_d:
					best_d = d
					pad_index = i
		var result: Dictionary
		if pad_index >= 0:
			result = w.sim.issue_expansion_construct(team, base_id, kind, pad_index)
		else:
			result = w.sim.issue_expansion_construct(team, base_id, kind)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"ai.build_base_building_per_tactical_marker",
				"simulation rejected marker build: %s" % String(result.get("reason", ""))
			)
		var sid := int(result.get("structure_id", 0))
		# Stamp intended marker position on the new structure for consumers.
		if sid > 0 and w.sim.structures.has(sid):
			var srow: Dictionary = w.sim.structures[sid]
			srow["marker_placement"] = target_pos
			srow["tactical_marker"] = marker_type
			w.sim.structures[sid] = srow
		var rejection := w._unit_reference_rejection(result_reference)
		if rejection != "":
			return _refuse_command(
				"ai.build_base_building_per_tactical_marker", rejection
			)
		w._bind_unit_reference(result_reference, sid)
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



	func base_population(player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("ai.base_population", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("ai.base_population", String(resolved["reason"]))
		return SageWorldQuery.hit(
			w.sim.living_ids(int(resolved["team"])).size()
		)

	func camps_should_unpack(region: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("ai.camps_should_unpack", "no simulation attached")
		return SageWorldQuery.hit(w.sim.surface_bag_bool("camps_unpack:%s" % region, false))


	func set_view_guardband(width: float, height: float) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.set_view_guardband", "no simulation attached")
		w.sim._ensure_parity()
		w.sim.parity.emit_presentation(
			w.sim, "ai.view_guardband", {"width": width, "height": height}
		)
		w.sim.match_script_flags["view_guardband"] = {"width": width, "height": height}
		return true

	func set_attack_priority(
		set_name: String, target_kind: String, target_name: String, priority: int
	) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.set_attack_priority", "no simulation attached")
		w.sim.set_attack_priority_entry(set_name, target_kind, target_name, priority)
		return true


	func set_default_attack_priority(set_name: String, priority: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.set_default_attack_priority", "no simulation attached")
		w.sim.set_default_attack_priority_entry(set_name, priority)
		return true

	func build_on_foundation(player: String, foundation: String, building_type: String) -> bool:
		## Construct only when `foundation` resolves to an owned base structure
		## that has expansion pads. Never invents a pad for an unrelated fortress.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.build_on_foundation", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("ai.build_on_foundation", String(resolved["reason"]))
		var team_id := int(resolved["team"])
		var kind: String = w.sim.expansion_kind_for_object_id(building_type)
		if kind == "":
			return _refuse_command(
				"ai.build_on_foundation",
				"building type '%s' is not a modeled expansion" % building_type
			)
		var base_id := -1
		var view := w.named_object_view(foundation)
		if not view.is_empty() and int(view.get("structure_id", 0)) > 0:
			base_id = int(view["structure_id"])
		elif foundation.is_valid_int() and w.sim.structures.has(int(foundation)):
			base_id = int(foundation)
		elif w.sim.unpackable_bases.has(foundation):
			var brow: Dictionary = w.sim.unpackable_bases[foundation]
			base_id = int(brow.get("structure_id", 0))
		if base_id <= 0 or not w.sim.structures.has(base_id):
			return _refuse_command(
				"ai.build_on_foundation",
				"foundation '%s' is not a known owned base structure" % foundation
			)
		var srow: Dictionary = w.sim.structures[base_id]
		if int(srow.get("team", -1)) != team_id:
			return _refuse_command(
				"ai.build_on_foundation", "foundation is not owned by player"
			)
		if not w.sim.expansion_pads.has(base_id):
			return _refuse_command(
				"ai.build_on_foundation", "foundation has no expansion pads"
			)
		var result: Dictionary = w.sim.issue_expansion_construct(team_id, base_id, kind)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"ai.build_on_foundation",
				String(result.get("reason", "expansion construct refused"))
			)
		return true

	func sell_on_foundation(player: String, foundation: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.sell_on_foundation", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("ai.sell_on_foundation", String(resolved["reason"]))
		var team_id := int(resolved["team"])
		# Sell the nearest owned structure to a foundation name if it matches a
		# structure name/ref; otherwise record a hash-backed sell intent.
		var view := w.named_object_view(foundation)
		if not view.is_empty() and int(view.get("structure_id", 0)) > 0:
			var sid := int(view["structure_id"])
			if w.sim.structures.has(sid):
				var srow: Dictionary = w.sim.structures[sid]
				if int(srow.get("team", -1)) == team_id:
					srow["health"] = 0
					srow["sold_by_script"] = true
					w.sim.structures[sid] = srow
					w.sim._emit_event("structure.sold", 0, sid, {"team": team_id})
					return true
		if not w.sim.match_script_flags.has("foundation_sells"):
			w.sim.match_script_flags["foundation_sells"] = {}
		var sells: Dictionary = w.sim.match_script_flags["foundation_sells"]
		sells["%s:%s" % [team_id, foundation]] = true
		w.sim.match_script_flags["foundation_sells"] = sells
		return true


	func create_reinforcement_team(
		player: String, team: String, target: Dictionary
	) -> bool:
		## Spawn a reinforcement army only when `team` names an authored unit
		## rule (no synthetic 100-hp fallback). Tracked for remove_reinforcement_army.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.create_reinforcement_team", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command(
				"ai.create_reinforcement_team", String(resolved["reason"])
			)
		var team_id := int(resolved["team"])
		var army := team.strip_edges()
		if army == "":
			return _refuse_command("ai.create_reinforcement_team", "empty army/team name")
		# Fail-closed: army name must resolve to a configured unit rule.
		var unit_rules: Dictionary = w.sim._rules.get("unit_rules", {}) as Dictionary
		if not unit_rules.has(army) and not w.sim._unit_production_rules.has(army):
			return _refuse_command(
				"ai.create_reinforcement_team",
				"no authored unit rule for reinforcement army '%s'" % army
			)
		var at := Vector2.ZERO
		var kind := int(target.get("kind", -1))
		if kind == SageScriptWorld.TargetKind.POSITION:
			at = RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO))
		elif kind == SageScriptWorld.TargetKind.WAYPOINT:
			var wp := String(target.get("name", ""))
			if not w.sim.script_waypoints.has(wp):
				return _refuse_command(
					"ai.create_reinforcement_team", "waypoint missing"
				)
			at = w.sim.script_waypoints[wp]
		elif target.has("position"):
			at = RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO))
		var eid := w.sim.spawn_script_object(army, team_id, at)
		if eid <= 0:
			return _refuse_command(
				"ai.create_reinforcement_team",
				"spawn refused for authored unit type '%s'" % army
			)
		if w.sim.entities.has(eid):
			var row: Dictionary = w.sim.entities[eid]
			row["reinforcement_army"] = army
			row["reinforcement_player_team"] = team_id
			w.sim.entities[eid] = row
		w.sim.parity.living_world_commands.append({
			"op": "create_reinforcement_team",
			"army": army,
			"team": team_id,
			"entity_id": eid,
			"tick": w.sim.tick_index,
		})
		if w._bound_team(army) < 0:
			w.bind_team(army, team_id)
		return true

	func remove_reinforcement_army(player: String, army: String) -> bool:
		## Remove tracked reinforcement entities for army name owned by player.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.remove_reinforcement_army", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command(
				"ai.remove_reinforcement_army", String(resolved["reason"])
			)
		var team_id := int(resolved["team"])
		var army_name := army.strip_edges()
		var removed := 0
		var kill: Array = []
		for eid in w.sim.entities.keys():
			var row: Dictionary = w.sim.entities[eid]
			if String(row.get("reinforcement_army", "")) != army_name:
				continue
			if int(row.get("reinforcement_player_team", row.get("team", -1))) != team_id:
				continue
			kill.append(int(eid))
		for eid in kill:
			if w.sim.entities.has(eid):
				var row2: Dictionary = w.sim.entities[eid]
				row2["health"] = 0
				row2["state"] = "dead"
				w.sim.entities[eid] = row2
				removed += 1
		w.sim.parity.living_world_commands.append({
			"op": "remove_reinforcement_army",
			"army": army_name,
			"team": team_id,
			"removed": removed,
			"tick": w.sim.tick_index,
		})
		if removed <= 0:
			return _refuse_command(
				"ai.remove_reinforcement_army",
				"no reinforcement army '%s' tracked for player" % army_name
			)
		return true

	func move_to_approach_path(scope: int, name: String, path: int) -> bool:
		## Move living units along a numbered approach path if waypoint path
		## "approach_%d" exists; otherwise record approach intent on each unit.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.move_to_approach_path", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("ai.move_to_approach_path", String(resolved["reason"]))
		var path_name := "approach_%d" % path
		var dest := Vector2.ZERO
		var have_dest := false
		if w.sim.script_waypoint_paths.has(path_name):
			var points: Array = w.sim.script_waypoint_paths[path_name]
			if not points.is_empty() and w.sim.script_waypoints.has(String(points[0])):
				dest = w.sim.script_waypoints[String(points[0])]
				have_dest = true
		var ids: Array[int] = []
		for eid_value in resolved["ids"] as Array:
			var eid := int(eid_value)
			ids.append(eid)
			if w.sim.entities.has(eid):
				var row: Dictionary = w.sim.entities[eid]
				row["approach_path"] = path
				w.sim.entities[eid] = row
		if have_dest and not ids.is_empty():
			w.sim.issue_move(ids, dest, "order.approach", int(resolved.get("team", -1)))
		return not ids.is_empty()


	func skirmish_build(
		player: String, building_type: String, placement: String, count: int
	) -> bool:
		## Queue N expansion builds of building_type for the player when modeled.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("ai.skirmish_build", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("ai.skirmish_build", String(resolved["reason"]))
		var team_id := int(resolved["team"])
		var kind: String = w.sim.expansion_kind_for_object_id(building_type)
		var built := 0
		if kind != "":
			var base_id := w.sim.fortress_id(team_id)
			if base_id <= 0:
				for sid in w.sim.living_structure_ids(team_id):
					if w.sim.expansion_pads.has(int(sid)):
						base_id = int(sid)
						break
			if base_id > 0:
				for _i in range(maxi(1, count)):
					var result: Dictionary = w.sim.issue_expansion_construct(
						team_id, base_id, kind
					)
					if bool(result.get("ok", false)):
						built += 1
					else:
						break
		if built <= 0:
			if not w.sim.match_script_flags.has("skirmish_builds"):
				w.sim.match_script_flags["skirmish_builds"] = {}
			var table: Dictionary = w.sim.match_script_flags["skirmish_builds"]
			table[team_id] = {
				"type": building_type, "placement": placement, "count": count
			}
			w.sim.match_script_flags["skirmish_builds"] = table
		return true


class SliceTerrain:
	extends SageScriptWorld.Terrain

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func set_buildability(object_type: String, buildability: int) -> bool:
		## Mutable tech-tree buildability override for an object type.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("terrain.set_buildability", "no simulation attached")
		if w.sim.winner != -1:
			return _refuse_command(
				"terrain.set_buildability", "the match is already resolved"
			)
		var result: Dictionary = w.sim.set_tech_buildability(
			object_type, buildability
		)
		if not bool(result.get("ok", false)):
			return _refuse_command(
				"terrain.set_buildability", String(result.get("reason", ""))
			)
		return true



	func bridge_state(bridge: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("terrain.bridge_state", "no simulation attached")
		return SageWorldQuery.hit(
			String(w.sim.surface_bag_get("bridge:%s" % bridge, "intact"))
		)


	func set_burn_rate(target: Dictionary, rate: float, max_burn: float) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("terrain.set_burn_rate", "no simulation attached")
		w.sim._ensure_parity()
		w.sim.parity.emit_presentation(
			w.sim,
			"terrain.burn_rate",
			{"target": target.duplicate(true), "rate": rate, "max": max_burn}
		)
		# Mark target cell impassable when max burn extinguishes path (slice).
		if max_burn <= 0.0 and target.has("position"):
			w.sim.parity.set_path_impassable(
				RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO)),
				true
			)
		return true


	func set_cloud_speed(multiplier: float) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("terrain.set_cloud_speed", "no simulation attached")
		w.sim._ensure_parity()
		w.sim.parity.emit_presentation(
			w.sim, "terrain.cloud_speed", {"multiplier": multiplier}
		)
		return true

	func set_logic_fog_state(state: String) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("terrain.set_logic_fog_state", "no simulation attached")
		w.sim._ensure_parity()
		var folded := state.strip_edges().to_lower()
		w.sim.parity.fog_border_shroud = folded in ["on", "true", "1", "enabled", "shroud"]
		w.sim.parity.emit_presentation(
			w.sim, "terrain.logic_fog", {"state": state}
		)
		return true

	func set_water_height(water: String, height: float, duration_ticks: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("terrain.set_water_height", "no simulation attached")
		w.sim._ensure_parity()
		w.sim.parity.emit_presentation(
			w.sim,
			"terrain.water_height",
			{"water": water, "height": height, "duration": duration_ticks}
		)
		return true


	func switch_border(border_index: int) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("terrain.switch_border", "no simulation attached")
		w.sim._ensure_parity()
		w.sim.parity.emit_presentation(
			w.sim, "terrain.border", {"border_index": border_index}
		)
		return true

class SliceAreas:
	extends SageScriptWorld.Areas

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func exists(area: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.exists", "no simulation attached")
		return SageWorldQuery.hit(w.sim.script_areas.has(area))

	func contains(area: String, position: Vector3) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.contains", "no simulation attached")
		var answer: Dictionary = w.sim.area_contains(area, RetailSliceScriptWorld._sim_point(position))
		if not bool(answer.get("ok", false)):
			return _refuse_query("areas.contains", String(answer.get("reason", "")))
		return SageWorldQuery.hit(bool(answer.get("value", false)))

	func set_human_impassable(area: String, impassable: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("areas.set_human_impassable", "no simulation attached")
		if not w.sim.script_areas.has(area):
			return _refuse_command("areas.set_human_impassable", "area '%s' is not registered" % area)
		var row: Dictionary = w.sim.script_areas[area]
		row["impassable"] = impassable
		w.sim.script_areas[area] = row
		return true

	func waypoint_path_exists(path: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.waypoint_path_exists", "no simulation attached")
		return SageWorldQuery.hit(w.sim.script_waypoint_paths.has(path))

	func waypoint_position(waypoint: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.waypoint_position", "no simulation attached")
		if not w.sim.script_waypoints.has(waypoint):
			return _refuse_query("areas.waypoint_position", "waypoint '%s' is not registered" % waypoint)
		return SageWorldQuery.hit(
			RetailSliceScriptWorld._world_point(w.sim.script_waypoints[waypoint])
		)

	func member_count(scope: int, name: String, area: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.member_count", "no simulation attached")
		if not w.sim.script_areas.has(area):
			return _refuse_query("areas.member_count", "area '%s' is not registered" % area)
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_query("areas.member_count", String(resolved["reason"]))
		var count := 0
		for eid in resolved["ids"] as Array:
			var pos: Vector2 = (w.sim.entities[int(eid)] as Dictionary).get("position", Vector2.ZERO)
			var answer: Dictionary = w.sim.area_contains(area, pos)
			if bool(answer.get("ok", false)) and bool(answer.get("value", false)):
				count += 1
		return SageWorldQuery.hit(count)

	func unit_count_in_area(player: String, area: String, filter: Dictionary) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.unit_count_in_area", "no simulation attached")
		if not w.sim.script_areas.has(area):
			return _refuse_query("areas.unit_count_in_area", "area '%s' is not registered" % area)
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("areas.unit_count_in_area", String(resolved["reason"]))
		var count := 0
		for eid in w.sim.living_ids(int(resolved["team"])):
			var pos: Vector2 = (w.sim.entities[int(eid)] as Dictionary).get("position", Vector2.ZERO)
			var answer: Dictionary = w.sim.area_contains(area, pos)
			if bool(answer.get("ok", false)) and bool(answer.get("value", false)):
				count += 1
		return SageWorldQuery.hit(count)

	func transition_count(scope: int, name: String, area: String, entered: bool) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.transition_count", "no simulation attached")
		var key := "area_transition:%s:%s:%s:%s" % [scope, name, area, entered]
		return SageWorldQuery.hit(w.sim.script_event_count(key))



	func is_on_fire(area: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.is_on_fire", "no simulation attached")
		return SageWorldQuery.hit(w.sim.surface_bag_bool("area_fire:%s" % area, false))

	func tree_count(area: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.tree_count", "no simulation attached")
		return SageWorldQuery.hit(w.sim.surface_bag_int("trees:%s" % area, 0))


	func object_inside_base(
		object_or_type: String, base_reference: String, is_type: bool
	) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.object_inside_base", "no simulation attached")
		return SageWorldQuery.hit(
			w.sim.surface_bag_bool(
				"inside_base:%s:%s:%s" % [object_or_type, base_reference, is_type], false
			)
		)

	func base_transition_count(
		scope: int, name: String, base_reference: String, entered: bool, entirely: bool
	) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("areas.base_transition_count", "no simulation attached")
		var key := "base_transition:%s:%s:%s:%s:%s" % [
			scope, name, base_reference, entered, entirely
		]
		return SageWorldQuery.hit(w.sim.script_event_count(key))


class SliceTransport:
	extends SageScriptWorld.Transport

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func passenger_count(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("transport.passenger_count", "no simulation attached")
		var view := w.named_object_view(object_name)
		if view.is_empty() or int(view.get("structure_id", 0)) <= 0:
			return _refuse_query("transport.passenger_count", "'%s' is not a structure" % object_name)
		return SageWorldQuery.hit(w.sim.passenger_count(int(view["structure_id"])))

	func garrisoned_count(player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("transport.garrisoned_count", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("transport.garrisoned_count", String(resolved["reason"]))
		var count := 0
		for eid in w.sim.living_ids(int(resolved["team"])):
			if w.sim.entity_container.has(int(eid)):
				count += 1
		return SageWorldQuery.hit(count)

	func captured_unit_count(player: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("transport.captured_unit_count", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_query("transport.captured_unit_count", String(resolved["reason"]))
		return SageWorldQuery.hit(0)

	func has_toggled_weapon(object_name: String) -> SageWorldQuery:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_query("transport.has_toggled_weapon", "no simulation attached")
		if object_name.is_valid_int() and w.sim.entities.has(int(object_name)):
			return SageWorldQuery.hit(w.sim.entity_bool_flag(int(object_name), "close_range_weapon"))
		return _refuse_query(
			"transport.has_toggled_weapon", "'%s' is not a live entity id" % object_name
		)

	func garrison(scope: int, name: String, target: Dictionary, instantly: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("transport.garrison", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("transport.garrison", String(resolved["reason"]))
		var view := w.named_object_view(String(target.get("name", "")))
		if view.is_empty() or int(view.get("structure_id", 0)) <= 0:
			return _refuse_command("transport.garrison", "garrison target is not a structure")
		var sid := int(view["structure_id"])
		for eid in resolved["ids"] as Array:
			w.sim.contain_entity(sid, int(eid))
		return true

	func load_transports(team: String) -> bool:
		## Load living team entities into nearest structures that declare
		## transport_capacity (or parity kind table) via can_load_entity +
		## contain_entity. Structures with capacity 0 are skipped.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("transport.load_transports", "no simulation attached")
		w.sim._ensure_parity()
		var team_id := w._bound_team(team)
		if team_id < 0:
			var player_r: Dictionary = w._resolve_single_player_team(team)
			if player_r.has("reason"):
				return _refuse_command(
					"transport.load_transports", String(player_r["reason"])
				)
			team_id = int(player_r["team"])
		var member_ids: Array = w.sim.living_ids(team_id)
		var loaded := 0
		for eid_value in member_ids:
			var eid := int(eid_value)
			if not w.sim.entities.has(eid):
				continue
			if w.sim.entity_container.has(eid):
				continue
			var epos: Vector2 = (w.sim.entities[eid] as Dictionary).get("position", Vector2.ZERO)
			var best_sid := -1
			var best_d := INF
			for sid in w.sim.living_structure_ids(team_id):
				var can: Dictionary = w.sim.parity.can_load_entity(w.sim, int(sid), eid)
				if not bool(can.get("ok", false)):
					continue
				var spos: Vector2 = (w.sim.structures[int(sid)] as Dictionary).get(
					"position", Vector2.ZERO
				)
				var d := epos.distance_to(spos)
				if d < best_d:
					best_d = d
					best_sid = int(sid)
			if best_sid < 0:
				continue
			var result: Dictionary = w.sim.contain_entity(best_sid, eid)
			if bool(result.get("ok", false)):
				loaded += 1
		if loaded <= 0:
			return _refuse_command(
				"transport.load_transports",
				"no transport-capable structure could load team members"
			)
		return true

	func teleport_to(scope: int, name: String, target: Dictionary) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("transport.teleport_to", "no simulation attached")
		var resolved := w._living_ids_for_order_scope(scope, name)
		if resolved.has("reason"):
			return _refuse_command("transport.teleport_to", String(resolved["reason"]))
		var pos := Vector2.ZERO
		var kind := int(target.get("kind", -1))
		if kind == SageScriptWorld.TargetKind.POSITION:
			pos = RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO))
		elif kind == SageScriptWorld.TargetKind.WAYPOINT:
			var wp := String(target.get("name", ""))
			if not w.sim.script_waypoints.has(wp):
				return _refuse_command("transport.teleport_to", "waypoint missing")
			pos = w.sim.script_waypoints[wp]
		else:
			return _refuse_command(
				"transport.teleport_to", "only POSITION/WAYPOINT targets are answerable"
			)
		for eid in resolved["ids"] as Array:
			w.sim.script_teleport_entity(int(eid), pos)
		return true

	func capture_nearest_unowned(team: String) -> bool:
		## Capture nearest neutral/unowned structure into the team's owner.
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("transport.capture_nearest_unowned", "no simulation attached")
		var team_r := w.resolve_script_team_name(team)
		if team_r.has("reason"):
			# Fall back to player/team name as player seat.
			var player_r := w._resolve_single_player_team(team)
			if player_r.has("reason"):
				return _refuse_command(
					"transport.capture_nearest_unowned", String(team_r["reason"])
				)
			team_r = {"team": int(player_r["team"])}
		var owner := int(team_r["team"])
		var origin := Vector2.ZERO
		var living: Array = w.sim.living_ids(owner)
		if not living.is_empty() and w.sim.entities.has(int(living[0])):
			origin = (w.sim.entities[int(living[0])] as Dictionary).get("position", Vector2.ZERO)
		var best_sid := -1
		var best_d := INF
		for sid in w.sim.structures.keys():
			var srow: Dictionary = w.sim.structures[sid]
			if int(srow.get("health", 0)) <= 0:
				continue
			var steam := int(srow.get("team", -1))
			if steam == owner:
				continue
			# Unowned / neutral / creep capturable only.
			if (
				steam != RetailSliceSim.NEUTRAL_TEAM
				and steam != RetailSliceSim.CREEP_TEAM
				and steam >= 0
			):
				if not bool(srow.get("capturable", false)):
					continue
			var d: float = origin.distance_to(srow.get("position", Vector2.ZERO))
			if d < best_d:
				best_d = d
				best_sid = int(sid)
		if best_sid < 0:
			return _refuse_command(
				"transport.capture_nearest_unowned", "no capturable structure found"
			)
		var captured: Dictionary = w.sim.structures[best_sid]
		captured["team"] = owner
		captured["captured_by_script"] = true
		w.sim.structures[best_sid] = captured
		w.sim._emit_event("structure.captured", 0, best_sid, {"team": owner})
		return true

	func create_team_from_captured(player: String, team: String) -> bool:
		## Register a script team for the player listing recently captured
		## structures as members (hash-backed membership handles).
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("transport.create_team_from_captured", "no simulation attached")
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command(
				"transport.create_team_from_captured", String(resolved["reason"])
			)
		var team_id := int(resolved["team"])
		var army := team.strip_edges()
		if army == "":
			return _refuse_command("transport.create_team_from_captured", "empty team name")
		var handles: Array = []
		for sid in w.sim.structures.keys():
			var srow: Dictionary = w.sim.structures[sid]
			if int(srow.get("team", -1)) != team_id:
				continue
			if not bool(srow.get("captured_by_script", false)):
				continue
			handles.append({"kind": "structure", "id": int(sid)})
		var reg: Dictionary = w.sim.register_script_team(
			army, team_id, false, handles, true, [], 0, true, false
		)
		if not bool(reg.get("ok", false)):
			return _refuse_command(
				"transport.create_team_from_captured", String(reg.get("reason", "register failed"))
			)
		if w._bound_team(army) < 0:
			w.bind_team(army, team_id)
		return true


class SlicePresentationSink:
	extends SageScriptWorld.PresentationSink

	func _init(channel_name: String) -> void:
		super(channel_name)

	func emit(op: String, values: Array) -> bool:
		## Presentation is one-way: authoritative presentation log + event.
		var w := world as RetailSliceScriptWorld
		if w == null or w.sim == null:
			return _refuse_command("%s.emit" % channel, "no simulation attached")
		w.sim._ensure_parity()
		w.sim.parity.emit_presentation(
			w.sim,
			channel,
			{"op": op, "values": values.duplicate()}
		)
		return true


class SliceFog:
	extends SageScriptWorld.Fog

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

	func _sim_radius(w: RetailSliceScriptWorld, source_radius: float) -> float:
		## THE UNIT TRAP, and it is a real one.
		##
		## `_fog_target_center_radius` converts the target's CENTRE into sim
		## space (via `_sim_point`) but returns the authored radius in SOURCE
		## units, untouched. On the legacy parity grid that mismatch was merely
		## invisible - its cells are 1887 source units across, so any radius at
		## all lit roughly the same handful of cells. On the retail grid it is
		## catastrophic: an authored radius of 500 read as 500 SIM units is
		## ~18,900 source units, and one MAP_REVEAL_AT_WAYPOINT would uncover the
		## entire map for the rest of the match.
		##
		## Scaled HERE and only here, deliberately. The legacy `parity.fog_*`
		## calls above keep receiving the unscaled value because that dictionary
		## is hashed authoritative state - correcting it there would move every
		## scripted scenario's hash and is an owner decision, not a bug fix. The
		## two grids therefore disagree about this radius on purpose, and the
		## legacy one is the one that is wrong.
		if w == null or w.sim == null:
			return source_radius
		return source_radius * float(w.sim.source_transform_scale())

	func _fog_target_center_radius(target: Dictionary) -> Dictionary:
		var center := Vector2.ZERO
		var radius := 100.0
		var kind := int(target.get("kind", -1))
		if kind == SageScriptWorld.TargetKind.POSITION:
			center = RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO))
		elif target.has("position"):
			center = RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO))
		if target.has("radius"):
			radius = float(target.get("radius", radius))
		return {"center": center, "radius": radius}

	func reveal(player: String, target: Dictionary, permanent: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("fog.reveal", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("fog.reveal", String(resolved["reason"]))
		var tr := _fog_target_center_radius(target)
		w.sim.parity.fog_reveal(
			int(resolved["team"]), tr["center"], float(tr["radius"]), permanent
		)
		# ...and the retail shroud grid, which is what the terrain overlay and
		# the minimap actually draw. Both are driven because the legacy parity
		# dictionary is hashed state that other systems already read, while the
		# shroud grid is the presented one; dropping either would make a map
		# script silently half-work. The reveal is keyed by the PLAYER NAME so
		# MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT has a name to remove, which is
		# how retail keys permanent reveals (a MapRevealName per record).
		w.sim.fog_of_war().reveal(
			int(resolved["team"]),
			tr["center"],
			_sim_radius(w, float(tr["radius"])),
			permanent,
			String(target.get("reveal_name", player))
		)
		return true

	func shroud(player: String, target: Dictionary) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("fog.shroud", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("fog.shroud", String(resolved["reason"]))
		var tr := _fog_target_center_radius(target)
		w.sim.parity.fog_shroud(int(resolved["team"]), tr["center"], float(tr["radius"]))
		w.sim.fog_of_war().shroud(
			int(resolved["team"]), tr["center"], _sim_radius(w, float(tr["radius"]))
		)
		return true

	func undo_permanent_reveal(player: String, target: Dictionary) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("fog.undo_permanent_reveal", "no simulation attached")
		w.sim._ensure_parity()
		var resolved := w._resolve_single_player_team(player)
		if resolved.has("reason"):
			return _refuse_command("fog.undo_permanent_reveal", String(resolved["reason"]))
		var tr := _fog_target_center_radius(target)
		w.sim.parity.fog_undo_permanent(
			int(resolved["team"]), tr["center"], float(tr["radius"])
		)
		var undo_team := int(resolved["team"])
		var undo_name := String(target.get("reveal_name", player))
		# Try the NAMED undo first (retail's own keying), and fall back to the
		# positional form for a target that carries no name. Never both: a named
		# hit that also ran the positional sweep would drop unrelated reveals
		# that merely happen to sit near the same waypoint.
		if not w.sim.fog_of_war().undo_permanent_reveal_named(undo_team, undo_name):
			w.sim.fog_of_war().undo_permanent_reveal(
				undo_team, tr["center"], _sim_radius(w, float(tr["radius"]))
			)
		return true

	func set_border_shroud(enabled: bool) -> bool:
		var w := _world()
		if w == null or w.sim == null:
			return _refuse_command("fog.set_border_shroud", "no simulation attached")
		w.sim._ensure_parity()
		w.sim.parity.fog_border_shroud = enabled
		w.sim.fog_of_war().border_shroud = enabled
		return true


