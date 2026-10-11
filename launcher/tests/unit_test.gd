## Lane LAUNCH-1: unit checks of the launcher's rules (versions, the network policy, the manifest schema, archive member names).
##
##   godot --headless --path launcher --script res://tests/unit_test.gd -- --key=<64 hex> --manifest=<file> --sig=<file>
##
## --manifest / --sig / --key: a valid signed manifest written by tools/release/test_launcher.py; this test re-signs nothing, it checks
## that the valid one passes and that each schema break (made here, AFTER the signature check would fail anyway) is named, by calling the
## schema check directly. Prints UNIT lines and "UNIT OK <n> checks" / "UNIT FAIL ..."; exit 0 only when all passed.
extends SceneTree

const Semver := preload("res://scripts/core/semver.gd")
const NetPolicy := preload("res://scripts/net/net_policy.gd")
const Manifest := preload("res://scripts/core/manifest.gd")
const Archive := preload("res://scripts/core/archive.gd")
const GameOptions := preload("res://scripts/core/game_options.gd")
const AioInstall := preload("res://scripts/core/aio_install.gd")
const AioPins := preload("res://scripts/core/aio_pins.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const ProgressMeter := preload("res://scripts/ui/progress_meter.gd")
const SettingsScreen := preload("res://scripts/ui/settings_screen.gd")
const MessagePanel := preload("res://scripts/ui/message_panel.gd")
const ReleaseNotes := preload("res://scripts/core/release_notes.gd")
const InstanceLock := preload("res://scripts/core/instance_lock.gd")
const Updater := preload("res://scripts/core/updater.gd")
const BuildInfo := preload("res://scripts/core/build_info.gd")

var _checks := 0
var _failed := 0


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if not ok:
		_failed += 1
		print("UNIT FAIL ", what)


func _init() -> void:
	# versions (SemVer 2.0.0 section 11 precedence)
	var order := ["v0.1.0-preview.0", "v0.1.0-preview.1", "v0.1.0-preview.2", "v0.1.0-preview.10", "v0.1.0", "v0.1.1-preview.1",
		"v0.1.1", "v0.2.0-preview.9", "v0.2.0", "v0.10.0", "v1.0.0-preview.1", "v1.0.0", "v1.0.10", "v10.0.0"]
	for i in order.size():
		for j in order.size():
			var want := 0 if i == j else (-1 if i < j else 1)
			_check(Semver.compare(order[i], order[j]) == want, "semver %s vs %s" % [order[i], order[j]])
	# lane RELNAME-1: the alpha builds follow the last preview (SemVer: "alpha" < "preview" within one release, so the alpha line is
	# v0.4.0): v0.3.0-preview.4 < v0.4.0-alpha.1 < v0.4.0-alpha.2 < v0.4.0; an alpha below a preview of the same release
	var alpha := ["v0.3.0-preview.3", "v0.3.0-preview.4", "v0.3.0", "v0.4.0-alpha.0", "v0.4.0-alpha.1", "v0.4.0-alpha.2", "v0.4.0-alpha.10",
		"v0.4.0-preview.1", "v0.4.0", "v0.4.1-alpha.1"]
	for i in alpha.size():
		for j in alpha.size():
			var want := 0 if i == j else (-1 if i < j else 1)
			_check(Semver.compare(alpha[i], alpha[j]) == want, "semver %s vs %s" % [alpha[i], alpha[j]])
	_check(Semver.channel_of("v0.4.0-alpha.1") == "preview", "an alpha build is in the pre-release channel (id preview)")
	_check(Semver.channel_name("preview") == "Alpha" and Semver.channel_name("stable") == "Stable", "the pre-release channel is shown as Alpha")
	# a launcher on the pre-release channel at v0.3.0-preview.4 offers v0.4.0-alpha.1 (its release list keeps the alpha tag, the channel
	# filter keeps it, and it sorts first)
	var rels := [{"tag": "v0.3.0-preview.4", "prerelease": true}, {"tag": "v0.4.0-alpha.1", "prerelease": true}, {"tag": "v0.2.0", "prerelease": false}]
	rels.sort_custom(func(a, b): return Semver.compare(a.tag, b.tag) > 0)
	var bi4 = BuildInfo.new()
	bi4.version = "v0.3.0-preview.4"
	var u4 = Updater.new(bi4)
	u4.state = ConfigFile.new()
	var offered: Array = u4.in_channel(rels, u4.channel())
	_check(u4.channel() == "preview" and offered[0].tag == "v0.4.0-alpha.1" and Semver.compare(offered[0].tag, bi4.version) > 0,
		"a preview launcher at v0.3.0-preview.4 offers v0.4.0-alpha.1, not %s" % offered[0].tag)
	var bia = BuildInfo.new()
	bia.version = "v0.4.0-alpha.1"
	var ua = Updater.new(bia)
	ua.state = ConfigFile.new()
	_check(ua.channel() == "preview", "an alpha launcher starts on the pre-release channel")
	# Sol r1: v1.0.0-1 was ordered above v1.0.0--1 (is_valid_int("-1")) and installed automatically; a trailing newline matched '$'
	for bad in ["0.1.0", "v1.0", "v01.0.0", "v1.0.0+build", "v1.0.0-", "v1.0.0-01", "v1.0.0-a..b", "vx.y.z", "v1.0.0 ", "", "v1.0.0-é",
			"v1.0.0-1", "v1.0.0--1", "v1.0.0-+1", "v1.0.0\n", "\nv1.0.0", "v1.0.0-preview", "v1.0.0-preview.01", "v1.0.0-preview.-1",
			"v1.0.0-preview.1.2", "v1.0.0-rc.1", "v1.0.0-alpha", "v1.0.0-PREVIEW.1", "v1.0.0-ALPHA.1", "v1.0.0-alpha.01",
			"v1.0.0-alpha.1.2", "v1.0.0-alpha-1", "v1.0.0-beta.1", "v1.0.0-alphapreview.1", "v1.0.0-alpha.1\n", "V1.0.0", "v1.0.0-preview.1\n", "v1234567890.0.0"]:
		_check(not Semver.is_valid(bad), "semver rejects '%s'" % bad.c_escape())
	_check(Semver.channel_of("v1.0.0") == "stable" and Semver.channel_of("v1.0.0-preview.3") == "preview", "channels")
	_check(Semver.channel_of("v1.0.0-preview.0") == "preview", "preview.0 is a preview")
	# lane RELTEST-1: until the player picks a channel, a launcher follows its own version's channel (a v0.3.0-preview.1 launcher on Stable
	# told a new player "There is no stable release yet." with Play off); a pick is kept; a development run (v0.0.0-dev) stays on stable
	for v in [["v0.3.0-preview.1", "preview"], ["v1.0.0", "stable"], ["v0.0.0-dev", "stable"]]:
		var bi = BuildInfo.new()
		bi.version = v[0]
		var u = Updater.new(bi)
		u.state = ConfigFile.new()  # no remembered pick (the test's user folder may hold one)
		_check(u.channel() == v[1], "a %s launcher starts on %s, not %s" % [v[0], v[1], u.channel()])
		u.state.set_value("launcher", "channel", "stable" if v[1] == "preview" else "preview")
		_check(u.channel() != v[1], "the player's channel pick wins over the %s launcher's default" % v[0])
	# lane RELTEST-1: release notes as the launcher shows them: autorelease.py's Markdown escapes undone, once, nothing else rendered
	for t in [["Movies play\\: the start\\-up movies\\.", "Movies play: the start-up movies."], ["Ctrl\\+number \\(off\\)", "Ctrl+number (off)"],
			["a &lt;b&gt; &amp; c", "a <b> & c"], ["&amp;lt; stays", "&lt; stays"], ["\\\\\\- two", "\\- two"], ["C:\\Games é\\x", "C:\\Games é\\x"],
			["## What's new\n\n- a\tb\u0007", "## What's new\n\n- a\tb"], ["end\\", "end\\"], ["&am", "&am"]]:
		_check(Updater.plain_text(t[0]) == t[1], "notes '%s' shown as '%s', not '%s'" % [t[0].c_escape(), t[1].c_escape(), Updater.plain_text(t[0]).c_escape()])
	# the network policy
	var pol := NetPolicy.new()
	for ok in ["https://api.github.com/repos/a/b/releases", "https://github.com/a/b/releases/download/v1/x.zip",
			"https://objects.githubusercontent.com/x", "https://release-assets.githubusercontent.com/y?z=1"]:
		_check(pol.check(ok) == "", "policy allows %s" % ok)
	for bad in ["http://api.github.com/x", "https://evil.com/x", "https://github.com.evil.com/x", "https://githubusercontent.com/x",
			"https://evil.com/.githubusercontent.com", "https://a.b.githubusercontent.com/x", "https://user@github.com/x",
			"https://github.com:8443/x", "ftp://github.com/x", "https://github.com/x#f", "https://127.0.0.1:8000/x",
			"http://127.0.0.1:8000/x", "https://-x.githubusercontent.com/x", "https://github.com/a b"]:
		_check(pol.check(bad) != "", "policy refuses %s" % bad)
	_check(NetPolicy.check_test_origin("http://127.0.0.1:8123") == "", "test origin loopback")
	for bad in ["http://10.0.0.1:80", "https://127.0.0.1:1", "http://127.0.0.1", "http://evil.com:80", "http://127.0.0.1:80/x"]:
		_check(NetPolicy.check_test_origin(bad) != "", "test origin refuses %s" % bad)
	pol.test_origin = "http://127.0.0.1:8123"
	_check(pol.check("http://127.0.0.1:8123/dl/x") == "", "test origin allowed")
	_check(pol.check("http://localhost:8123/dl/x") != "" and pol.check("http://127.0.0.1:8124/dl/x") != "", "only the one test origin")
	# archive member names
	var top := "openbfme-v1.0.0-linux-x64"
	for ok in [top + "/", top + "/OpenBFME.x86_64", top + "/sub/file.txt"]:
		_check(Archive.check_name(ok, top).ok, "archive name %s" % ok)
	for bad in ["/etc/passwd", top + "/../x", "../" + top + "/x", top + "/a/../../x", top + "/./x", top + "//x", "C:/x", top + "/C:x",
			top + "\\..\\x", "other/x", top + "/x.", top + "/x ", top + "/CON", top + "/nul.txt", top + "/a\nb", top + "/a|b", top + "/a:stream",
			top + "/COM¹", top + "/com².txt", top + "/COM³", top + "/LPT¹", top + "/lpt².log", top + "/LPT³", top + "/COM0", top + "/LPT0"]:
		_check(not Archive.check_name(bad, top).ok, "archive name refuses %s" % bad.c_escape())
	# the manifest: the given valid one passes; schema breaks are named
	var key := ""
	var mpath := ""
	var spath := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--key="):
			key = a.substr(6)
		elif a.begins_with("--manifest="):
			mpath = a.substr(11)
		elif a.begins_with("--sig="):
			spath = a.substr(6)
	if mpath != "":
		var bytes := FileAccess.get_file_as_bytes(mpath)
		var sig := FileAccess.get_file_as_bytes(spath)
		var v := Manifest.verify(bytes, sig, key.hex_decode(), {"repo": "Test/repo"})
		_check(v.ok, "the valid manifest passes (%s)" % v.error)
		_check(not Manifest.verify(bytes, sig, key.hex_decode(), {"repo": "Other/repo"}).ok, "another repository is refused")
		_check(not Manifest.verify(bytes, sig, key.hex_decode(), {"repo": "Test/repo", "tag": "v9.9.9"}).ok, "another tag is refused")
		_check(not Manifest.verify(bytes, sig, PackedByteArray(), {}).ok, "no key, no manifest")
		var m: Dictionary = JSON.parse_string(bytes.get_string_from_utf8())
		var breaks := {
			"format": func(d): d.format = 1,
			"files missing": func(d): d.assets.game["linux-x64"].erase("files"),
			"files empty": func(d): d.assets.game["linux-x64"].files = {},
			"file path ..": func(d): d.assets.game["linux-x64"].files["../x"] = {"size": 1, "sha256": "0".repeat(64)},
			"file path absolute": func(d): d.assets.launcher["linux-x64"].files["/etc/x"] = {"size": 1, "sha256": "0".repeat(64)},
			"file digest": func(d): d.assets.game["linux-x64"].files.values()[0].sha256 = "x",
			"file key": func(d): d.assets.game["linux-x64"].files.values()[0]["extra"] = 1,
			"file case twin": func(d): d.assets.game["linux-x64"].files["openbfme.PCK"] = {"size": 1, "sha256": "0".repeat(64)},
			"extra key": func(d): d["x"] = 1,
			"missing key": func(d): d.erase("date"),
			"channel": func(d): d.channel = "stable" if d.channel == "preview" else "preview",
			"version": func(d): d.version = "1.0",
			"commit": func(d): d.commit = "abc",
			"date": func(d): d.date = "yesterday",
			"repo": func(d): d.repo = "no-slash",
			"asset name": func(d): d.assets.game["linux-x64"].name = "../x.tar.gz",
			"asset size": func(d): d.assets.game["linux-x64"].size = -1,
			"asset fraction": func(d): d.assets.game["linux-x64"].size = 1.5,
			"asset digest": func(d): d.assets.game["linux-x64"].sha256 = "ABC",
			"platform": func(d): d.assets.game.erase("windows-x64"),
			"kind": func(d): d.assets.erase("launcher"),
		}
		for what in breaks:
			var d: Dictionary = m.duplicate(true)
			breaks[what].call(d)
			_check(Manifest._schema(d) != "", "schema refuses: %s" % what)
		_check(Manifest._schema(m.duplicate(true)) == "", "schema accepts the valid manifest")
	_game_options()
	_aio()
	_ui()
	_ui_dialogs()
	if _failed == 0:
		print("UNIT OK %d checks" % _checks)
		quit(0)
	else:
		print("UNIT FAIL %d of %d checks" % [_failed, _checks])
		quit(1)


## lane INPUT-1: the free camera box edits the game's Options.ini and leaves the rest of the file alone
func _game_options() -> void:
	var dir := OS.get_user_data_dir().path_join("game_options_test")
	DirAccess.make_dir_recursive_absolute(dir)
	var p := dir.path_join("Options.ini")
	if FileAccess.file_exists(p):
		DirAccess.remove_absolute(p)
	_check(not GameOptions.free_camera(p), "no Options.ini: the free camera is off")
	_check(GameOptions.set_free_camera(true, p) == "", "the first write creates Options.ini")
	_check(FileAccess.get_file_as_string(p) == "OpenBFMEFreeCamera = yes\n" and GameOptions.free_camera(p), "the key is written as the game reads it")
	var f := FileAccess.open(p, FileAccess.WRITE)
	f.store_string("AlternateMouseSetup = no\nOpenBFMEFreeCamera = yes\nResolution = 1280 720\nOpenBFMEFreeCamera=yes\n")
	f.close()
	_check(GameOptions.set_free_camera(false, p) == "", "the box turned off writes")
	_check(FileAccess.get_file_as_string(p) == "AlternateMouseSetup = no\nOpenBFMEFreeCamera = no\nResolution = 1280 720\n", "the other lines stay, the key's lines become one")
	_check(not GameOptions.free_camera(p) and GameOptions.get_value("Resolution", p) == "1280 720", "read back")
	DirAccess.remove_absolute(p)
	var forced := OS.get_environment("OPENBFME_GAME_USER_DIR")
	_check(GameOptions.game_user_dir().ends_with("app_userdata/OpenBFME") or forced != "", "the game's folder is Godot's app_userdata/OpenBFME")


## lane AIO-1: the All In One BFME Launcher's hosts are allowed only while its opt-in download runs; its file names must be plain
## relative paths; the pins are the 315 + 297 English files of the two packages
func _aio() -> void:
	var pol := NetPolicy.new()
	_check(pol.check("https://bfmeladder.com/api/workshop/download?guid=original-RotWK") != "", "AIO: the service is refused by default")
	pol.extra_hosts = AioInstall.HOSTS.duplicate()
	_check(pol.check("https://bfmeladder.com/api/workshop/download?guid=original-RotWK") == "", "AIO: the API host during a download")
	_check(pol.check("https://workshop-files.bfmeladder.com/x/0123") == "", "AIO: the file host during a download")
	for bad in ["http://bfmeladder.com/api", "https://bfmeladder.com:8443/api", "https://evil.bfmeladder.com/x", "https://bfmeladder.com.evil.net/x",
			"https://user@bfmeladder.com/x"]:
		_check(pol.check(bad) != "", "AIO: refuses %s" % bad)
	_check(pol.check("https://api.github.com/repos/a/b/releases") == "", "AIO: GitHub stays allowed")
	for good in [["lang\\English.big", "lang/English.big"], ["_patch201.big", "_patch201.big"], ["data\\movies\\EALogo.vp6", "data/movies/EALogo.vp6"]]:
		_check(AioInstall.safe_relative(good[0]) == good[1], "AIO: plain path %s" % good[0])
	for bad in ["", "\\x.big", "/etc/passwd", "C:\\x.big", "a\\..\\..\\x", "./x", "a\\\\b", "a. \\b", "a.\\b", "x\tb", "caf\u00e9.big", "a|b", "a*b"]:
		_check(AioInstall.safe_relative(bad) == "", "AIO: refuses the name '%s'" % bad.c_escape())
	_check(AioPins.PACKAGES.size() == 2 and AioPins.PACKAGES[0].guid == "original-RotWK" and AioPins.PACKAGES[1].guid == "original-BFME2", "AIO: two packages")
	_check(AioPins.PACKAGES[0].files.size() == 315 and AioPins.PACKAGES[1].files.size() == 297, "AIO: 315 + 297 pinned files")
	_check(AioInstall._whole(2.0) and not AioInstall._whole(2.5) and not AioInstall._whole("2") and not AioInstall._whole(null), "AIO: whole numbers only")


## lane UI-3: what the window may show of this machine (scripts/ui/paths.gd), where it finds the game folders, the progress numbers
func _ui() -> void:
	var home := "/" + "home/zedtester" if not Paths.is_windows() else "C:\\Users\\zedtester"
	var tilde := "%USERPROFILE%" if Paths.is_windows() else "~"
	var sep := "\\" if Paths.is_windows() else "/"
	_check(Paths.scrub(home + sep + "Games" + sep + "BFME", home, "zedtester") == tilde + sep + "Games" + sep + "BFME", "UI: the home folder becomes " + tilde)
	_check(Paths.scrub("cannot create %s/x (busy)" % home.replace("\\", "/"), home, "zedtester") == "cannot create %s/x (busy)" % tilde, "UI: also with forward slashes")
	_check(Paths.scrub("/run/media/zedtester/SD/RotWK", home, "zedtester") == "/run/media/…/SD/RotWK", "UI: the user name as a path component")
	_check(Paths.scrub("D:\\zedtester\\Games", home, "zedtester") == "D:\\…\\Games", "UI: the user name between backslashes")
	_check(Paths.scrub("zedtester likes /opt/zedtesters/x", home, "zedtester") == "zedtester likes /opt/zedtesters/x", "UI: only whole components")
	_check(Paths.scrub("/srv/a.b/x", "/h", "a.b") == "/srv/…/x" and Paths.scrub("/srv/aXb/x", "/h", "a.b") == "/srv/aXb/x", "UI: the name is matched literally")
	_check(Paths.scrub("/srv/zedtester2/x", "/srv/zedtester", "zedtester") == "/srv/zedtester2/x", "UI: a longer sibling name stays")
	_check(Paths.scrub("in /srv/zedtester.", "/srv/zedtester", "zedtester") == "in " + tilde + ".", "UI: the home folder at the end of a sentence")
	var dir := OS.get_user_data_dir().path_join("ui_paths_test")
	Paths.forget_game_folders(dir)
	DirAccess.remove_absolute(dir.path_join(Paths.MARKER))
	DirAccess.make_dir_recursive_absolute(dir.path_join("R"))
	DirAccess.make_dir_recursive_absolute(dir.path_join("B"))
	var env_set := OS.get_environment("ROTWK_INSTALL") != "" and OS.get_environment("BFME2_INSTALL") != ""
	if not env_set:
		_check(not Paths.game_folders(dir).found and Paths.game_folders(dir).source == "", "UI: no folder files, no games")
		var f := FileAccess.open(dir.path_join(Paths.MARKER), FileAccess.WRITE)
		f.store_string("# x\nSOURCE=test\nROTWK_INSTALL=%s\nBFME2_INSTALL=%s\n" % [dir.path_join("R"), dir.path_join("B")])
		f.close()
		var g := Paths.game_folders(dir)
		_check(g.found and g.source == "downloaded" and g.rotwk == dir.path_join("R"), "UI: the launcher's downloaded folders")
		f = FileAccess.open(dir.path_join(Paths.CONFIG), FileAccess.WRITE)
		f.store_string("# OpenBFME\r\nROTWK_INSTALL=%s\r\nBFME2_INSTALL=%s\r\n" % [dir.path_join("R"), dir.path_join("missing")])
		f.close()
		g = Paths.game_folders(dir)
		_check(not g.found and g.source == "config", "UI: the confirmed folders come first; a missing one is not found")
		_check(Paths.forget_game_folders(dir) == "" and not FileAccess.file_exists(dir.path_join(Paths.CONFIG)), "UI: choose again removes install-paths.cfg")
		_check(Paths.game_folders(dir).source == "downloaded", "UI: then the downloaded folders again")
	var m := ProgressMeter.new()
	_check(m.text(0, 1048576 * 100) == "0.0 MB of 100.0 MB", "UI: no rate before the first sample")
	m.feed(0, 1000)
	m.feed(10 * 1048576, 2000)
	_check(is_equal_approx(m.rate, 10 * 1048576.0), "UI: the rate of the first sample")
	_check(m.text(10 * 1048576, 1048576 * 100) == "10.0 MB of 100.0 MB · 10.0 MB/s · 9 s left", "UI: MB, MB/s and time left (%s)" % m.text(10 * 1048576, 1048576 * 100))
	m.feed(20 * 1048576, 2200)
	_check(is_equal_approx(m.rate, 10 * 1048576.0), "UI: samples under half a second wait")
	m.feed(5, 2600)
	_check(is_equal_approx(m.rate, 10 * 1048576.0) and m._b == 5, "UI: a new file restarts the sample")
	_check(ProgressMeter.time_left(59) == "59 s" and ProgressMeter.time_left(90) == "1 min 30 s" and ProgressMeter.time_left(1200) == "20 min" and ProgressMeter.time_left(3725) == "1 h 2 min", "UI: time left")
	_check(ProgressMeter.mb(3 * 1073741824) == "3.00 GB", "UI: GB above 1 GB")


## lane UI-5: the Settings tabs show one page at a time; the message dialog closed with its X (Close) opens again (lane UI-3 round 2,
## Sol r1: a dialog's X hid its inner layer for good); the release notes as the cards read them; the Update button's toggles
func _ui_dialogs() -> void:
	var st := SettingsScreen.new()
	for key in ["display", "about", "general"]:
		st.show_tab(key)
		var shown := st._pages.keys().filter(func(k): return _shown(st._pages[k], st))
		_check(shown == [key] and st.tab == key, "UI: the Settings tab %s shows alone (%s)" % [key, shown])
	st.free()
	var mp := MessagePanel.new()
	var card: Control = mp.find_children("*", "PanelContainer", true, false)[0]
	for round in 3:
		mp.show_error("t", "x", "about")
		_check(_shown(card, mp), "UI: the message dialog shows (opened %d times)" % (round + 1))
		var close: Button = mp.find_children("*", "Button", true, false).filter(func(b): return b.text == "Close")[0]
		close.pressed.emit()
		_check(not mp.visible, "UI: the message dialog closes")
	mp.free()
	var body := "OpenBFME v0.3.0-preview.2 is a preview build.\nInstall it.\n\n## What's new\n\n- Force-attack on the ground: hold Ctrl.\n" + \
		"- Multiplayer games stay in sync.\n\n## What's fixed\n\n- A fix.\n\n## Known issues\n\n- One.\n- Two.\n\nBuilt from commit a5c0211ba1 (2026-10-10)."
	var parsed := ReleaseNotes.parse(body)
	_check(parsed.intro == "OpenBFME v0.3.0-preview.2 is a preview build. Install it.", "notes: the intro (%s)" % parsed.intro)
	_check(parsed.sections.map(func(x): return x.title) == ["What's new", "What's fixed", "Known issues"], "notes: the sections")
	_check(ReleaseNotes.items(body, "what's new") == ["Force-attack on the ground: hold Ctrl.", "Multiplayer games stay in sync."], "notes: What's new")
	_check(ReleaseNotes.items(body, "Known issues", 1) == ["One."], "notes: at most `limit` items")
	_check(ReleaseNotes.items("no structure at all", "What's new").is_empty() and ReleaseNotes.parse("").sections.is_empty(), "notes: none")
	_check(ReleaseNotes.parse("[b]x[/b]\n## What's new\n- <img src=x> [url]y[/url]").sections[0].items[0] == "<img src=x> [url]y[/url]", "notes: text stays text")
	_check(ReleaseNotes.date_text("2026-10-10T18:03:11Z") == "10 October 2026" and ReleaseNotes.date_text("") == "" and ReleaseNotes.date_text("soon") == "", "notes: dates")
	_check(ReleaseNotes.title_of("v0.3.0-preview.2") == "Preview 2 of 0.3.0" and ReleaseNotes.title_of("v1.0.0") == "Version 1.0.0", "notes: titles")
	var bi = BuildInfo.new()
	var u = Updater.new(bi)
	u.state = ConfigFile.new()
	_check(u.auto_update(), "updates install by themselves by default")
	u.data_dir = OS.get_user_data_dir().path_join("unit-auto")
	DirAccess.make_dir_recursive_absolute(u.data_dir)
	u.set_auto_update(false)
	_check(not u.auto_update(), "updating by itself can be turned off")
	_check(u.cached_releases().is_empty(), "no cached release list: no notes")
	_check(u.running().is_empty(), "nothing runs")
	u.set_state("launcher", "running", {"v0.1.0": OS.get_process_id(), "v0.0.9": 2147483000})
	_check(u.running().is_empty(), "a process that is not that version's game does not count (%s)" % [u.running()])
	# Sol r3: the one-launcher lock is atomic, held by one owner, and released only by it
	var la := InstanceLock.new(u.data_dir.path_join("lock-test"), "v9.9.9")
	var lb := InstanceLock.new(u.data_dir.path_join("lock-test"))
	_check(la.port == lb.port and la.port >= InstanceLock.FIRST_PORT and la.port < InstanceLock.FIRST_PORT + InstanceLock.PORTS, "lock: one port per data folder")
	_check(la.acquire() and la.held(), "lock: the first launcher takes it")
	_check(not lb.acquire() and lb.other_launcher == "v9.9.9" and lb.error.contains(str(la.port)), "lock: a second one is refused and hears the holder's hello (%s)" % lb.error)
	lb.release()
	_check(la.held() and not InstanceLock.new(u.data_dir.path_join("lock-test")).acquire(), "lock: a refused launcher's release frees nothing")
	la.release()
	_check(lb.acquire(), "lock: free again once its holder releases it")
	lb.release()
	# Sol r2: a stale copy of state.cfg never writes back over newer values (the file is read again under the lock before a write)
	var u2 = Updater.new(bi)
	u2.data_dir = u.data_dir
	u2.state = ConfigFile.new()
	u.set_state("launcher", "running", {"v0.1.0": [111, 222]})
	u2.set_state("launcher", "selected", "v0.1.0")
	var back := ConfigFile.new()
	back.load(u.data_dir.path_join("state.cfg"))
	_check(back.get_value("launcher", "running", {}) == {"v0.1.0": [111, 222]} and back.get_value("launcher", "selected", "") == "v0.1.0",
		"a second writer keeps the first one's running games")
	u.set_state("launcher", "selected", "")
	# Sol r1: the Windows check, simulated (a running image cannot be opened for writing: here a read-only file plays the lock)
	u.test_windows_lock = true
	var gdir: String = u.data_dir.path_join("versions/v0.1.0/game")
	DirAccess.make_dir_recursive_absolute(gdir)
	var exe: String = gdir.path_join(Updater.GAME_EXE[bi.platform()])
	_check(not u._game_runs(4242, "v0.1.0"), "Windows check: no executable, not running")
	var fx := FileAccess.open(exe, FileAccess.WRITE)
	fx.store_string("game")
	fx.close()
	_check(not u._game_runs(4242, "v0.1.0"), "Windows check: an executable that can be opened for writing is not running")
	FileAccess.set_unix_permissions(exe, FileAccess.UNIX_READ_OWNER)
	_check(u._game_runs(4242, "v0.1.0"), "Windows check: a locked executable is running")
	u.set_state("launcher", "running", {"v0.1.0": [4242, 4343]})
	_check(u.running() == ["v0.1.0"], "Windows check: running() follows the lock")
	FileAccess.set_unix_permissions(exe, FileAccess.UNIX_READ_OWNER | FileAccess.UNIX_WRITE_OWNER)
	_check(u.running().is_empty(), "Windows check: unlocked, not running")
	u.set_state("launcher", "running", {"v0.1.0": 4242})
	FileAccess.set_unix_permissions(exe, FileAccess.UNIX_READ_OWNER)
	_check(u.running() == ["v0.1.0"], "an older state.cfg's single pid still counts")
	FileAccess.set_unix_permissions(exe, FileAccess.UNIX_READ_OWNER | FileAccess.UNIX_WRITE_OWNER)
	u.test_windows_lock = false
	DirAccess.remove_absolute(exe)
	for d in ["versions/v0.1.0/game", "versions/v0.1.0", "versions"]:
		DirAccess.remove_absolute(u.data_dir.path_join(d))
	# the versions lock: held, refused to a second holder while fresh, taken over when its owner is gone
	_check(u.lock_versions(100), "versions lock: taken")
	var owner: String = u.data_dir.path_join(Updater.LOCK).path_join("owner")
	var fo := FileAccess.open(owner, FileAccess.WRITE)
	fo.store_string("%d %d\n" % [1, int(Time.get_unix_time_from_system())])  # pid 1 (init) lives: another launcher holds it
	fo.close()
	_check(not u.lock_versions(200), "versions lock: a live holder's fresh lock is waited for")
	fo = FileAccess.open(owner, FileAccess.WRITE)
	fo.store_string("%d %d\n" % [1, int(Time.get_unix_time_from_system()) - 600])
	fo.close()
	_check(u.lock_versions(200), "versions lock: a lock older than a minute is taken over")
	u.unlock_versions()
	if DirAccess.dir_exists_absolute("/proc"):
		_check(u.lock_versions(100), "versions lock: taken again")
		fo = FileAccess.open(owner, FileAccess.WRITE)
		fo.store_string("%d %d\n" % [2147483000, int(Time.get_unix_time_from_system())])
		fo.close()
		_check(u.lock_versions(200), "versions lock: a lock whose launcher is gone is taken over")
		u.unlock_versions()
	_check(not DirAccess.dir_exists_absolute(u.data_dir.path_join(Updater.LOCK)), "versions lock: released")
	DirAccess.remove_absolute(u.data_dir.path_join("state.cfg"))
	DirAccess.remove_absolute(u.data_dir)


## `node` and every parent up to `top` are visible (what is_visible_in_tree means, without a running tree)
static func _shown(node: Control, top: Control) -> bool:
	var n: Node = node
	while n != null:
		if n is CanvasItem and not n.visible:
			return false
		if n == top:
			return true
		n = n.get_parent()
	return false
