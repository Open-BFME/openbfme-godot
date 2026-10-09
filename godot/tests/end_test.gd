## Headless test of the end of a skirmish (lane END-1): the whole flow of scenes/game.tscn as a child process, twice. --end: main menu -> lobby -> live game, the
## enemy's base is destroyed (the start option test_hooks), the human side's script shows the victory screen, it hides after 7 s, the game is left to the score
## screen (TimeLine.apt), its tabs switch the graph, More shows the statistics page, Continue returns to the Skirmish lobby and a second game starts.
## --end-lose: the same with the local player destroyed (the defeat screen of VictoryConditions' local defeat); --end-esc leaves through the quit menu during
## the end screen. Lane END-2 adds --quit (the quit menu: Resume, Exit, the way back) and --quit-restart (Restart).
##
##   godot --headless --path godot --script res://tests/end_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = _run()
	print("END %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("END SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
		return 77
	var godot_path := OS.get_executable_path()
	var project := ProjectSettings.globalize_path("res://")
	# variant: flag, faction, end screen label, the ShowEndGame argument (RW 0x808E5B: "0" for the evil side, "1" for the good one: Men must not play the evil movie)
	for variant in [["--end", "FactionMen", "APT:EndVictorious", "1"], ["--end-lose", "FactionMordor", "APT:EndDefeat", "0"], ["--end-esc", "FactionMen", "APT:EndVictorious", "1"]]:
		var output: Array = []
		var exit_code := OS.execute(godot_path, ["--headless", "--path", project, "--", "--auto", variant[0], "--faction=" + variant[1], "--advance=2", "--seed=42"], output, true)
		var text: String = "".join(output)
		_check(exit_code == 0, "%s %s: game.tscn exits 0 (got %d)" % [variant[0], variant[1], exit_code])
		_check(text.contains("\"kind\":\"show_end_game\"") and text.contains("\"text\":\"%s\"" % variant[2]), "%s: the end screen %s was requested" % [variant[0], variant[2]])
		_check(text.contains("GAME END show_end_game: {\"error\":\"\"") and text.contains("\"flag\":\"%s\"" % variant[3]), "%s: GuiFX.apt's ShowEndGame ran with the argument %s" % [variant[0], variant[3]])
		if variant[0] == "--end-esc":
			# Esc during the seven-second display: the movie is hidden before the game is cleared, and nothing is left showing
			_check(text.contains("GAME END hide_end_game on leaving: {\"error\":\"\",\"level\":11,\"ok\":true}"), "--end-esc: HideEndGame ran when the game was left")
			_check(text.contains("GAME END after Esc: end screen still showing: false"), "--end-esc: no end screen is left over the score screen")
		else:
			_check(text.contains("\"kind\":\"hide_end_game\"") and text.contains("MPorSkirmishFadeToScoreScreen"), "%s: the end screen hid and the sound faded" % variant[0])
		_check(text.contains("GAME screen: TimeLine.apt"), "%s: the score screen TimeLine.apt was shown" % variant[0])
		_check(text.contains("GAME shell request: ScoreScreenContinue"), "%s: its Continue button asked for the menu" % variant[0])
		_check(text.contains("GAME END RESULT: end screen true, score screen true, continue true, second game true"), "%s: back at the main menu a second game started and ran" % variant[0])
		# lane END-2: the score screen's tabs switch the graph, the statistics page lists retail's rows, Continue shows the Skirmish lobby again
		for tab in ["Units", "Structures", "Resources", "FinalScore"]:
			_check(text.contains("GAME END tab %s: graph mode %s" % [tab, tab]), "%s: the %s tab plots its series" % [variant[0], tab])
		_check(text.contains("GAME END stats row: Session Length#ffffffff") and text.contains("Structures Created#ffffffff") and not text.contains("MISSING"), "%s: the statistics page lists the STAT:RTS rows" % variant[0])
		_check(text.contains("GAME END Continue showed: [\"MainMenu.apt\", \"Skirmish.apt\"]"), "%s: Continue returned to the Skirmish lobby (S-1771)" % variant[0])
		# lane QA2-FIX (QA-2 #5): the main menu under the lobby is hidden (its movie loaded before the lobby covered it)
		_check(text.contains("GAME END Continue covered screens hidden: true"), "%s: the main menu does not show through the lobby after the game" % variant[0])
		_check(text.count("GAME STOP [S-1771]") == 1, "%s: the stop S-1771 is reported once" % variant[0])
		if exit_code != 0:
			for line in text.split("\n"):
				if line.begins_with("GAME FAIL") or line.contains("handle_crash"):
					print("    ", line)
	# lane END-2: the quit menu over a skirmish
	var out2: Array = []
	var code2 := OS.execute(godot_path, ["--headless", "--path", project, "--", "--auto", "--quit", "--faction=FactionMen", "--advance=2", "--seed=42"], out2, true)
	var t2: String = "".join(out2)
	_check(code2 == 0, "--quit: game.tscn exits 0 (got %d)" % code2)
	_check(t2.contains("\"popup_type\":\"Restart\"") and t2.contains("\"disabled\":[]"), "--quit: a skirmish's quit menu offers Restart and disables nothing (RW 0x921D5B)")
	_check(t2.contains("paused true"), "--quit: the skirmish pauses while the menu is open")
	_check(t2.contains("GAME QUIT action: QuitMenuReturn") and t2.contains("GAME QUIT resumed: true"), "--quit: Resume closed the menu and the game went on")
	_check(t2.contains("GAME QUIT action: QuitMenuExit") and t2.contains("GAME QUIT RESULT exit: ok"), "--quit: Exit and its confirmation left to the score screen")
	_check(t2.contains("GAME END Continue showed: [\"MainMenu.apt\", \"Skirmish.apt\"]") and t2.contains("second game true"), "--quit: Continue returned to the Skirmish lobby and a second game ran")
	var out3: Array = []
	var code3 := OS.execute(godot_path, ["--headless", "--path", project, "--", "--auto", "--quit-restart", "--faction=FactionMordor", "--advance=2", "--seed=42"], out3, true)
	var t3: String = "".join(out3)
	_check(code3 == 0 and t3.contains("GAME QUIT RESULT: ok"), "--quit-restart: Restart and its confirmation started the same game again (same seed)")
	return 0 if _failures == 0 else 1
