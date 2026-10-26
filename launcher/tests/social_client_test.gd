extends SceneTree

const SocialClient := preload("res://scripts/net/social_client.gd")
const SocialIdentity := preload("res://scripts/core/social_identity.gd")
const Screen := preload("res://scripts/ui/multiplayer_screen.gd")
const Style := preload("res://scripts/ui/style.gd")

var _failures := 0
var _checks := 0
var _case := ""
var _game_dir := ""
var _origin := ""


func _check(value: bool, name: String) -> void:
	_checks += 1
	if not value:
		_failures += 1
		print("SOCIAL FAIL ", name)


func _init() -> void:
	_case = OS.get_environment("SOCIAL_TEST_CASE")
	_game_dir = OS.get_environment("OPENBFME_GAME_USER_DIR")
	_origin = OS.get_environment("SOCIAL_TEST_ORIGIN")
	call_deferred("_run")


func _run() -> void:
	_identity_checks()
	if _case == "oauth_handoff":
		await _oauth_handoff_checks()
	elif _case == "redirect":
		await _redirect_checks()
	elif _case == "no_social":
		await _no_social_checks()
	elif _case == "lock_retry":
		await _lock_retry_checks()
	elif _case == "late":
		await _late_poll_checks()
	elif _case in ["signin_auth_429", "signin_me_503", "signin_friends_503"]:
		await _signin_retry_checks()
	elif _case == "refresh_retry":
		await _refresh_retry_checks()
	elif _case == "held_poll":
		await _held_poll_checks()
	elif _case == "chat_muted":
		await _chat_muted_checks()
	elif _case == "action_cooldown":
		await _action_cooldown_checks()
	elif _case in ["malformed_friends", "malformed_inbox", "malformed_chat", "large_friends"]:
		await _malformed_and_large_checks()
	else:
		await _client_checks()
	print("SOCIAL %s %d checks, %d failures" % ["FAILED" if _failures else "OK", _checks, _failures])
	quit(1 if _failures else 0)


func _oauth_handoff_checks() -> void:
	var client := SocialClient.new()
	root.add_child(client)
	_check(client.configure(_origin, "test-version", _origin, _game_dir) == "", "OAuth social configuration")
	var identity := await client.oauth_restore_identity("Alice")
	_check(identity.get("install") == SocialIdentity.identity("test-version", _game_dir).install \
		and not identity.has("identity_key") and client.token == "", "OAuth bootstrap only reads shared install")
	var restored := {"v": 1, "token": "social-test-token-never-print", "account": {
		"id": "guest:1111111111111111", "name": "Alice", "kind": "guest", "pid": "0123456789abcdef"}, "providers": ["discord"]}
	_check(client.adopt_oauth_session(restored), "accept restored S-A session")
	for i in 300:
		if client.is_signed_in() and not client.busy:
			break
		await process_frame
	_check(client.is_signed_in() and client.me.pid == restored.account.pid and client.friends.size() == 1, "restored token loads same social PID and friends")
	_check(client.oauth_session().id == restored.account.id, "roster identity is retained for linking")
	var invalid := restored.duplicate(true)
	invalid.account.pid = "a".repeat(32)
	_check(not client.adopt_oauth_session(invalid) and client.token == restored.token, "invalid second identity cannot replace social session")
	client.sign_out()
	_check(client.oauth_session().is_empty() and client.friends.is_empty(), "sign-out clears OAuth handoff state")
	client.queue_free()
	await process_frame


func _identity_checks() -> void:
	var first := SocialIdentity.identity("test-version", _game_dir)
	_check(first.has("install") and RegEx.create_from_string("^[0-9a-f]{32}$").search(str(first.install)) != null, "creates game-owned identity")
	var second := SocialIdentity.identity("test-version", _game_dir)
	_check(second.get("install", "") == first.get("install", ""), "reuses game-owned identity")
	_check(SocialIdentity._device_hash("test-machine") == "e1335feb0e81f4fb77a4d54489b0573532c99105ce3d01de8080d04d18b0a0cb",
		"uses the WP-C device hash")
	var configure_dir := _game_dir.path_join("configure-only")
	DirAccess.make_dir_recursive_absolute(configure_dir)
	var configure_client := SocialClient.new()
	root.add_child(configure_client)
	_check(configure_client.configure(_origin, "test-version", _origin, configure_dir).is_empty(),
		"configures without resolving identity")
	_check(not FileAccess.file_exists(configure_dir.path_join("install_id.txt")),
		"configure does not create shared identity")
	_check(not configure_client._valid_id("0123456789abcdef\n")
		and not configure_client._valid_friend_code("ABCD-EFGH-JK\n")
		and not configure_client._valid_recovery("0123456789ABCDEF\n")
		and RegEx.create_from_string(SocialClient.TOKEN_PATTERN).search("synthetic-token\n") == null,
		"identifiers and credentials reject trailing newlines")
	_check(not configure_client.configure("http://example.com", "test-version", _origin, configure_dir).is_empty()
		and not configure_client._configured, "invalid reconfiguration disables the previous lobby")
	configure_client.queue_free()
	var invalid_origin_client := SocialClient.new()
	root.add_child(invalid_origin_client)
	_check(not invalid_origin_client.configure("http://example.com", "test-version", _origin, configure_dir).is_empty(),
		"rejects non-loopback HTTP origins")
	invalid_origin_client.queue_free()
	var malformed_dir := _game_dir.path_join("malformed")
	DirAccess.make_dir_recursive_absolute(malformed_dir)
	var malformed := FileAccess.open(malformed_dir.path_join("install_id.txt"), FileAccess.WRITE)
	malformed.store_string("not-an-install\n")
	malformed.close()
	var refused := SocialIdentity.identity("test-version", malformed_dir)
	_check(refused.has("error"), "refuses malformed identity")
	_check(FileAccess.get_file_as_string(malformed_dir.path_join("install_id.txt")) == "not-an-install\n",
		"does not overwrite malformed identity")
	var oversized_dir := _game_dir.path_join("oversized")
	DirAccess.make_dir_recursive_absolute(oversized_dir)
	var oversized_path := oversized_dir.path_join("install_id.txt")
	var oversized := FileAccess.open(oversized_path, FileAccess.WRITE)
	oversized.store_string("a".repeat(35))
	oversized.close()
	var oversized_result := SocialIdentity.identity("test-version", oversized_dir)
	_check(oversized_result.has("error") and FileAccess.get_file_as_string(oversized_path) == "a".repeat(35),
		"refuses and preserves oversized identity")
	var lock_dir := _game_dir.path_join("contended")
	DirAccess.make_dir_recursive_absolute(lock_dir)
	DirAccess.make_dir_absolute(lock_dir.path_join(".install_id.lock"))
	var locked := SocialIdentity.identity("test-version", lock_dir)
	_check(locked.get("code", "") == "identity_unavailable", "reports shared identity lock contention")
	DirAccess.remove_absolute(lock_dir.path_join(".install_id.lock"))


func _client(game_dir: String = "") -> SocialClient:
	var client := SocialClient.new()
	root.add_child(client)
	await process_frame
	var error := client.configure(_origin, "test-version", _origin, game_dir if game_dir != "" else _game_dir)
	_check(error.is_empty(), "configures dedicated social policy")
	return client


func _wait_until(condition: Callable, seconds := 8.0) -> bool:
	var deadline := Time.get_ticks_msec() + int(seconds * 1000.0)
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await process_frame
	return condition.call()


func _client_checks() -> void:
	var client := await _client()
	var problems: Array[String] = []
	var busy_states: Array[bool] = []
	var outcomes: Array[Array] = []
	client.problem.connect(func(message: String) -> void: problems.append(message))
	client.changed.connect(func() -> void: busy_states.append(client.busy))
	client.action_done.connect(func(kind: String, success: bool) -> void: outcomes.append([kind, success]))
	client.sign_in("Alice")
	_check(await _wait_until(func() -> bool: return client.is_signed_in() and client._seq.ends_with(":1")), "signs in and starts inbox")
	if not client.is_signed_in():
		var request_states: Array[int] = []
		for context in client._requests.values():
			var request: HTTPRequest = context.get("node")
			request_states.append(request.get_http_client_status() if request != null else -1)
		print("SOCIAL SAFE SIGN_IN error=%s busy=%s configured=%s epoch=%s identity_code=%s has_install=%s requests=%s states=%s" % [
			client.last_error, client.busy, client._configured, client._epoch,
			client._identity.get("code", ""), client._identity.has("install"), client._requests.size(), request_states,
		])
	_check(not client.snapshot.requests.is_empty() and not client.snapshot.notices.is_empty(), "accepts initial inbox snapshot")

	client.request_friend("invalid-code")
	_check(outcomes == [["friend_request", false]], "immediate action validation reports failure")
	outcomes.clear()
	client.request_friend("ABCD-EFGH-JK")
	_check(await _wait_until(func() -> bool: return not client.busy), "friend request action completes")
	_check(outcomes.pop_back() == ["friend_request", true], "friend request reports success")
	client.respond_request("0123456789abcdef", true)
	_check(await _wait_until(func() -> bool: return not client.busy), "request response action completes")
	_check(outcomes.pop_back() == ["request_accept", true], "request response reports success")
	client.remove_friend("0123456789abcdef")
	_check(await _wait_until(func() -> bool: return not client.busy), "remove friend action completes")
	_check(outcomes.pop_back() == ["remove_friend", true], "remove friend reports success")
	client.decline_invite("fedcba9876543210", true)
	_check(await _wait_until(func() -> bool: return not client.busy), "invite decline action completes")
	_check(outcomes.pop_back() == ["invite_decline", true], "invite decline reports success")

	var recovery: Array[String] = [""]
	client.recovery_ready.connect(func(code: String) -> void: recovery[0] = code)
	client.create_recovery()
	_check(await _wait_until(func() -> bool: return not client.busy), "recovery action completes")
	_check(outcomes.pop_back() == ["recovery", true], "recovery reports success")
	if recovery[0] == "":
		print("SOCIAL SAFE RECOVERY error=%s busy=%s" % [client.last_error, client.busy])
	_check(recovery[0] == "0123456789ABCDEF", "emits recovery code once")

	var chat_result: Array[Dictionary] = [{}]
	client.chat_ready.connect(func(pid: String, result: Dictionary) -> void:
		if pid == "fedcba9876543210":
			chat_result[0] = result)
	client.get_chat("fedcba9876543210", 0)
	_check(await _wait_until(func() -> bool: return not client.busy and not chat_result[0].is_empty()), "gets chat")
	_check(outcomes.pop_back() == ["chat_get", true], "chat read reports success")
	if chat_result[0].is_empty():
		print("SOCIAL SAFE CHAT error=%s busy=%s" % [client.last_error, client.busy])
	_check(chat_result[0].get("after", -1) == 0 and chat_result[0].has("messages") and chat_result[0].has("next")
		and chat_result[0].has("last_id") and chat_result[0].has("read_id"), "preserves chat pagination shape")

	client.send_chat("fedcba9876543210", "hello")
	_check(await _wait_until(func() -> bool: return not client.busy), "failed chat send returns")
	_check(outcomes.pop_back() == ["chat_send", false], "failed chat send reports failure")
	client.send_chat("fedcba9876543210", "hello")
	_check(await _wait_until(func() -> bool: return not client.busy), "replayed chat send returns")
	_check(outcomes.pop_back() == ["chat_send", true], "matching chat ack reports success")
	client.mark_chat_read("fedcba9876543210", 1)
	_check(await _wait_until(func() -> bool: return not client.busy), "marks chat read")
	_check(await _wait_until(func() -> bool: return client._seq.ends_with(":3")), "continues sequential inbox polling")
	_check(client.snapshot.requests.is_empty() and client.snapshot.notices.is_empty(), "replaces arrays when reset is false")
	_check(busy_states.has(true) and busy_states.has(false), "emits changed on busy transitions")

	client.set_status("away", false)
	_check(await _wait_until(func() -> bool: return not client.busy and not client.is_signed_in()), "403 clears session and presence")
	_check(client._identity.is_empty(), "session drop clears in-memory identity")
	_check(not problems.is_empty() and not problems.back().contains("server-only-secret"), "server error text is not displayed")
	client.sign_out()
	_check(client._identity.is_empty(), "sign out clears in-memory identity")


func _redirect_checks() -> void:
	var client := await _client()
	client.sign_in("Alice")
	_check(await _wait_until(func() -> bool: return not client.busy), "redirect request finishes")
	_check(not client.is_signed_in(), "redirect is refused before credential forwarding")
	client.sign_out()


func _lock_retry_checks() -> void:
	var lock_path := _game_dir.path_join(".install_id.lock")
	DirAccess.make_dir_absolute(lock_path)
	var client := await _client()
	var release := create_timer(0.15)
	release.timeout.connect(func() -> void:
		DirAccess.remove_absolute(lock_path)
	)
	client.sign_in("Alice")
	_check(await _wait_until(func() -> bool: return client.is_signed_in() and not client.busy),
		"retries shared identity lock contention")
	client.sign_out()


func _late_poll_checks() -> void:
	var client := await _client()
	var changed_count: Array[int] = [0]
	client.changed.connect(func() -> void: changed_count[0] += 1)
	client.sign_in("Alice")
	_check(await _wait_until(func() -> bool: return client.is_signed_in() and client._seq != ""), "late poll session starts")
	_check(changed_count[0] > 0, "late poll signal counter observes state changes")
	await process_frame
	client.sign_out()
	var after_sign_out: int = changed_count[0]
	await create_timer(2.5).timeout
	_check(not client.is_signed_in() and client._seq == "", "sign out clears late poll state")
	_check(changed_count[0] == after_sign_out, "late poll cannot change state after sign out")


## ONLINEMENU-1 (BATCH-P3): a lobby whose /v1/health names no "social" (the deployed lobby before the social routes): sign-in stops before
## any write with "not available on this server yet", the client stays signed out and idle (test_launcher_social counts the requests)
func _no_social_checks() -> void:
	var client := await _client()
	var problems: Array[String] = []
	client.problem.connect(func(message: String) -> void: problems.append(message))
	client.sign_in("Alice")
	_check(await _wait_until(func() -> bool: return not client.busy), "no-social sign-in ends busy state")
	_check(not client.is_signed_in() and client.token.is_empty(), "no-social lobby is not signed in to")
	_check(client.unsupported, "no-social lobby is marked unsupported")
	_check(not problems.is_empty() and problems.back() == SocialClient.UNSUPPORTED_TEXT, "no-social lobby says it is not available yet")
	await create_timer(1.5).timeout
	_check(client._requests.is_empty(), "no-social lobby is not polled")


func _signin_retry_checks() -> void:
	var client := await _client()
	client.sign_in("Alice")
	_check(await _wait_until(func() -> bool: return not client.busy), "failed sign-in ends busy state")
	_check(not client.is_signed_in(), "failed sign-in does not retain partial credentials")
	if _case == "signin_auth_429":
		_check(client._rate_limit_remaining() > 0.0, "sign-in 429 starts cooldown")
		client.sign_in("Alice")
		_check(not client.busy and not client.is_signed_in(), "cooldown rejects early sign-in")
		await create_timer(1.1).timeout
	client.sign_in("Alice")
	var retry_succeeded := await _wait_until(func() -> bool: return client.is_signed_in() and not client.busy)
	_check(retry_succeeded, "sign-in retry succeeds")
	if not retry_succeeded:
		print("SOCIAL SAFE SIGNIN_RETRY error=", client.last_error, " busy=", client.busy,
			" signed_in=", client.is_signed_in(), " configured=", client._configured,
			" epoch=", client._epoch, " requests=", client._requests.size(),
			" expected_pid=", not client._expected_pid.is_empty(), " token_present=", not client.token.is_empty())
	client.sign_out()


func _refresh_retry_checks() -> void:
	var client := await _client()
	var outcomes: Array[Array] = []
	var problems: Array[String] = []
	client.action_done.connect(func(kind: String, success: bool) -> void: outcomes.append([kind, success]))
	client.problem.connect(func(message: String) -> void: problems.append(message))
	client.sign_in("Alice")
	_check(await _wait_until(func() -> bool: return client.is_signed_in() and client._seq != ""), "refresh fixture signs in")
	_check(await _wait_until(func() -> bool: return not client.busy and client._refresh_friends and not problems.is_empty()),
		"failed friends refresh is reported")
	_check(client.snapshot.presence.size() == 2, "failed friends refresh preserves the last good inbox snapshot")
	_check(await _wait_until(func() -> bool: return client.friends.size() == 2 and not client._refresh_friends, 14.0),
		"friends refresh recovers after failure and backoff")
	client.set_status("away", true)
	_check(await _wait_until(func() -> bool: return not client.busy), "hidden status acknowledgement completes")
	_check(client.me.get("hidden") == true, "successful status persists hidden state")
	_check(outcomes.back() == ["status", true], "status acknowledgement reports success")
	client.sign_out()


func _held_poll_checks() -> void:
	var client := await _client()
	client.sign_in("Alice")
	_check(await _wait_until(func() -> bool: return client.is_signed_in() and client._seq.ends_with(":2"), 33.0),
		"held inbox poll completes after 25 seconds")
	client.sign_out()


func _chat_muted_checks() -> void:
	root.size = Vector2i(1280, 720)
	var host := Control.new()
	host.theme = Style.make_theme()
	root.add_child(host)
	var screen := Screen.new()
	screen.build("Open-BFME/openbfme-dev")
	host.add_child(screen)
	screen.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var client := await _client()
	screen.bind_client(client, host)
	var outcomes: Array[Array] = []
	client.action_done.connect(func(kind: String, success: bool) -> void:
		if kind == "chat_send":
			outcomes.append([kind, success]))
	client.sign_in("Alice")
	_check(await _wait_until(func() -> bool: return client.is_signed_in() and not client.busy),
		"chat-muted fixture signs in")
	screen._open_chat("fedcba9876543210", "Friend")
	_check(await _wait_until(func() -> bool: return not screen._chat_loading), "real screen loads chat through client")
	screen._chat_text.text = "retry this"
	screen._send_chat()
	_check(await _wait_until(func() -> bool: return outcomes.size() == 1), "chat-muted failure is reported")
	_check(outcomes[0] == ["chat_send", false] and client.is_signed_in()
		and screen._chat_text.text == "retry this", "chat-muted retains session and screen draft")
	screen._send_chat()
	_check(await _wait_until(func() -> bool: return outcomes.size() == 2), "mismatched acknowledgement completes")
	_check(outcomes[1] == ["chat_send", false] and screen._chat_text.text == "retry this",
		"mismatched acknowledgement retains screen draft")
	screen._send_chat()
	_check(await _wait_until(func() -> bool: return outcomes.size() == 3), "matching chat acknowledgement completes")
	_check(outcomes[2] == ["chat_send", true] and screen._chat_text.text == "",
		"real screen clears draft only after matching success")
	client.sign_out()
	host.queue_free()


func _action_cooldown_checks() -> void:
	var client := await _client()
	var outcomes: Array[Array] = []
	client.action_done.connect(func(kind: String, success: bool) -> void: outcomes.append([kind, success]))
	client.sign_in("Alice")
	_check(await _wait_until(func() -> bool: return client.is_signed_in() and not client.busy),
		"cooldown fixture signs in")
	client.request_friend("ABCD-EFGH-JK")
	_check(await _wait_until(func() -> bool: return outcomes.size() == 1), "rate-limited mutation completes")
	_check(outcomes[0] == ["friend_request", false] and client._rate_limit_remaining() > 60.0,
		"long Retry-After is retained after action failure")
	client.request_friend("ABCD-EFGH-JK")
	_check(outcomes.size() == 2 and outcomes[1] == ["friend_request", false],
		"manual mutation is refused during cooldown")
	client.sign_out()


func _malformed_and_large_checks() -> void:
	var client := await _client()
	if _case == "malformed_friends":
		client.sign_in("Alice")
		_check(await _wait_until(func() -> bool: return not client.busy), "malformed friends sign-in finishes")
		_check(client.is_signed_in() and client.friends.is_empty(), "malformed friend row is skipped without losing the session")
	elif _case == "malformed_inbox":
		var problems: Array[String] = []
		client.problem.connect(func(message: String) -> void: problems.append(message))
		_check(not client._valid_id(123), "numeric identifier is rejected without coercion")
		_check(client._valid_int(1.0, 1, 1), "integral JSON number is accepted")
		_check(not client._valid_int(1.5, 0), "fractional JSON number is rejected")
		_check(not client._valid_int("1", 0), "numeric string is rejected")
		_check(not client._validate_presence({"pid": "fedcba9876543210", "state": "online", "game": 123,
			"joinable": false, "since": 1, "map": null, "mode": null}).ok,
			"presence game requires a hexadecimal id or null")
		client.sign_in("Alice")
		_check(await _wait_until(func() -> bool: return not problems.is_empty()), "malformed inbox is rejected")
		_check(client.snapshot.presence.is_empty(), "private inbox identity does not replace snapshot")
	elif _case == "malformed_chat":
		client.sign_in("Alice")
		_check(await _wait_until(func() -> bool: return client.is_signed_in() and not client.busy),
			"malformed chat fixture signs in")
		var row := {"id": 2, "from": "fedcba9876543210", "to": "0123456789abcdef",
			"text": "Message", "at": 1700000000, "client_id": "b".repeat(32)}
		var valid_page := {"messages": [row], "next": null, "last_id": 2, "read_id": 1}
		_check(client._validate_chat(valid_page, 1, "fedcba9876543210").ok,
			"accepts an ordered chat page for the signed-in pair")
		var valid_next := {"messages": [row], "next": 2, "last_id": 3, "read_id": 1}
		_check(client._validate_chat(valid_next, 1, "fedcba9876543210").ok,
			"accepts a consistent next cursor when more rows remain")
		var stale := client._validate_chat(valid_page, 2, "fedcba9876543210")
		_check(stale.ok and stale.result.messages.is_empty(),
			"skips a chat row at or before the requested cursor")
		var wrong_pair := row.duplicate(true)
		wrong_pair.from = "1111111111111111"
		var unrelated := client._validate_chat({"messages": [wrong_pair], "next": null,
			"last_id": 2, "read_id": 1}, 1, "fedcba9876543210")
		_check(unrelated.ok and unrelated.result.messages.is_empty(), "skips messages outside the signed-in conversation")
		_check(not client._validate_chat({"messages": [row], "next": null,
			"last_id": 2, "read_id": 3}, 1, "fedcba9876543210").ok,
			"rejects read cursors beyond the conversation")
		var outcomes: Array[Array] = []
		client.action_done.connect(func(kind: String, success: bool) -> void: outcomes.append([kind, success]))
		client.get_chat("fedcba9876543210", 0)
		_check(await _wait_until(func() -> bool: return outcomes.size() == 1), "malformed chat response completes")
		_check(outcomes[0] == ["chat_get", false], "inconsistent chat cursor is rejected")
	else:
		client.sign_in("Alice")
		_check(await _wait_until(func() -> bool: return client.is_signed_in() and not client.busy and client.friends.size() == 200),
			"valid friends response larger than 64 KiB is accepted")
	client.sign_out()
