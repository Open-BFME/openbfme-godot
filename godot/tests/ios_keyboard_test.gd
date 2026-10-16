extends SceneTree
class FakeShell extends Node:
	var focused := true
	var text := ""
	var submitted := false
	func native_text_input() -> Dictionary:
		return {"focused":focused,"id":1,"text":text,"rect":Rect2(100,100,200,40)}
	func edit_native_text(value: String, submit: bool) -> void:
		text = value
		submitted = submit
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var shell := FakeShell.new()
	root.add_child(shell)
	var bridge = preload("res://scripts/release/ios_keyboard.gd").new()
	bridge.shell = func() -> Node: return shell
	root.add_child(bridge)
	await process_frame
	bridge._process(0)
	assert(bridge.focus_id == 1 and bridge.editor.has_focus())
	bridge.editor.text = "iPad profile"
	bridge.editor.text_changed.emit(bridge.editor.text)
	assert(shell.text == "iPad profile")
	bridge.editor.text_submitted.emit(bridge.editor.text)
	assert(shell.submitted)
	shell.focused = false
	bridge._process(0)
	assert(bridge.focus_id == 0 and not bridge.editor.has_focus())
	print("IOS KEYBOARD BRIDGE PASS")
	quit()
