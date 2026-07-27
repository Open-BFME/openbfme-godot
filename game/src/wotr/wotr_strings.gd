extends RefCounted

## RETAIL'S STRING TABLE, so the strategic screen stops showing retail's ids.
##
## Every human-readable name in the living world is a KEY, not a name: a region
## carries `displayName = "LW:DisplayNameArnor"`, a territory carries
## `LW:TerritoryEriador`, an army carries `LWA:MenHeroArmy`. The screen showed
## the raw ids (`The_Black_Gate`) because nothing had converted the table.
##
## It is not a CSF. RotWK ships it as plain text - `data/lotr.str`, ~1.2 MB -
## and `openbfme_importer.living_world_strings` turns the strategic namespaces
## of it into the bundle this loads.
##
## THE RULE IT KEEPS: a key with no entry produces NOTHING, and the caller falls
## back to retail's id and says the label is missing. Deriving "The Black Gate"
## from `The_Black_Gate` would be inventing retail text, and retail's own
## spelling disagrees with its ids often enough to prove the point - retail's
## `LW:DisplayNameArnor` reads "Arthedain", and its `Buckland` reads "The North
## Downs". Neither is guessable.

const SCHEMA := "openbfme.living-world-strings"
const SCHEMA_VERSION := 1

const BUNDLE_ENV := "OPENBFME_LIVING_WORLD_STRINGS"
const FILE_NAME := "strings.json"
const MAX_BYTES := 32 * 1024 * 1024

var loaded := false
var source_path := ""
var errors: PackedStringArray = PackedStringArray()
var strings: Dictionary = {}
var totals: Dictionary = {}
## Keys asked for that the table does not carry, in the order first asked.
## Public so the screen can name what it is falling back on.
var missing_keys: Dictionary = {}


static func candidate_paths(roots: Array = []) -> Array[Dictionary]:
	var candidates: Array[Dictionary] = []
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		candidates.append({"path": override, "origin": BUNDLE_ENV})
	for root in roots:
		candidates.append({
			"path": String(root).path_join(FILE_NAME),
			"origin": "beside the region geometry",
		})
	return candidates


## Load from the first candidate that parses. Returns `{ok, path, reason}`.
func locate_and_load(roots: Array = []) -> Dictionary:
	var tried: Array[String] = []
	for candidate in candidate_paths(roots):
		var path := String(candidate["path"])
		tried.append("%s [%s]" % [path, String(candidate["origin"])])
		if not FileAccess.file_exists(path):
			continue
		if load_from(path):
			return {"ok": true, "path": path, "reason": ""}
	return {
		"ok": false, "path": "",
		"reason": (
			"NO STRING TABLE, so regions carry retail's own ids instead of their "
			+ "names. Looked at: %s. Produce one with: python -m "
			+ "openbfme_importer.living_world_strings --catalog <catalog>.json "
			+ "--out <dir>/%s") % ["; ".join(tried) if not tried.is_empty() else "nowhere", FILE_NAME],
	}


func load_from(path: String) -> bool:
	loaded = false
	errors = PackedStringArray()
	strings = {}
	totals = {}
	missing_keys = {}
	source_path = path

	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		errors.append("cannot open %s" % path)
		return false
	if file.get_length() > MAX_BYTES:
		errors.append("%s is %d bytes, over the %d limit" % [path, file.get_length(), MAX_BYTES])
		return false
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		errors.append("%s is not a JSON object" % path)
		return false
	var bundle: Dictionary = parsed
	if String(bundle.get("schema", "")) != SCHEMA:
		errors.append("schema is not %s" % SCHEMA)
		return false
	if int(bundle.get("schemaVersion", -1)) != SCHEMA_VERSION:
		errors.append("unsupported schemaVersion %d" % int(bundle.get("schemaVersion", -1)))
		return false
	strings = bundle.get("strings", {}) as Dictionary
	totals = bundle.get("totals", {}) as Dictionary
	loaded = true
	return true


## The English text for a key, or "" when the table does not carry it. A miss is
## RECORDED, so the screen can report how many labels it is standing in for
## rather than quietly showing ids.
func text(key: String) -> String:
	if key.is_empty():
		return ""
	if not loaded:
		return ""
	var value: Variant = strings.get(key, null)
	if value == null:
		missing_keys[key] = true
		return ""
	return String(value)


func has(key: String) -> bool:
	return loaded and strings.has(key)


func count() -> int:
	return strings.size()
