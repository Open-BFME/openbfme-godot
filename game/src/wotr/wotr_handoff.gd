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
## VERSION 2 carries what an AUTO-RESOLVED battle needs and a tactical one does
## not: each side's handicap rung and its unit roster with retail's auto-resolve
## blocks and current hitpoints, plus the campaign's battle-type rules. All of
## it is derived from authoritative state, so the brief stays a pure function of
## the state it was built from and its digest stays the chain link between the
## strategic and tactical hashes.
##
## VERSION 3 closes the STRATEGIC half of three register entries by carrying:
##   * the region's territory membership plus BOTH sides' unified-territory
##     rosters and summed bonuses (`region_bonus_modifiers`' strategic half);
##   * the region's standing strategic buildings, verbatim tokens, alongside
##     the fort flag and purse the brief already carried (`prebuilt_fortress`'
##     strategic half);
##   * each hero army's campaign level from the hero ledger and each side's
##     fallen heroes (`carried_hero_state`'s strategic half);
##   * `data_gaps`, naming the retail data the document does NOT record, so the
##     tactical side can never mistake "not carried" for "does not exist".
const SCHEMA_VERSION := 3

## Named capabilities a real battle needs that the retail-slice simulation does
## not expose today. These are requirements, not excuses: each one is a thing a
## tactical sim must grow before War of the Ring battles are playable.
##
## * `reinforcement_schedule` - retail feeds armies in one at a time,
##   `SecondsPerReinforcement` apart (900s in BFME2, 300s in RotWK). The slice
##   spawns its whole roster at match start.
## * `battle_outcome_report` - the strategic layer needs the surviving roster
##   back, not just a winner, to write armies back into the region.
## `battle_outcome_report` has LEFT this list. It named the gap that the
## strategic layer needed the surviving roster back, not just a winner - and
## that is exactly what auto-resolve now returns and `apply_attrition()` now
## writes. The gap remains open for TACTICAL battles, and is named separately as
## `tactical_battle_outcome_report` so the two cannot be confused: an
## auto-resolved battle reports survivors, a fought one still reports a boolean.
##
## `carried_hero_state`, `prebuilt_fortress` and `region_bonus_modifiers` have
## LEFT this list the same way, under the same renaming discipline: the brief
## now CARRIES each of them (schema version 3 above says exactly what), so the
## strategic half of each gap is closed and asserted closed by the strategic
## runner. What remains open is CONSUMPTION - the tactical sim still starts
## every hero fresh, still builds no authored fortress or building, and still
## applies no region or territory bonus - and each remainder is named with a
## `tactical_` prefix so it can never be confused with the closed half:
##
## * `tactical_carried_hero_state` - the brief's `hero_level` and
##   `fallen_heroes` must set each hero's starting level and revival ledger in
##   the match, and the level must come back out in the outcome report.
## * `tactical_prebuilt_fortress` - the brief's `has_fort` and
##   `standing_buildings` must become a standing fortress (and, once the LW_*
##   macro expansions are recorded, standing buildings) on the battlefield; the
##   reduced purse already arrives via `settings.starting_cash`.
## * `tactical_region_bonus_modifiers` - the brief's region `bonuses`,
##   `bonus_macros` and each side's `territory_bonuses` must reach the tactical
##   (and auto-resolve) combat rules.
const UNSUPPORTED_BY_TACTICAL_SIM := [
	"reinforcement_schedule",
	"tactical_battle_outcome_report",
	"tactical_carried_hero_state",
	"tactical_prebuilt_fortress",
	"tactical_region_bonus_modifiers",
]

## Named RETAIL DATA this brief cannot carry because the living-world document
## (and the gamedata macro table) does not record it. FAIL-CLOSED companions to
## the capability list above: each names something a parity implementation
## would need, states where the hole is, and is carried in the brief so the
## tactical side reads the absence instead of assuming a default.
##
## * `early_battle_hero_level_cap` - retail caps a WOTR hero's level in early
##   battles; no cap ladder appears anywhere in the document or the gamedata
##   `#define` table, so heroes carry their exact ledger level uncapped and the
##   cap remains unimplementable without new extraction.
## * `strategic_hero_revival` - the document authors the RTS-side revival
##   multipliers (`initial_revival_cost_milli`, `initial_revival_time_milli`,
##   both in `settings`) but NO strategic-turn revival rule, so a fallen hero
##   stays fallen and re-placing one is refused by name.
## * `lw_building_macro_expansion` - the buildings retail pre-places on the
##   strategic map are `LW_FORT`/`LW_BARRACKS`/`LW_ARMORY`/`LW_FARM`
##   preprocessor macros the importer recorded as unexpandable gaps; the brief
##   carries the TOKENS verbatim in `standing_buildings` and interprets none of
##   them - in particular a standing `LW_FORT` token does NOT set `has_fort`,
##   because equating the two would be a reading retail never wrote down.
## * `fortress_object_template` - `has_fort` says a fortress STANDS but cannot
##   say WHICH: the region's `fortress` block records a display name and a
##   portrait, not an object template, and no faction->fortress mapping appears
##   in the document. Auto-resolve's fortress-combatant consumer (see
##   `UNMODELLED_RETAIL_BEHAVIOUR.fortress_combatant` in
##   `wotr_autoresolve_battle.gd`) needs a template its bindings can answer
##   for, and deriving `MenFortress` from `FactionMen` would be a guessed
##   mapping wearing a lookup's costume.
const UNRECORDED_BY_LIVING_WORLD_DATA := [
	"early_battle_hero_level_cap",
	"fortress_object_template",
	"lw_building_macro_expansion",
	"strategic_hero_revival",
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
		# THE CAMPAIGN'S BATTLE RULES, carried so `configure()` can record them
		# in the commitment. They come from authoritative state, never from a UI
		# argument, which is what keeps them inside both hashes.
		"battle_type": state.battle_type,
		"battle_type_priority": state.battle_type_priority,
		"region": {
			"id": region_id,
			"map_name": String(region.get("map_name", "")),
			"display_name": String(region.get("display_name", "")),
			"still_image": String(region.get("skirmish_still_image", "")),
			"music_track": String(region.get("skirmish_music_track", "")),
			"cp_limit": world.region_cp_limit(region_id),
			"ally_cp_limit": world.region_ally_cp_limit(region_id),
			"bonuses": region.get("bonuses", {}),
			# The bonuses retail authored as MACROS (`FERTILE_TERRITORY_BONUS`),
			# carried symbolically exactly as the world carries them; resolving
			# them is the macro table's job, and an absent table must read as
			# "unresolved", never as zero.
			"bonus_macros": region.get("bonus_macros", {}),
			# The territory this region belongs to, so the tactical side can say
			# WHY a territory bonus applies. "" for a region outside every group.
			"territory": String(world.territory_of(region_id).get("effect_name", "")),
			"has_fort": has_fort,
			# STANDING STRATEGIC BUILDINGS, from authoritative state, verbatim
			# LW_* tokens (see `lw_building_macro_expansion` in the data-gap
			# register - they are carried, not interpreted).
			"standing_buildings": state.buildings_in_region(region_id),
			"building_spot_count": int(region.get("building_spot_count", 0)),
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
		"data_gaps": UNRECORDED_BY_LIVING_WORLD_DATA,
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
			"handicap": 0,
			"staging_region": region_id,
			"armies": [],
			"command_points": 0,
			"unified_territories": PackedStringArray(),
			"territory_bonuses": {},
			"fallen_heroes": [],
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
			# THE HERO'S CAMPAIGN LEVEL, from the ledger the strategic hash
			# covers - the level this hero must START the battle at, uncapped
			# because no early-battle cap ladder is recorded anywhere (see
			# `early_battle_hero_level_cap` in the data-gap register). 0 for a
			# garrison army, which has no hero to carry.
			"hero_level": state.hero_level(String(army.get("hero_template", ""))),
			"entries": roster.get("entries", []),
			"command_points": int(army.get("command_points", 0)),
			# THE ARMY AS IT STANDS RIGHT NOW, wounds and all. `entries` is the
			# roster it was RAISED from and never changes; `units` is what is
			# actually left of it, and that is what an auto-resolved battle
			# fights with. Two armies raised from the same roster that have
			# fought different battles are different armies, and only this field
			# can tell them apart.
			"units": army.get("units", []),
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
		# The seat's handicap rung, from authoritative state for the same reason
		# `controller` is: it scales auto-resolve combat and seeds the dice.
		"handicap": StateScript.normalized_handicap(seat.get("handicap", 0)),
		"staging_region": region_id,
		"armies": rows,
		"command_points": total,
		# THIS SIDE'S UNIFIED TERRITORIES and their summed bonuses, derived from
		# the same hashed ownership map everything else here derives from. The
		# names ride alongside the totals so the tactical side can report WHICH
		# unification granted what, not merely a number.
		"unified_territories": world.unified_territories(state.region_owner, player),
		"territory_bonuses": world.territory_bonus_totals(state.region_owner, player),
		# The seat's fallen heroes: name, the level they fell at, the turn they
		# fell. What a revival rule would consume, carried so the tactical side
		# can at least refuse to field them - a fallen hero arriving fresh would
		# be the silent resurrection this ledger exists to prevent.
		"fallen_heroes": state.fallen_heroes(player),
	}
