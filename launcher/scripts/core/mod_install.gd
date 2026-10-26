## Lane MODS-3: the player's one-click mods, from the All In One BFME Launcher's workshop service (the one scripts/core/aio_install.gd
## downloads the base games from; docs/AIO.md, docs/MODS.md). The rules of the layout are scripts/core/mod_catalog.gd's; this file asks
## the service and writes the disk:
##   * the catalogue: GET <service>/api/workshop/query?keyword=&game=2&type=<0|1|2>&sortMode=0&page=<n> (pages from 0 until an empty
##     list), then GET <service>/api/workshop/download?guid=<guid> for every package and what it depends on, with the headers the AIO
##     launcher sends (AuthAccountUuid: unauthenticated, AuthAccountPassword: empty). Only when the player asks for the list; kept for
##     CACHE_SECONDS in user://mods/catalog.json;
##   * an install: the package's layers into user://mods/<guid>/install (mounted with --mod-install) and user://mods/<guid>/mod (mounted
##     with --mod), every file through <name>.part (resumed with Range: Pause / Resume), its MD5 checked, then renamed; a file that is
##     no longer part of the layout (an older version, an enhancement switched off) is removed afterwards; then
##     user://mods/<guid>/openbfme-aio.json (the tool's record "openbfme.aio-package 1" plus the enabled enhancements). Nothing is ever
##     written outside user://mods/<guid>/ (every destination is checked) and the game folders are never touched;
##   * consent: nothing is downloaded before the player agreed once (the window's question names the source, the package and its size;
##     the command line's --mod-consent); the agreement is kept in state.cfg ([mods] consented) and logged with its text;
##   * the active mod: user://mods/active.cfg ([mods] active = <guid> or "" for the original game, enhancements = [<guid>...]); Play
##     passes its folders to the game (active_args, called by Updater.game_arguments);
##   * uninstall: user://mods/<guid>/ and nothing else.
## Unlike the base games there are no pinned tables: a mod is the service's word, checked file by file against the MD5 the service lists.
extends RefCounted

const ModCatalog := preload("res://scripts/core/mod_catalog.gd")
const AioInstall := preload("res://scripts/core/aio_install.gd")
const Log := preload("res://scripts/core/launcher_log.gd")

const CACHE := "catalog.json"
const ACTIVE := "active.cfg"
const RECORD := "openbfme-aio.json"
const CACHE_SECONDS := 1800          # "cached briefly": the list is asked for again after half an hour, or on Refresh
const MAX_JSON := 8 * 1024 * 1024    # one page of the list, one package's file list
const MAX_PAGES := 40
const MAX_ENTRIES := 400             # packages read for one catalogue (the list and what it depends on)
const MAX_UNKNOWN := 16 * 1024 * 1024 * 1024   # a file the service lists without a size (Size 0) may have at most this many bytes
const SOURCE := "bfmeladder.com, the workshop of the All In One BFME Launcher (the BFME Foundation Project and the Patch 2.22 team)"
const REMEDY := "Try again later, or start the mod with the All In One BFME Launcher (%s)." % AioInstall.PAGE

var updater  # Updater (scripts/core/updater.gd): its state, HTTP client, network policy, progress callback and data folder
var aio      # AioInstall: the service's origins (its test origin in tests)
var dir := ""  # user://mods
var stop_request := ""  # "pause" or "cancel", set from the UI thread (with updater.http.cancelled)
var test_stop_after := ""  # tests: "<part>/<file>:<pause|cancel>" asks for a stop right after that file was checked and kept


func _init(u, a) -> void:
	updater = u
	aio = a
	dir = u.data_dir.path_join("mods")


static func _fail(kind: String, error: String) -> Dictionary:
	return {"ok": false, "kind": kind, "error": error}


func consented() -> bool:
	return updater.get_state("mods", "consented", false) == true


## What the player agrees to before the first mod download (the window's question; logged with --mod-consent too)
func consent_text(name: String, bytes: int) -> String:
	return ("Mods are downloaded from %s. They are made by their own authors, not by EA or OpenBFME, and OpenBFME does not check what " +
		"they contain: it checks that every file is the one the service lists (its MD5). %s is about %s. They go into OpenBFME's own " +
		"folder; your game folders are never changed. OpenBFME sends that service only its name and version; its privacy policy " +
		"(bfmeladder.com/privacy) applies.") % [SOURCE, name, size_text(bytes)]


static func size_text(bytes: int) -> String:
	if bytes >= 1000000000:
		return "%.1f GB" % (bytes / 1e9)
	return "%d MB" % maxi(1, int(round(bytes / 1e6)))


func _report(text: String, done := -1, total := -1) -> void:
	updater._report(text, done, total)


## From the UI thread: "pause" (keep the partial file: the next install resumes it) or "cancel" (remove the partial file).
func request_stop(how: String) -> void:
	stop_request = how
	updater.http.cancelled = true


## runs `job` with the service's two hosts allowed (only then, as for the base games)
func _net(job: Callable) -> Dictionary:
	stop_request = ""
	updater.http.cancelled = false
	updater.policy.extra_hosts = AioInstall.HOSTS.duplicate()
	var r: Dictionary = job.call()
	updater.policy.extra_hosts = []
	updater.http.cancelled = false
	return r


## {ok, kind, error, data}: one JSON answer of the service
func _get_json(url: String, what: String) -> Dictionary:
	var m: Dictionary = updater.http.get_small(url, MAX_JSON, "application/json", "",
		PackedStringArray(["AuthAccountUuid: unauthenticated", "AuthAccountPassword: "]))
	if not m.ok and stop_request != "":
		return _fail(AioInstall.STOPPED, "stopped")
	if not m.ok:
		return _fail(AioInstall.NETWORK, "%s could not be fetched: %s. %s" % [what, m.error, REMEDY])
	if m.code != 200:
		return _fail(AioInstall.REJECTED if m.code < 500 and m.code != 429 else AioInstall.NETWORK, "The service answered HTTP %d for %s. %s" % [m.code, what, REMEDY])
	return {"ok": true, "kind": "", "error": "", "data": ModCatalog.parse_json(m.body), "body": m.body}


## {ok, kind, error, entry}: one package's checked entry
func _fetch_entry(guid: String) -> Dictionary:
	var g: Dictionary = _get_json("%s/api/workshop/download?guid=%s" % [aio.service, guid.uri_encode()], "the file list of %s" % guid)
	if not g.ok:
		return g
	var c := ModCatalog.check_entry(g.data, guid, aio.files_origin)
	if not c.ok:
		return _fail(AioInstall.REJECTED, c.error)
	return {"ok": true, "kind": "", "error": "", "entry": c.entry}


## Fetches what resolve() reports missing until nothing is (a refused package fails the whole resolve)
func _resolve(guid: String, enhancements: Array, fetched: Dictionary) -> Dictionary:
	for i in MAX_ENTRIES:
		var r := ModCatalog.resolve(guid, enhancements, fetched)
		if r.ok or r.missing.is_empty():
			return r if r.ok else _fail(AioInstall.REFUSED, r.error)
		for g in r.missing:
			if stop_request != "":
				return _fail(AioInstall.STOPPED, "stopped")
			_report("Asking %s for the files of %s" % [aio.service.trim_prefix("https://"), g])
			var e := _fetch_entry(g)
			if not e.ok:
				return e
			fetched[g] = e.entry
	return _fail(AioInstall.REJECTED, "%s depends on more than %d packages. %s" % [guid, MAX_ENTRIES, REMEDY])


# ---- the catalogue ----------------------------------------------------------------------------------------------------------------------

## The cached catalogue when it is younger than CACHE_SECONDS and from this service ({} otherwise)
func cached() -> Dictionary:
	if not FileAccess.file_exists(dir.path_join(CACHE)):
		return {}
	var d = ModCatalog.parse_json(FileAccess.get_file_as_bytes(dir.path_join(CACHE)))
	if typeof(d) != TYPE_DICTIONARY or d.get("service") != aio.service or typeof(d.get("time")) != TYPE_FLOAT:
		return {}
	if Time.get_unix_time_from_system() - d.time > CACHE_SECONDS or typeof(d.get("items")) != TYPE_ARRAY or typeof(d.get("fetched")) != TYPE_DICTIONARY:
		return {}
	for it in d.items:
		for k in ["type", "game", "size"]:
			it[k] = int(it[k])
	for g in d.fetched:
		_ints(d.fetched[g])
	return d


## JSON gives numbers back as floats: the entry's whole numbers as ints again
static func _ints(e: Dictionary) -> void:
	e.type = int(e.type)
	e.game = int(e.game)
	for f in e.files:
		f.size = int(f.size)


## {ok, kind, error, view (ModCatalog.group), time, from_cache}. Blocking (the window runs it on its worker thread).
func catalogue(force := false) -> Dictionary:
	if not force:
		var c := cached()
		if not c.is_empty():
			return {"ok": true, "kind": "", "error": "", "view": ModCatalog.group(c.items, c.fetched, c.get("problems", {})), "time": c.time, "from_cache": true}
	return _net(_catalogue)


func _catalogue() -> Dictionary:
	var items := []
	var seen := {}
	for t in ModCatalog.TYPES:
		for page in MAX_PAGES + 1:
			if page == MAX_PAGES:
				return _fail(AioInstall.REJECTED, "The service's list of %ss has more than %d pages. %s" % [ModCatalog.type_name(t), MAX_PAGES, REMEDY])
			_report("Reading the list of %ss (page %d)" % [ModCatalog.type_name(t), page + 1])
			var url := "%s/api/workshop/query?keyword=&game=%d&type=%d&sortMode=0&page=%d" % [aio.service, ModCatalog.GAME, t, page]
			var g := _get_json(url, "the list of mods")
			if not g.ok:
				return g
			var q := ModCatalog.parse_query(g.body)
			if not q.ok:
				return _fail(AioInstall.REJECTED, "%s. %s" % [q.error, REMEDY])
			if (g.data as Array).is_empty():
				break
			for it in q.items:
				if not seen.has(it.guid):
					seen[it.guid] = true
					items.append(it)
	var fetched := {}
	var problems := {}
	var todo := PackedStringArray(items.map(func(it): return it.guid))
	while not todo.is_empty():
		if fetched.size() + problems.size() + todo.size() > MAX_ENTRIES:
			return _fail(AioInstall.REJECTED, "The service lists more than %d packages. %s" % [MAX_ENTRIES, REMEDY])
		for g in todo:
			if stop_request != "":
				return _fail(AioInstall.STOPPED, "stopped")
			_report("Reading package %d of %d" % [fetched.size() + problems.size() + 1, fetched.size() + problems.size() + todo.size()])
			var e := _fetch_entry(g)
			if e.ok:
				fetched[g] = e.entry
			elif e.kind == AioInstall.REJECTED:
				problems[g] = e.error   # shown on that package's row; the others still list
			else:
				return e
		todo = PackedStringArray()
		for g in fetched:
			for m in ModCatalog.closure(fetched[g], [], fetched).missing:
				if not problems.has(m) and not m in todo:
					todo.append(m)
	var now := Time.get_unix_time_from_system()
	DirAccess.make_dir_recursive_absolute(dir)
	_write_json(dir.path_join(CACHE), {"service": aio.service, "time": now, "items": items, "fetched": fetched, "problems": problems})
	Log.line("LAUNCHER MODS the list has %d packages (%d read, %d refused)" % [items.size(), fetched.size(), problems.size()])
	return {"ok": true, "kind": "", "error": "", "view": ModCatalog.group(items, fetched, problems), "time": now, "from_cache": false}


static func _write_json(path: String, value: Variant) -> bool:
	var f := FileAccess.open(path + ".tmp", FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(JSON.stringify(value, " ") + "\n")
	f.close()
	return DirAccess.rename_absolute(path + ".tmp", path) == OK


# ---- installed mods, the active one -------------------------------------------------------------------------------------------------------

## user://mods/<guid>/openbfme-aio.json, or {} when that package is not (completely) installed
static func record_in(mods_dir: String, guid: String) -> Dictionary:
	var path := mods_dir.path_join(guid).path_join(RECORD)
	if not ModCatalog.safe_guid(guid) or not FileAccess.file_exists(path):
		return {}
	var d = ModCatalog.parse_json(FileAccess.get_file_as_bytes(path))
	if typeof(d) != TYPE_DICTIONARY or d.get("schema") != ModCatalog.SCHEMA or d.get("guid") != guid or typeof(d.get("parts")) != TYPE_ARRAY:
		return {}
	return d


func record(guid: String) -> Dictionary:
	return record_in(dir, guid)


## a download of this package was started and not finished (its folder is there, its record is not)
func partial(guid: String) -> bool:
	return ModCatalog.safe_guid(guid) and DirAccess.dir_exists_absolute(dir.path_join(guid)) and record(guid).is_empty()


## guid -> name (the cached list's, else the guid) of every download that was started and not finished
func partials() -> Dictionary:
	var out := {}
	var d := DirAccess.open(dir)
	if d == null:
		return out
	var c := cached()
	for g in d.get_directories():
		if partial(g):
			out[g] = c.get("fetched", {}).get(g, {}).get("name", g)
	return out


## every installed package's record, by name
func installed() -> Array:
	var out := []
	var d := DirAccess.open(dir)
	if d == null:
		return out
	for g in d.get_directories():
		var r := record(g)
		if not r.is_empty():
			out.append(r)
	out.sort_custom(func(a, b): return String(a.name).naturalnocasecmp_to(String(b.name)) < 0)
	return out


## {guid, enhancements}: the player's pick (guid "" = the original game)
static func active_in(mods_dir: String) -> Dictionary:
	var c := ConfigFile.new()
	if c.load(mods_dir.path_join(ACTIVE)) != OK:
		return {"guid": "", "enhancements": []}
	var g = c.get_value("mods", "active", "")
	var e = c.get_value("mods", "enhancements", [])
	return {"guid": g if typeof(g) == TYPE_STRING else "", "enhancements": Array(e).filter(func(x): return typeof(x) == TYPE_STRING) if typeof(e) in [TYPE_ARRAY, TYPE_PACKED_STRING_ARRAY] else []}


func active() -> Dictionary:
	return active_in(dir)


## Picks the mod Play starts ("" = the original game) and its enhancements. "" or why not.
func set_active(guid: String, enhancements: Array = []) -> String:
	if guid != "" and record(guid).is_empty():
		return "%s is not installed" % guid
	if guid == "":
		enhancements = []
	var err := DirAccess.make_dir_recursive_absolute(dir)
	if err != OK and not DirAccess.dir_exists_absolute(dir):
		return "cannot create %s (%s)" % [dir, error_string(err)]
	var c := ConfigFile.new()
	c.set_value("mods", "active", guid)
	c.set_value("mods", "enhancements", enhancements.duplicate())
	if c.save(dir.path_join(ACTIVE)) != OK:
		return "cannot write %s" % dir.path_join(ACTIVE)
	var what := "the original game (no mod)"
	if guid != "":
		what = guid if enhancements.is_empty() else "%s with %s" % [guid, ", ".join(PackedStringArray(enhancements))]
	Log.line("LAUNCHER MODS Play starts " + what)
	return ""


## The game's arguments for the active mod (Updater.game_arguments). {ok, error, args, name}: no mod gives no arguments; a picked mod that
## is not installed, misses a part, or whose installed enhancements are not the picked ones is an error (never a start without it).
static func active_args(mods_dir: String) -> Dictionary:
	var a := active_in(mods_dir)
	if a.guid == "":
		return {"ok": true, "error": "", "args": PackedStringArray(), "name": ""}
	var r := record_in(mods_dir, a.guid)
	if r.is_empty():
		return {"ok": false, "error": "The mod picked in Mods (%s) is not installed any more. Pick another one, or none, in Mods." % a.guid, "args": PackedStringArray()}
	var want := PackedStringArray(a.enhancements)
	var have := PackedStringArray(r.get("enhancements", []))
	want.sort()
	have.sort()
	if want != have:
		return {"ok": false, "error": "The enhancements picked for %s are not the installed ones. Press Install in Mods to add or remove them." % r.name, "args": PackedStringArray()}
	var base := mods_dir.path_join(a.guid)
	for p in r.parts:
		if p in ["install", "mod"] and not DirAccess.dir_exists_absolute(base.path_join(p)):
			return {"ok": false, "error": "%s is missing its files (%s). Install it again in Mods." % [r.name, p], "args": PackedStringArray()}
	var args := ModCatalog.game_args(base.path_join("install"), "install" in r.parts, base.path_join("mod"), "mod" in r.parts,
		PackedStringArray(r.get("engine_md5s", [])))
	return {"ok": true, "error": "", "args": args, "name": "%s %s" % [r.name, r.version]}


# ---- install, uninstall ------------------------------------------------------------------------------------------------------------------

static func _is_link(path: String) -> bool:
	var d := DirAccess.open(path.get_base_dir())
	return d != null and d.is_link(path.get_file())


## Downloads `guid` with these enhancements into user://mods/<guid>/. Blocking (the window runs it on its worker thread; pause / cancel
## through request_stop). {ok, kind, error, summary, downloaded, kept, removed}
func install(guid: String, enhancements: Array, consent: bool) -> Dictionary:
	if not consent:
		return _fail(AioInstall.REFUSED, "the player has not agreed to download mods from bfmeladder.com")
	for g in [guid] + enhancements:
		if typeof(g) != TYPE_STRING or not ModCatalog.safe_guid(g) or ModCatalog.base_game(g):
			return _fail(AioInstall.REFUSED, "'%s' is not a package OpenBFME can install" % str(g).left(80).c_escape())
	var err := DirAccess.make_dir_recursive_absolute(dir)
	if err != OK and not DirAccess.dir_exists_absolute(dir):
		return _fail(AioInstall.REFUSED, "cannot create %s (%s)" % [dir, error_string(err)])
	if _is_link(dir) or _is_link(dir.path_join(guid)):
		return _fail(AioInstall.REFUSED, "%s is a link: OpenBFME writes mods only into its own folder" % dir.path_join(guid))
	if not consented():
		updater.set_state("mods", "consented", true)
	return _net(_install.bind(guid, enhancements))


func _stopped(part: String) -> Dictionary:
	if stop_request == "cancel":
		if part != "":
			DirAccess.remove_absolute(part)
		return _fail(AioInstall.STOPPED, "cancelled; the files already checked are kept, the one in progress was removed")
	return _fail(AioInstall.STOPPED, "paused; it resumes where it stopped")


func _install(guid: String, enhancements: Array) -> Dictionary:
	var fetched := {}
	var r := _resolve(guid, enhancements, fetched)
	if not r.ok:
		return _stopped("") if r.kind == AioInstall.STOPPED else r
	var base := dir.path_join(guid)
	var files: Array = r.plan.files
	var total := 0
	var need := 0
	var unknown := 0
	for f in files:
		var dest := base.path_join(f.part).path_join(f.rel)
		if not ModCatalog.inside(base, dest):
			return _fail(AioInstall.REJECTED, "%s would be written outside %s: nothing was downloaded" % [f.rel, base])
		total += f.size
		if f.size == 0 and f.md5 != ModCatalog.EMPTY_MD5:
			unknown += 1
		elif not FileAccess.file_exists(dest):
			var part := dest + ".part"
			need += f.size - (FileAccess.open(part, FileAccess.READ).get_length() if FileAccess.file_exists(part) else 0)
	var space := DirAccess.open(dir).get_space_left() if DirAccess.open(dir) != null else 0
	if space <= 0:
		return _fail(AioInstall.REFUSED, "the free space of %s cannot be read" % dir)
	if need > space:
		return _fail(AioInstall.REFUSED, "%s has %.1f GB free; %s needs %.1f GB more" % [dir, space / 1e9, r.entry.name, need / 1e9])
	var title: String = r.entry.name
	Log.line("LAUNCHER MODS consent given: " + consent_text("%s %s" % [title, r.entry.version], total))
	Log.line("LAUNCHER MODS %s %s (%s): %d files, %.2f GB listed (%d without a size), %.2f GB to download, %d engine edits not taken" %
		[title, r.entry.version, r.status, files.size(), total / 1e9, unknown, need / 1e9, r.plan.engine_edits.size()])
	# the layout changes from here on: the record goes first, so a stopped download is never taken for an installed mod
	DirAccess.make_dir_recursive_absolute(base)
	if FileAccess.file_exists(base.path_join(RECORD)):
		DirAccess.remove_absolute(base.path_join(RECORD))
	var done := 0
	var downloaded := 0
	var kept := 0
	var keep := {}
	for i in files.size():
		var f: Dictionary = files[i]
		if stop_request != "":
			return _stopped("")
		var dest: String = base.path_join(f.part).path_join(f.rel)
		keep[dest] = true
		var show := "%s: file %d of %d, %s" % [title, i + 1, files.size(), f.rel]
		DirAccess.make_dir_recursive_absolute(dest.get_base_dir())
		if FileAccess.file_exists(dest):
			_report("Checking " + show, done, total)
			if AioInstall.md5_of(dest) == f.md5:
				kept += 1
				done += f.size
				continue
			DirAccess.remove_absolute(dest)   # this folder's own file of an older version: replaced
		if f.md5 == ModCatalog.EMPTY_MD5:
			var empty := FileAccess.open(dest, FileAccess.WRITE)
			if empty == null:
				return _fail(AioInstall.REFUSED, "cannot write %s" % dest)
			empty.close()
			downloaded += 1
			continue
		var part := dest + ".part"
		var size: int = f.size if f.size > 0 else -1
		var base_done := done
		var got := ""
		if size < 0 and FileAccess.file_exists(part):
			got = AioInstall.md5_of(part)   # a file without a size that was complete when the last run stopped
		if got != f.md5:
			var each := func(n: int, s: int) -> void: _report("%s: %d%%" % [show, int(n * 100.0 / maxi(s, 1))], base_done + n, maxi(total, base_done + n))
			var d: Dictionary = updater.http.download(f.url, part, size, each, MAX_UNKNOWN)
			if d.resumed > 0:
				Log.line("LAUNCHER MODS resumed %s at %d bytes" % [f.rel, d.resumed])
			if not d.ok and stop_request != "":
				return _stopped(part)
			if not d.ok:
				if "HTTP 404" in d.error or "HTTP 410" in d.error:
					return _fail(AioInstall.REJECTED, "The service no longer has %s of %s (%s). %s" % [f.rel, f.layer, d.error, REMEDY])
				if "HTTP" in d.error or "more than" in d.error or "wrong place" in d.error or "no usable size" in d.error:
					return _fail(AioInstall.REJECTED, "%s of %s: the service answered something OpenBFME does not recognise (%s). %s" % [f.rel, f.layer, d.error, REMEDY])
				return _fail(AioInstall.NETWORK, "%s of %s: %s. %s" % [f.rel, f.layer, d.error, REMEDY])
			_report("Checking " + show, done, total)
			got = AioInstall.md5_of(part)
		if got != f.md5:
			DirAccess.remove_absolute(part)
			return _fail(AioInstall.REJECTED, "%s of %s from the service has MD5 %s, expected %s: it was deleted. %s" % [f.rel, f.layer, got, f.md5, REMEDY])
		if DirAccess.rename_absolute(part, dest) != OK:
			return _fail(AioInstall.REFUSED, "cannot rename %s to %s" % [part, dest])
		downloaded += 1
		done += f.size
		if test_stop_after != "" and test_stop_after.get_slice(":", 0) == "%s/%s" % [f.part, f.rel]:
			request_stop(test_stop_after.get_slice(":", 1))
	if stop_request != "":
		return _stopped("")
	# what is no longer part of the layout (an older version's file, an enhancement switched off) goes: only below install/ and mod/
	var removed := 0
	for p in ["install", "mod", "maps"]:
		removed += _remove_unlisted(base.path_join(p), keep)
	var summary := ModCatalog.summary(r.entry, r.first, r.rest, r.plan, r.enhancements)
	if not _write_json(base.path_join(RECORD), summary):
		return _fail(AioInstall.REFUSED, "cannot write %s" % base.path_join(RECORD))
	# the pick (active.cfg) is the caller's: an install writes below user://mods/<guid>/ only
	Log.line("LAUNCHER MODS installed %s %s in %s: %d downloaded, %d already there, %d removed" % [title, r.entry.version, base, downloaded, kept, removed])
	return {"ok": true, "kind": "", "error": "", "summary": summary, "downloaded": downloaded, "kept": kept, "removed": removed, "status": r.status}


## removes every file below `folder` that `keep` does not hold (links are removed as links, never followed); empty folders go too
static func _remove_unlisted(folder: String, keep: Dictionary) -> int:
	var d := DirAccess.open(folder)
	if d == null:
		return 0
	var n := 0
	d.include_hidden = true
	for name in d.get_files():
		var path := folder.path_join(name)
		if not keep.has(path) and DirAccess.remove_absolute(path) == OK:
			n += 1
	for name in d.get_directories():
		var path := folder.path_join(name)
		if d.is_link(name):
			DirAccess.remove_absolute(path)
			continue
		n += _remove_unlisted(path, keep)
		DirAccess.remove_absolute(path)   # only when it is empty now
	return n


## removes `folder` and everything below it, never following a link. true when it is gone.
static func _remove_tree(folder: String) -> bool:
	var d := DirAccess.open(folder)
	if d == null:
		return not DirAccess.dir_exists_absolute(folder)
	d.include_hidden = true
	for name in d.get_files():
		DirAccess.remove_absolute(folder.path_join(name))
	for name in d.get_directories():
		if d.is_link(name):
			DirAccess.remove_absolute(folder.path_join(name))
		else:
			_remove_tree(folder.path_join(name))
	return DirAccess.remove_absolute(folder) == OK


## Removes user://mods/<guid>/ (and only that); the original game is picked when it was the active mod. "" or why not.
func uninstall(guid: String) -> String:
	if not ModCatalog.safe_guid(guid):
		return "'%s' is not a package id" % guid.left(80).c_escape()
	var base := dir.path_join(guid)
	if not DirAccess.dir_exists_absolute(base) and not _is_link(base):
		return "%s is not installed" % guid
	var name: String = record(guid).get("name", guid)
	if active().guid == guid:
		set_active("")
	var gone := DirAccess.remove_absolute(base) == OK if _is_link(base) else _remove_tree(base)
	if not gone:
		return "%s could not be removed completely (is a file open?)" % base
	Log.line("LAUNCHER MODS removed %s (%s)" % [name, base])
	return ""
