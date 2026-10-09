## Lane CAMP-1: the linear campaign flow of the game (scripts/game.gd owns this node). TheLinearCampaignManager's campaigns come from the retail INI
## (GameWorld.get_campaigns: RotWK's legend loads Data\INI\LinearCampaignExpansion1.ini: ANGMAR_CAMPAIGN, ANGMAR_CAMPAIGN_DEMO, ANGMAR_BONUS_CAMPAIGN).
##
##   the main menu's AptMainMenu::Expansion1Campaign (RW 0x91AF8C) starts ANGMAR_CAMPAIGN, AptMainMenu::BonusCampaign (RW 0x91AFAF) ANGMAR_BONUS_CAMPAIGN;
##   the command's first character is the difficulty (RW 0x91C108: 'E' easy, 'H' hard, else normal); AptMainMenu::ContinueCampaign continues the saved
##   progress. A mission: the load screen with the mission's LoadScreenMusicTrack, GameWorld.start_campaign_mission (the map with the campaign rules), the
##   HUD, the script presentation (scripts/campaign_cine.gd in HUD mode: letterbox, captions, fades, notifications, objectives; the script camera only while
##   a cinematic runs). The map's scripts end it (VICTORY_SCREEN / QUICKVICTORY / DEFEAT, GameWorld.campaign_status): a won mission saves the progress and
##   the next mission loads (RW 0x927EC6: retail auto-saves "00000000.sav" and shows CampaignMenu.apt first); after the last one the bonus campaign is
##   unlocked (the preference "BCU", RW 0x927F08) and the main menu returns. A lost mission returns to the main menu.
##
## lane CAMP-1H: the campaign movies (scripts/movie_player.gd, GameWorld.get_movie): the campaign's OverallCampaignIntroMovie when it starts from the menu
## (INFERENCE: RotWK's caller of it was not located), a mission's IntroMovie before its load screen (INFERENCE: ZH SinglePlayerLoadScreen plays the mission's
## movie first) and the maps' PLAY_MOVIE_IN_GAME (InGameUI RW 0x69B788 -> RW 0x6D5397) play their narration over a black screen (the VP6 picture: S-1710);
## --movies=off (game.gd) leaves them out of scripted runs.
## NOT PORTED (stops): the VP6 picture of the campaign movies (S-1710; formerly S-1364), CampaignMenu.apt / the
## auto-save (S-1360: the progress is this engine's sidecar text, CampaignProgress), the mission's LoadScreenImage on LoadScreen.apt (S-1364), the carry-over
## heroes between missions (S-1180).
extends Node

const PROGRESS_PATH := "user://Campaign/progress.txt"

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
	print("CAMPAIGN start %s (difficulty %d): %d missions" % [name, diff, missions_of(name).size()])
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
		add_child(movie)
	await movie.play(world, game._audio, title)


## AptMainMenu::ContinueCampaign: the saved progress (the next mission after a won one)
func continue_saved() -> bool:
	var p: Dictionary = world.campaign_progress_load(PROGRESS_PATH)
	if not p.get("ok", false):
		print("CAMPAIGN continue: ", p.get("error", "no progress"))
		return false
	campaign = p.campaign
	mission = int(p.mission)
	difficulty = int(p.difficulty)
	print("CAMPAIGN continue %s mission %d" % [campaign, mission])
	await begin_mission()
	return true


func begin_mission() -> void:
	var missions := missions_of(campaign)
	if mission >= missions.size():
		game._fail("CAMPAIGN no mission %d in %s" % [mission, campaign])
		return
	var m: Dictionary = missions[mission]
	print("CAMPAIGN mission %d: %s (%s)" % [mission, m.name, m.map])
	active = true
	begun += 1
	_ended_at = -1
	_win_step = 0
	game._state = game.State.LOADING
	await play_movie(String(m.intro_movie)) # lane CAMP-1H: the mission's narrated intro, before its load screen
	if not String(m.load_screen_music).is_empty():
		game._audio.play_music(m.load_screen_music)
	game._shell_load_screen_push()
	for i in 4:
		await get_tree().process_frame
	game._shell.auto_process = false
	var rep: Dictionary = world.start_campaign_mission({"campaign": campaign, "mission": mission, "difficulty": difficulty,
		"progress": Callable(game, "_on_load_progress"), "test_hooks": hooks})
	game._report = rep
	print("CAMPAIGN load ok=%s errors=%s" % [rep.ok, str(rep.get("errors", []).slice(0, 5))])
	if not rep.ok:
		game._fail("start_campaign_mission failed")
		return
	game._enter_game(rep)
	_cine = load("res://scripts/campaign_cine.gd").new()
	_cine.hud_mode = true
	game.add_child(_cine)
	game._game_nodes.append(_cine)
	_cine.setup(world, game._camera, rep.start.camera_start, _cine, null) # its overlay goes with it
	_cine.movie_hook = Callable(self, "_on_movie_request") # lane CAMP-1H: PLAY_MOVIE_IN_GAME


## lane CAMP-1H: a map's PLAY_MOVIE_IN_GAME (its first parameter is the Video's title); the game goes on underneath
func _on_movie_request(title: String) -> void:
	play_movie(title)


## every frame of a campaign game (game.gd's PLAYING state)
func tick() -> void:
	if not active or world == null:
		return
	if auto_win:
		_auto_win_tick()
	var st: Dictionary = world.campaign_status()
	if not st.get("ended", false):
		return
	if _ended_at < 0:
		_ended_at = Time.get_ticks_msec()
		var missions := missions_of(campaign)
		var line := "%s %d %s %s %d" % [campaign, mission, missions[mission].map, "victory" if st.victory else "defeat", st.frame]
		ended_log.append(line)
		print("CAMPAIGN end: ", line, " (", st.action, ")")
	# the victory / defeat screen shows for a while (the map scripts' own fade and VICTORY_SCREEN), then the campaign goes on
	if movie != null and not String(movie.playing).is_empty():
		_ended_at = Time.get_ticks_msec() - 4000 # lane CAMP-1H: an exit movie (PLAY_MOVIE_IN_GAME) plays to its end, then the mission is left
		return
	if Time.get_ticks_msec() - _ended_at < 4000:
		return
	active = false
	_teardown()
	if st.victory:
		await _next()
	else:
		_to_main_menu()


func _next() -> void:
	var missions := missions_of(campaign)
	if mission + 1 < missions.size():
		mission += 1
		DirAccess.make_dir_recursive_absolute("user://Campaign")
		var saved: Dictionary = world.campaign_progress_save(PROGRESS_PATH, {"campaign": campaign, "mission": mission, "difficulty": difficulty, "victorious": false})
		print("CAMPAIGN progress saved: ", saved)
		await begin_mission()
		return
	# RW 0x927EC6: the last mission won: the preference "BCU" = true (the bonus campaign) and the main menu
	print("CAMPAIGN %s complete: the bonus campaign is unlocked (BCU)" % campaign)
	game._campaign_complete = true
	_to_main_menu()


func _teardown() -> void:
	world.set_auto_advance(false)
	for n in game._game_nodes:
		if is_instance_valid(n):
			game.remove_child(n)
			n.free()
	game._game_nodes.clear()
	_cine = null
	if game._hud != null:
		game.remove_child(game._hud)
		game._hud.free()
		game._hud = null
	game._hud_installed = false
	while game._shell.shell_stack().size() > 0:
		game._shell.shell_pop()
		game._shell.tick(0.033)
	print("CAMPAIGN clear_game_data: ", JSON.stringify(world.clear_game_data(false)))
	game._camera.current = false


func _to_main_menu() -> void:
	game._shell.shell_push("MainMenu.apt")
	game._audio.play_shell_music(false)
	game._menu_revealed = false
	game._frames = 0
	game._state = game.State.MENU
	print("CAMPAIGN back to the main menu")


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
		return
	_win_step = 1
	print("CAMPAIGN auto-win: %s %s" % [target, world.debug_script_kill(target)])
