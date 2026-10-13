## OpenBFME. GPL-3.0.
## Lane SAVE-2: the saved games of a single player game (docs/SAVES.md, docs/PLAYING.md "Saving and loading"), driven by SaveLoad.apt's pages
## (AptSaveLoad): the quit menu's Save / Load (AptQuitMenu::SaveMenu / LoadMenu, RW 0x92179F / 0x9217F0) and the main menu's Load Game open the page with
## the Save folder's files (GameWorld.list_saves); its requests come back here: SaveGameFile writes the running game (GameWorld.save_game), DeleteGameFile
## removes a file and lists again, LoadGameFile checks the file (GameWorld.check_save: a refusal stays on the page, named), leaves the running game, loads
## the saved game's map behind the load screen and catches up to the saved frame with its log (GameWorld.save_load_step, a progress screen over the load
## screen that says so), then hands the game to the player.
##
## The Save folder is retail's (the user data folder's Save\, where Create-a-Hero keeps its heroes too); OpenBFME's files are .obfmesave, retail's own saves
## (.BfME2Skirmish / .BfME2Campaign) are listed as import entries. --save-dir=DIR uses another folder (tests).
##
## --save-test (with --auto, --screens=DIR for the pictures): in the skirmish Esc, the quit menu's Save, the page's Save (the default name), the result box;
## later Esc, Load, the save, Load and the confirmation; the catch-up; the game must come back at the saved frame with the saved state hash; then the
## game is left for the main menu: a copy of the save with another build's engine id is listed grey, refused by name and deleted (asked first); Load
## Game then loads the save again (no question there). Prints
## "GAME SAVE RESULT: ok" and exits 0, or FAILED / 1.
extends RefCounted

var game: Node
var last_save := {}
var last_load := {}
var catch_up_shot := ""
var _mode := 2
var _flags := 2
var _in_game := false
var _overlay: CanvasLayer = null
var _label: Label = null
var _bar: ProgressBar = null


func _init(g: Node) -> void:
	game = g


func save_dir() -> String:
	var dir: String = game._save_dir_arg
	if dir.is_empty():
		# retail: <application data>\<gi.dat UserDataLeafName>\Save\ (RW 0x6DE38D's folder; lane CAH-2's UserDataFolder)
		var r: Dictionary = game._shell.create_a_hero_save_folder(game._rotwk_install)
		if not r.get("ok", false):
			printerr("GAME SAVE no Save folder: ", r.get("error", ""))
			return ""
		dir = str(r.folder)
	DirAccess.make_dir_recursive_absolute(dir)
	return dir


func _info() -> Dictionary:
	var dir := save_dir()
	var saves: Array = [] if dir.is_empty() else game._world.list_saves(dir)
	return {"mode": _mode, "flags": _flags, "in_game": _in_game, "saves": saves}


## AptSaveLoad::OpenScreen(mode, types, over a game) (RW 0x816655): SaveLoad.apt with the Save folder's files
func open_menu(mode: int, flags: int, in_game: bool) -> void:
	_mode = mode
	_flags = flags
	_in_game = in_game
	var info := _info()
	game._shell.set_save_load(info)
	print("GAME SAVE menu: %s, types %d, %d files%s" % ["save" if mode == 3 else "load", flags, info.saves.size(), " (over the game)" if in_game else ""])
	if not game._shell.shell_push("SaveLoad.apt"):
		printerr("GAME SAVE the shell refused SaveLoad.apt: ", game._shell.get_shell_report().errors)


func request(action: String, argument: String) -> void:
	if action == "SaveGameFile":
		_save(argument)
	elif action == "DeleteGameFile":
		_delete(argument)
	elif action == "LoadGameFile":
		call_deferred("load_save", argument)


func _save(name: String) -> void:
	var dir := save_dir()
	if dir.is_empty():
		game._shell.save_load_saved(false, "there is no Save folder (the install's gi.dat names it)")
		return
	var kind := 1 if _flags == 1 else 2
	last_save = game._world.save_game(dir.path_join(name + ".obfmesave"), {"description": name, "kind": kind})
	print("GAME SAVE saved '%s': ok=%s frame %s hash %s, %s bytes, %s commands %s" % [name, str(last_save.get("ok", false)), str(last_save.get("frame", "")),
		str(last_save.get("hash", "")), str(last_save.get("bytes", "")), str(last_save.get("commands", "")), str(last_save.get("error", ""))])
	game._shell.save_load_saved(last_save.get("ok", false), str(last_save.get("error", "")))


func _delete(path: String) -> void:
	var err := DirAccess.remove_absolute(path)
	print("GAME SAVE deleted '%s': %s" % [path.get_file(), error_string(err)])
	game._shell.set_save_load(_info()) # the page lists again
	if err != OK:
		game._shell.save_load_refused(path.get_file().get_basename(), "the file could not be deleted: " + error_string(err))


## LoadGameFile: checked first (a refusal stays on the page), then the running game goes, the saved game loads and catches up
func load_save(path: String) -> void:
	var name := path.get_file().get_basename()
	var check: Dictionary = game._world.check_save(path)
	if not check.get("ok", false):
		print("GAME SAVE refused '%s': %s" % [name, str(check.get("error", ""))])
		game._shell.save_load_refused(name, str(check.get("error", "")))
		return
	if not str(check.get("note", "")).is_empty():
		print("GAME SAVE note: ", check.note) # a save of the other execution profile (single player): it replays under this one's rules
	print("GAME SAVE loading '%s'" % name)
	if game._state == game.State.PLAYING:
		game._close_quit_menu()
		if game._campaign_flow != null:
			game._campaign_flow.active = false # a running mission is left (its end must not run the campaign on)
		game._tear_down_game("load")
	else:
		game._start_stack = PackedStringArray(["MainMenu.apt"]) # the score screen's Continue goes back to the main menu
	var prep: Dictionary = game._world.prepare_save_load(path)
	if not prep.get("ok", false):
		_back_to_menu(name, "\n".join(prep.get("errors", [])))
		return
	while game._shell.shell_stack().size() > 0:
		game._shell.shell_pop()
		game._shell.tick(0.033)
	var flow = game._campaign_flow
	var campaign := str(prep.get("campaign", ""))
	var opts := {"progress": Callable(game, "_on_load_progress")}
	if not campaign.is_empty():
		# a campaign mission: its own loading screen (the mission's picture) and, after the catch-up, the campaign flow takes over (its next mission)
		var missions: Array = flow.missions_of(campaign)
		if int(prep.mission) >= missions.size():
			_back_to_menu(name, "the campaign %s has no mission %d" % [campaign, int(prep.mission)])
			return
		flow.campaign = campaign
		flow.mission = int(prep.mission)
		flow.difficulty = int(prep.difficulty)
		game._last_new_game = {}
		if not await flow.show_load_screen(missions[flow.mission]):
			return
	else:
		game._last_local_slot = int(prep.get("local_slot", -1))
		game._last_new_game = prep.message # the quit menu's Restart starts the saved game again from its start (RW 0x9220DE)
		game._state = game.State.LOADING
		game._game_kind = "save"
		game._shell_leave_for_game()
		var cards: Dictionary = game._shell.set_load_screen_from_game(prep.resolved)
		if cards.has("error"):
			_back_to_menu(name, "load screen: " + str(cards.error))
			return
		var choice: Dictionary = game._shell.choose_load_screen({"game_mode": 2, "screen_kind": 3})
		if choice.get("kind", "") != "players":
			_back_to_menu(name, "load screen: " + str(choice))
			return
		game._shell_load_screen_push()
		for i in 4:
			await game.get_tree().process_frame
		game._shell.auto_process = false
		if game._record:
			opts["record"] = game._last_replay_path() # the recording continues the saved log (the whole game stays one replay)
	var t0 := Time.get_ticks_msec()
	var rep: Dictionary = game._world.start_save_load(opts)
	game._report = rep
	if not rep.get("ok", false):
		_back_to_menu(name, "\n".join(rep.get("errors", [])))
		return
	print("GAME SAVE map loaded in %d ms; catching up from frame %d to %d%s" % [Time.get_ticks_msec() - t0, int(rep.start_frame), int(rep.target_frame),
		"" if campaign.is_empty() else " (%s mission %d)" % [campaign, int(prep.mission)]])
	# the catch-up: the log played at full speed, no game view (the load screen stays), a progress screen that says so
	_show_overlay()
	var st := {}
	var first := true
	while true:
		st = game._world.save_load_step(40)
		_update_overlay(st)
		if not catch_up_shot.is_empty() and first:
			for i in 3:
				await game.get_tree().process_frame
			game._save_named(catch_up_shot)
		first = false
		if st.get("state", "") != "catching_up":
			break
		await game.get_tree().process_frame
	_hide_overlay()
	last_load = st
	print("GAME SAVE catch-up: ", JSON.stringify(st))
	if st.get("state", "") != "ready":
		game._tear_down_game("load failed")
		_back_to_menu(name, str(st.get("error", "the catch-up failed")))
		return
	if not campaign.is_empty():
		flow.enter_mission(rep)
	else:
		game._enter_game(rep)


## a load that failed after the running game was left: back to the main menu, the load page again with the error (retail: GUI:ErrorLoadingGame, then the
## shell, RW 0x6DF55E)
func _back_to_menu(name: String, error: String) -> void:
	printerr("GAME SAVE load of '%s' failed: %s" % [name, error])
	if not game._save_test.is_empty():
		game._fail("the saved game could not be loaded: " + error)
		return
	game._start_stack = PackedStringArray(["MainMenu.apt"])
	game._state = game.State.SCORE
	game._score_continue()
	open_menu(2, 0xB, false)
	game._shell.save_load_refused(name, error)


func _show_overlay() -> void:
	_overlay = CanvasLayer.new()
	_overlay.layer = 96
	game.add_child(_overlay)
	var panel := PanelContainer.new()
	panel.anchor_left = 0.3
	panel.anchor_right = 0.7
	panel.anchor_top = 0.42
	panel.anchor_bottom = 0.42
	_overlay.add_child(panel)
	var box := VBoxContainer.new()
	panel.add_child(box)
	var title := Label.new()
	title.text = "Catching up with the saved game"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(title)
	_bar = ProgressBar.new()
	_bar.min_value = 0
	_bar.max_value = 100
	box.add_child(_bar)
	_label = Label.new()
	_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(_label)


func _update_overlay(st: Dictionary) -> void:
	if _bar == null:
		return
	_bar.value = int(st.get("percent", 0))
	var frame := int(st.get("frame", 0))
	var target := int(st.get("target", 0))
	_label.text = "Replaying the saved commands: %d:%02d of %d:%02d game time" % [frame / 300, (frame / 5) % 60, target / 300, (target / 5) % 60]


func _hide_overlay() -> void:
	if _overlay != null:
		_overlay.queue_free()
	_overlay = null
	_bar = null
	_label = null


# ---- --save-test ----------------------------------------------------------------------------------------------------------------------------------------

func _top_buttons() -> Array:
	var level: int = game._shell.shell_top_level()
	return game._shell.list_buttons(level) if level >= 0 else []


## --save-test: `path` copied under "Other Build.obfmesave" with another engine id in its header (same length, the HEAD part's SHA-256 recomputed): what
## another build's save looks like to this one
func _other_build_copy(path: String) -> String:
	var bytes := FileAccess.get_file_as_bytes(path)
	var head_len := bytes.decode_u32(16)
	var payload := bytes.slice(52, 52 + head_len)
	# the header's second string is the engine id: u32 version, then str build version (u32 length + bytes), then str engine id
	var v_len := payload.decode_u32(4)
	var at := 8 + v_len
	var e_len := payload.decode_u32(at)
	for i in e_len:
		payload[at + 4 + i] = 0x31 # "1111...": no build has that id
	var ctx := HashingContext.new()
	ctx.start(HashingContext.HASH_SHA256)
	ctx.update(payload)
	var digest := ctx.finish()
	var out := bytes.slice(0, 20) + digest + payload + bytes.slice(52 + head_len)
	var target := path.get_base_dir().path_join("Other Build.obfmesave")
	var f := FileAccess.open(target, FileAccess.WRITE)
	if f == null:
		return ""
	f.store_buffer(out)
	f.close()
	print("GAME SAVE test: wrote another build's copy (engine id %d bytes replaced)" % e_len)
	return target


## a real click on the first hittable button of the top screen whose path has `fragment` as a name part
func _click_top(fragment: String) -> bool:
	for attempt in 60:
		for b in _top_buttons():
			var path: String = b.path
			if b.hittable and (path.contains("." + fragment + ".") or path.ends_with("." + fragment) or path.contains(fragment + "Button")):
				var p := Vector2(b.x, b.y)
				game._mouse_event(p, false, false)
				await game._step(2)
				game._mouse_event(p, true, true)
				await game._step(2)
				game._mouse_event(p, false, true)
				await game._step(2)
				print("GAME SAVE clicked ", path)
				return true
		await game._wait_seconds(0.1)
	print("GAME SAVE no button '%s'; the top screen's buttons: %s" % [fragment, JSON.stringify(_top_buttons())])
	return false


## a real click on a hittable button of any loaded movie level whose path has both fragments (the message box lives in GuiFX.apt's own level)
func _click_any_level(a: String, b: String) -> bool:
	for attempt in 40:
		for level in 32:
			for btn in game._shell.list_buttons(level):
				var path: String = btn.path
				if btn.hittable and path.contains(a) and path.containsn(b):
					var p := Vector2(btn.x, btn.y)
					game._mouse_event(p, false, false)
					await game._step(2)
					game._mouse_event(p, true, true)
					await game._step(2)
					game._mouse_event(p, false, true)
					await game._step(2)
					print("GAME SAVE clicked ", path, " (level ", level, ")")
					return true
		await game._wait_seconds(0.1)
	var seen := []
	for level in 32:
		for btn in game._shell.list_buttons(level):
			if String(btn.path).contains(a):
				seen.append("%s hittable=%s" % [btn.path, str(btn.hittable)])
	print("GAME SAVE no button with '%s' and '%s' in any level; the '%s' buttons: %s" % [a, b, a, str(seen)])
	return false


func _wait_top(screen: String, seconds: float) -> bool:
	var waited := 0.0
	while waited < seconds:
		var stack: PackedStringArray = game._shell.shell_stack()
		if stack.size() > 0 and stack[-1] == screen and game._shell.has_level(game._shell.shell_top_level()):
			return true
		await game._wait_seconds(0.1)
		waited += 0.1
	return false


func run_test() -> bool:
	var ok := true
	while game._world.get_frame() < 150: # 30 s of game time: the AI builds, the armies move
		await game._wait_seconds(0.25)
	# the quit menu's Save: SaveLoad.apt's save page over the paused game
	await game._press_escape()
	await game._wait_seconds(2.0)
	ok = ok and await game._click_quit("Save")
	ok = ok and await _wait_top("SaveLoad.apt", 10.0)
	await game._wait_seconds(2.0)
	game._save_named("save2-save-page")
	ok = ok and await _click_top("Save")
	await game._wait_seconds(2.0)
	game._save_named("save2-game-saved")
	ok = ok and last_save.get("ok", false)
	print("GAME SAVE test: saved %s" % JSON.stringify(last_save))
	var saved_frame := int(last_save.get("frame", -1))
	var saved_hash := int(last_save.get("hash", -1))
	# the result box's Ok closes the page (retail state 9), Resume the quit menu; the game goes on
	ok = ok and await _click_any_level("MessageBox", "Ok")
	await game._wait_seconds(1.5)
	var stack: PackedStringArray = game._shell.shell_stack()
	print("GAME SAVE test: after the box's Ok the shell holds ", str(stack))
	ok = ok and (stack.size() == 0 or stack[-1] != "SaveLoad.apt")
	ok = ok and await game._click_quit("Resume")
	await game._wait_seconds(4.0)
	print("GAME SAVE test: the game went on to frame %d" % game._world.get_frame())
	ok = ok and game._world.get_frame() > saved_frame
	# the quit menu's Load: the page lists the save; Load, the movie's confirmation, the catch-up, the game at the saved frame
	await game._press_escape()
	await game._wait_seconds(2.0)
	ok = ok and await game._click_quit("Load")
	ok = ok and await _wait_top("SaveLoad.apt", 10.0)
	await game._wait_seconds(2.0)
	game._save_named("save2-load-page")
	catch_up_shot = "save2-catching-up"
	ok = ok and await _click_top("Load") # the newest save is selected (the page selects its first row)
	await game._wait_seconds(1.5)
	game._save_named("save2-load-confirm")
	ok = ok and await _click_top("ConfirmationOk") # the movie's Yes (MessageBox.ConfirmationOk)
	var waited := 0.0
	while (game._state != game.State.PLAYING or last_load.is_empty()) and waited < 120.0:
		await game._wait_seconds(0.1)
		waited += 0.1
	var frame_now: int = game._world.get_frame()
	var hash_now: int = game._world.get_state_hash()
	print("GAME SAVE test: loaded, catch-up %s; frame %d (saved %d), state hash %d (saved %d)" % [JSON.stringify(last_load), frame_now, saved_frame, hash_now, saved_hash])
	ok = ok and last_load.get("state", "") == "ready" and int(last_load.get("frame", -1)) == saved_frame
	ok = ok and int(last_load.get("hashes_compared", 0)) > 0
	await game._wait_seconds(3.0)
	game._save_named("save2-loaded-game")
	ok = ok and game._world.get_frame() > saved_frame # the loaded game runs on
	# the main menu's Load Game: leave the game (Esc, Exit, its confirmation, the score screen's Continue), Load Game, the save loads without a question
	await game._press_escape()
	await game._wait_seconds(2.0)
	ok = ok and await game._click_quit("ExitMission")
	await game._wait_seconds(2.0)
	ok = ok and await game._click_quit("Yes")
	var w := 0.0
	while game._state != game.State.SCORE and w < 20.0:
		await game._wait_seconds(0.25)
		w += 0.25
	game._score_continue()
	await game._wait_seconds(3.0)
	# a copy of the save as another build would have made it (its engine id changed, the header re-signed): listed grey, refused by name, deleted
	var other := _other_build_copy(str(last_save.get("path", "")))
	ok = ok and not other.is_empty()
	game._on_shell_request("LoadGame", "")
	ok = ok and await _wait_top("SaveLoad.apt", 10.0)
	await game._wait_seconds(2.0)
	game._save_named("save2-mainmenu-load-page")
	ok = ok and await _click_top("Skirmish") # the page opens on the first type of 0xB, Campaign (RW 0x816692)
	await game._wait_seconds(1.5)
	game._save_named("save2-mainmenu-load-skirmish")
	ok = ok and await _click_top("Load") # the newest file, the other build's copy: refused, named
	await game._wait_seconds(1.5)
	game._save_named("save2-refused-other-build")
	ok = ok and await _click_any_level("MessageBox", "Ok")
	await game._wait_seconds(1.0)
	ok = ok and FileAccess.file_exists(other)
	ok = ok and await _click_top("Delete")
	await game._wait_seconds(1.5)
	game._save_named("save2-delete-confirm")
	ok = ok and await _click_top("ConfirmationOk") # the movie's Yes (MessageBox.ConfirmationOk)
	await game._wait_seconds(1.5)
	print("GAME SAVE test: the other build's copy deleted: %s" % str(not FileAccess.file_exists(other)))
	ok = ok and not FileAccess.file_exists(other)
	game._save_named("save2-after-delete")
	last_load = {}
	catch_up_shot = ""
	ok = ok and await _click_top("Load")
	w = 0.0
	while (game._state != game.State.PLAYING or last_load.is_empty()) and w < 120.0:
		await game._wait_seconds(0.1)
		w += 0.1
	print("GAME SAVE test: loaded from the main menu, catch-up %s" % JSON.stringify(last_load))
	if last_load.is_empty():
		var lines: PackedStringArray = game._shell.get_shell_report().get("note_lines", PackedStringArray())
		print("GAME SAVE test: the shell's last notes: ", str(lines.slice(max(0, lines.size() - 12))))
		print("GAME SAVE test: the movies' last traces: ", str(game._shell.get_shell_report().get("traces", [])))
	ok = ok and last_load.get("state", "") == "ready" and int(last_load.get("frame", -1)) == saved_frame
	return ok


## --campaign=NAME --save-test: the mission saved on the Campaign page through the quit menu, played on, then loaded back over it (the campaign flow
## takes the loaded mission over: its end runs the campaign on)
func run_campaign_test() -> bool:
	var ok := true
	var waited := 0.0
	while (game._state != game.State.PLAYING or game._world.get_frame() < 100) and waited < 300.0:
		await game._wait_seconds(0.25)
		waited += 0.25
	await game._press_escape()
	await game._wait_seconds(2.0)
	ok = ok and await game._click_quit("Save")
	ok = ok and await _wait_top("SaveLoad.apt", 10.0)
	await game._wait_seconds(2.0)
	game._save_named("save2-campaign-save-page")
	ok = ok and await _click_top("Save")
	await game._wait_seconds(2.0)
	print("GAME SAVE campaign test: saved %s" % JSON.stringify(last_save))
	ok = ok and last_save.get("ok", false)
	var saved_frame := int(last_save.get("frame", -1))
	ok = ok and await _click_any_level("MessageBox", "Ok")
	await game._wait_seconds(1.5)
	ok = ok and await game._click_quit("Resume")
	await game._wait_seconds(4.0)
	ok = ok and game._world.get_frame() > saved_frame
	await game._press_escape()
	await game._wait_seconds(2.0)
	ok = ok and await game._click_quit("Load")
	ok = ok and await _wait_top("SaveLoad.apt", 10.0)
	await game._wait_seconds(2.0)
	game._save_named("save2-campaign-load-page")
	last_load = {}
	ok = ok and await _click_top("Load")
	await game._wait_seconds(1.5)
	ok = ok and await _click_top("ConfirmationOk")
	waited = 0.0
	while (game._state != game.State.PLAYING or last_load.is_empty()) and waited < 120.0:
		await game._wait_seconds(0.1)
		waited += 0.1
	print("GAME SAVE campaign test: loaded, catch-up %s; campaign %s mission %d active %s" % [JSON.stringify(last_load), game._campaign_flow.campaign,
		game._campaign_flow.mission, str(game._campaign_flow.active)])
	ok = ok and last_load.get("state", "") == "ready" and int(last_load.get("frame", -1)) == saved_frame
	ok = ok and game._campaign_flow.active and game._game_kind.begins_with("campaign")
	await game._wait_seconds(3.0)
	game._save_named("save2-campaign-loaded")
	ok = ok and game._world.get_frame() > saved_frame
	return ok

