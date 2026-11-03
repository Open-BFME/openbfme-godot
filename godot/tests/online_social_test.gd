## SOCIAL-1 presentation tests. No network, credentials or retail assets.
extends SceneTree
## GDScript assert() only logs a SCRIPT ERROR and the run carries on, so a failed one still ended in PASS / exit 0. Every check goes through
## _check: a failure is recorded and printed as FAIL, and _finish quits 1 when any failed.
var _failures: Array[String] = []

func _check(ok: bool, reason := "") -> bool:
	if not ok:
		_failures.append(reason)
		var stack := get_stack()
		printerr("FAIL: ", reason, " (line ", stack[1].line if stack.size() > 1 else 0, ")")
	return ok

func _finish(pass_message: String) -> void:
	if _failures.is_empty():
		print(pass_message)
		quit()
		return
	printerr(_failures.size(), " check(s) failed")
	quit(1)

const Social := preload("res://scripts/online_social.gd")
const Options := preload("res://scripts/openbfme_options_screen.gd")
const Display := preload("res://scripts/display_settings.gd")
const ME := "1111111111111111"
const FRIEND := "2222222222222222"
const PEER := "3333333333333333"
const STRANGER := "8888888888888888"

class ShellStub extends Node2D:
	var calls: Array = []
	var social: Dictionary = {}
	var refusal := false
	var online: Dictionary = {"signed_in": true, "in_game": false, "session_token": "test-only", "game_id": "4444444444444444"}
	func online_social_status() -> Dictionary: return social
	func online_social_apply(spec: Dictionary) -> Dictionary:
		calls.append(spec.duplicate(true))
		return {"ok": not refusal, "error": "social_unavailable" if refusal else ""}
	func online_status() -> Dictionary: return online
	func online_social_events() -> Array: return []
	func shell_stack() -> PackedStringArray: return ["OnlineShell.apt"]
	# the Messenger: closed unless a test opens it; open, it lists `messenger_ids`, keeps a selection a sync's "select" can move, and hands out
	# `messenger_actions` once (its buttons and ENTER); `last_spec` is what the script sent last (rows, chat, clear_entry_if)
	var messenger_open := false
	var messenger_ids: Array = []
	var messenger_selected := ""
	var messenger_actions: Array = []
	var last_spec: Dictionary = {}
	var rows_sent: Array = []
	var chat_sent: Array = []
	func online_messenger_sync(spec: Dictionary) -> Dictionary:
		last_spec = spec.duplicate(true)
		if spec.has("rows"): rows_sent = spec.rows
		if spec.has("chat"): chat_sent = spec.chat[0]
		if not messenger_open: return {"open": false, "actions": []}
		if spec.has("select") and messenger_ids.has(spec.select): messenger_selected = spec.select
		var actions := messenger_actions
		messenger_actions = []
		return {"open": true, "selected": messenger_selected, "actions": actions}
	# GuiFX's Yes / No box (AptOnlineShell::askYesNo): one at a time; `answers` are handed out once
	var box_busy := false
	var asked: Array = []
	var cancelled: Array = []
	var answers: Array = []
	func online_ask(key: String, title: String, text: String) -> bool:
		if box_busy: return false
		asked.append({"key": key, "title": title, "text": text})
		box_busy = true
		return true
	func online_answers() -> PackedStringArray:
		var out := PackedStringArray(answers)
		answers = []
		return out
	var displaced := false # another box took the question's place (the room closed under the player)
	func online_asking(key: String) -> bool: return box_busy and not displaced and not asked.is_empty() and asked.back().key == key
	func online_cancel_ask(key: String) -> void:
		cancelled.append(key)
		if online_asking(key): box_busy = false
	func answer(yes: bool) -> void:
		answers.append("%s|%s" % [asked.back().key, "yes" if yes else "no"])
		box_busy = false
	func shell_fscommand(_command: String, _arg: String) -> bool: return true
	func get_options() -> Dictionary: return {"OpenBFMEUberPerformance": "no"}
	func set_openbfme_available(_available: bool) -> void: pass
	func openbfme_page_open() -> Dictionary: return {"ok": true}
	func openbfme_page_state() -> Dictionary: return {"menu": false}
	func openbfme_page_show(_spec: Dictionary) -> Dictionary: return {"ok": true}
	func openbfme_page_set_row(_row: Dictionary) -> void: pass

class ReportStub extends Node:
	var context: Array = []
	func open_target(game: String, target: String, nick: String, token: String) -> void: context = [game, target, nick, token]


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var me := {"pid": ME, "nick": "Me", "code": "0123456789"}
	var friend := {"pid": FRIEND, "nick": "Friend", "code": "123456789A"}
	var peer := {"pid": PEER, "nick": "Peer", "code": "23456789AB", "target": "roster-peer"}
	var data := {
		"me": me, "friends": [friend], "presence": [{"pid": FRIEND, "state": "away"}],
		"requests": [{"id": "5555555555555555", "from": peer, "to": me, "at": 1}, {"id": "6666666666666666", "from": me, "to": friend, "at": 1}],
		"room_players": [{"pid": PEER, "nick": "Peer", "blocked": false, "target": "roster-peer"}],
		"recent": [{"game": "4444444444444444", "at": 1, "players": [peer]}], "conversations": {}, "chat": [], "seq": "aaaaaaaaaaaaaaaa:1",
		"invites": [{"id": "7777777777777777", "from": friend, "to": me, "game": "4444444444444444", "kind": "challenge", "mode": "normal", "note": "", "expires": int(Time.get_unix_time_from_system()) + 200}],
	}
	_stale_invite_tests(data)
	var rows: Dictionary = Social.messenger_rows(data)
	_check(rows.friends.size() == 2 and rows.friends[0].status == "away")
	_check(rows.friends[1].id == PEER and rows.friends[1].request)
	_check(rows.players.size() == 1 and rows.players[0].id == PEER and not rows.players[0].friendable, "a pending request: no ADD FRIEND")
	data.room_players[0].blocked = true
	# Recent players carry no block flag; the relationship filter must still hide a known block (shown as an ignored row: UN-IGNORE)
	data.blocks = [{"pid": PEER, "nick": "Peer", "note": "private note"}]
	rows = Social.messenger_rows(data)
	_check(rows.players.size() == 1 and rows.players[0].blocked and rows.players[0].status == "Ignored" and rows.friends.size() == 1)
	data.blocks = []
	data.room_players[0].blocked = false
	var shell := ShellStub.new()
	shell.social = data
	root.add_child(shell)
	var reports := ReportStub.new()
	root.add_child(reports)
	var panel := Social.new()
	panel.shell = shell
	panel.reports = reports
	root.add_child(panel)
	panel.set_process(false)
	shell.social.available = false
	panel._process(0.0)
	_check(shell.calls.is_empty() and not panel._started, "a restored token must not fetch social data before session validation")
	_check(panel._notices.is_empty(), "waiting for social startup must not show a refusal")
	# ONLINEMENU-1 (BATCH-P3): a lobby without the social service (unsupported): the Messenger says so, lists nobody, acts on nothing, asks
	# nothing of the lobby, and the corner shows nothing
	shell.social.unsupported = true
	shell.messenger_open = true
	shell.messenger_actions = [{"kind": "add_friend", "pid": PEER}, {"kind": "send", "pid": "", "text": "0123-4567-89"}]
	panel._process(0.0)
	_check(shell.chat_sent == [Social.UNSUPPORTED_TEXT] and shell.rows_sent == [[], []], str(shell.last_spec))
	_check(shell.calls.is_empty() and not panel._started and panel._notices.is_empty() and panel.corner_line() == "")
	panel._process(0.0)
	_check(shell.calls.is_empty())
	shell.social.erase("unsupported")
	shell.messenger_open = false
	shell.messenger_actions = []
	shell.rows_sent = []
	shell.chat_sent = []
	panel._rows_key = ""
	panel._chat_key = ""
	_check(panel.corner_line().is_empty())
	shell.social.available = true
	panel._process(0.0)
	_check(shell.calls.map(func(call): return call.action) == ["fetch_recent", "fetch_blocks", "fetch_threads"])
	panel._process(0.0)
	_check(shell.calls.size() == 3, "initial social reads must only start once")
	# what waited at the sign-in: the request is a Messenger row (counted in the corner); the challenge is asked in the box (it has no row)
	_check(panel.corner_line() == "Messenger: 1 friend request, 1 invite", panel.corner_line())
	_check(shell.asked.size() == 1 and shell.asked[0].title == "Challenge", str(shell.asked))
	# Sol (delta review): another box takes the question's place (the room closed under the player): it is asked again later, never acted on
	shell.displaced = true
	var calls_before := shell.calls.size()
	panel._process(0.0)
	_check(shell.calls.size() == calls_before and panel._asking.is_empty() and panel._asks.size() == 1, "a displaced question waits, unanswered")
	shell.displaced = false
	shell.box_busy = false # that other box closed
	panel._process(0.0)
	_check(shell.asked.size() == 2 and shell.asked[1].title == "Challenge", "asked again")
	shell.answer(false)
	panel._process(0.0)
	_check(shell.calls[-1] == {"action": "decline_invite", "id": "7777777777777777"})
	shell.social.invites.clear() # declined: the service drops it
	shell.asked.clear()
	# the Messenger's rows: the native fields (blocked, friendable) go with each row
	_check(shell.rows_sent.size() == 2 and shell.rows_sent[1][0].has("friendable") and shell.rows_sent[1][0].has("blocked"))
	# nobody chosen: the chat panel says the friend code and how to add a friend
	_check(shell.chat_sent == ["Your friend code: 0123-4567-89", "To add a friend, type their code below and press ENTER.", "Select Buddy to chat."], str(shell.chat_sent))
	shell.calls.clear()
	panel._data = data
	panel._select_player(PEER)
	_check(shell.calls.is_empty(), "selecting a new room peer must not request nonexistent friend chat")
	panel._select_player(FRIEND)
	_check(shell.calls[-1].action == "fetch_messages" and shell.calls[-1].pid == FRIEND)
	panel._data.threads = [{"pid": PEER}]
	panel._select_player(PEER)
	_check(shell.calls[-1].action == "fetch_messages" and shell.calls[-1].pid == PEER, "past friendships retain chat")
	panel._message(FRIEND, "Hello")
	panel._message(FRIEND, "Hello")
	_check(shell.calls[-1].client_id == shell.calls[-2].client_id)
	panel._message(FRIEND, "Changed")
	_check(shell.calls[-1].client_id != shell.calls[-2].client_id)
	panel._data.threads = []
	panel._select_player("")
	# ---- the Messenger's buttons: ACCEPT FRIEND and (on a request row) REMOVE declines ----
	shell.messenger_open = true
	shell.messenger_ids = [FRIEND, PEER]
	var act := func(action: Dictionary) -> void:
		shell.messenger_actions.append(action)
		panel._process(0.0)
	shell.calls.clear()
	act.call({"kind": "accept_friend", "pid": PEER, "text": ""})
	_check(shell.calls[-1] == {"action": "accept_friend", "id": "5555555555555555"})
	_check(panel.corner_line() == "You and Peer are now friends.", panel.corner_line())
	act.call({"kind": "remove_friend", "pid": PEER, "text": ""})
	_check(shell.calls[-1] == {"action": "decline_friend", "id": "5555555555555555"}, "REMOVE on a request row declines it")
	# ---- a friend code typed into the entry and sent with ENTER asks that player, whoever is chosen; the entry is cleared ----
	act.call({"kind": "send", "pid": "", "text": "abcd-efgh-jk"})
	_check(shell.calls[-1] == {"action": "request_friend_code", "code": "ABCDEFGHJK"})
	panel._process(0.0)
	_check(shell.last_spec.get("clear_entry_if", "") == "abcd-efgh-jk" or panel._acknowledged_text.is_empty())
	act.call({"kind": "send", "pid": FRIEND, "text": "ABCDEFGHJK"})
	_check(shell.calls[-1].action == "request_friend_code", "a code is a friend request even with a friend chosen")
	var before := shell.calls.size()
	act.call({"kind": "send", "pid": "", "text": "0123-4567-89"})
	_check(shell.calls.size() == before and panel._notices.back().contains("your own friend code"))
	act.call({"kind": "send", "pid": "", "text": "hello there"})
	_check(shell.calls.size() == before and panel._notices.back().begins_with("Select a friend"), "text with nobody chosen goes nowhere, and says so")
	act.call({"kind": "send", "pid": FRIEND, "text": "hello there"})
	_check(shell.calls[-1].action == "send_message" and shell.calls[-1].pid == FRIEND and shell.calls[-1].text == "hello there")
	# ---- REMOVE FRIEND and IGNORE ask in GuiFX's box first; No does nothing, Yes acts ----
	before = shell.calls.size()
	act.call({"kind": "remove_friend", "pid": FRIEND, "text": ""})
	_check(shell.asked.size() == 1 and shell.asked[0].title == "Remove friend" and shell.asked[0].text.contains("Remove Friend"), str(shell.asked))
	shell.answer(false)
	panel._process(0.0)
	_check(shell.calls.size() == before, "No keeps the friend")
	act.call({"kind": "block", "pid": PEER, "text": ""})
	_check(shell.asked.size() == 2 and shell.asked[1].title == "Ignore")
	shell.answer(true)
	panel._process(0.0)
	_check(shell.calls[-1] == {"action": "block_player", "pid": PEER, "note": ""})
	act.call({"kind": "unblock", "pid": PEER, "text": ""})
	_check(shell.calls[-1] == {"action": "unblock_player", "pid": PEER})
	act.call({"kind": "add_friend", "pid": PEER, "text": ""})
	_check(shell.calls[-1] == {"action": "request_friend", "pid": PEER})
	act.call({"kind": "invite", "pid": FRIEND, "text": ""})
	_check(shell.calls[-1] == {"action": "send_invite", "pid": FRIEND, "kind": "invite"})
	# ---- a new invite: RotWK's "Friend Game Invite" box; in a room, Yes asks to leave first, then leaves and accepts; No declines ----
	shell.online.in_game = true
	shell.social.invites.append({"id": "9999999999999999", "from": friend, "to": me, "game": "4444444444444444", "kind": "invite", "note": "", "expires": int(Time.get_unix_time_from_system()) + 200})
	panel._process(0.0)
	_check(shell.asked.size() == 3 and shell.asked[2].title == "Friend Game Invite" and shell.asked[2].text.begins_with("Friend has invited you"), str(shell.asked[-1]))
	shell.answer(true)
	panel._process(0.0)
	_check(shell.asked.size() == 4 and shell.asked[3].title == "Leave room?")
	shell.answer(true)
	panel._process(0.0)
	_check(shell.calls[-1] == {"action": "leave_accept_invite", "id": "9999999999999999"})
	# an invite withdrawn while "Leave room?" is up: YES keeps the player in the room (Sol delta2 follow-up)
	shell.social.invites.append({"id": "eeeeeeeeeeeeeeef", "from": friend, "to": me, "game": "4444444444444444", "kind": "invite", "note": "", "expires": int(Time.get_unix_time_from_system()) + 200})
	panel._process(0.0)
	shell.answer(true)
	panel._process(0.0)
	_check(shell.asked[-1].title == "Leave room?")
	shell.social.invites = shell.social.invites.filter(func(i): return i.id != "eeeeeeeeeeeeeeef")
	before = shell.calls.size()
	shell.answer(true)
	panel._process(0.0)
	_check(shell.calls.size() == before, "a withdrawn invite is never accepted: %s" % str(shell.calls.slice(before)))
	shell.online.in_game = false
	shell.social.invites.append({"id": "aaaaaaaaaaaaaaab", "from": friend, "to": me, "game": "4444444444444444", "kind": "challenge", "mode": "uber", "note": "", "expires": int(Time.get_unix_time_from_system()) + 200})
	panel._process(0.0)
	_check(shell.asked[-1].title == "Challenge" and shell.asked[-1].text.contains("(Uber)"))
	shell.answer(false)
	panel._process(0.0)
	_check(shell.calls[-1] == {"action": "decline_invite", "id": "aaaaaaaaaaaaaaab"})
	# one box at a time: two new invites, the second waits for the first's answer
	shell.social.invites.append({"id": "bbbbbbbbbbbbbbbc", "from": friend, "to": me, "game": "4444444444444444", "kind": "invite", "note": "", "expires": int(Time.get_unix_time_from_system()) + 200})
	shell.social.invites.append({"id": "cccccccccccccccd", "from": peer, "to": me, "game": "4444444444444444", "kind": "invite", "note": "", "expires": int(Time.get_unix_time_from_system()) + 200})
	var asked_before := shell.asked.size()
	panel._process(0.0)
	panel._process(0.0)
	_check(shell.asked.size() == asked_before + 1, "the second question waits while the box is up")
	shell.answer(false)
	panel._process(0.0)
	_check(shell.asked.size() == asked_before + 2 and shell.asked[-1].text.begins_with("Peer"))
	shell.answer(false)
	panel._process(0.0)
	# a new friend request: RotWK's notification line in the corner
	shell.social.requests.append({"id": "dddddddddddddddd", "from": {"pid": STRANGER, "nick": "Gimli", "code": "3456789ABC"}, "to": me, "at": 2})
	panel._process(0.0)
	_check(panel.corner_line() == "Gimli has requested to be put on your friends list.", panel.corner_line())
	shell.messenger_open = false
	panel._process(0.0)
	var defaults := {"invites": "friends_or_room", "challenges": "friends_or_room", "requests": "anyone", "presence": "friends"}
	shell.social.available = true
	shell.social.me.privacy = defaults.duplicate()
	shell.social.me.hidden = false
	shell.calls.clear()
	var options := Options.new()
	options.shell = shell
	options.settings = Display.new()
	root.add_child(options)
	options.set_process(false)
	options.open()
	options._show_tab("Online")
	options._on_value("Social.requests", 1)
	options._on_value("Social.hidden", 1)
	_check(options._social_pending.privacy.requests == "nobody")
	_check(shell.calls.is_empty(), "privacy changes must stay staged before ACCEPT")
	options.close(false)
	_check(shell.calls.is_empty(), "CANCEL must not send privacy mutations")
	options.open()
	_check(options._social_pending.privacy == defaults and not options._social_pending.hidden)
	options._show_tab("Online")
	options._on_value("Social.requests", 1)
	options._on_value("Social.hidden", 1)
	options.close(true)
	_check(shell.calls.size() == 2)
	_check(shell.calls[0].action == "set_privacy" and shell.calls[0].privacy.requests == "nobody")
	_check(shell.calls[1] == {"action": "set_presence", "state": "online", "hidden": true})
	options.open()
	options._show_tab("Online")
	options._on_value("Social.requests", 1)
	options._on_value("Social.hidden", 1)
	options.reset_defaults()
	_check(options._social_pending.privacy == defaults and not options._social_pending.hidden)
	options._on_value("Social.requests", 1)
	shell.refusal = true
	options.close(true)
	_check(options.is_open() and options._social_save_error.ends_with("social_unavailable"))
	options.close(false)
	options.queue_free()
	panel.queue_free()
	shell.queue_free()
	reports.queue_free()
	await process_frame
	_finish("SOCIAL UI PASS: Messenger rows (requests, ignored players, ADD FRIEND greyed), accept / decline, friend code in the entry, remove and ignore asked in GuiFX's box, invites and challenges in RotWK's invite box one at a time, corner line, retry IDs, privacy staging")


func _stale_invite_tests(data: Dictionary) -> void:
	var cases := ["withdrawn", "room closed", "expired", "yes race", "no race", "leave room", "unrelated box", "queued", "sign out"]
	for reason in cases:
		var shell := ShellStub.new()
		shell.social = data.duplicate(true)
		shell.social.available = true
		shell.social.requests = []
		root.add_child(shell)
		var panel := Social.new()
		panel.shell = shell
		root.add_child(panel)
		panel.set_process(false)
		panel._process(0.0)
		_check(shell.box_busy and shell.asked.size() == 1, reason)
		var key: String = shell.asked.back().key
		var id: String = shell.social.invites[0].id
		if reason == "leave room":
			shell.online.in_game = true
			shell.answer(true)
			panel._process(0.0)
			_check(shell.asked.back().title == "Leave room?")
			key = shell.asked.back().key
		if reason == "queued":
			var queued: Dictionary = shell.social.invites[0].duplicate(true)
			queued.id = "aaaaaaaaaaaaaaa1"
			shell.social.invites.append(queued)
			panel._process(0.0)
			_check(panel._asks.size() == 1)
		if reason == "yes race" or reason == "no race": shell.answer(reason == "yes race")
		if reason == "unrelated box": shell.displaced = true
		if reason == "expired":
			shell.social.invites[0].expires = int(Time.get_unix_time_from_system()) - 1
		elif reason == "sign out":
			shell.online.signed_in = false
		else:
			shell.social.invites = []
		var before := shell.calls.size()
		panel._process(0.0)
		_check(shell.calls.size() == before, reason + ": no accept, decline or leave request")
		_check(shell.cancelled == [key] and panel._asking.is_empty(), reason)
		_check(shell.box_busy == (reason == "unrelated box"), reason + ": only the invite box closes")
		_check(panel._asks.is_empty(), reason + ": no obsolete queued question")
		# A late button event cannot act on the retired key.
		shell.answers.append(key + "|yes")
		panel._service_asks()
		_check(shell.calls.size() == before, reason + ": late Yes ignored")
		if reason != "sign out":
			panel._accept_invite(id, "Friend")
			panel._leave_accept_invite(id, "Friend")
			_check(shell.calls.size() == before, reason + ": both stale acceptance guards")
		panel.free()
		shell.free()
	print("STALE INVITE PASS: %d cases" % cases.size())
