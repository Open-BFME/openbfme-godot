extends RefCounted

## WHICH AUTO-RESOLVE BLOCK EACH UNIT USES.
##
## Retail's nine `livingworldautoresolve*.ini` documents declare the blocks -
## 131 armours, 150 weapons, 102 bodies, 9 combat chains, 13 leaderships - and
## say nothing at all about WHICH UNIT USES WHICH. That binding is in the OBJECT
## inis, 502 of them at catalog precedence, and it is converted by
## `openbfme_importer.living_world_autoresolve_bindings` into the bundle this
## file loads.
##
## Without it the strategic layer cannot auto-resolve anything, because a
## living-world army is a list of object templates (`AngmarDarkDunedainHorde`,
## `RohanFrodo`) and retail's combat tables are keyed by block name. This is the
## bridge, and it is DATA rather than a rule: there is no way to derive
## `AutoResolve_GondorSoldierArmor` from `AngmarDarkDunedainHorde` except by
## reading the file that says so.
##
## THE MEASUREMENT THAT SURPRISED THIS LANE, carried in the bundle's own
## `findings` and repeated here because it decides how Angmar plays: Angmar's
## object files bind to GONDOR-named blocks. EA authored Angmar-named armour,
## weapon and body blocks and then never referenced them. So the Angmar-named
## blocks are orphans, and Angmar nevertheless auto-resolves on real, live,
## authored numbers - not on retail's default fallbacks.
##
## A TEMPLATE WITH NO BINDING IS NAMED, NEVER DEFAULTED. Five of the 105
## templates the living-world document fields have no auto-resolve data in any
## object ini, and a unit quietly fighting on `AutoResolve_DefaultWeapon` would
## be indistinguishable on screen from one fighting on its own numbers.

const SCHEMA := "openbfme.living-world-autoresolve-bindings"
const SCHEMA_VERSION := 1

const BUNDLE_ENV := "OPENBFME_LIVING_WORLD_AUTORESOLVE_BINDINGS"
const FILE_NAME := "living-world-autoresolve-bindings.json"
const MANIFEST_MAX_BYTES := 32 * 1024 * 1024

var loaded := false
var source_path := ""
var errors: PackedStringArray = PackedStringArray()
## The whole manifest as the converter wrote it.
var manifest: Dictionary = {}


static func candidate_paths(roots: Array = []) -> Array[Dictionary]:
	var candidates: Array[Dictionary] = []
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		var direct := override if override.ends_with(FILE_NAME) else override.path_join(FILE_NAME)
		candidates.append({"path": direct, "origin": BUNDLE_ENV})
	# The bindings sit beside the auto-resolve tables they bind to, so the
	# auto-resolve override finds both without a second variable being set.
	var beside := OS.get_environment("OPENBFME_LIVING_WORLD_AUTORESOLVE").strip_edges()
	if not beside.is_empty():
		var directory := beside.get_base_dir() if beside.ends_with(".json") else beside
		candidates.append({"path": directory.path_join(FILE_NAME),
			"origin": "beside OPENBFME_LIVING_WORLD_AUTORESOLVE"})
	for root in roots:
		var text := String(root).strip_edges()
		if not text.is_empty():
			candidates.append({"path": text.path_join(FILE_NAME), "origin": "beside the map bundles"})
	return candidates


## `{ok, path, reason}`. A failure NAMES every path tried and the command that
## produces a bundle - never a silent empty table, because an empty binding
## table would make every army unbindable and look exactly like a broken import.
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
		"ok": false,
		"path": "",
		"reason": (
			"NO AUTO-RESOLVE BINDING BUNDLE, so no army can be mapped onto retail's "
			+ "combat tables and nothing can auto-resolve. Looked at: %s. Produce one "
			+ "with: python -m openbfme_importer.living_world_autoresolve_bindings "
			+ "--catalog <catalog>.json --autoresolve <living-world-autoresolve.json> "
			+ "--out <dir>, then point %s at <dir>."
		) % ["; ".join(tried) if not tried.is_empty() else "nowhere", BUNDLE_ENV],
	}


func load_from(path: String) -> bool:
	loaded = false
	manifest = {}
	errors = PackedStringArray()
	source_path = path
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _fail("cannot open %s" % path)
	if file.get_length() > MANIFEST_MAX_BYTES:
		var oversized := file.get_length()
		file.close()
		return _fail("%s is %d bytes, over the %d limit" % [path, oversized, MANIFEST_MAX_BYTES])
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		return _fail("%s is not a JSON object" % path)
	var document: Dictionary = parsed
	if String(document.get("schema", "")) != SCHEMA:
		return _fail("%s carries schema %s, expected %s" % [
			path, String(document.get("schema", "<none>")), SCHEMA])
	if int(document.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return _fail("%s is schema version %d, expected %d" % [
			path, int(document.get("schemaVersion", -1)), SCHEMA_VERSION])
	if not document.has("objects"):
		return _fail("%s has no objects section" % path)
	manifest = document
	loaded = true
	return true


func _fail(reason: String) -> bool:
	errors.append(reason)
	manifest = {}
	loaded = false
	return false


## Object template -> binding record. Empty when nothing is loaded, which is
## what makes `units_for_roster()` report every template unbound BY NAME rather
## than fabricating a force.
func objects() -> Dictionary:
	return manifest.get("objects", {})


## The corrected reference census, carried in the data so a reader cannot repeat
## the miscount that produced it wrong the first time: `dangling` excludes
## commented-out references, `danglingIfCommentedCounted` is what you get if you
## do not, and the difference between them IS the finding.
func census() -> Dictionary:
	return manifest.get("census", {})


func dangling() -> Dictionary:
	return manifest.get("dangling", {})


func orphans() -> Dictionary:
	return manifest.get("orphans", {})


func findings() -> Array:
	return manifest.get("findings", [])


## Which living-world templates are bound and which are not, as the converter
## measured it. `{}` when the converter was run without a living-world document.
func coverage() -> Dictionary:
	return manifest.get("coverage", {})
