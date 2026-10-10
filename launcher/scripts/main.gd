## Lane LAUNCH-1: the OpenBFME Launcher's window and command line.
##
## The window (lane UI-3's redesign, see the window section below): the version Play starts and its update state with one button, the
## channel (Stable / Preview), the release notes, one progress bar (MB, MB/s, time left, Pause / Cancel) and Play; Settings behind the
## gear (game folders, free camera, the installed versions and older releases, the log folder); Download the games only while no game
## folders are found. At start it checks for updates, installs a newer game build of the channel (verified, see scripts/core/updater.gd)
## and stages a newer launcher. Offline or on an API error it says why the update was skipped and Play starts the newest verified
## installed version. The window shows no home folder or user name (scripts/ui/paths.gd).
##
## Command line (after `--`; with any of these the launcher runs without a window and exits, for tests and scripted use):
##   --update                 check and install the channel's newest version when it is newer than every installed one
##   --install=<version>      install that release (the user's pick: an older version is allowed)
##   --select=<version>       play that installed version from now on (--select= clears the pick)
##   --self-update            stage a newer launcher (replaced on the next start)
##   --channel=stable|preview remember the channel
##   --list                   print the installed versions
##   --play                   start the version Play starts
##   --version                print the launcher's version
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
##   after that file was kept (the pause / cancel paths); --ui-demo=main|update|downloading|settings|games|error   made-up state for the
##   screenshots of each screen (no network; tools/release/launcher_screens.py); --ui-stop=<pause|cancel>@<text>   press Pause / Cancel
##   once while a download whose progress names <text> runs, --ui-then=resume|cancel   then press Resume / Cancel once (with --screenshot)
## Exit status: 0 done; 1 an install / play / select failed (or the AIO download was refused: setting off, no consent, a bad folder); 2
## updates skipped (offline, API error; an AIO download that broke or was stopped: it resumes next time); 3 an update was rejected (signature, size,
## digest, archive or rollback check; the AIO service's data failed a check). Every step prints a LAUNCHER line (also in user://launcher.log).
extends Control

const BuildInfo := preload("res://scripts/core/build_info.gd")
const Updater := preload("res://scripts/core/updater.gd")
const SelfUpdate := preload("res://scripts/core/self_update.gd")
const Semver := preload("res://scripts/core/semver.gd")
const Log := preload("res://scripts/core/launcher_log.gd")
const GameOptions := preload("res://scripts/core/game_options.gd")
const AioInstall := preload("res://scripts/core/aio_install.gd")
const Style := preload("res://scripts/ui/style.gd")
const W := preload("res://scripts/ui/widgets.gd")
const Paths := preload("res://scripts/ui/paths.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const ProgressMeter := preload("res://scripts/ui/progress_meter.gd")
const Backdrop := preload("res://scripts/ui/backdrop.gd")
const SettingsPanel := preload("res://scripts/ui/settings_panel.gd")
const DownloadPanel := preload("res://scripts/ui/download_panel.gd")
const MessagePanel := preload("res://scripts/ui/message_panel.gd")

const CLI_ACTIONS := ["--update", "--install=", "--select=", "--self-update", "--channel=", "--list", "--play", "--version", "--free-camera=",
	"--aio=", "--aio-install="]

var info: RefCounted
var updater: Updater
var aio: AioInstall
var _thread: Thread
var _releases: Array = []
var _older: Array = []        # the channel's release tags, newest first (Settings: install another one)
var _latest: Dictionary = {}  # the newer release the Update button installs ({} when up to date)
var _games: Dictionary = {}   # Paths.game_folders()
var _meter := ProgressMeter.new()
var _job_kind := ""           # the running job: "check", "install" or "aio"
var _stop := ""               # "pause" / "cancel" asked of the running game download
var _paused := ""             # "game" / "launcher" / "aio": a download stopped by Pause (Resume continues it)
var _paused_release: Dictionary = {}
var _paused_pick := false
var _job_assets: Array = []   # the release assets the running game / launcher job downloads (its downloads/<name>.part files)
var _paused_assets: Array = []
var _ui_stop := ""            # tests: --ui-stop=<pause|cancel>@<text>, pressed once when a download whose progress names <text> runs
var _ui_then := ""            # tests: --ui-then=resume|cancel, pressed once after that stop
var _aio_target := ""         # the folder of the AIO download that runs or is paused
var _demo := ""

var _installed_label: Label
var _pill: PanelContainer
var _pill_icon: TextureRect
var _pill_text: Label
var _update: Button
var _stable: Button
var _preview: Button
var _check: Button
var _gear: Button
var _banner: PanelContainer
var _warning: Label
var _games_card: PanelContainer
var _games_button: Button
var _games_found: PanelContainer
var _games_where: Label
var _notes_title: Label
var _notes: RichTextLabel
var _progress: VBoxContainer
var _bar: ProgressBar
var _bar_title: Label
var _bar_label: Label
var _pause: Button
var _cancel: Button
var _status: Label
var _play: Button
var _settings: SettingsPanel
var _download: DownloadPanel
var _message: MessagePanel


func _ready() -> void:
	Log.open()
	info = BuildInfo.load_info()
	updater = Updater.new(info)
	aio = AioInstall.new(updater)
	var args := OS.get_cmdline_user_args()
	Log.line("LAUNCHER OpenBFME Launcher %s (%s, %s) updates from %s" % [info.version, info.commit.left(10) if info.commit != "" else "development run", info.platform(), info.repo])
	var problem := ""
	if info.error != "":
		problem = info.error
		Log.line("LAUNCHER " + info.error)
	for a in args:
		if a.begins_with("--api-base=") or a.begins_with("--trust-key=") or a.begins_with("--repo=") or a.begins_with("--test-kill-at=") \
				or a.begins_with("--aio-base=") or a.begins_with("--aio-pins=") or a.begins_with("--aio-stop-after=") or a.begins_with("--ui-demo=") \
				or a.begins_with("--ui-stop=") or a.begins_with("--ui-then="):
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
	if cli:
		get_tree().quit(_run_cli(args, problem))
		return
	_build_ui()
	if start.message != "":
		_set_status(start.message)
	_refresh_installed()
	if _demo != "":
		_show_demo(_demo)
	elif problem != "":
		_set_status("Updates are off: " + problem)
		_set_update_state("failed")
	elif not "--offline" in args:
		_start_job(_job_check.bind(true), "check")
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
	Log.line("LAUNCHER ui games card: " + ("shown" if _games_card.visible else "hidden"))
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
		out.append(node.text)
	if node is Window:
		out.append(node.title)
	if node is Control and node.tooltip_text != "":
		out.append(node.tooltip_text)
	if node is ItemList:
		for i in node.item_count:
			out.append(node.get_item_text(i))
	if node is OptionButton:
		for i in node.item_count:
			out.append(node.get_item_text(i))
	for c in node.get_children():
		out.append_array(_texts(c))
	return out


## --ui-demo: made-up state for the screenshots of each screen (development runs and test packages only); nothing is downloaded or
## written. The folders are under the home folder, so the screenshots show how they are shortened.
func _show_demo(screen: String) -> void:
	var home := Paths.home()
	var games := {"found": screen != "games", "rotwk": home.path_join("Games/BFME/RotWK"), "bfme2": home.path_join("Games/BFME/BFME2"), "source": "config"}
	if screen == "games":
		games = {"found": false, "rotwk": "", "bfme2": "", "source": ""}
	_show_games(games)
	_games_card.visible = not games.found
	_settings.set_folders(games, true)
	var installed := "v0.3.0-preview.1"
	_installed_label.text = installed
	_play.disabled = false
	_older = ["v0.3.0-preview.2", "v0.3.0-preview.1", "v0.2.0-preview.4"]
	_settings.set_versions([{"version": "v0.3.0-preview.1"}, {"version": "v0.2.0-preview.4"}], installed, _older)
	_settings.set_options(true, true, _about())
	_notes_title.text = "WHAT'S NEW IN V0.3.0-PREVIEW.1"
	# clearly demo text (the owner: never invent release facts in screenshots)
	_notes.text = "DEMO TEXT for the launcher screenshots, not real release notes.\n\n- Sample note: what changed in this build.\n- Sample note: a fix.\n- Sample note: a known issue.\n\nThe real notes of the newest release appear here."
	match screen:
		"main", "settings", "games":
			_set_update_state("current")
			_set_status("Up to date.")
			if screen == "settings":
				_settings.visible = true
			if screen == "games":
				_open_download()
		"update", "downloading", "error":
			_latest = {"tag": "v0.3.0-preview.2"}
			_set_update_state("update", "v0.3.0-preview.2")
			_notes_title.text = "WHAT'S NEW IN V0.3.0-PREVIEW.2"
			_set_status("v0.3.0-preview.2 is available.")
			if screen == "downloading":
				_update.disabled = true
				_play.disabled = true
				var total := 1073741824
				var done := 327155712
				_meter.rate = 8.2 * 1048576.0
				_bar.max_value = total
				_bar.value = done
				_bar_title.text = "Downloading openbfme-v0.3.0-preview.2-%s.%s" % [info.platform(), "zip" if OS.get_name() == "Windows" else "tar.gz"]
				_bar_label.text = _meter.text(done, total)
				_show_progress(true)
				_set_status("Downloading the update. Play is back when it is installed.")
			if screen == "error":
				_show_error("Update not installed", "Update rejected: openbfme-v0.3.0-preview.2-%s.tar.gz does not match the signed manifest: deleted, nothing was installed. The download folder was %s." % [info.platform(), home.path_join(".local/share/OpenBFMELauncher/downloads")])


# ---- command line ------------------------------------------------------------------------------------------------------------------------

func _run_cli(args: PackedStringArray, problem: String) -> int:
	var code := 0
	updater.progress = func(text: String, done: int, total: int) -> void:
		if done < 0:
			Log.line("LAUNCHER " + text)
	for a in args:
		if a.begins_with("--channel="):
			var ch := a.substr(10)
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
# Lane UI-3: one main screen (the version Play starts and its update state with one button, the channel switch, the release notes, the
# one progress bar with Pause / Cancel, a big Play), Settings behind the gear (scripts/ui/settings_panel.gd), Download the games only
# while no game folders are found (scripts/ui/download_panel.gd), errors and questions in one dialog (scripts/ui/message_panel.gd). Every
# text the window shows goes through Paths.scrub (no home folder, no user name: scripts/ui/paths.gd).

## the window's size before DPI scaling (project.godot's viewport; canvas_items stretch scales everything with the window)
const BASE_SIZE := Vector2(1280, 720)


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
	var back := Backdrop.new()
	back.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(back)
	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	margin.add_theme_constant_override("margin_left", 44)
	margin.add_theme_constant_override("margin_right", 44)
	margin.add_theme_constant_override("margin_top", 22)
	margin.add_theme_constant_override("margin_bottom", 0)
	add_child(margin)
	var page := W.vbox(16)
	margin.add_child(page)

	# the header: the launcher's version, the channel switch, check, the gear
	var head := W.hbox(10)
	page.add_child(head)
	var sub := W.label("Launcher %s" % info.version, "Muted", 15)
	sub.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	head.add_child(sub)
	head.add_child(W.spacer())
	var seg := PanelContainer.new()
	seg.theme_type_variation = "Inset"
	seg.add_theme_stylebox_override("panel", Style.box(Color("121418"), Style.EDGE, 1, Style.RADIUS, Vector2(3, 3)))
	var seg_row := W.hbox(2)
	seg.add_child(seg_row)
	var group := ButtonGroup.new()
	_stable = _seg_button("Stable", group)
	_preview = _seg_button("Preview", group)
	seg_row.add_child(_stable)
	seg_row.add_child(_preview)
	_stable.pressed.connect(_on_channel.bind("stable"))
	_preview.pressed.connect(_on_channel.bind("preview"))
	head.add_child(seg)
	_check = W.icon_button("refresh", "Check for updates")
	_check.pressed.connect(func() -> void: _start_job(_job_check.bind(false), "check"))
	head.add_child(_check)
	_gear = W.icon_button("gear", "Settings")
	_gear.pressed.connect(_open_settings)
	head.add_child(_gear)

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
	page.add_child(_banner)

	# the body: the title, the version card and (no games found) the games card on the left; the release notes on the right
	var body := W.hbox(28)
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	page.add_child(body)
	var left := W.vbox(16)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.size_flags_stretch_ratio = 1.0
	body.add_child(left)
	var hero := W.vbox(0)
	var big := W.label("OpenBFME", "Heading", 58)
	hero.add_child(big)
	hero.add_child(W.label("The Rise of the Witch-king 2.01, rebuilt.", "Muted", 17))
	left.add_child(hero)

	var vcard := PanelContainer.new()
	left.add_child(vcard)
	var vbox := W.vbox(10)
	vcard.add_child(vbox)
	vbox.add_child(W.label("INSTALLED", "Section"))
	_installed_label = W.label("", "Heading", 30)
	vbox.add_child(_installed_label)
	var srow := W.hbox(12)
	_pill = PanelContainer.new()
	_pill.theme_type_variation = "Pill"
	var prow := W.hbox(6)
	_pill_icon = TextureRect.new()
	_pill_icon.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	prow.add_child(_pill_icon)
	_pill_text = W.label("")
	prow.add_child(_pill_text)
	_pill.add_child(prow)
	_pill.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	srow.add_child(_pill)
	_update = W.button("Update", "download", "GoldButton")
	_update.visible = false
	_update.pressed.connect(func() -> void: _start_job(_job_install.bind(_latest, false), "install"))
	srow.add_child(_update)
	vbox.add_child(srow)

	_games_card = PanelContainer.new()
	_games_card.visible = false
	left.add_child(_games_card)
	var gbox := W.vbox(10)
	_games_card.add_child(gbox)
	gbox.add_child(W.label("GAMES", "Section"))
	gbox.add_child(W.wrap("The Rise of the Witch-king 2.01 and Battle for Middle-earth II 1.06 were not found."))
	var grow := W.hbox(14)
	_games_button = W.button("Download the games", "download", "GoldButton")
	_games_button.pressed.connect(_open_download)
	grow.add_child(_games_button)
	var already := W.wrap("Installed already? The game finds them when it starts.", "Muted")
	already.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grow.add_child(already)
	gbox.add_child(grow)

	# the games found: where they are, shortened (Settings has "Show folder")
	_games_found = PanelContainer.new()
	_games_found.visible = false
	left.add_child(_games_found)
	var fbox := W.vbox(8)
	_games_found.add_child(fbox)
	fbox.add_child(W.label("GAMES", "Section"))
	var frow := W.hbox(8)
	var ficon := TextureRect.new()
	ficon.texture = VectorIcon.make("check", 16)
	ficon.modulate = Style.GOOD
	ficon.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	frow.add_child(ficon)
	frow.add_child(W.label("The Rise of the Witch-king and Battle for Middle-earth II found"))
	fbox.add_child(frow)
	_games_where = W.label("", "Muted")
	_games_where.clip_text = true
	_games_where.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	fbox.add_child(_games_where)

	var ncard := PanelContainer.new()
	ncard.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	ncard.size_flags_stretch_ratio = 1.15
	ncard.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_child(ncard)
	var nbox := W.vbox(10)
	ncard.add_child(nbox)
	_notes_title = W.label("RELEASE NOTES", "Section")
	nbox.add_child(_notes_title)
	_notes = RichTextLabel.new()
	_notes.bbcode_enabled = false
	_notes.scroll_active = true
	_notes.selection_enabled = true
	_notes.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_notes.text = "The notes of the newest release appear here after the update check."
	nbox.add_child(_notes)

	# the action bar: status, the one progress bar with Pause / Cancel, Play
	var bar := W.hbox(28)
	bar.custom_minimum_size.y = 112
	page.add_child(bar)
	var info_box := W.vbox(6)
	info_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	info_box.alignment = BoxContainer.ALIGNMENT_CENTER
	bar.add_child(info_box)
	_progress = W.vbox(6)
	_progress.visible = false
	info_box.add_child(_progress)
	var trow := W.hbox(10)
	_bar_title = W.label("")
	_bar_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_bar_title.clip_text = true
	_bar_title.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	trow.add_child(_bar_title)
	_bar_label = W.label("", "Muted")
	trow.add_child(_bar_label)
	_progress.add_child(trow)
	var brow2 := W.hbox(10)
	_bar = ProgressBar.new()
	_bar.show_percentage = false
	_bar.custom_minimum_size.y = 12
	_bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_bar.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	brow2.add_child(_bar)
	_pause = W.button("Pause", "pause", "", 14)
	_pause.pressed.connect(_on_pause)
	brow2.add_child(_pause)
	_cancel = W.button("Cancel", "stop", "", 14)
	_cancel.pressed.connect(_on_cancel)
	brow2.add_child(_cancel)
	_progress.add_child(brow2)
	_status = W.wrap("", "Muted")
	_status.max_lines_visible = 2
	info_box.add_child(_status)
	# Play: the icon beside the word, both centred (a Button's icon sits at its edge), dimmed with the button when disabled
	_play = W.button("", "", "GoldButton")
	_play.custom_minimum_size = Vector2(250, 66)
	_play.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_play.pressed.connect(_on_play)
	var pc := CenterContainer.new()
	pc.set_anchors_preset(Control.PRESET_FULL_RECT)
	pc.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var pr := W.hbox(12)
	pr.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var picon := TextureRect.new()
	picon.texture = VectorIcon.make("play", 24)
	picon.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	picon.modulate = Style.INK
	pr.add_child(picon)
	var pl := W.label("Play", "", 30)
	pl.add_theme_font_override("font", Style.title_font(700))
	pl.add_theme_color_override("font_color", Style.INK)
	pr.add_child(pl)
	pc.add_child(pr)
	_play.add_child(pc)
	_play.draw.connect(func() -> void: pc.modulate.a = 0.5 if _play.disabled else 1.0)
	bar.add_child(_play)

	# the overlays
	_settings = SettingsPanel.new()
	_settings.channel_picked.connect(_on_channel)
	_settings.free_camera_toggled.connect(_on_free_camera)
	_settings.aio_toggled.connect(_on_aio_toggled)
	_settings.forget_folders.connect(_on_forget_folders)
	_settings.use_version.connect(_on_use)
	_settings.install_release.connect(_on_install_pick)
	_settings.open_logs.connect(_open_logs)
	_settings.show_folder.connect(_show_folder)
	add_child(_settings)
	_download = DownloadPanel.new()
	_download.download.connect(_start_aio)
	add_child(_download)
	_message = MessagePanel.new()
	_message.open_logs.connect(_open_logs)
	add_child(_message)

	_show_channel()
	_set_update_state("idle")
	_refresh_games()
	_play.grab_focus.call_deferred()


func _seg_button(text: String, group: ButtonGroup) -> Button:
	var b := Button.new()
	b.text = text
	b.toggle_mode = true
	b.button_group = group
	b.theme_type_variation = "SegButton"
	return b


func _show_channel() -> void:
	var ch := updater.channel()
	_stable.set_pressed_no_signal(ch == "stable")
	_preview.set_pressed_no_signal(ch == "preview")
	_settings.set_channel(ch)


## the update state beside the installed version: "idle" (no check yet), "checking", "current" (up to date), "update" (`tag` is newer:
## the gold button), "failed" (the check did not finish: `why` says it), "none" (no release of the channel)
func _set_update_state(state: String, tag := "") -> void:
	var looks := {
		"idle": ["", "", Style.MUTED],
		"checking": ["refresh", "Checking for updates…", Style.MUTED],
		"current": ["check", "Up to date", Style.GOOD],
		"update": ["", "", Style.GOLD],
		"failed": ["warning", "Could not check for updates", Style.WARN],
		"none": ["", "No %s release yet" % updater.channel(), Style.MUTED],
	}
	var l: Array = looks[state]
	_pill.visible = l[1] != ""
	_pill_text.text = l[1]
	_pill_text.add_theme_color_override("font_color", l[2])
	_pill_icon.texture = VectorIcon.make(l[0], 16) if l[0] != "" else null
	_pill_icon.visible = l[0] != ""
	_pill_icon.modulate = l[2]
	var edge: Color = l[2]
	_pill.add_theme_stylebox_override("panel", Style.box(Color(edge, 0.1), Color(edge, 0.45), 1, 12, Vector2(12, 4)))
	_update.visible = state == "update"
	if state == "update":
		_update.text = ("Update to " if updater.newest_installed() != "" or _demo != "" else "Install ") + tag


func _on_free_camera(on: bool) -> void:
	var why := GameOptions.set_free_camera(on)
	if why != "":
		_show_error("The setting was not saved", "The free camera setting was not saved: " + why)
		_settings.set_options(GameOptions.free_camera(), aio.enabled(), _about())
		return
	Log.line("LAUNCHER free camera %s (%s)" % ["on" if on else "off", GameOptions.options_path()])
	_set_status("Free camera %s: it applies the next time the game starts." % ["on" if on else "off"])


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


func _open_settings() -> void:
	_refresh_games()
	_settings.set_options(GameOptions.free_camera(), aio.enabled(), _about())
	_settings.visible = true


func _open_download() -> void:
	if _download.folder == "":
		_download.setup(Paths.default_games_dir(), aio.total_size() / 1e9, aio.packages)
	_download.visible = true


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
		_start_job(_job_install.bind(_paused_release, _paused_pick), "install")
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


func _refresh_installed() -> void:
	var pv := updater.play_version()
	_installed_label.text = pv if pv != "" else "Nothing installed yet"
	_play.disabled = pv == "" or _busy()
	_settings.set_versions(updater.installed(), pv, _older)


func _refresh_games() -> void:
	_show_games(Paths.game_folders())


func _show_games(g: Dictionary) -> void:
	_games = g
	_games_card.visible = not g.found and aio.enabled()
	_games_found.visible = g.found
	if g.found:
		var r := Paths.short(g.rotwk)
		var b := Paths.short(g.bfme2)
		var common := r.get_base_dir()
		_games_where.text = ("In " + common) if common == b.get_base_dir() and common != "" else "%s and %s" % [r, b]
	_settings.set_folders(g)


func _busy() -> bool:
	return _thread != null and _thread.is_alive()


func _set_buttons() -> void:
	var busy := _busy()
	_check.disabled = busy
	_update.disabled = busy or _latest.is_empty()
	_stable.disabled = busy
	_preview.disabled = busy
	_play.disabled = busy or updater.play_version() == ""
	_games_button.disabled = busy or _paused != ""
	_settings.set_busy(busy)


func _start_job(job: Callable, kind: String) -> void:
	if _busy():
		return
	if _thread != null:
		_thread.wait_to_finish()
	_job_kind = kind
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


func _finish(text: String) -> void:
	if _thread != null:
		_thread.wait_to_finish()
		_thread = null
	_set_status(text)
	_job_kind = ""
	_show_progress(_paused != "")
	_refresh_installed()
	_set_buttons()


func _show_progress(on: bool) -> void:
	_progress.visible = on
	_pause.text = "Resume" if _paused != "" else "Pause"
	_pause.icon = VectorIcon.make("resume" if _paused != "" else "pause", 14)
	if not on:
		_bar_title.text = ""
		_bar_label.text = ""


func _on_progress(text: String, done: int, total: int) -> void:
	if _ui_stop != "" and total > 0 and done < total and _busy() and text.contains(_ui_stop.get_slice("@", 1)):
		var how := _ui_stop.get_slice("@", 0)
		_ui_stop = ""
		Log.line("LAUNCHER ui test: %s pressed during %s" % [how, text])
		_on_pause() if how == "pause" else _on_cancel()
	if total > 0:
		_meter.feed(done, Time.get_ticks_msec())
		_bar.max_value = total
		_bar.value = done
		_bar_title.text = Paths.scrub(text)
		_bar_label.text = _meter.text(done, total)
		_show_progress(true)
	else:
		_bar_title.text = Paths.scrub(text)
		_status.text = Paths.scrub(text)


# ---- jobs (worker thread) ----------------------------------------------------------------------------------------------------------------

func _job_check(auto_install: bool) -> void:
	var r := updater.fetch_releases()
	if not r.ok:
		_after_check.call_deferred({}, [], "Updates skipped: %s. Play starts the newest installed version." % r.error, "failed", "")
		return
	_releases = r.releases
	var chan := updater.in_channel(r.releases, updater.channel())
	if chan.is_empty():
		_after_check.call_deferred({}, chan, "There is no %s release yet." % updater.channel(), "none", "")
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
	if _stop == "":
		_job_assets = _assets_of(chan[0], ["launcher"])
		s = updater.stage_self_update(chan[0])
	if not s.ok and _stop != "":
		msg = _stopped("launcher", chan[0], false)  # Sol r1: a stopped launcher download was reported as rejected
	elif not s.ok:
		msg += " Launcher update rejected: " + s.error
		err = (err + " " if err != "" else "") + "Launcher update rejected: " + s.error
	elif s.staged != "":
		msg += " The launcher %s is ready and replaces this one on the next start." % s.staged
	_after_check.call_deferred(chan[0] if newer else {}, chan, msg, "update" if newer else "current", err)


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


func _after_check(latest: Dictionary, chan: Array, msg: String, state: String, err: String) -> void:
	_latest = latest
	_older = []
	for r in chan:
		_older.append(r.tag)
	if not chan.is_empty():
		_notes_title.text = "WHAT'S NEW IN " + String(chan[0].tag).to_upper()
		_notes.text = Paths.scrub(chan[0].notes) if chan[0].notes != "" else "(no release notes)"
	_set_update_state(state, chan[0].tag if not chan.is_empty() else "")
	_finish(msg)
	if err != "":
		_message.show_error("Update not installed", err, _about())


func _job_install(release: Dictionary, user_pick: bool) -> void:
	_job_assets = _assets_of(release, ["game"])
	var i := updater.install(release, user_pick)
	if i.ok:
		_after_install.call_deferred("Installed %s." % i.version, "")
	elif _stop != "":
		_after_install.call_deferred(_stopped("game", release, user_pick), "")
	else:
		_after_install.call_deferred("Not installed: " + i.error, i.error)


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
	if err == "" and _paused == "" and msg.begins_with("Installed"):
		_latest = {}
		_set_update_state("current")
	_finish(msg)
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
	_start_job(_job_check.bind(false), "check")


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
		_settings.visible = false
		_message.ask("Install an older version?", "%s is older than the installed %s. Install it and play it from now on?" % [tag, newest],
			"Install", func() -> void: _start_job(_job_install.bind(rel[0], true), "install"))
	else:
		_settings.visible = false
		_start_job(_job_install.bind(rel[0], true), "install")


func _on_play() -> void:
	var v := updater.play_version()
	if v == "":
		return
	var p := updater.play(v)
	if p.ok:
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
			func() -> void: _start_job(_job_install.bind(rel[0], true), "install"))
		return
	_show_error("The game did not start", p.error)


func _exit_tree() -> void:
	if _thread != null:
		if _job_kind == "aio":
			aio.request_stop("pause")
		updater.http.cancelled = true
		_thread.wait_to_finish()
