extends SceneTree
# The client must allow a non-first allied claimant and send only one command
# while LAN lockstep is still carrying that command to the strategic logic.
const Choice := preload("res://scripts/wotr_award_choice.gd")
var failures := 0

class LogicStub extends RefCounted:
	var requests: Array = []
	func resolve_region_award(region: int, winner: int) -> Dictionary:
		requests.append([region, winner])
		return {"ok": true, "queued": true}

class WarStub extends Node:
	var active := true
	var local_player := 0
	var state := {"region_award_disputes": [{"region": 0, "resolver": 0, "players": [0, 1]}],
		"players": [{"name": "First"}, {"name": "Second"}]}
	var regions := [{"name": "Test territory"}]
	var lw := LogicStub.new()
	var runs := 0
	func in_battle() -> bool:
		return false
	func _run_logic() -> void:
		runs += 1

func check(ok: bool, message: String) -> void:
	print("WOTR AWARD ", "PASS " if ok else "FAIL ", message)
	if not ok:
		failures += 1

func _initialize() -> void:
	run.call_deferred()

func run() -> void:
	var war := WarStub.new()
	root.add_child(war)
	var choice := Choice.new()
	choice.sync(war)
	check(choice.dialog.visible, "resolver sees the choice")
	var choices: VBoxContainer = choice.dialog.get_child(choice.dialog.get_child_count() - 1)
	var second: Button = choices.get_child(2)
	second.pressed.emit()
	second.pressed.emit()
	check(war.lw.requests == [[0, 1]], "second claimant selected once")
	check(war.runs == 1, "logic resumes once")
	war.state.region_award_disputes = []
	choice.sync(war)
	check(choice.dialog == null, "settled choice closes")
	war.state.region_award_disputes = [{"region": 0, "resolver": 1, "players": [0, 1]}]
	choice.sync(war)
	check(choice.dialog == null, "other clients cannot choose")
	war.queue_free()
	await process_frame
	quit(0 if failures == 0 else 1)
