extends SceneTree

class FakeShell extends Node2D:
	var stack := PackedStringArray(["MainMenu.apt"])
	var calls: Array = []
	func shell_stack() -> PackedStringArray: return stack
	func shell_fscommand(command: String, argument: String) -> bool:
		calls.append([command, argument])
		return true

func _initialize() -> void:
	var game = load("res://scripts/game.gd").new()
	var shell := FakeShell.new()
	game._shell = shell
	game._state = game.State.MENU
	game._controller_menu_action("skirmish")
	assert(shell.calls == [["AptMainMenu::Skirmish", ""]])
	shell.stack = PackedStringArray(["MainMenu.apt", "Skirmish.apt"])
	game._controller_menu_action("skirmish")
	assert(shell.calls.back() == ["AptSkirmish::StartGame", ""])
	shell.stack.append("Options.apt")
	game._controller_menu_action("skirmish")
	game._controller_menu_action("jumpToSelection")
	assert(shell.calls.size() == 2)
	shell.stack = PackedStringArray(["MainMenu.apt"])
	game._state = game.State.PLAYING
	game._controller_menu_action("skirmish")
	assert(shell.calls.size() == 2)
	game._shell = null
	game.free()
	shell.free()
	print("CONTROLLER MENU PASS")
	quit()
