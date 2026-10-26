## OpenBFME. GPL-3.0. Asset-free social list decoding and response regressions.
## godot --headless --path launcher --script res://tests/lobby_list_test.gd
extends SceneTree

const Client := preload("res://scripts/net/social_client.gd")
const LobbyJSON := preload("res://scripts/net/lobby_json.gd")
const ME := "1111111111111111"
const PEER := "2222222222222222"
var checks := 0
var failures := 0


func _init() -> void:
	_run.call_deferred()


func _check(ok: bool, name: String) -> void:
	checks += 1
	if not ok: failures += 1
	print("LAUNCHER LIST %s %s" % ["ok" if ok else "FAIL", name])


func _fixtures() -> Dictionary:
	var player := {"pid": PEER, "nick": "Friend", "code": "0123456789"}
	var me := {"pid": ME, "nick": "Me", "code": "0123456789"}
	var presence := {"pid": PEER, "state": "online", "game": null, "map": "Friend", "mode": null, "joinable": false, "since": 1}
	var friend := player.duplicate(true)
	friend.merge({"since": 1, "presence": presence})
	return {
		"friends": {"v": 1, "friends": [friend]},
		"poll": {"v": 1, "seq": "aaaaaaaaaaaaaaaa:1", "reset": true, "presence": [presence],
			"requests": [{"id": "3333333333333333", "from": player, "to": me, "at": 1}],
			"invites": [{"id": "4444444444444444", "from": player, "to": me, "game": "5555555555555555", "kind": "invite", "mode": null, "note": "Friend", "expires": 1}],
			"notices": [{"id": "notice_1", "kind": "expired", "invite": null, "at": 1}], "chat": [{"pid": PEER, "last_id": 1, "read_id": 0}],
			"stats": {"v": 1, "players": 1, "games_open": 0, "games_running": 0, "as_of": 1}, "wait": 25},
		"chat_get": {"v": 1, "messages": [{"id": 1, "from": PEER, "to": ME, "text": "Friend", "at": 1, "client_id": "a".repeat(32)}], "next": null, "last_id": 1, "read_id": 0},
	}


func _validate(client: Node, kind: String, data: Dictionary) -> Dictionary:
	match kind:
		"friends": return client._validate_friends(data)
		"poll": return client._validate_snapshot(data)
		_: return client._validate_chat(data, 0, PEER)


func _run() -> void:
	var client := Client.new()
	root.add_child(client)
	client.me = {"pid": ME}
	var fixtures := _fixtures()
	for kind in fixtures:
		var text := JSON.stringify(fixtures[kind])
		var parsed: Dictionary = client._parse_json(text.to_utf8_buffer(), true)
		_check(parsed.ok and parsed.data == JSON.parse_string(text) and _validate(client, kind, parsed.data).ok, "normal list unchanged: " + kind)
		for escape in ["\\ud800", "\\uDBFF", "\\udc00", "\\uDFFF", "\\ud800x\\udc00"]:
			parsed = client._parse_json(text.replace("Friend", escape).to_utf8_buffer(), true)
			_check(parsed.ok and _validate(client, kind, parsed.data).ok and JSON.stringify(parsed.data).contains("�"), "repairs social text: " + kind + " " + escape)
		var spans := text.split("Friend", true, 1)
		parsed = client._parse_json(spans[0].to_utf8_buffer() + PackedByteArray([0xed, 0xa0, 0x80, 0xff]) + spans[1].to_utf8_buffer(), true)
		_check(parsed.ok and _validate(client, kind, parsed.data).ok and JSON.stringify(parsed.data).contains("�"), "repairs invalid UTF-8: " + kind)
		for bad in ["garbage", "[]", "null", "1"]:
			_check(not client._parse_json(bad.to_utf8_buffer(), true).ok, "rejects non-object envelope: " + kind)
		for suffix in [PackedByteArray([0]), PackedByteArray([0]) + "garbage".to_utf8_buffer()]:
			_check(not client._parse_json(text.to_utf8_buffer() + suffix, true).ok, "rejects raw NUL: " + kind)
	for collection in ["friends", "presence", "requests", "invites", "notices", "chat", "messages"]:
		var kind := "friends" if collection == "friends" else ("chat_get" if collection == "messages" else "poll")
		var data: Dictionary = fixtures[kind].duplicate(true)
		var normal: Dictionary = _validate(client, kind, data)
		data[collection].push_front({})
		data[collection].append(null)
		data[collection].append({"id": false, "pid": 123})
		_check(_validate(client, kind, data) == normal, "skips only malformed rows: " + collection)
		data = fixtures[kind].duplicate(true)
		data[collection] = "invalid"
		_check(not _validate(client, kind, data).ok, "rejects non-array list: " + collection)
		data[collection] = []
		data[collection].resize(501)
		_check(not _validate(client, kind, data).ok, "checks raw collection size before filtering: " + collection)
	for collection in ["friends", "presence", "requests", "invites", "notices", "chat"]:
		var kind := "friends" if collection == "friends" else "poll"
		var data: Dictionary = fixtures[kind].duplicate(true)
		var normal: Dictionary = _validate(client, kind, data)
		data[collection].append(data[collection][0].duplicate(true))
		_check(_validate(client, kind, data) == normal, "skips duplicate identities: " + collection)
	for key in ["seq", "reset", "stats", "backoff", "wait"]:
		var data: Dictionary = fixtures.poll.duplicate(true)
		data[key] = "invalid"
		_check(not client._validate_snapshot(data).ok, "inbox metadata stays strict: " + key)
	var chat: Dictionary = fixtures.chat_get.duplicate(true)
	chat.messages.append({"id": 2, "text": 3})
	chat.next = 2
	chat.last_id = 3
	var page: Dictionary = client._validate_chat(chat, 0, PEER)
	_check(page.ok and page.result.messages.size() == 1 and page.result.next == 1 and page.result.last_id == 3, "malformed message tail preserves watermark and usable cursor")
	chat.read_id = 4
	_check(not client._validate_chat(chat, 0, PEER).ok, "invalid read watermark fails")
	chat.read_id = 0
	chat.next = 1
	_check(not client._validate_chat(chat, 0, PEER).ok, "inconsistent cursor fails")
	var literal := '"literal: \\\\ud800"'
	_check(LobbyJSON.sanitize(literal) == literal, "literal escaped backslash unchanged")
	var pair := '"\\ud83d\\ude00"'
	_check(LobbyJSON.sanitize(pair) == pair and JSON.parse_string(pair) == "😀", "valid surrogate pair unchanged")
	_response_checks(client, fixtures)
	client.free()
	print("LAUNCHER LIST: %d passed, %d failed, %d checks" % [checks - failures, failures, checks])
	quit(0 if failures == 0 else 1)


func _response_checks(client: Node, fixtures: Dictionary) -> void:
	var errors: Array = []
	var chats: Array = []
	client.problem.connect(func(message): errors.append(message))
	client.chat_ready.connect(func(_pid, page): chats.append(page))
	var data: Dictionary = fixtures.friends.duplicate(true)
	data.friends.append({})
	var success: bool = client._process_response(HTTPRequest.RESULT_SUCCESS, 200, PackedStringArray(),
		JSON.stringify(data).replace("Friend", "\\ud800").to_utf8_buffer(), {"kind": "friends_refresh", "meta": {}})
	_check(success and errors.is_empty() and client.friends.size() == 1 and client.friends[0].nick == "�", "friends response path retains repaired row and skips malformed one")
	data = fixtures.poll.duplicate(true)
	for key in ["presence", "requests", "invites", "notices", "chat"]: data[key].append({})
	client._process_response(HTTPRequest.RESULT_SUCCESS, 200, PackedStringArray(),
		JSON.stringify(data).replace("Friend", "\\udc00").to_utf8_buffer(), {"kind": "poll", "meta": {}})
	_check(errors.is_empty() and client.snapshot.requests.size() == 1 and client.snapshot.requests[0].from.nick == "�", "independent inbox response path retains all valid collections")
	data = fixtures.chat_get.duplicate(true)
	data.messages.push_front({})
	success = client._process_response(HTTPRequest.RESULT_SUCCESS, 200, PackedStringArray(),
		JSON.stringify(data).replace("Friend", "\\ud800").to_utf8_buffer(), {"kind": "chat_get", "meta": {"pid": PEER, "after": 0}})
	_check(success and errors.is_empty() and chats.size() == 1 and chats[0].messages[0].text == "�", "messenger response path repairs text and skips malformed message")
	for bad in ["garbage", "[]", '{"v":2,"friends":[]}', '{"v":1,"friends":{}}']:
		errors.clear()
		success = client._process_response(HTTPRequest.RESULT_SUCCESS, 200, PackedStringArray(), bad.to_utf8_buffer(), {"kind": "friends_refresh", "meta": {}})
		_check(not success and errors.size() == 1 and client.friends.size() == 1, "bad response envelope fails clearly and preserves prior friends")
	data = fixtures.poll.duplicate(true)
	data.requests[0].from.device = "synthetic-private-field"
	errors.clear()
	client._process_response(HTTPRequest.RESULT_SUCCESS, 200, PackedStringArray(), JSON.stringify(data).to_utf8_buffer(), {"kind": "poll", "meta": {}})
	_check(errors.size() == 1, "private identity guard still rejects the entire inbox")
	for kind in ["auth", "chat_send"]:
		errors.clear()
		success = client._process_response(HTTPRequest.RESULT_SUCCESS, 200, PackedStringArray(), '{"v":1,"text":"\\ud800"}'.to_utf8_buffer(), {"kind": kind, "meta": {}})
		_check(not success and errors.size() == 1, "non-list protocol response stays strict: " + kind)
