extends RefCounted

## The War of the Ring SESSION: the one seam between a player-facing strategic
## screen and the authoritative strategic layer.
##
## `wotr_state.gd` owns the rules, `wotr_handoff.gd` derives the brief and
## `wotr_battle.gd` mints the commitment. None of them knows where a document
## comes from, how a session starts, or what survives a scene change - and a UI
## that answered those three questions inline would answer them differently every
## time. This file answers them once.
##
## THE RULES IT KEEPS, and why each one is here rather than in the screen:
##
## * THE DOCUMENT IS FOUND OR THE SESSION REFUSES. There is no fixture fallback,
##   no synthesised map and no "demo world". `locate_document()` returns a named
##   reason when nothing is found and `begin()` cannot be reached without one.
##   A fabricated strategic map would be indistinguishable from a real one on
##   screen, which is the entire silent-fallback failure class this project has
##   spent the day removing.
##
## * EVERY UI SELECTION THAT DECIDES WHAT A BATTLE IS FLOWS THROUGH THE
##   COMMITMENT. `commit_attack()` is the ONLY way to start a battle, it takes
##   the target region and nothing else, and the tactical configuration a caller
##   receives is derived from `state.pending_battle` - the record inside the
##   strategic hash. `tactical_roster()` re-derives from that record rather than
##   returning a stored array, so a caller cannot be handed a roster that the
##   hash never saw. The attacker is `state.active_player()`, authoritative
##   state, never an argument.
##
## * PRESENTATION STATE IS NOT STRATEGIC STATE. `selected_region`,
##   `selected_target` and `hover_region` live here, are per-seat by
##   construction, never enter `authoritative_state()`, and are dropped by
##   `handoff_payload()`. A camera position or a highlight must never change a
##   hash.
##
## * SORTED, DETERMINISTIC ORDER everywhere an order reaches a result: seats are
##   taken from sorted template names, targets are returned sorted, and the
##   battlefield binding table is built over sorted map ids.

const WorldScript = preload("res://src/wotr/wotr_world.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const HandoffScript = preload("res://src/wotr/wotr_handoff.gd")
const BattleScript = preload("res://src/wotr/wotr_battle.gd")
const AutoResolveScript = preload("res://src/wotr/wotr_autoresolve.gd")
const AutoResolveBattleScript = preload("res://src/wotr/wotr_autoresolve_battle.gd")
const AutoResolveBindingsScript = preload("res://src/wotr/wotr_autoresolve_bindings.gd")
const AiScript = preload("res://src/wotr/wotr_ai.gd")
const BuildingsScript = preload("res://src/wotr/wotr_buildings.gd")
const ReceiptScript = preload("res://src/wotr/wotr_receipt.gd")

## The document a pack ships, relative to its root. No pack built before the
## living-world lane carries one; that is a real state and it is reported, not
## papered over.
const PACK_DOCUMENT_RELATIVE := "data/living-world.json"
const DOCUMENT_MAX_BYTES := 32 * 1024 * 1024

## The environment override the living-world runner documents, and the only
## reason this file knows an environment variable exists at all.
const DOCUMENT_ENV := "OPENBFME_LIVING_WORLD_DOC"
const CONTENT_ENV := "OPENBFME_CONTENT"

## LIVING-WORLD FACTION -> PACK FACTION. Measured, not guessed: these are the
## verbatim `Faction =` values on the shipped `LivingWorldPlayerTemplate` rows in
## BFME2 1.06 (`FactionMen`, `FactionElves`, `FactionDwarves`, `FactionIsengard`,
## `FactionMordor`, `FactionWild`) mapped to the pack faction ids the retail
## slice resolves through `RetailFactionManifest`. `FactionAngmar` is RotWK's and
## is bound for the RotWK document; `FactionObserver` is deliberately absent - it
## is retail's spectator seat, it has no army, and binding it would seat a player
## who cannot fight.
##
## A table, not a rule. Deriving these by stripping `Faction` and lowercasing
## would be a guess wearing the costume of a lookup, and would answer confidently
## for `FactionObserver` too.
const FACTION_BINDINGS := {
	"FactionAngmar": "angmar",
	"FactionDwarves": "dwarves",
	"FactionElves": "elves",
	"FactionIsengard": "isengard",
	"FactionMen": "men",
	"FactionMordor": "mordor",
	"FactionWild": "wild",
}

## The handoff record that survives the tactical scene change. It is NOT a save
## format and does not pretend to be one: it carries the strategic snapshot
## verbatim (so the battle in flight rides along inside the hash) plus the two
## facts needed to rebuild the same world - which document and which campaign.
const HANDOFF_SCHEMA := "openbfme.wotr-session-handoff"
const HANDOFF_SCHEMA_VERSION := 1

var world: WorldScript = null
var state: StateScript = null
## Immutable-by-copy evidence minted before an evidenced session is opened.
var input_receipt: Dictionary = {}

## Retail's auto-resolve tables and the object-to-block bindings that map an
## army onto them. BOTH are needed to auto-resolve anything and neither is
## faked: with either missing, `auto_resolve_reason` says which one and where it
## looked, `state.roster_units` stays empty, and every attempt to auto-resolve
## refuses by name instead of fighting a battle with invented numbers.
var autoresolve: AutoResolveScript = null
var autoresolve_bindings: AutoResolveBindingsScript = null
var auto_resolve_reason := ""
## Living-world templates the binding bundle has no auto-resolve data for, from
## the last `load_auto_resolve()`. Named on screen, never defaulted.
var auto_resolve_unbound_templates: PackedStringArray = PackedStringArray()

## THE OPPONENT. Built once per session and reused, because it carries retail's
## AI template and re-reading a file every turn would make the campaign's speed
## depend on the disk. Always present - `wotr_ai.gd` is a decision function with
## no state of its own beyond that template - so a caller never has to test for
## null before asking whose turn it is.
var ai: AiScript = AiScript.new()

## Why the last `load_ai_template()` refused, empty when it loaded. Named on
## screen: an opponent playing without retail's preference weights is a DIFFERENT
## opponent from one playing with them, and the player is entitled to know which.
var ai_template_reason := ""

## RETAIL'S STRATEGIC BUILDING CATALOGUE, or null. Same shape and same discipline
## as `load_auto_resolve()` and `load_ai_template()`: a session whose catalogue is
## missing is a real state that must still start, run and fight - it simply
## cannot build, and `build_options()` and `commit_build()` say why by name
## instead of offering a ring of buttons that do nothing, which is precisely the
## defect this lane was opened to remove.
var buildings: BuildingsScript = null
var building_catalogue_reason := ""
## Roots the caller last offered for bundle discovery, kept so a lazy reload
## after `adopt_handoff()` searches the same places the first load did.
var _bundle_roots: Array = []

## Where the loaded document came from, and how it was found ("pack" or "env").
var document_path := ""
var document_source := ""
var scenario_name := ""

## Why the last operation refused, in order. Never empty on a false return.
var refusals: PackedStringArray = PackedStringArray()

## PRESENTATION ONLY - per seat, never hashed, never handed to the simulation.
var selected_region := ""
var selected_target := ""
var hover_region := ""


# --- document discovery ------------------------------------------------------

## Find the living-world document. `pack_roots` are the roots the game actually
## mounted (ContentDB's, in the order it loaded them); they are searched FIRST,
## because a document shipped inside the selected pack is the product path.
## `OPENBFME_LIVING_WORLD_DOC` is the documented workspace fallback and is what
## makes the lane usable before a ~40 minute pack rebuild ships one.
##
## Returns `{ok, path, source, document, reason}`. On failure `reason` is a
## player-facing sentence naming BOTH places that were searched, because "not
## found" alone sends nobody anywhere.
static func locate_document(pack_roots: Array = []) -> Dictionary:
	var searched: Array[String] = []
	for root_value in pack_roots:
		var root := String(root_value).strip_edges()
		if root.is_empty():
			continue
		var packed := root.path_join(PACK_DOCUMENT_RELATIVE)
		searched.append(packed)
		if not FileAccess.file_exists(packed):
			continue
		var pack_document: Variant = _read_document(packed)
		if pack_document is Dictionary:
			return {
				"ok": true, "path": packed, "source": "pack",
				"document": pack_document, "reason": "",
			}
		return _not_found("the pack document %s did not parse as JSON" % packed)

	var explicit := OS.get_environment(DOCUMENT_ENV).strip_edges()
	if not explicit.is_empty():
		if not FileAccess.file_exists(explicit):
			return _not_found("%s points at %s, which does not exist" % [DOCUMENT_ENV, explicit])
		var explicit_document: Variant = _read_document(explicit)
		if not (explicit_document is Dictionary):
			return _not_found("%s points at %s, which did not parse as JSON" % [DOCUMENT_ENV, explicit])
		return {
			"ok": true, "path": explicit, "source": "env",
			"document": explicit_document, "reason": "",
		}

	# The runner's third route, kept so a workspace content root behaves the same
	# here as it does under `wotr_livingworld_pack_runner`: the packs named by an
	# OPENBFME_CONTENT selection, active pack first.
	var content_root := OS.get_environment(CONTENT_ENV).strip_edges()
	if not content_root.is_empty():
		for relative in _selection_pack_relatives(content_root):
			var path := content_root.path_join(relative).path_join(PACK_DOCUMENT_RELATIVE)
			searched.append(path)
			if not FileAccess.file_exists(path):
				continue
			var selected_document: Variant = _read_document(path)
			if selected_document is Dictionary:
				return {
					"ok": true, "path": path, "source": "pack",
					"document": selected_document, "reason": "",
				}
			return _not_found("the pack document %s did not parse as JSON" % path)

	return _not_found(
		"no living-world document is available: no mounted pack ships %s "
		% PACK_DOCUMENT_RELATIVE
		+ "(packs built before the living-world lane do not), and %s is unset. "
		% DOCUMENT_ENV
		+ "Generate one with `openbfme-import living-world --install <retail>` "
		+ "and point %s at it." % DOCUMENT_ENV
	)


static func _not_found(reason: String) -> Dictionary:
	return {"ok": false, "path": "", "source": "", "document": {}, "reason": reason}


static func _read_document(path: String) -> Variant:
	var handle := FileAccess.open(path, FileAccess.READ)
	if handle == null:
		return null
	if handle.get_length() <= 0 or handle.get_length() > DOCUMENT_MAX_BYTES:
		handle.close()
		return null
	var text := handle.get_as_text()
	handle.close()
	return JSON.parse_string(text)


static func _selection_pack_relatives(content_root: String) -> Array[String]:
	var relatives: Array[String] = []
	var selection: Variant = _read_document(content_root.path_join("selection.json"))
	if not (selection is Dictionary):
		return relatives
	var active := String((selection as Dictionary).get("activePack", ""))
	if not active.is_empty():
		relatives.append(active)
	for extra in (selection as Dictionary).get("supplementalPacks", []) as Array:
		relatives.append(String(extra))
	return relatives


# --- session lifecycle -------------------------------------------------------

## Seats a session can offer: one row per player template that carries a real
## command-point economy AND binds to a pack faction. Sorted by template name so
## the offer is reproducible. `pack_factions` is the set of faction ids the
## tactical layer can actually field; a template whose faction is unavailable is
## RETURNED with its reason rather than hidden, so the screen can say why.
func seat_options(available_pack_factions: Dictionary) -> Array[Dictionary]:
	var options: Array[Dictionary] = []
	if world == null:
		return options
	var names: Array[String] = []
	for key in world.player_templates.keys():
		names.append(String(key))
	names.sort()
	for name in names:
		var template := world.player_templates[name] as Dictionary
		if int(template.get("starting_world_cp", -1)) <= 0:
			continue
		var living_faction := String(template.get("faction", ""))
		var pack_faction := String(FACTION_BINDINGS.get(living_faction, ""))
		# `available_pack_factions` is the menu's own availability map: faction id
		# -> "" when the tactical layer can field it, else the fail-closed note.
		# An id that is absent entirely is not converted at all.
		var reason := ""
		if pack_faction.is_empty():
			reason = "%s has no pack faction binding" % living_faction
		elif not available_pack_factions.has(pack_faction):
			reason = "the %s faction is not converted" % pack_faction
		else:
			reason = String(available_pack_factions[pack_faction])
		options.append({
			"template": name,
			"living_faction": living_faction,
			"pack_faction": pack_faction,
			"unavailable_reason": reason,
		})
	return options


## Load retail's auto-resolve tables and the unit bindings. Returns
## `{ok, reason, rules_path, bindings_path}`; on failure `reason` names every
## path tried and the command that produces each bundle.
##
## SEPARATE FROM `begin()` on purpose: a session with no auto-resolve data is a
## real and legitimate state - it is what every pack built before this lane
## produces - and it must start, run and fight tactical battles normally. What
## it must NOT do is auto-resolve, and `commit_attack()` refuses that by name.
func load_auto_resolve(pack_roots: Array = []) -> Dictionary:
	autoresolve = null
	autoresolve_bindings = null
	auto_resolve_reason = ""
	auto_resolve_unbound_templates = PackedStringArray()
	var model := AutoResolveScript.new()
	var located: Dictionary = model.locate_and_load(pack_roots)
	if not bool(located.get("ok", false)):
		auto_resolve_reason = String(located.get("reason", ""))
		return {"ok": false, "reason": auto_resolve_reason, "rules_path": "", "bindings_path": ""}
	var bound := AutoResolveBindingsScript.new()
	var found: Dictionary = bound.locate_and_load(pack_roots)
	if not bool(found.get("ok", false)):
		auto_resolve_reason = String(found.get("reason", ""))
		return {"ok": false, "reason": auto_resolve_reason,
			"rules_path": String(located.get("path", "")), "bindings_path": ""}
	autoresolve = model
	autoresolve_bindings = bound
	return {
		"ok": true, "reason": "",
		"rules_path": String(located.get("path", "")),
		"bindings_path": String(found.get("path", "")),
	}


## Roster name -> the auto-resolve units an army of that roster fields, each
## already carrying retail's `HitpointsAtLevel`. Built ONCE per session, over
## sorted roster names so the order a template is first reported unbound in is
## reproducible. `{}` when either bundle is missing, which is what makes every
## army field no units and every auto-resolve refuse by name.
func _build_roster_units() -> Dictionary:
	var table: Dictionary = {}
	auto_resolve_unbound_templates = PackedStringArray()
	if world == null or autoresolve == null or autoresolve_bindings == null:
		return table
	var bindings := autoresolve_bindings.objects()
	var unbound: Dictionary = {}
	var names: Array[String] = []
	for key in world.player_armies.keys():
		names.append(String(key))
	names.sort()
	for name in names:
		var roster := world.player_armies[name] as Dictionary
		var built: Dictionary = AutoResolveBattleScript.units_for_roster(
			bindings, roster.get("entries", []))
		for template in built["unbound"] as PackedStringArray:
			unbound[String(template)] = true
		var with_health: Dictionary = AutoResolveBattleScript.with_hitpoints(
			autoresolve.rules, built["units"])
		for note in with_health["unresolved"] as PackedStringArray:
			unbound[String(note)] = true
		table[name] = with_health["units"]
	var sorted_unbound: Array[String] = []
	for key in unbound.keys():
		sorted_unbound.append(String(key))
	sorted_unbound.sort()
	auto_resolve_unbound_templates = PackedStringArray(sorted_unbound)
	return table


## ============================================================================
## FREEFORM SCENARIOS
## ============================================================================
##
## RETAIL'S HEADLINE SCENARIO IS FREEFORM AND IT IS THE ONE THE SETUP SCREEN
## OPENS ON. `WOTRScenario001` - the one whose display name is simply "War of the
## Ring" - authors NO `OwnershipSet` rows at all, and says so in its own
## description: "This is a freeform game with no preset starting locations or
## claimed territories." Until this pass a session refused it, so the setup
## screen fell through to the first PRESET scenario instead, which claimed all 52
## regions for six seats and painted the whole of Middle-earth in six saturated
## colours. That picture was, in the adversarial read, "a region-adjacency debug
## visualiser, not a strategic map" - and the cause was never the renderer. It was
## this file refusing retail's own default.
##
## SO A FREEFORM SCENARIO STARTS FROM A START REGION PER SEAT, and the caller
## supplies them. That is not an invented rule: the scenario authors
## `defaultStartSpots` (`WOTRScenario001` names Mordor and Rivendell) and it
## authors `disallowStartInRegions` (empty, i.e. no region is barred), which is
## the whole shape of "each player begins in one region of their choosing".
##
## WHAT IS **NOT** IN ANY SHIPPED FILE, and is therefore a NAMED GAP rather than a
## default: WHICH region a seat beyond the authored spots begins in. RotWK's own
## capture of this screen shows six seats each holding one small territory, so
## retail assigns the remaining four somehow - and that assignment lives in
## `game.dat`, exactly like the `MPRules` table and the phase-timer second counts.
## This layer therefore REFUSES a freeform start whose seats are not all given a
## region, by name, instead of dealing out four regions of its own choosing.
const FREEFORM_START_GAP := (
	"retail's freeform scenarios author defaultStartSpots (two, on the shipped "
	+ "War of the Ring scenario) and no rule anywhere in data/ini says which "
	+ "region the remaining seats begin in; that assignment is compiled into "
	+ "game.dat. A seat past the authored spots must therefore be given a start "
	+ "territory explicitly - this layer will not choose one for it.")


## The start regions a freeform scenario AUTHORS, in the document's own order.
## Read off the RAW document rather than the loaded world, because
## `defaultStartSpots` is scenario setup that `wotr_world` does not model - and
## reading it here, at the one seam that starts a session, is cheaper and far more
## honest than teaching the world about a field only this path needs.
##
## Returns EMPTY for a scenario the document does not carry, which is the same
## answer as "authors none" and is treated the same way: every seat then needs an
## explicit region.
static func default_start_spots(document: Dictionary, scenario: String) -> PackedStringArray:
	if scenario.is_empty():
		return PackedStringArray()
	for row_value in document.get("scenarios", []) as Array:
		var row := row_value as Dictionary
		if String(row.get("name", "")) != scenario:
			continue
		var spots: Array[String] = []
		for spot in row.get("defaultStartSpots", []) as Array:
			var name := String(spot).strip_edges()
			if not name.is_empty() and not spots.has(name):
				spots.append(name)
		return PackedStringArray(spots)
	return PackedStringArray()


## True when `scenario` claims nothing for anybody - retail's freeform shape.
func is_freeform(scenario: String) -> bool:
	if world == null:
		return false
	var record := world.scenario(scenario)
	if record.is_empty():
		return false
	return (record.get("ownership_sets", []) as Array).is_empty()


## Load `document` and seat a session. Fails closed and NAMES the reason: a
## session that half-started would put a plausible map on screen.
##
## `rules` is the setup screen's RULES tab: `battle_type` and
## `battle_type_priority`. It defaults to retail's own screen defaults, so every
## existing caller keeps the behaviour it had.
##
## `start_regions` is one region per seat, in seat order, and is ONLY for a
## FREEFORM scenario. Passing it alongside a scenario that authors its own
## ownership is REFUSED rather than merged: two sources of truth for where a seat
## begins is how a campaign comes to disagree with the picture of itself.
## Production admission seam. Identity is required and has no default: callers
## must name the loaded selection, mounted pack manifests, and configuration
## bundle presence. Evidence is completely minted before the low-level begin is
## entered, so an incomplete or unhashable identity cannot leave strategic state.
func begin_evidenced(
	document: Dictionary, campaign: String, scenario: String, seats: Array,
	rules: Dictionary, start_regions: PackedStringArray, identity: Dictionary
) -> bool:
	world = null
	state = null
	input_receipt = {}
	refusals = PackedStringArray()
	var minted := ReceiptScript.mint(
		String(identity.get("document_path", "")),
		String(identity.get("document_source", "")), document,
		campaign, scenario, seats, rules, start_regions, FACTION_BINDINGS, identity)
	if minted.is_empty():
		refusals.append("the War of the Ring input identity is incomplete or cannot be hashed")
		return false
	if not begin(document, campaign, scenario, seats, rules, start_regions):
		return false
	# Commit evidence only with a successfully opened state. Handoff returns a
	# further copy, so callers cannot mutate this record through an alias.
	input_receipt = minted
	return true


func begin(
	document: Dictionary, campaign: String, scenario: String, seats: Array,
	rules: Dictionary = {}, start_regions: PackedStringArray = PackedStringArray()
) -> bool:
	# This explicit low-level seam is intentionally unreceipted, including when a
	# Session object that was previously evidenced is reused through it.
	input_receipt = {}
	refusals = PackedStringArray()
	world = WorldScript.new()
	if not world.load_from_dict(document, campaign):
		for message in world.errors:
			refusals.append(String(message))
		if refusals.is_empty():
			refusals.append("the living-world document did not load")
		world = null
		return false
	if seats.size() < 2:
		refusals.append("a War of the Ring session needs at least two seats")
		world = null
		return false
	var freeform := is_freeform(scenario)
	if not freeform and not start_regions.is_empty():
		refusals.append((
			"scenario '%s' authors its own ownership sets, so the %d start "
			+ "region(s) offered alongside them would be a second source of truth "
			+ "for where a seat begins") % [scenario, start_regions.size()])
		world = null
		return false
	state = StateScript.new()
	if not state.setup(world, seats, rules):
		refusals.append("the strategic layer refused the seating")
		world = null
		state = null
		return false
	# BEFORE any army is placed, because `place_army()` copies the units onto
	# the army record and an army placed without them would field nothing.
	state.roster_units = _build_roster_units()
	# BEFORE `apply_ownership_sets()`, because a scenario's `SpawnBuildings` rows
	# carry retail's `LW_*` tokens and resolving one into the concrete building
	# the controlling faction actually gets needs the catalogue. Without it the
	# tokens stand VERBATIM and typeless, which is honest but earns no income - so
	# binding it here is the difference between a campaign that opens with three
	# fortresses and one that opens with three question marks.
	_ensure_building_catalogue()
	state.building_catalogue = buildings
	# APPLIED EVEN FOR A FREEFORM SCENARIO, where it claims nothing: it is what
	# records `state.scenario_name` (which every later army resolution is scoped
	# by) and what checks the chosen victory type against the scenario's own list.
	if not state.apply_ownership_sets(scenario):
		refusals.append("scenario '%s' has no ownership this campaign can apply" % scenario)
		world = null
		state = null
		return false
	if freeform and not _apply_freeform_starts(scenario, seats, start_regions):
		world = null
		state = null
		return false
	scenario_name = scenario
	selected_region = ""
	selected_target = ""
	hover_region = ""
	return true


## Seat a freeform scenario on one start region each. ALL-OR-NOTHING by
## construction: every region is checked before the first one is claimed, so a
## refusal leaves a board nobody has been placed on.
func _apply_freeform_starts(
		scenario: String, seats: Array, start_regions: PackedStringArray) -> bool:
	if start_regions.size() != seats.size():
		refusals.append((
			"scenario '%s' is freeform and claims nothing, so every one of its %d "
			+ "seats needs a start territory; %d were given. %s") % [
				scenario, seats.size(), start_regions.size(), FREEFORM_START_GAP])
		return false
	var seen: Dictionary = {}
	for index in range(start_regions.size()):
		var region_id := String(start_regions[index]).strip_edges()
		if region_id.is_empty():
			refusals.append("seat %d has no start territory. %s" % [index, FREEFORM_START_GAP])
			return false
		if not world.has_region(region_id):
			refusals.append("seat %d starts in '%s', which this campaign has no region for" % [
				index, region_id])
			return false
		if seen.has(region_id):
			refusals.append("seats %d and %d both start in %s" % [
				int(seen[region_id]), index, region_id])
			return false
		seen[region_id] = index
	for index in range(start_regions.size()):
		var region_id := String(start_regions[index])
		if not state.transfer_region(region_id, index):
			refusals.append("the strategic layer refused %s to seat %d" % [region_id, index])
			return false
		# RETAIL'S OWN CAPITAL FIELD, the one `loseIfCapitalLost` tests. An
		# ownership set carries `StartRegion` for exactly this and a freeform
		# scenario has no set, so the seat's chosen start IS its capital.
		(state.players[index] as Dictionary)["capital"] = region_id
		for army_name in _seat_start_armies(scenario, index):
			if state.place_army(index, region_id, String(army_name)) < 0:
				# The strategic layer's own words for it: `place_army` records a
				# `rejected` event rather than raising, and repeating that event
				# here is what turns "it did not start" into a sentence.
				refusals.append("seat %d could not raise %s in %s: %s" % [
					index, String(army_name), region_id, _last_state_rejection()])
				return false
	return true


## The most recent `rejected` event the strategic layer recorded, or a stated
## fallback. `wotr_state` reports refusals as events rather than as a field, so
## this is where they are read back out.
func _last_state_rejection() -> String:
	if state == null:
		return "no strategic state"
	for index in range(state.events.size() - 1, -1, -1):
		var event := state.events[index] as Dictionary
		if String(event.get("kind", "")) == "rejected":
			return String(event.get("reason", ""))
	return "the strategic layer refused it without recording a reason"


## Every army RETAIL AUTHORS for this seat, by scripting name, sorted.
##
## NOT BORROWED FROM A PRESET SCENARIO. The preset scenarios spawn
## `GarrisonArmy1, HeroArmy1, HeroArmy2, HeroArmy3` per set, and hard-coding that
## list here would be this project deciding what a freeform start contains. What
## is read instead is the authoring itself: every `SpawnArmy` scripting name that
## lists THIS SEAT'S TEMPLATE, taken from the scenario's own act armies first and
## the campaign-wide defaults second - the same two scopes, in the same order,
## that `world.resolve_army_spawn()` searches. On the shipped freeform scenario
## that yields exactly the four names above for a Men seat, because those are the
## four rows retail wrote for it.
func _seat_start_armies(scenario: String, seat_index: int) -> PackedStringArray:
	var names: Array[String] = []
	if world == null or state == null:
		return PackedStringArray()
	var template := String((state.players[seat_index] as Dictionary).get("template", ""))
	if template.is_empty():
		return PackedStringArray()
	var scopes: Array = [
		world.scenario(scenario).get("act_armies", {}) as Dictionary,
		world.army_spawns,
	]
	for scope_value in scopes:
		var scope := scope_value as Dictionary
		var keys: Array[String] = []
		for key in scope.keys():
			keys.append(String(key))
		keys.sort()
		for key in keys:
			if names.has(key):
				continue
			for row in scope[key] as Array:
				var spawn := row as Dictionary
				if (spawn.get("spawn_for_templates", PackedStringArray()) as PackedStringArray).has(template):
					names.append(key)
					break
	return PackedStringArray(names)


## The scenarios this campaign can actually start.
##
## TWO SHAPES, BOTH REPORTED. A PRESET scenario is startable when it authors an
## ownership set per seat, each with regions - that is what it has always meant. A
## FREEFORM scenario claims nothing by design and is startable when the caller can
## give every seat a start territory; whether it can is the caller's business, so
## `include_freeform` says whether to list them. `startable_scenarios(n)` alone
## keeps its old meaning exactly, so no existing caller changes behaviour.
func startable_scenarios(seat_count: int, include_freeform: bool = false) -> PackedStringArray:
	var usable := PackedStringArray()
	if world == null:
		return usable
	for name in world.scenario_names:
		var scenario := world.scenario(name)
		if String(scenario.get("region_campaign", "")) != world.campaign_name:
			continue
		if int(scenario.get("max_players", 0)) < seat_count:
			continue
		var sets: Array = scenario.get("ownership_sets", []) as Array
		if sets.is_empty():
			if include_freeform:
				usable.append(name)
			continue
		if sets.size() < seat_count:
			continue
		var every_seat_starts_owned := true
		for index in range(seat_count):
			if ((sets[index] as Dictionary).get("regions", PackedStringArray()) as PackedStringArray).is_empty():
				every_seat_starts_owned = false
		if every_seat_starts_owned:
			usable.append(name)
	return usable


# --- the strategic view (read-only projections) ------------------------------

## One row per region in the world's sorted order: id, display name, owner, army
## count, command points standing there, and the authored map position when
## retail authored one. A region WITHOUT an authored position is reported as
## such (`has_position` false) rather than given a made-up coordinate.
func region_rows() -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	if world == null or state == null:
		return rows
	for region_id in world.region_ids:
		var region := world.region(region_id)
		var owner := state.owner_of(region_id)
		var armies := state.armies_in_region(region_id)
		rows.append({
			"id": region_id,
			"display_name": String(region.get("display_name", "")),
			"map_name": String(region.get("map_name", "")),
			"owner": owner,
			"armies": armies.size(),
			"command_points": state.command_points_in_region(region_id, owner) if owner != StateScript.NEUTRAL else 0,
			"has_position": bool(region.get("has_center_point", false)),
			"position": Vector2(float(region.get("center_x", 0)), float(region.get("center_y", 0))),
		})
	return rows


## Regions the ACTIVE seat owns and has an army standing in - the only regions an
## attack can be staged from. Sorted (world order).
func staging_regions() -> PackedStringArray:
	var staged := PackedStringArray()
	if state == null:
		return staged
	var player := state.active_player()
	for region_id in state.regions_owned_by(player):
		if not state.armies_in_region(region_id).is_empty():
			staged.append(region_id)
	return staged


## Regions the active seat may TAKE from `from_region`: adjacent, not theirs, and
## legal by the strategic layer's own rules. Sorted, because the order reaches
## what the screen offers.
##
## TWO RULES, ONE LIST, and the union is the point. An enemy-held region is taken
## by `can_attack()` - a battle. An UNOWNED one is taken by `can_claim()` - a
## march by a hero army, which is what retail's own tutorial narration says
## expands a kingdom (see `CLAIM_RECORD_SCHEMA` in `wotr_state.gd` for the shipped
## strings). `commit_attack()` reads the region's owner and takes the matching
## path, so a caller - the screen's buttons, or `wotr_ai.gd`, which builds its
## candidate set from exactly this function - never has to know which it picked.
##
## Before this pass the list was `can_attack()` alone, `can_attack()` said yes to
## neutral ground, and `wotr_battle.gd` then refused to configure a battle with no
## defending faction. Every neutral region in the list was a control that did
## nothing. Neither half of that is true now: neutral regions are here because
## they are CLAIMABLE, and only when a hero army can actually make the claim.
func attack_targets(from_region: String) -> PackedStringArray:
	var targets := PackedStringArray()
	if world == null or state == null:
		return targets
	var player := state.active_player()
	if state.owner_of(from_region) != player:
		return targets
	if state.armies_in_region(from_region).is_empty():
		return targets
	var found: Array[String] = []
	for neighbour in world.neighbours(from_region):
		if state.owner_of(neighbour) == player:
			continue
		if not (state.can_attack(player, neighbour) or state.can_claim(player, neighbour)):
			continue
		found.append(neighbour)
	found.sort()
	return PackedStringArray(found)


## Which of `attack_targets()` are CLAIMS rather than battles - unowned ground the
## active seat can march a hero army into. A pure projection, offered so a screen
## can label the two differently ("TAKE" against "ATTACK") without re-deriving the
## rule and risking a different answer from the one `commit_attack()` will take.
func claim_targets(from_region: String) -> PackedStringArray:
	var claims: Array[String] = []
	if state == null:
		return PackedStringArray()
	var player := state.active_player()
	for target in attack_targets(from_region):
		if state.owner_of(target) == StateScript.NEUTRAL and state.can_claim(player, target):
			claims.append(String(target))
	return PackedStringArray(claims)


## Adjacent regions the active seat ALSO owns - where the armies standing in
## `from_region` may march. Sorted. Movement is how a campaign reaches a front
## line at all: retail seats each side deep inside its own territory, so on turn
## one no region on the BFME2 map can legally attack anything.
func movement_targets(from_region: String) -> PackedStringArray:
	var moves := PackedStringArray()
	if world == null or state == null:
		return moves
	var player := state.active_player()
	if state.owner_of(from_region) != player:
		return moves
	if state.armies_in_region(from_region).is_empty():
		return moves
	var found: Array[String] = []
	for neighbour in world.neighbours(from_region):
		if state.owner_of(neighbour) == player:
			found.append(neighbour)
	found.sort()
	return PackedStringArray(found)


## March the active seat's armies one region along the graph. Every army in
## `from_region` moves, in ascending id order, so the order in which the
## destination's command-point cap is reached is reproducible. An army that
## cannot move is REPORTED rather than silently left behind: the strategic layer
## refuses it by name and that name is what the screen shows.
func move_armies(from_region: String, to_region: String) -> Dictionary:
	refusals = PackedStringArray()
	var moved: Array[int] = []
	if state == null:
		refusals.append("no War of the Ring session is running")
		return {"ok": false, "moved": PackedInt32Array(), "refusals": refusals}
	if not Array(movement_targets(from_region)).has(to_region):
		refusals.append("%s cannot march to %s" % [from_region, to_region])
		return {"ok": false, "moved": PackedInt32Array(), "refusals": refusals}
	var player := state.active_player()
	for army_id in state.armies_in_region(from_region):
		if int((state.armies[army_id] as Dictionary).get("owner", StateScript.NEUTRAL)) != player:
			continue
		if state.move_army(int(army_id), to_region):
			moved.append(int(army_id))
		else:
			refusals.append("army %d could not march into %s" % [int(army_id), to_region])
	return {
		"ok": refusals.is_empty() and not moved.is_empty(),
		"moved": PackedInt32Array(moved),
		"refusals": refusals,
	}


## Issue retail's individual army movement command.  Friendly ground moves the
## selected army immediately.  Neutral/enemy ground is staged for the existing
## claim/battle transaction so a right click cannot bypass its rules.
func order_army(army_id: int, to_region: String) -> Dictionary:
	refusals = PackedStringArray()
	if world == null or state == null:
		return {"ok": false, "kind": "", "refusals": PackedStringArray(
			["no War of the Ring session is running"])}
	if not state.armies.has(army_id):
		return {"ok": false, "kind": "", "refusals": PackedStringArray(
			["unknown army %d" % army_id])}
	var army := state.armies[army_id] as Dictionary
	var player := state.active_player()
	if int(army.get("owner", StateScript.NEUTRAL)) != player:
		return {"ok": false, "kind": "", "refusals": PackedStringArray(
			["army %d does not belong to the active seat" % army_id])}
	var from_region := String(army.get("region", ""))
	if not world.are_adjacent(from_region, to_region):
		return {"ok": false, "kind": "", "refusals": PackedStringArray(
			["%s is not adjacent to %s" % [to_region, from_region]])}
	var target_owner := state.owner_of(to_region)
	if target_owner == player:
		if not state.move_army(army_id, to_region):
			return {"ok": false, "kind": "", "refusals": PackedStringArray(
				["army %d could not march to %s" % [army_id, to_region]])}
		return {
			"ok": true, "kind": "move", "army": army_id,
			"from": from_region, "to": to_region, "refusals": PackedStringArray(),
		}
	if String(army.get("kind", "")) != StateScript.ARMY_HERO:
		return {"ok": false, "kind": "", "refusals": PackedStringArray([
			"garrison armies can move only through territories their seat controls; "
			+ "select a Hero army to take neutral or enemy territory"])}
	if target_owner == StateScript.NEUTRAL and not state.can_claim(player, to_region):
		return {"ok": false, "kind": "", "refusals": PackedStringArray(
			["%s cannot be claimed by this Hero army" % to_region])}
	if target_owner != StateScript.NEUTRAL and not state.can_attack(player, to_region):
		return {"ok": false, "kind": "", "refusals": PackedStringArray(
			["%s cannot be attacked by this Hero army" % to_region])}
	selected_region = from_region
	selected_target = to_region
	return {
		"ok": true,
		"kind": "claim" if target_owner == StateScript.NEUTRAL else "attack",
		"army": army_id, "from": from_region, "to": to_region,
		"refusals": PackedStringArray(),
	}


## The campaign's victory standing under its chosen victory type: a pure
## projection of `state.evaluate_victory()`, here so the screen has one seam to
## read it through. `{ok:false, reason}` when nothing is evaluable - a scenario
## with no authored victory types is reported by name, never scored by an
## invented rule.
func victory_status() -> Dictionary:
	if state == null:
		return {
			"ok": false,
			"reason": "no War of the Ring session is running",
			"defeated_players": PackedInt32Array(),
			"defeated_teams": PackedInt32Array(),
			"victorious_team": StateScript.NEUTRAL,
		}
	return state.evaluate_victory()


# =============================================================================
# CONSTRUCTION - the only path from a build plot to a standing structure
# =============================================================================
#
# THE CONTRACT, published in full at `scratchpad/WOTR_BUILD_CONTRACT.md`:
#
#   `load_building_catalogue(roots)`  bind retail's data. Optional - every entry
#       point below binds it lazily from the environment if nobody did.
#   `build_plots(region)`             what the region offers and what stands on
#       it. A pure projection; safe to call every frame.
#   `build_options(region)`           one row per building the ACTIVE seat could
#       consider raising there, each carrying `can_build` and, when false, the
#       one sentence saying why. NOTHING IS HIDDEN: an unaffordable structure is
#       returned with `can_build = false` and its price, because a build ring
#       that silently omits the fortress teaches the player nothing.
#   `commit_build(region, id, plot)`  the ONE mutation. Spends treasure, stands
#       the structure, returns what moved.
#   `commit_demolish(region, plot)`   the inverse.
#   `treasure()` / `income_report()`  what the active seat holds and earns.
#
# The seat is ALWAYS `state.active_player()` and never an argument, for the same
# reason `commit_attack()` takes no attacker: who is building is authoritative
# strategic state, and a caller that could name a different seat could spend a
# treasury the hash does not describe.

## Bind retail's building catalogue. `{ok, reason, ui_path, macros_path,
## buildings}`. Idempotent: a second call with the same roots reloads, which is
## what a caller changing content packs wants.
func load_building_catalogue(pack_roots: Array = []) -> Dictionary:
	_bundle_roots = pack_roots.duplicate()
	var catalogue := BuildingsScript.new()
	var found: Dictionary = catalogue.load_from_roots(pack_roots)
	if bool(found.get("ok", false)):
		buildings = catalogue
		building_catalogue_reason = ""
	else:
		buildings = null
		building_catalogue_reason = String(found.get("reason", ""))
	# THE STATE HOLDS THE RULES, THIS FILE HOLDS THE LOADING. `wotr_state.gd`
	# reads the catalogue through this handle to price and validate a build; it
	# never opens a bundle itself, exactly as it never opens the auto-resolve
	# tables.
	if state != null:
		state.building_catalogue = buildings
	return found


## Bind the catalogue if nothing has yet, and report whether one is bound.
##
## LAZY ON PURPOSE. The strategic screen owns bundle discovery for every other
## bundle and this lane must not require an edit to it to work at all; the
## environment variable `run_game.bat` already sets (`OPENBFME_LIVING_MAP_REGIONS`,
## the directory holding BOTH `living-world-ui.json` and `macros.json`) is enough
## on its own. A caller that has better roots calls `load_building_catalogue()`
## and this becomes a no-op.
func _ensure_building_catalogue() -> bool:
	if buildings != null:
		if state != null and state.building_catalogue == null:
			state.building_catalogue = buildings
		return true
	if not building_catalogue_reason.is_empty():
		# Already tried and failed. Do not re-open files every frame.
		return false
	load_building_catalogue(_bundle_roots)
	return buildings != null


## THE ACTIVE SEAT'S TREASURY. Retail calls it exactly that
## (`STRATEGICHUD:StatsCommandPointsTitle` is the literal string "Treasury").
func treasure(player: int = -1) -> int:
	if state == null:
		return 0
	var seat := player if player >= 0 else state.active_player()
	return state.treasure(seat)


## WHAT THE ACTIVE SEAT WILL EARN at the start of its next turn, broken out by
## source. `{total, fortresses, farms, fertile_regions, rows}`; `rows` is one
## sentence per contributor so a HUD can show the sum and its workings, which is
## what retail's own `STRATEGICHUD:StatsTreasuryIncome` panel does.
func income_report(player: int = -1) -> Dictionary:
	if state == null:
		return {"total": 0, "fortresses": 0, "farms": 0, "fertile_regions": 0,
			"rows": PackedStringArray()}
	_ensure_building_catalogue()
	var seat := player if player >= 0 else state.active_player()
	return state.turn_income(seat)


## THE PLOTS IN A REGION AND WHAT STANDS ON THEM. A pure projection, shaped for
## the two things a screen has to draw: retail's own "used / total" counter
## (`STRATEGICHUD:RegionBuildPlotsHelp`) and one card per foundation.
##
## `plots` is ALWAYS `total` entries long, in plot order, whether or not anything
## stands there - so a caller iterates plots rather than structures and an empty
## foundation is a first-class row it can draw retail's `BuildPlotIcon_*` decal
## on. `owner` is the region's owner, so a screen can show an ENEMY region's
## plots too, which is the shape the defect list asked for ("the building plots
## should always be visible even if you're an enemy").
func build_plots(region_id: String) -> Dictionary:
	if world == null or state == null or not world.has_region(region_id):
		return {"region": region_id, "total": 0, "used": 0, "free": 0,
			"owner": StateScript.NEUTRAL, "plots": []}
	var total := state.plot_count(region_id)
	var standing: Dictionary = {}
	for row in state.structures_in_region(region_id):
		standing[int((row as Dictionary).get("plot", -1))] = row
	var plots: Array[Dictionary] = []
	for index in range(total):
		var record := standing.get(index, {}) as Dictionary
		var catalogue_row: Dictionary = {}
		if not record.is_empty() and buildings != null:
			catalogue_row = buildings.building(String(record.get("building", "")))
		plots.append({
			"plot": index,
			"occupied": not record.is_empty(),
			"building": String(record.get("building", "")),
			"type": String(record.get("type", "")),
			"owner": int(record.get("owner", StateScript.NEUTRAL)),
			"turn": int(record.get("turn", 0)),
			# The authored `LW_*` token this stands for, "" for a built structure.
			"token": String(record.get("token", "")),
			# Retail's own string-table keys for the name and tooltip, resolved by
			# whoever owns the string table - never by this file.
			"display_name_tag": String(catalogue_row.get("display_name_tag", "")),
			"description_tag": String(catalogue_row.get("description_tag", "")),
			"icon": String(catalogue_row.get("icon", "")),
			"button_image": String(catalogue_row.get("button_image", "")),
		})
	return {
		"region": region_id,
		"total": total,
		"used": standing.size(),
		"free": maxi(0, total - standing.size()),
		"owner": state.owner_of(region_id),
		"plots": plots,
	}


## EVERY BUILDING THE ACTIVE SEAT COULD CONSIDER RAISING in `region_id`, sorted
## by building id so the ring is drawn in one order on every machine.
##
## `plot` names the foundation the player clicked; -1 means "the lowest free
## one", which is what the opponent and a scenario use.
##
## THE LIST IS NOT PRE-FILTERED. Every structure retail makes available to this
## seat's faction is returned, each carrying `can_build` and - when that is false
## - `refusal`, the one sentence naming the rule. That is deliberate and it is
## the correction at the heart of this lane: the ring already showed the player
## four real structures with four real prices and nothing happened when they
## clicked, and a ring that instead showed two structures would have been a
## smaller lie rather than none. Now every button either builds or says why not.
##
## Returns `[]` when there is no session, no catalogue, or no seat - and
## `build_reason()` says which.
func build_options(region_id: String, plot: int = -1) -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	if world == null or state == null or not world.has_region(region_id):
		return rows
	if not _ensure_building_catalogue():
		return rows
	var player := state.active_player()
	if player == StateScript.NEUTRAL:
		return rows
	var template := String((state.players[player] as Dictionary).get("template", ""))
	for record in buildings.buildings_for(template):
		var id := String(record.get("id", ""))
		var refusal := state.build_refusal(player, region_id, id, plot)
		rows.append({
			"building": id,
			"type": String(record.get("type", "")),
			"cost": int(record.get("cost", -1)),
			"cost_macro": String(record.get("cost_macro", "")),
			"turns_to_build": int(record.get("turns_to_build", 1)),
			"can_build": refusal == "",
			"refusal": refusal,
			# Retail's own string-table keys. This layer never renders a name.
			"display_name_tag": String(record.get("display_name_tag", "")),
			"description_tag": String(record.get("description_tag", "")),
			"build_title_tag": String(record.get("build_title_tag", "")),
			"build_help_tag": String(record.get("build_help_tag", "")),
			"button_image": String(record.get("button_image", "")),
			"icon": String(record.get("icon", "")),
			# What retail says the structure DOES, read off its own fields.
			"income_per_turn": buildings.income_for_type(String(record.get("type", ""))),
			"defends_territory": bool(record.get("can_defend_territory", false)),
			"recruit_count": (record.get("recruits", []) as Array).size(),
		})
	return rows


## Why nothing can be built at all, or "" when the catalogue is bound. For a
## screen that wants to explain an empty ring rather than draw a blank.
func build_reason() -> String:
	if state == null:
		return "no War of the Ring session is running"
	_ensure_building_catalogue()
	return building_catalogue_reason


## RAISE A STRUCTURE. The one mutation, and the one door the build ring and
## `wotr_ai.gd` both press.
##
## Returns `{ok, refusals, region, plot, building, type, cost, treasury_before,
## treasury_after, hash_after}`. On a refusal `refusals` is never empty and
## NOTHING has moved - `wotr_state.build_structure()` validates the whole
## transaction before it spends a coin.
##
## THE TURN DOES NOT PASS. See `build_structure()` in `wotr_state.gd` for
## retail's own sentence about why (construction, training and movement are all
## decided in one phase).
func commit_build(region_id: String, building_id: String, plot: int = -1) -> Dictionary:
	refusals = PackedStringArray()
	if state == null:
		return _build_refused("no War of the Ring session is running", region_id, building_id)
	if not _ensure_building_catalogue():
		return _build_refused(building_catalogue_reason, region_id, building_id)
	var player := state.active_player()
	if player == StateScript.NEUTRAL:
		return _build_refused("no seat is active", region_id, building_id)
	var built: Dictionary = state.build_structure(player, region_id, building_id, plot)
	if not bool(built.get("ok", false)):
		return _build_refused(String(built.get("refusal", "")), region_id, building_id)
	built["refusals"] = PackedStringArray()
	built["hash_after"] = state.state_hash()
	return built


## TEAR ONE DOWN, freeing its plot - retail's own
## `STRATEGICHUD:DemolishBuildingsChecklistItem`, "Demolish existing structures
## to make build plots available to rebuild new structures."
func commit_demolish(region_id: String, plot: int) -> Dictionary:
	refusals = PackedStringArray()
	if state == null:
		refusals.append("no War of the Ring session is running")
		return {"ok": false, "refusals": refusals, "region": region_id, "plot": plot, "building": ""}
	var razed: Dictionary = state.demolish_structure(state.active_player(), region_id, plot)
	if not bool(razed.get("ok", false)):
		refusals.append(String(razed.get("refusal", "")))
	razed["refusals"] = refusals
	return razed


func _build_refused(reason: String, region_id: String, building_id: String) -> Dictionary:
	refusals.append(reason)
	return {
		"ok": false, "refusals": refusals, "region": region_id, "plot": -1,
		"building": building_id, "type": "", "cost": 0,
		"treasury_before": treasure(), "treasury_after": treasure(), "hash_after": "",
	}


# --- the only path from a selection to a battle ------------------------------

## Bind every region map name in the world to a pack map. The stand-in rule is
## DETERMINISTIC and stated: no `MAP WOR *` map is cooked in any pack, so each
## region's battle is fought on the cooked map at a fixed position in the sorted
## available list, chosen by a digest of the region's own authored map name. The
## same region always draws the same ground, on any machine with the same pack.
##
## `available_map_ids` is the caller's list of maps the tactical layer can boot.
## An empty list binds NOTHING, and `commit_attack()` then refuses by name - a
## battle with no ground is not a battle.
func battlefield_bindings(available_map_ids: Array) -> Dictionary:
	var bindings: Dictionary = {}
	if world == null:
		return bindings
	var sorted_maps: Array[String] = []
	for value in available_map_ids:
		var map_id := String(value).strip_edges()
		if not map_id.is_empty() and not sorted_maps.has(map_id):
			sorted_maps.append(map_id)
	sorted_maps.sort()
	if sorted_maps.is_empty():
		return bindings
	for region_id in world.region_ids:
		var region_map := String(world.region(region_id).get("map_name", ""))
		if region_map.is_empty() or bindings.has(region_map):
			continue
		bindings[region_map] = sorted_maps[_stable_index(region_map, sorted_maps.size())]
	return bindings


## An unsigned index in [0, count) derived from a string. Byte-exact and
## platform-independent: the first four bytes of the SHA-256 of the name, which
## is the same canonical digest discipline the rest of this layer uses. NOT
## `hash()`, whose value is an engine implementation detail.
static func _stable_index(name: String, count: int) -> int:
	if count <= 0:
		return 0
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(name.to_utf8_buffer())
	var digest := context.finish()
	var value := 0
	for index in range(4):
		value = (value << 8) | int(digest[index])
	return value % count


## COMMIT AN ATTACK. The single door between the screen and a battle.
##
## `target_region` is the ONLY selection this takes, and the attacker is
## `state.active_player()` rather than an argument, because who is attacking is
## authoritative strategic state and a caller that could name a different one
## could start a battle the hash does not describe.
##
## Returns `{ok, refusals, commitment, team_roster, gameplay_rules,
## reinforcement_feed, battlefield_map, region_map_name}`. `team_roster` is re-derived from the
## commitment the state actually admitted, so what a caller feeds the simulation
## is provably the record the strategic hash covers.
##
## `requested_battle_type` is the attacker's per-battle pick - `"auto_resolve"`
## or `"rts"` - and it only does anything when the campaign rule is retail's own
## default "Auto Resolve and RTS", which OFFERS both. Under "Auto Resolve" or
## "RTS" the campaign rule wins and the request is ignored. The RESOLVED type is
## what lands in the commitment, so the request itself never reaches anything
## the hash cannot see.
func commit_attack(
	target_region: String, available_map_ids: Array, requested_battle_type: String = ""
) -> Dictionary:
	refusals = PackedStringArray()
	if world == null or state == null:
		return _commit_refused("no War of the Ring session is running")
	if not state.pending_battle.is_empty():
		return _commit_refused("a battle is already in flight in %s" % String(state.pending_battle.get("region", "")))
	if not state.pending_claim.is_empty():
		return _commit_refused("a claim is already in flight in %s" % String(state.pending_claim.get("region", "")))
	var attacker := state.active_player()
	if attacker == StateScript.NEUTRAL:
		return _commit_refused("no seat is active")

	# UNOWNED GROUND TAKES THE CLAIM PATH, and it takes it HERE - inside the one
	# door the screen's buttons and the opponent both already go through - rather
	# than as a second command a caller has to know to call. That placement is the
	# whole reason `wotr_ai.gd` needed no change to start expanding: it commits
	# through `commit_attack()` and resolves through `auto_resolve_pending_battle()`,
	# and both now understand a claim.
	#
	# There is NO BATTLE to configure and none is configured: a neutral region has
	# no owning seat, so no defending faction, no defending armies and no second
	# side for a tactical match. `wotr_battle.gd` says exactly that and refuses; it
	# was right to, and the fix was never to make it invent a defender.
	if state.owner_of(target_region) == StateScript.NEUTRAL:
		var claim: Dictionary = state.build_claim(attacker, target_region)
		if claim.is_empty():
			return _commit_refused(
				("%s is unowned and seat %d cannot claim it: taking neutral ground needs a HERO "
				+ "army standing in an adjacent region this seat owns (retail's own rule - a "
				+ "garrison army cannot take neutral territory on its own)")
				% [target_region, attacker])
		if not state.begin_claim(claim):
			return _commit_refused(
				"the strategic layer refused the claim on %s: %s" % [
					target_region, _last_state_rejection()])
		return {
			"ok": true,
			"refusals": PackedStringArray(),
			# A CLAIM IS NOT A COMMITMENT. The battle fields are returned EMPTY on
			# purpose, so a caller that hands `commitment` to a tactical launcher
			# fails closed on "names no battlefield" instead of booting a match with
			# one side. `claim` is the record that says what actually happened.
			"claim": state.pending_claim.duplicate(true),
			"commitment": {},
			"team_roster": [],
			"gameplay_rules": {},
			"reinforcement_feed": {},
			"battlefield_map": "",
			"region_map_name": String(world.region(target_region).get("map_name", "")),
		}

	var brief := HandoffScript.build_request(world, state, attacker, target_region)
	if brief.is_empty():
		return _commit_refused("seat %d cannot legally attack %s from any region it holds" % [attacker, target_region])
	var bindings := battlefield_bindings(available_map_ids)
	if requested_battle_type == StateScript.BATTLE_TYPE_AUTO_RESOLVE \
			and (autoresolve == null or autoresolve_bindings == null):
		return _commit_refused(
			"this attack asked to be auto-resolved and there is no auto-resolve data: %s"
			% (auto_resolve_reason if not auto_resolve_reason.is_empty()
				else "load_auto_resolve() was never called"))
	var configured: Dictionary = BattleScript.configure(
		brief, FACTION_BINDINGS, bindings, requested_battle_type)
	if not bool(configured.get("ok", false)):
		for reason in configured.get("refusals", PackedStringArray()) as PackedStringArray:
			refusals.append(String(reason))
		return _commit_refused_with_existing()
	var commitment := configured["commitment"] as Dictionary
	var admission := tactical_admission(configured, brief)
	if not bool(admission.get("ok", false)):
		for reason in admission.get("refusals", PackedStringArray()) as PackedStringArray:
			refusals.append(String(reason))
		return _commit_refused_with_existing()
	if not state.begin_battle(commitment):
		return _commit_refused("the strategic layer refused the commitment for %s" % target_region)
	return {
		"ok": true,
		"refusals": PackedStringArray(),
		"commitment": state.pending_battle.duplicate(true),
		# RE-DERIVED from the admitted record, not copied from `configured`.
		"team_roster": tactical_roster(),
		"gameplay_rules": configured["gameplay_rules"],
		"reinforcement_feed": configured["reinforcement_feed"],
		"battlefield_map": String(state.pending_battle.get("battlefield_map", "")),
		"region_map_name": String(state.pending_battle.get("map_name", "")),
	}


## The fail-closed RTS admission decision, kept pure so a future brief that has
## closed all five current tactical gaps can be proven without weakening today's
## handoff (which correctly names every one). Auto-resolve does not consume a
## tactical feed and remains admissible exactly as before. For RTS, every current
## unsupported capability and the complete top-level feed contract are checked
## before `state.begin_battle()` can mutate the strategic hash.
static func tactical_admission(configured: Dictionary, brief: Dictionary) -> Dictionary:
	var empty := PackedStringArray()
	var commitment_value: Variant = configured.get("commitment", {})
	if not (commitment_value is Dictionary):
		return {"ok": false, "refusals": PackedStringArray(
			["RTS tactical admission refused: Battle.configure returned a malformed commitment"]),
			"reinforcement_feed": {}}
	var commitment := commitment_value as Dictionary
	if String(commitment.get("battle_type", "")) != StateScript.BATTLE_TYPE_RTS:
		return {"ok": true, "refusals": empty,
			"reinforcement_feed": configured.get("reinforcement_feed", {})}

	var denied := PackedStringArray()
	if not brief.has("unsupported"):
		denied.append("RTS tactical admission refused: brief is missing the unsupported-gap list")
	else:
		var unsupported_value: Variant = brief["unsupported"]
		if unsupported_value is Array or unsupported_value is PackedStringArray:
			var gap_names: Array[String] = []
			for gap_value in unsupported_value:
				var gap_name := String(gap_value).strip_edges()
				if gap_name.is_empty():
					gap_name = "<empty>"
				gap_names.append(gap_name)
			gap_names.sort()
			for gap_name in gap_names:
				denied.append("RTS tactical admission refused: unsupported tactical gap '%s'" % gap_name)
		else:
			denied.append("RTS tactical admission refused: brief unsupported-gap list is malformed")

	var feed_value: Variant = configured.get("reinforcement_feed", null)
	if not configured.has("reinforcement_feed"):
		denied.append("RTS tactical admission refused: Battle.configure returned no reinforcement_feed")
	elif not (feed_value is Dictionary):
		denied.append("RTS tactical admission refused: reinforcement_feed is malformed")
	else:
		var feed := feed_value as Dictionary
		var malformed := not feed.has("ok") or typeof(feed["ok"]) != TYPE_BOOL \
			or not feed.has("refusals") or typeof(feed["refusals"]) != TYPE_PACKED_STRING_ARRAY \
			or not feed.has("seconds_per_reinforcement") \
			or typeof(feed["seconds_per_reinforcement"]) != TYPE_INT \
			or not feed.has("attacker") or not (feed["attacker"] is Array) \
			or not feed.has("defender") or not (feed["defender"] is Array)
		if malformed:
			denied.append("RTS tactical admission refused: reinforcement_feed is malformed")
		else:
			var feed_reasons: Array[String] = []
			for reason in feed["refusals"] as PackedStringArray:
				feed_reasons.append(String(reason))
			feed_reasons.sort()
			if not bool(feed["ok"]):
				denied.append("RTS tactical admission refused: reinforcement_feed is not ok")
			elif not feed_reasons.is_empty():
				denied.append(
					"RTS tactical admission refused: reinforcement_feed carries refusals despite ok=true")
			for reason in feed_reasons:
				denied.append("reinforcement_feed refusal: %s" % reason)
			if bool(feed["ok"]) and feed_reasons.is_empty() \
					and int(feed["seconds_per_reinforcement"]) < 0:
				denied.append("RTS tactical admission refused: reinforcement_feed is malformed")
	return {
		"ok": denied.is_empty(),
		"refusals": denied,
		"reinforcement_feed": feed_value if denied.is_empty() else {},
	}


## The tactical roster this session authorises: a pure projection of
## `state.pending_battle` and nothing else. Empty when no battle is in flight,
## so a caller cannot configure a match that the strategic layer never opened.
func tactical_roster() -> Array:
	if state == null or state.pending_battle.is_empty():
		return []
	return BattleScript.team_roster_for(state.pending_battle)


## Apply a decided tactical match and close the transaction, then hand the turn
## on. `winner_team` is the tactical simulation's `winner`; -1 is refused by
## `apply_outcome` rather than silently treated as a defender win.
func resolve_battle(winner_team: int) -> Dictionary:
	refusals = PackedStringArray()
	if state == null:
		return {"ok": false, "refusals": PackedStringArray(["no session"]), "winner_player": StateScript.NEUTRAL}
	var outcome: Dictionary = BattleScript.apply_outcome(state, winner_team)
	for reason in outcome.get("refusals", PackedStringArray()) as PackedStringArray:
		refusals.append(String(reason))
	if bool(outcome.get("ok", false)):
		state.advance_turn()
	selected_region = ""
	selected_target = ""
	return outcome


## AUTO-RESOLVE THE BATTLE IN FLIGHT, roll the dice, and write the result back.
##
## Returns `{ok, refusals, outcome, applied, seed}`. `outcome` carries the
## strike-by-strike record the battle screen draws - what each side rolled, what
## modified it, and which numbers are retail's and which are this project's.
##
## THE SEED IS THE COMMITMENT AND NOTHING ELSE. `AutoResolveBattleScript.
## seed_for()` digests `state.pending_battle`, which is inside the strategic
## hash, so two peers that agree on strategic state necessarily roll the same
## dice in the same order. There is no clock, no `randomize()` and no engine RNG
## anywhere on this path, and the auto-resolve runner asserts that by reading
## the file.
##
## It REFUSES rather than falling back when the commitment says this battle is
## not an auto-resolved one, when the data is missing, or when a committed army
## has no units - because each of those, resolved anyway, would produce a real
## strategic result from a configuration error.
func auto_resolve_pending_battle() -> Dictionary:
	refusals = PackedStringArray()
	if state == null:
		return _auto_resolve_refused("no War of the Ring session is running")

	# A CLAIM IN FLIGHT RESOLVES HERE, and it resolves without a die roll.
	#
	# This is the second half of the neutral-ground path `commit_attack()` opens,
	# and it lives on this function rather than a new one for a concrete reason:
	# this is the door the AUTO-RESOLVE button and `wotr_ai.gd` both already press
	# after committing. Giving the claim its own function would have meant editing
	# both callers, and the caller that was NOT edited would silently strand an open
	# transaction on the next turn.
	#
	# There is nothing to auto-resolve because there is nothing to fight: the region
	# is unowned, nothing defends it, and applying the claim IS the resolution. The
	# returned shape is the battle shape so a caller reading `applied.winner_player`
	# and `applied.undecided` needs no new branch, and `outcome.kind` names it a
	# claim so a caller that wants to say "taken" rather than "conquered" can.
	if not state.pending_claim.is_empty():
		return _apply_pending_claim()

	if state.pending_battle.is_empty():
		return _auto_resolve_refused("no battle is in flight")
	if autoresolve == null or autoresolve_bindings == null:
		return _auto_resolve_refused(
			auto_resolve_reason if not auto_resolve_reason.is_empty()
			else "auto-resolve data has not been loaded; call load_auto_resolve() first")
	if not BattleScript.is_auto_resolved(state.pending_battle):
		return _auto_resolve_refused(
			("this battle's commitment says battle_type '%s' with priority '%s', which is a "
			+ "tactical battle; auto-resolving it anyway would decide it by a rule the "
			+ "commitment does not name") % [
				String(state.pending_battle.get("battle_type", "")),
				String(state.pending_battle.get("battle_type_priority", ""))])

	var sides: Dictionary = BattleScript.auto_resolve_sides(state)
	if not bool(sides.get("ok", false)):
		for reason in sides.get("refusals", PackedStringArray()) as PackedStringArray:
			refusals.append(String(reason))
		return _auto_resolve_refused_with_existing()

	var seed_hex := AutoResolveBattleScript.seed_for(state.pending_battle)
	var outcome: Dictionary = AutoResolveBattleScript.resolve(
		autoresolve.rules, sides["attacker"], sides["defender"], seed_hex)
	if not bool(outcome.get("ok", false)):
		for reason in outcome.get("refusals", PackedStringArray()) as PackedStringArray:
			refusals.append(String(reason))
		return _auto_resolve_refused_with_existing()

	var applied: Dictionary = BattleScript.apply_auto_resolve_outcome(state, outcome)
	for reason in applied.get("refusals", PackedStringArray()) as PackedStringArray:
		refusals.append(String(reason))
	# The turn only passes on a battle that actually decided. An UNDECIDED
	# battle already cost both armies their attrition; ending the turn on top of
	# that would also cost the attacker the move, which retail states nothing
	# about and this project is not going to invent.
	if bool(applied.get("ok", false)) and not bool(applied.get("undecided", true)):
		state.advance_turn()
	selected_region = ""
	selected_target = ""
	return {
		"ok": bool(applied.get("ok", false)),
		"refusals": refusals,
		"outcome": outcome,
		"applied": applied,
		"seed": seed_hex,
	}


## Apply the claim in flight and hand the turn on. Shaped like an auto-resolved
## battle's return so every existing caller reads it without a new branch.
##
## THE TURN PASSES ON A CLAIM, exactly as it does on a decided battle, and for the
## same reason: the seat spent its action. It does NOT pass when the claim refuses
## - a claim the command-point cap forbids cost the seat nothing, and taking its
## turn for it would be a penalty retail states nothing about.
func _apply_pending_claim() -> Dictionary:
	var region_id := String(state.pending_claim.get("region", ""))
	var player := int(state.pending_claim.get("player", StateScript.NEUTRAL))
	var applied: Dictionary = state.apply_claim()
	for reason in applied.get("refusals", PackedStringArray()) as PackedStringArray:
		refusals.append(String(reason))
	if not bool(applied.get("ok", false)):
		# The transaction stays OPEN on a refusal, exactly as a failed auto-resolve
		# leaves its commitment open: the caller is told why and can abandon it.
		return _auto_resolve_refused_with_existing()
	state.advance_turn()
	selected_region = ""
	selected_target = ""
	return {
		"ok": true,
		"refusals": refusals,
		"outcome": {
			"ok": true,
			# NAMED after retail's own notice for this event -
			# `APT:LivingWorldRegionTakenNotice` ("%s taken!"), which is a different
			# string from the one a won battle raises.
			"kind": "region_claimed",
			"region": region_id,
			"player": player,
			"army": int(applied.get("army", -1)),
		},
		"applied": {
			"ok": true,
			"claimed": true,
			"undecided": false,
			"region": region_id,
			"winner_player": player,
			"captured": true,
			"armies_lost": PackedInt32Array(),
			"armies_reduced": PackedInt32Array(),
			"refusals": PackedStringArray(),
		},
		# NO SEED, because no dice were rolled. An empty string here is a fact about
		# the resolution, not a missing value.
		"seed": "",
	}


func _auto_resolve_refused(reason: String) -> Dictionary:
	refusals.append(reason)
	return _auto_resolve_refused_with_existing()


func _auto_resolve_refused_with_existing() -> Dictionary:
	return {"ok": false, "refusals": refusals, "outcome": {}, "applied": {}, "seed": ""}


## Abandon a battle that never decided - the player left the tactical match. The
## transaction closes with NO result applied, because there is no result: the
## region does not move and nobody dies. Reported, never invented.
##
## An open CLAIM closes here too. Not a special case bolted on: this is the one
## function every caller already uses to close a transaction it could not finish
## (`wotr_ai.gd` calls it after a refused resolution and then tries the next
## target), and a claim left open would refuse every later commit by name and
## freeze the campaign on the next human turn.
func abandon_battle() -> bool:
	if state == null:
		return false
	if not state.pending_claim.is_empty():
		return state.clear_claim()
	return state.clear_battle()


# --- the opponent -------------------------------------------------------------

## Load retail's living-world AI template. Same shape and same discipline as
## `load_auto_resolve()`: SEPARATE from `begin()`, because a session whose AI
## template is missing is a real and legitimate state that must still start, run
## and be playable. What it must not do is pretend to have retail's taste, and
## `wotr_ai.gd` reports that by name through `decision_provenance()`.
func load_ai_template(pack_roots: Array = []) -> Dictionary:
	var located: Dictionary = ai.locate_and_load(pack_roots)
	ai_template_reason = String(located.get("reason", ""))
	return located


## Whether the seat whose turn it is should be played by the opponent rather than
## waited on. THE SCREEN'S ONE TEST: it decides whether END TURN is the player's
## to press or whether the campaign should run itself forward.
func active_seat_is_ai() -> bool:
	return AiScript.active_seat_is_ai(state)


## PLAY ONE AI SEAT'S TURN. Returns `wotr_ai.gd`'s report - what it considered,
## what it did, the state hash on both sides of the turn, and the player-facing
## lines the screen shows so the turn is VISIBLE rather than an instant jump.
##
## `available_map_ids` is the same list `commit_attack()` takes, for the same
## reason: an AI attack needs a battlefield binding exactly as a human's does.
func run_ai_turn(available_map_ids: Array = []) -> Dictionary:
	if state == null:
		return {"ok": false, "refusals": PackedStringArray(["no War of the Ring session is running"])}
	return ai.take_turn(self, available_map_ids)


## PLAY EVERY CONSECUTIVE AI SEAT until the turn comes back to a human, the
## campaign is decided, or `max_turns` is reached. Returns one report per turn
## taken, in order, so the screen can show them one after another.
##
## `max_turns` IS A HARD BOUND, not a tuning knob. Every seat in a campaign can
## legitimately be an AI (a spectated game is a real setup), and in that case
## there is no human turn to stop at; a bound is the difference between a demo
## that runs and a frame that never returns.
func run_ai_turns(available_map_ids: Array = [], max_turns: int = 16) -> Array[Dictionary]:
	var reports: Array[Dictionary] = []
	if state == null:
		return reports
	for _step in range(max(0, max_turns)):
		if not active_seat_is_ai():
			break
		# A decided campaign stops the loop: turning after a victory would keep
		# playing a war that is over.
		var standing: Dictionary = state.evaluate_victory()
		if bool(standing.get("ok", false)) \
				and int(standing.get("victorious_team", StateScript.NEUTRAL)) != StateScript.NEUTRAL:
			break
		var report: Dictionary = run_ai_turn(available_map_ids)
		reports.append(report)
		# A turn that did not end is a turn that would repeat forever. The AI
		# refusing to move is a real outcome (no maps bound, a battle stuck in
		# flight); repeating it is not.
		if not bool(report.get("ok", false)) \
				or int(report.get("turn_index_after", -1)) == int(report.get("turn_index_before", -2)):
			break
	return reports


# --- surviving the tactical scene change -------------------------------------

## Everything needed to rebuild this session on the other side of a scene change,
## and nothing else. The strategic snapshot rides verbatim, so the battle in
## flight arrives inside the record the hash covers. Presentation state does NOT
## ride: a highlight is per-seat and rebuilding it would be inventing it.
func handoff_payload() -> Dictionary:
	if state == null or world == null:
		return {}
	return {
		"schema": HANDOFF_SCHEMA,
		"schema_version": HANDOFF_SCHEMA_VERSION,
		"document_path": document_path,
		"document_source": document_source,
		"campaign": world.campaign_name,
		"scenario": scenario_name,
		"snapshot": state.snapshot(),
		"input_receipt": input_receipt,
	}


## Production adoption seam. The receipt and every byte identity are verified
## before the legacy low-level adoption seam is allowed to rebuild anything.
func adopt_evidenced_handoff(payload: Dictionary) -> bool:
	refusals = PackedStringArray()
	var handed: Variant = payload.get("input_receipt", null)
	if typeof(handed) != TYPE_DICTIONARY:
		return _adopt_refused("the War of the Ring input receipt is missing")
	if not ReceiptScript.verify(handed as Dictionary, true):
		return _adopt_refused("the War of the Ring input receipt is invalid or its files drifted")
	var receipt_document := (handed as Dictionary).get("document", {}) as Dictionary
	if String(payload.get("document_path", "")) != String(receipt_document.get("path", "")):
		return _adopt_refused("the handoff document path does not match its input receipt")
	if String(payload.get("document_source", "")) != String(receipt_document.get("source", "")):
		return _adopt_refused("the handoff document source does not match its input receipt")
	if not adopt_handoff(payload):
		return false
	input_receipt = ReceiptScript.immutable_copy(handed as Dictionary)
	return true


## Rebuild this session from `payload`. ALL-OR-NOTHING: every part is staged and
## checked before `world`/`state` are written, so a session that reports false is
## left exactly as it was found rather than holding half a restored campaign -
## the same discipline `wotr_state.restore()` keeps, for the same reason.
func adopt_handoff(payload: Dictionary) -> bool:
	refusals = PackedStringArray()
	if payload.is_empty():
		return _adopt_refused("no War of the Ring handoff was recorded")
	if String(payload.get("schema", "")) != HANDOFF_SCHEMA:
		return _adopt_refused("handoff schema is not %s" % HANDOFF_SCHEMA)
	if int(payload.get("schema_version", -1)) != HANDOFF_SCHEMA_VERSION:
		return _adopt_refused("unsupported handoff schema_version %d" % int(payload.get("schema_version", -1)))
	var path := String(payload.get("document_path", ""))
	if path.is_empty() or not FileAccess.file_exists(path):
		return _adopt_refused("the living-world document the session came from is gone: %s" % path)
	var document: Variant = _read_document(path)
	if not (document is Dictionary):
		return _adopt_refused("the living-world document %s no longer parses" % path)
	var snapshot: Variant = payload.get("snapshot", null)
	if typeof(snapshot) != TYPE_PACKED_BYTE_ARRAY or (snapshot as PackedByteArray).is_empty():
		return _adopt_refused("the handoff carries no strategic snapshot")

	var rebuilt := WorldScript.new()
	if not rebuilt.load_from_dict(document as Dictionary, String(payload.get("campaign", ""))):
		return _adopt_refused("the living-world document no longer loads: %s" % str(rebuilt.errors))
	# The seating is a placeholder that `restore()` overwrites in full: players,
	# turn order, ownership, armies and the battle in flight all ride the
	# snapshot. `setup()` exists here only to bind the world.
	var rebuilt_state := StateScript.new()
	var placeholder := _first_template(rebuilt)
	if placeholder.is_empty():
		return _adopt_refused("the living-world document carries no player templates")
	if not rebuilt_state.setup(rebuilt, [{"template": placeholder}]):
		return _adopt_refused("the strategic layer refused the placeholder seating")
	if not rebuilt_state.restore(snapshot as PackedByteArray):
		return _adopt_refused("the strategic snapshot did not restore")

	# COMMIT. Nothing below can fail.
	world = rebuilt
	state = rebuilt_state
	# The armies' own units RODE THE SNAPSHOT and are already correct - they are
	# hashed state. This rebuilds only the lookup table a FUTURE `place_army()`
	# would need, which is a pure function of the two converted bundles and was
	# deliberately not carried in the handoff.
	state.roster_units = _build_roster_units()
	# The building catalogue is rebound for the same reason and on the same
	# terms: it is a pure function of the converted bundles, it was deliberately
	# not carried in the handoff, and the structures it priced RODE THE SNAPSHOT
	# and are already correct.
	_ensure_building_catalogue()
	state.building_catalogue = buildings
	document_path = path
	document_source = String(payload.get("document_source", ""))
	input_receipt = {}
	scenario_name = String(payload.get("scenario", ""))
	selected_region = ""
	selected_target = ""
	hover_region = ""
	return true


func _adopt_refused(reason: String) -> bool:
	refusals.append(reason)
	return false


static func _first_template(source_world: WorldScript) -> String:
	var names: Array[String] = []
	for key in source_world.player_templates.keys():
		names.append(String(key))
	names.sort()
	return names[0] if not names.is_empty() else ""


# --- internals ---------------------------------------------------------------

func _commit_refused(reason: String) -> Dictionary:
	refusals.append(reason)
	return _commit_refused_with_existing()


func _commit_refused_with_existing() -> Dictionary:
	return {
		"ok": false,
		"refusals": refusals,
		"commitment": {},
		"team_roster": [],
		"gameplay_rules": {},
		"reinforcement_feed": {},
		"battlefield_map": "",
		"region_map_name": "",
	}
