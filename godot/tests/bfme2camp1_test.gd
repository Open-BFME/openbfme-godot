## Headless test of lane BFME2CAMP1: the BFME2 Good / Evil campaigns start from BFME2's own main menu (the BFME2 menu style, Options.ini
## OpenBFMEMainMenuStyle = bfme2) with the player's clicks: Solo Play, BFME2 MainMenu.apt's SoloPlayNav.GoodCampaign (or EvilCampaign), the movie's
## LevelOfDifficulty choice -> AptMainMenu::GoodCampaign / EvilCampaign (RotWK's RW 0x91AF46 / 0x91AF69 -> RW 0x91BE05 / 0x91BDA6: GOOD_CAMPAIGN /
## EVIL_CAMPAIGN at that difficulty, game.gd start_bfme2_campaign), then the campaign's opening movie (skipped as by Esc) and its first mission:
##   1. Good, Easy: GOOD_CAMPAIGN at difficulty 0, Good_Campaign_Intro, then MAP GOOD Rivendell in game;
##   2. Evil, Hard: EVIL_CAMPAIGN at difficulty 2, then MAP EVIL Lorien;
##   3. (an OpenBFME addition, AptMainMenu.h ExtraRows) the BFME2 style's added Angmar Campaign row, Easy: ANGMAR_CAMPAIGN at difficulty 0;
##   4. the RotWK style (the default Options.ini) and its added Good Campaign row, Medium: GOOD_CAMPAIGN at difficulty 1;
##   5. the campaign's carry-over (scripts/campaign_save_test.gd "carry"): Rivendell won, its auto-save, Next Mission: High Pass spawns the carried
##      Glorfindel and Gloin.
## scenes/game.tscn runs as a child process with an isolated Options.ini.
##
##   godot --headless --path godot --script res://tests/bfme2camp1_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failures := 0
var _options := ""


func _initialize() -> void:
	var code: int = _run()
	print("BFME2CAMP1 %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


## runs game.tscn with `args` and the test's Options.ini
func _game(args: Array) -> String:
	var output: Array = []
	var code := OS.execute(OS.get_executable_path(), ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--",
		"--options-file=" + _options] + args, output, true)
	var text: String = "".join(output)
	_check(code == 0, "%s: game.tscn exits 0 (got %d)" % [" ".join(args), code])
	for line in text.split("\n"):
		if line.begins_with("GAME clicked") or line.begins_with("CAMPAIGN") or line.begins_with("GAME FAIL") or line.contains("SHELL PICTURES in game") \
				or line.begins_with("GAME stop") or line.contains("SCRIPT ERROR") or line.begins_with("CAMPSAVE1"):
			print("    ", line)
	return text


## the run's "GAME clicked _levelN.<path>" lines name `paths` in this order (each the clicked button itself or the clip it is under)
func _clicked(text: String, paths: Array) -> bool:
	var at := 0
	for path in paths:
		var found := -1
		for line in text.substr(at).split("\n"):
			if line.begins_with("GAME clicked _level"):
				var rest := line.substr(line.find(".") + 1)
				if rest == path or rest.begins_with(path + "."):
					found = text.find(line, at)
					break
		if found < 0:
			return false
		at = found + 1
	return true


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("BFME2CAMP1 SKIP: ROTWK_INSTALL and BFME2_INSTALL required")
		return 77
	var directory := OS.get_user_data_dir().path_join("bfme2camp1-test-%d" % OS.get_process_id())
	DirAccess.make_dir_recursive_absolute(directory)
	_options = directory.path_join("Options.ini")
	var f := FileAccess.open(_options, FileAccess.WRITE)
	f.store_string("OpenBFMEMainMenuStyle = bfme2\n")
	f.close()
	# 1. the Good campaign at Easy
	var t1 := _game(["--campaign-click=GoodCampaign:Easy", "--movie-skip-after=3000", "--shell-pictures-quit=2"])
	_check(_clicked(t1, ["SoloPlayNav", "SoloPlayNav.GoodCampaign", "LevelOfDifficulty.Easy"]), "1: Solo Play, Good Campaign, Easy were clicked")
	_check(t1.contains("CAMPAIGN start GOOD_CAMPAIGN (difficulty 0): 8 missions"), "1: BFME2's Good button started GOOD_CAMPAIGN at Easy (8 missions)")
	_check(not t1.contains("The BFME2 Good campaign is not available"), "1: no 'not available' box")
	_check(t1.contains("Good_Campaign_Intro"), "1: the campaign's opening movie Good_Campaign_Intro was asked for")
	_check(t1.contains("SHELL PICTURES in game (campaign GOOD_CAMPAIGN 0)"), "1: the first mission (MAP GOOD Rivendell) is in game")
	# 2. the Evil campaign at Hard
	var t2 := _game(["--campaign-click=EvilCampaign:Hard", "--movies=off", "--shell-pictures-quit=2"])
	_check(_clicked(t2, ["SoloPlayNav", "SoloPlayNav.EvilCampaign", "LevelOfDifficulty.Hard"]), "2: Solo Play, Evil Campaign, Hard were clicked")
	_check(t2.contains("CAMPAIGN start EVIL_CAMPAIGN (difficulty 2): 8 missions"), "2: BFME2's Evil button started EVIL_CAMPAIGN at Hard (8 missions)")
	_check(t2.contains("SHELL PICTURES in game (campaign EVIL_CAMPAIGN 0)"), "2: the first mission (MAP EVIL Lorien) is in game")
	# 3. the BFME2 style's Angmar Campaign row (a copy of its Good Campaign row sending AptMainMenu::Expansion1Campaign)
	var t3 := _game(["--campaign-click=Expansion1Campaign:Easy", "--movies=off", "--shell-pictures-quit=2"])
	_check(_clicked(t3, ["SoloPlayNav", "SoloPlayNav.Expansion1Campaign", "LevelOfDifficulty.Easy"]), "3: Solo Play, Angmar Campaign, Easy were clicked (BFME2 style)")
	_check(t3.contains("CAMPAIGN start ANGMAR_CAMPAIGN (difficulty 0): 8 missions"), "3: the added row started ANGMAR_CAMPAIGN at Easy")
	# 4. the RotWK style: its Good Campaign row (a copy of its Angmar row sending AptMainMenu::GoodCampaign)
	f = FileAccess.open(_options, FileAccess.WRITE)
	f.store_string("OpenBFMEMainMenuStyle = rotwk\n")
	f.close()
	var t4 := _game(["--campaign-click=GoodCampaign:Medium", "--movies=off", "--shell-pictures-quit=2"])
	_check(_clicked(t4, ["SoloPlayNav", "SoloPlayNav.GoodCampaign", "LevelOfDifficulty.Medium"]), "4: Solo Play, Good Campaign, Medium were clicked (RotWK style)")
	_check(t4.contains("CAMPAIGN start GOOD_CAMPAIGN (difficulty 1): 8 missions"), "4: the added row started GOOD_CAMPAIGN at Medium")
	_check(t4.contains("SHELL PICTURES in game (campaign GOOD_CAMPAIGN 0)"), "4: MAP GOOD Rivendell is in game")
	# 5. the carry-over from Rivendell to High Pass
	var saves := directory.path_join("save")
	DirAccess.make_dir_recursive_absolute(saves)
	var t5 := _game(["--campaign=GOOD_CAMPAIGN", "--mission=0", "--campaign-win", "--save-dir=" + saves, "--campsave-test=carry", "--movies=off"])
	_check(t5.contains("CAMPSAVE1 RESULT: ok"), "5: Rivendell won, its carry-over saved and spawned in High Pass")
	for file in DirAccess.get_files_at(saves):
		DirAccess.remove_absolute(saves.path_join(file))
	DirAccess.remove_absolute(saves)
	for file in DirAccess.get_files_at(directory):
		DirAccess.remove_absolute(directory.path_join(file))
	DirAccess.remove_absolute(directory)
	return 0 if _failures == 0 else 1
