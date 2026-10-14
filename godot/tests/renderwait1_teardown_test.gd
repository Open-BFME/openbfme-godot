## Lane RENDERWAIT-1 r3 (Sol r2): what the render side deferred for a game (script requests waiting for a held logic to play their audio) never survives
## the game's teardown into the next one. Game 1 loads, three script sounds are deferred (as a mission's scripts do while the worker is busy), game 2 loads
## over it (GameWorld::clear_scene): nothing of game 1 is pending, and nothing is applied to game 2 while it runs.
##
##   godot --headless --path godot --script res://tests/renderwait1_teardown_test.gd
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

const MAP := "map mp brown lands"
var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("RENDERWAIT1 TEARDOWN %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set")
		return 77
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		return 1
	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	if not world.setup(fs).ok:
		return 1
	var slots := [
		{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionMordor", "human": false, "team": 1, "start_index": 1},
	]
	var applied0 := 0
	for game in [1, 2]:
		var rep: Dictionary = world.load_map(MAP, {"slots": slots, "seed": 4711, "logic_thread": true})
		if not rep.ok:
			print("  load_map ", game, " failed ", rep.errors.slice(0, 3))
			return 1
		_check(world.get_pending_script_audio() == 0, "game %d starts with no deferred script audio (%d)" % [game, world.get_pending_script_audio()])
		world.set_auto_advance(false)
		if game == 1:
			for s in ["Sound_A", "Sound_B", "Sound_C"]:
				world.debug_defer_script_audio(s)
			_check(world.get_pending_script_audio() == 3, "three script sounds deferred in game 1")
			applied0 = world.get_script_audio_applied()
		else:
			for i in 30:
				world.advance(1.0 / 60.0)
				OS.delay_usec(16000)
			_check(world.get_pending_script_audio() == 0, "game 2 ran 30 render frames with nothing of game 1 pending (%d)" % world.get_pending_script_audio())
			# load_map(2) clears the scene and then runs frames the logic is held in: a leaked request would have been handed to game 2's audio there
			_check(world.get_script_audio_applied() == applied0, "none of game 1's deferred script audio was handed to game 2's audio (%d were)" % (world.get_script_audio_applied() - applied0))
	world.set_logic_thread(false)
	return 1 if _failures > 0 else 0
