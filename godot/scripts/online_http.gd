## OpenBFME. GPL-3.0.
## Lane ONLINE-4 (docs/ONLINE.md section 5, phase O2): the transport of the online screens' lobby client. OnlineShell.apt's engine side
## (GameClient/GUI/AptScreens/AptOnlineShell.h) and its client (GameNetwork/OnlineLobbyClient.h) do no I/O: they queue HTTP requests, this node sends them
## with Godot's HTTPRequest (TLS for https) and hands every answer back (AptMenuPlayer.online_take_requests / online_deliver). One request at a time, in
## order. As ONLINE-2's online_lobby.gd: no redirect is followed (a 3xx is an error: the bearer token stays with the lobby the player named), and the lobby
## URL must be a plain http(s)://host[:port][/path].
## Lane ONLINE-5: beside the ordinary requests, one held poll at a time (a request marked `held`: the lobby keeps it until the staging room changes, at
## most hold_seconds) travels on a connection of its own, so a chat line or a ready tick sent meanwhile does not wait for it.
extends Node

var shell: Object = null
var lobby_url := ""
var problem := ""          ## why the lobby URL is refused ("" when it is fine)

const BODY_LIMIT := 262144 ## OnlineLobbyClient::kMaxBodyBytes
const HELD_SLACK := 15.0   ## a held poll's timeout beyond the seconds the lobby may keep it (the round trip, a slow link)

var _http: HTTPRequest
var _held: HTTPRequest     ## lane ONLINE-5: the held poll's connection
var _id := 0               ## the request in flight (0: none)
var _held_id := 0          ## the held poll in flight (0: none)


func _make() -> HTTPRequest:
	var h := HTTPRequest.new()
	h.timeout = 20.0
	h.max_redirects = 0
	h.body_size_limit = BODY_LIMIT # Sol r1: a hostile lobby's answer is not read beyond the bound (the client refuses a larger one too)
	add_child(h)
	return h


func _ready() -> void:
	_http = _make()
	_http.request_completed.connect(_on_done.bind(false))
	_held = _make()
	_held.request_completed.connect(_on_done.bind(true))
	problem = load("res://scripts/online_lobby.gd").lobby_url_problem(lobby_url)
	if not problem.is_empty():
		printerr("GAME ONLINE error: ", problem)


func _process(_delta: float) -> void:
	if shell == null:
		return
	# the client hands out at most one ordinary request (none while one is in flight) and one held poll (likewise)
	for q: Dictionary in shell.online_take_requests():
		_send(q)


func _send(q: Dictionary) -> void:
	var held: bool = q.get("held", false)
	if not problem.is_empty():
		shell.online_deliver(q.id, 0, "", problem)
		return
	var node := _held if held else _http
	if held:
		if _held_id != 0:
			node.cancel_request() # the poll of a game the player left: its answer is nobody's
		node.timeout = float(int(q.get("hold_seconds", 0))) + HELD_SLACK
		_held_id = q.id
	else:
		_id = q.id
	var headers := PackedStringArray(["content-type: application/json", "accept: application/json"])
	if not String(q.token).is_empty():
		headers.append("authorization: Bearer " + q.token)
	var method := HTTPClient.METHOD_POST if q.method == "POST" else HTTPClient.METHOD_GET
	var err := node.request(lobby_url.trim_suffix("/") + q.path, headers, method, q.body if method == HTTPClient.METHOD_POST else "")
	if err != OK:
		if held:
			_held_id = 0
		else:
			_id = 0
		shell.online_deliver(q.id, 0, "", "cannot reach the lobby %s: %s" % [lobby_url, error_string(err)])


func _on_done(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray, held: bool) -> void:
	var id := _held_id if held else _id
	if held:
		_held_id = 0
	else:
		_id = 0
	if shell == null:
		return
	if result == HTTPRequest.RESULT_REDIRECT_LIMIT_REACHED or (code >= 300 and code < 400):
		shell.online_deliver(id, code, "", "the lobby %s answered with a redirect: not followed" % lobby_url)
	elif result == HTTPRequest.RESULT_BODY_SIZE_LIMIT_EXCEEDED:
		shell.online_deliver(id, 0, "", "the lobby %s answered with more than %d bytes: not read" % [lobby_url, BODY_LIMIT])
	elif result != HTTPRequest.RESULT_SUCCESS:
		shell.online_deliver(id, 0, "", "the lobby %s did not answer (%d)" % [lobby_url, result])
	else:
		shell.online_deliver(id, code, body.get_string_from_utf8(), "")
