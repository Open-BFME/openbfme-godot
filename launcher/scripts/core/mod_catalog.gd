## Lane MODS-3: the rules of the player's one-click mods, with no network and no disk (scripts/core/mod_install.gd does the fetching and
## the writing; tests/mod_catalog_test.gd feeds this recorded, made-up manifests). The layout is tools/mod/aio_package.py's, which is the
## All In One BFME Launcher's (github.com/MarcellVokk/BfmeFoundationProject, WorkshopKit BfmeWorkshopSyncManager.Sync / AddFiles):
##   * layers in priority order: L1 the enhancements (Type 2), L2 the package, L3 the patches it depends on (Type 0) and the other
##     dependencies; L4, the base game ("original-*"), is the verified retail install and is never downloaded;
##   * a dependency is "guid" or "guid:version" (the version is not asked for: the tool takes the service's current one);
##   * a file is taken when its Language is ALL, empty or lists EN; a file whose destination an earlier layer already has is skipped;
##   * Type 0 / 2 / 4 files go to the game folder (OpenBFME's install part, mounted with --mod-install), Type 1 files to the mod's own
##     folder (the -mod part);
##   * the game program and code injected into it (game.dat, game2.dat, file.dat, lotrbfme2ep1.exe, *.dll, *.asi) are engine edits: not
##     taken, listed (a package with any is "needs engine features"); other programs (*.exe, *.msi) are tools, left out.
## Two cases the tool lays out and the launcher leaves out (and lists), because OpenBFME cannot use them yet: a Type 3 (maps) layer, and a
## layer for another game (a BFME2 patch belongs in the BFME2 folder, which is not mounted from a mod).
extends RefCounted

const AioInstall := preload("res://scripts/core/aio_install.gd")

const GAME := 2                      # The Rise of the Witch-king (the service's Game 2)
const TYPES := [0, 1, 2]             # the catalogue: patches / data mods (0), -mod mods (1), enhancements (2)
const LANGUAGE := "EN"
# WorkshopKit ConfigUtils.IgnoredGameFiles (the AIO launcher never installs these) and a file a port never reads
const IGNORED := ["_zzlotr.big", "cs01.vp6", "nlc_logo.vp6", "te_logo.vp6", "ealogo.vp6", "newlinelogo.vp6", "mod.txt"]
const ENGINE_EDIT_EXT := [".dll", ".asi"]
const ENGINE_EDIT_NAMES := ["game.dat", "game2.dat", "file.dat", "lotrbfme2ep1.exe"]
const TOOL_EXT := [".exe", ".msi"]
const SKIP_EXT := [".pdf", ".wps"]
const EMPTY_MD5 := "d41d8cd98f00b204e9800998ecf8427e"
const READY := "ready"
const NEEDS := "needs engine features"
const MAX_FILES := 20000             # files of one entry
const SCHEMA := "openbfme.aio-package 1"


static func type_name(t: int) -> String:
	return {0: "patch", 1: "mod", 2: "enhancement", 3: "maps", 4: "snapshot"}.get(t, "type %d" % t)


static func dependency_guid(dep: String) -> String:
	return dep.get_slice(":", 0)


static func base_game(guid: String) -> bool:
	return guid.begins_with("original-") or guid.begins_with("exp-original-")


## A package id the launcher uses as a folder name (user://mods/<guid>): letters, digits, '.', '_', '-', not starting with a dot.
static func safe_guid(guid: String) -> bool:
	return RegEx.create_from_string("^[A-Za-z0-9_-][A-Za-z0-9._-]{0,79}$").search(guid) != null


static func _whole(v) -> bool:
	return typeof(v) == TYPE_FLOAT and v == floor(v) and absf(v) < 9.0e15


static func _text(v) -> String:
	return v if typeof(v) == TYPE_STRING else ""


## the JSON value of `body`, or null when it is not JSON (quietly: JSON.parse_string prints an engine error for every bad answer)
static func parse_json(body: PackedByteArray) -> Variant:
	var j := JSON.new()
	return j.data if j.parse(body.get_string_from_utf8()) == OK else null


## One page of GET /api/workshop/query (a JSON array of BfmeWorkshopEntryPreview). {ok, error, items: [{guid, name, version, author,
## type, game, size}]}; the base games are not items (OpenBFME has verified them).
static func parse_query(body: PackedByteArray) -> Dictionary:
	var d = parse_json(body)
	if typeof(d) != TYPE_ARRAY:
		return {"ok": false, "error": "the service's mod list is not a JSON array", "items": []}
	var items := []
	for p in d:
		if typeof(p) != TYPE_DICTIONARY or typeof(p.get("Guid")) != TYPE_STRING or typeof(p.get("Name")) != TYPE_STRING \
				or not _whole(p.get("Type")) or not _whole(p.get("Game")):
			return {"ok": false, "error": "the service's mod list has an entry OpenBFME does not recognise", "items": []}
		if base_game(p.Guid):
			continue
		if not safe_guid(p.Guid):
			return {"ok": false, "error": "the service's mod list has a package id OpenBFME does not accept: '%s'" % p.Guid.left(80).c_escape(), "items": []}
		var size = p.get("Size", 0)
		items.append({"guid": p.Guid, "name": p.Name.strip_edges(), "version": _text(p.get("Version")).strip_edges(),
			"author": _text(p.get("Author")).strip_edges(), "type": int(p.Type), "game": int(p.Game), "size": int(size) if _whole(size) and size >= 0 else 0})
	return {"ok": true, "error": "", "items": items}


## Checks one entry of GET /api/workshop/download?guid=<guid> (a BfmeWorkshopEntry) and returns its plain form. Every file must have a
## plain relative path, an MD5, a whole size (0: the service does not know it) and the URL <files origin>/<uploader>/<md5> (the service's
## content-addressed store; the uploader is not always the entry's Owner). {ok, error, entry: {guid, name, version, author, type, game,
## deps: [dependency strings], files: [{name, rel, md5, size, url, language}]}}
static func check_entry(m: Variant, guid: String, files_origin: String) -> Dictionary:
	var what := "The service's package %s" % guid
	var bad := func(why: String) -> Dictionary: return {"ok": false, "error": "%s %s" % [what, why], "entry": {}}
	if typeof(m) != TYPE_DICTIONARY:
		return bad.call("is not a package OpenBFME knows (not a JSON object).")
	if m.get("Guid") != dependency_guid(guid):
		return bad.call("answered with the package '%s'." % str(m.get("Guid")).left(80))
	if typeof(m.get("Name")) != TYPE_STRING or not _whole(m.get("Game")) or not _whole(m.get("Type")):
		return bad.call("has no name, game or type.")
	if typeof(m.get("Files")) != TYPE_ARRAY or m.Files.size() > MAX_FILES:
		return bad.call("has no file list.")
	var deps := []
	var raw = m.get("Dependencies", [])
	if raw == null:
		raw = []
	if typeof(raw) != TYPE_ARRAY:
		return bad.call("has a dependency list OpenBFME does not recognise.")
	for dep in raw:
		if typeof(dep) != TYPE_STRING or not safe_guid(dependency_guid(dep)):
			return bad.call("depends on '%s', which is not a package id OpenBFME accepts." % str(dep).left(80).c_escape())
		deps.append(dep)
	var hex := RegEx.create_from_string("^[0-9a-f]{32}$")
	var where := RegEx.create_from_string("^" + files_origin.replace(".", "\\.") + "/[A-Za-z0-9-]{1,64}/([0-9a-f]{32})$")
	var files := []
	for f in m.Files:
		if typeof(f) != TYPE_DICTIONARY or typeof(f.get("Name")) != TYPE_STRING or typeof(f.get("Md5")) != TYPE_STRING \
				or typeof(f.get("Url")) != TYPE_STRING:
			return bad.call("has a file entry OpenBFME does not recognise.")
		var lang = f.get("Language", "")
		if lang == null:
			lang = ""
		if typeof(lang) != TYPE_STRING:
			return bad.call("lists %s with a language OpenBFME does not recognise." % f.Name.left(120).c_escape())
		if not _whole(f.get("Size")) or f.Size < 0:
			return bad.call("lists %s with the size %s." % [f.Name.left(120).c_escape(), JSON.stringify(f.get("Size"))])
		var rel := AioInstall.safe_relative(f.Name)
		if rel == "":
			return bad.call("lists a file name that is not a plain relative path: '%s'." % f.Name.left(120).c_escape())
		if hex.search(f.Md5) == null:
			return bad.call("lists %s with a malformed MD5." % rel)
		var at := where.search(f.Url)
		if at == null or at.get_string(1) != f.Md5:
			return bad.call("lists %s at '%s', not in the service's file store (%s/<uploader>/<md5>)." % [rel, f.Url.left(160), files_origin])
		files.append({"name": f.Name, "rel": rel, "md5": f.Md5, "size": int(f.Size), "url": f.Url, "language": lang})
	return {"ok": true, "error": "", "entry": {"guid": m.Guid, "name": m.Name.strip_edges(), "version": _text(m.get("Version")).strip_edges(),
		"author": _text(m.get("Author")).strip_edges(), "type": int(m.Type), "game": int(m.Game), "deps": deps, "files": files}}


## The dependencies of `entry` and of the chosen enhancements, in the order the AIO launcher adds them (the entry's, breadth first, then
## each enhancement and its own). `fetched`: guid -> checked entry. {deps: [entries], missing: [guids not in fetched yet]}: the caller
## fetches the missing ones and asks again.
static func closure(entry: Dictionary, enhancements: Array, fetched: Dictionary) -> Dictionary:
	var deps := []
	var missing := PackedStringArray()
	var seen := {entry.guid: true}
	var todo: Array = entry.deps.duplicate()
	for e in [null] + enhancements:
		if e != null:
			todo = [e.guid] + e.deps
		while not todo.is_empty():
			var g := dependency_guid(String(todo.pop_front()))
			if seen.has(g) or base_game(g):
				continue
			seen[g] = true
			if not fetched.has(g):
				missing.append(g)
				continue
			deps.append(fetched[g])
			todo.append_array(fetched[g].deps)
	return {"deps": deps, "missing": missing}


## An enhancement fits a package when it depends on it ("guid" or "guid:version") or on one of its dependencies (WorkshopKit's
## BfmeWorkshopEnhancementIncompatibleException check).
static func fits(enhancement: Dictionary, entry: Dictionary, deps: Array) -> bool:
	for d in enhancement.deps:
		for p in [entry] + deps:
			if d == p.guid or d == "%s:%s" % [p.guid, p.version]:
				return true
	return false


## [first, rest]: the layers L1 + L2 and L3 in priority order (tools/mod/aio_package.py layers)
static func layers(entry: Dictionary, deps: Array) -> Array:
	var enhancements := deps.filter(func(d): return d.type == 2)
	var patches := deps.filter(func(d): return d.type == 0)
	var others := deps.filter(func(d): return d.type not in [0, 2])
	if entry.type == 2:
		# an enhancement alone: it sits over the package it extends
		return [[entry] + enhancements, others + patches]
	return [enhancements + [entry], patches + others]


static func part_of(e: Dictionary) -> String:
	return {1: "mod", 3: "maps"}.get(int(e.type), "install")


## What the layers lay out. {files: [{part, rel, md5, size, url, layer}], engine_edits: ["<layer>: <file>"], left_out: ["<layer>: <file>
## (why)"]} in layer order. Destinations are compared without case, as the AIO launcher does.
static func plan(first: Array, rest: Array) -> Dictionary:
	var files := []
	var engine_edits := PackedStringArray()
	var engine_md5s := PackedStringArray()   # lane MODS-3: the game's ModEngineFeatures profiles are keyed by these
	var left_out := PackedStringArray()
	var taken := {}
	for e in first + rest:
		for f in e.files:
			var langs: PackedStringArray = String(f.language).split(" ", false)
			if not (f.language in ["", "ALL"] or LANGUAGE in langs):
				continue
			var part := part_of(e)
			var key: String = part + "/" + String(f.rel).to_lower()
			if taken.has(key):
				continue   # AddFiles: an earlier layer already has this destination
			taken[key] = true
			var low: String = String(f.rel).get_file().to_lower()
			var show := "%s: %s" % [e.name, String(f.name).replace("/", "\\")]
			if low in IGNORED or SKIP_EXT.any(func(x): return low.ends_with(x)):
				left_out.append(show)
			elif ENGINE_EDIT_EXT.any(func(x): return low.ends_with(x)) or low in ENGINE_EDIT_NAMES:
				engine_edits.append(show)
				engine_md5s.append(String(f.md5).to_lower())
			elif TOOL_EXT.any(func(x): return low.ends_with(x)):
				left_out.append(show + " (a tool)")
			elif int(e.game) != GAME:
				left_out.append(show + " (for another game: not mounted from a mod yet)")
			elif part == "maps":
				left_out.append(show + " (a map: maps from the service are not supported yet)")
			else:
				files.append({"part": part, "rel": f.rel, "md5": f.md5, "size": int(f.size), "url": f.url, "layer": e.name})
	return {"files": files, "engine_edits": engine_edits, "engine_md5s": engine_md5s, "left_out": left_out}


static func status_of(p: Dictionary) -> String:
	return READY if p.engine_edits.is_empty() else NEEDS


## The record user://mods/<guid>/openbfme-aio.json (tools/mod/aio_package.py's schema, plus the enabled enhancements)
static func summary(entry: Dictionary, first: Array, rest: Array, p: Dictionary, enhancements: Array) -> Dictionary:
	var parts := {}
	var bytes := 0
	for f in p.files:
		parts[f.part] = true
		bytes += int(f.size)
	var names := parts.keys()
	names.sort()
	return {"schema": SCHEMA, "guid": entry.guid, "name": entry.name, "version": entry.version, "type": entry.type,
		"layers": (first + rest).map(func(e): return {"name": e.name, "version": e.version, "type": e.type, "guid": e.guid}),
		"parts": names, "files": p.files.size(), "bytes": bytes, "engine_edits": Array(p.engine_edits),
		"engine_md5s": Array(p.get("engine_md5s", PackedStringArray())), "left_out": Array(p.left_out),
		"enhancements": enhancements.map(func(e): return e.guid)}


## Resolves one package with these enhancements (all already fetched). {ok, error, missing, entry, deps, first, rest, plan, status}
static func resolve(guid: String, enhancement_guids: Array, fetched: Dictionary) -> Dictionary:
	if not fetched.has(guid):
		return {"ok": false, "error": "", "missing": PackedStringArray([guid])}
	var entry: Dictionary = fetched[guid]
	if entry.game != GAME:
		return {"ok": false, "error": "%s is a package for another game, not for The Rise of the Witch-king" % entry.name, "missing": PackedStringArray()}
	if entry.type not in TYPES:
		return {"ok": false, "error": "%s is a %s: only patches, mods and enhancements can be installed" % [entry.name, type_name(entry.type)], "missing": PackedStringArray()}
	var enh := []
	var missing := PackedStringArray()
	for g in enhancement_guids:
		if not fetched.has(g):
			missing.append(g)
		else:
			enh.append(fetched[g])
	if not missing.is_empty():
		return {"ok": false, "error": "", "missing": missing}
	var c := closure(entry, enh, fetched)
	if not c.missing.is_empty():
		return {"ok": false, "error": "", "missing": c.missing}
	var own: Array = closure(entry, [], fetched).deps
	for e in enh:
		if e.type != 2 or e.game != GAME:
			return {"ok": false, "error": "%s is not an enhancement for The Rise of the Witch-king" % e.name, "missing": PackedStringArray()}
		if not fits(e, entry, own):
			return {"ok": false, "error": "%s does not extend %s" % [e.name, entry.name], "missing": PackedStringArray()}
	var l := layers(entry, c.deps)
	var p := plan(l[0], l[1])
	return {"ok": true, "error": "", "missing": PackedStringArray(), "entry": entry, "deps": c.deps, "first": l[0], "rest": l[1], "plan": p,
		"status": status_of(p), "enhancements": enh}


## The catalogue the Mods screen shows: the packages (Type 0 / 1) in the service's order, each with its enhancements (the Type 2 entries
## that depend on it), and the enhancements that extend none of them (the original game's, or a package that is not listed). `items`:
## parse_query items; `fetched`: their checked entries and dependencies; `problems`: guid -> why its entry was refused.
## {packages: [row], loose: [row]}; a row: {guid, name, version, author, type, size, status, engine_edits, left_out, error, enhancements}
static func group(items: Array, fetched: Dictionary, problems: Dictionary) -> Dictionary:
	var rows := {}
	var packages := []
	var loose := []
	for it in items:
		if it.game != GAME or it.type not in TYPES:
			continue
		var row := {"guid": it.guid, "name": it.name, "version": it.version, "author": it.author, "type": it.type, "size": it.size,
			"status": "", "engine_edits": PackedStringArray(), "left_out": PackedStringArray(), "error": problems.get(it.guid, ""), "enhancements": [],
			"extends": PackedStringArray()}
		if row.error == "":
			var r := resolve(it.guid, [], fetched)
			if r.ok:
				row.status = r.status
				row.engine_edits = r.plan.engine_edits
				row.left_out = r.plan.left_out
				row.bytes = r.plan.files.reduce(func(n, f): return n + int(f.size), 0)
			else:
				var why := PackedStringArray()
				for g in Array(r.missing).slice(0, 3):
					why.append(problems.get(g, "%s was not read" % g))
				row.error = r.error if r.error != "" else "a package it needs could not be read: " + "; ".join(why)
		rows[it.guid] = row
		if it.type != 2:
			packages.append(row)
	for it in items:
		if not rows.has(it.guid) or it.type != 2:
			continue
		var row: Dictionary = rows[it.guid]
		var parents := PackedStringArray()
		if fetched.has(it.guid):
			for d in fetched[it.guid].deps:
				var g := dependency_guid(d)
				if rows.has(g) and rows[g].type != 2 and not g in parents:
					parents.append(g)
		row.extends = parents
		for g in parents:
			rows[g].enhancements.append(row)
		if parents.is_empty():
			loose.append(row)
	return {"packages": packages, "loose": loose}


## The game's arguments for an installed mod (godot/scripts/release/release.gd: --mod-install= the install-folder mod, --mod= the -mod
## folder; both absolute, after the "--" that starts the game's own arguments)
static func game_args(install_dir: String, has_install: bool, mod_dir: String, has_mod: bool, engine_md5s := PackedStringArray()) -> PackedStringArray:
	var out := PackedStringArray()
	if has_install:
		out.append("--mod-install=" + install_dir)
	if has_mod:
		out.append("--mod=" + mod_dir)
	for m in engine_md5s:   # lane MODS-3: the game turns on the engine features of these modified game binaries (Common/ModEngineFeatures.h)
		out.append("--mod-engine=" + m)
	return out


## `path` is `base` or below it (both absolute; ".." resolved first)
static func inside(base: String, path: String) -> bool:
	var b := base.simplify_path().trim_suffix("/")
	var p := path.simplify_path()
	return b != "" and (p == b or p.begins_with(b + "/"))
