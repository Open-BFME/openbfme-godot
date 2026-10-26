## OpenBFME. GPL-3.0. Asset-free list parsing and HTTP bridge regression.
## godot --headless --path godot --script res://tests/online_list_test.gd
extends SceneTree

const OnlineList := preload("res://scripts/online_list.gd")
const OnlineHttp := preload("res://scripts/online_http.gd")
const SocialList := preload("res://scripts/online_social_list.gd")

class ShellStub:
	extends RefCounted
	var delivered: Array = []
	func online_social_status() -> Dictionary:
		return {"me": {"pid": "1111111111111111"}}

	func online_deliver(id: int, code: int, body: String, error: String) -> void:
		delivered.append({"id": id, "code": code, "body": body, "error": error})

	func platform_deliver(id: int, code: int, body: String, error: String) -> void:
		online_deliver(id, code, body, error)


var checks := 0
var failures := 0


func _init() -> void:
	_run.call_deferred()


func _check(ok: bool, message: String) -> void:
	checks += 1
	if not ok:
		failures += 1
	print("LIST %s %s" % ["ok" if ok else "FAIL", message])


func _game() -> Dictionary:
	return {"id": "0123456789abcdef", "name": "Evening game", "host": "Player", "hostPlatform": null,
		"map": "maps/test", "players": 1, "openSlots": 3, "password": false, "state": "open", "mode": "normal"}


func _body(games: Array, extra := {}) -> String:
	var data := {"v": 1, "games": games}
	data.merge(extra)
	return JSON.stringify(data)


func _parsed(text: String, mine := false) -> Dictionary:
	return OnlineList.parse(text.to_utf8_buffer(), mine)


func _run() -> void:
	var normal := _body([_game(), _game()], {"extra": "kept"})
	var parsed := _parsed(normal)
	_check(parsed.ok and parsed.data == JSON.parse_string(normal) and parsed.text == normal, "normal list and unknown fields unchanged")
	_check(_parsed(_body([])).data.games == [], "empty list stays empty")
	var older := _game()
	older.erase("mode")
	older.erase("hostPlatform")
	_check(_parsed(_body([older])).data.games.size() == 1, "older optional fields remain optional")
	for field in ["name", "host", "map"]:
		for escape in ["\\ud800", "\\uDBFF", "\\udc00", "\\uDFFF"]:
			var damaged := _game()
			damaged[field] = "BROKEN"
			var text := _body([_game(), damaged]).replace("BROKEN", escape)
			parsed = _parsed(text)
			_check(parsed.ok and parsed.data.games.size() == 2 and parsed.data.games[1][field] == "�", "%s %s repaired without losing games" % [field, escape])
	for pair in ["\\ud800\\udc00", "\\uDBFF\\uDFFF", "\\ud83D\\uDe00"]:
		var text := _body([_game()]).replace("Evening game", pair)
		parsed = _parsed(text)
		_check(parsed.ok and parsed.data == JSON.parse_string(text) and parsed.text == text, "valid pair %s unchanged" % pair)
	for vector in [["\\ud800x", "�x"], ["\\ud800\\u0041", "�A"], ["\\ud800\\ud800\\udc00", "�" + String.chr(0x10000)],
			["\\udc00\\ud800", "��"], ["\\\\ud800", "\\ud800"], ["\\\\\\ud800", "\\�"], ["\\\"\\ud800", "\"�"]]:
		var text := _body([_game()]).replace("Evening game", vector[0])
		parsed = _parsed(text)
		_check(parsed.ok and parsed.data.games[0].name == vector[1], "escape boundaries %s" % vector[0])
	var unicode := _body([_game()]).replace("Evening game", "Éowyn 😀 �")
	_check(_parsed(unicode).text == unicode, "ordinary UTF-8 and replacement character unchanged")
	for invalid in [PackedByteArray([0xff]), PackedByteArray([0xc0, 0xaf]), PackedByteArray([0xed, 0xa0, 0x80]), PackedByteArray([0xf4, 0x90, 0x80, 0x80]), PackedByteArray([0xe2, 0x82])]:
		var bytes := '{"v":1,"games":['.to_utf8_buffer()
		bytes.append_array(JSON.stringify(_game()).replace("Evening game", "BROKEN").get_slice("BROKEN", 0).to_utf8_buffer())
		bytes.append_array(invalid)
		bytes.append_array(JSON.stringify(_game()).get_slice("Evening game", 1).to_utf8_buffer())
		bytes.append_array(("," + JSON.stringify(_game()) + "]}").to_utf8_buffer())
		parsed = OnlineList.parse(bytes)
		_check(parsed.ok and parsed.data.games.size() == 2 and parsed.data.games[0].name == "�".repeat(invalid.size()), "invalid UTF-8 %s replaced" % invalid.hex_encode())
	for field in _game():
		var missing := _game()
		missing.erase(field)
		if field in ["mode", "hostPlatform"]:
			continue
		parsed = _parsed(_body([_game(), missing, _game()]))
		_check(parsed.ok and parsed.data.games == JSON.parse_string(_body([_game(), _game()])).games, "missing %s skips only that game" % field)
	for change in [{"name": 1}, {"host": []}, {"map": null}, {"state": true}, {"id": "not-an-id"}, {"players": "1"}, {"players": 1.5},
			{"players": -1}, {"players": 9}, {"openSlots": {}}, {"password": 0}, {"mode": "other"}, {"mode": []}, {"hostPlatform": 5}, {"name": "x".repeat(49)}]:
		var bad := _game()
		bad.merge(change, true)
		parsed = _parsed(_body([bad, _game()]))
		_check(parsed.ok and parsed.data.games.size() == 1 and parsed.data.games[0].name == "Evening game", "wrong field %s skips only that game" % str(change))
	parsed = _parsed(_body([null, "text", 5, [], _game()]))
	_check(parsed.ok and parsed.data.games.size() == 1, "non-object entries skipped independently")
	_check(_parsed(_body([{}])).data.games == [], "all invalid entries leave a valid empty list")
	for bad in ["garbage", "null", "[]", "1", "{}", '{"v":2,"games":[]}', '{"v":"1","games":[]}', '{"v":true,"games":[]}',
			'{"v":1}', '{"v":1,"games":{}}', '{"v":1,"games":null}', '{"v":1,"games":[', '{"v":1,"games":[],"x":"\\uZZZZ"}',
			_body([_game()], {"left": {}})]:
		parsed = _parsed(bad, true)
		_check(not parsed.ok and not parsed.error.is_empty(), "bad top-level shape fails clearly: %s" % bad.c_escape().left(65))
	for bytes in [normal.to_utf8_buffer() + PackedByteArray([0]) + "garbage".to_utf8_buffer(),
			'{"v":1,"games":[],"name":"'.to_utf8_buffer() + PackedByteArray([0]) + '"}'.to_utf8_buffer()]:
		_check(not OnlineList.parse(bytes).ok, "raw NUL does not truncate or become valid JSON")
	var many: Array = []
	many.resize(201)
	many.fill(_game())
	_check(not _parsed(_body(many)).ok, "oversize game list rejected before filtering")
	var mine := _game()
	mine.slot = 1
	mine.state = "started"
	mine.rejoin = "/v1/games/0123456789abcdef/rejoin"
	var mine_text := _body([mine], {"left": ["fedcba9876543210"]})
	_check(_parsed(mine_text, true).text == mine_text, "normal mine list unchanged")
	for slot in [null, "1", -1, 0.5, 8]:
		var bad := mine.duplicate()
		bad.slot = slot
		parsed = _parsed(_body([bad, mine]), true)
		_check(parsed.ok and parsed.data.games.size() == 1, "invalid rejoin slot %s skipped" % str(slot))
	parsed = _parsed(_body([_game(), mine], {"left": [null, "fedcba9876543210", "bad"]}), true)
	_check(parsed.ok and parsed.data.games.size() == 1 and parsed.data.left == ["fedcba9876543210"], "missing rejoin slot and invalid left ids skipped independently")
	_check(not _parsed(_body(many.slice(0, 33)), true).ok, "mine limit retained")
	await _transport_tests(normal, mine_text)
	await _social_tests()
	print("ONLINE LIST: %d passed, %d failed, %d checks" % [checks - failures, failures, checks])
	quit(0 if failures == 0 else 1)


## A real HTTPRequest and loopback TCP server, without retail data or the native menu extension.
func _exchange(path: String, body: PackedByteArray, code := 200, kind := "lobby", method := "GET", channel := "ordinary") -> Dictionary:
	var server := TCPServer.new()
	var listening := server.listen(0, "127.0.0.1")
	if listening != OK:
		_check(false, "loopback server: " + error_string(listening))
		return {"body": "", "error": "test server failed"}
	var stub := ShellStub.new()
	var transport := OnlineHttp.new()
	transport.shell = stub
	transport.kind = kind
	transport.lobby_url = "http://127.0.0.1:%d" % server.get_local_port()
	transport._http = HTTPRequest.new()
	transport._inbox = transport._http
	transport._held = transport._http
	root.add_child(transport._http)
	transport._http.request_completed.connect(transport._on_done.bind(channel))
	transport._send({"id": 17, "method": method, "path": path, "body": "", "token": "", "channel": channel, "held": channel == "room"})
	var peer: StreamPeerTCP
	var deadline := Time.get_ticks_msec() + 5000
	while stub.delivered.is_empty() and Time.get_ticks_msec() < deadline:
		if peer == null and server.is_connection_available():
			peer = server.take_connection()
		if peer != null:
			peer.poll()
			if peer.get_available_bytes() > 0:
				peer.get_data(peer.get_available_bytes())
				peer.put_data(("HTTP/1.1 %d OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % [code, body.size()]).to_utf8_buffer() + body)
		await process_frame
	_check(stub.delivered.size() == 1 and stub.delivered[0].id == 17 and transport._list_requests.is_empty(), "HTTP %s %s delivered once and cleared request metadata" % [method, path])
	var result: Dictionary = stub.delivered[0] if not stub.delivered.is_empty() else {"body": "", "error": "test timed out"}
	transport._http.queue_free()
	transport.free()
	server.stop()
	return result


func _transport_tests(normal: String, mine: String) -> void:
	var reply := await _exchange("/v1/games", normal.to_utf8_buffer())
	_check(reply.body == normal and reply.error == "", "normal HTTP list unchanged")
	var bad := _game()
	bad.erase("players")
	var mixed := _body([_game(), bad, _game()]).replace("Evening game", "\\ud800")
	for path in ["/v1/games", "/v1/games?running=1"]:
		reply = await _exchange(path, mixed.to_utf8_buffer())
		var data: Variant = JSON.parse_string(reply.body)
		_check(reply.error == "" and data is Dictionary and data.games.size() == 2 and data.games[0].name == "�", "browser HTTP repairs text and drops only malformed row: " + path)
	reply = await _exchange("/v1/mine", mine.replace("Evening game", "\\udc00").to_utf8_buffer())
	_check(reply.error == "" and JSON.parse_string(reply.body).games[0].name == "�", "rejoin/session HTTP list repaired")
	reply = await _exchange("/v1/games", '{"v":1,"games":{}}'.to_utf8_buffer())
	_check(reply.body == "" and reply.error.contains("games"), "bad list envelope reaches shell as explicit error")
	for options in [["/v1/games", 403, "lobby", "GET"], ["/v1/games/0123456789abcdef", 200, "lobby", "GET"],
			["/v1/games", 200, "lobby", "POST"], ["/v1/games", 200, "platform", "GET"]]:
		var body := '{"v":1,"error":"unchanged","text":"\\ud800"}'
		reply = await _exchange(options[0], body.to_utf8_buffer(), options[1], options[2], options[3])
		_check(reply.body == body and reply.error == "", "non-list or failed HTTP reply retains strict downstream parsing: %s" % str(options))


func _social_fixtures() -> Array:
	var me := {"pid": "1111111111111111", "nick": "Me", "code": "0123456789"}
	var player := {"pid": "2222222222222222", "nick": "Friend", "code": "0123456789"}
	var presence := {"pid": player.pid, "state": "online", "game": null, "map": "Friend", "mode": null, "joinable": false, "since": 1}
	var friend := player.duplicate(true)
	friend.merge({"since": 1, "presence": presence})
	var recent := player.duplicate(true)
	recent.target = "roster-id"
	var block := player.duplicate(true)
	block.merge({"note": "Friend", "at": 1})
	var thread := player.duplicate(true)
	thread.merge({"last_id": 1, "read_id": 0})
	var inbox := {"v": 1, "seq": "aaaaaaaaaaaaaaaa:1", "reset": true,
		"presence": [presence], "requests": [{"id": "3333333333333333", "from": player, "to": me, "at": 1}],
		"invites": [{"id": "4444444444444444", "from": player, "to": me, "game": "5555555555555555", "kind": "invite", "mode": null, "note": "Friend", "expires": 1}],
		"notices": [{"id": "6666666666666666", "kind": "expired", "invite": null, "at": 1}], "chat": [{"pid": player.pid, "last_id": 1, "read_id": 0}],
		"stats": {"players": 1, "games_open": 0, "games_running": 0, "as_of": 1}, "wait": 25}
	return [
		["/v1/friends", "friends", {"v": 1, "friends": [friend]}],
		["/v1/recent", "games", {"v": 1, "games": [{"game": "5555555555555555", "at": 1, "players": [recent]}]}],
		["/v1/blocks?limit=50&after=aaaaaaaaaaaaaaaa", "blocks", {"v": 1, "blocks": [block], "next": null}],
		["/v1/chat?limit=50", "threads", {"v": 1, "threads": [thread], "next": null}],
		["/v1/chat/2222222222222222/messages?after=0&limit=50", "messages", {"v": 1, "messages": [{"id": 1, "from": player.pid, "to": me.pid, "text": "Friend", "at": 1, "client_id": "a".repeat(32)}], "next": null, "last_id": 1, "read_id": 0}],
		["/v1/inbox?wait=25", "presence", inbox],
	]


func _social_tests() -> void:
	var me := "1111111111111111"
	for path in ["/v1/chat/2222222222222222", "/v1/chat/2222222222222222/read", "/v1/chat/2222222222222222/messages/extra", "/v1/chat/invalid/messages"]:
		_check(not SocialList.handles(path), "non-list chat paths are not intercepted: " + path)
	for fixture in _social_fixtures():
		var path: String = fixture[0]
		var key: String = fixture[1]
		var normal: Dictionary = fixture[2]
		_check(SocialList.handles(path), "current native list endpoint is intercepted: " + path)
		var text := JSON.stringify(normal)
		var parsed := SocialList.parse(text.to_utf8_buffer(), path, me)
		_check(parsed.ok and parsed.data == JSON.parse_string(text), "normal social list unchanged: " + path)
		for escape in ["\\ud800", "\\uDBFF", "\\udc00", "\\uDFFF"]:
			parsed = SocialList.parse(text.replace("Friend", escape).to_utf8_buffer(), path, me)
			_check(parsed.ok and parsed.data[key].size() == 1 and parsed.text.contains("�"), "social surrogate repaired: " + path + " " + escape)
		var spans := text.split("Friend", true, 1)
		parsed = SocialList.parse(spans[0].to_utf8_buffer() + PackedByteArray([0xff]) + spans[1].to_utf8_buffer(), path, me)
		_check(parsed.ok and parsed.text.contains("�"), "social invalid UTF-8 repaired: " + path)
		var mixed := normal.duplicate(true)
		mixed[key].push_front({})
		mixed[key].append(null)
		parsed = SocialList.parse(JSON.stringify(mixed).to_utf8_buffer(), path, me)
		_check(parsed.ok and parsed.data[key] == JSON.parse_string(text)[key], "malformed social rows skipped: " + path)
		for wrong in ["bad", [], null, {"v": 2}, {"v": 1}]:
			parsed = SocialList.parse(JSON.stringify(wrong).to_utf8_buffer(), path, me)
			_check(not parsed.ok and not parsed.error.is_empty(), "bad social envelope fails: " + path)
		mixed = normal.duplicate(true)
		mixed[key] = {}
		_check(not SocialList.parse(JSON.stringify(mixed).to_utf8_buffer(), path, me).ok, "non-array collection fails: " + path)
		mixed = normal.duplicate(true)
		var bad: Dictionary = mixed[key][0].duplicate(true)
		if key == "games": bad.at = "wrong type"
		elif key == "messages": bad.text = 17
		else: bad.pid = 17
		mixed[key].push_front(bad)
		parsed = SocialList.parse(JSON.stringify(mixed).to_utf8_buffer(), path, me)
		_check(parsed.ok and parsed.data[key] == JSON.parse_string(text)[key], "wrongly typed social row skipped: " + path)
		_check(not SocialList.parse(text.to_utf8_buffer() + PackedByteArray([0]), path, me).ok, "raw social NUL fails: " + path)
		var reply := await _exchange(path, JSON.stringify(normal).replace("Friend", "\\ud800").to_utf8_buffer(), 200, "lobby", "GET", "social" if key == "presence" else "ordinary")
		_check(reply.error == "" and reply.body.contains("�"), "current social HTTP channel repairs list: " + path)
		reply = await _exchange(path, '{"v":1}'.to_utf8_buffer(), 200, "lobby", "GET", "social" if key == "presence" else "ordinary")
		_check(not reply.error.is_empty() and reply.body.is_empty(), "current social HTTP channel reports invalid envelope: " + path)
	var inbox: Dictionary = _social_fixtures().back()[2].duplicate(true)
	for key in ["presence", "requests", "invites", "notices", "chat"]:
		var mixed := inbox.duplicate(true)
		mixed[key].append(mixed[key][0])
		mixed[key].append({"id": 4, "pid": false})
		var parsed := SocialList.parse(JSON.stringify(mixed).to_utf8_buffer(), "/v1/inbox", me)
		_check(parsed.ok and parsed.data == JSON.parse_string(JSON.stringify(inbox)), "inbox skips malformed and duplicate " + key)
	var recent: Dictionary = _social_fixtures()[1][2].duplicate(true)
	recent.games[0].players.append({})
	_check(SocialList.parse(JSON.stringify(recent).to_utf8_buffer(), "/v1/recent", me).data.games[0].players.size() == 1, "recent nested player list survives a malformed player")
	var friends: Dictionary = _social_fixtures()[0][2].duplicate(true)
	friends.friends.resize(201)
	_check(not SocialList.parse(JSON.stringify(friends).to_utf8_buffer(), "/v1/friends", me).ok, "social collection limits checked before filtering")
	for field in ["seq", "reset", "stats", "backoff", "wait"]:
		var mixed := inbox.duplicate(true)
		mixed[field] = "invalid"
		_check(not SocialList.parse(JSON.stringify(mixed).to_utf8_buffer(), "/v1/inbox", me).ok, "inbox metadata remains strict: " + field)
	var private_data := inbox.duplicate(true)
	private_data.presence.append({"device": "synthetic-private-field"})
	_check(not SocialList.parse(JSON.stringify(private_data).to_utf8_buffer(), "/v1/inbox", me).ok, "privacy guard runs before discarding malformed rows")
	var chat: Dictionary = _social_fixtures()[4][2].duplicate(true)
	chat.messages.append({"id": 2, "text": 3})
	chat.last_id = 3
	chat.next = 2
	var page := SocialList.parse(JSON.stringify(chat).to_utf8_buffer(), "/v1/chat/2222222222222222/messages?after=0", me)
	_check(page.ok and page.data.messages.size() == 1 and page.data.next == 1 and page.data.last_id == 3, "discarded chat tail retains server watermark and usable visible cursor")
	for options in [["/v1/friends", 403, "lobby", "GET"], ["/v1/inbox", 200, "lobby", "POST"], ["/v1/chat/2222222222222222/messages", 200, "lobby", "POST"], ["/v1/friends", 200, "platform", "GET"], ["/v1/me", 200, "lobby", "GET"]]:
		var body := '{"v":1,"text":"\\ud800"}'
		var reply := await _exchange(options[0], body.to_utf8_buffer(), options[1], options[2], options[3])
		_check(reply.body == body and reply.error == "", "social actions/errors/profile stay strict: " + str(options))
