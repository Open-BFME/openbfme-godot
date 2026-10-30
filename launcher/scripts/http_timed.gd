## OpenBFME. GPL-3.0.
## Lane ONLINEMENU-1 (follow-up of CRASHACT-1 r3): a request's time limit that counts only running frames. Godot's own HTTPRequest.timeout
## counts wall time, so frames that block the main thread (a map or menu load: 10 s on a slow PC) use it up while the reply is never polled,
## and the request times out although the server answered (srv1 om1node3-srv1: the menu's player count, "no answer: 13 / 0").
## set_budget(request, seconds) turns Godot's timer off and adds a watcher child: while the request is in flight, frames shorter than
## LONG_FRAME add up, and past `seconds` the request is cancelled and request_completed fires with RESULT_TIMEOUT, as Godot's own timeout does.
## The watcher opens nothing: it only reads the request's status and cancels it.
## launcher/scripts/http_timed.gd is the same bytes (the crash client, the same bytes in both, preloads "../http_timed.gd"; tools/net/test_http_timed.py).
extends Node

const LONG_FRAME := 1.0  ## a frame longer than this (a blocking load) does not count toward the budget
const WATCHER := "HttpTimed"
const RESULT_TIMEOUT := 13      ## HTTPRequest.RESULT_TIMEOUT (named here only: this file uses no networking class, tools/release/net_guard.py)
const STATUS_DISCONNECTED := 0  ## HTTPClient.STATUS_DISCONNECTED: no request in flight

var budget_s := 0.0  ## 0: no limit
var waited_s := 0.0  ## running seconds of the request in flight


## the request's time limit, in seconds of running frames (0: none). Call it again before a request that follows a cancel_request() in the
## same frame (online_http.gd's held polls): the watcher would not see the request idle in between, and the new one starts from zero here.
static func set_budget(request: Node, seconds: float) -> void:
	request.set("timeout", 0.0)
	var w: Node = request.get_node_or_null(WATCHER)
	if w == null:
		w = (load("res://scripts/http_timed.gd") as GDScript).new()
		w.name = WATCHER
		request.add_child(w)
	w.budget_s = seconds
	w.waited_s = 0.0


## the running seconds the request in flight has used (0 when there is none or no budget was set)
static func waited(request: Node) -> float:
	var w: Node = request.get_node_or_null(WATCHER)
	return w.waited_s if w != null else 0.0


func _ready() -> void:
	get_parent().connect("request_completed", _on_completed)


## an answer (or a failure) ends the request: the next one starts from zero even if it is sent in the same frame
func _on_completed(_result: int, _code: int, _headers: PackedStringArray, _body: PackedByteArray) -> void:
	waited_s = 0.0


func _process(delta: float) -> void:
	var request := get_parent()
	if request == null or int(request.call("get_http_client_status")) == STATUS_DISCONNECTED:
		waited_s = 0.0
		return
	if delta < LONG_FRAME:
		waited_s += delta
	if budget_s > 0.0 and waited_s > budget_s:
		request.call("cancel_request")
		request.emit_signal("request_completed", RESULT_TIMEOUT, 0, PackedStringArray(), PackedByteArray())
