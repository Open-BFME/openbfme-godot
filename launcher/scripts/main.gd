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
##   --check-game-files       exit 0 when both configured game folders exist, 1 when either is missing
##   --channel=stable|nightly remember the channel (lane NIGHTLY-1: nightly is the id "preview", as is the old --channel=preview;
##                            --channel=alpha is the same as stable, the old shown name of the stable channel since VERSION-1)
##   --list                   print the installed versions
##   --play                   start the version Play starts
##   --version                print the launcher's version
##   --open-url openbfme://join/<id>   (lane ONLINE-2; what the operating system runs once the handler is registered) start the version Play starts in
##                            that online game: --online-join=<id> --online-lobby=<the lobby>; needs no launcher lock (a link works while the window is
##                            open). It must come alone (r2, Sol r1): any other argument, or a link anywhere else, refuses the start and nothing runs
##   --lobby=<url>            remember the online lobby the links use (until one is set, a link says so and starts nothing)
##   --register-url-handler   register this launcher for openbfme:// links (scripts/core/url_handler.gd: the current user's registry on Windows,
##                            ~/.local/share/applications + xdg-mime on Linux)
##   --install-desktop-entry  (lane OWNERFIX-3) Linux / Steam Deck: an "OpenBFME" entry with the OpenBFME icon in the desktop menus
##                            (scripts/core/desktop_entry.gd: <data home>/applications/openbfme.desktop and the icon under icons/hicolor)
##   --auto-update=on|off     (lane UI-5) the setting "Update automatically in the background" (on by default)
##   --online-count=on|off    (lane ONLINECOUNT-1) the setting "Show how many players are online" (on by default): the Play card's "N players online" from
##                            the lobby's GET /v1/stats (scripts/core/online_count.gd); off, the launcher never asks the lobby, and the game's menus follow it
##   --free-camera=on|off     (lane INPUT-1) the game's free camera (Options.ini OpenBFMEFreeCamera, see scripts/core/game_options.gd)
##   --game=bfme1|bfme2      remember which game Play starts (BFME1 uses its own asset folder)
##   --aio=on|off             (lane AIO-1, on by default: AioInstall.DEFAULT_ENABLED) the setting "Offer to download the games"
##                            (Settings; the All In One BFME Launcher's service)
##   --aio-install=<folder> --aio-consent   with that setting on: download RotWK 2.01 and BFME2 1.06 from the All In One BFME Launcher's
##                            service into <folder>/RotWK and <folder>/BFME2 (scripts/core/aio_install.gd, docs/AIO.md); --aio-consent is
##                            the player's agreement to the text it logs (in the window: the Download press)
##   --aio-package=bfme1      (community PR #36) with --aio-install: the OPTIONAL Battle for Middle-earth I 1.03 package instead, from BFME Arena
##                            (bfmeladder.com, a third party; OpenBFME does not host it) into <folder>/BFME1; refused unless --game=bfme1 is
##                            the chosen game (in the window: the Game files screen offers it only then)
##   --mods-list[=refresh]    (lane MODS-3) list the Rise of the Witch-king mods of the All In One BFME Launcher's service (bfmeladder.com):
##                            each package, its enhancements, "ready" or "needs engine features" with the files that need them (the list
##                            is kept for half an hour; =refresh asks again); scripts/core/mod_install.gd, docs/MODS.md
##   --mod-get=<guid> --mod-consent [--mod-with=<guid>,...]   download that package (and those enhancements) into the launcher's
##                            mods/<guid> folder; --mod-consent is the player's agreement to the text it logs (in the window: the question
##                            before the first mod download)
##   --mod-use=<guid>|none    the mod Play starts (with the enhancements it was installed with); none = the original game
##   --mod-remove=<guid>      remove that mod's folder (and nothing else)
##   --mod-args               print the game's arguments Play would pass (the active mod's --mod-install= / --mod=)
##   --asset-packs            (lane SDK-1) list the asset packs (HD models and textures) in the game's asset-packs folder: on or off, what
##                            each holds and the problems the game would stop at (scripts/core/asset_packs.gd, docs/MODS.md)
##   --asset-pack-on=<name> / --asset-pack-off=<name>   turn that pack on or off (asset-packs/disabled.txt, which the game reads)
##   --game-start=<folder>    (lane ONEZIP-1; what the game runs when a player starts it directly from the combined package, see
##                            scripts/core/game_start.gd) decide which version that start plays, then look for an update in the background
## --screenshot=FILE (with the window): save the window as a PNG once the start-up check has finished, then quit (the layout check).
## --screenshot-window=WxH: the same for a window of that size under the project's own stretch (expand aspect: a wider window lays out wider,
##   a smaller one scales down) instead of a layout at exactly WxH pixels.
## --ui-dump (with --screenshot): print every text of every screen, hidden ones included, as "LAUNCHER ui text:" lines, unscrubbed (the
##   test that no screen shows the home folder or the user name); --screenshot-size=WxH lays the window out at that size and saves
##   exactly that (the screenshots at 1280x720 and 1280x800 on any display).
## Test options (honoured only by a development run or a test package, BuildInfo.tests_allowed):
##   --api-base=http://127.0.0.1:<port>   the API origin (a local test server); --trust-key=<64 hex>[,<64 hex>...]   the release key(s) of a development run; --trust-nightly-key=<64 hex>[,<64 hex>...]   (lane RELGH-2) nightly-only keys;
##   --repo=<owner>/<name>   the repository; --launcher-dir=<folder>   the launcher's own folder in a development run (the combined
##   package's game beside it, the staged launcher update); --launcher-version=<version>   its version there; --aio-base=http://127.0.0.1:<port>   the AIO service's stand-in (both of its hosts);
##   --aio-pins=<file>   made-up pinned tables (JSON) for that stand-in; --aio-stop-after=<folder>/<file>:<pause|cancel>   a stop right
##   after that file was kept (the pause / cancel paths); --mod-stop-after=<part>/<file>:<pause|cancel>   the same for a mod download (lane
##   MODS-3); --ui-demo=<screen>   made-up state for the screenshots of each screen (no
##   network; tools/release/launcher_screens.py; the screens are listed at _show_demo); --ui-stop=<pause|cancel|play>@<text>   press
##   Pause / Cancel / Play once while a download whose progress names <text> runs, --ui-then=resume|cancel   then press Resume / Cancel
##   once (with --screenshot)
##   OPENBFME_TEST_LAUNCHER_OPTIONS (environment): test options, separated by spaces, for a launcher a game starts by itself
##   (tools/release/onezip_play_test.py); nothing but test options is taken from it
## Exit status: 0 done; 1 an install / play / select failed (or the AIO download was refused: setting off, no consent, a bad folder); 2
## updates skipped (offline, API error; an AIO download that broke or was stopped: it resumes next time); 3 an update was rejected (signature, size,
## digest, archive or rollback check; the AIO service's data failed a check). Every step prints a LAUNCHER line (also in user://launcher.log).
extends Control

const BuildInfo := preload("res://scripts/core/build_info.gd")
const AppIcon := preload("res://scripts/ui/app_icon.gd") # lane OWNERFIX-3
const Updater := preload("res://scripts/core/updater.gd")
const MacOSUpdate := preload("res://scripts/core/macos_update.gd")
const SelfUpdate := preload("res://scripts/core/self_update.gd")
const Semver := preload("res://scripts/core/semver.gd")
const Log := preload("res://scripts/core/launcher_log.gd")
const GlibcCheck := preload("res://scripts/core/glibc_check.gd")
const GameOptions := preload("res://scripts/core/game_options.gd")
const GameDisplay := preload("res://scripts/core/game_display.gd")
const AioInstall := preload("res://scripts/core/aio_install.gd")
const GameFiles := preload("res://scripts/core/game_files.gd")
const VerifyFilesPanel := preload("res://scripts/ui/verify_files_panel.gd")
const ModInstall := preload("res://scripts/core/mod_install.gd")  # lane MODS-3
const ModCatalog := preload("res://scripts/core/mod_catalog.gd")
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
const AssetPacks := preload("res://scripts/core/asset_packs.gd") # lane SDK-1
const GamesScreen := preload("res://scripts/ui/games_screen.gd")
const SettingsScreen := preload("res://scripts/ui/settings_screen.gd")
const MessagePanel := preload("res://scripts/ui/message_panel.gd")
const PlayerMessages := preload("res://scripts/ui/player_messages.gd")
const InstanceLock := preload("res://scripts/core/instance_lock.gd")
const LauncherCrash := preload("res://scripts/core/launcher_crash.gd")
const GameStart := preload("res://scripts/core/game_start.gd")
const OnlineCount := preload("res://scripts/core/online_count.gd")  # lane ONLINECOUNT-1
const HttpFetch := preload("res://scripts/net/http_fetch.gd")
const SocialClient := preload("res://scripts/net/social_client.gd")
const OAuthAccount := preload("res://scripts/core/oauth_account.gd")
const SocialLaunch := preload("res://scripts/core/social_launch.gd")
const ReleaseSignature := preload("res://scripts/crypto/release_signature.gd")  # lane SIGNKMS-1: --trust-key lists
const L10n := preload("res://scripts/l10n/l10n.gd")  # lane L10N-1: the launcher's own text (tools/l10n/catalog/, docs/L10N.md)

const CLI_ACTIONS := ["--update", "--install=", "--select=", "--self-update", "--check-game-files", "--channel=", "--list", "--play", "--version", "--free-camera=",
	"--game=", "--aio=", "--aio-install=", "--auto-update=", "--online-count=", "--open-url", "--lobby=", "--register-url-handler", "--game-start=",
	"--install-desktop-entry", "--mods-list", "--mods-list=", "--mod-get=", "--mod-use=", "--mod-remove=", "--mod-args", "--replay-association=",
	"--asset-packs", "--asset-pack-on=", "--asset-pack-off="]
const UrlHandler := preload("res://scripts/core/url_handler.gd")
const DesktopEntry := preload("res://scripts/core/desktop_entry.gd") # lane OWNERFIX-3
# the test options (honoured only by a development run or a test package); ONLINE-2 r2: also the only arguments allowed before --open-url there
const TEST_OPTIONS := ["--api-base=", "--trust-key=", "--trust-nightly-key=", "--repo=", "--test-kill-at=", "--aio-base=", "--aio-pins=", "--aio-stop-after=", "--ui-demo=",
	"--ui-stop=", "--ui-then=", "--launcher-dir=", "--launcher-version=", "--test-macos-kill-at=", "--ui-play=", "--mod-stop-after=", "--language="]
const RECHECK_SECONDS := 3600.0  # lane UI-5: while the launcher stays open, it looks for updates again every hour

var info: RefCounted
var updater: Updater
var _instance: InstanceLock  # lane UI-5 (Sol r2): one launcher at a time
var _crash_marker := false   # lane CRASHACT-1: this launcher wrote user://launcher.running (removed when it ends normally)
var aio: AioInstall
var game_files: GameFiles
var _verify_panel: VerifyFilesPanel
var _verify_report: Dictionary = {}
var mods: ModInstall  # lane MODS-3
var _thread: Thread
var _count_thread: Thread     # lane ONLINECOUNT-1
var _count_http: HttpFetch    # its request (MACPLAY-1: cancelled and joined when the window closes)
var _count_timer: Timer
var _count_asked_ms := 0
var _count_text := ""         # the chip's text and when it arrived (it expires: OnlineCount.EXPIRY_SECONDS)
var _count_got_ms := 0
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
# MACFIX-1 (issue #46): the running job's newest byte progress, [text, done, total] ([] once shown), written by the worker thread and
# shown by _process once a frame (_queue_progress); _job_ui_stop is the worker's copy of _ui_stop for the job
var _progress_lock := Mutex.new()
var _progress_next: Array = []
var _job_ui_stop := ""
var _aio_target := ""         # the folder of the AIO download that runs or is paused
var _mod_job: Array = []      # lane MODS-3: [guid, enhancements] of the mod download that runs or is paused
var _demo := ""
var _demo_bfme1 := false     # --ui-demo games-bfme1: the Game files screen offers the BFME1 package without the choice being saved
var _problem := ""            # why updates are off ("" when they work)
var _pv := ""                 # the version Play starts (refreshed with the installed versions)
var _update_state := "idle"
var _update_tag := ""
var _status_text := ""
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
var _social: SocialClient
var _multiplayer: MultiplayerScreen
var _social_handoff := false
var _banner: PanelContainer
var _warning: Label
var _macos_releases_link: LinkButton
var _macos_package_notice := ""
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
	# lane OWNERFIX-3: the window / taskbar / dock icon is the OpenBFME ring (the project sets none, so Godot's logo showed)
	var icon_error: String = AppIcon.apply()
	if icon_error != "":
		Log.line("LAUNCHER app icon: " + icon_error)
	info = BuildInfo.load_info()
	updater = Updater.new(info)
	aio = AioInstall.new(updater)
	aio.default_languages(String(Paths.game_folders().rotwk))  # issue #49: no saved choice: the found games' language
	game_files = GameFiles.new(aio)
	mods = ModInstall.new(updater, aio)
	var args := OS.get_cmdline_user_args()
	var raw_replay := args.is_empty() and OS.get_cmdline_args().size() == 1 and preload("res://scripts/core/replay_open.gd").is_recording(OS.get_cmdline_args()[0])
	var finder_open := OS.get_name() == "macOS" and raw_replay
	if raw_replay:
		args = OS.get_cmdline_args()
	if info.tests_allowed():
		MacOSUpdate.kill_at = OS.get_environment("OPENBFME_TEST_MACOS_KILL_AT")
	# lane ONEZIP-1: a game started directly runs the launcher beside it with fixed arguments; a test of that start gives the test
	# options through the environment (test builds and development runs only, like every test option)
	var test_env := OS.get_environment("OPENBFME_TEST_LAUNCHER_OPTIONS")
	if test_env != "" and info.tests_allowed():
		var first := PackedStringArray()   # before the command line's own: a link still has to come last and alone
		for a in test_env.split(" ", false):
			if TEST_OPTIONS.any(func(p): return a.begins_with(p)) and not a in args:
				first.append(a)
		args = first + args
	_choose_language(args)  # lane L10N-1: before any text is made
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
			elif a.begins_with("--test-macos-kill-at="):
				MacOSUpdate.kill_at = a.substr(21)
			elif a.begins_with("--launcher-dir="):
				updater.set_launcher_dir(a.substr(15))
			elif a.begins_with("--launcher-version="):
				info.version = a.substr(19)
			elif a.begins_with("--aio-stop-after="):
				aio.test_stop_after = a.substr(17)
			elif a.begins_with("--mod-stop-after="):
				mods.test_stop_after = a.substr(17)
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
			elif a.begins_with("--trust-key=") and info.release_keys.is_empty():
				# lane SIGNKMS-1: one key or a comma-separated list (the first plays launcher/release_key.pub)
				var trusted := ReleaseSignature.parse_hex_list(a.substr(12))
				if trusted.ok:
					info.release_keys = trusted.keys
				else:
					Log.line("LAUNCHER --trust-key is not used: " + trusted.error)
			elif a.begins_with("--trust-nightly-key=") and info.nightly_keys.is_empty():
				# lane RELGH-2: nightly-only keys (valid for a nightly manifest only), none of them a release key
				var nightly := ReleaseSignature.parse_nightly_list(a.substr(20))
				for k in nightly.keys:
					if k in info.release_keys:
						nightly = {"ok": false, "error": L10n.t("launcher.main.nightly_key_is_release_key"), "keys": []}
				if nightly.ok:
					info.nightly_keys = nightly.keys
				else:
					Log.line("LAUNCHER --trust-nightly-key is not used: " + nightly.error)
	if info.release_keys.is_empty() and problem == "":
		problem = L10n.t("launcher.main.no_release_key")
	updater.recover()
	var replay_mode := preload("res://scripts/core/replay_open.gd").arguments(args, TEST_OPTIONS if info.tests_allowed() else [])
	if replay_mode.mode:
		var result: Dictionary
		if not replay_mode.ok:
			result = replay_mode
		elif finder_open and not bool(updater.get_state("launcher", "replay_association", true)):
			result = {"ok": false, "error": L10n.t("launcher.main.recording_open_disabled")}
		else:
			result = updater.open_recording(replay_mode.path)
		if not result.ok:
			Log.line("LAUNCHER " + result.error)
			if DisplayServer.get_name() != "headless":
				OS.alert(result.error, L10n.t("launcher.main.recording_alert_title"))
		get_tree().quit(0 if result.ok else 1)
		return
	var start := SelfUpdate.on_start(updater, args, GameStart.request_of(args) == "" or SelfUpdate._arg(args, "--apply-update=") != "")
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
	var game_start := GameStart.request_of(args)
	if game_start != "":
		# lane ONEZIP-1: the game asked (it was started directly); this run takes the launcher lock only for its update part, and never
		# stops because a launcher window is open
		var code := GameStart.new(updater, game_start).run(problem)
		# the game has its answer and the update part is done: a launcher update staged by it (or earlier) is applied now, so the next
		# start runs the new launcher (its helper and the new launcher get --version: they print it and end)
		var forward := PackedStringArray(Array(args).filter(func(x): return TEST_OPTIONS.any(func(p): return x.begins_with(p))))
		forward.append("--version")
		SelfUpdate.on_start(updater, forward)
		get_tree().quit(code)
		return
	if not url_mode.mode and not (cli and Array(args).all(func(x): return x == "--version" or not CLI_ACTIONS.any(func(c): return x == c or (c.ends_with("=") and x.begins_with(c))))):
		_instance = InstanceLock.new(OS.get_user_data_dir(), info.version)
		# lane ONEZIP-1: a background update a directly started game asked for holds the lock: it is asked to stop (its download resumes
		# here) and this launcher takes over
		if not _instance.acquire() and not GameStart.take_over(_instance):
			# Sol r4: "already open" only when the port's holder answered as a launcher; another program on the port is said as such
			var why := L10n.t("launcher.main.already_open", {"launcher": _instance.other_launcher}) \
				if _instance.other_launcher != "" else L10n.t("launcher.main.cannot_start_port", {"error": _instance.error})
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
	_init_social()
	_crash_start()
	if start.message != "":
		_set_status(start.message)
	_refresh_installed()
	get_window().files_dropped.connect(_replay_files_dropped)
	if info.packaged and bool(updater.get_state("launcher", "replay_association", true)) and \
			(str(updater.get_state("launcher", "replay_association_exe", "")) != OS.get_executable_path() or not bool(updater.get_state("launcher", "save_association_registered", false))):
		_set_replay_association(true)
	var count_problem := updater.online_count_problem()
	if count_problem != "":
		Log.line("LAUNCHER " + count_problem)
	_refresh_count()
	var unseen := str(updater.get_state("launcher", "unseen_error", ""))
	if unseen != "" and _demo == "":
		Log.line("LAUNCHER showing an update failure from while the game ran: " + unseen)
		_message.show_error(L10n.t("launcher.main.update_failed_while_playing"), unseen, _about(), PlayerMessages.Operation.DOWNLOAD)
	if _demo != "":
		_show_demo(_demo)
	elif problem != "":
		_set_update_state("failed")
		_set_status(L10n.t("launcher.main.updates_off", {"problem": problem}))
	elif not "--offline" in args and (not updater.macos_preview() or updater.auto_update()):
		_start_job(_job_check.bind(updater.auto_update()), "check")
	elif updater.macos_preview():
		_set_update_state("manual")
	elif "--offline" in args:
		_set_update_state("offline")
	for a in args:
		if a.begins_with("--ui-play=") and info.tests_allowed():
			await _ui_play(int(a.substr(10)))
	for a in args:
		if a.begins_with("--screenshot="):
			_screenshot(a.substr(13))


## lane CRASHACT-1 (scripts/core/launcher_crash.gd): the last launcher's crash reported (queued, or offered when automatic reports are off), this
## launcher's marker written, the queue sent in the background. Only a packaged launcher (or a test's loopback crash service), holding the lock.
func _crash_start() -> void:
	if not LauncherCrash.eligible(info.packaged) or _demo != "" or _instance == null:
		return
	var previous := LauncherCrash.take_previous()
	LauncherCrash.write_marker(info.version)
	_crash_marker = true
	var automatic := LauncherCrash.automatic()
	if not previous.is_empty():
		var r: Dictionary = LauncherCrash.report(previous)
		if not r.ok:
			Log.line("LAUNCHER CRASH the last launcher did not end normally; not reported: " + str(r.error))
		elif automatic:
			LauncherCrash.enqueue(r.wire)
		else:
			Log.line("LAUNCHER CRASH the last launcher did not end normally; automatic reports are off: Send report offered")
			_message.ask(L10n.t("launcher.crash.title"), L10n.t("launcher.crash.offer"), L10n.t("launcher.crash.send"), func() -> void:
				if LauncherCrash.enqueue(r.wire).is_empty():
					LauncherCrash.upload(self))
	if automatic:
		LauncherCrash.upload(self)


## tests (MACPLAY-1): press the real PLAY button `times` times, each once no job runs (its pressed signal: the player's click, _on_play),
## and say what the window shows after each press
func _ui_play(times: int) -> void:
	for n in times:
		await _settle()
		Log.line("LAUNCHER ui test: PLAY pressed (%s, Play %s)" % [_pv if _pv != "" else "no version", "disabled" if _play.disabled else "enabled"])
		_play.pressed.emit()
		await get_tree().process_frame
		Log.line("LAUNCHER ui status: " + _status.text)
		Log.line("LAUNCHER ui dialog: " + (_message.shown_text() if _message.visible else ""))


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
		elif a.begins_with("--screenshot-window="):
			# what a window of that size shows (lane LAUNCHERUI-1): the project's stretch (canvas_items, expand aspect over 1280 x 720)
			# laid out on a display of any size: the base is the window over min(w / 1280, h / 720), saved at the window's pixels
			var wh := a.substr(20).split("x")
			want = Vector2i(int(wh[0]), int(wh[1]))
			var k := minf(want.x / BASE_SIZE.x, want.y / BASE_SIZE.y)
			get_window().content_scale_size = Vector2i((Vector2(want) / k).round())
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
	Log.line("LAUNCHER ui update pill: " + (_play_screen.state_chip.get_child(0) as Label).text)
	Log.line("LAUNCHER ui status head: " + _status_head.text)
	Log.line("LAUNCHER ui warning: " + (_warning.text if _banner.visible else ""))
	Log.line("LAUNCHER ui chip overlaps: " + ", ".join(_play_screen.chip_overlaps()))
	Log.line("LAUNCHER ui games card: " + ("shown" if _play_screen.games_card.visible else "hidden"))
	Log.line("LAUNCHER ui dialog: " + (_message.shown_text() if _message.visible else ""))
	Log.line("LAUNCHER ui update button: " + (_update.text if _update.visible else ""))
	if _macos_package_notice != "":
		Log.line("LAUNCHER ui Mac package notice: " + _macos_package_notice)
	if _macos_releases_link != null:
		Log.line("LAUNCHER ui releases link: " + ("shown" if _macos_releases_link.visible else "hidden"))
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
	if not "--ui-keep-open" in OS.get_cmdline_user_args():
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
## than the build's GLIBC, lane GLIBC-1). Lane LAUNCHERUI-1 (the layout check, tests/layout_test.gd) adds the states a player meets: first-run
## (nothing installed, no games), offline (the check failed), verifying (an install is being checked), paused (a paused download),
## update-launcher (only the launcher is newer), warning (the freeze banner), ask (the question dialog), playing (Play pressed while an
## update downloads), long (the longest texts the window gets), picker (the games' folder picker). Community PR #36: games-bfme1 (Battle for
## Middle-earth I chosen: the optional BFME1 download; the choice is not saved).
func _show_demo(screen: String) -> void:
	var home := Paths.home()
	var no_games := screen in ["games", "games-downloading", "first-run", "games-bfme1"]
	var games := {"found": true, "rotwk": home.path_join("Games/BFME/RotWK"), "bfme2": home.path_join("Games/BFME/BFME2"), "source": "config"}
	if no_games:
		games = {"found": false, "rotwk": "", "bfme2": "", "source": ""}
	_show_games(games, true)
	var rels := updater.cached_releases()
	if rels.is_empty():
		var sample := "DEMO TEXT for the launcher screenshots, not real release notes.\n\n## What's new\n\n- Sample note: what changed in this build.\n" + \
			"- Sample note: another change.\n\n## What's fixed\n\n- Sample note: a fix.\n\n## Known issues\n\n- Sample note: a known issue.\n"
		# lane NIGHTLY-2: two nightlies of one day, the longest version names the window shows ("Nightly 0.4.12 (2026-10-23, evening)")
		rels = [{"tag": "v0.4.12-nightly.261023200", "prerelease": true, "notes": sample, "published": "", "assets": {}},
			{"tag": "v0.4.12-nightly.261023100", "prerelease": true, "notes": sample, "published": "", "assets": {}}]
	_releases = rels
	var chan := updater.in_channel(rels, updater.channel())
	if chan.is_empty():
		updater.set_channel("preview")  # the screenshots' launcher is a development run (Stable); the demo is a Nightly player's
		_show_channel()
		chan = rels
	var installed: String = chan[1].tag if screen in ["update", "downloading", "error", "paused", "update-launcher", "long"] and chan.size() > 1 else chan[0].tag
	if screen == "first-run":
		installed = ""
	_pv = installed
	_older = chan.map(func(r): return r.tag)
	_settings.set_versions([{"version": installed}] if installed != "" else [], installed, _older)
	_settings.set_options(true, screen != "update", true, _about())
	_play_screen.set_players(L10n.t("launcher.online_count.players_online", {"count": 12345}))  # lane ONLINECOUNT-1: the widest chip, so the screens show the layout with it
	_play.disabled = installed == ""
	_show_notes()
	_set_update_state("current")
	_set_status(L10n.t("launcher.main.up_to_date_status"))
	match screen:
		"news", "multiplayer", "mods":
			if screen == "mods":
				_mods.demo()
			_show_screen(screen)
		"settings", "display", "about":
			_show_screen("settings")
			_settings.show_tab({"settings": "general", "display": "display", "about": "about"}[screen])
		"games-bfme1":
			_demo_bfme1 = true
			_play_screen.game_picker.select(1)
			aio.choose(AioInstall.BFME1)
			_show_screen("games")
			_games_screen.setup(Paths.default_games_dir(), aio.total_size() / 1e9, aio.chosen(), true)
		"games", "games-downloading":
			_show_screen("games")
			_games_screen.setup(Paths.default_games_dir(), aio.total_size() / 1e9, aio.chosen(), aio.choice == AioInstall.BFME1)
			if screen == "games-downloading":
				_job_kind = "aio"
				_demo_progress("RotWK: game files 412 of 1 830", 3328599654, 9234179686)
				_games_screen.set_download_state(true, false, true, true)
				_set_status(L10n.t("launcher.main.demo_downloading_games"))
		"glibc":
			_show_error(L10n.t("launcher.main.game_did_not_start"), L10n.t("launcher.main.cannot_be_started", {"version": installed, "reason": GlibcCheck.too_old("2.28", "2.27")}))
		"first-run":
			_show_screen("play")
			_set_update_state("none")
			_set_status(L10n.t("launcher.main.demo_first_version"))
		"offline":
			_set_update_state("failed")
			_set_status(L10n.t("launcher.main.updates_skipped", {"error": L10n.t("launcher.main.demo_offline_reason")}), PlayerMessages.Operation.UPDATE_CHECK)
		"verifying":
			_set_update_state("updating", chan[0].tag)
			_demo_progress("Checking openbfme-%s-%s.%s" % [chan[0].tag, info.platform(), "zip" if OS.get_name() == "Windows" else "tar.gz"], 1073741824, 1073741824)
			_set_status(L10n.t("launcher.main.demo_checking", {"version": chan[0].tag}))
		"paused":
			_paused = "game"
			_set_update_state("updating", chan[0].tag)
			_demo_progress("Downloading openbfme-%s-%s.tar.gz" % [chan[0].tag, info.platform()], 327155712, 1073741824)
			_set_status(L10n.t("launcher.main.game_paused"))
		"update-launcher":
			_launcher_only = true
			_latest = {"tag": chan[0].tag}
			_set_update_state("update", chan[0].tag)
			_set_status(L10n.t("launcher.main.demo_launcher_available", {"version": chan[0].tag}))
		"warning":
			_set_warning(L10n.t("launcher.main.demo_frozen"))
		"ask":
			_message.ask(L10n.t("launcher.main.install_older_title"), L10n.t("launcher.main.install_older_text", {"tag": chan[chan.size() - 1].tag, "installed": installed}), L10n.t("launcher.main.install"), Callable())
		"playing":
			_quit_when_done = true
			_set_update_state("updating", chan[0].tag)
			_demo_progress("Downloading openbfme-%s-%s.tar.gz" % [chan[0].tag, info.platform()], 327155712, 1073741824)
			_set_status(L10n.t("launcher.main.demo_play_started", {"version": installed}))
		"picker":
			_show_screen("games")
			_games_screen.setup(Paths.default_games_dir(), aio.total_size() / 1e9, aio.chosen(), aio.choice == AioInstall.BFME1)
			_games_screen.open_picker(true)
		"long":
			_latest = {"tag": chan[0].tag}
			_set_update_state("update", chan[0].tag)
			_set_warning(L10n.t("launcher.main.demo_frozen_long"))
			_set_status(L10n.t("launcher.main.demo_long_rejected", {"tag": chan[0].tag, "platform": info.platform()}), PlayerMessages.Operation.UPDATE_CHECK)
			_demo_progress("Downloading openbfme-%s-%s-a-very-long-package-name-that-goes-on.tar.gz" % [chan[0].tag, info.platform()], 327155712, 1073741824)
		"update", "downloading", "error":
			_latest = {"tag": chan[0].tag}
			_set_update_state("update", chan[0].tag)
			_set_status(L10n.t("launcher.main.version_available_status", {"version": chan[0].tag}))
			if screen == "downloading":
				_latest = {}
				_set_update_state("updating", chan[0].tag)
				_demo_progress("Downloading openbfme-%s-%s.%s" % [chan[0].tag, info.platform(), "zip" if OS.get_name() == "Windows" else "tar.gz"],
					327155712, 1073741824)
				_set_status(L10n.t("launcher.main.demo_downloading_background", {"tag": chan[0].tag, "installed": installed}))
			if screen == "error":
				_show_error(L10n.t("launcher.main.update_not_installed"), L10n.t("launcher.main.demo_error_text", {"tag": chan[0].tag, "platform": info.platform(), "folder": home.path_join(".local/share/OpenBFMELauncher/downloads")}))
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

## lane MODS-3: --mods-list, --mod-get, --mod-use, --mod-remove, --mod-args (in that order); `code` is the exit status so far
func _run_mods_cli(args: PackedStringArray, code: int) -> int:
	var kinds := {AioInstall.REFUSED: 1, AioInstall.NETWORK: 2, AioInstall.REJECTED: 3, AioInstall.STOPPED: 2}
	if "--mods-list" in args or "--mods-list=refresh" in args:
		var c := mods.catalogue("--mods-list=refresh" in args)
		if not c.ok:
			Log.line("LAUNCHER MODS no list: " + c.error)
			return kinds[c.kind]
		Log.line("LAUNCHER MODS %d packages%s" % [c.view.packages.size(), " (kept from the last half hour)" if c.from_cache else ""])
		for row in c.view.packages:
			_print_mod(row, "")
			for e in row.enhancements:
				_print_mod(e, "+ ")
		if not c.view.loose.is_empty():
			Log.line("LAUNCHER MOD enhancements of the original game or of a package that is not listed:")
		for row in c.view.loose:
			_print_mod(row, "")
	for a in args:
		if a.begins_with("--mod-get="):
			var with := PackedStringArray()
			for b in args:
				if b.begins_with("--mod-with="):
					with.append_array(b.substr(11).split(",", false))
			var r := mods.install(a.substr(10), Array(with), "--mod-consent" in args)
			if r.ok:
				Log.line("LAUNCHER MODS done: %s %s (%s), %d files downloaded, %d already there, %d removed" % [r.summary.name, r.summary.version, r.status, r.downloaded, r.kept, r.removed])
			else:
				if r.kind == AioInstall.STOPPED and mods.stop_request == "cancel":
					Log.line("LAUNCHER MODS the download was cancelled")
				Log.line("LAUNCHER MODS not installed: " + r.error)
				code = kinds[r.kind]
	for a in args:
		if a.begins_with("--mod-use="):
			var g := a.substr(10)
			g = "" if g == "none" else g
			var why := mods.set_active(g, mods.record(g).get("enhancements", []) if g != "" else [])
			if why != "":
				Log.line("LAUNCHER MODS cannot pick %s: %s" % [g, why])
				code = 1
	for a in args:
		if a.begins_with("--mod-remove="):
			var why := mods.uninstall(a.substr(13))
			if why != "":
				Log.line("LAUNCHER MODS not removed: " + why)
				code = 1
	# lane SDK-1: the asset packs (on / off first, then the list shows the result)
	for a in args:
		if a.begins_with("--asset-pack-on=") or a.begins_with("--asset-pack-off="):
			var on := a.begins_with("--asset-pack-on=")
			var why := AssetPacks.set_enabled(a.get_slice("=", 1), on)
			if why != "":
				Log.line("LAUNCHER ASSET PACKS cannot turn it %s: %s" % ["on" if on else "off", why])
				code = 1
	if "--asset-packs" in args:
		var packs := AssetPacks.list()
		Log.line("LAUNCHER ASSET PACKS %d in %s" % [packs.size(), AssetPacks.root()])
		for p in packs:
			Log.line("LAUNCHER ASSET PACK %s | %s | %d models, %d textures | %s" % [p.name, "on" if p.enabled else "off", p.models, p.textures,
				ModInstall.size_text(int(p.bytes))])
			for why in p.problems:
				Log.line("LAUNCHER ASSET PACK %s problem: %s" % [p.name, why])
	if "--mod-args" in args:
		var ga := updater.game_arguments()
		Log.line("LAUNCHER game arguments: " + (" ".join(ga.args) if ga.ok else "none, the game is not started: " + ga.error))
		if not ga.ok:
			code = 1
	return code


func _print_mod(row: Dictionary, indent: String) -> void:
	var state: String = row.status if row.error == "" else "refused: " + row.error
	Log.line("LAUNCHER MOD %s%s | %s | %s | %s %s | by %s | %s" % [indent, row.guid, ModCatalog.type_name(row.type), state, row.name, row.version,
		row.author if row.author != "" else "?", ModInstall.size_text(int(row.get("bytes", row.size)))])
	for e in row.engine_edits:
		Log.line("LAUNCHER MOD %s  needs an engine feature: %s" % [indent, e])


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
	for a in args:
		if a.begins_with("--replay-association="):
			var value := a.substr(21)
			if not value in ["on", "off"]:
				Log.line("LAUNCHER --replay-association must be on or off")
				return 1
			if not _set_replay_association(value == "on"):
				return 1
	updater.progress = func(text: String, done: int, total: int) -> void:
		if done < 0:
			Log.line("LAUNCHER " + text)
	for a in args:
		if a.begins_with("--channel="):
			var ch := a.substr(10)
			if ch == "alpha":
				ch = "stable"  # lane VERSION-1: the old shown name of the stable channel, where the plain releases ("Alpha 0.4.5") are
			elif ch == "nightly":
				ch = "preview"  # lane NIGHTLY-1 (owner, 2026-10-21): the shown name of the pre-release channel, whose id stays "preview"
			if not ch in ["stable", "preview"]:
				Log.line("LAUNCHER unknown channel '%s' (stable, nightly)" % ch)
				return 1
			updater.set_channel(ch)
			Log.line("LAUNCHER channel %s" % ch)
	for a in args:
		if a.begins_with("--game="):
			var game := a.trim_prefix("--game=")
			if game not in ["bfme1", "bfme2"]:
				Log.line("LAUNCHER --game takes bfme1 or bfme2")
				return 1
			updater.set_state("launcher", "game", game)
	if "--version" in args:
		print("OpenBFME Launcher %s" % info.version)
	if "--check-game-files" in args:
		var folders := Paths.game_folders()
		if folders.found:
			print("GAME FILES found (source: %s)" % folders.source)
			Log.line("LAUNCHER game files found (source: %s)" % folders.source)
		else:
			print("GAME FILES missing (set both game folders or choose them in the launcher)")
			Log.line("LAUNCHER game files not found")
			code = 1
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
		if a.begins_with("--online-count="):
			var v := a.substr(15)
			if not v in ["on", "off"]:
				Log.line("LAUNCHER --online-count takes on or off, not '%s'" % v)
				return 1
			updater.set_show_online_count(v == "on")
			Log.line("LAUNCHER online player count %s" % v)
	for a in args:
		if a.begins_with("--aio="):
			var v := a.substr(6)
			if not v in ["on", "off"]:
				Log.line("LAUNCHER --aio takes on or off, not '%s'" % v)
				return 1
			aio.set_enabled(v == "on")
			Log.line("LAUNCHER AIO setting %s" % v)
	for a in args:
		if a.begins_with("--aio-package="):
			var why := aio.choose(a.substr(14))
			if why != "":
				Log.line("LAUNCHER --aio-package: " + why)
				return 1
	# issue #49: the download's text and voice languages (codes of AioInstall.offered_languages; saved like the window's choice)
	for a in args:
		if a.begins_with("--aio-language=") or a.begins_with("--aio-voice="):
			var text := aio.text_language
			var voice := aio.voice_language
			for b in args:
				if b.begins_with("--aio-language="):
					text = b.substr(15)
				elif b.begins_with("--aio-voice="):
					voice = b.substr(12)
			var why := aio.set_languages(text, voice)
			if why != "":
				Log.line("LAUNCHER --aio-language / --aio-voice: " + why)
				return 1
			Log.line("LAUNCHER AIO language %s" % aio.language_text())
			break
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
	code = _run_mods_cli(args, code)
	for a in args:
		if a.begins_with("--select="):
			var v := a.substr(9)
			if v != "" and not updater.playable().any(func(i): return i.version == v):
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
					var mm := {} if updater.macos_preview() else updater.release_manifest(chan[0])
					if not updater.macos_preview() and mm.ok and updater.freeze_warning(mm.manifest) != "":
						Log.line("LAUNCHER warning: " + updater.freeze_warning(mm.manifest))
					if updater.macos_preview():
						var notice := updater.macos_preview_notice(chan[0])
						Log.line("LAUNCHER " + (notice.message + ": " + notice.url if notice.ok else "Update check rejected: " + notice.error))
						if not notice.ok:
							code = 3
					elif not mm.ok:
						Log.line("LAUNCHER update rejected: " + mm.error)
						code = 3
					elif updater.macos_updates_enabled() and mm.get("no_entry", false):
						Log.line("LAUNCHER this verified release has no signed Mac package; download a complete package manually from %s" % updater.releases_url())
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
		for i in updater.playable():
			Log.line("LAUNCHER installed %s%s%s" % [i.version, " (selected)" if i.version == updater.selected() else "",
				" (the game beside the launcher)" if i.get("bundled", false) else ""])
	for a in args:
		if a.begins_with("--lobby="):
			updater.set_state("online", "lobby", a.substr(8).strip_edges())
			Log.line("LAUNCHER online lobby: %s" % a.substr(8).strip_edges())
	if "--register-url-handler" in args:
		var reg := UrlHandler.register(OS.get_executable_path())
		Log.line("LAUNCHER openbfme:// links: %s" % (("registered (%s)" % reg.where) if reg.ok else "not registered: " + reg.error))
		if not reg.ok:
			code = 1
	if "--install-desktop-entry" in args:
		if OS.get_name() in ["Linux", "FreeBSD", "NetBSD", "OpenBSD", "BSD"]:
			var de := DesktopEntry.install(OS.get_executable_path(), DesktopEntry.data_home())
			Log.line("LAUNCHER desktop entry: %s" % (("written (%s)" % de.where) if de.ok else "not written: " + de.error))
			if not de.ok:
				code = 1
		else:
			Log.line("LAUNCHER desktop entry: only on Linux (Windows and macOS take the icon from the executable / the app bundle)")
			code = 1
	if "--play" in args:
		var v := updater.play_version()
		if v == "":
			Log.line("LAUNCHER nothing to play: " + updater.no_game_reason())
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
const RAIL_INK := Style.NAV_PAD  # lane LAUNCHERUI-1: the rail's logo and footer start where an entry's icon does
const SCREEN_MARGIN := 34  # the screens' and the bottom bar's left and right margin: one right edge for every card and PLAY
const BAR_H := 96.0
const SCREENS := [["play", "swords", "launcher.main.nav_play"], ["news", "star", "launcher.main.nav_news"], ["multiplayer", "rings", "launcher.main.nav_multiplayer"],
	["mods", "hammer", "launcher.main.nav_mods"], ["games", "drive", "launcher.main.nav_games"], ["settings", "gear", "launcher.main.nav_settings"]]


## A DPI-scaled window: on a screen with a scale above 1 (Windows: its DPI / 96) the window grows by that factor, within 92% of the
## usable screen; the canvas_items stretch scales the content with it. Not with --resolution (the screenshots) or without a display.
func _fit_window() -> void:
	if DisplayServer.get_name() == "headless":
		return
	var win := get_window()
	win.title = L10n.t("launcher.main.title_local_trial") if info.local_trial else "OpenBFME Launcher"
	if updater.macos_preview():
		win.title = L10n.t("launcher.main.title_mac_preview")
	var scr := win.current_screen
	var screen_scale := DisplayServer.screen_get_scale(scr)
	# MACPLAY-1: Godot sizes a macOS window in the screen's pixels (2 per point on Retina): the 960 x 540 minimum is in points there
	win.min_size = Vector2i((MIN_SIZE * (screen_scale if OS.get_name() == "macOS" else 1.0)).round())
	var usable := Vector2(DisplayServer.screen_get_usable_rect(scr).size)
	var dpi := DisplayServer.screen_get_dpi(scr)
	var want := fit_size(OS.get_name(), screen_scale, dpi, usable)
	Log.line("LAUNCHER window: screen scale %.2f, %d dpi, usable %dx%d px -> %s" % [screen_scale, dpi, int(usable.x), int(usable.y),
		"%dx%d px" % [want.x, want.y] if want != Vector2i.ZERO else "the project's size"])
	if "--resolution" in OS.get_cmdline_args() or OS.get_cmdline_args().has("--fullscreen") or want == Vector2i.ZERO:
		return
	win.size = want
	win.move_to_center()


const MIN_SIZE := Vector2(960, 540)
## the dots per inch of the layout's base unit on a Windows screen at 100 % (the size the launcher was designed at)
const REFERENCE_DPI := 96.0


## The window's size in pixels on a screen (Vector2i.ZERO: keep the project's 1280 x 720): BASE_SIZE times the screen's factor, within
## 92 % of its usable area. Windows: DPI / 96. MACPLAY-1, macOS: Godot reports the Retina factor (2) as the scale, but a Mac point is
## smaller than a Windows pixel at 100 % (a MacBook Air's default "looks like" 1470 x 956 is ~128 points per inch, Windows' reference
## 96): at the Retina factor alone the launcher read about three quarters of its Windows size. The factor is the screen's physical
## pixels per inch over 96, so a base unit has the same physical size as on Windows, never below the Retina factor and at most 1.5 times
## it (a screen that reports no or a wrong DPI keeps the Retina factor). Other systems: the screen's scale.
static func fit_size(os_name: String, screen_scale: float, dpi: int, usable: Vector2) -> Vector2i:
	var scale := screen_scale
	if os_name == "Windows":
		scale = dpi / REFERENCE_DPI
	elif os_name == "macOS":
		scale = clampf(dpi / REFERENCE_DPI, screen_scale, screen_scale * 1.5) if dpi > 0 else screen_scale
	if scale <= 1.05:
		return Vector2i.ZERO
	var want := BASE_SIZE * scale
	want *= minf(1.0, minf(usable.x * 0.92 / want.x, usable.y * 0.92 / want.y))
	return Vector2i(want)


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
	var rv := W.vbox(Style.NAV_GAP)
	rm.add_child(rv)
	var logo := W.label("OPENBFME")
	Style.text(logo, "Cinzel", 700, 25, Style.ACCENT, 2)
	var lm := MarginContainer.new()
	lm.add_theme_constant_override("margin_left", RAIL_INK)
	lm.add_theme_constant_override("margin_bottom", 16)
	lm.add_child(logo)
	rv.add_child(lm)
	var group := ButtonGroup.new()
	for s in SCREENS:
		var b := W.button(L10n.t(s[2]), s[1], "NavButton", 18)
		b.clip_text = true
		b.tooltip_text = b.text
		b.custom_minimum_size.y = Style.NAV_HEIGHT
		b.size_flags_vertical = Control.SIZE_FILL
		b.toggle_mode = true
		b.button_group = group
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.pressed.connect(_show_screen.bind(s[0]))
		rv.add_child(b)
		_nav[s[0]] = b
	rv.add_child(W.spacer())
	rv.get_child(rv.get_child_count() - 1).size_flags_vertical = Control.SIZE_EXPAND_FILL
	var foot := W.trim_label(L10n.t("launcher.main.footer_version", {"version": Semver.display(info.version)}), "Muted")
	if info.local_trial:
		foot.text += "\n" + L10n.t("launcher.main.footer_local_trial")
	foot.add_theme_font_size_override("font_size", 12)
	var fm := MarginContainer.new()
	fm.add_theme_constant_override("margin_left", RAIL_INK)
	fm.add_child(foot)
	rv.add_child(fm)

	# the screens
	var content := MarginContainer.new()
	content.set_anchors_preset(Control.PRESET_FULL_RECT)
	content.offset_left = RAIL_W
	content.offset_bottom = -BAR_H
	content.add_theme_constant_override("margin_left", SCREEN_MARGIN)
	content.add_theme_constant_override("margin_right", SCREEN_MARGIN)
	content.add_theme_constant_override("margin_top", 28)
	content.add_theme_constant_override("margin_bottom", 4)
	add_child(content)
	var cv := W.vbox(12)
	content.add_child(cv)
	if updater.macos_preview():
		cv.add_child(W.wrap(L10n.t("launcher.main.mac_preview_notice")))
		var releases := LinkButton.new()
		releases.text = L10n.t("launcher.main.open_releases_page")
		releases.uri = updater.releases_url()
		cv.add_child(releases)
	elif updater.macos_updates_enabled():
		# MACPLAY-1: what the player must know about the package beside this launcher (nothing when it is verified, or when there is none
		# and the launcher installs one); a translocated copy says how to get out of it
		if updater.translocated != "":
			_macos_package_notice = Updater.translocated_text()
		elif updater.launcher_dir != "" and MacOSUpdate.signed_required(updater, updater.launcher_dir):
			var receipt := MacOSUpdate.signed_receipt(updater, updater.launcher_dir)
			_macos_package_notice = "" if receipt.ok else L10n.t("launcher.main.mac_receipt_failed", {"error": receipt.error})
		elif updater.bundled_problem() != "":
			_macos_package_notice = L10n.t("launcher.main.mac_bundle_unsigned", {"problem": updater.bundled_problem()})
		var notice := W.wrap(_macos_package_notice)
		notice.visible = _macos_package_notice != ""
		cv.add_child(notice)
		_macos_releases_link = LinkButton.new()
		_macos_releases_link.text = L10n.t("launcher.main.open_releases_page")
		_macos_releases_link.uri = updater.releases_url()
		_macos_releases_link.visible = false
		cv.add_child(_macos_releases_link)
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
	_play_screen.game_picker.select(1 if updater.get_state("launcher", "game", "bfme2") == "bfme1" else 0)
	_play_screen.game_selected.connect(func(game: String):
		updater.set_state("launcher", "game", game)
		_refresh_games())
	_play_screen.download_games.connect(_open_download)
	_play_screen.show_news.connect(_show_screen.bind("news"))
	_play_screen.open_replay.connect(_pick_replay)
	_play_screen.open_save.connect(func(): _pick_replay(true))
	_news = NewsScreen.new()
	var mp := MultiplayerScreen.new()
	_multiplayer = mp
	mp.build(info.repo)
	_mods = ModsScreen.new()
	_mods.refresh.connect(func(force: bool) -> void: _start_job(_job_mods_list.bind(force), "mods-list"))
	_mods.install.connect(_on_mod_install)
	_mods.remove.connect(_on_mod_remove)
	_mods.pick.connect(_on_mod_pick)
	_mods.pause_pressed.connect(_on_pause)
	_mods.cancel_pressed.connect(_on_cancel)
	_mods.asset_pack_toggled.connect(func(name: String, on: bool) -> void: # lane SDK-1
		var why := AssetPacks.set_enabled(name, on)
		Log.line("LAUNCHER ASSET PACK %s turned %s%s" % [name, "on" if on else "off", "" if why == "" else ": not done, " + why])
		_mods.set_asset_packs(AssetPacks.list()))
	_mods.open_asset_packs.connect(func() -> void:
		var dir := AssetPacks.ensure_root()
		var why := Paths.show_in_file_manager(dir) if dir != "" else "it cannot be made"
		if why != "":
			Log.line("LAUNCHER ASSET PACKS cannot open the folder %s: %s" % [AssetPacks.root(), why]))
	_games_screen = GamesScreen.new()
	_games_screen.download.connect(_start_aio)
	_games_screen.forget_folders.connect(_on_forget_folders)
	_games_screen.show_folder.connect(_show_folder)
	_games_screen.verify_files.connect(_start_verify_files)
	_games_screen.pause_pressed.connect(_on_pause)
	_games_screen.cancel_pressed.connect(_on_cancel)
	_games_screen.languages_changed.connect(_on_languages)
	_games_screen.set_languages(aio.offered_languages(), aio.text_language, aio.voice_language)
	_settings = SettingsScreen.new(updater.macos_preview())
	_settings.channel_picked.connect(_on_channel)
	_settings.free_camera_toggled.connect(_on_free_camera)
	_settings.display_changed.connect(_on_display_changed)
	_settings.display_refresh_requested.connect(_refresh_display)
	get_window().focus_entered.connect(_refresh_display)
	_settings.auto_update_toggled.connect(_on_auto_update)
	_settings.aio_toggled.connect(_on_aio_toggled)
	_settings.show_online_count_toggled.connect(_on_online_count_toggled)
	_settings.use_version.connect(_on_use)
	_settings.install_release.connect(_on_install_pick)
	_settings.open_logs.connect(_open_logs)
	_settings.replay_association_toggled.connect(_set_replay_association)
	var automatic := _automatic_language()
	_settings.set_languages(_language_choices(automatic), str(updater.get_state("launcher", "language", "")), automatic.note)
	_settings.language_picked.connect(_on_language_picked)
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
	bm.add_theme_constant_override("margin_left", SCREEN_MARGIN)
	bm.add_theme_constant_override("margin_right", SCREEN_MARGIN)
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
	_status_head.mouse_filter = Control.MOUSE_FILTER_PASS   # lane NIGHTLY-2: its tooltip is the full version name (_refresh_head)
	head.add_child(_status_head)
	_status_tail = W.trim_label("")
	Style.text(_status_tail, "Inter", 500, 14, Style.TEXT2)
	_status_tail.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(_status_tail)
	info_box.add_child(head)
	_progress = W.vbox(5)
	_progress.visible = false
	var trow := W.hbox(10)
	_bar_title = W.trim_label("", "Muted")
	_bar_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
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
	_update = W.button(L10n.t("launcher.main.update_button"), "download", "GoldButton", 16)
	_update.visible = false
	_update.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_update.pressed.connect(func() -> void: _start_job(_job_update.bind(_latest), "install", String(_latest.get("tag", ""))))
	bar.add_child(_update)
	_check = W.button(L10n.t("launcher.main.check_for_updates"), "refresh", "", 15)
	_check.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_check.pressed.connect(func() -> void: _start_job(_job_check.bind(updater.auto_update()), "check"))
	bar.add_child(_check)
	_pause = W.button(L10n.t("launcher.main.pause"), "pause", "", 14)
	_pause.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_pause.pressed.connect(_on_pause)
	bar.add_child(_pause)
	_cancel = W.button(L10n.t("common.cancel"), "stop", "", 14)
	_cancel.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_cancel.pressed.connect(_on_cancel)
	bar.add_child(_cancel)
	_play = GoldButton.make(L10n.t("launcher.main.play_button"), Vector2(236, 60))
	_play.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_play.pressed.connect(_on_play)
	bar.add_child(_play)

	_verify_panel = VerifyFilesPanel.new()
	_verify_panel.repair_confirmed.connect(_start_repair_files)
	_verify_panel.cancel_requested.connect(_on_cancel)
	add_child(_verify_panel)
	_message = MessagePanel.new()
	_message.open_logs.connect(_open_logs)
	_message.closed.connect(func() -> void: updater.set_state("launcher", "unseen_error", ""))
	add_child(_message)

	var recheck := Timer.new()
	recheck.wait_time = RECHECK_SECONDS
	recheck.timeout.connect(_on_recheck)
	add_child(recheck)
	recheck.start()
	# lane ONLINECOUNT-1: the player count's refresh while the Play screen is visible (a timer that asks only when it may: _refresh_count)
	_count_timer = Timer.new()
	_count_timer.wait_time = OnlineCount.REFRESH_SECONDS
	_count_timer.timeout.connect(_count_tick)
	add_child(_count_timer)
	_count_timer.start()

	_show_channel()
	_show_progress(false)
	_set_update_state(_update_state, _update_tag, _status_text)
	_refresh_games()
	_refresh_mods()
	_show_screen("play")
	# Preserve the existing layout when a small window or UI scale needs scrolling.
	var scroll := ScrollContainer.new()
	scroll.set_anchors_preset(Control.PRESET_FULL_RECT)
	scroll.follow_focus = true
	var canvas := Control.new()
	canvas.custom_minimum_size = BASE_SIZE
	canvas.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	canvas.size_flags_vertical = Control.SIZE_EXPAND_FILL
	for child in get_children():
		if child is Control and child != _message:
			remove_child(child)
			canvas.add_child(child)
	add_child(scroll)
	scroll.add_child(canvas)
	move_child(_message, get_child_count() - 1)
	_play.grab_focus.call_deferred()


func _show_screen(key: String) -> void:
	_screen = key
	for k in _screens:
		_screens[k].visible = k == key
		_nav[k].set_pressed_no_signal(k == key)
	if key == "games" and _games_screen.folder == "":
		_games_screen.setup(Paths.default_games_dir(), aio.total_size() / 1e9, aio.chosen(), aio.choice == AioInstall.BFME1)
	if key == "play" and _play_screen != null:
		# back on the Play card: an old number is gone, and a stale one is asked for again (at most every MIN_GAP_SECONDS)
		_expire_count()
		if OnlineCount.stale(_count_text, float(Time.get_ticks_msec() - _count_got_ms) / 1000.0):
			_refresh_count()
	if key == "settings":
		_refresh_display()
		_settings.set_options(GameOptions.free_camera(), updater.auto_update(), aio.enabled(), _about())
		_settings.set_online_count(updater.show_online_count())
		_settings.set_replay_association(bool(updater.get_state("launcher", "replay_association", true)))


func _show_channel() -> void:
	_settings.set_channel(updater.channel())
	_refresh_head()


## the update state: "idle" (no check yet), "offline" (checks disabled), "checking", "updating" (an update downloads), "current" (up to date), "update" (`tag` is
## newer: the gold Update button), "failed" (the check did not finish: the status says why), "none" (no release of the channel)
func _set_update_state(state: String, tag := "", text := "") -> void:
	_update_state = state
	_update_tag = tag
	_status_text = Paths.scrub(text)
	_refresh_head()


func _set_macos_releases_link(visible: bool) -> void:
	if _macos_releases_link != null:
		_macos_releases_link.visible = visible


## the bottom bar's first line and the Play screen's chips: the update state, the version Play starts, the channel
func _refresh_head() -> void:
	if _status_head == null:
		return
	var ch := Semver.channel_name(updater.channel())  # lane NIGHTLY-1: "Stable" / "Nightly"
	var pv := Semver.display(_pv)
	var head: String = {"idle": L10n.t("launcher.main.head_idle"), "offline": L10n.t("launcher.main.head_offline"), "checking": L10n.t("launcher.main.head_checking"),
		"updating": L10n.t("launcher.main.head_updating"), "current": L10n.t("launcher.main.head_current"), "update": L10n.t("launcher.main.head_update"),
		"manual": L10n.t("launcher.main.head_manual"), "no_macos_entry": L10n.t("launcher.main.head_no_macos_entry"),
		"failed": L10n.t("launcher.main.head_failed"), "none": L10n.t("launcher.main.head_none", {"channel": ch.to_lower()})}[_update_state]
	var colour: Color = {"idle": Style.TEXT2, "offline": Style.TEXT2, "checking": Style.TEXT2, "updating": Style.ACCENT, "current": Style.GOOD, "update": Style.ACCENT,
		"manual": Style.TEXT2, "no_macos_entry": Style.TEXT2, "failed": Style.WARN, "none": Style.TEXT2}[_update_state]
	# lane NIGHTLY-2: the bottom bar's line names a nightly by its short name ("Nightly 0.4.12 is available"), the Play screen's chip by its
	# full one ("Nightly 0.4.12 (2026-10-23, evening) is available"); the bar's line has the full one as its tooltip
	var bar_head := head
	if _update_state == "update":
		var launcher_id := "launcher.main.head_launcher_available" if _launcher_only else "launcher.main.head_version_available"
		head = L10n.t(launcher_id, {"version": Semver.display(_update_tag)})
		bar_head = L10n.t(launcher_id, {"version": Semver.display_short(_update_tag)})
	if _update_state == "updating" and _update_tag != "":
		head = L10n.t("launcher.main.head_updating_to", {"version": Semver.display(_update_tag)})
		bar_head = L10n.t("launcher.main.head_updating_to", {"version": Semver.display_short(_update_tag)})
	if _quit_when_done:
		head = L10n.t("launcher.main.head_playing", {"version": pv})
		bar_head = L10n.t("launcher.main.head_playing", {"version": Semver.display_short(_pv)})
	_update.visible = _update_state == "update" and not _progress.visible
	if _update_state == "update":
		# lane NIGHTLY-2: the bar's short name ("Nightly 0.4.12"); the tooltip has the full one ("Nightly 0.4.12 (2026-10-23, evening)")
		_update.text = _update_label(Semver.display_short(_update_tag))
		_update.tooltip_text = _update_label(Semver.display(_update_tag))
	_status_head.text = bar_head
	_status_head.tooltip_text = head if head != bar_head else ""
	_status_tail.text = L10n.t("launcher.main.status_tail", {"version": pv, "channel": ch}) if _pv != "" else L10n.t("launcher.main.status_tail_none", {"channel": ch})
	W.set_chip(_play_screen.state_chip, "●  " + head, colour)
	W.set_chip(_play_screen.channel_chip, L10n.t("launcher.main.channel_chip", {"channel": ch}))
	W.set_chip(_play_screen.version_chip, pv if _pv != "" else L10n.t("launcher.main.not_installed"))
	_status.text = _status_text
	_status.tooltip_text = _status_text
	_status.visible = _status_text != "" and _status_text.trim_suffix(".") not in [head, bar_head]


## the Update button's / tooltip's text for `version`: the launcher's own update, a newer game version, or the first install
func _update_label(version: String) -> String:
	if _launcher_only:
		return L10n.t("launcher.main.update_launcher_to", {"version": version})
	return L10n.t("launcher.main.update_to" if _pv != "" else "launcher.main.install_version", {"version": version})


func _refresh_display() -> void:
	if _settings != null:
		var screen_size := DisplayServer.screen_get_size(get_window().current_screen)
		_settings.set_display(GameDisplay.read(screen_size))


func _on_display_changed(key: String, value: String) -> void:
	var why := GameDisplay.save(key, value)
	_refresh_display()
	if why != "":
		_show_error(L10n.t("launcher.main.setting_not_saved"), why)


func _on_free_camera(on: bool) -> void:
	var why := GameOptions.set_free_camera(on)
	if why != "":
		_show_error(L10n.t("launcher.main.setting_not_saved"), L10n.t("launcher.main.free_camera_not_saved", {"error": why}))
		_settings.set_options(GameOptions.free_camera(), updater.auto_update(), aio.enabled(), _about())
		return
	Log.line("LAUNCHER free camera %s (%s)" % ["on" if on else "off", GameOptions.options_path()])
	_set_status(L10n.t("launcher.main.free_camera_on" if on else "launcher.main.free_camera_off"))


func _on_auto_update(on: bool) -> void:
	updater.set_auto_update(on)
	if updater.macos_preview():
		_set_status(L10n.t("launcher.main.mac_check_on" if on else "launcher.main.mac_check_off"))
		return
	_set_status(L10n.t("launcher.main.auto_update_on" if on else "launcher.main.auto_update_off"))


func _on_online_count_toggled(on: bool) -> void:
	updater.set_show_online_count(on)
	Log.line("LAUNCHER online player count %s" % ["on" if on else "off"])
	if on:
		_set_status(L10n.t("launcher.main.online_count_on"))
		_refresh_count()
	else:
		_count_text = ""
		_play_screen.set_players("")
		_set_status(L10n.t("launcher.main.online_count_off"))


## lane ONLINECOUNT-1: the lobby's player count for the Play card. Asks only when the setting is on, the Play screen is the one in view and the window
## is not minimized, not offline / a demo / a development run without a named lobby, at most every MIN_GAP_SECONDS and never twice at once
## (its own thread and its own HttpFetch: an update download is never held up by it). Anything but a stats answer shows nothing.
func _expire_count() -> void:
	var shown := OnlineCount.chip_text(_count_text, float(Time.get_ticks_msec() - _count_got_ms) / 1000.0)
	if shown != _count_text:
		_count_text = ""
		_play_screen.set_players("")


func _count_tick() -> void:
	_expire_count()  # on any page: a number that is not renewed goes away
	_refresh_count()


func _refresh_count() -> void:
	if _social_handoff:
		return
	if _social != null and _social.is_signed_in():
		return
	if not updater.show_online_count() or _demo != "" or "--offline" in OS.get_cmdline_user_args() or _screen != "play":
		return
	var headless := DisplayServer.get_name() == "headless"
	if not OnlineCount.in_view(headless, not headless and get_window().mode == Window.MODE_MINIMIZED, not headless and get_window().has_focus()):
		return  # minimized or in the background: nothing is asked (the timer asks again, and the Play screen when it is back in view)
	if _count_thread != null and _count_thread.is_alive():
		return
	if Time.get_ticks_msec() - _count_asked_ms < int(OnlineCount.MIN_GAP_SECONDS * 1000.0) and _count_asked_ms != 0:
		return
	var url := updater.lobby_url()
	var why := updater.policy.set_lobby(url)
	if why != "":
		Log.line("LAUNCHER online player count not asked: " + why)
		_play_screen.set_players("")
		return
	if _count_thread != null:
		_count_thread.wait_to_finish()
	_count_asked_ms = maxi(Time.get_ticks_msec(), 1)
	var http := HttpFetch.new(updater.policy, info.version)
	_count_http = http
	_count_thread = Thread.new()
	_count_thread.start(func() -> void: _on_count.call_deferred(OnlineCount.fetch(http, url)))


func _on_count(r: Dictionary) -> void:
	if _social != null and _social.is_signed_in():
		return
	if not updater.show_online_count():
		_count_text = ""
		_play_screen.set_players("")  # it was switched off while the request was out: its answer is not shown
		return
	_count_text = r.text
	_count_got_ms = Time.get_ticks_msec()
	_play_screen.set_players(r.text)
	Log.line("LAUNCHER online player count: %s" % (r.text if r.ok else "not shown (%s)" % r.why))


func _on_aio_toggled(on: bool) -> void:
	aio.set_enabled(on)
	_set_status(L10n.t("launcher.main.aio_on" if on else "launcher.main.aio_off"))
	_refresh_games()


func _on_forget_folders() -> void:
	var why := Paths.forget_game_folders()
	if why != "":
		_show_error(L10n.t("launcher.main.folders_not_reset"), why)
		return
	_set_status(L10n.t("launcher.main.folders_reset"))
	_refresh_games()


func _open_download() -> void:
	_show_screen("games")


func _open_logs() -> void:
	_show_folder(OS.get_user_data_dir())


func _show_folder(path: String) -> void:
	var why := Paths.show_in_file_manager(path)
	if why != "":
		_show_error(L10n.t("launcher.main.folder_not_opened"), why)


func _about() -> String:
	return "OpenBFME Launcher %s (%s)" % [Semver.display(info.version), info.platform()]


func _show_error(title: String, text: String, operation := PlayerMessages.Operation.GENERAL) -> void:
	_set_status(text, operation)
	_message.show_error(title, text, _about(), operation)


## issue #49: the Game files screen's Language / Voices: the base download's languages (its size follows)
func _on_languages(text: String, voice: String) -> void:
	var why := aio.set_languages(text, voice)
	if why != "":
		_set_status(why)
	_games_screen.set_languages(aio.offered_languages(), aio.text_language, aio.voice_language)
	_games_screen.set_offer(aio.choice == AioInstall.BFME1, aio.total_size() / 1e9, aio.chosen())
	Log.line("LAUNCHER AIO language %s" % aio.language_text())


## the Game files screen's Download (resume: Resume of a paused one, which keeps its package)
func _start_aio(dir: String, resume := false) -> void:
	if dir == "" or not dir.is_absolute_path():
		_show_error(L10n.t("launcher.main.no_folder_title"), L10n.t("launcher.main.no_folder_text"))
		return
	if not resume:
		# community PR #36: the package the panel showed when Download was pressed (BFME1 only when that is the chosen game: install refuses
		# it otherwise)
		aio.choose(AioInstall.BFME1 if _games_screen.offer_bfme1 else AioInstall.GAMES)
	_aio_target = dir
	_start_job(_job_aio.bind(dir), "aio")


func _start_verify_files(key: String, dir: String) -> void:
	if _busy():
		return
	_verify_report = {}
	_verify_panel.begin()
	_start_job(_job_verify_files.bind(key, dir), "verify-files")


func _start_repair_files() -> void:
	if _busy() or _verify_report.is_empty():
		return
	if not updater.running().is_empty():
		_show_error(L10n.t("launcher.main.close_game_first"), L10n.t("launcher.main.close_game_repair"))
		return
	_verify_panel.begin(true)
	_start_job(_job_repair_files, "repair-files")


func _job_verify_files(key: String, dir: String) -> void:
	_after_verify_files.call_deferred(game_files.verify(key, dir))


func _job_repair_files() -> void:
	_after_verify_files.call_deferred(game_files.repair(_verify_report, true))


func _after_verify_files(result: Dictionary) -> void:
	if not result.ok:
		var repairing := _job_kind == "repair-files"
		_verify_report = {}
		_verify_panel.visible = false
		_finish(L10n.t("launcher.main.verify_stopped"), result.error)
		_show_error(L10n.t("launcher.main.repair_failed") if repairing else L10n.t("launcher.main.verify_failed"), result.error)
		return
	_verify_report = result
	_finish(VerifyFilesPanel.summary(result))
	_verify_panel.show_report(result)


## Pause stops the transfer and keeps the partial file (Resume continues it with a Range request); while paused it reads "Resume".
func _on_pause() -> void:
	if _busy():
		_stop = "pause"
		if _job_kind == "aio":
			aio.request_stop("pause")
		elif _job_kind == "mod":
			mods.request_stop("pause")
		else:
			updater.http.cancelled = true
	elif _paused == "aio":
		_start_aio(_aio_target, true)
	elif _paused == "mod":
		_start_mod(_mod_job[0], _mod_job[1])
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
		elif _job_kind == "mod":
			mods.request_stop("cancel")
		else:
			updater.http.cancelled = true
		return
	if _paused == "aio":
		var n := aio.remove_partials(_aio_target)
		_set_status(L10n.t("launcher.main.aio_cancelled", {"removed": n}))
	elif _paused == "mod":
		_set_status(L10n.t("launcher.main.mod_cancelled"))
		_refresh_mods()
	elif _paused in ["game", "launcher"]:
		_remove_partials(_paused_assets)
		_set_status(L10n.t("launcher.main.launcher_cancelled" if _paused == "launcher" else "launcher.main.update_cancelled"))
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
	return updater.asset_names(mm.manifest, kinds) if mm.ok else []


static func _folders_text(folders: Dictionary) -> String:
	var parts := PackedStringArray()
	for k in folders:
		parts.append("%s in %s" % [String(k).to_upper(), folders[k]])
	return ", ".join(parts)


func _set_warning(text: String) -> void:
	_warning.text = L10n.t("launcher.main.warning", {"text": text}) if text != "" else ""
	_banner.visible = text != ""


func _set_status(text: String, operation := -1) -> void:
	Log.line("LAUNCHER " + text)
	_status_text = PlayerMessages.status_text(text, operation) if operation != -1 else Paths.scrub(text)
	_refresh_head()


func _refresh_installed() -> void:
	if _demo != "":
		return
	_pv = updater.play_version()
	_settings.set_versions(updater.playable(), _pv, _older)
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
		_play_screen.set_notes(ReleaseNotes.title_of(_pv) + (" · " + when if when != "" else ""), mine[0].notes, L10n.t("launcher.main.no_release_notes"))
	elif not chan.is_empty():
		_play_screen.set_notes(L10n.t("launcher.main.newest", {"title": ReleaseNotes.title_of(chan[0].tag)}), chan[0].notes, L10n.t("launcher.main.no_release_notes"))
	else:
		_play_screen.set_notes("", "", L10n.t("launcher.main.notes_placeholder"))


func _refresh_games() -> void:
	_show_games(Paths.game_folders())


func _show_games(g: Dictionary, demo := false) -> void:
	_games = g
	var where := ""
	if g.found:
		var r := Paths.short(g.rotwk)
		var b := Paths.short(g.bfme2)
		var common := r.get_base_dir()
		where = L10n.t("launcher.main.games_in", {"folder": common}) if common == b.get_base_dir() and common != "" else L10n.t("launcher.main.games_two", {"first": r, "second": b})
	var bfme1 := Paths.bfme1_folder()
	g["bfme1"] = bfme1
	var use_bfme1: bool = updater.get_state("launcher", "game", "bfme2") == "bfme1"
	# community PR #36: BFME1 is the player's own install; its download is optional (Game files, from BFME Arena, a third party)
	_play_screen.missing_games.text = L10n.t("launcher.main.bfme1_missing", {"folder": Paths.short(Paths.default_games_dir().path_join("BFME1"))}) if use_bfme1 else L10n.t("launcher.play_screen.not_found")
	_play_screen.set_games(not bfme1.is_empty() if use_bfme1 else g.found, aio.enabled(), Paths.short(bfme1) if use_bfme1 else where)
	_games_screen.set_folders(g, demo)
	_set_buttons()


func _busy() -> bool:
	return _thread != null and _thread.is_alive()


## Play waits only for a job that changes the version it starts (a reinstall of it) or downloads the games; a background update of a
## newer version does not stop it (that version is used from the next Play on)
func _play_blocked() -> bool:
	# MACFIX-1 (issue #46): the running job first; the update lock's state is a process start (the bundle swap helper)
	# lane MODS-3: also while a mod downloads (its folder is half old, half new until the download is checked)
	if _busy() and (_job_kind in ["aio", "mod", "verify-files", "repair-files"] or (_job_target != "" and _job_target == _pv)):
		return true
	return updater.macos_updates_enabled() and (updater.macos_transaction or MacOSUpdate.transaction_locked(updater))


func _set_buttons() -> void:
	if _play == null:
		return
	var busy := _busy()
	var downloading := _progress.visible
	_check.visible = not downloading
	_check.disabled = busy
	_pause.visible = downloading
	if _job_kind in ["verify-files", "repair-files"]:
		_pause.visible = false
	_cancel.visible = downloading
	_update.visible = _update_state == "update" and not downloading  # LAUNCHERUI-1: no room beside Pause and Cancel, and it is disabled meanwhile
	_update.disabled = busy or _latest.is_empty()
	_play.disabled = _play_blocked()  # MACPLAY-1: never a dimmed Play with no word why: _on_play says what is missing
	_play_screen.games_button.disabled = busy or _paused != ""
	_settings.set_busy(busy)
	var aio_running := busy and _job_kind == "aio"
	# community PR #36: the Game files download follows the chosen game (BFME1: the optional BFME1 package), except while a download
	# runs or is paused: that one keeps its package (Resume continues what was started)
	if not aio_running and _paused != "aio" and not _demo_bfme1:
		aio.choose(AioInstall.BFME1 if aio.bfme1_chosen() else AioInstall.GAMES)
	if _games_screen.offer_bfme1 != (aio.choice == AioInstall.BFME1):
		_games_screen.set_offer(aio.choice == AioInstall.BFME1, aio.total_size() / 1e9, aio.chosen())
	_games_screen.set_download_state(aio_running, _paused == "aio", busy and not aio_running, aio.enabled())
	var mod_running := busy and _job_kind == "mod"
	_mods.set_state(mod_running, _paused == "mod", busy and not mod_running)


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
	_progress_lock.lock()
	_progress_next = []
	_job_ui_stop = _ui_stop
	_progress_lock.unlock()
	updater.progress = _queue_progress
	_thread = Thread.new()
	_thread.start(job)
	if _progress.visible:
		_show_progress(true)   # a resumed download: Pause again, not Resume (its progress no longer calls this)
	else:
		_set_buttons()


## `err`: the job failed (its message is shown in the dialog by the caller)
func _finish(text: String, err := "", state := "", tag := "", operation := PlayerMessages.Operation.DOWNLOAD) -> void:
	if _thread != null:
		_thread.wait_to_finish()
		_thread = null
	# Every check completion, including a cached/no-download result, replaces its transient state with its message.
	if state == "" and _job_kind == "check":
		state = "failed" if err != "" else "current"
	if state != "":
		_set_update_state("failed" if err != "" else state, tag)
	_set_status(text, operation if err != "" else -1)
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
	_pause.text = L10n.t("launcher.main.resume") if _paused != "" else L10n.t("launcher.main.pause")
	_pause.icon = VectorIcon.make("resume" if _paused != "" else "pause", 14)
	if not on:
		_bar_title.text = ""
		_bar_label.text = ""
	_set_buttons()


## MACFIX-1 (issue #46): the worker thread's progress callback. A download reports every chunk it reads (thousands a second on a fast
## line); each used to be a deferred _on_progress call, which the window handles more slowly than they came: Godot's message queue
## runs the calls that arrive while it runs, so the main thread never left it, never drew a frame again ("Checking File 2 of 612" for
## the whole 8.6 GB download on the Mac of issue #46) and the queue overflowed. Status lines (no byte counts) still go in order; byte
## progress only replaces the newest value, which _process shows once a frame. A --ui-stop test's matching line is passed on at once.
func _queue_progress(text: String, done: int, total: int) -> void:
	var now := total <= 0
	_progress_lock.lock()
	if not now and _job_ui_stop != "" and done < total and text.contains(_job_ui_stop.get_slice("@", 1)):
		_job_ui_stop = ""
		now = true
	if now:
		_progress_next = []
	else:
		_progress_next = [text, done, total]
	_progress_lock.unlock()
	if now:
		_on_progress.call_deferred(text, done, total)


func _process(_delta: float) -> void:
	_progress_lock.lock()
	var p := _progress_next
	_progress_next = []
	_progress_lock.unlock()
	if not p.is_empty() and _job_kind != "":
		_on_progress(p[0], p[1], p[2])


func _on_progress(text: String, done: int, total: int) -> void:
	if _job_kind == "":
		return
	if _job_kind in ["verify-files", "repair-files"]:
		_verify_panel.set_progress(text, done, total)
		return
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
		elif _job_kind == "mod":
			_mods.set_progress(_bar_title.text + "  ·  " + _bar_label.text, done, total)
		elif _job_kind in ["check", "install"] and _is_downloading(text) and _update_state != "updating":
			var m := RegEx.create_from_string("-(v\\d+\\.\\d+\\.\\d+(?:-(?:alpha|nightly|preview)\\.\\d+)?)-").search(text)
			_set_update_state("updating", m.get_string(1) if m != null else _update_tag)
		if not _progress.visible:
			_show_progress(true)   # MACFIX-1 (issue #46): not every chunk: it refreshes every button (_set_buttons, _play_blocked)
	else:
		_bar_title.text = Paths.scrub(text)
		_set_status(text)


# ---- jobs (worker thread) ----------------------------------------------------------------------------------------------------------------

func _job_check(auto_install: bool) -> void:
	_set_macos_releases_link.call_deferred(false)
	var r := updater.fetch_releases()
	if not r.ok:
		_after_check.call_deferred({}, [], L10n.t("launcher.main.updates_skipped", {"error": r.error}), "failed", "")
		return
	_releases = r.releases
	var chan := updater.in_channel(r.releases, updater.channel())
	if chan.is_empty():
		_after_check.call_deferred({}, chan, L10n.t("launcher.main.no_release_status", {"channel": Semver.channel_name(updater.channel()).to_lower()}), "none", "")
		return
	if updater.macos_preview():
		var notice := updater.macos_preview_notice(chan[0])
		_after_check.call_deferred({}, chan, notice.message if notice.ok else L10n.t("launcher.main.update_check_rejected", {"error": notice.error}),
			"manual" if notice.ok else "failed", "")
		return
	var mm := updater.release_manifest(chan[0])
	if not mm.ok:
		var rejected := L10n.t("launcher.main.update_rejected", {"error": mm.error})
		_after_check.call_deferred({}, chan, rejected, "failed", rejected)
		return
	if updater.macos_updates_enabled() and mm.get("no_entry", false):
		_set_macos_releases_link.call_deferred(true)
		_after_check.call_deferred({}, chan,
			L10n.t("launcher.main.no_mac_package"),
			"no_macos_entry", "")
		return
	if updater.translocated != "":
		# MACPLAY-1: a translocated copy installs nothing; the notice above the screens and Play say what to do
		_after_check.call_deferred({}, chan, L10n.t("launcher.main.translocated_off"), "failed", "")
		return
	var current := updater.newest_installed()
	var newer := current == "" or Semver.compare(chan[0].tag, current) > 0
	var msg := L10n.t("launcher.main.up_to_date_status") if not newer else L10n.t("launcher.main.version_available_status", {"version": chan[0].tag})
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
			msg = L10n.t("launcher.main.mac_staged_restart_launcher", {"version": i.version}) \
				if i.get("swap", false) else L10n.t("launcher.main.installed", {"version": i.version})
		elif _stop != "":
			msg = _stopped("game", chan[0], false)
		else:
			msg = L10n.t("launcher.main.update_rejected", {"error": i.error})
			err = msg
		newer = not i.ok
	var s := {"ok": true, "staged": ""}
	if _stop == "" and auto_install:  # lane UI-5: the launcher's own update follows the "update automatically" setting too
		_job_assets = _assets_of(chan[0], ["launcher"])
		s = updater.stage_self_update(chan[0])
	if not s.ok and _stop != "":
		msg = _stopped("launcher", chan[0], false)  # Sol r1: a stopped launcher download was reported as rejected
	elif not s.ok:
		msg += " " + L10n.t("launcher.main.launcher_update_rejected", {"error": s.error})
		err = (err + " " if err != "" else "") + L10n.t("launcher.main.launcher_update_rejected", {"error": s.error})
	elif s.staged != "" and not updater.macos_updates_enabled():
		msg += " " + L10n.t("launcher.main.launcher_ready", {"version": s.staged})
	elif updater.mac_launcher_notice(chan[0]) != "" and str(s.get("why", "")) == updater.mac_launcher_notice(chan[0]):
		msg += " " + s.why  # MACPLAY-1: a Mac launcher without its package beside it says that a newer launcher is out
		_set_macos_releases_link.call_deferred(true)
	# lane UI-5 (Sol r1): updating by itself off, the launcher's own update is offered on its own too (a launcher older than the
	# channel's newest release while the game is up to date said "Up to date" with no Update button)
	var launcher_only := not newer and not auto_install and _stop == "" and updater.launcher_update_available(chan[0])
	if launcher_only:
		msg = L10n.t("launcher.main.launcher_available_status", {"version": chan[0].tag})
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
		return L10n.t("launcher.main.launcher_paused" if kind == "launcher" else "launcher.main.game_paused")
	_remove_partials(_job_assets)
	return L10n.t("launcher.main.launcher_cancelled" if kind == "launcher" else "launcher.main.update_cancelled")


func _after_check(latest: Dictionary, chan: Array, msg: String, state: String, err: String, launcher_only := false) -> void:
	_latest = latest
	_launcher_only = launcher_only
	_older = []
	for r in chan:
		if not updater.macos_preview():
			_older.append(r.tag)
	# a check that did not finish is a failure too while the game runs (the player would otherwise never see it)
	var problem := err if err != "" else (msg if state == "failed" else "")
	var operation := PlayerMessages.Operation.UPDATE_CHECK if state == "failed" else PlayerMessages.Operation.DOWNLOAD
	_finish(msg, problem, state, chan[0].tag if not chan.is_empty() else "", operation)
	if problem != "":
		_message.show_error(L10n.t("launcher.main.update_not_installed"), problem, _about(), operation)


func _job_install(release: Dictionary, user_pick: bool) -> void:
	_job_assets = _assets_of(release, ["game"])
	var i := updater.install(release, user_pick)
	if i.ok:
		var message := L10n.t("launcher.main.mac_staged_restart_both", {"version": i.version}) \
			if i.get("swap", false) else L10n.t("launcher.main.installed", {"version": i.version})
		_after_install.call_deferred(message, "", true)
	elif _stop != "":
		_after_install.call_deferred(_stopped("game", release, user_pick), "")
	else:
		_after_install.call_deferred(L10n.t("launcher.main.not_installed_status", {"error": i.error}), i.error)


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
				_after_install.call_deferred(L10n.t("launcher.main.not_installed_status", {"error": i.error}), i.error)
			return
		msg = L10n.t("launcher.main.mac_staged_restart_launcher", {"version": i.version}) \
			if i.get("swap", false) else L10n.t("launcher.main.installed", {"version": i.version})
	_job_assets = _assets_of(release, ["launcher"])
	var s := updater.stage_self_update(release)
	if not s.ok and _stop != "":
		_after_install.call_deferred(_stopped("launcher", release, false), "", true)
	elif not s.ok:
		var rejected := L10n.t("launcher.main.launcher_update_rejected", {"error": s.error})
		_after_install.call_deferred((msg + " " if msg != "" else "") + rejected, rejected)
	else:
		if s.staged != "" and not updater.macos_updates_enabled():
			msg += (" " if msg != "" else "") + L10n.t("launcher.main.launcher_ready", {"version": s.staged})
		elif s.staged != "" and msg == "":
			msg = L10n.t("launcher.main.mac_staged_restart_both", {"version": s.staged})
		_after_install.call_deferred(msg if msg != "" else L10n.t("launcher.main.up_to_date_status"), "", true)


## Resume of a paused launcher download: stage the launcher of `release` (Updater.stage_self_update resumes its partial file)
func _job_stage(release: Dictionary) -> void:
	_job_assets = _assets_of(release, ["launcher"])
	var s := updater.stage_self_update(release)
	if s.ok:
		if updater.macos_updates_enabled():
			_after_install.call_deferred(L10n.t("launcher.main.mac_staged_restart_launcher", {"version": s.staged}) \
				if s.staged != "" else L10n.t("launcher.main.mac_package_update", {"why": str(s.get("why", ""))}), "", s.staged != "")
		else:
			_after_install.call_deferred(L10n.t("launcher.main.launcher_ready", {"version": s.staged}) if s.staged != "" \
				else L10n.t("launcher.main.launcher_update_note", {"why": str(s.get("why", ""))}), "", s.staged != "")
	elif _stop != "":
		_after_install.call_deferred(_stopped("launcher", release, false), "", true)
	else:
		var rejected := L10n.t("launcher.main.launcher_update_rejected", {"error": s.error})
		_after_install.call_deferred(rejected, rejected)


## `done`: the job ended with the game / launcher installed or the launcher's download cancelled (a state of "current", unless it is paused)
func _after_install(msg: String, err: String, done := false) -> void:
	var state := "failed" if err != "" else ""
	if err == "" and _paused == "" and done:
		_latest = {}
		_launcher_only = false
		state = "current"
	_finish(msg, err, state)
	if err != "":
		_message.show_error(L10n.t("launcher.main.not_installed_title"), err, _about(), PlayerMessages.Operation.DOWNLOAD)


func _job_aio(dir: String) -> void:
	var r := aio.install(dir, true)
	_after_aio.call_deferred(r)


func _after_aio(r: Dictionary) -> void:
	if r.ok and aio.choice == AioInstall.BFME1:
		_finish(L10n.t("launcher.main.bfme1_downloaded", {"folders": _folders_text(r.folders)}))
		_refresh_games()
	elif r.ok:
		_finish(L10n.t("launcher.main.games_downloaded", {"folders": _folders_text(r.folders)}))
		_refresh_games()
	elif r.kind == AioInstall.STOPPED and aio.stop_request == "pause":
		_paused = "aio"
		_finish(L10n.t("launcher.main.download_is", {"state": r.error}))
	else:
		if r.kind == AioInstall.STOPPED:
			aio.remove_partials(_aio_target)
			_finish(L10n.t("launcher.main.download_was", {"state": r.error}))
			return
		if r.kind == AioInstall.NETWORK:
			_paused = "aio"  # a broken transfer can be resumed
		_finish(L10n.t("launcher.main.games_not_downloaded_status", {"error": r.error}), r.error)
		_message.show_error(L10n.t("launcher.main.games_not_downloaded_title"), r.error, _about(), PlayerMessages.Operation.DOWNLOAD)


# ---- mods (lane MODS-3) ------------------------------------------------------------------------------------------------------------------

## the Mods screen's installed mods, the pick, the unfinished downloads, and the cached list (no network)
func _refresh_mods() -> void:
	if _demo != "":
		return
	_mods.set_installed(mods.installed(), mods.active(), mods.partials())
	_mods.set_asset_packs(AssetPacks.list()) # lane SDK-1
	if _mods._view.is_empty():
		var c := mods.cached()
		if not c.is_empty():
			_mods.set_catalogue(ModCatalog.group(c.items, c.fetched, c.get("problems", {})), c.time, true)


func _job_mods_list(force: bool) -> void:
	var r := mods.catalogue(force)
	_after_mods_list.call_deferred(r)


func _after_mods_list(r: Dictionary) -> void:
	if r.ok:
		_finish(L10n.t("launcher.main.mod_list_count", {"packages": r.view.packages.size()}))
		_mods.set_catalogue(r.view, r.time, r.from_cache)
		_refresh_mods()
		return
	_mods.set_note(L10n.t("launcher.main.mod_list_note", {"error": r.error}))
	_finish(L10n.t("launcher.main.mod_list_status", {"error": r.error}))
	if r.kind != AioInstall.STOPPED:
		_message.show_error(L10n.t("launcher.main.mod_list_title"), r.error, _about())


## Install / Update / Apply / Continue: the question before the first mod download (the consent), then the download
func _on_mod_install(guid: String, enhancements: Array) -> void:
	if mods.consented():
		_start_mod(guid, enhancements)
		return
	var name := guid
	var bytes := 0
	for row in _mods._view.get("packages", []) + _mods._view.get("loose", []):
		if row.guid == guid:
			name = "%s %s" % [row.name, row.version]
			bytes = int(row.get("bytes", row.size))
			for e in row.enhancements:
				if e.guid in enhancements:
					bytes += int(e.get("bytes", e.size))
	_message.ask(L10n.t("launcher.main.mod_consent_title"), mods.consent_text(name, bytes), L10n.t("launcher.main.download"), _start_mod.bind(guid, enhancements))


func _start_mod(guid: String, enhancements: Array) -> void:
	_mod_job = [guid, enhancements]
	_start_job(_job_mod.bind(guid, enhancements), "mod")


func _job_mod(guid: String, enhancements: Array) -> void:
	var r := mods.install(guid, enhancements, true)
	_after_mod.call_deferred(r, guid, enhancements)


func _after_mod(r: Dictionary, guid: String, enhancements: Array) -> void:
	if r.ok:
		# one click: the mod just installed is the one Play starts (the player picks the original game again under Play with)
		var why := mods.set_active(guid, enhancements)
		var text := L10n.t("launcher.main.mod_installed_picked", {"name": r.summary.name, "version": r.summary.version}) if why == "" else \
			L10n.t("launcher.main.mod_installed_not_picked", {"name": r.summary.name, "version": r.summary.version, "why": why})
		if r.status == ModCatalog.NEEDS:
			text += " " + L10n.t("launcher.main.mod_needs_engine")
		_finish(text)
	elif r.kind == AioInstall.STOPPED and mods.stop_request == "pause":
		_paused = "mod"
		_finish(L10n.t("launcher.main.mod_download_is", {"state": r.error}))
	elif r.kind == AioInstall.STOPPED:
		_finish(L10n.t("launcher.main.mod_download_was", {"state": r.error}))
	else:
		if r.kind == AioInstall.NETWORK:
			_paused = "mod"   # a broken transfer can be resumed
		_finish(L10n.t("launcher.main.mod_not_installed_status", {"error": r.error}))
		_message.show_error(L10n.t("launcher.main.mod_not_installed_title"), r.error, _about())
	_refresh_mods()


func _on_mod_remove(guid: String) -> void:
	var name: String = mods.record(guid).get("name", mods.partials().get(guid, guid))
	_message.ask(L10n.t("launcher.main.mod_remove_title", {"name": name}), L10n.t("launcher.main.mod_remove_text"),
		L10n.t("launcher.main.remove"), func() -> void:
			if _busy():
				return
			var why := mods.uninstall(guid)
			if why != "":
				_show_error(L10n.t("launcher.main.mod_not_removed"), why)
			else:
				_set_status(L10n.t("launcher.main.mod_removed", {"name": name}))
				if _paused == "mod" and not _mod_job.is_empty() and _mod_job[0] == guid:
					_paused = ""
					_show_progress(false)
			_refresh_mods())


func _on_mod_pick(guid: String) -> void:
	var why := mods.set_active(guid, mods.record(guid).get("enhancements", []) if guid != "" else [])
	if why != "":
		_show_error(L10n.t("launcher.main.mod_not_picked"), why)
	elif guid != "":
		_set_status(L10n.t("launcher.main.play_starts", {"name": mods.record(guid).get("name", guid)}))
	else:
		_set_status(L10n.t("launcher.main.play_starts_original"))
	_refresh_mods()


func _on_channel(ch: String) -> void:
	updater.set_channel(ch)
	_show_channel()
	_show_notes()
	_start_job(_job_check.bind(updater.auto_update()), "check")


func _on_use(v: String) -> void:
	updater.set_selected(v)
	_set_status(L10n.t("launcher.main.play_starts", {"name": v}))
	_refresh_installed()


func _on_install_pick(tag: String) -> void:
	var rel = _releases.filter(func(x): return x.tag == tag)
	if rel.is_empty():
		return
	var newest := updater.newest_installed()
	if newest != "" and Semver.compare(tag, newest) < 0:
		_message.ask(L10n.t("launcher.main.install_older_title"), L10n.t("launcher.main.install_older_text", {"tag": tag, "installed": newest}),
			L10n.t("launcher.main.install"), func() -> void: _start_job(_job_install.bind(rel[0], true), "install", tag))
	else:
		_start_job(_job_install.bind(rel[0], true), "install", tag)


func _pick_replay(save := false) -> void:
	if _play_blocked():
		return
	var dialog := FileDialog.new()
	dialog.title = L10n.t("launcher.main.open_save_title" if save else "launcher.main.open_replay_title")
	dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	dialog.access = FileDialog.ACCESS_FILESYSTEM
	dialog.filters = PackedStringArray(["*.obfmesave ; " + L10n.t("launcher.main.save_filter")] if save else ["*.replay ; " + L10n.t("launcher.main.replay_filter")])
	dialog.use_native_dialog = true
	add_child(dialog)
	dialog.file_selected.connect(func(path: String):
		dialog.queue_free()
		_open_replay_path(path))
	dialog.canceled.connect(dialog.queue_free)
	dialog.popup_centered_ratio(0.7)


func _replay_files_dropped(files: PackedStringArray) -> void:
	if files.size() != 1 or not preload("res://scripts/core/replay_open.gd").is_recording(files[0]):
		_show_error(L10n.t("launcher.main.file_refused"), L10n.t("launcher.main.drop_one_file"))
		return
	_open_replay_path(files[0])


func _open_replay_path(path: String) -> void:
	if _play_blocked():
		return
	var result := updater.open_recording(path)
	if not result.ok:
		_show_error(L10n.t("launcher.main.file_refused"), result.error)
		return
	if _thread != null:
		_quit_when_done = true
		_set_status(L10n.t("launcher.main.opening_file"))
		if DisplayServer.get_name() != "headless":
			get_window().mode = Window.MODE_MINIMIZED
	else:
		get_tree().quit(0)


## lane L10N-1: whether `text` is the updater's "Downloading <file>" progress line (launcher.updater.downloading) in the current language
static func _is_downloading(text: String) -> bool:
	var parts := L10n.t("launcher.updater.downloading", {"name": "\u0001"}).split("\u0001")
	return parts.size() == 2 and text.length() > parts[0].length() + parts[1].length() and text.begins_with(parts[0]) and text.ends_with(parts[1])


## lane L10N-1: the language of the launcher's own text, chosen once at start (Settings says a change applies at the next start): the
## player's pick in Settings (launcher/language in the state; --language=<code> in a test run), else Automatic: the retail install's
## language (its lang/<Language>.big) when OpenBFME has a translation of it Automatic may use, else English. Retail text is the game's.
func _choose_language(args: PackedStringArray) -> void:
	var pick := str(updater.get_state("launcher", "language", ""))
	for a in args:
		if a.begins_with("--language=") and info.tests_allowed():
			pick = a.substr(11)
	if pick != "" and L10n.language_name(pick) == "":
		Log.line("LAUNCHER text language: no catalog for the picked '%s' (back to Automatic)" % pick)
		pick = ""
	var auto := _automatic_language()
	var code: String = pick if pick != "" else auto.code
	if code != "":
		L10n.set_language(code)
	Log.line("LAUNCHER text language %s (%s)" % [L10n.language(), "picked" if pick != "" else "Automatic: " + auto.why])


## Automatic's choice: {code ("" = English), why (for the log), label (the Settings item), note (what it found, under the setting)}
func _automatic_language() -> Dictionary:
	var rotwk: String = Paths.game_folders().rotwk
	var found := L10n.install_language(rotwk)
	var english := L10n.t("launcher.settings.language_automatic", {"language": L10n.language_name(L10n.SOURCE)})
	if found.code == "":
		return {"code": "", "why": "the game's language is not known (%s)" % found.error, "label": english,
			"note": L10n.t("launcher.settings.language_automatic_unknown")}
	if not L10n.automatic_ok(found.code):
		var name := L10n.language_name(found.code)
		return {"code": "", "why": "no %s translation Automatic may use" % found.name, "label": english,
			"note": L10n.t("launcher.settings.language_automatic_missing", {"language": name if name != "" else found.name})}
	return {"code": found.code, "why": "the game is %s" % found.name,
		"label": L10n.t("launcher.settings.language_automatic", {"language": L10n.language_name(found.code)}),
		"note": L10n.t("launcher.settings.language_automatic_found", {"language": L10n.language_name(found.code)})}


## the Settings Language choices: Automatic, then every catalog language (a sample marked as such)
func _language_choices(automatic: Dictionary) -> Array:
	var out := [{"code": "", "label": automatic.label}]
	for l in L10n.languages():
		var label: String = l.name if l.status != "sample" else L10n.t("launcher.settings.language_sample", {"language": l.name})
		out.append({"code": l.code, "label": label})
	return out


func _on_language_picked(code: String) -> void:
	updater.set_state("launcher", "language", code)
	Log.line("LAUNCHER text language set to %s (applies at the next start)" % (code if code != "" else "Automatic"))


func _set_replay_association(on: bool) -> bool:
	updater.set_state("launcher", "replay_association", on)
	for kind in ["replay", "save"]:
		var key: String = kind + "_previous_handler"
		var previous := str(updater.get_state("launcher", key, ""))
		var result := preload("res://scripts/core/replay_association.gd").set_enabled(OS.get_executable_path(), on, previous, kind)
		if not result.ok:
			Log.line("LAUNCHER file association: " + result.error)
			if _settings != null:
				_show_error(L10n.t("launcher.main.association_title"), result.error)
			return false
		if result.has("previous"):
			updater.set_state("launcher", key, result.previous)
	updater.set_state("launcher", "replay_association_exe", OS.get_executable_path() if on else "")
	updater.set_state("launcher", "save_association_registered", on)
	return true


## Play: while an update downloads in the background, the game starts anyway; the new version is used from the next Play on.
func _on_play() -> void:
	var v := updater.play_version()
	if _play_blocked():
		return
	if v == "":
		_no_game()
		return
	var p := updater.play(v)
	if p.ok:
		# The worker may have ended while play() checked the game; its deferred completion still has to show any failure.
		if _thread != null:
			_quit_when_done = true
			_set_status(L10n.t("launcher.main.playing_update_continues", {"version": v}))
			if DisplayServer.get_name() != "headless":
				get_window().mode = Window.MODE_MINIMIZED
			return
		get_tree().quit(0)
		return
	_set_status(p.error, PlayerMessages.Operation.GENERAL)
	if p.get("modified", false):
		# a changed installed version is never started; offer to download it again (a user pick: the same version may be reinstalled)
		var rel = _releases.filter(func(x): return x.tag == v)
		if rel.is_empty():
			_show_error(L10n.t("launcher.main.game_did_not_start"), L10n.t("launcher.main.cannot_download_now", {"error": p.error}))
			return
		# MACFIX-1 (issue #47): what was found, by name (Finder / iCloud metadata is not a change: MacOSUpdate._codesign)
		_message.ask(L10n.t("launcher.main.download_again_title"), L10n.t("launcher.main.download_again_text", {"version": v, "change": p.get("change", "")}),
			L10n.t("launcher.main.download"),
			func() -> void: _start_job(_job_install.bind(rel[0], true), "install", v))
		return
	_show_error(L10n.t("launcher.main.game_did_not_start"), p.error)


## MACPLAY-1: Play with no game to start (the v0.4.6 Mac preview's Play did nothing): say what is missing and what to do, and start the
## download of the channel's newest release when that is the remedy
func _no_game() -> void:
	var why := updater.no_game_reason()
	Log.line("LAUNCHER play: no game to start: " + why)
	if updater.translocated == "" and not _busy() and _problem == "":
		var chan := updater.in_channel(_releases, updater.channel())
		if not chan.is_empty():
			_set_status(L10n.t("launcher.main.no_game_downloading", {"tag": chan[0].tag}))
			_start_job(_job_install.bind(chan[0], false), "install", chan[0].tag)
			_message.show_error(L10n.t("launcher.main.downloading_game_title"), L10n.t("launcher.main.downloading_tag_now", {"reason": why, "tag": chan[0].tag}), _about())
			return
	elif _busy() and _job_kind in ["check", "install"]:
		why = L10n.t("launcher.main.downloading_now", {"reason": why})
	_show_error(L10n.t("launcher.main.no_game_title"), why)


func _init_social() -> void:
	_social = SocialClient.new()
	add_child(_social)
	var local: String = updater.policy.test_origin if info.tests_allowed() else ""
	var why := _social.configure(updater.lobby_url(), info.version, local)
	var oauth := OAuthAccount.new()
	add_child(oauth)
	var oauth_error := oauth.configure(updater.lobby_url(), local)
	if why == "":
		why = oauth_error
	if "--offline" in OS.get_cmdline_user_args() or _demo != "":
		why = L10n.t("launcher.main.signin_disabled")
	_multiplayer.bind_client(_social, self, why)
	_multiplayer.problem_reported.connect(func(detail: String, operation: int) -> void:
		_message.show_error(L10n.t("launcher.main.signin_failed") if operation == PlayerMessages.Operation.SIGN_IN else L10n.t("launcher.main.lobby_failed"),
			detail, _about(), operation))
	_multiplayer.bind_oauth(oauth)
	_multiplayer.join_invite.connect(_join_social_invite)
	_social.changed.connect(_social_stats)
	_social.connection_changed.connect(func(connected: bool) -> void:
		_count_text = ""
		_play_screen.set_players("")
		if not connected:
			_refresh_count())


func _social_stats() -> void:
	if not _social.is_signed_in() or not updater.show_online_count():
		return
	var stats: Dictionary = _social.snapshot.get("stats", {})
	var parsed := OnlineCount.parse(200, JSON.stringify(stats).to_utf8_buffer())
	_count_text = parsed.text
	_count_got_ms = Time.get_ticks_msec()
	_play_screen.set_players(parsed.text)


func _join_social_invite(invite: Dictionary) -> void:
	if not _social.is_signed_in() or _social.busy or _multiplayer.oauth_in_progress():
		_multiplayer.show_problem(L10n.t("launcher.main.invite_finish_signin"))
		return
	if _play_blocked():
		_multiplayer.show_problem(L10n.t("launcher.main.invite_wait_install"))
		return
	if str(invite.get("to", {}).get("pid", "")) != str(_social.me.get("pid", "")):
		_multiplayer.show_problem(L10n.t("launcher.main.invite_not_yours"))
		return
	if int(invite.get("expires", 0)) <= int(Time.get_unix_time_from_system()):
		_multiplayer.show_problem(L10n.t("launcher.main.invite_expired"))
		return
	var version := updater.play_version()
	if version.is_empty():
		_multiplayer.show_problem(L10n.t("launcher.main.invite_install_game"))
		return
	var result := SocialLaunch.play(updater, version, str(invite.get("id", "")), _social.lobby_url, _social.token)
	if not result.ok:
		_multiplayer.show_problem(result.error)
		return
	# The game owns presence and the accepted slot from here; this launcher must not keep polling it.
	_social_handoff = true
	_social.sign_out()
	if _busy():
		_quit_when_done = true
		_set_status(L10n.t("launcher.main.opening_invite"))
		if DisplayServer.get_name() != "headless":
			get_window().mode = Window.MODE_MINIMIZED
	else:
		get_tree().quit(0)


func _exit_tree() -> void:
	_social_handoff = true
	if _social != null:
		_social.sign_out()
	# MACPLAY-1: the online-count request is cancelled and its thread joined before the window goes. It was never joined: a launcher that
	# closed while the lobby had not answered (Play started the game at once) freed itself under the running thread, which then called
	# back into it (the owner's Mac, 2026-10-17 11:13: SIGSEGV at 0x190 on that thread, the "quit unexpectedly" dialog)
	if _count_thread != null:
		if _count_http != null:
			_count_http.cancelled = true
		_count_thread.wait_to_finish()
		_count_thread = null
	if _thread != null:
		if _job_kind == "aio":
			aio.request_stop("pause")
		elif _job_kind == "mod":
			mods.request_stop("pause")
		updater.http.cancelled = true
		_thread.wait_to_finish()
	if _crash_marker:
		LauncherCrash.remove_marker()
	if _instance != null:
		_instance.release()
