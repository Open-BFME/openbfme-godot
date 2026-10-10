## Lane LAUNCH-1: the OpenBFME Launcher's window and command line.
##
## One window: the version Play starts, the newest available version of the channel with its release notes (plain text), a progress bar
## with MB and rate, Play / Update / Check, the channel (Stable / Preview), the installed versions (pick one to play it: a rollback) and
## the channel's releases (install an older one: the user's explicit pick). At start it checks for updates, installs a newer game build
## of the channel (verified, see scripts/core/updater.gd) and stages a newer launcher. Offline or on an API error it says why the
## update was skipped and Play starts the newest verified installed version.
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
##   --aio=on|off             (lane AIO-1, on by default: AioInstall.DEFAULT_ENABLED) the setting "Install game files with the All In One
##                            BFME Launcher"
##   --aio-install=<folder> --aio-consent   with that setting on: download RotWK 2.01 and BFME2 1.06 from the All In One BFME Launcher's
##                            service into <folder>/RotWK and <folder>/BFME2 (scripts/core/aio_install.gd, docs/AIO.md); --aio-consent is
##                            the player's agreement to the text it logs (the window asks in a dialog)
## --screenshot=FILE (with the window): save the window as a PNG once the start-up check has finished, then quit (the layout check).
## Test options (honoured only by a development run or a test package, BuildInfo.tests_allowed):
##   --api-base=http://127.0.0.1:<port>   the API origin (a local test server); --trust-key=<64 hex>   the release key of a development run;
##   --repo=<owner>/<name>   the repository; --aio-base=http://127.0.0.1:<port>   the AIO service's stand-in (both of its hosts);
##   --aio-pins=<file>   made-up pinned tables (JSON) for that stand-in; --aio-stop-after=<folder>/<file>:<pause|cancel>   a stop right
##   after that file was kept (the pause / cancel paths)
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

const CLI_ACTIONS := ["--update", "--install=", "--select=", "--self-update", "--channel=", "--list", "--play", "--version", "--free-camera=",
	"--aio=", "--aio-install="]

var info: RefCounted
var updater: Updater
var aio: AioInstall
var _thread: Thread
var _releases: Array = []
var _latest: Dictionary = {}
var _rate_t := 0
var _rate_b := 0
var _rate := 0.0

var _installed_label: Label
var _available_label: Label
var _notes: TextEdit
var _bar: ProgressBar
var _bar_label: Label
var _status: Label
var _warning: Label
var _play: Button
var _update: Button
var _check: Button
var _channel: OptionButton
var _versions: ItemList
var _use: Button
var _older: OptionButton
var _install_older: Button
var _confirm: ConfirmationDialog
var _free_camera: CheckBox
var _aio_box: CheckBox
var _aio_button: Button
var _aio_dialog: FileDialog
var _aio_consent: ConfirmationDialog
var _aio_consent_text: RichTextLabel
var _aio_own: CheckBox
var _aio_pause: Button
var _aio_cancel: Button
var _aio_target := ""      # the folder of the AIO download that runs or is paused
var _aio_running := false


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
				or a.begins_with("--aio-base=") or a.begins_with("--aio-pins=") or a.begins_with("--aio-stop-after="):
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
	if problem != "":
		_set_status("Updates are off: " + problem)
	elif not "--offline" in args:
		_start_job(_job_check.bind(true))
	for a in args:
		if a.begins_with("--screenshot="):
			_screenshot(a.substr(13))


func _screenshot(path: String) -> void:
	for i in 600:
		await get_tree().process_frame
		if not _busy() and i > 10:
			break
	for i in 10:
		await get_tree().process_frame
	# what the window shows, for the tests (headless has no image to save)
	Log.line("LAUNCHER ui status: " + _status.text)
	Log.line("LAUNCHER ui warning: " + (_warning.text if _warning.visible else ""))
	var img := get_viewport().get_texture().get_image()
	if img != null and DisplayServer.get_name() != "headless":
		img.save_png(path)
		Log.line("LAUNCHER screenshot " + path)
	get_tree().quit(0)


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

func _build_ui() -> void:
	# the look of the game's first-run screen (godot/scripts/release/first_run.gd): plain dark panel, white text
	var back := ColorRect.new()
	back.color = Color(0.06, 0.06, 0.07)
	back.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(back)
	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 28)
	add_child(margin)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 10)
	margin.add_child(box)
	var head := HBoxContainer.new()
	box.add_child(head)
	var title := Label.new()
	title.text = "OpenBFME Launcher"
	title.add_theme_font_size_override("font_size", 26)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(title)
	var ver := Label.new()
	ver.text = info.version
	ver.add_theme_color_override("font_color", Color(1, 1, 1, 0.5))
	head.add_child(ver)
	_warning = Label.new()
	_warning.add_theme_color_override("font_color", Color(1, 0.6, 0.4))
	_warning.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_warning.visible = false
	box.add_child(_warning)
	_installed_label = Label.new()
	_installed_label.add_theme_font_size_override("font_size", 18)
	box.add_child(_installed_label)
	var avail := HBoxContainer.new()
	box.add_child(avail)
	_available_label = Label.new()
	_available_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	avail.add_child(_available_label)
	var ch_label := Label.new()
	ch_label.text = "Channel"
	avail.add_child(ch_label)
	_channel = OptionButton.new()
	_channel.add_item("Stable")
	_channel.add_item("Preview")
	_channel.selected = 1 if updater.channel() == "preview" else 0
	_channel.item_selected.connect(_on_channel)
	avail.add_child(_channel)
	_notes = TextEdit.new()
	_notes.editable = false
	_notes.add_theme_color_override("font_readonly_color", Color(0.9, 0.9, 0.9))
	_notes.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	_notes.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_notes.placeholder_text = "Release notes"
	box.add_child(_notes)
	var lists := HBoxContainer.new()
	lists.custom_minimum_size = Vector2(0, 130)
	box.add_child(lists)
	var left := VBoxContainer.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	lists.add_child(left)
	var l1 := Label.new()
	l1.text = "Installed versions"
	left.add_child(l1)
	_versions = ItemList.new()
	_versions.size_flags_vertical = Control.SIZE_EXPAND_FILL
	left.add_child(_versions)
	_use = Button.new()
	_use.text = "Play this version from now on"
	_use.pressed.connect(_on_use)
	left.add_child(_use)
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	lists.add_child(right)
	var l2 := Label.new()
	l2.text = "Releases of the channel"
	right.add_child(l2)
	_older = OptionButton.new()
	right.add_child(_older)
	_install_older = Button.new()
	_install_older.text = "Install this release"
	_install_older.pressed.connect(_on_install_pick)
	right.add_child(_install_older)
	# lane INPUT-1: the game settings the launcher offers (written to the game's Options.ini, read at the game's start)
	_free_camera = CheckBox.new()
	_free_camera.text = "Free camera (zoom out further, not retail)"
	_free_camera.tooltip_text = "Lets the camera zoom out to the whole map. Retail RotWK stops much closer. Applies the next time the game starts; in a game, Ctrl+Z toggles it."
	_free_camera.button_pressed = GameOptions.free_camera()
	_free_camera.toggled.connect(_on_free_camera)
	box.add_child(_free_camera)
	# lane AIO-1 (on by default, nothing downloads without the consent screen): download the game files from the All In One BFME Launcher's service
	var aio_row := HBoxContainer.new()
	box.add_child(aio_row)
	_aio_box = CheckBox.new()
	_aio_box.text = "Install game files with the All In One BFME Launcher"
	_aio_box.tooltip_text = "Downloads The Rise of the Witch-king 2.01 and The Battle for Middle-earth II 1.06 from the All In One BFME Launcher's servers (bfmeladder.com). You are asked first."
	_aio_box.button_pressed = aio.enabled()
	_aio_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_aio_box.toggled.connect(_on_aio_toggled)
	aio_row.add_child(_aio_box)
	_aio_button = Button.new()
	_aio_button.text = "Download the game files..."
	_aio_button.visible = aio.enabled()
	_aio_button.pressed.connect(_on_aio_pick)
	aio_row.add_child(_aio_button)
	_aio_pause = Button.new()
	_aio_pause.text = "Pause"
	_aio_pause.visible = false
	_aio_pause.pressed.connect(_on_aio_pause)
	aio_row.add_child(_aio_pause)
	_aio_cancel = Button.new()
	_aio_cancel.text = "Cancel"
	_aio_cancel.visible = false
	_aio_cancel.pressed.connect(_on_aio_cancel)
	aio_row.add_child(_aio_cancel)
	# the consent screen: where the files come from (a link to the project's page), what the player agrees to, the ownership box
	_aio_consent = ConfirmationDialog.new()
	_aio_consent.title = "Download the game files"
	_aio_consent.ok_button_text = "Download"
	var cbox := VBoxContainer.new()
	cbox.custom_minimum_size = Vector2(720, 0)
	_aio_consent.add_child(cbox)
	_aio_consent_text = RichTextLabel.new()
	_aio_consent_text.fit_content = true
	_aio_consent_text.bbcode_enabled = false
	cbox.add_child(_aio_consent_text)
	var link := LinkButton.new()
	link.text = "The All In One BFME Launcher: " + AioInstall.PAGE
	link.uri = AioInstall.PAGE
	cbox.add_child(link)
	_aio_own = CheckBox.new()
	_aio_own.text = "I own The Battle for Middle-earth II and The Rise of the Witch-king and want to download them from this service"
	_aio_own.toggled.connect(func(on: bool) -> void: _aio_consent.get_ok_button().disabled = not on)
	cbox.add_child(_aio_own)
	_aio_consent.confirmed.connect(func() -> void: _start_aio(_aio_target))
	add_child(_aio_consent)
	_aio_dialog = FileDialog.new()
	_aio_dialog.file_mode = FileDialog.FILE_MODE_OPEN_DIR
	_aio_dialog.access = FileDialog.ACCESS_FILESYSTEM
	_aio_dialog.title = "The folder for RotWK and BFME2"
	_aio_dialog.dir_selected.connect(_on_aio_folder)
	add_child(_aio_dialog)
	_bar = ProgressBar.new()
	_bar.show_percentage = false
	box.add_child(_bar)
	_bar_label = Label.new()
	box.add_child(_bar_label)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 12)
	box.add_child(buttons)
	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	buttons.add_child(_status)
	_check = Button.new()
	_check.text = "Check"
	_check.pressed.connect(func(): _start_job(_job_check.bind(false)))
	buttons.add_child(_check)
	_update = Button.new()
	_update.text = "Update"
	_update.disabled = true
	_update.pressed.connect(func(): _start_job(_job_install.bind(_latest, false)))
	buttons.add_child(_update)
	_play = Button.new()
	_play.text = "Play"
	_play.custom_minimum_size = Vector2(140, 44)
	_play.pressed.connect(_on_play)
	buttons.add_child(_play)
	_confirm = ConfirmationDialog.new()
	add_child(_confirm)


func _on_free_camera(on: bool) -> void:
	var why := GameOptions.set_free_camera(on)
	if why != "":
		_set_status("The free camera setting was not saved: " + why)
		return
	Log.line("LAUNCHER free camera %s (%s)" % ["on" if on else "off", GameOptions.options_path().replace(OS.get_environment("HOME"), "~") if OS.get_environment("HOME") != "" else GameOptions.options_path()])
	_set_status("Free camera %s: it applies the next time the game starts." % ["on" if on else "off"])


func _on_aio_toggled(on: bool) -> void:
	aio.set_enabled(on)
	_aio_button.visible = on
	_set_status("Installing the game files with the All In One BFME Launcher is %s." % ["on" if on else "off"])


func _on_aio_pick() -> void:
	_aio_dialog.popup_centered_ratio(0.7)


func _on_aio_folder(dir: String) -> void:
	_aio_target = dir
	var into := PackedStringArray()
	for p in aio.packages:
		into.append(dir.path_join(p.folder))
	_aio_consent_text.text = "%s\n\nThey go into %s." % [aio.consent_text(), " and ".join(into)]
	_aio_own.button_pressed = false
	_aio_consent.get_ok_button().disabled = true
	_aio_consent.popup_centered()


func _start_aio(dir: String) -> void:
	_aio_running = true
	_aio_pause.text = "Pause"
	_aio_pause.visible = true
	_aio_cancel.visible = true
	_start_job(_job_aio.bind(dir))


## Pause stops the transfer and keeps the partial file (Resume continues it with a Range request); while paused it reads "Resume".
func _on_aio_pause() -> void:
	if _aio_running:
		aio.request_stop("pause")
	elif _aio_target != "":
		_start_aio(_aio_target)


func _on_aio_cancel() -> void:
	if _aio_running:
		aio.request_stop("cancel")
		return
	var n := aio.remove_partials(_aio_target)
	_aio_pause.visible = false
	_aio_cancel.visible = false
	_set_status("The download was cancelled (%d partial files removed; the checked files are kept)." % n)


static func _folders_text(folders: Dictionary) -> String:
	var parts := PackedStringArray()
	for k in folders:
		parts.append("%s in %s" % [String(k).to_upper(), folders[k]])
	return ", ".join(parts)


func _set_warning(text: String) -> void:
	_warning.text = "Warning: " + text if text != "" else ""
	_warning.visible = text != ""


func _set_status(text: String) -> void:
	Log.line("LAUNCHER " + text)
	if _status != null:
		_status.text = text


func _refresh_installed() -> void:
	var inst := updater.installed()
	var pv := updater.play_version()
	_installed_label.text = "Installed: %s" % (pv if pv != "" else "nothing yet")
	_versions.clear()
	for i in inst:
		var label: String = i.version
		if i.version == pv:
			label += "   (Play starts this)"
		_versions.add_item(label)
		_versions.set_item_metadata(_versions.item_count - 1, i.version)
	_play.disabled = pv == "" or _busy()


func _busy() -> bool:
	return _thread != null and _thread.is_alive()


func _set_buttons() -> void:
	var busy := _busy()
	_check.disabled = busy
	_update.disabled = busy or _latest.is_empty()
	_install_older.disabled = busy or _older.item_count == 0
	_use.disabled = busy
	_channel.disabled = busy
	_play.disabled = busy or updater.play_version() == ""
	_aio_box.disabled = busy or _aio_pause.visible
	_aio_button.disabled = busy or _aio_pause.visible


func _start_job(job: Callable) -> void:
	if _busy():
		return
	if _thread != null:
		_thread.wait_to_finish()
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
	_bar_label.text = ""
	_refresh_installed()
	_set_buttons()


func _on_progress(text: String, done: int, total: int) -> void:
	if total > 0:
		var now := Time.get_ticks_msec()
		if done < _rate_b or _rate_t == 0:
			_rate_t = now
			_rate_b = done
		elif now - _rate_t >= 500:
			_rate = (done - _rate_b) * 1000.0 / (now - _rate_t)
			_rate_t = now
			_rate_b = done
		_bar.max_value = total
		_bar.value = done
		_bar_label.text = "%s: %.1f / %.1f MB, %.1f MB/s" % [text, done / 1048576.0, total / 1048576.0, _rate / 1048576.0]
	else:
		_bar_label.text = text


# ---- jobs (worker thread) ----------------------------------------------------------------------------------------------------------------

func _job_check(auto_install: bool) -> void:
	var r := updater.fetch_releases()
	if not r.ok:
		_after_check.call_deferred({}, [], "Updates skipped: %s. Play starts the newest installed version." % r.error)
		return
	_releases = r.releases
	var chan := updater.in_channel(r.releases, updater.channel())
	if chan.is_empty():
		_after_check.call_deferred({}, chan, "There is no %s release yet." % updater.channel())
		return
	var mm := updater.release_manifest(chan[0])
	if not mm.ok:
		_after_check.call_deferred({}, chan, "Update rejected: " + mm.error)
		return
	var newest := updater.newest_installed()
	var newer := newest == "" or Semver.compare(chan[0].tag, newest) > 0
	var msg := "Up to date." if not newer else "%s is available." % chan[0].tag
	# the freeze warning has its own line in the window (round 3: folded into the status, it was overwritten by the install's messages)
	var freeze := updater.freeze_warning(mm.manifest)
	if freeze != "":
		Log.line("LAUNCHER warning: " + freeze)
	_set_warning.call_deferred(freeze)
	if newer and auto_install:
		var i := updater.install(chan[0], false)
		msg = "Installed %s." % i.version if i.ok else "Update rejected: " + i.error
		newer = not i.ok
	var s := updater.stage_self_update(chan[0])
	if not s.ok:
		msg += " Launcher update rejected: " + s.error
	elif s.staged != "":
		msg += " The launcher %s is ready and replaces this one on the next start." % s.staged
	_after_check.call_deferred(chan[0] if newer else {}, chan, msg)


func _after_check(latest: Dictionary, chan: Array, msg: String) -> void:
	_latest = latest
	if not chan.is_empty():
		_available_label.text = "Newest %s release: %s" % [updater.channel(), chan[0].tag]
		_notes.text = chan[0].notes if chan[0].notes != "" else "(no release notes)"
	else:
		_available_label.text = "Newest %s release: unknown" % updater.channel()
	_older.clear()
	for r in chan:
		_older.add_item(r.tag)
	_finish(msg)


func _job_install(release: Dictionary, user_pick: bool) -> void:
	var i := updater.install(release, user_pick)
	_finish.call_deferred("Installed %s." % i.version if i.ok else "Not installed: " + i.error)


func _job_aio(dir: String) -> void:
	var r := aio.install(dir, true)
	_after_aio.call_deferred(r)


func _after_aio(r: Dictionary) -> void:
	_aio_running = false
	if r.ok:
		_aio_pause.visible = false
		_aio_cancel.visible = false
		_finish("The game files are installed and checked: %s. OpenBFME offers these folders when it starts." % _folders_text(r.folders))
	elif r.kind == AioInstall.STOPPED and aio.stop_request == "pause":
		_aio_pause.text = "Resume"
		_finish("The download is " + r.error)
	else:
		if r.kind == AioInstall.STOPPED:
			aio.remove_partials(_aio_target)
		_aio_pause.visible = r.kind == AioInstall.NETWORK  # a broken transfer can be resumed
		_aio_pause.text = "Resume"
		_aio_cancel.visible = _aio_pause.visible
		_finish(("The download was " if r.kind == AioInstall.STOPPED else "The game files were not installed: ") + r.error)


func _on_channel(idx: int) -> void:
	updater.set_channel("preview" if idx == 1 else "stable")
	_start_job(_job_check.bind(false))


func _on_use() -> void:
	var sel := _versions.get_selected_items()
	if sel.is_empty():
		return
	var v: String = _versions.get_item_metadata(sel[0])
	updater.set_selected(v)
	_set_status("Play starts %s from now on." % v)
	_refresh_installed()


func _on_install_pick() -> void:
	if _older.item_count == 0:
		return
	var tag := _older.get_item_text(_older.selected)
	var rel = _releases.filter(func(x): return x.tag == tag)
	if rel.is_empty():
		return
	var newest := updater.newest_installed()
	if newest != "" and Semver.compare(tag, newest) < 0:
		_confirm.dialog_text = "%s is older than the installed %s. Install it anyway and play it from now on?" % [tag, newest]
		for c in _confirm.confirmed.get_connections():
			_confirm.confirmed.disconnect(c.callable)
		_confirm.confirmed.connect(func(): _start_job(_job_install.bind(rel[0], true)), CONNECT_ONE_SHOT)
		_confirm.popup_centered()
	else:
		_start_job(_job_install.bind(rel[0], true))


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
			_set_status(p.error + " It cannot be downloaded now: the release list is not available.")
			return
		_confirm.dialog_text = "%s was changed after it was installed and is not started. Download it again?" % v
		for c in _confirm.confirmed.get_connections():
			_confirm.confirmed.disconnect(c.callable)
		_confirm.confirmed.connect(func(): _start_job(_job_install.bind(rel[0], true)), CONNECT_ONE_SHOT)
		_confirm.popup_centered()


func _exit_tree() -> void:
	if _thread != null:
		if _aio_running:
			aio.request_stop("pause")
		updater.http.cancelled = true
		_thread.wait_to_finish()
