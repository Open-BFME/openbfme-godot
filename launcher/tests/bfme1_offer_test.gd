## Community PR #36: the optional BFME1 download in the real window (scripts/main.gd), driven the way a player does it: the Play card's game
## picker, the Game files screen's Download, Pause / Resume. Run by tools/release/test_aio.py (test_aio_bfme1_window_offers_it_only_when_chosen)
## against its stand-in service:
##
##   godot --headless --path launcher --script res://tests/bfme1_offer_test.gd -- --offline --aio-base=<stand-in> --aio-pins=<made-up pins>
##       --bfme1-offer-target=<folder>
##
## Prints "BFME1 OFFER fail: ..." lines and "BFME1 OFFER OK" when every check passed; exit 0 only then.
extends SceneTree

const MainScene := preload("res://scenes/main.tscn")
const GamesScreen := preload("res://scripts/ui/games_screen.gd")
const AioInstall := preload("res://scripts/core/aio_install.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")
const Paths := preload("res://scripts/ui/paths.gd")

var _failures := PackedStringArray()
var _checks := 0


func _initialize() -> void:
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if not ok:
		_failures.append(what)
		print("BFME1 OFFER fail: " + what)


func _frames(n: int) -> void:
	for i in n:
		await process_frame


## waits for the running download job to end (at most 120 s); false when it did not
func _job_done(main) -> bool:
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < 120000:
		await process_frame
		if not main._busy() and main._job_kind == "":
			await _frames(2)
			return true
	return false


func _pick(main, index: int) -> void:
	main._play_screen.game_picker.select(index)
	main._play_screen.game_picker.item_selected.emit(index)
	await _frames(2)


func _offers_bfme1(main, want: bool, where: String) -> void:
	var gs = main._games_screen
	_check(gs.offer_bfme1 == want, "%s: the Game files screen should offer %s" % [where, "the BFME1 package" if want else "the base download"])
	_check(gs._dl_title.text == L10n.t(GamesScreen.BFME1_TITLE if want else GamesScreen.DL_TITLE).to_upper(), "%s: the download title is '%s'" % [where, gs._dl_title.text])
	_check(gs._source.text.contains("BFME Arena") == want and gs._source.text.contains("third-party") == want,
		"%s: the source line is '%s'" % [where, gs._source.text])
	_check(gs._what.text.begins_with("BFME1 test, about") == want, "%s: the download names '%s'" % [where, gs._what.text])


func _run() -> void:
	var target := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--bfme1-offer-target="):
			target = a.trim_prefix("--bfme1-offer-target=")
	if target == "" or not "--offline" in OS.get_cmdline_user_args():
		_check(false, "start it with -- --offline --aio-base=... --aio-pins=... --bfme1-offer-target=<folder>")
		_end()
		return
	var main = MainScene.instantiate()
	root.add_child(main)
	await _frames(3)
	if main._games_screen == null or main.aio == null or main.aio.bfme1_packages.size() != 1:
		_check(false, "the window did not start with the test pins (one BFME1 package)")
		_end()
		return
	main._show_screen("games")
	main._games_screen.set_folder(target)
	await _frames(2)

	# 1. BFME II / RotWK chosen (the default): the base download only
	_check(main._play_screen.game_picker.selected == 0, "the picker starts on BFME II / RotWK")
	_offers_bfme1(main, false, "default")

	# 2. Battle for Middle-earth I chosen: the optional package, labelled; the Play card points to it
	await _pick(main, 1)
	_check(main.updater.get_state("launcher", "game", "") == "bfme1", "the picker remembers BFME1")
	_offers_bfme1(main, true, "BFME1 chosen")
	_check(main._play_screen.missing_games.text.contains("optional, from BFME Arena"), "the Play card's BFME1 text: " + main._play_screen.missing_games.text)
	_check(main._play_screen.games_card.visible and not main._play_screen.games_button.disabled, "the Play card offers the download")

	# 3. Download, paused after the first file; the picker changes while it is paused: the panel keeps the paused package
	main.aio.test_stop_after = "BFME1/dsound.dll:pause"
	main._games_screen._go.pressed.emit()
	await _frames(1)
	_check(main.aio.choice == AioInstall.BFME1, "Download with BFME1 offered starts the BFME1 package")
	_check(await _job_done(main), "the paused BFME1 download ended")
	main.aio.test_stop_after = ""
	_check(main._paused == "aio", "the BFME1 download is paused (%s)" % main._paused)
	await _pick(main, 0)
	_offers_bfme1(main, true, "BFME1 paused, BFME II chosen")
	# Resume now: refused (BFME1 is not the chosen game any more), nothing downloaded
	main._on_pause()
	_check(await _job_done(main), "the refused resume ended")
	_check(main._status.text.contains("offered only when Battle for Middle-earth I is the chosen game"), "the refused resume says why: " + main._status.text)
	_check(not FileAccess.file_exists(target.path_join("BFME1/ini.big")), "nothing more was downloaded")
	main._message.visible = false
	_offers_bfme1(main, false, "after the refused resume")

	# 4. BFME1 again: Download completes it (the checked file is kept), its own handoff, Play passes the folder
	await _pick(main, 1)
	_offers_bfme1(main, true, "BFME1 chosen again")
	main._games_screen._go.pressed.emit()
	_check(await _job_done(main), "the BFME1 download ended")
	_check(main._status.text.contains("Battle for Middle-earth I is downloaded and checked"), "BFME1 status: " + main._status.text)
	var folder := target.path_join("BFME1")
	for f in ["dsound.dll", "ini.big", "lang/english.big", "lotrbfme.exe"]:
		_check(FileAccess.file_exists(folder.path_join(f)), "BFME1 file %s installed" % f)
	_check(not FileAccess.file_exists(folder.path_join("lang/french.big")), "only the English files")
	_check(Paths.bfme1_folder() == folder, "the launcher finds the downloaded BFME1 folder (%s)" % Paths.bfme1_folder())
	var launch: Dictionary = main.updater.game_arguments()
	_check(launch.ok and "--bfme1-install=" + folder in launch.args, "Play passes the BFME1 folder: %s" % str(launch))
	_check(not FileAccess.file_exists(target.path_join("RotWK/_patch201.big")), "the BFME1 download took no base game")

	# 5. back on BFME II / RotWK: the base offer and the base download, unchanged
	await _pick(main, 0)
	_offers_bfme1(main, false, "BFME II chosen again")
	main._games_screen._go.pressed.emit()
	await _frames(1)
	_check(main.aio.choice == AioInstall.GAMES, "Download with the base offer starts the base download")
	_check(await _job_done(main), "the base download ended")
	_check(main._status.text.contains("The games are downloaded and checked"), "base status: " + main._status.text)
	_check(FileAccess.file_exists(target.path_join("RotWK/_patch201.big")) and FileAccess.file_exists(target.path_join("BFME2/INI.big")), "the base games installed")
	main.queue_free()
	await _frames(2)
	_end()


func _end() -> void:
	if _checks == 0:
		_failures.append("no check ran")
	print("BFME1 OFFER OK (%d checks)" % _checks if _failures.is_empty() else "BFME1 OFFER FAILED: %d of %d checks" % [_failures.size(), _checks])
	quit(0 if _failures.is_empty() else 1)
