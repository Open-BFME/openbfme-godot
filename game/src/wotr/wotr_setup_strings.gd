extends RefCounted

## RETAIL'S SHELL STRING TABLE for the GAME SETUP screen.
##
## `wotr_strings.gd` loads the STRATEGIC MAP's namespaces (`LW:`, `LWA:`,
## `LWScenario:`, `RULE:`, `VALUE:`, `STRATEGICHUD:`, `CONTROLBAR:LW_`). The
## GAME SETUP screen needs a different set entirely - every heading, column
## title, faction name, colour name and button caption on it lives in `APT:`,
## `Apt:`, `SIDE:`, `INI:Faction`, `Color:`, `GUI:*AI` and `ToolTip:` - and none
## of those is in that bundle. `openbfme_importer.wotr_setup_strings` converts
## them into the bundle this loads, out of the SAME `data/lotr.str` with the
## SAME parser.
##
## THE RULE IT KEEPS, verbatim from its sibling: A KEY WITH NO ENTRY PRODUCES
## NOTHING. `text()` returns "" and records the miss, and the screen shows
## retail's key and lists it as unresolved. It never derives "Game Setup" from
## `APT:GameSetup`, because retail's own spellings disagree with its ids often
## enough to make that a guess - `APT:Side` reads "Army", `SIDE:Wild` reads
## "Goblins", and neither is recoverable from the key.
##
## CASE IS LOAD-BEARING. Retail writes `Apt:Scenario` with a lowercase tail and
## there is no `APT:Scenario` block at all. Nothing here folds case.

const SCHEMA := "openbfme.wotr-setup-strings"
const SCHEMA_VERSION := 1

const BUNDLE_ENV := "OPENBFME_WOTR_SETUP_STRINGS"
const FILE_NAME := "wotr-setup-strings.json"
const MAX_BYTES := 32 * 1024 * 1024

var loaded := false
var source_path := ""
var errors: PackedStringArray = PackedStringArray()
var strings: Dictionary = {}
var totals: Dictionary = {}
## Keys asked for that the table does not carry, in the order first asked.
## Public so the screen can NAME every label it is standing in for.
var missing_keys: Dictionary = {}


static func candidate_paths(roots: Array = []) -> Array[Dictionary]:
	var candidates: Array[Dictionary] = []
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		# The environment may name the directory or the file; both are accepted so
		# one setting cannot be "right but spelled the other way".
		var direct := override if override.ends_with(FILE_NAME) else override.path_join(FILE_NAME)
		candidates.append({"path": direct, "origin": BUNDLE_ENV})
	for root in roots:
		candidates.append({
			"path": String(root).path_join(FILE_NAME),
			"origin": "beside the region geometry",
		})
	return candidates


## Load from the first candidate that parses. Returns `{ok, path, reason}`; a
## failure names every path tried and the command that produces a bundle.
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
			"NO GAME SETUP STRING TABLE, so this screen shows retail's own keys "
			+ "instead of its headings, faction names and button captions. Looked "
			+ "at: %s. Produce one with: python -m "
			+ "openbfme_importer.wotr_setup_strings --catalog <catalog>.json --out "
			+ "<dir>/%s") % [
				"; ".join(tried) if not tried.is_empty() else "nowhere", FILE_NAME],
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


## Retail's English text for a key, or "" when the table does not carry it. A
## miss is RECORDED so the screen can report what it is standing in for.
func text(key: String) -> String:
	if key.is_empty() or not loaded:
		if not key.is_empty():
			missing_keys[key] = true
		return ""
	var value: Variant = strings.get(key, null)
	if value == null:
		missing_keys[key] = true
		return ""
	return String(value)


## Retail's text, or the KEY ITSELF when the table has no entry. The key is the
## honest stand-in: it is visibly not a label, and it names exactly what is
## missing so a report can be made from the screen.
func label(key: String) -> String:
	var value := text(key)
	return value if not value.is_empty() else key


func has(key: String) -> bool:
	return loaded and strings.has(key)


func count() -> int:
	return strings.size()
