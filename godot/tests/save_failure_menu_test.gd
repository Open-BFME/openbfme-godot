## A late save refusal must redraw its error page instead of retaining the loading canvas.
## Synthetic world/shell only: no saves, retail data, renderer or native extension.
extends SceneTree

class Flow extends "res://scripts/save_flow.gd":
	func save_dir() -> String:
		return "" # The error page has no files; this test never touches a save folder.


class World extends RefCounted:
	var catch_up := false
	func check_save(_path: String) -> Dictionary:
		return {"ok": true}
	func prepare_save_load(_path: String) -> Dictionary:
		return {"ok": true, "message": {}}
	func start_save_load(_options: Dictionary) -> Dictionary:
		if catch_up:
			return {"ok": true, "mode": "catch_up", "start_frame": 0, "target_frame": 1}
		return {"ok": false, "errors": ["synthetic snapshot refusal"]}
	func save_load_step(_frames: int) -> Dictionary:
		return {"state": "failed", "error": "synthetic catch-up refusal"}


class Shell extends RefCounted:
	var auto_process := true
	var stack := PackedStringArray(["MainMenu.apt", "SaveLoad.apt"])
	var still := false
	var progress := -1
	var drawn := "SaveLoad.apt"
	var refused := []
	func shell_stack() -> PackedStringArray:
		return stack
	func shell_pop() -> void:
		stack.remove_at(stack.size() - 1)
	func shell_push(screen: String) -> bool:
		stack.append(screen)
		return true
	func tick(_delta: float) -> void:
		pass
	func choose_load_screen(_options: Dictionary) -> Dictionary:
		return {"kind": "still", "picture": "SyntheticPicture"}
	func show_still_load_screen(_picture: String) -> Dictionary:
		still = true
		drawn = "Loading"
		return {"ok": true}
	func hide_still_load_screen() -> void:
		still = false # Native hide marks the retained canvas dirty; it does not redraw it.
	func set_still_load_progress(value: int, _text: String) -> void:
		progress = value
	func set_save_load(_info: Dictionary) -> void:
		pass
	func save_load_refused(name: String, error: String) -> void:
		refused = [name, error]
	func render(_force: bool) -> void:
		drawn = "Loading" if still else (stack[-1] if not stack.is_empty() else "")
	func automatic_frame() -> void:
		if auto_process:
			render(false)


class Game extends Node:
	enum State { MENU, LOADING, PLAYING, SCORE }
	var _state := State.MENU
	var _world := World.new()
	var _shell := Shell.new()
	var _campaign_flow = null
	var _wotr_flow := false
	var _end := false
	var _record := false
	var _save_test := ""
	var _start_stack := PackedStringArray()
	var _last_local_slot := -1
	var _last_new_game := {}
	var _report := {}
	var _game_kind := ""
	var tears := 0
	func _shell_leave_for_game() -> void:
		pass
	func _tear_down_game(_reason: String) -> void:
		tears += 1
		_shell.stack.clear()
	func _score_continue() -> void:
		_shell.stack = PackedStringArray(["MainMenu.apt"])
		_state = State.MENU


var failures := 0

func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		printerr("SAVE FAILURE MENU FAIL: ", message)


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	for catch_up in [false, true]:
		var game := Game.new()
		root.add_child(game)
		game._world.catch_up = catch_up
		var flow := Flow.new(game)
		var error: String = await flow.load_save("Synthetic.obfmesave")
		var expected := "synthetic catch-up refusal" if catch_up else "synthetic snapshot refusal"
		check(error == expected, "original error is returned")
		check(game._shell.refused == ["Synthetic", expected], "error remains named on the load page")
		check(game.tears == 1, "failed game is torn down once")
		check(game._state == Game.State.MENU, "logical menu state is restored")
		check(not game._shell.still and not flow._overlay_on and game._shell.progress == -1, "loading picture and progress are cleared")
		check(game._shell.auto_process, "native shell processing resumes")
		game._shell.automatic_frame()
		check(game._shell.drawn == "SaveLoad.apt", "next automatic frame replaces the stale Loading canvas with the load page")
		# Cleanup remains safe when called after the loading UI was already removed.
		flow._back_to_menu("Synthetic", expected)
		game._shell.automatic_frame()
		check(game._shell.drawn == "SaveLoad.apt" and game._shell.refused == ["Synthetic", expected], "repeated cleanup keeps the named error page")
		game.free()
	print("SAVE FAILURE MENU ", "PASS" if failures == 0 else "FAILED")
	quit(0 if failures == 0 else 1)
