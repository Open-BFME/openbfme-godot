## SOCIAL-1: rendered privacy choices through the OPTS-4 viewport click router, not direct gadget setters.
## The caller supplies a signed-in game on the online shell and an isolated test identity.
extends RefCounted

const KEYS := ["Social.invites", "Social.challenges", "Social.requests", "Social.presence", "Social.hidden"]
const DEFAULTS := {"invites": "friends_or_room", "challenges": "friends_or_room", "requests": "anyone", "presence": "friends"}

var game: Node
var shell: Node
var screen: CanvasLayer


func run(host: Node, preserve_capture_display := false) -> bool:
	game = host
	shell = game._shell
	screen = game._display_screen
	var original: Dictionary = shell.online_social_status().get("me", {}).duplicate(true)
	if original.is_empty() or not await _open(): return _fail("signed-in privacy page did not open")
	var capture_display = screen._pending.copy()
	if not await _change_all(): return false
	if not _same_privacy(original, shell.online_social_status().me): return _fail("a staged choice reached the server before ACCEPT")
	await _bar("cancel")
	if screen.is_open() or not _same_privacy(original, shell.online_social_status().me): return _fail("CANCEL changed saved privacy")
	if not await _open() or not _same_privacy(original, screen._social_pending): return _fail("CANCEL did not restore the opening values")
	if not await _change_all(): return false
	await _bar("reset")
	if screen._social_pending.get("privacy") != DEFAULTS or screen._social_pending.get("hidden") != false:
		return _fail("RESET did not restore privacy defaults")
	if not _same_privacy(original, shell.online_social_status().me): return _fail("RESET saved before ACCEPT")
	if preserve_capture_display:
		# RESET also resets Display. Restore only capture geometry after checking its privacy defaults.
		screen._pending.window_mode = capture_display.window_mode
		screen._pending.resolution = capture_display.resolution
		screen._apply_pending()
		await game._step(90)
	if not await _change_all(): return false
	var expected: Dictionary = screen._social_pending.duplicate(true)
	game._online_shot("social-privacy-changed")
	await _bar("accept")
	var deadline := Time.get_ticks_msec() + 15000
	while Time.get_ticks_msec() < deadline and not _same_privacy(expected, shell.online_social_status().get("me", {})):
		await game.get_tree().process_frame
	if screen.is_open() or not _same_privacy(expected, shell.online_social_status().get("me", {})):
		return _fail("ACCEPT was not acknowledged by the lobby")
	if not await _open() or not _same_privacy(expected, screen._social_pending): return _fail("saved privacy did not survive reopening")
	game._online_shot("social-privacy-saved")
	await _bar("cancel")
	print("SOCIAL PRIVACY: all five rendered dropdowns, slow hover, overlap, CANCEL/RESET/ACCEPT passed")
	return true


func _same_privacy(a: Dictionary, b: Dictionary) -> bool:
	return a.get("privacy") == b.get("privacy") and a.get("hidden") == b.get("hidden")


func _open() -> bool:
	shell.shell_fscommand("AptOnline::Options", "")
	await game._step(120)
	if not screen.is_open(): return false
	var tabs: Array = shell.openbfme_page_state().get("tabs", [])
	var index: int = screen.TABS.find("Online")
	if index < 0 or index >= tabs.size(): return false
	await _click((tabs[index] as Rect2).get_center())
	return screen.current_tab() == "Online"


func _change_all() -> bool:
	for index in KEYS.size():
		var key: String = KEYS[index]
		var before: Dictionary = shell.openbfme_page_state().get("rows", {}).duplicate(true)
		if not before.has(key) or not before[key].enabled: return _fail(key + " is missing or disabled")
		var row: Rect2 = before[key].rect
		var chosen := 1 - int(before[key].value)
		await _click(Vector2(row.end.x - 12, row.get_center().y))
		var texts := []
		var open_seen := false
		for command in shell.gadget_draw_list():
			if command.lone:
				open_seen = true
				if command.kind == "text": texts.append(command)
			elif open_seen:
				return _fail(key + " is not drawn above the other rows")
		if texts.size() != 3: return _fail(key + " did not draw its current value and two choices")
		# OPTS-4 uses the text's drawn window position, then maps it back to the stage for game._mouse_event.
		var at: Vector2 = shell.window_to_stage(texts[chosen + 1].rect.position + Vector2(12, 3))
		if chosen == 1 and index + 1 < KEYS.size():
			var below: Rect2 = before[KEYS[index + 1]].rect
			if not below.has_point(at): return _fail(key + " overlap regression did not actually cover the row below")
		# Several hover frames and a held press catch failures hidden by immediate press/release calls.
		game._mouse_event(at, false, false)
		await game._step(45)
		if index == 0: game._online_shot("social-privacy-list-overlap")
		await _click(at)
		var after: Dictionary = shell.openbfme_page_state().get("rows", {})
		if int(after[key].value) != chosen: return _fail(key + " did not select the visible choice")
		for other in KEYS:
			if other != key and after[other].value != before[other].value: return _fail(key + " clicked through to " + other)
		for command in shell.gadget_draw_list():
			if command.lone: return _fail(key + " left a dropdown open after selection")
		var field := key.trim_prefix("Social.")
		var wanted: Variant = chosen == 1 if field == "hidden" else screen.SOCIAL_CHOICES[field][chosen]
		var actual: Variant = screen._social_pending.hidden if field == "hidden" else screen._social_pending.privacy[field]
		if actual != wanted: return _fail(key + " changed the gadget but not the staged privacy")
	return true


func _bar(which: String) -> void:
	await _click((shell.openbfme_page_state().buttons[which] as Rect2).get_center())
	await game._step(90)


func _click(at: Vector2) -> void:
	game._mouse_event(at, false, false)
	await game._step(3)
	game._mouse_event(at, true, true)
	await game._step(6)
	game._mouse_event(at, false, true)
	await game._step(12)


func _fail(message: String) -> bool:
	printerr("SOCIAL PRIVACY: ", message)
	return false
