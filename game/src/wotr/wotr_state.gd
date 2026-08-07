extends RefCounted

## War of the Ring AUTHORITATIVE STRATEGIC STATE: who owns what, where the
## armies are, and whose turn it is.
##
## The discipline here is deliberately the same as
## `res://src/retail_slice/retail_slice_sim.gd`, because the two will eventually
## have to agree across a network:
##
## * `state_hash()` canonicalises the authoritative dictionary (dictionaries
##   become sorted key/value rows) and SHA-256s it. Two peers that applied the
##   same commands in the same order MUST produce the same string.
## * `snapshot()` / `restore()` round-trip that same dictionary through
##   `var_to_bytes` - no side channel, no view state, no cached derivation.
## * Iteration is over sorted keys, never over `Dictionary.keys()` insertion
##   order. Region ids are strings, so every sweep sorts them first.
## * There is NO RNG here at all. Combat resolution, if it is ever added, must
##   take its randomness from a caller-supplied seeded stream, not from this
##   file, or the hash stops being reproducible.
## * There are NO floats in AUTHORITATIVE STATE. Command points, turn indices
##   and army sizes are ints; pure derived reports may expose content multipliers
##   but never serialize or hash them.
##
## What is intentionally NOT here: any tactical battle, any mission scripting,
## any presentation. A strategic->tactical transition is expressed as a request
## dictionary by `wotr_handoff.gd`; resolving it is somebody else's job.

const WorldScript = preload("res://src/wotr/wotr_world.gd")
## The building vocabulary and retail's four `Type` values. Preloaded for the
## CONSTANTS only - the catalogue INSTANCE is bound from outside (see
## `building_catalogue`), so this file never opens a bundle of its own.
const BuildingsScript = preload("res://src/wotr/wotr_buildings.gd")

const SCHEMA := "openbfme.wotr-strategic-state"
const SCHEMA_VERSION := 2

# Retail's three phase titles/end-phase flow (LW_InstructionText05/06/11/12;
# APT phase titles and end-phase control). These are the only phase values that
# may enter authoritative state.
const PHASE_TACTICAL := "tactical"
const PHASE_BATTLE := "battle"
const PHASE_RETREAT := "retreat"
const PHASES := [PHASE_TACTICAL, PHASE_BATTLE, PHASE_RETREAT]

# A retreat row is deliberately small and exhaustively typed. The army remains
# authoritative in `armies`; this row says that it may only be relocated.
const PENDING_RETREAT_FIELDS := {
	"army": TYPE_INT,
	"from_region": TYPE_STRING,
	"player": TYPE_INT,
	"turn": TYPE_INT,
	"hero_template": TYPE_STRING,
}

const NEUTRAL := -1

## Who drives a seat. This is AUTHORITATIVE STRATEGIC STATE, not a session
## setting, and that placement is the whole point: it is the value the tactical
## roster's `is_ai` is derived from, so it has to be identical on every peer and
## inside the strategic hash. The retail menu path already works this way -
## `_menu_sim_team_roster()` reads a shared lobby `controller` field rather than
## a per-machine "which seat am I" number - and the normalisation rule below is
## deliberately the same one: `human` means human, anything else means AI.
const CONTROLLER_HUMAN := "human"
const CONTROLLER_AI := "ai"

## The schema of the battle commitment `wotr_battle.gd` mints and `begin_battle`
## admits. It lives HERE rather than there because `begin_battle` has to validate
## what it is about to put inside the hash, and this file cannot preload the
## bridge (the bridge preloads this file). `wotr_battle.gd` aliases these
## constants rather than restating them, so there is one definition.
const BATTLE_COMMITMENT_SCHEMA := "openbfme.wotr-battle-commitment"
## VERSION 2 adds `battlefield_map`: the PACK MAP the tactical match is actually
## fought on, as opposed to `map_name`, which is the region's authored retail
## `MAP WOR <region>` name. They are different things and today they are never
## the same thing, because no `MAP WOR *` map is cooked in any content pack.
##
## It is IN THE COMMITMENT for exactly the reason `attacker_faction` is (see
## `commitment_matches_brief()` in `wotr_battle.gd`): the battlefield decides
## what the battle is, so a value chosen outside the hash and handed to the sim
## as an extra argument is 867447e's desync with a different name on it. The
## resolution table is an argument to `configure()`; its RESULT is recorded here,
## so two peers whose strategic hashes agree fought on the same ground.
## VERSION 3 adds the four fields auto-resolve needs, and adds them HERE rather
## than passing them alongside for the reason version 2 added `battlefield_map`:
## every value that decides what a battle IS has to be inside the record the
## strategic hash covers, or it is 867447e's desync with a different name on it.
##
##   `battle_type`           THE RESOLVED type of THIS battle: `auto_resolve` or
##                           `rts`, never the campaign rule. Retail's campaign
##                           rule "Auto Resolve and RTS" means both are OFFERED,
##                           so the campaign rule alone does not say what this
##                           battle is - only the resolved value does, and only
##                           the resolved value can be checked by a peer.
##   `battle_type_priority`  the campaign rule that resolved it, recorded so the
##                           resolution can be re-derived and disagreed with.
##   `attacker_handicap`     retail's `GUIDisplayedLevel`, which scales that
##   `defender_handicap`     side's auto-resolve weapon and armour multipliers.
##
## The handicaps matter to the hash twice over: they change the arithmetic AND
## they are inside the digest the dice are seeded from, so a peer with a
## different handicap does not merely compute different damage - it rolls
## different dice, which is exactly the divergence a hash comparison should
## catch rather than paper over.
const BATTLE_COMMITMENT_SCHEMA_VERSION := 3

## The three battle types retail's RULES tab offers, as stable ids. Retail's own
## string keys are `VALUE:AutoResolveAndRTS`, `VALUE:AutoResolve` and
## `VALUE:RTS`; these are the ids the strategic layer stores, because a string
## table key is presentation and this is state.
const BATTLE_TYPE_AUTO_RESOLVE_AND_RTS := "auto_resolve_and_rts"
const BATTLE_TYPE_AUTO_RESOLVE := "auto_resolve"
const BATTLE_TYPE_RTS := "rts"
const BATTLE_TYPES := [
	BATTLE_TYPE_AUTO_RESOLVE_AND_RTS, BATTLE_TYPE_AUTO_RESOLVE, BATTLE_TYPE_RTS,
]
## What a single battle can actually BE. "Auto Resolve and RTS" is a campaign
## rule offering both; it is not a way to fight one battle, and a commitment
## carrying it would describe a battle nothing could resolve.
const BATTLE_RESOLVED_TYPES := [BATTLE_TYPE_AUTO_RESOLVE, BATTLE_TYPE_RTS]
## Retail's tooltip: the priority decides "whether a battle will be decided
## through real-time or auto-resolve if players choose differently". Two
## outcomes, so two values.
const BATTLE_PRIORITIES := [BATTLE_TYPE_AUTO_RESOLVE, BATTLE_TYPE_RTS]

## Retail's own handicap rungs, `GUIDisplayedLevel` 0..100 in fives, transcribed
## from `livingworldautoresolvehandicaps.ini`. A commitment carrying a level
## retail never authored is refused rather than resolved to the nearest rung:
## the ladder is retail's and a value off it is not retail's.
const HANDICAP_STEP := 5
const HANDICAP_MAX := 100

## Every field a commitment may carry, and the type it must carry. Exhaustive in
## BOTH directions: a missing field is refused and so is an extra one. A field
## outside this table would ride into `authoritative_state()` uninspected, which
## is precisely the class of defect the hash exists to catch.
const BATTLE_COMMITMENT_FIELDS := {
	"attacker": TYPE_INT,
	"attacker_faction": TYPE_STRING,
	"attacker_handicap": TYPE_INT,
	"attacker_is_ai": TYPE_BOOL,
	"attacker_team": TYPE_INT,
	"battle_type": TYPE_STRING,
	"battle_type_priority": TYPE_STRING,
	"battlefield_map": TYPE_STRING,
	"brief_digest": TYPE_STRING,
	"committed_armies": TYPE_PACKED_INT32_ARRAY,
	"defender": TYPE_INT,
	"defender_faction": TYPE_STRING,
	"defender_handicap": TYPE_INT,
	"defender_is_ai": TYPE_BOOL,
	"defender_team": TYPE_INT,
	"defending_armies": TYPE_PACKED_INT32_ARRAY,
	"map_name": TYPE_STRING,
	"region": TYPE_STRING,
	"schema": TYPE_STRING,
	"schema_version": TYPE_INT,
	"staging_region": TYPE_STRING,
	"turn": TYPE_INT,
}

## THE SCHEMA OF A NEUTRAL-GROUND CLAIM, the strategic transaction that takes
## unowned territory. It lives beside the battle commitment for the same reason:
## `begin_claim()` has to validate what it is about to put inside the hash.
##
## WHAT RETAIL RECORDS, and where. This is not an invented verb - the shipped
## RotWK string table states the rule in prose, twice, in the War of the Ring
## tutorial's own narration (`data/lotr.str`, RotWK layer):
##
##   `WOTRSCRIPT:WOTR_Tutorial071subtitle` - "A Hero army is a group of
##   battalions that are commanded by a special Hero that has the ability to lead
##   troops into battle. They can not only move between territories that you
##   control, but are also used to EXPAND THE LANDS OF YOUR KINGDOM BY CONQUERING
##   NEUTRAL TERRITORIES and invading enemy territories."
##
##   `WOTRSCRIPT:WOTR_Tutorial066subtitle` - "Garrison armies ... can only move
##   between territories that you control. Thus, THEY CANNOT TAKEOVER NEUTRAL
##   TERRITORIES or invade enemy territories on their own. However, a garrison
##   army can join with a Hero army to strengthen a Hero army's attacking forces."
##
## So the claim is a MARCH, not a battle, and only a HERO army can make it. Three
## further facts from the shipped data pin the rest of the shape:
##
## * `APT:LivingWorldRegionTakenNotice` ("%s taken!") is a DIFFERENT string from
##   `APT:LivingWorldRegionConqueredNotice` ("%s conquered!") and from
##   `APT:LivingWorldRegionDefendedNotice` ("%s defended!"), and every region in
##   `livingworldregions.inc` names the "taken" one as its `ConqueredNotice`.
##   Retail distinguishes taking unowned ground from winning a battle over it.
## * NOTHING DEFENDS NEUTRAL GROUND. The document seats eight
##   `LivingWorldPlayerTemplate` rows (seven factions plus `PlayerObserver`) and
##   no neutral one; not one `SpawnArmy` or `LivingWorldCity` row carries an
##   `InitialRegion`; and `CreateAutoFort`'s own comment in
##   `livingworldregions.inc` is "This region can defend itself, no need to build
##   a fort" - a property of the region's OWNER, not a garrison for nobody.
## * THE TUTORIAL'S ONE NEUTRAL-GROUND BATTLE IS MANUFACTURED. `wotrtutorial.inc`
##   turn 3 scripts BOTH `MordorHeroArmy2` and `MenHeroArmy1` into
##   `The_Dead_Marshes`; the battle marker the narration then points at is two
##   seats colliding, not the region resisting. Under this layer's one-move turn
##   that case reduces to the second seat attacking a region the first now owns,
##   which is the battle retail produces and needs no separate rule.
##
## WHAT RETAIL DOES NOT RECORD, and is therefore stated project-authored in
## `wotr_strategic_gaps.gd`: any COST for the claim (no field anywhere prices
## one, so it is free), and WHICH hero army marches when several could (retail
## moves the army the player dragged; this layer has no drag, so it picks the
## lowest-sorted adjacent owned region's lowest army id, the same reproducible
## rule `wotr_handoff.gd` uses to choose a staging region).
const CLAIM_RECORD_SCHEMA := "openbfme.wotr-region-claim"
const CLAIM_RECORD_VERSION := 1

## Every field a claim record may carry, and the type it must carry. Exhaustive
## in BOTH directions, exactly like `BATTLE_COMMITMENT_FIELDS` above: a missing
## field is refused and so is an extra one, because anything outside this table
## would ride into `authoritative_state()` uninspected.
const CLAIM_RECORD_FIELDS := {
	"army": TYPE_INT,
	"from_region": TYPE_STRING,
	"player": TYPE_INT,
	"region": TYPE_STRING,
	"schema": TYPE_STRING,
	"schema_version": TYPE_INT,
	"turn": TYPE_INT,
}

## Every field a standing-structure record may carry, and the type it must
## carry. Exhaustive in BOTH directions, exactly like `BATTLE_COMMITMENT_FIELDS`
## and `CLAIM_RECORD_FIELDS`: anything outside this table would ride into
## `authoritative_state()` uninspected.
const STRUCTURE_RECORD_FIELDS := {
	"building": TYPE_STRING,
	"owner": TYPE_INT,
	"plot": TYPE_INT,
	"token": TYPE_STRING,
	"turn": TYPE_INT,
	"type": TYPE_STRING,
}

## Army kinds. A hero army carries a named hero and is capped by hero CP; a
## garrison army is ordinary troops capped by world CP.
const ARMY_HERO := "hero"
const ARMY_GARRISON := "garrison"

const MAX_ARMIES := 4096

var world: WorldScript = null

## Player seats, index-addressed. Each row: {index, template, faction, team,
## controller, handicap, world_cp, hero_cp, defeated}.
##
## `handicap` is retail's `GUIDisplayedLevel` for that seat. It is AUTHORITATIVE
## STRATEGIC STATE for the same reason `controller` is: it scales auto-resolve
## combat, so it decides outcomes, so it has to be identical on every peer and
## inside the hash. The setup screen used to draw the column locked saying
## "nothing carries it"; this field is what now carries it.
var players: Array[Dictionary] = []

## How battles are decided this campaign. AUTHORITATIVE: it selects between two
## different resolution paths, so two peers disagreeing about it would fight two
## different games. Set once by `setup()` from the setup screen's RULES tab and
## never changed mid-campaign.
var battle_type := BATTLE_TYPE_AUTO_RESOLVE_AND_RTS
var battle_type_priority := BATTLE_TYPE_AUTO_RESOLVE

## Roster name -> the auto-resolve units an army of that roster fields, each
## already carrying retail's `HitpointsAtLevel` for its body. Supplied by the
## session from the binding bundle BEFORE any army is placed.
##
## It is NOT hashed and it is NOT restored: it is a pure function of two
## converted bundles, identical on every peer that loaded the same content, and
## putting a 100-entry lookup table inside every snapshot would hash the content
## pack rather than the campaign. What IS hashed is the units it produced, which
## live on the army records themselves.
var roster_units: Dictionary = {}
## Turn order as player indices. Never shuffled here; the caller supplies it.
var turn_order: PackedInt32Array = PackedInt32Array()
## Completed turns since setup. `active_player()` derives from it.
var turn_index := 0
## The current retail phase, and defeated hero-led armies awaiting relocation.
var phase := PHASE_TACTICAL
var pending_retreats: Array[Dictionary] = []

## region id -> player index, or NEUTRAL.
var region_owner: Dictionary = {}
## army id (int) -> army record.
var armies: Dictionary = {}

## region id -> Array of STANDING STRUCTURE RECORDS, one per occupied build
## plot, sorted by plot index. AUTHORITATIVE and hashed (empty-is-absent): a
## standing barracks is board state exactly as an army is.
##
## THIS FIELD REPLACED `region_buildings`, which was a bare list of retail's
## `LW_*` scenario tokens, and the replacement is the point of the construction
## lane. A token list could say THAT something stood in a region; it could not
## say which PLOT it stood on, who OWNS it, what TYPE it is, or whether it was
## authored by a scenario or raised by a seat that paid for it - and every one of
## those is needed the moment a region's plots can be built on, taken with the
## region, and paid income for.
##
## Each record carries exactly these six fields and nothing else (validated by
## `_structure_record_refusal()`, exhaustively in both directions, for the same
## reason a battle commitment is: everything here enters the strategic hash):
##
##   `building`  THE ID THAT STANDS. A concrete `LivingWorldBuilding` id
##               (`LWB_IsengardUrukPit`) once anything has resolved it; the
##               verbatim authored token (`LW_FORT`) when a scenario placed it
##               and no building catalogue was bound to resolve it. A verbatim
##               token is an honest unresolved value, not a placeholder: it is
##               exactly what the scenario file says.
##   `token`     the scenario's authored `LW_*` token, or "" for a structure a
##               seat built. Kept so provenance survives the resolution.
##   `type`      retail's `Type =`: Fortress, Barracks, Armory or Resource, or
##               "" when nothing has resolved the token yet.
##   `owner`     the seat that holds it. Follows the region on a conquest.
##   `plot`      which of the region's authored `BuildingSpot`s it stands on.
##   `turn`      the `turn_index` it was raised on; 0 for scenario-authored.
var region_structures: Dictionary = {}

## Player index -> TREASURE. AUTHORITATIVE and hashed (empty-is-absent): it is
## what construction spends and what income adds to, so two peers disagreeing
## about it would disagree about what can be built.
##
## Retail calls this the Treasury (`STRATEGICHUD:StatsCommandPointsTitle` is the
## literal string "Treasury", and its help reads "Current Treasury available for
## creating new units and buildings"), and seats it from the living-world
## document's own `LivingWorldPlayerTemplate ScenarioStartResources` - 3000 on
## every playable template in the shipped RotWK document, which is retail's
## number and not this file's.
var treasury: Dictionary = {}

## The building catalogue the construction rules read: `wotr_buildings.gd`, or
## null. NOT HASHED and NOT RESTORED, for exactly the reason `roster_units` is
## not: it is a pure function of two converted bundles, identical on every peer
## that loaded the same content, and hashing it would hash the content pack
## rather than the campaign. What IS hashed is the structures it let a seat
## raise, which live in `region_structures` above.
##
## With no catalogue bound, every construction refuses BY NAME - `can_build()`
## says which bundle is missing - and nothing is built with invented prices.
var building_catalogue = null

## hero template -> {template, owner, level, fallen, fell_turn}. THE HERO
## LEDGER: a War of the Ring hero is a campaign-scoped person, not a property
## of whichever army record currently carries them. The ledger is what lets a
## hero's LEVEL survive the army's own record being rewritten by attrition, and
## what remembers a FALLEN hero at all - before it existed, a wiped hero army
## erased every trace of the hero, which silently made death unrecordable.
## Hashed (empty-is-absent) and restored, because two peers disagreeing about
## whether Aragorn is alive are not playing the same campaign.
##
## `fallen` is terminal today: the document authors revival COST multipliers
## for RTS battles (`initial_revival_cost_milli`) but NO strategic-turn revival
## rule, so re-placing a fallen hero is refused by name rather than resolved by
## an invented timer - see `strategic_hero_revival` in the data-gap register.
var heroes: Dictionary = {}

## The scenario whose ownership (and victory conditions) this campaign runs
## under, recorded by `apply_ownership_sets()`. Hashed: the victory evaluator
## reads the scenario's authored conditions through it, so two peers holding
## different names would be playing to different rules.
var scenario_name := ""

## Index into the scenario's authored `victory_types` - the choice retail's
## setup screen offers ("Elimination", "Capital Assault", the territory
## ladders). Hashed for the same reason `battle_type` is: it selects which
## conditions end the campaign. Validated against the scenario when ownership
## is applied; retail's own default is the scenario's first authored type.
var victory_type := 0

## The battle currently in flight, or `{}` when none is.
##
## This IS authoritative state, and it is here rather than on the bridge for the
## reason 87cf636 established the hard way: a battle in flight is a strategic
## transaction that is half-applied. A peer that adopted a snapshot taken during
## a battle without this record would not know a battle was happening, would
## have nothing to apply the result to, and would silently diverge the moment the
## result arrived. It is hashed EMPTY-IS-ABSENT (a state with no battle in
## flight contributes zero bytes, so the between-battles hash is exactly what it
## was before this field existed), and `setup()` clears it.
var pending_battle: Dictionary = {}

## The neutral-ground CLAIM currently in flight, or `{}` when none is.
##
## AUTHORITATIVE for exactly the reasons `pending_battle` is, and deliberately
## shaped the same way: opening it and applying it are two steps (the screen
## commits, then resolves; the opponent does the same through the same doors), so
## between them the campaign is half-way through a transaction and a peer that
## adopted a snapshot without the record would not know it. Hashed
## EMPTY-IS-ABSENT, so a campaign that has never claimed anything hashes exactly
## as it did before this field existed, and `setup()` clears it.
##
## A claim and a battle are MUTUALLY EXCLUSIVE - `begin_claim()` and
## `begin_battle()` each refuse while the other is in flight - because both end
## by handing the turn on, and two open transactions would hand it on twice.
var pending_claim: Dictionary = {}

var _next_army_id := 1

## Non-authoritative: cleared by `restore()` and excluded from the hash, exactly
## like the retail slice's `events`.
var events: Array[Dictionary] = []
var last_command_result: Variant = null


## Bind a world and seat the players. Returns false (and changes nothing that
## matters) when the setup is not fully understood.
##
## `rules` carries the campaign-wide choices the setup screen's RULES tab makes,
## today `battle_type` and `battle_type_priority`. It DEFAULTS rather than being
## required so every existing caller keeps working, and the defaults are
## retail's own screen defaults ("Auto Resolve and RTS", priority "Auto
## Resolve") rather than this project's preference.
func setup(source_world: WorldScript, seats: Array, rules: Dictionary = {}) -> bool:
	if source_world == null or source_world.region_ids.is_empty():
		return false
	if seats.is_empty() or seats.size() > WorldScript.MAX_PLAYERS:
		return false
	world = source_world
	players = []
	region_owner = {}
	armies = {}
	region_structures = {}
	treasury = {}
	heroes = {}
	scenario_name = ""
	pending_battle = {}
	pending_claim = {}
	phase = PHASE_TACTICAL
	pending_retreats = []
	events = []
	turn_index = 0
	_next_army_id = 1
	battle_type = normalized_battle_type(rules.get("battle_type", BATTLE_TYPE_AUTO_RESOLVE_AND_RTS))
	battle_type_priority = normalized_battle_priority(
		rules.get("battle_type_priority", BATTLE_TYPE_AUTO_RESOLVE))
	victory_type = normalized_victory_type(rules.get("victory_type", 0))
	var order: Array[int] = []
	for index in range(seats.size()):
		var seat := seats[index] as Dictionary
		var template_name := String(seat.get("template", ""))
		var template: Dictionary = world.player_templates.get(template_name, {})
		players.append({
			"index": index,
			"template": template_name,
			"faction": String(template.get("faction", seat.get("faction", ""))),
			"team": int(seat.get("team", index + 1)),
			"controller": normalized_controller(seat.get("controller", CONTROLLER_AI)),
			"handicap": normalized_handicap(seat.get("handicap", 0)),
			"world_cp": int(template.get("starting_world_cp", 0)),
			"hero_cp": int(template.get("starting_hero_cp", 0)),
			"defeated": false,
		})
		# RETAIL'S OWN OPENING TREASURY, off the seat's own
		# `LivingWorldPlayerTemplate ScenarioStartResources` row (3000 on every
		# playable template the shipped RotWK document carries; retail's setup
		# screen names the same quantity `RULE:InitialResources`, "Initial
		# Resources"). A template that authors none - retail's `PlayerObserver`
		# carries -1, because a spectator buys nothing - starts at zero rather
		# than at a number invented here.
		treasury[index] = maxi(0, int(template.get("scenario_start_resources", 0)))
		order.append(index)
	turn_order = PackedInt32Array(order)
	for region_id in world.region_ids:
		region_owner[region_id] = NEUTRAL
	return true


## Apply a scenario's `OwnershipSet` rows to seats in order: set index 0 goes to
## player 0, and so on. Extra sets beyond the seated players are ignored (retail
## authors six sets and lets fewer players sit down).
func apply_ownership_sets(name: String) -> bool:
	if world == null:
		return false
	var scenario := world.scenario(name)
	if scenario.is_empty():
		return false
	# The chosen victory type must be one the scenario actually authors. Checked
	# BEFORE any ownership moves so a refusal leaves the board untouched. Index 0
	# is always admissible - a scenario with no authored types simply has no
	# evaluable victory, which `evaluate_victory()` reports by name.
	var types: Array = scenario.get("victory_types", []) as Array
	if victory_type != 0 and victory_type >= types.size():
		return false
	# RECORDED FIRST, because `place_army()` resolves scenario-scoped act armies
	# through it: retail's `HeroArmy1` means this scenario's row for the seat's
	# template, and resolution without the scenario would fall back to whichever
	# campaign default happened to share the name.
	scenario_name = name
	var sets: Array = scenario.get("ownership_sets", []) as Array
	for index in range(players.size()):
		if index >= sets.size():
			break
		var ownership := sets[index] as Dictionary
		# THE SEAT'S CAPITAL: retail's `StartRegion`, the region `loseIfCapitalLost`
		# defeat conditions test. On the seat row so it rides players into the hash
		# and every snapshot.
		(players[index] as Dictionary)["capital"] = String(ownership.get("start_region", ""))
		for region_id in ownership.get("regions", PackedStringArray()) as PackedStringArray:
			if not world.has_region(region_id):
				return false
			region_owner[region_id] = index
		for spawn_row in ownership.get("spawn_armies", []) as Array:
			var spawn := spawn_row as Dictionary
			var region_id := String(spawn.get("region", ""))
			if not world.has_region(region_id):
				return false
			for army_name in spawn.get("armies", PackedStringArray()) as PackedStringArray:
				if place_army(index, region_id, army_name) < 0:
					return false
		for building_row in ownership.get("spawn_buildings", []) as Array:
			var building := building_row as Dictionary
			var region_id := String(building.get("region", ""))
			if not world.has_region(region_id):
				return false
			for token in building.get("buildings", PackedStringArray()) as PackedStringArray:
				if not place_authored_structure(index, region_id, String(token)):
					return false
	return true


## Stand a SCENARIO-AUTHORED structure on the next free plot of a region.
##
## The token is retail's `LW_*` `#define` - `LW_FORT`, `LW_BARRACKS`, `LW_ARMORY`
## or `LW_FARM` - and retail's own comment above those defines states the rule
## for reading it: "Allows scenarios to say that a fort should be spawned in a
## region, and THE APPROPRIATE ONE FOR THE CONTROLLING FACTION will be created."
## `wotr_buildings.gd` performs that resolution, by type and by retail's
## `AvailableTo`, and `resolve_scenario_token()` is the seam.
##
## WITH NO CATALOGUE BOUND the token is stood VERBATIM, typeless. That is not a
## fallback and not a guess: the record then says exactly what the scenario file
## says and nothing more, it occupies a plot (which the scenario did intend), and
## it earns no treasury income (because nothing yet knows whether it is a farm).
## A test harness with no converted bundles gets that state and can assert on it.
##
## PLOT OVERFLOW IS A REFUSAL. A scenario that authors more buildings than the
## region has `BuildingSpot`s is a contradiction in retail's own data, and
## seating the extra one on a plot that does not exist would put a structure
## nowhere.
func place_authored_structure(player: int, region_id: String, token: String) -> bool:
	if world == null or not world.has_region(region_id):
		return _reject("unknown region %s" % region_id)
	if player != NEUTRAL and (player < 0 or player >= players.size()):
		return _reject("unknown player %d" % player)
	var plot := free_plot(region_id)
	if plot < 0:
		return _reject("%s authors %d build plots and already has %d standing structure(s), so '%s' has nowhere to stand" % [
			region_id, plot_count(region_id), structures_in_region(region_id).size(), token])
	var resolved_id := token
	var resolved_type := ""
	if building_catalogue != null:
		var template := String((players[player] as Dictionary).get("template", "")) if player != NEUTRAL else ""
		var resolved: Dictionary = building_catalogue.resolve_scenario_token(token, template)
		if bool(resolved.get("ok", false)):
			resolved_id = String(resolved.get("id", token))
			resolved_type = String(resolved.get("type", ""))
	return _stand_structure(region_id, {
		"building": resolved_id,
		"owner": player,
		"plot": plot,
		"token": token,
		# -1, NOT 0: a scenario's structures were standing before the campaign
		# began, and turn 0 is a real turn the first seat gets to build on. With 0
		# here, retail's one-structure-per-territory-per-turn rule below would
		# refuse seat 0's opening build in every region its own scenario had
		# already furnished.
		"turn": -1,
		"type": resolved_type,
	})


func active_player() -> int:
	if turn_order.is_empty():
		return NEUTRAL
	return turn_order[turn_index % turn_order.size()]


## Completed rounds (one round = one turn for every seat).
func round_index() -> int:
	if turn_order.is_empty():
		return 0
	return turn_index / turn_order.size()


## Advance to the next seat, skipping seats already defeated. Deterministic and
## total: with every seat defeated it still advances exactly one turn, so a
## caller polling for a winner can never spin.
##
## THE ARRIVING SEAT IS PAID FIRST. Retail's own help string for the number it
## draws on the strategic HUD is "Treasury increases by this amount AT THE START
## OF EACH TURN" (`STRATEGICHUD:StatsCTreasuryIncomeHelp`), so the accrual
## belongs to the moment the turn arrives, not to the moment it ends. Putting it
## here rather than in every caller is what makes it impossible for one path
## through the campaign - the AI's, the END TURN button's, an auto-resolved
## battle's - to pay a seat and another to forget to.
## THE SINGLE PUBLIC PHASE DOOR. Empty battle and retreat phases auto-pass in
## the same call, deterministically. Only the retreat->tactical edge advances
## the seat, so income accrues exactly once.
func end_phase() -> String:
	if phase == PHASE_TACTICAL:
		phase = PHASE_BATTLE
		events.append({"kind": "phase_changed", "phase": phase, "turn": turn_index})
		if pending_battle.is_empty() and pending_claim.is_empty():
			events.append({"kind": "phase_auto_passed", "phase": PHASE_BATTLE, "turn": turn_index})
			phase = PHASE_RETREAT
			events.append({"kind": "phase_changed", "phase": phase, "turn": turn_index})
			if pending_retreats.is_empty():
				events.append({"kind": "phase_auto_passed", "phase": PHASE_RETREAT, "turn": turn_index})
				_finish_retreat_phase()
		return phase
	if phase == PHASE_BATTLE:
		if not pending_battle.is_empty() or not pending_claim.is_empty():
			_reject("battle phase cannot end while a result is undecided")
			return phase
		phase = PHASE_RETREAT
		events.append({"kind": "phase_changed", "phase": phase, "turn": turn_index})
		if pending_retreats.is_empty():
			events.append({"kind": "phase_auto_passed", "phase": PHASE_RETREAT, "turn": turn_index})
			_finish_retreat_phase()
		return phase
	# Retail's no-option relocation applies only when there is no adjacent friendly
	# order. Try those blocked rows deterministically; selectable retreats remain
	# player orders and keep the phase open.
	var blocked_ids: Array[int] = []
	for row in pending_retreats:
		var army_id := int(row["army"])
		if not retreat_targets(army_id).is_empty():
			_reject("retreat phase cannot end while army %d has a choosable destination" % army_id)
			return phase
		blocked_ids.append(army_id)
	for army_id in blocked_ids:
		if not _automatic_retreat(army_id):
			_reject("retreat army %d has no allied relocation destination" % army_id)
			return phase
	if not pending_retreats.is_empty():
		_reject("retreat phase cannot end while an army awaits a retreat order")
		return phase
	_finish_retreat_phase()
	return phase


func _finish_retreat_phase() -> void:
	advance_turn()
	phase = PHASE_TACTICAL
	events.append({"kind": "phase_changed", "phase": phase, "turn": turn_index})


## Admit the only survivor retail permits from a defeated hero-led army. The
## exact leader must be provable by template before any mutation occurs.
func queue_hero_retreat(army_id: int, hero_unit: Dictionary) -> bool:
	if phase != PHASE_BATTLE:
		return _reject("a hero retreat may only be queued while a battle is being settled")
	for row in pending_retreats:
		if int(row.get("army", -1)) == army_id:
			return _reject("army %d already has a pending retreat" % army_id)
	if not armies.has(army_id):
		return _reject("unknown retreat army %d" % army_id)
	var army := armies[army_id] as Dictionary
	var hero_template := String(army.get("hero_template", ""))
	if String(army.get("kind", "")) != ARMY_HERO or hero_template.is_empty():
		return _reject("army %d is not led by a proven hero" % army_id)
	if String(hero_unit.get("template", "")) != hero_template:
		return _reject("army %d survivor does not prove exact hero template %s" % [army_id, hero_template])
	if not apply_attrition(army_id, [hero_unit]):
		return _reject("army %d exact hero leader could not be written for retreat" % army_id)
	pending_retreats.append({
		"army": army_id, "from_region": String(army.get("region", "")),
		"player": int(army.get("owner", NEUTRAL)), "turn": turn_index,
		"hero_template": hero_template,
	})
	pending_retreats.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a["army"]) < int(b["army"]))
	return true


func retreat_targets(army_id: int) -> PackedStringArray:
	var found: Array[String] = []
	var row: Dictionary = {}
	for candidate in pending_retreats:
		if int(candidate.get("army", -1)) == army_id:
			row = candidate
			break
	if row.is_empty() or world == null:
		return PackedStringArray()
	var owner := int(row["player"])
	for region_id in world.neighbours(String(row["from_region"])):
		if owner_of(region_id) == owner and _retreat_fits(army_id, region_id):
			found.append(region_id)
	found.sort()
	return PackedStringArray(found)


func _automatic_retreat(army_id: int) -> bool:
	var row: Dictionary = {}
	for candidate in pending_retreats:
		if int(candidate["army"]) == army_id:
			row = candidate
			break
	if row.is_empty() or not retreat_targets(army_id).is_empty() or world == null:
		return false
	var owner := int(row["player"])
	var start := String(row["from_region"])
	var capital := (
		String((players[owner] as Dictionary).get("capital", ""))
		if owner >= 0 and owner < players.size() else ""
	)
	var frontier: Array[String] = [start]
	var distance := {start: 0}
	var candidates: Array[String] = []
	var best := -1
	while not frontier.is_empty():
		var current: String = String(frontier.pop_front())
		var depth := int(distance[current])
		if best >= 0 and depth > best:
			break
		if current != start and owner_of(current) == owner and _retreat_fits(army_id, current):
			best = depth
			candidates.append(current)
			continue
		var neighbours: Array[String] = []
		for value in world.neighbours(current):
			neighbours.append(String(value))
		neighbours.sort()
		for neighbour in neighbours:
			if not distance.has(neighbour):
				distance[neighbour] = depth + 1
				frontier.append(neighbour)
	if candidates.is_empty():
		# Retail's authored capital is the last relocation of record and ignores
		# the command-point cap. If it cannot be resolved, destruction is total and
		# named rather than leaving Retreat permanently locked.
		if not capital.is_empty() and world.has_region(capital) and owner_of(capital) == owner:
			return _complete_retreat(army_id, capital, true)
		var destruction_reason := "capital_absent" if capital.is_empty() else (
			"capital_unresolved" if not world.has_region(capital) else "capital_not_owned")
		_drop_pending_retreat(army_id)
		if armies.has(army_id):
			remove_army(army_id)
		events.append({"kind": "retreat_no_destination_destroyed", "army": army_id,
			"player": owner, "reason": destruction_reason, "turn": turn_index})
		return true
	# Project-authored `retreat_distance_rule`: shortest path, capital first on a
	# distance tie, then stable region id.
	candidates.sort_custom(func(a: String, b: String) -> bool:
		if a == capital and b != capital: return true
		if b == capital and a != capital: return false
		return a < b)
	return _complete_retreat(army_id, candidates[0], true)


func _retreat_fits(army_id: int, region_id: String) -> bool:
	if not armies.has(army_id):
		return false
	var army := armies[army_id] as Dictionary
	var player := int(army.get("owner", NEUTRAL))
	return (command_points_in_region(region_id, player) + int(army.get("command_points", 0))
		<= world.region_cp_limit(region_id))


func _drop_pending_retreat(army_id: int) -> void:
	for index in range(pending_retreats.size()):
		if int(pending_retreats[index].get("army", -1)) == army_id:
			pending_retreats.remove_at(index)
			return


func _complete_retreat(army_id: int, to_region: String, automatic: bool) -> bool:
	if not armies.has(army_id):
		return false
	(armies[army_id] as Dictionary)["region"] = to_region
	_drop_pending_retreat(army_id)
	events.append({"kind": "army_retreated", "army": army_id, "region": to_region,
		"automatic": automatic, "turn": turn_index})
	return true


func order_retreat(army_id: int, to_region: String) -> bool:
	if phase != PHASE_RETREAT:
		return _reject("retreat orders are only legal in the retreat phase")
	if to_region.is_empty():
		if not _automatic_retreat(army_id):
			return _reject("army %d has an adjacent retreat choice or no allied fallback" % army_id)
	elif not Array(retreat_targets(army_id)).has(to_region):
		return _reject("%s is not an adjacent friendly retreat destination" % to_region)
	elif not _complete_retreat(army_id, to_region, false):
		return _reject("retreat army %d disappeared" % army_id)
	return true


func advance_turn() -> int:
	if turn_order.is_empty():
		return NEUTRAL
	for _step in range(turn_order.size()):
		turn_index += 1
		var candidate := active_player()
		if not bool((players[candidate] as Dictionary).get("defeated", false)):
			accrue_income(candidate)
			return candidate
	turn_index += 1
	return active_player()


## Regions owned by `player`, in sorted order.
func regions_owned_by(player: int) -> PackedStringArray:
	var owned := PackedStringArray()
	for region_id in world.region_ids:
		if int(region_owner.get(region_id, NEUTRAL)) == player:
			owned.append(region_id)
	return owned


func owner_of(region_id: String) -> int:
	return int(region_owner.get(region_id, NEUTRAL))


## Transfer a region. Fails closed on an unknown region or an unseated player;
## a no-op transfer (already the owner) reports false so a caller can never
## mistake it for a capture.
func transfer_region(region_id: String, player: int) -> bool:
	if world == null or not world.has_region(region_id):
		return _reject("unknown region %s" % region_id)
	if player != NEUTRAL and (player < 0 or player >= players.size()):
		return _reject("unknown player %d" % player)
	var previous := owner_of(region_id)
	if previous == player:
		return _reject("region %s already owned by %d" % [region_id, player])
	region_owner[region_id] = player
	# THE STRUCTURES FOLLOW THE GROUND. Retail's own end-of-game tallies count
	# `APT:StructuresCreated`, `APT:StructuresLost` and
	# `APT:EnemyBuildingDestroyed` as three different things - so a structure in a
	# region that changes hands is LOST to its builder and GAINED by the taker,
	# not destroyed. Leaving `owner` pointing at the previous seat would have paid
	# them `GAIN_PER_FARM` for a farm standing behind an enemy border.
	var standing: Array = (region_structures.get(region_id, []) as Array)
	for row in standing:
		(row as Dictionary)["owner"] = player
	events.append({
		"kind": "region_transferred",
		"region": region_id,
		"from": previous,
		"to": player,
		"turn": turn_index,
	})
	last_command_result = true
	return true


## Place an army from the world's `LivingWorldPlayerArmy` roster. Returns the
## new army id, or -1 when the placement is not fully understood.
func place_army(player: int, region_id: String, army_name: String) -> int:
	if phase != PHASE_TACTICAL:
		_reject("training is only legal in the tactical phase")
		return -1
	if world == null or not world.has_region(region_id):
		_reject("unknown region %s" % region_id)
		return -1
	if player < 0 or player >= players.size():
		_reject("unknown player %d" % player)
		return -1
	if armies.size() >= MAX_ARMIES:
		_reject("army count exceeds limit")
		return -1
	# WHICH ROW `army_name` means for THIS seat. Retail reuses scripting names
	# across factions (ten `HeroArmy1` rows ship), scoped by the scenario's act
	# armies and the seat's template; `resolve_army_spawn()` applies both scopes
	# and refuses ambiguity. A name that is no spawn row at all may still be a
	# bare `LivingWorldPlayerArmy` roster name - the legitimate way callers raise
	# a reinforcement directly from a roster - and only the "unknown" outcome
	# falls through to that reading, so a template mismatch stays a refusal
	# rather than quietly becoming a roster-less ghost army (which is exactly
	# what the old single-row lookup produced on the shipped document).
	var template := String((players[player] as Dictionary).get("template", ""))
	var resolved: Dictionary = world.resolve_army_spawn(scenario_name, army_name, template)
	var spawn: Dictionary = {}
	if bool(resolved.get("ok", false)):
		spawn = resolved.get("spawn", {}) as Dictionary
	elif String(resolved.get("reason", "")) != "unknown":
		_reject("army %s cannot be resolved for seat %d: %s" % [
			army_name, player, String(resolved.get("reason", ""))])
		return -1
	var roster_name := String(spawn.get("player_army", army_name))
	var roster: Dictionary = world.player_armies.get(roster_name, {})
	# A name that is neither a resolvable spawn row nor a known roster would
	# place a GHOST: zero command points, no units, no roster - an army-shaped
	# nothing that nonetheless holds regions and rides the hash. That is exactly
	# what the shipped document's `HeroArmy1` used to become, and it is refused
	# by name now.
	if spawn.is_empty() and roster.is_empty():
		_reject("'%s' is neither a spawn row this seat can resolve nor a LivingWorldPlayerArmy roster" % army_name)
		return -1
	var kind := ARMY_HERO if bool(spawn.get("is_hero", false)) else ARMY_GARRISON
	var hero_template := String(spawn.get("hero_template_name", ""))
	# THE HERO LEDGER GATE. A fallen hero cannot be re-raised (no strategic
	# revival rule is authored - a named data gap, not an oversight here), and a
	# LIVING hero cannot lead two armies at once - both refusals by name, because
	# either admitted silently would fork the campaign's idea of who the hero is.
	if kind == ARMY_HERO and heroes.has(hero_template):
		var ledger := heroes[hero_template] as Dictionary
		if bool(ledger.get("fallen", false)):
			_reject("hero %s fell on turn %d and the document authors no strategic revival rule" % [
				hero_template, int(ledger.get("fell_turn", -1))])
			return -1
		_reject("hero %s already leads an army" % hero_template)
		return -1
	var id := _next_army_id
	_next_army_id += 1
	# THE UNITS ARE AUTHORITATIVE STATE from here on. Each one carries its
	# retail auto-resolve type, body, armour, weapon, combat chain, level and
	# CURRENT HITPOINTS, and all of it enters `authoritative_state()`. That is
	# the largest change this lane makes to the hash surface: an army's strength
	# stops being a command-point integer and becomes a continuous quantity that
	# a battle can reduce without destroying the army.
	#
	# With no `roster_units` table bound - no bindings bundle, or a caller that
	# never set one - the list is EMPTY, and auto-resolve then refuses that army
	# by name rather than inventing a force for it.
	var units: Array[Dictionary] = []
	for row in (roster_units.get(roster_name, []) as Array):
		var unit: Dictionary = (row as Dictionary).duplicate(true)
		unit["army_id"] = id
		units.append(unit)
	armies[id] = {
		"id": id,
		"owner": player,
		"region": region_id,
		"kind": kind,
		"spawn_name": army_name,
		"roster": roster_name,
		"hero_template": hero_template,
		"command_points": _roster_command_points(roster),
		"units": units,
	}
	# Seed the ledger the moment the hero enters the world. Level 1 is not a
	# guess: it is the same starting level `units_for_roster()` gives the hero's
	# own auto-resolve unit, and the two are kept in step by `apply_attrition()`.
	if kind == ARMY_HERO and not hero_template.is_empty():
		heroes[hero_template] = {
			"template": hero_template,
			"owner": player,
			"level": 1,
			"fallen": false,
			"fell_turn": -1,
		}
	events.append({"kind": "army_placed", "army": id, "region": region_id, "turn": turn_index})
	last_command_result = true
	return id


## Army ids in a region, sorted ascending so callers iterate deterministically.
func armies_in_region(region_id: String) -> PackedInt32Array:
	var found: Array[int] = []
	for key in armies.keys():
		if String((armies[key] as Dictionary).get("region", "")) == region_id:
			found.append(int(key))
	found.sort()
	return PackedInt32Array(found)


## Total command points a player has standing in a region.
func command_points_in_region(region_id: String, player: int) -> int:
	var total := 0
	for id in armies_in_region(region_id):
		var army := armies[id] as Dictionary
		if int(army.get("owner", NEUTRAL)) == player:
			total += int(army.get("command_points", 0))
	return total


## Move one army along a single graph edge. Movement is deliberately strict:
## adjacency only, own army only, and the destination must not exceed the
## region's command-point cap for that player.
func move_army(army_id: int, to_region: String) -> bool:
	if phase != PHASE_TACTICAL:
		return _reject("army movement is only legal in the tactical phase")
	return _move_army_phase_internal(army_id, to_region)


## Phase machinery and battle settlement use this private path; callers never
## receive a second public phase door.
func _move_army_phase_internal(army_id: int, to_region: String) -> bool:
	if not armies.has(army_id):
		return _reject("unknown army %d" % army_id)
	var army := armies[army_id] as Dictionary
	var from_region := String(army.get("region", ""))
	if from_region == to_region:
		return _reject("army %d is already in %s" % [army_id, to_region])
	if not world.are_adjacent(from_region, to_region):
		return _reject("%s is not adjacent to %s" % [to_region, from_region])
	var owner := int(army.get("owner", NEUTRAL))
	var arriving := int(army.get("command_points", 0))
	if command_points_in_region(to_region, owner) + arriving > world.region_cp_limit(to_region):
		return _reject("army %d exceeds the command-point cap of %s" % [army_id, to_region])
	army["region"] = to_region
	events.append({
		"kind": "army_moved",
		"army": army_id,
		"from": from_region,
		"to": to_region,
		"turn": turn_index,
	})
	last_command_result = true
	return true


## True when `player` may attack `region_id` this turn: ANOTHER SEAT must own it,
## and `player` must have an army standing in an adjacent region they do own.
##
## UNOWNED GROUND IS NOT ATTACKABLE, and that clause is the correction at the
## heart of this pass rather than a new restriction. It used to return true for a
## neutral region, `wotr_session.attack_targets()` therefore offered every one of
## them, and `wotr_battle.gd` then refused to configure the battle because a
## neutral region has no owning seat and so no defending FACTION to field. The
## result was a button that did nothing - for the human exactly as much as for the
## opponent, whose every legal target on retail's own board was neutral ground.
##
## Retail does not fight for unowned ground either (see `CLAIM_RECORD_SCHEMA`
## above for the shipped strings that say so); it is TAKEN, by marching a hero
## army in. `can_claim()` below is that test, and the session offers the two
## together so no neutral region is ever presented as a fight.
func can_attack(player: int, region_id: String) -> bool:
	if world == null or not world.has_region(region_id):
		return false
	var defender := owner_of(region_id)
	if defender == player or defender == NEUTRAL:
		return false
	for neighbour in world.neighbours(region_id):
		if owner_of(neighbour) != player:
			continue
		if not armies_in_region(neighbour).is_empty():
			return true
	return false


## True when `player` may CLAIM `region_id` this turn: it must be unowned, and
## `player` must have a HERO army standing in an adjacent region they own.
##
## The hero-only clause is retail's, verbatim from the tutorial narration quoted
## at `CLAIM_RECORD_SCHEMA`: a garrison army "cannot takeover neutral
## territories ... on their own". A frontier held only by garrisons therefore
## offers no claim, and the session shows no button for one, which is the
## difference between a rule and a dead control.
func can_claim(player: int, region_id: String) -> bool:
	return claiming_army(player, region_id) >= 0


## THE ARMY THAT WOULD MAKE THE CLAIM, or -1 when no claim is legal.
##
## DETERMINISTIC BY CONSTRUCTION, because the choice enters the hash: the
## lowest-sorted adjacent region `player` owns that holds one of their hero
## armies, and within it the lowest army id. `world.neighbours()` is already
## sorted and `armies_in_region()` returns ascending ids, so the answer is the
## same on every machine without a tie-break heuristic. Retail moves whichever
## army the player dragged; this layer has no drag, and the substitute rule is
## named project-authored in `wotr_strategic_gaps.gd` rather than passed off as
## retail's.
func claiming_army(player: int, region_id: String) -> int:
	if world == null or not world.has_region(region_id):
		return -1
	if player == NEUTRAL or player < 0 or player >= players.size():
		return -1
	if owner_of(region_id) != NEUTRAL:
		return -1
	for neighbour in world.neighbours(region_id):
		if owner_of(neighbour) != player:
			continue
		for army_id in armies_in_region(neighbour):
			var army := armies[army_id] as Dictionary
			if int(army.get("owner", NEUTRAL)) != player:
				continue
			if String(army.get("kind", "")) != ARMY_HERO:
				continue
			return int(army_id)
	return -1


## Mint the claim record `begin_claim()` admits. Returns `{}` when no claim is
## legal, so a caller can never hand `begin_claim()` a half-formed one.
func build_claim(player: int, region_id: String) -> Dictionary:
	var army_id := claiming_army(player, region_id)
	if army_id < 0:
		return {}
	return {
		"army": army_id,
		"from_region": String((armies[army_id] as Dictionary).get("region", "")),
		"player": player,
		"region": region_id,
		"schema": CLAIM_RECORD_SCHEMA,
		"schema_version": CLAIM_RECORD_VERSION,
		"turn": turn_index,
	}


## Open the claim transaction. Validated exhaustively before it is admitted, for
## the same reason `begin_battle()` validates its commitment: everything admitted
## here enters `authoritative_state()` and therefore the strategic hash.
func begin_claim(record: Dictionary) -> bool:
	if phase != PHASE_TACTICAL:
		return _reject("a claim may only be committed in the tactical phase")
	if record.is_empty():
		return _reject("claim record is empty")
	if not pending_claim.is_empty():
		return _reject("a claim is already in flight in %s" % String(pending_claim.get("region", "")))
	if not pending_battle.is_empty():
		return _reject("a battle is already in flight in %s" % String(pending_battle.get("region", "")))
	var schema_reason := _claim_record_refusal(record)
	if schema_reason != "":
		return _reject(schema_reason)
	var region_id := String(record.get("region", ""))
	var player := int(record.get("player", NEUTRAL))
	if world == null or not world.has_region(region_id):
		return _reject("unknown region %s" % region_id)
	if owner_of(region_id) != NEUTRAL:
		return _reject("region %s is not unowned; it is held by seat %d" % [region_id, owner_of(region_id)])
	# THE RECORD MUST DESCRIBE A CLAIM THAT IS LEGAL RIGHT NOW, not merely a
	# well-formed dictionary. A caller could otherwise name any army and any
	# neutral region and have the pair admitted into the hash.
	var army_id := int(record.get("army", -1))
	if claiming_army(player, region_id) != army_id:
		return _reject(
			"army %d is not the army that would claim %s for seat %d" % [army_id, region_id, player])
	pending_claim = record.duplicate(true)
	phase = PHASE_BATTLE
	events.append({"kind": "claim_begun", "region": region_id, "turn": turn_index})
	last_command_result = true
	return true


## Close the claim transaction WITHOUT taking the region - the abandoned-claim
## path, symmetric with `clear_battle()`. Nothing is applied, because there is
## nothing half-applied: `apply_claim()` is what moves the border.
func clear_claim() -> bool:
	if pending_claim.is_empty():
		return _reject("no claim is in flight")
	var region_id := String(pending_claim.get("region", ""))
	pending_claim = {}
	events.append({"kind": "claim_cleared", "region": region_id, "turn": turn_index})
	last_command_result = true
	return true


## APPLY THE CLAIM IN FLIGHT: the hero army marches in and the region changes
## hands. Returns `{ok, refusals, region, player, army}`.
##
## THE MARCH HAPPENS FIRST, deliberately. `move_army()` fails closed - adjacency
## and the destination's command-point cap are both checked before it writes
## anything - so a claim that the cap forbids refuses with the border untouched.
## Transferring first and then discovering the army cannot follow would leave a
## seat holding a region it never reached, which is precisely the half-applied
## shape the two-step transaction exists to prevent.
##
## The claim is REVALIDATED against the board as it stands, not trusted from the
## record: between `begin_claim()` and here the army could have been destroyed by
## a battle, or the region taken by another seat.
func apply_claim() -> Dictionary:
	var refusals := PackedStringArray()
	if pending_claim.is_empty():
		refusals.append("no claim is in flight")
		return {"ok": false, "refusals": refusals, "region": "", "player": NEUTRAL, "army": -1}
	var region_id := String(pending_claim.get("region", ""))
	var player := int(pending_claim.get("player", NEUTRAL))
	var army_id := int(pending_claim.get("army", -1))
	if owner_of(region_id) != NEUTRAL:
		refusals.append("region %s is no longer unowned" % region_id)
	elif not armies.has(army_id):
		refusals.append("the claiming army %d no longer exists" % army_id)
	elif int((armies[army_id] as Dictionary).get("owner", NEUTRAL)) != player:
		refusals.append("army %d no longer answers to seat %d" % [army_id, player])
	elif not _move_army_phase_internal(army_id, region_id):
		refusals.append("army %d could not march into %s: %s" % [
			army_id, region_id, _last_rejection()])
	elif not transfer_region(region_id, player):
		# Unreachable through `move_army` succeeding above (the region is known and
		# the seat is seated), and reported rather than assumed away: an army that
		# marched into ground its seat does not own is exactly the ghost state this
		# whole transaction is built to make impossible.
		refusals.append("region %s did not change hands: %s" % [region_id, _last_rejection()])
	if not refusals.is_empty():
		return {"ok": false, "refusals": refusals, "region": region_id, "player": player, "army": army_id}
	pending_claim = {}
	# The event retail's own notice is named after: `APT:LivingWorldRegionTakenNotice`
	# ("%s taken!"), which every region names as its `ConqueredNotice` and which is
	# a different string from the one a won battle raises.
	events.append({
		"kind": "region_claimed",
		"region": region_id,
		"player": player,
		"army": army_id,
		"turn": turn_index,
	})
	last_command_result = true
	return {"ok": true, "refusals": refusals, "region": region_id, "player": player, "army": army_id}


## Remove an army from the world entirely - the losing side of a battle. Fails
## closed on an unknown id so a double-remove can never look like a success.
func remove_army(army_id: int) -> bool:
	if not armies.has(army_id):
		return _reject("unknown army %d" % army_id)
	var army := armies[army_id] as Dictionary
	var region_id := String(army.get("region", ""))
	# A destroyed hero army does not erase the hero - it FELLS them. The ledger
	# keeps the name, the level they fell at and the turn it happened, which is
	# the whole record a future revival rule (or a battle report) needs and the
	# army table can no longer provide.
	var hero_template := String(army.get("hero_template", ""))
	if String(army.get("kind", "")) == ARMY_HERO and heroes.has(hero_template):
		var ledger := heroes[hero_template] as Dictionary
		ledger["fallen"] = true
		ledger["fell_turn"] = turn_index
	armies.erase(army_id)
	events.append({
		"kind": "army_removed",
		"army": army_id,
		"region": region_id,
		"turn": turn_index,
	})
	last_command_result = true
	return true


## Open the battle transaction. `commitment` is the record `wotr_battle.gd`
## derives from a handoff brief; this file does not build it and does not
## interpret it beyond the two invariants it must enforce itself.
##
## Strictly one battle at a time. A second `begin_battle()` over a live one is
## refused rather than overwritten: overwriting would silently strand the first
## battle's committed armies, which is a lost-update with no symptom until the
## roster is counted much later.
##
## THE COMMITMENT IS VALIDATED, not merely stored. Everything admitted here
## enters `authoritative_state()` and therefore the strategic hash, and it is the
## record `wotr_battle.gd` will later read to decide which regions change hands
## and which armies die. "The bridge only ever hands us a well-formed one" is
## caller discipline, and caller discipline is not a guarantee: an ad-hoc
## dictionary that merely names a real region used to be accepted, hash and all,
## and would then have resolved a battle with no attacker, no sides and no digest
## to check the tactical configuration against. `BATTLE_COMMITMENT_FIELDS` is
## checked exhaustively in both directions and the invariants this file owns -
## seated, distinct sides; distinct teams; a non-empty committed force; a
## well-formed digest - are checked here too.
func begin_battle(commitment: Dictionary) -> bool:
	if phase != PHASE_TACTICAL:
		return _reject("an attack may only be committed in the tactical phase")
	if commitment.is_empty():
		return _reject("battle commitment is empty")
	if not pending_battle.is_empty():
		return _reject("a battle is already in flight in %s" % String(pending_battle.get("region", "")))
	# A CLAIM IS ALSO A TRANSACTION IN FLIGHT. Both end by handing the turn on, so
	# admitting a battle over an open claim would hand it on twice and let one seat
	# take two regions in a turn.
	if not pending_claim.is_empty():
		return _reject("a claim is already in flight in %s" % String(pending_claim.get("region", "")))
	var schema_reason := _battle_commitment_refusal(commitment)
	if schema_reason != "":
		return _reject(schema_reason)
	var region_id := String(commitment.get("region", ""))
	if world == null or not world.has_region(region_id):
		return _reject("unknown region %s" % region_id)
	pending_battle = commitment.duplicate(true)
	phase = PHASE_BATTLE
	events.append({"kind": "battle_begun", "region": region_id, "turn": turn_index})
	last_command_result = true
	return true


## Close the battle transaction without applying a result. Used when a battle is
## abandoned; the applied-result path lives in `wotr_battle.gd` because it needs
## the tactical winner, which this file deliberately knows nothing about.
func clear_battle() -> bool:
	if pending_battle.is_empty():
		return _reject("no battle is in flight")
	var region_id := String(pending_battle.get("region", ""))
	pending_battle = {}
	events.append({"kind": "battle_cleared", "region": region_id, "turn": turn_index})
	last_command_result = true
	return true


## Write a battle's ATTRITION back to an army: the units that survived, with the
## hitpoints they survived on. This is the outcome path auto-resolve needs and
## `apply_outcome(winner_team)` cannot provide, because auto-resolve does not
## return a boolean - it returns who is left and how hurt they are.
##
## An army whose survivor list is EMPTY is REMOVED, not left standing at zero
## strength: an army with no units is not an army, and leaving one on the board
## would let a wiped force keep holding a region.
##
## Fails closed on an unknown army, and on a survivor list carrying a unit whose
## `army_id` is somebody else's - a mis-keyed writeback would silently move
## units between armies, which is a lost-update with no symptom until a much
## later battle counts the wrong roster.
func apply_attrition(army_id: int, survivors: Array) -> bool:
	if not armies.has(army_id):
		return _reject("unknown army %d" % army_id)
	var kept: Array[Dictionary] = []
	for row in survivors:
		var unit: Dictionary = row
		if int(unit.get("army_id", army_id)) != army_id:
			return _reject("attrition for army %d carries a unit belonging to army %d" % [
				army_id, int(unit.get("army_id", -1))])
		if int(unit.get("hitpoints_milli", 0)) < 0:
			return _reject("attrition for army %d carries negative hitpoints" % army_id)
		kept.append(unit.duplicate(true))
	if kept.is_empty():
		return remove_army(army_id)
	var army := armies[army_id] as Dictionary
	army["units"] = kept
	# THE LEDGER FOLLOWS THE UNITS. A hero's level lives on their auto-resolve
	# unit through a battle; writing the survivors back is the one moment the
	# campaign-scoped record can learn it. Highest survivor level, because a hero
	# army may carry an entourage and the hero is its senior member.
	var hero_template := String(army.get("hero_template", ""))
	if String(army.get("kind", "")) == ARMY_HERO and heroes.has(hero_template):
		var highest := 1
		for unit in kept:
			highest = maxi(highest, int(unit.get("level", 1)))
		(heroes[hero_template] as Dictionary)["level"] = highest
	events.append({
		"kind": "army_attrition",
		"army": army_id,
		"survivors": kept.size(),
		"turn": turn_index,
	})
	last_command_result = true
	return true


# =============================================================================
# CONSTRUCTION
# =============================================================================
#
# WHAT RETAIL RECORDS, and where, because until this lane landed the register
# claimed it recorded almost none of it:
#
#   `data/ini/livingworldbuildings.ini` (converted into `living-world-ui.json`'s
#   `buildings` array) - 28 `LivingWorldBuilding` blocks carrying `Type`,
#   `AvailableTo`, `StrategicResourceCost`, `TurnsToBuild`,
#   `BattleThingTemplate`, `CanDefendTerritory`, `CreateUnitDuringAutoResolve`
#   and their `ArmyToSpawn` recruit lists. `wotr_buildings.gd` is the catalogue.
#
#   `data/ini/gamedata.ini` - the four prices, `WOTR_FARM_COST = 0`,
#   `WOTR_BARRACKS_COST = 500`, `WOTR_FORTRESS_COST = 1500`,
#   `WOTR_FORGE_COST = 500`.
#
#   `livingworldregions.inc` per region - `BuildingSpot` lines (the plots; 0 to 3
#   per region across the shipped 52) and `RestrictBuildings` rows (how many of a
#   named type the territory allows: `Barracks` 1 or 2, `Fortress` 0 or 1).
#
#   `data/lotr.str` - the player-facing rules IN PROSE, and this is where most of
#   them live. The register used to call construction unrecorded; it was reading
#   the importer's JSON document, which does not read any of these files:
#     `LW:InstructionText07` (21717) - "You can construct new buildings on ANY
#     OPEN BUILD PLOT IN A TERRITORY YOU CONTROL." That sentence is the whole of
#     the ownership and plot rule below.
#     `LW:InstructionText08` (21721) - "There are FOUR BASIC STRUCTURE CLASSES to
#     choose from, Fortress, Barracks, Armory, and Farm."
#     `LW:InstructionText09` (21725) - "To build a new structure, LEFT CLICK ON A
#     BUILD PLOT to bring up the menu that shows all the available buildings that
#     can be constructed. Then left click on the building you want to construct."
#     `LW:InstructionText06` (21713) - "In the Tactical Phase, you will make all
#     your strategic choices... STRUCTURE CONSTRUCTION, UNIT TRAINING, AND ARMY
#     MOVEMENT ARE ALL DECIDED IN THIS PHASE." One phase, three verbs - which is
#     why building does NOT consume this layer's one action, below.
#     `CONTROLBAR:LW_FortRestricted` (28157) - "You cannot build a fortress here
#     since a stronghold already exists in this territory."
#     `CONTROLBAR:LW_BuildNumberRestriction` (28152) - "Number allowed in
#     territory: %d"
#     `CONTROLBAR:LW_Structure_BuildTimeSingular` (28142) - "Build Time: %d
#     Turn\nCost: %d"
#     `STRATEGICHUD:RegionBuildPlotsHelp` - "Current number of build plots
#     used/Total number of build plots available"
#     `STRATEGICHUD:TreasuryWarningPopUpMessage` (51238) - "You will not be able
#     to build any new units or buildings on the map (BESIDES FARMS) until your
#     Treasury is no longer empty", which is retail stating in prose the same
#     rule `WOTR_FARM_COST = 0` states in numbers.
#
# WHAT RETAIL DOES NOT RECORD, and is therefore named project-authored in
# `wotr_strategic_gaps.gd` rather than presented as parity: which PLOT a
# structure lands on when the caller does not name one (retail's player clicks a
# specific foundation), and the one rule retail states ONLY in prose and this
# layer cannot enforce - "only one structure per territory can be under
# construction at a time" (`LW:InstructionText10`, 21729), which is unobservable
# here because every one of the 28 shipped blocks is `TurnsToBuild = 1`.

## The build plots a region authors - retail's `BuildingSpot` count. A region
## that authors none has zero, which is a fact rather than a missing value.
func plot_count(region_id: String) -> int:
	if world == null:
		return 0
	return int(world.region(region_id).get("building_spot_count", 0))


## The standing structure records in a region, sorted by plot. Never null.
func structures_in_region(region_id: String) -> Array:
	return (region_structures.get(region_id, []) as Array)


## Standing strategic building ids in a region, sorted. Empty when none.
##
## This is the projection `wotr_handoff.gd` puts on a battle brief. It returns
## the `building` field of each standing record - a concrete `LWB_*` id where a
## catalogue resolved it, and retail's verbatim `LW_*` scenario token where none
## did, which is the same value this function returned before plots existed.
func buildings_in_region(region_id: String) -> PackedStringArray:
	var ids: Array[String] = []
	for row in structures_in_region(region_id):
		ids.append(String((row as Dictionary).get("building", "")))
	ids.sort()
	return PackedStringArray(ids)


## The lowest plot index in `region_id` nothing stands on, or -1 when the region
## is full (or authors no plots at all).
##
## LOWEST-FREE IS PROJECT-AUTHORED and named as such in the gap register. Retail
## lets the player click a specific foundation; this layer's callers may name a
## plot and `build_structure()` honours it, but a caller that does not - the
## opponent, a scenario's `SpawnBuildings` row - needs a rule, and the rule has
## to be reproducible because the plot index enters the strategic hash.
func free_plot(region_id: String) -> int:
	var total := plot_count(region_id)
	if total <= 0:
		return -1
	var taken: Dictionary = {}
	for row in structures_in_region(region_id):
		taken[int((row as Dictionary).get("plot", -1))] = true
	for index in range(total):
		if not taken.has(index):
			return index
	return -1


## How many of `type_name` a region allows, or -1 for "retail authors no limit".
##
## Retail's `RestrictBuildings` rows name only `Barracks` and `Fortress` across
## all 52 shipped regions, so an Armory and a Farm are limited by PLOTS alone.
## That is a read of retail's table, not a permission invented here.
func type_limit(region_id: String, type_name: String) -> int:
	if world == null or type_name.is_empty():
		return -1
	for row in world.region(region_id).get("restrict_buildings", []) as Array:
		var restriction := row as Dictionary
		for named in restriction.get("buildings", []) as Array:
			if String(named) == type_name:
				return int(restriction.get("numberAllowed", -1))
	return -1


## How many structures of `type_name` already stand in a region, whoever owns
## them. Whoever, because retail's restriction is on the TERRITORY - "Number
## allowed in territory" - not on the seat.
func count_of_type(region_id: String, type_name: String) -> int:
	var total := 0
	for row in structures_in_region(region_id):
		if String((row as Dictionary).get("type", "")) == type_name:
			total += 1
	return total


## A seat's treasure. 0 for an unseated index, never negative.
func treasure(player: int) -> int:
	return maxi(0, int(treasury.get(player, 0)))


## WHY `player` MAY NOT RAISE `building_id` IN `region_id`, or "" when they may.
##
## Every clause is checked in a fixed order so the sentence a player is shown
## does not depend on dictionary iteration, and every clause names the rule and
## where it comes from. This function is PURE - `build_structure()` calls it and
## refuses on a non-empty answer, so the offer a screen draws and the refusal it
## gets back can never disagree.
func build_refusal(player: int, region_id: String, building_id: String, plot: int = -1) -> String:
	if world == null or not world.has_region(region_id):
		return "there is no region called '%s'" % region_id
	if player < 0 or player >= players.size():
		return "seat %d is not seated in this campaign" % player
	if building_catalogue == null:
		return ("nothing can be built: no building catalogue is bound, so retail's "
			+ "LivingWorldBuilding blocks and their WOTR_*_COST prices are not "
			+ "loaded. Call wotr_session.load_building_catalogue().")
	var record: Dictionary = building_catalogue.building(building_id)
	if record.is_empty():
		return "'%s' is not a strategic building retail authors" % building_id
	if owner_of(region_id) != player:
		return "%s is not held by seat %d, and a seat builds only on ground it holds" % [
			region_id, player]
	var template := String((players[player] as Dictionary).get("template", ""))
	if String(record.get("available_to", "")) != template:
		# Retail's own `AvailableTo` field. A Mordor seat cannot raise an Elven
		# barracks, and the reason is the data rather than a taste judgement here.
		return "%s is available to %s, and seat %d plays %s" % [
			building_id, String(record.get("available_to", "")), player, template]
	var total_plots := plot_count(region_id)
	if total_plots <= 0:
		return "%s authors no build plots, so nothing can be raised there" % region_id
	var chosen := plot
	if chosen < 0:
		chosen = free_plot(region_id)
	if chosen < 0:
		return "all %d build plots in %s are already built on" % [total_plots, region_id]
	if chosen >= total_plots:
		return "%s has %d build plots, so there is no plot %d" % [region_id, total_plots, chosen]
	for row in structures_in_region(region_id):
		if int((row as Dictionary).get("plot", -1)) == chosen:
			return "build plot %d in %s already carries %s" % [
				chosen, region_id, String((row as Dictionary).get("building", ""))]
	var type_name := String(record.get("type", ""))
	var limit := type_limit(region_id, type_name)
	if limit >= 0 and count_of_type(region_id, type_name) >= limit:
		if type_name == BuildingsScript.TYPE_FORTRESS and limit == 0:
			# Retail's own sentence for this exact case, `CONTROLBAR:LW_FortRestricted`.
			# The regions that carry `RestrictBuildings Fortress 0` are the ones whose
			# `livingworldregions.inc` block already authors a permanent `Fortress`.
			return "you cannot build a fortress here since a stronghold already exists in this territory"
		# Retail's own label for the general case, `CONTROLBAR:LW_BuildNumberRestriction`.
		return "%s already has %d %s; number allowed in territory: %d" % [
			region_id, count_of_type(region_id, type_name), type_name, limit]
	# RETAIL'S ONE-AT-A-TIME RULE, and this is the one clause retail states ONLY
	# in prose. Its own tutorial: "All structures cost a number of turns to build
	# and ONLY ONE STRUCTURE PER TERRITORY CAN BE UNDER CONSTRUCTION AT A TIME"
	# (`LW:InstructionText10`, `data/lotr.str` line 21729). No ini authors it.
	#
	# THE READING, and it is named `one_structure_per_territory_per_turn` in
	# `wotr_strategic_gaps.gd` because the reading is this project's: RotWK
	# compressed every `TurnsToBuild` to 1 (the original 2s and 3s are commented
	# out in place in `livingworldbuildings.ini`), so there is no observable
	# "under construction" window for the rule to govern - a structure ordered
	# this turn is standing at the start of the next one either way. The rule's
	# EFFECT, though, is perfectly observable and it is the whole of what it was
	# for: a territory gains at most one structure per turn.
	#
	# It matters. `WOTR_FARM_COST = 0`, so without this clause a seat holding
	# eight two-plot regions fills all sixteen plots on turn one for nothing and
	# opens turn two on +4800 a turn. That is not retail's War of the Ring; it is
	# retail's price list with retail's pacing rule left out.
	for row in structures_in_region(region_id):
		if int((row as Dictionary).get("turn", -1)) == turn_index:
			return "%s already began a structure this turn, and only one structure per territory can be under construction at a time" % region_id
	var cost := int(record.get("cost", -1))
	if cost < 0:
		return "%s costs %s, and %s" % [
			building_id, String(record.get("cost_macro", "")), String(record.get("cost_reason", ""))]
	if treasure(player) < cost:
		return "%s costs %d and seat %d has %d in the treasury" % [
			building_id, cost, player, treasure(player)]
	return ""


func can_build(player: int, region_id: String, building_id: String, plot: int = -1) -> bool:
	return build_refusal(player, region_id, building_id, plot) == ""


## RAISE A STRUCTURE. Returns `{ok, refusal, region, plot, building, type, cost,
## treasury_before, treasury_after}`.
##
## THE TREASURY IS SPENT AND THE STRUCTURE STANDS, or neither happens: the whole
## transaction is validated by `build_refusal()` before one byte of state moves,
## which is the same all-or-nothing discipline `restore()` and `apply_claim()`
## keep and for the same reason - a seat charged for a building that is not there
## is a defect with no symptom until much later.
##
## IT DOES NOT END THE TURN, and that is RETAIL'S rule rather than a convenience.
## Retail's own tutorial narration puts all three verbs in one phase:
## "In the Tactical Phase, you will make all your strategic choices regarding the
## expansion of your kingdom. STRUCTURE CONSTRUCTION, UNIT TRAINING, AND ARMY
## MOVEMENT ARE ALL DECIDED IN THIS PHASE" (`LW:InstructionText06`,
## `data/lotr.str` line 21713), and `APT:TurnPhaseDescriptionPlanning` reads
## "Build units/structures. Move Heroes/garrisons. End Phase when done." A seat
## that had to choose between raising a farm and taking a region would be playing
## a game retail did not ship.
func build_structure(player: int, region_id: String, building_id: String, plot: int = -1) -> Dictionary:
	if phase != PHASE_TACTICAL:
		_reject("construction is only legal in the tactical phase")
		return {}
	var refusal := build_refusal(player, region_id, building_id, plot)
	if refusal != "":
		_reject(refusal)
		return {
			"ok": false, "refusal": refusal, "region": region_id, "plot": -1,
			"building": building_id, "type": "", "cost": 0,
			"treasury_before": treasure(player), "treasury_after": treasure(player),
		}
	var record: Dictionary = building_catalogue.building(building_id)
	var chosen := plot if plot >= 0 else free_plot(region_id)
	var cost := int(record.get("cost", 0))
	var before := treasure(player)
	# NOTHING BELOW CAN FAIL. `build_refusal()` proved the plot is free, the type
	# is allowed and the treasury covers it.
	treasury[player] = before - cost
	_stand_structure(region_id, {
		"building": building_id,
		"owner": player,
		"plot": chosen,
		"token": "",
		"turn": turn_index,
		"type": String(record.get("type", "")),
	})
	events.append({
		"kind": "structure_built",
		"region": region_id,
		"player": player,
		"plot": chosen,
		"building": building_id,
		"cost": cost,
		"turn": turn_index,
	})
	last_command_result = true
	return {
		"ok": true, "refusal": "", "region": region_id, "plot": chosen,
		"building": building_id, "type": String(record.get("type", "")), "cost": cost,
		"treasury_before": before, "treasury_after": treasure(player),
	}


## TEAR A STRUCTURE DOWN, freeing its plot. Retail has this verb and names it -
## `STRATEGICHUD:DemolishBuildingsChecklistItem`, "Demolish existing structures
## to make build plots available to rebuild new structures."
##
## NOTHING IS REFUNDED. Retail prices no refund for a structure anywhere (it
## prices one for a DISBANDED UNIT - `LW:UnitRefundHelpText`, "Treasury Refund if
## Disbanded +%d" - and the contrast is the evidence: retail wrote a refund where
## it meant one). Named project-authored as `demolition_refunds_nothing`.
func demolish_structure(player: int, region_id: String, plot: int) -> Dictionary:
	if phase != PHASE_TACTICAL:
		_reject("demolition is only legal in the tactical phase")
		return {}
	if world == null or not world.has_region(region_id):
		_reject("there is no region called '%s'" % region_id)
		return {"ok": false, "refusal": "there is no region called '%s'" % region_id,
			"region": region_id, "plot": plot, "building": ""}
	if owner_of(region_id) != player:
		var not_yours := "%s is not held by seat %d" % [region_id, player]
		_reject(not_yours)
		return {"ok": false, "refusal": not_yours, "region": region_id, "plot": plot, "building": ""}
	var standing: Array = structures_in_region(region_id)
	for index in range(standing.size()):
		var row := standing[index] as Dictionary
		if int(row.get("plot", -1)) != plot:
			continue
		var razed := String(row.get("building", ""))
		var kept: Array = standing.duplicate(true)
		kept.remove_at(index)
		if kept.is_empty():
			region_structures.erase(region_id)
		else:
			region_structures[region_id] = kept
		events.append({
			"kind": "structure_demolished", "region": region_id, "player": player,
			"plot": plot, "building": razed, "turn": turn_index,
		})
		last_command_result = true
		return {"ok": true, "refusal": "", "region": region_id, "plot": plot, "building": razed}
	var empty := "build plot %d in %s carries nothing" % [plot, region_id]
	_reject(empty)
	return {"ok": false, "refusal": empty, "region": region_id, "plot": plot, "building": ""}


# =============================================================================
# STRENGTHEN ARMY (AUTO-RESOLVE ONLY)
# =============================================================================

## Pure derived report for retail's THIS_TERRIORITY StrengthenArmy effect.
## Standing structures are read afresh on every call; nothing is serialized,
## cached, or added to the strategic hash.
func strengthen_army_report(player: int, region_id: String) -> Dictionary:
	var groups: Array[Dictionary] = []
	if world == null or not world.has_region(region_id):
		return {"ok": false, "armor_multiplier": 1.0, "groups": groups,
			"refusal": "StrengthenArmy names an unknown region '%s'" % region_id}
	if player < 0 or player >= players.size():
		return {"ok": false, "armor_multiplier": 1.0, "groups": groups,
			"refusal": "StrengthenArmy names an unknown player %d" % player}
	var standing_value: Variant = region_structures.get(region_id, [])
	if typeof(standing_value) != TYPE_ARRAY:
		return {"ok": false, "armor_multiplier": 1.0, "groups": groups,
			"refusal": "standing structures in %s are malformed" % region_id}
	var by_key: Dictionary = {}
	var key_order: Array[String] = []
	for structure_index in (standing_value as Array).size():
		var structure_value: Variant = (standing_value as Array)[structure_index]
		if typeof(structure_value) != TYPE_DICTIONARY:
			return {"ok": false, "armor_multiplier": 1.0, "groups": groups,
				"refusal": "standing structure %d in %s is not a dictionary" % [structure_index, region_id]}
		var structure := structure_value as Dictionary
		var structure_refusal := _structure_record_refusal(structure)
		if structure_refusal != "":
			return {"ok": false, "armor_multiplier": 1.0, "groups": groups,
				"refusal": "standing structure %d in %s: %s" % [
					structure_index, region_id, structure_refusal]}
		if int(structure["owner"]) != player:
			continue
		if building_catalogue == null:
			return {"ok": false, "armor_multiplier": 1.0, "groups": groups,
				"refusal": "owned standing structure %s in %s requires the missing building catalogue" % [String(structure["building"]), region_id]}
		var contribution: Dictionary = building_catalogue.strengthen_army_for_building(String(structure["building"]))
		if not bool(contribution.get("ok", false)):
			return {"ok": false, "armor_multiplier": 1.0, "groups": groups,
				"refusal": String(contribution.get("refusal", "standing structure cannot be interpreted for StrengthenArmy"))}
		for row_value in contribution.get("rows", []) as Array:
			var row := row_value as Dictionary
			var key := String(row.get("bonusKey", ""))
			var table: Array = row.get("bonuses", [])
			if by_key.has(key):
				var existing := by_key[key] as Dictionary
				if existing.get("bonuses", []) != table:
					return {"ok": false, "armor_multiplier": 1.0, "groups": groups,
						"refusal": "StrengthenArmy BonusKey %s has mismatched bonus tables" % key}
				existing["count"] = int(existing["count"]) + 1
				(existing["sources"] as Array).append({"building": String(structure["building"]),
					"plot": int(structure.get("plot", -1)), "nugget": int(row.get("index", -1))})
			else:
				key_order.append(key)
				by_key[key] = {"bonusKey": key, "count": 1, "bonuses": table.duplicate(true),
					"sources": [{"building": String(structure["building"]),
						"plot": int(structure.get("plot", -1)), "nugget": int(row.get("index", -1))}]}
	var multiplier := 1.0
	for key in key_order:
		var group := (by_key[key] as Dictionary).duplicate(true)
		var selected_threshold := 0
		var selected_pct := 100.0
		for bonus_value in group["bonuses"] as Array:
			var bonus := bonus_value as Dictionary
			if int(bonus["threshold"]) <= int(group["count"]):
				selected_threshold = int(bonus["threshold"])
				selected_pct = float(bonus["armorPct"])
			else:
				break
		group["threshold"] = selected_threshold
		group["armorPct"] = selected_pct
		groups.append(group)
		multiplier *= selected_pct / 100.0
		if not is_finite(multiplier):
			return {"ok": false, "armor_multiplier": 1.0, "groups": groups,
				"refusal": "StrengthenArmy armour multipliers overflow a finite result"}
	return {"ok": true, "armor_multiplier": multiplier, "groups": groups, "refusal": ""}


# =============================================================================
# WORLD COMMAND POINTS
# =============================================================================

## Pure derived accessor for retail's active standing-structure WORLD CP effect.
## `players[player].world_cp` is StartingWorldCP, each owned standing concrete
## structure contributes its validated typed nuggets once, and the result is
## capped at that template's MaxWorldCP. No state, cache, event, or hash changes.
##
## Structure ownership follows this project's accepted region-transfer projection.
## Retail's strategic ownership/destructor callback still needs a direct trace;
## capture is therefore not claimed independently parity-proven here.
func world_command_point_report(player: int) -> Dictionary:
	var rows: Array[Dictionary] = []
	if world == null or player < 0 or player >= players.size():
		return {"ok": false, "base": 0, "bonus": 0, "limit": 0, "max": 0,
			"rows": rows, "refusal": "world command points name an unknown player %d" % player}
	var player_row := players[player] as Dictionary
	var base := int(player_row.get("world_cp", 0))
	var template_name := String(player_row.get("template", ""))
	var template: Dictionary = world.player_templates.get(template_name, {}) as Dictionary
	if not template.has("max_world_cp") or typeof(template["max_world_cp"]) != TYPE_INT:
		return {"ok": false, "base": base, "bonus": 0, "limit": base, "max": 0,
			"rows": rows, "refusal": "player template %s has no integer MaxWorldCP" % template_name}
	var maximum := int(template["max_world_cp"])
	if maximum < 0:
		return {"ok": false, "base": base, "bonus": 0, "limit": base, "max": maximum,
			"rows": rows, "refusal": "player template %s has unsupported MaxWorldCP %d" % [template_name, maximum]}
	var bonus := 0
	for region_id_value in world.region_ids:
		var region_id := String(region_id_value)
		for structure_value in structures_in_region(region_id):
			var structure := structure_value as Dictionary
			if int(structure.get("owner", NEUTRAL)) != player:
				continue
			var building_id := String(structure.get("building", ""))
			var concrete := (not String(structure.get("type", "")).is_empty()
				or String(structure.get("token", "")).is_empty())
			if building_catalogue == null:
				if concrete:
					return {"ok": false, "base": base, "bonus": 0, "limit": base,
						"max": maximum, "rows": rows, "refusal":
						"standing concrete structure %s in %s requires the missing building catalogue" % [building_id, region_id]}
				continue
			var contribution: Dictionary = building_catalogue.world_command_points_for_building(building_id)
			if not bool(contribution.get("ok", false)):
				return {"ok": false, "base": base, "bonus": 0, "limit": base,
					"max": maximum, "rows": rows, "refusal": String(contribution.get("refusal", "standing structure cannot be interpreted"))}
			var amount := int(contribution.get("bonus", 0))
			if amount < 0 or bonus > 9223372036854775807 - amount:
				return {"ok": false, "base": base, "bonus": 0, "limit": base,
					"max": maximum, "rows": rows, "refusal": "standing WORLD command-point contributions overflow a signed 64-bit total"}
			bonus += amount
			for nugget_value in contribution.get("rows", []) as Array:
				var nugget := nugget_value as Dictionary
				rows.append({"region": region_id, "plot": int(structure.get("plot", -1)),
					"building": building_id, "nugget": int(nugget.get("index", -1)),
					"scope": String(nugget.get("scope", "")), "amount": int(nugget.get("amount", 0))})
	var limit := maximum
	if base < 0:
		# Preserve a negative authored base (no invented lower clamp) while avoiding
		# signed overflow before applying the upper MaxWorldCP cap.
		if bonus <= 9223372036854775807 + base:
			limit = mini(base + bonus, maximum)
	elif base < maximum and bonus <= maximum - base:
		limit = base + bonus
	return {"ok": true, "base": base, "bonus": bonus, "limit": limit,
		"max": maximum, "rows": rows, "refusal": ""}


# =============================================================================
# THE TREASURY
# =============================================================================

## WHAT `player` EARNS AT THE START OF THEIR TURN. Returns
## `{total, fortresses, farms, fertile_regions, rows}` - `rows` being one
## human-readable line per contributing source, so a screen can show the sum
## broken out rather than a bare number nobody can check.
##
## ----------------------------------------------------------------------------
## RETAIL'S HALF, with citations
## ----------------------------------------------------------------------------
## WHAT IS SUMMED IS RETAIL'S, in one sentence retail wrote itself:
##   `APT:NewWOTRFeature4Desc` (`data/lotr.str` 53418) - "ACCUMULATE TREASURY
##   FROM FARMS AND FORTRESSES to recruit units and construct buildings on the
##   strategic map." Its title, `APT:NewWOTRFeature4`, is "Strategic Treasury".
## And per building, in `data/ini/livingworldbuildings.ini`, as a typed nugget
## the converter carries: `BuildingNugget IncreaseTreasury / TreasureAmount =
## GAIN_PER_FORTRESS` on all seven fortresses (line 53 and six siblings) and
## `= GAIN_PER_FARM` on all seven farms (line 322 and six siblings).
##
## THE CADENCE IS RETAIL'S, and it is stated in prose in the shipped string
## table rather than in any ini - which is exactly where the neutral-ground claim
## rule was eventually found, after two passes concluded from the ini tree alone
## that no such rule existed:
##
##   `STRATEGICHUD:StatsCTreasuryIncomeHelp` (30446) - "Treasury increases by
##   this amount at the start of each turn."
##   `STRATEGICHUD:StatsTreasuryIncomeTitle` / `StatsTreasuryIncome` - "Treasury
##   Income", "+%d". Retail draws this number on its own HUD, and the authoring
##   comment above it says it is the "SUM of all region Treasury Income Bonuses".
##   `CONTROLBAR:LW_FarmTreasuryBonus` (28191) - "Treasury Bonus: +%d per turn."
##   `CONTROLBAR:LW_ToolTip_GondorFarm` and its six siblings - "Structure Type:
##   Farm\nIncreases treasury."
##
## THE AMOUNTS ARE RETAIL'S, three `#define`s in one contiguous `;//---WOTR---`
## block of `gamedata.ini` (8453-8460) immediately above the four `WOTR_*_COST`
## prices this layer charges: `GAIN_PER_FORTRESS = 300`, `GAIN_PER_FARM = 300`,
## `FERTILE_TERRITORY_BONUS = 500`.
##
## THE FERTILE TERM IS RETAIL'S TREASURY QUANTITY, established by elimination
## over retail's own region-bonus vocabulary and already recorded as such in
## `wotr_macros.gd`'s header. Every other region bonus retail authors has its own
## player-facing string naming it something that is NOT treasure - `army` is
## "+%d Command Points", `resource` is "+%d%% Resource Multiplier",
## `extraStartResources` is "Extra Resources at the Start of Battles",
## `buildingDiscount` is "Discount when Building Structures", `freeBuilder` is
## "Free Builder in Battles" - which leaves `LW:RegionTreasuryBonus` ("+%d
## Treasure") and `STRATEGICHUD:RegionTreasuryIncomeBonusTooltip` ("Treasury
## Income Bonus") with exactly one region field to describe: `fertileTerritory`.
##
## ----------------------------------------------------------------------------
## THIS PROJECT'S HALF, and it is stated rather than blended in
## ----------------------------------------------------------------------------
## Retail records the ingredients and the cadence; it does not record the SUM.
## The arithmetic below - that the three terms add, that there is no base income
## and no multiplier between them, and that a region's permanent authored
## stronghold is NOT a fortress for `GAIN_PER_FORTRESS` (only a structure
## standing on a plot is) - is this project's, and is named
## `strategic_treasury_income_arithmetic` in `wotr_strategic_gaps.gd`'s
## `PROJECT_AUTHORED_RULES`.
##
## Note what is deliberately NOT summed here: the per-region `resource` bonus.
## Retail's own string calls it a percentage multiplier on RESOURCE PRODUCTION IN
## BATTLE, not treasure, and folding a "+10%" into a treasury measured in
## hundreds would be this project deciding what retail meant.
func turn_income(player: int) -> Dictionary:
	var rows: Array[String] = []
	var total := 0
	var fortresses := 0
	var farms := 0
	var fertile := 0
	if world == null or player < 0 or player >= players.size():
		return {"total": 0, "fortresses": 0, "farms": 0, "fertile_regions": 0,
			"rows": PackedStringArray(rows)}
	var per_fortress := 0
	var per_farm := 0
	var per_fertile := 0
	if building_catalogue != null:
		per_fortress = building_catalogue.income_for_type(BuildingsScript.TYPE_FORTRESS)
		per_farm = building_catalogue.income_for_type(BuildingsScript.TYPE_RESOURCE)
		per_fertile = int(building_catalogue.resolved_macros.get(BuildingsScript.FERTILE_MACRO, 0))
	for region_id in regions_owned_by(player):
		# THE FERTILE TERM. Retail authors it as a MACRO on the region
		# (`FertileTerritoryBonus = FERTILE_TERRITORY_BONUS`), which is why the
		# document carries it under `bonus_macros` and its literal `bonuses` entry
		# is 0. Both readings are honoured: a literal number if a region ever
		# carries one, otherwise the macro resolved through retail's own table.
		var region := world.region(region_id)
		var literal := int((region.get("bonuses", {}) as Dictionary).get(BuildingsScript.FERTILE_FIELD, 0))
		var authored_macro := String(
			(region.get("bonus_macros", {}) as Dictionary).get(BuildingsScript.FERTILE_FIELD, ""))
		var region_bonus := literal
		if region_bonus == 0 and authored_macro == BuildingsScript.FERTILE_MACRO:
			region_bonus = per_fertile
		if region_bonus > 0:
			fertile += 1
			total += region_bonus
			rows.append("%s is fertile territory: +%d" % [region_id, region_bonus])
		for row in structures_in_region(region_id):
			var structure := row as Dictionary
			if int(structure.get("owner", NEUTRAL)) != player:
				continue
			var type_name := String(structure.get("type", ""))
			if type_name == BuildingsScript.TYPE_FORTRESS and per_fortress > 0:
				fortresses += 1
				total += per_fortress
				rows.append("%s in %s: +%d" % [String(structure.get("building", "")), region_id, per_fortress])
			elif type_name == BuildingsScript.TYPE_RESOURCE and per_farm > 0:
				farms += 1
				total += per_farm
				rows.append("%s in %s: +%d" % [String(structure.get("building", "")), region_id, per_farm])
	return {
		"total": total, "fortresses": fortresses, "farms": farms,
		"fertile_regions": fertile, "rows": PackedStringArray(rows),
	}


## Pay `player` their turn's income and return what was paid. Called by
## `advance_turn()` for the seat whose turn is BEGINNING, which is retail's own
## stated cadence ("Treasury increases by this amount at the start of each
## turn"); a defeated seat earns nothing.
func accrue_income(player: int) -> int:
	if player < 0 or player >= players.size():
		return 0
	if bool((players[player] as Dictionary).get("defeated", false)):
		return 0
	var earned := int(turn_income(player).get("total", 0))
	if earned <= 0:
		return 0
	treasury[player] = treasure(player) + earned
	events.append({"kind": "income", "player": player, "amount": earned, "turn": turn_index})
	return earned


# --- construction internals ---------------------------------------------------

## Insert one validated record into `region_structures`, keeping the region's
## list sorted by plot. Sorted at write rather than at read because the list is
## hashed: two peers that built in a different ORDER must still hash the same.
func _stand_structure(region_id: String, record: Dictionary) -> bool:
	var schema_reason := _structure_record_refusal(record)
	if schema_reason != "":
		return _reject(schema_reason)
	var standing: Array = (region_structures.get(region_id, []) as Array).duplicate(true)
	standing.append(record.duplicate(true))
	standing.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return int(a.get("plot", 0)) < int(b.get("plot", 0)))
	region_structures[region_id] = standing
	last_command_result = true
	return true


## `STRUCTURE_RECORD_FIELDS`, checked exhaustively in both directions.
func _structure_record_refusal(record: Dictionary) -> String:
	for field in _sorted_strings(STRUCTURE_RECORD_FIELDS.keys()):
		if not record.has(field):
			return "structure record is missing field '%s'" % field
		if typeof(record[field]) != int(STRUCTURE_RECORD_FIELDS[field]):
			return "structure record field '%s' has the wrong type" % field
	for field in _sorted_strings(record.keys()):
		if not STRUCTURE_RECORD_FIELDS.has(field):
			return "structure record carries unknown field '%s'" % field
	if String(record["building"]).strip_edges().is_empty():
		return "structure record names no building"
	if int(record["plot"]) < 0:
		return "structure record stands on plot %d" % int(record["plot"])
	return ""


## The ledger row for a hero, or `{}` when the hero has never entered the world.
func hero_record(hero_template: String) -> Dictionary:
	return (heroes.get(hero_template, {}) as Dictionary).duplicate(true)


## A hero's current campaign level: the ledger's, or 0 for a hero the ledger has
## never seen - 0 rather than 1, so "unknown" can never impersonate "fresh".
func hero_level(hero_template: String) -> int:
	if not heroes.has(hero_template):
		return 0
	return int((heroes[hero_template] as Dictionary).get("level", 1))


## The heroes of `player` that have fallen, sorted by template so the order a
## screen or a brief lists them in is reproducible.
func fallen_heroes(player: int) -> Array[Dictionary]:
	var fallen: Array[Dictionary] = []
	for template in _sorted_strings(heroes.keys()):
		var ledger := heroes[template] as Dictionary
		if int(ledger.get("owner", NEUTRAL)) != player:
			continue
		if bool(ledger.get("fallen", false)):
			fallen.append(ledger.duplicate(true))
	return fallen


## EVALUATE the campaign's chosen victory type against the current board.
## PURE - nothing is mutated; `apply_victory()` is the writer. Returns
## `{ok, reason, defeated_players, defeated_teams, victorious_team}`.
##
## The conditions are retail's own rows, read field-for-field:
##
##   PlayerDefeatCondition   a player on a listed team is defeated when their
##                           region count falls to `numControlledRegions-
##                           LessOrEqualTo`, or - `LoseIfCapitalLost` - when
##                           their capital (`StartRegion`) is no longer theirs.
##   TeamDefeatCondition     the same comparisons over a team's total.
##   TeamVictoryCondition    a listed team wins by holding every region in
##                           `controlledRegions`, or by holding at least
##                           `numControlledRegionsGreaterOrEqualTo` regions.
##
## Elimination types author NO team victory condition; there the winner is the
## last team with an undefeated seat, which is the only reading under which the
## authored defeat conditions can end the campaign at all. A condition shape
## retail never authored (a TEAM row demanding `LoseIfCapitalLost`, which no
## shipped row sets and which names no team-level capital) refuses evaluation
## by name rather than inventing a meaning.
func evaluate_victory() -> Dictionary:
	if world == null:
		return _victory_refused("no world is bound")
	var scenario := world.scenario(scenario_name)
	if scenario.is_empty():
		return _victory_refused("no scenario named '%s' to read victory conditions from" % scenario_name)
	var types: Array = scenario.get("victory_types", []) as Array
	if types.is_empty():
		return _victory_refused("scenario %s authors no victory types" % scenario_name)
	if victory_type < 0 or victory_type >= types.size():
		return _victory_refused("victory type %d is not one of the %d authored" % [victory_type, types.size()])
	var chosen := types[victory_type] as Dictionary

	# Region counts, once, over sorted region ids.
	var player_regions: Dictionary = {}
	var team_regions: Dictionary = {}
	for index in range(players.size()):
		player_regions[index] = 0
	for region_id in world.region_ids:
		var owner := owner_of(region_id)
		if owner == NEUTRAL:
			continue
		player_regions[owner] = int(player_regions.get(owner, 0)) + 1
		var team := int((players[owner] as Dictionary).get("team", 0))
		team_regions[team] = int(team_regions.get(team, 0)) + 1

	var newly_defeated: Array[int] = []
	for row in chosen.get("player_defeat_conditions", []) as Array:
		var condition := row as Dictionary
		for index in range(players.size()):
			var seat := players[index] as Dictionary
			if not (condition.get("teams", PackedInt32Array()) as PackedInt32Array).has(int(seat.get("team", 0))):
				continue
			if bool(seat.get("defeated", false)) or newly_defeated.has(index):
				continue
			var lost := false
			var ceiling := int(condition.get("max_controlled_regions", -1))
			if ceiling >= 0 and int(player_regions.get(index, 0)) <= ceiling:
				lost = true
			if bool(condition.get("lose_if_capital_lost", false)):
				var capital := String(seat.get("capital", ""))
				if not capital.is_empty() and owner_of(capital) != index:
					lost = true
			if lost:
				newly_defeated.append(index)
	newly_defeated.sort()

	var defeated_teams: Array[int] = []
	for row in chosen.get("team_defeat_conditions", []) as Array:
		var condition := row as Dictionary
		if bool(condition.get("lose_if_capital_lost", false)):
			return _victory_refused(
				"a team defeat condition demands LoseIfCapitalLost, which retail never "
				+ "authors and which names no team-level capital to test")
		var ceiling := int(condition.get("max_controlled_regions", -1))
		if ceiling < 0:
			continue
		for team in condition.get("teams", PackedInt32Array()) as PackedInt32Array:
			if int(team_regions.get(int(team), 0)) <= ceiling and not defeated_teams.has(int(team)):
				defeated_teams.append(int(team))
	defeated_teams.sort()

	var victorious := NEUTRAL
	for row in chosen.get("team_victory_conditions", []) as Array:
		var condition := row as Dictionary
		for team in condition.get("teams", PackedInt32Array()) as PackedInt32Array:
			if victorious != NEUTRAL:
				break
			var required: PackedStringArray = condition.get("controlled_regions", PackedStringArray())
			if not required.is_empty():
				var holds_all := true
				for region_id in required:
					var owner := owner_of(region_id)
					if owner == NEUTRAL or int((players[owner] as Dictionary).get("team", 0)) != int(team):
						holds_all = false
						break
				if holds_all:
					victorious = int(team)
				continue
			var floor_count := int(condition.get("min_controlled_regions", -1))
			if floor_count >= 0 and int(team_regions.get(int(team), 0)) >= floor_count:
				victorious = int(team)

	# ELIMINATION: with no explicit victory row, the last team standing wins.
	# "Standing" folds in every defeat this evaluation found plus every one
	# already applied, so a single call sees the campaign end rather than
	# needing a second pass over its own output.
	if victorious == NEUTRAL:
		var alive: Array[int] = []
		for index in range(players.size()):
			var seat := players[index] as Dictionary
			var team := int(seat.get("team", 0))
			if bool(seat.get("defeated", false)) or newly_defeated.has(index) or defeated_teams.has(team):
				continue
			if not alive.has(team):
				alive.append(team)
		if alive.size() == 1 and players.size() >= 2:
			victorious = alive[0]

	return {
		"ok": true,
		"reason": "",
		"defeated_players": PackedInt32Array(newly_defeated),
		"defeated_teams": PackedInt32Array(defeated_teams),
		"victorious_team": victorious,
	}


## Evaluate AND write: every newly defeated seat is marked. Returns the
## evaluation with `applied` (the players actually marked this call) added.
func apply_victory() -> Dictionary:
	var evaluation := evaluate_victory()
	if not bool(evaluation.get("ok", false)):
		return evaluation
	var applied: Array[int] = []
	for index in evaluation.get("defeated_players", PackedInt32Array()) as PackedInt32Array:
		if set_defeated(int(index)):
			applied.append(int(index))
	for index in range(players.size()):
		var seat := players[index] as Dictionary
		var team := int(seat.get("team", 0))
		if (evaluation.get("defeated_teams", PackedInt32Array()) as PackedInt32Array).has(team):
			if not bool(seat.get("defeated", false)) and set_defeated(index):
				applied.append(index)
	applied.sort()
	evaluation["applied"] = PackedInt32Array(applied)
	return evaluation


func _victory_refused(reason: String) -> Dictionary:
	return {
		"ok": false,
		"reason": reason,
		"defeated_players": PackedInt32Array(),
		"defeated_teams": PackedInt32Array(),
		"victorious_team": NEUTRAL,
	}


## Mark a seat defeated. Idempotent-safe: reports false when already defeated.
func set_defeated(player: int, defeated: bool = true) -> bool:
	if player < 0 or player >= players.size():
		return _reject("unknown player %d" % player)
	var seat := players[player] as Dictionary
	if bool(seat.get("defeated", false)) == defeated:
		return _reject("player %d defeat flag unchanged" % player)
	seat["defeated"] = defeated
	last_command_result = true
	return true


# --- determinism surface -----------------------------------------------------

func state_hash() -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(var_to_bytes(canonicalize(authoritative_state())))
	return context.finish().hex_encode()


func snapshot() -> PackedByteArray:
	return var_to_bytes(authoritative_state())


## ALL-OR-NOTHING. Every field is decoded and type-checked into a local BEFORE
## anything on `self` is written, and the writes then happen with no remaining
## way to fail.
##
## The half-applied shape this replaces was not theoretical: a snapshot whose
## `pending_battle` carried a non-Dictionary raised `Invalid cast` at the last
## assignment, AFTER `turn_index`, `players`, `region_owner` and `armies` had
## already been overwritten. `restore()` returned false, so a caller doing the
## right thing - checking the return and refusing to proceed - was nonetheless
## left holding a chimera: the donor's map and armies under the adopter's own
## stale battle, still in flight, still inside the hash. A restore that reports
## failure must leave the adopter exactly as it found it, or the report is worse
## than useless.
func restore(bytes: PackedByteArray) -> bool:
	if bytes.is_empty():
		return false
	var decoded: Variant = bytes_to_var(bytes)
	if typeof(decoded) != TYPE_DICTIONARY:
		return false
	var state := decoded as Dictionary
	for required in [
		["schema", TYPE_STRING], ["schema_version", TYPE_INT],
		["turn_index", TYPE_INT], ["turn_order", TYPE_PACKED_INT32_ARRAY],
		["players", TYPE_ARRAY], ["region_owner", TYPE_DICTIONARY],
		["armies", TYPE_DICTIONARY], ["next_army_id", TYPE_INT],
	]:
		var key := String((required as Array)[0])
		if not state.has(key):
			return false
		if typeof(state[key]) != int((required as Array)[1]):
			return false
	if String(state["schema"]) != SCHEMA:
		return false
	var source_version := int(state["schema_version"])
	if source_version != 1 and source_version != SCHEMA_VERSION:
		return false
	# Version 1 predates retail phases and migrates unambiguously to the opening
	# tactical phase with no retreat transaction. Version 2 requires both fields.
	var staged_phase := PHASE_TACTICAL
	var staged_retreats: Array[Dictionary] = []
	if source_version == SCHEMA_VERSION:
		if (not state.has("phase") or typeof(state["phase"]) != TYPE_STRING
				or not PHASES.has(String(state["phase"]))):
			return false
		if not state.has("pending_retreats") or typeof(state["pending_retreats"]) != TYPE_ARRAY:
			return false
		staged_phase = String(state["phase"])
		var previous_army := -1
		for value in state["pending_retreats"] as Array:
			if typeof(value) != TYPE_DICTIONARY:
				return false
			var row := value as Dictionary
			if row.size() != PENDING_RETREAT_FIELDS.size():
				return false
			for field in PENDING_RETREAT_FIELDS:
				if not row.has(field) or typeof(row[field]) != int(PENDING_RETREAT_FIELDS[field]):
					return false
			var army_id := int(row["army"])
			if army_id <= previous_army:
				return false
			previous_army = army_id
			staged_retreats.append(row.duplicate(true))

	var staged_players: Array[Dictionary] = []
	for row in state["players"] as Array:
		if typeof(row) != TYPE_DICTIONARY:
			return false
		staged_players.append((row as Dictionary).duplicate(true))
	var source_armies := state["armies"] as Dictionary
	var staged_armies: Dictionary = {}
	for key in source_armies.keys():
		if typeof(key) != TYPE_INT and typeof(key) != TYPE_FLOAT:
			return false
		if typeof(source_armies[key]) != TYPE_DICTIONARY:
			return false
		staged_armies[int(key)] = (source_armies[key] as Dictionary).duplicate(true)

	# Cross-validate every typed retreat row against the staged snapshot before
	# any live state is touched. A row is a defeated HERO army lifecycle record,
	# not a free-standing relocation request.
	for row in staged_retreats:
		var army_id := int(row["army"])
		var player := int(row["player"])
		if not staged_armies.has(army_id):
			return false
		if player < 0 or player >= staged_players.size():
			return false
		if int(row["turn"]) != int(state["turn_index"]):
			return false
		var army := staged_armies[army_id] as Dictionary
		if String(army.get("kind", "")) != ARMY_HERO:
			return false
		if String(army.get("hero_template", "")) != String(row["hero_template"]):
			return false
		if int(army.get("owner", NEUTRAL)) != player:
			return false
		if String(army.get("region", "")) != String(row["from_region"]):
			return false
	# The battle in flight rides the snapshot. It is NOT in the required list
	# above, because it is hashed empty-is-absent: a snapshot minted between
	# battles legitimately carries no such key, and demanding one would refuse
	# every ordinary strategic save. Absent therefore restores to `{}` - which is
	# the same thing the absence meant to the hash. PRESENT-BUT-NOT-A-DICTIONARY,
	# however, is a malformed snapshot and refuses the whole restore.
	var staged_battle: Dictionary = {}
	if state.has("pending_battle"):
		if typeof(state["pending_battle"]) != TYPE_DICTIONARY:
			return false
		staged_battle = (state["pending_battle"] as Dictionary).duplicate(true)
	# The claim in flight rides the snapshot under exactly the same rules: absent
	# restores to `{}` (which is what the absence meant to the hash), and
	# present-but-not-a-Dictionary refuses the whole restore.
	var staged_claim: Dictionary = {}
	if state.has("pending_claim"):
		if typeof(state["pending_claim"]) != TYPE_DICTIONARY:
			return false
		staged_claim = (state["pending_claim"] as Dictionary).duplicate(true)
	if source_version == 1 and (not staged_battle.is_empty() or not staged_claim.is_empty()):
		staged_phase = PHASE_BATTLE
	if source_version == SCHEMA_VERSION:
		if ((not staged_battle.is_empty() or not staged_claim.is_empty())
				and staged_phase != PHASE_BATTLE):
			return false
		if not staged_retreats.is_empty() and staged_phase != PHASE_RETREAT:
			return false
		if (staged_phase == PHASE_RETREAT
				and (not staged_battle.is_empty() or not staged_claim.is_empty())):
			return false

	# The battle rules ride the snapshot. They are NOT in the required list: a
	# snapshot minted before version 3 legitimately carries neither, and refusing
	# it would refuse every saved campaign. Absent restores to retail's own screen
	# defaults, which is what such a campaign was played under.
	var staged_type := normalized_battle_type(state.get("battle_type", BATTLE_TYPE_AUTO_RESOLVE_AND_RTS))
	var staged_priority := normalized_battle_priority(
		state.get("battle_type_priority", BATTLE_TYPE_AUTO_RESOLVE))

	# The scenario, victory rule, standing buildings and hero ledger follow the
	# same absent-restores-to-default discipline (a snapshot minted before they
	# existed carried none of them), and the same PRESENT-BUT-MALFORMED-REFUSES
	# discipline `pending_battle` keeps: a snapshot whose hero ledger is not a
	# dictionary is a malformed snapshot, not an empty ledger.
	var staged_scenario := String(state.get("scenario_name", ""))
	var staged_victory := normalized_victory_type(state.get("victory_type", 0))
	var staged_structures: Dictionary = {}
	if state.has("region_structures"):
		if typeof(state["region_structures"]) != TYPE_DICTIONARY:
			return false
		for key in (state["region_structures"] as Dictionary).keys():
			var rows: Variant = (state["region_structures"] as Dictionary)[key]
			if typeof(rows) != TYPE_ARRAY:
				return false
			var kept: Array = []
			for row in rows as Array:
				if typeof(row) != TYPE_DICTIONARY:
					return false
				var record := (row as Dictionary).duplicate(true)
				if _structure_record_refusal(record) != "":
					# A malformed structure is a malformed SNAPSHOT, refused whole. A
					# record admitted here would ride the hash uninspected, which is the
					# exact class of defect `STRUCTURE_RECORD_FIELDS` exists to catch on
					# the way in; it must be caught on the way back too.
					return false
				kept.append(record)
			staged_structures[String(key)] = kept
	elif state.has("region_buildings"):
		# LEGACY SNAPSHOTS. Before build plots existed this field was a bare sorted
		# token list per region, with no plot, no owner and no type. Migrated rather
		# than dropped - a saved campaign must not lose its fortresses - by seating
		# the tokens on plots 0..n-1 in their own sorted order (which is the order
		# they were hashed in, so the migration is itself reproducible) and giving
		# each one the region's CURRENT owner, which is the only owner the old
		# record ever implied.
		if typeof(state["region_buildings"]) != TYPE_DICTIONARY:
			return false
		var legacy_owners := state.get("region_owner", {}) as Dictionary
		for key in (state["region_buildings"] as Dictionary).keys():
			var tokens: Variant = (state["region_buildings"] as Dictionary)[key]
			if typeof(tokens) != TYPE_PACKED_STRING_ARRAY:
				return false
			var migrated: Array = []
			var plot := 0
			for token in tokens as PackedStringArray:
				migrated.append({
					"building": String(token),
					"owner": int(legacy_owners.get(String(key), NEUTRAL)),
					"plot": plot,
					"token": String(token),
					"turn": 0,
					"type": "",
				})
				plot += 1
			if not migrated.is_empty():
				staged_structures[String(key)] = migrated
	var staged_treasury: Dictionary = {}
	if state.has("treasury"):
		if typeof(state["treasury"]) != TYPE_DICTIONARY:
			return false
		for key in (state["treasury"] as Dictionary).keys():
			if typeof(key) != TYPE_INT and typeof(key) != TYPE_FLOAT:
				return false
			staged_treasury[int(key)] = int((state["treasury"] as Dictionary)[key])
	var staged_heroes: Dictionary = {}
	if state.has("heroes"):
		if typeof(state["heroes"]) != TYPE_DICTIONARY:
			return false
		for key in (state["heroes"] as Dictionary).keys():
			var ledger: Variant = (state["heroes"] as Dictionary)[key]
			if typeof(ledger) != TYPE_DICTIONARY:
				return false
			staged_heroes[String(key)] = (ledger as Dictionary).duplicate(true)

	# COMMIT. Nothing below can fail.
	battle_type = staged_type
	battle_type_priority = staged_priority
	scenario_name = staged_scenario
	victory_type = staged_victory
	region_structures = staged_structures
	treasury = staged_treasury
	heroes = staged_heroes
	turn_index = int(state["turn_index"])
	turn_order = PackedInt32Array(state["turn_order"])
	players = staged_players
	region_owner = (state["region_owner"] as Dictionary).duplicate(true)
	armies = staged_armies
	_next_army_id = int(state["next_army_id"])
	pending_battle = staged_battle
	pending_claim = staged_claim
	phase = staged_phase
	pending_retreats = staged_retreats
	# Derived and view-facing state is rebuilt, never restored, so a snapshot
	# can never smuggle in an event log that the hash does not cover.
	events = []
	last_command_result = null
	return true


## The full authoritative dictionary. Everything the hash covers lives here and
## nothing else does.
func authoritative_state() -> Dictionary:
	var owners: Dictionary = {}
	for region_id in _sorted_strings(region_owner.keys()):
		owners[region_id] = int(region_owner[region_id])
	var army_rows: Dictionary = {}
	var ids: Array[int] = []
	for key in armies.keys():
		ids.append(int(key))
	ids.sort()
	for id in ids:
		army_rows[id] = armies[id]
	var state := {
		"schema": SCHEMA,
		"schema_version": SCHEMA_VERSION,
		"phase": phase,
		"pending_retreats": pending_retreats,
		"world_campaign": world.campaign_name if world != null else "",
		"turn_index": turn_index,
		"turn_order": turn_order,
		"players": players,
		"region_owner": owners,
		"armies": army_rows,
		"next_army_id": _next_army_id,
		# THE CAMPAIGN'S OWN BATTLE RULES. Hashed, because they select which
		# resolution path runs, and two peers running different paths are not
		# playing the same campaign.
		"battle_type": battle_type,
		"battle_type_priority": battle_type_priority,
		# THE CAMPAIGN'S SCENARIO AND VICTORY RULE. Hashed for the same reason:
		# they select which authored conditions end the campaign, and the
		# scenario name is how the evaluator finds them at all.
		"scenario_name": scenario_name,
		"victory_type": victory_type,
	}
	# Standing structures, the treasury and the hero ledger, all EMPTY-IS-ABSENT
	# like `pending_battle` below: a campaign that never raised a building and one
	# whose last building fell would otherwise hash differently while no read can
	# tell them apart - and every pre-existing snapshot stays restorable.
	#
	# The structure rows are emitted over SORTED region ids, and each region's
	# list is already sorted by plot at write time, so the digest cannot depend on
	# the order two seats happened to build in.
	if not region_structures.is_empty():
		var structure_rows: Dictionary = {}
		for region_id in _sorted_strings(region_structures.keys()):
			structure_rows[region_id] = region_structures[region_id]
		state["region_structures"] = structure_rows
	# THE TREASURY IS HASHED, because it decides what can be built and two peers
	# disagreeing about it would disagree about the board one turn later. Emitted
	# over sorted integer seat indices, and only when some seat holds treasure -
	# a campaign whose templates author no `ScenarioStartResources` (retail's
	# `PlayerObserver` is one) hashes exactly as it did before this field existed.
	var purses: Dictionary = {}
	var seats: Array[int] = []
	for key in treasury.keys():
		if int(treasury[key]) != 0:
			seats.append(int(key))
	seats.sort()
	for seat in seats:
		purses[seat] = int(treasury[seat])
	if not purses.is_empty():
		state["treasury"] = purses
	if not heroes.is_empty():
		var hero_rows: Dictionary = {}
		for template in _sorted_strings(heroes.keys()):
			hero_rows[template] = heroes[template]
		state["heroes"] = hero_rows
	# EMPTY-IS-ABSENT, the same discipline the retail slice applies to
	# `script_env_state`: with no battle in flight the key contributes zero bytes,
	# so a strategic state that has never fought - and one that has fought and
	# resolved - hash identically to the pre-battle layer. No read can tell an
	# absent record from an empty one, so the two must not hash differently.
	if not pending_battle.is_empty():
		state["pending_battle"] = pending_battle
	# The claim in flight, under the same empty-is-absent discipline and for the
	# same reason: it is a half-applied strategic transaction, so a peer adopting a
	# snapshot taken between `begin_claim()` and `apply_claim()` must see it, and a
	# campaign that has never claimed anything must hash exactly as it did before
	# this field existed.
	if not pending_claim.is_empty():
		state["pending_claim"] = pending_claim
	return state


# --- internals ---------------------------------------------------------------

## The seat-controller normalisation, shared so the strategic layer and anything
## deriving a tactical roster from it can never disagree about what a value
## means. Deliberately the same rule `_menu_sim_team_roster()` applies to the
## lobby's `controller` field: trimmed, lowercased, `human` or else AI.
static func normalized_controller(value: Variant) -> String:
	return CONTROLLER_HUMAN if String(value).strip_edges().to_lower() == CONTROLLER_HUMAN else CONTROLLER_AI


## A seat's handicap, clamped onto retail's own ladder. Retail authors every
## multiple of five from 0 to 100 and states no interpolation, so a value off
## the ladder is snapped DOWN to the rung below and never averaged between two -
## an interpolated rung would be a multiplier retail never wrote.
static func normalized_handicap(value: Variant) -> int:
	var level := int(value)
	if level <= 0:
		return 0
	if level >= HANDICAP_MAX:
		return HANDICAP_MAX
	return (level / HANDICAP_STEP) * HANDICAP_STEP


static func is_authored_handicap(level: int) -> bool:
	return level >= 0 and level <= HANDICAP_MAX and level % HANDICAP_STEP == 0


## The chosen victory-type index, floored at 0. NOT clamped to a scenario's
## list here - this normaliser has no scenario - so `apply_ownership_sets()`
## validates the upper bound against the authored list and refuses beyond it.
static func normalized_victory_type(value: Variant) -> int:
	return maxi(0, int(value))


static func normalized_battle_type(value: Variant) -> String:
	var text := String(value).strip_edges().to_lower()
	return text if BATTLE_TYPES.has(text) else BATTLE_TYPE_AUTO_RESOLVE_AND_RTS


static func normalized_battle_priority(value: Variant) -> String:
	var text := String(value).strip_edges().to_lower()
	return text if BATTLE_PRIORITIES.has(text) else BATTLE_TYPE_AUTO_RESOLVE


## RESOLVE ONE BATTLE'S TYPE from the campaign rule, the priority, and what the
## attacker asked for. Returns `auto_resolve` or `rts` and never anything else.
##
## Retail's three campaign rules mean three different things and this is a
## transcription of them, not an interpretation:
##
##   "RTS"                 every battle is fought. The request is ignored.
##   "Auto Resolve"        every battle is auto-resolved. Likewise.
##   "Auto Resolve and RTS"  BOTH ARE OFFERED, so the player chooses per battle -
##                         which is the only reading under which the row means
##                         anything different from the other two.
##
## When both are offered and the attacker asks for neither in particular, the
## `battle_type_priority` row decides, which is exactly what retail's own
## tooltip says it is for: it settles "whether a battle will be decided through
## real-time or auto-resolve if players choose differently".
##
## THE DEFENDER IS NOT ASKED, and that is stated rather than hidden: no seat
## carries a per-battle preference today, so there is nobody to disagree with
## the attacker. `battle_type_priority` is recorded in the commitment now, and
## validated now, so the day a defender can express a preference the commitment
## does not need a fourth version.
static func resolve_battle_type(
	campaign_type: String, priority: String, requested: String
) -> String:
	if campaign_type == BATTLE_TYPE_AUTO_RESOLVE:
		return BATTLE_TYPE_AUTO_RESOLVE
	if campaign_type == BATTLE_TYPE_RTS:
		return BATTLE_TYPE_RTS
	if BATTLE_RESOLVED_TYPES.has(requested):
		return requested
	return priority if BATTLE_RESOLVED_TYPES.has(priority) else BATTLE_TYPE_RTS


## Why `commitment` may not be admitted, or "" when it may. Sorted iteration:
## the refusal a caller sees must not depend on dictionary insertion order.
func _battle_commitment_refusal(commitment: Dictionary) -> String:
	if String(commitment.get("schema", "")) != BATTLE_COMMITMENT_SCHEMA:
		return "battle commitment schema is not %s" % BATTLE_COMMITMENT_SCHEMA
	if int(commitment.get("schema_version", -1)) != BATTLE_COMMITMENT_SCHEMA_VERSION:
		return "unsupported battle commitment schema_version %d" % int(commitment.get("schema_version", -1))
	var expected := _sorted_strings(BATTLE_COMMITMENT_FIELDS.keys())
	for field in expected:
		if not commitment.has(field):
			return "battle commitment is missing field '%s'" % field
		if typeof(commitment[field]) != int(BATTLE_COMMITMENT_FIELDS[field]):
			return "battle commitment field '%s' has the wrong type" % field
	for field in _sorted_strings(commitment.keys()):
		if not BATTLE_COMMITMENT_FIELDS.has(field):
			return "battle commitment carries unknown field '%s'" % field

	var attacker := int(commitment["attacker"])
	var defender := int(commitment["defender"])
	if attacker < 0 or attacker >= players.size():
		return "battle commitment names an unseated attacker %d" % attacker
	if defender < 0 or defender >= players.size():
		return "battle commitment names an unseated defender %d" % defender
	if attacker == defender:
		return "battle commitment seats player %d on both sides" % attacker
	if int(commitment["attacker_team"]) == int(commitment["defender_team"]):
		return "battle commitment puts both sides on tactical team %d" % int(commitment["attacker_team"])
	if (commitment["committed_armies"] as PackedInt32Array).is_empty():
		return "battle commitment commits no attacking armies"
	# A battle with no battlefield cannot be fought, and a commitment that
	# carried an empty one would admit exactly the hole the field exists to
	# close: the ground would then have to come from somewhere outside the hash.
	if String(commitment["battlefield_map"]).strip_edges().is_empty():
		return "battle commitment names no battlefield map"
	var digest := String(commitment["brief_digest"])
	if digest.length() != 64 or not digest.is_valid_hex_number():
		return "battle commitment carries no well-formed brief digest"

	# THE VERSION 3 FIELDS. Checked here rather than trusted, for the same reason
	# every other field is: they decide the outcome, and they SEED THE DICE. A
	# handicap off retail's ladder would mean a multiplier retail never wrote; a
	# battle type this build does not implement would mean a battle nothing can
	# resolve, admitted into the hash and then stuck.
	if not BATTLE_RESOLVED_TYPES.has(String(commitment["battle_type"])):
		return ("battle commitment names battle_type '%s'; a single battle is either %s "
			+ "or %s, and '%s' is a campaign rule offering both") % [
			String(commitment["battle_type"]), BATTLE_TYPE_AUTO_RESOLVE, BATTLE_TYPE_RTS,
			BATTLE_TYPE_AUTO_RESOLVE_AND_RTS]
	if not BATTLE_PRIORITIES.has(String(commitment["battle_type_priority"])):
		return "battle commitment names battle_type_priority '%s', which is not one of %s" % [
			String(commitment["battle_type_priority"]), str(BATTLE_PRIORITIES)]
	for role in ["attacker", "defender"]:
		var level := int(commitment["%s_handicap" % role])
		if not is_authored_handicap(level):
			return ("battle commitment carries %s_handicap %d, which is not a rung retail "
				+ "authored (0 to %d in steps of %d)") % [role, level, HANDICAP_MAX, HANDICAP_STEP]
	return ""


func _reject(reason: String) -> bool:
	last_command_result = false
	events.append({"kind": "rejected", "reason": reason, "turn": turn_index})
	return false


## The reason the most recent refusal recorded. Refusals are events rather than a
## field (an event log is the one record a caller cannot forget to clear), so a
## caller that needs to QUOTE the reason - `apply_claim()` folding a failed march
## into its own refusal list - reads it back out here rather than restating it.
func _last_rejection() -> String:
	for index in range(events.size() - 1, -1, -1):
		var event := events[index] as Dictionary
		if String(event.get("kind", "")) == "rejected":
			return String(event.get("reason", ""))
	return "refused without recording a reason"


## `CLAIM_RECORD_FIELDS`, checked exhaustively in both directions. "" when the
## record is well formed; the reason otherwise.
func _claim_record_refusal(record: Dictionary) -> String:
	if String(record.get("schema", "")) != CLAIM_RECORD_SCHEMA:
		return "claim record schema is not %s" % CLAIM_RECORD_SCHEMA
	if int(record.get("schema_version", -1)) != CLAIM_RECORD_VERSION:
		return "unsupported claim record schema_version %d" % int(record.get("schema_version", -1))
	for field in _sorted_strings(CLAIM_RECORD_FIELDS.keys()):
		if not record.has(field):
			return "claim record is missing field '%s'" % field
		if typeof(record[field]) != int(CLAIM_RECORD_FIELDS[field]):
			return "claim record field '%s' has the wrong type" % field
	for field in _sorted_strings(record.keys()):
		if not CLAIM_RECORD_FIELDS.has(field):
			return "claim record carries unknown field '%s'" % field
	var player := int(record["player"])
	if player < 0 or player >= players.size():
		return "claim record names an unseated player %d" % player
	if not armies.has(int(record["army"])):
		return "claim record names an unknown army %d" % int(record["army"])
	if String((armies[int(record["army"])] as Dictionary).get("kind", "")) != ARMY_HERO:
		# Retail's own words: a garrison army "cannot takeover neutral territories
		# ... on their own" (`WOTRSCRIPT:WOTR_Tutorial066subtitle`).
		return "army %d is a garrison army, and a garrison army cannot take neutral ground" % int(record["army"])
	return ""


func _roster_command_points(roster: Dictionary) -> int:
	var total := 0
	for entry in roster.get("entries", []) as Array:
		total += int((entry as Dictionary).get("quantity", 0))
	return total


func _sorted_strings(values: Array) -> PackedStringArray:
	var names: Array[String] = []
	for value in values:
		names.append(String(value))
	names.sort()
	return PackedStringArray(names)


## Same canonical form the retail slice uses: dictionaries collapse to sorted
## key/value rows so insertion order can never leak into the hash.
##
## STATIC and shared on purpose. `wotr_battle.gd` digests the battle brief with
## this exact function rather than a second copy: a canonicalizer that exists
## twice is a canonicalizer that can drift, and the whole point of the digest is
## that two peers derive the same bytes from the same input.
static func canonicalize(value: Variant) -> Variant:
	if typeof(value) == TYPE_DICTIONARY:
		var source := value as Dictionary
		var keys := source.keys()
		keys.sort_custom(_canonical_key_less)
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


static func _canonical_key_less(a: Variant, b: Variant) -> bool:
	return "%02d:%s" % [typeof(a), var_to_str(a)] < "%02d:%s" % [typeof(b), var_to_str(b)]


## SHA-256 over the canonical form. `state_hash()` is exactly this over the
## authoritative dictionary; the battle bridge is exactly this over a brief.
static func canonical_digest(value: Variant) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(var_to_bytes(canonicalize(value)))
	return context.finish().hex_encode()
