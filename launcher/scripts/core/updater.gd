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
##   state.cfg                       the channel, the version picked by the user, launcher versions that failed to start, whether updates
##                                   install by themselves (lane UI-5), the versions started and their process ids (never removed while
##                                   they run)
## Lane ONEZIP-1 (one download): the folder of the launcher's executable may hold the game too (the combined package: OpenBFME beside
## OpenBFMELauncher, with the package's VERSION file). When its signed inventory verifies, Play starts the bundled game in place with
## no first download; it counts as installed when the launcher decides whether a release is newer.
## The launcher's own update comes out of the game package (stage_self_update, Manifest.launcher_source).
## See docs/RELEASE.md, "The launcher", for the rules (verify before use, rollback protection, keep two, offline start).
extends RefCounted

const Paths := preload("res://scripts/ui/paths.gd")

const Semver := preload("res://scripts/core/semver.gd")
const Manifest := preload("res://scripts/core/manifest.gd")
const Archive := preload("res://scripts/core/archive.gd")
const NetPolicy := preload("res://scripts/net/net_policy.gd")
const HttpFetch := preload("res://scripts/net/http_fetch.gd")
const Log := preload("res://scripts/core/launcher_log.gd")
const GlibcCheck := preload("res://scripts/core/glibc_check.gd")
const MacOSUpdate := preload("res://scripts/core/macos_update.gd")
const ModInstall := preload("res://scripts/core/mod_install.gd")  # lane MODS-3: the active mod's game arguments

const API_BASE := "https://api.github.com"
const KEEP_VERSIONS := 2
const FREEZE_DAYS := 60
const MAX_RELEASES_JSON := 4 * 1024 * 1024
const MAX_NOTES := 20000
const GAME_EXE := {"linux-x64": "OpenBFME.x86_64", "windows-x64": "OpenBFME.exe",
	"macos-arm64-unstable-preview": "OpenBFME.app/Contents/MacOS/OpenBFME",
	"macos-universal": "OpenBFME.app/Contents/MacOS/OpenBFME",
	"macos-arm64": "OpenBFME.app/Contents/MacOS/OpenBFME"}
const LAUNCHER_EXE := {"linux-x64": "OpenBFMELauncher.x86_64", "windows-x64": "OpenBFMELauncher.exe",
	"macos-arm64-unstable-preview": "OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher",
	"macos-universal": "OpenBFMELauncher.app/Contents/MacOS/OpenBFME Launcher",
	"macos-arm64": "OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher"}
## lane ONEZIP-1: set for every game this launcher starts, so that game does not ask the launcher for an update check again
## (godot/scripts/release/update_handover.gd)
const STARTED_BY_LAUNCHER := "OPENBFME_STARTED_BY_LAUNCHER"

const DEFAULT_LOBBY := "https://play.openbfme.com"  # lane ONLINECOUNT-1 (the game's ONLINE_DEFAULT_LOBBY)

var info: RefCounted  # BuildInfo
var data_dir := ""
var api_base := API_BASE
var policy := NetPolicy.new()
var http: HttpFetch
var state := ConfigFile.new()
var _state_lock := Mutex.new()  # lane UI-5: Play (the main thread) writes state.cfg while a worker thread installs an update
var progress := Callable()  # progress.call(text, done_bytes, total_bytes)
var launcher_dir := ""  # the folder of the running launcher's executable ("" in a development run: no self-update)
var macos_transaction := false
var translocated := ""  # lane MACPLAY-1: the read-only copy macOS started this launcher from (App Translocation), "" when it did not

## Lane MACPLAY-1: macOS runs a quarantined app that was not moved since it was unpacked from a temporary, read-only copy of that one app
## (App Translocation: <the system's temporary folder>/AppTranslocation/<UUID>/d/OpenBFME Launcher.app). OpenBFME.app is not beside that copy, so
## the launcher there finds no game (the v0.4.6 Mac preview showed a dimmed Play with no word why) and cannot update the package. The
## launcher says so and tells the player what to do (README-MAC.txt "FIRST OPEN"); it never downloads a second game for a translocated copy.
const TRANSLOCATED_TEXT := ("macOS opened a temporary copy of the launcher, so it cannot find the game. " +
	"Quit, then use Finder to move the folder containing both apps out of Downloads and reopen the launcher. " +
	"If macOS blocks it, verify the download, then use System Settings > Privacy & Security > Open Anyway. " +
	"Do not override malware or damaged-app warnings.")


static func is_translocated(path: String) -> bool:
	return path.replace("\\", "/").contains("/AppTranslocation/")


## MACPLAY-1: whether updates replace the verified package beside this Mac launcher as a whole (DEVPORT-6's swap). Only a folder the swap
## accepts (the unpacked openbfme-<v>-macos-arm64 folder, nothing else in it: MacOSUpdate.validate_installed_root); a package whose apps
## were moved elsewhere (both into Applications, say) still plays, and its updates install into versions/<v>/game.
func mac_swap_root() -> bool:
	if not macos_updates_enabled() or bundled().is_empty():
		return false
	var why := MacOSUpdate.validate_installed_root(launcher_dir.simplify_path(), self)
	if why != "":
		Log.line("LAUNCHER updates install into the launcher's data folder: the folder of the apps is not one the package update replaces (%s)" % why)
	return why == ""


## the Mac whose launcher installs, verifies and plays versions/<v>/game (the signed macos-arm64 package of manifest-macos.json)
func mac_platform() -> bool:
	return macos_updates_enabled()

func macos_updates_unsupported() -> bool:
	return info.platform() in ["macos-universal", "macos-arm64-unstable-preview"] or \
		(info.platform() == "macos-arm64" and not macos_updates_enabled())


func macos_updates_enabled() -> bool:
	return OS.get_name() == "macOS" and info.macos_updates and info.platform() == "macos-arm64"


func release_version() -> String:
	return info.release_version() if info.has_method("release_version") else String(info.version)


func macos_preview() -> bool:
	return OS.get_name() == "macOS" and info.platform() == "macos-arm64-unstable-preview"


func releases_url() -> String:
	return "https://github.com/%s/releases" % info.repo


func macos_preview_notice(release: Dictionary) -> Dictionary:
	if not macos_preview():
		return {"ok": false, "error": "not a Mac preview", "message": "", "url": ""}
	var verified := release_manifest(release)
	if not verified.ok:
		return {"ok": false, "error": verified.error, "message": "", "url": ""}
	var base := String(info.version).get_slice(" ", 0)
	if not Semver.is_valid(base):
		return {"ok": false, "error": "invalid Mac preview base version", "message": "", "url": ""}
	var newer := Semver.compare(verified.manifest.version, base) > 0
	return {"ok": true, "error": "", "newer": newer, "url": releases_url(),
		"message": "A newer version may be on the releases page" if newer else "Mac preview: updates are manual for now"}


func _init(build_info: RefCounted) -> void:
	info = build_info
	data_dir = OS.get_user_data_dir()
	http = HttpFetch.new(policy, info.version)
	state.load(data_dir.path_join("state.cfg"))
	if info.packaged:
		set_launcher_dir(package_root(OS.get_executable_path(), info.platform()))


static func package_root(executable_path: String, platform: String) -> String:
	# MACPLAY-1: <root>/OpenBFME Launcher.app/Contents/MacOS/<exe>: the root holds both apps (spaces in any folder name are kept as they are)
	var executable_dir := executable_path.get_base_dir()
	return executable_dir.get_base_dir().get_base_dir().get_base_dir() if platform in ["macos-universal", "macos-arm64-unstable-preview", "macos-arm64"] else executable_dir


## the folder of this launcher's executable: where its own update is staged, and where the game of the combined package lies. A
## launcher started out of an installed version (lane ONEZIP-1: the combined package is installed with its launcher in it) has none:
## staging an update there would add files to a signed version folder, and that folder's game is the installed version already.
func set_launcher_dir(dir: String) -> void:
	if info.platform().begins_with("macos") and is_translocated(dir):
		translocated = dir
		Log.line("LAUNCHER this launcher runs from a macOS App Translocation copy (%s): no game beside it, no package update; move both apps out of Downloads" % dir)
		launcher_dir = ""
		return
	translocated = ""
	var here := dir.replace("\\", "/").simplify_path()
	var versions := data_dir.replace("\\", "/").simplify_path().path_join("versions") + "/"
	if here.begins_with(versions) or (OS.get_name() == "Windows" and here.to_lower().begins_with(versions.to_lower())):
		Log.line("LAUNCHER this launcher runs out of an installed version's folder: it plays and updates the installed versions, and does not update itself there (start the launcher of the folder you unpacked)")
		launcher_dir = ""
		return
	launcher_dir = dir


func set_test_api_base(origin: String) -> String:
	var why := NetPolicy.check_test_origin(origin)
	if why != "":
		return why
	api_base = origin
	policy.test_origin = origin
	return ""


## the player's channel; until they pick one, the channel of this launcher's own version (lane RELTEST-1: a preview launcher defaulted to
## Stable, so a new player's first start said "There is no stable release yet." with Play off while only previews exist)
func channel() -> String:
	return get_state("launcher", "channel", default_channel())


func default_channel() -> String:
	return Semver.channel_of(release_version()) if Semver.is_valid(release_version()) else "stable"


func set_channel(c: String) -> void:
	set_state("launcher", "channel", c)


func selected() -> String:
	return get_state("launcher", "selected", "")


func set_selected(v: String) -> void:
	set_state("launcher", "selected", v)


## lane UI-5 (the owner): updates download and install in the background by themselves (on by default); off, the check only says that a
## newer version is there and the player presses Update. The launcher's own update follows the same setting.
func auto_update() -> bool:
	return bool(get_state("launcher", "auto_update", true))


func set_auto_update(on: bool) -> void:
	set_state("launcher", "auto_update", on)


## lane ONLINECOUNT-1 (the owner): "Show how many players are online". On by default; off, the launcher never asks the lobby for the count (not at
## start, not later) and shows nothing in its place; the game reads the same setting (godot/scripts/online_stats.gd) from state.cfg
func show_online_count() -> bool:
	var v: Variant = get_state("launcher", "show_online_count", true)
	return v if typeof(v) == TYPE_BOOL else false  # a damaged value (a string, a list ...) is off, never an error: online_count_problem() says so


## "" or what is wrong with the stored setting (the game reads the same value and treats it the same way)
func online_count_problem() -> String:
	var v: Variant = get_state("launcher", "show_online_count", true)
	return "" if typeof(v) == TYPE_BOOL else "show_online_count in state.cfg is not true or false: the player count is off until it is set again"


func set_show_online_count(on: bool) -> void:
	set_state("launcher", "show_online_count", on)


## the online lobby the launcher's links and the count use: --lobby=<url> (state online/lobby), else the release's lobby
func lobby_url() -> String:
	var kept := str(get_state("online", "lobby", "")).strip_edges()
	return kept if kept != "" else DEFAULT_LOBBY


func _save_state() -> void:
	_state_lock.lock()
	state.save(data_dir.path_join("state.cfg"))
	_state_lock.unlock()


## a value of state.cfg (read and written under a lock: the window's Play and a worker thread's install both use it); `fresh`: read the
## file again first (the list of running games: Sol r2)
func get_state(section: String, key: String, default: Variant = null, fresh := false) -> Variant:
	_state_lock.lock()
	if fresh:
		_reload_state()
	var v = state.get_value(section, key, default)
	_state_lock.unlock()
	return v


## sets a value of state.cfg and saves the file; the file is read again first under the lock (Sol r2: a stale copy in memory must never
## write back over newer values)
func set_state(section: String, key: String, value: Variant) -> void:
	_state_lock.lock()
	_reload_state()
	state.set_value(section, key, value)
	state.save(data_dir.path_join("state.cfg"))
	_state_lock.unlock()


func _reload_state() -> void:
	var fresh := ConfigFile.new()
	if fresh.load(data_dir.path_join("state.cfg")) == OK:
		state = fresh


func _report(text: String, done := -1, total := -1) -> void:
	if progress.is_valid():
		progress.call(text, done, total)


# ---- the releases ------------------------------------------------------------------------------------------------------------------------

## {ok, error, releases: [{tag, prerelease, notes, published, assets: {name: {url, size}}}] newest first, from_cache}
## Only releases whose tag is a release version are listed (drafts and other tags are skipped, and logged).
func fetch_releases() -> Dictionary:
	if macos_updates_unsupported() and not macos_preview():
		Log.line("LAUNCHER macOS automated updates are unsupported; update the complete app bundle manually")
		return {"ok": false, "error": "macOS automated updates are unsupported; unpack a newer complete bundle manually"}
	var cache := data_dir.path_join("cache")
	DirAccess.make_dir_recursive_absolute(cache)
	var etag := ""
	var cached := Manifest.read_limited(cache.path_join("releases.json"), MAX_RELEASES_JSON)
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
	var out = parse_releases(body)
	if typeof(out) != TYPE_ARRAY:
		return {"ok": false, "error": "the release list is not a JSON array"}
	if r.code == 200:
		_write(cache.path_join("releases.json"), body)
		var new_etag: String = r.headers.get("etag", "")
		if new_etag != "":
			_write(cache.path_join("releases.etag"), new_etag.to_utf8_buffer())
		elif FileAccess.file_exists(cache.path_join("releases.etag")):
			DirAccess.remove_absolute(cache.path_join("releases.etag"))
	return {"ok": true, "error": "", "releases": out, "from_cache": from_cache}


## lane UI-5: the release list of the last check (cache/releases.json), for the notes cards before a check has answered or offline; []
## when there is none. Never trusted for an install: an install reads the release's signed manifest.
func cached_releases() -> Array:
	var out = parse_releases(Manifest.read_limited(data_dir.path_join("cache/releases.json"), MAX_RELEASES_JSON), true)
	return out if typeof(out) == TYPE_ARRAY else []


## the API's release list as [{tag, prerelease, notes, published, assets}] newest first, or null when it is not a JSON array
func parse_releases(body: PackedByteArray, quiet := false):
	var parsed = JSON.parse_string(body.get_string_from_utf8()) if not body.is_empty() else null
	if typeof(parsed) != TYPE_ARRAY:
		return null
	var out := []
	for rel in parsed:
		if typeof(rel) != TYPE_DICTIONARY or typeof(rel.get("tag_name")) != TYPE_STRING:
			continue
		if rel.get("draft", false):
			continue
		var tag: String = rel.tag_name
		if not Semver.is_valid(tag):
			if not quiet:
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
	return out


## release notes as plain text: no control characters but newlines and tabs, at most MAX_NOTES characters (shown in a label with no
## markup: nothing remote is ever rendered as HTML, BBCode or an image). The release body is Markdown whose note lines
## tools/release/autorelease.py writes as literal text (markdown(): a backslash before every special character, &amp; &lt; &gt;); a
## label shows those escapes as they are ("start\-up", lane RELTEST-1), so they are undone here: a backslash before ASCII punctuation
## (CommonMark's backslash escape) and the three entities, in one pass (the result is never unescaped again).
static func plain_text(s: String) -> String:
	var out := ""
	var text := s.left(MAX_NOTES)
	var i := 0
	while i < text.length():
		var c := text[i]
		if c == "\\" and i + 1 < text.length() and _is_ascii_punct(text.unicode_at(i + 1)):
			out += text[i + 1]
			i += 2
			continue
		if c == "&":
			var hit := false
			for e in [["&amp;", "&"], ["&lt;", "<"], ["&gt;", ">"]]:
				if text.substr(i, e[0].length()) == e[0]:
					out += e[1]
					i += e[0].length()
					hit = true
					break
			if hit:
				continue
		var u := c.unicode_at(0)
		if u >= 0x20 and u != 0x7f or c == "\n" or c == "\t":
			out += c
		i += 1
	return out


static func _is_ascii_punct(u: int) -> bool:
	return (u >= 0x21 and u <= 0x2f) or (u >= 0x3a and u <= 0x40) or (u >= 0x5b and u <= 0x60) or (u >= 0x7b and u <= 0x7e)


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
	if macos_updates_enabled():
		return macos_release_manifest(release)
	return _standard_release_manifest(release)


func macos_update_on_start(args: PackedStringArray, begin_swap := true) -> Dictionary:
	return MacOSUpdate.on_start(self, args, begin_swap)


## The release's signed macOS sidecar (manifest-macos.json + .sig), cached per tag in cache/manifests-macos/<tag>/. MACPLAY-1 (Sol r1 on
## DEVPORT-6): the two files were written one after the other, and a cache holding one of them (a crash or a full disk between the
## writes) refused that release forever. Now the verified pair is written into a fresh folder beside the cache and published with one
## rename of that folder (both files or neither), and a cache entry that is incomplete or does not verify is removed and fetched again.
func macos_release_manifest(release: Dictionary) -> Dictionary:
	var dir := data_dir.path_join("cache/manifests-macos").path_join(release.tag)
	var manifest_path := dir.path_join("manifest-macos.json")
	var signature_path := dir.path_join("manifest-macos.json.sig")
	var expect := {"repo": info.repo, "tag": release.tag, "prerelease": release.prerelease}
	# MACPLAY-1 (Sol r2): a cache folder above the entry that is a link is never read, written or cleaned (its target is not ours): the
	# sidecar is fetched and verified as without a cache, and the log says why nothing is cached
	var linked := ""
	for above in [data_dir.path_join("cache"), data_dir.path_join("cache/manifests-macos")]:
		if linked == "" and _path_is_link(above):
			linked = above
	if linked != "":
		Log.line("LAUNCHER the macOS manifest cache is not used: %s is a symbolic link" % linked)
	if linked == "" and (DirAccess.dir_exists_absolute(dir) or FileAccess.file_exists(dir) or _path_is_link(dir)):
		var entry_linked := _path_is_link(dir)  # MACPLAY-1 (Sol r2): never read through it; the link alone is removed below
		var cached := Manifest.read_limited(manifest_path, Manifest.MAX_BYTES) if not entry_linked else PackedByteArray()
		var cached_sig := Manifest.read_limited(signature_path, 64) if not entry_linked else PackedByteArray()
		var cv := Manifest.verify(cached, cached_sig, info.release_keys, expect, "macos")
		if cv.ok:
			return {"ok": true, "error": "", "manifest": cv.manifest, "bytes": cached, "sig": cached_sig,
				"macos_sidecar": true, "no_entry": cv.manifest.assets.game.is_empty()}
		Log.line("LAUNCHER the cached macOS manifest of %s is %s (%s): it is fetched again" % [release.tag,
			"a symbolic link" if entry_linked else "incomplete" if not FileAccess.file_exists(manifest_path) or \
				not FileAccess.file_exists(signature_path) else "not valid", cv.error])
		remove_tree(dir)
		DirAccess.remove_absolute(dir)
	var has_manifest: bool = release.assets.has("manifest-macos.json")
	var has_signature: bool = release.assets.has("manifest-macos.json.sig") or release.assets.has("manifest-macos.json.sig2")
	if has_manifest or has_signature:
		if not has_manifest or not has_signature:
			return {"ok": false, "error": "release %s has an incomplete macOS manifest sidecar" % release.tag, "manifest": {}}
		var m := http.get_small(release.assets["manifest-macos.json"].url, Manifest.MAX_BYTES)
		if not m.ok or m.code != 200:
			return {"ok": false, "error": "the macOS manifest of %s could not be downloaded: %s" % [release.tag, m.error if not m.ok else "HTTP %d" % m.code], "manifest": {}}
		var s := release_signature(release, "manifest-macos.json", m.body, "macos")
		if not s.ok:
			return {"ok": false, "error": "release %s rejected: %s" % [release.tag, s.error], "manifest": {}}
		var v := Manifest.verify(m.body, s.sig, info.release_keys, expect, "macos")
		if not v.ok:
			return {"ok": false, "error": "release %s rejected: %s" % [release.tag, v.error], "manifest": {}}
		var why := _cache_pair(dir, "manifest-macos.json", m.body, s.sig) if linked == "" else ""
		if why != "":
			# the verified sidecar is used for this check; only its cache failed (the next check fetches it again)
			Log.line("LAUNCHER the verified macOS manifest of %s was not cached: %s" % [release.tag, why])
		return {"ok": true, "error": "", "manifest": v.manifest, "bytes": m.body, "sig": s.sig,
			"macos_sidecar": true, "no_entry": v.manifest.assets.game.is_empty()}
	var legacy := _standard_release_manifest(release)
	if not legacy.ok:
		return legacy
	legacy.macos_sidecar = false
	legacy.no_entry = true
	return legacy


## MACPLAY-1: `name` and `name`.sig into `dir` as one step: written into a fresh sibling folder, then that folder renamed to `dir` (a
## crash leaves either no `dir` or the complete pair; a leftover temporary folder is never read). "" or what failed.
func _cache_pair(dir: String, name: String, body: PackedByteArray, sig: PackedByteArray) -> String:
	var tmp := "%s.tmp-%d-%d" % [dir, OS.get_process_id(), Time.get_ticks_usec()]
	if DirAccess.make_dir_recursive_absolute(tmp) != OK:
		return "cannot create %s" % tmp
	if not _write(tmp.path_join(name), body) or not _write(tmp.path_join(name + ".sig"), sig):
		remove_tree(tmp)
		return "writing failed (is the disk full?)"
	test_point("manifest-cache-written")
	remove_tree(dir)
	if DirAccess.rename_absolute(tmp, dir) != OK:
		remove_tree(tmp)
		return "the folder could not be renamed into place"
	return ""


func _standard_release_manifest(release: Dictionary) -> Dictionary:
	var dir := data_dir.path_join("cache/manifests").path_join(release.tag)
	var expect := {"repo": info.repo, "tag": release.tag, "prerelease": release.prerelease}
	var bytes := Manifest.read_limited(dir.path_join("manifest.json"), Manifest.MAX_BYTES)
	var sig := Manifest.read_limited(dir.path_join("manifest.json.sig"), 64)
	if not bytes.is_empty() and sig.size() == 64:
		var v := Manifest.verify(bytes, sig, info.release_keys, expect)
		if v.ok:
			return {"ok": true, "error": "", "manifest": v.manifest, "bytes": bytes, "sig": sig}
	if not release.assets.has("manifest.json"):
		return {"ok": false, "error": "the release %s has no manifest.json (not a launcher release)" % release.tag}
	if not release.assets.has("manifest.json.sig") and not release.assets.has("manifest.json.sig2"):
		return {"ok": false, "error": "the release %s has no manifest.json.sig or manifest.json.sig2 (not a launcher release)" % release.tag}
	var m := http.get_small(release.assets["manifest.json"].url, Manifest.MAX_BYTES)
	if not m.ok or m.code != 200:
		return {"ok": false, "error": "the manifest of %s could not be downloaded: %s" % [release.tag, m.error if not m.ok else "HTTP %d" % m.code]}
	var s := release_signature(release, "manifest.json", m.body, "standard")
	if not s.ok:
		return {"ok": false, "error": "release %s rejected: %s" % [release.tag, s.error]}
	var v := Manifest.verify(m.body, s.sig, info.release_keys, expect)
	if not v.ok:
		return {"ok": false, "error": "release %s rejected: %s" % [release.tag, v.error]}
	DirAccess.make_dir_recursive_absolute(dir)
	_write(dir.path_join("manifest.json"), m.body)
	_write(dir.path_join("manifest.json.sig"), s.sig)
	return {"ok": true, "error": "", "manifest": v.manifest, "bytes": m.body, "sig": s.sig}


## lane SIGNKMS-1: the signature of the release file `name` (manifest.json / manifest-macos.json, its bytes `body`, checked in
## `schema_mode`) from the release's two slots: `name`.sig (what every launcher reads) when it is there and valid under the trusted keys,
## else `name`.sig2 (read by launchers with the key list only: the KMS envelope signature). Each slot is read with the 64-byte limit.
## Whichever verified is what the caller stores, under the plain `name`.sig (caches, versions/<v>/, the launcher's proof, receipts).
## {ok, error, sig, slot}; neither valid fails, naming both slots.
func release_signature(release: Dictionary, name: String, body: PackedByteArray, schema_mode: String) -> Dictionary:
	var tried := PackedStringArray()
	for slot in [name + ".sig", name + ".sig2"]:
		if not release.assets.has(slot):
			tried.append("%s: not in the release" % slot)
			continue
		var s := http.get_small(release.assets[slot].url, 64)
		if not s.ok or s.code != 200:
			tried.append("%s: could not be downloaded (%s)" % [slot, s.error if not s.ok else "HTTP %d" % s.code])
		elif s.body.size() != 64:
			tried.append("%s: %d bytes (an Ed25519 signature is 64)" % [slot, s.body.size()])
		elif Manifest.signature_ok(body, s.body, info.release_keys, schema_mode):
			return {"ok": true, "error": "", "sig": s.body, "slot": slot}
		else:
			tried.append("%s: not valid" % slot)
	var why := Manifest.not_signed_error(info.release_keys) if Manifest.has_keys(info.release_keys) else "this launcher has no release key"
	return {"ok": false, "error": "%s (%s)" % [why, "; ".join(tried)], "sig": PackedByteArray(), "slot": ""}


# ---- installed versions ------------------------------------------------------------------------------------------------------------------

## [{version, path (the game folder), manifest}] of the verified installed versions, newest first; problems are logged and the version
## is not offered (it is not deleted: the user may look at it)
func installed() -> Array:
	if macos_preview():
		return []
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
	return _verify_install_at(data_dir.path_join("versions").path_join(name), name, full)


func _verify_install_at(dir: String, name: String, full: bool) -> Dictionary:
	if macos_preview():
		return {"ok": false, "error": "Mac preview starts only its bundled game; updates are manual"}
	if not Semver.is_valid(name):
		return {"ok": false, "error": "not a version folder"}
	# MACPLAY-1: a Mac version folder holds the signed manifest-macos.json it was installed from (stored as manifest.json), "macos" schema
	var v := Manifest.verify(Manifest.read_limited(dir.path_join("manifest.json"), Manifest.MAX_BYTES),
		Manifest.read_limited(dir.path_join("manifest.json.sig"), 64), info.release_keys, {"repo": info.repo},
		"macos" if mac_platform() else "standard")
	if not v.ok:
		return {"ok": false, "error": v.error}
	if v.manifest.version != name:
		return {"ok": false, "error": "its manifest is for %s" % v.manifest.version}
	if not v.manifest.assets.game.has(info.platform()):
		return {"ok": false, "error": "its signed manifest has no %s package" % info.platform()}
	var files: Dictionary = v.manifest.assets.game[info.platform()].files
	if not files.has(GAME_EXE[info.platform()]):
		return {"ok": false, "error": "the package has no %s" % GAME_EXE[info.platform()]}
	if full:
		var why := MacOSUpdate.link_error(dir.path_join("game"), files) if mac_platform() else ""
		if why == "":
			why = Manifest.check_files(dir.path_join("game"), files, [], false, mac_platform())  # MACFIX-1: Finder's files on a Mac
		if why == "" and mac_platform():
			why = MacOSUpdate._verify_apps(dir.path_join("game"))  # the apps' own seals: Apple Silicon runs only validly signed code
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


## the newest version this launcher can start: the newest installed one, or the game beside the launcher when that is newer (lane
## ONEZIP-1: the combined package needs no first download, and an automatic update never fetches its own version again)
func newest_installed() -> String:
	var inst := installed()
	var newest: String = "" if inst.is_empty() else inst[0].version
	var b := bundled()
	if not b.is_empty() and (newest == "" or Semver.compare(b.version, newest) > 0):
		return b.version
	return newest


## the version Play starts: the user's pick when it is installed, else the newest installed (or the game beside the launcher while
## nothing newer is installed)
func play_version() -> String:
	var sel := selected()
	for i in playable():
		if i.version == sel:
			return sel
	return newest_installed()


## MACPLAY-1: why Play has nothing to start, and what the player can do (shown by Play and --play; never an empty answer)
func no_game_reason() -> String:
	if translocated != "":
		return TRANSLOCATED_TEXT
	var problem := bundled_problem()
	if problem != "":
		return ("The game beside the launcher is not the signed package (%s), and no verified version is installed. Download the " +
			"package again from %s, or let the launcher download the game (Check for updates).") % [problem, releases_url()]
	if launcher_dir == "" and info.packaged:
		return "No verified version is installed. The launcher downloads the newest one when it checks for updates (Check for updates)."
	return ("No game is installed yet: there is no signed game beside the launcher and no verified version in the launcher's data " +
		"folder. The launcher downloads the newest release when it checks for updates (Check for updates; it needs an internet connection).")


## the versions Play can start, newest first: the installed ones and the game beside the launcher ({version, path, bundled: true})
## when that version is not installed
func playable() -> Array:
	var out := installed()
	var b := bundled()
	if not b.is_empty() and not out.any(func(i): return i.version == b.version):
		out.append({"version": b.version, "path": b.path, "bundled": true})
		out.sort_custom(func(x, y): return Semver.compare(x.version, y.version) > 0)
	return out


func replay_builds() -> Array:
	var out := []
	for build in playable():
		var commit := str(build.get("manifest", {}).get("commit", ""))
		var version: String = build.version
		var record := Manifest.read_limited(build.path.path_join("VERSION"), 4096).get_string_from_utf8()
		for line in record.split("\n"):
			if line.begins_with("commit "):
				if build.get("bundled", false):
					commit = line.substr(7).strip_edges()
			elif line.begins_with("OpenBFME "):
				version = line.substr(9)
		out.append({"version": version, "commit": commit, "installed_version": build.version})
	return out


func open_replay(path: String) -> Dictionary:
	return open_recording(path)


func open_recording(path: String) -> Dictionary:
	var absolute := path if path.is_absolute_path() else DirAccess.open(".").get_current_dir().path_join(path).simplify_path()
	var replay := preload("res://scripts/core/replay_open.gd")
	var builds := replay_builds()
	var pick: Dictionary = replay.select_build(replay.read_header(absolute), builds)
	if not pick.ok:
		return pick
	return play(pick.version, PackedStringArray(["--", pick.argument, absolute]))


# ---- the game beside the launcher (lane ONEZIP-1) ----------------------------------------------------------------------------------------
## The combined package carries its signed payload inventory (payload.json + payload.json.sig, Manifest.verify_payload; written by
## tools/release/seal_package.py before the archive is finished). The game beside the launcher is offered only when that inventory
## verifies under the release key; before Play every file is checked against it, strictly: a changed or missing file, or a file that
## is neither the package's nor one of the launcher's own (bundle_extras), refuses the start by name. The launcher's executable is the
## package's, or a newer signed launcher a self-update put there, proven by that release's signed manifest beside it (PROOF).

const PROOF := "launcher-manifest.json"
const PROOF_SIG := "launcher-manifest.json.sig"

var _bundled_said := ""
var _bundled_cache := {}   # the payload's bytes and signature -> the verified result (a verification costs a signature check)


## the files that may lie in the package's folder besides its signed ones: the payload, the launcher's proof, its update in progress
func bundle_extras() -> Array:
	var exe: String = LAUNCHER_EXE[info.platform()]
	return [Manifest.PAYLOAD, Manifest.PAYLOAD_SIG, PROOF, PROOF_SIG, "update/" + exe, "update/VERSION.launcher", exe + ".old", exe + ".new",
		exe + ".old.tmp", exe + ".new.tmp", PROOF + ".tmp", PROOF_SIG + ".tmp", "update.partial/" + exe, "update.partial/VERSION.launcher"]


## {version, path, files} of the game in the launcher's own folder (the combined package as it was unpacked), {} when there is none
## or its payload inventory does not verify (said once in the log, by name; never guessed)
func bundled() -> Dictionary:
	if launcher_dir == "" or not FileAccess.file_exists(launcher_dir.path_join(GAME_EXE[info.platform()])):
		return {}
	if macos_updates_enabled():
		if MacOSUpdate.signed_required(self):
			var receipt := MacOSUpdate.signed_receipt(self, launcher_dir)
			if not receipt.ok:
				_bundled_said = receipt.error
				if _bundled_said != "":
					Log.line("LAUNCHER the signed Mac package is not offered: " + _bundled_said)
				return {}
			_bundled_said = ""
			return {"version": receipt.manifest.version, "path": launcher_dir, "files": receipt.files}
		# MACPLAY-1: the unpacked Mac package is offered like the Windows / Linux ones: only when its signed payload inventory verifies
		# (DEVPORT-6 offered it on its VERSION line and a pack file alone, with no signed data at all)
	if macos_preview():
		var version_file := Manifest.read_limited(launcher_dir.path_join("VERSION"), 4096).get_string_from_utf8()
		if version_file.get_slice("\n", 0).strip_edges() != "OpenBFME " + String(info.version):
			_bundled_said = "the Mac preview VERSION does not match this launcher"
			return {}
		if not FileAccess.file_exists(launcher_dir.path_join("OpenBFME.app/Contents/Resources/OpenBFME.pck")):
			_bundled_said = "the bundled Mac preview game pack is missing"
			return {}
		return {"version": info.version, "path": launcher_dir, "files": {}}
	# sizes first (Sol r2): nothing larger than the limits is read whole
	var pr := Manifest.read_bounded(launcher_dir.path_join(Manifest.PAYLOAD), Manifest.MAX_BYTES)
	var sr := Manifest.read_bounded(launcher_dir.path_join(Manifest.PAYLOAD_SIG), 64)
	var bytes: PackedByteArray = pr.bytes
	var sig: PackedByteArray = sr.bytes
	var key := bytes.hex_encode() + ":" + sig.hex_encode() + ":%d:%d" % [pr.size, sr.size]
	if not _bundled_cache.has(key):
		var v := Manifest.verify_payload(bytes, sig, info.release_keys, {"repo": info.repo, "platform": info.platform()})
		if pr.size > Manifest.MAX_BYTES:
			v = {"ok": false, "error": "%s is %d bytes (at most %d)" % [Manifest.PAYLOAD, pr.size, Manifest.MAX_BYTES]}
		elif sr.size > 64:
			v = {"ok": false, "error": "%s is %d bytes (an Ed25519 signature is 64)" % [Manifest.PAYLOAD_SIG, sr.size]}
		var why: String = v.error
		if v.ok:
			var first := FileAccess.get_file_as_string(launcher_dir.path_join("VERSION")).get_slice("\n", 0).strip_edges()
			# the Mac package's VERSION line carries a label after the version ("OpenBFME v0.4.7 Mac Unstable Preview (Apple Silicon)")
			var named := first.trim_prefix("OpenBFME ").get_slice(" ", 0) if mac_platform() and first.begins_with("OpenBFME ") else first
			if (mac_platform() and named != String(v.payload.version)) or (not mac_platform() and first != "OpenBFME " + String(v.payload.version)):
				why = "its VERSION file (%s) is not the signed version %s" % [first.left(60).c_escape(), v.payload.version]
		_bundled_cache = {key: {} if why != "" else {"version": v.payload.version, "path": launcher_dir, "files": v.payload.files}}
		if why != "" and _bundled_said != why:
			_bundled_said = why
			Log.line("LAUNCHER the game beside the launcher is not offered: " + why)
	var b: Dictionary = _bundled_cache[key]
	return b.duplicate() if not b.is_empty() else {}


## why the game beside the launcher is not offered ("" when it is, or when there is none)
func bundled_problem() -> String:
	return _bundled_said if bundled().is_empty() else ""


## Lane ONEZIP-1 r2: a game started directly starts an installed version itself (game_start.gd): that version is checked file by file
## against its signed manifest (and, on Linux, the system's GLIBC), and the versions lock is taken so that nothing removes or replaces
## it before the game reports its process id (record_started) or the hold ends (unlock_versions). {ok, error}
func hold_for_start(version: String) -> Dictionary:
	var v := verify_installed(version, true)
	if not v.ok:
		return {"ok": false, "error": v.error}
	var dir := data_dir.path_join("versions").path_join(version).path_join("game")
	if info.os_name() == "Linux":
		var glibc := GlibcCheck.check(dir, OS.get_environment("OPENBFME_TEST_SYSTEM_GLIBC") if info.tests_allowed() else "")
		if glibc != "":
			return {"ok": false, "error": glibc}
	if not lock_versions():
		return {"ok": false, "error": "another launcher holds the versions folder (%s)" % LOCK}
	if not verify_installed(version).ok:
		unlock_versions()
		return {"ok": false, "error": "it was removed or replaced while it was checked"}
	return {"ok": true, "error": ""}


## records a game of `version` the game started (under hold_for_start's lock): prune never removes a version that is played. false
## when `pid` is not a running game of that version
func record_started(version: String, pid: int) -> bool:
	if not _game_runs(pid, version):
		Log.line("LAUNCHER process %d is not a running game of %s: not recorded" % [pid, version])
		return false
	var runs := _live_runs()
	var pids: Array = runs.get(version, [])
	pids.append(pid)
	runs[version] = pids
	set_state("launcher", "running", runs)
	Log.line("LAUNCHER the game started %s (pid %d)" % [version, pid])
	return true


## whether Play of `version` starts the game beside the launcher (that version is not installed in the data folder)
func plays_bundled(version: String) -> bool:
	var b := bundled()
	return not b.is_empty() and b.version == version and (macos_preview() or not DirAccess.dir_exists_absolute(data_dir.path_join("versions").path_join(version)))


## Every file of the game beside the launcher against its signed payload inventory, and the launcher's executable against signed data
## (check_launcher). {ok, error}
func check_bundled() -> Dictionary:
	var b := bundled()
	if b.is_empty():
		var error := bundled_problem()
		if error != "":
			return {"ok": false, "error": error}
		return {"ok": false, "error": "there is no signed Mac package beside the launcher" if macos_updates_enabled() else
			"the bundled Mac preview is missing or its VERSION does not match" if macos_preview() else "there is no signed game beside the launcher"}
	if macos_preview():
		Log.line("LAUNCHER Mac preview: starting the bundled game without signed-manifest verification; updates are manual")
		return {"ok": true, "error": ""}
	if macos_updates_enabled():
		var transaction_error := MacOSUpdate.play_blocker(self)
		if transaction_error != "":
			return {"ok": false, "error": transaction_error}
		if MacOSUpdate.signed_required(self):
			var verified := MacOSUpdate.signed_receipt(self, b.path)
			return {"ok": verified.ok, "error": verified.error}
		if macos_transaction or MacOSUpdate.transaction_locked(self):
			return {"ok": false, "error": "a signed whole-package update is in progress"}
		# MACPLAY-1: every signed file by size and SHA-256 (the apps' code seals, _CodeSignature/CodeResources, are signed files too),
		# then the seals themselves: codesign refuses a file added to or removed from an app (MACFIX-1: Finder / iCloud extended
		# attributes are not refused, MacOSUpdate._codesign). Files outside the two apps that are not signed (Finder's .DS_Store) are
		# never run, so they are not refused.
		var why_mac := MacOSUpdate.link_error(b.path, b.files)
		if why_mac == "":
			why_mac = Manifest.check_files(b.path, b.files, [], true)
		if why_mac == "":
			why_mac = MacOSUpdate._verify_apps(b.path)
		return {"ok": why_mac == "", "error": why_mac}
	var exe: String = LAUNCHER_EXE[info.platform()]
	var files: Dictionary = b.files.duplicate()
	files.erase(exe)
	var why := Manifest.check_files(b.path, files, bundle_extras() + [exe])
	if why == "":
		why = check_launcher(b.path, b.files.get(exe, {}))
	return {"ok": why == "", "error": why}


## "" when the launcher executable in `dir` is a signed one: the package's own (`signed`: its entry in the payload) or the launcher a
## self-update put there, whose release's signed manifest lies beside it (PROOF, written when the new launcher confirmed its start)
func check_launcher(dir: String, signed: Dictionary) -> String:
	var exe: String = LAUNCHER_EXE[info.platform()]
	var path := dir.path_join(exe)
	if not FileAccess.file_exists(path):
		return "%s is missing" % exe
	var d := DirAccess.open(dir)
	if d != null and d.is_link(exe):
		return "%s is a link, not the signed file" % exe
	var sha := _sha256_of(path)
	if not signed.is_empty() and sha == signed.sha256:
		return ""
	var v := Manifest.verify(Manifest.read_limited(dir.path_join(PROOF), Manifest.MAX_BYTES), Manifest.read_limited(dir.path_join(PROOF_SIG), 64),
		info.release_keys, {"repo": info.repo})
	if v.ok:
		var src := Manifest.launcher_source(v.manifest, info.platform(), exe)
		if src.ok and sha == (src.file.sha256 if src.combined else String(src.asset.files.get(exe, {}).get("sha256", ""))):
			return ""
	return "%s is neither the package's nor a signed launcher update (%s)" % [exe, v.error if not v.ok else "its SHA-256 is not the signed one"]


## a confirmed launcher update in a combined package's folder: the signed manifest of its release beside it (PROOF), so check_launcher
## and the game (godot/scripts/release/launcher_check.gd) can tell it is signed. Written through a temporary file and a rename.
func write_launcher_proof(dir: String, tag: String) -> bool:
	if not FileAccess.file_exists(dir.path_join(GAME_EXE[info.platform()])):
		return true   # a launcher-only folder needs none
	var cache := data_dir.path_join("cache/manifests").path_join(tag)
	var bytes := Manifest.read_limited(cache.path_join("manifest.json"), Manifest.MAX_BYTES)
	var sig := Manifest.read_limited(cache.path_join("manifest.json.sig"), 64)
	if not Manifest.verify(bytes, sig, info.release_keys, {"repo": info.repo, "tag": tag}).ok:
		Log.line("LAUNCHER no verified manifest of %s to prove the launcher beside the game" % tag)
		return false
	for item in [[PROOF_SIG, sig], [PROOF, bytes]]:
		var tmp: String = dir.path_join(item[0] + ".tmp")
		if not _write(tmp, item[1]) or (FileAccess.file_exists(dir.path_join(item[0])) and DirAccess.remove_absolute(dir.path_join(item[0])) != OK) \
				or DirAccess.rename_absolute(tmp, dir.path_join(item[0])) != OK:
			DirAccess.remove_absolute(tmp)
			Log.line("LAUNCHER the proof of the launcher %s could not be written beside the game (is the folder writable, the disk full?)" % tag)
			return false
	return true


static func _sha256_of(path: String) -> String:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return ""
	var hc := HashingContext.new()
	hc.start(HashingContext.HASH_SHA256)
	while f.get_position() < f.get_length():
		hc.update(f.get_buffer(1 << 20))
	return hc.finish().hex_encode()


func _play_bundled(b: Dictionary, args: PackedStringArray, before_start := Callable()) -> Dictionary:
	if macos_updates_enabled() and (macos_transaction or MacOSUpdate.transaction_locked(self)):
		return {"ok": false, "pid": -1, "modified": false, "error": "a signed whole-package update is in progress"}
	var c := check_bundled()
	if not c.ok:
		return {"ok": false, "pid": -1, "modified": true, "change": c.error, "error": ("%s (the game beside the launcher) is not the signed package " +
			"(%s): it is not started; download it again (--install=%s, or Play to confirm)") % [b.version, c.error, b.version]}
	var exe: String = String(b.path).path_join(GAME_EXE[info.platform()])
	if info.os_name() == "Linux":
		var glibc := GlibcCheck.check(b.path, OS.get_environment("OPENBFME_TEST_SYSTEM_GLIBC") if info.tests_allowed() else "")
		if glibc != "":
			return {"ok": false, "error": "%s cannot be started: %s" % [b.version, glibc], "pid": -1}
	if before_start.is_valid() and not before_start.call():
		return {"ok": false, "error": "The game launch was canceled.", "pid": -1, "stopped": true}
	OS.set_environment(STARTED_BY_LAUNCHER, "1")
	var pid := OS.create_process(exe, args)
	if pid <= 0:
		return {"ok": false, "error": "%s could not be started" % exe, "pid": -1}
	Log.line("LAUNCHER started %s, the game beside the launcher (pid %d)" % [b.version, pid])
	return {"ok": true, "error": "", "pid": pid}


# ---- installing --------------------------------------------------------------------------------------------------------------------------

## Install `release`'s game package. `user_pick`: the user chose this version (an older one is allowed then); otherwise (an automatic
## update) only a version newer than every installed one is installed. {ok, error, version}
func install(release: Dictionary, user_pick: bool) -> Dictionary:
	# MACPLAY-1: a Mac launcher beside its verified package updates that package as a whole (DEVPORT-6's staged swap of both apps); a
	# launcher with no verified game beside it (alone, or the package's inventory does not verify) installs the signed Mac package into
	# versions/<v>/game like the Windows / Linux launchers. A translocated copy installs nothing (TRANSLOCATED_TEXT).
	if mac_swap_root():
		var r := MacOSUpdate.new(self).install(release, user_pick)
		r.swap = true
		return r
	if macos_updates_enabled() and translocated != "":
		return {"ok": false, "error": TRANSLOCATED_TEXT, "version": ""}
	if macos_updates_unsupported():
		return {"ok": false, "error": "macOS automated updates are unsupported; unpack a newer complete app bundle manually", "version": ""}
	var newest := newest_installed()
	if not user_pick and newest != "" and Semver.compare(release.tag, newest) <= 0:
		return {"ok": false, "error": "%s is not newer than the installed %s: an automatic update never installs an older or the same version (pick it yourself to roll back)" % [release.tag, newest], "version": ""}
	test_point("install-checked")
	var mm := release_manifest(release)
	if not mm.ok:
		return {"ok": false, "error": mm.error, "version": ""}
	test_point("manifest-verified")
	var m: Dictionary = mm.manifest
	if mm.get("no_entry", false) or not m.assets.game.has(info.platform()):
		return {"ok": false, "error": "the release %s has no signed %s package (download a complete package from %s)" % [release.tag, info.platform(), releases_url()], "version": ""}
	var asset: Dictionary = m.assets.game[info.platform()]
	var got := fetch_asset(release, asset)
	if not got.ok:
		return {"ok": false, "error": got.error, "version": ""}
	var root := data_dir.path_join("versions")
	var partial := root.path_join(m.version + ".partial")
	remove_tree(partial)
	_report("Unpacking %s" % asset.name)
	test_point("install-unpacking")
	var x := Archive.extract(got.path, "zip" if asset.name.ends_with(".zip") else "tar.gz", Manifest.top_folder(asset.name), partial.path_join("game"))
	if not x.ok:
		remove_tree(partial)
		return {"ok": false, "error": "%s was not installed: %s" % [m.version, x.error], "version": ""}
	var differ := same_files(x.files, asset.files)
	if differ == "" and mac_platform():
		differ = _mac_ready(partial.path_join("game"))
	if differ != "":
		remove_tree(partial)
		return {"ok": false, "error": "%s was not installed: %s" % [m.version, differ], "version": ""}
	test_point("install-extracted")
	for item in [["manifest.json", mm.bytes], ["manifest.json.sig", mm.sig]]:
		if not _write(partial.path_join(item[0]), item[1]):
			remove_tree(partial)
			return {"ok": false, "error": "%s was not installed: writing %s failed (is the disk full?)" % [m.version, item[0]], "version": ""}
	test_point("install-unpacked")
	# replacing an installed folder (Sol r1: the old code deleted it before its replacement was in place): the installed folder is set
	# aside as <v>.old, the new one renamed into place, and only then the old one removed; recover() completes or undoes this after a
	# crash at any point
	# lane UI-5 (Sol r1): the versions lock keeps Play (any launcher) from starting a version while it is replaced or pruned, and a
	# version whose game runs is never replaced
	if not lock_versions():
		remove_tree(partial)
		return {"ok": false, "error": "%s was not installed: another launcher holds the versions folder (%s)" % [m.version, LOCK], "version": ""}
	if m.version in running():
		unlock_versions()
		remove_tree(partial)
		return {"ok": false, "error": "%s is being played: close the game, then install it again" % m.version, "version": ""}
	var final := root.path_join(m.version)
	var aside := root.path_join(m.version + ".old")
	var had := DirAccess.dir_exists_absolute(final)
	if had:
		if DirAccess.rename_absolute(final, aside) != OK:
			unlock_versions()
			remove_tree(partial)
			return {"ok": false, "error": "%s is installed and could not be replaced (is the game running?)" % m.version, "version": ""}
		test_point("install-aside")
	if DirAccess.rename_absolute(partial, final) != OK:
		var why := "%s could not be moved into place (is the folder writable, is the disk full?)" % m.version
		if had:
			if DirAccess.rename_absolute(aside, final) != OK:
				why += "; the previous copy is kept in %s.old and recovery will retry on the next start" % m.version
		unlock_versions()
		remove_tree(partial)
		return {"ok": false, "error": why, "version": ""}
	test_point("install-placed")
	if had:
		remove_tree(aside)
	DirAccess.remove_absolute(got.path)
	test_point("install-committed")
	Log.line("LAUNCHER installed %s (%s, %d files)" % [m.version, asset.name, x.files.size()])
	if user_pick:
		set_selected(m.version)
	else:
		set_selected("")  # a new build replaces an earlier rollback pick
	test_point("install-selected")
	_prune_locked()
	test_point("install-pruned")
	unlock_versions()
	return {"ok": true, "error": "", "version": m.version}


## MACPLAY-1: an unpacked Mac package before it is installed: Archive.extract kept the executable bits (the ZIP's Unix modes; the release
## ZIPs hold no links: make_archive.py refuses them and Archive refuses a link member), both apps' ad-hoc code seals verify (Apple Silicon
## runs only validly signed code) and nothing carries the download quarantine (the launcher downloads with Godot's HTTP client, which sets
## none; it is removed anyway, then checked again). "" or what is wrong.
func _mac_ready(root: String) -> String:
	var why := MacOSUpdate._verify_apps(root)
	if why == "" and not MacOSUpdate._clear_tree_quarantine(root):
		why = "the download quarantine could not be removed from the unpacked package"
	if why == "":
		why = MacOSUpdate._verify_apps(root)
	return why


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
		test_point("asset-downloading")
		if done == total:
			Log.line("LAUNCHER downloaded %s: %d bytes in %.1f s" % [asset.name, total, secs]))
	if r.resumed > 0:
		Log.line("LAUNCHER resumed %s at %d bytes" % [asset.name, r.resumed])
	if not r.ok:
		return {"ok": false, "error": "%s: %s" % [asset.name, r.error]}
	test_point("asset-downloaded")
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
	test_point("asset-verified")
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
	if not lock_versions(5000):
		Log.line("LAUNCHER another launcher holds %s: the interrupted-install check waits for the next start" % LOCK)
		return
	_recover_locked(root)
	unlock_versions()


func _recover_locked(root: String) -> void:
	for name in DirAccess.get_directories_at(root):
		if name.ends_with(".partial"):
			Log.line("LAUNCHER removed an unfinished install (%s)" % name)
			remove_tree(root.path_join(name))
		elif name.ends_with(".old"):
			var v := name.trim_suffix(".old")
			var final := root.path_join(v)
			var aside := root.path_join(name)
			if DirAccess.dir_exists_absolute(final) and verify_installed(v, true).ok:
				Log.line("LAUNCHER finished replacing %s (removed the previous copy)" % v)
				remove_tree(aside)
			elif _verify_install_at(aside, v, true).ok:
				var partial := root.path_join(v + ".partial")
				remove_tree(partial)
				var had := DirAccess.dir_exists_absolute(final) or FileAccess.file_exists(final)
				if had and DirAccess.rename_absolute(final, partial) != OK:
					Log.line("LAUNCHER could not restore %s: the replacement could not be set aside (is the folder writable?); the previous copy is kept" % v)
					continue
				if DirAccess.rename_absolute(aside, final) == OK:
					remove_tree(partial)
					Log.line("LAUNCHER restored %s: replacing it was interrupted" % v)
				else:
					if had:
						DirAccess.rename_absolute(partial, final)
					Log.line("LAUNCHER could not restore %s (is the folder writable?); the previous copy is kept for the next start" % v)
			else:
				Log.line("LAUNCHER cannot recover %s: neither copy verifies against its signed manifest; both are kept" % v)


## Test builds only (BuildInfo.tests_allowed): --test-kill-at=<point> kills this process abruptly at that point, for the crash tests.
static var kill_at := ""
static var test_hook := Callable()


static func test_point(name: String) -> void:
	if test_hook.is_valid():
		test_hook.call(name)
	if kill_at == name:
		Log.line("LAUNCHER test kill at " + name)
		if OS.get_name() == "macOS":
			MacOSUpdate.kill_self()  # MACPLAY-1: OS.kill of its own process does not end it there
		else:
			OS.kill(OS.get_process_id())
		OS.delay_msec(10000)


## keep the KEEP_VERSIONS newest versions, the user's pick and every version whose game runs; remove the others
func prune() -> void:
	if lock_versions():
		_prune_locked()
		unlock_versions()


func _prune_locked() -> void:
	var root := data_dir.path_join("versions")
	var keep := {}
	var inst := installed()
	for i in mini(KEEP_VERSIONS, inst.size()):
		keep[inst[i].version] = true
	if selected() != "":
		keep[selected()] = true
	for v in running():
		keep[v] = true  # lane UI-5: an update installed while a version is played never removes that version
	for name in DirAccess.get_directories_at(root):
		if Semver.is_valid(name) and not keep.has(name) and inst.any(func(i): return i.version == name):
			Log.line("LAUNCHER removed the old version %s (the two newest are kept)" % name)
			remove_tree(root.path_join(name))


# ---- starting the game -------------------------------------------------------------------------------------------------------------------

## Start `version`'s game executable as its own process (Play passes no arguments: the game's first-run screen finds the game files; lane ONLINE-2:
## an openbfme://join link passes --online-join / --online-lobby). {ok, error, pid}
## Every file of the version is checked against the signed manifest first (round 3): a changed file is never started; the caller offers
## a fresh download ({modified: true}).
## lane ONEZIP-1: `before_start` (optional) is asked right before the process is started, after every check: false stops the start
## ({ok: false, stopped: true}; the game that asked for this start went on by itself, main.gd --game-start).
## Lane MODS-3: with RotWK (not BFME1, which mounts no mods), the mod picked in Mods (user://mods/active.cfg) is passed as --mod-install= /
## --mod= (scripts/core/mod_install.gd active_args); a picked mod that cannot be started is an error, never a start without it.
func game_arguments(args: PackedStringArray = PackedStringArray()) -> Dictionary:
	args = args.duplicate()
	if get_state("launcher", "game", "bfme2") == "bfme1" and not Array(args).any(func(a): return a.begins_with("--online-") or a == "--replay" or a.begins_with("--replay=")):
		var folder := Paths.bfme1_folder()
		if folder.is_empty():
			return {"ok": false, "error": "BFME1 assets were not found. Download the games from Game files, or set BFME1_INSTALL."}
		if not "--" in args: args.append("--")
		args.append_array(PackedStringArray(["--game=bfme1", "--bfme1-install=" + folder]))
	elif get_state("launcher", "game", "bfme2") != "bfme1":
		var mod := ModInstall.active_args(data_dir.path_join("mods"))
		if not mod.ok:
			return {"ok": false, "error": mod.error}
		if not mod.args.is_empty():
			if not "--" in args: args.append("--")
			args.append_array(mod.args)
			Log.line("LAUNCHER the game starts with the mod %s (%s)" % [mod.name, " ".join(mod.args)])
	return {"ok": true, "args": args}


func play(version: String, args: PackedStringArray = PackedStringArray(), before_start := Callable()) -> Dictionary:
	var launch := game_arguments(args)
	if not launch.ok:
		return {"ok": false, "pid": -1, "error": launch.error}
	args = launch.args
	_report("Checking %s" % version)
	if plays_bundled(version):
		return _play_bundled(bundled(), args, before_start)
	var v := verify_installed(version, true)
	if not v.ok:
		var why := "%s cannot be started: %s" % [version, v.error]
		if v.get("modified", false):
			why = "%s was changed after it was installed (%s): it is not started; download it again (--install=%s, or Play to confirm)" % [version, v.error, version]
		return {"ok": false, "error": why, "pid": -1, "modified": v.get("modified", false), "change": v.error}
	var exe := data_dir.path_join("versions").path_join(version).path_join("game").path_join(GAME_EXE[info.platform()])
	if info.os_name() == "Linux":
		# DEP-1 (Sol r1): Linux only; platform() is "linux-x64" for every non-Windows OS, macOS included
		# lane GLIBC-1: a system older than the build's GLIBC gets a message, not a game that fails to load its library
		# (OPENBFME_TEST_SYSTEM_GLIBC: the system's version as tools/release/test_launcher.py sets it; test builds only)
		var glibc := GlibcCheck.check(exe.get_base_dir(), OS.get_environment("OPENBFME_TEST_SYSTEM_GLIBC") if info.tests_allowed() else "")
		if glibc != "":
			return {"ok": false, "error": "%s cannot be started: %s" % [version, glibc], "pid": -1}
	# lane UI-5 (Sol r1): started and recorded under the versions lock, so no install or prune (of this or another launcher) removes or
	# replaces the version between the check and the record; the full check above stays outside (it reads every file)
	if not lock_versions():
		return {"ok": false, "error": "%s was not started: another launcher holds the versions folder (%s)" % [version, LOCK], "pid": -1}
	if not verify_installed(version).ok:
		unlock_versions()
		return {"ok": false, "error": "%s was removed or replaced while it was checked: press Play again" % version, "pid": -1}
	if before_start.is_valid() and not before_start.call():
		unlock_versions()
		return {"ok": false, "error": "%s was not started: the game that asked for it went on by itself" % version, "pid": -1, "stopped": true}
	OS.set_environment(STARTED_BY_LAUNCHER, "1")
	var pid := OS.create_process(exe, args)
	if pid <= 0:
		unlock_versions()
		return {"ok": false, "error": "%s could not be started" % exe, "pid": -1}
	Log.line("LAUNCHER started %s (pid %d)" % [version, pid])
	var runs := _live_runs()
	var pids: Array = runs.get(version, [])
	pids.append(pid)
	runs[version] = pids
	set_state("launcher", "running", runs)
	unlock_versions()
	return {"ok": true, "error": "", "pid": pid}


## {version: [pid...]} of the games started (every instance: Sol r1, a second instance closing must not unprotect the first) that
## still run; state.cfg of an older launcher kept one pid per version (an int)
func _live_runs() -> Dictionary:
	var runs: Dictionary = get_state("launcher", "running", {}, true)
	var out := {}
	for v in runs:
		var pids: Array = runs[v] if typeof(runs[v]) == TYPE_ARRAY else [runs[v]]
		var live := pids.filter(func(p): return _game_runs(int(p), String(v)))
		if not live.is_empty():
			out[String(v)] = live
	return out


## The versions lock (versions.lock, a folder: making one is atomic on every system) between installing / pruning and Play, across
## threads and launcher processes; its "owner" file names the process and the time. Waits up to `wait_ms`. A lock whose owner is gone
## (Linux: no /proc/<pid>) or older than STALE_LOCK_SECONDS (a launcher that died, e.g. killed mid-install) is taken over. true when held.
const LOCK := "versions.lock"
const STALE_LOCK_SECONDS := 60


func lock_versions(wait_ms := 30000) -> bool:
	DirAccess.make_dir_recursive_absolute(data_dir)
	var path := data_dir.path_join(LOCK)
	var start := Time.get_ticks_msec()
	while DirAccess.make_dir_absolute(path) != OK:
		var why := _stale_lock(path, Time.get_ticks_msec() - start)
		if why != "":
			Log.line("LAUNCHER took over %s: %s" % [LOCK, why])
			DirAccess.remove_absolute(path.path_join("owner"))
			DirAccess.remove_absolute(path)
			continue
		if Time.get_ticks_msec() - start > wait_ms:
			return false
		OS.delay_msec(50)
	var f := FileAccess.open(path.path_join("owner"), FileAccess.WRITE)
	if f != null:
		f.store_string("%d %d\n" % [OS.get_process_id(), int(Time.get_unix_time_from_system())])
		f.close()
	return true


## "" while the lock's owner may still hold it, else why it is stale
static func _stale_lock(path: String, waited_ms: int) -> String:
	var owner := FileAccess.get_file_as_string(path.path_join("owner")).split(" ")
	if owner.size() < 2 or not owner[0].is_valid_int():
		# just being made (the owner file comes right after the folder), or left by a launcher killed in between
		return "no owner after %d s" % (waited_ms / 1000) if waited_ms > 5000 else ""
	var pid := int(owner[0])
	if pid == OS.get_process_id():
		return ""  # this launcher's other thread holds it
	if OS.get_name() != "Windows" and DirAccess.dir_exists_absolute("/proc") and not DirAccess.dir_exists_absolute("/proc/%d" % pid):
		return "its launcher (pid %d) is gone" % pid
	var age := int(Time.get_unix_time_from_system()) - int(owner[1].strip_edges())
	return "%d s old" % age if age > STALE_LOCK_SECONDS else ""


func unlock_versions() -> void:
	DirAccess.remove_absolute(data_dir.path_join(LOCK).path_join("owner"))
	DirAccess.remove_absolute(data_dir.path_join(LOCK))


## lane UI-5: the installed versions whose game this launcher started and that still run (state.cfg remembers their process ids, also
## across launcher starts); prune() keeps them, so a background update never deletes the version being played
func running() -> Array:
	return _live_runs().keys()


## tests: use the Windows check (the executable's write lock) on any system
var test_windows_lock := false


## whether process `pid` is the game of `version` (OS.is_process_running only knows this launcher's own children, and a process id is
## reused): Linux reads its command line in /proc (the executable's path names versions/<version>/); Windows starts no helper process
## (tools/release/net_guard.py): a running executable cannot be opened for writing (Windows locks the image), so the game's executable
## of that version being locked counts as running (a virus scanner holding it only keeps that version a little longer)
func _game_runs(pid: int, version: String) -> bool:
	if pid <= 0:
		return false
	if macos_updates_enabled():
		var game_root := data_dir.path_join("versions").path_join(version).path_join("game")
		return MacOSUpdate.process_under(self, pid, game_root)
	if OS.get_name() == "Windows" or test_windows_lock:
		var exe := data_dir.path_join("versions").path_join(version).path_join("game").path_join(GAME_EXE[info.platform()])
		if not FileAccess.file_exists(exe):
			return false
		var w := FileAccess.open(exe, FileAccess.READ_WRITE)
		if w == null:
			return true
		w.close()
		return false
	var f := FileAccess.open("/proc/%d/cmdline" % pid, FileAccess.READ)
	if f == null:
		return false
	var cmd := f.get_buffer(65536)  # a /proc file reports no length: read what is there
	for i in cmd.size():
		if cmd[i] == 0:
			cmd[i] = 32
	return cmd.get_string_from_utf8().contains("/versions/%s/game/" % version)


# ---- launcher self-update ----------------------------------------------------------------------------------------------------------------

func launcher_files() -> Array:
	return [LAUNCHER_EXE[info.platform()]]  # one file: the pack is embedded (self_update.gd)


## lane UI-5: whether stage_self_update(release) would stage a launcher (a packaged launcher older than `release`, not refused before,
## not staged already), without downloading anything
func launcher_update_available(release: Dictionary) -> bool:
	if mac_platform() and not mac_swap_root():
		return false  # MACPLAY-1: a Mac launcher without its package beside it does not replace itself (mac_launcher_notice)
	if launcher_dir == "" or not Semver.is_valid(release_version()) or Semver.compare(release.tag, release_version()) <= 0:
		return false
	if release.tag in get_state("launcher", "rejected_launcher", []):
		return false
	return FileAccess.get_file_as_string(launcher_dir.path_join("update").path_join("VERSION.launcher")).strip_edges() != release.tag


## Stage the launcher of `release` in <launcher folder>/update/ when it is newer than this launcher and did not fail before. The swap
## happens on the next start (apply_staged_update). {ok, error, staged (version or "")}
func stage_self_update(release: Dictionary) -> Dictionary:
	if macos_updates_enabled() and not mac_swap_root():
		var notice := mac_launcher_notice(release)
		return {"ok": true, "error": "", "staged": "", "why": notice if notice != "" else "the launcher is up to date (%s)" % release_version()}
	if macos_updates_enabled():
		return MacOSUpdate.new(self).stage_self_update(release)
	if macos_updates_unsupported():
		return {"ok": false, "error": "macOS launcher self-update is unsupported; replace both app bundles manually", "staged": ""}
	if launcher_dir == "":
		return {"ok": true, "error": "", "staged": "", "why": "this launcher has no folder of its own (a development run, or one out of an installed version): it does not update itself"}
	if not Semver.is_valid(info.version):
		return {"ok": true, "error": "", "staged": "", "why": "this launcher's version %s is not a release version: it does not update itself" % info.version}
	if Semver.compare(release.tag, info.version) <= 0:
		return {"ok": true, "error": "", "staged": "", "why": "the launcher is up to date (%s)" % info.version}
	if release.tag in get_state("launcher", "rejected_launcher", []):
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
	var src := Manifest.launcher_source(mm.manifest, info.platform(), LAUNCHER_EXE[info.platform()])
	if not src.ok:
		return {"ok": false, "error": src.error, "staged": ""}
	if src.combined:
		return _stage_from_package(release, src)
	var asset: Dictionary = src.asset
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
	test_point("self-unpacked")
	if not _write(partial.path_join("VERSION.launcher"), (release.tag + "\n").to_utf8_buffer()) \
			or DirAccess.rename_absolute(partial, staged) != OK:
		remove_tree(partial)
		return {"ok": false, "error": "the launcher %s could not be staged in %s (is the folder writable?)" % [release.tag, launcher_dir], "staged": ""}
	DirAccess.remove_absolute(got.path)
	test_point("self-staged")
	Log.line("LAUNCHER staged the launcher %s; it replaces this one on the next start" % release.tag)
	return {"ok": true, "error": "", "staged": release.tag}


## Lane ONEZIP-1: stage the launcher of a combined package: its executable is a signed file of the game package. It is copied from the
## installed version of that release when there is one (the usual case: the game update was installed first), else the package is
## downloaded and unpacked beside the downloads for this alone; the copy in update.partial/ is checked against the signed size and
## SHA-256 before it becomes update/ (and again when it is applied, check_staged).
func _stage_from_package(release: Dictionary, src: Dictionary) -> Dictionary:
	var exe: String = LAUNCHER_EXE[info.platform()]
	var asset: Dictionary = src.asset
	var from := data_dir.path_join("versions").path_join(release.tag).path_join("game").path_join(exe)
	var unpacked := ""
	var downloaded := ""
	if not verify_installed(release.tag).ok:
		var got := fetch_asset(release, asset)
		if not got.ok:
			return {"ok": false, "error": got.error, "staged": ""}
		unpacked = data_dir.path_join("downloads").path_join(Manifest.top_folder(asset.name) + ".unpack")
		remove_tree(unpacked)
		_report("Unpacking %s" % asset.name)
		var x := Archive.extract(got.path, "zip" if asset.name.ends_with(".zip") else "tar.gz", Manifest.top_folder(asset.name), unpacked)
		var differ: String = x.error if not x.ok else same_files(x.files, asset.files)
		if differ != "":
			remove_tree(unpacked)
			return {"ok": false, "error": "the launcher %s was not staged: %s" % [release.tag, differ], "staged": ""}
		from = unpacked.path_join(exe)
		downloaded = got.path
	var staged := launcher_dir.path_join("update")
	var partial := launcher_dir.path_join("update.partial")
	remove_tree(partial)
	remove_tree(staged)
	var why := ""
	if DirAccess.make_dir_recursive_absolute(partial) != OK or DirAccess.copy_absolute(from, partial.path_join(exe)) != OK:
		why = "it could not be copied into %s (is the folder writable, is the disk full?)" % launcher_dir
	else:
		why = Manifest.check_files(partial, {exe: src.file})
	if unpacked != "":
		remove_tree(unpacked)
	if why == "" and OS.get_name() != "Windows":
		FileAccess.set_unix_permissions(partial.path_join(exe), 493)  # rwxr-xr-x
	if why == "":
		test_point("self-unpacked")
	if why == "" and (not _write(partial.path_join("VERSION.launcher"), (release.tag + "\n").to_utf8_buffer()) \
			or DirAccess.rename_absolute(partial, staged) != OK):
		why = "it could not be staged in %s (is the folder writable?)" % launcher_dir
	if why != "":
		remove_tree(partial)
		return {"ok": false, "error": "the launcher %s was not staged: %s" % [release.tag, why], "staged": ""}
	if downloaded != "":
		DirAccess.remove_absolute(downloaded)
	test_point("self-staged")
	Log.line("LAUNCHER staged the launcher %s (from the package %s); it replaces this one on the next start" % [release.tag, asset.name])
	return {"ok": true, "error": "", "staged": release.tag}


## MACPLAY-1: a Mac launcher updates itself only together with the package beside it (DEVPORT-6's swap of both apps: an app bundle is a
## signed folder, not the one executable file the Windows / Linux swap replaces). A launcher whose folder the swap does not take (no
## package beside it, or apps moved out of their package folder) says when a newer launcher is out instead: "" when it is not.
func mac_launcher_notice(release: Dictionary) -> String:
	if not mac_platform() or not Semver.is_valid(release_version()) or Semver.compare(release.tag, release_version()) <= 0:
		return ""
	return ("A new launcher %s is available. This launcher does not replace itself when OpenBFME.app is not beside it: download the Mac " +
		"package from %s and keep its two apps together.") % [release.tag, releases_url()]


## the release assets a job downloads for these kinds ("game", "launcher"), for Pause / Cancel (main.gd): a combined package is the
## launcher's download too
func asset_names(manifest: Dictionary, kinds: Array) -> Array:
	var out := []
	for k in kinds:
		var name: String = manifest.assets.game[info.platform()].name
		if k == "launcher":
			var src := Manifest.launcher_source(manifest, info.platform(), LAUNCHER_EXE[info.platform()])
			if src.ok:
				name = src.asset.name
		if not name in out:
			out.append(name)
	return out


## The staged launcher of `tag` in <dir>/update/, checked against signed data at the moment it is applied (round 3: the swap used to
## trust whatever file was in update/): the cached manifest of `tag` must verify again, and every staged file must be exactly a signed
## file of the launcher package (VERSION.launcher aside). {ok, error, new_sha256 (the signed SHA-256 of the executable)}
func check_staged(dir: String, tag: String) -> Dictionary:
	var cache := data_dir.path_join("cache/manifests").path_join(tag)
	var v := Manifest.verify(Manifest.read_limited(cache.path_join("manifest.json"), Manifest.MAX_BYTES),
		Manifest.read_limited(cache.path_join("manifest.json.sig"), 64), info.release_keys, {"repo": info.repo, "tag": tag})
	if not v.ok:
		return {"ok": false, "error": "no verified manifest for the staged launcher %s (%s)" % [tag, v.error]}
	var exe: String = launcher_files()[0]
	var src := Manifest.launcher_source(v.manifest, info.platform(), exe)
	if not src.ok:
		return {"ok": false, "error": src.error}
	# lane ONEZIP-1: a combined package: the one staged file is the package's signed launcher executable
	var files: Dictionary = {exe: src.file} if src.combined else src.asset.files
	if not files.has(exe):
		return {"ok": false, "error": "the signed launcher package %s has no %s" % [tag, exe]}
	var why := Manifest.check_files(dir.path_join("update"), files, ["VERSION.launcher"])
	if why != "":
		return {"ok": false, "error": "the staged launcher %s is not the signed one: %s" % [tag, why]}
	return {"ok": true, "error": "", "new_sha256": files[exe].sha256}


# ---- files -------------------------------------------------------------------------------------------------------------------------------

static func _path_is_link(path: String) -> bool:
	var parent := DirAccess.open(path.get_base_dir())
	return parent != null and parent.is_link(path.get_file())


static func _write(path: String, bytes: PackedByteArray) -> bool:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	var ok := f.store_buffer(bytes)
	f.flush()
	ok = ok and f.get_error() == OK and f.get_length() == bytes.size()
	f.close()
	return ok


## remove a folder and everything below it, never following a link (a link is removed, not entered). MACPLAY-1 (Sol r2): `path` itself
## too: a link given as the folder was opened and its target's contents deleted (an invalid cache/manifests-macos/<tag> linking elsewhere)
static func remove_tree(path: String) -> void:
	if _path_is_link(path):
		DirAccess.remove_absolute(path)
		return
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
