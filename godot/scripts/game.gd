## OpenBFME: the game. The real entry point (the project's main scene).
##
## Boots the retail shell (AptLevel0 + MainMenu through the WindowManager / Shell, the native gadget layer drawn over the movies), lets the player open Skirmish and set up the
## lobby, and on Start: resolves the lobby's random choices in the logic (retail's order), pushes LoadScreen.apt with the real players' cards, loads the map synchronously
## while driving the load screen's bar with real progress (the logic's milestones, then terrain, object assets and live objects), pops the shell and shows the live game
## (GameWorld) with Palantir.apt loaded as the HUD.
##
##   godot --path godot                        the game
##   godot --path godot -- [options]           options after `--`:
##     --res=1280x720        window size
##     --seed=N              the lobby's game seed (default: the clock, like retail's GetTickCount)
##     --auto                scripted run: click MainMenu -> Skirmish with mouse events, set the lobby (--map/--faction/--ai/--color), click Start, play --advance seconds
##     --map=<key>           (with --auto) the map cache key, default maps/map mp evendim/map mp evendim.map
##     --faction=<Name>      (with --auto) slot 0's PlayerTemplate (default FactionMen); --ai=<2..5> slot 1's AI level; --color=<index> slot 0's colour (default random)
##     --advance=<seconds>   (with --auto) game time to run after the first frame, then quit
##     --vsync=on|off|mailbox|adaptive, --max-fps=<n>   frame pacing (lane SMOOTH-1, scripts/frame_pacing.gd): default vsync on, no frame cap
##     --screens=<dir>       (with --auto) write start1-lobby.png, start1-loading.png and start1-game.png there
##     --check               (with --auto) no window assumptions: print the report lines and quit with the exit code (0 = no errors)
##     --report              print the load report as JSON
##   lane END-1, the end of the game:
##     --end                 (with --auto) after --advance seconds the enemy's base is destroyed (a test hook: start option test_hooks), the end sequence runs
##                           (victory screen, 7 s, hide), the game is left to the score screen (TimeLine.apt), Continue goes back to the main menu and a second
##                           game starts and runs 3 s; exit 0 when every step happened. --end-lose destroys the local player instead (the defeat screen).
##                           With --screens=<dir> it writes end1-<faction>-victory.png / -defeat.png, end1-<faction>-score.png and end1-menu.png
##     --options-shot=FILE   (lane UI-2) open Options from the main menu, save the window to FILE after 2 s and quit (the soft particles box)
##     --end-capture=DIR     (lane UI-2, with --end) save every rendered frame of the first 5 s of the end screen, half size, as DIR/fNNNN.png and the elapsed
##                           milliseconds of each in DIR/times.txt (the ring animation video)
##     In a live game Esc (or the Palantir's options button) toggles the quit menu QuitMenu.apt (lane END-2): Resume, Options, Restart / Forfeit, Exit
##   lane END-2, the quit menu (with --auto):
##     --quit                Esc opens the quit menu (the game pauses), Resume closes it, Esc and Exit with its confirmation leave to the score screen (its tabs
##                           and statistics page), Continue returns to the Skirmish lobby and a second game starts; --screens writes end2-*.png
##     --quit-restart        Esc, Restart and its confirmation start the same game again
##     --perf                (lane SMOOTH-1) while playing, print GAME PERF every 5 s: render fps, frame time mean / p95 / p99 / max and the C++ parts of the frame
##     --fps                 (lane SMOOTH-1) show the render frame rate and frame time in a corner
##     --opponents=<n>       (lane PERF-1, with --auto) slots 1..n are computers of level --ai (default 1; a 4-player map takes 3)
##     --warp=<seconds>      (lane PERF-1, with --auto) run the game at --warp-scale (default 8) times speed until this much game time has passed, then at normal speed
##                           for --advance seconds (the frame rate of a late game: GAME PERF restarts when the warp ends)
##   lane QA-1, the scripted player (scripts/qa_player.gd; tools/qa/qa_matrix.py runs the matrix):
##     --qa                  (with --auto) after the start the local side is played by the scripted human through the HUD's input (builds, trains, upgrades,
##                           casts, attacks) for --qa-minutes of game time (default 20) at --qa-speed times speed (default 4), then the game is left the way
##                           a player leaves it (victory: the end screen, then the quit menu's Exit; else Esc, Exit, Yes) to the score screen, whose
##                           Continue must return to the lobby. Prints QA ISSUE / QA SUMMARY lines; exit 0 when the flow completed (issues are data)
##     --qa-idle             (with --qa) the local side does nothing (an AI-vs-AI game; --teams=0,0,1,1 puts the slots on teams)
##     --qa-cash=<n>         (with --qa) a test hook gives the local player n once at the start (so a short run can win; logged as QA TEST HOOK)
##     --qa-tag=<name>       the run's name in the screenshots (--screens: qa1-<tag>-*.png)
##     --teams=<t0,t1,...>   (with --auto) the lobby Team of each slot in order (-1 = none)
##     --qa-scene=<name>     (lane QA-2, with --qa) instead of playing, one feedback scene (scripts/qa_scene.gd: melee, archers, troll, rohirrim, charge,
##                           garrison, grond, wall) is set up by a logged test hook, ordered through the HUD's input and recorded (--screens)
##   lane MP-1, a LAN game without the shell's lobby screens (GameNetwork/LANLobby.h: join by address; the APT LAN lobby is NET-2):
##     --net-host=PORT       host a game: --map, --seed, --net-slots=kind:Faction:start:team;... (kind human / easy / medium / hard / brutal; the
##                           first human slot is the host's), --net-run-ahead=N, --net-crc=N (CRC interval)
##     --net-join=IP:PORT    join the game hosted there (--net-name=Name)
##     --net-script          the local player is the scripted test player (GameNetwork/ScriptedPlayer.h)
##     --net-record=FILE     record the game (Common/Recorder.h)
##     --net-frames=N        quit after logic frame N, printing the network status (exit 2 on a desync)
##     --net-camera=x,y,z    look there at the start instead of the local start (Godot axes; the demo shows one place in both windows)
##     --net-overlay         draw the slot, the logic frame, the state hash and the CRC checks over the game
##     --net-end             (lane END-1) at --net-frames the LAN game is left like a finished game (clear_game_data to the score screen, which must be type 3), Continue
##                           goes to the main menu and a skirmish starts: its local slot must be slot 0 (the finished game's network state is gone); exit 0 then
##     --net-capture=DIR     save the window as DIR/f<logic frame>.png once per logic frame (the two-window demo video is put together by frame number)
##   lane MP-2, the disconnect path (GameNetwork/DisconnectManager.h): a silent peer brings up DisconnectScreen.apt (names, timeout bars, Kick, Quit); a dropped
##   player's "Network:PlayerLeftGame" notice; dropped / alone / Quit leaves the game to the score screen
##     --net-disconnect-ms=N / --net-player-timeout-ms=N   replace GameData's NetworkDisconnectTime / NetworkPlayerTimeoutTime (tests, demos)
##     --net-kick            press Kick for every player the disconnect screen offers it for
##     --net-shot-disconnect=FILE   save the window once the disconnect screen has been up for 2 s (the media)
##     --net-shot-desync=FILE       save the window 1 s after the desync message box came up (the media)
##   lane MP-2, replays: every skirmish and LAN game is recorded to <user data>/Replays/<GUI:LastReplay>.replay (Common/Recorder.h); the main menu's Load Replay
##   opens SaveLoad.apt's replay page (AptSaveLoad: the files of that folder) and Load plays the chosen one back (GameWorld.prepare_replay / start_replay)
##     --replay=FILE         play FILE back at once (no menu); --replay-check: quit when it ends, exit 0 when every frame's hash matched
##     --replay-speed=N      (lane QA-2) play the replay at N times speed (the QA matrix checks every game's replay this way)
##     --replay-watch=<Player>:<Template>   (lane QA-2) while a replay plays, every 50 logic frames print each of the player's objects of that template
##                           (position, conditions, production queue) and the player's money / command points: QA-2's reproductions
##     --replay-menu-test    (with --auto) after the game, open Load Replay, screenshot it (--screens), load the first replay and play it to the end
##     --no-record           do not record the games
##   lane MP-2, the LAN lobby: the main menu's LAN opens LanLobby.apt on GameWorld's LAN lobby (GameNetwork/LANAPI.h: RotWK's lobby ports 8086 ..
##   8093, broadcast); every player's start (the shell request "LanGameStart") loads the network game with net_begin as --net-host / --net-join do
##     --lan=host|join       script the lobby through the movie's commands: open LAN from the main menu; host: Create Game, the map (--map), the own faction
##                           (--faction), --lan-ai=N medium AIs after the players, Play Game once --lan-players=N humans are in and accepted; join: Join of the first
##                           game listed, the own faction, Accept (again after every options change). --net-name=Name, --net-script, --net-frames as above
##     --lan-targets=a.b.c.d,...   send the lobby's messages there instead of broadcasting (tests on one machine: 127.0.0.1); --lan-port-base=N (8086)
##     --lan-shot=FILE       (host) save the window when the lobby is complete, before Play Game (the media)
##     --lan-shot-start=FILE (host, lane UI-1) save the window 8 s after Play Game: the start countdown's QM:STARTINGGAME box (the media)
##     --net-shot=FILE:FRAME save the window once the network game reached logic frame FRAME (the media)
##
## HOOKS OF OTHER LANES (leave the names as they are):
##   _on_shell_service(kind, a, b)   every ShellServices call: kind "sound" (a = the sound name) is AUDIO-1's hook for UI sounds and music cues; "load_music" carries the faction's
##                                   LoadScreenMusic when a load starts; "background", "mouse_visible", "tooltip" are the others
##   _on_shell_request(action, arg)  the main-menu actions the shell hands to the engine (ExitGame, Lan, LoadGame ...)
##   _install_hud()                  HUD-1's install point: called once the live game exists and Palantir.apt is up; it gets the GameWorld, the AptMenuPlayer and the local player
extends Node

const DEFAULT_MAP := "maps/map mp evendim/map mp evendim.map"
const LEVEL_MAIN_MENU_FRAMES := 20
const KEEP_RENDERING := true

enum State { BOOT, MENU, LOADING, PLAYING, SCORE, FAILED }

var _res := Vector2i(0, 0)
var _seed := 0
var _auto := false
var _check := false
var _print_report := false
var _map_key := DEFAULT_MAP
var _faction := "FactionMen"
var _ai := 3
var _color := -1
var _advance := 3.0
var _screens := ""
var _perf := false           # lane SMOOTH-1: --perf / --fps
var _opponents := 1          # lane PERF-1: --opponents / --warp / --warp-scale
var _warp := 0.0
var _warp_scale := 8.0
var _show_fps := false
var _perf_times: Array = []
var _perf_cpu0 := -1.0       # lane PERF-1: the main thread's CPU time at the start of the GAME PERF window
var _perf_deltas: Array = []
var _perf_parts := {}
var _perf_last := 0
var _perf_prev_us := 0
var _fps_label: Label

var _fs: RefCounted
var _audio: Node
var _world: Node3D           # GameWorld
var _shell: Node2D           # AptMenuPlayer
var _camera: Camera3D
var _env: WorldEnvironment
var _sun: DirectionalLight3D
var _state := State.BOOT
var _frames := 0
var _menu_revealed := false
var _report := {}
var _failed := ""
var _loading_shot_done := false
var _load_log: Array = []
var _hud_installed := false
var _hud: Node
var _alternate_mouse := false
var _game_frames := 0
var _auto_task_started := false
# lane MP-1
var _net_host := -1
var _net_join := ""
var _net_name := "Joiner"
var _net_slots := "human:FactionMen:0:0;human:FactionMordor:1:1"
var _net_run_ahead := 2
var _net_crc := 100
var _net_script := false
var _net_record := ""
var _net_frames := -1
var _net_camera := ""
var _net_overlay := false
var _net_capture := ""
var _net_captured := -1
var _net_last_frame := -1
var _net_state := ""
var _net_label: Label
var _net_last_print := -1
var _net_end := false
var _net_end_started := false
var _net_disconnect_ms := -1      # lane MP-2
var _lan_mode := ""
var _lan_players := 2
var _lan_ai := 0
var _lan_targets := ""
var _lan_port_base := -1
var _lan_shot := ""
var _end_capture := "" # lane UI-2: --end-capture=DIR
var _options_shot := "" # lane UI-2: --options-shot=FILE
var _options_shot_frames := 0
var _lan_phase := 0
var _lan_wait := 0
var _lan_shot_done := false
var _lan_start_ms := -1
var _lan_shot_start := "" # lane UI-1: (host) the window 8 s after Play Game: the countdown's QM:STARTINGGAME box (the media)
var _net_shot := ""
var _net_shot_frame := -1
var _net_player_timeout_ms := -1
var _net_kick := false
var _net_shot_disconnect := ""
var _dc_shown := false
var _dc_since := 0
var _dc_shot_done := false
var _dc_left_seen := 0
var _dc_quit_handled := false
var _desync_shown := false
var _net_shot_desync := ""
var _replay_file := ""          # lane MP-2
var _replay_check := false
var _replay_speed := 1.0
var _replay_watch := ""
var _replay_watch_next := 0
var _replay_menu_test := false
var _record := true
var _replay_active := false
var _replay_last_print := -1
var _last_local_slot := -1
# lane END-1
var _end := false
var _end_lose := false
var _end_esc := false
var _end_shown := false
var _profile_made := false
var _games_started := 0
var _end_log: Array = []
var _end_message_layer: CanvasLayer
var _end_message_label: Label
var _end_message_until := 0
var _graph_layer: CanvasLayer
var _graph_node: Control
var _score_report := {}
var _continue_pressed := false
var _local_name := ""
var _second_game_ok := false
var _net_lobby_again := false
var _continuing := false
var _qa := false                # lane QA-1
var _qa_idle := false
var _qa_minutes := 20.0
var _qa_speed := 4.0
var _qa_scene := ""
var _qa_tag := "qa"
var _qa_cash := 0
var _teams := PackedStringArray()
var _quit_flow := ""            # lane END-2: --quit / --quit-restart (the scripted quit menu)  # lane END-2: Continue after a LAN game opens the LAN lobby again


func _ready() -> void:
	print("FRAME PACING ", preload("res://scripts/frame_pacing.gd").apply_from_args(OS.get_cmdline_user_args())) # lane SMOOTH-1
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--res="):
			var p := arg.substr(6).split("x")
			_res = Vector2i(int(p[0]), int(p[1]))
		elif arg.begins_with("--seed="):
			_seed = int(arg.substr(7))
		elif arg == "--auto":
			_auto = true
		elif arg == "--perf":
			_perf = true
		elif arg == "--fps":
			_show_fps = true
		elif arg == "--check":
			_check = true
		elif arg == "--alternate-mouse":
			_alternate_mouse = true
		elif arg == "--report":
			_print_report = true
		elif arg == "--end":
			_end = true
		elif arg == "--end-esc":
			_end = true
			_end_esc = true
		elif arg == "--qa":
			_qa = true
		elif arg == "--qa-idle":
			_qa = true
			_qa_idle = true
		elif arg.begins_with("--qa-minutes="):
			_qa_minutes = float(arg.substr(13))
		elif arg.begins_with("--qa-scene="):
			_qa_scene = arg.substr(11)
		elif arg.begins_with("--qa-speed="):
			_qa_speed = float(arg.substr(11))
		elif arg.begins_with("--qa-cash="):
			_qa_cash = int(arg.substr(10))
		elif arg.begins_with("--qa-tag="):
			_qa_tag = arg.substr(9)
		elif arg.begins_with("--teams="):
			_teams = arg.substr(8).split(",")
		elif arg == "--quit":
			_quit_flow = "exit"
		elif arg == "--quit-restart":
			_quit_flow = "restart"
		elif arg == "--end-lose":
			_end = true
			_end_lose = true
		elif arg.begins_with("--map="):
			_map_key = arg.substr(6)
		elif arg.begins_with("--faction="):
			_faction = arg.substr(10)
		elif arg.begins_with("--ai="):
			_ai = int(arg.substr(5))
		elif arg.begins_with("--opponents="):
			_opponents = clampi(int(arg.substr(12)), 1, 7)
		elif arg.begins_with("--warp="):
			_warp = float(arg.substr(7))
		elif arg.begins_with("--warp-scale="):
			_warp_scale = float(arg.substr(13))
		elif arg.begins_with("--color="):
			_color = int(arg.substr(8))
		elif arg.begins_with("--advance="):
			_advance = float(arg.substr(10))
		elif arg.begins_with("--screens="):
			_screens = arg.substr(10)
		elif arg.begins_with("--net-host="):
			_net_host = int(arg.substr(11))
		elif arg.begins_with("--net-join="):
			_net_join = arg.substr(11)
		elif arg.begins_with("--net-name="):
			_net_name = arg.substr(11)
		elif arg.begins_with("--net-slots="):
			_net_slots = arg.substr(12)
		elif arg.begins_with("--net-run-ahead="):
			_net_run_ahead = int(arg.substr(16))
		elif arg.begins_with("--net-crc="):
			_net_crc = int(arg.substr(10))
		elif arg == "--net-end":
			_net_end = true
		elif arg == "--net-script":
			_net_script = true
		elif arg.begins_with("--net-record="):
			_net_record = arg.substr(13)
		elif arg.begins_with("--net-frames="):
			_net_frames = int(arg.substr(13))
		elif arg.begins_with("--net-camera="):
			_net_camera = arg.substr(13)
		elif arg == "--net-overlay":
			_net_overlay = true
		elif arg.begins_with("--net-capture="):
			_net_capture = arg.substr(14)
		elif arg.begins_with("--net-disconnect-ms="):
			_net_disconnect_ms = int(arg.substr(20))
		elif arg.begins_with("--net-player-timeout-ms="):
			_net_player_timeout_ms = int(arg.substr(24))
		elif arg == "--net-kick":
			_net_kick = true
		elif arg.begins_with("--net-shot-disconnect="):
			_net_shot_disconnect = arg.substr(22)
		elif arg.begins_with("--net-shot-desync="):
			_net_shot_desync = arg.substr(18)
		elif arg.begins_with("--replay="):
			_replay_file = arg.substr(9)
		elif arg == "--replay-check":
			_replay_check = true
		elif arg.begins_with("--replay-speed="):
			_replay_speed = float(arg.substr(15))
		elif arg.begins_with("--replay-watch="):
			_replay_watch = arg.substr(15)
		elif arg == "--replay-menu-test":
			_replay_menu_test = true
		elif arg == "--no-record":
			_record = false
		elif arg.begins_with("--lan="):
			_lan_mode = arg.substr(6)
		elif arg.begins_with("--lan-ai="):
			_lan_ai = int(arg.substr(9))
		elif arg.begins_with("--lan-players="):
			_lan_players = int(arg.substr(14))
		elif arg.begins_with("--lan-targets="):
			_lan_targets = arg.substr(14)
		elif arg.begins_with("--lan-port-base="):
			_lan_port_base = int(arg.substr(16))
		elif arg.begins_with("--options-shot="):
			_options_shot = arg.substr(15)
		elif arg.begins_with("--end-capture="):
			_end_capture = arg.substr(14)
		elif arg.begins_with("--lan-shot="):
			_lan_shot = arg.substr(11)
		elif arg.begins_with("--lan-shot-start="):
			_lan_shot_start = arg.substr(17)
		elif arg.begins_with("--net-shot="):
			var spec := arg.substr(11)
			_net_shot = spec.substr(0, spec.rfind(":"))
			_net_shot_frame = int(spec.substr(spec.rfind(":") + 1))
	# lane QA-1 (review r1): the scripted player is a single-player run; a network flag would start both tasks (the scripted skirmish and the LAN session)
	if _qa and (_net_host > 0 or not _net_join.is_empty() or _net_script or _net_end or not _net_record.is_empty() or _net_frames >= 0 or not _lan_mode.is_empty()
			or not _replay_file.is_empty() or _replay_menu_test):
		printerr("GAME FAIL: --qa / --qa-idle cannot be combined with the --net-*, --lan* or --replay* options (a LAN game or a replay is not a scripted skirmish)")
		get_tree().quit(2)
		return
	if _res != Vector2i(0, 0):
		get_window().size = _res
	RenderingServer.set_default_clear_color(Color(0, 0, 0))
	_build_stage()
	await get_tree().process_frame
	_boot()


func _build_stage() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0) # the base game shows black outside the map (owner, 2026-10-06)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	_env = WorldEnvironment.new()
	_env.environment = env
	add_child(_env)
	_camera = Camera3D.new()
	_camera.fov = 45
	_camera.near = 2.0
	_camera.far = 60000.0
	_camera.current = false
	add_child(_camera)


func _fail(message: String) -> void:
	_failed = message
	_state = State.FAILED
	printerr("GAME FAIL: ", message)
	if _check or _auto or DisplayServer.get_name() == "headless":
		get_tree().quit(1)
	else:
		Release.fatal(message) # lane RELEASE-1: a message box naming the session log, then quit


func _boot() -> void:
	if not ClassDB.class_exists("AptMenuPlayer") or not ClassDB.class_exists("GameWorld"):
		_fail("the openbfme extension is not loaded (or too old); run build.bat")
		return
	_fs = ClassDB.instantiate("RetailFileSystem")
	# lane RELEASE-1: the environment, the remembered folders, or the first-run screen (scripts/release/release.gd)
	var mount: Dictionary = await Release.mount_retail(_fs)
	if mount.get("quit", false):
		get_tree().quit(0)
		return
	if not mount.ok:
		_fail("mount failed:\n" + "\n".join(mount.errors))
		return
	_world = ClassDB.instantiate("GameWorld")
	_world.name = "GameWorld"
	add_child(_world)
	var setup: Dictionary = _world.setup(_fs)
	print("GAME object world: ok=%s, %d templates, %d playable factions, %.1f s" % [setup.ok, setup.get("templates", 0), setup.get("factions", 0), setup.get("seconds", 0.0)])
	if not setup.ok:
		_fail("object world: " + "\n".join(setup.errors))
		return
	if not _boot_audio():
		return
	_shell = ClassDB.instantiate("AptMenuPlayer")
	_shell.name = "Shell"
	add_child(_shell)
	var boot: Dictionary = _shell.boot_shell(_fs, _world, {"seed": _seed})
	if not boot.ok:
		_fail("shell: " + "\n".join(boot.errors))
		return
	print("GAME shell booted in %.0f ms: %d strings, lobby seed %d" % [boot.load_ms, boot.strings, boot.seed])
	_shell.shell_request.connect(_on_shell_request)
	_shell.shell_service.connect(_on_shell_service)
	_shell.shell_screen.connect(_on_shell_screen)
	if not _shell.shell_push("MainMenu.apt"):
		_fail("the shell refused MainMenu.apt: " + str(_shell.get_shell_report().errors))
		return
	_audio.play_shell_music(false) # no shell map yet (View3D is a stop): the low-LOD shell music, as menu_viewer
	_state = State.MENU
	set_process(true)
	if _auto and not _auto_task_started:
		_auto_task_started = true
		_run_auto()
	if _net_host > 0 or not _net_join.is_empty():
		_net_open()
	elif not _replay_file.is_empty():
		_begin_replay(_replay_file)


func _process(delta: float) -> void:
	_frames += 1
	Release.set_in_game(_state == State.PLAYING) # lane RELEASE-1: the version corner in the menus only
	if _state == State.MENU:
		# the engine reveals the main menu once its first frames ran (menus-apt.md 1.2, S-139: who calls ShowMainMenu is not read)
		if not _menu_revealed and _frames >= LEVEL_MAIN_MENU_FRAMES and _shell.shell_top_level() >= 0 and _shell.shell_stack().size() > 0 and _shell.shell_stack()[-1] == "MainMenu.apt":
			_menu_revealed = true
			var shown: Dictionary = _shell.shell_invoke(_shell.shell_top_level(), "ShowMainMenu", PackedStringArray())
			if not shown.ok:
				printerr("GAME ShowMainMenu: ", shown.error)
		var message: Dictionary = _shell.take_new_game()
		if not message.is_empty():
			_begin_game(message)
		elif _net_state == "lobby":
			_net_poll_lobby()
		elif not _lan_mode.is_empty() and _menu_revealed:
			_lan_script_tick()
		elif not _options_shot.is_empty() and _menu_revealed:
			_options_shot_tick()
	elif _state == State.PLAYING:
		_game_frames += 1
		if not _net_state.is_empty():
			_net_tick()
		_update_listener()
		_end_game_tick()
		if _replay_active:
			_replay_tick()
		if _perf or _show_fps:
			_perf_frame(delta)
	elif _state == State.SCORE:
		_score_tick()


# lane FX-2: the 3D sounds (effects, deaths, weapons) are heard from where the tactical camera looks: its ground position and heading, SAGE space (ZH sets the
# listener from the view each frame; RotWK's call was not read: inference). Without it every positional sound is culled by distance from the origin.
func _update_listener() -> void:
	if _audio == null or _hud == null or not _hud.has_method("get_camera"):
		return
	var cam: Dictionary = _hud.get_camera()
	if not cam.has("position"):
		return
	var p: Vector2 = cam.position
	var a: float = cam.get("angle", 0.0)
	_audio.set_listener(Vector3(p.x, p.y, cam.get("ground_level", 0.0)), Vector3(cos(a), sin(a), 0.0))


# ---- the shell's hooks -----------------------------------------------------------------------------------------------------------------------

func _on_shell_service(kind: String, a: String, b: String) -> void:
	# AUDIO-1: kind "sound" is a UI sound or music cue name (a); "load_music" is the faction's LoadScreenMusic when a load starts
	if kind == "sound":
		_audio.play_shell_sound(a)
	elif kind == "load_music":
		_audio.play_music(a)
	elif kind == "tooltip" or kind == "mouse_visible" or kind == "background":
		pass


func _on_shell_request(action: String, argument: String) -> void:
	print("GAME shell request: ", action, " (", argument, ")")
	if action == "ExitGame":
		get_tree().quit(0)
	elif action == "ScoreScreenContinue":
		_score_continue()
	elif action == "LoadReplay":
		_open_replay_menu()
	elif action == "LoadReplayFile":
		_begin_replay(argument)
	elif action == "Lan":
		_open_lan_lobby()
	elif action == "LanGameStart":
		_lan_game_start()

	elif action == "ToggleQuitMenu":
		_toggle_quit_menu()
	elif action.begins_with("QuitMenu"):
		_quit_menu_action(action)


func _on_shell_screen(filename: String) -> void:
	print("GAME screen: ", filename)
	if _state == State.MENU and filename != "MainMenu.apt":
		_audio.play_submenu_music()


# The retail audio manager (AUDIO-1): UI sounds, shell and load-screen music; the logic's and drawables' sounds go through AudioApi once it is booted.
func _boot_audio() -> bool:
	if not ClassDB.class_exists("GameAudio"):
		_fail("the openbfme extension has no GameAudio class (too old); run build.bat")
		return false
	_audio = ClassDB.instantiate("GameAudio")
	_audio.name = "GameAudio"
	add_child(_audio)
	var booted: Dictionary = _audio.boot(_fs, {"seed": _seed})
	if not booted.ok:
		_fail("audio boot failed:\n" + "\n".join(booted.errors))
		return false
	print("GAME audio booted: %d events" % booted.events)
	return true


# ---- starting a game -------------------------------------------------------------------------------------------------------------------------

func _begin_game(message: Dictionary) -> void:
	print("GAME new game: map %s seed %d cash %d" % [message.map, message.seed, message.starting_cash])
	_last_new_game = message.duplicate(true) # lane END-2: the quit menu's Restart starts it again (RW 0x9220DE)
	if _restart_stack.is_empty():
		_start_stack = _shell.shell_stack() # lane END-2: the screens the score screen's Continue shows again (TheShell + 0x9C, RW 0x925798)
	_restart_stack = PackedStringArray()
	var prep: Dictionary = _world.prepare_new_game(message)
	_last_local_slot = int(prep.get("local_slot", -1))
	if not prep.ok:
		_fail("prepare_new_game: " + "\n".join(prep.errors))
		return
	var resolved: Dictionary = prep.resolved
	for i in range(resolved.slots.size()):
		var s: Dictionary = resolved.slots[i]
		if s.state >= 2:
			print("GAME   slot %d: state %d faction %d colour %d start %d team %d (was faction %d colour %d start %d)" % [i, s.state, s.player_template, s.color, s.start_pos, s.team, -1, -1, -1])
	_state = State.LOADING
	var cards: Dictionary = _shell.set_load_screen_from_game(resolved)
	if cards.has("error"):
		_fail("load screen: " + cards.error)
		return
	var local_card: int = cards.get("local_card", -1)
	if local_card >= 0 and local_card < cards.cards.size():
		var music: String = cards.cards[local_card].get("load_music", "")
		if not music.is_empty():
			_audio.play_music(music) # the local player's faction LoadScreenMusic
	_shell_load_screen_push()
	# a few frames so the loading screen is on screen before the synchronous load starts
	for i in 4:
		await get_tree().process_frame
	_shell.auto_process = false
	var started := Time.get_ticks_msec()
	_load_log.clear()
	var opts := {"progress": Callable(self, "_on_load_progress"), "test_hooks": _end}
	if _record and _net_state.is_empty():
		opts["record"] = _last_replay_path() # lane MP-2: every skirmish is recorded
	var rep: Dictionary = _world.start_new_game(opts)
	_report = rep
	print("GAME load: %d ms, ok=%s" % [Time.get_ticks_msec() - started, rep.ok])
	if rep.has("timings_ms"): # lane PERF-1: where the load went (ms per stage)
		print("GAME load timings: ", JSON.stringify(rep.timings_ms))
	if not rep.ok:
		for e in rep.errors.slice(0, 20):
			print("  load error: ", e)
		_fail("start_new_game failed")
		return
	_enter_game(rep)


func _shell_load_screen_push() -> void:
	if not _shell.shell_push("LoadScreen.apt"):
		_fail("the shell refused LoadScreen.apt: " + str(_shell.get_shell_report().errors))


## Called by the load with the retail percentage (RotWK updateLoadProgress): the load screen's bar and a frame, synchronously, like retail's loading loop.
func _on_load_progress(percent: int) -> void:
	_load_log.append(percent)
	_shell.set_load_progress(percent)
	_shell.tick(0.033)
	_shell.render(true)
	RenderingServer.force_draw(false, 0.0)
	if not _loading_shot_done and percent >= 40 and not _screens.is_empty():
		_loading_shot_done = true
		_save_screenshot("loading")


func _enter_game(rep: Dictionary) -> void:
	# retail: the shell is popped, the HUD movie (Palantir) is the only movie left
	while _shell.shell_stack().size() > 0:
		_shell.shell_pop()
		_shell.tick(0.033)
	if not _shell.shell_push("Palantir.apt"):
		_fail("the shell refused Palantir.apt: " + str(_shell.get_shell_report().errors))
		return
	_shell.auto_process = true
	_apply_lighting(rep)
	_camera.current = true
	# AUDIO-2 (review r1 fix 2): the live game's audio side before the game advances: sound owners, the player filter, ambient / group / move sounds, TheEva's
	# side and the in-game music scripts (which replace the load screen music)
	_audio.stop_music(true)
	var at: Dictionary = _world.attach_audio()
	print("GAME audio attached: ", at)
	if not at.get("ok", false):
		_fail("attach_audio failed: " + str(at.get("error", "")))
		return
	var ar: Dictionary = _world.get_audio_report()
	if not str(ar.get("music_error", "")).is_empty():
		printerr("GAME in-game music: ", ar.music_error)
	_world.set_auto_advance(true)
	_state = State.PLAYING
	_games_started += 1
	print("GAME playing: frame %d, %d objects, hash %d" % [_world.get_frame(), _world.get_object_count(), _world.get_state_hash()])
	if _print_report:
		print("GAME REPORT ", JSON.stringify(rep))
		for o in rep.start.starting_objects:
			print("GAME starting object: ", JSON.stringify(_world.get_object(o.id)))
	_install_hud(rep)
	# lane END-1: GuiFX.apt (the end screen's movie) is loaded with the game, so ShowEndGame finds it
	var gfx: Dictionary = _shell.end_game_screen({"kind": "preload"})
	print("GAME GuiFX for the end screen: level %d %s" % [gfx.get("level", -1), str(gfx.get("error", ""))])
	if not _net_state.is_empty():
		_net_begin()


## HUD-1's install point: the live game exists and Palantir.apt is pushed (the factory table makes it an AptPalantir). The HUD node takes the GameWorld, the camera, the
## shell-mode AptMenuPlayer and the local player: it feeds the camera and the mouse / keyboard to the HUD (selection, orders, the Palantir, the radar, the cursors) and draws
## the native components of the movie.
func _install_hud(rep: Dictionary) -> void:
	_hud_installed = false
	var hud: Node = ClassDB.instantiate("InGameHudNode")
	add_child(hud)
	var local: String = str(rep.get("start", {}).get("local_player", ""))
	_local_name = local
	var start: Dictionary = rep.get("start", {})
	if not start.has("camera_start"):
		_fail("the start report has no camera start (the retail camera has nothing to look at)")
		return
	# CAM-1: the HUD node owns the camera from here (TacticalCamera: retail limits, zoom, scrolling, rotation); it looks at the player's start waypoint
	var camera_start: Vector3 = start.camera_start
	if not _net_state.is_empty():
		print("GAME NET camera start ", camera_start)
	if not _net_camera.is_empty():
		var c := _net_camera.split(",")
		camera_start = Vector3(float(c[0]), float(c[1]), float(c[2]))
	var r: Dictionary = hud.attach(_fs, _world, _camera, _shell, local, {"alternate_mouse": _alternate_mouse, "camera_start": camera_start})
	print("GAME HUD installed: ok=%s errors=%s" % [r.ok, r.errors])
	if not r.ok:
		hud.queue_free()
		return
	_hud = hud
	_hud_installed = true
	# lane SPELL-2: the spell book is the retail movies the HUD drives (the Palantir's InGameSpellBook, SpellStore.apt from its store button); this node only
	# draws the targeting ring of a power waiting for its ground click
	var ring = load("res://scripts/spell_target_ring.gd").new()
	add_child(ring)
	_game_nodes.append(ring) # lane END-1: goes with the game
	ring.setup(hud, _world)


func _apply_lighting(rep: Dictionary) -> void:
	if not rep.has("lighting"):
		return
	var lit: Dictionary = rep.lighting
	_env.environment.ambient_light_color = lit.ambient
	_env.environment.ambient_light_energy = 1.0
	if _sun != null:
		_sun.queue_free()
	_sun = DirectionalLight3D.new()
	_sun.name = "ObjectSun"
	_sun.light_color = lit.diffuse
	_sun.shadow_enabled = false
	var dir: Vector3 = lit.direction
	if dir.length() > 0.0001:
		_sun.transform = Transform3D(Basis.looking_at(dir.normalized(), Vector3.UP if absf(dir.normalized().y) < 0.99 else Vector3.RIGHT), Vector3.ZERO)
	add_child(_sun)


# ---- the scripted run (--auto) --------------------------------------------------------------------------------------------------------------

func _mouse_event(stage_position: Vector2, pressed: bool, with_button: bool) -> void:
	var window_position: Vector2 = _shell.stage_to_window(stage_position)
	if with_button:
		var button := InputEventMouseButton.new()
		button.button_index = MOUSE_BUTTON_LEFT
		button.pressed = pressed
		button.position = window_position
		button.global_position = window_position
		get_viewport().push_input(button)
	else:
		var motion := InputEventMouseMotion.new()
		motion.position = window_position
		motion.global_position = window_position
		get_viewport().push_input(motion)


func _step(frames: int) -> void:
	# at least `frames` render frames AND frames / 60 seconds (SMOOTH-1: at an uncapped frame rate a two-frame click lasted less than one tick of the shell's clock)
	var until := Time.get_ticks_msec() + int(frames * 1000.0 / 60.0)
	for i in frames:
		await get_tree().process_frame
	while Time.get_ticks_msec() < until:
		await get_tree().process_frame


func _click_button(level: int, path: String) -> bool:
	var button: Dictionary = _shell.find_button(level, path)
	if not button.found:
		printerr("GAME no button at or under '%s' on _level%d" % [path, level])
		return false
	var p := Vector2(button.x, button.y)
	_mouse_event(p, false, false)
	await _step(2)
	_mouse_event(p, true, true)
	await _step(2)
	_mouse_event(p, false, true)
	await _step(2)
	print("GAME clicked ", button.path)
	return true


func _save_screenshot(state: String) -> void:
	if _screens.is_empty():
		return
	DirAccess.make_dir_recursive_absolute(_screens)
	var image := get_viewport().get_texture().get_image()
	var path := "%s/start1-%s.png" % [_screens, state]
	var err := image.save_png(path)
	print("GAME screenshot %s -> %s (%dx%d)" % [path, error_string(err), image.get_width(), image.get_height()])


func _run_auto() -> void:
	# main menu: wait for ShowMainMenu, open the solo nav and click Skirmish (real mouse events)
	await _step(LEVEL_MAIN_MENU_FRAMES + 70)
	if not await _auto_lobby_and_start():
		return
	await _step(30)
	_save_screenshot("game")
	if _qa:
		await _run_qa()
		return
	if _warp > 0.0:
		# lane PERF-1: fast forward (the logic frames due run as fast as the worker allows), then measure the late game at normal speed
		var w0 := Time.get_ticks_msec()
		_world.set_time_scale(_warp_scale)
		while _world.get_frame() < int(_warp * 5.0):
			await _step(30)
		_world.set_time_scale(1.0)
		print("GAME warp: logic frame %d after %.1f s (x%.1f)" % [_world.get_frame(), (Time.get_ticks_msec() - w0) / 1000.0, _warp_scale])
		_perf_times.clear()
		_perf_deltas.clear()
		_perf_parts.clear()
		_perf_prev_us = 0
		_perf_cpu0 = -1.0
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < int(_advance * 1000.0):
		await get_tree().process_frame
	if not _quit_flow.is_empty():
		await _run_quit()
		return
	if _end:
		await _run_end()
		return
	if _replay_menu_test:
		await _run_replay_menu_test()
		return
	_auto_report()


## --replay-menu-test (lane MP-2): the game just played was recorded; leave it, open Load Replay from the main menu, then play the newest replay back to its end
func _run_replay_menu_test() -> void:
	var recorded: Dictionary = _world.recording_status()
	print("GAME REPLAY recording: ", JSON.stringify(recorded))
	_exit_to_score_screen()
	await _step(30)
	_score_continue()
	await _step(LEVEL_MAIN_MENU_FRAMES + 70)
	_on_shell_request("LoadReplay", "")
	await _step(90)
	if not _screens.is_empty():
		DirAccess.make_dir_recursive_absolute(_screens)
		get_viewport().get_texture().get_image().save_png(_screens.path_join("mp2-replay-menu.png"))
	var files: Array = _world.list_replays(_replay_dir())
	if files.is_empty():
		_fail("no replay was recorded in " + _replay_dir())
		return
	print("GAME REPLAY menu lists %d files; loading %s" % [files.size(), files[0].path])
	_on_shell_request("LoadReplayFile", files[0].path)


## The scripted way from the main menu through the Skirmish lobby to a running game (real mouse events on the retail movies); false (and _fail) on a failure.
func _auto_lobby_and_start() -> bool:
	var stack: PackedStringArray = _shell.shell_stack()
	if stack.size() == 0 or stack[-1] != "Skirmish.apt":
		var menu_level: int = _shell.shell_top_level()
		if not await _click_button(menu_level, "SoloPlayNav"):
			_fail("no SoloPlayNav button")
			return false
		await _step(90)
		if not await _click_button(menu_level, "SoloPlayNav.Skirmish"):
			_fail("no Skirmish item")
			return false
		await _step(90)
	else:
		print("GAME the score screen's Continue left the Skirmish lobby up: ", str(stack))
		await _step(30)
	if _shell.shell_stack().size() < 2 or _shell.shell_stack()[-1] != "Skirmish.apt":
		_fail("Skirmish.apt is not on the shell stack: " + str(_shell.shell_stack()))
		return false
	# the lobby: the profile popup, the map, the slots
	var slots: Array = [{"slot": 0, "faction": _faction, "color": _color}]
	for i in range(1, _opponents + 1):
		slots.append({"slot": i, "state": _ai})
	for i in range(mini(_teams.size(), slots.size())): # lane QA-1: --teams
		slots[i]["team"] = int(_teams[i])
	var spec := {
		"map": _map_key,
		"slots": slots,
	}
	if not _profile_made:
		_profile_made = true
		spec["profile"] = "Gimli" # the first visit asks for a profile (the popup); the shell session keeps it for the second game (lane END-1)
	var applied: Dictionary = {}
	for attempt in 60:
		applied = _shell.lobby_apply(spec)
		if applied.ok:
			break
		await _step(10) # the lobby movie is still settling (the add-profile popup opens on its first updates)
	if not applied.ok:
		_fail("lobby: " + str(applied.errors))
		return false
	await _step(90)
	_save_screenshot("lobby")
	var skirmish_level: int = _shell.shell_top_level()
	if not await _click_button(skirmish_level, "lobby.StartGame"):
		_fail("no StartGame button")
		return false
	# the game starts from _process (take_new_game); wait for it, play, report
	var waited := 0
	while _state != State.PLAYING and _state != State.FAILED and waited < 900:
		await get_tree().process_frame
		waited += 1
	if _state != State.PLAYING:
		var rep: Dictionary = _shell.get_shell_report()
		print("GAME shell report: ", JSON.stringify(rep))
		_fail("the game did not start (state %d)" % _state)
		return false
	return true


func _auto_report() -> void:
	var gs: Dictionary = _world.get_stats()
	print("GAME ran %.1f s: logic frame %d, %d objects, hash %d" % [_advance, gs.frame, gs.objects, _world.get_state_hash()])
	var shell_report: Dictionary = _shell.get_shell_report()
	print("GAME shell notes: ", JSON.stringify(shell_report.get("notes", {})))
	for e in shell_report.get("errors", []):
		print("  shell error: ", e)
	var errors: Array = _report.get("errors", [])
	print("GAME load errors: %d, progress %s" % [errors.size(), str(_load_log)])
	for e in errors.slice(0, 30):
		print("  load error: ", e)
	# the movies' own script errors (the lobby's and the HUD's callbacks that are other lanes') are reported above and are not the start's failures
	print("GAME shell script errors reported: %d" % shell_report.get("errors", []).size())
	print("GAME shell calls without a function (S-380, retail skips them): %d" % shell_report.get("calls_without_function", []).size())
	for c in shell_report.get("calls_without_function", []):
		print("  call without a function: ", c)
	if _check or _auto:
		get_tree().quit(1 if errors.size() > 0 else 0)


# ---- lane END-1: the end of the game ------------------------------------------------------------------------------------------------------------------

## Every frame of a live game: the end sequence's presentation requests (GameWorld.take_end_game_requests: the end screen on GuiFX.apt, the defeat messages,
## the sound fade of MPorSkirmishFadeToScoreScreen; Eva is played by the engine itself).
func _end_game_tick() -> void:
	if _world == null:
		return
	for r in _world.take_end_game_requests():
		_end_log.append(r)
		print("GAME END request: ", JSON.stringify(r))
		if r.kind == "show_end_game" or r.kind == "hide_end_game":
			var res: Dictionary = _shell.end_game_screen(r)
			print("GAME END %s: %s" % [r.kind, JSON.stringify(res)])
			if not res.get("ok", false):
				printerr("GAME END the end screen failed: ", res.get("error", ""))
		elif r.kind == "message":
			_show_end_message(r)
		elif r.kind == "transition":
			_fade_tactical_sound()
	if _end_message_label != null and _end_message_until > 0 and Time.get_ticks_msec() > _end_message_until:
		_end_message_label.text = ""
		_end_message_until = 0


func _input(event: InputEvent) -> void:
	# lane END-2: Esc toggles the quit menu (the options meta command, RW 0x9DB877 -> ToggleQuitMenu RW 0x921C9D), as the Palantir's options button does
	if _state == State.PLAYING and event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE:
		get_viewport().set_input_as_handled()
		# review r1: Esc in the Options screen opened from the quit menu closes Options only (the quit menu and the pause stay)
		var stack: PackedStringArray = _shell.shell_stack()
		if _quit_open and not stack.is_empty() and stack[-1] == "Options.apt":
			_shell.shell_pop()
			return
		_toggle_quit_menu()


## The in-game UI message (InGameUI vslot 0x3C / 0x48: "GUI:YouHaveBeenDefeated", "GUI:PlayerHasBeenDefeated" with the name). The Palantir's message area is
## not drawn by the HUD yet: shown in a presentation label at the top of the screen for 5 s.
func _show_end_message(r: Dictionary) -> void:
	var t: Dictionary = _shell.fetch_text(r.text)
	var text: String = t.text
	for token in ["%ls", "%s", "%hs"]:
		text = text.replace(token, r.name)
	print("GAME END message: ", text)
	if _end_message_layer == null:
		_end_message_layer = CanvasLayer.new()
		_end_message_layer.layer = 90
		add_child(_end_message_layer)
		_end_message_label = Label.new()
		_end_message_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		_end_message_label.anchor_right = 1.0
		_end_message_label.offset_top = 90
		_end_message_label.add_theme_font_size_override("font_size", 22)
		_end_message_label.add_theme_color_override("font_color", Color(1.0, 0.92, 0.6))
		_end_message_label.add_theme_color_override("font_outline_color", Color.BLACK)
		_end_message_label.add_theme_constant_override("outline_size", 6)
		_end_message_layer.add_child(_end_message_label)
	_end_message_label.text = text
	_end_message_until = Time.get_ticks_msec() + 5000


var _saved_volumes := {}

## WindowTransitions.ini MPorSkirmishFadeToScoreScreen: SOUNDFADE of the TACTICAL view from frame 0 to 30, LeaveSilent (the transition handler's 30 frames are
## taken as one second of real time: S-1060)
func _fade_tactical_sound() -> void:
	if _audio == null:
		return
	for slider in ["sound", "ambient", "voice"]:
		if not _saved_volumes.has(slider):
			_saved_volumes[slider] = _audio.get_volume(slider)
	var tw := create_tween()
	tw.tween_method(func(f: float):
		for slider in _saved_volumes.keys():
			_audio.set_volume(slider, _saved_volumes[slider] * (1.0 - f)), 0.0, 1.0, 1.0)


func _restore_sound_volumes() -> void:
	for slider in _saved_volumes.keys():
		_audio.set_volume(slider, _saved_volumes[slider])
	_saved_volumes.clear()


var _game_nodes: Array = []

# ---- lane END-2: the quit menu ---------------------------------------------------------------------------------------------------------------------

var _quit_open := false
var _quit_paused := false
var _quit_log: Array = []
var _last_new_game := {}
var _start_stack := PackedStringArray()
var _restart_stack := PackedStringArray()

## ToggleQuitMenu (RW 0x921C9D): an open menu closes with code 0 (back to the game), else QuitMenu.apt opens over the game; a game that is not a network game
## pauses while it is open (RW 0x921C4C: RW 0x625AF1(1, 0, 1))
func _toggle_quit_menu() -> void:
	if _state != State.PLAYING:
		return
	if _quit_open:
		_close_quit_menu()
		return
	var ctx: Dictionary = _world.get_quit_menu_context()
	var r: Dictionary = _shell.quit_menu({"kind": "open", "context": ctx})
	print("GAME QUIT open: ", JSON.stringify(r), " context ", JSON.stringify(ctx))
	_quit_log.append("open")
	if not r.get("ok", false):
		printerr("GAME QUIT the quit menu did not open: ", r.get("error", ""))
		return
	_quit_open = true
	var multiplayer: bool = ctx.get("mode", 2) == 1 or ctx.get("mode", 2) == 5
	if not multiplayer:
		_world.set_paused(true)
		_quit_paused = true


## the menu's close (RW 0x9216B1) and dtor (RW 0x921904): without an Exit a paused game goes on
func _close_quit_menu() -> void:
	if not _quit_open:
		return
	var r: Dictionary = _shell.quit_menu({"kind": "close"})
	print("GAME QUIT close: ", JSON.stringify(r))
	_quit_open = false
	if _quit_paused:
		_world.set_paused(false)
		_quit_paused = false


func _quit_menu_action(action: String) -> void:
	print("GAME QUIT action: ", action)
	_quit_log.append(action)
	if action == "QuitMenuReturn":
		_close_quit_menu()
	elif action == "QuitMenuForfeit":
		# RW 0x921841: close, then MSG_SELF_DESTRUCT(false) unless the local alliance has won (TheVictoryConditions vslot 0x48)
		var ctx: Dictionary = _world.get_quit_menu_context()
		_close_quit_menu()
		if not ctx.get("allied_victory", false):
			print("GAME QUIT surrender: ", JSON.stringify(_world.self_destruct(false)))
	elif action == "QuitMenuExit":
		call_deferred("_quit_menu_exit")
	elif action == "QuitMenuRestart":
		call_deferred("_restart_game")
	elif action == "QuitMenuOptions":
		# RW 0x91ED91: the options screen over the game (the shell's Options.apt; its Back pops it)
		if not _shell.shell_push("Options.apt"):
			printerr("GAME QUIT Options.apt: ", str(_shell.get_shell_report().errors))


## Exit (RW 0x921904 -> RW 0x625E36(1)): a multiplayer game sends MSG_SELF_DESTRUCT(true) (TheGameInfo vslot 0x50 answers no for a LAN game: S-1062), then
## MSG_CLEAR_GAME_DATA leaves the game to the score screen (RW 0x779E28: clearGameData(true) for a skirmish or a multiplayer game)
func _quit_menu_exit() -> void:
	var ctx: Dictionary = _world.get_quit_menu_context()
	_close_quit_menu()
	var multiplayer: bool = ctx.get("mode", 2) == 1 or ctx.get("mode", 2) == 5
	if multiplayer:
		# the network session sends it with the leave (NetGameSession::finish); this peer's logic is cleared before it would run it (retail runs it in the
		# frame that also clears the game: the score screen is built before the victory rules look again)
		print("GAME QUIT exit self-destruct: ", JSON.stringify(_world.self_destruct(true)))
	_exit_to_score_screen()


## Restart (RW 0x9220DE): the game is cleared without the score screen and the same new-game message starts again (a skirmish keeps its seed: RW 0x922222 reseeds
## only outside mode 2)
func _restart_game() -> void:
	if _last_new_game.is_empty():
		return
	_close_quit_menu()
	_restart_stack = _start_stack
	_world.set_auto_advance(false)
	_net_state = ""
	for n in _game_nodes:
		if is_instance_valid(n):
			remove_child(n)
			n.free()
	_game_nodes.clear()
	if _hud != null:
		remove_child(_hud)
		_hud.free()
		_hud = null
	_hud_installed = false
	while _shell.shell_stack().size() > 0:
		_shell.shell_pop()
		_shell.tick(0.033)
	var r: Dictionary = _world.clear_game_data(false)
	print("GAME QUIT restart clear_game_data: ", JSON.stringify(r))
	_camera.current = false
	_restore_sound_volumes()
	_begin_game(_last_new_game)

## GameLogic::clearGameData(true) (RW 0x7792BC) and the score screen (RW 0x927898 pushes TimeLine.apt): the game is torn down (the worker stopped, objects,
## drawables, audio and the network session released), the HUD goes, the shell shows the score screen with the statistics the logic kept.
func _exit_to_score_screen() -> void:
	if _state != State.PLAYING:
		return
	_state = State.SCORE
	if _quit_open:
		_close_quit_menu()
	_world.set_paused(false)
	_world.set_auto_advance(false)
	# leaving during the seven-second display: the end screen movie goes first (HideEndGame), or GuiFX would stay over the score screen
	if _world.get_end_game_state().get("showing", false):
		var hid: Dictionary = _shell.end_game_screen({"kind": "hide_end_game"})
		print("GAME END hide_end_game on leaving: ", JSON.stringify(hid))
	_net_lobby_again = not _net_state.is_empty() # lane END-2: Continue opens the LAN lobby again
	_net_state = "" # a LAN game's state ends with the game (the next skirmish is not a network game)
	if _dc_shown: # lane MP-2: the disconnect screen goes with the game
		_shell.disconnect_screen({"visible": false})
	_dc_shown = false
	_dc_shot_done = false
	_dc_left_seen = 0
	_dc_quit_handled = false
	_desync_shown = false
	# the HUD and the HUD movie go first (they read the game), then the game data
	for n in _game_nodes:
		if is_instance_valid(n):
			remove_child(n)
			n.free()
	_game_nodes.clear()
	if _hud != null:
		remove_child(_hud)
		_hud.free()
		_hud = null
	_hud_installed = false
	while _shell.shell_stack().size() > 0:
		_shell.shell_pop()
		_shell.tick(0.033)
	var r: Dictionary = _world.clear_game_data(true)
	_score_report = r
	print("GAME END clear_game_data: ", JSON.stringify(r))
	_camera.current = false
	if _end_message_label != null:
		_end_message_label.text = ""
	_restore_sound_volumes()
	if not r.get("ok", false):
		_fail("clear_game_data: " + str(r.get("error", "")))
		return
	if not _shell.set_score_screen_from_world(_world):
		_fail("the score screen has no data")
		return
	if not _shell.shell_push("TimeLine.apt"):
		_fail("the shell refused TimeLine.apt: " + str(_shell.get_shell_report().errors))
		return
	_audio.stop_music(false)
	_audio.play_misc("ScoreScreenMusic") # MiscAudio ScoreScreenMusic
	_install_graph_overlay()


## AptTimeLine::RenderGraph is a device-drawn component (S-1063): the lines of the graph the movie's _graphMode picks, drawn over the movie's graph area.
class GraphOverlay extends Control:
	var shell: Node2D
	var graph := {}
	var rect := Rect2()
	func _draw() -> void:
		if graph.is_empty() or rect.size.x <= 0.0:
			return
		var lines: Array = graph.get("lines", [])
		for pass_i in 2:
			for l in lines:
				var c := Color.hex(0xFF000000 | int(l.color))
				var pts: PackedVector2Array = l.points
				if pts.size() < 2:
					continue
				var out := PackedVector2Array()
				var stride := maxi(1, pts.size() / 600)
				for i in range(0, pts.size(), stride):
					var p: Vector2 = pts[i]
					out.append(rect.position + Vector2(p.x * rect.size.x, (1.0 - clampf(p.y, 0.0, 1.0)) * rect.size.y))
				var last: Vector2 = pts[pts.size() - 1]
				out.append(rect.position + Vector2(last.x * rect.size.x, (1.0 - clampf(last.y, 0.0, 1.0)) * rect.size.y))
				# RW 0x924DD1 twice: width 3 with alpha, then width 1 opaque
				if pass_i == 0:
					draw_polyline(out, Color(c, 0.35), 3.0, true)
				else:
					draw_polyline(out, c, 1.0, true)
				if pass_i == 1:
					var marks: PackedVector2Array = l.get("marks", PackedVector2Array())
					for m in marks:
						draw_rect(Rect2(rect.position + Vector2(m.x * rect.size.x, (1.0 - clampf(m.y, 0.0, 1.0)) * rect.size.y) - Vector2(4, 4), Vector2(8, 8)), c, false, 2.0)


func _install_graph_overlay() -> void:
	if _graph_layer == null:
		_graph_layer = CanvasLayer.new()
		_graph_layer.layer = 80
		add_child(_graph_layer)
	_graph_node = GraphOverlay.new()
	_graph_node.shell = _shell
	_graph_node.set_anchors_preset(Control.PRESET_FULL_RECT)
	_graph_node.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_graph_layer.add_child(_graph_node)


var _graph_path := ""
var _graph_area := ""

## the movie instance the graph is drawn in: the first of the retail instance names that exists (TimeLine.apt names GridArea / GridRender)
func _find_graph_instance(level: int) -> void:
	for p in ["Main.Axis.GridArea.GridRender", "Main.Axis.GridArea"]:
		var info: Dictionary = _shell.instance_info(level, p)
		if info.get("found", false):
			_graph_path = p
			return


func _score_tick() -> void:
	if _graph_node == null:
		return
	var level: int = _shell.shell_top_level()
	if _graph_path.is_empty():
		_find_graph_instance(level)
	var g: Dictionary = _shell.get_timeline_graph("", _graph_path)
	if not g.get("ok", false):
		return
	_graph_node.graph = g
	var info: Dictionary = _shell.instance_info(level, _graph_path) if not _graph_path.is_empty() else {}
	# the graph is the timeline page's: on the statistics page (the Axis clip hidden) nothing is drawn
	var shown := true
	for p in ["Main", "Main.Axis", "Main.Axis.GridArea", _graph_path]:
		var vi: Dictionary = _shell.instance_info(level, p)
		if not vi.get("found", false) or not vi.get("visible", true):
			shown = false
	_graph_node.visible = shown
	if info.get("found", false) and info.has("bounds"):
		var bounds: Rect2 = info.bounds
		var a: Vector2 = _shell.stage_to_window(bounds.position)
		var b: Vector2 = _shell.stage_to_window(bounds.end)
		_graph_node.rect = Rect2(a, b - a)
	_graph_node.queue_redraw()


## AptTimeLine::OnButtonContinue (RW 0x925699): for a skirmish or LAN game (types 2 / 3) the shell shows again the screens it held when the game started
## (TheShell vslot 0x138 + 0x9C, RW 0x925798 .. 0x9257CC): MainMenu and Skirmish for a skirmish; a LAN game here is started without the shell's LAN screens
## (MP-1), so its peers open the LAN lobby again (GameNetwork/LANLobby: the host hosts, the joiner joins)
func _score_continue() -> void:
	if _state != State.SCORE or _continuing:
		return # a second Continue the shell's ticks below deliver is the same press
	_continuing = true
	_continue_pressed = true
	if _graph_node != null:
		_graph_node.queue_free()
		_graph_node = null
	while _shell.shell_stack().size() > 0:
		_shell.shell_pop()
		_shell.tick(0.033)
	_audio.stop_music(false)
	var screens: PackedStringArray = _start_stack if _start_stack.size() > 0 else PackedStringArray(["MainMenu.apt"])
	for screen in screens:
		_shell.shell_push(screen) # over a screen that is shown the push waits for its closing transition (Shell::push, pending)
		var ticks := 0
		while (_shell.shell_stack().size() == 0 or _shell.shell_stack()[-1] != screen) and ticks < 300:
			_shell.tick(0.033)
			ticks += 1
		if _shell.shell_stack().size() == 0 or _shell.shell_stack()[-1] != screen:
			_fail("the shell refused %s: %s" % [screen, str(_shell.get_shell_report().errors)])
			return
	_audio.play_shell_music(false)
	_menu_revealed = false
	_frames = 0
	_state = State.MENU
	_continuing = false
	print("GAME END back to the shell: ", str(_shell.shell_stack()))
	if _net_lobby_again:
		_net_lobby_again = false
		_net_open() # the LAN lobby again


## lane UI-2: --options-shot: the main menu's Options screen (with the OpenBFME Soft particles box) saved to FILE, then quit
func _options_shot_tick() -> void:
	_options_shot_frames += 1
	if _options_shot_frames == 30:
		if not _shell.shell_push("Options.apt"):
			_fail("--options-shot: Options.apt: " + str(_shell.get_shell_report().errors))
	elif _options_shot_frames == 150:
		var image := get_viewport().get_texture().get_image()
		print("GAME options shot %s -> %s" % [_options_shot, error_string(image.save_png(_options_shot))])
		get_tree().quit(0)


## lane UI-2: --end-capture: the end screen's first five seconds, every rendered frame
func _capture_end_frames() -> void:
	DirAccess.make_dir_recursive_absolute(_end_capture)
	var start := Time.get_ticks_msec()
	var times := PackedStringArray()
	var frames: Array[Image] = []
	while Time.get_ticks_msec() - start < 5000:
		await RenderingServer.frame_post_draw
		var image := get_viewport().get_texture().get_image()
		image.resize(image.get_width() / 2, image.get_height() / 2, Image.INTERPOLATE_BILINEAR) # half size in memory, written afterwards
		frames.append(image)
		times.append(str(Time.get_ticks_msec() - start))
	var n := frames.size()
	for i in n:
		frames[i].save_png("%s/f%04d.png" % [_end_capture, i])
	var f := FileAccess.open(_end_capture + "/times.txt", FileAccess.WRITE)
	f.store_string("\n".join(times))
	print("GAME END capture: %d frames in %s" % [n, _end_capture])


func _save_named(name: String) -> void:
	if _screens.is_empty():
		return
	DirAccess.make_dir_recursive_absolute(_screens)
	var image := get_viewport().get_texture().get_image()
	var path := "%s/%s.png" % [_screens, name]
	print("GAME screenshot %s -> %s" % [path, error_string(image.save_png(path))])


## --end: the scripted end of a game (see the header)
func _run_end() -> void:
	var faction := _faction.trim_prefix("Faction").to_lower()
	var eco: Dictionary = _world.get_economy()
	var enemy := ""
	for p in eco.players:
		if p.playable and p.name != _local_name:
			enemy = p.name
	if enemy.is_empty():
		_fail("no enemy player")
		return
	var loser := _local_name if _end_lose else enemy
	var winner := enemy if _end_lose else _local_name
	print("GAME END: %s destroys %s" % [winner, loser])
	# everything of the loser but its fortress goes (a test hook), then the winner's fortress lands the last hit on it: the last structure falls
	var objs: Array = _world.get_player_objects(loser)
	var fortress := -1
	for o in objs:
		if o.commandcenter:
			fortress = o.id
	var source := -1
	for o in _world.get_player_objects(winner):
		if o.structure:
			source = o.id
			break
	var keep := ""
	for o in objs:
		if o.id == fortress:
			keep = o.template
	if fortress >= 0 and _hud != null and _hud.has_method("camera_look_at"):
		for o in objs:
			if o.id == fortress:
				_hud.camera_look_at(Vector2(o.x, o.y))
	var k: Dictionary = _world.debug_kill_player_objects(loser, keep)
	print("GAME END killed: ", k)
	await _wait_seconds(3.0)
	# whatever the loser started in the meantime (a structure being built) goes too, so the fortress is the last structure that counts
	k = _world.debug_kill_player_objects(loser, keep)
	print("GAME END killed: ", k)
	await _wait_seconds(0.5)
	if fortress >= 0:
		var hit: Dictionary = _world.debug_damage_object(fortress, 1.0e7, source)
		print("GAME END last hit: ", hit)
	# the end screen: shown by the script (victory) or at once (the local defeat)
	var waited := 0.0
	while waited < 20.0 and not _world.get_end_game_state().get("shown", false):
		await _wait_seconds(0.25)
		waited += 0.25
	var st: Dictionary = _world.get_end_game_state()
	print("GAME END state: ", JSON.stringify(st))
	if not st.get("shown", false):
		print("GAME END victory report: ", JSON.stringify(_world.get_victory_report()))
		print("GAME END loser objects: ", JSON.stringify(_world.get_player_objects(loser)))
		_fail("the end screen was not shown")
		return
	_end_shown = true
	if not _end_capture.is_empty():
		await _capture_end_frames()
	await _wait_seconds(2.5)
	_save_named("end1-%s-%s" % [faction, "defeat" if _end_lose else "victory"])
	if _end_esc:
		# Esc during the seven-second display: the end screen movie must go with the game (HideEndGame before the game is cleared)
		print("GAME END Esc while the end screen shows: ", JSON.stringify(_world.get_end_game_state().get("showing", false)))
		# lane END-2: Esc opens the quit menu; its Exit and the confirmation leave the game (the end screen goes with it)
		await _press_escape()
		await _wait_seconds(3.0)
		if not await _click_quit("ExitMission"):
			_fail("no ExitMission button in the quit menu")
			return
		await _wait_seconds(1.5)
		if not await _click_quit("Yes"):
			_fail("no confirmation of the Exit")
			return
		var w := 0.0
		while _state != State.SCORE and w < 10.0:
			await _wait_seconds(0.25)
			w += 0.25
		await _wait_seconds(1.0)
		_save_named("end1-%s-esc-score" % faction)
		print("GAME END after Esc: end screen still showing: %s" % str(_world.get_end_game_state().get("showing", false)))
		await _finish_end_flow(faction)
		return
	# updateEndGame hides it after 7 s
	waited = 0.0
	while waited < 12.0 and not _world.get_end_game_state().get("hidden", false):
		await _wait_seconds(0.25)
		waited += 0.25
	st = _world.get_end_game_state()
	print("GAME END state after the end screen: ", JSON.stringify(st))
	if not st.get("hidden", false):
		_fail("the end screen did not hide")
		return
	await _wait_seconds(1.5)
	_exit_to_score_screen()
	await _finish_end_flow(faction)


## the score screen onwards: the movie's buttons, Continue, the main menu and a second game
func _finish_end_flow(faction: String, prefix := "end1") -> void:
	if _state != State.SCORE:
		return
	await _wait_seconds(4.0)
	_save_named("%s-%s-score" % [prefix, faction])
	var sr: Dictionary = _shell.get_shell_report()
	print("GAME END score screen notes: ", JSON.stringify(sr.get("notes", {})))
	print("GAME END score screen errors: ", JSON.stringify(sr.get("errors", [])))
	var lvl: int = _shell.shell_top_level()
	print("GAME END graph: ", JSON.stringify(_shell.get_timeline_graph("", _graph_path).get("axis", {})), " at ", _graph_path)
	# Skip ends the reveal (then Continue takes its place); the other tabs of the timeline are the movie's own buttons
	if _shell.find_button(lvl, "Main.MainButtons.Skip").found:
		await _click_button(lvl, "Main.MainButtons.Skip")
		await _wait_seconds(2.0)
	for tab in ["Units", "Structures", "Resources", "FinalScore"]:
		if _shell.find_button(lvl, "Main.TabButtons." + tab).found:
			# the tab bar shows the other tabs once the reveal is over (the movie hides the current one); a slow machine needs a moment more
			for attempt in 4:
				var w := 0.0
				while w < 6.0 and not _shell.find_button(lvl, "Main.TabButtons." + tab).get("hit", "").ends_with("TabButtons.%s.Button" % tab) and _shell.get_member(lvl, _graph_path, "_graphMode").get("value", "") != tab:
					await _wait_seconds(0.25)
					w += 0.25
				await _click_button(lvl, "Main.TabButtons." + tab)
				await _wait_seconds(2.0)
				if _shell.get_member(lvl, _graph_path, "_graphMode").get("value", "") == tab:
					break
			print("GAME END tab %s: graph mode %s (member %s)" % [tab, _shell.get_timeline_graph("", _graph_path).get("mode", ""), JSON.stringify(_shell.get_member(lvl, _graph_path, "_graphMode"))])
			_save_named("%s-%s-score-%s" % [prefix, faction, tab.to_lower()])
	# More opens the statistics page; its Continue leaves (AptTimeLine::OnButtonContinue)
	if _shell.find_button(lvl, "Main.MainButtons.More").found:
		await _click_button(lvl, "Main.MainButtons.More")
		await _wait_seconds(2.5)
		# lane END-2: the statistics page lists the rows of RW 0x9CDEC1 in the StatsList gadget
		var stats: Dictionary = _shell.get_timeline_stats()
		var listed: Array = stats.get("listed", [])
		print("GAME END stats: shown %s, fills %d, columns %d, %d listed rows" % [str(stats.get("shown", [])), stats.get("fills", 0), stats.get("columns", 0), listed.size()])
		print("GAME END stats list: size %s, column widths %s, row heights %s, length %s, display height %s" % [str(stats.get("size")), str(stats.get("column_widths")),
			str(stats.get("row_heights", []).slice(0, 8)), str(stats.get("list_length")), str(stats.get("display_height"))])
		for row in listed.slice(0, 12):
			var cells := []
			for c in row:
				cells.append("%s#%08x" % [c.text, c.color])
			print("GAME END stats row: ", " | ".join(cells))
		_save_named("%s-%s-score-stats" % [prefix, faction])
	var clicked := false
	for attempt in 6:
		if await _click_button(lvl, "Main.MainButtons.Continue"):
			clicked = true
		await _wait_seconds(1.5)
		if _state != State.SCORE:
			break
	if not clicked:
		_fail("no Continue button on the score screen")
		return
	var w := 0.0
	while _state != State.MENU and w < 10.0:
		await _wait_seconds(0.25)
		w += 0.25
	if _state != State.MENU:
		_fail("Continue did not return to the menu")
		return
	await _step(LEVEL_MAIN_MENU_FRAMES + 70)
	print("GAME END Continue showed: ", str(_shell.shell_stack()))
	_save_named("%s-menu" % prefix)
	# the second game: the same scripted path through the lobby
	if not await _auto_lobby_and_start():
		return
	await _wait_seconds(3.0)
	var gs: Dictionary = _world.get_stats()
	_second_game_ok = gs.frame > 5
	print("GAME END second game: logic frame %d, %d objects, games started %d" % [gs.frame, gs.objects, _games_started])
	print("GAME END RESULT: end screen %s, score screen %s, continue %s, second game %s" % [str(_end_shown), str(not _score_report.is_empty()),
		str(_continue_pressed), str(_second_game_ok)])
	get_tree().quit(0 if (_second_game_ok and _continue_pressed and _games_started == 2) else 1)


func _wait_seconds(sec: float) -> void:
	var until := Time.get_ticks_msec() + int(sec * 1000.0)
	while Time.get_ticks_msec() < until:
		await get_tree().process_frame


# ---- lane SMOOTH-1: the render frame rate of the live game ------------------------------------------------------------------------------------------------

func _perf_frame(delta: float) -> void:
	var now := Time.get_ticks_usec()
	if _perf_prev_us == 0:
		_perf_prev_us = now
		_perf_last = now
		return
	_perf_times.append((now - _perf_prev_us) / 1000.0)
	_perf_deltas.append(delta * 1000.0) # the (smoothed) delta the frame's animation and interpolation advanced by
	_perf_prev_us = now
	var t: Dictionary = _world.get_frame_timings() if _world.has_method("get_frame_timings") else {}
	if _perf and _perf_times.back() > 50.0: # lane PERF-1 r2: a long frame and what the C++ side of it spent
		print("GAME HITCH %.1f ms at logic frame %d: %s" % [_perf_times.back(), _world.get_frame(), JSON.stringify(t)])
	for k in ["logic_ms", "sync_ms", "streak_ms", "dyn_pose_ms", "dyn_upload_ms", "main_cpu_ms"]:
		if not _perf_parts.has(k):
			_perf_parts[k] = 0.0
		_perf_parts[k] += t.get(k, 0.0)
	if _hud != null and _hud.has_method("get_frame_timings"):
		var h: Dictionary = _hud.get_frame_timings()
		_perf_parts["hud_update_ms"] = _perf_parts.get("hud_update_ms", 0.0) + h.get("hud_update_ms", 0.0)
	if _show_fps:
		if _fps_label == null:
			var layer := CanvasLayer.new()
			layer.layer = 100
			add_child(layer)
			_fps_label = Label.new()
			_fps_label.position = Vector2(8, 4)
			_fps_label.add_theme_color_override("font_outline_color", Color.BLACK)
			_fps_label.add_theme_constant_override("outline_size", 4)
			layer.add_child(_fps_label)
		_fps_label.text = "%d fps  %.1f ms" % [Engine.get_frames_per_second(), _perf_times.back()]
	if _perf_cpu0 < 0.0:
		_perf_cpu0 = _main_thread_cpu_ms()
	if now - _perf_last < 5000000:
		return
	var n := _perf_times.size()
	var cpu1 := _main_thread_cpu_ms()
	var cpu_per_frame := (cpu1 - _perf_cpu0) / maxf(n, 1) if _perf_cpu0 >= 0.0 else -1.0
	_perf_cpu0 = cpu1
	if _perf:
		var v := _perf_times.duplicate()
		v.sort()
		var sum := 0.0
		for x in v:
			sum += x
		var parts := ""
		for k in _perf_parts.keys():
			parts += " %s=%.2f" % [k, _perf_parts[k] / maxf(n, 1)]
		var dv := _perf_deltas.duplicate()
		dv.sort()
		var low_n := maxi(1, n / 100) # lane PERF-1: the 1% low (the frame rate of the slowest 1% of the frames)
		var low_sum := 0.0
		for i in range(n - low_n, n):
			low_sum += v[i]
		print("GAME PERF main thread CPU %.2f ms per frame" % cpu_per_frame)
		print("GAME PERF frames=%d fps=%.1f low1%%=%.1f frame_ms mean %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f (engine delta p5 %.2f p50 %.2f p95 %.2f) objects=%d | mean%s" % [n,
			1000.0 * n / maxf(sum, 0.001), 1000.0 * low_n / maxf(low_sum, 0.001), sum / maxf(n, 1), v[int(0.5 * (n - 1))], v[int(0.95 * (n - 1))], v[int(0.99 * (n - 1))], v[n - 1],
			dv[int(0.05 * (n - 1))], dv[int(0.5 * (n - 1))], dv[int(0.95 * (n - 1))], _world.get_object_count(), parts])
	_perf_times.clear()
	_perf_deltas.clear()
	_perf_parts.clear()
	_perf_last = now
# ---- lane MP-1: a LAN game ---------------------------------------------------------------------------------------------------------------------

func _net_open() -> void:
	var r: Dictionary
	if _net_host > 0:
		var slots: Array = []
		var i := 0
		for spec in _net_slots.split(";"):
			var f := spec.split(":")
			var states := {"human": 6, "easy": 2, "medium": 3, "hard": 4, "brutal": 5}
			slots.append({"state": states[f[0]], "name": ("Human%d" % i) if f[0] == "human" else ("Computer%d" % i), "faction": f[1], "color": i,
				"start_pos": int(f[2]), "team": int(f[3]), "player_template": -1})
			i += 1
		while slots.size() < 8:
			slots.append({"state": 1, "name": "", "faction": "", "color": -1, "start_pos": -1, "team": -1, "player_template": -1}) # SLOT_CLOSED
		var message := {"mode": "skirmish", "map": _map_key, "map_crc": 0, "map_size": 0, "seed": _seed if _seed != 0 else Time.get_ticks_msec(), "starting_cash": 1500, "slots": slots}
		r = _world.net_host(_net_host, message, _net_run_ahead, _net_crc)
	else:
		r = _world.net_join(_net_join, _net_name)
	print("GAME NET open: ", r)
	if not r.ok:
		_fail("network: " + str(r.get("error", "")))
		return
	_net_state = "lobby"


func _net_poll_lobby() -> void:
	var p: Dictionary = _world.net_poll()
	if p.state == "error":
		_fail("network lobby: " + str(p.error))
	elif p.state == "started":
		_net_state = "loading"
		print("GAME NET lobby started: local slot %d, run-ahead %d, CRC interval %d" % [p.local_slot, p.run_ahead, p.crc_interval])
		_begin_game(p.message)


func _net_begin() -> void:
	var opts := {"script": _net_script, "desync_dir": OS.get_user_data_dir()}
	if not _net_record.is_empty():
		opts["record"] = _net_record
	elif _record:
		opts["record"] = _last_replay_path() # lane MP-2: every LAN game is recorded
	if _net_disconnect_ms >= 0:
		opts["disconnect_ms"] = _net_disconnect_ms
	if _net_player_timeout_ms >= 0:
		opts["player_timeout_ms"] = _net_player_timeout_ms
	var r: Dictionary = _world.net_begin(opts)
	print("GAME NET begin: ", r)
	if not r.ok:
		_fail("network: " + str(r.get("error", "")))
		return
	_net_state = "playing"
	if _net_overlay:
		var layer := CanvasLayer.new()
		layer.layer = 100
		add_child(layer)
		_net_label = Label.new()
		_net_label.position = Vector2(12, 8)
		_net_label.add_theme_font_size_override("font_size", 17)
		_net_label.add_theme_color_override("font_color", Color(1, 1, 0.4))
		_net_label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
		_net_label.add_theme_constant_override("outline_size", 6)
		layer.add_child(_net_label)


func _net_tick() -> void:
	var st: Dictionary = _world.net_status()
	if not st.get("active", false):
		return
	if _net_label:
		var waiting := "" if st.waiting_slots.is_empty() else "  waiting for %s" % str(st.waiting_slots)
		_net_label.text = "%s  frame %d  hash %08X  CRC checks %d  desyncs %d%s" % ["HOST" if _net_host > 0 or _lan_mode == "host" else "JOIN", st.frame, st.hash & 0xFFFFFFFF,
			st.crc_checks_passed, st.desyncs.size(), waiting]
	if st.frame / 50 != _net_last_print:
		_net_last_print = st.frame / 50
		print("GAME NET frame %d hash %08X crc_checks %d desyncs %d stalled %d" % [st.frame, st.hash & 0xFFFFFFFF, st.crc_checks_passed, st.desyncs.size(), st.stalled_frames])
	if not _net_capture.is_empty():
		# the window as drawn after the frame's own _process (its overlay shows the frame): saved on the next _process
		if _net_captured >= 0:
			get_viewport().get_texture().get_image().save_png("%s/f%05d.png" % [_net_capture, _net_captured])
			_net_captured = -1
		if st.frame != _net_last_frame:
			_net_last_frame = st.frame
			_net_captured = st.frame
	if not _net_shot.is_empty() and st.frame >= _net_shot_frame:
		print("GAME NET shot %s at frame %d -> %s" % [_net_shot, st.frame, error_string(get_viewport().get_texture().get_image().save_png(_net_shot))])
		_net_shot = ""
	for d in st.desyncs:
		printerr(d)
	if not st.desyncs.is_empty() and not _desync_shown:
		_desync_shown = true
		_show_desync_box(st.get("desync_dumps", []))
	_net_disconnect_tick(st.get("disconnect", {}))
	if _state != State.PLAYING:
		return
	if _net_frames > 0 and st.frame >= _net_frames:
		print("GAME NET status: ", JSON.stringify(st))
		if _net_end:
			if not _net_end_started and _games_started == 1:
				_net_end_started = true
				_net_end_flow(st.desyncs.size())
			return
		_world.net_finish()
		get_tree().quit(2 if st.desyncs.size() > 0 else 0)


## lane MP-2: the disconnect screen (DisconnectScreen.apt through the shell's window manager), its buttons, the left-game notices and the local player's quit
func _net_disconnect_tick(dc: Dictionary) -> void:
	if dc.is_empty():
		return
	var visible: bool = dc.get("visible", false)
	if visible or _dc_shown:
		var res: Dictionary = _shell.disconnect_screen(dc)
		if visible and not _dc_shown:
			_dc_since = Time.get_ticks_msec()
			print("GAME NET disconnect screen: ", JSON.stringify(res))
		elif not visible:
			print("GAME NET disconnect screen off")
		_dc_shown = visible
	if visible and _net_kick:
		var row := 0
		for r in dc.rows:
			if r.used and r.kick:
				_world.net_disconnect_kick(row)
			row += 1
	if visible and not _net_shot_disconnect.is_empty() and not _dc_shot_done and Time.get_ticks_msec() - _dc_since > 2000:
		_dc_shot_done = true
		get_viewport().get_texture().get_image().save_png(_net_shot_disconnect)
		print("GAME NET disconnect screenshot: ", _net_shot_disconnect)
		var rep: Dictionary = _shell.get_shell_report()
		print("GAME NET disconnect screen state: ", JSON.stringify(_shell.disconnect_screen(dc)), " notes ", JSON.stringify(rep.get("note_lines", [])), " calls without function ", JSON.stringify(rep.get("calls_without_function", [])))
	for a in _shell.take_disconnect_actions():
		print("GAME NET disconnect button: ", JSON.stringify(a))
		if a.kind == "kick":
			_world.net_disconnect_kick(a.row)
		elif a.kind == "quit":
			_world.net_disconnect_quit()
	var left: Array = dc.get("left", [])
	while _dc_left_seen < left.size():
		_show_end_message({"text": "Network:PlayerLeftGame", "name": left[_dc_left_seen]}) # RW 0x8D57D8
		_dc_left_seen += 1
	if dc.get("quit_requested", false) and not _dc_quit_handled:
		_dc_quit_handled = true
		print("GAME NET leaving the game: ", dc.get("quit_reason", ""))
		if _dc_shown:
			_shell.disconnect_screen({"visible": false})
			_dc_shown = false
		_exit_to_score_screen()


## lane MP-2: replays. The folder (retail: Replays\ of the user data, RW 0xC2EEF0) and the file every game is recorded to (retail: GUI:LastReplay)
func _replay_dir() -> String:
	var dir := OS.get_user_data_dir().path_join("Replays")
	DirAccess.make_dir_recursive_absolute(dir)
	return dir


func _last_replay_name() -> String:
	var t: Dictionary = _shell.fetch_text("GUI:LastReplay")
	var name: String = t.get("text", "")
	return name if t.get("found", false) and not name.is_empty() else "Last Replay"


func _last_replay_path() -> String:
	return _replay_dir().path_join(_last_replay_name() + ".replay")


## The main menu's Load Replay (RW 0x91C36C -> RW 0x816655(mode 2, Replay)): SaveLoad.apt's replay page with the folder's files
func _open_replay_menu() -> void:
	var rows: Array = []
	for f in _world.list_replays(_replay_dir()):
		var dt: Dictionary = Time.get_datetime_dict_from_unix_time(int(f.get("modified", 0)) + int(Time.get_time_zone_from_system().get("bias", 0)) * 60) # local time
		var map_key: String = str(f.get("map", ""))
		rows.append({"path": f.path, "name": str(f.file).get_basename(), "map": map_key.get_file().get_basename(),
			"date": "%02d/%02d/%04d" % [dt.month, dt.day, dt.year], "time": "%02d:%02d" % [dt.hour, dt.minute], "compatible": f.get("profile_ok", false)})
	_shell.set_save_load({"mode": 2, "flags": 4, "last_replay": _last_replay_name(), "replays": rows})
	print("GAME replay menu: %d replays in %s" % [rows.size(), _replay_dir()])
	if not _shell.shell_push("SaveLoad.apt"):
		printerr("GAME the shell refused SaveLoad.apt: ", _shell.get_shell_report().errors)


## A replay played back: prepared like a new game (the recorded lobby message), the load screen, then the game with the recording as its frame driver
func _begin_replay(path: String) -> void:
	print("GAME replay: ", path)
	if _state == State.SCORE:
		_score_continue()
		await _step(LEVEL_MAIN_MENU_FRAMES)
	var prep: Dictionary = _world.prepare_replay(path)
	if not prep.ok:
		_fail("prepare_replay: " + "\n".join(prep.errors))
		return
	_last_local_slot = int(prep.get("local_slot", -1))
	_state = State.LOADING
	var cards: Dictionary = _shell.set_load_screen_from_game(prep.resolved)
	if cards.has("error"):
		_fail("load screen: " + cards.error)
		return
	_shell_load_screen_push()
	for i in 4:
		await get_tree().process_frame
	_shell.auto_process = false
	var rep: Dictionary = _world.start_replay({"progress": Callable(self, "_on_load_progress")})
	_report = rep
	if not rep.ok:
		for e in rep.errors.slice(0, 20):
			print("  replay load error: ", e)
		_fail("start_replay failed")
		return
	print("GAME replay loaded: %d frames, %d commands" % [rep.final_frame, rep.commands])
	_replay_active = true
	_enter_game(rep)
	if _replay_speed != 1.0:
		_world.set_time_scale(_replay_speed)


func _replay_tick() -> void:
	var st: Dictionary = _world.replay_status()
	if not st.get("active", false):
		return
	if not _replay_watch.is_empty() and int(st.frame) >= _replay_watch_next:
		_replay_watch_next = int(st.frame) + 50
		_print_replay_watch(int(st.frame))
	if st.frame / 100 != _replay_last_print:
		_replay_last_print = st.frame / 100
		print("GAME REPLAY frame %d of %d, hashes compared %d, mismatches %d" % [st.frame, st.final_frame, st.hashes_compared, st.mismatches])
	if st.finished:
		_replay_active = false
		print("GAME REPLAY finished: ", JSON.stringify(st))
		if _replay_check or _replay_menu_test:
			print("GAME REPLAY RESULT: %s" % ("ok" if st.mismatches == 0 and st.hashes_compared > 0 else "FAILED"))
			get_tree().quit(0 if st.mismatches == 0 and st.hashes_compared > 0 else 1)


## lane QA-2: --replay-watch: the watched player's objects of one template, their production and the player's money / command points
func _print_replay_watch(frame: int) -> void:
	var parts := _replay_watch.split(":")
	if parts.size() != 2:
		return
	var index := -1
	var eco_line := ""
	for p in _world.get_economy().get("players", []):
		if p.name == parts[0]:
			index = int(p.index)
			eco_line = "money %d, command points %s / %s (available %s)" % [int(p.money), str(p.cp_used), str(p.cp_limit), str(p.cp_available)]
	for o in _world.get_player_objects(parts[0]):
		if o.template != parts[1]:
			continue
		var full: Dictionary = _world.get_object(int(o.id))
		var prod: Dictionary = _world.get_production(index, int(o.id))
		var queue := []
		for q in prod.get("queue", []):
			queue.append("%s %.1f%%" % [q.get("template", "?"), float(q.get("percent", 0.0))])
		print("QA REPLAY WATCH frame %d: %s %d at (%.0f, %.0f) conditions %s queue %s | %s" % [frame, o.template, int(o.id), o.x, o.y,
			str(full.get("conditions", [])), str(queue), eco_line])


## lane MP-2: RW 0x6290C7's message box after the first CRC mismatch of a game (type 4, GUI:DesyncTitle / GUI:DesyncText); the dump's path follows the text.
## The box is drawn by the presentation layer (the retail message box movie is not ported: S-1124); the game goes on.
func _show_desync_box(dumps: Array) -> void:
	var title: String = _shell.fetch_text("GUI:DesyncTitle").text
	var text: String = _shell.fetch_text("GUI:DesyncText").text
	if not dumps.is_empty():
		text += "\n\n" + str(dumps[0])
	print("GAME NET desync message: %s / %s" % [title, text])
	var layer := CanvasLayer.new()
	layer.layer = 95
	add_child(layer)
	var panel := PanelContainer.new()
	panel.anchor_left = 0.25
	panel.anchor_right = 0.75
	panel.anchor_top = 0.3
	panel.offset_bottom = 0
	layer.add_child(panel)
	var box := VBoxContainer.new()
	panel.add_child(box)
	var t := Label.new()
	t.text = title
	t.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	t.add_theme_font_size_override("font_size", 24)
	box.add_child(t)
	var body := Label.new()
	body.text = text
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(body)
	var ok := Button.new()
	ok.text = _shell.fetch_text("GUI:Ok").text
	ok.pressed.connect(func(): layer.queue_free())
	box.add_child(ok)
	_game_nodes.append(layer)
	if not _net_shot_desync.is_empty():
		await get_tree().create_timer(1.0).timeout
		get_viewport().get_texture().get_image().save_png(_net_shot_desync)
		print("GAME NET desync screenshot: ", _net_shot_desync)


# lane PERF-1: the main thread's CPU time in ms (Linux: /proc/self/task/<pid>/schedstat, nanoseconds on the CPU; -1 elsewhere): a frame cost that other
# processes' load does not inflate, readable with any build of the extension
func _main_thread_cpu_ms() -> float:
	var f := FileAccess.open("/proc/self/task/%d/schedstat" % OS.get_process_id(), FileAccess.READ)
	if f == null:
		return -1.0
	var parts := f.get_line().split(" ") # /proc files report size 0: read the line, not the "whole" file
	return float(parts[0]) / 1.0e6 if parts.size() > 0 else -1.0


## --net-end (lanes END-1 / END-2): at --net-frames the joiner surrenders through the quit menu (Esc, Forfeit, its confirmation: MSG_SELF_DESTRUCT(false) on
## the lockstep), both peers see the end (the joiner's defeat, the host's victory), leave through the quit menu's Exit to the score screen (type 3), and its
## Continue opens the LAN lobby again: the host hosts, the joiner joins, and a second LAN game starts and runs 20 frames
func _net_end_flow(desyncs: int) -> void:
	var ok := desyncs == 0
	var surrender := not _net_join.is_empty()
	if surrender:
		await _press_escape()
		await _wait_seconds(4.0)
		var st: Dictionary = _shell.quit_menu({"kind": "state"})
		print("GAME NET END quit menu: ", JSON.stringify(st))
		_save_named("end2-lan-quitmenu")
		ok = ok and st.get("popup_type", "") == "Forfeit" and str(st.get("disabled", [])) == str(["Save", "Load"])
		ok = ok and await _click_quit("Restart")
		await _wait_seconds(2.0)
		_save_named("end2-lan-surrender-confirm")
		ok = ok and await _click_quit("Yes")
	var waited := 0.0
	while waited < 40.0 and not _world.get_end_game_state().get("shown", false):
		await _wait_seconds(0.25)
		waited += 0.25
	var es: Dictionary = _world.get_end_game_state()
	print("GAME NET END end screen: shown %s, victory %s, surrendered %s" % [str(es.get("shown", false)), str(es.get("victory_screen", false)), str(surrender)])
	ok = ok and es.get("shown", false) and es.get("victory_screen", false) == (not surrender)
	await _wait_seconds(2.0)
	_save_named("end2-lan-%s" % ("defeat" if surrender else "victory"))
	waited = 0.0
	while waited < 12.0 and not _world.get_end_game_state().get("hidden", false):
		await _wait_seconds(0.25)
		waited += 0.25
	# Exit through the quit menu
	await _press_escape()
	await _wait_seconds(4.0)
	ok = ok and await _click_quit("ExitMission")
	await _wait_seconds(1.5)
	ok = ok and await _click_quit("Yes")
	waited = 0.0
	while _state != State.SCORE and waited < 20.0:
		await _wait_seconds(0.25)
		waited += 0.25
	var sd: Dictionary = _score_report.get("score_screen", {})
	var entries: Array = sd.get("entries", [])
	var local_result: int = entries[0].get("result", -1) if entries.size() > 0 else -1
	print("GAME NET END score screen type %d, entries %d, local result %d, net state '%s'" % [sd.get("type", -1), entries.size(), local_result, _net_state])
	ok = ok and sd.get("type", -1) == 3 and _state == State.SCORE and local_result == (1 if surrender else 0)
	await _wait_seconds(2.0)
	_score_continue() # the LAN lobby again
	waited = 0.0
	while waited < 90.0 and not (_state == State.PLAYING and _world.net_status().get("frame", 0) >= 20):
		await _wait_seconds(0.25)
		waited += 0.25
	var ns: Dictionary = _world.net_status()
	print("GAME NET END next game: local slot %d, net active %s, logic frame %d, games started %d, desyncs %d" % [_last_local_slot, str(ns.get("active", false)),
		ns.get("frame", 0), _games_started, ns.get("desyncs", []).size()])
	ok = ok and _games_started == 2 and ns.get("active", false) and ns.get("frame", 0) >= 20 and ns.get("desyncs", []).size() == 0
	print("GAME NET END RESULT: %s" % ("ok" if ok else "FAILED"))
	_world.net_finish()
	get_tree().quit(0 if ok else 1)


# ---- lane MP-2: the LAN lobby ------------------------------------------------------------------------------------------------------------------

## The main menu's LAN: GameWorld's LAN lobby (bound once, kept for the session) handed to LanLobby.apt
func _open_lan_lobby() -> void:
	var st: Dictionary = _world.lan_status()
	if not st.get("open", false):
		var opts := {"name": _net_name, "run_ahead": _net_run_ahead, "crc_interval": _net_crc}
		if not _lan_targets.is_empty():
			opts["broadcast"] = false
			opts["targets"] = Array(_lan_targets.split(","))
		if _lan_port_base > 0:
			opts["port_base"] = _lan_port_base
		var r: Dictionary = _world.lan_open(opts)
		print("GAME LAN lobby: ", r)
		if not r.ok:
			_fail("LAN lobby: " + str(r.get("error", "")))
			return
	_shell.set_lan(_world)
	if not _shell.shell_push("LanLobby.apt"):
		_fail("the shell refused LanLobby.apt: " + str(_shell.get_shell_report().errors))


## Every player of the LAN game has the start: the game socket and the start go to the network game, which loads like a --net-host / --net-join one
func _lan_game_start() -> void:
	var p: Dictionary = _world.lan_begin()
	if not p.ok:
		_fail("LAN start: " + str(p.get("error", "")))
		return
	_net_state = "loading"
	print("GAME LAN game started: local slot %d, run-ahead %d, CRC interval %d" % [p.local_slot, p.run_ahead, p.crc_interval])
	_begin_game(p.message)


## --lan=host|join: the lobby driven through the movie's own commands and gadget messages
func _lan_script_tick() -> void:
	_lan_wait += 1
	var st: Dictionary = _world.lan_status()
	var stack: PackedStringArray = _shell.shell_stack()
	var top: String = stack[stack.size() - 1] if stack.size() > 0 else ""
	if _lan_phase == 0:
		if _lan_wait > 30:
			print("GAME LAN script: the main menu's LAN")
			_shell.shell_fscommand("AptMainMenu::LAN", "")
			_lan_phase = 1
			_lan_wait = 0
	elif _lan_phase == 1:
		if top == "LanLobby.apt" and _lan_wait > 60:
			if _lan_mode == "host":
				print("GAME LAN script: Create Game")
				_shell.shell_fscommand("AptLanLobby::OnCreateGameBttn", "")
				_lan_phase = 2
				_lan_wait = 0
			elif int(st.get("games", 0)) > 0:
				print("GAME LAN script: Join of the first game")
				var j: Dictionary = _shell.lobby_apply({"join_row": 0})
				if not j.ok:
					printerr("GAME LAN join: ", j.errors)
				_lan_phase = 2
				_lan_wait = 0
	elif _lan_phase == 2:
		if st.get("in_game", false) and _lan_wait > 30:
			var slots: Array = []
			if not _faction.is_empty():
				slots.append({"slot": int(st.slot), "faction": _faction})
			var spec := {"slots": slots}
			if _lan_mode == "host":
				spec["map"] = _map_key
				for i in range(_lan_ai):
					slots.append({"slot": _lan_players + i, "state": 3})
			var a: Dictionary = _shell.lobby_apply(spec)
			print("GAME LAN script: the setup ", JSON.stringify(spec), " -> ", a.ok, " ", a.errors)
			_lan_phase = 3
			_lan_wait = 0
		elif _lan_wait > 600:
			_fail("LAN script: no game after 10 s: " + str(st))
	elif _lan_phase == 3:
		if _lan_mode == "join":
			if st.get("in_game", false) and not st.get("accepted_me", false) and _lan_wait > 60:
				print("GAME LAN script: Accept")
				_shell.shell_fscommand("MpGameSetup::OnReadyPress", str(int(st.slot)))
				_lan_wait = 0
		elif int(st.get("humans", 0)) >= _lan_players and int(st.get("accepted", 0)) >= _lan_players - 1:
			if _lan_wait > 90:
				if not _lan_shot.is_empty() and not _lan_shot_done:
					_lan_shot_done = true
					var image := get_viewport().get_texture().get_image()
					print("GAME LAN lobby shot %s -> %s" % [_lan_shot, error_string(image.save_png(_lan_shot))])
					# lane UI-2: what the lobby's native slots are (map picture, View3D ...) and the shell's notes, for the media runs
					var lr: Dictionary = _shell.get_report()
					print("GAME LAN lobby placeholders ", JSON.stringify(lr.get("placeholders", [])))
					print("GAME LAN lobby shell report ", JSON.stringify(_shell.get_shell_report()))
				print("GAME LAN script: Play Game (%d players)" % int(st.humans))
				_shell.shell_fscommand("AptLanLobby::OnStartGameBttn", "")
				_lan_phase = 4
				_lan_wait = 0
				_lan_start_ms = Time.get_ticks_msec()
		else:
			_lan_wait = 0
	elif _lan_phase == 4:
		if not _lan_shot_start.is_empty() and _lan_start_ms >= 0 and Time.get_ticks_msec() - _lan_start_ms >= 8000:
			_lan_start_ms = -1
			var image := get_viewport().get_texture().get_image()
			print("GAME LAN start shot %s -> %s" % [_lan_shot_start, error_string(image.save_png(_lan_shot_start))])

# ---- lane END-2: the scripted quit menu --------------------------------------------------------------------------------------------------------------

## the button of the quit menu movie whose path ends with `name` (QuitMenu.apt's clips: Resume, Options, Restart, ExitMission ... and the popup's Yes)
func _quit_button(name: String) -> Dictionary:
	var r: Dictionary = _shell.quit_menu({"kind": "state"})
	if not r.get("open", false):
		return {}
	for b in _shell.list_buttons(r.level):
		var path: String = b.path
		if b.hittable and (path.contains("." + name + ".") or path.ends_with("." + name)):
			return b
	return {}


func _click_quit(name: String) -> bool:
	for attempt in 40:
		var b := _quit_button(name)
		if not b.is_empty():
			var p := Vector2(b.x, b.y)
			_mouse_event(p, false, false)
			await _step(2)
			_mouse_event(p, true, true)
			await _step(2)
			_mouse_event(p, false, true)
			await _step(2)
			print("GAME QUIT clicked ", b.path)
			return true
		await _wait_seconds(0.1)
	var r: Dictionary = _shell.quit_menu({"kind": "state"})
	print("GAME QUIT buttons: ", JSON.stringify(_shell.list_buttons(r.get("level", -1))))
	return false


func _press_escape() -> void:
	var ev := InputEventKey.new()
	ev.keycode = KEY_ESCAPE
	ev.pressed = true
	Input.parse_input_event(ev)
	await _step(2)
	var up := InputEventKey.new()
	up.keycode = KEY_ESCAPE
	up.pressed = false
	Input.parse_input_event(up)
	await _step(2)


## --quit: Esc opens the quit menu over the skirmish (paused), Resume closes it, Esc again, Exit and its confirmation leave the game to the score screen, whose
## statistics page and tabs are used, then Continue returns to the Skirmish lobby and a second game starts. --quit-restart: Restart and its confirmation
## start the same game again.
func _run_quit() -> void:
	var faction := _faction.trim_prefix("Faction").to_lower()
	var frame0: int = _world.get_stats().frame
	await _press_escape()
	await _wait_seconds(4.0)
	var st: Dictionary = _shell.quit_menu({"kind": "state"})
	var paused: bool = _world.is_paused()
	await _wait_seconds(0.5)
	var frame_paused: int = _world.get_stats().frame
	print("GAME QUIT opened: %s, paused %s, frame %d -> %d" % [JSON.stringify(st), str(paused), frame0, frame_paused])
	_save_named("end2-quitmenu")
	var ok: bool = st.get("open", false) and paused and st.get("initialized", 0) >= 1
	if _quit_flow == "restart":
		var seed: int = _last_new_game.get("seed", -1)
		ok = ok and await _click_quit("Restart")
		await _wait_seconds(2.0)
		_save_named("end2-quitmenu-restart-confirm")
		ok = ok and await _click_quit("Yes")
		var w := 0.0
		while (_games_started < 2 or _state != State.PLAYING) and w < 60.0:
			await _wait_seconds(0.5)
			w += 0.5
		await _wait_seconds(2.0)
		var gs: Dictionary = _world.get_stats()
		print("GAME QUIT RESULT restart: requests %s, games started %d, same seed %s, frame %d" % [str(_quit_log), _games_started, str(_last_new_game.get("seed", -2) == seed), gs.frame])
		ok = ok and _games_started == 2 and _last_new_game.get("seed", -2) == seed and gs.frame > 3 and _quit_log.has("QuitMenuRestart")
		print("GAME QUIT RESULT: ", "ok" if ok else "FAILED")
		get_tree().quit(0 if ok else 1)
		return
	# Resume
	ok = ok and await _click_quit("Resume")
	await _wait_seconds(1.5)
	var after_resume: Dictionary = _shell.quit_menu({"kind": "state"})
	var f1: int = _world.get_stats().frame
	await _wait_seconds(1.0)
	var resumed: bool = not after_resume.get("open", true) and not _world.is_paused() and _world.get_stats().frame > f1
	print("GAME QUIT resumed: %s (frame %d -> %d)" % [str(resumed), f1, _world.get_stats().frame])
	ok = ok and resumed
	# Esc, Exit, the confirmation
	await _press_escape()
	await _wait_seconds(4.0)
	ok = ok and await _click_quit("ExitMission")
	await _wait_seconds(2.0)
	_save_named("end2-quitmenu-exit-confirm")
	ok = ok and await _click_quit("Yes")
	var waited := 0.0
	while _state != State.SCORE and waited < 20.0:
		await _wait_seconds(0.25)
		waited += 0.25
	print("GAME QUIT exit: state %d, requests %s" % [_state, str(_quit_log)])
	ok = ok and _state == State.SCORE and _quit_log.has("QuitMenuExit") and _quit_log.has("QuitMenuReturn")
	print("GAME QUIT RESULT exit: ", "ok" if ok else "FAILED")
	if not ok:
		get_tree().quit(1)
		return
	await _finish_end_flow(faction, "end2")


# ---- lane QA-1: the scripted player -------------------------------------------------------------------------------------------------------------------

## --qa: the scripted human plays (or, --qa-idle, watches an AI game), then the game is left the way a player leaves it and the score screen's Continue must
## return to the lobby. One QA SUMMARY line carries the player's findings and the flow's.
func _run_qa() -> void:
	if not _net_state.is_empty() or _net_host > 0 or not _net_join.is_empty() or not _lan_mode.is_empty() or not _replay_file.is_empty():
		_fail("QA: the scripted player does not play a network session or a replay")
		return
	if _hud == null:
		_fail("QA: the HUD was not installed")
		return
	var qa: Node = load("res://scripts/qa_scene.gd" if not _qa_scene.is_empty() else "res://scripts/qa_player.gd").new()
	qa.name = "QAPlayer"
	add_child(qa)
	qa.setup(self, _world, _hud, _local_name, {"minutes": _qa_minutes, "screens": _screens, "tag": _qa_tag, "cash": _qa_cash})
	_world.set_time_scale(_qa_speed)
	var started := Time.get_ticks_msec()
	var summary: Dictionary = {}
	if not _qa_scene.is_empty():
		summary = await qa.scene(_qa_scene)
	elif _qa_idle:
		summary = await qa.watch()
	else:
		summary = await qa.play()
	_world.set_time_scale(1.0)
	summary["real_seconds"] = (Time.get_ticks_msec() - started) / 1000.0
	var flow := {"end_screen": false, "end_hidden": false, "auto_score": false, "quit_exit": false, "score_screen": false, "continue_to_lobby": false}
	if summary.get("victory", false) or summary.get("defeat", false):
		var waited := 0.0
		while waited < 20.0 and not _world.get_end_game_state().get("shown", false):
			await _wait_seconds(0.25)
			waited += 0.25
		flow.end_screen = _world.get_end_game_state().get("shown", false)
		await _wait_seconds(2.0)
		_save_named("qa1-%s-end" % _qa_tag)
		waited = 0.0
		while waited < 12.0 and not _world.get_end_game_state().get("hidden", false):
			await _wait_seconds(0.25)
			waited += 0.25
		flow.end_hidden = _world.get_end_game_state().get("hidden", false)
		await _wait_seconds(4.0)
		flow.auto_score = _state == State.SCORE
	if _state == State.PLAYING:
		# the player leaves: Esc, the quit menu's Exit, its confirmation
		await _press_escape()
		await _wait_seconds(2.0)
		var ok: bool = await _click_quit("ExitMission")
		await _wait_seconds(1.5)
		ok = ok and await _click_quit("Yes")
		flow.quit_exit = ok
	var w := 0.0
	while _state != State.SCORE and w < 20.0:
		await _wait_seconds(0.25)
		w += 0.25
	flow.score_screen = _state == State.SCORE
	if flow.score_screen:
		await _wait_seconds(4.0)
		_save_named("qa1-%s-score" % _qa_tag)
		var lvl: int = _shell.shell_top_level()
		for attempt in 6:
			await _click_button(lvl, "Main.MainButtons.Continue")
			await _wait_seconds(1.5)
			if _state != State.SCORE:
				break
		w = 0.0
		while _state != State.MENU and w < 10.0:
			await _wait_seconds(0.25)
			w += 0.25
		await _step(LEVEL_MAIN_MENU_FRAMES + 30)
		var stack: PackedStringArray = _shell.shell_stack()
		flow.continue_to_lobby = _state == State.MENU and stack.size() > 0 and stack[-1] == "Skirmish.apt"
		flow["stack"] = str(stack)
		# lane QA-2: the lobby after the game, at once and once the shell settled (the main menu's buttons must not show through)
		_save_named("qa1-%s-lobby-after" % _qa_tag)
		await _wait_seconds(3.0)
		_save_named("qa1-%s-lobby-after-3s" % _qa_tag)
	var shell_report: Dictionary = _shell.get_shell_report()
	summary["flow"] = flow
	summary["shell_errors"] = shell_report.get("errors", [])
	summary["load_errors"] = _report.get("errors", [])
	summary["load_stops"] = _report.get("stops", [])
	# lane QA-1: the sound side (missing events, files that did not play) as the audio manager counted it over the session
	var ar: Dictionary = _audio.get_report() if _audio != null else {}
	summary["audio"] = {"unknown_events": ar.get("unknown_events", 0), "play_failures": ar.get("play_failures", 0), "played": ar.get("played", 0),
		"missing_hooks": ar.get("missing_hooks", []), "errors": ar.get("errors", []), "unknown_event_names": ar.get("unknown_event_names", {})}
	summary["map"] = _map_key
	summary["faction"] = _faction
	summary["ai"] = _ai
	summary["opponents"] = _opponents
	summary["seed"] = _seed
	print("QA SUMMARY ", JSON.stringify(summary))
	var complete: bool = flow.score_screen and flow.continue_to_lobby
	print("QA RESULT: flow %s, %d issues" % ["complete" if complete else "INCOMPLETE", summary.get("issues", []).size()])
	get_tree().quit(0 if complete else 1)
