## FB-0045: exercise game.gd's Escape dispatch without game files.
## Native pending-purchase semantics are covered by the shellfb1 SpellStore C++ test.
## godot --headless --path godot --script res://tests/shellfb1_spell_store_escape_test.gd
extends SceneTree

class ShellStub:
	extends Node2D
	func shell_stack() -> PackedStringArray:
		return PackedStringArray()
	func menu_has_input() -> bool:
		return false


class HudStub:
	extends Node
	var store_open := true
	var cancelled := 0
	var accepted := 0
	func get_spellbook_state() -> Dictionary:
		return {"store": {"open": store_open}}
	func cancel_spell_store() -> void:
		cancelled += 1
		store_open = false
	func close_spell_store() -> void:
		accepted += 1
		store_open = false


var _failures := 0


func _check(ok: bool, message: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", message)
	if not ok:
		_failures += 1


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var game_input := GDScript.new()
	game_input.source_code = "extends \"res://scripts/game.gd\"\nfunc _ready() -> void:\n\tpass\n"
	var compile_error := game_input.reload()
	_check(compile_error == OK, "game.gd input subclass parses and typechecks")
	if compile_error != OK:
		quit(1)
		return
	var game = game_input.new()
	var shell := ShellStub.new()
	var hud := HudStub.new()
	game.add_child(shell)
	game.add_child(hud)
	root.add_child(game)
	game.set_process(false)
	game.set_process_input(true)
	game._shell = shell
	game._hud = hud
	game._state = game.State.PLAYING
	var escape := InputEventKey.new()
	escape.keycode = KEY_ESCAPE
	escape.pressed = true
	root.get_viewport().push_input(escape)
	await process_frame
	_check(hud.cancelled == 1, "Escape dispatches cancellation exactly once")
	_check(hud.accepted == 0, "Escape never dispatches Accept")
	_check(not hud.store_open, "the store is closed")
	_check(not game._quit_open, "Escape does not open pause behind the store")
	game.free()
	print("SHELLFB1 SPELL STORE ESCAPE ", "PASS" if _failures == 0 else "FAIL")
	quit(0 if _failures == 0 else 1)
