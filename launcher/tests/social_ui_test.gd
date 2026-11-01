extends SceneTree

const Screen := preload("res://scripts/ui/multiplayer_screen.gd")
const Style := preload("res://scripts/ui/style.gd")

var _failed := 0
var _screen: Control


class FixtureOAuth extends Node:
	signal status_changed(message: String)
	signal completed(pid: String, providers: Array)
	signal session_ready(response: Dictionary)
	signal recovery_code(code: String)
	var busy := false
	var calls: Array = []
	var session: Dictionary = {}

	func set_session(value: Dictionary) -> void:
		clear_session()
		session = value.duplicate(true)

	func clear_session() -> void:
		busy = false
		session.clear()

	func finish(response: Dictionary) -> void:
		session = {"token": response.token, "pid": response.account.pid, "id": response.account.id}
		busy = false
		session_ready.emit(response)
		completed.emit(response.account.pid, response.providers)

	func begin(provider: String, mode: String, identity: Dictionary = {}, recovery: String = "") -> void:
		calls.append([provider, mode, identity] + ([recovery] if recovery != "" else []))
		busy = true
		status_changed.emit("Finish sign-in in your system browser…")

	func cancel() -> void:
		busy = false
		status_changed.emit("Sign-in cancelled.")

	func refresh() -> void:
		completed.emit("1111111111111111", ["discord"])

	func unlink(provider: String, recovery: String) -> void:
		calls.append(["unlink", provider, recovery])
		completed.emit("1111111111111111", [])


class FixtureClient extends Node:
	signal changed()
	signal notice(text: String)
	signal problem(message: String)
	signal recovery_ready(code: String)
	signal chat_ready(pid: String, result: Dictionary)
	signal connection_changed(connected: bool)
	signal action_done(kind: String, success: bool)
	var me: Dictionary = {}
	var friends: Array = []
	var snapshot := {"presence": [], "requests": [], "invites": [], "notices": [], "chat": [], "stats": {}}
	var busy := false
	var token := ""
	var last_error := ""
	var calls: Array = []
	var messages: Array = []
	var fail_send := false
	const SELF := {"pid": "1111111111111111", "nick": "Rowan", "code": "ABCD-EFGH-JK"}
	const PEER := {"pid": "2222222222222222", "nick": "A very patient friend", "code": "0123-4567-89"}

	func is_signed_in() -> bool:
		return not me.is_empty()

	func sign_in(_name: String, _recover: String) -> void:
		await get_tree().process_frame
		token = "synthetic-social-session"
		me = SELF.duplicate(true)
		me.hidden = false
		friends = [PEER.duplicate(true)]
		snapshot.presence = [{"pid": PEER.pid, "state": "in_room"}]
		snapshot.requests = [{"id": "3333333333333333", "from": PEER, "to": SELF},
			{"id": "4444444444444444", "from": SELF, "to": PEER}]
		snapshot.invites = [{"id": "5555555555555555", "from": PEER, "to": SELF, "kind": "challenge", "game": "6666666666666666",
			"mode": "uber", "note": "Ready for a friendly match?", "expires": int(Time.get_unix_time_from_system()) + 120}]
		messages = [{"id": 1, "from": PEER.pid, "to": SELF.pid, "text": "Hello! [b]This is plain text.[/b]", "at": 1, "client_id": "a".repeat(32)}]
		changed.emit()
		connection_changed.emit(true)

	func sign_out() -> void:
		token = ""
		me = {}
		friends = []
		for key in ["presence", "requests", "invites", "notices", "chat"]:
			snapshot[key] = []
		changed.emit()
		connection_changed.emit(false)

	func oauth_session() -> Dictionary:
		return {"token": token, "pid": SELF.pid, "id": "guest:1111111111111111"} if is_signed_in() else {}

	func oauth_restore_identity(name: String) -> Dictionary:
		return {"name": name, "install": "a".repeat(32)}

	func adopt_oauth_session(data: Dictionary) -> bool:
		sign_out()
		token = data.token
		me = SELF.duplicate(true)
		me.hidden = false
		friends = [PEER.duplicate(true)]
		snapshot.presence = [{"pid": PEER.pid, "state": "in_room"}]
		snapshot.requests = [{"id": "3333333333333333", "from": PEER, "to": SELF},
			{"id": "4444444444444444", "from": SELF, "to": PEER}]
		snapshot.invites = [{"id": "5555555555555555", "from": PEER, "to": SELF, "kind": "challenge", "game": "6666666666666666",
			"mode": "uber", "note": "Ready for a friendly match?", "expires": int(Time.get_unix_time_from_system()) + 120}]
		snapshot.notices = []
		snapshot.chat = []
		messages = [{"id": 1, "from": PEER.pid, "to": SELF.pid, "text": "Hello! [b]This is plain text.[/b]", "at": 1, "client_id": "a".repeat(32)}]
		changed.emit()
		connection_changed.emit(true)
		return true

	func request_friend(code: String) -> void:
		calls.append(["request", code])

	func respond_request(id: String, accept: bool) -> void:
		calls.append(["respond", id, accept])
		snapshot.requests = snapshot.requests.filter(func(r: Dictionary) -> bool: return r.id != id)
		changed.emit()

	func remove_friend(pid: String) -> void:
		calls.append(["remove", pid])
		friends = []
		changed.emit()

	func decline_invite(id: String) -> void:
		calls.append(["decline", id])
		snapshot.invites = []
		changed.emit()

	func set_status(state: String, hidden: bool) -> void:
		calls.append(["status", state, hidden])
		me.hidden = hidden
		changed.emit()

	var recovery_calls: Array = []

	func create_recovery(current: String = "") -> void:
		recovery_calls.append(current)
		recovery_ready.emit("0123456789ABCDEF")

	func get_chat(pid: String, after := 0) -> void:
		await get_tree().process_frame
		chat_ready.emit(pid, {"after": after, "messages": messages.filter(func(m: Dictionary) -> bool: return m.id > after),
			"next": null, "last_id": messages.size(), "read_id": 0})

	func send_chat(pid: String, text: String) -> void:
		calls.append(["send", pid, text])
		busy = true
		changed.emit()
		await get_tree().process_frame
		busy = false
		changed.emit()
		if fail_send:
			problem.emit("The message could not be sent.")
			action_done.emit("chat_send", false)
			return
		messages.append({"id": messages.size() + 1, "from": SELF.pid, "to": pid, "text": text, "at": 2, "client_id": "b".repeat(32)})
		action_done.emit("chat_send", true)

	func mark_chat_read(pid: String, through: int) -> void:
		calls.append(["read", pid, through])


func _init() -> void:
	_run.call_deferred()


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed += 1
		print("SOCIAL UI FAIL ", what)


func _recovery_dialog() -> ConfirmationDialog:
	for child in _screen.get_children():
		if child is ConfirmationDialog and child.find_child("RecoveryCode", true, false) != null and not child.is_queued_for_deletion():
			return child
	return null


func _button(node: Node, caption: String) -> Button:
	for child in node.get_children():
		if child is Button and child.text == caption:
			return child
		var nested := _button(child, caption)
		if nested != null:
			return nested
	return null


func _frames() -> void:
	for i in range(4):
		await process_frame


func _run() -> void:
	root.size = Vector2i(1280, 720)
	var host := Control.new()
	host.theme = Style.make_theme()
	host.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(host)
	var background := ColorRect.new()
	background.color = Style.BG
	background.set_anchors_preset(Control.PRESET_FULL_RECT)
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	host.add_child(background)
	_screen = Screen.new()
	_screen.build("Open-BFME/openbfme-dev")
	host.add_child(_screen)
	_screen.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_screen.offset_left = 40
	_screen.offset_right = -40
	_screen.offset_top = 32
	_screen.offset_bottom = -32
	var client := FixtureClient.new()
	root.add_child(client)
	_screen.bind_client(client, host)
	var oauth := FixtureOAuth.new()
	root.add_child(oauth)
	_screen.bind_oauth(oauth)
	await _frames()
	_check(_screen._signin.visible and not _screen._tabs.visible, "signed-out controls")
	_screen._name.text = "Rowan"
	await _screen._oauth_action("email")
	_check(oauth.calls.back()[0] == "email" and oauth.calls.back()[1] == "restore", "unsigned e-mail sign-in uses restore")
	_check(_screen._connect.disabled and _screen._oauth_cancel.visible, "browser flow disables guest sign-in and offers cancel")
	oauth.cancel()
	await _screen._sign_in()
	await _frames()
	_check(not _screen._signin.visible and _screen._tabs.visible, "signed-in controls")
	_screen._oauth_refresh()
	_check(_screen._oauth_buttons.discord.text == "Unlink Discord" and _screen._oauth_buttons.email.text == "Link e-mail", "same-session provider controls")
	# ONLINEMENU-1 (PR #8 review, finding 2): a second provider next to a linked one asks for the recovery code first
	var calls_before := oauth.calls.size()
	_screen._oauth_action("email")
	var ask := _recovery_dialog()
	_check(ask != null and oauth.calls.size() == calls_before, "a second provider asks for the recovery code before the browser flow")
	if ask != null:
		(ask.find_child("RecoveryCode", true, false) as LineEdit).text = "abcd-efgh jkmn pqrs"
		ask.confirmed.emit()
	_check(oauth.calls.size() == calls_before + 1 and oauth.calls.back() == ["email", "link", {}, "ABCDEFGHJKMNPQRS"],
		"the second provider's link carries the recovery code")
	await _frames()
	var linked := {"v": 1, "token": "synthetic-linked-token", "account": {"id": "guest:1111111111111111",
		"name": "Rowan", "kind": "guest", "pid": FixtureClient.SELF.pid}, "providers": ["discord", "email"]}
	oauth.finish(linked)
	_check(client.token == linked.token and oauth.session.get("token") == linked.token,
		"link adoption replaces the social session without clearing OAuth management credentials")
	# ONLINEMENU-1 (Sol r2): with an account linked, a new recovery code asks for the current one, or a confirmation with the linked account
	_screen._oauth_providers = ["discord", "email"]
	_screen._confirm_recovery()
	var remake := _recovery_dialog()
	_check(remake != null, "a linked player's new recovery code asks for the current code")
	if remake != null:
		(remake.find_child("RecoveryCode", true, false) as LineEdit).text = "ABCDEFGHJKMNPQRS"
		# ConfirmationDialog hides before confirmed; emitting the signal alone does not.
		remake.hide()
		remake.confirmed.emit()
		_check(client.recovery_calls.back() == "ABCDEFGHJKMNPQRS", "the current code goes with the request")
	await _frames()
	_screen._secret_dialog.hide()
	_screen._confirm_recovery()
	remake = _recovery_dialog()
	if remake != null:
		var confirm_calls := oauth.calls.size()
		remake.custom_action.emit(&"discord")
		_check(oauth.calls.size() == confirm_calls + 1 and oauth.calls.back().slice(0, 2) == ["discord", "confirm"],
			"Confirm with Discord starts the confirmation flow")
		oauth.cancel()
	await _frames()
	if "--preview" in OS.get_cmdline_user_args():
		await create_timer(3600.0).timeout
		quit()
		return
	_check(_screen._code.text.contains(FixtureClient.SELF.code), "own friend code displayed")
	_check(_screen._friends.get_child_count() == 1, "friends rendered")
	_check(_screen._toast.visible and _screen._invites.get_child_count() == 1, "challenge toast and inbox")
	if "--visual" in OS.get_cmdline_user_args():
		return
	_screen._friend_code.text = FixtureClient.PEER.code
	await _screen._add_friend()
	_check(client.calls.has(["request", FixtureClient.PEER.code]), "friend-code request")
	var accept := _button(_screen._requests, "Accept")
	_check(accept != null, "incoming request has accept")
	if accept != null:
		accept.pressed.emit()
	_check(client.calls.has(["respond", "3333333333333333", true]), "accept bound to exact request")
	_check(_button(_screen._requests, "Accept") == null, "outgoing request has no accept")
	client.snapshot.requests = [{"id": "3333333333333333", "from": FixtureClient.PEER, "to": FixtureClient.SELF}]
	client.changed.emit()
	_button(_screen._requests, "Decline").pressed.emit()
	_check(client.calls.has(["respond", "3333333333333333", false]), "decline bound to exact request")
	var joined: Array = []
	_screen.join_invite.connect(func(invite: Dictionary) -> void: joined.append(invite.id))
	_button(_screen._toast, "Join").pressed.emit()
	_check(joined == ["5555555555555555"], "join passes pending invite without accepting it")
	_button(_screen._toast, "Later").pressed.emit()
	_check(not _screen._toast.visible and _screen._invites.get_child_count() == 1, "Later does not decline")
	client.snapshot.invites = []
	client.changed.emit()
	_check(not _screen._toast.visible and _button(_screen._invites, "Join") == null, "replacement snapshot removes invitation")
	client.snapshot.invites = [{"id": "7777777777777777", "from": FixtureClient.SELF, "to": FixtureClient.PEER,
		"kind": "invite", "game": "6666666666666666", "mode": null, "note": "", "expires": int(Time.get_unix_time_from_system()) + 120}]
	client.changed.emit()
	_check(not _screen._toast.visible and _button(_screen._invites, "Join") == null, "outgoing invitation never offers Join")
	_check(_screen._invites.get_child(0) is VBoxContainer and _screen._invites.get_child(0).get_child(0).text.begins_with("Invitation sent to "), "outgoing invitation remains visible")
	client.create_recovery()
	_check(_screen._secret_label.text == "0123456789ABCDEF", "one-time recovery display")
	_screen._clear_secret()
	_check(_screen._secret_label.text == "" and not _screen._secret_dialog.visible, "dismiss clears recovery secret")
	_screen._open_chat(FixtureClient.PEER.pid, FixtureClient.PEER.nick)
	await _frames()
	_check(not _screen._chat_log.bbcode_enabled and _screen._chat_log.text.contains("[b]"), "chat markup is literal")
	_screen._chat_text.text = "Ready!"
	client.fail_send = true
	_screen._send_chat()
	_check(_screen._chat_text.text == "Ready!", "pending send retains draft")
	await _frames()
	_check(_screen._chat_text.text == "Ready!", "failed send retains draft")
	client.fail_send = false
	_screen._send_chat()
	await _frames()
	_check(client.calls.has(["send", FixtureClient.PEER.pid, "Ready!"]) and _screen._chat_text.text == "", "chat send and clear on success")
	client.sign_out()
	_check(_screen._chat_log.text == "" and _screen._friends.get_child_count() == 0 and not _screen._toast.visible, "sign-out clears private UI state")
	_check(_screen._oauth_providers.is_empty() and _screen._oauth_buttons.email.text == "Sign in with e-mail", "sign-out clears provider UI")
	host.queue_free()
	client.queue_free()
	oauth.queue_free()
	await _frames()
	print("SOCIAL UI OK" if _failed == 0 else "SOCIAL UI FAILED")
	quit(0 if _failed == 0 else 1)
