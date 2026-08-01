class_name SageScriptVocabulary
extends RefCounted

## The SAGE map-script / Lua action and condition vocabulary, plus the ONLY
## sanctioned way to turn a decoded script record into a vocabulary entry.
##
## ONE VOCABULARY, TWO FRONT ENDS
## ------------------------------
## SAGE map scripts (WorldBuilder .map / .scb script chunks) and SAGE Lua
## (`ExecuteAction("NAME", ...)` / `EvaluateCondition("NAME", ...)`) do not have
## two vocabularies - they have one. The Lua entry points are thin wrappers over
## the same engine action/condition table the map script interpreter runs. This
## class is therefore the single table for both; a Lua binding should resolve
## through `action_by_name()` exactly as the map interpreter does, and the same
## handler in SageScriptDispatch serves both callers.
##
## THE RENUMBERING HAZARD (read before adding numeric ids)
## -------------------------------------------------------
## Every decoded ScriptAction / Condition record carries BOTH a numeric
## `contentType` and an `internalName` string (sage_scb.py
## `_parse_script_content`). It is tempting to key dispatch off `contentType`.
## Do not.
##
##   * BFME2 renumbered the action and condition enums relative to C&C Generals.
##     A numeric id is only meaningful together with the game it came from, and
##     an id table borrowed from another SAGE title will silently dispatch the
##     wrong handler.
##   * Older map versions do not carry usable InternalName strings, so the
##     numeric id is sometimes the only identity available.
##
## The resolution policy that follows from those two facts:
##
##   1. If the record carries an InternalName, that name is authoritative.
##      An unrecognised name is an ERROR (`ERR_UNKNOWN_NAME`), never a silent
##      skip - the caller records a gap.
##   2. If the record carries no InternalName, resolution requires an id map
##      registered for that exact (game, map_version, kind) triple. No id map
##      ships with this file, because no numeric BFME2 action/condition table
##      could be sourced. With none registered, resolution fails with
##      `ERR_NO_ID_MAP`. It does NOT fall back to another game's table and it
##      does NOT guess.
##   3. If both an InternalName and a registered id map are present and they
##      disagree, that is `ERR_ID_NAME_MISMATCH` - a loud signal that the id map
##      belongs to a different game or version.
##
## `register_id_map()` exists so a future measured table (extracted from the
## shipped executable or from a corpus of maps that carry both fields) can be
## plugged in without touching this file. Register it against the game and map
## version it was measured from, and nothing else.

const ActionTable := preload("res://src/script/script_action_table.gd")
const ConditionTable := preload("res://src/script/script_condition_table.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")

enum Kind {
	ACTION,
	CONDITION,
}

## Games this vocabulary can be asked about. UNKNOWN is the honest default for
## a decoded payload whose origin the caller has not established.
enum Game {
	UNKNOWN,
	BFME1,
	BFME2,
	BFME2_ROTWK,
	GENERALS_ZH,
	OTHER_SAGE,
}

## Resolution outcomes. Anything other than OK must be surfaced, not swallowed.
enum Resolution {
	OK,
	ERR_UNKNOWN_NAME,
	ERR_NO_ID_MAP,
	ERR_UNKNOWN_ID,
	ERR_ID_NAME_MISMATCH,
	ERR_NO_IDENTITY,
}

## Signature-confidence values stored in the generated tables.
const CONFIDENCE_SOURCED := "S"
const CONFIDENCE_UNCERTAIN := "U"

## GAMES field values that indicate the entry is expected to exist in BFME2 or
## RotWK. "SHARED" means "in the reference's common table", which is a superset
## of BFME2 - see the note on estimated coverage in `coverage()`.
const BFME2_GAME_SETS := ["SHARED", "BFME2ROTWK", "BFME2ROTWK_PLUS"]

## Independently measured size of the real BFME2 action / condition surface,
## used only to report how much of the game this table plausibly covers. The
## reference's common table is a cross-title superset, so `total_known` will
## exceed these numbers; that gap is reported, not hidden.
const ESTIMATED_BFME2_ACTIONS := 474
const ESTIMATED_BFME2_CONDITIONS := 154

static var _actions: Dictionary = {}
static var _conditions: Dictionary = {}
static var _built := false

## "game:version:kind" -> {int id: String canonical name}. Ships empty.
static var _id_maps: Dictionary = {}


static func _build() -> void:
	if _built:
		return
	_built = true
	_actions = _parse_rows(ActionTable.ROWS, Kind.ACTION)
	_conditions = _parse_rows(ConditionTable.ROWS, Kind.CONDITION)


static func _parse_rows(rows: Array, kind: int) -> Dictionary:
	var out := {}
	for row: Variant in rows:
		var fields := String(row).split("|", true)
		if fields.size() != 5:
			push_error("SageScriptVocabulary: malformed table row '%s'" % str(row))
			continue
		var name := fields[0]
		var params: Array[String] = []
		if fields[1] != "-":
			for token in fields[1].split(",", false):
				params.append(token)
		out[name] = {
			"name": name,
			"kind": kind,
			"params": params,
			"category": "" if fields[2] == "-" else fields[2],
			"games": fields[3],
			"confidence": fields[4],
			"implemented": false,
		}
	return out


# --- Table access ---------------------------------------------------------


static func actions() -> Dictionary:
	_build()
	return _actions


static func conditions() -> Dictionary:
	_build()
	return _conditions


static func table_for(kind: int) -> Dictionary:
	return actions() if kind == Kind.ACTION else conditions()


static func action_by_name(name: String) -> Dictionary:
	_build()
	return _actions.get(name, {})


static func condition_by_name(name: String) -> Dictionary:
	_build()
	return _conditions.get(name, {})


static func entry_by_name(kind: int, name: String) -> Dictionary:
	return table_for(kind).get(name, {})


static func has_entry(kind: int, name: String) -> bool:
	return table_for(kind).has(name)


static func kind_label(kind: int) -> String:
	return "action" if kind == Kind.ACTION else "condition"


static func expected_in_bfme2(entry: Dictionary) -> bool:
	return BFME2_GAME_SETS.has(String(entry.get("games", "")))


# --- Numeric id maps (see the renumbering note at the top) -----------------


static func _id_map_key(game: int, map_version: int, kind: int) -> String:
	return "%d:%d:%d" % [game, map_version, kind]


static func register_id_map(game: int, map_version: int, kind: int, ids: Dictionary) -> void:
	## Registers a MEASURED numeric id table for exactly one (game, version,
	## kind). Never register a table measured from a different SAGE title: the
	## enums were renumbered and the ids do not carry across.
	_build()
	var table := table_for(kind)
	var checked := {}
	for id: Variant in ids:
		var name := String(ids[id])
		if not table.has(name):
			push_error(
				"SageScriptVocabulary.register_id_map: id %s maps to unknown %s '%s'"
				% [str(id), kind_label(kind), name]
			)
			continue
		checked[int(id)] = name
	_id_maps[_id_map_key(game, map_version, kind)] = checked


static func clear_id_maps() -> void:
	_id_maps = {}


static func has_id_map(game: int, map_version: int, kind: int) -> bool:
	return _id_maps.has(_id_map_key(game, map_version, kind))


static func registered_id_map_count() -> int:
	return _id_maps.size()


# --- Resolution -----------------------------------------------------------


static func resolve(
	kind: int,
	internal_name: String,
	content_type: int,
	game: int = Game.UNKNOWN,
	map_version: int = 0
) -> Dictionary:
	## Turns one decoded record's identity into a vocabulary entry.
	##
	## Returns {"status": Resolution, "entry": Dictionary, "name": String,
	##          "detail": String}. `entry` is empty unless status == OK.
	_build()
	var table := table_for(kind)
	var id_map: Dictionary = _id_maps.get(_id_map_key(game, map_version, kind), {})
	var has_map := _id_maps.has(_id_map_key(game, map_version, kind))
	var name := internal_name.strip_edges()

	if name != "":
		if not table.has(name):
			return _fail(
				Resolution.ERR_UNKNOWN_NAME,
				name,
				"%s '%s' is not in the sourced vocabulary" % [kind_label(kind), name]
			)
		if has_map and id_map.has(content_type) and String(id_map[content_type]) != name:
			return _fail(
				Resolution.ERR_ID_NAME_MISMATCH,
				name,
				(
					"id %d maps to '%s' but the record names '%s'; the id map is "
					+ "for a different game or map version"
				) % [content_type, String(id_map[content_type]), name]
			)
		return {"status": Resolution.OK, "entry": table[name], "name": name, "detail": ""}

	# No InternalName: the numeric id is the only identity available.
	if not has_map:
		return _fail(
			Resolution.ERR_NO_ID_MAP,
			"",
			(
				"record carries no InternalName and no numeric id map is registered "
				+ "for game=%d map_version=%d %s; refusing to guess (BFME2 renumbered "
				+ "the enums, so another title's ids would dispatch the wrong handler)"
			) % [game, map_version, kind_label(kind)]
		)
	if not id_map.has(content_type):
		return _fail(
			Resolution.ERR_UNKNOWN_ID,
			"",
			"id %d is absent from the registered map for game=%d map_version=%d %s"
			% [content_type, game, map_version, kind_label(kind)]
		)
	var mapped := String(id_map[content_type])
	if not table.has(mapped):
		return _fail(
			Resolution.ERR_UNKNOWN_NAME,
			mapped,
			"id %d maps to '%s', which is not in the sourced vocabulary"
			% [content_type, mapped]
		)
	return {"status": Resolution.OK, "entry": table[mapped], "name": mapped, "detail": ""}


static func _fail(status: int, name: String, detail: String) -> Dictionary:
	return {"status": status, "entry": {}, "name": name, "detail": detail}


static func resolution_label(status: int) -> String:
	match status:
		Resolution.OK:
			return "ok"
		Resolution.ERR_UNKNOWN_NAME:
			return "unknown-name"
		Resolution.ERR_NO_ID_MAP:
			return "no-id-map"
		Resolution.ERR_UNKNOWN_ID:
			return "unknown-id"
		Resolution.ERR_ID_NAME_MISMATCH:
			return "id-name-mismatch"
		Resolution.ERR_NO_IDENTITY:
			return "no-identity"
	return "unknown-status-%d" % status


# --- Coverage reporting ---------------------------------------------------


static func coverage(implemented_actions: Array, implemented_conditions: Array) -> Dictionary:
	## Coverage numbers for a set of implemented names.
	##
	## total_known     entries transcribed into this table for the given kind
	##                 (all SAGE titles the reference covers)
	## bfme2_known     entries the reference places in a BFME2-reachable section
	##                 (its common table is a cross-title SUPERSET of BFME2, so
	##                 this over-counts and is labelled as an upper bound)
	## total_estimated independently measured size of the real BFME2 surface
	_build()
	return {
		"actions": _coverage_for(Kind.ACTION, implemented_actions, ESTIMATED_BFME2_ACTIONS),
		"conditions": _coverage_for(
			Kind.CONDITION, implemented_conditions, ESTIMATED_BFME2_CONDITIONS
		),
	}


static func _coverage_for(kind: int, implemented: Array, estimated: int) -> Dictionary:
	var table := table_for(kind)
	var known := table.size()
	var bfme2_known := 0
	for entry: Variant in table.values():
		if expected_in_bfme2(entry):
			bfme2_known += 1
	var counted := 0
	var unknown: Array[String] = []
	for name: Variant in implemented:
		if table.has(String(name)):
			counted += 1
		else:
			unknown.append(String(name))
	return {
		"implemented": counted,
		"implemented_not_in_table": unknown,
		"total_known": known,
		"bfme2_known_upper_bound": bfme2_known,
		"total_estimated": estimated,
	}
