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
	# Sol r1: v1.0.0-1 was ordered above v1.0.0--1 (is_valid_int("-1")) and installed automatically; a trailing newline matched '$'
	for bad in ["0.1.0", "v1.0", "v01.0.0", "v1.0.0+build", "v1.0.0-", "v1.0.0-01", "v1.0.0-a..b", "vx.y.z", "v1.0.0 ", "", "v1.0.0-é",
			"v1.0.0-1", "v1.0.0--1", "v1.0.0-+1", "v1.0.0\n", "\nv1.0.0", "v1.0.0-preview", "v1.0.0-preview.01", "v1.0.0-preview.-1",
			"v1.0.0-preview.1.2", "v1.0.0-rc.1", "v1.0.0-alpha", "v1.0.0-PREVIEW.1", "V1.0.0", "v1.0.0-preview.1\n", "v1234567890.0.0"]:
		_check(not Semver.is_valid(bad), "semver rejects '%s'" % bad.c_escape())
	_check(Semver.channel_of("v1.0.0") == "stable" and Semver.channel_of("v1.0.0-preview.3") == "preview", "channels")
	_check(Semver.channel_of("v1.0.0-preview.0") == "preview", "preview.0 is a preview")
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
	if _failed == 0:
		print("UNIT OK %d checks" % _checks)
		quit(0)
	else:
		print("UNIT FAIL %d of %d checks" % [_failed, _checks])
		quit(1)
