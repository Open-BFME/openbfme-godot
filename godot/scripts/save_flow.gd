## OpenBFME. GPL-3.0.
## Lane SAVE-2: the saved games of a single player game (docs/SAVES.md, docs/PLAYING.md "Saving and loading"), driven by SaveLoad.apt's pages
## (AptSaveLoad): the quit menu's Save / Load (AptQuitMenu::SaveMenu / LoadMenu, RW 0x92179F / 0x9217F0) and the main menu's Load Game open the page with
## the Save folder's files (GameWorld.list_saves); its requests come back here: SaveGameFile writes the running game (GameWorld.save_game), DeleteGameFile
## removes a file and lists again, LoadGameFile checks the file (GameWorld.check_save: a refusal stays on the page, named), leaves the running game, loads
## the saved game's map behind the load screen and restores the saved state (lane SAVE-4e: from the save's snapshot, at once; an earlier alpha's
## log-only save catches up to the saved frame with its log, GameWorld.save_load_step, its progress drawn on the load screen), then hands the game to the
## player. Lane SAVE-4e: the load screen is retail's for a saved game (GameLogic::getLoadScreen RW 0x626A87 with a save loading): the picture screen
## with a film still, the spinning ring and "Loading", for a campaign mission and a skirmish alike (a skirmish's: INFERENCE S-5012, GameLogic + 0x114
## taken as 0 when no lobby ran).
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
var _overlay_on := false
var restored_after_refusal := {} # lane SAVE-4e review r1: the last refused load's name, error and the restored game's frame and state hash


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
	var flow = game._campaign_flow
	if flow != null and flow.menu_open and not _in_game:
		# lane CAMPSAVE-1: the Save page over CampaignMenu.apt (RW 0x927A93): the campaign's state between missions (the campaign menu save point)
		var opts: Dictionary = flow.progress()
		opts["description"] = name
		last_save = game._world.save_campaign_menu(dir.path_join(name + ".obfmesave"), opts)
		print("GAME SAVE saved the campaign menu '%s': %s" % [name, JSON.stringify(last_save)])
		game._shell.save_load_saved(last_save.get("ok", false), str(last_save.get("error", "")))
		return
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


## LoadGameFile: checked first (a refusal stays on the page), then the running game goes, the saved game loads and catches up. Lane SAVE-4e review r1
## (Sol): the running game's own save is kept in memory before it goes (GameWorld.keep_running_game); if the chosen save is refused later (a block of its
## snapshot), the kept game is loaded back (prepare_kept_load) and the refusal is shown over it on the load page: the player keeps the game they had


func load_save(path: String) -> String:
	var name := path.get_file().get_basename()
	var check: Dictionary = game._world.check_save(path)
	if not check.get("ok", false):
		print("GAME SAVE refused '%s': %s" % [name, str(check.get("error", ""))])
		game._shell.save_load_refused(name, str(check.get("error", "")))
		return str(check.get("error", "the save was refused"))
	if not str(check.get("note", "")).is_empty():
		print("GAME SAVE note: ", check.note) # a save of the other execution profile (single player): it replays under this one's rules
	print("GAME SAVE loading '%s'" % name)
	if int(check.get("save_point", 0)) == 1:
		# lane CAMPSAVE-1: a campaign menu save (the auto-save, a save made in the menu): no game to load, the menu with the saved state
		if game._state == game.State.PLAYING:
			game._close_quit_menu()
			if game._campaign_flow != null:
				game._campaign_flow.active = false
			game._tear_down_game("load")
		last_load = {"state": "campaign_menu", "campaign": check.campaign, "mission": check.mission}
		await game._campaign_flow.open_from_save(check)
		return ""
	if game._campaign_flow != null:
		game._campaign_flow.menu_open = false # lane CAMPSAVE-1: a game loaded from the campaign menu's Load page leaves the menu
	var kept := false
	if game._state == game.State.PLAYING:
		var keep: Dictionary = game._world.keep_running_game()
		kept = keep.get("ok", false)
		if not kept:
			# lane SAVE-4e review r2 (Sol): a running game that cannot be kept is never left for a save that may be refused: the load stops here,
			# the game stays as it was and the refusal is shown on the load page
			var why := "the game you are playing could not be kept, so the save was not loaded: " + str(keep.get("error", ""))
			print("GAME SAVE load of '%s' stopped: %s" % [name, why])
			game._shell.save_load_refused(name, why)
			return why
		print("GAME SAVE the running game kept: frame %d" % int(keep.frame))
		game._close_quit_menu()
		if game._campaign_flow != null:
			game._campaign_flow.active = false # a running mission is left (its end must not run the campaign on)
		game._tear_down_game("load")
	else:
		game._start_stack = PackedStringArray(["MainMenu.apt"]) # the score screen's Continue goes back to the main menu
	var error: String = await _load_prepared(game._world.prepare_save_load(path), name)
	if error.is_empty():
		game._world.drop_kept_game()
		return ""
	if kept:
		var back: String = await _load_prepared(game._world.prepare_kept_load(), name)
		if back.is_empty():
			restored_after_refusal = {"name": name, "error": error, "frame": game._world.get_frame(), "hash": game._world.get_state_hash()}
			print("GAME SAVE load of '%s' refused, the running game is back: %s" % [name, error])
			# the player is where they were: the quit menu's load page over their game, now with the refusal
			await game._press_escape()
			await game._wait_seconds(0.5)
			open_menu(2, _flags, true)
			game._shell.save_load_refused(name, error)
			return error
		error += "; the game you were playing could not be loaded back: " + back
	_back_to_menu(name, error)
	return error


## the prepared load (prepare_save_load / prepare_kept_load) started and caught up: "" when the game is the player's, else the error (the started game
## torn down)
func _load_prepared(prep: Dictionary, name: String) -> String:
	if not prep.get("ok", false):
		return "\n".join(prep.get("errors", []))
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
			return "the campaign %s has no mission %d" % [campaign, int(prep.mission)]
		flow.campaign = campaign
		flow.mission = int(prep.mission)
		flow.difficulty = int(prep.difficulty)
		flow.victorious = false # lane CAMPSAVE-1: a mission being played (RW 0x5EAB8F's flags)
		flow.campaign_ready = false
		flow.advanced = false
		flow.score_pending = false
		game._last_new_game = {}
		if not await flow.show_load_screen(missions[flow.mission], true):
			return "the mission's load screen could not be shown"
	else:
		game._last_local_slot = int(prep.get("local_slot", -1))
		game._last_new_game = prep.message # the quit menu's Restart starts the saved game again from its start (RW 0x9220DE)
		game._state = game.State.LOADING
		game._game_kind = "save"
		game._shell_leave_for_game()
		# lane SAVE-4e: retail's screen for a saved game (RW 0x626A87, a save loading, GameLogic + 0x114 == 0: S-5012): the picture screen, a film still
		var choice: Dictionary = game._shell.choose_load_screen({"game_mode": 2, "screen_kind": 0, "loading_save": true})
		if choice.get("kind", "") != "still" or not String(choice.get("error", "")).is_empty():
			return "load screen: " + str(choice)
		var shown: Dictionary = game._shell.show_still_load_screen(String(choice.picture))
		print("GAME SAVE load screen: %s %s %s" % [choice.kind, choice.picture, "" if shown.ok else str(shown.error)])
		if not shown.ok:
			return "load screen: " + str(shown.error)
		for i in 4:
			await game.get_tree().process_frame
		game._shell.auto_process = false
		if game._record:
			opts["record"] = game._last_replay_path() # the recording continues the saved log (the whole game stays one replay)
	var t0 := Time.get_ticks_msec()
	var rep: Dictionary = game._world.start_save_load(opts)
	game._report = rep
	if not rep.get("ok", false):
		game._shell.hide_still_load_screen()
		game._tear_down_game("load failed") # lane SAVE-4e: a refused snapshot: the started game goes (never handed over)
		return "\n".join(rep.get("errors", []))
	var mode := str(rep.get("mode", "catch_up"))
	print("GAME SAVE map loaded in %d ms; %s from frame %d to %d%s" % [Time.get_ticks_msec() - t0, "the snapshot loaded" if mode == "snapshot" else "catching up",
		int(rep.start_frame), int(rep.target_frame), "" if campaign.is_empty() else " (%s mission %d)" % [campaign, int(prep.mission)]])
	# a snapshot is loaded already (ready at the first step); an old save's catch-up: the log played at full speed, no game view (the load screen stays),
	# its progress drawn on the load screen in its font (set_still_load_progress)
	if mode != "snapshot":
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
	if st.get("state", "") == "ready":
		st["hash"] = game._world.get_state_hash()
	last_load = st
	print("GAME SAVE catch-up: ", JSON.stringify(st))
	if st.get("state", "") != "ready":
		game._shell.hide_still_load_screen()
		game._tear_down_game("load failed")
		return str(st.get("error", "the catch-up failed"))
	if not campaign.is_empty():
		flow.enter_mission(rep)
	else:
		game._enter_game(rep)
	return ""


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


## lane SAVE-4e: the catch-up's progress on the picture load screen (the Godot panel of SAVE-2 is gone)
func _show_overlay() -> void:
	_overlay_on = true
	game._shell.set_still_load_progress(0, "Catching up with the saved game")


func _update_overlay(st: Dictionary) -> void:
	if not _overlay_on:
		return
	var frame := int(st.get("frame", 0))
	var target := int(st.get("target", 0))
	game._shell.set_still_load_progress(int(st.get("percent", 0)), "Catching up with the saved game: %d:%02d of %d:%02d" % [frame / 300, (frame / 5) % 60,
		target / 300, (target / 5) % 60])
	game._shell.tick(0.033)
	game._shell.render(true)


func _hide_overlay() -> void:
	if _overlay_on:
		game._shell.set_still_load_progress(-1, "")
	_overlay_on = false


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


## lane SAVE-4e review r1: `path` copied under "Hostile Late Block.obfmesave" with its snapshot's last block (obfme:LargeGroupAudio) at version 255, the
## SNAP part's SHA-256 recomputed: the container and the framing are intact, the load refuses the block only after the earlier blocks loaded
func _hostile_copy(path: String) -> String:
	var bytes := FileAccess.get_file_as_bytes(path)
	var off := 12 # magic (8) and the container version (4); each part: tag (4), length (4), SHA-256 (32), payload
	for part in 3:
		var length := bytes.decode_u32(off + 4)
		if part == 2:
			var payload := bytes.slice(off + 40, off + 40 + length)
			var token := "obfme:LargeGroupAudio".to_ascii_buffer()
			var at := -1
			for i in payload.size() - token.size():
				if payload.slice(i, i + token.size()) == token:
					at = i
			if at < 0:
				return ""
			payload[at + token.size() + 8] = 0xFF # after the token: "BLOK", the end offset, then the block's version byte
			var ctx := HashingContext.new()
			ctx.start(HashingContext.HASH_SHA256)
			ctx.update(payload)
			var out := bytes.slice(0, off + 8) + ctx.finish() + payload + bytes.slice(off + 40 + length)
			var target := path.get_base_dir().path_join("Hostile Late Block.obfmesave")
			var f := FileAccess.open(target, FileAccess.WRITE)
			if f == null:
				return ""
			f.store_buffer(out)
			f.close()
			print("GAME SAVE test: wrote a hostile copy (its last block's version 255)")
			return target
		off += 40 + length
	return ""


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
	# the result box's Ok closes the page and the quit menu (retail state 10, RW 0x8184C0; lane SAVE-UI1): the game goes on
	ok = ok and await _click_any_level("MessageBox", "Ok")
	await game._wait_seconds(1.5)
	var stack: PackedStringArray = game._shell.shell_stack()
	print("GAME SAVE test: after the box's Ok the shell holds ", str(stack), ", quit menu open ", str(game._quit_open))
	ok = ok and (stack.size() == 0 or stack[-1] != "SaveLoad.apt") and not game._quit_open
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
	catch_up_shot = "save4e-loading-snapshot-save"
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
	ok = ok and last_load.get("mode", "") == "snapshot" and int(last_load.get("hashes_compared", -1)) == 0 # lane SAVE-4e: no frame of the log ran
	ok = ok and int(last_save.get("snapshot_bytes", 0)) > 0
	var menu_load := last_load.duplicate()
	last_load = {}
	var recording_error: String = game._set_recording_args(["--load", str(last_save.path)])
	ok = ok and recording_error.is_empty()
	await game._open_recording()
	game._set_recording_args([])
	print("GAME SAVE test: --load/menu parity: frame %d/%d, hash %d/%d" % [int(last_load.get("frame", -1)), int(menu_load.get("frame", -2)),
		int(last_load.get("hash", -1)), int(menu_load.get("hash", -2))])
	ok = ok and last_load.get("state", "") == "ready" and int(last_load.get("frame", -1)) == saved_frame and int(last_load.get("hash", -1)) == saved_hash
	ok = ok and last_load.get("frame") == menu_load.get("frame") and last_load.get("hash") == menu_load.get("hash")
	await game._wait_seconds(3.0)
	game._save_named("save2-loaded-game")
	ok = ok and game._world.get_frame() > saved_frame # the loaded game runs on
	# lane SAVE-4e: an earlier alpha's save (the log only), made now: the main menu's Load below loads it (the newest file) through the catch-up
	var old_alpha: Dictionary = game._world.save_game(save_dir().path_join("Old Alpha.obfmesave"), {"description": "Old Alpha", "kind": 2, "snapshot": false})
	print("GAME SAVE test: an old alpha's save %s" % JSON.stringify(old_alpha))
	ok = ok and old_alpha.get("ok", false) and int(old_alpha.get("snapshot_bytes", -1)) == 0
	var old_frame := int(old_alpha.get("frame", -1))
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
	catch_up_shot = "save4e-catching-up-old-save"
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
	ok = ok and last_load.get("state", "") == "ready" and int(last_load.get("frame", -1)) == old_frame
	ok = ok and last_load.get("mode", "") == "catch_up" and int(last_load.get("hashes_compared", 0)) > 0 # the old save caught up from its log
	DirAccess.remove_absolute(save_dir().path_join("Old Alpha.obfmesave"))
	# lane SAVE-4e review r1 (Sol): a save whose snapshot is refused late, loaded over the running game: the load is refused by name and the player keeps
	# the game they had (its frame and state hash), back on the quit menu's load page with the refusal
	await game._wait_seconds(2.0)
	var hostile := _hostile_copy(str(last_save.get("path", "")))
	ok = ok and not hostile.is_empty()
	await game._press_escape()
	await game._wait_seconds(2.0)
	var before_frame: int = game._world.get_frame()
	var before_hash: int = game._world.get_state_hash()
	ok = ok and await game._click_quit("Load")
	ok = ok and await _wait_top("SaveLoad.apt", 10.0)
	await game._wait_seconds(2.0)
	restored_after_refusal = {}
	ok = ok and await _click_top("Load") # the newest file: the hostile copy
	await game._wait_seconds(1.5)
	ok = ok and await _click_top("ConfirmationOk")
	w = 0.0
	while restored_after_refusal.is_empty() and w < 120.0:
		await game._wait_seconds(0.1)
		w += 0.1
	await game._wait_seconds(2.0)
	game._save_named("save4e-refused-load-game-kept")
	print("GAME SAVE test: the hostile load %s; the game before it: frame %d hash %d" % [JSON.stringify(restored_after_refusal), before_frame, before_hash])
	ok = ok and str(restored_after_refusal.get("error", "")).contains("obfme:LargeGroupAudio")
	ok = ok and int(restored_after_refusal.get("frame", -1)) == before_frame and int(restored_after_refusal.get("hash", -1)) == before_hash
	DirAccess.remove_absolute(hostile)
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
	ok = ok and not game._quit_open # lane SAVE-UI1: the box's Ok goes back to the game (retail state 10)
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




# ---- lane SAVE-UI1: --save-ui-test ----------------------------------------------------------------------------------------------------------------------
## The owner's report (2026-10-15): in a game, the quit menu's Save left an empty screen (the front-end background over the game, the HUD and the health
## bars on it) and no way back. Drives the real flow with real clicks and keys, in a skirmish (--auto) or a campaign mission started from the main menu
## (--campaign-menu=Expansion1Campaign): each named check prints "SAVEUI check <name>: ok|FAIL <detail>", then "SAVEUI RESULT <n> checks, <m> failed"
## (tests/saveui1_test.gd compares the names with its own list).

const QUIT_BUTTONS := ["Resume", "Options", "Save", "Load", "Restart", "ExitMission"]
const TYPED_NAME := "Owner Save 7"

var ui_checks: Array = []
var ui_failed := 0


func _ui_check(name: String, ok: bool, detail: String) -> void:
	ui_checks.append(name)
	if not ok:
		ui_failed += 1
	print("SAVEUI check %s: %s %s" % [name, "ok" if ok else "FAIL", detail])
	game._save_named("saveui1-" + name)


## the game's overlays drawn over the shell (game.gd _set_game_overlays_covered): the in-game message layer, the script presentation's layer
func _overlays_shown() -> Array:
	var out: Array = []
	if game._end_message_layer != null and game._end_message_layer.visible:
		out.append("message")
	var icons: Dictionary = game._hud.get_icon_ui() if game._hud != null else {}
	if int(icons.get("drawn", 0)) != 0:
		out.append("%d drawable decorations (health bars %d)" % [int(icons.drawn), int(icons.get("health_bars", 0))])
	for n in game._game_nodes:
		if is_instance_valid(n) and n.has_method("set_covered") and n._ui != null and n._ui.visible:
			out.append(str(n.name))
	return out


func _background_mode() -> int:
	return int(game._shell.get_backdrop_state().get("background_mode", -1))


## the quit menu's buttons a click reaches (QuitMenu.apt's clips by name)
func _quit_hittable() -> Array:
	var r: Dictionary = game._shell.quit_menu({"kind": "state"})
	var out: Array = []
	if not r.get("open", false):
		return out
	for b in game._shell.list_buttons(r.level):
		var path: String = b.path
		for n in QUIT_BUTTONS:
			if b.hittable and path.contains("." + n + ".") and not out.has(n):
				out.append(n)
	out.sort()
	return out


## waits (up to `seconds`) until every quit menu button is hittable again: the movie's intro plays them in
func _quit_all_buttons(seconds: float) -> Array:
	var want: Array = QUIT_BUTTONS.duplicate()
	want.sort()
	var got: Array = []
	var waited := 0.0
	while waited < seconds:
		got = _quit_hittable()
		if got == want:
			break
		await game._wait_seconds(0.1)
		waited += 0.1
	return got


func _top_hittable(names: Array) -> Array:
	var out: Array = []
	for b in _top_buttons():
		var path: String = b.path
		for n in names:
			if b.hittable and path.contains("." + n + ".") and not out.has(n):
				out.append(n)
	out.sort()
	return out


## the text the gadget renderer draws for the root gadget whose name ends with `root` ("" when none)
func _drawn_text(root: String) -> Array:
	var out: Array = []
	for e in game._shell.gadget_draw_list():
		if String(e.get("kind", "")) == "text" and String(e.get("root_name", "")).ends_with(root):
			out.append(String(e.text))
	return out


## the GameList rows as drawn: [{ y (window), text (the row's file name column, or the first column of the new-save row) }] top to bottom and the list's window
## rectangle: the gadget draw commands of the list's root
func _list_rows() -> Dictionary:
	var by_y := {}
	var list := Rect2()
	var bars := []
	for e in game._shell.gadget_draw_list():
		if not String(e.get("root_name", "")).ends_with("GameList") or not e.has("root_rect"):
			continue
		list = e.root_rect
		if String(e.get("kind", "")) == "text":
			if not String(e.text).is_empty():
				# a text command carries its origin only: the window position of its top left
				var key := int(round((e.rect as Rect2).position.y))
				if not by_y.has(key):
					by_y[key] = []
				by_y[key].append({"x": (e.rect as Rect2).position.x, "text": String(e.text)})
		elif String(e.get("kind", "")) == "fill" or String(e.get("kind", "")) == "rect" or String(e.get("kind", "")) == "image":
			bars.append((e.rect as Rect2).abs())
	var ys := by_y.keys()
	ys.sort()
	var rows := []
	for y in ys:
		var cells: Array = by_y[y]
		cells.sort_custom(func(a, b): return a.x < b.x)
		rows.append({"y": float(y), "cells": cells.map(func(c): return c.text)})
	return {"rows": rows, "list": list, "bars": bars}


## the row the list highlights now: the drawn highlight bar's offset from row 0's bar (`bar0`) in rows (-1 when no bar is drawn)
func _selected_row(bar0: Rect2, pitch: float) -> int:
	var shown := _list_rows()
	var list: Rect2 = shown.list
	var best := Rect2()
	for b in shown.bars:
		var br: Rect2 = b
		if br.size.x >= list.size.x * 0.7 and br.size.y >= pitch * 0.6 and br.size.y <= pitch * 1.6 and (best.size == Vector2.ZERO or br.size.y < best.size.y):
			best = br
	if best.size == Vector2.ZERO:
		return -1
	return int(round((best.position.y - bar0.position.y) / pitch))


func _top_is_save_page() -> bool:
	var stack: PackedStringArray = game._shell.shell_stack()
	return stack.size() > 0 and stack[-1] == "SaveLoad.apt"


func _entry_text() -> String:
	var t := _drawn_text("FileNameTextEntry")
	return "" if t.is_empty() else String(t[0])


func _entry_rect() -> Rect2:
	for e in game._shell.gadget_draw_list():
		if String(e.get("root_name", "")).ends_with("FileNameTextEntry") and e.has("root_rect"):
			return e.root_rect
	return Rect2()


func _key(code: Key, unicode := 0) -> void:
	for pressed in [true, false]:
		var ev := InputEventKey.new()
		ev.keycode = code
		ev.physical_keycode = code
		ev.unicode = unicode
		ev.pressed = pressed
		Input.parse_input_event(ev)
		await game._step(2)


func _window_click(p: Vector2) -> void:
	# the pointer arrives first (a step), then one press and its release: a release with no press would be a click of its own to the list boxes
	var m := InputEventMouseMotion.new()
	m.position = p
	m.global_position = p
	game.get_viewport().push_input(m)
	await game._step(2)
	for pressed in [true, false]:
		var b := InputEventMouseButton.new()
		b.button_index = MOUSE_BUTTON_LEFT
		b.pressed = pressed
		b.position = p
		b.global_position = p
		game.get_viewport().push_input(b)
		await game._step(2)


## a screen over the quit menu closed (Escape, Cancel, Options' close): the menu is back as it was: every one of its six buttons is hittable again (the
## movie was unloaded under the screen and loaded again: RW 0x812A37 / 0x812913), the game still paused at `frame`, the menu's background as `background`
func _back_on_quit_menu(name: String, frame: int, background: int) -> void:
	var stack: PackedStringArray = game._shell.shell_stack()
	var got: Array = await _quit_all_buttons(5.0)
	var r: Dictionary = game._shell.quit_menu({"kind": "state"})
	var want: Array = QUIT_BUTTONS.duplicate()
	want.sort()
	_ui_check(name, (stack.is_empty() or stack[-1] != "SaveLoad.apt") and game._quit_open and r.get("open", false) and r.get("movie_loaded", false)
		and got == want and _background_mode() == background and game._world.get_frame() == frame,
		"stack %s, quit open %s, movie loaded %s, buttons %s, background mode %d (want %d), frame %d (paused at %d)" % [str(stack), str(r.get("open", false)),
		str(r.get("movie_loaded", false)), str(got), _background_mode(), background, game._world.get_frame(), frame])


## a key press and release through Godot's input with the modifier flags a keyboard reports (the HUD node reads the flags)
func _key_event(code: Key, pressed: bool, ctrl := false, unicode := 0) -> void:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.physical_keycode = code
	ev.unicode = unicode
	ev.pressed = pressed
	ev.ctrl_pressed = ctrl
	Input.parse_input_event(ev)
	await game._step(2)


func _camera_position() -> Vector2:
	return game._hud.get_camera().position


func _free_camera_toggles() -> int:
	return int(game._hud.get_camera().get("free_camera_toggles", -1))


func _open_page(button: String) -> bool:
	var ok: bool = await game._click_quit(button)
	ok = ok and await _wait_top("SaveLoad.apt", 10.0)
	await game._wait_seconds(1.5)
	return ok


func run_ui_test() -> void:
	ui_checks.clear()
	ui_failed = 0
	var waited := 0.0
	while (game._state != game.State.PLAYING or game._world.get_frame() < 100 or game._control_bar_hidden) and waited < 600.0:
		await game._wait_seconds(0.25)
		waited += 0.25
	print("SAVEUI playing at frame %d (%s)" % [game._world.get_frame(), game._game_kind])
	# the camera on a unit of ours (away from the map's edges: a held arrow must be able to move it), a unit selected (its decorations are drawn)
	var own: Array = game._world.get_player_objects(game._local_name)
	if not own.is_empty():
		game._hud.camera_look_at(Vector2(own[0].x, own[0].y))
		game._hud.select_object(int(own[0].id))
	await game._wait_seconds(2.0)
	game._save_named("saveui1-game")
	# the control: the health bars are drawn over the game (the test's Options.ini: OpenBFMEHealthBars = always)
	var bars_before := int(game._hud.get_icon_ui().get("drawn", 0)) if game._hud != null else 0
	# a key held when the menu opens: the control (it scrolls the camera), then it is released UNDER the menu: the camera must have stopped
	var cam0 := _camera_position()
	await _key_event(KEY_RIGHT, true)
	await game._wait_seconds(1.0)
	var cam_moving := _camera_position()
	var moved_before: bool = cam_moving != cam0
	# the quit menu: the in-game background (RW 0x92262A) and its six buttons
	await game._press_escape()
	await game._wait_seconds(2.0)
	var got: Array = await _quit_all_buttons(5.0)
	var want: Array = QUIT_BUTTONS.duplicate()
	want.sort()
	_ui_check("quit-menu", game._quit_open and got == want and _background_mode() == 2 and bars_before > 0 and _overlays_shown().is_empty(),
		"buttons %s, background mode %d, decorations drawn before %d, game overlays over the menu %s" % [str(got), _background_mode(), bars_before,
		str(_overlays_shown())]) # tolerance: the control needs any drawn decoration (the count is the scene's)
	var paused_frame: int = game._world.get_frame()
	# Save: the page over the menu (Cancel / Save / Delete, the name entry), the HUD movie not drawn over it
	var ok: bool = await _open_page("Save")
	var page: Array = _top_hittable(["Cancel", "Save", "Delete"])
	_ui_check("save-page", ok and page == ["Cancel", "Delete", "Save"] and _drawn_text("FileNameTextEntry").size() == 1 and not game.control_bar_drawn()
		and _background_mode() == 2 and _overlays_shown().is_empty(), "page buttons %s, name entry %s, control bar drawn %s, background mode %d, game overlays %s"
		% [str(page), str(_drawn_text("FileNameTextEntry")), str(game.control_bar_drawn()), _background_mode(), str(_overlays_shown())])
	# the held arrow is let go under the page: the camera does not go on moving (OPTS-6: input that began before the menu opened ends with it)
	var cam_at_open := _camera_position()
	await _key_event(KEY_RIGHT, false)
	await game._wait_seconds(1.5)
	_ui_check("held-key-cancelled", moved_before and _camera_position() == cam_at_open, "moved while held %s (%s -> %s), at the menu's opening %s, after the release under the menu %s" % [
		str(moved_before), str(cam0), str(cam_moving), str(cam_at_open), str(_camera_position())])
	# Escape on the page (RW 0x816627): the page closes, the quit menu is back
	await game._press_escape()
	await game._wait_seconds(1.5)
	await _back_on_quit_menu("escape-closes-page", paused_frame, 2)
	# Cancel (RW 0x816146)
	ok = await _open_page("Save")
	ok = ok and await _click_top("Cancel")
	await game._wait_seconds(1.5)
	await _back_on_quit_menu("cancel-closes-page", paused_frame, 2)
	# Options over the menu and back with Esc (CANCEL), then a second visit closed with ACCEPT: the pause menu is the same after each (OPTIONS included)
	for visit in 2:
		ok = await game._click_quit("Options")
		var w := 0.0
		while not game._shell.shell_stack().has("Options.apt") and w < 10.0:
			await game._wait_seconds(0.1)
			w += 0.1
		await game._wait_seconds(1.5)
		if visit == 0:
			await game._press_escape()
		else:
			ok = ok and await _click_top("Accept")
		await game._wait_seconds(1.5)
		# Options shows the front-end background and nothing hides it until the quit menu goes (RW 0x91EE08; its dtor, RW 0x921A96)
		await _back_on_quit_menu("options-back" if visit == 0 else "options-twice", paused_frame, 1)
	# a typed name: click into the entry, clear the default name, type; the drawn entry shows it
	ok = await _open_page("Save")
	var rect := _entry_rect()
	if rect.size.x > 0:
		await _window_click(rect.get_center())
	for i in 24:
		await _key(KEY_BACKSPACE)
	for ch in TYPED_NAME:
		var code: Key = OS.find_keycode_from_string(ch.to_upper()) if ch != " " else KEY_SPACE
		await _key(code, ch.unicode_at(0))
	await game._wait_seconds(0.5)
	var drawn: Array = _drawn_text("FileNameTextEntry")
	_ui_check("typed-name", ok and rect.size.x > 0 and drawn == [TYPED_NAME] and game._world.get_frame() == paused_frame,
		"entry at %s draws %s, frame %d" % [str(rect), str(drawn), game._world.get_frame()])
	# Ctrl+Z in the name field is the field's (retail: nothing), not the game's free camera toggle (INPUT-1's Ctrl+Z): no toggle, no text, the camera where it was
	var toggles0 := _free_camera_toggles()
	var cam_field := _camera_position()
	await _key_event(KEY_CTRL, true, true)
	await _key_event(KEY_Z, true, true)
	await _key_event(KEY_Z, false, true)
	await _key_event(KEY_CTRL, false, false)
	await game._wait_seconds(0.5)
	_ui_check("ctrl-z-in-name-field", toggles0 >= 0 and _free_camera_toggles() == toggles0 and _drawn_text("FileNameTextEntry") == [TYPED_NAME] and _camera_position() == cam_field
		and not game._hud.get_camera().get("free_camera", true), "toggles %d -> %d, field draws %s, free camera %s" % [toggles0, _free_camera_toggles(),
		str(_drawn_text("FileNameTextEntry")), str(game._hud.get_camera().get("free_camera"))])
	# Save: the file with the typed name, the result box; its Ok goes back to the game (state 10, RW 0x8184C0): menu closed, background hidden
	last_save = {}
	ok = await _click_top("Save")
	await game._wait_seconds(2.0)
	var path := save_dir().path_join(TYPED_NAME + ".obfmesave")
	var saved_ok: bool = last_save.get("ok", false) and str(last_save.get("path", "")) == path and FileAccess.file_exists(path)
	ok = ok and await _click_any_level("MessageBox", "Ok")
	await game._wait_seconds(2.0)
	var saved_frame := int(last_save.get("frame", -1))
	var stack: PackedStringArray = game._shell.shell_stack()
	var f0: int = game._world.get_frame()
	await game._wait_seconds(3.0)
	_ui_check("saved-back-to-game", ok and saved_ok and (stack.is_empty() or stack[-1] != "SaveLoad.apt") and not game._quit_open and _background_mode() == 0
		and game.control_bar_drawn() and game._world.get_frame() > f0, "save %s, stack %s, quit open %s, background mode %d, control bar drawn %s, frame %d -> %d"
		% [JSON.stringify(last_save).left(160), str(stack), str(game._quit_open), _background_mode(), str(game.control_bar_drawn()), f0, game._world.get_frame()])
	# the Save page's list ROWS by click effect: a second save (a copy of the first) makes three rows (new save, two files); each row is pressed just inside its top
	# and bottom edges and is selected (the retail handler RW 0x8180A8 -> RW 0x816FA3 puts the file's name in the entry; the new-save row's the default name);
	# just outside the list's left and right edges nothing is selected (the entry stays on the control row's name)
	var second := save_dir().path_join("Second Save.obfmesave")
	var copied := DirAccess.copy_absolute(path, second) == OK
	await game._press_escape()
	await game._wait_seconds(2.0)
	ok = await _open_page("Save")
	var bad_rows := PackedStringArray()
	var shown := _list_rows()
	var rows: Array = []
	for r in shown.rows:
		if r.cells.size() > 0 and String(r.cells[0]) != "Mission" and String(r.cells[0]) != "Saved Files":
			rows.append(r)
	var list: Rect2 = shown.list
	var row_names := []
	for r in rows:
		row_names.append(String(r.cells[0]) if String(r.cells[0]).begins_with("**") or r.cells.size() == 1 else String(r.cells[1]))
	if rows.size() != 3 or not copied:
		bad_rows.append("rows %s (copied %s)" % [str(row_names), str(copied)])
	else:
		var pitch: float = float(rows[1].y) - float(rows[0].y) # the rows' text tops are one row apart
		# the selected row (the new-save row, selected when the page opens) is drawn as a bar: its exact rectangle is row 0's, the others follow by the pitch
		var bar := Rect2()
		for b in shown.bars:
			var br: Rect2 = b
			if br.size.x >= list.size.x * 0.7 and br.size.y >= pitch * 0.6 and br.size.y <= pitch * 1.6 and (bar.size == Vector2.ZERO or br.size.y < bar.size.y):
				bar = br
		if bar.size == Vector2.ZERO:
			bad_rows.append("no highlighted row among the list's bars %s (pitch %.1f)" % [str(shown.bars), pitch])
		if OS.get_environment("OPTS6_INVENTORY") == "1":
			var gl := []
			for g in game._shell.list_gadgets():
				if g.kind == "list" or g.kind == "entry":
					gl.append("%s %s enabled %s hidden %s at (%d, %d) %d x %d entries %s selected %s row_height %s display_pos %s" % [g.kind, g.name, str(g.enabled), str(g.hidden), g.x, g.y,
						g.w, g.h, str(g.get("entries")), str(g.get("selected")), str(g.get("row_height")), str(g.get("display_pos"))])
			print("SAVEUI DIAG gadgets: ", " | ".join(gl))
		if OS.get_environment("OPTS6_SCAN") == "1" and bar.size != Vector2.ZERO:
			# diagnosis: the row a press selects at each 2 px from 20 px above the highlight bar's top to three rows below (the list window's rect from the pick)
			var scan := PackedStringArray()
			for dy in range(-20, int(pitch) * 4, 2):
				await _window_click(Vector2(list.get_center().x, bar.position.y + dy))
				await game._wait_seconds(0.6)
				scan.append("%d:%d" % [dy, _selected_row(bar, pitch)])
			var pick: Dictionary = game._shell.gadget_pick(game._shell.window_to_stage(Vector2(list.get_center().x, bar.position.y + 6)))
			print("SAVEUI DIAG list rows: row selected by a press dy px from the highlight's top (bar %s, pitch %.1f, list %s, pick %s): %s" % [str(bar), pitch, str(list), str(pick), " ".join(scan)])
			await _window_click(Vector2(list.get_center().x, bar.position.y + 4)) # back on the new-save row for the probes
			await game._wait_seconds(0.8)
		# the entry's text per listed row, by what the row shows
		var entry_of := func(i: int) -> String:
			return "Saved Game 1" if String(row_names[i]).begins_with("**") else String(row_names[i])
		# the pressable zone of a row is the list box's own (GadgetListBox rowAtMouse: the window's top plus the rows' heights, one pitch each), which is
		# not the drawn row (the text and the highlight are drawn 4 px lower: W3DListBox, donor facts of S-4981): the points are taken from the zones
		var gad_scale := pitch / 18.0 # the stage pixel in window pixels (the page's rows are 18 stage px apart)
		var zone_h: float = pitch # the rows' own pitch (the cumulative heights both the drawing and the hit test read)
		for k in (rows.size() if bar.size != Vector2.ZERO else 0):
			var top: float = list.position.y + zone_h * k
			var yc: float = top + zone_h * 0.5
			var points := [
				[Vector2(list.get_center().x, top + 2.0 * gad_scale), true, "top inside"], [Vector2(list.get_center().x, top + zone_h - 2.0 * gad_scale), true, "bottom inside"],
				[Vector2(list.position.x - 3.0, yc), false, "left outside"], [Vector2(list.end.x + 3.0, yc), false, "right outside"]]
			var base := (k + 1) % rows.size()
			var base_y: float = list.position.y + zone_h * (base + 0.5)
			for p in points:
				# the control: a press on another row selects that row (the drawn highlight moves to it; on a file row the entry takes the file's name)
				await _window_click(Vector2(list.get_center().x, base_y))
				await game._wait_seconds(1.3) # past the double-click time (a double click on a file saves)
				if OS.get_environment("OPTS6_INVENTORY") == "1" and k == 0 and p[2] == "top inside":
					for g in game._shell.list_gadgets():
						if g.kind == "list":
							print("SAVEUI DIAG after the first press: selected %s double_click_time %s time_ms %s enabled %s" % [str(g.get("selected")), str(g.get("double_click_time")), str(g.get("time_ms")), str(g.enabled)])
				var control_row := _selected_row(bar, pitch)
				if control_row != base or (not String(row_names[base]).begins_with("**") and _entry_text() != String(row_names[base])):
					bad_rows.append("row %d %s: the control press on row %d selected row %d (entry '%s')" % [k, p[2], base, control_row, _entry_text()])
					continue
				# the probe: inside the edge selects row k, outside leaves the control row selected
				await _window_click(p[0])
				await game._wait_seconds(1.3)
				var now_row := _selected_row(bar, pitch)
				var want_row: int = k if p[1] else base
				if now_row != want_row:
					bad_rows.append("row %d (%s) %s: row %d is selected, expected %d (entry '%s')" % [k, row_names[k], p[2], now_row, want_row, _entry_text()])
				elif p[1] and not String(row_names[k]).begins_with("**") and _entry_text() != String(row_names[k]):
					bad_rows.append("row %d (%s) %s: selected, the entry shows '%s'" % [k, row_names[k], p[2], _entry_text()])
				if OS.get_environment("OPTS6_INVENTORY") == "1" and k == 0 and p[2] == "top inside":
					print("SAVEUI DIAG the new-save row selected: entry '%s'" % _entry_text())
	var ys := rows.map(func(r): return int(r.y))
	var why_stuck := ""
	if not bad_rows.is_empty():
		var gl2 := []
		for g in game._shell.list_gadgets():
			if g.kind == "list" or g.kind == "entry":
				gl2.append("%s enabled %s" % [g.name, str(g.enabled)])
		why_stuck = " | screen %s, gadgets %s, stack %s, box %s" % [str(game._shell.screen_state()).left(300), ", ".join(gl2), str(game._shell.shell_stack()), str(game._shell.list_buttons(game._shell.shell_top_level()).map(func(b): return String(b.path).get_slice(".instance", 0).get_file() if false else String(b.path).right(40)))]
		var lines: PackedStringArray = game._shell.get_shell_report().get("note_lines", PackedStringArray())
		why_stuck += " | notes " + " // ".join(lines.slice(maxi(0, lines.size() - 14)))
	_ui_check("save-list-rows", ok and bad_rows.is_empty(), "rows %s at y %s in %s; %s%s" % [str(row_names), str(ys), str(list), "; ".join(bad_rows), why_stuck])
	# back to the running game whatever the probes left (the checks after this one need it): the page closes (Esc), then the quit menu (Esc)
	for i in 3:
		if _top_is_save_page():
			await game._press_escape()
			await game._wait_seconds(1.5)
	if game._quit_open:
		await game._press_escape()
		await game._wait_seconds(1.5)
	DirAccess.remove_absolute(second)
	# Ctrl pressed before a menu and let go under it, the menu closed with the keyboard (no pointer event to repair the state): a plain Z after Resume is a
	# plain Z (no Ctrl left in the HUD's key state: no camera toggle)
	toggles0 = _free_camera_toggles()
	await _key_event(KEY_CTRL, true, true)
	await _key_event(KEY_ESCAPE, true, true)
	await _key_event(KEY_ESCAPE, false, true)
	await game._wait_seconds(2.0)
	var open_a: bool = game._quit_open
	await _key_event(KEY_CTRL, false, false)
	await _key_event(KEY_ESCAPE, true, false)
	await _key_event(KEY_ESCAPE, false, false)
	await game._wait_seconds(1.5)
	await _key_event(KEY_Z, true, false)
	await _key_event(KEY_Z, false, false)
	await game._wait_seconds(0.5)
	var key_state := int(game._hud.get_state().get("keys", {}).get("key_state", -1))
	_ui_check("modifier-released-in-menu", open_a and not game._quit_open and _free_camera_toggles() == toggles0 and key_state == 0,
		"quit menu opened %s, closed %s, toggles %d -> %d, the HUD's key state %d" % [str(open_a), str(not game._quit_open), toggles0, _free_camera_toggles(), key_state])
	# Ctrl held through a menu (opened and closed by Esc with Ctrl down): the first hotkey after it sees Ctrl (Ctrl+Z toggles the free camera once), and back
	await _key_event(KEY_CTRL, true, true)
	await _key_event(KEY_ESCAPE, true, true)
	await _key_event(KEY_ESCAPE, false, true)
	await game._wait_seconds(2.0)
	var open_b: bool = game._quit_open
	await _key_event(KEY_ESCAPE, true, true)
	await _key_event(KEY_ESCAPE, false, true)
	await game._wait_seconds(1.5)
	await _key_event(KEY_Z, true, true)
	await _key_event(KEY_Z, false, true)
	await game._wait_seconds(0.5)
	var toggled := _free_camera_toggles() == toggles0 + 1
	await _key_event(KEY_Z, true, true) # back to the retail camera
	await _key_event(KEY_Z, false, true)
	await _key_event(KEY_CTRL, false, false)
	await game._wait_seconds(0.5)
	_ui_check("modifier-held-through-menu", open_b and not game._quit_open and toggled and _free_camera_toggles() == toggles0 + 2
		and not game._hud.get_camera().get("free_camera", true), "menu opened %s, toggles %d -> %d (first Z after Resume toggled %s), free camera %s" % [
		str(open_b), toggles0, _free_camera_toggles(), str(toggled), str(game._hud.get_camera().get("free_camera"))])
	# Load from the quit menu: the page lists the typed name; Load, the confirmation, the catch-up; the loaded game runs on
	await game._press_escape()
	await game._wait_seconds(2.0)
	ok = await _open_page("Load")
	var listed: Array = _drawn_text("GameList")
	_ui_check("load-page-lists", ok and listed.has(TYPED_NAME), "GameList draws %s" % str(listed))
	last_load = {}
	ok = await _click_top("Load")
	await game._wait_seconds(1.5)
	ok = ok and await _click_top("ConfirmationOk")
	var w2 := 0.0
	while (game._state != game.State.PLAYING or last_load.is_empty()) and w2 < 180.0:
		await game._wait_seconds(0.1)
		w2 += 0.1
	await game._wait_seconds(2.0)
	f0 = game._world.get_frame()
	await game._wait_seconds(3.0)
	_ui_check("loaded-plays-on", ok and last_load.get("state", "") == "ready" and int(last_load.get("frame", -1)) == saved_frame and not game._quit_open
		and _background_mode() == 0 and game._world.get_frame() > f0, "catch-up %s (saved frame %d), quit open %s, background mode %d, frame %d -> %d" % [
		JSON.stringify(last_load), saved_frame, str(game._quit_open), _background_mode(), f0, game._world.get_frame()])
	# the plain way: Esc, Resume: the in-game background goes with the menu (RW 0x921A96)
	await game._press_escape()
	await game._wait_seconds(2.0)
	ok = await game._click_quit("Resume")
	await game._wait_seconds(2.0)
	f0 = game._world.get_frame()
	await game._wait_seconds(2.0)
	var bars_after := int(game._hud.get_icon_ui().get("drawn", 0)) if game._hud != null else 0
	_ui_check("resume-clears-background", ok and not game._quit_open and _background_mode() == 0 and game._world.get_frame() > f0 and bars_after > 0,
		"quit open %s, background mode %d, frame %d -> %d, decorations drawn %d" % [str(game._quit_open), _background_mode(), f0, game._world.get_frame(),
		bars_after]) # tolerance: drawn again at all (the count is the scene's)
	print("SAVEUI RESULT %d checks, %d failed: %s" % [ui_checks.size(), ui_failed, ",".join(ui_checks)])
	game.get_tree().quit(0 if ui_failed == 0 else 1)
