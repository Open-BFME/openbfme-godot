## OpenBFME. GPL-3.0.
## Asset-free check of the original relay seats for a battle with an AI participant.
extends SceneTree

const War := preload("res://scripts/wotr.gd")


func _initialize() -> void:
	var war := War.new()
	war.state = {"players": [{"human": true}, {"human": false}, {"human": true}]}
	war.slot_of_player = [0, 2, 1]
	var full_battle_seats := [0, 2, 1]
	var phase_seats: Array = war.online_battle_phase_seats([0, 1, 2])
	var ok: bool = phase_seats == [0, 1] and full_battle_seats == war.slot_of_player
	ok = ok and war.online_battle_phase_seats([0, 1]) == [0]
	war.free()
	print("ONLINE WAR PHASE SEATS ", "PASS" if ok else "FAIL")
	quit(0 if ok else 1)
