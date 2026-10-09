## Lane LAUNCH-1: the launcher's work: the releases list, the signed manifests, installing, choosing and starting a game version, and
## staging a launcher update. Blocking calls (the UI runs them on a worker thread; the command line runs them directly).
##
## The data folder (user://, see project.godot):
##   versions/<version>/game/        the unpacked game package (its single top folder stripped)
##   versions/<version>/manifest.json, manifest.json.sig   the signed manifest it was installed from (format 2: it lists every file of
##                                   the package with its size and SHA-256); a version counts as installed only when the signature
##                                   verifies again, the manifest names this folder's version and every signed file is there with its
##                                   size; before Play every file's SHA-256 is checked against the signed one (round 3)
##   versions/<version>.partial/     an install in progress (removed and redone by the next install)
##   downloads/<asset>.part (+ .json)  a download in progress, resumed with an HTTP Range request (the .json names the asset's digest)
##   cache/releases.json + .etag     the last releases list from the API (If-None-Match: a 304 does not count against the rate limit)
##   cache/manifests/<tag>/          verified manifests, so a check costs one API request
##   state.cfg                       the channel, the version picked by the user, launcher versions that failed to start
## See docs/RELEASE.md, "The launcher", for the rules (verify before use, rollback protection, keep two, offline start).
extends RefCounted

const Semver := preload("res://scripts/core/semver.gd")
const Manifest := preload("res://scripts/core/manifest.gd")
const Archive := preload("res://scripts/core/archive.gd")
const NetPolicy := preload("res://scripts/net/net_policy.gd")
const HttpFetch := preload("res://scripts/net/http_fetch.gd")
const Log := preload("res://scripts/core/launcher_log.gd")

const API_BASE := "https://api.github.com"
const KEEP_VERSIONS := 2
const FREEZE_DAYS := 60
const MAX_RELEASES_JSON := 4 * 1024 * 1024
const MAX_NOTES := 20000
const GAME_EXE := {"linux-x64": "OpenBFME.x86_64", "windows-x64": "OpenBFME.exe"}
const LAUNCHER_EXE := {"linux-x64": "OpenBFMELauncher.x86_64", "windows-x64": "OpenBFMELauncher.exe"}

var info: RefCounted  # BuildInfo
var data_dir := ""
var api_base := API_BASE
var policy := NetPolicy.new()
var http: HttpFetch
var state := ConfigFile.new()
var progress := Callable()  # progress.call(text, done_bytes, total_bytes)
var launcher_dir := ""  # the folder of the running launcher's executable ("" in a development run: no self-update)


func _init(build_info: RefCounted) -> void:
	info = build_info
	data_dir = OS.get_user_data_dir()
	http = HttpFetch.new(policy, info.version)
	state.load(data_dir.path_join("state.cfg"))
	if info.packaged:
		launcher_dir = OS.get_executable_path().get_base_dir()


func set_test_api_base(origin: String) -> String:
	var why := NetPolicy.check_test_origin(origin)
	if why != "":
		return why
	api_base = origin
	policy.test_origin = origin
	return ""


func channel() -> String:
	return state.get_value("launcher", "channel", "stable")


func set_channel(c: String) -> void:
	state.set_value("launcher", "channel", c)
	_save_state()


func selected() -> String:
	return state.get_value("launcher", "selected", "")


func set_selected(v: String) -> void:
	state.set_value("launcher", "selected", v)
	_save_state()


func _save_state() -> void:
	state.save(data_dir.path_join("state.cfg"))


func _report(text: String, done := -1, total := -1) -> void:
	if progress.is_valid():
		progress.call(text, done, total)


# ---- the releases ------------------------------------------------------------------------------------------------------------------------

## {ok, error, releases: [{tag, prerelease, notes, published, assets: {name: {url, size}}}] newest first, from_cache}
## Only releases whose tag is a release version are listed (drafts and other tags are skipped, and logged).
func fetch_releases() -> Dictionary:
	var cache := data_dir.path_join("cache")
	DirAccess.make_dir_recursive_absolute(cache)
	var etag := ""
	var cached := FileAccess.get_file_as_bytes(cache.path_join("releases.json"))
	if not cached.is_empty() and FileAccess.file_exists(cache.path_join("releases.etag")):
		etag = FileAccess.get_file_as_string(cache.path_join("releases.etag")).strip_edges()
	var url := "%s/repos/%s/releases?per_page=30" % [api_base, info.repo]
	var r := http.get_small(url, MAX_RELEASES_JSON, "application/vnd.github+json", etag)
	if not r.ok:
		return {"ok": false, "error": "the release list could not be fetched: " + r.error}
	var body: PackedByteArray = r.body
	var from_cache := false
	if r.code == 304:
		if cached.is_empty():
			return {"ok": false, "error": "GitHub answered 304 Not Modified but no release list is cached"}
		body = cached
		from_cache = true
	elif r.code == 403 or r.code == 429:
		var reset := ""
		if r.headers.has("x-ratelimit-reset") and str(r.headers["x-ratelimit-reset"]).is_valid_int():
			reset = " (it resets at %s UTC)" % Time.get_datetime_string_from_unix_time(int(r.headers["x-ratelimit-reset"]), true)
		if r.headers.get("x-ratelimit-remaining", "") == "0" or r.code == 429:
			return {"ok": false, "error": "GitHub's API rate limit for this network is used up%s" % reset}
		return {"ok": false, "error": "GitHub refused the release list (HTTP %d)" % r.code}
	elif r.code == 404:
		return {"ok": false, "error": "the repository %s has no releases page (HTTP 404)" % info.repo}
	elif r.code != 200:
		return {"ok": false, "error": "the release list request failed (HTTP %d)" % r.code}
	var parsed = JSON.parse_string(body.get_string_from_utf8())
	if typeof(parsed) != TYPE_ARRAY:
		return {"ok": false, "error": "the release list is not a JSON array"}
	if r.code == 200:
		_write(cache.path_join("releases.json"), body)
		var new_etag: String = r.headers.get("etag", "")
		if new_etag != "":
			_write(cache.path_join("releases.etag"), new_etag.to_utf8_buffer())
		elif FileAccess.file_exists(cache.path_join("releases.etag")):
			DirAccess.remove_absolute(cache.path_join("releases.etag"))
	var out := []
	for rel in parsed:
		if typeof(rel) != TYPE_DICTIONARY or typeof(rel.get("tag_name")) != TYPE_STRING:
			continue
		if rel.get("draft", false):
			continue
		var tag: String = rel.tag_name
		if not Semver.is_valid(tag):
			Log.line("LAUNCHER skipped release '%s': %s" % [tag.left(80).c_escape(), Semver.parse(tag).error])
			continue
		var assets := {}
		for a in rel.get("assets", []):
			if typeof(a) == TYPE_DICTIONARY and typeof(a.get("name")) == TYPE_STRING and typeof(a.get("browser_download_url")) == TYPE_STRING:
				assets[a.name] = {"url": a.browser_download_url, "size": int(a.get("size", 0))}
		var notes := str(rel.get("body", "")) if rel.get("body") != null else ""
		out.append({"tag": tag, "prerelease": bool(rel.get("prerelease", false)), "notes": plain_text(notes),
			"published": str(rel.get("published_at", "")), "assets": assets})
	out.sort_custom(func(a, b): return Semver.compare(a.tag, b.tag) > 0)
	return {"ok": true, "error": "", "releases": out, "from_cache": from_cache}


## release notes as plain text: no control characters but newlines and tabs, at most MAX_NOTES characters (shown in a label with no
## markup: nothing remote is ever rendered as HTML, BBCode or an image)
static func plain_text(s: String) -> String:
	var out := ""
	for c in s.left(MAX_NOTES):
		var u := c.unicode_at(0)
		if u >= 0x20 and u != 0x7f or c == "\n" or c == "\t":
			out += c
	return out


## A freeze attack (someone serving an old but validly signed release list) cannot be told apart from a quiet project, so it is a plain
## warning, never a block: "" or the warning when the newest manifest the launcher could get is signed with a date more than
## FREEZE_DAYS before today (Sol r1).
func freeze_warning(manifest: Dictionary) -> String:
	var signed := Time.get_unix_time_from_datetime_string(str(manifest.get("date", "")) + "T00:00:00")
	var age_days := int((Time.get_unix_time_from_system() - signed) / 86400.0)
	if age_days <= FREEZE_DAYS:
		return ""
	return ("the newest %s release the launcher can see (%s) is %d days old (signed %s). If you expected a newer build, the release list " +
		"you are being served may be out of date.") % [manifest.get("channel", ""), manifest.get("version", ""), age_days, manifest.get("date", "")]


func in_channel(releases: Array, ch: String) -> Array:
	return releases.filter(func(r): return ch == "preview" or not r.prerelease)


## The release's verified manifest (cached per tag). {ok, error, manifest, bytes, sig}
func release_manifest(release: Dictionary) -> Dictionary:
	var dir := data_dir.path_join("cache/manifests").path_join(release.tag)
	var expect := {"repo": info.repo, "tag": release.tag, "prerelease": release.prerelease}
	var bytes := FileAccess.get_file_as_bytes(dir.path_join("manifest.json"))
	var sig := FileAccess.get_file_as_bytes(dir.path_join("manifest.json.sig"))
	if not bytes.is_empty() and sig.size() == 64:
		var v := Manifest.verify(bytes, sig, info.release_key, expect)
		if v.ok:
			return {"ok": true, "error": "", "manifest": v.manifest, "bytes": bytes, "sig": sig}
	for need in ["manifest.json", "manifest.json.sig"]:
		if not release.assets.has(need):
			return {"ok": false, "error": "the release %s has no %s (not a launcher release)" % [release.tag, need]}
	var m := http.get_small(release.assets["manifest.json"].url, Manifest.MAX_BYTES)
	if not m.ok or m.code != 200:
		return {"ok": false, "error": "the manifest of %s could not be downloaded: %s" % [release.tag, m.error if not m.ok else "HTTP %d" % m.code]}
	var s := http.get_small(release.assets["manifest.json.sig"].url, 64)
	if not s.ok or s.code != 200:
		return {"ok": false, "error": "the manifest signature of %s could not be downloaded: %s" % [release.tag, s.error if not s.ok else "HTTP %d" % s.code]}
	var v := Manifest.verify(m.body, s.body, info.release_key, expect)
	if not v.ok:
		return {"ok": false, "error": "release %s rejected: %s" % [release.tag, v.error]}
	DirAccess.make_dir_recursive_absolute(dir)
	_write(dir.path_join("manifest.json"), m.body)
	_write(dir.path_join("manifest.json.sig"), s.body)
	return {"ok": true, "error": "", "manifest": v.manifest, "bytes": m.body, "sig": s.body}


# ---- installed versions ------------------------------------------------------------------------------------------------------------------

## [{version, path (the game folder), manifest}] of the verified installed versions, newest first; problems are logged and the version
## is not offered (it is not deleted: the user may look at it)
func installed() -> Array:
	var root := data_dir.path_join("versions")
	var out := []
	if not DirAccess.dir_exists_absolute(root):
		return out
	for name in DirAccess.get_directories_at(root):
		if name.ends_with(".partial") or name.ends_with(".old"):
			continue
		var v := verify_installed(name)
		if v.ok:
			out.append({"version": name, "path": root.path_join(name).path_join("game"), "manifest": v.manifest})
		else:
			Log.line("LAUNCHER installed version %s is not usable: %s" % [name.c_escape(), v.error])
	out.sort_custom(func(a, b): return Semver.compare(a.version, b.version) > 0)
	return out


## `full`: every file's size and SHA-256 against the signed manifest, and no other file (before Play); else that every file is there
func verify_installed(name: String, full := false) -> Dictionary:
	if not Semver.is_valid(name):
		return {"ok": false, "error": "not a version folder"}
	var dir := data_dir.path_join("versions").path_join(name)
	var v := Manifest.verify(FileAccess.get_file_as_bytes(dir.path_join("manifest.json")),
		FileAccess.get_file_as_bytes(dir.path_join("manifest.json.sig")), info.release_key, {"repo": info.repo})
	if not v.ok:
		return {"ok": false, "error": v.error}
	if v.manifest.version != name:
		return {"ok": false, "error": "its manifest is for %s" % v.manifest.version}
	var files: Dictionary = v.manifest.assets.game[info.platform()].files
	if not files.has(GAME_EXE[info.platform()]):
		return {"ok": false, "error": "the package has no %s" % GAME_EXE[info.platform()]}
	if full:
		var why := Manifest.check_files(dir.path_join("game"), files)
		if why != "":
			return {"ok": false, "modified": true, "error": why}
		return {"ok": true, "error": "", "manifest": v.manifest}
	# the listing only needs the files to be there; a changed file is found by the full check before Play, which then says so and
	# offers a fresh download (a version that silently vanished from the list would hide the change)
	for rel in files:
		if not FileAccess.file_exists(dir.path_join("game").path_join(rel)):
			return {"ok": false, "error": "%s is missing" % rel}
	return {"ok": true, "error": "", "manifest": v.manifest}


## "" when unpacked files (Archive.extract's {rel: {size, sha256}}) are exactly the signed ones
static func same_files(got: Dictionary, signed: Dictionary) -> String:
	for rel in signed:
		if not got.has(rel):
			return "%s is missing from the package" % rel
		if int(got[rel].size) != int(signed[rel].size) or got[rel].sha256 != signed[rel].sha256:
			return "%s does not match the signed manifest" % rel
	for rel in got:
		if not signed.has(rel):
			return "%s is not in the signed manifest" % rel
	return ""


func newest_installed() -> String:
	var inst := installed()
	return "" if inst.is_empty() else inst[0].version


## the version Play starts: the user's pick when it is installed, else the newest installed
func play_version() -> String:
	var inst := installed()
	for i in inst:
		if i.version == selected():
			return i.version
	return "" if inst.is_empty() else inst[0].version


# ---- installing --------------------------------------------------------------------------------------------------------------------------

## Install `release`'s game package. `user_pick`: the user chose this version (an older one is allowed then); otherwise (an automatic
## update) only a version newer than every installed one is installed. {ok, error, version}
func install(release: Dictionary, user_pick: bool) -> Dictionary:
	var newest := newest_installed()
	if not user_pick and newest != "" and Semver.compare(release.tag, newest) <= 0:
		return {"ok": false, "error": "%s is not newer than the installed %s: an automatic update never installs an older or the same version (pick it yourself to roll back)" % [release.tag, newest], "version": ""}
	var mm := release_manifest(release)
	if not mm.ok:
		return {"ok": false, "error": mm.error, "version": ""}
	var m: Dictionary = mm.manifest
	var asset: Dictionary = m.assets.game[info.platform()]
	var got := fetch_asset(release, asset)
	if not got.ok:
		return {"ok": false, "error": got.error, "version": ""}
	var root := data_dir.path_join("versions")
	var partial := root.path_join(m.version + ".partial")
	remove_tree(partial)
	_report("Unpacking %s" % asset.name)
	var x := Archive.extract(got.path, "zip" if asset.name.ends_with(".zip") else "tar.gz", Manifest.top_folder(asset.name), partial.path_join("game"))
	if not x.ok:
		remove_tree(partial)
		return {"ok": false, "error": "%s was not installed: %s" % [m.version, x.error], "version": ""}
	var differ := same_files(x.files, asset.files)
	if differ != "":
		remove_tree(partial)
		return {"ok": false, "error": "%s was not installed: %s" % [m.version, differ], "version": ""}
	for item in [["manifest.json", mm.bytes], ["manifest.json.sig", mm.sig]]:
		if not _write(partial.path_join(item[0]), item[1]):
			remove_tree(partial)
			return {"ok": false, "error": "%s was not installed: writing %s failed (is the disk full?)" % [m.version, item[0]], "version": ""}
	test_point("install-unpacked")
	# replacing an installed folder (Sol r1: the old code deleted it before its replacement was in place): the installed folder is set
	# aside as <v>.old, the new one renamed into place, and only then the old one removed; recover() completes or undoes this after a
	# crash at any point
	var final := root.path_join(m.version)
	var aside := root.path_join(m.version + ".old")
	var had := DirAccess.dir_exists_absolute(final)
	if had:
		if DirAccess.rename_absolute(final, aside) != OK:
			remove_tree(partial)
			return {"ok": false, "error": "%s is installed and could not be replaced (is the game running?)" % m.version, "version": ""}
		test_point("install-aside")
	if DirAccess.rename_absolute(partial, final) != OK:
		if had:
			DirAccess.rename_absolute(aside, final)
		remove_tree(partial)
		return {"ok": false, "error": "%s could not be moved into place" % m.version, "version": ""}
	test_point("install-placed")
	if had:
		remove_tree(aside)
	DirAccess.remove_absolute(got.path)
	Log.line("LAUNCHER installed %s (%s, %d files)" % [m.version, asset.name, x.files.size()])
	if user_pick:
		set_selected(m.version)
	else:
		set_selected("")  # a new build replaces an earlier rollback pick
	prune()
	return {"ok": true, "error": "", "version": m.version}


## Download a manifest asset to downloads/ (resuming) and check its size and SHA-256. {ok, error, path}. A mismatch deletes the file:
## the next attempt starts over (never a half-verified state).
func fetch_asset(release: Dictionary, asset: Dictionary) -> Dictionary:
	if not release.assets.has(asset.name):
		return {"ok": false, "error": "the release %s has no asset %s" % [release.tag, asset.name]}
	var dl := data_dir.path_join("downloads")
	DirAccess.make_dir_recursive_absolute(dl)
	var part := dl.path_join(asset.name + ".part")
	var meta := dl.path_join(asset.name + ".part.json")
	var meta_now := JSON.stringify({"sha256": asset.sha256, "size": asset.size})
	if FileAccess.file_exists(part) and FileAccess.get_file_as_string(meta) != meta_now:
		DirAccess.remove_absolute(part)  # a partial file of other bytes under the same name: start over
	_write(meta, meta_now.to_utf8_buffer())
	var started := Time.get_ticks_msec()
	var r := http.download(release.assets[asset.name].url, part, asset.size, func(done: int, total: int) -> void:
		var secs := maxf(0.001, (Time.get_ticks_msec() - started) / 1000.0)
		_report("Downloading %s" % asset.name, done, total)
		if done == total:
			Log.line("LAUNCHER downloaded %s: %d bytes in %.1f s" % [asset.name, total, secs]))
	if r.resumed > 0:
		Log.line("LAUNCHER resumed %s at %d bytes" % [asset.name, r.resumed])
	if not r.ok:
		return {"ok": false, "error": "%s: %s" % [asset.name, r.error]}
	_report("Checking %s" % asset.name)
	var f := FileAccess.open(part, FileAccess.READ)
	if f == null or f.get_length() != asset.size:
		var got := -1 if f == null else f.get_length()
		f = null
		DirAccess.remove_absolute(part)
		return {"ok": false, "error": "%s has %d bytes, the signed manifest says %d: deleted" % [asset.name, got, asset.size]}
	var hc := HashingContext.new()
	hc.start(HashingContext.HASH_SHA256)
	while f.get_position() < f.get_length():
		hc.update(f.get_buffer(1 << 20))
	f.close()
	var digest := hc.finish().hex_encode()
	if digest != asset.sha256:
		DirAccess.remove_absolute(part)
		DirAccess.remove_absolute(meta)
		return {"ok": false, "error": "%s does not match the signed manifest (SHA-256 %s, expected %s): deleted, nothing was installed" % [asset.name, digest, asset.sha256]}
	DirAccess.remove_absolute(meta)
	return {"ok": true, "error": "", "path": part}


## Complete or undo an install that was interrupted (a crash, a kill, a power cut), run at every start before anything else:
##   * <v>.partial: an unpack that never finished: removed;
##   * <v>.old (the installed folder set aside while its replacement is moved in): when <v> is in place and verifies, the replacement
##     finished: <v>.old is removed; when <v> is missing, it is renamed back to <v>.
## Every other step of an install is one rename, so the installed versions are always either the old or the new complete folder.
func recover() -> void:
	var root := data_dir.path_join("versions")
	if not DirAccess.dir_exists_absolute(root):
		return
	for name in DirAccess.get_directories_at(root):
		if name.ends_with(".partial"):
			Log.line("LAUNCHER removed an unfinished install (%s)" % name)
			remove_tree(root.path_join(name))
		elif name.ends_with(".old"):
			var v := name.trim_suffix(".old")
			if DirAccess.dir_exists_absolute(root.path_join(v)) and verify_installed(v).ok:
				Log.line("LAUNCHER finished replacing %s (removed the previous copy)" % v)
				remove_tree(root.path_join(name))
			elif not DirAccess.dir_exists_absolute(root.path_join(v)):
				DirAccess.rename_absolute(root.path_join(name), root.path_join(v))
				Log.line("LAUNCHER restored %s: replacing it was interrupted" % v)


## Test builds only (BuildInfo.tests_allowed): --test-kill-at=<point> kills this process abruptly at that point, for the crash tests.
static var kill_at := ""


static func test_point(name: String) -> void:
	if kill_at == name:
		Log.line("LAUNCHER test kill at " + name)
		OS.kill(OS.get_process_id())
		OS.delay_msec(10000)


## keep the KEEP_VERSIONS newest versions and the user's pick; remove the others and stale partial folders
func prune() -> void:
	var root := data_dir.path_join("versions")
	var keep := {}
	var inst := installed()
	for i in mini(KEEP_VERSIONS, inst.size()):
		keep[inst[i].version] = true
	if selected() != "":
		keep[selected()] = true
	for name in DirAccess.get_directories_at(root):
		if Semver.is_valid(name) and not keep.has(name) and inst.any(func(i): return i.version == name):
			Log.line("LAUNCHER removed the old version %s (the two newest are kept)" % name)
			remove_tree(root.path_join(name))


# ---- starting the game -------------------------------------------------------------------------------------------------------------------

## Start `version`'s game executable as its own process (the launcher passes no arguments: the game's first-run screen finds the game
## files). {ok, error, pid}
## Every file of the version is checked against the signed manifest first (round 3): a changed file is never started; the caller offers
## a fresh download ({modified: true}).
func play(version: String) -> Dictionary:
	_report("Checking %s" % version)
	var v := verify_installed(version, true)
	if not v.ok:
		var why := "%s cannot be started: %s" % [version, v.error]
		if v.get("modified", false):
			why = "%s was changed after it was installed (%s): it is not started; download it again (--install=%s, or Play to confirm)" % [version, v.error, version]
		return {"ok": false, "error": why, "pid": -1, "modified": v.get("modified", false)}
	var exe := data_dir.path_join("versions").path_join(version).path_join("game").path_join(GAME_EXE[info.platform()])
	var pid := OS.create_process(exe, [])
	if pid <= 0:
		return {"ok": false, "error": "%s could not be started" % exe, "pid": -1}
	Log.line("LAUNCHER started %s (pid %d)" % [version, pid])
	return {"ok": true, "error": "", "pid": pid}


# ---- launcher self-update ----------------------------------------------------------------------------------------------------------------

func launcher_files() -> Array:
	return [LAUNCHER_EXE[info.platform()]]  # one file: the pack is embedded (self_update.gd)


## Stage the launcher of `release` in <launcher folder>/update/ when it is newer than this launcher and did not fail before. The swap
## happens on the next start (apply_staged_update). {ok, error, staged (version or "")}
func stage_self_update(release: Dictionary) -> Dictionary:
	if launcher_dir == "":
		return {"ok": true, "error": "", "staged": "", "why": "a development run does not update itself"}
	if not Semver.is_valid(info.version):
		return {"ok": true, "error": "", "staged": "", "why": "this launcher's version %s is not a release version: it does not update itself" % info.version}
	if Semver.compare(release.tag, info.version) <= 0:
		return {"ok": true, "error": "", "staged": "", "why": "the launcher is up to date (%s)" % info.version}
	if release.tag in state.get_value("launcher", "rejected_launcher", []):
		return {"ok": true, "error": "", "staged": "", "why": "the launcher %s failed to start before and is not installed again automatically" % release.tag}
	var staged := launcher_dir.path_join("update")
	if FileAccess.get_file_as_string(staged.path_join("VERSION.launcher")).strip_edges() == release.tag:
		return {"ok": true, "error": "", "staged": release.tag, "why": "already staged"}
	var probe := launcher_dir.path_join("update.partial")
	if DirAccess.make_dir_recursive_absolute(probe) != OK:
		return {"ok": false, "error": "the launcher's folder %s is not writable: the launcher cannot update itself there (move it to a folder you can write to); game updates still work" % launcher_dir, "staged": ""}
	DirAccess.remove_absolute(probe)
	var mm := release_manifest(release)
	if not mm.ok:
		return {"ok": false, "error": mm.error, "staged": ""}
	var asset: Dictionary = mm.manifest.assets.launcher[info.platform()]
	var got := fetch_asset(release, asset)
	if not got.ok:
		return {"ok": false, "error": got.error, "staged": ""}
	var partial := launcher_dir.path_join("update.partial")
	remove_tree(partial)
	remove_tree(staged)
	var x := Archive.extract(got.path, "zip" if asset.name.ends_with(".zip") else "tar.gz", Manifest.top_folder(asset.name), partial)
	if not x.ok:
		remove_tree(partial)
		return {"ok": false, "error": "the launcher %s was not staged: %s" % [release.tag, x.error], "staged": ""}
	var differ := same_files(x.files, asset.files)
	for f in launcher_files():
		if differ == "" and not x.files.has(f):
			differ = "the package has no %s" % f
	if differ != "":
		remove_tree(partial)
		return {"ok": false, "error": "the launcher %s was not staged: %s" % [release.tag, differ], "staged": ""}
	if not _write(partial.path_join("VERSION.launcher"), (release.tag + "\n").to_utf8_buffer()) \
			or DirAccess.rename_absolute(partial, staged) != OK:
		remove_tree(partial)
		return {"ok": false, "error": "the launcher %s could not be staged in %s (is the folder writable?)" % [release.tag, launcher_dir], "staged": ""}
	DirAccess.remove_absolute(got.path)
	Log.line("LAUNCHER staged the launcher %s; it replaces this one on the next start" % release.tag)
	return {"ok": true, "error": "", "staged": release.tag}


## The staged launcher of `tag` in <dir>/update/, checked against signed data at the moment it is applied (round 3: the swap used to
## trust whatever file was in update/): the cached manifest of `tag` must verify again, and every staged file must be exactly a signed
## file of the launcher package (VERSION.launcher aside). {ok, error, new_sha256 (the signed SHA-256 of the executable)}
func check_staged(dir: String, tag: String) -> Dictionary:
	var cache := data_dir.path_join("cache/manifests").path_join(tag)
	var v := Manifest.verify(FileAccess.get_file_as_bytes(cache.path_join("manifest.json")),
		FileAccess.get_file_as_bytes(cache.path_join("manifest.json.sig")), info.release_key, {"repo": info.repo, "tag": tag})
	if not v.ok:
		return {"ok": false, "error": "no verified manifest for the staged launcher %s (%s)" % [tag, v.error]}
	var files: Dictionary = v.manifest.assets.launcher[info.platform()].files
	var exe: String = launcher_files()[0]
	if not files.has(exe):
		return {"ok": false, "error": "the signed launcher package %s has no %s" % [tag, exe]}
	var why := Manifest.check_files(dir.path_join("update"), files, ["VERSION.launcher"])
	if why != "":
		return {"ok": false, "error": "the staged launcher %s is not the signed one: %s" % [tag, why]}
	return {"ok": true, "error": "", "new_sha256": files[exe].sha256}


# ---- files -------------------------------------------------------------------------------------------------------------------------------

static func _write(path: String, bytes: PackedByteArray) -> bool:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	var ok := f.store_buffer(bytes)
	f.flush()
	ok = ok and f.get_error() == OK
	f.close()
	return ok


## remove a folder and everything below it, never following a link (a link is removed, not entered)
static func remove_tree(path: String) -> void:
	if not DirAccess.dir_exists_absolute(path):
		return
	var d := DirAccess.open(path)
	if d == null:
		return
	d.include_hidden = true
	for f in d.get_files():
		DirAccess.remove_absolute(path.path_join(f))
	for sub in d.get_directories():
		var p := path.path_join(sub)
		if d.is_link(sub):
			DirAccess.remove_absolute(p)
		else:
			remove_tree(p)
	DirAccess.remove_absolute(path)
