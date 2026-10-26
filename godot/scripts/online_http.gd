## OpenBFME. GPL-3.0.
## Lane ONLINE-4 (docs/ONLINE.md section 5, phase O2): the transport of the online screens' lobby client. OnlineShell.apt's engine side
## (GameClient/GUI/AptScreens/AptOnlineShell.h) and its client (GameNetwork/OnlineLobbyClient.h) do no I/O: they queue HTTP requests, this node sends them
## with Godot's HTTPRequest (TLS for https) and hands every answer back (AptMenuPlayer.online_take_requests / online_deliver). One request at a time, in
## order. As ONLINE-2's online_lobby.gd: no redirect is followed (a 3xx is an error: the bearer token stays with the lobby the player named), and the lobby
## URL must be a plain http(s)://host[:port][/path].
## Lane ONLINE-5: beside the ordinary requests, one held poll at a time (a request marked `held`: the lobby keeps it until the staging room changes, at
## most hold_seconds) travels on a connection of its own, so a chat line or a ready tick sent meanwhile does not wait for it.
## Lane ONLINEHUB-1: the same transport carries the OpenBFME platform API's requests (kind "platform": AptMenuPlayer.platform_take_requests /
## platform_deliver; GET / POST / DELETE, no token); `lobby_url` is then the platform's base URL (it names the version: .../v1).
extends Node

const OnlineList := preload("res://scripts/online_list.gd")
const SocialList := preload("res://scripts/online_social_list.gd")

var shell: Object = null
var lobby_url := ""
var problem := ""          ## why the lobby URL is refused ("" when it is fine)
var kind := "lobby"        ## lane ONLINEHUB-1: "lobby" or "platform"

const BODY_LIMIT := 262144 ## OnlineLobbyClient::kMaxBodyBytes
const HELD_SLACK := 15.0   ## a held poll's timeout beyond the seconds the lobby may keep it (the round trip, a slow link)

var _http: HTTPRequest
var _held: HTTPRequest     ## lane ONLINE-5: the held poll's connection
var _inbox: HTTPRequest    ## the independent social inbox hold
var _id := 0               ## the request in flight (0: none)
var _held_id := 0          ## the held poll in flight (0: none)
var _inbox_id := 0         ## the social inbox hold in flight (0: none)
var _list_requests := {}


func _make() -> HTTPRequest:
	var h := HTTPRequest.new()
	h.timeout = 20.0
	h.max_redirects = 0
	h.body_size_limit = BODY_LIMIT # Sol r1: a hostile lobby's answer is not read beyond the bound (the client refuses a larger one too)
	add_child(h)
	return h


func _ready() -> void:
	_http = _make()
	_http.request_completed.connect(_on_done.bind("ordinary"))
	_held = _make()
	_held.request_completed.connect(_on_done.bind("room"))
	_inbox = _make()
	_inbox.request_completed.connect(_on_done.bind("social"))
	problem = load("res://scripts/online_lobby.gd").lobby_url_problem(lobby_url)
	if not problem.is_empty():
		printerr("GAME ONLINE error: ", problem)


func _process(_delta: float) -> void:
	if shell == null:
		return
	# the client hands out at most one ordinary request (none while one is in flight) and one held poll (likewise)
	for q: Dictionary in (shell.platform_take_requests() if kind == "platform" else shell.online_take_requests()):
		_send(q)


func _deliver(id: int, code: int, body: String, error: String) -> void:
	if kind == "platform":
		shell.platform_deliver(id, code, body, error)
	else:
		shell.online_deliver(id, code, body, error)


func _service() -> String:
	return "the online services" if kind == "platform" else "the lobby"


func _send(q: Dictionary) -> void:
	var held: bool = q.get("held", false)
	if not problem.is_empty():
		_deliver(q.id, 0, "", problem)
		return
	var channel: String = q.get("channel", "ordinary")
	var node: HTTPRequest
	if channel == "social":
		node = _inbox
	elif held:
		node = _held
	else:
		node = _http
	if channel == "social":
		if _inbox_id != 0:
			_list_requests.erase(_inbox_id)
			node.cancel_request()
		node.timeout = float(int(q.get("hold_seconds", 0))) + HELD_SLACK
		_inbox_id = q.id
	elif held:
		if _held_id != 0:
			_list_requests.erase(_held_id)
			node.cancel_request() # the poll of a game the player left: its answer is nobody's
		node.timeout = float(int(q.get("hold_seconds", 0))) + HELD_SLACK
		_held_id = q.id
	else:
		_id = q.id
	var path: String = q.path.get_slice("?", 0)
	if kind == "lobby" and q.method == "GET" and (path in ["/v1/games", "/v1/mine"] or SocialList.handles(q.path)):
		_list_requests[q.id] = q.path
	var headers := PackedStringArray(["content-type: application/json", "accept: application/json"])
	headers.append("x-openbfme-max-slots: " + str(q.get("max_slots", 8)))
	if not String(q.token).is_empty():
		headers.append("authorization: Bearer " + q.token)
	var method: int
	match String(q.get("method", "")):
		"GET":
			method = HTTPClient.METHOD_GET
		"POST":
			method = HTTPClient.METHOD_POST
		"PUT":
			method = HTTPClient.METHOD_PUT
		"DELETE":
			method = HTTPClient.METHOD_DELETE
		_:
			_list_requests.erase(q.id)
			_clear_request_id(channel, held)
			_deliver(q.id, 0, "", "unsupported HTTP method")
			return
	var request_body := String(q.body) if method == HTTPClient.METHOD_POST or method == HTTPClient.METHOD_PUT else ""
	var err := node.request(lobby_url.trim_suffix("/") + q.path, headers, method, request_body)
	if err != OK:
		_list_requests.erase(q.id)
		_clear_request_id(channel, held)
		_deliver(q.id, 0, "", "cannot reach %s %s: %s" % [_service(), lobby_url, error_string(err)])


func _clear_request_id(channel: String, held: bool) -> void:
	if channel == "social":
		_inbox_id = 0
	elif held:
		_held_id = 0
	else:
		_id = 0


func _on_done(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray, channel: String) -> void:
	var id := _inbox_id if channel == "social" else (_held_id if channel == "room" else _id)
	var list_path: String = _list_requests.get(id, "")
	_list_requests.erase(id)
	_clear_request_id(channel, channel == "room")
	if shell == null:
		return
	if result == HTTPRequest.RESULT_REDIRECT_LIMIT_REACHED or (code >= 300 and code < 400):
		_deliver(id, code, "", "%s %s answered with a redirect: not followed" % [_service(), lobby_url])
	elif result == HTTPRequest.RESULT_BODY_SIZE_LIMIT_EXCEEDED:
		_deliver(id, 0, "", "%s %s answered with more than %d bytes: not read" % [_service(), lobby_url, BODY_LIMIT])
	elif result != HTTPRequest.RESULT_SUCCESS:
		_deliver(id, 0, "", "%s %s did not answer (%d)" % [_service(), lobby_url, result])
	elif code == 200 and not list_path.is_empty():
		var parsed: Dictionary
		if SocialList.handles(list_path):
			var self_pid := ""
			if list_path.begins_with("/v1/chat/") and shell.has_method("online_social_status"):
				self_pid = shell.online_social_status().get("me", {}).get("pid", "")
			parsed = SocialList.parse(body, list_path, self_pid)
		else:
			parsed = OnlineList.parse(body, list_path.get_slice("?", 0) == "/v1/mine")
		_deliver(id, code, parsed.text if parsed.ok else "", "" if parsed.ok else parsed.error)
	else:
		_deliver(id, code, body.get_string_from_utf8(), "")
