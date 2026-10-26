## Lane CAMP-1: the linear campaign flow of the game (scripts/game.gd owns this node). TheLinearCampaignManager's campaigns come from the retail INI
## (GameWorld.get_campaigns: RotWK's legend loads Data\INI\LinearCampaignExpansion1.ini: ANGMAR_CAMPAIGN, ANGMAR_CAMPAIGN_DEMO, ANGMAR_BONUS_CAMPAIGN).
##
##   the main menu's AptMainMenu::Expansion1Campaign (RW 0x91AF8C) starts ANGMAR_CAMPAIGN, AptMainMenu::BonusCampaign (RW 0x91AFAF) ANGMAR_BONUS_CAMPAIGN;
##   the command's first character is the difficulty (RW 0x91C108: 'E' easy, 'H' hard, else normal). A mission: the load screen with the mission's
##   LoadScreenMusicTrack, GameWorld.start_campaign_mission (the map with the campaign rules), the HUD, the script presentation (scripts/campaign_cine.gd
##   in HUD mode: letterbox, captions, fades, notifications, objectives; the script camera only while a cinematic runs). The map's scripts end it
##   (VICTORY_SCREEN / QUICKVICTORY / DEFEAT, GameWorld.campaign_status).
##
## Lane CAMPSAVE-1: what follows the end is RotWK's (GameClient/LinearCampaign.h, GUI/AptScreens/AptCampaignMenu.h; the campaign's state is a
## CampaignProgress the engine steps, GameWorld.campaign_progress_step):
##   * a won mission sets the campaign's won flag (RW 0x5EBAEE); 25 logic frames after the end the end-game timer leaves the game (RW 0x602FFE ->
##     MSG_CLEAR_GAME_DATA; the dispatcher shows the score screen for a campaign, RW 0x779A3D) and the campaign is told the game is over (RW 0x5EAB0A:
##     a won mission moves the index on); the score screen is TimeLine.apt of type 1 (RW 0x927898, single player);
##   * the score screen's Continue ends the screen, whose destructor (RW 0x926E0A, type 1) runs RW 0x927EC6: the last mission won -> the preference
##     BCU = "true" (Options.ini) and TheGlobalData + 0xAF5 (the main menu's BonusCampaign is enabled), then the main menu; otherwise a won mission is
##     auto-saved (RW 0x6DE8F1: "Auto Save for <map>", the AutoSaveList of the Load page) and CampaignMenu.apt is shown, won or lost;
##   * CampaignMenu.apt: Next Mission (the mission after the won one, with its IntroMovie), Replay Mission after a defeat (the same mission without its
##     IntroMovie), Save / Load (SaveLoad.apt's campaign page over the menu: OpenBFME saves of the campaign's state there, the campaign menu save point),
##     Main Menu. A campaign menu save loaded (the auto-save, a save made in the menu) shows the menu again with that state.
##   * AptMainMenu::ContinueCampaign (RW 0x91B50D) loads "00000000.sav" from the Save folder when it exists (RotWK names its auto-save otherwise and its
##     MainMenu.apt has no button for the command: the player continues from Load Game).
##   The sidecar of lanes CAMP-1 .. SAVE-4 (user://Campaign/progress.txt) is migrated once into an auto-save (migrate_sidecar).
##
## lane CAMP-1H: the campaign movies (scripts/movie_player.gd, GameWorld.get_movie): the campaign's OverallCampaignIntroMovie when it starts from the menu
## (INFERENCE: RotWK's caller of it was not located), a mission's IntroMovie and the maps' PLAY_MOVIE_IN_GAME (InGameUI RW 0x69B788 -> RW 0x6D5397).
## Lane CAMP-2: with their VP6 picture (S-2340); a mission's IntroMovie after its loading screen, as LinearCampaignExpansion1.ini's own comment says
## (CAMP-1H played it before the load). --movies=off (game.gd) leaves them out of scripted runs.
## NOT PORTED (stops): the auto-save holds the campaign's state, not retail's GameState snapshot (S-1360); the fade-up (S-1364), the carry-over heroes
## between missions (S-4903). Lane MENU-1: a mission loads behind its LoadScreenImage on RotWK's picture screen (S-3505).
extends Node

const PROGRESS_PATH := "user://Campaign/progress.txt" # lanes CAMP-1 .. SAVE-4's sidecar (read once, migrated into an auto-save)
const END_GAME_TIMER_FRAMES := 25 # RW 0x602FFE: ScriptEngine + 0x1A204 (the end-game timer, logic frames)

var game: Node            # scripts/game.gd
var world: Node3D         # GameWorld
var campaign := ""
var mission := 0
var difficulty := 1
var active := false
var hooks := false        # the start option test_hooks (scripted runs)
var auto_win := false     # --campaign-win: the test hook plays each mission's decisive moment (see _auto_win_tick)
var _ended_at := -1
var _cine: Node
var _win_step := 0
var _win_frame := 0
var begun := 0             # missions started (the scripted checks)
var ended_log: Array = []  # [ "<campaign> <index> <map> <victory|defeat> <frame>" ] for the scripted checks
var movies := true         # lane CAMP-1H: play the campaign movies (--movies=off: no)
var movie: Node            # lane CAMP-1H: scripts/movie_player.gd
# lane CAMPSAVE-1: the campaign's state between and during missions (CampaignProgress: + 0xD0 victorious, + 0xD2 ready, + 0xD3 advanced)
var victorious := false
var campaign_ready := false  # + 0xD2 (Node has its own "ready")
var advanced := false
var score_pending := false  # the score screen of a campaign game is up: its Continue runs RW 0x927EC6
var menu_open := false      # CampaignMenu.apt is up (between missions)
var menu_log: Array = []    # [ "<event> ..." ] for the scripted checks: "menu <campaign> <index> won|lost", "auto_save <path>", "bonus_unlock ..."
var last_auto_save := ""    # the auto-save written last
var last_unlock := {}       # the bonus unlock's answer (AptMenuPlayer.unlock_bonus_campaign: ok, file, error)
var _menu_sub := ""         # "save" / "load": SaveLoad.apt opened over the menu (the menu is shown again when it closes)
var _end_timer_ran := false # the end sequence's own timer asked to leave (EndGameController's clear_game_data request)
var _end_frame := -1


static func difficulty_from_command(argument: String) -> int:
	# RW 0x91C108
	if argument.begins_with("E"):
		return 0
	if argument.begins_with("H"):
		return 2
	return 1


func missions_of(name: String) -> Array:
	for c in world.get_campaigns():
		if String(c.name).to_upper() == name.to_upper():
			return c.missions
	return []


## a main menu command: start a campaign at its first mission
func start(name: String, diff: int) -> void:
	campaign = name
	mission = 0
	difficulty = diff
	victorious = false
	campaign_ready = false
	advanced = false
	print("CAMPAIGN start %s (difficulty %d): %d missions" % [name, diff, missions_of(name).size()])
	# lane PLAY-3 (owner: the menu music played on through the campaign's opening movie). TARGET FACTS: the main menu's campaign start (RW 0x91C262 ->
	# RW 0x91C108) queues RW 0x91BCD2 first: the transition PreParchmentMapFade_StartNew and TheShell's music stop (RW 0x75D9B1: TheAudio RW 0xDE42FC
	# vslot 0x8C(2, 1, 0), a type 2 request: the music of view 2 (the shell), music system 1); once the transition has ended it clears music system 1
	# of the views 1, 2 and 0 (vslot 0x98, type 7 requests). Only then come Map_Roll (RW 0x91BC1E, not ported: S-3303) and the campaign (RW 0x91BE64).
	# The menu itself leaves the music alone for the two campaign commands (RW 0x91B43C / 0x91B441 skip RW 0x75D9B1 for 0xD / 0xE). In retail the
	# transition covers the stop before any movie opens; the port has no transition (S-3303), so the music stops at once (review r1: a fade left the
	# shell voice playing under the narration for its two seconds)
	var was := str(game._audio.get_music_track())
	game._audio.stop_music(false)
	print("CAMPAIGN shell music stopped (RW 0x91BCD2 -> RW 0x75D9B1): '%s' before, '%s' now" % [was, game._audio.get_music_track()])
	print("CAMPAIGN STOP [S-3303] the main menu's campaign start: the transition PreParchmentMapFade_StartNew and the Map_Roll movie (RW 0x91BC1E) are not played")
	for c in world.get_campaigns():
		if String(c.name).to_upper() == name.to_upper():
			await play_movie(String(c.intro_movie))
	await begin_mission()


## lane CAMP-1H: a campaign movie (awaited; nothing when --movies=off or the title is empty)
func play_movie(title: String) -> void:
	if not movies or title.is_empty():
		return
	if movie == null:
		movie = load("res://scripts/movie_player.gd").new()
		movie.name = "CampaignMovie"
		movie.skip_after_ms = game._movie_skip_after # lane CAMP-2 (tests)
		movie.path_overrides = game._movie_files
		add_child(movie)
	await movie.play(world, game._audio, title)
	if not movie.last.is_empty():
		print("CAMPAIGN movie: ", JSON.stringify(movie.last)) # lane CAMP-2


## AptMainMenu::ContinueCampaign (RW 0x91B50D): with "00000000.sav" in the Save folder (TheGameState RW 0x6DDE0F) the menu's update loads it (state 1);
## else nothing happens (false). RotWK itself writes no file of that name (its auto-save is "Auto Save for <map>", RW 0x6DE8F1)
func continue_saved() -> bool:
	var dir: String = game._save_flow.save_dir()
	var path := dir.path_join("00000000.sav")
	if dir.is_empty() or not FileAccess.file_exists(path):
		print("CAMPAIGN continue: no 00000000.sav in the Save folder (RW 0x91B50D does nothing)")
		return false
	print("CAMPAIGN continue: loading ", path)
	game._save_flow.load_save(path)
	return true


## the campaign's state as GameWorld.campaign_progress_step takes it
func progress() -> Dictionary:
	return {"campaign": campaign, "mission": mission, "difficulty": difficulty, "victorious": victorious, "ready": campaign_ready, "advanced": advanced}


## one step of the campaign's state (the RW functions of GameClient/LinearCampaign.h); the state takes the answer's progress
func step(what: String) -> Dictionary:
	var r: Dictionary = world.campaign_progress_step(progress(), what)
	if not r.get("ok", false):
		printerr("CAMPAIGN step %s refused: %s" % [what, str(r.get("error", ""))])
		return r
	var p: Dictionary = r.progress
	campaign = String(p.campaign)
	mission = int(p.mission)
	difficulty = int(p.difficulty)
	victorious = bool(p.victorious)
	campaign_ready = bool(p.ready)
	advanced = bool(p.advanced)
	return r


## a mission started (RW 0x5EAB8F); `intro`: its IntroMovie plays (the start's second argument: a mission begun or the next one, not a replay after a defeat)
func begin_mission(intro := true) -> void:
	var missions := missions_of(campaign)
	if mission >= missions.size():
		game._fail("CAMPAIGN no mission %d in %s" % [mission, campaign])
		return
	var m: Dictionary = missions[mission]
	print("CAMPAIGN mission %d: %s (%s)%s" % [mission, m.name, m.map, "" if intro else " (replayed: no IntroMovie)"])
	if not campaign.is_empty() and not step("started").get("ok", false):
		game._fail("CAMPAIGN the mission start was refused")
		return
	menu_open = false
	score_pending = false
	_end_timer_ran = false
	_end_frame = -1
	active = true
	begun += 1
	if not await show_load_screen(m):
		return
	var rep: Dictionary = world.start_campaign_mission({"campaign": campaign, "mission": mission, "difficulty": difficulty,
		"progress": Callable(game, "_on_load_progress"), "test_hooks": hooks, "record_memory": true}) # lane SAVE-2: recorded so it can be saved
	game._report = rep
	print("CAMPAIGN load ok=%s errors=%s" % [rep.ok, str(rep.get("errors", []).slice(0, 5))])
	if not rep.ok:
		game._fail("start_campaign_mission failed")
		return
	# lane CAMP-2: the mission's IntroMovie plays after its loading screen (LinearCampaignExpansion1.ini: "A special movie which is played when the
	# campaign starts, BEFORE the first map is loaded (all the other movies are played AFTER the loading screen)"; CAMP-1H played it before the load)
	if intro and movies and not String(m.intro_movie).is_empty():
		game._audio.stop_music(false) # INFERENCE: the loading screen's music ends for the movie (its own <file>_Music event plays); at once, not under it (PLAY-3 r2)
	if intro:
		await play_movie(String(m.intro_movie))
	enter_mission(rep)


## the mission's loading screen (lane SAVE-2: also a loaded save's): false when it could not be shown (the run failed)
## lane SAVE-4e: `loading_save` (a saved mission loading): retail's getLoadScreen (RW 0x626A87) shows a film still (StillImage_Film01 .. 21), not the
## mission's picture, and not the mission's music (retail passes the AudioSettings' LoadScreenMusic there, which is not played here: S-5012)
func show_load_screen(m: Dictionary, loading_save := false) -> bool:
	_ended_at = -1
	_win_step = 0
	game._state = game.State.LOADING
	game._shell_leave_for_game() # lane CAMP-2: the shell backdrop and the front-end background go, as for every game (CAMP-2 bug 1)
	if not loading_save and not String(m.load_screen_music).is_empty():
		game._audio.play_music(m.load_screen_music)
	# lane MENU-1: a campaign mission (game mode 0) loads behind the picture screen with its LoadScreenImage (GameLogic::getLoadScreen RW 0x626A87 ->
	# RW 0x757CA0, init RW 0x7583CC), never the players' LoadScreen.apt of a skirmish
	var choice: Dictionary = game._shell.choose_load_screen({"game_mode": 0, "picture": String(m.load_screen_image), "loading_save": loading_save})
	if not String(choice.get("error", "")).is_empty():
		game._fail("CAMPAIGN load screen: " + String(choice.error))
		return false
	var shown: Dictionary = game._shell.show_still_load_screen(String(choice.picture))
	print("CAMPAIGN load screen: %s %s %s" % [choice.kind, choice.picture, "" if shown.ok else str(shown.error)])
	if not shown.ok:
		game._fail("CAMPAIGN load screen: " + str(shown.error))
		return false
	for i in 4:
		await get_tree().process_frame
	game._shell.auto_process = false
	return true


## the loaded mission handed to the player (lane SAVE-2: also a loaded save's mission, after its catch-up)
func enter_mission(rep: Dictionary) -> void:
	active = true
	game._game_kind = "campaign %s %d" % [campaign, mission] # lane CAMP-2
	game._enter_game(rep)
	_cine = load("res://scripts/campaign_cine.gd").new()
	_cine.hud_mode = true
	_cine.dev_overlay = game._dev_overlay # lane CAMP-2: no developer text in the real game
	_cine.control_bar_hook = Callable(game, "_set_control_bar_hidden") # lane CAMP-2: the letterbox hides the Palantir as retail's (RW 0x7BC8B7)
	_cine.input_hook = Callable(game, "_script_input") # lane OWNERFIX-1: no selection or orders during the cutscenes (RW 0x7BC586)
	_cine.hud = game._hud     # lane CAMPUI-1: the script's camera moves drive the HUD's TacticalCamera (retail's single view)
	_cine.shell = game._shell # lane CAMPUI-1: the caption's font (shell_font)
	_cine.objectives_open = func() -> int: return 2 if game._shell.shell_stack().has("Objectives.apt") else 0 # lane CAMPUI-1 (RW 0x69BFAE)
	game.add_child(_cine)
	game._game_nodes.append(_cine)
	# lane CAMPUI-1: rep.start.camera_start is in Godot axes (GodotGameWorldCampaign: (x, z, -y)); the presentation's target is SAGE (x, y, z): the start
	# handed over unconverted aimed the intro 179 units off its InitialCameraPosition (MAP ANG Angmar: at the rocks, not the Witch-king and Morgomir)
	var c: Vector3 = rep.start.camera_start
	_cine.setup(world, game._camera, Vector3(c.x, -c.z, c.y), _cine, game._audio) # its overlay goes with it; the audio: the caption's typing click
	_cine.movie_hook = Callable(self, "_on_movie_request") # lane CAMP-1H: PLAY_MOVIE_IN_GAME


## lane CAMP-1H: a map's PLAY_MOVIE_IN_GAME (its first parameter is the Video's title); the game goes on underneath
func _on_movie_request(title: String) -> void:
	play_movie(title)


## every frame of a campaign game (game.gd's PLAYING state)
func tick() -> void:
	if not active or world == null:
		return
	# lane RENDERWAIT-1: the auto-win harness reads and changes the live logic: only in a render frame that holds it (never a wait for the logic worker);
	# campaign_status answers "not ended" in the others. The teardown after the end is a one-off, not a per-frame read
	if auto_win and world.is_logic_held():
		_auto_win_tick()
	var st: Dictionary = world.campaign_status()
	if not st.get("ended", false):
		return
	if _ended_at < 0:
		_ended_at = Time.get_ticks_msec()
		_end_frame = int(st.frame)
		var missions := missions_of(campaign)
		var line := "%s %d %s %s %d" % [campaign, mission, missions[mission].map, "victory" if st.victory else "defeat", st.frame]
		ended_log.append(line)
		print("CAMPAIGN end: ", line, " (", st.action, ")")
		if st.victory:
			step("won") # RW 0x5EBAEE: + 0xD0
	# lane CAMPSAVE-1: the end-game timer (RW 0x602FFE: 25 logic frames after VICTORY / DEFEAT) leaves the game for the score screen, as in a skirmish
	# (the end sequence's own request, end_timer_ran, or the frame count); lane CAMP-1H: an exit movie (PLAY_MOVIE_IN_GAME) plays to its end first
	if movie != null and not String(movie.playing).is_empty():
		return
	if not _end_timer_ran:
		if not world.is_logic_held():
			return # lane RENDERWAIT-1: the frame count is read only in a render frame that holds the logic
		if world.get_frame() < _end_frame + END_GAME_TIMER_FRAMES:
			return
	leave_to_score_screen()


## the end sequence's timer ran out (game.gd's clear_game_data request in a campaign game)
func end_timer_ran() -> void:
	_end_timer_ran = true


## MSG_CLEAR_GAME_DATA in a campaign game: the campaign is told the game is over (RW 0x5EAB0A, INFERENCE: when the game is cleared), the score screen
## (RW 0x779A3D: shown for a campaign; TimeLine.apt type 1)
func leave_to_score_screen() -> void:
	if not active:
		return
	active = false
	_cine = null
	step("game_over")
	score_pending = true
	print("CAMPAIGN the game is left for the score screen (%s %s; the campaign's mission index is now %d)" % [campaign, "won" if victorious else "lost", mission])
	game._exit_to_score_screen()


## the quit menu's Restart in a campaign mission (RW 0x9220DE: a campaign -> TheLinearCampaignManager RW 0x5EC62F -> restartCurrentCampaignMission RW
## 0x5EC5D1): the game is cleared without a score screen (the mission start's RW 0x376E92(0, 0)) and the same mission starts again without its
## IntroMovie (a won one, inside the end-game timer: the next mission, with it)
func restart_mission() -> void:
	game._close_quit_menu()
	active = false
	_cine = null
	game._tear_down_game("restart")
	var r := step("last_mission")
	print("CAMPAIGN restart from the quit menu (RW 0x9220DE -> RW 0x5EC5D1): mission %d" % mission)
	await begin_mission(bool(r.get("intro_movie", false)))


## the score screen's Continue in a campaign game: RW 0x926E0A (TimeLine's destructor, type 1) -> RW 0x927EC6
func after_score() -> void:
	score_pending = false
	var r := step("after_score")
	var after := String(r.get("after", ""))
	print("CAMPAIGN after the score screen (RW 0x927EC6): ", after)
	if after == "bonus_unlock":
		# RW 0x927F00 .. 0x927F5B: OptionPreferences BCU = "true" written, TheGlobalData + 0xAF5 = 1, the shell shown (RW 0x75E43C(1)): the main menu
		var u: Dictionary = game._shell.unlock_bonus_campaign()
		last_unlock = u
		print("CAMPAIGN %s complete: the bonus campaign is unlocked (BCU written: %s %s)" % [campaign, str(u.get("ok", false)), str(u.get("error", u.get("file", "")))])
		menu_log.append("bonus_unlock %s %s" % [campaign, str(u.get("ok", false))])
		game._campaign_complete = true
		_to_main_menu()
		return
	if after == "auto_save_menu":
		write_auto_save()
	await open_menu()


## RW 0x6DE8F1: the auto-save: GUI:AutoSaveName ("Auto Save for %ls") with the map's display name (the mission the menu names, RW 0x5EAB0A), the
## description "__AUTO#SAVE__", IsAutoSaveOrNot = 1; OpenBFME: a campaign menu save (.obfmesave) of the campaign's state
func write_auto_save() -> Dictionary:
	var dir: String = game._save_flow.save_dir()
	if dir.is_empty():
		printerr("CAMPAIGN auto-save: there is no Save folder")
		return {"ok": false, "error": "no Save folder"}
	var menu: Dictionary = step("menu").get("menu", {})
	var format := String(game._shell.fetch_text("GUI:AutoSaveName").get("text", "Auto Save for %ls"))
	# the decomp's getMapDisplayName (GameStateAutoSave.cpp): the map cache's display name, a label resolved through the game text, else the label itself
	var map_name := String(menu.get("map_display", ""))
	if map_name.begins_with("$"):
		var t: Dictionary = game._shell.fetch_text(map_name.substr(1))
		map_name = String(t.text) if t.get("found", false) else map_name.substr(1)
	var name := format.replace("%ls", map_name).replace("%s", map_name)
	var path := dir.path_join(name.validate_filename() + ".obfmesave")
	var opts := progress()
	opts["description"] = "__AUTO#SAVE__"
	opts["auto_save"] = true
	var r: Dictionary = world.save_campaign_menu(path, opts)
	print("CAMPAIGN auto-save (RW 0x6DE8F1) '%s': %s" % [path.get_file(), JSON.stringify(r)])
	if r.get("ok", false):
		last_auto_save = path
		menu_log.append("auto_save %s" % path.get_file())
	return r


## CampaignMenu.apt with the campaign's state (RW 0x927EC6 -> RW 0x75E3B7)
func open_menu() -> void:
	var r := step("menu")
	var menu: Dictionary = r.get("menu", {})
	game._shell.set_campaign_menu(menu)
	game._shell.set_save_folder(game._save_flow.save_dir())
	while game._shell.shell_stack().size() > 0:
		game._shell.shell_pop()
		game._shell.tick(0.033)
	game._shell_back_from_game()
	game._menu_revealed = false
	game._frames = 0
	game._state = game.State.MENU
	menu_open = true
	_menu_sub = ""
	menu_log.append("menu %s %d %s" % [campaign, int(menu.get("mission_index", -1)), "won" if victorious else "lost"])
	print("CAMPAIGN menu: %s mission %d '%s' (%s), image %s" % [campaign, int(menu.get("mission_index", -1)), String(menu.get("mission_name", "")),
		"Next Mission" if victorious else "Failed Mission", String(menu.get("load_screen_image", ""))])
	if not game._shell.shell_push("CampaignMenu.apt"):
		game._fail("the shell refused CampaignMenu.apt: " + str(game._shell.get_shell_report().errors))


## a campaign menu save loaded (scripts/save_flow.gd: check_save's save_point 1): the menu with its state
func open_from_save(check: Dictionary) -> void:
	campaign = String(check.campaign)
	mission = int(check.mission)
	difficulty = int(check.difficulty)
	victorious = bool(check.victorious)
	campaign_ready = bool(check.ready)
	advanced = bool(check.advanced)
	active = false
	score_pending = false
	print("CAMPAIGN loaded the campaign menu: %s mission %d %s" % [campaign, mission, JSON.stringify(progress())])
	await open_menu()


## CampaignMenu.apt's requests (AptCampaignMenu; run deferred, outside the movie's command): Main Menu / Last / Next Mission leave the menu (RW 0x927A07:
## the engine pops it)
func menu_request(action: String) -> void:
	if not menu_open:
		print("CAMPAIGN menu request %s without the menu: ignored" % action)
		return
	if action == "CampaignMenuMainMenu":
		menu_open = false
		_to_main_menu()
	elif action == "CampaignMenuNextMission":
		var r := step("next_mission") # message 0x21 -> RW 0x5EAE37
		if not r.get("start", false):
			print("CAMPAIGN Next Mission: the campaign is not ready (RW 0x5EAE37 starts nothing)")
			return
		await _leave_menu_for_mission(bool(r.get("intro_movie", true)))
	elif action == "CampaignMenuLastMission":
		var r := step("last_mission") # RW 0x5EC5D1
		await _leave_menu_for_mission(bool(r.get("intro_movie", false)))
	elif action == "CampaignMenuSave":
		_menu_sub = "save"
		game._save_flow.open_menu(3, 1, false) # RW 0x927A93: AptSaveLoad::OpenScreen(3, 1, 0)
	elif action == "CampaignMenuLoad":
		_menu_sub = "load"
		game._save_flow.open_menu(2, 1, false) # RW 0x927AA4: (2, 1, 0)


func _leave_menu_for_mission(intro: bool) -> void:
	menu_open = false
	_menu_sub = ""
	if mission >= missions_of(campaign).size():
		game._fail("CAMPAIGN the menu's mission %d is past %s's list" % [mission, campaign])
		return
	await begin_mission(intro)


## every frame while the menu is up (game.gd's MENU state): SaveLoad.apt opened over the menu has closed -> the menu again (INFERENCE: the movie played
## its _close for Save / Load as for every button, RW 0x927A93 / 0x927AA4 leave the menu on the stack; retail's refocus of it was not read)
func menu_tick() -> void:
	if not menu_open or _menu_sub.is_empty():
		return
	var stack: PackedStringArray = game._shell.shell_stack()
	if stack.size() > 0 and stack[-1] == "SaveLoad.apt":
		return
	print("CAMPAIGN SaveLoad.apt closed over the menu (%s): the menu again" % _menu_sub)
	_menu_sub = ""
	open_menu()


## lanes CAMP-1 .. SAVE-4 kept the next mission in user://Campaign/progress.txt; it becomes the auto-save of that point once (the file is renamed
## progress.txt.migrated, never deleted)
func migrate_sidecar() -> void:
	if not FileAccess.file_exists(PROGRESS_PATH):
		return
	var p: Dictionary = world.campaign_progress_load(PROGRESS_PATH)
	if not p.get("ok", false):
		printerr("CAMPAIGN the old progress file is not migrated: ", p.get("error", ""))
		return
	campaign = String(p.campaign)
	difficulty = int(p.difficulty)
	# the sidecar held the mission to play next after a won one: RW 0x5EAB0A's state (won, ready, the index moved on)
	mission = int(p.mission)
	victorious = mission > 0
	campaign_ready = true
	advanced = mission > 0
	var r := write_auto_save()
	if r.get("ok", false):
		var err := DirAccess.rename_absolute(ProjectSettings.globalize_path(PROGRESS_PATH), ProjectSettings.globalize_path(PROGRESS_PATH + ".migrated"))
		print("CAMPAIGN the old progress file became the auto-save %s (renamed: %s)" % [last_auto_save.get_file(), error_string(err)])


func _to_main_menu() -> void:
	while game._shell.shell_stack().size() > 0: # lane CAMPSAVE-1: the campaign menu goes (RW 0x927A07's pop)
		game._shell.shell_pop()
		game._shell.tick(0.033)
	game._shell_back_from_game() # lane CAMP-2
	game._shell.shell_push("MainMenu.apt")
	game._audio.play_shell_music(false)
	game._menu_revealed = false
	game._frames = 0
	game._state = game.State.MENU
	print("CAMPAIGN back to the main menu")


## lane CAMPSAVE-1: --campaign-win also goes through the screens between missions as a player does, with real clicks: the score screen's Continue,
## then CampaignMenu.apt's Next Mission (Replay Mission after a defeat); with --screens each screen is saved first (campsave1-<mission>-score / -menu)
var _auto_busy := false
var auto_menu_choice := ""   # a test's own choice of menu button ("" Next / Replay Mission by the outcome)
var auto_win_all := false    # the cheat hook ends the missions that have no win move of their own (scripts/campaign_save_test.gd "eight")
var auto_screens := true     # false: a test (scripts/campaign_save_test.gd) clicks the screens itself


func auto_score_tick() -> void:
	if _auto_busy or not auto_screens:
		return
	_auto_busy = true
	await game._wait_seconds(3.0)
	game._save_named("campsave1-%d-score" % mission)
	var level: int = game._shell.shell_top_level()
	for attempt in 6:
		if not score_pending:
			break
		await game._click_button(level, "Main.MainButtons.Continue")
		await game._wait_seconds(3.0) # the button's release animation runs before its command
	_auto_busy = false


func auto_menu_tick() -> void:
	if _auto_busy or not auto_screens or not _menu_sub.is_empty():
		return
	var stack: PackedStringArray = game._shell.shell_stack()
	if stack.size() == 0 or stack[-1] != "CampaignMenu.apt":
		return
	_auto_busy = true
	await game._wait_seconds(3.0)
	game._save_named("campsave1-%d-menu" % mission)
	var button := auto_menu_choice if not auto_menu_choice.is_empty() else ("NextMission" if victorious else "LastMission")
	var level: int = game._shell.shell_top_level()
	for attempt in 6:
		if not menu_open:
			break
		var b: Dictionary = game._shell.find_button(level, "Buttons." + button)
		if not b.get("found", false):
			b = game._shell.find_button(level, button)
		if b.get("found", false):
			await game._click_button(level, String(b.path))
		else:
			print("CAMPAIGN auto: no %s button on the menu: %s" % [button, JSON.stringify(game._shell.list_buttons(level))])
		await game._wait_seconds(3.0)
	_auto_busy = false


## --campaign-win (with the start option test_hooks): the player's decisive moment of each mission by the GameWorld test hooks, so a scripted run goes from
## mission to mission (the full playthroughs are the C++ tests, test_camp1_missions.cpp)
func _auto_win_tick() -> void:
	var f: int = world.get_frame()
	var map: String = String(missions_of(campaign)[mission].map).to_lower()
	if map.ends_with("bonus"):
		# the Witch King is found first (NAMED_DISCOVERED: his dialogue and objective 6), then defeated
		if _win_step == 0 and f >= 120:
			var wk: Dictionary = world.debug_script_unit("Witch King")
			if wk.get("ok", false):
				print("CAMPAIGN auto-win: Earnur to the Witch King %s" % world.debug_script_place_at("Earnur", float(wk.x) + 60.0, float(wk.y)))
			_win_step = 1
			_win_frame = f
		elif _win_step == 1 and f >= _win_frame + 100:
			print("CAMPAIGN auto-win: Witch King %s" % world.debug_script_kill("Witch King"))
			_win_step = 2
		return
	if _win_step != 0 or f < 120:
		return
	var target := ""
	if map.ends_with("amon sul"):
		target = "Tower of Amon Sul"
	elif map.ends_with("fornost"):
		target = "THE BUILDING"
	if target.is_empty():
		if not auto_win_all:
			return
		# lane CAMPSAVE-1: the other missions' decisive moment is the VICTORY action itself (the cheat hook; their own scripts win them in the C++ mission tests)
		_win_step = 1
		print("CAMPAIGN auto-win: victory by the cheat hook %s" % world.debug_script_victory())
		return
	_win_step = 1
	print("CAMPAIGN auto-win: %s %s" % [target, world.debug_script_kill(target)])
