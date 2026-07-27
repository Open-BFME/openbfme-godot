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
## Named single objects ("NAMED_..." vocabulary) have no binding at all: sim
## entity rows carry display names, not script identities, so every
## object-name-keyed method refuses. That is the single largest gap.
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


# ==========================================================================
# PLAYERS
# ==========================================================================


class SlicePlayers:
	extends SageScriptWorld.Players

	## Implemented: exists, faction, the three command-point reads,
	## building_count, relation_to. Everything else refuses - including
	## can_build_at_base, whose base signature is missing the base argument
	## (reported defect; do not implement against a wrong slot).

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

	## Implemented: move_to, attack_move_to (POSITION targets), attack (TEAM
	## targets) and stand_ground, for TEAM and PLAYER scopes. UNIT scope needs
	## the missing object-name binding; waypoint/area/named-object targets
	## need map geometry the sim does not model.
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
		if int(target.get("kind", -1)) != SageScriptWorld.TargetKind.POSITION:
			return _refuse_command(
				"orders.move_to",
				"only explicit POSITION targets are answerable (no waypoint/area/object geometry)"
			)
		var w := _world()
		var team := int(resolved["team"])
		w.sim.issue_move(
			w.sim.living_ids(team),
			RetailSliceScriptWorld._sim_point(target.get("position", Vector3.ZERO)),
			"order.move",
			team
		)
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

	## Implemented: player_count and multiplayer_outcome. set_time_frozen is
	## deliberately refused even though the sim has clock_paused: freezing it
	## also freezes whatever drives the script layer, so a scripted UNFREEZE
	## could never run - a deadlock, which is worse than a refusal (finding).

	func _world() -> RetailSliceScriptWorld:
		return world as RetailSliceScriptWorld

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
