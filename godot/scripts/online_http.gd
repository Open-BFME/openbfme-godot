## OpenBFME. GPL-3.0.
## Lane ONLINE-4 (docs/ONLINE.md section 5, phase O2): the transport of the online screens' lobby client. OnlineShell.apt's engine side
## (GameClient/GUI/AptScreens/AptOnlineShell.h) and its client (GameNetwork/OnlineLobbyClient.h) do no I/O: they queue HTTP requests, this node sends them
## with Godot's HTTPRequest (TLS for https) and hands every answer back (AptMenuPlayer.online_take_requests / online_deliver). One request at a time, in
## order. As ONLINE-2's online_lobby.gd: no redirect is followed (a 3xx is an error: the bearer token stays with the lobby the player named), and the lobby
## URL must be a plain http(s)://host[:port][/path].
extends Node

var shell: Object = null
var lobby_url := ""
var problem := ""          ## why the lobby URL is refused ("" when it is fine)

const BODY_LIMIT := 262144 ## OnlineLobbyClient::kMaxBodyBytes

var _http: HTTPRequest
var _id := 0               ## the request in flight (0: none)


func _ready() -> void:
	_http = HTTPRequest.new()
	_http.timeout = 20.0
	_http.max_redirects = 0
	_http.body_size_limit = BODY_LIMIT # Sol r1: a hostile lobby's answer is not read beyond the bound (the client refuses a larger one too)
	add_child(_http)
	_http.request_completed.connect(_on_done)
	problem = load("res://scripts/online_lobby.gd").lobby_url_problem(lobby_url)
	if not problem.is_empty():
		printerr("GAME ONLINE error: ", problem)


func _process(_delta: float) -> void:
	if shell == null or _id != 0:
		return
	var requests: Array = shell.online_take_requests()
	if requests.is_empty():
		return
	var q: Dictionary = requests[0]
	if not problem.is_empty():
		shell.online_deliver(q.id, 0, "", problem)
		return
	var headers := PackedStringArray(["content-type: application/json", "accept: application/json"])
	if not String(q.token).is_empty():
		headers.append("authorization: Bearer " + q.token)
	var method := HTTPClient.METHOD_POST if q.method == "POST" else HTTPClient.METHOD_GET
	_id = q.id
	var err := _http.request(lobby_url.trim_suffix("/") + q.path, headers, method, q.body if method == HTTPClient.METHOD_POST else "")
	if err != OK:
		var id := _id
		_id = 0
		shell.online_deliver(id, 0, "", "cannot reach the lobby %s: %s" % [lobby_url, error_string(err)])


func _on_done(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	var id := _id
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
