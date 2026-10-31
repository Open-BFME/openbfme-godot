extends Node

signal status_changed(message: String)
signal completed(pid: String, providers: Array)
signal session_ready(response: Dictionary)
## ONLINEMENU-1 (Sol r2): a recovery code made by a confirmation with the linked account (mode "confirm"); shown once, never stored
signal recovery_code(code: String)

const NetPolicy := preload("res://scripts/net/net_policy.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")
const CALLBACK_PORT := 43827
const CALLBACK_PATH := "/oauth/callback"
const BODY_CAP := 65536
const FLOW_SECONDS := 300
## ONLINEMENU-1 (owner 2026-10-20): Discord sign-in, and e-mail (the lobby's page and a one-time link; the launcher polls for the result)
const PROVIDERS := ["discord", "email"]
const EMAIL_FLOW_SECONDS := 30 * 60
const EMAIL_POLL_SECONDS := 4.0
## ONLINEMENU-1 (PR #8 review, finding 2): the lobby's refusals of a link or an unlink, in words
const REFUSALS := {
	"recovery_required": "launcher.oauth_account.this_needs_your_player_s_recovery",
	"bad_recovery": "launcher.oauth_account.that_recovery_code_is_not_this",
	"install_proof": "launcher.oauth_account.sign_in_again_on_this_pc",
	"oauth_provider_exists": "launcher.oauth_account.that_account_type_is_already_linked",
	"oauth_conflict": "launcher.oauth_account.that_account_is_linked_to_another",
	"oauth_not_this_player": "launcher.oauth_account.that_is_not_the_account_linked",
	"provider_confirm_required": "launcher.oauth_account.confirm_with_your_linked_account_to",
	"email_in_use": "launcher.oauth_account.that_e_mail_address_belongs_to",
	"email_expired": "launcher.oauth_account.the_e_mail_sign_in_expired",
	"email_unavailable": "launcher.oauth_account.e_mail_sign_in_is_not",
	"identity_banned": "launcher.oauth_account.that_account_belongs_to_a_banned",
	# ONLINEMENU-1 (BATCH-P3): the lobby answers not_found only for a path it does not have (lobby.mjs "unknown path"): a lobby from before
	# account linking
	"not_found": "launcher.oauth_account.discord_and_e_mail_sign_in",
}
const LOBBY_HOST_PATTERN := "^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)*$"

var busy: bool = false
var lobby_url: String = ""
var _http: HTTPRequest
var _server := TCPServer.new()
## ONLINEMENU-1 (PR #8 review, finding 5): every connection to the callback port is read on its own (up to MAX_CALLBACK_CLIENTS, the oldest dropped
## past that); the first that is the real callback wins. One listener socket was replaced by each new connection, so a local process could keep
## knocking the browser's callback out. Each entry: {"socket": StreamPeerTCP, "buffer": PackedByteArray, "deadline": msec}
const MAX_CALLBACK_CLIENTS := 8
var _clients: Array[Dictionary] = []
var _deadline := 0
var _provider := ""
var _mode := ""
var _state := ""
var _verifier := ""
var _token := ""
var _pid := ""
var _account_id := ""
## ONLINEMENU-1 (PR #8 review, finding 2): the install the session was issued to, sent with a link or an unlink (the lobby's proof: the session
## token no longer shows it), and the recovery code an unlink or a second provider needs
var _install := ""
var _recovery := ""
var _restore_identity: Dictionary = {}
var _policy: NetPolicy
var _providers: Array = []
var _request_kind := ""
var _transport_override: Callable
var _browser_override: Callable
var _callback_finishing := false
var _email_ticket := ""
var _email_poll := ""
var _poll_timer: Timer


func _ready() -> void:
	_http = HTTPRequest.new()
	_http.max_redirects = 0
	_http.body_size_limit = BODY_CAP
	_http.timeout = 15.0
	_http.request_completed.connect(_on_request)
	add_child(_http)
	_poll_timer = Timer.new()
	_poll_timer.one_shot = true
	_poll_timer.wait_time = EMAIL_POLL_SECONDS
	_poll_timer.timeout.connect(_email_poll_now)
	add_child(_poll_timer)


func configure(url: String, test_origin: String = "") -> String:
	clear_session()
	lobby_url = ""
	_policy = NetPolicy.new()
	if test_origin != "":
		if NetPolicy.check_test_origin(test_origin) != "":
			return L10n.t("launcher.oauth_account.the_test_lobby_address_is_invalid")
		_policy.test_origin = test_origin
	var parsed_lobby: Dictionary = NetPolicy.parse_url(url.trim_suffix("/"))
	if _policy.set_lobby(url) != "" or not parsed_lobby.ok \
			or RegEx.create_from_string(LOBBY_HOST_PATTERN).search(parsed_lobby.host) == null:
		return L10n.t("launcher.oauth_account.the_lobby_address_is_invalid")
	lobby_url = url.trim_suffix("/")
	return ""


func clear_session() -> void:
	_cleanup()
	_token = ""
	_pid = ""
	_account_id = ""
	_install = ""
	_providers.clear()


func set_session(session: Dictionary) -> void:
	clear_session()
	_token = str(session.get("token", ""))
	_pid = str(session.get("pid", ""))
	_account_id = str(session.get("id", ""))
	_install = str(session.get("install", ""))


## the linked providers the last answer named (a link next to one needs the recovery code)
func providers() -> Array:
	return _providers.duplicate()


func begin(provider: String, mode: String, identity: Dictionary = {}, recovery: String = "") -> void:
	if busy:
		return
	if provider not in PROVIDERS or mode not in ["link", "restore", "confirm"]:
		status_changed.emit(L10n.t("launcher.oauth_account.that_sign_in_choice_is_not"))
		return
	if not _valid_origin():
		status_changed.emit(L10n.t("launcher.oauth_account.set_an_https_lobby_url_before"))
		return
	if mode in ["link", "confirm"] and not _has_session():
		_status(L10n.t("launcher.oauth_account.sign_in_before_linking_an_account"))
		return
	if recovery != "" and not _valid_recovery(recovery):
		_status(L10n.t("launcher.oauth_account.the_recovery_code_is_16_letters"))
		return
	if mode == "restore" and (_token != "" or identity.is_empty()):
		_status(L10n.t("launcher.oauth_account.sign_out_and_provide_this_pc"))
		return
	_restore_identity = identity.duplicate(true) if mode == "restore" else {}
	_recovery = recovery if mode == "link" else ""
	busy = true
	_deadline = Time.get_ticks_msec() + FLOW_SECONDS * 1000
	_provider = provider
	_mode = mode
	_status(L10n.t("launcher.oauth_account.preparing_secure_sign_in"))
	if provider == "email":
		_start_email()
		return
	if _server.listen(CALLBACK_PORT, "127.0.0.1") != OK:
		_finish_error(L10n.t("launcher.oauth_account.the_fixed_sign_in_callback_port"))
		return
	_state = _hex_random(32)
	_verifier = _base64url(Crypto.new().generate_random_bytes(32))
	if _verifier.length() < 43:
		_finish_error(L10n.t("launcher.oauth_account.cannot_create_a_secure_sign_in"))
		return
	_start_oauth()


func refresh() -> void:
	if busy:
		return
	if not _valid_origin() or not _has_session():
		status_changed.emit(L10n.t("launcher.oauth_account.set_an_https_lobby_url_to"))
		return
	busy = true
	_status(L10n.t("launcher.oauth_account.checking_linked_accounts"))
	_accounts()


func unlink(provider: String, recovery: String) -> void:
	if busy:
		return
	if provider not in PROVIDERS:
		status_changed.emit(L10n.t("launcher.oauth_account.that_provider_is_not_available"))
		return
	if not _valid_origin() or not _has_session():
		status_changed.emit(L10n.t("launcher.oauth_account.set_an_https_lobby_url_before_2"))
		return
	if not _valid_recovery(recovery):
		status_changed.emit(L10n.t("launcher.oauth_account.unlinking_needs_this_player_s_recovery"))
		return
	busy = true
	_provider = provider
	_request_kind = "unlink"
	_status(L10n.t("launcher.oauth_account.unlinking_account"))
	_post("/v1/auth/oauth/unlink", {"v": 1, "provider": provider, "install": _install, "recovery": recovery})


func cancel() -> void:
	if not busy:
		return
	_cleanup()
	status_changed.emit(L10n.t("launcher.oauth_account.sign_in_cancelled"))


func _has_session() -> bool:
	return _matches(_token, "^[A-Za-z0-9._~-]{1,4096}\\z") and _valid_pid(_pid) \
		and _matches(_account_id, "^guest:[0-9a-f]{16}\\z") and _matches(_install, "^[0-9a-f]{32}\\z")


func _valid_recovery(value: String) -> bool:
	return _matches(value, "^[0-9A-HJKMNP-TV-Z]{16}\\z")


func _accounts() -> void:
	_request_kind = "accounts"
	_request(_url("/v1/auth/oauth/accounts"), ["Authorization: Bearer " + _token, "Accept: application/json"], HTTPClient.METHOD_GET)


func _start_oauth() -> void:
	_request_kind = "start"
	var body := _restore_identity.duplicate(true)
	body.merge({"v": 1, "provider": _provider, "mode": _mode, "state": _state,
		"code_challenge": _base64url(_sha256(_verifier.to_utf8_buffer()))}, true)
	if _mode in ["link", "confirm"]:
		body["install"] = _install
		if _recovery != "":
			body["recovery"] = _recovery
	_post("/v1/auth/oauth/start", body)


## ONLINEMENU-1: the e-mail flow. The poll secret stays here; the lobby gets its SHA-256 and the browser page only the ticket
func _start_email() -> void:
	_deadline = Time.get_ticks_msec() + EMAIL_FLOW_SECONDS * 1000
	_email_poll = _hex_random(32)
	var body := _restore_identity.duplicate(true)
	body.merge({"v": 1, "mode": _mode, "poll_hash": _sha256(_email_poll.to_utf8_buffer()).hex_encode()}, true)
	if _mode in ["link", "confirm"]:
		body["install"] = _install
		if _recovery != "":
			body["recovery"] = _recovery
	_request_kind = "email_start"
	_post("/v1/auth/email/start", body)


func _email_poll_now() -> void:
	if not busy or _provider != "email" or _email_ticket == "":
		return
	if Time.get_ticks_msec() > _deadline:
		_finish_error(L10n.t("launcher.oauth_account.the_e_mail_sign_in_timed"))
		return
	_request_kind = "email_poll"
	var raw := JSON.stringify({"v": 1, "ticket": _email_ticket, "poll": _email_poll})
	_request(_url("/v1/auth/email/poll"), ["Content-Type: application/json", "Accept: application/json"], HTTPClient.METHOD_POST, raw)


func _email_result(value: Dictionary) -> void:
	if value.get("done") != true:
		if not _only(value, ["v", "done", "state"]) or value.get("state") not in ["waiting", "sent"]:
			_finish_error(L10n.t("launcher.oauth_account.the_lobby_returned_an_invalid_e"))
			return
		_status(L10n.t("launcher.oauth_account.check_your_inbox_and_click_the") if value.state == "sent" else L10n.t("launcher.oauth_account.enter_your_e_mail_address_on"))
		_poll_timer.start()
		return
	if _mode == "confirm":
		_handle_confirm(value)
	elif _mode == "link":
		if not _only(value, ["v", "done", "pid", "providers"]) or value.get("pid") != _pid or not _valid_providers(value.get("providers")):
			_finish_error(L10n.t("launcher.oauth_account.the_lobby_returned_an_invalid_e"))
			return
		_providers = value.providers.duplicate()
		_cleanup()
		completed.emit(_pid, _providers.duplicate())
		_status(L10n.t("launcher.oauth_account.e_mail_address_linked_to_this"))
	else:
		var finished := value.duplicate()
		finished.erase("done")
		_handle_finish(finished)


func _finish_oauth(code: String, state: String) -> void:
	if state != _state:
		_finish_error(L10n.t("launcher.oauth_account.the_browser_sign_in_state_did"))
		return
	_request_kind = "finish"
	_post("/v1/auth/oauth/finish", {"v": 1, "transaction": _queued_transaction, "state": state, "code": code, "code_verifier": _verifier})


var _queued_transaction := ""


func _post(path: String, value: Dictionary) -> void:
	var raw := JSON.stringify(value)
	var headers := ["Content-Type: application/json", "Accept: application/json"]
	if _token != "":
		headers.append("Authorization: Bearer " + _token)
	_request(_url(path), headers, HTTPClient.METHOD_POST, raw)


func _request(url: String, headers: PackedStringArray, method: int, body: String = "") -> void:
	var result: int = OK
	if _transport_override.is_valid():
		result = _transport_override.call(url, headers, method, body)
	else:
		result = _http.request(url, headers, method, body)
	if result != OK:
		_finish_error(L10n.t("launcher.oauth_account.the_lobby_request_could_not_be"))


func _on_request(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	if not busy:
		return
	if code == 401:
		_token = ""
		_pid = ""
		_providers.clear()
	if result == HTTPRequest.RESULT_SUCCESS and code in [403, 404, 409] and body.size() <= BODY_CAP:
		var refusal: Variant = JSON.parse_string(body.get_string_from_utf8())
		var why_id: String = REFUSALS.get(str(refusal.get("error", "")) if refusal is Dictionary else "", "")
		if why_id != "":
			_finish_error(L10n.t(why_id))
			return
	if result != HTTPRequest.RESULT_SUCCESS or code != 200 or body.size() > BODY_CAP:
		_finish_error(L10n.t("launcher.oauth_account.the_lobby_could_not_complete_the"))
		return
	var parser := JSON.new()
	if parser.parse(body.get_string_from_utf8()) != OK or typeof(parser.data) != TYPE_DICTIONARY:
		_finish_error(L10n.t("launcher.oauth_account.the_lobby_returned_an_invalid_account"))
		return
	var value: Dictionary = parser.data
	if value.get("v") != 1:
		_finish_error(L10n.t("launcher.oauth_account.the_lobby_returned_an_unsupported_account"))
		return
	if _request_kind == "accounts":
		_apply_accounts(value)
	elif _request_kind == "start":
		if not _only(value, ["v", "transaction", "authorization_url", "pid"]) \
				or not _matches(value.get("transaction"), "^[0-9a-f]{48}\\z") or typeof(value.get("authorization_url")) != TYPE_STRING \
				or not value.has("pid") or (_mode in ["link", "confirm"] and value.pid != _pid) or (_mode == "restore" and value.pid != null):
			_finish_error(L10n.t("launcher.oauth_account.the_lobby_returned_an_invalid_sign"))
			return
		_queued_transaction = value.transaction
		if not _allowed_authorization(value.authorization_url):
			_finish_error(L10n.t("launcher.oauth_account.the_lobby_returned_an_unsafe_sign"))
			return
		var open_result: int = _browser_override.call(value.authorization_url) if _browser_override.is_valid() else preload("res://scripts/core/external_open.gd").open_url(value.authorization_url)
		if open_result != OK:
			_finish_error(L10n.t("launcher.oauth_account.the_system_browser_could_not_be"))
			return
		_status(L10n.t("launcher.oauth_account.finish_sign_in_in_your_system"))
	elif _request_kind == "email_start":
		var ticket: Variant = value.get("ticket")
		if not _only(value, ["v", "ticket", "url", "expires"]) or not _matches(ticket, "^[0-9a-f]{48}\\z") \
				or value.get("url") != lobby_url + "/email/" + str(ticket):
			_finish_error(L10n.t("launcher.oauth_account.the_lobby_returned_an_invalid_e_2"))
			return
		_email_ticket = ticket
		var open_result: int = _browser_override.call(value.url) if _browser_override.is_valid() else preload("res://scripts/core/external_open.gd").open_url(value.url)
		if open_result != OK:
			_finish_error(L10n.t("launcher.oauth_account.the_system_browser_could_not_be"))
			return
		_status(L10n.t("launcher.oauth_account.enter_your_e_mail_address_on"))
		_poll_timer.start()
	elif _request_kind == "email_poll":
		_email_result(value)
	elif _request_kind == "finish" and _mode == "confirm":
		_handle_confirm(value)
	elif _request_kind == "finish":
		_handle_finish(value)
	elif _request_kind == "unlink":
		_apply_accounts(value)
	else:
		_finish_error(L10n.t("launcher.oauth_account.the_account_action_was_not_recognized"))


func _handle_confirm(value: Dictionary) -> void:
	var fields := ["v", "pid", "providers", "code", "done"] if _provider == "email" else ["v", "pid", "providers", "code"]
	if not _only(value, fields) or value.get("pid") != _pid or not _valid_providers(value.get("providers")) \
			or not value.get("code") is String or not _valid_recovery(value.code):
		_finish_error(L10n.t("launcher.oauth_account.the_lobby_did_not_complete_the"))
		return
	_providers = value.providers.duplicate()
	_cleanup()
	recovery_code.emit(value.code)
	completed.emit(_pid, _providers.duplicate())
	_status(L10n.t("launcher.oauth_account.new_recovery_code_made"))


func _handle_finish(value: Dictionary) -> void:
	var account: Variant = value.get("account")
	if not _only(value, ["v", "token", "account", "providers", "notice"]) \
			or not _matches(value.get("token"), "^[A-Za-z0-9._~-]{1,4096}\\z") or not account is Dictionary \
			or not _only(account, ["id", "name", "kind", "pid"]) \
			or not _valid_pid(account.get("pid")) or not _matches(account.get("id"), "^guest:[0-9a-f]{16}\\z") \
			or account.get("kind") != "guest" or not _valid_name(account.get("name")) \
			or _private_identity(value) \
			or not _valid_providers(value.get("providers")) \
			or (_mode == "link" and (account.pid != _pid or account.id != _account_id)):
		_finish_error(L10n.t("launcher.oauth_account.the_lobby_did_not_complete_sign"))
		return
	_token = value.token
	_pid = account.pid
	_account_id = account.id
	var providers: Array = value.providers.duplicate()
	var restored := _mode == "restore"
	if restored:
		_install = str(_restore_identity.get("install", ""))  # the restored session was issued to this PC's install
	_cleanup()
	session_ready.emit(value)
	_providers = providers
	completed.emit(account.pid, providers)
	_status(L10n.t("launcher.oauth_account.player_restored_on_this_pc") if restored else L10n.t("launcher.oauth_account.account_linked_to_this_player"))


func _apply_accounts(value: Dictionary) -> void:
	if not _only(value, ["v", "pid", "providers"]) or value.get("pid") != _pid or not _valid_providers(value.get("providers")):
		_finish_error(L10n.t("launcher.oauth_account.the_lobby_returned_invalid_linked_accounts"))
		return
	_providers = value.providers.duplicate()
	var unlinked := _request_kind == "unlink"
	_cleanup()
	completed.emit(_pid, _providers.duplicate())
	_status(L10n.t("launcher.oauth_account.account_unlinked") if unlinked else L10n.t("launcher.oauth_account.linked_accounts_checked"))


func _only(value: Dictionary, fields: Array) -> bool:
	for field in value:
		if field not in fields:
			return false
	return true


func _private_identity(value: Variant, depth := 0) -> bool:
	if depth > 8:
		return true
	if value is Dictionary:
		for key in value:
			if key in ["install", "device", "install_id", "device_hash"] or _private_identity(value[key], depth + 1):
				return true
	elif value is Array:
		for entry in value:
			if _private_identity(entry, depth + 1):
				return true
	return false


func _valid_name(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 24 or value.to_utf8_buffer().size() > 128 or value.strip_edges() != value:
		return false
	for c in value:
		if c.unicode_at(0) < 0x20 or c.unicode_at(0) == 0x7f:
			return false
	return true


func _matches(value: Variant, pattern: String) -> bool:
	return value is String and RegEx.create_from_string(pattern).search(value) != null


func _valid_pid(value: Variant) -> bool:
	return _matches(value, "^[0-9a-f]{16}\\z")


func _valid_providers(value: Variant) -> bool:
	if not value is Array or value.size() > 2:
		return false
	var seen := {}
	for provider in value:
		if not provider is String or provider not in PROVIDERS or seen.has(provider):
			return false
		seen[provider] = true
	return true


func _process(_delta: float) -> void:
	if not busy:
		return
	if _callback_finishing:
		return
	if _deadline > 0 and Time.get_ticks_msec() > _deadline:
		_finish_error(L10n.t("launcher.oauth_account.the_browser_sign_in_timed_out"))
		return
	while _server.is_connection_available():
		if _clients.size() >= MAX_CALLBACK_CLIENTS:
			_ignore_probe(_clients.pop_front())
		_clients.append({"socket": _server.take_connection(), "buffer": PackedByteArray(), "deadline": Time.get_ticks_msec() + 10000})
	for client in _clients.duplicate():
		if not busy or _callback_finishing:
			return
		_read_callback(client)


func _read_callback(client: Dictionary) -> void:
	var socket: StreamPeerTCP = client.socket
	socket.poll()
	if socket.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		_clients.erase(client)
		return
	if Time.get_ticks_msec() > int(client.deadline):
		_ignore_probe(client)
		return
	var available := socket.get_available_bytes()
	var buffer: PackedByteArray = client.buffer
	if available > 8192 or buffer.size() + available > 8192:
		_ignore_probe(client)
		return
	if available > 0:
		buffer.append_array(socket.get_data(available)[1])
		client.buffer = buffer  # a packed array in a Dictionary is a copy: stored back
	var text := buffer.get_string_from_utf8()
	if not text.contains("\r\n\r\n"):
		return
	if not text.substr(text.find("\r\n\r\n") + 4).is_empty():
		_ignore_probe(client)
		return
	var callback := _parse_callback(text)
	if not callback.ok:
		_ignore_probe(client)
		return
	_clients.erase(client)
	if callback.get("cancelled", false):
		socket.put_data(_callback_response(L10n.t("launcher.oauth_account.sign_in_was_cancelled_you_may")))
		socket.disconnect_from_host()
		_server.stop()
		_cleanup()
		_status(L10n.t("launcher.oauth_account.sign_in_was_cancelled_in_the"))
		return
	var page := L10n.t("launcher.oauth_account.sign_in_received_you_may_close")
	socket.put_data(_callback_response(page))
	socket.disconnect_from_host()
	_drop_clients()
	_server.stop()
	_callback_finishing = true
	_deadline = 0
	_finish_oauth(callback.code, callback.state)


func _callback_response(page: String) -> PackedByteArray:
	return ("HTTP/1.1 200 OK\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\nContent-Length: %d\r\n\r\n%s" % [page.to_utf8_buffer().size(), page]).to_utf8_buffer()


func _parse_callback(text: String) -> Dictionary:
	var lines := text.split("\r\n")
	if lines.is_empty() or not lines[0].begins_with("GET "):
		return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_was_malformed")}
	var request_parts := lines[0].split(" ")
	if request_parts.size() != 3 or request_parts[2] != "HTTP/1.1":
		return {"ok": false}
	var target := request_parts[1]
	var target_parts := target.split("?")
	if target_parts.size() != 2 or target_parts[0] != CALLBACK_PATH:
		return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_path_was_not")}
	var host := ""
	var query := {}
	var seen_headers := {}
	for line in lines.slice(1):
		if line == "":
			break
		var pair := line.split(":", true, 1)
		if pair.size() != 2:
			return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_headers_were_malformed")}
		var name := pair[0].to_lower()
		if seen_headers.has(name):
			return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_had_duplicate_headers")}
		seen_headers[name] = true
		if name == "host":
			host = pair[1].strip_edges()
		elif name == "origin":
			return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_origin_was_unexpected")}
		elif name == "content-length":
			if pair[1].strip_edges() != "0":
				return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_must_not_contain")}
		elif name == "transfer-encoding":
			return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_must_not_contain")}
	var host_ok := host == "127.0.0.1:%d" % CALLBACK_PORT
	if not host_ok:
		return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_host_was_not")}
	for part in target_parts[1].split("&"):
		var kv := part.split("=", true, 1)
		if kv.size() != 2 or query.has(kv[0].uri_decode()):
			return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_had_duplicate_or")}
		var name := kv[0].uri_decode()
		var value := kv[1].uri_decode()
		if name in query:
			return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_had_duplicate_values")}
		query[name] = value
	var allowed := ["state", "code", "error"]
	for name in query:
		if name not in allowed or query[name].length() > 2048:
			return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_contained_an_unsupported")}
	if query.get("state", "") != _state:
		return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_did_not_contain")}
	if query.get("error", "") == "access_denied":
		if query.has("code") or query.size() != 2:
			return {"ok": false}
		return {"ok": true, "cancelled": true}
	if query.has("error") or not query.has("code") or query.size() < 2 or not RegEx.create_from_string("^[\\x21-\\x7e]{1,2048}$").search(query.code):
		return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_did_not_contain")}
	if query.size() != 2:
		return {"ok": false, "error": L10n.t("launcher.oauth_account.the_browser_callback_did_not_contain")}
	return {"ok": true, "code": query.code, "state": query.state}


func _ignore_probe(client: Dictionary) -> void:
	_clients.erase(client)
	var socket: StreamPeerTCP = client.socket
	var body := "Not found."
	socket.put_data(("HTTP/1.1 404 Not Found\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\nContent-Length: %d\r\n\r\n%s" % [body.length(), body]).to_utf8_buffer())
	socket.disconnect_from_host()


func _drop_clients() -> void:
	for client in _clients:
		var socket: StreamPeerTCP = client.socket
		socket.disconnect_from_host()
	_clients.clear()


func _valid_origin() -> bool:
	return _policy != null and lobby_url != "" and _policy.set_lobby(lobby_url) == ""


func _url(path: String) -> String:
	return lobby_url.trim_suffix("/") + path


func _allowed_authorization(url: String) -> bool:
	var prefix := "https://discord.com/oauth2/authorize?"
	if not url.begins_with(prefix):
		return false
	var params := {}
	for part in url.substr(prefix.length()).split("&"):
		var pair := part.split("=", true, 1)
		if pair.size() != 2:
			return false
		var name := pair[0].uri_decode()
		if params.has(name):
			return false
		params[name] = pair[1].uri_decode()
	for name in params:
		if name not in ["client_id", "redirect_uri", "response_type", "scope", "state", "code_challenge", "code_challenge_method", "prompt"]:
			return false
	return params.get("redirect_uri") == "http://127.0.0.1:43827/oauth/callback" \
		and params.get("response_type") == "code" \
		and params.get("scope") == "identify" \
		and params.get("prompt") == "consent" \
		and params.get("state") == _state \
		and params.get("code_challenge_method") == "S256" \
		and params.get("code_challenge") == _base64url(_sha256(_verifier.to_utf8_buffer())) \
		and params.get("client_id", "") != ""


func _sha256(value: PackedByteArray) -> PackedByteArray:
	var context := HashingContext.new()
	if context.start(HashingContext.HASH_SHA256) != OK or context.update(value) != OK:
		return PackedByteArray()
	return context.finish()


func _hex_random(bytes: int) -> String:
	return Crypto.new().generate_random_bytes(bytes).hex_encode()


func _base64url(bytes: PackedByteArray) -> String:
	return Marshalls.raw_to_base64(bytes).replace("+", "-").replace("/", "_").trim_suffix("=").trim_suffix("=").trim_suffix("=")


func _status(message: String) -> void:
	status_changed.emit(message)


func _finish_error(message: String) -> void:
	_cleanup()
	status_changed.emit(message)


func _cleanup() -> void:
	if _http != null:
		_http.cancel_request()
	_drop_clients()
	_server.stop()
	_callback_finishing = false
	_state = ""
	_verifier = ""
	_queued_transaction = ""
	_deadline = 0
	_recovery = ""
	_email_ticket = ""
	_email_poll = ""
	if _poll_timer != null:
		_poll_timer.stop()
	_restore_identity.clear()
	_request_kind = ""
	busy = false
