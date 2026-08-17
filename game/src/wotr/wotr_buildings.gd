extends RefCounted

## THE WAR OF THE RING BUILDING CATALOGUE: what a seat may raise on a build
## plot, what it costs, and what standing it grants.
##
## ============================================================================
## WHY THIS FILE EXISTS, AND WHAT IT CORRECTS
## ============================================================================
##
## `wotr_strategic_gaps.gd` carried an entry called `strategic_building_
## construction` which said: "building prices are recorded (WOTR_*_COST) but the
## LW_* building macros the scenarios place are unexpanded importer gaps, so what
## a built structure IS or GRANTS is unrecorded."
##
## THE SECOND HALF OF THAT SENTENCE WAS FALSE, and it was false in the same way
## `strategic_ai_turns` was: it reasoned from ONE artefact (the importer's JSON
## living-world document, which transcribes `livingworldregions.inc` and the
## scenario files) as though that artefact were the whole of what retail ships.
## It is not. Retail's strategic buildings are authored in
## `data/ini/livingworldbuildings.ini`, twenty-eight `LivingWorldBuilding`
## blocks, and this project ALREADY CONVERTS THEM - they are the `buildings`
## array in `living-world-ui.json`, the bundle `wotr_living_world_ui.gd` has been
## loading for the strategic screen all along. Every field this layer needs is in
## there and has been for some time:
##
##     LWB_IsengardUrukPit
##         Type                     = Barracks
##         AvailableTo              = PlayerIsengard
##         StrategicResourceCost    = WOTR_BARRACKS_COST      (500, gamedata.ini)
##         TurnsToBuild             = 1
##         BattleThingTemplate      = IsengardUrukPit
##         BuildingIcon             = LWBIcon_IsengardUrukPit
##         CanDefendTerritory       = (blank; `Yes` on every Fortress)
##         CreateUnitDuringAutoResolve = (blank; `Yes` on every Fortress)
##         ArmyToSpawn ... x7       (the armies it recruits)
##
## AND THE `LW_*` MACROS ARE NOT A HOLE EITHER. They are `#define`s at the top of
## `data/ini/campaigns/riskcampaign.ini`, above retail's own comment explaining
## the rule they implement:
##
##     // Generic building defines for all factions.
##     // Allows scenarios to say that a fort should be spawned in a region, and
##     // the appropriate one for the controlling faction will be created.
##
## So `LW_FORT` is not one building; it is a POSITIONAL LIST, one id per faction
## in retail's fixed faction order (Men, Elves, Dwarves, Mordor, Isengard, Wild,
## Angmar; RotWK appended the seventh and left an eighth, `;LWB_ArnorFortress`,
## commented out). This file resolves it BY TYPE AND BY `AvailableTo` rather than
## by transcribing that list, for three reasons: by-type is what retail's own
## comment DESCRIBES; it reproduces retail's list exactly, entry for entry, for
## all seven factions; and a positional list transcribed into GDScript would be a
## retail payload living outside `workspace`, which `AGENTS.md` forbids.
##
## ============================================================================
## WHAT TYPED CONVERSION CARRIES, AND WHAT RUNTIME APPLIES
## ============================================================================
##
## Schema 2 carries all five BuildingNugget kinds without flattening them.
## IncreaseTreasury is resolved through the converted TreasureAmount macro and
## applied as per-turn income. IncreaseCommandPoints is applied for its exact
## `Type = WORLD` scope: all seven Resource carriers author +30, and retail's
## tooltip names it the "CP Bonus" gained when constructed. StrengthenArmy is
## applied to auto-resolve for its exact THIS_TERRIORITY scope; UpgradeTroops and
## SpawnArmy QueueSize remain typed runtime data unapplied.
## Every projected record deep-copies `nuggets` and `nuggets_status`.
##
## ============================================================================
## THE FOUR TYPES, AND THE TWO PLACES RETAIL SPELLS THEM DIFFERENTLY
## ============================================================================
##
## `Type =` takes exactly four values across the 28 blocks: `Fortress`,
## `Barracks`, `Armory`, `Resource`. Two other retail vocabularies name the same
## four things with different words, and both bindings are retail's own, not a
## resemblance:
##
##   `Resource` IS retail's "Farm". Every `Type = Resource` block costs
##   `WOTR_FARM_COST`, and every one of their tooltips reads, verbatim,
##   "Structure Type: Farm\nIncreases treasury" (`CONTROLBAR:LW_ToolTip_
##   GondorFarm`, `..._MordorSlaughterhouse`, `..._IsengardFurnace`,
##   `..._DwarvenMineShaft`, `..._ElvenMallornTree`, `..._AngmarFarm`,
##   `..._WildDefiledMineShaft`). Retail's own cost macro and retail's own
##   player-facing sentence both call it a farm.
##
##   `Fortress` IS the AI template's "Castle". `livingworldaitemplate.ini`
##   weights `BuildingScoreCastle`, and retail names the standing stronghold in
##   a region a castle in the string table it draws from
##   (`LW:DisplayNameAmonSulCastle` over `Fortress` blocks). There is no fifth
##   type for a `BuildingScoreCastle` to mean.
##
## ============================================================================
## WHAT THIS FILE WILL NOT DO
## ============================================================================
##
## It does not carry a single retail number in its source. Costs are macro NAMES
## read out of the converted bundle and resolved through `wotr_macros.gd` against
## retail's own `gamedata.ini` `#define` table; a cost whose macro is missing or
## non-numeric makes that building UNBUILDABLE and says which macro, rather than
## defaulting to zero and quietly giving the player a free fortress.

const LivingWorldUiScript = preload("res://src/wotr/wotr_living_world_ui.gd")
const MacrosScript = preload("res://src/wotr/wotr_macros.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")

## Retail's four `Type =` values, verbatim. Sorted, so every consumer that walks
## the types walks them in one order.
const TYPE_ARMORY := "Armory"
const TYPE_BARRACKS := "Barracks"
const TYPE_FORTRESS := "Fortress"
const TYPE_RESOURCE := "Resource"
const TYPES := [TYPE_ARMORY, TYPE_BARRACKS, TYPE_FORTRESS, TYPE_RESOURCE]

## RETAIL'S `LW_*` SCENARIO TOKENS -> the `Type` they stand for. Four rows, and
## each one is a reading of retail's own naming rather than a transcription of
## retail's expansion list (see the header for why by-type resolution is the
## correct reading of `riskcampaign.ini`'s comment):
##
##   LW_FORT     -> Fortress   the comment itself says "a fort should be spawned"
##   LW_BARRACKS -> Barracks   the token IS the type name
##   LW_ARMORY   -> Armory     the token IS the type name
##   LW_FARM     -> Resource   retail's own cost macro for every `Type =
##                             Resource` block is `WOTR_FARM_COST`, and every
##                             one of their tooltips says "Structure Type: Farm"
const SCENARIO_TOKEN_TYPES := {
	"LW_ARMORY": TYPE_ARMORY,
	"LW_BARRACKS": TYPE_BARRACKS,
	"LW_FARM": TYPE_RESOURCE,
	"LW_FORT": TYPE_FORTRESS,
}

## RETAIL'S AI WEIGHT NAMES -> the `Type` each one values, so `wotr_ai.gd` can
## spend `livingworldaitemplate.ini`'s `BuildingScore*` numbers on the thing they
## were written about. `Castle`->`Fortress` and `Farm`->`Resource` are the two
## bindings the header argues; the other two are the same word twice.
const AI_SCORE_KEY_TYPES := {
	"BuildingScoreArmory": TYPE_ARMORY,
	"BuildingScoreBarracks": TYPE_BARRACKS,
	"BuildingScoreCastle": TYPE_FORTRESS,
	"BuildingScoreFarm": TYPE_RESOURCE,
}

## THE PER-TURN TREASURY MACROS, by the `Type` each one pays for.
##
## RETAIL AUTHORS THE BINDING, THE AMOUNTS AND THE CADENCE, and the fact that the
## register once called this an unknown rule is a lesson rather than an
## embarrassment: two of the three are in files the importer never read, and the
## third is in the STRING TABLE - which is exactly where the neutral-ground claim
## rule turned out to be, after two passes concluded from the ini tree alone that
## no such rule existed.
##
## THE BINDING is a nugget on the building itself, in
## `data/ini/livingworldbuildings.ini`. It is on all fourteen income buildings
## and on none of the other fourteen:
##
##   LWB_MenFortress (line 53)   BuildingNugget IncreaseTreasury NuggetTag_GiveLoot
##                                   TreasureAmount = GAIN_PER_FORTRESS
##   LWB_GondorFarm  (line 322)  BuildingNugget IncreaseTreasury NuggetTag_GiveLoot
##                                   TreasureAmount = GAIN_PER_FARM
##
## Schema 2 carries that binding directly. This file groups the validated typed
## IncreaseTreasury nuggets by Type only to expose the existing `income_for_type`
## API; it refuses partial, inconsistent, or unresolved bindings and never
## supplies a macro from the Type name.
##
## THE AMOUNTS are `gamedata.ini`, in one contiguous `;//---WOTR---` block
## immediately above the four `WOTR_*_COST` prices this same file spends:
##
##   line 8453  ;//-------------------------WOTR------------------------------
##   line 8454  #define GAIN_PER_FORTRESS       300
##   line 8455  #define GAIN_PER_FARM           300
##   line 8456  #define FERTILE_TERRITORY_BONUS 500
##   line 8457  #define WOTR_FARM_COST            0
##   line 8458  #define WOTR_BARRACKS_COST      500
##   line 8459  #define WOTR_FORTRESS_COST     1500
##   line 8460  #define WOTR_FORGE_COST         500
##
## THE CADENCE, AND THE SUM ITSELF, are `data/lotr.str`:
##
##   `STRATEGICHUD:StatsCTreasuryIncomeHelp` (line 30446) - "Treasury increases
##   by this amount at the START OF EACH TURN."
##   `APT:NewWOTRFeature4Desc` (line 53418) - "ACCUMULATE TREASURY FROM FARMS AND
##   FORTRESSES to recruit units and construct buildings on the strategic map."
##   `CONTROLBAR:LW_FarmTreasuryBonus` (28191) - "Treasury Bonus: +%d PER TURN",
##   above retail's own authoring comment "the first argument is the amount of
##   treasure added per turn".
##   `STRATEGICHUD:StatsTreasuryIncome` (50163) - authoring comment: "format
##   string to show treasury income total (SUM OF ALL REGION Treasury Income
##   Bonuses)". The region term is a SUM, in retail's own words.
##
## What remains this project's, and is stated in `wotr_strategic_gaps.gd`'s
## `PROJECT_AUTHORED_RULES`, is only the last mile: that the building term and
## the region term ADD rather than combining some other way, that there is no
## base income, and that a region's permanent authored stronghold is not a
## fortress for `GAIN_PER_FORTRESS`. See `turn_income()` in `wotr_state.gd`.

## The region-bonus macro whose value is a per-turn treasury bonus for holding
## the region. Retail authors it on eleven regions as
## `FertileTerritoryBonus = FERTILE_TERRITORY_BONUS` and the importer carries the
## macro symbolically; `wotr_macros.gd`'s own header already records why this is
## a TREASURY quantity ("which is why retail's own panel reads '+500 Treasure'
## over Mordor" - `LW:RegionTreasuryBonus`, "+%d Treasure").
const FERTILE_MACRO := "FERTILE_TERRITORY_BONUS"
const FERTILE_FIELD := "fertileTerritory"

## Every field this layer reads off a converted `LivingWorldBuilding`, and the
## bundle key it lives under. Named here rather than inline so a bundle that
## stops carrying one of them is a NAMED refusal instead of a silently empty
## catalogue.
const REQUIRED_BUILDING_FIELDS := ["id", "type", "availableTo", "strategicResourceCost"]

var loaded := false
var reason := ""
var ui_path := ""
var macros_path := ""

## `building id -> record`. See `_project()` for the record's fields.
var buildings: Dictionary = {}
## Sorted building ids, so every sweep is reproducible.
var building_ids: PackedStringArray = PackedStringArray()

## Resolved gamedata numbers, macro name -> int. Populated only for macros this
## file actually spends, so a reader can see exactly which retail constants the
## construction and treasury rules stand on.
var resolved_macros: Dictionary = {}
## Macros the catalogue needed and could not resolve, name -> why. A building
## whose cost macro is in here is UNBUILDABLE and says so.
var unresolved_macros: Dictionary = {}
## Derived only from validated IncreaseTreasury nuggets, never guessed by Type.
var income_macro_by_type: Dictionary = {}
var income_by_type: Dictionary = {}


## Where the two bundles this catalogue needs may live. Deliberately wider than
## either loader's own list, because BOTH of them sit inside the region-geometry
## bundle in every shipped and staged layout (`living-world-ui.json` and
## `macros.json` are written beside `regions.bin`), and `run_game.bat` points
## `OPENBFME_LIVING_MAP_REGIONS` at that directory and nothing else.
##
## THE CALLER'S ROOTS ARE SEARCHED FIRST, and the environment after - the same
## precedence `wotr_session.locate_document()` keeps, and for the same reason: a
## bundle shipped inside the pack the game actually mounted is the product path,
## and an environment variable is the workspace fallback that makes the lane
## usable before a pack ships one.
static func bundle_roots(roots: Array = []) -> Array[String]:
	var found: Array[String] = []
	for value in roots:
		var root := String(value).strip_edges()
		if root.is_empty() or found.has(root):
			continue
		found.append(root)
		# A mounted content pack keeps the bundle at its own relative path.
		var packed := root.path_join(RegionGeometryScript.PACK_BUNDLE_RELATIVE)
		if not found.has(packed):
			found.append(packed)
	var geometry_root := OS.get_environment(RegionGeometryScript.BUNDLE_ENV).strip_edges()
	if not geometry_root.is_empty() and not found.has(geometry_root):
		found.append(geometry_root)
	if not found.has(RegionGeometryScript.USER_BUNDLE):
		found.append(RegionGeometryScript.USER_BUNDLE)
	return found


## Load the catalogue. `{ok, reason, ui_path, macros_path, buildings}`.
##
## FAILS CLOSED AND SAYS WHAT IS LOST. Two bundles are needed and they fail
## independently: without `living-world-ui.json` there is no building vocabulary
## at all; without `macros.json` the buildings are known but their prices are
## macro NAMES, and a price nobody can resolve is not a price. Neither is
## defaulted, and `wotr_session.commit_build()` refuses by name in both cases.
func load_from_roots(roots: Array = []) -> Dictionary:
	_reset()
	var search := bundle_roots(roots)

	var ui := LivingWorldUiScript.new()
	var ui_found: Dictionary = ui.locate_and_load(search)
	if not bool(ui_found.get("ok", false)):
		reason = ("NO BUILDING CATALOGUE, so nothing can be built anywhere: %s"
			% String(ui_found.get("reason", "")))
		return _result()
	ui_path = String(ui_found.get("path", ""))

	var macros := MacrosScript.new()
	var macros_found: Dictionary = macros.locate_and_load(search)
	if not bool(macros_found.get("ok", false)):
		reason = ("NO gamedata #define TABLE, so every strategic building's price is "
			+ "the macro NAME retail wrote (WOTR_BARRACKS_COST and its three "
			+ "siblings) and not a number this layer can charge a treasury: %s"
			% String(macros_found.get("reason", "")))
		return _result()
	macros_path = String(macros_found.get("path", ""))

	# The region bonus is independent. Building income is resolved below only
	# from typed IncreaseTreasury nuggets; Type names are never a substitute.
	_resolve_macro(macros, FERTILE_MACRO)

	var ids: Array[String] = []
	for id in ui.building_ids:
		var source := ui.buildings.get(String(id), {}) as Dictionary
		var record := _project(source, macros)
		if record.is_empty():
			continue
		buildings[String(id)] = record
		ids.append(String(id))
	ids.sort()
	building_ids = PackedStringArray(ids)
	_derive_income(macros)
	if buildings.is_empty():
		reason = ("the living-world UI bundle at %s carries no building block with "
			+ "all of %s") % [ui_path, ", ".join(REQUIRED_BUILDING_FIELDS)]
		return _result()
	loaded = true
	reason = ""
	return _result()


## One catalogue record from one converted `LivingWorldBuilding`, or `{}` when
## the block is missing a field this layer cannot proceed without.
func _project(source: Dictionary, macros: MacrosScript) -> Dictionary:
	for field in REQUIRED_BUILDING_FIELDS:
		if String(source.get(String(field), "")).strip_edges().is_empty():
			return {}
	var type_name := String(source.get("type", "")).strip_edges()
	if not TYPES.has(type_name):
		# A fifth type would mean retail authored a structure class this layer has
		# no rule for. Refused by omission and counted, never bucketed into one of
		# the four that exist.
		unresolved_macros["Type=%s" % type_name] = (
			"building %s declares a Type retail's four (%s) do not cover"
				% [String(source.get("id", "")), ", ".join(TYPES)])
		return {}
	var cost_macro := String(source.get("strategicResourceCost", "")).strip_edges()
	var cost := _resolve_macro(macros, cost_macro)
	var recruits: Array[Dictionary] = []
	for row in source.get("recruits", []) as Array:
		var recruit := row as Dictionary
		recruits.append({
			"player_army": String(recruit.get("playerArmy", "")),
			"hero_template": String(recruit.get("heroTemplateName", "")),
			"title": String(recruit.get("constructButtonTitle", "")),
			"help": String(recruit.get("constructButtonHelp", "")),
			"image": String(recruit.get("constructButtonImage", "")),
			"build_time": int(String(recruit.get("buildTime", "0")).to_int()),
		})
	return {
		"id": String(source.get("id", "")),
		"type": type_name,
		# Retail's `AvailableTo` is a `LivingWorldPlayerTemplate` name
		# (`PlayerIsengard`), which is exactly what a seat carries in `players`.
		"available_to": String(source.get("availableTo", "")),
		"cost_macro": cost_macro,
		"cost": int(cost.get("value", 0)) if bool(cost.get("ok", false)) else -1,
		"cost_reason": String(cost.get("reason", "")),
		# Every shipped block says 1. Read rather than assumed, because reading it
		# costs nothing and a data change would otherwise be invisible.
		"turns_to_build": maxi(1, int(String(source.get("turnsToBuild", "1")).to_int())),
		"battle_thing": String(source.get("battleThingTemplate", "")),
		"icon": String(source.get("buildingIcon", "")),
		"button_image": String(source.get("constructButtonImage", "")),
		# `Yes` on every Fortress and blank on everything else. Carried as retail
		# wrote it and tested as non-empty, so a future third value is not silently
		# read as false.
		"can_defend_territory": not String(source.get("canDefendTerritory", "")).strip_edges().is_empty(),
		"creates_unit_during_auto_resolve":
			not String(source.get("createUnitDuringAutoResolve", "")).strip_edges().is_empty(),
		"display_name_tag": String(source.get("displayNameTag", "")),
		"description_tag": String(source.get("displayDescriptionTag", "")),
		"build_title_tag": String(source.get("constructButtonTitle", "")),
		"build_help_tag": String(source.get("constructButtonHelp", "")),
		"recruits": recruits,
		# Deep copies prevent runtime consumers from mutating the loader's bundle
		# state through a shared Array/Dictionary reference.
		"nuggets_status": String(source.get("nuggetsStatus", "refused")),
		"nuggets": (source.get("nuggets", []) as Array).duplicate(true),
	}


func _derive_income(macros: MacrosScript) -> void:
	for type_name in TYPES:
		var rows: Array[Dictionary] = []
		for id in building_ids:
			var record := buildings[String(id)] as Dictionary
			if String(record.get("type", "")) == type_name:
				rows.append(record)
		var found: Dictionary = {}
		var with_income := 0
		var refused := false
		for record in rows:
			var local: Array[String] = []
			if String(record.get("nuggets_status", "")) == "ok":
				for nugget_value in record.get("nuggets", []) as Array:
					var nugget := nugget_value as Dictionary
					if String(nugget.get("kind", "")) == "increase_treasury":
						local.append(String(nugget.get("treasureAmount", "")))
			else:
				refused = true
			if local.size() == 1:
				with_income += 1
				found[local[0]] = true
			elif local.size() > 1:
				found["<multiple on one building>"] = true
		# Uniform absence is authored zero income. Partial presence, differing
		# macros, or a refused nugget conversion cannot safely define a Type.
		if with_income == 0 and found.is_empty() and not refused:
			continue
		if refused or with_income != rows.size() or found.size() != 1:
			unresolved_macros["IncomeType=%s" % type_name] = "typed IncreaseTreasury nuggets are missing or inconsistent for Type=%s" % type_name
			continue
		var macro_name := String(found.keys()[0])
		var resolved := _resolve_macro(macros, macro_name)
		if not bool(resolved.get("ok", false)):
			unresolved_macros["IncomeType=%s" % type_name] = String(resolved.get("reason", "unresolved treasury macro"))
			continue
		income_macro_by_type[type_name] = macro_name
		income_by_type[type_name] = int(resolved["value"])


## Resolve one gamedata macro to an int and remember the outcome. Retail's
## `#define`s in this range are whole numbers; a body that is an expression or a
## decimal is recorded UNRESOLVED rather than truncated, because a truncated
## price is a price retail did not write.
func _resolve_macro(macros: MacrosScript, name: String) -> Dictionary:
	if name.is_empty():
		return {"ok": false, "value": 0, "reason": "no macro named"}
	if resolved_macros.has(name):
		return {"ok": true, "value": int(resolved_macros[name]), "reason": ""}
	if unresolved_macros.has(name):
		return {"ok": false, "value": 0, "reason": String(unresolved_macros[name])}
	var found: Dictionary = macros.resolve(name)
	if not bool(found.get("ok", false)):
		var note := ("gamedata.ini's #define table has no numeric body for %s%s"
			% [name, " (retail's body is '%s')" % String(found.get("raw", ""))
				if not String(found.get("raw", "")).is_empty() else ""])
		unresolved_macros[name] = note
		return {"ok": false, "value": 0, "reason": note}
	var value := float(found.get("value", 0.0))
	if value != floor(value):
		var note := "%s resolves to %s, which is not a whole number of treasure" % [
			name, String(found.get("raw", ""))]
		unresolved_macros[name] = note
		return {"ok": false, "value": 0, "reason": note}
	resolved_macros[name] = int(value)
	return {"ok": true, "value": int(value), "reason": ""}


## The catalogue record for one building id, or `{}`.
func building(id: String) -> Dictionary:
	return (buildings.get(id, {}) as Dictionary)


## Every building `player_template` may raise, sorted by id. The filter is
## retail's own `AvailableTo` field and nothing else - no faction-name
## resemblance, no fallback to "the Men one".
func buildings_for(player_template: String) -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	if player_template.is_empty():
		return rows
	for id in building_ids:
		var record := buildings[String(id)] as Dictionary
		if String(record.get("available_to", "")) == player_template:
			rows.append(record)
	return rows


## The one building of `type` that `player_template` may raise, or `{}` when the
## template has none. This is the resolution retail's `riskcampaign.ini` comment
## describes for the `LW_*` scenario tokens - "the appropriate one for the
## controlling faction will be created" - and it is also how a build menu names
## a type the player asked for.
##
## AMBIGUITY IS A REFUSAL, not a pick: retail ships exactly one block per
## (template, type) pair across all 28, and a bundle with two would make "the
## appropriate one" a coin toss that would enter the strategic hash.
func building_of_type(player_template: String, type_name: String) -> Dictionary:
	var matches: Array[Dictionary] = []
	for record in buildings_for(player_template):
		if String(record.get("type", "")) == type_name:
			matches.append(record)
	return matches[0] if matches.size() == 1 else {}


## Resolve a scenario's authored `SpawnBuildings` token for the seat that holds
## the region. `{ok, id, type, reason}`.
##
## The token is retail's `LW_*` `#define` and the rule is retail's own comment on
## it. A token this table does not know, or a faction with no block of that type,
## is REPORTED, never substituted - retail's own lists carry a commented-out
## eighth entry (`;LWB_ArnorFortress`) for a faction whose blocks were cut, and a
## resolver that quietly stood a Gondor fortress for it would be inventing an
## army's worth of territory defence.
func resolve_scenario_token(token: String, player_template: String) -> Dictionary:
	var type_name := String(SCENARIO_TOKEN_TYPES.get(token, ""))
	if type_name.is_empty():
		return {"ok": false, "id": "", "type": "", "reason":
			"'%s' is not one of retail's four LW_* scenario building tokens (%s)"
				% [token, ", ".join(_sorted(SCENARIO_TOKEN_TYPES.keys()))]}
	if not loaded:
		return {"ok": false, "id": "", "type": type_name, "reason": reason}
	var record := building_of_type(player_template, type_name)
	if record.is_empty():
		return {"ok": false, "id": "", "type": type_name, "reason":
			"retail authors no %s block AvailableTo %s, so its scenario token %s "
			% [type_name, player_template, token]
			+ "names a structure that faction has none of"}
	return {"ok": true, "id": String(record.get("id", "")), "type": type_name, "reason": ""}


## Retail's per-turn treasury gain for one standing structure of `type_name`, or
## 0 for a type retail pays nothing for (Barracks and Armory: no `GAIN_PER_*`
## macro names them, so they earn nothing and that is a read of retail's table,
## not an omission here).
func income_for_type(type_name: String) -> int:
	return int(income_by_type.get(type_name, 0))


## The standing WORLD command-point contribution authored on one concrete
## building. This is a reporting seam, not state: calls neither cache nor mutate.
## Nuggets are walked in source order, duplicates remain separate rows, and their
## amounts are summed only after each typed row has passed the same closed checks.
func world_command_points_for_building(id: String) -> Dictionary:
	var rows: Array[Dictionary] = []
	if not loaded:
		return {"ok": false, "building": id, "bonus": 0, "rows": rows,
			"refusal": reason if not reason.is_empty() else "building catalogue is not loaded"}
	if not buildings.has(id):
		return {"ok": false, "building": id, "bonus": 0, "rows": rows,
			"refusal": "standing structure %s is absent from the building catalogue" % id}
	var record := buildings[id] as Dictionary
	if String(record.get("nuggets_status", "")) != "ok":
		return {"ok": false, "building": id, "bonus": 0, "rows": rows,
			"refusal": "standing structure %s has refused typed BuildingNuggets (status=%s)" % [
				id, String(record.get("nuggets_status", "missing"))]}
	var bonus := 0
	var nuggets_value: Variant = record.get("nuggets", null)
	if typeof(nuggets_value) != TYPE_ARRAY:
		return {"ok": false, "building": id, "bonus": 0, "rows": rows,
			"refusal": "standing structure %s carries malformed typed BuildingNuggets" % id}
	var nugget_index := 0
	for nugget_value in nuggets_value as Array:
		if typeof(nugget_value) != TYPE_DICTIONARY:
			return {"ok": false, "building": id, "bonus": 0, "rows": rows,
				"refusal": "standing structure %s nugget %d is not a dictionary" % [id, nugget_index]}
		var nugget := nugget_value as Dictionary
		if String(nugget.get("kind", "")) != "increase_command_points":
			nugget_index += 1
			continue
		if typeof(nugget.get("type", null)) != TYPE_STRING:
			return {"ok": false, "building": id, "bonus": 0, "rows": rows,
				"refusal": "standing structure %s IncreaseCommandPoints nugget %d has malformed type" % [id, nugget_index]}
		var scope := String(nugget["type"])
		if scope != "WORLD":
			return {"ok": false, "building": id, "bonus": 0, "rows": rows,
				"refusal": "standing structure %s IncreaseCommandPoints nugget %d has unsupported scope '%s'; only WORLD is implemented" % [id, nugget_index, scope]}
		var amount_value: Variant = nugget.get("amount", null)
		# Match the converted bundle's JSON integer contract exactly: JSON numbers
		# arrive as floats and are accepted only while finite, integral, and exactly
		# representable by IEEE-754 (2^53-1). Never broaden this to coercion.
		if (not (amount_value is float) or not is_finite(amount_value)
				or amount_value != floor(amount_value)
				or absf(amount_value) > 9007199254740991.0):
			return {"ok": false, "building": id, "bonus": 0, "rows": rows,
				"refusal": "standing structure %s IncreaseCommandPoints nugget %d has malformed amount" % [id, nugget_index]}
		var amount := int(amount_value)
		if amount < 0:
			return {"ok": false, "building": id, "bonus": 0, "rows": rows,
				"refusal": "standing structure %s IncreaseCommandPoints nugget %d has unsupported negative amount %d" % [id, nugget_index, amount]}
		if bonus > 9223372036854775807 - amount:
			return {"ok": false, "building": id, "bonus": 0, "rows": rows,
				"refusal": "standing structure %s IncreaseCommandPoints nuggets overflow a signed 64-bit total" % id}
		bonus += amount
		rows.append({"index": nugget_index, "scope": scope, "amount": amount})
		nugget_index += 1
	return {"ok": true, "building": id, "bonus": bonus, "rows": rows, "refusal": ""}


## Validated StrengthenArmy rows for one standing building. This is a pure
## reporting primitive: it preserves nugget/table source order and never caches a
## derived effect. Only the executable THIS_TERRIORITY armour form is admitted.
func strengthen_army_for_building(id: String) -> Dictionary:
	var rows: Array[Dictionary] = []
	if not loaded:
		return {"ok": false, "building": id, "rows": rows, "refusal":
			reason if not reason.is_empty() else "building catalogue is not loaded"}
	if not buildings.has(id):
		return {"ok": false, "building": id, "rows": rows, "refusal":
			"standing structure %s is absent from the building catalogue" % id}
	var record := buildings[id] as Dictionary
	if String(record.get("nuggets_status", "")) != "ok":
		return {"ok": false, "building": id, "rows": rows, "refusal":
			"standing structure %s has refused typed BuildingNuggets (status=%s)" % [
				id, String(record.get("nuggets_status", "missing"))]}
	var nuggets_value: Variant = record.get("nuggets", null)
	if typeof(nuggets_value) != TYPE_ARRAY:
		return {"ok": false, "building": id, "rows": rows, "refusal":
			"standing structure %s carries malformed typed BuildingNuggets" % id}
	var nugget_index := 0
	for nugget_value in nuggets_value as Array:
		if typeof(nugget_value) != TYPE_DICTIONARY:
			return {"ok": false, "building": id, "rows": rows, "refusal":
				"standing structure %s nugget %d is not a dictionary" % [id, nugget_index]}
		var nugget := nugget_value as Dictionary
		var kind_value: Variant = nugget.get("kind", null)
		if typeof(kind_value) != TYPE_STRING or not ["increase_treasury",
				"increase_command_points", "strengthen_army", "upgrade_troops",
				"spawn_army"].has(String(kind_value)):
			return {"ok": false, "building": id, "rows": rows, "refusal":
				"standing structure %s nugget %d has an unknown typed kind" % [id, nugget_index]}
		if String(kind_value) != "strengthen_army":
			nugget_index += 1
			continue
		if typeof(nugget.get("strengtheningRange", null)) != TYPE_STRING or String(nugget["strengtheningRange"]) != "THIS_TERRIORITY":
			return {"ok": false, "building": id, "rows": rows, "refusal":
				"standing structure %s StrengthenArmy nugget %d has unsupported strengtheningRange; only THIS_TERRIORITY is implemented" % [id, nugget_index]}
		if typeof(nugget.get("bonusKey", null)) != TYPE_STRING or String(nugget["bonusKey"]).strip_edges().is_empty():
			return {"ok": false, "building": id, "rows": rows, "refusal":
				"standing structure %s StrengthenArmy nugget %d has malformed bonusKey" % [id, nugget_index]}
		var bonuses_value: Variant = nugget.get("bonuses", null)
		if typeof(bonuses_value) != TYPE_ARRAY or (bonuses_value as Array).is_empty():
			return {"ok": false, "building": id, "rows": rows, "refusal":
				"standing structure %s StrengthenArmy nugget %d has no bonus rows" % [id, nugget_index]}
		var bonuses: Array[Dictionary] = []
		var previous := 0
		var bonus_index := 0
		for bonus_value in bonuses_value as Array:
			if typeof(bonus_value) != TYPE_DICTIONARY:
				return {"ok": false, "building": id, "rows": rows, "refusal": "standing structure %s StrengthenArmy nugget %d bonus %d is not a dictionary" % [id, nugget_index, bonus_index]}
			var bonus := bonus_value as Dictionary
			var threshold_value: Variant = bonus.get("threshold", null)
			if (not (threshold_value is float) or not is_finite(threshold_value)
					or threshold_value != floor(threshold_value)
					or threshold_value <= 0.0 or threshold_value > 9007199254740991.0):
				return {"ok": false, "building": id, "rows": rows, "refusal": "standing structure %s StrengthenArmy nugget %d bonus %d has malformed threshold" % [id, nugget_index, bonus_index]}
			var threshold := int(threshold_value)
			if threshold <= previous:
				return {"ok": false, "building": id, "rows": rows, "refusal": "standing structure %s StrengthenArmy nugget %d thresholds are not strictly increasing" % [id, nugget_index]}
			for refused_field in ["weaponPct", "experiencePct"]:
				if not bonus.has(refused_field) or bonus[refused_field] != null:
					return {"ok": false, "building": id, "rows": rows, "refusal": "standing structure %s StrengthenArmy nugget %d bonus %d carries unsupported %s" % [id, nugget_index, bonus_index, refused_field]}
			var armor_value: Variant = bonus.get("armorPct", null)
			if not (armor_value is float) or not is_finite(armor_value) or armor_value < 0.0:
				return {"ok": false, "building": id, "rows": rows, "refusal": "standing structure %s StrengthenArmy nugget %d bonus %d has malformed armorPct" % [id, nugget_index, bonus_index]}
			bonuses.append({"threshold": threshold, "armorPct": float(armor_value)})
			previous = threshold
			bonus_index += 1
		rows.append({"index": nugget_index, "bonusKey": String(nugget["bonusKey"]),
			"strengtheningRange": "THIS_TERRIORITY", "bonuses": bonuses})
		nugget_index += 1
	return {"ok": true, "building": id, "rows": rows, "refusal": ""}


## One line per fact worth printing at load, in the shape every other bundle
## loader in this layer uses.
func describe_load() -> PackedStringArray:
	var lines: Array[String] = []
	if not loaded:
		lines.append(reason)
		return PackedStringArray(lines)
	var by_type: Dictionary = {}
	for id in building_ids:
		var type_name := String((buildings[String(id)] as Dictionary).get("type", ""))
		by_type[type_name] = int(by_type.get(type_name, 0)) + 1
	var counts: Array[String] = []
	for type_name in TYPES:
		counts.append("%s x%d" % [type_name, int(by_type.get(type_name, 0))])
	lines.append("%d strategic buildings from %s (%s)" % [
		building_ids.size(), ui_path, ", ".join(counts)])
	var prices: Array[String] = []
	for macro_name in _sorted(resolved_macros.keys()):
		prices.append("%s=%d" % [macro_name, int(resolved_macros[macro_name])])
	lines.append("retail gamedata numbers in play, from %s: %s" % [
		macros_path, ", ".join(prices)])
	for macro_name in _sorted(unresolved_macros.keys()):
		lines.append("UNRESOLVED %s: %s" % [macro_name, String(unresolved_macros[macro_name])])
	return PackedStringArray(lines)


func _result() -> Dictionary:
	return {
		"ok": loaded,
		"reason": reason,
		"ui_path": ui_path,
		"macros_path": macros_path,
		"buildings": building_ids.size(),
	}


func _reset() -> void:
	loaded = false
	reason = ""
	ui_path = ""
	macros_path = ""
	buildings = {}
	building_ids = PackedStringArray()
	resolved_macros = {}
	unresolved_macros = {}
	income_macro_by_type = {}
	income_by_type = {}


static func _sorted(values: Array) -> PackedStringArray:
	var names: Array[String] = []
	for value in values:
		names.append(String(value))
	names.sort()
	return PackedStringArray(names)
