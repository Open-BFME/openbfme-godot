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


## Load `document` and seat a session. Fails closed and NAMES the reason: a
## session that half-started would put a plausible map on screen.
func begin(document: Dictionary, campaign: String, scenario: String, seats: Array) -> bool:
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
	state = StateScript.new()
	if not state.setup(world, seats):
		refusals.append("the strategic layer refused the seating")
		world = null
		state = null
		return false
	if not state.apply_ownership_sets(scenario):
		refusals.append("scenario '%s' has no ownership this campaign can apply" % scenario)
		world = null
		state = null
		return false
	scenario_name = scenario
	selected_region = ""
	selected_target = ""
	hover_region = ""
	return true


## The scenarios this campaign can actually start: the campaign's own, seating at
## least `seat_count`, with an authored ownership set per seat. Sorted.
func startable_scenarios(seat_count: int) -> PackedStringArray:
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


## Regions the active seat may attack FROM `from_region`: adjacent, not theirs,
## and legal by the strategic layer's own `can_attack`. Sorted, because the order
## reaches what the screen offers.
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
		if not state.can_attack(player, neighbour):
			continue
		found.append(neighbour)
	found.sort()
	return PackedStringArray(found)


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
## battlefield_map, region_map_name}`. `team_roster` is re-derived from the
## commitment the state actually admitted, so what a caller feeds the simulation
## is provably the record the strategic hash covers.
func commit_attack(target_region: String, available_map_ids: Array) -> Dictionary:
	refusals = PackedStringArray()
	if world == null or state == null:
		return _commit_refused("no War of the Ring session is running")
	if not state.pending_battle.is_empty():
		return _commit_refused("a battle is already in flight in %s" % String(state.pending_battle.get("region", "")))
	var attacker := state.active_player()
	if attacker == StateScript.NEUTRAL:
		return _commit_refused("no seat is active")
	var brief := HandoffScript.build_request(world, state, attacker, target_region)
	if brief.is_empty():
		return _commit_refused("seat %d cannot legally attack %s from any region it holds" % [attacker, target_region])
	var bindings := battlefield_bindings(available_map_ids)
	var configured: Dictionary = BattleScript.configure(brief, FACTION_BINDINGS, bindings)
	if not bool(configured.get("ok", false)):
		for reason in configured.get("refusals", PackedStringArray()) as PackedStringArray:
			refusals.append(String(reason))
		return _commit_refused_with_existing()
	var commitment := configured["commitment"] as Dictionary
	if not state.begin_battle(commitment):
		return _commit_refused("the strategic layer refused the commitment for %s" % target_region)
	return {
		"ok": true,
		"refusals": PackedStringArray(),
		"commitment": state.pending_battle.duplicate(true),
		# RE-DERIVED from the admitted record, not copied from `configured`.
		"team_roster": tactical_roster(),
		"gameplay_rules": configured["gameplay_rules"],
		"battlefield_map": String(state.pending_battle.get("battlefield_map", "")),
		"region_map_name": String(state.pending_battle.get("map_name", "")),
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


## Abandon a battle that never decided - the player left the tactical match. The
## transaction closes with NO result applied, because there is no result: the
## region does not move and nobody dies. Reported, never invented.
func abandon_battle() -> bool:
	if state == null:
		return false
	return state.clear_battle()


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
	}


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
	document_path = path
	document_source = String(payload.get("document_source", ""))
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
		"battlefield_map": "",
		"region_map_name": "",
	}
