## Headless test of starting a skirmish (lane START-1): the logic's consumer of a new-game message through the GameWorld class (two runs of the same message agree on the
## state hash of every frame; the resolution is the logic's, the load progress is monotonic with the retail milestones) and the whole game flow of scenes/game.tscn run
## as a child process (--auto: main menu -> Skirmish by mouse events -> lobby -> Start -> load screen -> live game, exit code 0 = no load errors).
##
##   godot --headless --path godot --script res://tests/start_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("START %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _message() -> Dictionary:
	var slots: Array = []
	for i in 8:
		slots.append({"state": 1, "name": "Closed", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": -1})
	slots[0] = {"state": 6, "name": "Gimli", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": 0}
	slots[1] = {"state": 4, "name": "Hard AI", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": 1}
	slots[2] = {"state": 3, "name": "Medium AI", "accepted": true, "color": -1, "start_pos": -1, "player_template": -1, "team": 1}
	return {"mode": "skirmish", "difficulty": 1, "rank_points": 0, "map": "maps/map mp evendim/map mp evendim.map", "map_crc": 0, "map_size": 0, "map_mask": 0,
		"seed": 9001, "starting_cash": 2000, "slots": slots}


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return 77
	if not ClassDB.class_exists("GameWorld"):
		print("FAIL: the openbfme extension is not loaded")
		return 1
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("FAIL: mount: ", mount.errors)
		return 1
	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	_check(setup.ok, "the object world loads")
	if not setup.ok:
		return 1
	# two runs of one message
	var hashes: Array = []
	var progress_runs: Array = []
	var resolved_runs: Array = []
	for run in 2:
		var prep: Dictionary = world.prepare_new_game(_message())
		_check(prep.ok, "run %d: prepare_new_game %s" % [run, str(prep.get("errors", []))])
		if not prep.ok:
			return 1
		resolved_runs.append(prep.resolved)
		var seen: Array = []
		var started: Dictionary = world.start_new_game({"progress": func(p: int) -> void: seen.append(p)})
		_check(started.ok, "run %d: start_new_game %s" % [run, str(started.get("errors", []).slice(0, 3))])
		if not started.ok:
			return 1
		progress_runs.append(seen)
		var h: Array = [world.get_state_hash()]
		for f in 6:
			world.advance(0.2)
			h.append(world.get_state_hash())
		hashes.append(h)
		if run == 0:
			var r: Dictionary = prep.resolved
			var starts := {}
			var colours := {}
			for i in 3:
				starts[r.slots[i].start_pos] = true
				colours[r.slots[i].color] = true
				_check(r.slots[i].player_template >= 3 and r.slots[i].player_template <= 10, "slot %d has a playable faction (%d)" % [i, r.slots[i].player_template])
			_check(starts.size() == 3, "three distinct start positions")
			_check(colours.size() == 3, "three distinct colours")
			_check(started.start.starting_objects.size() == 9, "three starting bases of a structure and two units (%d objects)" % started.start.starting_objects.size())
			_check(started.start.errors.is_empty(), "no start errors")
			_check(started.start.slot_players.size() == 8, "a side name per slot")
	_check(hashes[0] == hashes[1], "the state hash of the first frames is the same in both runs")
	_check(hashes[0][0] != hashes[0][6], "the world moves (hash changes over 6 frames)")
	_check(progress_runs[0] == progress_runs[1], "the progress milestones are the same in both runs")
	var mono := true
	for i in range(1, progress_runs[0].size()):
		mono = mono and progress_runs[0][i] >= progress_runs[0][i - 1]
	_check(mono and progress_runs[0][0] == 1 and progress_runs[0][-1] == 100, "progress is monotonic from 1 to 100: %s" % str(progress_runs[0]))
	_check(JSON.stringify(resolved_runs[0]) == JSON.stringify(resolved_runs[1]), "both runs resolved the same slots")
	# a different seed
	var m: Dictionary = _message()
	m.seed = 9002
	world.prepare_new_game(m)
	world.start_new_game({})
	_check(world.get_state_hash() != hashes[0][0], "another seed gives another world")
	# the whole flow of the game scene
	world.queue_free()
	var output: Array = []
	var godot_path := OS.get_executable_path()
	var project := ProjectSettings.globalize_path("res://")
	var exit_code := OS.execute(godot_path, ["--headless", "--path", project, "--", "--auto", "--check", "--advance=1"], output, true)
	var text: String = "".join(output)
	_check(exit_code == 0, "scenes/game.tscn --auto --check exits 0 (got %d)" % exit_code)
	_check(text.contains("GAME load errors: 0"), "the game flow reports no load errors")
	_check(text.contains("GAME screen: LoadScreen.apt") and text.contains("GAME screen: Palantir.apt"), "the shell showed the load screen and then Palantir")
	_check(text.contains("palantir-initialized"), "AptPalantir::OnInitialized fired")
	# AUDIO-2 review r1 fix 2: the real game attaches its audio side (owners, player filter, Eva side, the in-game music scripts)
	_check(text.contains("GAME audio attached: ") and text.contains("\"ok\": true"), "the game flow attached the live game's audio")
	return 0 if _failures == 0 else 1
