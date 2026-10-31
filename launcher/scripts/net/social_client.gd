extends Node

const NetPolicy := preload("res://scripts/net/net_policy.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")
const SocialIdentity := preload("res://scripts/core/social_identity.gd")
const LobbyJSON := preload("res://scripts/net/lobby_json.gd")

signal changed()
signal problem(message: String)
signal recovery_ready(code: String)
signal chat_ready(pid: String, result: Dictionary)
signal connection_changed(connected: bool)
signal action_done(kind: String, success: bool)
## ONLINEMENU-1: the sign-in answer's notice text (a moderation notice, or "your player was moved to another PC"), once per notice id
signal notice(text: String)

const BODY_LIMIT := 1048576
const REQUEST_LIMIT := 16384
const REQUEST_TIMEOUT := 25.0
const HELD_POLL_TIMEOUT := 35.0
const MAX_COLLECTION := 500
const MAX_FRIENDS := 200
const MAX_REQUESTS := 250
const MAX_NOTICES := 50
const MAX_CHAT_HEADS := 200
const MAX_BACKOFF := 10.0
const MAX_RECONNECT := 60.0
const MAX_RETRY_AFTER_SECONDS := 2147483647
const PID_PATTERN := "^[0-9a-f]{16}\\z"
const INSTALL_PATTERN := "^[0-9a-f]{32}\\z"
const DEVICE_PATTERN := "^[0-9a-f]{64}\\z"
const CODE_PATTERN := "^[0-9A-HJKMNP-TV-Z]{10}\\z"
const CODE_DISPLAY_PATTERN := "^[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{2}\\z"
const RECOVERY_PATTERN := "^[0-9A-HJKMNP-TV-Z]{16}\\z"
## the error code -> text id tables; ERROR_MESSAGES / HTTP_ERROR_MESSAGES give the same tables with the texts in the current language
const ERROR_MESSAGE_IDS := {
	"signed_out": "launcher.social_client.your_social_session_expired_sign_in",
	"session_revoked": "launcher.social_client.your_social_session_was_revoked_sign",
	"bad_recovery": "launcher.social_client.the_recovery_code_was_not_accepted",
	"install_required": "launcher.social_client.start_the_game_once_to_create",
	"banned": "launcher.social_client.this_social_account_is_unavailable",
	"suspended": "launcher.social_client.this_social_account_is_temporarily_unavailable",
	"rate_limited": "launcher.social_client.too_many_social_requests_try_again",
	"already_friends": "launcher.social_client.you_are_already_friends",
	"already_requested": "launcher.social_client.that_friend_request_is_already_pending",
	"unavailable": "launcher.social_client.that_social_item_is_no_longer",
	"not_found": "launcher.social_client.that_social_item_was_not_found",
	"chat_muted": "launcher.social_client.chat_is_unavailable_for_this_account",
	"social_unavailable": "launcher.social_client.the_social_service_is_unavailable",
	"social_store_full": "launcher.social_client.the_social_service_cannot_save_more",
}
const HTTP_ERROR_MESSAGE_IDS := {
	429: "launcher.social_client.too_many_social_requests_try_again",
	401: "launcher.social_client.the_social_session_is_no_longer",
	403: "launcher.social_client.the_social_session_is_no_longer",
	404: "launcher.social_client.that_social_item_is_unavailable",
}
static var ERROR_MESSAGES: Dictionary:
	get:
		return _translated(ERROR_MESSAGE_IDS)
static var HTTP_ERROR_MESSAGES: Dictionary:
	get:
		return _translated(HTTP_ERROR_MESSAGE_IDS)
const TOKEN_PATTERN := "^[A-Za-z0-9._~-]{1,4096}\\z"

var me: Dictionary = {}
var friends: Array = []
var snapshot: Dictionary = {
	"presence": [], "requests": [], "invites": [], "notices": [], "chat": [],
	"stats": {"v": 1, "players": 0, "games_open": 0, "games_running": 0, "as_of": 0},
}
var token := ""
var _last_notice := ""
var busy := false
var lobby_url := ""
var last_error := ""
## ONLINEMENU-1 (BATCH-P3 live-lobby check): the lobby's /v1/health names no "social" feature (the deployed lobby before the social routes):
## sign-in is not offered against it; the screen says so
var unsupported := false
static var UNSUPPORTED_TEXT: String:
	get:
		return L10n.t("launcher.social_client.online_accounts_are_not_available")

var _policy: NetPolicy
var _version := ""
var _game_dir := ""
var _identity: Dictionary = {}
var _signin_body := ""  ## the guest sign-in sent once /v1/health has named the "social" feature
var _configured := false
var _epoch := 0
var _next_request := 1
var _requests: Dictionary = {}
var _poll_id := 0
var _seq := ""
var _poll_timer: Timer
var _reconnect_attempt := 0
var _connected := false
var _refresh_friends := false
var _expected_pid := ""
var _roster_id := ""
var _chat_retries: Dictionary = {}
var _rate_limit_until_msec := 0
var _poll_not_before_msec := 0


func _ready() -> void:
	_poll_timer = Timer.new()
	_poll_timer.one_shot = true
	_poll_timer.timeout.connect(_poll_tick)
	add_child(_poll_timer)


func configure(origin: String, version: String, test_origin: String = "", game_dir: String = "") -> String:
	var previous_origin := lobby_url
	if _configured:
		sign_out()
	_configured = false
	_policy = null
	lobby_url = ""
	_version = ""
	_game_dir = ""
	_identity.clear()
	if version.is_empty() or version.length() > 64 or version.strip_edges() != version:
		return L10n.t("launcher.social_client.the_launcher_version_is_invalid")
	for c in version:
		var code := c.unicode_at(0)
		if code < 0x20 or code == 0x7f:
			return L10n.t("launcher.social_client.the_launcher_version_is_invalid")
	var policy := NetPolicy.new()
	if test_origin != "":
		var test_error := NetPolicy.check_test_origin(test_origin)
		if test_error != "":
			return L10n.t("launcher.social_client.the_test_lobby_address_is_invalid")
		policy.test_origin = test_origin
	var lobby_error := policy.set_lobby(origin)
	if lobby_error != "":
		return L10n.t("launcher.social_client.the_lobby_address_is_invalid")
	var parsed := NetPolicy.parse_url(origin.trim_suffix("/"))
	if not parsed.ok:
		return L10n.t("launcher.social_client.the_lobby_address_is_invalid")
	_policy = policy
	lobby_url = str(parsed.origin)
	_version = version
	_game_dir = game_dir
	_configured = true
	if lobby_url != previous_origin:
		_rate_limit_until_msec = 0
		_poll_not_before_msec = 0
	last_error = ""
	return ""


func is_signed_in() -> bool:
	return not token.is_empty() and me.has("pid")


func sign_in(name: String, recover: String = "") -> void:
	if not _configured:
		_problem(L10n.t("launcher.social_client.the_social_lobby_is_not_configured"))
		return
	if busy:
		_problem(L10n.t("launcher.social_client.another_social_action_is_in_progress"))
		return
	if _rate_limit_remaining() > 0.0:
		_problem(L10n.t("launcher.social_client.the_social_lobby_asked_you_to"))
		return
	if not _valid_text(name, 24, 128):
		_problem(L10n.t("launcher.social_client.choose_a_valid_player_name"))
		return
	if recover != "" and not _valid_recovery(recover):
		_problem(L10n.t("launcher.social_client.the_recovery_code_is_invalid"))
		return
	var identity_epoch := _epoch
	_set_busy(true)
	for attempt in range(5):
		_identity = SocialIdentity.identity(_version, _game_dir)
		if not _identity.has("error") or _identity.get("code", "") != "identity_unavailable" or attempt == 4:
			break
		await get_tree().create_timer(0.1).timeout
		if identity_epoch != _epoch:
			return
	if identity_epoch != _epoch:
		return
	if _identity.has("error"):
		_set_busy(false)
		_problem(str(_identity.error))
		return
	if not _valid_install(str(_identity.get("install", ""))) \
			or (_identity.has("device") and not _valid_device(str(_identity.device))):
		_set_busy(false)
		_problem(L10n.t("launcher.social_client.the_game_install_identity_is_invalid"))
		return
	var body: Dictionary = _identity.duplicate(true)
	body["v"] = 1
	body["name"] = name
	if recover != "":
		body["recover"] = recover
	# the lobby's features first: a lobby without the social service is never signed in to (no write on it), it is "not available yet"
	_signin_body = JSON.stringify(body)
	if _send_request("features", HTTPClient.METHOD_GET, "/v1/health", "", false) < 0 and identity_epoch == _epoch:
		_drop_session()


func oauth_session() -> Dictionary:
	# ONLINEMENU-1 (PR #8 review, finding 2): with the install, the lobby's proof for a link or an unlink (the token no longer shows it)
	return {"token": token, "pid": _expected_pid, "id": _roster_id, "install": str(_identity.get("install", ""))} if is_signed_in() else {}


func oauth_restore_identity(name: String) -> Dictionary:
	if not _configured or busy or token != "" or not _valid_text(name, 24, 128):
		_problem(L10n.t("launcher.social_client.choose_a_valid_player_name_before"))
		return {}
	var identity_epoch := _epoch
	_set_busy(true)
	var identity: Dictionary = {}
	for attempt in range(5):
		identity = SocialIdentity.identity(_version, _game_dir)
		if not identity.has("error") or identity.get("code", "") != "identity_unavailable" or attempt == 4:
			break
		await get_tree().create_timer(0.1).timeout
		if identity_epoch != _epoch:
			return {}
	_set_busy(false)
	if identity.has("error") or not _valid_install(identity.get("install", "")) \
			or (identity.has("device") and not _valid_device(identity.device)):
		_problem(L10n.t("launcher.social_client.the_shared_game_install_identity_is"))
		return {}
	identity["name"] = name
	return identity


func adopt_oauth_session(data: Dictionary) -> bool:
	if not _configured or data.get("v") != 1 or _contains_private_identity(data) or not _valid_auth(data):
		_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_oauth"))
		return false
	sign_out()
	# ONLINEMENU-1: this PC's install (the session was issued to it): the proof a recovery code, a link or an unlink needs
	var identity: Dictionary = SocialIdentity.identity(_version, _game_dir)
	if not identity.has("error") and _valid_install(str(identity.get("install", ""))):
		_identity = identity
	_set_busy(true)
	return _handle_action("auth", data, {})


func sign_out() -> void:
	var was_connected := is_signed_in()
	_epoch += 1
	_cancel_requests()
	_poll_id = 0
	if _poll_timer != null:
		_poll_timer.stop()
	token = ""
	_identity.clear()
	me = {}
	friends = []
	snapshot = _empty_snapshot()
	_seq = ""
	_refresh_friends = false
	_reconnect_attempt = 0
	_chat_retries.clear()
	_expected_pid = ""
	_roster_id = ""
	last_error = ""
	_set_busy(false)
	_connected = false
	if was_connected:
		connection_changed.emit(false)
	changed.emit()


func request_friend(code: String) -> void:
	if not _valid_friend_code(code):
		_action_problem("friend_request", L10n.t("launcher.social_client.the_friend_code_is_invalid"))
		return
	_begin_action("friend_request", HTTPClient.METHOD_POST, "/v1/friends/requests", {"v": 1, "code": code})


func respond_request(id: String, accept: bool) -> void:
	if not _valid_id(id):
		_action_problem("request_accept" if accept else "request_decline", L10n.t("launcher.social_client.the_friend_request_is_invalid"))
		return
	_begin_action("request_accept" if accept else "request_decline", HTTPClient.METHOD_POST,
		"/v1/friends/requests/%s/%s" % [id, "accept" if accept else "decline"], {"v": 1})


func remove_friend(pid: String) -> void:
	if not _valid_id(pid):
		_action_problem("remove_friend", L10n.t("launcher.social_client.the_friend_is_invalid"))
		return
	_begin_action("remove_friend", HTTPClient.METHOD_DELETE, "/v1/friends/%s" % pid, {})


func decline_invite(id: String, busy_reply: bool = false) -> void:
	if not _valid_id(id):
		_action_problem("invite_decline", L10n.t("launcher.social_client.the_invitation_is_invalid"))
		return
	var body := {"v": 1}
	if busy_reply:
		body["busy"] = true
	_begin_action("invite_decline", HTTPClient.METHOD_POST, "/v1/invites/%s/decline" % id, body)


func create_recovery(current: String = "") -> void:
	# ONLINEMENU-1 (PR #8 review, finding 2): the install the session was issued to is the lobby's proof for a recovery code; once an account is
	# linked, the current code is needed too (Sol r2), or a confirmation with the linked account (OAuthAccount mode "confirm")
	if not _valid_install(str(_identity.get("install", ""))):
		_problem(L10n.t("launcher.social_client.sign_in_again_on_this_pc"))
		return
	if current != "" and not _valid_recovery(current):
		_problem(L10n.t("launcher.social_client.the_recovery_code_is_16_letters"))
		return
	var body := {"v": 1, "install": _identity.install}
	if current != "":
		body["recovery"] = current
	_begin_action("recovery", HTTPClient.METHOD_POST, "/v1/me/recovery", body)


func set_status(state: String, hidden: bool) -> void:
	if not ["online", "away", "matching"].has(state):
		_action_problem("status", L10n.t("launcher.social_client.the_presence_state_is_invalid"))
		return
	_begin_action("status", HTTPClient.METHOD_POST, "/v1/me/status",
		{"v": 1, "state": state, "hidden": hidden}, {"hidden": hidden})


func get_chat(pid: String, after: int = 0) -> void:
	if not _valid_id(pid) or after < 0:
		_action_problem("chat_get", L10n.t("launcher.social_client.the_chat_request_is_invalid"))
		return
	_begin_action("chat_get", HTTPClient.METHOD_GET, "/v1/chat/%s/messages?after=%d&limit=50" % [pid, after], {}, {"pid": pid, "after": after})


func send_chat(pid: String, text: String) -> void:
	if not _valid_id(pid) or not _valid_text(text, 1000, 4000):
		_action_problem("chat_send", L10n.t("launcher.social_client.the_chat_message_is_invalid"))
		return
	var key := _chat_key(pid, text)
	var client_id: String = _chat_retries.get(key, "")
	if client_id.is_empty():
		var random := Crypto.new().generate_random_bytes(16)
		if random.size() != 16:
			_action_problem("chat_send", L10n.t("launcher.social_client.the_chat_message_could_not_be"))
			return
		client_id = random.hex_encode()
		_chat_retries[key] = client_id
		while _chat_retries.size() > 16:
			_chat_retries.erase(_chat_retries.keys()[0])
	_begin_action("chat_send", HTTPClient.METHOD_POST, "/v1/chat/%s/messages" % pid,
		{"v": 1, "client_id": client_id, "text": text},
		{"pid": pid, "chat_key": key, "client_id": client_id, "text": text})


func mark_chat_read(pid: String, through: int) -> void:
	if not _valid_id(pid) or through < 0:
		_action_problem("chat_read", L10n.t("launcher.social_client.the_chat_position_is_invalid"))
		return
	_begin_action("chat_read", HTTPClient.METHOD_POST, "/v1/chat/%s/read" % pid, {"v": 1, "through": through})


func _begin_action(kind: String, method: int, path: String, body: Dictionary, meta: Dictionary = {}) -> void:
	if not is_signed_in():
		_action_problem(kind, L10n.t("launcher.social_client.sign_in_before_using_social_features"))
		return
	if busy:
		_action_problem(kind, L10n.t("launcher.social_client.another_social_action_is_in_progress"))
		return
	if _rate_limit_remaining() > 0.0:
		_action_problem(kind, L10n.t("launcher.social_client.the_social_lobby_asked_you_to"))
		return
	_set_busy(true)
	if _send_request(kind, method, path, JSON.stringify(body) if not body.is_empty() else "", true, meta) < 0:
		action_done.emit(kind, false)


func _action_problem(kind: String, message: String) -> void:
	_problem(message)
	action_done.emit(kind, false)


func _send_request(kind: String, method: int, path: String, body: String, authenticated: bool, meta: Dictionary = {}) -> int:
	if not _configured:
		if kind != "poll":
			_set_busy(false)
		_problem(L10n.t("launcher.social_client.the_social_lobby_is_not_configured"))
		return -1
	var url := lobby_url + path
	var parsed := NetPolicy.parse_url(url)
	if not parsed.ok or parsed.origin != lobby_url or not str(parsed.path).begins_with("/v1/"):
		if kind != "poll":
			_set_busy(false)
		_problem(L10n.t("launcher.social_client.the_social_request_address_is_invalid"))
		return -1
	if body.to_utf8_buffer().size() > REQUEST_LIMIT:
		if kind != "poll":
			_set_busy(false)
		_problem(L10n.t("launcher.social_client.the_social_request_is_too_large"))
		return -1
	var request := HTTPRequest.new()
	request.timeout = HELD_POLL_TIMEOUT if kind == "poll" else REQUEST_TIMEOUT
	request.max_redirects = 0
	request.body_size_limit = BODY_LIMIT
	request.set_tls_options(TLSOptions.client())
	add_child(request)
	var request_id := _next_request
	_next_request += 1
	request.request_completed.connect(_on_response.bind(request_id))
	var headers := PackedStringArray(["accept: application/json"])
	if body != "":
		headers.append("content-type: application/json")
	if authenticated:
		headers.append("authorization: Bearer " + token)
	_requests[request_id] = {"node": request, "kind": kind, "epoch": _epoch, "meta": meta}
	var error := request.request(url, headers, method, body)
	if error != OK:
		_requests.erase(request_id)
		request.queue_free()
		if kind == "poll":
			_poll_id = 0
			_schedule_reconnect()
		else:
			_set_busy(false)
			_problem(L10n.t("launcher.social_client.the_lobby_could_not_be_reached"))
		return -1
	if kind == "poll":
		_poll_id = request_id
	return request_id


func _on_response(result: int, code: int, headers: PackedStringArray, body: PackedByteArray, request_id: int) -> void:
	var context: Dictionary = _requests.get(request_id, {})
	_requests.erase(request_id)
	if context.is_empty():
		return
	var request: HTTPRequest = context.node
	if request != null:
		request.queue_free()
	if int(context.epoch) != _epoch:
		return
	var kind: String = context.kind
	if kind == "poll":
		_poll_id = 0
	var success := _process_response(result, code, headers, body, context)
	if int(context.epoch) == _epoch and _is_action_kind(kind):
		action_done.emit(kind, success)


func _process_response(result: int, code: int, headers: PackedStringArray, body: PackedByteArray,
		context: Dictionary) -> bool:
	var kind: String = context.kind
	var header_map := _headers(headers)
	if kind == "features":
		var parsed := _parse_json(body) if result == HTTPRequest.RESULT_SUCCESS and code == 200 else {}
		var features: Variant = parsed.get("data", {}).get("features", []) if parsed.get("ok", false) else []
		if result != HTTPRequest.RESULT_SUCCESS:
			_drop_session()
			_problem(L10n.t("launcher.social_client.the_lobby_could_not_be_reached"))
			return false
		if not features is Array or not "social" in features:
			_drop_session()
			unsupported = true
			_problem(UNSUPPORTED_TEXT)
			return false
		unsupported = false
		var signin := _signin_body
		_signin_body = ""
		if _send_request("auth", HTTPClient.METHOD_POST, "/v1/auth/guest", signin, false) < 0:
			_drop_session()
		return true
	if result != HTTPRequest.RESULT_SUCCESS:
		if kind == "poll":
			_problem(L10n.t("launcher.social_client.the_inbox_connection_failed"))
			_schedule_reconnect()
		elif kind == "friends_refresh":
			_set_busy(false)
			_problem(L10n.t("launcher.social_client.the_friends_list_could_not_be"))
			_schedule_reconnect()
		elif _is_signin_kind(kind):
			_drop_session()
			_problem(L10n.t("launcher.social_client.the_lobby_could_not_be_reached"))
		else:
			_set_busy(false)
			_problem(L10n.t("launcher.social_client.the_lobby_could_not_be_reached"))
		return false
	if code >= 300 and code < 400:
		if kind == "poll":
			_problem(L10n.t("launcher.social_client.the_inbox_redirect_was_not_followed"))
			_schedule_reconnect()
		elif kind == "friends_refresh":
			_set_busy(false)
			_problem(L10n.t("launcher.social_client.the_lobby_returned_a_redirect_that"))
			_schedule_reconnect()
		elif _is_signin_kind(kind):
			_drop_session()
			_problem(L10n.t("launcher.social_client.the_lobby_returned_a_redirect_that"))
		else:
			_set_busy(false)
			_problem(L10n.t("launcher.social_client.the_lobby_returned_a_redirect_that"))
		return false
	if body.size() > BODY_LIMIT:
		if kind == "poll":
			_problem(L10n.t("launcher.social_client.the_inbox_response_was_too_large"))
			_schedule_reconnect()
		elif kind == "friends_refresh":
			_set_busy(false)
			_problem(L10n.t("launcher.social_client.the_friends_response_was_too_large"))
			_schedule_reconnect()
		elif _is_signin_kind(kind):
			_drop_session()
			_problem(L10n.t("launcher.social_client.the_lobby_response_was_too_large"))
		else:
			_set_busy(false)
			_problem(L10n.t("launcher.social_client.the_lobby_response_was_too_large"))
		return false
	var list_reply := code == 200 and kind in ["friends", "friends_refresh", "poll", "chat_get"]
	var parsed := _parse_json(body, list_reply)
	if code != 200:
		var error_code := str(parsed.get("error", "")) if parsed.ok else ""
		var retry := _retry_after(header_map, parsed) if code == 429 else 0.0
		if code == 429:
			_extend_rate_limit(retry)
		var fatal := _is_fatal_session_error(code, error_code)
		if fatal:
			_drop_session()
		if kind == "poll":
			_problem(_safe_error(code, error_code))
			if code == 429:
				_extend_poll_not_before(retry)
				_schedule_poll(retry)
			elif not fatal:
				_schedule_reconnect()
		elif _is_signin_kind(kind):
			if not fatal:
				_drop_session()
			_problem(_safe_error(code, error_code))
		elif kind == "friends_refresh":
			_set_busy(false)
			_problem(_safe_error(code, error_code))
			if code == 429:
				_schedule_poll(retry)
			elif not fatal:
				_schedule_reconnect()
		else:
			if not fatal:
				_set_busy(false)
			_problem(_safe_error(code, error_code))
		return false
	if not parsed.ok or not _valid_int(parsed.data.get("v"), 1, 1):
		if kind == "poll":
			_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_social"))
			_schedule_reconnect()
		elif kind == "friends_refresh":
			_set_busy(false)
			_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_friends"))
			_schedule_reconnect()
		elif _is_signin_kind(kind):
			_drop_session()
			_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_social"))
		else:
			_set_busy(false)
			_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_social"))
		return false
	last_error = ""
	if kind == "poll":
		_handle_poll(parsed.data)
		return false
	return _handle_action(kind, parsed.data, context.meta)


func _handle_action(kind: String, data: Dictionary, meta: Dictionary) -> bool:
	if _contains_private_identity(data):
		if _is_signin_kind(kind):
			_drop_session()
		else:
			_set_busy(false)
			if kind == "friends_refresh":
				_schedule_reconnect()
		_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_social"))
		return false
	match kind:
		"auth":
			if not _valid_auth(data):
				_drop_session()
				_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_sign"))
				return false
			token = data.token
			_expected_pid = data.account.pid
			_roster_id = str(data.account.get("id", ""))
			var said: Variant = data.get("notice", null)
			if said is Dictionary and said.get("text") is String and said.get("id") is String and not str(said.text).is_empty() \
					and str(said.text).length() <= 2000 and said.id != _last_notice:
				_last_notice = said.id
				notice.emit(said.text)
			me = {}
			_request_next("me", HTTPClient.METHOD_GET, "/v1/me")
			return true
		"me":
			if not _valid_me(data) or data.get("pid", "") != _expected_pid:
				_drop_session()
				_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_profile"))
				return false
			me = data.duplicate(true)
			_request_next("friends", HTTPClient.METHOD_GET, "/v1/friends")
			return true
		"friends", "friends_refresh":
			var parsed_friends := _validate_friends(data)
			if not parsed_friends.ok:
				_set_busy(false)
				if kind == "friends":
					_drop_session()
				else:
					_schedule_reconnect()
				_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_friends"))
				return false
			friends = parsed_friends.friends
			if kind == "friends":
				_set_busy(false)
				_reconnect_attempt = 0
				_set_connected(true)
				changed.emit()
				_schedule_poll(0.0)
			else:
				_refresh_friends = false
				_set_busy(false)
				changed.emit()
				_schedule_poll(0.0)
			return true
		"friend_request":
			if not _validate_request(data.get("request", {})):
				_set_busy(false)
				_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_action"))
				return false
			_set_busy(false)
			changed.emit()
			return true
		"request_accept", "request_decline", "remove_friend", "invite_decline", "status":
			if not _valid_ok(data):
				_set_busy(false)
				_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_action"))
				return false
			if kind == "status":
				me["hidden"] = meta.get("hidden", me.get("hidden", false))
			_set_busy(false)
			changed.emit()
			return true
		"chat_read":
			if not _valid_int(data.get("read_id", -1), 0):
				_set_busy(false)
				_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_chat"))
				return false
			_set_busy(false)
			changed.emit()
			return true
		"recovery":
			var code: Variant = data.get("code", "")
			if not _valid_recovery(code):
				_set_busy(false)
				_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_recovery"))
				return false
			_set_busy(false)
			recovery_ready.emit(code)
			changed.emit()
			return true
		"chat_get":
			var chat := _validate_chat(data, int(meta.get("after", 0)), str(meta.get("pid", "")))
			if not chat.ok:
				_set_busy(false)
				_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_chat"))
				return false
			_set_busy(false)
			chat_ready.emit(str(meta.pid), chat.result)
			changed.emit()
			return true
		"chat_send":
			var message := _validate_message(data.get("message", {}))
			if not message.ok or message.value.client_id != meta.get("client_id", "") \
					or message.value.text != meta.get("text", "") or message.value.from != me.get("pid", "") \
					or message.value.to != meta.get("pid", ""):
				_set_busy(false)
				_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_chat"))
				return false
			_chat_retries.erase(str(meta.chat_key))
			_set_busy(false)
			changed.emit()
			return true
	_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_social"))
	_set_busy(false)
	return false


func _request_next(kind: String, method: int, path: String) -> void:
	if _send_request(kind, method, path, "", true) < 0:
		_set_busy(false)
		_drop_session()


func _handle_poll(data: Dictionary) -> void:
	if _contains_private_identity(data):
		_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_inbox"))
		_schedule_reconnect()
		return
	var validated := _validate_snapshot(data)
	if not validated.ok:
		_problem(L10n.t("launcher.social_client.the_lobby_returned_an_invalid_inbox"))
		_schedule_reconnect()
		return
	var old_pids := {}
	for friend in friends:
		old_pids[friend.pid] = true
	var new_pids := {}
	for presence in validated.snapshot.presence:
		new_pids[presence.pid] = true
	_seq = validated.snapshot.seq
	snapshot = validated.snapshot
	_extend_poll_not_before(float(validated.delay))
	_reconnect_attempt = 0
	changed.emit()
	if old_pids != new_pids:
		_refresh_friends = true
	if _refresh_friends and not busy:
		_set_busy(true)
		if _send_request("friends_refresh", HTTPClient.METHOD_GET, "/v1/friends", "", true) < 0:
			_schedule_reconnect()
		return
	_schedule_poll(float(validated.delay))


func _poll_tick() -> void:
	if not is_signed_in() or _poll_id != 0:
		return
	var path := "/v1/inbox?wait=25"
	if _seq != "":
		path = "/v1/inbox?since=%s&wait=25" % _seq
	_send_request("poll", HTTPClient.METHOD_GET, path, "", true)


func _schedule_poll(seconds: float) -> void:
	if not is_signed_in() or _poll_timer == null:
		return
	_poll_timer.stop()
	var delay := maxf(seconds, _rate_limit_remaining())
	delay = maxf(delay, _poll_backoff_remaining())
	_poll_timer.start(maxf(delay, 0.01))


func _rate_limit_remaining() -> float:
	return maxf(0.0, float(_rate_limit_until_msec - Time.get_ticks_msec()) / 1000.0)


func _poll_backoff_remaining() -> float:
	return maxf(0.0, float(_poll_not_before_msec - Time.get_ticks_msec()) / 1000.0)


func _extend_rate_limit(seconds: float) -> void:
	_rate_limit_until_msec = maxi(_rate_limit_until_msec,
		Time.get_ticks_msec() + int(seconds * 1000.0))


func _extend_poll_not_before(seconds: float) -> void:
	_poll_not_before_msec = maxi(_poll_not_before_msec,
		Time.get_ticks_msec() + int(seconds * 1000.0))


func _schedule_reconnect() -> void:
	_reconnect_attempt = mini(_reconnect_attempt + 1, 6)
	_schedule_poll(minf(pow(2.0, _reconnect_attempt - 1), MAX_RECONNECT))


func _drop_session() -> void:
	_signin_body = ""
	var was_connected := is_signed_in() or _connected
	_epoch += 1
	_cancel_requests()
	_poll_id = 0
	if _poll_timer != null:
		_poll_timer.stop()
	token = ""
	_identity.clear()
	me = {}
	friends = []
	snapshot = _empty_snapshot()
	_seq = ""
	_refresh_friends = false
	_chat_retries.clear()
	_expected_pid = ""
	_roster_id = ""
	_set_busy(false)
	_connected = false
	if was_connected:
		connection_changed.emit(false)
	changed.emit()


func _cancel_requests() -> void:
	for request_id in _requests.keys():
		var context: Dictionary = _requests[request_id]
		var request: HTTPRequest = context.get("node")
		if request != null:
			request.cancel_request()
			request.queue_free()
	_requests.clear()


func _set_busy(value: bool) -> void:
	if busy == value:
		return
	busy = value
	changed.emit()


func _set_connected(value: bool) -> void:
	if _connected == value:
		return
	_connected = value
	connection_changed.emit(value)


func _problem(message: String) -> void:
	last_error = message
	problem.emit(message)


func _parse_json(body: PackedByteArray, tolerant := false) -> Dictionary:
	if tolerant and body.has(0):
		return {"ok": false}
	var parser := JSON.new()
	if parser.parse(LobbyJSON.decode(body) if tolerant else body.get_string_from_utf8()) != OK or not parser.data is Dictionary:
		return {"ok": false}
	var data: Dictionary = parser.data
	return {"ok": true, "data": data, "error": str(data.get("error", "")), "retry_after": data.get("retry_after", 0)}


func _headers(headers: PackedStringArray) -> Dictionary:
	var result := {}
	for line in headers:
		var colon := line.find(":")
		if colon > 0:
			result[line.substr(0, colon).strip_edges().to_lower()] = line.substr(colon + 1).strip_edges()
	return result


func _retry_after(headers: Dictionary, data: Dictionary) -> float:
	var value := 0
	var body_value: Variant = data.get("retry_after", 0)
	if _valid_int(body_value, 1, MAX_RETRY_AFTER_SECONDS):
		value = int(body_value)
	var header_value: Variant = headers.get("retry-after", "")
	if header_value is String and RegEx.create_from_string("^[0-9]{1,10}\\z").search(header_value) != null:
		var header_seconds := int(header_value)
		if header_seconds > 0 and header_seconds <= MAX_RETRY_AFTER_SECONDS:
			value = maxi(value, header_seconds)
	return float(value if value > 0 else 10)


static func _translated(ids: Dictionary) -> Dictionary:
	var texts := {}
	for key in ids:
		texts[key] = L10n.t(ids[key])
	return texts


func _safe_error(code: int, error_code: String) -> String:
	if ERROR_MESSAGE_IDS.has(error_code):
		return L10n.t(ERROR_MESSAGE_IDS[error_code])
	if HTTP_ERROR_MESSAGE_IDS.has(code):
		return L10n.t(HTTP_ERROR_MESSAGE_IDS[code])
	return L10n.t("launcher.social_client.the_social_lobby_did_not_accept")


func _is_signin_kind(kind: String) -> bool:
	return kind in ["features", "auth", "me", "friends"]


func _is_action_kind(kind: String) -> bool:
	return kind not in ["poll", "features", "auth", "me", "friends", "friends_refresh"]


func _is_fatal_session_error(code: int, error_code: String) -> bool:
	return code == 401 or error_code in ["signed_out", "session_revoked", "banned", "suspended"]


func _valid_auth(data: Dictionary) -> bool:
	var account: Variant = data.get("account", {})
	return data.get("token") is String and RegEx.create_from_string(TOKEN_PATTERN).search(data.token) != null \
		and account is Dictionary and _valid_id(account.get("pid", null)) \
		and _valid_text(account.get("name", null), 24, 128) and account.get("kind") is String \
		and account.kind == "guest"


func _valid_me(data: Dictionary) -> bool:
	if not _valid_id(data.get("pid", null)) or not _valid_friend_code(data.get("code", null)):
		return false
	if not _valid_text(data.get("nick", null), 24, 128):
		return false
	if data.get("hidden") is not bool or not data.get("privacy") is Dictionary:
		return false
	var privacy: Dictionary = data.privacy
	return privacy.get("invites") in ["friends_or_room", "friends"] \
		and privacy.get("challenges") in ["friends_or_room", "friends"] \
		and privacy.get("requests") in ["anyone", "nobody"] \
		and privacy.get("presence") in ["friends", "nobody"]


func _filter_rows(rows: Array, valid: Callable, collection: String, unique := "") -> Array:
	var result: Array = []
	var seen := {}
	for index in rows.size():
		var row: Variant = rows[index]
		if valid.call(row) and (unique.is_empty() or not seen.has(row[unique])):
			result.append(row.duplicate(true))
			if not unique.is_empty(): seen[row[unique]] = true
		else:
			print("LAUNCHER ONLINE skipped malformed %s list entry at index %d" % [collection, index])
	return result


func _valid_friend(row: Variant) -> bool:
	if not _valid_player(row) or not _valid_int(row.get("since"), 0):
		return false
	var presence := _validate_presence(row.get("presence", {}))
	return presence.ok and presence.value.pid == row.pid


func _validate_friends(data: Dictionary) -> Dictionary:
	var rows: Variant = data.get("friends", null)
	if not rows is Array or rows.size() > MAX_FRIENDS:
		return {"ok": false}
	return {"ok": true, "friends": _filter_rows(rows, _valid_friend, "friends", "pid")}


func _validate_snapshot(data: Dictionary) -> Dictionary:
	var seq: Variant = data.get("seq", "")
	if not seq is String or seq.length() > 80 or RegEx.create_from_string("^[0-9a-f]{16}:[0-9]+\\z").search(seq) == null:
		return {"ok": false}
	if data.get("reset") is not bool:
		return {"ok": false}
	var presence: Variant = data.get("presence", null)
	var requests: Variant = data.get("requests", null)
	var invites: Variant = data.get("invites", null)
	var notices: Variant = data.get("notices", null)
	var chat: Variant = data.get("chat", null)
	if not presence is Array or not requests is Array or not invites is Array or not notices is Array or not chat is Array:
		return {"ok": false}
	if presence.size() > MAX_FRIENDS or requests.size() > MAX_REQUESTS or invites.size() > MAX_COLLECTION \
			or notices.size() > MAX_NOTICES or chat.size() > MAX_CHAT_HEADS:
		return {"ok": false}
	var stats: Variant = data.get("stats", {})
	if not _valid_stats(stats):
		return {"ok": false}
	var wait: Variant = data.get("wait", 25)
	if not _valid_int(wait, 0) or int(wait) > 25:
		return {"ok": false}
	var delay := 0.0
	if data.has("backoff"):
		if not _valid_int(data.backoff, 1, int(MAX_BACKOFF)):
			return {"ok": false}
		delay = float(data.backoff)
	var valid_presence := _filter_rows(presence, func(row): return _validate_presence(row).ok, "presence", "pid")
	var valid_requests := _filter_rows(requests, _validate_request, "requests", "id")
	var valid_invites := _filter_rows(invites, _validate_invite, "invites", "id")
	var valid_notices := _filter_rows(notices, func(row): return row is Dictionary and _valid_notice(row), "notices", "id")
	var valid_chat := _filter_rows(chat, _valid_chat_head, "chat", "pid")
	return {"ok": true, "delay": delay, "snapshot": {
		"seq": seq, "reset": data.reset, "presence": valid_presence,
		"requests": valid_requests, "invites": valid_invites,
		"notices": valid_notices, "chat": valid_chat, "stats": stats.duplicate(true),
	}}


func _valid_chat_head(row: Variant) -> bool:
	return row is Dictionary and _valid_id(row.get("pid")) and _valid_int(row.get("last_id"), 0) \
		and _valid_int(row.get("read_id"), 0) and row.read_id <= row.last_id


func _validate_presence(value: Variant) -> Dictionary:
	if not value is Dictionary or not value.has_all(["pid", "state", "game", "joinable", "since", "map", "mode"]) \
			or not _valid_id(value.get("pid", null)):
		return {"ok": false}
	if not ["offline", "online", "in_room", "matching", "loading", "playing", "away"].has(value.get("state")):
		return {"ok": false}
	if value.get("joinable") is not bool or not _valid_int(value.get("since", -1), 0):
		return {"ok": false}
	if value.get("game") != null and not _valid_id(value.game):
		return {"ok": false}
	if value.get("map") != null and not _valid_text(value.map, 64, 256, true):
		return {"ok": false}
	if value.get("mode") != null and (not value.mode is String or not ["normal", "uber"].has(value.mode)):
		return {"ok": false}
	return {"ok": true, "value": value.duplicate(true)}


func _validate_request(value: Variant) -> bool:
	return value is Dictionary and _valid_id(value.get("id", null)) \
		and _valid_player(value.get("from", {})) and _valid_player(value.get("to", {})) \
		and _valid_int(value.get("at", -1), 0)


func _validate_invite(value: Variant) -> bool:
	if not value is Dictionary or not value.has_all(["id", "from", "to", "game", "kind", "mode", "note", "expires"]) \
			or not _valid_id(value.get("id", null)) \
			or not _valid_player(value.get("from", {})) or not _valid_player(value.get("to", {})) \
			or not _valid_id(value.get("game", null)) or not ["invite", "challenge"].has(value.get("kind")) \
			or not _valid_int(value.get("expires", -1), 0):
		return false
	if value.get("mode") != null and not ["normal", "uber"].has(value.mode):
		return false
	return _valid_text(value.get("note", null), 200, 800, true)


func _valid_notice(value: Dictionary) -> bool:
	return value.has_all(["id", "kind", "invite", "at"]) and value.get("id") is String and str(value.id).length() <= 64 \
		and RegEx.create_from_string("^[A-Za-z0-9_-]+\\z").search(value.id) != null \
		and ["declined", "busy", "expired", "recovered"].has(value.get("kind")) \
		and (value.get("invite") == null or _valid_id(value.invite)) \
		and _valid_int(value.get("at", -1), 0)


func _valid_stats(value: Variant) -> bool:
	return value is Dictionary and _valid_int(value.get("v"), 1, 1) \
		and _valid_int(value.get("players", -1), 0, 10000000) \
		and _valid_int(value.get("games_open", -1), 0, 10000000) and _valid_int(value.get("games_running", -1), 0, 10000000) \
		and _valid_int(value.get("as_of", -1), 0)


func _valid_player(value: Variant) -> bool:
	return value is Dictionary and _valid_id(value.get("pid", null)) \
		and _valid_text(value.get("nick", null), 24, 128) and _valid_friend_code(value.get("code", null))


func _validate_chat(data: Dictionary, after: int, peer_pid: String) -> Dictionary:
	var rows: Variant = data.get("messages", null)
	if not rows is Array or rows.size() > 100 or not _valid_id(peer_pid) \
			or peer_pid == str(me.get("pid", "")):
		return {"ok": false}
	var next: Variant = data.get("next", null)
	var last_id: Variant = data.get("last_id", null)
	var read_id: Variant = data.get("read_id", null)
	if not data.has("next") or (next != null and not _valid_int(next, after + 1)) or not _valid_int(last_id, 0) \
			or not _valid_int(read_id, 0) or int(read_id) > int(last_id):
		return {"ok": false}
	if rows.is_empty() and after < int(last_id):
		return {"ok": false}
	if next != null:
		if int(next) >= int(last_id) or rows.is_empty():
			return {"ok": false}
		var tail: Variant = rows.back()
		if tail is Dictionary and _valid_int(tail.get("id"), 1) and int(next) != int(tail.id):
			return {"ok": false}
	var messages: Array = []
	var previous_id := after
	var self_pid := str(me.get("pid", ""))
	for index in rows.size():
		var result := _validate_message(rows[index])
		var valid: bool = result.ok and int(result.value.id) > previous_id and int(result.value.id) <= int(last_id)
		if valid:
			var from_pid: String = result.value.from
			var to_pid: String = result.value.to
			valid = (from_pid == self_pid and to_pid == peer_pid) or (from_pid == peer_pid and to_pid == self_pid)
		if not valid:
			print("LAUNCHER ONLINE skipped malformed messages list entry at index %d" % index)
			continue
		previous_id = int(result.value.id)
		messages.append(result.value)
	if messages.size() == rows.size() and next == null and not messages.is_empty() and previous_id != int(last_id):
		return {"ok": false}
	# A discarded tail is not a visible message or a usable page cursor.
	if next != null: next = null if messages.is_empty() else previous_id
	return {"ok": true, "result": {"v": 1, "messages": messages, "next": next,
		"last_id": int(last_id), "read_id": int(read_id), "after": after}}


func _validate_message(value: Variant) -> Dictionary:
	if not value is Dictionary or not _valid_int(value.get("id", -1), 1) \
			or not _valid_id(value.get("from", null)) or not _valid_id(value.get("to", null)) \
			or not _valid_text(value.get("text", null), 1000, 4000) \
			or not _valid_int(value.get("at", -1), 0) \
			or not value.get("client_id") is String or RegEx.create_from_string("^[0-9a-f]{32}\\z").search(value.client_id) == null:
		return {"ok": false}
	return {"ok": true, "value": value.duplicate(true)}


func _valid_ok(data: Dictionary) -> bool:
	return data.get("ok") is bool and data.ok


func _contains_private_identity(value: Variant, depth := 0) -> bool:
	if depth > 8:
		return true
	if value is Dictionary:
		for key in value.keys():
			if str(key) in ["install", "device", "install_id", "device_hash"]:
				return true
			if _contains_private_identity(value[key], depth + 1):
				return true
	elif value is Array:
		for item in value:
			if _contains_private_identity(item, depth + 1):
				return true
	return false


func _valid_id(value: Variant) -> bool:
	return value is String and RegEx.create_from_string(PID_PATTERN).search(value) != null


func _valid_install(value: String) -> bool:
	return RegEx.create_from_string(INSTALL_PATTERN).search(value) != null


func _valid_device(value: String) -> bool:
	return RegEx.create_from_string(DEVICE_PATTERN).search(value) != null


func _valid_friend_code(value: Variant) -> bool:
	if not value is String:
		return false
	return RegEx.create_from_string(CODE_PATTERN).search(value) != null \
		or RegEx.create_from_string(CODE_DISPLAY_PATTERN).search(value) != null


func _valid_recovery(value: Variant) -> bool:
	if not value is String:
		return false
	return RegEx.create_from_string(RECOVERY_PATTERN).search(value) != null


func _valid_text(value: Variant, max_chars: int, max_bytes: int, allow_empty := false) -> bool:
	if not value is String:
		return false
	if not allow_empty and value.is_empty():
		return false
	if value.length() > max_chars or value.to_utf8_buffer().size() > max_bytes:
		return false
	if value.strip_edges() != value or (value.strip_edges().is_empty() and (not allow_empty or not value.is_empty())):
		return false
	for c in value:
		var code: int = c.unicode_at(0)
		if code < 0x20 or code == 0x7f:
			return false
	return true


func _valid_int(value: Variant, minimum: int, maximum: int = 9007199254740991) -> bool:
	if typeof(value) == TYPE_INT:
		return value >= minimum and value <= maximum
	if typeof(value) != TYPE_FLOAT or not is_finite(value):
		return false
	return floor(value) == value and value >= minimum and value <= maximum


func _chat_key(pid: String, text: String) -> String:
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	var bytes := pid.to_utf8_buffer()
	bytes.append(0)
	bytes.append_array(text.to_utf8_buffer())
	hash.update(bytes)
	return hash.finish().hex_encode()


func _empty_snapshot() -> Dictionary:
	return {"presence": [], "requests": [], "invites": [], "notices": [], "chat": [],
		"stats": {"v": 1, "players": 0, "games_open": 0, "games_running": 0, "as_of": 0}}
