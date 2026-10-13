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

signal started(start_json: String)
signal failed(message: String)

const BODY_LIMIT := 262144 ## Sol r1 (ONLINE-4): the largest lobby answer read (OnlineLobbyClient::kMaxBodyBytes)

var lobby_url := ""
var player_name := "Player"
var profile := ""
var join_id := ""          ## join this game; empty: host one with host_setup
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


func begin() -> void:
	_http = HTTPRequest.new()
	_http.timeout = 20.0
	_http.max_redirects = 0 # ONLINE-2 r2: never follow a redirect (Godot would send the bearer token to the new origin)
	_http.body_size_limit = BODY_LIMIT # Sol r1 (ONLINE-4): a hostile lobby's answer is not read beyond the bound
	add_child(_http)
	_http.request_completed.connect(_on_response)
	if lobby_url.is_empty():
		_fail("no lobby: --online-lobby=URL (or OPENBFME_LOBBY)")
		return
	var problem := lobby_url_problem(lobby_url)
	if not problem.is_empty():
		_fail(problem)
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
		_step = "signin"
		_send(HTTPClient.METHOD_POST, "/v1/auth/guest", {"name": player_name})
	else:
		_after_signin()


func _process(_delta: float) -> void:
	if _step == "wait" and not _busy and Time.get_ticks_msec() >= _poll_at:
		_send(HTTPClient.METHOD_GET, "/v1/games/%s?since=%d" % [_game_id, _since])
	elif _step == "spectate_wait" and not _busy and Time.get_ticks_msec() >= _poll_at:
		_step = "spectate"
		_send(HTTPClient.METHOD_POST, "/v1/games/%s/spectate" % join_id)


func _send(method: int, path: String, body: Variant = null) -> void:
	var headers := PackedStringArray(["content-type: application/json", "accept: application/json"])
	if not token.is_empty():
		headers.append("authorization: Bearer " + token)
	_busy = true
	var err := _http.request(lobby_url.trim_suffix("/") + path, headers, method, "" if body == null else JSON.stringify(body))
	if err != OK:
		_fail("cannot reach the lobby %s: %s" % [lobby_url, error_string(err)])


func _fail(message: String) -> void:
	_step = "failed"
	message = AptMenuPlayer.online_redact(message) # Sol r1 (ONLINE-4): a ticket or token the lobby echoed is never printed or shown
	printerr("GAME ONLINE error: ", message)
	failed.emit(message)


func _after_signin() -> void:
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
		body["profile"] = profile
		body.erase("players")
		_send(HTTPClient.METHOD_POST, "/v1/games", body)
	else:
		_step = "join"
		_send(HTTPClient.METHOD_POST, "/v1/games/%s/join" % join_id, {"profile": profile})


func _on_response(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	_busy = false
	if result == HTTPRequest.RESULT_REDIRECT_LIMIT_REACHED or (code >= 300 and code < 400):
		_fail("the lobby %s answered with a redirect: not followed (the session stays with the lobby you named)" % lobby_url)
		return
	if result == HTTPRequest.RESULT_BODY_SIZE_LIMIT_EXCEEDED:
		_fail("the lobby %s answered with more than %d bytes: not read" % [lobby_url, BODY_LIMIT])
		return
	if result != HTTPRequest.RESULT_SUCCESS:
		_fail("the lobby %s did not answer (%d)" % [lobby_url, result])
		return
	var data: Variant = JSON.parse_string(body.get_string_from_utf8())
	if typeof(data) != TYPE_DICTIONARY or int(data.get("v", 0)) != 1:
		_fail("the lobby answered HTTP %d with something that is not lobby version 1" % code)
		return
	if code == 409 and _step == "spectate" and data.get("error", "") == "not_started":
		if _poll_at == 0:
			print("GAME ONLINE game %s has not started: waiting to spectate" % join_id)
		_step = "spectate_wait" # lane ONLINE-3: asked again once a second until the host starts it
		_poll_at = Time.get_ticks_msec() + 1000
		return
	if code != 200:
		_fail("%s (%s)" % [data.get("message", "HTTP %d" % code), data.get("error", "")])
		return
	match _step:
		"signin":
			token = data.token
			print("GAME ONLINE signed in as %s (%s)" % [data.account.name, data.account.kind])
			var f: FileAccess = null if spectate else FileAccess.open(SESSION_FILE, FileAccess.WRITE) # lane ONLINE-3: kept for a rejoin after a restart
			if f != null:
				f.store_string(token + "\n")
			_after_signin()
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
