## OpenBFME. GPL-3.0.
## Lane ONLINEMENU-1: scripts/http_timed.gd against a fake server (tools/net/test_http_timed.py). Run with -- --server=<url: /late answers
## after 2 s, /second after 1 s, /never never>. One "TIMED ok|FAIL" line per check; exit 0 when all hold.
extends SceneTree

const HttpTimed := preload("res://scripts/http_timed.gd")

var _ok := true
var _server := ""


func _init() -> void:
	_run.call_deferred()


func _check(what: String, cond: bool, detail := "") -> void:
	print("TIMED %s %s%s" % ["ok  " if cond else "FAIL", what, ": " + detail if not cond or detail != "" else ""])
	_ok = _ok and cond


func _make() -> HTTPRequest:
	var h := HTTPRequest.new()
	h.max_redirects = 0
	root.add_child(h)
	return h


## [result, code, wall seconds] of one GET; `blocked` frames of `block_ms` each block the main thread right after the request starts (a load)
func _fetch(h: HTTPRequest, path: String, blocked := 0, block_ms := 0) -> Array:
	var got := []
	var on_done := func(result: int, code: int, _headers: PackedStringArray, _body: PackedByteArray) -> void: got.append_array([result, code])
	h.request_completed.connect(on_done, CONNECT_ONE_SHOT)
	var t0 := Time.get_ticks_msec()
	var err := h.request(_server + path)
	if err != OK:
		return [-1, 0, 0.0]
	for i in blocked:
		OS.delay_msec(block_ms)
		await process_frame
	var until := Time.get_ticks_msec() + 30000
	while got.is_empty() and Time.get_ticks_msec() < until:
		await process_frame
	return [got[0] if got.size() > 0 else -1, got[1] if got.size() > 1 else 0, (Time.get_ticks_msec() - t0) / 1000.0]


func _run() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--server="):
			_server = a.substr(9)
	# why the helper exists: Godot's own timeout counts the blocked frames (six 1.5 s frames = 9 s of a 3 s limit) though the answer came at 2 s
	var plain := _make()
	plain.timeout = 3.0
	var r: Array = await _fetch(plain, "/late", 6, 1500)
	_check("Godot's own timeout runs out during a blocking load", r[0] == HTTPRequest.RESULT_TIMEOUT, "result %d code %d after %.1f s" % r)
	# the helper: the same request and the same load, a 3 s budget of running frames: the answer is read
	var timed := _make()
	HttpTimed.set_budget(timed, 3.0)
	_check("set_budget turns Godot's own timeout off", timed.timeout == 0.0, str(timed.timeout))
	r = await _fetch(timed, "/late", 6, 1500)
	_check("a request started during a blocking load is not timed out early", r[0] == HTTPRequest.RESULT_SUCCESS and r[1] == 200,
		"result %d code %d after %.1f s" % r)
	# a server that never answers still ends: after the budget of running frames (their deltas, which run a little ahead of the wall clock), as
	# Godot's own timeout does (RESULT_TIMEOUT)
	var never := _make()
	HttpTimed.set_budget(never, 1.5)
	r = await _fetch(never, "/never")
	_check("no answer ends after the budget", r[0] == HTTPRequest.RESULT_TIMEOUT and r[2] >= 1.0 and r[2] < 5.0, "result %d code %d after %.1f s" % r)
	_check("the timed-out request is idle again", never.get_http_client_status() == HTTPClient.STATUS_DISCONNECTED, str(never.get_http_client_status()))
	# the budget is per request: two 1 s answers in a row on one node with a 1.5 s budget both arrive (the second starts from zero)
	var twice := _make()
	HttpTimed.set_budget(twice, 1.5)
	var first: Array = await _fetch(twice, "/second")
	var second: Array = await _fetch(twice, "/second")
	_check("each request has its own budget", first[0] == HTTPRequest.RESULT_SUCCESS and second[0] == HTTPRequest.RESULT_SUCCESS,
		"%s then %s" % [str(first), str(second)])
	# a cancelled request followed by a new one in the same frame: set_budget starts the new one from zero (online_http.gd's held polls)
	var held := _make()
	HttpTimed.set_budget(held, 1.5)
	held.request(_server + "/never")
	for i in 60:
		await process_frame
	var used := HttpTimed.waited(held)
	held.cancel_request()
	HttpTimed.set_budget(held, 1.5)
	_check("set_budget starts the next request from zero", used > 0.0 and HttpTimed.waited(held) == 0.0, "%.2f then %.2f" % [used, HttpTimed.waited(held)])
	r = await _fetch(held, "/second")
	_check("the request after a cancel gets its whole budget", r[0] == HTTPRequest.RESULT_SUCCESS, "result %d code %d after %.1f s" % r)
	quit(0 if _ok else 1)
