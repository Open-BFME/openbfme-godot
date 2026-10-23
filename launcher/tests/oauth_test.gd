extends SceneTree

const OAuth := preload("res://scripts/core/oauth_account.gd")
const PID := "0123456789abcdef"
const ACCOUNT := "guest:1111111111111111"
const INSTALL := "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
var _failed := 0
var _checks := 0


func _initialize() -> void:
	call_deferred("_run")


func _check(ok: bool, label: String) -> void:
	_checks += 1
	if not ok:
		_failed += 1
		push_error("OAUTH FAIL: " + label)


func _reply(core: Node, body: Dictionary) -> void:
	core._on_request(HTTPRequest.RESULT_SUCCESS, 200, PackedStringArray(), JSON.stringify(body).to_utf8_buffer())


func _authorization(core: Node) -> String:
	return "https://discord.com/oauth2/authorize?client_id=synthetic&redirect_uri=%s&response_type=code&scope=identify&state=%s&code_challenge=%s&code_challenge_method=S256&prompt=consent" % [
		"http://127.0.0.1:43827/oauth/callback".uri_encode(), core._state, core._base64url(core._sha256(core._verifier.to_utf8_buffer()))]


func _connect() -> StreamPeerTCP:
	var socket := StreamPeerTCP.new()
	socket.connect_to_host("127.0.0.1", OAuth.CALLBACK_PORT)
	for i in 100:
		socket.poll()
		if socket.get_status() == StreamPeerTCP.STATUS_CONNECTED:
			break
		await process_frame
	return socket


func _run() -> void:
	var core := OAuth.new()
	root.add_child(core)
	_check(core.configure("https://lobby.example.org") == "", "strict HTTPS origin")
	for origin in ["http://example.org", "https://user@lobby.example.org", "https://lobby.example.org/path", "https://lobby.example.org?query=1"]:
		_check(core.configure(origin) != "" and not core._valid_origin(), "unsafe origin refused")
	core.configure("https://lobby.example.org")
	var requests: Array = []
	var opened: Array = []
	var sessions: Array = []
	var accounts: Array = []
	core._transport_override = func(url: String, headers: PackedStringArray, _method: int, body: String) -> int:
		requests.append({"url": url, "headers": headers, "body": JSON.parse_string(body) if body != "" else {}})
		return OK
	core._browser_override = func(url: String) -> int:
		opened.append(url)
		return OK
	core.session_ready.connect(func(response: Dictionary) -> void: sessions.append(response))
	core.completed.connect(func(pid: String, providers: Array) -> void: accounts.append({"pid": pid, "providers": providers}))
	core.refresh()
	_check(requests.is_empty(), "unsigned accounts never guest-signs in or invents identity")
	for provider in ["discord"]:
		core.set_session({"token": "synthetic-social-token", "pid": PID, "id": ACCOUNT, "install": INSTALL})
		core.begin(provider, "link")
		_check(requests.back().url.ends_with("/v1/auth/oauth/start"), "link uses current social session directly")
		# ONLINEMENU-1 (PR #8 review, finding 2): the session's install goes with a link (the lobby's proof; the token no longer shows it)
		_check(requests.back().headers.has("Authorization: Bearer synthetic-social-token") and requests.back().body.get("install") == INSTALL
			and not requests.back().body.has("recovery"), "link is bearer-bound and names the session's install")
		var url := _authorization(core)
		_check(core._allowed_authorization(url) and not core._allowed_authorization(url + "&state=duplicate"), "constrained authorization URL")
		_reply(core, {"v": 1, "pid": PID, "transaction": "1".repeat(48), "authorization_url": url})
		_check(opened.back() == url, "opens constrained system browser URL")
		var good := "GET /oauth/callback?code=synthetic-code&state=%s HTTP/1.1\r\nHost: 127.0.0.1:43827\r\n\r\n" % core._state
		_check(core._parse_callback(good).ok, "correct callback")
		for bad in [good.replace("/oauth/callback?", "/wrong?"), good.replace("127.0.0.1:43827", "evil.example"),
			good.replace("state=", "state=wrong"), good.replace(" HTTP/1.1", "&code=duplicate HTTP/1.1"),
			good.replace("\r\n\r\n", "\r\nOrigin: https://evil.example\r\n\r\n")]:
			_check(not core._parse_callback(bad).ok, "malformed callback refused")
		_check(not core._parse_callback(good.replace(" HTTP/1.1", "&scope=openid HTTP/1.1")).ok, "extra callback values refused")
		# ONLINEMENU-1 (finding 5): local connections that send nothing, or come after the browser's, do not knock the real callback out
		var probes: Array = []
		for i in 3:
			probes.append(await _connect())
		var socket: StreamPeerTCP = await _connect()
		_check(socket.get_status() == StreamPeerTCP.STATUS_CONNECTED, "real loopback callback connected")
		probes.append(await _connect())
		for i in 5:
			await process_frame
		socket.put_data(good.to_utf8_buffer())
		for i in 100:
			await process_frame
			if requests.back().url.ends_with("/v1/auth/oauth/finish"):
				break
		_check(requests.back().url.ends_with("/v1/auth/oauth/finish") and not core._server.is_listening(), "one callback closes listener and finishes")
		_check(requests.back().body.code_verifier.length() >= 43, "PKCE proof submitted")
		_reply(core, {"v": 1, "token": "fresh-social-token", "account": {"id": ACCOUNT, "name": "Alice", "kind": "guest", "pid": PID}, "providers": [provider]})
		_check(sessions.back().account.pid == PID and sessions.back().account.id == ACCOUNT and not core.busy, "link preserves 16-hex PID and roster identity")
		socket.disconnect_from_host()
		for probe in probes:
			(probe as StreamPeerTCP).disconnect_from_host()
	core.unlink("discord", "")
	_check(not core.busy, "unlink without the recovery code is not sent")
	core.unlink("discord", "ABCDEFGHJKMNPQRS")
	_check(requests.back().body.get("install") == INSTALL and requests.back().body.get("recovery") == "ABCDEFGHJKMNPQRS",
		"unlink names the session's install and the recovery code")
	_reply(core, {"v": 1, "pid": PID, "providers": []})
	_check(accounts.back().providers.is_empty(), "unlink refreshes linked providers")
	# ONLINEMENU-1 (Sol r2): a confirmation with the linked account makes a recovery code; nothing about the session changes
	var codes: Array = []
	core.recovery_code.connect(func(code: String) -> void: codes.append(code))
	core.set_session({"token": "synthetic-social-token", "pid": PID, "id": ACCOUNT, "install": INSTALL})
	core.begin("discord", "confirm")
	_check(requests.back().body.get("mode") == "confirm" and requests.back().body.get("install") == INSTALL, "confirmation names the session's install")
	_reply(core, {"v": 1, "pid": PID, "transaction": "4".repeat(48), "authorization_url": _authorization(core)})
	core._server.stop()
	core._finish_oauth("synthetic-code", core._state)
	var sessions_before := sessions.size()
	_reply(core, {"v": 1, "pid": PID, "providers": ["discord"], "code": "ABCDEFGHJKMNPQRS"})
	_check(codes == ["ABCDEFGHJKMNPQRS"] and sessions.size() == sessions_before and core._token == "synthetic-social-token" and not core.busy,
		"confirmation hands over the new recovery code once and keeps the session")
	core.begin("discord", "confirm")
	_reply(core, {"v": 1, "pid": PID, "transaction": "5".repeat(48), "authorization_url": _authorization(core)})
	core._server.stop()
	core._finish_oauth("synthetic-code", core._state)
	_reply(core, {"v": 1, "pid": PID, "providers": ["discord"], "code": "not-a-code", "token": "x"})
	_check(codes.size() == 1 and not core.busy, "a malformed confirmation answer is refused")
	core.clear_session()
	var identity := {"name": "Alice", "install": "a".repeat(32), "client": {"platform": "linux", "version": "test"}}
	core.begin("discord", "restore", identity)
	_check(requests.back().body.install == identity.install and requests.back().body.mode == "restore", "restore binds shared install without guest allocation")
	_check(not str(requests.back().headers).contains("Authorization"), "restore bootstrap has no bearer")
	_reply(core, {"v": 1, "pid": null, "transaction": "2".repeat(48), "authorization_url": _authorization(core)})
	core._server.stop()
	core._finish_oauth("synthetic-code", core._state)
	_check(not str(requests.back().headers).contains("Authorization"), "restore finish has no bearer")
	_reply(core, {"v": 1, "token": "restored-social-token", "account": {"id": "guest:2222222222222222", "name": "Alice", "kind": "guest", "pid": PID}, "providers": ["discord"]})
	_check(sessions.back().token == "restored-social-token" and sessions.back().account.pid == PID, "new PC adopts same PID session")
	var before := sessions.size()
	core.begin("discord", "link")
	_reply(core, {"v": 1, "pid": PID, "transaction": "3".repeat(48), "authorization_url": _authorization(core)})
	core._server.stop()
	core._finish_oauth("synthetic-code", core._state)
	_reply(core, {"v": 1, "token": "bad", "account": {"id": "guest:2222222222222222", "name": "Alice", "kind": "guest", "pid": "a".repeat(32)}, "providers": ["discord"]})
	_check(sessions.size() == before and not core.busy, "second PID namespace rejected")
	# ONLINEMENU-1 (owner 2026-10-20): e-mail sign-in: the lobby's page opens, the poll secret stays here, the poll collects the result
	core.set_session({"token": "synthetic-social-token", "pid": PID, "id": ACCOUNT, "install": INSTALL})
	core.begin("email", "link")
	var start: Dictionary = requests.back()
	_check(start.url.ends_with("/v1/auth/email/start") and start.body.mode == "link" and start.body.install == INSTALL
		and str(start.body.poll_hash).length() == 64 and not start.body.has("poll"), "e-mail link names the install and only the poll secret's hash")
	_check(start.body.poll_hash == core._sha256(core._email_poll.to_utf8_buffer()).hex_encode(), "the poll hash is the secret's SHA-256")
	var ticket := "7".repeat(48)
	_reply(core, {"v": 1, "ticket": ticket, "url": "https://evil.example/email/" + ticket, "expires": 1})
	_check(not core.busy and opened.back() != "https://evil.example/email/" + ticket, "an e-mail page off the lobby is refused")
	core.begin("email", "link")
	_reply(core, {"v": 1, "ticket": ticket, "url": "https://lobby.example.org/email/" + ticket, "expires": 1})
	_check(opened.back() == "https://lobby.example.org/email/" + ticket and core.busy, "the lobby's e-mail page opens in the browser")
	core._email_poll_now()
	_check(requests.back().url.ends_with("/v1/auth/email/poll") and requests.back().body.poll == core._email_poll and requests.back().body.ticket == ticket,
		"the poll sends the secret to the lobby only")
	_reply(core, {"v": 1, "done": false, "state": "sent"})
	_check(core.busy and core._poll_timer.time_left > 0.0, "a waiting answer polls again later")
	core._poll_timer.stop()
	core._email_poll_now()
	_reply(core, {"v": 1, "done": true, "pid": PID, "providers": ["discord", "email"]})
	_check(accounts.back().providers == ["discord", "email"] and not core.busy and core._email_poll == "", "the e-mail link completes and forgets the secret")
	# moving a player with e-mail: the poll's answer is a sign-in
	core.clear_session()
	core.begin("email", "restore", identity)
	_check(requests.back().body.mode == "restore" and requests.back().body.install == identity.install and not str(requests.back().headers).contains("Authorization"),
		"e-mail restore binds the new install without a bearer")
	_reply(core, {"v": 1, "ticket": ticket, "url": "https://lobby.example.org/email/" + ticket, "expires": 1})
	core._poll_timer.stop()
	core._email_poll_now()
	_reply(core, {"v": 1, "done": true, "token": "email-restored-token", "account": {"id": "guest:3333333333333333", "name": "Alice", "kind": "guest", "pid": PID}, "providers": ["email"]})
	_check(sessions.back().token == "email-restored-token" and not core.busy, "e-mail restore adopts the moved player's session")
	core.begin("discord", "link")
	core.cancel()
	_check(core._state == "" and core._verifier == "" and not core._server.is_listening(), "cancel clears transaction secrets and listener")
	core.clear_session()
	_check(core._token == "" and core._pid == "", "sign-out clears cached session")
	for request in requests:
		_check(not request.url.ends_with("/auth/guest") and not request.body.has("identity_key"), "no second identity mechanism")
	core.queue_free()
	await process_frame
	print("OAUTH %s %d checks" % ["PASS" if _failed == 0 else "FAIL", _checks])
	quit(0 if _failed == 0 else 1)
