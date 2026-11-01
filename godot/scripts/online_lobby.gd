## OpenBFME. GPL-3.0.
## Lane ONLINE-2 (docs/ONLINE.md phase O1): the game's client of the online lobby service (services/online/lobby: JSON over HTTP(S), "v": 1). It signs in
## (a guest identity, or a lobby session token given in OPENBFME_LOBBY_TOKEN), joins a game by its id (the debug "join by id" path and the launcher's
## openbfme://join/<id> link) or hosts one, says ready and waits for the START the service builds; game.gd hands the START to GameWorld.net_online and the
## game then loads and plays like a LAN game with every peer behind the relay. Online play is a test feature.
## Every step prints a "GAME ONLINE" line; a refusal (another profile, a full or started game, the lobby unreachable) is an error, never a retry loop.
## ONLINE-2 r2 (Sol r1: Godot follows a redirect to another origin with the request's headers, the bearer token included): the client follows no
## redirect (max_redirects 0: a 3xx is an error), and the lobby URL must be a plain http(s) origin (no user, query or fragment), so every request
## and its token go to the lobby the player named and nowhere else.
## Lane ONLINE-3 (docs/ONLINE.md "Phase O3"): rejoin. A guest session is kept in the user data (user://online_session.txt) so a game that restarted (a
## crash, a lost connection) signs in as the same player; `rejoin` lists the player's running games (GET /v1/mine) and asks for a rejoin START
## (POST /v1/games/<id>/rejoin: a fresh ticket of the next generation) for `join_id`, or for the only running game when it is empty.
## Lane ONLINE-3 (spectators): `spectate` asks for a spectator START for `join_id` (POST /v1/games/<id>/spectate: a version 3 ticket, a fresh index
## each time), once a second while the game has not started; the session is not kept (a spectator has no slot to rejoin: it asks again).
extends Node

const OnlineList := preload("res://scripts/online_list.gd")
const HttpTimed := preload("res://scripts/http_timed.gd") ## a time limit in running frames (a blocking load does not use it up)

signal started(start_json: String)
signal failed(message: String)
## lane JOINVER-1: a join refused for a different build: the plain words and the DETAILS text (both builds' tags and profile ids)
signal build_refused(message: String, details: String)
signal notice(message: String)
signal report_context(context: Dictionary)
signal oneshot_done(result: int, code: int, body: PackedByteArray) ## NETGUARD-1: the answer of a post_oneshot request

const BODY_LIMIT := 262144 ## Sol r1 (ONLINE-4): the largest lobby answer read (OnlineLobbyClient::kMaxBodyBytes)

var lobby_url := ""
var player_name := "Player"
var profile := ""
var mode := "normal"        ## lane UBERJOIN-1: the performance mode of this player's games ("normal" or "uber": the lobby refuses a join in the other)
var platform := OS.get_name().to_lower() ## SOCIAL-1: told with a join (lobby.mjs checkPlatform)
var join_id := ""          ## join this game; empty: host one with host_setup
var invite_id := ""        ## accept with this game's locally computed profile and inherited lobby token
var host_setup := {}        ## {name, setup: {map, startingCash, slots: [...]}, players: N}
var token := ""
var rejoin := false          ## lane ONLINE-3: rejoin a running game instead of joining / hosting
var spectate := false        ## lane ONLINE-3: watch the game `join_id` as a spectator
const SESSION_FILE := "user://online_session.txt"

var _http: HTTPRequest
var _step := ""
var _game_id := ""
var _since := 0
var _poll_at := 0
var _ready_sent := false
var _busy := false


## "" when `url` is a plain http(s) origin with an optional path, else why not
static func lobby_url_problem(url: String) -> String:
	var re := RegEx.create_from_string("^https?://[A-Za-z0-9.-]+(:[0-9]{1,5})?(/[A-Za-z0-9._~/-]*)?$")
	var m := re.search(url)
	return "" if m != null and m.get_string() == url else "the lobby URL must be http(s)://host[:port][/path], got %s" % url


## NETGUARD-1: one authenticated POST of an already serialized body to the lobby (the player reports of online_reports.gd), through this
## client's one HTTP path: same lobby_url check, no redirect, same body bound, the session `token` as the bearer. The answer arrives as
## oneshot_done (a refused URL or a failed start arrives the same way, result -1 / code 0). No sign-in, no state machine.
func post_oneshot(path: String, body_text: String, extra_headers: PackedStringArray) -> void:
	_step = "oneshot"
	var problem := lobby_url_problem(lobby_url)
	if not problem.is_empty() or token.is_empty():
		oneshot_done.emit(-1, 0, PackedByteArray())
		return
	_make_http()
	_busy = true
	if not _send(HTTPClient.METHOD_POST, path, null, extra_headers, body_text):
		_busy = false
		oneshot_done.emit(-1, 0, PackedByteArray())


func _make_http() -> void:
	if _http != null:
		return
	_http = HTTPRequest.new()
	HttpTimed.set_budget(_http, 20.0)
	_http.max_redirects = 0 # ONLINE-2 r2: never follow a redirect (Godot would send the bearer token to the new origin)
	_http.body_size_limit = BODY_LIMIT # Sol r1 (ONLINE-4): a hostile lobby's answer is not read beyond the bound
	add_child(_http)
	_http.request_completed.connect(_on_response)


func begin() -> void:
	_make_http()
	if lobby_url.is_empty():
		_fail("no lobby: --online-lobby=URL (or OPENBFME_LOBBY)")
		return
	var problem := lobby_url_problem(lobby_url)
	if not problem.is_empty():
		_fail(problem)
		return
	if mode not in ["normal", "uber"]:
		_fail("the local performance mode is invalid")
		return
	if not invite_id.is_empty():
		if RegEx.create_from_string("^[0-9a-f]{16}$").search(invite_id) == null:
			_fail("the invitation id is invalid")
			return
		if token.is_empty():
			_fail("accepting an invitation requires the original lobby session")
			return
		if not join_id.is_empty() or not host_setup.is_empty() or rejoin or spectate or RegEx.create_from_string("^[0-9a-f]{32}$").search(profile) == null:
			_fail("accepting an invitation requires a local profile and no other join mode")
			return
	if token.is_empty() and rejoin:
		# lane ONLINE-3: the session of the game that restarted (the slot belongs to that guest)
		var f := FileAccess.open(SESSION_FILE, FileAccess.READ)
		if f != null:
			token = f.get_as_text().strip_edges()
		if token.is_empty():
			_fail("no saved lobby session to rejoin with (%s): the game that restarted never signed in on this computer" % SESSION_FILE)
			return
	if token.is_empty():
		var identity: Dictionary = load("res://scripts/online_identity.gd").identity()
		if identity.has("error"):
			_fail(str(identity.error))
			return
		identity["name"] = player_name
		_step = "signin"
		_send(HTTPClient.METHOD_POST, "/v1/auth/guest", identity)
	else:
		_after_signin()


func _process(_delta: float) -> void:
	if _step == "wait" and not _busy and Time.get_ticks_msec() >= _poll_at:
		_send(HTTPClient.METHOD_GET, "/v1/games/%s?since=%d" % [_game_id, _since])
	elif _step == "spectate_wait" and not _busy and Time.get_ticks_msec() >= _poll_at:
		_step = "spectate"
		_send(HTTPClient.METHOD_POST, "/v1/games/%s/spectate" % join_id)


func _send(method: int, path: String, body: Variant = null, extra_headers := PackedStringArray(), body_text := "") -> bool:
	var headers := PackedStringArray(["content-type: application/json", "accept: application/json"])
	if not token.is_empty():
		headers.append("authorization: Bearer " + token)
	headers.append_array(extra_headers)
	_busy = true
	var text := body_text if body == null else JSON.stringify(body)
	var err := _http.request(lobby_url.trim_suffix("/") + path, headers, method, text)
	if err != OK and _step != "oneshot":
		_fail("cannot reach the lobby %s: %s" % [lobby_url, error_string(err)])
	return err == OK


func _fail(message: String) -> void:
	_step = "failed"
	message = AptMenuPlayer.online_redact(message) # Sol r1 (ONLINE-4): a ticket or token the lobby echoed is never printed or shown
	printerr("GAME ONLINE error: ", message)
	failed.emit(message)


func _join_identity() -> Dictionary:
	var identity := {"profile": profile, "mode": mode}
	if platform in ["windows", "linux", "macos"]:
		identity["platform"] = platform
	var build := own_build()
	if RegEx.create_from_string("^[0-9A-Za-z._+-]{1,64}$").search(str(build.get("version", ""))) != null:
		identity["build"] = build # lane JOINVER-1: lobby.mjs checkBuild (an odd version is not told: the join never fails on it)
	return identity


## lane JOINVER-1: this build as the lobby is told it (online_identity.gd build_info: the build day, an official release or not)
func own_build() -> Dictionary:
	if not ClassDB.class_exists("ReleaseInfo"):
		return {}
	var build: Dictionary = {"version": str(ClassDB.instantiate("ReleaseInfo").version())}
	build.merge(load("res://scripts/online_identity.gd").build_info())
	if platform in ["windows", "linux", "macos"]:
		build["platform"] = platform
	return build


func _after_signin() -> void:
	if not invite_id.is_empty():
		_step = "invite"
		var identity := _join_identity()
		identity.erase("build") # lane JOINVER-1 (Sol): an invite's accept is checked field by field (social.mjs exact): an older lobby refuses it
		identity["v"] = 1
		_send(HTTPClient.METHOD_POST, "/v1/invites/%s/accept" % invite_id, identity)
		return
	if spectate:
		if join_id.is_empty():
			_fail("spectating needs the game's id")
			return
		_step = "spectate"
		_send(HTTPClient.METHOD_POST, "/v1/games/%s/spectate" % join_id)
		return
	if rejoin:
		_step = "mine"
		_send(HTTPClient.METHOD_GET, "/v1/mine")
		return
	if join_id.is_empty():
		_step = "create"
		var body: Dictionary = host_setup.duplicate(true)
		body.merge(_join_identity(), true)
		body.erase("players")
		_send(HTTPClient.METHOD_POST, "/v1/games", body)
	else:
		_step = "join"
		_send(HTTPClient.METHOD_POST, "/v1/games/%s/join" % join_id, _join_identity())


func _on_response(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	_busy = false
	if _step == "oneshot":
		oneshot_done.emit(result, code, body)
		return
	if result == HTTPRequest.RESULT_REDIRECT_LIMIT_REACHED or (code >= 300 and code < 400):
		_fail("the lobby %s answered with a redirect: not followed (the session stays with the lobby you named)" % lobby_url)
		return
	if result == HTTPRequest.RESULT_BODY_SIZE_LIMIT_EXCEEDED:
		_fail("the lobby %s answered with more than %d bytes: not read" % [lobby_url, BODY_LIMIT])
		return
	if result != HTTPRequest.RESULT_SUCCESS:
		_fail("the lobby %s did not answer (%d)" % [lobby_url, result])
		return
	var data: Variant
	if _step == "mine" and code == 200:
		var parsed := OnlineList.parse(body, true)
		if not parsed.ok:
			_fail(parsed.error)
			return
		data = parsed.data
	else:
		data = JSON.parse_string(body.get_string_from_utf8())
	if typeof(data) != TYPE_DICTIONARY or int(data.get("v", 0)) != 1:
		_fail("the lobby answered HTTP %d with something that is not lobby version 1" % code)
		return
	if code == 409 and _step == "spectate" and data.get("error", "") == "not_started":
		if _poll_at == 0:
			print("GAME ONLINE game %s has not started: waiting to spectate" % join_id)
		_step = "spectate_wait" # lane ONLINE-3: asked again once a second until the host starts it
		_poll_at = Time.get_ticks_msec() + 1000
		return
	if code == 409 and data.get("error", "") == "profile" and ClassDB.class_exists("ReleaseInfo"):
		# lane JOINVER-1 (the owner, 2026-10-23: "it says the build is different and gives me a hash"): both builds in plain words; the profile ids
		# only in the DETAILS (an older lobby names no host build: its side is unknown)
		var info: RefCounted = ClassDB.instantiate("ReleaseInfo")
		var host: Variant = data.get("host_build")
		var host_build: Dictionary = host if host is Dictionary else {}
		var words: String = info.join_refusal_text(own_build(), host_build)
		var details: String = info.join_details_text(own_build(), profile, host_build, str(data.get("host_profile", "")) if data.get("host_profile") is String else "")
		_step = "failed"
		printerr("GAME ONLINE join refused for a different build: ", details.replace("\n", "; "))
		build_refused.emit(words, details)
		return
	if code != 200:
		_fail("%s (%s)" % [data.get("message", "HTTP %d" % code), data.get("error", "")])
		return
	if data.get("game") is Dictionary and data.game.get("slots") is Array:
		var players: Array = []
		for slot in data.game.slots:
			if slot is Dictionary and slot.get("state") == "player" and slot.get("player_id") is String:
				players.append({"id": slot.player_id, "name": slot.get("name", ""), "you": slot.get("you", false)})
		report_context.emit({"game_id": data.game.get("id", _game_id), "session_token": token, "report_players": players})
	match _step:
		"signin":
			if data.get("notice") != null:
				var pending: Variant = data.notice
				if not pending is Dictionary or not pending.get("id") is String or not pending.get("text") is String or pending.id.is_empty() or pending.id.to_utf8_buffer().size() > 128 or pending.text.is_empty() or pending.text.to_utf8_buffer().size() > 4000:
					_fail("the sign-in notice is malformed")
					return
				notice.emit(AptMenuPlayer.online_redact(pending.text))
			token = data.token
			print("GAME ONLINE signed in as %s (%s)" % [data.account.name, data.account.kind])
			var f: FileAccess = null if spectate else FileAccess.open(SESSION_FILE, FileAccess.WRITE) # lane ONLINE-3: kept for a rejoin after a restart
			if f != null:
				f.store_string(token + "\n")
			_after_signin()
		"invite":
			if not data.get("id") is String or RegEx.create_from_string("^[0-9a-f]{16}$").search(data.id) == null or not data.get("game") is Dictionary or data.game.get("id") != data.id or not data.get("slot") is float or data.slot != floor(data.slot) or data.slot < 0 or data.slot >= 8:
				_fail("the lobby returned an invalid invitation acceptance")
				return
			join_id = data.id
			invite_id = ""
			_after_signin() # same token, ordinary idempotent join of the slot just reserved
		"mine":
			var games: Array = data.get("games", [])
			for g in games:
				print("GAME ONLINE running game %s, slot %d: rejoin offered" % [g.id, int(g.slot)])
			var pick: Array = games.filter(func(g): return join_id.is_empty() or g.id == join_id)
			if pick.size() != 1:
				_fail("%d running games of this player%s: nothing to rejoin" % [pick.size(), "" if join_id.is_empty() else " named " + join_id])
				return
			_game_id = pick[0].id
			_step = "rejoin"
			_send(HTTPClient.METHOD_POST, "/v1/games/%s/rejoin" % _game_id)
		"spectate":
			_step = "done"
			print("GAME ONLINE spectator START: game %s, spectator %d, delay %d s, relay %s" % [data.start.game, int(data.start.spectator), int(data.start.delay), data.start.relay])
			started.emit(JSON.stringify(data.start))
		"rejoin":
			_step = "done"
			print("GAME ONLINE rejoin START: slot %d, generation %d" % [int(data.start.slot), int(data.start.rejoin)])
			started.emit(JSON.stringify(data.start))
		"create":
			_game_id = data.id
			print("GAME ONLINE hosting game %s: invite openbfme://join/%s" % [_game_id, _game_id])
			_step = "wait"
		"join":
			_game_id = join_id
			print("GAME ONLINE joined game %s in slot %d" % [_game_id, int(data.slot)])
			_step = "ready"
			_send(HTTPClient.METHOD_POST, "/v1/games/%s/ready" % _game_id, {"ready": true})
		"ready":
			_ready_sent = true
			_step = "wait"
		"start":
			_step = "done"
			started.emit(JSON.stringify(data.start))
		"wait":
			_poll_at = Time.get_ticks_msec() + 1000
			for c in data.get("chat", []):
				_since = maxi(_since, int(c.seq))
				print("GAME ONLINE chat %s: %s" % [c.from if c.from != null else "lobby", c.text])
			if data.start != null:
				_step = "done"
				print("GAME ONLINE START: slot %d, relay %s" % [int(data.start.slot), data.start.relay])
				started.emit(JSON.stringify(data.start))
				return
			var g: Dictionary = data.game
			var humans: Array = g.slots.filter(func(s): return s.state == "player")
			var me: Array = g.slots.filter(func(s): return s.you)
			if join_id.is_empty():
				# the host starts once the wanted number of players is in and every other one is ready
				if humans.size() >= int(host_setup.get("players", 2)) and humans.all(func(s): return s.ready or s.you):
					_step = "start"
					_send(HTTPClient.METHOD_POST, "/v1/games/%s/start" % _game_id)
			elif not me.is_empty() and not me[0].ready:
				_step = "ready" # an options change unreadies everyone: ready again
				_send(HTTPClient.METHOD_POST, "/v1/games/%s/ready" % _game_id, {"ready": true})
