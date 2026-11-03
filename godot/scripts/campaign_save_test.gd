## OpenBFME. GPL-3.0.
## Lane CAMPSAVE-1: --campsave-test=<scenario> (scripts/game.gd), the campaign between missions played through its screens with real clicks
## (scripts/campaign_flow.gd; the C++ side is GameClient/LinearCampaign.h and GUI/AptScreens/AptCampaignMenu.h). With --screens=DIR every screen is saved as
## campsave1-<scenario>-<step>.png. Prints "CAMPSAVE1 RESULT: ok" and exits 0, or "CAMPSAVE1 RESULT: FAILED <why>" and exits 1.
##
##   menu      (--campaign=ANGMAR_CAMPAIGN --mission=N --campaign-win): mission N is won by the test hook; the score screen's Continue; the auto-save is
##             written (RW 0x6DE8F1, its header an auto-save of the campaign menu save point) and CampaignMenu.apt names the next mission (Next Mission);
##             the menu's Save writes a campaign menu save on SaveLoad.apt's page and the menu comes back; its Load loads that save and the menu comes
##             back with the same state; Next Mission starts mission N + 1; that mission is lost (the local player's objects killed); the score screen; the
##             menu after a defeat (Failed Mission, no new auto-save); Replay Mission starts mission N + 1 again
##   bonus     (--campaign=ANGMAR_CAMPAIGN --mission=7 --campaign-win): the last mission won, the score screen's Continue: no auto-save, no menu, Options.ini's
##             BCU = "true" (RW 0x927EC6), the main menu with MainMenuUnlockBonusCampaign set
##   continue  (no --campaign, a Save folder holding an auto-save, e.g. the one "menu" wrote): a restarted game's main menu, Load Game, the auto-save (the
##             only campaign save: selected), Load, CampaignMenu.apt with the saved mission, Next Mission starts it
##   eight     (--campaign=ANGMAR_CAMPAIGN --mission=0 --campaign-win): the whole campaign through its menus: every mission ended by the cheat hook (Amon Sul
##             and Fornost by their own win moves), the score screen's Continue, the auto-save (its header names the next mission, its listing is an
##             auto-save of the Campaign page), CampaignMenu.apt naming the next mission of the campaign's INI (name, picture), Next Mission; the
##             eighth mission's win unlocks the bonus campaign (no auto-save, no menu)
##   carry     (lane BFME2CAMP1; --campaign=GOOD_CAMPAIGN --mission=0 --campaign-win): MAP GOOD Rivendell won by the cheat hook (the QUICKVICTORY action:
##             RW 0x5EBAEE records the campaign's carry-over), the score screen's Continue, the auto-save holding the carry-over (Glorfindel, Gloin and the
##             heroes Rivendell held back), Next Mission: MAP GOOD High Pass, whose intro spawns Glorfindel and Gloin from it
##   quit      (--campaign=ANGMAR_CAMPAIGN --mission=N): the in-game menu during the mission: Restart (RW 0x9220DE -> RW 0x5EC5D1) starts the same mission
##             again; Exit Mission leaves to the score screen, whose Continue shows CampaignMenu.apt after a defeat (Replay Mission)
##   single_player (no --campaign): the main menu's single player page (SoloPlayNav), saved as a picture: EPILOGUE (BonusCampaign) is enabled only with
##             Options.ini's BCU = true (MainMenuUnlockBonusCampaign, RW 0x91BBAD)
##   continue_command (no --campaign, a Save folder holding an auto-save): AptMainMenu::ContinueCampaign (RW 0x91B50D) without 00000000.sav starts
##             nothing; with the auto-save copied to 00000000.sav it loads it: CampaignMenu.apt
extends RefCounted

var game: Node
var flow: Node
var scenario := ""
var _failed := ""


func _init(g: Node, s: String) -> void:
	game = g
	flow = g._campaign_flow
	scenario = s


func _check(ok: bool, what: String) -> bool:
	print("CAMPSAVE1 CHECK %s: %s" % ["ok" if ok else "FAILED", what])
	if not ok and _failed.is_empty():
		_failed = what
	return ok


func _shot(step: String) -> void:
	game._save_named("campsave1-%s-%s" % [scenario, step])


func _wait_until(cond: Callable, seconds: float) -> bool:
	var waited := 0.0
	while waited < seconds:
		if cond.call():
			return true
		await game._wait_seconds(0.25)
		waited += 0.25
	return cond.call()


func _top() -> String:
	var stack: PackedStringArray = game._shell.shell_stack()
	return stack[-1] if stack.size() > 0 else ""


## a real click on a button of the top screen by its path below the level ("Buttons.NextMission", "Main.MainButtons.Continue")
func _click(path: String) -> bool:
	var level: int = game._shell.shell_top_level()
	var b: Dictionary = game._shell.find_button(level, path)
	if not b.get("found", false):
		print("CAMPSAVE1 no button %s on %s: %s" % [path, _top(), JSON.stringify(game._shell.list_buttons(level))])
		return false
	return await game._click_button(level, String(b.path))


## the score screen's Continue until the campaign took it (the button's release animation runs before its command)
func _continue_score() -> bool:
	if not await _wait_until(func(): return game._state == game.State.SCORE, 120.0):
		return false
	await game._wait_seconds(3.0)
	_shot("score")
	for attempt in 6:
		if not flow.score_pending:
			return true
		await _click("Main.MainButtons.Continue")
		await game._wait_seconds(3.0)
	return not flow.score_pending


func _menu_shown() -> bool:
	return flow.menu_open and _top() == "CampaignMenu.apt" and game._shell.has_level(game._shell.shell_top_level())


func run() -> bool:
	flow.auto_screens = false # the test clicks the screens itself
	var ok := false
	if scenario == "menu":
		ok = await _run_menu()
	elif scenario == "bonus":
		ok = await _run_bonus()
	elif scenario == "continue":
		ok = await _run_continue()
	elif scenario == "single_player":
		ok = await _run_single_player()
	elif scenario == "eight":
		ok = await _run_eight()
	elif scenario == "carry":
		ok = await _run_carry()
	elif scenario == "quit":
		ok = await _run_quit()
	elif scenario == "continue_command":
		ok = await _run_continue_command()
	else:
		_check(false, "unknown scenario '%s'" % scenario)
	ok = ok and _failed.is_empty()
	print("CAMPSAVE1 RESULT: ", "ok" if ok else "FAILED " + _failed)
	return ok


func _run_menu() -> bool:
	var first: int = flow.mission
	if not _check(await _wait_until(func(): return flow.ended_log.size() >= 1, 300.0), "mission %d ended" % first):
		return false
	_check(String(flow.ended_log[0]).contains(" victory "), "mission %d won: %s" % [first, flow.ended_log[0]])
	if not _check(await _continue_score(), "the score screen's Continue ran RW 0x927EC6"):
		return false
	if not _check(await _wait_until(_menu_shown, 30.0), "CampaignMenu.apt is shown after the won mission"):
		return false
	await game._wait_seconds(3.0)
	_shot("menu-won")
	var auto_path: String = flow.last_auto_save
	_check(not auto_path.is_empty() and FileAccess.file_exists(auto_path), "the auto-save was written: %s" % auto_path)
	var check: Dictionary = game._world.check_save(auto_path)
	_check(check.get("ok", false) and int(check.get("save_point", 0)) == 1 and check.get("auto_save", false), "the auto-save is a campaign menu auto-save: %s" % JSON.stringify(check))
	_check(int(check.get("mission", -1)) == first + 1 and check.get("victorious", false), "the auto-save names mission %d after a win" % (first + 1))
	_check(flow.mission == first + 1 and flow.victorious, "the menu's campaign state: mission %d won" % flow.mission)
	var listed: Array = game._world.list_saves(game._save_flow.save_dir())
	var auto_listed := false
	for s in listed:
		auto_listed = auto_listed or (String(s.path) == auto_path and s.get("auto_save", false) and int(s.kind) == 1)
	_check(auto_listed, "the Save folder lists the auto-save as an auto-save of the Campaign page")
	# the menu's Save: SaveLoad.apt's save page (RW 0x927A93), the default name, Save, the result box's Ok, the menu again
	if not _check(await _click("Buttons.SaveGame"), "the menu's Save button"):
		return false
	if not _check(await _wait_until(func(): return _top() == "SaveLoad.apt", 10.0), "SaveLoad.apt's save page over the menu"):
		return false
	await game._wait_seconds(2.5)
	_shot("save-page")
	await game._save_flow._click_top("Save")
	await game._wait_seconds(2.0)
	_shot("saved")
	var saved: Dictionary = game._save_flow.last_save
	_check(saved.get("ok", false), "the menu's save was written: %s" % JSON.stringify(saved))
	await game._save_flow._click_any_level("MessageBox", "Ok")
	if not _check(await _wait_until(_menu_shown, 20.0), "the menu is shown again after the save page"):
		return false
	var saved_check: Dictionary = game._world.check_save(String(saved.get("path", "")))
	_check(int(saved_check.get("save_point", 0)) == 1 and not saved_check.get("auto_save", true) and int(saved_check.get("mission", -1)) == first + 1,
		"the menu's save is a campaign menu save of mission %d (not an auto-save): %s" % [first + 1, JSON.stringify(saved_check)])
	# the menu's Load: the load page (RW 0x927AA4), the newest save selected (the menu's save), Load: the menu with that state
	await game._wait_seconds(2.0)
	if not _check(await _click("Buttons.LoadGame"), "the menu's Load button"):
		return false
	if not _check(await _wait_until(func(): return _top() == "SaveLoad.apt", 10.0), "SaveLoad.apt's load page over the menu"):
		return false
	await game._wait_seconds(2.5)
	_shot("load-page")
	game._save_flow.last_load = {}
	await game._save_flow._click_top("Load")
	if not _check(await _wait_until(func(): return String(game._save_flow.last_load.get("state", "")) == "campaign_menu" and _menu_shown(), 30.0),
			"the loaded campaign menu save shows the menu again"):
		return false
	_check(flow.mission == first + 1 and flow.victorious and flow.campaign_ready, "the loaded state: mission %d, won, ready" % flow.mission)
	await game._wait_seconds(3.0)
	_shot("menu-loaded")
	# Next Mission (message 0x21 -> RW 0x5EAE37): mission N + 1 with its intro
	var begun: int = flow.begun
	if not _check(await _click("Buttons.NextMission"), "the menu's Next Mission button"):
		return false
	if not _check(await _wait_until(func(): return flow.begun == begun + 1 and game._state == game.State.PLAYING, 240.0), "Next Mission started mission %d" % (first + 1)):
		return false
	_check(flow.mission == first + 1 and not flow.victorious and not flow.campaign_ready, "the started mission's flags were cleared (RW 0x5EAB8F)")
	# the mission is lost: the local player's objects are killed; its scripts declare the defeat
	await game._wait_seconds(4.0)
	var player := String(game._report.get("start", {}).get("local_player", ""))
	var killed: Dictionary = {}
	for attempt in 40:
		if flow.ended_log.size() >= 2:
			break
		killed = game._world.debug_kill_player_objects(player, "")
		await game._wait_seconds(1.5)
	if not _check(flow.ended_log.size() >= 2, "mission %d ended after the player's objects died (%s, %s)" % [first + 1, player, JSON.stringify(killed)]):
		return false
	_check(String(flow.ended_log[1]).contains(" defeat "), "mission %d lost: %s" % [first + 1, flow.ended_log[1]])
	var autos_before: Array = flow.menu_log.filter(func(l): return String(l).begins_with("auto_save"))
	if not _check(await _continue_score(), "the score screen's Continue after the defeat"):
		return false
	if not _check(await _wait_until(_menu_shown, 30.0), "CampaignMenu.apt is shown after the lost mission"):
		return false
	await game._wait_seconds(3.0)
	_shot("menu-lost")
	var autos_after: Array = flow.menu_log.filter(func(l): return String(l).begins_with("auto_save"))
	_check(autos_after.size() == autos_before.size(), "a lost mission writes no auto-save (RW 0x927EC6)")
	_check(flow.mission == first + 1 and not flow.victorious, "the menu names the failed mission %d" % flow.mission)
	# Replay Mission (RW 0x5EC5D1): the same mission again
	begun = flow.begun
	if not _check(await _click("Buttons.LastMission"), "the menu's Replay Mission button"):
		return false
	_check(await _wait_until(func(): return flow.begun == begun + 1 and game._state == game.State.PLAYING, 240.0), "Replay Mission started mission %d again" % (first + 1))
	_check(flow.mission == first + 1, "the replayed mission is %d" % flow.mission)
	return true


func _run_bonus() -> bool:
	if not _check(await _wait_until(func(): return flow.ended_log.size() >= 1, 300.0), "the last mission ended"):
		return false
	_check(String(flow.ended_log[0]).contains(" victory "), "the last mission won: %s" % flow.ended_log[0])
	var files_before := DirAccess.get_files_at(game._save_flow.save_dir()).size()
	if not _check(await _continue_score(), "the score screen's Continue ran RW 0x927EC6"):
		return false
	if not _check(await _wait_until(func(): return game._state == game.State.MENU and _top() == "MainMenu.apt", 30.0), "the main menu after the campaign"):
		return false
	_check(not flow.menu_open, "no campaign menu after the last mission")
	_check(DirAccess.get_files_at(game._save_flow.save_dir()).size() == files_before, "no auto-save after the last mission")
	_check(game._shell.is_bonus_campaign_unlocked(), "TheGlobalData + 0xAF5 (MainMenuUnlockBonusCampaign) is set")
	_check(flow.last_unlock.get("ok", false), "the unlock wrote Options.ini: %s" % JSON.stringify(flow.last_unlock))
	var opts := FileAccess.get_file_as_string(String(flow.last_unlock.get("file", "")))
	_check(opts.contains("BCU = true"), "Options.ini holds BCU = true")
	await game._wait_seconds(5.0)
	_shot("main-menu")
	return true


func _run_continue() -> bool:
	if not _check(await _wait_until(func(): return game._state == game.State.MENU and game._menu_revealed, 60.0), "the main menu"):
		return false
	await game._wait_seconds(2.0)
	game._on_shell_request("LoadGame", "")
	if not _check(await _wait_until(func(): return _top() == "SaveLoad.apt", 10.0), "the main menu's Load Game page"):
		return false
	await game._wait_seconds(2.5)
	_shot("load-page")
	game._save_flow.last_load = {}
	await game._save_flow._click_top("Load") # the Campaign page's only save is the auto-save: the AutoSaveList's first row is selected (RW 0x6E04AD)
	if not _check(await _wait_until(func(): return String(game._save_flow.last_load.get("state", "")) == "campaign_menu" and _menu_shown(), 30.0),
			"the auto-save loaded: CampaignMenu.apt"):
		return false
	await game._wait_seconds(3.0)
	_shot("menu")
	var m: int = flow.mission
	_check(flow.victorious and flow.campaign_ready, "the auto-save's state: won, ready (Next Mission)")
	var begun: int = flow.begun
	if not _check(await _click("Buttons.NextMission"), "Next Mission"):
		return false
	_check(await _wait_until(func(): return flow.begun == begun + 1 and game._state == game.State.PLAYING, 240.0), "Next Mission started mission %d" % m)
	_check(flow.mission == m, "the continued campaign plays mission %d" % m)
	return true


func _run_continue_command() -> bool:
	if not _check(await _wait_until(func(): return game._state == game.State.MENU and game._menu_revealed, 60.0), "the main menu"):
		return false
	await game._wait_seconds(2.0)
	var dir: String = game._save_flow.save_dir()
	var fixed := dir.path_join("00000000.sav")
	if FileAccess.file_exists(fixed):
		DirAccess.remove_absolute(fixed)
	var begun: int = flow.begun
	_check(game._shell.shell_fscommand("AptMainMenu::ContinueCampaign", ""), "the main menu took AptMainMenu::ContinueCampaign")
	await game._wait_seconds(3.0)
	_check(flow.begun == begun and not flow.menu_open and game._state == game.State.MENU, "without 00000000.sav Continue started nothing")
	var auto := ""
	for s in game._world.list_saves(dir):
		if s.get("auto_save", false):
			auto = String(s.path)
	if not _check(not auto.is_empty(), "the Save folder holds an auto-save to copy"):
		return false
	_check(DirAccess.copy_absolute(auto, fixed) == OK, "the auto-save copied to 00000000.sav")
	game._save_flow.last_load = {}
	_check(game._shell.shell_fscommand("AptMainMenu::ContinueCampaign", ""), "the main menu took AptMainMenu::ContinueCampaign again")
	var shown := await _wait_until(func(): return String(game._save_flow.last_load.get("state", "")) == "campaign_menu" and _menu_shown(), 30.0)
	_check(shown, "with 00000000.sav Continue loaded it: CampaignMenu.apt")
	await game._wait_seconds(2.0)
	_shot("menu")
	DirAccess.remove_absolute(fixed)
	return shown


func _quit_menu(button: String) -> bool:
	await game._press_escape()
	await game._wait_seconds(2.0)
	var ok: bool = await game._click_quit(button)
	await game._wait_seconds(2.0)
	_shot("quit-" + button.to_lower())
	ok = ok and await game._click_quit("Yes")
	return ok


func _run_quit() -> bool:
	var first: int = flow.mission
	if not _check(await _wait_until(func(): return game._state == game.State.PLAYING and game._world.get_frame() > 60, 240.0), "mission %d plays" % first):
		return false
	var begun: int = flow.begun
	if not _check(await _quit_menu("Restart"), "the in-game menu's Restart and its Yes"):
		return false
	if not _check(await _wait_until(func(): return flow.begun == begun + 1 and game._state == game.State.PLAYING and game._world.get_frame() > 30, 240.0),
			"Restart started mission %d again (RW 0x9220DE -> RW 0x5EC5D1)" % first):
		return false
	_check(flow.mission == first and flow.ended_log.is_empty(), "the restarted mission is %d, nothing ended" % flow.mission)
	await game._wait_seconds(3.0)
	if not _check(await _quit_menu("ExitMission"), "the in-game menu's Exit Mission and its Yes"):
		return false
	if not _check(await _continue_score(), "Exit Mission led to the score screen and its Continue ran RW 0x927EC6"):
		return false
	if not _check(await _wait_until(_menu_shown, 30.0), "CampaignMenu.apt after Exit Mission"):
		return false
	_check(flow.mission == first and not flow.victorious, "the menu names the left mission %d as failed" % flow.mission)
	await game._wait_seconds(3.0)
	_shot("menu-after-exit")
	return true


func _run_single_player() -> bool:
	if not _check(await _wait_until(func(): return game._state == game.State.MENU and game._menu_revealed, 60.0), "the main menu"):
		return false
	await game._wait_seconds(3.0)
	_shot("main-menu")
	var level: int = game._shell.shell_top_level()
	var nav: Dictionary = game._shell.find_button(level, "SoloPlayNav")
	if not _check(nav.get("found", false), "the main menu's SoloPlayNav"):
		return false
	await game._click_button(level, String(nav.path))
	await game._wait_seconds(3.0)
	_shot("single-player-%s" % ("unlocked" if game._shell.is_bonus_campaign_unlocked() else "locked"))
	print("CAMPSAVE1 single player page, bonus campaign unlocked: %s" % str(game._shell.is_bonus_campaign_unlocked()))
	# lane CAMPMENU-1: retail's list is the campaigns' only entry point (no OpenBFME panel); the bonus campaign's face follows the run's own Options.ini
	# (BCU = true, read here from the file, not from the shell): its button, else its bttnDisabled face (MainMenu.apt, MainMenuUnlockBonusCampaign)
	var unlocked: bool = not game._options_file.is_empty() and FileAccess.get_file_as_string(game._options_file).contains("BCU = true")
	var entries: Array = game.solo_play_entries()
	entries.sort()
	var expected := ["BonusCampaign." + ("bttn" if unlocked else "bttnDisabled"), "Expansion1Campaign.bttn", "LoadGame.bttn", "Skirmish.bttn", "WarOfTheRing.bttn"]
	return _check(entries == expected, "the Solo Play list's entries %s are retail's %s (Options.ini BCU %s)" % [entries, expected, unlocked])


func _run_eight() -> bool:
	flow.auto_win_all = true
	var camp: Dictionary = {}
	for c in game._world.get_campaigns():
		if String(c.name) == flow.campaign:
			camp = c
	var missions: Array = camp.get("missions", [])
	if not _check(missions.size() == 8 and flow.mission == 0, "%s has 8 missions and starts at mission 0 (%d, %d)" % [flow.campaign, missions.size(), flow.mission]):
		return false
	var autos: Array = []
	for i in 8:
		var m: Dictionary = missions[i]
		if not _check(await _wait_until(func(): return flow.ended_log.size() >= i + 1, 400.0), "mission %d (%s) ended" % [i, m.name]):
			return false
		_check(String(flow.ended_log[i]).contains(" victory ") and String(flow.ended_log[i]).contains(String(m.map)), "mission %d won on %s: %s" % [i, m.map, flow.ended_log[i]])
		if not _check(await _continue_score(), "mission %d: the score screen's Continue ran RW 0x927EC6" % i):
			return false
		if i == 7:
			break
		if not _check(await _wait_until(_menu_shown, 30.0), "mission %d: CampaignMenu.apt is shown" % i):
			return false
		var next: Dictionary = missions[i + 1]
		var path: String = flow.last_auto_save
		var fresh := not path.is_empty() and not autos.has(path)
		autos.append(path)
		_check(fresh and FileAccess.file_exists(path), "mission %d: a new auto-save was written (%s)" % [i, path.get_file()])
		var check: Dictionary = game._world.check_save(path)
		_check(check.get("ok", false) and int(check.get("save_point", 0)) == 1 and check.get("auto_save", false) and int(check.get("mission", -1)) == i + 1
			and check.get("victorious", false) and check.get("ready", false) and check.get("advanced", false),
			"mission %d: its header is an auto-save naming mission %d, won, ready, advanced: %s" % [i, i + 1, JSON.stringify(check)])
		var listed := false
		for sv in game._world.list_saves(game._save_flow.save_dir()):
			listed = listed or (String(sv.path) == path and sv.get("auto_save", false) and int(sv.kind) == 1 and sv.get("loadable", false))
		_check(listed, "mission %d: the Load page lists it as a loadable auto-save of the Campaign page" % i)
		var menu: Dictionary = flow.step("menu").get("menu", {})
		_check(int(menu.get("mission_index", -1)) == i + 1 and String(menu.get("mission_name", "")) == String(next.name)
			and String(menu.get("load_screen_image", "")) == String(next.load_screen_image),
			"mission %d: the menu names %s with the picture %s" % [i, next.name, next.load_screen_image])
		var label: Dictionary = game._shell.fetch_text("CampaignName:" + String(next.name))
		_check(label.get("found", false), "mission %d: lotr.str has CampaignName:%s = '%s'" % [i, next.name, label.get("text", "")])
		_shot("menu-%d-%s" % [i + 1, String(next.name)])
		var begun: int = flow.begun
		if not _check(await _click("Buttons.NextMission"), "mission %d: Next Mission" % i):
			return false
		if not _check(await _wait_until(func(): return flow.begun == begun + 1 and game._state == game.State.PLAYING, 240.0), "mission %d started (%s)" % [i + 1, next.map]):
			return false
		_check(flow.mission == i + 1 and not flow.victorious and not flow.campaign_ready and not flow.advanced, "mission %d: the flags were cleared by its start (RW 0x5EAB8F)" % (i + 1))
	_check(autos.size() == 7, "seven auto-saves, one per won mission before the last (%d)" % autos.size())
	if not _check(await _wait_until(func(): return game._state == game.State.MENU and _top() == "MainMenu.apt", 30.0), "the main menu after the eighth mission"):
		return false
	_check(not flow.menu_open and game._shell.is_bonus_campaign_unlocked() and flow.last_unlock.get("ok", false),
		"no campaign menu after the eighth mission; the bonus campaign is unlocked: %s" % JSON.stringify(flow.last_unlock))
	_check(flow.last_auto_save == autos[6], "the last mission wrote no auto-save")
	await game._wait_seconds(3.0)
	_shot("main-menu")
	return true


## lane BFME2CAMP1: the campaign's carry-over from a won mission to the next one, through the score screen, the auto-save and the menu
func _run_carry() -> bool:
	flow.auto_win_all = true
	if not _check(flow.campaign == "GOOD_CAMPAIGN" and flow.mission == 0, "GOOD_CAMPAIGN starts at mission 0 (%s %d)" % [flow.campaign, flow.mission]):
		return false
	if not _check(await _wait_until(func(): return flow.ended_log.size() >= 1, 400.0), "MAP GOOD Rivendell ended"):
		return false
	_check(String(flow.ended_log[0]).contains(" victory "), "Rivendell won: %s" % flow.ended_log[0])
	if not _check(await _continue_score(), "the score screen's Continue ran RW 0x927EC6"):
		return false
	if not _check(await _wait_until(_menu_shown, 30.0), "CampaignMenu.apt is shown"):
		return false
	_check(flow.carryover.contains(" ElvenGlorfindel") and flow.carryover.contains(" DwarvenGloin") and flow.carryover.contains(" ElvenElrond"),
		"the campaign's carry-over holds Glorfindel, Gloin and the held-back Elrond: %s" % flow.carryover.replace("\n", " | ").left(300))
	var check: Dictionary = game._world.check_save(flow.last_auto_save)
	_check(check.get("ok", false) and String(check.get("carryover", "")) == flow.carryover, "the auto-save %s holds the same carry-over" % flow.last_auto_save.get_file())
	_shot("menu-high-pass")
	var begun: int = flow.begun
	if not _check(await _click("Buttons.NextMission"), "Next Mission"):
		return false
	if not _check(await _wait_until(func(): return flow.begun == begun + 1 and game._state == game.State.PLAYING, 240.0), "MAP GOOD High Pass started"):
		return false
	var found := func() -> bool:
		return game._world.debug_script_unit("Glorfindel").get("ok", false) and game._world.debug_script_unit("Gloin").get("ok", false)
	if not _check(await _wait_until(found, 60.0), "High Pass's intro spawned Glorfindel and Gloin from the carry-over"):
		return false
	print("CAMPSAVE1 carry: Glorfindel %s, Gloin %s" % [JSON.stringify(game._world.debug_script_unit("Glorfindel")), JSON.stringify(game._world.debug_script_unit("Gloin"))])
	for i in 4: # the intro's camera moves over the heroes' arrival: a picture every few seconds (with --screens)
		await game._wait_seconds(3.0 + 3.0 * i)
		_shot("high-pass-heroes-%d" % i)
	return true
