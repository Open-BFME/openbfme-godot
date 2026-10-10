## Headless test of lane CAMP-2 (the owner's campaign report, 2026-10-09): every way into a game takes the shell's pictures away and every way back
## brings them back, read from the scene (AptMenuPlayer.get_backdrop_state: the backdrop the canvas drew, the front-end background's visible commands in
## the render list), the real game shows no developer text (game.gd _dev_text_on_screen) and a cinematic's letterbox hides the Palantir as retail's
## doLetterBoxMode (RW 0x7BC8B7: HideControlBar). scenes/game.tscn runs as a child process:
##   1. the main menu's Bonus campaign (AptMainMenu::BonusCampaign), its mission won by the test hooks, the way back to the main menu;
##   2. --campaign=ANGMAR_CAMPAIGN --mission=2 (Amon Sul) won, the next mission (Dark Eye) starts; with --dev-overlay (the check sees the overlay);
##   3. the main menu's Continue (AptMainMenu::ContinueCampaign: the progress run 2 saved);
##   4. a skirmish from the menu, its score screen's Continue, the main menu's Load Replay and the replay;
##   5. the movies: the bonus mission's intro movie whole (frames, length, audio events); 6. the campaign's opening movie skipped as by Esc; 7. the
##   start-up movies.
## (Restart: end_test.gd --quit-restart; a LAN game: net_end_test.gd.)
##
##   godot --headless --path godot --script res://tests/camp2_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failures := 0
const GONE := "backdrop '', background mode 0 "


func _initialize() -> void:
	var code: int = _run()
	print("CAMP2 %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


## runs game.tscn with `args`; `expect_exit` (-1: any) is checked
func _game(args: Array, expect_exit := 0) -> String:
	var output: Array = []
	var code := OS.execute(OS.get_executable_path(), ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--"] + args, output, true)
	var text: String = "".join(output)
	if expect_exit >= 0:
		_check(code == expect_exit, "%s: game.tscn exits %d (got %d)" % [" ".join(args), expect_exit, code])
	var lines: PackedStringArray = []
	for line in text.split("\n"):
		if line.contains("SHELL PICTURES") or line.contains("control bar") or line.begins_with("GAME FAIL") or line.begins_with("CAMPAIGN end") \
				or line.contains("movie") or line.contains("MOVIE"):
			lines.append(line)
	print("    " + "\n    ".join(lines))
	return text


## the SHELL PICTURES line of `where`, "" when there is none
func _line(text: String, where: String) -> String:
	for line in text.split("\n"):
		if line.begins_with("GAME SHELL PICTURES %s:" % where):
			return line
	return ""


## the playback record of `title` (a "CAMPAIGN movie: {json}" line), {} when there is none
func _movie(text: String, title: String) -> Dictionary:
	return _json_line(text, "CAMPAIGN movie: ", title)


func _intro_movie(text: String, title: String) -> Dictionary:
	return _json_line(text, "GAME intro movie: ", title)


func _json_line(text: String, prefix: String, title: String) -> Dictionary:
	for line in text.split("\n"):
		if line.begins_with(prefix):
			var d = JSON.parse_string(line.substr(prefix.length()))
			if d is Dictionary and d.get("title", "") == title:
				return d
	return {}


func _movie_error(text: String, title: String) -> String:
	for line in text.split("\n"):
		var at := line.find("MOVIE ERROR %s: " % title)
		if at >= 0:
			return line.substr(at + ("MOVIE ERROR %s: " % title).length())
	return ""


## the install's Data/Movies has `file` (any case)
func _install_has_movie(file: String) -> bool:
	var root := OS.get_environment("ROTWK_INSTALL")
	for data in DirAccess.get_directories_at(root):
		if data.to_lower() != "data":
			continue
		for movies in DirAccess.get_directories_at(root.path_join(data)):
			if movies.to_lower() != "movies":
				continue
			for f in DirAccess.get_files_at(root.path_join(data).path_join(movies)):
				if f.to_lower() == file.to_lower():
					return true
	return false


func _in_game_clean(text: String, where: String, dev_text_expected := false) -> void:
	var l := _line(text, "in game (%s)" % where)
	_check(not l.is_empty() and l.contains(GONE), "in game (%s): no backdrop and no front-end background on screen" % where)
	if dev_text_expected:
		_check(not l.is_empty() and not l.contains("developer text []"), "in game (%s) with --dev-overlay: the check sees the developer overlay" % where)
	else:
		_check(not l.is_empty() and l.ends_with("developer text []"), "in game (%s): no developer text on screen" % where)


func _back_in_shell(text: String) -> void:
	# the shell after the game draws its backdrop again (ShellMapLowLOD, as at the start; the front-end background follows the screen shown)
	var back := _line(text, "back in the shell")
	_check(not back.is_empty() and back.contains("backdrop 'ShellMapLowLOD'"), "back in the shell: the backdrop is drawn again (%s)" % back)


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("CAMP2 SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
		return 77
	# 1. the main menu's campaign button, the mission, the way back
	var t1 := _game(["--campaign-menu=BonusCampaign:N", "--campaign-win", "--movies=off", "--shell-pictures-quit=3"])
	_check(t1.contains("GAME campaign menu: AptMainMenu::BonusCampaign") and t1.contains("CAMPAIGN start ANGMAR_BONUS_CAMPAIGN"), "1: the main menu started the bonus campaign")
	_in_game_clean(t1, "campaign ANGMAR_BONUS_CAMPAIGN 0")
	_check(t1.contains("CAMPAIGN back to the main menu"), "1: the campaign returned to the main menu")
	_back_in_shell(t1)
	# the cinematic's letterbox hides the Palantir (HideControlBar) and its end shows it again, read from the render list
	_check(t1.contains("GAME control bar drawn after the hide: false"), "1: the letterbox hid the Palantir (RW 0x7BC8B7)")
	_check(t1.contains("GAME control bar drawn after the show: true"), "1: the letterbox's end showed the Palantir again")
	# 2. a mission won, the next one starts (the progress is saved for 3)
	var t2 := _game(["--campaign=ANGMAR_CAMPAIGN", "--mission=2", "--campaign-win", "--movies=off", "--dev-overlay", "--shell-pictures-quit=2"])
	_in_game_clean(t2, "campaign ANGMAR_CAMPAIGN 2", true)
	_in_game_clean(t2, "campaign ANGMAR_CAMPAIGN 3", true)
	# 3. the main menu's Continue
	var t3 := _game(["--campaign-menu=ContinueCampaign", "--movies=off", "--shell-pictures-quit=2"])
	_check(t3.contains("CAMPAIGN continue ANGMAR_CAMPAIGN mission 3"), "3: Continue loaded the saved progress")
	_in_game_clean(t3, "campaign ANGMAR_CAMPAIGN 3")
	# 5. lane CAMP-2's movies: the bonus campaign's mission intro (Angmar_Campaign_BonusOpen, BABMO.vp6) plays whole after the loading screen: every
	# frame shown, its length the file's, its narration / music events started; then the mission with no shell pictures
	var t5 := _game(["--campaign-menu=BonusCampaign:N", "--shell-pictures-quit=2"])
	var m5 := _movie(t5, "Angmar_Campaign_BonusOpen")
	_check(not m5.is_empty() and m5.error == "" and not m5.skipped, "5: the bonus mission's intro movie played to its end (%s)" % str(m5))
	if not m5.is_empty():
		_check(int(m5.frame) == int(m5.frames) - 1 and int(m5.frames_decoded) == int(m5.frames), "5: every frame was decoded and the last one shown")
		_check(absf(float(m5.elapsed_ms) - float(m5.duration_ms)) < 1500.0, "5: it ran for the file's length (%.0f ms of %.0f)" % [m5.elapsed_ms, m5.duration_ms])
		_check(int(m5.distinct_pictures) > 50, "5: the picture changed (%d distinct frames seen)" % m5.distinct_pictures)
		_check(int(m5.audio_started) >= 1, "5: its audio events started (%s)" % str(m5.audio_events))
	_check(t5.find("CAMPAIGN movie:") < t5.find("GAME SHELL PICTURES in game"), "5: the movie played before the mission started (after its loading screen)")
	_in_game_clean(t5, "campaign ANGMAR_BONUS_CAMPAIGN 0")
	# 6. the campaign's opening movie (Angmar_Campaign_Intro, BACO.vp6) from the main menu, skipped as by Esc after 3 s, then the first mission's intro
	var t6 := _game(["--campaign-menu=Expansion1Campaign:N", "--movie-skip-after=3000", "--shell-pictures-quit=2"])
	var m6 := _movie(t6, "Angmar_Campaign_Intro")
	_check(not m6.is_empty() and m6.error == "" and m6.skipped and int(m6.frame) >= 60 and int(m6.distinct_pictures) > 10,
		"6: the campaign's opening movie played (its frames advanced) and Esc skipped it (%s)" % str(m6))
	var m6b := _movie(t6, "Angmar_Campaign_M1Open")
	_check(not m6b.is_empty() and m6b.error == "", "6: the first mission's intro movie played after its loading screen")
	_check(not t6.contains("MOVIE ERROR"), "6: no movie error")
	_in_game_clean(t6, "campaign ANGMAR_CAMPAIGN 0")
	# 7. the start-up movies (EALogoMovie, NewLineLogo, TolkienLogo, Overall_Game_Intro), each skipped after 1 s. A movie whose file the install lacks
	# (some copies of the game have no logo movies) is reported (MOVIE ERROR) and skipped without a picture, as retail's display skips a stream that
	# does not open (lane PLAY-3: RW 0x65C67E / 0x65D3F5, scripts/movie_player.gd); the player sees no error
	var t7 := _game(["--intro-check", "--movie-skip-after=1000"], -1)
	var missing := 0
	for title in ["EALogoMovie", "NewLineLogo", "TolkienLogo", "Overall_Game_Intro"]:
		var mi := _intro_movie(t7, title)
		var err := _movie_error(t7, title)
		if not mi.is_empty():
			_check(mi.error == "" and int(mi.frame) >= 0, "7: the start-up movie %s played (%s)" % [title, str(mi)])
		else:
			var file := err.get_slice(" - ", 1).get_slice(" (tried", 0)
			var absent := not file.is_empty() and not _install_has_movie(file + ".vp6")
			_check(absent, "7: the start-up movie %s is reported missing only when the install lacks %s.vp6 (%s)" % [title, file, err])
			_check(t7.contains("MOVIE %s not played:" % title), "7: the missing movie %s is skipped as retail (no picture)" % title)
			missing += 1
	if missing > 0:
		print("    NOTE: this install lacks %d of the start-up movies; each was reported (MOVIE ERROR)" % missing)
	else:
		_check(t7.contains("GAME intro movies done: 0 errors"), "7: the start-up movies played without errors")
	_check(not t7.contains("MOVIE ERROR shown to the player"), "7: no movie error is shown to the player")
	# 8. a movie that cannot be played never stops the game (review r1): the bonus mission's intro opens a file whose header has the picture size 0 x 400
	# (never reaching Godot's Image), then a file that does not exist; each is a MOVIE ERROR in the report, skipped without a picture as retail skips a
	# stream that does not open (lane PLAY-3: RW 0x65C67E), and the mission starts
	var bad := ProjectSettings.globalize_path("user://camp2-bad-size.vp6")
	var f := FileAccess.open(bad, FileAccess.WRITE)
	var head := PackedByteArray([0x4D, 0x56, 0x68, 0x64, 32, 0, 0, 0, 0x76, 0x70, 0x36, 0x30, 0, 0, 144, 1, 1, 0, 0, 0, 0, 0, 0, 0, 30, 0, 0, 0, 1, 0, 0, 0,
		0x4D, 0x56, 0x30, 0x4B, 12, 0, 0, 0, 0, 0, 0, 0])
	f.store_buffer(head)
	f.close()
	for spec in [[bad, "picture size 0 x 400"], [ProjectSettings.globalize_path("user://camp2-no-such-movie.vp6"), "cannot open"]]:
		var t8 := _game(["--campaign-menu=BonusCampaign:N", "--movie-file=Angmar_Campaign_BonusOpen=" + spec[0], "--shell-pictures-quit=2"])
		var err := _movie_error(t8, "Angmar_Campaign_BonusOpen")
		_check(err.contains(spec[1]), "8: the broken movie is a MOVIE ERROR naming the problem (%s)" % err)
		_check(t8.contains("MOVIE Angmar_Campaign_BonusOpen not played:") and not t8.contains("MOVIE ERROR shown to the player"),
			"8: the movie is skipped without a picture or a message, as retail")
		_in_game_clean(t8, "campaign ANGMAR_BONUS_CAMPAIGN 0")
	DirAccess.remove_absolute(bad)
	# 4. a skirmish, the way back, a replay
	var t4 := _game(["--auto", "--replay-menu-test", "--advance=8", "--seed=42"])
	_in_game_clean(t4, "skirmish")
	_back_in_shell(t4)
	_in_game_clean(t4, "replay")
	return 0 if _failures == 0 else 1
