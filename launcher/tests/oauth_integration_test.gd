extends SceneTree

const OAuthAccount := preload("res://scripts/core/oauth_account.gd")
const SocialClient := preload("res://scripts/net/social_client.gd")
const MultiplayerScreen := preload("res://scripts/ui/multiplayer_screen.gd")

class RecoveryProbe extends Node:
	signal recovery_ready(code: String)
	signal action_done(kind: String, ok: bool)
	var busy := true
	var me := {"pid": "synthetic-player"}
	var calls := 0
	func is_signed_in() -> bool:
		return true
	func create_recovery() -> void:
		calls += 1
		if busy:
			action_done.emit("recovery", false)
			return
		recovery_ready.emit("ABCDEFGHJKMNPQRS")
		action_done.emit("recovery", true)

var lobby_origin := ""
var control_origin := ""
var install_root := ""
var _checks := 0
var _failed := 0
var _failures: Array[String] = []
var _client: Node
var _oauth: Node
var _screen: Control
var _host: Control
var _expected_player: Dictionary = {}
var _last_chat: Dictionary = {}
var _browser_calls := 0
var _recovery := "" # ONLINEMENU-1: the player's recovery code, made before linking (once an account is linked, a new one needs it)


func _initialize() -> void:
	_run.call_deferred()


func _check(ok: bool, label: String) -> void:
	_checks += 1
	if not ok:
		_failed += 1
		_failures.append(label)


func _args() -> Dictionary:
	var result := {}
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--lobby-origin="):
			result.lobby = arg.substr("--lobby-origin=".length())
		elif arg.begins_with("--control-origin="):
			result.control = arg.substr("--control-origin=".length())
		elif arg.begins_with("--install-root="):
			result.install = arg.substr("--install-root=".length())
	return result


func _http(method: int, url: String, body := {}, token := "") -> Dictionary:
	var request := HTTPRequest.new()
	request.timeout = 4.0
	request.max_redirects = 0
	request.body_size_limit = 65536
	root.add_child(request)
	var headers := PackedStringArray(["accept: application/json"])
	if token != "":
		headers.append("authorization: Bearer " + token)
	var raw := ""
	if not body.is_empty():
		raw = JSON.stringify(body)
		headers.append("content-type: application/json")
	var started := request.request(url, headers, method, raw)
	if started != OK:
		request.queue_free()
		return {"ok": false, "status": 0, "body": {}}
	var result: Array = await request.request_completed
	request.queue_free()
	var raw_response := (result[3] as PackedByteArray).get_string_from_utf8()
	var parsed: Variant = JSON.parse_string(raw_response) if not raw_response.is_empty() else {}
	return {"ok": result[0] == HTTPRequest.RESULT_SUCCESS, "status": result[1], "body": parsed if parsed is Dictionary else {}}


func _control(path: String, method := HTTPClient.METHOD_GET) -> Dictionary:
	return await _http(method, control_origin + path)


func _wait_for(predicate: Callable, label: String, seconds := 8.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < deadline:
		if predicate.call():
			return true
		await process_frame
	print("WAIT TIMEOUT: " + label)
	_check(false, label)
	return false


func _browser(url: String) -> int:
	_browser_calls += 1
	_consent(url)
	return OK


func _consent(url: String) -> void:
	var result := await _http(HTTPClient.METHOD_GET, control_origin + "/authorize?url=" + url.uri_encode())
	if not result.ok or result.status != 200 or not result.body.has("callback_url"):
		_check(false, "test browser consent accepted authorization URL")
		return
	var callback_url := str(result.body.get("callback_url", ""))
	var delivered := await _http(HTTPClient.METHOD_POST, control_origin + "/callback", {"url": callback_url})
	_check(delivered.status == 200 and bool(delivered.body.get("ok", false)), "browser followed actual loopback callback")


func _new_client(game_dir: String) -> Node:
	var value := SocialClient.new()
	root.add_child(value)
	_check(value.configure(lobby_origin, "launcher-e2e", lobby_origin, game_dir) == "", "real SocialClient configured for loopback lobby")
	return value


func _new_oauth() -> Node:
	var value := OAuthAccount.new()
	root.add_child(value)
	_check(value.configure(lobby_origin, lobby_origin) == "", "real OAuthAccount configured for loopback lobby")
	value._browser_override = Callable(self, "_browser")
	return value


func _build_screen(client: Node, oauth: Node) -> void:
	_host = Control.new()
	_host.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(_host)
	_screen = MultiplayerScreen.new()
	_screen.build("Open-BFME/openbfme-dev")
	_host.add_child(_screen)
	_screen.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_screen.bind_client(client, _host)
	_screen.bind_oauth(oauth)


func _wait_social(client: Node) -> bool:
	return await _wait_for(func() -> bool:
		return not client.busy and client.is_signed_in() and not client.me.is_empty(),
		"SocialClient loaded /me, /friends, and /inbox")


func _wait_oauth(client: Node, oauth: Node, provider: String) -> bool:
	return await _wait_for(func() -> bool:
		return not oauth.busy and not client.busy and client.is_signed_in() and provider in _screen._oauth_providers,
		"OAuth flow completed and session was adopted for " + provider, 30.0)


func _seed_friend_and_chat() -> Dictionary:
	var friend_install := "b".repeat(32)
	var friend_device := "c".repeat(64)
	var friend_result := await _http(HTTPClient.METHOD_POST, lobby_origin + "/v1/auth/guest", {
		"v": 1, "name": "Friend", "install": friend_install, "device": friend_device,
		"client": {"platform": "linux", "version": "launcher-e2e"},
	})
	_check(friend_result.status == 200 and friend_result.body.has("token"), "friend seeded through guest HTTP route")
	if friend_result.status != 200:
		return {}
	var friend_token := str(friend_result.body.get("token", ""))
	var friend_me := await _http(HTTPClient.METHOD_GET, lobby_origin + "/v1/me", {}, friend_token)
	_check(friend_me.status == 200 and friend_me.body.has("pid") and friend_me.body.has("code"), "friend profile loaded through HTTP route")
	if friend_me.status != 200:
		return {}
	var request := await _http(HTTPClient.METHOD_POST, lobby_origin + "/v1/friends/requests", {
		"v": 1, "code": friend_me.body.get("code", ""),
	}, _client.token)
	_check(request.status == 200 and request.body.has("request"), "friend request created through HTTP route")
	if request.status != 200:
		return {}
	var request_id := str(request.body.get("request", {}).get("id", ""))
	var accepted := await _http(HTTPClient.METHOD_POST,
		lobby_origin + "/v1/friends/requests/%s/accept" % request_id, {"v": 1}, friend_token)
	_check(accepted.status == 200, "friend request accepted through HTTP route")
	var chat_pid := str(friend_me.body.get("pid", ""))
	var sent := await _http(HTTPClient.METHOD_POST, lobby_origin + "/v1/chat/%s/messages" % chat_pid, {
		"v": 1, "client_id": "d".repeat(32), "text": "Persistent launcher chat",
	}, _client.token)
	_check(sent.status == 200, "chat message persisted through actual lobby route")
	return {"pid": str(friend_me.body.get("pid", "")), "code": str(friend_me.body.get("code", "")), "token": friend_token}


func _accounts(provider: String) -> bool:
	_screen._oauth_refresh()
	return await _wait_for(func() -> bool: return not _oauth.busy and provider in _screen._oauth_providers,
		"accounts route reports linked provider")


func _restore(provider: String, install_dir: String, old_token: String, expected: Dictionary) -> Dictionary:
	var held := false
	var deadline := Time.get_ticks_msec() + 8000
	while not held and Time.get_ticks_msec() < deadline:
		var stats := await _control("/stats")
		held = int(stats.body.get("stats", {}).get("lobby_pending", {}).get("inbox", 0)) > 0
		if not held:
			await create_timer(0.02).timeout
	_check(held, "precondition: sign-out cancels a live held inbox request")
	if not held:
		return {"ok": false}
	var started := Time.get_ticks_msec()
	_client.sign_out()
	_check(Time.get_ticks_msec() - started < 1000, "sign-out does not block on the held inbox request")
	_oauth.clear_session()
	_client.queue_free()
	_oauth.queue_free()
	_host.queue_free()
	await process_frame
	await process_frame
	_client = _new_client(install_dir)
	_oauth = _new_oauth()
	_build_screen(_client, _oauth)
	_screen._name.text = "Alice"
	await _screen._oauth_action(provider)
	if not await _wait_social(_client):
		return {"ok": false}
	var same_identity: bool = _client.me.get("pid", "") == expected.get("pid", "") and _client.me.get("code", "") == expected.get("code", "")
	_check(same_identity, "restore retained PID and friend code")
	if not same_identity:
		return {"ok": false}
	var has_friend: bool = _client.friends.any(func(friend: Dictionary) -> bool: return friend.get("pid", "") == expected.get("friend_pid", ""))
	_check(has_friend, "restore reloaded friend list from actual lobby")
	if not has_friend:
		return {"ok": false}
	var old_session := await _http(HTTPClient.METHOD_GET, lobby_origin + "/v1/me", {}, old_token)
	_check(old_session.status == 401, "restore revoked old bearer")
	if old_session.status != 401:
		return {"ok": false}
	_last_chat = {}
	_client.chat_ready.connect(func(_pid: String, result: Dictionary) -> void: _last_chat = result, CONNECT_ONE_SHOT)
	_client.get_chat(expected.get("friend_pid", ""))
	if not await _wait_for(func() -> bool: return not _client.busy and not _last_chat.is_empty(), "restore reloaded persisted chat"):
		return {"ok": false}
	var messages: Array = _last_chat.get("messages", [])
	_check(messages.size() > 0 and messages[0].get("text", "") == "Persistent launcher chat",
		"persisted chat survived restore")
	if messages.is_empty() or messages[0].get("text", "") != "Persistent launcher chat":
		return {"ok": false}
	return {"ok": true, "token": _client.token, "pid": _client.me.get("pid", ""), "code": _client.me.get("code", ""), "friend_pid": expected.get("friend_pid", "")}


func _deny_restore(provider: String, baseline_token: String, expected: Dictionary, install_dir: String) -> bool:
	var denied := _new_client(install_dir)
	var denied_oauth := _new_oauth()
	var identity: Dictionary = await denied.oauth_restore_identity("Alice")
	var has_identity := not identity.is_empty()
	_check(has_identity, "denied restore created a new install identity")
	if not has_identity:
		denied.queue_free()
		denied_oauth.queue_free()
		return false
	denied_oauth.begin(provider, "restore", identity)
	if not await _wait_for(func() -> bool: return not denied_oauth.busy, "unlinked restore finished with a cleared busy state"):
		denied.queue_free()
		denied_oauth.queue_free()
		return false
	var no_session: bool = denied.token.is_empty() and denied_oauth._token.is_empty()
	_check(no_session, "unlinked restore did not create a session")
	if not no_session:
		denied.queue_free()
		denied_oauth.queue_free()
		return false
	var unchanged := await _http(HTTPClient.METHOD_GET, lobby_origin + "/v1/me", {}, baseline_token)
	var unchanged_ok: bool = unchanged.status == 200 and unchanged.body.get("pid", "") == expected.get("pid", "") and unchanged.body.get("code", "") == expected.get("code", "")
	_check(unchanged_ok,
		"denied restore left the linked player unchanged")
	denied.queue_free()
	denied_oauth.queue_free()
	return unchanged_ok


func _make_recovery(client: Node) -> bool:
	# Friend seeding can start an inbox-driven friends refresh on this client.
	if not await _wait_social(client):
		return false
	var code: Array[String] = [""]
	var action := {"done": false, "ok": false}
	var take := func(value: String) -> void: code[0] = value
	var finished := func(kind: String, ok: bool) -> void:
		if kind == "recovery":
			action.done = true
			action.ok = ok
	client.recovery_ready.connect(take)
	client.action_done.connect(finished)
	client.create_recovery()
	var completed := await _wait_for(func() -> bool: return action.done, "recovery action completed before linking")
	client.recovery_ready.disconnect(take)
	client.action_done.disconnect(finished)
	var made: bool = completed and action.ok and code[0] != "" and not client.busy
	_check(made, "recovery code made before linking")
	_recovery = code[0]
	return made


func _recover_when_ready(client: Node, result: Dictionary) -> void:
	result.ok = await _make_recovery(client)
	result.done = true


func _check_recovery_ordering() -> void:
	var probe := RecoveryProbe.new()
	var result := {"done": false, "ok": false}
	_recover_when_ready(probe, result)
	_check(probe.calls == 0 and not result.done, "recovery waits while a friends refresh owns the client")
	probe.busy = false
	await _wait_for(func() -> bool: return result.done, "recovery resumed after the refresh")
	_check(result.ok and probe.calls == 1, "idle client creates exactly one recovery code")
	probe.free()
	_recovery = ""


func _unlink(provider: String) -> bool:
	# ONLINEMENU-1 (PR #8 review, finding 2): unlinking needs the player's recovery code (an OAuth restore keeps it)
	_oauth.set_session(_client.oauth_session())
	_oauth.unlink(provider, _recovery)
	return await _wait_for(func() -> bool: return not _oauth.busy and provider not in _screen._oauth_providers,
		"unlink completed through real OAuthAccount route")


func _run_provider(provider: String, install_b: String, install_c: String, with_failure := false, fresh := true) -> bool:
	var expected := {}
	if fresh:
		_client.sign_in("Alice")
		if not await _wait_social(_client):
			return false
		var friend := await _seed_friend_and_chat()
		if friend.is_empty():
			return false
		if not await _make_recovery(_client):
			return false
		expected = {"pid": _client.me.get("pid", ""), "code": _client.me.get("code", ""), "friend_pid": friend.get("pid", "")}
		_expected_player = expected.duplicate(true)
	else:
		expected = _expected_player.duplicate(true)
		var same_identity: bool = _client.me.get("pid", "") == expected.get("pid", "") and _client.me.get("code", "") == expected.get("code", "")
		_check(same_identity, "second provider reused the original player identity")
		if not same_identity:
			return false
		var has_friend: bool = _client.friends.any(func(friend: Dictionary) -> bool: return friend.get("pid", "") == expected.get("friend_pid", ""))
		_check(has_friend, "second provider reused the original friend graph")
		if not has_friend:
			return false
	if not await _wait_social(_client):
		return false
	if with_failure:
		var fault := await _control("/fault/next-exchange", HTTPClient.METHOD_POST)
		if fault.status != 200:
			_check(false, "provider failure control armed")
			return false
		_check(true, "provider failure control armed")
		_oauth.set_session(_client.oauth_session())
		_screen._oauth_action(provider)
		if not await _wait_for(func() -> bool: return not _oauth.busy, "provider-error finish cleared OAuth busy state"):
			return false
		var session_unchanged: bool = _client.is_signed_in() and _client.me.get("pid", "") == expected.get("pid", "")
		_check(session_unchanged, "provider-error finish left the current social session unchanged")
		var still_unlinked: bool = provider not in _screen._oauth_providers
		_check(still_unlinked, "provider-error finish did not link the provider")
		if not session_unchanged or not still_unlinked:
			return false
	_screen._oauth_action(provider)
	if not await _wait_oauth(_client, _oauth, provider):
		return false
	if not await _accounts(provider):
		return false
	var old_token: String = _client.token
	var restored := await _restore(provider, install_b, old_token, expected)
	if not restored.get("ok", false):
		return false
	if not await _unlink(provider):
		return false
	if not await _deny_restore(provider, restored.get("token", ""), expected, install_c):
		return false
	return true


func _run() -> void:
	await _check_recovery_ordering()
	var supplied := _args()
	lobby_origin = str(supplied.get("lobby", ""))
	control_origin = str(supplied.get("control", ""))
	install_root = str(supplied.get("install", ""))
	_check(lobby_origin.begins_with("http://127.0.0.1:"), "real loopback lobby origin supplied")
	_check(control_origin.begins_with("http://127.0.0.1:"), "separate loopback control origin supplied")
	_check(not install_root.is_empty(), "temporary install root supplied")
	_client = _new_client(install_root.path_join("discord-a"))
	_oauth = _new_oauth()
	_build_screen(_client, _oauth)
	await process_frame
	_screen._name.text = "Alice"
	var first_provider_ok := await _run_provider("discord", install_root.path_join("discord-b"), install_root.path_join("discord-c"))
	_check(first_provider_ok, "Discord link, restore, unlink, and denied restore completed")
	var second_provider_ok := false
	if first_provider_ok:
		# ONLINEMENU-1 (owner 2026-10-20): Discord is the only OAuth provider; a second round, with a provider failure first, on the same player
		second_provider_ok = await _run_provider("discord", install_root.path_join("discord2-b"), install_root.path_join("discord2-c"), true, false)
		_check(second_provider_ok, "Discord again: failure, link, restore, unlink, and denied restore completed")
	var stats := await _control("/stats")
	var stats_body: Dictionary = stats.body.get("stats", {})
	_check(stats.status == 200 and int(stats_body.get("unexpected_provider_calls", 1)) == 0 and
			int(stats_body.get("provider_calls", -1)) == 13 and int(stats_body.get("provider_failures", -1)) == 1,
		"provider mocks observed only expected offline requests")
	_check(int(stats_body.get("pending_codes", -1)) == 0, "synthetic authorization codes were consumed")
	if first_provider_ok and second_provider_ok:
		_check(_browser_calls == 7, "both providers used the expected browser callback count")
	elif first_provider_ok:
		_check(_browser_calls == 3, "Discord used the expected browser callback count")
	_check(int(stats_body.get("guest_calls", -1)) == 2,
		"only the player and seeded friend used guest sign-in; successful and denied restore made none")
	if _failed > 0:
		for failure in _failures:
			print("OAUTH FAILURE: " + failure)
	print("OAUTH INTEGRATION %s %d checks" % ["PASS" if _failed == 0 else "FAIL", _checks])
	quit(0 if _failed == 0 else 1)
