## Lane ONLINEMENU-1: a walk through the friends features in RotWK's own Messenger for the owner's video (game.gd --online-walk=friends
## --friends-code=<a friend's code>): the Messenger opened, a real click into its entry, the friend's code typed with real key presses and sent with
## ENTER, the friend chosen with a click once the second player (tools/net/onlinemenu1_friends_video.sh's helper) accepted, a reply typed, the
## PLAYERS tab and back; pauses so each step reads. Prints FRIENDS WALK ok / FAIL.
extends RefCounted

var game: Node
var shell: Node


func run(host: Node, code: String) -> bool:
	game = host
	shell = host._shell
	var deadline := Time.get_ticks_msec() + 20000
	while Time.get_ticks_msec() < deadline and not shell.online_social_status().get("available", false):
		await game._wait_seconds(0.25)
	if not shell.online_social_status().get("available", false):
		return _fail("the social service never answered")
	await game._wait_seconds(2.0)
	# the frame's MESSENGER button (its command; the movie's own button sends the same)
	shell.shell_fscommand("AptMessenger::OnMessengerBttn", "")
	await game._wait_seconds(3.0)
	if not shell.online_messenger_sync({}).get("open", false):
		return _fail("the Messenger did not open")
	# the chat panel shows the player's own code and how to add a friend; the friend's code goes into the entry, sent with ENTER
	var entry := _gadget("Messenger::ChatEntry")
	if entry.is_empty():
		return _fail("no Messenger entry")
	await _click(Vector2(float(entry.x) + float(entry.w) * 0.5, float(entry.y) + float(entry.h) * 0.5))
	await game._wait_seconds(0.5)
	for c in code:
		await _key(c)
	await game._wait_seconds(1.0)
	await _key("", KEY_ENTER)
	await game._wait_seconds(2.5)
	# the friend accepts (the helper); the FRIENDS tab lists them
	deadline = Time.get_ticks_msec() + 30000
	while Time.get_ticks_msec() < deadline and (shell.online_social_status().get("friends", []) as Array).is_empty():
		await game._wait_seconds(0.5)
	if (shell.online_social_status().get("friends", []) as Array).is_empty():
		return _fail("the friend never accepted")
	await game._wait_seconds(2.5)
	var list := _gadget("Messenger::PlayersTab_0")
	if list.is_empty() or int(list.get("entries", 0)) == 0:
		return _fail("the FRIENDS list is empty")
	await _click(Vector2(float(list.x) + float(list.w) * 0.4, float(list.y) + 10))
	await game._wait_seconds(3.0)
	if str(shell.online_messenger_sync({}).get("selected", "")).is_empty():
		return _fail("a click on the friend did not choose them")
	# a reply in the chat
	entry = _gadget("Messenger::ChatEntry")
	await _click(Vector2(float(entry.x) + float(entry.w) * 0.5, float(entry.y) + float(entry.h) * 0.5))
	for c in "Sure, see you in Open Play!":
		await _key(c)
	await game._wait_seconds(0.8)
	await _key("", KEY_ENTER)
	await game._wait_seconds(3.0)
	# the PLAYERS tab (room and recent players, ADD FRIEND / IGNORE), then FRIENDS again
	if not await _switch_tab(1, "buddyGadgets.switchBttn"):
		return false
	await game._wait_seconds(2.0)
	if not await _switch_tab(0, "buddyGadgets.instance22"):
		return false
	await game._wait_seconds(2.0)
	shell.shell_fscommand("AptMessenger::OnMessengerBttn", "")
	await game._wait_seconds(1.5)
	print("FRIENDS WALK ok")
	return true


func _gadget(name: String) -> Dictionary:
	for gadget in shell.list_gadgets():
		if str(gadget.get("name", "")) == name and not gadget.get("hidden", true):
			return gadget
	return {}


func _switch_tab(tab: int, button_suffix: String) -> bool:
	var list := _gadget("Messenger::PlayersTab_0")
	if list.is_empty():
		list = _gadget("Messenger::PlayersTab_1")
	if not list.is_empty():
		for button in shell.list_buttons(int(list.level)):
			if str(button.path).ends_with(button_suffix) and button.get("hittable", false):
				await _click(Vector2(button.x, button.y))
				await game._wait_seconds(1.0)
				if shell.online_messenger_sync({}).get("tab", -1) == tab:
					return true
	return _fail("the Messenger's tab %d did not open" % tab)


func _fail(why: String) -> bool:
	printerr("FRIENDS WALK FAIL: ", why)
	return false


func _click(stage: Vector2) -> void:
	game._mouse_event(stage, false, false)
	await game._step(6)
	game._mouse_event(stage, true, true)
	await game._step(3)
	game._mouse_event(stage, false, true)
	await game._step(6)


func _key(c: String, keycode: int = 0) -> void:
	var down := InputEventKey.new()
	down.pressed = true
	if keycode != 0:
		down.keycode = keycode
	else:
		down.unicode = c.unicode_at(0)
		down.keycode = OS.find_keycode_from_string(c.to_upper()) if c.length() == 1 else KEY_NONE
	Input.parse_input_event(down)
	await game._step(2)
	var up := down.duplicate()
	up.pressed = false
	Input.parse_input_event(up)
	await game._step(3)
