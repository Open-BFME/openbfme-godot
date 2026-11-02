## OpenBFME. GPL-3.0.
## Lane SAVE-4e review r2 (Sol): scripts/save_flow.gd's LoadGameFile with a stand-in GameWorld and shell (tools/save/test_save_flow.py runs it). A
## running game that cannot be kept (GameWorld.keep_running_game refused) is never torn down: the load stops before the saved game is prepared and the
## refusal is shown on the load page; a kept game is left for the load as before. One "FLOW ok|FAIL" line per check; exit 0 when all hold.
extends SceneTree

const SaveFlow := preload("res://scripts/save_flow.gd")

var _ok := true


class World extends RefCounted:
	var keep_ok := false
	var calls: PackedStringArray = []

	func check_save(_path: String) -> Dictionary:
		calls.append("check_save")
		return {"ok": true}

	func keep_running_game() -> Dictionary:
		calls.append("keep_running_game")
		if keep_ok:
			return {"ok": true, "frame": 120}
		return {"ok": false, "error": "the game cannot be saved: the snapshot is refused: test:Unsaved"}

	func prepare_save_load(_path: String) -> Dictionary:
		calls.append("prepare_save_load")
		return {"ok": false, "errors": ["the save's snapshot is refused: obfme:LargeGroupAudio"]}

	func prepare_kept_load() -> Dictionary:
		calls.append("prepare_kept_load")
		return {"ok": false, "errors": ["the kept game is not loaded in this test"]}

	func drop_kept_game() -> void:
		calls.append("drop_kept_game")


class Shell extends RefCounted:
	var refused: Array = []
	var auto_process := true

	func hide_still_load_screen() -> void:
		pass

	func save_load_refused(name: String, error: String) -> void:
		refused.append([name, error])


class Game extends Node:
	enum State {PLAYING, SCORE}
	var _state = State.PLAYING
	var _world := World.new()
	var _shell := Shell.new()
	var _campaign_flow = null
	var _save_test := "flow"
	var _start_stack := PackedStringArray()
	var tears: PackedStringArray = []
	var failed := ""

	func _close_quit_menu() -> void:
		pass

	func _tear_down_game(reason: String) -> void:
		tears.append(reason)

	func _fail(error: String) -> void:
		failed = error


func _init() -> void:
	_run.call_deferred()


func _check(what: String, cond: bool, detail := "") -> void:
	print("FLOW %s %s%s" % ["ok  " if cond else "FAIL", what, ": " + detail if not cond or detail != "" else ""])
	_ok = _ok and cond


func _run() -> void:
	# the running game cannot be kept: nothing is torn down, the saved game is never prepared, the refusal is on the load page
	var g := Game.new()
	var flow = SaveFlow.new(g)
	await flow.load_save("user://saves/Hostile.obfmesave")
	_check("a game that cannot be kept is not torn down", g.tears.is_empty(), str(g.tears))
	_check("the saved game is not prepared", not g._world.calls.has("prepare_save_load"), str(g._world.calls))
	_check("the game is still playing", g._state == Game.State.PLAYING)
	_check("the refusal is shown on the load page, named", g._shell.refused.size() == 1 and g._shell.refused[0][0] == "Hostile"
		and str(g._shell.refused[0][1]).contains("could not be kept") and str(g._shell.refused[0][1]).contains("test:Unsaved"), str(g._shell.refused))
	_check("no failure reported", g.failed.is_empty(), g.failed)
	g.free()
	# a kept game: left for the load (torn down once), the saved game prepared; its refusal then fails by name (the kept game not loaded here)
	var k := Game.new()
	k._world.keep_ok = true
	var kflow = SaveFlow.new(k)
	await kflow.load_save("user://saves/Hostile.obfmesave")
	_check("a kept game is left for the load", k.tears.size() == 1 and k.tears[0] == "load", str(k.tears))
	_check("the saved game is prepared after the keep", str(k._world.calls).find("keep_running_game") < str(k._world.calls).find("prepare_save_load")
		and k._world.calls.has("prepare_save_load"), str(k._world.calls))
	_check("the refused load fails by name", k.failed.contains("obfme:LargeGroupAudio"), k.failed)
	k.free()
	print("FLOW %s" % ("PASS" if _ok else "FAILED"))
	quit(0 if _ok else 1)
