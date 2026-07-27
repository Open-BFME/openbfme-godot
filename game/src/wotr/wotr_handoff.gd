extends RefCounted

## The SHAPE of a strategic->tactical handoff.
##
## When a War of the Ring army attacks a region, the strategic layer has to hand
## the tactical simulation a complete, self-contained brief: which map, which
## armies on which side, and which battle settings. This file builds that brief
## as a plain dictionary and nothing else. It does not start a battle, does not
## load a map, and does not touch `retail_slice_sim.gd` - by design, because the
## tactical side of this contract does not exist yet.
##
## Two properties make that useful rather than speculative:
##
## 1. The request is DERIVED, not authored. Everything in it comes from the
##    world data and the authoritative state, so the same state produces the
##    same request every time and it can be hashed alongside a state pin.
## 2. The request is HONEST about the other side of the contract.
##    `unsupported` lists, by name, every part of the brief the tactical
##    simulation cannot honour today. That list is the actual work item, and a
##    test asserts on it so it can never quietly rot.

const WorldScript = preload("res://src/wotr/wotr_world.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")

const SCHEMA := "openbfme.wotr-battle-request"
const SCHEMA_VERSION := 1

## Named capabilities a real battle needs that the retail-slice simulation does
## not expose today. These are requirements, not excuses: each one is a thing a
## tactical sim must grow before War of the Ring battles are playable.
##
## * `reinforcement_schedule` - retail feeds armies in one at a time,
##   `SecondsPerReinforcement` apart (900s in BFME2, 300s in RotWK). The slice
##   spawns its whole roster at match start.
## * `carried_hero_state` - a WOTR hero keeps its level and its revive timer
##   between battles, and early battles cap that level. The slice starts every
##   hero fresh.
## * `prebuilt_fortress` - a region with `CreateAutoFort` (or a built fort)
##   starts the battle with a standing fortress and the reduced
##   `StartingCashRTSWithFort` purse.
## * `battle_outcome_report` - the strategic layer needs the surviving roster
##   back, not just a winner, to write armies back into the region.
## * `region_bonus_modifiers` - per-region and unified-territory attack/defence/
##   experience/resource bonuses have to reach the tactical rules.
const UNSUPPORTED_BY_TACTICAL_SIM := [
	"battle_outcome_report",
	"carried_hero_state",
	"prebuilt_fortress",
	"region_bonus_modifiers",
	"reinforcement_schedule",
]


## Build the brief for `attacker` attacking `region_id`. Returns `{}` when the
## attack is not legal in the current state - a caller must never be handed a
## half-formed battle.
static func build_request(
	world: WorldScript,
	state: StateScript,
	attacker: int,
	region_id: String
) -> Dictionary:
	if world == null or state == null:
		return {}
	if not world.has_region(region_id):
		return {}
	if not state.can_attack(attacker, region_id):
		return {}

	var region := world.region(region_id)
	var defender := state.owner_of(region_id)
	var staging := _staging_region(world, state, attacker, region_id)
	if staging.is_empty():
		return {}

	var has_fort := bool(region.get("create_auto_fort", false)) or bool(region.get("has_fortress", false))
	var settings := world.rts_settings
	var starting_cash := int(settings.get("starting_cash_rts", -1))
	if has_fort:
		starting_cash = int(settings.get("starting_cash_rts_with_fort", starting_cash))

	return {
		"schema": SCHEMA,
		"schema_version": SCHEMA_VERSION,
		"turn_index": state.turn_index,
		"region": {
			"id": region_id,
			"map_name": String(region.get("map_name", "")),
			"display_name": String(region.get("display_name", "")),
			"still_image": String(region.get("skirmish_still_image", "")),
			"music_track": String(region.get("skirmish_music_track", "")),
			"cp_limit": world.region_cp_limit(region_id),
			"ally_cp_limit": world.region_ally_cp_limit(region_id),
			"bonuses": region.get("bonuses", {}),
			"has_fort": has_fort,
		},
		"attacker": _side(world, state, attacker, staging),
		"defender": _side(world, state, defender, region_id),
		"settings": {
			"seconds_per_reinforcement": int(settings.get("seconds_per_reinforcement", -1)),
			"starting_cash": starting_cash,
			"initial_revival_cost_milli": int(settings.get("initial_revival_cost_milli", -1)),
			"initial_revival_time_milli": int(settings.get("initial_revival_time_milli", -1)),
		},
		"unsupported": UNSUPPORTED_BY_TACTICAL_SIM,
	}


## The attacker's staging region: the lowest-sorted adjacent region they own
## that actually has an army in it. Lowest-sorted rather than "nearest" or
## "strongest" so the choice is reproducible without a tie-break heuristic.
static func _staging_region(
	world: WorldScript,
	state: StateScript,
	attacker: int,
	region_id: String
) -> String:
	for neighbour in world.neighbours(region_id):
		if state.owner_of(neighbour) != attacker:
			continue
		if not state.armies_in_region(neighbour).is_empty():
			return neighbour
	return ""


static func _side(
	world: WorldScript,
	state: StateScript,
	player: int,
	region_id: String
) -> Dictionary:
	if player == StateScript.NEUTRAL or player < 0 or player >= state.players.size():
		return {
			"player": StateScript.NEUTRAL,
			"faction": "",
			"team": 0,
			"controller": StateScript.CONTROLLER_AI,
			"staging_region": region_id,
			"armies": [],
			"command_points": 0,
		}
	var seat := state.players[player] as Dictionary
	var rows: Array[Dictionary] = []
	var total := 0
	for id in state.armies_in_region(region_id):
		var army := state.armies[id] as Dictionary
		if int(army.get("owner", StateScript.NEUTRAL)) != player:
			continue
		var roster: Dictionary = world.player_armies.get(String(army.get("roster", "")), {})
		rows.append({
			"army_id": id,
			"kind": String(army.get("kind", "")),
			"hero_template": String(army.get("hero_template", "")),
			"entries": roster.get("entries", []),
			"command_points": int(army.get("command_points", 0)),
		})
		total += int(army.get("command_points", 0))
	return {
		"player": player,
		"faction": String(seat.get("faction", "")),
		"team": int(seat.get("team", 0)),
		# Who drives this seat, carried from AUTHORITATIVE strategic state. The
		# tactical roster's `is_ai` is derived from this rather than from a
		# per-session "which seat am I" argument, so it is inside the brief, inside
		# the digest, and inside the strategic hash.
		"controller": StateScript.normalized_controller(seat.get("controller", StateScript.CONTROLLER_AI)),
		"staging_region": region_id,
		"armies": rows,
		"command_points": total,
	}
