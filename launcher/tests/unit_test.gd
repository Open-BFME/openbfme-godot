## Lane LAUNCH-1: unit checks of the launcher's rules (versions, the network policy, the manifest schema, archive member names).
##
##   godot --headless --path launcher --script res://tests/unit_test.gd -- --key=<64 hex> --manifest=<file> --sig=<file> [--signkms=<file>]
##
## --manifest / --sig / --key: a valid signed manifest written by tools/release/test_launcher.py; this test re-signs nothing, it checks
## that the valid one passes and that each schema break (made here, AFTER the signature check would fail anyway) is named, by calling the
## schema check directly. Prints UNIT lines and "UNIT OK <n> checks" / "UNIT FAIL ..."; exit 0 only when all passed.
## --signkms: lane SIGNKMS-1's fixtures (JSON, hex; written by tools/release/test_launcher.py with tools/release/ed25519.py and
## release_keys.py, throwaway keys): signatures under an "old" (Deck, raw) and a "new" (KMS, envelope) key, for the key-list checks.
extends SceneTree

const Semver := preload("res://scripts/core/semver.gd")
const NetPolicy := preload("res://scripts/net/net_policy.gd")
const Manifest := preload("res://scripts/core/manifest.gd")
const MacOSUpdate := preload("res://scripts/core/macos_update.gd")
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
const UrlHandler := preload("res://scripts/core/url_handler.gd")
const OnlineCount := preload("res://scripts/core/online_count.gd")
const DesktopEntry := preload("res://scripts/core/desktop_entry.gd")
const AppIcon := preload("res://scripts/ui/app_icon.gd")
const ReleaseSignature := preload("res://scripts/crypto/release_signature.gd")
const NetPolicy2 := preload("res://scripts/net/net_policy.gd")


## lane SIGNKMS-1: the release download stand-in for the signature slots: url -> bytes, anything else 404; get_small's limit applies
class FakeHttp extends "res://scripts/net/http_fetch.gd":
	var files := {}
	var asked := []

	func _init() -> void:
		super(NetPolicy2.new(), "v0.0.0-test")

	func get_small(url: String, cap: int, accept := "", etag := "", extra := PackedStringArray()) -> Dictionary:
		asked.append(url)
		if not files.has(url):
			return {"ok": true, "error": "", "code": 404, "headers": {}, "body": PackedByteArray()}
		var body: PackedByteArray = files[url]
		if body.size() > cap:
			return {"ok": false, "error": "more than %d bytes" % cap, "code": 200, "headers": {}, "body": PackedByteArray()}
		return {"ok": true, "error": "", "code": 200, "headers": {}, "body": body}

var _checks := 0
var _failed := 0


## lane ONLINE-2: openbfme:// links join a game and nothing else; the registration entries
func _url_handler() -> void:
	var p := UrlHandler.parse("openbfme://join/0123456789abcdef")
	_check(p.ok and p.action == "join" and p.game == "0123456789abcdef", "a join link parses")
	_check(UrlHandler.parse("openbfme://join/0123456789abcdef/").ok, "a trailing slash is allowed")
	for bad in ["openbfme://join/0123456789ABCDEF", "openbfme://join/0123", "openbfme://join/0123456789abcdeg", "openbfme://run/calc.exe",
			"openbfme://join/0123456789abcdef/extra", "https://example.org/j/0123456789abcdef", "openbfme://join/../../x", " openbfme://join/0123456789abcdef",
			"openbfme://join/0123456789abcdef\" --update \"", "OPENBFME://join/0123456789abcdef", "openbfme://join/0123456789abcdef\n"]:
		_check(not UrlHandler.parse(bad).ok, "a link is refused: " + bad.c_escape())
	var args := UrlHandler.game_args(p, "https://lobby.example.org")
	_check(args == PackedStringArray(["--", "--online-join=0123456789abcdef", "--online-lobby=https://lobby.example.org"]), "the game's arguments of a link")
	# ONLINE-2 r2 (Sol r1): the isolated link mode; Windows splices %1 into the command line, so a link with quotes adds arguments of its own
	var tests := ["--api-base=", "--trust-key="]
	var ok := UrlHandler.isolated_url(PackedStringArray(["--open-url", "openbfme://join/0123456789abcdef"]), false, tests)
	_check(ok.mode and ok.ok and ok.game == "0123456789abcdef", "--open-url <link> alone is taken")
	_check(not UrlHandler.isolated_url(PackedStringArray(["--update", "--list"]), false, tests).mode, "no link: not the link mode")
	for injected in [["--open-url", "openbfme://join/0123456789abcdef", "--update"],                 # Sol's injection: "...\" --update \""
			["--open-url", "openbfme://join/0123456789abcdef", "--install=v0.1.0", ""],
			["--update", "--open-url", "openbfme://join/0123456789abcdef"],
			["--open-url"], ["--open-url", "--update"], ["openbfme://join/0123456789abcdef"],      # round 1's bare link
			["--lobby=https://evil.example", "--open-url", "openbfme://join/0123456789abcdef"],
			["--open-url", "openbfme://join/0123456789abcdef", "--open-url", "openbfme://join/0123456789abcdef"],
			["--api-base=http://127.0.0.1:1", "--open-url", "openbfme://join/0123456789abcdef"]]:  # test options only in a test build
		var r := UrlHandler.isolated_url(PackedStringArray(injected), false, tests)
		_check(r.mode and not r.ok, "refused: " + str(injected))
	_check(UrlHandler.isolated_url(PackedStringArray(["--api-base=http://127.0.0.1:1", "--open-url", "openbfme://join/0123456789abcdef"]), true, tests).ok,
		"a test build: its test options before --open-url")
	_check(not UrlHandler.isolated_url(PackedStringArray(["--open-url", "openbfme://join/0123456789abcdef", "--api-base=http://127.0.0.1:1"]), true, tests).ok,
		"a test build: nothing after the link")
	var entry := UrlHandler.linux_desktop_entry("/opt/OpenBFME Launcher/OpenBFMELauncher.x86_64")
	_check(entry.contains("MimeType=x-scheme-handler/openbfme;") and entry.contains("Exec=\"/opt/OpenBFME Launcher/OpenBFMELauncher.x86_64\" -- --open-url %u"), "the .desktop entry")
	var reg: Array = UrlHandler.windows_reg_commands("C:\\Games\\OpenBFMELauncher.exe")
	_check(reg.size() == 3 and reg[0][1] == "HKCU\\Software\\Classes\\openbfme" and reg[2][4] == "\"C:\\Games\\OpenBFMELauncher.exe\" -- --open-url \"%1\"", "the registry entries (current user only)")


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
	# lane NIGHTLY-1 (owner, 2026-10-21): the channels are shown as "Stable" (id stable) and "Nightly" (id preview)
	_check(Semver.channel_name("stable") == "Stable" and Semver.channel_name("preview") == "Nightly", "stable is shown as Stable, preview as Nightly")
	# the nightly word: v<x>.<y>.<z>-nightly.<YYYYMMDD>, between alpha and preview of one release, below the release
	var nightly := ["v0.4.12", "v0.4.13-alpha.1", "v0.4.13-nightly.1", "v0.4.13-nightly.20261021", "v0.4.13-nightly.20261022",
		"v0.4.13-nightly.999999999", "v0.4.13-preview.1", "v0.4.13", "v0.4.14-nightly.20261023"]
	for i in nightly.size():
		for j in nightly.size():
			var want := 0 if i == j else (-1 if i < j else 1)
			_check(Semver.compare(nightly[i], nightly[j]) == want, "semver %s vs %s" % [nightly[i], nightly[j]])
	_check(Semver.is_valid("v0.4.13-nightly.20261022") and not Semver.is_valid("v0.4.13-nightly.") and not Semver.is_valid("v0.4.13-nightly.020261022")
		and not Semver.is_valid("v0.4.13-nightly.1234567890") and not Semver.is_valid("v0.4.13-nightly") and not Semver.is_valid("v0.4.13-nightly.1\n"),
		"nightly grammar")
	var pn := Semver.parse("v0.4.13-nightly.20261022")
	_check(pn.ok and pn.kind == "nightly" and pn.preview == 20261022 and pn.core == [0, 4, 13], "nightly parse")
	_check(Semver.channel_of("v0.4.13-nightly.20261022") == "preview" and Semver.channel_of("v0.4.13") == "stable", "a nightly is in the pre-release channel (id preview)")
	for d in [["v0.4.13-nightly.20261022", "Nightly 0.4.13 (2026-10-22)"], ["v0.4.13-nightly.20260101", "Nightly 0.4.13 (2026-01-01)"],
			["v0.4.13-nightly.20261322", "Nightly 0.4.13 build 20261322"], ["v0.4.13-nightly.20261032", "Nightly 0.4.13 build 20261032"],
			["v0.4.13-nightly.20261000", "Nightly 0.4.13 build 20261000"], ["v0.4.13-nightly.7", "Nightly 0.4.13 build 7"]]:
		_check(Semver.display(d[0]) == d[1], "display %s: %s, not %s" % [d[0], d[1], Semver.display(d[0])])
	_check(ReleaseNotes.title_of("v0.4.13-nightly.20261022") == "Nightly 0.4.13 (2026-10-22)", "notes: nightly title")
	var plain := ["v0.4.0-alpha.1", "v0.4.0-alpha.4", "v0.4.0-alpha.10", "v0.4.0", "v0.4.5", "v0.4.6", "v0.4.10", "v0.5.0", "v1.0.0"]
	for i in plain.size():
		for j in plain.size():
			var want := 0 if i == j else (-1 if i < j else 1)
			_check(Semver.compare(plain[i], plain[j]) == want, "semver %s vs %s" % [plain[i], plain[j]])
	_check(Semver.compare("v0.4.5", "v0.3.0-preview.6") > 0 and Semver.channel_of("v0.4.5") == "stable", "v0.4.5 is a stable release above every preview")
	for d in [["v0.4.5", "Alpha 0.4.5"], ["v0.5.0", "Alpha 0.5.0"], ["v0.4.0-alpha.3", "v0.4.0 Alpha 3"], ["v0.3.0-preview.6", "v0.3.0-preview.6"],
			["v0.0.0-dev", "v0.0.0-dev"], ["0af8b6b84c-dirty", "0af8b6b84c-dirty"], ["", ""]]:
		_check(Semver.display(d[0]) == d[1], "display %s: %s, not %s" % [d[0], d[1], Semver.display(d[0])])
	# a launcher installed from v0.4.5 starts on stable and offers every later plain release, a full release (not a pre-release)
	var rels45 := [{"tag": "v0.4.5", "prerelease": false}, {"tag": "v0.4.6", "prerelease": false}, {"tag": "v0.5.0", "prerelease": false},
		{"tag": "v0.4.0-alpha.4", "prerelease": true}, {"tag": "v0.3.0-preview.6", "prerelease": true}]
	rels45.sort_custom(func(a, b): return Semver.compare(a.tag, b.tag) > 0)
	var bi45 = BuildInfo.new()
	bi45.version = "v0.4.5"
	var u45 = Updater.new(bi45)
	u45.state = ConfigFile.new()
	var offered45: Array = u45.in_channel(rels45, u45.channel())
	_check(u45.channel() == "stable" and offered45.map(func(r): return r.tag) == ["v0.5.0", "v0.4.6", "v0.4.5"],
		"a v0.4.5 launcher on its default channel offers the plain releases: %s" % [offered45.map(func(r): return r.tag)])
	var bia4 = BuildInfo.new()
	bia4.version = "v0.4.0-alpha.4"
	var ua4 = Updater.new(bia4)
	ua4.state = ConfigFile.new()
	_check(ua4.channel() == "preview" and ua4.in_channel(rels45, ua4.channel())[0].tag == "v0.5.0", "an alpha.4 launcher offers the plain releases")
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
	# lane ONLINECOUNT-1: the lobby's host, for its stats path alone
	var lp := NetPolicy.new()
	_check(lp.check("https://play.openbfme.com/v1/stats") != "", "the lobby is refused before one is set")
	_check(lp.set_lobby("https://play.openbfme.com") == "" and lp.set_lobby("https://play.openbfme.com/") == "", "the lobby is a plain https host")
	_check(lp.check("https://play.openbfme.com/v1/stats") == "", "the lobby's stats path is allowed")
	for bad in ["https://play.openbfme.com/v1/games", "https://play.openbfme.com/v1/stats?x=1", "https://play.openbfme.com/v1/stats/", "https://play.openbfme.com/",
			"https://play.openbfme.com/v1/auth/guest", "https://play.openbfme.com:8443/v1/stats", "http://play.openbfme.com/v1/stats", "https://evil.com/v1/stats",
			"https://play.openbfme.com.evil.com/v1/stats", "https://u@play.openbfme.com/v1/stats"]:
		_check(lp.check(bad) != "", "policy refuses %s for the lobby" % bad)
	for bad in ["http://lobby.example.org", "https://lobby.example.org:8443", "https://lobby.example.org/x", "https://u@lobby.example.org", "ftp://x", "", "https://"]:
		_check(lp.set_lobby(bad) != "" and lp.lobby_host == "", "a lobby of %s is refused and leaves none set" % bad)
	_check(NetPolicy.new().check("https://api.github.com/repos/a/b/releases") == "", "GitHub stays allowed beside a lobby")
	# the player count's answer: exactly version 1 with whole counts
	for t in [[200, '{"v":1,"players":1,"games_open":0,"games_running":0}', "1 player online"], [200, '{"v":1,"players":0,"games_open":3,"games_running":2,"as_of":5}', "0 players online"],
			[200, '{"v":1,"players":1234,"games_open":0,"games_running":0}', "1234 players online"]]:
		var good := OnlineCount.parse(t[0], t[1].to_utf8_buffer())
		_check(good.ok and good.text == t[2], "the count of %s is '%s', not '%s'" % [t[1], t[2], good.text])
	for t in [[404, '{"v":1,"players":1,"games_open":0,"games_running":0}'], [200, ''], [200, 'null'], [200, '[]'], [200, '{"v":2,"players":1,"games_open":0,"games_running":0}'],
			[200, '{"players":1,"games_open":0,"games_running":0}'], [200, '{"v":1,"players":-1,"games_open":0,"games_running":0}'],
			[200, '{"v":1,"players":1.5,"games_open":0,"games_running":0}'], [200, '{"v":1,"players":"1","games_open":0,"games_running":0}'],
			[200, '{"v":1,"players":1e9,"games_open":0,"games_running":0}'], [200, '{"v":1,"players":1,"games_open":0}'], [200, '{"v":1,"players":null,"games_open":0,"games_running":0}']]:
		var r := OnlineCount.parse(t[0], t[1].to_utf8_buffer())
		_check(not r.ok and r.text == "", "the answer %d %s shows nothing" % [t[0], t[1].left(40)])
	# r2 (Sol): the count never outlives its renewal
	_check(OnlineCount.chip_text("3 players online", 0.0) == "3 players online" and OnlineCount.chip_text("3 players online", OnlineCount.EXPIRY_SECONDS - 1.0) == "3 players online",
		"a count is shown until it expires")
	_check(OnlineCount.chip_text("3 players online", OnlineCount.EXPIRY_SECONDS) == "" and OnlineCount.chip_text("3 players online", 1e9) == "" and OnlineCount.chip_text("x", -1.0) == "",
		"an expired (or future-dated) count is gone")
	_check(OnlineCount.stale("", 0.0) and OnlineCount.stale("1 player online", OnlineCount.REFRESH_SECONDS) and not OnlineCount.stale("1 player online", 10.0),
		"the Play screen asks again for no number or an old one only")
	_check(OnlineCount.in_view(false, false, true) and OnlineCount.in_view(true, true, false), "a focused window, or a headless run, is in view")
	_check(not OnlineCount.in_view(false, true, true) and not OnlineCount.in_view(false, false, false) and not OnlineCount.in_view(false, true, false),
		"a minimized or unfocused window asks nothing")
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
	var kmspath := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--signkms="):
			kmspath = a.substr(10)
		elif a.begins_with("--key="):
			key = a.substr(6)
		elif a.begins_with("--manifest="):
			mpath = a.substr(11)
		elif a.begins_with("--sig="):
			spath = a.substr(6)
	if mpath != "":
		var bytes := FileAccess.get_file_as_bytes(mpath)
		var sig := FileAccess.get_file_as_bytes(spath)
		var v := Manifest.verify(bytes, sig, [key.hex_decode()], {"repo": "Test/repo"})
		_check(v.ok, "the valid manifest passes (%s)" % v.error)
		_check(not Manifest.verify(bytes, sig, [key.hex_decode()], {"repo": "Other/repo"}).ok, "another repository is refused")
		_check(not Manifest.verify(bytes, sig, [key.hex_decode()], {"repo": "Test/repo", "tag": "v9.9.9"}).ok, "another tag is refused")
		_check(not Manifest.verify(bytes, sig, [], {}).ok, "no key, no manifest")
		_check(not Manifest.verify(bytes, sig, [PackedByteArray()], {}).ok, "an empty key, no manifest")
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
	if kmspath != "":
		_signkms(kmspath)
	_build_info_keys()
	_macos_manifest_schema()
	_macos_update_tree()
	_macos_manifest_only_on_macos()
	_game_options()
	_aio()
	_ui()
	_ui_dialogs()
	_url_handler()
	_app_icon()
	if _failed == 0:
		print("UNIT OK %d checks" % _checks)
		quit(0)
	else:
		print("UNIT FAIL %d of %d checks" % [_failed, _checks])
		quit(1)


## lane SIGNKMS-1: the key list. A signature is valid under ANY trusted key, raw (the old Deck key's scheme) or over the envelope of its
## kind (the KMS key's); an unknown key, changed bytes, another kind or an empty list are refused; the release's X.sig / X.sig2 slots
func _signkms(path: String) -> void:
	var fx: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	var h := func(k: String) -> PackedByteArray: return String(fx[k]).hex_decode()
	var deck: PackedByteArray = h.call("deck")
	var kms: PackedByteArray = h.call("kms")
	var both := [deck, kms]
	var expect := {"repo": "Test/repo"}
	print("UNIT signkms: the key-list checks run")
	# the envelope is release_keys.py's byte for byte
	_check(ReleaseSignature.envelope("manifest.json", "abc".to_ascii_buffer()) == h.call("envelope_abc"), "the envelope matches release_keys.py's")
	var m: PackedByteArray = h.call("manifest")
	var raw: PackedByteArray = h.call("manifest_raw")
	var env: PackedByteArray = h.call("manifest_env")
	var t0 := Time.get_ticks_msec()
	_check(Manifest.verify(m, raw, [deck], expect).ok, "old key only: its raw signature passes")
	_check(Manifest.verify(m, env, [kms], expect).ok, "new key only: its envelope signature passes")
	_check(Manifest.verify(m, raw, both, expect).ok and Manifest.verify(m, env, both, expect).ok, "the list of both: either signature passes")
	_check(Manifest.verify(m, env, [kms, deck], expect).ok, "the list's order does not matter")
	_check(ReleaseSignature.signer(both, "manifest.json", m, raw) == 0 and ReleaseSignature.signer(both, "manifest.json", m, env) == 1,
		"signer names the key that signed")
	_check(Manifest.verify(m, h.call("manifest_env_deck"), [deck], expect).ok, "the old key's envelope signature passes too (the rule is per key, not per scheme)")
	print("UNIT signkms: 7 manifest verifications in %d ms" % (Time.get_ticks_msec() - t0))
	var r := Manifest.verify(m, env, [deck], expect)
	_check(not r.ok and String(r.error).contains("signature is not valid"), "the new key's signature under the old key alone is refused (%s)" % r.error)
	_check(not Manifest.verify(m, raw, [kms], expect).ok, "the old key's signature under the new key alone is refused")
	_check(not Manifest.verify(m, h.call("manifest_env_other"), both, expect).ok, "an unknown key's envelope signature is refused")
	_check(not Manifest.verify(m, h.call("manifest_raw_other"), both, expect).ok, "an unknown key's raw signature is refused")
	var tampered := m.duplicate()
	tampered[tampered.size() / 2] ^= 1
	_check(not Manifest.verify(tampered, raw, both, expect).ok, "a tampered manifest is refused (raw)")
	_check(not Manifest.verify(tampered, env, both, expect).ok, "a tampered manifest is refused (envelope)")
	var longer := m.duplicate()
	longer.append(0x20)
	_check(not Manifest.verify(longer, env, both, expect).ok, "a manifest with a byte more is refused (envelope)")
	_check(not Manifest.verify(m, h.call("manifest_env_payload_kind"), both, expect).ok, "an envelope of kind payload.json is not a manifest signature")
	_check(not Manifest.verify(m, h.call("manifest_env_macos_kind"), both, expect).ok, "an envelope of kind manifest-macos.json is not a manifest.json signature")
	_check(not Manifest.verify(h.call("envelope_of_manifest"), env, both, expect).ok, "the envelope itself is not a manifest (its signature never passes as raw)")
	var bad := env.duplicate()
	bad[40] ^= 1
	_check(not Manifest.verify(m, bad, both, expect).ok, "a changed signature is refused")
	_check(not Manifest.verify(m, env.slice(0, 63), both, expect).ok, "a 63-byte signature is refused")
	r = Manifest.verify(m, env, [], expect)
	_check(not r.ok and String(r.error).contains("no release key"), "an empty list is refused: " + String(r.error))
	_check(not Manifest.verify(m, env, [kms.slice(0, 16)], expect).ok, "a list with a 16-byte key is refused")
	# the macOS sidecar: kind manifest-macos.json
	var mac: PackedByteArray = h.call("macos")
	_check(Manifest.verify(mac, h.call("macos_raw"), both, expect, "macos").ok, "macOS manifest: the old key's raw signature passes")
	_check(Manifest.verify(mac, h.call("macos_env"), both, expect, "macos").ok, "macOS manifest: the new key's envelope signature passes")
	_check(not Manifest.verify(mac, h.call("macos_env_wrong_kind"), both, expect, "macos").ok, "macOS manifest: an envelope of kind manifest.json is refused")
	var mac_t := mac.duplicate()
	mac_t[mac_t.size() / 2] ^= 1
	_check(not Manifest.verify(mac_t, h.call("macos_raw"), both, expect, "macos").ok and not Manifest.verify(mac_t, h.call("macos_env"), both, expect, "macos").ok,
		"macOS manifest: tampered, refused under both schemes")
	_check(not Manifest.verify(mac, h.call("macos_env"), [deck], expect, "macos").ok, "macOS manifest: the new key's signature under the old key alone is refused")
	# the payload inventory: kind payload.json
	var pay: PackedByteArray = h.call("payload")
	var pexpect := {"repo": "Test/repo", "platform": "linux-x64"}
	var pv := Manifest.verify_payload(pay, h.call("payload_raw"), both, pexpect)
	_check(pv.ok, "payload: the old key's raw signature passes (%s)" % pv.error)
	_check(Manifest.verify_payload(pay, h.call("payload_env"), both, pexpect).ok, "payload: the new key's envelope signature passes")
	_check(Manifest.verify_payload(pay, h.call("payload_env"), [kms], pexpect).ok, "payload: new key only")
	_check(not Manifest.verify_payload(pay, h.call("payload_env"), [deck], pexpect).ok, "payload: the new key's signature under the old key alone is refused")
	_check(not Manifest.verify_payload(pay, h.call("payload_env_wrong_kind"), both, pexpect).ok, "payload: an envelope of kind manifest.json is refused")
	var pay_t := pay.duplicate()
	pay_t[pay_t.size() / 2] ^= 1
	_check(not Manifest.verify_payload(pay_t, h.call("payload_raw"), both, pexpect).ok and not Manifest.verify_payload(pay_t, h.call("payload_env"), both, pexpect).ok,
		"payload: tampered, refused under both schemes")
	r = Manifest.verify_payload(pay, h.call("payload_env"), [], pexpect)
	_check(not r.ok and String(r.error).contains("no release key"), "payload: an empty list is refused")
	# parse_hex_list (--trust-key, release_trust.gd RELEASE_KEYS)
	var dh := deck.hex_encode()
	var kh := kms.hex_encode()
	_check(ReleaseSignature.parse_hex_list(dh).keys == [deck], "one key parses")
	_check(ReleaseSignature.parse_hex_list(dh + "," + kh).keys == [deck, kms], "two keys parse, in order")
	var nine := PackedStringArray()
	for i in 9:
		nine.append("%064x" % (i + 1))
	for broken in ["", dh + ",", dh + "," + dh, dh.to_upper(), dh.left(63), dh + "00", dh + ",," + kh, ",".join(nine), dh + ";" + kh]:
		_check(not ReleaseSignature.parse_hex_list(broken).ok, "a malformed key list is refused: '%s'" % broken.left(40))
	_slots(fx, deck, kms)


## lane SIGNKMS-1: a release's two signature slots (Updater.release_signature): X.sig, else X.sig2; whichever verified is cached as X.sig
func _slots(fx: Dictionary, deck: PackedByteArray, kms: PackedByteArray) -> void:
	var m := String(fx.manifest).hex_decode()
	var raw := String(fx.manifest_raw).hex_decode()
	var env := String(fx.manifest_env).hex_decode()
	var zero := PackedByteArray()
	zero.resize(64)
	var big := env.duplicate()
	big.append(0)
	var tag := "v1.2.3-preview.4"
	var cases := [
		# [name, X.sig (null: not in the release), X.sig2, keys, want ok, the cached / error text, sig2 must not be asked]
		["X.sig valid (old key), no X.sig2", raw, null, [deck, kms], true, raw, false],
		["X.sig valid, X.sig2 broken: X.sig2 is not even asked", raw, zero, [deck, kms], true, raw, true],
		["X.sig missing, X.sig2 valid (new key)", null, env, [deck, kms], true, env, false],
		["X.sig invalid, X.sig2 valid", zero, env, [deck, kms], true, env, false],
		["X.sig of 65 bytes, X.sig2 valid", big, env, [deck, kms], true, env, false],
		["X.sig unknown key, X.sig2 valid", String(fx.manifest_env_other).hex_decode(), env, [deck, kms], true, env, false],
		["both invalid", zero, zero, [deck, kms], false, "manifest.json.sig: not valid; manifest.json.sig2: not valid", false],
		["X.sig missing, X.sig2 invalid", null, zero, [deck, kms], false, "manifest.json.sig: not in the release; manifest.json.sig2: not valid", false],
		["neither slot", null, null, [deck, kms], false, "has no manifest.json.sig or manifest.json.sig2", false],
		["an old launcher's list: X.sig missing, X.sig2 by the new key", null, env, [deck], false, "signature is not valid", false],
	]
	var n := 0
	for c in cases:
		n += 1
		var info := BuildInfo.new()
		info.repo = "Test/repo"
		info.release_keys = c[3]
		var up := Updater.new(info)
		up.data_dir = OS.get_user_data_dir().path_join("signkms-slots-%d" % n)
		Updater.remove_tree(up.data_dir)
		var fake := FakeHttp.new()
		fake.files["http://t/manifest.json"] = m
		var assets := {"manifest.json": {"url": "http://t/manifest.json"}}
		for slot in [["manifest.json.sig", c[1]], ["manifest.json.sig2", c[2]]]:
			if slot[1] != null:
				assets[slot[0]] = {"url": "http://t/" + slot[0]}
				fake.files["http://t/" + slot[0]] = slot[1]
		up.http = fake
		var got: Dictionary = up.release_manifest({"tag": tag, "prerelease": true, "assets": assets})
		if c[4]:
			var cached := FileAccess.get_file_as_bytes(up.data_dir.path_join("cache/manifests").path_join(tag).path_join("manifest.json.sig"))
			_check(got.ok and got.sig == c[5] and cached == c[5], "slots, %s: accepted, the verified one kept as manifest.json.sig (%s)" % [c[0], got.get("error", "")])
		else:
			_check(not got.ok and String(got.error).contains(c[5]), "slots, %s: refused by name (%s)" % [c[0], got.get("error", "")])
		if c[6]:
			_check(not "http://t/manifest.json.sig2" in fake.asked, "slots, %s: X.sig2 not fetched" % c[0])
	# the macOS sidecar's slots, the same rule (kind manifest-macos.json)
	var mac := String(fx.macos).hex_decode()
	for c in [["mac: X.sig missing, X.sig2 valid", null, String(fx.macos_env).hex_decode(), true],
			["mac: X.sig invalid, X.sig2 valid", zero, String(fx.macos_env).hex_decode(), true],
			["mac: X.sig valid (old key)", String(fx.macos_raw).hex_decode(), null, true],
			["mac: X.sig2 of the wrong kind", null, String(fx.macos_env_wrong_kind).hex_decode(), false],
			["mac: both invalid", zero, zero, false]]:
		n += 1
		var info := BuildInfo.new()
		info.repo = "Test/repo"
		info.release_keys = [deck, kms]
		var up := Updater.new(info)
		up.data_dir = OS.get_user_data_dir().path_join("signkms-slots-%d" % n)
		Updater.remove_tree(up.data_dir)
		var fake := FakeHttp.new()
		fake.files["http://t/manifest-macos.json"] = mac
		var assets := {"manifest-macos.json": {"url": "http://t/manifest-macos.json"}}
		for slot in [["manifest-macos.json.sig", c[1]], ["manifest-macos.json.sig2", c[2]]]:
			if slot[1] != null:
				assets[slot[0]] = {"url": "http://t/" + slot[0]}
				fake.files["http://t/" + slot[0]] = slot[1]
		up.http = fake
		var got: Dictionary = up.macos_release_manifest({"tag": tag, "prerelease": true, "assets": assets})
		if c[3]:
			var want: PackedByteArray = c[2] if c[1] == null or c[1] == zero else c[1]
			var cached := FileAccess.get_file_as_bytes(up.data_dir.path_join("cache/manifests-macos").path_join(tag).path_join("manifest-macos.json.sig"))
			_check(got.ok and got.sig == want and cached == want, "slots, %s: accepted and cached as manifest-macos.json.sig (%s)" % [c[0], got.get("error", "")])
		else:
			_check(not got.ok and String(got.error).contains("manifest-macos.json.sig2"), "slots, %s: refused, both slots named (%s)" % [c[0], got.get("error", "")])


## lane SIGNKMS-1: build_info.json's release_keys: absent -> [release_key]; present -> the list, beginning with release_key; malformed -> error
func _build_info_keys() -> void:
	var deck := "1a".repeat(32)
	var kms := "2b".repeat(32)
	var base := {"version": "v1.0.0", "repo": "Test/repo", "release_key": deck, "test_build": false}
	var parse := func(extra: Dictionary) -> RefCounted:
		var d := base.duplicate()
		d.merge(extra, true)
		return BuildInfo.from_text(BuildInfo.new(), JSON.stringify(d))
	var old: RefCounted = parse.call({})
	_check(old.error == "" and old.release_keys == [deck.hex_decode()], "build_info without release_keys trusts [release_key] (%s)" % old.error)
	var two: RefCounted = parse.call({"release_keys": [deck, kms]})
	_check(two.error == "" and two.release_keys == [deck.hex_decode(), kms.hex_decode()], "build_info with release_keys trusts the list (%s)" % two.error)
	var nine := []
	for i in 9:
		nine.append(deck if i == 0 else "%064x" % i)
	for bad in [[kms, deck], [], "x", deck, [deck, deck], [deck, kms.to_upper()], [deck, 5], [deck, kms + "," + kms], [deck, kms.left(62)], nine, null, {}]:
		var b: RefCounted = parse.call({"release_keys": bad})
		_check(b.error != "" and b.release_keys.is_empty(), "build_info refuses release_keys %s (%s)" % [str(bad).left(50), b.error])
	var badkey: RefCounted = parse.call({"release_key": deck.to_upper()})
	_check(badkey.error != "" and badkey.release_keys.is_empty(), "build_info refuses an upper-case release_key")


## lane OWNERFIX-3: the launcher's icon decodes to the 256 x 256 ring, and the Linux desktop entry names the launcher and the icon it writes
func _app_icon() -> void:
	var img: Image = AppIcon.image()
	_check(img != null and img.get_width() == 256 and img.get_height() == 256, "the launcher's icon decodes to 256 x 256")
	var home := OS.get_user_data_dir().path_join("desktop_entry_test")
	var r := DesktopEntry.install("/opt/Open BFME/OpenBFMELauncher.x86_64", home)
	_check(r.ok, "the desktop entry is written: " + str(r.get("error", "")))
	var entry := FileAccess.get_file_as_string(home.path_join("applications/openbfme.desktop"))
	_check(entry.contains("\nName=OpenBFME\n") and entry.contains("\nIcon=openbfme\n") and entry.contains("\nExec=\"/opt/Open BFME/OpenBFMELauncher.x86_64\"\n"),
		"the entry names OpenBFME, the icon and the quoted launcher")
	var png := FileAccess.get_file_as_bytes(home.path_join("icons/hicolor/256x256/apps/openbfme.png"))
	_check(png == Marshalls.base64_to_raw(AppIcon.PNG_BASE64) and png.size() > 1000, "the icon file is the window icon's PNG")
	_check(DesktopEntry.text("/a/\"b\"$c").contains("Exec=\"/a/\\\\\"b\\\\\"\\\\$c\""), "quotes and $ in the path are escaped")
	_check(not DesktopEntry.install("", home).ok, "no launcher path: refused")


func _macos_manifest_schema() -> void:
	var version := "v1.2.3-preview.4"
	var manifest := {
		"format": 3, "product": "OpenBFME", "repo": "Test/repo", "version": version, "channel": "preview",
		"date": "2026-10-16", "commit": "0123456789abcdef0123456789abcdef01234567",
		"assets": {"game": {"macos-arm64": {
			"name": Manifest.macos_asset_name(version), "size": 1, "sha256": "0".repeat(64),
			"files": {"OpenBFME.app/Contents/MacOS/OpenBFME": {"size": 1, "sha256": "0".repeat(64)}},
		}}},
	}
	_check(Manifest._schema(manifest.duplicate(true), "macos") == "", "the signed Mac schema accepts one arm64 game package")
	_check(Manifest._schema({"format": 3, "product": "OpenBFME", "repo": "Test/repo", "version": version, "channel": "preview",
		"date": "2026-10-16", "commit": "0123456789abcdef0123456789abcdef01234567", "assets": {"game": {}}}, "macos") == "",
		"the signed Mac schema accepts an authenticated no-entry manifest")
	_check(Manifest._schema(manifest.duplicate(true)) != "", "the default schema does not accept a Mac platform")
	for invalid in [
		func(d): d.format = 2,
		func(d): d.assets.game["linux-x64"] = {},
		func(d): d.assets["launcher"] = {},
		func(d): d.assets.game["macos-arm64"].name = "openbfme-v1.2.3-macos-arm64.zip",
		func(d): d.assets.game["macos-arm64"].files["../outside"] = {"size": 1, "sha256": "0".repeat(64)},
	]:
		var changed: Dictionary = manifest.duplicate(true)
		invalid.call(changed)
		_check(Manifest._schema(changed, "macos") != "", "the Mac schema rejects malformed or cross-platform data")


func _macos_update_tree() -> void:
	var root := OS.get_user_data_dir().path_join("macos_update_tree_test")
	MacOSUpdate._remove_tree(root)
	_check(MacOSUpdate.receipt_path(root, "v1.2.3") != MacOSUpdate.receipt_path(root, "v1.2.4"),
		"Mac receipts are isolated by package root and version")
	_check(MacOSUpdate.required_state_path(root, "v1.2.3") != MacOSUpdate.required_state_path(root, "v1.2.4"),
		"signed-required state is isolated by package root and version")
	var info := BuildInfo.new()
	info.version = "v1.2.3"
	info.macos_updates = true
	var updater := Updater.new(info)
	updater.launcher_dir = root
	var receipt_path := MacOSUpdate.receipt_path(root, "v1.2.3")
	var required_path := MacOSUpdate.required_state_path(root, "v1.2.3")
	DirAccess.remove_absolute(receipt_path)
	DirAccess.remove_absolute(required_path)
	_check(not MacOSUpdate.signed_required(updater, root, "v1.2.3"),
		"an initial generic package may have no signed receipt or required state")
	var state := FileAccess.open(required_path, FileAccess.WRITE)
	state.store_string("corrupt")
	state.close()
	_check(MacOSUpdate.signed_required(updater, root, "v1.2.3"),
		"a corrupt signed-required state fails closed")
	DirAccess.remove_absolute(required_path)
	var receipt := FileAccess.open(receipt_path, FileAccess.WRITE)
	receipt.store_string("corrupt")
	receipt.close()
	_check(MacOSUpdate.signed_required(updater, root, "v1.2.3"),
		"a missing state does not downgrade a present signed receipt")
	DirAccess.remove_absolute(receipt_path)
	var names := [
		"LICENSE", "NOTICE", "README-MAC.txt", "VERSION",
		"OpenBFME.app/Contents/MacOS/OpenBFME",
		"OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher",
		"OpenBFME Launcher.app/Contents/MacOS/openbfme-bundle-swap",
	]
	var files := {}
	for name in names:
		var path := root.path_join(name)
		DirAccess.make_dir_recursive_absolute(path.get_base_dir())
		var bytes: PackedByteArray = ("fixture:" + name).to_utf8_buffer()
		var file := FileAccess.open(path, FileAccess.WRITE)
		file.store_buffer(bytes)
		file.close()
		var hash := HashingContext.new()
		hash.start(HashingContext.HASH_SHA256)
		hash.update(bytes)
		files[name] = {"size": bytes.size(), "sha256": hash.finish().hex_encode()}
	_check(MacOSUpdate.validate_tree(root, files) == "", "a complete exact Mac package matches its signed inventory")
	FileAccess.open(root.path_join("unexpected.txt"), FileAccess.WRITE).close()
	_check(MacOSUpdate.validate_tree(root, files) != "", "a staged Mac package rejects an unexpected file")
	MacOSUpdate._remove_tree(root)


## lane MACREL-1: manifest-macos.json is read on macOS only. A Windows / Linux launcher, even one whose build_info.json carries the Mac
## flag, takes the release's manifest.json (the separately signed Mac sidecar never reaches it); on macOS the Mac flag selects the sidecar
func _macos_manifest_only_on_macos() -> void:
	var info := BuildInfo.new()
	info.version = "v1.2.3"
	info.macos_updates = true
	var updater := Updater.new(info)
	var release := {"tag": "v1.2.4", "prerelease": false, "assets": {
		"manifest-macos.json": {"url": "http://127.0.0.1:9/manifest-macos.json", "size": 1},
		"manifest-macos.json.sig": {"url": "http://127.0.0.1:9/manifest-macos.json.sig", "size": 64}}}
	if OS.get_name() == "macOS":
		_check(updater.macos_updates_enabled() and info.platform() == "macos-arm64", "macOS: a Mac-updates launcher reads manifest-macos.json")
		return
	_check(not updater.macos_updates_enabled(), "not macOS: the Mac flag never enables the Mac sidecar")
	_check(info.platform() in ["linux-x64", "windows-x64"], "not macOS: the platform is the Windows / Linux one")
	var v: Dictionary = updater.release_manifest(release)
	_check(not v.ok and String(v.error).contains("has no manifest.json"),
		"not macOS: a release with only manifest-macos.json is not a release for this launcher (%s)" % v.get("error", ""))


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
	# community PR #36: the optional BFME1 package, never in the base download
	_check(AioPins.BFME1_PACKAGES.size() == 1 and AioPins.BFME1_PACKAGES[0].guid == "original-BFME1" and AioPins.BFME1_PACKAGES[0].files.size() == 346, "AIO: the optional BFME1 package, 346 pinned files")
	_check(AioPins.PACKAGES.all(func(p): return p.key != "bfme1"), "AIO: the base download has no BFME1")
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
	_check(ReleaseNotes.title_of("v0.3.0-preview.2") == "Preview 2 of 0.3.0" and ReleaseNotes.title_of("v1.0.0") == "Alpha 1.0.0" and ReleaseNotes.title_of("v0.4.5") == "Alpha 0.4.5" and ReleaseNotes.title_of("v0.4.0-alpha.2") == "v0.4.0 Alpha 2", "notes: titles")
	# NOTES-1: the studio-style patch notes (bold area lines, a title heading, a table, both generations of section titles)
	var studio := "# OpenBFME v0.4.0-alpha.5 patch notes\n\nOctober 13, 2026 · alpha build for testers\n\nInstall it.\n\n## Which file to download\n\n| File | What |\n|---|---|\n" + \
		"| **[a.zip](https://x.example/a.zip)** | **Launcher.** |\n\n## Added\n\n**Online**\n- Server browser.\n- Rejoin.\n\n**Saves**\n- Games can be saved.\n\n" + \
		"**General**\n- No area.\n\n## Changed\n\n**Performance**\n- Faster paths.\n\n## Bug fixes\n\n**Interface**\n- A click deselects.\n\n" + \
		"## Known issues\n\n**Audio**\n- Some sounds are missing.\n  and a wrapped line.\n\nBuilt from commit a5c0211ba1 (2026-10-13)."
	var sp := ReleaseNotes.parse(studio)
	_check(sp.sections.map(func(x): return x.title) == ["Added", "Changed", "Bug fixes", "Known issues"], "notes: studio sections (the title and the table have no items)")
	_check(sp.sections[0].items == ["Server browser.", "Rejoin.", "Games can be saved.", "No area."], "notes: studio items stay flat for the Play card")
	_check(sp.sections[0].groups.map(func(g): return g.area) == ["Online", "Saves", "General"] and sp.sections[0].groups[0].items.size() == 2, "notes: studio area groups")
	_check(sp.sections[3].groups[0].items == ["Some sounds are missing. and a wrapped line."], "notes: a wrapped item stays in its group")
	_check(ReleaseNotes.play_items(studio, 2) == ["Server browser.", "Rejoin."], "notes: the Play card reads Added")
	_check(ReleaseNotes.play_items("## Bug fixes\n\n**UI**\n- Only a fix.\n") == ["Only a fix."] and ReleaseNotes.play_items("## Changed\n- c\n") == ["c"], "notes: the Play card falls back to fixes, then changes")
	_check(ReleaseNotes.play_items(body) == ["Force-attack on the ground: hold Ctrl.", "Multiplayer games stay in sync."] and ReleaseNotes.parse(body).sections[0].groups[0].area == "", "notes: the older titles still read")
	var bridge := studio.replace("## Added", "## What's new").replace("## Bug fixes", "## What's fixed")
	_check(ReleaseNotes.play_items(bridge) == sp.sections[0].items, "notes: the bridge titles give the same Play card")
	_check(ReleaseNotes.parse("## Added\n**not an area | a table**\n**a** and **b**\n- x").sections[0].groups[0].area == "", "notes: only a whole-line bold text is an area")
	# NOTES-1 r2: the News screen's own labels for a page as the generator escapes it (Updater.plain_text undoes the escapes first)
	var esc := Updater.plain_text("## Added\n\n**Create\\-a\\-Hero**\n- A hero\\.\n\n**Online**\n- A browser\\.\n\n## Known issues\n\n**Audio**\n- A sound\\.\n")
	var news = load("res://scripts/ui/news_screen.gd").new()
	root.add_child(news)
	news.set_releases([{"tag": "v0.4.0-alpha.5", "notes": esc, "published": "2026-10-13T10:00:00Z"}])
	var shown: Array = news.find_children("*", "Label", true, false).map(func(l): return l.text)
	_check(shown.filter(func(t): return t in ["Added", "Create-a-Hero", "Online", "Known issues", "Audio"]) == ["Added", "Create-a-Hero", "Online", "Known issues", "Audio"],
		"News: section titles and area labels as shown (%s)" % str(shown))
	_check(shown.has("A hero.") and not shown.any(func(t): return String(t).contains("\\") or String(t).contains("**")), "News: no escapes or bold marks on screen")
	news.free()
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
	# Windows locks a running image; on Linux a read-only file simulates that lock.
	u.test_windows_lock = true
	var gdir: String = u.data_dir.path_join("versions/v0.1.0/game")
	DirAccess.make_dir_recursive_absolute(gdir)
	var exe: String = gdir.path_join(Updater.GAME_EXE[bi.platform()])
	DirAccess.make_dir_recursive_absolute(exe.get_base_dir())
	_check(not u._game_runs(4242, "v0.1.0"), "Windows check: no executable, not running")
	var fx := FileAccess.open(exe, FileAccess.WRITE)
	fx.store_string("game")
	fx.close()
	_check(not u._game_runs(4242, "v0.1.0"), "Windows check: an executable that can be opened for writing is not running")
	_set_game_lock(exe, true)
	_check(u._game_runs(4242, "v0.1.0"), "Windows check: a locked executable is running")
	u.set_state("launcher", "running", {"v0.1.0": [4242, 4343]})
	_check(u.running() == ["v0.1.0"], "Windows check: running() follows the lock")
	_set_game_lock(exe, false)
	_check(u.running().is_empty(), "Windows check: unlocked, not running")
	u.set_state("launcher", "running", {"v0.1.0": 4242})
	_set_game_lock(exe, true)
	_check(u.running() == ["v0.1.0"], "an older state.cfg's single pid still counts")
	_set_game_lock(exe, false)
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


var _locked_game_pid := -1


func _set_game_lock(exe: String, locked: bool) -> void:
	if OS.get_name() != "Windows":
		FileAccess.set_unix_permissions(exe, FileAccess.UNIX_READ_OWNER if locked else FileAccess.UNIX_READ_OWNER | FileAccess.UNIX_WRITE_OWNER)
		return
	if locked:
		var ping := OS.get_environment("SystemRoot").path_join("System32/PING.EXE")
		_check(DirAccess.copy_absolute(ping, exe) == OK, "Windows lock fixture: copy PING.EXE")
		_locked_game_pid = OS.create_process(exe, ["-n", "60", "127.0.0.1"])
		_check(_locked_game_pid > 0, "Windows lock fixture: start the executable")
	else:
		if _locked_game_pid > 0:
			_check(OS.kill(_locked_game_pid) == OK, "Windows lock fixture: stop the executable")
			var unlocked := false
			for i in range(1000):
				if not OS.is_process_running(_locked_game_pid):
					var writable := FileAccess.open(exe, FileAccess.READ_WRITE)
					if writable != null:
						writable.close()
						unlocked = true
						break
				OS.delay_msec(10)
			_check(not OS.is_process_running(_locked_game_pid), "Windows lock fixture: executable stopped")
			_check(unlocked, "Windows lock fixture: executable unlocked")
		_locked_game_pid = -1


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
