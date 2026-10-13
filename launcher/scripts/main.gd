## Lane LAUNCH-1: the OpenBFME Launcher's window and command line.
##
## The window (lane UI-5's "Parchment & Steel", see the window section below): a left rail with Play, News, Multiplayer, Mods, Game files
## and Settings; the Play screen with the update state, the channel and version, the games and What's New; a bottom bar with the status
## or the one progress bar (MB, MB/s, time left, Pause / Cancel), Check for updates and PLAY. At start (and every hour while it stays
## open) it checks for updates and, with "Update automatically" on (the default), installs a newer game build of the channel in the
## background (verified, see scripts/core/updater.gd) and stages a newer launcher; Play keeps starting the installed version meanwhile
## and the new one from the next Play on. Offline or on an API error it says why the update was skipped and Play starts the newest
## verified installed version. The window shows no home folder or user name (scripts/ui/paths.gd).
##
## Command line (after `--`; with any of these the launcher runs without a window and exits, for tests and scripted use):
##   --update                 check and install the channel's newest version when it is newer than every installed one
##   --install=<version>      install that release (the user's pick: an older version is allowed)
##   --select=<version>       play that installed version from now on (--select= clears the pick)
##   --self-update            stage a newer launcher (replaced on the next start)
##   --channel=stable|preview remember the channel (--channel=alpha is the same as preview, the channel's shown name)
##   --list                   print the installed versions
##   --play                   start the version Play starts
##   --version                print the launcher's version
##   --open-url openbfme://join/<id>   (lane ONLINE-2; what the operating system runs once the handler is registered) start the version Play starts in
##                            that online game: --online-join=<id> --online-lobby=<the lobby>; needs no launcher lock (a link works while the window is
##                            open). It must come alone (r2, Sol r1): any other argument, or a link anywhere else, refuses the start and nothing runs
##   --lobby=<url>            remember the online lobby the links use (until one is set, a link says so and starts nothing)
##   --register-url-handler   register this launcher for openbfme:// links (scripts/core/url_handler.gd: the current user's registry on Windows,
##                            ~/.local/share/applications + xdg-mime on Linux)
##   --auto-update=on|off     (lane UI-5) the setting "Update automatically in the background" (on by default)
##   --free-camera=on|off     (lane INPUT-1) the game's free camera (Options.ini OpenBFMEFreeCamera, see scripts/core/game_options.gd)
##   --aio=on|off             (lane AIO-1, on by default: AioInstall.DEFAULT_ENABLED) the setting "Offer to download the games"
##                            (Settings; the All In One BFME Launcher's service)
##   --aio-install=<folder> --aio-consent   with that setting on: download RotWK 2.01 and BFME2 1.06 from the All In One BFME Launcher's
##                            service into <folder>/RotWK and <folder>/BFME2 (scripts/core/aio_install.gd, docs/AIO.md); --aio-consent is
##                            the player's agreement to the text it logs (in the window: the Download press)
## --screenshot=FILE (with the window): save the window as a PNG once the start-up check has finished, then quit (the layout check).
## --ui-dump (with --screenshot): print every text of every screen, hidden ones included, as "LAUNCHER ui text:" lines, unscrubbed (the
##   test that no screen shows the home folder or the user name); --screenshot-size=WxH lays the window out at that size and saves
##   exactly that (the screenshots at 1280x720 and 1280x800 on any display).
## Test options (honoured only by a development run or a test package, BuildInfo.tests_allowed):
##   --api-base=http://127.0.0.1:<port>   the API origin (a local test server); --trust-key=<64 hex>   the release key of a development run;
##   --repo=<owner>/<name>   the repository; --aio-base=http://127.0.0.1:<port>   the AIO service's stand-in (both of its hosts);
##   --aio-pins=<file>   made-up pinned tables (JSON) for that stand-in; --aio-stop-after=<folder>/<file>:<pause|cancel>   a stop right
##   after that file was kept (the pause / cancel paths); --ui-demo=<screen>   made-up state for the screenshots of each screen (no
##   network; tools/release/launcher_screens.py; the screens are listed at _show_demo); --ui-stop=<pause|cancel|play>@<text>   press
##   Pause / Cancel / Play once while a download whose progress names <text> runs, --ui-then=resume|cancel   then press Resume / Cancel
##   once (with --screenshot)
## Exit status: 0 done; 1 an install / play / select failed (or the AIO download was refused: setting off, no consent, a bad folder); 2
## updates skipped (offline, API error; an AIO download that broke or was stopped: it resumes next time); 3 an update was rejected (signature, size,
## digest, archive or rollback check; the AIO service's data failed a check). Every step prints a LAUNCHER line (also in user://launcher.log).
extends Control

const BuildInfo := preload("res://scripts/core/build_info.gd")
const Updater := preload("res://scripts/core/updater.gd")
const SelfUpdate := preload("res://scripts/core/self_update.gd")
const Semver := preload("res://scripts/core/semver.gd")
const Log := preload("res://scripts/core/launcher_log.gd")
const GlibcCheck := preload("res://scripts/core/glibc_check.gd")
const GameOptions := preload("res://scripts/core/game_options.gd")
const AioInstall := preload("res://scripts/core/aio_install.gd")
const ReleaseNotes := preload("res://scripts/core/release_notes.gd")
const Style := preload("res://scripts/ui/style.gd")
const W := preload("res://scripts/ui/widgets.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const ProgressMeter := preload("res://scripts/ui/progress_meter.gd")
const Textures := preload("res://scripts/ui/textures.gd")
const GoldButton := preload("res://scripts/ui/gold_button.gd")
const PlayScreen := preload("res://scripts/ui/play_screen.gd")
const NewsScreen := preload("res://scripts/ui/news_screen.gd")
const MultiplayerScreen := preload("res://scripts/ui/multiplayer_screen.gd")
const ModsScreen := preload("res://scripts/ui/mods_screen.gd")
const GamesScreen := preload("res://scripts/ui/games_screen.gd")
const SettingsScreen := preload("res://scripts/ui/settings_screen.gd")
const MessagePanel := preload("res://scripts/ui/message_panel.gd")
const InstanceLock := preload("res://scripts/core/instance_lock.gd")

const CLI_ACTIONS := ["--update", "--install=", "--select=", "--self-update", "--channel=", "--list", "--play", "--version", "--free-camera=",
	"--aio=", "--aio-install=", "--auto-update=", "--open-url", "--lobby=", "--register-url-handler"]
const UrlHandler := preload("res://scripts/core/url_handler.gd")
# the test options (honoured only by a development run or a test package); ONLINE-2 r2: also the only arguments allowed before --open-url there
const TEST_OPTIONS := ["--api-base=", "--trust-key=", "--repo=", "--test-kill-at=", "--aio-base=", "--aio-pins=", "--aio-stop-after=", "--ui-demo=",
	"--ui-stop=", "--ui-then="]
const RECHECK_SECONDS := 3600.0  # lane UI-5: while the launcher stays open, it looks for updates again every hour

var info: RefCounted
var updater: Updater
var _instance: InstanceLock  # lane UI-5 (Sol r2): one launcher at a time
var aio: AioInstall
var _thread: Thread
var _releases: Array = []
var _older: Array = []        # the channel's release tags, newest first (Settings: install another one)
var _latest: Dictionary = {}  # the newer release the Update button installs ({} when up to date)
var _games: Dictionary = {}   # Paths.game_folders()
var _meter := ProgressMeter.new()
var _job_kind := ""           # the running job: "check", "install" or "aio"
var _job_target := ""         # the version the running install job replaces or adds ("" when not known yet)
var _stop := ""               # "pause" / "cancel" asked of the running game download
var _paused := ""             # "game" / "launcher" / "aio": a download stopped by Pause (Resume continues it)
var _paused_release: Dictionary = {}
var _paused_pick := false
var _job_assets: Array = []   # the release assets the running game / launcher job downloads (its downloads/<name>.part files)
var _paused_assets: Array = []
var _ui_stop := ""            # tests: --ui-stop=<pause|cancel|play>@<text>, pressed once when a download whose progress names <text> runs
var _ui_then := ""            # tests: --ui-then=resume|cancel, pressed once after that stop
var _aio_target := ""         # the folder of the AIO download that runs or is paused
var _demo := ""
var _problem := ""            # why updates are off ("" when they work)
var _pv := ""                 # the version Play starts (refreshed with the installed versions)
var _update_state := "idle"
var _update_tag := ""
var _launcher_only := false   # the update offered is only the launcher's (the game is up to date)
var _quit_when_done := false  # Play was pressed while an update downloaded: close once it is done (unless the window came back)

var _screen := "play"
var _nav := {}
var _screens := {}
var _play_screen: PlayScreen
var _news: NewsScreen
var _mods: ModsScreen
var _games_screen: GamesScreen
var _settings: SettingsScreen
var _banner: PanelContainer
var _warning: Label
var _status_head: Label
var _status_tail: Label
var _status: Label
var _progress: VBoxContainer
var _bar: ProgressBar
var _bar_title: Label
var _bar_label: Label
var _update: Button
var _check: Button
var _pause: Button
var _cancel: Button
var _play: Button
var _message: MessagePanel


func _ready() -> void:
	Log.open()
	info = BuildInfo.load_info()
	updater = Updater.new(info)
	aio = AioInstall.new(updater)
	var args := OS.get_cmdline_user_args()
	# lane ONLINE-2 r2 (Sol r1): an openbfme:// link is taken only in the isolated mode (UrlHandler.isolated_url: the user arguments are exactly
	# --open-url <link>), checked before anything else reads the arguments; anything else around a link (Windows splices the link into the command
	# line: a link with quotes adds arguments) refuses the whole start, so no CLI action of an injected argument ever runs
	var url_mode := UrlHandler.isolated_url(args, info.tests_allowed(), TEST_OPTIONS)
	if url_mode.mode and not url_mode.ok:
		Log.line("LAUNCHER link refused, nothing done: " + url_mode.error)
		get_tree().quit(1)
		return
	for a in OS.get_cmdline_args():
		if a.to_lower().contains(UrlHandler.SCHEME + ":"):
			Log.line("LAUNCHER link refused, nothing done: a link outside the user arguments (start the launcher with -- --open-url <link>)")
			get_tree().quit(1)
			return
	Log.line("LAUNCHER OpenBFME Launcher %s (%s, %s) updates from %s" % [info.version, info.commit.left(10) if info.commit != "" else "development run", info.platform(), info.repo])
	var problem := ""
	if info.error != "":
		problem = info.error
		Log.line("LAUNCHER " + info.error)
	for a in args:
		if TEST_OPTIONS.any(func(p): return a.begins_with(p)):
			if not info.tests_allowed():
				Log.line("LAUNCHER ignored %s: test options are off in a release build" % a.get_slice("=", 0))
				continue
			if a.begins_with("--api-base="):
				var why := updater.set_test_api_base(a.substr(11))
				if why != "":
					Log.line("LAUNCHER " + why)
					get_tree().quit(1)
					return
				Log.line("LAUNCHER test API origin %s" % a.substr(11))
			elif a.begins_with("--repo="):
				info.repo = a.substr(7)
			elif a.begins_with("--test-kill-at="):
				Updater.kill_at = a.substr(15)
			elif a.begins_with("--aio-stop-after="):
				aio.test_stop_after = a.substr(17)
			elif a.begins_with("--ui-demo="):
				_demo = a.substr(10)
			elif a.begins_with("--ui-stop="):
				_ui_stop = a.substr(10)
			elif a.begins_with("--ui-then="):
				_ui_then = a.substr(10)
			elif a.begins_with("--aio-base=") or a.begins_with("--aio-pins="):
				var why := aio.set_test_origin(a.substr(11)) if a.begins_with("--aio-base=") else aio.set_test_pins(a.substr(11))
				if why != "":
					Log.line("LAUNCHER " + why)
					get_tree().quit(1)
					return
				Log.line("LAUNCHER test AIO %s %s" % ["origin" if a.begins_with("--aio-base=") else "pins", a.substr(11)])
			elif info.release_key.is_empty():
				info.release_key = a.substr(12).hex_decode()
	if info.release_key.size() != 32 and problem == "":
		problem = "this launcher has no release key: it cannot check updates"
	updater.recover()
	var start := SelfUpdate.on_start(updater, args)
	if start.exit:
		get_tree().quit(0)
		return
	if start.has("swap"):
		var state := "waiting"
		while state == "waiting":
			await get_tree().create_timer(0.1).timeout
			state = SelfUpdate.poll(start.swap)
		if state == "confirmed":
			Log.line("LAUNCHER the new launcher %s confirmed its start; the update helper exits" % start.swap.tag)
			get_tree().quit(0)
			return
		SelfUpdate.roll_back(updater, start.swap, state.trim_prefix("failed: "))
		get_tree().quit(0)
		return
	var cli := false
	for a in args:
		for c in CLI_ACTIONS:
			if a == c or (c.ends_with("=") and a.begins_with(c)):
				cli = true
	# one launcher at a time (after the self-update swap above: its helper never holds the lock); --version alone needs none
	# lane ONLINE-2: an openbfme:// link starts the game without the lock too (it installs nothing; the window may be open)
	if not url_mode.mode and not (cli and Array(args).all(func(x): return x == "--version" or not CLI_ACTIONS.any(func(c): return x == c or (c.ends_with("=") and x.begins_with(c))))):
		_instance = InstanceLock.new(OS.get_user_data_dir(), info.version)
		if not _instance.acquire():
			# Sol r4: "already open" only when the port's holder answered as a launcher; another program on the port is said as such
			var why := ("Another OpenBFME Launcher (%s) is already open. Use that one, or close it and start this one again." % _instance.other_launcher) \
				if _instance.other_launcher != "" else ("The OpenBFME Launcher cannot start: %s. The launcher uses that port only to make sure one " +
				"launcher runs at a time; close the other program, or start the launcher again later.") % _instance.error
			Log.line("LAUNCHER " + why)
			if not cli and DisplayServer.get_name() != "headless":
				OS.alert(why, "OpenBFME Launcher")
			_instance = null
			get_tree().quit(1)
			return
	if cli:
		get_tree().quit(_run_cli(args, problem))
		return
	_problem = problem
	_build_ui()
	if start.message != "":
		_set_status(start.message)
	_refresh_installed()
	var unseen := str(updater.get_state("launcher", "unseen_error", ""))
	if unseen != "" and _demo == "":
		Log.line("LAUNCHER showing an update failure from while the game ran: " + unseen)
		_message.show_error("An update failed while you played", unseen, _about())
	if _demo != "":
		_show_demo(_demo)
	elif problem != "":
		_set_status("Updates are off: " + problem)
		_set_update_state("failed")
	elif not "--offline" in args:
		_start_job(_job_check.bind(updater.auto_update()), "check")
	for a in args:
		if a.begins_with("--screenshot="):
			_screenshot(a.substr(13))


func _screenshot(path: String) -> void:
	# --screenshot-size=WxH: lay the window out at exactly that size (letterboxed: KEEP aspect), then crop the letterbox away, so a fixed
	# display (the Deck's gamescope is always 1280x800) still gives 1280x720 shots
	var want := Vector2i.ZERO
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--screenshot-size="):
			var wh := a.substr(18).split("x")
			want = Vector2i(int(wh[0]), int(wh[1]))
			get_window().content_scale_size = want
			get_window().content_scale_aspect = Window.CONTENT_SCALE_ASPECT_KEEP
	await _settle()
	if _ui_then != "":
		Log.line("LAUNCHER ui paused: " + _paused)
		Log.line("LAUNCHER ui test: %s pressed" % _ui_then)
		_on_pause() if _ui_then == "resume" else _on_cancel()
		await _settle()
	# what the window shows, for the tests (headless has no image to save)
	Log.line("LAUNCHER ui paused: " + _paused)
	Log.line("LAUNCHER ui status: " + _status.text)
	Log.line("LAUNCHER ui warning: " + (_warning.text if _banner.visible else ""))
	Log.line("LAUNCHER ui games card: " + ("shown" if _play_screen.games_card.visible else "hidden"))
	Log.line("LAUNCHER ui dialog: " + (_message.shown_text() if _message.visible else ""))
	Log.line("LAUNCHER ui update button: " + (_update.text if _update.visible else ""))
	Log.line("LAUNCHER ui screen: %s%s" % [_screen, " (%s)" % _settings.tab if _screen == "settings" else ""])
	if "--ui-dump" in OS.get_cmdline_user_args():
		for t in _texts(self):
			print("LAUNCHER ui text: " + t)  # not Log.line: the test checks what the window shows, before any scrubbing
	var img := get_viewport().get_texture().get_image() if DisplayServer.get_name() != "headless" else null
	if img != null:
		if want != Vector2i.ZERO:
			var win := Vector2(img.get_size())
			var k := minf(win.x / want.x, win.y / want.y)
			var got := Vector2i(Vector2(want) * k)
			img = img.get_region(Rect2i((img.get_size() - got) / 2, got))
			if got != want:
				img.resize(want.x, want.y, Image.INTERPOLATE_LANCZOS)
		img.save_png(path)
		Log.line("LAUNCHER screenshot " + path)
	get_tree().quit(0)


## waits until no job runs (at most two minutes), then a few more frames for the window to show the result
func _settle() -> void:
	var start := Time.get_ticks_msec()
	var frames := 0
	while (_busy() or frames < 10) and Time.get_ticks_msec() - start < 120000:
		await get_tree().process_frame
		frames += 1
	for i in 10:
		await get_tree().process_frame


## every text below `node` a player could see on some screen: labels, buttons, links, tooltips, list and menu items, rich text, window
## titles (hidden screens included)
static func _texts(node: Node) -> PackedStringArray:
	var out := PackedStringArray()
	if node is Label or node is Button or node is LineEdit:
		out.append(node.text)
	if node is RichTextLabel:
		out.append(node.get_parsed_text())
	if node is Window:
		out.append(node.title)
	if node is Control and node.tooltip_text != "":
		out.append(node.tooltip_text)
	if node is ItemList:
		for i in node.item_count:
			out.append(node.get_item_text(i))
	if node is LinkButton:
		out.append(node.text)
		out.append(node.uri)
	if node is OptionButton:
		for i in node.item_count:
			out.append(node.get_item_text(i))
	for c in node.get_children():
		out.append_array(_texts(c))
	return out


## --ui-demo: made-up state for the screenshots of each screen (development runs and test packages only); nothing is downloaded or
## written. The folders are under the home folder, so the screenshots show how they are shortened. The notes are the releases of the
## launcher's cached release list when there is one (real notes), else text marked as demo text (the owner: never invent release facts
## in screenshots).
## Screens: main (Play), news, multiplayer, mods, games (no games found: the download), games-downloading, settings (General), display,
## about, update (a newer version, updating by itself off), downloading (an update downloading), error, glibc (Play on a Linux system older
## than the build's GLIBC, lane GLIBC-1).
func _show_demo(screen: String) -> void:
	var home := Paths.home()
	var no_games := screen in ["games", "games-downloading"]
	var games := {"found": true, "rotwk": home.path_join("Games/BFME/RotWK"), "bfme2": home.path_join("Games/BFME/BFME2"), "source": "config"}
	if no_games:
		games = {"found": false, "rotwk": "", "bfme2": "", "source": ""}
	_show_games(games, true)
	var rels := updater.cached_releases()
	if rels.is_empty():
		var sample := "DEMO TEXT for the launcher screenshots, not real release notes.\n\n## What's new\n\n- Sample note: what changed in this build.\n" + \
			"- Sample note: another change.\n\n## What's fixed\n\n- Sample note: a fix.\n\n## Known issues\n\n- Sample note: a known issue.\n"
		rels = [{"tag": "v0.4.0-alpha.1", "prerelease": true, "notes": sample, "published": "", "assets": {}},
			{"tag": "v0.3.0-preview.4", "prerelease": true, "notes": sample, "published": "", "assets": {}}]
	_releases = rels
	var chan := updater.in_channel(rels, updater.channel())
	if chan.is_empty():
		updater.set_channel("preview")  # the screenshots' launcher is a development run (Stable); the demo is a preview player's
		_show_channel()
		chan = rels
	var installed: String = chan[1].tag if screen in ["update", "downloading", "error"] and chan.size() > 1 else chan[0].tag
	_pv = installed
	_older = chan.map(func(r): return r.tag)
	_settings.set_versions([{"version": installed}], installed, _older)
	_settings.set_options(true, screen != "update", true, _about())
	_play.disabled = false
	_show_notes()
	_set_update_state("current")
	_set_status("Up to date.")
	match screen:
		"news", "multiplayer", "mods":
			_show_screen(screen)
		"settings", "display", "about":
			_show_screen("settings")
			_settings.show_tab({"settings": "general", "display": "display", "about": "about"}[screen])
		"games", "games-downloading":
			_show_screen("games")
			_games_screen.setup(Paths.default_games_dir(), aio.total_size() / 1e9, aio.packages)
			if screen == "games-downloading":
				_job_kind = "aio"
				_demo_progress("RotWK: game files 412 of 1 830", 3328599654, 9234179686)
				_games_screen.set_download_state(true, false, true, true)
				_set_status("Downloading the games. Play is back when they are checked.")
		"glibc":
			_show_error("The game did not start", "%s cannot be started: %s" % [installed, GlibcCheck.too_old("2.28", "2.27")])
		"update", "downloading", "error":
			_latest = {"tag": chan[0].tag}
			_set_update_state("update", chan[0].tag)
			_set_status("%s is available." % chan[0].tag)
			if screen == "downloading":
				_latest = {}
				_set_update_state("updating", chan[0].tag)
				_demo_progress("Downloading openbfme-%s-%s.%s" % [chan[0].tag, info.platform(), "zip" if OS.get_name() == "Windows" else "tar.gz"],
					327155712, 1073741824)
				_set_status("Downloading %s in the background. Play starts %s until it is installed." % [chan[0].tag, installed])
			if screen == "error":
				_show_error("Update not installed", "Update rejected: openbfme-%s-%s.tar.gz does not match the signed manifest: deleted, nothing was installed. The download folder was %s." % [chan[0].tag, info.platform(), home.path_join(".local/share/OpenBFMELauncher/downloads")])
	_refresh_head()
	_set_buttons()


func _demo_progress(title: String, done: int, total: int) -> void:
	_meter.rate = 8.2 * 1048576.0
	_bar.max_value = total
	_bar.value = done
	_bar_title.text = title
	_bar_label.text = _meter.text(done, total)
	_games_screen.set_progress(title + "  ·  " + _bar_label.text, done, total)
	_show_progress(true)


# ---- command line ------------------------------------------------------------------------------------------------------------------------

## lane ONLINE-2: the isolated link mode: start the version Play starts in that online game (the lobby is the one --lobby set, never the link's)
func _open_url(url_mode: Dictionary) -> int:
	var lobby := str(updater.get_state("online", "lobby", ""))
	if lobby == "":
		Log.line("LAUNCHER link %s: no online lobby is set (--lobby=<url>); online play is a test feature" % url_mode.link)
		return 1
	var lv := updater.play_version()
	if lv == "":
		Log.line("LAUNCHER nothing to play: no verified version is installed")
		return 1
	var lp := updater.play(lv, UrlHandler.game_args(url_mode, lobby))
	Log.line("LAUNCHER join game %s on %s with %s" % [url_mode.game, lobby, lv if lp.ok else "failed: " + lp.error])
	return 0 if lp.ok else 1


func _run_cli(args: PackedStringArray, problem: String) -> int:
	var url_mode := UrlHandler.isolated_url(args, info.tests_allowed(), TEST_OPTIONS)
	if url_mode.mode:
		# lane ONLINE-2: openbfme://join/<id>, alone on the command line (a refused one never gets here: _ready)
		return _open_url(url_mode) if url_mode.ok else 1
	var code := 0
	updater.progress = func(text: String, done: int, total: int) -> void:
		if done < 0:
			Log.line("LAUNCHER " + text)
	for a in args:
		if a.begins_with("--channel="):
			var ch := a.substr(10)
			if ch == "alpha":
				ch = "preview"  # lane RELNAME-1: the pre-release channel's shown name
			if not ch in ["stable", "preview"]:
				Log.line("LAUNCHER unknown channel '%s' (stable, preview)" % ch)
				return 1
			updater.set_channel(ch)
			Log.line("LAUNCHER channel %s" % ch)
	if "--version" in args:
		print("OpenBFME Launcher %s" % info.version)
	for a in args:
		if a.begins_with("--free-camera="):
			var v := a.substr(14)
			if not v in ["on", "off"]:
				Log.line("LAUNCHER --free-camera takes on or off, not '%s'" % v)
				return 1
			var why := GameOptions.set_free_camera(v == "on")
			Log.line("LAUNCHER free camera %s" % (v if why == "" else "not changed: " + why))
			if why != "":
				code = 1
	for a in args:
		if a.begins_with("--auto-update="):
			var v := a.substr(14)
			if not v in ["on", "off"]:
				Log.line("LAUNCHER --auto-update takes on or off, not '%s'" % v)
				return 1
			updater.set_auto_update(v == "on")
			Log.line("LAUNCHER updating by itself %s" % v)
	for a in args:
		if a.begins_with("--aio="):
			var v := a.substr(6)
			if not v in ["on", "off"]:
				Log.line("LAUNCHER --aio takes on or off, not '%s'" % v)
				return 1
			aio.set_enabled(v == "on")
			Log.line("LAUNCHER AIO setting %s" % v)
	for a in args:
		if a.begins_with("--aio-install="):
			var r := aio.install(a.substr(14), "--aio-consent" in args)
			if r.ok:
				Log.line("LAUNCHER AIO done: %d files downloaded, %d already there; %s (the game offers them on its first start)" % [r.downloaded, r.kept, _folders_text(r.folders)])
			else:
				if r.kind == AioInstall.STOPPED and aio.stop_request == "cancel":
					Log.line("LAUNCHER AIO removed %d partial files of this download" % aio.remove_partials(a.substr(14)))
				Log.line("LAUNCHER AIO not installed: " + r.error)
				code = {AioInstall.REFUSED: 1, AioInstall.NETWORK: 2, AioInstall.REJECTED: 3, AioInstall.STOPPED: 2}[r.kind]
	for a in args:
		if a.begins_with("--select="):
			var v := a.substr(9)
			if v != "" and not updater.installed().any(func(i): return i.version == v):
				Log.line("LAUNCHER cannot select %s: it is not installed" % v)
				code = 1
			else:
				updater.set_selected(v)
				Log.line("LAUNCHER selected %s" % (v if v != "" else "the newest installed version"))
	var wants_net := "--update" in args or "--self-update" in args or Array(args).any(func(x): return x.begins_with("--install="))
	if wants_net and problem != "":
		Log.line("LAUNCHER updates skipped: " + problem)
		code = 2
	elif wants_net:
		var r := updater.fetch_releases()
		if not r.ok:
			Log.line("LAUNCHER updates skipped: " + r.error)
			code = 2
		else:
			Log.line("LAUNCHER %d releases%s" % [r.releases.size(), " (unchanged: 304 Not Modified, the cached list)" if r.from_cache else ""])
			var chan := updater.in_channel(r.releases, updater.channel())
			if "--update" in args:
				if chan.is_empty():
					Log.line("LAUNCHER no %s release yet" % updater.channel())
				else:
					var newest := updater.newest_installed()
					var mm := updater.release_manifest(chan[0])
					if mm.ok and updater.freeze_warning(mm.manifest) != "":
						Log.line("LAUNCHER warning: " + updater.freeze_warning(mm.manifest))
					if not mm.ok:
						Log.line("LAUNCHER update rejected: " + mm.error)
						code = 3
					elif newest != "" and Semver.compare(chan[0].tag, newest) <= 0:
						Log.line("LAUNCHER up to date: %s installed, newest %s release %s" % [newest, updater.channel(), chan[0].tag])
					else:
						var i := updater.install(chan[0], false)
						Log.line("LAUNCHER update " + ("installed %s" % i.version if i.ok else "rejected: " + i.error))
						if not i.ok:
							code = 3
			for a in args:
				if a.begins_with("--install="):
					var want := a.substr(10)
					var rel = r.releases.filter(func(x): return x.tag == want)
					if rel.is_empty():
						Log.line("LAUNCHER there is no release %s" % want)
						code = 1
						continue
					var newest := updater.newest_installed()
					if newest != "" and Semver.compare(want, newest) < 0:
						Log.line("LAUNCHER installing the older %s by request (newest installed %s)" % [want, newest])
					var i := updater.install(rel[0], true)
					Log.line("LAUNCHER install " + ("installed %s" % i.version if i.ok else "rejected: " + i.error))
					if not i.ok:
						code = 3
			if "--self-update" in args:
				if chan.is_empty():
					Log.line("LAUNCHER no %s release for the launcher" % updater.channel())
				else:
					var s := updater.stage_self_update(chan[0])
					if not s.ok:
						Log.line("LAUNCHER launcher update rejected: " + s.error)
						code = 3
					elif s.staged != "":
						Log.line("LAUNCHER launcher update %s staged" % s.staged)
					else:
						Log.line("LAUNCHER launcher update: " + s.why)
	if "--list" in args:
		for i in updater.installed():
			Log.line("LAUNCHER installed %s%s" % [i.version, " (selected)" if i.version == updater.selected() else ""])
	for a in args:
		if a.begins_with("--lobby="):
			updater.set_state("online", "lobby", a.substr(8).strip_edges())
			Log.line("LAUNCHER online lobby: %s" % a.substr(8).strip_edges())
	if "--register-url-handler" in args:
		var reg := UrlHandler.register(OS.get_executable_path())
		Log.line("LAUNCHER openbfme:// links: %s" % (("registered (%s)" % reg.where) if reg.ok else "not registered: " + reg.error))
		if not reg.ok:
			code = 1
	if "--play" in args:
		var v := updater.play_version()
		if v == "":
			Log.line("LAUNCHER nothing to play: no verified version is installed")
			return 1
		var p := updater.play(v)
		Log.line("LAUNCHER play %s" % (v if p.ok else "failed: " + p.error))
		if not p.ok:
			return 1
	return code



# ---- the window --------------------------------------------------------------------------------------------------------------------------
# Lane UI-5: "Parchment & Steel", the owner's pick of ten launcher concepts (2026-10-10; the prototype's design 1): a misty backdrop, the
# left rail on dark steel (Play, News, Multiplayer, Mods, Game files, Settings), the screens beside it, and the bottom bar (the status or
# the one progress bar with Pause / Cancel, Check for updates, the gold PLAY). Errors and questions in one dialog
# (scripts/ui/message_panel.gd). Every text the window shows goes through Paths.scrub (no home folder, no user name: scripts/ui/paths.gd).
# Updates download in the background (Updater.auto_update): Play stays usable while they do and starts the installed version; a newer
# version is used from the next Play on. Play pressed during a download minimises the window, which closes once the download is done.

## the window's size before DPI scaling (project.godot's viewport; canvas_items stretch scales everything with the window)
const BASE_SIZE := Vector2(1280, 720)
const RAIL_W := 214.0
const BAR_H := 96.0
const SCREENS := [["play", "swords", "Play"], ["news", "star", "News"], ["multiplayer", "rings", "Multiplayer"], ["mods", "hammer", "Mods"],
	["games", "drive", "Game files"], ["settings", "gear", "Settings"]]


## A DPI-scaled window: on a screen with a scale above 1 (Windows: its DPI / 96) the window grows by that factor, within 92% of the
## usable screen; the canvas_items stretch scales the content with it. Not with --resolution (the screenshots) or without a display.
func _fit_window() -> void:
	if DisplayServer.get_name() == "headless":
		return
	var win := get_window()
	win.title = "OpenBFME Launcher"
	win.min_size = Vector2i(960, 540)
	if "--resolution" in OS.get_cmdline_args() or OS.get_cmdline_args().has("--fullscreen"):
		return
	var scr := win.current_screen
	var scale := DisplayServer.screen_get_scale(scr)
	if OS.get_name() == "Windows":
		scale = DisplayServer.screen_get_dpi(scr) / 96.0
	if scale <= 1.05:
		return
	var usable := Vector2(DisplayServer.screen_get_usable_rect(scr).size)
	var want := BASE_SIZE * scale
	want *= minf(1.0, minf(usable.x * 0.92 / want.x, usable.y * 0.92 / want.y))
	win.size = Vector2i(want)
	win.move_to_center()


func _build_ui() -> void:
	_fit_window()
	theme = Style.make_theme()
	add_child(Textures.surface("mist"))

	# the left rail
	var rail := Control.new()
	rail.anchor_bottom = 1.0
	rail.offset_right = RAIL_W
	add_child(rail)
	rail.add_child(Textures.surface("steel"))
	var edge := ColorRect.new()
	edge.color = Style.LINE
	edge.anchor_left = 1.0
	edge.anchor_right = 1.0
	edge.anchor_bottom = 1.0
	edge.offset_left = -1
	rail.add_child(edge)
	var rm := MarginContainer.new()
	rm.set_anchors_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right"]:
		rm.add_theme_constant_override("margin_" + side, 13)
	rm.add_theme_constant_override("margin_top", 22)
	rm.add_theme_constant_override("margin_bottom", 16)
	rail.add_child(rm)
	var rv := W.vbox(6)
	rm.add_child(rv)
	var logo := W.label("OPENBFME")
	Style.text(logo, "Cinzel", 700, 25, Style.ACCENT, 2)
	var lm := MarginContainer.new()
	lm.add_theme_constant_override("margin_left", 9)
	lm.add_theme_constant_override("margin_bottom", 16)
	lm.add_child(logo)
	rv.add_child(lm)
	var group := ButtonGroup.new()
	for s in SCREENS:
		var b := W.button(s[2], s[1], "NavButton", 18)
		b.toggle_mode = true
		b.button_group = group
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.pressed.connect(_show_screen.bind(s[0]))
		rv.add_child(b)
		_nav[s[0]] = b
	rv.add_child(W.spacer())
	rv.get_child(rv.get_child_count() - 1).size_flags_vertical = Control.SIZE_EXPAND_FILL
	var foot := W.label("Launcher %s" % info.version, "Muted")
	foot.add_theme_font_size_override("font_size", 12)
	var fm := MarginContainer.new()
	fm.add_theme_constant_override("margin_left", 10)
	fm.add_child(foot)
	rv.add_child(fm)

	# the screens
	var content := MarginContainer.new()
	content.set_anchors_preset(Control.PRESET_FULL_RECT)
	content.offset_left = RAIL_W
	content.offset_bottom = -BAR_H
	content.add_theme_constant_override("margin_left", 34)
	content.add_theme_constant_override("margin_right", 34)
	content.add_theme_constant_override("margin_top", 28)
	content.add_theme_constant_override("margin_bottom", 4)
	add_child(content)
	var cv := W.vbox(12)
	content.add_child(cv)
	# the freeze warning (its own line: round 3, Sol r2)
	_banner = PanelContainer.new()
	_banner.theme_type_variation = "Banner"
	_banner.visible = false
	var brow := W.hbox(10)
	var bicon := TextureRect.new()
	bicon.texture = VectorIcon.make("warning", 18)
	bicon.modulate = Style.WARN
	bicon.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	brow.add_child(bicon)
	_warning = W.wrap("")
	_warning.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	brow.add_child(_warning)
	_banner.add_child(brow)
	cv.add_child(_banner)
	var stack := Control.new()
	stack.size_flags_vertical = Control.SIZE_EXPAND_FILL
	cv.add_child(stack)
	_play_screen = PlayScreen.new()
	_play_screen.download_games.connect(_open_download)
	_play_screen.show_news.connect(_show_screen.bind("news"))
	_news = NewsScreen.new()
	var mp := MultiplayerScreen.new()
	mp.build(info.repo)
	_mods = ModsScreen.new()
	_games_screen = GamesScreen.new()
	_games_screen.download.connect(_start_aio)
	_games_screen.forget_folders.connect(_on_forget_folders)
	_games_screen.show_folder.connect(_show_folder)
	_games_screen.pause_pressed.connect(_on_pause)
	_games_screen.cancel_pressed.connect(_on_cancel)
	_settings = SettingsScreen.new()
	_settings.channel_picked.connect(_on_channel)
	_settings.free_camera_toggled.connect(_on_free_camera)
	_settings.auto_update_toggled.connect(_on_auto_update)
	_settings.aio_toggled.connect(_on_aio_toggled)
	_settings.use_version.connect(_on_use)
	_settings.install_release.connect(_on_install_pick)
	_settings.open_logs.connect(_open_logs)
	_screens = {"play": _play_screen, "news": _news, "multiplayer": mp, "mods": _mods, "games": _games_screen, "settings": _settings}
	for k in _screens:
		stack.add_child(_screens[k])

	# the bottom bar: status or progress, Update, Check for updates (Pause / Cancel while downloading), PLAY
	var bar_root := Control.new()
	bar_root.anchor_top = 1.0
	bar_root.anchor_right = 1.0
	bar_root.anchor_bottom = 1.0
	bar_root.offset_left = RAIL_W
	bar_root.offset_top = -BAR_H
	add_child(bar_root)
	var shade := TextureRect.new()
	shade.set_anchors_preset(Control.PRESET_FULL_RECT)
	shade.mouse_filter = Control.MOUSE_FILTER_IGNORE
	shade.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	shade.stretch_mode = TextureRect.STRETCH_SCALE
	var g := GradientTexture2D.new()
	g.gradient = Gradient.new()
	g.gradient.offsets = PackedFloat32Array([0.0, 0.45, 1.0])
	g.gradient.colors = PackedColorArray([Color(0, 0, 0, 0), Color(0, 0, 0, 0.85), Color(0, 0, 0, 0.85)])
	g.fill_from = Vector2(0, 0)
	g.fill_to = Vector2(0, 1)
	g.width = 4
	g.height = 64
	shade.texture = g
	bar_root.add_child(shade)
	var bm := MarginContainer.new()
	bm.set_anchors_preset(Control.PRESET_FULL_RECT)
	bm.add_theme_constant_override("margin_left", 34)
	bm.add_theme_constant_override("margin_right", 30)
	bm.add_theme_constant_override("margin_top", 10)
	bm.add_theme_constant_override("margin_bottom", 10)
	bar_root.add_child(bm)
	var bar := W.hbox(14)
	bm.add_child(bar)
	var info_box := W.vbox(4)
	info_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	info_box.alignment = BoxContainer.ALIGNMENT_CENTER
	bar.add_child(info_box)
	var head := W.hbox(0)
	_status_head = W.label("")
	Style.text(_status_head, "Inter", 600, 14, Style.TEXT)
	head.add_child(_status_head)
	_status_tail = W.label("")
	Style.text(_status_tail, "Inter", 500, 14, Style.TEXT2)
	_status_tail.clip_text = true
	_status_tail.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	_status_tail.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(_status_tail)
	info_box.add_child(head)
	_progress = W.vbox(5)
	_progress.visible = false
	var trow := W.hbox(10)
	_bar_title = W.label("", "Muted")
	_bar_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_bar_title.clip_text = true
	_bar_title.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	trow.add_child(_bar_title)
	_bar_label = W.label("", "Muted")
	trow.add_child(_bar_label)
	_progress.add_child(trow)
	_bar = W.progress_bar()
	_progress.add_child(_bar)
	info_box.add_child(_progress)
	_status = W.label("", "Muted")  # one line (the whole message: its tooltip, the log, the error dialog)
	_status.clip_text = true
	_status.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	_status.mouse_filter = Control.MOUSE_FILTER_PASS
	info_box.add_child(_status)
	_update = W.button("Update", "download", "GoldButton", 16)
	_update.visible = false
	_update.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_update.pressed.connect(func() -> void: _start_job(_job_update.bind(_latest), "install", String(_latest.get("tag", ""))))
	bar.add_child(_update)
	_check = W.button("Check for updates", "refresh", "", 15)
	_check.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_check.pressed.connect(func() -> void: _start_job(_job_check.bind(updater.auto_update()), "check"))
	bar.add_child(_check)
	_pause = W.button("Pause", "pause", "", 14)
	_pause.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_pause.pressed.connect(_on_pause)
	bar.add_child(_pause)
	_cancel = W.button("Cancel", "stop", "", 14)
	_cancel.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_cancel.pressed.connect(_on_cancel)
	bar.add_child(_cancel)
	_play = GoldButton.make("PLAY", Vector2(236, 60))
	_play.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_play.pressed.connect(_on_play)
	bar.add_child(_play)

	_message = MessagePanel.new()
	_message.open_logs.connect(_open_logs)
	_message.closed.connect(func() -> void: updater.set_state("launcher", "unseen_error", ""))
	add_child(_message)

	var recheck := Timer.new()
	recheck.wait_time = RECHECK_SECONDS
	recheck.timeout.connect(_on_recheck)
	add_child(recheck)
	recheck.start()

	_show_channel()
	_show_progress(false)
	_set_update_state("idle")
	_refresh_games()
	_show_screen("play")
	_play.grab_focus.call_deferred()


func _show_screen(key: String) -> void:
	_screen = key
	for k in _screens:
		_screens[k].visible = k == key
		_nav[k].set_pressed_no_signal(k == key)
	if key == "games" and _games_screen.folder == "":
		_games_screen.setup(Paths.default_games_dir(), aio.total_size() / 1e9, aio.packages)
	if key == "settings":
		_settings.set_options(GameOptions.free_camera(), updater.auto_update(), aio.enabled(), _about())


func _show_channel() -> void:
	_settings.set_channel(updater.channel())
	_refresh_head()


## the update state: "idle" (no check yet), "checking", "updating" (an update downloads), "current" (up to date), "update" (`tag` is
## newer: the gold Update button), "failed" (the check did not finish: the status says why), "none" (no release of the channel)
func _set_update_state(state: String, tag := "") -> void:
	_update_state = state
	_update_tag = tag
	_update.visible = state == "update"
	if state == "update":
		_update.text = ("Update launcher to " if _launcher_only else "Update to " if _pv != "" else "Install ") + tag
	_refresh_head()


## the bottom bar's first line and the Play screen's chips: the update state, the version Play starts, the channel
func _refresh_head() -> void:
	if _status_head == null:
		return
	var ch := Semver.channel_name(updater.channel())  # lane RELNAME-1: the pre-release channel is shown as "Alpha"
	var head: String = {"idle": "Ready", "checking": "Checking for updates", "updating": "Updating", "current": "Up to date",
		"update": "Update available", "failed": "Could not check for updates", "none": "No %s release yet" % ch.to_lower()}[_update_state]
	var colour: Color = {"idle": Style.TEXT2, "checking": Style.TEXT2, "updating": Style.ACCENT, "current": Style.GOOD, "update": Style.ACCENT,
		"failed": Style.WARN, "none": Style.TEXT2}[_update_state]
	if _update_state == "update":
		head = ("Launcher %s is available" if _launcher_only else "%s is available") % _update_tag
	if _update_state == "updating" and _update_tag != "":
		head = "Updating to %s" % _update_tag
	if _quit_when_done:
		head = "Playing %s" % _pv
	_status_head.text = head
	_status_tail.text = "  ·  %s  ·  %s channel" % [_pv if _pv != "" else "nothing installed yet", ch]
	W.set_chip(_play_screen.state_chip, "●  " + head, colour)
	W.set_chip(_play_screen.channel_chip, "Channel: " + ch)
	W.set_chip(_play_screen.version_chip, _pv if _pv != "" else "Not installed")
	_status.visible = _status.text.trim_suffix(".") != head


func _on_free_camera(on: bool) -> void:
	var why := GameOptions.set_free_camera(on)
	if why != "":
		_show_error("The setting was not saved", "The free camera setting was not saved: " + why)
		_settings.set_options(GameOptions.free_camera(), updater.auto_update(), aio.enabled(), _about())
		return
	Log.line("LAUNCHER free camera %s (%s)" % ["on" if on else "off", GameOptions.options_path()])
	_set_status("Free camera %s: it applies the next time the game starts." % ["on" if on else "off"])


func _on_auto_update(on: bool) -> void:
	updater.set_auto_update(on)
	_set_status("Updates install by themselves in the background." if on else "Updates are only checked: press Update to install one.")


func _on_aio_toggled(on: bool) -> void:
	aio.set_enabled(on)
	_set_status("Offering the game download is %s." % ["on" if on else "off"])
	_refresh_games()


func _on_forget_folders() -> void:
	var why := Paths.forget_game_folders()
	if why != "":
		_show_error("The folders were not reset", why)
		return
	_set_status("The game asks for its folders on its next start.")
	_refresh_games()


func _open_download() -> void:
	_show_screen("games")


func _open_logs() -> void:
	_show_folder(OS.get_user_data_dir())


func _show_folder(path: String) -> void:
	var why := Paths.show_in_file_manager(path)
	if why != "":
		_set_status("The folder was not opened: " + why)


func _about() -> String:
	return "OpenBFME Launcher %s (%s)" % [info.version, info.platform()]


func _show_error(title: String, text: String) -> void:
	_set_status(text)
	_message.show_error(title, text, _about())


func _start_aio(dir: String) -> void:
	if dir == "" or not dir.is_absolute_path():
		_show_error("No folder", "Pick a folder for the games first.")
		return
	_aio_target = dir
	_start_job(_job_aio.bind(dir), "aio")


## Pause stops the transfer and keeps the partial file (Resume continues it with a Range request); while paused it reads "Resume".
func _on_pause() -> void:
	if _busy():
		_stop = "pause"
		if _job_kind == "aio":
			aio.request_stop("pause")
		else:
			updater.http.cancelled = true
	elif _paused == "aio":
		_start_aio(_aio_target)
	elif _paused == "game":
		_start_job(_job_install.bind(_paused_release, _paused_pick), "install", String(_paused_release.get("tag", "")))
	elif _paused == "launcher":
		_start_job(_job_stage.bind(_paused_release), "install")


## Cancel stops the transfer and removes the partial file (the AIO download keeps the files it already checked).
func _on_cancel() -> void:
	if _busy():
		_stop = "cancel"
		if _job_kind == "aio":
			aio.request_stop("cancel")
		else:
			updater.http.cancelled = true
		return
	if _paused == "aio":
		var n := aio.remove_partials(_aio_target)
		_set_status("The download was cancelled (%d partial files removed; the checked files are kept)." % n)
	elif _paused in ["game", "launcher"]:
		_remove_partials(_paused_assets)
		_set_status("The launcher update was cancelled." if _paused == "launcher" else "The update was cancelled.")
	_paused = ""
	_show_progress(false)
	_set_buttons()


## After a Cancel: the partial downloads of the cancelled job's own assets (downloads/<asset>.part and its .part.json, written by
## Updater.fetch_asset), nothing else in that folder (Sol r1: every .part went, also other versions' resumable downloads)
func _remove_partials(assets: Array) -> void:
	var dl := updater.data_dir.path_join("downloads")
	for a in assets:
		for suffix in [".part", ".part.json"]:
			var f := dl.path_join(String(a) + suffix)
			if FileAccess.file_exists(f):
				DirAccess.remove_absolute(f)


## the names of `release`'s assets of these kinds ("game", "launcher") for this platform, from its verified manifest (worker thread;
## the manifest is cached, the install reads it anyway); [] when it does not verify (then nothing is downloaded either)
func _assets_of(release: Dictionary, kinds: Array) -> Array:
	var mm := updater.release_manifest(release)
	var out := []
	if mm.ok:
		for k in kinds:
			out.append(String(mm.manifest.assets[k][info.platform()].name))
	return out


static func _folders_text(folders: Dictionary) -> String:
	var parts := PackedStringArray()
	for k in folders:
		parts.append("%s in %s" % [String(k).to_upper(), folders[k]])
	return ", ".join(parts)


func _set_warning(text: String) -> void:
	_warning.text = "Warning: " + text if text != "" else ""
	_banner.visible = text != ""


func _set_status(text: String) -> void:
	Log.line("LAUNCHER " + text)
	if _status != null:
		_status.text = Paths.scrub(text)
		_status.tooltip_text = _status.text
		_refresh_head()


func _refresh_installed() -> void:
	if _demo != "":
		return
	_pv = updater.play_version()
	_settings.set_versions(updater.installed(), _pv, _older)
	_set_buttons()
	_show_notes()
	_refresh_head()


## the notes cards: What's New of the version Play starts (else of the channel's newest release), and News (every release of the
## channel); from the last check's release list, else the cached one
func _show_notes() -> void:
	var rels := _releases if not _releases.is_empty() else updater.cached_releases()
	var chan := updater.in_channel(rels, updater.channel())
	_news.set_releases(chan)
	var mine := rels.filter(func(r): return r.tag == _pv)
	if _pv != "" and not mine.is_empty():
		var when := ReleaseNotes.date_text(str(mine[0].published))
		_play_screen.set_notes(ReleaseNotes.title_of(_pv) + (" · " + when if when != "" else ""), mine[0].notes, "(no release notes)")
	elif not chan.is_empty():
		_play_screen.set_notes("Newest: " + ReleaseNotes.title_of(chan[0].tag), chan[0].notes, "(no release notes)")
	else:
		_play_screen.set_notes("", "", "The notes of your version appear here after the first update check.")


func _refresh_games() -> void:
	_show_games(Paths.game_folders())


func _show_games(g: Dictionary, demo := false) -> void:
	_games = g
	var where := ""
	if g.found:
		var r := Paths.short(g.rotwk)
		var b := Paths.short(g.bfme2)
		var common := r.get_base_dir()
		where = ("In " + common) if common == b.get_base_dir() and common != "" else "%s and %s" % [r, b]
	_play_screen.set_games(g.found, aio.enabled(), where)
	_mods.set_games(g.found)
	_games_screen.set_folders(g, demo)
	_set_buttons()


func _busy() -> bool:
	return _thread != null and _thread.is_alive()


## Play waits only for a job that changes the version it starts (a reinstall of it) or downloads the games; a background update of a
## newer version does not stop it (that version is used from the next Play on)
func _play_blocked() -> bool:
	if not _busy():
		return false
	return _job_kind == "aio" or (_job_target != "" and _job_target == _pv)


func _set_buttons() -> void:
	if _play == null:
		return
	var busy := _busy()
	var downloading := _progress.visible
	_check.visible = not downloading
	_check.disabled = busy
	_pause.visible = downloading
	_cancel.visible = downloading
	_update.disabled = busy or _latest.is_empty()
	_play.disabled = _pv == "" or _play_blocked()
	_play_screen.games_button.disabled = busy or _paused != ""
	_settings.set_busy(busy)
	var aio_running := busy and _job_kind == "aio"
	_games_screen.set_download_state(aio_running, _paused == "aio", busy and not aio_running, aio.enabled())


func _start_job(job: Callable, kind: String, target := "") -> void:
	if _busy():
		return
	if _thread != null:
		_thread.wait_to_finish()
	_job_kind = kind
	_job_target = target
	_stop = ""
	_paused = ""
	updater.http.cancelled = false
	_meter.reset()
	if kind == "check":
		_set_update_state("checking")
	updater.progress = func(text: String, done: int, total: int) -> void:
		_on_progress.call_deferred(text, done, total)
	_thread = Thread.new()
	_thread.start(job)
	_set_buttons()


## `err`: the job failed (its message is shown in the dialog by the caller)
func _finish(text: String, err := "") -> void:
	if _thread != null:
		_thread.wait_to_finish()
		_thread = null
	_set_status(text)
	_job_kind = ""
	_job_target = ""
	_show_progress(_paused != "")
	_refresh_installed()
	_set_buttons()
	if _quit_when_done:
		# Play was pressed while this ran: close now, unless the player brought the window back
		var mode := get_window().mode if DisplayServer.get_name() != "headless" else Window.MODE_MINIMIZED
		_quit_when_done = mode == Window.MODE_MINIMIZED
		if _quit_when_done and err != "":
			# lane UI-5 (Sol r1): a failure is never swallowed by the close: the window comes back with the dialog, and the failure is kept
			# in state.cfg until the dialog is closed (a launcher closed without reading it shows it at the next start)
			_quit_when_done = false
			updater.set_state("launcher", "unseen_error", err)
			Log.line("LAUNCHER the background update failed while the game runs: the launcher stays open to show it (%s)" % err)
			if DisplayServer.get_name() != "headless":
				get_window().mode = Window.MODE_WINDOWED
				get_window().grab_focus()
			_refresh_head()
		elif _quit_when_done:
			Log.line("LAUNCHER the background update finished while the game runs: the launcher closes (%s)" % text)
			get_tree().quit(0)


## the hourly check while the launcher stays open (installs in the background when updating by itself is on)
func _on_recheck() -> void:
	if _busy() or _paused != "" or _problem != "" or _demo != "":
		return
	Log.line("LAUNCHER the hourly update check")
	_start_job(_job_check.bind(updater.auto_update()), "check")


func _show_progress(on: bool) -> void:
	_progress.visible = on
	_pause.text = "Resume" if _paused != "" else "Pause"
	_pause.icon = VectorIcon.make("resume" if _paused != "" else "pause", 14)
	if not on:
		_bar_title.text = ""
		_bar_label.text = ""
	_set_buttons()


func _on_progress(text: String, done: int, total: int) -> void:
	if _ui_stop != "" and total > 0 and done < total and _busy() and text.contains(_ui_stop.get_slice("@", 1)):
		var how := _ui_stop.get_slice("@", 0)
		_ui_stop = ""
		Log.line("LAUNCHER ui test: %s pressed during %s" % [how, text])
		match how:
			"pause":
				_on_pause()
			"play":
				_on_play()
			_:
				_on_cancel()
	if total > 0:
		_meter.feed(done, Time.get_ticks_msec())
		_bar.max_value = total
		_bar.value = done
		_bar_title.text = Paths.scrub(text)
		_bar_label.text = _meter.text(done, total)
		if _job_kind == "aio":
			_games_screen.set_progress(_bar_title.text + "  ·  " + _bar_label.text, done, total)
		elif _job_kind in ["check", "install"] and text.begins_with("Downloading") and _update_state != "updating":
			var m := RegEx.create_from_string("-(v\\d+\\.\\d+\\.\\d+(?:-(?:alpha|preview)\\.\\d+)?)-").search(text)
			_set_update_state("updating", m.get_string(1) if m != null else _update_tag)
		_show_progress(true)
	else:
		_bar_title.text = Paths.scrub(text)
		_status.text = Paths.scrub(text)
		_refresh_head()


# ---- jobs (worker thread) ----------------------------------------------------------------------------------------------------------------

func _job_check(auto_install: bool) -> void:
	var r := updater.fetch_releases()
	if not r.ok:
		_after_check.call_deferred({}, [], "Updates skipped: %s. Play starts the newest installed version." % r.error, "failed", "")
		return
	_releases = r.releases
	var chan := updater.in_channel(r.releases, updater.channel())
	if chan.is_empty():
		_after_check.call_deferred({}, chan, "There is no %s release yet." % Semver.channel_name(updater.channel()).to_lower(), "none", "")
		return
	var mm := updater.release_manifest(chan[0])
	if not mm.ok:
		_after_check.call_deferred({}, chan, "Update rejected: " + mm.error, "failed", "Update rejected: " + mm.error)
		return
	var newest := updater.newest_installed()
	var newer := newest == "" or Semver.compare(chan[0].tag, newest) > 0
	var msg := "Up to date." if not newer else "%s is available." % chan[0].tag
	var err := ""
	# the freeze warning has its own line in the window (round 3: folded into the status, it was overwritten by the install's messages)
	var freeze := updater.freeze_warning(mm.manifest)
	if freeze != "":
		Log.line("LAUNCHER warning: " + freeze)
	_set_warning.call_deferred(freeze)
	if newer and auto_install:
		_job_assets = _assets_of(chan[0], ["game"])
		var i := updater.install(chan[0], false)
		if i.ok:
			msg = "Installed %s." % i.version
		elif _stop != "":
			msg = _stopped("game", chan[0], false)
		else:
			msg = "Update rejected: " + i.error
			err = msg
		newer = not i.ok
	var s := {"ok": true, "staged": ""}
	if _stop == "" and auto_install:  # lane UI-5: the launcher's own update follows the "update automatically" setting too
		_job_assets = _assets_of(chan[0], ["launcher"])
		s = updater.stage_self_update(chan[0])
	if not s.ok and _stop != "":
		msg = _stopped("launcher", chan[0], false)  # Sol r1: a stopped launcher download was reported as rejected
	elif not s.ok:
		msg += " Launcher update rejected: " + s.error
		err = (err + " " if err != "" else "") + "Launcher update rejected: " + s.error
	elif s.staged != "":
		msg += " The launcher %s is ready and replaces this one on the next start." % s.staged
	# lane UI-5 (Sol r1): updating by itself off, the launcher's own update is offered on its own too (a launcher older than the
	# channel's newest release while the game is up to date said "Up to date" with no Update button)
	var launcher_only := not newer and not auto_install and _stop == "" and updater.launcher_update_available(chan[0])
	if launcher_only:
		msg = "The launcher %s is available." % chan[0].tag
	_after_check.call_deferred(chan[0] if newer or launcher_only else {}, chan, msg, "update" if newer or launcher_only else "current", err,
		launcher_only)


## a game ("game") or launcher ("launcher") download the player paused or cancelled (worker thread): the message, and what Resume
## continues; a cancel removes this job's partial files
func _stopped(kind: String, release: Dictionary, user_pick: bool) -> String:
	if _stop == "pause":
		_paused = kind
		_paused_release = release
		_paused_pick = user_pick
		_paused_assets = _job_assets.duplicate()
		return "The launcher update is paused. Resume continues it." if kind == "launcher" else "Paused. Resume continues where it stopped."
	_remove_partials(_job_assets)
	return "The launcher update was cancelled." if kind == "launcher" else "The update was cancelled."


func _after_check(latest: Dictionary, chan: Array, msg: String, state: String, err: String, launcher_only := false) -> void:
	_latest = latest
	_launcher_only = launcher_only
	_older = []
	for r in chan:
		_older.append(r.tag)
	_set_update_state(state, chan[0].tag if not chan.is_empty() else "")
	# a check that did not finish is a failure too while the game runs (the player would otherwise never see it)
	var problem := err if err != "" else (msg if state == "failed" and _quit_when_done else "")
	_finish(msg, problem)
	if problem != "":
		_message.show_error("Update not installed", problem, _about())


func _job_install(release: Dictionary, user_pick: bool) -> void:
	_job_assets = _assets_of(release, ["game"])
	var i := updater.install(release, user_pick)
	if i.ok:
		_after_install.call_deferred("Installed %s." % i.version, "")
	elif _stop != "":
		_after_install.call_deferred(_stopped("game", release, user_pick), "")
	else:
		_after_install.call_deferred("Not installed: " + i.error, i.error)


## lane UI-5: the Update button (updating by itself is off, or it failed): the newer game version, then the launcher's own update
func _job_update(release: Dictionary) -> void:
	_job_assets = _assets_of(release, ["game"])
	var newest := updater.newest_installed()
	var msg := ""
	if newest == "" or Semver.compare(release.tag, newest) > 0:
		var i := updater.install(release, false)
		if not i.ok:
			if _stop != "":
				_after_install.call_deferred(_stopped("game", release, false), "")
			else:
				_after_install.call_deferred("Not installed: " + i.error, i.error)
			return
		msg = "Installed %s." % i.version
	_job_assets = _assets_of(release, ["launcher"])
	var s := updater.stage_self_update(release)
	if not s.ok and _stop != "":
		_after_install.call_deferred(_stopped("launcher", release, false), "")
	elif not s.ok:
		_after_install.call_deferred((msg + " " if msg != "" else "") + "Launcher update rejected: " + s.error, "Launcher update rejected: " + s.error)
	else:
		if s.staged != "":
			msg += (" " if msg != "" else "") + "The launcher %s is ready and replaces this one on the next start." % s.staged
		_after_install.call_deferred(msg if msg != "" else "Up to date.", "")


## Resume of a paused launcher download: stage the launcher of `release` (Updater.stage_self_update resumes its partial file)
func _job_stage(release: Dictionary) -> void:
	_job_assets = _assets_of(release, ["launcher"])
	var s := updater.stage_self_update(release)
	if s.ok:
		_after_install.call_deferred("The launcher %s is ready and replaces this one on the next start." % s.staged if s.staged != "" \
			else "Launcher update: " + str(s.get("why", "")), "")
	elif _stop != "":
		_after_install.call_deferred(_stopped("launcher", release, false), "")
	else:
		_after_install.call_deferred("Launcher update rejected: " + s.error, "Launcher update rejected: " + s.error)


func _after_install(msg: String, err: String) -> void:
	if err == "" and _paused == "" and (msg.begins_with("Installed") or msg == "Up to date." or msg.begins_with("The launcher")):
		_latest = {}
		_launcher_only = false
		_set_update_state("current")
	_finish(msg, err)
	if err != "":
		_message.show_error("Not installed", err, _about())


func _job_aio(dir: String) -> void:
	var r := aio.install(dir, true)
	_after_aio.call_deferred(r)


func _after_aio(r: Dictionary) -> void:
	if r.ok:
		_finish("The games are downloaded and checked: %s. OpenBFME offers these folders when it starts." % _folders_text(r.folders))
		_refresh_games()
	elif r.kind == AioInstall.STOPPED and aio.stop_request == "pause":
		_paused = "aio"
		_finish("The download is " + r.error)
	else:
		if r.kind == AioInstall.STOPPED:
			aio.remove_partials(_aio_target)
			_finish("The download was " + r.error)
			return
		if r.kind == AioInstall.NETWORK:
			_paused = "aio"  # a broken transfer can be resumed
		_finish("The games were not downloaded: " + r.error)
		_message.show_error("The games were not downloaded", r.error, _about())


func _on_channel(ch: String) -> void:
	updater.set_channel(ch)
	_show_channel()
	_show_notes()
	_start_job(_job_check.bind(updater.auto_update()), "check")


func _on_use(v: String) -> void:
	updater.set_selected(v)
	_set_status("Play starts %s from now on." % v)
	_refresh_installed()


func _on_install_pick(tag: String) -> void:
	var rel = _releases.filter(func(x): return x.tag == tag)
	if rel.is_empty():
		return
	var newest := updater.newest_installed()
	if newest != "" and Semver.compare(tag, newest) < 0:
		_message.ask("Install an older version?", "%s is older than the installed %s. Install it and play it from now on?" % [tag, newest],
			"Install", func() -> void: _start_job(_job_install.bind(rel[0], true), "install", tag))
	else:
		_start_job(_job_install.bind(rel[0], true), "install", tag)


## Play: the version Play starts. While an update downloads in the background, the game starts anyway; the window is minimised and
## closes once the download is done (the new version is used from the next Play on).
func _on_play() -> void:
	var v := updater.play_version()
	if v == "" or _play_blocked():
		return
	var p := updater.play(v)
	if p.ok:
		if _busy():
			_quit_when_done = true
			_set_status("Playing %s. The update keeps downloading in the background; your next Play starts the new version." % v)
			if DisplayServer.get_name() != "headless":
				get_window().mode = Window.MODE_MINIMIZED
			return
		get_tree().quit(0)
		return
	_set_status(p.error)
	if p.get("modified", false):
		# a changed installed version is never started; offer to download it again (a user pick: the same version may be reinstalled)
		var rel = _releases.filter(func(x): return x.tag == v)
		if rel.is_empty():
			_show_error("The game did not start", p.error + " It cannot be downloaded now: the release list is not available.")
			return
		_message.ask("Download it again?", "%s was changed after it was installed and is not started. Download it again?" % v, "Download",
			func() -> void: _start_job(_job_install.bind(rel[0], true), "install", v))
		return
	_show_error("The game did not start", p.error)


func _exit_tree() -> void:
	if _thread != null:
		if _job_kind == "aio":
			aio.request_stop("pause")
		updater.http.cancelled = true
		_thread.wait_to_finish()
	if _instance != null:
		_instance.release()
