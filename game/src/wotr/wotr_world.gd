extends RefCounted

## War of the Ring strategic WORLD DATA: the immutable half of the layer.
##
## This is the territory graph plus the authored constants a strategic session
## needs - regions, their connections, per-region and per-territory bonuses, the
## army rosters, the command-point economy, and the RTS handoff settings. It is
## produced by the importer (`openbfme_importer.livingworld`, schema
## `openbfme.living-world`) and consumed here without reinterpretation.
##
## Rules this file keeps, so that `wotr_state.gd` can hash and snapshot on top
## of it:
##
## * NOTHING here is presentation. Portraits, load screens, movies and effect
##   geometry are carried through verbatim as opaque identifiers so a future
##   view layer can resolve them; this file never loads or draws anything.
## * NO dependency on the retail-slice simulation. The strategic layer must be
##   testable and hashable with no tactical battle in existence.
## * FAIL-CLOSED. `load_from_dict()` returns false and fills `errors` on any
##   shape it does not fully understand; a partially-understood world is never
##   handed back. Unknown *authored content* (as opposed to a malformed
##   document) is not this layer's problem - the importer already recorded it as
##   a gap, and `import_gaps` carries that list through unchanged.
## * NO floats. Every authored decimal arrives from the importer pre-scaled as
##   an exact `*Milli` integer, so the whole world is byte-stable.
## * SORTED, DETERMINISTIC ORDER. Region ids are sorted once at load; every
##   accessor that returns a collection returns it in that order.

const SCHEMA := "openbfme.living-world"
const SCHEMA_VERSION := 1

## Retail caps War of the Ring at six players even though skirmish allows
## eight. Measured, not assumed: it is the maximum `MaxPlayers` across every
## shipped WOTR scenario in both BFME2 1.06 and RotWK 2.01.
const MAX_PLAYERS := 6

## Bounds. A hostile or corrupt pack must not be able to make the strategic
## layer allocate without limit.
const MAX_REGIONS := 512
const MAX_CONNECTIONS_PER_REGION := 32
const MAX_SCENARIOS := 256
const MAX_ARMY_ENTRIES := 64

## Retail's unauthored-region defaults, quoted in LivingWorldRegions.inc:
## "Each territory can have a custom CP limit (default is 1000)" / ally 400.
const DEFAULT_CP_LIMIT := 1000
const DEFAULT_ALLY_CP_LIMIT := 400

var schema_version := 0
var game := ""
var campaign_name := ""

## Sorted region ids. Every ordered accessor follows this order.
var region_ids: PackedStringArray = PackedStringArray()
## region id -> region record (see `_read_region`).
var regions: Dictionary = {}
## region id -> sorted PackedStringArray of neighbour ids.
var adjacency: Dictionary = {}

## Territory groupings (`ConcurrentRegionBonus`): unified-territory bonuses.
var territories: Array[Dictionary] = []
## Player templates by name, carrying the world/hero command-point economy.
var player_templates: Dictionary = {}
## `LivingWorldPlayerArmy` rosters by name.
var player_armies: Dictionary = {}
## `SpawnArmy` records by scripting name. Each value is an ARRAY of spawn rows
## in document order, because retail reuses one scripting name per player
## template: the shipped document carries ten different `HeroArmy1` rows, one
## per faction. Keying a single row by name silently kept whichever row loaded
## last, which seated every faction with the same (usually wrong) hero. Use
## `resolve_army_spawn()` to pick the row a seat may actually spawn.
var army_spawns: Dictionary = {}
## Scenario setups by name (ownership sets, start spots, defeat conditions).
var scenarios: Dictionary = {}
var scenario_names: PackedStringArray = PackedStringArray()
## The strategic->tactical handoff constants.
var rts_settings: Dictionary = {}

## Gaps the importer recorded, carried through untouched. Never silently empty:
## if the importer could not model something, it is visible here too.
var import_gaps: Array = []

## DEFECTS IN THE RETAIL DATA ITSELF that this loader refused, as opposed to
## `import_gaps`, which are things the IMPORTER could not model. Each row is
## `{kind, subject, detail, resolution}` and each names a shipped record that
## cannot mean anything - today, one self-referential `LivingWorldPlayerArmy`
## entry (see the comment at the roster loop). Never silently empty: a refusal
## nobody can see is indistinguishable from a repair nobody wrote down.
var data_defects: Array[Dictionary] = []

var errors: PackedStringArray = PackedStringArray()


## Load a `openbfme.living-world` document. Returns false on any shape this
## layer does not fully understand, leaving the instance unusable by design.
func load_from_dict(document: Dictionary, campaign: String = "") -> bool:
	_reset()
	if String(document.get("schema", "")) != SCHEMA:
		return _fail("schema is not %s" % SCHEMA)
	schema_version = int(document.get("schemaVersion", -1))
	if schema_version != SCHEMA_VERSION:
		return _fail("unsupported schemaVersion %d" % schema_version)
	game = String(document.get("game", ""))
	if game.is_empty():
		return _fail("document carries no game")

	var campaigns: Array = document.get("regionCampaigns", []) as Array
	if campaigns.is_empty():
		return _fail("document carries no region campaigns")
	var chosen: Dictionary = _select_campaign(campaigns, campaign)
	if chosen.is_empty():
		return _fail("no region campaign named %s" % campaign)
	campaign_name = String(chosen.get("name", ""))

	if not _read_regions(chosen.get("regions", []) as Array):
		return false
	if not _read_adjacency():
		return false

	for row in chosen.get("territoryBonuses", []) as Array:
		territories.append(_read_territory(row as Dictionary))
	territories.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return String(a.get("effect_name", "")) < String(b.get("effect_name", "")))

	for row in document.get("playerTemplates", []) as Array:
		var template := _read_player_template(row as Dictionary)
		player_templates[String(template.get("name", ""))] = template
	# RETAIL REDECLARES ARMY ROSTERS, and one of the redeclarations is broken.
	#
	# Twenty-four `LivingWorldPlayerArmy` names are declared twice in the shipped
	# RotWK data - once in `campaigns/common/livingworldstartingunits.inc` and once
	# in `campaigns/common/livingworldbuildableunits.inc`. Twenty-three of the
	# pairs are identical, so which one wins cannot matter.
	#
	# `DainPlayerArmy` is the exception, and it is a RETAIL TYPO:
	#
	#   livingworldstartingunits.inc:146   ArmyEntry ThingTemplate = DwarvenDain
	#   livingworldbuildableunits.inc:781  ArmyEntry ThingTemplate = DainPlayerArmy
	#
	# The second names the ROSTER ITSELF as the object the roster contains. No such
	# object exists - `DainPlayerArmy` is a `LivingWorldPlayerArmy` block, not a
	# thing template - so the entry cannot resolve to anything, in this port or in
	# retail. Under a plain last-one-wins read it shadowed the correct row, the
	# Dwarven `HeroArmy2` was raised with an empty auto-resolve roster, and every
	# battle that army was in refused BY NAME. That is the whole of the reported
	# "no Dwarven roster in the auto-resolve bindings bundle": the bundle binds 24
	# Dwarven objects including `DwarvenDain`; one roster row pointed at a name
	# that is not an object.
	#
	# SO A SELF-REFERENTIAL ROW IS REFUSED, not arbitrated. This invents nothing
	# and prefers nothing: a record whose only entry names the record is not a
	# record, and dropping it leaves whatever well-formed declaration of the same
	# name the document also carries. The refusal is RECORDED in `data_defects` so
	# it is a stated fact rather than a silent repair, and if a document ever
	# carries ONLY the broken row the roster stays absent and auto-resolve refuses
	# the army by name exactly as it does today.
	for row in document.get("playerArmies", []) as Array:
		var army := _read_player_army(row as Dictionary)
		var army_name := String(army.get("name", ""))
		var self_reference := _self_referential_entry(army_name, army.get("entries", []))
		if not self_reference.is_empty():
			data_defects.append({
				"kind": "self_referential_army_entry",
				"subject": army_name,
				"detail": self_reference,
				"resolution": ("the row is refused; any other declaration of '%s' in the "
					+ "document stands") % army_name,
			})
			continue
		player_armies[army_name] = army
	for row in document.get("defaultArmies", []) as Array:
		var spawn := _read_army_spawn(row as Dictionary)
		var key := String(spawn.get("scripting_name", ""))
		if key.is_empty():
			continue
		if not army_spawns.has(key):
			army_spawns[key] = []
		(army_spawns[key] as Array).append(spawn)

	var scenario_rows: Array = document.get("scenarios", []) as Array
	if scenario_rows.size() > MAX_SCENARIOS:
		return _fail("scenario count %d exceeds limit" % scenario_rows.size())
	var names: Array[String] = []
	for row in scenario_rows:
		var scenario := _read_scenario(row as Dictionary)
		var name := String(scenario.get("name", ""))
		if name.is_empty():
			return _fail("scenario without a name")
		if scenarios.has(name):
			return _fail("duplicate scenario %s" % name)
		scenarios[name] = scenario
		names.append(name)
	names.sort()
	scenario_names = PackedStringArray(names)

	rts_settings = _read_rts_settings(document.get("rtsSettings", {}) as Dictionary)
	import_gaps = (document.get("gaps", []) as Array).duplicate(true)
	return errors.is_empty()


## True when `region_id` exists in the loaded campaign.
func has_region(region_id: String) -> bool:
	return regions.has(region_id)


## Neighbours of `region_id`, in sorted order. Empty for an unknown region.
func neighbours(region_id: String) -> PackedStringArray:
	return adjacency.get(region_id, PackedStringArray()) as PackedStringArray


## True when the two regions are directly connected. Order does not matter.
func are_adjacent(from_region: String, to_region: String) -> bool:
	return neighbours(from_region).has(to_region)


## The TERRITORY record a region belongs to, or `{}` when it belongs to none.
## Retail's region panel reads "Territory of Region: Mordor" above the unified
## bonus, and this is where that comes from. Deterministic: the first territory
## IN DOCUMENT ORDER that lists the region. The document's order is retail's own
## authoring order, which is fixed for a given document, so this is stable
## without a sort - and retail never lists a region in two territories, so the
## choice between the two orders has no observable difference today. (The
## docstring used to claim sorted order, which the loop never did; the COMMENT
## was the wrong one and is the half that changed.)
func territory_of(region_id: String) -> Dictionary:
	for territory in territories:
		var members: PackedStringArray = territory.get("regions", PackedStringArray())
		if members.has(region_id):
			return territory
	return {}


## The whole region record, or `{}`.
func region(region_id: String) -> Dictionary:
	return regions.get(region_id, {}) as Dictionary


## Command-point cap for a region: the retail default when unauthored.
func region_cp_limit(region_id: String) -> int:
	var row := region(region_id)
	var limit := int(row.get("cp_limit", -1))
	return limit if limit >= 0 else DEFAULT_CP_LIMIT


## Ally command-point cap for a region: the retail default when unauthored.
func region_ally_cp_limit(region_id: String) -> int:
	var row := region(region_id)
	var limit := int(row.get("ally_cp_limit", -1))
	return limit if limit >= 0 else DEFAULT_ALLY_CP_LIMIT


## Territory groups whose regions are all owned by one player earn the
## territory bonus. Returns the sorted effect names for the given ownership.
func unified_territories(owner_by_region: Dictionary, player: int) -> PackedStringArray:
	var unified: Array[String] = []
	for territory in territories:
		var members: PackedStringArray = territory.get("regions", PackedStringArray())
		if members.is_empty():
			continue
		var all_owned := true
		for member in members:
			if int(owner_by_region.get(member, -1)) != player:
				all_owned = false
				break
		if all_owned:
			unified.append(String(territory.get("effect_name", "")))
	unified.sort()
	return PackedStringArray(unified)


## The SUMMED unified-territory bonuses `player` currently earns under
## `owner_by_region`: every bonus field of every territory whose member regions
## are all theirs, added together over sorted keys. `{}` when nothing is
## unified - absent and zero must read the same, so zero-only totals are
## dropped rather than carried as noise.
func territory_bonus_totals(owner_by_region: Dictionary, player: int) -> Dictionary:
	var unified := unified_territories(owner_by_region, player)
	var totals: Dictionary = {}
	for territory in territories:
		if not unified.has(String(territory.get("effect_name", ""))):
			continue
		var bonuses := territory.get("bonuses", {}) as Dictionary
		for key in _sorted_keys(bonuses):
			totals[key] = int(totals.get(key, 0)) + int(bonuses[key])
	var kept: Dictionary = {}
	for key in _sorted_keys(totals):
		if int(totals[key]) != 0:
			kept[key] = int(totals[key])
	return kept


func scenario(name: String) -> Dictionary:
	return scenarios.get(name, {}) as Dictionary


## THE ROW a seat may actually spawn under `army_name`, or why it may not.
## Returns `{ok, spawn, reason}`.
##
## Retail reuses scripting names across factions - ten `HeroArmy1` rows ship in
## the RotWK document - and scopes the real one two ways: the SCENARIO's own act
## armies override the campaign-wide default armies, and within either list the
## row is the one whose `SpawnForTemplates` names the seat's player template.
## Both scopes are applied here, in that order, and both demand EXACTLY ONE
## match: zero is a data error named as such, and two is an ambiguity this
## layer refuses to break with a tie-rule retail never wrote. (Measured against
## the shipped document, act-then-template resolution is unique for every
## seatable template of every scenario that authors ownership.)
##
## A name that matches NO spawn row anywhere is not resolved here at all -
## `{ok:false, reason:"unknown"}` - and the caller may still treat it as a bare
## `LivingWorldPlayerArmy` roster name, which is a different, legitimate way to
## raise an army.
func resolve_army_spawn(scenario_name: String, army_name: String, template: String) -> Dictionary:
	var scoped: Array = []
	var act_spawns: Dictionary = scenario(scenario_name).get("act_armies", {}) as Dictionary
	scoped.append(act_spawns.get(army_name, []) as Array)
	scoped.append(army_spawns.get(army_name, []) as Array)
	if (scoped[0] as Array).is_empty() and (scoped[1] as Array).is_empty():
		return {"ok": false, "spawn": {}, "reason": "unknown"}
	for rows_value in scoped:
		var rows := rows_value as Array
		var matched: Array[Dictionary] = []
		for row in rows:
			var spawn := row as Dictionary
			if (spawn.get("spawn_for_templates", PackedStringArray()) as PackedStringArray).has(template):
				matched.append(spawn)
		if matched.size() == 1:
			return {"ok": true, "spawn": matched[0], "reason": ""}
		if matched.size() > 1:
			return {"ok": false, "spawn": {}, "reason":
				"%d spawn rows named %s list template %s; retail states no tie rule" % [
					matched.size(), army_name, template]}
	return {"ok": false, "spawn": {}, "reason":
		"no spawn row named %s lists template %s" % [army_name, template]}


# --- loading -----------------------------------------------------------------

func _reset() -> void:
	schema_version = 0
	game = ""
	campaign_name = ""
	region_ids = PackedStringArray()
	regions = {}
	adjacency = {}
	territories = []
	player_templates = {}
	player_armies = {}
	army_spawns = {}
	scenarios = {}
	scenario_names = PackedStringArray()
	rts_settings = {}
	import_gaps = []
	data_defects = []
	errors = PackedStringArray()


func _fail(message: String) -> bool:
	errors.append(message)
	return false


func _select_campaign(campaigns: Array, wanted: String) -> Dictionary:
	if not wanted.is_empty():
		for row in campaigns:
			if String((row as Dictionary).get("name", "")) == wanted:
				return row as Dictionary
		return {}
	# With no explicit choice, prefer the campaign that actually carries a
	# connected graph: retail ships legacy Risk-campaign region lists with no
	# connections at all, and picking one of those silently would produce a
	# world where no army can ever move.
	var best: Dictionary = {}
	var best_connections := -1
	for row in campaigns:
		var candidate := row as Dictionary
		var count := 0
		for region_row in candidate.get("regions", []) as Array:
			count += (region_row as Dictionary).get("connections", []).size()
		if count > best_connections:
			best_connections = count
			best = candidate
	return best


func _read_regions(rows: Array) -> bool:
	if rows.is_empty():
		return _fail("region campaign carries no regions")
	if rows.size() > MAX_REGIONS:
		return _fail("region count %d exceeds limit" % rows.size())
	var ids: Array[String] = []
	for row in rows:
		var source := row as Dictionary
		var id := String(source.get("id", ""))
		if id.is_empty():
			return _fail("region without an id")
		if regions.has(id):
			return _fail("duplicate region %s" % id)
		var links: Array = source.get("connections", []) as Array
		if links.size() > MAX_CONNECTIONS_PER_REGION:
			return _fail("region %s has %d connections" % [id, links.size()])
		var neighbour_ids: Array[String] = []
		for link in links:
			var target := String((link as Dictionary).get("region", ""))
			if target.is_empty():
				return _fail("region %s has a connection without a target" % id)
			if not neighbour_ids.has(target):
				neighbour_ids.append(target)
		neighbour_ids.sort()
		regions[id] = {
			"id": id,
			"display_name": String(source.get("displayName", "")),
			"map_name": String(source.get("mapName", "")),
			"sub_object": String(source.get("subObject", "")),
			"region_portrait": String(source.get("regionPortrait", "")),
			"skirmish_still_image": String(source.get("skirmishStillImage", "")),
			"skirmish_music_track": String(source.get("skirmishMusicTrack", "")),
			"conquered_notice": String(source.get("conqueredNotice", "")),
			"bonuses": _read_int_map(source.get("bonuses", {}) as Dictionary),
			# The bonuses retail authored as a MACRO rather than a literal
			# (`FertileTerritoryBonus = FERTILE_TERRITORY_BONUS`). Carried
			# symbolically because that is exactly what the document carries; a
			# view layer resolves them through retail's own gamedata `#define`
			# table, or says the macro is unresolved.
			"bonus_macros": (source.get("bonusMacros", {}) as Dictionary).duplicate(true),
			"cp_limit": int(source.get("cpLimit", -1)),
			"ally_cp_limit": int(source.get("allyCpLimit", -1)),
			# AUTHORED MAP POSITION, carried through verbatim as opaque numbers -
			# this file still draws nothing. `has_center_point` is false when
			# retail left `CustomCenterPoint` off, in which case the engine derives
			# the marker from the region's map mesh, which no pack ships. A view
			# layer must SAY SO for those regions rather than invent a coordinate:
			# one BFME2 region (Rhun) is in exactly that state.
			"has_center_point": (source.get("centerPoint", null) is Dictionary),
			"center_x": int((source.get("centerPoint", {}) as Dictionary).get("x", 0)) if (source.get("centerPoint", null) is Dictionary) else 0,
			"center_y": int((source.get("centerPoint", {}) as Dictionary).get("y", 0)) if (source.get("centerPoint", null) is Dictionary) else 0,
			# BUILD PLOTS. Retail's region panel reads "3 Build Plots", and the
			# number is the count of `BuildingSpot` lines the region authors -
			# nothing else in the document carries it. Carried as a COUNT and as
			# the authored points, both verbatim; a region that authors none has
			# zero plots, which is a fact, not a missing value.
			"building_spot_count": (source.get("buildingSpots", []) as Array).size(),
			"building_spots": (source.get("buildingSpots", []) as Array).duplicate(true),
			"hero_army_spot_count": (source.get("heroArmySpots", []) as Array).size(),
			"garrison_army_spot_count": (source.get("garrisonArmySpots", []) as Array).size(),
			"restrict_buildings": (source.get("restrictBuildings", []) as Array).duplicate(true),
			"create_auto_fort": bool(source.get("createAutoFort", false)),
			"has_fortress": source.get("fortress", null) != null,
			"neighbours": PackedStringArray(neighbour_ids),
		}
		ids.append(id)
	ids.sort()
	region_ids = PackedStringArray(ids)
	return true


func _read_adjacency() -> bool:
	# The graph is symmetrised on load: retail authors both directions and the
	# importer gaps any one-way edge, so a missing reciprocal here would be a
	# pack that contradicts its own gap report. Symmetrising keeps movement
	# rules simple and can never invent a link neither side declared.
	var built: Dictionary = {}
	for id in region_ids:
		built[id] = {}
	for id in region_ids:
		var row := regions[id] as Dictionary
		for target in row.get("neighbours", PackedStringArray()) as PackedStringArray:
			if not regions.has(target):
				return _fail("region %s links to unknown region %s" % [id, target])
			(built[id] as Dictionary)[target] = true
			(built[target] as Dictionary)[id] = true
	for id in region_ids:
		var keys: Array = (built[id] as Dictionary).keys()
		keys.sort()
		var packed := PackedStringArray()
		for key in keys:
			packed.append(String(key))
		adjacency[id] = packed
	return true


func _read_territory(source: Dictionary) -> Dictionary:
	var members: Array[String] = []
	for name in source.get("regions", []) as Array:
		members.append(String(name))
	members.sort()
	return {
		"territory": String(source.get("territory", "")),
		"effect_name": String(source.get("effectName", "")),
		"regions": PackedStringArray(members),
		"bonuses": _read_int_map(source.get("bonuses", {}) as Dictionary),
	}


func _read_player_template(source: Dictionary) -> Dictionary:
	return {
		"name": String(source.get("name", "")),
		"faction": String(source.get("faction", "")),
		"starting_world_cp": int(source.get("startingWorldCp", -1)),
		"max_world_cp": int(source.get("maxWorldCp", -1)),
		"starting_hero_cp": int(source.get("startingHeroCp", -1)),
		"max_hero_cp": int(source.get("maxHeroCp", -1)),
		"scenario_start_resources": int(source.get("scenarioStartResources", -1)),
	}


## The detail string for an army roster whose entry names the roster itself, or
## "" when every entry names something else. Whole-list, not first-entry-only: a
## multi-entry roster with one self-reference is just as unresolvable.
static func _self_referential_entry(army_name: String, entries: Array) -> String:
	if army_name.is_empty():
		return ""
	for row in entries:
		var entry := row as Dictionary
		if String(entry.get("thing_template", "")) == army_name:
			return ("ArmyEntry ThingTemplate = %s names the LivingWorldPlayerArmy block "
				+ "itself, which is not a thing template") % army_name
	return ""


func _read_player_army(source: Dictionary) -> Dictionary:
	var entries: Array[Dictionary] = []
	for row in source.get("entries", []) as Array:
		if entries.size() >= MAX_ARMY_ENTRIES:
			break
		var entry := row as Dictionary
		entries.append({
			"thing_template": String(entry.get("thingTemplate", "")),
			"quantity": int(entry.get("quantity", 0)),
		})
	return {
		"name": String(source.get("name", "")),
		"display_name_tag": String(source.get("displayNameTag", "")),
		"entries": entries,
	}


func _read_army_spawn(source: Dictionary) -> Dictionary:
	var templates: Array[String] = []
	for name in source.get("spawnForTemplates", []) as Array:
		templates.append(String(name))
	templates.sort()
	return {
		"scripting_name": String(source.get("scriptingName", "")),
		"spawn_for_templates": PackedStringArray(templates),
		"hero_template_name": String(source.get("heroTemplateName", "")),
		"player_army": String(source.get("playerArmy", "")),
		"initial_region": String(source.get("initialRegion", "")),
		"is_hero": not String(source.get("heroTemplateName", "")).is_empty(),
	}


func _read_scenario(source: Dictionary) -> Dictionary:
	var ownership: Array[Dictionary] = []
	for row in source.get("ownershipSets", []) as Array:
		var set_row := row as Dictionary
		var members: Array[String] = []
		for name in set_row.get("regions", []) as Array:
			members.append(String(name))
		members.sort()
		var spawns: Array[Dictionary] = []
		for spawn_row in set_row.get("spawnArmies", []) as Array:
			var spawn := spawn_row as Dictionary
			var army_names: Array[String] = []
			for name in spawn.get("armies", []) as Array:
				army_names.append(String(name))
			army_names.sort()
			spawns.append({
				"region": String(spawn.get("region", "")),
				"armies": PackedStringArray(army_names),
			})
		spawns.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			return String(a.get("region", "")) < String(b.get("region", "")))
		# PRE-PLACED STRATEGIC BUILDINGS. Retail's Angmar scenario authors
		# `SpawnBuildings` rows whose building names are LW_* preprocessor macros
		# the importer recorded as unexpandable gaps; the TOKENS are carried
		# verbatim here, never interpreted - see `wotr_handoff.gd`'s data-gap
		# register for what that means downstream.
		var buildings: Array[Dictionary] = []
		for building_row in set_row.get("spawnBuildings", []) as Array:
			var building := building_row as Dictionary
			var tokens: Array[String] = []
			for token in building.get("buildings", []) as Array:
				tokens.append(String(token))
			tokens.sort()
			buildings.append({
				"region": String(building.get("region", "")),
				"buildings": PackedStringArray(tokens),
			})
		buildings.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			return String(a.get("region", "")) < String(b.get("region", "")))
		ownership.append({
			"regions": PackedStringArray(members),
			"start_region": String(set_row.get("startRegion", "")),
			"spawn_armies": spawns,
			"spawn_buildings": buildings,
		})
	# THE SCENARIO'S OWN ACT ARMIES, keyed like the campaign-wide defaults and
	# consulted FIRST by `resolve_army_spawn()`: retail's ownership sets name
	# `HeroArmy1` and mean the row THIS scenario authors for the seat's template,
	# not whichever default loaded last.
	var act_armies: Dictionary = {}
	for row in source.get("actArmies", []) as Array:
		var spawn := _read_army_spawn(row as Dictionary)
		var key := String(spawn.get("scripting_name", ""))
		if key.is_empty():
			continue
		if not act_armies.has(key):
			act_armies[key] = []
		(act_armies[key] as Array).append(spawn)
	# VICTORY TYPES: the options retail's setup screen offers for this scenario,
	# each carrying its own defeat and victory conditions, in the document's own
	# (retail's authored) order so an index into the list is stable.
	var victory_types: Array[Dictionary] = []
	for row in source.get("victoryTypes", []) as Array:
		victory_types.append(_read_victory_type(row as Dictionary))
	var max_players := int(source.get("maxPlayers", 0))
	return {
		"name": String(source.get("name", "")),
		"region_campaign": String(source.get("regionCampaign", "")),
		"min_players": int(source.get("minPlayers", 0)),
		"max_players": mini(max_players, MAX_PLAYERS) if max_players > 0 else 0,
		"authored_max_players": max_players,
		"is_evil_campaign": bool(source.get("isEvilCampaign", false)),
		"is_historical_scenario": bool(source.get("isHistoricalScenario", false)),
		"ownership_sets": ownership,
		"act_armies": act_armies,
		"victory_types": victory_types,
	}


## One victory type: its display tags verbatim (presentation, opaque here) and
## its three condition lists, each read through the one condition reader so the
## strategic rules can never see two shapes.
func _read_victory_type(source: Dictionary) -> Dictionary:
	var tags: Dictionary = {}
	var authored := source.get("displayTags", {}) as Dictionary
	for key in _sorted_keys(authored):
		tags[key] = String(authored[key])
	return {
		"display_tags": tags,
		"player_defeat_conditions": _read_conditions(source.get("playerDefeatConditions", []) as Array),
		"team_defeat_conditions": _read_conditions(source.get("teamDefeatConditions", []) as Array),
		"team_victory_conditions": _read_conditions(source.get("teamVictoryConditions", []) as Array),
	}


func _read_conditions(rows: Array) -> Array[Dictionary]:
	var conditions: Array[Dictionary] = []
	for row in rows:
		var source := row as Dictionary
		var teams: Array[int] = []
		for value in source.get("teams", []) as Array:
			teams.append(int(value))
		teams.sort()
		var regions: Array[String] = []
		for name in source.get("controlledRegions", []) as Array:
			regions.append(String(name))
		regions.sort()
		conditions.append({
			"kind": String(source.get("kind", "")),
			"teams": PackedInt32Array(teams),
			"controlled_regions": PackedStringArray(regions),
			"lose_if_capital_lost": bool(source.get("loseIfCapitalLost", false)),
			# Retail's own -1 sentinel means "this comparison is off"; carried
			# through unchanged so the evaluator tests `>= 0` exactly as retail's
			# field names read.
			"min_controlled_regions": int(source.get("numControlledRegionsGreaterOrEqualTo", -1)),
			"max_controlled_regions": int(source.get("numControlledRegionsLessOrEqualTo", -1)),
		})
	return conditions


func _read_rts_settings(source: Dictionary) -> Dictionary:
	return {
		"seconds_per_reinforcement": int(source.get("secondsPerReinforcement", -1)),
		"starting_cash_rts": int(source.get("startingCashRts", -1)),
		"starting_cash_rts_with_fort": int(source.get("startingCashRtsWithFort", -1)),
		"initial_revival_cost_milli": int(source.get("initialRevivalCostMilli", -1)),
		"initial_revival_time_milli": int(source.get("initialRevivalTimeMilli", -1)),
	}


func _sorted_keys(source: Dictionary) -> PackedStringArray:
	var names: Array[String] = []
	for key in source.keys():
		names.append(String(key))
	names.sort()
	return PackedStringArray(names)


func _read_int_map(source: Dictionary) -> Dictionary:
	var keys: Array = source.keys()
	keys.sort()
	var mapped: Dictionary = {}
	for key in keys:
		mapped[String(key)] = int(source[key])
	return mapped
