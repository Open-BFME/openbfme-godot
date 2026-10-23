## The real-client S-B social contract walk.
##
## This deliberately drives the native client and its Godot HTTP transport. The
## peer below is another HTTP client, not a store fixture or a response
## injector.
extends "res://scripts/game.gd"

const WAIT_LOGIN := 30.0
const WAIT_PHASE := 20.0
const WAIT_ROOM := 35.0
const PEER_MODE := "normal"
const PEER_PLATFORM := "linux"

var _phase := ""
var _peer_token := ""
var _peer_pid := ""
var _peer_code := ""
var _main_token := ""
var _main_pid := ""
var _profile := ""
var _source_game := ""
var _challenge_game := ""
var _reverse_game := ""
var _challenge_invite := ""
var _reverse_invite := ""
var _message_client := ""
var _message_id := 0
var _reply_id := 0
var _reply_client := ""
var _reply_text := ""
var _initial_social_seq := ""
var _reports_received: Array = []


func _online_walk_run() -> void:
	if _online_walk != "social-node":
		_online_walk_end(false)
		return
	var ok := await _run_social_node_walk()
	_online_walk_end(ok)


func _run_social_node_walk() -> bool:
	_phase = "native sign-in"
	while not _menu_revealed:
		await get_tree().process_frame
		if not is_inside_tree():
			return false
	if not _shell.shell_fscommand("AptMainMenu::OnlineButtonPressed", ""):
		return _phase_fail()
	if not await _wait_status("the sign-in page", func(state: Dictionary): return state.get("page", "") == "OnlineLogin", WAIT_LOGIN):
		return false
	if not await _wait_login_gadget():
		return false
	var name_result: Dictionary = _shell.online_apply({"nickname": "Social node host"})
	if not name_result.get("ok", false):
		return _phase_fail()
	var login_result: Dictionary = _shell.online_apply({"command": "AptOnline::Login::Login"})
	if not login_result.get("ok", false):
		return _phase_fail()
	if not await _wait_status("the signed-in home page", func(state: Dictionary):
			return state.get("signed_in", false) and state.get("page", "") == "OnlineHome", WAIT_LOGIN):
		return false
	_main_token = str(_shell.online_status().get("session_token", ""))
	if _main_token.is_empty():
		return _phase_fail()
	_profile = _world.net_profile_digest()
	if not _valid_hex(_profile, 32):
		return _phase_fail()
	if _online_social == null:
		return _phase_fail()
	# The UI process consumes the same event queue this walk asserts. Leave the
	# transport and native scheduler running; only stop that consumer.
	_online_social.set_process(false)
	if not await _wait_social("complete native social inbox", func(social: Dictionary):
			return _social_ready(social), WAIT_PHASE):
		return false
	var initial_social: Dictionary = _shell.online_social_status()
	_main_pid = str(initial_social.get("me", {}).get("pid", ""))
	_initial_social_seq = _social_seq(initial_social)

	if not await _create_peer():
		return false
	if not await _friend_peer():
		return false
	if not await _chat_peer():
		return false
	if not await _privacy_and_blocks():
		return false
	if not await _ordinary_room_and_report():
		return false
	if not await _challenge_and_reverse_invite():
		return false
	if not await _recovery_event():
		return false
	print("SOCIAL-NODE PASS phases=7")
	return true


func _wait_login_gadget() -> bool:
	var until := Time.get_ticks_msec() + int(WAIT_LOGIN * 1000.0)
	while Time.get_ticks_msec() < until:
		for gadget in _shell.list_gadgets():
			if gadget.get("name", "") == "OnlineLogin::Nickname" and not gadget.get("hidden", true):
				return true
		await get_tree().process_frame
	return _phase_fail()


func _wait_status(what: String, condition: Callable, seconds: float) -> bool:
	var until := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		var state: Dictionary = _shell.online_status()
		if condition.call(state):
			return true
		await get_tree().process_frame
	return _phase_fail_named(what)


func _wait_social(what: String, condition: Callable, seconds: float) -> bool:
	var until := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		var state: Dictionary = _shell.online_social_status()
		if condition.call(state):
			return true
		await get_tree().process_frame
	return _phase_fail_named(what)


func _social_ready(social: Dictionary) -> bool:
	var me: Dictionary = social.get("me", {})
	if not social.get("available", false) or not _valid_hex(str(me.get("pid", "")), 16):
		return false
	if _social_seq(social).is_empty():
		return false
	for key in ["friends", "requests", "invites", "notices", "blocks", "threads", "chat_heads", "conversations"]:
		var value: Variant = social.get(key)
		if not (value is Array or value is Dictionary):
			return false
	return true


func _social_seq(social: Dictionary) -> String:
	return str(social.get("seq", ""))


func _drain_social_events() -> void:
	_shell.online_social_events()


func _wait_native_ack(what: String, request: String, event_condition: Callable, state_condition: Callable, seconds: float = WAIT_PHASE) -> bool:
	var acknowledged := false
	var until := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		for event in _shell.online_social_events():
			var event_kind := str(event.get("kind", ""))
			var event_request := str(event.get("request", "")).trim_prefix("social_")
			if event_kind == "error" and event_request == request:
				printerr("SOCIAL-NODE FAIL phase=%s request=%s code=%s" % [_phase, request, str(event.get("code", ""))])
				return false
			if event_condition.call(event):
				acknowledged = true
		if acknowledged and state_condition.call(_shell.online_social_status()):
			return true
		await get_tree().process_frame
	return _phase_fail_named(what)


func _wait_native_refusal(what: String, request: String, expected_code: String, seconds: float = WAIT_PHASE) -> bool:
	var until := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		for event in _shell.online_social_events():
			if str(event.get("kind", "")) != "error":
				continue
			if str(event.get("request", "")).trim_prefix("social_") != request:
				continue
			if str(event.get("code", "")) == expected_code:
				return true
			printerr("SOCIAL-NODE FAIL phase=%s request=%s code=%s" % [_phase, request, str(event.get("code", ""))])
			return false
		await get_tree().process_frame
	return _phase_fail_named(what)


func _wait_condition(what: String, condition: Callable, seconds: float) -> bool:
	var until := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		if condition.call():
			return true
		await get_tree().process_frame
	return _phase_fail_named(what)


func _phase_fail_named(what: String) -> bool:
	printerr("SOCIAL-NODE FAIL phase=%s wait=%s" % [_phase, what])
	return false


func _phase_fail() -> bool:
	var frames: Array = get_stack()
	var caller: Dictionary = {}
	if frames.size() > 1 and frames[frames.size() - 2] is Dictionary:
		caller = frames[frames.size() - 2]
	printerr("SOCIAL-NODE FAIL phase=%s at=%s" % [_phase, str(caller.get("line", "?"))])
	return false


func _valid_hex(value: String, length: int) -> bool:
	if value.length() != length:
		return false
	return RegEx.create_from_string("^[0-9a-f]{%d}$" % length).search(value) != null


func _new_id() -> String:
	return Crypto.new().generate_random_bytes(16).hex_encode()


func _native_action(spec: Dictionary) -> bool:
	var result: Dictionary = _shell.online_social_apply(spec)
	return bool(result.get("ok", false))


func _create_peer() -> bool:
	_phase = "peer guest"
	var install := _new_id()
	if not _valid_hex(install, 32):
		return _phase_fail()
	var response := await _peer_request(HTTPClient.METHOD_POST, "/v1/auth/guest", "", {
		"name": "Social node peer",
		"install": install,
		"client": {"platform": "linux", "version": "social-node-test"},
	})
	if not response.get("ok", false):
		return _phase_fail()
	var body: Dictionary = response.get("body", {})
	_peer_token = str(body.get("token", ""))
	var account: Dictionary = body.get("account", {})
	_peer_pid = str(account.get("pid", ""))
	if _peer_token.is_empty() or not _valid_hex(_peer_pid, 16):
		return _phase_fail()
	var me: Dictionary = await _peer_request(HTTPClient.METHOD_GET, "/v1/me", _peer_token)
	if not me.get("ok", false):
		return _phase_fail()
	_peer_code = str(me.get("body", {}).get("code", ""))
	if _peer_code.is_empty():
		return _phase_fail()
	return true


func _friend_peer() -> bool:
	_phase = "friend request and acceptance"
	_drain_social_events()
	if not _native_action({"action": "request_friend_code", "code": _peer_code}):
		return _phase_fail()
	var request_id := ""
	var until := Time.get_ticks_msec() + int(WAIT_PHASE * 1000.0)
	while Time.get_ticks_msec() < until:
		var inbox := await _peer_request(HTTPClient.METHOD_GET, "/v1/inbox", _peer_token)
		if inbox.get("ok", false):
			for item in inbox.get("body", {}).get("requests", []):
				var from: Dictionary = item.get("from", {})
				var to: Dictionary = item.get("to", {})
				if str(from.get("pid", "")) == _main_pid and str(to.get("pid", "")) == _peer_pid:
					request_id = str(item.get("id", ""))
					break
		if not request_id.is_empty():
			break
		await _wait_seconds(0.15)
	if request_id.is_empty():
		return _phase_fail_named("peer friend request")
	var accepted := await _peer_request(HTTPClient.METHOD_POST, "/v1/friends/requests/%s/accept" % request_id, _peer_token, {"v": 1})
	if not accepted.get("ok", false):
		return _phase_fail()
	if not await _wait_social("native friend replacement inbox", func(social: Dictionary):
			if _social_seq(social) == _initial_social_seq:
				return false
			for friend in social.get("friends", []):
				if str(friend.get("pid", "")) == _peer_pid:
					for request in social.get("requests", []):
						if str(request.get("from", {}).get("pid", "")) == _peer_pid or str(request.get("to", {}).get("pid", "")) == _peer_pid:
							return false
					return true
			return false, WAIT_PHASE):
		return false
	return true


func _chat_peer() -> bool:
	_phase = "native chat idempotency and read state"
	_message_client = _new_id()
	var message_text := "native social node message"
	_drain_social_events()
	if not _native_action({"action": "send_message", "pid": _peer_pid, "client_id": _message_client, "text": message_text}):
		return _phase_fail()
	if not await _wait_native_ack("first native message acknowledgement", "send_message",
		func(event: Dictionary):
			return event.get("kind", "") == "chat" and str(event.get("pid", "")) == _peer_pid and \
				str(event.get("id", "")) == _message_client and str(event.get("message", "")) == message_text,
			func(social: Dictionary):
				return _conversation(social).get("messages", []).any(func(message: Dictionary):
					return _same_message(message, _message_client, message_text, _main_pid, _peer_pid))):
		return false
	_drain_social_events()
	if not _native_action({"action": "send_message", "pid": _peer_pid, "client_id": _message_client, "text": message_text}):
		return _phase_fail()
	if not await _wait_native_ack("idempotent message replay acknowledgement", "send_message",
		func(event: Dictionary):
			return event.get("kind", "") == "chat" and str(event.get("pid", "")) == _peer_pid and \
				str(event.get("id", "")) == _message_client and str(event.get("message", "")) == message_text,
			func(social: Dictionary):
				var messages: Array = _conversation(social).get("messages", [])
				return messages.size() == 1 and _same_message(messages[0], _message_client, message_text, _main_pid, _peer_pid)):
		return false
	if not await _wait_message(_message_client, message_text, 1):
		return false
	var conversation: Dictionary = _conversation()
	if conversation.get("messages", []).size() != 1:
		return _phase_fail_named("one durable idempotent message")
	_message_id = int(conversation.get("messages", [])[0].get("id", 0))
	if _message_id <= 0:
		return _phase_fail()
	var peer_history: Dictionary = await _peer_request(HTTPClient.METHOD_GET, "/v1/chat/%s/messages?after=0&limit=100" % _main_pid, _peer_token)
	if not peer_history.get("ok", false):
		return _phase_fail()
	var peer_messages: Array = peer_history.get("body", {}).get("messages", [])
	if peer_messages.size() != 1 or not _same_message(peer_messages[0], _message_client, message_text, _main_pid, _peer_pid):
		return _phase_fail_named("server chat history")
	if not _same_message(conversation.get("messages", [])[0], _message_client, message_text, _main_pid, _peer_pid):
		return _phase_fail_named("native chat history")

	_reply_client = _new_id()
	_reply_text = "peer held-inbox reply"
	var reply: Dictionary = await _peer_request(HTTPClient.METHOD_POST, "/v1/chat/%s/messages" % _main_pid, _peer_token, {
		"v": 1, "client_id": _reply_client, "text": _reply_text,
	})
	if not reply.get("ok", false):
		return _phase_fail()
	_reply_id = int(reply.get("body", {}).get("message", {}).get("id", 0))
	if _reply_id <= 0:
		return _phase_fail()
	# Keep ordinary page work moving while the social held inbox can wake.
	await _online_click("Toggles.ButtonNav.OpenPlay")
	if not await _wait_status("ordinary Open Play request", func(state: Dictionary):
			return state.get("page", "") == "OnlineOpenPlay", WAIT_PHASE):
		return false
	_drain_social_events()
	if not _native_action({"action": "fetch_messages", "pid": _peer_pid, "after": 0}):
		return _phase_fail()
	if not await _wait_native_ack("peer reply fetch acknowledgement", "messages",
			func(event: Dictionary): return event.get("kind", "") == "chat" and str(event.get("pid", "")) == _peer_pid,
			func(social: Dictionary):
				var messages: Array = _conversation(social).get("messages", [])
				return messages.size() == 2 and messages.any(func(message: Dictionary):
					return _same_message(message, _reply_client, _reply_text, _peer_pid, _main_pid))):
		return false
	if not await _wait_message(_reply_client, _reply_text, 1):
		return false
	if _conversation().get("messages", []).size() != 2:
		return _phase_fail_named("both native chat messages")
	_drain_social_events()
	if not _native_action({"action": "mark_read", "pid": _peer_pid, "through": _reply_id}):
		return _phase_fail()
	if not await _wait_native_ack("native read marker acknowledgement", "mark_read",
			func(event: Dictionary): return event.get("kind", "") == "changed",
			func(social: Dictionary): return int(_conversation(social).get("read_id", 0)) >= _reply_id):
		return false
	if not await _server_history_valid("server history and read marker"):
		return false
	return true


func _same_message(message: Dictionary, client_id: String, text: String, from: String, to: String) -> bool:
	return str(message.get("client_id", "")) == client_id and str(message.get("text", "")) == text and \
		str(message.get("from", "")) == from and str(message.get("to", "")) == to and int(message.get("id", 0)) > 0


func _conversation(social: Dictionary = {}) -> Dictionary:
	if social.is_empty():
		social = _shell.online_social_status()
	return social.get("conversations", {}).get(_peer_pid, {})


func _server_history_valid(what: String) -> bool:
	var history: Dictionary = await _peer_request(HTTPClient.METHOD_GET,
		"/v1/chat/%s/messages?after=0&limit=100" % _peer_pid, _main_token)
	if not history.get("ok", false):
		return _phase_fail_named(what)
	var body: Dictionary = history.get("body", {})
	var messages: Array = body.get("messages", [])
	if messages.size() != 2 or not _same_message(messages[0], _message_client,
			"native social node message", _main_pid, _peer_pid):
		return _phase_fail_named(what)
	if not _same_message(messages[1], _reply_client, _reply_text, _peer_pid, _main_pid):
		return _phase_fail_named(what)
	if int(body.get("read_id", 0)) < _reply_id or int(body.get("last_id", 0)) != _reply_id:
		return _phase_fail_named(what)
	return true


func _wait_message(client_id: String, text: String, expected_count: int) -> bool:
	var latest_total := 0
	var latest_matches := 0
	var until := Time.get_ticks_msec() + int(WAIT_PHASE * 1000.0)
	while Time.get_ticks_msec() < until:
		var messages: Array = _conversation().get("messages", [])
		latest_total = messages.size()
		latest_matches = 0
		for message in messages:
			if str(message.get("client_id", "")) == client_id and str(message.get("text", "")) == text:
				latest_matches += 1
		if latest_matches == expected_count and latest_total >= expected_count:
			return true
		await get_tree().process_frame
	printerr("SOCIAL-NODE FAIL phase=%s wait=native message state messages=%d matching=%d" % [_phase, latest_total, latest_matches])
	return false


func _privacy_and_blocks() -> bool:
	_phase = "privacy, hidden presence, block, and history"
	var privacy := {"invites": "friends_or_room", "challenges": "friends_or_room", "requests": "nobody", "presence": "nobody"}
	_drain_social_events()
	if not _native_action({"action": "set_privacy", "privacy": privacy}):
		return _phase_fail()
	if not await _wait_native_ack("native privacy replacement acknowledgement", "privacy",
			func(event: Dictionary): return event.get("kind", "") == "changed",
			func(social: Dictionary):
			return social.get("me", {}).get("privacy", {}) == privacy, WAIT_PHASE):
		return false
	var main_me: Dictionary = await _peer_request(HTTPClient.METHOD_GET, "/v1/me", _main_token)
	if not main_me.get("ok", false) or main_me.get("body", {}).get("privacy", {}) != privacy:
		return _phase_fail_named("server privacy replacement")
	_drain_social_events()
	if not _native_action({"action": "set_presence", "state": "online", "hidden": true}):
		return _phase_fail()
	if not await _wait_native_ack("native hidden presence acknowledgement", "presence",
			func(event: Dictionary): return event.get("kind", "") == "changed",
			func(social: Dictionary): return social.get("me", {}).get("hidden", false) == true):
		return false
	main_me = await _peer_request(HTTPClient.METHOD_GET, "/v1/me", _main_token)
	if not main_me.get("ok", false) or main_me.get("body", {}).get("hidden", false) != true:
		return _phase_fail_named("server hidden presence")
	_drain_social_events()
	if not _native_action({"action": "block_player", "pid": _peer_pid, "note": "social node block note"}):
		return _phase_fail()
	if not await _wait_native_ack("native block mutation acknowledgement", "block_player",
			func(event: Dictionary): return event.get("kind", "") == "changed",
			func(_social: Dictionary): return true):
		return false
	var server_blocks: Dictionary = await _peer_request(HTTPClient.METHOD_GET, "/v1/blocks?limit=100", _main_token)
	if not server_blocks.get("ok", false):
		return _phase_fail_named("server block note")
	var server_block_rows: Array = server_blocks.get("body", {}).get("blocks", [])
	if not server_block_rows.any(func(block: Dictionary):
			return str(block.get("pid", "")) == _peer_pid and str(block.get("note", "")) == "social node block note"):
		return _phase_fail_named("server block note")
	_drain_social_events()
	if not _native_action({"action": "fetch_blocks"}):
		return _phase_fail()
	if not await _wait_native_ack("fresh native block listing", "blocks",
			func(event: Dictionary): return event.get("kind", "") == "changed",
			func(social: Dictionary):
				for block in social.get("blocks", []):
					if str(block.get("pid", "")) == _peer_pid and str(block.get("note", "")) == "social node block note":
						return true
				return false):
		return false
	_drain_social_events()
	if not _native_action({"action": "send_message", "pid": _peer_pid, "client_id": _message_client, "text": "native social node message"}):
		return _phase_fail()
	if not await _wait_native_refusal("blocked chat refusal", "send_message", "unavailable"):
		return false
	_drain_social_events()
	if not _native_action({"action": "fetch_messages", "pid": _peer_pid, "after": 0}):
		return _phase_fail()
	if not await _wait_native_ack("fresh history fetch after block", "messages",
			func(event: Dictionary): return event.get("kind", "") == "chat" and str(event.get("pid", "")) == _peer_pid,
			func(social: Dictionary):
				var messages: Array = _conversation(social).get("messages", [])
				return messages.size() == 2 and messages.any(func(message: Dictionary):
					return _same_message(message, _reply_client, _reply_text, _peer_pid, _main_pid))):
		return false
	if not await _wait_message(_message_client, "native social node message", 1):
		return false
	if not await _server_history_valid("history after block"):
		return false
	_drain_social_events()
	if not _native_action({"action": "unblock_player", "pid": _peer_pid}):
		return _phase_fail()
	if not await _wait_native_ack("native unblock mutation acknowledgement", "unblock_player",
			func(event: Dictionary): return event.get("kind", "") == "changed",
			func(_social: Dictionary): return true):
		return false
	server_blocks = await _peer_request(HTTPClient.METHOD_GET, "/v1/blocks?limit=100", _main_token)
	if not server_blocks.get("ok", false) or server_blocks.get("body", {}).get("blocks", []).any(
			func(block: Dictionary): return str(block.get("pid", "")) == _peer_pid):
		return _phase_fail_named("server unblock")
	_drain_social_events()
	if not _native_action({"action": "fetch_blocks"}):
		return _phase_fail()
	if not await _wait_native_ack("fresh native unblock listing", "blocks",
			func(event: Dictionary): return event.get("kind", "") == "changed",
			func(social: Dictionary):
				return not social.get("blocks", []).any(func(block: Dictionary): return str(block.get("pid", "")) == _peer_pid)):
		return false
	if not await _refriend_peer():
		return false
	_drain_social_events()
	if not _native_action({"action": "remove_friend", "pid": _peer_pid}):
		return _phase_fail()
	if not await _wait_native_ack("native unfriend acknowledgement", "remove_friend",
			func(event: Dictionary): return event.get("kind", "") == "changed",
			func(social: Dictionary):
				return not social.get("friends", []).any(func(friend: Dictionary): return str(friend.get("pid", "")) == _peer_pid)):
		return false
	var server_friends: Dictionary = await _peer_request(HTTPClient.METHOD_GET, "/v1/friends", _main_token)
	if not server_friends.get("ok", false) or server_friends.get("body", {}).get("friends", []).any(
			func(friend: Dictionary): return str(friend.get("pid", "")) == _peer_pid):
		return _phase_fail_named("server unfriend")
	_drain_social_events()
	if not _native_action({"action": "send_message", "pid": _peer_pid, "client_id": _message_client, "text": "native social node message"}):
		return _phase_fail()
	if not await _wait_native_refusal("unfriended chat refusal", "send_message", "unavailable"):
		return false
	_drain_social_events()
	if not _native_action({"action": "fetch_messages", "pid": _peer_pid, "after": 0}):
		return _phase_fail()
	if not await _wait_native_ack("fresh history fetch after unfriend", "messages",
			func(event: Dictionary): return event.get("kind", "") == "chat" and str(event.get("pid", "")) == _peer_pid,
			func(social: Dictionary):
				var messages: Array = _conversation(social).get("messages", [])
				return messages.size() == 2 and messages.any(func(message: Dictionary):
					return _same_message(message, _reply_client, _reply_text, _peer_pid, _main_pid))):
		return false
	if not await _wait_message(_message_client, "native social node message", 1):
		return false
	return await _server_history_valid("history after unfriend")


func _refriend_peer() -> bool:
	var previous_seq := _social_seq(_shell.online_social_status())
	_drain_social_events()
	if not _native_action({"action": "request_friend_code", "code": _peer_code}):
		return _phase_fail()
	if not await _wait_native_ack("fresh outgoing friend request", "friend_request_code",
			func(event: Dictionary): return event.get("kind", "") == "changed",
			func(social: Dictionary):
				return social.get("requests", []).any(func(request: Dictionary):
					return str(request.get("to", {}).get("pid", "")) == _peer_pid)):
		return false
	var request_id := ""
	var until := Time.get_ticks_msec() + int(WAIT_PHASE * 1000.0)
	while Time.get_ticks_msec() < until and request_id.is_empty():
		var inbox: Dictionary = await _peer_request(HTTPClient.METHOD_GET, "/v1/inbox", _peer_token)
		if inbox.get("ok", false):
			for item in inbox.get("body", {}).get("requests", []):
				if str(item.get("from", {}).get("pid", "")) == _main_pid and \
						str(item.get("to", {}).get("pid", "")) == _peer_pid:
					request_id = str(item.get("id", ""))
					break
		if request_id.is_empty():
			await _wait_seconds(0.15)
	if request_id.is_empty():
		return _phase_fail_named("peer re-friend request")
	var accepted: Dictionary = await _peer_request(HTTPClient.METHOD_POST,
		"/v1/friends/requests/%s/accept" % request_id, _peer_token, {"v": 1})
	if not accepted.get("ok", false):
		return _phase_fail()
	return await _wait_social("native re-friend inbox sequence", func(social: Dictionary):
		if _social_seq(social) == previous_seq or not social.get("friends", []).any(
				func(friend: Dictionary): return str(friend.get("pid", "")) == _peer_pid):
			return false
		return not social.get("requests", []).any(func(request: Dictionary):
			return str(request.get("from", {}).get("pid", "")) == _peer_pid or \
				str(request.get("to", {}).get("pid", "")) == _peer_pid), WAIT_PHASE)


func _ordinary_room_and_report() -> bool:
	_phase = "ordinary room roster and report"
	if not await _wait_status("Open Play", func(state: Dictionary): return state.get("page", "") == "OnlineOpenPlay", WAIT_PHASE):
		return false
	await _online_click("*Controls.CreateGame")
	await _wait_seconds(1.5)
	var password := _new_id()
	var fields_result: Dictionary = _shell.online_apply({"game_name": "Social node ordinary", "game_password": password})
	if not fields_result.get("ok", false):
		return _phase_fail()
	await _wait_seconds(0.5)
	await _online_click("*CreateGamePopup.Accept")
	if not await _wait_status("ordinary staging room", func(state: Dictionary):
			return state.get("in_game", false) and int(state.get("state", -1)) == 6, WAIT_ROOM):
		return false
	_source_game = str(_shell.online_status().get("game_id", ""))
	if not _valid_hex(_source_game, 16):
		return _phase_fail()
	var peer_join: Dictionary = await _peer_request(HTTPClient.METHOD_POST,
		"/v1/games/%s/join" % _source_game, _peer_token, {
			"profile": _profile,
			"password": password,
			"mode": PEER_MODE,
			"platform": PEER_PLATFORM,
		})
	if not peer_join.get("ok", false):
		return _phase_fail()
	if not await _wait_social("native authoritative room player", func(social: Dictionary):
			for player in social.get("room_players", []):
				if str(player.get("pid", "")) == _peer_pid and not str(player.get("target", "")).is_empty():
					return true
			return false, WAIT_ROOM):
		return false
	var view: Dictionary = await _peer_request(HTTPClient.METHOD_GET,
		"/v1/games/%s?since=0" % _source_game, _peer_token)
	if not view.get("ok", false):
		return _phase_fail()
	var game: Dictionary = view.get("body", {}).get("game", {})
	var peer_slots := 0
	var main_slots := 0
	for slot in game.get("slots", []):
		if str(slot.get("pid", "")) == _peer_pid:
			peer_slots += 1
		if str(slot.get("pid", "")) == _main_pid:
			main_slots += 1
	if peer_slots != 1 or main_slots != 1:
		return _phase_fail_named("authoritative room roster")
	var target := ""
	var room_target := ""
	for player in _shell.online_social_status().get("room_players", []):
		if str(player.get("pid", "")) == _peer_pid:
			room_target = str(player.get("target", ""))
			break
	if room_target.is_empty():
		return _phase_fail_named("authoritative room report target")
	var target_nick := "Social node peer"
	for player in _shell.online_status().get("report_players", []):
		if not player.get("you", false) and not str(player.get("id", "")).is_empty():
			target = str(player.get("id", ""))
			target_nick = str(player.get("name", target_nick))
			break
	if target.is_empty():
		return _phase_fail_named("report target")
	if target != room_target:
		return _phase_fail_named("roster and report target agreement")
	if _online_reports == null or not _online_reports.has_method("open_target"):
		return _phase_fail()
	_reports_received.clear()
	_online_reports.connect("received", Callable(self, "_report_received"))
	_online_reports.call("open_target", _source_game, target, target_nick, _main_token)
	var reason: TextEdit = _online_reports.get("_reason")
	if reason == null:
		return _phase_fail_named("report dialog")
	reason.text = "social node contract report"
	_online_reports.call("submit")
	if not await _wait_condition("first report receipt", func(): return _reports_received.size() == 1, WAIT_PHASE):
		return false
	var first_receipt := str(_reports_received[0])
	if RegEx.create_from_string("^R-[0-9a-f]{24}$").search(first_receipt) == null:
		return _phase_fail()
	var report_key := str(_online_reports.get("_key"))
	var report_payload := str(_online_reports.get("_payload"))
	if not _valid_hex(report_key, 32) or report_payload.is_empty():
		return _phase_fail_named("retained report submission")
	_online_reports.call("submit")
	if not await _wait_condition("idempotent report receipt", func(): return _reports_received.size() == 2, WAIT_PHASE):
		return false
	if str(_reports_received[1]) != first_receipt or str(_online_reports.get("_key")) != report_key or \
			str(_online_reports.get("_payload")) != report_payload:
		return _phase_fail_named("replayed report receipt")
	return true


func _report_received(report_id: String) -> void:
	_reports_received.append(report_id)


func _challenge_and_reverse_invite() -> bool:
	_phase = "challenge handoff and reverse invite"
	var challenge_password := _new_id()
	if not _native_action({"action": "create_challenge", "pid": _peer_pid, "mode": "normal", "password": challenge_password}):
		return _phase_fail()
	if not await _wait_status("challenge room", func(state: Dictionary):
			return state.get("in_game", false) and str(state.get("game_id", "")) != _source_game, WAIT_ROOM):
		return false
	_challenge_game = str(_shell.online_status().get("game_id", ""))
	if not _valid_hex(_challenge_game, 16):
		return _phase_fail_named("challenge room identity")
	var source_view: Dictionary = await _peer_request(HTTPClient.METHOD_GET,
		"/v1/games/%s?since=0" % _source_game, _peer_token)
	if source_view.get("ok", false):
		var source_body: Dictionary = source_view.get("body", {})
		var source_game_view: Variant = source_body.get("game")
		if not source_game_view is Dictionary or not source_game_view.get("slots") is Array:
			return _phase_fail_named("authoritative source-room roster")
		for slot in source_game_view.get("slots", []):
			if str(slot.get("pid", "")) == _main_pid:
				return _phase_fail_named("confirmed source leave")
	else:
		var status := int(source_view.get("status", 0))
		var error := str(source_view.get("body", {}).get("error", ""))
		var authoritative_closure := (status == 404 and error in ["no_game", "gone"]) or \
			(status == 410 and error == "gone")
		if not authoritative_closure:
			return _phase_fail_named("authoritative source-room closure")
	var challenge_inbox := await _wait_peer_invite("challenge", _challenge_game)
	if challenge_inbox.is_empty():
		return _phase_fail_named("peer challenge invitation")
	_challenge_invite = str(challenge_inbox.get("id", ""))
	if _challenge_invite.is_empty():
		return _phase_fail()
	var before: Dictionary = await _peer_request(HTTPClient.METHOD_GET,
		"/v1/games/%s?since=0" % _challenge_game, _main_token)
	if not before.get("ok", false) or not before.get("body", {}).get("game") is Dictionary:
		return _phase_fail_named("challenge room before profile refusal")
	var before_slots: Variant = before.get("body", {}).get("game", {}).get("slots")
	if not before_slots is Array:
		return _phase_fail_named("challenge room before profile refusal")
	var incompatible: Dictionary = await _peer_request(HTTPClient.METHOD_POST,
		"/v1/invites/%s/accept" % _challenge_invite, _peer_token, {
			"v": 1, "profile": "0".repeat(32), "mode": PEER_MODE, "platform": PEER_PLATFORM,
		})
	if incompatible.get("status", 0) != 409 or str(incompatible.get("body", {}).get("error", "")) != "profile":
		return _phase_fail_named("incompatible profile refusal")
	var after: Dictionary = await _peer_request(HTTPClient.METHOD_GET,
		"/v1/games/%s?since=0" % _challenge_game, _main_token)
	if not after.get("ok", false) or not after.get("body", {}).get("game") is Dictionary:
		return _phase_fail_named("challenge room after profile refusal")
	var after_slots: Variant = after.get("body", {}).get("game", {}).get("slots")
	if not after_slots is Array or before_slots != after_slots:
		return _phase_fail_named("profile refusal roster preservation")
	var accepted: Dictionary = await _peer_request(HTTPClient.METHOD_POST,
		"/v1/invites/%s/accept" % _challenge_invite, _peer_token, {
			"v": 1, "profile": _profile, "mode": PEER_MODE, "platform": PEER_PLATFORM,
	})
	if not accepted.get("ok", false) or str(accepted.get("body", {}).get("id", "")) != _challenge_game:
		return _phase_fail()
	var joined: Dictionary = await _peer_request(HTTPClient.METHOD_POST,
		"/v1/games/%s/join" % _challenge_game, _peer_token, {
			"profile": _profile, "mode": PEER_MODE, "platform": PEER_PLATFORM,
		})
	if not joined.get("ok", false) or str(joined.get("body", {}).get("game", {}).get("id", "")) != _challenge_game:
		return _phase_fail()
	if not await _wait_social("challenge room roster", func(social: Dictionary):
			return social.get("room_players", []).any(func(player: Dictionary): return str(player.get("pid", "")) == _peer_pid), WAIT_ROOM):
		return false
	var challenge_view: Dictionary = await _peer_request(HTTPClient.METHOD_GET,
		"/v1/games/%s?since=0" % _challenge_game, _peer_token)
	if not challenge_view.get("ok", false):
		return _phase_fail_named("challenge room authoritative roster")
	var challenge_main_slots := 0
	var challenge_peer_slots := 0
	for slot in challenge_view.get("body", {}).get("game", {}).get("slots", []):
		if str(slot.get("pid", "")) == _main_pid:
			challenge_main_slots += 1
		if str(slot.get("pid", "")) == _peer_pid:
			challenge_peer_slots += 1
	if challenge_main_slots != 1 or challenge_peer_slots != 1:
		return _phase_fail_named("challenge room authoritative roster")
	if not await _reverse_invitation():
		return false
	return true


func _reverse_invitation() -> bool:
	_phase = "reverse invitation acceptance"
	# The peer leaves while the native player is still present, preserving the
	# peer's room handoff. The native player then explicitly leaves before
	# accepting, so no implicit leave is hidden in the accept action.
	var peer_left := await _peer_request(HTTPClient.METHOD_POST, "/v1/games/%s/leave" % _challenge_game, _peer_token)
	if not peer_left.get("ok", false):
		return _phase_fail()
	var reverse_setup := {
		"map": "maps/map mp evendim/map mp evendim.map",
		"startingCash": 1500,
		"slots": [{"state": "player", "faction": 0, "color": 0, "startPos": 0, "team": 0}, {"state": "open"}],
	}
	var created: Dictionary = await _peer_request(HTTPClient.METHOD_POST, "/v1/games", _peer_token, {
		"name": "Social node reverse",
		"profile": _profile,
		"mode": PEER_MODE,
		"platform": PEER_PLATFORM,
		"setup": reverse_setup,
		"password": _new_id(),
	})
	if not created.get("ok", false):
		return _phase_fail()
	_reverse_game = str(created.get("body", {}).get("id", ""))
	if not _valid_hex(_reverse_game, 16):
		return _phase_fail()
	var invite := await _peer_request(HTTPClient.METHOD_POST, "/v1/invites", _peer_token, {
		"v": 1, "to": _main_pid, "game": _reverse_game, "kind": "challenge", "mode": "normal",
	})
	if not invite.get("ok", false):
		return _phase_fail()
	_reverse_invite = str(invite.get("body", {}).get("invite", {}).get("id", ""))
	if _reverse_invite.is_empty():
		return _phase_fail()
	if not _native_action({"action": "leave_game"}):
		return _phase_fail()
	if not await _wait_status("explicit reverse source leave", func(state: Dictionary): return not state.get("in_game", false), WAIT_PHASE):
		return false
	if not await _wait_social("reverse invite in native inbox", func(social: Dictionary):
			return social.get("invites", []).any(func(item: Dictionary): return str(item.get("id", "")) == _reverse_invite), WAIT_PHASE):
		return false
	if not _native_action({"action": "accept_invite", "id": _reverse_invite}):
		return _phase_fail()
	if not await _wait_status("native accepted reverse room", func(state: Dictionary):
			return state.get("in_game", false) and str(state.get("game_id", "")) == _reverse_game, WAIT_ROOM):
		return false
	var reverse_view: Dictionary = await _peer_request(HTTPClient.METHOD_GET,
		"/v1/games/%s?since=0" % _reverse_game, _peer_token)
	if not reverse_view.get("ok", false):
		return _phase_fail()
	var main_slots := 0
	var peer_slots := 0
	for slot in reverse_view.get("body", {}).get("game", {}).get("slots", []):
		if str(slot.get("pid", "")) == _main_pid:
			main_slots += 1
		if str(slot.get("pid", "")) == _peer_pid:
			peer_slots += 1
	if main_slots != 1 or peer_slots != 1:
		return _phase_fail_named("reverse room unique occupied slots")
	return true


func _wait_peer_invite(kind: String, game: String) -> Dictionary:
	var until := Time.get_ticks_msec() + int(WAIT_ROOM * 1000.0)
	while Time.get_ticks_msec() < until:
		var inbox := await _peer_request(HTTPClient.METHOD_GET, "/v1/inbox", _peer_token)
		if inbox.get("ok", false):
			for item in inbox.get("body", {}).get("invites", []):
				if str(item.get("kind", "")) == kind and str(item.get("game", "")) == game:
					return item
		await _wait_seconds(0.2)
	return {}


func _recovery_event() -> bool:
	_phase = "recovery event-only code"
	_drain_social_events()
	if not _native_action({"action": "generate_recovery"}):
		return _phase_fail()
	var event_code := ""
	var until := Time.get_ticks_msec() + int(WAIT_PHASE * 1000.0)
	while Time.get_ticks_msec() < until and event_code.is_empty():
		for event in _shell.online_social_events():
			if event.get("kind", "") == "recovery" and event.has("code"):
				event_code = str(event.get("code", ""))
				break
		if event_code.is_empty():
			await get_tree().process_frame
	if event_code.is_empty():
		return _phase_fail_named("recovery event")
	if event_code.is_empty() or not _valid_recovery(event_code):
		return _phase_fail_named("recovery event format")
	var state: Dictionary = _shell.online_social_status()
	if state.has("recovery") or state.has("recovery_code") or state.has("code"):
		return _phase_fail_named("recovery code leaked into social state")
	return true


func _valid_recovery(code: String) -> bool:
	return RegEx.create_from_string("^[0123456789ABCDEFGHJKMNPQRSTVWXYZ]{16}$").search(code) != null


func _peer_request(method: int, path: String, token: String, payload: Dictionary = {}) -> Dictionary:
	var request := HTTPRequest.new()
	request.timeout = 10.0
	request.max_redirects = 0
	request.body_size_limit = 262144
	add_child(request)
	var headers := PackedStringArray(["accept: application/json"])
	if not token.is_empty():
		headers.append("authorization: Bearer " + token)
	var data := ""
	if not payload.is_empty():
		headers.append("content-type: application/json")
		data = JSON.stringify(payload)
	var error := request.request(_online_lobby_url().trim_suffix("/") + path, headers, method, data)
	if error != OK:
		request.queue_free()
		return {"ok": false, "status": 0}
	var completed: Array = await request.request_completed
	request.queue_free()
	var result := int(completed[0])
	var status := int(completed[1])
	var parsed: Variant = JSON.parse_string((completed[3] as PackedByteArray).get_string_from_utf8())
	if result != HTTPRequest.RESULT_SUCCESS or parsed == null or not parsed is Dictionary:
		return {"ok": false, "status": status}
	return {"ok": status >= 200 and status < 300, "status": status, "body": parsed}
