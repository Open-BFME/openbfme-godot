## Headless test of lane PLAY-3 (the owner's play session on the v0.3.0-preview.1 candidate, 2026-10-10). scenes/game.tscn runs as a child process:
##   1. dragbox: a skirmish (--auto), a left drag around the own units by InputEvents pushed into the viewport (scripts/play1_player.gd _scenario_dragbox):
##      the box RotWK's W3DInGameUI::drawSelectionRegion draws (RW 0x48ECF4) is on screen while the button is held, follows the pointer and selects the own
##      units inside at the release.
##   2. ghosts, for every playable faction (--faction): the builder is selected by a click, each of its DOZER_CONSTRUCT buttons is clicked and the
##      pointer moves over the ground: the placement ghost of that structure is drawn (a fortress as its castle layout), untinted on a legal site and
##      tinted on an illegal one; a right click ends each placement.
##   3. a new campaign from the main menu (AptMainMenu::Expansion1Campaign): the shell music stops before the campaign's opening movie (RW 0x91BCD2 ->
##      RW 0x75D9B1), so no menu music plays under it: neither the track nor its voice (playing or fading) while the movie plays.
##
##   godot --headless --path godot --script res://tests/play3_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = _run()
	print("PLAY3 %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


## runs game.tscn with `args`; the exit code is checked against `expect_exit` (-1: any); prints the lines that begin with one of `prefixes`
func _game(args: Array, prefixes: Array, expect_exit := 0) -> String:
	var output: Array = []
	var code := OS.execute(OS.get_executable_path(), ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--"] + args, output, true)
	var text: String = "".join(output)
	if expect_exit >= 0:
		_check(code == expect_exit, "%s: game.tscn exits %d (got %d)" % [" ".join(args), expect_exit, code])
	var lines: PackedStringArray = []
	for line in text.split("\n"):
		for p in prefixes:
			if line.begins_with(p):
				lines.append(line)
				break
	print("    " + "\n    ".join(lines))
	return text


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return 77
	if not ClassDB.class_exists("GameWorld"):
		print("FAIL: the openbfme extension is not loaded")
		return 1
	# 1. the drag box (owner: "no left-click drag box in skirmish")
	var t1 := _game(["--auto", "--play1=dragbox", "--seed=42"], ["PLAY1", "PLAY3", "GAME FAIL"])
	_check(t1.contains("PLAY1 dragbox: ok"), "1: the drag box is drawn while dragging, follows the pointer and selects the own units inside")
	# 2. the placement ghost of every structure of every faction's builder (owner: "no placement preview when building with builders")
	for faction in ["FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar"]:
		var t2 := _game(["--auto", "--play1=ghosts", "--seed=42", "--faction=" + faction], ["PLAY1", "PLAY3 ghosts: %s:" % faction, "PLAY3 ghosts: %s," % faction, "GAME FAIL"])
		_check(t2.contains("PLAY1 ghosts: ok"), "2: %s: every structure its builder offers shows its placement ghost" % faction)
	# 3. the menu music under the campaign's opening movie (owner: "the menu music keeps playing during the opening cinematic")
	var t3 := _game(["--campaign-menu=Expansion1Campaign:N", "--movie-skip-after=3000", "--shell-pictures-quit=2"], ["CAMPAIGN start", "CAMPAIGN shell music",
		"CAMPAIGN STOP", "CAMPAIGN movie:", "MOVIE ERROR", "GAME FAIL"])
	var intro := {}
	for line in t3.split("\n"):
		if line.begins_with("CAMPAIGN movie: "):
			var d = JSON.parse_string(line.substr(16))
			if d is Dictionary and d.get("title", "") == "Angmar_Campaign_Intro":
				intro = d
	_check(t3.contains("CAMPAIGN shell music stopped"), "3: the campaign start stops the shell music (RW 0x91BCD2)")
	_check(not intro.is_empty() and str(intro.get("music_before", "?")) == "", "3: no music plays when the campaign's opening movie starts (%s)" % str(intro.get("music_before", "no movie")))
	var was := ""
	for line in t3.split("\n"):
		if line.begins_with("CAMPAIGN shell music stopped"):
			was = line.get_slice("'", 1)
	_check(not was.is_empty() and intro.has("other_voices") and not Array(intro.other_voices).has(was),
		"3: the shell music's voice (%s) is gone, not fading, while the movie plays (other voices %s)" % [was, str(intro.get("other_voices", "?"))])
	return 0 if _failures == 0 else 1
