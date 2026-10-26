## OpenBFME. GPL-3.0. Scripted rejoin list regression; needs the native extension, not retail data.
## godot --headless --path godot --script res://tests/online_list_client_test.gd
extends SceneTree

var checks := 0
var failures := 0


func _init() -> void:
	_run.call_deferred()


func _check(ok: bool, message: String) -> void:
	checks += 1
	if not ok:
		failures += 1
	print("LIST CLIENT %s %s" % ["ok" if ok else "FAIL", message])


func _run() -> void:
	if not ClassDB.class_exists("AptMenuPlayer"):
		var extension := load("res://openbfme.gdextension")
		if extension == null:
			quit(1)
			return
	var script: GDScript = load("res://scripts/online_lobby.gd")
	var game := {"id": "0123456789abcdef", "name": "Evening game", "host": "Player", "map": "maps/test", "state": "started",
		"players": 2, "openSlots": 0, "password": false, "slot": 1}
	var other := game.duplicate()
	other.id = "fedcba9876543210"
	for body in [JSON.stringify({"v": 1, "games": [game]}), JSON.stringify({"v": 1, "games": [{}, game, other]}).replace("Evening game", "\\ud800")]:
		var client: Node = script.new()
		client.lobby_url = "http://127.0.0.1:1"
		client.join_id = game.id
		client.token = "test-session"
		client._step = "mine"
		var errors: Array = []
		client.failed.connect(func(message): errors.append(message))
		root.add_child(client)
		client._make_http()
		client._on_response(HTTPRequest.RESULT_SUCCESS, 200, PackedStringArray(), body.to_utf8_buffer())
		_check(client._step == "rejoin" and client._game_id == game.id, "normal or repaired mine list selects the requested surviving game")
		_check(errors.is_empty() and client.token == "test-session", "malformed row does not fail or clear the rejoin session")
		client._http.cancel_request()
		client.free()
	for body in ["garbage", "[]", '{"v":2,"games":[]}', '{"v":1,"games":{}}']:
		var client: Node = script.new()
		client._step = "mine"
		var errors: Array = []
		client.failed.connect(func(message): errors.append(message))
		client._on_response(HTTPRequest.RESULT_SUCCESS, 200, PackedStringArray(), body.to_utf8_buffer())
		_check(client._step == "failed" and errors.size() == 1 and not errors[0].is_empty(), "bad top-level mine response emits one explicit failure")
		client.free()
	print("ONLINE LIST CLIENT: %d passed, %d failed, %d checks" % [checks - failures, failures, checks])
	quit(0 if failures == 0 else 1)
