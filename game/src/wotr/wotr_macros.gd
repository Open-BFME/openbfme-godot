extends RefCounted

## RETAIL'S `#define` TABLE from `data/ini/gamedata.ini`, so a region bonus
## retail authored as a MACRO shows retail's number instead of a symbol.
##
## Retail writes `FertileTerritoryBonus = FERTILE_TERRITORY_BONUS` on eight
## regions, and the living-world importer carries that through symbolically -
## correctly, because the value is not in the file it read. It is one file away:
## `gamedata.ini` says `#define FERTILE_TERRITORY_BONUS 500`, which is why
## retail's own panel reads "+500 Treasure" over Mordor.
##
## THE RULE IT KEEPS: a macro this table does not carry, or one whose body is an
## expression rather than a number (retail writes `#DIVIDE( 1.0, 0.95 )` in
## places), resolves to NOTHING and the panel prints the macro's name and says it
## is unresolved. It never evaluates and never guesses.

const SCHEMA := "openbfme.living-world-macros"
const SCHEMA_VERSION := 1

const BUNDLE_ENV := "OPENBFME_LIVING_WORLD_MACROS"
const FILE_NAME := "macros.json"
const MAX_BYTES := 32 * 1024 * 1024

var loaded := false
var source_path := ""
var defines: Dictionary = {}
var totals: Dictionary = {}


static func candidate_paths(roots: Array = []) -> Array[String]:
	var candidates: Array[String] = []
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		candidates.append(override)
	for root in roots:
		candidates.append(String(root).path_join(FILE_NAME))
	return candidates


func locate_and_load(roots: Array = []) -> Dictionary:
	var tried: Array[String] = []
	for path in candidate_paths(roots):
		tried.append(path)
		if FileAccess.file_exists(path) and load_from(path):
			return {"ok": true, "path": path, "reason": ""}
	return {
		"ok": false, "path": "",
		"reason": (
			"NO gamedata #define TABLE, so a region bonus retail authored as a "
			+ "macro shows the macro's name rather than its number. Looked at: %s. "
			+ "Produce one with: python -m openbfme_importer.living_world_macros "
			+ "--catalog <catalog>.json --out <dir>/%s") % [
				"; ".join(tried) if not tried.is_empty() else "nowhere", FILE_NAME],
	}


func load_from(path: String) -> bool:
	loaded = false
	defines = {}
	totals = {}
	source_path = path
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return false
	if file.get_length() > MAX_BYTES:
		return false
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		return false
	var bundle: Dictionary = parsed
	if String(bundle.get("schema", "")) != SCHEMA:
		return false
	if int(bundle.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return false
	defines = bundle.get("defines", {}) as Dictionary
	totals = bundle.get("totals", {}) as Dictionary
	loaded = true
	return true


## `{ok, value, raw}`. `ok` is false when the macro is unknown OR when retail's
## body is not a plain number - both cases the caller must show as unresolved.
func resolve(name: String) -> Dictionary:
	if not loaded or name.is_empty() or not defines.has(name):
		return {"ok": false, "value": 0.0, "raw": ""}
	var record := defines[name] as Dictionary
	if not bool(record.get("numeric", false)):
		return {"ok": false, "value": 0.0, "raw": String(record.get("raw", ""))}
	return {"ok": true, "value": float(record.get("value", 0.0)), "raw": String(record.get("raw", ""))}
