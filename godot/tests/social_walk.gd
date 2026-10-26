## SOCIAL-1 visual walk through the real game and retail Messenger.
## Use tools/net/social_preview.py (loopback-only, synthetic users), not production.
extends RefCounted

var game: Node
var shell: Node


func run(host: Node) -> bool:
	game = host
	shell = game._shell
	var deadline := Time.get_ticks_msec() + 15000
	while Time.get_ticks_msec() < deadline and not shell.online_social_status().get("available", false):
		await game.get_tree().process_frame
	if not shell.online_social_status().get("available", false):
		printerr("SOCIAL WALK: social identity did not become available")
		return false
	await game._wait_seconds(2.0)
	shell.shell_fscommand("AptMessenger::OnMessengerBttn", "")
	await game._wait_seconds(2.0)
	var state: Dictionary = shell.online_messenger_sync({})
	if not state.get("open", false):
		printerr("SOCIAL WALK: retail Messenger did not open")
		return false
	game._online_shot("social-friends")
	var selected := false
	for gadget in shell.list_gadgets():
		if str(gadget.get("name", "")) == "Messenger::PlayersTab_0":
			if gadget.get("entries", 0) == 0 or gadget.get("hidden", true):
				printerr("SOCIAL WALK: FRIENDS list is empty or hidden")
				return false
			await _click(Vector2(float(gadget.x) + float(gadget.w) * 0.5, float(gadget.y) + 10))
			selected = true
			break
	if not selected:
		printerr("SOCIAL WALK: FRIENDS list gadget is missing")
		return false
	await game._wait_seconds(1.0)
	if str(shell.online_messenger_sync({}).get("selected", "")).is_empty():
		printerr("SOCIAL WALK: FRIENDS row click did not select a public player")
		return false
	game._online_shot("social-chat")
	if not await _switch_tab(1, "buddyGadgets.switchBttn"):
		return false
	var players_ready := false
	for gadget in shell.list_gadgets():
		if gadget.get("name", "") == "Messenger::PlayersTab_1" and not gadget.get("hidden", true) and gadget.get("entries", 0) != 0:
			await _click(Vector2(float(gadget.x) + float(gadget.w) * 0.5, float(gadget.y) + 10))
			players_ready = true
			break
	if not players_ready:
		printerr("SOCIAL WALK: PLAYERS list is missing, empty or hidden")
		return false
	await game._wait_seconds(0.5)
	if str(shell.online_messenger_sync({}).get("selected", "")).is_empty():
		printerr("SOCIAL WALK: PLAYERS row click did not select a public player")
		return false
	game._online_shot("social-players")
	if not await _switch_tab(0, "buddyGadgets.instance22"):
		return false
	# lane ONLINEMENU-1 (owner, 2026-10-20): the friends features are the Messenger's (the FRIENDS page is gone); its questions use GuiFX's box
	# (tests/online_social_test.gd)
	shell.shell_fscommand("AptMessenger::OnMessengerBttn", "")
	await game._wait_seconds(1.0)
	var privacy: RefCounted = load("res://tests/social_privacy_walk.gd").new()
	if not await privacy.run(game): return false
	print("SOCIAL WALK: real retail Messenger captured")
	return true


func _switch_tab(tab: int, button_suffix: String) -> bool:
	for gadget in shell.list_gadgets():
		if gadget.get("name", "") != "Messenger::PlayersTab_0":
			continue
		for button in shell.list_buttons(int(gadget.level)):
			if str(button.path).ends_with(button_suffix) and button.get("hittable", false):
				await _click(Vector2(button.x, button.y))
				await game._wait_seconds(1.0)
				if shell.online_messenger_sync({}).get("tab", -1) == tab:
					return true
	printerr("SOCIAL WALK: native Messenger tab button did not switch tabs")
	return false


func _click(at: Vector2) -> void:
	game._mouse_event(at, false, false)
	await game._step(2)
	game._mouse_event(at, true, true)
	await game._step(2)
	game._mouse_event(at, false, true)
	await game._step(6)
