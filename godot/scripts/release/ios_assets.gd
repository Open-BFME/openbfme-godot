## iOS game data: the launcher's All In One BFME Launcher download (scripts/core/aio_install.gd, the compact set) into the app's own
## storage, kept on the iPad and out of its backups; or a native directory picker for a drive (the data stays on the drive).
extends CanvasLayer
signal finished(result: Dictionary)

const AioInstall := preload("res://scripts/core/aio_install.gd")
## the download's folder in user:// (the app's Documents on iOS): <it>/RotWK, <it>/BFME2; complete once user://aio-install.json exists
const DOWNLOAD_DIR := "games"

## What aio_install.gd uses of the launcher's Updater (launcher/scripts/core/updater.gd): its HTTP client and network policy, the
## progress callback, its state and data folder.
class DownloadHost extends RefCounted:
	const NetPolicy := preload("res://scripts/net/net_policy.gd")
	const HttpFetch := preload("res://scripts/net/http_fetch.gd")
	var data_dir := OS.get_user_data_dir()
	var policy := NetPolicy.new()
	var http: HttpFetch
	var progress := Callable()
	var _state := {}

	func _init(version: String) -> void:
		http = HttpFetch.new(policy, version)
		http.user_agent = "OpenBFME-iOS/%s (+%s)" % [version, HttpFetch.PROJECT_URL]

	func get_state(section: String, key: String, default: Variant = null) -> Variant:
		return _state.get(section + "/" + key, default)

	func set_state(section: String, key: String, value: Variant) -> void:
		_state[section + "/" + key] = value

	func _report(text: String, done := -1, total := -1) -> void:
		if progress.is_valid():
			progress.call(text, done, total)

var _setup: RefCounted
var _fs: RefCounted
var _status: Label
var _download: Button
var _pause: Button
var _choose: Button
var _bar: ProgressBar
var _aio: RefCounted
var _thread: Thread
var _last_path := ""


## The folders of a completed download {rotwk, bfme2}, resolved now (iOS moves the app's sandbox on an update), or {} before one completed.
static func downloaded_folders() -> Dictionary:
	if not FileAccess.file_exists(OS.get_user_data_dir().path_join(AioInstall.RECORD)):
		return {}
	var folders := {}
	for p in AioInstall.Pins.PACKAGES:
		folders[p.key] = OS.get_user_data_dir().path_join(DOWNLOAD_DIR).path_join(p.folder)
	return folders


func run(fs: RefCounted, setup: RefCounted, problem := "") -> Dictionary:
	_fs = fs
	_setup = setup
	var host := DownloadHost.new(ClassDB.instantiate("ReleaseInfo").version())
	host.progress = func(text: String, done: int, total: int) -> void: call_deferred("_show_progress", text, done, total)
	_aio = AioInstall.new(host)
	_aio.use_compact()
	layer = 90
	var background := ColorRect.new()
	background.color = Color("10191e")
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 40)
	add_child(margin)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 16)
	margin.add_child(box)
	var title := Label.new()
	title.text = "OpenBFME — game data"
	title.add_theme_font_size_override("font_size", 28)
	box.add_child(title)
	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.text = problem if problem != "" else "OpenBFME needs The Rise of the Witch-king 2.01 and The Battle for Middle-earth II 1.06. Download them to this iPad, or choose a drive that holds them."
	box.add_child(_status)
	var consent := Label.new()
	consent.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	consent.add_theme_font_size_override("font_size", 14)
	consent.text = _aio.consent_text() + "\nCampaign cinematics are not downloaded. Keep OpenBFME open until the download finishes; a paused or interrupted download continues where it stopped."
	box.add_child(consent)
	_download = Button.new()
	_download.text = "Download the games (%.1f GB)" % (_aio.total_size() / 1e9)
	_download.custom_minimum_size.y = 64
	_download.pressed.connect(_start_download)
	box.add_child(_download)
	_bar = ProgressBar.new()
	_bar.visible = false
	box.add_child(_bar)
	_pause = Button.new()
	_pause.text = "Pause"
	_pause.visible = false
	_pause.custom_minimum_size.y = 64
	_pause.pressed.connect(func() -> void:
		_pause.disabled = true
		_aio.request_stop("pause"))
	box.add_child(_pause)
	_choose = Button.new()
	_choose.text = "Choose folder on drive"
	_choose.custom_minimum_size.y = 64
	_choose.pressed.connect(func() -> void:
		_last_path = ""
		_setup.pick_external_assets())
	box.add_child(_choose)
	call_deferred("_start_access")
	return await finished

func _start_access() -> void:
	# Let iPadOS finish activating the scene before resolving a drive's saved bookmark.
	await get_tree().create_timer(0.5).timeout
	if _thread == null and _setup.external_assets().state == "selected":
		_check()

func _process(_delta: float) -> void:
	if _thread != null:
		if not _thread.is_alive():
			_finish_download(_thread.wait_to_finish())
		return
	if _setup != null:
		_check()

func _exit_tree() -> void:
	if _thread != null:
		_aio.request_stop("pause")
		_thread.wait_to_finish()
		_thread = null
		DisplayServer.screen_set_keep_on(false)

func _start_download() -> void:
	var target := OS.get_user_data_dir().path_join(DOWNLOAD_DIR)
	var err := DirAccess.make_dir_recursive_absolute(target)
	if err != OK and not DirAccess.dir_exists_absolute(target):
		_status.text = "Cannot create %s (%s)." % [target, error_string(err)]
		return
	var backup: Dictionary = _setup.exclude_from_backup(target)
	if not backup.ok:
		_status.text = "Cannot keep the game files out of this iPad's backups: %s" % backup.error
		return
	# AioInstall.write_marker tells the game through the game's user folder; on iOS that is user:// itself.
	OS.set_environment("OPENBFME_GAME_USER_DIR", OS.get_user_data_dir())
	_download.disabled = true
	_choose.disabled = true
	_pause.disabled = false
	_pause.visible = true
	_bar.visible = true
	_status.text = "Starting the download…"
	# The screen locking would suspend the app and break the download.
	DisplayServer.screen_set_keep_on(true)
	_thread = Thread.new()
	_thread.start(_aio.install.bind(target, true))

func _show_progress(text: String, done: int, total: int) -> void:
	if _thread == null:
		return
	_status.text = text
	if total > 0 and done >= 0:
		_bar.value = done * 100.0 / total

func _finish_download(result: Dictionary) -> void:
	_thread = null
	DisplayServer.screen_set_keep_on(false)
	_pause.visible = false
	_download.disabled = false
	_choose.disabled = false
	if not result.ok:
		_status.text = "The download stopped: %s" % result.error
		_download.text = "Continue the download"
		return
	_status.text = "Checking the downloaded game archives…"
	if not _mount(result.folders.rotwk, result.folders.bfme2, "iOS download"):
		_download.text = "Download the games again"

func _check() -> void:
	var access: Dictionary = _setup.external_assets()
	if access.state == "error":
		_status.text = str(access.error)
		return
	if access.state != "selected" or access.path == _last_path:
		return
	_last_path = access.path
	var base: String = access.path
	if DirAccess.dir_exists_absolute(base.path_join("retail")):
		base = base.path_join("retail")
	_status.text = "Checking game archives on the drive…"
	_mount(base.path_join("rotwk"), base.path_join("bfme2"), "iOS external drive", "Choose the folder containing rotwk and bfme2.")

## mounts and remembers the folders, then finishes; false (with `hint` and the reasons shown) when they do not work
func _mount(rotwk: String, bfme2: String, source: String, hint := "The downloaded game files do not work.") -> bool:
	var mount: Dictionary = _fs.mount_retail_paths(rotwk, bfme2)
	if not mount.ok:
		_status.text = hint + "\n" + "\n".join(mount.errors)
		return false
	var saved: Dictionary = _setup.remember(rotwk, bfme2)
	if not saved.ok:
		_status.text = str(saved.error)
		return false
	set_process(false)
	print("RELEASE game folders: ", source)
	finished.emit(mount)
	return true
